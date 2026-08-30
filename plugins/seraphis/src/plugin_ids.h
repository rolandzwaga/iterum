#pragma once

// ==============================================================================
// Seraphis - Plugin Identifiers and Parameter IDs
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seraphis {

/// FR-012. State version for serialization (bump when the format changes
/// post-release). Shared by processor and controller; neither includes the
/// other, so the constants live here.
///
/// Every version is NAMED so the FR-093 / FR-031 migration path is expressed
/// against symbols and never against bare literals. Each older stream is a
/// STRICT BYTE PREFIX of the next (Phase 9 spec C-8, Phase 10 spec C-8), which
/// is what lets the EOF-safe loader chain migrate with no version-aware branch.
constexpr Steinberg::int32 kStateVersion1       = 1;  ///< Phase 8's 36-byte layout.
constexpr Steinberg::int32 kStateVersion2       = 2;  ///< Phase 9 (spec C-8), 2532 bytes.
constexpr Steinberg::int32 kCurrentStateVersion = 3;  ///< Phase 10: + the 16 effects fields.

/// FR-011. Freshly generated (v4 GUID), never reused, never changed
/// post-release. Processor component ID - the audio processing component
/// (runs on the audio thread).
static const Steinberg::FUID kProcessorUID(0xD13457BF, 0x55DC4576, 0xA26AF99B, 0x8873244D);

/// FR-011. Controller component ID - the edit controller component
/// (runs on the UI thread).
static const Steinberg::FUID kControllerUID(0x18FAB644, 0xBA15411A, 0x8F635433, 0x1FB8B7C5);

/// FR-014. DEF_CLASS2 subcategory string; instrument.
/// Deliberately `const`, NOT `constexpr` (cross-platform rule: anything
/// initialized from / handed to an SDK constant is `const`). The pointer itself
/// is also `const` so the unused-in-this-TU case falls under
/// `-Wunused-const-variable` (off by default in C++) rather than
/// `-Wunused-variable`, which GCC 13 emits for a mutable namespace-scope static
/// in every TU that includes this header without using it.
static const char* const kSubCategories = "Instrument|Synth";

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Constitution Principle V: all parameter values at the VST boundary are
// normalized (0.0 to 1.0).
//
/// FR-013 / spec C-5. Reserved ranges. The "start at 0 with Ruinae-style 100-ID
/// section gaps" decision is roadmap line 396; the eight-band reserve list is
/// roadmap lines 399-401. (The pre-Phase-9 comment cited a span that stopped
/// before six of the eight bands. Both citations were re-verified after FR-058's
/// roadmap amendment, which shifted every line after roadmap line 313.)
///
/// Every parameter sits in the band of the COMPONENT IT CONTROLS, without
/// exception - the band is a property of the target, never of the modulator
/// family a control happens to belong to.
///
///   0-99      Global            (Phase 8 - SHIPPED; +kSeedId in Phase 9)
///   100-199   Macros            (Phase 8 - SHIPPED; no longer inert in Phase 9)
///   200-399   Harmonic Cloud    (Phase 9 - SHIPPED)
///   400-599   Spectral Morph / Entropy (Phase 9 - SHIPPED)
///   600-799   Life Modulators   (Phase 9 - SHIPPED; 600-699 orbit/width,
///                                700-799 voice envelope - one pack, one band)
///   800-999   Continuous Body   (Phase 9 - SHIPPED)
///   1000-1199 Atmosphere        (Phase 9 - SHIPPED)
///   1200-1399 Aether            (Phase 9 - SHIPPED)
///   1400+     Effects           (Phase 10)
// ==============================================================================
enum ParameterIDs : Steinberg::Vst::ParamID {
    // --- Global (0-99) ---
    kMasterGainId = 0,
    kPolyphonyId  = 1,
    kSoftLimitId  = 2,
    kSeedId       = 3,  ///< Phase 9. 16 curated seeds (spec C-10); index 0 == 1u.

    // --- Macros (100-199) ---
    kMacroDreamId    = 100,
    kMacroBloomId    = 101,
    kMacroDissolveId = 102,
    kMacroGravityId  = 103,
    kMacroEntropyId  = 104,

    // --- Harmonic Cloud (200-399) --- 11 IDs
    kCloudRichnessId        = 200,
    kCloudInharmonicityId   = 201,
    kCloudTiltId            = 202,
    kCloudMutationId        = 203,
    kCloudGravityId         = 204,
    kCloudDriftDepthId      = 205,
    kCloudDriftSmoothnessId = 206,
    kCloudStereoSpreadId    = 207,
    kCloudAttackId          = 208,
    kCloudDecayId           = 209,
    kCloudEnvOffsetSpreadId = 210,

    // --- Spectral Morph / Entropy (400-599) --- 13 IDs
    kMorphEntropyId          = 400,
    kMorphBloomId            = 401,
    kMorphPositionId         = 402,
    kMorphTravelModeId       = 403,
    kMorphTravelRateId       = 404,
    kMorphSyncId             = 405,
    kMorphSyncNoteId         = 406,
    kMorphWaypointIntervalId = 407,
    kMorphStateCountId       = 408,
    kMorphState0Id           = 409,
    kMorphState1Id           = 410,
    kMorphState2Id           = 411,
    kMorphState3Id           = 412,

    // --- Life Modulators (600-699) --- 5 IDs
    kLifeSpatialDepthId    = 600,
    kLifeSpatialRateId     = 601,
    kLifeSpatialCouplingId = 602,
    kLifeSpatialGrowthId   = 603,
    kLifeVoiceWidthId      = 604,

    // --- Voice Envelope (700-799, inside the Life-Modulator band) --- 5 IDs
    kEnvModeId            = 700,
    kEnvGrowthDurationId  = 701,
    kEnvStage0MsId        = 702,
    kEnvStage1MsId        = 703,
    kEnvReleaseMsId       = 704,

    // --- Continuous Body (800-999) --- 13 IDs
    kBodyMaterialId         = 800,
    kBodyResonanceId        = 801,
    kBodyDampingId          = 802,
    kBodyKeyTrackingId      = 803,
    kBodyDriveId            = 804,
    kBodyMixId              = 805,
    kBodyCloudMixId         = 806,
    kBodyCloudDecayId       = 807,
    kBodyCloudSizeId        = 808,
    kBodyCloudDampingId     = 809,
    kBodyWidthId            = 810,
    kBodyInputAgcId         = 811,
    kBodyResonatorBypassId  = 812,

    // --- Granular Atmosphere (1000-1199) --- 17 IDs
    kAtmosLevelId           = 1000,
    kAtmosBlurId            = 1001,
    kAtmosDensityId         = 1002,
    kAtmosGrainSecondsId    = 1003,
    kAtmosDriftDepthId      = 1004,
    kAtmosPanSpreadId       = 1005,
    kAtmosDecorrelationId   = 1006,
    kAtmosFreezeMixId       = 1007,
    kAtmosFreezeId          = 1008,
    kAtmosDriftSmoothnessId = 1009,
    kAtmosDriftRangeId      = 1010,
    kAtmosJitterId          = 1011,
    kAtmosPositionId        = 1012,
    kAtmosPositionSpreadId  = 1013,
    kAtmosPitchId           = 1014,
    kAtmosPitchSpreadId     = 1015,
    kAtmosGrainEnvelopeId   = 1016,

    // --- Aether Space (1200-1399) --- 18 IDs
    kAetherMixId               = 1200,
    kAetherSizeId              = 1201,
    kAetherDensityId           = 1202,
    kAetherDecayId             = 1203,
    kAetherFreezeId            = 1204,
    kAetherDimensionalityId    = 1205,
    kAetherDampingId           = 1206,
    kAetherPreDelayId          = 1207,
    kAetherModDepthId          = 1208,
    kAetherModSmoothnessId     = 1209,
    kAetherShimmerOctaveId     = 1210,
    kAetherShimmerFifthId      = 1211,
    kAetherBloomSendId         = 1212,
    kAetherBloomDecayId        = 1213,
    kAetherSpectralDiffusionId = 1214,
    kAetherSizeBreathDepthId   = 1215,
    kAetherTideDepthId         = 1216,
    kAetherWidthId             = 1217,

    // --- Effects (1400-1499) --- 16 IDs (Phase 10)
    kFxSaturationId            = 1400,
    kFxDelayMixId              = 1410,
    kFxDelayTimeId             = 1411,
    kFxDelaySpreadId           = 1412,
    kFxDelaySpreadDirectionId  = 1413,
    kFxDelayFeedbackId         = 1414,
    kFxDelayTiltId             = 1415,
    kFxDelayDiffusionId        = 1416,
    kFxDelayWidthId            = 1417,
    kFxDelaySyncId             = 1418,
    kFxDelaySyncNoteId         = 1419,
    kFxSpectralFreezeId        = 1430,
    kFxWidthId                 = 1440,
    kFxWanderDepthId           = 1441,
    kFxWanderRateId            = 1442,
    kFxAzimuthDepthId          = 1443,
};

/// FR-013 / FR-048 / spec C-9. REGISTERED TYPES ARE FROZEN FOR THE LIFE OF THE
/// PLUGIN. NEVER swap a type at a live ID: a `RangeParameter` <->
/// `StringListParameter` swap breaks editor load in DAWs that cache parameter
/// metadata, because the cached metadata no longer matches what the controller
/// registers. If a control's render style must change, write a custom CView or
/// claim a NEW ParamID - never re-type an existing one.
///
/// Legend, matching spec C-6's *Type* column:
///   R = plain Steinberg::Vst::Parameter
///   L = Steinberg::Vst::StringListParameter, which is what
///       createDropdownParameterWithDefault returns
///       (plugins/shared/src/ui/parameter_helpers.h:47)
///   T = stepped toggle, a plain Vst::Parameter with stepCount = 1
///
/// All 107 registered IDs, grouped by type (85 R + 14 L + 8 T = 107):
///
///   R (85):
///     0                                   kMasterGainId
///     100-104                             kMacroDreamId .. kMacroEntropyId
///     200-210                             kCloudRichnessId .. kCloudEnvOffsetSpreadId
///     400-402, 404, 407                   kMorphEntropyId, kMorphBloomId,
///                                         kMorphPositionId, kMorphTravelRateId,
///                                         kMorphWaypointIntervalId
///     600-604                             kLifeSpatialDepthId .. kLifeVoiceWidthId
///     701-704                             kEnvGrowthDurationId, kEnvStage0MsId,
///                                         kEnvStage1MsId, kEnvReleaseMsId
///     801-810                             kBodyResonanceId .. kBodyWidthId
///     1000-1007, 1009-1015                kAtmosLevelId .. kAtmosFreezeMixId,
///                                         kAtmosDriftSmoothnessId .. kAtmosPitchSpreadId
///     1200-1203, 1205-1217                kAetherMixId .. kAetherDecayId,
///                                         kAetherDimensionalityId .. kAetherWidthId
///     1400                                kFxSaturationId
///     1410-1412, 1414-1417                kFxDelayMixId .. kFxDelaySpreadId,
///                                         kFxDelayFeedbackId .. kFxDelayWidthId
///     1440-1443                           kFxWidthId .. kFxAzimuthDepthId
///
///   L (14) - one label table each in parameters/dropdown_mappings.h:
///     1     kPolyphonyId          (Phase 8 - SHIPPED, type already frozen)
///     3     kSeedId               (Phase 9, new)
///     403   kMorphTravelModeId    (Phase 9, new)
///     406   kMorphSyncNoteId      (Phase 9, new)
///     408   kMorphStateCountId    (Phase 9, new)
///     409   kMorphState0Id        (Phase 9, new)
///     410   kMorphState1Id        (Phase 9, new)
///     411   kMorphState2Id        (Phase 9, new)
///     412   kMorphState3Id        (Phase 9, new)
///     700   kEnvModeId            (Phase 9, new)
///     800   kBodyMaterialId       (Phase 9, new)
///     1016  kAtmosGrainEnvelopeId (Phase 9, new)
///     1413  kFxDelaySpreadDirectionId (Phase 10, new)
///     1419  kFxDelaySyncNoteId        (Phase 10, new)
///   NOTE: plan section 2.1(e) enumerates TEN new `L` IDs and omits 700. C-6's
///   table types kEnvModeId as `L` (Standard / Growth) and plan section 2.2 gives it
///   its own `kEnvelopeModeLabels` table, so the new-`L` count is ELEVEN and 700
///   is registered as a StringListParameter. The list above is the record.
///
///   T (8) - stepped toggles, stepCount = 1:
///     2     kSoftLimitId           (Phase 8 - SHIPPED, type already frozen)
///     405   kMorphSyncId           (Phase 9, new)
///     811   kBodyInputAgcId        (Phase 9, new)
///     812   kBodyResonatorBypassId (Phase 9, new)
///     1008  kAtmosFreezeId         (Phase 9, new)
///     1204  kAetherFreezeId        (Phase 9, new)
///     1418  kFxDelaySyncId         (Phase 10, new)
///     1430  kFxSpectralFreezeId    (Phase 10, new)

/// Range-dispatch bounds used by processParameterChanges (FR-011, FR-042).
/// A ladder of upper bounds, so the dispatch stays `if (id < X)` and never
/// becomes a 91-case switch. Each constant is the FIRST ID of the NEXT band.
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd  =  100;  // IDs <  100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd   =  200;  // IDs <  200 -> macro pack
constexpr Steinberg::Vst::ParamID kCloudParamRangeEnd   =  400;  // IDs <  400 -> cloud pack
constexpr Steinberg::Vst::ParamID kMorphParamRangeEnd   =  600;  // IDs <  600 -> morph pack
constexpr Steinberg::Vst::ParamID kLifeModParamRangeEnd =  800;  // 600-799: life mods + envelope
constexpr Steinberg::Vst::ParamID kBodyParamRangeEnd    = 1000;  // IDs < 1000 -> body pack
constexpr Steinberg::Vst::ParamID kAtmosParamRangeEnd   = 1200;  // IDs < 1200 -> atmosphere pack
constexpr Steinberg::Vst::ParamID kAetherParamRangeEnd  = 1400;  // IDs < 1400 -> aether pack
constexpr Steinberg::Vst::ParamID kEffectsParamRangeEnd = 1500;  // IDs < 1500 -> effects pack

// --- Immediate compile-time gates for the ID map -----------------------------

static_assert(kGlobalParamRangeEnd < kMacroParamRangeEnd &&
                  kMacroParamRangeEnd < kCloudParamRangeEnd &&
                  kCloudParamRangeEnd < kMorphParamRangeEnd &&
                  kMorphParamRangeEnd < kLifeModParamRangeEnd &&
                  kLifeModParamRangeEnd < kBodyParamRangeEnd &&
                  kBodyParamRangeEnd < kAtmosParamRangeEnd &&
                  kAtmosParamRangeEnd < kAetherParamRangeEnd &&
                  kAetherParamRangeEnd < kEffectsParamRangeEnd,
              "FR-011: the range-dispatch ladder must be strictly increasing, or "
              "processParameterChanges routes an ID to the wrong pack");

static_assert(kMasterGainId < kGlobalParamRangeEnd && kSeedId < kGlobalParamRangeEnd &&
                  kMacroDreamId >= kGlobalParamRangeEnd &&
                  kMacroEntropyId < kMacroParamRangeEnd &&
                  kCloudRichnessId >= kMacroParamRangeEnd &&
                  kCloudEnvOffsetSpreadId < kCloudParamRangeEnd &&
                  kMorphEntropyId >= kCloudParamRangeEnd &&
                  kMorphState3Id < kMorphParamRangeEnd,
              "spec C-5: global / macro / cloud / morph IDs must lie inside their bands");

static_assert(kLifeSpatialDepthId >= kMorphParamRangeEnd &&
                  kEnvReleaseMsId < kLifeModParamRangeEnd &&
                  kBodyMaterialId >= kLifeModParamRangeEnd &&
                  kBodyResonatorBypassId < kBodyParamRangeEnd &&
                  kAtmosLevelId >= kBodyParamRangeEnd &&
                  kAtmosGrainEnvelopeId < kAtmosParamRangeEnd &&
                  kAetherMixId >= kAtmosParamRangeEnd &&
                  kAetherWidthId < kAetherParamRangeEnd,
              "spec C-5: life / body / atmosphere / aether IDs must lie inside their bands");

static_assert(kFxSaturationId >= kAetherParamRangeEnd &&
                  kFxAzimuthDepthId < kEffectsParamRangeEnd,
              "spec C-6: effects IDs must lie inside the 1400+ band");

static_assert(kStateVersion1 < kStateVersion2 && kStateVersion2 < kCurrentStateVersion,
              "FR-012 / FR-031: the state version chain must be strictly increasing");

}  // namespace Seraphis
