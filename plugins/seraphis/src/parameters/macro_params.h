#pragma once

// ==============================================================================
// Seraphis - Macro Parameters (ID 100-199)   FR-041
// ==============================================================================
// INERT IN PHASE 8. No file in Phase 8 may read MacroParams and write it into
// SeraphisMacroMatrix::setMacro / setMacros (seraphis_macro_matrix.h:554, :599).
// The matrix is driven only by its own constructed defaults. This is verified as
// a NEGATIVE CONTROL (SC-023) and is exactly what Phase 9 inverts.
// ==============================================================================

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// Parameter Storage
// ==============================================================================

/// FR-041. The initializers are LOAD-BEARING: value-initialization would leave
/// `gravity` at 0.0f, contradicting BOTH the registered default AND
/// SeraphisMacroValues::gravity = 0.5f (seraphis_macro_matrix.h:126). Gravity is
/// bipolar around 0.5; the rest are neutral at 0. Caught by SC-010's
/// default-state clause (gravity == 0.5f at stream offset 28).
struct MacroParams {
    std::atomic<float> dream{0.0f};
    std::atomic<float> bloom{0.0f};
    std::atomic<float> dissolve{0.0f};
    std::atomic<float> gravity{0.5f};
    std::atomic<float> entropy{0.0f};
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleMacroParamChange(
    MacroParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    const float clamped = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

    switch (id) {
        case kMacroDreamId:    params.dream.store(clamped, std::memory_order_relaxed);    break;
        case kMacroBloomId:    params.bloom.store(clamped, std::memory_order_relaxed);    break;
        case kMacroDissolveId: params.dissolve.store(clamped, std::memory_order_relaxed); break;
        case kMacroGravityId:  params.gravity.store(clamped, std::memory_order_relaxed);  break;
        case kMacroEntropyId:  params.entropy.store(clamped, std::memory_order_relaxed);  break;
        default: break;
    }
}

// ==============================================================================
// Parameter Registration (FR-048 - five plain Vst::Parameters, FROZEN)
// ==============================================================================

inline void registerMacroParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Defaults are exactly SeraphisMacroValues (seraphis_macro_matrix.h:122-128).
    parameters.addParameter(STR16("Dream"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate, kMacroDreamId);
    parameters.addParameter(STR16("Bloom"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate, kMacroBloomId);
    parameters.addParameter(STR16("Dissolve"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate, kMacroDissolveId);
    parameters.addParameter(STR16("Gravity"), STR16("%"), 0, 0.5,
                            ParameterInfo::kCanAutomate, kMacroGravityId);
    parameters.addParameter(STR16("Entropy"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate, kMacroEntropyId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatMacroParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    if (id >= kMacroDreamId && id <= kMacroEntropyId) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 20 bytes (five floats)
// ==============================================================================

inline void saveMacroParams(const MacroParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.dream.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bloom.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dissolve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gravity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.entropy.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read leaves the atomic at its default and returns false.
inline bool loadMacroParams(MacroParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;

    if (!streamer.readFloat(fv)) { return false; }
    params.dream.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.bloom.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.dissolve.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.gravity.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.entropy.store(fv, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (macros are stored normalized - identity mapping)
// ==============================================================================

template <typename SetParamFunc>
inline void loadMacroParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float fv = 0.0f;

    if (streamer.readFloat(fv)) { setParam(kMacroDreamId, static_cast<double>(fv)); }
    if (streamer.readFloat(fv)) { setParam(kMacroBloomId, static_cast<double>(fv)); }
    if (streamer.readFloat(fv)) { setParam(kMacroDissolveId, static_cast<double>(fv)); }
    if (streamer.readFloat(fv)) { setParam(kMacroGravityId, static_cast<double>(fv)); }
    if (streamer.readFloat(fv)) { setParam(kMacroEntropyId, static_cast<double>(fv)); }
}

}  // namespace Seraphis
