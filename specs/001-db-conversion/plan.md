# Implementation Plan: dB/Linear Conversion Utilities (Refactor)

**Branch**: `001-db-conversion` | **Date**: 2025-12-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-db-conversion/spec.md`
**Type**: Refactor & Upgrade

## Summary

Refactor existing dB/linear conversion functions from `src/dsp/dsp_utils.h` into proper Layer 0 core utilities. This involves extracting the functions to `src/dsp/core/db_utils.h`, upgrading them to be `constexpr`, improving the silence floor from -80 dB to -144 dB, adding NaN handling, and updating the namespace from `VSTWork` to `Iterum`.

## Technical Context

**Language/Version**: C++20 (constexpr math functions required)
**Primary Dependencies**: None (Layer 0 - standard library only: `<cmath>`)
**Storage**: N/A (pure functions, no state)
**Testing**: Catch2 (existing test framework in project)
**Target Platform**: Windows (MSVC 2019+), macOS (Clang/Xcode 13+), Linux (GCC 10+)
**Project Type**: Single project - VST3 plugin with layered DSP architecture
**Performance Goals**: < 0.1% CPU per instance (Layer 0 utilities should be negligible)
**Constraints**: Zero memory allocation, no exceptions, deterministic constant-time execution
**Scale/Scope**: Extract 2 functions, upgrade to constexpr, migrate existing usages

## Existing Code Analysis

**Current location**: `src/dsp/dsp_utils.h`
**Current namespace**: `VSTWork::DSP`

| Function | Current | Target |
|----------|---------|--------|
| `dBToLinear` | `inline`, -80dB floor implicit | `constexpr`, rename to `dbToGain` |
| `linearToDb` | `inline`, -80dB floor | `constexpr`, rename to `gainToDb`, -144dB floor |
| `kSilenceThreshold` | `1e-8f` | Replace with `kSilenceFloorDb = -144.0f` |

**Files requiring migration**:
- `src/dsp/dsp_utils.h` - Extract functions, add include to new file
- Any files using `VSTWork::DSP::dBToLinear` or `linearToDb`

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Gate (Phase 0)

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Audio Thread Safety | PASS | No allocation, no locks, no exceptions, no I/O |
| III. Modern C++ Standards | PASS | Using constexpr, const, value semantics only |
| IV. SIMD & DSP Optimization | PASS | Minimal branching (one conditional for floor check) |
| VII. Project Structure | PASS | Files go in `src/dsp/core/` per constitution |
| VIII. Testing Discipline | PASS | Pure functions, testable without VST infrastructure |
| IX. Layered DSP Architecture | PASS | Layer 0 - NO dependencies on higher layers |
| X. DSP Processing Constraints | PASS | N/A for unit conversion (no sample-rate dependent operations) |
| XI. Performance Budgets | PASS | Simple math operations, well under 0.1% CPU |
| XII. Test-First Development | PASS | Tasks include TESTING-GUIDE check, tests before implementation, commits |

### Post-Design Gate (Phase 1)

| Principle | Status | Notes |
|-----------|--------|-------|
| All gates | PASS | No design changes alter compliance |

**Gate Result**: PASS - Proceed with implementation

## Project Structure

### Documentation (this feature)

```text
specs/001-db-conversion/
├── plan.md              # This file
├── research.md          # Phase 0 output - existing code analysis
├── data-model.md        # Phase 1 output - function signatures
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contract (header file spec)
│   └── db_utils.h       # Function contract specification
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code Changes

```text
src/
└── dsp/
    ├── dsp_utils.h          # MODIFY: Remove dB functions, add #include "core/db_utils.h"
    └── core/                # CREATE: Layer 0 directory
        └── db_utils.h       # CREATE: New constexpr dB utilities

tests/
└── unit/
    └── core/                # CREATE: Layer 0 test directory
        └── db_utils_test.cpp    # CREATE: Unit tests for dB utilities
```

### Migration Path

1. Create `src/dsp/core/` directory (Layer 0)
2. Create `src/dsp/core/db_utils.h` with new `Iterum::DSP` functions
3. Update `src/dsp/dsp_utils.h`:
   - Add `#include "core/db_utils.h"`
   - Remove old `dBToLinear` and `linearToDb` functions
   - Add `using` declarations or inline wrappers for backward compatibility (optional)
4. Update any direct usages of old functions throughout codebase
5. Update namespace from `VSTWork` to `Iterum` where needed

**Structure Decision**: Extract to new header in `src/dsp/core/` following the layered DSP architecture. The original `dsp_utils.h` will include the new header for compatibility.

## Complexity Tracking

> No violations - all constitution gates pass.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| *None* | *N/A* | *N/A* |

## Breaking Changes

| Change | Impact | Mitigation |
|--------|--------|------------|
| Silence floor -80dB → -144dB | Very quiet signals now report lower dB values | Acceptable - more accurate representation |
| Function rename | Compilation errors if using old names | Provide compatibility aliases or search/replace |
| Namespace change | `VSTWork` → `Iterum` | Part of broader project rename, apply consistently |
