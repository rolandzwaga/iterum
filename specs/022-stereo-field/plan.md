# Implementation Plan: Stereo Field

**Branch**: `022-stereo-field` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/022-stereo-field/spec.md`

## Summary

Layer 3 system component for stereo processing modes in the delay effect. Provides five stereo modes (Mono, Stereo, PingPong, DualMono, MidSide), width control (0-200%), constant-power panning, L/R timing offset (±50ms), and L/R ratio for polyrhythmic delays. Composes from existing DelayEngine and MidSideProcessor.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayEngine (Layer 3), MidSideProcessor (Layer 2), OnePoleSmoother (Layer 1), dbToGain (Layer 0)
**Storage**: N/A (pure DSP, no persistence)
**Testing**: Catch2 (Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project - DSP library
**Performance Goals**: <1% CPU per instance at 44.1kHz stereo (SC-003)
**Constraints**: Real-time safe (no allocations in process), 50ms mode transitions (SC-002)
**Scale/Scope**: Single StereoField class with ~300 LOC, ~25 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() path
- [x] noexcept on all processing methods
- [x] Pre-allocate all buffers in prepare()

**Required Check - Principle IX (Layered Architecture):**
- [x] StereoField is Layer 3 (depends on Layer 0-2 only)
- [x] No circular dependencies

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

**Classes/Structs to be created**: StereoField, StereoMode

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| StereoField | `grep -r "class StereoField" src/` | No | Create New |
| StereoMode | `grep -r "StereoMode" src/` | No | Create New (enum class) |
| PanLaw | `grep -r "PanLaw\|constantPowerPan" src/` | No | Create as inline function |

**Utility Functions to be created**: constantPowerPan

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| constantPowerPan | `grep -r "constantPowerPan\|panLaw" src/` | No | N/A | Create inline in stereo_field.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayEngine | src/dsp/systems/delay_engine.h | 3 | Per-channel delays in Stereo/PingPong modes |
| MidSideProcessor | src/dsp/processors/midside_processor.h | 2 | M/S encoding/decoding and width control |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | All parameter smoothing |
| dbToGain | src/dsp/core/db_utils.h | 0 | Level calculations for pan |
| isNaN | src/dsp/core/db_utils.h | 0 | NaN input validation (FR-019) |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No StereoField or panning utilities
- [x] `src/dsp/core/` - No stereo/pan utilities
- [x] `src/dsp/systems/` - No StereoField class
- [x] `ARCHITECTURE.md` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (StereoField, StereoMode) are unique and not found in codebase. constantPowerPan is a simple inline function that will be local to stereo_field.h. Existing components (DelayEngine, MidSideProcessor) will be composed, not duplicated.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

**Note**: `constantPowerPan` is specific to stereo field control and unlikely to have 3+ consumers. Keep as member/local function.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| constantPowerPan | Single usage in StereoField, simple trigonometric calculation |
| calculateLRRatio | Feature-specific, only used by StereoField |

**Decision**: No Layer 0 extraction needed. All utilities are feature-specific.

## Project Structure

### Documentation (this feature)

```text
specs/022-stereo-field/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── stereo_field.h   # API contract
└── tasks.md             # Phase 2 output (via /speckit.tasks)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── systems/
        └── stereo_field.h    # Layer 3 StereoField class

tests/
└── unit/
    └── systems/
        └── stereo_field_test.cpp   # All test cases
```

**Structure Decision**: Single header in Layer 3 (systems/). No .cpp file needed as implementation is header-only for inlining.

## Design Decisions

### Mode Architecture

Each stereo mode has distinct routing:

| Mode | Routing Description |
|------|---------------------|
| Mono | Sum L+R → single delay → output to both channels |
| Stereo | L → delayL, R → delayR (independent times via L/R Ratio) |
| PingPong | L+R → delay → alternating L/R output with cross-feedback |
| DualMono | L+R → single delay → panned output |
| MidSide | L/R → M/S encode → delay M and S independently → M/S decode → L/R |

### Constant-Power Panning

Use sine/cosine pan law for constant-power panning:
- `gainL = cos(pan * PI/2)` where pan is 0-1 (0=left, 0.5=center, 1=right)
- `gainR = sin(pan * PI/2)`

This ensures constant total power as pan position changes.

### Mode Transition Strategy

Use the same crossfade approach as CharacterProcessor (spec 021):
- 50ms linear crossfade between old and new mode outputs
- Mode transition state tracks crossfade progress
- Both modes run during transition, outputs blended

### L/R Offset Implementation

L/R Offset (±50ms) adds a small delay to one channel:
- Positive offset: R delayed relative to L
- Negative offset: L delayed relative to R
- Requires separate small delay lines for offset (max 50ms at 192kHz = 9,600 samples)

### L/R Ratio Implementation

L/R Ratio multiplies the base delay time for each channel:
- Ratio stored as L:R where R is always 1.0 (base)
- L time = base * (L/R ratio)
- Clamped to [0.1, 10.0] to prevent extreme values (FR-016)
