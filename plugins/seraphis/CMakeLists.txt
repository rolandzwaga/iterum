# ==============================================================================
# Seraphis VST3 Plugin
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
# Links against shared KrateDSP library (Seraphis DSP = Phases 1-7 of dsp/).

# ==============================================================================
# Version + Generated Files (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_read_version(SERAPHIS)
krate_plugin_configure_generated_files()

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${SERAPHIS_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Shared
    src/plugin_ids.h
    src/version.h

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp

    # Parameter Packs
    src/parameters/global_params.h
    src/parameters/macro_params.h

    # Prepare-time engine/reverb configuration (no DSP)
    src/engine/seraphis_engine_config.h

    # Adapters to the shared library
    src/preset/seraphis_preset_config.h
    src/update/seraphis_update_config.h

    # VST3 SDK Common Sources (for MemoryStream used by preset saving)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)

# Link SDK, VSTGUI, and KrateDSP
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
        KrateDSP
        KratePluginsShared
)

# Include directories
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration (macOS AU targets + Windows .rc)
# ==============================================================================
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG SERAPHIS
    BUNDLE_BASE com.krateaudio.seraphis
    ENTITLEMENTS Seraphis.entitlements
    KIND instrument
)

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build: Copy to VST3 System Folder (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_install_to_system(${PLUGIN_NAME})

# ==============================================================================
# Post-Build: Install Factory Presets to ProgramData
# ==============================================================================
krate_plugin_install_presets(${PLUGIN_NAME})

# ==============================================================================
# Compiler Warnings (see cmake/KratePlugin.cmake)
# ==============================================================================
krate_plugin_set_warnings(${PLUGIN_NAME})

# C4459 (MSVC /W4 only): `dsp/include/krate/dsp/systems/timevar_comb_bank.h:915`
# declares a function-local `constexpr float kPi` that shadows the namespace-scope
# `Krate::DSP::kPi` (`dsp/include/krate/dsp/core/math_constants.h:28`), which
# `continuous_body.h:32` has already pulled in by the time `:42` includes the comb
# bank. Seraphis is the FIRST plugin whose engine reaches `continuous_body.h`, so it
# is the first /W4 target to see a shadow that has been in the header since it
# landed; `KrateDSP`'s own /W4 is PRIVATE and header-only code never compiles under
# it. GCC/Clang do not warn at all here (`-Wshadow` is not in the shared flag set,
# `cmake/KratePlugin.cmake:319`), so this is MSVC-specific noise from a DEFECT THAT
# IS NOT OURS TO FIX IN THIS PHASE: Phase 8's Scope is "No modification of any file
# under dsp/". Suppressed narrowly HERE, on this target only, rather than absorbed
# into an unrelated dsp/ edit. Delete this block when the shadow is removed in dsp/
# (drop the local `kPi` and use the math_constants.h one) in its own change.
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /wd4459)
endif()

# ==============================================================================
# Plugin Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
