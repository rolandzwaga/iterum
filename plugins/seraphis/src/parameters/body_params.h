#pragma once

// ==============================================================================
// Seraphis - Continuous Body Parameters (ID 800-812)
// ==============================================================================
// The six-function contract (global_params.h:85, :124, :169, :211, :218, :241).
// Every bound below is a NAMED ContinuousBody constant, never a re-typed literal
// (plan 2.3.1 rule 1) - the body publishes a kMin*/kMax*/kDefault* triple for all
// ten continuous controls of this band (continuous_body.h:122-160), so there is
// nothing to re-type here.
//
// The DEFAULTS are C-6's, not the component's: ID 802's C-6 default is 0.25 while
// ContinuousBody::kDefaultDamping is 0.0 (continuous_body.h:128), because 802 is
// MB-routed and its origin is the macro row, not the body's member initializer.
// So the plain defaults below are written as literals and only the BOUNDS are
// named constants.
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kBodyMaterialLabels (ID 800)
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/continuous_body.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults
// ==============================================================================

inline constexpr double kBodyResonanceMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinResonance);      // :122
inline constexpr double kBodyResonanceMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxResonance);      // :123
inline constexpr double kBodyDampingMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinDamping);        // :126
inline constexpr double kBodyDampingMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxDamping);        // :127
inline constexpr double kBodyKeyTrackingMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinKeyTracking);    // :130
inline constexpr double kBodyKeyTrackingMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxKeyTracking);    // :131
inline constexpr double kBodyDriveMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinUserDrive);      // :134
inline constexpr double kBodyDriveMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxUserDrive);      // :135
inline constexpr double kBodyMixMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinMix);            // :138
inline constexpr double kBodyMixMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxMix);            // :139
inline constexpr double kBodyCloudMixMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinCloudMix);       // :142
inline constexpr double kBodyCloudMixMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxCloudMix);       // :143
inline constexpr double kBodyCloudDecayMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinCloudDecaySec);  // :146
inline constexpr double kBodyCloudDecayMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxCloudDecaySec);  // :147
inline constexpr double kBodyCloudSizeMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinCloudSize);      // :150
inline constexpr double kBodyCloudSizeMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxCloudSize);      // :151
inline constexpr double kBodyCloudDampingMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinCloudDamping);   // :154
inline constexpr double kBodyCloudDampingMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxCloudDamping);   // :155
inline constexpr double kBodyWidthMin =
    static_cast<double>(Krate::DSP::ContinuousBody::kMinWidth);          // :158
inline constexpr double kBodyWidthMax =
    static_cast<double>(Krate::DSP::ContinuousBody::kMaxWidth);          // :159

inline constexpr double kBodyResonanceDefault    = 0.7;
inline constexpr double kBodyDampingDefault      = 0.25;
inline constexpr double kBodyKeyTrackingDefault  = 1.0;
inline constexpr double kBodyDriveDefault        = 1.0;
inline constexpr double kBodyMixDefault          = 1.0;
inline constexpr double kBodyCloudMixDefault     = 0.25;
inline constexpr double kBodyCloudDecayDefault   = 4.0;
inline constexpr double kBodyCloudSizeDefault    = 1.0;
inline constexpr double kBodyCloudDampingDefault = 0.3;
inline constexpr double kBodyWidthDefault        = 1.0;

/// ID 800's default index: Glass == 0 (continuous_body.h:162's kDefaultMaterial).
inline constexpr int kBodyMaterialDefaultIndex =
    static_cast<int>(Krate::DSP::ContinuousBody::kDefaultMaterial);

/// IDs 811 / 812 take the COMPONENT's own member initializers
/// (continuous_body.h:163-164) - SeraphisVoice::prepare() touches neither.
inline constexpr bool kBodyInputAgcDefault =
    Krate::DSP::ContinuousBody::kDefaultAgcEnabled;
inline constexpr bool kBodyResonatorBypassDefault =
    Krate::DSP::ContinuousBody::kDefaultResonatorBypass;

// ==============================================================================
// Parameter Storage - 10 float + 1 int + 2 bool atomics
// ==============================================================================

struct BodyParams {
    std::atomic<float> resonance{static_cast<float>(kBodyResonanceDefault)};
    std::atomic<float> damping{static_cast<float>(kBodyDampingDefault)};
    std::atomic<float> keyTracking{static_cast<float>(kBodyKeyTrackingDefault)};
    std::atomic<float> drive{static_cast<float>(kBodyDriveDefault)};
    std::atomic<float> mix{static_cast<float>(kBodyMixDefault)};
    std::atomic<float> cloudMix{static_cast<float>(kBodyCloudMixDefault)};
    std::atomic<float> cloudDecaySec{static_cast<float>(kBodyCloudDecayDefault)};
    std::atomic<float> cloudSize{static_cast<float>(kBodyCloudSizeDefault)};
    std::atomic<float> cloudDamping{static_cast<float>(kBodyCloudDampingDefault)};
    std::atomic<float> width{static_cast<float>(kBodyWidthDefault)};

    /// ID 800. The list INDEX; ContinuousBody::BodyMaterial is recovered through
    /// toBodyMaterial() (dropdown_mappings.h:197).
    std::atomic<int> material{kBodyMaterialDefaultIndex};

    std::atomic<bool> inputAgc{kBodyInputAgcDefault};
    std::atomic<bool> resonatorBypass{kBodyResonatorBypassDefault};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

inline void handleBodyParamChange(
    BodyParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    switch (id) {
        case kBodyMaterialId:
            params.material.store(
                std::clamp(static_cast<int>(
                               value * static_cast<double>(kBodyMaterialLabels.size() - 1) +
                               0.5),
                           0, static_cast<int>(kBodyMaterialLabels.size()) - 1),
                std::memory_order_relaxed);
            break;
        case kBodyResonanceId:
            params.resonance.store(
                std::clamp(static_cast<float>(kBodyResonanceMin +
                                              value * (kBodyResonanceMax - kBodyResonanceMin)),
                           static_cast<float>(kBodyResonanceMin),
                           static_cast<float>(kBodyResonanceMax)),
                std::memory_order_relaxed);
            break;
        case kBodyDampingId:
            params.damping.store(
                std::clamp(static_cast<float>(kBodyDampingMin +
                                              value * (kBodyDampingMax - kBodyDampingMin)),
                           static_cast<float>(kBodyDampingMin),
                           static_cast<float>(kBodyDampingMax)),
                std::memory_order_relaxed);
            break;
        case kBodyKeyTrackingId:
            params.keyTracking.store(
                std::clamp(static_cast<float>(kBodyKeyTrackingMin +
                                              value * (kBodyKeyTrackingMax -
                                                       kBodyKeyTrackingMin)),
                           static_cast<float>(kBodyKeyTrackingMin),
                           static_cast<float>(kBodyKeyTrackingMax)),
                std::memory_order_relaxed);
            break;
        case kBodyDriveId:
            params.drive.store(
                std::clamp(static_cast<float>(kBodyDriveMin +
                                              value * (kBodyDriveMax - kBodyDriveMin)),
                           static_cast<float>(kBodyDriveMin),
                           static_cast<float>(kBodyDriveMax)),
                std::memory_order_relaxed);
            break;
        case kBodyMixId:
            params.mix.store(
                std::clamp(static_cast<float>(kBodyMixMin +
                                              value * (kBodyMixMax - kBodyMixMin)),
                           static_cast<float>(kBodyMixMin),
                           static_cast<float>(kBodyMixMax)),
                std::memory_order_relaxed);
            break;
        case kBodyCloudMixId:
            params.cloudMix.store(
                std::clamp(static_cast<float>(kBodyCloudMixMin +
                                              value * (kBodyCloudMixMax - kBodyCloudMixMin)),
                           static_cast<float>(kBodyCloudMixMin),
                           static_cast<float>(kBodyCloudMixMax)),
                std::memory_order_relaxed);
            break;
        case kBodyCloudDecayId:
            params.cloudDecaySec.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kBodyCloudDecayMin, kBodyCloudDecayMax)),
                std::memory_order_relaxed);
            break;
        case kBodyCloudSizeId:
            params.cloudSize.store(
                std::clamp(static_cast<float>(kBodyCloudSizeMin +
                                              value * (kBodyCloudSizeMax - kBodyCloudSizeMin)),
                           static_cast<float>(kBodyCloudSizeMin),
                           static_cast<float>(kBodyCloudSizeMax)),
                std::memory_order_relaxed);
            break;
        case kBodyCloudDampingId:
            params.cloudDamping.store(
                std::clamp(static_cast<float>(kBodyCloudDampingMin +
                                              value * (kBodyCloudDampingMax -
                                                       kBodyCloudDampingMin)),
                           static_cast<float>(kBodyCloudDampingMin),
                           static_cast<float>(kBodyCloudDampingMax)),
                std::memory_order_relaxed);
            break;
        case kBodyWidthId:
            params.width.store(
                std::clamp(static_cast<float>(kBodyWidthMin +
                                              value * (kBodyWidthMax - kBodyWidthMin)),
                           static_cast<float>(kBodyWidthMin),
                           static_cast<float>(kBodyWidthMax)),
                std::memory_order_relaxed);
            break;
        case kBodyInputAgcId:
            params.inputAgc.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kBodyResonatorBypassId:
            params.resonatorBypass.store(value >= 0.5, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-016, FR-048)
// ==============================================================================

inline void registerBodyParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // ID 800 (Glass) through Seraphis::addDropdownParam (dropdown_mappings.h) -
    // the one path that pins info.defaultNormalizedValue, used even though this
    // default index is 0, so changing it later cannot silently un-pin it.
    addDropdownParam(parameters, STR16("Body Material"), kBodyMaterialId,
                     kBodyMaterialDefaultIndex, kBodyMaterialLabels.data(),
                     static_cast<int>(kBodyMaterialLabels.size()));

    parameters.addParameter(STR16("Body Resonance"), STR16(""), 0,
                            (kBodyResonanceDefault - kBodyResonanceMin) /
                                (kBodyResonanceMax - kBodyResonanceMin),
                            ParameterInfo::kCanAutomate, kBodyResonanceId);
    parameters.addParameter(STR16("Body Damping"), STR16(""), 0,
                            (kBodyDampingDefault - kBodyDampingMin) /
                                (kBodyDampingMax - kBodyDampingMin),
                            ParameterInfo::kCanAutomate, kBodyDampingId);
    parameters.addParameter(STR16("Body Key Tracking"), STR16(""), 0,
                            (kBodyKeyTrackingDefault - kBodyKeyTrackingMin) /
                                (kBodyKeyTrackingMax - kBodyKeyTrackingMin),
                            ParameterInfo::kCanAutomate, kBodyKeyTrackingId);
    parameters.addParameter(STR16("Body Drive"), STR16(""), 0,
                            (kBodyDriveDefault - kBodyDriveMin) /
                                (kBodyDriveMax - kBodyDriveMin),
                            ParameterInfo::kCanAutomate, kBodyDriveId);
    parameters.addParameter(STR16("Body Mix"), STR16(""), 0,
                            (kBodyMixDefault - kBodyMixMin) / (kBodyMixMax - kBodyMixMin),
                            ParameterInfo::kCanAutomate, kBodyMixId);
    parameters.addParameter(STR16("Body Cloud Mix"), STR16(""), 0,
                            (kBodyCloudMixDefault - kBodyCloudMixMin) /
                                (kBodyCloudMixMax - kBodyCloudMixMin),
                            ParameterInfo::kCanAutomate, kBodyCloudMixId);
    parameters.addParameter(STR16("Body Cloud Decay"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(kBodyCloudDecayDefault,
                                                               kBodyCloudDecayMin,
                                                               kBodyCloudDecayMax),
                            ParameterInfo::kCanAutomate, kBodyCloudDecayId);
    parameters.addParameter(STR16("Body Cloud Size"), STR16(""), 0,
                            (kBodyCloudSizeDefault - kBodyCloudSizeMin) /
                                (kBodyCloudSizeMax - kBodyCloudSizeMin),
                            ParameterInfo::kCanAutomate, kBodyCloudSizeId);
    parameters.addParameter(STR16("Body Cloud Damping"), STR16(""), 0,
                            (kBodyCloudDampingDefault - kBodyCloudDampingMin) /
                                (kBodyCloudDampingMax - kBodyCloudDampingMin),
                            ParameterInfo::kCanAutomate, kBodyCloudDampingId);
    parameters.addParameter(STR16("Body Width"), STR16(""), 0,
                            (kBodyWidthDefault - kBodyWidthMin) /
                                (kBodyWidthMax - kBodyWidthMin),
                            ParameterInfo::kCanAutomate, kBodyWidthId);

    parameters.addParameter(STR16("Body Input AGC"), STR16(""), 1,
                            kBodyInputAgcDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kBodyInputAgcId);
    parameters.addParameter(STR16("Body Resonator Bypass"), STR16(""), 1,
                            kBodyResonatorBypassDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kBodyResonatorBypassId);
}

// ==============================================================================
// Display Formatting (FR-061: ID 800 formats itself and is NOT claimed here)
// ==============================================================================

inline Steinberg::tresult formatBodyParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        case kBodyResonanceId:
        case kBodyDampingId:
        case kBodyKeyTrackingId:
        case kBodyMixId:
        case kBodyCloudMixId:
        case kBodyCloudSizeId:
        case kBodyCloudDampingId:
        case kBodyWidthId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kBodyDriveId:
            snprintf(text, sizeof(text), "%.2f",
                     kBodyDriveMin + value * (kBodyDriveMax - kBodyDriveMin));
            break;
        case kBodyCloudDecayId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(value, kBodyCloudDecayMin,
                                                          kBodyCloudDecayMax));
            break;
        case kBodyInputAgcId:
        case kBodyResonatorBypassId:
            snprintf(text, sizeof(text), "%s", (value >= 0.5) ? "On" : "Off");
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 52 bytes, plan 5.1's [body] block order
// ==============================================================================
// 10 floats (801-810) | 3 int32 (800 material, 811 agc, 812 bypass).

inline void saveBodyParams(const BodyParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.resonance.load(std::memory_order_relaxed));
    streamer.writeFloat(params.damping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.keyTracking.load(std::memory_order_relaxed));
    streamer.writeFloat(params.drive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cloudMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cloudDecaySec.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cloudSize.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cloudDamping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));

    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.material.load(std::memory_order_relaxed)));
    streamer.writeInt32(params.inputAgc.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.resonatorBypass.load(std::memory_order_relaxed) ? 1 : 0);
}

/// EOF-safe (FR-093).
inline bool loadBodyParams(BodyParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.resonance.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.damping.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.keyTracking.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.drive.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.cloudMix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.cloudDecaySec.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.cloudSize.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.cloudDamping.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.width.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.material.store(
        std::clamp(static_cast<int>(iv), 0,
                   static_cast<int>(kBodyMaterialLabels.size()) - 1),
        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.inputAgc.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.resonatorBypass.store(iv != 0, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadBodyParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (streamer.readFloat(fv)) {
        setParam(kBodyResonanceId,
                 std::clamp((static_cast<double>(fv) - kBodyResonanceMin) /
                                (kBodyResonanceMax - kBodyResonanceMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyDampingId,
                 std::clamp((static_cast<double>(fv) - kBodyDampingMin) /
                                (kBodyDampingMax - kBodyDampingMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyKeyTrackingId,
                 std::clamp((static_cast<double>(fv) - kBodyKeyTrackingMin) /
                                (kBodyKeyTrackingMax - kBodyKeyTrackingMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyDriveId,
                 std::clamp((static_cast<double>(fv) - kBodyDriveMin) /
                                (kBodyDriveMax - kBodyDriveMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyMixId,
                 std::clamp((static_cast<double>(fv) - kBodyMixMin) /
                                (kBodyMixMax - kBodyMixMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyCloudMixId,
                 std::clamp((static_cast<double>(fv) - kBodyCloudMixMin) /
                                (kBodyCloudMixMax - kBodyCloudMixMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyCloudDecayId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kBodyCloudDecayMin,
                                                    kBodyCloudDecayMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyCloudSizeId,
                 std::clamp((static_cast<double>(fv) - kBodyCloudSizeMin) /
                                (kBodyCloudSizeMax - kBodyCloudSizeMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyCloudDampingId,
                 std::clamp((static_cast<double>(fv) - kBodyCloudDampingMin) /
                                (kBodyCloudDampingMax - kBodyCloudDampingMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kBodyWidthId,
                 std::clamp((static_cast<double>(fv) - kBodyWidthMin) /
                                (kBodyWidthMax - kBodyWidthMin),
                            0.0, 1.0));
    }

    if (streamer.readInt32(iv)) {
        setParam(kBodyMaterialId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kBodyMaterialLabels.size()) - 1)) /
                     static_cast<double>(kBodyMaterialLabels.size() - 1));
    }
    if (streamer.readInt32(iv)) {
        setParam(kBodyInputAgcId, iv != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(iv)) {
        setParam(kBodyResonatorBypassId, iv != 0 ? 1.0 : 0.0);
    }
}

}  // namespace Seraphis
