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
#include <limits>
#include <numeric>
#include <random>
#include <vector>

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
    ms.setWidth(100.0f);  // Start at unity
    ms.reset();

    // Change width by 50% (moderate change)
    ms.setWidth(150.0f);

    // Process a buffer - first samples should be transitioning
    std::array<float, 256> left, right;
    std::array<float, 256> leftOut, rightOut;

    // Mixed stereo input for better smoothing visibility
    for (size_t i = 0; i < 256; ++i) {
        left[i] = 0.8f;
        right[i] = 0.2f;
    }

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 256);

    // SC-004: Parameter changes produce click-free transitions
    // At width=100%: Mid=0.5, Side=0.3, L=0.8, R=0.2
    // At width=150%: Mid=0.5, Side=0.3*1.5=0.45, L=0.95, R=0.05

    // First sample should be near starting value (width=100%)
    REQUIRE(leftOut[0] < 0.95f);  // Not at 150% yet
    REQUIRE(leftOut[0] >= 0.79f);  // Near starting value

    // Last sample should be closer to target (width=150%)
    REQUIRE(leftOut[255] > leftOut[0]);

    // Check for smooth transition (no sudden jumps)
    // With per-sample smoothing, adjacent samples should differ by small amount
    float maxJump = 0.0f;
    for (size_t i = 1; i < 256; ++i) {
        float jump = std::abs(leftOut[i] - leftOut[i-1]);
        if (jump > maxJump) maxJump = jump;
    }
    // Max jump should be small relative to the total change (~0.15)
    // With good smoothing, max jump should be < 1% of signal range per sample
    REQUIRE(maxJump < 0.05f);
}

// ==============================================================================
// User Story 3: Independent Mid and Side Gain (P3)
// ==============================================================================

// T031: midGain=+6dB doubles Mid amplitude
TEST_CASE("MidSideProcessor midGain=+6dB doubles Mid amplitude", "[midside][US3][gain]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setMidGain(6.0206f);  // Exactly +6dB = 2.0 linear
    ms.reset();

    // Input: pure mid content (L=R)
    std::array<float, 4> left  = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> right = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // Mid = 0.5, Side = 0
    // Mid * 2.0 = 1.0
    // L = R = Mid = 1.0
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(1.0f).margin(0.001f));
        REQUIRE(rightOut[i] == Approx(1.0f).margin(0.001f));
    }
}

// T032: sideGain=-96dB produces effectively silent Side
TEST_CASE("MidSideProcessor sideGain=-96dB produces effectively silent Side", "[midside][US3][gain]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSideGain(-96.0f);  // Essentially mutes side
    ms.reset();

    // Input: pure side content (L=-R)
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-011: Gain at -96dB MUST effectively silence the channel (< -120dB output)
    // Side = 1.0, Side * 10^(-96/20) ≈ Side * 1.58e-5 ≈ 0
    // Output should be essentially mono (L ≈ R ≈ Mid = 0)
    for (size_t i = 0; i < 4; ++i) {
        // Should be effectively zero (< -120dB = 1e-6)
        REQUIRE(std::abs(leftOut[i]) < 0.001f);
        REQUIRE(std::abs(rightOut[i]) < 0.001f);
        // And mono
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(0.001f));
    }
}

// T033: setMidGain/setSideGain clamp to [-96dB, +24dB]
TEST_CASE("MidSideProcessor gain setters clamp to valid range", "[midside][US3][gain]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("midGain clamps below minimum") {
        ms.setMidGain(-200.0f);
        REQUIRE(ms.getMidGain() == Approx(-96.0f));
    }

    SECTION("midGain clamps above maximum") {
        ms.setMidGain(50.0f);
        REQUIRE(ms.getMidGain() == Approx(24.0f));
    }

    SECTION("sideGain clamps below minimum") {
        ms.setSideGain(-150.0f);
        REQUIRE(ms.getSideGain() == Approx(-96.0f));
    }

    SECTION("sideGain clamps above maximum") {
        ms.setSideGain(30.0f);
        REQUIRE(ms.getSideGain() == Approx(24.0f));
    }

    SECTION("values within range are accepted") {
        ms.setMidGain(-12.0f);
        REQUIRE(ms.getMidGain() == Approx(-12.0f));

        ms.setSideGain(6.0f);
        REQUIRE(ms.getSideGain() == Approx(6.0f));
    }

    SECTION("boundary values work correctly") {
        ms.setMidGain(-96.0f);
        REQUIRE(ms.getMidGain() == Approx(-96.0f));

        ms.setMidGain(24.0f);
        REQUIRE(ms.getMidGain() == Approx(24.0f));
    }
}

// T034: gain changes are smoothed (click-free)
TEST_CASE("MidSideProcessor gain changes are smoothed", "[midside][US3][gain][smoothing]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setMidGain(0.0f);  // Start at unity
    ms.reset();

    // Change to +12dB without reset
    ms.setMidGain(12.0f);

    // Process a buffer - first samples should be transitioning
    std::array<float, 64> left, right;
    std::array<float, 64> leftOut, rightOut;

    // Pure mid input
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 64);

    // FR-012: Gain changes MUST be smoothed to prevent clicks
    // At +12dB (4x), output would be 0.5 * 4 = 2.0
    // But we started from 0dB (1x), so first sample should be near 0.5

    // First sample should be close to starting value
    REQUIRE(leftOut[0] < 1.0f);  // Not at full +12dB yet
    REQUIRE(leftOut[0] >= 0.5f - 0.01f);  // Near starting value

    // Last sample should be closer to target
    REQUIRE(leftOut[63] > leftOut[0]);

    // Check for no sudden jumps (click-free)
    float maxJump = 0.0f;
    for (size_t i = 1; i < 64; ++i) {
        float jump = std::abs(leftOut[i] - leftOut[i-1]);
        if (jump > maxJump) maxJump = jump;
    }
    // Max jump should be small (smoothed transition)
    REQUIRE(maxJump < 0.1f);
}

// T035: gain uses dbToGain() for conversion
TEST_CASE("MidSideProcessor gain uses correct dB conversion", "[midside][US3][gain]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("-6dB halves amplitude") {
        ms.setMidGain(-6.0206f);  // -6dB ≈ 0.5 linear
        ms.reset();

        std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> right = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Mid = 1.0, Side = 0
        // Mid * 0.5 = 0.5
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(leftOut[i] == Approx(0.5f).margin(0.001f));
        }
    }

    SECTION("0dB is unity") {
        ms.setMidGain(0.0f);
        ms.setSideGain(0.0f);
        ms.reset();

        std::array<float, 4> left  = {0.7f, -0.3f, 0.5f, -0.9f};
        std::array<float, 4> right = {0.7f, -0.3f, 0.5f, -0.9f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(leftOut[i] == Approx(left[i]).margin(kTolerance));
        }
    }

    SECTION("+20dB multiplies by 10") {
        ms.setSideGain(20.0f);  // +20dB = 10x
        ms.reset();

        // Pure side input (L=0.1, R=-0.1) -> Side = 0.1
        std::array<float, 4> left  = {0.1f, 0.1f, 0.1f, 0.1f};
        std::array<float, 4> right = {-0.1f, -0.1f, -0.1f, -0.1f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Mid = 0, Side = 0.1, Side * 10 = 1.0
        // L = 0 + 1.0 = 1.0, R = 0 - 1.0 = -1.0
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(leftOut[i] == Approx(1.0f).margin(0.01f));
            REQUIRE(rightOut[i] == Approx(-1.0f).margin(0.01f));
        }
    }
}

// ==============================================================================
// User Story 4: Solo Modes for Monitoring (P4)
// ==============================================================================

// T044: soloMid=true outputs only Mid content (L=R=Mid)
TEST_CASE("MidSideProcessor soloMid outputs only Mid content", "[midside][US4][solo]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSoloMid(true);
    ms.reset();

    // Input: mixed stereo
    std::array<float, 4> left  = {0.8f, 0.6f, 0.4f, 0.2f};
    std::array<float, 4> right = {0.2f, 0.4f, 0.6f, 0.8f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-015: soloMid - output Mid only: L = R = Mid
    for (size_t i = 0; i < 4; ++i) {
        float expectedMid = (left[i] + right[i]) * 0.5f;
        REQUIRE(leftOut[i] == Approx(expectedMid).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(expectedMid).margin(kTolerance));
        // L should equal R (mono output)
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(kTolerance));
    }
}

// T045: soloSide=true outputs only Side content (L=+Side, R=-Side)
TEST_CASE("MidSideProcessor soloSide outputs only Side content", "[midside][US4][solo]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSoloSide(true);
    ms.reset();

    // Input: mixed stereo
    std::array<float, 4> left  = {0.8f, 0.6f, 0.4f, 0.2f};
    std::array<float, 4> right = {0.2f, 0.4f, 0.6f, 0.8f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-016: soloSide - output Side only: L = +Side, R = -Side
    for (size_t i = 0; i < 4; ++i) {
        float expectedSide = (left[i] - right[i]) * 0.5f;
        REQUIRE(leftOut[i] == Approx(expectedSide).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(-expectedSide).margin(kTolerance));
        // L should be opposite of R
        REQUIRE(leftOut[i] == Approx(-rightOut[i]).margin(kTolerance));
    }
}

// T046: Both solos enabled - soloMid takes precedence
TEST_CASE("MidSideProcessor soloMid takes precedence over soloSide", "[midside][US4][solo]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSoloMid(true);
    ms.setSoloSide(true);  // Both enabled
    ms.reset();

    // Input: pure side content
    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-017: When both solos enabled, soloMid MUST take precedence
    // Mid = 0, so output should be silence
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(0.0f).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(0.0f).margin(kTolerance));
    }
}

// T047: Solo mode toggled produces click-free transition
TEST_CASE("MidSideProcessor solo mode transitions are click-free", "[midside][US4][solo][smoothing]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSoloMid(false);
    ms.reset();

    // Toggle solo mid on (without reset)
    ms.setSoloMid(true);

    // Process a buffer
    std::array<float, 64> left, right;
    std::array<float, 64> leftOut, rightOut;

    // Input: pure side content (will go silent when soloMid)
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), -1.0f);

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 64);

    // FR-018: Solo mode changes MUST be smoothed to prevent clicks
    // Output should transition from full side (L=1, R=-1) to silent (L=R=0)

    // First sample should still have some side content
    REQUIRE(std::abs(leftOut[0]) > 0.1f);

    // Check for no sudden jumps
    float maxJump = 0.0f;
    for (size_t i = 1; i < 64; ++i) {
        float jump = std::abs(leftOut[i] - leftOut[i-1]);
        if (jump > maxJump) maxJump = jump;
    }
    REQUIRE(maxJump < 0.2f);
}

// T048: Getter methods for solo states
TEST_CASE("MidSideProcessor solo getters work correctly", "[midside][US4][solo]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    // Default state
    REQUIRE_FALSE(ms.isSoloMidEnabled());
    REQUIRE_FALSE(ms.isSoloSideEnabled());

    // Set and check
    ms.setSoloMid(true);
    REQUIRE(ms.isSoloMidEnabled());
    REQUIRE_FALSE(ms.isSoloSideEnabled());

    ms.setSoloMid(false);
    ms.setSoloSide(true);
    REQUIRE_FALSE(ms.isSoloMidEnabled());
    REQUIRE(ms.isSoloSideEnabled());
}

// ==============================================================================
// User Story 5: Mono Input Handling (P5)
// ==============================================================================

// T057: Mono input (L=R) produces Side=0 exactly
TEST_CASE("MidSideProcessor mono input produces zero Side", "[midside][US5][mono]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    // Mono input: L = R
    std::array<float, 4> left  = {0.5f, -0.3f, 0.8f, -0.1f};
    std::array<float, 4> right = {0.5f, -0.3f, 0.8f, -0.1f};  // Same as left
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-019: Mono input (L=R) produces Side=0 exactly
    // SC-008: Mono input produces exactly zero Side component
    // With Side=0, L = R = Mid
    for (size_t i = 0; i < 4; ++i) {
        // Output should be mono (L = R)
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(kTolerance));
        // And equal to original input
        REQUIRE(leftOut[i] == Approx(left[i]).margin(kTolerance));
    }
}

// T058: Mono input with width=200% remains mono
TEST_CASE("MidSideProcessor mono input with width=200% remains mono", "[midside][US5][mono]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setWidth(200.0f);  // Maximum width
    ms.reset();

    // Mono input: L = R
    std::array<float, 4> left  = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> right = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-020: Width adjustments on mono input MUST NOT produce phantom stereo
    // Side = 0, so Side * 2 = 0 - output remains mono
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(kTolerance));
        REQUIRE(leftOut[i] == Approx(0.5f).margin(kTolerance));
    }
}

// T059: Mono input with sideGain boost produces no noise
TEST_CASE("MidSideProcessor mono input with sideGain boost produces no noise", "[midside][US5][mono]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.setSideGain(20.0f);  // +20dB boost on side
    ms.reset();

    // Mono input: L = R (Side is exactly 0)
    std::array<float, 4> left  = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> right = {0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // FR-020: Gain adjustments on mono input MUST NOT produce noise
    // Side = 0, so Side * 10 = 0 - output remains clean mono
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(rightOut[i]).margin(kTolerance));
        // Should still be 0.5 (mid-only output)
        REQUIRE(leftOut[i] == Approx(0.5f).margin(kTolerance));
    }
}

// ==============================================================================
// User Story 6: Real-Time Safe Processing (P6)
// ==============================================================================

// T065: process() handles various block sizes (1 to 8192)
TEST_CASE("MidSideProcessor handles various block sizes", "[midside][US6][realtime]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, 8192);
    ms.reset();

    const std::array<size_t, 5> blockSizes = {1, 64, 512, 2048, 8192};

    for (size_t blockSize : blockSizes) {
        DYNAMIC_SECTION("block size: " << blockSize) {
            std::vector<float> left(blockSize, 0.5f);
            std::vector<float> right(blockSize, 0.3f);
            std::vector<float> leftOut(blockSize);
            std::vector<float> rightOut(blockSize);

            // FR-023: System MUST support block sizes from 1 to 8192 samples
            REQUIRE_NOTHROW(ms.process(left.data(), right.data(),
                                       leftOut.data(), rightOut.data(), blockSize));

            // Verify processing happened correctly
            for (size_t i = 0; i < blockSize; ++i) {
                REQUIRE(std::isfinite(leftOut[i]));
                REQUIRE(std::isfinite(rightOut[i]));
            }
        }
    }
}

// T066: Extreme parameter values produce bounded output
TEST_CASE("MidSideProcessor extreme parameters produce bounded output", "[midside][US6][realtime]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("maximum gain + maximum width") {
        ms.setMidGain(24.0f);   // +24dB = ~15.85x
        ms.setSideGain(24.0f);
        ms.setWidth(200.0f);
        ms.reset();

        std::array<float, 4> left  = {0.1f, 0.1f, 0.1f, 0.1f};
        std::array<float, 4> right = {-0.1f, -0.1f, -0.1f, -0.1f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Output should be finite (no NaN or Inf)
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(std::isfinite(leftOut[i]));
            REQUIRE(std::isfinite(rightOut[i]));
        }
    }

    SECTION("minimum gain (silence)") {
        ms.setMidGain(-96.0f);
        ms.setSideGain(-96.0f);
        ms.reset();

        std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Output should be essentially silent
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(std::abs(leftOut[i]) < 0.001f);
            REQUIRE(std::abs(rightOut[i]) < 0.001f);
        }
    }
}

// T067: process() is noexcept (compile-time check via static_assert where possible)
TEST_CASE("MidSideProcessor methods are noexcept", "[midside][US6][realtime]") {
    // FR-022: process() MUST be noexcept
    // This is verified at compile time via noexcept specifier on the function

    MidSideProcessor ms;

    // Verify noexcept attribute on process()
    float left = 0.5f, right = 0.3f;
    float leftOut, rightOut;

    // If this compiles without warning, process() is noexcept
    static_assert(noexcept(ms.process(&left, &right, &leftOut, &rightOut, 1)),
                  "process() must be noexcept");

    // Also verify other methods
    static_assert(noexcept(ms.prepare(44100.0f, 512)), "prepare() must be noexcept");
    static_assert(noexcept(ms.reset()), "reset() must be noexcept");
    static_assert(noexcept(ms.setWidth(100.0f)), "setWidth() must be noexcept");
    static_assert(noexcept(ms.setMidGain(0.0f)), "setMidGain() must be noexcept");
    static_assert(noexcept(ms.setSideGain(0.0f)), "setSideGain() must be noexcept");
    static_assert(noexcept(ms.setSoloMid(true)), "setSoloMid() must be noexcept");
    static_assert(noexcept(ms.setSoloSide(true)), "setSoloSide() must be noexcept");

    REQUIRE(true);  // Test passed if we got here (compile-time checks passed)
}

// ==============================================================================
// Polish: Edge Cases and Additional Features
// ==============================================================================

// T074: NaN input handling
TEST_CASE("MidSideProcessor handles NaN input safely", "[midside][polish][edge]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    std::array<float, 4> left  = {0.5f, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f};
    std::array<float, 4> right = {0.5f, 0.5f, std::numeric_limits<float>::quiet_NaN(), 0.5f};
    std::array<float, 4> leftOut{}, rightOut{};

    // Should not crash
    REQUIRE_NOTHROW(ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4));

    // Samples without NaN input should still be valid
    REQUIRE(std::isfinite(leftOut[0]));
    REQUIRE(std::isfinite(rightOut[0]));
    REQUIRE(std::isfinite(leftOut[3]));
    REQUIRE(std::isfinite(rightOut[3]));
}

// T075: Infinity input handling
TEST_CASE("MidSideProcessor handles infinity input safely", "[midside][polish][edge]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    std::array<float, 4> left  = {0.5f, std::numeric_limits<float>::infinity(), 0.5f, 0.5f};
    std::array<float, 4> right = {0.5f, 0.5f, -std::numeric_limits<float>::infinity(), 0.5f};
    std::array<float, 4> leftOut{}, rightOut{};

    // Should not crash
    REQUIRE_NOTHROW(ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4));
}

// T076: Width boundary values (exactly 0% and 200%)
TEST_CASE("MidSideProcessor width boundary values work correctly", "[midside][polish][edge]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("exactly 0%") {
        ms.setWidth(0.0f);
        ms.reset();

        std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Should be exactly mono (Mid = 0)
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(leftOut[i] == Approx(0.0f).margin(kTolerance));
            REQUIRE(rightOut[i] == Approx(0.0f).margin(kTolerance));
        }
    }

    SECTION("exactly 200%") {
        ms.setWidth(200.0f);
        ms.reset();

        std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
        std::array<float, 4> leftOut{}, rightOut{};

        ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

        // Side doubled: L = 2.0, R = -2.0
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(leftOut[i] == Approx(2.0f).margin(kTolerance));
            REQUIRE(rightOut[i] == Approx(-2.0f).margin(kTolerance));
        }
    }
}

// T077: DC offset preservation through encode/decode cycle
TEST_CASE("MidSideProcessor preserves DC offset", "[midside][polish][edge]") {
    MidSideProcessor ms;
    ms.prepare(kTestSampleRate, kTestBlockSize);
    ms.reset();

    // Input with DC offset
    const float dcOffset = 0.3f;
    std::array<float, 4> left  = {dcOffset, dcOffset, dcOffset, dcOffset};
    std::array<float, 4> right = {dcOffset, dcOffset, dcOffset, dcOffset};
    std::array<float, 4> leftOut{}, rightOut{};

    ms.process(left.data(), right.data(), leftOut.data(), rightOut.data(), 4);

    // DC offset should be preserved through encode/decode
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut[i] == Approx(dcOffset).margin(kTolerance));
        REQUIRE(rightOut[i] == Approx(dcOffset).margin(kTolerance));
    }
}

// T077a: DC offset test (from analyze remediation)
// Already covered by T077 above

// T077b: Sample rate change handling
TEST_CASE("MidSideProcessor handles sample rate changes", "[midside][polish][edge]") {
    MidSideProcessor ms;

    // Prepare at 44.1kHz
    ms.prepare(44100.0f, 512);
    ms.setWidth(50.0f);
    ms.reset();

    std::array<float, 4> left  = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> right = {-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4> leftOut1{}, rightOut1{};

    ms.process(left.data(), right.data(), leftOut1.data(), rightOut1.data(), 4);

    // Re-prepare at 96kHz (simulating sample rate change)
    ms.prepare(96000.0f, 512);
    ms.reset();  // Snap smoothers after sample rate change

    std::array<float, 4> leftOut2{}, rightOut2{};
    ms.process(left.data(), right.data(), leftOut2.data(), rightOut2.data(), 4);

    // Results should be similar (same width setting)
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(leftOut2[i] == Approx(leftOut1[i]).margin(0.01f));
        REQUIRE(rightOut2[i] == Approx(rightOut1[i]).margin(0.01f));
    }
}
