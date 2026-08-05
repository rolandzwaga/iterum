# ==============================================================================
# Shared Plugin Tests
# ==============================================================================
# Tests for shared UI components (ColorUtils, StepPatternEditor, etc.)
# ==============================================================================

add_executable(shared_tests
    test_main.cpp
    test_color_utils.cpp
    test_step_pattern_editor.cpp
    test_arp_lane_editor.cpp
    test_arp_lane_container.cpp
    test_arp_lane_header.cpp
    test_arp_modifier_lane.cpp
    test_arp_condition_lane.cpp
    test_playhead_trail.cpp
    test_transform.cpp
    test_copy_paste.cpp
    test_euclidean_dot_display.cpp
    test_color_scheme.cpp
    test_toggle_button.cpp
    test_xy_morph_pad.cpp
    test_adsr_display.cpp
    test_oscillator_type_selector.cpp
    test_lfo_shape_selector.cpp
    test_midi_event_dispatcher.cpp
    test_version_compare.cpp
    test_update_checker.cpp
    test_shared_display_bridge.cpp

    # Seraphis Phase 12 (T013 / FR-028a). This TU is the SINGLE owner of the
    # global allocation-operator replacements in shared_tests (it includes
    # <allocation_operator_overrides.h>); no other file in this list may include
    # that header, or the binary fails to link on duplicate symbols.
    test_allocation_detector.cpp

    # Stubs for GetPluginFactory and moduleHandle (needed by vstgui_support on Linux)
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
)

target_link_libraries(shared_tests
    PRIVATE
        KratePluginsShared
        KrateDSP
        Catch2::Catch2
        test_helpers
        sdk
        vstgui_support
        nlohmann_json::nlohmann_json
)

target_include_directories(shared_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/tests
        ${vst3sdk_SOURCE_DIR}
)

target_compile_features(shared_tests PRIVATE cxx_std_20)

# Compiler warnings
if(MSVC)
    target_compile_options(shared_tests PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(shared_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# IEEE 754 compliance: test_oscillator_type_selector.cpp uses std::isnan/std::isinf
# which are broken by -ffast-math (VST3 SDK enables it globally)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        test_oscillator_type_selector.cpp
        test_lfo_shape_selector.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# CTest Integration
catch_discover_tests(shared_tests REPORTER console)
