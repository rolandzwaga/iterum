#pragma once

// ==============================================================================
// Seraphis - Integrated Effects Parameters (ID 1400-1443, Phase 10)
// ==============================================================================
// The six-function contract, in the shape aether_params.h implements it (line
// numbers as of 2026-08-02; the NAMES are the stable citation, the numbers move
// whenever that header grows): handleAetherParamChange:100, registerAetherParams:161,
// formatAetherParam:221, saveAetherParams:274, loadAetherParams:297,
// loadAetherParamsToController:347, over a struct of std::atomic<> fields at :73.
//
// UNLIKE aether_params.h, EVERY range constant this pack needs is PUBLIC on its
// DSP component, so nothing here is re-typed by hand: each named constant below
// is an alias of the component's own constant, with the source line cited beside
// it (FR-015). That is strictly stronger than aether_params.h:15-23's
// transcription, which exists only because AetherReverb's three range constants
// sit under `private:` and no plugin TU can name them.
//
// The ONE deliberate exception is kFxDelayFeedbackMax, which is NOT the
// component's kMaxFeedback: spec C-7 clause 2 registers a narrower maximum on
// purpose, and tiltCompensatedFeedback below is what makes that cap actually
// bound the per-bin loop gain.
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kFxSpreadDirectionLabels, kFxDelaySyncNoteLabels,
                                           // kFxDelaySyncNoteDefaultIndex, addDropdownParam
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/effects/spectral_delay.h>       // SpectralDelay ranges (C-6 rows 1411-1419)
#include <krate/dsp/processors/brownian_drift.h>    // BrownianDrift::kDefaultSmoothness (row 1442)
#include <krate/dsp/processors/midside_processor.h> // MidSideProcessor width range (row 1440)
#include <krate/dsp/systems/seraphis_engine.h>      // SeraphisEngine::kOutputSaturation (row 1400)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 ranges - transcribed ONCE, each with its source line (FR-015)
// ==============================================================================

inline constexpr float kFxDelayTimeMinMs   = Krate::DSP::SpectralDelay::kMinDelayMs;    ///< == spectral_delay.h:91  (0)
inline constexpr float kFxDelayTimeMaxMs   = Krate::DSP::SpectralDelay::kMaxDelayMs;    ///< == :92  (2000)
inline constexpr float kFxDelaySpreadMinMs = Krate::DSP::SpectralDelay::kMinSpreadMs;   ///< == :95  (0)
inline constexpr float kFxDelaySpreadMaxMs = Krate::DSP::SpectralDelay::kMaxSpreadMs;   ///< == :96  (2000)
inline constexpr float kFxDelayTiltMin     = Krate::DSP::SpectralDelay::kMinTilt;       ///< == :101 (-1)
inline constexpr float kFxDelayTiltMax     = Krate::DSP::SpectralDelay::kMaxTilt;       ///< == :102 (+1)
inline constexpr float kFxWidthMinPercent  = Krate::DSP::MidSideProcessor::kMinWidth;   ///< == midside_processor.h:65 (0)
inline constexpr float kFxWidthMaxPercent  = Krate::DSP::MidSideProcessor::kMaxWidth;   ///< == :66 (200)

/// C-7 clause 2 / FR-016. DELIBERATELY BELOW the component's own
/// kMaxFeedback = 1.2f (spectral_delay.h:99), which is documented "Allow slight
/// overdrive" and is a per-bin loop gain above unity in a GLOBAL, always-summed
/// bus. Raising this to 1.2 is forbidden.
///
/// The cap ALONE bounds nothing: calculateTiltedFeedback (:603-614) multiplies
/// the global feedback by a tilt factor spanning [0, 2] and clamps the PRODUCT to
/// kMaxFeedback, so at feedback 0.95 with tilt +1 the upper 243 of 513 bins would
/// receive a loop gain > 1 and sustain forever. tiltCompensatedFeedback below is
/// what actually establishes the bound.
inline constexpr float kFxDelayFeedbackMax = 0.95f;

// ==============================================================================
// FR-016a - the tilt compensation, and it lives HERE, not in processor.cpp
// ==============================================================================
// Two test obligations evaluate this from another translation unit (SC-005
// clause 1's 513-bin sweep and FR-016a's own SECTION, both in
// effects_chain_test.cpp), and a constexpr function defined in a .cpp with no
// header declaration is neither callable nor constant-evaluable from there.
//
// The derivation: the worst per-bin loop gain calculateTiltedFeedback can
// produce for a pushed feedback f is f * (1 + |tilt|). Pushing
// f = fb / (1 + |tilt|) makes that worst bin land back at exactly fb, so
// fb <= kFxDelayFeedbackMax = 0.95 < 1.0 bounds the loop at EVERY tilt setting,
// and tilt 0 is unchanged.
//
// std::abs is NOT used: <cmath>'s float overloads are not constexpr before C++23
// on every toolchain leg, so the branchless form is mandatory.

[[nodiscard]] inline constexpr float tiltCompensatedFeedback(float fb, float tilt) noexcept {
    const float mag = (tilt < 0.0f) ? -tilt : tilt;
    return fb / (1.0f + mag);
}

static_assert(tiltCompensatedFeedback(kFxDelayFeedbackMax, 1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, -1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, 0.0f) == kFxDelayFeedbackMax,
              "FR-016a: the worst tilted bin must land back at the registered cap");

// ==============================================================================
// C-6 defaults - every one is the value the shipped chain already produces (C-7)
// ==============================================================================

inline constexpr float kFxSaturationDefault  = Krate::DSP::SeraphisEngine::kOutputSaturation;   ///< == seraphis_engine.h:248 (0.15)
inline constexpr float kFxDelayMixDefault    = 0.0f;                                           ///< 0 => full send bypass (C-3)
inline constexpr float kFxDelayTimeDefault   = Krate::DSP::SpectralDelay::kDefaultDelayMs;     ///< == spectral_delay.h:93 (250)
inline constexpr float kFxDelaySpreadDefault = 0.0f;
inline constexpr int   kFxDelaySpreadDirectionDefault = 0;                                     ///< SpreadDirection::LowToHigh
inline constexpr float kFxDelayFeedbackDefault  = 0.35f;
inline constexpr float kFxDelayTiltDefault      = 0.0f;
inline constexpr float kFxDelayDiffusionDefault = 0.30f;
inline constexpr float kFxDelayWidthDefault     = 0.50f;
inline constexpr bool  kFxDelaySyncDefault      = false;
inline constexpr int   kFxDelaySyncNoteDefault  = kFxDelaySyncNoteDefaultIndex;                ///< == dropdown_mappings.h:276 (7 = "1/16")
inline constexpr bool  kFxSpectralFreezeDefault = false;
inline constexpr float kFxWidthDefault       = Krate::DSP::MidSideProcessor::kDefaultWidth;    ///< == midside_processor.h:67 (100)
inline constexpr float kFxWanderDepthDefault = 0.0f;
inline constexpr float kFxWanderRateDefault  = Krate::DSP::BrownianDrift::kDefaultSmoothness;  ///< == brownian_drift.h:107 (0.50)
inline constexpr float kFxAzimuthDepthDefault = 0.0f;

// ==============================================================================
// Parameter Storage - 12 float + 2 int + 2 bool atomics
// ==============================================================================

struct EffectsParams {
    std::atomic<float> saturation{kFxSaturationDefault};            ///< 1400
    std::atomic<float> delayMix{kFxDelayMixDefault};                ///< 1410
    std::atomic<float> delayTimeMs{kFxDelayTimeDefault};            ///< 1411
    std::atomic<float> delaySpreadMs{kFxDelaySpreadDefault};        ///< 1412
    std::atomic<int>   spreadDirection{kFxDelaySpreadDirectionDefault};  ///< 1413
    std::atomic<float> delayFeedback{kFxDelayFeedbackDefault};      ///< 1414, always <= kFxDelayFeedbackMax
    std::atomic<float> delayTilt{kFxDelayTiltDefault};              ///< 1415
    std::atomic<float> delayDiffusion{kFxDelayDiffusionDefault};    ///< 1416
    std::atomic<float> delayWidth{kFxDelayWidthDefault};            ///< 1417
    std::atomic<bool>  delaySync{kFxDelaySyncDefault};              ///< 1418
    std::atomic<int>   delaySyncNote{kFxDelaySyncNoteDefault};      ///< 1419
    std::atomic<bool>  spectralFreeze{kFxSpectralFreezeDefault};    ///< 1430
    std::atomic<float> width{kFxWidthDefault};                      ///< 1440, percent
    std::atomic<float> wanderDepth{kFxWanderDepthDefault};          ///< 1441
    std::atomic<float> wanderRate{kFxWanderRateDefault};            ///< 1442
    std::atomic<float> azimuthDepth{kFxAzimuthDepthDefault};        ///< 1443
};

// ==============================================================================
// Parameter Change Handler (FR-018)
// ==============================================================================

namespace detail {

/// The `L` denormalization form, shared by this pack's two dropdowns. Named
/// inside `Seraphis::detail` beside morph_params.h's morphDropdownIndex (:130),
/// with its own name so the two cannot collide.
[[nodiscard]] inline int effectsDropdownIndex(Steinberg::Vst::ParamValue value,
                                              int entryCount) noexcept {
    return std::clamp(static_cast<int>(value * static_cast<double>(entryCount - 1) + 0.5),
                      0, entryCount - 1);
}

}  // namespace detail

inline void handleEffectsParamChange(
    EffectsParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    const float unit = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kFxSaturationId:
            params.saturation.store(unit, std::memory_order_relaxed);
            break;
        case kFxDelayMixId:
            params.delayMix.store(unit, std::memory_order_relaxed);
            break;
        case kFxDelayTimeId:
            params.delayTimeMs.store(
                std::clamp(static_cast<float>(kFxDelayTimeMinMs +
                                              value * (kFxDelayTimeMaxMs - kFxDelayTimeMinMs)),
                           kFxDelayTimeMinMs, kFxDelayTimeMaxMs),
                std::memory_order_relaxed);
            break;
        case kFxDelaySpreadId:
            params.delaySpreadMs.store(
                std::clamp(static_cast<float>(kFxDelaySpreadMinMs +
                                              value * (kFxDelaySpreadMaxMs - kFxDelaySpreadMinMs)),
                           kFxDelaySpreadMinMs, kFxDelaySpreadMaxMs),
                std::memory_order_relaxed);
            break;
        case kFxDelaySpreadDirectionId:
            params.spreadDirection.store(
                detail::effectsDropdownIndex(
                    value, static_cast<int>(kFxSpreadDirectionLabels.size())),
                std::memory_order_relaxed);
            break;
        case kFxDelayFeedbackId:
            // Clamped to [0, kFxDelayFeedbackMax] HERE, before any use: FR-016a's
            // compensation divide must never see an out-of-range value, and a
            // state blob or a hostile host can supply one (spec Edge cases).
            params.delayFeedback.store(
                std::clamp(static_cast<float>(value * kFxDelayFeedbackMax),
                           0.0f, kFxDelayFeedbackMax),
                std::memory_order_relaxed);
            break;
        case kFxDelayTiltId:
            params.delayTilt.store(
                std::clamp(static_cast<float>(kFxDelayTiltMin +
                                              value * (kFxDelayTiltMax - kFxDelayTiltMin)),
                           kFxDelayTiltMin, kFxDelayTiltMax),
                std::memory_order_relaxed);
            break;
        case kFxDelayDiffusionId:
            params.delayDiffusion.store(unit, std::memory_order_relaxed);
            break;
        case kFxDelayWidthId:
            params.delayWidth.store(unit, std::memory_order_relaxed);
            break;
        case kFxDelaySyncId:
            params.delaySync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kFxDelaySyncNoteId:
            params.delaySyncNote.store(
                detail::effectsDropdownIndex(
                    value, static_cast<int>(kFxDelaySyncNoteLabels.size())),
                std::memory_order_relaxed);
            break;
        case kFxSpectralFreezeId:
            params.spectralFreeze.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kFxWidthId:
            params.width.store(
                std::clamp(static_cast<float>(kFxWidthMinPercent +
                                              value * (kFxWidthMaxPercent - kFxWidthMinPercent)),
                           kFxWidthMinPercent, kFxWidthMaxPercent),
                std::memory_order_relaxed);
            break;
        case kFxWanderDepthId:
            params.wanderDepth.store(unit, std::memory_order_relaxed);
            break;
        case kFxWanderRateId:
            params.wanderRate.store(unit, std::memory_order_relaxed);
            break;
        case kFxAzimuthDepthId:
            params.azimuthDepth.store(unit, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (C-6: 12 R + 2 L + 2 T = 16; the types are FROZEN)
// ==============================================================================

inline void registerEffectsParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("FX Saturation"), STR16(""), 0,
                            static_cast<ParamValue>(kFxSaturationDefault),
                            ParameterInfo::kCanAutomate, kFxSaturationId);
    parameters.addParameter(STR16("Delay Mix"), STR16(""), 0,
                            static_cast<ParamValue>(kFxDelayMixDefault),
                            ParameterInfo::kCanAutomate, kFxDelayMixId);
    parameters.addParameter(STR16("Delay Time"), STR16("ms"), 0,
                            static_cast<ParamValue>(kFxDelayTimeDefault - kFxDelayTimeMinMs) /
                                static_cast<ParamValue>(kFxDelayTimeMaxMs - kFxDelayTimeMinMs),
                            ParameterInfo::kCanAutomate, kFxDelayTimeId);
    parameters.addParameter(STR16("Delay Spread"), STR16("ms"), 0,
                            static_cast<ParamValue>(kFxDelaySpreadDefault - kFxDelaySpreadMinMs) /
                                static_cast<ParamValue>(kFxDelaySpreadMaxMs - kFxDelaySpreadMinMs),
                            ParameterInfo::kCanAutomate, kFxDelaySpreadId);

    // ID 1413 through Seraphis::addDropdownParam (dropdown_mappings.h) - the one
    // path that pins info.defaultNormalizedValue, used even though this default
    // index is 0, so changing it later cannot silently un-pin it.
    addDropdownParam(parameters, STR16("Delay Spread Dir"), kFxDelaySpreadDirectionId,
                     kFxDelaySpreadDirectionDefault, kFxSpreadDirectionLabels.data(),
                     static_cast<int>(kFxSpreadDirectionLabels.size()));

    parameters.addParameter(STR16("Delay Feedback"), STR16(""), 0,
                            static_cast<ParamValue>(kFxDelayFeedbackDefault) /
                                static_cast<ParamValue>(kFxDelayFeedbackMax),
                            ParameterInfo::kCanAutomate, kFxDelayFeedbackId);
    parameters.addParameter(STR16("Delay Tilt"), STR16(""), 0,
                            static_cast<ParamValue>(kFxDelayTiltDefault - kFxDelayTiltMin) /
                                static_cast<ParamValue>(kFxDelayTiltMax - kFxDelayTiltMin),
                            ParameterInfo::kCanAutomate, kFxDelayTiltId);
    parameters.addParameter(STR16("Delay Diffusion"), STR16(""), 0,
                            static_cast<ParamValue>(kFxDelayDiffusionDefault),
                            ParameterInfo::kCanAutomate, kFxDelayDiffusionId);
    parameters.addParameter(STR16("Delay Width"), STR16(""), 0,
                            static_cast<ParamValue>(kFxDelayWidthDefault),
                            ParameterInfo::kCanAutomate, kFxDelayWidthId);
    parameters.addParameter(STR16("Delay Sync"), STR16(""), 1,
                            kFxDelaySyncDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kFxDelaySyncId);

    // ID 1419: default index 7 ("1/16"), which is exactly why this MUST go
    // through addDropdownParam - createDropdownParameterWithDefault alone leaves
    // info.defaultNormalizedValue at 0 (dropdown_mappings.h:356-363).
    addDropdownParam(parameters, STR16("Delay Sync Note"), kFxDelaySyncNoteId,
                     kFxDelaySyncNoteDefault, kFxDelaySyncNoteLabels.data(),
                     static_cast<int>(kFxDelaySyncNoteLabels.size()));

    parameters.addParameter(STR16("Spectral Freeze"), STR16(""), 1,
                            kFxSpectralFreezeDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kFxSpectralFreezeId);
    parameters.addParameter(STR16("Stereo Width"), STR16("%"), 0,
                            static_cast<ParamValue>(kFxWidthDefault - kFxWidthMinPercent) /
                                static_cast<ParamValue>(kFxWidthMaxPercent - kFxWidthMinPercent),
                            ParameterInfo::kCanAutomate, kFxWidthId);
    parameters.addParameter(STR16("Wander Depth"), STR16(""), 0,
                            static_cast<ParamValue>(kFxWanderDepthDefault),
                            ParameterInfo::kCanAutomate, kFxWanderDepthId);
    parameters.addParameter(STR16("Wander Rate"), STR16(""), 0,
                            static_cast<ParamValue>(kFxWanderRateDefault),
                            ParameterInfo::kCanAutomate, kFxWanderRateId);
    parameters.addParameter(STR16("Azimuth Depth"), STR16(""), 0,
                            static_cast<ParamValue>(kFxAzimuthDepthDefault),
                            ParameterInfo::kCanAutomate, kFxAzimuthDepthId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================
// Unlike every other Seraphis pack, this one DOES claim its two dropdowns
// (1413, 1419): it writes the label the index names, out of the single table
// registration used, so the displayed string cannot drift from the registered
// list.

inline Steinberg::tresult formatEffectsParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        // The plain [0,1] rows.
        case kFxSaturationId:
        case kFxDelayMixId:
        case kFxDelayDiffusionId:
        case kFxDelayWidthId:
        case kFxWanderDepthId:
        case kFxWanderRateId:
        case kFxAzimuthDepthId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kFxDelayFeedbackId:
            // Same %.0f%% shape, but denormalized through the row's OWN maximum
            // rather than through 1.0 - C-6 registers this row as lin [0, 0.95],
            // so the percentage shown is the feedback coefficient the component
            // is actually given (FR-016's cap, before FR-016a's compensation).
            snprintf(text, sizeof(text), "%.0f%%",
                     std::clamp(value, 0.0, 1.0) *
                         static_cast<double>(kFxDelayFeedbackMax) * 100.0);
            break;
        case kFxDelayTimeId:
            snprintf(text, sizeof(text), "%.1f ms",
                     kFxDelayTimeMinMs + value * (kFxDelayTimeMaxMs - kFxDelayTimeMinMs));
            break;
        case kFxDelaySpreadId:
            snprintf(text, sizeof(text), "%.1f ms",
                     kFxDelaySpreadMinMs + value * (kFxDelaySpreadMaxMs - kFxDelaySpreadMinMs));
            break;
        case kFxDelayTiltId:
            snprintf(text, sizeof(text), "%+.2f",
                     kFxDelayTiltMin + value * (kFxDelayTiltMax - kFxDelayTiltMin));
            break;
        case kFxWidthId:
            snprintf(text, sizeof(text), "%.0f %%",
                     kFxWidthMinPercent + value * (kFxWidthMaxPercent - kFxWidthMinPercent));
            break;
        case kFxDelaySyncId:
        case kFxSpectralFreezeId:
            snprintf(text, sizeof(text), "%s", (value >= 0.5) ? "On" : "Off");
            break;

        // The two `L` rows write UTF-16 straight out of the registered table, so
        // they return here rather than falling through to the ASCII conversion.
        case kFxDelaySpreadDirectionId: {
            const int index = detail::effectsDropdownIndex(
                value, static_cast<int>(kFxSpreadDirectionLabels.size()));
            UString(string, 128).assign(
                kFxSpreadDirectionLabels[static_cast<std::size_t>(index)]);
            return kResultOk;
        }
        case kFxDelaySyncNoteId: {
            const int index = detail::effectsDropdownIndex(
                value, static_cast<int>(kFxDelaySyncNoteLabels.size()));
            UString(string, 128).assign(
                kFxDelaySyncNoteLabels[static_cast<std::size_t>(index)]);
            return kResultOk;
        }

        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - EXACTLY 64 bytes, C-8's [effects] block order
// ==============================================================================
// 12 floats in C-6 table order (1400, 1410, 1411, 1412, 1414, 1415, 1416, 1417,
// 1440, 1441, 1442, 1443) | 4 int32 (1413, 1419, 1418, 1430).
//
// THIS ORDER IS FIXED HERE and mirrored exactly by loadEffectsParams and
// loadEffectsParamsToController below. The block is appended AFTER every
// version-2 field, so a version-2 stream stays a strict byte prefix of a
// version-3 stream and the EOF-safe loader chain migrates with no version-aware
// branch (C-8).

inline void saveEffectsParams(const EffectsParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.saturation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayTimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delaySpreadMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayFeedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayTilt.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayDiffusion.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayWidth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
    streamer.writeFloat(params.wanderDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.wanderRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.azimuthDepth.load(std::memory_order_relaxed));

    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.spreadDirection.load(std::memory_order_relaxed)));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.delaySyncNote.load(std::memory_order_relaxed)));
    streamer.writeInt32(params.delaySync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.spectralFreeze.load(std::memory_order_relaxed) ? 1 : 0);
}

/// EOF-safe: every read is guarded, a short stream returns false, and every
/// unread field is left at its C-6 default - which by C-7 is the behaviour a
/// version-1 or version-2 stream already had, so `false` here is not an error.
inline bool loadEffectsParams(EffectsParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.saturation.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delayMix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delayTimeMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delaySpreadMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    // Re-clamped for the same reason handleEffectsParamChange clamps it: a
    // corrupt or hand-edited blob must not reach FR-016a's compensation divide
    // with a value the registered range cannot produce (spec Edge cases).
    params.delayFeedback.store(std::clamp(fv, 0.0f, kFxDelayFeedbackMax),
                               std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delayTilt.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delayDiffusion.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.delayWidth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.width.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.wanderDepth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.wanderRate.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.azimuthDepth.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.spreadDirection.store(
        std::clamp(static_cast<int>(iv), 0,
                   static_cast<int>(kFxSpreadDirectionLabels.size()) - 1),
        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.delaySyncNote.store(
        std::clamp(static_cast<int>(iv), 0,
                   static_cast<int>(kFxDelaySyncNoteLabels.size()) - 1),
        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.delaySync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.spectralFreeze.store(iv != 0, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadEffectsParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    const auto unitParam = [&setParam](Steinberg::Vst::ParamID id, float plain) {
        setParam(id, std::clamp(static_cast<double>(plain), 0.0, 1.0));
    };

    if (streamer.readFloat(fv)) { unitParam(kFxSaturationId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kFxDelayMixId, fv); }
    if (streamer.readFloat(fv)) {
        setParam(kFxDelayTimeId,
                 std::clamp((static_cast<double>(fv) - kFxDelayTimeMinMs) /
                                (static_cast<double>(kFxDelayTimeMaxMs) - kFxDelayTimeMinMs),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kFxDelaySpreadId,
                 std::clamp((static_cast<double>(fv) - kFxDelaySpreadMinMs) /
                                (static_cast<double>(kFxDelaySpreadMaxMs) - kFxDelaySpreadMinMs),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kFxDelayFeedbackId,
                 std::clamp(static_cast<double>(fv) / static_cast<double>(kFxDelayFeedbackMax),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kFxDelayTiltId,
                 std::clamp((static_cast<double>(fv) - kFxDelayTiltMin) /
                                (static_cast<double>(kFxDelayTiltMax) - kFxDelayTiltMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) { unitParam(kFxDelayDiffusionId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kFxDelayWidthId, fv); }
    if (streamer.readFloat(fv)) {
        setParam(kFxWidthId,
                 std::clamp((static_cast<double>(fv) - kFxWidthMinPercent) /
                                (static_cast<double>(kFxWidthMaxPercent) - kFxWidthMinPercent),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) { unitParam(kFxWanderDepthId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kFxWanderRateId, fv); }
    if (streamer.readFloat(fv)) { unitParam(kFxAzimuthDepthId, fv); }

    if (streamer.readInt32(iv)) {
        setParam(kFxDelaySpreadDirectionId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kFxSpreadDirectionLabels.size()) - 1)) /
                     static_cast<double>(kFxSpreadDirectionLabels.size() - 1));
    }
    if (streamer.readInt32(iv)) {
        setParam(kFxDelaySyncNoteId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kFxDelaySyncNoteLabels.size()) - 1)) /
                     static_cast<double>(kFxDelaySyncNoteLabels.size() - 1));
    }
    if (streamer.readInt32(iv)) {
        setParam(kFxDelaySyncId, iv != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(iv)) {
        setParam(kFxSpectralFreezeId, iv != 0 ? 1.0 : 0.0);
    }
}

}  // namespace Seraphis
