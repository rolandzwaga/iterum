# Implementation Plan: Saturation Processor

**Branch**: `009-saturation-processor` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/009-saturation-processor/spec.md`

## Summary

Layer 2 DSP Processor providing analog-style saturation/waveshaping with 5 distinct algorithms (Tape, Tube, Transistor, Digital, Diode). Composes Layer 1 primitives (Oversampler for alias-free processing, Biquad for DC blocking, OnePoleSmoother for parameter smoothing) with gain staging and dry/wet mix control. All processing is real-time safe with 2x oversampling to prevent aliasing from nonlinear waveshaping.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, Layer 1 primitives (Oversampler, Biquad, OnePoleSmoother)
**Storage**: N/A (stateless processor, state managed externally)
**Testing**: Catch2 3.x - unit tests for DSP, approval tests for regressions *(Constitution Principle XII)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - all x64 + ARM64
**Project Type**: Single project (VST3 plugin DSP library)
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz mono (Layer 2 budget)
**Constraints**: No memory allocation in process(), all methods noexcept, < 1 buffer parameter latency
**Scale/Scope**: Single mono processor; stereo via dual instantiation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Applicable | Status | Notes |
|-----------|------------|--------|-------|
| I. VST3 Architecture Separation | No | N/A | DSP-only component, no VST interface |
| II. Real-Time Audio Thread Safety | **Yes** | ✅ Pass | No allocations in process, all noexcept |
| III. Modern C++ Standards | **Yes** | ✅ Pass | C++20, RAII, constexpr where possible |
| IV. SIMD & DSP Optimization | **Yes** | ✅ Pass | Sequential memory access, minimal branching |
| V. VSTGUI Development | No | N/A | No UI component |
| VI. Memory Architecture | **Yes** | ✅ Pass | Pre-allocate in prepare(), use smoothers |
| VII. Project Structure | **Yes** | ✅ Pass | Goes in src/dsp/processors/ |
| VIII. Testing Discipline | **Yes** | ✅ Pass | Unit tests for all algorithms |
| IX. Layered DSP Architecture | **Yes** | ✅ Pass | Layer 2, depends only on Layer 0/1 |
| X. DSP Processing Constraints | **Yes** | ✅ Pass | 2x oversampling, DC blocking after asymmetric |
| XI. Performance Budgets | **Yes** | ✅ Pass | Target < 0.5% CPU |
| XII. Test-First Development | **Yes** | ✅ Pass | Write tests before implementation |
| XIII. Living Architecture | **Yes** | ✅ Pass | Update ARCHITECTURE.md on completion |
| XIV. ODR Prevention | **Yes** | ✅ Pass | See Codebase Research section |
| XV. Honest Completion | **Yes** | ✅ Pass | All FR/SC must be verified |

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

**Classes/Structs to be created**: SaturationProcessor, SaturationType enum

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SaturationProcessor | `grep -r "class SaturationProcessor" src/` | No | Create New |
| SaturationType | `grep -r "enum.*SaturationType\|Saturation" src/` | No | Create New |

**Utility Functions to be created**: None (use existing)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| hardClip | `grep -r "hardClip" src/` | Yes | dsp_utils.h:104 | Reuse |
| softClip | `grep -r "softClip" src/` | Yes | dsp_utils.h:109 | Reference for tanh approx |
| dbToGain | `grep -r "dbToGain" src/` | Yes | dsp/core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" src/` | Yes | dsp/core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Oversampler<2,1> | dsp/primitives/oversampler.h | 1 | 2x oversampling for saturation |
| Biquad | dsp/primitives/biquad.h | 1 | Highpass @ ~10Hz for DC blocking |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Smooth input/output gain and mix |
| dbToGain | dsp/core/db_utils.h | 0 | Convert dB parameters to linear gain |
| hardClip | dsp/dsp_utils.h | 0 | Digital saturation type implementation |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - Contains hardClip/softClip, no conflict with SaturationProcessor
- [x] `src/dsp/core/` - Contains db_utils.h, will reuse
- [x] `src/dsp/processors/multimode_filter.h` - Uses std::tanh for drive, separate component
- [x] `ARCHITECTURE.md` - No saturation processor exists

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: SaturationProcessor is a new class not found in codebase. All saturation-related code in multimode_filter.h is internal to that processor. Waveshaping functions will be implemented as private methods or inline lambdas within SaturationProcessor, not as standalone utilities that could conflict.

## Project Structure

### Documentation (this feature)

```text
specs/009-saturation-processor/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output - saturation algorithm analysis
├── data-model.md        # Phase 1 output - API design
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - header contract
│   └── saturation_processor.h
└── tasks.md             # Phase 2 output (via /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/                    # Layer 0 (existing)
│   └── db_utils.h           # Reuse: dbToGain, gainToDb
├── primitives/              # Layer 1 (existing)
│   ├── oversampler.h        # Reuse: Oversampler<2,1>
│   ├── biquad.h             # Reuse: Biquad (Highpass)
│   └── smoother.h           # Reuse: OnePoleSmoother
└── processors/              # Layer 2
    ├── multimode_filter.h   # Existing - reference pattern
    └── saturation_processor.h  # NEW - this feature

tests/
├── unit/
│   └── processors/
│       └── saturation_processor_test.cpp  # NEW - unit tests
└── regression/
    └── approved/
        └── saturation_*.approved.txt  # NEW - approval baselines (if needed)
```

**Structure Decision**: Single header in src/dsp/processors/ following MultimodeFilter pattern. Header-only implementation for inlining critical paths.

## Complexity Tracking

No constitution violations required. Design is straightforward composition of existing primitives.

## Saturation Algorithm Summary

The five saturation types and their characteristics:

| Type | Curve | Symmetry | Dominant Harmonics | DC Offset Risk |
|------|-------|----------|-------------------|----------------|
| Tape | tanh(x) | Symmetric | Odd (3rd, 5th, 7th) | None |
| Tube | Asymmetric polynomial | Asymmetric | Even (2nd, 4th) | Yes |
| Transistor | Hard-knee soft clip | Symmetric | Odd + higher | None |
| Digital | Hard clip (clamp) | Symmetric | Odd (harsh) | None |
| Diode | Soft asymmetric | Asymmetric | Even + odd | Yes |

**Key Implementation Notes:**
1. Tube and Diode generate DC offset from asymmetry - DC blocker is essential
2. Tape uses std::tanh (standard, well-understood)
3. Digital uses std::clamp (simplest, most CPU efficient)
4. Transistor uses polynomial approximation to hard-knee curve
5. All types processed at 2x sample rate via oversampler

## Next Steps

1. Generate research.md with algorithm details
2. Generate data-model.md with class API
3. Generate contracts/saturation_processor.h as header contract
4. Generate quickstart.md with usage examples
5. Run /speckit.tasks to generate implementation tasks
