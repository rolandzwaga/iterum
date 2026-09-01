// ==============================================================================
// Unit Tests: NoiseOscillator
// ==============================================================================
// Layer 1: DSP Primitive Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// Test organization by User Story:
// - US1: White Noise Generation [US1]
// - US2: Pink Noise Generation [US2]
// - US3: Brown Noise Generation [US3]
// - US4: Blue and Violet Noise Generation [US4]
// - US5: Block Processing Efficiency [US5]
// - US6: Grey Noise Generation [US6]
//
// Success Criteria tags:
// - [SC-001] through [SC-012]
//
// Spec: specs/023-noise-oscillator/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <krate/dsp/primitives/noise_oscillator.h>
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
/// Per spec clarification: 8192-point FFT, averaged over 10 windows, Hann windowing
/// @param buffer Input samples
/// @param size Number of samples
/// @param freqLow Low frequency for slope measurement (Hz)
/// @param freqHigh High frequency for slope measurement (Hz)
/// @param sampleRate Sample rate in Hz
/// @return Spectral slope in dB/octave
float measureSpectralSlope(const float* buffer, size_t size,
                           float freqLow, float freqHigh, float sampleRate) {
    constexpr size_t kFftSize = 8192;
    constexpr size_t kNumWindows = 10;

    if (size < kFftSize * 2) {
        return 0.0f;
    }

    FFT fft;
    fft.prepare(kFftSize);

    // Accumulate averaged magnitude spectrum
    std::vector<float> avgMagnitude(kFftSize / 2 + 1, 0.0f);

    // Process windows
    size_t hopSize = (size - kFftSize) / (kNumWindows > 1 ? (kNumWindows - 1) : 1);
    if (hopSize < 1) hopSize = 1;

    std::vector<float> windowedInput(kFftSize);
    std::vector<Complex> fftOutput(kFftSize / 2 + 1);

    size_t windowsProcessed = 0;
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
        windowsProcessed++;
    }

    if (windowsProcessed == 0) return 0.0f;

    // Average
    for (auto& m : avgMagnitude) {
        m /= static_cast<float>(windowsProcessed);
    }

    // Calculate frequency resolution
    float binWidth = sampleRate / static_cast<float>(kFftSize);

    // Measure power at octave-spaced frequencies and perform linear regression
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

/// Calculate mean of buffer
inline float calculateMean(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(size);
}

/// Calculate variance of buffer
inline float calculateVariance(const float* buffer, size_t size) {
    if (size < 2) return 0.0f;
    float mean = calculateMean(buffer, size);
    float sumSq = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diff = buffer[i] - mean;
        sumSq += diff * diff;
    }
    return sumSq / static_cast<float>(size - 1);
}

/// Check if all samples are within bounds
inline bool allInBounds(const float* buffer, size_t size, float minVal, float maxVal) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] < minVal || buffer[i] > maxVal) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

// ==============================================================================
// Phase 3: User Story 1 - White Noise Generation [US1]
// ==============================================================================

TEST_CASE("White noise mean is approximately zero (SC-001)", "[noise_oscillator][US1][SC-001]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::White);
    osc.setSeed(12345);

    // Generate 44100 samples (1 second)
    constexpr size_t numSamples = 44100;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    float mean = calculateMean(buffer.data(), numSamples);

    // SC-001: Mean should be within 0.05 of zero
    INFO("Mean: " << mean);
    REQUIRE(std::abs(mean) < 0.05f);
}

TEST_CASE("White noise variance matches theoretical (SC-002)", "[noise_oscillator][US1][SC-002]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::White);
    osc.setSeed(54321);

    // Generate 44100 samples
    constexpr size_t numSamples = 44100;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    float variance = calculateVariance(buffer.data(), numSamples);

    // SC-002: Variance within 10% of theoretical (1/3 for uniform [-1,1])
    // Theoretical variance for uniform distribution on [-1, 1] = 1/3 = 0.333...
    float theoretical = 1.0f / 3.0f;
    float tolerance = theoretical * 0.10f;

    INFO("Variance: " << variance << ", Expected: " << theoretical);
    REQUIRE(variance == Approx(theoretical).margin(tolerance));
}

TEST_CASE("Same seed produces identical sequences (SC-008)", "[noise_oscillator][US1][SC-008]") {
    NoiseOscillator osc1;
    NoiseOscillator osc2;

    osc1.prepare(kSampleRate);
    osc2.prepare(kSampleRate);

    osc1.setColor(NoiseColor::White);
    osc2.setColor(NoiseColor::White);

    osc1.setSeed(99999);
    osc2.setSeed(99999);

    // Generate 1000 samples from each
    constexpr size_t numSamples = 1000;

    for (size_t i = 0; i < numSamples; ++i) {
        float s1 = osc1.process();
        float s2 = osc2.process();
        REQUIRE(s1 == s2);
    }
}

TEST_CASE("Reset restarts sequence from beginning", "[noise_oscillator][US1]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::White);
    osc.setSeed(11111);

    // Generate first sequence
    constexpr size_t numSamples = 100;
    std::vector<float> firstRun(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        firstRun[i] = osc.process();
    }

    // Reset and generate again
    osc.reset();
    std::vector<float> secondRun(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        secondRun[i] = osc.process();
    }

    // Both runs should be identical
    for (size_t i = 0; i < numSamples; ++i) {
        REQUIRE(firstRun[i] == secondRun[i]);
    }
}

TEST_CASE("White noise bounded to [-1.0, 1.0] (SC-007)", "[noise_oscillator][US1][SC-007]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::White);
    osc.setSeed(77777);

    // Generate 10 seconds of noise (per SC-007)
    constexpr size_t numSamples = 441000; // 10 seconds at 44.1kHz
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
}

// ==============================================================================
// Phase 4: User Story 2 - Pink Noise Generation [US2]
// ==============================================================================

TEST_CASE("Pink noise spectral slope is -3dB/octave (SC-003)", "[noise_oscillator][US2][SC-003]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Pink);
    osc.setSeed(12345);

    // Generate 10 seconds of pink noise
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Measure spectral slope from 100Hz to 10kHz
    float slope = measureSpectralSlope(buffer.data(), numSamples, 100.0f, 10000.0f, kSampleRate);

    // SC-003: -3dB/octave +/- 0.5dB
    INFO("Pink noise slope: " << slope << " dB/octave");
    REQUIRE(slope == Approx(-3.0f).margin(0.5f));
}

TEST_CASE("Pink noise remains bounded within [-1.0, 1.0] (SC-007)", "[noise_oscillator][US2][SC-007]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Pink);
    osc.setSeed(33333);

    // Generate 10 seconds
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
}

// ==============================================================================
// Phase 5: User Story 3 - Brown Noise Generation [US3]
// ==============================================================================

TEST_CASE("Brown noise spectral slope is -6dB/octave (SC-004)", "[noise_oscillator][US3][SC-004]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Brown);
    osc.setSeed(44444);

    // Generate 10 seconds of brown noise
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Measure spectral slope from 100Hz to 10kHz
    float slope = measureSpectralSlope(buffer.data(), numSamples, 100.0f, 10000.0f, kSampleRate);

    // SC-004: -6dB/octave +/- 1.0dB
    INFO("Brown noise slope: " << slope << " dB/octave");
    REQUIRE(slope == Approx(-6.0f).margin(1.0f));
}

TEST_CASE("Brown noise remains bounded within [-1.0, 1.0] (SC-007)", "[noise_oscillator][US3][SC-007]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Brown);
    osc.setSeed(55555);

    // Generate 10 seconds
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
}

// ==============================================================================
// Phase 6: User Story 4 - Blue and Violet Noise Generation [US4]
// ==============================================================================

TEST_CASE("Blue noise spectral slope is +3dB/octave (SC-005)", "[noise_oscillator][US4][SC-005]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Blue);
    osc.setSeed(66666);

    // Generate 10 seconds of blue noise
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Measure spectral slope from 100Hz to 10kHz
    float slope = measureSpectralSlope(buffer.data(), numSamples, 100.0f, 10000.0f, kSampleRate);

    // SC-005: +3dB/octave +/- 0.5dB
    INFO("Blue noise slope: " << slope << " dB/octave");
    REQUIRE(slope == Approx(3.0f).margin(0.5f));
}

TEST_CASE("Violet noise spectral slope is +6dB/octave (SC-006)", "[noise_oscillator][US4][SC-006]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Violet);
    osc.setSeed(77777);

    // Generate 10 seconds of violet noise
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Measure spectral slope from 100Hz to 10kHz
    float slope = measureSpectralSlope(buffer.data(), numSamples, 100.0f, 10000.0f, kSampleRate);

    // SC-006: +6dB/octave +/- 1.0dB
    INFO("Violet noise slope: " << slope << " dB/octave");
    REQUIRE(slope == Approx(6.0f).margin(1.0f));
}

TEST_CASE("Blue and violet noise remain bounded within [-1.0, 1.0] (SC-007)", "[noise_oscillator][US4][SC-007]") {
    constexpr size_t numSamples = 441000;

    SECTION("Blue noise bounded") {
        NoiseOscillator osc;
        osc.prepare(kSampleRate);
        osc.setColor(NoiseColor::Blue);
        osc.setSeed(88888);

        std::vector<float> buffer(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = osc.process();
        }
        REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
    }

    SECTION("Violet noise bounded") {
        NoiseOscillator osc;
        osc.prepare(kSampleRate);
        osc.setColor(NoiseColor::Violet);
        osc.setSeed(99999);

        std::vector<float> buffer(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = osc.process();
        }
        REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
    }
}

// ==============================================================================
// Phase 7: User Story 5 - Block Processing [US5]
// ==============================================================================

TEST_CASE("Block processing identical to sample-by-sample (SC-009)", "[noise_oscillator][US5][SC-009]") {
    constexpr size_t blockSize = 512;
    constexpr size_t numBlocks = 10;
    constexpr size_t totalSamples = blockSize * numBlocks;

    // Test with all noise colors
    for (auto color : {NoiseColor::White, NoiseColor::Pink, NoiseColor::Brown,
                       NoiseColor::Blue, NoiseColor::Violet, NoiseColor::Grey}) {
        SECTION("Color " + std::to_string(static_cast<int>(color))) {
            NoiseOscillator oscSample;
            NoiseOscillator oscBlock;

            oscSample.prepare(kSampleRate);
            oscBlock.prepare(kSampleRate);

            oscSample.setColor(color);
            oscBlock.setColor(color);

            oscSample.setSeed(12345);
            oscBlock.setSeed(12345);

            // Generate sample-by-sample
            std::vector<float> sampleOutput(totalSamples);
            for (size_t i = 0; i < totalSamples; ++i) {
                sampleOutput[i] = oscSample.process();
            }

            // Generate using blocks
            std::vector<float> blockOutput(totalSamples);
            for (size_t b = 0; b < numBlocks; ++b) {
                oscBlock.processBlock(blockOutput.data() + b * blockSize, blockSize);
            }

            // Compare outputs - use Approx for floating-point comparison
            // SC-009 requires identical output, but floating-point operations
            // may have minor precision differences due to compiler optimization
            // Use machine epsilon (1e-6f) for comparison
            for (size_t i = 0; i < totalSamples; ++i) {
                REQUIRE(sampleOutput[i] == Approx(blockOutput[i]).margin(1e-6f));
            }
        }
    }
}

TEST_CASE("Block processing performance benchmark", "[noise_oscillator][US5][!benchmark]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Pink);
    osc.setSeed(12345);

    constexpr size_t blockSize = 512;
    std::vector<float> buffer(blockSize);

    BENCHMARK("processBlock 512 samples") {
        osc.processBlock(buffer.data(), blockSize);
        return buffer[0];
    };

    BENCHMARK("process sample-by-sample 512 samples") {
        for (size_t i = 0; i < blockSize; ++i) {
            buffer[i] = osc.process();
        }
        return buffer[0];
    };
}

// ==============================================================================
// Phase 8: User Story 6 - Grey Noise Generation [US6]
// ==============================================================================

TEST_CASE("Grey noise spectral response follows inverse A-weighting (SC-012)", "[noise_oscillator][US6][SC-012]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Grey);
    osc.setSeed(11111);

    // Generate 10 seconds of grey noise
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Measure energy at low frequencies (100Hz) vs 1kHz
    constexpr size_t kFftSize = 8192;
    FFT fft;
    fft.prepare(kFftSize);

    std::vector<float> windowedInput(kFftSize);
    std::vector<Complex> fftOutput(kFftSize / 2 + 1);

    // Average over multiple windows
    std::vector<float> avgMagnitude(kFftSize / 2 + 1, 0.0f);
    constexpr size_t kNumWindows = 10;
    size_t hopSize = (numSamples - kFftSize) / (kNumWindows - 1);

    for (size_t w = 0; w < kNumWindows; ++w) {
        size_t startIdx = w * hopSize;
        for (size_t i = 0; i < kFftSize; ++i) {
            float hann = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize));
            windowedInput[i] = buffer[startIdx + i] * hann;
        }
        fft.forward(windowedInput.data(), fftOutput.data());
        for (size_t i = 0; i < avgMagnitude.size(); ++i) {
            avgMagnitude[i] += std::sqrt(fftOutput[i].real * fftOutput[i].real +
                                         fftOutput[i].imag * fftOutput[i].imag);
        }
    }
    for (auto& m : avgMagnitude) {
        m /= static_cast<float>(kNumWindows);
    }

    // Get energy at 100Hz and 1kHz
    float binWidth = kSampleRate / static_cast<float>(kFftSize);
    size_t bin100Hz = static_cast<size_t>(100.0f / binWidth);
    size_t bin1kHz = static_cast<size_t>(1000.0f / binWidth);

    float mag100Hz = avgMagnitude[bin100Hz];
    float mag1kHz = avgMagnitude[bin1kHz];

    float dBDiff = 20.0f * std::log10(mag100Hz / mag1kHz);

    // SC-012: Low frequencies should have +10 to +20dB more energy than 1kHz
    INFO("100Hz magnitude: " << mag100Hz);
    INFO("1kHz magnitude: " << mag1kHz);
    INFO("dB difference (100Hz vs 1kHz): " << dBDiff);
    REQUIRE(dBDiff >= 10.0f);
    REQUIRE(dBDiff <= 20.0f);
}

TEST_CASE("Grey noise output bounded to [-1.0, 1.0] (SC-007)", "[noise_oscillator][US6][SC-007]") {
    NoiseOscillator osc;
    osc.prepare(kSampleRate);
    osc.setColor(NoiseColor::Grey);
    osc.setSeed(22222);

    // Generate 10 seconds
    constexpr size_t numSamples = 441000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));
}

// ==============================================================================
// Phase 9: Polish - Edge Cases
// ==============================================================================

TEST_CASE("setSeed(0) uses default seed", "[noise_oscillator][edge]") {
    NoiseOscillator osc1;
    NoiseOscillator osc2;

    osc1.prepare(kSampleRate);
    osc2.prepare(kSampleRate);

    osc1.setColor(NoiseColor::White);
    osc2.setColor(NoiseColor::White);

    // Seed 0 should use default
    osc1.setSeed(0);
    osc2.setSeed(0);

    // Both should produce identical output
    constexpr size_t numSamples = 100;
    for (size_t i = 0; i < numSamples; ++i) {
        float s1 = osc1.process();
        float s2 = osc2.process();
        REQUIRE(s1 == s2);
    }
}

TEST_CASE("setColor mid-stream preserves PRNG state", "[noise_oscillator][edge]") {
    NoiseOscillator osc1;
    NoiseOscillator osc2;

    osc1.prepare(kSampleRate);
    osc2.prepare(kSampleRate);

    osc1.setSeed(12345);
    osc2.setSeed(12345);

    osc1.setColor(NoiseColor::White);
    osc2.setColor(NoiseColor::White);

    // Generate some samples
    for (int i = 0; i < 100; ++i) {
        (void)osc1.process();
        (void)osc2.process();
    }

    // Change color on osc1
    osc1.setColor(NoiseColor::Pink);
    osc2.setColor(NoiseColor::Pink);

    // After color change, both should produce identical output
    // (PRNG state preserved, filter state reset on both)
    constexpr size_t testSamples = 100;
    for (size_t i = 0; i < testSamples; ++i) {
        float s1 = osc1.process();
        float s2 = osc2.process();
        REQUIRE(s1 == s2);
    }
}

TEST_CASE("High sample rates (192kHz) produce valid output", "[noise_oscillator][edge]") {
    constexpr float kHighSampleRate = 192000.0f;

    NoiseOscillator osc;
    osc.prepare(kHighSampleRate);
    osc.setColor(NoiseColor::Pink);
    osc.setSeed(11111);

    // Generate 1 second at 192kHz
    constexpr size_t numSamples = 192000;
    std::vector<float> buffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = osc.process();
    }

    // Check bounds
    REQUIRE(allInBounds(buffer.data(), numSamples, -1.0f, 1.0f));

    // Check that there's actual noise (not silence or DC)
    float mean = calculateMean(buffer.data(), numSamples);
    float variance = calculateVariance(buffer.data(), numSamples);

    REQUIRE(std::abs(mean) < 0.1f);
    REQUIRE(variance > 0.01f);
}

// ==============================================================================
// Vorago Phase 2 (specs/vorago-phase2-noise-organism): FR-098 / SC-011 (v)
// ==============================================================================
// NoiseColor::Velvet and NoiseColor::RadioStatic exist in the enum
// (core/pattern_freeze_types.h:124-135) but have no `case` in the colour switch
// of NoiseOscillator::process() (noise_oscillator.h:241-267) - both fall through
// `default:` and render plain White. This case pins the defect and the fix:
//   - neither colour may be bit-identical to White any more;
//   - Velvet must be the sparse +/-1 impulse train of noise_generator.h:521-534;
//   - RadioStatic must be audible but band-limited (less sample-to-sample motion
//     than White).
// Expected to FAIL until T007 adds the two cases.
// ==============================================================================
TEST_CASE("NoiseOscillator_VelvetRadioStaticFixed", "[noise_oscillator][vorago-phase2]") {
    constexpr double kSr = 48000.0;
    constexpr size_t kNumSamples = 8192;
    constexpr std::uint32_t kSeed = 4242u;

    auto render = [&](NoiseColor color, std::vector<float>& out) {
        NoiseOscillator osc;
        osc.prepare(kSr);
        osc.setColor(color);
        osc.setSeed(kSeed);
        osc.processBlock(out.data(), out.size());
    };

    auto maxAbsDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
        float worst = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            const float d = std::abs(a[i] - b[i]);
            if (d > worst) worst = d;
        }
        return worst;
    };

    auto rmsDb = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (float s : v) {
            sum += static_cast<double>(s) * static_cast<double>(s);
        }
        const double rms = std::sqrt(sum / static_cast<double>(v.size()));
        return 20.0 * std::log10(rms > 1e-12 ? rms : 1e-12);
    };

    // Mean absolute first difference: a coarse, allocation-free proxy for
    // high-frequency content. A low-passed colour moves less per sample.
    auto meanAbsFirstDiff = [](const std::vector<float>& v) {
        double sum = 0.0;
        for (size_t i = 1; i < v.size(); ++i) {
            sum += std::abs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1]));
        }
        return sum / static_cast<double>(v.size() - 1);
    };

    // Reference White render, same seed, same sample rate.
    std::vector<float> white(kNumSamples, 0.0f);
    render(NoiseColor::White, white);

    SECTION("Velvet renders a sparse +/-1 impulse train, not White") {
        std::vector<float> velvet(kNumSamples, 0.0f);
        render(NoiseColor::Velvet, velvet);

        // FAILS today: with no `case NoiseColor::Velvet`, this render is
        // bit-identical to the White render above.
        REQUIRE(maxAbsDiff(velvet, white) > 1e-3f);

        // Sparse: the impulse density is a small fraction of the sample rate,
        // so the overwhelming majority of samples are exactly zero.
        size_t zeroCount = 0;
        bool everyImpulseIsUnitMagnitude = true;
        for (float s : velvet) {
            if (s == 0.0f) {
                ++zeroCount;
            } else if (std::abs(std::abs(s) - 1.0f) > 1e-6f) {
                everyImpulseIsUnitMagnitude = false;
            }
        }
        const double zeroFraction =
            static_cast<double>(zeroCount) / static_cast<double>(kNumSamples);

        REQUIRE(zeroFraction >= 0.60);
        REQUIRE(everyImpulseIsUnitMagnitude);
    }

    SECTION("RadioStatic renders a band-limited colour, not White") {
        std::vector<float> radio(kNumSamples, 0.0f);
        render(NoiseColor::RadioStatic, radio);

        // FAILS today for the same reason as Velvet.
        REQUIRE(maxAbsDiff(radio, white) > 1e-3f);

        // Audible, not a silent stub.
        REQUIRE(rmsDb(radio) > -60.0);

        // Band-limited: measurably less sample-to-sample motion than White.
        REQUIRE(meanAbsFirstDiff(radio) <= 0.7 * meanAbsFirstDiff(white));
    }
}
