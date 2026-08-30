#pragma once

// ==============================================================================
// Seraphis - Life Modulators (600-604) + Voice Envelope (700-704)
// ==============================================================================
// ONE pack, ONE band: 600-799 is the Life-Modulator reserve (plugin_ids.h:65-66),
// and the voice envelope's five IDs live inside it. The six-function contract
// (global_params.h:85, :124, :169, :211, :218, :240) applies unchanged.
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kEnvelopeModeLabels (ID 700)
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/orbit_modulator.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults
// ==============================================================================

inline constexpr double kLifeSpatialRateMin =
    static_cast<double>(Krate::DSP::OrbitModulator::kMinRate);            // :108
inline constexpr double kLifeSpatialRateMax =
    static_cast<double>(Krate::DSP::OrbitModulator::kMaxRate);            // :110
inline constexpr double kLifeGrowthMin = -1.0;
inline constexpr double kLifeGrowthMax = 1.0;
inline constexpr double kLifeVoiceWidthMin = 50.0;   ///< percent
inline constexpr double kLifeVoiceWidthMax = 150.0;  ///< percent
inline constexpr double kEnvGrowthDurationMin =
    static_cast<double>(Krate::DSP::GrowthEnvelope::kMinDuration);        // :96
inline constexpr double kEnvGrowthDurationMax =
    static_cast<double>(Krate::DSP::GrowthEnvelope::kMaxDuration);        // :98

/// THE 1 ms FLOOR ON IDs 702-704 IS LOAD-BEARING, NOT COSMETIC (spec C-6's note,
/// plan 2.3.2). `logMapFromNormalized` is `clamp(mn * pow(mx/mn, u), mn, mx)`
/// (parameter_helpers.h:80-83); at `mn == 0` the ratio is +inf, `pow(+inf, u)` is
/// +inf for every u > 0, and `0 * inf` is NaN - which std::clamp PROPAGATES,
/// because both of its comparisons are false. All three IDs are MB-routed, so
/// FR-003's isFiniteBits rejection would then silently keep the kRows literal and
/// leave three parameters permanently inert. MultiStageEnvelope::setStageTime
/// clamps to [0, kMaxStageTimeMs], so 1 ms is inaudible at the DSP end.
inline constexpr double kEnvStageTimeMinMs = 1.0;
inline constexpr double kEnvStageTimeMaxMs =
    static_cast<double>(Krate::DSP::MultiStageEnvelope::kMaxStageTimeMs);  // :65

inline constexpr double kLifeSpatialDepthDefault    = 0.35;
inline constexpr double kLifeSpatialRateDefault     = 0.1;
inline constexpr double kLifeSpatialCouplingDefault = 0.0;
inline constexpr double kLifeSpatialGrowthDefault   = 0.0;
inline constexpr double kLifeVoiceWidthDefault      = 100.0;
inline constexpr double kEnvGrowthDurationDefault   = 10.0;
inline constexpr double kEnvStage0MsDefault         = 2000.0;
inline constexpr double kEnvStage1MsDefault         = 4000.0;
inline constexpr double kEnvReleaseMsDefault        = 8000.0;
inline constexpr int    kEnvModeDefaultIndex        = 0;  ///< Standard

// ==============================================================================
// Parameter Storage - 9 float + 1 int atomics
// ==============================================================================

struct LifeModParams {
    std::atomic<float> spatialDepth{static_cast<float>(kLifeSpatialDepthDefault)};
    std::atomic<float> spatialRateHz{static_cast<float>(kLifeSpatialRateDefault)};
    std::atomic<float> spatialCoupling{static_cast<float>(kLifeSpatialCouplingDefault)};
    std::atomic<float> spatialGrowth{static_cast<float>(kLifeSpatialGrowthDefault)};
    std::atomic<float> voiceWidthPercent{static_cast<float>(kLifeVoiceWidthDefault)};
    std::atomic<float> growthDurationSec{static_cast<float>(kEnvGrowthDurationDefault)};
    std::atomic<float> stage0Ms{static_cast<float>(kEnvStage0MsDefault)};
    std::atomic<float> stage1Ms{static_cast<float>(kEnvStage1MsDefault)};
    std::atomic<float> releaseMs{static_cast<float>(kEnvReleaseMsDefault)};

    /// ID 700. The list INDEX; SeraphisVoice::EnvelopeMode is recovered through
    /// toEnvelopeMode() (dropdown_mappings.h:234).
    std::atomic<int> envMode{kEnvModeDefaultIndex};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

inline void handleLifeModParamChange(
    LifeModParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    const float unit = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kLifeSpatialDepthId:
            params.spatialDepth.store(unit, std::memory_order_relaxed);
            break;
        case kLifeSpatialRateId:
            params.spatialRateHz.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kLifeSpatialRateMin, kLifeSpatialRateMax)),
                std::memory_order_relaxed);
            break;
        case kLifeSpatialCouplingId:
            params.spatialCoupling.store(unit, std::memory_order_relaxed);
            break;
        case kLifeSpatialGrowthId:
            params.spatialGrowth.store(
                std::clamp(static_cast<float>(kLifeGrowthMin +
                                              value * (kLifeGrowthMax - kLifeGrowthMin)),
                           static_cast<float>(kLifeGrowthMin),
                           static_cast<float>(kLifeGrowthMax)),
                std::memory_order_relaxed);
            break;
        case kLifeVoiceWidthId:
            params.voiceWidthPercent.store(
                std::clamp(static_cast<float>(kLifeVoiceWidthMin +
                                              value * (kLifeVoiceWidthMax -
                                                       kLifeVoiceWidthMin)),
                           static_cast<float>(kLifeVoiceWidthMin),
                           static_cast<float>(kLifeVoiceWidthMax)),
                std::memory_order_relaxed);
            break;
        case kEnvModeId:
            params.envMode.store(
                std::clamp(static_cast<int>(
                               value * static_cast<double>(kEnvelopeModeLabels.size() - 1) +
                               0.5),
                           0, static_cast<int>(kEnvelopeModeLabels.size()) - 1),
                std::memory_order_relaxed);
            break;
        case kEnvGrowthDurationId:
            params.growthDurationSec.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kEnvGrowthDurationMin, kEnvGrowthDurationMax)),
                std::memory_order_relaxed);
            break;
        case kEnvStage0MsId:
            params.stage0Ms.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kEnvStageTimeMinMs, kEnvStageTimeMaxMs)),
                std::memory_order_relaxed);
            break;
        case kEnvStage1MsId:
            params.stage1Ms.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kEnvStageTimeMinMs, kEnvStageTimeMaxMs)),
                std::memory_order_relaxed);
            break;
        case kEnvReleaseMsId:
            params.releaseMs.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kEnvStageTimeMinMs, kEnvStageTimeMaxMs)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-016, FR-048)
// ==============================================================================

inline void registerLifeModParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Spatial Depth"), STR16(""), 0,
                            kLifeSpatialDepthDefault,
                            ParameterInfo::kCanAutomate, kLifeSpatialDepthId);
    parameters.addParameter(STR16("Spatial Rate"), STR16("Hz"), 0,
                            Krate::Plugins::logMapToNormalized(kLifeSpatialRateDefault,
                                                               kLifeSpatialRateMin,
                                                               kLifeSpatialRateMax),
                            ParameterInfo::kCanAutomate, kLifeSpatialRateId);
    parameters.addParameter(STR16("Spatial Coupling"), STR16(""), 0,
                            kLifeSpatialCouplingDefault,
                            ParameterInfo::kCanAutomate, kLifeSpatialCouplingId);
    parameters.addParameter(STR16("Spatial Growth"), STR16(""), 0,
                            (kLifeSpatialGrowthDefault - kLifeGrowthMin) /
                                (kLifeGrowthMax - kLifeGrowthMin),
                            ParameterInfo::kCanAutomate, kLifeSpatialGrowthId);
    parameters.addParameter(STR16("Voice Width"), STR16("%"), 0,
                            (kLifeVoiceWidthDefault - kLifeVoiceWidthMin) /
                                (kLifeVoiceWidthMax - kLifeVoiceWidthMin),
                            ParameterInfo::kCanAutomate, kLifeVoiceWidthId);

    // ID 700 through Seraphis::addDropdownParam (dropdown_mappings.h) - the one
    // path that pins info.defaultNormalizedValue, used even though this default
    // index is 0, so changing it later cannot silently un-pin the default.
    addDropdownParam(parameters, STR16("Envelope Mode"), kEnvModeId, kEnvModeDefaultIndex,
                     kEnvelopeModeLabels.data(),
                     static_cast<int>(kEnvelopeModeLabels.size()));

    parameters.addParameter(STR16("Growth Duration"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(kEnvGrowthDurationDefault,
                                                               kEnvGrowthDurationMin,
                                                               kEnvGrowthDurationMax),
                            ParameterInfo::kCanAutomate, kEnvGrowthDurationId);
    parameters.addParameter(STR16("Env Stage 0"), STR16("ms"), 0,
                            Krate::Plugins::logMapToNormalized(kEnvStage0MsDefault,
                                                               kEnvStageTimeMinMs,
                                                               kEnvStageTimeMaxMs),
                            ParameterInfo::kCanAutomate, kEnvStage0MsId);
    parameters.addParameter(STR16("Env Stage 1"), STR16("ms"), 0,
                            Krate::Plugins::logMapToNormalized(kEnvStage1MsDefault,
                                                               kEnvStageTimeMinMs,
                                                               kEnvStageTimeMaxMs),
                            ParameterInfo::kCanAutomate, kEnvStage1MsId);
    parameters.addParameter(STR16("Env Release"), STR16("ms"), 0,
                            Krate::Plugins::logMapToNormalized(kEnvReleaseMsDefault,
                                                               kEnvStageTimeMinMs,
                                                               kEnvStageTimeMaxMs),
                            ParameterInfo::kCanAutomate, kEnvReleaseMsId);
}

// ==============================================================================
// Display Formatting (FR-061: ID 700 formats itself and is NOT claimed here)
// ==============================================================================

inline Steinberg::tresult formatLifeModParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        case kLifeSpatialDepthId:
        case kLifeSpatialCouplingId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kLifeSpatialRateId:
            snprintf(text, sizeof(text), "%.3f Hz",
                     Krate::Plugins::logMapFromNormalized(value, kLifeSpatialRateMin,
                                                          kLifeSpatialRateMax));
            break;
        case kLifeSpatialGrowthId:
            snprintf(text, sizeof(text), "%.2f",
                     kLifeGrowthMin + value * (kLifeGrowthMax - kLifeGrowthMin));
            break;
        case kLifeVoiceWidthId:
            snprintf(text, sizeof(text), "%.0f%%",
                     kLifeVoiceWidthMin +
                         value * (kLifeVoiceWidthMax - kLifeVoiceWidthMin));
            break;
        case kEnvGrowthDurationId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(value, kEnvGrowthDurationMin,
                                                          kEnvGrowthDurationMax));
            break;
        case kEnvStage0MsId:
        case kEnvStage1MsId:
        case kEnvReleaseMsId:
            snprintf(text, sizeof(text), "%.0f ms",
                     Krate::Plugins::logMapFromNormalized(value, kEnvStageTimeMinMs,
                                                          kEnvStageTimeMaxMs));
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 40 bytes, plan 5.1's [life] block order
// ==============================================================================
// 9 floats (600-604, 701-704) | 1 int32 (700 envMode).

inline void saveLifeModParams(const LifeModParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.spatialDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spatialRateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spatialCoupling.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spatialGrowth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.voiceWidthPercent.load(std::memory_order_relaxed));
    streamer.writeFloat(params.growthDurationSec.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stage0Ms.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stage1Ms.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));

    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.envMode.load(std::memory_order_relaxed)));
}

/// EOF-safe (FR-093).
inline bool loadLifeModParams(LifeModParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.spatialDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.spatialRateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.spatialCoupling.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.spatialGrowth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.voiceWidthPercent.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.growthDurationSec.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.stage0Ms.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.stage1Ms.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.releaseMs.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.envMode.store(
        std::clamp(static_cast<int>(iv), 0,
                   static_cast<int>(kEnvelopeModeLabels.size()) - 1),
        std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadLifeModParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (streamer.readFloat(fv)) {
        setParam(kLifeSpatialDepthId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kLifeSpatialRateId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kLifeSpatialRateMin,
                                                    kLifeSpatialRateMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kLifeSpatialCouplingId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kLifeSpatialGrowthId,
                 std::clamp((static_cast<double>(fv) - kLifeGrowthMin) /
                                (kLifeGrowthMax - kLifeGrowthMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kLifeVoiceWidthId,
                 std::clamp((static_cast<double>(fv) - kLifeVoiceWidthMin) /
                                (kLifeVoiceWidthMax - kLifeVoiceWidthMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kEnvGrowthDurationId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kEnvGrowthDurationMin,
                                                    kEnvGrowthDurationMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kEnvStage0MsId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kEnvStageTimeMinMs, kEnvStageTimeMaxMs));
    }
    if (streamer.readFloat(fv)) {
        setParam(kEnvStage1MsId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kEnvStageTimeMinMs, kEnvStageTimeMaxMs));
    }
    if (streamer.readFloat(fv)) {
        setParam(kEnvReleaseMsId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kEnvStageTimeMinMs, kEnvStageTimeMaxMs));
    }
    if (streamer.readInt32(iv)) {
        setParam(kEnvModeId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kEnvelopeModeLabels.size()) - 1)) /
                     static_cast<double>(kEnvelopeModeLabels.size() - 1));
    }
}

}  // namespace Seraphis
