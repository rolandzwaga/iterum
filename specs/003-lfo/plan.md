# Implementation Plan: LFO DSP Primitive

**Branch**: `003-lfo` | **Date**: 2025-12-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/003-lfo/spec.md`

## Summary

Implement a wavetable-based Low Frequency Oscillator (LFO) as a Layer 1 DSP Primitive. The LFO provides modulation signals for chorus/flanger/vibrato effects (via DelayLine modulation), tremolo, auto-pan, and filter sweeps. Key features include multiple waveforms (sine, triangle, sawtooth, square, sample & hold, smoothed random), tempo sync with musical note values, adjustable phase offset, and retrigger capability. All processing must be real-time safe with no allocations during process callbacks.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 13+, GCC 10+)
**Primary Dependencies**: Standard library only (Layer 1 depends only on Layer 0/stdlib per Constitution Principle IX)
**Storage**: N/A (stateless between sessions; state is runtime-only)
**Testing**: Catch2 v3 (per existing test infrastructure in tests/CMakeLists.txt) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - x64 and ARM64
**Project Type**: Single header library (inline implementation pattern, same as DelayLine)
**Performance Goals**:
- Process methods < 50 nanoseconds per sample (per spec SC-005)
- O(1) time complexity per sample (per spec NFR-001)
- Phase drift < 0.0001 degrees over 24 hours (per spec SC-004)
**Constraints**:
- Zero memory allocations during process callback (per spec FR-010)
- All public methods noexcept (per spec FR-011)
- Frequency range 0.01 Hz to 20 Hz (per spec FR-003)
- Output range [-1.0, +1.0] (per spec FR-008)
**Scale/Scope**:
- 6 waveforms to implement
- 18 note values for tempo sync (6 notes × 3 modifiers)
- 2048 samples per wavetable (per spec NFR-002)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Principle I (VST3 Architecture Separation)
- [x] **N/A** - Layer 1 DSP primitive has no VST3 coupling; pure C++ class

### Principle II (Real-Time Audio Thread Safety)
- [x] **PASS** - Pre-allocate wavetables in prepare(); no allocations in process()
- [x] **PASS** - No locks, mutexes, or blocking operations
- [x] **PASS** - All methods noexcept

### Principle III (Modern C++ Standards)
- [x] **PASS** - Targeting C++20
- [x] **PASS** - RAII for wavetable storage (std::vector)
- [x] **PASS** - constexpr where applicable

### Principle VIII (Testing Discipline)
- [x] **PASS** - Pure functions testable without VST infrastructure
- [x] **PASS** - Unit tests for all waveforms and behaviors

### Principle IX (Layered DSP Architecture)
- [x] **PASS** - Layer 1 primitive depends only on Layer 0/stdlib
- [x] **PASS** - No circular dependencies

### Principle X (DSP Processing Constraints)
- [x] **PASS** - Sample-accurate timing for tempo sync
- [x] **N/A** - No saturation/distortion requiring oversampling
- [x] **N/A** - No asymmetric processing requiring DC blocking

### Principle XI (Performance Budgets)
- [x] **PASS** - Layer 1 primitive budget < 0.1% CPU per instance

### Principle XII (Test-First Development)
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

### Principle XIII (Living Architecture Documentation)
- [x] **PASS** - ARCHITECTURE.md update required as final task

**Gate Status**: ✅ ALL GATES PASS - Proceed to Phase 0

## Project Structure

### Documentation (this feature)

```text
specs/003-lfo/
├── spec.md              # Feature specification (created by /speckit.specify)
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
│   └── lfo.h            # API contract header
├── checklists/          # Created by /speckit.specify
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── core/                    # Layer 0: Core utilities
│   │   └── db_utils.h           # Existing: dB/linear conversion
│   └── primitives/              # Layer 1: DSP primitives
│       ├── delay_line.h         # Existing: Circular buffer delay
│       └── lfo.h                # NEW: Wavetable LFO (this feature)

tests/
├── unit/
│   ├── core/
│   │   └── db_utils_test.cpp    # Existing
│   └── primitives/
│       ├── delay_line_test.cpp  # Existing
│       └── lfo_test.cpp         # NEW: LFO unit tests (this feature)
└── CMakeLists.txt               # Update to include lfo_test.cpp
```

**Structure Decision**: Single header implementation in `src/dsp/primitives/lfo.h` following the established pattern from `delay_line.h`. All implementation is inline in the header for performance and simplicity. Tests in `tests/unit/primitives/lfo_test.cpp`.

## Complexity Tracking

No constitution violations requiring justification. Design is straightforward Layer 1 primitive.

## Phase 0 Research Topics

Based on Technical Context, the following need research:

1. **Wavetable generation** - Optimal algorithms for generating sine, triangle, saw, square waveforms
2. **Sample & hold / smoothed random** - Implementation strategies for random-based waveforms
3. **Phase accumulator precision** - Double precision requirements for drift prevention
4. **Tempo sync formulas** - BPM to frequency conversion with dotted/triplet support
5. **Linear interpolation** - Wavetable interpolation for anti-aliasing at low table sizes

## Phase 1 Design Deliverables

1. **data-model.md** - LFO class structure, state variables, enumerations
2. **contracts/lfo.h** - Public API contract (header without implementation)
3. **quickstart.md** - Usage examples for each waveform and mode
