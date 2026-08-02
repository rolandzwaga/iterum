#pragma once

// ==============================================================================
// Seraphis - Aether Space Parameters (ID 1200-1217)
// ==============================================================================
// The six-function contract, as global_params.h implements it (line numbers as
// of 2026-08-01; the NAMES are the stable citation, the numbers move whenever
// that header grows): handleGlobalParamChange:85, registerGlobalParams:124,
// formatGlobalParam:169, saveGlobalParams:211, loadGlobalParams:218,
// loadGlobalParamsToController:240.
//
// FIFTEEN of the eighteen rows are `lin [0,1]`; the exceptions are 1203 (log,
// seconds), 1207 (lin, milliseconds) and 1204 (a toggle).
//
// PLAN 2.3.1 RULE 1 CANNOT BE SATISFIED FOR 1203 AND 1207, AND THAT IS A
// PROPERTY OF THE DSP HEADER, NOT A SHORTCUT. AetherReverb::kDecayMinSeconds,
// kDecayMaxSeconds and kMaxPreDelayMs are declared under `private:`
// (aether_reverb.h:2724 opens the section; the three sit at :2735, :2736, :2743),
// so no plugin translation unit can name them. The two ranges are therefore
// transcribed here ONCE, as named constants, with the DSP line cited beside each
// - never re-typed at a use site. If the reverb ever widens a range, the
// static_assert-free duplication below is what a reviewer greps for; making the
// three constants public would remove it.
// ==============================================================================

#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults
// ==============================================================================

inline constexpr double kAetherDecayMinSeconds = 0.5;   ///< == aether_reverb.h:2735
inline constexpr double kAetherDecayMaxSeconds = 60.0;  ///< == :2736
inline constexpr double kAetherPreDelayMinMs   = 0.0;   ///< setPreDelayMs' own floor (:2248)
inline constexpr double kAetherPreDelayMaxMs   = 200.0; ///< == kMaxPreDelayMs (:2743)

inline constexpr double kAetherMixDefault               = 0.35;
inline constexpr double kAetherSizeDefault              = 0.50;
inline constexpr double kAetherDensityDefault           = 0.70;
inline constexpr double kAetherDecayDefault             = 4.0;
inline constexpr double kAetherDimensionalityDefault    = 0.35;
inline constexpr double kAetherDampingDefault           = 0.40;
inline constexpr double kAetherPreDelayDefault          = 0.0;
inline constexpr double kAetherModDepthDefault          = 0.25;
inline constexpr double kAetherModSmoothnessDefault     = 0.60;
inline constexpr double kAetherShimmerOctaveDefault     = 0.0;
inline constexpr double kAetherShimmerFifthDefault      = 0.0;
inline constexpr double kAetherBloomSendDefault         = 0.0;
inline constexpr double kAetherBloomDecayDefault        = 0.50;
inline constexpr double kAetherSpectralDiffusionDefault = 0.0;
inline constexpr double kAetherSizeBreathDepthDefault   = 0.20;
inline constexpr double kAetherTideDepthDefault         = 0.20;
inline constexpr double kAetherWidthDefault             = 1.0;
inline constexpr bool   kAetherFreezeDefault            = false;

// ==============================================================================
// Parameter Storage - 17 float + 1 bool atomics
// ==============================================================================

struct AetherParams {
    std::atomic<float> mix{static_cast<float>(kAetherMixDefault)};
    std::atomic<float> size{static_cast<float>(kAetherSizeDefault)};
    std::atomic<float> density{static_cast<float>(kAetherDensityDefault)};
    std::atomic<float> decaySeconds{static_cast<float>(kAetherDecayDefault)};
    std::atomic<float> dimensionality{static_cast<float>(kAetherDimensionalityDefault)};
    std::atomic<float> damping{static_cast<float>(kAetherDampingDefault)};
    std::atomic<float> preDelayMs{static_cast<float>(kAetherPreDelayDefault)};
    std::atomic<float> modDepth{static_cast<float>(kAetherModDepthDefault)};
    std::atomic<float> modSmoothness{static_cast<float>(kAetherModSmoothnessDefault)};
    std::atomic<float> shimmerOctave{static_cast<float>(kAetherShimmerOctaveDefault)};
    std::atomic<float> shimmerFifth{static_cast<float>(kAetherShimmerFifthDefault)};
    std::atomic<float> bloomSend{static_cast<float>(kAetherBloomSendDefault)};
    std::atomic<float> bloomDecay{static_cast<float>(kAetherBloomDecayDefault)};
    std::atomic<float> spectralDiffusion{static_cast<float>(kAetherSpectralDiffusionDefault)};
    std::atomic<float> sizeBreathDepth{static_cast<float>(kAetherSizeBreathDepthDefault)};
    std::atomic<float> tideDepth{static_cast<float>(kAetherTideDepthDefault)};
    std::atomic<float> width{static_cast<float>(kAetherWidthDefault)};

    /// ID 1204. AE-routed; AetherReverb::setFreeze is a self-guarding latch.
    std::atomic<bool> freeze{kAetherFreezeDefault};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

inline void handleAetherParamChange(
    AetherParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    const float unit = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kAetherMixId:            params.mix.store(unit, std::memory_order_relaxed); break;
        case kAetherSizeId:           params.size.store(unit, std::memory_order_relaxed); break;
        case kAetherDensityId:        params.density.store(unit, std::memory_order_relaxed); break;
        case kAetherDecayId:
            params.decaySeconds.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kAetherDecayMinSeconds, kAetherDecayMaxSeconds)),
                std::memory_order_relaxed);
            break;
        case kAetherFreezeId:
            params.freeze.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kAetherDimensionalityId:
            params.dimensionality.store(unit, std::memory_order_relaxed);
            break;
        case kAetherDampingId:        params.damping.store(unit, std::memory_order_relaxed); break;
        case kAetherPreDelayId:
            params.preDelayMs.store(
                std::clamp(static_cast<float>(kAetherPreDelayMinMs +
                                              value * (kAetherPreDelayMaxMs -
                                                       kAetherPreDelayMinMs)),
                           static_cast<float>(kAetherPreDelayMinMs),
                           static_cast<float>(kAetherPreDelayMaxMs)),
                std::memory_order_relaxed);
            break;
        case kAetherModDepthId:       params.modDepth.store(unit, std::memory_order_relaxed); break;
        case kAetherModSmoothnessId:
            params.modSmoothness.store(unit, std::memory_order_relaxed);
            break;
        case kAetherShimmerOctaveId:
            params.shimmerOctave.store(unit, std::memory_order_relaxed);
            break;
        case kAetherShimmerFifthId:
            params.shimmerFifth.store(unit, std::memory_order_relaxed);
            break;
        case kAetherBloomSendId:      params.bloomSend.store(unit, std::memory_order_relaxed); break;
        case kAetherBloomDecayId:     params.bloomDecay.store(unit, std::memory_order_relaxed); break;
        case kAetherSpectralDiffusionId:
            params.spectralDiffusion.store(unit, std::memory_order_relaxed);
            break;
        case kAetherSizeBreathDepthId:
            params.sizeBreathDepth.store(unit, std::memory_order_relaxed);
            break;
        case kAetherTideDepthId:      params.tideDepth.store(unit, std::memory_order_relaxed); break;
        case kAetherWidthId:          params.width.store(unit, std::memory_order_relaxed); break;
        default:                      break;
    }
}

// ==============================================================================
// Parameter Registration (FR-048 - seventeen plain Vst::Parameters + one toggle)
// ==============================================================================

inline void registerAetherParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Aether Mix"), STR16(""), 0, kAetherMixDefault,
                            ParameterInfo::kCanAutomate, kAetherMixId);
    parameters.addParameter(STR16("Aether Size"), STR16(""), 0, kAetherSizeDefault,
                            ParameterInfo::kCanAutomate, kAetherSizeId);
    parameters.addParameter(STR16("Aether Density"), STR16(""), 0, kAetherDensityDefault,
                            ParameterInfo::kCanAutomate, kAetherDensityId);
    parameters.addParameter(STR16("Aether Decay"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(kAetherDecayDefault,
                                                               kAetherDecayMinSeconds,
                                                               kAetherDecayMaxSeconds),
                            ParameterInfo::kCanAutomate, kAetherDecayId);
    parameters.addParameter(STR16("Aether Freeze"), STR16(""), 1,
                            kAetherFreezeDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kAetherFreezeId);
    parameters.addParameter(STR16("Aether Dimensionality"), STR16(""), 0,
                            kAetherDimensionalityDefault,
                            ParameterInfo::kCanAutomate, kAetherDimensionalityId);
    parameters.addParameter(STR16("Aether Damping"), STR16(""), 0, kAetherDampingDefault,
                            ParameterInfo::kCanAutomate, kAetherDampingId);
    parameters.addParameter(STR16("Aether Pre-Delay"), STR16("ms"), 0,
                            (kAetherPreDelayDefault - kAetherPreDelayMinMs) /
                                (kAetherPreDelayMaxMs - kAetherPreDelayMinMs),
                            ParameterInfo::kCanAutomate, kAetherPreDelayId);
    parameters.addParameter(STR16("Aether Mod Depth"), STR16(""), 0, kAetherModDepthDefault,
                            ParameterInfo::kCanAutomate, kAetherModDepthId);
    parameters.addParameter(STR16("Aether Mod Smoothness"), STR16(""), 0,
                            kAetherModSmoothnessDefault,
                            ParameterInfo::kCanAutomate, kAetherModSmoothnessId);
    parameters.addParameter(STR16("Aether Shimmer Octave"), STR16(""), 0,
                            kAetherShimmerOctaveDefault,
                            ParameterInfo::kCanAutomate, kAetherShimmerOctaveId);
    parameters.addParameter(STR16("Aether Shimmer Fifth"), STR16(""), 0,
                            kAetherShimmerFifthDefault,
                            ParameterInfo::kCanAutomate, kAetherShimmerFifthId);
    parameters.addParameter(STR16("Aether Bloom Send"), STR16(""), 0,
                            kAetherBloomSendDefault,
                            ParameterInfo::kCanAutomate, kAetherBloomSendId);
    parameters.addParameter(STR16("Aether Bloom Decay"), STR16(""), 0,
                            kAetherBloomDecayDefault,
                            ParameterInfo::kCanAutomate, kAetherBloomDecayId);
    parameters.addParameter(STR16("Aether Spectral Diffusion"), STR16(""), 0,
                            kAetherSpectralDiffusionDefault,
                            ParameterInfo::kCanAutomate, kAetherSpectralDiffusionId);
    parameters.addParameter(STR16("Aether Size Breath"), STR16(""), 0,
                            kAetherSizeBreathDepthDefault,
                            ParameterInfo::kCanAutomate, kAetherSizeBreathDepthId);
    parameters.addParameter(STR16("Aether Tide Depth"), STR16(""), 0,
                            kAetherTideDepthDefault,
                            ParameterInfo::kCanAutomate, kAetherTideDepthId);
    parameters.addParameter(STR16("Aether Width"), STR16(""), 0, kAetherWidthDefault,
                            ParameterInfo::kCanAutomate, kAetherWidthId);
}

// ==============================================================================
// Display Formatting (this pack owns no dropdown, so it claims none)
// ==============================================================================

inline Steinberg::tresult formatAetherParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        case kAetherMixId:
        case kAetherSizeId:
        case kAetherDensityId:
        case kAetherDimensionalityId:
        case kAetherDampingId:
        case kAetherModDepthId:
        case kAetherModSmoothnessId:
        case kAetherShimmerOctaveId:
        case kAetherShimmerFifthId:
        case kAetherBloomSendId:
        case kAetherBloomDecayId:
        case kAetherSpectralDiffusionId:
        case kAetherSizeBreathDepthId:
        case kAetherTideDepthId:
        case kAetherWidthId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kAetherDecayId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(value, kAetherDecayMinSeconds,
                                                          kAetherDecayMaxSeconds));
            break;
        case kAetherFreezeId:
            snprintf(text, sizeof(text), "%s", (value >= 0.5) ? "On" : "Off");
            break;
        case kAetherPreDelayId:
            snprintf(text, sizeof(text), "%.1f ms",
                     kAetherPreDelayMinMs +
                         value * (kAetherPreDelayMaxMs - kAetherPreDelayMinMs));
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 72 bytes, plan 5.1's [aether] block order
// ==============================================================================
// 17 floats (1200-1203, 1205-1217) | 1 int32 (1204 freeze).

inline void saveAetherParams(const AetherParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.size.load(std::memory_order_relaxed));
    streamer.writeFloat(params.density.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decaySeconds.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dimensionality.load(std::memory_order_relaxed));
    streamer.writeFloat(params.damping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.preDelayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modSmoothness.load(std::memory_order_relaxed));
    streamer.writeFloat(params.shimmerOctave.load(std::memory_order_relaxed));
    streamer.writeFloat(params.shimmerFifth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bloomSend.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bloomDecay.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralDiffusion.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sizeBreathDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tideDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));

    streamer.writeInt32(params.freeze.load(std::memory_order_relaxed) ? 1 : 0);
}

/// EOF-safe (FR-093).
inline bool loadAetherParams(AetherParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.size.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.density.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.decaySeconds.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.dimensionality.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.damping.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.preDelayMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.modDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.modSmoothness.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.shimmerOctave.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.shimmerFifth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.bloomSend.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.bloomDecay.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.spectralDiffusion.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.sizeBreathDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.tideDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.width.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.freeze.store(iv != 0, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadAetherParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    const auto unitParam = [&setParam](Steinberg::Vst::ParamID id, float plain) {
        setParam(id, std::clamp(static_cast<double>(plain), 0.0, 1.0));
    };

    if (streamer.readFloat(fv)) { unitParam(kAetherMixId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherSizeId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherDensityId, fv); }
    if (streamer.readFloat(fv)) {
        setParam(kAetherDecayId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kAetherDecayMinSeconds,
                                                    kAetherDecayMaxSeconds));
    }
    if (streamer.readFloat(fv)) { unitParam(kAetherDimensionalityId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherDampingId, fv); }
    if (streamer.readFloat(fv)) {
        setParam(kAetherPreDelayId,
                 std::clamp((static_cast<double>(fv) - kAetherPreDelayMinMs) /
                                (kAetherPreDelayMaxMs - kAetherPreDelayMinMs),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) { unitParam(kAetherModDepthId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherModSmoothnessId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherShimmerOctaveId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherShimmerFifthId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherBloomSendId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherBloomDecayId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherSpectralDiffusionId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherSizeBreathDepthId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherTideDepthId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kAetherWidthId, fv); }

    if (streamer.readInt32(iv)) {
        setParam(kAetherFreezeId, iv != 0 ? 1.0 : 0.0);
    }
}

}  // namespace Seraphis
