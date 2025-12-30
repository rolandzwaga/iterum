// ==============================================================================
// Layer 4: User Feature Tests - ReverseDelay
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/reverse_delay.h"

#include <cmath>
#include <array>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Phase 3: User Story 1 Tests - Basic Reverse Echo
// =============================================================================

TEST_CASE("ReverseDelay lifecycle", "[reverse-delay][lifecycle]") {
    ReverseDelay delay;

    SECTION("prepare succeeds") {
        delay.prepare(44100.0, 512, 2000.0f);
        REQUIRE(true);  // If we get here, prepare succeeded
    }

    SECTION("reset after prepare succeeds") {
        delay.prepare(44100.0, 512, 2000.0f);
        delay.reset();
        REQUIRE(true);
    }

    SECTION("can be re-prepared") {
        delay.prepare(44100.0, 512, 1000.0f);
        delay.prepare(96000.0, 1024, 2000.0f);
        REQUIRE(true);
    }
}

TEST_CASE("ReverseDelay basic processing (US1)", "[reverse-delay][US1][processing]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(10.0f);  // 441 samples for fast testing
    delay.setDryWetMix(100.0f);    // 100% wet for clear verification
    delay.setFeedbackAmount(0.0f); // No feedback for basic test
    delay.snapParameters();

    SECTION("outputs zero during first chunk capture") {
        std::vector<float> left(441, 1.0f);
        std::vector<float> right(441, 1.0f);

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Output should be mostly zero (capturing first chunk)
        // Allow small values due to any dry bleed
        float maxOutput = 0.0f;
        for (float sample : left) {
            maxOutput = std::max(maxOutput, std::abs(sample));
        }
        REQUIRE(maxOutput < 0.01f);
    }

    SECTION("outputs reversed audio after first chunk") {
        // Process first chunk with known values
        std::vector<float> left(441);
        std::vector<float> right(441);

        for (size_t i = 0; i < 441; ++i) {
            left[i] = static_cast<float>(i);
            right[i] = static_cast<float>(i);
        }

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Process second chunk (zeros)
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);

        // Output should be reversed first chunk
        // Note: FlexibleFeedbackNetwork has ~1ms minimum delay (~44 samples),
        // so we verify the pattern is reversed (decreasing) rather than exact values

        // Find the peak (start of reversed output)
        float maxVal = 0.0f;
        size_t peakIdx = 0;
        for (size_t i = 0; i < 100; ++i) {
            if (left[i] > maxVal) {
                maxVal = left[i];
                peakIdx = i;
            }
        }

        // Verify we got substantial output (reversed chunk)
        REQUIRE(maxVal > 350.0f);  // Should be near 440, accounting for FFN delay

        // Verify decreasing pattern after peak (characteristic of reversed playback)
        if (peakIdx + 10 < 441) {
            REQUIRE(left[peakIdx + 5] < left[peakIdx]);  // Should decrease
            REQUIRE(left[peakIdx + 10] < left[peakIdx + 5]);  // Continue decreasing
        }
    }
}

TEST_CASE("ReverseDelay chunk configuration (FR-005)", "[reverse-delay][FR-005][config]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setChunkSizeMs updates chunk size") {
        delay.setChunkSizeMs(500.0f);
        REQUIRE(delay.getCurrentChunkMs() == Approx(500.0f));
    }

    SECTION("chunk size clamps to minimum (10ms)") {
        delay.setChunkSizeMs(5.0f);
        REQUIRE(delay.getCurrentChunkMs() >= 10.0f);
    }

    SECTION("chunk size clamps to maximum (2000ms)") {
        delay.setChunkSizeMs(5000.0f);
        REQUIRE(delay.getCurrentChunkMs() <= 2000.0f);
    }
}

TEST_CASE("ReverseDelay latency reporting (FR-024)", "[reverse-delay][FR-024][latency]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("latency equals chunk size") {
        delay.setChunkSizeMs(500.0f);  // 22050 samples at 44.1kHz
        delay.snapParameters();

        size_t latency = delay.getLatencySamples();
        REQUIRE(latency == 22050);
    }

    SECTION("latency updates with chunk size") {
        delay.setChunkSizeMs(100.0f);  // 4410 samples
        delay.snapParameters();
        REQUIRE(delay.getLatencySamples() == 4410);

        delay.setChunkSizeMs(200.0f);  // 8820 samples
        delay.snapParameters();
        REQUIRE(delay.getLatencySamples() == 8820);
    }
}

TEST_CASE("ReverseDelay playback modes (FR-011, FR-012, FR-013)", "[reverse-delay][modes]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("supports FullReverse mode") {
        delay.setPlaybackMode(PlaybackMode::FullReverse);
        REQUIRE(delay.getPlaybackMode() == PlaybackMode::FullReverse);
    }

    SECTION("supports Alternating mode") {
        delay.setPlaybackMode(PlaybackMode::Alternating);
        REQUIRE(delay.getPlaybackMode() == PlaybackMode::Alternating);
    }

    SECTION("supports Random mode") {
        delay.setPlaybackMode(PlaybackMode::Random);
        REQUIRE(delay.getPlaybackMode() == PlaybackMode::Random);
    }
}

TEST_CASE("ReverseDelay feedback (FR-016, FR-017)", "[reverse-delay][feedback]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("feedback amount can be set") {
        delay.setFeedbackAmount(0.5f);  // 50%
        // No crash or error
        REQUIRE(true);
    }

    SECTION("feedback above 100% is allowed (120% max)") {
        delay.setFeedbackAmount(1.2f);  // 120%
        REQUIRE(true);
    }
}

TEST_CASE("ReverseDelay dry/wet mix (FR-020)", "[reverse-delay][mix]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(10.0f);  // Small for fast testing

    SECTION("0% wet outputs dry signal only") {
        delay.setDryWetMix(0.0f);
        delay.snapParameters();

        // Process impulse
        std::vector<float> left(441, 0.0f);
        std::vector<float> right(441, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Output should preserve the impulse
        REQUIRE(left[0] == Approx(1.0f));
    }

    SECTION("100% wet outputs processed signal only") {
        delay.setDryWetMix(100.0f);
        delay.snapParameters();

        // Process first chunk
        std::vector<float> left(441, 1.0f);
        std::vector<float> right(441, 1.0f);

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // First chunk output should be near zero (capturing)
        REQUIRE(left[0] == Approx(0.0f).margin(0.01f));
    }
}

TEST_CASE("ReverseDelay filter configuration (FR-018, FR-019)", "[reverse-delay][filter]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("filter can be enabled/disabled") {
        delay.setFilterEnabled(true);
        delay.setFilterEnabled(false);
        REQUIRE(true);  // No crash
    }

    SECTION("filter cutoff can be set") {
        delay.setFilterEnabled(true);
        delay.setFilterCutoff(2000.0f);
        REQUIRE(true);  // No crash
    }

    SECTION("filter type can be set") {
        delay.setFilterEnabled(true);
        delay.setFilterType(FilterType::Lowpass);
        delay.setFilterType(FilterType::Highpass);
        REQUIRE(true);  // No crash
    }
}

TEST_CASE("ReverseDelay tempo sync (FR-006)", "[reverse-delay][FR-006][tempo]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("can set time mode to synced") {
        delay.setTimeMode(TimeMode::Synced);
        // No crash
        REQUIRE(true);
    }

    SECTION("can set note value") {
        delay.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        delay.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
        delay.setNoteValue(NoteValue::Sixteenth, NoteModifier::Triplet);
        REQUIRE(true);
    }
}

TEST_CASE("ReverseDelay reset behavior (FR-025)", "[reverse-delay][FR-025][reset]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(10.0f);
    delay.setDryWetMix(100.0f);
    delay.snapParameters();

    // Process some audio
    std::vector<float> left(441, 1.0f);
    std::vector<float> right(441, 1.0f);
    BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};

    delay.process(left.data(), right.data(), 441, ctx);
    delay.process(left.data(), right.data(), 441, ctx);

    // Reset
    delay.reset();

    // Process again - should behave like fresh start
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);
    delay.process(left.data(), right.data(), 441, ctx);

    // Output should be zero (first chunk capture)
    REQUIRE(left[0] == Approx(0.0f).margin(0.01f));
}

TEST_CASE("ReverseDelay noexcept specifications", "[reverse-delay][realtime]") {
    SECTION("constructors are noexcept") {
        static_assert(std::is_nothrow_default_constructible_v<ReverseDelay>,
                      "Default constructor must be noexcept");
        static_assert(std::is_nothrow_destructible_v<ReverseDelay>,
                      "Destructor must be noexcept");
        REQUIRE(true);
    }

    SECTION("processing method is noexcept") {
        ReverseDelay delay;
        delay.prepare(44100.0, 512, 2000.0f);
        delay.setChunkSizeMs(10.0f);

        std::vector<float> left(512, 0.0f);
        std::vector<float> right(512, 0.0f);
        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};

        static_assert(noexcept(delay.process(left.data(), right.data(), 512, ctx)),
                      "process() must be noexcept");
        REQUIRE(true);
    }
}

TEST_CASE("ReverseDelay integration with FlexibleFeedbackNetwork (FR-015)", "[reverse-delay][FR-015][integration]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(10.0f);
    delay.setFeedbackAmount(0.5f);  // 50% feedback
    delay.setDryWetMix(100.0f);
    delay.snapParameters();

    SECTION("feedback creates multiple reversed repetitions (US1-AC3)") {
        // Fill first chunk with impulse
        std::vector<float> left(441, 0.0f);
        std::vector<float> right(441, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Process more chunks and look for feedback
        float maxAmplitudeChunk2 = 0.0f;
        float maxAmplitudeChunk3 = 0.0f;

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);
        for (float s : left) maxAmplitudeChunk2 = std::max(maxAmplitudeChunk2, std::abs(s));

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);
        for (float s : left) maxAmplitudeChunk3 = std::max(maxAmplitudeChunk3, std::abs(s));

        // Should have audible output in both chunks
        REQUIRE(maxAmplitudeChunk2 > 0.1f);
        // Feedback should decay
        REQUIRE(maxAmplitudeChunk3 < maxAmplitudeChunk2);
    }
}

TEST_CASE("ReverseDelay sample rate support (SC-007)", "[reverse-delay][SC-007]") {
    const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        DYNAMIC_SECTION("Sample rate " << sr << " Hz") {
            ReverseDelay delay;
            delay.prepare(sr, 512, 2000.0f);
            delay.setChunkSizeMs(100.0f);  // 100ms
            delay.snapParameters();

            // Verify latency is correct for sample rate
            size_t expectedLatency = static_cast<size_t>(sr * 0.1);  // 100ms
            REQUIRE(delay.getLatencySamples() == expectedLatency);

            // Process some audio
            std::vector<float> left(512, 0.5f);
            std::vector<float> right(512, 0.5f);
            BlockContext ctx{.sampleRate = sr, .tempoBPM = 120.0, .isPlaying = false};

            delay.process(left.data(), right.data(), 512, ctx);

            // Should process without NaN/Inf
            for (float s : left) {
                REQUIRE(std::isfinite(s));
            }
        }
    }
}

// =============================================================================
// BUG REGRESSION TEST: Filter enabling should not break reversal
// =============================================================================
// Bug: When filter is enabled, the delayed signal no longer gets reversed.
// Expected: Filter should only apply filtering, not affect the reversal itself.
// =============================================================================

TEST_CASE("ReverseDelay filter does not break reversal", "[reverse-delay][filter][regression]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(10.0f);  // 441 samples for fast testing
    delay.setDryWetMix(100.0f);    // 100% wet
    delay.setFeedbackAmount(0.0f); // No feedback

    // Helper lambda to detect reversal - checks for decreasing pattern
    auto hasDecreasingPattern = [](const std::vector<float>& output, size_t startIdx, size_t checkLength) {
        // Find peak value and its position
        float maxVal = 0.0f;
        size_t peakIdx = startIdx;
        for (size_t i = startIdx; i < startIdx + checkLength && i < output.size(); ++i) {
            if (output[i] > maxVal) {
                maxVal = output[i];
                peakIdx = i;
            }
        }

        // Need substantial output to verify pattern
        if (maxVal < 100.0f) return false;

        // Check for decreasing pattern after peak (characteristic of reversal)
        if (peakIdx + 10 >= output.size()) return false;
        return output[peakIdx + 5] < output[peakIdx] &&
               output[peakIdx + 10] < output[peakIdx + 5];
    };

    SECTION("reversal works without filter") {
        delay.setFilterEnabled(false);
        delay.snapParameters();
        delay.reset();

        // Process first chunk with ramp (0, 1, 2, ..., 440)
        std::vector<float> left(441);
        std::vector<float> right(441);
        for (size_t i = 0; i < 441; ++i) {
            left[i] = static_cast<float>(i);
            right[i] = static_cast<float>(i);
        }

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Process second chunk (zeros) - should output reversed first chunk
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);

        // Verify reversed pattern (decreasing after peak)
        REQUIRE(hasDecreasingPattern(left, 0, 100));
    }

    SECTION("reversal still works with filter enabled") {
        delay.setFilterEnabled(true);
        delay.setFilterCutoff(4000.0f);  // Reasonable lowpass cutoff
        delay.setFilterType(FilterType::Lowpass);
        delay.snapParameters();
        delay.reset();

        // Process first chunk with ramp (0, 1, 2, ..., 440)
        std::vector<float> left(441);
        std::vector<float> right(441);
        for (size_t i = 0; i < 441; ++i) {
            left[i] = static_cast<float>(i);
            right[i] = static_cast<float>(i);
        }

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Process second chunk (zeros) - should output reversed first chunk
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);

        // Verify reversed pattern still present (filter should NOT break reversal)
        // The pattern may be smoothed by the lowpass, but should still decrease
        REQUIRE(hasDecreasingPattern(left, 0, 100));
    }

    SECTION("reversal works with highpass filter") {
        delay.setFilterEnabled(true);
        delay.setFilterCutoff(200.0f);  // Low highpass cutoff
        delay.setFilterType(FilterType::Highpass);
        delay.snapParameters();
        delay.reset();

        // Process first chunk with ramp
        std::vector<float> left(441);
        std::vector<float> right(441);
        for (size_t i = 0; i < 441; ++i) {
            left[i] = static_cast<float>(i);
            right[i] = static_cast<float>(i);
        }

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
        delay.process(left.data(), right.data(), 441, ctx);

        // Process second chunk
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 441, ctx);

        // With highpass filter, the ramp output will be transformed,
        // but there should still be substantial output from the reversal
        float maxOutput = 0.0f;
        for (float s : left) {
            maxOutput = std::max(maxOutput, std::abs(s));
        }
        // Should have audible output (reversal is happening)
        REQUIRE(maxOutput > 50.0f);
    }
}
