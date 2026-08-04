// ==============================================================================
// Seraphis - Parameter surface tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.3)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-001  the registered surface is complete - 107 IDs, exactly C-6's set,
//           each with the stepCount its Type column demands, plus the
//           getParamStringByValue section that covers FR-061
//   SC-014  the eight Phase 8 IDs are frozen field-for-field
//   SC-015  every registered ID has a control tag in editor.uidesc, and no
//           orphan tag exists
//   SC-022  every registered default matches the C-6 table
//
// PHASE 11 (T019) ADDS TWO MORE to Seraphis_UidescControlTags_MatchRegisteredIds,
// because the layout it asserts about now exists:
//   SC-002  the binding budget is EXACTLY 110 - 107 primary bindings plus the
//           three enumerated second bindings of the header freeze cluster - and
//           unreachableParams() is empty with an EMPTY allowlist
//   SC-003  every bound view's class is in the SET its parameter's kind permits,
//           with MacroRingKnob enumerated by ID for 100..104
//
// All of them are PURE TABLE TESTS: no render, no engine. Nothing here prepares
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
enum class Kind : std::uint8_t { R, L, T };

struct SurfaceRow {
    Vst::ParamID id;
    Kind kind;
    /// Entry count for `L` rows; 0 otherwise. stepCount == entries - 1.
    int entries;
};

/// All 107 rows of spec C-6, in band order. 85 R + 14 L + 8 T
/// (plugin_ids.h:217-267 carries the same grouping as prose).
constexpr SurfaceRow kSurface[] = {
    // --- Global (0-99) -------------------------------------------------------
    {.id = Seraphis::kMasterGainId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kPolyphonyId, .kind = Kind::L, .entries = 16},
    {.id = Seraphis::kSoftLimitId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kSeedId, .kind = Kind::L, .entries = 16},

    // --- Macros (100-199) ----------------------------------------------------
    {.id = Seraphis::kMacroDreamId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMacroBloomId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMacroDissolveId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMacroGravityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMacroEntropyId, .kind = Kind::R, .entries = 0},

    // --- Harmonic Cloud (200-399) -------------------------------------------
    {.id = Seraphis::kCloudRichnessId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudInharmonicityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudTiltId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudMutationId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudGravityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudDriftDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudDriftSmoothnessId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudStereoSpreadId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudAttackId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudDecayId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kCloudEnvOffsetSpreadId, .kind = Kind::R, .entries = 0},

    // --- Spectral Morph / Entropy (400-599) ---------------------------------
    {.id = Seraphis::kMorphEntropyId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMorphBloomId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMorphPositionId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMorphTravelModeId, .kind = Kind::L, .entries = 2},
    {.id = Seraphis::kMorphTravelRateId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMorphSyncId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kMorphSyncNoteId, .kind = Kind::L, .entries = 8},
    {.id = Seraphis::kMorphWaypointIntervalId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kMorphStateCountId, .kind = Kind::L, .entries = 3},
    {.id = Seraphis::kMorphState0Id, .kind = Kind::L, .entries = 5},
    {.id = Seraphis::kMorphState1Id, .kind = Kind::L, .entries = 5},
    {.id = Seraphis::kMorphState2Id, .kind = Kind::L, .entries = 5},
    {.id = Seraphis::kMorphState3Id, .kind = Kind::L, .entries = 5},

    // --- Life Modulators (600-699) + Voice Envelope (700-799) ---------------
    {.id = Seraphis::kLifeSpatialDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kLifeSpatialRateId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kLifeSpatialCouplingId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kLifeSpatialGrowthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kLifeVoiceWidthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kEnvModeId, .kind = Kind::L, .entries = 2},
    {.id = Seraphis::kEnvGrowthDurationId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kEnvStage0MsId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kEnvStage1MsId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kEnvReleaseMsId, .kind = Kind::R, .entries = 0},

    // --- Continuous Body (800-999) ------------------------------------------
    {.id = Seraphis::kBodyMaterialId, .kind = Kind::L, .entries = 5},
    {.id = Seraphis::kBodyResonanceId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyDampingId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyKeyTrackingId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyDriveId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyMixId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyCloudMixId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyCloudDecayId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyCloudSizeId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyCloudDampingId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyWidthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kBodyInputAgcId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kBodyResonatorBypassId, .kind = Kind::T, .entries = 0},

    // --- Granular Atmosphere (1000-1199) ------------------------------------
    {.id = Seraphis::kAtmosLevelId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosBlurId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosDensityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosGrainSecondsId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosDriftDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosPanSpreadId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosDecorrelationId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosFreezeMixId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosFreezeId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kAtmosDriftSmoothnessId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosDriftRangeId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosJitterId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosPositionId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosPositionSpreadId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosPitchId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosPitchSpreadId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAtmosGrainEnvelopeId, .kind = Kind::L, .entries = 6},

    // --- Aether Space (1200-1399) -------------------------------------------
    {.id = Seraphis::kAetherMixId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherSizeId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherDensityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherDecayId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherFreezeId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kAetherDimensionalityId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherDampingId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherPreDelayId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherModDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherModSmoothnessId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherShimmerOctaveId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherShimmerFifthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherBloomSendId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherBloomDecayId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherSpectralDiffusionId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherSizeBreathDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherTideDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kAetherWidthId, .kind = Kind::R, .entries = 0},

    // --- Effects (1400-1499) - Phase 10 --------------------------------------
    // The two `L` rows carry the ENTRY COUNT, not the stepCount: `entries` is
    // documented above as "stepCount == entries - 1", so 1413's three
    // kFxSpreadDirectionLabels (dropdown_mappings.h:244-245) give stepCount 2 and
    // 1419's ten kFxDelaySyncNoteLabels (dropdown_mappings.h:267-269) give
    // stepCount 9 - exactly what T005's table demands.
    {.id = Seraphis::kFxSaturationId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelayMixId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelayTimeId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelaySpreadId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelaySpreadDirectionId, .kind = Kind::L, .entries = 3},
    {.id = Seraphis::kFxDelayFeedbackId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelayTiltId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelayDiffusionId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelayWidthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxDelaySyncId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kFxDelaySyncNoteId, .kind = Kind::L, .entries = 10},
    {.id = Seraphis::kFxSpectralFreezeId, .kind = Kind::T, .entries = 0},
    {.id = Seraphis::kFxWidthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxWanderDepthId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxWanderRateId, .kind = Kind::R, .entries = 0},
    {.id = Seraphis::kFxAzimuthDepthId, .kind = Kind::R, .entries = 0},
};

constexpr std::size_t kSurfaceRowCount = sizeof(kSurface) / sizeof(kSurface[0]);

static_assert(kSurfaceRowCount == 107,
              "SC-001: spec C-6 is a 107-row table (8 shipped + 83 Phase 9 + 16 Phase 10)");

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
    {.id = Seraphis::kSeedId, .labels = Seraphis::kSeedLabels.data(),
     .count=static_cast<int>(Seraphis::kSeedLabels.size())},
    {.id = Seraphis::kMorphTravelModeId, .labels = Seraphis::kTravelModeLabels.data(),
     .count=static_cast<int>(Seraphis::kTravelModeLabels.size())},
    {.id = Seraphis::kMorphSyncNoteId, .labels = Seraphis::kSyncNoteLabels.data(),
     .count=static_cast<int>(Seraphis::kSyncNoteLabels.size())},
    {.id = Seraphis::kMorphStateCountId, .labels = Seraphis::kStateCountLabels.data(),
     .count=static_cast<int>(Seraphis::kStateCountLabels.size())},
    {.id = Seraphis::kMorphState0Id, .labels = Seraphis::kSpectralStateLabels.data(),
     .count=static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {.id = Seraphis::kMorphState1Id, .labels = Seraphis::kSpectralStateLabels.data(),
     .count=static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {.id = Seraphis::kMorphState2Id, .labels = Seraphis::kSpectralStateLabels.data(),
     .count=static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {.id = Seraphis::kMorphState3Id, .labels = Seraphis::kSpectralStateLabels.data(),
     .count=static_cast<int>(Seraphis::kSpectralStateLabels.size())},
    {.id = Seraphis::kEnvModeId, .labels = Seraphis::kEnvelopeModeLabels.data(),
     .count=static_cast<int>(Seraphis::kEnvelopeModeLabels.size())},
    {.id = Seraphis::kBodyMaterialId, .labels = Seraphis::kBodyMaterialLabels.data(),
     .count=static_cast<int>(Seraphis::kBodyMaterialLabels.size())},
    {.id = Seraphis::kAtmosGrainEnvelopeId, .labels = Seraphis::kGrainEnvelopeLabels.data(),
     .count=static_cast<int>(Seraphis::kGrainEnvelopeLabels.size())},
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
    // UString only wraps a MUTABLE char16 buffer, so the read-only argument is copied
    // into one rather than const_cast away its constness.
    Vst::String128 owned{};
    UString(owned, 128).assign(s);
    char8 buffer[256] = {};
    UString(owned, 128).toAscii(buffer, 256);
    return {static_cast<const char*>(buffer)};
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
        views.push_back(BoundView{.viewClass = element.substr(classStart, classEnd - classStart),
                                  .tagName=element.substr(tagStart, tagEnd - tagStart)});
    }
    return views;
}

/// C-6's *Type* column, expressed as the SET of VSTGUI control classes Phase 11's
/// layout is allowed to draw that kind as (plan section 9, SC-003). FR-048 still
/// freezes the REGISTERED types; a mismatched control class binds with NO error
/// path, which is the only reason this is checked at all.
///
/// `T` is DELIBERATELY a singleton set. The header freeze cluster and every drawer
/// toggle are CCheckBox, `ToggleButton` appears nowhere in the shipped uidesc, and
/// widening `T` to {"CCheckBox", "ToggleButton"} would make this criterion unable
/// to detect EITHER choice.
[[nodiscard]] std::set<std::string> expectedViewClasses(Kind kind) {
    switch (kind) {
        case Kind::R: return {"CSlider", "ArcKnob"};
        case Kind::L: return {"COptionMenu"};
        case Kind::T: return {"CCheckBox"};
    }
    return {};
}

/// FR-020's exception to the `R` set, ENUMERATED BY ID rather than waived by
/// loosening the rule: exactly the five macro parameters draw as the big rings,
/// and a sixth MacroRingKnob anywhere else is a failure.
constexpr Vst::ParamID kMacroRingIds[] = {
    Seraphis::kMacroDreamId,   Seraphis::kMacroBloomId, Seraphis::kMacroDissolveId,
    Seraphis::kMacroGravityId, Seraphis::kMacroEntropyId,
};

[[nodiscard]] bool isMacroRingId(Vst::ParamID id) {
    return std::ranges::any_of(kMacroRingIds,
                               [id](const Vst::ParamID ring) { return ring == id; });
}

/// C-3's COMPLETE duplicate allowlist (RQ-2): the three freeze parameters the
/// header re-binds BESIDE their primary binding on the drawer page that owns
/// them. Every other registered ID is bound exactly once, and this list is
/// enumerated here so a fourth duplicate is a failure rather than a shrug.
constexpr Vst::ParamID kSecondBindingIds[] = {
    Seraphis::kAtmosFreezeId,
    Seraphis::kAetherFreezeId,
    Seraphis::kFxSpectralFreezeId,
};

[[nodiscard]] bool isSecondBindingId(Vst::ParamID id) {
    return std::ranges::any_of(kSecondBindingIds,
                               [id](const Vst::ParamID dup) { return dup == id; });
}

}  // namespace

// ==============================================================================
// SC-001 - the registered surface is exactly spec C-6
// ==============================================================================

TEST_CASE("Seraphis_ParameterSurface_IsComplete", "[seraphis][controller][params]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // --- the count -----------------------------------------------------------
    CHECK(controller.getParameterCount() == 107);

    // --- the ID set, in both directions, with no duplicate -------------------
    std::set<Vst::ParamID> registered;
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
        INFO("duplicate registration of parameter ID " << info.id);
        CHECK(registered.insert(info.id).second);
        // spec C-5: nothing may sit outside the reserved bands, which run
        // contiguously from 0 to kEffectsParamRangeEnd - 1. Phase 10 claimed the
        // ninth band (1400-1499, plugin_ids.h:280), so the bound moved up from
        // kAetherParamRangeEnd - loosening it any further would stop catching a
        // squatter above 1500.
        INFO("parameter ID " << info.id << " lies outside the reserved bands");
        CHECK(info.id < Seraphis::kEffectsParamRangeEnd);
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
        for (const auto & table : kDropdownTables) {
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
        for (unsigned int id : kAllDropdownIds) {
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
        {.id = Seraphis::kMasterGainId, .stepCount = 0, .defaultNormalized = 0.5, .units = "dB", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kPolyphonyId, .stepCount = 15, .defaultNormalized = 7.0 / 15.0, .units = "",
         .flags=Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList},
        {.id = Seraphis::kSoftLimitId, .stepCount = 1, .defaultNormalized = 1.0, .units = "", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kMacroDreamId, .stepCount = 0, .defaultNormalized = 0.0, .units = "%", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kMacroBloomId, .stepCount = 0, .defaultNormalized = 0.0, .units = "%", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kMacroDissolveId, .stepCount = 0, .defaultNormalized = 0.0, .units = "%", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kMacroGravityId, .stepCount = 0, .defaultNormalized = 0.5, .units = "%", .flags = Vst::ParameterInfo::kCanAutomate},
        {.id = Seraphis::kMacroEntropyId, .stepCount = 0, .defaultNormalized = 0.0, .units = "%", .flags = Vst::ParameterInfo::kCanAutomate},
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
    // Phase 9 added 83 tags with NO view on purpose (layout was Phase 11's), so
    // this case used to consume only the tag MAP. Phase 11 SHIPS the layout, so
    // Krate::Test::unreachableParams is now used as well - with an EMPTY
    // allowlist (T019, SC-002/C-3).
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

    // ==========================================================================
    // SC-002 / FR-003 (T019) - THE BINDING BUDGET, AND IT IS EXACT.
    // ==========================================================================
    // 110 = 107 primary bindings + the three SECOND bindings of the header freeze
    // cluster. If extractBoundViews returns anything else, the XML is what gets
    // fixed - never this number.
    const std::vector<BoundView> bound = extractBoundViews(xml);
    CHECK(bound.size() == 110u);

    // C-3: EVERY registered ID is reachable from some <view>, asserted with an
    // EMPTY allowlist. No parameter of this surface is driven by a custom view
    // instead of a control-tag, and the drawer is a plain CViewContainer with all
    // seven pages present rather than a UIViewSwitchContainer (which would realise
    // only the active page and strand six tabs' worth of IDs here).
    const std::vector<int> registeredIds(registered.begin(), registered.end());
    const std::vector<int> unreachable =
        Krate::Test::unreachableParams(xml, registeredIds, {});
    std::ostringstream unreachableList;
    for (const int id : unreachable) {
        unreachableList << id << ' ';
    }
    INFO("registered parameter IDs no <view> binds: " << unreachableList.str());
    CHECK(unreachable.empty());

    // The MULTISET of bound IDs: exactly twice for the enumerated three, exactly
    // once for every other registered ID, and never an ID that is not registered.
    std::map<int, int> bindCount;
    for (const BoundView& view : bound) {
        INFO("<view control-tag=\"" << view.tagName << "\"> names no <control-tag>");
        const auto it = tagMap.find(view.tagName);
        REQUIRE(it != tagMap.end());
        ++bindCount[it->second];
    }
    for (const int id : registered) {
        const int expectedCount =
            isSecondBindingId(static_cast<Vst::ParamID>(id)) ? 2 : 1;
        const auto found = bindCount.find(id);
        const int actualCount = (found == bindCount.end()) ? 0 : found->second;
        INFO("parameter ID " << id << " is bound " << actualCount << " time(s), want "
                             << expectedCount);
        CHECK(actualCount == expectedCount);
    }
    for (const auto& entry : bindCount) {
        INFO("a <view> binds tag " << entry.first << ", which names no registered "
                                                     "parameter");
        CHECK(registered.count(entry.first) == 1u);
    }

    // ==========================================================================
    // SC-003 / FR-004 - each bound view's class matches its parameter's kind.
    // ==========================================================================
    for (const BoundView& view : bound) {
        INFO("<view class=\"" << view.viewClass << "\" control-tag=\"" << view.tagName
                              << "\">");
        const auto it = tagMap.find(view.tagName);
        REQUIRE(it != tagMap.end());

        const auto id = static_cast<Vst::ParamID>(it->second);
        const SurfaceRow* row = nullptr;
        for (const SurfaceRow& candidate : kSurface) {
            if (candidate.id == id) {
                row = &candidate;
                break;
            }
        }
        REQUIRE(row != nullptr);

        std::set<std::string> allowed = expectedViewClasses(row->kind);
        if (isMacroRingId(id)) {
            allowed.insert("MacroRingKnob");
        }
        CHECK(allowed.count(view.viewClass) == 1u);
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
            {{.id = Seraphis::kMasterGainId, .expected = 1.0f}},
            {{.id = Seraphis::kPolyphonyId, .expected = 8},
             {.id = Seraphis::kSoftLimitId, .expected = 1},
             {.id = Seraphis::kSeedId, .expected = 0}});  // index 0 == seed 1u (C-10)
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
            {{.id = Seraphis::kMacroDreamId, .expected = 0.0f},
             {.id = Seraphis::kMacroBloomId, .expected = 0.0f},
             {.id = Seraphis::kMacroDissolveId, .expected = 0.0f},
             {.id = Seraphis::kMacroGravityId, .expected = 0.5f},  // bipolar around 0.5
             {.id = Seraphis::kMacroEntropyId, .expected = 0.0f}},
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
            {{.id = Seraphis::kCloudRichnessId, .expected = 0.60f},
             {.id = Seraphis::kCloudInharmonicityId, .expected = 0.030f},
             {.id = Seraphis::kCloudTiltId, .expected = 0.0f},
             {.id = Seraphis::kCloudMutationId, .expected = 0.25f},
             {.id = Seraphis::kCloudGravityId, .expected = 0.20f},
             {.id = Seraphis::kCloudDriftDepthId, .expected = 0.0f},
             {.id = Seraphis::kCloudDriftSmoothnessId, .expected = 0.5f},
             {.id = Seraphis::kCloudStereoSpreadId, .expected = 0.35f},
             {.id = Seraphis::kCloudAttackId, .expected = 0.05f},  // == HarmonicCloud::kMinAttackSec
             {.id = Seraphis::kCloudDecayId, .expected = 0.5f},
             {.id = Seraphis::kCloudEnvOffsetSpreadId, .expected = 0.0f}},
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
            {{.id = Seraphis::kMorphEntropyId, .expected = 0.20f},
             {.id = Seraphis::kMorphBloomId, .expected = 0.0f},
             {.id = Seraphis::kMorphPositionId, .expected = 0.0f},
             {.id = Seraphis::kMorphTravelRateId,
              .expected=Krate::DSP::SpectralMorphEngine::kMinTravelRate},
             {.id = Seraphis::kMorphWaypointIntervalId, .expected = 2.0f}},
            {{.id = Seraphis::kMorphTravelModeId, .expected = 0},   // External
             {.id = Seraphis::kMorphSyncId, .expected = 0},         // Free
             {.id = Seraphis::kMorphSyncNoteId, .expected = 4},     // "1 Bar"
             {.id = Seraphis::kMorphStateCountId, .expected = 2},   // index 0 -> count 2
             {.id = Seraphis::kMorphState0Id, .expected = 0},       // SineStack
             {.id = Seraphis::kMorphState1Id, .expected = 3},       // Glass
             {.id = Seraphis::kMorphState2Id, .expected = 0},       // SineStack
             {.id = Seraphis::kMorphState3Id, .expected = 0}});     // SineStack
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
            {{.id = Seraphis::kLifeSpatialDepthId, .expected = 0.35f},
             {.id = Seraphis::kLifeSpatialRateId, .expected = 0.1f},
             {.id = Seraphis::kLifeSpatialCouplingId, .expected = 0.0f},
             {.id = Seraphis::kLifeSpatialGrowthId, .expected = 0.0f},
             {.id = Seraphis::kLifeVoiceWidthId, .expected = 100.0f},
             {.id = Seraphis::kEnvGrowthDurationId, .expected = 10.0f},
             {.id = Seraphis::kEnvStage0MsId, .expected = 2000.0f},
             {.id = Seraphis::kEnvStage1MsId, .expected = 4000.0f},
             {.id = Seraphis::kEnvReleaseMsId, .expected = 8000.0f}},
            {{.id = Seraphis::kEnvModeId, .expected = 0}});  // Standard
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
            {{.id = Seraphis::kBodyResonanceId, .expected = 0.7f},
             {.id = Seraphis::kBodyDampingId, .expected = 0.25f},
             {.id = Seraphis::kBodyKeyTrackingId, .expected = 1.0f},
             {.id = Seraphis::kBodyDriveId, .expected = 1.0f},
             {.id = Seraphis::kBodyMixId, .expected = 1.0f},
             {.id = Seraphis::kBodyCloudMixId, .expected = 0.25f},
             {.id = Seraphis::kBodyCloudDecayId, .expected = 4.0f},
             {.id = Seraphis::kBodyCloudSizeId, .expected = 1.0f},
             {.id = Seraphis::kBodyCloudDampingId, .expected = 0.3f},
             {.id = Seraphis::kBodyWidthId, .expected = 1.0f}},
            {{.id = Seraphis::kBodyMaterialId, .expected = 0},        // Glass
             {.id = Seraphis::kBodyInputAgcId, .expected = 1},        // continuous_body.h:163
             {.id = Seraphis::kBodyResonatorBypassId, .expected = 0}});  // continuous_body.h:164
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
            {{.id = Seraphis::kAtmosLevelId, .expected = 0.5f},
             {.id = Seraphis::kAtmosBlurId, .expected = 0.0f},
             {.id = Seraphis::kAtmosDensityId, .expected = 4.0f},
             {.id = Seraphis::kAtmosGrainSecondsId, .expected = 4.0f},
             {.id = Seraphis::kAtmosDriftDepthId, .expected = 0.3f},
             {.id = Seraphis::kAtmosPanSpreadId, .expected = 0.7f},
             {.id = Seraphis::kAtmosDecorrelationId, .expected = 0.5f},
             {.id = Seraphis::kAtmosFreezeMixId, .expected = 0.0f},
             {.id = Seraphis::kAtmosDriftSmoothnessId, .expected = 0.7f},
             {.id = Seraphis::kAtmosDriftRangeId, .expected = 2.0f},
             {.id = Seraphis::kAtmosJitterId, .expected = 0.5f},
             {.id = Seraphis::kAtmosPositionId, .expected = 1.0f},
             {.id = Seraphis::kAtmosPositionSpreadId, .expected = 0.3f},
             {.id = Seraphis::kAtmosPitchId, .expected = 0.0f},
             {.id = Seraphis::kAtmosPitchSpreadId, .expected = 0.15f}},
            {{.id = Seraphis::kAtmosFreezeId, .expected = 0},
             {.id = Seraphis::kAtmosGrainEnvelopeId, .expected = 0}});  // Hann
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
            {{.id = Seraphis::kAetherMixId, .expected = 0.35f},
             {.id = Seraphis::kAetherSizeId, .expected = 0.50f},
             {.id = Seraphis::kAetherDensityId, .expected = 0.70f},
             {.id = Seraphis::kAetherDecayId, .expected = 4.0f},
             {.id = Seraphis::kAetherDimensionalityId, .expected = 0.35f},
             {.id = Seraphis::kAetherDampingId, .expected = 0.40f},
             {.id = Seraphis::kAetherPreDelayId, .expected = 0.0f},
             {.id = Seraphis::kAetherModDepthId, .expected = 0.25f},
             {.id = Seraphis::kAetherModSmoothnessId, .expected = 0.60f},
             {.id = Seraphis::kAetherShimmerOctaveId, .expected = 0.0f},
             {.id = Seraphis::kAetherShimmerFifthId, .expected = 0.0f},
             {.id = Seraphis::kAetherBloomSendId, .expected = 0.0f},
             {.id = Seraphis::kAetherBloomDecayId, .expected = 0.50f},
             {.id = Seraphis::kAetherSpectralDiffusionId, .expected = 0.0f},
             {.id = Seraphis::kAetherSizeBreathDepthId, .expected = 0.20f},
             {.id = Seraphis::kAetherTideDepthId, .expected = 0.20f},
             {.id = Seraphis::kAetherWidthId, .expected = 1.0f}},
            {{.id = Seraphis::kAetherFreezeId, .expected = 0}});
    }

    controller.terminate();
}
