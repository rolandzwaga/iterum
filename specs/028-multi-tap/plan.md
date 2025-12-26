# Implementation Plan: Multi-Tap Delay Mode

**Branch**: `028-multi-tap` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/028-multi-tap/spec.md`

## Summary

Layer 4 User Feature implementing rhythmic multi-tap delay patterns with 25 preset patterns (14 rhythmic, 5 mathematical, 6 spatial/level), pattern morphing, and per-tap modulation. Composes TapManager (Layer 3) for tap management, FeedbackNetwork for master feedback, and ModulationMatrix for per-tap modulation.

## Technical Context

**Language/Version**: C++20 (VST3 plugin codebase)
**Primary Dependencies**:
- TapManager (Layer 3) - src/dsp/systems/tap_manager.h
- FeedbackNetwork (Layer 3) - src/dsp/systems/feedback_network.h
- ModulationMatrix (Layer 3) - src/dsp/systems/modulation_matrix.h
- OnePoleSmoother (Layer 1) - src/dsp/primitives/smoother.h
**Storage**: N/A (audio processing feature)
**Testing**: Catch2 (tests/unit/features/)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 plugin (single project)
**Performance Goals**: < 1% CPU at 44.1kHz stereo (Layer 4 budget per SC-007)
**Constraints**: Real-time safe (noexcept, no allocations in process), 16-tap maximum
**Scale/Scope**: Single feature class, 3 Layer 3 dependencies

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

**Classes/Structs to be created**: MultiTapDelay, TimingPattern enum, SpatialPattern enum, TapConfiguration struct

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MultiTapDelay | `grep -r "class MultiTap" src/` | No | Create New |
| TimingPattern | `grep -r "TimingPattern" src/` | No | Create New |
| SpatialPattern | `grep -r "SpatialPattern" src/` | No | Create New |
| TapConfiguration | `grep -r "TapConfiguration" src/` | No | Create New |

**Utility Functions to be created**: None new - reuse existing

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| msToSamples | `grep -r "msToSamples" src/` | Yes | tap_manager.h, feedback_network.h | Keep as member (one-liner) |
| stereoCrossBlend | `grep -r "stereoCrossBlend" src/` | Yes | stereo_utils.h | Reuse from Layer 0 |
| calcPanCoefficients | `grep -r "calcPanCoefficients" src/` | Yes | tap_manager.h | Private member, not reusable |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| TapManager | dsp/systems/tap_manager.h | 3 | Primary - manages 16 taps with time, level, pan, filter |
| FeedbackNetwork | dsp/systems/feedback_network.h | 3 | Master feedback path with filtering and limiting |
| ModulationMatrix | dsp/systems/modulation_matrix.h | 3 | Per-tap parameter modulation (time, level, pan, filter) |
| TapPattern enum | dsp/systems/tap_manager.h | 3 | Existing patterns: QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci |
| NoteValue enum | dsp/core/note_value.h | 0 | Extended patterns via loadNotePattern() |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Pattern morphing smoothing |
| dbToGain | dsp/core/db_utils.h | 0 | Level conversions |
| stereoCrossBlend | dsp/core/stereo_utils.h | 0 | Stereo routing |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No conflicts found
- [x] `src/dsp/core/` - Using existing utilities, no conflicts
- [x] `ARCHITECTURE.md` - TapManager documented, no MultiTapDelay yet
- [x] `src/dsp/systems/tap_manager.h` - Understanding existing TapPattern enum

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (MultiTapDelay, TimingPattern, SpatialPattern) are unique and not found in codebase. No utility functions will be duplicated - all required utilities exist in Layer 0. The feature composes existing Layer 3 components without modification.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | No new Layer 0 utilities needed | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| morphPattern() | Feature-specific, only one consumer |
| applyTimingPattern() | Feature-specific pattern logic |
| applySpatialPattern() | Feature-specific spatial logic |

**Decision**: No new Layer 0 extraction needed. All required utilities (stereoCrossBlend, dbToGain, etc.) already exist. Pattern-specific logic stays in Layer 4 feature class.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md or known plans):
- Ping-Pong Delay (027): Already implemented, uses StereoField + FeedbackNetwork
- Shimmer Mode (future): Will use FeedbackNetwork + PitchShifter
- Reverse Delay (future): May share pattern concepts

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TimingPattern enum | MEDIUM | Shimmer, Reverse Delay | Keep local, extract after 2nd use |
| SpatialPattern enum | MEDIUM | Ping-Pong (already uses StereoField differently) | Keep local |
| Pattern morphing | LOW | Not typical for other features | Keep local |

### Detailed Analysis (for MEDIUM potential items)

**TimingPattern enum** provides:
- Extended rhythmic patterns (14 note values × modifiers)
- Mathematical patterns (Exponential, PrimeNumbers, LinearSpread)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Shimmer Mode | MAYBE | Might use tempo sync but not multi-tap patterns |
| Reverse Delay | MAYBE | Could use timing patterns for reverse point |
| Ping-Pong Delay | NO | Already uses simpler tempo sync |

**Recommendation**: Keep in this feature's file. If Shimmer or Reverse needs same patterns, extract to common location.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First multi-tap feature - patterns not established |
| Keep pattern enums local | Only one consumer currently |
| Compose Layer 3, don't extend | TapManager already provides 90% of functionality |

### Review Trigger

After implementing **Shimmer Mode**, review this section:
- [ ] Does Shimmer need TimingPattern or similar? → Extract to shared location
- [ ] Does Shimmer use same composition pattern? → Document shared pattern
- [ ] Any duplicated code? → Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/028-multi-tap/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (from /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── core/           # Layer 0 (existing, no changes)
│   │   ├── db_utils.h
│   │   ├── note_value.h
│   │   └── stereo_utils.h
│   ├── primitives/     # Layer 1 (existing, no changes)
│   │   └── smoother.h
│   ├── systems/        # Layer 3 (existing, no changes)
│   │   ├── tap_manager.h
│   │   ├── feedback_network.h
│   │   └── modulation_matrix.h
│   └── features/       # Layer 4 (new file)
│       └── multi_tap_delay.h  # NEW: MultiTapDelay class

tests/
└── unit/
    └── features/
        └── multi_tap_delay_test.cpp  # NEW: Test file
```

**Structure Decision**: Single new header file in features/ following existing pattern (ping_pong_delay.h). All implementation inline per project convention for DSP code.

## Complexity Tracking

No constitution violations requiring justification. Feature follows established Layer 4 composition pattern.

## Implementation Approach

### Composition Strategy

MultiTapDelay will compose existing Layer 3 components rather than modifying them:

```cpp
class MultiTapDelay {
    TapManager tapManager_;           // Core tap functionality
    FeedbackNetwork feedbackNetwork_; // Master feedback path
    ModulationMatrix* modMatrix_;     // Optional modulation (external)

    // Layer 4 additions
    TimingPattern currentTimingPattern_;
    SpatialPattern currentSpatialPattern_;
    OnePoleSmoother morphSmoother_;   // For pattern morphing
};
```

### New Patterns (Layer 4 Extensions)

TapManager already provides 5 TapPattern values + loadNotePattern() for any NoteValue. This feature adds:

**Mathematical Patterns** (FR-002a):
- Exponential: 1×, 2×, 4×, 8×... base time
- PrimeNumbers: 2×, 3×, 5×, 7×, 11×... base time
- LinearSpread: Equal spacing from min to max time

**Spatial Patterns** (FR-002b):
- Cascade: Pan L→R across taps
- Alternating: Pan alternates L, R, L, R
- Centered: All taps center
- WideningStereo: Pan spreads progressively
- DecayingLevel: Each tap -3dB from previous
- FlatLevel: All taps equal level

### Pattern Morphing (FR-025, FR-026)

Smooth transitions between patterns using interpolated tap parameters:

```cpp
void morphToPattern(TimingPattern newPattern, float morphTimeMs) {
    // Store current tap times as "from" values
    // Calculate target tap times from newPattern
    // Smoothly interpolate over morphTimeMs
}
```

### Modulation Integration (FR-021, FR-022, FR-023)

Per-tap modulation destinations registered with ModulationMatrix:
- Time: tap[n].time (0-15)
- Level: tap[n].level (16-31)
- Pan: tap[n].pan (32-47)
- FilterCutoff: tap[n].cutoff (48-63)

## Key Design Decisions

1. **Compose, don't inherit**: MultiTapDelay owns TapManager rather than extending it
2. **External ModulationMatrix**: Passed as pointer, not owned (shared resource)
3. **Pattern enums are Layer 4**: New TimingPattern/SpatialPattern stay in feature file
4. **Reuse TapManager patterns**: Use loadNotePattern() for rhythmic patterns
5. **Layer 4 budget**: < 1% CPU including all 3 composed systems
