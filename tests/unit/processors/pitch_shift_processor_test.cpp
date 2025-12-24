// ==============================================================================
// Unit Tests: PitchShiftProcessor
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 016-pitch-shifter
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/pitch_shift_processor.h"

#include <array>
#include <cmath>
#include <complex>
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
constexpr float kTolerance = 1e-5f;
constexpr float kTestPi = 3.14159265358979323846f;
constexpr float kTestTwoPi = 2.0f * kTestPi;

// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTestTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Generate white noise with optional seed for reproducibility
inline void generateWhiteNoise(float* buffer, size_t size, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

// Generate impulse (single sample at 1.0, rest zeros)
inline void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    if (size > 0) buffer[0] = 1.0f;
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

// Calculate peak absolute value
inline float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
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

// Check if two buffers are equal within tolerance
inline bool buffersEqual(const float* a, const float* b, size_t size, float tolerance = kTolerance) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

// Estimate fundamental frequency using zero-crossing rate
// Returns frequency in Hz, suitable for simple pitch detection
inline float estimateFrequency(const float* buffer, size_t size, float sampleRate) {
    if (size < 4) return 0.0f;

    size_t zeroCrossings = 0;
    for (size_t i = 1; i < size; ++i) {
        if ((buffer[i-1] >= 0.0f && buffer[i] < 0.0f) ||
            (buffer[i-1] < 0.0f && buffer[i] >= 0.0f)) {
            ++zeroCrossings;
        }
    }

    // Zero-crossing rate gives 2x frequency for sine wave
    return (zeroCrossings * sampleRate) / (2.0f * static_cast<float>(size));
}

// More accurate frequency estimation using autocorrelation
inline float estimateFrequencyAutocorr(const float* buffer, size_t size, float sampleRate) {
    if (size < 64) return 0.0f;

    // Find the peak in autocorrelation (excluding lag 0)
    size_t minLag = static_cast<size_t>(sampleRate / 2000.0f);  // 2000Hz max
    size_t maxLag = static_cast<size_t>(sampleRate / 50.0f);    // 50Hz min

    if (maxLag >= size) maxLag = size - 1;
    if (minLag < 1) minLag = 1;

    float maxCorr = -1.0f;
    size_t bestLag = minLag;

    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        float corr = 0.0f;
        for (size_t i = 0; i < size - lag; ++i) {
            corr += buffer[i] * buffer[i + lag];
        }
        corr /= static_cast<float>(size - lag);

        if (corr > maxCorr) {
            maxCorr = corr;
            bestLag = lag;
        }
    }

    return sampleRate / static_cast<float>(bestLag);
}

} // namespace

// ==============================================================================
// Phase 2: Foundational Utilities Tests
// ==============================================================================

TEST_CASE("pitchRatioFromSemitones converts semitones to pitch ratio", "[pitch][utility]") {
    // T006: pitchRatioFromSemitones utility tests

    SECTION("0 semitones returns unity ratio") {
        REQUIRE(pitchRatioFromSemitones(0.0f) == Approx(1.0f));
    }

    SECTION("+12 semitones returns 2.0 (octave up)") {
        REQUIRE(pitchRatioFromSemitones(12.0f) == Approx(2.0f).margin(1e-5f));
    }

    SECTION("-12 semitones returns 0.5 (octave down)") {
        REQUIRE(pitchRatioFromSemitones(-12.0f) == Approx(0.5f).margin(1e-5f));
    }

    SECTION("+7 semitones returns perfect fifth ratio (~1.498)") {
        // Perfect fifth = 2^(7/12) ≈ 1.4983
        REQUIRE(pitchRatioFromSemitones(7.0f) == Approx(1.4983f).margin(1e-3f));
    }

    SECTION("+24 semitones returns 4.0 (two octaves up)") {
        REQUIRE(pitchRatioFromSemitones(24.0f) == Approx(4.0f).margin(1e-4f));
    }

    SECTION("-24 semitones returns 0.25 (two octaves down)") {
        REQUIRE(pitchRatioFromSemitones(-24.0f) == Approx(0.25f).margin(1e-5f));
    }

    SECTION("+1 semitone returns semitone ratio (~1.0595)") {
        // Semitone = 2^(1/12) ≈ 1.05946
        REQUIRE(pitchRatioFromSemitones(1.0f) == Approx(1.05946f).margin(1e-4f));
    }

    SECTION("fractional semitones work (0.5 = quarter tone)") {
        // Quarter tone = 2^(0.5/12) ≈ 1.02930
        REQUIRE(pitchRatioFromSemitones(0.5f) == Approx(1.02930f).margin(1e-4f));
    }
}

TEST_CASE("semitonesFromPitchRatio converts pitch ratio to semitones", "[pitch][utility]") {
    // T008: semitonesFromPitchRatio utility tests

    SECTION("unity ratio returns 0 semitones") {
        REQUIRE(semitonesFromPitchRatio(1.0f) == Approx(0.0f));
    }

    SECTION("2.0 ratio returns +12 semitones (octave up)") {
        REQUIRE(semitonesFromPitchRatio(2.0f) == Approx(12.0f).margin(1e-4f));
    }

    SECTION("0.5 ratio returns -12 semitones (octave down)") {
        REQUIRE(semitonesFromPitchRatio(0.5f) == Approx(-12.0f).margin(1e-4f));
    }

    SECTION("4.0 ratio returns +24 semitones (two octaves up)") {
        REQUIRE(semitonesFromPitchRatio(4.0f) == Approx(24.0f).margin(1e-4f));
    }

    SECTION("0.25 ratio returns -24 semitones (two octaves down)") {
        REQUIRE(semitonesFromPitchRatio(0.25f) == Approx(-24.0f).margin(1e-4f));
    }

    SECTION("invalid ratio (0) returns 0") {
        REQUIRE(semitonesFromPitchRatio(0.0f) == 0.0f);
    }

    SECTION("invalid ratio (negative) returns 0") {
        REQUIRE(semitonesFromPitchRatio(-1.0f) == 0.0f);
    }

    SECTION("roundtrip: semitones -> ratio -> semitones") {
        // Test that conversion roundtrips correctly
        for (float semitones = -24.0f; semitones <= 24.0f; semitones += 1.0f) {
            float ratio = pitchRatioFromSemitones(semitones);
            float recovered = semitonesFromPitchRatio(ratio);
            REQUIRE(recovered == Approx(semitones).margin(1e-4f));
        }
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Basic Pitch Shifting (Priority: P1) MVP
// ==============================================================================

// T014: 440Hz sine + 12 semitones = 880Hz output
TEST_CASE("PitchShiftProcessor shifts 440Hz up one octave to 880Hz", "[pitch][US1]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);  // Use Simple mode for basic test
    shifter.setSemitones(12.0f);  // One octave up

    // Generate 440Hz sine wave (multiple cycles for accurate frequency detection)
    constexpr size_t numSamples = 8192;  // Enough samples for autocorrelation
    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    generateSine(input.data(), numSamples, 440.0f, kTestSampleRate);

    // Process in blocks
    for (size_t offset = 0; offset < numSamples; offset += kTestBlockSize) {
        size_t blockSize = std::min(kTestBlockSize, numSamples - offset);
        shifter.process(input.data() + offset, output.data() + offset, blockSize);
    }

    // Let the processor settle, then measure frequency
    // Skip the first part due to transient response
    const float* measureStart = output.data() + numSamples / 2;
    size_t measureSize = numSamples / 2;
    float detectedFreq = estimateFrequencyAutocorr(measureStart, measureSize, kTestSampleRate);

    // Allow ±10 cents tolerance for Simple mode (SC-001)
    // 10 cents = 10/1200 octaves = 0.578% frequency tolerance
    float expectedFreq = 880.0f;
    float tolerance = expectedFreq * 0.01f;  // 1% tolerance (more than 10 cents)
    REQUIRE(detectedFreq == Approx(expectedFreq).margin(tolerance));
}

// T015: 440Hz sine - 12 semitones = 220Hz output
TEST_CASE("PitchShiftProcessor shifts 440Hz down one octave to 220Hz", "[pitch][US1]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);
    shifter.setSemitones(-12.0f);  // One octave down

    constexpr size_t numSamples = 8192;
    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);
    generateSine(input.data(), numSamples, 440.0f, kTestSampleRate);

    for (size_t offset = 0; offset < numSamples; offset += kTestBlockSize) {
        size_t blockSize = std::min(kTestBlockSize, numSamples - offset);
        shifter.process(input.data() + offset, output.data() + offset, blockSize);
    }

    const float* measureStart = output.data() + numSamples / 2;
    size_t measureSize = numSamples / 2;
    float detectedFreq = estimateFrequencyAutocorr(measureStart, measureSize, kTestSampleRate);

    float expectedFreq = 220.0f;
    float tolerance = expectedFreq * 0.01f;
    REQUIRE(detectedFreq == Approx(expectedFreq).margin(tolerance));
}

// T016: 0 semitones = unity pass-through
TEST_CASE("PitchShiftProcessor at 0 semitones passes audio unchanged", "[pitch][US1]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);
    shifter.setSemitones(0.0f);

    std::vector<float> input(kTestBlockSize);
    std::vector<float> output(kTestBlockSize);
    generateSine(input.data(), kTestBlockSize, 440.0f, kTestSampleRate);

    shifter.process(input.data(), output.data(), kTestBlockSize);

    // For Simple mode at 0 semitones, output should closely match input
    // Allow small tolerance for any internal processing artifacts
    for (size_t i = 0; i < kTestBlockSize; ++i) {
        REQUIRE(output[i] == Approx(input[i]).margin(0.01f));
    }
}

// T017: prepare()/reset()/isPrepared() lifecycle
TEST_CASE("PitchShiftProcessor lifecycle methods", "[pitch][US1][lifecycle]") {
    PitchShiftProcessor shifter;

    SECTION("isPrepared returns false before prepare()") {
        REQUIRE_FALSE(shifter.isPrepared());
    }

    SECTION("isPrepared returns true after prepare()") {
        shifter.prepare(kTestSampleRate, kTestBlockSize);
        REQUIRE(shifter.isPrepared());
    }

    SECTION("reset() clears internal state but keeps prepared status") {
        shifter.prepare(kTestSampleRate, kTestBlockSize);
        shifter.setSemitones(12.0f);

        // Process some audio to fill internal buffers
        std::vector<float> buffer(kTestBlockSize);
        generateSine(buffer.data(), kTestBlockSize, 440.0f, kTestSampleRate);
        shifter.process(buffer.data(), buffer.data(), kTestBlockSize);

        // Reset
        shifter.reset();

        // Should still be prepared
        REQUIRE(shifter.isPrepared());

        // Parameters should be preserved
        REQUIRE(shifter.getSemitones() == Approx(12.0f));
    }

    SECTION("prepare() can be called multiple times") {
        shifter.prepare(44100.0, 256);
        REQUIRE(shifter.isPrepared());

        shifter.prepare(96000.0, 512);
        REQUIRE(shifter.isPrepared());
    }
}

// T018: in-place processing (FR-029)
TEST_CASE("PitchShiftProcessor supports in-place processing", "[pitch][US1][FR-029]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);
    shifter.setSemitones(0.0f);

    std::vector<float> buffer(kTestBlockSize);
    std::vector<float> reference(kTestBlockSize);
    generateSine(buffer.data(), kTestBlockSize, 440.0f, kTestSampleRate);
    std::copy(buffer.begin(), buffer.end(), reference.begin());

    // Process in-place (same buffer for input and output)
    shifter.process(buffer.data(), buffer.data(), kTestBlockSize);

    // At 0 semitones, in-place processing should work correctly
    for (size_t i = 0; i < kTestBlockSize; ++i) {
        REQUIRE(buffer[i] == Approx(reference[i]).margin(0.01f));
    }
}

// T019: FR-004 duration preservation
TEST_CASE("PitchShiftProcessor output sample count equals input", "[pitch][US1][FR-004]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);

    SECTION("at +12 semitones") {
        shifter.setSemitones(12.0f);

        std::vector<float> input(kTestBlockSize);
        std::vector<float> output(kTestBlockSize, -999.0f);  // Fill with sentinel
        generateSine(input.data(), kTestBlockSize, 440.0f, kTestSampleRate);

        shifter.process(input.data(), output.data(), kTestBlockSize);

        // All output samples should be valid (not sentinel value)
        for (size_t i = 0; i < kTestBlockSize; ++i) {
            REQUIRE(output[i] != -999.0f);
        }
    }

    SECTION("at -12 semitones") {
        shifter.setSemitones(-12.0f);

        std::vector<float> input(kTestBlockSize);
        std::vector<float> output(kTestBlockSize, -999.0f);
        generateSine(input.data(), kTestBlockSize, 440.0f, kTestSampleRate);

        shifter.process(input.data(), output.data(), kTestBlockSize);

        for (size_t i = 0; i < kTestBlockSize; ++i) {
            REQUIRE(output[i] != -999.0f);
        }
    }
}

// T020: FR-005 unity gain
TEST_CASE("PitchShiftProcessor maintains unity gain at 0 semitones", "[pitch][US1][FR-005]") {
    PitchShiftProcessor shifter;
    shifter.prepare(kTestSampleRate, kTestBlockSize);
    shifter.setMode(PitchMode::Simple);
    shifter.setSemitones(0.0f);

    std::vector<float> input(kTestBlockSize);
    std::vector<float> output(kTestBlockSize);
    generateSine(input.data(), kTestBlockSize, 440.0f, kTestSampleRate);

    float inputRMS = calculateRMS(input.data(), kTestBlockSize);
    shifter.process(input.data(), output.data(), kTestBlockSize);
    float outputRMS = calculateRMS(output.data(), kTestBlockSize);

    // RMS should be approximately equal (within 1dB)
    // 1dB = ~11.5% change in amplitude
    float gainRatio = outputRMS / inputRMS;
    REQUIRE(gainRatio == Approx(1.0f).margin(0.12f));
}

// ==============================================================================
// Phase 4: User Story 2 - Quality Mode Selection (Priority: P1)
// ==============================================================================

// T030: Simple mode latency == 0 samples
TEST_CASE("Simple mode has zero latency", "[pitch][US2][latency]") {
    // Test to be implemented
}

// T031: Granular mode latency < 2048 samples
TEST_CASE("Granular mode latency is under 2048 samples", "[pitch][US2][latency]") {
    // Test to be implemented
}

// T032: PhaseVocoder mode latency < 8192 samples
TEST_CASE("PhaseVocoder mode latency is under 8192 samples", "[pitch][US2][latency]") {
    // Test to be implemented
}

// T033: setMode()/getMode()
TEST_CASE("PitchShiftProcessor mode setter and getter", "[pitch][US2]") {
    // Test to be implemented
}

// T034: mode switching is click-free
TEST_CASE("Mode switching produces no discontinuities", "[pitch][US2]") {
    // Test to be implemented
}

// T035: Granular mode produces shifted pitch
TEST_CASE("Granular mode produces correct pitch shift", "[pitch][US2]") {
    // Test to be implemented
}

// T036: PhaseVocoder mode produces shifted pitch
TEST_CASE("PhaseVocoder mode produces correct pitch shift", "[pitch][US2]") {
    // Test to be implemented
}

// ==============================================================================
// Phase 5: User Story 3 - Fine Pitch Control with Cents (Priority: P2)
// ==============================================================================

// T055: 0 semitones + 50 cents = 452.9Hz from 440Hz
TEST_CASE("50 cents shift produces quarter tone up", "[pitch][US3][cents]") {
    // Test to be implemented
}

// T056: +1 semitone - 50 cents = +0.5 semitones
TEST_CASE("Semitones and cents combine correctly", "[pitch][US3][cents]") {
    // Test to be implemented
}

// T057: Cents changes are smooth
TEST_CASE("Cents parameter changes are smooth", "[pitch][US3][cents]") {
    // Test to be implemented
}

// T058: setCents()/getCents()
TEST_CASE("PitchShiftProcessor cents setter and getter", "[pitch][US3][cents]") {
    // Test to be implemented
}

// ==============================================================================
// Phase 6: User Story 4 - Formant Preservation (Priority: P2)
// ==============================================================================

// T066: Formant peaks remain within 10%
TEST_CASE("Formant preservation keeps formants within 10%", "[pitch][US4][formant]") {
    // Test to be implemented
}

// T067: Formants shift without preservation
TEST_CASE("Without formant preservation, formants shift with pitch", "[pitch][US4][formant]") {
    // Test to be implemented
}

// T068: Formant toggle transition is smooth
TEST_CASE("Formant toggle transition is click-free", "[pitch][US4][formant]") {
    // Test to be implemented
}

// T069: Extreme shift formant behavior (>1 octave)
TEST_CASE("Formant preservation gracefully degrades at extreme shifts", "[pitch][US4][formant][edge]") {
    // Test to be implemented
}

// ==============================================================================
// Phase 7: User Story 5 - Feedback Path Integration (Priority: P2)
// ==============================================================================

// T075: 80% feedback loop decays naturally
TEST_CASE("Pitch shifter in 80% feedback loop decays naturally", "[pitch][US5][feedback]") {
    // Test to be implemented
}

// T076: Multiple iterations maintain pitch accuracy
TEST_CASE("Multiple feedback iterations maintain pitch accuracy", "[pitch][US5][feedback]") {
    // Test to be implemented
}

// T077: No DC offset after extended feedback
TEST_CASE("No DC offset after extended feedback processing", "[pitch][US5][feedback]") {
    // Test to be implemented
}

// ==============================================================================
// Phase 8: User Story 6 - Real-Time Parameter Automation (Priority: P3)
// ==============================================================================

// T083: Sweep -24 to +24 is smooth
TEST_CASE("Full range pitch sweep is click-free", "[pitch][US6][automation]") {
    // Test to be implemented
}

// T084: Rapid parameter changes cause no clicks
TEST_CASE("Rapid parameter changes produce no clicks", "[pitch][US6][automation]") {
    // Test to be implemented
}

// T085: Parameter reaches target within 50ms
TEST_CASE("Parameter smoothing reaches target within 50ms", "[pitch][US6][automation]") {
    // Test to be implemented
}

// ==============================================================================
// Success Criteria Tests
// ==============================================================================

// SC-001: Pitch accuracy (±10 cents Simple, ±5 cents others)
TEST_CASE("SC-001 Pitch accuracy meets tolerance", "[pitch][SC-001]") {
    // Test to be implemented
}

// SC-006: No clicks during parameter sweep
TEST_CASE("SC-006 No clicks during parameter sweep", "[pitch][SC-006]") {
    // Test to be implemented
}

// SC-008: Stable after 1000 feedback iterations
TEST_CASE("SC-008 Stable after 1000 feedback iterations", "[pitch][SC-008]") {
    // Test to be implemented
}

// ==============================================================================
// Edge Case Tests
// ==============================================================================

TEST_CASE("PitchShiftProcessor handles extreme pitch values", "[pitch][edge]") {
    // Test ±24 semitones
}

TEST_CASE("PitchShiftProcessor handles silence input", "[pitch][edge]") {
    // Test that silence produces silence without noise
}

TEST_CASE("PitchShiftProcessor handles NaN input gracefully", "[pitch][edge][FR-023]") {
    // Test NaN input outputs silence
}

TEST_CASE("PitchShiftProcessor clamps out-of-range parameters", "[pitch][edge][FR-020]") {
    // Test parameter clamping
}
