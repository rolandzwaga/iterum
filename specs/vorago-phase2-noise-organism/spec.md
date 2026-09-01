# Feature Specification: Vorago Phase 2 — Noise Organism

**Spec slug:** `vorago-phase2-noise-organism`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 2 (lines 178–197); reuse-inventory row
`L2 Noise Organism` (line 110). Cross-cutting constraints: roadmap lines 468–483.
**Layer:** one new Layer 3 component (`dsp/include/krate/dsp/systems/noise_organism.h`) plus one
additive Layer 2 amendment (`processors/noise_generator.h`, FR-080 series).
**Test target:** `dsp_systems_tests` (source list `dsp/tests/CMakeLists.txt:310-390`)
**Depends on:** Vorago Phase 1 — `PerlinNoiseSource` (`processors/perlin_noise_source.h:174`) and
`SlowEventScheduler` (`processors/slow_event_scheduler.h:143`) are both present in the working tree
and were read this session.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Overview

The Noise Organism is Vorago's second sound source, standing beside the harmonic cloud: not a noise
generator with an LFO on a filter, but a small population of semi-independent noise emitters, each
living inside its own resonator/comb/filter chain whose every parameter wanders on a
seconds-to-minutes time scale. It is the "sound mass" idea applied to unpitched material — hold a
note for five minutes and the noise bed should never be in the same place twice, without ever
drifting louder, quieter, or into a limit cycle.

Almost all of the audio DSP already exists. `NoiseGenerator` ships **thirteen** models (the roadmap
says twelve; it predates `RadioStatic`), `ResonatorBank`, `TimeVaryingCombBank` and
`StochasticFilter` are mature, and the wander sources (`BrownianDrift`, `PerlinNoiseSource`,
`BreathingModulator`) shipped in Seraphis Phase 1 and Vorago Phase 1. What does **not** exist is the
composition: a fixed-capacity population of source slots, three composed models the roadmap names,
a per-slot chain with life-modulated parameters, dormancy/awakening hooks for the slow-event
scheduler, and — critically — per-slot **decorrelation**, which today is impossible because
`NoiseGenerator` has no seed control at all (`noise_generator.h:189` reseeds from its own advancing
state and `:593` fixes every instance's construction seed at `12345`, so four slots built the same
way emit *bit-identical* noise and sum coherently at +12 dB instead of +6).

**Nothing in this phase re-implements existing DSP.** Every "exists" claim below carries a
`file:line` citation from a header opened in this session; the ODR sweep was run this session.

## Clarifications

### Session 2026-08-31

- Q1 (gain staging): Every slot audible at all — how is level calibrated across 13 noise types and
  drifting resonator Q? → A: Calibrated per-type drive table. Compile-time
  `kSourceDriveDb[kNumNoiseTypes]` (FR-017), measured once by rendering each type alone and recording
  the offset, plus a per-resonator make-up gain derived from the resonator's configured Q (FR-018).
  Runtime auto-normalisation and relaxing the dBFS floor were both rejected.
  [FR-017, FR-018, SC-001, SC-019]
- Q2 (reset semantics): What does `reset()` do to configuration vs. audio state? → A:
  Configuration-preserving only. `reset()` clears audio state and rewinds every wander lane, then
  mandatorily re-applies the organism's current configuration to every sub-component (working around
  `ResonatorBank::reset()`'s configuration wipe). No second `reinitialise()` method — `prepare()`
  already is the full-return-to-defaults path. [FR-002, FR-004, SC-006]
- Q3 (dust grain gain law): What is `DustGrain::gain` and what keeps `GranularDust` level flat across
  density? → A: Concurrency-normalised grain gain with the velvet trigger's random polarity folded in
  as the sign: `gain = sign * 1/sqrt(max(1, expectedConcurrency))`, recomputed on density/length
  change; in-flight grains keep their birth gain. [FR-034, FR-036, SC-019]
- Q4 (slot-gain ramp law): What exactly is the 50 ms smoothing law? → A: Per-sample, linear in gain
  (not dB), 50 ms total (25 ms down + 25 ms up for the FR-013 duck). SC-009 (a)'s 10-90 % wording is
  restated as 0-100 % = 50 ms ± 5 ms. [FR-013, FR-073, SC-009, SC-018]
- Q5 (duck re-triggering): Do repeated/overlapping model or type writes re-arm the duck? → A:
  Change-detected and coalescing. A write equal to the current effective value is a no-op; a new
  change mid-duck updates the pending target without restarting the ramp — a burst of writes costs
  exactly one 50 ms duck. [FR-013, SC-018]
- Q6 (dormancy cost/continuity): What does a dormant slot keep running? → A: Source runs, chain
  skipped. The slot's `NoiseGenerator`/`NoiseOscillator` keeps rendering into a scratch buffer (so the
  source stream and colour-filter state stay live); only the resonator/comb/`StochasticFilter` stages
  are skipped. [FR-071, FR-073, SC-004, SC-010]
- Q7 (wander-lane rates): Do the four wander lanes share one rate, and what defines SC-002's `T`? → A:
  Single organism-level `setWanderRate(float hz)` scalar maps to all four lane kinds (Brownian tau,
  Perlin rate, `StochasticFilter::setChangeRate`, breathing rate), default 0.03 Hz (unchanged from the
  prior Perlin-only anchor, so SC-002 stands as written). [FR-069, SC-002]
- Q8 (comb tuning control surface): Is comb fundamental/spread/feedback settable, and does the
  organism track note pitch? → A: Expose the three comb setters (`setCombTuning`, `setCombFeedback`)
  and matching getters; no key-following or note input in this phase — that idiom belongs to roadmap
  Phase 3. [FR-015, FR-057]
- OQ-MONO-AND-SHIFTER (roadmap-silent decisions, reconfirmed): Mono output and no `FrequencyShifter`
  in `MetallicHiss`? → A: Both confirmed as already specified; Non-Goals expanded with the cost
  derivation and the explicit contrast against `HarmonicCloud::processStereoBlock`. [FR-003, FR-042]
- OQ-CPU-POLICY (SC-004 miss handling): If the reference configuration misses the 1 %/voice budget, do
  the caps come down or does the budget rise? → A: Neither is pre-committed. FR-095's "caps come down"
  commitment is replaced with a hard stop: a miss must surface the measured ns/512-block figure and a
  per-stage breakdown for the user to judge; no implementing agent may lower caps, raise the budget, or
  relax the threshold unilaterally. [FR-095, SC-004]
- OQ-SHIPPED-DEFECTS (two shipped-component defects): Fix or work around? → A: Fix both, in scope,
  each with a failing test written first. `NoiseOscillator::process()`'s Velvet/RadioStatic fallthrough
  (FR-098) and `ResonatorBank::setFrequency`'s missing Q re-derivation (FR-099) are corrected; consumer
  suites (`dsp_processors_tests`, `dsp_primitives_tests`, `dsp_systems_tests`, `membrum_tests`) gate the
  change via SC-011, broadened to cover all three shared-component amendments. Session verification
  found **zero** existing consumers of the exact `ResonatorBank` class (Membrum and Seraphis's
  `continuous_body.h` use the unrelated `ModalResonatorBank`) and confirmed no existing `NoiseOscillator`
  consumer selects `NoiseColor::Velvet`/`RadioStatic` today (Membrum's `denormColor` is capped at
  Brown/Pink/White/Violet, `noise_layer.h:306-309`) — recorded here because it corrects the interview's
  premise about existing-consumer risk. [FR-098, FR-099, SC-011, FR-032]

## Scope

In scope:

- One new Layer 3 component, `NoiseOrganism`, at `dsp/include/krate/dsp/systems/noise_organism.h`
  (roadmap line 183), holding up to four source slots (roadmap line 185: "N (2–4)").
- Per-slot model selection over the existing `NoiseType` roster (roadmap line 185) plus the three
  **composed** models the roadmap names: filtered wind, granular dust, metallic hiss (lines 186–188).
- The per-slot chain the roadmap prescribes — resonator → comb → stochastic filter (line 189) — with
  **all** of its filter parameters wandering via `BrownianDrift`/`PerlinNoiseSource` (lines 190–191).
- Per-slot level breathing via `BreathingModulator` and event hooks that let an externally owned
  `SlowEventScheduler` awaken a dormant slot (roadmap lines 192–193).
- One additive, opt-in amendment to `NoiseGenerator`: `setSeed(std::uint32_t)` (FR-080 series). It is
  the minimum change that makes roadmap line 185 ("N simultaneous noise sources") mean N *different*
  sources, and it is required by the roadmap's own cross-cutting determinism rule.
- Unit tests covering the roadmap's four Phase-2 success criteria (line 196–197: stationarity,
  spectral-motion metric, zero allocation after prepare, CPU ≤ 1 % per voice) plus the cross-cutting
  gates from roadmap lines 468–483 (boundedness soak, seed determinism, sample-rate change, layer/ODR
  lints, portability, no bit-exact goldens).

## Non-Goals (owned by later phases)

- **Anything that consumes the organism.** `ResonanceDriftNetwork` (Phase 3) is the next stage in the
  voice chain; `VoragoVoice` (Phase 10) is what actually instantiates a `NoiseOrganism`, decides how
  many slots a voice runs, and connects a `SlowEventScheduler` to the wake hooks. This phase produces
  a block of audio and a control surface; nothing drives it yet.
- **Modulation routing.** Vorago Phase 1 established (spec `vorago-phase1-events-modulation`,
  Non-Goals) that implementing the `ModulationSource` ABC does **not** make a source routable —
  `ModulationEngine` dispatches through a fixed `switch (ModSource)` and `VoiceModRouter::computeOffsets`
  takes eight explicit floats. `NoiseOrganism` therefore **owns** its wander lanes internally (the
  `HarmonicCloud` precedent: private `DriftLanes`, `harmonic_cloud.h:1863-1964`) and exposes plain
  scalar setters for its wake hooks. No `ModSource` enumerator is added, no `kModSourceCount` bumped.
- **Stereo placement / panning.** Every element of the prescribed chain is mono
  (`ResonatorBank::process(float)` `resonator_bank.h:467`, `TimeVaryingCombBank::process(float)`
  `timevar_comb_bank.h:328`, `StochasticFilter::process(float)` `stochastic_filter.h:231`), and the
  roadmap says nothing about width for this component. `NoiseOrganism` renders **mono**; stereo
  placement belongs to Phase 3/Phase 10. **Reconfirmed in the 2026-08-31 clarification session
  (OQ-MONO-AND-SHIFTER):** a stereo organism would roughly double the per-slot chain cost against the
  1 %/voice budget (FR-095), and this deliberately differs from `HarmonicCloud`, which does provide
  `processStereoBlock` — recorded as a contrast, not an oversight. A mono noise bed still acquires
  width downstream from the cavern space engine, the normal arrangement; Phase 10 owns placement.
  (See also "Decisions taken where the roadmap is silent".)
- **`FrequencyShifter` in the metallic-hiss model.** The roadmap qualifies it "optional" (line 188);
  the CPU arithmetic in FR-042 rules it out for Phase 2 — a per-slot `FrequencyShifter`
  (`processors/frequency_shifter.h:98`, Hilbert pair + quadrature oscillator per sample) at
  `kMaxSources = 4` would consume a large fraction of the 1 %-per-voice budget before any of the
  prescribed chain is paid for. **Reconfirmed in the 2026-08-31 clarification session
  (OQ-MONO-AND-SHIFTER):** inharmonic detune is delivered instead by FR-042's organism-computed comb
  ratios plus the FR-063 external wander — not by the bank's own `setModRate`/`setModDepth`/
  `setRandomModulation`, which FR-042 pins at their library-default `0.0f` for the unsalted-PRNG
  reason given there. Recorded with its derivation, not dropped silently.
- **`MultimodeFilter`.** Named in the roadmap's reuse row (line 110) but not in the Phase-2 chain
  (line 189, which specifies `StochasticFilter`). Not used; no speculative second filter.
- **Refactoring `NoiseGenerator`'s per-sample structure.** Its thirteen level smoothers run every
  sample regardless of which models are enabled (`noise_generator.h:380-576`). That is a cost this
  spec *budgets for* (FR-095), not a thing it rewrites — the component has five other consumers.
- **Key-following / note-pitch tracking of the comb fundamental or resonator anchors.** FR-057 exposes
  the three comb setters (fundamental, spread, feedback) but adds no `setNoteFrequency` or key-follow
  amount — decided in the 2026-08-31 clarification session (Q8): roadmap Phase 3 already specifies
  free/keyed/hybrid anchoring as an explicit control for the resonance drift network, and one keying
  idiom should cover both components rather than Phase 2 inventing its own. The noise bed is therefore
  unpitched by default; Phase 10 may drive the anchors and comb fundamental directly if it wants pitch
  tracking before Phase 3's idiom exists.
- **Any plugin, parameter ID, UI, preset or state work** (Phases 11–14).

## Existing components (verified this session)

Every row below was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 2 reuses / relies on |
|---|---|---|
| `NoiseGenerator` (L2) | `processors/noise_generator.h:98` | The slot source. `void prepare(float sampleRate, size_t maxBlockSize) noexcept` (`:135` — **float**, unlike the `double` used everywhere else), `reset() :186`, `setNoiseEnabled(NoiseType,bool) :255`, `setNoiseLevel(NoiseType,float dB) :235`, `setMasterLevel :273`, `setVelvetDensity(float) :315` (clamped `[100, 20000]` imp/s), `setCrackleParams :308`, `setTapeHissParams :292`, `setAsperityParams :300`, `process(float* out, size_t) :332`, `isAnyEnabled() :365`. **Roster is 13, not 12**: `enum class NoiseType : uint8_t` at `:44` = White/Pink/TapeHiss/VinylCrackle/Asperity/Brown/Blue/Violet/Grey/Velvet/VinylRumble/ModulationNoise/**RadioStatic**, `kNumNoiseTypes = 13` (`:61`). **Two load-bearing defects for this spec:** (1) there is no `setSeed` and `rng_` is fixed at construction (`Xorshift32 rng_{12345}`, `:593`), so identical instances are bit-identical — FR-080; (2) `reset()` reseeds as `rng_.seed(rng_.next() ^ 0xDEADBEEF)` (`:189`), i.e. the stream depends on how many times `reset()` has been called — not reproducible. Velvet emits `±1 × velvetGain` only at impulse samples and exactly `0.0f` between them (`:521-534`) — this is what FR-031 detects. **Third load-bearing fact:** of the three signal-dependent models only `TapeHiss` (`:407-412`) and `Asperity` (`:452-457`) have a noise floor (`floorGain + (1 − floorGain) × envelope × sensitivity`); `ModulationNoise` is explicitly floor-less — `// Track input signal envelope (no floor - zero when silent)` (`:553`), `envelope = modulationEnvelope_.processSample(sidechainInput)` (`:554`), `modulatedNoise = whiteNoise * envelope` (`:558`) — so under a zero sidechain it contributes exactly `0.0f`. FR-012 excludes it. **Fourth:** each type is gated on `if (noiseEnabled_[idx])` (`:388` and the parallel blocks through `:568`), so disabling a type drops its full-amplitude contribution on the next sample while `updateLevelTarget` (`:255-261`) never gets to ramp — FR-013's duck. Colour filters were **fs-fixed**: `kBrownLeak = 0.98f` (`:467-468`), blue = one-sample differentiator of pink (`:483`), violet = one-sample differentiator of white (`:498`), pink = Kellet coefficients tuned at 44.1 kHz (`primitives/pink_noise_filter.h:65-70`). **White, brown and pink were made rate-aware in this phase, each anchored at 44.1 kHz so the reference rate is bit-reproduced; blue and violet were measured and deliberately left alone** — FR-093/SC-008. |
| `StochasticFilter` (L2) | `processors/stochastic_filter.h:95` | Third stage of the slot chain. `prepare(double, size_t) :139`, `reset() :193`, `process(float) :231`, `processBlock(float*, size_t) :281`, `setMode(RandomMode) :297` (`enum class RandomMode : uint8_t { Walk, Jump, Lorenz, Perlin }` at `:44`), `setCutoffRandomEnabled(bool) :311`, `setResonanceRandomEnabled :316`, `setTypeRandomEnabled :321`, `setBaseCutoff(float) :343`, `setBaseResonance(float) :350`, `setBaseFilterType(SVFMode) :356` (`SVFMode` at `primitives/svf.h:38`), `setCutoffOctaveRange :382`, `setResonanceRange :388`, `setChangeRate(float) :417`, `setSmoothingTime(float) :423`, `setSeed(uint32_t) :436`. Constants `kMinChangeRate = 0.01f` / `kMaxChangeRate = 100.0f` (`:101-102`), `kDefaultSmoothing = 50.0f` ms (`:107`), `kControlRateInterval = 32` (`:117`). **Defaults that the organism must override or pin explicitly, not inherit:** `cutoffRandomEnabled_ = true` (`:555`), `resonanceRandomEnabled_ = false` (`:556`), `typeRandomEnabled_ = false` (`:557`), `kDefaultChangeRate = 1.0f` (`:103`), `kDefaultOctaveRange = 2.0f` (`:112`) — i.e. a stock instance wanders its cutoff ±2 octaves at 1 Hz, an order of magnitude faster than anything this component is about (FR-016, FR-056, FR-068). Its internal wander is **retained** and is a distinct role from the external lanes — see FR-060 and FR-023. Not modified. |
| `ResonatorBank` (L2) | `processors/resonator_bank.h:174` | First stage of the slot chain. `prepare(double) :184`, `reset() :213`, `setCustomFrequencies(const float*, size_t) :295`, `setFrequency(size_t,float) :328`, `setDecay(size_t,float) :345`, `setGain(size_t,float dB) :364`, `setQ(size_t,float) :381`, `setEnabled(size_t,bool) :398`, `setDamping :418`, `setSpectralTilt(float) :440`, `process(float) :467`, `processBlock(float*,size_t) :519`. `kMaxResonators = 16` (`:39`), `kMinResonatorFrequency = 20.0f` (`:42`), `kMaxResonatorFrequencyRatio = 0.45f` (`:45`), `kMinResonatorQ/kMaxResonatorQ = 0.1/100` (`:48,51`). **Three verified facts this spec is built on:** (a) the per-sample loop skips disabled slots (`for (i < kMaxResonators) { if (!enabled_[i]) continue; }`, `:484-486`), so cost scales with enabled count; (b) `calculateTiltGain` early-returns `1.0f` when tilt is exactly `0.0f` (`:120-123`) and otherwise costs a `std::log2` **plus** a `dbToGain` **per resonator per sample** (`:124-125`, called at `:504`) — FR-055 keeps tilt at 0; (c) `setFrequency` hard-swaps Biquad coefficients with no interpolation (`updateFilterCoefficients`, `:545-555`, `FilterType::Bandpass`) and — **before this phase's FR-099 fix** — did not re-derive Q from the decay time the way `setDecay` does (`:350-351`), so drifting frequency silently changed the effective RT60. **Fixed in this phase (FR-099).** Verified this session: `grep -rln "\bResonatorBank\b"` (word-bounded, excluding the unrelated `ModalResonatorBank`) across `dsp/` and `plugins/` finds **zero** consumers outside `resonator_bank_test.cpp` and the compile-only `dsp/lint_all_headers.cpp` — Membrum's bodies and `continuous_body.h` (Seraphis) all use `ModalResonatorBank`, a different class declared in the same header. The fix is therefore zero-regression-risk for every shipped consumer, gated by `dsp_processors_tests` (which owns `resonator_bank_test.cpp`) per SC-011; and (d) **`setDecay` and `setQ` write the same variable** — `qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])` (`:349`) vs `qValues_[index] = std::clamp(q, kMinResonatorQ, kMaxResonatorQ)` (`:383`) — so alternating them destroys one setting with the other (FR-052 gives Q a single owner). `rt60ToQ` is `(π × f × RT60) / ln1000` clamped to `[0.1, 100]` (`:92-98`), i.e. saturated at `kMaxResonatorQ` for every `f × RT60 > 219.9` (FR-064). There is **no** `setNumResonators`/count method at all (swept: `grep -n "setNum" resonator_bank.h` — no hits), and `exciterMix_` defaults to `0.0f` (`:589`, re-zeroed at `:235`) which in `output = input × mix + wetSum × (1 − mix)` (`:511`) means **fully wet** — so a bank with nothing enabled emits silence, not bypass (FR-051). |
| `TimeVaryingCombBank` (L3) | `systems/timevar_comb_bank.h:81` | Second stage of the slot chain. **The class is `TimeVaryingCombBank`, not "TimevarCombBank"** — the roadmap uses the file stem, not the type name. `prepare(double, float maxDelayMs = 50.0f) :154`, `reset() :162`, `setNumCombs(size_t) :178`, `setCombDelay(size_t,float ms) :189`, `setCombFeedback :197`, `setCombDamping :206`, `setCombGain :214`, `setTuningMode(Tuning) :226` (`enum class Tuning : uint8_t { Harmonic, Inharmonic, Custom }` at `:43`), `setFundamental :238`, `setSpread :249`, `setModRate(float) :263`, `setModDepth(float) :274`, `setRandomModulation(float) :296`, `process(float) :328`, `processBlock(const float*, float*, size_t) :345`. `kMaxCombs = 8` (`:88`), `kDelaySmoothingMs = 20.0f` (`:109`) — the built-in delay smoothing is why comb-delay drift needs no extra slew limiter (FR-063). It is Layer 3; a Layer 3 header including it is permitted and precedented (`systems/continuous_body.h:42`, `// L3 - same layer, see banner`) — `tools/lint-layers.js:8` fails only when a file reaches **up**. Note `prepare()` sizes all `kMaxCombs` delay lines regardless of `setNumCombs` — the FR-096 memory figure. **Three further verified facts this spec is built on:** (a) `setNumCombs` clamps to `std::clamp(count, size_t{1}, kMaxCombs)` (`:502`) — it **cannot take 0**, and `process()` returns only the sum of active combs with no dry path (`:328-330`), so bypass must be done by not calling it (FR-054); (b) `setCombDelay` unconditionally sets `tuningMode_ = Tuning::Custom` (`:189` doc, assignment at `:515`), so any per-control-step delay write permanently leaves `Inharmonic` (FR-042); (c) there is **no `setSeed`** — `prepare` hard-seeds every per-comb PRNG with `ch.rng.seed(12345u + i * 7919u)` (`:466`, repeated in `reset()` at `:487`) — so its internal motion would be bit-identical across slots; `modDepth_` and `randomModAmount_` default to `0.0f` (`:414,416`) and FR-042 leaves them there. The inharmonic law is `f[n] = fundamental * sqrt(1 + n*spread)` (`:237`, implementation `:959`). |
| `BrownianDrift` (L2) | `processors/brownian_drift.h:94` | Wander lane for resonator frequency and filter cutoff. `prepare(double) :121`, `reset() :133`, `setSeed(uint32_t) :145`, `setSmoothness(float) :152`, `setDepth(float) :159`, `setMean(float) :165`, `process() :178`, `processBlock(size_t) :194`, `getCurrentValue() override :212`, `getSourceRange() override :217`. `kTauMin/kTauMax = 0.2/30.0` s (`:97,99`), `kDriftOutputSmoothMs = 150.0f` (`:103`), `kControlRateInterval = 32` (`:105`). Ornstein–Uhlenbeck with mean reversion — bounded by construction, which is why FR-061's bounds are cheap to guarantee. Not modified. |
| `PerlinNoiseSource` (L2) | `processors/perlin_noise_source.h:174` | Wander lane for comb delay time (smoother, band-limited by construction — the roadmap pairs it with `BrownianDrift` at line 190). `prepare(double) :211`, `reset() :221`, `setSeed(uint32_t) :233`, `setRate(float cellsPerSecond) :242`, `setOctaves(int) :249`, `setDepth(float) :256`, `process() :272`, `processBlock(size_t) :291`, `getCurrentValue() override :321`. `kMinRate = 0.005f` / `kMaxRate = 5.0f` (`:177,179`), `kMinOctaves/kMaxOctaves = 1/4` (`:181,183`), `kOutputSmoothMs = 5.0f` (`:191`), `kControlRateInterval = 32` (`:193`). Vorago Phase 1. Not modified. |
| `BreathingModulator` (L2) | `processors/breathing_modulator.h:105` | Per-slot level breathing (roadmap line 192). `prepare(double) :144`, `reset() :152`, `setSeed(uint32_t) :164`, `setRate(float hz) :170`, `setDepth(float) :177`, `setIrregularity(float) :184`, `process() :197`, `processBlock(size_t) :209`, `getCurrentValue() override :222`. `kMinRate = 0.01f` / `kMaxRate = 0.5f` (`:108,110`) — i.e. 2 s–100 s per breath; `kDefaultRate = 0.1f`, `kDefaultDepth = 1.0f`, `kDefaultIrregularity = 0.0f` (`:111-113`); `kOutputSmoothMs = 20.0f` (`:117`). **Output is bipolar `[-1, +1]`, not unipolar.** The header says so explicitly — "Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with the depth setting; depth scales the signal inside that range)" (`:103`) — and `getSourceRange()` returns `{-1.0f, 1.0f}` (`:227-229`). A slot gain multiplied by that value directly would invert the slot on every exhale and null it at each zero crossing, so FR-070 defines an explicit affine mapping instead of a bare multiply. Not modified. |
| `SlowEventScheduler` (L2) | `processors/slow_event_scheduler.h:143` | **Not owned by `NoiseOrganism`** — read to define the wake-hook contract (FR-070 series). `kNoTarget = 0xFFu` (`:150`), `kMaxTargets = 16u` (`:152`), `enum class Phase : uint8_t { Idle, Attack, Hold, Release }` (`:180`), `struct Event { uint8_t target; float depth; int8_t polarity; }` (`:187-191`), `getActiveTarget() :356`, `isEventActive() :361`, `getEnvelopeValue() :391` (unipolar `[0,1]`), `getActiveDepth() :397`, `getActivePolarity() :400`, `getCurrentValue() override :331`. Vorago Phase 1. |
| `GrainEnvelope` (L0) | `core/grain_envelope.h` | Granular-dust window. `enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman, Linear, Exponential }` (`:14-21`); `inline void generate(float* output, size_t size, GrainEnvelopeType type, float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept` (`:33-34`) — table generation, `prepare`-time only; `[[nodiscard]] inline float lookup(const float* table, size_t tableSize, float phase) noexcept` (`:165-166`) — interpolated read, RT-safe. Not modified. |
| `NoiseOscillator` (L1) | `primitives/noise_oscillator.h:67` | The **granular-dust carrier**. `prepare(double) :92`, `reset() :97`, `setColor(NoiseColor) :103`, `setSeed(uint32_t) :109` (`:232-235`: `rng_.seed(seed)`) — the seeding signature FR-080 copies, `process() :119`, `processBlock(float*,size_t) :127`. `NoiseColor` at `core/pattern_freeze_types.h:124-133`. **Defect, fixed in this phase (FR-098):** `process()` had no `case` for `Velvet`/`RadioStatic` and fell through `default:` to white (`:265-267`). Verified this session: zero existing consumers select either colour (`ring_modulator.h:295,298` and every Membrum caller pin `White`/`Violet`; Membrum's runtime-selectable `noise_layer.h:306-309` `denormColor` is capped at Brown/Pink/White/Violet), so the fix changes no shipped output. FR-032 now restricts the granular-dust carrier to seven colours (all but `Velvet`, excluded on musical-design grounds, not the old technical one — see FR-032). |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Sole PRNG and the per-lane seed derivation. `explicit constexpr Xorshift32(uint32_t seedValue = 1)` (`:44-45`) substitutes its default for a 0 seed; `constexpr uint32_t deriveStreamSeed(uint32_t base, size_t salt) noexcept` (`:102-103`) is the lowbias32 finaliser with a guaranteed-non-zero result — the exact tool FR-005 needs to give ~40 lanes independent streams from one organism seed. |
| `HarmonicCloud` (L3) | `systems/harmonic_cloud.h:127` | **Shape template**, not a dependency. `kControlChunkSamples = 64` (`:144`) is the shared Phase-7 control clock (`continuous_body.h:97` static-asserts on it at `:630`); `processStereoBlock(float*, float*, size_t) :878` establishes the render contract this spec copies verbatim into FR-003: null pointer ⇒ nothing written (`:880-883`), `numSamples == 0` ⇒ no-op, un-`prepare`d ⇒ silence rather than uninitialised coefficients (`:886-891`). Private drift lanes (`:1863-1964`) are the precedent for owning wander internally. |
| `ContinuousBody` (L3) | `systems/continuous_body.h:71` | Second shape template: `processStereoBlock(const float*, const float*, float*, float*, size_t) :1369` with the same guard ladder (`:1372-1385`), `setSeed(uint32_t) :1344`, and the same-layer include of `timevar_comb_bank.h` at `:42`. |
| `AtmosphereEngine` (L3) | `systems/atmosphere_engine.h:179` | Precedent for `struct PrepareConfig { … }` (`:369-376`) as the prepare-time capacity/sizing surface, which FR-002 copies. |
| Perf-test idiom | `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp:22-70` | The measurement basis SC-004 inherits: **ns per 512-sample block at 48 kHz**, best-of-25 × 500 blocks after 400 warm-up blocks, gated against a checked-in baseline × 1.5, with `static_assert(kBaseline * kRegressionFactor <= kReferenceNs)` and `static_assert(kBaseline >= kReferenceNs / 50.0)` binding the absolute figure at compile time. Also the source of the out-of-region convention (`:44-50`) and the rule "IF A MEASUREMENT IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE" (`:65`). |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h` (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance`) for SC-013; `allocation_detector.h:48 AllocationDetector` / `:111 AllocationScope` for SC-003; `audio_features.h:23 AudioFeatures` (`peakDbfs`, `rmsDbfs`, `centroidHz`, 5-band energy fractions) and `:37 extractAudioFeatures(const std::vector<float>&, double)` for SC-001/SC-002/SC-008; `statistical_utils.h:41-140` for the window statistics. **`artifact_detection.h:38-99 ClickDetector` is deliberately NOT used** — it thresholds the signal's first derivative at 5σ, so on a broadband noise render every sample is a "click"; SC-009 measures the **envelope** instead. |

## New components

ODR sweep run this session over `dsp/`, `plugins/` and `tools/` with
`grep -rn "\(class\|struct\|enum class\|using\) <Name>\b"`:

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `NoiseOrganism` | 3 | `dsp/include/krate/dsp/systems/noise_organism.h` (new) | **0 hits.** Also swept and clean: `NoiseSource`, `NoiseVoice`, `OrganismSource`, `NoiseChain`, `SourceChain`, `WanderingFilter` — all **0 hits**. Near-name hazards that DO exist and must not be shadowed: `NoiseGenerator` (`processors/noise_generator.h:98`), `NoiseOscillator` (`primitives/noise_oscillator.h:67`), `NoiseType` (`:44`), `NoiseColor` (`core/pattern_freeze_types.h:124`). None collide. |
| `NoiseOrganismModel` (enum class) | 3 | same header | **0 hits** for `NoiseOrganismModel`, `OrganismModel`, `WindModel`, `FilteredWind`, `GranularDust`, `MetallicHiss`. Deliberately **not** named `NoiseModel` (0 hits today, but one character from `NoiseType`/`NoiseColor` in the same namespace — the near-name hazard the roadmap warns about at line 127). |
| `NoiseOrganism::PrepareConfig` (nested) | 3 | same header | Nested, following `AtmosphereEngine::PrepareConfig` (`atmosphere_engine.h:369`). `NoiseOrganismConfig` also swept: **0 hits**; nested is preferred so no new top-level name is claimed. |
| `NoiseOrganism::DustGrain` (nested POD) | 3 | same header | **0 hits** for `DustGrain`, `DustGrainSlot`, `GrainEnvelope` as a *class* — but `namespace GrainEnvelope` exists (`core/grain_envelope.h:23`) and `struct Grain` exists (`primitives/grain_pool.h:23`), so both of those names are **forbidden** here. Nesting inside `NoiseOrganism` removes the question entirely (the `SlowEventScheduler::Event` precedent, `slow_event_scheduler.h:187-191`). |
| `NoiseGenerator::setSeed` (member fn) | 2 (amendment) | `processors/noise_generator.h` (existing) | Additive member on an existing class; no new type name. Signature copied verbatim from `NoiseOscillator::setSeed(uint32_t seed) noexcept` (`primitives/noise_oscillator.h:109`). |

`tools/lint-odr.js` and `tools/lint-layers.js` must pass on the result (SC-012).

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** — `NoiseOrganism` is a Layer 3 class in `namespace Krate::DSP`, declared in
  `dsp/include/krate/dsp/systems/noise_organism.h`, header-only, with a `// Layer: 3 (Systems)` banner
  matching `timevar_comb_bank.h:8`. Its includes reach only Layers 0–3; the single same-layer include
  (`systems/timevar_comb_bank.h`) carries the `continuous_body.h:42` justification comment.
  Trace: roadmap line 183.
- **FR-002** — `void prepare(double sampleRate, const PrepareConfig& config) noexcept` is the only
  method allowed to allocate. `PrepareConfig` (nested, `AtmosphereEngine::PrepareConfig` shape,
  `atmosphere_engine.h:369-376`) carries `std::size_t maxBlockSamples` (default 2048, clamped
  `[64, 8192]`), `float maxCombDelayMs` (default 50.0, clamped `[5, 200]`) and
  `std::size_t numSources` (default 2, clamped `[1, kMaxSources]`). `sampleRate` is floored at 1 Hz
  (`brownian_drift.h:122` idiom). Re-preparing is legal and fully re-initialises. `NoiseGenerator`'s
  `prepare` takes a **float** sample rate and a block size (`noise_generator.h:135`); the narrowing
  cast happens once, in `prepare`, never in the render path.
  **`prepare()` is the organism's only return-to-FR-016-defaults path.** `reset()` (FR-004) is
  configuration-preserving, not a defaults reset — decided in the 2026-08-31 clarification session
  (Q2) — so a caller that wants every setting back at its FR-016 default must call `prepare()` again
  (with a default-valued `PrepareConfig` if `numSources`/`maxBlockSamples`/`maxCombDelayMs` are also to
  reset). No separate `reinitialise()` method is added: `prepare()` already has exactly those
  semantics, and a third method would duplicate it for no gain.
- **FR-003** — `void processBlock(float* output, std::size_t numSamples) noexcept` renders mono audio
  and **overwrites** `output` (it does not accumulate). Guard ladder, in order, copied from
  `harmonic_cloud.h:880-891`: `output == nullptr` ⇒ **nothing is written and no state advances**;
  `numSamples == 0` ⇒ no-op consuming no control step; not prepared ⇒ `std::fill_n(output, numSamples, 0.0f)`
  and no state advance. Any `numSamples` is legal, including values far above the control chunk.
- **FR-004** — `void reset() noexcept` is **configuration-preserving** (decided in the 2026-08-31
  clarification session, Q2): it clears all audio state (noise filters, resonators, combs, SVF, dust
  grains), rewinds every wander lane to its `prepare`-time phase, and then **mandatorily re-applies the
  organism's current configuration** to every sub-component. The re-apply step is not optional and not
  an optimisation detail — it is load-bearing. Verified reason: `ResonatorBank::reset()` is itself a
  **configuration wipe**, not a state clear — it sets every resonator to 440 Hz, `kDefaultDecayTime`,
  unity gain, `kDefaultResonatorQ` and **`enabled_[i] = false`** (`resonator_bank.h:225-231`), and its
  own doc says so: "User must reconfigure tuning after calling reset()" (`:212`). An organism that
  forwarded `reset()` to `ResonatorBank` without re-applying `setEnabled`/`setResonatorAnchor`/
  `setResonatorDecay` afterward would render **silence** on every slot with resonators enabled. Phase
  10 calls `reset()` on note-off and voice-steal against a fully configured organism, so this must be
  safe to call freely without losing the voice's preset.
  `reset()` after `prepare(sr, cfg)` reproduces the exact stream `prepare(sr, cfg)` produced **only
  when no setter has been called since `prepare`** — once a setter has changed the configuration,
  `reset()` reproduces the stream that configuration would have produced from a fresh `prepare`, not
  the `prepare`-time defaults. `prepare()` (FR-002) remains the only way back to the FR-016 defaults.
- **FR-005** — `void setSeed(std::uint32_t seed) noexcept` seeds the whole organism. Every internal
  stream receives `deriveStreamSeed(seed, salt)` (`core/random.h:102`) with a **unique, documented,
  stable** salt, assigned from a compile-time table so adding a lane later cannot renumber existing
  ones. The seeded lane kinds are, exhaustively:

  | Lane kind | Count | Seeded via |
  |---|---|---|
  | per-slot `NoiseGenerator` | `kMaxSources` | `setSeed` (FR-080, new) |
  | per-slot dust carrier `NoiseOscillator` | `kMaxSources` | `setSeed` (`noise_oscillator.h:109`) |
  | per-slot chain `StochasticFilter` | `kMaxSources` | `setSeed` (`stochastic_filter.h:436`) |
  | resonator frequency/Q `BrownianDrift` | `kMaxSources × kMaxResonatorsPerSource` | `setSeed` (`brownian_drift.h:145`) |
  | filter-cutoff `BrownianDrift` | `kMaxSources` | `setSeed` (`brownian_drift.h:145`) |
  | filter-resonance `BrownianDrift` (FR-067) | `kMaxSources` | `setSeed` (`brownian_drift.h:145`) |
  | comb-delay `PerlinNoiseSource` | `kMaxSources × kMaxCombsPerSource` | `setSeed` (`perlin_noise_source.h:233`) |
  | per-slot `BreathingModulator` | `kMaxSources` | `setSeed` (`breathing_modulator.h:164`) |

  **`TimeVaryingCombBank` is deliberately absent from that table and carries no salt**: it exposes no
  `setSeed` and hard-seeds its per-comb PRNGs `ch.rng.seed(12345u + i * 7919u)` in `prepare`
  (`timevar_comb_bank.h:466`, repeated in `reset()` at `:487`). FR-042 therefore pins its internal
  motion off (`setModDepth`/`setRandomModulation` at their `0.0f` defaults, `:414,416`) so that no
  unsalted, cross-slot-identical trajectory reaches the audio. All comb motion comes from the salted
  FR-063 Perlin lanes.
  A `seed` of 0 is legal (`Xorshift32` and `deriveStreamSeed` both guarantee non-zero streams).
  Trace: roadmap line 478 (determinism) and line 185 (N *independent* sources).
- **FR-006** — The render path is real-time safe: `noexcept`, no allocation, no lock, no exception, no
  I/O, no `std::function`, no virtual dispatch on the per-sample path. Trace: roadmap line 469.
- **FR-007** — Control-rate work runs on a **64-sample absolute grid** (`kControlChunkSamples = 64`,
  matching `harmonic_cloud.h:144` and `continuous_body.h:97`). A control chunk split by a block
  boundary (e.g. 36 + 28) produces the same control step as an unsplit 64: block size must not change
  the output.
- **FR-008** — Every value written into a chained component is finite and in that component's
  documented range before the call. Non-finite lane output (impossible by construction, guarded
  anyway) is replaced by the lane's neutral value; finiteness is tested on the IEEE-754 exponent field,
  never with `std::isnan`/`std::isinf` (macOS CI builds `-ffast-math`). Trace: roadmap line 480.

### FR-010 series — Source slots

- **FR-010** — `static constexpr std::size_t kMaxSources = 4`. Slot storage is a fixed
  `std::array`; the active count is `PrepareConfig::numSources`, changeable at runtime by
  `void setNumSources(std::size_t n) noexcept` clamped to `[1, kMaxSources]`. Reducing the count
  silences the dropped slots over a click-free ramp (FR-072); it never reallocates.
  Trace: roadmap line 185 ("N (2–4) simultaneous noise sources").
- **FR-011** — Each slot has a model: `enum class NoiseOrganismModel : std::uint8_t { Direct = 0,
  FilteredWind = 1, GranularDust = 2, MetallicHiss = 3 }`, selected by
  `void setSourceModel(std::size_t slot, NoiseOrganismModel model) noexcept`.
  `Direct` emits the slot's selected `NoiseType` with the default chain configuration; the other three
  are the roadmap's composed models (lines 186–188). Enumerators are **appended only**, never
  reordered. Out-of-range `slot` is a silent no-op (the `resonator_bank.h:329` idiom).
- **FR-012** — `void setSourceNoiseType(std::size_t slot, NoiseType type) noexcept` selects from the
  existing roster (`noise_generator.h:44-59`), so velvet, pink, brown, crackle and hiss are reachable
  exactly as the roadmap requires (line 185). **Twelve of the thirteen types are selectable;
  `NoiseType::ModulationNoise` is not** — it is snapped to `NoiseType::TapeHiss` (its floored
  signal-dependent analogue) and `getSourceNoiseType` reports the *effective* type, so the rejection
  is observable rather than silent. This is the FR-032 pattern applied to a second verified hazard:
  `ModulationNoise` is explicitly floor-less — `// Track input signal envelope (no floor - zero when
  silent)` (`noise_generator.h:553`), `envelope = modulationEnvelope_.processSample(sidechainInput)`
  (`:554`), `modulatedNoise = whiteNoise * envelope` (`:558`) — so under the zero sidechain FR-013
  mandates it contributes exactly `0.0f` and the slot would render dead. A Phase-10/Phase-12 preset
  must not be able to select a guaranteed-silent model. SC-019 asserts every selectable type renders
  non-silent.
  Type selection takes effect only for `Direct` slots; the three composed models pin their own base
  type (FR-021, FR-031, FR-041). The selected type is remembered across model changes, so switching
  to a composed model and back restores it. The change itself is ducked per FR-013.
- **FR-013** — Exactly one `NoiseType` is enabled on a slot's `NoiseGenerator` at any time
  (`setNoiseEnabled`, `noise_generator.h:255`). Model-specific parameters are forwarded where they
  exist: `setVelvetDensity` (`:315`), `setCrackleParams` (`:308`), `setTapeHissParams` (`:292`),
  `setAsperityParams` (`:300`). The two **floored** signal-dependent types are `TapeHiss`
  (`modulation = floorGain + (1 − floorGain) × envelope × sensitivity`, `noise_generator.h:407-412`)
  and `Asperity` (the same expression, `:452-457`): driven with a zero sidechain
  (`process(float*, size_t)`, `:332`) they sit at their configured noise floor, which is documented,
  not worked around. `ModulationNoise` is **not** floored and is therefore not selectable — see
  FR-012.
  **Type and model changes are ducked, because `NoiseGenerator` cannot fade them.** Each type's
  contribution is gated on `if (noiseEnabled_[idx])` (`:388` and the parallel blocks through `:568`),
  so disabling a type removes its full-amplitude broadband contribution on the very next sample while
  the level smoother `updateLevelTarget` sets to zero (`:255-261`) never gets to ramp. The organism
  therefore wraps every `setSourceNoiseType` and `setSourceModel` change in a **duck-and-restore**
  over FR-073's per-sample, linear-in-gain 50 ms ramp: the slot gain ramps to 0 over 25 ms, the
  `setNoiseEnabled` pair is applied at the zero point on a control step, and the gain ramps back over
  25 ms (both legs share FR-073's domain and per-sample update rate). SC-018 measures it.
  **Duck triggering is change-detected and coalescing** (decided 2026-08-31, Q5, option a). A
  `setSourceNoiseType`/`setSourceModel` write whose value equals the slot's current *effective* value
  (per FR-012's substitution and FR-011's model roster) is a **no-op**: it does not arm the duck and
  does not touch `NoiseGenerator`. A write that genuinely changes the value while a duck from an
  **earlier** change is still in progress **updates the pending target without restarting the ramp**
  — a burst of changes on one slot costs exactly one 50 ms duck, not one per write. This matters
  because Phase 12 drives these setters from VST3 parameters, and hosts commonly re-send every
  parameter's value every block, including unchanged ones: without change-detection a parameter-
  echoing host would hold the slot permanently near silence, and without coalescing a preset load that
  writes model then type in the same block would duck twice back to back (100 ms of near-silence).
  SC-018 gains an explicit arm for both properties.
- **FR-014** — `void setSourceLevel(std::size_t slot, float dB) noexcept`, clamped
  `[-96, +12]` to match `NoiseGenerator::kMinLevelDb/kMaxLevelDb` (`:104-105`), sets the slot's
  contribution to the mix. Level changes are smoothed; a step of the full range must not click
  (SC-009).
- **FR-015** — Read surface, every member `[[nodiscard]] … const noexcept`. Out-of-range `slot`
  returns the documented neutral value and never reads out of bounds.
  *Configuration echo:* `getNumSources()`, `getSourceModel(slot)`, `getSourceNoiseType(slot)` (the
  **effective** type after FR-012's substitution), `getSourceLevel(slot)`, `isSourceDormant(slot)`,
  `getSourceWakeAmount(slot)`, `getNumResonators(slot)`, `getNumCombs(slot)`,
  `getCombFundamental(slot)`, `getCombSpread(slot)`, `getCombFeedback(slot)` (FR-057),
  `getDustDensity(slot)`,
  `getDustGrainMs(slot)` and `getDustCarrierColor(slot)` (the last three **effective** after FR-032
  and FR-035), `isWanderEnabled()` (FR-068), `getWanderRate()` (FR-069).
  *Applied-state echo — these exist because success criteria need a deterministic, noise-free
  observable and the audio signal is not one:*
  `float getSourceGain(std::size_t slot)` — the slot's **applied smoothed gain** (level × breathing ×
  wake gate, FR-050), the quantity SC-009 asserts monotonicity and ramp duration on;
  `float getResonatorCurrentFrequency(std::size_t slot, std::size_t index)` and
  `float getResonatorCurrentQ(std::size_t slot, std::size_t index)` — the values last written through
  FR-052; `float getFilterCurrentCutoff(std::size_t slot)` — the value last written through FR-062.
  SC-010 compares those trajectories to prove lane freewheeling under dormancy.
  *Diagnostics:* `float getSourceRms(std::size_t slot)` — the slot's smoothed output level, a one-pole
  on the squared slot output computed on the control grid (`EnvelopeFollower` is *not* added for it);
  `std::uint32_t getClampEngagementCount()` (FR-074); `std::size_t getAllocatedBytes()` — the sum of
  the heap sizes the component requested in `prepare`, accumulated by `prepare` itself from the
  lengths it asks for (FR-096), which is what SC-014 measures.
  `getSourceRms` is retained deliberately, not speculatively: SC-009 and SC-010 both depend on it, and
  its ~2 flops per slot per control step are inside the SC-004 measurement. Its second consumer is
  Phase 8's ecosystem energy sensing, which may replace it with `EnvelopeFollower`
  (Vorago-roadmap.md:123) — recorded under "Decisions taken where the roadmap is silent".
- **FR-016** — **Normative default configuration.** Every organism default is fixed here, in one
  place, so that "the reference configuration" and "all wander depths at default" are reproducible
  statements. SC-001, SC-002, SC-004, SC-009 and SC-013 reference this table by name. Where the
  organism **overrides** a library default the library value is quoted beside it, so nothing is left
  to be inherited by accident.

  | Setting | Default | Owner |
  |---|---|---|
  | `PrepareConfig::numSources` / `maxBlockSamples` / `maxCombDelayMs` | 2 / 2048 / 50.0 ms | FR-002 |
  | per-slot model | `Direct` | FR-011 |
  | per-slot `NoiseType` | `Brown` (every slot) | FR-012 |
  | per-slot level | −12.0 dB | FR-014 |
  | resonators per slot | 2 | FR-051 |
  | resonator anchors, index 0..3 | 70, 140, 260, 500 Hz | FR-052 |
  | resonator decay (nominal RT60 at anchor) | 1.5 s | FR-053 |
  | `kSourceDriveDb[type]` per-type calibration | compile-time table, measured (see FR-017) | FR-017 |
  | resonator per-Q make-up gain | computed from configured Q, measured coefficients (see FR-018) | FR-018 |
  | resonator frequency wander | 2.0 semitones, smoothness = FR-069's mapping (**30 s**, clamped at `kTauMax` — see FR-069) | FR-061, FR-069 |
  | resonator Q wander amount | 0.25 | FR-064 |
  | combs per slot | 2 | FR-054 |
  | comb base tuning | fundamental 60 Hz, spread 0.35, ratios computed by the organism (settable via FR-057) | FR-042, FR-057 |
  | comb feedback | 0.55 (`MetallicHiss`: 0.75), settable via FR-057 up to the 0.9 cap | FR-042, FR-057, FR-090 |
  | comb delay wander | 12 %, Perlin rate = FR-069's `kDefaultWanderRateHz` (**0.03 cells/s**, unchanged) | FR-063, FR-069 |
  | `TimeVaryingCombBank::setModDepth` / `setRandomModulation` | 0.0 / 0.0 — library defaults, `timevar_comb_bank.h:414,416` | FR-042 |
  | chain filter base type | `SVFMode::Lowpass` (`FilteredWind`: `Bandpass`) | FR-022, FR-056 |
  | chain filter base cutoff / base resonance | 800 Hz / 0.7 | FR-056 |
  | `setCutoffRandomEnabled` | **true** (library default, `stochastic_filter.h:555`) | FR-023, FR-056 |
  | `setChangeRate` | FR-069's `kDefaultWanderRateHz` (**0.03 Hz**) — overrides `kDefaultChangeRate = 1.0f` (`:103`); same value for `FilteredWind` (FR-022) and every other model | FR-056, FR-069 |
  | `setCutoffOctaveRange` | **1.0** (`FilteredWind`: 2.0) — overrides `kDefaultOctaveRange = 2.0f` (`:112`) | FR-022, FR-056 |
  | `setResonanceRandomEnabled` / `setTypeRandomEnabled` | false / false (library defaults, `:556,557`) | FR-056 |
  | `setSmoothingTime` | 200 ms (`FilteredWind`: 400 ms) | FR-022, FR-056 |
  | filter cutoff wander | 1.5 octaves, smoothness = FR-069's mapping (**30 s**, clamped) | FR-062, FR-069 |
  | filter resonance wander | 0.2, smoothness = FR-069's mapping (**30 s**, clamped) | FR-067, FR-069 |
  | breathing rate / depth / irregularity | FR-069's `kDefaultWanderRateHz` (**0.03 Hz**, was an independent 0.05 Hz default before FR-069) / 0.25 / 0.3 | FR-070, FR-069 |
  | `BreathingModulator::setDepth` | left at library default 1.0 (`breathing_modulator.h:112`) | FR-070 |
  | wake amount / dormant | 1.0 / false | FR-071, FR-072 |
  | dust grain length / density / carrier colour | 40 ms / 100 imp/s / `NoiseColor::Brown` | FR-032, FR-035 |
  | `setWanderEnabled` | true | FR-068 |
  | `setWanderRate` | `kDefaultWanderRateHz = 0.03` Hz, applied at `prepare()`/`reset()` | FR-069 |

  The breathing default is chosen against the criteria, not the other way round: `depth = 0.25` gives
  a per-slot breathing factor in `[0.89, 1.11]` (±0.92 dB, FR-070), which is inside SC-001 (a)'s ±4.5 dB
  window with room for the other lanes and, summed incoherently over independently salted slots,
  inside SC-002 (c)'s broadband RMS CV cap of 0.28. No threshold is relaxed to accommodate a default:
  that cap is itself measured (min 0.1107 / median 0.1334 / max 0.1861 over 35 seeds), and the old 0.06
  was the guess — it had accounted for breathing alone while the wander lanes move level too.
- **FR-017** — **Per-type source drive calibration** (decided 2026-08-31, Q1, option b). Three verified
  facts collide without it: `NoiseGenerator`'s per-type level defaults to `kDefaultLevelDb = -20.0f`
  identically for all 13 types (`noise_generator.h:106`, `:597-600`), yet the 13 types have wildly
  different RMS at that same dB setting (velvet at the 100 imp/s floor is a sparse train near
  `sqrt(density/fs)` of its peak; blue/violet are one-sample differentiators), so SC-019 (a)'s
  "every selectable type/model renders above −60 dBFS" and SC-001 (c)'s "every reference-configuration
  window sits in (−60, −3] dBFS" would not hold for every type at a fixed level. The organism therefore
  ships a compile-time `static constexpr float kSourceDriveDb[kNumNoiseTypes]` table in
  `noise_organism.h`, one entry per `NoiseType`. **Measurement method (normative, recorded verbatim in
  the header):** render each of the 13 types alone, through a bare, prepared `NoiseGenerator` with only
  that type enabled at its own `kDefaultLevelDb`, for 5 s at 48 kHz; measure RMS in dBFS; set
  `kSourceDriveDb[type] = rmsDbfs(White) − rmsDbfs(type)` (`NoiseType::White` — flat spectrum, no
  shaping — is the 0 dB reference). Every `setNoiseLevel(type, requestedDb)` call the organism issues
  actually passes `requestedDb + kSourceDriveDb[type]`, clamped to `NoiseGenerator`'s own
  `[kMinLevelDb, kMaxLevelDb]`. This is a constant additive offset per type: deterministic, compile-time,
  monotone in `requestedDb` (so it cannot break `getSourceGain`'s SC-009 (a) monotonicity), and
  RT-safe — no control loop, unlike the rejected option (c) runtime auto-normalisation, which would
  make `getSourceGain` non-monotone. The table values are **not placeholder constants**: they are the
  output of the measurement procedure above, an explicit, checked step, not authored by guess.
- **FR-018** — **Per-resonator Q make-up gain** (decided 2026-08-31, Q1, option b, second half).
  Verified reason: `ResonatorBank`'s bandpass filters are documented constant-0-dB-peak-gain
  (`biquad.h:71`, configured at `resonator_bank.h:548`), but a bandpass admits less of a broadband
  source's total energy as Q rises, so a slot's RMS falls as the FR-052/FR-064 Q lane moves, with no
  compensating write anywhere in the existing design. The organism computes a make-up gain from each
  resonator's *currently configured* Q — the value FR-052 writes on every control step, wander included
  — and applies it through `ResonatorBank::setGain(index, gainDb)` (`:364`) on the same control step,
  immediately after the `setQ` write. The make-up law is **measured, not assumed**: the same
  measurement pass that produces FR-017's table also sweeps resonator Q at each FR-016 anchor and fits
  the dB-vs-Q curve that flattens broadband output RMS across the sweep; the fitted coefficients are
  compile-time constants recorded in the header alongside the measurement method used to derive them.
  The result is clamped so that a saturated `kMaxResonatorQ = 100` resonator — which FR-064's
  downward-only Q factor guarantees is the *quietest*, never the loudest, end of the lane — cannot push
  a slot above SC-001 (c)'s −3 dBFS ceiling. Deterministic, monotone in Q, RT-safe: no control loop,
  same properties as FR-017.

### FR-020 series — Composed model: filtered wind (roadmap line 186)

- **FR-020** — `FilteredWind` = brown noise through a band-pass `StochasticFilter` whose cutoff
  wanders. It adds **no new DSP**: it is a pinned configuration of the slot's existing parts.
- **FR-021** — Base type is pinned to `NoiseType::Brown` (`noise_generator.h:49`).
- **FR-022** — The slot's chain filter is configured `setBaseFilterType(SVFMode::Bandpass)`
  (`stochastic_filter.h:356`, `svf.h:41`), `setCutoffRandomEnabled(true)` (`:311`),
  `setCutoffOctaveRange` default 2.0 (`kDefaultOctaveRange`, `:112`), `setChangeRate` sourced from
  the FR-069 organism-wide wander-rate scalar like every other slot's chain filter (inside
  `[kMinChangeRate, kMaxChangeRate] = [0.01, 100]`, `:101-102`),
  `setSmoothingTime` default 400 ms (inside `[0, 1000]`, `:105-106`).
- **FR-023** — **Revised 2026-08-31 (Q7/FR-069).** The external cutoff lane (FR-062) remains active on
  top of the `StochasticFilter`'s own internal randomiser; neither is removed, and they are not
  redundant, but the reason is no longer a tempo difference. Before FR-069, the internal randomiser's
  `setChangeRate` was pinned to a fixed value independently of the external Brownian lane's smoothness,
  giving it a genuinely faster "texture" role. **Since FR-069 unifies both under one organism-wide
  rate scalar, they now share the same nominal tempo by default** (`kDefaultWanderRateHz = 0.03`) — the
  distinction between the two lanes is in **what** moves and **how**, not in speed: the external
  `BrownianDrift` is a continuous, mean-reverting walk of the filter's **base** cutoff; the internal
  randomiser is a series of discrete jumps to a new nearby target (`RandomMode::Walk`) around
  *whatever the base currently is*, each smoothed over `setSmoothingTime` (400 ms here). A slower- or
  faster-feeling internal texture relative to the external base motion is not independently reachable
  in this phase's public API — both move with `setWanderRate` together, which is the roadmap's
  Phase-10 "one target" requirement (FR-069) taking priority over independent per-lane tempo. This is
  a genuine, intentional trade recorded here rather than left implicit.

### FR-030 series — Composed model: granular dust (roadmap line 187)

- **FR-030** — `GranularDust` = velvet impulses used as **grain triggers**, each opening a windowed
  burst of a continuous dark-noise carrier. An impulse multiplied by an envelope is still an impulse,
  so the envelope must window a carrier — that is what "velvet impulses through grain envelopes"
  means here, and the header says so.
- **FR-031** — Trigger source: the slot's `NoiseGenerator` with **only** `NoiseType::Velvet` enabled.
  Verified property this relies on: velvet contributes `±1 × velvetGain` at impulse samples and
  exactly `0.0f` between them (`noise_generator.h:521-534`), so a `!= 0.0f` test on the generator
  output is an exact impulse detector, consuming no extra RNG stream and preserving determinism.
- **FR-032** — Carrier: one `NoiseOscillator` per slot (`primitives/noise_oscillator.h:67`), seeded
  per FR-005, colour selectable by `void setDustCarrierColor(std::size_t slot, NoiseColor c) noexcept`.
  **Revised 2026-08-31 (OQ-SHIPPED-DEFECTS)**: `NoiseOscillator::process()`'s Velvet/RadioStatic
  fallthrough is fixed in this phase (FR-098), so the technical reason for excluding those two colours
  is gone. The roster is **seven of eight** colours — White/Pink/Brown/Blue/Violet/Grey/RadioStatic —
  restricted only by a **musical-design** reason that survives the fix: `Velvet` is an impulsive,
  sparse colour (it is what FR-031 uses as the grain *trigger*), not a continuous "dark noise" signal,
  so it is unsuitable as a grain *carrier* regardless of whether `NoiseOscillator` renders it correctly.
  `setDustCarrierColor(slot, NoiseColor::Velvet)` is rejected and snapped to the default; every other
  colour, `RadioStatic` included, is now selectable. Default `NoiseColor::Brown` — Vorago sinks.
- **FR-033** — Grain window: one organism-level table of `kDustEnvelopeTableSize = 2048` floats,
  filled once in `prepare` by `GrainEnvelope::generate(table, 2048, GrainEnvelopeType::Hann)`
  (`core/grain_envelope.h:33`), read per active grain with `GrainEnvelope::lookup(table, 2048, phase)`
  (`:165`). One table serves all slots; the type is fixed (no runtime envelope selection — the
  roadmap does not ask for one).
- **FR-034** — Grain pool: `static constexpr std::size_t kMaxDustGrains = 24` **per slot**, a fixed
  array of `DustGrain { float phase; float phaseIncrement; float gain; bool active; }`. **`gain` is
  assigned once, at grain birth, per FR-036's concurrency-normalised law** (decided 2026-08-31, Q3,
  option b) — it is not recomputed for the lifetime of the grain. Overflow
  policy is **steal-oldest** — deterministic, allocation-free, and it cannot drop the newest event.
  **Why 24, derived:** `NoiseGenerator::setVelvetDensity` floors density at 100 imp/s
  (`noise_generator.h:315-317`, `std::clamp(density, 100.0f, 20000.0f)`) and FR-031 makes that
  generator the sole trigger source, so the lowest reachable density is 100 imp/s. FR-035 allows
  grains up to 200 ms, giving a mean concurrency of `100 × 0.200 = 20`. A pool of 8 would make the
  whole upper half of the grain-length range unreachable; 24 covers the mean with headroom, and
  steal-oldest handles the Poisson tail. SC-004 (c) sits at 100 imp/s × 40 ms — mean concurrency 4,
  ~17 % of the pool — so steal-oldest is genuinely a backstop there and not the normal path.
- **FR-035** — `void setDustGrainMs(std::size_t slot, float ms) noexcept`, requested value clamped
  `[5, 200]`, and `void setDustDensity(std::size_t slot, float impulsesPerSecond) noexcept`,
  requested value clamped `[100, 20000]` — the range `NoiseGenerator::setVelvetDensity` itself
  enforces (`noise_generator.h:315-317`, `std::clamp(density, 100.0f, 20000.0f)`; note the **floor of
  100**, which is why the concurrency rule below cannot be satisfied by lowering density alone).
  The mean-concurrency rule `density × grainMs / 1000 ≤ kMaxDustGrains` is then enforced
  **bidirectionally**, density first, grain length second:

  ```
  effectiveDensity = clamp(requestedDensity, 100.0f, 20000.0f);
  grainCeilingMs   = 1000.0f * kMaxDustGrains / effectiveDensity;
  effectiveGrainMs = min(clamp(requestedGrainMs, 5.0f, 200.0f), grainCeilingMs);
  ```

  Both effective values are readable — `[[nodiscard]] float getDustDensity(std::size_t) const noexcept`
  and `[[nodiscard]] float getDustGrainMs(std::size_t) const noexcept` — so the clamp is observable
  rather than silent. At the 100 imp/s floor the grain ceiling is `1000 × 24 / 100 = 240 ms`, i.e. the
  whole `[5, 200]` ms range is reachable there; at 20 000 imp/s it is 1.2 ms, so `effectiveGrainMs`
  becomes 1.2 ms. Steal-oldest (FR-034) remains the hard backstop for the Poisson tail, not the
  normal path.
- **FR-036** — **Concurrency-normalised grain gain** (decided 2026-08-31, Q3, option b). Per sample,
  the dust slot emits `carrier × Σ(active grain envelope × grain gain)`. At birth, each grain's `gain`
  is set to

  ```
  gain = sign * 1.0f / sqrt(max(1.0f, expectedConcurrency));
  ```

  where `sign` is the triggering velvet impulse's random polarity (`±1`, `noise_generator.h:529-530`)
  — free decorrelation of overlapping grains at no extra state or RNG draw — and `expectedConcurrency
  = effectiveDensity × effectiveGrainMs / 1000` (FR-035's effective, post-clamp values), **recomputed
  by the organism only when density or grain length changes**, not per grain and not per sample.
  **In-flight grains keep their birth gain** — recomputing a sounding grain's gain would step its
  amplitude; only newly triggered grains use the freshly recomputed `expectedConcurrency`. What keeps
  the level **continuous** across grain births and deaths, independent of the gain law, is that the
  Hann window starts and ends at exactly zero (`core/grain_envelope.h:33`), so no grain ever appears or
  disappears at a non-zero amplitude. **This replaces the prior fixed `1/sqrt(kMaxDustGrains)` divisor**,
  which bounded only the worst case and could not flatten level across the density sweep SC-019 (b)
  measures — this is the design that makes that criterion both passable and non-vacuous. Grains
  advance by `phaseIncrement = 1 / (grainSeconds × sampleRate)` and retire at `phase >= 1`.

### FR-040 series — Composed model: metallic hiss (roadmap line 188)

- **FR-040** — `MetallicHiss` = bright noise through an inharmonically tuned, wandering comb bank.
  No new DSP.
- **FR-041** — Base type is pinned to `NoiseType::Blue` (`noise_generator.h:50`);
  `void setHissBright(std::size_t slot, bool violet) noexcept` selects `NoiseType::Violet` (`:51`)
  instead, covering the roadmap's "blue/violet".
- **FR-042** — **The organism computes the inharmonic comb ratios itself** and writes them as per-comb
  base delays; it does *not* rely on `Tuning::Inharmonic`. Verified reason:
  `TimeVaryingCombBank::setCombDelay` unconditionally switches the bank to `Tuning::Custom`
  (documented at `timevar_comb_bank.h:189`, "Implicitly switches to Custom tuning mode"; the
  assignment `tuningMode_ = Tuning::Custom;` is at `:515`), and FR-063 drives `setCombDelay` on every
  control step — so a bank pinned to `Inharmonic` would be in `Custom` from the first control step
  onward, `setFundamental`/`setSpread` would stop governing the delays, and any test asserting
  `getTuningMode() == Tuning::Inharmonic` would fail. Instead the organism evaluates the bank's own
  documented inharmonic law, `f[n] = fundamental × sqrt(1 + n × spread)` (`:237`, implementation at
  `:959`), converts it to base delays `1000 / f[n]` ms, and applies FR-063's wander around those. The
  bank is in `Custom` mode **by design**; nothing asserts otherwise. Defaults: fundamental 60 Hz,
  spread 0.35, feedback 0.75 for `MetallicHiss` (0.55 elsewhere), all under the FR-090 cap. SC-020
  measures the comb-peak frequency ratios of an isolated `MetallicHiss` slot and asserts they are
  inharmonic — the property the model actually claims.
  **`setModDepth` and `setRandomModulation` are left at their library defaults of `0.0f`**
  (`timevar_comb_bank.h:414,416`); the bank's internal motion is unused. Verified reason: the class
  has no `setSeed`, and `prepare` hard-seeds every per-comb PRNG with
  `ch.rng.seed(12345u + static_cast<uint32_t>(i) * 7919u)` (`:466`, repeated in `reset()` at `:487`),
  so four identically configured slots would run **bit-identical** comb drift and LFO trajectories — a
  correlated, repeating motion inside a component whose premise is "nothing repeats exactly". All comb
  motion therefore comes from the FR-063 Perlin lanes, which are salted per slot and per comb
  (FR-005). Adding `TimeVaryingCombBank::setSeed` was considered and **deferred**: it would be a
  second shared-component amendment with its own no-change guarantee to write for `ContinuousBody`
  (`continuous_body.h:42`), bought for no capability the salted lanes do not already provide.
  **`FrequencyShifter` is out of scope for Phase 2** — the roadmap qualifies it "optional"
  (line 188), and a per-slot `FrequencyShifter` (`processors/frequency_shifter.h:98`, Hilbert pair +
  quadrature oscillator per sample, `:249`) at `kMaxSources = 4` would consume a large fraction of the
  1 %-per-voice budget before any of the prescribed chain is paid for. Its role — inharmonic detune —
  is delivered by the organism-computed ratios above plus the FR-063 wander. Recorded, with this
  derivation, rather than dropped.

### FR-050 series — Per-source chain (roadmap line 189)

- **FR-050** — Every slot's signal path is, in order: source → `ResonatorBank` → `TimeVaryingCombBank`
  → `StochasticFilter` → per-slot gain (level × breathing × wake gate) → organism mix. The order is
  the roadmap's, verbatim.
- **FR-051** — `static constexpr std::size_t kMaxResonatorsPerSource = 4`. Each slot owns one
  `ResonatorBank`; exactly `getNumResonators(slot)` of its 16 filters are enabled
  (`resonator_bank.h:398`), so per-sample cost scales with the enabled count (`:484-486`).
  `void setNumResonators(std::size_t slot, std::size_t n) noexcept` clamps to `[0, 4]`.
  At `n == 0` the organism **does not call `ResonatorBank::process` at all**: the source sample is
  passed straight to the comb stage. That is normative, not an optimisation. `ResonatorBank` has no
  count method at all (swept this session: `grep -n "setNum" resonator_bank.h` — no hits), so the
  organism's wrapper is a `setEnabled(i, i < n)` loop (`:398`); with nothing enabled `process()`
  returns `input × currentMix + wetSum × (1 − currentMix)` (`:511`) where `wetSum == 0` (the
  per-sample loop skips disabled resonators, `:484-486`) and `currentMix == 0` (`exciterMix_ = 0.0f`,
  `:589`, re-zeroed in `reset()` at `:235`, and pinned neutral by FR-055) — i.e. the stage emits
  **silence, not bypass**. Forwarding the call at `n == 0` would mute the whole slot.
- **FR-052** — Resonator anchors are set by
  `void setResonatorAnchor(std::size_t slot, std::size_t index, float hz) noexcept`, clamped to
  `[kMinResonatorFrequency, sampleRate × kMaxResonatorFrequencyRatio]` (`resonator_bank.h:42,45`).
  **The organism is the sole owner of `ResonatorBank`'s Q state.** `setDecay` and `setQ` write the
  *same* variable — `setDecay` assigns `qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])`
  (`resonator_bank.h:349`), `setQ` assigns `std::clamp(q, kMinResonatorQ, kMaxResonatorQ)` (`:383`) —
  so a component that alternates them silently destroys one setting with the other. The organism
  therefore calls `setDecay` **exactly once per resonator, at configuration time** (FR-053), so that
  `getDecay` reports the nominal RT60, and thereafter drives Q through **`setQ` only**. On each
  control step, immediately after the `setFrequency` call for that resonator, it writes

  ```
  targetQ = clamp(rt60ToQ(driftedHz, decaySeconds) * qFactor, kMinResonatorQ, kMaxResonatorQ);
  ```

  where `rt60ToQ` is the free function at `resonator_bank.h:92-98`, re-evaluated by the organism on
  the control grid, and `qFactor` is FR-064's wander factor. That is what makes the configured RT60
  track the drifted frequency — `setFrequency` does not re-derive Q (`:328-332` vs `:345-352`) — while
  leaving exactly one writer of `qValues_`. `setDecay` is never called again after configuration, and
  FR-064 adds no second write path.
- **FR-053** — Resonator decay is `void setResonatorDecay(std::size_t slot, float seconds) noexcept`,
  clamped `[kMinDecayTime, kMaxDecayTime] = [0.001, 30]` (`resonator_bank.h:54,57`), applied to every
  enabled resonator in the slot. It reaches `ResonatorBank::setDecay` (`:345`) **once per resonator,
  at configuration time only**; thereafter the value is held by the organism and enters the audio path
  solely through FR-052's single `setQ` write. RT60 is therefore *nominal at the anchor*: the organism
  re-derives the Q that realises it at the drifted frequency on every control step.
- **FR-054** — `static constexpr std::size_t kMaxCombsPerSource = 4`. Each slot owns one
  `TimeVaryingCombBank`. The organism's **own** setter
  `void setNumCombs(std::size_t slot, std::size_t n) noexcept` clamps `n` to `[0, 4]` at the
  `NoiseOrganism` level. **`TimeVaryingCombBank::setNumCombs` cannot take 0** — it clamps to
  `std::clamp(count, size_t{1}, kMaxCombs)` (`timevar_comb_bank.h:502`) — and `process()` returns only
  the sum of the active combs, with no dry path (`:328-330`). Forwarding a user-facing `0` into it
  would leave **one** comb running, not a bypassed stage. So: the organism calls
  `TimeVaryingCombBank::setNumCombs` only with values in `[1, 4]`, and at `n == 0` it **skips the
  bank's `process`/`processBlock` call entirely**, passing the resonator-stage output straight to the
  chain filter. `prepare` sizes all `kMaxCombs = 8` delay lines regardless (`:154`, `kMaxCombs` at
  `:88`) — that is the FR-096 memory figure, not a leak.
- **FR-055** — `ResonatorBank::setSpectralTilt` is left at exactly `0.0f` and is **not** exposed.
  Verified reason: at any non-zero tilt, `calculateTiltGain` costs a `std::log2` plus a `dbToGain`
  **per enabled resonator per sample** (`resonator_bank.h:120-126`, called at `:504`); at zero it
  early-returns `1.0f` (`:121-123`). Spectral shaping is the chain filter's job. `setDamping` is
  likewise fixed at its neutral default. `setExciterMix` is fixed at its default `0.0f`
  (`resonator_bank.h:589`, re-zeroed in `reset()` at `:235`), which in this component's sign
  convention means **fully wet** — `process()` returns `input × mix + wetSum × (1 − mix)` (`:511`).
  That is precisely why FR-051 must *skip* the stage at zero resonators rather than forward a zero
  count: fully wet with nothing enabled is silence, not bypass.
- **FR-056** — The chain filter is one `StochasticFilter` per slot, `prepare(double, size_t)`
  (`stochastic_filter.h:139`), default `RandomMode::Walk` (`:44-49`), default base type
  `SVFMode::Lowpass`, base cutoff 800 Hz, base resonance 0.7, `setSmoothingTime` default 200 ms
  (inside `[0, 1000]`, `:105-106`).
  **The organism overrides three library defaults and pins two flags; none is left implicit.**
  `setChangeRate` is sourced from the FR-069 organism-wide wander-rate scalar (default
  `kDefaultWanderRateHz = 0.03` Hz) and `setCutoffOctaveRange` is pinned to **1.0** (`FilteredWind`:
  2.0, FR-022) — the library defaults are `kDefaultChangeRate = 1.0f` (`:103`) and
  `kDefaultOctaveRange = 2.0f` (`:112`), i.e. a 1 Hz, 2-octave wander that is an order of magnitude
  faster than anything this component is about, and that would dominate every spectral-motion
  measurement. `setCutoffRandomEnabled` is left **true** (its default, `:555`) — FR-023 keeps this
  internal randomiser running deliberately, distinct in character (not, since FR-069, necessarily in
  tempo) from the external cutoff lane — but it is reachable and switchable through FR-068.
  `setTypeRandomEnabled` is left **false** (its default, `:557`): random filter-type switching is a
  discrete jump that would fight the "nothing repeats, nothing clicks" requirement, and the roadmap
  asks only for wandering parameters. `setResonanceRandomEnabled` is left **false** (its default,
  `:556`) because FR-067 wanders resonance through an external, *salted* lane instead; enabling both
  would double-modulate one parameter for no added identity.
- **FR-057** — **Comb tuning control surface** (decided 2026-08-31, Q8, option a). FR-016 already
  names comb fundamental, spread and feedback as *defaults*, which makes them de facto parameters with
  no way to set them — Phase 12 needs something to register. The organism adds
  `void setCombTuning(std::size_t slot, float fundamentalHz, float spread) noexcept` (forwarded into
  the FR-042 ratio computation the organism performs itself; `fundamentalHz` clamped
  `[kMinResonatorFrequency, sampleRate × kMaxResonatorFrequencyRatio]`, `resonator_bank.h:42,45`, the
  same bound FR-052 uses; `spread` clamped `[0, 1]`) and
  `void setCombFeedback(std::size_t slot, float feedback) noexcept`, clamped
  `[0, kCombFeedbackCap] = [0, 0.9]` — FR-090's stability cap, not `TimeVaryingCombBank`'s own (higher)
  limit. Matching getters (`getCombFundamental`, `getCombSpread`, `getCombFeedback`) are added to the
  FR-015 read surface. **No `setNoteFrequency` or key-follow amount is added in this phase** — see
  Non-Goals: roadmap Phase 3 already specifies free/keyed/hybrid anchoring as an explicit control for
  the resonance drift network, and one keying idiom should cover both components rather than Phase 2
  inventing its own. The noise bed is therefore unpitched by default.

### FR-060 series — Life modulation (roadmap lines 190–191: "All filter parameters wander")

- **FR-060** — Every wandering parameter is driven by an organism-owned lane, advanced on the FR-007
  control grid. Lanes are private members (the `harmonic_cloud.h:1863-1964` precedent); none is
  exposed as a `ModulationSource` and none is routed through `ModulationEngine` (see Non-Goals).
- **FR-061** — **Resonator frequency**: one `BrownianDrift` per enabled resonator per slot
  (≤ 16 lanes). The lane's `[-1,+1]` output is mapped to a bounded semitone offset around the anchor,
  `void setResonatorWander(std::size_t slot, float semitones, float smoothness) noexcept`, semitones
  clamped `[0, 12]` (default 2.0), smoothness forwarded to `BrownianDrift::setSmoothness`
  (`brownian_drift.h:152`, τ ∈ `[0.2, 30]` s, `:97-99`). Mean reversion makes the excursion bounded
  by construction. Drifted frequencies are re-clamped to the `ResonatorBank` range before every
  `setFrequency`.
- **FR-062** — **Filter cutoff**: one `BrownianDrift` per slot driving `setBaseCutoff`
  (`stochastic_filter.h:343`) over a bounded octave span,
  `void setFilterWander(std::size_t slot, float octaves, float smoothness) noexcept`, octaves clamped
  `[0, 6]` (default 1.5), around `void setFilterBaseCutoff(std::size_t slot, float hz) noexcept`.
  Result is clamped to `[20 Hz, 0.45 × sampleRate]` before the call.
- **FR-063** — **Comb delay**: one `PerlinNoiseSource` per comb per slot (≤ 16 lanes) driving
  `setCombDelay` (`timevar_comb_bank.h:189`) around a per-comb base delay, span set by
  `void setCombWander(std::size_t slot, float percent, float ratePerSecond) noexcept`, percent clamped
  `[0, 50]` (default 12), rate forwarded to `PerlinNoiseSource::setRate`
  (`perlin_noise_source.h:242`, `[0.005, 5]` cells/s, `:177-179`), default 0.03. No extra slew limiter
  is added: `TimeVaryingCombBank` already smooths delay changes with
  `kDelaySmoothingMs = 20.0f` (`:109`). This lane is the **sole** source of comb motion: the bank's own
  `setModDepth`/`setRandomModulation` stay at their `0.0f` defaults for the unsalted-PRNG reason given
  in FR-042. Writing `setCombDelay` puts the bank in `Tuning::Custom` (`timevar_comb_bank.h:515`),
  which is the intended and documented state — see FR-042.
- **FR-064** — **Resonator Q** wanders on the same `BrownianDrift` lane as that resonator's frequency,
  scaled by a separate depth `void setResonatorQWander(std::size_t slot, float amount) noexcept`
  (`[0, 1]`, default 0.25). The lane's `[-1, +1]` output `b` becomes a **strictly downward** factor

  ```
  qFactor = 1.0f - kQWanderSpan * amount * (1.0f + b) * 0.5f;   // kQWanderSpan = 0.9f
  ```

  i.e. `qFactor ∈ [1 − 0.9·amount, 1]`, clamped into `[kMinResonatorQ, kMaxResonatorQ]`
  (`resonator_bank.h:48,51`) by FR-052's expression. It is applied **inside FR-052's single `setQ`
  write**; FR-064 introduces no second writer of `qValues_` and never calls `setDecay`.
  **Why downward-only — derived, not asserted:** `rt60ToQ` saturates at `kMaxResonatorQ = 100`
  (`resonator_bank.h:96-97`) for every `frequency × RT60 > 100 × ln1000 / π = 219.9`. At the
  drone-scale decays this component exists for (FR-016's 1.5 s default, up to `kMaxDecayTime = 30 s`,
  `:57`) every default anchor is already at that ceiling, so an *upward* factor would be silently
  clipped and the lane would be half-inert. A downward factor is audible at every base Q, saturated or
  not — which is what SC-021 measures.
  A second independent lane per resonator is **not** added: the roadmap asks for wandering Q, not for
  Q that is statistically independent of frequency, and 16 further lanes buy nothing audible.
- **FR-065** — Each lane's seed is `deriveStreamSeed(organismSeed, salt)` with a distinct salt
  (FR-005), so no two lanes — and no two slots — share a trajectory.
- **FR-066** — Setting any wander depth to 0 freezes that parameter at its base value **without**
  stopping the lane: lanes keep advancing so that turning depth back up does not jump. (The
  `harmonic_cloud.h:897-901` quiescent-lane rule.)
- **FR-067** — **Filter resonance**: one `BrownianDrift` per slot driving `setBaseResonance`
  (`stochastic_filter.h:350`) around `void setFilterBaseResonance(std::size_t slot, float q) noexcept`,
  span set by `void setFilterResonanceWander(std::size_t slot, float amount, float smoothness) noexcept`,
  `amount` clamped `[0, 1]` (default 0.2), smoothness forwarded to `BrownianDrift::setSmoothness`
  (`brownian_drift.h:152`). The result is clamped into the SVF's legal Q range before the call.
  This lane exists because the roadmap's Phase-2 core requirement is "**All filter parameters wander**"
  (Vorago-roadmap.md:190-191) and resonance is a filter parameter: without it the chain filter's Q is
  the one named parameter that never moves. The alternative — `setResonanceRandomEnabled(true)`
  (`stochastic_filter.h:316-318`, default `false` at `:556`) with a bounded `setResonanceRange`
  (`:386-389`) — is **not** taken: the external lane is salted per slot (FR-005) and runs on the same
  seconds-to-minutes clock as every other lane, whereas the internal randomiser shares the filter's
  own change-rate clock. Enabling both would double-modulate one parameter with no added identity.
- **FR-068** — `void setWanderEnabled(bool enabled) noexcept`, default `true`, is the single control
  that produces a genuinely wander-free render. It zeroes the contribution of **every** external lane
  (FR-061 … FR-067) *and* calls `setCutoffRandomEnabled(false)` on every slot's `StochasticFilter`.
  Both halves are required: FR-023 deliberately retains the filter's internal cutoff wander, whose
  defaults are `cutoffRandomEnabled_ = true` (`stochastic_filter.h:555`) at `kDefaultChangeRate` and a
  2-octave `kDefaultOctaveRange` (`:103,112`), so zeroing the external depths alone would leave a fast
  spectral wander running on every slot and no control arm would be reachable. Lanes keep advancing
  while disabled (FR-066), so re-enabling does not jump. SC-002 (b) and SC-009 (a) name this setter as
  the mechanism that produces their control arms; `isWanderEnabled()` reports it.
- **FR-069** — **Single organism-level wander-rate scalar** (decided 2026-08-31, Q7, option a).
  `void setWanderRate(float hz) noexcept` maps to all four wander-lane kinds at once, for every slot:

  | Lane kind | Mapping |
  |---|---|
  | Brownian smoothness (resonator freq FR-061, filter cutoff FR-062, filter resonance FR-067) | `tau = clamp(1.0f / hz, kTauMin, kTauMax)` = `clamp(1/hz, 0.2, 30)` s (`brownian_drift.h:96-99`), forwarded to `BrownianDrift::setSmoothness` |
  | Comb-delay `PerlinNoiseSource` rate (FR-063) | `hz`, forwarded directly to `setRate` (`perlin_noise_source.h:242`, clamped `[0.005, 5]` cells/s, `:177-179`) |
  | Chain filter's own internal randomiser (FR-022, FR-056) | `hz`, forwarded directly to `StochasticFilter::setChangeRate` (`:417`, clamped `[0.01, 100]`, `:101-102`) |
  | Breathing (FR-070) | `hz`, forwarded directly to `BreathingModulator::setRate` (`:170`, clamped `[0.01, 0.5]`) |

  **`static constexpr float kDefaultWanderRateHz = 0.03f`** — chosen to leave the Perlin comb rate
  (the previous, single T-defining lane) numerically unchanged, so **SC-002 stands exactly as written**
  (its `T = 1/r = 33.3 s` anchor does not move). Applied implicitly at `prepare()`-time and by every
  `reset()` re-apply (FR-004); the FR-016 defaults table's individual rate/smoothness rows are now
  *derived* from this one constant, not independently authored — see the updated FR-016 table.
  **What happens at the ends of the range, documented explicitly:** the Brownian mapping is the only
  one that clamps at the default — `1 / 0.03 = 33.3` s exceeds `kTauMax = 30` s, so the three Brownian
  lanes run at the **clamp ceiling** (30 s tau) by default, not at 33.3 s. This is a deliberate,
  measured consequence, not an oversight: it is what fixes the SC-002 failure mode the roadmap's own
  problem statement names — before this scalar existed, the chain filter's cutoff `BrownianDrift` lane
  defaulted to a `smoothness` of 0.6 s (an order of magnitude faster than the Perlin comb's 33.3 s
  period), so it dominated *band-energy* motion at a lag far below SC-002 (a)'s `[0.4T, 3.0T]` window
  on a correct implementation. At `setWanderRate`'s low end (`hz → kMinChangeRate = 0.01`, i.e. a
  100 s target period) every Brownian lane is already pinned at the same 30 s ceiling as the default —
  raising the rate below 0.033 Hz has no further effect on the Brownian lanes, only on the Perlin/
  filter/breathing lanes, which continue to slow linearly. At the high end
  (`hz → kMaxChangeRate = 100`), the Perlin rate clamps first, at its own `kMaxRate = 5` cells/s
  (`perlin_noise_source.h:179`), well before the Brownian tau clamps at its floor `kTauMin = 0.2` s
  (`hz = 5`). **FR-023 is revised accordingly** — see below.
  **Per-slot rate offsets are not added in this phase.** The roadmap's Phase 10 "Movement" macro gets
  exactly one target through this scalar; per-slot deviations may be layered on later if wanted, but
  the scalar is the primary control, as decided.

### FR-070 series — Breathing and event hooks (roadmap lines 192–193)

- **FR-070** — Per-slot level breathing is one `BreathingModulator` per slot
  (`breathing_modulator.h:105`), configured by
  `void setSourceBreathing(std::size_t slot, float rateHz, float depth, float irregularity) noexcept`.
  `rateHz` is forwarded to `setRate` (`:170`, `[0.01, 0.5]` Hz, `:108-110`) and `irregularity` to
  `setIrregularity` (`:184`). **`BreathingModulator::setDepth` is left at its library default `1.0f`**
  (`kDefaultDepth`, `:112`) so there is exactly one owner of the breathing depth: `depth` is clamped
  `[0, 1]` by the organism and applied in the gain mapping below, never also forwarded to `setDepth`
  (which would square it).
  **Mapping (normative).** The modulator's output `b` is **bipolar `[-1, +1]`** (`:103`;
  `getSourceRange()` returns `{-1.0f, 1.0f}` at `:227-229`), so a bare multiply would flip the slot's
  polarity on every exhale and null it at each zero crossing. The slot's breathing factor is instead
  the affine map

  ```
  gBreath = 1.0f + kBreathGainSpan * depth * b;   // kBreathGainSpan = 0.45f
  ```

  which is exactly `1.0f` when `b == 0` (so `depth == 0` is neutral, not a constant duck) and lies in
  `[1 − 0.45·depth, 1 + 0.45·depth] ⊆ [0.55, 1.45]` for every legal `depth`: **strictly positive,
  never zero, never sign-changing**, and ±0.92 dB at the FR-016 default `depth = 0.25`. `gBreath`
  multiplies the slot gain alongside level and the wake gate (FR-050). SC-001 asserts the bound and
  the sign directly.
- **FR-071** — Dormancy: `void setSourceDormant(std::size_t slot, bool dormant) noexcept`.
  **Revised 2026-08-31 (Q6, option a — "source runs, chain skipped"):** a dormant slot contributes
  exactly zero to the mix and **skips only the resonator/comb/`StochasticFilter` chain stages**; its
  `NoiseGenerator`/`NoiseOscillator` source **keeps rendering into a scratch buffer**, and its wander
  lanes keep advancing, so waking a slot reveals neither a frozen chain filter nor a rewound noise
  stream, and the source's colour-filter state (brown's leaky integrator, pink's Kellet chain) is
  exactly what an always-awake slot would have. This is the only reading under which FR-071's
  freewheeling claim and SC-010 (a)'s dormant-then-woken-vs-always-awake source-RMS agreement can both
  be true — freewheeling the RNG alone without running the source would leave the colour filters stale
  on wake. This is the roadmap's "dormant source" (line 193). The residual cost is a **measured**
  number, not an unquantified claim — SC-004 adds a "4 slots, all dormant" configuration.
- **FR-072** — Awakening: `void setSourceWake(std::size_t slot, float amount) noexcept`, `amount`
  clamped `[0, 1]`, is the event hook. It is a **plain scalar input**, not a scheduler reference:
  Phase 10 owns the `SlowEventScheduler` and writes `scheduler.getEnvelopeValue()`
  (`slow_event_scheduler.h:391`) — or `getCurrentValue()` (`:331`) rectified — into this setter when
  `getActiveTarget()` (`:356`) matches the slot. Vorago Phase 1's OQ-3 deferred all routing to
  Phase 10; this keeps that boundary.
- **FR-073** — **Revised 2026-08-31 (Q4, option a).** The wake amount multiplies the slot gain through
  a **per-sample ramp, linear in gain (not in dB)**, 50 ms total — the same ramp law FR-013's
  duck-and-restore uses for its 25 ms + 25 ms legs. The domain is stated explicitly because it matters:
  for a −96 → +12 dB step the linear-in-gain and linear-in-dB shapes differ by orders of magnitude.
  The gain is recomputed **every sample**, not held across the FR-007 64-sample control grid — a
  1.33 ms staircase on the one signal whose monotonicity is a success criterion (SC-009 (a)) is not
  acceptable. A slot at `amount == 0` with `dormant == false` still runs its chain (so it re-enters at
  the correct filter state).
  `setSourceDormant(slot, true)` (FR-071) skips only the chain stages, not the source — it is **not**
  "the cheap variant that also skips the DSP" in the sense of stopping the source render; it applies
  over the same 50 ms per-sample linear ramp so it cannot click.
- **FR-074** — The organism mix is the sum of active slot outputs times a fixed `1/sqrt(kMaxSources)`
  headroom constant, followed by a final clamp to `[-4, +4]` as a non-audible NaN/blow-up backstop.
  It is never reached in normal operation, and that is **observable rather than asserted**: the
  organism counts engagements in a saturating counter exposed as
  `[[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept` (FR-015), cleared by
  `prepare` and `reset`. SC-005 requires it to be 0.

### FR-080 series — `NoiseGenerator::setSeed` (shared-component amendment)

- **FR-080** — `NoiseGenerator` gains `void setSeed(std::uint32_t seed) noexcept`, signature copied
  from `NoiseOscillator::setSeed` (`primitives/noise_oscillator.h:109`), plus
  `[[nodiscard]] std::uint32_t getSeed() const noexcept`. It seeds `rng_` (`noise_generator.h:593`)
  immediately and latches the value.
- **FR-081** — **Existing behaviour is preserved bit-for-bit for every current consumer.** `reset()`
  keeps its `rng_.seed(rng_.next() ^ 0xDEADBEEF)` scramble (`:189`) *unless* `setSeed` has been called
  on that instance, in which case `reset()` re-seeds to the latched value. No consumer
  (`systems/character_processor.h`, `systems/tape_machine.h`, `effects/pattern_freeze_mode.h`,
  `primitives/noise_oscillator.h`, `plugins/membrum/src/dsp/exciters/noise_burst_exciter.h`) calls
  `setSeed`, so all five see zero change. Trace: roadmap line 481 ("shared-component changes must keep
  [existing consumers'] tests green").
- **FR-082** — Rationale, recorded because it is the one shared-component change in this phase: without
  it, `kMaxSources` slots emit **bit-identical** noise (all `Xorshift32 rng_{12345}`, `:593`, advanced
  identically), summing coherently at +12 dB instead of the +6 dB of four independent sources — i.e.
  roadmap line 185's "N simultaneous noise sources" would be one source at four times the level. The
  alternative considered and rejected: calling `reset()` a per-slot number of times to walk each
  instance's RNG apart. It works only because of an undocumented side effect of `:189`, is silently
  broken by any future `reset()` change, and produces no reproducible seed. Rejected.
- **FR-083** — No other `NoiseGenerator` behaviour, signature or default changes. `kNumNoiseTypes`
  stays 13; the `NoiseType` enum is untouched.

### FR-090 series — Safety, budget, footprint

- **FR-090** — Output is bounded for **every** legal parameter combination over arbitrarily long runs:
  no divergence, no decay to silence with sources active. Every stochastic element is
  bounded by construction (Ornstein–Uhlenbeck mean reversion, `brownian_drift.h:94-99`; Perlin lattice
  range, `perlin_noise_source.h:189`) and every feedback element is capped
  (`setCombFeedback` ≤ 0.9 enforced by this component, below `TimeVaryingCombBank`'s own limit; total
  resonator Q ≤ `kMaxResonatorQ`). Trace: roadmap lines 471–474.
- **FR-091** — Zero allocation after `prepare()`: the render path and every setter are
  allocation-free. All pools, tables and lanes are `std::array` members or are sized in `prepare`.
- **FR-092** — Deterministic under seed: same seed + same configuration + same sample rate ⇒ same
  render, within one process/binary. No wall-clock, no thread id, no address-derived value enters any
  stream.
- **FR-093** — **The organism's own** time constants are sample-rate independent by construction:
  every one is expressed in seconds or Hz and re-derived in `prepare` (wander rates, breathing rate,
  smoothing times, grain lengths, ramp durations, comb base delays in ms). Supported range
  44.1–192 kHz.
  **The reused `NoiseGenerator` colour filters are not, and this spec does not pretend otherwise.**
  Verified this session: brown noise is a leaky integrator with a hard-coded coefficient
  (`constexpr float kBrownLeak = 0.98f`, `noise_generator.h:467-468`), so its corner moves from
  ~155 Hz at 48 kHz to ~620 Hz at 192 kHz; blue is a one-sample differentiator of pink (`:483`) and
  violet a one-sample differentiator of white (`:498`), both of which redistribute energy across fixed
  Hz bands as Nyquist moves; pink uses Paul Kellet's fixed coefficients, tuned at 44.1 kHz
  (`primitives/pink_noise_filter.h:65-70`).
  **Three of the four were fixed in this phase (2026-09-01), each anchored at 44.1 kHz so the reference
  rate is reproduced exactly and no existing consumer moves there.** Measured through three fixed-Hz
  resonators (70/140/260 Hz), deviation from 48 kHz at 44.1/96/192 kHz:
  white was −0.75/+3.01/+6.02 dB and is now −0.75/−0.23/−0.18 (its fixed *sample variance* meant its
  spectral **density** halved per rate doubling; it now carries `sqrt(fs/44100)`);
  brown was likewise rate-dependent and is now −0.78/−0.47/−0.44 (`kBrownLeak = 0.98` became a fixed-Hz
  corner, `exp(-1/(fs·τ))`);
  pink was −1.14/+2.14/+4.00 and is now −0.78/−0.47/−0.48 (`PinkNoiseFilter::prepare` maps each Kellet
  pole to the running rate, preserving its time constant in seconds and its DC gain).
  **Blue and violet are deliberately still not compensated, and that is now a measured conclusion rather
  than an accepted limitation.** The compensation was implemented and removed: an `fs/44100` factor does
  fix blue at 96 kHz (measured +0.01 dB) but cannot hold at 192 kHz, for a structural reason. Blue's
  spectrum *rises* at +3 dB/oct, so holding its density fixed in Hz makes its total power grow as `fs²`
  and its RMS as `fs` — measured, the raw generator went −16.69 → −4.09 dBFS from 44.1 to 192 kHz, +12.6 dB
  against the +12.8 dB the integral predicts. That overruns the `[-1, 1]` contract its clamp enforces, and
  the clamp's broadband distortion then read **+6.44 dB** through the resonators, *worse* than the
  uncompensated error it was meant to remove. Rate-invariant density and bounded amplitude are not
  simultaneously available for a rising-spectrum noise over a 4.35× rate range, and amplitude wins: a
  colour 12.8 dB louder at 192 kHz is a musical defect where the density tilt is a documented limitation.
  One consequence is recorded honestly: with pink now correct, blue's own error is *larger* in magnitude
  (−6.01/−11.96 dB at 96/192 kHz, versus −3.38/−7.41 before) because pink's rate error had been partially
  cancelling the differentiator's. It is now the pure, predictable `1/fs` law rather than two errors
  offsetting. SC-008 (a)'s ±1.0 dB overall-RMS bound still passes with `MetallicHiss` pinning Blue. `FilteredWind` pins Brown (FR-021) and `MetallicHiss` pins
  Blue/Violet (FR-041), so the FR-016 reference configuration contains three of them. Their **spectra
  are therefore rate-dependent**, and SC-008 measures only what is genuinely invariant.
  **This paragraph originally said fixing them was out of scope because it would change the output of five
  existing consumers (FR-081's list). Three of the four were fixed anyway, and the stated risk did not
  materialise — every fix is anchored at 44.1 kHz, where it reproduces the previous coefficients exactly,
  so no consumer's output moves at the reference rate. `dsp_primitives_tests` and `dsp_processors_tests`
  both pass unchanged.** What did move is this phase's own 48 kHz render fingerprint, which was regenerated
  against the deliberate change and had the attribution verified by neutralising the two lines and
  confirming the previous golden passes.
- **FR-094** — Portable: passes `node tools/check-portability.js`; no `std::isnan`/`std::isinf`; no
  narrowing in brace initialisation (designated initialisers for `PrepareConfig`); no new SIMD
  (so `tools/lint-simd-aligned-loadstore.js` is trivially satisfied). Trace: roadmap line 479–480.
- **FR-095** — CPU: ≤ **1.75 %** of one core at 48 kHz per voice in the **reference configuration**
  (SC-004), i.e. ≤ **186 666 ns** per 512-sample block. The reference configuration and the
  out-of-region configuration are named in SC-004.
  **Raised from 1 % (106 666 ns) on 2026-09-01 by explicit user decision**, which is the one route this
  FR's stop-and-surface rule leaves open (option C below). It was taken with the measured per-stage table
  in hand, and the alternatives were priced first: only **one** cap reduction actually fits —
  `kMaxSources` 4 → 2 (~86 477 ns) — while every smaller one still misses (slots 4 → 3 ≈ 114 640 ns,
  dust pool 24 → 12 ≈ 128 510 ns, resonators 3 → 2 ≈ 133 426 ns), and halving the slot count would have
  redefined the SC-004 (c) reference configuration itself. Option A (hoisting `StochasticFilter`, the
  largest slot cost at 10 074 ns × 4 = 30 % of the total) projected ~30 700 ns of saving by analogy with
  the comb bank's own hoisted path (15 251 → 3 665 ns, a 4.16× cut), landing at ~104 600 ns — inside 1 %
  by only ~2 %, which is not a margin worth building a budget on. So the **ceiling**, not the instrument,
  was what was wrong: 1 % was set before the cost of four slots of per-slot stochastic filtering was known.
  **The number is set from a distribution, not one sample.** The first figure surfaced was 142 794 ns and it
  was not reproducible: five isolated runs gave 159 023.6 / 153 616.8 / 158 896.0 / 154 703.6 / 163 491.8 ns
  — centre ~158 000 (1.48 % of a core), run-to-run spread 6.4 % — because the session's absolute timings
  drifted ~14 % upward under sustained benchmarking and 142 794 was taken at the cool end. 1.75 % covers the
  observed **maximum** (163 492) with ~14 % margin. Read that spread before trusting any absolute ns figure
  here: it is why every perf test in this repo is excluded from CI, and why a number measured once is not
  evidence.
  **On a miss, no direction is pre-committed (decided 2026-08-31, OQ-CPU-POLICY) — this overrides and
  replaces this FR's prior "the caps come down, never the budget" commitment, and deliberately departs
  from the general project convention at `atmosphere_engine_perf_test.cpp:65`
  ("REDUCE COST, NEVER RAISE THE BASELINE") for this phase.** If SC-004 (c) — the reference
  configuration — misses 106 666 ns, the build **stops** and **surfaces**, rather than silently
  resolving: the measured ns/512-block figure, and a per-stage breakdown (source, resonators, combs,
  `StochasticFilter`, dust grains), so the trade-off is judged on real numbers. Neither lowering the
  caps (`kMaxSources`, `kMaxResonatorsPerSource`, `kMaxCombsPerSource`, `kMaxDustGrains`) nor raising
  the 1 % budget may be applied unilaterally by an implementing agent, and under **no** circumstance
  may the threshold be relaxed to make a test pass — that prohibition is not relaxed by this policy.
  Precedent recorded for context, not as a rule this phase must follow either way: Seraphis's
  `AtmosphereEngine` went 1 % → 1.5 % by an explicit user call, so raising is a legitimate outcome when
  the user makes it; Vorago Phase 1's own per-sample cost assumption was off by 1.4× against its own
  ceiling and only measurement caught it, which is exactly why these caps are not to be trusted until
  measured.
  **In-region envelope (resolves the SC-004 (d) tension).** The 1 % budget is a gate over the
  configurations a Vorago voice may actually instantiate, and that envelope is named here rather than
  left implicit: **at most 4 slots × 3 resonators × 2 combs, at most one `GranularDust` slot, dust
  concurrency at most 50 % of the FR-035 ceiling.** SC-004 (a), (b) and (c) sit inside it and are
  gated against 106 666 ns. SC-004 (d) — every cap maxed — is deliberately **outside** it and exists
  only as a regression tripwire against its own baseline, the
  `atmosphere_engine_perf_test.cpp:44-50` convention. Phase 10 (`VoragoVoice`) is bound by this
  envelope: if it needs to go outside it, the miss-handling policy above applies — stop and surface,
  not a unilateral cap change.
  Trace: roadmap line 197.
- **FR-096** — Memory: the header documents the per-instance heap footprint, whose only significant
  term is `kMaxSources × kMaxCombs × delay-line bytes` — `TimeVaryingCombBank::prepare` sizes all
  eight comb delay lines regardless of `setNumCombs` (`timevar_comb_bank.h:154,88`) and `DelayLine`
  rounds to a power of two (`primitives/delay_line.h:255,275`). At the default
  `maxCombDelayMs = 50` and 48 kHz that is 4 × 8 × 4096 × 4 B = **512 KiB per organism**, plus an
  8 KiB shared dust envelope table. `maxCombDelayMs` is the knob that trades it.
  The same figure is computed at runtime and exposed as
  `[[nodiscard]] std::size_t getAllocatedBytes() const noexcept` (FR-015): `prepare` accumulates the
  byte count of every buffer length it requests. That accessor is what SC-014 measures, because
  `AllocationDetector` counts allocations and not bytes — `recordAllocation()` does
  `allocationCount_.fetch_add(1, …)` (`allocation_detector.h:83-89`) and the replaced operator-new
  forms discard the size argument (`allocation_operator_overrides.h:66-95`). Extending that shared
  helper with a byte accumulator is **not** in this phase's scope; the component reporting its own
  sizing is the smaller change and is independently useful to Phase 10.
- **FR-097** — The new test TUs are registered **by name** in `dsp/tests/CMakeLists.txt`'s
  `dsp_systems_tests` list (the list is enumerated, not globbed — `:360-361` says so explicitly).
  Exactly **one** of them, `unit/systems/noise_organism_nonfinite_test.cpp`, is additionally added to
  the `-fno-fast-math -fno-finite-math-only` block, following the established precedent in that file
  — `atmosphere_engine_nonfinite_test.cpp` (`:758`), `aether_reverb_nonfinite_test.cpp` (`:768`) and
  `seraphis_nonfinite_test.cpp` (`:776`) are listed there and their siblings deliberately are not
  (`:745-777`). The other new TUs, and the perf TU in particular, **must not** be added: the perf
  baselines are pinned to figures `-fno-fast-math` would change (`:757`), and the ordinary TUs must
  build in the FP mode the header actually ships in so the FR-008 guards are proved under
  `-ffast-math` on the macOS leg. SC-015 is the criterion that consumes this.

### FR-098 series — Shared-component defect fixes (decided 2026-08-31, OQ-SHIPPED-DEFECTS)

Both defects below were found, cited, and originally scoped as "reported here and not fixed" while
writing this spec's first draft. The 2026-08-31 clarification session reversed that: the user chose to
widen scope and fix both, in this phase, each behind a failing test written first. This mirrors the
FR-080 series' shared-component-amendment pattern (existing consumer suites gate the change; no
consumer's behaviour may move silently — see SC-011, broadened below).

- **FR-098** — `NoiseOscillator::process()` gains a `case` for `NoiseColor::Velvet` and
  `NoiseColor::RadioStatic` (`primitives/noise_oscillator.h:265-267`), rendering the actual colour
  instead of silently falling through to white via `default:`. **Zero-regression-risk, verified this
  session:** every existing consumer of `NoiseOscillator` (`ring_modulator.h:295,298`; Membrum's
  `noise_body.h`, `click_layer.h`, `clap_exciter.h`, `feedback_exciter.h`, `noise_burst_exciter.h`,
  `noise_layer.h`) pins a fixed colour other than Velvet/RadioStatic, except `noise_layer.h:81`'s
  runtime-selectable `denormColor(float)`, whose output range is hard-capped to
  Brown/Pink/White/Violet (`noise_layer.h:306-309`) and therefore can never reach either broken colour.
  The fix changes no shipped output. FR-032 is updated accordingly (seven of eight colours now
  selectable as the `GranularDust` carrier, `Velvet` excluded on musical-design grounds instead).
- **FR-099** — `ResonatorBank::setFrequency` (`resonator_bank.h:328-332`) is corrected to re-derive Q
  from the resonator's currently configured decay time when frequency changes, the same way
  `setDecay` already does (`:345-352`) — so a drifting frequency no longer silently changes the
  effective RT60. **Zero-regression-risk, verified this session:** `grep -rln "\bResonatorBank\b"`
  (word-bounded, so it excludes the unrelated `ModalResonatorBank`) across `dsp/` and `plugins/` finds
  no consumer of the exact `ResonatorBank` class outside `dsp/tests/unit/processors/resonator_bank_test.cpp`
  and the compile-only `dsp/lint_all_headers.cpp`. Membrum's body files and `continuous_body.h`
  (Seraphis's dependency) all use `ModalResonatorBank`, a different class declared in the same header,
  and are structurally unaffected. **No change to FR-052's own call sequence is possible or needed**:
  `ResonatorBank` exposes no `getQ`/Q-reading accessor, so the organism still computes
  `rt60ToQ(driftedHz, decaySeconds)` itself to derive the FR-064 wander-adjusted target it writes via
  `setQ` — `setFrequency` now *also* computing a nominal Q internally is immediately superseded by that
  `setQ` call and is invisible to the organism's own behaviour. The substantive effect is that every
  citation in this spec describing `setFrequency`'s prior silent RT60 drift now describes **fixed**,
  not current, behaviour (see the "Existing components" table and "Pre-existing notes").

## Success Criteria

Measurement basis for every timing figure: **ns per 512-sample block at 48 kHz**, the basis
established at `harmonic_cloud_perf_test.cpp:69-101` and reused by `continuous_body_perf_test.cpp`
and `atmosphere_engine_perf_test.cpp:22-44`. One 512-block period is 10 666 667 ns, so 1 % is
**106 666 ns**.

- **SC-001 — Long-render stationarity (roadmap line 196).**
  Metric: RMS of consecutive 10 s windows over a **10 minute** mono render at 48 kHz, in SC-004
  reference configuration (c), **every setting exactly as the FR-016 defaults table states it** —
  nothing inherited implicitly from a library default.
  Thresholds: (a) every window is within **±4.5 dB** of the median window — bound set from
  measurement, not assumption (corrected 2026-09-01): across 24 seeds at this exact configuration
  the worst window deviation ran min 1.703 / median 2.371 / p90 2.717 / **max 3.247** dB, so the
  originally specified ±3.0 dB sat *inside* the criterion's own natural spread and passed or failed
  on seed luck. ±4.5 dB is the observed max plus 1.0 dB of margin. Clause (c) below, not this one,
  is what fails when the drone dies or runs away; (b) the least-squares slope
  of window RMS (dB) against time is within **±0.5 dB per 10 minutes** — no creep in either
  direction; (c) no window is below −60 dBFS (the drone did not die) and none above −3 dBFS — this is
  the criterion FR-017's per-type drive table and FR-018's per-resonator Q make-up gain exist to keep
  passable without per-preset trimming (decided 2026-08-31, Q1);
  (d) the FR-070 breathing factor, sampled every control step from `getSourceGain(slot)` with level
  and wake held fixed, is **strictly positive, never zero, never sign-changing**, and stays inside
  `[1 − 0.45·depth, 1 + 0.45·depth]` — the clause that makes the bipolar `BreathingModulator` output
  (`breathing_modulator.h:103`, `:227-229`) safe to use as a gain.
  Measured by: `NoiseOrganism_LongRenderStationarity`, tagged `[long]`, using
  `extractAudioFeatures` (`audio_features.h:37`) per window and `statistical_utils.h:41-76`.
- **SC-002 — Spectral-motion metric (roadmap line 196).**
  Metric: per-band energy trajectory. Every 100 ms extract the five band-energy fractions
  (`AudioFeatures::band`, `audio_features.h:28-29`); for each band compute the normalised
  autocorrelation of the (mean-removed) trajectory. `L`, the lag of its first zero crossing, is still
  **reported** for continuity, but no threshold rests on it any more — see (a) for the measurement that
  retired it as an estimator.
  `T = 1/r` where `r` is the FR-069 organism-level wander-rate scalar (decided 2026-08-31, Q7 — before
  FR-069, `r` was only the comb lane's own `PerlinNoiseSource` rate; FR-069 unifies every lane onto
  this one number, and its default `kDefaultWanderRateHz = 0.03` is chosen to leave `T` at the same
  33.3 s this criterion was built around, so no threshold below moves). Reported `L` is capped at
  0.25 × the record length, so an unmeasurable lag is a
  **failure**, not a coin flip. The wander-on arm renders `≥ 10·T` (350 s at the default rate) so the
  acceptance window in (a) is statistically resolvable.
  Thresholds:
  (a) **wander on, FR-016 defaults** — the normalised autocorrelation of the strongest-moving band's
  trajectory, at a lag of `T/8` in **seconds**, is **≥ 0.20**: motion exists *at the configured rate*.
  **Rewritten 2026-09-01 after measurement.** It required "at least three of the five bands have
  `L ∈ [0.4·T, 3.0·T]`", which rests on the first-zero-crossing lag — the estimator SC-008 (c) had to
  abandon after it measured 67–163 % spread across seeds with the rate held constant. Over 24 seeds the
  count came out 0:1, 3:17, 4:1, 5:5 — one seed scored **zero**, and not marginally: its five band lags
  were 11.2, 11.2, 114.7, 101.6 and 100.6 s, straddling *both* window edges at once, so the clause fell
  off a cliff rather than degrading. Widening the window does not repair it either: `[8, 120]` passes all
  24 seeds but then contains 93 % of all observed band lags (against 66.7 % for the original), so
  "3 of 5 inside" becomes nearly free. The fixed-lag autocorrelation needs no zero crossing, is bounded
  in `[-1, 1]`, and uses every sample pair; measured min 0.298 / median 0.470 / max 0.645 across 24 seeds,
  so the 0.20 bound clears the minimum by 1.5×. **Verified by injection:** running the lanes at 10× the
  default rate drives it through the bound. The symmetric "too slow" bound is deliberately **not**
  asserted — `setWanderRate` clamps at 0.01 Hz, so the slowest expressible organism is 3× slower than
  default, and at that rate `r(2T)` reads −0.052 against the default's −0.010, i.e. no reachable
  configuration can violate it. It is reported instead;
  (b) **control arm, `setWanderEnabled(false)` (FR-068)** — every band has `L < 0.4·T`, i.e. no band's
  `L` falls inside (a)'s acceptance window, **and** the strongest band-fraction CV of this arm is at
  least **1.8×** below the one measured in the wander-on arm. **The multiplier was 3.0× and is measured,
  not assumed (corrected 2026-09-01):** across 35 seeds in two sweeps the ratio ran min 2.290, median
  ~3.0, max 5.44, so 3.0 sat squarely *inside* the criterion's own spread and failed 7 of 12 seeds —
  `kTestSeed` happened to sit at 3.35 and made it look green. 3.0 is not reachable because the
  denominator is the wander-off control arm, whose band CV is ~0.75 and is substantially the *estimator
  floor* rather than organism motion. 1.8 keeps 27 % margin under the observed minimum and still asserts
  a real effect: wander must raise band motion 80 % over the control arm, and an organism with wander
  disabled scores exactly 1.0 by construction. This is the correct direction: with no wander the
  band-fraction estimates are stationary plus estimator noise, so the mean-removed ACF crosses zero
  after roughly one 100 ms frame — `L ≈ 0.1 s`, far *below* `T`, not above it. (An earlier draft
  required `L > 6·T = 200 s`, which no correct implementation can produce and which a 300 s record
  could not resolve anyway.) The arm must use `setWanderEnabled(false)`, not merely zeroed depths:
  FR-023 keeps the `StochasticFilter`'s internal cutoff wander on by default
  (`cutoffRandomEnabled_ = true`, `stochastic_filter.h:555`), so zeroed depths alone still move
  spectrally, and faster than the lanes being isolated.
  (c) the strongest band's energy-fraction CV is **≥ 0.10** (the motion is real) **and ≥ 5× the
  broadband RMS CV** (it is **spectral**, not level), **and** the broadband RMS CV is **≤ 0.28**.
  **Both numbers are measured over 35 seeds and the pair is deliberate (corrected 2026-09-01).** The
  clause read "broadband CV ≤ 0.06", which measured **0.141** — and that is not the measurement-floor
  case the helper's caveat anticipated (the estimator floor is ~0.04, so 0.141 sits 3.5× above it): the
  organism's broadband level really does move that much. 0.141 linear is ±1.15 dB over 10 s windows,
  agreeing with what SC-001 independently measures on the same configuration, and it follows from the
  *specified* feature set rather than a defect — FR-070 breathing is ±0.92 dB per slot by construction,
  and the wander lanes move resonator frequency and cutoff, which moves level too. The old 0.06 (~0.5 dB)
  accounted for breathing alone. **Why both a ratio and a cap:** injection settled it. A pure broadband
  AM (multiplying the render by 1 + 0.8·sin(2π·0.05·t)) left every band CV *exactly* unchanged at 2.596,
  as a broadband gain must, and drove the level CV 0.141 → 0.395 — but the ratio only fell 18.42 → 6.57,
  so a ratio alone is an insensitive pump detector. The cap moves decisively on the same defect. Measured
  ranges: ratio min 11.32 / median 18.02 / max 30.53 → bound 5.0 (2.5× clear); level CV min 0.1107 /
  median 0.1334 / max 0.1861 → cap 0.28 (1.5× clear), with the injected pump at 0.395 caught by 1.4×.
  Measured by: `NoiseOrganism_SpectralMotion`, tagged `[long]`.
- **SC-003 — Zero allocation after prepare (roadmap line 196).**
  Metric: allocation count inside an `AllocationScope` (`allocation_detector.h:111`).
  Threshold: **0** allocations across 20 000 blocks of 512 samples that also exercise every setter
  (model changes, noise-type changes, all six wander depths including `setFilterResonanceWander`,
  `setWanderEnabled` and `setWanderRate` (FR-069), dust density/length, wake/dormant toggles,
  `setNumSources`, `setNumResonators`, `setNumCombs`, `setCombTuning`/`setCombFeedback` (FR-057),
  `setSeed`) once per block, and across `reset()`.
  Measured by: `NoiseOrganism_NoAllocationAfterPrepare`.
- **SC-004 — CPU ≤ 1 % per voice (roadmap line 197).**
  Metric: ns per 512-sample block at 48 kHz, best-of-25 trials × 500 blocks after 400 warm-up blocks,
  gated against checked-in baselines at `× 1.5`, with the two compile-time clauses from
  `atmosphere_engine_perf_test.cpp:32-42` binding the absolute reference
  (`kReferenceNsPerBlock = 106666`).
  Configurations, each with its own baseline:
  (a) default — 2 slots, `Direct`, 2 resonators + 2 combs each;
  (b) 4 slots, `Direct`, 3 resonators + 2 combs each;
  (c) **reference** — 4 slots, one each of `Direct`/`FilteredWind`/`GranularDust`/`MetallicHiss`,
      3 resonators + 2 combs each, everything else at the FR-016 defaults, dust at **100 imp/s ×
      40 ms** (mean concurrency 4 of `kMaxDustGrains = 24`, ~17 % — so FR-034's steal-oldest is a
      genuine backstop here and not the normal path);
  (d) **out-of-region** — every cap maxed (4 slots × 4 resonators × 4 combs, all `GranularDust` at the
      FR-035 concurrency ceiling): regression-tracked against its own baseline, **not** gated against
      the 1 % reference, exactly as `atmosphere_engine_perf_test.cpp:44-50` does for its saturated
      pool. This is not an exemption from FR-095: (d) sits deliberately **outside** the in-region
      envelope FR-095 names (≤ 4 slots × 3 resonators × 2 combs, ≤ 1 dust slot, dust concurrency ≤
      50 % of the FR-035 ceiling), which is the envelope Phase 10 is bound by and which (a)–(c) sit
      inside. If a future Phase-10 voice needs to leave that envelope, FR-095's stop-and-surface policy
      applies (decided 2026-08-31, OQ-CPU-POLICY) — not a unilateral cap change;
  (e) **all-dormant** (decided 2026-08-31, Q6) — 4 slots configured as (c) but every slot
      `setSourceDormant(true)`: measures the residual cost of FR-071's "source runs, chain skipped"
      dormancy (the source's `NoiseGenerator`/`NoiseOscillator` still renders; only the
      resonator/comb/`StochasticFilter` stages are skipped). Regression-tracked against its own
      baseline like (d), not gated against the 1 % reference — dormancy's *saving* relative to (c) is
      the number FR-071 requires to be measured, not claimed.
  (a), (b) and (c) are gated against 106 666 ns; if (c) misses, FR-095's stop-and-surface policy
  applies. Measured by: `NoiseOrganism_CpuBudget`, tagged `[.perf]`.
- **SC-005 — Boundedness (roadmap lines 471–474).** Two arms, split by lane, because finiteness is a
  cross-platform sentinel and must not sit behind a `[long]` tag: per-push CI **excludes** `[long]`
  cases (CLAUDE.md's `[long]` convention says so, and says never to tag NaN/Inf-guard tests), so a
  `[long]`-only finiteness assertion would ship with no per-push guard at all.
  Fixture for both arms: configuration (d), every wander depth at maximum and fastest rate, comb
  feedback at the FR-090 cap, **decay at `kMaxDecayTime = 30 s`** — which saturates `rt60ToQ` at
  `kMaxResonatorQ` for every anchor (FR-064's derivation), so "maximum base Q" and "maximum decay"
  are one coherent state and not two settings of one variable — with **Q-wander depth 1.0**, and
  wake/dormant toggled by a seeded pseudo-schedule.
  (a) **Per-push arm, untagged.** 60 s render. Every sample finite (IEEE-754 exponent-field test,
  never `std::isnan`); peak < 4.0; `getClampEngagementCount()` (FR-074) is **0**; no 1 s window below
  −60 dBFS. Measured by: `NoiseOrganism_BoundedShort`.
  (b) **Soak arm, `[long]`.** 30 minute render, same fixture. All of (a)'s thresholds, plus: RMS of
  the final minute within ±6 dB of the RMS of the first minute after the initial 30 s settle.
  Measured by: `NoiseOrganism_BoundedSoak`, tagged `[long]`.
- **SC-006 — Seed determinism (roadmap line 478).**
  Metric: sample-exact comparison within one process. Two instances, same seed, same configuration,
  same sample rate ⇒ identical 10 s renders (max |difference| = 0). A third instance seeded
  differently ⇒ |Pearson r| against the first ≤ **0.05**.
  **Reset semantics (restated 2026-08-31, Q2 — `reset()` is configuration-preserving, FR-004):**
  (a) with no setter called since `prepare(sr, cfg)`, `reset()` reproduces the exact post-`prepare`
  stream; (b) after configuration changes (e.g. non-default resonator count, anchors, or comb tuning),
  `reset()` re-applies that configuration — the render immediately after `reset()` is **non-silent**
  and matches a fresh `prepare()` followed by the same configuration calls, sample-exact, **not** the
  FR-016 defaults. (b) is the assertion that catches an implementation that forwards `reset()` to
  `ResonatorBank`/`TimeVaryingCombBank`/`StochasticFilter` without re-applying configuration, which
  FR-004 shows renders silence (`resonator_bank.h:225-231`'s `enabled_[i] = false`).
  Measured by: `NoiseOrganism_SeedDeterminism`.
- **SC-007 — Per-slot decorrelation (the FR-080 rationale, made measurable).**
  Metric: pairwise Pearson correlation of the four slots' isolated outputs, configured **identically**
  — same model, same noise type, same chain — differing only in the FR-005 salt. Isolation is by
  `setSourceDormant(other, true)`, which contributes **exactly** zero (FR-071); `setSourceLevel(-96)`
  is **not** used, because it leaves a residual that floors the measurable correlation.
  Threshold: all six pairwise |r| ≤ **0.05** over 10 s at 48 kHz.
  **Control arm, constructed in-process.** "A build where `setSeed` is not applied" is not something a
  test binary can produce — FR-005 makes the call unconditional and no FR exposes a way to skip it —
  so the anti-vacuity arm is built from the real pre-amendment condition instead: instantiate two bare
  `NoiseGenerator` objects, prepare them identically, call **no** `setSeed`, render both, and REQUIRE
  |r| > 0.99 (they share `Xorshift32 rng_{12345}`, `noise_generator.h:593`, and advance identically —
  this is exactly FR-082's +12 dB coherent-sum hazard). Then call
  `setSeed(deriveStreamSeed(seed, salt))` on each with distinct salts and REQUIRE |r| ≤ 0.05 for the
  same pair. That pins the amendment's effect with public API only, and the criterion cannot pass
  vacuously.
  Measured by: `NoiseOrganism_SourceDecorrelation`.
- **SC-008 — Sample-rate change (roadmap cross-cutting).**
  Metric: 60 s renders at 44 100, 48 000, 96 000 and 192 000 Hz, same seed, FR-016 configuration.
  Thresholds — restricted to what FR-093 establishes is genuinely invariant:
  (a) overall RMS within **±1.0 dB** across rates;
  (b) every sample finite (exponent-field test) and no 1 s window below −60 dBFS at any rate;
  (c) the organism's **own** time constants are invariant *in seconds*: the comb lane's normalised
  autocorrelation **at a fixed 0.5 s lag**, sampled from `getCombCurrentDelayMs` on a 0.1 s wall-clock
  grid, agrees across the four rates to within **0.005 absolute**, and the measured
  10–90 % duration of a `setSourceWake(0 → 1)` ramp read from `getSourceGain` is **50 ms ± 5 ms** at
  every rate;
  **(c) was rewritten on 2026-09-01 after measurement showed the original could not work.** It used the
  SC-002 first-zero-crossing lag of the strongest-moving band, within ±15 %. Two faults compounded: the
  lanes it observed include `BrownianDrift`, which draws RNG **once per control step**, so at 96 kHz the
  same seed walks a *different sample path* (its statistics are rate-invariant; its realisation is not);
  and first-zero-crossing is a very high variance estimator on a record holding only ~20 correlation
  times. Holding the **rate constant** and varying only the seed, the lag moved by **67.5 % / 163.2 %**
  (resonator frequency at 48 / 96 kHz) and **80.7 %** (cutoff) — 4× to 11× the budget it was measured
  against. Fitting τ by regression was worse (136.8–421.6 %), though the *median* fitted τ agreed across
  rates to **0.8 %**, confirming the population parameter is rate-invariant and the estimator was the
  fault. Band energy could not carry it either: even at a 240 s record the paired `|r₉₆ₖ − r₄₈ₖ|`
  reached 0.105, above the ~0.074 a **doubled** τ would produce. The comb lane is a `PerlinNoiseSource`,
  a deterministic function of lattice position advancing at exactly `rate` cells per **second** at any
  sample rate, so it carries no realisation noise: measured paired deviation **≤ 0.0001** across 8 seeds
  and all four rates on the same 60 s render, against a 0.005 bound. It is **sharper**, not weaker —
  deriving the increment against a hardcoded `44100.0` instead of `sampleRate_` (the canonical form of
  this defect, which hits every lane) moves the statistic to **0.0239 / 0.3187 / 0.9349** at 44.1 / 96 /
  192 kHz, i.e. 5× to 190× over the bound where the clean tree sits 50× under it;
  (d) a mid-render `prepare()` at a new rate produces silence-free, finite output.
  **Spectral shape is deliberately not asserted, and that is a correction, not a relaxation.** Centroid
  and band-energy fractions cannot be rate-invariant for this signal, so asserting them would be a
  criterion no correct implementation can pass: brown's leaky integrator has a hard-coded coefficient
  (`kBrownLeak = 0.98f`, `noise_generator.h:467-468`) whose corner moves ~155 Hz → ~620 Hz from 48 to
  192 kHz; blue and violet are one-sample differentiators (`:483`, `:498`) whose spectra extend to
  Nyquist; pink's Kellet coefficients are tuned at 44.1 kHz (`pink_noise_filter.h:65-70`); and the
  fifth `AudioFeatures` band is literally `[8k, Nyquist]` (`audio_features.h:28-29`), so its own edges
  move with the rate. FR-021 and FR-041 pin three of those colours into the reference configuration.
  FR-093 records the same fact rather than claiming an invariance the reused DSP does not provide.
  Measured by: `NoiseOrganism_SampleRateInvariance`, tagged `[long]`.
- **SC-009 — No zipper, no click.** The teeth are in the **gain domain**, not the audio: a 1 ms frame
  at 48 kHz is 48 samples, whose RMS estimate on broadband noise carries ~10 % relative spread
  (~0.9 dB σ), and a max taken over ~300 000 frame pairs of a 5 minute render is dominated by that
  estimator variance — several dB — before any zipper exists, so a real 1–2 dB zipper would be
  invisible under a 1.5× ratio bound. With `GranularDust` in the configuration the frame-to-frame
  envelope variance is larger still.
  (a) **Gain domain — deterministic, noise-free.** Sample `getSourceGain(slot)` (FR-015) every control
  step. Across a full-range `setSourceLevel` step, a `setSourceDormant` toggle, a
  `setSourceWake(0 → 1)` transition and a `setSourceNoiseType`/`setSourceModel` change — each
  exercised 100 times at random block offsets — the trajectory is **monotone through each ramp** (no
  overshoot, no reversal) and its **0–100 % duration is 50 ms ± 5 ms** (FR-073) — restated in
  0–100 % terms 2026-08-31 (Q4): FR-073's per-sample linear-in-gain ramp has a **10–90 % duration of
  40 ms ± 4 ms**, not 50 ms, so 10–90 % wording would fail this criterion on a *correct*
  implementation; 0–100 % is the domain that is actually exact for a linear ramp that reaches zero
  (required by FR-013's duck-and-restore swap point). `getSourceGain` is the applied smoothed gain;
  `getSourceLevel` (the configured target) and `getSourceRms` (a smoothed output level) are **not**
  substitutes and the test says so.
  (b) **Envelope domain — coarse blow-up check only.** 25 ms-frame RMS envelope in dB over a 5 minute
  render with every wander lane at maximum rate and depth; `maxΔ = max |env[k] − env[k−1]|`. The test
  first **measures and records** the estimator noise floor at this frame length on a fixed-gain render
  of the same configuration and asserts the acceptance threshold sits at least 3 σ above it; only then
  does it require `maxΔ` ≤ **1.5 ×** the same statistic on a wander-disabled render. The
  wander-disabled render is produced with `setWanderEnabled(false)` (FR-068) — zeroed depths alone are
  not a static configuration, because FR-023 keeps the `StochasticFilter`'s internal cutoff wander on
  (`cutoffRandomEnabled_ = true`, `stochastic_filter.h:555`).
  `artifact_detection.h`'s `ClickDetector` is **not** used and the test says why: it thresholds the
  signal's first derivative at 5 σ (`artifact_detection.h:38-99`), which flags every sample of a
  broadband noise render.
  Measured by: `NoiseOrganism_NoZipperUnderDrift`, tagged `[long]`.
- **SC-010 — Wake/dormancy continuity and lane freewheeling (FR-071).** Split into two arms, because
  the obvious single arm asserts a state the product cannot be in. A slot rendered dormant for 60 s
  and one rendered awake for 60 s **cannot** produce matching post-wake audio: the awake arm's
  `ResonatorBank` biquads carry up to `kMaxDecayTime = 30 s` of ringing (`resonator_bank.h:57`), its
  `TimeVaryingCombBank` delay lines hold `maxCombDelayMs` of past audio at feedback up to 0.75
  (FR-042), and its `StochasticFilter` SVF is in steady state, while the dormant arm's chain state is
  whatever it was when dormancy began — the **chain** stages (FR-071, revised 2026-08-31 Q6) are
  exactly what dormancy skips. So a `kSampleTolerance` (5.0e-4f, `render_fingerprint.h:58`)
  sample-identity clause would fail on a *correct* implementation, by orders of magnitude, for seconds.
  **The two arms are about different things, not two views of the same property (Q6):** (a) is about
  the **source** — the `NoiseGenerator`/`NoiseOscillator` stream and its wander lanes, which FR-071
  keeps running through dormancy — while (b) is about the **chain** — the resonator/comb/filter state,
  which FR-071 freezes at whatever it was when dormancy began and which only re-converges after a real
  settle time. Both are true simultaneously because they are about disjoint sub-components.
  (a) **Source and lane freewheeling — the property FR-071 actually claims.** Render slot 0 dormant for
  60 s then wake it; independently render the same slot awake for 60 s with its gate applied **after**
  the chain. Compare read-surface trajectories, not samples: `getResonatorCurrentFrequency`,
  `getResonatorCurrentQ` and `getFilterCurrentCutoff` (FR-015), sampled every control step, agree to
  within 1e-5 relative across the whole 60 s; and the source RNG streams are shown to have advanced
  identically by requiring the `getSourceRms(slot)` trajectory over the first 250 ms after wake to
  agree within **0.5 dB** — satisfiable specifically because FR-071's "source runs, chain skipped"
  design keeps the source and its colour filters live throughout dormancy, not merely the RNG counter.
  (b) **Post-wake chain settle, on statistics not samples.** After a stated settle window
  `tSettle = max(resonatorDecay, 8 × maxCombDelayMs / (1 − combFeedback))` — **1.5 s** at the FR-016
  defaults (decay 1.5 s, 16.7 ms base delay, feedback 0.55) — the dormant-then-woken render agrees
  with the always-awake render on RMS within **±1.0 dB** and on each of the five band-energy fractions
  within **±0.05** absolute, measured over the following 10 s. There is no sample-identity clause;
  continuity across the wake transition itself is bounded by SC-009 (a).
  Measured by: `NoiseOrganism_DormantLanesFreewheel`.
- **SC-011 — Shared-component regression (roadmap line 481).**
  Metric: existing suites, unchanged, after the FR-080, FR-098 and FR-099 amendments — **broadened
  2026-08-31 (OQ-SHIPPED-DEFECTS)** from FR-080 alone to cover all three shared-component amendments
  this phase makes.
  Threshold: `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `dsp_primitives_tests`
  and `membrum_tests` all report "All tests passed"; specifically
  `dsp/tests/unit/processors/noise_generator_test.cpp`, `resonator_bank_test.cpp` and
  `noise_oscillator_test.cpp` are **green with no edits**. Session verification found these last two
  suites' gating to be a low-risk formality rather than a live regression hazard: `ResonatorBank` (the
  exact class, word-bounded — excluding `ModalResonatorBank`) has zero consumers outside
  `resonator_bank_test.cpp` itself and the compile-only `dsp/lint_all_headers.cpp`, and no existing
  `NoiseOscillator` consumer selects `NoiseColor::Velvet`/`RadioStatic` (see FR-098/FR-099's citations)
  — the suites are still run in full, because "verified low-risk" is not the same claim as "verified
  zero-risk", and because the decision explicitly required the gate regardless.
  (v) `NoiseOscillator` with `NoiseColor::Velvet` (and separately `RadioStatic`) selected no longer
  matches a `NoiseColor::White` instance sample-for-sample from the same seed — the regression the
  FR-098 fix must produce, since before the fix both colours silently rendered identical output to
  White (`:265-267`);
  (vi) `ResonatorBank::setFrequency`, called on a resonator with a configured decay via `setDecay`,
  produces an impulse response whose measured RT60 (envelope-decay fit, the `testing-dsp-analysis`
  method SC-020 already uses for spectral measurement) tracks the configured decay after a frequency
  change within a measured tolerance — the property FR-099 adds and the property the "Existing
  components" table's prior citation (`:328-332` vs `:345-352`) shows was previously absent.
  The pre-amendment behaviour is pinned by **behavioural invariants, not by a stored render.** The
  pre-amendment binary no longer exists, so "a render identical to the pre-amendment behaviour" could
  only mean a checked-in float golden of `NoiseGenerator` output — precisely the bit-exact float golden
  the project forbids (roadmap line 477; `tools/lint-float-bit-goldens.js` exists to catch it), and it
  would go red on the Linux and macOS legs. The new case asserts instead, entirely through the public
  API:
  (i) two default-constructed instances that never call `setSeed` produce **identical** streams — the
  `Xorshift32 rng_{12345}` property at `noise_generator.h:593` that FR-082 is built on;
  (ii) on an instance that never calls `setSeed`, two successive `reset()` calls produce **different**
  streams — the historical scramble `rng_.seed(rng_.next() ^ 0xDEADBEEF)` (`:189`) is intact;
  (iii) on an instance that **does** call `setSeed`, `reset()` reproduces the post-`setSeed` stream
  exactly (FR-081);
  (iv) two instances given different `setSeed` values are decorrelated (|Pearson r| ≤ 0.05).
  None of (i)–(vi) requires a stored float golden, and (i)–(ii) fail immediately if the FR-080
  amendment disturbs the un-seeded path; (v)–(vi) fail immediately if the FR-098/FR-099 fixes are not
  actually applied.
  Measured by: full-suite run recorded in `compliance.md` plus
  `NoiseGenerator_SetSeedIsOptInAndReproducible`, `NoiseOscillator_VelvetRadioStaticFixed` and
  `ResonatorBank_SetFrequencyRederivesQ`.
- **SC-012 — Lints and layering (roadmap lines 476, 479).**
  Metric/threshold: `node tools/lint-odr.js`, `node tools/lint-layers.js`,
  `node tools/check-portability.js` and `node tools/lint-simd-aligned-loadstore.js` each exit 0;
  `noise_organism.h`'s only same-layer include is `systems/timevar_comb_bank.h` and it carries the
  justification comment.
  Measured by: CI gates, recorded in `compliance.md`.
- **SC-013 — Render pin without bit-exact goldens (roadmap line 477).**
  Metric: `render_fingerprint.h` over a 30 s render of the FR-016 reference configuration (c) at
  48 kHz, seed pinned.
  Threshold: aggregate metrics within the shared `kMetricTolerance` (`render_fingerprint.h:61`);
  checkpoint samples within a **measured per-comparison sample tolerance** passed explicitly as
  `compareFingerprints`' `sampleTolerance` argument (`:124`), derived from a three-toolchain probe
  (MSVC, `g++ -O3 -ffast-math`, `clang++ -O2`) of this exact render and recorded in `compliance.md`.
  The shared `kSampleTolerance = 5.0e-4f` (`:58`) is **not** loosened for this caller — the header's
  banner mandates exactly this treatment for a "STORED golden of a trajectory-accumulating render
  (drift, mutation, chaotic modulators)" (`:116-121`) and records why: its own measured
  trajectory-bearing spread is already `sample 3.73498e-4` / `metric 9.36659e-5` (`:31-36`), within a
  factor of 1.3 of the shared sample bound *before* this component's OU walks, Perlin lattice, velvet
  Poisson triggers and 0.75-feedback combs are added.
  The test must be shown to **fail** at the loosened bound on an injected defect (comb wander lane salt
  collided with the resonator lane salt) before it is accepted — a fingerprint that cannot fail is not
  a pin, and a loosened bound that cannot fail is worse than none.
  Measured by: `NoiseOrganism_RenderFingerprint`.
- **SC-014 — Memory footprint (FR-096).**
  Metric: `getAllocatedBytes()` (FR-015/FR-096) after `prepare` at 48 kHz with the default
  `PrepareConfig`, cross-checked against the FR-096 formula computed in the test from
  `maxCombDelayMs`, the sample rate and `kMaxCombs`.
  Thresholds: `getAllocatedBytes() ≤` **640 KiB** per instance; the header's documented figure matches
  it within 5 %; and separately, the **allocation count** inside an `AllocationScope` over `prepare` is
  bounded (≤ 64) while every post-`prepare` operation counts 0 (SC-003).
  `AllocationDetector` is used for the **count only**, because it has no byte accounting:
  `recordAllocation()` does `allocationCount_.fetch_add(1, …)` (`allocation_detector.h:83-89`),
  `stopTracking`/`getAllocationCount` return that count, and the replaced operator-new forms discard
  the `size` argument (`allocation_operator_overrides.h:66-95`). A byte threshold "counted by
  `AllocationDetector`" would not be measurable at all; extending that shared helper is not this
  phase's work, so the component reports its own sizing instead.
  Measured by: `NoiseOrganism_PrepareFootprint`.
- **SC-015 — Non-finite input handling (FR-008).**
  Metric: every public setter is called with NaN, +Inf and −Inf, each **built from bit patterns through
  a volatile sink** — never `std::numeric_limits<float>::quiet_NaN()`/`infinity()`, which fold to
  finite garbage on the macOS `-ffast-math` leg — and a 1 s block is then rendered.
  Thresholds: (a) each non-finite argument is replaced by the documented neutral value, read back
  through the FR-015 surface (e.g. `setSourceWake(slot, NaN)` ⇒ `getSourceWakeAmount(slot) == 0.0f`);
  (b) every rendered sample is finite (IEEE-754 exponent-field test, never `std::isnan`); (c) the
  render following the injection matches an uninjected reference on RMS within ±0.5 dB — a rejected
  value must not perturb state.
  Measured by: `NoiseOrganism_NonFiniteSetterInputs`, in
  `dsp/tests/unit/systems/noise_organism_nonfinite_test.cpp`, which per FR-097 is the **only** new TU
  added to the `-fno-fast-math -fno-finite-math-only` block of `dsp/tests/CMakeLists.txt`, following
  `atmosphere_engine_nonfinite_test.cpp` (`:758`), `aether_reverb_nonfinite_test.cpp` (`:768`) and
  `seraphis_nonfinite_test.cpp` (`:776`). Untagged — it runs in the per-push lane.
- **SC-016 — Block-size invariance (FR-007).**
  Metric: render 240 000 samples (5 s at 48 kHz) three ways from the same seed and configuration, each
  on a freshly `prepare`d and `setSeed`-ed instance: (i) one single call; (ii) 469 calls of 512;
  (iii) an irregular repeating sequence 36, 28, 1000, 1, 511, 2048.
  Threshold: the three buffers are **identical**, max |difference| = 0. This is a same-binary equality
  within one process, not a stored cross-toolchain golden, so the no-bit-exact-goldens rule does not
  apply. It is the sharp, cheap test of FR-007's "a control chunk split 36 + 28 produces the same
  control step as an unsplit 64".
  Measured by: `NoiseOrganism_BlockSizeInvariance`.
- **SC-017 — Guard ladder (FR-003).**
  Metric/thresholds, each measured against an uninterrupted reference render of the same instance, so
  that "no state advanced" is asserted and not merely the output:
  (a) `processBlock(nullptr, 512)` writes nothing **and advances nothing** — the render that follows
  is identical (max |difference| = 0) to the reference at the same absolute sample position;
  (b) `processBlock(out, 0)` leaves `out` untouched and consumes no control step, proved the same way;
  (c) `processBlock` before `prepare()` fills exactly `numSamples` zeros and advances nothing;
  (d) `numSamples` far above `maxBlockSamples` (100 000) gives the same output as 196 blocks of 512 —
  FR-007's absolute-grid claim at the other extreme.
  Measured by: `NoiseOrganism_GuardLadder`.
- **SC-018 — Transition continuity across model and type changes (FR-011, FR-012, FR-013).**
  Metric: `getSourceGain(slot)` on the control grid plus the SC-009 (b) 25 ms envelope, across 100
  `setSourceModel` changes and 100 `setSourceNoiseType` changes at random block offsets.
  Thresholds: FR-013's duck-and-restore is present, monotone in each direction, **total duration
  50 ms ± 5 ms** (consistent with SC-009 (a)'s 0–100 % restatement and FR-073's per-sample
  linear-in-gain law — a "total duration" figure has no 10–90 %/0–100 % ambiguity, since it already
  spans the full sweep); the envelope `maxΔ` across each transition is inside the SC-009 (b) bound. The
  test also asserts the **naive path fails**: with the duck removed, `maxΔ` across a
  `setSourceNoiseType` change exceeds the bound — because `NoiseGenerator` gates each type on
  `if (noiseEnabled_[idx])` (`noise_generator.h:388` and the parallel blocks through `:568`) and
  removes a full-amplitude broadband contribution on the very next sample, while the level smoother
  `updateLevelTarget` sets to zero (`:255-261`) never gets to ramp. Without that arm the criterion
  could pass with no duck at all.
  **Coalescing arm, required 2026-08-31 (Q5).** Write the same effective `setSourceNoiseType` value
  1000 times in succession and assert `getSourceGain(slot)` never leaves `1.0` — a parameter-echoing
  host (Phase 12) must not arm the duck on a no-op write. Separately, write a genuine change, then
  write a second genuine change **before the first duck completes**, and assert the trajectory shows
  exactly **one** 50 ms duck (the pending target updates without restarting the ramp), not two
  back-to-back ducks (100 ms of near-silence).
  Measured by: `NoiseOrganism_ModelChangeContinuity`.
- **SC-019 — Every selectable model sounds; dust level is stable across density (FR-012, FR-036).**
  (a) For each of the **twelve** selectable `NoiseType` values (FR-012) and each of the four
  `NoiseOrganismModel` values, a 5 s isolated slot render has RMS above **−60 dBFS** — no selectable
  configuration is silent, so no Phase-10/Phase-12 preset can render a dead slot. Separately,
  `setSourceNoiseType(slot, NoiseType::ModulationNoise)` is asserted to snap to `TapeHiss` through
  `getSourceNoiseType`, and a bare `NoiseGenerator` with only `ModulationNoise` enabled and a zero
  sidechain is asserted to render **exactly** `0.0f` — the verified fact FR-012's exclusion rests on
  (`noise_generator.h:553-558`).
  (b) `GranularDust` slot RMS across a density sweep of 100 / 400 / 1600 / 6400 / 20 000 imp/s, **each
  held at a constant, explicitly requested grain length of 40 ms** (the FR-016 default —
  required to be stated explicitly, decided 2026-08-31 Q3: requesting the FR-035 *maximum* of 200 ms
  would let the FR-035 ceiling clamp pin concurrency at `kMaxDustGrains = 24` for every density in the
  sweep, at which point FR-036's concurrency-normalised gain trivially flattens the level and the
  criterion would pass without exercising the gain law at all — at 40 ms the ceiling only binds above
  ~600 imp/s, so the lower sweep points genuinely exercise FR-036's `1/sqrt(expectedConcurrency)` term),
  varies by at most **6 dB** peak-to-peak with no adjacent step above **3 dB**. That is FR-036's
  level-continuity claim (decided 2026-08-31, Q3, option b — concurrency-normalised grain gain with the
  velvet trigger's polarity as sign) measured rather than asserted; this is the arm that would catch a
  concurrency-dependent level step.
  Measured by: `NoiseOrganism_ModelRosterAndDustLevel`.
- **SC-020 — Inharmonic comb tuning (FR-042).**
  Metric: 20 s isolated `MetallicHiss` slot render with the comb-delay wander depth at 0; magnitude
  spectrum; locate the comb peaks and take their ratios to the lowest peak.
  Thresholds: (a) at least three peaks are found; (b) every ratio deviates from the nearest integer by
  at least **4 %** — the comb series is genuinely inharmonic, which is the property FR-040 makes the
  model's defining one and which nothing else in this spec measures; (c) the measured peak frequencies
  match the organism's own `f[n] = fundamental × sqrt(1 + n × spread)` (`timevar_comb_bank.h:237`,
  implementation at `:959`) within **3 %**.
  `getTuningMode()` is deliberately **not** asserted: FR-042 leaves the bank in `Tuning::Custom` by
  design, because `setCombDelay` sets `tuningMode_ = Tuning::Custom;` (`:515`) on the first control
  step, so an `Inharmonic` assertion would fail on a correct implementation.
  Measured by: `NoiseOrganism_MetallicHissInharmonicity`.
- **SC-021 — The Q lane is audible (FR-064).**
  Metric: all other wander depths at 0, one resonator enabled at a default FR-016 anchor; hold the
  FR-064 lane at each extreme (`qFactor = 1` and `qFactor = 1 − 0.9` at `amount = 1.0`) and measure the
  resonator peak's −3 dB bandwidth from the spectrum of a 10 s render at each.
  Threshold: the two bandwidths differ by at least a factor of **3**, and both renders are non-silent.
  This is what keeps FR-064 from being vacuous: `rt60ToQ` saturates at `kMaxResonatorQ = 100` for
  `frequency × RT60 > 219.9` (`resonator_bank.h:96-97`), so a symmetric or upward Q factor would be
  clipped away at every default anchor and the lane would measure as a no-op. The downward-only factor
  FR-064 specifies is the design that makes this criterion passable.
  Measured by: `NoiseOrganism_QWanderAudible`.

## Edge Cases

**RT-safety boundaries**

- `processBlock(nullptr, n)`: nothing written, **no state advanced** — a null render must not consume
  control steps, or block-size independence (FR-007) breaks in hosts that pass null probe buffers.
- `processBlock(out, 0)`: no-op, no control step consumed (`continuous_body.h:1380-1382` idiom).
- `processBlock` before `prepare()`: fills silence, advances nothing.
- `numSamples` far above `maxBlockSamples`: legal and correct — nothing is sized by block length; the
  control grid is absolute, so a 100 000-sample block gives the same output as 196 blocks of 512.
- A setter called from a different thread mid-block is out of contract (the whole component is
  single-threaded, like every other Layer 3 system here) and the header says so.
- Every setter is callable inside the render callback: all are `noexcept`, allocation-free, and only
  latch values that the next control step consumes.

**Parameter extremes**

- `setNumSources(0)` clamps to 1; `setNumSources(> kMaxSources)` clamps to 4. Reducing the count
  ramps the dropped slots down over 50 ms (FR-072), so it cannot click.
- `setNumResonators(slot, 0)` and `setNumCombs(slot, 0)` bypass those stages **by the organism not
  calling them** (FR-051, FR-054) — the slot is then source → filter → gain. Neither call is forwarded
  at 0, and that is load-bearing, not an optimisation: `ResonatorBank::process` with nothing enabled
  returns `input × 0 + 0 × 1 = 0` (`resonator_bank.h:511`, `exciterMix_ = 0.0f` at `:589`), and
  `TimeVaryingCombBank::setNumCombs` floors at 1 (`timevar_comb_bank.h:502`) so a forwarded 0 would
  leave one comb running. This is a legal, tested configuration: SC-019 (a) asserts a slot with 0
  resonators and 0 combs renders above −60 dBFS, which is the assertion that catches an implementation
  that forwards the calls.
- All wander depths at 0: the external lanes contribute nothing, the lanes still advance (FR-066) so
  re-enabling depth does not jump — but the output is **not** static, because the per-slot
  `StochasticFilter`'s own cutoff randomisation is still running (`cutoffRandomEnabled_ = true`,
  `stochastic_filter.h:555`), deliberately, per FR-023. The genuinely static configuration is
  `setWanderEnabled(false)` (FR-068), which zeroes the external lanes **and** disables the internal
  randomisation; that is the control arm SC-002 (b) and SC-009 (b) use, and the only way to reach it.
- All wander depths at maximum with the fastest rates: SC-005 and SC-009 are measured exactly here.
- `setResonatorAnchor` above Nyquist: clamped to `sampleRate × 0.45` (`resonator_bank.h:45`), which at
  44.1 kHz is 19 845 Hz. Anchors set at 96 kHz and then re-`prepare`d at 44.1 kHz are re-clamped;
  the stored anchor is **not** overwritten, so returning to 96 kHz restores the original.
- `setFilterBaseCutoff` below 20 Hz or above Nyquist: clamped before the `setBaseCutoff` call.
- `setDustDensity` at 20 000 imp/s with `setDustGrainMs(200)`: mean concurrency would be 4 000 ≫
  `kMaxDustGrains = 24`. The density **cannot** be clamped down to fix it — `setVelvetDensity` floors
  at 100 imp/s (`noise_generator.h:315-317`) and FR-031 makes that generator the sole trigger source —
  so FR-035's clamp is bidirectional and lands on the **grain length**: at an effective 20 000 imp/s
  the ceiling is `1000 × 24 / 20 000 = 1.2 ms`, so `getDustGrainMs` reports 1.2 ms while
  `getDustDensity` reports 20 000. At the other end, at the 100 imp/s floor the ceiling is 240 ms, so
  the whole requested `[5, 200]` ms range is honoured. Both effective values are readable, so the
  clamp is observable rather than silent, and steal-oldest (FR-034) remains the backstop for the
  Poisson tail rather than the normal path.
- `setDustGrainMs` changed while grains are in flight: in-flight grains keep their original
  `phaseIncrement` and finish normally; only new grains use the new length. No grain is truncated.
- `setDustCarrierColor(slot, NoiseColor::Velvet)`: rejected, snapped to the default Brown — **not**
  because of `NoiseOscillator`'s fallthrough (fixed in this phase, FR-098), but on musical-design
  grounds: Velvet is the impulsive colour FR-031 uses as the grain *trigger*, unsuitable as a
  continuous carrier. **`RadioStatic` is now accepted** (revised 2026-08-31, OQ-SHIPPED-DEFECTS/FR-032)
  now that `NoiseOscillator::process()` renders it correctly. `getDustCarrierColor` reports the
  effective colour.
- A `setSourceModel`/`setSourceNoiseType` write equal to the slot's current effective value: a no-op
  (decided 2026-08-31, Q5) — does not arm FR-013's duck, does not touch `NoiseGenerator`. A genuine
  change arriving while an earlier change's duck is still in progress updates the pending target
  without restarting the ramp; a burst of writes costs exactly one 50 ms duck.
- `setSourceLevel(slot, -96)`: the slot is inaudible but still costs CPU. `setSourceDormant` is the
  documented way to stop paying for it.
- Model or noise type changed mid-block: takes effect at the next control step, inside FR-013's 50 ms
  duck-and-restore (the slot gain ramps to 0, the `setNoiseEnabled` pair is swapped at the zero point,
  the gain ramps back). The previous model's **chain** state is **not** cleared — a resonator ringing
  from the previous model decays naturally rather than being cut. SC-018 measures both halves,
  including the arm proving the un-ducked path clicks.
- `setSourceWake` with a value outside `[0,1]`: clamped. NaN/Inf input: replaced by 0 (FR-008),
  detected on the exponent field. Every public setter behaves this way and SC-015 asserts it for all
  of them, with the non-finite arguments built from bit patterns so the check survives the macOS
  `-ffast-math` leg.

**Sample-rate changes**

- `prepare()` called a second time at a different rate: full re-initialisation and re-allocation, all
  time constants re-derived; the same seed produces a *rate-appropriate* stream, not the same samples
  (times in seconds are invariant, sample sequences are not — this is what SC-008 measures).
- 192 kHz with `maxCombDelayMs = 200`: 4 × 8 × 65 536 × 4 B = 8 MiB per organism. Documented in the
  header alongside FR-096's default figure, so a Phase-10 voice count is chosen with the real number.
- 44.1 kHz: the dust grain floor of 5 ms is 220 samples — still ≫ the 2048-entry envelope table's
  useful resolution, so `GrainEnvelope::lookup`'s interpolation is doing real work rather than
  stepping.

**Seed determinism**

- `setSeed(0)`: legal. `deriveStreamSeed` guarantees non-zero per-lane seeds
  (`random.h:98-101` documents exactly this hazard), and `Xorshift32::seed` substitutes its own
  default for 0 (`random.h:44-45`).
- Two organisms with the same seed but different `numSources`: slots 0..n−1 are identical streams —
  salts are indexed by slot, not by active count, so changing the count does not renumber lanes.
- Adding a lane in a later phase: salts come from a compile-time table (FR-005), so appending a lane
  cannot shift an existing lane's salt and silently change every Phase-2 render.
- `NoiseGenerator` instances that never call `setSeed` keep the historical `reset()` scramble
  (`noise_generator.h:189`) — deliberate, so no existing consumer's output moves (FR-081, SC-011).

## Open Questions

The roadmap's own Open Questions list (lines 485–498) defers **nothing** to this phase: items 1–7 are
scoped to Phases 6, 8, 9, 10, 11 and 12. There are therefore no roadmap-deferred open questions here.

### Decisions taken where the roadmap is silent

Recorded for the clarify/grill stage — each is decided above with a rationale, not left open.

1. **Mono output** (Non-Goals, FR-003). Every prescribed chain element is mono; stereo placement is
   Phase 3/10 work. The alternative (per-slot pan via `crossfade_utils` equal-power gains, the
   `harmonic_cloud.h:1818` pattern) is a wider component than the roadmap describes. **Reconfirmed
   2026-08-31 (OQ-MONO-AND-SHIFTER)** with the cost derivation and the `HarmonicCloud` contrast — see
   Non-Goals.
2. **`FrequencyShifter` omitted from metallic hiss** (FR-042). The roadmap calls it optional; the CPU
   arithmetic rules it out at `kMaxSources = 4`. A 1-slot cap was considered and rejected as a
   surprising asymmetry in the control surface. **Reconfirmed 2026-08-31 (OQ-MONO-AND-SHIFTER)** — see
   Non-Goals.
3. **`NoiseGenerator::setSeed` as an additive shared-component amendment** (FR-080–FR-083) rather than
   an in-organism decorrelation hack. It touches a component with five other consumers, which is why
   FR-081 and SC-011 pin the no-change guarantee explicitly.
4. **Q wanders on the frequency lane** rather than on its own (FR-064) — 16 extra lanes for an
   inaudible independence property.
5. **`ResonatorBank` spectral tilt fixed at 0** (FR-055) on measured-cost grounds, with the chain
   filter doing the spectral shaping instead.
6. **The organism owns `ResonatorBank`'s Q outright** (FR-052/FR-064): `setDecay` is a
   configuration-time call, `setQ` is the only per-control-step writer, and RT60 is documented as
   nominal-at-anchor. The alternative — re-applying `setDecay` on frequency drift *and* wandering Q
   through `setQ` — was rejected because the two calls write the same `qValues_` entry
   (`resonator_bank.h:349` vs `:383`) and each silently destroys the other.
7. **`TimeVaryingCombBank`'s internal motion is left off** (FR-042/FR-063): `setModDepth` and
   `setRandomModulation` stay at their `0.0f` defaults because the class has no `setSeed` and
   hard-seeds its per-comb PRNGs (`timevar_comb_bank.h:466`), so four identically configured slots
   would share a bit-identical trajectory. Adding `TimeVaryingCombBank::setSeed` was considered and
   deferred — a second shared-component amendment with its own `ContinuousBody` no-change guarantee,
   for no capability the salted Perlin lanes lack.
8. **`NoiseType::ModulationNoise` is excluded from the selectable roster** (FR-012), snapped to
   `TapeHiss` and reported through `getSourceNoiseType`. It is explicitly floor-less
   (`noise_generator.h:553-558`), so under FR-013's zero sidechain it renders exactly `0.0f`; twelve
   selectable models that all sound beats thirteen with one dead. Feeding the slot's own pre-chain
   output back as the sidechain was the alternative — rejected as a hidden feedback path in a
   component whose boundedness is a headline FR.
9. **The chain filter's resonance gets its own external lane** (FR-067) rather than
   `setResonanceRandomEnabled(true)`, so that every wandering filter parameter runs on a salted,
   seconds-to-minutes lane. Without it the roadmap's "**All filter parameters wander**"
   (Vorago-roadmap.md:190-191) would be silently half-implemented.
10. **`getSourceRms` is kept** (FR-015) even though roadmap Phase 2 does not ask for an energy read
    surface. It is not speculative: SC-009 and SC-010 both measure through it, its cost is inside the
    SC-004 measurement, and Phase 8's ecosystem energy sensing (Vorago-roadmap.md:123, which names
    `envelope_follower`) is free to replace it with `EnvelopeFollower` later.
11. **`setWanderEnabled`** (FR-068) exists because two success criteria need a control arm with no
    wander at all, and FR-023 deliberately keeps the `StochasticFilter`'s internal cutoff
    randomisation on. Without the setter that arm is unreachable through the public API and the
    criteria reduce to their positive halves.

## Review notes

### Corrections applied in review (2026-08-31)

Every item below changed a requirement or a criterion because the code says something different from
what the spec said. None was resolved by relaxing a threshold; where a threshold moved, the old value
was unachievable *by any correct implementation* and the citation showing that is in the FR or SC
itself.

1. **`BreathingModulator` is bipolar, not unipolar** (`breathing_modulator.h:103`, `:227-229`). The
   "Existing components" row was wrong and FR-070's bare multiply would have inverted the slot on
   every exhale. FR-070 now carries a normative affine mapping with a strictly positive factor, and
   SC-001 (d) asserts it.
2. **Zero resonators is silence, not bypass** (`resonator_bank.h:511` with `exciterMix_ = 0.0f` at
   `:589`). FR-051 and the Edge Cases now require the organism to skip the call; SC-019 (a) catches an
   implementation that forwards it.
3. **`ResonatorBank::setDecay` and `setQ` write the same variable** (`:349` vs `:383`). FR-052 now
   gives Q a single owner (configuration-time `setDecay`, per-control-step `setQ` only) and FR-064's
   factor folds into that one write.
4. **SC-002 (b) was inverted.** With no wander the band trajectory is stationary plus estimator noise,
   so `L ≈ 0.1 s`, not `> 6·T = 200 s` — and a 200 s lag is not resolvable from a 300 s record. The
   control arm now requires `L < 0.4·T`, reported lags are capped at 0.25 × record length, and the
   wander-on arm renders `≥ 10·T`.
5. **The dust concurrency clamp was unapplicable** — `setVelvetDensity` floors at 100 imp/s
   (`noise_generator.h:315-317`), so density cannot be clamped to 40. FR-035's clamp is now
   bidirectional (grain length is the movable term), `kMaxDustGrains` rose 8 → 24 so the full
   `[5, 200]` ms range is reachable at the density floor, and SC-004 (c) moved to 100 imp/s × 40 ms so
   steal-oldest is a backstop rather than the normal path.
6. **Chain-filter resonance never wandered**, against the roadmap's "All filter parameters wander"
   (Vorago-roadmap.md:190-191). Added as FR-067 with its own salt row in FR-005.
7. **`ModulationNoise` is floor-less** (`noise_generator.h:553-558`) and renders exactly `0.0f` under
   the mandated zero sidechain. FR-013's wording is corrected and FR-012 excludes it; SC-019 (a)
   asserts every selectable type sounds.
8. **`TimeVaryingCombBank` has no `setSeed`** and hard-seeds per-comb PRNGs (`timevar_comb_bank.h:466`).
   FR-005's salt table now says so explicitly and FR-042 pins the bank's internal motion off; the
   issue's option (a) was taken, and the deferred `setSeed` amendment is recorded as decision 7.
9. **`setNumCombs` cannot take 0** (`timevar_comb_bank.h:502`). FR-054 rewritten: the organism clamps
   `[0,4]` itself, forwards only `[1,4]`, and skips the stage at 0.
10. **`setCombDelay` forces `Tuning::Custom`** (`:189`, `:515`), so pinning `Inharmonic` was
    unattainable. FR-042 now has the organism compute the inharmonic ratios itself, and SC-020
    measures the resulting peak ratios — the property the model actually claims.
11. **The reference configuration was underspecified** and would have inherited
    `BreathingModulator`'s full-depth default and `StochasticFilter`'s 1 Hz / 2-octave defaults,
    failing SC-001 (a) and SC-002 (c) on a correct implementation. FR-016 is a normative defaults
    table; SC-001/SC-002/SC-004/SC-009/SC-013 name it.
12. **No control arm with wander off was reachable** (FR-023 keeps `cutoffRandomEnabled_ = true`,
    `stochastic_filter.h:555`). Added `setWanderEnabled` as FR-068; SC-002 (b) and SC-009 (b) name it.
13. **SC-010 asserted a state the product cannot be in.** Split into a lane/RNG freewheeling arm
    measured on the read surface and a statistics-only post-wake settle arm with a derived settle
    time. The sample-identity clause is gone.
14. **SC-008 demanded rate-invariance the reused colour filters do not provide**
    (`noise_generator.h:467-468`, `:483`, `:498`; `pink_noise_filter.h:65-70`; and the fifth
    `AudioFeatures` band is `[8k, Nyquist]`). FR-093 now states this honestly and SC-008 asserts RMS,
    finiteness, non-silence and the organism's own time constants in seconds — the issue's option (a).
15. **SC-011's "identical to the pre-amendment behaviour" was unbuildable** without a forbidden float
    golden (roadmap line 477, `tools/lint-float-bit-goldens.js`). Replaced with four behavioural
    invariants reachable through the public API.
16. **SC-007's control arm needed a second build.** Rebuilt in-process from two bare `NoiseGenerator`
    instances (`noise_generator.h:593`), and slot isolation switched from `setSourceLevel(-96)` to
    `setSourceDormant`.
17. **SC-014 measured bytes with a counter that has none** — `AllocationDetector` only counts
    (`allocation_detector.h:83-89`; the operator replacements discard `size`,
    `allocation_operator_overrides.h:66-95`). `getAllocatedBytes()` added to FR-015/FR-096; the
    detector is used for the count only.
18. **SC-009 had no discriminating power** at a 1 ms frame on broadband noise. The teeth moved to the
    gain domain via a new `getSourceGain`, with the envelope arm at 25 ms and a measured noise floor.
19. **SC-005's only finiteness assertion sat behind `[long]`**, which per-push CI excludes and which
    CLAUDE.md's `[long]` rule forbids for NaN/Inf sentinels. Split into an untagged 60 s arm and the
    30-minute soak; the clamp clause now reads a real counter (`getClampEngagementCount`, FR-074).
20. **Four FRs had no criterion.** Added SC-015 (FR-008 non-finite, with the `-fno-fast-math` TU in
    FR-097), SC-016 (FR-007 block-size invariance), SC-017 (FR-003 guard ladder) and SC-018
    (FR-011/FR-012/FR-013 transition continuity, including the arm proving the un-ducked path clicks).
21. **SC-013 pinned a trajectory-accumulating render at the shared sample tolerance**, against
    `render_fingerprint.h`'s own banner (`:116-121`, measured spreads at `:31-36`). It now uses a
    measured per-comparison bound recorded in `compliance.md`, with the injected-defect arm proving
    the loosened bound still fails.
22. **FR-036's stated mechanism did not deliver its stated property.** Restated at the time: the
    `1/sqrt(kMaxDustGrains)` divisor bounds the worst-case sum; continuity comes from the Hann
    window's zero endpoints. SC-019 (b) measures the residual level variation across a density sweep.
    **Superseded 2026-08-31 (Q3, in the Clarifications log above):** the fixed divisor is replaced by
    a concurrency-normalised per-grain gain, which flattens the level itself rather than only bounding
    its worst case.
23. **SC-004 (d) contradicted FR-095's remedy.** FR-095 now names the in-region envelope Phase 10 is
    bound by, which (a)–(c) sit inside and (d) deliberately sits outside.

No issue in the review was rejected. Three were resolved through the alternative the issue itself
offered rather than its first suggestion, and each says so in place: SC-008 takes option (a) (drop the
spectral clauses, state the honest FR-093); FR-042 takes option (a) for the comb seeding (pin the
bank's internal motion off, defer `TimeVaryingCombBank::setSeed`); and `getSourceRms` is **kept**
rather than dropped, recorded as decision 10 — it is now load-bearing for SC-009 and SC-010, its cost
is inside the SC-004 measurement, and Phase 8 may still replace it with `EnvelopeFollower`.

### Pre-existing notes

- Roadmap staleness corrected here, with citations: `NoiseGenerator` has **13** models, not 12
  (`noise_generator.h:61`); the comb bank class is `TimeVaryingCombBank`, not `TimevarCombBank`
  (`timevar_comb_bank.h:81`); `MultimodeFilter`, listed in the reuse row (line 110), is not used
  because the Phase-2 chain (line 189) specifies `StochasticFilter`.
- Two defects found in shipped code while writing this spec's first draft, originally scoped
  **reported and not fixed** (surgical changes): `NoiseOscillator::process()` silently returns white
  noise for `NoiseColor::Velvet` and `NoiseColor::RadioStatic` (`primitives/noise_oscillator.h:265-267`),
  and `ResonatorBank::setFrequency` does not re-derive Q from the configured decay the way `setDecay`
  does (`resonator_bank.h:328-332` vs `:345-352`), so a drifting frequency silently changes RT60.
  **Decision reversed in the 2026-08-31 clarification session (OQ-SHIPPED-DEFECTS): both are now fixed
  in this phase** — FR-098 and FR-099 — rather than worked around, after session verification found
  zero shipped consumers at risk for either fix (see FR-098/FR-099 and SC-011). FR-032's carrier
  roster and FR-052's Q-ownership design are revisited accordingly in FR-098/FR-099.
