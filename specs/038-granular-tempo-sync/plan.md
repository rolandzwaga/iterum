# Implementation Plan: Granular Delay Tempo Sync

**Branch**: `038-granular-tempo-sync` | **Date**: 2025-12-30 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/038-granular-tempo-sync/spec.md`

## Summary

Add tempo synchronization to the Granular Delay mode, allowing the grain position (delay time) parameter to lock to musical note divisions based on host tempo. This follows the established pattern used by Digital Delay, PingPong Delay, and Reverse Delay modes.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, VSTGUI, existing DSP layer components
**Storage**: N/A (parameters stored via VST3 state persistence)
**Testing**: Catch2 (via dsp_tests and vst_tests)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 plugin
**Performance Goals**: < 0.5% CPU for tempo sync calculations per block
**Constraints**: Real-time safe, no allocations in process(), < 1 buffer parameter latency
**Scale/Scope**: Adding 2 new parameters (TimeMode, NoteValue) to existing mode

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific code required
- [x] Uses existing cross-platform VSTGUI abstractions

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None new - extending existing GranularParams and GranularDelay

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| GranularParams | `grep -r "struct GranularParams" src/` | Yes | Extend with timeMode, noteValue |
| GranularDelay | `grep -r "class GranularDelay" src/` | Yes | Add setTimeMode(), setNoteValue() |

**Utility Functions to be created**: None - reusing existing Layer 0 utilities

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| noteToDelayMs | `grep -r "noteToDelayMs" src/` | Yes | dsp/core/note_value.h | Reuse |
| dropdownToDelayMs | `grep -r "dropdownToDelayMs" src/` | Yes | dsp/core/note_value.h | Reuse |
| getNoteValueFromDropdown | `grep -r "getNoteValueFromDropdown" src/` | Yes | dsp/core/note_value.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| TimeMode enum | dsp/systems/delay_engine.h | 3 | Reuse for Free/Synced selection |
| NoteValue enum | dsp/core/note_value.h | 0 | Reuse for note division selection |
| NoteModifier enum | dsp/core/note_value.h | 0 | Reuse for dotted/triplet modifiers |
| noteToDelayMs() | dsp/core/note_value.h | 0 | Convert note+tempo to milliseconds |
| dropdownToDelayMs() | dsp/core/note_value.h | 0 | Direct dropdown index to delay time |
| getNoteValueFromDropdown() | dsp/core/note_value.h | 0 | Map dropdown index to NoteValueMapping |
| kNoteValueDropdownMapping | dsp/core/note_value.h | 0 | Standard tempo sync dropdown order |
| GranularDelay | dsp/features/granular_delay.h | 4 | Component to extend |
| GranularParams | parameters/granular_params.h | N/A | Parameter pack to extend |
| createDropdownParameterWithDefault | controller/parameter_helpers.h | N/A | Create StringListParameter |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No granular tempo sync code
- [x] `src/dsp/core/note_value.h` - Contains reusable tempo sync utilities
- [x] `src/dsp/systems/delay_engine.h` - Contains TimeMode enum to reuse
- [x] `src/dsp/features/granular_delay.h` - Component to extend (no existing tempo sync)
- [x] `src/parameters/granular_params.h` - Parameter pack to extend

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All types being extended already exist and are uniquely defined. No new classes or structs are being created. We are adding parameters to existing structures and methods to existing classes following the established pattern from Digital Delay.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | ✓ |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | ✓ |
| noteToDelayMs | function | `[[nodiscard]] inline constexpr float noteToDelayMs(NoteValue note, NoteModifier modifier, double tempoBPM) noexcept` | ✓ |
| dropdownToDelayMs | function | `[[nodiscard]] inline constexpr float dropdownToDelayMs(int dropdownIndex, double tempoBPM) noexcept` | ✓ |
| getNoteValueFromDropdown | function | `[[nodiscard]] inline constexpr NoteValueMapping getNoteValueFromDropdown(int dropdownIndex) noexcept` | ✓ |
| GranularEngine | setPosition | `void setPosition(float ms) noexcept` (via GranularDelay::setDelayTime) | ✓ |
| TimeMode | enum | `enum class TimeMode : uint8_t { Free, Synced }` | ✓ |

### Header Files Read

- [x] `src/dsp/core/block_context.h` - BlockContext struct
- [x] `src/dsp/core/note_value.h` - NoteValue, NoteModifier, noteToDelayMs, dropdownToDelayMs
- [x] `src/dsp/systems/delay_engine.h` - TimeMode enum
- [x] `src/dsp/features/granular_delay.h` - GranularDelay class
- [x] `src/parameters/granular_params.h` - GranularParams struct
- [x] `src/parameters/digital_params.h` - Reference implementation pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| noteToDelayMs | Returns float milliseconds, not samples | Use result directly for setDelayTime(ms) |
| Dropdown index | Standard order is shortest to longest (0=1/32, 9=1/1) | Use kNoteValueDropdownMapping array |
| StringListParameter | Required for dropdown UI controls | Use createDropdownParameterWithDefault() |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

No new Layer 0 utilities needed - all tempo sync functionality already exists in `note_value.h`.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| updatePositionFromTempo() | Will be a private method in GranularDelay, specific to this feature |

**Decision**: No extraction needed. The feature reuses existing Layer 0 utilities.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md or known plans):
- Spectral Delay - could potentially benefit from similar tempo sync for delay time
- Any future delay modes

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Tempo sync parameter pattern | HIGH | Spectral Delay, future modes | Already established pattern in Digital/PingPong - no new extraction needed |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No new shared code | Tempo sync pattern already established via Layer 0 note_value.h utilities |
| Follow Digital Delay pattern | Proven implementation, consistent UX across delay modes |

## Project Structure

### Documentation (this feature)

```text
specs/038-granular-tempo-sync/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Phase 2 output (generated by /speckit.tasks)
```

### Source Code Changes

```text
src/
├── plugin_ids.h                      # Add kGranularTimeModeId, kGranularNoteValueId
├── parameters/
│   └── granular_params.h             # Add timeMode, noteValue atomics + handlers
├── dsp/
│   └── features/
│       └── granular_delay.h          # Add setTimeMode(), setNoteValue(), process with tempo sync
└── controller/
    └── controller.cpp                # Register new parameters (already auto-registered via granular_params.h)

resources/
└── editor.uidesc                     # Add TimeMode dropdown, NoteValue dropdown

tests/
└── unit/
    ├── features/
    │   └── granular_delay_tempo_sync_test.cpp  # DSP unit tests
    └── vst/
        └── granular_tempo_sync_ui_test.cpp     # UI E2E tests
```

**Structure Decision**: Minimal changes to existing structure. Two new parameter IDs (113, 114), parameter handling extensions, and DSP logic updates.

## Complexity Tracking

> No constitution violations identified. Implementation follows established patterns.

| Aspect | Assessment |
|--------|------------|
| New types created | 0 (reusing existing enums) |
| Files modified | 4-5 (plugin_ids.h, granular_params.h, granular_delay.h, editor.uidesc, controller.cpp) |
| New test files | 2 (DSP unit tests + UI E2E tests) |
| Risk level | Low (follows proven pattern from Digital Delay) |
