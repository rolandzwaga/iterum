// ==============================================================================
// Granular Delay Tempo Sync UI Tests (spec 038)
// ==============================================================================
// Tests for VST3 parameter registration and UI behavior for tempo sync feature.
//
// Constitution Principle XII: Tests MUST be written before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"
#include "controller/parameter_helpers.h"
#include "plugin_ids.h"

#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Iterum;

// Helper to convert TChar string to std::string for comparison
static std::string tcharToString(const TChar* tstr) {
    if (!tstr) return "";

    std::string result;
    while (*tstr) {
        char16_t ch = static_cast<char16_t>(*tstr);
        if (ch < 128) {
            result += static_cast<char>(ch);
        } else {
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
    buffer[0] = 0;
    param->toString(param->toNormalized(static_cast<ParamValue>(index)), buffer);
    return tcharToString(buffer);
}

// ==============================================================================
// Parameter ID Tests (T049, T050)
// ==============================================================================

TEST_CASE("Granular tempo sync parameter IDs are defined", "[vst][granular][tempo-sync]") {
    SECTION("T049: kGranularTimeModeId is 113") {
        REQUIRE(kGranularTimeModeId == 113);
    }

    SECTION("T050: kGranularNoteValueId is 114") {
        REQUIRE(kGranularNoteValueId == 114);
    }
}

// ==============================================================================
// TimeMode Dropdown Tests (T051, T054)
// ==============================================================================

TEST_CASE("Granular TimeMode dropdown has correct options", "[vst][granular][tempo-sync]") {
    auto* param = createDropdownParameter(
        STR16("Time Mode"), kGranularTimeModeId,
        {STR16("Free"), STR16("Synced")}
    );

    SECTION("T051: TimeMode has 2 options") {
        // stepCount = numOptions - 1
        REQUIRE(param->getInfo().stepCount == 1);
    }

    SECTION("TimeMode options are 'Free' and 'Synced'") {
        REQUIRE(getStringAtIndex(param, 0) == "Free");
        REQUIRE(getStringAtIndex(param, 1) == "Synced");
    }

    SECTION("T054: TimeMode default is index 0 (Free)") {
        // Default normalized value should be 0.0
        REQUIRE(param->getNormalized() == Catch::Approx(0.0));
    }

    delete param;
}

// ==============================================================================
// NoteValue Dropdown Tests (T052, T053)
// ==============================================================================

TEST_CASE("Granular NoteValue dropdown has correct options", "[vst][granular][tempo-sync]") {
    auto* param = createDropdownParameterWithDefault(
        STR16("Note Value"), kGranularNoteValueId,
        4,  // default: 1/8 note (index 4)
        {STR16("1/32"), STR16("1/16T"), STR16("1/16"), STR16("1/8T"), STR16("1/8"),
         STR16("1/4T"), STR16("1/4"), STR16("1/2T"), STR16("1/2"), STR16("1/1")}
    );

    SECTION("T052: NoteValue has 10 options") {
        // stepCount = numOptions - 1 = 9
        REQUIRE(param->getInfo().stepCount == 9);
    }

    SECTION("NoteValue options are correct") {
        REQUIRE(getStringAtIndex(param, 0) == "1/32");
        REQUIRE(getStringAtIndex(param, 1) == "1/16T");
        REQUIRE(getStringAtIndex(param, 2) == "1/16");
        REQUIRE(getStringAtIndex(param, 3) == "1/8T");
        REQUIRE(getStringAtIndex(param, 4) == "1/8");
        REQUIRE(getStringAtIndex(param, 5) == "1/4T");
        REQUIRE(getStringAtIndex(param, 6) == "1/4");
        REQUIRE(getStringAtIndex(param, 7) == "1/2T");
        REQUIRE(getStringAtIndex(param, 8) == "1/2");
        REQUIRE(getStringAtIndex(param, 9) == "1/1");
    }

    SECTION("T053: NoteValue default is index 4 (1/8 note)") {
        // Default index 4 out of 10 options = normalized 4/9 ≈ 0.444
        double expectedNormalized = 4.0 / 9.0;
        REQUIRE(param->getNormalized() == Catch::Approx(expectedNormalized).margin(0.001));
    }

    delete param;
}

// ==============================================================================
// Parameter Flags Tests
// ==============================================================================

TEST_CASE("Granular tempo sync parameters have correct flags", "[vst][granular][tempo-sync]") {
    auto* timeModeParam = createDropdownParameter(
        STR16("Time Mode"), kGranularTimeModeId,
        {STR16("Free"), STR16("Synced")}
    );

    auto* noteValueParam = createDropdownParameterWithDefault(
        STR16("Note Value"), kGranularNoteValueId,
        4,
        {STR16("1/32"), STR16("1/16T"), STR16("1/16"), STR16("1/8T"), STR16("1/8"),
         STR16("1/4T"), STR16("1/4"), STR16("1/2T"), STR16("1/2"), STR16("1/1")}
    );

    SECTION("TimeMode is automatable") {
        REQUIRE((timeModeParam->getInfo().flags & ParameterInfo::kCanAutomate) != 0);
    }

    SECTION("TimeMode is a list parameter") {
        REQUIRE((timeModeParam->getInfo().flags & ParameterInfo::kIsList) != 0);
    }

    SECTION("NoteValue is automatable") {
        REQUIRE((noteValueParam->getInfo().flags & ParameterInfo::kCanAutomate) != 0);
    }

    SECTION("NoteValue is a list parameter") {
        REQUIRE((noteValueParam->getInfo().flags & ParameterInfo::kIsList) != 0);
    }

    delete timeModeParam;
    delete noteValueParam;
}

// ==============================================================================
// toPlain Tests (verifies StringListParameter behavior)
// ==============================================================================

TEST_CASE("Granular tempo sync parameters toPlain returns integer indices", "[vst][granular][tempo-sync]") {
    auto* noteValueParam = createDropdownParameterWithDefault(
        STR16("Note Value"), kGranularNoteValueId,
        4,
        {STR16("1/32"), STR16("1/16T"), STR16("1/16"), STR16("1/8T"), STR16("1/8"),
         STR16("1/4T"), STR16("1/4"), STR16("1/2T"), STR16("1/2"), STR16("1/1")}
    );

    SECTION("toPlain returns correct integer indices") {
        // StringListParameter::toPlain should convert normalized values to indices
        for (int i = 0; i <= 9; ++i) {
            double normalized = noteValueParam->toNormalized(static_cast<ParamValue>(i));
            int32 plain = static_cast<int32>(noteValueParam->toPlain(normalized));
            REQUIRE(plain == i);
        }
    }

    delete noteValueParam;
}
