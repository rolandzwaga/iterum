// ==============================================================================
// Unit Tests: MidSideProcessor
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 014-midside-processor
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/midside_processor.h"

#include <array>
#include <cmath>
#include <vector>
#include <random>
#include <numeric>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestBlockSize = 512;
constexpr float kTolerance = 1e-6f;

// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    constexpr float kTwoPi = 6.283185307179586f;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
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

// Calculate maximum absolute difference between two buffers
inline float maxDifference(const float* a, const float* b, size_t size) {
    float maxDiff = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diff = std::abs(a[i] - b[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    return maxDiff;
}

// Check if two buffers are equal within tolerance
inline bool buffersEqual(const float* a, const float* b, size_t size, float tolerance = kTolerance) {
    return maxDifference(a, b, size) <= tolerance;
}

} // anonymous namespace

// ==============================================================================
// User Story 1: Basic Mid/Side Encoding and Decoding (P1 - MVP)
// ==============================================================================

// T006: encode L=1.0,R=1.0 → Mid=1.0,Side=0.0
TEST_CASE("MidSideProcessor encodes identical L/R to pure Mid", "[midside][US1][encode]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();  // Snap smoothers to defaults (width=100%, gains=0dB)

    // Input: L=1.0, R=1.0 (identical channels = pure mono/mid content)
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // At width=100%, output should equal input (unity behavior)
    // Mid = (L + R) / 2 = 1.0, Side = (L - R) / 2 = 0.0
    // L = Mid + Side = 1.0, R = Mid - Side = 1.0
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(1.0f).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(1.0f).margin(kTolerance));
    }
}

// T007: encode L=1.0,R=-1.0 → Mid=0.0,Side=1.0
TEST_CASE("MidSideProcessor encodes opposite L/R to pure Side", "[midside][US1][encode]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    // Input: L=1.0, R=-1.0 (opposite channels = pure side content)
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // At width=100%, output should equal input (unity behavior)
    // Mid = (L + R) / 2 = 0.0, Side = (L - R) / 2 = 1.0
    // L = Mid + Side = 1.0, R = Mid - Side = -1.0
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(1.0f).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(-1.0f).margin(kTolerance));
    }
}

// T008: roundtrip L=0.5,R=0.3 → encode → decode → L=0.5,R=0.3
TEST_CASE("MidSideProcessor roundtrip preserves input at unity width", "[midside][US1][roundtrip]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    // Input: arbitrary stereo signal
    std::array<float, 4> left  = {0.5f, -0.3f, 0.8f, -0.1f};
    std::array<float, 4> right = {0.3f, -0.5f, 0.2f, -0.9f};
    std::array<float, 4> leftOut{}, rightOut{};

    // Store original values for comparison
    std::array<float, 4> origLeft = left;
    std::array<float, 4> origRight = right;

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // At width=100%, output should equal input (perfect reconstruction)
    // FR-003: decode(encode(L,R)) = (L, R) within floating-point tolerance
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(origLeft[i]).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(origRight[i]).margin(kTolerance));
    }
}

// T009: process() method signature and basic operation
TEST_CASE("MidSideProcessor process() handles various block sizes", "[midside][US1][process]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    SECTION("single sample") {
        float left = 0.7f, right = 0.3f;
        float leftOut = 0.0f, rightOut = 0.0f;

        ms.process(&left, &right, &leftOut, &rightOut, 1);

        REQUIRE(leftOut == Approx(0.7f).margin(kTolerance));
        REQUIRE(rightOut == Approx(0.3f).margin(kTolerance));
    }

    SECTION("standard block size") {
        std::vector<float> left(512, 0.5f);
        std::vector<float> right(512, 0.5f);
        std::vector<float> leftOut(512), rightOut(512);

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 512);

        for (size_t i = 0; i < 512; ++i) {
            REQUIRE(leftOut[i] == Approx(0.5f).margin(kTolerance));
            REQUIRE(rightOut[i] == Approx(0.5f).margin(kTolerance));
        }
    }

    SECTION("in-place processing") {
        std::array<float, 4> left  = {0.5f, 0.3f, -0.2f, 0.8f};
        std::array<float, 4> right = {0.3f, 0.5f, -0.4f, 0.6f};
        std::array<float, 4> origLeft = left;
        std::array<float, 4> origRight = right;

        // Process in-place (output buffers same as input buffers)
        ms.process(left.data(), right.data(), left.data(), right.data(), 4);

        // At unity width, should equal original
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(left[i] == Approx(origLeft[i]).margin(kTolerance));
            REQUIRE(right[i] == Approx(origRight[i]).margin(kTolerance));
        }
    }
}

// T009a: prepare() method signature and smoother initialization
TEST_CASE("MidSideProcessor prepare() initializes correctly", "[midside][US1][lifecycle]") {
    MidSideProcessor ms;

    SECTION("default values before prepare") {
        REQUIRE(ms.getWidth() == Approx(100.0f));
        REQUIRE(ms.getMidGain() == Approx(0.0f));
        REQUIRE(ms.getSideGain() == Approx(0.0f));
        REQUIRE_FALSE(ms.isSoloMidEnabled());
        REQUIRE_FALSE(ms.isSoloSideEnabled());
    }

    SECTION("prepare() accepts various sample rates") {
        // Should not throw for valid sample rates
        REQUIRE_NOTHROW(ms.prepare(44100.0f, 512));
        REQUIRE_NOTHROW(ms.prepare(48000.0f, 256));
        REQUIRE_NOTHROW(ms.prepare(96000.0f, 1024));
        REQUIRE_NOTHROW(ms.prepare(192000.0f, 2048));
    }

    SECTION("can process immediately after prepare") {
        ms.prepare(44100.0f, 512);

        std::array<float, 4> left  = {1.0f, 0.0f, -1.0f, 0.5f};
        std::array<float, 4> right = {1.0f, 0.0f, -1.0f, 0.5f};
        std::array<float, 4> leftOut{}, rightOut{};

        REQUIRE_NOTHROW(ms.process(left.data(), right.data(),
                                   leftOut.data(), rightOut.data(), 4));
    }
}

// T010: reset() clears smoother state
TEST_CASE("MidSideProcessor reset() snaps smoothers to current targets", "[midside][US1][lifecycle]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    // Set non-default parameters
    ms.setWidth(50.0f);  // Narrow width
    ms.setMidGain(6.0f); // Boost mid

    // Reset should snap smoothers to current targets (no interpolation)
    ms.reset();

    // Process a small buffer - with reset, should immediately use new values
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // With width=50% (0.5 factor) and pure side input:
    // Mid = 0, Side = 1.0
    // Side after width scaling = 1.0 * 0.5 = 0.5
    // Mid after gain (+6dB = 2.0) = 0 * 2.0 = 0
    // L = Mid + Side = 0.5, R = Mid - Side = -0.5

    // First sample should already be at target (no smoothing after reset)
    REQUIRE(leftOut[0] == Approx(0.5f).margin(kTolerance));
    REQUIRE(rightOut[0] == Approx(-0.5f).margin(kTolerance));
}

// ==============================================================================
// User Story 2: Stereo Width Control (P2)
// ==============================================================================

// T018: width=0% produces mono output (L=R=Mid)
TEST_CASE("MidSideProcessor width=0% produces mono output", "[midside][US2][width]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setWidth(0.0f);  // Mono
    ms.reset();

    // Input: stereo signal with L != R
    std::array<float, 4> left  = {1.0f, 0.5f, -0.3f, 0.8f};
    std::array<float, 4> right = {-1.0f, 0.3f, -0.7f, 0.2f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-006: At width=0%, output MUST be mono (L=R=Mid)
    // SC-002: Width=0% produces |L - R| < 1e-6
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(kTolerance));

        // Also verify output is the Mid value: (L + R) / 2
        float expectedMid = (left[i] + right[i]) * 0.5f;
        REQUIRE(leftOut[i] == Approx(expectedMid).margin(kTolerance));
    }
}

// T019: width=100% produces unity output (equals input)
TEST_CASE("MidSideProcessor width=100% produces unity output", "[midside][US2][width]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setWidth(100.0f);  // Unity
    ms.reset();

    // Input: arbitrary stereo signal
    std::array<float, 4> left  = {0.7f, -0.2f, 0.5f, -0.9f};
    std::array<float, 4> right = {0.3f, 0.8f, -0.4f, 0.1f};
    std::array<float, 4> leftOut{}, rightOut{};

    // Store original for comparison
    std::array<float, 4> origLeft = left;
    std::array<float, 4> origRight = right;

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-007: At width=100%, output MUST equal input (unity/bypass behavior)
    // SC-003: Width=100% produces output within 1e-6 of input
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(origLeft[i]).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(origRight[i]).margin(kTolerance));
    }
}

// T020: width=200% doubles Side component
TEST_CASE("MidSideProcessor width=200% doubles Side component", "[midside][US2][width]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setWidth(200.0f);  // Maximum width
    ms.reset();

    // Input: pure side content (L=1, R=-1) -> Mid=0, Side=1
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-008: At width=200%, Side component MUST be doubled
    // Mid = 0, Side = 1, Side*2 = 2
    // L = Mid + Side*2 = 2.0, R = Mid - Side*2 = -2.0
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(2.0f).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(-2.0f).margin(kTolerance));
    }
}

// T021: setWidth() clamps to [0%, 200%]
TEST_CASE("MidSideProcessor setWidth() clamps to valid range", "[midside][US2][width]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("clamps negative values to 0%") {
        ms.setWidth(-50.0f);
        REQUIRE(ms.getWidth() == Approx(0.0f));
    }

    SECTION("clamps values above 200% to 200%") {
        ms.setWidth(300.0f);
        REQUIRE(ms.getWidth() == Approx(200.0f));
    }

    SECTION("accepts values within range") {
        ms.setWidth(75.0f);
        REQUIRE(ms.getWidth() == Approx(75.0f));

        ms.setWidth(150.0f);
        REQUIRE(ms.getWidth() == Approx(150.0f));
    }

    SECTION("boundary values work correctly") {
        ms.setWidth(0.0f);
        REQUIRE(ms.getWidth() == Approx(0.0f));

        ms.setWidth(200.0f);
        REQUIRE(ms.getWidth() == Approx(200.0f));
    }
}

// T022: width changes are smoothed (no clicks)
TEST_CASE("MidSideProcessor width changes are smoothed", "[midside][US2][width][smoothing]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setWidth(0.0f);  // Start at mono
    ms.reset();

    // Change to full width without reset
    ms.setWidth(200.0f);

    // Process a buffer - first samples should be transitioning
    std::array<float, 64> left, right;
    std::array<float, 64> leftOut, rightOut;

    // Pure side input
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), -1.0f);

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 64);

    // SC-004: Parameter changes produce click-free transitions
    // First sample should NOT be at target value (smoothing in progress)
    // After width=200%, pure side would give L=2.0, R=-2.0
    // But we started from width=0%, so first sample should be near 0

    // The first sample should be transitioning from mono (0% width)
    // Not exactly at either 0% or 200% value
    REQUIRE(leftOut[0] < 2.0f);  // Not at full 200% yet
    REQUIRE(leftOut[0] > 0.0f);  // Some side coming through

    // Last sample should be closer to target
    REQUIRE(leftOut[63] > leftOut[0]);

    // Check for no sudden jumps (click-free)
    // Adjacent samples should not differ by more than a reasonable amount
    float maxJump = 0.0f;
    for (size_t i = 1; i < 64; ++i) {
        float jump = std::abs(leftOut[i] - leftOut[i-1]);
        if (jump > maxJump) maxJump = jump;
    }
    // Max jump should be small (smoothed transition)
    REQUIRE(maxJump < 0.2f);  // Reasonable threshold for smoothed transition
}

// ==============================================================================
// User Story 3: Independent Mid and Side Gain (P3)
// ==============================================================================

// T031-T035 tests go here

// ==============================================================================
// User Story 4: Solo Modes for Monitoring (P4)
// ==============================================================================

// T044-T048 tests go here

// ==============================================================================
// User Story 5: Mono Input Handling (P5)
// ==============================================================================

// T057-T059 tests go here

// ==============================================================================
// User Story 6: Real-Time Safe Processing (P6)
// ==============================================================================

// T065-T067 tests go here

// ==============================================================================
// Polish: Edge Cases and Additional Features
// ==============================================================================

// T074-T077b tests go here
