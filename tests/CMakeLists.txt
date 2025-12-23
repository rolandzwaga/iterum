# ==============================================================================
# Test Configuration
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - DSP algorithms MUST be pure functions testable without VST infrastructure
# - Unit tests MUST cover all DSP algorithms
# - Tests MUST run in CI/CD pipeline
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Fetch Testing Frameworks
# ==============================================================================
# Using Catch2 v3 for modern C++ testing
# Using ApprovalTests.cpp for golden master / approval testing

include(FetchContent)

# Ensure consistent runtime library with Catch2
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Catch2 - Unit testing framework
# Using URL download instead of git clone for CI reliability
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=f6d4f8d78a9b59ec72a81d49f58d18eb317372ac07f8d9432710a079e69fd66a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# ApprovalTests.cpp - Golden master / approval testing
# Note: Hash may need updating if download fails
FetchContent_Declare(
    ApprovalTests
    URL https://github.com/approvals/ApprovalTests.cpp/archive/refs/tags/v.10.13.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(Catch2 ApprovalTests)

# ==============================================================================
# Test Helper Library
# ==============================================================================
# Shared utilities for DSP testing (signal generators, comparators, etc.)

add_library(test_helpers INTERFACE)

target_include_directories(test_helpers
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}  # For test_helpers/xxx.h includes
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_helpers
    INTERFACE
        Catch2::Catch2
)

# ==============================================================================
# DSP Unit Tests
# ==============================================================================
# Constitution Principle VIII: DSP algorithms must be independently testable

add_executable(dsp_tests
    unit/test_dsp_utils.cpp
    unit/core/db_utils_test.cpp
    unit/core/window_functions_test.cpp
    unit/primitives/delay_line_test.cpp
    unit/primitives/lfo_test.cpp
    unit/primitives/biquad_test.cpp
    unit/primitives/smoother_test.cpp
    unit/primitives/oversampler_test.cpp
    unit/primitives/fft_test.cpp
    unit/primitives/spectral_buffer_test.cpp
    unit/primitives/stft_test.cpp
)

target_include_directories(dsp_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dsp_tests
    PRIVATE
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_tests PRIVATE cxx_std_20)

# ==============================================================================
# Disable -ffast-math for files that require IEEE 754 compliant NaN handling
# ==============================================================================
# The VST3 SDK enables -ffast-math globally (see SMTG_PlatformToolset.cmake)
# which causes isnan() and __builtin_isnan() to be optimized out.
# We must disable it for files using db_utils.h NaN detection.
# This follows the same pattern used by VSTGUI (see uijsonpersistence.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/test_dsp_utils.cpp
        unit/core/db_utils_test.cpp
        unit/core/window_functions_test.cpp
        unit/primitives/smoother_test.cpp
        unit/primitives/oversampler_test.cpp
        unit/primitives/delay_line_test.cpp
        unit/primitives/lfo_test.cpp
        unit/primitives/biquad_test.cpp
        unit/primitives/fft_test.cpp
        unit/primitives/spectral_buffer_test.cpp
        unit/primitives/stft_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# ==============================================================================
# Approval Tests (Golden Master / Regression)
# ==============================================================================
# For verifying DSP algorithm outputs against approved references

add_executable(approval_tests
    regression/test_approvals_main.cpp
)

target_include_directories(approval_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(approval_tests
    PRIVATE
        Catch2::Catch2
        ApprovalTests::ApprovalTests
        test_helpers
)

target_compile_features(approval_tests PRIVATE cxx_std_20)

# ==============================================================================
# CTest Integration
# ==============================================================================

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(dsp_tests)
catch_discover_tests(approval_tests)

# ==============================================================================
# VST3 Validator Integration
# ==============================================================================
# The vstvalidator tool checks VST3 plugin conformity
# It's built as part of SMTG_ENABLE_VST3_HOSTING_EXAMPLES

# Custom target to validate the built plugin
if(TARGET validator)
    # Path to the built plugin
    set(PLUGIN_PATH "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/Iterum.vst3")

    add_custom_target(validate_plugin
        COMMAND $<TARGET_FILE:validator> "${PLUGIN_PATH}"
        DEPENDS Iterum validator
        COMMENT "Running VST3 validator on Iterum.vst3"
        VERBATIM
    )

    # Also add a quick validation (basic tests only)
    add_custom_target(validate_plugin_quick
        COMMAND $<TARGET_FILE:validator> "${PLUGIN_PATH}" -q
        DEPENDS Iterum validator
        COMMENT "Running quick VST3 validation on Iterum.vst3"
        VERBATIM
    )
endif()

# ==============================================================================
# Integration Tests (Optional - requires built plugin)
# ==============================================================================
# These tests verify the plugin loads correctly in a test host
# Uncomment when ready to add integration tests

# add_executable(integration_tests
#     integration/test_plugin_load.cpp
# )
# target_link_libraries(integration_tests PRIVATE Catch2::Catch2WithMain sdk)
