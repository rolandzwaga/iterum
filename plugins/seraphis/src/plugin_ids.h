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
/// other, so the constant lives here.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

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
/// FR-013. Reserved ranges (roadmap 383-386):
///   0-99      Global            (Phase 8 - SHIPPED)
///   100-199   Macros            (Phase 8 - SHIPPED, inert)
///   200-399   Harmonic Cloud    (Phase 9)
///   400-599   Spectral Morph / Entropy (Phase 9)
///   600-799   Life Modulators   (Phase 9)
///   800-999   Continuous Body   (Phase 9)
///   1000-1199 Atmosphere        (Phase 9)
///   1200-1399 Aether            (Phase 9)
///   1400+     Effects           (Phase 10)
// ==============================================================================
enum ParameterIDs : Steinberg::Vst::ParamID {
    // --- Global (0-99) ---
    kMasterGainId = 0,
    kPolyphonyId  = 1,
    kSoftLimitId  = 2,

    // --- Macros (100-199) ---
    kMacroDreamId    = 100,
    kMacroBloomId    = 101,
    kMacroDissolveId = 102,
    kMacroGravityId  = 103,
    kMacroEntropyId  = 104,
};

/// FR-048. REGISTERED TYPES ARE FROZEN FOR THE LIFE OF THE PLUGIN:
///   kMasterGainId, kSoftLimitId, kMacro*Id  -> plain Steinberg::Vst::Parameter  (7)
///   kPolyphonyId                            -> Steinberg::Vst::StringListParameter (1)
/// StringListParameter is what createDropdownParameterWithDefault returns
/// (plugins/shared/src/ui/parameter_helpers.h:47). NEVER swap a type at an ID:
/// DAWs cache parameter metadata and the editor fails to load.

/// Range-dispatch bounds used by processParameterChanges (FR-042).
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;  // IDs < 100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd  = 200;  // IDs < 200 -> macro pack

}  // namespace Seraphis
