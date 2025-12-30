# Changelog

All notable changes to Iterum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2025-12-30

### Added

- **Click-Free Mode Switching** (spec 041)
  - 50ms equal-power crossfade between all 11 delay modes eliminates clicks/pops
  - Smooth transitions when switching modes mid-playback or via automation
  - All 110 mode combinations (11×10) supported seamlessly
  - Real-time safe implementation with pre-allocated buffers
  - Dry signal remains unaffected during crossfade; only wet signal transitions

- **CrossfadingDelayLine Equal-Power Upgrade**
  - Upgraded from linear to equal-power crossfade (sine/cosine curves)
  - Maintains constant perceived loudness during delay time changes
  - Eliminates the -3dB dip at midpoint that occurred with linear crossfading
  - Shared `crossfade_utils.h` provides `equalPowerGains()` for ODR-safe reuse

### Changed

- **Simplified Output Architecture**
  - Removed redundant per-mode output parameters from all 11 delay modes
  - Global output control (kGainId) is now the single source of output gain adjustment
  - Cleaner UI with one OUTPUT control instead of per-mode duplicates
  - Reduced parameter count by 11 (1376 lines of code removed)

### Removed

- Per-mode output parameters:
  - `kGranularOutputGainId`, `kSpectralOutputGainId`, `kShimmerOutputGainId`
  - `kTapeOutputLevelId`, `kBBDOutputLevelId`, `kDigitalOutputLevelId`
  - `kPingPongOutputLevelId`, `kReverseOutputGainId`, `kMultiTapOutputLevelId`
  - `kFreezeOutputGainId`, `kDuckingOutputGainId`

---

## [0.2.1] - 2025-12-30

### Changed

- **GUI Layout Redesign with Grouped Controls** (spec 040)
  - All 11 mode panels reorganized with visually distinct, labeled control groups
  - Controls grouped by functionality: TIME & MIX, CHARACTER, MODULATION, OUTPUT, etc.
  - Groups use CViewContainer with section background color for visual separation
  - Group headers use section-font with accent color for consistent styling
  - Improved discoverability: related controls are now adjacent and clearly labeled
  - Freeze, Ducking, and MultiTap modes place their primary function group first

### Improved

- **Build System**
  - Windows resource file (win32resource.rc) now generated from version.json template
  - Single source of truth for version numbers and plugin metadata
  - Plugin properties in Windows Explorer now show correct version info

### Removed

- **Legacy Files**
  - Removed unused editor_minimal.uidesc (legacy debug file)

---

## [0.2.0] - 2025-12-30

### Added

- **Audio Unit v2 Support (macOS)** (spec 039)
  - Iterum now available as an AU component for Logic Pro, GarageBand, and other AU-only hosts
  - Universal Binary (arm64 + x86_64) for native performance on both Intel and Apple Silicon Macs
  - AU validation (`auval`) runs automatically in CI to ensure compliance
  - macOS installer now includes both VST3 and AU formats
  - AU codes: `aufx` (effect), `KrAt` (Krate Audio), `Itrm` (Iterum)

- **Granular Delay Tempo Sync** (spec 038)
  - Added Time Mode parameter: "Free" (milliseconds) or "Synced" (note values)
  - Synced mode locks grain position to musical divisions based on host tempo
  - Same note value options as other delay modes (1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1)
  - Smooth transitions when switching between Free and Synced modes
  - UI shows Note Value dropdown only when Time Mode is "Synced"

- **Digital Delay Stereo Width Control** (spec 036)
  - New Width parameter (0-200%, default 100%)
  - Mid/Side processing applied to wet signal only
  - 0% = mono, 100% = original stereo, 200% = maximum stereo separation
  - Smooth parameter changes via OnePoleSmoother (20ms)

- **Dynamic Version Display**
  - Plugin version now displayed in UI from version.json
  - Version automatically stays in sync across builds

### Fixed

- **Editor Close/Reopen Crash**
  - VisibilityController stored direct pointer to VST3Editor
  - When editor was closed and reopened, pending IDependent callbacks accessed dangling pointer
  - Fixed by using pointer-to-pointer pattern that safely handles editor lifecycle

- **DC Offset in Feedback Network**
  - Added DC blocking to FeedbackNetwork to prevent low-frequency buildup
  - Particularly important for high feedback settings

- **BitCrusher Issues**
  - Fixed symmetric quantization for cleaner bit reduction
  - Fixed RNG bias in dither generation

- **UI Toggle Visibility**
  - Replaced COnOffButton with CCheckBox for proper visible state display

---

## [0.1.2] - 2025-12-29

### Added

- **Conditional Visibility for Delay Time Controls**
  - Delay time knobs now automatically hide when time mode is set to "Synced"
  - In synced mode, only note values control delay time, so the time knob is hidden to reduce UI clutter
  - Applies to both Digital Delay and PingPong Delay modes
  - Thread-safe implementation using VST3's IDependent mechanism
  - Updates run on UI thread at 30Hz via UpdateHandler
  - View-switch-safe: Uses dynamic tag-based control lookup instead of cached pointers
  - Survives mode switching between Digital ↔ PingPong without losing functionality

### Changed

- **Default Time Mode**: Digital and PingPong delays now default to "Synced" instead of "Free"
  - Tempo-synced delays are more common in modern production
  - Users can still switch to "Free" mode for millisecond-based timing
  - Delay time controls are hidden by default since synced mode ignores them

### Fixed

- **Thread Safety**: Visibility updates no longer cause crashes when triggered by automation or state loading
  - Previously called `setVisible()` directly from parameter callbacks (can be called from any thread)
  - Now uses IDependent pattern to defer updates to UI thread
- **View-Switch Safety**: Visibility updates continue working after switching between Digital and PingPong modes
  - UIViewSwitchContainer destroys/recreates controls when switching templates
  - Fixed by using dynamic tag-based lookup instead of caching control pointers
  - Added regression test documenting the dangling reference bug

---

## [0.1.1] - 2025-12-28

### Fixed

- **TapeDelay Dry/Wet Mix Bug**: The mix parameter now works correctly
  - Bug: Dry signal was read AFTER TapManager overwrote buffers with wet signal
  - Result: mix=0% or mix=50% still produced 100% wet output
  - Fix: Save dry signal to stack-allocated temp buffers BEFORE processing
  - Real-time safe: Uses `constexpr kMaxBlockSize` (4096), no heap allocations
  - Applies to both stereo and mono `process()` methods

### Added

- **Regression Tests for Dry/Wet Mix** (`tests/unit/features/tape_delay_test.cpp`)
  - 7 new tests to prevent reintroduction of the bug:
    - Mix=0 (dry): output equals input exactly
    - Mix=1 (wet): dry signal completely absent
    - Mix=0.5: both dry and wet present with correct proportions
    - Impulse produces immediate output (dry path)
    - Wet echo appears at correct delay time
    - Mono dry signal passthrough
    - Mono 50% mix produces correct amplitude

---

## [0.1.0] - 2025-12-28

### Summary

**First Alpha Milestone** - Iterum is now a fully functional VST3 delay plugin with 11 delay modes,
complete parameter automation, and a clean layered DSP architecture.

### Added

- **Complete VST3 Parameter Integration for All 11 Delay Modes**
  - Each mode has dedicated parameter packs with atomic storage for thread-safe access
  - Full parameter automation support in all major DAWs
  - State save/restore working correctly across sessions
  - Modes: Tape, BBD, Digital, PingPong, MultiTap, Shimmer, Reverse, Freeze, Ducking, Spectral, Granular

- **Mode-Specific Parameter Pack Architecture** (`src/parameters/*_params.h`)
  - Introduced dedicated parameter structs for each delay mode
  - Clean separation between VST3 parameter IDs and DSP state
  - Expanded parameter ID ranges for future scalability (100 IDs per mode)

- **CrossfadingDelayLine** (`src/dsp/primitives/crossfading_delay_line.h`)
  - Layer 1 primitive for click-free delay time changes
  - Two-tap crossfading algorithm with drift detection
  - Triggers 20ms crossfade when delay drifts ≥100 samples
  - Integrated into FeedbackNetwork and FlexibleFeedbackNetwork (Layer 3)
  - 15 comprehensive tests verifying crossfade behavior

- **Type-Safe Dropdown Mappings** (`src/parameters/dropdown_mappings.h`)
  - Explicit UI dropdown index → DSP enum conversion functions
  - Replaces fragile `static_cast<>` that assumed enum values match indices
  - Functions: `getBBDEraFromDropdown()`, `getLRRatioFromDropdown()`,
    `getTimingPatternFromDropdown()`, `getSpatialPatternFromDropdown()`
  - Clean architecture: Parameters layer handles UI translation, DSP layer stays pure
  - 39 test cases for all mappings

- **Tempo Sync Utilities** (`src/dsp/core/note_value.h`)
  - `noteToDelayMs()`: Convert note value + modifier to milliseconds at given BPM
  - `dropdownToDelayMs()`: Direct dropdown index → delay time conversion
  - `getNoteValueFromDropdown()`: Type-safe note value mapping
  - Comprehensive tests for all note values, dotted, and triplet variants

- **VST3 StringListParameter Helper** (`src/controller/parameter_helpers.h`)
  - Type-safe dropdown menu parameter creation
  - Proper step count and string list handling

- **Parameter Normalization Tests** (`tests/unit/vst/`)
  - Validates all 11 delay modes have correct parameter normalization
  - Ensures UI ↔ DSP value conversion is consistent

### Changed

- **TapeDelay**: Removed unused FeedbackNetwork component
  - Feedback was already working through TapManager's per-tap feedback mechanism
  - Simplified component composition, no functional change

- **Output Level Parameters**: Fixed double-conversion issue
  - Previously: normalized → dB → linear → dB → linear (redundant)
  - Now: Store dB directly in params, pass to delay without conversion
  - Affected modes: Tape, BBD, Digital, PingPong, MultiTap

- **FeedbackNetwork & FlexibleFeedbackNetwork**: Integrated CrossfadingDelayLine
  - Delay time changes now click-free via automatic crossfading
  - Maintains all existing API, internal implementation upgraded

### Fixed

- DigitalDelay tempo sync now uses proper Layer 0 note value utilities
- Dropdown parameters use explicit mappings instead of assumption-based casts

### Technical Details

- **Test Coverage**: 1,419 DSP test cases + 8 VST test cases, all passing
- **Architecture**: 5-layer DSP system (Layers 0-4) with clean dependency hierarchy
- **Real-Time Safe**: All audio processing is `noexcept`, allocation-free
- **Cross-Platform**: Builds on Windows (MSVC), macOS (Clang), Linux (GCC)

### Breaking Changes

None - this is the first alpha milestone.

---

## [0.0.35] - 2025-12-27

### Added

- **Layer 4 User Feature: GranularDelay** (`src/dsp/features/granular_delay.h`)
  - Real-time granular delay effect using tapped delay line architecture
  - Breaks incoming audio into small grains (10-500ms) with configurable parameters
  - Complete feature set with 6 user stories:
    - **US1: Basic Granular Texture**: Grain size (10-500ms) and density (1-100 Hz) control
    - **US2: Per-Grain Pitch Shifting**: ±24 semitones with pitch spray randomization
    - **US3: Position Randomization**: Delay time (0-2000ms) with position spray
    - **US4: Reverse Grain Playback**: Per-grain reverse probability (0-100%)
    - **US5: Granular Freeze**: Infinite sustain with frozen buffer capture
    - **US6: Feedback Path**: 0-120% feedback with soft limiting

- **Layered DSP architecture** for granular processing:
  - **Layer 0**: `pitch_utils.h` - semitone/ratio conversion utilities
  - **Layer 1**: `GrainPool` - lock-free grain allocation
  - **Layer 2**: `GrainProcessor`, `GrainScheduler`, `GrainEnvelope` - grain lifecycle
  - **Layer 3**: `GranularEngine` - grain orchestration and mixing
  - **Layer 4**: `GranularDelay` - complete user feature

- **Granular delay parameters**:
  - **Grain Size**: 10-500ms grain duration
  - **Density**: 1-100 grains/second spawn rate
  - **Delay Time**: 0-2000ms base read position
  - **Pitch**: ±24 semitones per-grain pitch shift
  - **Pitch Spray**: 0-100% pitch randomization
  - **Position Spray**: 0-100% time scatter
  - **Pan Spray**: 0-100% stereo field randomization
  - **Reverse Probability**: 0-100% chance of backwards playback
  - **Freeze**: Buffer capture for infinite sustain
  - **Feedback**: 0-120% with tanh() soft limiting
  - **Dry/Wet Mix**: 0-100% with parameter smoothing
  - **Output Gain**: -96dB to +6dB
  - **Envelope Type**: Hann, Trapezoid, Sine, Blackman window shapes

- **VST3 integration**: Full parameter automation support with state save/restore

- **Comprehensive test suite** (1,308 test cases, 4.6M+ assertions)
  - All functional requirements verified across all layers
  - Reproducible output via deterministic seeding

### Changed

- **Refactored pitch utilities**: Consolidated `pitchRatioFromSemitones`/`semitonesFromPitchRatio`
  from `pitch_shift_processor.h` into shared Layer 0 `pitch_utils.h`
  - `semitonesToRatio()` and `ratioToSemitones()` now used by both PitchShiftProcessor and GranularDelay

### Technical Details

- **Layer 4 architecture**: Composes Layers 0-3 per constitution
  - Uses: `StereoDelayBuffer` (L1) - 4-second circular buffer
  - Uses: `GrainPool` (L1) - 128-grain lock-free allocation
  - Uses: `OnePoleSmoother` (L1) x 4 - parameter smoothing
  - Uses: `GrainProcessor` (L2) - grain playback with pitch shift
  - Uses: `GrainScheduler` (L2) - stochastic grain spawning
  - Uses: `GranularEngine` (L3) - grain orchestration

- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, VIII, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/granular_delay.h"

Iterum::DSP::GranularDelay delay;
delay.prepare(44100.0);

// Configure granular texture
delay.setGrainSize(100.0f);      // 100ms grains
delay.setDensity(25.0f);         // 25 grains/sec
delay.setDelayTime(500.0f);      // 500ms delay
delay.setPitch(12.0f);           // +12 semitones (octave up)
delay.setPitchSpray(0.3f);       // 30% pitch randomization
delay.setPositionSpray(0.5f);    // 50% position scatter
delay.setReverseProbability(0.2f); // 20% reverse chance
delay.setFeedback(0.4f);         // 40% feedback
delay.setDryWet(0.6f);           // 60% wet

// Process audio
delay.process(inL, inR, outL, outR, numSamples);
```

## [0.0.34] - 2025-12-27

### Added

- **Layer 4 User Feature: SpectralDelay** (`src/dsp/features/spectral_delay.h`)
  - Frequency-domain delay effect using STFT analysis/resynthesis
  - Per-bin delay lines for ethereal, frequency-dependent echo effects
  - Composes STFT, OverlapAdd, SpectralBuffer, DelayLine (all Layer 1)
  - Complete feature set with 5 user stories:
    - **US1: Basic Spectral Delay**: Per-bin delays with configurable FFT (512-4096)
    - **US2: Delay Spread Control**: LowToHigh, HighToLow, CenterOut spread modes
    - **US3: Spectral Freeze**: Capture and hold spectrum indefinitely with crossfade
    - **US4: Frequency-Dependent Feedback**: Global feedback with tilt control
    - **US5: Spectral Diffusion**: 3-tap blur kernel for spectral smearing

- **SpreadDirection enum**: LowToHigh, HighToLow, CenterOut delay distribution

- **Spectral delay parameters**:
  - **FFT Size**: 512, 1024, 2048, 4096 (tradeoff: resolution vs latency)
  - **Base Delay**: 0-2000ms base delay time
  - **Spread**: 0-2000ms offset range across frequency bins
  - **Feedback**: 0-120% with tanh() soft limiting
  - **Feedback Tilt**: -100% to +100% (low vs high frequency emphasis)
  - **Diffusion**: 0-100% spectral blur amount
  - **Dry/Wet Mix**: 0-100% with parameter smoothing
  - **Output Gain**: -96dB to +6dB

- **Spectral freeze mode**:
  - Captures magnitude AND phase for true spectrum hold
  - 75ms crossfade for click-free transitions
  - Ignores new input when fully frozen

- **Comprehensive test suite** (33 test cases, 1,100 assertions)
  - All 26 functional requirements verified
  - 7/8 success criteria measured (1 PARTIAL: 60s test impractical)
  - All sample rates verified (44.1kHz to 192kHz)

- **Performance benchmark** (`benchmark_spectral_delay.exe`)
  - 0.93% CPU at 2048 FFT (target <3%) [PASS]
  - All FFT sizes under 1.1% CPU

### Technical Details

- **Layer 4 architecture**: Composes Layer 1 primitives only
  - Uses: `STFT` (L1) - spectral analysis
  - Uses: `OverlapAdd` (L1) - spectral resynthesis
  - Uses: `SpectralBuffer` (L1) - magnitude/phase storage
  - Uses: `DelayLine` (L1) x numBins - per-bin delay lines
  - Uses: `OnePoleSmoother` (L1) x 7 - parameter smoothing

- **Latency**: Equals FFT size samples (analysis window fill time)
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/spectral_delay.h"

Iterum::DSP::SpectralDelay delay;
delay.setFFTSize(2048);
delay.prepare(44100.0, 512);

// Configure spectral delay
delay.setBaseDelayMs(500.0f);
delay.setSpreadMs(300.0f);
delay.setSpreadDirection(SpreadDirection::LowToHigh);
delay.setFeedback(0.5f);
delay.setFeedbackTilt(0.2f);
delay.setDiffusion(0.3f);
delay.setDryWetMix(50.0f);
delay.snapParameters();

// Enable freeze for drone textures
delay.setFreezeEnabled(true);

// In audio callback
delay.process(left, right, numSamples, ctx);
```

---

## [0.0.33] - 2025-12-26

### Added

- **Layer 4 User Feature: DuckingDelay** (`src/dsp/features/ducking_delay.h`)
  - Delay effect with automatic gain reduction when input signal is present
  - Classic sidechain ducking for voiceover, podcasts, and live performance
  - Composes FlexibleFeedbackNetwork (L3) with dual DuckingProcessor (L2) instances
  - Complete feature set with 4 user stories:
    - **US1: Basic Ducking Delay**: Threshold-triggered gain reduction with attack/release
    - **US2: Feedback Path Ducking**: Target selection (Output, Feedback, Both)
    - **US3: Hold Time Control**: 0-500ms hold before release begins
    - **US4: Sidechain Filtering**: Optional 20-500Hz highpass on detection signal

- **DuckTarget enum**: Output (duck wet signal), Feedback (duck feedback only), Both

- **Ducking parameters**:
  - **Threshold**: -60dB to 0dB trigger level
  - **Duck Amount**: 0-100% (maps to 0 to -48dB depth)
  - **Attack Time**: 0.1-100ms
  - **Release Time**: 10-2000ms
  - **Hold Time**: 0-500ms (prevents pumping)
  - **Sidechain Filter**: Optional HP filter to ignore bass content

- **User controls**:
  - **Dry/Wet Mix**: 0-100% with 20ms parameter smoothing
  - **Output Gain**: -96dB to +6dB master gain
  - **Gain Reduction Meter**: Real-time ducking depth display

- **Comprehensive test suite** (35 test cases, 110 assertions)
  - All 24 functional requirements verified
  - All 8 success criteria measured
  - Click-free transitions verified via OnePoleSmoother
  - -48dB attenuation at 100% duck amount verified

### Technical Details

- **Layer 4 architecture**: Composes Layer 1-3 components
  - Uses: `FlexibleFeedbackNetwork` (L3) - delay engine with feedback and filtering
  - Uses: `DuckingProcessor` (L2) x 2 - output and feedback ducking instances
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing for dryWet, outputGain, delayTime

- **Target modes**:
  - **Output**: Ducks delay output before dry/wet mix (classic ducking)
  - **Feedback**: Ducks feedback path only (preserves first tap, reduces repeats)
  - **Both**: Ducks both paths simultaneously (maximum separation)

- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/ducking_delay.h"

Iterum::DSP::DuckingDelay delay;
delay.prepare(44100.0, 512);

// Configure ducking
delay.setDuckingEnabled(true);
delay.setThreshold(-30.0f);        // Trigger at -30dB
delay.setDuckAmount(70.0f);        // Duck by 70% (~33.6dB)
delay.setAttackTime(10.0f);        // 10ms attack
delay.setReleaseTime(200.0f);      // 200ms release
delay.setHoldTime(50.0f);          // 50ms hold
delay.setDuckTarget(DuckTarget::Output);

// Configure delay
delay.setDelayTimeMs(500.0f);
delay.setFeedbackAmount(50.0f);
delay.setDryWetMix(50.0f);
delay.snapParameters();

// Process audio
delay.process(left, right, numSamples, ctx);

// Read gain reduction for metering
float gr = delay.getGainReduction();  // 0 to -48dB
```

---

## [0.0.32] - 2025-12-26

### Added

- **Layer 4 User Feature: FreezeMode** (`src/dsp/features/freeze_mode.h`)
  - Infinite sustain of delay buffer contents with optional evolving textures
  - Composes FlexibleFeedbackNetwork (L3) with injected FreezeFeedbackProcessor
  - When frozen: sets feedback to 100%, mutes input, optionally enables pitch shifting
  - Complete feature set with 5 user stories:
    - **US1: Basic Freeze Capture**: Capture and hold delay buffer indefinitely
    - **US2: Shimmer Freeze**: Pitch-shifted feedback for evolving textures
    - **US3: Decay Control**: 0-100% decay (0 = infinite, 100 = fast fade)
    - **US4: Diffusion**: Smear transients into smooth pad-like textures
    - **US5: Tonal Shaping**: LP/HP/BP filter in feedback path

- **FreezeFeedbackProcessor** (internal to FreezeMode)
  - Implements `IFeedbackProcessor` for injection into FlexibleFeedbackNetwork
  - Combines PitchShifter, DiffusionNetwork, and cumulative decay gain
  - Shimmer mix blends pitched and unpitched content

- **Freeze parameters**:
  - **Freeze Toggle**: Smooth click-free engage/disengage via FFN crossfade
  - **Pitch Shift**: ±24 semitones with real-time modulation
  - **Shimmer Mix**: 0-100% blend of pitched/unpitched frozen content
  - **Decay**: 0% (infinite sustain) to 100% (reach -60dB in 500ms)
  - **Diffusion**: 0-100% smearing of transients
  - **Filter**: Optional LP/HP/BP with 20Hz-20kHz cutoff

- **User controls**:
  - **Dry/Wet Mix**: 0-100% with 20ms parameter smoothing
  - **Output Level**: -96dB to +6dB master gain

- **Comprehensive test suite** (40 test cases, 70 assertions)
  - All 29 functional requirements verified
  - All 8 success criteria measured
  - Click-free freeze transitions verified
  - Infinite sustain at 0% decay verified
  - -60dB decay in 500ms at 100% decay verified

### Technical Details

- **Layer 4 architecture**: Composes Layer 1-3 components
  - Uses: `FlexibleFeedbackNetwork` (L3) - delay + feedback + filtering + freeze crossfade
  - Uses: `FreezeFeedbackProcessor` - implements `IFeedbackProcessor` interface
  - Uses: `PitchShifter` (L2) x 2 - stereo pitch shifting for shimmer
  - Uses: `DiffusionNetwork` (L2) - pad-like texture generation
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing

- **Freeze state management**: Handled by FlexibleFeedbackNetwork's built-in `setFreezeEnabled()`
  - Smooth crossfade during state transitions (no clicks)
  - Input muting when frozen (>96dB attenuation)

- **Decay implementation**: Cumulative per-sample gain reduction
  - Persists across process() calls for continuous fade effect
  - Resets on freeze disengage or reset()

- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/freeze_mode.h"

Iterum::DSP::FreezeMode freeze;
freeze.prepare(44100.0, 512, 2000.0f);
freeze.setDelayTimeMs(500.0f);
freeze.setFeedbackAmount(0.8f);
freeze.setDryWetMix(100.0f);

// Configure shimmer freeze
freeze.setPitchSemitones(12.0f);  // +1 octave
freeze.setShimmerMix(50.0f);      // 50% blend
freeze.setDecay(0.0f);            // Infinite sustain
freeze.setDiffusionAmount(30.0f); // Light smearing

freeze.snapParameters();

// Process audio, then engage freeze
freeze.setFreezeEnabled(true);  // Freeze current delay content
freeze.process(left, right, numSamples, ctx);
```

---

## [0.0.31] - 2025-12-26

### Added

- **Layer 4 User Feature: ReverseDelay** (`src/dsp/features/reverse_delay.h`)
  - Reverse delay effect that captures audio in chunks and plays it back reversed
  - Composes FlexibleFeedbackNetwork (L3) with injected ReverseFeedbackProcessor (L2)
  - Follows ShimmerDelay architectural pattern
  - Complete feature set with 4 user stories:
    - **US1: Basic Reverse Echo**: Capture and play backwards with feedback
    - **US2: Smooth Crossfade**: Equal-power crossfade between chunks eliminates clicks
    - **US3: Playback Modes**: Full Reverse, Alternating, and Random direction per chunk
    - **US4: Feedback Filtering**: Optional LP/HP/BP filter in feedback path

- **Layer 2 Processor: ReverseFeedbackProcessor** (`src/dsp/processors/reverse_feedback_processor.h`)
  - Implements `IFeedbackProcessor` for injection into FlexibleFeedbackNetwork
  - Wraps stereo ReverseBuffer pair with synchronized chunk boundaries
  - Manages playback mode logic (FullReverse, Alternating, Random)

- **Layer 1 Primitive: ReverseBuffer** (`src/dsp/primitives/reverse_buffer.h`)
  - Double-buffer primitive for capturing audio and playing back in reverse
  - Features: configurable chunk size (10-2000ms), equal-power crossfade, forward/reverse modes
  - Real-time safe: all methods noexcept, no allocations in process()

- **Chunk control parameters**:
  - **Chunk Size**: 10-2000ms controls the size of captured/reversed audio segments
  - **Tempo Sync**: Lock chunk size to note values (1/32 to 1/1)
  - **Crossfade**: 0-100% overlap between consecutive chunks

- **Playback modes**:
  - **Full Reverse**: Every chunk plays backwards
  - **Alternating**: Chunks alternate forward/reverse/forward/reverse
  - **Random**: 50/50 random direction per chunk using fast RNG

- **Feedback parameters**:
  - **Amount**: 0-120% with soft limiting for stability
  - **Filter**: Optional lowpass/highpass/bandpass in feedback path (20Hz-20kHz)

- **User controls**:
  - **Dry/Wet Mix**: 0-100% with 20ms parameter smoothing
  - **Output Level**: -96dB to +6dB master gain

- **Comprehensive test suite** (31 test cases, 22,887 assertions)
  - All 25 functional requirements verified
  - All 8 success criteria measured
  - Sample-accurate reverse playback verified
  - Click-free crossfade transitions verified
  - All three playback modes verified

### Technical Details

- **Layer 4 architecture**: Composes Layer 1-3 components
  - Uses: `FlexibleFeedbackNetwork` (L3) - delay + feedback + filtering + limiting
  - Uses: `ReverseFeedbackProcessor` (L2) - implements `IFeedbackProcessor` interface
  - Uses: `ReverseBuffer` (L1) x 2 - stereo reverse buffer pair
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing

- **PlaybackMode enum**: FullReverse, Alternating, Random
  - Mode changes occur at chunk boundary for glitch-free transitions

- **Latency**: Equal to chunk size (reported via getLatencySamples())

- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/reverse_delay.h"

Iterum::DSP::ReverseDelay reverseDelay;
reverseDelay.prepare(44100.0, 512, 2000.0f);

// Configure basic reverse delay
reverseDelay.setChunkSizeMs(500.0f);
reverseDelay.setPlaybackMode(PlaybackMode::FullReverse);
reverseDelay.setCrossfadePercent(50.0f);
reverseDelay.setFeedbackAmount(0.5f);
reverseDelay.setDryWetMix(50.0f);
reverseDelay.snapParameters();

// In process callback
reverseDelay.process(left, right, numSamples, ctx);
```

---

## [0.0.30] - 2025-12-26

### Added

- **Layer 4 User Feature: ShimmerDelay** (`src/dsp/features/shimmer_delay.h`)
  - Pitch-shifted feedback delay for ambient/ethereal textures
  - Composes FlexibleFeedbackNetwork (L3), PitchShiftProcessor (L2), DiffusionNetwork (L2)
  - Complete feature set with 6 user stories:
    - **US1: Shimmer Effect**: Pitch-shifted feedback creates cascading harmonics
    - **US2: Reverb-like Textures**: Diffusion network smears repeats for pad-like sound
    - **US3: Blend Control**: Mix between pitched and unpitched feedback
    - **US4: Tempo Sync**: Lock delay time to host tempo with note values
    - **US5: Mix/Output**: Independent dry/wet and output level controls
    - **US6: Quality Modes**: Simple, Granular, and Phase Vocoder pitch algorithms

- **Layer 3 System Component: FlexibleFeedbackNetwork** (`src/dsp/systems/flexible_feedback_network.h`)
  - Extensible feedback network with processor injection via `IFeedbackProcessor` interface
  - Enables shimmer, freeze mode, and future experimental effects
  - Features: freeze mode, feedback filtering, >100% feedback limiting, hot-swap with crossfade
  - Hybrid design: sample-by-sample delay loop with block-based processor support

- **Pitch shifting parameters**:
  - **Range**: ±24 semitones + ±100 cents fine tuning
  - **Modes**: Simple (low CPU), Granular (balanced), PhaseVocoder (high quality)
  - **Smoothing**: 20ms pitch ratio smoothing prevents zipper noise

- **Diffusion parameters**:
  - **Amount**: 0-100% controls diffusion intensity
  - **Size**: 0-100% controls diffusion time/density

- **Feedback parameters**:
  - **Amount**: 0-120% with limiter + tanh soft clipping for stability
  - **Filter**: Lowpass/highpass in feedback path (20Hz-20kHz)
  - **Freeze**: Input mute + 100% feedback for infinite sustain

- **User controls**:
  - **Delay Time**: 10-5000ms with tempo sync support
  - **Shimmer Mix**: 0-100% blend of pitched/unpitched feedback
  - **Dry/Wet Mix**: 0-100% with 20ms parameter smoothing
  - **Output Level**: ±12dB master gain

- **Comprehensive test suite** (21 test cases, 116 assertions)
  - All 28 functional requirements verified
  - All 9 success criteria measured
  - Pitch accuracy verified (±5 cents)
  - Feedback stability verified at 120%
  - Tempo sync accuracy verified (±1 sample)

### Technical Details

- **Layer 4 architecture**: Composes Layer 2-3 components
  - Uses: `FlexibleFeedbackNetwork` (L3) - delay + feedback + filtering + limiting
  - Uses: `ShimmerFeedbackProcessor` - implements `IFeedbackProcessor` interface
  - Uses: `PitchShiftProcessor` (L2) × 2 - stereo pitch shifting
  - Uses: `DiffusionNetwork` (L2) - allpass diffusion for reverb texture
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing
  - Uses: `dbToGain` (L0) - level conversion

- **IFeedbackProcessor interface**: Enables injection of arbitrary processors into feedback path
  - `prepare()`, `process()`, `reset()`, `getLatencySamples()`
  - Real-time safe: no allocations in process()

- **Modulation destinations**: DelayTime, Pitch, ShimmerMix, DiffusionAmount, FilterCutoff, DryWetMix

- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Fixed

- **FlexibleFeedbackNetwork parameter smoothing**: Processor mix now uses per-sample smoothing to prevent clicks
- **FlexibleFeedbackNetwork freeze interpolation**: Feedback now interpolates smoothly instead of hard threshold at 50%

### Usage

```cpp
#include "dsp/features/shimmer_delay.h"

using namespace Iterum::DSP;

ShimmerDelay shimmer;
shimmer.prepare(44100.0, 512, 5000.0f);  // 5s max delay

// Configure shimmer effect
shimmer.setPitchSemitones(12.0f);        // +1 octave
shimmer.setShimmerMix(75.0f);            // 75% pitched
shimmer.setFeedbackAmount(0.7f);         // 70% feedback
shimmer.setDiffusionAmount(50.0f);       // 50% diffusion

// Configure output
shimmer.setDryWetMix(60.0f);             // 60% wet
shimmer.setOutputGainDb(-3.0f);          // -3dB output

// In audio callback
shimmer.process(left, right, numSamples, ctx);
```

## [0.0.29] - 2025-12-26

### Added

- **Layer 4 User Feature: MultiTapDelay** (`src/dsp/features/multi_tap_delay.h`)
  - Rhythmic multi-tap delay with 25 preset patterns and custom pattern support
  - Composes TapManager (L3), FeedbackNetwork (L3), ModulationMatrix (L3)
  - Complete feature set with 6 user stories:
    - **US1: Basic Patterns**: 14 rhythmic + 5 mathematical + 6 spatial presets
    - **US2: Per-Tap Control**: Independent level, pan, filter, mute per tap
    - **US3: Master Feedback**: 0-110% feedback with saturation safety
    - **US4: Pattern Morphing**: Smooth transitions between patterns (50-2000ms)
    - **US5: Per-Tap Modulation**: ModulationMatrix integration for LFO routing
    - **US6: Tempo Sync**: Lock to host tempo with note values

- **Timing patterns**:
  - **Rhythmic**: WholeNote, HalfNote, QuarterNote, EighthNote, SixteenthNote, ThirtySecondNote
  - **Dotted**: DottedHalf, DottedQuarter, DottedEighth, DottedSixteenth
  - **Triplet**: TripletHalf, TripletQuarter, TripletEighth, TripletSixteenth
  - **Mathematical**: GoldenRatio (×1.618), Fibonacci, Exponential, PrimeNumbers, LinearSpread

- **Spatial patterns**:
  - **Cascade**: Pan sweeps L→R across taps
  - **Alternating**: Pan alternates L, R, L, R
  - **Centered**: All taps center pan
  - **WideningStereo**: Pan spreads progressively wider
  - **DecayingLevel**: Each tap -3dB from previous
  - **FlatLevel**: All taps equal level

- **User controls**:
  - **Tap Count**: 2-16 simultaneous taps
  - **Base Time**: 1-5000ms with tempo sync
  - **Per-Tap**: Level (-96 to +6dB), Pan (-100 to +100), Filter (20Hz-20kHz), Mute
  - **Feedback**: 0-110% with automatic saturation limiting
  - **Morph Time**: 50-2000ms for pattern transitions
  - **Mix**: Dry/wet balance with 20ms parameter smoothing
  - **Output Level**: ±12dB master gain

- **Comprehensive test suite** (23 test cases, 225 assertions)
  - All 30 functional requirements verified
  - All 9 success criteria measured (SC-005, SC-007, SC-008 explicitly tested)
  - Click prevention verified via sample-to-sample jump analysis
  - CPU benchmark verified (<200ms for 1s audio in debug)

### Technical Details

- **Layer 4 architecture**: Composes Layer 3 system components
  - Uses: `TapManager` (L3) - multi-tap delay management
  - Uses: `FeedbackNetwork` (L3) - master feedback with filtering/saturation
  - Uses: `ModulationMatrix` (L3) - per-tap parameter modulation
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing
  - Uses: `dbToGain` (L0) - level conversion
- **TimingPattern enum**: 20 values (14 rhythmic + 5 mathematical + Custom)
- **SpatialPattern enum**: 7 values (6 presets + Custom)
- **Modulation destination IDs**: time (0-15), level (16-31), pan (32-47), cutoff (48-63)
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Process Improvements

- **Added Dependency API Contracts section to plan template** (`.specify/templates/plan-template.md`)
  - Prevents API mismatch errors (e.g., `ctx.tempo` vs `ctx.tempoBPM`)
  - Requires explicit header file verification before implementation
  - Documents common gotchas for each dependency

### Usage

```cpp
#include "dsp/features/multi_tap_delay.h"

using namespace Iterum::DSP;

MultiTapDelay delay;
delay.prepare(44100.0, 512, 5000.0f);  // 5s max delay

// Load rhythmic pattern
delay.loadTimingPattern(TimingPattern::DottedEighth, 4);
delay.applySpatialPattern(SpatialPattern::Cascade);
delay.setTempo(120.0f);

// Configure feedback
delay.setFeedbackAmount(0.6f);         // 60% feedback
delay.setDryWetMix(50.0f);             // 50% wet

// Per-tap adjustments
delay.setTapLevelDb(0, -3.0f);         // First tap -3dB
delay.setTapPan(1, 50.0f);             // Second tap right

// Pattern morphing
delay.morphToPattern(TimingPattern::GoldenRatio, 500.0f);  // 500ms morph

// In audio callback
delay.process(left, right, numSamples, ctx);
```

---

## [0.0.28] - 2025-12-26

### Added

- **Layer 4 User Feature: PingPongDelay** (`src/dsp/features/ping_pong_delay.h`)
  - Classic stereo ping-pong delay with alternating L/R bounces
  - Composes DelayLine (L1), LFO (L1), OnePoleSmoother (L1), DynamicsProcessor (L2), stereoCrossBlend (L0)
  - Complete feature set with 6 user stories:
    - **US1: Classic Ping-Pong**: Alternating L/R delays with feedback and mix control
    - **US2: Asymmetric Timing**: 7 L/R ratios for polyrhythmic patterns (1:1, 2:1, 3:2, 4:3, 1:2, 2:3, 3:4)
    - **US3: Tempo Sync**: Lock to host tempo with note values and modifiers
    - **US4: Stereo Width**: 0% (mono) to 200% (ultra-wide) using M/S processing
    - **US5: Cross-Feedback**: Variable 0-100% for dual mono to full ping-pong
    - **US6: Modulation**: Optional LFO with 90° L/R phase offset

- **L/R ratio presets for polyrhythmic delays**:
  - **1:1**: Classic even ping-pong
  - **2:1 / 1:2**: Double speed relationships
  - **3:2 / 2:3**: Triplet feel polyrhythms
  - **4:3 / 3:4**: Subtle polyrhythmic patterns

- **User controls**:
  - **Time**: 1-10000ms delay with tempo sync support
  - **L/R Ratio**: 7 preset timing relationships
  - **Feedback**: 0-120% with soft limiting (tanh-based)
  - **Cross-Feedback**: 0% (dual mono) to 100% (full ping-pong)
  - **Width**: 0-200% stereo spread using M/S technique
  - **Modulation**: Depth (0-100%) and Rate (0.1-10Hz)
  - **Mix**: Dry/wet balance with 20ms parameter smoothing
  - **Output Level**: -120dB to +12dB master gain

- **Comprehensive test suite** (23 test cases, 16028 assertions)
  - All 34 functional requirements verified
  - All 10 success criteria measured
  - Ratio accuracy within 1% tolerance
  - Width correlation measurements verified

### Technical Details

- **Layer 4 architecture**: Composes Layer 0-2 components
  - Uses: `DelayLine` (L1) - 2 instances for independent L/R timing
  - Uses: `LFO` (L1) - 2 instances with 90° phase offset
  - Uses: `OnePoleSmoother` (L1) - 7 instances for parameter smoothing
  - Uses: `DynamicsProcessor` (L2) - feedback limiting for >100%
  - Uses: `stereoCrossBlend` (L0) - cross-feedback routing
  - Uses: `dbToGain` (L0) - output level conversion
- **LRRatio enum**: OneToOne, TwoToOne, ThreeToTwo, FourToThree, OneToTwo, TwoToThree, ThreeToFour
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/ping_pong_delay.h"

using namespace Iterum::DSP;

PingPongDelay delay;
delay.prepare(44100.0, 512, 2000.0f);  // 2s max delay

// Classic ping-pong
delay.setDelayTimeMs(375.0f);            // 375ms delay
delay.setFeedback(0.5f);                 // 50% feedback
delay.setCrossFeedback(1.0f);            // Full ping-pong
delay.setLRRatio(LRRatio::OneToOne);     // Even L/R timing
delay.setWidth(100.0f);                  // Natural stereo
delay.setMix(0.4f);                      // 40% wet

// Polyrhythmic variation
delay.setLRRatio(LRRatio::ThreeToTwo);   // Triplet feel
delay.setCrossFeedback(0.75f);           // Partial crossing

// In audio callback
delay.process(left, right, numSamples, ctx);
```

## [0.0.27] - 2025-12-26

### Added

- **Layer 4 User Feature: DigitalDelay** (`src/dsp/features/digital_delay.h`)
  - Clean digital delay with three era presets (Pristine, 80s Digital, Lo-Fi)
  - Composes DelayEngine (L3), FeedbackNetwork (L3), CharacterProcessor (L3), DynamicsProcessor (L2)
  - Complete feature set with 6 user stories:
    - **US1: Core Delay**: Pristine digital delay with 1-10000ms range and tempo sync
    - **US2: Era Presets**: Three distinct digital characters with age control
    - **US3: Feedback Limiting**: Program-dependent limiter prevents runaway oscillation
    - **US4: LFO Modulation**: 6 waveforms (Sine, Triangle, Saw, Square, S&H, Random)
    - **US5: Tempo Sync**: Musical note values with dotted/triplet modifiers
    - **US6: Stereo Processing**: Full stereo support with channel separation

- **Era presets with distinct characteristics**:
  - **Pristine**: Perfect digital delay, <-120dB noise floor, 0.1dB flat response
  - **EightiesDigital**: 14kHz anti-alias filter, -80dB noise floor, subtle character
  - **LoFi**: Aggressive bit/sample rate reduction, higher noise, heavy filtering

- **User controls**:
  - **Time**: 1-10000ms delay with tempo sync support
  - **Feedback**: 0-120% with program-dependent limiter (Soft/Medium/Hard knee)
  - **Modulation**: Depth (0-100%) and Rate (0.1-10Hz) with 6 LFO waveforms
  - **Era**: Pristine / 80s Digital / Lo-Fi character presets
  - **Age**: 0-100% degradation intensity within each era
  - **Mix**: Dry/wet balance with 20ms parameter smoothing
  - **Output Level**: -96dB to +12dB master gain

- **Precision audio measurement tests**:
  - SC-001: Pristine frequency response 0.1dB flat 20Hz-20kHz (FFT verified)
  - SC-002: Pristine noise floor below -120dB (RMS measurement)
  - FR-009: 80s mode 18kHz attenuation >3dB vs 10kHz (anti-alias filter)
  - FR-010: 80s noise floor between -90dB and -70dB

- **Comprehensive test suite** (35 test cases, 9939 assertions)
  - All 44 functional requirements verified
  - Precision audio measurements with FFT analysis
  - Era preset distinction tests
  - Parameter smoothing and edge case coverage

### Technical Details

- **Layer 4 architecture**: Composes Layer 0-3 components
  - Uses: `DelayEngine` (L3) - core delay with tempo sync (set to 100% wet)
  - Uses: `FeedbackNetwork` (L3) - feedback with filtering
  - Uses: `CharacterProcessor` (L3) - DigitalVintage mode for 80s/Lo-Fi
  - Uses: `DynamicsProcessor` (L2) - program-dependent limiter (100:1 ratio, peak detection)
  - Uses: `LFO` (L1) - modulation with 6 waveform shapes
  - Uses: `Biquad` (L1) - anti-aliasing filter for 80s/Lo-Fi era
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing (20ms)
  - Uses: `Xorshift32` (L0) - noise generation for 80s/Lo-Fi era
- **DigitalEra enum**: Pristine, EightiesDigital, LoFi
- **LimiterCharacter enum**: Soft (6dB knee), Medium (3dB), Hard (0dB)
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Fixed

- **Double-mixing bug**: DelayEngine now set to 100% wet to avoid comb filter artifacts when DigitalDelay handles dry/wet mixing

### Usage

```cpp
#include "dsp/features/digital_delay.h"

using namespace Iterum::DSP;

DigitalDelay delay;
delay.prepare(44100.0, 512, 10000.0f);  // 10s max delay

// Set digital character
delay.setTime(500.0f);                     // 500ms delay
delay.setFeedback(0.5f);                   // 50% feedback
delay.setEra(DigitalEra::EightiesDigital); // 80s character
delay.setAge(0.3f);                        // Moderate aging
delay.setLimiterCharacter(LimiterCharacter::Soft);  // Soft knee
delay.setModulationDepth(0.1f);            // Subtle modulation
delay.setModulationRate(0.5f);             // Slow LFO
delay.setMix(0.5f);                        // 50/50 dry/wet

// In audio callback
delay.process(left, right, numSamples, ctx);
```

## [0.0.26] - 2025-12-26

### Added

- **Layer 4 User Feature: BBDDelay** (`src/dsp/features/bbd_delay.h`)
  - Authentic bucket-brigade device delay emulation (MN3005, MN3007, MN3205, SAD1024)
  - Composes DelayEngine (L3), FeedbackNetwork (L3), CharacterProcessor (L3), Biquad (L1)
  - Complete feature set with 6 user stories:
    - **US1: Darker Character**: BBD produces noticeably warmer tone than digital delay
    - **US2: Bandwidth Tracking**: Shorter delays = brighter, longer delays = darker (BBD clock physics)
    - **US3: Era Presets**: Four BBD chip models with distinct tonal signatures
    - **US4: Chorus Modulation**: Triangle LFO pitch wobble (0-100% depth, 0.1-10Hz rate)
    - **US5: Age Degradation**: Component aging simulation (noise, bandwidth reduction)
    - **US6: Compander Artifacts**: Subtle pumping/breathing dynamics characteristic of BBD

- **User controls**:
  - **Time**: Delay time 20-900ms (inversely affects bandwidth)
  - **Feedback**: 0-100% with soft limiting in feedback path
  - **Modulation Depth**: 0-100% triangle LFO modulation
  - **Modulation Rate**: 0.1-10Hz modulation speed
  - **Age**: 0-100% degradation (noise floor + bandwidth reduction + compander intensity)
  - **Era**: BBD chip model selection (MN3005/MN3007/MN3205/SAD1024)
  - **Mix**: Dry/wet balance with smoothing
  - **Output Level**: Master gain in dB

- **BBD-specific behaviors**:
  - **Bandwidth tracking**: 50ms = 12kHz cutoff, 900ms = 3kHz cutoff (BBD clock rate physics)
  - **Era characteristics**:
    - MN3005: Brightest, cleanest, 12kHz max bandwidth
    - MN3007: Slightly darker, 10kHz max bandwidth
    - MN3205: Darker, more lo-fi, 8kHz max bandwidth
    - SAD1024: Darkest, most "vintage", 6kHz max bandwidth
  - **Compander simulation**: Attack/release asymmetry creates subtle pumping on transients
  - **Noise floor**: Increases with Age and varies by Era preset

- **Perceptual testing guide** (`specs/025-bbd-delay/perceptual-testing.md`)
  - Manual testing procedures for SC-001 to SC-006 audio quality criteria
  - Test signals, procedures, pass criteria, optional analysis tools
  - Troubleshooting section for common listening issues

- **Comprehensive test suite** (26 test cases, 18762 assertions)
  - All 41 functional requirements verified
  - Bandwidth tracking tests (short vs long delay)
  - Era preset distinction tests
  - Compander attack/release tests
  - Edge cases: extreme settings, parameter smoothing, real-time safety

### Technical Details

- **Layer 4 architecture**: Composes Layer 0-3 components
  - Uses: `DelayEngine` (L3) - core delay with tempo sync support
  - Uses: `FeedbackNetwork` (L3) - feedback with filtering and saturation
  - Uses: `CharacterProcessor` (L3) - BBD mode for noise/modulation
  - Uses: `Biquad` (L1) - dynamic lowpass for bandwidth tracking
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing (20ms)
  - Uses: `dbToGain()` (L0) - level conversion
- **BBDChipModel enum**: MN3005, MN3007, MN3205, SAD1024
- **Bandwidth calculation**: `cutoff = baseHz * (1.0 - (delayMs - 50) / 850 * (1.0 - minRatio))`
- **Compander emulation**: Fast attack (1ms), slow release (50ms) envelope
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/bbd_delay.h"

using namespace Iterum::DSP;

BBDDelay delay;
delay.prepare(44100.0, 512, 900.0f);  // 900ms max delay

// Set BBD character
delay.setTime(300.0f);             // Delay time in ms
delay.setFeedback(0.5f);           // 50% feedback
delay.setModulationDepth(0.3f);    // Moderate chorus wobble
delay.setModulationRate(0.5f);     // Slow LFO rate
delay.setAge(0.2f);                // Slight component aging
delay.setEra(BBDChipModel::MN3005); // Cleanest BBD model
delay.setMix(0.5f);                // 50/50 dry/wet

// In audio callback
delay.process(left, right, numSamples);
```

## [0.0.25] - 2025-12-25

### Added

- **Layer 4 User Feature: TapeDelay** (`src/dsp/features/tape_delay.h`)
  - Classic tape delay emulation inspired by Roland RE-201, Echoplex, and Watkins Copicat
  - Composes TapManager (L3), FeedbackNetwork (L3), CharacterProcessor (L3), MotorController
  - Complete feature set with 6 user stories:
    - **US1: Basic Tape Echo**: Warm delay with progressive high-frequency rolloff
    - **US2: Wow/Flutter Modulation**: Characteristic pitch wobble via Wear control
    - **US3: Tape Saturation**: Warm compression and harmonic richness
    - **US4: Multi-Head Echo Pattern**: RE-201 style 3-head rhythmic delays (1x, 1.5x, 2x ratios)
    - **US5: Age/Degradation Character**: Lo-fi character with hiss, rolloff, splice artifacts
    - **US6: Motor Inertia**: Realistic pitch sweep when changing delay times (200-500ms transition)

- **User controls**:
  - **Motor Speed**: Delay time 20-2000ms with motor inertia simulation
  - **Wear**: Wow/flutter depth (0-100%) + hiss level, wow rate scales inversely with speed
  - **Saturation**: Tape drive/warmth (0-100%)
  - **Age**: EQ rolloff (12kHz→4kHz) + noise + splice artifact intensity
  - **Echo Heads**: 3 playback heads with enable, level (-inf to +6dB), pan (-100 to +100)
  - **Feedback**: 0-120% with soft limiting to prevent runaway
  - **Mix**: Dry/wet balance with smoothing
  - **Output Level**: Master gain in dB

- **Tape-specific behaviors**:
  - **FR-007**: Wow rate scales inversely with Motor Speed (slower tape = slower wow)
  - **FR-023**: Optional splice artifacts at tape loop point (periodic transients)
  - **FR-024**: Age control simultaneously affects hiss, rolloff, AND splice intensity
  - Motor inertia creates smooth 200-500ms transitions when changing delay time
  - Head gap simulation through CharacterProcessor frequency response

- **Comprehensive test suite** (26 test cases, 1628 assertions)
  - All 36 functional requirements verified
  - Wow rate inverse scaling tests
  - Splice artifact periodic occurrence tests
  - Age/splice intensity relationship tests
  - Edge cases: all heads disabled, 120% feedback stability, parameter smoothing

### Technical Details

- **Layer 4 architecture**: Composes Layer 0-3 components
  - Uses: `TapManager` (L3) - 3-head delay with ratios 1x, 1.5x, 2x
  - Uses: `FeedbackNetwork` (L3) - feedback with lowpass filtering at 8kHz
  - Uses: `CharacterProcessor` (L3) - tape mode with wow/flutter/saturation/hiss
  - Uses: `MotorController` - OnePoleSmoother-based inertia simulation
  - Uses: `OnePoleSmoother` (L1) - parameter smoothing (20ms)
  - Uses: `dbToGain()` (L0) - level conversion
- **TapeHead struct**: Per-head enabled, ratio, level, pan state
- **MotorController class**: Smooth delay time transitions with configurable inertia (100-1000ms)
- **Splice artifact generation**: Decaying impulse with noise character at tape loop interval
- **Real-time safe**: `noexcept`, no allocations in `process()`
- **Constitution compliance**: Principles II, III, IX, X, XII, XIII, XIV, XV

### Usage

```cpp
#include "dsp/features/tape_delay.h"

using namespace Iterum::DSP;

TapeDelay delay;
delay.prepare(44100.0, 512, 2000.0f);  // 2 second max delay

// Set tape character
delay.setMotorSpeed(375.0f);   // Delay time in ms
delay.setWear(0.4f);           // Moderate wow/flutter
delay.setSaturation(0.3f);     // Light tape warmth
delay.setAge(0.2f);            // Slight degradation
delay.setFeedback(0.5f);       // 50% feedback
delay.setMix(0.5f);            // 50/50 dry/wet

// Configure heads (RE-201 style)
delay.setHeadEnabled(0, true);   // Head 1: 1x delay
delay.setHeadEnabled(1, true);   // Head 2: 1.5x delay
delay.setHeadEnabled(2, false);  // Head 3: disabled
delay.setHeadLevel(0, 0.0f);     // Unity gain
delay.setHeadLevel(1, -3.0f);    // -3dB
delay.setHeadPan(0, -30.0f);     // Slightly left
delay.setHeadPan(1, 30.0f);      // Slightly right

// Enable optional splice artifacts
delay.setSpliceEnabled(true);    // Age controls intensity

// In audio callback
delay.process(left, right, numSamples);
```

## [0.0.24] - 2025-12-25

### Added

- **Layer 3 System Component: TapManager** (`src/dsp/systems/tap_manager.h`)
  - Multi-tap delay manager with up to 16 independent delay taps
  - Complete feature set with 6 user stories:
    - **US1: Basic Multi-Tap Delay**: 16 taps with independent time and level
    - **US2: Per-Tap Spatial Positioning**: Constant-power pan law (-100 to +100)
    - **US3: Per-Tap Filtering**: LP/HP/Bypass with 20Hz-20kHz cutoff, 0.5-10 Q
    - **US4: Feedback Routing**: Per-tap feedback (0-100%) with soft limiting
    - **US5: Preset Patterns**: QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci
    - **US6: Tempo Sync**: Free-running (ms) or tempo-synced (NoteValue) time modes
  - Delay time accuracy within 1 sample (SC-003)
  - Constant-power pan law preserves power within 0.5dB (SC-004)
  - 20ms parameter smoothing on all controls (SC-002)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - Soft feedback limiting via `tanh()` prevents runaway oscillation (FR-021)

- **Preset pattern implementations**:
  - `QuarterNote` - taps at 1×, 2×, 3×... quarter note intervals
  - `DottedEighth` - taps at 0.75× quarter note intervals
  - `Triplet` - taps at 0.667× quarter note intervals
  - `GoldenRatio` - each tap = previous × 1.618 (φ)
  - `Fibonacci` - tap times follow Fibonacci sequence (1, 1, 2, 3, 5, 8...)

- **Extended note patterns** via `loadNotePattern(NoteValue, NoteModifier, tapCount)`:
  - Added `SixtyFourth` (1/64 note) and `DoubleWhole` (breve, 2/1 note) to NoteValue enum
  - All 8 note values: 64th, 32nd, 16th, 8th, quarter, half, whole, double-whole
  - All 3 modifiers: normal (1×), dotted (1.5×), triplet (2/3×)
  - **24 rhythmic pattern combinations** for precise delay timing
  - Example: `taps.loadNotePattern(NoteValue::Eighth, NoteModifier::Dotted, 4)` → 375ms, 750ms, 1125ms, 1500ms at 120 BPM

- **Comprehensive test suite** (44 test cases)
  - All 16 taps enabled simultaneously without dropouts
  - Pattern formula verification for all 5 presets
  - Constant-power pan law verification
  - Feedback runaway prevention verification
  - Out-of-range tap index handling (silently ignored)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Layer 3 architecture**: Composes Layer 0-1 components
  - Uses: `DelayLine` (L1) - shared buffer with 16 read positions
  - Uses: `Biquad` (L1) - per-tap 12dB/oct filtering
  - Uses: `OnePoleSmoother` (L1) - 6 smoothers per tap + 2 master
  - Uses: `NoteValue` (L0), `dbToGain()` (L0), `kGoldenRatio` (L0)
- **Memory efficient**: Single DelayLine shared by all taps
- **Parameter smoothing**: 20ms one-pole smoothing on delay, level, pan, cutoff
- **Constant-power pan**: `gainL = cos(θ)`, `gainR = sin(θ)` where θ = pan mapped to 0-π/2
- **Pattern formulas**: 1-based indexing (n = i + 1) for intuitive tap numbering
- **Constitution compliance**: Principles II, III, IX, X, XII, XIV, XV

### Usage

```cpp
#include "dsp/systems/tap_manager.h"

using namespace Iterum::DSP;

TapManager taps;
taps.prepare(44100.0f, 512, 5000.0f);  // 5 second max delay

// Manual tap configuration
taps.setTapEnabled(0, true);
taps.setTapTimeMs(0, 250.0f);
taps.setTapLevelDb(0, -3.0f);
taps.setTapPan(0, -50.0f);  // Pan left

// Or load a preset pattern
taps.setTempo(120.0f);
taps.loadPattern(TapPattern::GoldenRatio, 8);  // 8 taps

// Or use extended note patterns (24 combinations)
taps.loadNotePattern(NoteValue::Eighth, NoteModifier::Dotted, 4);  // Dotted 8ths
taps.loadNotePattern(NoteValue::SixtyFourth, NoteModifier::None, 16);  // 64th notes

// In audio callback
taps.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

## [0.0.23] - 2025-12-25

### Added

- **Layer 3 System Component: StereoField** (`src/dsp/systems/stereo_field.h`)
  - Manages stereo processing modes for delay effects with comprehensive stereo imaging
  - Complete feature set with 6 user stories:
    - **US1: Stereo Processing Modes**: Mono, Stereo, PingPong, DualMono, MidSide
    - **US2: Width Control**: 0% (mono) to 200% (exaggerated stereo) via M/S processing
    - **US3: Output Panning**: Constant-power pan law (sin/cos) from -100 to +100
    - **US4: L/R Timing Offset**: Haas-style widening with ±50ms timing difference
    - **US5: L/R Delay Ratio**: Polyrhythmic patterns (0.1-10.0 ratio between channels)
    - **US6: Smooth Mode Transitions**: 50ms crossfade prevents clicks when switching
  - Width at 0% produces correlation of 1.0 (perfect mono) (SC-005)
  - Width at 200% doubles the Side component (SC-006)
  - Pan at ±100 achieves 40dB+ channel separation (SC-007)
  - L/R offset accuracy within ±1 sample (SC-008)
  - L/R ratio accuracy within ±1% (SC-009)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN input handling (treated as 0.0)

- **Stereo mode implementations**:
  - `Mono` - Sum L+R, output identical signals to both channels
  - `Stereo` - Independent L/R delays with L/R ratio control
  - `PingPong` - Alternating L/R output with cross-feedback
  - `DualMono` - Same delay time, independent pan control per channel
  - `MidSide` - M/S encode, delay Mid and Side independently, decode back

- **Comprehensive test suite** (37 test cases, 1649 assertions)
  - All 5 stereo modes verified with distinct output characteristics
  - Width control at 0%, 100%, 200% tested
  - Constant-power panning verification
  - L/R offset and ratio accuracy tests
  - Mode transition click-free verification
  - Parameter automation smoothness
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Layer 3 architecture**: Composes Layer 0-2 components
  - Uses: `DelayLine` (L1), `OnePoleSmoother` (L1)
  - Uses: `MidSideProcessor` (L2) for M/S encoding/decoding
  - Uses: `dbToGain()` (L0), `isNaN()` (L0)
- **Delay smoother**: Stores samples (not ms) to avoid double-conversion
- **Mode crossfading**: 50ms linear crossfade for click-free transitions
- **Parameter smoothing**: 20ms one-pole smoothing on all parameters
- **Constant-power pan**: `gainL = cos(pan * PI/2)`, `gainR = sin(pan * PI/2)`
- **L/R ratio**: L delay = base × ratio, R delay = base
- **Constitution compliance**: Principles II, III, IX, X, XII, XIV, XV

### Usage

```cpp
#include "dsp/systems/stereo_field.h"

using namespace Iterum::DSP;

StereoField stereo;
stereo.prepare(44100.0, 512, 2000.0f);

// Wide ping-pong delay
stereo.setMode(StereoMode::PingPong);
stereo.setDelayTimeMs(375.0f);
stereo.setWidth(150.0f);  // Enhanced stereo

// Polyrhythmic delay (3:4 ratio)
stereo.setMode(StereoMode::Stereo);
stereo.setDelayTimeMs(400.0f);  // R = 400ms
stereo.setLRRatio(0.75f);       // L = 300ms

// In audio callback
stereo.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

## [0.0.22] - 2025-12-25

### Added

- **Layer 3 System Component: CharacterProcessor** (`src/dsp/systems/character_processor.h`)
  - Applies analog character/coloration to audio with four distinct modes
  - Complete feature set with 6 user stories:
    - **US1: Tape Mode**: Saturation, wow/flutter modulation, hiss, high-frequency rolloff
    - **US2: BBD Mode**: Bandwidth limiting, clock noise, soft input saturation
    - **US3: Digital Vintage Mode**: Bit depth reduction (4-16 bits), sample rate reduction (1x-8x)
    - **US4: Clean Mode**: Transparent bypass with no processing
    - **US5: Smooth Mode Transitions**: 50ms crossfade prevents clicks when switching modes
    - **US6: Per-Mode Parameter Control**: Independent parameters for each character mode
  - Tape mode: THD controllable from 0.1% to 5% (SC-005)
  - BBD mode: -12dB attenuation at 2x cutoff frequency (SC-006)
  - Digital Vintage mode: 8-bit achieves ~48dB SNR (SC-007)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN input handling (treated as 0.0)

- **Internal character mode implementations**:
  - `TapeCharacter` - Composes SaturationProcessor, LFO (wow/flutter), NoiseGenerator (hiss), MultimodeFilter (rolloff)
  - `BBDCharacter` - Composes SaturationProcessor, NoiseGenerator (clock noise), MultimodeFilter (bandwidth)
  - `DigitalVintageCharacter` - Bit crushing and sample rate reduction with dither

- **Test tolerance audit fixes**:
  - Pitch Shifter SC-001: Fixed false positive - FFT was fooled by AM artifacts, autocorrelation method is correct
  - Oversampler SC-003: Added proper 0.1dB passband flatness test (implementation compliant)
  - Documented frequency estimation helpers in TESTING-GUIDE.md

- **Comprehensive test suite** (all user stories and success criteria)
  - THD measurement and calibration (SC-005)
  - Bandwidth limiting verification (SC-006)
  - SNR measurement for bit depth (SC-007)
  - Mode transition click-free verification (SC-002)
  - Parameter automation smoothness (SC-004)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Layer 3 architecture**: Composes Layer 0-2 components
  - Uses: `SaturationProcessor` (L2), `NoiseGenerator` (L2), `MultimodeFilter` (L2)
  - Uses: `LFO` (L1), `OnePoleSmoother` (L1)
  - Uses: `dbToGain()` (L0), `isNaN()` (L0)
- **Mode crossfading**: Linear crossfade over 50ms for click-free transitions
- **Tape saturation**: tanh-based with configurable drive (0-100%)
- **Wow/flutter**: LFO-modulated pitch shift (0.1-10Hz, 0-100% depth)
- **BBD bandwidth**: Lowpass filter with configurable cutoff (2-15kHz)
- **Bit crushing formula**: `output = floor(input * levels + 0.5) / levels` where levels = 2^bits
- **Sample rate reduction**: Zero-order hold with optional TPDF dither
- **Constitution compliance**: Principles II, III, IX, X, XII, XIV, XV

### Usage

```cpp
#include "dsp/systems/character_processor.h"

using namespace Iterum::DSP;

CharacterProcessor character;

// In prepare() - allocates buffers
character.prepare(44100.0, 512);

// Select character mode
character.setMode(CharacterMode::Tape);

// Configure Tape mode
character.setTapeSaturation(50.0f);     // 50% saturation
character.setTapeWowFlutterRate(2.0f);  // 2Hz modulation
character.setTapeWowFlutterDepth(30.0f); // 30% depth
character.setTapeHissLevel(-60.0f);     // -60dB hiss
character.setTapeRolloff(8000.0f);      // 8kHz rolloff

// BBD mode
character.setMode(CharacterMode::BBD);
character.setBBDBandwidth(8000.0f);     // 8kHz bandwidth
character.setBBDClockNoiseLevel(-70.0f); // -70dB clock noise
character.setBBDDrive(40.0f);           // 40% input saturation

// Digital Vintage mode
character.setMode(CharacterMode::DigitalVintage);
character.setDigitalBitDepth(8);        // 8-bit (classic lo-fi)
character.setDigitalSampleRateReduction(4); // 4x downsampling

// Clean mode for A/B comparison
character.setMode(CharacterMode::Clean);

// In processBlock() - real-time safe
character.process(buffer, numSamples);

// Stereo processing
character.process(leftBuffer, rightBuffer, numSamples);
```

---

## [0.0.21] - 2025-12-25

### Added

- **Layer 3 System Component: ModulationMatrix** (`src/dsp/systems/modulation_matrix.h`)
  - Routes modulation sources (LFO, EnvelopeFollower) to parameter destinations with depth control
  - Complete feature set with 6 user stories:
    - **US1: Route LFO to Delay Time**: Basic source-to-destination routing with depth control
    - **US2: Multiple Routes to Same Destination**: Sum contributions from multiple sources
    - **US3: Unipolar Modulation Mode**: Map [-1,+1] sources to [0,1] for parameters like gain
    - **US4: Smooth Depth Changes**: 20ms smoothing prevents zipper noise on depth automation
    - **US5: Enable/Disable Routes**: A/B comparison without removing route configuration
    - **US6: Query Applied Modulation**: UI feedback via `getCurrentModulation()`
  - Supports 16 sources, 16 destinations, 32 routes maximum
  - Bipolar mode: source [-1,+1] → ±50% of destination range at depth=1.0
  - Unipolar mode: source [-1,+1] → 0-50% of destination range at depth=1.0
  - NaN source values treated as 0.0 (FR-018)
  - Real-time safe: `noexcept`, no allocations in `process()`

- **ModulationSource Interface** (`src/dsp/systems/modulation_matrix.h`)
  - Abstract base class for modulation providers
  - `getCurrentValue()`: Returns current modulation output
  - `getSourceRange()`: Returns min/max output range
  - Implemented by LFO, EnvelopeFollower, or custom sources

- **Comprehensive test suite** (40 test cases, 243 assertions)
  - All 6 user stories covered with acceptance scenarios
  - Edge cases: NaN handling, depth clamping, route limits
  - Performance verification: 16 routes process efficiently
  - Real-time safety: `static_assert(noexcept(process()))`

### Technical Details

- **Layer 3 architecture**: Composes Layer 0-1 components
  - Uses: `OnePoleSmoother` (L1), `detail::isNaN()` (L0)
- **Modulation signal flow**:
  1. Poll each source via `getCurrentValue()`
  2. Apply mode conversion (bipolar passthrough, unipolar: (x+1)/2)
  3. Multiply by smoothed depth and destination half-range
  4. Accumulate to destination modulation sum
  5. `getModulatedValue()` adds sum to base value, clamps to range
- **Depth smoothing**: 20ms one-pole filter per route
- **Constitution compliance**: Principles II, III, IX, X, XII, XV

### Usage

```cpp
#include "dsp/systems/modulation_matrix.h"
#include "dsp/primitives/lfo.h"

using namespace Iterum::DSP;

// Wrap LFO as ModulationSource
class LFOModSource : public ModulationSource {
    LFO& lfo_;
public:
    explicit LFOModSource(LFO& lfo) : lfo_(lfo) {}
    float getCurrentValue() const noexcept override { return lfo_.getCurrentValue(); }
    std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }
};

ModulationMatrix matrix;
matrix.prepare(44100.0, 512, 32);

LFO lfo;
lfo.prepare(44100.0);
lfo.setRate(2.0f);
LFOModSource lfoSource(lfo);

// Register source and destination
matrix.registerSource(0, &lfoSource);
matrix.registerDestination(0, 0.0f, 100.0f, "Delay Time");

// Create route: LFO → Delay Time, 50% depth, bipolar
matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

// In audio callback
lfo.process();
matrix.process(numSamples);
float delayMs = matrix.getModulatedValue(0, 50.0f);  // 50 ± 25ms
```

## [0.0.20] - 2025-12-25

### Added

- **Layer 3 System Component: FeedbackNetwork** (`src/dsp/systems/feedback_network.h`)
  - Manages feedback loops for delay effects with filtering, saturation, and cross-feedback routing
  - Complete feature set with 6 user stories:
    - **US1: Basic Feedback Loop**: Adjustable feedback amount (0-100%) with smooth parameter changes
    - **US2: Self-Oscillation Mode**: 120% feedback with saturation for controlled runaway
    - **US3: Filtered Feedback**: LP/HP/BP filter in feedback path for tone shaping
    - **US4: Saturated Feedback**: 5 saturation types (Tape, Tube, Transistor, Digital, Diode)
    - **US5: Freeze Mode**: Infinite sustain with input muting
    - **US6: Stereo Cross-Feedback**: Ping-pong delays via cross-channel routing
  - 20ms parameter smoothing for click-free transitions
  - NaN rejection on all parameters
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Layer 0 Utility: stereoCrossBlend** (`src/dsp/core/stereo_utils.h`)
  - Stereo channel cross-blending for ping-pong delays
  - `crossAmount = 0.0`: Normal stereo (L→L, R→R)
  - `crossAmount = 0.5`: Mono blend ((L+R)/2)
  - `crossAmount = 1.0`: Full swap / ping-pong (L→R, R→L)
  - `constexpr` and `noexcept` for real-time safety

- **Performance Benchmark** (`tests/benchmark_feedback_network.cpp`)
  - Verifies <1% CPU at 44.1kHz stereo (SC-007)
  - Results: 0.45% CPU stereo, 0.25% CPU mono

- **Comprehensive test suite** (51 test cases, 158 assertions for FeedbackNetwork)
  - All 6 user stories covered
  - Edge cases: NaN handling, parameter clamping, click-free transitions
  - Real-time safety verification

### Technical Details

- **Layer 3 architecture**: Composes Layer 0-2 components
  - Uses: `DelayLine` (L1), `OnePoleSmoother` (L1), `MultimodeFilter` (L2), `SaturationProcessor` (L2)
  - Uses: `BlockContext` (L0), `stereoCrossBlend` (L0)
- **Feedback path signal flow**:
  1. Read delayed sample (read-before-write pattern)
  2. Apply filter (if enabled)
  3. Apply saturation (if enabled)
  4. Apply cross-feedback blend (stereo only)
  5. Scale by feedback amount
  6. Mix with input and write to delay line
- **Freeze mode implementation**:
  - Stores pre-freeze feedback amount
  - Sets feedback to 100%, mutes input
  - Restores original feedback on unfreeze
- **Constitution compliance**: Principles II, III, IX, X, XI, XII, XIII, XV
- **ARCHITECTURE.md**: Updated with stereoCrossBlend documentation in Layer 0 section

### Usage

```cpp
#include "dsp/systems/feedback_network.h"

using namespace Iterum::DSP;

FeedbackNetwork network;

// In prepare() - allocates buffers
network.prepare(44100.0, 512, 2000.0f);  // 2 second max delay

// Basic feedback
network.setDelayTimeMs(500.0f);
network.setFeedbackAmount(0.7f);  // 70% feedback

// Self-oscillation with saturation
network.setFeedbackAmount(1.2f);  // 120% feedback
network.setSaturationEnabled(true);
network.setSaturationType(SaturationType::Tape);

// Filtered feedback (darkening repeats)
network.setFilterEnabled(true);
network.setFilterType(FilterType::Lowpass);
network.setFilterCutoff(2000.0f);

// Freeze mode
network.setFreeze(true);  // Infinite sustain

// Ping-pong delay
network.setCrossFeedbackAmount(1.0f);  // Full L↔R swap

// In processBlock() - real-time safe
BlockContext ctx;
ctx.sampleRate = 44100.0;
network.process(left, right, numSamples, ctx);
```

---

## [0.0.19] - 2025-12-25

### Added

- **Layer 3 System Component: DelayEngine** (`src/dsp/systems/delay_engine.h`)
  - High-level delay wrapper composing Layer 1 primitives (DelayLine, OnePoleSmoother)
  - Complete feature set with 4 user stories:
    - **Free Mode** (US1): Delay time in milliseconds with smooth transitions
    - **Synced Mode** (US2): Tempo-synced via BlockContext and NoteValue
    - **Mix Control** (US3): Dry/wet blend with kill-dry option for parallel processing
    - **Lifecycle** (US4): Stereo support, prepare/reset, query methods
  - TimeMode enum: `Free` (milliseconds) or `Synced` (tempo-based)
  - 20ms parameter smoothing for click-free transitions (FR-004)
  - Linear interpolation via `readLinear()` for sub-sample accuracy (FR-012)
  - NaN rejection: Invalid values keep previous delay time (FR-011)
  - Clamping: Delay time clamped to [0, maxDelayMs] (FR-010)
  - Kill-dry mode: Outputs only wet signal for parallel delay chains (FR-006)
  - Stereo processing with independent left/right delay lines
  - Write-before-read order enables true 0-sample delay
  - Real-time safe: `noexcept`, no allocations in `process()`
  - Zero latency (no lookahead)

- **Comprehensive test suite** (58 test cases, 5,935 assertions)
  - US1: Free mode timing, smoothing, clamping, NaN handling
  - US2: All NoteValue types at various tempos (120, 100, 140, 60 BPM)
  - US3: Mix control (0%, 50%, 100%), kill-dry, smooth transitions
  - US4: isPrepared(), getMaxDelayMs(), stereo processing, variable block sizes
  - Edge cases: 0ms delay, negative delay, infinity, tempo=0 (clamps to 20 BPM)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Layer 3 architecture**: First Layer 3 system component in codebase
  - Composes: `DelayLine` (L1), `OnePoleSmoother` (L1)
  - Uses: `BlockContext` (L0), `NoteValue` (L0)
- **Delay time calculation**:
  - Free mode: Direct milliseconds → samples conversion
  - Synced mode: `ctx.tempoToSamples(noteValue, modifier)` → milliseconds
- **Write-before-read order**: Enables true 0-sample delay behavior
  - `delayLine_.write(dry)` before `delayLine_.readLinear(delaySamples)`
- **Mix formula**:
  - `dryCoeff = killDry ? 0.0f : (1.0f - mix)`
  - `wetCoeff = mix`
  - `output = dry * dryCoeff + wet * wetCoeff`
- **Tempo clamping**: Minimum 20 BPM prevents infinite delay times
- **Namespace**: `Iterum::DSP` (Layer 3 system components)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIII (Architecture Docs), XV (Honest Completion)

### Usage

```cpp
#include "dsp/systems/delay_engine.h"

using namespace Iterum::DSP;

DelayEngine delay;

// In prepare() - allocates buffers
delay.prepare(44100.0, 512, 2000.0f);  // 2 second max delay

// Free mode: delay in milliseconds
delay.setTimeMode(TimeMode::Free);
delay.setDelayTimeMs(250.0f);  // 250ms delay
delay.setMix(0.5f);            // 50% wet

// Synced mode: delay from tempo
delay.setTimeMode(TimeMode::Synced);
delay.setNoteValue(NoteValue::Quarter, NoteModifier::Dotted);

// Kill-dry for parallel processing
delay.setKillDry(true);  // Output = 100% wet

// In processBlock() - real-time safe
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.tempoBPM = 120.0;
delay.process(buffer, numSamples, ctx);

// Stereo processing
delay.process(leftBuffer, rightBuffer, numSamples, ctx);

// Query current state
float currentMs = delay.getCurrentDelayMs();
bool ready = delay.isPrepared();
TimeMode mode = delay.getTimeMode();
```

---

## [0.0.18] - 2025-12-24

### Added

- **Layer 0 Core Utility: NoteValue** (`src/dsp/core/note_value.h`)
  - Musical note duration enums for tempo-synced features
  - `NoteValue` enum: Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond
  - `NoteModifier` enum: None, Dotted, Triplet
  - `getBeatsForNote()` constexpr function with modifier support
  - `kBeatsPerNote` compile-time lookup array

- **Layer 0 Core Utility: BlockContext** (`src/dsp/core/block_context.h`)
  - Per-block processing context for tempo-synced DSP
  - Sample rate, block size, tempo (BPM), time signature, transport state
  - `tempoToSamples()` constexpr function for note-to-samples conversion
  - Default-constructible with sensible audio defaults (44100 Hz, 512 samples, 120 BPM)

- **Layer 0 Core Utility: FastMath** (`src/dsp/core/fast_math.h`)
  - `fastTanh()` - Padé (5,4) approximation, **~3x faster than std::tanh**
  - Accuracy: <0.05% error for |x| < 3.5, exact saturation to ±1 for larger values
  - Ideal for saturation/waveshaping in feedback loops and CPU-critical paths
  - constexpr, noexcept, real-time safe
  - NaN → NaN, ±Infinity → ±1.0 (graceful edge case handling)

- **Layer 0 Core Utility: Interpolation** (`src/dsp/core/interpolation.h`)
  - Standalone interpolation functions for fractional sample reading
  - `linearInterpolate()` - 2-point linear (fast, good for modulated delay)
  - `cubicHermiteInterpolate()` - 4-point cubic with C1 continuity (pitch shifting)
  - `lagrangeInterpolate()` - 4-point 3rd-order polynomial (lowest aliasing)
  - All constexpr, noexcept, exact at sample boundaries (t=0 or t=1)

- **Performance benchmark** (`tests/benchmark_tanh.cpp`)
  - Verified SC-001: fastTanh is 3x faster than std::tanh
  - Uses volatile function pointers to prevent over-optimization

- **Comprehensive test suite** (51 test cases for Layer 0 utilities)
  - NoteValue: 11 test cases (beat calculations, modifiers)
  - BlockContext: 11 test cases (tempo-to-samples conversions)
  - FastMath: 10 test cases (accuracy, NaN/infinity, constexpr, noexcept)
  - Interpolation: 18 test cases (polynomial exactness, boundary conditions)

### Changed

- **FastMath scope reduction** (User approved):
  - Removed `fastSin`, `fastCos`, `fastExp` - benchmarking showed MSVC's std:: versions are faster (SIMD/lookup tables)
  - Only `fastTanh` provides meaningful performance improvement
  - Recommendation: Use `std::sin/cos/exp` for runtime, `fastTanh` for saturation

### Technical Details

- **fastTanh algorithm**: Padé (5,4) approximation
  - Formula: `x * (945 + 105*x² + x⁴) / (945 + 420*x² + 15*x⁴)`
  - Saturation threshold: |x| >= 3.5 returns ±1 (avoids polynomial overshoot)
- **Benchmark results** (Windows/MSVC Release, 1M samples × 10 iterations):
  - fastTanh: ~28,000 μs vs std::tanh: ~91,000 μs = **3.2x speedup**
- **Dependencies**: Layer 0 only (db_utils.h for isNaN/isInf)
- **Namespace**: `Iterum::DSP` (NoteValue, BlockContext), `Iterum::DSP::FastMath`, `Iterum::DSP::Interpolation`
- **Constitution compliance**: Principles II, III, IX, XII, XIII, XV

### Usage

```cpp
#include "dsp/core/block_context.h"
#include "dsp/core/fast_math.h"
#include "dsp/core/interpolation.h"

using namespace Iterum::DSP;
using namespace Iterum::DSP::FastMath;
using namespace Iterum::DSP::Interpolation;

// Tempo-synced delay
BlockContext ctx;
ctx.tempoBPM = 120.0;
ctx.sampleRate = 48000.0;
double delaySamples = ctx.tempoToSamples(NoteValue::Quarter);  // 24000

// Fast saturation in feedback loop
for (size_t i = 0; i < numSamples; ++i) {
    feedback[i] = fastTanh(feedback[i] * drive);  // 3x faster
}

// Fractional delay reading
float sample = cubicHermiteInterpolate(y[-1], y[0], y[1], y[2], frac);
```

---

## [0.0.17] - 2025-12-24

### Added

- **Layer 2 DSP Processor: PitchShiftProcessor** (`src/dsp/processors/pitch_shift_processor.h`)
  - Complete pitch shifting processor for transposing audio without changing duration
  - Three quality modes with different latency/quality trade-offs:
    - `Simple` - Zero latency, delay-line modulation with dual crossfade (audible artifacts)
    - `Granular` - ~46ms latency, Hann window crossfades, good quality for general use
    - `PhaseVocoder` - ~116ms latency, STFT-based with phase coherence, excellent quality
  - Pitch range: ±24 semitones (4 octaves) with fine-tuning via cents (±100)
  - **Formant Preservation** using cepstral envelope extraction:
    - Prevents "chipmunk" effect when shifting vocals
    - Available in PhaseVocoder mode (requires spectral access)
    - Quefrency cutoff: 1.5ms default (suitable for vocals up to ~666Hz F0)
  - Real-time parameter automation with click-free transitions
  - Stable in feedback configurations (verified 1000 iterations at 80% feedback)
  - In-place processing support (input == output buffers)
  - Sample rates 44.1kHz to 192kHz supported
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Internal pitch shifter implementations**:
  - `SimplePitchShifter` - Dual delay-line with half-sine crossfade, Doppler-based pitch shift
  - `GranularPitchShifter` - Hann window crossfades, longer crossfade region (33%)
  - `PhaseVocoderPitchShifter` - STFT with phase vocoder algorithm, instantaneous frequency tracking
  - `FormantPreserver` - Cepstral low-pass liftering for spectral envelope extraction

- **Utility functions**:
  - `pitchRatioFromSemitones(float semitones)` - Convert semitones to pitch ratio (2^(s/12))
  - `semitonesFromPitchRatio(float ratio)` - Convert pitch ratio to semitones (12*log2(r))

- **Comprehensive test suite** (2,815 assertions across 42 test cases)
  - US1: Basic pitch shifting (440Hz → 880Hz/220Hz verification)
  - US2: Quality mode latency verification (0/~2029/~5120 samples)
  - US3: Cents fine control with combined semitone+cents
  - US4: Formant preservation with cepstral envelope extraction
  - US5: Feedback path stability (SC-008: 1000 iterations at 80%)
  - US6: Real-time parameter automation sweeps
  - Edge cases: Extreme values (±24st), NaN/infinity handling, sample rate changes

### Technical Details

- **Pitch shift physics**: ω_out = ω_in × (1 - dDelay/dt) (Doppler effect)
  - Pitch up (ratio > 1): delay DECREASES at rate (ratio - 1) samples/sample
  - Pitch down (ratio < 1): delay INCREASES at rate (1 - ratio) samples/sample
- **Simple mode crossfade**: Half-sine for constant power, 25% of delay range
- **Granular mode crossfade**: Hann window for smoother transitions, 33% of delay range
- **PhaseVocoder algorithm**:
  - FFT size: 4096 samples (~93ms at 44.1kHz)
  - Hop size: 1024 samples (25% overlap, 4x redundancy)
  - Instantaneous frequency from phase difference + expected phase increment
  - Spectrum scaling with phase accumulation for coherence
- **Formant preservation**:
  - Log magnitude spectrum → IFFT → real cepstrum
  - Hann lifter window at quefrency cutoff → low-pass in cepstral domain
  - FFT → exp → spectral envelope
  - Apply envelope ratio: output = shifted × (originalEnv / shiftedEnv)
- **Dependencies** (Layer 0-1 primitives):
  - `DelayLine` - Simple mode delay buffer
  - `STFT` / `FFT` - PhaseVocoder spectral analysis
  - `SpectralBuffer` - Phase manipulation storage
  - `OnePoleSmoother` - Parameter smoothing
  - `WindowFunctions` - Grain windowing (Hann)
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIII (Architecture Docs), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/pitch_shift_processor.h"

using namespace Iterum::DSP;

PitchShiftProcessor shifter;

// In prepare() - allocates all buffers
shifter.prepare(44100.0, 512);

// Select quality mode
shifter.setMode(PitchMode::Granular);  // Good balance of quality/latency

// Set pitch shift
shifter.setSemitones(7.0f);   // Perfect fifth up
shifter.setCents(0.0f);       // No fine tuning

// Enable formant preservation for vocals (PhaseVocoder mode only)
shifter.setMode(PitchMode::PhaseVocoder);
shifter.setFormantPreserve(true);

// In processBlock() - real-time safe
shifter.process(input, output, numSamples);

// In-place processing also supported
shifter.process(buffer, buffer, numSamples);

// Query latency for host delay compensation
size_t latency = shifter.getLatencySamples();

// Zero-latency monitoring mode
shifter.setMode(PitchMode::Simple);  // 0 samples latency
shifter.setSemitones(0.0f);          // Pass-through for comparison

// Shimmer delay feedback loop
shifter.setMode(PitchMode::Simple);
shifter.setSemitones(12.0f);  // Octave up
// Route output back to input with feedback gain < 1.0
```

---

## [0.0.16] - 2025-12-24

### Added

- **Layer 2 DSP Processor: DiffusionNetwork** (`src/dsp/processors/diffusion_network.h`)
  - 8-stage Schroeder allpass diffusion network for reverb-like temporal smearing
  - Creates smeared, ambient textures by cascading allpass filters with irrational delay ratios
  - Complete diffusion processing with 6 user stories:
    - **Basic Diffusion**: Spreads transient energy over time while preserving frequency spectrum
    - **Size Control** [0-100%]: Scales all delay times proportionally
      - 0% = bypass (no diffusion)
      - 50% = ~28ms spread
      - 100% = ~57ms spread (maximum diffusion)
    - **Density Control** [0-100%]: Number of active stages
      - 25% = 2 stages, 50% = 4 stages, 75% = 6 stages, 100% = 8 stages
    - **Modulation** [0-100% depth, 0.1-5Hz rate]: LFO on delay times for chorus-like movement
      - Per-stage phase offsets (45°) for decorrelated modulation
    - **Stereo Width** [0-100%]: Controls L/R decorrelation
      - 0% = mono (L=R), 100% = full stereo decorrelation
    - **Real-time Safety**: All methods noexcept, no allocations in process()
  - Irrational delay ratios: {1.0, 1.127, 1.414, 1.732, 2.236, 2.828, 3.317, 4.123}
  - Golden ratio allpass coefficient (g = 0.618) for optimal diffusion
  - First-order allpass interpolation for energy-preserving fractional delays
  - Single-delay-line Schroeder formulation for efficiency
  - 10ms parameter smoothing on all controls (no zipper noise)
  - In-place processing support (input == output buffers)
  - Block sizes 1-8192 samples supported
  - Sample rates 44.1kHz to 192kHz supported

- **Comprehensive test suite** (46 test cases with 1,139 assertions)
  - US1: Basic diffusion processing with energy preservation
  - US2: Size control with bypass and spread verification
  - US3: Density control with stage count mapping
  - US4: Modulation with per-stage phase offsets
  - US5: Stereo width with mono/decorrelation verification
  - US6: Real-time safety (noexcept, block sizes, in-place)
  - Edge cases: NaN/Infinity input, sample rate changes, extreme parameters

### Technical Details

- **Schroeder allpass formula**:
  - `v[n] = x[n] + g * v[n-D]`
  - `y[n] = -g * v[n] + v[n-D]`
  - Where g = 0.618 (golden ratio inverse)
- **Stereo decorrelation**: Right channel delays multiplied by 1.127 offset
- **Modulation**: Single LFO with per-stage phase offsets (i × 45°)
- **Density**: Smooth crossfade for stage enable/disable transitions
- **Allpass interpolation**: Unity magnitude at all frequencies (energy-preserving)
- **Dependencies** (Layer 1 primitives):
  - `DelayLine` - Variable delay with allpass interpolation
  - `OnePoleSmoother` - Parameter smoothing
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II, III, IX, X, XII, XV

### Usage

```cpp
#include "dsp/processors/diffusion_network.h"

using namespace Iterum::DSP;

DiffusionNetwork diffuser;

// In prepare() - allocates delay buffers
diffuser.prepare(44100.0f, 512);

// Configure diffusion
diffuser.setSize(60.0f);       // 60% diffusion size
diffuser.setDensity(100.0f);   // All 8 stages
diffuser.setWidth(100.0f);     // Full stereo
diffuser.setModDepth(25.0f);   // Subtle movement
diffuser.setModRate(1.0f);     // 1 Hz LFO

// In processBlock() - real-time safe
diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);

// As reverb pre-diffuser (no modulation for cleaner tail)
diffuser.setModDepth(0.0f);
diffuser.setSize(80.0f);
diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

---

## [0.0.15] - 2025-12-24

### Added

- **Layer 2 DSP Processor: MidSideProcessor** (`src/dsp/processors/midside_processor.h`)
  - Stereo Mid/Side encoder, decoder, and manipulator for stereo field control
  - Complete M/S processing with 6 user stories:
    - **Encoding**: Mid = (L + R) / 2, Side = (L - R) / 2
    - **Decoding**: L = Mid + Side, R = Mid - Side
    - **Width control** [0-200%] via Side channel scaling
      - 0% = mono (Side removed)
      - 100% = unity (original stereo image)
      - 200% = maximum width (Side doubled)
    - **Independent Mid gain** [-96dB, +24dB] with dB-to-linear conversion
    - **Independent Side gain** [-96dB, +24dB] with dB-to-linear conversion
    - **Solo modes** for Mid and Side monitoring (soloMid takes precedence)
  - Perfect reconstruction at unity settings (roundtrip < 1e-6 error)
  - Mono input handling: L=R produces zero Side component exactly
  - 5 OnePoleSmoother instances for click-free parameter transitions:
    - Width (0.0-2.0 factor), Mid gain (linear), Side gain (linear)
    - Solo Mid (0.0-1.0), Solo Side (0.0-1.0)
  - Smooth crossfade for solo mode transitions (prevents clicks)
  - Block processing with in-place support (leftIn == leftOut OK)
  - Block sizes 1-8192 samples supported
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling with graceful degradation

- **Comprehensive test suite** (22,910 assertions across 32 test cases)
  - US1: Basic M/S encoding/decoding with roundtrip verification
  - US2: Width control (0%, 100%, 200%) with clamping
  - US3: Independent Mid/Side gain with dB conversion
  - US4: Solo modes with precedence and smooth transitions
  - US5: Mono input handling (no phantom stereo)
  - US6: Real-time safety (block sizes, noexcept verification)
  - Edge cases: NaN, Infinity, DC offset, sample rate changes

### Technical Details

- **M/S formulas**:
  - Encode: `Mid = (L + R) * 0.5f`, `Side = (L - R) * 0.5f`
  - Width: `Side *= widthFactor` where factor = percent / 100
  - Solo crossfade: `side *= (1.0f - soloMidFade)`, `mid *= (1.0f - soloSideFade)`
  - Decode: `L = Mid + Side`, `R = Mid - Side`
- **Parameter smoothing**: 10ms one-pole smoothing (configurable)
- **Gain conversion**: Uses `dbToGain()` from Layer 0 core utilities
- **Solo precedence**: When both solos enabled, soloMid crossfade applied first
- **Dependencies** (Layer 0-1 primitives):
  - `OnePoleSmoother` - Click-free parameter transitions
  - `dbToGain()` - dB to linear gain conversion
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/midside_processor.h"

using namespace Iterum::DSP;

MidSideProcessor ms;

// In prepare() - configures smoothers
ms.prepare(44100.0f, 512);

// Widen stereo image
ms.setWidth(150.0f);  // 150% width

// Boost mid, cut side (for vocal clarity)
ms.setMidGain(3.0f);   // +3 dB
ms.setSideGain(-6.0f); // -6 dB

// Monitor side channel only
ms.setSoloSide(true);

// In processBlock() - real-time safe, in-place OK
ms.process(leftIn, rightIn, leftOut, rightOut, numSamples);

// For mono collapse
ms.setWidth(0.0f);  // L = R = Mid

// For extreme stereo enhancement
ms.setWidth(200.0f);  // Side doubled
```

---

## [0.0.14] - 2025-12-24

### Added

- **Layer 2 DSP Processor: NoiseGenerator** (`src/dsp/processors/noise_generator.h`)
  - Comprehensive noise generator for analog character and lo-fi effects
  - 13 noise types with distinct spectral and temporal characteristics:
    - `White` - Flat spectrum white noise (Xorshift32 PRNG)
    - `Pink` - -3dB/octave rolloff (Paul Kellet 7-state filter)
    - `TapeHiss` - Signal-modulated high-frequency noise (EnvelopeFollower)
    - `VinylCrackle` - Poisson-distributed clicks with exponential amplitudes
    - `Asperity` - Signal-dependent tape head noise
    - `Brown` - -6dB/octave 1/f² noise (leaky integrator)
    - `Blue` - +3dB/octave noise (differentiated pink)
    - `Violet` - +6dB/octave noise (differentiated white)
    - `Grey` - Inverse A-weighting for perceptually flat loudness (LowShelf filter)
    - `Velvet` - Sparse random impulses (configurable density 100-20000 imp/sec)
    - `VinylRumble` - Low-frequency motor noise concentrated <100Hz
    - `ModulationNoise` - Signal-correlated noise with no floor (correlation >0.8)
    - `RadioStatic` - Band-limited atmospheric noise (~5kHz AM bandwidth)
  - Per-type independent level control [-96, 0 dB] with 5ms smoothing
  - Per-type enable/disable for efficient processing
  - Master level control [-96, 0 dB]
  - Two-input processing API: `process(inputBuffer, outputBuffer, numSamples)`
  - Single-input API for additive-only modes: `process(buffer, numSamples)`
  - Sample-by-sample generation via `generateNoiseSample(inputSample)`
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling with graceful degradation

- **Configurable noise parameters**:
  - `setNoiseLevel(type, levelDb)` - Per-type level control
  - `setNoiseEnabled(type, enabled)` - Per-type enable/disable
  - `setCrackleParams(density, surfaceNoiseDb)` - Vinyl crackle configuration
  - `setVelvetDensity(density)` - Velvet impulse rate [100-20000 imp/sec]
  - `setMasterLevel(levelDb)` - Overall output level

- **Comprehensive test suite** (943,618 assertions across 91 test cases)
  - Phase 1: US1-US6 (White, Pink, TapeHiss, VinylCrackle, Asperity, Multi-mix)
  - Phase 2: US7-US15 (Brown, Blue, Violet, Grey, Velvet, Rumble, Modulation, Radio)
  - Spectral slope verification (±1dB tolerance for colored noise)
  - Signal-dependent modulation correlation tests
  - Energy concentration tests (>90% below 100Hz for rumble)
  - Impulse density verification
  - Click-free level transitions (SC-004 verification)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Noise generation algorithms**:
  - White: Xorshift32 PRNG → uniform distribution → [-1, +1] mapping
  - Pink: Paul Kellet 7-state filter (b0-b6 coefficients)
  - Brown: `y = leak * y + white; leak = 0.98` (leaky integrator)
  - Blue: `y = pink[n] - pink[n-1]` (first-order differentiator)
  - Violet: `y = white[n] - white[n-1]` (first-order differentiator)
  - Grey: White noise → LowShelf +12dB @ 200Hz
  - Velvet: `if (random < density/sampleRate) output ±1.0`
  - Vinyl: Poisson clicks + exponential amplitudes + surface noise
  - Rumble: Leaky integrator → 80Hz lowpass
  - Modulation: EnvelopeFollower(input) × white noise (no floor)
  - Radio: White noise → 5kHz lowpass

- **Spectral characteristics**:
  - White: Flat ±3dB across 20Hz-20kHz
  - Pink: -3dB/octave ±1dB slope
  - Brown: -6dB/octave ±1dB slope
  - Blue: +3dB/octave ±1dB slope
  - Violet: +6dB/octave ±1dB slope

- **Dependencies** (Layer 0-1 primitives):
  - `Biquad` - Filtering for colored noise and rumble
  - `OnePoleSmoother` - Click-free level transitions
  - `EnvelopeFollower` - Signal modulation for TapeHiss, Asperity, Modulation
  - `dbToGain()` / `gainToDb()` - dB/linear conversion
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIII (Architecture Docs), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/noise_generator.h"

using namespace Iterum::DSP;

NoiseGenerator noise;

// In prepare() - configures internal components
noise.prepare(44100.0, 512);

// Enable noise types
noise.setNoiseEnabled(NoiseType::White, true);
noise.setNoiseEnabled(NoiseType::Pink, true);
noise.setNoiseEnabled(NoiseType::VinylCrackle, true);

// Set levels
noise.setNoiseLevel(NoiseType::White, -20.0f);      // -20 dB
noise.setNoiseLevel(NoiseType::Pink, -24.0f);       // -24 dB
noise.setNoiseLevel(NoiseType::VinylCrackle, -30.0f);

// Configure vinyl crackle
noise.setCrackleParams(5.0f, -40.0f);  // 5 clicks/sec, -40dB surface

// Configure velvet noise
noise.setNoiseEnabled(NoiseType::Velvet, true);
noise.setVelvetDensity(2000.0f);  // 2000 impulses/sec

// In processBlock() - additive noise (no input signal)
noise.process(outputBuffer, numSamples);

// Or with input signal (for signal-dependent noise types)
noise.process(inputBuffer, outputBuffer, numSamples);

// For modulation noise - correlates with input level
noise.setNoiseEnabled(NoiseType::ModulationNoise, true);
noise.setNoiseLevel(NoiseType::ModulationNoise, -12.0f);
noise.process(inputSignal, outputBuffer, numSamples);
```

---

## [0.0.13] - 2025-12-23

### Added

- **Layer 2 DSP Processor: DuckingProcessor** (`src/dsp/processors/ducking_processor.h`)
  - Sidechain-triggered gain reduction for ducking audio based on external signal level
  - Complete feature set with 6 user stories:
    - Threshold control [-60, 0 dB] with configurable ducking trigger level
    - Depth control [0, -48 dB] for maximum attenuation amount
    - Attack time [0.1-500ms] via EnvelopeFollower
    - Release time [1-5000ms] with smooth recovery
    - Hold time [0-1000ms] with 3-state machine (Idle → Ducking → Holding)
    - Range limit [0, -48 dB] to cap maximum attenuation (0 dB = disabled)
  - Optional sidechain highpass filter [20-500Hz] to ignore bass content in trigger
  - Dual-input processing API: `processSample(main, sidechain)`
  - Block processing with separate or in-place output buffers
  - Gain reduction metering via `getCurrentGainReduction()` (negative dB)
  - Zero latency (`getLatency()` returns 0)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling with graceful degradation

- **Comprehensive test suite** (1,493 assertions across 37 test cases)
  - US1: Basic ducking with threshold/depth (8 tests)
  - US2: Attack/release timing (6 tests)
  - US3: Hold time behavior with state machine (5 tests)
  - US4: Range/maximum attenuation limiting (4 tests)
  - US5: Sidechain highpass filter (5 tests)
  - US6: Gain reduction metering accuracy (4 tests)
  - Click-free transitions (SC-004 verification)
  - Edge cases (NaN, Inf, silent sidechain)

### Technical Details

- **State machine** for hold time behavior:
  ```
  Idle ──(sidechain > threshold)──► Ducking
    ▲                                  │
    │  hold expired                    │ sidechain < threshold
    │                                  ▼
    └──────────────────────────── Holding
  ```
- **Gain reduction formula**:
  - `overshoot = envelopeDb - thresholdDb`
  - `factor = clamp(overshoot / 10.0, 0.0, 1.0)` (full depth at 10dB overshoot)
  - `targetGR = depth * factor`
  - `actualGR = max(targetGR, range)` (range limits maximum attenuation)
- **Peak GR tracking**: Stores deepest gain reduction during Ducking state for stable Hold level
- **Dependencies** (Layer 1-2 primitives):
  - `EnvelopeFollower` - Sidechain level detection (peer Layer 2)
  - `OnePoleSmoother` - Click-free gain reduction smoothing
  - `Biquad` - Sidechain highpass filter
  - `dbToGain()` / `gainToDb()` - dB/linear conversion
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIII (Architecture Docs), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/ducking_processor.h"

using namespace Iterum::DSP;

DuckingProcessor ducker;

// In prepare() - configures internal components
ducker.prepare(44100.0, 512);
ducker.setThreshold(-30.0f);   // Duck when sidechain > -30 dB
ducker.setDepth(-12.0f);       // -12 dB maximum attenuation
ducker.setAttackTime(10.0f);   // 10ms attack
ducker.setReleaseTime(200.0f); // 200ms release
ducker.setHoldTime(100.0f);    // 100ms hold before release

// Enable sidechain HPF to focus on voice, ignore bass
ducker.setSidechainFilterEnabled(true);
ducker.setSidechainFilterCutoff(150.0f);

// In processBlock() - real-time safe
// mainBuffer = music, sidechainBuffer = voice
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = ducker.processSample(mainBuffer[i], sidechainBuffer[i]);
}

// Or block processing
ducker.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);

// For metering UI
float grDb = ducker.getCurrentGainReduction();  // e.g., -8.5
```

---

## [0.0.12] - 2025-12-23

### Added

- **Layer 2 DSP Processor: DynamicsProcessor** (`src/dsp/processors/dynamics_processor.h`)
  - Real-time compressor/limiter composing EnvelopeFollower, OnePoleSmoother, DelayLine, and Biquad
  - Full compressor feature set with 8 user stories:
    - Threshold control [-60, 0 dB] with hard/soft knee transition
    - Ratio control [1:1 to 100:1] (1:1 = bypass, 100:1 = limiter mode)
    - Soft knee [0-24 dB] with quadratic interpolation for smooth transition
    - Attack time [0.1-500ms] with EnvelopeFollower timing
    - Release time [1-5000ms] with configurable decay
    - Makeup gain [-24, +24 dB] with auto-makeup option
    - Detection mode: RMS (program material) or Peak (transient catching)
    - Sidechain highpass filter [20-500Hz] to reduce bass pumping
    - Lookahead [0-10ms] with accurate latency reporting
  - Gain reduction metering via `getCurrentGainReduction()` (negative dB)
  - Auto-makeup formula: `-threshold * (1 - 1/ratio)`
  - Per-sample processing (`processSample()`) and block processing (`process()`)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling with denormal flushing

- **Comprehensive test suite** (91 assertions across 39 test cases)
  - US1: Basic compression with threshold/ratio (8 tests)
  - US2: Attack/release timing verification (6 tests)
  - US3: Knee control - hard vs soft (6 tests)
  - US4: Makeup gain - manual and auto (4 tests)
  - US5: Detection mode - RMS vs Peak (2 tests)
  - US6: Sidechain filtering effectiveness (3 tests)
  - US7: Gain reduction metering accuracy (2 tests)
  - US8: Lookahead delay and latency reporting (5 tests)
  - Click-free operation verification
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Gain reduction formula** (hard knee):
  - `GR = (inputLevel_dB - threshold) * (1 - 1/ratio)`
  - Example: -10dB input, -20dB threshold, 4:1 ratio → 7.5dB GR
- **Soft knee formula**:
  - Quadratic interpolation in region `[threshold - knee/2, threshold + knee/2]`
  - Smooth transition from no compression to full ratio
- **Auto-makeup formula**:
  - `makeup = -threshold * (1 - 1/ratio)`
  - Compensates for average gain reduction at threshold level
- **Dependencies** (Layer 1-2 primitives):
  - `EnvelopeFollower` - Level detection (peer Layer 2)
  - `OnePoleSmoother` - Click-free gain reduction smoothing
  - `DelayLine` - Lookahead audio delay
  - `Biquad` - Sidechain highpass filter
  - `dbToGain()` / `gainToDb()` - dB/linear conversion
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIII (Architecture Docs), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/dynamics_processor.h"

using namespace Iterum::DSP;

DynamicsProcessor comp;

// In prepare() - allocates buffers
comp.prepare(44100.0, 512);
comp.setThreshold(-20.0f);     // -20 dB threshold
comp.setRatio(4.0f);           // 4:1 ratio
comp.setKneeWidth(6.0f);       // 6 dB soft knee
comp.setAttackTime(10.0f);     // 10ms attack
comp.setReleaseTime(100.0f);   // 100ms release
comp.setAutoMakeup(true);      // Automatic level compensation

// Enable sidechain HPF to reduce bass pumping
comp.setSidechainEnabled(true);
comp.setSidechainCutoff(80.0f);

// In processBlock() - real-time safe
comp.process(buffer, numSamples);

// Read gain reduction for UI metering
float grDb = comp.getCurrentGainReduction();  // e.g., -7.5

// Limiter mode with lookahead
comp.setThreshold(-0.3f);
comp.setRatio(100.0f);
comp.setLookahead(5.0f);       // 5ms lookahead
size_t latency = comp.getLatency();  // Report to host
```

---

## [0.0.11] - 2025-12-23

### Added

- **Layer 2 DSP Processor: EnvelopeFollower** (`src/dsp/processors/envelope_follower.h`)
  - Amplitude envelope tracking processor for dynamics processing
  - Three detection modes with distinct characteristics:
    - `Amplitude` - Full-wave rectification + asymmetric smoothing (~0.637 for sine)
    - `RMS` - Squared signal + blended smoothing + sqrt (~0.707 for sine, true RMS)
    - `Peak` - Instant attack (at min), exponential release (~1.0 captures peaks)
  - Configurable attack time [0.1-500ms] with one-pole smoothing
  - Configurable release time [1-5000ms] with one-pole smoothing
  - Optional sidechain highpass filter [20-500Hz] to reduce bass pumping
  - Per-sample processing (`processSample()`) for real-time envelope tracking
  - Block processing (`process()`) with envelope output buffer
  - `getCurrentValue()` for reading envelope without advancing state
  - Zero latency (Biquad TDF2 sidechain filter has no latency)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling (returns 0 for NaN, clamps Inf)
  - Denormal flushing for consistent CPU performance

- **Comprehensive test suite** (36 test cases covering all user stories)
  - US1: Basic detection modes with waveform verification
  - US2: Configurable attack/release timing accuracy
  - US3: Sample and block processing API
  - US4: Smooth parameter changes without discontinuities
  - US5: Sidechain filter for bass-insensitive detection
  - US6: Edge cases (NaN, Infinity, denormals, zero input)
  - US7: Mode switching continuity (no pops/clicks)
  - RMS accuracy within 1% of theoretical (0.707 for sine)
  - Attack/release coefficient verification via exponential decay

### Technical Details

- **Detection formulas**:
  - Amplitude: `rectified = |x|; env = rect + coeff * (env - rect)` (asymmetric)
  - RMS: `squared = x²; sqEnv = sq + blendedCoeff * (sqEnv - sq); env = √sqEnv`
  - Peak: Instant capture when `|x| > env`, exponential release otherwise
- **RMS blended coefficient**: `rmsCoeff = attackCoeff * 0.25 + releaseCoeff * 0.75`
  - Provides symmetric averaging while maintaining transient response
  - Achieves true RMS (0.707) for sine wave within 1% tolerance
- **Coefficient formula**: `coeff = exp(-1.0 / (timeMs * 0.001 * sampleRate))`
- **Sidechain filter**: Biquad highpass (Butterworth Q=0.707) at configurable cutoff
- **Dependencies**: db_utils.h (isNaN, isInf, flushDenormal, constexprExp), biquad.h
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/envelope_follower.h"

using namespace Iterum::DSP;

EnvelopeFollower env;

// In prepare() - recalculates coefficients
env.prepare(44100.0, 512);
env.setMode(DetectionMode::RMS);
env.setAttackTime(10.0f);   // 10ms attack
env.setReleaseTime(100.0f); // 100ms release

// Enable sidechain filter to reduce bass pumping
env.setSidechainEnabled(true);
env.setSidechainCutoff(80.0f);  // 80Hz highpass

// In processBlock() - per-sample tracking for compressor
for (size_t i = 0; i < numSamples; ++i) {
    float envelope = env.processSample(input[i]);
    float gainReduction = calculateGainReduction(envelope);
    output[i] = input[i] * gainReduction;
}

// Or block processing (envelope output buffer)
std::vector<float> envelopeBuffer(numSamples);
env.process(input, envelopeBuffer.data(), numSamples);

// Gate trigger (Peak mode with fast attack)
env.setMode(DetectionMode::Peak);
env.setAttackTime(0.1f);  // Near-instant
env.setReleaseTime(50.0f);
```

---

## [0.0.10] - 2025-12-23

### Added

- **Layer 2 DSP Processor: SaturationProcessor** (`src/dsp/processors/saturation_processor.h`)
  - Analog-style saturation/waveshaping processor composing Layer 1 primitives
  - 5 saturation algorithm types with distinct harmonic characteristics:
    - `Tape` - tanh(x), symmetric odd harmonics, warm classic saturation
    - `Tube` - Asymmetric polynomial (x + 0.3x² - 0.15x³), even harmonics, rich character
    - `Transistor` - Hard-knee soft clip at 0.5 threshold, aggressive clipping
    - `Digital` - Hard clip (clamp ±1.0), harsh all-harmonic distortion
    - `Diode` - Asymmetric exp/linear curve, subtle warmth with even harmonics
  - 2x oversampling via `Oversampler<2,1>` for alias-free nonlinear processing
  - Automatic DC blocking (10Hz highpass biquad) after asymmetric saturation
  - Input/output gain staging [-24dB, +24dB] for drive and makeup control
  - Dry/wet mix control [0.0, 1.0] with efficiency bypass at 0%
  - Parameter smoothing (5ms) via OnePoleSmoother for click-free modulation
  - Block processing (`process()`) and sample processing (`processSample()`)
  - Latency reporting for host delay compensation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Comprehensive test suite** (5,162 assertions across 28 test cases)
  - All 7 user stories covered (US1-US7)
  - DFT-based harmonic analysis for saturation verification
  - THD (Total Harmonic Distortion) increases with drive
  - Odd/even harmonic balance per saturation type
  - Gain staging verification (input/output properly applied)
  - Mix blending accuracy (dry/wet interpolation)
  - DC blocking effectiveness verification
  - Real-time safety verification (noexcept static_assert)
  - Edge case coverage (NaN, infinity, denormals, max drive)

### Technical Details

- **Saturation formulas**:
  - Tape: `y = tanh(x)`
  - Tube: `y = tanh(x + 0.3x² - 0.15x³)` - polynomial creates even harmonics
  - Transistor: Linear below 0.5, then `y = 0.5 + 0.5 * tanh((|x| - 0.5) / 0.5)`
  - Digital: `y = clamp(x, -1, +1)`
  - Diode: Forward `y = 1 - exp(-1.5x)`, Reverse `y = x / (1 - 0.5x)`
- **Oversampling**: 2x factor using IIR anti-aliasing filters
- **DC blocker**: 10Hz highpass biquad (Q=0.707) removes asymmetric DC offset
- **Smoothing**: 5ms one-pole RC filter for input gain, output gain, and mix
- **Constants**:
  - `kMinGainDb = -24.0f`, `kMaxGainDb = +24.0f`
  - `kDefaultSmoothingMs = 5.0f`
  - `kDCBlockerCutoffHz = 10.0f`
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/saturation_processor.h"

using namespace Iterum::DSP;

SaturationProcessor sat;

// In prepare() - allocates buffers
sat.prepare(44100.0, 512);
sat.setType(SaturationType::Tape);
sat.setInputGain(12.0f);   // +12 dB drive
sat.setOutputGain(-6.0f);  // -6 dB makeup
sat.setMix(1.0f);          // 100% wet

// In processBlock() - real-time safe
sat.process(buffer, numSamples);

// Tube saturation for warmth
sat.setType(SaturationType::Tube);
sat.setInputGain(6.0f);  // Moderate drive for even harmonics

// Parallel saturation (NY compression style)
sat.setMix(0.5f);  // 50% wet blends with dry signal

// Get latency for delay compensation
size_t latency = sat.getLatency();

// Sample-accurate processing (less efficient)
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = sat.processSample(buffer[i]);
}
```

---

## [0.0.9] - 2025-12-23

### Added

- **Layer 2 DSP Processor: MultimodeFilter** (`src/dsp/processors/multimode_filter.h`)
  - Complete filter module composing Layer 1 primitives (Biquad, OnePoleSmoother, Oversampler)
  - 8 filter types from RBJ Audio EQ Cookbook:
    - `Lowpass` - Low frequencies pass, attenuates above cutoff
    - `Highpass` - High frequencies pass, attenuates below cutoff
    - `Bandpass` - Passes band around cutoff frequency
    - `Notch` - Rejects band around cutoff frequency
    - `Allpass` - Flat magnitude, phase shift only (for phaser effects)
    - `LowShelf` - Boost/cut below shelf frequency
    - `HighShelf` - Boost/cut above shelf frequency
    - `Peak` - Parametric EQ bell curve (boost/cut at center)
  - 4 selectable slopes for LP/HP/BP/Notch:
    - `Slope12dB` - 12 dB/oct (1 biquad stage)
    - `Slope24dB` - 24 dB/oct (2 biquad stages, Butterworth Q)
    - `Slope36dB` - 36 dB/oct (3 biquad stages)
    - `Slope48dB` - 48 dB/oct (4 biquad stages)
  - Parameter smoothing via OnePoleSmoother (5ms default, configurable)
  - Pre-filter drive/saturation with 2x oversampled tanh waveshaping
  - Block processing (`process()`) with per-block coefficient updates
  - Sample processing (`processSample()`) for sample-accurate modulation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Comprehensive test suite** (1,686 assertions across 21 test cases)
  - All 7 user stories covered (US1-US7)
  - Slope verification: 24dB LP/HP attenuation at octave boundaries
  - Bandpass bandwidth verification (Q relationship)
  - Click-free modulation testing
  - Self-oscillation at high resonance
  - Drive THD verification (harmonics added)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Butterworth Q formula**: `Q[i] = 1 / (2 * cos(π * (2i + 1) / (4 * N)))` for N stages
- **Slope mapping**: `FilterSlope` enum values 1-4 map directly to stage counts
- **Smoothing**: One-pole RC filter per parameter (cutoff, resonance, gain, drive)
- **Drive saturation**: `tanh(sample * driveGain)` at 2x oversampled rate
- **Latency**: 0 samples (drive disabled) or `oversampler.getLatency()` (drive enabled)
- **Parameter ranges**:
  - Cutoff: 20 Hz to Nyquist/2
  - Resonance (Q): 0.1 to 100
  - Gain: -24 to +24 dB (for Shelf/Peak types)
  - Drive: 0 to 24 dB
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/multimode_filter.h"

using namespace Iterum::DSP;

MultimodeFilter filter;

// In prepare() - allocates buffers
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setSlope(FilterSlope::Slope24dB);  // 24 dB/oct
filter.setCutoff(1000.0f);
filter.setResonance(0.707f);  // Butterworth Q

// In processBlock() - real-time safe
filter.process(buffer, numSamples);

// LFO modulated filter
filter.setSmoothingTime(5.0f);  // 5ms smoothing
float lfoValue = lfo.process();
float cutoff = 1000.0f + lfoValue * 800.0f;  // 200-1800 Hz
filter.setCutoff(cutoff);
filter.process(buffer, numSamples);

// Pre-filter saturation
filter.setDrive(12.0f);  // 12dB drive (oversampled)
filter.process(buffer, numSamples);

// Sample-accurate modulation (more CPU)
for (size_t i = 0; i < numSamples; ++i) {
    filter.setCutoff(modulatedCutoff[i]);
    buffer[i] = filter.processSample(buffer[i]);
}

// High resonance (self-oscillation)
filter.setResonance(80.0f);  // Rings at cutoff frequency
```

---

## [0.0.8] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: FFT Processor** (`src/dsp/primitives/fft.h`)
  - Radix-2 Cooley-Tukey FFT implementation for spectral processing
  - Forward real-to-complex FFT (`forward()`)
  - Inverse complex-to-real FFT (`inverse()`)
  - Power-of-2 sizes: 256, 512, 1024, 2048, 4096, 8192
  - O(N log N) time complexity
  - Real-time safe: `noexcept`, pre-allocated twiddle factors and bit-reversal table
  - N/2+1 complex bins output for N-point real FFT

- **Window Functions** (`src/dsp/primitives/window_functions.h`)
  - Hann window - cosine-based, good frequency resolution
  - Hamming window - reduced first sidelobe
  - Blackman window - excellent sidelobe rejection (-58 dB)
  - Kaiser window (β=9.0) - configurable main lobe/sidelobe tradeoff
  - All windows normalized for COLA (Constant Overlap-Add) reconstruction
  - Pre-computed lookup tables for real-time performance

- **STFT (Short-Time Fourier Transform)** (`src/dsp/primitives/stft.h`)
  - Frame-by-frame spectral analysis
  - Configurable hop size (50%/75% overlap)
  - Window application before FFT
  - Continuous streaming audio input
  - `analyze()` returns spectrum when frame is ready
  - `reset()` clears internal state without reallocation

- **Overlap-Add Synthesis** (`src/dsp/primitives/stft.h`)
  - Artifact-free audio reconstruction from spectral frames
  - COLA normalization for perfect reconstruction
  - Configurable hop size matching analysis
  - Output accumulator for smooth frame overlap
  - `synthesize()` accepts spectrum, returns audio when ready

- **SpectralBuffer** (`src/dsp/primitives/spectral_buffer.h`)
  - Complex spectrum storage and manipulation
  - Polar access: `getMagnitude()`, `getPhase()`, `setMagnitude()`, `setPhase()`
  - Cartesian access: `getReal()`, `getImag()`, `setCartesian()`
  - Raw data access for FFT input/output
  - Building block for spectral effects (filtering, freeze, morphing)

- **Comprehensive FFT test suite** (421,777 assertions across 287 test cases)
  - All 6 user stories covered (US1-US6)
  - Forward FFT frequency bin accuracy (< 1 bin error)
  - Round-trip reconstruction (< 0.0001% error)
  - STFT/ISTFT perfect reconstruction (< 0.01% error)
  - Window function shape verification
  - O(N log N) complexity verification
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **FFT formulas**:
  - Forward: `X[k] = Σ x[n] * e^(-j2πkn/N)`
  - Inverse: `x[n] = (1/N) * Σ X[k] * e^(j2πkn/N)`
  - Twiddle factor: `W_N^k = e^(-j2πk/N) = cos(2πk/N) - j*sin(2πk/N)`
- **COLA windows**: Hann with 50% overlap, Hann with 75% overlap both sum to constant
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/primitives/fft.h"
#include "dsp/primitives/window_functions.h"
#include "dsp/primitives/stft.h"
#include "dsp/primitives/spectral_buffer.h"

using namespace Iterum::DSP;

// Basic FFT round-trip
FFT fft;
fft.prepare(1024);

std::array<float, 1024> input = { /* audio samples */ };
SpectralBuffer spectrum;
spectrum.prepare(1024);

fft.forward(input.data(), spectrum.data());
// Modify spectrum...
fft.inverse(spectrum.data(), input.data());

// STFT for continuous spectral processing
STFT stft;
stft.prepare(1024, WindowType::Hann, 0.5f);  // 50% overlap

OverlapAdd ola;
ola.prepare(1024, WindowType::Hann, 0.5f);

// In processBlock()
for (size_t i = 0; i < numSamples; ++i) {
    if (stft.analyze(input[i], spectrum)) {
        // Process spectrum...
        spectrum.setMagnitude(100, spectrum.getMagnitude(100) * 0.5f);  // Attenuate bin 100

        if (ola.synthesize(spectrum, output[i])) {
            // Output sample ready
        }
    }
}
```

---

## [0.0.7] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: Oversampler** (`src/dsp/primitives/oversampler.h`)
  - Upsampling/downsampling primitive for anti-aliased nonlinear processing
  - Supports 2x and 4x oversampling factors
  - Three quality presets:
    - `Economy` - IIR 8-pole Butterworth, ~48dB stopband, zero latency
    - `Standard` - FIR 31-tap halfband, ~80dB stopband, 15 samples latency
    - `High` - FIR 63-tap halfband, ~100dB stopband, 31 samples latency
  - Two processing modes:
    - `ZeroLatency` - IIR filters (minimum-phase, no latency)
    - `LinearPhase` - FIR filters (symmetric, linear phase)
  - Kaiser-windowed sinc FIR filter design for optimal stopband rejection
  - Pre-computed halfband coefficients (h[n]=0 for even n)
  - Polyphase FIR implementation for efficiency
  - Mono and stereo templates: `Oversampler<Factor, NumChannels>`
  - `processBlock()` for block-based processing with callback
  - `reset()` clears filter state without reallocation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Type aliases for common configurations**:
  - `Oversampler2xMono`, `Oversampler2xStereo`
  - `Oversampler4xMono`, `Oversampler4xStereo`

- **Comprehensive oversampler test suite** (1,068 assertions across 24 test cases)
  - All 5 user stories covered (US1-US5)
  - Stopband rejection verification (≥48dB economy, ≥80dB standard, ≥96dB high)
  - Passband flatness (<0.1dB ripple up to 20kHz)
  - Latency verification (0 for IIR, 15/31 samples for FIR)
  - Real-time safety verification (noexcept static_assert)
  - Multi-channel independence tests

### Changed

- **LFO: Click-free waveform transitions** (SC-008)
  - Smooth crossfade when changing waveforms during playback
  - Captures current output value at transition point
  - Handles mid-crossfade waveform changes correctly
  - `hasProcessed_` flag distinguishes setup vs runtime changes
  - Zero-crossing detection for waveforms that start at zero

- **Constitution v1.7.0**: Added Principle XV (Honest Completion)
  - Mandatory Implementation Verification sections in specs
  - Explicit compliance status tables for all requirements
  - Completion checklists with honest assessment guidelines
  - No softening of assessments or quiet scope reductions

- **Task templates**: Added cross-platform compatibility checks
  - IEEE 754 verification step after each user story
  - `-fno-fast-math` requirement for test files using `std::isnan`/`std::isfinite`/`std::isinf`
  - Floating-point precision guidelines (Approx().margin())

- **Tests CMakeLists.txt**: Extended `-fno-fast-math` to additional test files
  - `delay_line_test.cpp`, `lfo_test.cpp`, `biquad_test.cpp` added
  - Ensures IEEE 754 compliance on macOS/Linux (VST3 SDK enables `-ffast-math`)

### Fixed

- **Spec 001-db-conversion**: Added Implementation Verification section, SC-002 accuracy test
- **Spec 002-delay-line**: Added Implementation Verification section, SC-002/SC-003/SC-007 explicit tests
- **Spec 003-lfo**: Added Implementation Verification section, SC-008 click-free transitions
- **Spec 004-biquad-filter**: Added Implementation Verification section
- **Spec 005-parameter-smoother**: Added Implementation Verification section

### Technical Details

- **Oversampling formulas**:
  - FIR halfband: `y[n] = Σ h[k] * x[n-k]` for symmetric kernel
  - Kaiser window: `w[n] = I0(β × sqrt(1 - (n/M)²)) / I0(β)`
  - β calculation: `β = 0.1102 × (A - 8.7)` where A = stopband dB
- **LFO crossfade**: Linear interpolation over ~10ms (configurable)
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II, III, IX, X, XII, XV

### Usage

```cpp
#include "dsp/primitives/oversampler.h"

using namespace Iterum::DSP;

// 2x oversampling for saturation
Oversampler2xMono oversampler;
oversampler.prepare(44100.0);
oversampler.setQuality(OversamplingQuality::Standard);

// In processBlock() - process with callback
oversampler.processBlock(buffer, numSamples, [](float sample) {
    return std::tanh(sample * 2.0f);  // Saturation at oversampled rate
});

// 4x stereo oversampling
Oversampler4xStereo stereoOS;
stereoOS.prepare(48000.0);
stereoOS.setMode(OversamplingMode::LinearPhase);  // Best quality

// Get latency for delay compensation
size_t latency = stereoOS.getLatency();  // Report to host
```

---

## [0.0.5] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: Parameter Smoother** (`src/dsp/primitives/smoother.h`)
  - Three smoother types for different modulation characteristics:
    - `OnePoleSmoother` - Exponential approach (RC filter behavior)
    - `LinearRamp` - Constant rate change (tape-like pitch effects)
    - `SlewLimiter` - Maximum rate limiting with separate rise/fall rates
  - Sub-sample accurate transitions for artifact-free automation
  - Real-time safe: `noexcept`, zero allocations in `process()`
  - Configurable smoothing time (0.1ms - 1000ms)
  - Completion detection with configurable threshold (0.0001 default)
  - Denormal flushing (< 1e-15 → 0)
  - NaN/Infinity protection (NaN → 0, Inf → clamped)
  - Cross-platform NOINLINE macro for /fp:fast compatibility

- **Smoother characteristics**:
  - `OnePoleSmoother`: 99% convergence at specified time, exponential decay
  - `LinearRamp`: Fixed-duration transitions regardless of distance
  - `SlewLimiter`: Asymmetric rise/fall rates for envelope-like behavior

- **Comprehensive test suite** (5,320 assertions across 57 test cases)
  - All user stories covered (US1-US5)
  - Exponential convergence verification
  - Linear ramp timing accuracy
  - Slew rate limiting behavior
  - NaN/Infinity edge case handling
  - Completion detection with threshold
  - Reset and snap-to-target functionality

### Changed

- **Constitution v1.6.0**: Added Principle XIV (Pre-Implementation Research / ODR Prevention)
  - Mandatory codebase search before creating new classes
  - Diagnostic checklist for ODR symptoms (garbage values, test failures)
  - Lesson learned from 005-parameter-smoother incident

- **Planning templates**: Added mandatory codebase research gates
  - `spec-template.md`: "Existing Codebase Components" section
  - `plan-template.md`: Full "Codebase Research" section with search tables

- **CLAUDE.md**: Added "Pre-Implementation Research" section with ODR prevention checklist

### Fixed

- Removed duplicate `constexprExp` and `isNaN` functions from `smoother.h` (ODR violation)
- Updated `test_approvals_main.cpp` to use new OnePoleSmoother API

### Technical Details

- **Smoothing formulas**:
  - One-pole: `y = target + coeff * (y - target)` where `coeff = exp(-5000 / (timeMs * sampleRate))`
  - Linear: `y += increment` where `increment = delta / (timeMs * sampleRate / 1000)`
  - Slew: `y += clamp(target - y, -maxFall, +maxRise)`
- **Time constant**: Specified time is to 99% (5 tau), not 63% (1 tau)
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Dependencies**: `dsp/core/db_utils.h` for shared math utilities
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIV (ODR Prevention)

### Usage

```cpp
#include "dsp/primitives/smoother.h"

using namespace Iterum::DSP;

// One-pole smoother for filter cutoff
OnePoleSmoother cutoffSmoother;
cutoffSmoother.configure(10.0f, 44100.0f);  // 10ms to 99%
cutoffSmoother.setTarget(2000.0f);

// In processBlock()
for (size_t i = 0; i < numSamples; ++i) {
    float smoothedCutoff = cutoffSmoother.process();
    filter.setCutoff(smoothedCutoff);
    output[i] = filter.process(input[i]);
}

// Linear ramp for delay time (tape effect)
LinearRamp delayRamp;
delayRamp.configure(100.0f, 44100.0f);  // Always 100ms transitions
delayRamp.setTarget(newDelayMs);

// Slew limiter for envelope follower
SlewLimiter envelope;
envelope.configure(10.0f, 100.0f, 44100.0f);  // Fast attack, slow release
envelope.setTarget(inputLevel);
float smoothedEnvelope = envelope.process();
```

---

## [0.0.4] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: Biquad Filter** (`src/dsp/primitives/biquad.h`)
  - Second-order IIR filter using Transposed Direct Form II topology
  - 8 filter types from Robert Bristow-Johnson's Audio EQ Cookbook:
    - `Lowpass` - 12 dB/oct rolloff above cutoff
    - `Highpass` - 12 dB/oct rolloff below cutoff
    - `Bandpass` - Peak at center, rolloff both sides
    - `Notch` - Null at center frequency
    - `Allpass` - Flat magnitude, phase shift only
    - `LowShelf` - Boost/cut below shelf frequency
    - `HighShelf` - Boost/cut above shelf frequency
    - `Peak` - Parametric EQ bell curve
  - `BiquadCascade<N>` template for steeper slopes (24/36/48 dB/oct)
  - `SmoothedBiquad` for click-free coefficient modulation
  - Butterworth configuration (maximally flat passband)
  - Linkwitz-Riley configuration (flat sum at crossover)
  - Constexpr coefficient calculation for compile-time EQ
  - Denormal flushing (state < 1e-15 → 0)
  - NaN protection (returns 0, resets state)
  - Stability validation (Jury criterion)

- **Type aliases for common filter slopes**:
  - `Biquad12dB` - Single stage, 12 dB/oct
  - `Biquad24dB` - 2-stage cascade, 24 dB/oct
  - `Biquad36dB` - 3-stage cascade, 36 dB/oct
  - `Biquad48dB` - 4-stage cascade, 48 dB/oct

- **Utility functions**:
  - `butterworthQ(stageIndex, totalStages)` - Q values for Butterworth cascades
  - `linkwitzRileyQ(stageIndex, totalStages)` - Q values for LR crossovers
  - `BiquadCoefficients::isStable()` - Stability check
  - `BiquadCoefficients::isBypass()` - Bypass detection

- **Comprehensive test suite** (180 assertions across 49 test cases)
  - All 6 user stories covered (US1-US6)
  - Filter type coefficient verification
  - Frequency response tests at cutoff
  - Cascade slope verification (24/48 dB/oct)
  - Linkwitz-Riley flat sum at crossover
  - Smoothed coefficient convergence
  - Click-free modulation verification
  - Constexpr compile-time evaluation
  - Edge cases (frequency clamping, Q limits, denormals)

### Technical Details

- **TDF2 processing**: `y = b0*x + s0; s0 = b1*x - a1*y + s1; s1 = b2*x - a2*y`
- **Constexpr math**: Custom Taylor series for sin/cos (MSVC compatibility)
- **Smoothing**: One-pole interpolation per coefficient (1-100ms typical)
- **Stability**: Jury criterion with epsilon tolerance for boundary cases
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First)

### Usage

```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

// Basic lowpass
Biquad lpf;
lpf.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
float out = lpf.process(input);

// Steep 24 dB/oct highpass
Biquad24dB hp;
hp.setButterworth(FilterType::Highpass, 80.0f, 44100.0f);
hp.processBlock(buffer, numSamples);

// Click-free filter modulation
SmoothedBiquad modFilter;
modFilter.setSmoothingTime(10.0f, 44100.0f);
modFilter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
modFilter.snapToTarget();

// In audio callback - smoothly modulate cutoff
float cutoff = baseCutoff + lfo.process() * modAmount;
modFilter.setTarget(FilterType::Lowpass, cutoff, butterworthQ(), 0.0f, 44100.0f);
modFilter.processBlock(buffer, numSamples);

// Compile-time coefficients
constexpr auto staticEQ = BiquadCoefficients::calculateConstexpr(
    FilterType::Peak, 3000.0f, 2.0f, 6.0f, 44100.0f);
Biquad eq(staticEQ);
```

---

## [0.0.3] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: LFO (Low Frequency Oscillator)** (`src/dsp/primitives/lfo.h`)
  - Wavetable-based oscillator for generating modulation signals
  - 6 waveform types:
    - `Sine` - Smooth sinusoidal, starts at zero crossing
    - `Triangle` - Linear ramp 0→1→-1→0
    - `Sawtooth` - Linear ramp -1→+1, instant reset
    - `Square` - Binary +1/-1 alternation
    - `SampleHold` - Random value held for each cycle
    - `SmoothRandom` - Interpolated random, smooth transitions
  - Wavetable generation with 2048 samples per table
  - Double-precision phase accumulator (< 0.0001° drift over 24 hours)
  - Linear interpolation for smooth wavetable reading
  - Tempo sync with all note values (1/1 to 1/32)
  - Dotted and triplet modifiers
  - Phase offset (0-360°) for stereo LFO configurations
  - Retrigger functionality for note-on synchronization
  - Real-time safe: `noexcept`, no allocations in `process()`
  - Frequency range: 0.01-20 Hz

- **Enumerations for LFO configuration**:
  - `Iterum::DSP::Waveform` - 6 waveform types
  - `Iterum::DSP::NoteValue` - Note divisions (Whole to ThirtySecond)
  - `Iterum::DSP::NoteModifier` - None, Dotted, Triplet

- **Comprehensive test suite** (145,739 assertions across 87 test cases)
  - All 6 user stories covered (US1-US6)
  - Waveform shape verification
  - Tempo sync frequency calculations
  - Phase offset and retrigger behavior
  - Real-time safety verification (noexcept static_assert)
  - Edge case coverage (frequency clamping, phase wrapping)

### Technical Details

- **Wavetable size**: 2048 samples per waveform (power of 2 for efficient wrapping)
- **Phase precision**: Double-precision accumulator prevents drift over extended sessions
- **Tempo sync formula**: `frequency = BPM / (60 × noteBeats)`
- **Random generator**: LCG (Linear Congruential Generator) for deterministic, real-time safe randomness
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), XII (Test-First)

### Usage

```cpp
#include "dsp/primitives/lfo.h"

Iterum::DSP::LFO lfo;

// In prepare() - generates wavetables
lfo.prepare(44100.0);
lfo.setWaveform(Iterum::DSP::Waveform::Sine);
lfo.setFrequency(2.0f);  // 2 Hz

// In processBlock() - real-time safe
for (size_t i = 0; i < numSamples; ++i) {
    float mod = lfo.process();  // [-1, +1]
    // Use mod to modulate delay time, filter cutoff, etc.
}

// Tempo sync example
lfo.setTempoSync(true);
lfo.setTempo(120.0f);
lfo.setNoteValue(Iterum::DSP::NoteValue::Quarter,
                 Iterum::DSP::NoteModifier::Dotted);  // 1.33 Hz

// Stereo chorus with phase offset
Iterum::DSP::LFO lfoLeft, lfoRight;
lfoLeft.prepare(44100.0);
lfoRight.prepare(44100.0);
lfoLeft.setPhaseOffset(0.0f);
lfoRight.setPhaseOffset(90.0f);  // 90° offset for stereo width
```

---

## [0.0.2] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: DelayLine** (`src/dsp/primitives/delay_line.h`)
  - Real-time safe circular buffer delay line with fractional sample interpolation
  - Three read methods for different use cases:
    - `read(size_t)` - Integer delay, no interpolation (fastest)
    - `readLinear(float)` - Linear interpolation for modulated delays (chorus, flanger, vibrato)
    - `readAllpass(float)` - Allpass interpolation for feedback loops (unity gain at all frequencies)
  - Power-of-2 buffer sizing for O(1) bitwise wraparound
  - `prepare(sampleRate, maxDelaySeconds)` - Allocates buffer before processing
  - `reset()` - Clears buffer to silence without reallocation
  - Query methods: `maxDelaySamples()`, `sampleRate()`

- **Utility function**: `Iterum::DSP::nextPowerOf2(size_t)` - Constexpr power-of-2 calculation

- **Comprehensive test suite** (436 assertions across 50 test cases)
  - Basic fixed delay (write/read operations)
  - Linear interpolation accuracy
  - Allpass interpolation unity gain verification
  - Modulation smoothness tests
  - Real-time safety verification (noexcept static_assert)
  - O(1) performance characteristics
  - Edge case coverage (clamping, wrap-around)

### Technical Details

- **Interpolation formulas**:
  - Linear: `y = y0 + frac * (y1 - y0)`
  - Allpass: `y = x0 + a * (state - x1)` where `a = (1 - frac) / (1 + frac)`
- **Buffer sizing**: Next power of 2 >= (maxDelaySamples + 1) for efficient bitwise AND wrap
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), XII (Test-First)

### Usage

```cpp
#include "dsp/primitives/delay_line.h"

Iterum::DSP::DelayLine delay;

// In prepare() - allocates memory
delay.prepare(44100.0, 1.0f);  // 1 second max delay

// In processBlock() - real-time safe
delay.write(inputSample);

// Fixed delay (simple echo)
float echo = delay.read(22050);  // 0.5 second delay

// Modulated delay (chorus with LFO)
float lfoDelay = 500.0f + 20.0f * lfoValue;
float chorus = delay.readLinear(lfoDelay);

// Feedback network (fractional comb filter)
float comb = delay.readAllpass(100.5f);  // Fixed fractional delay
```

---

## [0.0.1] - 2025-12-22

### Added

- **Layer 0 Core Utilities: dB/Linear Conversion** (`src/dsp/core/db_utils.h`)
  - `Iterum::DSP::dbToGain(float dB)` - Convert decibels to linear gain
  - `Iterum::DSP::gainToDb(float gain)` - Convert linear gain to decibels
  - `Iterum::DSP::kSilenceFloorDb` - Silence floor constant (-144 dB)
  - Full C++20 `constexpr` support for compile-time evaluation
  - Real-time safe: no allocation, no exceptions, no I/O
  - NaN handling: `dbToGain(NaN)` returns 0.0f, `gainToDb(NaN)` returns -144 dB

- **Custom constexpr math implementations** (MSVC compatibility)
  - Taylor series `constexprExp()` and `constexprLn()` functions
  - Required because MSVC lacks constexpr `std::pow`/`std::log10`

- **Comprehensive test suite** (146 assertions across 24 test cases)
  - Unit tests for all dB conversion functions
  - Constexpr compile-time evaluation tests
  - Edge case coverage (NaN, infinity, silence)

- **Project infrastructure**
  - Layered DSP architecture (Layer 0-4 hierarchy)
  - Test-first development workflow (Constitution Principle XII)
  - Catch2 testing framework integration

### Technical Details

- **Silence floor**: -144 dB (24-bit dynamic range: 6.02 dB/bit * 24 = ~144 dB)
- **Formulas**:
  - `dbToGain`: gain = 10^(dB/20)
  - `gainToDb`: dB = 20 * log10(gain), clamped to -144 dB floor
- **Namespace**: `Iterum::DSP` (Layer 0 core utilities)

### Usage

```cpp
#include "dsp/core/db_utils.h"

// Runtime conversion
float gain = Iterum::DSP::dbToGain(-6.0f);    // ~0.5
float dB   = Iterum::DSP::gainToDb(0.5f);     // ~-6 dB

// Compile-time lookup tables
constexpr std::array<float, 3> gains = {
    Iterum::DSP::dbToGain(-20.0f),  // 0.1
    Iterum::DSP::dbToGain(0.0f),    // 1.0
    Iterum::DSP::dbToGain(20.0f)    // 10.0
};
```
