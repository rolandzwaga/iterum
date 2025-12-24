# Research: NoiseGenerator

**Feature**: 013-noise-generator | **Date**: 2025-12-23

## Research Questions

### RQ1: Pink Noise Generation Algorithm

**Question**: What is the best algorithm for real-time pink noise generation?

**Options Evaluated**:

| Algorithm | CPU Cost | Accuracy | Complexity |
|-----------|----------|----------|------------|
| Voss-McCartney | Very Low | Good (-3dB/oct ±1dB) | Low |
| Paul Kellet's Filter | Low | Excellent (-3dB/oct ±0.5dB) | Medium |
| True FIR Filter | High | Excellent | High |
| Stacked Biquads | Medium | Good | Medium |

**Decision**: Paul Kellet's 3-term filter

**Rationale**:
- Excellent accuracy with only 3 filter states
- Minimal CPU overhead (3 multiplies + 3 adds per sample)
- Well-documented and widely used in audio software
- Better spectral accuracy than Voss-McCartney at low frequencies

**Implementation**:
```cpp
// Paul Kellet's pink noise filter
// Filter white noise through these recursive equations:
b0 = 0.99886f * b0 + white * 0.0555179f;
b1 = 0.99332f * b1 + white * 0.0750759f;
b2 = 0.96900f * b2 + white * 0.1538520f;
b3 = 0.86650f * b3 + white * 0.3104856f;
b4 = 0.55000f * b4 + white * 0.5329522f;
b5 = -0.7616f * b5 - white * 0.0168980f;
pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
b6 = white * 0.115926f;
```

**Alternatives Considered**:
- Voss-McCartney: Faster but less accurate at extremes
- True FIR: Too expensive for real-time
- Stacked biquads: More complex setup, similar results

---

### RQ2: Random Number Generator Selection

**Question**: What PRNG should be used for audio noise generation?

**Options Evaluated**:

| Algorithm | Speed | Period | Audio Quality |
|-----------|-------|--------|---------------|
| xorshift32 | Excellent | 2^32-1 | Good |
| xorshift64 | Excellent | 2^64-1 | Excellent |
| LCG (from LFO) | Excellent | 2^31 | Acceptable |
| std::mt19937 | Slow | 2^19937-1 | Excellent |
| PCG | Good | 2^64 | Excellent |

**Decision**: xorshift32

**Rationale**:
- Extremely fast (3 XOR + 3 shift operations)
- Period of 2^32-1 is sufficient for audio (97+ seconds at 44.1kHz before repeat)
- No audible patterns when used for noise generation
- Simpler than PCG, faster than mt19937
- Stateless conversion to float is trivial

**Implementation**:
```cpp
class Xorshift32 {
    uint32_t state_;
public:
    explicit Xorshift32(uint32_t seed = 1) noexcept : state_(seed ? seed : 1) {}

    uint32_t next() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    // Convert to [-1, 1] range for audio
    float nextFloat() noexcept {
        return static_cast<float>(next()) / 2147483648.0f - 1.0f;
    }
};
```

**Alternatives Considered**:
- LCG from LFO: Shorter period, slightly more predictable
- mt19937: Overkill for audio, much slower
- PCG: Better statistical properties but more complex

---

### RQ3: Tape Hiss Spectral Shaping

**Question**: What spectral shape characterizes authentic tape hiss?

**Research Findings**:
- Real tape hiss has a slight high-frequency emphasis (not flat like white noise)
- Typically characterized as "pink-ish" but brighter
- Spectral shape varies with tape speed and formulation
- Key characteristic: +3dB shelf above 4-8kHz

**Decision**: Use a high-shelf biquad filter on pink noise

**Rationale**:
- Pink noise base provides natural low-end rolloff
- High-shelf adds authentic tape character
- Computationally cheap (single biquad)
- Easy to tune for different "tape types"

**Implementation**:
```cpp
// Tape hiss = pink noise + high shelf boost
// Shelf frequency: ~5kHz, gain: +3dB
Biquad hissShaper;
hissShaper.setHighShelf(sampleRate, 5000.0f, 0.707f, 3.0f);
```

---

### RQ4: Vinyl Crackle Impulse Generation

**Question**: How should vinyl crackle impulses be generated?

**Research Findings**:
- Real vinyl crackle has two components:
  1. Continuous surface noise (low-level filtered noise)
  2. Impulsive clicks/pops (random timing, varied amplitude)
- Click amplitudes follow roughly exponential distribution (many small, few loud)
- Click density varies with "record condition"
- Clicks have characteristic fast attack, medium decay shape

**Decision**: Poisson-distributed impulse timing + exponential amplitude distribution

**Rationale**:
- Poisson process naturally models random, independent events
- Exponential amplitude creates realistic "many small clicks, few big pops"
- Simple to implement with uniform random source

**Implementation**:
```cpp
// Crackle generation per sample:
// 1. Roll for click occurrence (Poisson approximation)
float clickProb = density / sampleRate;  // density in Hz
if (rng.nextFloat() * 0.5f + 0.5f < clickProb) {
    // 2. Generate click with exponential amplitude
    float amp = -std::log(rng.nextFloat() * 0.5f + 0.5f) * 0.3f;
    amp = std::min(amp, 1.0f);  // Clamp max amplitude
    // 3. Apply click shape (fast attack, medium decay)
    triggerClick(amp);
}
```

---

### RQ5: Signal-Dependent Modulation Strategy

**Question**: How should tape hiss and asperity noise respond to signal level?

**Research Findings**:
- Real tape hiss increases with recording level (bias interaction)
- Modulation should be smooth (envelope-followed), not instantaneous
- Typical relationship is roughly linear in dB (louder signal = proportionally more hiss)
- Floor level prevents complete silence (tape always has some noise)

**Decision**: Use EnvelopeFollower with dB-domain modulation

**Rationale**:
- EnvelopeFollower already exists (Layer 2, reuse per Principle XIV)
- dB-domain modulation gives perceptually linear response
- Attack/release times can simulate different tape "responsiveness"

**Implementation**:
```cpp
// Signal-dependent modulation
float signalDb = gainToDb(envelopeFollower.process(inputSample));
float modulation = std::max(floorLevel, signalDb / 40.0f + 1.0f);  // Normalize
modulation = std::clamp(modulation, 0.0f, 1.0f);
float hissSample = pinkNoise * baseLevel * modulation;
```

---

## Summary of Decisions

| Question | Decision | Key Reason |
|----------|----------|------------|
| Pink noise algorithm | Paul Kellet's filter | Best accuracy/cost ratio |
| Random number generator | xorshift32 | Fast, sufficient period, no patterns |
| Tape hiss spectrum | Pink + high shelf | Authentic tape character |
| Vinyl crackle model | Poisson + exponential | Realistic random distribution |
| Signal modulation | EnvelopeFollower + dB | Reuses existing component, perceptually linear |

## References

- Paul Kellet's pink noise filter: https://www.firstpr.com.au/dsp/pink-noise/
- Voss-McCartney algorithm: Gardner, "Statistical Methods" (1978)
- xorshift PRNG: Marsaglia, "Xorshift RNGs" (2003)
- Tape noise characteristics: Bohn, "Audio Specifications" (1988)
