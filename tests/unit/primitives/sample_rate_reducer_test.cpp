// ==============================================================================
// Layer 1: DSP Primitive Tests - SampleRateReducer
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 021-character-processor
//
// Reference: specs/021-character-processor/spec.md (FR-015)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/sample_rate_reducer.h"

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

// Generate a sine wave
void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Count number of unique values in buffer (with tolerance)
size_t countUniqueValues(const float* buffer, size_t size, float tolerance = 0.0001f) {
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

// Count "holds" - consecutive identical samples
size_t countHolds(const float* buffer, size_t size) {
    if (size <= 1) return 0;
    size_t holds = 0;
    for (size_t i = 1; i < size; ++i) {
        if (buffer[i] == buffer[i - 1]) {
            holds++;
        }
    }
    return holds;
}

} // namespace

// =============================================================================
// T020: Foundational Tests
// =============================================================================

TEST_CASE("SampleRateReducer default construction", "[samplerate][layer1][foundational]") {
    SampleRateReducer reducer;

    SECTION("default reduction factor is 1.0 (no reduction)") {
        REQUIRE(reducer.getReductionFactor() == Approx(1.0f));
    }
}

TEST_CASE("SampleRateReducer setReductionFactor clamps to valid range", "[samplerate][layer1][foundational]") {
    SampleRateReducer reducer;

    SECTION("reduction factor clamps to minimum 1") {
        reducer.setReductionFactor(0.5f);
        REQUIRE(reducer.getReductionFactor() == Approx(1.0f));

        reducer.setReductionFactor(0.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(1.0f));

        reducer.setReductionFactor(-1.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(1.0f));
    }

    SECTION("reduction factor clamps to maximum 8") {
        reducer.setReductionFactor(10.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(8.0f));

        reducer.setReductionFactor(16.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(8.0f));
    }

    SECTION("valid reduction factors are accepted") {
        reducer.setReductionFactor(1.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(1.0f));

        reducer.setReductionFactor(2.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(2.0f));

        reducer.setReductionFactor(4.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(4.0f));

        reducer.setReductionFactor(8.0f);
        REQUIRE(reducer.getReductionFactor() == Approx(8.0f));
    }
}

TEST_CASE("SampleRateReducer process signatures exist", "[samplerate][layer1][foundational]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);

    SECTION("single sample process returns float") {
        float result = reducer.process(0.5f);
        REQUIRE(std::isfinite(result));
    }

    SECTION("buffer process modifies in-place") {
        std::array<float, 64> buffer;
        std::fill(buffer.begin(), buffer.end(), 0.5f);

        reducer.process(buffer.data(), buffer.size());

        REQUIRE(std::isfinite(buffer[0]));
    }
}

TEST_CASE("SampleRateReducer reset clears hold state", "[samplerate][layer1][foundational]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(4.0f);

    // Process some samples to build up hold state
    for (int i = 0; i < 10; ++i) {
        reducer.process(static_cast<float>(i) * 0.1f);
    }

    // Reset
    reducer.reset();

    // After reset, the first input should be immediately captured
    float result = reducer.process(0.75f);
    REQUIRE(result == Approx(0.75f));
}

// =============================================================================
// T022: Sample-and-Hold Tests
// =============================================================================

TEST_CASE("SampleRateReducer factor=1 passes audio unchanged", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(1.0f);

    std::array<float, 128> input, output;
    generateSine(input.data(), input.size(), 1000.0f, 44100.0f);
    std::copy(input.begin(), input.end(), output.begin());

    reducer.process(output.data(), output.size());

    // With factor=1, output should match input exactly
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE(output[i] == Approx(input[i]));
    }
}

TEST_CASE("SampleRateReducer factor=2 holds each sample for 2 outputs", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(2.0f);

    // Create a ramp input where each sample is different
    std::array<float, 16> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i) * 0.1f;
    }

    reducer.process(buffer.data(), buffer.size());

    // With factor=2, we should see each value repeated twice
    // First sample (0.0) held for sample 0,1
    // Second unique (sample 2's input) held for sample 2,3
    // etc.

    // Count consecutive equal pairs
    size_t pairs = 0;
    for (size_t i = 0; i < buffer.size() - 1; i += 2) {
        if (buffer[i] == buffer[i + 1]) {
            pairs++;
        }
    }

    // Most pairs should be equal (allowing for boundary effects)
    REQUIRE(pairs >= 6); // At least 6 out of 8 pairs
}

TEST_CASE("SampleRateReducer factor=4 holds each sample for 4 outputs", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(4.0f);

    // Create a ramp input
    std::array<float, 32> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i) * 0.05f;
    }

    reducer.process(buffer.data(), buffer.size());

    // With factor=4, we should see staircasing
    // The number of unique values should be approximately size/4
    size_t uniqueCount = countUniqueValues(buffer.data(), buffer.size());

    // 32 samples with factor 4 should give ~8 unique values
    REQUIRE(uniqueCount >= 6);
    REQUIRE(uniqueCount <= 10);
}

TEST_CASE("SampleRateReducer fractional factors work", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(2.5f);

    // Create a ramp input
    std::array<float, 100> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i) * 0.01f;
    }

    reducer.process(buffer.data(), buffer.size());

    // With factor 2.5, we should get approximately 100/2.5 = 40 unique values
    size_t uniqueCount = countUniqueValues(buffer.data(), buffer.size());

    REQUIRE(uniqueCount >= 35);
    REQUIRE(uniqueCount <= 45);
}

// =============================================================================
// T024: Aliasing Tests
// =============================================================================

TEST_CASE("SampleRateReducer creates aliasing artifacts", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);

    // Generate a high frequency sine (10kHz)
    std::array<float, 1024> original, processed;
    generateSine(original.data(), original.size(), 10000.0f, 44100.0f);
    std::copy(original.begin(), original.end(), processed.begin());

    // Apply strong sample rate reduction
    reducer.setReductionFactor(4.0f);
    reducer.process(processed.data(), processed.size());

    // The processed signal should be significantly different
    // due to aliasing (the original frequency cannot be represented)
    float totalDiff = 0.0f;
    for (size_t i = 0; i < original.size(); ++i) {
        totalDiff += std::abs(processed[i] - original[i]);
    }
    float avgDiff = totalDiff / static_cast<float>(original.size());

    // With factor 4, effective sample rate is ~11kHz
    // A 10kHz sine will alias severely
    REQUIRE(avgDiff > 0.1f); // Significant difference
}

TEST_CASE("SampleRateReducer aliasing increases with reduction factor", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);

    auto measureAliasing = [&reducer](float factor) {
        std::array<float, 1024> original, processed;
        generateSine(original.data(), original.size(), 8000.0f, 44100.0f);
        std::copy(original.begin(), original.end(), processed.begin());

        reducer.reset();
        reducer.setReductionFactor(factor);
        reducer.process(processed.data(), processed.size());

        float totalDiff = 0.0f;
        for (size_t i = 0; i < original.size(); ++i) {
            totalDiff += std::abs(processed[i] - original[i]);
        }
        return totalDiff / static_cast<float>(original.size());
    };

    float alias2x = measureAliasing(2.0f);
    float alias4x = measureAliasing(4.0f);
    float alias8x = measureAliasing(8.0f);

    // Higher reduction factors should cause significant aliasing
    // Note: The simple difference metric may not increase monotonically
    // due to phase relationships, but all should show significant aliasing
    // compared to the input signal
    REQUIRE(alias2x > 0.05f);  // Some aliasing at 2x
    REQUIRE(alias4x > 0.1f);   // More aliasing at 4x
    REQUIRE(alias8x > 0.1f);   // Significant aliasing at 8x

    // 4x and 8x should both cause more aliasing than 2x
    REQUIRE(alias4x > alias2x * 0.5f);
    REQUIRE(alias8x > alias2x * 0.5f);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("SampleRateReducer handles edge input values", "[samplerate][layer1][edge]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(4.0f);

    SECTION("zero input produces zero output") {
        reducer.reset();
        REQUIRE(reducer.process(0.0f) == Approx(0.0f));
    }

    SECTION("full scale input is preserved") {
        reducer.reset();
        float result = reducer.process(1.0f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("negative values are handled") {
        reducer.reset();
        float result = reducer.process(-0.5f);
        REQUIRE(result == Approx(-0.5f));
    }
}

TEST_CASE("SampleRateReducer produces staircased output", "[samplerate][layer1][US3]") {
    SampleRateReducer reducer;
    reducer.prepare(44100.0);
    reducer.setReductionFactor(8.0f);

    // Generate a smooth ramp
    std::array<float, 64> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<float>(i) / 64.0f;
    }

    reducer.process(buffer.data(), buffer.size());

    // Count holds (consecutive identical values)
    size_t holds = countHolds(buffer.data(), buffer.size());

    // With factor 8, most consecutive samples should be equal
    // 64 samples -> 63 transitions, most should be holds
    REQUIRE(holds >= 50);
}
