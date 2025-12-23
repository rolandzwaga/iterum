# Implementation Plan: Envelope Follower

**Branch**: `010-envelope-follower` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/010-envelope-follower/spec.md`

## Summary

Layer 2 DSP Processor that tracks the amplitude envelope of an audio signal with configurable attack/release times and three detection modes (Amplitude, RMS, Peak). Composes Layer 1 primitives (OnePoleSmoother for envelope smoothing, Biquad for optional sidechain highpass filtering) to provide real-time safe amplitude detection for use in dynamics processors, ducking, and modulation sources.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- OnePoleSmoother (`dsp/primitives/smoother.h`) - for attack/release envelope smoothing
- Biquad (`dsp/primitives/biquad.h`) - for optional sidechain highpass filter
- db_utils.h (`dsp/core/db_utils.h`) - for denormal flushing and NaN handling

**Storage**: N/A (stateful but no persistence)
**Testing**: Catch2 v3 (Constitution Principle XII: Test-First Development)
**Target Platform**: VST3 plugin - Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single DSP processor component (Layer 2)
**Performance Goals**: < 0.1% CPU per instance at 44.1kHz stereo (Principle XI budget for Layer 2)
**Constraints**: Real-time safe (noexcept, no allocations in process path), O(N) complexity
**Scale/Scope**: Single-channel envelope follower, composable for stereo/multichannel use

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | noexcept, no allocations in process(), pre-allocated state |
| III. Modern C++ | PASS | C++20, RAII, constexpr where possible |
| VIII. Testing Discipline | PASS | Pure DSP function testable without VST |
| IX. Layered Architecture | PASS | Layer 2 depends only on Layer 0/1 |
| X. DSP Constraints | PASS | Sample-accurate, denormal handling |
| XI. Performance Budgets | PASS | < 0.5% CPU target for Layer 2 processor |
| XII. Test-First | PASS | Tests written before implementation |
| XIV. ODR Prevention | PASS | Codebase search complete (see below) |
| XV. Honest Completion | PASS | Will verify all FR/SC before claiming done |

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

**Classes/Structs to be created**: EnvelopeFollower, DetectionMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| EnvelopeFollower | `grep -r "class EnvelopeFollower" src/` | No | Create New |
| Envelope | `grep -r "class Envelope" src/` | No | Create New |
| DetectionMode | `grep -r "DetectionMode" src/` | No | Create New |

**Utility Functions to be created**: None - using existing primitives

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none needed) | - | - | - | - |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Envelope smoothing with custom attack/release coefficients |
| Biquad | dsp/primitives/biquad.h | 1 | Sidechain highpass filter (FilterType::Highpass) |
| detail::flushDenormal | dsp/core/db_utils.h | 0 | Prevent denormal CPU slowdown in output |
| detail::isNaN | dsp/core/db_utils.h | 0 | NaN input handling |
| calculateOnePolCoefficient | dsp/primitives/smoother.h | 1 | Attack/release coefficient calculation |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - Legacy utilities - no envelope-related code
- [x] `src/dsp/core/` - Layer 0 core utilities - no envelope implementation
- [x] `src/dsp/primitives/` - Layer 1 primitives - no envelope follower
- [x] `src/dsp/processors/` - Layer 2 processors - SaturationProcessor, MultimodeFilter exist, no envelope

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing envelope-related classes found in codebase. The name "EnvelopeFollower" is unique. Will reuse existing OnePoleSmoother and Biquad rather than creating duplicates.

## Project Structure

### Documentation (this feature)

```text
specs/010-envelope-follower/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal - no unknowns)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── envelope_follower.h  # API contract
├── checklists/
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── processors/
        └── envelope_follower.h  # EnvelopeFollower class (Layer 2)

tests/
└── unit/
    └── processors/
        └── envelope_follower_test.cpp  # Unit tests
```

**Structure Decision**: Single header file in Layer 2 processors following the established pattern from SaturationProcessor and MultimodeFilter. Header-only implementation for performance (inline processing).

## Implementation Approach

### Algorithm Design

**Amplitude Mode (FR-001)**:
```
output = smooth(|input|, attack_if_rising, release_if_falling)
```
- Full-wave rectification: `abs(input)`
- Asymmetric smoothing: use attack coefficient when rising, release when falling

**RMS Mode (FR-002)**:
```
squared_env = smooth(input², attack, release)
output = sqrt(squared_env)
```
- Square input signal
- Smooth the squared signal
- Take square root of smoothed value

**Peak Mode (FR-003)**:
```
if |input| > current: output = |input|  (instant attack)
else: output = smooth(current, release)
```
- Instant attack (captures peaks immediately)
- Exponential release decay

### Attack/Release Coefficient Formula

Using the one-pole coefficient formula from `smoother.h`:
```cpp
coeff = exp(-1.0 / (time_samples))
// where time_samples = time_ms * 0.001 * sampleRate
```

For asymmetric smoothing:
- If `|input| > current`: use `attackCoeff_`
- If `|input| < current`: use `releaseCoeff_`

### Sidechain Filter (FR-008-010)

Use existing `Biquad` configured as highpass:
```cpp
sidechainFilter_.configure(FilterType::Highpass, cutoffHz, kButterworthQ, 0.0f, sampleRate);
```

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none) | - | - |
