# Data Model: Multi-Tap Delay Mode

**Feature**: 028-multi-tap
**Date**: 2025-12-26

## Entity Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           MultiTapDelay (Layer 4)                           │
├─────────────────────────────────────────────────────────────────────────────┤
│ Composes:                                                                   │
│   ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────────────┐  │
│   │   TapManager     │  │ FeedbackNetwork  │  │   ModulationMatrix*      │  │
│   │   (Layer 3)      │  │   (Layer 3)      │  │   (Layer 3, external)    │  │
│   └──────────────────┘  └──────────────────┘  └──────────────────────────┘  │
│                                                                             │
│ Adds:                                                                       │
│   ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────────────┐  │
│   │  TimingPattern   │  │  SpatialPattern  │  │   Pattern Morphing       │  │
│   │   (enum class)   │  │   (enum class)   │  │   (OnePoleSmoother×N)    │  │
│   └──────────────────┘  └──────────────────┘  └──────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Enumerations

### TimingPattern (Layer 4 - New)

Controls WHEN taps occur (delay times).

```cpp
enum class TimingPattern : uint8_t {
    // Rhythmic patterns (delegate to TapManager)
    WholeNote,
    HalfNote,
    QuarterNote,
    EighthNote,
    SixteenthNote,
    ThirtySecondNote,

    // Dotted variants
    DottedHalf,
    DottedQuarter,
    DottedEighth,
    DottedSixteenth,

    // Triplet variants
    TripletHalf,
    TripletQuarter,
    TripletEighth,
    TripletSixteenth,

    // Mathematical patterns (Layer 4 implementation)
    GoldenRatio,      // φ = 1.618: 1×, 1.618×, 2.618×, 4.236×...
    Fibonacci,        // 1, 1, 2, 3, 5, 8, 13...
    Exponential,      // 2^n: 1×, 2×, 4×, 8×, 16×...
    PrimeNumbers,     // 2, 3, 5, 7, 11, 13, 17, 19, 23, 29...
    LinearSpread,     // Equal spacing from min to max

    Custom            // User-defined times
};
```

### SpatialPattern (Layer 4 - New)

Controls WHERE taps are positioned (pan and level distribution).

```cpp
enum class SpatialPattern : uint8_t {
    // Pan patterns
    Cascade,          // Pan sweeps L→R across taps
    Alternating,      // Pan alternates L, R, L, R...
    Centered,         // All taps center pan
    WideningStereo,   // Pan spreads progressively wider

    // Level patterns
    DecayingLevel,    // Each tap -3dB from previous
    FlatLevel,        // All taps equal level

    Custom            // User-defined pan/level
};
```

### Existing Enums (Reused from Layer 3)

```cpp
// From TapManager (Layer 3)
enum class TapPattern : uint8_t {
    Custom, QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci
};

enum class TapTimeMode : uint8_t {
    FreeRunning,    // Time in milliseconds
    TempoSynced     // Time as note value
};

enum class TapFilterMode : uint8_t {
    Bypass, Lowpass, Highpass
};

// From note_value.h (Layer 0)
enum class NoteValue : uint8_t {
    DoubleWhole, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth
};

enum class NoteModifier : uint8_t {
    None, Dotted, Triplet
};
```

## Structures

### TapConfiguration (Layer 4 - New)

Runtime state for external tap configuration. Provides a clean API for UI/preset systems.

```cpp
struct TapConfiguration {
    // Enable state
    bool enabled = true;

    // Timing
    TapTimeMode timeMode = TapTimeMode::FreeRunning;
    float timeMs = 250.0f;              // For FreeRunning mode
    NoteValue noteValue = NoteValue::Quarter;  // For TempoSynced mode
    NoteModifier noteModifier = NoteModifier::None;

    // Level and pan
    float levelDb = 0.0f;               // -96 to +6 dB
    float pan = 0.0f;                   // -100 (L) to +100 (R)

    // Filter
    TapFilterMode filterMode = TapFilterMode::Bypass;
    float filterCutoff = 1000.0f;       // 20 to 20000 Hz
    float filterQ = 0.707f;             // 0.5 to 10.0

    // Feedback contribution
    float feedbackAmount = 0.0f;        // 0 to 100%
};
```

### MorphState (Layer 4 - Internal)

Internal state for pattern morphing transitions.

```cpp
struct MorphState {
    bool active = false;
    float progress = 0.0f;              // 0.0 to 1.0
    float morphRatePerSample = 0.0f;    // Calculated from morphTimeMs

    // From values (captured at morph start)
    std::array<float, 16> fromTimes{};
    std::array<float, 16> fromLevels{};
    std::array<float, 16> fromPans{};

    // To values (calculated from target pattern)
    std::array<float, 16> toTimes{};
    std::array<float, 16> toLevels{};
    std::array<float, 16> toPans{};
};
```

## Class Definition

### MultiTapDelay

```cpp
/// @brief Layer 4 User Feature - Multi-Tap Delay with Pattern System
class MultiTapDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxTaps = 16;
    static constexpr float kMinTapCount = 2;
    static constexpr float kMaxTapCount = 16;
    static constexpr float kMinBaseTimeMs = 1.0f;
    static constexpr float kMaxBaseTimeMs = 5000.0f;
    static constexpr float kMinMorphTimeMs = 50.0f;
    static constexpr float kMaxMorphTimeMs = 2000.0f;
    static constexpr float kDefaultMorphTimeMs = 200.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void prepare(float sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Pattern Selection (FR-002, FR-002a, FR-002b)
    // =========================================================================

    void loadTimingPattern(TimingPattern pattern, size_t tapCount) noexcept;
    void loadSpatialPattern(SpatialPattern pattern) noexcept;

    // =========================================================================
    // Pattern Morphing (FR-025, FR-026)
    // =========================================================================

    void morphToTimingPattern(TimingPattern pattern, float morphTimeMs) noexcept;
    void morphToSpatialPattern(SpatialPattern pattern, float morphTimeMs) noexcept;
    [[nodiscard]] bool isMorphing() const noexcept;
    [[nodiscard]] float getMorphProgress() const noexcept;

    // =========================================================================
    // Tempo Sync (FR-007, FR-008)
    // =========================================================================

    void setTempo(float bpm) noexcept;
    void setBaseTimeMs(float timeMs) noexcept;
    void setBaseNoteValue(NoteValue note, NoteModifier modifier) noexcept;

    // =========================================================================
    // Per-Tap Configuration (FR-004, FR-011-FR-015)
    // =========================================================================

    void setTapConfiguration(size_t tapIndex, const TapConfiguration& config) noexcept;
    [[nodiscard]] TapConfiguration getTapConfiguration(size_t tapIndex) const noexcept;

    // =========================================================================
    // Master Controls (FR-016-FR-020, FR-028-FR-030)
    // =========================================================================

    void setMasterFeedback(float amount) noexcept;        // 0-110%
    void setFeedbackLowpassCutoff(float hz) noexcept;     // 20-20000 Hz
    void setFeedbackHighpassCutoff(float hz) noexcept;    // 20-20000 Hz
    void setDryWetMix(float mix) noexcept;                // 0-100%
    void setOutputLevel(float db) noexcept;               // -12 to +12 dB

    // =========================================================================
    // Modulation (FR-021-FR-023)
    // =========================================================================

    void setModulationMatrix(ModulationMatrix* matrix) noexcept;
    void registerModulationDestinations() noexcept;

    // =========================================================================
    // Processing (FR-028)
    // =========================================================================

    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    [[nodiscard]] size_t getActiveTapCount() const noexcept;
    [[nodiscard]] TimingPattern getCurrentTimingPattern() const noexcept;
    [[nodiscard]] SpatialPattern getCurrentSpatialPattern() const noexcept;

private:
    // =========================================================================
    // Composed Components
    // =========================================================================

    TapManager tapManager_;
    FeedbackNetwork feedbackNetwork_;
    ModulationMatrix* modMatrix_ = nullptr;

    // =========================================================================
    // Pattern State
    // =========================================================================

    TimingPattern timingPattern_ = TimingPattern::QuarterNote;
    SpatialPattern spatialPattern_ = SpatialPattern::DecayingLevel;
    size_t activeTapCount_ = 4;
    float baseTimeMs_ = 250.0f;
    float bpm_ = 120.0f;

    // =========================================================================
    // Morph State
    // =========================================================================

    MorphState morphState_;
    float morphTimeMs_ = kDefaultMorphTimeMs;

    // =========================================================================
    // Audio State
    // =========================================================================

    float sampleRate_ = 44100.0f;
    size_t maxBlockSize_ = 512;
    float maxDelayMs_ = 5000.0f;

    // =========================================================================
    // Internal Methods
    // =========================================================================

    void applyExponentialPattern(size_t tapCount) noexcept;
    void applyPrimeNumbersPattern(size_t tapCount) noexcept;
    void applyLinearSpreadPattern(size_t tapCount) noexcept;
    void applyCascadePattern(size_t tapCount) noexcept;
    void applyAlternatingPattern(size_t tapCount) noexcept;
    void applyCenteredPattern(size_t tapCount) noexcept;
    void applyWideningStereoPattern(size_t tapCount) noexcept;
    void applyDecayingLevelPattern(size_t tapCount) noexcept;
    void applyFlatLevelPattern(size_t tapCount) noexcept;
    void updateMorph(size_t numSamples) noexcept;
    void applyModulation() noexcept;
};
```

## Modulation Destination IDs (FR-021-FR-023)

Per-tap modulation destinations registered with ModulationMatrix. Each tap has 4 modulatable parameters:

| Parameter | Tap 0 | Tap 1 | ... | Tap 15 | Formula |
|-----------|-------|-------|-----|--------|---------|
| Time | 0 | 1 | ... | 15 | `tapIndex` |
| Level | 16 | 17 | ... | 31 | `16 + tapIndex` |
| Pan | 32 | 33 | ... | 47 | `32 + tapIndex` |
| FilterCutoff | 48 | 49 | ... | 63 | `48 + tapIndex` |

**Total destinations**: 64 (16 taps × 4 parameters)

```cpp
// Destination ID calculation
enum class ModulationDestination : uint8_t {
    // Time modulation (IDs 0-15)
    Tap0Time = 0, Tap1Time, /* ... */ Tap15Time = 15,

    // Level modulation (IDs 16-31)
    Tap0Level = 16, Tap1Level, /* ... */ Tap15Level = 31,

    // Pan modulation (IDs 32-47)
    Tap0Pan = 32, Tap1Pan, /* ... */ Tap15Pan = 47,

    // Filter cutoff modulation (IDs 48-63)
    Tap0Cutoff = 48, Tap1Cutoff, /* ... */ Tap15Cutoff = 63
};

// Helper functions
constexpr uint8_t getTimeDestination(size_t tapIndex) { return tapIndex; }
constexpr uint8_t getLevelDestination(size_t tapIndex) { return 16 + tapIndex; }
constexpr uint8_t getPanDestination(size_t tapIndex) { return 32 + tapIndex; }
constexpr uint8_t getCutoffDestination(size_t tapIndex) { return 48 + tapIndex; }
```

**Modulation Behavior** (FR-023):
- Modulation values are applied **additively** to base parameter values
- Time modulation: ±10% of base time (prevents extreme pitch shifts)
- Level modulation: ±12 dB from base level
- Pan modulation: ±100 (full stereo range)
- Cutoff modulation: ±2 octaves from base cutoff

## Relationships

```
MultiTapDelay (Layer 4)
    │
    ├──► TapManager (Layer 3) [owns]
    │       │
    │       ├──► DelayLine (Layer 1) [owns]
    │       ├──► Biquad×16 (Layer 1) [owns, per-tap filters]
    │       └──► OnePoleSmoother×N (Layer 1) [owns, per-tap smoothing]
    │
    ├──► FeedbackNetwork (Layer 3) [owns]
    │       │
    │       ├──► DelayLine×2 (Layer 1) [owns, stereo]
    │       ├──► MultimodeFilter (Layer 2) [owns]
    │       └──► SaturationProcessor (Layer 2) [owns]
    │
    └──► ModulationMatrix* (Layer 3) [references, external]
            │
            └──► ModulationSource* ×N [references, LFOs/Envelopes]
```

## Memory Layout

```
MultiTapDelay instance (~24 KB at default settings):
├── TapManager (~16 KB)
│   ├── DelayLine (~10 KB @ 5000ms, 44.1kHz)
│   ├── Tap×16 (~6 KB)
│   │   ├── Biquad filter
│   │   └── Smoothers×4
│   └── Master smoothers
├── FeedbackNetwork (~6 KB)
│   ├── DelayLine×2 (~4 KB @ 5000ms, 44.1kHz)
│   ├── MultimodeFilter
│   └── SaturationProcessor
├── MorphState (~512 bytes)
│   └── Arrays×3 (times, levels, pans)
└── Configuration state (~128 bytes)
```

## State Persistence

For preset save/load, serialize:

```cpp
struct MultiTapDelayState {
    // Pattern selection
    TimingPattern timingPattern;
    SpatialPattern spatialPattern;
    size_t activeTapCount;

    // Timing
    float baseTimeMs;
    float bpm;
    bool tempoSyncEnabled;

    // Per-tap configurations
    std::array<TapConfiguration, 16> taps;

    // Master settings
    float masterFeedback;
    float feedbackLPCutoff;
    float feedbackHPCutoff;
    float dryWetMix;
    float outputLevel;

    // Morph settings
    float morphTimeMs;
};
```
