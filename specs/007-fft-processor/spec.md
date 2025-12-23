# Feature Specification: FFT Processor

**Feature Branch**: `007-fft-processor`
**Created**: 2025-12-23
**Status**: Draft
**Layer**: 1 (DSP Primitives)
**Input**: User description: "Layer 1 DSP Primitive - FFT Processor for spectral processing. Includes forward/inverse FFT (power-of-2 sizes: 256-8192), STFT with windowing (Hann, Hamming, Blackman, Kaiser), overlap-add reconstruction with configurable hop size (50%/75%), and complex spectrum buffer. Core building block for spectral delay, granular processing, and pitch shifting features."

## Overview

The FFT Processor is a Layer 1 DSP Primitive providing Fast Fourier Transform capabilities for spectral processing. It serves as a foundational building block for higher-layer processors that require frequency-domain analysis and manipulation:
- Spectral delay (per-band delay times)
- Pitch shifting (phase vocoder)
- Granular processing (spectral freeze, smearing)
- Spectral effects (filtering, gating, morphing)

As a Layer 1 component, it may only depend on Layer 0 utilities and must be independently testable without VST infrastructure.

## User Scenarios & Testing *(mandatory)*

**Note**: For this DSP primitive, "users" are developers building higher-layer DSP processors.

### User Story 1 - Forward FFT Analysis (Priority: P1)

A DSP developer needs to transform time-domain audio into frequency-domain representation for spectral analysis or manipulation. They instantiate an FFT processor, provide a buffer of samples, and receive complex spectral data.

**Why this priority**: Forward FFT is the most fundamental operation - every spectral effect begins with transforming audio to frequency domain.

**Independent Test**: Can be fully tested by transforming known signals (sine waves) and verifying the resulting spectrum contains energy at the expected frequency bins.

**Acceptance Scenarios**:

1. **Given** an FFT processor configured for size 1024, **When** I provide a 1024-sample buffer containing a sine wave at bin frequency, **Then** the magnitude spectrum shows a peak at that bin with minimal spectral leakage.
2. **Given** a windowed FFT input, **When** I perform forward FFT, **Then** the output is a complex array of size N/2+1 representing positive frequencies (real FFT).
3. **Given** DC and Nyquist bins, **When** I examine the output, **Then** DC is at bin 0 and Nyquist is at bin N/2.

---

### User Story 2 - Inverse FFT Synthesis (Priority: P1)

A DSP developer needs to transform frequency-domain data back to time-domain audio for playback. After manipulating spectral data, they perform inverse FFT to reconstruct audio samples.

**Why this priority**: Inverse FFT is equally fundamental - spectral processing requires round-trip transformation.

**Independent Test**: Can be tested by performing forward FFT followed by inverse FFT and verifying the output matches the input within floating-point precision.

**Acceptance Scenarios**:

1. **Given** a complex spectrum from forward FFT, **When** I perform inverse FFT without modification, **Then** the output matches the original input within 0.0001% error.
2. **Given** spectral data with modified magnitudes, **When** I perform inverse FFT, **Then** the output reflects the spectral modifications correctly.
3. **Given** spectral data with modified phases, **When** I perform inverse FFT, **Then** the output reflects the phase changes (time shift, etc.).

---

### User Story 3 - STFT with Windowing (Priority: P1)

A DSP developer building a spectral delay needs to continuously analyze audio using Short-Time Fourier Transform. They configure the FFT processor with a window function and hop size for overlapping analysis frames.

**Why this priority**: STFT is the practical interface for real-time spectral processing - essential for any spectral effect.

**Independent Test**: Can be tested by processing a test signal through STFT and verifying frame-by-frame spectral content with known window characteristics.

**Acceptance Scenarios**:

1. **Given** an STFT processor with Hann window and 50% overlap, **When** I process a continuous signal, **Then** each frame is correctly windowed before FFT.
2. **Given** different window types (Hann, Hamming, Blackman, Kaiser), **When** I analyze a sine wave, **Then** each window produces its characteristic main lobe width and sidelobe level.
3. **Given** a configured hop size, **When** I process audio continuously, **Then** analysis frames are spaced by the hop size in samples.

---

### User Story 4 - Overlap-Add Reconstruction (Priority: P2)

A DSP developer needs artifact-free audio reconstruction from modified spectral frames. They use overlap-add synthesis to combine processed frames without discontinuities.

**Why this priority**: Required for any effect that modifies spectral content - without proper OLA, output has audible clicks at frame boundaries.

**Independent Test**: Can be tested by passing audio through STFT/ISTFT without modification and verifying perfect reconstruction (unity gain, no artifacts).

**Acceptance Scenarios**:

1. **Given** STFT with Hann window and 50% overlap, **When** I perform analysis-only (no modification) and overlap-add synthesis, **Then** output equals input (COLA property verified).
2. **Given** 75% overlap (4x redundancy), **When** I reconstruct, **Then** output equals input with COLA-compliant window.
3. **Given** frame-by-frame spectral modification, **When** I reconstruct, **Then** transitions between frames are smooth with no clicks.

---

### User Story 5 - Complex Spectrum Manipulation (Priority: P2)

A DSP developer needs to access and modify spectral data (magnitude and phase) for effects like spectral filtering, freeze, or morphing.

**Why this priority**: Provides the interface for higher-layer processors to implement spectral effects.

**Independent Test**: Can be tested by modifying spectrum (zeroing bins, scaling magnitude, shifting phase) and verifying expected audio output.

**Acceptance Scenarios**:

1. **Given** a spectrum buffer, **When** I access magnitude and phase at each bin, **Then** I can read and modify both independently.
2. **Given** bin k zeroed, **When** I reconstruct, **Then** that frequency is absent from output.
3. **Given** bin k phase shifted by 180 degrees, **When** I reconstruct, **Then** that frequency component is inverted.

---

### User Story 6 - Real-Time Safety (Priority: P1)

A DSP developer integrating the FFT processor into the audio callback needs guaranteed real-time safety. The FFT must never allocate memory or block during processing.

**Why this priority**: Real-time safety is a constitution-level requirement; violations cause audio glitches.

**Independent Test**: Can be verified by code inspection and profiler analysis during process calls.

**Acceptance Scenarios**:

1. **Given** a prepared FFT processor, **When** calling forward/inverse FFT, **Then** no memory allocations occur.
2. **Given** `prepare()` called before processing, **When** processing begins, **Then** all internal buffers are pre-allocated.
3. **Given** any valid input, **When** calling process methods, **Then** no exceptions are thrown (noexcept guarantee).

---

### Edge Cases

- What happens with FFT size not power of 2? Reject in `prepare()` with assertion.
- What happens with input buffer smaller than FFT size? Zero-pad remaining samples.
- What happens with NaN/infinity in input? Propagate (user responsibility to sanitize).
- What happens with hop size larger than FFT size? Invalid configuration - reject in `prepare()`.
- What happens with zero-magnitude bins during phase extraction? Return phase 0.
- What happens with aliased frequencies in modified spectrum? OLA can't prevent - user responsibility.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: FFT processor MUST provide forward real-to-complex FFT transformation.
- **FR-002**: FFT processor MUST provide inverse complex-to-real FFT transformation.
- **FR-003**: FFT processor MUST support power-of-2 sizes: 256, 512, 1024, 2048, 4096, 8192.
- **FR-004**: FFT processor MUST provide window functions: Hann, Hamming, Blackman, Kaiser.
- **FR-005**: FFT processor MUST provide STFT with configurable hop size (frame advance in samples).
- **FR-006**: FFT processor MUST provide overlap-add synthesis for artifact-free reconstruction.
- **FR-007**: FFT processor MUST pre-allocate all memory in `prepare()` before audio processing.
- **FR-008**: FFT processor MUST NOT allocate memory, throw exceptions, or block in process methods.
- **FR-009**: FFT processor MUST provide access to complex spectrum as magnitude/phase or real/imaginary pairs.
- **FR-010**: FFT processor MUST provide `reset()` method to clear internal state without reallocation.
- **FR-011**: All public methods MUST be marked `noexcept`.
- **FR-012**: FFT processor MUST support mono operation (single channel).
- **FR-013**: Output of forward FFT MUST be N/2+1 complex bins for N-point real FFT.
- **FR-014**: STFT MUST support 50% (2x) and 75% (4x) overlap configurations.
- **FR-015**: Window functions MUST be normalized for COLA (Constant Overlap-Add) reconstruction.

### Non-Functional Requirements

- **NFR-001**: Forward/inverse FFT MUST complete in O(N log N) time.
- **NFR-002**: Perfect reconstruction (STFT -> ISTFT without modification) MUST achieve < 0.01% error.
- **NFR-003**: Memory footprint MUST be bounded by `3 * FFT_SIZE * sizeof(float)` for core FFT operations.

### Key Entities

- **FFT**: Core forward/inverse transformation class.
  - Attributes: size, input buffer, output buffer (complex)
  - Operations: prepare(), forward(), inverse(), reset()

- **Window**: Window function generator.
  - Types: Hann, Hamming, Blackman, Kaiser
  - Operations: generate(size) -> float array

- **STFT**: Short-Time Fourier Transform processor.
  - Attributes: fftSize, hopSize, window type, analysis frames
  - Operations: prepare(), analyze(input) -> spectrum, reset()

- **OverlapAdd**: Overlap-add synthesis for reconstruction.
  - Attributes: fftSize, hopSize, window type, output accumulator
  - Operations: prepare(), synthesize(spectrum) -> output, reset()

- **SpectralBuffer**: Complex spectrum storage and manipulation.
  - Attributes: bins (N/2+1 complex values)
  - Operations: getMagnitude(bin), getPhase(bin), setMagnitude(bin), setPhase(bin), getPolar(bin), getCartesian(bin)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Forward FFT correctly identifies sine wave frequency within 1 bin accuracy.
- **SC-002**: Round-trip (FFT -> IFFT) reconstruction error is less than 0.0001% (float32 precision).
- **SC-003**: STFT -> ISTFT perfect reconstruction achieves < 0.01% error with COLA-compliant windows.
- **SC-004**: All window functions produce correct shapes verified against analytical formulas.
- **SC-005**: FFT execution time scales as O(N log N) verified by timing at multiple sizes.
- **SC-006**: Zero memory allocations during process callbacks (verified by code inspection).
- **SC-007**: All unit tests pass on Windows (MSVC), macOS (Clang), and Linux (GCC).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Single-precision float (32-bit) is the primary sample format.
- Power-of-2 FFT sizes are sufficient for plugin applications.
- Radix-2 Cooley-Tukey algorithm is acceptable (no need for split-radix or FFTW-level optimization).
- Kaiser window beta parameter defaults to 9.0 (good balance of main lobe width and sidelobe rejection).
- Phase vocoder-style processing will be handled by higher-layer processors, not this primitive.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| constexpr math (Taylor series) | dsp/core/db_utils.h | May reuse for sin/cos in FFT twiddle factors |
| kPi constant | dsp/dsp_utils.h | Reuse for FFT calculations |
| isNaN helper | dsp/core/db_utils.h | May reuse for input validation |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "fft\|FFT" src/
grep -r "spectrum\|Spectrum" src/
grep -r "window\|Window" src/
```

**Search Results Summary**: No existing FFT implementation found. Will implement from scratch as Layer 1 primitive.

## Dependencies

- **Layer 0**: May use constexpr math utilities from `dsp/core/db_utils.h`
- **Constitution**: Must comply with Principle II (Real-Time Safety) and Principle IX (Layered Architecture)

## Out of Scope

- Multi-channel FFT (handled by multiple instances)
- Phase vocoder (Layer 2 processor)
- Spectral effects (Layer 2 processors)
- GPU/SIMD optimization (future enhancement)
- Non-power-of-2 FFT sizes (use zero-padding if needed)
- FFTW or external library integration (pure C++ implementation)

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |

### Completion Checklist

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [PENDING IMPLEMENTATION]

**Recommendation**: Proceed with `/speckit.plan` to design implementation approach.
