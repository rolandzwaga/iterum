// ==============================================================================
// Approval Tests (Golden Master / Regression Testing)
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// These tests compare DSP algorithm outputs against approved references.
// See specs/TESTING-GUIDE.md for usage guidance.
//
// How Approval Tests Work:
// 1. First run: Output is saved to a .received.txt file
// 2. Review the output manually and approve it (rename to .approved.txt)
// 3. Subsequent runs compare against the approved file
// 4. Any difference fails the test
//
// When to Update Approved Files:
// - When intentionally changing algorithm behavior
// - When fixing a bug in the reference
// - When improving quality (document the change)
//
// Never update because:
// - Tests are "red" after unrelated changes
// - You don't understand why output changed
// ==============================================================================

// Catch2 v3 + ApprovalTests integration
// Must define APPROVALS_CATCH2_V3 before including ApprovalTests
#define APPROVALS_CATCH2_V3
#include <ApprovalTests.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

// Test helpers
#include "test_helpers/test_signals.h"
#include "test_helpers/buffer_comparison.h"

// DSP code to test
#include "dsp/dsp_utils.h"

#include <array>
#include <sstream>
#include <iomanip>

using namespace TestHelpers;
using namespace VSTWork::DSP;
using Catch::Approx;

// ==============================================================================
// Main Entry Point
// ==============================================================================

int main(int argc, char* argv[]) {
    // Configure ApprovalTests to use subdirectory for approved files
    auto directoryDisposer = ApprovalTests::Approvals::useApprovalsSubdirectory("approved");

    return Catch::Session().run(argc, argv);
}

// ==============================================================================
// Example: Gain Function Approval Test
// ==============================================================================

TEST_CASE("applyGain output matches approved", "[regression][dsp][gain]") {
    std::array<float, 64> buffer;
    generateSine(buffer, 440.0f, 44100.0f);

    // Apply gain
    applyGain(buffer.data(), buffer.size(), 0.5f);

    // Convert to string for approval (sample every 8th value to keep it manageable)
    // Use 5 decimal places for cross-platform consistency
    // (6+ decimal places differ between MSVC/Clang/GCC at the ULP level)
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(5);
    oss << "# applyGain(sine_440Hz, gain=0.5)\n";
    oss << "# Sample Rate: 44100 Hz\n";
    oss << "# Buffer Size: 64 samples\n";
    oss << "#\n";
    oss << "# Index, Value\n";

    for (size_t i = 0; i < buffer.size(); i += 8) {
        oss << i << ", " << buffer[i] << "\n";
    }

    ApprovalTests::Approvals::verify(oss.str());
}

// ==============================================================================
// Example: Soft Clip Approval Test
// ==============================================================================

TEST_CASE("softClip transfer function matches approved", "[regression][dsp][clip]") {
    // Test the transfer function: input -> output mapping
    // Use 5 decimal places for cross-platform consistency
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(5);
    oss << "# softClip transfer function\n";
    oss << "# Input range: -3.0 to +3.0\n";
    oss << "#\n";
    oss << "# Input, Output\n";

    for (float input = -3.0f; input <= 3.0f; input += 0.25f) {
        float output = softClip(input);
        oss << input << ", " << output << "\n";
    }

    ApprovalTests::Approvals::verify(oss.str());
}

// ==============================================================================
// Example: Smoother Convergence Approval Test
// ==============================================================================

TEST_CASE("OnePoleSmoother convergence matches approved", "[regression][dsp][smoother]") {
    OnePoleSmoother smoother;
    smoother.setTime(0.01f, 44100.0f);  // 10ms smoothing time
    smoother.reset(0.0f);

    // Use 5 decimal places for cross-platform consistency
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(5);
    oss << "# OnePoleSmoother convergence\n";
    oss << "# Smooth Time: 10ms\n";
    oss << "# Sample Rate: 44100 Hz\n";
    oss << "# Target: 1.0 (step from 0.0)\n";
    oss << "#\n";
    oss << "# Sample, Value\n";

    // Track convergence over 1000 samples (~23ms)
    for (int i = 0; i < 1000; i += 50) {
        // Process samples to reach this point
        float value = 0.0f;
        smoother.reset(0.0f);
        for (int j = 0; j <= i; ++j) {
            value = smoother.process(1.0f);
        }
        oss << i << ", " << value << "\n";
    }

    ApprovalTests::Approvals::verify(oss.str());
}

// ==============================================================================
// Template for New Approval Tests
// ==============================================================================
// Copy and modify this template when adding new DSP algorithms:
//
// TEST_CASE("MyAlgorithm output matches approved", "[regression][dsp][myalgo]") {
//     // 1. Set up the algorithm
//     MyAlgorithm algo;
//     algo.prepare(44100.0, 512);
//     algo.setParameter(0.5f);
//
//     // 2. Create input signal
//     std::array<float, 512> buffer;
//     generateSine(buffer, 440.0f, 44100.0f);
//
//     // 3. Process
//     algo.process(buffer.data(), buffer.size());
//
//     // 4. Convert to string (include metadata header)
//     std::ostringstream oss;
//     oss << std::fixed << std::setprecision(8);
//     oss << "# MyAlgorithm output\n";
//     oss << "# Parameters: param=0.5\n";
//     oss << "# Sample Rate: 44100 Hz\n";
//     oss << "#\n";
//     oss << "# Index, Value\n";
//
//     for (size_t i = 0; i < buffer.size(); i += 16) {
//         oss << i << ", " << buffer[i] << "\n";
//     }
//
//     // 5. Verify
//     ApprovalTests::Approvals::verify(oss.str());
// }
