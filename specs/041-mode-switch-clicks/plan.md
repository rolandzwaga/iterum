# Implementation Plan: Mode Switch Click-Free Transitions

**Branch**: `041-mode-switch-clicks` | **Date**: 2025-12-30 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/041-mode-switch-clicks/spec.md`

## Summary

Eliminate audible clicks when switching between delay modes by implementing a 50ms equal-power crossfade between the old and new mode outputs. When a mode change is detected, both modes process simultaneously and their outputs are blended using sin/cos gains until the transition completes.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, VSTGUI, existing DSP primitives (OnePoleSmoother, LinearRamp)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (unit tests), Approval Tests (regression), pluginval (VST3 validation) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: VST3 audio plugin
**Performance Goals**: <5% total CPU usage, <50ms mode transition latency
**Constraints**: Real-time audio thread safety (no allocations, no locks in process())
**Scale/Scope**: 11 delay modes, 110 mode-to-mode transitions (11×10)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocations in process() - crossfade buffers pre-allocated in setupProcessing()
- [x] No locks/mutexes in audio thread - using std::atomic for mode state
- [x] All buffers pre-allocated in setupProcessing()

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

This section prevents One Definition Rule (ODR) violations by documenting existing components that may be reused or would conflict with new implementations.

### Mandatory Searches Performed

**Classes/Structs to be created**: None - modifying existing Processor class

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ModeCrossfader | `grep -r "class ModeCrossfader" src/` | No | Not needed - inline in Processor |

**Utility Functions to be created**: None - using existing patterns

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| processMode | `grep -r "processMode" src/` | No | processor.cpp | Create as private helper |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Equal-power crossfade | character_processor.h:256-258 | 3 | Reference pattern for sin/cos gains |
| LinearRamp | smoother.h | 1 | Could use for crossfade position (optional) |
| OnePoleSmoother | smoother.h | 1 | Available if smoothing needed |
| BlockContext | block_context.h | 0 | Passed to mode processing |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No crossfade utilities
- [x] `src/dsp/core/` - No crossfade utilities in Layer 0
- [x] `src/dsp/systems/character_processor.h` - Reference implementation
- [x] `src/dsp/systems/stereo_field.h` - Reference implementation
- [x] `src/processor/processor.h` - Target file for modifications
- [x] `src/processor/processor.cpp` - Target file for modifications
- [x] `ARCHITECTURE.md` - Component inventory reviewed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new classes are being created. Modifications are confined to the existing Processor class, adding private member variables and a helper method. The crossfade pattern is implemented inline, following the CharacterProcessor example.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | sampleRate | `double sampleRate = 44100.0` | ✓ |
| BlockContext | blockSize | `size_t blockSize = 0` | ✓ |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | ✓ |
| BlockContext | isPlaying | `bool isPlaying = false` | ✓ |
| std::atomic | load | `T load(memory_order order) const noexcept` | ✓ |

### Header Files Read

- [x] `src/dsp/core/block_context.h` - BlockContext struct
- [x] `src/dsp/systems/character_processor.h` - Crossfade reference pattern
- [x] `src/dsp/systems/stereo_field.h` - Alternate crossfade pattern
- [x] `src/dsp/primitives/smoother.h` - LinearRamp, OnePoleSmoother
- [x] `src/processor/processor.h` - Target class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CharacterProcessor | Crossfade uses `1.5707963f` (π/2) | `std::cos(position * 1.5707963f)` |
| Mode atomic | Load with relaxed ordering | `mode_.load(std::memory_order_relaxed)` |
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `equalPowerGains()` | Audio-specific algorithm, 4+ consumers, reduces duplication | `src/dsp/core/crossfade_utils.h` | Processor, CharacterProcessor, CrossfadingDelayLine, future features |
| `kHalfPi` | Shared constant for crossfade math | `src/dsp/core/crossfade_utils.h` | Same as above |

**Identified for extraction**: The equal-power crossfade pattern is duplicated across multiple components:
- `CharacterProcessor` (character_processor.h:256-258) - inline sin/cos
- `CrossfadingDelayLine` (crossfading_delay_line.h) - linear crossfade (could use equal-power)
- `Processor` (new) - would be 4th duplicate

Per CLAUDE.md criteria: "Will 3+ components need it?" → **Yes (4 components)**

### Proposed Layer 0 API

```cpp
// src/dsp/core/crossfade_utils.h
namespace Iterum::DSP {

/// Pi/2 constant for crossfade calculations
constexpr float kHalfPi = 1.5707963267948966f;

/// Calculate equal-power crossfade gains (constant power: fadeOut² + fadeIn² ≈ 1)
/// @param position Crossfade position [0.0 = start, 1.0 = complete]
/// @param fadeOut Output gain for outgoing signal (1.0 → 0.0)
/// @param fadeIn Output gain for incoming signal (0.0 → 1.0)
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept {
    fadeOut = std::cos(position * kHalfPi);
    fadeIn = std::sin(position * kHalfPi);
}

/// Single-call version returning both gains as a pair
[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept {
    return {std::cos(position * kHalfPi), std::sin(position * kHalfPi)};
}

} // namespace Iterum::DSP
```

### Existing Components to Refactor

| Component | Current Implementation | Refactor Action |
|-----------|------------------------|-----------------|
| CharacterProcessor | Inline `std::cos/sin(crossfadePosition_ * 1.5707963f)` | Replace with `equalPowerGains()` |
| CrossfadingDelayLine | Linear crossfade (`+=/-= increment`) | **Optional**: Could upgrade to equal-power for smoother fades |
| Processor (new) | N/A | Use `equalPowerGains()` from start |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| processMode() | Processor-specific, handles 11 mode cases |

**Decision**: Extract `equalPowerGains()` to Layer 0, refactor existing duplicates to use it.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Processor (crosses Layer 3/4 boundary)

**Related features at same layer** (from ROADMAP.md or known plans):
- Future preset morphing (would need similar crossfade)
- Future parameter automation smoothing (uses OnePoleSmoother, not crossfade)
- Any future mode/state transitions in DSP components

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `equalPowerGains()` | HIGH | CharacterProcessor, CrossfadingDelayLine, Processor, preset morphing, any transition | Extract to Layer 0 |
| Mode crossfade state management | LOW | Processor-specific | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract `equalPowerGains()` to Layer 0 | 4+ existing/planned consumers, audio-specific semantics, reduces duplication |
| Refactor CharacterProcessor | Eliminate duplicate inline crossfade math |
| Keep CrossfadingDelayLine linear (optional upgrade) | Linear crossfade works for delay time changes; equal-power is optional improvement |
| No shared ModeTransitionManager | Over-engineering - state management is component-specific |

### Review Trigger

After implementing **preset morphing** (if ever), review this section:
- [x] Crossfade utility already extracted to Layer 0 - reuse `equalPowerGains()`
- [ ] Does preset morphing need additional crossfade helpers? → Extend crossfade_utils.h

## Project Structure

### Documentation (this feature)

```text
specs/041-mode-switch-clicks/
├── plan.md              # This file
├── research.md          # Phase 0 output - codebase analysis
├── spec.md              # Feature specification
├── quickstart.md        # Phase 1 output - implementation guide
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── processor/
│   ├── processor.h           # Add crossfade state variables
│   └── processor.cpp         # Add crossfade logic, processMode helper
└── dsp/
    ├── core/
    │   └── crossfade_utils.h # NEW: Layer 0 - equalPowerGains(), kHalfPi
    ├── primitives/
    │   └── crossfading_delay_line.h  # REFACTOR (optional): Use equalPowerGains()
    └── systems/
        └── character_processor.h     # REFACTOR: Use equalPowerGains()

tests/
└── unit/
    ├── core/
    │   └── crossfade_utils_tests.cpp  # NEW: Layer 0 utility tests
    └── processor/
        └── mode_crossfade_tests.cpp   # NEW: Mode transition tests
```

**Structure Decision**:
1. Create new Layer 0 utility (`crossfade_utils.h`) for shared crossfade math
2. Refactor CharacterProcessor to use the shared utility
3. Optionally upgrade CrossfadingDelayLine from linear to equal-power crossfade
4. Add unit tests for both the utility and the mode transitions

## Complexity Tracking

No constitution violations requiring justification.
