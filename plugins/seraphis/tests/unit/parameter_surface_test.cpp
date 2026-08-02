// ==============================================================================
// Seraphis - Parameter surface tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.3)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-001  the registered surface is complete - 91 IDs, exactly C-6's set,
//           each with the stepCount its Type column demands, plus the
//           getParamStringByValue section that covers FR-061
//   SC-014  the eight Phase 8 IDs are frozen field-for-field
//   SC-015  every registered ID has a control tag in editor.uidesc, and no
//           orphan tag exists
//   SC-022  every registered default matches the C-6 table
//
// All four are PURE TABLE TESTS: no render, no engine. Nothing here prepares
// the 771 968 B engine, so the whole TU stays cheap.
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt.
//
// ------------------------------------------------------------------------------
// WRITTEN BEFORE THE IMPLEMENTATION (tasks.md T009). On arrival all four cases
// FAIL: the controller registers 8 parameters, not 91, and editor.uidesc carries
// 8 control tags, not 91. The six parameter packs this file includes are created
// by T010-T015, so the TU does not COMPILE until GROUP 10 lands - that is the
// intended shape of a test-first task whose subject is the pack API itself
// (SC-022 must call handle<Section>ParamChange, and SC-001's third formatting
// assertion must call all six format<Section>Param functions directly).
//
// HOW SC-022 READS "THE STORED PLAIN VALUE", AND WHY IT IS NOT A FIELD ACCESS.
// Neither spec.md nor plan.md names a single atomic FIELD of the six new packs;
// what they DO fix normatively is (a) the six-function contract every pack
// implements (plan §2.3) and (b) the exact write order of each pack's state
// block (plan §5.1 / spec C-8). This file therefore observes the stored plain
// values through each pack's own save<Section>Params() - the one documented,
// order-normative read-back - instead of inventing seventy field names that the
// packs would then have to be reverse-engineered to match. A pack that stores
// the right value but writes its block in the wrong order fails here too, which
// is a feature: SC-010 owns the byte total, this case owns the values.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"

#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/systems/spectral_morph_engine.h>  // kMinTravelRate (ID 404)

#include <uidesc_reachability.h>  // tests/test_helpers/uidesc_reachability.h

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using Catch::Approx;

namespace {

// ==============================================================================
// Spec C-6, transcribed. THIS TABLE IS THE CRITERION.
// ==============================================================================

/// C-6's *Type* column. `R` = plain Vst::Parameter, `L` = StringListParameter,
/// `T` = stepped toggle (stepCount == 1).
enum class Kind { R, L, T };

struct SurfaceRow {
    Vst::ParamID id;
    Kind kind;
    /// Entry count for `L` rows; 0 otherwise. stepCount == entries - 1.
    int entries;
};

/// All 91 rows of spec C-6, in band order. 73 R + 12 L + 6 T
/// (plugin_ids.h:197-239 carries the same grouping as prose).
constexpr SurfaceRow kSurface[] = {
    // --- Global (0-99) -------------------------------------------------------
    {Seraphis::kMasterGainId, Kind::R, 0},
    {Seraphis::kPolyphonyId, Kind::L, 16},
    {Seraphis::kSoftLimitId, Kind::T, 0},
    {Seraphis::kSeedId, Kind::L, 16},

    // --- Macros (100-199) ----------------------------------------------------
    {Seraphis::kMacroDreamId, Kind::R, 0},
    {Seraphis::kMacroBloomId, Kind::R, 0},
    {Seraphis::kMacroDissolveId, Kind::R, 0},
    {Seraphis::kMacroGravityId, Kind::R, 0},
    {Seraphis::kMacroEntropyId, Kind::R, 0},

    // --- Harmonic Cloud (200-399) -------------------------------------------
    {Seraphis::kCloudRichnessId, Kind::R, 0},
    {Seraphis::kCloudInharmonicityId, Kind::R, 0},
    {Seraphis::kCloudTiltId, Kind::R, 0},
    {Seraphis::kCloudMutationId, Kind::R, 0},
    {Seraphis::kCloudGravityId, Kind::R, 0},
    {Seraphis::kCloudDriftDepthId, Kind::R, 0},
    {Seraphis::kCloudDriftSmoothnessId, Kind::R, 0},
    {Seraphis::kCloudStereoSpreadId, Kind::R, 0},
    {Seraphis::kCloudAttackId, Kind::R, 0},
    {Seraphis::kCloudDecayId, Kind::R, 0},
    {Seraphis::kCloudEnvOffsetSpreadId, Kind::R, 0},

    // --- Spectral Morph / Entropy (400-599) ---------------------------------
    {Seraphis::kMorphEntropyId, Kind::R, 0},
    {Seraphis::kMorphBloomId, Kind::R, 0},
    {Seraphis::kMorphPositionId, Kind::R, 0},
    {Seraphis::kMorphTravelModeId, Kind::L, 2},
    {Seraphis::kMorphTravelRateId, Kind::R, 0},
    {Seraphis::kMorphSyncId, Kind::T, 0},
    {Seraphis::kMorphSyncNoteId, Kind::L, 8},
    {Seraphis::kMorphWaypointIntervalId, Kind::R, 0},
    {Seraphis::kMorphStateCountId, Kind::L, 3},
    {Seraphis::kMorphState0Id, Kind::L, 5},
    {Seraphis::kMorphState1Id, Kind::L, 5},
    {Seraphis::kMorphState2Id, Kind::L, 5},
    {Seraphis::kMorphState3Id, Kind::L, 5},

    // --- Life Modulators (600-699) + Voice Envelope (700-799) ---------------
    {Seraphis::kLifeSpatialDepthId, Kind::R, 0},
    {Seraphis::kLifeSpatialRateId, Kind::R, 0},
    {Seraphis::kLifeSpatialCouplingId, Kind::R, 0},
    {Seraphis::kLifeSpatialGrowthId, Kind::R, 0},
    {Seraphis::kLifeVoiceWidthId, Kind::R, 0},
    {Seraphis::kEnvModeId, Kind::L, 2},
    {Seraphis::kEnvGrowthDurationId, Kind::R, 0},
    {Seraphis::kEnvStage0MsId, Kind::R, 0},
    {Seraphis::kEnvStage1MsId, Kind::R, 0},
    {Seraphis::kEnvReleaseMsId, Kind::R, 0},

    // --- Continuous Body (800-999) ------------------------------------------
    {Seraphis::kBodyMaterialId, Kind::L, 5},
    {Seraphis::kBodyResonanceId, Kind::R, 0},
    {Seraphis::kBodyDampingId, Kind::R, 0},
    {Seraphis::kBodyKeyTrackingId, Kind::R, 0},
    {Seraphis::kBodyDriveId, Kind::R, 0},
    {Seraphis::kBodyMixId, Kind::R, 0},
    {Seraphis::kBodyCloudMixId, Kind::R, 0},
    {Seraphis::kBodyCloudDecayId, Kind::R, 0},
    {Seraphis::kBodyCloudSizeId, Kind::R, 0},
    {Seraphis::kBodyCloudDampingId, Kind::R, 0},
    {Seraphis::kBodyWidthId, Kind::R, 0},
    {Seraphis::kBodyInputAgcId, Kind::T, 0},
    {Seraphis::kBodyResonatorBypassId, Kind::T, 0},

    // --- Granular Atmosphere (1000-1199) ------------------------------------
    {Seraphis::kAtmosLevelId, Kind::R, 0},
    {Seraphis::kAtmosBlurId, Kind::R, 0},
    {Seraphis::kAtmosDensityId, Kind::R, 0},
    {Seraphis::kAtmosGrainSecondsId, Kind::R, 0},
    {Seraphis::kAtmosDriftDepthId, Kind::R, 0},
    {Seraphis::kAtmosPanSpreadId, Kind::R, 0},
    {Seraphis::kAtmosDecorrelationId, Kind::R, 0},
    {Seraphis::kAtmosFreezeMixId, Kind::R, 0},
    {Seraphis::kAtmosFreezeId, Kind::T, 0},
    {Seraphis::kAtmosDriftSmoothnessId, Kind::R, 0},
    {Seraphis::kAtmosDriftRangeId, Kind::R, 0},
    {Seraphis::kAtmosJitterId, Kind::R, 0},
    {Seraphis::kAtmosPositionId, Kind::R, 0},
    {Seraphis::kAtmosPositionSpreadId, Kind::R, 0},
    {Seraphis::kAtmosPitchId, Kind::R, 0},
    {Seraphis::kAtmosPitchSpreadId, Kind::R, 0},
    {Seraphis::kAtmosGrainEnvelopeId, Kind::L, 6},

    // --- Aether Space (1200-1399) -------------------------------------------
    {Seraphis::kAetherMixId, Kind::R, 0},
    {Seraphis::kAetherSizeId, Kind::R, 0},
    {Seraphis::kAetherDensityId, Kind::R, 0},
    {Seraphis::kAetherDecayId, Kind::R, 0},
    {Seraphis::kAetherFreezeId, Kind::T, 0},
    {Seraphis::kAetherDimensionalityId, Kind::R, 0},
    {Seraphis::kAetherDampingId, Kind::R, 0},
    {Seraphis::kAetherPreDelayId, Kind::R, 0},
    {Seraphis::kAetherModDepthId, Kind::R, 0},
    {Seraphis::kAetherModSmoothnessId, Kind::R, 0},
    {Seraphis::kAetherShimmerOctaveId, Kind::R, 0},
    {Seraphis::kAetherShimmerFifthId, Kind::R, 0},
    {Seraphis::kAetherBloomSendId, Kind::R, 0},
    {Seraphis::kAetherBloomDecayId, Kind::R, 0},
    {Seraphis::kAetherSpectralDiffusionId, Kind::R, 0},
    {Seraphis::kAetherSizeBreathDepthId, Kind::R, 0},
    {Seraphis::kAetherTideDepthId, Kind::R, 0},
    {Seraphis::kAetherWidthId, Kind::R, 0},
};

constexpr std::size_t kSurfaceRowCount = sizeof(kSurface) / sizeof(kSurface[0]);

static_assert(kSurfaceRowCount == 91,
              "SC-001: spec C-6 is a 91-row table (8 shipped + 83 new)");

[[nodiscard]] int expectedStepCount(const SurfaceRow& row) {
    switch (row.kind) {
        case Kind::R: return 0;
        case Kind::T: return 1;
        case Kind::L: return row.entries - 1;
    }
    return -1;
}

// ==============================================================================
// The dropdown label tables (FR-015 / FR-061)
// ==============================================================================
// ELEVEN rows, not ten. plan §7.3 and §2.1(e) enumerate ten `L` IDs
// (3, 403, 406, 408-412, 800, 1016) and omit 700 - the same slip that wrote
// "nine" over a list of ten, since 409-412 is FOUR IDs sharing ONE label table.
// plugin_ids.h:228-231 is the record and rules the other way: C-6 types
// kEnvModeId as `L` (Standard / Growth) and plan §2.2 gives it its own
// kEnvelopeModeLabels table, so 700 IS a dropdown and is label-checked here.
//
// kPolyphonyId (1) is a twelfth `L`, but its sixteen labels are registered
// INLINE (global_params.h:145-149) rather than from a dropdown_mappings.h table
// - they are ordinals, not a mapping, and nothing formats them - so there is no
// single table to compare against. It still participates in the "no formatter
// claims a dropdown" assertion below.
struct DropdownTable {
    Vst::ParamID id;
    const Vst::TChar* const* labels;
    int count;
};

const DropdownTable kDropdownTables[] = {
    {Seraphis::kSeedId, Seraphis::kSeedLabels.data(),
     static_cast<int>(Seraphis::kSeedLabels.size())},
    {Seraphis::kMorphTravelModeId, Seraphis::kTravelModeLabels.data(),
     static_cast<int>(Seraphis::kTravelModeLabels.size())},
    {Seraphis::kMorphSyncNoteId, Seraphis::kSyncNoteLabels.data(),
     static_cast<int>(Seraphis::kSyncNoteLabels.size())},
    {Seraphis::kMorphStateCountId, Seraphis::kStateCountLabels.data(),
     static_cast<int>(Seraphis::kStateCountLabels.size())},
    {Seraphis::kMorphState0Id, Seraphis::kSpectralStateLabels.data(),
     static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {Seraphis::kMorphState1Id, Seraphis::kSpectralStateLabels.data(),
     static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {Seraphis::kMorphState2Id, Seraphis::kSpectralStateLabels.data(),
     static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {Seraphis::kMorphState3Id, Seraphis::kSpectralStateLabels.data(),
     static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {Seraphis::kEnvModeId, Seraphis::kEnvelopeModeLabels.data(),
     static_cast<int>(Seraphis::kEnvelopeModeLabels.size())},
    {Seraphis::kBodyMaterialId, Seraphis::kBodyMaterialLabels.data(),
     static_cast<int>(Seraphis::kBodyMaterialLabels.size())},
    {Seraphis::kAtmosGrainEnvelopeId, Seraphis::kGrainEnvelopeLabels.data(),
     static_cast<int>(Seraphis::kGrainEnvelopeLabels.size())},
};

constexpr std::size_t kDropdownTableCount =
    sizeof(kDropdownTables) / sizeof(kDropdownTables[0]);

/// All TWELVE registered `L` IDs - the eleven label-checked above plus
/// kPolyphonyId, whose labels are inline. FR-061: no format<Section>Param may
/// claim any of them.
constexpr Vst::ParamID kAllDropdownIds[] = {
    Seraphis::kPolyphonyId,      Seraphis::kSeedId,
    Seraphis::kMorphTravelModeId, Seraphis::kMorphSyncNoteId,
    Seraphis::kMorphStateCountId, Seraphis::kMorphState0Id,
    Seraphis::kMorphState1Id,     Seraphis::kMorphState2Id,
    Seraphis::kMorphState3Id,     Seraphis::kEnvModeId,
    Seraphis::kBodyMaterialId,    Seraphis::kAtmosGrainEnvelopeId,
};

constexpr std::size_t kAllDropdownIdCount =
    sizeof(kAllDropdownIds) / sizeof(kAllDropdownIds[0]);

// ==============================================================================
// Small string helpers (UTF-16 comparison, no allocation games)
// ==============================================================================

[[nodiscard]] bool tcharEquals(const Vst::TChar* a, const Vst::TChar* b) {
    while (*a != 0 && *b != 0) {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

[[nodiscard]] bool tcharEqualsAscii(const Vst::TChar* s, const char* ascii) {
    while (*s != 0 && *ascii != 0) {
        if (*s != static_cast<Vst::TChar>(*ascii)) {
            return false;
        }
        ++s;
        ++ascii;
    }
    return (*s == 0) && (*ascii == 0);
}

/// For INFO() only - never for an assertion.
[[nodiscard]] std::string describe(const Vst::TChar* s) {
    char8 buffer[256] = {};
    UString(const_cast<Vst::TChar*>(s), 128).toAscii(buffer, 256);
    return std::string(buffer);
}

// ==============================================================================
// Controller helpers
// ==============================================================================

[[nodiscard]] bool infoForId(Vst::EditController& controller, Vst::ParamID id,
                             Vst::ParameterInfo& out) {
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        if (controller.getParameterInfo(i, info) != kResultOk) {
            continue;
        }
        if (info.id == id) {
            out = info;
            return true;
        }
    }
    return false;
}

[[nodiscard]] Vst::ParamValue defaultNormalizedFor(Vst::EditController& controller,
                                                   Vst::ParamID id) {
    Vst::ParameterInfo info{};
    INFO("parameter ID " << id << " must be registered before its default can be read");
    REQUIRE(infoForId(controller, id, info));
    return info.defaultNormalizedValue;
}

// ==============================================================================
// State-stream plumbing for SC-022
// ==============================================================================

struct StreamReleaser {
    void operator()(MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<MemoryStream, StreamReleaser>;

struct FloatRow {
    Vst::ParamID id;
    float expected;
};

struct IntRow {
    Vst::ParamID id;
    int32 expected;
};

/// SC-022 for one pack: push every registered default through the pack's own
/// handler, then read the stored plain values back out of its state block in
/// plan §5.1's order and compare with EXACT float `==`, no tolerance.
template <typename Params, typename HandleFn, typename SaveFn>
void checkPackDefaults(const char* packName,
                       Vst::EditController& controller,
                       const std::vector<Vst::ParamID>& ids,
                       Params& params,
                       HandleFn handle,
                       SaveFn save,
                       const std::vector<FloatRow>& floatRows,
                       const std::vector<IntRow>& intRows) {
    for (const Vst::ParamID id : ids) {
        handle(params, id, defaultNormalizedFor(controller, id));
    }

    StreamPtr stream(new MemoryStream());
    {
        IBStreamer writer(stream.get(), kLittleEndian);
        save(params, writer);
    }
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(stream.get(), kLittleEndian);

    for (const FloatRow& row : floatRows) {
        float value = 0.0f;
        INFO(packName << ": plain default of ID " << row.id);
        REQUIRE(reader.readFloat(value));
        CHECK(value == row.expected);
    }
    for (const IntRow& row : intRows) {
        int32 value = 0;
        INFO(packName << ": plain default of ID " << row.id);
        REQUIRE(reader.readInt32(value));
        CHECK(value == row.expected);
    }
}

// ==============================================================================
// editor.uidesc parsing (SC-015)
// ==============================================================================

[[nodiscard]] std::string readUidesc() {
    const std::string path = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";
    std::ifstream file(path);
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

struct BoundView {
    std::string viewClass;
    std::string tagName;
};

/// Every `<view>` element that binds a control-tag, with the class it is drawn
/// as. Text-level, deliberately: no VSTGUI frame is instantiated in this TU.
[[nodiscard]] std::vector<BoundView> extractBoundViews(const std::string& xml) {
    std::vector<BoundView> views;
    const std::string marker = "<view";
    std::size_t pos = 0;
    while ((pos = xml.find(marker, pos)) != std::string::npos) {
        const std::size_t end = xml.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        const std::string element = xml.substr(pos, end - pos);
        pos = end + 1;

        const auto tagPos = element.find("control-tag=\"");
        const auto classPos = element.find("class=\"");
        if (tagPos == std::string::npos || classPos == std::string::npos) {
            continue;  // a decorative view (CTextLabel) binds nothing
        }
        const auto tagStart = tagPos + std::string("control-tag=\"").size();
        const auto tagEnd = element.find('"', tagStart);
        const auto classStart = classPos + std::string("class=\"").size();
        const auto classEnd = element.find('"', classStart);
        if (tagEnd == std::string::npos || classEnd == std::string::npos) {
            continue;
        }
        views.push_back(BoundView{element.substr(classStart, classEnd - classStart),
                                  element.substr(tagStart, tagEnd - tagStart)});
    }
    return views;
}

/// C-6's *Type* column, expressed as the VSTGUI control class the placeholder
/// template must keep using (FR-048 freezes the registered types, and a
/// mismatched control class binds with NO error path).
[[nodiscard]] const char* expectedViewClass(Kind kind) {
    switch (kind) {
        case Kind::R: return "CSlider";
        case Kind::L: return "COptionMenu";
        case Kind::T: return "CCheckBox";
    }
    return "";
}

}  // namespace

// ==============================================================================
// SC-001 - the registered surface is exactly spec C-6
// ==============================================================================

TEST_CASE("Seraphis_ParameterSurface_IsComplete", "[seraphis][controller][params]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // --- the count -----------------------------------------------------------
    CHECK(controller.getParameterCount() == 91);

    // --- the ID set, in both directions, with no duplicate -------------------
    std::set<Vst::ParamID> registered;
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
        INFO("duplicate registration of parameter ID " << info.id);
        CHECK(registered.insert(info.id).second);
        // spec C-5: nothing may sit outside the eight reserved bands, which run
        // contiguously from 0 to kAetherParamRangeEnd - 1. 1400+ is Phase 10.
        INFO("parameter ID " << info.id << " lies outside the reserved bands");
        CHECK(info.id < Seraphis::kAetherParamRangeEnd);
    }
    CHECK(registered.size() == kSurfaceRowCount);

    std::set<Vst::ParamID> expected;
    for (const SurfaceRow& row : kSurface) {
        expected.insert(row.id);
    }
    CHECK(expected.size() == kSurfaceRowCount);  // C-6 itself carries no duplicate
    CHECK(registered == expected);

    // --- the Type column, read back as stepCount -----------------------------
    for (const SurfaceRow& row : kSurface) {
        Vst::ParameterInfo info{};
        INFO("parameter ID " << row.id << " is missing from the registered surface");
        REQUIRE(infoForId(controller, row.id, info));
        INFO("parameter ID " << row.id << ": stepCount must match spec C-6's Type column");
        CHECK(info.stepCount == expectedStepCount(row));
    }

    // ==========================================================================
    // FR-061 - display formatting. No other planned case exercises it.
    // ==========================================================================
    SECTION("Seraphis_ParamStringByValue_UsesTheSingleLabelTable") {
        // (1) Every dropdown whose labels live in dropdown_mappings.h, at EVERY
        //     index. A label list that existed in two places fails here.
        for (std::size_t t = 0; t < kDropdownTableCount; ++t) {
            const DropdownTable& table = kDropdownTables[t];
            REQUIRE(table.count > 1);
            for (int i = 0; i < table.count; ++i) {
                const Vst::ParamValue normalized =
                    static_cast<Vst::ParamValue>(i) /
                    static_cast<Vst::ParamValue>(table.count - 1);
                Vst::String128 text{};
                INFO("ID " << table.id << " index " << i);
                REQUIRE(controller.getParamStringByValue(table.id, normalized, text) ==
                        kResultOk);
                INFO("ID " << table.id << " index " << i << ": got \"" << describe(text)
                           << "\", want \"" << describe(table.labels[i]) << "\"");
                CHECK(tcharEquals(text, table.labels[i]));
            }
        }

        // (2) One `R` ID per new pack, at 0.0 / 0.5 / 1.0: never an empty string.
        const Vst::ParamID kSampledRangeIds[] = {
            Seraphis::kCloudRichnessId,      // cloud
            Seraphis::kMorphEntropyId,       // morph
            Seraphis::kLifeSpatialRateId,    // life
            Seraphis::kBodyDriveId,          // body
            Seraphis::kAtmosGrainSecondsId,  // atmosphere
            Seraphis::kAetherPreDelayId,     // aether
        };
        const Vst::ParamValue kSamplePoints[] = {0.0, 0.5, 1.0};
        for (const Vst::ParamID id : kSampledRangeIds) {
            for (const Vst::ParamValue point : kSamplePoints) {
                Vst::String128 text{};
                INFO("ID " << id << " at normalized " << point);
                REQUIRE(controller.getParamStringByValue(id, point, text) == kResultOk);
                CHECK(text[0] != 0);
            }
        }

        // (3) FR-061's prohibition, asserted the only way it is visible: call all
        //     six pack formatters DIRECTLY on every dropdown ID. A formatter that
        //     claimed one would render "0.400" instead of "Metal Plate" in every
        //     host and pass assertion (1) only by accident of ordering.
        for (std::size_t d = 0; d < kAllDropdownIdCount; ++d) {
            const Vst::ParamID id = kAllDropdownIds[d];
            const Vst::ParamValue probe = 0.5;
            Vst::String128 text{};
            INFO("dropdown ID " << id << " must not be claimed by any format<Section>Param");
            CHECK(Seraphis::formatCloudParam(id, probe, text) != kResultOk);
            CHECK(Seraphis::formatMorphParam(id, probe, text) != kResultOk);
            CHECK(Seraphis::formatLifeModParam(id, probe, text) != kResultOk);
            CHECK(Seraphis::formatBodyParam(id, probe, text) != kResultOk);
            CHECK(Seraphis::formatAtmosphereParam(id, probe, text) != kResultOk);
            CHECK(Seraphis::formatAetherParam(id, probe, text) != kResultOk);
        }
    }

    controller.terminate();
}

// ==============================================================================
// SC-014 - Phase 8's eight IDs are frozen (FR-063)
// ==============================================================================

TEST_CASE("Seraphis_Phase8Parameters_AreFrozen", "[seraphis][controller][params]") {
    struct FrozenInfo {
        Vst::ParamID id;
        int32 stepCount;
        double defaultNormalized;
        const char* units;
        int32 flags;
    };

    // The checked-in table. Values are Phase 8's registration
    // (global_params.h:124-162, macro_params.h:67-85).
    //
    // ID 1's default is 7/15 (Polyphony 8), NOT 0.0, and that row is the one
    // this table deliberately states against the SDK's behaviour rather than
    // against it. Verified this session:
    // Krate::Plugins::createDropdownParameterWithDefault only calls
    // Parameter::setNormalized (parameter_helpers.h:126-128), which writes
    // valueNormalized and NEVER info.defaultNormalizedValue, while
    // StringListParameter's constructor leaves info.defaultNormalizedValue at 0
    // - so on the shared helper alone a host "reset to default" on Polyphony
    // yields 1 voice, not the 8 that C-6, GlobalParams::polyphony
    // (global_params.h:44) and SeraphisEngineConfig all agree on.
    //
    // THE FIX BELONGS IN REGISTRATION, NOT IN THIS TABLE, and that is where it
    // is: Seraphis::addDropdownParam (dropdown_mappings.h:263-278) pins
    // getInfo().defaultNormalizedValue on every Seraphis StringListParameter,
    // including the other two non-zero indices C-6 carries (406 "1 Bar", 410
    // "Glass"). It changes no ID, type, unit or INTENDED default, so FR-063 is
    // untouched. Editing 7.0/15.0 down to 0.0 here would freeze the defect and
    // make SC-022's row for ID 1 unsatisfiable.
    constexpr FrozenInfo kPhase8[] = {
        {Seraphis::kMasterGainId, 0, 0.5, "dB", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kPolyphonyId, 15, 7.0 / 15.0, "",
         Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList},
        {Seraphis::kSoftLimitId, 1, 1.0, "", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kMacroDreamId, 0, 0.0, "%", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kMacroBloomId, 0, 0.0, "%", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kMacroDissolveId, 0, 0.0, "%", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kMacroGravityId, 0, 0.5, "%", Vst::ParameterInfo::kCanAutomate},
        {Seraphis::kMacroEntropyId, 0, 0.0, "%", Vst::ParameterInfo::kCanAutomate},
    };

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    for (const FrozenInfo& frozen : kPhase8) {
        Vst::ParameterInfo info{};
        INFO("Phase 8 parameter ID " << frozen.id << " is no longer registered");
        REQUIRE(infoForId(controller, frozen.id, info));

        INFO("Phase 8 parameter ID " << frozen.id);
        CHECK(info.id == frozen.id);
        CHECK(info.stepCount == frozen.stepCount);
        CHECK(info.defaultNormalizedValue == Approx(frozen.defaultNormalized).margin(1e-12));
        CHECK(tcharEqualsAscii(info.units, frozen.units));
        CHECK(info.flags == frozen.flags);
    }

    controller.terminate();
}

// ==============================================================================
// SC-015 - editor.uidesc control tags match the registered surface
// ==============================================================================

TEST_CASE("Seraphis_UidescControlTags_MatchRegisteredIds", "[seraphis][controller][ui]") {
    // NOT Krate::Test::unreachableParams: Phase 9 adds 83 tags with NO view on
    // purpose (layout is Phase 11's), and that helper would report all 83 as
    // unreachable. Only the tag MAP is consumed here.
    const std::string xml = readUidesc();
    const std::map<std::string, int> tagMap = Krate::Test::extractControlTagMap(xml);

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    std::set<int> registered;
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
        registered.insert(static_cast<int>(info.id));
    }

    std::set<int> tagged;
    for (const auto& entry : tagMap) {
        INFO("duplicate <control-tag> value " << entry.second << " (name \"" << entry.first
                                              << "\")");
        CHECK(tagged.insert(entry.second).second);
    }

    // Direction 1: no registered ID lacks a tag.
    for (const int id : registered) {
        INFO("registered parameter ID " << id << " has no <control-tag> in editor.uidesc");
        CHECK(tagged.count(id) == 1u);
    }
    // Direction 2: no tag names an ID that is not registered.
    for (const int tag : tagged) {
        INFO("<control-tag> " << tag << " names no registered parameter");
        CHECK(registered.count(tag) == 1u);
    }
    CHECK(tagged == registered);

    // The eight placeholder views still bind, and still bind as the control class
    // spec C-6's Type column demands (FR-048: no new <view> is added by Phase 9).
    const std::vector<BoundView> bound = extractBoundViews(xml);
    CHECK(bound.size() == 8u);

    for (const BoundView& view : bound) {
        INFO("<view class=\"" << view.viewClass << "\" control-tag=\"" << view.tagName
                              << "\">");
        const auto it = tagMap.find(view.tagName);
        REQUIRE(it != tagMap.end());

        const Vst::ParamID id = static_cast<Vst::ParamID>(it->second);
        const SurfaceRow* row = nullptr;
        for (const SurfaceRow& candidate : kSurface) {
            if (candidate.id == id) {
                row = &candidate;
                break;
            }
        }
        REQUIRE(row != nullptr);
        CHECK(view.viewClass == std::string(expectedViewClass(row->kind)));
    }

    controller.terminate();
}

// ==============================================================================
// SC-022 - every registered default denormalizes to spec C-6's Default column
// ==============================================================================
// EXACT float `==`, no tolerance. plan §2.3.1 shows why that is achievable: the
// mappings compute in double (round-trip error ~1e-15 relative) and the store
// casts to float (half-ULP ~6e-8 relative), so every C-6 default lands on the
// same float as its literal. A tolerance here would hide the one failure mode
// the criterion exists for - a re-typed bound or a hand-typed
// defaultNormalizedValue that drifts by a ULP.

TEST_CASE("Seraphis_RegisteredDefaults_AreExact", "[seraphis][controller][params]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // --- global (0-3) --------------------------------------------------------
    // saveGlobalParams writes float | int32 | int32; the seed is a SEPARATE,
    // explicitly-positioned int32 (global_params.h:260-276, plan §5.2). Both are
    // written back-to-back here because this case reads one pack at a time - the
    // stream-wide ordering is SC-010's subject, not this one's.
    {
        Seraphis::GlobalParams params;
        checkPackDefaults(
            "global", controller,
            {Seraphis::kMasterGainId, Seraphis::kPolyphonyId, Seraphis::kSoftLimitId,
             Seraphis::kSeedId},
            params,
            [](Seraphis::GlobalParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleGlobalParamChange(p, id, v);
            },
            [](const Seraphis::GlobalParams& p, IBStreamer& s) {
                Seraphis::saveGlobalParams(p, s);
                Seraphis::saveGlobalSeed(p, s);
            },
            {{Seraphis::kMasterGainId, 1.0f}},
            {{Seraphis::kPolyphonyId, 8},
             {Seraphis::kSoftLimitId, 1},
             {Seraphis::kSeedId, 0}});  // index 0 == seed 1u (C-10)
    }

    // --- macros (100-104) ----------------------------------------------------
    {
        Seraphis::MacroParams params;
        checkPackDefaults(
            "macro", controller,
            {Seraphis::kMacroDreamId, Seraphis::kMacroBloomId, Seraphis::kMacroDissolveId,
             Seraphis::kMacroGravityId, Seraphis::kMacroEntropyId},
            params,
            [](Seraphis::MacroParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleMacroParamChange(p, id, v);
            },
            [](const Seraphis::MacroParams& p, IBStreamer& s) {
                Seraphis::saveMacroParams(p, s);
            },
            {{Seraphis::kMacroDreamId, 0.0f},
             {Seraphis::kMacroBloomId, 0.0f},
             {Seraphis::kMacroDissolveId, 0.0f},
             {Seraphis::kMacroGravityId, 0.5f},  // bipolar around 0.5
             {Seraphis::kMacroEntropyId, 0.0f}},
            {});
    }

    // --- cloud (200-210) -----------------------------------------------------
    {
        Seraphis::CloudParams params;
        checkPackDefaults(
            "cloud", controller,
            {Seraphis::kCloudRichnessId, Seraphis::kCloudInharmonicityId,
             Seraphis::kCloudTiltId, Seraphis::kCloudMutationId, Seraphis::kCloudGravityId,
             Seraphis::kCloudDriftDepthId, Seraphis::kCloudDriftSmoothnessId,
             Seraphis::kCloudStereoSpreadId, Seraphis::kCloudAttackId,
             Seraphis::kCloudDecayId, Seraphis::kCloudEnvOffsetSpreadId},
            params,
            [](Seraphis::CloudParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleCloudParamChange(p, id, v);
            },
            [](const Seraphis::CloudParams& p, IBStreamer& s) {
                Seraphis::saveCloudParams(p, s);
            },
            {{Seraphis::kCloudRichnessId, 0.60f},
             {Seraphis::kCloudInharmonicityId, 0.030f},
             {Seraphis::kCloudTiltId, 0.0f},
             {Seraphis::kCloudMutationId, 0.25f},
             {Seraphis::kCloudGravityId, 0.20f},
             {Seraphis::kCloudDriftDepthId, 0.0f},
             {Seraphis::kCloudDriftSmoothnessId, 0.5f},
             {Seraphis::kCloudStereoSpreadId, 0.35f},
             {Seraphis::kCloudAttackId, 0.05f},  // == HarmonicCloud::kMinAttackSec
             {Seraphis::kCloudDecayId, 0.5f},
             {Seraphis::kCloudEnvOffsetSpreadId, 0.0f}},
            {});
    }

    // --- morph (400-412) -----------------------------------------------------
    // ID 408 stores the COUNT, not the list index: Processor::
    // pushSpectralStatesIfPending passes morphParams_.stateCount straight into
    // applySpectralStates(count) (plan §3.3, plan.md:1320-1322), and
    // dropdown_mappings.h:132-135 records that index i selects a count of
    // i + kMinStates. Index 0 therefore stores 2.
    // IDs 409-412 store the factory-state INDEX (SpectralStateId), which is what
    // factoryStates_[...] is subscripted with (plan.md:2034).
    {
        Seraphis::MorphParams params;
        checkPackDefaults(
            "morph", controller,
            {Seraphis::kMorphEntropyId, Seraphis::kMorphBloomId, Seraphis::kMorphPositionId,
             Seraphis::kMorphTravelModeId, Seraphis::kMorphTravelRateId,
             Seraphis::kMorphSyncId, Seraphis::kMorphSyncNoteId,
             Seraphis::kMorphWaypointIntervalId, Seraphis::kMorphStateCountId,
             Seraphis::kMorphState0Id, Seraphis::kMorphState1Id, Seraphis::kMorphState2Id,
             Seraphis::kMorphState3Id},
            params,
            [](Seraphis::MorphParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleMorphParamChange(p, id, v);
            },
            [](const Seraphis::MorphParams& p, IBStreamer& s) {
                Seraphis::saveMorphParams(p, s);
            },
            {{Seraphis::kMorphEntropyId, 0.20f},
             {Seraphis::kMorphBloomId, 0.0f},
             {Seraphis::kMorphPositionId, 0.0f},
             {Seraphis::kMorphTravelRateId,
              Krate::DSP::SpectralMorphEngine::kMinTravelRate},
             {Seraphis::kMorphWaypointIntervalId, 2.0f}},
            {{Seraphis::kMorphTravelModeId, 0},   // External
             {Seraphis::kMorphSyncId, 0},         // Free
             {Seraphis::kMorphSyncNoteId, 4},     // "1 Bar"
             {Seraphis::kMorphStateCountId, 2},   // index 0 -> count 2
             {Seraphis::kMorphState0Id, 0},       // SineStack
             {Seraphis::kMorphState1Id, 3},       // Glass
             {Seraphis::kMorphState2Id, 0},       // SineStack
             {Seraphis::kMorphState3Id, 0}});     // SineStack
    }

    // --- life modulators + voice envelope (600-604, 700-704) -----------------
    {
        Seraphis::LifeModParams params;
        checkPackDefaults(
            "life", controller,
            {Seraphis::kLifeSpatialDepthId, Seraphis::kLifeSpatialRateId,
             Seraphis::kLifeSpatialCouplingId, Seraphis::kLifeSpatialGrowthId,
             Seraphis::kLifeVoiceWidthId, Seraphis::kEnvModeId,
             Seraphis::kEnvGrowthDurationId, Seraphis::kEnvStage0MsId,
             Seraphis::kEnvStage1MsId, Seraphis::kEnvReleaseMsId},
            params,
            [](Seraphis::LifeModParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleLifeModParamChange(p, id, v);
            },
            [](const Seraphis::LifeModParams& p, IBStreamer& s) {
                Seraphis::saveLifeModParams(p, s);
            },
            {{Seraphis::kLifeSpatialDepthId, 0.35f},
             {Seraphis::kLifeSpatialRateId, 0.1f},
             {Seraphis::kLifeSpatialCouplingId, 0.0f},
             {Seraphis::kLifeSpatialGrowthId, 0.0f},
             {Seraphis::kLifeVoiceWidthId, 100.0f},
             {Seraphis::kEnvGrowthDurationId, 10.0f},
             {Seraphis::kEnvStage0MsId, 2000.0f},
             {Seraphis::kEnvStage1MsId, 4000.0f},
             {Seraphis::kEnvReleaseMsId, 8000.0f}},
            {{Seraphis::kEnvModeId, 0}});  // Standard
    }

    // --- body (800-812) ------------------------------------------------------
    // 811/812 carry the COMPONENT member initializers (continuous_body.h:163-164:
    // kDefaultAgcEnabled = true, kDefaultResonatorBypass = false), not
    // SeraphisVoice::prepare()'s - prepare() touches neither.
    {
        Seraphis::BodyParams params;
        checkPackDefaults(
            "body", controller,
            {Seraphis::kBodyMaterialId, Seraphis::kBodyResonanceId,
             Seraphis::kBodyDampingId, Seraphis::kBodyKeyTrackingId, Seraphis::kBodyDriveId,
             Seraphis::kBodyMixId, Seraphis::kBodyCloudMixId, Seraphis::kBodyCloudDecayId,
             Seraphis::kBodyCloudSizeId, Seraphis::kBodyCloudDampingId,
             Seraphis::kBodyWidthId, Seraphis::kBodyInputAgcId,
             Seraphis::kBodyResonatorBypassId},
            params,
            [](Seraphis::BodyParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleBodyParamChange(p, id, v);
            },
            [](const Seraphis::BodyParams& p, IBStreamer& s) {
                Seraphis::saveBodyParams(p, s);
            },
            {{Seraphis::kBodyResonanceId, 0.7f},
             {Seraphis::kBodyDampingId, 0.25f},
             {Seraphis::kBodyKeyTrackingId, 1.0f},
             {Seraphis::kBodyDriveId, 1.0f},
             {Seraphis::kBodyMixId, 1.0f},
             {Seraphis::kBodyCloudMixId, 0.25f},
             {Seraphis::kBodyCloudDecayId, 4.0f},
             {Seraphis::kBodyCloudSizeId, 1.0f},
             {Seraphis::kBodyCloudDampingId, 0.3f},
             {Seraphis::kBodyWidthId, 1.0f}},
            {{Seraphis::kBodyMaterialId, 0},        // Glass
             {Seraphis::kBodyInputAgcId, 1},        // continuous_body.h:163
             {Seraphis::kBodyResonatorBypassId, 0}});  // continuous_body.h:164
    }

    // --- atmosphere (1000-1016) ---------------------------------------------
    // 1011-1016 carry AtmosphereEngine's own member initializers
    // (atmosphere_engine.h:798, :805-806, :813-814, :820, :828-829, :952) - again
    // NOT prepare()'s, which sets eight atmosphere values and none of these six.
    {
        Seraphis::AtmosphereParams params;
        checkPackDefaults(
            "atmosphere", controller,
            {Seraphis::kAtmosLevelId, Seraphis::kAtmosBlurId, Seraphis::kAtmosDensityId,
             Seraphis::kAtmosGrainSecondsId, Seraphis::kAtmosDriftDepthId,
             Seraphis::kAtmosPanSpreadId, Seraphis::kAtmosDecorrelationId,
             Seraphis::kAtmosFreezeMixId, Seraphis::kAtmosFreezeId,
             Seraphis::kAtmosDriftSmoothnessId, Seraphis::kAtmosDriftRangeId,
             Seraphis::kAtmosJitterId, Seraphis::kAtmosPositionId,
             Seraphis::kAtmosPositionSpreadId, Seraphis::kAtmosPitchId,
             Seraphis::kAtmosPitchSpreadId, Seraphis::kAtmosGrainEnvelopeId},
            params,
            [](Seraphis::AtmosphereParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleAtmosphereParamChange(p, id, v);
            },
            [](const Seraphis::AtmosphereParams& p, IBStreamer& s) {
                Seraphis::saveAtmosphereParams(p, s);
            },
            {{Seraphis::kAtmosLevelId, 0.5f},
             {Seraphis::kAtmosBlurId, 0.0f},
             {Seraphis::kAtmosDensityId, 4.0f},
             {Seraphis::kAtmosGrainSecondsId, 4.0f},
             {Seraphis::kAtmosDriftDepthId, 0.3f},
             {Seraphis::kAtmosPanSpreadId, 0.7f},
             {Seraphis::kAtmosDecorrelationId, 0.5f},
             {Seraphis::kAtmosFreezeMixId, 0.0f},
             {Seraphis::kAtmosDriftSmoothnessId, 0.7f},
             {Seraphis::kAtmosDriftRangeId, 2.0f},
             {Seraphis::kAtmosJitterId, 0.5f},
             {Seraphis::kAtmosPositionId, 1.0f},
             {Seraphis::kAtmosPositionSpreadId, 0.3f},
             {Seraphis::kAtmosPitchId, 0.0f},
             {Seraphis::kAtmosPitchSpreadId, 0.15f}},
            {{Seraphis::kAtmosFreezeId, 0},
             {Seraphis::kAtmosGrainEnvelopeId, 0}});  // Hann
    }

    // --- aether (1200-1217) --------------------------------------------------
    {
        Seraphis::AetherParams params;
        checkPackDefaults(
            "aether", controller,
            {Seraphis::kAetherMixId, Seraphis::kAetherSizeId, Seraphis::kAetherDensityId,
             Seraphis::kAetherDecayId, Seraphis::kAetherFreezeId,
             Seraphis::kAetherDimensionalityId, Seraphis::kAetherDampingId,
             Seraphis::kAetherPreDelayId, Seraphis::kAetherModDepthId,
             Seraphis::kAetherModSmoothnessId, Seraphis::kAetherShimmerOctaveId,
             Seraphis::kAetherShimmerFifthId, Seraphis::kAetherBloomSendId,
             Seraphis::kAetherBloomDecayId, Seraphis::kAetherSpectralDiffusionId,
             Seraphis::kAetherSizeBreathDepthId, Seraphis::kAetherTideDepthId,
             Seraphis::kAetherWidthId},
            params,
            [](Seraphis::AetherParams& p, Vst::ParamID id, Vst::ParamValue v) {
                Seraphis::handleAetherParamChange(p, id, v);
            },
            [](const Seraphis::AetherParams& p, IBStreamer& s) {
                Seraphis::saveAetherParams(p, s);
            },
            {{Seraphis::kAetherMixId, 0.35f},
             {Seraphis::kAetherSizeId, 0.50f},
             {Seraphis::kAetherDensityId, 0.70f},
             {Seraphis::kAetherDecayId, 4.0f},
             {Seraphis::kAetherDimensionalityId, 0.35f},
             {Seraphis::kAetherDampingId, 0.40f},
             {Seraphis::kAetherPreDelayId, 0.0f},
             {Seraphis::kAetherModDepthId, 0.25f},
             {Seraphis::kAetherModSmoothnessId, 0.60f},
             {Seraphis::kAetherShimmerOctaveId, 0.0f},
             {Seraphis::kAetherShimmerFifthId, 0.0f},
             {Seraphis::kAetherBloomSendId, 0.0f},
             {Seraphis::kAetherBloomDecayId, 0.50f},
             {Seraphis::kAetherSpectralDiffusionId, 0.0f},
             {Seraphis::kAetherSizeBreathDepthId, 0.20f},
             {Seraphis::kAetherTideDepthId, 0.20f},
             {Seraphis::kAetherWidthId, 1.0f}},
            {{Seraphis::kAetherFreezeId, 0}});
    }

    controller.terminate();
}
