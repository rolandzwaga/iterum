// ==============================================================================
// Tests: DuckingDelay (Layer 4 User Feature)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests MUST be written before implementation.
//
// Feature: 032-ducking-delay
// Reference: specs/032-ducking-delay/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/features/ducking_delay.h"
#include "dsp/core/block_context.h"

#include <array>
#include <cmath>
#include <numeric>
#include <vector>
#include <algorithm>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kMaxDelayMs = 2000.0f;

/// @brief Create a default BlockContext for testing
BlockContext makeTestContext(double sampleRate = kSampleRate, double bpm = 120.0) {
    return BlockContext{
        .sampleRate = sampleRate,
        .blockSize = kBlockSize,
        .tempoBPM = bpm,
        .timeSignatureNumerator = 4,
        .timeSignatureDenominator = 4,
        .isPlaying = true
    };
}

/// @brief Generate silence in a stereo buffer
void generateSilence(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);
}

/// @brief Generate an impulse in a stereo buffer
void generateImpulse(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
}

/// @brief Generate a constant level signal (for threshold testing)
void generateConstantLevel(float* left, float* right, size_t size, float level) {
    std::fill(left, left + size, level);
    std::fill(right, right + size, level);
}

/// @brief Generate a sine wave
void generateSineWave(float* buffer, size_t size, float frequency, double sampleRate, float amplitude = 1.0f) {
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * static_cast<float>(std::sin(twoPi * frequency * static_cast<double>(i) / sampleRate));
    }
}

/// @brief Generate stereo sine wave
void generateStereoSineWave(float* left, float* right, size_t size, float frequency, double sampleRate, float amplitude = 1.0f) {
    generateSineWave(left, size, frequency, sampleRate, amplitude);
    generateSineWave(right, size, frequency, sampleRate, amplitude);
}

/// @brief Find peak in buffer
float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// @brief Find peak in stereo buffer
float findStereoPeak(const float* left, const float* right, size_t size) {
    return std::max(findPeak(left, size), findPeak(right, size));
}

/// @brief Calculate RMS energy
float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// @brief Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -96.0f;
    return 20.0f * std::log10(linear);
}

/// @brief Convert dB to linear amplitude
float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

/// @brief Create and prepare a DuckingDelay for testing
DuckingDelay createPreparedDelay(double sampleRate = kSampleRate, size_t maxBlockSize = kBlockSize) {
    DuckingDelay delay;
    delay.prepare(sampleRate, maxBlockSize);
    return delay;
}

} // anonymous namespace

// =============================================================================
// Phase 1: Setup Tests (Class Skeleton)
// =============================================================================

TEST_CASE("DuckingDelay class exists and can be instantiated", "[ducking-delay][setup]") {
    DuckingDelay delay;
    // Basic construction should succeed without crash
    REQUIRE(true);
}

TEST_CASE("DuckTarget enum has correct values", "[ducking-delay][setup]") {
    REQUIRE(static_cast<int>(DuckTarget::Output) == 0);
    REQUIRE(static_cast<int>(DuckTarget::Feedback) == 1);
    REQUIRE(static_cast<int>(DuckTarget::Both) == 2);
}

TEST_CASE("DuckingDelay can be prepared", "[ducking-delay][setup]") {
    DuckingDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    // Preparation should succeed without crash
    REQUIRE(true);
}

TEST_CASE("DuckingDelay can be reset", "[ducking-delay][setup]") {
    auto delay = createPreparedDelay();
    delay.reset();
    // Reset should succeed without crash
    REQUIRE(true);
}

// =============================================================================
// Phase 2: Foundational Tests (prepare/reset, parameter forwarding)
// =============================================================================

TEST_CASE("DuckingDelay prepare() sets prepared flag", "[ducking-delay][foundational]") {
    DuckingDelay delay;
    REQUIRE_FALSE(delay.isPrepared());

    delay.prepare(kSampleRate, kBlockSize);
    REQUIRE(delay.isPrepared());
}

TEST_CASE("DuckingDelay prepare() works at different sample rates", "[ducking-delay][foundational]") {
    SECTION("44100 Hz") {
        DuckingDelay delay;
        delay.prepare(44100.0, 512);
        REQUIRE(delay.isPrepared());
    }

    SECTION("48000 Hz") {
        DuckingDelay delay;
        delay.prepare(48000.0, 512);
        REQUIRE(delay.isPrepared());
    }

    SECTION("96000 Hz") {
        DuckingDelay delay;
        delay.prepare(96000.0, 1024);
        REQUIRE(delay.isPrepared());
    }

    SECTION("192000 Hz") {
        DuckingDelay delay;
        delay.prepare(192000.0, 2048);
        REQUIRE(delay.isPrepared());
    }
}

TEST_CASE("DuckingDelay reset() clears state without crash", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    // Process some audio
    std::array<float, kBlockSize> left{}, right{};
    left[0] = 1.0f;
    right[0] = 1.0f;
    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBlockSize, ctx);

    // Reset should not crash
    delay.reset();
    REQUIRE(delay.isPrepared());
}

TEST_CASE("DuckingDelay snapParameters() applies all parameter changes immediately", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    // Set multiple parameters
    delay.setDryWetMix(75.0f);
    delay.setOutputGainDb(-6.0f);
    delay.setDelayTimeMs(1000.0f);
    delay.setThreshold(-40.0f);
    delay.setDuckAmount(75.0f);

    // Snap parameters
    delay.snapParameters();

    // Verify parameters are set
    REQUIRE(delay.getDryWetMix() == Approx(75.0f));
    REQUIRE(delay.getOutputGainDb() == Approx(-6.0f));
    REQUIRE(delay.getDelayTimeMs() == Approx(1000.0f));
    REQUIRE(delay.getThreshold() == Approx(-40.0f));
    REQUIRE(delay.getDuckAmount() == Approx(75.0f));
}

TEST_CASE("DuckingDelay delay time parameter forwarding", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    SECTION("Set delay time within range") {
        delay.setDelayTimeMs(500.0f);
        REQUIRE(delay.getDelayTimeMs() == Approx(500.0f));
    }

    SECTION("Clamp delay time below minimum") {
        delay.setDelayTimeMs(5.0f);  // Below 10ms minimum
        REQUIRE(delay.getDelayTimeMs() == Approx(DuckingDelay::kMinDelayMs));
    }

    SECTION("Clamp delay time above maximum") {
        delay.setDelayTimeMs(10000.0f);  // Above 5000ms maximum
        REQUIRE(delay.getDelayTimeMs() == Approx(DuckingDelay::kMaxDelayMs));
    }
}

TEST_CASE("DuckingDelay feedback amount parameter forwarding", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    SECTION("Set feedback within range") {
        delay.setFeedbackAmount(50.0f);  // 50%
        REQUIRE(delay.getFeedbackAmount() == Approx(50.0f));
    }

    SECTION("Set feedback at maximum") {
        delay.setFeedbackAmount(120.0f);  // Max is 120%
        REQUIRE(delay.getFeedbackAmount() == Approx(120.0f));
    }

    SECTION("Clamp feedback above maximum") {
        delay.setFeedbackAmount(150.0f);
        REQUIRE(delay.getFeedbackAmount() == Approx(120.0f));
    }
}

TEST_CASE("DuckingDelay filter parameter forwarding", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    SECTION("Filter enable/disable") {
        REQUIRE_FALSE(delay.isFilterEnabled());
        delay.setFilterEnabled(true);
        REQUIRE(delay.isFilterEnabled());
        delay.setFilterEnabled(false);
        REQUIRE_FALSE(delay.isFilterEnabled());
    }

    SECTION("Filter cutoff within range") {
        delay.setFilterCutoff(2000.0f);
        REQUIRE(delay.getFilterCutoff() == Approx(2000.0f));
    }

    SECTION("Filter cutoff clamped to minimum") {
        delay.setFilterCutoff(10.0f);
        REQUIRE(delay.getFilterCutoff() == Approx(DuckingDelay::kMinFilterCutoff));
    }

    SECTION("Filter cutoff clamped to maximum") {
        delay.setFilterCutoff(25000.0f);
        REQUIRE(delay.getFilterCutoff() == Approx(DuckingDelay::kMaxFilterCutoff));
    }
}

TEST_CASE("DuckingDelay latency reports correctly", "[ducking-delay][foundational]") {
    auto delay = createPreparedDelay();

    // Latency should be reported (value depends on FFN implementation)
    std::size_t latency = delay.getLatencySamples();
    // FFN has zero latency in its current implementation
    REQUIRE(latency == 0);
}

// =============================================================================
// Phase 3: User Story 1 Tests - Basic Ducking Delay (MVP)
// =============================================================================
// Tests will be added in T015-T024

// =============================================================================
// Phase 4: User Story 2 Tests - Feedback Path Ducking
// =============================================================================
// Tests will be added in T041-T045

// =============================================================================
// Phase 5: User Story 3 Tests - Hold Time Control
// =============================================================================
// Tests will be added in T052-T054

// =============================================================================
// Phase 6: User Story 4 Tests - Sidechain Filtering
// =============================================================================
// Tests will be added in T062-T065
