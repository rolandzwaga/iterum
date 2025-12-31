# Implementation Plan: Spectral Delay Tempo Sync

**Branch**: `041-spectral-tempo-sync` | **Date**: 2025-12-31 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/041-spectral-tempo-sync/spec.md`

## Summary

Add tempo synchronization to Spectral Delay mode, enabling the base delay to lock to musical note divisions (1/32 through 1/1 with triplets) in addition to the existing free millisecond mode. When in Synced mode, the Base Delay control is hidden and a Note Value dropdown is shown instead. This follows the established pattern from Digital Delay (026) and Granular Delay (038).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, VSTGUI, existing Iterum DSP framework
**Storage**: VST3 state persistence via IBStreamer
**Testing**: Catch2 (DSP tests), pluginval (validation)
**Target Platform**: Windows x64, macOS x64/ARM64, Linux x64
**Project Type**: VST3 Plugin (single project)
**Performance Goals**: < 3% CPU for Spectral Delay (already met per spec 033)
**Constraints**: Real-time audio thread safety, no allocations in process()
**Scale/Scope**: Adds 2 parameters (Time Mode, Note Value), 1 visibility controller

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
- [x] Uses VSTGUI VisibilityController (already cross-platform)
- [x] No platform-specific APIs needed

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None - reusing existing components

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TimeMode | `grep -r "enum.*TimeMode" src/` | Yes | Reuse from delay_engine.h |
| NoteValueMapping | `grep -r "NoteValueMapping" src/` | Yes | Reuse from note_value.h |

**Utility Functions to be created**: None - reusing existing

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dropdownToDelayMs | `grep -r "dropdownToDelayMs" src/` | Yes | note_value.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| TimeMode enum | dsp/systems/delay_engine.h | 3 | Enum for Free/Synced mode selection |
| dropdownToDelayMs() | dsp/core/note_value.h | 0 | Convert note index + tempo to ms |
| kNoteValueDropdownMapping | dsp/core/note_value.h | 0 | UI dropdown labels (10 options) |
| VisibilityController | controller/controller.cpp | - | UI visibility based on param value |
| BlockContext | dsp/core/block_context.h | 0 | Access tempoBPM for calculations |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Already used by SpectralDelay |

### Files Checked for Conflicts

- [x] `src/dsp/core/note_value.h` - TimeMode, dropdownToDelayMs exist and will be reused
- [x] `src/dsp/systems/delay_engine.h` - TimeMode enum exists here
- [x] `src/dsp/features/spectral_delay.h` - Component to extend
- [x] `src/parameters/spectral_params.h` - Parameter pack to extend
- [x] `src/plugin_ids.h` - IDs 200-210 used, 211+ available for new params

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All required components already exist and will be reused. No new classes or utility functions are needed. The TimeMode enum is used by multiple delay modes and SpectralDelay will simply include the same header. This is the 4th delay mode to implement tempo sync using the same pattern (Digital, PingPong, Granular).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | ✓ |
| dropdownToDelayMs | (function) | `[[nodiscard]] inline constexpr float dropdownToDelayMs(int dropdownIndex, double tempoBPM) noexcept` | ✓ |
| TimeMode | Free/Synced | `enum class TimeMode : uint8_t { Free, Synced }` | ✓ |
| SpectralDelay | setBaseDelayMs | `void setBaseDelayMs(float ms) noexcept` | ✓ |
| SpectralDelay | kMaxDelayMs | `static constexpr float kMaxDelayMs = 2000.0f` | ✓ |

### Header Files Read

- [x] `src/dsp/core/block_context.h` - BlockContext struct with tempoBPM member
- [x] `src/dsp/core/note_value.h` - dropdownToDelayMs function signature
- [x] `src/dsp/systems/delay_engine.h` - TimeMode enum definition
- [x] `src/dsp/features/spectral_delay.h` - SpectralDelay class API
- [x] `src/dsp/features/granular_delay.h` - Reference implementation for tempo sync pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| dropdownToDelayMs | Returns float in milliseconds | Direct use, no conversion needed |
| TimeMode | Defined in delay_engine.h, not note_value.h | Include from systems/delay_engine.h |
| Note dropdown index | 0-9, default 4 = 1/8 note | Match granular_params.h pattern |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| — | No new member functions needed - reusing existing pattern |

**Decision**: No new utilities needed. All tempo sync functionality exists in Layer 0 (note_value.h) and is reused.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer**:
- Digital Delay: Already has tempo sync (reference implementation)
- PingPong Delay: Already has tempo sync
- Granular Delay: Already has tempo sync
- Any future delay modes: Would use same pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Tempo sync pattern | HIGH (already shared) | All delay modes | Pattern already documented |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Follow granular_delay.h pattern exactly | 4th implementation of same pattern - consistency maximizes maintainability |
| Use existing VisibilityController | Already works for Digital, PingPong, Granular delay modes |

## Project Structure

### Documentation (this feature)

```text
specs/041-spectral-tempo-sync/
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal - pattern well-established)
├── quickstart.md        # Implementation checklist
└── tasks.md             # Task list (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── plugin_ids.h                     # Add kSpectralTimeModeId, kSpectralNoteValueId
├── parameters/spectral_params.h     # Add timeMode, noteValue atomics + handlers
├── dsp/features/spectral_delay.h    # Add tempo sync to process()
├── controller/controller.h          # Add spectralBaseDelayVisibilityController_
├── controller/controller.cpp        # Create VisibilityController for spectral
└── resources/editor.uidesc          # Add Time Mode dropdown, Note Value dropdown, control-tags

tests/
├── unit/features/spectral_delay_test.cpp  # Add tempo sync tests
└── vst/spectral_params_test.cpp           # Add parameter tests (optional)
```

**Structure Decision**: Standard VST3 plugin structure. Feature adds parameters to existing spectral_params.h and extends SpectralDelay class.

## Implementation Phases

### Phase 1: DSP Layer (SpectralDelay class)

1. Add TimeMode enum include to spectral_delay.h
2. Add timeMode_ and noteValueIndex_ members
3. Add setTimeMode() and setNoteValue() methods
4. Modify process() to calculate base delay from tempo when synced
5. Write tests for tempo sync accuracy at various BPM values

### Phase 2: Parameter Layer (spectral_params.h + plugin_ids.h)

1. Add kSpectralTimeModeId (211) and kSpectralNoteValueId (212) to plugin_ids.h
2. Add timeMode and noteValue atomics to SpectralParams struct
3. Add handleSpectralParamChange cases for new params
4. Add registerSpectralParams entries for dropdowns
5. Add formatSpectralParam cases
6. Add save/load entries
7. Add sync entries

### Phase 3: UI Layer (editor.uidesc + controller)

1. Add control-tags for Time Mode, Note Value, BaseDelayLabel
2. Add VisibilityController for spectral base delay controls
3. Add COptionMenu controls to SpectralPanel in editor.uidesc
4. Test visibility toggle behavior

### Phase 4: Integration & Validation

1. Run all tests
2. Run pluginval at strictness level 5
3. Manual testing in DAW
4. Commit

## Complexity Tracking

No constitution violations - this feature follows an established, proven pattern.
