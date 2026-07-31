// ==============================================================================
// VSTGUI Test Stubs
// ==============================================================================
// Provides the stub for GetPluginFactory() which is normally defined in the
// plugin's entry.cpp. Test executables have no real plugin entry point.
// All other symbols (ModuleInitializer, etc.) are resolved from libsdk.a by
// placing `sdk` AFTER `vstgui_support` in the link order.
// ==============================================================================

#include "pluginterfaces/base/ipluginbase.h"

Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }
