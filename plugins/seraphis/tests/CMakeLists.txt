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

    # Seraphis Phase 10 (specs/seraphis-phase10-effects): effects chain + v3
    # state. Same rule as Phase 9 above - this list is ENUMERATED, not globbed,
    # so an unregistered TU silently drops out of the build and its cases never
    # run (Catch2 reports "No tests ran" rather than a failure).
    unit/state_v3_test.cpp
    integration/effects_chain_test.cpp
    integration/effects_perf_test.cpp

    # Seraphis Phase 11 (specs/seraphis-phase11-ui): the organism-first editor.
    # Same rule as Phases 9 and 10 above - this list is ENUMERATED, not globbed,
    # so an unregistered TU silently drops out of the build and its cases never
    # run (Catch2 reports "No tests ran" rather than a failure).
    #
    # T027 audit: all five Phase 11 TUs are present here EXACTLY ONCE. Verify this
    # list by the rebuilt `M test cases` count against T001's baseline, never by
    # the exit code - an unregistered TU exits 0.
    integration/cloud_frame_test.cpp
    unit/controller/custom_view_test.cpp
    integration/partial_edit_test.cpp
    integration/ui_negative_control_test.cpp
    # ui_perf_test.cpp carries SC-011's RT-safety arms now and the [.perf] CPU
    # arms (SC-009/SC-010) later, so it is created BEFORE any baseline is pinned
    # and must NOT appear in the -fno-fast-math block below - see the note there.
    integration/ui_perf_test.cpp

    # Seraphis Phase 12 (specs/seraphis-phase12-presets-release): the factory
    # preset library + release readiness. Same rule as Phases 9, 10 and 11 above
    # - this list is ENUMERATED, not globbed, so an unregistered TU silently
    # drops out of the build and its cases never run (Catch2 reports "No tests
    # ran" rather than a failure, and the exit code stays 0).
    unit/preset/factory_preset_test.cpp
    integration/preset_render_sweep_test.cpp

    # SECOND compilation of every plugin .cpp (needed by the plugin-shell tests)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    # Seraphis Phase 11 custom views. The PLUGIN source list carries these too -
    # that is T027's audit; this entry is what makes the view's cases link and
    # run inside seraphis_tests. T027 audit: all three .cpp files appear here and
    # in plugins/seraphis/CMakeLists.txt, exactly once in each. A new plugin .cpp
    # that is missing HERE fails as an unresolved external from controller.obj
    # when seraphis_tests links; one missing from the PLUGIN list fails the same
    # way when Seraphis.vst3 links.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/cloud_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/drawer_container.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/edit_sub_controller.cpp

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
#
# SC-011's "locks and exceptions" clause is instrumented in
# integration/effects_perf_test.cpp by scanning the Phase 10 audio-thread source
# set for lock and throw primitives. That scan needs the two roots those sources
# live under, so they are handed to the TU the same way the resources path is.
target_compile_definitions(seraphis_tests
    PRIVATE
        SERAPHIS_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
        SERAPHIS_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src"
        KRATE_DSP_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/dsp/include"
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
        # Seraphis Phase 10: effects_chain_test checks non-finite payloads by
        # bit pattern and measures per-sample step statistics that fast-math
        # contraction would reshape; state_v3_test round-trips raw floats.
        # ONLY these two of the three Phase 10 TUs are listed.
        # integration/effects_perf_test.cpp must stay out for the same reason
        # param_perf_test.cpp does: -fno-fast-math would move the figures its
        # baselines are pinned to.
        integration/effects_chain_test.cpp
        unit/state_v3_test.cpp
        # Seraphis Phase 11: integration/cloud_frame_test.cpp and
        # unit/controller/custom_view_test.cpp are DELIBERATELY ABSENT. They
        # inject no non-finite payloads and measure no per-sample step statistic
        # - every number they assert is an integer count, a comparison of two
        # reads of the same live DSP/UI state, or (SC-008) a RELATIVE 1e-5
        # comparison of two frame sequences the SAME compiled path produced in
        # the same process, which fast-math contraction reshapes identically on
        # both sides. integration/ui_perf_test.cpp is ABSENT for the same reason
        # param_perf_test.cpp and effects_perf_test.cpp are: it carries the
        # phase's [.perf] CPU arms, and -fno-fast-math would move the figures
        # their baselines are pinned to. Its RT-safety arms assert integer counts
        # only and inject no non-finite value. Only
        # partial_edit_test.cpp and ui_negative_control_test.cpp get the flag in
        # this phase (plan section 10.3). T027 audit: EXACTLY those two Phase 11
        # entries are present below, and cloud_frame_test.cpp / ui_perf_test.cpp /
        # custom_view_test.cpp are confirmed absent.
        #
        # partial_edit_test.cpp DOES get it: SC-018 fuzzes Processor::notify with
        # bit-pattern NaN/Inf EditMessage payloads and asserts on the rejection,
        # and SC-029 measures a per-chunk step statistic that fast-math
        # contraction would reshape.
        integration/partial_edit_test.cpp
        # ui_negative_control_test.cpp DOES get it: SC-001 compares two renders
        # per-sample at EXACTLY 0.0f, and fast-math contraction is free to fuse
        # the two arms' arithmetic differently once anything around the call site
        # changes.
        integration/ui_negative_control_test.cpp
        # Seraphis Phase 12: FR-009 checks stored floats for non-finiteness by
        # bit pattern.
        unit/preset/factory_preset_test.cpp
        # preset_render_sweep_test.cpp DOES get it too: bit-pattern non-finite
        # checks over every rendered sample and per-second RMS statistics.
        integration/preset_render_sweep_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration (FR-065)
catch_discover_tests(seraphis_tests REPORTER console)
