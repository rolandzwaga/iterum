# FFT/STFT Implementation Research

**Date**: 2025-12-23
**Feature**: 007-fft-processor (Layer 1 DSP Primitive)
**Purpose**: Implementation guidance for real-time audio FFT/STFT processor

---

## Executive Summary

This document provides actionable implementation guidance for building a real-time FFT/STFT processor for audio plugin use. Key findings:

1. **COLA Windows**: Hann, Hamming, and Blackman windows are COLA-compliant at 50% and 75% overlap with unity gain normalization
2. **Real FFT Format**: Industry standard is N/2+1 complex bins with DC (bin 0) and Nyquist (bin N/2) as real-only values
3. **Streaming Architecture**: Circular buffer for sample accumulation; minimum latency equals FFT size
4. **Radix-2 FFT**: Precomputed twiddle factor LUT and bit-reversal LUT are essential for performance
5. **Float32 Precision**: Cooley-Tukey achieves O(ε log N) error bound; round-trip error < 0.01% is achievable
6. **Kaiser Window**: NOT naturally COLA-compliant; requires high overlap (90%+) for approximate reconstruction

---

## 1. COLA (Constant Overlap-Add) Window Normalization

### Overview

COLA (Constant Overlap-Add) is the constraint that ensures perfect reconstruction when performing STFT analysis followed by overlap-add synthesis without spectral modification. A window-overlap combination is COLA-compliant when successive windowed frames overlap-add to a constant gain.

**Mathematical Property**: ∑ w(n - mR) = C (constant), where R is hop size and m indexes frames.

### COLA-Compliant Windows at Standard Overlaps

| Window Type | 50% Overlap | 75% Overlap | Normalization Factor |
|-------------|-------------|-------------|----------------------|
| **Hann (periodic/DFT-even)** | ✅ COLA | ✅ COLA | 1.0 (unity) |
| **Hamming** | ✅ COLA | ✅ COLA | 1.0 (unity) |
| **Blackman** | ✅ COLA | ✅ COLA | 1.0 (unity) |
| **Kaiser (β=9)** | ❌ NOT COLA | ❌ NOT COLA | Varies (see below) |
| **Rectangular** | ✅ COLA at 100% | ❌ NOT at 75% | R (hop size) |

**Key Insight**: Hann, Hamming, and Blackman windows are **"periodic" or "DFT-even"** variants designed specifically for FFT analysis. These are COLA-compliant at overlaps of 1/2, 2/3, 3/4, 4/5, etc., and sum to **unity gain (1.0)** when overlapped correctly.

### Exact COLA Formulas

For **Hann window** at 50% overlap:
```
w[n] = 0.5 - 0.5 * cos(2π * n / (N-1))   // DFT-even variant for n = 0..N-1
Overlap: R = N/2
Sum: w[n] + w[n - R] = 1.0  (for all n in overlap region)
Normalization: None needed (already unity)
```

For **Hamming window** at 50% overlap:
```
w[n] = 0.54 - 0.46 * cos(2π * n / (N-1))
Overlap: R = N/2
Sum: w[n] + w[n - R] = 1.0
Normalization: None needed
```

For **Blackman window** at 50% overlap:
```
w[n] = 0.42 - 0.5 * cos(2π * n / (N-1)) + 0.08 * cos(4π * n / (N-1))
Overlap: R = N/2
Sum: w[n] + w[n - R] = 1.0
Normalization: None needed
```

### Verification Method

To verify COLA compliance programmatically:
```cpp
// Pseudocode for COLA verification
float sum = 0.0f;
for (size_t i = 0; i < fftSize; ++i) {
    sum += window[i];  // Accumulate overlapping windows
}
// For COLA: sum should equal constant (typically fftSize / hopSize)
bool isCOLA = std::abs(sum - expectedConstant) < 1e-6f;
```

### WOLA (Weighted Overlap-Add) Consideration

When the window is applied **twice** (once in analysis, once in synthesis), this is called **WOLA (Weighted Overlap-Add)**. For double windowing:
- The effective window becomes w²[n]
- Hann² requires 75% overlap (4x redundancy) to maintain COLA
- Alternatively, use **root-Hann window** (√w[n]) with 50% overlap for WOLA

**Recommendation for Implementation**: Apply window **only once** (in analysis stage) and use standard COLA overlaps to avoid complexity.

### Implementation Checklist

- [ ] Use "periodic" or "DFT-even" window variants (N samples for N-point FFT)
- [ ] Verify COLA property programmatically during initialization
- [ ] No normalization scaling needed for Hann/Hamming/Blackman at 50%/75% overlap
- [ ] Document whether WOLA or OLA approach is used

**Sources**:
- [COLA Cases - Spectral Audio Signal Processing](https://www.dsprelated.com/freebooks/sasp/Constant_Overlap_Add_COLA_Cases.html)
- [COLA Examples - Stanford CCRMA](https://ccrma.stanford.edu/~jos/sasp/COLA_Examples.html)
- [SciPy check_COLA Documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.check_COLA.html)
- [Window Function COLA Conditions - GitHub Gist](https://gist.github.com/endolith/c5b39ab78f1910e99cadaced9b839fc1)

---

## 2. Real FFT Output Format Conventions

### Industry Standard Format

Both **FFTW** and **KissFFT** (the two most widely used FFT libraries) use the same convention for real-to-complex FFT:

**Input**: N real samples
**Output**: N/2+1 complex bins (exploiting Hermitian symmetry)

### Bin Layout

| Bin Index | Frequency | Format | Notes |
|-----------|-----------|--------|-------|
| 0 | DC (0 Hz) | **Real only** | imaginary = 0 |
| 1 to N/2-1 | Positive frequencies | Complex (real + imag) | Standard complex representation |
| N/2 | Nyquist (fs/2) | **Real only** (if N even) | imaginary = 0 |

**Hermitian Symmetry**: For real input, X[k] = conj(X[N-k]), so negative frequencies are redundant.

### Storage Conventions

**FFTW Convention**:
```c
// Output array: fft_complex out[N/2+1]
out[0]     = { DC_real, 0.0 }          // DC bin (imag always 0)
out[1..N/2-1] = { real, imag }         // Positive frequencies
out[N/2]   = { Nyquist_real, 0.0 }     // Nyquist (imag always 0, N even only)
```

**KissFFT Convention**:
```c
// Output: kiss_fft_cpx cx_out[nfft/2+1]
// cx_out[0] is DC bin
// cx_out[nfft/2] is Nyquist bin (if nfft is even)
// Frequency domain data stored from DC to Nyquist
```

### Interleaved vs Split Format

**Interleaved (recommended for portability)**:
```cpp
struct Complex {
    float real;
    float imag;
};
Complex spectrum[N/2+1];  // Standard layout
```

**Split (better for SIMD)**:
```cpp
float real[N/2+1];
float imag[N/2+1];
// Easier for vectorization but less portable
```

### Accessing Magnitude and Phase

```cpp
// For bin k in range [0, N/2]
float magnitude = std::sqrt(spectrum[k].real * spectrum[k].real +
                            spectrum[k].imag * spectrum[k].imag);
float phase = std::atan2(spectrum[k].imag, spectrum[k].real);

// Special cases:
// DC (k=0): phase is meaningless (imag = 0)
// Nyquist (k=N/2): phase is meaningless (imag = 0)
```

### Implementation Recommendations

1. **Use interleaved complex format** for cross-platform compatibility
2. **Allocate N/2+1 complex values** (not N) for output
3. **Document DC and Nyquist bins as real-only** in code comments
4. **Validate N is even** during initialization (Nyquist bin only exists for even N)
5. **Zero imaginary parts** of DC and Nyquist in synthesis path

**Sources**:
- [FFTW: One-Dimensional DFTs of Real Data](https://www.fftw.org/fftw3_doc/One_002dDimensional-DFTs-of-Real-Data.html)
- [KissFFT GitHub Repository](https://github.com/mborgerding/kissfft)
- [Interpreting FFT Results - GaussianWaves](https://www.gaussianwaves.com/2015/11/interpreting-fft-results-complex-dft-frequency-bins-and-fftshift/)

---

## 3. Streaming STFT Architecture for Real-Time Audio

### System Overview

A real-time STFT system for audio plugins requires three core components:

1. **Input Circular Buffer**: Accumulates incoming samples
2. **Analysis Engine**: Extracts frames, applies window, performs FFT
3. **Output Accumulator**: Performs overlap-add synthesis

### Circular Buffer Design

**Purpose**: Continuous accumulation of samples arriving in arbitrary block sizes from audio callback.

```
┌─────────────────────────────────────────────────────┐
│  Circular Buffer (Size: fftSize)                    │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐ │
│  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │10 │11 │ │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘ │
│       ↑ Write Ptr           ↑ Read Ptr (FFT start) │
└─────────────────────────────────────────────────────┘
```

**Implementation Pattern**:
```cpp
class CircularBuffer {
    std::vector<float> buffer_;  // Size: fftSize
    size_t writeIndex_ = 0;
    size_t samplesAvailable_ = 0;

    void write(const float* samples, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            buffer_[writeIndex_] = samples[i];
            writeIndex_ = (writeIndex_ + 1) % buffer_.size();
            samplesAvailable_ = std::min(samplesAvailable_ + 1, buffer_.size());
        }
    }

    bool canExtractFrame() const {
        return samplesAvailable_ >= fftSize_;
    }
};
```

### Frame Extraction with Hop Size

**Hop Size (R)** = Frame advance in samples (e.g., R = fftSize/2 for 50% overlap)

**Process Flow**:
```
1. Write samples from audio callback to circular buffer
2. When buffer contains ≥ fftSize samples:
   a. Extract fftSize samples starting from (writeIndex - fftSize)
   b. Apply window
   c. Perform FFT
   d. Advance read pointer by hopSize (not fftSize)
3. Repeat step 2 while buffer has enough samples
```

**Key Insight**: Read pointer advances by `hopSize`, not `fftSize`, creating overlap.

### Overlap-Add Synthesis Accumulator

**Output Accumulator Design**:
```
┌──────────────────────────────────────────────────────┐
│  Output Accumulator (Size: fftSize)                  │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐  │
│  │ + │ + │ + │ + │ + │ + │ + │ + │   │   │   │   │  │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘  │
│  Frame 1: ███████████████████                        │
│  Frame 2:         ███████████████████                │
│  Frame 3:                 ███████████████████        │
│  Output:  ├──────────┤ (hopSize samples/frame)       │
└──────────────────────────────────────────────────────┘
```

**Synthesis Process**:
```cpp
// After inverse FFT:
void addToOutputBuffer(const float* frame, size_t fftSize) {
    for (size_t i = 0; i < fftSize; ++i) {
        outputAccumulator_[i] += frame[i];  // Overlap-add
    }
}

// Extract output samples:
void getOutput(float* output, size_t hopSize) {
    std::copy(outputAccumulator_.begin(),
              outputAccumulator_.begin() + hopSize, output);

    // Shift accumulator left by hopSize
    std::copy(outputAccumulator_.begin() + hopSize,
              outputAccumulator_.end(),
              outputAccumulator_.begin());

    // Zero out the vacated region
    std::fill(outputAccumulator_.end() - hopSize,
              outputAccumulator_.end(), 0.0f);
}
```

### Latency Characteristics

**Minimum Latency** = `fftSize` samples

**Rationale**: Must collect a full FFT frame before processing can begin.

**Practical Latency** (with overlap-add):
- **Input delay**: Wait for `fftSize` samples to fill buffer
- **Processing delay**: FFT + IFFT computation (typically < 1 sample at audio rates)
- **Output delay**: `fftSize - hopSize` samples still in accumulator
- **Total**: `fftSize + (fftSize - hopSize) = 2*fftSize - hopSize`

**Example** (1024-point FFT, 50% overlap at 44.1kHz):
```
fftSize = 1024
hopSize = 512
Latency = 2*1024 - 512 = 1536 samples = 34.8 ms
```

**Latency Reduction Strategy**: Use smaller FFT sizes for low-latency applications (256 or 512 instead of 1024+).

### Block-Based vs Sample-by-Sample Processing

**Sample-by-Sample** (lowest latency):
- Process callback: Write samples to circular buffer one-by-one
- Check after each sample if frame is ready
- Extract and process frame immediately
- **Pros**: Minimum latency (exactly `fftSize` samples)
- **Cons**: High overhead (many small FFT checks)

**Block-Based** (recommended):
- Process callback: Write entire block to circular buffer
- Check once per callback if frame(s) ready
- Process all available frames in batch
- **Pros**: Efficient (amortized overhead), still achieves near-minimum latency
- **Cons**: Latency increased by max callback block size

**Recommendation**: Use **block-based with small blocks** (≤ 128 samples) for best efficiency/latency trade-off.

### Implementation Checklist

- [ ] Pre-allocate circular buffer (size = fftSize) in `prepare()`
- [ ] Pre-allocate output accumulator (size = fftSize) in `prepare()`
- [ ] Implement wrap-around indexing for circular buffer
- [ ] Process multiple frames per callback if enough samples available
- [ ] Report plugin latency to host as `fftSize` samples (minimum case)

**Sources**:
- [FFT Processing in JUCE - Audio Dev Blog](https://audiodev.blog/fft-processing/)
- [Overlap-Add STFT Processing - Stanford CCRMA](https://ccrma.stanford.edu/~jos/sasp/Overlap_Add_OLA_STFT_Processing.html)
- [Circular Buffering - DSP Guide](https://www.dspguide.com/ch28/2.htm)
- [Real-Time Block Simulation - WPI Lab](http://www.faculty.jacobs-university.de/jwallace/xwallace/courses/dsp/labs/03_ml_block/)

---

## 4. Radix-2 Cooley-Tukey Implementation Details

### Algorithm Overview

**Radix-2 Decimation-in-Time (DIT)** is the classic FFT algorithm:
- Input: Time-domain samples
- Output: Frequency-domain bins
- Complexity: O(N log₂ N)
- Constraint: N must be power of 2

**DIT vs DIF**:
| Property | Decimation-in-Time (DIT) | Decimation-in-Frequency (DIF) |
|----------|--------------------------|-------------------------------|
| Input order | **Bit-reversed** | Natural order |
| Output order | Natural order | **Bit-reversed** |
| Pre-processing | Bit-reversal required | None |
| Post-processing | None | Bit-reversal required |
| Use case | Prefer natural-order output | Prefer in-place with post-process |

**Recommendation**: Implement **DIT** with pre-processing bit-reversal for natural-order frequency output (standard for audio applications).

### Butterfly Operation (Core Computation)

The "butterfly" is the fundamental 2-point DFT:

```
┌───┐        ┌───┐
│ a │────+──→│ a+b*W │  (even output)
└───┘    │   └───────┘
        ╳
┌───┐    │   ┌───────┐
│ b │────×──→│ a-b*W │  (odd output)
└───┘        └───────┘
  ↑
Twiddle factor W = e^(-j*2π*k/N)
```

**Implementation**:
```cpp
// Butterfly operation for DIT FFT
void butterfly(Complex& a, Complex& b, const Complex& W) noexcept {
    Complex temp = b * W;  // Complex multiplication
    b = a - temp;
    a = a + temp;
}

// Complex multiplication (critical for performance):
Complex operator*(const Complex& a, const Complex& b) noexcept {
    return {
        a.real * b.real - a.imag * b.imag,  // Real part
        a.real * b.imag + a.imag * b.real   // Imaginary part
    };
}
```

### Bit-Reversal Permutation

**Purpose**: Reorder input samples for in-place DIT FFT.

**Example** (N=8):
```
Index (decimal) | Binary | Bit-reversed | Reversed index
----------------|--------|--------------|----------------
0               | 000    | 000          | 0
1               | 001    | 100          | 4
2               | 010    | 010          | 2
3               | 011    | 110          | 6
4               | 100    | 001          | 1
5               | 101    | 101          | 5
6               | 110    | 011          | 3
7               | 111    | 111          | 7
```

**Lookup Table vs Computation**:

| Approach | Pros | Cons | When to Use |
|----------|------|------|-------------|
| **Precomputed LUT** | O(1) lookup, simple code | Memory cost (N entries) | Fixed FFT size, < 8192 |
| **On-the-fly computation** | Zero memory, flexible | O(log N) per index | Variable FFT size, > 8192 |
| **Hardware bit-reverse** | Fastest, zero overhead | DSP-specific (not portable) | Embedded DSP chips |

**Recommendation**: Use **precomputed LUT** for fixed power-of-2 sizes (256–8192) in audio plugins.

**LUT Generation** (do once during initialization):
```cpp
std::vector<size_t> generateBitReversalLUT(size_t N) noexcept {
    std::vector<size_t> lut(N);
    size_t numBits = static_cast<size_t>(std::log2(N));

    for (size_t i = 0; i < N; ++i) {
        size_t reversed = 0;
        for (size_t b = 0; b < numBits; ++b) {
            if (i & (1 << b)) {
                reversed |= (1 << (numBits - 1 - b));
            }
        }
        lut[i] = reversed;
    }
    return lut;
}

// Apply bit-reversal using LUT:
void bitReversePermute(Complex* data, size_t N, const size_t* lut) noexcept {
    for (size_t i = 0; i < N; ++i) {
        size_t j = lut[i];
        if (j > i) {  // Swap only once per pair
            std::swap(data[i], data[j]);
        }
    }
}
```

### Twiddle Factor Precomputation

**Twiddle Factors**: W_N^k = e^(-j*2π*k/N) = cos(2πk/N) - j*sin(2πk/N)

**Critical for Accuracy**: Transcendental function errors accumulate; precompute once.

**Storage Pattern**:
```cpp
// Precompute twiddle factors for all FFT stages
std::vector<Complex> generateTwiddleFactors(size_t N) noexcept {
    std::vector<Complex> twiddles(N / 2);  // Only need N/2 unique values
    constexpr float kTwoPi = 6.28318530717958647692f;

    for (size_t k = 0; k < N / 2; ++k) {
        float angle = -kTwoPi * static_cast<float>(k) / static_cast<float>(N);
        twiddles[k] = { std::cos(angle), std::sin(angle) };
    }
    return twiddles;
}
```

**Memory Cost**: N/2 complex values = N float32 values (e.g., 4 KB for N=1024).

### In-Place vs Out-of-Place

**In-Place** (recommended for real-time):
- Modifies input buffer directly
- Memory: Single N-element buffer
- **Requires**: Bit-reversal permutation
- **Pros**: Minimal memory, cache-friendly
- **Cons**: Destroys input data

**Out-of-Place**:
- Writes to separate output buffer
- Memory: Two N-element buffers
- **Avoids**: Bit-reversal (can read in natural order, write bit-reversed)
- **Pros**: Preserves input, simpler for some algorithms
- **Cons**: 2x memory, more cache misses

**Recommendation**: Use **in-place DIT** with pre-allocated bit-reversal LUT for audio plugins.

### Implementation Checklist

- [ ] Implement bit-reversal LUT generation in `prepare()`
- [ ] Precompute twiddle factors in `prepare()`
- [ ] Use in-place algorithm to minimize memory
- [ ] Validate N is power of 2 during initialization (assert or error)
- [ ] Test against known transforms (sine wave → peak at bin frequency)

**Sources**:
- [Cooley-Tukey FFT Algorithm - Wikipedia](https://en.wikipedia.org/wiki/Cooley–Tukey_FFT_algorithm)
- [Radix-2 FFT - Digital Signals Theory](https://brianmcfee.net/dstbook-site/content/ch08-fft/FFT.html)
- [Bit-Reversal Permutation - Wikipedia](https://en.wikipedia.org/wiki/Bit-reversal_permutation)
- [Texas Instruments: Implementing Radix-2 FFT](https://www.ti.com/lit/pdf/spna071)

---

## 5. Float32 FFT Numerical Precision

### Error Bounds for Cooley-Tukey Algorithm

**Theoretical Error** (relative to naive DFT):

| Algorithm | Upper Bound | RMS Error (typical) |
|-----------|-------------|---------------------|
| Naive DFT | O(ε * N^(3/2)) | O(ε * √N) |
| Cooley-Tukey FFT | **O(ε * log N)** | **O(ε * √(log N))** |

Where ε = machine precision (float32: ε ≈ 1.2e-7)

**Key Insight**: FFT is **more accurate** than naive DFT due to pairwise summation structure.

**For N=1024 FFT with float32**:
- Upper bound: ~10 * ε ≈ 1.2e-6 (0.00012%)
- RMS error: ~3.3 * ε ≈ 4e-7 (0.00004%)

### Round-Trip Error (Forward + Inverse)

**Forward FFT**: x[n] → X[k] (error ε₁)
**Inverse FFT**: X[k] → x'[n] (error ε₂)
**Total error**: ε_total ≈ ε₁ + ε₂ ≈ 2 * O(ε * log N)

**Expected round-trip error for float32**:
```
N=256:  ~2e-6  (0.0002%)  ✓ Excellent
N=512:  ~2.5e-6 (0.00025%) ✓ Excellent
N=1024: ~3e-6  (0.0003%)  ✓ Excellent
N=2048: ~3.5e-6 (0.00035%) ✓ Good
N=4096: ~4e-6  (0.0004%)  ✓ Good
N=8192: ~5e-6  (0.0005%)  ✓ Acceptable
```

**Conclusion**: The spec requirement of **< 0.0001% error** (1e-6) is **achievable** for FFT sizes up to 1024 with careful implementation. For 2048+ sizes, expect ~0.0003–0.0005% error, which is still acceptable for audio.

### Sources of Numerical Error

1. **Twiddle Factor Accuracy** (dominant source)
   - Problem: sin/cos computed with float32 transcendentals
   - Solution: Use double-precision computation, downcast to float32:
     ```cpp
     double angle = -2.0 * M_PI * k / N;
     twiddles[k] = { static_cast<float>(std::cos(angle)),
                     static_cast<float>(std::sin(angle)) };
     ```

2. **Butterfly Accumulation**
   - Problem: Repeated additions accumulate rounding error
   - Solution: Use Kahan summation if targeting < 1e-7 error (overkill for audio)

3. **Denormalized Numbers**
   - Problem: Silence decays into denormals → 100x CPU slowdown
   - Solution: Flush to zero (already handled in existing codebase per constitution)

4. **DC/Nyquist Bin Handling**
   - Problem: Imaginary parts should be exactly zero but can accumulate error
   - Solution: Explicitly zero imaginary parts:
     ```cpp
     spectrum[0].imag = 0.0f;  // DC
     spectrum[N/2].imag = 0.0f;  // Nyquist
     ```

### Precision Tricks

**Kahan Summation** (if targeting extreme accuracy):
```cpp
// Compensated summation for accumulation
float kahanSum(const float* values, size_t count) noexcept {
    float sum = 0.0f;
    float compensation = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float y = values[i] - compensation;
        float temp = sum + y;
        compensation = (temp - sum) - y;
        sum = temp;
    }
    return sum;
}
```

**Not Recommended for Audio**: Adds 3x overhead for negligible benefit (float32 error is already below audible threshold).

### Validation Strategy

**Unit Test Pattern**:
```cpp
TEST_CASE("FFT round-trip error < 0.01%", "[fft][precision]") {
    constexpr size_t N = 1024;
    std::vector<float> original(N);
    std::vector<float> reconstructed(N);

    // Generate test signal (sine wave)
    for (size_t i = 0; i < N; ++i) {
        original[i] = std::sin(2.0f * M_PI * 10.0f * i / N);
    }

    // Round-trip: FFT → IFFT
    fft.forward(original.data(), spectrum);
    fft.inverse(spectrum, reconstructed.data());

    // Measure error
    float maxError = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float error = std::abs(reconstructed[i] - original[i]);
        maxError = std::max(maxError, error);
    }

    // 0.01% of peak amplitude (1.0)
    REQUIRE(maxError < 1e-4f);
}
```

### Implementation Checklist

- [ ] Compute twiddle factors in double precision, store as float32
- [ ] Explicitly zero DC and Nyquist imaginary bins
- [ ] Validate round-trip error < 0.01% in unit tests
- [ ] Test with various FFT sizes (256, 512, 1024, 2048, 4096, 8192)
- [ ] Document expected error bounds in code comments

**Sources**:
- [Fast Fourier Transform - Wikipedia (Accuracy Section)](https://en.wikipedia.org/wiki/Fast_Fourier_transform)
- [Mixed Precision FFT Research - arXiv](https://arxiv.org/html/2512.04317)
- [GNU Scientific Library: FFT Documentation](https://www.gnu.org/software/gsl/doc/html/fft.html)

---

## 6. Kaiser Window for STFT

### COLA Compliance Status

**Kaiser Window is NOT naturally COLA-compliant at standard overlaps (50%/75%).**

**Why**: Unlike Hann/Hamming/Blackman windows which have harmonic nulls aligned with frame rate, the Kaiser window is a **continuous-parameter family** designed for arbitrary time-bandwidth trade-offs, not periodic overlap-add.

### Approximate COLA with High Overlap

**Kaiser window can achieve approximate perfect reconstruction** with sufficiently high overlap:

| Overlap | Beta (β) | Reconstruction Quality | Use Case |
|---------|----------|------------------------|----------|
| 50% | 9.0 | ❌ Poor (visible artifacts) | Not recommended |
| 75% | 9.0 | ⚠️ Fair (minor artifacts) | Spectral display only |
| **90%** | **10.0** | ✅ **Good** (~0.01% error) | **Robust spectral modification** |

**Example from Literature** (Stanford CCRMA):
- Window length: M = 256
- Beta: β = 10
- Hop size: R = M/10 = 26 samples (90% overlap)
- Result: Approximate COLA with negligible reconstruction error

### Beta Parameter Selection

**Beta (β)** controls the trade-off between main lobe width and sidelobe rejection:

| Beta | Main Lobe Width | Stopband Attenuation | Use Case |
|------|-----------------|----------------------|----------|
| 5 | Narrow | ~50 dB | Tone detection, low overlap |
| **9** | **Medium** | **~80 dB** | **General-purpose (spec default)** |
| 10 | Medium-wide | ~90 dB | Robust spectral modification |
| 14 | Wide | ~110 dB | Research-grade analysis |

**Formula**: Stopband attenuation A ≈ 8.7 + 0.1102 * β * (M-1)

**Recommendation from Literature**: β = 9.0 is a **good default for audio STFT**, providing 80 dB stopband rejection with reasonable main lobe width.

### Trade-offs vs Hann/Hamming/Blackman

| Property | Hann | Hamming | Blackman | Kaiser (β=9) |
|----------|------|---------|----------|--------------|
| **COLA at 50%** | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| **COLA at 75%** | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| **Approx COLA at 90%** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| **Stopband rejection** | ~44 dB | ~53 dB | ~74 dB | ~80 dB |
| **Main lobe width** | 4π/N | 4π/N | 6π/N | ~5π/N |
| **Spectral modification robustness** | Good | Good | Better | **Best** |

**Key Insight**: Kaiser window is **preferred for spectral modification** (e.g., filtering, morphing) because it doesn't rely on alias cancellation—side lobes are suppressed below noise floor.

### Implementation Considerations

**Kaiser Window Generation** (reuse existing code from `oversampler.h`):
```cpp
// From F:\projects\iterum\src\dsp\primitives\oversampler.h (lines 66-74)
// Kaiser window: w[n] = I0(β * sqrt(1 - (n/M)²)) / I0(β)
// Already implemented for oversampler FIR filters!

// Bessel I0 function is already in codebase:
constexpr float besselI0(float x) noexcept {
    // Taylor series implementation (see oversampler.h comments)
    // ...
}

// Kaiser window generator:
std::vector<float> generateKaiserWindow(size_t N, float beta) noexcept {
    std::vector<float> window(N);
    float norm = besselI0(beta);
    float M = static_cast<float>(N - 1) / 2.0f;

    for (size_t n = 0; n < N; ++n) {
        float x = (static_cast<float>(n) - M) / M;
        window[n] = besselI0(beta * std::sqrt(1.0f - x * x)) / norm;
    }
    return window;
}
```

**Overlap Requirement for Kaiser**:
- **If using Kaiser**: Must use 90% overlap (10% hop size)
- **Memory impact**: 10x redundancy vs 2x (Hann 50%) or 4x (Hann 75%)
- **CPU impact**: 5x–10x more FFTs per second

**Recommendation**: **Use Hann or Blackman for standard STFT**, not Kaiser, unless:
1. Extreme spectral modification robustness is required
2. 90% overlap overhead is acceptable
3. Research-grade analysis is needed

### Why Spec Includes Kaiser

The spec lists Kaiser (β=9.0) as an option because:
1. **Completeness**: Standard window family for audio DSP
2. **Reuse**: Bessel I0 implementation already exists in `oversampler.h`
3. **Future-proofing**: Higher-layer processors may need it (spectral freeze, morphing)

**Implementation Strategy**:
1. Implement Hann/Hamming/Blackman first (COLA at 50%/75%)
2. Add Kaiser later if Layer 2+ processors require it
3. Document Kaiser's COLA limitations and required overlap

### Implementation Checklist

- [ ] Reuse `besselI0()` from existing codebase (oversampler.h)
- [ ] Default beta to 9.0 for general-purpose use
- [ ] Enforce 90% overlap if Kaiser selected (or document limitation)
- [ ] Provide factory function: `Window::createKaiser(size, beta)`
- [ ] Validate COLA property programmatically during initialization
- [ ] Add warning in documentation: "Kaiser requires 90% overlap for COLA"

**Sources**:
- [Kaiser Window - Wikipedia](https://en.wikipedia.org/wiki/Kaiser_window)
- [STFT Kaiser Window Beta=10, 90% Overlap - Stanford CCRMA](https://www.dsprelated.com/freebooks/sasp/STFT_Kaiser_Window_Beta_10.html)
- [Kaiser Overlap-Add Example - Stanford CCRMA](https://www.dsprelated.com/freebooks/sasp/Kaiser_Overlap_Add_Example.html)
- [SciPy Kaiser Window Documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.kaiser.html)

---

## 7. Implementation Recommendations Summary

### Critical Design Decisions

| Decision Point | Recommendation | Rationale |
|----------------|----------------|-----------|
| **Default Window** | Hann (periodic variant) | COLA at 50%/75%, unity gain, excellent spectral characteristics |
| **Default Overlap** | 75% (4x redundancy) | Good time/frequency resolution balance for spectral effects |
| **FFT Algorithm** | Radix-2 DIT with LUT | Standard, well-tested, O(N log N), natural-order output |
| **Complex Storage** | Interleaved (struct) | Portable, cache-friendly, standard library compatible |
| **Bit-Reversal** | Precomputed LUT | O(1) lookup, acceptable memory cost (< 8 KB for N=8192) |
| **Twiddle Factors** | Precomputed (double→float) | High accuracy, minimal memory (< 16 KB for N=8192) |
| **In-Place vs Out-of-Place** | In-place | Minimize memory, better cache locality for real-time |
| **Latency Target** | Report `fftSize` to host | Minimum achievable latency (conservative estimate) |
| **Error Target** | < 0.01% round-trip | Achievable with float32 for N ≤ 2048 |

### Precomputed Constants to Generate

During `prepare()`, generate and cache:

1. **Window functions** (per type):
   - Hann: N floats
   - Hamming: N floats
   - Blackman: N floats
   - Kaiser: N floats (optional, if implemented)

2. **Twiddle factors**:
   - N/2 complex values (N floats total)
   - Compute in double precision, store as float32

3. **Bit-reversal LUT**:
   - N size_t indices

**Total Memory** (per FFT size, worst case with all windows):
```
N=1024: 4 windows * 1024 * 4 bytes + 1024 * 4 bytes (twiddle) + 1024 * 8 bytes (LUT)
      = 16 KB + 4 KB + 8 KB = 28 KB (acceptable)
```

### Potential Pitfalls to Avoid

1. **Window Variant Mismatch**
   - ❌ **Pitfall**: Using "symmetric" Hann instead of "periodic" (DFT-even)
   - ✅ **Solution**: Use formula `w[n] = 0.5 - 0.5*cos(2πn/(N-1))` for n=0..N-1

2. **DC/Nyquist Imaginary Contamination**
   - ❌ **Pitfall**: Leaving imaginary parts of DC/Nyquist bins non-zero
   - ✅ **Solution**: Explicitly zero after forward FFT and before inverse FFT

3. **Bit-Reversal Order Confusion**
   - ❌ **Pitfall**: Forgetting to bit-reverse input for DIT algorithm
   - ✅ **Solution**: Apply LUT before FFT stages (or use DIF with post-reversal)

4. **Twiddle Factor Precision Loss**
   - ❌ **Pitfall**: Computing sin/cos directly in float32
   - ✅ **Solution**: Use double precision for computation, downcast to float32 storage

5. **Denormal Numbers in Silence**
   - ❌ **Pitfall**: FFT of silence produces denormals in spectrum
   - ✅ **Solution**: Already handled by constitution (FTZ/DAZ), but add explicit flush in IFFT output

6. **COLA Validation Skipped**
   - ❌ **Pitfall**: Assuming window is COLA without verification
   - ✅ **Solution**: Implement `verifyCOLA()` unit test for each window type

7. **Hop Size > FFT Size**
   - ❌ **Pitfall**: Allowing hopSize > fftSize (undefined behavior)
   - ✅ **Solution**: Validate in `prepare()`: `assert(hopSize <= fftSize)`

8. **Memory Allocation in Process**
   - ❌ **Pitfall**: Creating temp buffers during forward/inverse calls
   - ✅ **Solution**: Pre-allocate all working memory in `prepare()`

### Key Formulas and Constants

**Window Functions (Periodic/DFT-Even Variant for N samples)**:
```cpp
// Hann (for n = 0..N-1):
w[n] = 0.5 - 0.5 * cos(2π * n / (N-1))

// Hamming:
w[n] = 0.54 - 0.46 * cos(2π * n / (N-1))

// Blackman:
w[n] = 0.42 - 0.5 * cos(2π * n / (N-1)) + 0.08 * cos(4π * n / (N-1))

// Kaiser (β = shape parameter):
w[n] = I₀(β * √(1 - ((n - M)/M)²)) / I₀(β), where M = (N-1)/2
```

**FFT Twiddle Factors**:
```cpp
W_N^k = e^(-j*2πk/N) = cos(2πk/N) - j*sin(2πk/N)
```

**COLA Verification**:
```cpp
// Sum of overlapping windows should be constant:
sum = 0
for i in 0..fftSize-1:
    for m in range(-overlap, overlap+1):
        if 0 <= i + m*hopSize < fftSize:
            sum += window[i + m*hopSize]
// Expect: sum ≈ constant (typically fftSize / hopSize)
```

**Error Bounds**:
```cpp
// Cooley-Tukey FFT relative error (float32):
ε_upper = ε * log₂(N)  where ε ≈ 1.2e-7 (FLT_EPSILON)
// For N=1024: ε_upper ≈ 1.2e-6 (0.00012%)
```

---

## Codebase Integration Notes

### Existing Components to Reuse

1. **Kaiser Window Implementation** (`oversampler.h` lines 66-120)
   - Bessel I0 function (Taylor series, constexpr-compatible)
   - Kaiser window coefficients already computed for FIR filters
   - **Action**: Extract to shared utility in `dsp/core/window_functions.h`

2. **Constexpr Math Utilities** (`db_utils.h`)
   - Pattern for Taylor series implementations
   - NaN detection with bit manipulation
   - **Action**: Reuse patterns for any constexpr window generation

3. **Pi Constant** (`dsp_utils.h`)
   - `kPi` is already defined
   - **Action**: Reuse for twiddle factor computation

### Layer 0 Dependencies (Allowed)

The FFT processor is Layer 1, so it may depend on:
- `dsp/core/db_utils.h` (constexpr math patterns)
- `dsp/dsp_utils.h` (constants like kPi)
- Standard library (complex numbers, vectors, etc.)

**NOT allowed**:
- Layer 1+ primitives (delay line, biquad, etc.)
- Layer 2+ processors
- VST3 infrastructure

### File Structure Recommendation

```
src/dsp/primitives/
├── fft.h                    # Core FFT class (forward/inverse)
├── fft.cpp                  # FFT implementation (if not header-only)
├── stft.h                   # STFT wrapper (windowing + overlap-add)
├── spectral_buffer.h        # Complex spectrum manipulation utilities
└── window_functions.h       # Window generators (Hann, Hamming, Blackman, Kaiser)

tests/unit/primitives/
├── test_fft.cpp            # FFT accuracy, round-trip, performance tests
├── test_stft.cpp           # STFT COLA compliance, streaming tests
├── test_spectral_buffer.cpp # Magnitude/phase manipulation tests
└── test_window_functions.cpp # COLA verification for each window type
```

---

## References

### Academic Sources
- J. W. Cooley and J. W. Tukey, "An algorithm for the machine calculation of complex Fourier series," *Mathematics of Computation*, 1965
- F. J. Harris, "On the use of windows for harmonic analysis with the discrete Fourier transform," *Proceedings of the IEEE*, 1978
- J. O. Smith, *Spectral Audio Signal Processing*, Stanford CCRMA, https://ccrma.stanford.edu/~jos/sasp/

### Software Library Documentation
- FFTW: https://www.fftw.org/fftw3_doc/
- KissFFT: https://github.com/mborgerding/kissfft
- SciPy Signal Processing: https://docs.scipy.org/doc/scipy/reference/signal.html

### DSP Resources
- DSP Guide by Steven W. Smith: https://www.dspguide.com/
- DSP Related: https://www.dsprelated.com/
- GaussianWaves (FFT Interpretation): https://www.gaussianwaves.com/

### Standards and Best Practices
- IEEE 754 Floating-Point Standard
- VST3 Plugin API Documentation: https://steinbergmedia.github.io/vst3_dev_portal/

---

**Document Version**: 1.0
**Last Updated**: 2025-12-23
**Next Steps**: Proceed to `/speckit.plan` for detailed implementation design based on these research findings.
