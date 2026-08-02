#pragma once

// ==============================================================================
// Seraphis - Harmonic Cloud Parameters (ID 200-210)   FR-014, FR-016 - FR-019
// ==============================================================================
// The six-function contract every Seraphis pack implements (global_params.h:85,
// :124, :160, :202, :209, :232), applied to spec C-6's eleven cloud rows.
//
// TWO CONSTRUCTION RULES BIND EVERY BOUND AND EVERY DEFAULT HERE (plan 2.3.1):
//   1. Each mn/mx is `static_cast<double>(<the DSP constant>)`, never a re-typed
//      literal - a re-typed literal is how a default drifts by one ULP.
//   2. Each registered defaultNormalizedValue is computed FROM the plain default
//      THROUGH the same mapping, never hand-typed. That is what makes SC-022's
//      exact float `==` achievable.
// FR-018: every handler clamps into the C-6 plain range BEFORE storing, so
// FR-042's change detector cannot latch on an out-of-range value.
// FR-019: no sample rate is read here; times stay in seconds.
// ==============================================================================

#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // logMapFromNormalized / logMapToNormalized

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/harmonic_cloud.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults (the SINGLE transcription)
// ==============================================================================
// Unit ranges ([0,1]) and the bipolar gravity range carry no named DSP constant -
// HarmonicCloud clamps them against literals at the setter (harmonic_cloud.h:412,
// :452, :478, :513, :535, :580) - so they are written as literals here and
// nowhere else.

inline constexpr double kCloudInharmonicityMin = 0.0;
inline constexpr double kCloudInharmonicityMax =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMaxInharmonicity);  // :191
inline constexpr double kCloudTiltMin =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMinTiltDbPerOct);   // :194
inline constexpr double kCloudTiltMax =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMaxTiltDbPerOct);   // :195
inline constexpr double kCloudGravityMin = -1.0;
inline constexpr double kCloudGravityMax = 1.0;
inline constexpr double kCloudDriftDepthMin = 0.0;
inline constexpr double kCloudDriftDepthMax =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMaxDriftCents);     // :214
inline constexpr double kCloudAttackMin =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMinAttackSec);      // :218
inline constexpr double kCloudAttackMax =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMaxAttackSec);      // :219
inline constexpr double kCloudDecayMin =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMinDecaySec);       // :220
inline constexpr double kCloudDecayMax =
    static_cast<double>(Krate::DSP::HarmonicCloud::kMaxDecaySec);       // :221

inline constexpr double kCloudRichnessDefault        = 0.60;
inline constexpr double kCloudInharmonicityDefault   = 0.030;
inline constexpr double kCloudTiltDefault            = 0.0;
inline constexpr double kCloudMutationDefault        = 0.25;
inline constexpr double kCloudGravityDefault         = 0.20;
inline constexpr double kCloudDriftDepthDefault      = 0.0;
inline constexpr double kCloudDriftSmoothnessDefault = 0.5;
inline constexpr double kCloudStereoSpreadDefault    = 0.35;
inline constexpr double kCloudAttackDefault          = 0.05;
inline constexpr double kCloudDecayDefault           = 0.5;
inline constexpr double kCloudEnvOffsetSpreadDefault = 0.0;

// ==============================================================================
// Parameter Storage - 11 float atomics
// ==============================================================================

struct CloudParams {
    std::atomic<float> richness{static_cast<float>(kCloudRichnessDefault)};
    std::atomic<float> inharmonicity{static_cast<float>(kCloudInharmonicityDefault)};
    std::atomic<float> tiltDbPerOct{static_cast<float>(kCloudTiltDefault)};
    std::atomic<float> mutation{static_cast<float>(kCloudMutationDefault)};
    std::atomic<float> gravity{static_cast<float>(kCloudGravityDefault)};
    std::atomic<float> driftDepthCents{static_cast<float>(kCloudDriftDepthDefault)};
    std::atomic<float> driftSmoothness{static_cast<float>(kCloudDriftSmoothnessDefault)};
    std::atomic<float> stereoSpread{static_cast<float>(kCloudStereoSpreadDefault)};
    std::atomic<float> attackSec{static_cast<float>(kCloudAttackDefault)};
    std::atomic<float> decaySec{static_cast<float>(kCloudDecayDefault)};
    std::atomic<float> envOffsetSpread{static_cast<float>(kCloudEnvOffsetSpreadDefault)};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

inline void handleCloudParamChange(
    CloudParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    // The `lin [0,1]` form, shared by the five unit-range rows.
    const float unit = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kCloudRichnessId:
            params.richness.store(unit, std::memory_order_relaxed);
            break;
        case kCloudInharmonicityId:
            params.inharmonicity.store(
                std::clamp(static_cast<float>(kCloudInharmonicityMin +
                                              value * (kCloudInharmonicityMax -
                                                       kCloudInharmonicityMin)),
                           static_cast<float>(kCloudInharmonicityMin),
                           static_cast<float>(kCloudInharmonicityMax)),
                std::memory_order_relaxed);
            break;
        case kCloudTiltId:
            params.tiltDbPerOct.store(
                std::clamp(static_cast<float>(kCloudTiltMin +
                                              value * (kCloudTiltMax - kCloudTiltMin)),
                           static_cast<float>(kCloudTiltMin),
                           static_cast<float>(kCloudTiltMax)),
                std::memory_order_relaxed);
            break;
        case kCloudMutationId:
            params.mutation.store(unit, std::memory_order_relaxed);
            break;
        case kCloudGravityId:
            params.gravity.store(
                std::clamp(static_cast<float>(kCloudGravityMin +
                                              value * (kCloudGravityMax - kCloudGravityMin)),
                           static_cast<float>(kCloudGravityMin),
                           static_cast<float>(kCloudGravityMax)),
                std::memory_order_relaxed);
            break;
        case kCloudDriftDepthId:
            params.driftDepthCents.store(
                std::clamp(static_cast<float>(kCloudDriftDepthMin +
                                              value * (kCloudDriftDepthMax -
                                                       kCloudDriftDepthMin)),
                           static_cast<float>(kCloudDriftDepthMin),
                           static_cast<float>(kCloudDriftDepthMax)),
                std::memory_order_relaxed);
            break;
        case kCloudDriftSmoothnessId:
            params.driftSmoothness.store(unit, std::memory_order_relaxed);
            break;
        case kCloudStereoSpreadId:
            params.stereoSpread.store(unit, std::memory_order_relaxed);
            break;
        case kCloudAttackId:
            // logMapFromNormalized already clamps to [mn, mx] (parameter_helpers.h:80).
            params.attackSec.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kCloudAttackMin, kCloudAttackMax)),
                std::memory_order_relaxed);
            break;
        case kCloudDecayId:
            params.decaySec.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kCloudDecayMin, kCloudDecayMax)),
                std::memory_order_relaxed);
            break;
        case kCloudEnvOffsetSpreadId:
            params.envOffsetSpread.store(unit, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-048 - eleven plain Vst::Parameters, FROZEN)
// ==============================================================================

inline void registerCloudParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Cloud Richness"), STR16(""), 0,
                            kCloudRichnessDefault,
                            ParameterInfo::kCanAutomate, kCloudRichnessId);
    parameters.addParameter(STR16("Cloud Inharmonicity"), STR16(""), 0,
                            (kCloudInharmonicityDefault - kCloudInharmonicityMin) /
                                (kCloudInharmonicityMax - kCloudInharmonicityMin),
                            ParameterInfo::kCanAutomate, kCloudInharmonicityId);
    parameters.addParameter(STR16("Cloud Tilt"), STR16("dB/oct"), 0,
                            (kCloudTiltDefault - kCloudTiltMin) /
                                (kCloudTiltMax - kCloudTiltMin),
                            ParameterInfo::kCanAutomate, kCloudTiltId);
    parameters.addParameter(STR16("Cloud Mutation"), STR16(""), 0,
                            kCloudMutationDefault,
                            ParameterInfo::kCanAutomate, kCloudMutationId);
    parameters.addParameter(STR16("Cloud Gravity"), STR16(""), 0,
                            (kCloudGravityDefault - kCloudGravityMin) /
                                (kCloudGravityMax - kCloudGravityMin),
                            ParameterInfo::kCanAutomate, kCloudGravityId);
    parameters.addParameter(STR16("Cloud Drift Depth"), STR16("cents"), 0,
                            (kCloudDriftDepthDefault - kCloudDriftDepthMin) /
                                (kCloudDriftDepthMax - kCloudDriftDepthMin),
                            ParameterInfo::kCanAutomate, kCloudDriftDepthId);
    parameters.addParameter(STR16("Cloud Drift Smoothness"), STR16(""), 0,
                            kCloudDriftSmoothnessDefault,
                            ParameterInfo::kCanAutomate, kCloudDriftSmoothnessId);
    parameters.addParameter(STR16("Cloud Stereo Spread"), STR16(""), 0,
                            kCloudStereoSpreadDefault,
                            ParameterInfo::kCanAutomate, kCloudStereoSpreadId);
    parameters.addParameter(STR16("Cloud Attack"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(
                                kCloudAttackDefault, kCloudAttackMin, kCloudAttackMax),
                            ParameterInfo::kCanAutomate, kCloudAttackId);
    parameters.addParameter(STR16("Cloud Decay"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(
                                kCloudDecayDefault, kCloudDecayMin, kCloudDecayMax),
                            ParameterInfo::kCanAutomate, kCloudDecayId);
    parameters.addParameter(STR16("Cloud Env Offset Spread"), STR16(""), 0,
                            kCloudEnvOffsetSpreadDefault,
                            ParameterInfo::kCanAutomate, kCloudEnvOffsetSpreadId);
}

// ==============================================================================
// Display Formatting (FR-061 - this pack owns no dropdown, so it claims none)
// ==============================================================================

inline Steinberg::tresult formatCloudParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        case kCloudRichnessId:
        case kCloudMutationId:
        case kCloudDriftSmoothnessId:
        case kCloudStereoSpreadId:
        case kCloudEnvOffsetSpreadId:
            snprintf(text, sizeof(text), "%.0f%%",
                     std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kCloudInharmonicityId:
            snprintf(text, sizeof(text), "%.3f",
                     kCloudInharmonicityMin +
                         value * (kCloudInharmonicityMax - kCloudInharmonicityMin));
            break;
        case kCloudTiltId:
            snprintf(text, sizeof(text), "%.1f dB/oct",
                     kCloudTiltMin + value * (kCloudTiltMax - kCloudTiltMin));
            break;
        case kCloudGravityId:
            snprintf(text, sizeof(text), "%.2f",
                     kCloudGravityMin + value * (kCloudGravityMax - kCloudGravityMin));
            break;
        case kCloudDriftDepthId:
            snprintf(text, sizeof(text), "%.1f ct",
                     kCloudDriftDepthMin +
                         value * (kCloudDriftDepthMax - kCloudDriftDepthMin));
            break;
        case kCloudAttackId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(
                         value, kCloudAttackMin, kCloudAttackMax));
            break;
        case kCloudDecayId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(
                         value, kCloudDecayMin, kCloudDecayMax));
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 44 bytes (11 floats, plan 5.1's [cloud] block order)
// ==============================================================================

inline void saveCloudParams(const CloudParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.richness.load(std::memory_order_relaxed));
    streamer.writeFloat(params.inharmonicity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tiltDbPerOct.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mutation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gravity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.driftDepthCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.driftSmoothness.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoSpread.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackSec.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decaySec.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envOffsetSpread.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read leaves this and every later atomic at its registered
/// default and returns false, which is not an error (FR-093).
inline bool loadCloudParams(CloudParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;

    if (!streamer.readFloat(fv)) { return false; }
    params.richness.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.inharmonicity.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.tiltDbPerOct.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.mutation.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.gravity.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.driftDepthCents.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.driftSmoothness.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.stereoSpread.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.attackSec.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.decaySec.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.envOffsetSpread.store(fv, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadCloudParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;

    if (streamer.readFloat(fv)) {
        setParam(kCloudRichnessId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudInharmonicityId,
                 std::clamp((static_cast<double>(fv) - kCloudInharmonicityMin) /
                                (kCloudInharmonicityMax - kCloudInharmonicityMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudTiltId,
                 std::clamp((static_cast<double>(fv) - kCloudTiltMin) /
                                (kCloudTiltMax - kCloudTiltMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudMutationId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudGravityId,
                 std::clamp((static_cast<double>(fv) - kCloudGravityMin) /
                                (kCloudGravityMax - kCloudGravityMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudDriftDepthId,
                 std::clamp((static_cast<double>(fv) - kCloudDriftDepthMin) /
                                (kCloudDriftDepthMax - kCloudDriftDepthMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudDriftSmoothnessId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudStereoSpreadId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudAttackId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kCloudAttackMin, kCloudAttackMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudDecayId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kCloudDecayMin, kCloudDecayMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kCloudEnvOffsetSpreadId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
}

}  // namespace Seraphis
