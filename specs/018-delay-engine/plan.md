# Implementation Plan: DelayEngine

**Branch**: `018-delay-engine` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/018-delay-engine/spec.md`

## Summary

DelayEngine is the foundational Layer 3 system component that wraps the existing DelayLine primitive (Layer 1) with higher-level functionality. It adds time mode selection (Free ms / Synced tempo), smooth parameter changes via OnePoleSmoother, and dry/wet mixing with kill-dry option. This creates the core building block for all delay-based effects in the plugin.

**Technical Approach**: Compose from existing Layer 1 primitives (DelayLine + OnePoleSmoother) and Layer 0 utilities (BlockContext + NoteValue). No new algorithms needed - this is a composition/wrapper pattern.

## Technical Context

**Language/Version**: C++20 (project standard)
**Primary Dependencies**: DelayLine, OnePoleSmoother (Layer 1), BlockContext, NoteValue (Layer 0)
**Storage**: N/A (in-memory audio buffers)
**Testing**: Catch2 (per Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 Plugin (single project with layered DSP library)
**Performance Goals**: < 1% CPU per instance at 44.1kHz stereo (Layer 3 budget per Constitution XI)
**Constraints**: Zero allocations in process(), noexcept audio path
**Scale/Scope**: ~200 LOC, ~15 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All process() methods will be noexcept
- [x] No memory allocation in audio path (use pre-allocated DelayLine)
- [x] No blocking operations (no mutexes, no file I/O)

**Required Check - Principle IX (Layered Architecture):**
- [x] DelayEngine is Layer 3 (System Component)
- [x] Only depends on Layer 0 (BlockContext, NoteValue) and Layer 1 (DelayLine, OnePoleSmoother)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [x] All 12 FRs and 6 SCs will be verified at completion
- [x] No thresholds will be relaxed from spec
- [x] Compliance table will be honestly filled

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: DelayEngine, TimeMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DelayEngine | `grep -r "DelayEngine" src/` | No | Create New |
| TimeMode | `grep -r "TimeMode" src/` | No | Create New |
| class.*Delay | `grep -r "class.*Delay" src/dsp/` | Yes: DelayLine | Wrap, don't duplicate |

**Utility Functions to be created**: None (using existing BlockContext::tempoToSamples)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| tempoToSamples | N/A | Yes | BlockContext | Reuse |
| noteValueToMs | `grep -r "noteValueToMs" src/` | No | N/A | Not needed (use tempoToSamples) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | src/dsp/primitives/delay_line.h | 1 | Core delay buffer - wrap with high-level interface |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | Smooth delay time changes (FR-004) |
| BlockContext | src/dsp/core/block_context.h | 0 | Tempo/sample rate info for synced mode (FR-003) |
| NoteValue | src/dsp/core/note_value.h | 0 | Note value enum for tempo sync (FR-003) |
| getBeatsForNote | src/dsp/core/note_value.h | 0 | Convert NoteValue to beat duration |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No conflicts found
- [x] `src/dsp/core/` - Only Layer 0 utilities, no delay-related code
- [x] `src/dsp/primitives/` - Contains DelayLine (will wrap) and smoothers (will use)
- [x] `src/dsp/processors/` - No delay engines exist
- [x] `src/dsp/systems/` - Directory may need to be created for Layer 3
- [x] `ARCHITECTURE.md` - Reviewed component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (DelayEngine, TimeMode) have no existing definitions in the codebase. The wrapper pattern clearly composes existing components without duplication.

## Project Structure

### Documentation (this feature)

```text
specs/018-delay-engine/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output - class/struct definitions
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - header contract
│   └── delay_engine.h   # API contract for DelayEngine
├── checklists/          # Quality checklists
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/dsp/
├── core/                    # Layer 0 (existing)
│   ├── block_context.h      # Reuse: tempo/sample rate context
│   ├── note_value.h         # Reuse: NoteValue enum
│   └── ...
├── primitives/              # Layer 1 (existing)
│   ├── delay_line.h         # Reuse: wrap this class
│   ├── smoother.h           # Reuse: OnePoleSmoother
│   └── ...
├── processors/              # Layer 2 (existing)
│   └── ...
└── systems/                 # Layer 3 (new directory)
    └── delay_engine.h       # NEW: DelayEngine class

tests/unit/
├── core/                    # Layer 0 tests (existing)
├── primitives/              # Layer 1 tests (existing)
├── processors/              # Layer 2 tests (existing)
└── systems/                 # Layer 3 tests (new directory)
    └── delay_engine_test.cpp  # NEW: DelayEngine tests
```

**Structure Decision**: Single header `delay_engine.h` in new `src/dsp/systems/` directory following the layered architecture. Tests go in corresponding `tests/unit/systems/` directory.

## Complexity Tracking

No Constitution Check violations - no complexity tracking needed.

## Implementation Notes

### Key Design Decisions

1. **Delay time smoothing via OnePoleSmoother**: Changes to delay time (either from setDelayTime() or tempo changes) are smoothed to prevent clicks. FR-004 uses exponential smoothing, not linear ramp, because:
   - Natural response to parameter changes
   - No "tape speed" pitch effect (that's for feedback/crossfade mode later)

2. **Time mode as enum, not runtime polymorphism**: TimeMode::Free vs TimeMode::Synced is a simple enum with if/else in process() - no virtual calls needed for this simple branching.

3. **Dry/wet mix implementation**: Simple linear crossfade between dry (input) and wet (delayed) signals. Kill-dry mode simply zeros the dry coefficient.

4. **BlockContext passed to process()**: Per-block context enables real-time tempo changes without storing stale tempo values.

### Interface Preview

```cpp
namespace Iterum::DSP {

enum class TimeMode : uint8_t {
    Free,    // Delay time in milliseconds
    Synced   // Delay time from NoteValue + BlockContext tempo
};

class DelayEngine {
public:
    // Lifecycle (FR-007, FR-009)
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;

    // Configuration
    void setTimeMode(TimeMode mode) noexcept;
    void setDelayTimeMs(float ms) noexcept;           // Free mode (FR-002)
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept; // Synced mode (FR-003)
    void setMix(float wetRatio) noexcept;             // 0.0-1.0 (FR-005)
    void setKillDry(bool killDry) noexcept;           // FR-006

    // Processing (FR-008)
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept; // Stereo

    // Query
    [[nodiscard]] float getCurrentDelayMs() const noexcept;
    [[nodiscard]] TimeMode getTimeMode() const noexcept;

private:
    DelayLine delayLine_;           // FR-001
    OnePoleSmoother delaySmoother_; // FR-004
    OnePoleSmoother mixSmoother_;   // Smooth mix changes
    // ... configuration state
};

} // namespace Iterum::DSP
```

### Test Strategy

1. **US1 (Free Time Mode)**: Impulse response tests verifying delay timing accuracy
2. **US2 (Synced Mode)**: BlockContext with known tempo, verify sample counts
3. **US3 (Dry/Wet Mix)**: Level tests at 0%, 50%, 100%, and with kill-dry
4. **US4 (State Management)**: Lifecycle tests for prepare/reset
5. **Edge Cases**: 0ms delay, negative values, NaN handling, tempo=0
