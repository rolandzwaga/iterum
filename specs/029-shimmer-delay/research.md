# Research: Shimmer Delay Mode

**Feature**: 029-shimmer-delay
**Date**: 2025-12-26

## Overview

This is a **composition-only feature** that combines existing Layer 2-3 components. No new algorithms or research are required.

## Decisions

### Signal Flow Architecture

**Decision**: Pitch shifting operates in the feedback path, not on dry input.

**Rationale**: The classic shimmer effect (Strymon BigSky, Eventide Space, Valhalla Shimmer) creates cascading harmonics by pitch-shifting the feedback signal. Each delay repeat goes through the pitch shifter, so the 1st repeat is +12 semitones, 2nd is +24 semitones, etc. This creates the characteristic "ascending heavenly" texture.

**Alternatives considered**:
- Pitch shift on dry input only: Would not create cascading effect, just a fixed pitch shift
- Parallel pitch shifter: More complex, less efficient, same result achievable with shimmer mix control

### Shimmer Mix Control

**Decision**: Blend pitched and unpitched signal in the feedback path using a mix control (0-100%).

**Rationale**: This provides creative flexibility:
- 0% shimmer mix = standard delay (no pitch shifting)
- 50% shimmer mix = subtle shimmer with some clean repeats
- 100% shimmer mix = full shimmer cascade

**Alternatives considered**:
- Fixed 100% pitch in feedback: Too inflexible for subtle use
- Separate pitched delay line: More complex, higher CPU, same result achievable

### Diffusion Placement

**Decision**: Apply diffusion after pitch shifting in the feedback path.

**Rationale**: This creates the lush, reverb-like texture of premium shimmer effects. Diffusion smears the pitch-shifted repeats into a continuous wash while preserving the harmonic cascade.

**Alternatives considered**:
- Pre-pitch diffusion: Would smear before pitch shift, less defined harmonics
- Parallel diffusion: More complex, harder to control

### Default Pitch Mode

**Decision**: Use Granular mode as default (FR-008).

**Rationale**:
- Granular offers good quality with acceptable latency (~46ms)
- Simple mode has audible artifacts at extreme shifts
- PhaseVocoder has excellent quality but high latency (~116ms)
- Granular is the best balance for real-time use

## Component Integration Notes

### PitchShiftProcessor Integration

- Process() is mono-only, must call twice for stereo
- Latency varies by mode, must report to host
- Semitones and cents parameters already smoothed internally

### FeedbackNetwork Integration

- Must enable saturation for >100% feedback safety
- Provides filtering in feedback path (matches spec FR-020, FR-021)
- Cross-feedback not needed for shimmer (used for ping-pong)

### DiffusionNetwork Integration

- prepare() takes `float` sampleRate (cast from double)
- Size parameter controls smear characteristics
- Density controls number of allpass stages

### DelayEngine Integration

- Provides tempo sync via setNoteValue()
- Already handles BlockContext for tempo
- Dry/wet mixing handled externally (ShimmerDelay manages mix)

## No Open Questions

All technical decisions resolved. Ready for implementation.
