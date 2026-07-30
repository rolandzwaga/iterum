# ==============================================================================
# KrateDSP Library
# ==============================================================================
# Shared DSP library for all Krate Audio plugins
# Contains: Layers 0-4 (core utilities, primitives, processors, systems, effects)

# Define KrateDSP as a static library
# Most DSP code is header-only; .cpp files provide out-of-line implementations
add_library(KrateDSP STATIC
    include/krate/dsp/core/dsp_utils.cpp
    include/krate/dsp/core/spectral_simd.cpp
    include/krate/dsp/effects/fdn_reverb_simd.cpp
    include/krate/dsp/processors/arpeggiator_core.cpp
    include/krate/dsp/processors/pitch_shift_processor.cpp
    include/krate/dsp/processors/harmonic_oscillator_bank_simd.cpp
    include/krate/dsp/processors/modal_resonator_bank_simd.cpp
    include/krate/dsp/systems/sympathetic_resonance_simd.cpp
)

# Public include directory for consumers
target_include_directories(KrateDSP
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link to pffft for SIMD-optimized FFT
# PUBLIC because fft.h (a public header) includes <pffft.h>
target_link_libraries(KrateDSP PUBLIC PFFFT)

# Link to Google Highway for SIMD-accelerated spectral math
# PRIVATE because no Highway headers appear in public API
target_link_libraries(KrateDSP PRIVATE hwy hwy_contrib)

# C++20 requirement
target_compile_features(KrateDSP PUBLIC cxx_std_20)

# macOS universal binary support (x86_64 + arm64)
# Uses VST3 SDK function - no-op on non-macOS platforms
smtg_target_setup_universal_binary(KrateDSP)

# Compiler warnings
if(MSVC)
    target_compile_options(KrateDSP PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /wd4100  # Unreferenced formal parameter
    )
else()
    target_compile_options(KrateDSP PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ==============================================================================
# IEEE 754 compliance for SIMD math
# ==============================================================================
# The VST3 SDK enables -ffast-math globally, which can break float comparisons
# used for input clamping (e.g., clamping negatives before log10). On ARM/NEON,
# this causes batchLog10 to produce NaN/inf for zero/negative inputs.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        include/krate/dsp/core/spectral_simd.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# ==============================================================================
# Header Files (for IDE visibility)
# ==============================================================================
# Layer 0: Core Utilities
set(KRATE_DSP_CORE_HEADERS
    include/krate/dsp/core/block_context.h
    include/krate/dsp/core/crossfade_utils.h
    include/krate/dsp/core/db_utils.h
    include/krate/dsp/core/dsp_utils.h
    include/krate/dsp/core/env_curve.h
    include/krate/dsp/core/fast_math.h
    include/krate/dsp/core/spectral_simd.h
    include/krate/dsp/core/grain_envelope.h
    include/krate/dsp/core/interpolation.h
    include/krate/dsp/core/math_constants.h
    include/krate/dsp/core/note_value.h
    include/krate/dsp/core/pitch_utils.h
    include/krate/dsp/core/random.h
    include/krate/dsp/core/stereo_output.h
    include/krate/dsp/core/stereo_utils.h
    include/krate/dsp/core/wavetable_data.h
    include/krate/dsp/core/window_functions.h
    include/krate/dsp/core/hungarian_algorithm.h
)

# Layer 1: Primitives
set(KRATE_DSP_PRIMITIVES_HEADERS
    include/krate/dsp/primitives/adsr_envelope.h
    include/krate/dsp/primitives/biquad.h
    include/krate/dsp/primitives/bit_crusher.h
    include/krate/dsp/primitives/crossfading_delay_line.h
    include/krate/dsp/primitives/delay_line.h
    include/krate/dsp/primitives/fft.h
    include/krate/dsp/primitives/grain_pool.h
    include/krate/dsp/primitives/i_feedback_processor.h
    include/krate/dsp/primitives/lfo.h
    include/krate/dsp/primitives/oversampler.h
    include/krate/dsp/primitives/reverse_buffer.h
    include/krate/dsp/primitives/sample_rate_reducer.h
    include/krate/dsp/primitives/pitch_tracker.h
    include/krate/dsp/primitives/smoother.h
    include/krate/dsp/primitives/spectral_buffer.h
    include/krate/dsp/primitives/spectral_transient_detector.h
    include/krate/dsp/primitives/spectrum_fifo.h
    include/krate/dsp/primitives/stft.h
    include/krate/dsp/primitives/wavetable_generator.h
    include/krate/dsp/primitives/wavetable_oscillator.h
)

# Layer 2: Processors
set(KRATE_DSP_PROCESSORS_HEADERS
    include/krate/dsp/processors/diffusion_network.h
    include/krate/dsp/processors/ducking_processor.h
    include/krate/dsp/processors/dynamics_processor.h
    include/krate/dsp/processors/envelope_follower.h
    include/krate/dsp/processors/formant_preserver.h
    include/krate/dsp/processors/grain_processor.h
    include/krate/dsp/processors/grain_scheduler.h
    include/krate/dsp/processors/midside_processor.h
    include/krate/dsp/processors/multimode_filter.h
    include/krate/dsp/processors/noise_generator.h
    include/krate/dsp/processors/pitch_shift_processor.h
    include/krate/dsp/processors/reverse_feedback_processor.h
    include/krate/dsp/processors/saturation_processor.h
    include/krate/dsp/processors/spectral_freeze_oscillator.h
    include/krate/dsp/processors/mono_handler.h
    include/krate/dsp/processors/note_processor.h
    include/krate/dsp/processors/arpeggiator_core.h
    include/krate/dsp/processors/chorus.h
    include/krate/dsp/processors/harmonic_types.h
    include/krate/dsp/processors/yin_pitch_detector.h
    include/krate/dsp/processors/partial_tracker.h
    include/krate/dsp/processors/multi_pitch_detector.h
    include/krate/dsp/processors/multi_source_sieve.h
    include/krate/dsp/processors/harmonic_oscillator_bank_simd.h
    include/krate/dsp/processors/modal_resonator_bank.h
    include/krate/dsp/processors/modal_resonator_bank_simd.h
)

# Layer 3: Systems
set(KRATE_DSP_SYSTEMS_HEADERS
    include/krate/dsp/systems/character_processor.h
    include/krate/dsp/systems/delay_engine.h
    include/krate/dsp/systems/feedback_network.h
    include/krate/dsp/systems/flexible_feedback_network.h
    include/krate/dsp/systems/granular_engine.h
    include/krate/dsp/systems/modulation_matrix.h
    include/krate/dsp/systems/stereo_field.h
    include/krate/dsp/systems/tap_manager.h
    include/krate/dsp/systems/vector_mixer.h
    include/krate/dsp/systems/voice_allocator.h
    include/krate/dsp/systems/synth_voice.h
    include/krate/dsp/systems/poly_synth_engine.h
    include/krate/dsp/systems/harmonizer_engine.h
)

# Layer 4: Effects
set(KRATE_DSP_EFFECTS_HEADERS
    include/krate/dsp/effects/aether_reverb.h
    include/krate/dsp/effects/bbd_delay.h
    include/krate/dsp/effects/digital_delay.h
    include/krate/dsp/effects/ducking_delay.h
    include/krate/dsp/effects/freeze_mode.h
    include/krate/dsp/effects/granular_delay.h
    include/krate/dsp/effects/multi_tap_delay.h
    include/krate/dsp/effects/ping_pong_delay.h
    include/krate/dsp/effects/reverse_delay.h
    include/krate/dsp/effects/shimmer_delay.h
    include/krate/dsp/effects/spectral_delay.h
    include/krate/dsp/effects/fdn_reverb.h
    include/krate/dsp/effects/reverb.h
    include/krate/dsp/effects/tape_delay.h
)

# Add headers to target for IDE visibility
target_sources(KrateDSP PRIVATE
    ${KRATE_DSP_CORE_HEADERS}
    ${KRATE_DSP_PRIMITIVES_HEADERS}
    ${KRATE_DSP_PROCESSORS_HEADERS}
    ${KRATE_DSP_SYSTEMS_HEADERS}
    ${KRATE_DSP_EFFECTS_HEADERS}
)

# ==============================================================================
# Lint Stub (for strict clang-tidy analysis of all public headers)
# ==============================================================================
# This OBJECT library compiles a single .cpp that includes every public header,
# giving clang-tidy a translation unit under the root .clang-tidy (strict).
# Run: ./tools/run-clang-tidy.ps1 -Target dsp-lib
add_library(dsp_lint_stub OBJECT lint_all_headers.cpp)
target_link_libraries(dsp_lint_stub PRIVATE KrateDSP)
target_compile_features(dsp_lint_stub PRIVATE cxx_std_20)
set_target_properties(dsp_lint_stub PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ==============================================================================
# Tests
# ==============================================================================
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
