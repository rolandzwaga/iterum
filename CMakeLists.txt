cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Project Configuration
# ==============================================================================
project(Iterum
    VERSION 0.0.1
    DESCRIPTION "Iterum VST3 Delay Plugin"
    LANGUAGES CXX
)

# C++ Standard (Constitution Principle III: C++17 minimum, C++20 preferred)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# VST3 SDK Configuration
# ==============================================================================
# Path to VST3 SDK (as git submodule in extern/)
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")

# Verify SDK exists
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}\n"
        "Please run: git submodule add https://github.com/steinbergmedia/vst3sdk.git extern/vst3sdk\n"
        "Then run: git submodule update --init --recursive"
    )
endif()

# SDK Options
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)  # Enables vstvalidator
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CXX_STANDARD "20" CACHE STRING "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)  # We use our own copy step
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)   # Don't auto-run on every build

# Add VST3 SDK
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# ==============================================================================
# Version Header Generation
# ==============================================================================
# Generate version.h from template - single source of truth for version
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h"
    @ONLY
)

# ==============================================================================
# Plugin Target
# ==============================================================================
set(PLUGIN_NAME "${PROJECT_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    # Plugin Entry
    src/entry.cpp

    # Processor (Audio Thread)
    src/processor/processor.h
    src/processor/processor.cpp

    # Controller (UI Thread)
    src/controller/controller.h
    src/controller/controller.cpp

    # Shared
    src/plugin_ids.h
    src/version.h

    # DSP (Pure algorithms - Constitution Principle VIII)
    src/dsp/dsp_utils.h
    src/dsp/dsp_utils.cpp
)

# Link SDK and VSTGUI
target_link_libraries(${PLUGIN_NAME}
    PRIVATE
        sdk
        vstgui_support
)

# Include directories (Constitution Principle VII: include/<project>/ style)
target_include_directories(${PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Configure version file
smtg_target_configure_version_file(${PLUGIN_NAME})

# ==============================================================================
# Platform-Specific Configuration
# ==============================================================================
if(SMTG_MAC)
    smtg_target_set_bundle(${PLUGIN_NAME}
        BUNDLE_IDENTIFIER "com.yourcompany.vstwork"
        COMPANY_NAME "Your Company"
    )
    # Set debug executable (e.g., a DAW for testing)
    # smtg_target_set_debug_executable(${PLUGIN_NAME} "/Applications/Reaper.app")
elseif(SMTG_WIN)
    target_sources(${PLUGIN_NAME}
        PRIVATE
            resources/win32resource.rc
    )
endif()

# ==============================================================================
# VSTGUI Resources
# ==============================================================================
smtg_target_add_plugin_resources(${PLUGIN_NAME}
    RESOURCES
        resources/editor.uidesc
)

# ==============================================================================
# Post-Build: Copy to VST3 System Folder
# ==============================================================================
# Copies the built plugin to the system VST3 folder after each build.
# NOTE: Requires running build as Administrator on Windows.
# Disable with: cmake -DVSTWORK_COPY_TO_VST3_FOLDER=OFF

option(VSTWORK_COPY_TO_VST3_FOLDER "Copy plugin to system VST3 folder after build" ON)

if(VSTWORK_COPY_TO_VST3_FOLDER AND SMTG_WIN)
    set(VST3_SYSTEM_FOLDER "C:/Program Files/Common Files/VST3")
    set(VST3_BUILD_OUTPUT "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${PLUGIN_NAME}.vst3")

    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Copying plugin to ${VST3_SYSTEM_FOLDER}..."
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VST3_BUILD_OUTPUT}"
            "${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E echo "[VSTWORK] Plugin installed to ${VST3_SYSTEM_FOLDER}/${PLUGIN_NAME}.vst3"
        COMMENT "Installing ${PLUGIN_NAME}.vst3 to system VST3 folder"
        VERBATIM
    )
endif()

# ==============================================================================
# Compiler Warnings (Constitution Principle III: Modern C++)
# ==============================================================================
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE
        /W4           # Warning level 4
        /permissive-  # Strict conformance
        /Zc:__cplusplus  # Report correct C++ version
        /wd4100       # Unreferenced formal parameter (common in SDK callbacks)
        /wd4458       # Declaration hides class member (SDK headers)
        # /WX         # Treat warnings as errors (enable for CI)
    )
else()
    target_compile_options(${PLUGIN_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter  # VST SDK has many unused params
        # -Werror     # Treat warnings as errors (enable for CI)
    )
endif()

# ==============================================================================
# Testing (Constitution Principle VIII)
# ==============================================================================
option(VSTWORK_BUILD_TESTS "Build unit tests" ON)

if(VSTWORK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==============================================================================
# Installation
# ==============================================================================
# Install to standard VST3 locations
if(SMTG_WIN)
    set(VSTWORK_INSTALL_DIR "$ENV{COMMONPROGRAMFILES}/VST3")
elseif(SMTG_MAC)
    set(VSTWORK_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(VSTWORK_INSTALL_DIR "$ENV{HOME}/.vst3")
endif()

install(TARGETS ${PLUGIN_NAME}
    LIBRARY DESTINATION "${VSTWORK_INSTALL_DIR}"
    ARCHIVE DESTINATION "${VSTWORK_INSTALL_DIR}"
)
