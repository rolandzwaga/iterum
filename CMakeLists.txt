cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Krate Audio Monorepo
# ==============================================================================
# Constitution Principle VII: Modern CMake with target-based configuration
#
# Structure:
#   dsp/              - KrateDSP shared library (Layers 0-4)
#   plugins/iterum/   - Iterum VST3 plugin
#   extern/vst3sdk/   - VST3 SDK (shared by all plugins)
#   tools/            - Shared development tools
# ==============================================================================

project(KrateAudio
    VERSION 0.8.0
    DESCRIPTION "Krate Audio Plugin Suite"
    LANGUAGES C CXX
)

# C++ Standard (Constitution Principle III: C++17 minimum, C++20 preferred)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for VS Code IntelliSense and clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Clang-Tidy Integration (Optional)
# ==============================================================================
# Static analysis for code quality. Not enabled by default to avoid slowing builds.
#
# Usage:
#   - Standalone (recommended): ./tools/run-clang-tidy.ps1 or ./tools/run-clang-tidy.sh
#   - CMake integration: cmake -DENABLE_CLANG_TIDY=ON ... (runs on every compile)
#
# NOTE: CMake integration works best with Ninja generator, not Visual Studio.
# For MSVC/Visual Studio generator, use the standalone scripts instead.
# ==============================================================================
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis during build" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy" DOC "Path to clang-tidy executable")
    if(CLANG_TIDY_EXE)
        message(STATUS "[KRATE] Clang-Tidy ENABLED: ${CLANG_TIDY_EXE}")

        # Configure clang-tidy command
        # --config-file: Use project .clang-tidy file
        # --header-filter: Only check our headers, not external deps
        set(CLANG_TIDY_COMMAND
            "${CLANG_TIDY_EXE}"
            "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
        )

        # MSVC-specific workarounds
        if(MSVC)
            list(APPEND CLANG_TIDY_COMMAND
                "--extra-arg=/EHsc"
                "--extra-arg=/std:c++20"
            )
        endif()

        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
    else()
        message(WARNING "[KRATE] ENABLE_CLANG_TIDY is ON but clang-tidy not found in PATH")
    endif()
endif()

# ==============================================================================
# Compiler Cache (ccache) Support
# ==============================================================================
option(USE_CCACHE "Use ccache for faster rebuilds" OFF)

if(USE_CCACHE)
    include(FetchContent)
    FetchContent_Declare(
        Ccache.cmake
        URL https://github.com/TheLartians/Ccache.cmake/archive/refs/tags/v1.2.5.tar.gz
        URL_HASH SHA256=5ecd99d8f7e2b8191b0481ffbe31822c96b1a0dbc55eda570b31228dbe39ae6f
    )
    FetchContent_MakeAvailable(Ccache.cmake)
endif()

# ==============================================================================
# MSVC PDB Workaround for sccache/ccache
# ==============================================================================
if(MSVC AND (CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER))
    message(STATUS "[KRATE] Using /Z7 instead of /Zi for sccache/ccache compatibility")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

# ==============================================================================
# AddressSanitizer (ASan) Support
# ==============================================================================
# Detects memory errors at runtime:
# - Use-after-free (heap and stack)
# - Heap buffer overflow
# - Stack buffer overflow
# - Memory leaks
#
# Usage:
#   cmake -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
#   cmake --build . --config Debug
#   ctest  # ASan will report errors if memory issues are detected
#
# NOTE: ASan adds ~2x runtime overhead. Use for debugging, not release.
# ==============================================================================
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)

if(ENABLE_ASAN)
    message(STATUS "[KRATE] AddressSanitizer ENABLED - memory errors will be detected at runtime")

    if(MSVC)
        # MSVC ASan support (Visual Studio 2019 16.9+)
        # Reference: https://learn.microsoft.com/en-us/cpp/sanitizers/asan
        add_compile_options(/fsanitize=address)

        # CRITICAL: ASan is INCOMPATIBLE with /RTC1 (Runtime Checks)
        # /RTC1 is enabled by default in Debug builds. We must remove it.
        # Reference: https://devblogs.microsoft.com/cppblog/addresssanitizer-asan-for-windows-with-msvc/
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/RTCs" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REPLACE "/RTCu" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

        # CRITICAL: ASan is INCOMPATIBLE with /ZI (Edit and Continue debug info)
        # Must use /Zi instead. Replace globally in Debug flags.
        string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        # Force these into the cache so they apply to subdirectories (VST SDK)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}" CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" CACHE STRING "" FORCE)

        # Tell the VST SDK to use ASan mode (prevents it from adding /ZI)
        # The SDK uses SMTG_ENABLE_ADDRESS_SANITIZER
        set(SMTG_ENABLE_ADDRESS_SANITIZER ON CACHE BOOL "" FORCE)

        # Disable incremental linking (incompatible with ASan)
        add_link_options(/INCREMENTAL:NO)
        add_link_options(/DEBUG)

        message(STATUS "[KRATE] Using MSVC AddressSanitizer (/fsanitize=address)")
        message(STATUS "[KRATE] Disabled /RTC1 and /ZI (incompatible with ASan)")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # Clang/GCC ASan support
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address)
        message(STATUS "[KRATE] Using Clang/GCC AddressSanitizer (-fsanitize=address)")
    else()
        message(WARNING "[KRATE] AddressSanitizer not supported for this compiler")
    endif()
endif()

# ==============================================================================
# Build-Time Instrumentation (opt-in, OFF by default)
# ==============================================================================
# Measure WHERE compile time goes so agents stop guessing header blast radius by
# grepping include counts. Enable via a timing preset (see CMakePresets.json) or
# -DKRATE_BUILD_TIMING=ON. Analyze the results with:
#     node tools/aggregate-build-times.js --build <build-dir>
#
#   Clang/GCC (Ninja): -ftime-trace writes a Chrome-trace <obj>.json next to every
#     object file; the aggregator ranks the worst headers across all TUs. Pair with
#     ClangBuildAnalyzer for a deeper per-symbol view.
#   MSVC (windows-ninja): /Bt+ prints front-end + back-end timing per TU and
#     /d2cgsummary prints a codegen summary; capture the build stdout to a log and
#     pass it with --msvc-log.
#   Any Ninja build: the aggregator also reads .ninja_log for a zero-config
#     slowest-target baseline even without these flags.
# ==============================================================================
option(KRATE_BUILD_TIMING "Emit compiler build-time instrumentation (-ftime-trace / MSVC /Bt+)" OFF)

if(KRATE_BUILD_TIMING)
    if(MSVC)
        add_compile_options(/Bt+ /d2cgsummary)
        message(STATUS "[KRATE] Build timing ENABLED (MSVC /Bt+ /d2cgsummary) - capture build stdout to a log, then: node tools/aggregate-build-times.js --msvc-log <log>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-ftime-trace)
        message(STATUS "[KRATE] Build timing ENABLED (-ftime-trace) - per-TU <obj>.json traces; then: node tools/aggregate-build-times.js --build ${CMAKE_BINARY_DIR}")
    else()
        message(WARNING "[KRATE] KRATE_BUILD_TIMING not supported for this compiler")
    endif()
endif()

# ==============================================================================
# VST3 SDK Configuration (Shared by all plugins)
# ==============================================================================
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")

if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}\n"
        "Please run: git submodule add https://github.com/steinbergmedia/vst3sdk.git extern/vst3sdk\n"
        "Then run: git submodule update --init --recursive"
    )
endif()

# SDK Options
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CXX_STANDARD "20" CACHE STRING "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)

# Disable clang-tidy for external SDK (restore after add_subdirectory)
set(_SAVED_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)

# Build the AUv3 wrapper library (macOS + Xcode only).
# The SDK only includes this under SMTG_ENABLE_VST3_PLUGIN_EXAMPLES, but our
# plugins need auv3_wrapper_macos without pulling in all the example plugins.
if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV3_BUILDS)
    # The auv3wrapper CMakeLists.txt expects SDK_ROOT and public_sdk_SOURCE_DIR
    # (set internally by the SDK's top-level CMakeLists.txt).
    set(SDK_ROOT "${vst3sdk_SOURCE_DIR}")
    set(public_sdk_SOURCE_DIR "${vst3sdk_SOURCE_DIR}/public.sdk")
    list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
    add_subdirectory(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auv3wrapper
        ${PROJECT_BINARY_DIR}/vst3sdk_auv3wrapper
    )
endif()

# Restore clang-tidy for our code
set(CMAKE_CXX_CLANG_TIDY "${_SAVED_CLANG_TIDY}")
smtg_enable_vst3_sdk()

# Suppress compiler warnings from VST3 SDK and VSTGUI headers.
# The SDK sets PUBLIC (non-SYSTEM) include directories, which causes GCC/Clang
# to emit warnings for third-party code we can't modify. Re-adding them as
# SYSTEM tells the compiler to treat those headers like system headers.
function(smtg_mark_includes_as_system target)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_inc ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_inc)
        set_target_properties(${target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
        target_include_directories(${target} SYSTEM INTERFACE ${_inc})
    endif()
endfunction()

smtg_mark_includes_as_system(sdk_common)
smtg_mark_includes_as_system(sdk)
smtg_mark_includes_as_system(sdk_hosting)
smtg_mark_includes_as_system(base)
smtg_mark_includes_as_system(pluginterfaces)
smtg_mark_includes_as_system(vstgui)
smtg_mark_includes_as_system(vstgui_uidescription)
smtg_mark_includes_as_system(vstgui_support)

# Suppress warnings from VST3 SDK / VSTGUI source file compilations on GCC/Clang.
# These are third-party sources we cannot modify.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wformat: int64 format string mismatch (long int vs long long int) in fstring.cpp, ustring.cpp
    target_compile_options(base PRIVATE -Wno-format)
    target_compile_options(pluginterfaces PRIVATE -Wno-format)
    # -Wsuggest-override: missing override in cscrollview.cpp
    # -Wcpp: #warning TODO directives in cairobitmap.cpp, x11frame.cpp
    # -Wdeprecated-declarations: newer libc++ (Xcode 26.5 / macOS 26 SDK) marks
    #   std::wstring_convert / std::codecvt_utf8_utf16 (deprecated since C++17)
    #   as deprecated; VSTGUI's generictextedit.cpp still uses them and builds
    #   with -Werror, so the deprecation became a hard error. We cannot modify
    #   the SDK source, so don't treat the deprecation as an error here.
    if(TARGET vstgui)
        target_compile_options(vstgui PRIVATE
            -Wno-suggest-override -Wno-cpp -Wno-deprecated-declarations)
    endif()
    if(TARGET vstgui_uidescription)
        target_compile_options(vstgui_uidescription PRIVATE -Wno-deprecated-declarations)
    endif()
    if(TARGET vstgui_support)
        target_compile_options(vstgui_support PRIVATE -Wno-deprecated-declarations)
    endif()
    # -Wsuggest-override passed to C compiler for wayland protocol .c files
    if(TARGET vstgui_wayland_protocols)
        target_compile_options(vstgui_wayland_protocols PRIVATE -Wno-suggest-override)
    endif()
endif()

# ==============================================================================
# Testing Configuration (Constitution Principle VIII)
# ==============================================================================
option(VSTWORK_BUILD_TESTS "Build unit tests" ON)

if(VSTWORK_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 for testing
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    # Fetch ApprovalTests for approval testing
    FetchContent_Declare(
        ApprovalTests
        GIT_REPOSITORY https://github.com/approvals/ApprovalTests.cpp.git
        GIT_TAG v.10.13.0
    )
    FetchContent_MakeAvailable(ApprovalTests)

    # Test helpers library (shared by all tests)
    add_subdirectory(tests/test_helpers)
endif()

# ==============================================================================
# Google Highway (SIMD-accelerated math for spectral DSP)
# ==============================================================================
# Provides runtime dispatch: SSE2/AVX2/AVX-512 on x86, NEON on ARM.
# Single binary uses the best available ISA at load time.
include(FetchContent)
FetchContent_Declare(
    highway
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.2.0
)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB ON CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(highway)
# macOS universal binary (x86_64 + arm64) - must match KrateDSP/plugin targets
smtg_target_setup_universal_binary(hwy)
smtg_target_setup_universal_binary(hwy_contrib)

# ==============================================================================
# pffft (SIMD-optimized FFT, marton78 fork, BSD license)
# ==============================================================================
# Auto-detects SIMD: SSE on x86/x64, NEON on ARM, scalar fallback.
FetchContent_Declare(
    pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG v1.1.0
)
set(PFFFT_USE_TYPE_DOUBLE OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PFFFT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# INSTALL_PFFFT must stay ON because the pffft CMakeLists.txt unconditionally
# references the pffft-targets export. Harmless — we never run cmake --install.
set(INSTALL_PFFFT ON CACHE BOOL "" FORCE)
set(INSTALL_PFDSP OFF CACHE BOOL "" FORCE)
set(INSTALL_PFFASTCONV OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pffft)
smtg_target_setup_universal_binary(PFFFT)

# ==============================================================================
# cpp-httplib (header-only HTTP/HTTPS client, MIT license)
# ==============================================================================
# Used for update checking. Requires Mbed TLS for HTTPS support.
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ==============================================================================
# nlohmann/json (header-only JSON library, MIT license)
# ==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ==============================================================================
# Eigen 3.4 (linear algebra, MPL-2 license) -- used by tools/membrum-fit
# ==============================================================================
# Header-only. Imported as an INTERFACE target to skip Eigen's own CMake
# machinery (tests, docs, install rules). Provides Eigen3::Eigen.
option(VSTWORK_BUILD_MEMBRUM_FIT "Build the tools/membrum-fit offline drum-sample fitter" ON)

if(VSTWORK_BUILD_MEMBRUM_FIT)
    FetchContent_Declare(
        Eigen3
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(Eigen3)
    if(NOT eigen3_POPULATED)
        FetchContent_Populate(Eigen3)
    endif()
    if(NOT TARGET Eigen3::Eigen)
        add_library(Eigen3_headeronly INTERFACE)
        target_include_directories(Eigen3_headeronly INTERFACE ${eigen3_SOURCE_DIR})
        add_library(Eigen3::Eigen ALIAS Eigen3_headeronly)
    endif()

    # =====================
    # CLI11 (command-line parser, BSD-3) -- used by tools/membrum-fit
    # =====================
    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.4.2
        GIT_SHALLOW TRUE
    )
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(CLI11_SINGLE_FILE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cli11)

    # =====================
    # NLopt BOBYQA (derivative-free refinement) -- tools/membrum-fit
    # =====================
    # Gated by a cache option so CI / minimal configurations can skip NLopt.
    option(MEMBRUM_FIT_ENABLE_NLOPT "Fetch and link NLopt for BOBYQA refinement" ON)
    if(MEMBRUM_FIT_ENABLE_NLOPT)
        FetchContent_Declare(
            nlopt
            GIT_REPOSITORY https://github.com/stevengj/nlopt.git
            GIT_TAG v2.7.1
            GIT_SHALLOW TRUE
        )
        set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
        set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
        set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
        set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
        set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
        set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
        set(NLOPT_FORTRAN OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        # NLopt v2.7.1 uses cmake_minimum_required(VERSION 2.8.12), which CMake 4.x
        # (shipped on current macOS CI runners) rejects. Scope the compatibility
        # shim to this FetchContent call and restore afterwards so other deps
        # are not affected.
        set(_KA_PREV_POLICY_MIN "${CMAKE_POLICY_VERSION_MINIMUM}")
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        FetchContent_MakeAvailable(nlopt)
        set(CMAKE_POLICY_VERSION_MINIMUM "${_KA_PREV_POLICY_MIN}")
        unset(_KA_PREV_POLICY_MIN)

        # NLopt v2.7.1 generates nlopt.f via a custom command attached as a
        # source to BOTH the `nlopt` library and the `generate-fortran` utility
        # target. Xcode's new build system rejects that unless one is a
        # dependency of the other ("attached to multiple targets" error).
        # Make `nlopt` depend on `generate-fortran` so Xcode sees a valid DAG.
        if(CMAKE_GENERATOR STREQUAL "Xcode" AND TARGET nlopt AND TARGET generate-fortran)
            add_dependencies(nlopt generate-fortran)
        endif()
    endif()
endif()

# ==============================================================================
# KrateDSP Library (Shared by all plugins)
# ==============================================================================
add_subdirectory(dsp)

# ==============================================================================
# Shared Plugin Infrastructure
# ==============================================================================
add_subdirectory(plugins/shared)

# ==============================================================================
# Plugins
# ==============================================================================
# Shared plugin CMake helpers (version parsing, AU targets, install, warnings).
# Defined here so they are visible to every plugin subdirectory added below.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(KratePlugin)

add_subdirectory(plugins/iterum)
add_subdirectory(plugins/disrumpo)
add_subdirectory(plugins/ruinae)
add_subdirectory(plugins/innexus)
add_subdirectory(plugins/gradus)
add_subdirectory(plugins/membrum)
add_subdirectory(plugins/seraphis)

# ==============================================================================
# Tools (Shared development tools)
# ==============================================================================
add_executable(preset_generator
    tools/preset_generator.cpp
)
target_compile_features(preset_generator PRIVATE cxx_std_20)
set_target_properties(preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(disrumpo_preset_generator
    tools/disrumpo_preset_generator.cpp
)
target_compile_features(disrumpo_preset_generator PRIVATE cxx_std_20)
set_target_properties(disrumpo_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Disrumpo factory presets on demand
# Usage: cmake --build build --target generate_disrumpo_presets
add_custom_target(generate_disrumpo_presets
    COMMAND disrumpo_preset_generator "${CMAKE_SOURCE_DIR}/plugins/disrumpo/resources/presets"
    DEPENDS disrumpo_preset_generator
    COMMENT "Generating Disrumpo factory presets (120 presets across 11 categories)"
    VERBATIM
)

add_executable(ruinae_preset_generator
    tools/ruinae_preset_generator.cpp
)
target_compile_features(ruinae_preset_generator PRIVATE cxx_std_20)
set_target_properties(ruinae_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Ruinae factory arp presets on demand
# Usage: cmake --build build --target generate_ruinae_presets
add_custom_target(generate_ruinae_presets
    COMMAND ruinae_preset_generator "${CMAKE_SOURCE_DIR}/plugins/ruinae/resources/presets"
    DEPENDS ruinae_preset_generator
    COMMENT "Generating Ruinae factory arp presets"
    VERBATIM
)

add_executable(innexus_preset_generator
    tools/innexus_preset_generator.cpp
)
target_compile_features(innexus_preset_generator PRIVATE cxx_std_20)
set_target_properties(innexus_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Innexus factory presets on demand
# Usage: cmake --build build --target generate_innexus_presets
add_custom_target(generate_innexus_presets
    COMMAND innexus_preset_generator "${CMAKE_SOURCE_DIR}/plugins/innexus/resources/presets"
    DEPENDS innexus_preset_generator
    COMMENT "Generating Innexus factory presets (35 presets across 7 categories)"
    VERBATIM
)

add_executable(gradus_preset_generator
    tools/gradus_preset_generator.cpp
)
target_compile_features(gradus_preset_generator PRIVATE cxx_std_20)
set_target_properties(gradus_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Gradus factory presets on demand
# Usage: cmake --build build --target generate_gradus_presets
add_custom_target(generate_gradus_presets
    COMMAND gradus_preset_generator "${CMAKE_SOURCE_DIR}/plugins/gradus/resources/presets"
    DEPENDS gradus_preset_generator
    COMMENT "Generating Gradus factory presets (25 presets across 8 categories)"
    VERBATIM
)

add_executable(membrum_preset_generator
    tools/membrum_preset_generator.cpp
)
target_compile_features(membrum_preset_generator PRIVATE cxx_std_20)
set_target_properties(membrum_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target to regenerate Membrum factory kit presets on demand
# Usage: cmake --build build --target generate_membrum_presets
add_custom_target(generate_membrum_presets
    COMMAND membrum_preset_generator "${CMAKE_SOURCE_DIR}/plugins/membrum/resources/presets/Kit Presets"
    DEPENDS membrum_preset_generator
    COMMENT "Generating Membrum factory kit presets (20 kits across 4 subcategories)"
    VERBATIM
)

# ==============================================================================
# gen_v2_fixtures — Spec 142 migration regression fixture generator
# ==============================================================================
# Generates byte-exact v2 state fixtures (.bin) for the Gradus processor and
# golden MIDI captures (.txt) for both Gradus and Ruinae before the spec-142
# kCurrentStateVersion 2->3 / kNumLanes 9->10 changes land. Must be run on the
# pre-feature parent commit; the harness source is checked in so future
# maintainers can re-generate.
#
# Build: cmake --build build --target gen_v2_fixtures
# Run  : build/.../bin/Release/gen_v2_fixtures.exe
#
# Outputs:
#   plugins/gradus/tests/fixtures/gradus_v2_preset_{default,heavy_lanes,midi_delay}.bin
#   plugins/gradus/tests/fixtures/gradus_v2_golden_midi_{default,heavy_lanes,midi_delay}.txt
#   plugins/ruinae/tests/fixtures/ruinae_factory_{preset}_golden_midi.txt (per arp preset)
#
# Marked EXCLUDE_FROM_ALL: not built by default; only when explicitly requested.
# ==============================================================================
add_executable(gen_v2_fixtures EXCLUDE_FROM_ALL
    # Harness orchestration (plugin-agnostic)
    tools/gen_v2_fixtures/main.cpp
    tools/gen_v2_fixtures/common.cpp

    # Per-plugin generators (each lives in its own TU so the Gradus and Ruinae
    # plugin_ids.h enum namespaces — which contain conflicting symbols at the
    # same numeric IDs — never compile together in a single translation unit)
    tools/gen_v2_fixtures/gen_gradus.cpp
    tools/gen_v2_fixtures/gen_ruinae.cpp

    # Gradus processor (audio thread) + parameters
    plugins/gradus/src/processor/processor.cpp

    # Ruinae processor (audio thread)
    plugins/ruinae/src/processor/processor.cpp
    plugins/ruinae/src/processor/processor_state.cpp
    plugins/ruinae/src/processor/processor_params.cpp
    plugins/ruinae/src/processor/processor_midi.cpp
    plugins/ruinae/src/processor/processor_messaging.cpp

    # VST3 SDK helpers (PresetFile + MemoryStream + module init)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstpresetfile.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp

    # GetPluginFactory stub (resolves entry-point symbol referenced by SDK)
    plugins/gradus/tests/vstgui_test_stubs.cpp
)

target_link_libraries(gen_v2_fixtures
    PRIVATE
        KrateDSP
        KratePluginsShared
        sdk
)

# Target-level include directories: common ones used by every TU.
target_include_directories(gen_v2_fixtures
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/gen_v2_fixtures
        ${vst3sdk_SOURCE_DIR}
)

# Per-source include dirs: each plugin's src/ is added ONLY for the TU that
# includes its headers, so the `#include "plugin_ids.h"` in each TU resolves
# to the correct plugin's plugin_ids.h.
set_source_files_properties(
    tools/gen_v2_fixtures/gen_gradus.cpp
    plugins/gradus/src/processor/processor.cpp
    PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/plugins/gradus/src"
)
set_source_files_properties(
    tools/gen_v2_fixtures/gen_ruinae.cpp
    plugins/ruinae/src/processor/processor.cpp
    plugins/ruinae/src/processor/processor_state.cpp
    plugins/ruinae/src/processor/processor_params.cpp
    plugins/ruinae/src/processor/processor_midi.cpp
    plugins/ruinae/src/processor/processor_messaging.cpp
    PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/plugins/ruinae/src"
)

target_compile_features(gen_v2_fixtures PRIVATE cxx_std_20)

set_target_properties(gen_v2_fixtures PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Match gradus_tests warning posture (silence VST SDK headers' /W4 chatter)
if(MSVC)
    target_compile_options(gen_v2_fixtures PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100
        /wd4458
    )
else()
    target_compile_options(gen_v2_fixtures PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ==============================================================================
# Control Testbench (UI Development Tool)
# ==============================================================================
# Standalone application for testing custom VSTGUI controls without loading
# the full plugin in a DAW. Provides fast iteration for UI development.
#
# Build with: cmake --build build --target control_testbench
# ==============================================================================
option(BUILD_CONTROL_TESTBENCH "Build the control testbench tool" ON)

if(BUILD_CONTROL_TESTBENCH)
    add_subdirectory(tools/control_testbench)
endif()

# ==============================================================================
# tools/membrum-fit -- offline drum-sample to Membrum-preset fitter
# ==============================================================================
# Analysis-by-synthesis CLI that ingests WAV drum samples, renders through the
# production Membrum voice (via membrum_dsp), and emits .vstpreset files.
# See specs/membrum-fit-tool.md for the execution plan.
if(VSTWORK_BUILD_MEMBRUM_FIT)
    add_subdirectory(tools/membrum-fit)
endif()

# ==============================================================================
# tools/krate-render -- offline audio render + feature-extraction CLI
# ==============================================================================
# Renders a plugin Processor to WAV + a JSON feature summary; the executor for
# the "render is ground truth" A/B loop. Currently Membrum-only (links membrum_core).
option(VSTWORK_BUILD_KRATE_RENDER "Build the tools/krate-render offline render CLI" ON)

if(VSTWORK_BUILD_KRATE_RENDER)
    add_subdirectory(tools/krate-render)
endif()

# ==============================================================================
# Copy compile_commands.json to project root (for clangd / Serena)
# ==============================================================================
if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    configure_file(
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COPYONLY
    )
    message(STATUS "[KRATE] Copied compile_commands.json to project root")
endif()
