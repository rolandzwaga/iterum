#pragma once

// ==============================================================================
// Plugin Identifiers
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
// Generate new GUIDs for your plugin at: https://www.guidgenerator.com/
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Iterum {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0x12345678, 0x12345678, 0x12345678, 0x12345678);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0x87654321, 0x87654321, 0x87654321, 0x87654321);

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    kBypassId = 0,
    kGainId = 1,
    // Add more parameters here
    kNumParameters
};

// ==============================================================================
// Plugin Metadata
// ==============================================================================

constexpr const char* kPluginName = "Iterum";
constexpr const char* kVendorName = "Your Company";
constexpr const char* kVendorURL = "https://yourcompany.com";
constexpr const char* kVendorEmail = "info@yourcompany.com";
constexpr const char* kVersionString = "1.0.0";

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// Examples: "Fx", "Instrument", "Analyzer", "Delay", "Reverb", etc.
constexpr const char* kSubCategories = "Fx";

} // namespace Iterum
