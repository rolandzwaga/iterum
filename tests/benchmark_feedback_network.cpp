// ==============================================================================
// Benchmark: FeedbackNetwork Performance
// ==============================================================================
// Verifies SC-007: FeedbackNetwork uses <1% CPU at 44.1kHz stereo
//
// Methodology:
//   - Process realistic audio buffers (512 samples, stereo)
//   - Measure processing time over multiple iterations
//   - Compare against available time budget (buffer duration)
//   - CPU% = (processing time / buffer duration) × 100
//
// Target: <1% CPU at 44.1kHz, 512 samples/block
// ==============================================================================
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dsp/systems/feedback_network.h"
#include "dsp/core/block_context.h"

using namespace Iterum::DSP;

int main() {
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr size_t BLOCK_SIZE = 512;
    constexpr float MAX_DELAY_MS = 2000.0f;  // 2 second max delay
    constexpr int NUM_ITERATIONS = 1000;

    // Calculate time budget for one block
    const double blockDurationMs = (static_cast<double>(BLOCK_SIZE) / SAMPLE_RATE) * 1000.0;

    std::cout << "FeedbackNetwork Benchmark: " << NUM_ITERATIONS << " iterations\n";
    std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "Block Size: " << BLOCK_SIZE << " samples\n";
    std::cout << "Block Duration: " << std::fixed << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "=================================================================\n";

    // Create and prepare FeedbackNetwork
    FeedbackNetwork network;
    network.prepare(SAMPLE_RATE, BLOCK_SIZE, MAX_DELAY_MS);

    // Configure for realistic usage (filter + saturation enabled)
    network.setFeedbackAmount(0.75f);
    network.setDelayTimeMs(500.0f);
    network.setFilterEnabled(true);
    network.setFilterType(FilterType::Lowpass);
    network.setFilterCutoff(4000.0f);
    network.setSaturationEnabled(true);
    network.setSaturationDrive(6.0f);
    network.setCrossFeedbackAmount(0.3f);  // Some ping-pong

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
    BlockContext ctx;
    ctx.sampleRate = SAMPLE_RATE;
    ctx.blockSize = BLOCK_SIZE;
    ctx.tempoBPM = 120.0;

    // Warm up
    for (int i = 0; i < 10; ++i) {
        network.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }

    // Benchmark stereo processing
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Reset buffers with fresh noise each iteration
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            leftBuffer[i] = dist(rng);
            rightBuffer[i] = dist(rng);
        }
        network.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;

    // Calculate CPU percentage
    double cpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    std::cout << "Stereo Processing Results:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << totalTimeMs << " ms\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << avgTimePerBlockMs << " ms\n";
    std::cout << "  Time budget: " << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << cpuPercent << "%\n";
    std::cout << "=================================================================\n";

    // Also benchmark mono processing
    network.reset();
    std::vector<float> monoBuffer(BLOCK_SIZE);

    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            monoBuffer[i] = dist(rng);
        }
        network.process(monoBuffer.data(), BLOCK_SIZE, ctx);
    }
    end = std::chrono::high_resolution_clock::now();

    totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;
    double monoCpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    std::cout << "Mono Processing Results:\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << avgTimePerBlockMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << monoCpuPercent << "%\n";
    std::cout << "=================================================================\n";

    // Verdict
    bool stereoPass = cpuPercent < 1.0;
    bool monoPass = monoCpuPercent < 1.0;

    std::cout << "SC-007 (<1% CPU stereo): " << (stereoPass ? "PASS" : "FAIL") << "\n";
    std::cout << "SC-007 (<1% CPU mono):   " << (monoPass ? "PASS" : "FAIL") << "\n";

    return (stereoPass && monoPass) ? 0 : 1;
}
