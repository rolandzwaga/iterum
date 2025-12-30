# Data Model: Granular Delay Tempo Sync

**Feature**: 038-granular-tempo-sync
**Date**: 2025-12-30

## Parameter Entities

### GranularParams (Extended)

```
+----------------------------------+
|         GranularParams           |
+----------------------------------+
| grainSize: atomic<float>         | 10-500ms
| density: atomic<float>           | 1-100 grains/sec
| delayTime: atomic<float>         | 0-2000ms (Free mode)
| pitch: atomic<float>             | -24 to +24 semitones
| pitchSpray: atomic<float>        | 0-1
| positionSpray: atomic<float>     | 0-1
| panSpray: atomic<float>          | 0-1
| reverseProb: atomic<float>       | 0-1
| freeze: atomic<bool>             | on/off
| feedback: atomic<float>          | 0-1.2
| dryWet: atomic<float>            | 0-1
| outputGain: atomic<float>        | -96 to +6 dB
| envelopeType: atomic<int>        | 0-3
+----------------------------------+
| timeMode: atomic<int>            | NEW: 0=Free, 1=Synced
| noteValue: atomic<int>           | NEW: 0-9 (dropdown index)
+----------------------------------+
```

### TimeMode (Existing Enum)

| Value | Name | Description |
|-------|------|-------------|
| 0 | Free | Delay time in milliseconds (delayTime parameter) |
| 1 | Synced | Delay time from note value + host tempo |

### NoteValue Dropdown Mapping (Existing)

**Note**: Uses standard `kNoteValueDropdownMapping` from `note_value.h` - includes straight notes and triplets only (no dotted notes). This matches Digital Delay, PingPong Delay, and Reverse Delay modes.

| Index | Display | NoteValue | NoteModifier | At 120 BPM |
|-------|---------|-----------|--------------|------------|
| 0 | 1/32 | ThirtySecond | None | 62.5ms |
| 1 | 1/16T | Sixteenth | Triplet | 83.3ms |
| 2 | 1/16 | Sixteenth | None | 125ms |
| 3 | 1/8T | Eighth | Triplet | 166.7ms |
| 4 | 1/8 | Eighth | None | 250ms (default) |
| 5 | 1/4T | Quarter | Triplet | 333.3ms |
| 6 | 1/4 | Quarter | None | 500ms |
| 7 | 1/2T | Half | Triplet | 666.7ms |
| 8 | 1/2 | Half | None | 1000ms |
| 9 | 1/1 | Whole | None | 2000ms |

## Parameter IDs

| ID | Name | Range | Default | Format |
|----|------|-------|---------|--------|
| 113 | kGranularTimeModeId | 0-1 | 0 (Free) | Dropdown: "Free", "Synced" |
| 114 | kGranularNoteValueId | 0-9 | 4 (1/8) | Dropdown: see mapping above |

## State Relationships

```
                    ┌─────────────┐
                    │ TimeMode    │
                    │ (Free/Sync) │
                    └──────┬──────┘
                           │
           ┌───────────────┴───────────────┐
           │                               │
           ▼                               ▼
    ┌──────────────┐              ┌──────────────────┐
    │   Free Mode  │              │   Synced Mode    │
    │              │              │                  │
    │ delayTime    │              │ noteValue +      │
    │ parameter    │              │ host tempo       │
    │ (0-2000ms)   │              │ = calculated ms  │
    └──────┬───────┘              └────────┬─────────┘
           │                               │
           └───────────────┬───────────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │ Effective       │
                  │ Position (ms)   │
                  │ ← clamp 0-2000  │
                  └────────┬────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │ GranularEngine  │
                  │ .setPosition()  │
                  └─────────────────┘
```

## Validation Rules

1. **TimeMode**: Must be 0 or 1
2. **NoteValue**: Must be 0-9, out-of-range defaults to 4 (1/8 note)
3. **Synced Position**: Result clamped to 0-2000ms (max delay buffer)
4. **Tempo**: Clamped to 20-300 BPM by noteToDelayMs()

## State Persistence

Binary format (appended to existing granular state):

| Offset | Size | Type | Field |
|--------|------|------|-------|
| +0 | 4 | int32 | timeMode (0 or 1) |
| +4 | 4 | int32 | noteValue (0-9) |

**Note**: Must be appended AFTER existing granular params for backward compatibility. New params default to 0 (Free mode) if not present in saved state.
