# Implementation Plan: Shimmer Delay Mode

**Branch**: `029-shimmer-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/029-shimmer-delay/spec.md`

## Summary

Implement a Layer 4 user feature that creates pitch-shifted feedback delay for ambient/ethereal textures. The classic "shimmer" effect routes audio through a pitch shifter in the feedback path, creating evolving cascades of harmonics. This is a composition-heavy feature that composes 5 existing Layer 2-3 components: DelayEngine, PitchShiftProcessor, DiffusionNetwork, FeedbackNetwork, and ModulationMatrix.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PitchShiftProcessor (L2), DiffusionNetwork (L2), FeedbackNetwork (L3), DelayEngine (L3), ModulationMatrix (L3)
**Storage**: N/A (stateless audio processing)
**Testing**: Catch2 (per Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 Plugin - DSP Layer 4 Feature
**Performance Goals**: <1% CPU per instance at 44.1kHz stereo (Constitution Principle XI)
**Constraints**: Real-time safe (no allocations in process()), noexcept
**Scale/Scope**: Single feature implementation composing existing components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() - composition uses pre-allocated components
- [x] All operations noexcept - follows component patterns
- [x] Pre-allocation in prepare() - follows FeedbackNetwork pattern

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 4 depends only on Layer 0-3 - correct composition
- [x] No circular dependencies - confirmed
- [x] Components at appropriate layers - all verified

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [x] Compliance table will be filled before claiming completion
- [x] No test thresholds will be relaxed from spec requirements

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ShimmerDelay

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ShimmerDelay | `grep -r "class ShimmerDelay" src/` | No | Create New |
| ShimmerConfig | N/A - will be embedded parameters | N/A | Not needed |

**Utility Functions to be created**: None - all utilities exist in Layer 0-3

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PitchShiftProcessor | dsp/processors/pitch_shift_processor.h | 2 | Pitch shifting in feedback path |
| DiffusionNetwork | dsp/processors/diffusion_network.h | 2 | Smear/reverb texture in feedback |
| FeedbackNetwork | dsp/systems/feedback_network.h | 3 | Feedback loop with filtering/limiting |
| DelayEngine | dsp/systems/delay_engine.h | 3 | Primary delay with tempo sync |
| ModulationMatrix | dsp/systems/modulation_matrix.h | 3 | Parameter modulation routing |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| BlockContext | dsp/core/block_context.h | 0 | Tempo and transport info |
| dbToGain/gainToDb | dsp/core/db_utils.h | 0 | dB conversions |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No shimmer-related utilities
- [x] `src/dsp/core/` - Layer 0 utilities available
- [x] `src/dsp/features/` - No existing shimmer implementation
- [x] `ARCHITECTURE.md` - Reviewed component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: ShimmerDelay is a new class name not found in codebase. This is a pure composition feature with no new utility functions - all math/audio utilities already exist in Layer 0-2. The only new code is the ShimmerDelay class itself.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | ✓ |
| BlockContext | isPlaying | `bool isPlaying = false` | ✓ |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | ✓ |
| DelayEngine | prepare | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept` | ✓ |
| DelayEngine | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` | ✓ |
| DelayEngine | setTimeMode | `void setTimeMode(TimeMode mode) noexcept` | ✓ |
| DelayEngine | setNoteValue | `void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept` | ✓ |
| DelayEngine | process | `void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept` | ✓ |
| FeedbackNetwork | prepare | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept` | ✓ |
| FeedbackNetwork | setFeedbackAmount | `void setFeedbackAmount(float amount) noexcept` | ✓ |
| FeedbackNetwork | setFilterEnabled | `void setFilterEnabled(bool enabled) noexcept` | ✓ |
| FeedbackNetwork | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | ✓ |
| FeedbackNetwork | setSaturationEnabled | `void setSaturationEnabled(bool enabled) noexcept` | ✓ |
| FeedbackNetwork | process | `void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept` | ✓ |
| PitchShiftProcessor | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | ✓ |
| PitchShiftProcessor | setSemitones | `void setSemitones(float semitones) noexcept` | ✓ |
| PitchShiftProcessor | setCents | `void setCents(float cents) noexcept` | ✓ |
| PitchShiftProcessor | setMode | `void setMode(PitchMode mode) noexcept` | ✓ |
| PitchShiftProcessor | process | `void process(const float* input, float* output, std::size_t numSamples) noexcept` | ✓ |
| PitchShiftProcessor | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | ✓ |
| DiffusionNetwork | prepare | `void prepare(float sampleRate, size_t maxBlockSize) noexcept` | ✓ |
| DiffusionNetwork | setSize | `void setSize(float sizePercent) noexcept` | ✓ |
| DiffusionNetwork | setDensity | `void setDensity(float densityPercent) noexcept` | ✓ |
| DiffusionNetwork | process | `void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples) noexcept` | ✓ |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate) noexcept` | ✓ |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | ✓ |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | ✓ |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | ✓ |

### Header Files Read

- [x] `src/dsp/core/block_context.h` - BlockContext struct
- [x] `src/dsp/systems/delay_engine.h` - DelayEngine class
- [x] `src/dsp/systems/feedback_network.h` - FeedbackNetwork class
- [x] `src/dsp/processors/pitch_shift_processor.h` - PitchShiftProcessor class
- [x] `src/dsp/processors/diffusion_network.h` - DiffusionNetwork class
- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| PitchShiftProcessor | process() is mono only | Call twice for L/R channels |
| DiffusionNetwork | prepare() takes `float` sampleRate | Cast from double |
| FeedbackNetwork | Saturation must be enabled for >100% feedback safety | `setSaturationEnabled(true)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| All utilities | No new utilities needed - pure composition feature |

**Decision**: No new Layer 0 utilities needed. ShimmerDelay is a pure composition of existing components.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- Reverse Delay (Week 19) - may share feedback-with-processing pattern
- Granular Delay (Week 20) - different processing paradigm
- Freeze Mode (future) - could share feedback path with pitch

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Feedback-with-processor pattern | MEDIUM | Reverse Delay, Freeze | Keep local, document pattern |
| Shimmer mix blend logic | LOW | Shimmer-specific | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Each Layer 4 feature has unique processing needs |
| Keep composition local | Pattern documented but not abstracted |

### Review Trigger

After implementing **Reverse Delay (Week 19)**, review this section:
- [ ] Does Reverse need pitch-in-feedback pattern? → Consider shared utility
- [ ] Does Reverse use same composition pattern? → Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/029-shimmer-delay/
├── plan.md              # This file
├── spec.md              # Feature specification
├── checklists/          # Quality checklists
└── tasks.md             # Generated by /speckit.tasks
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── features/
        └── shimmer_delay.h     # ShimmerDelay class (header-only like other L4 features)

tests/
└── unit/
    └── features/
        └── shimmer_delay_test.cpp  # Unit tests for ShimmerDelay
```

**Structure Decision**: Single header file following the pattern established by other Layer 4 features (tape_delay.h, bbd_delay.h, etc.). Header-only implementation for inline optimization.

## Signal Flow Design

The shimmer effect requires a specific signal flow to achieve the cascading pitch effect:

```
Input ──┬─────────────────────────────────────────────────────┬─── Mix ─── Output
        │                                                     │
        │                     ┌───────────────────────────────┘
        │                     │
        └──► DelayEngine ──► FeedbackNetwork ──┬─────────────────────────► (back to delay input)
                                               │
                            ┌──────────────────┘
                            │
             ┌──────────────┴──────────────┐
             │        Shimmer Mix          │
             │   (blend pitched/unpitched) │
             └──────────────┬──────────────┘
                            │
             ┌──────────────┴──────────────┐
             │     PitchShiftProcessor     │
             │   (pitched portion only)    │
             └──────────────┬──────────────┘
                            │
             ┌──────────────┴──────────────┐
             │      DiffusionNetwork       │
             │   (smear for reverb-like)   │
             └─────────────────────────────┘
```

**Key insight**: The pitch shifter operates on the feedback signal, not the dry input. This creates the cascading octave effect where each repetition is pitched higher than the last.

## Complexity Tracking

> No constitution violations to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
