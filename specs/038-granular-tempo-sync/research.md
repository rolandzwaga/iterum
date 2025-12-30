# Research: Granular Delay Tempo Sync

**Feature**: 038-granular-tempo-sync
**Date**: 2025-12-30

## Research Summary

All technical unknowns have been resolved. The feature follows an established pattern used by Digital Delay, PingPong Delay, and Reverse Delay modes.

## Resolved Questions

### Q1: How to implement tempo-synced delay time?

**Decision**: Use existing `dropdownToDelayMs()` from `note_value.h`

**Rationale**: This function already exists and is used by Digital Delay mode. It takes a dropdown index (0-9) and tempo BPM, returning the delay time in milliseconds.

**Alternatives Considered**:
- Custom tempo-to-ms calculation: Rejected because `note_value.h` already provides this
- Using BlockContext::tempoToSamples(): More complex, requires sample-to-ms conversion

### Q2: What note values to support?

**Decision**: Use standard dropdown order from `kNoteValueDropdownMapping`:
- Index 0: 1/32
- Index 1: 1/16T (triplet)
- Index 2: 1/16
- Index 3: 1/8T
- Index 4: 1/8 (default)
- Index 5: 1/4T
- Index 6: 1/4
- Index 7: 1/2T
- Index 8: 1/2
- Index 9: 1/1

**Rationale**: This matches the existing pattern in Digital Delay and PingPong Delay, providing UI consistency.

**Alternatives Considered**:
- Extended note values (1/64, 2/1): Not needed for typical granular use cases
- Dotted notes in dropdown: Not included in standard kNoteValueDropdownMapping (matches existing delay modes)

### Q3: Where does TimeMode enum live?

**Decision**: Reuse `TimeMode` from `dsp/systems/delay_engine.h`

**Rationale**: The enum already exists with values `Free` (milliseconds) and `Synced` (note value). No need to duplicate.

**Alternatives Considered**:
- Define new enum in granular_delay.h: Would cause ODR issues
- Use int directly: Less type-safe, harder to read

### Q4: What parameter IDs to use?

**Decision**:
- kGranularTimeModeId = 113
- kGranularNoteValueId = 114

**Rationale**: Granular parameters use range 100-199 (per plugin_ids.h). Current highest is 112 (EnvelopeType). IDs 113-199 are available.

### Q5: How to handle tempo-synced position in process()?

**Decision**: Check timeMode at start of process, calculate synced position using dropdownToDelayMs(), clamp to max delay (2000ms).

**Rationale**: This matches how Digital Delay and Reverse Delay handle it. The granular engine already accepts position in milliseconds via setPosition().

**Implementation Pattern** (from Digital Delay):
```cpp
if (timeMode == TimeMode::Synced) {
    float syncedMs = dropdownToDelayMs(noteValueIndex, ctx.tempoBPM);
    syncedMs = std::clamp(syncedMs, 0.0f, kMaxDelayMs);
    engine_.setPosition(syncedMs);
}
```

### Q6: What happens when tempo is 0 or unavailable?

**Decision**: Use 120 BPM fallback (from kMinTempoSyncBPM/kMaxTempoSyncBPM in note_value.h)

**Rationale**: The `noteToDelayMs()` function already clamps tempo to 20-300 BPM range, preventing division by zero and unreasonable values.

### Q7: UI layout for new controls?

**Decision**: Add TimeMode dropdown and NoteValue dropdown to granular panel in editor.uidesc, following the pattern from Digital Delay panel.

**Rationale**: Consistent UI patterns across delay modes.

## Existing Code to Reuse

| Component | File | Usage |
|-----------|------|-------|
| TimeMode enum | delay_engine.h | Reuse directly |
| dropdownToDelayMs() | note_value.h | Convert note index + tempo to ms |
| getNoteValueFromDropdown() | note_value.h | Get note/modifier pair if needed |
| createDropdownParameterWithDefault() | parameter_helpers.h | Register StringListParameter |

## Implementation Approach

1. **Add parameter IDs** to plugin_ids.h
2. **Extend GranularParams** with timeMode and noteValue atomics
3. **Add parameter handlers** in granular_params.h
4. **Register parameters** with proper StringListParameter for dropdowns
5. **Extend GranularDelay** with setTimeMode(), setNoteValue(), and tempo-aware process()
6. **Update process()** to calculate synced position from tempo when in Synced mode
7. **Add UI controls** in editor.uidesc
8. **Write tests** for tempo sync accuracy at various tempos

## Test Strategy

1. **DSP Unit tests** (granular_delay_tempo_sync_test.cpp):
   - Verify position is exactly `noteToDelayMs()` result when synced
   - Verify position ignores tempo when in Free mode
   - Verify mode switching doesn't cause clicks (smooth transition)
   - Verify clamping to max delay (2000ms)

2. **Mathematical accuracy** (from spec SC-001):
   - At 120 BPM: 1/4 note = 500ms, 1/8 note = 250ms, 1/4T triplet = 333.33ms
   - Accuracy within 0.1ms across 20-300 BPM range

3. **UI E2E tests** (granular_tempo_sync_ui_test.cpp):
   - Verify TimeMode dropdown is registered with correct options ("Free", "Synced")
   - Verify NoteValue dropdown is registered with all 10 note value options
   - Verify parameter IDs (113, 114) are correctly defined
   - Verify dropdown parameters use StringListParameter (not plain Parameter)
   - Verify NoteValue dropdown default is index 4 (1/8 note)
   - Verify TimeMode dropdown default is index 0 (Free)
   - Verify parameter normalization/denormalization roundtrips correctly
   - Verify state persistence saves and restores both new parameters
   - Verify NoteValue dropdown is visible only when TimeMode is Synced (FR-009, SC-004)

## Risk Assessment

**Low Risk**: This feature follows an established pattern with proven components.

| Risk | Mitigation |
|------|------------|
| State persistence | Follow existing pattern in granular_params.h save/load |
| UI dropdown binding | Use StringListParameter (proven pattern) |
| Real-time safety | dropdownToDelayMs() is constexpr/noexcept, no allocations |
