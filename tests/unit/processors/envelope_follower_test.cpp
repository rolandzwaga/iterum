// ==============================================================================
// Layer 2: DSP Processor Tests - Envelope Follower
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests organized by user story for independent implementation and testing.
// Reference: specs/010-envelope-follower/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/envelope_follower.h"

#include <array>
#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave into buffer
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    const float omega = 2.0f * std::numbers::pi_v<float> * frequency / sampleRate;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(omega * static_cast<float>(i));
    }
}

/// Generate a step signal (0 for first half, value for second half)
inline void generateStep(float* buffer, size_t size, float value = 1.0f, size_t stepPoint = 0) {
    if (stepPoint == 0) stepPoint = size / 2;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = (i >= stepPoint) ? value : 0.0f;
    }
}

/// Generate an impulse (single sample at specified position)
inline void generateImpulse(float* buffer, size_t size, float value = 1.0f, size_t position = 0) {
    std::fill(buffer, buffer + size, 0.0f);
    if (position < size) {
        buffer[position] = value;
    }
}

/// Generate square wave
inline void generateSquare(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    const float period = sampleRate / frequency;
    for (size_t i = 0; i < size; ++i) {
        float phase = std::fmod(static_cast<float>(i), period) / period;
        buffer[i] = (phase < 0.5f) ? amplitude : -amplitude;
    }
}

/// Find maximum absolute value in buffer
inline float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Check if a value is a valid float (not NaN or Inf)
inline bool isValidFloat(float x) {
    return std::isfinite(x);
}

/// Calculate time in samples for a given time in ms
inline size_t msToSamples(float ms, double sampleRate) {
    return static_cast<size_t>(ms * 0.001 * sampleRate);
}

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational Tests
// =============================================================================

TEST_CASE("DetectionMode enum values", "[envelope][foundational]") {
    REQUIRE(static_cast<uint8_t>(DetectionMode::Amplitude) == 0);
    REQUIRE(static_cast<uint8_t>(DetectionMode::RMS) == 1);
    REQUIRE(static_cast<uint8_t>(DetectionMode::Peak) == 2);
}

TEST_CASE("EnvelopeFollower constants", "[envelope][foundational]") {
    REQUIRE(EnvelopeFollower::kMinAttackMs == Approx(0.1f));
    REQUIRE(EnvelopeFollower::kMaxAttackMs == Approx(500.0f));
    REQUIRE(EnvelopeFollower::kMinReleaseMs == Approx(1.0f));
    REQUIRE(EnvelopeFollower::kMaxReleaseMs == Approx(5000.0f));
    REQUIRE(EnvelopeFollower::kDefaultAttackMs == Approx(10.0f));
    REQUIRE(EnvelopeFollower::kDefaultReleaseMs == Approx(100.0f));
    REQUIRE(EnvelopeFollower::kMinSidechainHz == Approx(20.0f));
    REQUIRE(EnvelopeFollower::kMaxSidechainHz == Approx(500.0f));
    REQUIRE(EnvelopeFollower::kDefaultSidechainHz == Approx(80.0f));
}

TEST_CASE("EnvelopeFollower prepare and reset", "[envelope][foundational]") {
    EnvelopeFollower env;

    SECTION("prepare initializes processor") {
        env.prepare(44100.0, 512);
        // After prepare, envelope should be at 0
        REQUIRE(env.getCurrentValue() == Approx(0.0f));
    }

    SECTION("reset clears state") {
        env.prepare(44100.0, 512);
        // Process some samples to change state
        float sample = 1.0f;
        env.processSample(sample);
        REQUIRE(env.getCurrentValue() > 0.0f);

        // Reset should clear state
        env.reset();
        REQUIRE(env.getCurrentValue() == Approx(0.0f));
    }
}

TEST_CASE("EnvelopeFollower parameter getters/setters with clamping", "[envelope][foundational]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);

    SECTION("setMode and getMode") {
        env.setMode(DetectionMode::RMS);
        REQUIRE(env.getMode() == DetectionMode::RMS);

        env.setMode(DetectionMode::Peak);
        REQUIRE(env.getMode() == DetectionMode::Peak);

        env.setMode(DetectionMode::Amplitude);
        REQUIRE(env.getMode() == DetectionMode::Amplitude);
    }

    SECTION("setAttackTime clamps to valid range") {
        env.setAttackTime(10.0f);
        REQUIRE(env.getAttackTime() == Approx(10.0f));

        // Below minimum should clamp
        env.setAttackTime(0.01f);
        REQUIRE(env.getAttackTime() == Approx(EnvelopeFollower::kMinAttackMs));

        // Above maximum should clamp
        env.setAttackTime(1000.0f);
        REQUIRE(env.getAttackTime() == Approx(EnvelopeFollower::kMaxAttackMs));
    }

    SECTION("setReleaseTime clamps to valid range") {
        env.setReleaseTime(100.0f);
        REQUIRE(env.getReleaseTime() == Approx(100.0f));

        // Below minimum should clamp
        env.setReleaseTime(0.1f);
        REQUIRE(env.getReleaseTime() == Approx(EnvelopeFollower::kMinReleaseMs));

        // Above maximum should clamp
        env.setReleaseTime(10000.0f);
        REQUIRE(env.getReleaseTime() == Approx(EnvelopeFollower::kMaxReleaseMs));
    }
}

// =============================================================================
// Phase 3: User Story 1 - Basic Envelope Tracking (Amplitude Mode)
// =============================================================================

TEST_CASE("Amplitude mode attack time constant accuracy", "[envelope][US1]") {
    constexpr double kSampleRate = 44100.0;
    constexpr float kAttackMs = 10.0f;

    EnvelopeFollower env;
    env.prepare(kSampleRate, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(kAttackMs);
    env.setReleaseTime(1000.0f);  // Long release to isolate attack behavior

    // Calculate samples for one time constant (should reach 63% of target)
    const size_t attackSamples = msToSamples(kAttackMs, kSampleRate);

    // Feed step input from 0 to 1.0
    for (size_t i = 0; i < attackSamples; ++i) {
        env.processSample(1.0f);
    }

    // After one time constant, should be at ~63% of target
    float envelopeValue = env.getCurrentValue();
    REQUIRE(envelopeValue >= 0.55f);  // Allow some tolerance (55-70%)
    REQUIRE(envelopeValue <= 0.75f);
}

TEST_CASE("Amplitude mode release time constant accuracy", "[envelope][US1]") {
    constexpr double kSampleRate = 44100.0;
    constexpr float kReleaseMs = 100.0f;

    EnvelopeFollower env;
    env.prepare(kSampleRate, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(0.1f);  // Very fast attack
    env.setReleaseTime(kReleaseMs);

    // Build up envelope to 1.0 first
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }
    float peakValue = env.getCurrentValue();
    REQUIRE(peakValue > 0.95f);  // Should be near 1.0

    // Calculate samples for one time constant
    const size_t releaseSamples = msToSamples(kReleaseMs, kSampleRate);

    // Feed silence
    for (size_t i = 0; i < releaseSamples; ++i) {
        env.processSample(0.0f);
    }

    // After one time constant, should decay to ~37% of peak
    float envelopeValue = env.getCurrentValue();
    float expectedMin = peakValue * 0.30f;  // Allow tolerance (30-45%)
    float expectedMax = peakValue * 0.45f;
    REQUIRE(envelopeValue >= expectedMin);
    REQUIRE(envelopeValue <= expectedMax);
}

TEST_CASE("processSample returns envelope value and advances state", "[envelope][US1]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(1.0f);

    float result = env.processSample(1.0f);
    REQUIRE(result > 0.0f);
    REQUIRE(result == env.getCurrentValue());

    // Second sample should advance further
    float result2 = env.processSample(1.0f);
    REQUIRE(result2 > result);
}

TEST_CASE("process block (separate buffers)", "[envelope][US1]") {
    constexpr size_t kBlockSize = 64;
    EnvelopeFollower env;
    env.prepare(44100.0, kBlockSize);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(1.0f);

    std::array<float, kBlockSize> input;
    std::array<float, kBlockSize> output;
    std::fill(input.begin(), input.end(), 1.0f);
    std::fill(output.begin(), output.end(), 0.0f);

    env.process(input.data(), output.data(), kBlockSize);

    // Output should contain increasing envelope values
    REQUIRE(output[0] > 0.0f);
    REQUIRE(output[kBlockSize - 1] > output[0]);

    // Last output should match current value
    REQUIRE(output[kBlockSize - 1] == Approx(env.getCurrentValue()));
}

TEST_CASE("process block (in-place)", "[envelope][US1]") {
    constexpr size_t kBlockSize = 64;
    EnvelopeFollower env;
    env.prepare(44100.0, kBlockSize);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(1.0f);

    std::array<float, kBlockSize> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);

    env.process(buffer.data(), kBlockSize);

    // Buffer should now contain envelope values
    REQUIRE(buffer[0] > 0.0f);
    REQUIRE(buffer[kBlockSize - 1] > buffer[0]);
}

TEST_CASE("getCurrentValue returns current envelope without advancing", "[envelope][US1]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);

    env.processSample(1.0f);
    float value1 = env.getCurrentValue();
    float value2 = env.getCurrentValue();
    float value3 = env.getCurrentValue();

    // Multiple calls should return same value
    REQUIRE(value1 == value2);
    REQUIRE(value2 == value3);
}

TEST_CASE("Time constant scaling across sample rates", "[envelope][US1]") {
    constexpr float kAttackMs = 10.0f;
    constexpr float kTestInput = 1.0f;

    // Test at different sample rates
    const std::array<double, 3> sampleRates = {44100.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        EnvelopeFollower env;
        env.prepare(sr, 512);
        env.setMode(DetectionMode::Amplitude);
        env.setAttackTime(kAttackMs);
        env.setReleaseTime(1000.0f);

        const size_t attackSamples = msToSamples(kAttackMs, sr);

        for (size_t i = 0; i < attackSamples; ++i) {
            env.processSample(kTestInput);
        }

        // Should reach ~63% regardless of sample rate
        float envelope = env.getCurrentValue();
        CAPTURE(sr);
        REQUIRE(envelope >= 0.55f);
        REQUIRE(envelope <= 0.75f);
    }
}

TEST_CASE("Envelope settles to zero within 10x release time (SC-006)", "[envelope][US1]") {
    constexpr double kSampleRate = 44100.0;
    constexpr float kReleaseMs = 100.0f;

    EnvelopeFollower env;
    env.prepare(kSampleRate, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(0.1f);
    env.setReleaseTime(kReleaseMs);

    // Build up envelope
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }
    REQUIRE(env.getCurrentValue() > 0.9f);

    // Feed silence for 10x release time
    const size_t decaySamples = msToSamples(kReleaseMs * 10.0f, kSampleRate);
    for (size_t i = 0; i < decaySamples; ++i) {
        env.processSample(0.0f);
    }

    // Should be essentially zero
    REQUIRE(env.getCurrentValue() < 0.001f);
}

// =============================================================================
// Phase 4: User Story 2 - RMS Level Detection
// =============================================================================

TEST_CASE("RMS mode with 0dB sine wave outputs ~0.707 (SC-002)", "[envelope][US2]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100;  // 1 second

    EnvelopeFollower env;
    env.prepare(kSampleRate, kBlockSize);
    env.setMode(DetectionMode::RMS);
    env.setAttackTime(10.0f);
    env.setReleaseTime(100.0f);

    // Generate 1kHz sine wave at 0dBFS (amplitude = 1.0)
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 1000.0f, static_cast<float>(kSampleRate), 1.0f);

    // Process entire buffer
    for (size_t i = 0; i < kBlockSize; ++i) {
        env.processSample(buffer[i]);
    }

    // RMS of sine = peak / sqrt(2) = 1.0 / 1.414 = 0.707
    float rmsValue = env.getCurrentValue();
    REQUIRE(rmsValue == Approx(0.707f).margin(0.007f));  // Within 1%
}

TEST_CASE("RMS mode with 0dB square wave outputs ~1.0", "[envelope][US2]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100;  // 1 second

    EnvelopeFollower env;
    env.prepare(kSampleRate, kBlockSize);
    env.setMode(DetectionMode::RMS);
    env.setAttackTime(10.0f);
    env.setReleaseTime(100.0f);

    // Generate square wave at 0dBFS (amplitude = 1.0)
    std::array<float, kBlockSize> buffer;
    generateSquare(buffer.data(), kBlockSize, 100.0f, static_cast<float>(kSampleRate), 1.0f);

    // Process entire buffer
    for (size_t i = 0; i < kBlockSize; ++i) {
        env.processSample(buffer[i]);
    }

    // RMS of square wave = peak = 1.0
    float rmsValue = env.getCurrentValue();
    REQUIRE(rmsValue == Approx(1.0f).margin(0.02f));
}

TEST_CASE("RMS mode attack/release behavior", "[envelope][US2]") {
    constexpr double kSampleRate = 44100.0;
    constexpr float kAttackMs = 10.0f;

    EnvelopeFollower env;
    env.prepare(kSampleRate, 512);
    env.setMode(DetectionMode::RMS);
    env.setAttackTime(kAttackMs);
    env.setReleaseTime(100.0f);

    // Feed constant 1.0 (RMS = 1.0)
    const size_t attackSamples = msToSamples(kAttackMs, kSampleRate);
    for (size_t i = 0; i < attackSamples; ++i) {
        env.processSample(1.0f);
    }

    // Should rise toward 1.0
    REQUIRE(env.getCurrentValue() > 0.5f);
}

// =============================================================================
// Phase 5: User Story 3 - Peak Level Detection
// =============================================================================

TEST_CASE("Peak mode captures single-sample impulse (SC-003)", "[envelope][US3]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Peak);
    env.setAttackTime(0.1f);  // Minimum attack for instant capture
    env.setReleaseTime(100.0f);

    // Send impulse
    float result = env.processSample(1.0f);

    // Peak mode with minimum attack should capture immediately
    REQUIRE(result == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Peak mode release behavior", "[envelope][US3]") {
    constexpr double kSampleRate = 44100.0;
    constexpr float kReleaseMs = 100.0f;

    EnvelopeFollower env;
    env.prepare(kSampleRate, 512);
    env.setMode(DetectionMode::Peak);
    env.setAttackTime(0.1f);
    env.setReleaseTime(kReleaseMs);

    // Capture peak
    env.processSample(1.0f);
    REQUIRE(env.getCurrentValue() > 0.99f);

    // Feed silence - should decay
    const size_t releaseSamples = msToSamples(kReleaseMs, kSampleRate);
    for (size_t i = 0; i < releaseSamples; ++i) {
        env.processSample(0.0f);
    }

    // Should have decayed to ~37%
    float envelope = env.getCurrentValue();
    REQUIRE(envelope >= 0.30f);
    REQUIRE(envelope <= 0.45f);
}

TEST_CASE("Peak mode captures all transients (output >= input magnitude)", "[envelope][US3]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Peak);
    env.setAttackTime(0.1f);
    env.setReleaseTime(10.0f);  // Short release to test multiple peaks

    // Send increasing peaks
    const std::array<float, 5> peaks = {0.2f, 0.5f, 0.8f, 1.0f, 0.6f};

    for (float peak : peaks) {
        float result = env.processSample(peak);
        // Output should always capture the peak
        REQUIRE(result >= peak - 0.01f);

        // Add some silence between peaks
        for (int j = 0; j < 100; ++j) {
            env.processSample(0.0f);
        }
    }
}

TEST_CASE("Peak mode with configurable attack time", "[envelope][US3]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Peak);
    env.setAttackTime(10.0f);  // Non-instant attack
    env.setReleaseTime(100.0f);

    // With longer attack, peak won't be captured instantly
    float result = env.processSample(1.0f);

    // Should start rising but not reach 1.0 immediately
    REQUIRE(result > 0.0f);
    REQUIRE(result < 0.5f);  // Not instant capture

    // After more samples, should get closer
    // With 10ms attack at 44100Hz, need ~7ms (309 samples) to reach 50%
    // Use 500 samples (~11ms) to comfortably exceed 50%
    for (int i = 0; i < 500; ++i) {
        env.processSample(1.0f);
    }
    REQUIRE(env.getCurrentValue() > 0.5f);
}

// =============================================================================
// Phase 6: User Story 4 - Smooth Parameter Changes
// =============================================================================

TEST_CASE("Attack time change produces no discontinuity (SC-008)", "[envelope][US4]") {
    constexpr size_t kBlockSize = 512;
    EnvelopeFollower env;
    env.prepare(44100.0, kBlockSize);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(10.0f);
    env.setReleaseTime(100.0f);

    // Build up some envelope
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(0.5f);
    }

    float beforeChange = env.getCurrentValue();

    // Change attack time
    env.setAttackTime(50.0f);

    // Process one more sample
    float afterChange = env.processSample(0.5f);

    // Discontinuity should be < 0.01
    float discontinuity = std::abs(afterChange - beforeChange);
    REQUIRE(discontinuity < 0.01f);
}

TEST_CASE("Release time change produces no discontinuity", "[envelope][US4]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(1.0f);
    env.setReleaseTime(100.0f);

    // Build up envelope
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }

    // Start release
    for (size_t i = 0; i < 100; ++i) {
        env.processSample(0.0f);
    }

    float beforeChange = env.getCurrentValue();

    // Change release time
    env.setReleaseTime(500.0f);

    // Process one more sample
    float afterChange = env.processSample(0.0f);

    // Should be smooth
    float discontinuity = std::abs(afterChange - beforeChange);
    REQUIRE(discontinuity < 0.01f);
}

TEST_CASE("Mode change (Amplitude to RMS) produces smooth transition", "[envelope][US4]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(1.0f);
    env.setReleaseTime(100.0f);

    // Build up envelope
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }

    float beforeChange = env.getCurrentValue();

    // Change mode
    env.setMode(DetectionMode::RMS);

    // Process one more sample
    float afterChange = env.processSample(1.0f);

    // Should transition smoothly (values may differ but no huge jump)
    float discontinuity = std::abs(afterChange - beforeChange);
    REQUIRE(discontinuity < 0.1f);  // Allow some difference due to algorithm change
}

TEST_CASE("Mode change (RMS to Peak) produces smooth transition", "[envelope][US4]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::RMS);
    env.setAttackTime(1.0f);
    env.setReleaseTime(100.0f);

    // Build up envelope
    for (size_t i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }

    float beforeChange = env.getCurrentValue();

    // Change mode
    env.setMode(DetectionMode::Peak);

    // Process one more sample
    float afterChange = env.processSample(1.0f);

    // Should not have huge discontinuity
    float discontinuity = std::abs(afterChange - beforeChange);
    REQUIRE(discontinuity < 0.1f);
}

// =============================================================================
// Phase 7: User Story 5 - Pre-filtering Option (Sidechain)
// =============================================================================

TEST_CASE("Sidechain filter enabled attenuates bass", "[envelope][US5]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100;

    // Test with sidechain disabled
    EnvelopeFollower envNoFilter;
    envNoFilter.prepare(kSampleRate, kBlockSize);
    envNoFilter.setMode(DetectionMode::Amplitude);
    envNoFilter.setAttackTime(10.0f);
    envNoFilter.setReleaseTime(100.0f);
    envNoFilter.setSidechainEnabled(false);

    // Test with sidechain enabled at 100Hz
    EnvelopeFollower envWithFilter;
    envWithFilter.prepare(kSampleRate, kBlockSize);
    envWithFilter.setMode(DetectionMode::Amplitude);
    envWithFilter.setAttackTime(10.0f);
    envWithFilter.setReleaseTime(100.0f);
    envWithFilter.setSidechainEnabled(true);
    envWithFilter.setSidechainCutoff(100.0f);

    // Generate 50Hz sine (below cutoff - should be attenuated)
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 50.0f, static_cast<float>(kSampleRate), 1.0f);

    // Process with both
    for (size_t i = 0; i < kBlockSize; ++i) {
        envNoFilter.processSample(buffer[i]);
        envWithFilter.processSample(buffer[i]);
    }

    // Filtered version should have lower envelope (bass attenuated)
    REQUIRE(envWithFilter.getCurrentValue() < envNoFilter.getCurrentValue());
}

TEST_CASE("Sidechain filter disabled passes all frequencies", "[envelope][US5]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 44100;

    EnvelopeFollower env;
    env.prepare(kSampleRate, kBlockSize);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(10.0f);
    env.setReleaseTime(100.0f);
    env.setSidechainEnabled(false);

    // Generate 50Hz sine
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 50.0f, static_cast<float>(kSampleRate), 1.0f);

    for (size_t i = 0; i < kBlockSize; ++i) {
        env.processSample(buffer[i]);
    }

    // Should track full amplitude
    REQUIRE(env.getCurrentValue() > 0.6f);
}

TEST_CASE("setSidechainCutoff clamps to valid range", "[envelope][US5]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);

    env.setSidechainCutoff(100.0f);
    REQUIRE(env.getSidechainCutoff() == Approx(100.0f));

    // Below minimum
    env.setSidechainCutoff(5.0f);
    REQUIRE(env.getSidechainCutoff() == Approx(EnvelopeFollower::kMinSidechainHz));

    // Above maximum
    env.setSidechainCutoff(1000.0f);
    REQUIRE(env.getSidechainCutoff() == Approx(EnvelopeFollower::kMaxSidechainHz));
}

TEST_CASE("setSidechainEnabled toggles filter", "[envelope][US5]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);

    REQUIRE_FALSE(env.isSidechainEnabled());

    env.setSidechainEnabled(true);
    REQUIRE(env.isSidechainEnabled());

    env.setSidechainEnabled(false);
    REQUIRE_FALSE(env.isSidechainEnabled());
}

TEST_CASE("getLatency returns appropriate value (SC-005)", "[envelope][US5]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);

    // Biquad is zero-latency
    REQUIRE(env.getLatency() == 0);

    env.setSidechainEnabled(true);
    REQUIRE(env.getLatency() == 0);  // Still zero with Biquad filter
}

// =============================================================================
// Phase 8: Edge Cases
// =============================================================================

TEST_CASE("NaN input handling", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);

    // Build up some envelope first
    for (int i = 0; i < 100; ++i) {
        env.processSample(0.5f);
    }
    float beforeNaN = env.getCurrentValue();
    REQUIRE(isValidFloat(beforeNaN));

    // Process NaN
    float nanValue = std::numeric_limits<float>::quiet_NaN();
    float result = env.processSample(nanValue);

    // Output should remain valid
    REQUIRE(isValidFloat(result));
    REQUIRE(isValidFloat(env.getCurrentValue()));
}

TEST_CASE("Inf input handling", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);

    // Process +Inf
    float infValue = std::numeric_limits<float>::infinity();
    float result = env.processSample(infValue);

    // Output should be clamped/valid
    REQUIRE(isValidFloat(result));
    REQUIRE(std::abs(result) < 1e10f);  // Bounded
}

TEST_CASE("Denormalized numbers flushed to zero (SC-007)", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(0.1f);
    env.setReleaseTime(100.0f);

    // Build up envelope
    for (int i = 0; i < 100; ++i) {
        env.processSample(1.0f);
    }

    // Let it decay for a long time
    for (int i = 0; i < 100000; ++i) {
        env.processSample(0.0f);
    }

    float finalValue = env.getCurrentValue();

    // Should be zero or very close, not denormalized
    // Denormalized floats are between 0 and ~1e-38
    bool isZeroOrNormal = (finalValue == 0.0f) || (std::abs(finalValue) > 1e-30f);
    REQUIRE(isZeroOrNormal);
}

TEST_CASE("Silent input decays to zero and remains stable", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setReleaseTime(10.0f);

    // Process silence for a while
    for (int i = 0; i < 10000; ++i) {
        env.processSample(0.0f);
    }

    float value1 = env.getCurrentValue();

    // Continue processing silence
    for (int i = 0; i < 1000; ++i) {
        env.processSample(0.0f);
    }

    float value2 = env.getCurrentValue();

    // Should be at or near zero and stable
    REQUIRE(value1 < 0.001f);
    REQUIRE(value2 < 0.001f);
    REQUIRE(std::abs(value1 - value2) < 0.0001f);
}

TEST_CASE("Extreme attack time (minimum) behavior", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(EnvelopeFollower::kMinAttackMs);
    env.setReleaseTime(1000.0f);

    // Should rise very quickly
    float result = env.processSample(1.0f);
    REQUIRE(result > 0.1f);  // Significant rise in one sample
}

TEST_CASE("Extreme release time (maximum) behavior", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(0.1f);
    env.setReleaseTime(EnvelopeFollower::kMaxReleaseMs);  // 5000ms

    // Build up envelope
    for (int i = 0; i < 1000; ++i) {
        env.processSample(1.0f);
    }
    float peakValue = env.getCurrentValue();

    // With very long release, decay should be very slow
    for (int i = 0; i < 1000; ++i) {
        env.processSample(0.0f);
    }

    // Should still be high
    REQUIRE(env.getCurrentValue() > peakValue * 0.95f);
}

TEST_CASE("Output range with >0dBFS input (FR-011)", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(0.1f);

    // Process signal >1.0
    for (int i = 0; i < 100; ++i) {
        env.processSample(2.0f);
    }

    // Output should exceed 1.0
    REQUIRE(env.getCurrentValue() > 1.0f);

    // But should be proportional
    REQUIRE(env.getCurrentValue() < 2.5f);
}

TEST_CASE("Output stability (FR-012): no oscillation after step response", "[envelope][edge]") {
    EnvelopeFollower env;
    env.prepare(44100.0, 512);
    env.setMode(DetectionMode::Amplitude);
    env.setAttackTime(10.0f);
    env.setReleaseTime(100.0f);

    // Step up
    for (int i = 0; i < 5000; ++i) {
        env.processSample(1.0f);
    }

    // Collect samples during release to check for monotonic decay
    std::array<float, 1000> releaseSamples;
    for (size_t i = 0; i < releaseSamples.size(); ++i) {
        releaseSamples[i] = env.processSample(0.0f);
    }

    // Check monotonic decay (each sample <= previous)
    for (size_t i = 1; i < releaseSamples.size(); ++i) {
        REQUIRE(releaseSamples[i] <= releaseSamples[i - 1] + 0.0001f);  // Small tolerance for floating point
    }
}
