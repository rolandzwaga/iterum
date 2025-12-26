# Implementation Plan: Ping-Pong Delay Mode

**Branch**: `027-ping-pong-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/027-ping-pong-delay/spec.md`

## Summary

Ping-Pong Delay Mode is a Layer 4 user feature that provides classic stereo ping-pong delay with alternating left/right bounces. The implementation composes Layer 0-3 components: DelayLine (x2 for independent L/R timing), LFO (x2 for stereo modulation), DynamicsProcessor (for feedback limiting), and stereoCrossBlend (for cross-feedback routing). Key features include L/R timing ratios (1:1, 2:1, 3:2, etc.), adjustable cross-feedback (0-100%), stereo width (0-200%), tempo sync, and optional modulation.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: VST3 SDK, Layer 0-3 DSP components
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (test-first per Constitution Principle XII)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 Plugin - DSP Feature
**Performance Goals**: < 1% CPU per instance at 44.1kHz stereo (Layer 4 budget)
**Constraints**: Real-time safe (no allocations in process), < 10s max delay at 192kHz
**Scale/Scope**: Single Layer 4 feature class with ~600 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process() methods
- [x] All functions are noexcept
- [x] Pre-allocate buffers in prepare()

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 4 feature depends only on Layer 0-3
- [x] No circular dependencies

**Required Check - Principle X (DSP Constraints):**
- [x] Parameter smoothing (20ms one-pole)
- [x] Feedback limiting for > 100%
- [x] Click-free delay time changes

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
| PingPongDelay | `grep -r "class PingPongDelay" src/` | No | Create New |
| LRRatio (enum) | `grep -r "enum.*LRRatio" src/` | No* | Create New |

*Note: StereoField has a continuous `lrRatio_` float member (0.1-10.0), but we need discrete preset enum.

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| stereoCrossBlend | `grep -r "stereoCrossBlend" src/` | Yes | stereo_utils.h | Reuse |
| msToSamples | `grep -r "msToSamples" src/` | Yes | Multiple | Keep as member (one-liner) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/primitives/delay_line.h | 1 | 2 instances for L/R delay buffers |
| LFO | dsp/primitives/lfo.h | 1 | 2 instances for stereo modulation |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | 8 instances for parameter smoothing |
| DynamicsProcessor | dsp/processors/dynamics_processor.h | 2 | Feedback limiting |
| stereoCrossBlend | dsp/core/stereo_utils.h | 0 | Cross-channel feedback routing |
| BlockContext | dsp/core/block_context.h | 0 | Tempo sync |
| NoteValue | dsp/core/note_value.h | 0 | Tempo sync note values |
| dbToGain | dsp/core/db_utils.h | 0 | Level conversions |
| TimeMode | dsp/systems/delay_engine.h | 3 | Reuse enum for free/synced mode |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No conflicts found
- [x] `src/dsp/core/` - Utilities identified for reuse
- [x] `ARCHITECTURE.md` - Component inventory reviewed
- [x] `src/dsp/features/digital_delay.h` - Reference pattern for Layer 4

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: PingPongDelay is a unique class name not found in codebase. The LRRatio enum is new (StereoField uses continuous float, not enum). All other types are reused from existing components with proper includes.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

No new Layer 0 utilities needed. All required utilities already exist (stereoCrossBlend, dbToGain, etc.).

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples() | One-liner, class stores sampleRate_, exists in multiple classes already |
| getRatioMultipliers() | Specific to LRRatio enum, only used by PingPongDelay |

**Decision**: No Layer 0 extractions needed. Keep ratio multiplier logic as member/helper.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- Multi-Tap Delay: Multiple delay taps with individual times
- Shimmer Delay: Pitch-shifted feedback
- Ducking Delay: Sidechain-triggered delay
- Tape Delay: Analog tape emulation
- BBD Delay: Analog bucket-brigade emulation

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| LRRatio enum | LOW | Multi-Tap (maybe) | Keep local |
| Cross-feedback pattern | MEDIUM | Already in FeedbackNetwork | Already shared |
| Width (M/S) pattern | MEDIUM | Already in StereoField | Already shared |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | DigitalDelay didn't create one; features are too different |
| Keep LRRatio local | Only PingPong uses discrete ratios; Multi-Tap would use continuous |
| Reuse existing patterns | Cross-feedback and width already exist in Layer 3 |

### Review Trigger

After implementing **Multi-Tap Delay**, review this section:
- [ ] Does Multi-Tap need LRRatio or similar? → Consider extraction
- [ ] Does Multi-Tap use same composition pattern? → Document shared pattern
- [ ] Any duplicated code? → Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/027-ping-pong-delay/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Component research
├── data-model.md        # Entity definitions
├── quickstart.md        # Test scenarios
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Task list (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/features/
└── ping_pong_delay.h    # Layer 4: PingPongDelay class + LRRatio enum

tests/unit/features/
└── ping_pong_delay_test.cpp  # All tests for this feature
```

**Structure Decision**: Single header file following DigitalDelay pattern. All implementation inline in header for template-like optimization.

## Complexity Tracking

No constitution violations requiring justification.
