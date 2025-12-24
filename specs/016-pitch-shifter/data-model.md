# Data Model: Pitch Shift Processor

**Feature**: 016-pitch-shifter
**Date**: 2025-12-24
**Related**: [spec.md](spec.md) | [plan.md](plan.md)

## Entity Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                      PitchShiftProcessor                            │
│  (Layer 2: Public facade managing quality modes and parameters)     │
├─────────────────────────────────────────────────────────────────────┤
│ - mode_: PitchMode                                                  │
│ - semitones_: float [-24, +24]                                      │
│ - cents_: float [-100, +100]                                        │
│ - formantPreserve_: bool                                            │
│ - sampleRate_: double                                               │
│ - prepared_: bool                                                   │
│ - semitoneSmoother_: OnePoleSmoother                                │
│ - centsSmoother_: OnePoleSmoother                                   │
│ - simpleShifter_: SimplePitchShifter                                │
│ - granularShifter_: GranularPitchShifter                            │
│ - phaseVocoderShifter_: PhaseVocoderPitchShifter                    │
├─────────────────────────────────────────────────────────────────────┤
│ + prepare(sampleRate, maxBlockSize)                                 │
│ + process(input, output, numSamples)                                │
│ + reset()                                                           │
│ + setMode(mode) / getMode()                                         │
│ + setSemitones(semitones) / getSemitones()                          │
│ + setCents(cents) / getCents()                                      │
│ + setFormantPreserve(enable) / getFormantPreserve()                 │
│ + getLatencySamples()                                               │
│ + getPitchRatio()                                                   │
└─────────────────────────────────────────────────────────────────────┘
          │
          │ composes (internal)
          ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Internal Classes                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌───────────────────────┐  ┌───────────────────────┐              │
│  │  SimplePitchShifter   │  │ GranularPitchShifter  │              │
│  │  (Delay modulation)   │  │ (OLA grains)          │              │
│  ├───────────────────────┤  ├───────────────────────┤              │
│  │ - delayLine_          │  │ - grainBuffer_        │              │
│  │ - readPos1_, readPos2_│  │ - grains_[4]          │              │
│  │ - crossfadePhase_     │  │ - writePos_           │              │
│  │ - activeReader_       │  │ - readPos_            │              │
│  └───────────────────────┘  │ - hopsUntilNext_      │              │
│                             │ - formantPreserver_?  │              │
│                             └───────────────────────┘              │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                   PhaseVocoderPitchShifter                    │ │
│  │                   (STFT with phase locking)                   │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │ - stft_: STFT                                                 │ │
│  │ - analysisBuffer_: SpectralBuffer                             │ │
│  │ - synthesisBuffer_: SpectralBuffer                            │ │
│  │ - phaseAccumulator_: std::array<float, FFT_SIZE/2+1>          │ │
│  │ - lastPhase_: std::array<float, FFT_SIZE/2+1>                 │ │
│  │ - formantPreserver_?: FormantPreserver                        │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                     FormantPreserver                          │ │
│  │                 (Cepstral envelope estimation)                │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │ - fft_: FFT                                                   │ │
│  │ - cepstrum_: std::array<float, FFT_SIZE>                      │ │
│  │ - envelope_: std::array<float, FFT_SIZE/2+1>                  │ │
│  │ - quefrencyCutoff_: size_t                                    │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Enumerations

### PitchMode

Quality mode selection for pitch shifting algorithm.

| Value | Name | Description |
|-------|------|-------------|
| 0 | Simple | Delay-line modulation, zero latency, audible artifacts |
| 1 | Granular | OLA grains, ~46ms latency, good quality |
| 2 | PhaseVocoder | STFT-based, ~116ms latency, excellent quality |

## Entity Definitions

### PitchShiftProcessor

The main public class that users interact with. Acts as a facade over the three internal pitch shifting implementations.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| mode_ | PitchMode | enum | Current quality mode |
| semitones_ | float | [-24, +24] | Pitch shift in semitones |
| cents_ | float | [-100, +100] | Fine pitch adjustment |
| formantPreserve_ | bool | true/false | Enable formant preservation |
| sampleRate_ | double | [44100, 192000] | Current sample rate |
| prepared_ | bool | true/false | Whether prepare() was called |

**Derived Values:**
- `pitchRatio = 2^((semitones + cents/100) / 12)`
- `totalSemitones = semitones + cents/100`

### SimplePitchShifter (Internal)

Zero-latency pitch shifter using dual read pointer crossfade on a delay line.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| delayLine_ | DelayLine | 50ms worth | Circular buffer for delay |
| readPos1_ | float | - | First read pointer position (fractional) |
| readPos2_ | float | - | Second read pointer position (fractional) |
| crossfadePhase_ | float | [0, 1] | Current position in crossfade cycle |
| activeReader_ | int | 0 or 1 | Which reader is currently dominant |
| windowSize_ | size_t | samples | Crossfade window size in samples |

**Algorithm Constants:**
- Window size: 50ms (2205 samples at 44.1kHz)
- Crossfade shape: Half-sine (sqrt energy preserving)
- Latency: 0 samples

### GranularPitchShifter (Internal)

OLA-based pitch shifter with overlapping grain windows.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| grainBuffer_ | float[] | 60ms worth | Input grain buffer |
| grains_ | GrainState[4] | 4 grains | Overlapping grain instances |
| writePos_ | size_t | - | Write position in grain buffer |
| readPos_ | float | - | Read position (fractional, for pitch) |
| analysisHop_ | size_t | samples | Input hop size |
| hopsUntilNext_ | size_t | samples | Countdown to next grain emission |
| hannWindow_ | float[] | grain size | Pre-computed Hann window |
| formantPreserver_ | FormantPreserver* | - | Optional formant preservation |

**Grain State:**

| Field | Type | Description |
|-------|------|-------------|
| active | bool | Is this grain currently playing |
| readPos | float | Current read position in grain |
| amplitude | float | Current window amplitude |
| startOffset | size_t | Where this grain started in buffer |

**Algorithm Constants:**
- Grain size: 40ms (1764 samples at 44.1kHz)
- Overlap: 75% (4 simultaneous grains)
- Window: Hann
- Latency: ~grain_size samples

### PhaseVocoderPitchShifter (Internal)

STFT-based pitch shifter with scaled phase locking for highest quality.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| stft_ | STFT | - | Short-time Fourier transform |
| analysisMags_ | float[] | FFT_SIZE/2+1 | Analysis magnitudes |
| analysisPhases_ | float[] | FFT_SIZE/2+1 | Analysis phases |
| synthesisMags_ | float[] | FFT_SIZE/2+1 | Synthesis magnitudes |
| synthesisPhases_ | float[] | FFT_SIZE/2+1 | Synthesis phases |
| phaseAccum_ | float[] | FFT_SIZE/2+1 | Phase accumulator per bin |
| lastPhase_ | float[] | FFT_SIZE/2+1 | Previous frame phases |
| trueFreq_ | float[] | FFT_SIZE/2+1 | Estimated true frequencies |
| peakIndices_ | size_t[] | dynamic | Spectral peak locations |
| formantPreserver_ | FormantPreserver* | - | Optional formant preservation |

**Algorithm Constants:**
- FFT size: 4096 samples
- Hop size: 1024 samples (75% overlap)
- Window: Hann (analysis and synthesis)
- Latency: FFT_SIZE + HOP_SIZE = 5120 samples (~116ms at 44.1kHz)

### FormantPreserver (Internal)

Cepstral-domain spectral envelope estimator for formant preservation.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| fft_ | FFT | - | FFT for cepstrum computation |
| cepstrum_ | float[] | FFT_SIZE | Cepstral coefficients |
| envelope_ | float[] | FFT_SIZE/2+1 | Estimated spectral envelope |
| excitation_ | float[] | FFT_SIZE/2+1 | Excitation (spectrum / envelope) |
| quefrencyCutoff_ | size_t | samples | Liftering cutoff (~50 samples) |
| lifterWindow_ | float[] | FFT_SIZE | Low-pass lifter in cepstral domain |

**Algorithm Constants:**
- Quefrency cutoff formula: `quefrencyCutoff = sampleRate / minFundamentalFreq`
  - For minFundamentalFreq = 80Hz (male bass voice): cutoff = 551 samples at 44.1kHz
  - Default implementation: cutoff = 50 samples (~1.1ms) works for speech/vocals (fundamentals > 900Hz)
  - Adjustable based on expected content type
- Lifter window: Rectangular low-pass in cepstral domain (keep coefficients 0 to cutoff, zero the rest)

## Memory Layout

### Pre-allocation Requirements

All memory must be allocated in `prepare()`, never in `process()`.

| Component | Allocation Size (44.1kHz) | Allocation Size (192kHz) |
|-----------|---------------------------|--------------------------|
| SimplePitchShifter.delayLine | 2,205 floats | 9,600 floats |
| GranularPitchShifter.grainBuffer | 2,646 floats | 11,520 floats |
| GranularPitchShifter.hannWindow | 1,764 floats | 7,680 floats |
| PhaseVocoderPitchShifter (all) | ~32,768 floats | ~32,768 floats |
| FormantPreserver (all) | ~16,384 floats | ~16,384 floats |

**Total worst-case (PhaseVocoder + Formant at 192kHz):** ~60KB per instance

## Relationships

```
PitchShiftProcessor
    │
    ├── owns ──► SimplePitchShifter
    │               └── uses ──► DelayLine (Layer 1)
    │
    ├── owns ──► GranularPitchShifter
    │               ├── uses ──► WindowFunctions::hann() (Layer 0)
    │               └── owns? ──► FormantPreserver
    │
    ├── owns ──► PhaseVocoderPitchShifter
    │               ├── uses ──► STFT (Layer 1)
    │               ├── uses ──► SpectralBuffer (Layer 1)
    │               └── owns? ──► FormantPreserver
    │
    └── owns ──► OnePoleSmoother (×2 for semitones/cents)
```

## State Transitions

```
[Constructed] ──prepare()──► [Prepared] ──process()──► [Processing]
      │                           │                          │
      │                           │                          │
      └───────────reset()─────────┴──────────reset()─────────┘
                                  │
                                  ▼
                            [Prepared]
```

## Thread Safety

- All parameter setters are atomic (single float writes)
- No locks in `process()` path
- Mode switching is safe between process calls
- Formant toggle may cause brief transient during crossfade

## Validation Rules

| Parameter | Validation | Action on Invalid |
|-----------|-----------|-------------------|
| semitones | [-24, +24] | Clamp to range |
| cents | [-100, +100] | Clamp to range |
| mode | enum PitchMode | No-op if invalid |
| formantPreserve | bool | N/A |
| sampleRate | [44100, 192000] | Clamp to range |
| blockSize | [1, maxBlockSize] | Process available |
