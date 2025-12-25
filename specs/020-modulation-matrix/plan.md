# Implementation Plan: Modulation Matrix

**Branch**: `020-modulation-matrix` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/020-modulation-matrix/spec.md`

## Summary

Implement a Layer 3 system component that routes modulation sources (LFO, EnvelopeFollower) to parameter destinations with depth control. The ModulationMatrix provides source-to-destination routing with per-route depth, bipolar/unipolar modes, and smooth depth transitions. It uses existing Layer 1/2 components (LFO, EnvelopeFollower, OnePoleSmoother) and is real-time safe with pre-allocated storage for up to 32 routes.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, LFO (Layer 1), EnvelopeFollower (Layer 2), OnePoleSmoother (Layer 1)
**Storage**: N/A (in-memory state only)
**Testing**: Catch2 *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project (VST plugin)
**Performance Goals**: <1% CPU for 16 routes at 44.1kHz stereo, sample-accurate modulation
**Constraints**: Zero allocations in process path, pre-allocate 32 routes
**Scale/Scope**: Layer 3 system component, 32 max routes, 16 max sources, 16 max destinations

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [X] No memory allocation in process() path
- [X] No locks or blocking operations in audio thread
- [X] Pre-allocation in prepare()

**Required Check - Principle IX (Layered Architecture):**
- [X] Layer 3 component depending only on Layer 0-2
- [X] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [X] Tasks will include TESTING-GUIDE.md context verification step
- [X] Tests will be written BEFORE implementation code
- [X] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [X] Codebase Research section below is complete
- [X] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [X] All FRs will have corresponding test verification
- [X] No placeholder implementations allowed

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ModulationMatrix, ModulationRoute, ModulationSource (interface), ModulationDestination

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ModulationMatrix | `grep -r "class ModulationMatrix" src/` | No | Create New |
| ModulationRoute | `grep -r "struct ModulationRoute" src/` | No | Create New |
| ModulationSource | `grep -r "class ModulationSource" src/` | No | Create New (interface) |
| ModulationDestination | `grep -r "struct ModulationDestination" src/` | No | Create New |
| ModulationMode | `grep -r "enum.*ModulationMode" src/` | No | Create New |

**Utility Functions to be created**: None needed - all can be member functions

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| bipolarToUnipolar | N/A | No | — | Keep as inline in header |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Smooth depth changes per route |
| LFO | dsp/primitives/lfo.h | 1 | Reference for ModulationSource interface |
| EnvelopeFollower | dsp/processors/envelope_follower.h | 2 | Reference for ModulationSource interface |
| detail::isNaN | dsp/core/db_utils.h | 0 | NaN handling in process |
| detail::flushDenormal | dsp/core/db_utils.h | 0 | Denormal prevention |

### Files Checked for Conflicts

- [X] `src/dsp/dsp_utils.h` - No modulation classes
- [X] `src/dsp/core/` - No modulation utilities
- [X] `src/dsp/primitives/` - Has LFO, no ModulationMatrix
- [X] `src/dsp/processors/` - Has EnvelopeFollower, no ModulationMatrix
- [X] `src/dsp/systems/` - Has DelayEngine, FeedbackNetwork, no ModulationMatrix

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ModulationMatrix, ModulationRoute, ModulationSource, ModulationDestination) are unique and not found in the codebase. No similar utility functions exist that could conflict.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

No new Layer 0 utilities needed. The only potential candidate (bipolar-to-unipolar conversion) is a trivial one-liner: `(x + 1.0f) * 0.5f`.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| bipolarToUnipolar | One-liner: `(x + 1.0f) * 0.5f`, only used internally |
| sumModulations | Specific to ModulationMatrix internals |
| applyDepth | Combines mode and depth, class-specific |

**Decision**: No Layer 0 extraction needed. All utilities are trivial one-liners or class-specific logic.

## Project Structure

### Documentation (this feature)

```text
specs/020-modulation-matrix/
├── plan.md              # This file
├── research.md          # Interface design decisions
├── data-model.md        # Entity definitions
├── quickstart.md        # Usage examples
├── contracts/           # C++ header contract
│   └── modulation_matrix.h
└── tasks.md             # Implementation tasks (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── systems/
        └── modulation_matrix.h   # Layer 3 header-only implementation

tests/
└── unit/
    └── systems/
        └── modulation_matrix_test.cpp
```

**Structure Decision**: Single header-only implementation in `src/dsp/systems/` following the pattern established by DelayEngine and FeedbackNetwork.

## Complexity Tracking

> No constitution violations to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
