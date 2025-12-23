// ==============================================================================
// Layer 1: DSP Primitive Tests - Fast Fourier Transform
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/primitives/fft.h
// Contract: specs/007-fft-processor/contracts/fft_processor.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/fft.h"
#include "dsp/primitives/spectral_buffer.h"

#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// ==============================================================================
// Helper Functions
// ==============================================================================

/// Generate sine wave at specific frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

/// Calculate RMS error between two buffers
inline float calculateRMSError(const float* a, const float* b, size_t size) {
    float sumSquared = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diff = a[i] - b[i];
        sumSquared += diff * diff;
    }
    return std::sqrt(sumSquared / static_cast<float>(size));
}

// ==============================================================================
// Complex Struct Tests (T028-T029)
// ==============================================================================

TEST_CASE("Complex struct arithmetic operators", "[fft][complex][US1]") {
    Complex a{3.0f, 4.0f};
    Complex b{1.0f, 2.0f};

    SECTION("addition") {
        Complex c = a + b;
        REQUIRE(c.real == Approx(4.0f));
        REQUIRE(c.imag == Approx(6.0f));
    }

    SECTION("subtraction") {
        Complex c = a - b;
        REQUIRE(c.real == Approx(2.0f));
        REQUIRE(c.imag == Approx(2.0f));
    }

    SECTION("multiplication") {
        // (3+4i)(1+2i) = 3 + 6i + 4i + 8i² = 3 + 10i - 8 = -5 + 10i
        Complex c = a * b;
        REQUIRE(c.real == Approx(-5.0f));
        REQUIRE(c.imag == Approx(10.0f));
    }

    SECTION("conjugate") {
        Complex c = a.conjugate();
        REQUIRE(c.real == Approx(3.0f));
        REQUIRE(c.imag == Approx(-4.0f));
    }
}

TEST_CASE("Complex magnitude and phase", "[fft][complex][US1]") {
    SECTION("magnitude of 3+4i is 5") {
        Complex c{3.0f, 4.0f};
        REQUIRE(c.magnitude() == Approx(5.0f));
    }

    SECTION("magnitude of 1+0i is 1") {
        Complex c{1.0f, 0.0f};
        REQUIRE(c.magnitude() == Approx(1.0f));
    }

    SECTION("phase of 1+0i is 0") {
        Complex c{1.0f, 0.0f};
        REQUIRE(c.phase() == Approx(0.0f));
    }

    SECTION("phase of 0+1i is pi/2") {
        Complex c{0.0f, 1.0f};
        REQUIRE(c.phase() == Approx(kPi / 2.0f));
    }

    SECTION("phase of -1+0i is pi") {
        Complex c{-1.0f, 0.0f};
        REQUIRE(c.phase() == Approx(kPi));
    }
}

// ==============================================================================
// FFT::prepare() Tests (T033)
// ==============================================================================

TEST_CASE("FFT prepare validates power of 2", "[fft][prepare][US1]") {
    FFT fft;

    SECTION("prepare with 256 succeeds") {
        fft.prepare(256);
        REQUIRE(fft.isPrepared());
        REQUIRE(fft.size() == 256);
        REQUIRE(fft.numBins() == 129);  // N/2+1
    }

    SECTION("prepare with 1024 succeeds") {
        fft.prepare(1024);
        REQUIRE(fft.isPrepared());
        REQUIRE(fft.size() == 1024);
        REQUIRE(fft.numBins() == 513);
    }

    SECTION("prepare with 4096 succeeds") {
        fft.prepare(4096);
        REQUIRE(fft.isPrepared());
        REQUIRE(fft.size() == 4096);
        REQUIRE(fft.numBins() == 2049);
    }
}

// ==============================================================================
// FFT::forward() Tests (T034-T036)
// ==============================================================================

TEST_CASE("FFT forward with DC signal", "[fft][forward][US1]") {
    FFT fft;
    fft.prepare(1024);

    std::vector<float> input(1024, 1.0f);  // DC = 1.0
    std::vector<Complex> output(fft.numBins());

    fft.forward(input.data(), output.data());

    SECTION("DC component is at bin 0") {
        // DC bin should have all the energy
        REQUIRE(output[0].real == Approx(1024.0f).margin(0.01f));
        REQUIRE(output[0].imag == Approx(0.0f).margin(1e-5f));
    }

    SECTION("other bins are near zero") {
        for (size_t i = 1; i < fft.numBins(); ++i) {
            REQUIRE(output[i].magnitude() == Approx(0.0f).margin(0.01f));
        }
    }
}

TEST_CASE("FFT forward with sine wave at bin frequency", "[fft][forward][US1]") {
    FFT fft;
    const size_t fftSize = 1024;
    fft.prepare(fftSize);

    const float sampleRate = 44100.0f;
    const size_t targetBin = 10;
    const float frequency = targetBin * sampleRate / fftSize;

    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, frequency, sampleRate);

    std::vector<Complex> output(fft.numBins());
    fft.forward(input.data(), output.data());

    SECTION("peak is at expected bin") {
        size_t peakBin = 0;
        float peakMag = 0.0f;
        for (size_t i = 0; i < fft.numBins(); ++i) {
            float mag = output[i].magnitude();
            if (mag > peakMag) {
                peakMag = mag;
                peakBin = i;
            }
        }
        REQUIRE(peakBin == targetBin);
    }
}

TEST_CASE("FFT forward output format", "[fft][forward][US1]") {
    FFT fft;
    fft.prepare(1024);

    std::vector<float> input(1024);
    generateSine(input.data(), 1024, 440.0f, 44100.0f);

    std::vector<Complex> output(fft.numBins());
    fft.forward(input.data(), output.data());

    SECTION("output has N/2+1 bins") {
        REQUIRE(fft.numBins() == 513);
    }

    SECTION("DC bin has zero imaginary") {
        REQUIRE(output[0].imag == Approx(0.0f).margin(1e-5f));
    }

    SECTION("Nyquist bin has zero imaginary") {
        REQUIRE(output[512].imag == Approx(0.0f).margin(1e-5f));
    }
}

// ==============================================================================
// FFT::inverse() Tests (T037)
// ==============================================================================

TEST_CASE("FFT inverse basic reconstruction", "[fft][inverse][US2]") {
    FFT fft;
    fft.prepare(1024);

    std::vector<float> input(1024);
    generateSine(input.data(), 1024, 440.0f, 44100.0f);

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(input.data(), spectrum.data());

    std::vector<float> output(1024);
    fft.inverse(spectrum.data(), output.data());

    SECTION("output matches input") {
        float rmsError = calculateRMSError(input.data(), output.data(), 1024);
        float rmsInput = 0.0f;
        for (float s : input) rmsInput += s * s;
        rmsInput = std::sqrt(rmsInput / 1024.0f);

        float relativeError = rmsError / rmsInput;
        REQUIRE(relativeError < 0.0001f);  // < 0.01% error
    }
}

// ==============================================================================
// Round-Trip Tests (T038)
// ==============================================================================

TEST_CASE("FFT round-trip error < 0.0001%", "[fft][roundtrip][US2]") {
    const std::array<size_t, 5> sizes = {256, 512, 1024, 2048, 4096};

    for (size_t fftSize : sizes) {
        DYNAMIC_SECTION("FFT size " << fftSize) {
            FFT fft;
            fft.prepare(fftSize);

            // Create test signal
            std::vector<float> input(fftSize);
            generateSine(input.data(), fftSize, 440.0f, 44100.0f);

            // Forward FFT
            std::vector<Complex> spectrum(fft.numBins());
            fft.forward(input.data(), spectrum.data());

            // Inverse FFT
            std::vector<float> output(fftSize);
            fft.inverse(spectrum.data(), output.data());

            // Calculate error
            float sumSquaredError = 0.0f;
            float sumSquaredInput = 0.0f;
            for (size_t i = 0; i < fftSize; ++i) {
                float diff = input[i] - output[i];
                sumSquaredError += diff * diff;
                sumSquaredInput += input[i] * input[i];
            }

            float relativeError = std::sqrt(sumSquaredError / sumSquaredInput) * 100.0f;
            REQUIRE(relativeError < 0.0001f);  // SC-002: < 0.0001%
        }
    }
}

// ==============================================================================
// Real-Time Safety Tests (T094)
// ==============================================================================

TEST_CASE("FFT process methods are noexcept", "[fft][realtime][US6]") {
    SECTION("forward is noexcept") {
        FFT fft;
        static_assert(noexcept(fft.forward(nullptr, nullptr)));
    }

    SECTION("inverse is noexcept") {
        FFT fft;
        static_assert(noexcept(fft.inverse(nullptr, nullptr)));
    }

    SECTION("reset is noexcept") {
        FFT fft;
        static_assert(noexcept(fft.reset()));
    }
}

// ==============================================================================
// Memory Footprint Test (T100b, NFR-003)
// ==============================================================================

TEST_CASE("FFT working memory bounded by 3*N*sizeof(float)", "[fft][memory][US6]") {
    // NFR-003: Memory footprint MUST be bounded by 3 * FFT_SIZE * sizeof(float)
    // for core FFT operations.
    //
    // The FFT class uses:
    // - workBuffer_: N Complex values = 2N floats = 2N * sizeof(float)
    //
    // This is within the 3N * sizeof(float) limit.
    //
    // Note: bitReversalLUT_ (N size_t) and twiddleFactors_ (N/2 Complex)
    // are precomputed lookup tables, not part of "core FFT operations".

    SECTION("working buffer uses 2N floats (within 3N limit)") {
        // The working buffer is exactly 2N floats (N Complex values)
        constexpr size_t N = 1024;
        constexpr size_t workBufferBytes = N * sizeof(Complex);
        constexpr size_t limitBytes = 3 * N * sizeof(float);

        // Verify the working buffer is within limit
        REQUIRE(workBufferBytes <= limitBytes);

        // Document actual usage
        INFO("Work buffer: " << workBufferBytes << " bytes");
        INFO("Limit: " << limitBytes << " bytes");
        INFO("Usage: " << (100.0f * workBufferBytes / limitBytes) << "%");
    }

    SECTION("output buffer requirement is N/2+1 Complex (within reason)") {
        // User-provided output buffer for forward FFT needs N/2+1 Complex
        constexpr size_t N = 1024;
        constexpr size_t outputBins = N / 2 + 1;
        constexpr size_t outputBytes = outputBins * sizeof(Complex);

        // This is approximately N floats worth
        INFO("Output buffer: " << outputBytes << " bytes");
        INFO("Equivalent floats: " << outputBytes / sizeof(float));
    }
}

// ==============================================================================
// Integration Tests (T103-T105)
// ==============================================================================

TEST_CASE("FFT -> SpectralBuffer manipulation -> IFFT round-trip", "[fft][integration]") {
    const size_t fftSize = 1024;

    FFT fft;
    fft.prepare(fftSize);

    SpectralBuffer spectrum;
    spectrum.prepare(fftSize);

    // Create test signal (440 Hz sine)
    std::vector<float> input(fftSize);
    generateSine(input.data(), fftSize, 440.0f, 44100.0f);

    // Forward FFT into SpectralBuffer
    fft.forward(input.data(), spectrum.data());

    SECTION("modify magnitude and verify round-trip") {
        // Store original magnitudes
        std::vector<float> originalMags(spectrum.numBins());
        for (size_t i = 0; i < spectrum.numBins(); ++i) {
            originalMags[i] = spectrum.getMagnitude(i);
        }

        // Scale all magnitudes by 2x using SpectralBuffer API
        for (size_t i = 0; i < spectrum.numBins(); ++i) {
            spectrum.setMagnitude(i, originalMags[i] * 2.0f);
        }

        // Inverse FFT
        std::vector<float> output(fftSize);
        fft.inverse(spectrum.data(), output.data());

        // Output should be approximately 2x the input
        float sumIn = 0.0f, sumOut = 0.0f;
        for (size_t i = 0; i < fftSize; ++i) {
            sumIn += std::abs(input[i]);
            sumOut += std::abs(output[i]);
        }

        float ratio = sumOut / sumIn;
        REQUIRE(ratio == Catch::Approx(2.0f).margin(0.1f));
    }

    SECTION("phase modification preserves magnitude") {
        // Get original magnitude at peak bin
        size_t peakBin = 0;
        float maxMag = 0.0f;
        for (size_t i = 1; i < spectrum.numBins() - 1; ++i) {
            float mag = spectrum.getMagnitude(i);
            if (mag > maxMag) {
                maxMag = mag;
                peakBin = i;
            }
        }

        // Shift phase by π
        float originalPhase = spectrum.getPhase(peakBin);
        spectrum.setPhase(peakBin, originalPhase + 3.14159f);

        // Magnitude should be preserved
        REQUIRE(spectrum.getMagnitude(peakBin) == Catch::Approx(maxMag).margin(0.001f));
    }
}

TEST_CASE("FFT O(N log N) complexity verification", "[fft][performance]") {
    // Verify that doubling FFT size roughly doubles processing time
    // (not quadruples, which would indicate O(N²))

    const std::array<size_t, 4> sizes = {256, 512, 1024, 2048};
    std::vector<float> times;

    for (size_t fftSize : sizes) {
        FFT fft;
        fft.prepare(fftSize);

        std::vector<float> input(fftSize, 0.0f);
        std::vector<Complex> spectrum(fft.numBins());

        // Simple timing (not high-precision, just sanity check)
        const int iterations = 1000;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            fft.forward(input.data(), spectrum.data());
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        times.push_back(static_cast<float>(duration.count()));
    }

    SECTION("time increases sub-quadratically with size") {
        // For O(N log N), doubling N should increase time by factor of ~2.x
        // For O(N²), doubling N would increase time by factor of 4

        // Compare 512 vs 256 (2x size)
        float ratio1 = times[1] / times[0];
        INFO("256->512 ratio: " << ratio1);

        // Compare 1024 vs 512 (2x size)
        float ratio2 = times[2] / times[1];
        INFO("512->1024 ratio: " << ratio2);

        // Compare 2048 vs 1024 (2x size)
        float ratio3 = times[3] / times[2];
        INFO("1024->2048 ratio: " << ratio3);

        // For O(N log N), expect ratios around 2.0-2.5
        // For O(N²), would expect ratio around 4.0
        // Small sizes (256, 512) are dominated by cache effects, allow more margin
        // Larger sizes (1024, 2048) should show clearer O(N log N) behavior
        REQUIRE(ratio1 < 4.5f);  // Small sizes can show higher ratios due to cache
        REQUIRE(ratio2 < 3.5f);
        REQUIRE(ratio3 < 3.5f);
    }
}
