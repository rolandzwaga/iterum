// ==============================================================================
// dB/Linear Conversion Utilities - Unit Tests
// ==============================================================================
// Layer 0: Core Utilities
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: src/dsp/core/db_utils.h
// Contract: specs/001-db-conversion/contracts/db_utils.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/core/db_utils.h"

#include <array>
#include <cmath>
#include <limits>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// User Story 1: dbToGain Function Tests
// ==============================================================================
// Formula: gain = 10^(dB/20)
// Reference: specs/001-db-conversion/spec.md US1

TEST_CASE("dbToGain converts decibels to linear gain", "[dsp][core][db_utils][US1]") {

    SECTION("T007: 0 dB returns exactly 1.0 (unity gain)") {
        REQUIRE(dbToGain(0.0f) == 1.0f);
    }

    SECTION("T008: -20 dB returns 0.1") {
        REQUIRE(dbToGain(-20.0f) == Approx(0.1f));
    }

    SECTION("T009: +20 dB returns 10.0") {
        REQUIRE(dbToGain(20.0f) == Approx(10.0f));
    }

    SECTION("T010: -6.0206 dB returns approximately 0.5") {
        // -6.0206 dB = 20 * log10(0.5) = -6.0205999...
        REQUIRE(dbToGain(-6.0206f) == Approx(0.5f).margin(0.001f));
    }

    SECTION("T011: NaN input returns 0.0 (safe fallback)") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(dbToGain(nan) == 0.0f);
    }

    SECTION("T012: Extreme values return valid results without overflow") {
        // +200 dB should return a large but finite value
        float highGain = dbToGain(200.0f);
        REQUIRE(std::isfinite(highGain));
        REQUIRE(highGain > 0.0f);

        // -200 dB should return a very small but positive value
        float lowGain = dbToGain(-200.0f);
        REQUIRE(std::isfinite(lowGain));
        REQUIRE(lowGain > 0.0f);
        REQUIRE(lowGain < 1e-9f);
    }
}

// Additional dbToGain tests for comprehensive coverage

TEST_CASE("dbToGain formula verification", "[dsp][core][db_utils][US1]") {

    SECTION("+6 dB is approximately double") {
        // +6.0206 dB = 20 * log10(2) = 6.0205999...
        REQUIRE(dbToGain(6.0206f) == Approx(2.0f).margin(0.001f));
    }

    SECTION("-40 dB returns 0.01") {
        REQUIRE(dbToGain(-40.0f) == Approx(0.01f));
    }

    SECTION("-60 dB returns 0.001") {
        REQUIRE(dbToGain(-60.0f) == Approx(0.001f));
    }

    SECTION("Negative infinity returns 0") {
        float negInf = -std::numeric_limits<float>::infinity();
        // 10^(-inf/20) = 10^(-inf) = 0
        REQUIRE(dbToGain(negInf) == 0.0f);
    }
}

// ==============================================================================
// User Story 2: gainToDb Function Tests
// ==============================================================================
// Formula: dB = 20 * log10(gain), with -144 dB floor
// Reference: specs/001-db-conversion/spec.md US2
// Note: US3 (Handle Silence Safely) is integrated into these tests (T022-T025)

TEST_CASE("gainToDb converts linear gain to decibels", "[dsp][core][db_utils][US2]") {

    SECTION("T018: 1.0 returns exactly 0.0 dB (unity gain)") {
        REQUIRE(gainToDb(1.0f) == 0.0f);
    }

    SECTION("T019: 0.1 returns -20.0 dB") {
        REQUIRE(gainToDb(0.1f) == Approx(-20.0f));
    }

    SECTION("T020: 10.0 returns +20.0 dB") {
        REQUIRE(gainToDb(10.0f) == Approx(20.0f));
    }

    SECTION("T021: 0.5 returns approximately -6.02 dB") {
        // 20 * log10(0.5) = -6.0205999...
        REQUIRE(gainToDb(0.5f) == Approx(-6.0206f).margin(0.01f));
    }

    // US3 (Silence Handling) integrated tests
    SECTION("T022: 0.0 (silence) returns -144.0 dB floor") {
        REQUIRE(gainToDb(0.0f) == -144.0f);
    }

    SECTION("T023: -1.0 (negative/invalid) returns -144.0 dB floor") {
        REQUIRE(gainToDb(-1.0f) == -144.0f);
    }

    SECTION("T024: NaN returns -144.0 dB floor (safe fallback)") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(gainToDb(nan) == -144.0f);
    }

    SECTION("T025: Very small value (1e-10) returns -144.0 dB floor") {
        // 20 * log10(1e-10) = -200 dB, but clamped to floor
        REQUIRE(gainToDb(1e-10f) == -144.0f);
    }

    SECTION("T026: kSilenceFloorDb constant equals -144.0f") {
        REQUIRE(kSilenceFloorDb == -144.0f);
    }
}

// Additional gainToDb tests for comprehensive coverage

TEST_CASE("gainToDb formula verification", "[dsp][core][db_utils][US2]") {

    SECTION("2.0 is approximately +6 dB") {
        REQUIRE(gainToDb(2.0f) == Approx(6.0206f).margin(0.01f));
    }

    SECTION("0.01 returns -40 dB") {
        REQUIRE(gainToDb(0.01f) == Approx(-40.0f));
    }

    SECTION("0.001 returns -60 dB") {
        REQUIRE(gainToDb(0.001f) == Approx(-60.0f));
    }

    SECTION("Positive infinity returns positive infinity") {
        float posInf = std::numeric_limits<float>::infinity();
        // log10(+inf) = +inf, so 20 * log10(+inf) = +inf
        float result = gainToDb(posInf);
        REQUIRE(std::isinf(result));
        REQUIRE(result > 0.0f);
    }
}

TEST_CASE("dbToGain and gainToDb are inverse operations", "[dsp][core][db_utils][US1][US2]") {
    const float testGainValues[] = {0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 10.0f};

    for (float gain : testGainValues) {
        float dB = gainToDb(gain);
        float backToGain = dbToGain(dB);
        REQUIRE(backToGain == Approx(gain).margin(0.0001f));
    }

    const float testDbValues[] = {-40.0f, -20.0f, -6.0f, 0.0f, 6.0f, 20.0f};

    for (float dB : testDbValues) {
        float gain = dbToGain(dB);
        float backToDb = gainToDb(gain);
        REQUIRE(backToDb == Approx(dB).margin(0.0001f));
    }
}

// ==============================================================================
// User Story 4: Compile-Time Evaluation Tests
// ==============================================================================
// Verify functions work in constexpr context for compile-time constant initialization
// Reference: specs/001-db-conversion/spec.md US4

TEST_CASE("dbToGain is constexpr", "[dsp][core][db_utils][US4][constexpr]") {

    SECTION("T032: constexpr dbToGain compiles and equals runtime result") {
        // Compile-time evaluation
        constexpr float gain = dbToGain(-6.0f);

        // Runtime evaluation for comparison
        float runtimeGain = dbToGain(-6.0f);

        // Verify they match
        REQUIRE(gain == runtimeGain);
        REQUIRE(gain == Approx(0.501187f).margin(0.0001f));
    }

    SECTION("constexpr dbToGain with 0 dB") {
        constexpr float unity = dbToGain(0.0f);
        REQUIRE(unity == 1.0f);
    }

    SECTION("constexpr dbToGain with -20 dB") {
        constexpr float tenth = dbToGain(-20.0f);
        REQUIRE(tenth == Approx(0.1f));
    }
}

TEST_CASE("gainToDb is constexpr", "[dsp][core][db_utils][US4][constexpr]") {

    SECTION("T033: constexpr gainToDb compiles and equals runtime result") {
        // Compile-time evaluation
        constexpr float dB = gainToDb(0.5f);

        // Runtime evaluation for comparison
        float runtimeDb = gainToDb(0.5f);

        // Verify they match (use Approx for floating-point comparison)
        REQUIRE(dB == Approx(runtimeDb).margin(0.0001f));
        REQUIRE(dB == Approx(-6.0206f).margin(0.01f));
    }

    SECTION("constexpr gainToDb with unity") {
        constexpr float zeroDB = gainToDb(1.0f);
        REQUIRE(zeroDB == 0.0f);
    }

    SECTION("constexpr gainToDb with silence") {
        constexpr float floor = gainToDb(0.0f);
        REQUIRE(floor == kSilenceFloorDb);
    }
}

TEST_CASE("constexpr array initialization", "[dsp][core][db_utils][US4][constexpr]") {

    SECTION("T034: std::array with constexpr converted values compiles") {
        // This MUST compile - constexpr array initialization
        constexpr std::array<float, 5> gains = {
            dbToGain(-40.0f),  // 0.01
            dbToGain(-20.0f),  // 0.1
            dbToGain(-6.0f),   // ~0.5
            dbToGain(0.0f),    // 1.0
            dbToGain(20.0f)    // 10.0
        };

        // Verify values are correct
        REQUIRE(gains[0] == Approx(0.01f));
        REQUIRE(gains[1] == Approx(0.1f));
        REQUIRE(gains[2] == Approx(0.501187f).margin(0.0001f));
        REQUIRE(gains[3] == 1.0f);
        REQUIRE(gains[4] == Approx(10.0f));
    }

    SECTION("constexpr dB lookup table") {
        // Pre-computed dB values at compile time
        constexpr std::array<float, 4> dbValues = {
            gainToDb(0.1f),   // -20 dB
            gainToDb(0.5f),   // ~-6 dB
            gainToDb(1.0f),   // 0 dB
            gainToDb(2.0f)    // ~+6 dB
        };

        REQUIRE(dbValues[0] == Approx(-20.0f));
        REQUIRE(dbValues[1] == Approx(-6.0206f).margin(0.01f));
        REQUIRE(dbValues[2] == 0.0f);
        REQUIRE(dbValues[3] == Approx(6.0206f).margin(0.01f));
    }
}

TEST_CASE("kSilenceFloorDb is constexpr", "[dsp][core][db_utils][US4][constexpr]") {

    SECTION("Can be used in constexpr context") {
        constexpr float floor = kSilenceFloorDb;
        REQUIRE(floor == -144.0f);
    }

    SECTION("Can initialize constexpr array") {
        constexpr std::array<float, 2> floors = {kSilenceFloorDb, kSilenceFloorDb + 6.0f};
        REQUIRE(floors[0] == -144.0f);
        REQUIRE(floors[1] == -138.0f);
    }
}
