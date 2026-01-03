// ==============================================================================
// TapeDelay Parameter Diagnostic Test
// ==============================================================================
// Diagnostic test to verify TapeDelay parameters actually affect the output.
// This tests the user-reported issue where Wear, Age, and Splice controls
// appear to have no audible effect.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/effects/tape_delay.h>
#include <krate/dsp/core/db_utils.h>

#include <array>
#include <cmath>
#include <numeric>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

// Generate a simple impulse buffer
void generateImpulse(float* buffer, size_t numSamples) {
    std::fill(buffer, buffer + numSamples, 0.0f);
    if (numSamples > 0) {
        buffer[0] = 1.0f;
    }
}

// Generate a simple sine wave
void generateSine(float* buffer, size_t numSamples, float freq, float sampleRate) {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) / sampleRate);
    }
}

// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

// Calculate peak of a buffer
float calculatePeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

// Count non-zero samples (above threshold)
size_t countNonZero(const float* buffer, size_t numSamples, float threshold = 1e-6f) {
    size_t count = 0;
    for (size_t i = 0; i < numSamples; ++i) {
        if (std::abs(buffer[i]) > threshold) {
            count++;
        }
    }
    return count;
}

} // namespace

// =============================================================================
// Wear Parameter Tests
// =============================================================================

TEST_CASE("TapeDelay Wear parameter affects output", "[tape-delay][diagnostic][wear]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 4096;
    constexpr size_t numBlocks = 10;  // Process multiple blocks to let effects settle

    std::array<float, blockSize> leftWear0, rightWear0;
    std::array<float, blockSize> leftWear100, rightWear100;

    SECTION("Wear affects wow/flutter modulation") {
        // Process with Wear = 0
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(0.0f);
            delay.setSaturation(0.0f);
            delay.setAge(0.0f);
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);  // 0dB

            // Process several blocks with sine wave to measure modulation
            for (size_t b = 0; b < numBlocks; ++b) {
                generateSine(leftWear0.data(), blockSize, 1000.0f, sampleRate);
                generateSine(rightWear0.data(), blockSize, 1000.0f, sampleRate);
                delay.process(leftWear0.data(), rightWear0.data(), blockSize);
            }
        }

        // Process with Wear = 1
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(1.0f);  // Max wear
            delay.setSaturation(0.0f);
            delay.setAge(0.0f);
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);  // 0dB

            for (size_t b = 0; b < numBlocks; ++b) {
                generateSine(leftWear100.data(), blockSize, 1000.0f, sampleRate);
                generateSine(rightWear100.data(), blockSize, 1000.0f, sampleRate);
                delay.process(leftWear100.data(), rightWear100.data(), blockSize);
            }
        }

        // Compare: Wear=1 should have added hiss (noise floor should be higher)
        float rmsWear0 = calculateRMS(leftWear0.data(), blockSize);
        float rmsWear100 = calculateRMS(leftWear100.data(), blockSize);

        INFO("RMS at Wear=0: " << rmsWear0);
        INFO("RMS at Wear=1: " << rmsWear100);

        // Wear=1 should have audible difference (hiss adds energy)
        // At minimum, they should not be identical
        // Note: This may fail if parameters aren't actually affecting output
        REQUIRE(rmsWear0 != Approx(rmsWear100).margin(0.001f));
    }

    SECTION("Wear=1 produces audible hiss with silence input") {
        TapeDelay delay;
        delay.prepare(sampleRate, blockSize, 2000.0f);
        delay.setMotorSpeed(50.0f);  // Short delay
        delay.setWear(1.0f);  // Max wear for max hiss
        delay.setSaturation(0.0f);
        delay.setAge(0.0f);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        delay.setHeadEnabled(0, true);
        delay.setHeadLevel(0, 0.0f);

        // Process silence - should still produce hiss
        std::array<float, blockSize> left{}, right{};
        for (size_t b = 0; b < numBlocks; ++b) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), blockSize);
        }

        // Hiss should be audible at Wear=1 (level should be around -40dB = 0.01)
        float rms = calculateRMS(left.data(), blockSize);
        float rmsdB = 20.0f * std::log10(rms + 1e-10f);

        INFO("Hiss RMS: " << rms << " (" << rmsdB << " dB)");

        // At Wear=1, hiss should be at -40dB per the code
        // So RMS should be around 0.01 (give or take)
        REQUIRE(rms > 0.001f);  // Should be well above noise floor
    }
}

// =============================================================================
// Age Parameter Tests
// =============================================================================

TEST_CASE("TapeDelay Age parameter affects output", "[tape-delay][diagnostic][age]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 4096;

    SECTION("Age affects high-frequency rolloff") {
        std::array<float, blockSize> leftAge0, rightAge0;
        std::array<float, blockSize> leftAge100, rightAge100;

        // Process high-frequency content with Age = 0
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(0.0f);
            delay.setSaturation(0.0f);
            delay.setAge(0.0f);  // No age = 12kHz rolloff
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);

            // Use 8kHz sine to test HF rolloff
            generateSine(leftAge0.data(), blockSize, 8000.0f, sampleRate);
            generateSine(rightAge0.data(), blockSize, 8000.0f, sampleRate);

            // Process multiple blocks for filter to settle
            for (int i = 0; i < 5; ++i) {
                delay.process(leftAge0.data(), rightAge0.data(), blockSize);
                if (i < 4) {
                    generateSine(leftAge0.data(), blockSize, 8000.0f, sampleRate);
                    generateSine(rightAge0.data(), blockSize, 8000.0f, sampleRate);
                }
            }
        }

        // Process with Age = 1
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(0.0f);
            delay.setSaturation(0.0f);
            delay.setAge(1.0f);  // Max age = 4kHz rolloff
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);

            generateSine(leftAge100.data(), blockSize, 8000.0f, sampleRate);
            generateSine(rightAge100.data(), blockSize, 8000.0f, sampleRate);

            for (int i = 0; i < 5; ++i) {
                delay.process(leftAge100.data(), rightAge100.data(), blockSize);
                if (i < 4) {
                    generateSine(leftAge100.data(), blockSize, 8000.0f, sampleRate);
                    generateSine(rightAge100.data(), blockSize, 8000.0f, sampleRate);
                }
            }
        }

        // Age=1 should have much lower 8kHz content (rolled off at 4kHz)
        float rmsAge0 = calculateRMS(leftAge0.data(), blockSize);
        float rmsAge100 = calculateRMS(leftAge100.data(), blockSize);

        INFO("8kHz RMS at Age=0: " << rmsAge0);
        INFO("8kHz RMS at Age=1: " << rmsAge100);

        // Age=1 should significantly attenuate 8kHz (4kHz rolloff is ~12dB/octave at 8kHz)
        REQUIRE(rmsAge100 < rmsAge0 * 0.5f);  // At least 6dB reduction
    }
}

// =============================================================================
// Saturation Parameter Tests
// =============================================================================

TEST_CASE("TapeDelay Saturation parameter affects output", "[tape-delay][diagnostic][saturation]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 4096;

    SECTION("Saturation adds harmonic distortion") {
        std::array<float, blockSize> leftSat0, rightSat0;
        std::array<float, blockSize> leftSat100, rightSat100;

        // Process with Saturation = 0
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(0.0f);
            delay.setSaturation(0.0f);  // No saturation
            delay.setAge(0.0f);
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);

            // Use loud sine wave to trigger saturation
            for (size_t i = 0; i < blockSize; ++i) {
                leftSat0[i] = 0.9f * std::sin(2.0f * 3.14159265f * 500.0f * static_cast<float>(i) / sampleRate);
                rightSat0[i] = leftSat0[i];
            }

            for (int b = 0; b < 5; ++b) {
                delay.process(leftSat0.data(), rightSat0.data(), blockSize);
                if (b < 4) {
                    for (size_t i = 0; i < blockSize; ++i) {
                        leftSat0[i] = 0.9f * std::sin(2.0f * 3.14159265f * 500.0f * static_cast<float>(i) / sampleRate);
                        rightSat0[i] = leftSat0[i];
                    }
                }
            }
        }

        // Process with Saturation = 1
        {
            TapeDelay delay;
            delay.prepare(sampleRate, blockSize, 2000.0f);
            delay.setMotorSpeed(50.0f);  // Short delay so signal emerges quickly
            delay.setWear(0.0f);
            delay.setSaturation(1.0f);  // Max saturation
            delay.setAge(0.0f);
            delay.setMix(1.0f);
            delay.setFeedback(0.0f);
            delay.setHeadEnabled(0, true);
            delay.setHeadLevel(0, 0.0f);

            for (size_t i = 0; i < blockSize; ++i) {
                leftSat100[i] = 0.9f * std::sin(2.0f * 3.14159265f * 500.0f * static_cast<float>(i) / sampleRate);
                rightSat100[i] = leftSat100[i];
            }

            for (int b = 0; b < 5; ++b) {
                delay.process(leftSat100.data(), rightSat100.data(), blockSize);
                if (b < 4) {
                    for (size_t i = 0; i < blockSize; ++i) {
                        leftSat100[i] = 0.9f * std::sin(2.0f * 3.14159265f * 500.0f * static_cast<float>(i) / sampleRate);
                        rightSat100[i] = leftSat100[i];
                    }
                }
            }
        }

        // Saturation should change the waveform (peak should be compressed)
        float peakSat0 = calculatePeak(leftSat0.data(), blockSize);
        float peakSat100 = calculatePeak(leftSat100.data(), blockSize);

        INFO("Peak at Saturation=0: " << peakSat0);
        INFO("Peak at Saturation=1: " << peakSat100);

        // With saturation, peaks may be slightly different due to compression
        // The output should not be identical
        bool different = std::abs(peakSat0 - peakSat100) > 0.001f;

        // Also check RMS - saturation adds harmonics which may change RMS
        float rmsSat0 = calculateRMS(leftSat0.data(), blockSize);
        float rmsSat100 = calculateRMS(leftSat100.data(), blockSize);

        INFO("RMS at Saturation=0: " << rmsSat0);
        INFO("RMS at Saturation=1: " << rmsSat100);

        REQUIRE((different || std::abs(rmsSat0 - rmsSat100) > 0.001f));
    }
}

// =============================================================================
// Splice Parameter Tests
// =============================================================================

TEST_CASE("TapeDelay Splice parameter produces artifacts", "[tape-delay][diagnostic][splice]") {
    constexpr float sampleRate = 44100.0f;
    constexpr size_t blockSize = 4096;
    constexpr size_t numBlocks = 20;  // Need enough blocks to capture splice

    SECTION("Splice enabled produces clicks in output") {
        TapeDelay delay;
        delay.prepare(sampleRate, blockSize, 2000.0f);
        delay.setMotorSpeed(100.0f);  // Short delay for more frequent splices
        delay.setWear(0.0f);
        delay.setSaturation(0.0f);
        delay.setAge(0.0f);
        delay.setSpliceEnabled(true);
        delay.setSpliceIntensity(1.0f);  // Max intensity
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        delay.setHeadEnabled(0, true);
        delay.setHeadLevel(0, 0.0f);

        // Process silence - should still produce splice clicks
        std::array<float, blockSize> left{}, right{};
        float maxPeak = 0.0f;
        size_t totalNonZero = 0;

        for (size_t b = 0; b < numBlocks; ++b) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), blockSize);

            maxPeak = std::max(maxPeak, calculatePeak(left.data(), blockSize));
            totalNonZero += countNonZero(left.data(), blockSize);
        }

        INFO("Max peak with splice enabled: " << maxPeak);
        INFO("Total non-zero samples: " << totalNonZero);

        // Splice clicks should produce some output even with silent input
        REQUIRE(maxPeak > 0.001f);
        REQUIRE(totalNonZero > 0);
    }

    SECTION("Splice disabled produces no clicks") {
        TapeDelay delay;
        delay.prepare(sampleRate, blockSize, 2000.0f);
        delay.setMotorSpeed(100.0f);
        delay.setWear(0.0f);
        delay.setSaturation(0.0f);
        delay.setAge(0.0f);
        delay.setSpliceEnabled(false);  // Disabled
        delay.setSpliceIntensity(1.0f);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        delay.setHeadEnabled(0, true);
        delay.setHeadLevel(0, 0.0f);

        std::array<float, blockSize> left{}, right{};
        float maxPeak = 0.0f;

        for (size_t b = 0; b < numBlocks; ++b) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), blockSize);
            maxPeak = std::max(maxPeak, calculatePeak(left.data(), blockSize));
        }

        INFO("Max peak with splice disabled: " << maxPeak);

        // With splice disabled and no other artifacts, silence in = silence out
        // (excluding any residual hiss at wear=0 which should be ~-80dB)
        REQUIRE(maxPeak < 0.01f);  // Should be essentially silent
    }
}
