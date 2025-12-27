// ==============================================================================
// Benchmark: SpectralDelay Performance
// ==============================================================================
// Verifies SC-005: SpectralDelay uses <3% CPU at 44.1kHz stereo with 2048 FFT
//
// Methodology:
//   - Process realistic audio buffers (512 samples, stereo)
//   - Measure processing time over multiple iterations
//   - Compare against available time budget (buffer duration)
//   - CPU% = (processing time / buffer duration) x 100
//
// Target: <3% CPU at 44.1kHz, 512 samples/block, 2048 FFT (Layer 4 budget)
// ==============================================================================
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dsp/features/spectral_delay.h"
#include "dsp/core/block_context.h"

using namespace Iterum::DSP;

// Benchmark a specific FFT size configuration
double benchmarkFFTSize(std::size_t fftSize, int numIterations) {
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr size_t BLOCK_SIZE = 512;

    // Calculate time budget for one block
    const double blockDurationMs = (static_cast<double>(BLOCK_SIZE) / SAMPLE_RATE) * 1000.0;

    // Create and prepare SpectralDelay
    SpectralDelay delay;
    delay.setFFTSize(fftSize);
    delay.prepare(SAMPLE_RATE, BLOCK_SIZE);

    // Configure for realistic usage (all features enabled)
    delay.setBaseDelayMs(500.0f);
    delay.setSpreadMs(300.0f);
    delay.setSpreadDirection(SpreadDirection::LowToHigh);
    delay.setFeedback(0.5f);
    delay.setFeedbackTilt(0.2f);
    delay.setDiffusion(0.3f);
    delay.setDryWetMix(50.0f);
    delay.setOutputGainDb(0.0f);
    delay.snapParameters();

    // Allocate buffers
    std::vector<float> leftBuffer(BLOCK_SIZE);
    std::vector<float> rightBuffer(BLOCK_SIZE);

    // Generate noise input
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        leftBuffer[i] = dist(rng);
        rightBuffer[i] = dist(rng);
    }

    // Create block context
    BlockContext ctx{
        .sampleRate = SAMPLE_RATE,
        .blockSize = BLOCK_SIZE,
        .tempoBPM = 120.0,
        .timeSignatureNumerator = 4,
        .timeSignatureDenominator = 4,
        .isPlaying = true,
        .transportPositionSamples = 0
    };

    // Warm up (need many iterations to prime STFT buffers)
    for (int i = 0; i < 50; ++i) {
        delay.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }

    // Benchmark stereo processing
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < numIterations; ++iter) {
        // Reset buffers with fresh noise each iteration
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            leftBuffer[i] = dist(rng);
            rightBuffer[i] = dist(rng);
        }
        delay.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double avgTimePerBlockMs = totalTimeMs / numIterations;

    // Calculate CPU percentage
    double cpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    return cpuPercent;
}

int main() {
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr size_t BLOCK_SIZE = 512;
    constexpr int NUM_ITERATIONS = 1000;

    // Calculate time budget for one block
    const double blockDurationMs = (static_cast<double>(BLOCK_SIZE) / SAMPLE_RATE) * 1000.0;

    std::cout << "SpectralDelay Benchmark (SC-005): " << NUM_ITERATIONS << " iterations\n";
    std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "Block Size: " << BLOCK_SIZE << " samples\n";
    std::cout << "Block Duration: " << std::fixed << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "Target: <3% CPU usage at 2048 FFT\n";
    std::cout << "=================================================================\n\n";

    // Test all FFT sizes
    std::cout << "FFT Size Comparison:\n";
    std::cout << "-----------------------------------------------------------------\n";

    double cpu512 = benchmarkFFTSize(512, NUM_ITERATIONS);
    std::cout << "  FFT 512:  " << std::fixed << std::setprecision(2) << cpu512 << "% CPU\n";

    double cpu1024 = benchmarkFFTSize(1024, NUM_ITERATIONS);
    std::cout << "  FFT 1024: " << cpu1024 << "% CPU\n";

    double cpu2048 = benchmarkFFTSize(2048, NUM_ITERATIONS);
    std::cout << "  FFT 2048: " << cpu2048 << "% CPU ";
    std::cout << (cpu2048 < 3.0 ? "[PASS]" : "[FAIL]") << " (target: <3%)\n";

    double cpu4096 = benchmarkFFTSize(4096, NUM_ITERATIONS);
    std::cout << "  FFT 4096: " << cpu4096 << "% CPU\n";

    std::cout << "-----------------------------------------------------------------\n\n";

    // Detailed test with freeze enabled
    std::cout << "Freeze Mode Test (FFT 2048):\n";
    std::cout << "-----------------------------------------------------------------\n";

    {
        SpectralDelay delay;
        delay.setFFTSize(2048);
        delay.prepare(SAMPLE_RATE, BLOCK_SIZE);

        delay.setBaseDelayMs(500.0f);
        delay.setSpreadMs(300.0f);
        delay.setFeedback(0.5f);
        delay.setDiffusion(0.3f);
        delay.setDryWetMix(50.0f);
        delay.setFreezeEnabled(true);  // Enable freeze
        delay.snapParameters();

        std::vector<float> leftBuffer(BLOCK_SIZE);
        std::vector<float> rightBuffer(BLOCK_SIZE);

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

        BlockContext ctx{
            .sampleRate = SAMPLE_RATE,
            .blockSize = BLOCK_SIZE,
            .tempoBPM = 120.0,
            .timeSignatureNumerator = 4,
            .timeSignatureDenominator = 4,
            .isPlaying = true,
            .transportPositionSamples = 0
        };

        // Warm up
        for (int i = 0; i < 50; ++i) {
            for (size_t j = 0; j < BLOCK_SIZE; ++j) {
                leftBuffer[j] = dist(rng);
                rightBuffer[j] = dist(rng);
            }
            delay.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (size_t i = 0; i < BLOCK_SIZE; ++i) {
                leftBuffer[i] = dist(rng);
                rightBuffer[i] = dist(rng);
            }
            delay.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
        }
        auto end = std::chrono::high_resolution_clock::now();

        double totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;
        double freezeCpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

        std::cout << "  With Freeze: " << std::fixed << std::setprecision(2) << freezeCpuPercent << "% CPU\n";
    }

    std::cout << "-----------------------------------------------------------------\n\n";

    // Summary
    std::cout << "Summary (SC-005 target: <3% CPU at 2048 FFT):\n";
    std::cout << "=================================================================\n";
    std::cout << "  FFT 512:  " << std::setprecision(2) << cpu512 << "%\n";
    std::cout << "  FFT 1024: " << cpu1024 << "%\n";
    std::cout << "  FFT 2048: " << cpu2048 << "% " << (cpu2048 < 3.0 ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "  FFT 4096: " << cpu4096 << "%\n";
    std::cout << "=================================================================\n";

    // Return non-zero if 2048 FFT (target configuration) fails
    return (cpu2048 < 3.0) ? 0 : 1;
}
