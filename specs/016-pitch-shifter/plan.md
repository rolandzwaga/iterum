# Implementation Plan: Pitch Shift Processor

**Branch**: `016-pitch-shifter` | **Date**: 2025-12-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/016-pitch-shifter/spec.md`

## Summary

Implement a Layer 2 DSP Pitch Shift Processor that transposes audio pitch by semitones without changing duration. Three quality modes provide different latency/quality trade-offs:
- **Simple**: Zero-latency delay-line modulation (Doppler effect)
- **Granular**: Low-latency OLA with ~46ms delay
- **PhaseVocoder**: Highest quality STFT-based with ~116ms delay

Includes formant preservation for vocal processing and feedback stability for Shimmer integration.

## Technical Context

**Language/Version**: C++20 (matching existing codebase)
**Primary Dependencies**:
- Layer 1: DelayLine, STFT, FFT, SpectralBuffer, OnePoleSmoother, WindowFunctions
- Layer 0: db_utils.h (math utilities)
**Storage**: N/A (stateful DSP processor, no persistence)
**Testing**: Catch2 (per `tests/CMakeLists.txt`)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: Single project, Layer 2 DSP Processor
**Performance Goals**:
- Simple mode: <2% CPU
- Granular mode: <5% CPU
- PhaseVocoder mode: <10% CPU
- (All measured at 44.1kHz stereo)
**Constraints**:
- Real-time safe (no allocations in process())
- Simple mode: 0 samples latency
- Granular mode: <2048 samples latency
- PhaseVocoder mode: <8192 samples latency
**Scale/Scope**: Single mono processor class, stereo via dual instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Requirement | Status |
|-----------|-------------|--------|
| I. VST3 Separation | DSP is pure, no VST dependencies | ✅ PASS |
| II. Real-Time Safety | No allocations in process() | ✅ PLANNED |
| III. Modern C++ | RAII, noexcept, C++20 | ✅ PLANNED |
| IV. SIMD Optimization | Aligned buffers, contiguous access | ✅ PLANNED |
| VIII. Testing | Unit tests before implementation | ✅ PLANNED |
| IX. Layered Architecture | Layer 2 composing Layer 1 primitives | ✅ PASS |
| X. DSP Constraints | COLA windows, proper overlap, DC blocking | ✅ PLANNED |
| XI. Performance Budgets | <0.5% per Layer 2 processor (spec allows higher) | ⚠️ EXCEPTION |
| XII. Test-First Development | Tests before code | ✅ PLANNED |
| XIII. Architecture Doc | Update ARCHITECTURE.md at completion | ✅ PLANNED |
| XIV. ODR Prevention | Search before creating types | ✅ COMPLETED |
| XV. Honest Completion | Full FR/SC verification | ✅ PLANNED |

**Required Check - Principle XII (Test-First Development):**
- [X] Tasks will include TESTING-GUIDE.md context verification step
- [X] Tests will be written BEFORE implementation code
- [X] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [X] Codebase Research section below is complete
- [X] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: PitchShiftProcessor, SimplePitchShifter, GranularPitchShifter, PhaseVocoderPitchShifter, FormantPreserver

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PitchShiftProcessor | `grep -r "class PitchShift" src/` | No | Create New |
| SimplePitchShifter | `grep -r "class Simple" src/` | No | Create New (internal) |
| GranularPitchShifter | `grep -r "class Granular" src/` | No | Create New (internal) |
| PhaseVocoderPitchShifter | `grep -r "PhaseVocoder" src/` | No | Create New (internal) |
| FormantPreserver | `grep -r "Formant" src/` | No | Create New (internal) |
| GrainWindow | `grep -r "class Grain" src/` | No | Create New (internal) |

**Utility Functions to be created**: None new - will reuse existing

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| pitchRatioFromSemitones | N/A | No | pitch_shift_processor.h | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/primitives/delay_line.h | 1 | Simple mode buffer + read pointers |
| STFT | dsp/primitives/stft.h | 1 | PhaseVocoder analysis/synthesis |
| FFT | dsp/primitives/fft.h | 1 | Formant preservation (cepstrum) |
| SpectralBuffer | dsp/primitives/spectral_buffer.h | 1 | Phase/magnitude storage |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| WindowFunctions | dsp/core/window_functions.h | 0 | Hann windows for grains/STFT |
| constexprPow10 | dsp/core/db_utils.h | 0 | Pitch ratio calculation |

### Files Checked for Conflicts

- [X] `src/dsp/dsp_utils.h` - No pitch-related utilities
- [X] `src/dsp/core/` - Layer 0 core utilities (will reuse math)
- [X] `src/dsp/primitives/` - Layer 1 primitives available
- [X] `ARCHITECTURE.md` - No existing pitch shifter

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (PitchShiftProcessor, etc.) are unique and do not exist in codebase. Internal helper classes will be in anonymous namespace or nested within the processor class to prevent symbol conflicts.

## Project Structure

### Documentation (this feature)

```text
specs/016-pitch-shifter/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Algorithm research (COMPLETE)
├── data-model.md        # Entity definitions
├── quickstart.md        # Usage examples
├── contracts/           # API contracts
│   └── pitch_shift_processor.h
└── tasks.md             # Implementation tasks (generated by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/processors/
└── pitch_shift_processor.h   # Complete implementation (header-only)

tests/unit/processors/
└── pitch_shift_processor_test.cpp
```

**Structure Decision**: Single header file containing all three pitch shifting modes as internal implementation classes, with PitchShiftProcessor as the public facade. This matches existing Layer 2 processor patterns (saturation_processor.h, diffusion_network.h).

## Algorithm Design

### Simple Mode (Delay-Line Modulation)

```
Input → DelayLine → [ReadPtr1 + ReadPtr2 with crossfade] → Output

Components:
- 1x DelayLine (50ms capacity)
- 2 read pointers at variable speed
- Crossfade: half-sine window (energy-preserving)
- Latency: 0 samples

Pitch ratio = 2^(semitones/12) × 2^(cents/1200)
Read speed = 1.0 / pitch_ratio
Crossfade period = buffer_size / |1 - read_speed|
```

### Granular Mode (OLA)

```
Input → [Grain Buffer] → [Windowed Grains at pitch rate] → OLA → Output

Components:
- Grain buffer (40ms)
- 4 overlapping grain windows (75% overlap)
- Hann window for COLA compliance
- Latency: ~40ms (grain_size)

Grain playback rate = pitch_ratio
Grain emission rate = 1.0 (maintains duration)
Synthesis hop = analysis_hop (no time stretch)
```

### PhaseVocoder Mode (STFT)

```
Input → STFT → [Magnitude/Phase] → Frequency Scaling → ISTFT → Output

Components:
- FFT size: 4096
- Hop size: 1024 (75% overlap)
- Phase accumulator per bin
- Scaled phase locking for vertical coherence
- Latency: 5120 samples (~116ms at 44.1kHz)

Phase propagation:
  phase_new[bin] = phase_old[bin] + 2π × true_freq × hop_size / sample_rate

Frequency scaling:
  new_bin = old_bin × pitch_ratio
```

### Formant Preservation (Cepstral)

```
Spectrum → Log → IFFT → Lifter → FFT → Exp → Envelope

Quefrency cutoff: 50 samples (~1.1ms at 44.1kHz)
Lifter: Low-pass in cepstral domain

Process:
1. envelope = cepstral_smooth(magnitude)
2. excitation = magnitude / envelope
3. shifted_excitation = pitch_shift(excitation)
4. output = shifted_excitation × envelope
```

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| CPU budget (0.5% → up to 10%) | PhaseVocoder requires significant FFT processing | CPU budget is for typical Layer 2 processors; pitch shifting is inherently complex |
| Multiple internal classes | Three distinct algorithms with different characteristics | Single class would be unmaintainable; modes are fundamentally different algorithms |

## Key Implementation Decisions

### Decision 1: Internal Class Strategy
- **Chosen**: Nested internal classes within PitchShiftProcessor
- **Rationale**: Avoids ODR issues, keeps API simple, matches DiffusionNetwork pattern
- **Alternative rejected**: Separate files per mode - increases complexity, ODR risk

### Decision 2: Phase Locking Strategy
- **Chosen**: Scaled phase locking (Laroche & Dolson 1999)
- **Rationale**: Best quality for pitch shifting, industry standard
- **Alternative rejected**: Identity phase locking - inferior for pitch (better for time stretch only)

### Decision 3: Formant Estimation
- **Chosen**: Cepstral liftering
- **Rationale**: Good quality, moderate CPU, works with existing FFT primitives
- **Alternative rejected**: LPC - more complex, marginal quality improvement

### Decision 4: Simple Mode Window
- **Chosen**: Half-sine (sqrt of Hann) crossfade
- **Rationale**: Energy-preserving, sounds better than linear
- **Alternative rejected**: Linear ramp - causes amplitude dips
