# Quickstart: Granular Delay Tempo Sync

**Feature**: 038-granular-tempo-sync
**Date**: 2025-12-30

## Overview

This feature adds tempo synchronization to the Granular Delay mode, allowing the grain position (delay time) to lock to musical note divisions based on host tempo.

## Key Files to Modify

| File | Changes |
|------|---------|
| `src/plugin_ids.h` | Add kGranularTimeModeId (113), kGranularNoteValueId (114) |
| `src/parameters/granular_params.h` | Add timeMode, noteValue atomics and handlers |
| `src/dsp/features/granular_delay.h` | Add setTimeMode(), setNoteValue(), tempo-aware process() |
| `resources/editor.uidesc` | Add TimeMode and NoteValue dropdowns |

## Key Dependencies

```cpp
#include "dsp/core/note_value.h"        // dropdownToDelayMs, getNoteValueFromDropdown
#include "dsp/systems/delay_engine.h"   // TimeMode enum
#include "dsp/core/block_context.h"     // BlockContext (tempoBPM)
```

## Implementation Snippets

### 1. Add Parameter IDs (plugin_ids.h)

```cpp
// After kGranularEnvelopeTypeId = 112
kGranularTimeModeId = 113,       // 0=Free, 1=Synced
kGranularNoteValueId = 114,      // 0-9 (dropdown index)
```

### 2. Add Atomic Storage (granular_params.h)

```cpp
struct GranularParams {
    // ... existing params ...

    std::atomic<int> timeMode{0};       // 0=Free, 1=Synced
    std::atomic<int> noteValue{4};      // 0-9 (default: 1/8 note)
};
```

### 3. Handle Parameter Changes (granular_params.h)

```cpp
case kGranularTimeModeId:
    params.timeMode.store(
        normalizedValue >= 0.5 ? 1 : 0,
        std::memory_order_relaxed);
    break;

case kGranularNoteValueId:
    params.noteValue.store(
        static_cast<int>(normalizedValue * 9.0 + 0.5),
        std::memory_order_relaxed);
    break;
```

### 4. Register Parameters (granular_params.h)

```cpp
// Time Mode dropdown
parameters.addParameter(createDropdownParameter(
    STR16("Time Mode"), kGranularTimeModeId,
    {STR16("Free"), STR16("Synced")}
));

// Note Value dropdown
parameters.addParameter(createDropdownParameterWithDefault(
    STR16("Note Value"), kGranularNoteValueId,
    4,  // default: 1/8 (index 4)
    {STR16("1/32"), STR16("1/16T"), STR16("1/16"), STR16("1/8T"), STR16("1/8"),
     STR16("1/4T"), STR16("1/4"), STR16("1/2T"), STR16("1/2"), STR16("1/1")}
));
```

### 5. Add DSP Methods (granular_delay.h)

```cpp
class GranularDelay {
public:
    void setTimeMode(int mode) noexcept {
        timeMode_ = (mode == 1) ? TimeMode::Synced : TimeMode::Free;
    }

    void setNoteValue(int index) noexcept {
        noteValueIndex_ = std::clamp(index, 0, 9);
    }

private:
    TimeMode timeMode_ = TimeMode::Free;
    int noteValueIndex_ = 4;  // 1/8 note default
};
```

### 6. Process with Tempo Sync (granular_delay.h)

```cpp
void process(const float* leftIn, const float* rightIn,
             float* leftOut, float* rightOut,
             size_t numSamples,
             const BlockContext& ctx) noexcept {

    // Update position from tempo if synced
    if (timeMode_ == TimeMode::Synced) {
        float syncedMs = dropdownToDelayMs(noteValueIndex_, ctx.tempoBPM);
        syncedMs = std::clamp(syncedMs, 0.0f, kMaxDelaySeconds * 1000.0f);
        engine_.setPosition(syncedMs);
    }

    // ... rest of existing process code ...
}
```

## Testing

### DSP Unit Tests

Write tests in `tests/unit/features/granular_delay_tempo_sync_test.cpp`:

```cpp
TEST_CASE("GranularDelay tempo sync", "[granular][tempo-sync]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.setTimeMode(1);  // Synced
    delay.setNoteValue(6); // 1/4 note

    BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0};

    // At 120 BPM, 1/4 note = 500ms
    // Process and verify position
    REQUIRE(/* position is ~500ms */);
}
```

### UI E2E Tests

Write tests in `tests/unit/vst/granular_tempo_sync_ui_test.cpp`:

```cpp
TEST_CASE("Granular tempo sync parameter IDs are defined", "[vst][granular][tempo-sync]") {
    REQUIRE(kGranularTimeModeId == 113);
    REQUIRE(kGranularNoteValueId == 114);
}

TEST_CASE("Granular TimeMode dropdown has correct options", "[vst][granular][tempo-sync]") {
    // Verify StringListParameter with "Free", "Synced" options
}

TEST_CASE("Granular NoteValue dropdown has all note values", "[vst][granular][tempo-sync]") {
    // Verify StringListParameter with 10 options (1/32 through 1/1)
    // Verify default is index 4 (1/8 note)
}

TEST_CASE("Granular tempo sync state persistence", "[vst][granular][tempo-sync]") {
    // Verify save/load roundtrip preserves timeMode and noteValue
}

TEST_CASE("Granular NoteValue visibility depends on TimeMode", "[vst][granular][tempo-sync]") {
    // Verify NoteValue dropdown is only visible when TimeMode is Synced (FR-009, SC-004)
}
```

## Checklist

- [ ] Add parameter IDs to plugin_ids.h
- [ ] Extend GranularParams struct
- [ ] Add parameter change handlers
- [ ] Register parameters with StringListParameter
- [ ] Add save/load for new parameters
- [ ] Add controller state sync
- [ ] Extend GranularDelay with setTimeMode/setNoteValue
- [ ] Update process() to use tempo when synced
- [ ] Add UI controls in editor.uidesc
- [ ] Implement conditional visibility for NoteValue dropdown (visible only in Synced mode)
- [ ] Write DSP unit tests
- [ ] Write UI E2E tests (including conditional visibility test)
- [ ] Verify all tests pass
- [ ] Run pluginval validation
