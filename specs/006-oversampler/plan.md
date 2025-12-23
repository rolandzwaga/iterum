# Implementation Plan: Oversampler

**Branch**: `006-oversampler` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/006-oversampler/spec.md`

## Summary

Implement a Layer 1 DSP primitive for 2x and 4x oversampling to enable anti-aliased nonlinear processing (saturation, waveshaping). The oversampler will support multiple filter quality levels and latency modes (zero-latency IIR, linear-phase FIR). It composes the existing `BiquadCascade` for IIR filtering and introduces new halfband FIR filtering for linear-phase mode.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK, existing Biquad primitive, standard library
**Storage**: N/A (real-time audio processing, no persistence)
**Testing**: Catch2 v3 with ApprovalTests *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows x64, macOS x64/ARM64, Linux x64
**Project Type**: VST3 plugin DSP primitive (Layer 1)
**Performance Goals**: < 0.5% CPU for 512-sample stereo block at 2x, 44.1kHz
**Constraints**: Zero allocations in process(), < 0.1% CPU per instance (Layer 1 budget)
**Scale/Scope**: Supports 44.1-192kHz base rates, 2x/4x factors, up to 8192 sample blocks

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process() path
- [x] No locks or blocking operations
- [x] Pre-allocated buffers in prepare()
- [x] noexcept on all audio-thread functions

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Component is Layer 1 (DSP Primitive)
- [x] Depends only on Layer 0 (core utilities) and standard library
- [x] Reuses existing Layer 1 components where appropriate (BiquadCascade)

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Oversampling required for nonlinearities (this IS that component)
- [x] Denormal flushing included
- [x] Filter stability validated

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
| Oversampler | `grep -r "class Oversampler" src/` | No | Create New |
| HalfbandFilter | `grep -r "class HalfbandFilter" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none - using inline methods) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| BiquadCascade<4> | dsp/primitives/biquad.h | 1 | IIR anti-aliasing for zero-latency mode (8-pole elliptic-ish) |
| BiquadCoefficients::calculate | dsp/primitives/biquad.h | 1 | Generate lowpass coefficients for IIR mode |
| kDenormalThreshold | dsp/primitives/biquad.h | 1 | Denormal flushing constant |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No oversampler components found
- [x] `src/dsp/core/` - No oversampler components found
- [x] `src/dsp/primitives/` - Biquad exists (will reuse); no Oversampler
- [x] `ARCHITECTURE.md` - Oversampler listed as planned Layer 1, not implemented

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing oversampler or halfband filter implementations found. The planned classes (`Oversampler`, `HalfbandFilter`) are unique. Will reuse existing `BiquadCascade` for IIR mode rather than duplicating filter logic.

## Project Structure

### Documentation (this feature)

```text
specs/006-oversampler/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── oversampler.h    # API contract
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── primitives/
        └── oversampler.h    # Main implementation (header-only)

tests/
└── unit/
    └── primitives/
        └── oversampler_test.cpp
```

**Structure Decision**: Following existing pattern from biquad.h, lfo.h, delay_line.h - header-only implementation in primitives folder with corresponding unit test file.

## Complexity Tracking

> **No violations requiring justification**

The implementation follows all constitution principles without exceptions.
