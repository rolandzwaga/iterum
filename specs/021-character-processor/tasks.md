# Implementation Tasks: Character Processor

**Feature**: 021-character-processor
**Generated**: 2025-12-25
**Branch**: `021-character-processor`
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Task Overview

| Phase | Tasks | Parallel | Estimated |
|-------|-------|----------|-----------|
| Setup | 4 | 2 | Foundation |
| Layer 1 Primitives | 12 | 4 | BitCrusher, SampleRateReducer |
| Layer 3 System | 24 | 6 | CharacterProcessor |
| User Stories | 12 | 4 | US1-US6 acceptance |
| Polish | 6 | 2 | Performance, docs |
| **Total** | **58** | **18** | — |

## Execution Rules

1. **Test-First**: Write failing tests BEFORE implementation (Principle XII)
2. **Layer Order**: Complete Layer 1 primitives before Layer 3 system
3. **Parallel [P]**: Tasks marked [P] can run concurrently within same phase
4. **Sequential**: Unmarked tasks must complete in order
5. **Commit Points**: Commit after each completed user story

---

## Phase 0: Setup (4 tasks)

### T001: Verify TESTING-GUIDE.md in context
- [ ] Read `specs/TESTING-GUIDE.md` if not in context
- **Tags**: `[setup]`

### T002: Create test directory structure [P]
- [ ] Create `tests/unit/primitives/` if not exists
- [ ] Create `tests/unit/systems/` if not exists
- **Files**: `tests/unit/primitives/`, `tests/unit/systems/`
- **Tags**: `[setup]`

### T003: Create source directory structure [P]
- [ ] Verify `src/dsp/primitives/` exists
- [ ] Verify `src/dsp/systems/` exists
- **Files**: `src/dsp/primitives/`, `src/dsp/systems/`
- **Tags**: `[setup]`

### T004: Update CMakeLists.txt for new test files
- [ ] Add `bit_crusher_test.cpp` to dsp_tests
- [ ] Add `sample_rate_reducer_test.cpp` to dsp_tests
- [ ] Add `character_processor_test.cpp` to dsp_tests
- **Files**: `tests/CMakeLists.txt`
- **Tags**: `[setup]`

---

## Phase 1: Layer 1 Primitives - BitCrusher (6 tasks)

### T010: Write BitCrusher foundational tests
- [ ] Test default bit depth is 16 bits (no quantization noise)
- [ ] Test setBitDepth clamps to [4, 16] range
- [ ] Test setDither clamps to [0, 1] range
- [ ] Test process(float) signature exists
- [ ] Test process(float*, size_t) signature exists
- **Files**: `tests/unit/primitives/bit_crusher_test.cpp`
- **Tags**: `[layer1]`, `[bitcrusher]`, `[foundational]`
- **Refs**: FR-014, data-model.md BitCrusher class

### T011: Implement BitCrusher skeleton
- [ ] Create header with class declaration matching contract
- [ ] Implement prepare(), reset()
- [ ] Implement parameter setters with clamping
- [ ] Implement parameter getters
- **Files**: `src/dsp/primitives/bit_crusher.h`
- **Tags**: `[layer1]`, `[bitcrusher]`
- **Refs**: data-model.md, contracts/character_processor.h

### T012: Write BitCrusher quantization tests
- [ ] Test 8-bit mode produces ~256 quantization levels
- [ ] Test 4-bit mode produces ~16 quantization levels
- [ ] Test 16-bit mode is nearly transparent (<0.001% distortion)
- [ ] Test fractional bit depths work (e.g., 10.5 bits)
- **Files**: `tests/unit/primitives/bit_crusher_test.cpp`
- **Tags**: `[layer1]`, `[bitcrusher]`, `[US3]`
- **Refs**: FR-014, SC-007

### T013: Implement BitCrusher quantization
- [ ] Implement quantization: `round(input * levels) / levels`
- [ ] Handle fractional bit depths via floating-point levels
- **Files**: `src/dsp/primitives/bit_crusher.h`
- **Tags**: `[layer1]`, `[bitcrusher]`
- **Refs**: research.md Section 4

### T014: Write BitCrusher dither tests
- [ ] Test dither=0 produces no noise (deterministic output)
- [ ] Test dither=1 adds TPDF noise before quantization
- [ ] Test dither smooths quantization noise spectrum
- **Files**: `tests/unit/primitives/bit_crusher_test.cpp`
- **Tags**: `[layer1]`, `[bitcrusher]`, `[US3]`
- **Refs**: FR-016, research.md Section 4

### T015: Implement BitCrusher TPDF dither
- [ ] Add RNG state for dither generation
- [ ] Implement TPDF: `(rng1 + rng2) * ditherAmount / levels`
- [ ] Apply dither before quantization
- **Files**: `src/dsp/primitives/bit_crusher.h`
- **Tags**: `[layer1]`, `[bitcrusher]`

### T016: Verify BitCrusher tests pass
- [ ] Run: `dsp_tests.exe "[bitcrusher]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer1]`, `[bitcrusher]`, `[verify]`

---

## Phase 2: Layer 1 Primitives - SampleRateReducer (6 tasks)

### T020: Write SampleRateReducer foundational tests
- [ ] Test default reduction factor is 1.0 (no reduction)
- [ ] Test setReductionFactor clamps to [1, 8] range
- [ ] Test process(float) signature exists
- [ ] Test process(float*, size_t) signature exists
- [ ] Test reset() clears hold state
- **Files**: `tests/unit/primitives/sample_rate_reducer_test.cpp`
- **Tags**: `[layer1]`, `[samplerate]`, `[foundational]`
- **Refs**: FR-015, data-model.md SampleRateReducer class

### T021: Implement SampleRateReducer skeleton
- [ ] Create header with class declaration matching contract
- [ ] Implement prepare(), reset()
- [ ] Implement setReductionFactor with clamping
- [ ] Implement getReductionFactor
- **Files**: `src/dsp/primitives/sample_rate_reducer.h`
- **Tags**: `[layer1]`, `[samplerate]`

### T022: Write SampleRateReducer sample-and-hold tests
- [ ] Test factor=1 passes audio unchanged
- [ ] Test factor=2 holds each sample for 2 outputs
- [ ] Test factor=4 holds each sample for 4 outputs
- [ ] Test fractional factors work (e.g., 2.5)
- **Files**: `tests/unit/primitives/sample_rate_reducer_test.cpp`
- **Tags**: `[layer1]`, `[samplerate]`, `[US3]`
- **Refs**: FR-015, research.md Section 5

### T023: Implement SampleRateReducer sample-and-hold
- [ ] Implement hold counter with fractional support
- [ ] Update holdValue when counter exceeds factor
- [ ] Use counter subtraction for fractional accuracy
- **Files**: `src/dsp/primitives/sample_rate_reducer.h`
- **Tags**: `[layer1]`, `[samplerate]`

### T024: Write SampleRateReducer aliasing tests
- [ ] Test high-frequency content creates aliasing artifacts
- [ ] Test aliasing increases with higher reduction factors
- **Files**: `tests/unit/primitives/sample_rate_reducer_test.cpp`
- **Tags**: `[layer1]`, `[samplerate]`, `[US3]`

### T025: Verify SampleRateReducer tests pass
- [ ] Run: `dsp_tests.exe "[samplerate]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer1]`, `[samplerate]`, `[verify]`

### T026: Commit Layer 1 primitives
- [ ] `git add src/dsp/primitives/bit_crusher.h src/dsp/primitives/sample_rate_reducer.h`
- [ ] `git add tests/unit/primitives/bit_crusher_test.cpp tests/unit/primitives/sample_rate_reducer_test.cpp`
- [ ] `git commit -m "feat(dsp): add BitCrusher and SampleRateReducer Layer 1 primitives"`
- **Tags**: `[layer1]`, `[commit]`

---

## Phase 3: Layer 3 System - CharacterProcessor Core (8 tasks)

### T030: Write CharacterProcessor lifecycle tests
- [ ] Test default construction creates valid object
- [ ] Test prepare() accepts sampleRate and maxBlockSize
- [ ] Test reset() clears state without reallocation
- [ ] Test default mode is Clean (FR-017)
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[foundational]`
- **Refs**: FR-004, FR-005, FR-006, FR-017

### T031: Implement CharacterProcessor skeleton
- [ ] Create header with class declaration matching contract
- [ ] Add CharacterMode enum
- [ ] Add constants (kMinCrossfadeTimeMs, etc.)
- [ ] Implement constructor, prepare(), reset()
- [ ] Add private member variables for state
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`
- **Refs**: contracts/character_processor.h

### T032: Write CharacterProcessor mode selection tests
- [ ] Test setMode(CharacterMode::Tape) sets mode
- [ ] Test setMode(CharacterMode::BBD) sets mode
- [ ] Test setMode(CharacterMode::DigitalVintage) sets mode
- [ ] Test setMode(CharacterMode::Clean) sets mode
- [ ] Test getMode() returns current mode
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[mode]`
- **Refs**: FR-001, FR-002

### T033: Implement CharacterProcessor mode selection
- [ ] Implement setMode() with crossfade initiation
- [ ] Implement getMode()
- [ ] Add mode state tracking (current, target)
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`

### T034: Write Clean mode passthrough tests (US4)
- [ ] Test Clean mode output equals input within 0.001dB
- [ ] Test Clean mode adds no latency
- [ ] Test Clean mode works with mono buffer
- [ ] Test Clean mode works with stereo buffers
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[clean]`, `[US4]`
- **Refs**: FR-017, US4 Acceptance Scenarios

### T035: Implement Clean mode processing
- [ ] Implement process() for Clean mode (passthrough)
- [ ] Implement processStereo() for Clean mode
- [ ] Ensure no allocations in process path (FR-019)
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`

### T036: Write NaN input handling tests
- [ ] Test NaN input produces 0.0 output
- [ ] Test NaN in buffer is replaced with 0.0
- [ ] Test processing continues after NaN
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[edge]`
- **Refs**: FR-020

### T037: Implement NaN input handling
- [ ] Add NaN detection using bit-level check (per CLAUDE.md)
- [ ] Replace NaN with 0.0f before processing
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`

---

## Phase 4: Layer 3 System - Mode Transitions (4 tasks)

### T040: Write crossfade transition tests (US5)
- [ ] Test mode change initiates crossfade
- [ ] Test isCrossfading() returns true during transition
- [ ] Test transition completes within 50ms (default)
- [ ] Test no clicks during transition
- [ ] Test rapid mode switching (10x/sec) produces no artifacts
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[crossfade]`, `[US5]`
- **Refs**: FR-003, SC-002, US5 Acceptance Scenarios

### T041: Implement crossfade mechanism
- [ ] Add dual processing paths (old mode, new mode)
- [ ] Use OnePoleSmoother for crossfade coefficient
- [ ] Implement equal-power crossfade blending
- [ ] Add setCrossfadeTime() parameter
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`
- **Refs**: research.md Section 1

### T042: Write crossfade time configuration tests
- [ ] Test setCrossfadeTime(30.0f) changes duration
- [ ] Test crossfade time clamps to [10, 100] ms
- [ ] Test crossfade time affects transition speed
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[crossfade]`

### T043: Verify crossfade tests pass
- [ ] Run: `dsp_tests.exe "[crossfade]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer3]`, `[crossfade]`, `[verify]`

---

## Phase 5: Layer 3 System - Tape Mode (6 tasks)

### T050: Write Tape mode saturation tests (US1)
- [ ] Test Tape mode adds harmonic distortion
- [ ] Test setTapeSaturation(0.0f) is nearly transparent
- [ ] Test setTapeSaturation(1.0f) adds >1% THD
- [ ] Test saturation amount scales THD (SC-005: 0.1% to 5%)
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[tape]`, `[US1]`
- **Refs**: FR-007, SC-005, US1 Acceptance Scenario 1

### T051: Implement Tape mode saturation
- [ ] Integrate SaturationProcessor with Tape curve
- [ ] Wire setTapeSaturation() to processor drive
- [ ] Use OnePoleSmoother for parameter changes
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[tape]`

### T052: Write Tape mode wow/flutter tests (US1)
- [ ] Test wow/flutter adds pitch modulation
- [ ] Test setTapeWowRate() controls slow modulation (0.1-10Hz)
- [ ] Test setTapeWowDepth() controls modulation amount
- [ ] Test setTapeFlutterRate() controls fast modulation
- [ ] Test setTapeFlutterDepth() controls flutter amount
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[tape]`, `[US1]`
- **Refs**: FR-008, US1 Acceptance Scenario 2

### T053: Implement Tape mode wow/flutter
- [ ] Add two LFO instances (wow + flutter)
- [ ] Add short delay line for pitch modulation (0-5ms)
- [ ] Modulate delay time with LFO outputs
- [ ] Wire rate/depth setters to LFO parameters
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[tape]`
- **Refs**: research.md Section 2

### T054: Write Tape mode hiss and rolloff tests (US1)
- [ ] Test setTapeHissLevel() adds noise floor
- [ ] Test hiss level clamps to [-144, -40] dB
- [ ] Test setTapeRolloffFreq() attenuates high frequencies
- [ ] Test rolloff at 8kHz produces >6dB attenuation at 10kHz
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[tape]`, `[US1]`
- **Refs**: FR-009, FR-010, US1 Acceptance Scenario 3

### T055: Implement Tape mode hiss and rolloff
- [ ] Add NoiseGenerator with TapeHiss type
- [ ] Add MultimodeFilter with HighShelf rolloff
- [ ] Wire hiss level to noise gain
- [ ] Wire rolloff frequency to filter cutoff
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[tape]`

### T056: Verify Tape mode tests pass
- [ ] Run: `dsp_tests.exe "[tape]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer3]`, `[tape]`, `[verify]`

### T057: Commit Tape mode
- [ ] `git add src/dsp/systems/character_processor.h tests/unit/systems/character_processor_test.cpp`
- [ ] `git commit -m "feat(dsp): add Tape mode character processing (US1)"`
- **Tags**: `[layer3]`, `[tape]`, `[commit]`

---

## Phase 6: Layer 3 System - BBD Mode (4 tasks)

### T060: Write BBD mode bandwidth tests (US2)
- [ ] Test BBD mode limits bandwidth
- [ ] Test setBBDBandwidth() controls cutoff (2-15kHz)
- [ ] Test bandwidth limiting achieves -12dB at 2x cutoff (SC-006)
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[bbd]`, `[US2]`
- **Refs**: FR-011, SC-006, US2 Acceptance Scenario 1

### T061: Implement BBD mode bandwidth limiting
- [ ] Add MultimodeFilter with Lowpass mode
- [ ] Wire setBBDBandwidth() to filter cutoff
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[bbd]`

### T062: Write BBD mode clock noise and saturation tests (US2)
- [ ] Test setBBDClockNoiseLevel() adds high-frequency noise
- [ ] Test clock noise level clamps to [-144, -50] dB
- [ ] Test setBBDSaturation() adds soft clipping
- [ ] Test saturation is softer than Tape mode
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[bbd]`, `[US2]`
- **Refs**: FR-012, FR-013, US2 Acceptance Scenarios 2-3

### T063: Implement BBD mode clock noise and saturation
- [ ] Add NoiseGenerator with RadioStatic type for clock noise
- [ ] Add SaturationProcessor with Tube/Diode curve
- [ ] Wire clock noise level to noise gain
- [ ] Wire saturation amount to drive
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[bbd]`
- **Refs**: research.md Section 3

### T064: Verify BBD mode tests pass
- [ ] Run: `dsp_tests.exe "[bbd]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer3]`, `[bbd]`, `[verify]`

### T065: Commit BBD mode
- [ ] `git add src/dsp/systems/character_processor.h tests/unit/systems/character_processor_test.cpp`
- [ ] `git commit -m "feat(dsp): add BBD mode character processing (US2)"`
- **Tags**: `[layer3]`, `[bbd]`, `[commit]`

---

## Phase 7: Layer 3 System - Digital Vintage Mode (4 tasks)

### T070: Write Digital Vintage mode bit reduction tests (US3)
- [ ] Test setDigitalBitDepth() controls quantization
- [ ] Test 8-bit mode produces ~48dB SNR (SC-007)
- [ ] Test bit depth clamps to [4, 16]
- [ ] Test setDigitalDitherAmount() controls dither
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[digital]`, `[US3]`
- **Refs**: FR-014, FR-016, SC-007, US3 Acceptance Scenarios 1, 3

### T071: Implement Digital Vintage mode bit reduction
- [ ] Integrate BitCrusher primitive
- [ ] Wire setDigitalBitDepth() to crusher
- [ ] Wire setDigitalDitherAmount() to crusher
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[digital]`

### T072: Write Digital Vintage mode sample rate reduction tests (US3)
- [ ] Test setDigitalSampleRateReduction() controls aliasing
- [ ] Test factor=4 creates audible aliasing
- [ ] Test factor=1 is transparent
- [ ] Test reduction factor clamps to [1, 8]
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[digital]`, `[US3]`
- **Refs**: FR-015, US3 Acceptance Scenario 2

### T073: Implement Digital Vintage mode sample rate reduction
- [ ] Integrate SampleRateReducer primitive
- [ ] Wire setDigitalSampleRateReduction() to reducer
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[digital]`

### T074: Verify Digital Vintage mode tests pass
- [ ] Run: `dsp_tests.exe "[digital]" --reporter compact`
- [ ] All assertions pass
- **Tags**: `[layer3]`, `[digital]`, `[verify]`

### T075: Commit Digital Vintage mode
- [ ] `git add src/dsp/systems/character_processor.h tests/unit/systems/character_processor_test.cpp`
- [ ] `git commit -m "feat(dsp): add Digital Vintage mode character processing (US3)"`
- **Tags**: `[layer3]`, `[digital]`, `[commit]`

---

## Phase 8: Parameter Smoothing and Automation (4 tasks)

### T080: Write parameter smoothing tests (US6)
- [ ] Test parameter changes don't produce zipper noise
- [ ] Test 20ms smoothing time for all parameters
- [ ] Test rapid parameter automation (100Hz rate) is glitch-free (SC-004)
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[smoothing]`, `[US6]`
- **Refs**: FR-018, SC-004, US6 Acceptance Scenarios

### T081: Implement parameter smoothing
- [ ] Add OnePoleSmoother for each mode's parameters
- [ ] Configure 20ms smoothing time constant
- [ ] Apply smoothed values in process()
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`, `[smoothing]`

### T082: Write latency reporting tests
- [ ] Test getLatency() returns correct sample count
- [ ] Test latency accounts for wow/flutter delay
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`

### T083: Implement latency reporting
- [ ] Calculate total latency from internal components
- [ ] Implement getLatency()
- **Files**: `src/dsp/systems/character_processor.h`
- **Tags**: `[layer3]`, `[character]`

---

## Phase 9: Polish and Verification (6 tasks)

### T090: Write performance tests
- [ ] Test processing 512 samples at 44.1kHz < 1% CPU (SC-003)
- [ ] Test each mode individually for CPU budget
- [ ] Verify Clean mode is minimal CPU
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[performance]`
- **Refs**: SC-003

### T091: Write spectral analysis tests
- [ ] Test each mode produces distinct spectral characteristics (SC-001)
- [ ] Compare Tape vs BBD vs Digital vs Clean spectra
- **Files**: `tests/unit/systems/character_processor_test.cpp`
- **Tags**: `[layer3]`, `[character]`, `[spectral]`
- **Refs**: SC-001

### T092: Run full test suite
- [ ] Run: `dsp_tests.exe "[character]" --reporter compact`
- [ ] Run: `dsp_tests.exe "[bitcrusher]" --reporter compact`
- [ ] Run: `dsp_tests.exe "[samplerate]" --reporter compact`
- [ ] All tests pass
- **Tags**: `[verify]`, `[full]`

### T093: Run VST3 validator
- [ ] Build plugin: `cmake --build build --config Debug --target Iterum`
- [ ] Run: `validator.exe "build\VST3\Debug\Iterum.vst3"`
- [ ] No validation errors
- **Tags**: `[verify]`, `[vst3]`

### T094: Final commit
- [ ] `git add -A`
- [ ] `git commit -m "feat(dsp): complete CharacterProcessor (021-character-processor)"`
- **Tags**: `[commit]`, `[final]`

### T095: Update compliance table
- [ ] Fill Implementation Verification table in spec.md
- [ ] Verify all FR-xxx requirements MET
- [ ] Verify all SC-xxx success criteria MET
- [ ] Update Overall Status to COMPLETE
- **Files**: `specs/021-character-processor/spec.md`
- **Tags**: `[docs]`, `[final]`

---

## Task Dependencies

```
T001 ─┬─ T002 ─┬─ T004 ─── T010...T016 (BitCrusher)
      └─ T003 ─┘           │
                           ├─ T020...T026 (SampleRateReducer)
                           │
                           └─ T030...T037 (CharacterProcessor Core)
                                    │
                              T040...T043 (Crossfade)
                                    │
                              T050...T057 (Tape Mode)
                                    │
                              T060...T065 (BBD Mode)
                                    │
                              T070...T075 (Digital Vintage Mode)
                                    │
                              T080...T083 (Smoothing)
                                    │
                              T090...T095 (Polish)
```

---

## Requirements Traceability

| Requirement | Tasks |
|-------------|-------|
| FR-001 (Four modes) | T032, T033 |
| FR-002 (setMode) | T032, T033 |
| FR-003 (Crossfade) | T040, T041, T042 |
| FR-004 (prepare) | T030, T031 |
| FR-005 (process) | T034, T035 |
| FR-006 (reset) | T030, T031 |
| FR-007 (Tape saturation) | T050, T051 |
| FR-008 (Tape wow/flutter) | T052, T053 |
| FR-009 (Tape hiss) | T054, T055 |
| FR-010 (Tape rolloff) | T054, T055 |
| FR-011 (BBD bandwidth) | T060, T061 |
| FR-012 (BBD clock noise) | T062, T063 |
| FR-013 (BBD saturation) | T062, T063 |
| FR-014 (Digital bit depth) | T010-T016, T070, T071 |
| FR-015 (Digital SR reduction) | T020-T026, T072, T073 |
| FR-016 (Digital dither) | T014, T015, T070, T071 |
| FR-017 (Clean mode) | T034, T035 |
| FR-018 (Parameter smoothing) | T080, T081 |
| FR-019 (Real-time safe) | T035, T090 |
| FR-020 (NaN handling) | T036, T037 |
| SC-001 (Distinct modes) | T091 |
| SC-002 (50ms transition) | T040, T041 |
| SC-003 (CPU <1%) | T090 |
| SC-004 (100Hz automation) | T080 |
| SC-005 (THD 0.1-5%) | T050 |
| SC-006 (BBD -12dB at 2x) | T060 |
| SC-007 (8-bit ~48dB SNR) | T070 |

---

## User Story Coverage

| User Story | Tasks | Acceptance Criteria |
|------------|-------|---------------------|
| US1: Tape Character | T050-T057 | THD, wow/flutter, HF rolloff |
| US2: BBD Character | T060-T065 | Bandwidth, clock noise, saturation |
| US3: Digital Vintage | T070-T075 | Bit depth, SR reduction, aliasing |
| US4: Clean Mode | T034-T035 | Unity gain, no latency |
| US5: Mode Transitions | T040-T043 | 50ms crossfade, no clicks |
| US6: Parameter Control | T080-T081 | Smoothing, automation |
