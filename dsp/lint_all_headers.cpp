// ==============================================================================
// KrateDSP Lint Stub - Strict clang-tidy analysis of all public headers
// ==============================================================================
// This file exists solely to give clang-tidy a .cpp translation unit that
// includes every public DSP header. Because it lives in dsp/ (not dsp/tests/),
// it uses the root .clang-tidy config with strict checks.
//
// Usage:  ./tools/run-clang-tidy.ps1 -Target dsp-lib
//
// This file is NOT part of the KrateDSP library itself; it is compiled as a
// separate OBJECT library target (dsp_lint_stub) for compile_commands.json.
// ==============================================================================

// Layer 0: Core
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/stereo_output.h>
#include <krate/dsp/core/chebyshev.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/dsp_utils.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/filter_design.h>
#include <krate/dsp/core/filter_tables.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/polyblep.h>
#include <krate/dsp/core/scale_harmonizer.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/stereo_utils.h>
#include <krate/dsp/core/wavefold_math.h>
#include <krate/dsp/core/window_functions.h>

// Layer 1: Primitives
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/allpass_1pole.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/bit_crusher.h>
#include <krate/dsp/primitives/bitwise_mangler.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/primitives/chebyshev_shaper.h>
#include <krate/dsp/primitives/comb_filter.h>
#include <krate/dsp/primitives/crossfading_delay_line.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/grain_pool.h>
#include <krate/dsp/primitives/hard_clip_adaa.h>
#include <krate/dsp/primitives/hard_clip_polyblamp.h>
#include <krate/dsp/primitives/hilbert_transform.h>
#include <krate/dsp/primitives/i_feedback_processor.h>
#include <krate/dsp/primitives/ladder_filter.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/reverse_buffer.h>
#include <krate/dsp/primitives/ring_saturation.h>
#include <krate/dsp/primitives/rolling_capture_buffer.h>
#include <krate/dsp/primitives/sample_rate_converter.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/sequencer_core.h>
#include <krate/dsp/primitives/slice_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_transient_detector.h>
#include <krate/dsp/primitives/spectral_utils.h>
#include <krate/dsp/primitives/spectrum_fifo.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/stochastic_shaper.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/sweep_position_buffer.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/primitives/wavefolder.h>
#include <krate/dsp/primitives/waveshaper.h>

// Layer 2: Processors
#include <krate/dsp/processors/aliasing_effect.h>
#include <krate/dsp/processors/allpass_saturator.h>
#include <krate/dsp/processors/arpeggiator_core.h>
#include <krate/dsp/processors/audio_rate_filter_fm.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/processors/chaos_mod_source.h>
#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/processors/diffusion_network.h>
#include <krate/dsp/processors/diode_clipper.h>
#include <krate/dsp/processors/ducking_processor.h>
#include <krate/dsp/processors/dynamics_processor.h>
#include <krate/dsp/processors/envelope_filter.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/feedback_distortion.h>
#include <krate/dsp/processors/formant_distortion.h>
#include <krate/dsp/processors/formant_filter.h>
#include <krate/dsp/processors/fractal_distortion.h>
#include <krate/dsp/processors/frequency_shifter.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/grain_processor.h>
#include <krate/dsp/processors/grain_scheduler.h>
#include <krate/dsp/processors/granular_distortion.h>
#include <krate/dsp/processors/karplus_strong.h>
#include <krate/dsp/processors/midside_processor.h>
#include <krate/dsp/processors/modal_resonator.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/multimode_filter.h>
#include <krate/dsp/processors/multistage_env_filter.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/note_selective_filter.h>
#include <krate/dsp/processors/pattern_scheduler.h>
#include <krate/dsp/processors/perlin_noise_source.h>
#include <krate/dsp/processors/phaser.h>
#include <krate/dsp/processors/pitch_follower_source.h>
#include <krate/dsp/processors/pitch_shift_processor.h>
#include <krate/dsp/processors/pitch_tracking_filter.h>
#include <krate/dsp/processors/random_source.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/processors/reverse_feedback_processor.h>
#include <krate/dsp/processors/sample_hold_filter.h>
#include <krate/dsp/processors/sample_hold_source.h>
#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/self_oscillating_filter.h>
#include <krate/dsp/processors/sidechain_filter.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
#include <krate/dsp/processors/spectral_distortion.h>
#include <krate/dsp/processors/spectral_gate.h>
#include <krate/dsp/processors/spectral_morph_filter.h>
#include <krate/dsp/processors/spectral_tilt.h>
#include <krate/dsp/processors/stochastic_filter.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/temporal_distortion.h>
#include <krate/dsp/processors/transient_detector.h>
#include <krate/dsp/processors/transient_filter.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/processors/waveguide_resonator.h>

// Layer 3: Systems
#include <krate/dsp/systems/amp_channel.h>
#include <krate/dsp/systems/vector_mixer.h>
#include <krate/dsp/systems/character_processor.h>
#include <krate/dsp/systems/delay_engine.h>
#include <krate/dsp/systems/distortion_rack.h>
#include <krate/dsp/systems/feedback_network.h>
#include <krate/dsp/systems/filter_feedback_matrix.h>
#include <krate/dsp/systems/filter_step_sequencer.h>
#include <krate/dsp/systems/flexible_feedback_network.h>
#include <krate/dsp/systems/fuzz_pedal.h>
#include <krate/dsp/systems/granular_engine.h>
#include <krate/dsp/systems/granular_filter.h>
#include <krate/dsp/systems/modulation_engine.h>
#include <krate/dsp/systems/modulation_matrix.h>
#include <krate/dsp/systems/stereo_field.h>
#include <krate/dsp/systems/tap_manager.h>
#include <krate/dsp/systems/tape_machine.h>
#include <krate/dsp/systems/timevar_comb_bank.h>
#include <krate/dsp/systems/vowel_sequencer.h>

#include <krate/dsp/systems/synth_voice.h>
#include <krate/dsp/systems/poly_synth_engine.h>

// Seraphis Phase 7 (specs/seraphis-phase7-voice-engine), FR-080
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

// Vorago Phase 2 (specs/vorago-phase2-noise-organism), FR-001
#include <krate/dsp/systems/noise_organism.h>

// Layer 4: Effects
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/effects/bbd_delay.h>
#include <krate/dsp/effects/digital_delay.h>
#include <krate/dsp/effects/ducking_delay.h>
#include <krate/dsp/effects/freeze_mode.h>
#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/effects/multi_tap_delay.h>
#include <krate/dsp/effects/pattern_freeze_mode.h>
#include <krate/dsp/effects/ping_pong_delay.h>
#include <krate/dsp/effects/reverb.h>
#include <krate/dsp/effects/reverse_delay.h>
#include <krate/dsp/effects/shimmer_delay.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/effects/tape_delay.h>
