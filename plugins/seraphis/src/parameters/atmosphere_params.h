#pragma once

// ==============================================================================
// Seraphis - Granular Atmosphere Parameters (ID 1000-1016)
// ==============================================================================
// The six-function contract (global_params.h:85, :124, :169, :211, :218, :241).
//
// ID 1012 IS `lin`, NOT `log`, AND THAT IS FORCED (spec C-6's note, plan 2.3.2):
// its plain range starts at 0, where logMapFromNormalized's `mn * pow(mx/mn, u)`
// evaluates `0 * inf` = NaN for every u > 0 - and a non-zero floor would make
// position 0 (grains born at the write head) unreachable, which is a musically
// meaningful setting.
//
// The six 1011-1016 defaults are the COMPONENT's own member initializers, not
// SeraphisVoice::prepare()'s: prepare() sets eight atmosphere values
// (seraphis_voice.h:319-327) and touches none of these six.
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kGrainEnvelopeLabels (ID 1016)
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/atmosphere_engine.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults
// ==============================================================================

inline constexpr double kAtmosLevelMin = 0.0;
inline constexpr double kAtmosLevelMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxLevel);               // :314
inline constexpr double kAtmosDensityMin =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMinDensity);             // :301
inline constexpr double kAtmosDensityMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxDensity);             // :302
inline constexpr double kAtmosGrainSecondsMin =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMinGrainSeconds);        // :299
inline constexpr double kAtmosGrainSecondsMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds);        // :300
inline constexpr double kAtmosDriftRangeMin = 0.0;
inline constexpr double kAtmosDriftRangeMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxDriftRangeSemitones); // :306
inline constexpr double kAtmosPositionMin = 0.0;
inline constexpr double kAtmosPositionMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxPositionSeconds);     // :303
inline constexpr double kAtmosPitchMax =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxPitchSemitones);      // :304
inline constexpr double kAtmosPitchMin = -kAtmosPitchMax;

inline constexpr double kAtmosLevelDefault           = 0.5;
inline constexpr double kAtmosBlurDefault            = 0.0;
inline constexpr double kAtmosDensityDefault         = 4.0;
inline constexpr double kAtmosGrainSecondsDefault    = 4.0;
inline constexpr double kAtmosDriftDepthDefault      = 0.3;
inline constexpr double kAtmosPanSpreadDefault       = 0.7;
inline constexpr double kAtmosDecorrelationDefault   = 0.5;
inline constexpr double kAtmosFreezeMixDefault       = 0.0;
inline constexpr double kAtmosDriftSmoothnessDefault = 0.7;
inline constexpr double kAtmosDriftRangeDefault      = 2.0;
inline constexpr double kAtmosJitterDefault          = 0.5;   ///< atmosphere_engine.h:2352
inline constexpr double kAtmosPositionDefault        = 1.0;   ///< :2353
inline constexpr double kAtmosPositionSpreadDefault  = 0.3;   ///< :2354
inline constexpr double kAtmosPitchDefault           = 0.0;   ///< :2355
inline constexpr double kAtmosPitchSpreadDefault     = 0.15;  ///< :2356
inline constexpr bool   kAtmosFreezeDefault          = false;
inline constexpr int    kAtmosGrainEnvelopeDefaultIndex = 0;  ///< Hann (:2292)

// ==============================================================================
// Parameter Storage - 15 float + 1 int + 1 bool atomics
// ==============================================================================

struct AtmosphereParams {
    std::atomic<float> level{static_cast<float>(kAtmosLevelDefault)};
    std::atomic<float> blur{static_cast<float>(kAtmosBlurDefault)};
    std::atomic<float> density{static_cast<float>(kAtmosDensityDefault)};
    std::atomic<float> grainSeconds{static_cast<float>(kAtmosGrainSecondsDefault)};
    std::atomic<float> driftDepth{static_cast<float>(kAtmosDriftDepthDefault)};
    std::atomic<float> panSpread{static_cast<float>(kAtmosPanSpreadDefault)};
    std::atomic<float> decorrelation{static_cast<float>(kAtmosDecorrelationDefault)};
    std::atomic<float> freezeMix{static_cast<float>(kAtmosFreezeMixDefault)};
    std::atomic<float> driftSmoothness{static_cast<float>(kAtmosDriftSmoothnessDefault)};
    std::atomic<float> driftRangeSemitones{static_cast<float>(kAtmosDriftRangeDefault)};
    std::atomic<float> jitter{static_cast<float>(kAtmosJitterDefault)};
    std::atomic<float> positionSeconds{static_cast<float>(kAtmosPositionDefault)};
    std::atomic<float> positionSpread{static_cast<float>(kAtmosPositionSpreadDefault)};
    std::atomic<float> pitchSemitones{static_cast<float>(kAtmosPitchDefault)};
    std::atomic<float> pitchSpread{static_cast<float>(kAtmosPitchSpreadDefault)};

    /// ID 1008. ENG-routed (SeraphisEngine::setAtmosphereFreeze).
    std::atomic<bool> freeze{kAtmosFreezeDefault};

    /// ID 1016. The list INDEX; GrainEnvelopeType is recovered through
    /// toGrainEnvelopeType() (dropdown_mappings.h:215).
    std::atomic<int> grainEnvelope{kAtmosGrainEnvelopeDefaultIndex};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

inline void handleAtmosphereParamChange(
    AtmosphereParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    const float unit = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kAtmosLevelId:
            params.level.store(
                std::clamp(static_cast<float>(kAtmosLevelMin +
                                              value * (kAtmosLevelMax - kAtmosLevelMin)),
                           static_cast<float>(kAtmosLevelMin),
                           static_cast<float>(kAtmosLevelMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosBlurId:
            params.blur.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosDensityId:
            params.density.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kAtmosDensityMin, kAtmosDensityMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosGrainSecondsId:
            params.grainSeconds.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kAtmosGrainSecondsMin, kAtmosGrainSecondsMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosDriftDepthId:
            params.driftDepth.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosPanSpreadId:
            params.panSpread.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosDecorrelationId:
            params.decorrelation.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosFreezeMixId:
            params.freezeMix.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosFreezeId:
            params.freeze.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kAtmosDriftSmoothnessId:
            params.driftSmoothness.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosDriftRangeId:
            params.driftRangeSemitones.store(
                std::clamp(static_cast<float>(kAtmosDriftRangeMin +
                                              value * (kAtmosDriftRangeMax -
                                                       kAtmosDriftRangeMin)),
                           static_cast<float>(kAtmosDriftRangeMin),
                           static_cast<float>(kAtmosDriftRangeMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosJitterId:
            params.jitter.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosPositionId:
            params.positionSeconds.store(
                std::clamp(static_cast<float>(kAtmosPositionMin +
                                              value * (kAtmosPositionMax - kAtmosPositionMin)),
                           static_cast<float>(kAtmosPositionMin),
                           static_cast<float>(kAtmosPositionMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosPositionSpreadId:
            params.positionSpread.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosPitchId:
            params.pitchSemitones.store(
                std::clamp(static_cast<float>(kAtmosPitchMin +
                                              value * (kAtmosPitchMax - kAtmosPitchMin)),
                           static_cast<float>(kAtmosPitchMin),
                           static_cast<float>(kAtmosPitchMax)),
                std::memory_order_relaxed);
            break;
        case kAtmosPitchSpreadId:
            params.pitchSpread.store(unit, std::memory_order_relaxed);
            break;
        case kAtmosGrainEnvelopeId:
            params.grainEnvelope.store(
                std::clamp(static_cast<int>(
                               value * static_cast<double>(kGrainEnvelopeLabels.size() - 1) +
                               0.5),
                           0, static_cast<int>(kGrainEnvelopeLabels.size()) - 1),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-016, FR-048)
// ==============================================================================

inline void registerAtmosphereParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Atmos Level"), STR16(""), 0,
                            (kAtmosLevelDefault - kAtmosLevelMin) /
                                (kAtmosLevelMax - kAtmosLevelMin),
                            ParameterInfo::kCanAutomate, kAtmosLevelId);
    parameters.addParameter(STR16("Atmos Blur"), STR16(""), 0,
                            kAtmosBlurDefault,
                            ParameterInfo::kCanAutomate, kAtmosBlurId);
    parameters.addParameter(STR16("Atmos Density"), STR16("gr/s"), 0,
                            Krate::Plugins::logMapToNormalized(kAtmosDensityDefault,
                                                               kAtmosDensityMin,
                                                               kAtmosDensityMax),
                            ParameterInfo::kCanAutomate, kAtmosDensityId);
    parameters.addParameter(STR16("Atmos Grain Size"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(kAtmosGrainSecondsDefault,
                                                               kAtmosGrainSecondsMin,
                                                               kAtmosGrainSecondsMax),
                            ParameterInfo::kCanAutomate, kAtmosGrainSecondsId);
    parameters.addParameter(STR16("Atmos Drift Depth"), STR16(""), 0,
                            kAtmosDriftDepthDefault,
                            ParameterInfo::kCanAutomate, kAtmosDriftDepthId);
    parameters.addParameter(STR16("Atmos Pan Spread"), STR16(""), 0,
                            kAtmosPanSpreadDefault,
                            ParameterInfo::kCanAutomate, kAtmosPanSpreadId);
    parameters.addParameter(STR16("Atmos Decorrelation"), STR16(""), 0,
                            kAtmosDecorrelationDefault,
                            ParameterInfo::kCanAutomate, kAtmosDecorrelationId);
    parameters.addParameter(STR16("Atmos Freeze Mix"), STR16(""), 0,
                            kAtmosFreezeMixDefault,
                            ParameterInfo::kCanAutomate, kAtmosFreezeMixId);
    parameters.addParameter(STR16("Atmos Freeze"), STR16(""), 1,
                            kAtmosFreezeDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kAtmosFreezeId);
    parameters.addParameter(STR16("Atmos Drift Smoothness"), STR16(""), 0,
                            kAtmosDriftSmoothnessDefault,
                            ParameterInfo::kCanAutomate, kAtmosDriftSmoothnessId);
    parameters.addParameter(STR16("Atmos Drift Range"), STR16("st"), 0,
                            (kAtmosDriftRangeDefault - kAtmosDriftRangeMin) /
                                (kAtmosDriftRangeMax - kAtmosDriftRangeMin),
                            ParameterInfo::kCanAutomate, kAtmosDriftRangeId);
    parameters.addParameter(STR16("Atmos Jitter"), STR16(""), 0,
                            kAtmosJitterDefault,
                            ParameterInfo::kCanAutomate, kAtmosJitterId);
    parameters.addParameter(STR16("Atmos Position"), STR16("s"), 0,
                            (kAtmosPositionDefault - kAtmosPositionMin) /
                                (kAtmosPositionMax - kAtmosPositionMin),
                            ParameterInfo::kCanAutomate, kAtmosPositionId);
    parameters.addParameter(STR16("Atmos Position Spread"), STR16(""), 0,
                            kAtmosPositionSpreadDefault,
                            ParameterInfo::kCanAutomate, kAtmosPositionSpreadId);
    parameters.addParameter(STR16("Atmos Pitch"), STR16("st"), 0,
                            (kAtmosPitchDefault - kAtmosPitchMin) /
                                (kAtmosPitchMax - kAtmosPitchMin),
                            ParameterInfo::kCanAutomate, kAtmosPitchId);
    parameters.addParameter(STR16("Atmos Pitch Spread"), STR16(""), 0,
                            kAtmosPitchSpreadDefault,
                            ParameterInfo::kCanAutomate, kAtmosPitchSpreadId);

    // ID 1016 (Hann) through Seraphis::addDropdownParam (dropdown_mappings.h) -
    // the one path that pins info.defaultNormalizedValue, used even though this
    // default index is 0, so changing it later cannot silently un-pin it.
    addDropdownParam(parameters, STR16("Atmos Grain Envelope"), kAtmosGrainEnvelopeId,
                     kAtmosGrainEnvelopeDefaultIndex, kGrainEnvelopeLabels.data(),
                     static_cast<int>(kGrainEnvelopeLabels.size()));
}

// ==============================================================================
// Display Formatting (FR-061: ID 1016 formats itself and is NOT claimed here)
// ==============================================================================

inline Steinberg::tresult formatAtmosphereParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    // Zero-initialized deliberately: the buffer is written in every case that
    // falls through to fromAscii(), but /W4's C4701 and GCC's
    // -Wmaybe-uninitialized both reason per-path, and the standing constraint
    // is zero warnings.
    char8 text[32] = {};

    switch (id) {
        case kAtmosBlurId:
        case kAtmosDriftDepthId:
        case kAtmosPanSpreadId:
        case kAtmosDecorrelationId:
        case kAtmosFreezeMixId:
        case kAtmosDriftSmoothnessId:
        case kAtmosJitterId:
        case kAtmosPositionSpreadId:
        case kAtmosPitchSpreadId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kAtmosLevelId:
            snprintf(text, sizeof(text), "%.2f",
                     kAtmosLevelMin + value * (kAtmosLevelMax - kAtmosLevelMin));
            break;
        case kAtmosDensityId:
            snprintf(text, sizeof(text), "%.2f gr/s",
                     Krate::Plugins::logMapFromNormalized(value, kAtmosDensityMin,
                                                          kAtmosDensityMax));
            break;
        case kAtmosGrainSecondsId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(value, kAtmosGrainSecondsMin,
                                                          kAtmosGrainSecondsMax));
            break;
        case kAtmosFreezeId:
            snprintf(text, sizeof(text), "%s", (value >= 0.5) ? "On" : "Off");
            break;
        case kAtmosDriftRangeId:
            snprintf(text, sizeof(text), "%.1f st",
                     kAtmosDriftRangeMin +
                         value * (kAtmosDriftRangeMax - kAtmosDriftRangeMin));
            break;
        case kAtmosPositionId:
            snprintf(text, sizeof(text), "%.2f s",
                     kAtmosPositionMin + value * (kAtmosPositionMax - kAtmosPositionMin));
            break;
        case kAtmosPitchId:
            snprintf(text, sizeof(text), "%.1f st",
                     kAtmosPitchMin + value * (kAtmosPitchMax - kAtmosPitchMin));
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 68 bytes, plan 5.1's [atmos] block order
// ==============================================================================
// 15 floats (1000-1007, 1009-1015) | 2 int32 (1008 freeze, 1016 grainEnvelope).

inline void saveAtmosphereParams(const AtmosphereParams& params,
                                 Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.blur.load(std::memory_order_relaxed));
    streamer.writeFloat(params.density.load(std::memory_order_relaxed));
    streamer.writeFloat(params.grainSeconds.load(std::memory_order_relaxed));
    streamer.writeFloat(params.driftDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.panSpread.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decorrelation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.freezeMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.driftSmoothness.load(std::memory_order_relaxed));
    streamer.writeFloat(params.driftRangeSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.jitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.positionSeconds.load(std::memory_order_relaxed));
    streamer.writeFloat(params.positionSpread.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchSpread.load(std::memory_order_relaxed));

    streamer.writeInt32(params.freeze.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.grainEnvelope.load(std::memory_order_relaxed)));
}

/// EOF-safe (FR-093).
inline bool loadAtmosphereParams(AtmosphereParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.level.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.blur.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.density.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.grainSeconds.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.driftDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.panSpread.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.decorrelation.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.freezeMix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.driftSmoothness.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.driftRangeSemitones.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.jitter.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.positionSeconds.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.positionSpread.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.pitchSemitones.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.pitchSpread.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.freeze.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.grainEnvelope.store(
        std::clamp(static_cast<int>(iv), 0,
                   static_cast<int>(kGrainEnvelopeLabels.size()) - 1),
        std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadAtmosphereParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (streamer.readFloat(fv)) {
        setParam(kAtmosLevelId,
                 std::clamp((static_cast<double>(fv) - kAtmosLevelMin) /
                                (kAtmosLevelMax - kAtmosLevelMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosBlurId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosDensityId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kAtmosDensityMin, kAtmosDensityMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosGrainSecondsId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kAtmosGrainSecondsMin,
                                                    kAtmosGrainSecondsMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosDriftDepthId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosPanSpreadId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosDecorrelationId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosFreezeMixId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosDriftSmoothnessId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosDriftRangeId,
                 std::clamp((static_cast<double>(fv) - kAtmosDriftRangeMin) /
                                (kAtmosDriftRangeMax - kAtmosDriftRangeMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosJitterId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosPositionId,
                 std::clamp((static_cast<double>(fv) - kAtmosPositionMin) /
                                (kAtmosPositionMax - kAtmosPositionMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosPositionSpreadId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosPitchId,
                 std::clamp((static_cast<double>(fv) - kAtmosPitchMin) /
                                (kAtmosPitchMax - kAtmosPitchMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kAtmosPitchSpreadId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }

    if (streamer.readInt32(iv)) {
        setParam(kAtmosFreezeId, iv != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(iv)) {
        setParam(kAtmosGrainEnvelopeId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kGrainEnvelopeLabels.size()) - 1)) /
                     static_cast<double>(kGrainEnvelopeLabels.size() - 1));
    }
}

}  // namespace Seraphis
