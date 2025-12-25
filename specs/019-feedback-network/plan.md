# Implementation Plan: FeedbackNetwork

**Branch**: `019-feedback-network` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/019-feedback-network/spec.md`

## Summary

FeedbackNetwork is a Layer 3 system component that manages feedback loops for delay effects. It wraps DelayEngine (018) and composes MultimodeFilter (008) and SaturationProcessor (009) in the feedback path. Key features include:
- Feedback amount 0-120% (with soft limiting for self-oscillation)
- Filter in feedback path (LP/HP/BP) for tone shaping
- Saturation in feedback path for warmth
- Freeze mode (100% feedback + muted input)
- Stereo cross-feedback routing (ping-pong)

This spec also creates a reusable Layer 0 utility `stereoCrossBlend()` for cross-routing that will be used by future specs (022, 023).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- DelayEngine (Layer 3, 018-delay-engine)
- MultimodeFilter (Layer 2, 008-multimode-filter)
- SaturationProcessor (Layer 2, 009-saturation-processor)
- OnePoleSmoother (Layer 1, src/dsp/primitives/smoother.h)
- BlockContext (Layer 0, src/dsp/core/block_context.h)

**Storage**: N/A (all state is runtime, pre-allocated)
**Testing**: Catch2 (see specs/TESTING-GUIDE.md for patterns)
**Target Platform**: VST3 plugin (Windows/macOS/Linux)
**Project Type**: Single project (VST plugin)
**Performance Goals**: <1% CPU per FeedbackNetwork instance at 44.1kHz stereo (Constitution XI)
**Constraints**:
- All process() methods must be noexcept and allocation-free (Constitution II)
- 20ms parameter smoothing for click-free transitions
- Maximum delay support: 10 seconds at 192kHz
**Scale/Scope**: Single component ~300 LOC, ~25 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All process() methods will be noexcept
- [x] No allocations in audio path (pre-allocate in prepare())
- [x] No locks/mutexes in process()
- [x] Parameter changes via atomic with relaxed ordering

**Required Check - Principle IX (Layered Architecture):**
- [x] FeedbackNetwork is Layer 3 - composes from Layer 0-2 only
- [x] stereoCrossBlend() is Layer 0 - no dependencies above Layer 0
- [x] No circular dependencies

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Feedback limiting for >100% values (uses SaturationProcessor)
- [x] DC blocking considered (SaturationProcessor handles this)
- [x] Parameter smoothing (20ms via OnePoleSmoother)

**Required Check - Principle XI (Performance Budgets):**
- [x] Target <1% CPU per instance
- [x] Will validate with profiling in Release builds

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [x] All FR/SC will be explicitly verified before claiming completion
- [x] No placeholder implementations will be claimed as complete

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FeedbackNetwork | `grep -r "class FeedbackNetwork" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| stereoCrossBlend | `grep -r "stereoCrossBlend" src/` | No | N/A | Create New in src/dsp/core/stereo_utils.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayEngine | src/dsp/systems/delay_engine.h | 3 | Wrapped as internal delay line |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | 2 | Filter in feedback path |
| SaturationProcessor | src/dsp/processors/saturation_processor.h | 2 | Saturation in feedback path |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | Smooth feedback/cross-feedback changes |
| BlockContext | src/dsp/core/block_context.h | 0 | Processing context for tempo sync |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No conflicting stereo utilities
- [x] `src/dsp/core/` - No stereo_utils.h exists yet
- [x] `ARCHITECTURE.md` - Component inventory reviewed
- [x] `src/dsp/systems/` - No FeedbackNetwork exists

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (FeedbackNetwork, stereoCrossBlend) do not exist in codebase. Dependencies are verified to exist at expected locations. No naming conflicts detected.

## Project Structure

### Documentation (this feature)

```text
specs/019-feedback-network/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── feedback_network.h  # API contract header
├── checklists/          # Validation checklists
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/
│   ├── stereo_utils.h   # NEW - Layer 0: stereoCrossBlend() utility
│   └── [existing...]
├── systems/
│   ├── delay_engine.h   # Existing - Layer 3: Wrapped by FeedbackNetwork
│   └── feedback_network.h  # NEW - Layer 3: FeedbackNetwork class
└── [existing layers...]

tests/
├── unit/
│   ├── core/
│   │   └── stereo_utils_test.cpp  # NEW - Layer 0 tests
│   └── systems/
│       ├── delay_engine_test.cpp  # Existing
│       └── feedback_network_test.cpp  # NEW - Layer 3 tests
└── [existing...]
```

**Structure Decision**: Follows existing layered architecture. Layer 0 utility goes in `src/dsp/core/`, Layer 3 system component goes in `src/dsp/systems/`.

## Complexity Tracking

No constitution violations requiring justification. All requirements can be met within normal architectural constraints.

## Phase 0: Research

*No NEEDS CLARIFICATION items in Technical Context - minimal research needed.*

### Research Questions

1. **Feedback path signal flow**: Verify optimal order of filter → saturator in feedback path
2. **Freeze mode implementation**: Confirm approach for muting input while maintaining feedback
3. **Cross-feedback matrix**: Verify stereoCrossBlend formula handles all edge cases

### Findings

See [research.md](research.md) for detailed findings.

## Phase 1: Design Artifacts

### Data Model

See [data-model.md](data-model.md) for:
- FeedbackNetwork class structure
- Parameter definitions and ranges
- State machine for freeze mode

### API Contracts

See [contracts/feedback_network.h](contracts/feedback_network.h) for:
- Complete public API with documentation
- Lifecycle methods: prepare(), reset(), process()
- Parameter setters with smoothing behavior

### Usage Examples

See [quickstart.md](quickstart.md) for:
- Basic feedback delay setup
- Filtered feedback (tape-style)
- Freeze mode usage
- Cross-feedback ping-pong

## Next Steps

1. Generate research.md (Phase 0)
2. Generate data-model.md, contracts/, quickstart.md (Phase 1)
3. Run `/speckit.tasks` to generate tasks.md (Phase 2)
4. Run `/speckit.implement` to execute implementation
