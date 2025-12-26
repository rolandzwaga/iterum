# Implementation Plan: Freeze Mode

**Branch**: `031-freeze-mode` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/031-freeze-mode/spec.md`

## Summary

FreezeMode is a Layer 4 user feature that provides infinite sustain of delay buffer contents with optional pitch shifting, diffusion, and decay control. **Key insight**: FlexibleFeedbackNetwork already has freeze mode built-in (`setFreezeEnabled()`), so this feature is primarily a user-facing wrapper that adds decay control and shimmer mix capabilities on top of the existing infrastructure.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: FlexibleFeedbackNetwork (Layer 3), PitchShiftProcessor (Layer 2), DiffusionNetwork (Layer 2), MultimodeFilter (Layer 2), OnePoleSmoother (Layer 1)
**Storage**: N/A (real-time audio processor)
**Testing**: Catch2 via ctest
**Target Platform**: Windows, macOS, Linux (VST3 plugin)
**Project Type**: Single (DSP library within plugin)
**Performance Goals**: <1% CPU at 44.1kHz stereo (SC-008)
**Constraints**: Real-time safe (no allocations in process), <50ms transitions (SC-001)
**Scale/Scope**: Single Layer 4 feature composing existing Layer 1-3 components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

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
| FreezeMode | `grep -r "class FreezeMode" src/` | No | Create New |
| FreezeFeedbackProcessor | `grep -r "class FreezeFeedbackProcessor" src/` | No | Create New |
| FreezeState (enum) | `grep -r "enum.*FreezeState" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none needed) | N/A | N/A | N/A | Reuse existing utilities |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FlexibleFeedbackNetwork | dsp/systems/flexible_feedback_network.h | 3 | Core delay + freeze infrastructure |
| IFeedbackProcessor | dsp/systems/i_feedback_processor.h | 3 | Interface for FreezeFeedbackProcessor |
| PitchShiftProcessor | dsp/processors/pitch_shift_processor.h | 2 | Optional pitch shifting in freeze |
| DiffusionNetwork | dsp/processors/diffusion_network.h | 2 | Optional diffusion in freeze |
| MultimodeFilter | dsp/processors/multimode_filter.h | 2 | Already integrated in FFN |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| dbToGain | dsp/core/db_utils.h | 0 | Output gain conversion |
| BlockContext | dsp/core/block_context.h | 0 | Tempo sync context |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No freeze-related classes
- [x] `src/dsp/core/` - No freeze-related utilities
- [x] `ARCHITECTURE.md` - FreezeMode not yet listed
- [x] `src/dsp/systems/flexible_feedback_network.h` - Has built-in freeze, will compose
- [x] `src/dsp/features/shimmer_delay.h` - Reference pattern for Layer 4 feature

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (FreezeMode, FreezeFeedbackProcessor) are unique. The existing freeze functionality in FlexibleFeedbackNetwork will be composed, not duplicated. Pattern follows established ShimmerDelay architecture.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FlexibleFeedbackNetwork | setFreezeEnabled | `void setFreezeEnabled(bool enabled) noexcept` | ✓ |
| FlexibleFeedbackNetwork | isFreezeEnabled | `[[nodiscard]] bool isFreezeEnabled() const noexcept` | ✓ |
| FlexibleFeedbackNetwork | setProcessor | `void setProcessor(IFeedbackProcessor* processor, float crossfadeMs = 50.0f) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setProcessorMix | `void setProcessorMix(float mix) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setFeedbackAmount | `void setFeedbackAmount(float amount) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setFilterEnabled | `void setFilterEnabled(bool enabled) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | ✓ |
| FlexibleFeedbackNetwork | setFilterType | `void setFilterType(FilterType type) noexcept` | ✓ |
| FlexibleFeedbackNetwork | process | `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` | ✓ |
| IFeedbackProcessor | prepare | `virtual void prepare(double sampleRate, std::size_t maxBlockSize) noexcept = 0` | ✓ |
| IFeedbackProcessor | process | `virtual void process(float* left, float* right, std::size_t numSamples) noexcept = 0` | ✓ |
| IFeedbackProcessor | reset | `virtual void reset() noexcept = 0` | ✓ |
| IFeedbackProcessor | getLatencySamples | `[[nodiscard]] virtual std::size_t getLatencySamples() const noexcept = 0` | ✓ |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate) noexcept` | ✓ |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | ✓ |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | ✓ |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | ✓ |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | ✓ |
| BlockContext | blockSize | `size_t blockSize = 512` | ✓ |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | ✓ |

### Header Files Read

- [x] `src/dsp/systems/flexible_feedback_network.h` - FlexibleFeedbackNetwork class
- [x] `src/dsp/systems/i_feedback_processor.h` - IFeedbackProcessor interface
- [x] `src/dsp/features/shimmer_delay.h` - ShimmerDelay + ShimmerFeedbackProcessor pattern
- [x] `src/dsp/core/block_context.h` - BlockContext struct

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member order is sampleRate, blockSize, tempoBPM | Use C++20 designated initializers: `{.sampleRate = 44100.0, .tempoBPM = 120.0}` |
| FlexibleFeedbackNetwork | setProcessorMix takes 0-100% | `setProcessorMix(100.0f)` for full effect |
| FlexibleFeedbackNetwork | setFeedbackAmount takes 0.0-1.2 | NOT 0-100%, use raw ratio |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

No new Layer 0 utilities needed. Decay is implemented as simple per-sample gain multiplication.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateEffectiveFeedback() | One-liner combining freeze/decay state |

**Decision**: No Layer 0 extractions. Decay control is trivial arithmetic applied per-sample in the feedback processor.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- Granular Delay (4.8): May share freeze/capture concepts
- Spectral Delay (4.9): Spectral freeze could use similar state management
- Ducking Delay (4.10): No freeze overlap

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FreezeFeedbackProcessor | MEDIUM | Granular/Spectral if they use FFN | Keep local - only 1 consumer now |
| FreezeState enum | LOW | Unlikely - other features have different state models | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep FreezeFeedbackProcessor local | Only FreezeMode needs it; Granular/Spectral may have different freeze semantics |
| No shared FreezeState enum | Different features will have different state machines |

### Review Trigger

After implementing **Granular Delay (4.8)**, review this section:
- [ ] Does Granular need freeze or similar? → Consider shared pattern
- [ ] Does Granular use FFN with processor injection? → Document shared pattern
- [ ] Any duplicated freeze state logic? → Consider extraction

## Project Structure

### Documentation (this feature)

```text
specs/031-freeze-mode/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/dsp/
├── features/
│   └── freeze_mode.h      # FreezeMode class + FreezeFeedbackProcessor
├── systems/
│   └── flexible_feedback_network.h  # Already has freeze built-in (compose this)
└── processors/
    └── (pitch_shift_processor.h, diffusion_network.h - reuse existing)

tests/
└── unit/
    └── features/
        └── freeze_mode_test.cpp
```

**Structure Decision**: Single header file in `src/dsp/features/` following ShimmerDelay pattern. FreezeFeedbackProcessor is a nested/companion class in the same header.

## Architecture Design

### Signal Flow

```
                            FREEZE DISENGAGED
Input ──┬────────────────────────────────────────────────┬──> Mix ──> Output
        │                                                │
        v                                                │
   ┌─────────┐                                          │
   │  Delay  │<─────────────────────────────────────────┤
   │  Line   │                                          │
   └────┬────┘                                          │
        │                                               │
        v (feedback path)                               │
   ┌───────────────────────────────────────────────────┐│
   │ FreezeFeedbackProcessor:                          ││
   │  ┌──────────┐  ┌───────────┐  ┌────────┐        ││
   │  │  Pitch   │─>│ Diffusion │─>│ Decay  │        ││
   │  │ Shifter  │  │  Network  │  │ (gain) │        ││
   │  └──────────┘  └───────────┘  └────────┘        ││
   │       ^                           │              ││
   │       └─── shimmerMix blend ──────┘              ││
   └──────────────────────────────────────────────────┘│
        │                                              │
        v                                              │
   ┌────────┐  ┌────────┐                             │
   │ Filter │─>│ Limit  │─────────────────────────────┘
   └────────┘  └────────┘

                            FREEZE ENGAGED
Input (MUTED) ─────────────────────────────────────────> Dry (silent)
        x                                                │
        │                                                │
        v                                                │
   ┌─────────┐                                          │
   │  Delay  │<──── (100% - decay) feedback ────────────┤
   │  Line   │                                          │
   └────┬────┘                                          │
        │                                               │
        v (frozen loop)                                 │
   ┌───────────────────────────────────────────────────┐│
   │ FreezeFeedbackProcessor (optional pitch evolve)   ││
   └───────────────────────────────────────────────────┘│
        │                                              │
        └──────────────────────────────────────────────┘
```

### Component Responsibilities

1. **FreezeMode** (Layer 4):
   - User-facing API (setFreezeEnabled, setDecay, setShimmerMix, etc.)
   - Composes FlexibleFeedbackNetwork
   - Manages FreezeFeedbackProcessor injection
   - Handles dry/wet mixing

2. **FreezeFeedbackProcessor** (implements IFeedbackProcessor):
   - Applies pitch shifting (optional)
   - Applies diffusion (optional)
   - Applies decay gain reduction per iteration
   - Blends shimmer mix

3. **FlexibleFeedbackNetwork** (Layer 3 - existing):
   - Handles freeze state transitions (already built-in)
   - Manages delay lines
   - Applies feedback filtering
   - Applies limiting for >100% feedback

### Key Implementation Notes

1. **Decay Implementation**:
   - Decay is NOT the same as reducing feedback from 100%
   - Decay is per-iteration gain reduction applied IN the feedback path
   - At 0% decay: gain = 1.0 (infinite sustain)
   - At 100% decay: gain ≈ 0.999^n per sample (rapid fade ~500ms to -60dB)
   - Formula: `decayGain = 1.0f - (decayAmount * decayCoeff)` where decayCoeff is calculated from sample rate

2. **Freeze Transition Timing (FR-007, SC-001)**:
   - FlexibleFeedbackNetwork already uses 20ms smoothing via `freezeMixSmoother_`
   - For delay times < 50ms, transition adapts to `min(50ms, delay_time)`
   - This is already handled by the existing FFN smoothing

3. **Shimmer Mix in Freeze**:
   - When frozen with pitch shift, each iteration shifts further up/down
   - shimmerMix controls blend of pitched vs unpitched in the loop
   - At 0% shimmerMix: standard freeze (unpitched loop)
   - At 100% shimmerMix: full shimmer freeze (each loop iteration is pitch-shifted)

## Complexity Tracking

No constitution violations. Design follows established patterns from ShimmerDelay (029).

## Next Steps

1. Generate `research.md` (Phase 0) - minimal since architecture is well-understood
2. Generate `data-model.md` (Phase 1) - document FreezeFeedbackProcessor, FreezeMode classes
3. Run `/speckit.tasks` to generate implementation tasks
