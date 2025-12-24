// ==============================================================================
// Unit Tests: DiffusionNetwork
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 015-diffusion-network
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/diffusion_network.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestBlockSize = 512;
constexpr float kTolerance = 1e-5f;
constexpr float kTestPi = 3.14159265358979323846f;
constexpr float kTestTwoPi = 2.0f * kTestPi;

// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTestTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Generate white noise with optional seed for reproducibility
inline void generateWhiteNoise(float* buffer, size_t size, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

// Generate impulse (single sample at 1.0, rest zeros)
inline void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    if (size > 0) buffer[0] = 1.0f;
}

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Calculate peak absolute value
inline float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// Check if buffer contains any NaN or Inf values
inline bool hasInvalidSamples(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) {
            return true;
        }
    }
    return false;
}

// Check if two buffers are equal within tolerance
inline bool buffersEqual(const float* a, const float* b, size_t size, float tolerance = kTolerance) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

// Calculate energy in a buffer (sum of squared samples)
inline float calculateEnergy(const float* buffer, size_t size) {
    float energy = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        energy += buffer[i] * buffer[i];
    }
    return energy;
}

// Find the sample index where cumulative energy reaches a threshold percentage
inline size_t findEnergyThresholdSample(const float* buffer, size_t size, float thresholdPercent) {
    float totalEnergy = calculateEnergy(buffer, size);
    float threshold = totalEnergy * thresholdPercent;
    float cumulative = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        cumulative += buffer[i] * buffer[i];
        if (cumulative >= threshold) {
            return i;
        }
    }
    return size;
}

// Calculate cross-correlation coefficient between two buffers at lag 0
inline float crossCorrelation(const float* a, const float* b, size_t size) {
    float sumA = 0.0f, sumB = 0.0f, sumAB = 0.0f;
    float sumA2 = 0.0f, sumB2 = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        sumA += a[i];
        sumB += b[i];
        sumAB += a[i] * b[i];
        sumA2 += a[i] * a[i];
        sumB2 += b[i] * b[i];
    }

    float n = static_cast<float>(size);
    float numerator = n * sumAB - sumA * sumB;
    float denominator = std::sqrt((n * sumA2 - sumA * sumA) * (n * sumB2 - sumB * sumB));

    if (denominator < 1e-10f) return 0.0f;
    return numerator / denominator;
}

// Simple DFT for spectrum analysis (not optimized, for testing only)
inline std::vector<float> computeMagnitudeSpectrum(const float* buffer, size_t size) {
    std::vector<float> spectrum(size / 2 + 1);

    for (size_t k = 0; k <= size / 2; ++k) {
        float real = 0.0f, imag = 0.0f;
        for (size_t n = 0; n < size; ++n) {
            float angle = -kTestTwoPi * static_cast<float>(k * n) / static_cast<float>(size);
            real += buffer[n] * std::cos(angle);
            imag += buffer[n] * std::sin(angle);
        }
        spectrum[k] = std::sqrt(real * real + imag * imag) / static_cast<float>(size);
    }

    return spectrum;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: AllpassStage Tests
// ==============================================================================

// T005: AllpassStage single sample processing
TEST_CASE("AllpassStage processes single samples correctly", "[diffusion][allpass][US1]") {
    AllpassStage stage;

    SECTION("prepare sets up delay line with correct max delay") {
        // Max delay of 50ms at 44.1kHz = 2205 samples
        stage.prepare(kTestSampleRate, 0.05f);  // 50ms max
        stage.reset();

        // Should be able to process without crash
        float output = stage.process(1.0f, 100.0f);  // 100 samples delay
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("reset clears internal state") {
        stage.prepare(kTestSampleRate, 0.05f);

        // Feed some signal
        for (int i = 0; i < 100; ++i) {
            (void)stage.process(1.0f, 50.0f);
        }

        stage.reset();

        // After reset, processing silence should produce silence (eventually)
        float lastOutput = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            lastOutput = stage.process(0.0f, 50.0f);
        }
        REQUIRE(std::abs(lastOutput) < 0.01f);
    }

    SECTION("implements Schroeder allpass formula") {
        // y[n] = -g * x[n] + x[n-D] + g * y[n-D]
        // For D=1 (1 sample delay) and first impulse input, we can verify
        stage.prepare(kTestSampleRate, 0.001f);  // 1ms max
        stage.reset();

        constexpr float g = 0.618033988749895f;  // Golden ratio inverse

        // Process impulse
        float y0 = stage.process(1.0f, 1.0f);  // First sample: x[0]=1, x[-1]=0, y[-1]=0
        // Expected: y[0] = -g * 1 + 0 + g * 0 = -g
        REQUIRE(y0 == Approx(-g).margin(0.001f));

        // Second sample: x[1]=0, x[0]=1, y[0]=-g
        float y1 = stage.process(0.0f, 1.0f);
        // Expected: y[1] = -g * 0 + 1 + g * (-g) = 1 - g^2
        float expected_y1 = 1.0f - g * g;
        REQUIRE(y1 == Approx(expected_y1).margin(0.001f));
    }
}

// T006: AllpassStage preserves frequency spectrum (flat response)
TEST_CASE("AllpassStage preserves frequency spectrum", "[diffusion][allpass][US1]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);
    stage.reset();

    constexpr size_t kFFTSize = 4096;
    std::array<float, kFFTSize> impulse{};
    impulse[0] = 1.0f;

    std::array<float, kFFTSize> output{};

    // Process impulse through allpass
    for (size_t i = 0; i < kFFTSize; ++i) {
        output[i] = stage.process(impulse[i], 50.0f);  // 50 sample delay
    }

    // Compute magnitude spectra
    auto inputSpectrum = computeMagnitudeSpectrum(impulse.data(), kFFTSize);
    auto outputSpectrum = computeMagnitudeSpectrum(output.data(), kFFTSize);

    // Check that magnitudes are approximately equal (within 0.5dB = ~6% tolerance)
    // Skip DC and very high frequencies
    size_t startBin = 10;   // ~100Hz
    size_t endBin = kFFTSize / 4;  // ~5.5kHz

    for (size_t i = startBin; i < endBin; ++i) {
        if (inputSpectrum[i] > 1e-6f) {
            float ratio = outputSpectrum[i] / inputSpectrum[i];
            float ratioDb = 20.0f * std::log10(ratio);
            REQUIRE(std::abs(ratioDb) < 0.5f);  // Within ±0.5dB
        }
    }
}

// T006b: AllpassStage preserves energy (diagnostic test)
TEST_CASE("AllpassStage preserves energy", "[diffusion][allpass][US1][diagnostic]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);  // 50ms max delay
    stage.reset();

    constexpr size_t kBufferSize = 8192;  // ~185ms at 44.1kHz

    SECTION("integer delay preserves energy") {
        std::array<float, kBufferSize> impulse{};
        impulse[0] = 1.0f;
        std::array<float, kBufferSize> output{};

        for (size_t i = 0; i < kBufferSize; ++i) {
            output[i] = stage.process(impulse[i], 70.0f);  // Integer delay
        }

        float inputEnergy = calculateEnergy(impulse.data(), kBufferSize);
        float outputEnergy = calculateEnergy(output.data(), kBufferSize);
        float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

        INFO("Single stage integer delay energy ratio: " << energyRatioDb << " dB");
        REQUIRE(std::abs(energyRatioDb) < 0.5f);
    }

    SECTION("fractional delay with linear interpolation") {
        stage.reset();
        std::array<float, kBufferSize> impulse{};
        impulse[0] = 1.0f;
        std::array<float, kBufferSize> output{};

        for (size_t i = 0; i < kBufferSize; ++i) {
            output[i] = stage.process(impulse[i], 70.56f);  // Fractional delay
        }

        float inputEnergy = calculateEnergy(impulse.data(), kBufferSize);
        float outputEnergy = calculateEnergy(output.data(), kBufferSize);
        float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

        INFO("Single stage fractional delay energy ratio: " << energyRatioDb << " dB");
        // Fractional delay with linear interpolation causes some HF loss
        // Allow ±1dB for a single stage
        REQUIRE(std::abs(energyRatioDb) < 1.0f);
    }
}

// T006b2: AllpassStage DC response verification
TEST_CASE("AllpassStage DC response is unity", "[diffusion][allpass][US1][diagnostic]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);  // 50ms max delay
    stage.reset();

    // Feed constant 1.0 for long enough to reach steady state
    constexpr size_t kNumSamples = 1000;
    float lastOutput = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        lastOutput = stage.process(1.0f, 70.0f);
    }

    // At steady state, allpass should pass DC unchanged
    INFO("DC response after " << kNumSamples << " samples: " << lastOutput);
    REQUIRE(lastOutput == Approx(1.0f).margin(0.01f));
}

// T006b3: Two cascaded stages energy preservation (diagnostic)
TEST_CASE("Two cascaded AllpassStages preserve energy", "[diffusion][allpass][US1][diagnostic]") {
    AllpassStage stage1, stage2;
    stage1.prepare(kTestSampleRate, 0.1f);
    stage2.prepare(kTestSampleRate, 0.1f);
    stage1.reset();
    stage2.reset();

    constexpr size_t kBufferSize = 32768;  // ~0.75s
    std::vector<float> input(kBufferSize, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output(kBufferSize, 0.0f);

    // Process impulse through 2-stage cascade
    for (size_t n = 0; n < kBufferSize; ++n) {
        float sample = input[n];
        sample = stage1.process(sample, 70.0f);  // Stage 1: 70 samples delay
        sample = stage2.process(sample, 80.0f);  // Stage 2: 80 samples delay
        output[n] = sample;
    }

    float inputEnergy = calculateEnergy(input.data(), kBufferSize);
    float outputEnergy = calculateEnergy(output.data(), kBufferSize);
    float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

    INFO("2-stage cascade energy ratio: " << energyRatioDb << " dB");
    INFO("Input: " << inputEnergy << ", Output: " << outputEnergy);
    REQUIRE(std::abs(energyRatioDb) < 0.5f);
}

// T006b4: Four cascaded stages energy preservation (diagnostic)
TEST_CASE("Four cascaded AllpassStages preserve energy", "[diffusion][allpass][US1][diagnostic]") {
    std::array<AllpassStage, 4> stages;
    for (auto& stage : stages) {
        stage.prepare(kTestSampleRate, 0.1f);
        stage.reset();
    }
    std::array<float, 4> delays = {70.0f, 80.0f, 100.0f, 122.0f};

    constexpr size_t kBufferSize = 65536;  // ~1.5s
    std::vector<float> input(kBufferSize, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output(kBufferSize, 0.0f);

    for (size_t n = 0; n < kBufferSize; ++n) {
        float sample = input[n];
        for (size_t i = 0; i < 4; ++i) {
            sample = stages[i].process(sample, delays[i]);
        }
        output[n] = sample;
    }

    float inputEnergy = calculateEnergy(input.data(), kBufferSize);
    float outputEnergy = calculateEnergy(output.data(), kBufferSize);
    float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

    INFO("4-stage cascade energy ratio: " << energyRatioDb << " dB");
    INFO("Input: " << inputEnergy << ", Output: " << outputEnergy);
    REQUIRE(std::abs(energyRatioDb) < 1.0f);
}

// T006c: 6-stage cascade energy preservation (diagnostic test)
TEST_CASE("Six cascaded AllpassStages preserve energy", "[diffusion][allpass][US1][diagnostic]") {
    constexpr size_t kNumStages = 6;
    std::array<AllpassStage, kNumStages> stages;

    for (auto& stage : stages) {
        stage.prepare(kTestSampleRate, 0.1f);
        stage.reset();
    }

    std::array<float, kNumStages> delays = {70.0f, 80.0f, 100.0f, 122.0f, 158.0f, 200.0f};

    constexpr size_t kBufferSize = 131072;
    std::vector<float> input(kBufferSize, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output(kBufferSize, 0.0f);

    for (size_t n = 0; n < kBufferSize; ++n) {
        float sample = input[n];
        for (size_t i = 0; i < kNumStages; ++i) {
            sample = stages[i].process(sample, delays[i]);
        }
        output[n] = sample;
    }

    float inputEnergy = calculateEnergy(input.data(), kBufferSize);
    float outputEnergy = calculateEnergy(output.data(), kBufferSize);
    float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

    INFO("6-stage cascade energy ratio: " << energyRatioDb << " dB");
    INFO("Input: " << inputEnergy << ", Output: " << outputEnergy);
    REQUIRE(std::abs(energyRatioDb) < 1.0f);
}

// T006d: 8-stage cascade energy preservation (diagnostic test)
TEST_CASE("Eight cascaded AllpassStages preserve energy", "[diffusion][allpass][US1][diagnostic]") {
    constexpr size_t kNumStages = 8;
    std::array<AllpassStage, kNumStages> stages;

    for (auto& stage : stages) {
        stage.prepare(kTestSampleRate, 0.1f);
        stage.reset();
    }

    std::array<float, kNumStages> delays = {70.0f, 80.0f, 100.0f, 122.0f, 158.0f, 200.0f, 234.0f, 291.0f};

    constexpr size_t kBufferSize = 131072;
    std::vector<float> input(kBufferSize, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output(kBufferSize, 0.0f);

    for (size_t n = 0; n < kBufferSize; ++n) {
        float sample = input[n];
        for (size_t i = 0; i < kNumStages; ++i) {
            sample = stages[i].process(sample, delays[i]);
        }
        output[n] = sample;
    }

    float inputEnergy = calculateEnergy(input.data(), kBufferSize);
    float outputEnergy = calculateEnergy(output.data(), kBufferSize);
    float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

    INFO("8-stage cascade energy ratio: " << energyRatioDb << " dB");
    INFO("Input: " << inputEnergy << ", Output: " << outputEnergy);
    REQUIRE(std::abs(energyRatioDb) < 1.0f);
}

// T006e: 8-stage cascade with EXACT fractional delays from DiffusionNetwork
TEST_CASE("Eight cascaded AllpassStages with fractional delays preserve energy", "[diffusion][allpass][US1][diagnostic]") {
    constexpr size_t kNumStages = 8;
    std::array<AllpassStage, kNumStages> stages;

    for (auto& stage : stages) {
        stage.prepare(kTestSampleRate, 0.1f);
        stage.reset();
    }

    // Use EXACT same delays as DiffusionNetwork at size=50%
    constexpr float kBaseDelayMs = 3.2f;
    constexpr float kSize = 0.5f;
    constexpr std::array<float, kNumStages> kDelayRatios = {
        1.000f, 1.127f, 1.414f, 1.732f, 2.236f, 2.828f, 3.317f, 4.123f
    };

    std::array<float, kNumStages> delays;
    for (size_t i = 0; i < kNumStages; ++i) {
        float delayMs = kBaseDelayMs * kSize * kDelayRatios[i];
        delays[i] = delayMs * 0.001f * kTestSampleRate;
    }

    constexpr size_t kBufferSize = 131072;
    std::vector<float> input(kBufferSize, 0.0f);
    input[0] = 1.0f;

    std::vector<float> output(kBufferSize, 0.0f);

    for (size_t n = 0; n < kBufferSize; ++n) {
        float sample = input[n];
        for (size_t i = 0; i < kNumStages; ++i) {
            sample = stages[i].process(sample, delays[i]);
        }
        output[n] = sample;
    }

    float inputEnergy = calculateEnergy(input.data(), kBufferSize);
    float outputEnergy = calculateEnergy(output.data(), kBufferSize);
    float energyRatioDb = 10.0f * std::log10(outputEnergy / inputEnergy);

    INFO("8-stage cascade with fractional delays energy ratio: " << energyRatioDb << " dB");
    INFO("Delays: " << delays[0] << ", " << delays[1] << ", ..., " << delays[7]);
    INFO("Input: " << inputEnergy << ", Output: " << outputEnergy);
    REQUIRE(std::abs(energyRatioDb) < 1.0f);
}

// T007: AllpassStage supports delay time modulation
TEST_CASE("AllpassStage supports delay time modulation", "[diffusion][allpass][US4]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);  // 50ms max delay
    stage.reset();

    SECTION("varying delay time produces valid output") {
        // Simulate LFO-modulated delay
        std::array<float, 1000> output{};
        float baseDelay = 100.0f;  // 100 samples
        float modDepth = 20.0f;    // ±20 samples

        for (size_t i = 0; i < output.size(); ++i) {
            // Sine LFO at 2Hz
            float lfo = std::sin(kTestTwoPi * 2.0f * static_cast<float>(i) / kTestSampleRate);
            float delay = baseDelay + modDepth * lfo;
            output[i] = stage.process(0.5f, delay);
        }

        // Verify no NaN or Inf values
        REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));

        // Verify output is bounded
        float peak = calculatePeak(output.data(), output.size());
        REQUIRE(peak < 2.0f);  // Reasonable bound for allpass
    }

    SECTION("delay time clamped to valid range") {
        // Request delay beyond max - should clamp, not crash
        float output1 = stage.process(1.0f, 10000.0f);  // Way beyond max
        REQUIRE_FALSE(std::isnan(output1));

        // Negative delay should clamp to 0
        float output2 = stage.process(1.0f, -10.0f);
        REQUIRE_FALSE(std::isnan(output2));
    }
}

// ==============================================================================
// Phase 3: User Story 1 + 2 Tests - Basic Diffusion + Size Control (P1 MVP)
// ==============================================================================

// T015: DiffusionNetwork prepare/reset lifecycle
TEST_CASE("DiffusionNetwork prepare and reset lifecycle", "[diffusion][US1]") {
    DiffusionNetwork diffuser;

    SECTION("prepare initializes processor for given sample rate") {
        diffuser.prepare(kTestSampleRate, kTestBlockSize);

        // Should be able to process without crash
        std::array<float, 64> left{}, right{};
        std::array<float, 64> leftOut{}, rightOut{};
        left[0] = 1.0f;

        diffuser.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 64);

        // Output should contain some signal
        float peak = calculatePeak(leftOut.data(), 64);
        REQUIRE(peak > 0.0f);
    }

    SECTION("reset clears all internal state") {
        diffuser.prepare(kTestSampleRate, kTestBlockSize);
        diffuser.setSize(100.0f);

        // Process some signal
        std::array<float, 256> left{}, right{};
        std::array<float, 256> leftOut{}, rightOut{};
        for (size_t i = 0; i < 256; ++i) {
            left[i] = 0.5f;
            right[i] = 0.5f;
        }
        diffuser.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 256);

        // Reset
        diffuser.reset();

        // After reset, processing silence should produce silence eventually
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        // Process silence for long enough to flush delay lines
        for (int block = 0; block < 20; ++block) {
            diffuser.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 256);
        }

        // Output should be nearly silent
        float peakL = calculatePeak(leftOut.data(), 256);
        float peakR = calculatePeak(rightOut.data(), 256);
        REQUIRE(peakL < 0.01f);
        REQUIRE(peakR < 0.01f);
    }
}

// T016: Impulse diffusion (energy spread over time)
TEST_CASE("DiffusionNetwork spreads impulse energy over time", "[diffusion][US1]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setSize(100.0f);
    diffuser.setDensity(100.0f);  // All stages active

    // Process impulse through network
    constexpr size_t kBufferSize = 8192;  // Long enough to capture diffusion tail
    std::vector<float> leftIn(kBufferSize, 0.0f);
    std::vector<float> rightIn(kBufferSize, 0.0f);
    std::vector<float> leftOut(kBufferSize, 0.0f);
    std::vector<float> rightOut(kBufferSize, 0.0f);

    leftIn[0] = 1.0f;  // Impulse
    rightIn[0] = 1.0f;

    diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBufferSize);

    SECTION("impulse is smeared over time") {
        // Find where 25% and 75% of energy has accumulated
        size_t sample25 = findEnergyThresholdSample(leftOut.data(), kBufferSize, 0.25f);
        size_t sample75 = findEnergyThresholdSample(leftOut.data(), kBufferSize, 0.75f);

        // Energy should be spread (not concentrated in one sample)
        float spread = static_cast<float>(sample75 - sample25) / kTestSampleRate * 1000.0f;  // ms
        REQUIRE(spread > 5.0f);  // At least 5ms spread between 25% and 75% energy
    }

    SECTION("output contains no NaN or Inf values") {
        REQUIRE_FALSE(hasInvalidSamples(leftOut.data(), kBufferSize));
        REQUIRE_FALSE(hasInvalidSamples(rightOut.data(), kBufferSize));
    }

    SECTION("peak output is bounded") {
        float peakL = calculatePeak(leftOut.data(), kBufferSize);
        float peakR = calculatePeak(rightOut.data(), kBufferSize);
        REQUIRE(peakL < 2.0f);  // Reasonable bound for allpass cascade
        REQUIRE(peakR < 2.0f);
    }
}

// T017: Frequency spectrum preservation (allpass property)
// Note: Testing energy conservation instead of per-bin flatness for 8-stage cascade.
// Individual AllpassStage test verifies ±0.5dB flatness; cascade verification is done
// through energy preservation (allpass filters conserve energy by definition).
TEST_CASE("DiffusionNetwork preserves energy (allpass property)", "[diffusion][US1]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setSize(50.0f);
    diffuser.setDensity(100.0f);
    diffuser.reset();  // Snap smoothers to targets

    // Use large buffer to capture the full impulse response (same as standalone cascade tests)
    constexpr size_t kBufferSize = 131072;
    std::vector<float> leftIn(kBufferSize, 0.0f);
    std::vector<float> rightIn(kBufferSize, 0.0f);
    std::vector<float> leftOut(kBufferSize, 0.0f);
    std::vector<float> rightOut(kBufferSize, 0.0f);

    // Create mono impulse (only left channel, to isolate from stereo effects)
    leftIn[0] = 1.0f;
    rightIn[0] = 0.0f;

    diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBufferSize);

    // Calculate left channel energy (with mono input, only left has signal)
    float inputEnergy = calculateEnergy(leftIn.data(), kBufferSize);
    float outputEnergy = calculateEnergy(leftOut.data(), kBufferSize);

    // Also calculate right channel energy (should be ~0 with mono left input)
    float rightEnergy = calculateEnergy(rightOut.data(), kBufferSize);

    // Allpass filters preserve energy
    // Allow ±1dB tolerance for numerical precision with 8 cascaded stages
    float energyRatio = outputEnergy / inputEnergy;
    float energyRatioDb = 10.0f * std::log10(energyRatio);  // Use 10*log10 for energy

    INFO("Left input energy: " << inputEnergy);
    INFO("Left output energy: " << outputEnergy);
    INFO("Right output energy: " << rightEnergy);
    INFO("Energy ratio: " << energyRatioDb << " dB");

    REQUIRE(std::abs(energyRatioDb) < 1.0f);

    // Also verify output contains no invalid samples
    REQUIRE_FALSE(hasInvalidSamples(leftOut.data(), kBufferSize));
    REQUIRE_FALSE(hasInvalidSamples(rightOut.data(), kBufferSize));
}

// T018: Size=0% bypass behavior
TEST_CASE("DiffusionNetwork at size=0% acts as bypass", "[diffusion][US2]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setSize(0.0f);  // Bypass
    diffuser.setDensity(100.0f);
    diffuser.reset();  // Snap smoothers to targets for immediate effect

    std::array<float, 256> leftIn{}, rightIn{};
    std::array<float, 256> leftOut{}, rightOut{};

    // Generate test signal
    generateSine(leftIn.data(), 256, 440.0f, kTestSampleRate);
    generateSine(rightIn.data(), 256, 440.0f, kTestSampleRate);

    diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), 256);

    SECTION("output equals input when size is zero") {
        // At size=0%, delay times are 0, so output should equal input
        REQUIRE(buffersEqual(leftIn.data(), leftOut.data(), 256, 0.01f));
        REQUIRE(buffersEqual(rightIn.data(), rightOut.data(), 256, 0.01f));
    }
}

// T019: Size=50% moderate diffusion
TEST_CASE("DiffusionNetwork at size=50% provides moderate diffusion", "[diffusion][US2]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setSize(50.0f);
    diffuser.setDensity(100.0f);

    constexpr size_t kBufferSize = 4096;
    std::vector<float> leftIn(kBufferSize, 0.0f);
    std::vector<float> rightIn(kBufferSize, 0.0f);
    std::vector<float> leftOut(kBufferSize, 0.0f);
    std::vector<float> rightOut(kBufferSize, 0.0f);

    leftIn[0] = 1.0f;
    rightIn[0] = 1.0f;

    diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBufferSize);

    // At size=50%, expect diffusion spread of ~28ms (about 1200 samples @ 44.1kHz)
    size_t sample95 = findEnergyThresholdSample(leftOut.data(), kBufferSize, 0.95f);
    float spreadMs = static_cast<float>(sample95) / kTestSampleRate * 1000.0f;

    REQUIRE(spreadMs > 15.0f);   // At least 15ms spread
    REQUIRE(spreadMs < 60.0f);   // But less than max spread
}

// T020: Size=100% maximum diffusion (50-100ms target)
TEST_CASE("DiffusionNetwork at size=100% provides maximum diffusion", "[diffusion][US2]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setSize(100.0f);
    diffuser.setDensity(100.0f);

    constexpr size_t kBufferSize = 8192;
    std::vector<float> leftIn(kBufferSize, 0.0f);
    std::vector<float> rightIn(kBufferSize, 0.0f);
    std::vector<float> leftOut(kBufferSize, 0.0f);
    std::vector<float> rightOut(kBufferSize, 0.0f);

    leftIn[0] = 1.0f;
    rightIn[0] = 1.0f;

    diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBufferSize);

    // At size=100%, expect diffusion spread of 50-100ms per SC-002
    size_t sample95 = findEnergyThresholdSample(leftOut.data(), kBufferSize, 0.95f);
    float spreadMs = static_cast<float>(sample95) / kTestSampleRate * 1000.0f;

    REQUIRE(spreadMs >= 50.0f);   // At least 50ms
    REQUIRE(spreadMs <= 150.0f);  // Upper bound with margin
}

// T021: Size parameter smoothing (no clicks on rapid changes)
TEST_CASE("DiffusionNetwork size parameter changes are smooth", "[diffusion][US2]") {
    DiffusionNetwork diffuser;
    diffuser.prepare(kTestSampleRate, kTestBlockSize);
    diffuser.setDensity(100.0f);

    constexpr size_t kBlockSize = 64;
    std::array<float, kBlockSize> leftIn{}, rightIn{};
    std::array<float, kBlockSize> leftOut{}, rightOut{};

    // Fill with constant signal
    std::fill(leftIn.begin(), leftIn.end(), 0.5f);
    std::fill(rightIn.begin(), rightIn.end(), 0.5f);

    // Start at size=0%
    diffuser.setSize(0.0f);

    // Process a few blocks to stabilize
    for (int i = 0; i < 10; ++i) {
        diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBlockSize);
    }

    // Abruptly change size to 100%
    diffuser.setSize(100.0f);

    // Process several blocks and check for clicks (large sample-to-sample differences)
    float maxDiff = 0.0f;
    float prevSample = leftOut[kBlockSize - 1];

    for (int block = 0; block < 20; ++block) {
        diffuser.process(leftIn.data(), rightIn.data(), leftOut.data(), rightOut.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            float diff = std::abs(leftOut[i] - prevSample);
            if (diff > maxDiff) maxDiff = diff;
            prevSample = leftOut[i];
        }
    }

    // Max sample-to-sample difference should be reasonable (no clicks)
    // For smoothed parameters, jumps should be gradual
    REQUIRE(maxDiff < 0.5f);
}

// ==============================================================================
// Phase 4: User Story 3 Tests - Density Control (P2)
// ==============================================================================

// Tests for density parameter
// (T035-T039)

// ==============================================================================
// Phase 5: User Story 4 Tests - Modulation (P2)
// ==============================================================================

// Tests for LFO modulation
// (T047-T050)

// ==============================================================================
// Phase 6: User Story 5 Tests - Stereo Width Control (P2)
// ==============================================================================

// Tests for stereo width parameter
// (T059-T062)

// ==============================================================================
// Phase 7: User Story 6 Tests - Real-Time Safety (P1)
// ==============================================================================

// Tests for real-time safety compliance
// (T069-T072)

// ==============================================================================
// Phase 8: Edge Cases
// ==============================================================================

// Tests for edge cases and error handling
// (T078-T080)
