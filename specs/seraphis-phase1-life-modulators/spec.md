# Feature Specification: Seraphis Phase 1 — Life Modulator Suite

**Spec slug:** `seraphis-phase1-life-modulators`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 1 (lines 103–132)
**Layer:** all new components are Layer 2 (`dsp/include/krate/dsp/processors/`)
**Plugin work:** none (KrateDSP-only, unit-tested; the Seraphis plugin starts at Phase 8)

## Overview

The Life Modulator Suite is Seraphis's identity layer: a family of slow, autonomous, *bounded*
modulation sources that replace ordinary LFOs. Every downstream Seraphis subsystem (harmonic cloud,
continuous body, atmosphere, aether space, per-voice spatial position) consumes these sources, so they
ship first. This phase adds six new Layer 2 processors — `BrownianDrift`, `BreathingModulator`,
`TidalModulator`, `OrbitModulator`, `SplineTrajectory`, `GrowthEnvelope` — each of which implements the
existing `Krate::DSP::ModulationSource` abstract interface (`dsp/include/krate/dsp/core/modulation_source.h:31`),
is real-time-safe and allocation-free, is deterministic under a seeded `Xorshift32`
(`dsp/include/krate/dsp/core/random.h:40`), and is evaluated per block with per-sample smoothing via the
existing `OnePoleSmoother` / `SlewLimiter` primitives (`dsp/include/krate/dsp/primitives/smoother.h:134,468`).
The design intent (roadmap "Entropy, not chaos", line 78) is that all motion reads as *organic drift* —
never white noise, never a periodic LFO. This is a foundations phase: it delivers reusable DSP building
blocks with unit tests and offline trajectory evaluation, not audible synthesis.

## Clarifications

### Session 2026-07-24

- **Q1 — Per-modulator output polarity.** Uniform bipolar: all six modulators report
  `getSourceRange() = [-1,+1]` EXCEPT `GrowthEnvelope`, which is unipolar `[0,1]`. Consumers remap where
  needed. Matches the `RandomSource` precedent.
- **Q2 — `getSourceRange()` vs the depth control.** `getSourceRange()` is FIXED at polarity full-scale
  (±1; Growth 0..1) regardless of settings; depth scales the signal internally within that range. No
  double-attenuation; SC-001 asserts against the fixed full-scale bounds.
- **Q3 — `TidalModulator` internal structure.** Fixed 3 layers, fixed irrational-ratio constants
  (golden-ratio / √2 / √3 style), one rate scalar. A "sine pair" = two slightly detuned sines that beat.
  Seed-independent and analytically never-repeating; SC-003(b) predicts the period from the rate scalar
  alone.
- **Q4 — `SplineTrajectory` waypoint count and spacing.** Fixed N=4 waypoint ring (Catmull-Rom minimum) +
  `setWaypointInterval(seconds)`, range ~0.5–30 s. This is the spacing knob SC-002/SC-003(a) assume.
- **Q5 — `OrbitModulator` rate range, coupling model, and x/y projection.** Kuramoto-style
  phase-difference coupling between two phase oscillators, rate range 0.01–0.5 Hz; `x = depth·sin(φ₁)` via
  `getCurrentValue()`, `y = depth·sin(φ₂)` via `getY()`; decay/growth = clamped radius envelope, neutral
  setting sustains the orbit. Bounded by construction; SC-002 worst case = max rate + max coupling.
- **Q6 — `BreathingModulator` asymmetry.** Fixed physiologically-inspired asymmetric shape (~40% inhale /
  60% exhale, distinct rise/fall curvature), hardcoded. Control surface stays exactly {rate, depth,
  irregularity}. FR-021 tests rise time ≠ fall time and a non-sinusoidal shape.
- **Q7 — `GrowthEnvelope` trigger API, idle output, and fall.** `trigger()` starts the rise (a mid-rise
  trigger continues, never restarts); `getCurrentValue()` reads bottom-of-range before the first trigger;
  holds top-of-range after completion; `trigger()` after completion is a no-op; NO fall/release segment.
- **OQ1 — Router integration scope (Open Question 1).** Phase 1 is ABC-only: each modulator implements the
  `ModulationSource` ABC + contract tests. `ModulationEngine` / `VoiceModRouter` enum slots and route
  wiring are DEFERRED to Phase 7. No router changes in Phase 1.
- **OQ2 — Slew (SC-002) and CPU (SC-007) thresholds (Open Question 2).** Both spec-set thresholds accepted
  as binding: SC-002 slew ≤ 1.0e-3 of full range per sample at each modulator's worst-case config; SC-007
  combined control-rate CPU of all six ≤ 0.05% of one core @48 kHz, measured as informational vs a
  checked-in baseline.
- **OQ3 — `reset()` RNG semantics (Open Question 3).** `reset()` rewinds the `Xorshift32` stream to the
  original seed: `reset()` restores exact post-`prepare` state, so the same seed → bit-identical trajectory
  after every reset. SC-004 asserts this.

## Scope

In scope for this phase:

- Six new Layer 2 modulation-source processors, exactly as enumerated in the roadmap (lines 111–122).
- Each conforms to the `ModulationSource` interface and the shared contract in roadmap lines 124–126
  (RT-safe, allocation-free, per-block evaluation, per-sample `Smoother`, seeded determinism).
- Unit tests for the four roadmap success-criteria families (boundedness, smoothness, statistical
  character, determinism) plus RT-safety and sample-rate-change behaviour.
- An offline evaluation harness that renders 60 s trajectories to CSV for visual inspection
  (roadmap "Evaluation", lines 131–132).

## Non-Goals (owned by later phases)

- **Consuming these modulators / audible sound.** No harmonic cloud, body, atmosphere, or aether uses
  them yet — that begins Phase 2 (`seraphis-phase2-harmonic-cloud`) and later.
- **Routing-engine integration.** Wiring these sources into `ModulationEngine`'s fixed `ModSource` enum
  dispatch (`dsp/include/krate/dsp/systems/modulation_engine.h:659`) or `VoiceModRouter`'s
  `VoiceModSource` enum (defined in `dsp/include/krate/dsp/systems/voice_mod_types.h:29`, indexed by
  `computeOffsets` at `voice_mod_router.h:162-168`) is **not** done here.
  Per Clarifications OQ1, those routers do not route via the `ModulationSource` interface polymorphically,
  so any enum/route additions belong to Phase 7 (`seraphis-phase7-voice-engine`). This phase only
  guarantees each modulator *implements* the interface.
- **Per-voice seed spreading.** The "no two voices drift identically" unified seed spread is Phase 7
  (roadmap line 285). This phase requires only that each modulator is individually seedable/deterministic.
- **Any plugin, parameter ID, UI, or preset work** (Phases 8–12).
- **The spectral-evolution `EntropyProcessor`** — a Phase 3 component (roadmap line 87, 180), distinct
  from the Life Modulators despite the shared "entropy" vocabulary.

## Functional Requirements

Each requirement is testable and traces to a specific roadmap statement.

### Shared contract (all six modulators)

- **FR-001** — Every new modulator publicly derives from `Krate::DSP::ModulationSource` and overrides
  `getCurrentValue() const noexcept` and `getSourceRange() const noexcept`
  (`dsp/include/krate/dsp/core/modulation_source.h:37,41`). Trace: roadmap line 124.
- **FR-002** — Every modulator is real-time-safe: all processing methods are `noexcept` and perform no
  heap allocation, locks, exceptions, or I/O. All buffers/waypoint storage are fixed-size members sized
  before or at `prepare()`. Trace: roadmap line 126 and Cross-Cutting Constraints (line 479).
- **FR-003** — Every modulator exposes a control-rate `processBlock(size_t numSamples) noexcept` that
  advances internal state by a full block and leaves `getCurrentValue()` returning the smoothed output,
  mirroring the existing `RandomSource::processBlock` / `ChaosModSource::processBlock` pattern
  (`random_source.h:66`, `chaos_mod_source.h:80`). Per-sample smoothing uses `OnePoleSmoother` or
  `SlewLimiter` so block-boundary steps are inaudible. Trace: roadmap line 125–126.
- **FR-004** — Every modulator exposes `prepare(double sampleRate) noexcept` and `reset() noexcept`,
  matching the lifecycle of existing Layer 2 sources (`random_source.h:45,53`). After `prepare`, output
  is well-defined without any prior `processBlock` call.
- **FR-005** — Every stochastic modulator (`BrownianDrift`, `BreathingModulator` irregularity,
  `TidalModulator` phase seeding, `OrbitModulator` initial state, `SplineTrajectory` waypoints) is driven
  by an owned `Xorshift32` seeded via an explicit `setSeed(uint32_t)` / constructor seed, using only
  `nextFloat()`/`nextUnipolar()`/`seed()` (`random.h:58,66,72`). Given the same seed and call sequence the
  output is bit-reproducible within one build. Trace: roadmap "determinism with seeded `random.h`" (line 130).
- **FR-006** — Every modulator has a depth/amount control that scales its output **internally**.
  `getSourceRange()` is **fixed at polarity full-scale regardless of settings** — `[-1,+1]` for all
  modulators except `GrowthEnvelope`, which is unipolar `[0,1]` — and does **not** shrink with the depth
  setting; depth attenuates the signal *within* that fixed range, so a downstream depth knob never
  double-attenuates. `getCurrentValue()` (and `OrbitModulator::getY()`) is bounded to that fixed range at
  all settings and all times. Trace: roadmap "boundedness (never exceeds depth)" (line 129) and "bounded,
  slow, and smooth" (line 78); Clarifications Q1/Q2.

### FR-010 series — `BrownianDrift` (roadmap lines 111–112)

- **FR-011** — `BrownianDrift` implements a bounded random walk with mean reversion, i.e. a discrete
  Ornstein–Uhlenbeck process, so the walk is pulled back toward a configurable mean rather than diffusing
  without bound. Trace: roadmap line 111 ("bounded random walk with mean-reversion (Ornstein–Uhlenbeck)").
- **FR-012** — `BrownianDrift` exposes a smoothness control that governs the correlation/slew of the walk
  (higher smoothness → slower, more correlated motion). Trace: roadmap line 111 ("smoothness control").
- **FR-013** — `BrownianDrift` exposes a depth control that scales its output **within** the fixed
  `[-1,+1]` source range (per FR-006 — the reported range does not shrink with depth); output stays within
  that fixed range for every input, satisfying FR-006. Trace: roadmap line 111 and the boundedness SC
  (line 129).
- **FR-014** — `BrownianDrift` is usable as a shared, decimated per-target drift generator (the roadmap
  calls it "the workhorse: per-partial detune drift, brightness wander, stereo wandering", line 112). It
  must therefore be cheap enough to instantiate/evaluate many times per block; the API supports being
  advanced once per block (FR-003). (Multi-instance orchestration itself is Phase 2+.)

### FR-020 series — `BreathingModulator` (roadmap lines 113–114)

- **FR-021** — `BreathingModulator` produces an asymmetric inhale/exhale cycle that is explicitly *not*
  sinusoidal. The breath shape is a **fixed, physiologically-inspired asymmetric curve** — roughly
  **40% inhale / 60% exhale** with distinct, independently shaped rise-vs-fall curvature — **hardcoded, not
  a parameter**: the control surface stays exactly {rate, depth, irregularity}. FR-021 is tested by
  asserting rise time ≠ fall time and that the shape is non-sinusoidal. Trace: roadmap line 113
  ("asymmetric inhale/exhale cycle (not sinusoidal)"); Clarifications Q6.
- **FR-022** — Cycle rate is settable across at least 0.01–0.5 Hz and clamped to that range. Trace:
  roadmap line 113 ("rate 0.01–0.5 Hz").
- **FR-023** — A depth control scales output amplitude within the declared source range (FR-006). Trace:
  roadmap line 113 ("depth").
- **FR-024** — An irregularity control applies cycle-to-cycle period jitter (each breath period is
  perturbed by a bounded, seeded random factor). At irregularity = 0 the period is exactly constant.
  Trace: roadmap line 114 ("irregularity (cycle-to-cycle period jitter)").

### FR-030 series — `TidalModulator` (roadmap lines 115–116)

- **FR-031** — `TidalModulator` sums **exactly 3 layers**, each a "sine pair" — two slightly detuned sines
  that slowly beat — whose periods are very slow, spanning at least 30 s to 10 min, and clamped to that
  range. A **single rate scalar** scales all three layers together. Trace: roadmap line 115 ("very slow
  (30 s – 10 min periods)"); Clarifications Q3.
- **FR-032** — The 3 layers use **fixed, mutually incommensurate irrational-ratio constants** (golden-ratio
  / √2 / √3 derived, hardcoded — *not* seed-drawn) such that no exact repeat of the combined output occurs
  within any stated finite test horizon (verifiably: no exact repeat within the longest render used in
  tests — see SC-003 `TidalModulator_NoExactRepeat`). The mathematical incommensurability of the ratios is
  the *design justification* for indefinite non-repetition; the testable claim is bounded to the finite
  window, since infinite-horizon non-repetition cannot be verified by any render. Because the ratios are
  fixed constants (not seeded), the dominant period SC-003(b) predicts follows from the rate scalar alone.
  Trace: roadmap line 116 ("incommensurate ratios; never repeats exactly"); Clarifications Q3.
- **FR-033** — Output is bounded to the declared source range regardless of how the component sines
  phase-align. The primary guarantee is *analytic*: the sum of the normalized per-layer amplitudes is
  ≤ the range span by construction (Σ|aₖ| ≤ range), so the worst-case fully-constructive alignment of all
  layers still cannot exceed range. This analytic bound is the FR-033 guarantee because worst-case phase
  alignment for 30 s – 10 min incommensurate periods cannot be reached by any practical render; the
  boundedness render (SC-001) is a sanity check on the implementation, not the proof. Trace: roadmap
  line 116 + boundedness SC (line 129).

### FR-040 series — `OrbitModulator` (roadmap lines 117–118)

- **FR-041** — `OrbitModulator` runs two weakly coupled phase oscillators using **Kuramoto-style
  phase-difference coupling** (`dφ = ω + k·sin(φ_other − φ_self)`) and exposes a 2D output: an x-value and
  a y-value, each independently retrievable so two different targets can be driven. The oscillator rate is
  settable across **0.01–0.5 Hz** and clamped to that range. Trace: roadmap line 117 ("two coupled
  oscillators … 2D output (x = one target, y = another)"); Clarifications Q5.
- **FR-042** — The base `getCurrentValue()` (single-float `ModulationSource` contract) returns the x axis
  (`x = depth·sin(φ₁)`); the second axis (`y = depth·sin(φ₂)`) is available via `getY() const noexcept`.
  Both axes obey the fixed `[-1,+1]` source range. Trace: roadmap line 117 + FR-001/FR-006;
  Clarifications Q5.
- **FR-043** — An orbital decay/growth parameter drives a **clamped radius (amplitude) envelope** that
  contracts or expands both axes; at the neutral setting the orbit is sustained (neither collapses to a
  point nor diverges), and the radius clamp bounds both axes by construction. Trace: roadmap line 118
  ("orbital decay/growth parameter"); Clarifications Q5.

### FR-050 series — `SplineTrajectory` (roadmap lines 119–120)

- **FR-051** — `SplineTrajectory` produces a Catmull-Rom interpolated path through a **fixed ring of N=4
  random-walk waypoints** (the Catmull-Rom minimum for one C1 segment with both neighbours) held in
  fixed-size storage. Trace: roadmap line 119 ("Catmull-Rom trajectory through N random-walk waypoints");
  Clarifications Q4.
- **FR-052** — Waypoints are regenerated ahead of the playhead from the seeded `Xorshift32`, with no
  allocation, so the trajectory advances indefinitely (ring of 4 waypoints; a new waypoint is drawn as the
  oldest is consumed). The waypoint spacing is set by `setWaypointInterval(double seconds)`, settable across
  **~0.5–30 s** and clamped to that range. Trace: roadmap line 120 ("regenerates waypoints ahead of
  playback"); Clarifications Q4.
- **FR-053** — Output is C1-continuous (continuous value and first derivative) across waypoint boundaries.
  Trace: roadmap line 120 ("guarantees C1-continuous output").
- **FR-054** — Output stays within the declared source range for all waypoint sequences (waypoints are
  themselves range-bounded and Catmull-Rom overshoot is clamped or bounded by construction). Trace:
  roadmap line 119 + boundedness SC (line 129).

### FR-060 series — `GrowthEnvelope` (roadmap lines 121–122)

- **FR-061** — `GrowthEnvelope` is a one-shot logistic / S-curve rise whose total duration is settable
  across at least 1–60 s and clamped to that range. Trace: roadmap line 121 ("one-shot logistic/S-curve
  rise over 1–60 s").
- **FR-062** — `GrowthEnvelope` is triggered via `trigger() noexcept`, which starts the rise. It is
  retriggerable *with continuation*: a `trigger()` while a rise is in progress continues from the current
  value toward the target and never restarts / snaps back to the start. A `trigger()` **after completion is
  a no-op** — the envelope holds at the top. Trace: roadmap line 122 ("retriggerable with continuation
  (never snaps back)"); Clarifications Q7.
- **FR-063** — Before the first `trigger()`, `getCurrentValue()` reads the **bottom of the range** (0, in
  the unipolar `[0,1]` source range). During a rise the output is monotonic non-decreasing and bounded to
  that fixed `[0,1]` range; once the target is reached it **holds at the top**. There is **no fall/release
  segment** — the envelope is a pure one-shot rise-and-hold. Trace: roadmap line 121 ("the sound slowly
  becomes") + FR-006; Clarifications Q7.

## Success Criteria

Each is measurable, with metric, threshold, and a test-name sketch. Tests live in
`dsp/tests/unit/processors/<name>_test.cpp` and register in the `dsp_processors_tests` target
(`dsp/tests/CMakeLists.txt:156`); run via `build/windows-x64-release/bin/Release/dsp_processors_tests.exe`.

- **SC-001 (Boundedness).** For each of the six modulators, over a render at every extreme of every
  parameter (min/max rate, min/max depth, max irregularity/coupling), `getCurrentValue()` (and
  `OrbitModulator::getY()`) never exceeds the `[min,max]` returned by `getSourceRange()`. Those bounds are
  **fixed at polarity full-scale** — `[-1,+1]` for every modulator except `GrowthEnvelope` (`[0,1]`) — and
  do **not** shrink with depth (Clarifications Q1/Q2), so the criterion asserts against those fixed
  full-scale bounds. The render horizon
  is **a function of the modulator's slowest configured period, not a flat 60 s**: each boundedness render
  spans at least **3× the longest configured period** at that parameter setting, so the criterion actually
  exercises the states it names. Concretely: `TidalModulator` at its 10 min max period renders ≥30 min;
  `BreathingModulator` at 0.01 Hz (100 s period) renders ≥300 s; `GrowthEnvelope` at 60 s duration renders
  ≥180 s; the fast/short-period settings render ≥60 s. For `TidalModulator` the render is a sanity check
  only — the load-bearing boundedness guarantee is the FR-033 analytic bound (Σ|aₖ| ≤ range), since
  worst-case constructive phase alignment is unreachable by any practical render. Metric: max absolute
  violation. Threshold: **0 violations** (hard). Test sketch: `BrownianDrift_NeverExceedsDepth`,
  `TidalModulator_BoundedUnderPhaseAlignment` (asserts the analytic amplitude-sum bound plus a long-render
  sanity check), …one boundedness case per modulator. Trace: roadmap SC line 129.
- **SC-002 (Smoothness / bounded slew).** For each modulator, the maximum absolute per-sample output delta
  is measured at that modulator's **worst-case (maximum-slew) configuration** — the fastest / least-smoothed
  / shortest-period / max-depth extreme, which is precisely where the per-sample smoother is most stressed
  and zipper-noise risk is highest. Measuring at the slowest-motion extreme is explicitly rejected: under
  FR-012 that is where per-sample slew is *smallest* (the trivially-passing case). The worst-case parameter
  values are named per modulator (each rendered at max depth, so the delta is measured against a full-scale
  output swing):

  | Modulator | Worst-case (max-slew) configuration |
  |---|---|
  | `BrownianDrift` | minimum smoothness (least correlated → fastest step), max depth |
  | `BreathingModulator` | maximum rate (0.5 Hz), max depth, max irregularity |
  | `TidalModulator` | shortest period (30 s), max depth, all layers at full amplitude |
  | `OrbitModulator` | maximum orbital rate and maximum coupling, max depth (both axes) |
  | `SplineTrajectory` | shortest waypoint spacing / fastest playhead advance, max depth |
  | `GrowthEnvelope` | minimum duration (1 s), full range rise |

  The render at the worst case spans ≥ the modulator's configured period (or the full rise for
  `GrowthEnvelope`) so the aggressive segment is actually traversed. Optionally the test may sweep all
  settings and take the global maximum, which subsumes the per-modulator worst case. Metric:
  `max |out[n] − out[n−1]|` over the worst-case render. Threshold: **≤ 1.0e-3** of the source-range span
  per sample at 48 kHz (derived from the modulation-step target used by `ModulationEngine`'s 120 ms amount
  smoother, `modulation_engine.h:111`; this value is **binding**, accepted in Clarifications OQ2). Test
  sketch: `<Modulator>_MaxSlewBounded`. Trace: roadmap SC line 129
  ("smoothness (max slew bounded)" — i.e. bounded at the aggressive extreme, not the quiescent one).
- **SC-003 (Statistical character).** The roadmap SC is "autocorrelation time matches rate parameter"
  (line 130). The 1/e autocorrelation-**decay**-time metric is only well-defined for genuinely
  stochastic/decorrelating sources; for periodic/quasi-periodic sources autocorrelation oscillates and
  never decays to a stable 1/e crossing, so the criterion is split by source type. All renders below are
  long relative to the slowest structure being measured (≥ 10× the longest configured period / relaxation
  time) and are **mean-detrended** before autocorrelation, since a slow quasi-periodic signal needs a
  render ≫ its longest period for the estimate to be meaningful.

  - **(a) Stochastic sources — decorrelation-time metric.** For `BrownianDrift` (OU, genuine exponential
    autocorrelation) and `SplineTrajectory` (random-walk waypoints), the lag at which the normalized
    autocorrelation first crosses 1/e scales monotonically with the smoothness / `setWaypointInterval`
    waypoint-spacing parameter (slower/smoother setting → longer decorrelation time), measured at ≥3
    settings. Threshold: **monotonic
    ordering across settings**, output distinguishable from white noise (decorrelation time ≫ 1 sample) and
    from a pure LFO (no single dominant periodic peak). These parameters have no closed-form target, so
    monotonicity is the correct form. Test sketch: `BrownianDrift_AutocorrTimeTracksSmoothness`,
    `SplineTrajectory_AutocorrTimeTracksSpacing`.
  - **(b) Periodic sources — period-tracking metric with a quantitative target.** For `TidalModulator`
    (deterministic undamped sine sum, FR-032) and `BreathingModulator` (exactly periodic at
    irregularity = 0, FR-024) the "1/e decay" is undefined; instead track the **period** via the dominant
    spectral peak (or the lag of the first autocorrelation *maximum*). Because these sources have a known
    physical rate/period, the check is **quantitative, not merely monotonic**: the measured dominant period
    must fall within a **measured tolerance band** of the value predicted by the set parameter —
    `TidalModulator`'s tracked period matches the configured layer period(s) (FR-031) and
    `BreathingModulator`'s matches 1/rate (FR-022) — at ≥3 settings, and the ordering is monotonic. The
    tolerance is set from the spectral-estimator bin width at the stated render length (documented in the
    test), not bit-exact. Test sketch: `TidalModulator_PeriodTracksSetting`,
    `BreathingModulator_PeriodMatchesRate`, `TidalModulator_NoExactRepeat`.

  Trace: roadmap SC line 130 ("autocorrelation time matches rate parameter") + roadmap eval line 131
  ("must read as organic drift, not noise and not LFO").
- **SC-004 (Determinism).** For every stochastic modulator, two instances given the same seed and the same
  `prepare` + `processBlock` call sequence produce identical `getCurrentValue()` sequences; a different
  seed produces a different sequence. Additionally, `reset()` **rewinds the modulator's `Xorshift32` stream
  to its original seed**, restoring the exact post-`prepare` state, so the same seed yields a bit-identical
  trajectory after every `reset()` (Clarifications OQ3). Metric: sample-by-sample equality of the two
  same-seed renders, and of a render before vs after `reset()`. Threshold: **exact equality** for same seed
  and across `reset()` (single build), inequality for differing seed. Test sketch:
  `<Modulator>_SeededDeterminism`, `<Modulator>_ResetRewindsToSeed`. Trace: roadmap SC line 130
  ("determinism with seeded `random.h`").
- **SC-005 (Sample-rate invariance).** For each modulator, rendering the same wall-clock duration at
  44.1 kHz and 96 kHz yields trajectories whose statistics agree within a measured tolerance — the
  modulator's behaviour is defined in seconds, not samples. Two refinements are required because the
  sources are near-zero-mean and stochastic:
  - **Drop relative-of-mean.** `BrownianDrift` (zero-mean OU), `TidalModulator` (zero-mean sine sum) and
    `OrbitModulator` are ~zero-mean, so a *relative* difference of the mean divides by ~0 and is undefined.
    The mean is either not compared, or compared with an **absolute** tolerance expressed in source-range-span
    units (not a percentage).
  - **Handle the RNG-draw mismatch.** Rendering the same wall-clock duration at 44.1 vs 96 kHz draws the
    seeded RNG a different number of times, so the two renders are different stochastic *realizations*, not
    one trajectory resampled — their statistics agree only in distribution. The spec resolves this one of two
    ways, chosen per modulator and stated in the test: **(a)** decouple the RNG draw schedule from the audio
    sample rate (draw at a fixed control rate, e.g. every `kControlRateInterval` samples-in-seconds) so the
    *same* wall-clock trajectory is produced at both rates and compared like-for-like; or **(b)** where (a)
    is impractical, compare distributional statistics (RMS, autocorrelation/decorrelation time) **averaged
    over multiple seeds**, with the tolerance derived from the measured across-seed realization variance
    rather than a flat guess.
  Metric: absolute mean difference (range-span units) + relative RMS and autocorrelation-time difference.
  Threshold: **RMS and autocorrelation-time within a measured tolerance** derived per the above (starting
  point ≤ 5%, tightened or loosened to the measured realization/estimator spread — not bit-exact, per the
  no-bit-exact-goldens rule, roadmap line 484); mean within an absolute range-span tolerance. Test sketch:
  `<Modulator>_SampleRateInvariant`. Trace: cross-cutting sample-rate-change constraint.
- **SC-006 (RT-safety, allocation-free).** A steady-state `processBlock` loop for each modulator performs
  zero heap allocations. Metric: allocation count in a `processBlock` hot loop (global new/delete counting
  harness or ASan/valgrind on the nightly lane). Threshold: **0 allocations** after `prepare`. Test
  sketch: `<Modulator>_NoAllocInProcess`. Trace: roadmap line 479.
- **SC-007 (Control-rate cost).** Advancing all six modulators once per 512-sample block is negligible.
  Because a "% of one core" figure is not reproducible across CI runners or dev machines (the same code
  passes or fails by hardware), the measurement basis is pinned: the metric is **nanoseconds per block** to
  advance one instance of each of the six once per 512-sample block, and the derived percentage is computed
  against the fixed 512-samples-at-48 kHz wall-clock budget (512 / 48000 ≈ 10.67 ms). The `[.perf]` test is
  **informational, not gating**, on arbitrary machines; it gates only against a **checked-in ns/block
  baseline** as a relative regression bound (fail if > baseline × 1.5), or when run on the designated CI
  perf runner. Threshold: **≤ 0.05%** of the 10.67 ms block budget combined on the reference runner
  (equivalently ns/block ≤ 0.0005 × 10.67 ms ≈ 5.3 µs), and no regression vs the checked-in baseline
  elsewhere (**binding**, accepted in Clarifications OQ2; the Phase 1 roadmap states only "cheap at control
  rate", line 126, and gives no number). Test sketch: `LifeModulators_ControlRateCost` (`[.perf]`). Trace:
  roadmap line 126.

## Edge Cases

- **RT-safety boundaries.** `processBlock(0)` is a no-op that leaves state and output unchanged.
  `numSamples` larger than any internal fixed buffer is handled without overrun (the modulators hold no
  per-sample block buffers, but `SplineTrajectory`'s waypoint ring must survive arbitrarily large block
  advances that consume multiple waypoints in one call).
- **Parameter extremes.** Rate/period at both clamp ends (e.g. `TidalModulator` at a 10 min period,
  `GrowthEnvelope` at 60 s) must still produce continuous, bounded output; `BreathingModulator`
  irregularity = 1.0 must not produce a zero or negative period; `OrbitModulator` growth at the extreme
  must neither diverge to Inf nor collapse to a stuck point.
- **Sample-rate changes.** Calling `prepare()` with a new sample rate mid-life re-derives all per-sample
  increments/coefficients so behaviour stays defined in seconds (SC-005). `setSampleRate`-style live
  changes, if provided, must not produce a discontinuity beyond the SC-002 slew bound.
- **Seed determinism.** Seed 0 must be handled safely (the `Xorshift32` constructor already substitutes a
  default for 0, `random.h:44`). `reset()` **rewinds the RNG to the original seed** (does not preserve
  stream position), restoring the exact post-`prepare` state so the same seed re-renders bit-identically
  (Clarifications OQ3) — asserted by SC-004.
- **Retrigger continuity.** `GrowthEnvelope` retrigger mid-rise continues from the current value (FR-062);
  a `trigger()` after completion is a **no-op** — the envelope holds at the top, never snaps back, and has
  no fall/release segment (Clarifications Q7, roadmap line 122).
- **Non-finite hygiene.** No NaN/Inf may propagate to `getCurrentValue()`; where the existing smoothers
  are used they already sanitize (`smoother.h:170` NaN/Inf handling), but any bespoke math (OU update,
  Catmull-Rom, logistic) must be verified non-divergent at extremes (aligns with the `-ffast-math`
  bit-pattern NaN rule; do not rely on `std::isnan`).

## Existing Components (reused — verified this session)

| Component | Header (verified) | Real signature / what is reused |
|---|---|---|
| `ModulationSource` (ABC, L0) | `dsp/include/krate/dsp/core/modulation_source.h:31` | `class ModulationSource` with pure virtual `[[nodiscard]] virtual float getCurrentValue() const noexcept` (:37) and `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept` (:41). The "concept" all six modulators implement. |
| `Xorshift32` (L0) | `dsp/include/krate/dsp/core/random.h:40` | `explicit constexpr Xorshift32(uint32_t seedValue=1)` (:44); `nextFloat()` → `[-1,1]` (:58); `nextUnipolar()` → `[0,1]` (:66); `seed(uint32_t)` (:72); `state()` (:78). Seeded determinism source. |
| `OnePoleSmoother` (L1) | `dsp/include/krate/dsp/primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (:160); `setTarget` (:170); `process()` (:197); closed-form `advanceSamples(size_t)` (:243); `snapTo` (:263). Per-sample smoothing of control-rate targets (FR-003). |
| `SlewLimiter` (L1) | `dsp/include/krate/dsp/primitives/smoother.h:468` | `configure(float riseRatePerMs, float fallRatePerMs, float sampleRate)` (:501); `process()` (:545). Alternative bounded-slew primitive for SC-002. |
| `RandomSource` (L2, pattern) | `dsp/include/krate/dsp/processors/random_source.h:34` | Reference pattern: `prepare(double)` (:45), control-rate `processBlock(size_t)` (:66), per-sample `process()` (:87), `getCurrentValue() override` (:114), `getSourceRange()` → `{-1,1}` (:118); owns `Xorshift32 rng_{98765}` + `OnePoleSmoother`. The template every new modulator follows. |
| `SampleHoldSource` (L2, pattern) | `dsp/include/krate/dsp/processors/sample_hold_source.h:36` | Same lifecycle/pattern; owns `Xorshift32 rng_{54321}`. Secondary reference. |
| `ChaosModSource` (L2, contrast ref) | `dsp/include/krate/dsp/processors/chaos_mod_source.h:35` | Control-rate decimation reference: `kControlRateInterval=32` (:43); `processBlock` advancing the correct number of control steps (:80); divergence guard `checkAndResetIfDiverged` (:306). Roadmap names it as the *contrast* reference (line 88) — bounded organic drift is the deliberate opposite of chaos. |
| `ModulationEngine` (L3, consumer) | `dsp/include/krate/dsp/systems/modulation_engine.h:79` | Verified it owns a **fixed** set of concrete sources (:810–818) and dispatches by `ModSource` enum in `getRawSourceValue` (:659) — it does **not** hold `ModulationSource*` polymorphically. Has `setExternalSourceValue(uint8_t, float)` slots (:552). Basis for the Clarifications OQ1 decision to scope router integration to Phase 7. |
| `VoiceModRouter` (L3, consumer) | `dsp/include/krate/dsp/systems/voice_mod_router.h:58` | Verified `computeOffsets(...)` takes raw float source values (:154) indexed by the fixed `VoiceModSource` enum — also not polymorphic over `ModulationSource`. Confirms Phase 1 adds no routing wiring. |

Note: roadmap line 88 also names `smoother` and `random` generically; verified these resolve to
`OnePoleSmoother`/`SlewLimiter` (smoother.h) and `Xorshift32` (random.h) above.

## New Components (ODR-swept this session)

ODR sweep run: `grep -rn "class <Name>|struct <Name>" dsp/ plugins/` for each name (plus a partial-name
sweep for `*Drift`, `*Modulator`, `*Trajectory`, `*Orbit`, `*Growth`, `Ornstein`, `CatmullRom`).

| Class | Layer | Header path (new) | ODR sweep result |
|---|---|---|---|
| `BrownianDrift` | 2 | `dsp/include/krate/dsp/processors/brownian_drift.h` | **Clear** — no `class`/`struct BrownianDrift` anywhere in `dsp/` or `plugins/`. |
| `BreathingModulator` | 2 | `dsp/include/krate/dsp/processors/breathing_modulator.h` | **Clear** — no collision. (Partial sweep: existing `RingModulator`, `HarmonicModulator`, `ModulatorSubController`, `ModulatorActivityView` are unrelated names, no clash.) |
| `TidalModulator` | 2 | `dsp/include/krate/dsp/processors/tidal_modulator.h` | **Clear** — no collision. |
| `OrbitModulator` | 2 | `dsp/include/krate/dsp/processors/orbit_modulator.h` | **Clear** — no collision. |
| `SplineTrajectory` | 2 | `dsp/include/krate/dsp/processors/spline_trajectory.h` | **Clear** — no `*Trajectory` class exists. |
| `GrowthEnvelope` | 2 | `dsp/include/krate/dsp/processors/growth_envelope.h` | **Clear** — no `*Growth` / `GrowthEnvelope` class exists (existing `adsr_envelope`, `multi_stage_envelope` are different names). |

All six are Layer 2: they may include Layer 0 (`modulation_source.h`, `random.h`) and Layer 1
(`smoother.h`) only, and must not include any Layer 2/3/4 header (layer discipline, roadmap line 481).

## Resolved Questions (roadmap-vs-code conflicts)

These flagged places where verified code conflicts with, or the roadmap explicitly defers to, this spec.
All three are now decided (Clarifications OQ1–OQ3, Session 2026-07-24) and encoded into the body above.

1. **"Route without changes" is not literal against the current routers — DECIDED (OQ1).** The roadmap
   (lines 124–126) says conforming to the `ModulationSource` concept lets `ModulationEngine` /
   `VoiceModRouter` route the Life Modulators "without changes." Verified this session, neither router
   consumes `ModulationSource` polymorphically: `ModulationEngine` dispatches a fixed `ModSource` enum
   (`modulation_engine.h:659`, sources owned as concrete members :810–818) and `VoiceModRouter` takes raw
   float source values indexed by a fixed `VoiceModSource` enum (enum defined in `voice_mod_types.h:29`,
   indexed inside `computeOffsets` at `voice_mod_router.h:162-168`). **Decision:** Phase 1 is ABC-only —
   each modulator implements the `ModulationSource` ABC (FR-001) + contract tests; the enum/route wiring
   (and any generic `ModulationSource*` slot) is DEFERRED to Phase 7. No router changes in Phase 1.
2. **No numeric SC thresholds for slew (SC-002) or CPU (SC-007) in the Phase 1 roadmap — DECIDED (OQ2).**
   The roadmap states "max slew bounded" (line 129) and "cheap at control rate" (line 126) without numbers.
   **Decision:** both spec-set thresholds are accepted as **binding** — SC-002 slew ≤ 1.0e-3 of full range
   per sample at each modulator's worst-case config; SC-007 combined control-rate CPU of all six ≤ 0.05% of
   one core @48 kHz, measured as informational vs a checked-in baseline.
3. **`reset()` RNG semantics — DECIDED (OQ3).** **Decision:** `reset()` rewinds each modulator's
   `Xorshift32` to its original seed, restoring the exact post-`prepare` state, so the same seed re-renders
   bit-identically after every `reset()`. Locked by the SC-004 determinism test.
