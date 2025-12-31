# Quickstart: Spectral Delay Tempo Sync

**Feature**: 041-spectral-tempo-sync
**Date**: 2025-12-31

## Implementation Checklist

### Prerequisites

- [ ] Verify TESTING-GUIDE.md is in context
- [ ] Verify VST-GUIDE.md is in context
- [ ] Current branch is `041-spectral-tempo-sync`

### Phase 1: DSP Layer

1. **Add tempo sync to SpectralDelay class** (`src/dsp/features/spectral_delay.h`)
   - [ ] Include `dsp/systems/delay_engine.h` for TimeMode enum
   - [ ] Add `TimeMode timeMode_ = TimeMode::Free` member
   - [ ] Add `int noteValueIndex_ = 4` member (default: 1/8 note)
   - [ ] Add `setTimeMode(int mode)` method
   - [ ] Add `setNoteValue(int index)` method
   - [ ] Modify `process()` to calculate base delay from tempo when synced

2. **Write tests** (`tests/unit/features/spectral_delay_test.cpp`)
   - [ ] Test: synced mode calculates correct delay at 120 BPM
   - [ ] Test: free mode uses setBaseDelayMs() value
   - [ ] Test: all 10 note values produce correct delays
   - [ ] Test: fallback to 120 BPM when tempo is 0
   - [ ] Test: delay clamped to 2000ms maximum

### Phase 2: Parameter Layer

3. **Add parameter IDs** (`src/plugin_ids.h`)
   - [ ] Add `kSpectralTimeModeId = 211`
   - [ ] Add `kSpectralNoteValueId = 212`

4. **Extend SpectralParams** (`src/parameters/spectral_params.h`)
   - [ ] Add `std::atomic<int> timeMode{0}` to struct
   - [ ] Add `std::atomic<int> noteValue{4}` to struct
   - [ ] Add handleSpectralParamChange cases
   - [ ] Add registerSpectralParams dropdown entries
   - [ ] Add formatSpectralParam cases (handled by StringListParameter)
   - [ ] Add saveSpectralParams entries
   - [ ] Add loadSpectralParams entries
   - [ ] Add syncSpectralParamsToController entries

5. **Connect processor** (`src/processor/processor.cpp`)
   - [ ] Add setTimeMode() call in Spectral mode switch
   - [ ] Add setNoteValue() call in Spectral mode switch

### Phase 3: UI Layer

6. **Add control-tags** (`resources/editor.uidesc`)
   - [ ] Add `SpectralTimeMode` control-tag (211)
   - [ ] Add `SpectralNoteValue` control-tag (212)
   - [ ] Add `SpectralBaseDelayLabel` control-tag (e.g., 9911)

7. **Add UI controls** (`resources/editor.uidesc`)
   - [ ] Add Time Mode COptionMenu in SpectralPanel
   - [ ] Add Note Value COptionMenu in SpectralPanel
   - [ ] Add control-tag to Base Delay label for visibility

8. **Add visibility controller** (`src/controller/controller.h` and `.cpp`)
   - [ ] Add `spectralBaseDelayVisibilityController_` member
   - [ ] Create VisibilityController in didOpen()
   - [ ] Null out controller in willClose()

### Phase 4: Validation

9. **Run tests**
   - [ ] Build: `cmake --build build --config Debug`
   - [ ] Run: `ctest --test-dir build -C Debug --output-on-failure`
   - [ ] All tests pass

10. **Run pluginval**
    - [ ] `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`
    - [ ] Passes at strictness level 5

11. **Commit**
    - [ ] `git add . && git commit -m "feat(spectral): add tempo sync with note values"`

## Reference Files

| Purpose | File |
|---------|------|
| Pattern reference | `src/dsp/features/granular_delay.h:128-177` |
| Parameter pattern | `src/parameters/granular_params.h` |
| Visibility pattern | `src/controller/controller.cpp:824-850` |
| Note value utils | `src/dsp/core/note_value.h` |
| TimeMode enum | `src/dsp/systems/delay_engine.h:37-40` |
