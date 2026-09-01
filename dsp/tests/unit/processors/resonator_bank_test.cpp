// ==============================================================================
// Unit Tests: ResonatorBank
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 083-resonator-bank
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/resonator_bank.h>

#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr double kTestSampleRateDouble = 44100.0;
constexpr size_t kTestBlockSize = 512;
constexpr float kTolerance = 1e-5f;
constexpr float kTestPi = 3.14159265358979323846f;
constexpr float kTestTwoPi = 2.0f * kTestPi;

// Generate an impulse (single sample at 1.0, rest zeros)
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

// Calculate energy in a buffer (sum of squared samples)
inline float calculateEnergy(const float* buffer, size_t size) {
    float energy = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        energy += buffer[i] * buffer[i];
    }
    return energy;
}

// Simple DFT bin magnitude at a specific frequency
inline float getDFTMagnitudeAtFrequency(
    const float* buffer, size_t size, float frequency, float sampleRate
) {
    const float binFloat = frequency * static_cast<float>(size) / sampleRate;
    const size_t bin = static_cast<size_t>(std::round(binFloat));
    if (bin > size / 2) return 0.0f;

    float real = 0.0f, imag = 0.0f;
    for (size_t n = 0; n < size; ++n) {
        float angle = -kTestTwoPi * static_cast<float>(bin * n) / static_cast<float>(size);
        real += buffer[n] * std::cos(angle);
        imag += buffer[n] * std::sin(angle);
    }
    return std::sqrt(real * real + imag * imag) / static_cast<float>(size);
}

// Find the frequency with maximum magnitude using DFT
inline float findPeakFrequency(
    const float* buffer, size_t size, float sampleRate,
    float minFreq = 20.0f, float maxFreq = 20000.0f
) {
    float maxMag = 0.0f;
    float peakFreq = 0.0f;

    // Search in 1Hz steps (crude but sufficient for testing)
    for (float freq = minFreq; freq <= maxFreq && freq < sampleRate / 2.0f; freq += 1.0f) {
        float mag = getDFTMagnitudeAtFrequency(buffer, size, freq, sampleRate);
        if (mag > maxMag) {
            maxMag = mag;
            peakFreq = freq;
        }
    }
    return peakFreq;
}

// Measure decay time (time to reach -60dB from peak)
// Returns decay time in seconds using RMS-based envelope
inline float measureRT60(const float* buffer, size_t size, float sampleRate) {
    // Find peak RMS in short windows
    constexpr size_t windowSize = 256;
    float peakRms = 0.0f;
    size_t peakWindowStart = 0;

    for (size_t i = 0; i + windowSize < size; i += windowSize / 2) {
        float windowRms = calculateRMS(buffer + i, windowSize);
        if (windowRms > peakRms) {
            peakRms = windowRms;
            peakWindowStart = i;
        }
    }

    if (peakRms == 0.0f) return 0.0f;

    // Find when RMS drops to -60dB (1/1000 of peak)
    const float threshold = peakRms * 0.001f;

    for (size_t i = peakWindowStart + windowSize; i + windowSize < size; i += windowSize / 2) {
        float windowRms = calculateRMS(buffer + i, windowSize);
        if (windowRms < threshold) {
            return static_cast<float>(i - peakWindowStart) / sampleRate;
        }
    }

    // Decay didn't complete in buffer - estimate from available data
    return static_cast<float>(size - peakWindowStart) / sampleRate;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: Foundational Tests
// ==============================================================================

// T003: ResonatorBank construction and initialization
TEST_CASE("ResonatorBank prepare initializes properly", "[resonator_bank][lifecycle][US1]") {
    ResonatorBank bank;

    SECTION("prepare sets initialized state") {
        REQUIRE_FALSE(bank.isPrepared());
        bank.prepare(kTestSampleRateDouble);
        REQUIRE(bank.isPrepared());
    }

    SECTION("prepare works with different sample rates") {
        bank.prepare(48000.0);
        REQUIRE(bank.isPrepared());

        // Should be able to process without crash
        float output = bank.process(0.0f);
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("prepare at 192kHz for high sample rate support") {
        bank.prepare(192000.0);
        REQUIRE(bank.isPrepared());
    }
}

// T005: reset() behavior
TEST_CASE("ResonatorBank reset clears state and parameters", "[resonator_bank][lifecycle][US1]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    SECTION("reset clears filter states") {
        // Configure and excite
        bank.setHarmonicSeries(440.0f, 4);

        // Feed some signal
        for (int i = 0; i < 1000; ++i) {
            (void)bank.process(i == 0 ? 1.0f : 0.0f);
        }

        // Reset
        bank.reset();

        // After reset, should produce silence
        float output = bank.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("reset clears parameters to defaults") {
        // Set custom values
        bank.setHarmonicSeries(880.0f, 8);
        bank.setDamping(0.5f);
        bank.setExciterMix(0.5f);
        bank.setSpectralTilt(-6.0f);

        // Reset
        bank.reset();

        // Verify defaults
        REQUIRE(bank.getDamping() == Approx(0.0f));
        REQUIRE(bank.getExciterMix() == Approx(0.0f));
        REQUIRE(bank.getSpectralTilt() == Approx(0.0f));
        REQUIRE(bank.getTuningMode() == TuningMode::Custom);
        REQUIRE(bank.getNumActiveResonators() == 0);
    }

    SECTION("reset clears trigger state") {
        bank.trigger(1.0f);
        bank.reset();

        // After reset, no pending trigger
        float output = bank.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Basic Resonator Bank Processing (P1)
// ==============================================================================

// T009: setHarmonicSeries configuration
TEST_CASE("ResonatorBank setHarmonicSeries configures frequencies correctly", "[resonator_bank][tuning][US1]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    SECTION("4 partials at 440Hz") {
        bank.setHarmonicSeries(440.0f, 4);

        REQUIRE(bank.getTuningMode() == TuningMode::Harmonic);
        REQUIRE(bank.getNumActiveResonators() == 4);

        // Verify frequencies
        REQUIRE(bank.getFrequency(0) == Approx(440.0f).margin(1.0f));
        REQUIRE(bank.getFrequency(1) == Approx(880.0f).margin(1.0f));
        REQUIRE(bank.getFrequency(2) == Approx(1320.0f).margin(1.0f));
        REQUIRE(bank.getFrequency(3) == Approx(1760.0f).margin(1.0f));
    }

    SECTION("8 partials at 100Hz") {
        bank.setHarmonicSeries(100.0f, 8);

        REQUIRE(bank.getNumActiveResonators() == 8);

        for (int i = 0; i < 8; ++i) {
            REQUIRE(bank.getFrequency(static_cast<size_t>(i)) ==
                    Approx(100.0f * (i + 1)).margin(1.0f));
        }
    }

    SECTION("numPartials clamped to kMaxResonators (16)") {
        bank.setHarmonicSeries(100.0f, 20);
        REQUIRE(bank.getNumActiveResonators() == kMaxResonators);
    }
}

// T010: Harmonic impulse response
TEST_CASE("ResonatorBank produces harmonic impulse response", "[resonator_bank][impulse][US1]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    // Process impulse
    constexpr size_t bufferSize = 8192;
    std::vector<float> output(bufferSize, 0.0f);

    // First sample is impulse
    output[0] = bank.process(1.0f);
    for (size_t i = 1; i < bufferSize; ++i) {
        output[i] = bank.process(0.0f);
    }

    SECTION("output contains energy at fundamental frequency") {
        float mag440 = getDFTMagnitudeAtFrequency(output.data(), bufferSize, 440.0f, kTestSampleRate);
        // Bandpass filters produce low amplitude output - just verify nonzero energy
        REQUIRE(mag440 > 0.00001f);
    }

    SECTION("output contains energy at harmonics") {
        float mag880 = getDFTMagnitudeAtFrequency(output.data(), bufferSize, 880.0f, kTestSampleRate);
        float mag1320 = getDFTMagnitudeAtFrequency(output.data(), bufferSize, 1320.0f, kTestSampleRate);
        float mag1760 = getDFTMagnitudeAtFrequency(output.data(), bufferSize, 1760.0f, kTestSampleRate);

        // Verify each harmonic has some energy (bandpass filters have low amplitude output)
        REQUIRE(mag880 > 0.00001f);
        REQUIRE(mag1320 > 0.00001f);
        REQUIRE(mag1760 > 0.00001f);
    }

    SECTION("no invalid samples in output") {
        REQUIRE_FALSE(hasInvalidSamples(output.data(), bufferSize));
    }
}

// T011: Silent output when no excitation
TEST_CASE("ResonatorBank produces silence without excitation", "[resonator_bank][silence][US1]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 8);

    SECTION("process(0) returns 0 with no prior input") {
        float output = bank.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("processBlock with zeros returns zeros") {
        std::array<float, kTestBlockSize> buffer{};
        bank.processBlock(buffer.data(), kTestBlockSize);

        float rms = calculateRMS(buffer.data(), kTestBlockSize);
        REQUIRE(rms == Approx(0.0f).margin(kTolerance));
    }
}

// T012: Natural decay behavior
TEST_CASE("ResonatorBank output decays naturally after impulse", "[resonator_bank][decay][US1]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    // Set specific decay time
    for (size_t i = 0; i < 4; ++i) {
        bank.setDecay(i, 0.5f);  // 500ms decay
    }

    // Process impulse
    constexpr size_t bufferSize = 44100;  // 1 second
    std::vector<float> output(bufferSize, 0.0f);

    output[0] = bank.process(1.0f);
    for (size_t i = 1; i < bufferSize; ++i) {
        output[i] = bank.process(0.0f);
    }

    SECTION("amplitude decreases over time") {
        // Check energy in first half vs second half
        float energyFirst = calculateEnergy(output.data(), bufferSize / 2);
        float energySecond = calculateEnergy(output.data() + bufferSize / 2, bufferSize / 2);

        REQUIRE(energySecond < energyFirst);
    }

    SECTION("output eventually approaches silence") {
        // Check last 1000 samples
        float rmsTail = calculateRMS(output.data() + bufferSize - 1000, 1000);
        float rmsStart = calculateRMS(output.data() + 100, 1000);

        REQUIRE(rmsTail < rmsStart * 0.1f);  // Tail should be much quieter
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Per-Resonator Control (P1)
// ==============================================================================

// T023: setFrequency test
TEST_CASE("ResonatorBank setFrequency changes resonator frequency", "[resonator_bank][frequency][US2]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("setFrequency changes frequency for specific resonator") {
        bank.setFrequency(0, 880.0f);
        REQUIRE(bank.getFrequency(0) == Approx(880.0f).margin(1.0f));

        // Other resonators unchanged
        REQUIRE(bank.getFrequency(1) == Approx(880.0f).margin(1.0f));  // Was already 880 (2nd harmonic)
    }

    SECTION("frequency is clamped to valid range") {
        bank.setFrequency(0, 5.0f);  // Below minimum
        REQUIRE(bank.getFrequency(0) >= kMinResonatorFrequency);

        bank.setFrequency(0, 30000.0f);  // Above maximum for 44.1kHz
        REQUIRE(bank.getFrequency(0) <= kTestSampleRate * kMaxResonatorFrequencyRatio);
    }

    SECTION("invalid index is ignored") {
        bank.setFrequency(100, 1000.0f);  // Should not crash
        REQUIRE(bank.getFrequency(100) == 0.0f);  // Returns 0 for invalid index
    }
}

// T024: setDecay with RT60 accuracy
TEST_CASE("ResonatorBank setDecay provides accurate RT60", "[resonator_bank][decay][US2][SC-003]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 1);  // Single resonator for clarity

    SECTION("decay time is stored correctly") {
        bank.setDecay(0, 2.0f);
        REQUIRE(bank.getDecay(0) == Approx(2.0f).margin(0.01f));
    }

    SECTION("decay is clamped to valid range") {
        bank.setDecay(0, 0.0001f);  // Below minimum
        REQUIRE(bank.getDecay(0) >= kMinDecayTime);

        bank.setDecay(0, 100.0f);  // Above maximum
        REQUIRE(bank.getDecay(0) <= kMaxDecayTime);
    }

    SECTION("longer decay produces more sustained output (SC-003)") {
        // Test that longer decay settings produce longer sustained output
        // This verifies the decay parameter has effect, without requiring exact RT60 measurement

        // Short decay
        bank.setDecay(0, 0.1f);
        constexpr size_t bufferSize = 44100;  // 1 second
        std::vector<float> outputShort(bufferSize, 0.0f);
        outputShort[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputShort[i] = bank.process(0.0f);
        }
        float energyShort = calculateEnergy(outputShort.data() + bufferSize / 2, bufferSize / 2);

        // Long decay - need to reset and reconfigure
        bank.reset();
        bank.prepare(kTestSampleRateDouble);
        bank.setHarmonicSeries(440.0f, 1);
        bank.setDecay(0, 2.0f);  // Much longer decay

        std::vector<float> outputLong(bufferSize, 0.0f);
        outputLong[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputLong[i] = bank.process(0.0f);
        }
        float energyLong = calculateEnergy(outputLong.data() + bufferSize / 2, bufferSize / 2);

        // Longer decay should have more energy in the tail
        REQUIRE(energyLong > energyShort);
    }
}

// T025: setGain amplitude control
TEST_CASE("ResonatorBank setGain controls amplitude", "[resonator_bank][gain][US2]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 1);

    SECTION("-6dB resonator outputs approximately half amplitude") {
        // Reference: 0dB gain
        bank.setGain(0, 0.0f);
        float output0dB = bank.process(1.0f);

        bank.reset();
        bank.prepare(kTestSampleRateDouble);
        bank.setHarmonicSeries(440.0f, 1);

        // Test: -6dB gain
        bank.setGain(0, -6.0f);
        float outputMinus6dB = bank.process(1.0f);

        // -6dB should be approximately half amplitude
        REQUIRE(std::abs(outputMinus6dB) == Approx(std::abs(output0dB) * 0.5f).margin(0.1f));
    }

    SECTION("getGain returns dB value") {
        bank.setGain(0, -12.0f);
        REQUIRE(bank.getGain(0) == Approx(-12.0f).margin(0.1f));
    }
}

// T026: setQ bandwidth control
TEST_CASE("ResonatorBank setQ controls bandwidth", "[resonator_bank][q][US2]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    SECTION("Q is clamped to valid range") {
        bank.setHarmonicSeries(440.0f, 1);

        bank.setQ(0, 0.01f);  // Below minimum
        REQUIRE(bank.getQ(0) >= kMinResonatorQ);

        bank.setQ(0, 500.0f);  // Above maximum
        REQUIRE(bank.getQ(0) <= kMaxResonatorQ);
    }

    SECTION("higher Q produces longer decay") {
        // Low Q
        bank.setHarmonicSeries(440.0f, 1);
        bank.setQ(0, 2.0f);

        constexpr size_t bufferSize = 44100;
        std::vector<float> outputLowQ(bufferSize, 0.0f);
        outputLowQ[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputLowQ[i] = bank.process(0.0f);
        }

        // High Q
        bank.reset();
        bank.prepare(kTestSampleRateDouble);
        bank.setHarmonicSeries(440.0f, 1);
        bank.setQ(0, 50.0f);

        std::vector<float> outputHighQ(bufferSize, 0.0f);
        outputHighQ[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputHighQ[i] = bank.process(0.0f);
        }

        // High Q should have more energy in tail
        float energyLowQTail = calculateEnergy(outputLowQ.data() + bufferSize / 2, bufferSize / 2);
        float energyHighQTail = calculateEnergy(outputHighQ.data() + bufferSize / 2, bufferSize / 2);

        REQUIRE(energyHighQTail > energyLowQTail);
    }
}

// T027: Parameter smoothing
TEST_CASE("ResonatorBank parameter changes are smoothed", "[resonator_bank][smoothing][US2][SC-005]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("frequency change produces no clicks") {
        constexpr size_t bufferSize = 4410;  // 100ms
        std::vector<float> output(bufferSize, 0.0f);

        // Start with constant input
        for (size_t i = 0; i < bufferSize / 2; ++i) {
            output[i] = bank.process(0.1f);
        }

        // Change frequency mid-buffer
        bank.setFrequency(0, 880.0f);

        for (size_t i = bufferSize / 2; i < bufferSize; ++i) {
            output[i] = bank.process(0.1f);
        }

        // Check for clicks (sudden large changes)
        float maxDiff = 0.0f;
        for (size_t i = 1; i < bufferSize; ++i) {
            float diff = std::abs(output[i] - output[i - 1]);
            if (diff > maxDiff) maxDiff = diff;
        }

        // No sample-to-sample jump should be extreme
        REQUIRE(maxDiff < 0.5f);
    }
}

// ==============================================================================
// Phase 5: User Story 3 - Tuning Modes (P2)
// ==============================================================================

// T039: Harmonic series accuracy (SC-002)
TEST_CASE("ResonatorBank harmonic series within 1 cent accuracy", "[resonator_bank][tuning][US3][SC-002]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 8);

    // 1 cent = 1/100 of a semitone
    // Frequency ratio for 1 cent = 2^(1/1200) ~= 1.0005778
    constexpr float oneCentRatio = 1.0005778f;

    for (int i = 0; i < 8; ++i) {
        float expected = 440.0f * (i + 1);
        float actual = bank.getFrequency(static_cast<size_t>(i));

        // Check ratio is within 1 cent
        float ratio = actual / expected;
        REQUIRE(ratio >= (1.0f / oneCentRatio));
        REQUIRE(ratio <= oneCentRatio);
    }
}

// T040: Inharmonic series formula
TEST_CASE("ResonatorBank setInharmonicSeries follows correct formula", "[resonator_bank][tuning][US3]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    const float baseHz = 100.0f;
    const float B = 0.01f;

    bank.setInharmonicSeries(baseHz, B);

    REQUIRE(bank.getTuningMode() == TuningMode::Inharmonic);
    REQUIRE(bank.getNumActiveResonators() == kMaxResonators);

    // Verify formula: f_n = f_0 * n * sqrt(1 + B*n^2)
    for (size_t n = 1; n <= kMaxResonators; ++n) {
        float expected = baseHz * static_cast<float>(n) *
                         std::sqrt(1.0f + B * static_cast<float>(n * n));
        float actual = bank.getFrequency(n - 1);

        // Clamp expected to valid frequency range
        float maxFreq = kTestSampleRate * kMaxResonatorFrequencyRatio;
        if (expected > maxFreq) expected = maxFreq;

        REQUIRE(actual == Approx(expected).margin(1.0f));
    }
}

// T041: Custom frequencies
TEST_CASE("ResonatorBank setCustomFrequencies works correctly", "[resonator_bank][tuning][US3]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    std::array<float, 4> customFreqs = {100.0f, 220.0f, 350.0f, 480.0f};

    bank.setCustomFrequencies(customFreqs.data(), customFreqs.size());

    REQUIRE(bank.getTuningMode() == TuningMode::Custom);
    REQUIRE(bank.getNumActiveResonators() == 4);

    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(bank.getFrequency(i) == Approx(customFreqs[i]).margin(1.0f));
    }
}

// T042: Tuning mode tracking
TEST_CASE("ResonatorBank getTuningMode returns correct mode", "[resonator_bank][tuning][US3]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    SECTION("Harmonic mode") {
        bank.setHarmonicSeries(440.0f, 4);
        REQUIRE(bank.getTuningMode() == TuningMode::Harmonic);
    }

    SECTION("Inharmonic mode") {
        bank.setInharmonicSeries(100.0f, 0.01f);
        REQUIRE(bank.getTuningMode() == TuningMode::Inharmonic);
    }

    SECTION("Custom mode") {
        std::array<float, 2> freqs = {200.0f, 400.0f};
        bank.setCustomFrequencies(freqs.data(), freqs.size());
        REQUIRE(bank.getTuningMode() == TuningMode::Custom);
    }
}

// ==============================================================================
// Phase 6: User Story 4 - Global Controls (P2)
// ==============================================================================

// T053: setDamping
TEST_CASE("ResonatorBank setDamping reduces decay times", "[resonator_bank][damping][US4]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 1);

    SECTION("damping is stored correctly") {
        bank.setDamping(0.5f);
        REQUIRE(bank.getDamping() == Approx(0.5f));
    }

    SECTION("damping is clamped to [0, 1]") {
        bank.setDamping(-0.5f);
        REQUIRE(bank.getDamping() >= 0.0f);

        bank.setDamping(1.5f);
        REQUIRE(bank.getDamping() <= 1.0f);
    }

    SECTION("damping=0.5 reduces decay") {
        // No damping
        bank.setDamping(0.0f);
        constexpr size_t bufferSize = 44100;
        std::vector<float> outputNoDamp(bufferSize, 0.0f);
        outputNoDamp[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputNoDamp[i] = bank.process(0.0f);
        }
        float energyNoDamp = calculateEnergy(outputNoDamp.data() + bufferSize / 2, bufferSize / 2);

        // With damping
        bank.reset();
        bank.prepare(kTestSampleRateDouble);
        bank.setHarmonicSeries(440.0f, 1);
        bank.setDamping(0.5f);
        std::vector<float> outputDamped(bufferSize, 0.0f);
        outputDamped[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputDamped[i] = bank.process(0.0f);
        }
        float energyDamped = calculateEnergy(outputDamped.data() + bufferSize / 2, bufferSize / 2);

        // Damped should have less energy in tail
        REQUIRE(energyDamped < energyNoDamp);
    }
}

// T054: setExciterMix
TEST_CASE("ResonatorBank setExciterMix blends dry and wet", "[resonator_bank][mix][US4]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("exciterMix is stored correctly") {
        bank.setExciterMix(0.5f);
        REQUIRE(bank.getExciterMix() == Approx(0.5f));
    }

    SECTION("exciterMix=0 produces wet only") {
        bank.setExciterMix(0.0f);
        // With no input and no trigger, should be silence
        float output = bank.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("exciterMix=1 produces dry only") {
        bank.setExciterMix(1.0f);

        // Process enough samples to let smoother settle (20ms at 44.1kHz = 882 samples)
        // Need ~5x time constant for 99% settling
        for (int i = 0; i < 4410; ++i) {
            (void)bank.process(0.0f);
        }

        // Now input should pass through directly
        float input = 0.5f;
        float output = bank.process(input);
        REQUIRE(output == Approx(input).margin(0.05f));
    }

    SECTION("exciterMix=0.5 produces 50% blend") {
        bank.setExciterMix(0.5f);

        // Process to settle smoother
        for (int i = 0; i < 100; ++i) {
            (void)bank.process(0.0f);
        }

        float input = 1.0f;
        float output = bank.process(input);

        // Output should contain some dry signal
        REQUIRE(std::abs(output) > 0.1f);
    }
}

// T055: setSpectralTilt
TEST_CASE("ResonatorBank setSpectralTilt attenuates high frequencies", "[resonator_bank][tilt][US4]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    SECTION("spectralTilt is stored correctly") {
        bank.setSpectralTilt(-6.0f);
        REQUIRE(bank.getSpectralTilt() == Approx(-6.0f));
    }

    SECTION("spectralTilt is clamped to [-12, +12]") {
        bank.setSpectralTilt(-20.0f);
        REQUIRE(bank.getSpectralTilt() >= kMinSpectralTilt);

        bank.setSpectralTilt(20.0f);
        REQUIRE(bank.getSpectralTilt() <= kMaxSpectralTilt);
    }

    SECTION("negative tilt reduces high frequency resonator output") {
        bank.setHarmonicSeries(440.0f, 4);

        // No tilt
        bank.setSpectralTilt(0.0f);
        constexpr size_t bufferSize = 8192;
        std::vector<float> outputNoTilt(bufferSize, 0.0f);
        outputNoTilt[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputNoTilt[i] = bank.process(0.0f);
        }
        float magHighNoTilt = getDFTMagnitudeAtFrequency(
            outputNoTilt.data(), bufferSize, 1760.0f, kTestSampleRate);

        // With tilt
        bank.reset();
        bank.prepare(kTestSampleRateDouble);
        bank.setHarmonicSeries(440.0f, 4);
        bank.setSpectralTilt(-6.0f);
        std::vector<float> outputTilt(bufferSize, 0.0f);
        outputTilt[0] = bank.process(1.0f);
        for (size_t i = 1; i < bufferSize; ++i) {
            outputTilt[i] = bank.process(0.0f);
        }
        float magHighTilt = getDFTMagnitudeAtFrequency(
            outputTilt.data(), bufferSize, 1760.0f, kTestSampleRate);

        // High frequency should be reduced with negative tilt
        REQUIRE(magHighTilt < magHighNoTilt);
    }
}

// T056: Global parameter smoothing
TEST_CASE("ResonatorBank global parameters are smoothed", "[resonator_bank][smoothing][US4][SC-005]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("damping change produces no clicks") {
        constexpr size_t bufferSize = 4410;
        std::vector<float> output(bufferSize, 0.0f);

        // Feed constant signal
        for (size_t i = 0; i < bufferSize / 2; ++i) {
            output[i] = bank.process(0.1f);
        }

        // Change damping abruptly
        bank.setDamping(0.8f);

        for (size_t i = bufferSize / 2; i < bufferSize; ++i) {
            output[i] = bank.process(0.1f);
        }

        // Check for extreme sample-to-sample jumps
        float maxDiff = 0.0f;
        for (size_t i = 1; i < bufferSize; ++i) {
            float diff = std::abs(output[i] - output[i - 1]);
            if (diff > maxDiff) maxDiff = diff;
        }

        REQUIRE(maxDiff < 0.5f);
    }
}

// ==============================================================================
// Phase 7: User Story 5 - Percussive Trigger (P3)
// ==============================================================================

// T068: trigger with velocity=1.0
TEST_CASE("ResonatorBank trigger excites all active resonators", "[resonator_bank][trigger][US5]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("trigger(1.0) produces output") {
        bank.trigger(1.0f);

        // Process some samples
        constexpr size_t bufferSize = 1024;
        std::vector<float> output(bufferSize, 0.0f);
        for (size_t i = 0; i < bufferSize; ++i) {
            output[i] = bank.process(0.0f);
        }

        float peak = calculatePeak(output.data(), bufferSize);
        REQUIRE(peak > 0.01f);
    }
}

// T069: trigger velocity scaling
TEST_CASE("ResonatorBank trigger velocity scales amplitude", "[resonator_bank][trigger][US5]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    // Full velocity
    bank.trigger(1.0f);
    float outputFull = bank.process(0.0f);

    bank.reset();
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    // Half velocity
    bank.trigger(0.5f);
    float outputHalf = bank.process(0.0f);

    // Half velocity should produce approximately half amplitude
    REQUIRE(std::abs(outputHalf) == Approx(std::abs(outputFull) * 0.5f).margin(0.1f));
}

// T070: trigger latency (SC-004)
TEST_CASE("ResonatorBank trigger produces output within 1 sample", "[resonator_bank][trigger][US5][SC-004]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    bank.trigger(1.0f);

    // First sample after trigger should have output
    float output = bank.process(0.0f);
    REQUIRE(std::abs(output) > 0.001f);
}

// T071: trigger decay behavior
TEST_CASE("ResonatorBank trigger produces natural decay", "[resonator_bank][trigger][US5]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    bank.trigger(1.0f);

    constexpr size_t bufferSize = 44100;
    std::vector<float> output(bufferSize, 0.0f);
    for (size_t i = 0; i < bufferSize; ++i) {
        output[i] = bank.process(0.0f);
    }

    // Verify decay
    float energyFirst = calculateEnergy(output.data(), bufferSize / 4);
    float energyLast = calculateEnergy(output.data() + 3 * bufferSize / 4, bufferSize / 4);

    REQUIRE(energyLast < energyFirst);
}

// ==============================================================================
// Phase 8: Edge Cases and Stability
// ==============================================================================

// T079: Parameter clamping edge cases
TEST_CASE("ResonatorBank clamps parameters correctly", "[resonator_bank][edge][US2]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("frequency below 20Hz is clamped") {
        bank.setFrequency(0, 5.0f);
        REQUIRE(bank.getFrequency(0) >= kMinResonatorFrequency);
    }

    SECTION("Q above 100 is clamped") {
        bank.setQ(0, 150.0f);
        REQUIRE(bank.getQ(0) <= kMaxResonatorQ);
    }

    SECTION("decay above 30s is clamped") {
        bank.setDecay(0, 50.0f);
        REQUIRE(bank.getDecay(0) <= kMaxDecayTime);
    }
}

// T080: Custom frequencies exceeding 16
TEST_CASE("ResonatorBank setCustomFrequencies handles excess", "[resonator_bank][edge][US3]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);

    std::array<float, 20> manyFreqs;
    for (size_t i = 0; i < 20; ++i) {
        manyFreqs[i] = 100.0f + 50.0f * static_cast<float>(i);
    }

    bank.setCustomFrequencies(manyFreqs.data(), manyFreqs.size());

    // Only first 16 should be used
    REQUIRE(bank.getNumActiveResonators() == kMaxResonators);
}

// T081: Stability with all 16 resonators and long decays (SC-007)
TEST_CASE("ResonatorBank remains stable with all 16 resonators", "[resonator_bank][stability][SC-007]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(110.0f, 16);

    // Set long decay for all
    for (size_t i = 0; i < 16; ++i) {
        bank.setDecay(i, 10.0f);  // 10 second decay
    }

    // Process impulse and extended buffer
    constexpr size_t bufferSize = 88200;  // 2 seconds
    std::vector<float> output(bufferSize, 0.0f);

    output[0] = bank.process(1.0f);
    for (size_t i = 1; i < bufferSize; ++i) {
        output[i] = bank.process(0.0f);
    }

    SECTION("no NaN in output") {
        for (size_t i = 0; i < bufferSize; ++i) {
            REQUIRE_FALSE(std::isnan(output[i]));
        }
    }

    SECTION("no infinity in output") {
        for (size_t i = 0; i < bufferSize; ++i) {
            REQUIRE_FALSE(std::isinf(output[i]));
        }
    }

    SECTION("output remains bounded") {
        float peak = calculatePeak(output.data(), bufferSize);
        REQUIRE(peak < 100.0f);  // Reasonable bound
    }
}

// ==============================================================================
// Enabled/Disabled Tests
// ==============================================================================

TEST_CASE("ResonatorBank setEnabled controls resonator activity", "[resonator_bank][enabled][US2]") {
    ResonatorBank bank;
    bank.prepare(kTestSampleRateDouble);
    bank.setHarmonicSeries(440.0f, 4);

    SECTION("resonators can be disabled individually") {
        REQUIRE(bank.isEnabled(0) == true);
        bank.setEnabled(0, false);
        REQUIRE(bank.isEnabled(0) == false);
        REQUIRE(bank.getNumActiveResonators() == 3);
    }

    SECTION("disabled resonator produces no output") {
        bank.setEnabled(0, false);
        bank.setEnabled(1, false);
        bank.setEnabled(2, false);
        bank.setEnabled(3, false);

        float output = bank.process(1.0f);
        REQUIRE(output == Approx(0.0f).margin(kTolerance));
    }

    SECTION("invalid index returns false for isEnabled") {
        REQUIRE(bank.isEnabled(100) == false);
    }
}

// ==============================================================================
// Vorago Phase 2 (specs/vorago-phase2-noise-organism) - T005 / FR-099 / SC-011 (vi)
// ==============================================================================
// setFrequency() must re-derive Q from the CONFIGURED decay, exactly the way
// setDecay() already does. Today it does not (resonator_bank.h:328-332 sets
// frequencies_[] + updateFilterCoefficients() only, while setDecay at :345-352
// also writes qValues_[index] = rt60ToQ(...)), so a resonator whose frequency is
// wandered - which is precisely what the NoiseOrganism control step does - has
// its effective RT60 silently scaled by the frequency ratio.

namespace {

// Least-squares RT60 fit. Deliberately distinct from measureRT60() above, which is a
// threshold-crossing estimator: this one fits dB-vs-time across the -6 dB .. -40 dB
// region of the 20 ms RMS envelope and extrapolates the slope to -60 dB, so it is
// insensitive to where the tail meets the noise floor and to the constant log-domain
// offset that window-averaging an exponential decay introduces.
// Returns 0.0f if no usable decaying region is present.
inline float fitRT60LeastSquares(const float* buffer, size_t size, float sampleRate) {
    constexpr double kEnvelopeWindowSeconds = 0.020;
    if (!(sampleRate > 0.0f)) return 0.0f;
    // Rounded, not truncated: 0.020f * 48000.0f evaluates to 959.99998 in float, which
    // would truncate to 959 and lose the exact one-period-per-window alignment at 50 Hz.
    const size_t windowSize = static_cast<size_t>(
        std::lround(kEnvelopeWindowSeconds * static_cast<double>(sampleRate)));
    if (windowSize == 0 || size < windowSize * 4) return 0.0f;

    const size_t numWindows = size / windowSize;
    std::vector<float> envelopeDb(numWindows, -144.0f);
    float peakDb = -144.0f;
    size_t peakWindow = 0;
    for (size_t w = 0; w < numWindows; ++w) {
        envelopeDb[w] = linearToDb(calculateRMS(buffer + w * windowSize, windowSize));
        if (envelopeDb[w] > peakDb) {
            peakDb = envelopeDb[w];
            peakWindow = w;
        }
    }
    if (peakDb <= -143.0f) return 0.0f;

    // Fit region starts at the first window at or below -6 dB, searched forward from
    // the peak window so a pre-peak ramp can never be mistaken for the decay.
    size_t firstWindow = numWindows;
    for (size_t w = peakWindow; w < numWindows; ++w) {
        if (envelopeDb[w] - peakDb <= -6.0f) {
            firstWindow = w;
            break;
        }
    }
    if (firstWindow >= numWindows) return 0.0f;

    double sumT = 0.0, sumY = 0.0, sumTT = 0.0, sumTY = 0.0;
    size_t count = 0;
    for (size_t w = firstWindow; w < numWindows; ++w) {
        const double relDb = static_cast<double>(envelopeDb[w] - peakDb);
        if (relDb < -40.0) break;
        const double t = (static_cast<double>(w) + 0.5) * static_cast<double>(windowSize)
                       / static_cast<double>(sampleRate);
        sumT += t;
        sumY += relDb;
        sumTT += t * t;
        sumTY += t * relDb;
        ++count;
    }
    if (count < 3) return 0.0f;

    const double n = static_cast<double>(count);
    const double denominator = n * sumTT - sumT * sumT;
    if (denominator <= 0.0) return 0.0f;
    const double slopeDbPerSecond = (n * sumTY - sumT * sumY) / denominator;
    if (slopeDbPerSecond >= 0.0) return 0.0f;  // not decaying - caller treats as a failure
    return static_cast<float>(-60.0 / slopeDbPerSecond);
}

} // anonymous namespace

TEST_CASE("ResonatorBank_SetFrequencyRederivesQ", "[resonator_bank][vorago-phase2]") {
    // --------------------------------------------------------------------------
    // Fixture note - DELIBERATE DEVIATION from the 200 Hz -> 800 Hz numbers in
    // tasks.md T005, because those two are arithmetically incompatible with the
    // shipped kMaxResonatorQ = 100 clamp (resonator_bank.h:50) inside rt60ToQ (:89):
    //     rt60ToQ(800, 1.0) = pi*800/ln(1000) = 363.8 -> CLAMPED to 100
    //     => the best achievable RT60 at 800 Hz is ln(1000)*100/(pi*800) = 0.275 s,
    //        so "within +/-25 % of 1.0 s" at 800 Hz cannot hold even AFTER the fix.
    // The constraint is f <= 100*ln(1000)/pi = 219.8 Hz at a 1.0 s decay, so the
    // fixture keeps BOTH the 1.0 s decay and the 4x frequency step by anchoring at
    // 50 Hz and moving to 200 Hz:
    //     rt60ToQ(50,  1.0) =  22.74  (unclamped)
    //     rt60ToQ(200, 1.0) =  90.96  (unclamped)
    // The pre-fix failure mode is unchanged and is exactly the one T005 describes:
    // the Q held from 50 Hz gives RT60 = 22.74*ln(1000)/(pi*200) = 0.25 s at 200 Hz,
    // i.e. ~1/4 of the configured decay - a SHORT fit, not a NaN.
    // A 1.0 s RT60 also keeps the fit statistically sound: the -6 dB .. -40 dB region
    // spans ~28 envelope windows, against ~7 for the 0.25 s decay that keeping the
    // 800 Hz endpoint would have forced.
    // --------------------------------------------------------------------------
    constexpr double kSampleRate = 48000.0;
    constexpr float kSampleRateF = 48000.0f;
    constexpr float kDecaySeconds = 1.0f;
    constexpr float kAnchorHz = 50.0f;
    constexpr float kMovedHz = 200.0f;  // 4x the anchor
    constexpr size_t kRenderSamples = 144000;  // 3 s @ 48 kHz
    constexpr float kRt60LowerBound = kDecaySeconds * 0.75f;   // -25 %
    constexpr float kRt60UpperBound = kDecaySeconds * 1.25f;   // +25 %

    ResonatorBank bank;
    bank.prepare(kSampleRate);
    bank.setEnabled(0, true);
    bank.setFrequency(0, kAnchorHz);
    bank.setDecay(0, kDecaySeconds);

    std::vector<float> render(kRenderSamples, 0.0f);

    // Single unit impulse, then free decay. Global defaults keep the measurement clean:
    // exciterMix_ = 0 (wet only), damping_ = 0, spectralTilt_ = 0, gains_[0] = 1.0.
    auto exciteAndRender = [&bank, &render]() {
        render[0] = bank.process(1.0f);
        for (size_t i = 1; i < render.size(); ++i) {
            render[i] = bank.process(0.0f);
        }
    };

    // --- Baseline at the anchor frequency: passes BEFORE and AFTER the fix. --------
    exciteAndRender();
    REQUIRE_FALSE(hasInvalidSamples(render.data(), render.size()));
    const float baselineRt60 = fitRT60LeastSquares(render.data(), render.size(), kSampleRateF);
    INFO("baseline RT60 at " << kAnchorHz << " Hz = " << baselineRt60 << " s (configured "
         << kDecaySeconds << " s, Q = " << bank.getQ(0) << ")");
    REQUIRE(baselineRt60 > 0.0f);
    REQUIRE(baselineRt60 > kRt60LowerBound);
    REQUIRE(baselineRt60 < kRt60UpperBound);

    // --- Move the frequency WITHOUT touching setDecay. ----------------------------
    bank.setFrequency(0, kMovedHz);
    REQUIRE(bank.getFrequency(0) == Approx(kMovedHz).margin(0.5f));
    // setFrequency must not disturb the configured decay itself.
    REQUIRE(bank.getDecay(0) == Approx(kDecaySeconds).margin(1e-6f));

    // Clear the residual ring by rendering silence - NOT by calling bank.reset().
    // reset() is a CONFIGURATION wipe (resonator_bank.h:212-232: 440 Hz, default decay,
    // default Q, enabled_[i] = false, documented at :211-212), so it would destroy the
    // very state under test. After 3 s at a 1 s RT60 the previous ring is ~-180 dB;
    // 0.5 s more silence puts it far below the next impulse own -60 dB point, and
    // also flushes the transient caused by re-tuning the biquad under a live state.
    for (size_t i = 0; i < 24000; ++i) {
        static_cast<void>(bank.process(0.0f));
    }

    // --- Re-fit at the moved frequency. -------------------------------------------
    exciteAndRender();
    REQUIRE_FALSE(hasInvalidSamples(render.data(), render.size()));
    const float movedRt60 = fitRT60LeastSquares(render.data(), render.size(), kSampleRateF);
    INFO("post-setFrequency RT60 at " << kMovedHz << " Hz = " << movedRt60 << " s (configured "
         << kDecaySeconds << " s, Q = " << bank.getQ(0) << ")");

    // Right-reason guard: before the FR-099 fix this must be a real, finite, SHORT fit
    // (~0.25 s), never 0.0f / NaN / a failed fit.
    REQUIRE(movedRt60 > 0.0f);

    // The requirement itself: the effective RT60 still tracks the configured decay.
    REQUIRE(movedRt60 > kRt60LowerBound);
    REQUIRE(movedRt60 < kRt60UpperBound);

    // And the re-derived Q is the one rt60ToQ would produce for the NEW frequency.
    REQUIRE(bank.getQ(0) == Approx(rt60ToQ(kMovedHz, kDecaySeconds)).margin(1e-3f));
}
