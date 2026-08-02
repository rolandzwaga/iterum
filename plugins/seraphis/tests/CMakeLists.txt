# ==============================================================================
# Seraphis Plugin Tests
# ==============================================================================

add_executable(seraphis_tests
    unit/test_main.cpp
    unit/processor_bus_test.cpp
    unit/param_denorm_test.cpp
    unit/state_roundtrip_test.cpp
    unit/midi_event_test.cpp
    unit/lifecycle_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/processor_audio_test.cpp
    integration/param_flow_test.cpp

    # Seraphis Phase 9 (specs/seraphis-phase9-parameters): full parameter
    # surface + v2 state. This list is ENUMERATED, not globbed - an unregistered
    # TU silently drops out of the build and its cases never run.
    unit/parameter_surface_test.cpp
    unit/state_v2_test.cpp
    unit/morph_sync_test.cpp
    integration/param_reach_test.cpp
    integration/param_cadence_test.cpp
    integration/param_continuity_test.cpp
    integration/macro_wiring_test.cpp
    integration/param_character_test.cpp
    integration/param_perf_test.cpp

    # SECOND compilation of every plugin .cpp (needed by the plugin-shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp

    # VST3 SDK sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp

    # SDK hosting helpers (HostApplication, HostMessage, HostAttributeList)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp

    # Stubs for GetPluginFactory/moduleHandle + SDK module init (Linux linker)
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
)

# `sdk` AFTER `vstgui_support` — see the comment in vstgui_test_stubs.cpp.
target_link_libraries(seraphis_tests
    PRIVATE
        KrateDSP
        KratePluginsShared
        Catch2::Catch2
        test_helpers
        vstgui_support
        sdk
)

target_include_directories(seraphis_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/tools
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
)

target_compile_features(seraphis_tests PRIVATE cxx_std_20)

# FR-063: tests locate resources/editor.uidesc through this definition.
target_compile_definitions(seraphis_tests
    PRIVATE
        SERAPHIS_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
)

# FR-064: disable -ffast-math for IEEE 754 compliant NaN/Inf handling.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/lifecycle_test.cpp
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        # Seraphis Phase 9: SC-012 injects non-finite payloads in state_v2_test
        # and SC-005's per-sample step statistic in param_continuity_test must
        # not be reshaped by fast-math contraction. ONLY these two of the nine
        # Phase 9 TUs are listed; the other seven must NOT be.
        # integration/param_perf_test.cpp in particular must stay out:
        # -fno-fast-math would move the figures its baselines are pinned to
        # (same rule as dsp/tests/CMakeLists.txt:735-740).
        integration/param_continuity_test.cpp
        unit/state_v2_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration (FR-065)
catch_discover_tests(seraphis_tests REPORTER console)
