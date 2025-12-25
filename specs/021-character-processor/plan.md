# Implementation Plan: Character Processor

**Branch**: `021-character-processor` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/021-character-processor/spec.md`

## Summary

Layer 3 system component that applies analog character/coloration to audio signals. Provides four distinct modes (Tape, BBD, Digital Vintage, Clean) that compose existing Layer 1-2 processors (SaturationProcessor, NoiseGenerator, MultimodeFilter, LFO) into cohesive character presets. Smooth mode transitions via crossfading and per-mode parameter controls for fine-tuning.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, existing Layer 1-2 DSP components
**Storage**: N/A (stateless processor, state managed by host)
**Testing**: Catch2 (per specs/TESTING-GUIDE.md)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project - VST3 plugin
**Performance Goals**: < 1% CPU per instance at 44.1kHz stereo (SC-003)
**Constraints**: Real-time safe (noexcept, no allocations in process), 50ms mode transitions
**Scale/Scope**: Single character processor class composing 4-6 internal components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | Will use noexcept, pre-allocated buffers |
| III. Modern C++ | PASS | C++20, RAII, value semantics |
| IX. Layered Architecture | PASS | Layer 3 composing Layer 1-2 only |
| X. DSP Constraints | PASS | Oversampling via SaturationProcessor |
| XI. Performance Budgets | PASS | Target < 1% CPU (Layer 3 budget) |
| XII. Test-First | PASS | Tests before implementation |
| XIV. ODR Prevention | PASS | See codebase research below |
| XV. Honest Completion | PASS | Will verify all FR/SC at completion |

**Required Check - Principle XII (Test-First Development):**
- [X] Tasks will include TESTING-GUIDE.md context verification step
- [X] Tests will be written BEFORE implementation code
- [X] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [X] Codebase Research section below is complete
- [X] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| CharacterProcessor | `grep -r "class CharacterProcessor" src/` | No | Create New (Layer 3) |
| CharacterMode | `grep -r "CharacterMode" src/` | No | Create New (enum) |
| BitCrusher | `grep -r "class BitCrusher" src/` | No | Create New (Layer 1 primitive) |
| SampleRateReducer | `grep -r "class SampleRateReducer" src/` | No | Create New (Layer 1 primitive) |
| TapeCharacter | `grep -r "TapeCharacter" src/` | No | Create New (internal to CharacterProcessor) |
| BBDCharacter | `grep -r "BBDCharacter" src/` | No | Create New (internal to CharacterProcessor) |
| DigitalVintageCharacter | `grep -r "DigitalVintage" src/` | No | Create New (internal to CharacterProcessor) |

**Note**: BitCrusher and SampleRateReducer are general-purpose lo-fi DSP primitives that will be created as Layer 1 components for reuse in future effects.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SaturationProcessor | dsp/processors/saturation_processor.h | 2 | Tape/BBD saturation curves |
| NoiseGenerator | dsp/processors/noise_generator.h | 2 | Tape hiss, BBD clock noise |
| MultimodeFilter | dsp/processors/multimode_filter.h | 2 | EQ rolloff, bandwidth limiting |
| LFO | dsp/primitives/lfo.h | 1 | Wow/flutter modulation |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing, mode crossfade |
| dbToGain/gainToDb | dsp/core/db_utils.h | 0 | dB to linear conversion |

### New Layer 1 Primitives to Create

| Component | Location | Purpose | Reuse Potential |
|-----------|----------|---------|-----------------|
| BitCrusher | dsp/primitives/bit_crusher.h | Bit depth reduction with optional dither | Lo-fi effects, distortion, retro emulation |
| SampleRateReducer | dsp/primitives/sample_rate_reducer.h | Sample-and-hold for SR reduction | Lo-fi effects, aliasing artifacts, retro emulation |

### Files Checked for Conflicts

- [X] `src/dsp/dsp_utils.h` - No character-related classes
- [X] `src/dsp/core/` - No bit crushing or sample rate reduction utilities
- [X] `src/dsp/processors/` - No existing character processor

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (CharacterProcessor, CharacterMode, BitCrusher, SampleRateReducer) are unique and not found in codebase. BitCrusher and SampleRateReducer will be created as Layer 1 primitives following established patterns (similar to LFO, Biquad, Smoother).

## Layer 1 Primitive Analysis

*For Layer 3 features: Identify DSP algorithms that should be created as reusable Layer 1 primitives.*

### New Layer 1 Primitives

| Primitive | Why Create as L1? | Proposed Location | Future Consumers |
|-----------|-------------------|-------------------|------------------|
| BitCrusher | General-purpose lo-fi effect, reusable across multiple features | dsp/primitives/bit_crusher.h | Lo-fi processor, distortion effects, retro emulation |
| SampleRateReducer | General-purpose lo-fi effect, reusable across multiple features | dsp/primitives/sample_rate_reducer.h | Lo-fi processor, aliasing effects, retro emulation |

### Utilities to Keep Internal

| Function | Why Keep Internal? |
|----------|-------------------|
| TapeCharacter | Mode-specific composition of L1/L2 components |
| BBDCharacter | Mode-specific composition of L1/L2 components |
| DigitalVintageCharacter | Mode-specific composition of L1/L2 components |

**Decision**: BitCrusher and SampleRateReducer will be created as Layer 1 primitives in `dsp/primitives/` for reuse. Mode-specific character classes remain internal to CharacterProcessor.

## Project Structure

### Documentation (this feature)

```text
specs/021-character-processor/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API headers)
│   └── character_processor.h
├── checklists/          # Quality checklists
│   └── requirements.md
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
src/dsp/primitives/
├── bit_crusher.h              # Layer 1: Bit depth reduction primitive
└── sample_rate_reducer.h      # Layer 1: Sample rate reduction primitive

src/dsp/systems/
└── character_processor.h      # Layer 3: Main implementation (header-only)

tests/unit/primitives/
├── bit_crusher_test.cpp       # Layer 1 primitive tests
└── sample_rate_reducer_test.cpp

tests/unit/systems/
└── character_processor_test.cpp  # Layer 3 system tests
```

**Structure Decision**:
- BitCrusher and SampleRateReducer as Layer 1 primitives in `dsp/primitives/` (reusable)
- CharacterProcessor as Layer 3 system in `dsp/systems/` (composes primitives)

## Complexity Tracking

> No constitution violations to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
