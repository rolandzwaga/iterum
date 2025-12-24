// ==============================================================================
// Benchmark: fastTanh vs std::tanh
// ==============================================================================
// Verifies SC-001: fastTanh is 2x faster than std::tanh
//
// Typical results (Windows/MSVC, Release, 1M samples × 10 iterations):
//   fastTanh: ~35,000 μs
//   std::tanh: ~105,000 μs
//   Speedup: ~3x (exceeds 2x target)
//
// Note: Uses volatile function pointers to prevent compiler over-optimization.
// ==============================================================================
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;

int main() {
    constexpr size_t NUM_SAMPLES = 1000000;
    constexpr int NUM_ITERATIONS = 10;

    // Generate random input values in range [-4, 4]
    std::vector<float> input(NUM_SAMPLES);
    std::vector<float> output(NUM_SAMPLES);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-4.0f, 4.0f);
    for (auto& x : input) {
        x = dist(rng);
    }

    std::cout << "fastTanh Benchmark: " << NUM_SAMPLES << " samples x " << NUM_ITERATIONS << " iterations\n";
    std::cout << "=================================================================\n";

    // Warm up
    for (size_t i = 0; i < NUM_SAMPLES; ++i) {
        output[i] = fastTanh(input[i]);
    }
    for (size_t i = 0; i < NUM_SAMPLES; ++i) {
        output[i] = std::tanh(input[i]);
    }

    // Use volatile function pointers to prevent inlining/optimization
    volatile auto fastFunc = &fastTanh;
    volatile auto stdFunc = static_cast<float(*)(float)>(&std::tanh);

    // Benchmark fastTanh
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < NUM_SAMPLES; ++i) {
            output[i] = (*fastFunc)(input[i]);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto fastTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Prevent optimization
    volatile float sink = output[0];

    // Benchmark std::tanh
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < NUM_SAMPLES; ++i) {
            output[i] = (*stdFunc)(input[i]);
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto stdTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Prevent optimization
    sink = output[0];
    (void)sink;

    double speedup = static_cast<double>(stdTime) / static_cast<double>(fastTime);

    std::cout << "fastTanh: " << std::setw(6) << fastTime << " us\n";
    std::cout << "std::tanh: " << std::setw(5) << stdTime << " us\n";
    std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
    std::cout << "=================================================================\n";
    std::cout << "SC-001 (2x faster): " << (speedup >= 2.0 ? "PASS" : "FAIL") << "\n";

    return speedup >= 2.0 ? 0 : 1;
}
