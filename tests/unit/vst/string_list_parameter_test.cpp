// ==============================================================================
// StringListParameter Unit Tests
// ==============================================================================
// Tests for VST3 StringListParameter behavior to diagnose dropdown issues.
// Specifically tests the difference between:
//   1. Direct appendString calls (known working)
//   2. Helper function with initializer_list (suspected encoding issue)
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Provide main for Catch2
int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"
#include "controller/parameter_helpers.h"

#include <string>
#include <vector>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

// Helper to convert TChar string to std::string for comparison
static std::string tcharToString(const TChar* tstr) {
    if (!tstr) return "";

    std::string result;
    while (*tstr) {
        // TChar is char16_t on Windows, so we need to handle UTF-16
        char16_t ch = static_cast<char16_t>(*tstr);
        if (ch < 128) {
            result += static_cast<char>(ch);
        } else {
            // Non-ASCII character - indicate with placeholder
            result += "[U+";
            char hex[8];
            snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(ch));
            result += hex;
            result += "]";
        }
        ++tstr;
    }
    return result;
}

// Helper to get string at index from StringListParameter
static std::string getStringAtIndex(StringListParameter* param, int32 index) {
    String128 buffer;
    buffer[0] = 0;  // Initialize to empty
    param->toString(param->toNormalized(static_cast<ParamValue>(index)), buffer);
    return tcharToString(buffer);
}

// ==============================================================================
// TEST: Direct appendString calls (the known working pattern)
// ==============================================================================

TEST_CASE("Direct appendString creates correct strings", "[vst][parameter]") {
    auto* param = new StringListParameter(
        STR16("Test Param"),
        1000,
        nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList
    );

    // This is exactly how the Mode selector does it (and it works)
    param->appendString(STR16("Option A"));
    param->appendString(STR16("Option B"));
    param->appendString(STR16("Option C"));

    SECTION("stepCount is correct") {
        REQUIRE(param->getInfo().stepCount == 2);  // 3 options = stepCount of 2
    }

    SECTION("toString returns correct strings") {
        std::string optA = getStringAtIndex(param, 0);
        std::string optB = getStringAtIndex(param, 1);
        std::string optC = getStringAtIndex(param, 2);

        INFO("Option A: " << optA);
        INFO("Option B: " << optB);
        INFO("Option C: " << optC);

        REQUIRE(optA == "Option A");
        REQUIRE(optB == "Option B");
        REQUIRE(optC == "Option C");
    }

    SECTION("toPlain returns integer indices") {
        REQUIRE(param->toPlain(0.0) == Catch::Approx(0.0));
        REQUIRE(param->toPlain(0.5) == Catch::Approx(1.0));
        REQUIRE(param->toPlain(1.0) == Catch::Approx(2.0));
    }

    delete param;
}

// ==============================================================================
// TEST: Helper function with initializer_list
// ==============================================================================

TEST_CASE("Helper function creates correct strings", "[vst][parameter][helper]") {
    auto* param = Iterum::createDropdownParameter(
        STR16("Test Param"),
        1001,
        {STR16("Option A"), STR16("Option B"), STR16("Option C")}
    );

    SECTION("stepCount is correct") {
        REQUIRE(param->getInfo().stepCount == 2);
    }

    SECTION("toString returns correct strings") {
        std::string optA = getStringAtIndex(param, 0);
        std::string optB = getStringAtIndex(param, 1);
        std::string optC = getStringAtIndex(param, 2);

        INFO("Option A: " << optA);
        INFO("Option B: " << optB);
        INFO("Option C: " << optC);

        // This is where we expect to see the encoding issue
        REQUIRE(optA == "Option A");
        REQUIRE(optB == "Option B");
        REQUIRE(optC == "Option C");
    }

    SECTION("toPlain returns integer indices") {
        REQUIRE(param->toPlain(0.0) == Catch::Approx(0.0));
        REQUIRE(param->toPlain(0.5) == Catch::Approx(1.0));
        REQUIRE(param->toPlain(1.0) == Catch::Approx(2.0));
    }

    delete param;
}

// ==============================================================================
// TEST: Compare direct vs helper approach
// ==============================================================================

TEST_CASE("Direct and helper produce identical results", "[vst][parameter][compare]") {
    // Create using direct method
    auto* directParam = new StringListParameter(
        STR16("Direct Param"),
        1002,
        nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList
    );
    directParam->appendString(STR16("MN3005"));
    directParam->appendString(STR16("MN3007"));
    directParam->appendString(STR16("MN3205"));
    directParam->appendString(STR16("SAD1024"));

    // Create using helper
    auto* helperParam = Iterum::createDropdownParameter(
        STR16("Helper Param"),
        1003,
        {STR16("MN3005"), STR16("MN3007"), STR16("MN3205"), STR16("SAD1024")}
    );

    SECTION("stepCount matches") {
        REQUIRE(directParam->getInfo().stepCount == helperParam->getInfo().stepCount);
    }

    SECTION("all strings match") {
        for (int32 i = 0; i <= directParam->getInfo().stepCount; ++i) {
            std::string directStr = getStringAtIndex(directParam, i);
            std::string helperStr = getStringAtIndex(helperParam, i);

            INFO("Index " << i << ": direct='" << directStr << "' helper='" << helperStr << "'");
            REQUIRE(directStr == helperStr);
        }
    }

    delete directParam;
    delete helperParam;
}

// ==============================================================================
// TEST: String memory and lifetime
// ==============================================================================

TEST_CASE("String literals have correct lifetime in initializer_list", "[vst][parameter][memory]") {
    // Test that string literals passed through initializer_list survive
    auto* param = Iterum::createDropdownParameter(
        STR16("Memory Test"),
        1004,
        {STR16("First"), STR16("Second"), STR16("Third")}
    );

    // Force some other allocations to potentially overwrite dangling pointers
    std::vector<std::string> dummy;
    for (int i = 0; i < 100; ++i) {
        dummy.push_back(std::string(1000, 'X'));
    }

    SECTION("strings are still valid after other allocations") {
        std::string first = getStringAtIndex(param, 0);
        std::string second = getStringAtIndex(param, 1);
        std::string third = getStringAtIndex(param, 2);

        INFO("First: " << first);
        INFO("Second: " << second);
        INFO("Third: " << third);

        REQUIRE(first == "First");
        REQUIRE(second == "Second");
        REQUIRE(third == "Third");
    }

    delete param;
}

// ==============================================================================
// TEST: Raw pointer content inspection
// ==============================================================================

TEST_CASE("Inspect raw TChar content", "[vst][parameter][debug]") {
    // Direct test of what STR16 produces
    const TChar* testStr = STR16("Test");

    SECTION("STR16 produces valid UTF-16") {
        // 'T' = 0x0054, 'e' = 0x0065, 's' = 0x0073, 't' = 0x0074
        REQUIRE(testStr[0] == 0x0054);  // 'T'
        REQUIRE(testStr[1] == 0x0065);  // 'e'
        REQUIRE(testStr[2] == 0x0073);  // 's'
        REQUIRE(testStr[3] == 0x0074);  // 't'
        REQUIRE(testStr[4] == 0x0000);  // null terminator
    }

    SECTION("appendString copies the content correctly") {
        auto* param = new StringListParameter(
            STR16("Debug Param"),
            1005,
            nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList
        );

        param->appendString(STR16("ABC"));

        String128 buffer;
        param->toString(0.0, buffer);  // Index 0

        // Check the buffer content
        REQUIRE(buffer[0] == 0x0041);  // 'A'
        REQUIRE(buffer[1] == 0x0042);  // 'B'
        REQUIRE(buffer[2] == 0x0043);  // 'C'
        REQUIRE(buffer[3] == 0x0000);  // null

        delete param;
    }
}

// ==============================================================================
// TEST: Real-world parameter names from the plugin
// ==============================================================================

TEST_CASE("BBD Era options work correctly", "[vst][parameter][bbd]") {
    auto* param = Iterum::createDropdownParameter(
        STR16("BBD Era"),
        1006,
        {STR16("MN3005"), STR16("MN3007"), STR16("MN3205"), STR16("SAD1024")}
    );

    REQUIRE(param->getInfo().stepCount == 3);

    std::string opt0 = getStringAtIndex(param, 0);
    std::string opt1 = getStringAtIndex(param, 1);
    std::string opt2 = getStringAtIndex(param, 2);
    std::string opt3 = getStringAtIndex(param, 3);

    INFO("BBD Era options: " << opt0 << ", " << opt1 << ", " << opt2 << ", " << opt3);

    REQUIRE(opt0 == "MN3005");
    REQUIRE(opt1 == "MN3007");
    REQUIRE(opt2 == "MN3205");
    REQUIRE(opt3 == "SAD1024");

    delete param;
}

TEST_CASE("Digital Time Mode options work correctly", "[vst][parameter][digital]") {
    auto* param = Iterum::createDropdownParameter(
        STR16("Digital Time Mode"),
        1007,
        {STR16("Free"), STR16("Synced")}
    );

    REQUIRE(param->getInfo().stepCount == 1);

    std::string opt0 = getStringAtIndex(param, 0);
    std::string opt1 = getStringAtIndex(param, 1);

    INFO("Time Mode options: " << opt0 << ", " << opt1);

    REQUIRE(opt0 == "Free");
    REQUIRE(opt1 == "Synced");

    delete param;
}

TEST_CASE("Playback Mode options work correctly", "[vst][parameter][reverse]") {
    auto* param = Iterum::createDropdownParameter(
        STR16("Reverse Playback Mode"),
        1008,
        {STR16("Full Reverse"), STR16("Alternating"), STR16("Random")}
    );

    REQUIRE(param->getInfo().stepCount == 2);

    std::string opt0 = getStringAtIndex(param, 0);
    std::string opt1 = getStringAtIndex(param, 1);
    std::string opt2 = getStringAtIndex(param, 2);

    INFO("Playback Mode options: " << opt0 << ", " << opt1 << ", " << opt2);

    REQUIRE(opt0 == "Full Reverse");
    REQUIRE(opt1 == "Alternating");
    REQUIRE(opt2 == "Random");

    delete param;
}
