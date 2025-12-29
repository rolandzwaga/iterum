// ==============================================================================
// Digital Width Parameter Unit Tests
// ==============================================================================
// Tests for Digital Delay width parameter (spec 036)
// Verifies parameter ID, normalization, change handling, and state persistence.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"
#include "parameters/digital_params.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace Iterum;
using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// ==============================================================================
// Test: Parameter ID Definition
// ==============================================================================

TEST_CASE("kDigitalWidthId is defined correctly", "[vst][parameter][digital][width]") {
    SECTION("ID value is 612") {
        REQUIRE(kDigitalWidthId == 612);
    }

    SECTION("ID is in Digital Delay range (600-699)") {
        REQUIRE(kDigitalWidthId >= 600);
        REQUIRE(kDigitalWidthId < 700);
    }
}

// ==============================================================================
// Test: Parameter Registration
// ==============================================================================

TEST_CASE("Width parameter registration", "[vst][parameter][digital][width]") {
    ParameterContainer parameters;
    registerDigitalParams(parameters);

    Parameter* widthParam = parameters.getParameter(kDigitalWidthId);
    REQUIRE(widthParam != nullptr);

    const ParameterInfo& info = widthParam->getInfo();

    SECTION("Parameter is automatable") {
        REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    }

    SECTION("Default value is 100% (normalized 0.5)") {
        REQUIRE(widthParam->getInfo().defaultNormalizedValue == Approx(0.5));
    }

    SECTION("Parameter has correct title") {
        // Convert TChar to ASCII for comparison
        std::string title;
        for (int i = 0; i < 128 && info.title[i] != 0; ++i) {
            if (info.title[i] < 128) {
                title += static_cast<char>(info.title[i]);
            }
        }
        REQUIRE(title == "Digital Width");
    }

    SECTION("Parameter has correct unit (%)") {
        std::string unit;
        for (int i = 0; i < 128 && info.units[i] != 0; ++i) {
            if (info.units[i] < 128) {
                unit += static_cast<char>(info.units[i]);
            }
        }
        REQUIRE(unit == "%");
    }
}

// ==============================================================================
// Test: Normalization and Denormalization
// ==============================================================================

TEST_CASE("Width parameter normalization", "[vst][parameter][digital][width]") {
    DigitalParams params;

    SECTION("0% width (normalized 0.0)") {
        handleDigitalParamChange(params, kDigitalWidthId, 0.0);
        REQUIRE(params.width.load() == Approx(0.0f));
    }

    SECTION("100% width (normalized 0.5) - default") {
        handleDigitalParamChange(params, kDigitalWidthId, 0.5);
        REQUIRE(params.width.load() == Approx(100.0f));
    }

    SECTION("200% width (normalized 1.0)") {
        handleDigitalParamChange(params, kDigitalWidthId, 1.0);
        REQUIRE(params.width.load() == Approx(200.0f));
    }

    SECTION("50% width (normalized 0.25)") {
        handleDigitalParamChange(params, kDigitalWidthId, 0.25);
        REQUIRE(params.width.load() == Approx(50.0f));
    }

    SECTION("150% width (normalized 0.75)") {
        handleDigitalParamChange(params, kDigitalWidthId, 0.75);
        REQUIRE(params.width.load() == Approx(150.0f));
    }
}

// ==============================================================================
// Test: Display Formatting
// ==============================================================================

TEST_CASE("Width parameter display formatting", "[vst][parameter][digital][width]") {
    String128 buffer;

    SECTION("0% displays correctly") {
        formatDigitalParam(kDigitalWidthId, 0.0, buffer);
        std::string result;
        for (int i = 0; i < 128 && buffer[i] != 0; ++i) {
            if (buffer[i] < 128) result += static_cast<char>(buffer[i]);
        }
        REQUIRE(result == "0%");
    }

    SECTION("100% displays correctly") {
        formatDigitalParam(kDigitalWidthId, 0.5, buffer);
        std::string result;
        for (int i = 0; i < 128 && buffer[i] != 0; ++i) {
            if (buffer[i] < 128) result += static_cast<char>(buffer[i]);
        }
        REQUIRE(result == "100%");
    }

    SECTION("200% displays correctly") {
        formatDigitalParam(kDigitalWidthId, 1.0, buffer);
        std::string result;
        for (int i = 0; i < 128 && buffer[i] != 0; ++i) {
            if (buffer[i] < 128) result += static_cast<char>(buffer[i]);
        }
        REQUIRE(result == "200%");
    }

    SECTION("75% displays correctly") {
        formatDigitalParam(kDigitalWidthId, 0.375, buffer);
        std::string result;
        for (int i = 0; i < 128 && buffer[i] != 0; ++i) {
            if (buffer[i] < 128) result += static_cast<char>(buffer[i]);
        }
        REQUIRE(result == "75%");
    }
}

// ==============================================================================
// Test: Default Value
// ==============================================================================

TEST_CASE("Width parameter default value", "[vst][parameter][digital][width]") {
    DigitalParams params;

    SECTION("Default width is 100%") {
        // Default initialized value should be 100%
        REQUIRE(params.width.load() == Approx(100.0f));
    }
}

// ==============================================================================
// Test: Thread Safety
// ==============================================================================

TEST_CASE("Width parameter is thread-safe", "[vst][parameter][digital][width][atomic]") {
    DigitalParams params;

    SECTION("Atomic store and load") {
        params.width.store(150.0f, std::memory_order_relaxed);
        float loaded = params.width.load(std::memory_order_relaxed);
        REQUIRE(loaded == Approx(150.0f));
    }

    SECTION("Multiple stores") {
        params.width.store(50.0f, std::memory_order_relaxed);
        params.width.store(100.0f, std::memory_order_relaxed);
        params.width.store(150.0f, std::memory_order_relaxed);
        REQUIRE(params.width.load() == Approx(150.0f));
    }
}
