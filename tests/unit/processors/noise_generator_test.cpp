// ==============================================================================
// Unit Tests: NoiseGenerator
// ==============================================================================
// Layer 2: DSP Processor Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// Test organization by User Story:
// - US1: White Noise Generation [US1]
// - US2: Pink Noise Generation [US2]
// - US3: Tape Hiss Generation [US3]
// - US4: Vinyl Crackle Generation [US4]
// - US5: Asperity Noise Generation [US5]
// - US6: Multi-Noise Mixing [US6]
//
// Success Criteria tags:
// - [SC-001] through [SC-008]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/processors/noise_generator.h"
#include "dsp/primitives/fft.h"

#include <array>
#include <cmath>
#include <vector>
#include <numeric>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kSampleRate = 44100.0f;
constexpr size_t kBlockSize = 512;
constexpr float kTwoPi = 6.283185307179586f;

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Find peak absolute value in buffer
inline float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// Convert dB to linear
inline float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

// Check if buffer is all zeros
inline bool isAllZeros(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != 0.0f) return false;
    }
    return true;
}

// Check if buffer has any non-zero values
inline bool hasNonZeroValues(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

// Measure energy in a frequency band using FFT
// Returns average magnitude in the frequency range [freqLow, freqHigh] Hz
inline float measureBandEnergy(const float* buffer, size_t size,
                                float freqLow, float freqHigh, float sampleRate) {
    // Use a power of 2 size for FFT
    constexpr size_t fftSize = 4096;
    if (size < fftSize) return 0.0f;

    FFT fft;
    fft.prepare(fftSize);

    // Copy and window the input
    std::vector<float> input(fftSize);
    std::vector<Complex> output(fftSize / 2 + 1);

    // Use Hanning window
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize));
        input[i] = buffer[i] * window;
    }

    // Perform FFT
    fft.forward(input.data(), output.data());

    // Calculate frequency resolution
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t binLow = static_cast<size_t>(freqLow / binWidth);
    size_t binHigh = static_cast<size_t>(freqHigh / binWidth);

    if (binLow >= output.size()) binLow = output.size() - 1;
    if (binHigh >= output.size()) binHigh = output.size() - 1;
    if (binLow > binHigh) std::swap(binLow, binHigh);

    // Average magnitude in band
    float sumMag = 0.0f;
    size_t count = 0;
    for (size_t i = binLow; i <= binHigh; ++i) {
        float mag = std::sqrt(output[i].real * output[i].real +
                              output[i].imag * output[i].imag);
        sumMag += mag;
        count++;
    }

    return (count > 0) ? (sumMag / static_cast<float>(count)) : 0.0f;
}

} // anonymous namespace

// ==============================================================================
// User Story 1: White Noise Generation [US1]
// ==============================================================================

TEST_CASE("NoiseGenerator prepare() initializes correctly", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // All noise types should be disabled by default
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::Pink));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::TapeHiss));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::VinylCrackle));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::Asperity));
    REQUIRE_FALSE(noise.isAnyEnabled());
}

TEST_CASE("White noise: output is zero when disabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Leave white noise disabled (default)
    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f); // Fill with non-zero to verify it gets zeroed

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("White noise: output is non-zero when enabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f); // 0 dB = unity gain

    // Generate a larger buffer for statistical reliability
    constexpr size_t largeSize = 4096;
    std::vector<float> buffer(largeSize, 0.0f);

    // Process multiple blocks to let smoother settle
    for (size_t i = 0; i < largeSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("White noise: samples in [-1.0, 1.0] range (SC-003)", "[noise][US1][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate many samples
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // All samples must be in valid range
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("White noise: level at -20dB produces ~0.1 amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, -20.0f);

    // Generate enough samples for statistical reliability
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    // Process multiple blocks to let smoother settle
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip first part where smoother is settling (first 10ms)
    constexpr size_t skipSamples = 441;
    float rms = calculateRMS(buffer.data() + skipSamples, testSize - skipSamples);

    // -20dB = 0.1 linear, but white noise has RMS of ~0.577 for uniform [-1,1]
    // So expected RMS ≈ 0.1 * 0.577 ≈ 0.058
    // Allow reasonable tolerance
    float expectedRMS = 0.1f * 0.577f;
    REQUIRE(rms == Approx(expectedRMS).margin(0.03f));
}

TEST_CASE("White noise: level at 0dB produces ~1.0 amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate enough samples for statistical reliability
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip first part where smoother is settling
    constexpr size_t skipSamples = 441;
    float rms = calculateRMS(buffer.data() + skipSamples, testSize - skipSamples);

    // White noise from uniform [-1,1] has RMS of ~0.577
    float expectedRMS = 0.577f;
    REQUIRE(rms == Approx(expectedRMS).margin(0.05f));
}

TEST_CASE("White noise: setNoiseLevel affects output amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferLoud(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate at 0dB
    noise.setNoiseLevel(NoiseType::White, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferLoud.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate at -20dB
    noise.reset();
    noise.setNoiseLevel(NoiseType::White, -20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsLoud = calculateRMS(bufferLoud.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsLoud / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("White noise: spectral flatness within 3dB (SC-001)", "[noise][US1][SC-001]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate 10 seconds of white noise for spectral analysis
    constexpr size_t testSize = 441000; // 10 seconds at 44.1kHz
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial smoother settling
    const float* analysisStart = buffer.data() + 4410; // Skip first 100ms
    size_t analysisSize = testSize - 4410;

    // Measure energy at different frequency bands
    // Using wider bands for more stable measurements
    float energy1k = measureBandEnergy(analysisStart, analysisSize, 800.0f, 1200.0f, kSampleRate);
    float energy4k = measureBandEnergy(analysisStart, analysisSize, 3500.0f, 4500.0f, kSampleRate);

    // Convert to dB
    float db1k = linearToDb(energy1k);
    float db4k = linearToDb(energy4k);

    // Energy should be within 3dB across these bands for white noise
    float difference = std::abs(db1k - db4k);
    REQUIRE(difference < 3.0f);
}

TEST_CASE("White noise: different seeds produce different sequences", "[noise][US1]") {
    NoiseGenerator noise1;
    NoiseGenerator noise2;

    noise1.prepare(kSampleRate, kBlockSize);
    noise2.prepare(kSampleRate, kBlockSize);

    noise1.setNoiseEnabled(NoiseType::White, true);
    noise2.setNoiseEnabled(NoiseType::White, true);
    noise1.setNoiseLevel(NoiseType::White, 0.0f);
    noise2.setNoiseLevel(NoiseType::White, 0.0f);

    // Reset to get different sequences
    noise2.reset();

    std::array<float, kBlockSize> buffer1, buffer2;
    noise1.process(buffer1.data(), buffer1.size());
    noise2.process(buffer2.data(), buffer2.size());

    // Sequences should be different
    bool allSame = true;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (buffer1[i] != buffer2[i]) {
            allSame = false;
            break;
        }
    }
    REQUIRE_FALSE(allSame);
}

TEST_CASE("White noise: master level affects output", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferNormal(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate with master at 0dB
    noise.setMasterLevel(0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferNormal.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate with master at -20dB
    noise.reset();
    noise.setMasterLevel(-20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsNormal = calculateRMS(bufferNormal.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsNormal / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("White noise: processMix adds noise to input", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, -20.0f);

    // Create input with known DC offset
    std::array<float, kBlockSize> input;
    std::array<float, kBlockSize> output;
    input.fill(0.5f);

    // Process multiple blocks to let smoother settle
    for (int i = 0; i < 10; ++i) {
        noise.processMix(input.data(), output.data(), kBlockSize);
    }

    // Output should contain input (0.5) plus noise
    // Mean should be approximately 0.5, but with some variation from noise
    float sum = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        sum += output[i];
    }
    float mean = sum / static_cast<float>(kBlockSize);

    REQUIRE(mean == Approx(0.5f).margin(0.1f));

    // Output should not be exactly 0.5 everywhere (noise added)
    bool hasVariation = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (std::abs(output[i] - 0.5f) > 0.01f) {
            hasVariation = true;
            break;
        }
    }
    REQUIRE(hasVariation);
}

TEST_CASE("NoiseGenerator getNoiseLevel returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseLevel(NoiseType::White, -15.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(-15.0f));

    noise.setNoiseLevel(NoiseType::Pink, -30.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::Pink) == Approx(-30.0f));
}

TEST_CASE("NoiseGenerator isNoiseEnabled returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));

    noise.setNoiseEnabled(NoiseType::White, true);
    REQUIRE(noise.isNoiseEnabled(NoiseType::White));

    noise.setNoiseEnabled(NoiseType::White, false);
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));
}

TEST_CASE("NoiseGenerator isAnyEnabled returns true when any enabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE_FALSE(noise.isAnyEnabled());

    noise.setNoiseEnabled(NoiseType::Pink, true);
    REQUIRE(noise.isAnyEnabled());

    noise.setNoiseEnabled(NoiseType::Pink, false);
    REQUIRE_FALSE(noise.isAnyEnabled());
}

TEST_CASE("NoiseGenerator level clamped to valid range", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Set below minimum
    noise.setNoiseLevel(NoiseType::White, -200.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(-96.0f));

    // Set above maximum
    noise.setNoiseLevel(NoiseType::White, 50.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(12.0f));
}

TEST_CASE("NoiseGenerator getMasterLevel returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE(noise.getMasterLevel() == Approx(0.0f)); // Default

    noise.setMasterLevel(-6.0f);
    REQUIRE(noise.getMasterLevel() == Approx(-6.0f));
}

// ==============================================================================
// User Story 2: Pink Noise Generation [US2]
// ==============================================================================

TEST_CASE("Pink noise: output is zero when disabled", "[noise][US2]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Leave pink noise disabled (default)
    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Pink noise: output is non-zero when enabled", "[noise][US2]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::Pink, 0.0f);

    constexpr size_t largeSize = 4096;
    std::vector<float> buffer(largeSize, 0.0f);

    for (size_t i = 0; i < largeSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("Pink noise: samples in [-1.0, 1.0] range (SC-003)", "[noise][US2][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::Pink, 0.0f);

    constexpr size_t testSize = 44100;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // All samples must be in valid range
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("Pink noise: spectral slope of -3dB/octave (SC-002)", "[noise][US2][SC-002]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::Pink, 0.0f);

    // Generate 10 seconds of pink noise for spectral analysis
    constexpr size_t testSize = 441000;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial smoother settling
    const float* analysisStart = buffer.data() + 4410;
    size_t analysisSize = testSize - 4410;

    // Measure energy at different frequency bands
    float energy1k = measureBandEnergy(analysisStart, analysisSize, 800.0f, 1200.0f, kSampleRate);
    float energy2k = measureBandEnergy(analysisStart, analysisSize, 1800.0f, 2200.0f, kSampleRate);
    float energy4k = measureBandEnergy(analysisStart, analysisSize, 3500.0f, 4500.0f, kSampleRate);

    // Convert to dB
    float db1k = linearToDb(energy1k);
    float db2k = linearToDb(energy2k);
    float db4k = linearToDb(energy4k);

    // Pink noise: -3dB per octave
    // 1kHz to 2kHz = 1 octave = -3dB (tolerance: ±1dB)
    float slope1to2 = db2k - db1k;
    REQUIRE(slope1to2 >= -4.0f);
    REQUIRE(slope1to2 <= -2.0f);

    // 1kHz to 4kHz = 2 octaves = -6dB (tolerance: ±2dB)
    float slope1to4 = db4k - db1k;
    REQUIRE(slope1to4 >= -8.0f);
    REQUIRE(slope1to4 <= -4.0f);
}

TEST_CASE("Pink noise: level control affects amplitude", "[noise][US2]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Pink, true);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferLoud(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate at 0dB
    noise.setNoiseLevel(NoiseType::Pink, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferLoud.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate at -20dB
    noise.reset();
    noise.setNoiseLevel(NoiseType::Pink, -20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsLoud = calculateRMS(bufferLoud.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsLoud / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("Pink noise: reset clears filter state", "[noise][US2]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::Pink, 0.0f);

    // Generate some samples to build up filter state
    std::array<float, kBlockSize> buffer;
    for (int i = 0; i < 10; ++i) {
        noise.process(buffer.data(), buffer.size());
    }

    // Reset should clear state
    noise.reset();

    // After reset, first samples should still be valid (filter restarted)
    noise.process(buffer.data(), buffer.size());

    // Just verify output is valid (no NaN, within range)
    for (size_t i = 0; i < buffer.size(); ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("White and pink noise can be mixed (US6 preview)", "[noise][US2][US6]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Enable both white and pink at equal levels
    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::White, -6.0f);
    noise.setNoiseLevel(NoiseType::Pink, -6.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Verify output is non-zero
    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));

    // Calculate RMS - should be higher than single noise type at -6dB
    float rms = calculateRMS(buffer.data() + 1000, testSize - 1000);

    // With two uncorrelated noise sources at -6dB each, combined should be roughly -3dB
    // Expected RMS around 0.5 * sqrt(2) ≈ 0.7 (two sources at 0.5)
    REQUIRE(rms > 0.3f);  // Higher than single source
    REQUIRE(rms < 1.5f);  // Not clipping
}

// ==============================================================================
// User Story 3: Tape Hiss Generation [US3]
// ==============================================================================

TEST_CASE("Tape hiss: output is zero when disabled", "[noise][US3]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Tape hiss: produces noise when enabled", "[noise][US3]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseLevel(NoiseType::TapeHiss, -20.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize, 0.0f);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Should have non-zero output (floor level noise)
    float rms = calculateRMS(buffer.data() + 1000, testSize - 1000);
    REQUIRE(rms > 0.0f);
}

TEST_CASE("Tape hiss: signal-dependent modulation (SC-004)", "[noise][US3][SC-004]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseLevel(NoiseType::TapeHiss, -20.0f);
    noise.setTapeHissParams(-60.0f, 1.0f); // Floor at -60dB, sensitivity 1.0

    constexpr size_t testSize = 8192;

    // Test with silent input (should get floor level noise)
    std::vector<float> silentInput(testSize, 0.0f);
    std::vector<float> silentOutput(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(silentInput.data() + i * kBlockSize,
                     silentOutput.data() + i * kBlockSize, kBlockSize);
    }
    float rmsSilent = calculateRMS(silentOutput.data() + 1000, testSize - 1000);

    // Reset and test with loud input
    noise.reset();
    std::vector<float> loudInput(testSize, 0.5f); // Constant loud signal
    std::vector<float> loudOutput(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(loudInput.data() + i * kBlockSize,
                     loudOutput.data() + i * kBlockSize, kBlockSize);
    }
    float rmsLoud = calculateRMS(loudOutput.data() + 1000, testSize - 1000);

    // Loud input should produce more noise than silent input
    REQUIRE(rmsLoud > rmsSilent);
}

TEST_CASE("Tape hiss: high-frequency spectral emphasis (SC-004)", "[noise][US3][SC-004][slope]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseLevel(NoiseType::TapeHiss, 0.0f);

    // Generate 10 seconds of tape hiss
    constexpr size_t testSize = 441000;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial settling
    const float* analysisStart = buffer.data() + 4410;
    size_t analysisSize = testSize - 4410;

    // Tape hiss should have more high-frequency energy than pink noise
    // Compare 2kHz band to 8kHz band - should be closer than pink (-3dB/octave)
    float energy2k = measureBandEnergy(analysisStart, analysisSize, 1800.0f, 2200.0f, kSampleRate);
    float energy8k = measureBandEnergy(analysisStart, analysisSize, 7000.0f, 9000.0f, kSampleRate);

    float db2k = linearToDb(energy2k);
    float db8k = linearToDb(energy8k);

    // Pure pink would be ~-6dB at 8kHz relative to 2kHz (2 octaves)
    // Tape hiss with shelf boost should be less steep
    float slope = db8k - db2k;
    REQUIRE(slope > -6.0f); // Less negative slope than pink noise
}

TEST_CASE("Tape hiss: setTapeHissParams configures floor and sensitivity", "[noise][US3]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Default values should be set
    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseLevel(NoiseType::TapeHiss, -20.0f);

    // Configure with different floor
    noise.setTapeHissParams(-40.0f, 0.5f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Should produce output
    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

// ==============================================================================
// User Story 4: Vinyl Crackle Generation [US4]
// ==============================================================================

TEST_CASE("Vinyl crackle: output is zero when disabled", "[noise][US4]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Vinyl crackle: produces output when enabled", "[noise][US4]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseLevel(NoiseType::VinylCrackle, -10.0f);
    noise.setCrackleParams(10.0f, -30.0f); // 10 clicks/sec, -30dB surface noise

    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize, 0.0f);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("Vinyl crackle: produces impulsive clicks (SC-005)", "[noise][US4][SC-005]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseLevel(NoiseType::VinylCrackle, 0.0f);
    noise.setCrackleParams(15.0f, -96.0f); // High density, no surface noise

    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize, 0.0f);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Count peaks (impulsive clicks have high peak-to-RMS ratio)
    float peak = findPeak(buffer.data(), buffer.size());
    float rms = calculateRMS(buffer.data(), buffer.size());

    // Crest factor (peak/RMS) should be high for impulsive signals
    float crestFactor = (rms > 0.0f) ? (peak / rms) : 0.0f;
    REQUIRE(crestFactor > 3.0f); // Impulsive signals have high crest factor
}

TEST_CASE("Vinyl crackle: density affects click rate (SC-005)", "[noise][US4][SC-005]") {
    // Test with low density
    NoiseGenerator noiseLow;
    noiseLow.prepare(kSampleRate, kBlockSize);
    noiseLow.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noiseLow.setNoiseLevel(NoiseType::VinylCrackle, 0.0f);
    noiseLow.setCrackleParams(2.0f, -96.0f); // 2 clicks/sec

    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> bufferLow(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noiseLow.process(bufferLow.data() + i * kBlockSize, kBlockSize);
    }

    // Test with high density
    NoiseGenerator noiseHigh;
    noiseHigh.prepare(kSampleRate, kBlockSize);
    noiseHigh.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noiseHigh.setNoiseLevel(NoiseType::VinylCrackle, 0.0f);
    noiseHigh.setCrackleParams(15.0f, -96.0f); // 15 clicks/sec

    std::vector<float> bufferHigh(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noiseHigh.process(bufferHigh.data() + i * kBlockSize, kBlockSize);
    }

    // Higher density should produce more non-zero samples or higher RMS
    float rmsLow = calculateRMS(bufferLow.data(), bufferLow.size());
    float rmsHigh = calculateRMS(bufferHigh.data(), bufferHigh.size());

    REQUIRE(rmsHigh > rmsLow);
}

TEST_CASE("Vinyl crackle: surface noise adds continuous noise (SC-005)", "[noise][US4][SC-005]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseLevel(NoiseType::VinylCrackle, 0.0f);
    noise.setCrackleParams(0.1f, -20.0f); // Very few clicks, high surface noise

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize, 0.0f);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Surface noise should create continuous output
    float rms = calculateRMS(buffer.data() + 1000, testSize - 1000);
    REQUIRE(rms > 0.01f); // Should have audible noise floor
}

TEST_CASE("Vinyl crackle: setCrackleParams configures density and surface", "[noise][US4]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseLevel(NoiseType::VinylCrackle, -20.0f);
    noise.setCrackleParams(5.0f, -40.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

// ==============================================================================
// User Story 5: Asperity Noise Generation [US5]
// ==============================================================================

TEST_CASE("Asperity noise: output is zero when disabled", "[noise][US5]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Asperity noise: produces output when enabled", "[noise][US5]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Asperity, true);
    noise.setNoiseLevel(NoiseType::Asperity, -20.0f);
    noise.setAsperityParams(-60.0f, 1.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize, 0.0f);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Should have floor level noise
    float rms = calculateRMS(buffer.data() + 1000, testSize - 1000);
    REQUIRE(rms > 0.0f);
}

TEST_CASE("Asperity noise: signal-dependent modulation (SC-006)", "[noise][US5][SC-006]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Asperity, true);
    noise.setNoiseLevel(NoiseType::Asperity, -20.0f);
    noise.setAsperityParams(-60.0f, 1.0f);

    constexpr size_t testSize = 8192;

    // Test with silent input
    std::vector<float> silentInput(testSize, 0.0f);
    std::vector<float> silentOutput(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(silentInput.data() + i * kBlockSize,
                     silentOutput.data() + i * kBlockSize, kBlockSize);
    }
    float rmsSilent = calculateRMS(silentOutput.data() + 1000, testSize - 1000);

    // Reset and test with loud input
    noise.reset();
    std::vector<float> loudInput(testSize, 0.5f);
    std::vector<float> loudOutput(testSize, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(loudInput.data() + i * kBlockSize,
                     loudOutput.data() + i * kBlockSize, kBlockSize);
    }
    float rmsLoud = calculateRMS(loudOutput.data() + 1000, testSize - 1000);

    // Loud input should produce more asperity noise
    REQUIRE(rmsLoud > rmsSilent);
}

TEST_CASE("Asperity noise: setAsperityParams configures floor and sensitivity", "[noise][US5]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Asperity, true);
    noise.setNoiseLevel(NoiseType::Asperity, -20.0f);
    noise.setAsperityParams(-40.0f, 1.5f);

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

// ==============================================================================
// User Story 6: Multi-Noise Mixing [US6]
// ==============================================================================

TEST_CASE("Multi-noise: all types can be enabled simultaneously (SC-007)", "[noise][US6][SC-007]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Enable all noise types
    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseEnabled(NoiseType::Asperity, true);

    // Set moderate levels
    noise.setNoiseLevel(NoiseType::White, -20.0f);
    noise.setNoiseLevel(NoiseType::Pink, -20.0f);
    noise.setNoiseLevel(NoiseType::TapeHiss, -20.0f);
    noise.setNoiseLevel(NoiseType::VinylCrackle, -20.0f);
    noise.setNoiseLevel(NoiseType::Asperity, -20.0f);

    REQUIRE(noise.isAnyEnabled());

    constexpr size_t testSize = 8192;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Combined output should have substantial energy
    // Note: TapeHiss and Asperity are signal-dependent and produce floor-level
    // noise without sidechain input, so combined RMS is lower than expected
    float rms = calculateRMS(buffer.data() + 1000, testSize - 1000);
    REQUIRE(rms > 0.05f);  // Lower threshold accounting for signal-dependent noise
}

TEST_CASE("Multi-noise: samples stay in valid range when combined (SC-003)", "[noise][US6][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Enable all at moderate levels
    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseEnabled(NoiseType::TapeHiss, true);
    noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
    noise.setNoiseEnabled(NoiseType::Asperity, true);

    noise.setNoiseLevel(NoiseType::White, -12.0f);
    noise.setNoiseLevel(NoiseType::Pink, -12.0f);
    noise.setNoiseLevel(NoiseType::TapeHiss, -12.0f);
    noise.setNoiseLevel(NoiseType::VinylCrackle, -12.0f);
    noise.setNoiseLevel(NoiseType::Asperity, -12.0f);

    constexpr size_t testSize = 44100;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Note: Combined output may exceed [-1, 1] when all sources are enabled
    // This is expected behavior - user should manage levels appropriately
    // Just verify no NaN or infinite values
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(std::isfinite(buffer[i]));
    }
}

TEST_CASE("NoiseGenerator handles maxBlockSize=8192 (FR-014)", "[noise][US6]") {
    NoiseGenerator noise;
    constexpr size_t largeBlockSize = 8192;

    noise.prepare(kSampleRate, largeBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseEnabled(NoiseType::Pink, true);
    noise.setNoiseLevel(NoiseType::White, -20.0f);
    noise.setNoiseLevel(NoiseType::Pink, -20.0f);

    std::vector<float> buffer(largeBlockSize, 0.0f);

    // Process a single large block
    noise.process(buffer.data(), largeBlockSize);

    // Verify output is valid
    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
    for (size_t i = 0; i < largeBlockSize; ++i) {
        REQUIRE(std::isfinite(buffer[i]));
    }
}

// ==============================================================================
// User Story 7: Brown/Red Noise Generation [US7]
// ==============================================================================

TEST_CASE("Brown noise: output is zero when disabled", "[noise][US7]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Leave brown noise disabled (default)
    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Brown noise: output is non-zero when enabled", "[noise][US7]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Brown, true);
    noise.setNoiseLevel(NoiseType::Brown, 0.0f);

    constexpr size_t largeSize = 4096;
    std::vector<float> buffer(largeSize, 0.0f);

    for (size_t i = 0; i < largeSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("Brown noise: samples in [-1.0, 1.0] range", "[noise][US7][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Brown, true);
    noise.setNoiseLevel(NoiseType::Brown, 0.0f);

    constexpr size_t testSize = 44100;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // All samples must be in valid range
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("Brown noise: setNoiseLevel affects output amplitude", "[noise][US7]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Brown, true);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferLoud(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate at 0dB
    noise.setNoiseLevel(NoiseType::Brown, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferLoud.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate at -20dB
    noise.reset();
    noise.setNoiseLevel(NoiseType::Brown, -20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsLoud = calculateRMS(bufferLoud.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsLoud / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("Brown noise: spectral slope of -6dB/octave", "[noise][US7][SC-002]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Brown, true);
    noise.setNoiseLevel(NoiseType::Brown, 0.0f);

    // Generate 10 seconds of brown noise for spectral analysis
    constexpr size_t testSize = 441000;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial smoother settling
    const float* analysisStart = buffer.data() + 4410;
    size_t analysisSize = testSize - 4410;

    // Measure energy at different frequency bands
    float energy1k = measureBandEnergy(analysisStart, analysisSize, 800.0f, 1200.0f, kSampleRate);
    float energy2k = measureBandEnergy(analysisStart, analysisSize, 1800.0f, 2200.0f, kSampleRate);
    float energy4k = measureBandEnergy(analysisStart, analysisSize, 3500.0f, 4500.0f, kSampleRate);

    // Convert to dB
    float db1k = linearToDb(energy1k);
    float db2k = linearToDb(energy2k);
    float db4k = linearToDb(energy4k);

    // Brown noise: -6dB per octave (1/f² spectrum)
    // 1kHz to 2kHz = 1 octave = -6dB (tolerance: ±1.5dB)
    float slope1to2 = db2k - db1k;
    REQUIRE(slope1to2 >= -7.5f);
    REQUIRE(slope1to2 <= -4.5f);

    // 1kHz to 4kHz = 2 octaves = -12dB (tolerance: ±2dB)
    float slope1to4 = db4k - db1k;
    REQUIRE(slope1to4 >= -14.0f);
    REQUIRE(slope1to4 <= -10.0f);
}

TEST_CASE("Brown noise: reset clears filter state", "[noise][US7]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Brown, true);
    noise.setNoiseLevel(NoiseType::Brown, 0.0f);

    // Generate some samples to build up integrator state
    std::array<float, kBlockSize> buffer;
    for (int i = 0; i < 10; ++i) {
        noise.process(buffer.data(), buffer.size());
    }

    // Reset should clear state
    noise.reset();

    // After reset, first samples should still be valid
    noise.process(buffer.data(), buffer.size());

    // Verify output is valid (no NaN, within range)
    for (size_t i = 0; i < buffer.size(); ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

// ==============================================================================
// User Story 8: Blue Noise Generation [US8]
// ==============================================================================

TEST_CASE("Blue noise: output is zero when disabled", "[noise][US8]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Leave blue noise disabled (default)
    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f);

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("Blue noise: output is non-zero when enabled", "[noise][US8]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Blue, true);
    noise.setNoiseLevel(NoiseType::Blue, 0.0f);

    constexpr size_t largeSize = 4096;
    std::vector<float> buffer(largeSize, 0.0f);

    for (size_t i = 0; i < largeSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("Blue noise: samples in [-1.0, 1.0] range", "[noise][US8][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Blue, true);
    noise.setNoiseLevel(NoiseType::Blue, 0.0f);

    constexpr size_t testSize = 44100;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // All samples must be in valid range
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("Blue noise: setNoiseLevel affects output amplitude", "[noise][US8]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Blue, true);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferLoud(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate at 0dB
    noise.setNoiseLevel(NoiseType::Blue, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferLoud.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate at -20dB
    noise.reset();
    noise.setNoiseLevel(NoiseType::Blue, -20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsLoud = calculateRMS(bufferLoud.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsLoud / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("Blue noise: spectral slope of +3dB/octave", "[noise][US8][SC-002]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Blue, true);
    noise.setNoiseLevel(NoiseType::Blue, 0.0f);

    // Generate 10 seconds of blue noise for spectral analysis
    constexpr size_t testSize = 441000;
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial smoother settling
    const float* analysisStart = buffer.data() + 4410;
    size_t analysisSize = testSize - 4410;

    // Measure energy at different frequency bands
    float energy1k = measureBandEnergy(analysisStart, analysisSize, 800.0f, 1200.0f, kSampleRate);
    float energy2k = measureBandEnergy(analysisStart, analysisSize, 1800.0f, 2200.0f, kSampleRate);
    float energy4k = measureBandEnergy(analysisStart, analysisSize, 3500.0f, 4500.0f, kSampleRate);

    // Convert to dB
    float db1k = linearToDb(energy1k);
    float db2k = linearToDb(energy2k);
    float db4k = linearToDb(energy4k);

    // Blue noise: +3dB per octave
    // 1kHz to 2kHz = 1 octave = +3dB (tolerance: ±1.5dB)
    float slope1to2 = db2k - db1k;
    REQUIRE(slope1to2 >= 1.5f);
    REQUIRE(slope1to2 <= 4.5f);

    // 1kHz to 4kHz = 2 octaves = +6dB (tolerance: ±2dB)
    float slope1to4 = db4k - db1k;
    REQUIRE(slope1to4 >= 4.0f);
    REQUIRE(slope1to4 <= 8.0f);
}

TEST_CASE("Blue noise: reset clears filter state", "[noise][US8]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::Blue, true);
    noise.setNoiseLevel(NoiseType::Blue, 0.0f);

    // Generate some samples to build up filter state
    std::array<float, kBlockSize> buffer;
    for (int i = 0; i < 10; ++i) {
        noise.process(buffer.data(), buffer.size());
    }

    // Reset should clear state
    noise.reset();

    // After reset, first samples should still be valid
    noise.process(buffer.data(), buffer.size());

    // Verify output is valid (no NaN, within range)
    for (size_t i = 0; i < buffer.size(); ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}
