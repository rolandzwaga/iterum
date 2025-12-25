# Implementation Plan: TapManager

**Branch**: `023-tap-manager` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/023-tap-manager/spec.md`

## Summary

TapManager is a Layer 3 system component that manages up to 16 independent delay taps. Each tap has per-tap controls for time, level, pan, filter, and feedback routing. The component includes preset tap patterns (quarter notes, dotted eighths, triplets, golden ratio, Fibonacci) and supports both free-running (milliseconds) and tempo-synced time modes. This is the final Layer 3 component before moving to Layer 4 user features.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayLine (Layer 1), Biquad (Layer 1), OnePoleSmoother (Layer 1), BlockContext (Layer 0), NoteValue (Layer 0)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: Single - DSP system component
**Performance Goals**: < 2% CPU for 16 active taps at 44.1kHz stereo (per SC-007)
**Constraints**: Real-time safe (noexcept, no allocations in process), all parameters smoothed within 20ms
**Scale/Scope**: 16 taps maximum, supports delay times up to maxDelayMs

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | ✅ PASS | All processing noexcept, pre-allocate 16 taps in prepare() |
| III. Modern C++ | ✅ PASS | C++20, RAII, constexpr where possible |
| IX. Layered Architecture | ✅ PASS | Layer 3 using Layer 0-1 components only |
| X. DSP Processing Constraints | ✅ PASS | Parameter smoothing, constant-power pan law |
| XI. Performance Budgets | ✅ PASS | < 2% for 16 taps (within Layer 3 budget) |
| XII. Test-First Development | ✅ WILL COMPLY | Tests before implementation |
| XIII. Living Architecture | ✅ WILL COMPLY | Update ARCHITECTURE.md at completion |
| XIV. ODR Prevention | ✅ PASS | See Codebase Research section below |
| XV. Honest Completion | ✅ WILL COMPLY | Full compliance verification at end |

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

**Classes/Structs to be created**: Tap, TapManager, TapPattern (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Tap | `grep -r "class Tap" src/` | No | Create New |
| TapManager | `grep -r "class TapManager" src/` | No | Create New |
| TapPattern | `grep -r "TapPattern" src/` | No | Create New (enum) |
| DelayTap | `grep -r "DelayTap" src/` | No | Not needed |
| MultiTap | `grep -r "MultiTap" src/` | No | Not needed |

**Utility Functions to be created**: None - will use existing utilities

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| kGoldenRatio | `grep -r "kGoldenRatio" src/` | Yes | math_constants.h | Reuse |
| dbToGain | `grep -r "dbToGain" src/` | Yes | db_utils.h | Reuse |
| NoteValue | `grep -r "NoteValue" src/` | Yes | note_value.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | src/dsp/primitives/delay_line.h | 1 | Core delay storage for each tap |
| Biquad | src/dsp/primitives/biquad.h | 1 | Per-tap LP/HP filtering |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | Parameter smoothing for all tap controls |
| BlockContext | src/dsp/core/block_context.h | 0 | Tempo sync (BPM) |
| NoteValue | src/dsp/core/note_value.h | 0 | Tempo-synced note values |
| kGoldenRatio | src/dsp/core/math_constants.h | 0 | Golden ratio pattern preset |
| dbToGain | src/dsp/core/db_utils.h | 0 | Level parameter conversion |
| kPi | src/dsp/core/math_constants.h | 0 | Constant-power pan law calculations |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No Tap-related classes
- [x] `src/dsp/core/` - Only reusable utilities found
- [x] `ARCHITECTURE.md` - No existing TapManager component
- [x] `src/dsp/systems/` - DelayEngine exists (reference), no TapManager

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (Tap, TapManager, TapPattern enum) are unique and not found in codebase. All utility functions needed (golden ratio, dB conversion, note values) already exist in Layer 0 and will be reused. No duplicate definitions will be created.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| fibonacciSequence() | Mathematical pattern, reusable | math_utils.h | TapManager, future sequencer |
| — | — | — | — |

**Decision**: Will implement Fibonacci sequence generation inline in TapManager for now. If reused by 3+ components in the future, extract to Layer 0.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples() | Already exists in multiple components (StereoField, DelayEngine) - consider future Layer 0 extraction |
| applyPan() | Simple constant-power pan, only used internally |
| generatePattern() | TapManager-specific pattern generation |

**Decision**: Keep pattern generation internal. msToSamples() pattern is repeated but simple enough that ODR is not a concern (inline in each header).

## Project Structure

### Documentation (this feature)

```text
specs/023-tap-manager/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contract)
│   └── tap_manager.h    # C++ header contract
├── checklists/
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── systems/
        └── tap_manager.h    # TapManager class (header-only, Layer 3)

tests/
└── unit/
    └── systems/
        └── tap_manager_test.cpp  # Unit tests
```

**Structure Decision**: Single header file in `src/dsp/systems/` following established pattern (delay_engine.h, feedback_network.h, stereo_field.h). Header-only implementation for inline optimization.

## Complexity Tracking

> No Constitution Check violations. No complexity tracking needed.

---

## Design Decisions

### Tap Data Structure

Each tap is a lightweight struct containing:
- DelayLine reference (shared with manager)
- Biquad filter (owned per tap)
- OnePoleSmoother instances for time, level, pan, cutoff (owned per tap)
- Configuration: enabled, time mode, note value, feedback amount

### Memory Strategy

- Pre-allocate all 16 taps in `prepare()`
- Each tap shares a single large DelayLine (TapManager owns one DelayLine)
- Taps read at different positions from the shared delay line
- This is memory-efficient compared to 16 separate DelayLines

### Pan Law

Constant-power pan law using sine/cosine:
- L = cos(θ) × signal
- R = sin(θ) × signal
- Where θ = (pan + 1) × π/4 (pan in [-1, +1])

### Pattern Presets

Note: `n` is 1-based (n = 1, 2, 3, ..., tapCount). First tap is always at n=1.

| Pattern | Formula |
|---------|---------|
| Quarter | tap[n] = n × quarterNoteMs (e.g., 500, 1000, 1500, 2000ms at 120 BPM) |
| Dotted Eighth | tap[n] = n × (quarterNoteMs × 0.75) (e.g., 375, 750, 1125, 1500ms) |
| Triplet | tap[n] = n × (quarterNoteMs × 0.667) (e.g., 333, 667, 1000, 1333ms) |
| Golden Ratio | tap[1] = quarterNoteMs, tap[n] = tap[n-1] × 1.618 |
| Fibonacci | tap[n] = fib(n) × baseMs, where fib = 1, 1, 2, 3, 5, 8, 13... |

### Tempo Sync

- Each tap can independently be free-running (ms) or tempo-synced
- Tempo sync uses NoteValue enum from block_context.h
- Delay time = (60000 / BPM) × noteMultiplier

---

## Next Steps

1. Generate `research.md` (complete - no unknowns)
2. Generate `data-model.md` with Tap and TapManager entities
3. Generate `contracts/tap_manager.h` with full API
4. Generate `quickstart.md` with usage examples
5. Run `/speckit.tasks` to generate implementation tasks
