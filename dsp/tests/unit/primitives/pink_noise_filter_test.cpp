// ==============================================================================
// Unit Tests: PinkNoiseFilter
// ==============================================================================
// Layer 1: DSP Primitive Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// This file tests the extracted PinkNoiseFilter primitive that converts
// white noise to pink noise using Paul Kellet's algorithm.
//
// Reference: https://www.firstpr.com.au/dsp/pink-noise/
// Spec: specs/023-noise-oscillator/spec.md (RF-001 to RF-004)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/math_constants.h>

#include <array>
#include <cmath>
#include <vector>
#include <numeric>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kSampleRate = 44100.0f;

/// Measure spectral slope in dB/octave using 8192-pt FFT, 10 windows, Hann windowing
/// @param buffer Input samples
/// @param size Number of samples
/// @param freqLow Low frequency for slope measurement (Hz)
/// @param freqHigh High frequency for slope measurement (Hz)
/// @param sampleRate Sample rate in Hz
/// @return Spectral slope in dB/octave (negative for pink noise)
float measureSpectralSlope(const float* buffer, size_t size,
                           float freqLow, float freqHigh, float sampleRate) {
    constexpr size_t kFftSize = 8192;
    constexpr size_t kNumWindows = 10;

    if (size < kFftSize * kNumWindows) {
        // Not enough samples, use what we have
        return 0.0f;
    }

    FFT fft;
    fft.prepare(kFftSize);

    // Accumulate averaged magnitude spectrum
    std::vector<float> avgMagnitude(kFftSize / 2 + 1, 0.0f);

    // Process windows with 50% overlap
    size_t hopSize = size / kNumWindows;
    if (hopSize > kFftSize) hopSize = kFftSize;

    std::vector<float> windowedInput(kFftSize);
    std::vector<Complex> fftOutput(kFftSize / 2 + 1);

    for (size_t w = 0; w < kNumWindows; ++w) {
        size_t startIdx = w * hopSize;
        if (startIdx + kFftSize > size) break;

        // Apply Hann window
        for (size_t i = 0; i < kFftSize; ++i) {
            float hannCoeff = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) /
                                                     static_cast<float>(kFftSize));
            windowedInput[i] = buffer[startIdx + i] * hannCoeff;
        }

        // FFT
        fft.forward(windowedInput.data(), fftOutput.data());

        // Accumulate magnitude
        for (size_t i = 0; i < avgMagnitude.size(); ++i) {
            float mag = std::sqrt(fftOutput[i].real * fftOutput[i].real +
                                  fftOutput[i].imag * fftOutput[i].imag);
            avgMagnitude[i] += mag;
        }
    }

    // Average
    for (auto& m : avgMagnitude) {
        m /= static_cast<float>(kNumWindows);
    }

    // Calculate frequency resolution
    float binWidth = sampleRate / static_cast<float>(kFftSize);

    // Measure power at octave-spaced frequencies and perform linear regression
    // log2(f) vs dB gives slope in dB/octave
    std::vector<float> logFreqs;
    std::vector<float> dbValues;

    // Sample at octave-spaced frequencies from freqLow to freqHigh
    float freq = freqLow;
    while (freq <= freqHigh) {
        size_t bin = static_cast<size_t>(freq / binWidth);
        if (bin > 0 && bin < avgMagnitude.size()) {
            float mag = avgMagnitude[bin];
            if (mag > 1e-10f) {
                logFreqs.push_back(std::log2(freq));
                dbValues.push_back(20.0f * std::log10(mag));
            }
        }
        freq *= 2.0f; // Next octave
    }

    if (logFreqs.size() < 2) return 0.0f;

    // Linear regression: slope = (n*sum(xy) - sum(x)*sum(y)) / (n*sum(xx) - sum(x)^2)
    float n = static_cast<float>(logFreqs.size());
    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumXX = 0.0f;
    for (size_t i = 0; i < logFreqs.size(); ++i) {
        sumX += logFreqs[i];
        sumY += dbValues[i];
        sumXY += logFreqs[i] * dbValues[i];
        sumXX += logFreqs[i] * logFreqs[i];
    }

    float denominator = n * sumXX - sumX * sumX;
    if (std::abs(denominator) < 1e-10f) return 0.0f;

    return (n * sumXY - sumX * sumY) / denominator;
}

} // anonymous namespace

// ==============================================================================
// RF-002: Pink noise filter produces -3dB/octave slope
// ==============================================================================

TEST_CASE("Pink noise filter produces -3dB/octave slope", "[pink_noise_filter][RF-002]") {
    PinkNoiseFilter filter;
    Xorshift32 rng(12345);

    // Generate 10 seconds of pink noise at 44.1kHz
    constexpr size_t numSamples = 441000; // 10 seconds
    std::vector<float> pinkNoise(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float white = rng.nextFloat();
        pinkNoise[i] = filter.process(white);
    }

    // Measure spectral slope from 100Hz to 10kHz
    float slope = measureSpectralSlope(pinkNoise.data(), pinkNoise.size(),
                                        100.0f, 10000.0f, kSampleRate);

    // SC-003 specifies -3dB/octave +/- 0.5dB
    REQUIRE(slope == Approx(-3.0f).margin(0.5f));
}

// ==============================================================================
// RF-003: Pink noise filter reset clears state
// ==============================================================================

TEST_CASE("Pink noise filter reset clears state", "[pink_noise_filter][RF-003]") {
    PinkNoiseFilter filter1;
    PinkNoiseFilter filter2;
    Xorshift32 rng1(12345);
    Xorshift32 rng2(12345);

    // Process some samples through filter1
    for (int i = 0; i < 1000; ++i) {
        (void)filter1.process(rng1.nextFloat());
    }

    // Reset filter1
    filter1.reset();
    rng1.seed(12345);

    // Both filters should now produce identical output
    constexpr size_t testSize = 100;
    std::vector<float> output1(testSize);
    std::vector<float> output2(testSize);

    for (size_t i = 0; i < testSize; ++i) {
        output1[i] = filter1.process(rng1.nextFloat());
        output2[i] = filter2.process(rng2.nextFloat());
    }

    // Compare outputs
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(output1[i] == Approx(output2[i]).margin(1e-6f));
    }
}

// ==============================================================================
// RF-004: Pink noise filter bounds output to [-1, 1]
// ==============================================================================

TEST_CASE("Pink noise filter bounds output to [-1, 1]", "[pink_noise_filter][RF-004]") {
    PinkNoiseFilter filter;
    Xorshift32 rng(12345);

    // Generate 10 seconds of pink noise
    constexpr size_t numSamples = 441000; // 10 seconds at 44.1kHz

    bool allInBounds = true;
    float minSample = 1.0f;
    float maxSample = -1.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        float white = rng.nextFloat();
        float pink = filter.process(white);

        if (pink < minSample) minSample = pink;
        if (pink > maxSample) maxSample = pink;

        if (pink < -1.0f || pink > 1.0f) {
            allInBounds = false;
        }
    }

    INFO("Min sample: " << minSample);
    INFO("Max sample: " << maxSample);
    REQUIRE(allInBounds);
    REQUIRE(minSample >= -1.0f);
    REQUIRE(maxSample <= 1.0f);
}

// ==============================================================================
// Additional tests for filter behavior
// ==============================================================================

TEST_CASE("Pink noise filter produces deterministic output for same input", "[pink_noise_filter]") {
    PinkNoiseFilter filter1;
    PinkNoiseFilter filter2;

    // Feed same sequence to both filters
    std::array<float, 100> inputs;
    Xorshift32 rng(54321);
    for (auto& x : inputs) {
        x = rng.nextFloat();
    }

    std::array<float, 100> output1;
    std::array<float, 100> output2;

    for (size_t i = 0; i < inputs.size(); ++i) {
        output1[i] = filter1.process(inputs[i]);
        output2[i] = filter2.process(inputs[i]);
    }

    // Outputs should be identical (margin accounts for MSVC optimizer
    // reordering FP operations between the two filter instances)
    for (size_t i = 0; i < inputs.size(); ++i) {
        REQUIRE(output1[i] == Approx(output2[i]).margin(1e-7f));
    }
}

TEST_CASE("Pink noise filter output has lower high-frequency energy than white noise", "[pink_noise_filter]") {
    PinkNoiseFilter filter;
    Xorshift32 rng(99999);

    constexpr size_t numSamples = 44100; // 1 second
    std::vector<float> whiteNoise(numSamples);
    std::vector<float> pinkNoise(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float white = rng.nextFloat();
        whiteNoise[i] = white;
        pinkNoise[i] = filter.process(white);
    }

    // Measure high-frequency energy (5kHz-10kHz)
    FFT fft;
    fft.prepare(4096);

    std::vector<float> windowedWhite(4096);
    std::vector<float> windowedPink(4096);
    std::vector<Complex> whiteSpectrum(4096 / 2 + 1);
    std::vector<Complex> pinkSpectrum(4096 / 2 + 1);

    // Apply Hann window
    for (size_t i = 0; i < 4096; ++i) {
        float window = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / 4096.0f);
        windowedWhite[i] = whiteNoise[i] * window;
        windowedPink[i] = pinkNoise[i] * window;
    }

    fft.forward(windowedWhite.data(), whiteSpectrum.data());
    fft.forward(windowedPink.data(), pinkSpectrum.data());

    // Sum energy in high-frequency bins (5kHz-10kHz)
    float binWidth = kSampleRate / 4096.0f;
    size_t bin5k = static_cast<size_t>(5000.0f / binWidth);
    size_t bin10k = static_cast<size_t>(10000.0f / binWidth);

    float whiteHFEnergy = 0.0f;
    float pinkHFEnergy = 0.0f;

    for (size_t i = bin5k; i <= bin10k && i < whiteSpectrum.size(); ++i) {
        whiteHFEnergy += whiteSpectrum[i].real * whiteSpectrum[i].real +
                         whiteSpectrum[i].imag * whiteSpectrum[i].imag;
        pinkHFEnergy += pinkSpectrum[i].real * pinkSpectrum[i].real +
                        pinkSpectrum[i].imag * pinkSpectrum[i].imag;
    }

    // Pink noise should have significantly less HF energy
    INFO("White HF energy: " << whiteHFEnergy);
    INFO("Pink HF energy: " << pinkHFEnergy);
    REQUIRE(pinkHFEnergy < whiteHFEnergy * 0.5f); // At least 3dB less
}

// =============================================================================
// Vorago Phase 2 — FR-093 / SC-008: the 44.1 kHz anchor
// =============================================================================
// prepare() maps Kellet's poles to the running sample rate. The whole
// zero-regression argument for that change rests on ONE claim: at 44.1 kHz the
// mapping is the identity, so every existing consumer's output is untouched.
// That claim was previously asserted nowhere -- this file never called
// prepare() at all -- so a mapping that was subtly wrong at the reference rate
// would have shipped behind a green suite.
TEST_CASE("PinkNoiseFilter_RateAnchor", "[pink_noise_filter][vorago-phase2]") {
    constexpr std::size_t kNumSamples = 4096;

    // One shared white sequence, so the three filters differ only in coefficients.
    Krate::DSP::Xorshift32 rng(0xA11CEu);
    std::vector<float>     white(kNumSamples);
    for (float& w : white) {
        w = rng.nextFloat();
    }

    const auto render = [&white](Krate::DSP::PinkNoiseFilter& f) {
        std::vector<float> out;
        out.reserve(white.size());
        for (const float w : white) {
            out.push_back(f.process(w));
        }
        return out;
    };

    SECTION("prepare(44100) is EXACTLY the identity") {
        Krate::DSP::PinkNoiseFilter unprepared;
        Krate::DSP::PinkNoiseFilter anchored;
        anchored.prepare(44100.0f);

        const std::vector<float> a = render(unprepared);
        const std::vector<float> b = render(anchored);

        REQUIRE(a.size() == b.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) {
            worst = std::max(worst, std::fabs(a[i] - b[i]));
        }
        CAPTURE(worst);
        // BIT-exact, not approximately equal: pow(|a|, 1.0) returns a unchanged
        // and g * (1 - a) / (1 - a) is g, so the mapped coefficients are the
        // published ones. Any tolerance here would hide the defect this guards.
        REQUIRE(worst == 0.0f);

        // Not vacuous: the filter really did produce a signal.
        float peak = 0.0f;
        for (const float v : a) {
            peak = std::max(peak, std::fabs(v));
        }
        REQUIRE(peak > 1.0e-3f);
    }

    SECTION("a different rate really does move the coefficients") {
        // The anti-vacuity half. If prepare() were a no-op at EVERY rate the
        // section above would pass while the rate fix did nothing at all --
        // which is precisely the bug it exists to catch.
        Krate::DSP::PinkNoiseFilter anchored;
        anchored.prepare(44100.0f);
        Krate::DSP::PinkNoiseFilter resampled;
        resampled.prepare(96000.0f);

        const std::vector<float> a = render(anchored);
        const std::vector<float> b = render(resampled);

        float worst = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) {
            worst = std::max(worst, std::fabs(a[i] - b[i]));
        }
        CAPTURE(worst);
        REQUIRE(worst > 1.0e-4f);
    }

    SECTION("the mapping is stable and idempotent") {
        // prepare() must be callable repeatedly (consumers call it from their own
        // prepare()) without drifting the coefficients.
        Krate::DSP::PinkNoiseFilter once;
        once.prepare(96000.0f);
        Krate::DSP::PinkNoiseFilter thrice;
        thrice.prepare(96000.0f);
        thrice.prepare(96000.0f);
        thrice.prepare(96000.0f);

        const std::vector<float> a = render(once);
        const std::vector<float> b = render(thrice);
        for (std::size_t i = 0; i < a.size(); ++i) {
            REQUIRE(a[i] == b[i]);
        }
    }

    SECTION("a degenerate rate is floored, not divided by") {
        Krate::DSP::PinkNoiseFilter zero;
        zero.prepare(0.0f);
        Krate::DSP::PinkNoiseFilter negative;
        negative.prepare(-48000.0f);

        for (const float v : render(zero)) {
            REQUIRE(std::isfinite(v));
        }
        for (const float v : render(negative)) {
            REQUIRE(std::isfinite(v));
        }
    }
}
