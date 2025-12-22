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
// ==============================================================================

#include "plugin_ids.h"
#include "version.h"
#include "processor/processor.h"
#include "controller/controller.h"

#include "public.sdk/source/main/pluginfactory.h"

// ==============================================================================
// Plugin Factory Definition
// ==============================================================================

#define stringPluginName "Iterum"

BEGIN_FACTORY_DEF(
    stringCompanyName,      // Vendor name
    stringVendorURL,        // Vendor URL (defined in version.h as empty)
    stringVendorEmail       // Vendor email (defined in version.h as empty)
)

    // ==========================================================================
    // Processor Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Iterum::kProcessorUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstAudioEffectClass,                 // component category
        stringPluginName,                     // plugin name
        Steinberg::Vst::kDistributable,       // Constitution: enable separation
        Iterum::kSubCategories,              // subcategories
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Iterum::Processor::createInstance    // factory function
    )

    // ==========================================================================
    // Controller Component Registration
    // ==========================================================================
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Iterum::kControllerUID),
        PClassInfo::kManyInstances,           // cardinality
        kVstComponentControllerClass,         // component category
        stringPluginName "Controller",        // controller name
        0,                                    // unused for controller
        "",                                   // unused for controller
        FULL_VERSION_STR,                     // version
        kVstVersionString,                    // SDK version
        Iterum::Controller::createInstance   // factory function
    )

END_FACTORY

// ==============================================================================
// Module Entry/Exit (Platform Specific)
// ==============================================================================
// The VST3 SDK handles module initialization via macros in pluginfactory.h
// No additional code needed here for most cases.
// ==============================================================================
