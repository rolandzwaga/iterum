# Implementation Plan: FFT Processor

**Branch**: `007-fft-processor` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/007-fft-processor/spec.md`

## Summary

Implement a Layer 1 DSP primitive providing Fast Fourier Transform capabilities for spectral processing. The FFT Processor is the foundation for higher-layer spectral effects including spectral delay, pitch shifting, and granular processing. Key components: forward/inverse FFT (Radix-2 DIT), STFT with windowing (Hann/Hamming/Blackman/Kaiser), overlap-add reconstruction, and complex spectrum buffer manipulation.

**Technical Approach** (from research):
- Radix-2 Cooley-Tukey DIT algorithm with precomputed LUTs (twiddle factors, bit-reversal)
- Real FFT output format: N/2+1 complex bins (industry standard FFTW/KissFFT convention)
- COLA-compliant windows: Hann/Hamming/Blackman at 50%/75% overlap with unity gain
- Streaming STFT: Circular buffer input + overlap-add output accumulator
- Float32 precision: Expected round-trip error < 0.0003% (well within spec's 0.0001% target for N ≤ 1024)

## Technical Context

**Language/Version**: C++20 (constexpr math, std::bit_cast)
**Primary Dependencies**: Standard library only (Layer 1 - no external FFT libraries)
**Storage**: N/A (all in-memory buffers)
**Testing**: Catch2 (see `specs/TESTING-GUIDE.md`) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project (DSP primitive library)
**Performance Goals**: O(N log N) FFT execution, < 0.1% CPU per Layer 1 instance
**Constraints**: Real-time safe (no allocations in process), noexcept guarantee, pre-allocation in prepare()
**Scale/Scope**: Power-of-2 FFT sizes 256-8192, mono operation, single-channel

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Evidence |
|-----------|--------|----------|
| **II. Real-Time Safety** | ✅ PASS | All allocations in `prepare()`, noexcept process methods, no locks |
| **III. Modern C++** | ✅ PASS | C++20, RAII, constexpr where possible, std::vector for buffers |
| **IX. Layered Architecture** | ✅ PASS | Layer 1 primitive, depends only on Layer 0 (kPi from dsp_utils.h) |
| **X. DSP Constraints** | ✅ PASS | COLA windows for FFT, proper handling per constitution |
| **XII. Test-First** | ✅ PLANNED | Tasks include TESTING-GUIDE check, tests before implementation |
| **XIV. ODR Prevention** | ✅ PASS | No existing FFT/STFT/Spectrum classes found (search completed) |
| **XV. Honest Completion** | ✅ PLANNED | Compliance verification in tasks.md |

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

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `FFT` | `grep -r "class FFT\|struct FFT" src/` | No | Create New |
| `Window` | `grep -r "class Window\|struct Window" src/` | No | Create New |
| `STFT` | `grep -r "class STFT\|struct STFT" src/` | No | Create New |
| `OverlapAdd` | `grep -r "class OverlapAdd\|struct OverlapAdd" src/` | No | Create New |
| `SpectralBuffer` | `grep -r "class SpectralBuffer\|struct SpectralBuffer" src/` | No | Create New |
| `Complex` | `grep -r "struct Complex" src/` | No | Create New (simple POD) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `generateHannWindow` | `grep -r "Hann\|hann" src/` | No | - | Create New |
| `generateHammingWindow` | `grep -r "Hamming\|hamming" src/` | No | - | Create New |
| `generateBlackmanWindow` | `grep -r "Blackman\|blackman" src/` | No | - | Create New |
| `generateKaiserWindow` | `grep -r "Kaiser\|kaiser" src/` | Yes | oversampler.h | Extract pattern |
| `besselI0` | `grep -r "besselI0\|bessel" src/` | Yes | oversampler.h (implicit) | Extract to shared |
| `bitReverse` | `grep -r "bitReverse\|bit_reverse" src/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `kPi`, `kTwoPi` | dsp/dsp_utils.h | 0 | Twiddle factor calculation |
| Kaiser window pattern | dsp/primitives/oversampler.h | 1 | Reference for Bessel I0 implementation |
| constexpr math patterns | dsp/core/db_utils.h | 0 | Constexpr Taylor series reference |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - Has kPi/kTwoPi constants (will reuse)
- [x] `src/dsp/core/db_utils.h` - Constexpr math patterns (reference only)
- [x] `src/dsp/primitives/oversampler.h` - Kaiser FIR coefficients (different use case - FIR filter design vs STFT window)
- [x] `ARCHITECTURE.md` - No FFT/spectral components exist

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (FFT, STFT, Window, SpectralBuffer, OverlapAdd) are unique and not found in codebase. The Kaiser window in oversampler.h is for FIR filter coefficient design (different use case), not STFT windowing. Will extract Bessel I0 to a shared utility to avoid duplication.

## Project Structure

### Documentation (this feature)

```text
specs/007-fft-processor/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Completed research on FFT/STFT implementation
├── data-model.md        # Entity definitions
├── quickstart.md        # Usage examples
├── contracts/           # API contracts (header signatures)
│   └── fft_processor.h  # Public API contract
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Implementation tasks (generated by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/
│   ├── db_utils.h           # (existing) Layer 0 utilities
│   └── window_functions.h   # (NEW) Window generators + Bessel I0
└── primitives/
    ├── fft.h                # (NEW) Core FFT class
    ├── stft.h               # (NEW) STFT + OverlapAdd wrapper
    └── spectral_buffer.h    # (NEW) Complex spectrum manipulation

tests/unit/
├── core/
│   └── window_functions_test.cpp  # (NEW) Window COLA tests
└── primitives/
    ├── fft_test.cpp               # (NEW) FFT accuracy, round-trip tests
    ├── stft_test.cpp              # (NEW) STFT streaming, reconstruction tests
    └── spectral_buffer_test.cpp   # (NEW) Magnitude/phase manipulation tests
```

**Structure Decision**: Single-project DSP primitive following established Layer 1 pattern (see delay_line.h, lfo.h, biquad.h). Window functions extracted to Layer 0 core/ for shared use (Bessel I0 may be useful for other components).

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Decision | Rationale | Alternative Considered |
|----------|-----------|------------------------|
| Separate `window_functions.h` | Bessel I0 is reusable; windows may be used by other processors | Inline in FFT - rejected (duplication risk with oversampler) |
| Split FFT/STFT/SpectralBuffer | Single responsibility, independent testability | Monolithic FFTProcessor - rejected (too complex, harder to test) |
| Header-only implementation | Performance-critical, enables inlining | Separate .cpp - rejected (inline critical for performance) |

## Implementation Strategy

### Phase 1: Foundation (Window Functions + Core FFT)

1. **Window Functions** (Layer 0)
   - Extract Bessel I0 to shared utility
   - Implement Hann, Hamming, Blackman, Kaiser generators
   - COLA compliance tests for each window type

2. **Core FFT** (Layer 1)
   - Complex type definition
   - Bit-reversal LUT generation
   - Twiddle factor precomputation (double → float)
   - Forward FFT (real-to-complex, N/2+1 bins)
   - Inverse FFT (complex-to-real)
   - Round-trip accuracy tests

### Phase 2: STFT and Streaming

3. **SpectralBuffer** (Layer 1)
   - Complex spectrum storage (N/2+1 bins)
   - Magnitude/phase access methods
   - Cartesian/polar conversion

4. **STFT + OverlapAdd** (Layer 1)
   - Circular buffer input accumulator
   - Windowed analysis
   - Output overlap-add accumulator
   - Streaming reconstruction tests

### Phase 3: Integration and Real-Time Safety

5. **Real-Time Safety Verification**
   - Verify no allocations in process methods
   - noexcept guarantee tests
   - Performance profiling (O(N log N) verification)

6. **Documentation and ARCHITECTURE.md Update**
   - Update component inventory
   - Usage examples

## Key Design Decisions (from research.md)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| FFT Algorithm | Radix-2 DIT | Natural-order output, well-tested, O(N log N) |
| Bit-Reversal | Precomputed LUT | O(1) lookup, acceptable memory (< 8KB for N=8192) |
| Twiddle Factors | Double precision → float | Accuracy is dominant error source |
| Complex Format | Interleaved struct | Portable, cache-friendly, standard layout |
| Output Format | N/2+1 bins (DC..Nyquist) | Industry standard (FFTW, KissFFT) |
| Default Window | Hann | COLA at 50%/75%, unity gain, excellent characteristics |
| Default Overlap | 75% (4x redundancy) | Good time/frequency resolution for spectral effects |
| Kaiser COLA | Require 90% overlap | Document limitation, enforce in validation |
| Window Application | Analysis only (not synthesis) | Simpler, standard OLA approach |

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Float32 precision insufficient | Research shows 0.0003% error for N=1024; test at all sizes |
| Window COLA mismatch | Use "periodic" variants; unit test COLA property |
| DC/Nyquist imaginary contamination | Explicitly zero imaginary parts; verify in tests |
| Twiddle factor inaccuracy | Compute in double, store as float; validate spectrum peaks |
| Real-time allocation | Code inspection + static_assert on noexcept |

## Success Criteria Verification Plan

| Criterion | Verification Method |
|-----------|---------------------|
| SC-001: Sine frequency detection | FFT known sine wave, verify bin index matches frequency |
| SC-002: Round-trip < 0.0001% | FFT → IFFT, measure max error across all sizes |
| SC-003: STFT reconstruction < 0.01% | STFT → ISTFT (no modification), compare input/output |
| SC-004: Window shapes | Compare generated windows to analytical formulas |
| SC-005: O(N log N) scaling | Time FFT at multiple sizes, verify log-linear growth |
| SC-006: Zero allocations | Code inspection, noexcept static_assert |
| SC-007: Cross-platform | CI on Windows/macOS/Linux |

---

**Next Step**: Run `/speckit.tasks` to generate detailed task breakdown.
