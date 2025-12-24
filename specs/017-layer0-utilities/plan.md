# Implementation Plan: Layer 0 Utilities (BlockContext, FastMath, Interpolation)

**Branch**: `017-layer0-utilities` | **Date**: 2025-12-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/017-layer0-utilities/spec.md`

## Summary

This feature adds three Layer 0 utility components needed for Layer 3 readiness:
1. **BlockContext** - Per-block processing context carrying sample rate, block size, tempo, transport state, and time signature
2. **FastMath** - Optimized approximations of transcendental functions (fastSin, fastCos, fastTanh, fastExp) for CPU-critical paths
3. **Interpolation** - Standalone interpolation utilities (linear, cubic Hermite, Lagrange) extracted from DelayLine for reuse

All components are constexpr where possible, noexcept, and have no dependencies on Layer 1+.

## Technical Context

**Language/Version**: C++20 (matches project standard)
**Primary Dependencies**: Standard library only (`<cmath>`, `<cstddef>`, `<bit>`)
**Storage**: N/A (pure computation utilities)
**Testing**: Catch2 v3 (per `tests/CMakeLists.txt`) - Test-First per Constitution Principle XII
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform audio plugin
**Project Type**: VST3 plugin with layered DSP architecture
**Performance Goals**:
  - FastMath functions 2x faster than std:: equivalents
  - All functions suitable for audio callback (no allocations, no blocking)
**Constraints**:
  - Layer 0: No dependencies on DSP primitives or higher layers
  - Real-time safe: noexcept, no allocations, no exceptions
  - Cross-platform: Must work with -ffast-math disabled per CLAUDE.md
**Scale/Scope**: Core utilities used by all higher layers

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All functions will be noexcept
- [x] No dynamic memory allocation in any function
- [x] No blocking operations (locks, I/O)
- [x] No exceptions thrown

**Required Check - Principle III (Modern C++):**
- [x] Functions will be constexpr where algorithm permits
- [x] Value semantics throughout
- [x] C++20 features used appropriately

**Required Check - Principle IX (Layered Architecture):**
- [x] All components placed in `src/dsp/core/` (Layer 0)
- [x] No `#include` of Layer 1+ headers
- [x] Components can be used by any higher layer

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

**Classes/Structs to be created**: BlockContext, NoteValue (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| BlockContext | `grep -r "BlockContext\|ProcessContext" src/` | No | Create New |
| NoteValue | `grep -r "enum.*NoteValue" src/` | Yes (lfo.h:55) | Reuse from lfo.h or define compatible |
| NoteModifier | `grep -r "enum.*NoteModifier" src/` | Yes (lfo.h:66) | Reuse from lfo.h |

**Utility Functions to be created**: fastSin, fastCos, fastTanh, fastExp, linearInterpolate, cubicHermiteInterpolate, lagrangeInterpolate

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| fastSin | `grep -r "fastSin" src/` | No | - | Create New |
| fastCos | `grep -r "fastCos" src/` | No | - | Create New |
| fastTanh | `grep -r "fastTanh" src/` | No | - | Create New |
| fastExp | `grep -r "fastExp" src/` | No | - | Create New |
| linearInterpolate | `grep -r "linearInterpolate\|linear.*[Ii]nterpolat" src/` | Inline only | delay_line.h:213 | Extract as standalone |
| cubicHermiteInterpolate | `grep -r "cubicHermite\|hermite" src/` | No | - | Create New |
| lagrangeInterpolate | `grep -r "lagrange" src/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| kPi, kTwoPi, kHalfPi | dsp/core/math_constants.h | 0 | Used in FastMath sin/cos implementations |
| detail::isNaN() | dsp/core/db_utils.h | 0 | Referenced for NaN handling pattern |
| detail::constexprExp() | dsp/core/db_utils.h | 0 | Reference pattern for Taylor series |
| NoteValue enum | dsp/primitives/lfo.h | 1 | Informs BlockContext tempo sync |
| NoteModifier enum | dsp/primitives/lfo.h | 1 | Informs BlockContext tempo sync |
| updateTempoSyncFrequency() | dsp/primitives/lfo.h | 1 | Reference for tempoToSamples logic |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No conflicting utilities found
- [x] `src/dsp/core/` - Layer 0 core utilities (db_utils.h, math_constants.h checked)
- [x] `src/dsp/primitives/delay_line.h` - Inline interpolation found, will extract
- [x] `src/dsp/primitives/lfo.h` - NoteValue/NoteModifier enums found, tempo sync logic

### ODR Risk Assessment

**Risk Level**: Low

**Justification**:
- All planned types (BlockContext, FastMath functions, Interpolation functions) are unique and not found in codebase
- NoteValue/NoteModifier enums exist in lfo.h (Layer 1), but we'll define them in Layer 0 for proper layering, then update lfo.h to use them
- Inline interpolation in delay_line.h is method-internal, not standalone - no conflict when extracting to separate utilities

### Design Decision: NoteValue Location

The LFO already defines NoteValue/NoteModifier in Layer 1. For proper Layer 0 compliance:
1. Define canonical enums in new `src/dsp/core/note_value.h` (Layer 0)
2. Have lfo.h include and use the Layer 0 definitions
3. BlockContext uses the same Layer 0 definitions

This ensures consistent type definitions and proper layer dependencies.

## Project Structure

### Documentation (this feature)

```text
specs/017-layer0-utilities/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Technical decisions and rationale
├── data-model.md        # Entity definitions (BlockContext, NoteValue)
├── quickstart.md        # Integration examples
├── contracts/           # API contracts
│   ├── block_context.h  # BlockContext API contract
│   ├── fast_math.h      # FastMath API contract
│   └── interpolation.h  # Interpolation API contract
└── checklists/
    └── requirements.md  # Quality checklist
```

### Source Code (repository root)

```text
src/dsp/core/
├── db_utils.h           # Existing - dB conversion utilities
├── math_constants.h     # Existing - kPi, kTwoPi, etc.
├── random.h             # Existing - random number generation
├── window_functions.h   # Existing - window functions
├── note_value.h         # NEW: NoteValue/NoteModifier enums (Layer 0)
├── block_context.h      # NEW: BlockContext struct
├── fast_math.h          # NEW: FastMath utilities
└── interpolation.h      # NEW: Interpolation utilities

tests/unit/core/
├── db_utils_test.cpp        # Existing
├── math_constants_test.cpp  # Existing
├── random_test.cpp          # Existing
├── window_functions_test.cpp # Existing
├── note_value_test.cpp      # NEW: NoteValue tests
├── block_context_test.cpp   # NEW: BlockContext tests
├── fast_math_test.cpp       # NEW: FastMath tests
└── interpolation_test.cpp   # NEW: Interpolation tests
```

**Structure Decision**: Single project structure following existing `src/dsp/core/` pattern for Layer 0 utilities and `tests/unit/core/` for tests.

## Complexity Tracking

No constitution violations identified. All planned components align with:
- Layer 0 placement (no higher-layer dependencies)
- Real-time safety (constexpr/noexcept, no allocations)
- Test-first development workflow
- ODR prevention via codebase research
