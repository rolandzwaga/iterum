// ==============================================================================
// Unit Tests: MultimodeFilter
// ==============================================================================
// Layer 2: DSP Processor Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/multimode_filter.h"

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

// Generate a sine wave at specified frequency
// Note: kTwoPi is now available from Iterum::DSP via math_constants.h
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
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

// Generate white noise
inline void generateWhiteNoise(float* buffer, size_t size, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

// Measure response at a specific frequency by processing a sine and measuring output RMS
inline float measureResponseDb(MultimodeFilter& filter, float testFreq, float sampleRate, size_t numSamples = 4096) {
    std::vector<float> buffer(numSamples);
    generateSine(buffer.data(), numSamples, testFreq, sampleRate);

    // Let filter settle for first half, measure second half
    filter.process(buffer.data(), numSamples);

    // Skip first quarter (transient), measure rest
    size_t startSample = numSamples / 4;
    float rms = calculateRMS(buffer.data() + startSample, numSamples - startSample);

    // Input sine has RMS of 1/sqrt(2) ≈ 0.707
    constexpr float kInputRms = 0.7071067811865476f;
    return linearToDb(rms / kInputRms);
}

} // anonymous namespace

// ==============================================================================
// Phase 2: FilterSlope Enumeration Tests
// ==============================================================================

TEST_CASE("FilterSlope enumeration values", "[multimode][slope][US2]") {
    SECTION("enum values match expected stage counts") {
        REQUIRE(static_cast<size_t>(FilterSlope::Slope12dB) == 1);
        REQUIRE(static_cast<size_t>(FilterSlope::Slope24dB) == 2);
        REQUIRE(static_cast<size_t>(FilterSlope::Slope36dB) == 3);
        REQUIRE(static_cast<size_t>(FilterSlope::Slope48dB) == 4);
    }
}

TEST_CASE("slopeToStages utility function", "[multimode][slope][US2]") {
    SECTION("returns correct stage count for each slope") {
        REQUIRE(slopeToStages(FilterSlope::Slope12dB) == 1);
        REQUIRE(slopeToStages(FilterSlope::Slope24dB) == 2);
        REQUIRE(slopeToStages(FilterSlope::Slope36dB) == 3);
        REQUIRE(slopeToStages(FilterSlope::Slope48dB) == 4);
    }

    SECTION("is constexpr") {
        constexpr size_t stages = slopeToStages(FilterSlope::Slope24dB);
        static_assert(stages == 2, "slopeToStages must be constexpr");
        REQUIRE(stages == 2);
    }
}

TEST_CASE("slopeTodBPerOctave utility function", "[multimode][slope][US2]") {
    SECTION("returns correct dB per octave for each slope") {
        REQUIRE(slopeTodBPerOctave(FilterSlope::Slope12dB) == Approx(12.0f));
        REQUIRE(slopeTodBPerOctave(FilterSlope::Slope24dB) == Approx(24.0f));
        REQUIRE(slopeTodBPerOctave(FilterSlope::Slope36dB) == Approx(36.0f));
        REQUIRE(slopeTodBPerOctave(FilterSlope::Slope48dB) == Approx(48.0f));
    }

    SECTION("is constexpr") {
        constexpr float dBPerOct = slopeTodBPerOctave(FilterSlope::Slope24dB);
        static_assert(dBPerOct == 24.0f, "slopeTodBPerOctave must be constexpr");
        REQUIRE(dBPerOct == 24.0f);
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Basic Filtering Tests
// ==============================================================================

TEST_CASE("MultimodeFilter construction and defaults", "[multimode][lifecycle][US1]") {
    MultimodeFilter filter;

    SECTION("default type is Lowpass") {
        REQUIRE(filter.getType() == FilterType::Lowpass);
    }

    SECTION("default slope is 12dB") {
        REQUIRE(filter.getSlope() == FilterSlope::Slope12dB);
    }

    SECTION("default cutoff is 1000Hz") {
        REQUIRE(filter.getCutoff() == Approx(1000.0f));
    }

    SECTION("default resonance is Butterworth Q") {
        REQUIRE(filter.getResonance() == Approx(0.7071067811865476f));
    }

    SECTION("default gain is 0dB") {
        REQUIRE(filter.getGain() == Approx(0.0f));
    }

    SECTION("default drive is 0dB (bypass)") {
        REQUIRE(filter.getDrive() == Approx(0.0f));
    }

    SECTION("is not prepared initially") {
        REQUIRE_FALSE(filter.isPrepared());
    }
}

TEST_CASE("MultimodeFilter prepare and reset", "[multimode][lifecycle][US1]") {
    MultimodeFilter filter;

    SECTION("prepare sets prepared state") {
        filter.prepare(44100.0, 512);
        REQUIRE(filter.isPrepared());
        REQUIRE(filter.sampleRate() == Approx(44100.0));
    }

    SECTION("reset clears state without affecting prepared status") {
        filter.prepare(44100.0, 512);
        filter.setCutoff(5000.0f);

        // Process some audio
        std::array<float, 512> buffer{};
        buffer[0] = 1.0f;  // Impulse
        filter.process(buffer.data(), 512);

        // Reset and verify still prepared
        filter.reset();
        REQUIRE(filter.isPrepared());

        // Verify state is cleared (process impulse again, should get same result)
        std::array<float, 512> buffer2{};
        buffer2[0] = 1.0f;
        filter.process(buffer2.data(), 512);

        // Outputs should be similar (state was reset)
        REQUIRE(buffer[100] == Approx(buffer2[100]).margin(0.01f));
    }
}

TEST_CASE("MultimodeFilter parameter setters and getters", "[multimode][params][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);

    SECTION("setType/getType") {
        filter.setType(FilterType::Highpass);
        REQUIRE(filter.getType() == FilterType::Highpass);

        filter.setType(FilterType::Bandpass);
        REQUIRE(filter.getType() == FilterType::Bandpass);

        filter.setType(FilterType::Peak);
        REQUIRE(filter.getType() == FilterType::Peak);
    }

    SECTION("setSlope/getSlope") {
        filter.setSlope(FilterSlope::Slope24dB);
        REQUIRE(filter.getSlope() == FilterSlope::Slope24dB);

        filter.setSlope(FilterSlope::Slope48dB);
        REQUIRE(filter.getSlope() == FilterSlope::Slope48dB);
    }

    SECTION("setCutoff/getCutoff with clamping") {
        filter.setCutoff(500.0f);
        REQUIRE(filter.getCutoff() == Approx(500.0f));

        // Below minimum (20Hz)
        filter.setCutoff(5.0f);
        REQUIRE(filter.getCutoff() == Approx(20.0f));

        // Above maximum (Nyquist/2 = 11025Hz for 44100Hz)
        filter.setCutoff(20000.0f);
        REQUIRE(filter.getCutoff() <= 44100.0f / 2.0f);
    }

    SECTION("setResonance/getResonance with clamping") {
        filter.setResonance(4.0f);
        REQUIRE(filter.getResonance() == Approx(4.0f));

        // Below minimum (0.1)
        filter.setResonance(0.01f);
        REQUIRE(filter.getResonance() == Approx(0.1f));

        // Above maximum (100)
        filter.setResonance(200.0f);
        REQUIRE(filter.getResonance() == Approx(100.0f));
    }

    SECTION("setGain/getGain with clamping") {
        filter.setGain(6.0f);
        REQUIRE(filter.getGain() == Approx(6.0f));

        // Below minimum (-24dB)
        filter.setGain(-30.0f);
        REQUIRE(filter.getGain() == Approx(-24.0f));

        // Above maximum (+24dB)
        filter.setGain(30.0f);
        REQUIRE(filter.getGain() == Approx(24.0f));
    }

    SECTION("setDrive/getDrive with clamping") {
        filter.setDrive(12.0f);
        REQUIRE(filter.getDrive() == Approx(12.0f));

        // Below minimum (0dB)
        filter.setDrive(-5.0f);
        REQUIRE(filter.getDrive() == Approx(0.0f));

        // Above maximum (24dB)
        filter.setDrive(30.0f);
        REQUIRE(filter.getDrive() == Approx(24.0f));
    }
}

TEST_CASE("MultimodeFilter Lowpass basic behavior", "[multimode][lowpass][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    filter.setSlope(FilterSlope::Slope12dB);

    SECTION("passes frequencies below cutoff") {
        float responseAt500Hz = measureResponseDb(filter, 500.0f, 44100.0f);
        // Should be close to 0dB (within 1dB passband)
        REQUIRE(responseAt500Hz > -1.0f);
    }

    SECTION("attenuates frequencies above cutoff") {
        filter.reset();
        float responseAt2kHz = measureResponseDb(filter, 2000.0f, 44100.0f);
        // At one octave above cutoff, 12dB slope should give ~-12dB
        REQUIRE(responseAt2kHz < -6.0f);  // At least some attenuation
    }
}

TEST_CASE("MultimodeFilter Highpass basic behavior", "[multimode][highpass][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Highpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    filter.setSlope(FilterSlope::Slope12dB);

    SECTION("passes frequencies above cutoff") {
        float responseAt2kHz = measureResponseDb(filter, 2000.0f, 44100.0f);
        // Should be close to 0dB (within 1dB passband)
        REQUIRE(responseAt2kHz > -1.0f);
    }

    SECTION("attenuates frequencies below cutoff") {
        filter.reset();
        float responseAt500Hz = measureResponseDb(filter, 500.0f, 44100.0f);
        // At one octave below cutoff, 12dB slope should give ~-12dB
        REQUIRE(responseAt500Hz < -6.0f);  // At least some attenuation
    }
}

TEST_CASE("MultimodeFilter Bandpass basic behavior", "[multimode][bandpass][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Bandpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(4.0f);  // Q = 4

    SECTION("passes center frequency") {
        float responseAtCenter = measureResponseDb(filter, 1000.0f, 44100.0f);
        // Should be close to 0dB at center
        REQUIRE(responseAtCenter > -3.0f);
    }

    SECTION("attenuates frequencies away from center") {
        filter.reset();
        float responseAt250Hz = measureResponseDb(filter, 250.0f, 44100.0f);
        REQUIRE(responseAt250Hz < -6.0f);

        filter.reset();
        float responseAt4kHz = measureResponseDb(filter, 4000.0f, 44100.0f);
        REQUIRE(responseAt4kHz < -6.0f);
    }
}

TEST_CASE("MultimodeFilter Notch basic behavior", "[multimode][notch][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Notch);
    filter.setCutoff(1000.0f);
    filter.setResonance(4.0f);

    SECTION("attenuates center frequency") {
        float responseAtCenter = measureResponseDb(filter, 1000.0f, 44100.0f);
        // Should be significantly attenuated at center
        REQUIRE(responseAtCenter < -10.0f);
    }

    SECTION("passes frequencies away from center") {
        filter.reset();
        float responseAt250Hz = measureResponseDb(filter, 250.0f, 44100.0f);
        REQUIRE(responseAt250Hz > -3.0f);

        filter.reset();
        float responseAt4kHz = measureResponseDb(filter, 4000.0f, 44100.0f);
        REQUIRE(responseAt4kHz > -3.0f);
    }
}

TEST_CASE("MultimodeFilter Allpass basic behavior", "[multimode][allpass][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Allpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);

    SECTION("flat magnitude response at various frequencies") {
        float response500Hz = measureResponseDb(filter, 500.0f, 44100.0f);
        filter.reset();
        float response1kHz = measureResponseDb(filter, 1000.0f, 44100.0f);
        filter.reset();
        float response2kHz = measureResponseDb(filter, 2000.0f, 44100.0f);

        // All should be near 0dB (flat response)
        REQUIRE(response500Hz == Approx(0.0f).margin(1.0f));
        REQUIRE(response1kHz == Approx(0.0f).margin(1.0f));
        REQUIRE(response2kHz == Approx(0.0f).margin(1.0f));
    }
}

TEST_CASE("MultimodeFilter LowShelf basic behavior", "[multimode][shelf][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::LowShelf);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    filter.setGain(6.0f);  // +6dB boost

    SECTION("boosts frequencies below shelf frequency") {
        float response500Hz = measureResponseDb(filter, 200.0f, 44100.0f);
        // Should be boosted by approximately the gain
        REQUIRE(response500Hz > 4.0f);  // At least +4dB
    }

    SECTION("leaves frequencies above shelf frequency unaffected") {
        filter.reset();
        float response4kHz = measureResponseDb(filter, 4000.0f, 44100.0f);
        // Should be near 0dB
        REQUIRE(response4kHz == Approx(0.0f).margin(1.0f));
    }
}

TEST_CASE("MultimodeFilter HighShelf basic behavior", "[multimode][shelf][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::HighShelf);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    filter.setGain(6.0f);  // +6dB boost

    SECTION("boosts frequencies above shelf frequency") {
        float response4kHz = measureResponseDb(filter, 4000.0f, 44100.0f);
        // Should be boosted by approximately the gain
        REQUIRE(response4kHz > 4.0f);  // At least +4dB
    }

    SECTION("leaves frequencies below shelf frequency unaffected") {
        filter.reset();
        float response200Hz = measureResponseDb(filter, 200.0f, 44100.0f);
        // Should be near 0dB
        REQUIRE(response200Hz == Approx(0.0f).margin(1.0f));
    }
}

TEST_CASE("MultimodeFilter Peak (parametric) basic behavior", "[multimode][peak][US1]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Peak);
    filter.setCutoff(1000.0f);
    filter.setResonance(4.0f);  // Q = 4
    filter.setGain(6.0f);  // +6dB boost

    SECTION("boosts center frequency") {
        float responseAtCenter = measureResponseDb(filter, 1000.0f, 44100.0f);
        // Should be boosted by approximately the gain
        REQUIRE(responseAtCenter > 4.0f);
    }

    SECTION("leaves frequencies away from center unaffected") {
        filter.reset();
        float response200Hz = measureResponseDb(filter, 200.0f, 44100.0f);
        filter.reset();
        float response5kHz = measureResponseDb(filter, 5000.0f, 44100.0f);

        // Should be near 0dB
        REQUIRE(response200Hz == Approx(0.0f).margin(2.0f));
        REQUIRE(response5kHz == Approx(0.0f).margin(2.0f));
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Slope Selection Tests
// ==============================================================================

TEST_CASE("MultimodeFilter slope selection for Lowpass", "[multimode][slope][US2][SC-001]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 8192);
    filter.setType(FilterType::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);

    // Measure at 2x cutoff (one octave above)
    SECTION("12dB slope gives ~12dB attenuation at 2x cutoff") {
        filter.setSlope(FilterSlope::Slope12dB);
        float response = measureResponseDb(filter, 2000.0f, 44100.0f);
        // Should be within ±3dB of -12dB (accounting for measurement tolerance)
        REQUIRE(response < -9.0f);
        REQUIRE(response > -15.0f);
    }

    SECTION("24dB slope gives ~24dB attenuation at 2x cutoff") {
        filter.setSlope(FilterSlope::Slope24dB);
        float response = measureResponseDb(filter, 2000.0f, 44100.0f);
        REQUIRE(response < -21.0f);
        REQUIRE(response > -27.0f);
    }

    SECTION("36dB slope gives ~36dB attenuation at 2x cutoff") {
        filter.setSlope(FilterSlope::Slope36dB);
        float response = measureResponseDb(filter, 2000.0f, 44100.0f);
        REQUIRE(response < -33.0f);
        REQUIRE(response > -39.0f);
    }

    SECTION("48dB slope gives ~48dB attenuation at 2x cutoff") {
        filter.setSlope(FilterSlope::Slope48dB);
        float response = measureResponseDb(filter, 2000.0f, 44100.0f);
        REQUIRE(response < -45.0f);
        REQUIRE(response > -51.0f);
    }
}

TEST_CASE("MultimodeFilter slope selection for Highpass", "[multimode][slope][US2][SC-002]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 8192);
    filter.setType(FilterType::Highpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);

    // Measure at 0.5x cutoff (one octave below)
    SECTION("24dB slope gives ~24dB attenuation at 0.5x cutoff") {
        filter.setSlope(FilterSlope::Slope24dB);
        float response = measureResponseDb(filter, 500.0f, 44100.0f);
        REQUIRE(response < -21.0f);
        REQUIRE(response > -27.0f);
    }
}

TEST_CASE("MultimodeFilter slope is ignored for Allpass/Shelf/Peak", "[multimode][slope][US2]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);

    SECTION("Allpass always uses single stage regardless of slope") {
        filter.setType(FilterType::Allpass);
        filter.setSlope(FilterSlope::Slope48dB);  // Should be ignored

        // Allpass should still have flat magnitude
        float response1k = measureResponseDb(filter, 1000.0f, 44100.0f);
        REQUIRE(response1k == Approx(0.0f).margin(1.0f));
    }

    SECTION("LowShelf uses single stage regardless of slope") {
        filter.setType(FilterType::LowShelf);
        filter.setGain(6.0f);
        filter.setCutoff(1000.0f);
        filter.setSlope(FilterSlope::Slope48dB);  // Should be ignored

        // Gain should be applied normally (not stacked 4x)
        float responseLow = measureResponseDb(filter, 200.0f, 44100.0f);
        REQUIRE(responseLow > 4.0f);
        REQUIRE(responseLow < 10.0f);  // Not 24dB!
    }

    SECTION("Peak uses single stage regardless of slope") {
        filter.setType(FilterType::Peak);
        filter.setGain(6.0f);
        filter.setCutoff(1000.0f);
        filter.setResonance(4.0f);
        filter.setSlope(FilterSlope::Slope48dB);  // Should be ignored

        float responseCenter = measureResponseDb(filter, 1000.0f, 44100.0f);
        REQUIRE(responseCenter > 4.0f);
        REQUIRE(responseCenter < 10.0f);  // Not 24dB!
    }
}

TEST_CASE("MultimodeFilter Bandpass -3dB bandwidth matches Q", "[multimode][bandpass][SC-003]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 8192);
    filter.setType(FilterType::Bandpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(4.0f);  // Q = 4, so BW = 1000/4 = 250 Hz

    // Expected -3dB points: 1000 ± 125 Hz (approximately 875 Hz and 1125 Hz)
    SECTION("-3dB bandwidth approximately matches f0/Q") {
        // This is a rough check - exact bandwidth measurement would need frequency sweep
        float responseAtCenter = measureResponseDb(filter, 1000.0f, 44100.0f);
        filter.reset();
        float responseAtLowerEdge = measureResponseDb(filter, 875.0f, 44100.0f);
        filter.reset();
        float responseAtUpperEdge = measureResponseDb(filter, 1125.0f, 44100.0f);

        // Edges should be ~3dB below center
        float diffLower = responseAtCenter - responseAtLowerEdge;
        float diffUpper = responseAtCenter - responseAtUpperEdge;

        REQUIRE(diffLower == Approx(3.0f).margin(1.5f));
        REQUIRE(diffUpper == Approx(3.0f).margin(1.5f));
    }
}

// ==============================================================================
// Phase 5: User Story 7 - Real-Time Safety Tests
// ==============================================================================
// Note: These are primarily verified through code inspection (T051-T057)
// Runtime tests verify the API works correctly under normal conditions

TEST_CASE("MultimodeFilter process methods are noexcept", "[multimode][realtime][US7]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);

    std::array<float, 512> buffer{};

    SECTION("process is noexcept") {
        static_assert(noexcept(filter.process(buffer.data(), 512)),
                     "process() must be noexcept");
        REQUIRE(true);  // Compilation is the test
    }

    SECTION("processSample is noexcept") {
        static_assert(noexcept(filter.processSample(0.0f)),
                     "processSample() must be noexcept");
        REQUIRE(true);
    }

    SECTION("reset is noexcept") {
        static_assert(noexcept(filter.reset()),
                     "reset() must be noexcept");
        REQUIRE(true);
    }

    SECTION("all setters are noexcept") {
        static_assert(noexcept(filter.setType(FilterType::Lowpass)), "setType must be noexcept");
        static_assert(noexcept(filter.setSlope(FilterSlope::Slope12dB)), "setSlope must be noexcept");
        static_assert(noexcept(filter.setCutoff(1000.0f)), "setCutoff must be noexcept");
        static_assert(noexcept(filter.setResonance(1.0f)), "setResonance must be noexcept");
        static_assert(noexcept(filter.setGain(0.0f)), "setGain must be noexcept");
        static_assert(noexcept(filter.setDrive(0.0f)), "setDrive must be noexcept");
        static_assert(noexcept(filter.setSmoothingTime(5.0f)), "setSmoothingTime must be noexcept");
        REQUIRE(true);
    }

    SECTION("all getters are noexcept") {
        static_assert(noexcept(filter.getType()), "getType must be noexcept");
        static_assert(noexcept(filter.getSlope()), "getSlope must be noexcept");
        static_assert(noexcept(filter.getCutoff()), "getCutoff must be noexcept");
        static_assert(noexcept(filter.getResonance()), "getResonance must be noexcept");
        static_assert(noexcept(filter.getGain()), "getGain must be noexcept");
        static_assert(noexcept(filter.getDrive()), "getDrive must be noexcept");
        static_assert(noexcept(filter.getLatency()), "getLatency must be noexcept");
        static_assert(noexcept(filter.isPrepared()), "isPrepared must be noexcept");
        static_assert(noexcept(filter.sampleRate()), "sampleRate must be noexcept");
        REQUIRE(true);
    }
}

// ==============================================================================
// Additional Tests (to be expanded in later phases)
// ==============================================================================

TEST_CASE("MultimodeFilter processSample works correctly", "[multimode][sample][US4]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);
    filter.setType(FilterType::Lowpass);
    filter.setCutoff(1000.0f);

    SECTION("processSample returns filtered value") {
        float output = filter.processSample(1.0f);
        // First sample of lowpass response to impulse should be non-zero
        REQUIRE(output != 0.0f);
        REQUIRE(std::isfinite(output));
    }

    SECTION("sequential processSample matches block process") {
        // Process same signal both ways
        std::array<float, 64> blockBuffer;
        std::array<float, 64> sampleBuffer;

        for (size_t i = 0; i < 64; ++i) {
            blockBuffer[i] = (i == 0) ? 1.0f : 0.0f;
            sampleBuffer[i] = blockBuffer[i];
        }

        filter.process(blockBuffer.data(), 64);

        filter.reset();
        for (size_t i = 0; i < 64; ++i) {
            sampleBuffer[i] = filter.processSample(sampleBuffer[i]);
        }

        // Results should match closely
        for (size_t i = 0; i < 64; ++i) {
            REQUIRE(blockBuffer[i] == Approx(sampleBuffer[i]).margin(0.001f));
        }
    }
}

TEST_CASE("MultimodeFilter output is valid", "[multimode][safety]") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);

    std::array<float, 512> buffer;

    SECTION("no NaN or Inf with normal input") {
        generateSine(buffer.data(), 512, 440.0f, 44100.0f);
        filter.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE(std::isfinite(sample));
        }
    }

    SECTION("handles zero input gracefully") {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        filter.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE(std::isfinite(sample));
        }
    }

    SECTION("handles very small input gracefully") {
        std::fill(buffer.begin(), buffer.end(), 1e-30f);
        filter.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE(std::isfinite(sample));
        }
    }
}
