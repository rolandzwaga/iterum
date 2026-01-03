# UI Controls Inventory - Iterum Plugin

**Created**: 2026-01-03
**Purpose**: Inventory of all UI controls per delay mode for UI enhancement planning

---

## CONFIRMED DECISIONS

*Updated as user confirms each mode. This section persists decisions across session compaction.*

### Mode 0: Granular - CONFIRMED
| Control | Decision |
|---------|----------|
| GranularTimeMode | CSegmentButton (2) |
| GranularEnvelopeType | CSegmentButton (4) |
| GranularPitchQuant | CSegmentButton (5) |
| GranularNoteValue | Keep COptionMenu |

### Mode 1: Spectral - CONFIRMED
| Control | Decision |
|---------|----------|
| SpectralTimeMode | CSegmentButton (2) |
| SpectralFFTSize | CSegmentButton (4) |
| SpectralSpreadDirection | CSegmentButton (3) |
| SpectralSpreadCurve | CSegmentButton (2) |
| SpectralNoteValue | Keep COptionMenu |

### Mode 2: Shimmer - CONFIRMED
| Control | Decision |
|---------|----------|
| ShimmerTimeMode | CSegmentButton (2) |
| ShimmerNoteValue | Keep COptionMenu |

### Mode 3: Tape - CONFIRMED
No dropdowns - no changes needed.

### Mode 4: BBD - CONFIRMED
| Control | Decision |
|---------|----------|
| BBDTimeMode | CSegmentButton (2) |
| BBDEra | CSegmentButton (4) |
| BBDNoteValue | Keep COptionMenu |

### Mode 5: Digital - CONFIRMED
| Control | Decision |
|---------|----------|
| DigitalTimeMode | CSegmentButton (2) |
| DigitalEra | CSegmentButton (3) |
| DigitalLimiterCharacter | CSegmentButton (3) |
| DigitalModWaveform | Keep COptionMenu |
| DigitalNoteValue | Keep COptionMenu |

### Mode 6: PingPong - CONFIRMED
| Control | Decision |
|---------|----------|
| PingPongTimeMode | CSegmentButton (2) |
| PingPongLRRatio | Keep COptionMenu |
| PingPongNoteValue | Keep COptionMenu |

### Mode 7: Reverse - CONFIRMED
| Control | Decision |
|---------|----------|
| ReverseTimeMode | CSegmentButton (2) |
| ReversePlaybackMode | CSegmentButton (3) |
| ReverseFilterType | CSegmentButton (3) |
| ReverseNoteValue | Keep COptionMenu |

### Mode 8: MultiTap - CONFIRMED
| Control | Decision |
|---------|----------|
| MultiTapTimeMode | CSegmentButton (2) |
| MultiTapTimingPattern | Keep COptionMenu |
| MultiTapSpatialPattern | Keep COptionMenu |
| MultiTapNoteValue | Keep COptionMenu |

### Mode 9: Freeze - CONFIRMED
| Control | Decision |
|---------|----------|
| FreezeTimeMode | CSegmentButton (2) |
| FreezeFilterType | CSegmentButton (3) |
| FreezeNoteValue | Keep COptionMenu |

### Mode 10: Ducking - CONFIRMED
| Control | Decision |
|---------|----------|
| DuckingTimeMode | CSegmentButton (2) |
| DuckingDuckTarget | CSegmentButton (3) |
| DuckingNoteValue | Keep COptionMenu |

---

## FINAL SUMMARY - ALL DECISIONS CONFIRMED

**Total controls to change: 22**

| Change Type | Count | Controls |
|-------------|-------|----------|
| TimeMode (2 segments) | 10 | Granular, Spectral, Shimmer, BBD, Digital, PingPong, Reverse, MultiTap, Freeze, Ducking |
| FilterType (3 segments) | 2 | Reverse, Freeze |
| Era/Model (3-4 segments) | 2 | BBD (4), Digital (3) |
| Spectral-specific | 3 | FFTSize (4), SpreadDirection (3), SpreadCurve (2) |
| Granular-specific | 2 | EnvelopeType (4), PitchQuant (5) |
| Other | 3 | DigitalLimiterCharacter (3), ReversePlaybackMode (3), DuckingDuckTarget (3) |

**Kept as COptionMenu: 16** (all NoteValue + high-option-count selectors)

---

## Summary Statistics

| Mode | CSlider | COptionMenu | CCheckBox | Total Controls |
|------|---------|-------------|-----------|----------------|
| Granular | 13 | 4 | 1 | 18 |
| Spectral | 7 | 5 | 1 | 13 |
| Shimmer | 9 | 2 | 1 | 12 |
| Tape | 10 | 0 | 4 | 14 |
| BBD | 6 | 3 | 0 | 9 |
| Digital | 7 | 5 | 0 | 12 |
| PingPong | 7 | 3 | 0 | 10 |
| Reverse | 5 | 4 | 1 | 10 |
| MultiTap | 8 | 4 | 0 | 12 |
| Freeze | 9 | 3 | 2 | 14 |
| Ducking | 10 | 4 | 2 | 16 |
| **TOTAL** | **91** | **37** | **12** | **140** |

---

## Enhancement Candidates Summary

### High-Priority: TimeMode Controls (10 instances)

All delay modes (except Tape) have a `TimeMode` dropdown with only **2 options** (Free/Synced).

**Recommendation**: Replace all with CSegmentButton (2 segments, horizontal)

| Mode | Control Tag | Current | Proposed |
|------|-------------|---------|----------|
| Granular | GranularTimeMode | COptionMenu | CSegmentButton (2) |
| Spectral | SpectralTimeMode | COptionMenu | CSegmentButton (2) |
| Shimmer | ShimmerTimeMode | COptionMenu | CSegmentButton (2) |
| BBD | BBDTimeMode | COptionMenu | CSegmentButton (2) |
| Digital | DigitalTimeMode | COptionMenu | CSegmentButton (2) |
| PingPong | PingPongTimeMode | COptionMenu | CSegmentButton (2) |
| Reverse | ReverseTimeMode | COptionMenu | CSegmentButton (2) |
| MultiTap | MultiTapTimeMode | COptionMenu | CSegmentButton (2) |
| Freeze | FreezeTimeMode | COptionMenu | CSegmentButton (2) |
| Ducking | DuckingTimeMode | COptionMenu | CSegmentButton (2) |

### Medium-Priority: Small Option Dropdowns (2-5 options)

These dropdowns have few enough options to be visible all at once:

| Mode | Control Tag | Options | Current | Proposed |
|------|-------------|---------|---------|----------|
| Spectral | SpectralSpreadCurve | 2 (Linear, Logarithmic) | COptionMenu | CSegmentButton (2) |
| Spectral | SpectralSpreadDirection | 3 (Low->High, High->Low, Center Out) | COptionMenu | CSegmentButton (3) |
| Spectral | SpectralFFTSize | 4 (512, 1024, 2048, 4096) | COptionMenu | CSegmentButton (4) |
| Granular | GranularEnvelopeType | 4 (Hann, Trapezoid, Sine, Blackman) | COptionMenu | CSegmentButton (4) |
| Granular | GranularPitchQuant | 5 (Off, Semitones, Octaves, Fifths, Scale) | COptionMenu | CSegmentButton (5) |
| BBD | BBDEra | 4 (MN3005, MN3007, MN3205, SAD1024) | COptionMenu | CSegmentButton (4) or CAnimKnob |
| Digital | DigitalEra | 3 (Pristine, 80s Digital, Lo-Fi) | COptionMenu | CSegmentButton (3) |
| Digital | DigitalLimiterCharacter | 3 (Soft, Medium, Hard) | COptionMenu | CSegmentButton (3) |
| Reverse | ReversePlaybackMode | 3 (Full Reverse, Alternating, Random) | COptionMenu | CSegmentButton (3) |
| Reverse | ReverseFilterType | 3 (LowPass, HighPass, BandPass) | COptionMenu | CSegmentButton (3) |
| Freeze | FreezeFilterType | 3 (LowPass, HighPass, BandPass) | COptionMenu | CSegmentButton (3) |
| Ducking | DuckingDuckTarget | 3 (Output, Feedback, Both) | COptionMenu | CSegmentButton (3) |

### Keep as COptionMenu (6+ options)

These have too many options to display all at once:

| Mode | Control Tag | Options | Reason |
|------|-------------|---------|--------|
| All | *NoteValue | 20 | Too many musical note values |
| Digital | DigitalModWaveform | 6 (Sine, Triangle, Saw Up, Saw Down, Square, Random) | 6 is borderline, could be segmented |
| PingPong | PingPongLRRatio | 7 (1:1, 2:1, 3:2, 4:3, 1:2, 2:3, 3:4) | 7 options too many |
| MultiTap | MultiTapTimingPattern | 20 | Too many preset patterns |
| MultiTap | MultiTapSpatialPattern | 7 (Cascade, Alternating, Centered, Widening, Decaying, Flat, Custom) | 7 options too many |

### Discrete Slider Candidates (Stepped Parameters)

These are discrete parameters that might benefit from stepped knobs:

| Mode | Control Tag | Range | Proposed |
|------|-------------|-------|----------|
| MultiTap | MultiTapTapCount | 2-16 (15 values) | CAnimKnob with 15 frames (stepped feel) |
| Shimmer | ShimmerPitchSemitones | -24 to +24 (49 values) | Keep CSlider (too many steps for visual knob) |
| Freeze | FreezePitchSemitones | -24 to +24 (49 values) | Keep CSlider (too many steps for visual knob) |

---

## Detailed Control Inventory By Mode

### Mode 0: Granular

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| GranularDelayTime | CSlider | Continuous | OK |
| GranularFeedback | CSlider | Continuous | OK |
| GranularDryWet | CSlider | Continuous | OK |
| GranularGrainSize | CSlider | Continuous | OK |
| GranularDensity | CSlider | Continuous | OK |
| GranularPitch | CSlider | Continuous | OK |
| GranularJitter | CSlider | Continuous | OK |
| GranularPitchSpray | CSlider | Continuous | OK |
| GranularPositionSpray | CSlider | Continuous | OK |
| GranularPanSpray | CSlider | Continuous | OK |
| GranularReverseProb | CSlider | Continuous | OK |
| GranularTexture | CSlider | Continuous | OK |
| GranularStereoWidth | CSlider | Continuous | OK |
| GranularTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| GranularNoteValue | COptionMenu | 20 | OK (too many) |
| GranularEnvelopeType | COptionMenu | 4 (Hann, Trapezoid, Sine, Blackman) | **CSegmentButton** |
| GranularPitchQuant | COptionMenu | 5 (Off, Semitones, Octaves, Fifths, Scale) | **CSegmentButton** |
| GranularFreeze | CCheckBox | Binary | OK |

---

### Mode 1: Spectral

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| SpectralBaseDelay | CSlider | Continuous | OK |
| SpectralFeedback | CSlider | Continuous | OK |
| SpectralDryWet | CSlider | Continuous | OK |
| SpectralSpread | CSlider | Continuous | OK |
| SpectralFeedbackTilt | CSlider | Continuous | OK |
| SpectralDiffusion | CSlider | Continuous | OK |
| SpectralStereoWidth | CSlider | Continuous | OK |
| SpectralTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| SpectralNoteValue | COptionMenu | 20 | OK (too many) |
| SpectralFFTSize | COptionMenu | 4 (512, 1024, 2048, 4096) | **CSegmentButton** |
| SpectralSpreadDirection | COptionMenu | 3 (Low->High, High->Low, Center Out) | **CSegmentButton** |
| SpectralSpreadCurve | COptionMenu | 2 (Linear, Logarithmic) | **CSegmentButton** |
| SpectralFreeze | CCheckBox | Binary | OK |

---

### Mode 2: Shimmer

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| ShimmerDelayTime | CSlider | Continuous | OK |
| ShimmerFeedback | CSlider | Continuous | OK |
| ShimmerDryWet | CSlider | Continuous | OK |
| ShimmerPitchSemitones | CSlider | -24 to +24 | OK (49 values too many for stepped) |
| ShimmerPitchCents | CSlider | Continuous | OK |
| ShimmerShimmerMix | CSlider | Continuous | OK |
| ShimmerDiffusionAmount | CSlider | Continuous | OK |
| ShimmerDiffusionSize | CSlider | Continuous | OK |
| ShimmerFilterCutoff | CSlider | Continuous | OK |
| ShimmerTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| ShimmerNoteValue | COptionMenu | 20 | OK (too many) |
| ShimmerFilterEnabled | CCheckBox | Binary | OK |

---

### Mode 3: Tape

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| TapeFeedback | CSlider | Continuous | OK |
| TapeMix | CSlider | Continuous | OK |
| TapeMotorSpeed | CSlider | Continuous | OK |
| TapeMotorInertia | CSlider | Continuous | OK |
| TapeWear | CSlider | Continuous | OK |
| TapeSaturation | CSlider | Continuous | OK |
| TapeAge | CSlider | Continuous | OK |
| TapeSpliceIntensity | CSlider | Continuous | OK |
| TapeHead1Level | CSlider | Continuous | OK |
| TapeHead1Pan | CSlider | Continuous | OK |
| TapeHead2Level | CSlider | Continuous | OK |
| TapeHead2Pan | CSlider | Continuous | OK |
| TapeHead3Level | CSlider | Continuous | OK |
| TapeHead3Pan | CSlider | Continuous | OK |
| TapeSpliceEnabled | CCheckBox | Binary | OK |
| TapeHead1Enabled | CCheckBox | Binary | OK |
| TapeHead2Enabled | CCheckBox | Binary | OK |
| TapeHead3Enabled | CCheckBox | Binary | OK |

**Note**: Tape mode has no dropdown controls - all binary or continuous.

---

### Mode 4: BBD

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| BBDDelayTime | CSlider | Continuous | OK |
| BBDFeedback | CSlider | Continuous | OK |
| BBDMix | CSlider | Continuous | OK |
| BBDAge | CSlider | Continuous | OK |
| BBDModulationDepth | CSlider | Continuous | OK |
| BBDModulationRate | CSlider | Continuous | OK |
| BBDTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| BBDNoteValue | COptionMenu | 20 | OK (too many) |
| BBDEra | COptionMenu | 4 (MN3005, MN3007, MN3205, SAD1024) | **CSegmentButton or CAnimKnob (4 frames)** |

---

### Mode 5: Digital

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| DigitalDelayTime | CSlider | Continuous | OK |
| DigitalFeedback | CSlider | Continuous | OK |
| DigitalMix | CSlider | Continuous | OK |
| DigitalAge | CSlider | Continuous | OK |
| DigitalModDepth | CSlider | Continuous | OK |
| DigitalModRate | CSlider | Continuous | OK |
| DigitalWidth | CSlider | Continuous | OK |
| DigitalTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| DigitalNoteValue | COptionMenu | 20 | OK (too many) |
| DigitalEra | COptionMenu | 3 (Pristine, 80s Digital, Lo-Fi) | **CSegmentButton** |
| DigitalLimiterCharacter | COptionMenu | 3 (Soft, Medium, Hard) | **CSegmentButton** |
| DigitalModWaveform | COptionMenu | 6 (Sine, Triangle, Saw Up, Saw Down, Square, Random) | Borderline - consider CSegmentButton (6) or keep |

---

### Mode 6: PingPong

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| PingPongDelayTime | CSlider | Continuous | OK |
| PingPongFeedback | CSlider | Continuous | OK |
| PingPongMix | CSlider | Continuous | OK |
| PingPongWidth | CSlider | Continuous | OK |
| PingPongCrossFeedback | CSlider | Continuous | OK |
| PingPongModDepth | CSlider | Continuous | OK |
| PingPongModRate | CSlider | Continuous | OK |
| PingPongTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| PingPongNoteValue | COptionMenu | 20 | OK (too many) |
| PingPongLRRatio | COptionMenu | 7 (1:1, 2:1, 3:2, 4:3, 1:2, 2:3, 3:4) | OK (7 too many) |

---

### Mode 7: Reverse

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| ReverseFeedback | CSlider | Continuous | OK |
| ReverseDryWet | CSlider | Continuous | OK |
| ReverseChunkSize | CSlider | Continuous | OK |
| ReverseCrossfade | CSlider | Continuous | OK |
| ReverseFilterCutoff | CSlider | Continuous | OK |
| ReversePlaybackMode | COptionMenu | 3 (Full Reverse, Alternating, Random) | **CSegmentButton** |
| ReverseTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| ReverseNoteValue | COptionMenu | 20 | OK (too many) |
| ReverseFilterType | COptionMenu | 3 (LowPass, HighPass, BandPass) | **CSegmentButton** |
| ReverseFilterEnabled | CCheckBox | Binary | OK |

---

### Mode 8: MultiTap

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| MultiTapBaseTime | CSlider | Continuous | OK |
| MultiTapTempo | CSlider | Continuous | OK |
| MultiTapTapCount | CSlider | 2-16 (15 discrete values) | Consider CAnimKnob (stepped) |
| MultiTapFeedback | CSlider | Continuous | OK |
| MultiTapDryWet | CSlider | Continuous | OK |
| MultiTapFeedbackLPCutoff | CSlider | Continuous | OK |
| MultiTapFeedbackHPCutoff | CSlider | Continuous | OK |
| MultiTapMorphTime | CSlider | Continuous | OK |
| MultiTapTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| MultiTapNoteValue | COptionMenu | 20 | OK (too many) |
| MultiTapTimingPattern | COptionMenu | 20 | OK (too many) |
| MultiTapSpatialPattern | COptionMenu | 7 | OK (7 too many) |

---

### Mode 9: Freeze

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| FreezeEnabled | CCheckBox | Binary | OK |
| FreezeDecay | CSlider | Continuous | OK |
| FreezeFeedback | CSlider | Continuous | OK |
| FreezeDelayTime | CSlider | Continuous | OK |
| FreezeDryWet | CSlider | Continuous | OK |
| FreezePitchSemitones | CSlider | -24 to +24 | OK (49 values too many) |
| FreezePitchCents | CSlider | Continuous | OK |
| FreezeShimmerMix | CSlider | Continuous | OK |
| FreezeDiffusionAmount | CSlider | Continuous | OK |
| FreezeDiffusionSize | CSlider | Continuous | OK |
| FreezeFilterCutoff | CSlider | Continuous | OK |
| FreezeTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| FreezeNoteValue | COptionMenu | 20 | OK (too many) |
| FreezeFilterType | COptionMenu | 3 (LowPass, HighPass, BandPass) | **CSegmentButton** |
| FreezeFilterEnabled | CCheckBox | Binary | OK |

---

### Mode 10: Ducking

| Control Tag | Current Type | Options/Range | Recommendation |
|-------------|--------------|---------------|----------------|
| DuckingEnabled | CCheckBox | Binary | OK |
| DuckingThreshold | CSlider | Continuous | OK |
| DuckingDuckAmount | CSlider | Continuous | OK |
| DuckingAttackTime | CSlider | Continuous | OK |
| DuckingReleaseTime | CSlider | Continuous | OK |
| DuckingHoldTime | CSlider | Continuous | OK |
| DuckingDelayTime | CSlider | Continuous | OK |
| DuckingFeedback | CSlider | Continuous | OK |
| DuckingDryWet | CSlider | Continuous | OK |
| DuckingSidechainFilterCutoff | CSlider | Continuous | OK |
| DuckingTimeMode | COptionMenu | 2 (Free, Synced) | **CSegmentButton** |
| DuckingNoteValue | COptionMenu | 20 | OK (too many) |
| DuckingDuckTarget | COptionMenu | 3 (Output, Feedback, Both) | **CSegmentButton** |
| DuckingSidechainFilterEnabled | CCheckBox | Binary | OK |

---

## Implementation Priority

### Phase 1: TimeMode Controls (Quick Win - 10 changes)
All TimeMode dropdowns are identical (2 options) - create one reusable template.

### Phase 2: Filter Type Controls (3 changes)
Reverse, Freeze filter types are identical (3 options) - create one reusable template.

### Phase 3: Era/Model Selectors (2 changes)
BBD Era (4 chips), Digital Era (3 eras) - good for visual identity.

### Phase 4: Spectral-Specific Controls (3 changes)
FFTSize (4), SpreadDirection (3), SpreadCurve (2) - Spectral mode enhancements.

### Phase 5: Remaining Medium-Priority (6 changes)
GranularEnvelopeType, GranularPitchQuant, DigitalLimiterCharacter, ReversePlaybackMode, DuckingDuckTarget.

### Phase 6: Optional (Borderline Cases)
DigitalModWaveform (6 options) - decide if CSegmentButton works visually.

---

## Total Changes Summary

| Category | Count | Control Type Change |
|----------|-------|---------------------|
| TimeMode (all modes) | 10 | COptionMenu -> CSegmentButton (2) |
| Filter Type (Reverse, Freeze) | 2 | COptionMenu -> CSegmentButton (3) |
| Era/Model (BBD, Digital) | 2 | COptionMenu -> CSegmentButton (3-4) |
| Spectral-specific | 3 | COptionMenu -> CSegmentButton (2-4) |
| Other small dropdowns | 5 | COptionMenu -> CSegmentButton (3-5) |
| **TOTAL** | **22** | |

---

## Next Steps

1. **User reviews this inventory** and decides which changes to implement
2. **Step through each mode** together to finalize control choices
3. **Create bitmap assets** if using CAnimKnob for any controls
4. **Create implementation spec** with detailed changes per file
