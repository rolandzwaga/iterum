# BBD Delay Perceptual Testing Guide

**Purpose**: Manual verification of audio quality success criteria that cannot be tested through unit tests alone.

**Prerequisites**:
- Built plugin (Debug or Release)
- DAW with plugin hosting capability
- Headphones or studio monitors
- Optional: spectrum analyzer, oscilloscope plugin

---

## Test Environment Setup

### Audio Settings
- Sample rate: 44.1kHz (reference) or 48kHz
- Buffer size: 256-512 samples
- Bit depth: 24-bit or 32-bit float

### Test Signals
Prepare these audio sources:

| Signal | Purpose | How to Create |
|--------|---------|---------------|
| **Impulse** | Transient response | Single sample at 0dBFS, then silence |
| **Click track** | Rhythm clarity | 1kHz sine, 10ms duration, every 500ms |
| **White noise** | Frequency response | Noise generator or audio file |
| **Sine sweep** | Bandwidth verification | 20Hz-20kHz sweep over 10 seconds |
| **Drum loop** | Real-world transients | Any percussive material |
| **Pad/sustained** | Modulation audibility | Sustained chord or synth pad |

---

## SC-001: Darker Character Than Digital

**Requirement**: BBD delay should sound noticeably darker/warmer than a clean digital delay.

### Test Procedure

1. **Setup A/B comparison**:
   - Insert BBDDelay on a track
   - Insert a clean digital delay (stock DAW delay) on a parallel track
   - Match delay times exactly (e.g., 300ms)
   - Set both to 100% wet, 0% feedback

2. **Configure BBDDelay**:
   ```
   Time: 300ms
   Feedback: 0%
   Modulation: 0%
   Age: 0%
   Era: MN3005
   Mix: 100%
   ```

3. **Test with white noise burst**:
   - Play 100ms white noise burst
   - Listen to the delayed output from each
   - BBD should have audibly reduced high frequencies

4. **Test with sine sweep**:
   - Play 20Hz-20kHz sweep
   - BBD output should attenuate above ~10-12kHz at 300ms delay

### Pass Criteria
- [ ] High frequencies (>8kHz) are noticeably reduced in BBD vs digital
- [ ] Character is "warmer" or "darker" without sounding muffled
- [ ] Low-mid frequencies remain clear

### Optional Analysis
- Use spectrum analyzer to compare frequency content
- At 300ms delay, expect -3dB around 10-12kHz vs flat digital response

---

## SC-002: Bandwidth Change Audible with Delay Time

**Requirement**: Changing delay time should produce audible brightness changes.

### Test Procedure

1. **Setup**:
   ```
   Feedback: 50%
   Modulation: 0%
   Age: 0%
   Era: MN3005
   Mix: 100%
   ```

2. **A/B test short vs long delay**:
   - Set Time to 50ms, play percussive material, note brightness
   - Set Time to 800ms, play same material, note brightness
   - Short delay should be noticeably brighter

3. **Sweep test**:
   - While playing sustained material (pad/noise)
   - Slowly sweep Time from 50ms to 900ms
   - Listen for progressive darkening

### Pass Criteria
- [ ] 50ms delay is clearly brighter than 800ms delay
- [ ] Sweeping delay time produces smooth brightness transition
- [ ] No clicks or artifacts during sweep

### Optional Analysis
- Spectrum analyzer shows high-frequency rolloff increasing with delay time
- At 50ms: -3dB ~12-15kHz
- At 900ms: -3dB ~3-4kHz

---

## SC-003: Era Presets Sound Distinct

**Requirement**: Each BBD chip model should have audibly different character.

### Test Procedure

1. **Fixed settings for comparison**:
   ```
   Time: 400ms
   Feedback: 40%
   Modulation: 0%
   Age: 30%
   Mix: 100%
   ```

2. **Cycle through each Era while playing material**:

   | Era | Expected Character |
   |-----|-------------------|
   | MN3005 | Brightest, cleanest, least noise |
   | MN3007 | Slightly darker, bit more noise |
   | MN3205 | Noticeably darker, more lo-fi |
   | SAD1024 | Darkest, most noise, most "vintage" |

3. **Listen for**:
   - Brightness differences (high-frequency content)
   - Noise floor differences
   - Overall "vintage-ness"

### Pass Criteria
- [ ] MN3005 is clearly brighter than SAD1024
- [ ] Each Era has distinct tonal character
- [ ] Noise increases from MN3005 → SAD1024
- [ ] All Eras sound musically useful (not broken)

---

## SC-004: Modulation Creates Pitch Wobble

**Requirement**: Modulation should create audible pitch variation (chorus effect).

### Test Procedure

1. **Setup**:
   ```
   Time: 300ms
   Feedback: 0%
   Age: 0%
   Era: MN3005
   Mix: 100%
   ```

2. **Test A: Modulation Off**:
   - Modulation Depth: 0%
   - Play sustained tone (440Hz sine or held note)
   - Delayed output should be pitch-stable

3. **Test B: Modulation On**:
   - Modulation Depth: 50%
   - Modulation Rate: 0.5Hz
   - Play same sustained tone
   - Delayed output should have audible pitch wobble

4. **Rate test**:
   - Try rates from 0.1Hz (slow, lush) to 5Hz (vibrato-like)
   - Each should produce different movement character

### Pass Criteria
- [ ] 0% modulation = stable pitch
- [ ] 50% modulation = clear pitch wobble
- [ ] Wobble is smooth (no stepping or clicking)
- [ ] Rate control audibly changes wobble speed
- [ ] Maximum depth (100%) doesn't produce artifacts

### Optional Analysis
- Use pitch detection/tuner plugin
- At 50% depth, pitch should vary ±5-10 cents

---

## SC-005: Age Adds Artifacts

**Requirement**: Increasing Age should add audible degradation (noise, bandwidth reduction).

### Test Procedure

1. **Setup**:
   ```
   Time: 500ms
   Feedback: 40%
   Modulation: 0%
   Era: MN3005
   Mix: 100%
   ```

2. **Test A: Age at 0%**:
   - Listen during silence (between notes)
   - Note noise floor level
   - Play material, note brightness

3. **Test B: Age at 50%**:
   - Noise floor should be higher
   - Material should be slightly darker

4. **Test C: Age at 100%**:
   - Noise floor clearly audible
   - Material noticeably darker/more degraded
   - May hear subtle "pumping" on transients

### Pass Criteria
- [ ] Age 0% has minimal noise
- [ ] Age 100% has audible noise floor
- [ ] Higher Age = darker tone
- [ ] Degradation sounds "vintage" not "broken"
- [ ] Effect is gradual across 0-100% range

---

## SC-006: Compander Pumping Audible

**Requirement**: The compander should create audible dynamic artifacts (pumping/breathing).

### Test Procedure

1. **Setup for maximum effect**:
   ```
   Time: 300ms
   Feedback: 50%
   Modulation: 0%
   Age: 100% (compander fully engaged)
   Era: MN3005
   Mix: 100%
   ```

2. **Test with transient material**:
   - Play drum hits with significant silence between
   - Listen for:
     - Attack softening (transients slightly rounded)
     - Release "pumping" (level swell after transient)

3. **Test with dynamics**:
   - Play material with loud and quiet sections
   - Listen for level "breathing" during quiet sections after loud

4. **Compare Age 0% vs 100%**:
   - Age 0%: Dynamics should be more preserved
   - Age 100%: Dynamics should be more "squashed" with pumping

### Pass Criteria
- [ ] Transients at Age 100% are softer than Age 0%
- [ ] Audible "pumping" on decay tails at Age 100%
- [ ] Effect is subtle but noticeable
- [ ] Sound remains musical (not over-compressed)

### Note on Subtlety
Compander artifacts in real BBD units are subtle. The effect should be noticeable on isolated delay signal (100% wet) but blend naturally in a mix context. If you can't hear it clearly:
- Try more extreme transient material (rim shots, hi-hats)
- Use headphones for detail
- Compare directly against Age 0%

---

## Audio Analysis Tools (Optional)

For more objective verification, use these analysis approaches:

### Spectrum Analyzer
- **Purpose**: Verify bandwidth tracking
- **What to look for**: High-frequency rolloff point changing with delay time
- **Tools**: Voxengo SPAN (free), FabFilter Pro-Q, iZotope Insight

### Oscilloscope
- **Purpose**: Verify modulation waveform
- **What to look for**: Triangle-shaped delay time variation
- **Tools**: s(M)exoscope (free), Plugin Doctor

### Pitch Tracker
- **Purpose**: Verify modulation depth
- **What to look for**: Pitch deviation in cents
- **Tools**: Waves Tune, GVST GTune (free)

### Noise Floor Measurement
- **Purpose**: Verify Age/Era noise differences
- **What to look for**: RMS level during silence
- **Tools**: Any loudness meter, Youlean Loudness Meter (free)

---

## Test Results Template

Use this template to document your testing:

```markdown
## BBD Delay Perceptual Test Results

**Date**: YYYY-MM-DD
**Tester**: [Name]
**Plugin Version**: [Git hash or version]
**DAW**: [Name and version]
**Sample Rate**: [44.1k/48k/etc]

### SC-001: Darker Character
- [ ] PASS / [ ] FAIL
- Notes:

### SC-002: Bandwidth Tracking
- [ ] PASS / [ ] FAIL
- Notes:

### SC-003: Era Distinction
- [ ] PASS / [ ] FAIL
- Notes:

### SC-004: Modulation Wobble
- [ ] PASS / [ ] FAIL
- Notes:

### SC-005: Age Degradation
- [ ] PASS / [ ] FAIL
- Notes:

### SC-006: Compander Pumping
- [ ] PASS / [ ] FAIL
- Notes:

### Overall Assessment
[ ] All criteria pass - ready for release
[ ] Some criteria fail - issues documented below

### Issues Found
1.
2.
```

---

## Troubleshooting

### "I can't hear any difference"
- Use headphones, not laptop speakers
- Use 100% wet signal for testing
- Use isolated test signals, not busy mixes
- Increase Age to 50%+ to make effects more obvious

### "The effect seems too subtle"
- This is often correct - real BBD units are subtle
- Compare against a completely clean digital delay to hear the difference
- Era SAD1024 has the most obvious character

### "I hear clicks or artifacts"
- Check buffer size (increase if too small)
- Verify modulation rate isn't too fast for the delay time
- May indicate a bug - document and report

---

## Sign-Off

When all tests pass:

```
Perceptual testing completed on [DATE]
All success criteria verified by: [NAME]
Plugin ready for: [ ] Internal testing [ ] Beta release [ ] Production
```
