# Feature Specification: Vorago Phase 1 — Modulation Gap-Fill + Slow Event Engine

**Spec slug:** `vorago-phase1-events-modulation`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 1 (lines 154–172); reuse row
`L8 Dark Modulation` (line 116) and `L11 Slow Event Engine` (line 119)
**Layer:** all new/changed code is Layer 2 (`dsp/include/krate/dsp/processors/`) plus one Layer 1
enum amendment (`chaos_waveshaper.h`, whose own banner reads `// Layer: 1 (Primitives)` at
`dsp/include/krate/dsp/primitives/chaos_waveshaper.h:7` — no Layer 0 file is touched; see FR-030 series)
**Test target:** `dsp_processors_tests` (source list `dsp/tests/CMakeLists.txt:255-287`)
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Overview

Vorago's identity is *discrete slow happenings* and *organic, never-looping drift*. Its modulation
substrate already exists — the six Seraphis life modulators shipped as Layer 2 `ModulationSource`
implementations and are verified present in this repo (table below) — so this phase closes the two
real gaps the roadmap names and nothing else. It adds **`PerlinNoiseSource`**, a 1D gradient-noise
(fBm-capable) modulation source that supplies the one texture the existing suite cannot produce
(band-limited coherent noise with a controllable roughness spectrum, as distinct from the
Ornstein–Uhlenbeck walk of `BrownianDrift` and the 3-layer beating sines of `TidalModulator`); it
adds the **Aizawa attractor** to the existing `ChaosModSource` roster; and it builds the first
Vorago identity component, **`SlowEventScheduler`** — a seeded, RT-safe, allocation-free stochastic
scheduler that fires one discrete event every 20–90 s (scalable), each event carrying a target id, a
seconds-to-minutes attack/hold/release envelope, a depth and a polarity. Every component here is
consumed by phases 2–8, so this phase ships first and ships complete: no audible synthesis, only
reusable building blocks with unit tests and an offline trajectory harness.

**Nothing in this phase re-implements existing DSP.** The ODR sweep and reuse verification below were
run against the real headers in this session; every "exists" claim carries a `file:line` citation.

## Clarifications

### Session 2026-08-31

- **Q1 — Envelope granularity.** Exact per-sample envelope: the control-rate (32-sample) loop still
  owns event draws and Idle/Attack/Hold/Release transitions, but the envelope *value* is computed
  every sample from the elapsed-sample counter. FR-003's 32-sample-staircase contract is now scoped
  to `PerlinNoiseSource` and `ChaosModSource` (including Aizawa) and explicitly excludes the
  scheduler's envelope output. No output smoother is added. SC-009's 100-points-per-segment stride,
  including the 50 ms shortest-segment configuration, is unchanged and unfloored.
  [FR-003, FR-065, SC-009]
- **Q2 — `getOctaveValue(i)` semantics.** Raw lattice noise: it returns octave `i`'s raw
  gradient-lattice noise at the current position, scaled only by `kGradientNormalize` (range
  `[-1,+1]`) — excluding `aₖ`, `Σaₖ`, depth and the output smoother. It is a pure function of
  `(seed, octave, position)` and is valid for any `octaveIndex < kMaxOctaves` regardless of the
  currently configured octave count. FR-015 is reworded from "the normalised contribution of one
  octave" to "the raw gradient-lattice noise of one octave stream"; its bit-identity clause and
  SC-004(c) hold as written and needed no further change. [FR-015, SC-004]
- **Q3 — Setters against an in-flight event.** Everything latches: one rule across the whole setter
  surface. `setEnvelopeTimes`, `setIntervalRange`, `setDepthRange`, `setBipolarProbability` and
  `setTargetCount` each update stored configuration immediately but never alter an in-flight event's
  drawn period, target, depth, polarity or fitted segment times until the next onset — extending
  FR-062's `setSeed` rule to the whole surface. FR-055's fit rule is therefore re-evaluated on the
  stored configuration only at draw time, never under a running envelope. New criterion: hammer every
  setter during an active event ("setter storm") and assert the per-sample delta never exceeds
  SC-009's slew bound. [FR-055, FR-066, SC-018]
- **Q4 — `prepare()` mid-flight.** Full re-initialisation: `prepare()` is identical in effect to
  `reset()` for both components — RNG rewound, scheduler returned to Idle with a freshly drawn first
  period, Perlin position zeroed (the `BrownianDrift` precedent, `brownian_drift.h:121-132`). The
  contradicting mid-flight sample-rate-switch Edge Case is reworded: periods and segment times are
  expressed in seconds and are therefore sample-rate invariant across two *independent* runs, which
  is exactly what SC-012 already measures. No rescaling `prepare()` and no mid-flight-continuity FR
  are added (no consumer exists before Phase 10). [FR-002, SC-012]
- **Q5 — Read-surface completeness.** Three accessors, not four: add `getEnvelopeValue()` (unipolar
  `[0,1]`), `getActiveDepth()` and `getActivePolarity()` to FR-058's read surface — each un-folds
  information `getCurrentValue()`'s `polarity · depth · envelope` product genuinely destroys.
  `valueForTarget(uint8_t)` is explicitly rejected as a speculative single-use convenience with zero
  information content; consumers write their own two-term target gate. [FR-058]
- **Q6 — Perlin lattice index domain and hash.** No wrap, `int64` lattice index: an unwrapped
  `double` position accumulator; a `std::int64_t` lattice index; the gradient sign bit is
  `deriveStreamSeed(octaveSeed, static_cast<std::size_t>(index + kIndexBias))` (`random.h:102`),
  named explicitly in FR-011/FR-012 in place of the previously-unnamed "stateless integer hash";
  `kIndexBias` is documented so negative lattice indices are well-defined. FR-007's wrap permission
  gets an explicit carve-out forbidding any wrap of the Perlin lattice index — for a lattice hash
  every wrap creates an exact repeat, which would silently violate the roadmap's "nothing repeats
  exactly" identity while still passing SC-017 as written. At `kMaxRate` the top octave reaches only
  ≈1.15e6 cells across SC-017's 8 h window, so `int64` is never stressed.
  [FR-007, FR-011, FR-012, SC-017]
- **Q7 — First onset after `prepare()`.** Idle a full drawn period first: after `prepare()`/`reset()`
  the scheduler idles for one freshly drawn period before its first onset — it does not fire at
  `t = 0`, consistent with FR-054's onset-to-onset definition and FR-061's "freshly drawn first
  period." New explicit FR-067 states the rule. Consequential fix: FR-081's harness event-count claim
  changes from "≥ 10 events at the slowest draw" to "≥ 9". SC-013's "≥ 20 rising edges" and SC-015's
  "≥ 3 rising edges in 900 s" were re-checked against the same arithmetic and both still hold with a
  wide margin (≈45 edges and ≈9 edges respectively at the pinned/worst-case configuration), so neither
  threshold changes. [FR-054, FR-061, FR-067, FR-081, SC-008]
- **Q8 — Harness output path.** CMake-injected compile definition: the harness resolves its output
  directory from `VORAGO_P1_HARNESS_DIR`, injected as `${CMAKE_BINARY_DIR}/vorago_p1/` via
  `target_compile_definitions` in `dsp/tests/CMakeLists.txt` — deterministic from any working
  directory and reproducible on all three OS legs and in CI. The harness stays `[.harness]`-tagged and
  SC-015's "default suite run writes no CSV files" clause is unchanged. [FR-081]
- **OQ-1 — Aizawa scope.** Append Aizawa with inert waveshaper arms: `ChaosModel` gains `Aizawa = 4`
  as drafted in FR-034, implemented in `ChaosModSource` with the drafted constants; `ChaosWaveshaper`
  gets unreachable-by-construction `case` arms sharing `Lorenz` behaviour at both switch sites, its
  validator semantics and existing tests stay untouched, and no real audible Aizawa mode is added to
  `ChaosWaveshaper`. Confirms the FR-030 series as drafted; no spec-body change required.
  [FR-031, FR-032, FR-033, FR-034, SC-006, SC-007]
- **OQ-2 — fBm as a mode or a second class.** One class with `setOctaves(1..4)`: `n = 1` is plain
  gradient noise; no separate `FbmNoiseSource` class. Confirms FR-014 as drafted; no spec-body change
  required. [FR-014]
- **OQ-3 — Routing integration.** ABC conformance only; all routing wiring deferred to Vorago
  Phase 10: Phase 1 ships `PerlinNoiseSource` and `SlowEventScheduler` conforming to the
  `ModulationSource` ABC and nothing more — no `ModSource::Perlin`/`ModSource::SlowEvent` slots, no
  `kModSourceCount` bump (that Layer-0 enum's indices are persisted in shipped plugins' state).
  Confirms the Non-Goals section's existing correction of the roadmap's false "routes unchanged"
  premise; no further spec-body change required. [FR-001; Non-Goals section]
- **OQ-4 — Event target vocabulary.** Opaque `uint8_t` target index: target ids stay opaque indices
  with a settable count (≤ 16); no shared Layer-0 `VoragoEventTarget` enum is defined now. Confirms
  FR-059 as drafted. Recorded ODR result stands: `ScheduledEvent` is forbidden (3 existing hits
  outside `Krate::DSP`); the nested POD stays `SlowEventScheduler::Event`. [FR-059]
- **OQ-5 — Event envelope defaults vs the roadmap's "seconds–minutes".** Option (a) confirmed: the
  default cadence stays 20–90 s (FR-052) with the ~16 s envelope fitted inside the minimum cadence
  (FR-055); minutes-scale envelopes remain reachable only by widening the interval range — e.g.
  `setIntervalRange(300, 600)` — never by changing the default. FR-053's single-active-event
  invariant survives unchanged; FR-054 and FR-055 keep their current defaults. Option (c) (allow
  events to overlap) is rejected as redundant scope: roadmap line 163's "multiple independent
  schedulers per voice" already provides concurrency, which is how overlapping events are obtained —
  several single-event schedulers rather than one multi-event scheduler — without a fixed event pool,
  an allocation-free slot allocator, a summing rule, or a multi-valued `getActiveTarget()`.
  [FR-052, FR-053, FR-054, FR-055]

### Session 2026-08-31 (b) — corrections found while planning

The plan stage re-derived this spec's arithmetic against the real headers and in WSL, and found five
places where the spec as written was wrong or unattainable. All five are amended in the body above;
**no threshold is relaxed by any of them.** Recorded here so the change is traceable rather than
silent. Full derivations: `plan.md` §8 items 1, 7, 8, 9, 10, 11.

- **Envelope shape — raised cosine → smootherstep.** The Q1 decision (per-sample evaluation) stands
  unchanged; only the *curve* changes. A per-sample `0.5 − 0.5·cos(π·u)` measures 4.84–4.94 ns per
  evaluation ⇒ ≈10 000 ns/block in SC-014's workload, i.e. **1.41× the 7111 ns baseline ceiling** —
  SC-014 is unattainable with a cosine. Smootherstep costs 0.86–0.87 ns ⇒ ≈1780 ns/block, and is C2
  rather than merely C1, so it satisfies the roadmap's continuity requirement more strictly. Every
  downstream figure re-derived and still inside bounds. [FR-056, FR-065, SC-009, SC-014, SC-018]
- **SC-017 windows 60 s → 1 h, and its RMS/ZCR clauses no longer apply to the scheduler.** As
  written, SC-017 **fails on a correct implementation** (5 of 8 seeds) and divides by zero for the
  scheduler when the FR-067 pre-roll exceeds the window. Thresholds stay at 20 %. [SC-017]
- **FR-018's cutoff parenthetical was arithmetically wrong** (7.96 Hz / ~14 dB is the *100 ms*
  smoother; the 20 ms one is 39.8 Hz / ≈3.0 dB). The 5 ms conclusion is unchanged, but the margin is
  far smaller than claimed — which is why SC-003(c) now carries a measured absolute floor
  (`fracAbove(8·rate) ≥ 1.30e-4` at n = 4) instead of resting on a shape argument, and is measured on
  the undecimated 1500 Hz trajectory to avoid aliasing at `kMaxRate`. [FR-018, SC-003]
- **Inverted `setIntervalRange(90, 20)` collapses to 90 s, not 20 s.** The Edge Cases line
  contradicted FR-052 in the same document. [FR-052]
- **FR-005 reworded to "an owned seed"**, consumed either as an `Xorshift32` stream
  (`SlowEventScheduler`) or as a stateless `deriveStreamSeed` hash (`PerlinNoiseSource`, which owns no
  RNG object because FR-012 forbids a running stream). Nothing in the implementation changes. [FR-005]
- Cosmetic, no requirement depends on either: the Aizawa collapse fixed point is z ≈ **−**1.105 (sign
  was wrong), and `max |out|` at `kMinSpeed` measures 0.750, so the quoted band is 0.750–0.780
  (SC-006(b)'s actual gate, `0.5 < max |out| < 0.99`, is unaffected). [SC-006]

## Scope

In scope:

- One new Layer 2 modulation source, `PerlinNoiseSource`, conforming to the existing
  `Krate::DSP::ModulationSource` ABC (`dsp/include/krate/dsp/core/modulation_source.h:31,37,41`),
  including the octave-summed (fBm) roughness control the roadmap requires (line 162).
- One new attractor model, `Aizawa`, added to the existing `ChaosModel` roster and implemented in
  `ChaosModSource` (roadmap line 164), plus the exhaustiveness/validation amendments the shared enum
  forces on `ChaosWaveshaper`, plus the two small observability/stability amendments FR-036 requires
  to make the Aizawa criteria measurable at all.
- One new Layer 2 component, `SlowEventScheduler` (roadmap lines 166–168).
- Unit tests covering the roadmap's four success-criteria families (Perlin smoothness + spectral
  rolloff; scheduler inter-event distribution; event envelope C1 continuity; 60 s CSV renders) plus
  the cross-cutting gates (boundedness, seeded determinism, sample-rate change, zero allocation,
  control-rate cost, long-run numerical resolution).
- An offline evaluation harness that renders trajectories to CSV (roadmap line 172). Unlike the
  Seraphis Phase 1 precedent — where the harness sat in Scope with **no FR and no SC and was
  consequently never built** (`specs/seraphis-phase1-life-modulators/spec.md:69`; no `.csv`
  producer exists anywhere under `dsp/tests/` or `tools/`) — here it is FR-081/FR-082 and SC-015 and
  is therefore gated.

## Non-Goals (owned by later phases)

- **Any consumer of these components.** `NoiseOrganism` (Phase 2), `ResonanceDriftNetwork` (Phase 3),
  `FeedbackEcology` (Phase 5), `BloomEngine` (Phase 7) and `EcosystemEngine` (Phase 8) are the
  things that subscribe to scheduler events and Perlin drift. This phase produces values; nothing
  listens yet.
- **Routing-engine integration.** The roadmap asserts `PerlinNoiseSource` will conform to
  `ModulationSource` "so `ModulationEngine`/`VoiceModRouter` route it unchanged" (line 162–163).
  **This claim is false as written and was verified false this session:** `ModulationEngine` dispatches
  through a fixed `switch (source)` over the `ModSource` enum
  (`dsp/include/krate/dsp/systems/modulation_engine.h:659-700`, enum at
  `dsp/include/krate/dsp/core/modulation_types.h:36-63` with `kModSourceCount = 18`), and
  `VoiceModRouter::computeOffsets` takes **eight explicit float arguments**
  (`dsp/include/krate/dsp/systems/voice_mod_router.h:155-171`) indexed by the fixed
  `VoiceModSource` enum (`dsp/include/krate/dsp/systems/voice_mod_types.h:29-40`). Neither routes a
  `ModulationSource*` polymorphically. Implementing the ABC therefore does **not** make a source
  routable. Decided in Clarifications OQ-3 (2026-08-31); the resolution follows the Seraphis precedent (routing deferred to the
  voice/engine phase, i.e. Vorago Phase 10).
- **Refactoring `StochasticFilter`.** It owns a private 3-octave coherent-noise implementation
  (`dsp/include/krate/dsp/processors/stochastic_filter.h:722 perlin1D`, `:742 noise1D`,
  `:760 gradientAt`, mode enum `:44 RandomMode`, enumerator `Perlin` at `:48`). Note it is **value
  noise, not gradient noise** — `noise1D` lerps the hashed values themselves
  (`return g0 * (1.0f - u) + g1 * u;`, `:754`) rather than the gradient dot products FR-011 specifies,
  and its hash returns a continuous value in [-1,1] rather than a unit gradient. `PerlinNoiseSource`
  is a standalone `ModulationSource` with different mathematics, lifecycle, seeding and range
  guarantees. **No speculative unification** — `StochasticFilter` is not touched, and its tests
  (`dsp/tests/unit/processors/stochastic_filter_test.cpp:805+`) must stay green untouched.
- **Extending `ChaosWaveshaper` with an audible Aizawa mode.** The roadmap scopes Aizawa to
  `ChaosModSource` only (line 164). The waveshaper changes here are strictly the minimum the shared
  enum forces (FR-034), not a new distortion character.
- **Any plugin, parameter ID, UI, preset or state-version work** (Phases 11–14).
- **Multi-scheduler orchestration policy** (which targets exist, how many schedulers a voice owns,
  how event depth scales with the Life/Age macros) — Phases 7, 8 and 10.

## Existing components (verified this session)

Every row was opened and read; signatures are quoted from the file, not from memory. Line numbers
were re-verified against the working tree in this session (an earlier pass had 1–5 line drift in
`random.h`, `tidal_modulator.h`, `growth_envelope.h`, `pattern_scheduler.h` and `stochastic_filter.h`;
those are corrected here).

| Component | Header (verified) | What Phase 1 reuses / relies on |
|---|---|---|
| `ModulationSource` (ABC, L0) | `dsp/include/krate/dsp/core/modulation_source.h:31` | The contract all three new/extended sources implement: `virtual float getCurrentValue() const noexcept = 0` (`:37`), `virtual std::pair<float,float> getSourceRange() const noexcept = 0` (`:41`). Only these two are virtual — `setSeed`/`prepare` stay non-virtual, per the `BrownianDrift` precedent (`brownian_drift.h:141-148`). |
| `Xorshift32` (L0) | `dsp/include/krate/dsp/core/random.h:41` | Sole PRNG. `explicit constexpr Xorshift32(uint32_t seedValue = 1)` (`:45`), `next()` (`:50`), `nextFloat()` → [-1,1] (`:59`), `nextUnipolar()` → [0,1] (`:67`), `seed(uint32_t)` (`:73`), `state()` (`:79`). **Seed 0 is silently replaced by `kDefaultSeed = 2463534242u`** (`:74`, definition `:85`) — an edge case both new components must document. |
| `deriveStreamSeed` (L0) | `dsp/include/krate/dsp/core/random.h:102` | `constexpr uint32_t deriveStreamSeed(uint32_t base, size_t salt) noexcept` — lowbias32 finaliser with guaranteed non-zero result. Used to give each fBm octave / each scheduler instance an independent, non-colliding stream from one user seed. |
| `BrownianDrift` (L2) | `dsp/include/krate/dsp/processors/brownian_drift.h:94` | **The shape template** every new source copies: `prepare(double) :121`, `reset() :133`, `setSeed(uint32_t) :145`, `setDepth(float) :159`, `process() :178`, `processBlock(size_t) :194`, `getCurrentValue() override :212`, `getSourceRange() override :217` (fixed `{-1.f, 1.f}`), `kControlRateInterval = 32` (`:105`), sample-rate floor `sampleRate > 1.0 ? sampleRate : 1.0` (`:122`), defaults `kDefaultSmoothness/kDefaultDepth/kDefaultDriftSeed` (`:108-110`). **Load-bearing for FR-018:** its raw control-rate value is a 32-sample staircase; the thing that makes it meet the 2.0e-3 slew threshold is the mandatory `kDriftOutputSmoothMs = 150.0f` one-pole (`:102`), applied in `getCurrentValue()` (`:212-214`). Not modified. |
| `TidalModulator` (L2) | `dsp/include/krate/dsp/processors/tidal_modulator.h:122` | Exists (contradicting nothing in the roadmap). `kMinPeriod = 30.0f`, `kMaxPeriod = 600.0f` (`:125,127`), `kNumLayers = 3` (`:132`). Covers the 30 s–10 min "seasonal" slot; `PerlinNoiseSource` must not duplicate it (FR-011 differentiates them). |
| `BreathingModulator` (L2) | `dsp/include/krate/dsp/processors/breathing_modulator.h:105` | `kMinRate = 0.01f` / `kMaxRate = 0.5f` (`:108,110`), `prepare :144`, `setSeed :164`, `processBlock :209`. Reused unchanged by later phases; cited here only to prove the gap-fill is a gap. |
| `OrbitModulator` (L2) | `dsp/include/krate/dsp/processors/orbit_modulator.h:105` | `kMinRate/kMaxRate 0.01–0.5 Hz` (`:108,110`); precedent for out-of-band extra output accessors (`getY()`) alongside the single-float ABC — the pattern `SlowEventScheduler`'s read surface follows (FR-058). |
| `SplineTrajectory` (L2) | `dsp/include/krate/dsp/processors/spline_trajectory.h:114` | `kMinInterval = 0.5f` / `kMaxInterval = 30.0f` s (`:117,119`). Fixed-ring, no-allocation waypoint precedent for the scheduler's fixed event slot. Its C1 test is the template SC-009 copies (see that criterion). |
| `GrowthEnvelope` (L2) | `dsp/include/krate/dsp/processors/growth_envelope.h:93` | Unipolar `[0,1]` one-shot rise, `kMinDuration = 1.0f` … `kMaxDuration = 60.0f` s (`:96,98`), **no fall segment**. Therefore it cannot serve as the slow-event envelope (which needs rise **and** fall) — the justification for FR-055. Not modified. |
| `ChaosModSource` (L2) | `dsp/include/krate/dsp/processors/chaos_mod_source.h:35` | **Extended** by FR-030 series. `kMinSpeed = 0.05f` / `kMaxSpeed = 20.0f` (`:37,38`), `kControlRateInterval = 32` (`:43`). Per-model tables live in three `switch (model_)` statements: `updateModelParams() :158-179`, `resetModelState()`, `updateAttractor() :218-231`; output is `normalizedOutput_ = std::clamp(std::tanh(state_.x / normalizationScale_), -1.0f, 1.0f)` (`:236`); coupling perturbation `state_.x += coupling_ * inputLevel_ * 0.1f` (`:215`) with `setInputLevel` **unclamped** (`:119-121`); divergence guard `checkAndResetIfDiverged()` (`:306-313`) resets when any axis exceeds **`safeBound_ * 10.0f`** — note the 10× factor, and note that everything after `private:` (`:151`) is inaccessible, which is why FR-036 adds one accessor. |
| `ChaosModel` enum (L1) | `dsp/include/krate/dsp/primitives/chaos_waveshaper.h:52-57` | `enum class ChaosModel : uint8_t { Lorenz=0, Rossler=1, Chua=2, Henon=3 }` — **shared with `ChaosWaveshaper`**, which validates with `if (static_cast<uint8_t>(model) > static_cast<uint8_t>(ChaosModel::Henon)) model = ChaosModel::Lorenz;` (`:451-455`) and switches exhaustively without a `default:` at `:640` and `:685`. This is the blast radius of adding Aizawa (FR-034). |
| `RandomSource` / `SampleHoldSource` (L2) | `random_source.h:34` / `sample_hold_source.h:36` | Lifecycle precedent: `prepare(double) :45/:47`, `reset() :53/:55`, `processBlock(size_t) :66/:67`, `process() :87/:86`, both ABC overrides `:114,118 / :107,111`. Not modified. |
| `ModulationEngine` (L3) | `dsp/include/krate/dsp/systems/modulation_engine.h:659` | Read to establish the **non-goal**: `getRawSourceValue(ModSource)` is a fixed switch (`:659-700`); `setChaosModel(ChaosModel)` at `:493` and `getChaosModel()` at `:613` are the only chaos surface — they gain Aizawa for free once the enum grows, with no engine edit. |
| `VoiceModRouter` (L3) | `dsp/include/krate/dsp/systems/voice_mod_router.h:155` | Read to establish the same non-goal: `computeOffsets(float env1, float env2, float env3, float lfo, float gate, float velocity, float keyTrack, float aftertouch) noexcept` — eight floats, no polymorphism. |
| `PatternScheduler` (L2) | `dsp/include/krate/dsp/processors/pattern_scheduler.h:54` | **Explicitly rejected as a base**, per roadmap line 119 ("rhythmic, wrong time scale"). Verified reasons: it is tempo-synced (`prepare(double, size_t) :79`, `setTempoSync(...)`) and it fires through `using TriggerCallback = std::function<void(int step)>` (`:57`, installed at `:175`) — a `std::function` on the audio thread is the exact allocation hazard FR-004 forbids. `SlowEventScheduler` is polled, not callback-driven (FR-057). |
| `MultiStageEnvelope` (L2) | `dsp/include/krate/dsp/processors/multi_stage_envelope.h:61` | **Rejected as the event envelope**: `kMaxStageTimeMs = 10000.0f` (`:65`) caps a stage at 10 s; slow events need minutes-scale segments. Justifies the scheduler's own internal envelope (FR-055). |
| `OnePoleSmoother` / `SlewLimiter` (L1) | `dsp/include/krate/dsp/primitives/smoother.h:134` / `:468` | `configure(float smoothTimeMs, float sampleRate) :160` — **`smoothTimeMs` is time-to-99 %, i.e. τ = smoothTimeMs / 5** (`calculateOnePolCoefficient :77-93`, `coeff = exp(-5000 / (smoothTimeMs * sampleRate))`), clamped to `[kMinSmoothingTimeMs, kMaxSmoothingTimeMs] = [0.1f, 1000.0f]` ms (`:58,61`); `advanceSamples(size_t) :243` applies `coeff^N` in closed form. Used for block-boundary smoothing exactly as `BrownianDrift` does (`brownian_drift.h:126,187,204`). |
| Perf/allocation test idiom | `dsp/tests/unit/processors/life_modulators_perf_test.cpp:1-80` | The ns-per-512-sample-block-vs-checked-in-baseline pattern (`kBlockBudgetNs :54`, `kReferenceNsPerBlock :58`, `kBaselineNsPerBlock = 3000.0 :69`, `kRegressionFactor = 1.5 :72`) and the `-ffast-math`-safe finiteness check via the IEEE-754 exponent field (`:76-80`). **Load-bearing detail SC-014 inherits:** `:60-66` documents that `kBaselineNsPerBlock * kRegressionFactor` (4500 ns) must stay **below** `kReferenceNsPerBlock` (5333 ns) "so the test is no weaker than the SC-007 reference figure". Note its warning: `brownian_drift_test.cpp` is the **single owner** of the global `operator new/delete` replacements in this binary; new TUs include only `<allocation_detector.h>`. |

## New components

ODR sweep run this session over `dsp/`, `plugins/` and `tools/` with
`grep -rn "\(class\|struct\|enum class\) <Name>\b"`:

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `PerlinNoiseSource` | 2 | `dsp/include/krate/dsp/processors/perlin_noise_source.h` (new) | **0 hits** for `PerlinNoiseSource`, `PerlinNoise`, `FbmNoise`. Near-name hazard: `RandomMode::Perlin` enumerator (`stochastic_filter.h:48`) and the private `StochasticFilter::perlin1D/noise1D/gradientAt` helpers (`:722,742,760`) — private members of another class, no ODR conflict, but see the non-goal above. |
| `ChaosModel::Aizawa` (enumerator) | 1 (amendment) | `dsp/include/krate/dsp/primitives/chaos_waveshaper.h:52` (existing enum) | **0 hits** for `Aizawa` anywhere in `dsp/` or `plugins/`. Appended, never inserted (FR-031). |
| `SlowEventScheduler` | 2 | `dsp/include/krate/dsp/processors/slow_event_scheduler.h` (new) | **0 hits** for `SlowEventScheduler`, `SlowEvent`, `SlowEventConfig`, `EventEnvelope`, `EventTarget`, `EventSlot`. Near-name hazards: `PatternScheduler` (`pattern_scheduler.h:54`) and `GrainScheduler` (`grain_scheduler.h:29`) — distinct names, both untouched. **`ScheduledEvent` has 3 hits** (`plugins/gradus/tests/unit/processor/live_mode_byte_identical_test.cpp:117`, `plugins/ruinae/tests/unit/ruinae_byte_identical_post_lane10_test.cpp:144`, `tools/gen_v2_fixtures/common.h:27`) — all test/tool-local structs outside `Krate::DSP`; the name `ScheduledEvent` is therefore **forbidden** for this component. Nested `SlowEventScheduler::Event` is used instead (FR-053). |

`tools/lint-odr.js` and `tools/lint-layers.js` must pass on the result (SC-016).

## Functional Requirements

### Shared contract (all three new/extended modulation sources)

- **FR-001** — Each new source publicly derives from `Krate::DSP::ModulationSource` and overrides
  `getCurrentValue() const noexcept` and `getSourceRange() const noexcept`
  (`modulation_source.h:37,41`). This applies to **both** `PerlinNoiseSource` and
  `SlowEventScheduler` (see FR-051). Trace: roadmap line 162 ("conforming to `ModulationSource`").
- **FR-002** — Each exposes `prepare(double sampleRate) noexcept` and `reset() noexcept`; after
  `prepare`, `getCurrentValue()` is well-defined with no prior advance. `prepare` floors the sample
  rate at 1 Hz, matching `brownian_drift.h:122`, so no derived time constant can become non-finite.
  **`prepare()` is a full re-initialisation, identical in effect to `reset()` (Clarifications Q4):**
  it rewinds the owned `Xorshift32` to the freshly-seeded state, and for `SlowEventScheduler` returns
  the state machine to Idle with a freshly drawn first period (FR-067); for `PerlinNoiseSource` it
  zeroes the lattice position accumulator. This matches the in-repo `BrownianDrift` precedent, whose
  `prepare()` ends in `initState()` (`brownian_drift.h:121-132`). A sample-rate change is therefore
  never preserved mid-flight by `prepare()` — see the corrected Edge Case under "Sample-rate changes".
- **FR-003** — Each exposes `void process() noexcept` (advance one sample) and
  `void processBlock(size_t numSamples) noexcept` (advance a whole block at control rate), using the
  `kControlRateInterval = 32` decimation loop of `brownian_drift.h:194-206`. `processBlock(0)` is a
  no-op. `processBlock(n)` is observationally equivalent to `n` `process()` calls. **Consequence,
  load-bearing for FR-018 and SC-002:** the value produced by the decimation loop is a 32-sample
  staircase, not a per-sample continuous function; the control counter is instance state and is not
  reset per block, so control steps land at fixed absolute sample indices regardless of block
  partitioning (this is why SC-004 does *not* use two multiples of 32 as its discriminator).
  **Scope of the staircase contract (Clarifications Q1):** this governs `PerlinNoiseSource`'s output
  and `ChaosModSource`'s output (including Aizawa) — both write their observable value only inside
  the control-rate update. It does **not** govern `SlowEventScheduler`'s envelope output: FR-065
  requires that value to be computed every sample from the elapsed-sample counter, even though the
  control-rate loop still owns event draws and Idle/Attack/Hold/Release transitions.
- **FR-004** — Real-time safe: every method is `noexcept`; no heap allocation, lock, exception, I/O,
  `std::function`, or virtual call in the advance path after `prepare`. All storage is fixed-size
  members. Trace: roadmap Cross-Cutting Constraints (line 468).
- **FR-005** — Each is deterministic under a seed: an **owned seed**, set through a non-virtual
  `setSeed(uint32_t) noexcept` and consumed **either** as a running `Xorshift32` stream
  (`SlowEventScheduler`) **or** as a stateless `deriveStreamSeed` hash (`PerlinNoiseSource`, which owns
  no RNG object at all — FR-012 forbids a running stream, so it stores `configuredSeed_` plus its four
  derived octave seeds and hashes the lattice statelessly). Independent internal streams are derived
  with
  `deriveStreamSeed(base, salt)` (`random.h:102`) so no two streams collide on the zero-substitution
  path. Same seed + same call sequence ⇒ bit-identical output within one build. `reset()` rewinds
  the RNG to the post-`prepare` state (the `BrownianDrift` `reset` semantics, `brownian_drift.h:133`).
  Trace: roadmap line 168 ("Deterministic under seed").
- **FR-006** — `getSourceRange()` is **fixed at polarity full scale** and does **not** shrink with
  depth: `{-1.f, +1.f}` for both new sources. Depth attenuates inside that fixed range so a
  downstream depth control never double-attenuates. `getCurrentValue()` is inside that range at all
  settings and all times, and the bound must be **analytic** — a terminal `std::clamp` may exist as
  an inert net but may not be the thing that supplies the bound (SC-001, SC-006 and SC-011 each carry
  an explicit non-tautology clause asserting the clamp is not engaging).
- **FR-007** — Long-run time bases are accumulated in `double` (or an integer sample counter), never
  in `float`. A `float` phase/time accumulator advancing every control step loses sub-step resolution
  after ~10⁷ increments, i.e. within an hour of drone runtime; an 8-hour soak (roadmap Phase 10
  success criterion, line 387) must not quantize or freeze. Where a wrap is possible without changing
  observable output, the accumulator wraps rather than growing without bound. **Carve-out
  (Clarifications Q6): this permission does not extend to `PerlinNoiseSource`'s lattice index.** For
  a lattice hash, *any* wrap changes observable output and produces an exact repeat of the noise
  trajectory — silently violating the roadmap's "nothing repeats exactly" identity while still
  passing every criterion in this phase (SC-017's RMS and zero-crossing-rate stability clauses are
  satisfied by a perfect loop). The lattice index is therefore never wrapped: it is an unwrapped
  `std::int64_t` (FR-011), which at `kMaxRate` reaches only ≈1.15e6 cells over SC-017's 8 h soak
  window — never stressed. **Enforcing criterion:
  SC-017.**

### FR-010 series — `PerlinNoiseSource` (roadmap lines 161–163)

- **FR-011** — `PerlinNoiseSource` produces **1D gradient noise**: for a position `x`, output is the
  smootherstep-interpolated blend of two hashed **unit** gradients evaluated at the bracketing integer
  lattice points, `n(x) = lerp(g₀·t, g₁·(t−1), s(t))` with `s(t) = 6t⁵ − 15t⁴ + 10t³` and
  `gₖ ∈ {−1, +1}` (one hash bit selects the sign — this is what makes the FR-017 derivation and
  `kGradientNormalize = 2.0f` exact). This is *coherent* noise — smooth and band-limited by
  construction — and is characterised by a self-similar octave spectrum, distinguishing it from
  `BrownianDrift`'s Ornstein–Uhlenbeck walk, `TidalModulator`'s three beating sine pairs, and
  `StochasticFilter`'s **value** noise (`stochastic_filter.h:742-754`, which interpolates hashed
  values rather than gradient dot products). Trace: roadmap line 161.
  **Lattice index domain (Clarifications Q6):** the lattice index is `std::int64_t`, derived from an
  unwrapped `double` position accumulator that never wraps (see FR-007's carve-out). The gradient
  sign bit at lattice index `i` is
  `deriveStreamSeed(octaveSeed, static_cast<std::size_t>(i + kIndexBias)) & 1` (`random.h:102`) — the
  same lowbias32 finaliser already used to derive per-octave streams (FR-015), reused here as the
  stateless per-cell hash; `kIndexBias` is a documented positive constant large enough that
  `i + kIndexBias` never underflows `std::size_t` for any reachable `i`, so negative lattice indices
  are well-defined rather than undefined-by-omission.
- **FR-012** — Lattice gradients are produced by a **stateless integer hash of (lattice index, seed)
  — specifically `deriveStreamSeed` (`random.h:102`), the hash FR-011 names — never** by consuming a
  running RNG stream. Consequence (asserted by SC-004 and SC-005): the value at a
  given position is independent of how the position was reached — block size, `process()` vs
  `processBlock()`, and sample rate cannot change which gradients are used.
- **FR-013** — `setRate(float hz)` sets the lattice-crossing rate (features per second) and is
  clamped to `[kMinRate, kMaxRate] = [0.005f, 5.0f]` Hz — 200 s per feature at the slow end (drone /
  geological scale) down to 0.2 s at the fast end. The rate is read from a getter for test assertions.
  **Rationale for the 5 Hz ceiling (corrected):** it is *not* "audible-rate shimmer" — 5 Hz is far
  below audio, and at `kMaxOctaves = 4` the top octave's feature rate is 5 × 2³ = 40 Hz, still
  sub-audio. The ceiling exists so a single source can also serve the fast-but-not-audio jitter that
  phases 2 and 5 need (per-source level flicker, delay-time micro-drift), and it is the value that
  sets the worst case for SC-002 and FR-018 — which is why the output smoother of FR-018 is sized
  against `kMaxRate × kLacunarity^(kMaxOctaves−1)` and not against the default.
- **FR-014** — `setOctaves(int n)` selects octave-summed fBm with `n` clamped to
  `[kMinOctaves, kMaxOctaves] = [1, 4]`; `n = 1` is plain gradient noise. Fixed persistence
  `kPersistence = 0.5f` and lacunarity `kLacunarity = 2.0f` (matching the existing repo convention at
  `stochastic_filter.h:719-736`). The octave sum is normalised by `Σ amplitudeₖ` (= 1, 1.5, 1.75,
  1.875 for n = 1…4) so the output range is octave-count-independent. Higher `n` ⇒ more
  high-frequency detail ("roughness"). Trace: roadmap line 162 ("Octave-summed (fBm) variant for
  roughness control").
- **FR-015** — Each octave uses an independent gradient hash stream derived with
  `deriveStreamSeed(seed, octaveIndex)`, so octaves are uncorrelated. **Stated at the stream level,
  because it is false at the output level:** the octave-0 gradient sequence and its raw contribution
  are identical regardless of octave count, but the *observable* output differs by exactly the
  `1 / Σ amplitudeₖ` normalisation of FR-014 (1.0 for n = 1 vs 1.875 for n = 4), so a naive
  `setOctaves(4)` vs `setOctaves(1)` output comparison must **not** be asserted. To make the property
  testable the component exposes
  `[[nodiscard]] float getOctaveValue(std::size_t octaveIndex) const noexcept` — **(Clarifications
  Q2) the raw gradient-lattice noise of one octave stream** at the current position, scaled only by
  `kGradientNormalize` (range `[-1,+1]`) — it excludes the octave amplitude `aₖ`, the `1/Σaₖ`
  normalisation, depth and the output smoother — recomputed on demand (const, non-RT, used by SC-004
  and by the FR-081 harness). It is a pure function of `(seed, octaveIndex, position)` and is valid
  for any `octaveIndex < kMaxOctaves` regardless of the currently configured octave count, so SC-004(c)
  is callable without reconfiguring the instance. `getOctaveValue(0)` is bit-identical between a
  1-octave and a 4-octave instance with the same seed and position.
- **FR-016** — `setDepth(float)` clamps to `[0,1]` and scales output inside the fixed range (FR-006).
  At depth 0 the output is exactly 0 for all time.
- **FR-017** — Boundedness is analytic and documented in the header: for 1D gradient noise with unit
  gradients the raw value satisfies `|n(x)| ≤ 0.5`, attained mid-cell with opposing gradients
  (`t = 0.5`, `s(0.5) = 0.5`, `g₀ = +1`, `g₁ = −1` ⇒ `n = 0.5·0.5 + 0.5·0.5 = 0.5`), so the
  normalisation constant `kGradientNormalize = 2.0f` maps a single octave to exactly `[-1, +1]`; the fBm sum
  divided by `Σ amplitudeₖ` preserves that bound. The header states this derivation; the terminal
  clamp is inert (SC-001).
- **FR-018** — **Output smoothing is mandatory, not optional.** The raw control-rate value is a
  32-sample staircase (FR-003), so the "C1 by construction" property belongs to the underlying
  *function of position*, not to the emitted signal — the emitted signal has a step at every control
  boundary. Sizing that step at the worst case (`rate = kMaxRate = 5 Hz`, `octaves = 4`, 48 kHz):
  the lattice advance per control step is `5 × 32 / 48000 = 3.333e-3` cells and the analytic fBm
  slope bound is `kMaxSlope × Σ(aₖ·lₖ)/Σaₖ = 2.7 × 4/1.875 = 5.76` per cell (FR-017's
  `kGradientNormalize = 2.0` × the smootherstep-derived per-cell raw maximum ≈ 1.35), giving a raw
  step of **1.92e-2** — roughly 10× SC-002's 2.0e-3 threshold. The component therefore applies a
  mandatory `OnePoleSmoother` in `getCurrentValue()`, exactly as `BrownianDrift` does
  (`brownian_drift.h:102,212-214`), configured at
  **`kOutputSmoothMs = 5.0f`** (time-to-99 %, i.e. τ = 1 ms, cutoff ≈ 159 Hz per
  `smoother.h:77-93`). This constant is chosen to satisfy both constraints simultaneously:
  - **Step suppression (satisfies SC-002):** the steady-state per-sample delta of a one-pole driven by
    a step `J` every `N = kControlRateInterval` samples is `J · α / (1 − (1−α)^N)` with
    `α = 1 − exp(−5000 / (kOutputSmoothMs · sampleRate))`. At 48 kHz, `α = 2.0618e-2` and the gain
    factor is `4.237e-2`, so the worst-case emitted delta is `1.92e-2 × 4.237e-2 = 8.14e-4` — inside
    the 2.0e-3 threshold with 2.5× margin (SC-002 asserts this closed form, not just the threshold).
  - **No flattening of real content:** the fastest content the source can carry is the top octave at
    `kMaxRate × kLacunarity^(kMaxOctaves−1) = 40 Hz`, which a 159 Hz-cutoff one-pole attenuates by
    0.27 dB. (A 20 ms smoother — the value an earlier draft named — has a **39.8 Hz** cutoff and
    would attenuate that content by **≈3.0 dB**. `OnePoleSmoother`'s `smoothTimeMs` is time-to-99 %,
    so `τ = ms/5000` s and `f_c = 5000/(2π·ms)` Hz, `smoother.h:86-92`; 7.96 Hz is the *100 ms*
    cutoff, not the 20 ms one — an earlier draft of this FR quoted 7.96 Hz and ~14 dB here and was
    arithmetically wrong. The 5 ms figures above are correct. The conclusion is unchanged — 5 ms is
    still the right constant — but the margin is far smaller than that draft claimed, which is
    exactly why SC-003 clause (c) carries a **measured absolute floor** rather than resting on a
    shape argument, and why SC-003 repeats the roughness measurement at `rate = kMaxRate`.)
- **FR-019 — Defaults.** The component declares, in the `brownian_drift.h:108-110` style:
  `kDefaultRate = 0.1f` Hz, `kDefaultOctaves = 2`, `kDefaultDepth = 1.0f`,
  `kDefaultPerlinSeed = 0x9E37u`. These are the values in force after default construction and after
  `prepare()` with no configuration call, so FR-002's "well-defined with no prior advance", the
  Edge Cases' post-construction state, and the FR-081 harness configuration are all determinate.

### FR-030 series — Aizawa attractor in `ChaosModSource` (roadmap line 164)

- **FR-031** — `ChaosModel` gains `Aizawa = 4`, **appended after `Henon = 3`**, never inserted.
  Rationale (verified): `dsp/tests/unit/primitives/chaos_waveshaper_test.cpp:33-36` pins
  `Lorenz==0, Rossler==1, Chua==2, Henon==3` and `:40` static-asserts the `uint8_t` underlying type;
  `ModulationEngine::setChaosModel/getChaosModel` (`modulation_engine.h:493,613`) pass the enum
  through by value, and plugin state persists such enums by index. Appending keeps every existing
  index and every existing test valid.
- **FR-032** — `ChaosModSource` implements the Aizawa system with the standard literature parameters
  `a=0.95, b=0.7, c=0.6, d=3.5, e=0.25, f=0.1`:
  `dx = (z−b)x − d·y`, `dy = d·x + (z−b)y`, `dz = c + a·z − z³/3 − (x²+y²)(1+e·z) + f·z·x³`,
  integrated with the existing forward-Euler control-rate step. Added as new arms in all three
  `switch (model_)` statements (`chaos_mod_source.h:158-179`, `resetModelState()`, `:218-231`).
- **FR-033** — Per-model constants for Aizawa: `kAizawaScale = 1.5f` (the attractor's x-extent is
  ≈ ±1.5), **`baseDt_ = 5.0e-4f`**, **`safeBound_ = 25.0f`**, initial state `{0.1f, 0.0f, 0.0f}`.
  Output passes through the existing
  `std::clamp(std::tanh(state_.x / normalizationScale_), -1, 1)` path (`chaos_mod_source.h:236`)
  unchanged, so the FR-006 *bound* is inherited — but note that `tanh` alone would make a boundedness
  assertion tautological, which is why SC-006 carries its own non-tautology clause.
  **Both constants are measurement-derived, not copied from a sibling model:**
  - `baseDt_` is constrained by `baseDt_ × kMaxSpeed ≤ 0.01` because the effective step is
    `dt = baseDt_ × speed` (`chaos_mod_source.h:211`) and `kMaxSpeed = 20.0f` (`:38`). Forward-Euler
    Aizawa was simulated across `dt ∈ [5e-4, 0.2]` from four different initial states this session:
    it is chaotic with an x-extent of ±1.5…±1.6 for `dt ≤ 0.015`, and for **`dt ≥ 0.02` it collapses,
    silently and from every initial state tried, onto the `x = y = 0` fixed point (z ≈ −1.105), where
    the output is identically 0** — no divergence, no guard reset, just a dead modulator. An earlier
    draft's `baseDt_ = 0.01f` would therefore give `dt = 0.2` at `kMaxSpeed` and produce a constant
    zero across most of the speed range. `5.0e-4f` puts `dt` in `[2.5e-5, 0.01]`, entirely inside the
    verified-chaotic region with a 1.5× margin to the collapse edge.
  - `safeBound_` is constrained by the coupling path. `updateAttractor()` adds
    `coupling_ * inputLevel_ * 0.1f` to `state_.x` every control step (`:214-216`) and `setInputLevel`
    is unclamped (`:119-121`), so at `coupling = 1` with a full-scale DC-biased input the state is
    driven well off the attractor: simulated worst case is `|state| ≈ 112` at `kMinSpeed`. With
    `safeBound_ = 5.0f` (the Chua value) the guard threshold would be 50 and the guard fires ~2000
    times per 600 s render; `safeBound_ = 25.0f` puts the threshold at 250 and the guard never fires
    across the whole speed × coupling grid. The state legitimately *exceeds* the attractor's natural
    extent under coupling — that is what coupling does — and the requirement is only that it stays
    below the guard, stays finite, and that the output stays bounded by `tanh`.
- **FR-034** — Shared-enum blast radius on `ChaosWaveshaper` (Layer 1) is handled explicitly, because
  a growing enum breaks it two ways: (a) its `switch (model_)` statements at
  `chaos_waveshaper.h:640` and `:685` have **no `default:` arm**, so a new enumerator triggers
  `-Wswitch` and the zero-warning gate fails on GCC/Clang; (b) its validator at `:451-455` rejects
  anything above `Henon`. Resolution (decided in Clarifications OQ-1, 2026-08-31): keep the validator's semantics —
  `ChaosWaveshaper` continues to support Lorenz…Henon only, and `setModel(Aizawa)` continues to fall
  back to `Lorenz` — and add explicit `case ChaosModel::Aizawa:` arms to both switches that share the
  `Lorenz` behaviour, documented in-line as unreachable-by-construction and present solely for
  switch exhaustiveness. Existing chaos-waveshaper tests, including the invalid-enum fallback cases
  at `chaos_waveshaper_test.cpp:579-583`, stay green unmodified.
- **FR-035** — No other consumer changes. `ModulationEngine` needs no edit: `setChaosModel` /
  `getChaosModel` (`modulation_engine.h:493,613`) already forward the enum, so `ModSource::Chaos`
  gains the model with zero routing work.
- **FR-036 — Divergence observability.** `ChaosModSource` gains
  `[[nodiscard]] uint32_t getDivergenceResetCount() const noexcept`, a plain const accessor over a
  `uint32_t` counter incremented inside `checkAndResetIfDiverged()` (`chaos_mod_source.h:306-313`)
  and zeroed by `prepare()`/`reset()`. Reason this is a requirement and not a test detail: everything
  after `private:` (`:151`) is inaccessible, so `state_` and `safeBound_` cannot be inspected, and
  SC-006's "the guard never fires" clause is otherwise unwritable against the public API. The counter
  adds one `uint32_t` member and one branch already present; it is model-agnostic (it therefore also
  gives Lorenz/Rossler/Chua/Henon the same observability) and changes no existing behaviour, so all
  existing `ChaosModSource` tests stay green unmodified.

### FR-050 series — `SlowEventScheduler` (roadmap lines 166–168)

- **FR-051** — `SlowEventScheduler` is a Layer 2 component in
  `dsp/include/krate/dsp/processors/slow_event_scheduler.h`, including only Layer 0/1 headers.
  It satisfies **FR-001** … FR-007 — i.e. it *is* a `ModulationSource` (public derivation, both
  virtuals overridden), which is what FR-058's `getCurrentValue()` override and SC-011's fixed
  `{-1,+1}` range assertion assume.
- **FR-052** — It draws the **period until the next event onset** from a bounded distribution over
  `[minIntervalSeconds, maxIntervalSeconds]`, set by
  `setIntervalRange(float minSeconds, float maxSeconds) noexcept`, defaulting to **20 s / 90 s**
  (roadmap line 167) and clamped to `[kMinIntervalSeconds, kMaxIntervalSeconds] = [1.0f, 600.0f]`
  (the roadmap's "scalable"). If `max < min` after clamping, both collapse to `min` — a degenerate but
  well-defined fixed period. The distribution is **uniform** over the range, drawn from the seeded
  `Xorshift32` via `nextUnipolar()`; no draw may fall outside the range (SC-008).
  `kDefaultEventSeed = 0x51E7u`.
- **FR-053** — One event is active at a time per scheduler instance. The active event is described by
  a nested POD `SlowEventScheduler::Event { uint8_t target; float depth; int8_t polarity; }` plus its
  envelope timing — **not** a type named `ScheduledEvent` (ODR near-name hazard, see the New
  Components table). Concurrency comes from instantiating several schedulers, per roadmap line 168
  ("Multiple independent schedulers per voice"); the component itself does not manage a pool.
- **FR-054 — Timeline, measured onset-to-onset.** The cycle is
  `Attack → Hold → Release → Idle → (next onset)`. The drawn period of FR-052 is measured from one
  event **onset** to the next, so **the observable event cadence is exactly the drawn period** and the
  idle stretch is the remainder, `period − (attack + hold + release)`.
  **Why this and not "wait measured from the end of release":** the roadmap states the *cadence*, not
  a wait — "Events fire every 20–90 s" (line 31), "one event per 20–90 s" (line 119), "draws
  next-event time from a bounded distribution (20–90 s default)" (line 166). Measuring the drawn
  interval from the end of the release would make the delivered cadence `wait + attack + hold +
  release`; with a 45/30/90 s envelope that is 185–255 s, three to four times rarer than the identity
  the roadmap specifies, and no criterion in an earlier draft could catch it. Onset-to-onset makes
  the drawn quantity and the observed quantity the same thing, so SC-008 measures what the roadmap
  states.
  FR-055's fit rule guarantees `attack + hold + release ≤ minIntervalSeconds`, so events never
  overlap for any draw and FR-053's single-active-event invariant holds without truncation.
  Both the drawn period and the effective envelope times are readable (FR-058).
- **FR-055 — Event envelope, with a fit rule.** The envelope is internal to the component
  (justification: `MultiStageEnvelope` caps a stage at 10 s, `multi_stage_envelope.h:65`;
  `GrowthEnvelope` has no fall segment and caps at 60 s, `growth_envelope.h:98`). Segment times are
  set by `setEnvelopeTimes(float attackSeconds, float holdSeconds, float releaseSeconds) noexcept`,
  each clamped to `[kMinSegmentSeconds, kMaxSegmentSeconds] = [0.05f, 300.0f]`, defaulting to
  **5 s / 3 s / 8 s** (16 s total, leaving ≥ 4 s of idle at the shortest default draw of 20 s).
  **Fit rule:** the *effective* segment times are the configured times multiplied by
  `min(1, minIntervalSeconds / (attack + hold + release))`. Uniform scaling preserves the envelope
  shape (and therefore C1) and is order-independent. **Per the FR-066 latch rule (Clarifications Q3),
  the fit is re-evaluated against the currently *stored* configuration at the moment the next event is
  drawn — never applied to a running envelope.** Calling `setEnvelopeTimes` or `setIntervalRange`
  while an event is in flight updates the stored configuration immediately (visible right away
  through their getters) but does not touch the in-flight event's effective segment times; the new
  fit is computed once, at the next Attack onset, so the configuration is deterministic regardless of
  call order. Minutes-scale envelopes remain reachable exactly as the roadmap's
  "seconds–minutes" (line 167) requires — e.g. `setIntervalRange(300, 600)` with
  `setEnvelopeTimes(120, 60, 120)` fits unchanged. A segment at the minimum is still C1 (FR-056).
  **Resolved tension, stated plainly (Clarifications OQ-5, 2026-08-31):** the roadmap's "Events fire
  every 20–90 s" (identity statement, repeated at lines 31, 119 and 166) and its "envelope over
  seconds–minutes" (capability statement, line 167) cannot both describe the *default* configuration
  of a single non-overlapping scheduler — a minutes-scale envelope does not fit inside a 20 s minimum
  cadence without either truncating it (violating FR-053's single-active-event invariant) or
  delivering a cadence several times rarer than the roadmap states. This is not left for the reader to
  rediscover: the default stays the 20–90 s cadence with the envelope fitted inside it, exactly as
  drafted above, and a minutes-scale envelope is reached only by widening `setIntervalRange`, never by
  changing the default segment times or by letting events overlap.
- **FR-056** — Attack and release are **C1 rise shapes with zero first derivative at both ends**,
  implemented as the smootherstep polynomial `f(u) = 6u⁵ − 15u⁴ + 10u³` (rising, its mirror falling);
  hold is flat at 1, idle is flat at 0. The envelope is therefore C1 at every join by construction —
  the derivative is zero on both sides of every join — which is the roadmap's "event envelope
  continuity (C1, no clicks)" requirement (line 171); the polynomial is in fact C2 there, since its
  second derivative also vanishes at both ends.
  **Why a polynomial and not a raised cosine** (plan §8 item 7, measured WSL g++ 13 `-O2`, 2×10⁷
  iterations): FR-065 makes `getCurrentValue()` a per-sample call that nothing caches, so the shape
  function is evaluated 2048 times per 512-sample block in SC-014's four-scheduler workload.
  `0.5 − 0.5·cos(π·u)` costs 4.84–4.94 ns per evaluation ⇒ ≈10 000 ns/block, which is 0.94× SC-014's
  10 667 ns absolute reference and **1.41× the 7111 ns ceiling** its `static_assert` imposes on the
  baseline — before the four `PerlinNoiseSource` instances and the Aizawa source are counted. SC-014
  is therefore unattainable with a cosine. The polynomial costs 0.86–0.87 ns ⇒ ≈1780 ns/block.
  All consequences stay inside their existing bounds and **no threshold is relaxed**: peak slope
  1.875/T instead of ½π/T ⇒ worst-case per-sample slew 7.81e-4 (was 6.54e-4) against SC-009/SC-018's
  2.0e-3; peak second derivative 10/√3 instead of ½π² ⇒ decimated interior second difference ≈5.77e-4
  (was ≈4.93e-4), i.e. SC-009's anti-vacuity guard gets *stronger*; onset residual artefact ≤ 2.4e-5
  (was ≤ 4.4e-4). No discontinuity may be
  introduced by block boundaries: the envelope is evaluated from the elapsed-sample counter, not
  accumulated per block.
- **FR-057** — The component is **polled, never callback-driven**: no `std::function`, no listener
  registration, no virtual dispatch in the advance path (contrast `PatternScheduler`'s
  `TriggerCallback`, `pattern_scheduler.h:57`, which is precisely the allocation hazard being avoided).
  Consumers read state after `processBlock`.
- **FR-058 — Read surface**, all `noexcept` and all const. The multi-value accessor pattern follows
  `OrbitModulator::getY()` (`orbit_modulator.h`) — extra outputs are plain non-virtual members
  alongside the single-float ABC override:
  - `getCurrentValue()` — the ABC override (FR-001); returns `polarity · depth · envelope(t)`,
    bounded to the fixed `{-1,+1}` range of FR-006.
  - `getActiveTarget()` — the active event's target id, or `kNoTarget = 0xFFu` when idle.
  - `isEventActive()` — whether an event is in attack/hold/release.
  - `getEventPhase()` — the current segment.
  - `getPeriodSeconds()` — **the period drawn for the cycle currently in flight**, in seconds. This
    is the observable SC-008 is written against; without it the drawn distribution is not readable at
    all, and inferring it from `isEventActive()` edges would quantize it to the control step and
    measure a different quantity.
  - `getEventDurationSeconds()` — the effective `attack + hold + release` of the current cycle.
  - `getEffectiveAttackSeconds()` / `getEffectiveHoldSeconds()` / `getEffectiveReleaseSeconds()` —
    the post-fit-rule segment times of FR-055, so a caller (and SC-009) can see what the fit did.
  - `getEnvelopeValue()` — **(Clarifications Q5)** the unipolar envelope shape alone, `[0,1]`, i.e.
    `getCurrentValue()` with the `polarity · depth` factor divided back out (0 while idle).
  - `getActiveDepth()` — **(Clarifications Q5)** the depth drawn for the event currently in flight
    (or the last event's depth while idle), `[0,1]`.
  - `getActivePolarity()` — **(Clarifications Q5)** the polarity drawn for the event currently in
    flight (or the last event's polarity while idle), `±1`.

  These three exist because `getCurrentValue()`'s `polarity · depth · envelope(t)` product genuinely
  destroys information a consumer may need back — e.g. the envelope shape under the consumer's own
  scaling. No fourth accessor (`valueForTarget(uint8_t)`) is added: it would be a one-line convenience
  with no information content beyond what `getActiveTarget()` plus `getCurrentValue()` already
  provide; each consumer writes its own two-term target gate (Clarifications Q5).
- **FR-059** — Target selection: each event draws its target uniformly from `[0, targetCount)`, with
  `setTargetCount(uint8_t)` clamped to `[1, kMaxTargets] = [1, 16]`. Target ids are **opaque
  indices**; this component assigns no meaning to them (meaning is the consumers' business in
  phases 2–8). Decided in Clarifications OQ-4 (2026-08-31).
- **FR-060** — Depth: each event draws its depth uniformly from `[minDepth, maxDepth]` set by
  `setDepthRange(float, float) noexcept`, both clamped to `[0,1]`, default `0.3f`/`1.0f`. Polarity is
  drawn as ±1 with a settable probability `setBipolarProbability(float) noexcept` clamped to `[0,1]`,
  default `0.5f`; at 0 every event is positive. Each event consumes exactly one `nextUnipolar()` draw
  per attribute (period, target, depth, polarity), in that fixed order, so two runs that differ only
  in a *range* setting stay RNG-aligned (SC-011 relies on this).
  Trace: roadmap line 167 ("depth, polarity").
- **FR-061** — `reset()` returns the scheduler to Idle with the RNG rewound to its post-`prepare`
  state and a freshly drawn first period identical to the one drawn after `prepare` — i.e. the whole
  event stream after `reset()` is bit-identical to the stream after `prepare()` (FR-005).
- **FR-062** — `setSeed()` called while an event is active does not truncate that event; the new seed
  takes effect from the next draw. This keeps a live drone from clicking when a host changes a seed
  parameter.
- **FR-063** — A `processBlock(n)` spanning multiple complete events is handled without allocation
  and without unbounded looping: the advance is computed from elapsed samples, and any number of
  state transitions inside one block is resolved in a bounded loop. Trace: the analogous
  `SplineTrajectory` requirement (`spline_trajectory.h` rotate-while-consumed pattern).
- **FR-064 — Non-accumulating timeline.** Every segment and period boundary carries its fractional
  remainder forward: the transition is taken with `elapsed -= targetSamples`, **never**
  `elapsed = 0`, and `targetSamples` is computed in `double` from wall-clock seconds (consistent with
  FR-007). Without this requirement each of the four transitions per cycle would quantize
  independently to the control grid — 0.726 ms at 44.1 kHz vs 0.333 ms at 96 kHz — and over the 50
  events SC-012 measures (~200 transitions) the two sample rates would drift apart by up to ~79 ms,
  two orders beyond SC-012's one-control-step tolerance. FR-056 already mandates non-accumulating
  evaluation for the envelope *shape*; FR-064 extends it to the *timeline*, which is what makes
  SC-012's bound attainable rather than aspirational.
- **FR-065 — Envelope granularity (Clarifications Q1).** The control-rate loop
  (`kControlRateInterval = 32`, per FR-003) is responsible only for drawing events
  (FR-052/FR-059/FR-060) and for taking Idle/Attack/Hold/Release state transitions (FR-054); it does
  **not** gate the envelope's numeric output. `getCurrentValue()` computes the FR-056 envelope shape
  from the elapsed-sample counter **every sample**, so the emitted value is per-sample
  continuous rather than a 32-sample staircase. No output smoother is added — none is needed, because
  per-sample evaluation has no step to smooth. This corrects what FR-003's blanket staircase language
  would otherwise imply for this component; see FR-003's scope clause. Enforced by SC-009, whose
  100-points-per-segment measurement grid is valid only under per-sample evaluation (a control-rate
  staircase would make its `interiorMax`/`joinMax` ratio measure quantization, not curvature).
- **FR-066 — Setter latch rule, whole surface (Clarifications Q3).** Extends FR-062's `setSeed`
  latch semantics to every configuration setter: `setEnvelopeTimes`, `setIntervalRange`,
  `setDepthRange`, `setBipolarProbability` and `setTargetCount` each update stored configuration
  immediately (visible to their getters), but an event already in flight keeps the period, target,
  depth, polarity and effective segment times it was drawn/fitted with until the next onset. FR-055's
  fit rule is therefore re-evaluated on the stored configuration only at draw time, never mid-event.
  One rule for the whole setter surface, applied uniformly, is what keeps every setter click-free by
  construction. Enforcing criterion: SC-018.
- **FR-067 — First onset, after `prepare()`/`reset()` (Clarifications Q7).** After `prepare()` or
  `reset()` the scheduler idles for one freshly drawn period (FR-052) before its first event onset —
  it does **not** fire at `t = 0`. This is the only reading consistent with FR-054's onset-to-onset
  period definition and FR-061's "freshly drawn first period," and it keeps SC-008's inter-event
  uniformity histogram clean (no special-cased first interval). Multiple concurrent schedulers per
  voice still decorrelate immediately because each draws from its own independent seeded stream.
  Enforcing criterion: SC-008; consequential to FR-081's harness event count (below).

### FR-080 series — Offline evaluation harness (roadmap line 172)

- **FR-081** — A Catch2 case tagged `[.harness]` (hidden by default, so it never lengthens the
  per-push suite) renders trajectories at 48 kHz and writes CSVs, one row per control step, to a path
  resolved from the CMake-injected compile definition `VORAGO_P1_HARNESS_DIR` (Clarifications Q8):
  `dsp/tests/CMakeLists.txt` sets it via `target_compile_definitions` to
  `${CMAKE_BINARY_DIR}/vorago_p1/`, so the output location is deterministic from any working
  directory and reproducible on all three OS legs and in CI. **Exactly five files**, named and
  configured so SC-015 is reproducible:
  | File | Source | Configuration | Duration | Columns |
  |---|---|---|---|---|
  | `vorago_p1_perlin_oct1.csv` | `PerlinNoiseSource` | `kDefaultRate`, octaves 1, depth 1, `kDefaultPerlinSeed` | 60 s | `timeSeconds,value` |
  | `vorago_p1_perlin_oct2.csv` | `PerlinNoiseSource` | as above, octaves 2 | 60 s | `timeSeconds,value` |
  | `vorago_p1_perlin_oct4.csv` | `PerlinNoiseSource` | as above, octaves 4 | 60 s | `timeSeconds,value` |
  | `vorago_p1_aizawa.csv` | `ChaosModSource` (Aizawa) | speed 1.0, coupling 0 | 60 s | `timeSeconds,value` |
  | `vorago_p1_slow_events.csv` | `SlowEventScheduler` | all defaults (20–90 s period, 5/3/8 s envelope), `kDefaultEventSeed` | **900 s** | `timeSeconds,value,target,phase` |
  The scheduler's window is 900 s, not 60 s, because at the roadmap's own 20–90 s cadence a 60 s
  render contains **zero or one** event and the file would, across most seeds, be a flat line of
  zeros with `target = kNoTarget` throughout — nothing to inspect. Per FR-067's idle-first-onset rule
  (Clarifications Q7), 900 s contains **≥ 9** events at the slowest possible draw (not 10 — the first
  up to 90 s is idle before the first onset) and ~16 at the mean, which is what "inspected for organic
  character" needs. The Perlin and Aizawa windows stay at the roadmap's 60 s (line 172).
- **FR-082** — The harness is opt-in only and writes nothing when not selected; no test in the
  default run performs file I/O. It exists for the roadmap's "inspected for organic character"
  evaluation step and is not itself an assertion of quality.

## Success Criteria

Each is measurable, with the metric, the threshold, and the test that measures it. Test names are
sketches (`dsp_processors_tests` Catch2 cases, filterable positionally, e.g.
`dsp_processors_tests.exe "PerlinNoiseSource_*"`).

**Definition — "accelerated rendering".** Several criteria below cover hours of wall-clock time.
*Accelerated* means: **`prepare()` at the criterion's stated sample rate, advancing with large
`processBlock(n)` calls** — `n = kControlRateInterval` (32) where onset resolution matters
(SC-008, SC-012), `n = 4096` where only bulk statistics matter (SC-006, SC-011, SC-017). This keeps
the control-step count identical to a real-time render of the same wall-clock duration, so nothing
about the signal changes; only the loop overhead is skipped.
**Lowering the `prepare()` sample rate is explicitly *not* the acceleration mechanism** — it changes
the control-step resolution, which is the very quantity SC-005 and SC-012 hold constant and the unit
in which SC-008(d) and SC-012 express their tolerances. SC-002, SC-005, SC-009 and SC-012 are
per-sample or rate-sensitive criteria and **may not** be accelerated at all; they render every sample
at the stated rate.

- **SC-001 (Perlin boundedness — analytic, not clamped).** Over a 300 s per-sample render at 48 kHz at
  every corner of {rate ∈ [kMinRate, kMaxRate]} × {octaves ∈ [1,4]} × {depth ∈ {0, 0.5, 1}} × 8 seeds:
  `max |out|` ≤ 1.0 with **zero** non-finite samples (finiteness checked on the IEEE-754 exponent
  field, never `std::isnan` — `-ffast-math` on macOS folds it; idiom at
  `life_modulators_perf_test.cpp:76-80`). At depth 0 the output is exactly 0 at every sample (FR-016).
  **Non-tautology proof, scoped to where it is statistically forced:** at every corner with
  `rate ≥ 0.1 Hz` — i.e. ≥ 30 lattice cells traversed in the 300 s window — `max |out| > 0.5` at
  depth 1.0, **and** the depth-0.5 render's peak is exactly half the depth-1.0 peak within 1e-4, which
  is impossible if a clamp were engaging. The excursion clause is *not* asserted at `kMinRate`: a
  300 s render at 0.005 Hz advances only 1.5 cells, so whether the trajectory exceeds 0.5 depends on
  the two or three gradients that happen to be hashed for that seed. Simulated over 8 seeds this
  session, the minimum peak over 1.5 cells is 0.26–0.36 (a guaranteed intermittent failure), while the
  minimum peak over 30 cells is 0.774 (n = 4) to 1.000 (n = 1) — hence the 0.1 Hz floor on this clause
  only. The bound, finiteness and exact-depth-scaling clauses still run at every corner including
  `kMinRate`.
  Test: `PerlinNoiseSource_NeverExceedsRange`.
- **SC-002 (Perlin smoothness / bounded derivative).** Roadmap line 170 ("Perlin smoothness (bounded
  derivative)"). Rendered per-sample at 48 kHz, never accelerated. At the worst case
  (`rate = kMaxRate`, `octaves = 4`, `depth = 1`), the maximum absolute per-sample delta
  `max |out[n] − out[n−1]|` ≤ **1.0e-3 of the range span** (= 2.0e-3 bipolar), the same threshold the
  Seraphis suite meets (`brownian_drift_test.cpp:233`, `spline_trajectory_test.cpp:237`,
  `orbit_modulator_test.cpp:243`).
  **The analytic cross-check is stated in the measured units** (an earlier draft compared a per-cell
  slope against a per-sample delta, two quantities that differ by `rate/sampleRate` ≈ 1e-4, so the
  inequality held by four orders of magnitude regardless of correctness). Predicted per-sample delta:
  ```
  predicted = kStepResponseGain × kMaxSlope × fbmFactor(n) × depth × rate × kControlRateInterval / sampleRate
      kMaxSlope        = 2.7           (kGradientNormalize 2.0 × per-cell raw max 1.35, FR-017/FR-018)
      fbmFactor(n)     = Σ(aₖ·lₖ)/Σaₖ  ( = 1.000 at n=1, 2.133 at n=4 )
      kStepResponseGain = α / (1 − (1−α)^kControlRateInterval),  α = 1 − exp(−5000/(kOutputSmoothMs·sampleRate))
  ```
  At 48 kHz with `kOutputSmoothMs = 5.0f`: `α = 2.0618e-2`, `kStepResponseGain = 4.237e-2`, so
  `predicted = 3.81e-4` at n = 1 and `8.14e-4` at n = 4 — both inside 2.0e-3 with ≥ 2.5× margin.
  The criterion asserts a **two-sided band**: `0.5 × predicted ≤ measured ≤ predicted`. An
  over-smoothed or frozen implementation fails the lower edge; an unbounded one fails the upper edge.
  The lower edge is 0.5 rather than 1.0 because the closed form is a monotonic-ramp steady state and
  the octaves rarely align: direct simulation this session gives a measured/analytic slope ratio of
  0.998 at n = 1 (max |d out/dx| = 2.694 vs the bound 2.7 — the bound is *attained*) and 0.875 at
  n = 4 (5.041 vs 5.76). Test: `PerlinNoiseSource_MaxSlewBounded`.
- **SC-003 (Perlin spectral rolloff).** Roadmap line 170 ("spectral rolloff tests").
  **Measurement, stated completely** (an earlier draft left the decimation stride and the octave count
  unstated, which made the clauses unevaluable): render 600 s of output at `rate = 0.1 Hz`, depth 1,
  at 48 kHz; take the control-rate trajectory (one point per `kControlRateInterval` = 32 samples,
  1500 Hz) and decimate it by 15 to **100 Hz** (the signal's fastest content at this rate is
  `0.1 × 2³ = 0.8 Hz`, 60× below the resulting 50 Hz Nyquist, so no anti-alias filter is needed);
  60 000 points, **Hann-windowed** (mandatory — a rectangular window's leakage skirt would put energy
  above the band edges that is an artefact of the transform, not of the signal), zero-padded to a
  65 536-point FFT ⇒ 1.53 mHz resolution.
  - **(a) Band-limitation**, asserted at **every** octave count `n ∈ {1,2,3,4}`: ≥ **99 %** of total
    energy lies below `8 × rate`, and no bin above `32 × rate` is within **30 dB** of the peak. This
    is what "band-limited by construction" means operationally. Measured this session (Hann-windowed,
    as specified): fraction below `8 × rate` = 1.0000 / 1.0000 / 0.99999 / 0.99979 for n = 1…4, and
    the loudest bin above `32 × rate` sits −129 / −114 / −97 / −84 dB below the peak. The clause holds
    at n = 4 — the top octave's energy is centred well below its own lattice rate, so an octave-share
    argument that places it *at* `8 × rate` overstates it by ~50×.
  - **(b) Roughness monotonicity**: the fraction of energy above `4 × rate` increases strictly with
    octave count over `n ∈ {1,2,3,4}` (measured 7e-9 → 3.0e-5 → 9.0e-4 → 8.55e-3), and the n = 4
    fraction is ≥ 10× the n = 2 fraction (measured ratio ≈ 285×).
  - **(c) Same measurement repeated at `rate = kMaxRate`** (5 Hz, top octave 40 Hz), asserting the
    same strict monotonicity **plus an absolute floor**: the fraction of energy above `8 × rate`
    (40 Hz) at `n = 4` is ≥ **1.30e-4**. This clause is measured on the **undecimated 1500 Hz control
    trajectory** — the 100 Hz grid clause (a) uses would alias at this rate (50 Hz Nyquist vs a 40 Hz
    top octave), which is why the stride differs here and nowhere else.
    This is the enforcing criterion for FR-018's smoother-sizing argument, and the floor is what gives
    it teeth: the inherited strict monotonicity is preserved by *any* low-pass — measured this session
    as monotonic at 1, 5, 20 **and** 100 ms — so a smoother 20× too slow could not fail the
    monotonicity clause alone. A 20 ms smoother (39.8 Hz cutoff, ≈3.0 dB at 40 Hz) fails the floor;
    the specified 5 ms / 159 Hz smoother attenuates 40 Hz by 0.27 dB and passes it.
  - **(d) Not white, not a sine**: at `n = 1`, the autocorrelation of the decimated trajectory at
    lag `0.1 / rate` (one tenth of a cell) is > **0.7** (measured 0.936 — white noise would give ~0),
    and `|autocorrelation|` at lag `2 / rate` (two cells) is < **0.35** (measured 0.152 — a sine
    would hold near 1). A lag-1 autocorrelation is deliberately **not** used: at the 100 Hz decimated
    rate it is ~0.99999 for any smooth signal and cannot fail.
  Test: `PerlinNoiseSource_SpectralRolloff`.
- **SC-004 (Perlin determinism and position-independence).** Two instances, same seed: 400 captured
  blocks compare bit-identical (`REQUIRE(a == b)`); different seeds differ; `reset()` reproduces the
  first run exactly.
  **Position-independence (FR-012) is asserted with discriminators that can actually fail.** An
  earlier draft compared block sizes 64 and 512 — both exact multiples of `kControlRateInterval` — but
  per FR-003 the control counter is instance state, so control steps land at identical absolute
  sample indices for any such partitioning and a stream-consuming implementation would draw the same
  values in the same order and pass. The replacements:
  - **(a) Non-aligned block sequence:** render 60 s driving the source with the repeating block
    sequence `{37, 1, 64, 512}` and compare per-sample against a pure `process()` render — identical
    within 1e-6. This varies the control-step *phase* relative to block boundaries, which a
    per-block-reseeding or stream-consuming implementation cannot survive.
  - **(b) Cross-rate identity:** the SC-005 comparison doubles as the FR-012 discriminator. A
    stream-consuming implementation draws a different number of gradients per wall-clock second at
    44.1 kHz than at 96 kHz and diverges by O(1); a hash-based one differs only by the control-grid
    offset (SC-005's ~4e-4).
  - **(c) Octave-stream identity (FR-015):** with the same seed and position,
    `getOctaveValue(0)` is bit-identical between a `setOctaves(1)` and a `setOctaves(4)` instance.
    The *outputs* are **not** compared — they legitimately differ by the `1/Σaₖ` normalisation
    (1.0 vs 1.875).
  Test: `PerlinNoiseSource_SeededDeterminism`.
- **SC-005 (Perlin sample-rate invariance).** Configuration is named, because the tolerance depends on
  it entirely: `rate = kDefaultRate` (0.1 Hz), `octaves ∈ {1, 4}`, `depth = 1`, same seed. The same
  120 s wall-clock render at 44.1 kHz and 96 kHz produces the same trajectory shape: per-sample
  comparison after resampling the 44.1 kHz run onto the 96 kHz grid differs by ≤ **1e-3 of the range
  span** (2.0e-3 absolute), and the RMS values differ by ≤ 2 %. Mean is compared with an **absolute**
  range-span tolerance, never relative-to-mean (zero-mean trap).
  **Tolerance justification:** the two control grids are offset by up to one 44.1 kHz control step
  (32/44100 = 0.73 ms); at this configuration the worst output slope is
  `kMaxSlope × fbmFactor(4) × rate = 2.7 × 2.133 × 0.1 = 0.576` per second, so the offset alone
  contributes ≤ 4.2e-4 — a 4.8× margin under the threshold. Rendered per-sample, never accelerated.
  This criterion is **not** run at `kMaxRate`: there the same 0.73 ms offset contributes ~2e-2, 10×
  the tolerance, and the criterion would fail on a correct implementation. If a fast-corner check is
  wanted it must compare at aligned control-step boundaries, not per audio sample.
  Test: `PerlinNoiseSource_SampleRateInvariant`.
- **SC-006 (Aizawa boundedness, non-divergence, chaotic character).** Over a 1 h accelerated render at
  every speed in `[kMinSpeed, kMaxSpeed]` (`chaos_mod_source.h:37,38`):
  - **(a) At coupling 0** — output ∈ [-1,+1], zero non-finite samples, and
    `getDivergenceResetCount() == 0` (FR-036; the guard must never fire — an attractor that needs
    rescuing is mis-tuned). Simulated this session at `baseDt_ = 5.0e-4`: `max |state| ≤ 1.94` at
    every speed, guard threshold 250, zero resets.
  - **(b) Non-tautology proof at coupling 0** — the output goes through
    `std::clamp(std::tanh(...), -1, 1)` (`chaos_mod_source.h:236`), and `tanh` alone bounds *any*
    finite input, so "output ∈ [-1,+1]" passes even for a mis-tuned attractor pinned at |x| = 40 or a
    dead one pinned at 0. The criterion therefore asserts a real, moderate excursion:
    **`0.5 < max |out| < 0.99`** and **`stddev(out) > 0.1`**. Measured: `max |out|` = 0.750–0.780
    (`kAizawaScale = 1.5`, x-extent ≈ 1.5 ⇒ `tanh(1.0) = 0.76`) and `stddev` = 0.26–0.32. This clause
    is what catches the `dt ≥ 0.02` fixed-point collapse documented in FR-033, where the output is
    identically 0 while every bound and finiteness clause still passes.
  - **(c) Chaotic character at coupling 0** — the autocorrelation of the trajectory falls below 1/e
    within **60 s** of wall clock at every speed (measured worst case 8.9 s at `kMinSpeed`, 0.023 s at
    `kMaxSpeed`), ruling out a slow LFO or a frozen output; **and** sensitive dependence at
    `speed = kMaxSpeed`: two instances whose initial `x` differs by 1e-4 reach an RMS output
    difference > 0.1 within 60 s (measured 0.21 by 10 s) — the defining property of chaos, and
    impossible for any periodic source.
    *No "autocorrelation never exceeds 0.6 beyond the decorrelation lag" clause is asserted, because
    it is false for a correct Aizawa:* the attractor's orbit has a near-regular period with
    chaotically varying lobe amplitude, and the measured maximum autocorrelation in the band
    [2×, 40×] the decorrelation lag is 0.68–0.98 across the speed range. Sensitive dependence is the
    honest non-periodicity discriminator.
  - **(d) At coupling 1.0 with a full-scale input** — output ∈ [-1,+1], zero non-finite samples, and
    `getDivergenceResetCount() == 0`. The state is **not** required to stay on the attractor or within
    `safeBound_`: the coupling path adds a fixed `coupling_ * inputLevel_ * 0.1f` to `state_.x` every
    control step (`chaos_mod_source.h:214-216`, `setInputLevel` unclamped at `:119-121`) and
    legitimately drives `|state|` to ≈ 112 at `kMinSpeed`. FR-033's `safeBound_ = 25.0f` (guard
    threshold 250) is sized for exactly that; with the Chua-style `safeBound_ = 5.0f` the guard fires
    ~2000 times per 600 s render, which is why that constant is not reused. Clauses (b) and (c) are
    **not** asserted at coupling 1 — a DC-biased input legitimately saturates the output.
  Test: `ChaosModSource_AizawaBoundedAndChaotic`.
- **SC-007 (Aizawa causes no regression).** `dsp_primitives_tests` and `dsp_processors_tests` pass
  with **zero** new failures; specifically `chaos_waveshaper_test.cpp` (enum indices `:33-36`,
  underlying type `:40`, invalid-enum fallback `:579-583`) passes **unmodified**.
  **Zero new compiler warnings, measured by a method that can actually observe warnings.** An earlier
  draft named `node tools/check-portability.js` and Catch2 summary lines; both were verified this
  session to be structurally incapable of catching this: the script invokes
  `g++ -std=c++20 -fsyntax-only -DNDEBUG -DRELEASE` with **no warning flags and no `-Werror`**
  (`tools/check-portability.js:230-231`), so `-Wswitch` is never emitted and could not fail it, and
  its `isCheckable()` accepts only `.cpp|.cc` (`:203-208`, comment: "a header alone has no TU to
  compile") while the FR-034 change lives entirely in headers. Catch2 summary lines report test
  results, not compiler diagnostics. The replacement method:
  - **(a)** a dedicated TU that `#include`s both `chaos_waveshaper.h` and `chaos_mod_source.h` is
    compiled under WSL with **`g++ -std=c++20 -Wall -Wextra -Wswitch -Werror -fsyntax-only`** and
    again with `clang++` under the same flags; both must exit 0. This is the clause that binds
    FR-034's exhaustive `case ChaosModel::Aizawa:` arms.
  - **(b)** the MSVC leg is a **build-log diff**: `cmake --build … --target dsp_primitives_tests
    dsp_processors_tests 2>&1 | tee` before and after the change, diffed for new warning lines
    (`C####`), with zero additions required. The GCC/Clang CI legs provide the same evidence on
    those toolchains.
  `node tools/check-portability.js` still runs as part of SC-016's repo gates — it is just not the
  thing that proves warning-cleanliness.
  Test: `ChaosModSource_AizawaNoRegression` plus the two out-of-suite compile commands above.
- **SC-008 (Scheduler inter-event-time distribution).** Roadmap line 171 ("inter-event-time
  distribution tests (seeded, statistical bounds)"). Across 32 seeds × 500 events each at the default
  20–90 s range, using accelerated rendering (`processBlock(32)`), with the histogram built from
  `getPeriodSeconds()` (FR-058) sampled at each event onset:
  (a) **every** drawn period ∈ [20, 90] s — zero violations;
  (b) sample mean ∈ 55 ± 2 s (uniform mean 55, standard error ≈ 20.2/√16000 ≈ 0.16 s, so ±2 s is a
  >12σ band and cannot go flaky);
  (c) a 10-bin histogram over the range is flat within ±15 % of expected count per bin (uniformity,
  per FR-052);
  (d) **onset-to-onset cadence matches the drawn period**: with `setIntervalRange(30, 30)` the
  measured interval between consecutive `isEventActive()` rising edges is 30 s ± one control step,
  and `getPeriodSeconds()` returns exactly 30.0 s at every onset. Per FR-054 the drawn quantity *is*
  the cadence, so these two are the same number — that identity is the point of the clause. (Under
  the rejected end-of-release semantics the same measurement would read 30 + attack + hold + release,
  and this clause would fail on a correct implementation.)
  Test: `SlowEventScheduler_IntervalDistribution`.
- **SC-009 (Event envelope C1 continuity — no clicks).** Roadmap line 171. Rendered per-sample at
  48 kHz, never accelerated, at two configurations: the shortest legal segments
  (`setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)` with
  `setIntervalRange(1, 1)`), and the FR-055 defaults; ≥ 10 events each.
  **The second difference is formed on a decimated grid, copying
  `spline_trajectory_test.cpp:252-256, 265-268, 312-315` completely rather than partially.** A
  *per-sample* second difference is unmeasurable here: for FR-056's smootherstep the peak per-sample
  second difference is `(10/√3)·(dt/T)²` — 1.17× the raised cosine's `0.5·π²·(dt/T)²` — which at the
  default 5 s attack at 48 kHz is 1.0e-8 and at `kMinSegmentSeconds = 0.05` is 1.0e-6 — at or below the float32 noise floor (~1.2e-7 at amplitude 1),
  i.e. the earlier `> 1e-9` interior guard compared noise to noise. The grid is therefore
  `stride = round(min(effectiveAttack, effectiveHold, effectiveRelease) × sampleRate / 100)` — **100
  points per shortest segment**, the same construction as the precedent's `kStride = 240`; the stride
  is reported by the test. With `h = stride·dt` the interior curvature term is
  `0.5·π²·(1/100)² ≈ 4.9e-4` in the shortest segment, ~4000× above the float noise floor. Assertions,
  with the precedent's thresholds:
  - `interiorMax > 1.0e-5` (the precedent's value at `spline_trajectory_test.cpp:314`) — proves real
    curvature is being measured, not quantization noise;
  - `joinMax ≤ 5.0 × interiorMax` — no join spike (a C0-but-not-C1 join is `O(h·Δslope)`, one order
    larger in `h`, and is rejected);
  - `max |out[n] − out[n−1]| ≤ 1.0e-3` of the range span at default segment times.
  A C0-but-not-C1 implementation fails the ratio clause.
  Test: `SlowEventScheduler_EnvelopeC1AtJoins`.
- **SC-010 (Scheduler determinism).** Same seed ⇒ bit-identical event streams over 400 captured
  blocks, including the target-id and polarity sequences (compared as vectors); different seeds ⇒
  different streams; `reset()` reproduces the post-`prepare` stream exactly (FR-061); `setSeed()`
  mid-event does not alter the in-flight event's remaining samples (FR-062).
  Test: `SlowEventScheduler_SeededDeterminism`.
- **SC-011 (Scheduler output boundedness).** Over 2 h of accelerated rendering (`processBlock(4096)`)
  across the parameter corners (interval range extremes, segment-time extremes, depth range extremes,
  bipolar probability 0/0.5/1, target count 1/16, 8 seeds): output ∈ [-1, +1], zero non-finite
  samples, `getActiveTarget()` ∈ `[0, targetCount) ∪ {kNoTarget}` at every sample, and
  `getSourceRange() == {-1.f, +1.f}` at every setting (FR-006).
  **Non-tautology proof, stated precisely** (an earlier draft's "half-max-depth run" was ambiguous and
  false on the literal reading, because FR-060 draws depth from `[minDepth, maxDepth]` and halving
  only `maxDepth` does not halve the draw): with `setDepthRange(0.15f, 0.5f)` versus
  `setDepthRange(0.3f, 1.0f)`, same seed, the peak ratio is **0.5 within 1e-4**. This holds exactly
  because FR-060 fixes the draw order and consumes one `nextUnipolar()` for depth in both runs, so the
  RNG sequences stay aligned and each event's depth is `0.3 + u·0.7` versus exactly half of it.
  Test: `SlowEventScheduler_BoundedOverLongRun`.
- **SC-012 (Scheduler sample-rate invariance).** Rendered per-sample (or with `processBlock(32)`),
  never at a reduced sample rate. Event onsets measured in wall-clock seconds at 44.1 kHz and 96 kHz
  for the same seed agree within **one control step** (32/44100 ≈ 0.73 ms) for the first 50 events —
  as a **cumulative** bound on onset position, not a per-event bound — and the event count over a
  fixed 30 min wall-clock window is identical. This bound is attainable only because FR-064 mandates a
  non-accumulating timeline; without it the ~200 transitions in 50 events would drift by up to ~79 ms.
  Test: `SlowEventScheduler_SampleRateInvariant`.
- **SC-013 (RT safety — zero allocations).** With the allocation-tracking harness
  (`tests/test_helpers/allocation_detector.h`; note `brownian_drift_test.cpp` remains the single TU
  owning the global operator replacements in `dsp_processors_tests` —
  `life_modulators_perf_test.cpp:19-23`), after `prepare` and an untracked warm-up, the tracked window
  is 500 × `processBlock(512)` + 4096 × `process()` + 40 × `processBlock(48'000)` per component, and
  yields **exactly 0** allocations for `PerlinNoiseSource`, `SlowEventScheduler` and `ChaosModSource`
  in Aizawa mode.
  **The scheduler's configuration is pinned inside the criterion**, because the default configuration
  makes the window vacuous: 500 × 512 + 4096 = 260 096 samples = 5.42 s at 48 kHz, while the default
  minimum period is 20 s (FR-052), so the scheduler would sit idle for the entire tracked run and an
  allocation in the draw path (FR-052/059/060), in any state transition (FR-054), or in the
  multi-transition bounded loop (FR-063) would pass unnoticed. The pinned configuration is
  `setIntervalRange(kMinIntervalSeconds, kMinIntervalSeconds)` (1 s) with
  `setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)`, and the
  40 × `processBlock(48'000)` calls exercise FR-063's multi-event bounded loop directly.
  The criterion additionally asserts **≥ 20 `isEventActive()` rising edges within the tracked window**
  (measured by counting edges), so a future change to the defaults or the constants cannot silently
  re-empty it. Tests: `PerlinNoiseSource_NoAllocInProcess`, `SlowEventScheduler_NoAllocInProcess`.
- **SC-014 (Control-rate cost).** Advancing four `PerlinNoiseSource` instances (4 octaves each) plus
  four `SlowEventScheduler` instances plus one Aizawa `ChaosModSource` once per 512-sample block is
  measured in **ns per block** and gated against a checked-in baseline at `≤ baseline × 1.5` — the
  reproducible basis established at `life_modulators_perf_test.cpp:9-72`.
  **The schedulers are configured for cycle coverage, and the gate is on the cycle-inclusive number.**
  At the FR-052/FR-055 defaults four schedulers sit in Idle for the whole measurement (one 512-sample
  block is 10.7 ms; the minimum period is 20 s), so the draws, the four transitions and the
  envelope evaluation would never appear in the measurement. The schedulers are therefore set to
  `setIntervalRange(kMinIntervalSeconds, kMinIntervalSeconds)` with
  `setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)`, giving ≥ 1 full
  cycle per ~100 measured blocks. **Two numbers are reported** — idle-path ns/block (defaults) and
  cycle-inclusive ns/block — and the regression gate applies to the **cycle-inclusive** figure.
  **The absolute reference is a constraint on the baseline, not a free-floating report.** SC-014
  commits to **≤ 0.1 % of the 512-samples-at-48 kHz block budget**, i.e. 0.1 % of 10.667 ms =
  **10 667 ns per block**, and requires — as the precedent does explicitly at
  `life_modulators_perf_test.cpp:60-66` — that **`kBaselineNsPerBlock × kRegressionFactor ≤
  kReferenceNsPerBlock`**, i.e. the checked-in baseline may not exceed **7111 ns**. Without that
  constraint the gate is self-referential and any measured cost becomes acceptable. The absolute
  figure itself stays *reported* rather than *asserted* on arbitrary runners (percent-of-core is not
  reproducible), but the baseline constant is what CI enforces and it is bounded.
  **Why 0.1 % where the Seraphis precedent used 0.05 %:** the workload is 9 instances performing
  ~256 gradient-octave evaluations plus 64 scheduler control steps plus 16 Aizawa integration steps
  per block, against the precedent's 6 single-layer modulators (~32 control steps of simple math) —
  roughly 5× the control-rate work for 2× the percentage allowance, i.e. a tightening in
  work-normalised terms, not a loosening. This threshold is spec-set: the roadmap specifies no CPU
  budget for Phase 1 (per-voice budgets begin at Phase 2, roadmap line 195).
  Test: `VoragoPhase1_ControlRateCost`.
- **SC-015 (Offline evaluation harness).** Running `dsp_processors_tests.exe "[.harness]"` exits 0 and
  produces **exactly the five files named in FR-081**, checked by name. Each has a header line, parses
  as numeric, and covers its stated duration (60 s for the three Perlin files and the Aizawa file,
  900 s for the scheduler file). **Content assertion, not just row count:**
  `vorago_p1_slow_events.csv` contains **≥ 3 `isEventActive()` rising edges** (detected as
  `phase` transitioning out of idle), and at least one row with `target != kNoTarget`. Running the
  default suite produces **no** files (FR-082). Test: `VoragoPhase1_TrajectoryHarness`.
- **SC-016 (Repo gates).** `node tools/check-portability.js`, `node tools/lint-layers.js`,
  `node tools/lint-odr.js`, `node tools/lint-nonfinite-symbols.js` and
  `node tools/lint-float-bit-goldens.js` all exit 0; `./tools/run-clang-tidy.ps1 -Target dsp` reports
  no new diagnostics. No test in this phase pins a float render with a bit-exact digest (project
  rule); every render assertion uses a measured tolerance.
- **SC-017 (Long-run numerical resolution — the enforcing criterion for FR-007).** 8 h accelerated
  render (`processBlock(4096)`) at 48 kHz of `PerlinNoiseSource` (at `kDefaultRate` and at `kMaxRate`,
  4 octaves) and of `SlowEventScheduler` (defaults). Assertions:
  (a) **`PerlinNoiseSource` only** — the RMS of the last **1 h** is within **20 %** of the RMS of the
  first 1 h;
  (b) **`PerlinNoiseSource` only** — the zero-crossing rate of the last 1 h is within **20 %** of the
  first 1 h;
  (c) **zero** non-finite samples across the whole render (IEEE-754 exponent-field check), both
  sources;
  (d) the scheduler's event count in the last hour is within **20 %** of its count in the first hour,
  with `eventsHour1 ≥ 10` as the anti-vacuity guard, and `getPeriodSeconds()` still returns values
  inside `[20, 90]` s at the end;
  (e) the scheduler's RMS over the final 900 s is **> 0** (liveness).
  **Why the windows are 1 h and why clauses (a)/(b) exclude the scheduler** (plan §8 item 9; simulated
  this session against the real `deriveStreamSeed`/`Xorshift32`, `random.h:50-68,102-111`, 8 h at
  48 kHz): at 60 s windows *this criterion fails on a correct implementation*. 60 s at `kDefaultRate`
  is 6 lattice cells and 11–21 zero crossings, so both statistics are sampling noise — over 8 seeds the
  RMS deviation reaches **30.7 %** (seed `0x9E37`: 0.3311 → 0.2294) and the ZCR deviation **100 %**
  (seed `0x51E7`: 11 → 22), failing 5 of 8 seeds. At 1 h windows (360 cells) the worst case over the
  same seeds is RMS **5.3 %** / ZCR **7.7 %** — a 2.6× margin — and at `kMaxRate` ≤ 0.6 % / 0.7 %.
  **Only the window changes; the 20 % thresholds are untouched, and a frozen accumulator still drives
  both statistics to 0.** For the scheduler these two statistics are not merely noisy but *undefined*:
  its output is 0 except during an event, and the FR-067 pre-roll (drawn from 20–90 s) exceeds a 60 s
  window for 3 of 8 seeds, making `rmsFirst` exactly 0 so clause (a) divides by zero (`0x3039`:
  74.39 s; `0xBEEF`: 84.66 s; `0xABCD`: 70.96 s); even at 900 s windows the ZCR swings 22 → 6 and
  12 → 26. Clauses (d) and (e) replace them and are **not weaker**: the event count is the direct
  FR-007 observable (measured hour-1 vs hour-8 deviation 1.5–6.9 % over 8 seeds against the unchanged
  20 % bound), and the liveness clause is exactly the "scheduler stops firing" failure this criterion
  names.
  The failure mode this exists to catch is a `float` accumulator whose ULP grows past the per-step
  increment: the modulator silently freezes (RMS → 0, zero crossings → 0) or the scheduler stops
  firing, while every other criterion in this phase still passes. No other criterion runs long enough
  to expose it — SC-001 renders 300 s, SC-006 1 h, SC-011 2 h. This was an Edge Case with no numbered
  criterion in an earlier draft, i.e. outside the compliance surface — precisely the failure the Scope
  section cites for the Seraphis Phase 1 harness.
  Test: `VoragoPhase1_LongRunResolution` (tagged `[long]`: > 15 s, and its assertions are
  toolchain-independent).
- **SC-018 (Setter-storm continuity — Clarifications Q3).** With a `SlowEventScheduler` mid-event
  (any segment), call every setter in the FR-066 surface — `setEnvelopeTimes`, `setIntervalRange`,
  `setDepthRange`, `setBipolarProbability`, `setTargetCount` — with a new, deliberately different
  value once per control step for the remainder of the event (a "setter storm"), then let the event
  complete naturally. The per-sample delta `max |out[n] − out[n−1]|` never exceeds SC-009's
  1.0e-3-of-range-span slew bound at any point during the storm, including at the segment/join
  boundaries the storm crosses. This is possible only if every setter latches per FR-066; an
  implementation that re-fits or re-draws mid-event fails this bound at the instant the change is
  applied.
  Test: `SlowEventScheduler_SetterStormContinuity`.

## Edge Cases

**Real-time-safety boundaries**
- `processBlock(0)` — no-op for every component; no state advance, no draw.
- `processBlock(10'000'000)` (≈ 3.5 min in one call) — must resolve every state transition it spans
  in a bounded loop with zero allocation (FR-063); asserted finite, in-range, and with the correct
  number of events elapsed. Directly exercised by SC-013's 40 × `processBlock(48'000)` calls.
- Advance methods called before `prepare()` — must not crash and must not produce non-finite output;
  the post-construction state is well-defined by FR-019 (`kDefaultRate`, `kDefaultOctaves`,
  `kDefaultDepth`, `kDefaultPerlinSeed`) and, for the scheduler, by FR-052/FR-055's defaults with the
  state machine idle.
- `prepare()` called twice, and called while an event is active — the second call re-derives
  coefficients and re-initialises state; it must not leave a half-completed envelope segment that
  jumps.

**Parameter extremes**
- `setRate(0)` / `setRate(1e9)` / negative / non-finite input → clamped to `[kMinRate, kMaxRate]`.
- `setOctaves(0)` / `setOctaves(99)` → clamped to `[1, 4]`.
- `setDepth(0)` → output exactly 0 for all time; `setDepth(1)` → SC-001's non-tautology proof applies.
- `setIntervalRange(90, 20)` (inverted) → collapses to the fixed **90 s** period, still valid. FR-052
  says that if `max < min` after clamping, both collapse to `min` — and `min` here is the *first*
  argument (90), so the inverted call raises `hi` to `lo`. (An earlier draft of this line said 20 s,
  contradicting FR-052 in the same document.)
- `setIntervalRange(0, 0)` → clamped to `kMinIntervalSeconds` (1 s); FR-055's fit rule then scales the
  envelope to fit inside 1 s, the scheduler fires back-to-back events, and it must remain bounded,
  C1 and allocation-free (this is SC-013's and SC-014's pinned configuration).
- `setEnvelopeTimes(0, 0, 0)` → clamped to `kMinSegmentSeconds` each; C1 must still hold (SC-009 runs
  this configuration explicitly).
- `setEnvelopeTimes(300, 300, 300)` with the default 20–90 s range → the FR-055 fit rule scales all
  three by `20 / 900`, giving 6.67 s each; `getEffectiveAttackSeconds()` etc. report the scaled values
  and the cadence stays at the drawn 20–90 s. Calling `setIntervalRange(600, 600)` afterwards
  re-evaluates the fit and restores the segments toward their configured 300 s (capped by
  `600 / 900`). Order-independence of the two setters is asserted.
- `setTargetCount(0)` → clamped to 1; `getActiveTarget()` then always returns 0 while active.
- `setBipolarProbability(0)` → every event positive; `1` → every event negative; both must satisfy
  SC-011's bound.
- Aizawa at `kMinSpeed` and `kMaxSpeed` (`chaos_mod_source.h:37,38`) with coupling 1.0 and a
  full-scale input level — the coupling perturbation `state_.x += coupling_ * inputLevel_ * 0.1f`
  drives the state **off the attractor and well outside `safeBound_`** (simulated worst case
  |state| ≈ 112 at `kMinSpeed`, versus `safeBound_ = 25`). That is expected and legal: the requirement
  (SC-006(d)) is that the state stays below the **guard threshold** `safeBound_ * 10 = 250`, that
  `getDivergenceResetCount()` stays 0, and that the output stays finite and inside [-1,+1]. It is
  **not** required to stay within `safeBound_`; asserting that would fail on a correct implementation.

**Sample-rate changes**
- `prepare(44100)` → run → `prepare(96000)` mid-flight (Clarifications Q4): `prepare()` is a full
  re-initialisation (FR-002) — it does **not** preserve the in-flight event timeline or the Perlin
  position; both are reset exactly as a fresh `prepare()`/`reset()` would. What *is* preserved across
  a sample-rate change is that periods and segment times are expressed in seconds (FR-052, FR-055),
  so two **independent** runs at 44.1 kHz and 96 kHz from the same seed produce the same wall-clock
  timeline — this is exactly what SC-012 measures (two separate renders, not one render that switches
  rate mid-flight). Event onsets between the two independent runs agree within one control step
  (SC-012).
- `prepare(1.0)` and `prepare(0.0)`/negative: the 1 Hz floor (`brownian_drift.h:122` precedent)
  guarantees `controlDt` stays finite; assert no non-finite output.
- Block size changing every call — the repeating sequence `{37, 1, 64, 512}` (deliberately *not* all
  multiples of `kControlRateInterval`, which would not discriminate; see SC-004) must reproduce a pure
  `process()` render within 1e-6 for Perlin (SC-004(a)), and the scheduler's event onsets must land
  within one control step of the `process()`-only run.

**Seed determinism**
- `setSeed(0)` — `Xorshift32::seed()` silently substitutes `kDefaultSeed = 2463534242u`
  (`random.h:73-74`, definition `:85`). Both components must document that seed 0 is a valid alias for
  that stream and is **not** an error; two components seeded 0 and 2463534242 produce identical
  streams.
- Two `PerlinNoiseSource` instances with adjacent seeds (`n`, `n+1`) must not be visibly correlated:
  their cross-correlation over 300 s is < 0.2. `deriveStreamSeed` (`random.h:102`) exists precisely to
  avoid the low-bit correlation of raw adjacent seeds.
- Two schedulers seeded identically fire identically — the caller (Phase 10) is responsible for
  spreading per-voice seeds; this phase only guarantees that distinct seeds give distinct streams
  (asserted for 32 seed pairs).
- Determinism is asserted as **exact equality of captured float vectors within one build** (the
  Seraphis precedent), never as a cross-toolchain golden digest.

**Long-run numerics**
- 8 h render (accelerated) — the Perlin position accumulator and the scheduler elapsed counter must
  not lose resolution (FR-007). Failure mode to guard against: a `float` accumulator whose ULP
  exceeds the per-step increment silently freezes the modulator. **This is SC-017**, a numbered
  criterion on the compliance surface, not an unenforced note.

## Review notes

Findings from the review pass that were **not** applied as filed, with the evidence:

- **SC-003 clause (a) is *not* unsatisfiable at 4 octaves — issue rejected on measurement.** Two
  review issues (one filed under fidelity, one under testability) argued that with `kPersistence = 0.5`
  and `kLacunarity = 2.0` the 4-octave configuration places its top octave *at* `8 × rate` carrying
  ~1.2 % of total variance, so "≥ 99 % of energy below `8 × rate`" fails on a correct implementation.
  That reasoning assumes each octave's energy sits *at* its lattice rate. It does not: 1D gradient
  noise has its spectral peak well below its lattice rate and rolls off steeply above it. Simulated
  this session (±1 gradients, smootherstep, Hann-windowed, 600 s at `rate = 0.1 Hz`, decimated to
  100 Hz, 65 536-point FFT), the fraction of energy below `8 × rate` is **1.0000 / 1.0000 / 0.99999 /
  0.99979** for n = 1…4 — the clause passes at every octave count with margin. Clause (a) is therefore
  kept at its stated threshold and asserted at **every** n rather than being scoped to n = 1 or
  relaxed to a `16 × rate` band edge. The genuine defects those two issues also identified — the
  unstated decimation stride, the unstated FFT/window, the vacuous lag-1 autocorrelation clause, and
  the missing `kMaxRate` repetition — **were** applied, and the windowing requirement is now explicit
  because a rectangular window's leakage would break the 99 % figure for transform reasons rather
  than signal reasons.

Defects found while verifying the review that were **not** in the review, and are now fixed in the
requirements above:

- **FR-033's `baseDt_ = 0.01f` would have shipped a dead Aizawa.** With `kMaxSpeed = 20`
  (`chaos_mod_source.h:38`) the effective step reaches `dt = 0.2`. Forward-Euler Aizawa was simulated
  across `dt ∈ [5e-4, 0.2]` from four initial states this session: for `dt ≥ 0.02` it collapses onto
  the `x = y = 0` fixed point (z ≈ −1.105) and the output is **identically zero** — silently, with no
  divergence and no guard reset — across most of the speed range. `baseDt_` is now `5.0e-4f` and
  SC-006(b) carries the `stddev(out) > 0.1` / `0.5 < max|out| < 0.99` clause that catches this class
  of failure.
- **SC-006's "no autocorrelation peak above 0.6 beyond the decorrelation lag" clause was false for a
  correct Aizawa** and has been replaced by a sensitive-dependence clause. Measured maximum
  autocorrelation in the band [2×, 40×] the decorrelation lag is 0.68–0.98 across the speed range,
  because the attractor orbits with a near-regular period while the lobe amplitude varies chaotically.
- **FR-033's `safeBound_ = 5.0f` (the Chua value) makes the divergence guard fire under legal
  coupling.** At `coupling = 1` with a full-scale DC-biased input the guard fired ~2000 times per
  600 s render at `kMinSpeed`. `safeBound_` is now `25.0f` (guard threshold 250) against a simulated
  worst-case `|state| ≈ 112`, and the corresponding Edge Case now states the true expectation.
