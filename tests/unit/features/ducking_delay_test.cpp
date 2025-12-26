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

// T015: Ducking enable/disable control (FR-001)
TEST_CASE("DuckingDelay enable/disable control", "[ducking-delay][US1][FR-001]") {
    auto delay = createPreparedDelay();

    SECTION("Ducking is enabled by default") {
        REQUIRE(delay.isDuckingEnabled());
    }

    SECTION("Can disable ducking") {
        delay.setDuckingEnabled(false);
        REQUIRE_FALSE(delay.isDuckingEnabled());
    }

    SECTION("Can re-enable ducking") {
        delay.setDuckingEnabled(false);
        delay.setDuckingEnabled(true);
        REQUIRE(delay.isDuckingEnabled());
    }

    SECTION("Disabled ducking passes delay signal unchanged") {
        delay.setDuckingEnabled(false);
        delay.setDelayTimeMs(100.0f);  // Short delay
        delay.setFeedbackAmount(0.0f);  // No feedback
        delay.setDryWetMix(100.0f);     // 100% wet
        delay.setThreshold(-60.0f);     // Low threshold
        delay.setDuckAmount(100.0f);    // Full ducking
        delay.snapParameters();

        // Feed impulse through delay
        std::vector<float> left(kBlockSize * 10, 0.0f);
        std::vector<float> right(kBlockSize * 10, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        auto ctx = makeTestContext();

        // Process enough blocks to get impulse through delay
        for (size_t block = 0; block < 10; ++block) {
            delay.process(left.data() + block * kBlockSize,
                         right.data() + block * kBlockSize,
                         kBlockSize, ctx);
        }

        // Find delayed impulse - should have energy (not ducked to silence)
        float delayedPeak = 0.0f;
        for (size_t i = 100; i < left.size(); ++i) {  // After delay time
            delayedPeak = std::max(delayedPeak, std::abs(left[i]));
        }
        REQUIRE(delayedPeak > 0.1f);  // Should have significant output
    }
}

// T016: Threshold triggers ducking (FR-002, SC-001)
TEST_CASE("DuckingDelay threshold triggers ducking", "[ducking-delay][US1][FR-002][SC-001]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(100.0f);    // Full ducking for clear test
    delay.setAttackTime(0.1f);      // Fastest attack
    delay.setReleaseTime(10.0f);    // Short release
    delay.setDryWetMix(100.0f);     // 100% wet to see ducking
    delay.setDelayTimeMs(100.0f);
    delay.setFeedbackAmount(0.0f);

    SECTION("Signal above threshold triggers ducking") {
        delay.setThreshold(-20.0f);  // -20dB threshold
        delay.snapParameters();

        // Prime the delay with an impulse
        std::vector<float> primeL(kBlockSize, 0.0f);
        std::vector<float> primeR(kBlockSize, 0.0f);
        primeL[0] = 0.5f;
        primeR[0] = 0.5f;
        auto ctx = makeTestContext();
        delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

        // Now feed loud signal (-6dB, above threshold)
        std::vector<float> left(kBlockSize, 0.5f);  // ~-6dB
        std::vector<float> right(kBlockSize, 0.5f);

        // Process several blocks to let ducking engage
        for (int i = 0; i < 5; ++i) {
            delay.process(left.data(), right.data(), kBlockSize, ctx);
        }

        // Gain reduction should be significant
        float gr = delay.getGainReduction();
        REQUIRE(gr < -6.0f);  // Should show significant reduction
    }

    SECTION("Signal below threshold does not trigger ducking") {
        delay.setThreshold(-20.0f);  // -20dB threshold
        delay.snapParameters();

        // Prime with quiet impulse
        std::vector<float> primeL(kBlockSize, 0.0f);
        std::vector<float> primeR(kBlockSize, 0.0f);
        primeL[0] = 0.01f;  // Very quiet
        primeR[0] = 0.01f;
        auto ctx = makeTestContext();
        delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

        // Feed quiet signal (-40dB, below threshold)
        std::vector<float> left(kBlockSize, 0.01f);  // ~-40dB
        std::vector<float> right(kBlockSize, 0.01f);

        for (int i = 0; i < 5; ++i) {
            delay.process(left.data(), right.data(), kBlockSize, ctx);
        }

        // Gain reduction should be minimal
        float gr = delay.getGainReduction();
        REQUIRE(gr > -3.0f);  // Little to no reduction
    }

    SECTION("Threshold range is -60 to 0 dB (FR-002)") {
        delay.setThreshold(-60.0f);
        REQUIRE(delay.getThreshold() == Approx(-60.0f));

        delay.setThreshold(0.0f);
        REQUIRE(delay.getThreshold() == Approx(0.0f));

        delay.setThreshold(-80.0f);  // Below min, should clamp
        REQUIRE(delay.getThreshold() == Approx(-60.0f));

        delay.setThreshold(10.0f);  // Above max, should clamp
        REQUIRE(delay.getThreshold() == Approx(0.0f));
    }
}

// T017: Duck amount 0% results in no attenuation (FR-005)
TEST_CASE("DuckingDelay duck amount 0% results in no attenuation", "[ducking-delay][US1][FR-005]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(0.0f);      // 0% = no ducking
    delay.setThreshold(-60.0f);     // Very low threshold
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    // Prime delay
    std::vector<float> primeL(kBlockSize, 0.0f);
    std::vector<float> primeR(kBlockSize, 0.0f);
    primeL[0] = 1.0f;
    primeR[0] = 1.0f;
    auto ctx = makeTestContext();
    delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

    // Feed loud signal
    std::vector<float> left(kBlockSize, 0.5f);
    std::vector<float> right(kBlockSize, 0.5f);

    for (int i = 0; i < 10; ++i) {
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // With 0% duck amount, gain reduction should be 0
    float gr = delay.getGainReduction();
    REQUIRE(gr == Approx(0.0f).margin(0.5f));
}

// T018: Duck amount 100% results in -48dB attenuation (FR-004, SC-003)
TEST_CASE("DuckingDelay duck amount 100% results in -48dB attenuation", "[ducking-delay][US1][FR-004][SC-003]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(100.0f);    // 100% = -48dB
    delay.setThreshold(-60.0f);     // Very low threshold
    delay.setAttackTime(0.1f);      // Fast attack
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    // Prime delay
    std::vector<float> primeL(kBlockSize, 0.0f);
    std::vector<float> primeR(kBlockSize, 0.0f);
    primeL[0] = 1.0f;
    primeR[0] = 1.0f;
    auto ctx = makeTestContext();
    delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

    // Feed loud continuous signal
    std::vector<float> left(kBlockSize, 0.9f);
    std::vector<float> right(kBlockSize, 0.9f);

    // Process enough blocks for full attack
    for (int i = 0; i < 20; ++i) {
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // Gain reduction should approach -48dB
    float gr = delay.getGainReduction();
    REQUIRE(gr < -40.0f);  // Should be close to -48dB
}

// T019: Duck amount 50% results in approximately -24dB attenuation (FR-003)
TEST_CASE("DuckingDelay duck amount 50% results in -24dB attenuation", "[ducking-delay][US1][FR-003]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(50.0f);     // 50% = -24dB
    delay.setThreshold(-60.0f);
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    // Prime delay
    std::vector<float> primeL(kBlockSize, 0.0f);
    std::vector<float> primeR(kBlockSize, 0.0f);
    primeL[0] = 1.0f;
    primeR[0] = 1.0f;
    auto ctx = makeTestContext();
    delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

    // Feed loud continuous signal
    std::vector<float> left(kBlockSize, 0.9f);
    std::vector<float> right(kBlockSize, 0.9f);

    for (int i = 0; i < 20; ++i) {
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // Gain reduction should be around -24dB
    float gr = delay.getGainReduction();
    REQUIRE(gr < -18.0f);
    REQUIRE(gr > -30.0f);  // Should be roughly -24dB +/- 6dB
}

// T020: Ducking engages within attack time (FR-006, SC-001)
TEST_CASE("DuckingDelay engages within attack time", "[ducking-delay][US1][FR-006][SC-001]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(100.0f);
    delay.setThreshold(-60.0f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(50.0f);
    delay.setFeedbackAmount(0.0f);

    SECTION("Attack time range is 0.1 to 100 ms (FR-006)") {
        delay.setAttackTime(0.1f);
        REQUIRE(delay.getAttackTime() == Approx(0.1f));

        delay.setAttackTime(100.0f);
        REQUIRE(delay.getAttackTime() == Approx(100.0f));

        delay.setAttackTime(0.01f);  // Below min
        REQUIRE(delay.getAttackTime() == Approx(0.1f));

        delay.setAttackTime(200.0f);  // Above max
        REQUIRE(delay.getAttackTime() == Approx(100.0f));
    }

    SECTION("Fast attack engages quickly") {
        delay.setAttackTime(0.1f);  // 0.1ms = very fast
        delay.snapParameters();

        // Prime with impulse
        std::vector<float> primeL(kBlockSize, 0.0f);
        std::vector<float> primeR(kBlockSize, 0.0f);
        primeL[0] = 1.0f;
        primeR[0] = 1.0f;
        auto ctx = makeTestContext();
        delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

        // Start with silence, then loud signal
        delay.reset();
        std::vector<float> left(kBlockSize, 0.9f);
        std::vector<float> right(kBlockSize, 0.9f);

        // One block should be enough for fast attack
        delay.process(left.data(), right.data(), kBlockSize, ctx);

        float gr = delay.getGainReduction();
        REQUIRE(gr < -10.0f);  // Should have engaged significantly
    }
}

// T021: Ducking releases within release time (FR-007, SC-002)
TEST_CASE("DuckingDelay releases within release time", "[ducking-delay][US1][FR-007][SC-002]") {
    auto delay = createPreparedDelay();

    SECTION("Release time range is 10 to 2000 ms (FR-007)") {
        delay.setReleaseTime(10.0f);
        REQUIRE(delay.getReleaseTime() == Approx(10.0f));

        delay.setReleaseTime(2000.0f);
        REQUIRE(delay.getReleaseTime() == Approx(2000.0f));

        delay.setReleaseTime(5.0f);  // Below min
        REQUIRE(delay.getReleaseTime() == Approx(10.0f));

        delay.setReleaseTime(3000.0f);  // Above max
        REQUIRE(delay.getReleaseTime() == Approx(2000.0f));
    }

    SECTION("Fast release recovers quickly") {
        delay.setDuckingEnabled(true);
        delay.setDuckAmount(100.0f);
        delay.setThreshold(-60.0f);
        delay.setAttackTime(0.1f);
        delay.setReleaseTime(10.0f);  // Fast release
        delay.setHoldTime(0.0f);      // No hold
        delay.setDryWetMix(100.0f);
        delay.setDelayTimeMs(50.0f);
        delay.setFeedbackAmount(0.0f);
        delay.snapParameters();

        auto ctx = makeTestContext();

        // Prime and engage ducking with loud signal (separate buffers)
        for (int i = 0; i < 10; ++i) {
            std::vector<float> loudL(kBlockSize, 0.9f);
            std::vector<float> loudR(kBlockSize, 0.9f);
            delay.process(loudL.data(), loudR.data(), kBlockSize, ctx);
        }

        // Verify ducking is engaged
        float engagedGR = delay.getGainReduction();
        REQUIRE(engagedGR < -30.0f);

        // Now feed silence for release (separate buffers)
        for (int i = 0; i < 5; ++i) {
            std::vector<float> silenceL(kBlockSize, 0.0f);
            std::vector<float> silenceR(kBlockSize, 0.0f);
            delay.process(silenceL.data(), silenceR.data(), kBlockSize, ctx);
        }

        // Gain reduction should have recovered
        float releasedGR = delay.getGainReduction();
        REQUIRE(releasedGR > engagedGR);  // Should have increased (less negative)
    }
}

// T022: Dry/wet mix control (FR-020)
TEST_CASE("DuckingDelay dry/wet mix control", "[ducking-delay][US1][FR-020]") {
    auto delay = createPreparedDelay();

    SECTION("Dry/wet range is 0 to 100%") {
        delay.setDryWetMix(0.0f);
        REQUIRE(delay.getDryWetMix() == Approx(0.0f));

        delay.setDryWetMix(100.0f);
        REQUIRE(delay.getDryWetMix() == Approx(100.0f));

        delay.setDryWetMix(50.0f);
        REQUIRE(delay.getDryWetMix() == Approx(50.0f));

        delay.setDryWetMix(-10.0f);  // Below min
        REQUIRE(delay.getDryWetMix() == Approx(0.0f));

        delay.setDryWetMix(110.0f);  // Above max
        REQUIRE(delay.getDryWetMix() == Approx(100.0f));
    }

    SECTION("0% wet outputs only dry signal") {
        delay.setDryWetMix(0.0f);
        delay.setDelayTimeMs(100.0f);
        delay.setDuckingEnabled(false);
        delay.snapParameters();

        std::vector<float> left(kBlockSize, 0.5f);
        std::vector<float> right(kBlockSize, 0.5f);
        auto ctx = makeTestContext();

        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // Output should be unchanged (dry only)
        REQUIRE(left[0] == Approx(0.5f).margin(0.01f));
    }

    SECTION("100% wet outputs only delay signal") {
        delay.setDryWetMix(100.0f);
        delay.setDelayTimeMs(100.0f);
        delay.setFeedbackAmount(0.0f);
        delay.setDuckingEnabled(false);
        delay.snapParameters();

        // Process silence - no delayed signal yet
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        left[0] = 0.5f;  // Single impulse
        right[0] = 0.5f;
        auto ctx = makeTestContext();

        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // First sample should be near zero (only wet, but delay hasn't come through yet)
        REQUIRE(std::abs(left[0]) < 0.1f);
    }
}

// T023: Output gain control (FR-021)
TEST_CASE("DuckingDelay output gain control", "[ducking-delay][US1][FR-021]") {
    auto delay = createPreparedDelay();

    SECTION("Output gain range is -96 to +6 dB") {
        delay.setOutputGainDb(0.0f);
        REQUIRE(delay.getOutputGainDb() == Approx(0.0f));

        delay.setOutputGainDb(-96.0f);
        REQUIRE(delay.getOutputGainDb() == Approx(-96.0f));

        delay.setOutputGainDb(6.0f);
        REQUIRE(delay.getOutputGainDb() == Approx(6.0f));

        delay.setOutputGainDb(-100.0f);  // Below min
        REQUIRE(delay.getOutputGainDb() == Approx(-96.0f));

        delay.setOutputGainDb(12.0f);  // Above max
        REQUIRE(delay.getOutputGainDb() == Approx(6.0f));
    }

    SECTION("+6dB gain boosts output") {
        delay.setOutputGainDb(6.0f);
        delay.setDryWetMix(0.0f);  // Dry only for simple test
        delay.setDuckingEnabled(false);
        delay.snapParameters();

        std::vector<float> left(kBlockSize, 0.25f);
        std::vector<float> right(kBlockSize, 0.25f);
        auto ctx = makeTestContext();

        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // +6dB = approximately 2x gain
        // Allow for smoothing convergence
        float expected = 0.25f * std::pow(10.0f, 6.0f / 20.0f);  // ~0.5
        REQUIRE(left[kBlockSize - 1] == Approx(expected).margin(0.05f));
    }

    SECTION("-96dB effectively mutes output") {
        delay.setOutputGainDb(-96.0f);
        delay.setDryWetMix(0.0f);
        delay.setDuckingEnabled(false);
        delay.snapParameters();

        std::vector<float> left(kBlockSize, 0.5f);
        std::vector<float> right(kBlockSize, 0.5f);
        auto ctx = makeTestContext();

        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // Should be nearly silent
        REQUIRE(std::abs(left[kBlockSize - 1]) < 0.001f);
    }
}

// T024: Gain reduction meter (FR-022)
TEST_CASE("DuckingDelay gain reduction meter", "[ducking-delay][US1][FR-022]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckAmount(100.0f);
    delay.setThreshold(-60.0f);
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(50.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    SECTION("Returns 0 dB when not ducking") {
        // With no signal, no ducking
        auto ctx = makeTestContext();
        std::vector<float> silence(kBlockSize, 0.0f);

        delay.process(silence.data(), silence.data(), kBlockSize, ctx);

        float gr = delay.getGainReduction();
        REQUIRE(gr == Approx(0.0f).margin(1.0f));  // Should be 0 or near 0
    }

    SECTION("Returns negative dB when ducking") {
        auto ctx = makeTestContext();

        // Prime with impulse
        std::vector<float> prime(kBlockSize, 0.0f);
        prime[0] = 1.0f;
        delay.process(prime.data(), prime.data(), kBlockSize, ctx);

        // Feed loud signal
        std::vector<float> loud(kBlockSize, 0.9f);
        for (int i = 0; i < 10; ++i) {
            delay.process(loud.data(), loud.data(), kBlockSize, ctx);
        }

        float gr = delay.getGainReduction();
        REQUIRE(gr < 0.0f);  // Should be negative when ducking
        REQUIRE(gr > -60.0f);  // But not beyond reasonable range
    }
}

// =============================================================================
// Phase 4: User Story 2 Tests - Feedback Path Ducking
// =============================================================================

// T041: DuckTarget enum has Output, Feedback, Both values
TEST_CASE("DuckTarget enum values", "[ducking-delay][US2][FR-010]") {
    // Already tested in setup phase, but verify for US2 completeness
    REQUIRE(static_cast<int>(DuckTarget::Output) == 0);
    REQUIRE(static_cast<int>(DuckTarget::Feedback) == 1);
    REQUIRE(static_cast<int>(DuckTarget::Both) == 2);
}

// T042: setDuckTarget() and getDuckTarget() work correctly
TEST_CASE("DuckingDelay duck target getter/setter", "[ducking-delay][US2][FR-010]") {
    auto delay = createPreparedDelay();

    SECTION("Default target is Output") {
        REQUIRE(delay.getDuckTarget() == DuckTarget::Output);
    }

    SECTION("Can set target to Feedback") {
        delay.setDuckTarget(DuckTarget::Feedback);
        REQUIRE(delay.getDuckTarget() == DuckTarget::Feedback);
    }

    SECTION("Can set target to Both") {
        delay.setDuckTarget(DuckTarget::Both);
        REQUIRE(delay.getDuckTarget() == DuckTarget::Both);
    }

    SECTION("Can set target back to Output") {
        delay.setDuckTarget(DuckTarget::Both);
        delay.setDuckTarget(DuckTarget::Output);
        REQUIRE(delay.getDuckTarget() == DuckTarget::Output);
    }
}

// T043: Output mode ducks wet signal before dry/wet mix (FR-011)
TEST_CASE("DuckingDelay Output mode ducks wet signal", "[ducking-delay][US2][FR-011]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckTarget(DuckTarget::Output);
    delay.setDuckAmount(100.0f);
    delay.setThreshold(-60.0f);
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);  // 100% wet to see effect
    delay.setDelayTimeMs(50.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    auto ctx = makeTestContext();

    // Prime with impulse
    std::vector<float> primeL(kBlockSize, 0.0f);
    std::vector<float> primeR(kBlockSize, 0.0f);
    primeL[0] = 1.0f;
    primeR[0] = 1.0f;
    delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

    // Feed loud continuous signal - should duck the delay output
    for (int i = 0; i < 10; ++i) {
        std::vector<float> left(kBlockSize, 0.9f);
        std::vector<float> right(kBlockSize, 0.9f);
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // With Output mode, ducking should be engaged
    float gr = delay.getGainReduction();
    REQUIRE(gr < -30.0f);  // Should show significant ducking
}

// T044: Feedback mode preserves first tap, ducks subsequent repeats (FR-012)
TEST_CASE("DuckingDelay Feedback mode preserves first tap", "[ducking-delay][US2][FR-012]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckTarget(DuckTarget::Feedback);
    delay.setDuckAmount(100.0f);
    delay.setThreshold(-60.0f);
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(50.0f);
    delay.setFeedbackAmount(80.0f);  // High feedback
    delay.snapParameters();

    auto ctx = makeTestContext();

    // In Feedback mode, the output to user is NOT ducked
    // Only the feedback path is ducked

    // Feed loud continuous signal
    for (int i = 0; i < 10; ++i) {
        std::vector<float> left(kBlockSize, 0.9f);
        std::vector<float> right(kBlockSize, 0.9f);
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // The output should still have audio content (not ducked to silence)
    // because Feedback mode only ducks what feeds back, not the output
    std::vector<float> testL(kBlockSize, 0.9f);
    std::vector<float> testR(kBlockSize, 0.9f);
    delay.process(testL.data(), testR.data(), kBlockSize, ctx);

    float outputPeak = findStereoPeak(testL.data(), testR.data(), kBlockSize);
    // Should have some output (the first tap is preserved)
    // Note: With 100% wet, we'll see the delay output
    REQUIRE(outputPeak > 0.0f);  // Not complete silence
}

// T045: Both mode ducks both output and feedback paths (FR-013)
TEST_CASE("DuckingDelay Both mode ducks output and feedback", "[ducking-delay][US2][FR-013]") {
    auto delay = createPreparedDelay();
    delay.setDuckingEnabled(true);
    delay.setDuckTarget(DuckTarget::Both);
    delay.setDuckAmount(100.0f);
    delay.setThreshold(-60.0f);
    delay.setAttackTime(0.1f);
    delay.setDryWetMix(100.0f);
    delay.setDelayTimeMs(50.0f);
    delay.setFeedbackAmount(80.0f);
    delay.snapParameters();

    auto ctx = makeTestContext();

    // Prime with impulse
    std::vector<float> primeL(kBlockSize, 0.0f);
    std::vector<float> primeR(kBlockSize, 0.0f);
    primeL[0] = 1.0f;
    primeR[0] = 1.0f;
    delay.process(primeL.data(), primeR.data(), kBlockSize, ctx);

    // Feed loud continuous signal - both output and feedback are ducked
    for (int i = 0; i < 10; ++i) {
        std::vector<float> left(kBlockSize, 0.9f);
        std::vector<float> right(kBlockSize, 0.9f);
        delay.process(left.data(), right.data(), kBlockSize, ctx);
    }

    // Should have full ducking (both paths)
    float gr = delay.getGainReduction();
    REQUIRE(gr < -30.0f);  // Should show significant ducking
}

// Additional test: Compare Output vs Both modes
TEST_CASE("DuckingDelay Output and Both modes both duck output", "[ducking-delay][US2]") {
    auto ctx = makeTestContext();

    // Test with Output mode
    auto delayOutput = createPreparedDelay();
    delayOutput.setDuckingEnabled(true);
    delayOutput.setDuckTarget(DuckTarget::Output);
    delayOutput.setDuckAmount(100.0f);
    delayOutput.setThreshold(-60.0f);
    delayOutput.setAttackTime(0.1f);
    delayOutput.setDryWetMix(100.0f);
    delayOutput.setDelayTimeMs(50.0f);
    delayOutput.setFeedbackAmount(0.0f);
    delayOutput.snapParameters();

    // Test with Both mode
    auto delayBoth = createPreparedDelay();
    delayBoth.setDuckingEnabled(true);
    delayBoth.setDuckTarget(DuckTarget::Both);
    delayBoth.setDuckAmount(100.0f);
    delayBoth.setThreshold(-60.0f);
    delayBoth.setAttackTime(0.1f);
    delayBoth.setDryWetMix(100.0f);
    delayBoth.setDelayTimeMs(50.0f);
    delayBoth.setFeedbackAmount(0.0f);
    delayBoth.snapParameters();

    // Process both
    for (int i = 0; i < 10; ++i) {
        std::vector<float> left1(kBlockSize, 0.9f);
        std::vector<float> right1(kBlockSize, 0.9f);
        delayOutput.process(left1.data(), right1.data(), kBlockSize, ctx);

        std::vector<float> left2(kBlockSize, 0.9f);
        std::vector<float> right2(kBlockSize, 0.9f);
        delayBoth.process(left2.data(), right2.data(), kBlockSize, ctx);
    }

    float grOutput = delayOutput.getGainReduction();
    float grBoth = delayBoth.getGainReduction();

    // Both should show significant ducking
    REQUIRE(grOutput < -30.0f);
    REQUIRE(grBoth < -30.0f);
}

// =============================================================================
// Phase 5: User Story 3 Tests - Hold Time Control
// =============================================================================

// T052: Hold time range is 0 to 500 ms (FR-008)
TEST_CASE("DuckingDelay hold time parameter range", "[ducking-delay][US3][FR-008]") {
    auto delay = createPreparedDelay();

    SECTION("Hold time range is 0 to 500 ms") {
        delay.setHoldTime(0.0f);
        REQUIRE(delay.getHoldTime() == Approx(0.0f));

        delay.setHoldTime(500.0f);
        REQUIRE(delay.getHoldTime() == Approx(500.0f));

        delay.setHoldTime(250.0f);
        REQUIRE(delay.getHoldTime() == Approx(250.0f));
    }

    SECTION("Hold time clamped below minimum") {
        delay.setHoldTime(-10.0f);
        REQUIRE(delay.getHoldTime() == Approx(0.0f));
    }

    SECTION("Hold time clamped above maximum") {
        delay.setHoldTime(1000.0f);
        REQUIRE(delay.getHoldTime() == Approx(500.0f));
    }
}

// T053: Hold time delays release phase (FR-009)
TEST_CASE("DuckingDelay hold time delays release", "[ducking-delay][US3][FR-009]") {
    // This test verifies that hold time keeps ducking engaged after input drops
    // by comparing release behavior with and without hold time

    auto ctx = makeTestContext();

    SECTION("Zero hold time allows immediate release") {
        auto delay = createPreparedDelay();
        delay.setDuckingEnabled(true);
        delay.setDuckAmount(100.0f);
        delay.setThreshold(-60.0f);
        delay.setAttackTime(0.1f);
        delay.setReleaseTime(10.0f);  // Fast release
        delay.setHoldTime(0.0f);      // No hold
        delay.setDryWetMix(100.0f);
        delay.setDelayTimeMs(50.0f);
        delay.setFeedbackAmount(0.0f);
        delay.snapParameters();

        // Engage ducking (separate buffers)
        for (int i = 0; i < 10; ++i) {
            std::vector<float> loudL(kBlockSize, 0.9f);
            std::vector<float> loudR(kBlockSize, 0.9f);
            delay.process(loudL.data(), loudR.data(), kBlockSize, ctx);
        }

        float engagedGR = delay.getGainReduction();
        REQUIRE(engagedGR < -30.0f);

        // Feed silence - should start releasing (separate buffers)
        // Need enough blocks for release to complete (10ms release at 44.1kHz)
        for (int i = 0; i < 20; ++i) {
            std::vector<float> silenceL(kBlockSize, 0.0f);
            std::vector<float> silenceR(kBlockSize, 0.0f);
            delay.process(silenceL.data(), silenceR.data(), kBlockSize, ctx);
        }

        float releasedGR = delay.getGainReduction();
        // After extended silence, should have significantly recovered
        // from -48dB towards 0dB
        REQUIRE(releasedGR > -30.0f);  // Should have recovered significantly
    }

    SECTION("Non-zero hold time delays release") {
        auto delay = createPreparedDelay();
        delay.setDuckingEnabled(true);
        delay.setDuckAmount(100.0f);
        delay.setThreshold(-60.0f);
        delay.setAttackTime(0.1f);
        delay.setReleaseTime(10.0f);
        delay.setHoldTime(200.0f);    // 200ms hold
        delay.setDryWetMix(100.0f);
        delay.setDelayTimeMs(50.0f);
        delay.setFeedbackAmount(0.0f);
        delay.snapParameters();

        // Engage ducking (separate buffers)
        for (int i = 0; i < 10; ++i) {
            std::vector<float> loudL(kBlockSize, 0.9f);
            std::vector<float> loudR(kBlockSize, 0.9f);
            delay.process(loudL.data(), loudR.data(), kBlockSize, ctx);
        }

        float engagedGR = delay.getGainReduction();
        REQUIRE(engagedGR < -30.0f);

        // Feed one block of silence - should still be holding (separate buffers)
        std::vector<float> silenceL(kBlockSize, 0.0f);
        std::vector<float> silenceR(kBlockSize, 0.0f);
        delay.process(silenceL.data(), silenceR.data(), kBlockSize, ctx);

        float holdingGR = delay.getGainReduction();
        // During hold phase, gain reduction should remain similar
        REQUIRE(holdingGR < -20.0f);  // Still significantly reduced
    }
}

// T054: Default hold time (FR-008)
TEST_CASE("DuckingDelay default hold time", "[ducking-delay][US3][FR-008]") {
    DuckingDelay delay;
    // Default hold time should be a reasonable value
    REQUIRE(delay.getHoldTime() == Approx(DuckingDelay::kDefaultHoldMs));
}

// =============================================================================
// Phase 6: User Story 4 Tests - Sidechain Filtering
// =============================================================================

// T062: Sidechain filter enable/disable (FR-016)
TEST_CASE("DuckingDelay sidechain filter enable/disable", "[ducking-delay][US4][FR-016]") {
    auto delay = createPreparedDelay();

    SECTION("Sidechain filter disabled by default") {
        REQUIRE_FALSE(delay.isSidechainFilterEnabled());
    }

    SECTION("Can enable sidechain filter") {
        delay.setSidechainFilterEnabled(true);
        REQUIRE(delay.isSidechainFilterEnabled());
    }

    SECTION("Can disable sidechain filter") {
        delay.setSidechainFilterEnabled(true);
        delay.setSidechainFilterEnabled(false);
        REQUIRE_FALSE(delay.isSidechainFilterEnabled());
    }
}

// T063: Sidechain filter cutoff range (FR-015)
TEST_CASE("DuckingDelay sidechain filter cutoff range", "[ducking-delay][US4][FR-015]") {
    auto delay = createPreparedDelay();

    SECTION("Cutoff range is 20 to 500 Hz") {
        delay.setSidechainFilterCutoff(20.0f);
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(20.0f));

        delay.setSidechainFilterCutoff(500.0f);
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(500.0f));

        delay.setSidechainFilterCutoff(100.0f);
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(100.0f));
    }

    SECTION("Cutoff clamped below minimum") {
        delay.setSidechainFilterCutoff(10.0f);
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(20.0f));
    }

    SECTION("Cutoff clamped above maximum") {
        delay.setSidechainFilterCutoff(1000.0f);
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(500.0f));
    }
}

// T064: Sidechain highpass filter parameter setting (FR-014)
TEST_CASE("DuckingDelay sidechain highpass filter parameters", "[ducking-delay][US4][FR-014]") {
    auto delay = createPreparedDelay();

    // Verify filter parameters can be set and forwarded
    // The actual filtering behavior is tested in DuckingProcessor tests

    SECTION("Filter can be enabled and cutoff set") {
        delay.setSidechainFilterEnabled(true);
        delay.setSidechainFilterCutoff(100.0f);

        REQUIRE(delay.isSidechainFilterEnabled());
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(100.0f));
    }

    SECTION("Filter settings persist through snapParameters") {
        delay.setSidechainFilterEnabled(true);
        delay.setSidechainFilterCutoff(250.0f);
        delay.snapParameters();

        REQUIRE(delay.isSidechainFilterEnabled());
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(250.0f));
    }

    SECTION("Filter settings persist through reset") {
        delay.setSidechainFilterEnabled(true);
        delay.setSidechainFilterCutoff(300.0f);
        delay.reset();

        // Settings should persist (reset clears audio state, not parameters)
        REQUIRE(delay.isSidechainFilterEnabled());
        REQUIRE(delay.getSidechainFilterCutoff() == Approx(300.0f));
    }
}

// T065: Default sidechain filter cutoff
TEST_CASE("DuckingDelay default sidechain filter cutoff", "[ducking-delay][US4]") {
    DuckingDelay delay;
    // Default should be reasonable value (80 Hz per research.md)
    REQUIRE(delay.getSidechainFilterCutoff() == Approx(DuckingDelay::kDefaultSidechainHz));
}
