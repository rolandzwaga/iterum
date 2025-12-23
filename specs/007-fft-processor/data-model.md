# Data Model: FFT Processor

**Feature**: 007-fft-processor
**Layer**: 1 (DSP Primitives)
**Namespace**: `Iterum::DSP`

---

## Entity Relationship Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Layer 1: DSP Primitives                   │
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │  FFT         │    │  Window      │    │ SpectralBuffer│      │
│  │              │    │              │    │              │       │
│  │ forward()    │───▶│ generate()   │    │ getMagnitude()│      │
│  │ inverse()    │    │ verify COLA  │    │ getPhase()   │       │
│  └──────────────┘    └──────────────┘    │ setMagnitude()│      │
│         │                   │            │ setPhase()   │       │
│         ▼                   ▼            └──────────────┘       │
│  ┌──────────────────────────────────────────────┐     ▲        │
│  │                   STFT                        │     │        │
│  │                                               │─────┘        │
│  │ analyze(input) ──▶ SpectralBuffer             │              │
│  │ synthesize(SpectralBuffer) ──▶ output        │              │
│  └──────────────────────────────────────────────┘              │
│                          │                                      │
│                          ▼                                      │
│  ┌──────────────────────────────────────────────┐              │
│  │               OverlapAdd                      │              │
│  │                                               │              │
│  │ Accumulates windowed IFFT frames              │              │
│  │ Extracts output at hop rate                  │              │
│  └──────────────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Layer 0: Core Utilities                      │
│                                                                  │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │ window_functions │    │ dsp_utils.h      │                   │
│  │                  │    │                  │                   │
│  │ generateHann()   │    │ kPi, kTwoPi      │                   │
│  │ generateHamming()│    │                  │                   │
│  │ generateBlackman()│   │                  │                   │
│  │ generateKaiser() │    │                  │                   │
│  │ besselI0()       │    │                  │                   │
│  └──────────────────┘    └──────────────────┘                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Entities

### 1. Complex (POD struct)

**Purpose**: Simple complex number storage for FFT operations.

| Field | Type | Description |
|-------|------|-------------|
| `real` | `float` | Real component |
| `imag` | `float` | Imaginary component |

**Operations**:
- Arithmetic operators: `+`, `-`, `*`, conjugate
- No virtual functions (POD for performance)

**Invariants**:
- DC bin (index 0): `imag == 0`
- Nyquist bin (index N/2): `imag == 0`

---

### 2. FFT

**Purpose**: Core forward/inverse Fast Fourier Transform.

| Attribute | Type | Description |
|-----------|------|-------------|
| `size_` | `size_t` | FFT size (power of 2: 256-8192) |
| `bitReversalLUT_` | `std::vector<size_t>` | Precomputed bit-reversal indices |
| `twiddleFactors_` | `std::vector<Complex>` | Precomputed W_N^k values |
| `workBuffer_` | `std::vector<Complex>` | In-place computation buffer |

**Operations**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(size_t fftSize) noexcept` | Allocate LUTs and buffers |
| `forward` | `void forward(const float* input, Complex* output) noexcept` | Real-to-complex FFT (N real → N/2+1 complex) |
| `inverse` | `void inverse(const Complex* input, float* output) noexcept` | Complex-to-real IFFT (N/2+1 complex → N real) |
| `reset` | `void reset() noexcept` | Clear work buffer (not LUTs) |
| `size` | `size_t size() const noexcept` | Return configured FFT size |

**State Machine**:
```
┌──────────────┐   prepare()   ┌──────────────┐
│ Unprepared   │──────────────▶│   Prepared   │
└──────────────┘               └──────────────┘
                                      │
                                      │ forward() / inverse()
                                      ▼
                               ┌──────────────┐
                               │  Processing  │
                               │  (stateless) │
                               └──────────────┘
```

**Validation Rules**:
- `fftSize` must be power of 2 in range [256, 8192]
- `prepare()` must be called before `forward()`/`inverse()`

---

### 3. Window (Namespace Functions)

**Purpose**: Generate analysis window coefficients.

**Window Types** (enum):
```cpp
enum class WindowType : uint8_t {
    Hann,       // 0.5 - 0.5*cos(2πn/(N-1))
    Hamming,    // 0.54 - 0.46*cos(2πn/(N-1))
    Blackman,   // 0.42 - 0.5*cos(2πn/(N-1)) + 0.08*cos(4πn/(N-1))
    Kaiser      // I0(β*sqrt(1-(n/M)²)) / I0(β)
};
```

**Operations** (free functions in `Window` namespace):

| Function | Signature | Description |
|----------|-----------|-------------|
| `generate` | `std::vector<float> generate(WindowType type, size_t size, float kaiserBeta = 9.0f)` | Generate window coefficients |
| `generateHann` | `void generateHann(float* output, size_t size) noexcept` | Fill buffer with Hann window |
| `generateHamming` | `void generateHamming(float* output, size_t size) noexcept` | Fill buffer with Hamming window |
| `generateBlackman` | `void generateBlackman(float* output, size_t size) noexcept` | Fill buffer with Blackman window |
| `generateKaiser` | `void generateKaiser(float* output, size_t size, float beta) noexcept` | Fill buffer with Kaiser window |
| `verifyCOLA` | `bool verifyCOLA(const float* window, size_t size, size_t hopSize, float tolerance = 1e-6f) noexcept` | Verify COLA property |
| `besselI0` | `float besselI0(float x) noexcept` | Modified Bessel function (for Kaiser) |

**COLA Properties**:

| Window | 50% Overlap | 75% Overlap | 90% Overlap |
|--------|-------------|-------------|-------------|
| Hann | ✅ Unity | ✅ Unity | ✅ Unity |
| Hamming | ✅ Unity | ✅ Unity | ✅ Unity |
| Blackman | ✅ Unity | ✅ Unity | ✅ Unity |
| Kaiser (β=9) | ❌ | ❌ | ✅ ~Unity |

---

### 4. SpectralBuffer

**Purpose**: Storage and manipulation of complex spectrum data.

| Attribute | Type | Description |
|-----------|------|-------------|
| `data_` | `std::vector<Complex>` | N/2+1 complex bins |
| `size_` | `size_t` | Number of bins |

**Operations**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(size_t fftSize) noexcept` | Allocate for N/2+1 bins |
| `numBins` | `size_t numBins() const noexcept` | Return number of bins |
| `getMagnitude` | `float getMagnitude(size_t bin) const noexcept` | Get |X[k]| |
| `getPhase` | `float getPhase(size_t bin) const noexcept` | Get ∠X[k] in radians |
| `setMagnitude` | `void setMagnitude(size_t bin, float mag) noexcept` | Set magnitude, preserve phase |
| `setPhase` | `void setPhase(size_t bin, float phase) noexcept` | Set phase, preserve magnitude |
| `getReal` | `float getReal(size_t bin) const noexcept` | Get Re(X[k]) |
| `getImag` | `float getImag(size_t bin) const noexcept` | Get Im(X[k]) |
| `setCartesian` | `void setCartesian(size_t bin, float real, float imag) noexcept` | Set real/imag directly |
| `data` | `Complex* data() noexcept` | Raw access for FFT |
| `reset` | `void reset() noexcept` | Zero all bins |

**Bin Index Mapping**:
```
Bin 0:     DC component (0 Hz)
Bin k:     k * sampleRate / fftSize Hz
Bin N/2:   Nyquist frequency (sampleRate / 2)
```

**Validation Rules**:
- `bin` must be in range [0, numBins())
- DC and Nyquist bins should have imag == 0 (enforced by FFT, verified here)

---

### 5. STFT

**Purpose**: Short-Time Fourier Transform for continuous audio streams.

| Attribute | Type | Description |
|-----------|------|-------------|
| `fft_` | `FFT` | Internal FFT processor |
| `window_` | `std::vector<float>` | Analysis window coefficients |
| `windowType_` | `WindowType` | Current window type |
| `fftSize_` | `size_t` | FFT frame size |
| `hopSize_` | `size_t` | Frame advance in samples |
| `inputBuffer_` | `std::vector<float>` | Circular input accumulator |
| `writeIndex_` | `size_t` | Input buffer write position |
| `samplesAvailable_` | `size_t` | Samples ready for analysis |

**Operations**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(size_t fftSize, size_t hopSize, WindowType window, float kaiserBeta = 9.0f) noexcept` | Configure STFT |
| `pushSamples` | `void pushSamples(const float* input, size_t numSamples) noexcept` | Add samples to input buffer |
| `canAnalyze` | `bool canAnalyze() const noexcept` | Check if frame is ready |
| `analyze` | `void analyze(SpectralBuffer& output) noexcept` | Extract windowed FFT frame |
| `reset` | `void reset() noexcept` | Clear buffers |
| `fftSize` | `size_t fftSize() const noexcept` | Return FFT size |
| `hopSize` | `size_t hopSize() const noexcept` | Return hop size |
| `latency` | `size_t latency() const noexcept` | Return processing latency |

**Processing Flow**:
```
Input samples → pushSamples() → [Circular Buffer]
                                      │
         canAnalyze() returns true ◀──┘
                                      │
                    analyze() ────────┘
                        │
                        ▼
            ┌─────────────────┐
            │ 1. Extract frame │
            │ 2. Apply window  │
            │ 3. Forward FFT   │
            └─────────────────┘
                        │
                        ▼
              SpectralBuffer output
```

**Validation Rules**:
- `hopSize` must be ≤ `fftSize`
- `hopSize` must be power of 2 for 50%/75% overlap
- Kaiser window requires `hopSize` ≤ `fftSize/10` for COLA

---

### 6. OverlapAdd

**Purpose**: Reconstruct time-domain audio from spectral frames.

| Attribute | Type | Description |
|-----------|------|-------------|
| `fft_` | `FFT` | Internal IFFT processor |
| `outputBuffer_` | `std::vector<float>` | Accumulator buffer |
| `hopSize_` | `size_t` | Output advance in samples |
| `fftSize_` | `size_t` | Frame size |

**Operations**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(size_t fftSize, size_t hopSize) noexcept` | Configure synthesis |
| `synthesize` | `void synthesize(const SpectralBuffer& input) noexcept` | Add IFFT frame to accumulator |
| `pullSamples` | `void pullSamples(float* output, size_t numSamples) noexcept` | Extract output samples |
| `samplesAvailable` | `size_t samplesAvailable() const noexcept` | Samples ready to pull |
| `reset` | `void reset() noexcept` | Clear accumulator |

**Processing Flow**:
```
SpectralBuffer input
        │
        ▼
   synthesize()
        │
        ▼
┌───────────────────┐
│ 1. Inverse FFT    │
│ 2. Add to accum.  │
│ 3. Shift left     │
└───────────────────┘
        │
        ▼
 [Output Accumulator]
        │
 pullSamples() ◀────┘
        │
        ▼
   Output audio
```

---

## Data Flow: Complete STFT → ISTFT Round-Trip

```
Input Audio          STFT Analysis        Spectral Processing      Overlap-Add Synthesis
    │                     │                      │                         │
    │   pushSamples()     │                      │                         │
    ├────────────────────▶│                      │                         │
    │                     │ (wait for frame)     │                         │
    │   pushSamples()     │                      │                         │
    ├────────────────────▶│                      │                         │
    │                     │ canAnalyze()=true    │                         │
    │                     │                      │                         │
    │                     │ analyze()            │                         │
    │                     ├─────────────────────▶│                         │
    │                     │                      │ (modify spectrum)       │
    │                     │                      │                         │
    │                     │                      │ synthesize()            │
    │                     │                      ├────────────────────────▶│
    │                     │                      │                         │
    │                     │                      │          pullSamples()  │
    │◀───────────────────────────────────────────────────────────────────┤
    │                     │                      │                         │
Output Audio
```

---

## Memory Layout

### FFT Buffers (for N=1024)

| Buffer | Size | Memory |
|--------|------|--------|
| `bitReversalLUT_` | N size_t | 8 KB |
| `twiddleFactors_` | N/2 Complex | 4 KB |
| `workBuffer_` | N Complex | 8 KB |
| **Total per FFT** | | **~20 KB** |

### STFT Buffers (for N=1024, 75% overlap)

| Buffer | Size | Memory |
|--------|------|--------|
| `inputBuffer_` | N float | 4 KB |
| `window_` | N float | 4 KB |
| FFT internal | | 20 KB |
| **Total per STFT** | | **~28 KB** |

### OverlapAdd Buffers (for N=1024)

| Buffer | Size | Memory |
|--------|------|--------|
| `outputBuffer_` | N float | 4 KB |
| FFT internal | | 20 KB |
| **Total per OLA** | | **~24 KB** |

### Complete STFT+OLA System

| Component | Memory |
|-----------|--------|
| STFT | 28 KB |
| SpectralBuffer | 4 KB |
| OverlapAdd | 24 KB |
| **Total** | **~56 KB** |

---

## Validation Rules Summary

| Rule | Entity | Enforcement |
|------|--------|-------------|
| FFT size power of 2 | FFT, STFT | `assert()` in `prepare()` |
| FFT size in [256, 8192] | FFT, STFT | `assert()` in `prepare()` |
| hopSize ≤ fftSize | STFT, OLA | `assert()` in `prepare()` |
| Bin index in range | SpectralBuffer | Clamp to valid range |
| DC/Nyquist imag = 0 | FFT | Explicitly zeroed in `forward()` |
| COLA compliance | Window | `verifyCOLA()` test utility |
