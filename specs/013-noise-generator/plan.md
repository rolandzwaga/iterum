# Implementation Plan: NoiseGenerator

**Branch**: `013-noise-generator` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/013-noise-generator/spec.md`

## Summary

Implement a Layer 2 DSP processor that generates five types of audio noise for analog character and lo-fi effects: white noise (flat spectrum), pink noise (-3dB/octave), tape hiss (signal-dependent), vinyl crackle (impulsive), and asperity noise (tape head contact). Each noise type has independent level control, with mixing support and real-time safety.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**: Layer 0-1 primitives (Biquad, OnePoleSmoother, EnvelopeFollower, db_utils)
**Storage**: N/A (real-time audio processor)
**Testing**: Catch2 (per TESTING-GUIDE.md)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project (DSP library within VST plugin)
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz stereo (Layer 2 budget per Constitution Principle XI)
**Constraints**: No memory allocation in process(), pre-allocate in prepare()
**Scale/Scope**: Layer 2 DSP processor, composable with Character Processor in Layer 3

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- [x] **Principle II (Real-Time Safety)**: No allocations in process(), pre-allocate all buffers in prepare()
- [x] **Principle III (Modern C++)**: C++20, RAII, constexpr where applicable
- [x] **Principle VIII (Testing)**: Catch2 tests for all DSP functions per TESTING-GUIDE.md
- [x] **Principle IX (Layered Architecture)**: Layer 2 processor composing Layer 0-1 components
- [x] **Principle X (DSP Constraints)**: Sample-accurate processing, proper filtering for pink noise
- [x] **Principle XI (Performance Budget)**: < 0.5% CPU per Layer 2 processor instance

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| NoiseGenerator | `grep -r "class NoiseGenerator" src/` | No | Create New |
| NoiseType | `grep -r "enum.*NoiseType" src/` | No | Create New |
| PinkNoiseFilter | `grep -r "class PinkNoise" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| generateWhiteNoise | `grep -r "whiteNoise\|WhiteNoise" src/` | No | - | Create New |
| generatePinkNoise | `grep -r "pinkNoise\|PinkNoise" src/` | No | - | Create New |

**Random Number Generation**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| xorshift32 / fastRandom | `grep -r "random" src/` | Yes (in LFO) | primitives/lfo.h | Reference pattern, create separate RNG |

**Note**: LFO uses a simple LCG for its SmoothRandom waveform. We will implement a similar approach (xorshift32 for speed) but as a separate RNG to avoid any coupling.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Level smoothing to prevent clicks |
| Biquad | dsp/primitives/biquad.h | 1 | Pink noise filter, tape hiss spectral shaping |
| EnvelopeFollower | dsp/processors/envelope_follower.h | 2 | Signal-level detection for tape hiss/asperity |
| dbToGain/gainToDb | dsp/core/db_utils.h | 0 | Level conversion for all noise types |
| kPi, kTwoPi | dsp/dsp_utils.h | 0 | Mathematical constants |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No noise implementations found
- [x] `src/dsp/core/` - No noise implementations found
- [x] `src/dsp/primitives/lfo.h` - Has simple LCG random, will reference pattern
- [x] `ARCHITECTURE.md` - Component inventory reviewed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (NoiseGenerator, NoiseType, PinkNoiseFilter) are unique and not found in codebase. The random number generator pattern from LFO will be referenced but implemented as a separate component to avoid any coupling.

## Project Structure

### Documentation (this feature)

```text
specs/013-noise-generator/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Pink noise algorithms, RNG selection
├── data-model.md        # NoiseGenerator API design
├── quickstart.md        # Usage examples
├── contracts/           # API contracts
│   └── noise_generator.h  # Header contract
├── checklists/
│   └── requirements.md  # Validation checklist
└── tasks.md             # Implementation tasks (generated by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── core/
│   │   └── random.h          # New: Fast PRNG (xorshift32)
│   └── processors/
│       └── noise_generator.h # New: NoiseGenerator Layer 2 processor

tests/
└── unit/
    ├── core/
    │   └── random_test.cpp   # New: RNG tests
    └── processors/
        └── noise_generator_test.cpp  # New: NoiseGenerator tests
```

**Structure Decision**: Standard DSP structure following Layer 2 processor pattern. RNG utility placed in core/ (Layer 0) since it may be reused by other components.

## Complexity Tracking

> No constitution violations requiring justification.

## Phase Status

### Phase 0: Outline & Research - COMPLETE

- [x] Codebase searches for ODR prevention
- [x] research.md created with algorithm decisions:
  - Pink noise: Paul Kellet's 3-term filter (best accuracy/cost ratio)
  - RNG: xorshift32 (fast, sufficient period for audio)
  - Tape hiss: pink noise + high-shelf biquad
  - Vinyl crackle: Poisson-distributed impulses + exponential amplitudes
  - Signal modulation: EnvelopeFollower with dB-domain scaling

### Phase 1: Design & Contracts - COMPLETE

- [x] data-model.md created with entities:
  - NoiseType enumeration (5 types)
  - NoiseChannelConfig value object
  - NoiseGenerator processor class
  - PinkNoiseFilter and CrackleState internal types
- [x] contracts/noise_generator.h created with full API specification
- [x] quickstart.md created with usage examples
- [x] Agent context updated (CLAUDE.md)

### Phase 2: Task Generation - COMPLETE

- [x] tasks.md generated with 82 tasks across 11 phases
- [x] Tasks organized by user story (US1-US6)
- [x] Test-first workflow enforced per Principle XII
- [x] Cross-platform verification steps included

**Ready for implementation**: Run `/speckit.implement` or work through tasks manually.
