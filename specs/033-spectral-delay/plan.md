# Implementation Plan: Spectral Delay

**Branch**: `033-spectral-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/033-spectral-delay/spec.md`

## Summary

Layer 4 user feature that applies delay to individual frequency bands using STFT analysis/resynthesis. Creates ethereal, frequency-dependent echo effects where different frequency bands can have different delay times, enabling unique spectral smearing and freeze capabilities.

**Technical Approach**:
- Use existing STFT (Layer 1) for analysis, OverlapAdd for synthesis
- Create per-bin delay lines using existing DelayLine primitive
- Implement spectral freeze by holding SpectralBuffer contents
- Add spread control for automatic delay time distribution across bins
- Follow ShimmerDelay architectural pattern (Layer 4 composing Layer 1-3)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- STFT, OverlapAdd (Layer 1) - spectral analysis/synthesis
- SpectralBuffer (Layer 1) - spectrum storage
- DelayLine (Layer 1) - per-bin delay lines
- OnePoleSmoother (Layer 1) - parameter smoothing
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (per Constitution Principle XII)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single (VST3 plugin DSP component)
**Performance Goals**: < 3% CPU at 44.1kHz stereo with 2048 FFT size (SC-005)
**Constraints**: Latency = FFT size / 2 samples; real-time safe processing
**Scale/Scope**: Up to 4096 FFT size (2049 bins), 2000ms max delay per bin

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Additional Constitution Checks:**
- [x] Principle II: All processing is noexcept, allocations in prepare()
- [x] Principle IX: Layer 4 composes only from Layer 0-3
- [x] Principle X: FFT uses power-of-2, COLA windows maintained
- [x] Principle XI: CPU budget < 3% (validated by benchmark)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SpectralDelay, SpreadDirection (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralDelay | `grep -r "class SpectralDelay" src/` | No | Create New |
| SpreadDirection | `grep -r "SpreadDirection" src/` | No | Create New |
| PerBinDelayLine | N/A (array of DelayLine) | N/A | Use existing DelayLine[] |

**Utility Functions to be created**: None - all utilities exist in Layer 0/1

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" src/` | Yes | dsp/core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" src/` | Yes | dsp/core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| STFT | dsp/primitives/stft.h | 1 | Input analysis (windowing + FFT) |
| OverlapAdd | dsp/primitives/stft.h | 1 | Output synthesis (IFFT + overlap) |
| SpectralBuffer | dsp/primitives/spectral_buffer.h | 1 | Per-frame spectrum storage |
| DelayLine | dsp/primitives/delay_line.h | 1 | Per-bin delay (array of instances) |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| dbToGain | dsp/core/db_utils.h | 0 | Output gain conversion |
| BlockContext | dsp/core/block_context.h | 0 | Processing context |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No spectral delay utilities
- [x] `src/dsp/core/` - Layer 0 core utilities checked
- [x] `src/dsp/primitives/` - No SpectralDelay class
- [x] `src/dsp/features/` - No SpectralDelay class
- [x] `ARCHITECTURE.md` - Component inventory reviewed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: SpectralDelay and SpreadDirection are unique types not found in codebase. All other functionality uses existing primitives (STFT, DelayLine, SpectralBuffer). No name collisions detected.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| STFT | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| STFT | pushSamples | `void pushSamples(const float* input, size_t numSamples) noexcept` | Yes |
| STFT | canAnalyze | `[[nodiscard]] bool canAnalyze() const noexcept` | Yes |
| STFT | analyze | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| STFT | latency | `[[nodiscard]] size_t latency() const noexcept` | Yes |
| OverlapAdd | prepare | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| OverlapAdd | synthesize | `void synthesize(const SpectralBuffer& input) noexcept` | Yes |
| OverlapAdd | samplesAvailable | `[[nodiscard]] size_t samplesAvailable() const noexcept` | Yes |
| OverlapAdd | pullSamples | `void pullSamples(float* output, size_t numSamples) noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| SpectralBuffer | getMagnitude | `[[nodiscard]] float getMagnitude(size_t bin) const noexcept` | Yes |
| SpectralBuffer | getPhase | `[[nodiscard]] float getPhase(size_t bin) const noexcept` | Yes |
| SpectralBuffer | setMagnitude | `void setMagnitude(size_t bin, float magnitude) noexcept` | Yes |
| SpectralBuffer | setPhase | `void setPhase(size_t bin, float phase) noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| SpectralBuffer | data | `[[nodiscard]] Complex* data() noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `src/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `src/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `src/dsp/primitives/delay_line.h` - DelayLine class
- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `src/dsp/core/db_utils.h` - dB conversion utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| STFT | Needs fftSize samples before canAnalyze() | Accumulate across multiple blocks |
| SpectralBuffer | numBins = fftSize/2 + 1, not fftSize | Use `numBins()` for iteration |
| OverlapAdd | Must synthesize() before pullSamples() | Call in correct order |
| DelayLine | prepare() requires sampleRate, not FFT rate | Use audio sample rate |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | Spectral processing is domain-specific | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateBinDelayTime | Per-feature spread logic, not reusable |
| applyDiffusion | Spectral-specific, not general utility |

**Decision**: No Layer 0 extraction needed. Spectral delay processing is unique to this feature.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- Granular Delay (4.8) - Different approach (time-domain grains)
- Future vocoder effects - May share spectral processing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SpectralDelay | LOW | Unique effect | Keep local |
| Freeze logic | MEDIUM | Future freeze variants | Keep local, extract if needed |
| Per-bin delay pattern | LOW | Unique to spectral | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First spectral Layer 4 feature |
| Keep per-bin logic local | Unique to frequency-domain processing |

### Review Trigger

After implementing **Granular Delay (4.8)**, review this section:
- [ ] Does Granular need spectral analysis? (Likely not - time domain)
- [ ] Any shared freeze logic? (Check after implementation)

## Project Structure

### Documentation (this feature)

```text
specs/033-spectral-delay/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output (TDD scenarios)
├── contracts/           # N/A (internal DSP, no external API)
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
src/dsp/
├── features/
│   └── spectral_delay.h      # Layer 4 SpectralDelay class
├── primitives/
│   ├── stft.h                # STFT + OverlapAdd (existing)
│   ├── spectral_buffer.h     # SpectralBuffer (existing)
│   └── delay_line.h          # DelayLine (existing)
└── core/
    └── db_utils.h            # dB utilities (existing)

tests/unit/
├── features/
│   └── spectral_delay_test.cpp   # Unit tests
└── primitives/
    └── stft_test.cpp             # Existing STFT tests
```

**Structure Decision**: Single header file at Layer 4 (spectral_delay.h) composing existing primitives. No new Layer 1-3 components needed.

## Complexity Tracking

No constitution violations requiring justification. All principles satisfied:
- Layer 4 composing from Layer 1 primitives (IX)
- Real-time safe with prepare() allocation (II)
- FFT power-of-2 with COLA windows (X)
- CPU budget manageable with efficient per-bin processing (XI)
