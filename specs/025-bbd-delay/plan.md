# Implementation Plan: BBD Delay

**Branch**: `025-bbd-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/025-bbd-delay/spec.md`

## Summary

Layer 4 user feature implementing classic bucket-brigade device (BBD) delay emulation. Composes DelayEngine, FeedbackNetwork, CharacterProcessor (BBD mode), and ModulationMatrix. Key behaviors: bandwidth inversely proportional to delay time, compander artifacts (pumping/breathing), clock noise proportional to delay time, limited frequency response (dark character). Emulates vintage units: Boss DM-2, EHX Memory Man, Roland Dimension D.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, Layer 0-3 DSP components
**Storage**: N/A (audio processing only)
**Testing**: Catch2 via CTest *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: VST3 plugin DSP layer
**Performance Goals**: <5% CPU total plugin at 44.1kHz stereo; <1% for this Layer 4 component
**Constraints**: Real-time safe (noexcept, no allocations in process), 10s max delay at 192kHz
**Scale/Scope**: Single Layer 4 feature class (~400-600 LOC), ~30 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All process() methods will be noexcept
- [x] No memory allocation in audio path (buffers allocated in prepare())
- [x] No blocking operations (locks, I/O) in audio path

**Required Check - Principle IX (Layered Architecture):**
- [x] BBDDelay is Layer 4 (User Feature)
- [x] Composes only from Layer 0-3 (DelayEngine, FeedbackNetwork, CharacterProcessor, ModulationMatrix)
- [x] No circular dependencies

**Required Check - Principle X (DSP Constraints):**
- [x] Parameter smoothing (OnePoleSmoother) for click-free changes
- [x] Feedback limiting via soft saturation for >100% feedback
- [x] DC blocking after asymmetric saturation (handled in FeedbackNetwork)

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [x] All FR-xxx requirements will be verified before claiming completion
- [x] No placeholder implementations will be claimed as complete

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

This section prevents One Definition Rule (ODR) violations by documenting existing components that may be reused or would conflict with new implementations.

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| BBDDelay | `grep -r "class BBDDelay" src/` | No | Create New |
| BBDChipModel | `grep -r "BBDChipModel\|enum.*BBD" src/` | No | Create New |
| Compander | `grep -r "class Compander\|compander" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculateBBDBandwidth | `grep -r "calculateBBDBandwidth\|BBDBandwidth" src/` | No | bbd_delay.h | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayEngine | dsp/systems/delay_engine.h | 3 | Core delay with tempo sync |
| FeedbackNetwork | dsp/systems/feedback_network.h | 3 | Feedback path with filtering |
| CharacterProcessor | dsp/systems/character_processor.h | 3 | BBD mode for bandwidth limiting, saturation, clock noise |
| ModulationMatrix | dsp/systems/modulation_matrix.h | 3 | LFO routing for triangle modulation |
| LFO | dsp/primitives/lfo.h | 1 | Triangle waveform for modulation (FR-011) |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| MultimodeFilter | dsp/processors/multimode_filter.h | 2 | Anti-aliasing filter simulation (FR-018) |
| dbToGain | dsp/core/db_utils.h | 0 | dB conversion for output level |
| BlockContext | dsp/core/block_context.h | 0 | Tempo sync support |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No BBD-related classes found
- [x] `src/dsp/core/` - No BBD or compander utilities found
- [x] `src/dsp/features/tape_delay.h` - Reference architecture for Layer 4 composition
- [x] `src/dsp/systems/character_processor.h` - Has CharacterMode::BBD - will leverage

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (BBDDelay, BBDChipModel, Compander) are unique and not found in codebase. The pattern follows TapeDelay exactly. CharacterProcessor already has BBD mode that provides bandwidth limiting, saturation, and clock noise - we compose rather than duplicate.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

See CLAUDE.md "Layer 0 Refactoring Analysis" for decision framework.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | No Layer 0 candidates | — | — |

**Analysis**: The bandwidth calculation formula is BBD-specific (inversely proportional to delay time based on BBD clock frequency relationship). This is unlikely to be needed by other components since it encodes BBD physics. Keep as member function.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateBandwidth(delayMs, eraFactor) | BBD-specific physics formula, unlikely to be reused elsewhere |
| msToSamples(ms) | One-liner, already exists in DelayEngine/FeedbackNetwork |

**Decision**: No Layer 0 extractions needed. BBD-specific formulas are kept in BBDDelay class. The CharacterProcessor::BBD mode already handles the character processing - we just configure it.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- TapeDelay (024): Already implemented - reference pattern
- DigitalDelay (future): Clean digital delay mode
- PingPongDelay (future): Stereo ping-pong with cross-feedback
- MultiTapDelay (future): Multi-head rhythmic delays
- ShimmerDelay (future): Pitch-shifted feedback
- ReverseDelay (future): Reversed audio in delay
- GranularDelay (future): Granular texture delay

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| BBDChipModel enum | LOW | BBDDelay only | Keep local |
| Compander class | MEDIUM | Possibly vintage-style effects | Keep local, extract if 2nd consumer appears |
| Layer 4 composition pattern | HIGH | All sibling delays | Already established by TapeDelay - follow it |

### Detailed Analysis (for HIGH potential items)

**Layer 4 Composition Pattern** (established by TapeDelay):
- Compose from Layer 3 systems (DelayEngine, FeedbackNetwork, CharacterProcessor)
- Parameters mapped to composed components
- Smoothers for all user-facing parameters
- prepare()/reset()/process() lifecycle

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| DigitalDelay | YES | Same pattern, CharacterMode::Clean |
| PingPongDelay | YES | Same pattern, uses cross-feedback from FeedbackNetwork |
| ShimmerDelay | YES | Same pattern, adds pitch shifter |

**Recommendation**: Follow TapeDelay pattern exactly. No new shared abstractions needed.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Follow TapeDelay pattern | Pattern already established, proven to work |
| Keep BBDChipModel local | BBD-specific, unlikely to be needed elsewhere |
| Keep Compander local for now | May be useful for future vintage effects, but wait for 2nd consumer |

### Review Trigger

After implementing **DigitalDelay**, review this section:
- [ ] Does DigitalDelay use same composition pattern? → Document as standard
- [ ] Any duplicated parameter handling? → Consider shared utilities
- [ ] Compander needed? → Extract if so

## Project Structure

### Documentation (this feature)

```text
specs/025-bbd-delay/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output (BBD physics, chip characteristics)
├── quickstart.md        # Usage examples and quick reference
├── checklists/
│   └── requirements.md  # Spec validation checklist
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── features/
        └── bbd_delay.h      # BBDDelay class (Layer 4)

tests/
└── unit/
    └── features/
        └── bbd_delay_test.cpp  # Unit tests for BBDDelay
```

**Structure Decision**: Single header file in `src/dsp/features/` following TapeDelay pattern. Tests in `tests/unit/features/`. No additional source files needed - composes from existing Layer 3 components.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

No violations. All Constitution principles satisfied:
- Principle II: Real-time safe (noexcept, pre-allocated buffers)
- Principle IX: Layer 4 composing Layer 0-3 only
- Principle X: DSP constraints (smoothing, limiting, DC blocking)
- Principle XII: Test-first development
- Principle XV: Honest completion with compliance table

## Design Notes

### BBD Physics and Bandwidth Tracking (FR-014 to FR-018)

Real BBD chips use a clock signal to shift samples through the bucket stages. The clock frequency determines both:
1. **Delay time**: Lower clock = longer delay
2. **Bandwidth**: Lower clock = lower Nyquist frequency = more limited bandwidth

**Bandwidth formula** (FR-017):
```
bandwidth = clockFrequency / 2 (Nyquist)
clockFrequency = numStages / delayTime

For MN3005 (4096 stages):
- At 20ms: clock = 4096 / 0.020s = 204.8kHz → bandwidth ≈ 102.4kHz (clamped to 15kHz)
- At 1000ms: clock = 4096 / 1.0s = 4.096kHz → bandwidth ≈ 2.048kHz
```

### Era/Chip Model Characteristics (FR-024 to FR-029)

| Chip | Stages | Max Delay | Character |
|------|--------|-----------|-----------|
| MN3005 | 4096 | 205ms (x2 cascaded) | Wide bandwidth, low noise - Memory Man |
| MN3007 | 1024 | 51ms | Medium-dark - common in pedals |
| MN3205 | 4096 | Same as MN3005 | Darker, more noise - budget chip |
| SAD1024 | 1024 | 51ms | Most noise, limited BW - early chip |

The Era selector adjusts:
- `bandwidthFactor`: Multiplier for base bandwidth (1.0 for MN3005 down to 0.6 for SAD1024)
- `noiseFactor`: Multiplier for clock noise level
- `saturationFactor`: Multiplier for saturation drive

### Compander Emulation (FR-030 to FR-032)

Real BBD delays use companding (compression before, expansion after) to improve S/N ratio. This creates:
- **Attack softening**: Compressor smooths transients going in
- **Release pumping**: Expander creates "breathing" effect on decay

Implementation: Simple envelope follower with attack/release, applied as gain modulation.
Intensity scales with Age parameter (FR-022).

### Modulation Implementation (FR-009 to FR-013)

Uses ModulationMatrix with:
- Source: LFO set to Triangle waveform (FR-011)
- Destination: Delay time
- Depth: User "Modulation" control [0%, 100%]
- Rate: User "Modulation Rate" control [0.1Hz, 10Hz]

Modulation creates pitch variation (chorusing) on the delayed signal.
