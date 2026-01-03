# ==============================================================================
# KrateDSP Test Configuration
# ==============================================================================
# Constitution Principle VIII: Testing Discipline
# - DSP algorithms MUST be pure functions testable without VST infrastructure
# - Unit tests MUST cover all DSP algorithms
# - Tests MUST run in CI/CD pipeline
# ==============================================================================

# ==============================================================================
# DSP Unit Tests
# ==============================================================================
# Constitution Principle VIII: DSP algorithms must be independently testable

add_executable(dsp_tests
    # Root test file
    unit/test_dsp_utils.cpp

    # Layer 0: Core
    unit/core/db_utils_test.cpp
    unit/core/math_constants_test.cpp
    unit/core/window_functions_test.cpp
    unit/core/random_test.cpp
    unit/core/note_value_test.cpp
    unit/core/note_value_mapping_test.cpp
    unit/core/block_context_test.cpp
    unit/core/fast_math_test.cpp
    unit/core/interpolation_test.cpp
    unit/core/stereo_utils_test.cpp
    unit/core/pitch_utils_test.cpp
    unit/core/grain_envelope_test.cpp
    unit/core/crossfade_utils_test.cpp

    # Layer 1: Primitives
    unit/primitives/delay_line_test.cpp
    unit/primitives/crossfading_delay_line_test.cpp
    unit/primitives/lfo_test.cpp
    unit/primitives/biquad_test.cpp
    unit/primitives/smoother_test.cpp
    unit/primitives/oversampler_test.cpp
    unit/primitives/fft_test.cpp
    unit/primitives/spectral_buffer_test.cpp
    unit/primitives/stft_test.cpp
    unit/primitives/bit_crusher_test.cpp
    unit/primitives/bit_crusher_symmetric_quantization_test.cpp
    unit/primitives/bit_crusher_rng_bias_test.cpp
    unit/primitives/sample_rate_reducer_test.cpp
    unit/primitives/reverse_buffer_test.cpp
    unit/primitives/grain_pool_test.cpp

    # Layer 2: Processors
    unit/processors/multimode_filter_test.cpp
    unit/processors/saturation_processor_test.cpp
    unit/processors/envelope_follower_test.cpp
    unit/processors/dynamics_processor_test.cpp
    unit/processors/ducking_processor_test.cpp
    unit/processors/noise_generator_test.cpp
    unit/processors/midside_processor_test.cpp
    unit/processors/diffusion_network_test.cpp
    unit/processors/pitch_shift_processor_test.cpp
    unit/processors/reverse_feedback_processor_test.cpp
    unit/processors/grain_scheduler_test.cpp
    unit/processors/grain_processor_test.cpp

    # Layer 3: Systems
    unit/systems/delay_engine_test.cpp
    unit/systems/feedback_network_test.cpp
    unit/systems/feedback_network_dc_blocking_test.cpp
    unit/systems/modulation_matrix_test.cpp
    unit/systems/character_processor_test.cpp
    unit/systems/character_processor_digital_ramping_test.cpp
    unit/systems/stereo_field_test.cpp
    unit/systems/tap_manager_test.cpp
    unit/systems/flexible_feedback_network_test.cpp
    unit/systems/granular_engine_test.cpp

    # Layer 4: Effects
    unit/effects/tape_delay_test.cpp
    unit/effects/tape_delay_params_diagnostic_test.cpp
    unit/effects/bbd_delay_test.cpp
    unit/effects/digital_delay_test.cpp
    unit/effects/digital_width_processing_test.cpp
    unit/effects/digital_delay_age0_ramping_test.cpp
    unit/effects/ping_pong_delay_test.cpp
    unit/effects/multi_tap_delay_test.cpp
    unit/effects/shimmer_delay_test.cpp
    unit/effects/reverse_delay_test.cpp
    unit/effects/freeze_mode_test.cpp
    unit/effects/ducking_delay_test.cpp
    unit/effects/spectral_delay_test.cpp
    unit/effects/granular_delay_test.cpp
    unit/effects/granular_delay_tempo_sync_test.cpp
)

target_link_libraries(dsp_tests
    PRIVATE
        KrateDSP
        Catch2::Catch2
        test_helpers
)

target_compile_features(dsp_tests PRIVATE cxx_std_20)

# ==============================================================================
# Disable -ffast-math for files that require IEEE 754 compliant NaN handling
# ==============================================================================
# The VST3 SDK enables -ffast-math globally (see SMTG_PlatformToolset.cmake)
# which causes isnan() and __builtin_isnan() to be optimized out.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/test_dsp_utils.cpp
        unit/core/db_utils_test.cpp
        unit/core/math_constants_test.cpp
        unit/core/window_functions_test.cpp
        unit/core/note_value_test.cpp
        unit/core/note_value_mapping_test.cpp
        unit/core/block_context_test.cpp
        unit/core/fast_math_test.cpp
        unit/core/interpolation_test.cpp
        unit/core/pitch_utils_test.cpp
        unit/core/grain_envelope_test.cpp
        unit/core/crossfade_utils_test.cpp
        unit/primitives/smoother_test.cpp
        unit/primitives/oversampler_test.cpp
        unit/primitives/delay_line_test.cpp
        unit/primitives/lfo_test.cpp
        unit/primitives/biquad_test.cpp
        unit/primitives/fft_test.cpp
        unit/primitives/spectral_buffer_test.cpp
        unit/primitives/stft_test.cpp
        unit/primitives/bit_crusher_test.cpp
        unit/primitives/bit_crusher_symmetric_quantization_test.cpp
        unit/primitives/bit_crusher_rng_bias_test.cpp
        unit/primitives/sample_rate_reducer_test.cpp
        unit/primitives/reverse_buffer_test.cpp
        unit/primitives/grain_pool_test.cpp
        unit/processors/multimode_filter_test.cpp
        unit/processors/saturation_processor_test.cpp
        unit/processors/envelope_follower_test.cpp
        unit/processors/dynamics_processor_test.cpp
        unit/processors/ducking_processor_test.cpp
        unit/processors/noise_generator_test.cpp
        unit/processors/midside_processor_test.cpp
        unit/processors/diffusion_network_test.cpp
        unit/processors/pitch_shift_processor_test.cpp
        unit/processors/reverse_feedback_processor_test.cpp
        unit/processors/grain_scheduler_test.cpp
        unit/processors/grain_processor_test.cpp
        unit/systems/delay_engine_test.cpp
        unit/systems/feedback_network_test.cpp
        unit/systems/modulation_matrix_test.cpp
        unit/systems/character_processor_test.cpp
        unit/systems/stereo_field_test.cpp
        unit/systems/tap_manager_test.cpp
        unit/systems/flexible_feedback_network_test.cpp
        unit/systems/granular_engine_test.cpp
        unit/systems/feedback_network_dc_blocking_test.cpp
        unit/systems/character_processor_digital_ramping_test.cpp
        unit/effects/tape_delay_test.cpp
        unit/effects/tape_delay_params_diagnostic_test.cpp
        unit/effects/bbd_delay_test.cpp
        unit/effects/digital_delay_test.cpp
        unit/effects/digital_width_processing_test.cpp
        unit/effects/digital_delay_age0_ramping_test.cpp
        unit/effects/ping_pong_delay_test.cpp
        unit/effects/multi_tap_delay_test.cpp
        unit/effects/shimmer_delay_test.cpp
        unit/effects/reverse_delay_test.cpp
        unit/effects/freeze_mode_test.cpp
        unit/effects/ducking_delay_test.cpp
        unit/effects/spectral_delay_test.cpp
        unit/effects/granular_delay_test.cpp
        unit/effects/granular_delay_tempo_sync_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()

# ==============================================================================
# CTest Integration
# ==============================================================================
catch_discover_tests(dsp_tests)
