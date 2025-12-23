// ==============================================================================
// Layer 1: DSP Primitive Tests - STFT and OverlapAdd
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/primitives/stft.h
// Contract: specs/007-fft-processor/contracts/fft_processor.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/stft.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

// Use namespace-qualified constants to avoid conflicts with window_functions.h
constexpr size_t kTestFFTSize = 1024;
constexpr float kTestSampleRate = 44100.0f;

// ==============================================================================
// Helper Functions
// ==============================================================================

/// Generate sine wave at specific frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);  // uses Iterum::DSP::kTwoPi via using namespace
    }
}

/// Calculate RMS of buffer
inline float calculateRMS(const float* buffer, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// Calculate relative error between two buffers
inline float calculateRelativeError(const float* a, const float* b, size_t size) {
    float sumSquaredError = 0.0f;
    float sumSquaredA = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diff = a[i] - b[i];
        sumSquaredError += diff * diff;
        sumSquaredA += a[i] * a[i];
    }
    if (sumSquaredA < 1e-10f) return 0.0f;
    return std::sqrt(sumSquaredError / sumSquaredA) * 100.0f;  // Percentage
}

// ==============================================================================
// STFT::prepare() Tests (T066)
// ==============================================================================

TEST_CASE("STFT prepare with different window types", "[stft][prepare][US3]") {
    STFT stft;

    SECTION("prepare with Hann window") {
        stft.prepare(1024, 512, WindowType::Hann);
        REQUIRE(stft.isPrepared());
        REQUIRE(stft.fftSize() == 1024);
        REQUIRE(stft.hopSize() == 512);
        REQUIRE(stft.windowType() == WindowType::Hann);
    }

    SECTION("prepare with Hamming window") {
        stft.prepare(1024, 256, WindowType::Hamming);
        REQUIRE(stft.windowType() == WindowType::Hamming);
        REQUIRE(stft.hopSize() == 256);
    }

    SECTION("prepare with Blackman window") {
        stft.prepare(2048, 512, WindowType::Blackman);
        REQUIRE(stft.fftSize() == 2048);
        REQUIRE(stft.windowType() == WindowType::Blackman);
    }

    SECTION("prepare with Kaiser window") {
        stft.prepare(1024, 256, WindowType::Kaiser, 9.0f);
        REQUIRE(stft.windowType() == WindowType::Kaiser);
    }
}

// ==============================================================================
// pushSamples()/canAnalyze() Tests (T067)
// ==============================================================================

TEST_CASE("STFT sample accumulation", "[stft][input][US3]") {
    STFT stft;
    stft.prepare(1024, 512, WindowType::Hann);

    std::vector<float> samples(256, 0.5f);

    SECTION("canAnalyze is false initially") {
        REQUIRE_FALSE(stft.canAnalyze());
    }

    SECTION("canAnalyze after pushing enough samples") {
        // Need to push fftSize samples for first frame
        for (int i = 0; i < 4; ++i) {
            stft.pushSamples(samples.data(), samples.size());
        }
        REQUIRE(stft.canAnalyze());
    }

    SECTION("canAnalyze after analyze consumes samples") {
        // Fill buffer
        std::vector<float> fullBuffer(1024, 0.5f);
        stft.pushSamples(fullBuffer.data(), fullBuffer.size());
        REQUIRE(stft.canAnalyze());

        SpectralBuffer spectrum;
        spectrum.prepare(1024);
        stft.analyze(spectrum);

        // After analyze, need hopSize more samples
        REQUIRE_FALSE(stft.canAnalyze());

        // Push hop size samples
        std::vector<float> hopSamples(512, 0.5f);
        stft.pushSamples(hopSamples.data(), hopSamples.size());
        REQUIRE(stft.canAnalyze());
    }
}

// ==============================================================================
// analyze() Tests (T068)
// ==============================================================================

TEST_CASE("STFT analyze applies window correctly", "[stft][analyze][US3]") {
    STFT stft;
    stft.prepare(1024, 512, WindowType::Hann);

    SpectralBuffer spectrum;
    spectrum.prepare(1024);

    // Create test signal
    std::vector<float> input(1024);
    generateSine(input.data(), 1024, 440.0f, kTestSampleRate);

    stft.pushSamples(input.data(), input.size());
    REQUIRE(stft.canAnalyze());

    stft.analyze(spectrum);

    SECTION("spectrum is populated") {
        // Find peak bin
        float maxMag = 0.0f;
        for (size_t i = 0; i < spectrum.numBins(); ++i) {
            float mag = spectrum.getMagnitude(i);
            if (mag > maxMag) maxMag = mag;
        }
        REQUIRE(maxMag > 0.0f);
    }
}

// ==============================================================================
// Hop Size Tests (T069)
// ==============================================================================

TEST_CASE("STFT different hop sizes", "[stft][hopsize][US3]") {
    SECTION("50% overlap (hop = fftSize/2)") {
        STFT stft;
        stft.prepare(1024, 512, WindowType::Hann);
        REQUIRE(stft.hopSize() == 512);
        REQUIRE(stft.latency() == 1024);
    }

    SECTION("75% overlap (hop = fftSize/4)") {
        STFT stft;
        stft.prepare(1024, 256, WindowType::Hann);
        REQUIRE(stft.hopSize() == 256);
    }
}

// ==============================================================================
// Continuous Streaming Tests (T070)
// ==============================================================================

TEST_CASE("STFT continuous streaming", "[stft][streaming][US3]") {
    STFT stft;
    stft.prepare(512, 256, WindowType::Hann);

    SpectralBuffer spectrum;
    spectrum.prepare(512);

    // Push samples in small chunks
    std::vector<float> chunk(64);
    generateSine(chunk.data(), 64, 1000.0f, kTestSampleRate);

    int analyzeCount = 0;

    // Process enough samples for multiple frames
    for (int i = 0; i < 32; ++i) {
        stft.pushSamples(chunk.data(), chunk.size());

        while (stft.canAnalyze()) {
            stft.analyze(spectrum);
            analyzeCount++;
        }
    }

    SECTION("multiple frames are analyzed") {
        // With 32*64=2048 samples and hopSize=256, expect ~8 frames
        REQUIRE(analyzeCount >= 6);
    }
}

// ==============================================================================
// OverlapAdd::prepare() Tests (T080)
// ==============================================================================

TEST_CASE("OverlapAdd prepare", "[ola][prepare][US4]") {
    OverlapAdd ola;

    SECTION("prepare allocates correctly") {
        ola.prepare(1024, 512);
        REQUIRE(ola.isPrepared());
        REQUIRE(ola.fftSize() == 1024);
        REQUIRE(ola.hopSize() == 512);
    }
}

// ==============================================================================
// OverlapAdd::synthesize() Tests (T081)
// ==============================================================================

TEST_CASE("OverlapAdd synthesize adds to accumulator", "[ola][synthesize][US4]") {
    OverlapAdd ola;
    ola.prepare(1024, 512);

    SpectralBuffer spectrum;
    spectrum.prepare(1024);

    // Set DC component
    spectrum.setCartesian(0, 1024.0f, 0.0f);

    ola.synthesize(spectrum);

    SECTION("samples become available after synthesize") {
        REQUIRE(ola.samplesAvailable() >= 512);
    }
}

// ==============================================================================
// pullSamples() Tests (T082)
// ==============================================================================

TEST_CASE("OverlapAdd pullSamples", "[ola][output][US4]") {
    OverlapAdd ola;
    ola.prepare(1024, 512);

    SpectralBuffer spectrum;
    spectrum.prepare(1024);

    // Create simple spectrum (DC only)
    spectrum.setCartesian(0, 512.0f, 0.0f);

    ola.synthesize(spectrum);

    std::vector<float> output(512);
    size_t available = ola.samplesAvailable();

    if (available >= 512) {
        ola.pullSamples(output.data(), 512);

        SECTION("output contains reconstructed samples") {
            // With DC spectrum, output should be constant
            float sum = 0.0f;
            for (float s : output) sum += s;
            REQUIRE(sum != 0.0f);  // Should have some output
        }
    }
}

// ==============================================================================
// STFT→ISTFT Round-Trip Tests (T083-T084)
// ==============================================================================

TEST_CASE("STFT-ISTFT round-trip Hann 50%", "[stft][ola][roundtrip][US4]") {
    const size_t fftSize = 1024;
    const size_t hopSize = 512;  // 50% overlap

    STFT stft;
    stft.prepare(fftSize, hopSize, WindowType::Hann);

    OverlapAdd ola;
    ola.prepare(fftSize, hopSize, WindowType::Hann);  // Must match STFT window

    SpectralBuffer spectrum;
    spectrum.prepare(fftSize);

    // Create test signal (longer than FFT size)
    const size_t signalLength = 4096;
    std::vector<float> input(signalLength);
    generateSine(input.data(), signalLength, 440.0f, kTestSampleRate);

    std::vector<float> output(signalLength, 0.0f);
    size_t outputWritten = 0;

    // Process through STFT→ISTFT
    stft.pushSamples(input.data(), signalLength);

    while (stft.canAnalyze() && outputWritten < signalLength - fftSize) {
        stft.analyze(spectrum);
        ola.synthesize(spectrum);

        if (ola.samplesAvailable() >= hopSize) {
            ola.pullSamples(output.data() + outputWritten, hopSize);
            outputWritten += hopSize;
        }
    }

    // Skip initial latency for comparison
    const size_t latency = fftSize;
    if (outputWritten > latency + 1024) {
        float error = calculateRelativeError(
            input.data() + latency,
            output.data() + latency,
            1024
        );

        SECTION("reconstruction error < 0.01%") {
            REQUIRE(error < 0.01f);  // SC-003
        }
    }
}

TEST_CASE("STFT-ISTFT round-trip Hann 75%", "[stft][ola][roundtrip][US4]") {
    const size_t fftSize = 1024;
    const size_t hopSize = 256;  // 75% overlap

    STFT stft;
    stft.prepare(fftSize, hopSize, WindowType::Hann);

    OverlapAdd ola;
    ola.prepare(fftSize, hopSize, WindowType::Hann);  // Must match STFT window

    SpectralBuffer spectrum;
    spectrum.prepare(fftSize);

    // Create test signal
    const size_t signalLength = 4096;
    std::vector<float> input(signalLength);
    generateSine(input.data(), signalLength, 1000.0f, kTestSampleRate);

    std::vector<float> output(signalLength, 0.0f);
    size_t outputWritten = 0;

    stft.pushSamples(input.data(), signalLength);

    while (stft.canAnalyze() && outputWritten < signalLength - fftSize) {
        stft.analyze(spectrum);
        ola.synthesize(spectrum);

        if (ola.samplesAvailable() >= hopSize) {
            ola.pullSamples(output.data() + outputWritten, hopSize);
            outputWritten += hopSize;
        }
    }

    const size_t latency = fftSize;
    if (outputWritten > latency + 1024) {
        float error = calculateRelativeError(
            input.data() + latency,
            output.data() + latency,
            1024
        );

        SECTION("reconstruction error < 0.01%") {
            REQUIRE(error < 0.01f);  // SC-003
        }
    }
}

// ==============================================================================
// COLA Verification Tests (T085)
// ==============================================================================

TEST_CASE("COLA property with different windows", "[stft][cola][US4]") {
    // This test verifies that STFT→ISTFT achieves unity gain
    // when using COLA-compliant windows at proper overlap

    SECTION("Hann at 50% overlap is COLA") {
        std::vector<float> window(1024);
        Window::generateHann(window.data(), window.size());
        REQUIRE(Window::verifyCOLA(window.data(), window.size(), 512));
    }

    SECTION("Hamming at 50% overlap is COLA") {
        std::vector<float> window(1024);
        Window::generateHamming(window.data(), window.size());
        REQUIRE(Window::verifyCOLA(window.data(), window.size(), 512));
    }

    SECTION("Blackman at 75% overlap is COLA") {
        std::vector<float> window(1024);
        Window::generateBlackman(window.data(), window.size());
        REQUIRE(Window::verifyCOLA(window.data(), window.size(), 256));
    }
}

// ==============================================================================
// Kaiser 90% Overlap Test (T085b)
// ==============================================================================

TEST_CASE("Kaiser window COLA at 90% overlap", "[stft][cola][kaiser][US4]") {
    // Kaiser window requires ~90% overlap for COLA compliance
    const size_t fftSize = 1024;
    const size_t hopSize = fftSize / 10;  // 90% overlap

    std::vector<float> window(fftSize);
    Window::generateKaiser(window.data(), fftSize, 9.0f);

    SECTION("Kaiser at 90% overlap achieves near-COLA") {
        bool isCOLA = Window::verifyCOLA(window.data(), fftSize, hopSize, 0.1f);
        REQUIRE(isCOLA);
    }
}

// ==============================================================================
// Real-Time Safety Tests (T095-T096)
// ==============================================================================

TEST_CASE("STFT process methods are noexcept", "[stft][realtime][US6]") {
    STFT stft;

    SECTION("pushSamples is noexcept") {
        static_assert(noexcept(stft.pushSamples(nullptr, 0)));
    }

    SECTION("canAnalyze is noexcept") {
        static_assert(noexcept(stft.canAnalyze()));
    }

    SECTION("reset is noexcept") {
        static_assert(noexcept(stft.reset()));
    }
}

TEST_CASE("OverlapAdd process methods are noexcept", "[ola][realtime][US6]") {
    OverlapAdd ola;

    SECTION("synthesize is noexcept") {
        SpectralBuffer sb;
        static_assert(noexcept(ola.synthesize(sb)));
    }

    SECTION("pullSamples is noexcept") {
        static_assert(noexcept(ola.pullSamples(nullptr, 0)));
    }

    SECTION("reset is noexcept") {
        static_assert(noexcept(ola.reset()));
    }
}

// ==============================================================================
// Integration Test: Full STFT Pipeline with Spectrum Modification (T104)
// ==============================================================================

TEST_CASE("Full STFT -> modify spectrum -> OLA pipeline", "[stft][ola][integration]") {
    const size_t fftSize = 1024;
    const size_t hopSize = 512;

    STFT stft;
    stft.prepare(fftSize, hopSize, WindowType::Hann);

    OverlapAdd ola;
    ola.prepare(fftSize, hopSize, WindowType::Hann);

    SpectralBuffer spectrum;
    spectrum.prepare(fftSize);

    SECTION("spectral gain modification produces scaled output") {
        // Create test signal
        const size_t signalLength = 4096;
        std::vector<float> input(signalLength);
        generateSine(input.data(), signalLength, 440.0f, kTestSampleRate);

        std::vector<float> output(signalLength, 0.0f);
        size_t outputWritten = 0;

        // Process with 2x gain in spectral domain
        stft.pushSamples(input.data(), signalLength);

        while (stft.canAnalyze() && outputWritten < signalLength - fftSize) {
            stft.analyze(spectrum);

            // Apply 2x gain to all bins
            for (size_t i = 0; i < spectrum.numBins(); ++i) {
                float mag = spectrum.getMagnitude(i);
                spectrum.setMagnitude(i, mag * 2.0f);
            }

            ola.synthesize(spectrum);

            while (ola.samplesAvailable() >= hopSize && outputWritten < signalLength - fftSize) {
                ola.pullSamples(output.data() + outputWritten, hopSize);
                outputWritten += hopSize;
            }
        }

        // Verify output is approximately 2x input (after latency)
        const size_t latency = fftSize;
        if (outputWritten > latency + 512) {
            float inputRMS = calculateRMS(input.data() + latency, 512);
            float outputRMS = calculateRMS(output.data() + latency, 512);

            float ratio = outputRMS / inputRMS;
            // Expect ratio close to 2.0, allowing for window effects
            REQUIRE(ratio > 1.5f);
            REQUIRE(ratio < 2.5f);
        }
    }

    SECTION("pass-through (no modification) maintains signal") {
        // This is essentially what the round-trip tests verify,
        // but organized as an integration test pattern

        const size_t signalLength = 4096;
        std::vector<float> input(signalLength);
        generateSine(input.data(), signalLength, 1000.0f, kTestSampleRate);

        std::vector<float> output(signalLength, 0.0f);
        size_t outputWritten = 0;

        // Reset for clean state
        stft.reset();
        ola.reset();

        stft.pushSamples(input.data(), signalLength);

        while (stft.canAnalyze() && outputWritten < signalLength - fftSize) {
            stft.analyze(spectrum);
            // No modification - pass through
            ola.synthesize(spectrum);

            while (ola.samplesAvailable() >= hopSize && outputWritten < signalLength - fftSize) {
                ola.pullSamples(output.data() + outputWritten, hopSize);
                outputWritten += hopSize;
            }
        }

        // Verify output matches input (after latency)
        const size_t latency = fftSize;
        if (outputWritten > latency + 1024) {
            float error = calculateRelativeError(
                input.data() + latency,
                output.data() + latency,
                1024
            );
            REQUIRE(error < 0.01f);  // < 0.01% error
        }
    }
}
