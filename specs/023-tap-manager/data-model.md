# Data Model: TapManager

**Feature**: 023-tap-manager
**Date**: 2025-12-25

---

## Entities

### TapPattern (Enum)

Defines preset tap timing patterns.

```cpp
enum class TapPattern : uint8_t {
    Custom,         // User-defined times
    QuarterNote,    // 1, 2, 3, 4... × quarter note
    DottedEighth,   // 1, 2, 3, 4... × dotted eighth (0.75 × quarter)
    Triplet,        // 1, 2, 3, 4... × triplet quarter (0.667 × quarter)
    GoldenRatio,    // Each tap = previous × 1.618
    Fibonacci       // Fibonacci sequence (1, 1, 2, 3, 5, 8...)
};
```

### TapTimeMode (Enum)

Defines how a tap's delay time is specified.

```cpp
enum class TapTimeMode : uint8_t {
    FreeRunning,    // Time in milliseconds (absolute)
    TempoSynced     // Time as note value (relative to BPM)
};
```

### TapFilterMode (Enum)

Defines the filter type for a tap.

```cpp
enum class TapFilterMode : uint8_t {
    Bypass,     // No filtering
    Lowpass,    // Low-pass filter
    Highpass    // High-pass filter
};
```

### Tap (Struct)

Represents a single delay tap with all its parameters.

**Fields**:

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| enabled | bool | - | false | Whether tap produces output |
| timeMode | TapTimeMode | - | FreeRunning | Time specification mode |
| timeMs | float | 0 to maxDelayMs | 0.0f | Delay time in milliseconds |
| noteValue | NoteValue | - | Quarter | Note value for tempo sync |
| levelDb | float | -96 to +6 | 0.0f | Output level in dB |
| pan | float | -100 to +100 | 0.0f | Stereo position (%) |
| filterMode | TapFilterMode | - | Bypass | Filter type |
| filterCutoff | float | 20 to 20000 | 1000.0f | Filter cutoff in Hz |
| filterQ | float | 0.5 to 10.0 | 0.707f | Filter resonance (Q) |
| feedbackAmount | float | 0 to 100 | 0.0f | Feedback to master (%) |

**Internal State** (not user-configurable):

| Field | Type | Description |
|-------|------|-------------|
| delaySmoother | OnePoleSmoother | Smooths delay time changes |
| levelSmoother | OnePoleSmoother | Smooths level changes |
| panSmoother | OnePoleSmoother | Smooths pan changes |
| cutoffSmoother | OnePoleSmoother | Smooths filter cutoff changes |
| filter | Biquad | Per-tap filter instance |

### TapManager (Class)

Container managing up to 16 taps with shared delay line and output mixing.

**Configuration Fields**:

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| sampleRate | float | > 0 | 44100.0f | Current sample rate |
| maxDelayMs | float | > 0 | 5000.0f | Maximum delay time |
| pattern | TapPattern | - | Custom | Current pattern preset |
| masterLevel | float | -96 to +6 | 0.0f | Master output level (dB) |
| dryWetMix | float | 0 to 100 | 100.0f | Dry/wet mix (%) |
| bpm | float | > 0 | 120.0f | Current tempo |

**Internal State**:

| Field | Type | Description |
|-------|------|-------------|
| taps | std::array<Tap, 16> | All 16 tap instances |
| delayLine | DelayLine | Shared delay storage |
| masterLevelSmoother | OnePoleSmoother | Smooths master level |
| dryWetSmoother | OnePoleSmoother | Smooths dry/wet mix |

---

## Relationships

```
TapManager (1) ──owns──> (16) Tap
TapManager (1) ──owns──> (1) DelayLine (shared by all taps)

Tap (1) ──owns──> (1) Biquad
Tap (1) ──owns──> (4) OnePoleSmoother
Tap (1) ──references──> (1) NoteValue (from Layer 0)
```

---

## Validation Rules

### Tap Validation

1. `timeMs` MUST be clamped to [0, maxDelayMs]
2. `levelDb` MUST be clamped to [-96, +6] (FR-009)
3. `pan` MUST be clamped to [-100, +100] (FR-012)
4. `filterCutoff` MUST be clamped to [20, 20000] (FR-016)
5. `filterQ` MUST be clamped to [0.5, 10.0] (FR-017)
6. `feedbackAmount` MUST be clamped to [0, 100] (FR-019)

### TapManager Validation

1. Maximum 16 taps (FR-001)
2. Total feedback MUST be limited to prevent runaway (FR-021)
3. Pattern tap count MUST be in [1, 16] (FR-027)
4. `bpm` MUST be > 0 for tempo sync calculations

---

## State Transitions

### Tap Enable/Disable

```
Disabled ──setEnabled(true)──> Enabled
Enabled ──setEnabled(false)──> Disabled

Transition: Smooth fade via levelSmoother (no click)
```

### Pattern Change

```
Custom ──loadPattern(X)──> [Pattern X]
[Pattern X] ──modifyTap()──> Custom

Side effects:
- All tap times recalculated
- Tap count may change (based on pattern configuration)
- Smoothers snap to new values (instant transition)
```

### Time Mode Change

```
FreeRunning ──setTimeMode(Synced)──> TempoSynced
TempoSynced ──setTimeMode(Free)──> FreeRunning

On TempoSynced: timeMs = (60000 / bpm) × noteMultiplier
On FreeRunning: timeMs uses stored value
```

---

## Computed Values

### Tempo-Synced Delay Time

```cpp
float getEffectiveDelayMs(const Tap& tap, float bpm) {
    if (tap.timeMode == TapTimeMode::FreeRunning) {
        return tap.timeMs;
    }
    float quarterNoteMs = 60000.0f / bpm;
    return quarterNoteMs * getNoteMultiplier(tap.noteValue);
}
```

### Pattern Times

Note: Array index `i` is 0-based, but formulas use 1-based tap number `n = i + 1`.

```cpp
// Quarter Note: tap[i] = (i+1) × quarterNoteMs  (500, 1000, 1500, 2000ms at 120 BPM)
// Dotted Eighth: tap[i] = (i+1) × (quarterNoteMs × 0.75)  (375, 750, 1125, 1500ms)
// Triplet: tap[i] = (i+1) × (quarterNoteMs × 0.667)  (333, 667, 1000, 1333ms)
// Golden Ratio: tap[0] = quarterNoteMs, tap[i] = tap[i-1] × 1.618
// Fibonacci: tap[i] = fib(i+1) × baseMs, where fib = 1, 1, 2, 3, 5, 8...
```
