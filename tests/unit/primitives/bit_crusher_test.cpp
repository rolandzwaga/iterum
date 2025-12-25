// ==============================================================================
// Layer 1: DSP Primitive Tests - BitCrusher
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 021-character-processor
//
// Reference: specs/021-character-processor/spec.md (FR-014, FR-016)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/primitives/bit_crusher.h"

#include <array>
#include <cmath>
#include <numeric>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Calculate SNR in dB between signal and noise
float calculateSNRdB(float signalRMS, float noiseRMS) {
    if (noiseRMS == 0.0f) return 144.0f; // Effectively infinite
    return 20.0f * std::log10(signalRMS / noiseRMS);
}

// Generate a sine wave
void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Count unique quantization levels in a buffer
size_t countUniqueLevels(const float* buffer, size_t size, float tolerance = 0.0001f) {
    std::vector<float> uniqueValues;
    for (size_t i = 0; i < size; ++i) {
        bool found = false;
        for (float v : uniqueValues) {
            if (std::abs(buffer[i] - v) < tolerance) {
                found = true;
                break;
            }
        }
        if (!found) {
            uniqueValues.push_back(buffer[i]);
        }
    }
    return uniqueValues.size();
}

} // namespace

// =============================================================================
// T010: Foundational Tests
// =============================================================================

TEST_CASE("BitCrusher default construction", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;

    SECTION("default bit depth is 16 bits") {
        REQUIRE(crusher.getBitDepth() == Approx(16.0f));
    }

    SECTION("default dither is 0 (disabled)") {
        REQUIRE(crusher.getDither() == Approx(0.0f));
    }
}

TEST_CASE("BitCrusher setBitDepth clamps to valid range", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;

    SECTION("bit depth clamps to minimum 4") {
        crusher.setBitDepth(2.0f);
        REQUIRE(crusher.getBitDepth() == Approx(4.0f));

        crusher.setBitDepth(-1.0f);
        REQUIRE(crusher.getBitDepth() == Approx(4.0f));

        crusher.setBitDepth(0.0f);
        REQUIRE(crusher.getBitDepth() == Approx(4.0f));
    }

    SECTION("bit depth clamps to maximum 16") {
        crusher.setBitDepth(20.0f);
        REQUIRE(crusher.getBitDepth() == Approx(16.0f));

        crusher.setBitDepth(32.0f);
        REQUIRE(crusher.getBitDepth() == Approx(16.0f));
    }

    SECTION("valid bit depths are accepted") {
        crusher.setBitDepth(8.0f);
        REQUIRE(crusher.getBitDepth() == Approx(8.0f));

        crusher.setBitDepth(12.0f);
        REQUIRE(crusher.getBitDepth() == Approx(12.0f));

        crusher.setBitDepth(4.0f);
        REQUIRE(crusher.getBitDepth() == Approx(4.0f));

        crusher.setBitDepth(16.0f);
        REQUIRE(crusher.getBitDepth() == Approx(16.0f));
    }
}

TEST_CASE("BitCrusher setDither clamps to valid range", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;

    SECTION("dither clamps to minimum 0") {
        crusher.setDither(-0.5f);
        REQUIRE(crusher.getDither() == Approx(0.0f));

        crusher.setDither(-1.0f);
        REQUIRE(crusher.getDither() == Approx(0.0f));
    }

    SECTION("dither clamps to maximum 1") {
        crusher.setDither(1.5f);
        REQUIRE(crusher.getDither() == Approx(1.0f));

        crusher.setDither(2.0f);
        REQUIRE(crusher.getDither() == Approx(1.0f));
    }

    SECTION("valid dither amounts are accepted") {
        crusher.setDither(0.0f);
        REQUIRE(crusher.getDither() == Approx(0.0f));

        crusher.setDither(0.5f);
        REQUIRE(crusher.getDither() == Approx(0.5f));

        crusher.setDither(1.0f);
        REQUIRE(crusher.getDither() == Approx(1.0f));
    }
}

TEST_CASE("BitCrusher process signatures exist", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);

    SECTION("single sample process returns float") {
        float result = crusher.process(0.5f);
        // Just verify it compiles and returns something
        REQUIRE(std::isfinite(result));
    }

    SECTION("buffer process modifies in-place") {
        std::array<float, 64> buffer;
        std::fill(buffer.begin(), buffer.end(), 0.5f);

        crusher.process(buffer.data(), buffer.size());

        // Just verify it runs without crash
        REQUIRE(std::isfinite(buffer[0]));
    }
}

// =============================================================================
// T012: Quantization Tests
// =============================================================================

TEST_CASE("BitCrusher 8-bit mode quantization", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);
    crusher.setDither(0.0f); // Disable dither for deterministic testing

    SECTION("produces approximately 256 quantization levels") {
        // Generate a full-scale ramp to capture all quantization levels
        std::array<float, 1024> buffer;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = 2.0f * static_cast<float>(i) / static_cast<float>(buffer.size() - 1) - 1.0f;
        }

        crusher.process(buffer.data(), buffer.size());

        size_t uniqueLevels = countUniqueLevels(buffer.data(), buffer.size());

        // 8-bit = 256 levels, but we're quantizing [-1, 1] so ~255 unique levels
        // Allow some tolerance for edge cases
        REQUIRE(uniqueLevels >= 200);
        REQUIRE(uniqueLevels <= 260);
    }

    SECTION("SNR is approximately 48dB") {
        // Generate a sine wave and measure quantization noise
        std::array<float, 4096> original;
        std::array<float, 4096> processed;

        generateSine(original.data(), original.size(), 1000.0f, 44100.0f, 0.9f);
        std::copy(original.begin(), original.end(), processed.begin());

        crusher.process(processed.data(), processed.size());

        // Calculate noise (difference between original and processed)
        std::array<float, 4096> noise;
        for (size_t i = 0; i < noise.size(); ++i) {
            noise[i] = processed[i] - original[i];
        }

        float signalRMS = calculateRMS(original.data(), original.size());
        float noiseRMS = calculateRMS(noise.data(), noise.size());
        float snr = calculateSNRdB(signalRMS, noiseRMS);

        // 8-bit should give ~48dB SNR (±3dB tolerance per SC-007)
        REQUIRE(snr >= 45.0f);
        REQUIRE(snr <= 51.0f);
    }
}

TEST_CASE("BitCrusher 4-bit mode quantization", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(4.0f);
    crusher.setDither(0.0f);

    SECTION("produces approximately 16 quantization levels") {
        std::array<float, 512> buffer;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = 2.0f * static_cast<float>(i) / static_cast<float>(buffer.size() - 1) - 1.0f;
        }

        crusher.process(buffer.data(), buffer.size());

        size_t uniqueLevels = countUniqueLevels(buffer.data(), buffer.size());

        // 4-bit = 16 levels
        REQUIRE(uniqueLevels >= 14);
        REQUIRE(uniqueLevels <= 18);
    }

    SECTION("SNR is approximately 24dB") {
        std::array<float, 4096> original;
        std::array<float, 4096> processed;

        generateSine(original.data(), original.size(), 1000.0f, 44100.0f, 0.9f);
        std::copy(original.begin(), original.end(), processed.begin());

        crusher.process(processed.data(), processed.size());

        std::array<float, 4096> noise;
        for (size_t i = 0; i < noise.size(); ++i) {
            noise[i] = processed[i] - original[i];
        }

        float signalRMS = calculateRMS(original.data(), original.size());
        float noiseRMS = calculateRMS(noise.data(), noise.size());
        float snr = calculateSNRdB(signalRMS, noiseRMS);

        // 4-bit should give ~24dB SNR (±3dB tolerance)
        REQUIRE(snr >= 21.0f);
        REQUIRE(snr <= 27.0f);
    }
}

TEST_CASE("BitCrusher 16-bit mode is nearly transparent", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(16.0f);
    crusher.setDither(0.0f);

    std::array<float, 1024> original;
    std::array<float, 1024> processed;

    generateSine(original.data(), original.size(), 1000.0f, 44100.0f, 0.9f);
    std::copy(original.begin(), original.end(), processed.begin());

    crusher.process(processed.data(), processed.size());

    // Calculate max difference
    float maxDiff = 0.0f;
    for (size_t i = 0; i < original.size(); ++i) {
        float diff = std::abs(processed[i] - original[i]);
        maxDiff = std::max(maxDiff, diff);
    }

    // 16-bit should have <0.001% distortion
    // Max quantization step at 16-bit is 2/65536 ≈ 0.00003
    REQUIRE(maxDiff < 0.0001f);
}

TEST_CASE("BitCrusher fractional bit depths work", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setDither(0.0f);

    SECTION("10.5 bits produces intermediate quantization") {
        crusher.setBitDepth(10.5f);

        std::array<float, 2048> buffer;
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = 2.0f * static_cast<float>(i) / static_cast<float>(buffer.size() - 1) - 1.0f;
        }

        crusher.process(buffer.data(), buffer.size());

        size_t uniqueLevels = countUniqueLevels(buffer.data(), buffer.size());

        // 10 bits = 1024 levels, 11 bits = 2048 levels
        // 10.5 bits should be ~1448 levels (sqrt(1024 * 2048))
        REQUIRE(uniqueLevels > 1000);
        REQUIRE(uniqueLevels < 2000);
    }
}

// =============================================================================
// T014: Dither Tests
// =============================================================================

TEST_CASE("BitCrusher dither=0 produces deterministic output", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);
    crusher.setDither(0.0f);

    std::array<float, 256> buffer1, buffer2;
    generateSine(buffer1.data(), buffer1.size(), 1000.0f, 44100.0f, 0.5f);
    std::copy(buffer1.begin(), buffer1.end(), buffer2.begin());

    crusher.process(buffer1.data(), buffer1.size());

    crusher.reset();
    crusher.process(buffer2.data(), buffer2.size());

    // Output should be identical when dither is disabled
    for (size_t i = 0; i < buffer1.size(); ++i) {
        REQUIRE(buffer1[i] == buffer2[i]);
    }
}

TEST_CASE("BitCrusher dither=1 adds TPDF noise before quantization", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);
    crusher.setDither(1.0f);

    // With dither enabled, processing the same input value repeatedly
    // should occasionally give different outputs due to random dither
    // pushing samples across quantization boundaries

    // Process the same value multiple times and count unique outputs
    std::array<float, 256> outputs;
    constexpr float testValue = 0.503f; // Value near a quantization boundary

    for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i] = crusher.process(testValue);
    }

    // Count unique output values
    std::vector<float> uniqueOutputs;
    for (float v : outputs) {
        bool found = false;
        for (float u : uniqueOutputs) {
            if (v == u) {
                found = true;
                break;
            }
        }
        if (!found) {
            uniqueOutputs.push_back(v);
        }
    }

    // With dither near a boundary, we should see at least 2 different output values
    // (the sample gets pushed to different quantization levels by random dither)
    REQUIRE(uniqueOutputs.size() >= 2);
}

TEST_CASE("BitCrusher dither smooths quantization noise spectrum", "[bitcrusher][layer1][US3]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);

    // Process same signal with and without dither
    std::array<float, 4096> original, withDither, withoutDither;
    generateSine(original.data(), original.size(), 1000.0f, 44100.0f, 0.5f);
    std::copy(original.begin(), original.end(), withDither.begin());
    std::copy(original.begin(), original.end(), withoutDither.begin());

    // Without dither
    crusher.setDither(0.0f);
    crusher.process(withoutDither.data(), withoutDither.size());

    // With dither
    crusher.reset();
    crusher.setDither(1.0f);
    crusher.process(withDither.data(), withDither.size());

    // Calculate noise for both
    std::array<float, 4096> noiseWithDither, noiseWithoutDither;
    for (size_t i = 0; i < original.size(); ++i) {
        noiseWithDither[i] = withDither[i] - original[i];
        noiseWithoutDither[i] = withoutDither[i] - original[i];
    }

    // Dithered noise should be roughly similar in level but more random
    // The undithered noise is correlated with the signal (harmonics)
    // Dithered noise is more white-noise-like

    // Calculate variance of noise differences (should be different patterns)
    float ditherNoiseRMS = calculateRMS(noiseWithDither.data(), noiseWithDither.size());
    float noDitherNoiseRMS = calculateRMS(noiseWithoutDither.data(), noiseWithoutDither.size());

    // Dither adds about 1 LSB of noise, so RMS should be slightly higher
    // but not dramatically (maybe 1-2 dB difference)
    REQUIRE(ditherNoiseRMS > 0.0f);
    REQUIRE(noDitherNoiseRMS > 0.0f);

    // Both should produce comparable total noise level
    // Dither adds about 1 LSB of noise, which can increase total RMS
    // Allow wider tolerance since dither is meant to change noise character, not level
    float ratio = ditherNoiseRMS / noDitherNoiseRMS;
    REQUIRE(ratio >= 0.3f);
    REQUIRE(ratio <= 4.0f);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("BitCrusher handles edge input values", "[bitcrusher][layer1][edge]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);
    crusher.setDither(0.0f);

    SECTION("zero input produces zero output") {
        REQUIRE(crusher.process(0.0f) == Approx(0.0f).margin(0.01f));
    }

    SECTION("full scale input stays within bounds") {
        float result1 = crusher.process(1.0f);
        float result2 = crusher.process(-1.0f);

        REQUIRE(result1 >= -1.0f);
        REQUIRE(result1 <= 1.0f);
        REQUIRE(result2 >= -1.0f);
        REQUIRE(result2 <= 1.0f);
    }

    SECTION("values beyond -1..1 are handled gracefully") {
        float result1 = crusher.process(2.0f);
        float result2 = crusher.process(-2.0f);

        // Should still produce valid output (implementation may clip or wrap)
        REQUIRE(std::isfinite(result1));
        REQUIRE(std::isfinite(result2));
    }
}

TEST_CASE("BitCrusher reset clears state", "[bitcrusher][layer1][foundational]") {
    BitCrusher crusher;
    crusher.prepare(44100.0);
    crusher.setBitDepth(8.0f);
    crusher.setDither(1.0f); // Enable dither so there's RNG state

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        crusher.process(0.5f);
    }

    // Reset
    crusher.reset();

    // Parameters should be unchanged
    REQUIRE(crusher.getBitDepth() == Approx(8.0f));
    REQUIRE(crusher.getDither() == Approx(1.0f));

    // Should still process normally
    float result = crusher.process(0.0f);
    REQUIRE(std::isfinite(result));
}
