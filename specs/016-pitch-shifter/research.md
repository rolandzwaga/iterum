# Research: Pitch Shift Processor

**Feature**: 016-pitch-shifter
**Date**: 2025-12-24
**Status**: Complete

## Executive Summary

This document compiles research on pitch shifting algorithms for implementing a production-quality Layer 2 DSP processor. Three main approaches are analyzed: delay-line modulation (Simple mode), granular synthesis (Granular mode), and phase vocoder (PhaseVocoder mode), plus formant preservation techniques.

---

## 1. Delay-Line Based Pitch Shifting (Simple Mode)

### 1.1 Core Principle

The delay-line approach exploits the Doppler effect: when a delay line's read pointer moves relative to the write pointer, the output pitch changes proportionally.

**Key insight**: "Vibrato and pitch shifting can be accomplished using a modulated delay line. This works because a time-varying delay line induces a simulated Doppler shift on the signal within it." ([DSPRelated - Time-Varying Delay Effects](https://www.dsprelated.com/freebooks/pasp/Time_Varying_Delay_Effects.html))

### 1.2 Two-Pointer Crossfade Algorithm

The standard approach uses two alternating read pointers with crossfading:

1. Two read pointers move through a circular buffer at non-unity speed
2. When a pointer reaches the buffer boundary, it resets
3. Crossfading between pointers masks discontinuities at reset points

**Crossfade window options**:
- **Linear ramps**: Simple but can cause slight amplitude dips
- **Sine windows**: "A good choice of envelope is one half cycle of a sinusoid. If we assume on average that the two delayed copies are uncorrelated, the signal power from the two delay lines, after enveloping, will add to a constant." ([katjaas.nl](https://www.katjaas.nl/pitchshift/pitchshift.html))
- **Hann windows**: Common choice, sum to constant with 50% overlap

### 1.3 Recommended Parameters

| Parameter | Recommended Value | Notes |
|-----------|-------------------|-------|
| Max delay | 50-100ms | Longer = more flexibility, more latency |
| Grain/window size | 30-100ms | Too small = audible modulation; too large = echo effect |
| Crossfade time | 5-20ms | Must be short enough to avoid comb filtering |

### 1.4 Latency

**Zero latency is achievable** with this method because input samples can be processed immediately - only the internal delay buffer introduces any timing offset, which can be managed to be zero at the output.

### 1.5 Known Artifacts

- **Rhythmic artifacts**: Audible when crossfade rate matches audio periodicity
- **Comb filtering**: If crossfade is too long, the two delayed copies create interference
- **Lost transients**: "With downward pitch shifting, short attacks can easily get lost" ([katjaas.nl](https://www.katjaas.nl/pitchshift/pitchshift.html))

### 1.6 Reference Implementation

The [katjaas.nl low-latency pitch shifter](https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html) achieves "an average latency of only a few milliseconds" by integrating pitch detection to optimize splice points.

---

## 2. Granular Synthesis (Granular Mode)

### 2.1 Core Principle

Audio is decomposed into small overlapping "grains" (typically 10-100ms). Each grain is played at a modified speed to change pitch, while the grain emission rate is adjusted to maintain duration.

**Formula**: If grains are extracted with a pitch shift factor P, stretching the grain window to P×N samples and reconstructing every N samples produces pitch shift without duration change.

### 2.2 Grain Parameters

| Parameter | Range | Effect |
|-----------|-------|--------|
| Grain size | 10-100ms | Larger = better frequency resolution, more latency |
| Overlap | 50-87.5% | Higher = smoother output, more CPU |
| Window function | Hann, raised cosine | Shapes crossfade envelope |

**Critical findings**:
- "Grains are typically between 10 and 100 msec in length. In order to be heard as a pitched event, the minimum length is 13 msec for high frequencies and 45 msec for low frequencies." ([FLOSS Manual](http://floss.booktype.pro/csound/g-granular-synthesis/))
- "Lengths greater than 50 msec create the impression of separate sound events."

### 2.3 Window Functions

The **Hann window** is most common: `w(n) = 0.5 * (1 - cos(2π*n/N))`

Alternative: **Raised cosine** - some find it sounds better, others report more clicks.

**COLA constraint**: Windows must sum to constant (or power-constant) when overlapped to avoid amplitude modulation artifacts.

### 2.4 Overlap-Add (OLA) Process

1. Segment input into overlapping frames (analysis hop Ha)
2. Apply window function to each frame
3. Resample each frame by pitch factor (or play at different rate)
4. Reconstruct with synthesis hop Hs = Ha × stretch_factor
5. Overlap-add windowed frames

**Time-stretch factor**: α = Hs / Ha

For pitch shifting without duration change: stretch by factor P, then resample by 1/P.

### 2.5 Latency

Minimum latency ≈ grain_size + analysis_hop

For 50ms grains with 75% overlap: ~62ms latency

**Target**: < 2048 samples (~46ms at 44.1kHz) per spec

### 2.6 Known Artifacts

- **Amplitude modulation**: If COLA constraint not met
- **Phasiness**: Phase discontinuities between grains
- **Transient smearing**: Attack transients spread across multiple grains

---

## 3. Phase Vocoder (PhaseVocoder Mode)

### 3.1 Core Principle

The phase vocoder uses Short-Time Fourier Transform (STFT) to analyze audio in the frequency domain, scales the frequency bins, then resynthesizes using inverse STFT.

"At the heart of the phase vocoder is the short-time Fourier transform (STFT), typically coded using fast Fourier transforms." ([Wikipedia](https://en.wikipedia.org/wiki/Phase_vocoder))

### 3.2 STFT Analysis

**Bin frequency**: `f = m × (sample_rate / FFT_size)` where m is bin index

**Recommended FFT sizes** ([Bernsee](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/)):

| FFT Size | Sample Rate | Best For | Latency |
|----------|-------------|----------|---------|
| 1024 | 44.1kHz | Speech | 23ms |
| 2048 | 44.1kHz | Music | 46ms |
| 4096 | 44.1kHz | Tonal content | 93ms |

**Overlap**: "Minimum 4x overlap (75% overlap between frames) is critical" - insufficient overlap causes phase wrapping artifacts.

### 3.3 Phase Calculation

The algorithm derives true partial frequencies from phase differences:

1. Convert FFT to polar form (magnitude, phase)
2. Calculate phase difference between adjacent frames
3. Subtract expected phase advance: `Δφ_expected = 2π × bin_freq × hop_size / sample_rate`
4. Remaining phase deviation reveals frequency offset from bin center

**Key formula** for instantaneous frequency:
```
ω_true = bin_freq + (Δφ_measured - Δφ_expected) × sample_rate / (2π × hop_size)
```

### 3.4 Pitch Shifting Process

1. Analyze: Compute STFT of input
2. Extract: Calculate true frequencies and magnitudes for each partial
3. Scale: Multiply frequencies by pitch ratio
4. Resynthesize: Place scaled magnitudes at new frequency locations
5. ISTFT: Apply inverse transform with overlap-add

### 3.5 Phase Coherence

Two types of coherence must be maintained:

**Horizontal coherence** (inter-frame): Phase relationship between same bin across frames
- Maintained by propagating phase based on instantaneous frequency

**Vertical coherence** (intra-frame): Phase relationship between adjacent bins
- Critical for quality; solved by Laroche & Dolson (1999) with **phase locking**

### 3.6 Phase Locking

"The problem of vertical coherence remained a major issue until 1999 when Laroche and Dolson proposed a means to preserve phase consistency across spectral bins." ([Wikipedia](https://en.wikipedia.org/wiki/Phase_vocoder))

**Identity phase locking**: Maintains phase relationships from original signal
**Scaled phase locking**: Adjusts phase relationships proportionally to pitch shift

**Region of influence**: Each spectral peak controls phase of surrounding bins

### 3.7 Transient Handling

Transients cause "smearing" artifacts because phase vocoder spreads energy temporally.

**Solutions**:
- Phase reset at transient onsets
- Disable time-scaling during transients
- Harmonic-percussive separation (process separately)
- Multi-resolution peak picking

"The most common approach for transient smearing compensation is performing a phase reset or phase locking at transients." ([Röbel, ICMC 2003](https://hal.science/hal-01161125/document))

### 3.8 Latency

Minimum latency = FFT_size + hop_size

For 4096 FFT with 75% overlap (1024 hop) at 44.1kHz: ~116ms

**Target**: < 8192 samples (~186ms at 44.1kHz) per spec

### 3.9 Reference Implementations

- **smbPitchShift** by Stephan Bernsee ([download](http://blogs.zynaptiq.com/bernsee/download/))
- **stftPitchShift** by jurihock ([GitHub](https://github.com/jurihock/stftPitchShift))
- **Signalsmith Stretch** ([GitHub](https://github.com/Signalsmith-Audio/signalsmith-stretch))

---

## 4. Formant Preservation

### 4.1 Why It Matters

"Formant frequencies aren't dependent on the fundamental frequency (pitch) of the note, they depend on the vocal tract of the singer... That's why you don't want to move the formants when you pitch shift, because it's the note that's changing, not the vocal tract." ([Bernsee blog](http://blogs.zynaptiq.com/bernsee/formants-pitch-shifting/))

Without formant preservation: "Mickey Mouse" effect (pitch up) or "monster" effect (pitch down).

### 4.2 Spectral Envelope Estimation Methods

#### Cepstral Method (Recommended)

1. Compute DFT magnitude spectrum
2. Take log of magnitude
3. Apply inverse FFT → cepstrum
4. Low-pass filter (lifter) the cepstrum
5. FFT back → smoothed spectral envelope

**Quefrency cutoff**: Should be smaller than fundamental period. Start with 1-2ms and adjust.

"The vocal formants are represented by the spectral envelope, which is given by the smoothed DFT magnitude vector. In this implementation, the smoothing of the DFT magnitude vector takes place in the cepstral domain by low-pass liftering." ([stftPitchShift docs](https://pypi.org/project/stftpitchshift/))

#### LPC Method

- Linear Predictive Coding estimates vocal tract filter
- More computationally expensive
- Better for speech, may introduce artifacts on music

#### Direct Spectral Smoothing

- Low-pass filter the magnitude spectrum directly
- Simpler but less accurate
- Can leave "trace amount of original envelope"

### 4.3 Formant Preservation Process

1. **Estimate** spectral envelope from input
2. **Remove** envelope from spectrum (divide or subtract in log domain)
3. **Pitch shift** the residual/excitation
4. **Reapply** original envelope to shifted spectrum

### 4.4 Limitations

- Only meaningful for Granular and PhaseVocoder modes
- Simple mode (delay-line) cannot preserve formants
- At extreme shifts (>1 octave), formant preservation may sound unnatural

---

## 5. Shimmer/Feedback Integration

### 5.1 How Shimmer Works

"The basic foundation of the Brian Eno / Daniel Lanois shimmer sound is fairly simple: Create a feedback loop, incorporating a pitch shifter set to +1 octave, and a reverb with a fairly long decay time." ([Valhalla DSP](https://valhalladsp.com/2010/05/13/feedback-anti-feedback-and-complexity-in-time-varying-systems/))

### 5.2 Feedback Considerations

**Artifact accumulation**: "The goal was to generate similar artifacts to what a 'de-glitched' pitch shifter would produce in a feedback loop with a reverberator."

**Self-oscillation avoidance**: "Pitch shifting, in and of itself, is a useful way of avoiding oscillation, as it pushes the feedback energy into regions that are above or below the original energy in frequency."

### 5.3 Design Requirements for Feedback Use

1. **DC stability**: Must not accumulate DC offset over iterations
2. **Energy preservation**: Should not amplify or attenuate significantly
3. **Artifact consistency**: Artifacts should not compound exponentially
4. **Deterministic behavior**: Same input should produce same output (no random elements that could diverge)

---

## 6. Implementation Recommendations

### 6.1 Simple Mode (Delay-Line)

```
Algorithm: Dual-pointer crossfade
- Two read pointers with sinusoidal crossfade
- Window size: 50ms (2205 samples at 44.1kHz)
- Crossfade shape: Half-sine (sqrt energy)
- Latency: 0 samples
- CPU: Very low
```

### 6.2 Granular Mode

```
Algorithm: OLA with Hann windows
- Grain size: 40-60ms (adaptive to content)
- Overlap: 75% (4x)
- Window: Hann
- Latency: ~grain_size (1764-2646 samples)
- CPU: Low-moderate
```

### 6.3 PhaseVocoder Mode

```
Algorithm: STFT with phase locking
- FFT size: 4096 samples
- Overlap: 75% (hop = 1024)
- Window: Hann (analysis and synthesis)
- Phase locking: Scaled phase locking with peak detection
- Transient handling: Phase reset at detected onsets
- Latency: FFT_size + hop = 5120 samples (~116ms)
- CPU: Moderate-high
```

### 6.4 Formant Preservation

```
Algorithm: Cepstral envelope estimation
- Quefrency cutoff: 1-2ms (adjustable)
- Apply in: Granular and PhaseVocoder modes only
- Process: Remove envelope → shift → reapply envelope
```

---

## 7. Sources

### Academic Papers
- Laroche & Dolson (1999): "New phase-vocoder techniques for pitch-shifting" - [Columbia](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf)
- Röbel (2003): "Transient detection and preservation in the phase vocoder" - [HAL](https://hal.science/hal-01161125/document)
- Průša & Holighaus (2022): "Phase Vocoder Done Right" - [arXiv](https://arxiv.org/pdf/2202.07382)

### Tutorials & Documentation
- Bernsee: "Pitch Shifting Using the Fourier Transform" - [zynaptiq](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/)
- katjaas.nl: "Pitch Shifting" - [katjaas](https://www.katjaas.nl/pitchshift/pitchshift.html)
- katjaas.nl: "Low Latency Pitch Shifting" - [katjaas](https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html)
- MathWorks: "Delay-Based Pitch Shifter" - [MATLAB](https://www.mathworks.com/help/audio/ug/delay-based-pitch-shifter.html)
- DSPRelated: "Overlap-Add STFT Processing" - [DSPRelated](https://www.dsprelated.com/freebooks/sasp/Overlap_Add_OLA_STFT_Processing.html)

### Reference Implementations
- smbPitchShift (C++): [dspdimension](http://downloads.dspdimension.com/smbPitchShift.cpp)
- stftPitchShift (C++/Python): [GitHub](https://github.com/jurihock/stftPitchShift)
- Signalsmith Stretch (C++): [GitHub](https://github.com/Signalsmith-Audio/signalsmith-stretch)
- PyTSMod (Python): [GitHub](https://github.com/KAIST-MACLab/PyTSMod)

### Industry
- Valhalla DSP: "Feedback, anti-feedback, and complexity" - [Valhalla](https://valhalladsp.com/2010/05/13/feedback-anti-feedback-and-complexity-in-time-varying-systems/)
- Perfect Circuit: "Pitch Shifted Delays" - [Perfect Circuit](https://www.perfectcircuit.com/signal/pitch-shifted-delays)

---

## 8. Key Takeaways for Implementation

1. **Simple mode** can achieve true zero latency but with audible artifacts (acceptable for monitoring/feedback use)

2. **Granular mode** offers the best balance of quality/latency for general use (~50ms latency, good quality)

3. **PhaseVocoder mode** provides highest quality but significant latency (~116ms) - best for non-real-time or quality-critical use

4. **Phase locking is essential** for quality phase vocoder output - without it, "phasiness" artifacts are severe

5. **Transient handling** is the key differentiator between amateur and professional implementations

6. **Formant preservation** requires spectral envelope estimation - cepstral method is most practical

7. **Feedback stability** requires careful gain staging and DC blocking
