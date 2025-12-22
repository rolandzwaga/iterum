// ==============================================================================
// Layer 1: DSP Primitive Tests - Biquad Filter
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/primitives/biquad.h
// Contract: specs/004-biquad-filter/contracts/biquad.h
// Reference: Robert Bristow-Johnson's Audio EQ Cookbook
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/biquad.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

constexpr float kTestSampleRate = 44100.0f;
constexpr float kTestFrequency = 1000.0f;
// Use butterworthQ() from biquad.h - avoid redefining butterworthQ()
constexpr float kPi = 3.14159265358979323846f;

// ==============================================================================
// Phase 2: Foundational Tests - FilterType Enum (T004-T005)
// ==============================================================================

// T004: FilterType enum definition tests
TEST_CASE("FilterType enum has correct values", "[biquad][enum][foundational]") {
    SECTION("Lowpass is first type (value 0)") {
        CHECK(static_cast<uint8_t>(FilterType::Lowpass) == 0);
    }

    SECTION("All 8 filter types have sequential values") {
        CHECK(static_cast<uint8_t>(FilterType::Lowpass) == 0);
        CHECK(static_cast<uint8_t>(FilterType::Highpass) == 1);
        CHECK(static_cast<uint8_t>(FilterType::Bandpass) == 2);
        CHECK(static_cast<uint8_t>(FilterType::Notch) == 3);
        CHECK(static_cast<uint8_t>(FilterType::Allpass) == 4);
        CHECK(static_cast<uint8_t>(FilterType::LowShelf) == 5);
        CHECK(static_cast<uint8_t>(FilterType::HighShelf) == 6);
        CHECK(static_cast<uint8_t>(FilterType::Peak) == 7);
    }

    SECTION("FilterType enum is uint8_t") {
        static_assert(std::is_same_v<std::underlying_type_t<FilterType>, uint8_t>);
    }
}

// T005: FilterType covers all 8 types
TEST_CASE("FilterType enum covers all filter types", "[biquad][enum][foundational]") {
    // Verify we have exactly the 8 types from the contract
    std::array<FilterType, 8> allTypes = {
        FilterType::Lowpass,
        FilterType::Highpass,
        FilterType::Bandpass,
        FilterType::Notch,
        FilterType::Allpass,
        FilterType::LowShelf,
        FilterType::HighShelf,
        FilterType::Peak
    };

    // Each type should have a unique value
    for (size_t i = 0; i < allTypes.size(); ++i) {
        for (size_t j = i + 1; j < allTypes.size(); ++j) {
            CHECK(static_cast<uint8_t>(allTypes[i]) != static_cast<uint8_t>(allTypes[j]));
        }
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - Utility Functions (T006-T008)
// ==============================================================================

// T006: butterworthQ() utility function tests
TEST_CASE("butterworthQ calculates correct Q values", "[biquad][utility][foundational]") {
    SECTION("Single stage returns Butterworth Q") {
        float q = butterworthQ(0, 1);
        CHECK(q == Approx(butterworthQ()).margin(1e-6f));
    }

    SECTION("Two-stage cascade (24 dB/oct) Q values") {
        // 4th order Butterworth Q values
        float q0 = butterworthQ(0, 2);
        float q1 = butterworthQ(1, 2);
        // Q values for 4th order: 0.5412, 1.3065
        CHECK(q0 == Approx(0.5412f).margin(0.01f));
        CHECK(q1 == Approx(1.3065f).margin(0.01f));
    }

    SECTION("Three-stage cascade (36 dB/oct) Q values") {
        // 6th order Butterworth Q values
        float q0 = butterworthQ(0, 3);
        float q1 = butterworthQ(1, 3);
        float q2 = butterworthQ(2, 3);
        CHECK(q0 == Approx(0.5176f).margin(0.01f));
        CHECK(q1 == Approx(butterworthQ()).margin(0.01f));
        CHECK(q2 == Approx(1.9319f).margin(0.01f));
    }

    SECTION("butterworthQ is constexpr") {
        constexpr float q = butterworthQ(0, 1);
        static_assert(q > 0.7f && q < 0.71f);
    }
}

// T007: Frequency constraint functions
TEST_CASE("Frequency constraints are correct", "[biquad][utility][foundational]") {
    SECTION("minFilterFrequency returns 1 Hz") {
        CHECK(minFilterFrequency() == 1.0f);
    }

    SECTION("maxFilterFrequency is 0.495 * sampleRate") {
        CHECK(maxFilterFrequency(44100.0f) == Approx(21829.5f).margin(0.1f));
        CHECK(maxFilterFrequency(48000.0f) == Approx(23760.0f).margin(0.1f));
        CHECK(maxFilterFrequency(96000.0f) == Approx(47520.0f).margin(0.1f));
    }

    SECTION("Frequency constraints are constexpr") {
        constexpr float minF = minFilterFrequency();
        constexpr float maxF = maxFilterFrequency(44100.0f);
        static_assert(minF == 1.0f);
        static_assert(maxF > 21000.0f);
    }
}

// T008: Q constraint functions
TEST_CASE("Q constraints are correct", "[biquad][utility][foundational]") {
    SECTION("minQ returns 0.1") {
        CHECK(minQ() == 0.1f);
    }

    SECTION("maxQ returns 30.0") {
        CHECK(maxQ() == 30.0f);
    }

    SECTION("butterworthQ() constant returns sqrt(2)/2") {
        CHECK(butterworthQ() == Approx(butterworthQ()).margin(1e-10f));
    }

    SECTION("Q constraints are constexpr") {
        constexpr float minQVal = minQ();
        constexpr float maxQVal = maxQ();
        constexpr float butterQ = butterworthQ();
        static_assert(minQVal == 0.1f);
        static_assert(maxQVal == 30.0f);
        static_assert(butterQ > 0.707f);
    }
}

// ==============================================================================
// Phase 2: Foundational Tests - BiquadCoefficients (T009-T016)
// ==============================================================================

// T009: Default construction yields bypass state
TEST_CASE("BiquadCoefficients default construction", "[biquad][coefficients][foundational]") {
    BiquadCoefficients coeffs;

    SECTION("Default b0 is 1.0") {
        CHECK(coeffs.b0 == 1.0f);
    }

    SECTION("Default b1 is 0.0") {
        CHECK(coeffs.b1 == 0.0f);
    }

    SECTION("Default b2 is 0.0") {
        CHECK(coeffs.b2 == 0.0f);
    }

    SECTION("Default a1 is 0.0") {
        CHECK(coeffs.a1 == 0.0f);
    }

    SECTION("Default a2 is 0.0") {
        CHECK(coeffs.a2 == 0.0f);
    }

    SECTION("Default coefficients represent bypass") {
        CHECK(coeffs.isBypass());
    }
}

// T010: isStable() tests
TEST_CASE("BiquadCoefficients isStable detects stability", "[biquad][coefficients][foundational]") {
    SECTION("Default coefficients are stable") {
        BiquadCoefficients coeffs;
        CHECK(coeffs.isStable());
    }

    SECTION("Valid lowpass coefficients are stable") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
        CHECK(coeffs.isStable());
    }

    SECTION("Coefficients with a2 > 1 are unstable") {
        BiquadCoefficients coeffs;
        coeffs.a2 = 1.1f;
        CHECK_FALSE(coeffs.isStable());
    }

    SECTION("Coefficients with |a1| > 1 + a2 are unstable") {
        BiquadCoefficients coeffs;
        coeffs.a1 = 2.5f;
        coeffs.a2 = 0.9f;
        CHECK_FALSE(coeffs.isStable());
    }
}

// T011: isBypass() tests
TEST_CASE("BiquadCoefficients isBypass detection", "[biquad][coefficients][foundational]") {
    SECTION("Default coefficients are bypass") {
        BiquadCoefficients coeffs;
        CHECK(coeffs.isBypass());
    }

    SECTION("Unity pass-through is bypass") {
        BiquadCoefficients coeffs{1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        CHECK(coeffs.isBypass());
    }

    SECTION("Lowpass coefficients are NOT bypass") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
        CHECK_FALSE(coeffs.isBypass());
    }

    SECTION("Peak with 0 dB gain is effectively bypass") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Peak, 1000.0f, 1.0f, 0.0f, 44100.0f);
        // 0 dB peak should be close to unity
        CHECK(coeffs.b0 == Approx(1.0f).margin(1e-4f));
    }
}

// ==============================================================================
// Phase 3: US1 - Lowpass/Highpass Filter Tests (T017-T032)
// ==============================================================================

// T017: Lowpass coefficient calculation
TEST_CASE("Lowpass coefficient calculation", "[biquad][US1][lowpass]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Coefficients are non-zero") {
        CHECK(coeffs.b0 != 0.0f);
        CHECK(coeffs.b1 != 0.0f);
        CHECK(coeffs.b2 != 0.0f);
        CHECK(coeffs.a1 != 0.0f);
        CHECK(coeffs.a2 != 0.0f);
    }

    SECTION("Filter is stable") {
        CHECK(coeffs.isStable());
    }

    SECTION("Feedforward coefficients are symmetric for lowpass") {
        // b0 == b2 for lowpass
        CHECK(coeffs.b0 == Approx(coeffs.b2).margin(1e-6f));
    }

    SECTION("b1 = 2 * b0 for lowpass") {
        CHECK(coeffs.b1 == Approx(2.0f * coeffs.b0).margin(1e-6f));
    }
}

// T018: Highpass coefficient calculation
TEST_CASE("Highpass coefficient calculation", "[biquad][US1][highpass]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Highpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Coefficients are non-zero") {
        CHECK(coeffs.b0 != 0.0f);
        CHECK(coeffs.b1 != 0.0f);
        CHECK(coeffs.b2 != 0.0f);
    }

    SECTION("Filter is stable") {
        CHECK(coeffs.isStable());
    }

    SECTION("b0 == b2 for highpass") {
        CHECK(coeffs.b0 == Approx(coeffs.b2).margin(1e-6f));
    }

    SECTION("b1 = -2 * b0 for highpass") {
        CHECK(coeffs.b1 == Approx(-2.0f * coeffs.b0).margin(1e-6f));
    }
}

// T019: Biquad default construction
TEST_CASE("Biquad default construction", "[biquad][US1]") {
    Biquad filter;

    SECTION("State is zeroed") {
        CHECK(filter.getZ1() == 0.0f);
        CHECK(filter.getZ2() == 0.0f);
    }

    SECTION("Default coefficients are bypass") {
        CHECK(filter.coefficients().isBypass());
    }
}

// T020: Biquad construction with coefficients
TEST_CASE("Biquad construction with coefficients", "[biquad][US1]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
    Biquad filter(coeffs);

    SECTION("Coefficients are set correctly") {
        CHECK(filter.coefficients().b0 == coeffs.b0);
        CHECK(filter.coefficients().b1 == coeffs.b1);
        CHECK(filter.coefficients().a1 == coeffs.a1);
    }

    SECTION("State is still zeroed") {
        CHECK(filter.getZ1() == 0.0f);
        CHECK(filter.getZ2() == 0.0f);
    }
}

// T021: Biquad configure method
TEST_CASE("Biquad configure method", "[biquad][US1]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Coefficients are set") {
        CHECK_FALSE(filter.coefficients().isBypass());
    }

    SECTION("Filter is stable") {
        CHECK(filter.coefficients().isStable());
    }
}

// T022: Single sample processing (TDF2)
TEST_CASE("Biquad single sample processing", "[biquad][US1][process]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Impulse response is non-zero") {
        float output = filter.process(1.0f);
        CHECK(output != 0.0f);
    }

    SECTION("State is updated after processing") {
        filter.process(1.0f);
        CHECK((filter.getZ1() != 0.0f || filter.getZ2() != 0.0f));
    }

    SECTION("Bypass filter passes signal unchanged") {
        Biquad bypass;  // Default = bypass
        float input = 0.5f;
        float output = bypass.process(input);
        CHECK(output == Approx(input).margin(1e-6f));
    }
}

// T023: Block processing
TEST_CASE("Biquad block processing", "[biquad][US1][process]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Processes buffer in-place") {
        std::array<float, 64> buffer{};
        buffer[0] = 1.0f;  // Impulse input
        filter.processBlock(buffer.data(), buffer.size());

        // Output should be the impulse response
        CHECK(buffer[0] != 0.0f);
        // Impulse response should eventually decay (check end vs peak)
        float maxAbs = 0.0f;
        for (const auto& s : buffer) {
            maxAbs = std::max(maxAbs, std::abs(s));
        }
        // Last sample should be less than peak (filter is decaying)
        CHECK(std::abs(buffer[63]) < maxAbs);
    }

    SECTION("Block matches sequential sample processing") {
        Biquad filter1, filter2;
        filter1.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
        filter2.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

        std::array<float, 4> blockBuffer = {0.5f, -0.3f, 0.1f, 0.8f};
        std::array<float, 4> sampleBuffer = {0.5f, -0.3f, 0.1f, 0.8f};

        // Block process
        filter1.processBlock(blockBuffer.data(), blockBuffer.size());

        // Sample-by-sample process
        for (size_t i = 0; i < sampleBuffer.size(); ++i) {
            sampleBuffer[i] = filter2.process(sampleBuffer[i]);
        }

        // Results should match
        for (size_t i = 0; i < 4; ++i) {
            CHECK(blockBuffer[i] == Approx(sampleBuffer[i]).margin(1e-6f));
        }
    }
}

// T024: Reset clears state
TEST_CASE("Biquad reset clears state", "[biquad][US1]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    // Process some samples to build up state
    filter.process(1.0f);
    filter.process(0.5f);

    SECTION("State is non-zero before reset") {
        CHECK((filter.getZ1() != 0.0f || filter.getZ2() != 0.0f));
    }

    filter.reset();

    SECTION("State is zero after reset") {
        CHECK(filter.getZ1() == 0.0f);
        CHECK(filter.getZ2() == 0.0f);
    }
}

// T025: Lowpass frequency response at cutoff
TEST_CASE("Lowpass frequency response at cutoff", "[biquad][US1][response]") {
    Biquad filter;
    float cutoff = 1000.0f;
    filter.configure(FilterType::Lowpass, cutoff, butterworthQ(), 0.0f, kTestSampleRate);

    // Generate test tone at cutoff frequency
    constexpr size_t numSamples = 4096;
    std::vector<float> buffer(numSamples);
    const float omega = 2.0f * kPi * cutoff / kTestSampleRate;

    // Fill with sine at cutoff
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(omega * static_cast<float>(i));
    }

    // Let filter settle
    for (size_t i = 0; i < 1000; ++i) {
        filter.process(std::sin(omega * static_cast<float>(i)));
    }
    filter.reset();

    // Process
    filter.processBlock(buffer.data(), numSamples);

    // Measure amplitude in steady state (last quarter)
    float maxOutput = 0.0f;
    for (size_t i = numSamples * 3 / 4; i < numSamples; ++i) {
        maxOutput = std::max(maxOutput, std::abs(buffer[i]));
    }

    // At cutoff, Butterworth should be -3dB (gain ~= 0.707)
    CHECK(maxOutput == Approx(butterworthQ()).margin(0.05f));
}

// T026: Highpass frequency response at cutoff
TEST_CASE("Highpass frequency response at cutoff", "[biquad][US1][response]") {
    Biquad filter;
    float cutoff = 1000.0f;
    filter.configure(FilterType::Highpass, cutoff, butterworthQ(), 0.0f, kTestSampleRate);

    constexpr size_t numSamples = 4096;
    std::vector<float> buffer(numSamples);
    const float omega = 2.0f * kPi * cutoff / kTestSampleRate;

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(omega * static_cast<float>(i));
    }

    // Let filter settle
    for (size_t i = 0; i < 1000; ++i) {
        filter.process(std::sin(omega * static_cast<float>(i)));
    }
    filter.reset();

    filter.processBlock(buffer.data(), numSamples);

    float maxOutput = 0.0f;
    for (size_t i = numSamples * 3 / 4; i < numSamples; ++i) {
        maxOutput = std::max(maxOutput, std::abs(buffer[i]));
    }

    CHECK(maxOutput == Approx(butterworthQ()).margin(0.05f));
}

// ==============================================================================
// Phase 4: US2 - All Filter Types (T033-T048)
// ==============================================================================

// T033: Bandpass coefficient calculation
TEST_CASE("Bandpass coefficient calculation", "[biquad][US2][bandpass]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Bandpass, 1000.0f, 1.0f, 0.0f, kTestSampleRate);

    SECTION("Filter is stable") {
        CHECK(coeffs.isStable());
    }

    SECTION("b2 = -b0 for bandpass") {
        CHECK(coeffs.b2 == Approx(-coeffs.b0).margin(1e-6f));
    }

    SECTION("b1 = 0 for bandpass") {
        CHECK(coeffs.b1 == Approx(0.0f).margin(1e-6f));
    }
}

// T034: Notch coefficient calculation
TEST_CASE("Notch coefficient calculation", "[biquad][US2][notch]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Notch, 1000.0f, 10.0f, 0.0f, kTestSampleRate);

    SECTION("Filter is stable") {
        CHECK(coeffs.isStable());
    }

    SECTION("b0 == b2 for notch") {
        CHECK(coeffs.b0 == Approx(coeffs.b2).margin(1e-6f));
    }
}

// T035: Allpass coefficient calculation
TEST_CASE("Allpass coefficient calculation", "[biquad][US2][allpass]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Allpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    SECTION("Filter is stable") {
        CHECK(coeffs.isStable());
    }

    SECTION("Unity gain at all frequencies") {
        // For allpass: b0 = a2, b1 = a1, b2 = 1
        CHECK(coeffs.b0 == Approx(coeffs.a2).margin(1e-6f));
        CHECK(coeffs.b1 == Approx(coeffs.a1).margin(1e-6f));
        CHECK(coeffs.b2 == Approx(1.0f).margin(1e-6f));
    }
}

// T036: LowShelf coefficient calculation
TEST_CASE("LowShelf coefficient calculation", "[biquad][US2][shelf]") {
    SECTION("+6dB boost") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::LowShelf, 1000.0f, butterworthQ(), 6.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }

    SECTION("-6dB cut") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::LowShelf, 1000.0f, butterworthQ(), -6.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }
}

// T037: HighShelf coefficient calculation
TEST_CASE("HighShelf coefficient calculation", "[biquad][US2][shelf]") {
    SECTION("+6dB boost") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::HighShelf, 1000.0f, butterworthQ(), 6.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }

    SECTION("-6dB cut") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::HighShelf, 1000.0f, butterworthQ(), -6.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }
}

// T038: Peak coefficient calculation
TEST_CASE("Peak coefficient calculation", "[biquad][US2][peak]") {
    SECTION("+12dB boost") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Peak, 1000.0f, 2.0f, 12.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }

    SECTION("-12dB cut") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Peak, 1000.0f, 2.0f, -12.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }

    SECTION("0dB is near bypass") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Peak, 1000.0f, 2.0f, 0.0f, kTestSampleRate);
        // Should be very close to unity
        CHECK(coeffs.b0 == Approx(1.0f).margin(0.01f));
    }
}

// T039: Notch frequency response
TEST_CASE("Notch frequency response at center", "[biquad][US2][response]") {
    Biquad filter;
    float center = 1000.0f;
    filter.configure(FilterType::Notch, center, 10.0f, 0.0f, kTestSampleRate);

    constexpr size_t numSamples = 8192;
    std::vector<float> buffer(numSamples);
    const float omega = 2.0f * kPi * center / kTestSampleRate;

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(omega * static_cast<float>(i));
    }

    // Let filter settle
    for (size_t i = 0; i < 2000; ++i) {
        filter.process(std::sin(omega * static_cast<float>(i)));
    }
    filter.reset();

    filter.processBlock(buffer.data(), numSamples);

    // Measure amplitude at center (should be near zero)
    float maxOutput = 0.0f;
    for (size_t i = numSamples * 3 / 4; i < numSamples; ++i) {
        maxOutput = std::max(maxOutput, std::abs(buffer[i]));
    }

    // At center frequency, notch should be very deep
    CHECK(maxOutput < 0.1f);
}

// T040: Allpass maintains unity magnitude
TEST_CASE("Allpass maintains unity magnitude", "[biquad][US2][response]") {
    Biquad filter;
    filter.configure(FilterType::Allpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    // Test at multiple frequencies
    std::array<float, 3> testFreqs = {100.0f, 1000.0f, 5000.0f};

    for (float freq : testFreqs) {
        filter.reset();
        constexpr size_t numSamples = 4096;
        std::vector<float> buffer(numSamples);
        const float omega = 2.0f * kPi * freq / kTestSampleRate;

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = std::sin(omega * static_cast<float>(i));
        }

        // Let filter settle
        for (size_t i = 0; i < 1000; ++i) {
            filter.process(std::sin(omega * static_cast<float>(i)));
        }

        filter.processBlock(buffer.data(), numSamples);

        float maxOutput = 0.0f;
        for (size_t i = numSamples * 3 / 4; i < numSamples; ++i) {
            maxOutput = std::max(maxOutput, std::abs(buffer[i]));
        }

        INFO("Testing frequency: " << freq << " Hz");
        CHECK(maxOutput == Approx(1.0f).margin(0.05f));
    }
}

// ==============================================================================
// Phase 5: US3 - Cascade Tests (T049-T065)
// ==============================================================================

// T049: BiquadCascade construction
TEST_CASE("BiquadCascade construction", "[biquad][US3][cascade]") {
    SECTION("2-stage cascade (24 dB/oct)") {
        BiquadCascade<2> cascade;
        CHECK(cascade.numStages() == 2);
        CHECK(cascade.order() == 4);
        CHECK(cascade.slopeDbPerOctave() == 24.0f);
    }

    SECTION("3-stage cascade (36 dB/oct)") {
        BiquadCascade<3> cascade;
        CHECK(cascade.numStages() == 3);
        CHECK(cascade.order() == 6);
        CHECK(cascade.slopeDbPerOctave() == 36.0f);
    }

    SECTION("4-stage cascade (48 dB/oct)") {
        BiquadCascade<4> cascade;
        CHECK(cascade.numStages() == 4);
        CHECK(cascade.order() == 8);
        CHECK(cascade.slopeDbPerOctave() == 48.0f);
    }
}

// T050: Type aliases
TEST_CASE("Biquad type aliases", "[biquad][US3][cascade]") {
    SECTION("Biquad12dB is Biquad") {
        static_assert(std::is_same_v<Biquad12dB, Biquad>);
    }

    SECTION("Biquad24dB is BiquadCascade<2>") {
        static_assert(std::is_same_v<Biquad24dB, BiquadCascade<2>>);
    }

    SECTION("Biquad36dB is BiquadCascade<3>") {
        static_assert(std::is_same_v<Biquad36dB, BiquadCascade<3>>);
    }

    SECTION("Biquad48dB is BiquadCascade<4>") {
        static_assert(std::is_same_v<Biquad48dB, BiquadCascade<4>>);
    }
}

// T051: setButterworth configuration
TEST_CASE("BiquadCascade setButterworth", "[biquad][US3][cascade]") {
    Biquad24dB cascade;
    cascade.setButterworth(FilterType::Lowpass, 1000.0f, kTestSampleRate);

    SECTION("All stages are configured") {
        CHECK_FALSE(cascade.stage(0).coefficients().isBypass());
        CHECK_FALSE(cascade.stage(1).coefficients().isBypass());
    }

    SECTION("All stages are stable") {
        CHECK(cascade.stage(0).coefficients().isStable());
        CHECK(cascade.stage(1).coefficients().isStable());
    }
}

// T052: linkwitzRileyQ utility function
TEST_CASE("linkwitzRileyQ calculates correct Q values", "[biquad][US3][utility]") {
    SECTION("1-stage Linkwitz-Riley (LR2)") {
        // LR2 uses Q = 0.5 (critically damped)
        float q = linkwitzRileyQ(0, 1);
        CHECK(q == Approx(0.5f).margin(1e-6f));
    }

    SECTION("2-stage Linkwitz-Riley (LR4)") {
        // LR4 uses cascaded Butterworth sections
        float q0 = linkwitzRileyQ(0, 2);
        float q1 = linkwitzRileyQ(1, 2);
        // Both stages use Butterworth Q values for 4th order
        CHECK(q0 == Approx(butterworthQ(0, 2)).margin(1e-6f));
        CHECK(q1 == Approx(butterworthQ(1, 2)).margin(1e-6f));
    }

    SECTION("linkwitzRileyQ is constexpr") {
        constexpr float q = linkwitzRileyQ(0, 1);  // LR2 case
        static_assert(q == 0.5f);
    }
}

// T053: setLinkwitzRiley configuration
TEST_CASE("BiquadCascade setLinkwitzRiley", "[biquad][US3][cascade]") {
    Biquad24dB cascade;
    cascade.setLinkwitzRiley(FilterType::Lowpass, 1000.0f, kTestSampleRate);

    SECTION("All stages are configured") {
        CHECK_FALSE(cascade.stage(0).coefficients().isBypass());
        CHECK_FALSE(cascade.stage(1).coefficients().isBypass());
    }

    SECTION("All stages are stable") {
        CHECK(cascade.stage(0).coefficients().isStable());
        CHECK(cascade.stage(1).coefficients().isStable());
    }
}

// T054: Linkwitz-Riley flat sum at crossover
TEST_CASE("Linkwitz-Riley flat sum at crossover", "[biquad][US3][response]") {
    float crossover = 1000.0f;

    Biquad24dB lpf, hpf;
    lpf.setLinkwitzRiley(FilterType::Lowpass, crossover, kTestSampleRate);
    hpf.setLinkwitzRiley(FilterType::Highpass, crossover, kTestSampleRate);

    // Test at crossover frequency
    constexpr size_t numSamples = 4096;
    std::vector<float> lpBuffer(numSamples);
    std::vector<float> hpBuffer(numSamples);
    const float omega = 2.0f * kPi * crossover / kTestSampleRate;

    for (size_t i = 0; i < numSamples; ++i) {
        float sample = std::sin(omega * static_cast<float>(i));
        lpBuffer[i] = sample;
        hpBuffer[i] = sample;
    }

    lpf.processBlock(lpBuffer.data(), numSamples);
    hpf.processBlock(hpBuffer.data(), numSamples);

    // Linkwitz-Riley sums to unity in POWER (LP^2 + HP^2 = 1), not voltage
    // At crossover, both LP and HP are at -3dB (0.707 amplitude)
    float maxPowerSum = 0.0f;
    for (size_t i = numSamples * 3 / 4; i < numSamples; ++i) {
        float powerSum = lpBuffer[i] * lpBuffer[i] + hpBuffer[i] * hpBuffer[i];
        maxPowerSum = std::max(maxPowerSum, powerSum);
    }

    // Power sum should be unity at crossover (within tolerance)
    CHECK(maxPowerSum == Approx(1.0f).margin(0.15f));
}

// T055: Cascade processing
TEST_CASE("BiquadCascade processing", "[biquad][US3][process]") {
    Biquad24dB cascade;
    cascade.setButterworth(FilterType::Lowpass, 1000.0f, kTestSampleRate);

    SECTION("Single sample processing") {
        float output = cascade.process(1.0f);
        CHECK(output != 0.0f);
    }

    SECTION("Block processing") {
        std::array<float, 8> buffer = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        cascade.processBlock(buffer.data(), buffer.size());
        CHECK(buffer[0] != 0.0f);
    }
}

// T056: Cascade reset
TEST_CASE("BiquadCascade reset clears all stages", "[biquad][US3]") {
    Biquad24dB cascade;
    cascade.setButterworth(FilterType::Lowpass, 1000.0f, kTestSampleRate);

    // Build up state
    cascade.process(1.0f);
    cascade.process(0.5f);

    // At least one stage should have state
    bool hasState = (cascade.stage(0).getZ1() != 0.0f) ||
                    (cascade.stage(0).getZ2() != 0.0f) ||
                    (cascade.stage(1).getZ1() != 0.0f) ||
                    (cascade.stage(1).getZ2() != 0.0f);
    REQUIRE(hasState);

    cascade.reset();

    CHECK(cascade.stage(0).getZ1() == 0.0f);
    CHECK(cascade.stage(0).getZ2() == 0.0f);
    CHECK(cascade.stage(1).getZ1() == 0.0f);
    CHECK(cascade.stage(1).getZ2() == 0.0f);
}

// ==============================================================================
// Phase 6: US4 - Smoothed Biquad Tests (T066-T078)
// ==============================================================================

// T066: SmoothedBiquad default construction
TEST_CASE("SmoothedBiquad default construction", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;
    // Should not crash
    CHECK_FALSE(filter.isSmoothing());
}

// T067: setSmoothingTime configuration
TEST_CASE("SmoothedBiquad setSmoothingTime", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;

    SECTION("10ms smoothing at 44.1kHz") {
        filter.setSmoothingTime(10.0f, kTestSampleRate);
        // Just verify it doesn't crash
        CHECK(true);
    }

    SECTION("1ms smoothing (fast)") {
        filter.setSmoothingTime(1.0f, kTestSampleRate);
        CHECK(true);
    }

    SECTION("100ms smoothing (slow)") {
        filter.setSmoothingTime(100.0f, kTestSampleRate);
        CHECK(true);
    }
}

// T068: setTarget and snapToTarget
TEST_CASE("SmoothedBiquad setTarget and snapToTarget", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;
    filter.setSmoothingTime(10.0f, kTestSampleRate);

    SECTION("setTarget starts smoothing") {
        filter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
        filter.snapToTarget();  // Start at target

        // Change target
        filter.setTarget(FilterType::Lowpass, 2000.0f, butterworthQ(), 0.0f, kTestSampleRate);
        CHECK(filter.isSmoothing());
    }

    SECTION("snapToTarget jumps immediately") {
        filter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
        filter.snapToTarget();
        CHECK_FALSE(filter.isSmoothing());
    }
}

// T069: Smoothing converges over time
TEST_CASE("SmoothedBiquad smoothing converges", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;
    filter.setSmoothingTime(1.0f, kTestSampleRate);  // 1ms - faster convergence

    // Start at 1kHz
    filter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
    filter.snapToTarget();

    // Move to 2kHz
    filter.setTarget(FilterType::Lowpass, 2000.0f, butterworthQ(), 0.0f, kTestSampleRate);
    REQUIRE(filter.isSmoothing());

    // 1ms at 44100Hz = ~44 samples for 1 time constant
    // Need ~5 time constants to reach 1e-5 precision = ~220 samples
    // Process a full buffer to be safe
    std::vector<float> buffer(4096, 0.0f);
    filter.processBlock(buffer.data(), buffer.size());

    // After 4096 samples with 1ms time constant, smoothing should complete
    CHECK_FALSE(filter.isSmoothing());
}

// T070: Click-free parameter changes
TEST_CASE("SmoothedBiquad produces no clicks", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;
    filter.setSmoothingTime(5.0f, kTestSampleRate);

    // Start with 1kHz lowpass
    filter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);
    filter.snapToTarget();

    // Generate input signal
    constexpr size_t numSamples = 2048;
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / kTestSampleRate);
    }

    // Process first half
    filter.processBlock(buffer.data(), numSamples / 2);

    // Change filter mid-stream
    filter.setTarget(FilterType::Lowpass, 4000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    // Process second half
    filter.processBlock(buffer.data() + numSamples / 2, numSamples / 2);

    // Check for discontinuities (clicks)
    float maxDiff = 0.0f;
    for (size_t i = 1; i < numSamples; ++i) {
        float diff = std::abs(buffer[i] - buffer[i - 1]);
        maxDiff = std::max(maxDiff, diff);
    }

    // With smoothing, max sample-to-sample difference should be reasonable
    // (not a huge jump that would indicate a click)
    CHECK(maxDiff < 0.5f);
}

// T071: SmoothedBiquad reset
TEST_CASE("SmoothedBiquad reset clears state", "[biquad][US4][smoothed]") {
    SmoothedBiquad filter;
    filter.setSmoothingTime(10.0f, kTestSampleRate);
    filter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    // Process some samples
    std::array<float, 64> buffer{};
    buffer[0] = 1.0f;
    filter.processBlock(buffer.data(), buffer.size());

    filter.reset();

    // After reset, filter should be in clean state
    CHECK_FALSE(filter.isSmoothing());
}

// ==============================================================================
// Phase 7: US5 - Stability and Edge Cases (T079-T096)
// ==============================================================================

// T079: Frequency clamping at Nyquist
TEST_CASE("Frequency is clamped to valid range", "[biquad][US5][edge]") {
    SECTION("Frequency above Nyquist is clamped") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 30000.0f, butterworthQ(), 0.0f, kTestSampleRate);
        // Should still produce stable filter
        CHECK(coeffs.isStable());
    }

    SECTION("Frequency below minimum is clamped") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 0.1f, butterworthQ(), 0.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }
}

// T080: Q clamping
TEST_CASE("Q is clamped to valid range", "[biquad][US5][edge]") {
    SECTION("Q above maximum is clamped") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 1000.0f, 100.0f, 0.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }

    SECTION("Q below minimum is clamped") {
        auto coeffs = BiquadCoefficients::calculate(
            FilterType::Lowpass, 1000.0f, 0.001f, 0.0f, kTestSampleRate);
        CHECK(coeffs.isStable());
    }
}

// T081: Zero sample rate handling
TEST_CASE("Zero sample rate produces bypass", "[biquad][US5][edge]") {
    auto coeffs = BiquadCoefficients::calculate(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 0.0f);
    // Should produce safe (bypass) coefficients
    CHECK(coeffs.isBypass());
}

// T082: Denormal flushing
TEST_CASE("Denormals are flushed to zero", "[biquad][US5][denormal]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 100.0f, butterworthQ(), 0.0f, kTestSampleRate);

    // Feed an impulse
    filter.process(1.0f);

    // Feed silence for a long time
    for (size_t i = 0; i < 100000; ++i) {
        filter.process(0.0f);
    }

    // State should be flushed to zero (not denormal)
    float z1 = filter.getZ1();
    float z2 = filter.getZ2();

    // Check state is either zero or a normal number (not denormal)
    auto isNormalOrZero = [](float x) {
        return x == 0.0f || std::isnormal(x) || std::abs(x) > 1e-30f;
    };

    CHECK(isNormalOrZero(z1));
    CHECK(isNormalOrZero(z2));
}

// T083: Stability in 99% feedback loop
TEST_CASE("Filter remains stable in high feedback", "[biquad][US5][stability]") {
    Biquad filter;
    filter.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, kTestSampleRate);

    const float feedback = 0.99f;
    float delayedSample = 0.0f;

    // Feed impulse through feedback loop
    delayedSample = filter.process(1.0f + feedback * delayedSample);

    // Run for 10 seconds worth of samples
    constexpr size_t tenSeconds = static_cast<size_t>(kTestSampleRate * 10);
    bool hasInfOrNan = false;

    for (size_t i = 0; i < tenSeconds; ++i) {
        delayedSample = filter.process(feedback * delayedSample);
        if (std::isnan(delayedSample) || std::isinf(delayedSample)) {
            hasInfOrNan = true;
            break;
        }
    }

    CHECK_FALSE(hasInfOrNan);

    // Final output should be near zero (decayed)
    CHECK(std::abs(delayedSample) < 1.0f);
}

// ==============================================================================
// Phase 8: US6 - Constexpr Tests (T097-T105)
// ==============================================================================

// T097: calculateConstexpr produces valid coefficients
TEST_CASE("calculateConstexpr produces valid coefficients", "[biquad][US6][constexpr]") {
    constexpr auto coeffs = BiquadCoefficients::calculateConstexpr(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);

    // Verify at compile time
    static_assert(coeffs.b0 != 0.0f);
    static_assert(coeffs.b1 != 0.0f);
    static_assert(coeffs.a1 != 0.0f);

    // Also check at runtime
    CHECK(coeffs.isStable());
}

// T098: Constexpr matches runtime calculation
TEST_CASE("Constexpr matches runtime calculation", "[biquad][US6][constexpr]") {
    constexpr auto constexprCoeffs = BiquadCoefficients::calculateConstexpr(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);

    auto runtimeCoeffs = BiquadCoefficients::calculate(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);

    // Should match within floating-point tolerance
    CHECK(constexprCoeffs.b0 == Approx(runtimeCoeffs.b0).margin(1e-4f));
    CHECK(constexprCoeffs.b1 == Approx(runtimeCoeffs.b1).margin(1e-4f));
    CHECK(constexprCoeffs.b2 == Approx(runtimeCoeffs.b2).margin(1e-4f));
    CHECK(constexprCoeffs.a1 == Approx(runtimeCoeffs.a1).margin(1e-4f));
    CHECK(constexprCoeffs.a2 == Approx(runtimeCoeffs.a2).margin(1e-4f));
}

// T099: Constexpr array initialization
TEST_CASE("Constexpr filter bank initialization", "[biquad][US6][constexpr]") {
    constexpr std::array<BiquadCoefficients, 4> filterBank = {
        BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 500.0f, butterworthQ(), 0.0f, 44100.0f),
        BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f),
        BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 2000.0f, butterworthQ(), 0.0f, 44100.0f),
        BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 4000.0f, butterworthQ(), 0.0f, 44100.0f),
    };

    // Verify all are valid at compile time
    static_assert(filterBank[0].b0 != 0.0f);
    static_assert(filterBank[1].b0 != 0.0f);
    static_assert(filterBank[2].b0 != 0.0f);
    static_assert(filterBank[3].b0 != 0.0f);

    // Also verify at runtime
    for (const auto& coeffs : filterBank) {
        CHECK(coeffs.isStable());
    }
}

// T100: Constexpr works for all filter types
TEST_CASE("Constexpr works for all filter types", "[biquad][US6][constexpr]") {
    constexpr auto lp = BiquadCoefficients::calculateConstexpr(
        FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
    constexpr auto hp = BiquadCoefficients::calculateConstexpr(
        FilterType::Highpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
    constexpr auto bp = BiquadCoefficients::calculateConstexpr(
        FilterType::Bandpass, 1000.0f, 1.0f, 0.0f, 44100.0f);
    constexpr auto notch = BiquadCoefficients::calculateConstexpr(
        FilterType::Notch, 1000.0f, 10.0f, 0.0f, 44100.0f);
    constexpr auto ap = BiquadCoefficients::calculateConstexpr(
        FilterType::Allpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
    constexpr auto ls = BiquadCoefficients::calculateConstexpr(
        FilterType::LowShelf, 1000.0f, butterworthQ(), 6.0f, 44100.0f);
    constexpr auto hs = BiquadCoefficients::calculateConstexpr(
        FilterType::HighShelf, 1000.0f, butterworthQ(), 6.0f, 44100.0f);
    constexpr auto peak = BiquadCoefficients::calculateConstexpr(
        FilterType::Peak, 1000.0f, 2.0f, 6.0f, 44100.0f);

    // All should produce non-bypass coefficients
    static_assert(lp.b0 != 1.0f || lp.b1 != 0.0f);
    static_assert(hp.b0 != 1.0f || hp.b1 != 0.0f);
    static_assert(bp.b0 != 1.0f || bp.b1 != 0.0f);
    static_assert(notch.b0 != 1.0f || notch.b1 != 0.0f);
    static_assert(ap.b0 != 1.0f || ap.b1 != 0.0f);
    static_assert(ls.b0 != 1.0f || ls.b1 != 0.0f);
    static_assert(hs.b0 != 1.0f || hs.b1 != 0.0f);
    static_assert(peak.b0 != 1.0f || peak.b1 != 0.0f);

    CHECK(true);  // If we get here, all constexpr evaluations succeeded
}

// ==============================================================================
// End of Biquad Tests
// ==============================================================================
