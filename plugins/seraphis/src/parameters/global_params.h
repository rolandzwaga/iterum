#pragma once

// ==============================================================================
// Seraphis - Global Parameters (ID 0-99)   FR-040, FR-043, FR-044, FR-048
// ==============================================================================
// The Ruinae six-function contract (plugins/ruinae/src/parameters/global_params.h:
// 39, 87, 130, 178, 187, 220), reduced to Phase 8's three globals + Phase 9's
// kSeedId, plus TWO deliberate divergences:
//   1. clampPolyphony() (see below);
//   2. the seed's state functions are a SEPARATE, explicitly-positioned trio
//      (saveGlobalSeed / loadGlobalSeed / loadGlobalSeedToController) written
//      AFTER the [macro] block, NOT a fourth field inside saveGlobalParams -
//      see the banner above them (FR-091a, plan section 5.2).
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kSeedLabels (FR-015)
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h (FR-048)

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/systems/seraphis_engine.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct GlobalParams {
    /// Linear gain, [0, 2]; normalized 0.5 == unity.
    std::atomic<float> masterGain{1.0f};
    /// [1, 16]; matches SeraphisEngineConfig::polyphony's default of 8.
    std::atomic<int> polyphony{8};
    /// FR-044. ON -> SeraphisEngine::kOutputSaturation (0.15), OFF -> 0.0.
    ///
    /// THIS CONTROLS THE TAPE-SATURATION AMOUNT ONLY. It does NOT bypass the
    /// true-peak limiter: SeraphisEngine::processOutputStage ends with
    /// `limiter_.processBlock(l, r, n)` and has no bypass path
    /// (dsp/include/krate/dsp/systems/seraphis_engine.h:521). Turning it off
    /// removes the tape colouration, never the peak ceiling.
    std::atomic<bool> softLimit{true};
    /// FR-091a. INDEX into Seraphis::kSeedValues (dropdown_mappings.h:65), NOT
    /// the seed constant itself: index 0 is pinned to 1u so the Phase 8
    /// kEngineSeed == kReverbSeed == 1u survives as the registered default.
    ///
    /// It is stored and persisted SEPARATELY from the three fields above - see
    /// saveGlobalSeed / loadGlobalSeed at the bottom of this header.
    std::atomic<int> seedIndex{0};
};

// ==============================================================================
// The ONE conversion into the engine's polyphony domain
// ==============================================================================
// MANDATORY, and a deliberate divergence from Ruinae, whose loader stores the
// raw stream value (ruinae/src/parameters/global_params.h:197-198). Seraphis
// cannot: pushGlobalParams() compares the stored value against the engine's
// CLAMPED getPolyphony() (seraphis_engine.h:322, :665). A corrupt stream
// carrying polyphony 0, 20 or a negative int32 would then make the change
// detector fire on EVERY BLOCK, FOREVER -- re-arming sumGain_ and walking the
// allocator's excess-slot loop per block. Clamping at the single conversion
// point puts both sides of the comparison in the same domain.
// ==============================================================================

[[nodiscard]] inline std::size_t clampPolyphony(int raw) noexcept {
    return std::clamp(static_cast<std::size_t>(std::max(raw, 1)),
                      std::size_t{1},
                      Krate::DSP::SeraphisEngine::kMaxVoices);  // :130 == 16
}

// ==============================================================================
// Parameter Change Handler (FR-043)
// ==============================================================================

inline void handleGlobalParamChange(
    GlobalParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    switch (id) {
        case kMasterGainId:
            // 0-1 normalized -> 0-2 linear gain
            params.masterGain.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed);
            break;
        case kPolyphonyId:
            // 0-1 normalized -> 1-16
            params.polyphony.store(
                std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16),
                std::memory_order_relaxed);
            break;
        case kSoftLimitId:
            params.softLimit.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kSeedId:
            // 0-1 normalized -> index 0-15 (the `L` denormalization form,
            // plan section 2.3.1). FR-018: clamped into the C-6 plain range
            // BEFORE storing, so FR-042's change detector cannot latch on an
            // out-of-range index.
            params.seedIndex.store(
                std::clamp(static_cast<int>(value * 15.0 + 0.5), 0, 15),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-048 - THE REGISTERED TYPES ARE FROZEN)
// ==============================================================================

inline void registerGlobalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Master Gain (0-200%, default 100% = normalized 0.5) -> plain Vst::Parameter
    parameters.addParameter(
        STR16("Master Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kMasterGainId);

    // Polyphony (1-16, default 8 => index 7) -> Vst::StringListParameter.
    // Added through addDropdownParam (dropdown_mappings.h) so the registered
    // default is index 7 and not 0 - the shared helper leaves
    // info.defaultNormalizedValue untouched, so a host "reset to default" on
    // this control used to yield ONE voice. The ID, type, unit and intended
    // default are unchanged, so FR-063 is untouched. The sixteen labels stay
    // INLINE (they are ordinals, not a mapping) - FR-015's single-table rule is
    // about label lists that registration AND formatting both read, and nothing
    // formats this one.
    addDropdownParam(parameters,
                     Krate::Plugins::createDropdownParameterWithDefault(
                         STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/7,
                         {STR16("1"), STR16("2"), STR16("3"), STR16("4"),
                          STR16("5"), STR16("6"), STR16("7"), STR16("8"),
                          STR16("9"), STR16("10"), STR16("11"), STR16("12"),
                          STR16("13"), STR16("14"), STR16("15"), STR16("16")}),
                     /*defaultIndex=*/7);

    // Soft Limit (tape-saturation amount ONLY - does NOT bypass the true-peak
    // limiter; see GlobalParams::softLimit). stepCount = 1 toggle, default on.
    parameters.addParameter(
        STR16("Soft Limit"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSoftLimitId);

    // Seed (16 curated constants, spec C-10; default index 0 == 1u, the Phase 8
    // engine/reverb seed) -> Vst::StringListParameter, stepCount 15.
    // Registered from the SINGLE kSeedLabels table through the pointer+count
    // overload (plugins/shared/src/ui/parameter_helpers.h:118) so the label list
    // cannot exist in two places and drift (FR-015/FR-016).
    addDropdownParam(parameters, STR16("Seed"), kSeedId, /*defaultIndex=*/0,
                     kSeedLabels.data(), static_cast<int>(kSeedLabels.size()));
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatGlobalParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kMasterGainId: {
            const float gain = static_cast<float>(value * 2.0);
            const float dB = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -80.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(dB));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kSoftLimitId: {
            UString(string, 128).fromAscii((value >= 0.5) ? "On" : "Off");
            return kResultOk;
        }
        // kPolyphonyId and kSeedId are StringListParameters and format
        // themselves - FR-061 forbids claiming them here.
        default:
            break;
    }
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence - 12 bytes (float + int32 + int32)
// ==============================================================================
// THE SHAPE OF THIS BLOCK IS FROZEN AT PHASE 8's THREE FIELDS (FR-091a). The
// Phase 9 seed is NOT written here and MUST NOT be: loadGlobalParams is a fixed
// three-field sequential reader with no version parameter, so a v2 reader that
// consumed a fourth field here would eat `dream`'s four bytes as `seed` on every
// version-1 stream, shift the whole [macro] block by one field, and hit EOF
// before `entropy`. That is a shape divergence MID-stream, which FR-093's
// EOF-safety mechanism addresses only for TRUNCATION and cannot detect at all.
// The seed's trio lives at the bottom of this header and is written AFTER
// [macro] (plan section 5.2).
// ==============================================================================

inline void saveGlobalParams(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.masterGain.load(std::memory_order_relaxed));
    streamer.writeInt32(params.polyphony.load(std::memory_order_relaxed));
    streamer.writeInt32(params.softLimit.load(std::memory_order_relaxed) ? 1 : 0);
}

/// EOF-safe: a failed read leaves the atomic at its default and returns false.
inline bool loadGlobalParams(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 1.0f;
    Steinberg::int32 intVal = 0;

    if (!streamer.readFloat(floatVal)) { return false; }
    params.masterGain.store(floatVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) { return false; }
    // clampPolyphony, NOT the raw stream value - see the banner above.
    params.polyphony.store(static_cast<int>(clampPolyphony(intVal)),
                           std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) { return false; }
    params.softLimit.store(intVal != 0, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadGlobalParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal)) {
        setParam(kMasterGainId, static_cast<double>(floatVal) / 2.0);
    }
    if (streamer.readInt32(intVal)) {
        setParam(kPolyphonyId,
                 (static_cast<double>(clampPolyphony(intVal)) - 1.0) / 15.0);
    }
    if (streamer.readInt32(intVal)) {
        setParam(kSoftLimitId, intVal != 0 ? 1.0 : 0.0);
    }
}

// ==============================================================================
// Seed State Persistence - 4 bytes, POSITIONED AFTER [macro] (FR-091a)
// ==============================================================================
// A SEPARATE trio, not a fourth field in the block above: see that block's
// banner for why. The caller writes/reads it immediately after the [macro]
// block, which makes the 36-byte version-1 stream a STRICT PREFIX of version 2
// (plan section 5.1) - so a v1 stream loads correctly and the seed simply stays
// at its registered default of index 0.
//
// Neither loadGlobalParams nor loadGlobalParamsToController gains a version
// parameter: with this placement neither needs one, and a version-aware
// fixed-sequence reader is exactly the failure mode C-8 records.
// ==============================================================================

inline void saveGlobalSeed(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.seedIndex.load(std::memory_order_relaxed)));
}

/// EOF-safe: a failed read leaves seedIndex at its default (0) and returns
/// false, which is not an error - it is exactly what a version-1 stream does.
inline bool loadGlobalSeed(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0;

    if (!streamer.readInt32(intVal)) { return false; }
    // Clamped, for the same reason clampPolyphony exists: an out-of-range index
    // from a corrupt stream must not reach kSeedValues[] or FR-042's detector.
    params.seedIndex.store(std::clamp(static_cast<int>(intVal), 0, 15),
                           std::memory_order_relaxed);

    return true;
}

template <typename SetParamFunc>
inline void loadGlobalSeedToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    Steinberg::int32 intVal = 0;

    if (streamer.readInt32(intVal)) {
        setParam(kSeedId,
                 static_cast<double>(std::clamp(static_cast<int>(intVal), 0, 15)) / 15.0);
    }
}

}  // namespace Seraphis
