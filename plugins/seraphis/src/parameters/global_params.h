#pragma once

// ==============================================================================
// Seraphis - Global Parameters (ID 0-99)   FR-040, FR-043, FR-044, FR-048
// ==============================================================================
// The Ruinae six-function contract (plugins/ruinae/src/parameters/global_params.h:
// 39, 87, 130, 178, 187, 220), reduced to Phase 8's three globals, plus ONE
// deliberate divergence: clampPolyphony() (see below).
// ==============================================================================

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

    // Polyphony (1-16, default 8 => index 7) -> Vst::StringListParameter
    parameters.addParameter(Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/7,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4"),
         STR16("5"), STR16("6"), STR16("7"), STR16("8"),
         STR16("9"), STR16("10"), STR16("11"), STR16("12"),
         STR16("13"), STR16("14"), STR16("15"), STR16("16")}));

    // Soft Limit (tape-saturation amount ONLY - does NOT bypass the true-peak
    // limiter; see GlobalParams::softLimit). stepCount = 1 toggle, default on.
    parameters.addParameter(
        STR16("Soft Limit"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSoftLimitId);
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
        // kPolyphonyId is a StringListParameter and formats itself.
        default:
            break;
    }
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence - 12 bytes (float + int32 + int32)
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

}  // namespace Seraphis
