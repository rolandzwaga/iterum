// ==============================================================================
// Plugin Entry Point
// ==============================================================================
// This file contains the plugin factory that the host uses to create
// processor and controller instances.
//
// Constitution Principle I: VST3 Architecture Separation
// - Processor and Controller are registered as separate classes
// - Each has its own unique FUID
// - Host can instantiate them independently
//
// NOTE: as of Phase 11 (FR-052) this file DOES include ui/*.h headers, and that
// is the only reason the custom views exist at runtime. A ViewCreatorAdapter
// registers itself from the constructor of an inline global
// (plugins/shared/src/ui/arc_knob.h:714-716), and an inline global only runs its
// constructor in a translation unit that is actually LINKED - so the entry TU is
// where every creator Seraphis uses must be pulled in:
//   <ui/arc_knob.h>          registers "ArcKnob"       (shared drawer knobs)
//   "ui/macro_ring_knob.h"   registers "MacroRingKnob" (the five macro rings)
// The Phase 8 prohibition this replaces ("MUST NOT include any ui/*.h header ...
// no custom views until Phase 11") has expired with the phase that wrote it.
//
// ui/toggle_button.h is DELIBERATELY ABSENT: the freeze cluster and every drawer
// toggle are stock CCheckBox views (FR-025's four permitted classes are ArcKnob,
// CSlider, COptionMenu, CCheckBox), so registering ToggleButton's creator would
// be dead weight and would leave the intent ambiguous for the next reader.
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

#include <ui/arc_knob.h>
#include "ui/macro_ring_knob.h"

#include "public.sdk/source/main/pluginfactory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================
// An IDENTICAL redefinition of the macro cmake/version.h.in generates from
// version.json's "name" -- so it is warning-free. Keep the spellings identical.

#define stringPluginName "Seraphis"

BEGIN_FACTORY_DEF(
    stringCompanyName,      // Vendor name
    stringVendorURL,        // Vendor URL (defined in version.h)
    stringVendorEmail       // Vendor email (defined in version.h)
)

    // ==========================================================================
    // Processor Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seraphis::kProcessorUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstAudioEffectClass,                 // component category
        stringPluginName,                     // plugin name
        Steinberg::Vst::kDistributable,       // Constitution: enable separation
        Seraphis::kSubCategories,             // subcategories
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Seraphis::Processor::createInstance   // factory function
    )

    // ==========================================================================
    // Controller Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seraphis::kControllerUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstComponentControllerClass,         // component category
        stringPluginName "Controller",        // controller name
        0,                                    // unused for controller
        "",                                   // unused for controller
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Seraphis::Controller::createInstance  // factory function
    )

END_FACTORY
