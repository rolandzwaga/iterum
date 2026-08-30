# Feature Specification: Seraphis Phase 7 — Voice & Engine

**Spec slug:** `seraphis-phase7-voice-engine`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 7 (lines 288–315); reuse-inventory row
line 92 (`Voice / Poly`) plus rows 86–94 for the composed engines; cross-cutting constraints lines 493–504;
roadmap Open Question 4 (line 511).
**Layers:** three new Layer 3 components (`SeraphisVoice`, `SeraphisEngine`, `SeraphisMacroMatrix`), all
under `dsp/include/krate/dsp/systems/`. No new Layer 0–2 or Layer 4 code, and **no amendment to any
shipped Phase 1–6 component** (see N-9).
**Depends on:** all of Phases 1–6. This is the first Seraphis phase that is a *composition* rather than a
new algorithm — every DSP block it drives already exists and was verified in this session by opening its
header.
**Plugin work:** none. KrateDSP only, unit-tested. `plugins/seraphis/` starts at Phase 8.

---

## Overview

Phase 7 turns six independently-tested DSP components into a playable instrument core (roadmap line 289).
`SeraphisVoice` chains **harmonic cloud → voice envelope → continuous body → atmosphere tap → spatial
position** — the roadmap's chain (lines 285–287) with the envelope moved to the **excitation** path, so
the body's 30 s decay cloud and the atmosphere's grains ring out after the gate closes rather than being
cut by it (Clarifications Q1, FR-010) — with the Phase 3 morph/entropy engine steering the cloud's spectrum
and a per-voice `OrbitModulator` wandering its azimuth. `SeraphisEngine` owns a pool of those voices
behind `VoiceAllocator`, spreads a distinct life-modulator seed into every voice so no two drift
identically (roadmap line 292), and produces the summed voice bus. Because `AetherReverb` is Layer 4 and
`SeraphisEngine` is Layer 3, the reverb is **not owned by the engine**: the engine exposes two entry
points — `processStereoBlock` (voice sum, pre-reverb) and `processOutputStage` (low-drive
`TapeSaturator` + `TruePeakLimiter`, in place on the reverb return) — and the caller composes
`voice sum → AetherReverb → processOutputStage`, which is roadmap line 293's chain realised without a
layer violation (FR-070). `SeraphisMacroMatrix` implements the five performance controls —
**Dream · Bloom · Dissolve · Gravity · Entropy** — as a fixed, documented mapping from five 0–1 knobs
onto engine internals plus a POD of computed Aether targets the same caller pushes into the reverb
(roadmap lines 294–309).

Almost nothing here is new signal processing. What *is* new, and what this spec has to pin exactly, is
**composition contract**: who calls whose setter, at what cadence, in what order, with which seed, and
what happens at the boundaries — voice steal on a 10-second release, a note-on while the previous tail is
still ringing, a sample-rate change, a full pool. Three roadmap statements do not survive contact with the
shipped headers (the 25 % full-poly ceiling, the 10 s+ release, and the `StereoField` azimuth path); they
are recorded in **Roadmap-vs-Reality Corrections** rather than silently reinterpreted.

Every claim below about existing code was verified by opening the header in the session that produced this
document; each cites `file:line`.

---

## Amendments

### Session 2026-07-31 — compliance remediation

Fourteen amendments, across nine criteria, were made against **measured** evidence after the first
compliance pass. Each amendment is written into the FR/SC body itself, with its measurement, so the list below is an index and
nothing depends on reading it.

| Item | What changed | Why, in one line |
|---|---|---|
| FR-013 | scratch `std::array` sized `kControlChunkSamples`, not `kMaxBlockSamples` | the voice never renders a partial chunk (FR-006's carry FIFO), so 2048-entry buffers would be 97 % unused and 1 MiB across the pool |
| FR-034 | `silence()` **arms** the ramp (clear, then add a decaying tail) instead of fading before clearing | a steal is issued *between* blocks, so there are no samples for a fade to occupy |
| FR-047 | the middle call is `resetForSteal()`, not `reset()` | `reset()` clears the FR-034 tail one line after arming it |
| SC-001 | baseline replaced by the **measured** 2 247 641 ns/block; ten-run dataset recorded | the derived stand-in was never measured; the procedure's own rule moved it DOWN |
| SC-003 | clause 1's window is positioned in the **output domain** (+ `getLatencySamples()`) | the reverb delays the chain 1024 samples; the ±10 ms window was measuring the wrong audio and the deliberately broken control PASSED |
| SC-003 | clause 3 is max-vs-max at **equal N**, not max-vs-p95 of 64 | a max over N against a percentile over M is an extreme-value estimator; the reported ratio grew 1.118 → 1.676 with the event count on an unchanged build |
| SC-003 | positive control (b) is **asserted**, through a friend probe | "recorded by hand" cannot rot loudly, and no figure had ever been recorded |
| SC-004 | the Growth clause compares sample-for-sample against a real `GrowthEnvelope`; the 0.99 crossing moves to the last 10 % | the logistic reaches 0.99 at τ = 0.9085, and both halves of the 5 % form passed on the build they were written to fail |
| SC-006(b) | the pairwise-Pearson 0.5 bound is **withdrawn**; distinctness is fingerprint inequality plus 0.1 × peak separation | Pearson on two signals that share their partial frequencies is cos(phase offset); measured worst correlation 0.98 through the chain, 0.60 for the cloud alone |
| SC-009 | Dream's detector matches **ordinally**, not by nearest ratio; the 24-partial support figure becomes "exactly the count the cloud sounds" | nearest-ratio makes the deviation a residual mod f₀; measured ρ = −0.232 on a metric that was reporting aliasing |
| SC-009 | Dissolve's differential is taken over the settled last second; its blur observable is L/R decorrelation | the slew rows move energy in time; blur is a documented phase decoherer, so spread moves the wrong way by construction (ρ = −1.0) |
| SC-009 | Gravity's body-decay observable is measured off an isolated-damping render, band-limited to 1–8 kHz | `getEngineT60Sec()` is resonance-only by construction and read 4.56972 at every step (ρ = 0) |
| SC-009 | Entropy's primary moves to the cloud-only arm and its effect size becomes ≥ 25 % relative | flatness 0.10 is a near-white spectrum; measured 0.000198 → 0.000270, two to three orders of magnitude below the absolute figure |
| SC-013 | clauses 1 and 2 are stated in dB and inherit ±1 dB / ±2 dB | `ContinuousBody`'s own SC-011 guarantees only ±1 dB across these rates; a composed criterion cannot be tighter than a component it contains |

Two FR-058 `amount`s were retuned rather than any criterion lowered, which is the remedy SC-009 names:
`Dissolve → AtmosBlur` 1.0 → 0.40 and `Entropy → MorphEntropy` 0.80 → 0.30. Both are recorded in
`seraphis_macro_matrix.h` with their measurements.

---

## Clarifications

### Session 2026-07-30

Every question the `specify` stage raised — the eight clarify-scan questions and all three **Open
Questions** — was ruled on by the phase owner in this session. **Nothing in this spec is contingent on an
unanswered question any more**; the FR/SC body below states the decided behaviour directly and the
`## Resolved Questions` section records the three roadmap-deferred rulings. The log is a record, not a
source of behaviour: a reader must never need it to know what the spec requires.

- **Q1 — Where does the amplitude envelope apply; what does a note-off gate?** *Decision:* the envelope
  gates **the cloud only** (the excitation path). `MultiStageEnvelope` (and, in Growth mode, the composite
  growth × MSE gain) is applied **pre-body**, to the `HarmonicCloud` output; the body and the atmosphere
  ring out freely, up to ~30 s tails. RA-2, FR-032's audio-quiescence retirement, FR-046's amnesty and
  SC-012 therefore **stand as written**. The CPU consequence is accepted: a released voice keeps rendering
  at full cost until it is quiescent, so 8 sounding voices is the steady state, which is exactly what
  SC-001 measures. FR-010's step order is rewritten accordingly.
- **Q2 — How far does the FR-019 "shipped voice default" table extend?** *Decision:* to **every** target —
  the table now covers every FR-058 macro target plus every other audible forwarder (body, atmosphere,
  spatial, envelope). The two zero-travel rows are fixed: orbit/spatial depth ships **below** its clamp
  maximum and body damping ships **above** its minimum. SC-010 asserts the whole surface against this one
  table, which is the normative definition of a neutral Seraphis.
- **Q3 — Are the FR-058 amounts and curves pinned, and is there a minimum effect size?** *Decision:*
  per-row `amount` and `curve` remain **implementation tuning**, but SC-009 gains a **per-macro minimum
  end-to-end effect size** clause, so an inaudibly small macro now fails the gate. Gravity's rows carry
  **signed** amounts (bipolar about 0.5).
- **Q4 — How is the atmosphere freeze triggered?** *Decision:* **both** surfaces —
  per-voice `captureFreeze()` / `releaseFreeze()` / `isFreezeCaptured()` on `SeraphisVoice`, **plus** an
  engine-wide `setAtmosphereFreeze(bool)` that fans out to every prepared voice, including voices
  allocated while freeze is engaged. FR-030 and FR-085 are amended to expose them.
- **Q5 (= OQ-1) — Shipped voice cap and gate scenario?** *Decision:* **ship 8 voices, gate on the
  worst case.** SC-001 measures the frozen-atmosphere (2.322 %/voice) + Aether configuration (c) scenario;
  the shipped default polyphony is **8** (20.36 %, 4.6 points of headroom) and `kMaxVoices` is compiled at
  **16**. Roadmap line 311's "16 voices" is a **deviation, recorded with evidence**; the ≤ 25 % ceiling is
  **kept**.
- **Q6 — What is `n` in FR-052's voice-sum gain?** *Decision:* `n` = **the current polyphony**, changing
  only on `setPolyphony` — static between polyphony changes, so there is no swell as tails retire and no
  block-partition hazard for SC-014. The −9 dB single-note cost is accepted; the limiter and the Aether
  stage see a consistent bus.
- **Q7 — What is `getCurrentLevel()`'s detector?** *Decision:* **absolute peak** over each 64-sample
  control chunk, fed into a one-pole with **instant attack** and a named
  `static constexpr kLevelReleaseMs` (~100 ms), sampled at chunk boundaries. FR-032, FR-045, FR-046,
  SC-011 and SC-012 all read this one detector, and SC-012's reclaim latency is derived from the constant.
- **Q8 — Retrigger mode, and does the steal sequence apply to a note-on onto a still-ringing slot?**
  *Decision:* `RetriggerMode::Legato` is set **explicitly** in FR-020 (continuation semantics — jump to
  sustain at the current level). **Additionally**, FR-047's `silence()` → `reset()` → `noteOn()` teardown
  is **required** whenever a `noteOn` lands on a slot the allocator force-idled during a polyphony shrink
  whose `isFinished()` is still false; ordinary retriggers on live voices use a plain `noteOn` with legato
  continuation.
- **OQ-2 — Atmosphere capture length, per-voice or shared ring?** *Decision:* **confirmed at 4 s per
  voice** (2.10 MB/voice, 33.6 MB resident at `kMaxVoices` = 16 @ 48 kHz), as FR-014 proposes. The
  shared-ring alternative is **rejected** — "the organism feeds on itself" — and the per-voice determinism
  criteria SC-005 / SC-006 stay as written.
- **OQ-3 — Does the macro mapping live in DSP or in the plugin?** *Decision:* **confirmed at DSP Layer 3**
  as `SeraphisMacroMatrix` (this spec's reading). SC-009 and SC-010 remain **Phase 7** criteria, and
  `ModulationEngine` is not reused (`kMaxMacros = 4`, VST-param-keyed routing).

---

## Scope

**In scope (this phase ships all of it):**

1. `SeraphisVoice` — a Layer 3 per-voice system owning one `HarmonicCloud`, one `SpectralMorphEngine`
   (which owns the `EntropyProcessor`), one `ContinuousBody`, one `AtmosphereEngine`, one
   `MultiStageEnvelope`, one `GrowthEnvelope` and one `OrbitModulator`, with `prepare` / `reset` /
   `noteOn` / `noteOff` / `silence` / `processStereoBlock` and a full introspection surface.
2. Voice envelope with two modes: **Standard** (`MultiStageEnvelope` alone, slow defaults) and **Growth**
   (`GrowthEnvelope` supplies the rise, `MultiStageEnvelope` supplies sustain and release) — roadmap
   lines 286–287.
3. Per-voice spatial placement: azimuth from `OrbitModulator`'s x axis, width from its y axis, applied as
   an equal-power balance + M/S width stage on the already-stereo voice bus (RA-3).
4. `SeraphisEngine` — a Layer 3 polyphonic system: `VoiceAllocator`-driven pool, quietest-with-amnesty
   steal selection (RA-4), per-voice seed spread, voice sum (`processStereoBlock`), a separate in-place
   `TapeSaturator` + `TruePeakLimiter` output stage (`processOutputStage`) that the caller applies to the
   reverb return, and the partial-set plumbing a caller needs to drive `AetherReverb::bloomNoteOn` /
   `bloomNoteOff` so the harmonic bloom follows the held chord.
5. `SeraphisMacroMatrix` — the five macros as a pinned, unit-testable mapping. Its engine-owned half is
   applied through `apply(SeraphisEngine&)`; its Aether-owned half is returned as the
   `SeraphisAetherTargets` POD for the Layer-4-aware caller to push (FR-056). Direction, target list and
   target *owner* are part of the contract, not an implementation detail.
6. The composed chain itself: a test-TU helper (`tests/test_helpers/seraphis_chain.h`,
   `renderSeraphisChain(engine, reverb, macros, script, …)`) that wires
   `processStereoBlock → AetherReverb::processStereoBlock → processOutputStage` and pushes
   `SeraphisAetherTargets` and `bloomNoteOn`/`bloomNoteOff`. Every SC that says "the composed chain"
   drives this helper, and Phase 8's processor reproduces it.
7. Determinism: a seeded engine render is reproducible under `render_fingerprint.h` tolerances
   (`tests/test_helpers/render_fingerprint.h:46-52`), never bit-exact goldens.
8. Unit tests covering every FR and every SC, registered in `dsp/tests/CMakeLists.txt`, plus the header
   registrations in `dsp/CMakeLists.txt` and `dsp/lint_all_headers.cpp`.

**Non-goals (owned by later phases, or deliberately excluded):**

- **N-1 — No plugin.** No `plugins/seraphis/`, no VST parameter IDs, no `plugin_ids.h`, no `editor.uidesc`,
  no state serialization, no preset format. Phase 8 (roadmap lines 321–433) and Phase 9 (line 435).
- **N-2 — No VST parameter surface for the macros.** `SeraphisMacroMatrix` exposes five `float` setters.
  Registering them as VST parameters with `k{Section}{Parameter}Id` names is Phase 9 (RQ-3 confirms the
  *mapping* itself is Phase 7 DSP; only the parameter surface is deferred).
- **N-3 — No effects roster beyond the roadmap's Phase 7 output stage.** Spectral freeze, spectral delay
  and stereo wandering are Phase 10 (roadmap lines 444–451). Phase 7 ships exactly `TapeSaturator` (low
  drive) + `TruePeakLimiter` (line 293) and nothing else.
- **N-4 — No MPE, no poly-aftertouch, no per-note expression.** Roadmap Open Question 5 assigns that to
  Phase 8/9 (line 513). `SeraphisEngine`'s note API is `noteOn(note, velocity)` / `noteOff(note)`.
- **N-5 — No MIDI, no tempo sync, no `BlockContext`.** `SeraphisEngine` is driven by note numbers and a
  sample count. Host transport plumbing is Phase 8.
- **N-6 — No unison, no portamento/glide, no mono/legato mode.** `VoiceAllocator` has unison
  (`voice_allocator.h:365-382`) and `PolySynthEngine` has portamento (`poly_synth_engine.h:264-277`);
  neither is named anywhere in the roadmap's Seraphis text, and both are excluded. Retuning a *sounding*
  body is still exercised — `ContinuousBody::setNoteFrequencyHz` (`continuous_body.h:982`) is smoothed at
  `kPitchSmoothMs = 20` (`:168`) — but nothing generates a glide.
- **N-7 — No new Layer 4 code and no second reverb.** `AetherReverb` ships complete
  (`effects/aether_reverb.h:1377`); Phase 7 instantiates and drives one.
- **N-8 — No UI, no visualization, no DataExchange.** Phase 11 (roadmap lines 453–461). The
  introspection accessors FR-085 requires exist for *tests*, and Phase 11 may later reuse them.
- **N-9 — No amendment to any Phase 1–6 component.** Phase 3 amended Phase 2 four times
  (`specs/seraphis-phase3-spectral-morph/spec.md:47-72`) and that register exists so the practice stays
  visible. Phase 7 declares up-front that it needs **no** such amendment: every composition below uses a
  public method quoted in the **Existing components** table. The two places where the roadmap implies an
  amendment (release > 10 s, `StereoField` azimuth) are resolved *without* touching the shipped component
  — see RA-2 and RA-3.
- **N-10 — No renegotiation of a Phase 2/4/5/6 CPU gate.** RA-1's tally uses the *measured* figures those
  phases recorded. Phase 7 may not raise any of them; its only admissible levers are voice count
  (the shipped default, RQ-1) and its own composition cost.

---

## Roadmap-vs-Reality Corrections

These are places where the roadmap's Phase 7 text does not match the code that Phases 1–6 actually
shipped. Each is recorded with the evidence, not silently reinterpreted.

### RA-1 — The 25 % full-poly ceiling at 16 voices is arithmetically unreachable; the voice cap is the lever

Roadmap line 311: *"full-poly CPU budget: 16 voices, everything on, ≤ 25 % of one core @ 48 kHz (sets
per-voice budgets from phases 2/4/5 with headroom)"*.

Phase 3 already raised this (`specs/seraphis-phase3-spectral-morph/spec.md:108-119`, RA-2) and recorded
that a blockquote had been added *"directly under Phase 7's success criteria in the roadmap, so Phase 7
cannot be specced without reading it"*. **That blockquote is not in the current roadmap file** — `grep -n
"47.4\|42.4\|25%\|25 %" specs/Seraphis-roadmap.md` returns exactly one line, line 311 itself. The warning
was lost in a later roadmap edit. It is restated here with *measured* numbers rather than the budget gates
Phase 3 used.

Reference block: 512 samples @ 48 kHz = **10 666 666.7 ns**, the constant every Seraphis perf TU derives
(`harmonic_cloud_perf_test.cpp:73`, `continuous_body_perf_test.cpp:108`,
`atmosphere_engine_perf_test.cpp:437`, `aether_reverb_perf_test.cpp:135`).

| Term | Measured figure | Source | % of one core |
|---|---|---|---|
| `HarmonicCloud`, 64 partials + drift | 29 642.8 ns/block | `specs/seraphis-phase2-harmonic-cloud/compliance.md:120` (worst of three) | 0.278 % |
| `SpectralMorphEngine` + `EntropyProcessor` | 9 300 ns/block | `spectral_morph_perf_test.cpp:224` `kMorphBaselineNs` (worst of eight) | 0.087 % |
| `ContinuousBody`, worst material config | 55 094.6 ns/block | `specs/seraphis-phase4-continuous-body/compliance.md:19` | 0.517 % |
| `AtmosphereEngine`, unfrozen | — | roadmap line 253 (measured, recorded there for this tally) | 1.048 % |
| `AtmosphereEngine`, frozen | — | roadmap line 253 | 1.440 % |
| **Per-voice total, unfrozen** | | | **1.930 %** |
| **Per-voice total, frozen** | | | **2.322 %** |
| `AetherReverb`, default config (b) | 109 138 ns/block | `aether_reverb_perf_test.cpp:256` | 1.023 % |
| `AetherReverb`, worst **measured** config (c) | 190 584 ns/block | `aether_reverb_perf_test.cpp:255-258` | **1.787 %** |

> **The (c) row is a measurement, not the checked-in baseline.** A previous revision of this table carried
> 200 114 ns (1.876 %), which is `ceil(190584 × 1.05)` — the *regression bound* stored at
> `aether_reverb_perf_test.cpp:329-330`, not a measured cost. Phase 6 states the correct figure verbatim
> and names this tally as its consumer: *"FOR RA-3 / Phase 7's TALLY, the number to carry is the
> quiet-machine dataset-1 figure — (b) = 109138 ns/block = 1.023 % of one core, GLOBAL — and the worst case
> (c) = 190584 ns = 1.787 %"* (`aether_reverb_perf_test.cpp:255-258`, repeated at
> `specs/seraphis-phase6-aether-space/compliance.md:205`). Mixing a padded baseline with measured
> per-voice figures inflates every row below, so the table and RQ-1's ruling use 1.787 %.

**The scenario below is normative, and SC-001 measures exactly it.** Frozen atmosphere (2.322 %/voice),
`ContinuousBody` at its worst measured material configuration, `AetherReverb` at configuration (c)
(N = 16 channels, shimmer + bloom + spectral diffusion all on, `diffusionFftSize = 4096`,
size = density = 1, 32 bloom resonators — `aether_reverb_perf_test.cpp:329-330`), all five macros at
their FR-060 neutral. Totals are **before** the output stage, the voice sum, the spatial stage and the
macro matrix, none of which carries a roadmap budget:

| Voice cap | 16 | 12 | 10 | **8** |
|---|---|---|---|---|
| Voices (frozen, 2.322 %/voice) | 37.15 % | 27.86 % | 23.22 % | **18.58 %** |
| + Aether (c) at 1.787 % | **38.94 %** | **29.65 %** | **25.01 %** | **20.36 %** |
| Verdict vs 25 % | 1.56× over | 1.19× over | 1.00× — no headroom | **fits, 4.6 pts spare** |

For reference only — **not** the gate — the same tally at the *default* Aether (b) and an *unfrozen*
atmosphere (1.930 %/voice) is 16: 31.90 %, 12: 24.19 %, 10: 20.32 %, 8: 16.46 %. That configuration would
admit 12 voices. The worst-case scenario is chosen as the gate because a shipped default polyphony has to
survive the frozen-atmosphere playing technique (roadmap line 74 makes freeze a first-class technique) and
config (c) is reachable from the Phase 8 parameter surface. The alternative — gating on the
default-Aether/unfrozen scenario, which would ship 12 — was **considered and rejected** by the phase owner
on 2026-07-30 (Clarifications Q5).

**Consequence for this spec.** The roadmap's own Open Question 4 (line 511) defers the voice cap to Phase 7
*"after budgets are real"*. They are now real, and under the normative worst-case scenario they select
**8**. This spec therefore compiles a capacity of 16 (`kMaxVoices`, so a later re-derivation costs no ABI
change) and ships a **default polyphony of 8**, with SC-001 gating at the shipped default *in the scenario
above*. **This is a deviation from roadmap line 311, which names 16 voices**, and it is recorded as such in
the Traceability row for line 311 as well as here; the **25 % ceiling itself is kept**, not amended.
**The phase owner ruled on exactly this on 2026-07-30: ship 8, gate on the worst case** (Clarifications
Q5 / Resolved Question RQ-1), so the shipped default is settled, not proposed.
Phase 3's other four permitted resolutions (raise the ceiling, re-derive per-voice budgets, budget the
output stage) are *not* taken: N-10 forbids the first two and the third does not close a 1.56× gap.

### RA-2 — `MultiStageEnvelope` cannot produce the "10 s+" release the roadmap's steal policy assumes

Roadmap line 291 justifies the steal amnesty with *"since releases are 10 s+"*. The shipped envelope caps
every stage time **and** the release at 10 s exactly: `MultiStageEnvelope::kMaxStageTimeMs = 10000.0f`
(`processors/multi_stage_envelope.h:65`) and `setReleaseTime` does
`releaseTimeMs_ = std::clamp(ms, 0.0f, kMaxStageTimeMs)` (`:206-208`). "10 s+" is therefore not
reachable from `MultiStageEnvelope` alone, and N-9 forbids raising the constant (it is a shared Layer 2
component with existing consumers).

**Resolution, no amendment required.** The audible Seraphis tail is not the amplitude envelope. It is the
resonant body's decay cloud — `ContinuousBody::kMaxCloudDecaySec = 30.0f` (`continuous_body.h:147`) with
`kMinB1 = 0.23f` giving `T60 = 6.91/0.23 = 30.0 s` (`:177`) — plus the atmosphere's grains, which live up
to 30 s (roadmap line 240), plus the global Aether tail. FR-032 therefore defines voice release as
**envelope release (≤ 10 s) followed by a tail-quiescence test on the actual audio**, and FR-046 keys the
steal amnesty on that quiescence test rather than on an envelope time. This is strictly stronger than the
roadmap's intent and needs nothing new.

### RA-3 — No `StereoField` mode does what "per-voice azimuth wandering via OrbitModulator + StereoField" requires

Roadmap line 286 names `StereoField` for per-voice spatial position. Reading the five modes
(`systems/stereo_field.h:47-53`):

| Mode | Pan (azimuth) | Width | Preserves the cloud's stereo image? |
|---|---|---|---|
| `Mono` | applied (`:481`) | discarded (`:469`) | **no** — sums to mono at `:461` |
| `Stereo` | **discarded** (`:500`) | applied via M/S (`:529-530`) | yes |
| `PingPong` | — | — | no (alternating taps) |
| `DualMono` | applied (`:620-621`) | discarded (`:593`) | **no** — sums to mono at `:584` |
| `MidSide` | **discarded** (`:648`) | applied (`:657`) | yes |

Every mode either discards pan or mono-sums the input. Mono-summing would destroy `HarmonicCloud`'s
per-partial equal-power stereo placement — `equalPowerGains(p01, panLeft_[i], panRight_[i])` at
`harmonic_cloud.h:1820` with the endpoint sign clamp at `:1832-1833` (`:977-991` are only the read-back
accessors) — which is the whole point of Phase 2's
`setStereoSpread`. On top of that, `StereoField::prepare` allocates **four `DelayLine`s plus a
`MidSideProcessor`** per instance (`:257-273`) — 64 delay lines across a 16-voice pool — and `setMode`
hard-resets those lines with no crossfade (`:312-325`). `StereoField` also has **zero production
consumers**: `grep -rln "stereo_field.h" dsp/ plugins/` matches only `dsp/CMakeLists.txt`, the systems
README, `dsp/lint_all_headers.cpp`, its own test, and an Iterum CHANGELOG line.

**Resolution, no amendment required.** FR-025 implements per-voice spatial placement as (a) an
equal-power azimuth *balance* on the already-stereo voice bus, **normalised to unity at centre** —
`StereoField::applyPan` (`:665-681`) and `equalPowerGains` (`core/crossfade_utils.h:50-53`) are *mono
panners* that distribute one sample across two outputs, so their raw `cos/sin` pair applied as a balance
attenuates a centred stereo bus by 3 dB; FR-025 therefore scales the pair by `√2` so centre is exactly
unity — followed by (b) M/S width via the
shipped Layer 2 `MidSideProcessor` (`processors/midside_processor.h:59`, `setWidth` at `:133`, `process`
at `:183`). This is delay-free, allocation-free per voice, preserves the cloud image, and delivers exactly
the two axes the roadmap asks `OrbitModulator` to drive. `StereoField` is **not instantiated** by Seraphis.

### RA-4 — `VoiceAllocator` has no "quietest" allocation mode

Roadmap line 290 specifies *"steal policy: quietest, with long-release amnesty"*. `AllocationMode`
(`systems/voice_allocator.h:55-60`) offers exactly `RoundRobin`, `Oldest`, `LowestVelocity`,
`HighestNote`. There is no `Quietest`, and there cannot be one inside `VoiceAllocator`: the class
*"[d]oes NOT own or process any DSP"* (`:124-125`) and so has no access to a voice's current output level.
It also has no amnesty concept — `findVoiceToSteal` sees only `state`, `note`, `velocity`, `timestamp`,
`frequency` (`:478-490`).

**Resolution, no amendment required — but the mechanism has to be the real one.** A previous revision of
this correction claimed the engine "overrides the choice via the public
`setVoiceNote`/`voiceFinished`/`getVoiceState` surface". **None of those three can redirect a steal**, and
the claim is withdrawn:

- `noteOn` allocates and selects the victim *internally* and returns only the resulting events
  (`voice_allocator.h:228-249` → `allocateNote`). There is no pre-emption hook.
- `setVoiceNote` writes only the `note` atomic; `state`, `timestamp`, `velocity`, `frequency` and
  `activeVoiceCount_` are untouched (`:415-422`).
- `voiceFinished` returns early for anything that is not `Releasing` (`:288-292`:
  `if (state != VoiceState::Releasing) return;`), so it cannot free an `Active` slot.

**The mechanism that works, using only shipped public methods.** `VoiceAllocator` stays the *bookkeeper*
(note↔slot mapping, `VoiceState`, event emission) and steal *selection* moves to `SeraphisEngine`, which
owns the voices and can read `SeraphisVoice::getCurrentLevel()`. The engine **frees its chosen slot
before** calling `allocator_.noteOn`, so the allocator's own `Oldest` search finds that slot idle and
allocates it:

1. Engine picks the victim `v` by FR-045's rule.
2. If `getVoiceState(v) == Active`, the engine calls
   `static_cast<void>(allocator_.noteOff(static_cast<std::uint8_t>(allocator_.getVoiceNote(v))))`
   (`:257`, `:406`) to move it `Active → Releasing`.
3. The engine calls `allocator_.voiceFinished(v)` (`:288`), which is now legal and returns the slot to
   `Idle`, clearing note/velocity/frequency and decrementing `activeVoiceCount_`.
4. The engine calls `allocator_.noteOn(note, velocity)` and dispatches the returned events normally. The
   only idle-or-oldest slot the allocator can now pick for the new note is `v`; **FR-045 requires the
   engine to assert that the returned `NoteOn` event names `v`** — SC-011 checks it, so a future allocator
   change that breaks the assumption fails loudly rather than silently stealing the wrong voice.
5. The voice-side teardown (`silence()` → `reset()` → `noteOn()`, FR-047) runs on `v` when that event is
   dispatched.

Steps 2–3 emit `VoiceEvent`s that the engine **discards** (the note-off is bookkeeping, not a musical
release: the voice is about to be silenced by FR-047). Precedent for the engine owning allocator
lifecycle decisions: `PolySynthEngine` already owns the deferred `voiceFinished` call rather than the
allocator (`poly_synth_engine.h:810-813`).

### RA-5 — `PolySynthEngine` is a pattern reference only, not a reusable container

The reuse-inventory row (roadmap line 92) lists `poly_synth_engine` and `synth_voice` for this phase.
`PolySynthEngine` hard-codes its pool as `std::array<SynthVoice, kMaxPolyphony> voices_`
(`systems/poly_synth_engine.h:867`) — it is not a template and has no voice-type parameter, and its whole
setter surface is subtractive-synth-specific (`setOsc1Waveform`, `setFilterCutoff`, … `:329-495`).
`SynthVoice` likewise is a two-oscillator/SVF/ADSR voice (`systems/synth_voice.h:71`, members at
`:444-451`). Neither can be instantiated by Seraphis. What Phase 7 **does** reuse from them is the *shape*:
the allocator-event dispatch loop (`poly_synth_engine.h:600-628`) and the deferred-`voiceFinished`
discipline (`:810-813`). This is recorded so no later reader expects a `PolySynthEngine<SeraphisVoice>`.

### RA-6 — `VoiceModRouter`'s destination enum cannot express any Seraphis destination

Reuse-inventory row 92 also names `voice_mod_router`. Its destinations are a closed, Ruinae-specific enum
— `FilterCutoff, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch,
OscALevel, OscBLevel, SpectralTilt` (`systems/voice_mod_types.h:50-62`) — and its sources are equally
closed (`:29-39`). Only `SpectralTilt` and `MorphPosition` are even nominally Seraphis-shaped. Phase 1's
Clarifications OQ1 had already deferred "adding Seraphis life modulators to the `VoiceModRouter` enum
slots" to Phase 7 (`specs/seraphis-phase2-harmonic-cloud/spec.md:60`). **Phase 7 declines the extension**:
adding ~20 Seraphis destinations to a shared enum would grow `VoiceModRouter::offsets_` for Ruinae too,
and the macro mapping this phase actually needs is a fixed five-input function, not a user-editable
matrix. `SeraphisMacroMatrix` (FR-056…FR-065) is that function. `VoiceModRouter` is **not used**; the
Phase-1 OQ1 deferral is hereby closed as *declined, with reason*.

### RA-7 — `ModulationEngine` supports 4 macros; Seraphis needs 5

Roadmap line 294 says the macro system is *"implemented as modulation-matrix presets over engine
internals (reuse `ModulationEngine`)"*. `ModulationEngine`'s macro count is a hard constant:
`kMaxMacros = 4` (`core/modulation_types.h:136`) with `ModSource::Macro1..Macro4`
(`:42-45`). Its routings are also keyed on `uint32_t destParamId` — *"Destination VST parameter ID"*
(`:107`) — which does not exist at Layer 3, and it owns **nine non-macro** modulation sources Seraphis does
not want. The header states the roster exactly: *"13 modulation sources: 2 LFOs, EnvFollower, Random, 4
Macros, Chaos, Rungler, S&H, PitchFollower, Transient"* (`modulation_engine.h:48-49`) — so the non-macro
nine are two `LFO`s, `EnvFollower`, `Random`, `Chaos`, `Rungler`, `SampleHold`, `PitchFollower` and
`Transient`, two of which (`EnvFollower`, `Transient`) need audio input
(`process(ctx, inputL, inputR, n)`, `:186-188`). Reusing it would mean raising a shared constant
(forbidden by the surgical-change rule), inventing Layer-3 param IDs, and paying for nine unused sources
per instance.

**Resolution.** `SeraphisMacroMatrix` is a standalone Layer 3 component. Its *shape* follows
`ModulationEngine`'s contract — bipolar amounts, per-target `ModCurve` shaping via the shared Layer 0
`applyModCurve` (`core/modulation_curves.h:38`), summation then clamp — so Phase 9 can expose it through
the same idioms. **`ModCurve::Stepped` is excluded** from the FR-058 table: it is
`std::floor(x · 4) / 3` (`modulation_curves.h:53-54`), a 4-level staircase whose jumps over a 21-step
sweep are ~6.7× the mean step and would fail SC-009's continuity clause by construction. Only `Linear`,
`Exponential` (x², `:46`) and `SCurve` (`x²(3−2x)`, `:49`) may appear. The reuse-inventory row's own note
(line 94, *"macro system (plugin layer)"*) contradicts roadmap line 294 on where this lives; **the phase
owner resolved that contradiction on 2026-07-30 in favour of DSP Layer 3** (Clarifications OQ-3 /
RQ-3), so `SeraphisMacroMatrix` is a Phase 7 component and SC-009 / SC-010 are Phase 7 criteria.

### RA-8 — Per-voice atmosphere capture memory does not scale; Phase 5 deferred the decision here

`AtmosphereEngine::PrepareConfig::captureSeconds` defaults to 8 s
(`systems/atmosphere_engine.h:368`) and `RollingCaptureBuffer` rounds capacity up to a power of two, so
8 s costs **4.19 MB per voice** — 67.1 MB at 16 voices, 33.6 MB at 8
(`specs/seraphis-phase5-atmosphere/spec.md:198-203`). Phase 5's OQ-2 states verbatim that the shipped
value *"and any move to a shared ring, are **Phase 7** decisions"* (`:83-86`). FR-014 sets the shipped
default. **That decision was taken on 2026-07-30: 4 s per voice, and the shared ring is rejected**
(Clarifications OQ-2 / RQ-2) — a shared ring would turn *"the organism feeds on itself"* into *"the
organism feeds on the ensemble"* and would force SC-005 / SC-006's per-voice determinism to be restated.

**The basis is `kMaxVoices`, not the default polyphony.** FR-041 prepares **all 16 slots** so that raising
polyphony after `prepare` cannot allocate, so the capture rings are resident at 16 regardless of the
shipped default of 8. Every memory figure in this spec is therefore priced at ×16, matching Phase 5's own
table row (`specs/seraphis-phase5-atmosphere/spec.md:199`: 4 s → 2.10 MB/voice → **33.6 MB** ×16). A
previous revision of FR-014 priced 4 s at ×8 (16.8 MB) and understated the resident figure by 2×.

---

## Existing components

Everything Phase 7 composes. Every signature was read this session at the cited line.

| Component | Header | What Phase 7 reuses — verified signature |
|---|---|---|
| `HarmonicCloud` (L3) | `systems/harmonic_cloud.h:127` | `void prepare(double)` `:282`; `void processStereoBlock(float* l, float* r, std::size_t n)` `:878`; `void noteOn()` `:635` / `void noteOff()` `:663`; `void setSpectralTarget(const float* ratios, const float* amplitudes, std::size_t count)` `:769`; `void setSeed(std::uint32_t)` `:701`; macro setters `setRichness` `:412`, `setInharmonicity` `:426`, `setSpectralTiltDb` `:439`, `setMutation` `:452`, `setSpectralGravity` `:478`, `setDriftDepthCents` `:501`, `setStereoSpread` `:535`, `setAttackTimeSec` `:556`, `setDecayTimeSec` `:568`; `bool isQuiescent() const` `:1040`; `float getPartialFrequencyHz(std::size_t)` `:955`; `std::size_t getActivePartialCount()` `:950`; `kMaxPartials = 64` `:138`; `kControlChunkSamples = 64` `:144` |
| `SpectralMorphEngine` (L3) | `systems/spectral_morph_engine.h:90` | `void prepare(double)` `:231`; `void updateChunk(std::size_t numSamples)` `:405`; `const float* getOutputRatios()` `:423` / `getOutputAmplitudes()` `:424` / `std::size_t getOutputCount()` `:425`; `void setState(int slot, const SpectralState&)` `:292`; `void setStateCount(int)` `:318`; `void setEntropy(float)` `:341`; `void setBloom(float)` `:332`; `void setTravelMode(TravelMode)` `:345`; `void setTargetPosition(float)` `:348`; `void setTravelRate(float)` `:358`; `void setSeed(std::uint32_t)` `:266`; `void reset()` `:249` |
| `EntropyProcessor` (L2) | `processors/entropy_processor.h:57` | Not instantiated directly — owned by `SpectralMorphEngine` and reached through `setEntropy` (`spectral_morph_engine.h:341`) and the const accessor `entropy()` `:453`. Roadmap line 309's "direct wire" is that forward. |
| `SpectralState` (L2, POD) | `processors/spectral_state.h:44` | `kStatePartials = 64` `:48`; `enum class SpectralStateId { SineStack, Bell, Choir, Glass, Breath }` `:313`; `SpectralState makeFactoryState(SpectralStateId)` `:373` |
| `ContinuousBody` (L3) | `systems/continuous_body.h:71` | `void prepare(double)` `:660`; `void processStereoBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t n)` `:1161`; `void setMaterial(BodyMaterial)` `:914` with `enum class BodyMaterial { Glass, Strings, MetalPlate, Chamber, Ice }` `:81`; `setResonance` `:953`, `setDamping` `:962`, `setKeyTracking` `:971`, `setNoteFrequencyHz` `:982`, `setDrive` `:992`, `setMix` `:1001`, `setCloudMix` `:1011`, `setCloudDecaySec` `:1022`, `setCloudSize` `:1033`, `setCloudDamping` `:1046`, `setWidth` `:1056`, `setSeed` `:1136`; `bool stateFinite() const` `:1328`. **There is no `getDamping`, `getResonance`, `getMix` or `getCloudMix`** — the twelve `[[nodiscard]]` getters (`:1242`–`:1320`) are material/mode/T60/drive/RMS/crossfade/cloud-loop/clamp-count introspection only, which is why SC-010 cannot assert a read-back on those targets. **No `noteOn`/`noteOff`** — it is continuous by design; the internal waveguide `noteOn` at `:1832` is a slot-assignment detail, injected at velocity 0 (`:1132-1134`). `kControlChunkSamples = 64` `:97` |
| `AtmosphereEngine` (L3) | `systems/atmosphere_engine.h:177` | `void prepare(double, const PrepareConfig&)` `:405` with `PrepareConfig{captureSeconds=8, blurEnabled, freezeEnabled, blurFftSize=1024, freezeFftSize=2048, maxBlockSamples=2048}` `:367-374`; `void processStereoBlock(const float* inL, const float* inR, float* outL, float* outR, std::size_t n)` `:665` — *"Shape identical to `ContinuousBody::processStereoBlock` … so Phase 7 chains them without an adapter"* `:656-658`; output is **wet texture only** `:660-661`; `void silence()` `:644` (**latches** — `reset()` is the only re-entry, `:641-643`); `setLevel` `:946`, `setBlur` `:874`, `setDensity` `:792`, `setGrainSeconds` `:779`, `setDriftDepth` `:836`, `setPanSpread` `:859`, `setDecorrelation` `:866`, `setFreezeMix` `:882`, `setSeed` `:977`; `void captureFreeze()` `:909` / `releaseFreeze()` `:928`; `std::size_t getLatencySamples()` `:1073`; `kMaxGrains = 64` `:187`; `kControlChunkSamples = 64` `:269` |
| `AetherReverb` (L4) | `effects/aether_reverb.h:1377` | `void prepare(double, const PrepareConfig&)` `:1614` with `PrepareConfig{numChannels=8, maxBlockSamples=2048, maxDelaySeconds=0.5, shimmerEnabled, shimmerMode, bloomEnabled, spectralDiffusionEnabled, diffusionFftSize=1024, seed=1}` `:1577-1587`; `void processStereoBlock(const float*, const float*, float*, float*, std::size_t)` `:2164`; `void bloomNoteOn(std::int32_t voiceId, const float* partialHz, std::size_t count)` `:2392`; `void bloomNoteOff(std::int32_t voiceId)` `:2473`; `setSize` `:2208`, `setDensity` `:2211`, `setDecaySeconds` `:2214`, `setFreeze` `:2230`, `setDimensionality` `:2239`, `setDamping` `:2244`, `setShimmerOctaveSend` `:2280`, `setShimmerFifthSend` `:2285`, `setBloomSend` `:2295`, `setSpectralDiffusion` `:2310`, `setSizeBreathDepth` `:2320`, `setDimensionalityTideDepth` `:2328`, `setWidth` `:2333`, `setMix` `:2336`, `setSeed` `:2361`; `std::size_t getLatencySamples()` `:2612`; `void silence()` `:2145` (**resumes on its own**, unlike the atmosphere's — `:2140-2143`); `kMaxBloomVoices = 8` `:1445`; **`kMaxBloomResonators = 32` `:1442`** — `bloomNoteOn` truncates with `std::min(count, kMaxBloomResonators)` (`:2398-2399`, documented at `:2377` *"@param count Clamped to kMaxBloomResonators (32)"*), which is **half** `HarmonicCloud::kMaxPartials` and forces FR-071's selection rule; `float getEffectiveDelayLengthSamples(std::size_t channel)` `:2506` — the only life-modulation observable the class exposes (there is **no** breath/tide accessor, and Phase 6 declined to add one: `specs/seraphis-phase6-aether-space/spec.md:2160`). **No getter exists for `mix`, `size`, `width`, either shimmer send, `bloomSend`, `sizeBreathDepth` or `dimensionalityTideDepth`** — the nine `[[nodiscard]]` getters are `getMatrixOrthogonalityError` `:2503`, `getEffectiveDelayLengthSamples` `:2506`, `getModalDensityPerHz` `:2511`, `getMaxSizeScale` `:2523`, `getCurrentMorphPosition` `:2526`, `getStateEnergy` `:2559`, `getActiveBloomResonatorCount` `:2583`, `getNonFiniteRecoveryCount` `:2588`, `getLatencySamples` `:2612`. SC-010 is written against that fact |
| `VoiceAllocator` (L3) | `systems/voice_allocator.h:187` | `std::span<const VoiceEvent> noteOn(uint8_t, uint8_t)` `:228`; `std::span<const VoiceEvent> noteOff(uint8_t)` `:257`; `void voiceFinished(size_t)` `:288`; `void setVoiceCount(size_t)` `:326`; `void setAllocationMode(AllocationMode)` `:311`; `void setStealMode(StealMode)` `:317`; `int getVoiceNote(size_t)` `:406`; `void setVoiceNote(size_t, uint8_t)` `:415`; `VoiceState getVoiceState(size_t)` `:424`; `float getVoiceFrequency(size_t)` `:446`; `void reset()` `:456`; `struct VoiceEvent{Type type; uint8_t voiceIndex, note, velocity; float frequency;}` `:102-115`; `kMaxVoices = 32` `:193` |
| `MultiStageEnvelope` (L2) | `processors/multi_stage_envelope.h:61` | `void prepare(float)` `:73`; `void gate(bool)` `:99`; `void setNumStages(int)` `:135` (`kMinStages=4, kMaxStages=8` `:63-64`); `void setStage(int, float level, float ms, EnvCurve)` `:166`; `void setSustainPoint(int)` `:178`; `void setReleaseTime(float ms)` `:206`; `void setRetriggerMode(RetriggerMode)` `:215`; `float process()` `:223`; `bool isActive()` `:251` / `bool isReleasing()` `:252`; `float getOutput()` `:253`. **`kMaxStageTimeMs = 10000.0f` `:65` bounds every stage *and* the release** — see RA-2 |
| `GrowthEnvelope` (L2) | `processors/growth_envelope.h:93` | `class GrowthEnvelope : public ModulationSource`; `void prepare(double)` `:117`; `void setDuration(float seconds)` `:144` clamped to `[kMinDuration=1, kMaxDuration=60]` `:96-98`; `void trigger()` `:161` (**no-op while Rising or Complete** — continues, never snaps back, `:158-160`); `void process()` `:173` / `void processBlock(size_t)` `:185`; `float getCurrentValue() const override` `:197`, range `{0,1}` `:202`. **Has no release** — it holds at the top |
| `OrbitModulator` (L2) | `processors/orbit_modulator.h:105` | `class OrbitModulator : public ModulationSource`; `void prepare(double)` `:137`; `void setSeed(std::uint32_t)` `:160`; `setRate` `:167`, `setCoupling` `:173`, `setGrowth` `:179`, `setDepth` `:185`; `void processBlock(size_t)` `:216`; `float getCurrentValue() const override` `:236` (**x** axis, clamped ±1); `float getY() const` `:242` (**y** axis — plain member, `ModulationSource` has no second-axis virtual, `:241`) |
| `ModulationSource` (L0) | `core/modulation_source.h:30` | `virtual float getCurrentValue() const noexcept = 0` and `virtual std::pair<float,float> getSourceRange() const noexcept = 0`. This is the concept the cross-cutting constraint names; `GrowthEnvelope` and `OrbitModulator` already conform |
| `MidSideProcessor` (L2) | `processors/midside_processor.h:59` | `void prepare(float, size_t)` `:96`; `void setWidth(float widthPercent)` `:133`; `void process(const float* lIn, const float* rIn, …)` `:183`. Used by FR-025 in place of `StereoField` (RA-3). At 100 % it computes `mid = (L+R)·0.5; side = (L−R)·0.5; out = mid ± side` (`:196-207`) — algebraically the identity, **not bit-exact** in IEEE float, which is why FR-026 makes a tolerance claim rather than a transparency claim |
| `TapeSaturator` (L2) | `processors/tape_saturator.h:80` | `void prepare(double, size_t)` `:141`; `void setModel(TapeModel)` `:207`; `void setDrive(float dB)` `:239`; `void setSaturation(float)` `:248`; `void setMix(float)` `:266`; `void process(float* buffer, size_t n)` `:335` — **mono, in-place**, so the engine calls it once per channel |
| `TruePeakLimiter` (L2) | `processors/true_peak_limiter.h:44` | `void prepare(double, std::size_t maxBlockSize)` `:59`; `void setCeilingDb(float)` `:85` (`kDefaultCeilingDb = -1.0f` `:46`); `void setReleaseMs(float)` `:91`; `void processBlock(float* left, float* right, int n)` `:104` — **stereo, in-place** |
| `deriveStreamSeed` (L0) | `core/random.h:102` | `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser with an explicit non-zero substitution `:110`. This is the per-voice seed-spread primitive (FR-016); `HarmonicCloud::deriveSeed` is a one-line forward to it (`harmonic_cloud.h:693-696`) |
| `render_fingerprint.h` (test helper) | `tests/test_helpers/render_fingerprint.h` | `RenderFingerprint fingerprintRender(std::span<const float>)` `:64` → `{rms, peak, meanAbs, totalVariation, checkpoints[32]}` `:54-60`; `FingerprintComparison compareFingerprints(actual, reference)` `:101`; tolerances `kSampleTolerance = 1e-4f` `:49`, `kMetricTolerance = 1e-5` `:52` |

**Named in the reuse inventory but deliberately NOT used:** `poly_synth_engine` (RA-5), `synth_voice`
(RA-5), `voice_mod_router` (RA-6), `modulation_engine` / `modulation_matrix` (RA-7), `stereo_field`
(RA-3), `adsr_envelope` (superseded by `MultiStageEnvelope`, which the roadmap itself names at line 286).

---

## New components

ODR sweep run this session: `grep -rn "class <Name>\b|struct <Name>\b|enum class <Name>\b" dsp/ plugins/`.

| Class / type | Layer | Header | ODR sweep result |
|---|---|---|---|
| `SeraphisVoice` | 3 | `dsp/include/krate/dsp/systems/seraphis_voice.h` | **CLEAN** — 0 matches in `dsp/` or `plugins/` |
| `SeraphisEngine` | 3 | `dsp/include/krate/dsp/systems/seraphis_engine.h` | **CLEAN** — 0 matches |
| `SeraphisMacroMatrix` | 3 | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` | **CLEAN** — 0 matches |
| `SeraphisMacro` (enum class) | 3 | `seraphis_macro_matrix.h` | **CLEAN** — 0 matches |
| `SeraphisVoiceConfig` (POD) | 3 | `seraphis_voice.h` | **CLEAN** — 0 matches |
| `SeraphisEngineConfig` (POD) | 3 | `seraphis_engine.h` | **CLEAN** — 0 matches |
| `SeraphisMacroValues` (POD) | 3 | `seraphis_macro_matrix.h` | **CLEAN** — 0 matches |
| `SeraphisAetherTargets` (POD) | 3 | `seraphis_macro_matrix.h` | **CLEAN** — 0 matches. The Aether-owned half of the macro mapping (FR-056): plain `float`s, no Layer 4 type named |
| `SeraphisMacroTargetOwner` (enum class) | 3 | `seraphis_macro_matrix.h` | **CLEAN** — 0 matches. `{ Voice, Engine, Aether }`, the FR-058 table's owner column |

Near-name components that exist and are **not** these (checked so no reader assumes reuse): `VoicePool`
(`plugins/membrum/src/voice_pool/voice_pool.h:86`, Membrum-local), `PolySynthEngine`
(`systems/poly_synth_engine.h:84`), `SynthVoice` (`systems/synth_voice.h:71`), `ModulationEngine`
(`systems/modulation_engine.h:79`), `ModulationMatrix` (`systems/modulation_matrix.h`), `MacroConfig`
(`core/modulation_types.h:128`).

`SeraphisEngine` including `effects/aether_reverb.h` (Layer 4) from a Layer 3 header would be **a layer
violation and is not permitted**. `AetherReverb` therefore stays *outside* `SeraphisEngine` (FR-070), and
for the same reason `SeraphisMacroMatrix` cannot name any `AetherReverb` type either — its Aether-facing
rows are computed into the `SeraphisAetherTargets` POD and pushed by the caller (FR-056).

---

## Functional Requirements

### A. `SeraphisVoice` — lifecycle and structure

- **FR-001** `SeraphisVoice` is a Layer 3 system in `dsp/include/krate/dsp/systems/seraphis_voice.h`,
  namespace `Krate::DSP`, including only Layers 0–2 and Layer 3 peers. It includes no Layer 4 header.
- **FR-002** It owns exactly one each of `HarmonicCloud`, `SpectralMorphEngine`, `ContinuousBody`,
  `AtmosphereEngine`, `MultiStageEnvelope`, `GrowthEnvelope`, `OrbitModulator` and `MidSideProcessor`, by
  value. It owns no `AetherReverb`, no `StereoField`, no `VoiceModRouter`, no `ModulationEngine`.
- **FR-003** `void prepare(double sampleRate, const SeraphisVoiceConfig& config) noexcept` is the **only**
  method on any path of which an allocation may occur. `SeraphisVoice`'s *own* state is entirely
  fixed-size (FR-013) and the three new headers contain no heap container at all (SC-008); the allocation
  FR-003 permits is the transitive one inside the sub-components' `prepare` — `AtmosphereEngine` alone
  owns eight `std::vector` members (`atmosphere_engine.h:2291`, `:2315-2316`, `:2329-2330`). FR-003
  forwards to each sub-component's `prepare` and ends with `reset()`. Calling it a second time fully
  reconfigures. Sample rate ≤ 1.0 is floored at 1.0 (the `atmosphere_engine.h:406-408` idiom).
- **FR-004** `SeraphisVoiceConfig` carries only what the sub-components need at prepare time:
  `captureSeconds`, `blurEnabled`, `freezeEnabled`, `blurFftSize`, `freezeFftSize`, `maxBlockSamples`.
  Every field is clamped, never rejected. It is a POD with default member initialisers and is passed by
  const reference (no narrowing in brace init — designated initialisers only).
  `maxBlockSamples` is clamped to `[1, SeraphisVoice::kMaxBlockSamples]` with
  `static constexpr std::size_t kMaxBlockSamples = 2048`, matching
  `AtmosphereEngine::PrepareConfig::maxBlockSamples`'s own default (`atmosphere_engine.h:373`). That
  constant is what lets FR-013's scratch be `std::array` rather than a heap buffer, which is what makes
  SC-008's zero-hit grep satisfiable. `SeraphisEngine` declares the identical constant and clamps the
  same way (FR-051's stereo bus is sized from it).
- **FR-005** `void reset() noexcept` returns the voice to the exact post-`prepare` state: every
  sub-component reset, envelope idle, gate off, orbit rewound, output silent. It is allocation-free.
- **FR-006** `void processStereoBlock(float* outLeft, float* outRight, std::size_t numSamples) noexcept`
  renders. It is a **generator**: no audio input. Guard order mirrors the shipped siblings
  (`continuous_body.h:1166-1180`): any null pointer → write nothing and return; `numSamples == 0` → no-op
  that consumes no control step; not prepared → zero-fill and return.
- **FR-007** All processing runs on an **absolute 64-sample control grid**, matching
  `HarmonicCloud::kControlChunkSamples` (`:144`), `ContinuousBody::kControlChunkSamples` (`:97`) and
  `AtmosphereEngine::kControlChunkSamples` (`:269`). A control chunk split 36 + 28 by a caller block
  boundary yields exactly the same control step as an unsplit 64. The value is a `static constexpr
  std::size_t kControlChunkSamples = 64` on `SeraphisVoice`, copied (not included) from the siblings, as
  those three already do to each other.
- **FR-008** Every public method except `prepare` is real-time safe: `noexcept`, no allocation, no lock,
  no exception, no I/O. Finiteness checks use the bit-pattern idiom (`detail::isNaN` /
  `detail::isInf` from `core/db_utils.h`, wrapped as the siblings do at
  `atmosphere_engine.h:1214-1216`), never `std::isnan`.

### B. `SeraphisVoice` — signal chain

- **FR-010** The chain is, in order: **harmonic cloud → voice envelope → continuous body → atmosphere
  tap → spatial position**. This is roadmap line 285's chain with **the envelope on the excitation path,
  pre-body** — the ruling of Clarifications Q1. The envelope therefore gates **only what the body is
  driven with**; the body's decay cloud (`ContinuousBody::kMaxCloudDecaySec = 30.0f`,
  `continuous_body.h:147`) and the atmosphere's grains keep ringing after the gate closes, which is what
  makes RA-2's tail argument, FR-032's audio-quiescence retirement, FR-046's amnesty and SC-012 true
  rather than vacuous. Applying the envelope to the summed voice bus instead would cap every tail at the
  envelope release (≤ 10 s, `multi_stage_envelope.h:65`) and was rejected for exactly that reason.
  **The accepted cost is CPU:** a released voice renders at full cost until it is quiescent, so a
  saturated pool of sounding voices is the steady state rather than a peak, and steals are the normal
  allocation path — which is why SC-001 measures 8 voices all sounding.
  Concretely, per control chunk of `n ≤ 64` samples:
  1. `morph_.updateChunk(n)`, then
     `cloud_.setSpectralTarget(morph_.getOutputRatios(), morph_.getOutputAmplitudes(), morph_.getOutputCount())`;
  2. `cloud_.processStereoBlock(cloudL, cloudR, n)` into a prepare-sized scratch;
  3. **voice envelope gain (FR-020, or FR-021's composite in Growth mode) applied per sample to
     `cloudL/R`, in place** — the excitation gate. `HarmonicCloud` already carries its own per-partial
     attack/decay/release with its own `noteOff()` (`harmonic_cloud.h:663-671`); the `MultiStageEnvelope`
     is retained on top of it because it is what supplies the roadmap's *slow* shape (line 286), the
     Growth-mode sustain/release carrier (FR-021), and Dissolve's envelope-slew axis (FR-063). Nothing
     downstream of this point is gated;
  4. `body_.processStereoBlock(cloudL, cloudR, bodyL, bodyR, n)` — `ContinuousBody` owns its own dry/wet
     via `setMix` (`:1001`), so the (already enveloped) cloud reaches the output through it;
  5. `atmos_.processStereoBlock(bodyL, bodyR, atmosL, atmosR, n)` — the tap reads the **post-body**
     signal, which is the roadmap's *"captures the voice's own output"* (line 231) and *"cloud+body
     output"* (line 236);
  6. voice bus = `bodyL/R + atmosL/R` — a **plain sum**, with no additional scaling. `AtmosphereEngine`'s
     output is wet texture only (`atmosphere_engine.h:660-661`), so this is a parallel mix, not a
     replacement, and the atmosphere's own level trim is **already applied inside** the component:
     `setLevel` is documented as *"Output gain trim"* (`:944-949`) and is multiplied per output sample by
     `const float levelGain = levelSmoother_.process();` at `:2233`. A previous revision of this step
     multiplied the returned buffer by `getLevel()` again, which squares the trim and would make FR-063's
     `setLevel`-up axis quadratic — SC-009's Dissolve gate would then have been measured on the wrong law.
     If a *separate* voice-level atmosphere blend is ever wanted it must be a distinct named
     `SeraphisVoice` member with its own constant, never a second read of the component's trim;
  7. spatial stage (FR-025) applied per sample.

  There is **no** gain stage on the voice bus other than the spatial stage's. The only bus-level gain the
  voice ever applies is `silence()`'s teardown ramp (FR-034, `kSilenceRampMs`), which is not musical.
- **FR-011** The morph→cloud handoff runs **once per control chunk of at most 64 samples**, which is the
  cadence `HarmonicCloud` documents for a driven spectral target (`harmonic_cloud.h:744-750`). Supplying a
  target at a coarser cadence is a defect, not a performance choice.
- **FR-012** `setSpectralTarget` is called every chunk unconditionally. The whole-array skip inside it
  (`harmonic_cloud.h:776-786`) makes an unchanged target cheap; the voice does not duplicate that check.
- **FR-013** Scratch buffers for FR-010's steps 2–5 (`cloudL/R`, `bodyL/R`, `atmosL/R`; the envelope in
  step 3 is applied in place on `cloudL/R` and needs none) are fixed-size `std::array` **members**,
  never locals and never heap containers. No `std::vector`, `resize`, `push_back` or `emplace` appears
  anywhere in the three new headers, on a render path or off one (SC-008).

  **They are sized `kControlChunkSamples` (64), not `kMaxBlockSamples` (2048), and that follows from
  FR-006/FR-007 rather than being a shortcut.** *(Amended 2026-07-31; a previous revision said
  `std::array<float, kMaxBlockSamples>`.)* The voice never renders a partial chunk: FR-006's carry FIFO
  renders exactly one 64-sample control chunk on demand and serves the caller out of it, so **no scratch
  buffer ever holds more than one chunk** and a 2048-entry array would leave 97 % of every buffer
  permanently unused. The cost of getting this wrong is not academic — eight 2048-entry arrays is 64 KiB
  per voice and 1 MiB across `kMaxVoices`, against the 2 KiB/voice the chunked design needs, and
  `SeraphisVoice` is already 47 616 B. The same reasoning applies to `SeraphisEngine`'s four
  bus/voice scratch arrays. What FR-013 actually requires — fixed size, member storage, no heap
  container anywhere in the three headers — is unchanged and is what SC-008 measures.
- **FR-014** `AtmosphereEngine` is prepared from `SeraphisVoiceConfig`. The **shipped default
  `captureSeconds` is 4.0 s** — 2.10 MB/voice → **33.6 MB resident at `kMaxVoices` = 16 @ 48 kHz**, and
  **67.2 MB @ 96 kHz**, because FR-041 prepares all 16 slots regardless of the default polyphony of 8
  (`specs/seraphis-phase5-atmosphere/spec.md:199`). Reduced from the component default of 8 s
  (`atmosphere_engine.h:368`, which would be 67.1 MB / 134 MB on the same basis) on the RA-8 memory table.
  **Confirmed at 4 s per voice, with the shared-ring alternative rejected** (Clarifications OQ-2 / RQ-2),
  decided against those ×16 figures. Each voice therefore captures **its own** output only.
- **FR-015** Voice latency is **0 samples** and `SeraphisVoice` exposes no `getLatencySamples()`. The only
  latent element is the atmosphere's blur stage (`atmosphere_engine.h:1073-1075`), and its output is wet
  texture with no dry counterpart to align against (`:660-661`, `:1070-1072`) — grains are already
  time-scrambled by construction, so compensating the parallel body path would align nothing. This is a
  decision, not an omission; it is restated in Edge Cases.

### C. `SeraphisVoice` — determinism and seeding

- **FR-016** `void setSeed(std::uint32_t seed) noexcept` sets the voice seed and distributes derived
  stream seeds via the Layer 0 `deriveStreamSeed(seed, salt)` (`core/random.h:102-111`) with **distinct,
  documented, `static constexpr` salts** — one each for the cloud, the morph engine, the body and the
  atmosphere, plus one for the orbit modulator. Salts are pairwise distinct and their ranges do not
  overlap (the `atmosphere_engine.h:358` `static_assert` pattern).
- **FR-017** The seed is applied at `setSeed` and at `prepare`. `ContinuousBody::setSeed` is documented
  *"[c]onfigure-time only, and deliberately NOT retro-deterministic"* and takes effect at the next mode-set
  rebuild (`continuous_body.h:1117-1124`) — `SeraphisVoice` therefore calls it **before the first note**,
  which is exactly the usage that header promises (`:1122-1123`).
- **FR-018** Two voices prepared with the same config and the same seed, given the same call sequence,
  produce renders that compare equal under `compareFingerprints` at the shipped tolerances. Two voices
  with **different** seeds must not (the distinctness clause is SC-006).

### C2. `SeraphisVoice` — shipped voice defaults (the macro base point)

- **FR-019** `prepare` leaves the voice at a documented **shipped voice default** that is *not* in every
  case the sub-component's own constructor default. This FR exists because several roadmap-normative macro
  axes have **zero travel** from the component defaults, which would make SC-009's gates unsatisfiable and
  SC-010's inertness check vacuous. The base point is normative and is what FR-060/SC-010 mean by "the
  documented voice default".

  **The table is complete and closed** (Clarifications Q2): it covers **every** FR-058 macro target *and*
  every other audible parameter `SeraphisVoice` forwards — cloud, morph, body, atmosphere, envelope and
  spatial. There is no "unlisted target ships at its component default" escape clause; if a forwarder is
  audible it has a row here. **This table is the normative definition of a neutral Seraphis**, SC-010
  asserts the whole surface against it, and any change to a row moves SC-009's and SC-010's base point and
  must be made here first. Rows marked *(unchanged)* deliberately adopt the component default; the reason
  is stated so the adoption is a decision rather than an omission.

  **Harmonic cloud** (`systems/harmonic_cloud.h`)

  | Target | Component default | **Shipped voice default** | Why |
  |---|---|---|---|
  | `setRichness` | `1.0f` (`:2125`), clamp max (`:416`) | **0.60** | At 1.0 Bloom's `setRichness ↑` row (FR-062, roadmap line 305) is a no-op — it is already at the clamp. 0.60 leaves headroom for Bloom and travel for Gravity ↓ (FR-064) |
  | `setInharmonicity` | `0.0f` (`:2126`), range [0, `kMaxInharmonicity = 0.1`] (`:191`, `:430`) | **0.030** | Dream's `setInharmonicity ↓` (FR-061) needs somewhere to descend from; at 0 it is already at the floor |
  | `setMutation` | `0.0f` (`:2128`) | **0.25** | Same reason — Dream drives it ↓ |
  | `setSpectralGravity` | `0.0f` (`:2129`), range [−1, +1] (`:483`) | **+0.20** | Dream drives it *to 0* (FR-061); from a default of 0 that row does nothing |
  | `setSpectralTiltDb` | `0.0f` (`:2127`), range [−12, +12] (`:194-195`) | **0.0** *(unchanged)* | Bloom ↑ and Gravity ↓ are symmetric about it |
  | `setDriftDepthCents` | `0.0f` (`:2130`), range [0, `kMaxDriftCents = 50`] (`:214`) | **0.0** *(unchanged)* | Entropy is the only macro that raises it (FR-065) and only goes up, so the floor is the correct base; SC-013 also measures with it at 0 |
  | `setStereoSpread` | `0.0f` (`:2132`), range [0, 1] (`:535`) | **0.35** | At 0 every partial sits dead centre, so RA-3's whole reason for not mono-summing (preserving the per-partial equal-power placement) has nothing to preserve at neutral, and Bloom's stereo-width sub-axis widens a mono source. 0.35 leaves Bloom travel to 1.0 |
  | `setAttackTimeSec` | `0.05f` s (`:2133`), range [0.05, 30] (`:556`) | **0.05 s** *(unchanged)* | The floor is the correct base for Dissolve's ↑ row (FR-063); the roadmap's *slow* shape is supplied by the `MultiStageEnvelope` (FR-020), not by the per-partial envelope |
  | `setDecayTimeSec` | `0.5f` s (`:2134`), range [0.05, 60] (`:568`) | **0.5 s** *(unchanged)* | No macro row targets it |

  **Spectral morph** (`systems/spectral_morph_engine.h`)

  | Target | Component default | **Shipped voice default** | Why |
  |---|---|---|---|
  | `setEntropy` | `0.0f` — the ctor calls `entropy_.setEntropy(0.0f)` (`:224`) | **0.20** | Dream ↓ and Entropy ↑ both need travel, in opposite directions, from a common base |
  | state slots / count | all four slots `SineStack`, `numStates_ = kMinStates = 2` (`:214-219`) | **slot 0 = `SineStack`, slot 1 = `Glass`, count 2** | FR-019a — without it Bloom's morph row is SineStack → SineStack, i.e. inaudible |
  | `setTargetPosition` | `0.0f` (`:740`) | **0.0** *(unchanged)* = slot 0 | Bloom morphs *toward* slot 1 (FR-062), so slot 0 is the base |
  | `setTravelMode` | `TravelMode::External` is enum value 0 (`:139`) | **`External`** *(unchanged)* | Position is driven by the macro, not by an autonomous spline journey |
  | `setTravelRate` | `kMinTravelRate = 1/600` journeys/s (`:101`, `:741`) | **`kMinTravelRate`** *(unchanged)* | Inert under `External`; kept at the floor so a later mode switch cannot lurch |
  | `setBloom` | `0.0f` (`:220`, `:748`) | **0.0** *(unchanged)* | No macro row targets it — the **Bloom macro** drives `setTargetPosition` and the Aether sends, not this morph-internal fill |

  **Continuous body** (`systems/continuous_body.h`)

  | Target | Component default | **Shipped voice default** | Why |
  |---|---|---|---|
  | `setDamping` | `kDefaultDamping = 0.0f` (`:128`) — which **is** `kMinDamping` (`:126`) | **0.25** | **Zero-travel fix.** Gravity is bipolar (FR-064), so its *air* half must be able to **lower** damping; at the component default it is already at the floor and half the axis is a no-op. 0.25 leaves travel in both directions inside [0, 1] |
  | `setMaterial` | `kDefaultMaterial = BodyMaterial::Glass` (`:162`) | **`Glass`** *(unchanged)* | No macro row selects material; SC-001/SC-002 pin the worst *measured* material configuration explicitly rather than inheriting it |
  | `setResonance` | `kDefaultResonance = 0.7f` (`:124`) | **0.7** *(unchanged)* | No macro row targets it |
  | `setKeyTracking` | `kDefaultKeyTracking = 1.0f` (`:132`) | **1.0** *(unchanged)* | The body tracks the played note (FR-023) |
  | `setDrive` | `kDefaultUserDrive = 1.0f` (`:136`), range [0, 4] (`:134-136`) | **1.0** *(unchanged)* | Key Design Decision 4 (roadmap line 78): no aggressive distortion |
  | `setMix` | `kDefaultMix = 1.0f` (`:140`) — fully wet | **1.0** *(unchanged)*, and its consequence is stated: at neutral the cloud reaches the output **only through the resonators** (FR-010 step 4); there is no direct-cloud path | Recorded rather than left implicit, because a reader comparing FR-010 against the body's dry/wet would otherwise have to derive it |
  | `setCloudMix` | `kDefaultCloudMix = 0.25f` (`:144`) | **0.25** *(unchanged)* | No macro row targets it |
  | `setCloudDecaySec` | `kDefaultCloudDecaySec = 4.0f` (`:148`), max `kMaxCloudDecaySec = 30.0f` (`:147`) | **4.0 s** *(unchanged)* | The 30 s tail RA-2 relies on is *reachable*, not the default; SC-012's script raises it explicitly to produce the 10 s+ tails it measures |
  | `setCloudSize` | `kDefaultCloudSize = 1.0f` (`:152`) | **1.0** *(unchanged)* | No macro row targets it |
  | `setCloudDamping` | `kDefaultCloudDamping = 0.3f` (`:156`) | **0.3** *(unchanged)* | No macro row targets it |
  | `setWidth` | `kDefaultWidth = 1.0f` (`:160`) | **1.0** *(unchanged)* | The voice's own width axis is the FR-025 M/S stage, driven by the orbit's y |

  **Atmosphere** (`systems/atmosphere_engine.h`)

  | Target | Component default | **Shipped voice default** | Why |
  |---|---|---|---|
  | `setLevel` | `level_ = 1.0f` (`:2365`), range [0, `kMaxLevel` = 2] (`:944-949`) | **0.5** | At the component default the atmosphere is already at full trim before Dissolve does anything, so Dissolve's headline axis (roadmap line 306, *atmosphere mix ↑*) is a fine trim rather than an entrance. 0.5 keeps it audible at neutral and gives Dissolve a 4× range to 2.0 |
  | `setBlur` | `blur_ = 0.0f` (`:2363`) | **0.0** *(unchanged)* | Dissolve only raises it (FR-063); the floor is the correct base |
  | `setDensity` | `density_ = 4.0f` grains/s (`:2351`), range [0.1, 20] | **4.0** *(unchanged)* | No macro row targets it |
  | `setGrainSeconds` | `grainSeconds_ = 4.0f` (`:2350`), range [0.05, 30] | **4.0 s** *(unchanged)* | Equals FR-014's 4 s capture ring, so a grain can span the whole history and no grain can outlive it |
  | `setDriftDepth` | `driftDepth_ = 0.3f` (`:2357`) | **0.3** *(unchanged)* | Entropy raises it (FR-065) and travel to 1.0 remains |
  | `setPanSpread` | `panSpread_ = 0.7f` (`:2360`) | **0.7** *(unchanged)* | No macro row targets it |
  | `setDecorrelation` | `decorrelation_ = 0.5f` (`:2361`) | **0.5** *(unchanged)* | No macro row targets it |
  | `setFreezeMix` | `freezeMix_ = 0.0f` (`:2364`) | **0.0** *(unchanged)* | Freeze is a played technique (FR-030a), not a default state |
  | freeze capture | not captured | **not captured** (`isFreezeCaptured() == false`) | FR-030a; SC-001 engages it explicitly as part of its scenario |

  **Envelope and spatial** (`SeraphisVoice`'s own state)

  | Target | Component default | **Shipped voice default** | Why |
  |---|---|---|---|
  | `MultiStageEnvelope` stages, sustain point, release, curve | component ctor values | **FR-020's set**: 4 stages, sustain point 2, {2000, 4000, 0, 0} ms, release 8000 ms, `EnvCurve::Exponential` | Roadmap line 286's *slow defaults*; every value is inside `kMaxStageTimeMs = 10000` (`multi_stage_envelope.h:65`), which is also why Dissolve's release row has 2000 ms of travel and no more |
  | `MultiStageEnvelope::setRetriggerMode` | `RetriggerMode::Hard` (`multi_stage_envelope.h:463`) | **`RetriggerMode::Legato`** (`primitives/envelope_utils.h:64`) | FR-020, Clarifications Q8 — a retrigger continues at the current level instead of restarting a 2 s attack |
  | `EnvelopeMode` | — | **`Standard`** | Growth is opt-in (FR-021) |
  | `GrowthEnvelope::setDuration` | `kDefaultDuration = 10.0f` s (`growth_envelope.h:100`, `:256`) | **10.0 s** *(unchanged)* | Inside [1, 60] (`:96-98`); no macro row targets it |
  | `setSpatialDepth` → `OrbitModulator::setDepth` | `kDefaultDepth = 1.0f` (`orbit_modulator.h:123`, `:322`) — the clamp **maximum** of `setDepth` (`:184-186`) | **0.35** | **Zero-travel fix.** Dream's `setSpatialDepth ↑` row (FR-061) is a no-op at the component default. 0.35 keeps a gentle default wander — which SC-016 requires to be non-zero — and leaves Dream travel to 1.0 |
  | `setSpatialRate` → `setRate` | `kDefaultRate = 0.1f` Hz (`orbit_modulator.h:122`, `:319`) | **0.1 Hz** *(unchanged)* | One orbit per 10 s: motion, not vibrato |
  | `setSpatialCoupling` → `setCoupling` | `0.0f` (`:320`) | **0.0** *(unchanged)* | Voices must drift **independently** (roadmap line 292, FR-050's seed spread); coupling would pull them together |
  | `setSpatialGrowth` → `setGrowth` | `0.0f` (`:321`) | **0.0** *(unchanged)* | 0 is the documented sustain neutral (`orbit_modulator.h:177-180`), and FR-025 forbids using growth to shape the width axis |

- **FR-019a (default spectral-state set)** `SpectralMorphEngine`'s constructor loads **all four slots with
  `makeFactoryState(SpectralStateId::SineStack)`** and sets `numStates_ = kMinStates = 2`
  (`spectral_morph_engine.h:214-219`, `:96`), explicitly so that *"the default configuration [is] the
  well-trodden 'perfectly static output' corner"* (`:210-213`). Under that state, Bloom's
  `setTargetPosition` row (FR-062) morphs SineStack → SineStack and is **inaudible**. `SeraphisVoice::prepare`
  therefore authors the voice's own two-state set, before the first note (FR-031 makes `setState` /
  `setStateCount` configure-time only):
  - **slot 0 = `makeFactoryState(SpectralStateId::SineStack)`** — amplitude law `n^-1`
    (`processors/spectral_state.h:365`), the darker endpoint;
  - **slot 1 = `makeFactoryState(SpectralStateId::Glass)`** — amplitude law `n^-0.5 · (even ? 0.35 : 1)`
    (`:368`, `detail::factory::kGlassEvenAtten` `:331`), i.e. a 6 dB/oct shallower rolloff and therefore
    unambiguously the **brighter** of the two;
  - `setStateCount(2)` (`spectral_morph_engine.h:318`, already the default but stated so a future
    `kMinStates` change is caught), `setTravelMode(TravelMode::External)` and `setTargetPosition(0.0f)`.
  `SpectralStateId` is at `processors/spectral_state.h:313` and `makeFactoryState` at `:373`.
  FR-062 names the target slot **by index (1)**, not by a description.

### D. `SeraphisVoice` — envelope

- **FR-020** The voice amplitude envelope is a `MultiStageEnvelope` applied to the **cloud/excitation**
  path (FR-010 step 3), with **slow defaults** (roadmap
  line 286): 4 stages, sustain point 2, stage times {attack 2000 ms, decay 4000 ms, sustain-hold 0 ms,
  post-sustain 0 ms}, release 8000 ms, `EnvCurve::Exponential`. All values are inside
  `kMaxStageTimeMs = 10000` (`multi_stage_envelope.h:65`).
  **`prepare` additionally calls `setRetriggerMode(RetriggerMode::Legato)`**
  (`multi_stage_envelope.h:215`, enum at `primitives/envelope_utils.h:64`) — explicitly, because the
  component default is `RetriggerMode::Hard` (`:463`) and would silently apply. Under `Hard`, `gate(true)`
  on a sounding voice re-enters stage 0 (`:99-106`), i.e. a fresh 2000 ms attack on a note the player is
  re-articulating; under `Legato` a gate on a `Releasing` envelope returns straight to `Sustaining` **at
  the current level** (`:106-121`), which is the continuation semantics the rest of this spec assumes
  (roadmap line 124, *"retriggerable with continuation (never snaps back)"*, and Edge Case 14). The mode
  is not exposed as a setter; it is part of the FR-019 shipped voice default.
- **FR-021** Two envelope modes, selected by `void setEnvelopeMode(EnvelopeMode) noexcept` with
  `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }`:
  - **Standard** — the `MultiStageEnvelope` output is the excitation gain (FR-010 step 3).
  - **Growth** — `GrowthEnvelope` supplies the rise; the excitation gain is
    `growth_.getCurrentValue() * mse_.getOutput()`. Release still comes from the `MultiStageEnvelope`.
    This is the roadmap's *"`GrowthEnvelope` replaces attack"* (line 287) expressed against the two real
    APIs — `GrowthEnvelope` has no release and holds at the top (`growth_envelope.h:158-160`), so it
    cannot be the whole envelope.
    **The `MultiStageEnvelope` must reach sustain immediately, and forcing stage 0 to 0 ms does not do
    that.** `MultiStageEnvelope` traverses stages sequentially and only enters `Sustaining` when
    `currentStage_ == sustainPoint_` (`multi_stage_envelope.h:386-389` inside `advanceToNextStage`,
    reached from the stage-completion branch at `:307-312`). With FR-020's defaults (sustain point 2,
    stage-1 decay 4000 ms) zeroing stage 0 alone still leaves a 4 s stage-1 ramp, so the composite gain
    would be shaped by *both* rises. Growth mode therefore forces **every stage time from 0 up to and
    including `sustainPoint − 1`** — with FR-020's defaults that is stages 0 and 1 — to **0 ms** via
    `setStage(stage, level, 0.0f, curve)` (`:166`), preserving each stage's level and curve. The
    `MultiStageEnvelope` then lands on its sustain level within one sample of the gate and the composite
    gain is the `GrowthEnvelope` shape alone, which is what SC-004's Growth clause asserts. Leaving
    Standard mode restores the FR-020 stage times.
- **FR-022** `void setGrowthDurationSeconds(float) noexcept` forwards to
  `GrowthEnvelope::setDuration`, clamped by that component to [1, 60] s (`growth_envelope.h:96-98, 145`).
- **FR-023** `noteOn(float frequencyHz, float velocity)`:
  1. `body_.setNoteFrequencyHz(frequencyHz)`; `cloud_.setFundamentalHz(frequencyHz)`;
  2. `cloud_.noteOn()` (`harmonic_cloud.h:635`) — which redraws phases **only when quiescent**
     (`:636-655`), making a sounding retrigger click-free by construction;
  3. `mse_.gate(true)`; in Growth mode `growth_.trigger()` — a no-op while already rising, so a retrigger
     **continues** rather than snapping back (`growth_envelope.h:158-166`), which is the roadmap's
     *"retriggerable with continuation (never snaps back)"* (line 124);
  4. velocity scales the **excitation** gain only (FR-010 step 3), alongside the envelope. It does not
     modulate any spectral parameter (nothing in the roadmap's Seraphis text assigns velocity a timbral
     role), and because it acts pre-body it colours how hard the body is struck rather than how loud the
     already-ringing tail is.
- **FR-024** `noteOff()`: `cloud_.noteOff()` (`harmonic_cloud.h:663`) and `mse_.gate(false)`. The body,
  atmosphere and orbit keep running — they are the tail (RA-2). `GrowthEnvelope` is **not** reset; a
  re-note during the tail continues its rise.

### E. `SeraphisVoice` — spatial position

- **FR-025** Per-voice spatial placement is two stages on the already-stereo voice bus (RA-3):
  1. **azimuth** — a **unity-at-centre** equal-power balance from `orbit_.getCurrentValue()` (x, clamped
     ±1, `orbit_modulator.h:236-238`) mapped to `panNorm = (x+1)/2`, with gains
     `gL = √2·cos(panNorm·π/2)` and `gR = √2·sin(panNorm·π/2)` applied to L and R respectively.
     The `√2` is load-bearing and is the correction RA-3 records: `StereoField::applyPan`
     (`stereo_field.h:665-681`) and `equalPowerGains` (`core/crossfade_utils.h:50-53`) distribute **one
     mono sample** across two outputs, so the bare `cos/sin` pair used as a *balance* on an already-stereo
     bus gives `cos(π/4) = sin(π/4) = 0.7071` on both channels at centre — a 3 dB attenuation, not unity.
     With the `√2` the centre gains are exactly 1.0 and the hard-pan gains are 1.414 (a +3 dB endpoint,
     the standard constant-power balance shape). `gL² + gR² = 2` at every position, so the law is still
     constant-power.
  2. **width** — `MidSideProcessor::setWidth` (`midside_processor.h:133`) driven **directly** from
     `orbit_.getY()` (`orbit_modulator.h:242-244`), linearly mapped from y ∈ [−1, +1] onto
     [`kMinVoiceWidthPct = 50.0f`, `kMaxVoiceWidthPct = 150.0f`] — a range **symmetric about 100 %**, so
     y = 0 gives exactly 100 % and FR-026's depth-0 claim holds by construction.
     **`getGrowth()` must not shape this axis.** A previous revision specified
     "`orbit_.getGrowth()`-shaped `orbit_.getY()`", which is wrong twice: `getGrowth()` is the *orbital
     decay/growth control* whose documented neutral is 0 (`orbit_modulator.h:177-180`, `:191`), so at the
     component default the width axis would be multiplied by zero and pinned constant for the whole
     render — silently defeating FR-027 on the width axis; and growth is *already* baked into the radius
     that `getY()` returns (`y = depth · r · sin(phi2)`, `orbit_modulator.h:25`, `:240-244`), so shaping
     by it again double-applies the same term. SC-016 asserts non-zero total variation on
     `getSpatialWidthPercent()` precisely so this cannot regress.

  The azimuth gains and the width percent are updated **once per control chunk** and smoothed; they are
  never stepped per sample from a raw modulator read.
- **FR-026** `void setSpatialDepth(float) noexcept` forwards to `OrbitModulator::setDepth` (`:185`);
  `setSpatialRate` → `setRate` (`:167`); `setSpatialCoupling` → `setCoupling` (`:173`);
  `setSpatialGrowth` → `setGrowth` (`:179`). At depth 0 both orbit axes read 0
  (`orbit_modulator.h:236-244`), so the voice sits **dead centre with exactly equal L/R gains of 1.0** and
  the width stage sits at exactly 100 %. The stage is **transparent to within 1e-6 per sample**, not
  bit-transparent: `MidSideProcessor` at 100 % computes `mid = (L+R)·0.5; side = (L−R)·0.5; out = mid ± side`
  (`midside_processor.h:196-207`), which is the algebraic identity but not bit-exact in IEEE float. The
  claim is stated as a measurable bound because that is what a test can assert.
- **FR-027** `orbit_.processBlock(n)` is advanced **every** control chunk, including while the voice is
  idle and contributing no audio, so per-voice spatial motion never stops — roadmap Key Design Decision 1:
  *"life modulators run free even with no notes held"* (lines 71–72).
  **The mechanism is an explicit cheap path, not the render path.** FR-051 skips idle voices, and a voice
  that is never rendered never reaches its own control loop, so the requirement needs its own entry point:
  `void advanceLifeOnly(std::size_t numSamples) noexcept` ticks `orbit_` (and any other free-running
  modulator the voice acquires) on the same absolute 64-sample grid as `processStereoBlock`, writes **no**
  samples anywhere, and touches no cloud/body/atmosphere state. It also **releases FR-033's level
  detector** on that same grid, feeding it a chunk peak of 0, so a skipped voice's `getCurrentLevel()`
  decays to zero instead of freezing at its last rendered value. Advancing a voice by `n` samples through
  `advanceLifeOnly` and through `processStereoBlock` must leave `getSpatialAzimuth()` and
  `getSpatialWidthPercent()` identical — the grid is absolute either way (FR-007). This follows the
  shipped `HarmonicCloud` idiom, which keeps a quiescent early-out that still calls `advanceDriftLanes(...)`
  and bumps `driftReadCount_` so *"a silent render and a sounding render of the same length leave identical
  lane state"* (`harmonic_cloud.h:893-903`). FR-051 calls `advanceLifeOnly` on every skipped voice.

### F. `SeraphisVoice` — engine parameter surface

- **FR-030** `SeraphisVoice` forwards, one-to-one and with no reinterpretation, the sub-component setters
  the macro matrix and Phase 9 need: cloud (`setRichness`, `setInharmonicity`, `setSpectralTiltDb`,
  `setMutation`, `setSpectralGravity`, `setDriftDepthCents`, `setStereoSpread`, `setAttackTimeSec`,
  `setDecayTimeSec`), morph (`setEntropy`, `setBloom`, `setTravelMode`, `setTargetPosition`,
  `setTravelRate`, `setState`, `setStateCount`), body (`setMaterial`, `setResonance`, `setDamping`,
  `setKeyTracking`, `setDrive`, `setMix`, `setCloudMix`, `setCloudDecaySec`, `setCloudSize`,
  `setCloudDamping`, `setWidth`), atmosphere (`setLevel`, `setBlur`, `setDensity`, `setGrainSeconds`,
  `setDriftDepth`, `setPanSpread`, `setDecorrelation`, `setFreezeMix`). Clamping stays in the owning
  component; the voice adds none.

  **Envelope forwarders are part of this list, not an omission.** FR-063 (Dissolve) drives envelope times,
  and without these there is no target to write to — the only other envelope-facing public methods on
  `SeraphisVoice` are `setEnvelopeMode` (FR-021) and `setGrowthDurationSeconds` (FR-022):
  - `void setEnvelopeStageTimeMs(int stage, float ms) noexcept` → `MultiStageEnvelope::setStage(stage,
    level, ms, curve)` (`multi_stage_envelope.h:166`) preserving that stage's current level and curve,
    with `ms` clamped by the component to [0, `kMaxStageTimeMs = 10000`] (`:65`, `:170`) and `stage`
    ignored outside `[0, kMaxStages)` (`:167`);
  - `void setEnvelopeReleaseMs(float ms) noexcept` → `MultiStageEnvelope::setReleaseTime` (`:206`), same
    10 s clamp (`:208`);
  - `[[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept` and
    `getEnvelopeReleaseMs()` so SC-009's Dissolve row and SC-010's inertness check can read them back.

  In Growth mode `setEnvelopeStageTimeMs` on a stage below `sustainPoint` is **stored but not applied**
  (FR-021 forces those to 0 ms); it takes effect on the return to Standard mode. This is stated so
  FR-063's Dissolve axis is understood to act on the Standard-mode envelope.
- **FR-030a (atmosphere freeze — the played technique)** Freeze is a first-class playing technique
  (roadmap line 74) and SC-001's normative scenario requires it, so it has an explicit API at **both**
  levels (Clarifications Q4):
  - **Per voice.** `void captureFreeze() noexcept`, `void releaseFreeze() noexcept` and
    `[[nodiscard]] bool isFreezeCaptured() const noexcept` on `SeraphisVoice`, forwarding one-to-one to
    `AtmosphereEngine::captureFreeze()` (`atmosphere_engine.h:909`), `releaseFreeze()` (`:928`) and
    `isFreezeCaptured()` (`:940`). All three are `noexcept` and allocation-free — `captureFreeze` writes
    into prepare-owned scratch only (`:904-908`) — so all three are callable from the audio thread.
  - **Engine-wide.** `void setAtmosphereFreeze(bool) noexcept` on `SeraphisEngine` **fans out to every
    prepared voice** (all `kMaxVoices`, per FR-041), calling `captureFreeze()` on each when set and
    `releaseFreeze()` on each when cleared, and **latches the state**:
    `[[nodiscard]] bool getAtmosphereFreeze() const noexcept` reads it back.
  - **Voices that start while freeze is engaged are covered.** `AtmosphereEngine::captureFreeze()` is a
    documented no-op until its ring holds a whole analysis window (`:911-916`), and a voice that was just
    `reset()` by the FR-047 steal path has an empty ring, so a single fan-out call at
    `setAtmosphereFreeze(true)` time would leave later voices unfrozen. The engine therefore keeps a
    per-voice **freeze-pending** flag: while the latched state is `true` and that voice's
    `isFreezeCaptured()` is `false`, the engine calls that voice's `captureFreeze()` **once per 64-sample
    control chunk**, which costs only the cheap early-out until the ring has filled, and stops as soon as
    the capture succeeds. `reset()`, `silence()` and a steal re-arm the flag for that voice.
  - Freeze changes what the atmosphere costs — 1.440 %/voice frozen against 1.048 % unfrozen (RA-1) —
    which is why SC-001 measures **with freeze captured** and states it in its scenario.
- **FR-031** Every forwarder listed above is safe to call while the voice is sounding, **with the
  configure-time exclusions below**. No forwarder reallocates.
  Methods documented as configure-time-only are **not** forwarded:
  - `ContinuousBody::setSeed` (`continuous_body.h:1117-1124`) — FR-017 owns it;
  - `SpectralMorphEngine::setState` **and** `setStateCount`. The header carries a boxed contract:
    *"CONFIGURATION-TIME CALLS: prepare(), reset(), setSeed(), setState() and setStateCount() are NOT to be
    called while the consumer is sounding"* (`spectral_morph_engine.h:198-207`), and `setState`
    additionally arms an absorption fade when the slot contributes to the output (`:288-291`). A previous
    revision listed both in FR-030's freely-callable set, which contradicts that contract. They are
    reachable only from `SeraphisVoice::prepare` (FR-019a authors the two slots there); the public
    forwarders `setSpectralState(int slot, const SpectralState&)` and `setSpectralStateCount(int)` exist
    for Phase 9 but **reject the call while `!isFinished()`** and report the rejection through
    `getRejectedConfigureTimeCallCount()` (FR-085) so a mis-sequenced caller is visible rather than
    silently clicking.
  - `SpectralMorphEngine::setTargetPosition` (`:348`) is **not** in that list and stays freely callable —
    FR-062's Bloom row depends on it.

### G. `SeraphisVoice` — tail, quiescence and silence

- **FR-032** `[[nodiscard]] bool isFinished() const noexcept` is true when **all** hold: the envelope is
  not active (`MultiStageEnvelope::isActive()`, `:251`), the cloud is quiescent
  (`HarmonicCloud::isQuiescent()`, `:1040`), and the voice's own output has stayed below
  `kTailSilenceThreshold` for at least **four consecutive control chunks** (5.33 ms @ 48 kHz — one chunk
  can be a zero crossing of a still-loud tail). It is the *audio* test RA-2 requires, not an
  envelope-time test.
  **`static constexpr float kTailSilenceThreshold = 1.0e-5f`** — linear peak, i.e. **−100 dBFS**.
  Derivation: the output stage's ceiling is `TruePeakLimiter::kDefaultCeilingDb = −1.0 dBFS`
  (`true_peak_limiter.h:46`), so −100 dBFS is 99 dB below anything the engine can emit and ~4.5 dB below
  the least-significant bit of 16-bit audio (−96.3 dBFS). A voice at or under it cannot be audible in the
  sum however many voices are at it, and it is two decades above the `float` denormal region so FTZ/DAZ
  cannot make the test unreachable. It is measured on `getCurrentLevel()` (FR-033), i.e. **pre**-sum-gain
  and pre-output-stage, so the threshold does not move with polyphony.
  **Retirement latency is therefore derived, not free:** with FR-033's 100 ms release the detector needs
  `kLevelReleaseMs · ln(1 / kTailSilenceThreshold) = 0.1 s × 11.51 ≈ 1.15 s` to fall from full scale to
  the threshold, plus the four chunks (5.33 ms), so a voice is retired **≤ ~1.16 s** after its audio
  actually goes silent. Every criterion that waits for reclaim (SC-012) is sized against that figure.
- **FR-033** `[[nodiscard]] float getCurrentLevel() const noexcept` returns the voice's level from a
  **single, fully specified detector** — the one FR-032's retirement, FR-045's steal ordering, FR-046's
  amnesty, SC-011 and SC-012 all read (Clarifications Q7):
  1. **Statistic:** the **absolute peak** of the voice's stereo output — `max(|L|, |R|)` over the
     samples — accumulated across each **64-sample control chunk** (FR-007's absolute grid), taken on the
     voice bus **after** the spatial stage, i.e. exactly what the voice contributes to the sum, but
     **before** the engine's sum gain (FR-052) and the output stage, so it does not move with polyphony.
  2. **Ballistics:** that chunk peak is fed into a one-pole with **instant attack** (a chunk peak above
     the current value replaces it outright) and an exponential release with
     **`static constexpr float kLevelReleaseMs = 100.0f`**. Instant attack is what makes a newly loud
     voice immediately ineligible for the quietest steal; the 100 ms release is what stops the steal
     victim depending on where in a partial's cycle a chunk boundary fell, which is what SC-011's "known,
     distinct levels" needs in order to be pinnable at all.
  3. **Update cadence:** the value is updated **once per control chunk, at the chunk boundary**, and is
     otherwise constant. It is not recomputed per sample and never depends on the caller's block
     partition, so SC-014 is unaffected.
  4. Peak, not RMS: FR-032's −100 dBFS and FR-046's −30 dBFS thresholds are both derived from the
     `TruePeakLimiter` ceiling, which is a peak quantity; an RMS detector would make both derivations
     invalid.

  A voice that is not rendering (idle and skipped by FR-051) still has its detector released on the same
  grid, so it decays to 0 rather than freezing at its last value. `getCurrentLevel()` is `const`,
  allocation-free and `noexcept`.
- **FR-034** `void silence() noexcept` **arms** a fixed short anti-click ramp and then hard-clears every
  sub-component. The ramp is **`static constexpr float kSilenceRampMs = 1.0f`** — 48 samples
  @ 48 kHz, 44 @ 44.1 kHz. It is deliberately **shorter than one 64-sample control chunk at every
  supported rate**, which is what makes FR-047's `silence()` → `reset()` → `noteOn()` sequence
  completable *within one block* rather than spanning blocks; a longer ramp would make the steal path
  stateful across calls and SC-003 would be measuring a two-block transition. 1 ms is also the shortest
  ramp that keeps the fade itself out of the click régime: it is ~4× the shortest audible
  discontinuity window and the fade is applied as a linear gain over a signal already at the tail of its
  release. Because `AtmosphereEngine::silence()` **latches** (`:641-643`),
  `SeraphisVoice::silence()` is defined to be followed by a reset entry point before the voice is
  reused — the engine's steal path (FR-047) does exactly that, in that order.

  **The ramp is ARMED, not rendered, and the distinction is forced by when a steal happens.**
  *(Amended 2026-07-31; a previous revision said "fades the voice out over a fixed short ramp and then
  hard-clears".)* A steal is issued **between** blocks, from `SeraphisEngine::noteOn`, so at the moment
  `silence()` runs there are **no samples for a fade to occupy** — every sample of the current block has
  already been served. `silence()` therefore captures the last sample pair the caller actually received,
  clears every sub-component immediately, and the captured pair is added, decaying linearly to zero over
  `kSilenceRampMs`, to the first samples the voice renders afterwards. The audible result is exactly the
  ramp this FR asks for — the output leaves the victim's last value continuously instead of stepping to
  the new note's ~0 onset — and it is what SC-003's positive control (b) measures: with the ramp length
  forced to 0 the same render's worst steal-window per-sample delta is **13.7×** its own reference
  maximum, against **1.10×** with the shipped 1 ms ramp.
- **FR-035** `[[nodiscard]] bool stateFinite() const noexcept` aggregates the sub-components' own
  finiteness probes where they exist (`ContinuousBody::stateFinite()` `:1328`,
  `SpectralMorphEngine::stateFinite()` `:456`) and checks the voice's own accumulators otherwise. A voice
  that reports non-finite is recovered by `reset()`; it never propagates a non-finite sample to the
  engine bus (FR-072).

### H. `SeraphisEngine` — pool and allocation

- **FR-040** `SeraphisEngine` is a Layer 3 system in `systems/seraphis_engine.h`. It owns
  `std::array<SeraphisVoice, kMaxVoices>` with `static constexpr std::size_t kMaxVoices = 16` (the
  roadmap's upper bound, line 290) and one `VoiceAllocator`. The **shipped default polyphony is 8**
  (RA-1); `void setPolyphony(std::size_t) noexcept` clamps to `[1, kMaxVoices]` and forwards to
  `VoiceAllocator::setVoiceCount` (`:326`). **8 is settled**, ruled on the worst-case gate on 2026-07-30
  (Clarifications Q5 / RQ-1) with the ≤ 25 % ceiling kept; it is not a placeholder.

  **What shrink actually returns, and what the engine does with it.** A previous revision called these
  *"`Steal`-shaped shrink events"* handled by FR-047, "so the dropped voices fade rather than cut". The
  shipped code does neither: `setVoiceCount` pushes `VoiceEvent::Type::NoteOff` (`voice_allocator.h:340-346`)
  and, in the same loop, **force-idles** each excess slot — `state = Idle`, `note = -1`, `velocity = 0`,
  `frequency = 0`, `activeVoiceCount_` decremented (`:347-352`). Running FR-047 on a `NoteOff` event would
  be a hard cut with no new note to start, and treating the slot as still-releasing would contradict the
  allocator, which already believes it is idle. The engine therefore:
  1. treats each returned `NoteOff` event as a **musical release**: `voices_[i].noteOff()` (FR-024), not a
     steal, not a `silence()`;
  2. **keeps rendering** slots at or above the new polyphony until their own `isFinished()` is true, so a
     30 s body tail decays naturally. Those slots are counted by `getRenderingVoiceCount()` but not by
     `getActiveVoiceCount()`, and they are not eligible for allocation (the allocator already excludes
     them);
  3. does **not** call `voiceFinished` on them — the allocator has already idled the slot, and
     `voiceFinished` would early-out anyway (`:288-292`);
  4. **guards the reuse of such a slot.** Because the allocator believes those slots are `Idle`, a later
     `noteOn` can be allocated onto one while `SeraphisVoice::isFinished()` is still `false` and a 30 s
     tail is still rendering. FR-042 therefore requires the **FR-047 teardown**
     (`silence()` → `reset()` → `noteOn()`) on any dispatched `NoteOn` whose target voice is not finished,
     so a reused slot always starts clean instead of layering a new note over an orphaned tail
     (Clarifications Q8). This is the *only* case in which a plain `NoteOn` event triggers the steal
     sequence; a same-note retrigger on a **live** voice does not (FR-042, Edge Case 14).

  This keeps FR-044's promise ("a voice whose tail is still audible is never returned to the idle pool")
  honest in the only sense the engine controls — it never *stops rendering* an audible tail — while
  accepting that the allocator's own `VoiceState` says `Idle` the moment `setVoiceCount` returns. SC-012
  is written against `getRenderingVoiceCount()`/`SeraphisVoice::isFinished()` for exactly this reason and
  **exempts the allocator's state during a polyphony shrink**, which is stated there.
- **FR-041** Only voices below the current polyphony are prepared and rendered. Raising polyphony after
  `prepare` must not allocate: `prepare` prepares **all `kMaxVoices` slots** and `setPolyphony` only
  changes how many are summed. The memory consequence is stated in RA-8 and priced at `kMaxVoices`, not at
  the default.
- **FR-042** `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` and
  `void noteOff(std::uint8_t note) noexcept` drive `VoiceAllocator` and dispatch its `VoiceEvent` span
  exactly as `PolySynthEngine::dispatchPolyNoteOn` does (`poly_synth_engine.h:600-628`): `NoteOn` →
  `voice.noteOn(event.frequency, velocity/127)`, `NoteOff` → `voice.noteOff()`, `Steal` → FR-047.
  Velocity 0 is a note-off (the allocator already maps it, `:230-233`).
  **One qualification on the `NoteOn` case** (Clarifications Q8): if the target voice's `isFinished()` is
  `false` **and** the allocator reports its state as `Idle` — the orphaned-tail situation FR-040's
  polyphony shrink creates — the engine runs FR-047's `silence()` → `reset()` → `noteOn()` sequence on it
  instead of a bare `noteOn()`. Every other `NoteOn`, including a retrigger of a live `Active` or
  `Releasing` voice, is dispatched as a plain `voice.noteOn(...)`: with `RetriggerMode::Legato` (FR-020)
  the envelope continues from its current level and `HarmonicCloud::noteOn()` preserves phases while
  non-quiescent (`harmonic_cloud.h:604-606`), so the organism keeps its state and no click occurs.
- **FR-043** `VoiceAllocator` runs at `AllocationMode::Oldest` and `StealMode::Hard`. The mode is not
  exposed; the engine's own selection (FR-046) is what determines *which* voice is stolen.
- **FR-044** `voiceFinished(i)` is called on the allocator **only** after `voices_[i].isFinished()`
  becomes true, evaluated once per block after rendering — the deferred discipline at
  `poly_synth_engine.h:810-813`. A voice in `VoiceState::Releasing` whose tail is still audible is never
  returned to the idle pool.
- **FR-045** When `noteOn` arrives with no idle voice, the engine selects the victim itself and **frees
  that slot before** calling `allocator_.noteOn`, by the mechanism RA-4 specifies (it is the only one the
  shipped allocator surface permits). Selection is **quietest-with-amnesty** (roadmap line 290):
  1. Consider only voices in `VoiceState::Releasing` (allocator `:424`) — these are the amnesty
     candidates;
  2. Among them, pick the lowest `getCurrentLevel()` (FR-033);
  3. If no voice is `Releasing`, consider all `Active` voices and pick the lowest `getCurrentLevel()`;
  4. Ties break on the older allocator timestamp, preserving `Oldest` semantics.

  Freeing the chosen slot `v`: for step 3 (an `Active` victim) the engine first discards
  `allocator_.noteOff(getVoiceNote(v))` (`:257`, `:406`) to reach `Releasing`; then, for every case, it
  discards `allocator_.voiceFinished(v)` (`:288`) to reach `Idle`. Only then does it call
  `allocator_.noteOn(note, velocity)`. **The engine asserts that the returned `NoteOn` event's
  `voiceIndex == v`** and records the outcome in `getLastStolenVoiceIndex()`; a mismatch is a defect, not
  a fallback, and SC-011 checks it.
- **FR-046 (amnesty)** A `Releasing` voice whose `getCurrentLevel()` — FR-033's chunk-peak detector with
  instant attack and a 100 ms release — is at or above
  `kAmnestyLevelThreshold` is **skipped** in step 1 unless no candidate is below it — a 30 s body tail at
  audible level is not free to steal. This is RA-2's replacement for the roadmap's "releases are 10 s+"
  heuristic, and it is measured on audio rather than assumed from an envelope time.
  **`static constexpr float kAmnestyLevelThreshold = 0.0316f`** — linear peak, i.e. **−30 dBFS** on
  `getCurrentLevel()`, which is the same pre-sum-gain scale as `kTailSilenceThreshold` (FR-032) so the two
  are directly comparable and neither moves with polyphony. Derivation: −30 dBFS is the conventional
  "clearly audible in a mix" floor and sits 70 dB above `kTailSilenceThreshold`, giving the amnesty a wide
  band in which a decaying tail is protected; a threshold defined *relative to the loudest sounding voice*
  was rejected because it makes the steal decision depend on unrelated voices and therefore on note order,
  which SC-011 could not pin. If **every** `Releasing` candidate is at or above it, the quietest is still
  taken (Edge Case 15).
- **FR-047** A steal is: `voices_[i].silence()`, then `voices_[i].resetForSteal()`, then
  `voices_[i].noteOn(...)`, in that order and within the same block. **The middle call is
  `resetForSteal()` and NOT `reset()`** *(amended 2026-07-31; a previous revision said `reset()`)*: the
  two entry points differ in exactly one thing — `reset()` clears the FR-034 anti-click tail,
  `resetForSteal()` preserves it — and clearing it here would discard the ramp one line after arming it,
  which is the click SC-003 measures. `SeraphisEngine::reset()` and `::silence()` use the
  tail-clearing `reset()` precisely because FR-055 requires the block after them to be exactly 0. **The same sequence is required, on
  the same terms, for a `NoteOn` dispatched onto a slot the allocator force-idled during a polyphony
  shrink while `isFinished()` is still false** (FR-040 step 4, FR-042, Clarifications Q8) — it is the one
  non-steal path that uses it. The order is load-bearing —
  `AtmosphereEngine::silence()` latches and `reset()` is its documented only re-entry
  (`atmosphere_engine.h:641-643`).

### I. `SeraphisEngine` — seed spread, render, output stage

- **FR-050** At `prepare`, every voice `v` is seeded with `deriveStreamSeed(engineSeed, kVoiceSaltBase + v)`
  (`core/random.h:102`), so no two voices drift identically (roadmap line 292). `void setSeed(std::uint32_t)`
  re-spreads. `kVoiceSaltBase` is `static constexpr` and documented not to overlap any voice-internal salt
  range (FR-016).
- **FR-051** `void processStereoBlock(float* outLeft, float* outRight, std::size_t numSamples) noexcept`
  produces **the voice sum and nothing else**: sum the prepared, sounding voices into a fixed-size stereo
  bus, apply the FR-052 sum gain, write the result out. It is **pre-reverb and pre-output-stage** — the
  reverb is Layer 4 and the engine does not own it (FR-070), and the output stage is a separate entry
  point (FR-053a) so the caller can apply it to the reverb *return*. Same guard order as FR-006.
  A previous revision had this method also run the Aether stage and the output stage, which FR-070
  forbids; that wording is withdrawn and every SC now names which entry point (or the composed chain) it
  measures.
  **Idle voices are skipped from the audio path but not from time** — a voice with `VoiceState::Idle` and
  `isFinished()` contributes no samples and none of its cloud/body/atmosphere runs, which is what makes
  the 8-voice budget an 8-voice *worst case* rather than a floor. Every skipped voice instead receives
  `advanceLifeOnly(numSamples)` (FR-027), so its `OrbitModulator` keeps running and roadmap Key Design
  Decision 1 holds. `advanceLifeOnly` writes no samples and touches no generator state; its cost is
  bounded by one `OrbitModulator::processBlock` per voice per block (`orbit_modulator.h:216-229`,
  O(control steps)) and is **inside** SC-001's measurement, which renders with all voices sounding and
  therefore takes the more expensive path for every slot.
- **FR-052** Voice-sum gain is `1/√n`, matching the population-compensation law Phase 5 uses on the grain
  bus (`specs/seraphis-phase5-atmosphere/spec.md:61-64`, Q4). It is a single multiply on the summed stereo
  bus.
  **`n` is the current polyphony** — the value `setPolyphony` last set (FR-040), clamped to
  `[1, kMaxVoices]` — **not** the number of voices currently rendering (Clarifications Q6). Consequences,
  all intended:
  - the gain is **static between `setPolyphony` calls**, so the mix does not swell as tails retire. Under
    FR-010's cloud-only envelope a released voice can ring for up to 30 s, so a rendering-count `n` would
    make the bus level drift audibly for half a minute after every chord;
  - the gain target changes **only inside `setPolyphony`**, never on a note event, so SC-014's 1-sample
    and 512-sample partitions can never update it at different times. The one-pole that ramps to a new
    target advances on the **absolute 64-sample control grid** (FR-007) for the same reason, with a named
    `static constexpr float kSumGainSmoothMs`;
  - the accepted cost is **dynamic range**: a single note at polyphony 8 renders `1/√8` (−9 dB) below the
    same note's contribution at polyphony 1. This is accepted deliberately — the limiter (FR-054) and the
    Aether stage see a bus whose scale does not depend on how many tails happen to be alive.

  At polyphony 1 the gain is exactly 1 (Edge Case 7).
- **FR-053a (the output-stage entry point)**
  `void processOutputStage(float* left, float* right, std::size_t numSamples) noexcept` applies FR-053 and
  FR-054 **in place** on a caller-supplied stereo buffer, which in the composed chain is the
  `AetherReverb` return. It is the second of `SeraphisEngine`'s two entry points, it is listed in FR-085's
  surface, and it is what makes the roadmap's *"voice-sum → `AetherReverb` → output stage"* (line 293)
  expressible under the layer rule. It is `noexcept`, allocation-free, and carries FR-006's guard order.
  Calling `processOutputStage` on a buffer the engine did not produce is the **intended** usage, not a
  misuse; the engine keeps no cross-call state that assumes otherwise beyond the saturator's and
  limiter's own.
- **FR-053** Output saturation is one `TapeSaturator` per channel (it is mono and in-place,
  `tape_saturator.h:335`) at **low drive** (roadmap line 293, and Key Design Decision 4: *"No aggressive
  distortion (that belongs to Disrumpo)"*, line 78). Shipped defaults: drive 0 dB, saturation 0.15,
  mix 1.0, all inside the setters' own clamps (`:239`, `:248`, `:266`). `void setOutputSaturation(float)`
  exposes the saturation amount only; the drive is not user-exposed at Layer 3. It runs inside
  `processOutputStage` (FR-053a), never inside `processStereoBlock`.
- **FR-054** Output safety is one `TruePeakLimiter` (stereo, in-place, `true_peak_limiter.h:104`) at
  `kDefaultCeilingDb = -1.0f` (`:46`). It is **always last inside `processOutputStage`** and is not
  bypassable. Note precisely what this does and does not promise: it bounds the output of
  `processOutputStage`, which in the composed chain is the instrument's final output. It says nothing
  about `processStereoBlock`'s voice sum, which is an intermediate signal and is **not** limited — SC-015
  is asserted on the composed chain for that reason.
- **FR-055** `void reset()` and `void silence()` exist on the engine and cover **the voices and the output
  stage only** — the engine does not own the reverb (FR-070), so silencing `AetherReverb` is the caller's
  call and the FR-070 helper makes it. `SeraphisEngine::silence()` follows the FR-047 order for every
  voice (`silence()` then `reset()`, because `AtmosphereEngine::silence()` latches, `:641-643`) and clears
  the `TapeSaturator`/`TruePeakLimiter` state. For the caller's benefit the asymmetry is stated here:
  `AetherReverb::silence()` resumes on its own (`aether_reverb.h:2140-2143`) and needs no paired
  `reset()`.

### J. `SeraphisMacroMatrix`

- **FR-056** `SeraphisMacroMatrix` is a Layer 3 system in `systems/seraphis_macro_matrix.h`. It holds five
  `float` knob values in [0, 1] with `enum class SeraphisMacro : std::uint8_t { Dream = 0, Bloom,
  Dissolve, Gravity, Entropy, Count }`. It has **two** application surfaces, because its targets have two
  different owners:
  - `void apply(SeraphisEngine&) const noexcept` — every `Voice`- and `Engine`-owned row (cloud, morph,
    body, atmosphere, envelope, spatial), applied through `SeraphisEngine`'s and `SeraphisVoice`'s
    forwarders;
  - `[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept` — every `Aether`-owned
    row, returned as a POD of plain `float`s: `{mix, size, width, shimmerOctaveSend, shimmerFifthSend,
    bloomSend, sizeBreathDepth, dimensionalityTideDepth}`. The Layer-4-aware caller — the FR-070 test
    helper in this phase, `plugins/seraphis/`'s processor from Phase 8 — pushes each field into the
    matching `AetherReverb` setter.

  **Why not one call.** A previous revision specified `apply(SeraphisEngine&)` as the *only* surface while
  FR-061/062/064 named eight `AetherReverb` setters. Those are unreachable from a `SeraphisEngine&`
  (FR-070: the engine does not own a reverb) and, worse, unnameable from this header at all —
  `AetherReverb` is Layer 4 (`effects/aether_reverb.h:8`) and `seraphis_macro_matrix.h` is Layer 3, so it
  may not include the type. Returning a POD of floats keeps the mapping — which is the roadmap-normative
  part (lines 303–309) — entirely inside Phase 7 and testable without a reverb, while the push is one
  trivial, layer-legal line in the caller. Taking `AetherReverb&` as a second `apply` parameter was
  rejected for the same include reason.
- **FR-057** Each macro maps to a fixed target list. Every target is `base + macro · amount`, where
  `base` is the **FR-019 shipped voice default** (not the sub-component's constructor default), shaped by
  a per-target `ModCurve` through the shared Layer 0 `applyModCurve` (`core/modulation_curves.h:38`),
  summed across macros where two macros hit the same target, then clamped to the target setter's own
  documented range. Summation-then-clamp is `ModulationEngine`'s order (`modulation_engine.h:44-54`) and
  is kept so Phase 9 inherits familiar semantics.
  **Permitted curves are `Linear`, `Exponential` and `SCurve` only.** `ModCurve::Stepped` is excluded
  (RA-7): it is `std::floor(x · 4) / 3` (`modulation_curves.h:53-54`), which over SC-009's 21-step sweep
  produces 18 zero-change steps and 3 jumps of ~1/3 — a jump/mean-step ratio of ~6.7×, failing SC-009's
  3× continuity bound by construction. `Exponential` (x², `:46`) is the steepest permitted curve and
  peaks at ~1.95× the mean step over 21 steps, which is where SC-009's 3× factor comes from.
  **Two macros sharing one target is specified, not accidental.** `HarmonicCloud::setRichness` is the
  worked case: Bloom contributes `+amount_B · curve(bloom)` and Gravity contributes
  `−amount_G · curve(gravity)`, both added to the FR-019 base of 0.60, then clamped to [0, 1]
  (`harmonic_cloud.h:416`). At Bloom = Gravity = 1 they partially cancel; that is the sum, and Edge Case 9
  covers it. The same applies to `setSpectralTiltDb` (Bloom ↑, Gravity ↓) and to
  `SpectralMorphEngine::setEntropy` (Dream ↓, Entropy ↑).
- **FR-058** The mapping is **data, not code**: a `static constexpr` table of
  `{macro, owner, target, amount, curve}` rows, so a test can assert the table's contents and a render
  test can assert each macro's audible axis independently. **`owner` is a
  `SeraphisMacroTargetOwner { Voice, Engine, Aether }`** and it is what routes a row to `apply()` or to
  `computeAetherTargets()`. A row with no owner, or an `Aether` row absent from
  `SeraphisAetherTargets`, is a compile-time error via a `static_assert` over the table — no row may be
  unreachable. The table additionally carries each target's `base` (the FR-019 value) so SC-010 can assert
  inertness against the table itself.
  **`amount` and `curve` are per-row implementation tuning, not pinned in this spec** (Clarifications Q3).
  What constrains them is not left open: each row's **direction** is normative (FR-061…FR-065), the
  permitted curves are the three FR-057 names, and **SC-009 now carries a per-macro minimum end-to-end
  effect size** so a row tuned so small that the macro is inaudible fails the gate — which a
  scale-invariant Spearman ρ alone could never catch. The checked-in table is the record of the tuning.
  **`amount` is signed.** A row may be negative (a macro that lowers its target), and **every Gravity row
  is signed about the neutral**: Gravity's contribution is `amount · curve(|g|) · sign(g)` where
  `g = (gravity − 0.5) · 2 ∈ [−1, +1]` (FR-064), so one row expresses both the *air* and the *stone* half
  of the axis and both halves have travel from the FR-019 base by construction.
- **FR-059** `apply()` and `computeAetherTargets()` are real-time safe and idempotent. Calling them every
  block with unchanged knobs must not step any parameter (the underlying setters' smoothers do the work;
  the matrix adds none).
- **FR-060** Every knob defaults to its **documented neutral**: Dream, Bloom, Dissolve and Entropy default
  to **0**; **Gravity defaults to 0.5**, which is its neutral centre (FR-064 — it is the one axis the
  roadmap defines as bipolar, air ↔ stone). At all knobs neutral the matrix must be **inert**: `apply()`
  and `computeAetherTargets()` on a freshly prepared engine leave every target at exactly the FR-058
  table's `base` — i.e. the FR-019 shipped voice default for voice/engine rows and the component default
  for Aether rows — so a Seraphis with untouched macros sounds exactly like the FR-019 defaults.
  A previous revision said "all five knobs default to 0" *and* "0.5 is Gravity's neutral centre", which
  cannot both hold: at Gravity = 0 the matrix would have to move `setRichness`, `setDamping`, `setSize`
  and `setSpectralTiltDb` to full "air" and would not be inert. The neutral formulation resolves it, and
  SC-009 correspondingly holds each non-swept macro at **its own** neutral, not at 0.

The five mappings (roadmap lines 303–309). **Direction is normative**; the exact amounts and curves are
pinned in the FR-058 table at implementation time (Clarifications Q3) and are gated by SC-009's
monotonicity clause **and its per-macro minimum effect size**, so "tuned later" never means "tuned to
inaudible".

- **FR-061 Dream** — *harmonic purity ↑, reverb send ↑, life-mod depth ↑, entropy ↓*.
  Owner `Voice`: `HarmonicCloud::setInharmonicity` ↓ (from the FR-019 base of 0.030 → 0),
  `setSpectralGravity` → 0 (from +0.20), `setMutation` ↓ (from 0.25 → 0);
  `SpectralMorphEngine::setEntropy` ↓ (from 0.20 → 0); `SeraphisVoice::setSpatialDepth` ↑ (from the
  FR-019 base of **0.35** toward 1.0 — at `OrbitModulator`'s own default of 1.0, which is `setDepth`'s
  clamp maximum (`orbit_modulator.h:123`, `:184-186`), this row would be a no-op, which is why FR-019
  moves it).
  Owner `Aether` (through `SeraphisAetherTargets`): `mix` ↑, `sizeBreathDepth` ↑,
  `dimensionalityTideDepth` ↑.
  **`HarmonicCloud::setDriftDepthCents` is deliberately NOT a Dream target.** A previous revision listed
  it ↑ under "life-mod depth ↑". Drift depth is a per-partial detune in cents written straight onto
  partial frequency (`harmonic_cloud.h:505-509`, read per partial at `getPartialDriftDetune` `:991`), so
  raising it *increases* deviation from the harmonic grid — directly against Dream's own "harmonic purity
  ↑" and against SC-009's Dream metric. Dream's four purity rows would have been fighting its own fifth
  row, and whether the sweep passed would have depended on unstated relative amounts. Drift depth belongs
  to Entropy (FR-065) and stays there; roadmap line 303's "life-mod depth ↑" is carried by spatial depth
  plus the reverb's breath and tide depths, which move no partial frequency.
- **FR-062 Bloom** — *upper partials ↑, shimmer send ↑, stereo width ↑, morph toward brighter state*.
  Owner `Voice`: `HarmonicCloud::setSpectralTiltDb` ↑ (toward the `kMaxTiltDbPerOct = +12` end,
  `harmonic_cloud.h:195`); `setRichness` ↑ (from the FR-019 base of **0.60** — at the component default of
  1.0 this row would be a no-op, since `setRichness` clamps at 1.0, `:416`); `setStereoSpread` ↑;
  `SeraphisVoice` width ↑; `SpectralMorphEngine::setTargetPosition` → **slot 1**, the `Glass` state
  authored by FR-019a (amplitude law `n^-0.5` against slot 0's `n^-1`, `spectral_state.h:365-368`).
  Without FR-019a all four slots are `SineStack` (`spectral_morph_engine.h:214-219`) and this row morphs
  SineStack → SineStack, i.e. nothing.
  Owner `Aether`: `shimmerOctaveSend` ↑, `shimmerFifthSend` ↑, `bloomSend` ↑, `width` ↑.
- **FR-063 Dissolve** — *atmosphere mix ↑, spectral blur ↑, transient definition ↓, envelope slew ↑*.
  Owner `Voice`: `AtmosphereEngine::setLevel` ↑ (from the FR-019 base of **0.5** toward the setter's
  `kMaxLevel = 2` ceiling, `atmosphere_engine.h:944-949` — a 4× trim range, which is what makes the
  atmosphere *arrive* on this macro rather than merely being trimmed) and `setBlur` ↑;
  `HarmonicCloud::setAttackTimeSec` ↑;
  envelope slew via FR-030's **named** forwarders —
  `SeraphisVoice::setEnvelopeStageTimeMs(0, …)` ↑, `setEnvelopeStageTimeMs(1, …)` ↑ and
  `setEnvelopeReleaseMs` ↑, each bounded by `MultiStageEnvelope::kMaxStageTimeMs = 10000`
  (`multi_stage_envelope.h:65`, clamped at `:170` and `:208`). A previous revision cited "FR-030's
  forwarders" for the envelope rows when FR-030 contained none; the three names above are now in FR-030.
  No `Aether` rows.
- **FR-064 Gravity** — *air ↔ stone density axis* (roadmap line 307). It is the one **bipolar** macro:
  **0 = air, 0.5 = neutral, 1 = stone**, and its knob therefore **defaults to 0.5** (FR-060), not to 0.
  Every row is driven by the **signed** deviation `g = (gravity − 0.5) · 2 ∈ [−1, +1]` as
  `amount · curve(|g|) · sign(g)` (FR-058), so at 0.5 every row contributes exactly zero and the matrix is
  inert (SC-010), and **each row travels in both directions** from its FR-019 base.
  Owner `Voice`: `HarmonicCloud::setRichness` ↓ toward stone / ↑ toward air (from the FR-019 base of
  0.60, which is why that base is below the clamp maximum); `ContinuousBody::setDamping` ↑ toward stone /
  ↓ toward air (from the FR-019 base of **0.25** — at `kDefaultDamping = 0.0f`, which *is* `kMinDamping`
  (`continuous_body.h:126`, `:128`), the air half would have zero travel, which is why FR-019 moves it);
  `HarmonicCloud::setSpectralTiltDb` ↓ toward stone / ↑ toward air (tilt darkening, symmetric about the
  FR-019 base of 0.0).
  Owner `Aether`: `size` ↑ toward stone.
- **FR-065 Entropy** — *direct wire to the Phase 3 `EntropyProcessor` + drift depths*. Owner `Voice`:
  `SpectralMorphEngine::setEntropy` ↑ (the table's near-identity row, offset by the FR-019 base of 0.20
  and clamped at 1.0), `HarmonicCloud::setDriftDepthCents` ↑ (from 0 toward `kMaxDriftCents = 50`,
  `harmonic_cloud.h:214`), `AtmosphereEngine::setDriftDepth` ↑. No `Aether` rows.

### K. Aether integration

- **FR-070** `AetherReverb` is **Layer 4** (`effects/aether_reverb.h:2,8`) and a Layer 3 header may not
  include it. `SeraphisEngine` therefore does not own it. The composition is expressed as **two engine
  entry points plus a caller**:
  - `SeraphisEngine::processStereoBlock(l, r, n)` (FR-051) produces the **voice sum**, post-`1/√n`,
    **pre-reverb**;
  - the caller runs `AetherReverb::processStereoBlock` (`aether_reverb.h:2164`) on that bus;
  - `SeraphisEngine::processOutputStage(l, r, n)` (FR-053a) applies `TapeSaturator` + `TruePeakLimiter`
    **in place on the reverb return**. This is the method the previous revision of this FR referred to
    without naming, and its absence made FR-054 and SC-015 unassertable;
  - the engine additionally exposes `void collectHeldPartials(std::size_t voiceIndex, float* dest,
    std::size_t capacity, std::size_t& outCount) const noexcept` (FR-071) and the per-voice note
    lifecycle hooks a caller needs to drive `bloomNoteOn`/`bloomNoteOff`, and
    `computeAetherTargets()`'s POD from the macro matrix (FR-056) for the caller to push.
  - the whole composition **voice sum → `AetherReverb` → `processOutputStage`** is realised in this
    phase's test helper `tests/test_helpers/seraphis_chain.h` (`renderSeraphisChain(...)`) and, from
    Phase 8, in `plugins/seraphis/`'s processor. Every SC that measures "the composed chain" drives that
    helper; SCs that name `processStereoBlock` or `processOutputStage` measure exactly that method's
    output and nothing else.

  > This is the one structural deviation from the roadmap's Phase 7 text (line 293 implies the engine owns
  > the reverb). It is forced by the layer rule, which is a cross-cutting constraint (roadmap line 496)
  > and outranks the phrasing. The alternative — promoting `SeraphisEngine` to Layer 4 — was rejected
  > because `SeraphisVoice` must stay Layer 3 and a Layer 4 engine composing Layer 3 voices would put the
  > instrument core in the effects tree.
- **FR-071** Harmonic bloom follows the held chord (roadmap lines 276–278). On `SeraphisVoice::noteOn`
  the engine records the voice's partial frequencies — read from `HarmonicCloud::getPartialFrequencyHz(i)`
  for `i < getActivePartialCount()` (`:950`, `:955`) — and exposes them through `collectHeldPartials` for
  `AetherReverb::bloomNoteOn(voiceId, partialHz, count)` (`:2392`), using the **voice index as `voiceId`**.
  `bloomNoteOff(voiceId)` is issued when the voice is stolen or finished. `voiceId < 0` is rejected by that
  API (`:2394`), and `kMaxBloomVoices = 8` (`:1445`) — at a polyphony above 8 the reverb retires its own
  oldest bloom voice (`:2416-2424`), which is accepted behaviour, not an error.

  **The per-voice partial cap, and the selection rule.** `bloomNoteOn` silently truncates:
  `const std::size_t wanted = std::min(count, static_cast<std::size_t>(kMaxBloomResonators));`
  (`aether_reverb.h:2398-2399`) with `kMaxBloomResonators = 32` (`:1442`), documented at `:2377`
  (*"@param count Clamped to kMaxBloomResonators (32)"*). `HarmonicCloud::kMaxPartials = 64`
  (`harmonic_cloud.h:138`), so **up to half the partials of a full-richness voice are dropped** and the
  choice of *which* 32 is audible. `collectHeldPartials` therefore selects the **32 partials of greatest
  current amplitude**, ties broken by lower partial index, and emits them in **ascending frequency
  order**. Amplitude, not index, because the bloom stage is a resonant emphasis of the held chord and
  reinforcing 32 inaudible upper partials while dropping the fundamental's neighbours would invert the
  effect. `outCount = min(getActivePartialCount(), kMaxBloomResonators, capacity)`. The rule is
  deterministic given the voice's state, which is what lets SC-017 assert against it.
- **FR-072** Nothing non-finite reaches the Aether stage. The voice sum is scanned with the bit-pattern
  guard at the point of accumulation (the `continuous_body.h:1196-1200` idiom — never repaired
  afterwards), and a voice that trips it is `reset()` and counted in an introspection counter (FR-085).

### L. Registration, tooling and tests

- **FR-080** The three new headers are added to the header list in `dsp/CMakeLists.txt` (the
  `include/krate/dsp/effects/aether_reverb.h` entry at `:170` shows the shape) **and** to
  `dsp/lint_all_headers.cpp` (`:171` shows the shape). Missing either is a silent CI gap.
- **FR-081** Every new test TU is listed explicitly in `dsp/tests/CMakeLists.txt` under
  `dsp_systems_tests` (sources are enumerated, not globbed — `:337-351`). Any TU that injects NaN/Inf is
  additionally added to the `-fno-fast-math -fno-finite-math-only` source-property list (`:702-748`) and,
  if it carries its own `#error` guard, to the `tools/check-portability.js` exclusion (`:90`). The FR-070
  chain helper `tests/test_helpers/seraphis_chain.h` is registered in
  `tests/test_helpers/CMakeLists.txt` alongside the existing helpers; it is a test-only header and is
  **not** added to `dsp/lint_all_headers.cpp` (which covers shipped DSP headers only) and **not** subject
  to SC-008's grep.
- **FR-082** `node tools/check-portability.js` must be clean on the staged tree before commit. A green
  MSVC build is not evidence.
- **FR-083** Perf cases are tagged `[.perf]` and are hidden from the default run, following
  `TEST_CASE("AtmosphereEngine_GrainSampleCost", "[.perf]")` (`atmosphere_engine_perf_test.cpp:1251`) and
  `TEST_CASE("AtmosphereEngine_CpuBudget", "[.perf]")` (`:1322`) — a previous revision cited `:992`, which
  is a `setFreezeMix` call inside a helper. Their baselines are **measured** and the ns/block reference is
  derived from `(512/48000)·1e9 · <fraction>` with a `static_assert` tying the checked-in baseline to the
  reference (`aether_reverb_perf_test.cpp:350`). A measurement above the admissible baseline is resolved by
  reducing cost, never by raising the constant.
- **FR-084** No test pins a render with a bit-exact float digest. Determinism tests use
  `render_fingerprint.h` (`:64`, `:101`) at its shipped tolerances.
- **FR-085** Both engines expose a const, allocation-free introspection surface sufficient to test every
  FR **and every SC** without `#ifdef` scaffolding. The surface below is derived from what the criteria
  actually read; a previous revision omitted six accessors that six SCs need.
  - `SeraphisVoice::{getCurrentLevel, isFinished, stateFinite, getEnvelopeMode, getEnvelopeOutput,
    getEnvelopeStageTimeMs(stage), getEnvelopeReleaseMs, getSpatialAzimuth, getSpatialWidthPercent,
    getSeed, getRejectedConfigureTimeCallCount, isFreezeCaptured}` — `isFreezeCaptured` is the read-back
    half of FR-030a and is what lets a test prove the freeze *path* ran rather than assume it
    (`atmosphere_engine.h:936-941`) — plus the const sub-component accessors the FR-058
    table's voice-owned rows are read back through: `cloud() const`, `morph() const`, `body() const`,
    `atmos() const`, each returning a const reference so SC-010 can call the components' own getters
    (`HarmonicCloud::getRichness` … `getSpectralGravity`, `harmonic_cloud.h:490-494`;
    `ContinuousBody`'s twelve at `:1242-1320`).
  - `SeraphisEngine::{getPolyphony, getActiveVoiceCount, getRenderingVoiceCount, getVoiceLevel(i),
    getVoiceState(i), getVoice(i) (const ref), getLastStolenVoiceIndex, getNonFiniteRecoveryCount,
    getSeed, getLastBloomPartials(i) (const span), getLastBloomCount(i), getAtmosphereFreeze}`.
    `getAtmosphereFreeze()` reads back FR-030a's latched engine-wide state; the **mutating** freeze
    triggers are `SeraphisEngine::setAtmosphereFreeze(bool)` and `SeraphisVoice`'s own
    `captureFreeze()` / `releaseFreeze()`, which are part of the engines' functional surface (FR-030a),
    not of this const introspection surface — which is why `getVoice(i)` can stay a **const** reference.
    `getVoiceState(i)` forwards to `VoiceAllocator::getVoiceState` (`voice_allocator.h:424`) and is what
    SC-011 and SC-012 assert on. `getVoice(i)` is what SC-006 uses to render and correlate one voice at a
    time. `getLastBloomPartials(i)` / `getLastBloomCount(i)` record the exact array
    `collectHeldPartials` last produced for that voice, which is the only way SC-017 can compare what was
    *sent* against the cloud's partials — `AetherReverb` has no read-back for it.
  - **What deliberately has no accessor, and what the SCs do instead.** `ContinuousBody` exposes no
    `getDamping`/`getResonance`/`getMix`/`getCloudMix`, and `AetherReverb` exposes none of
    `getMix`/`getSize`/`getWidth`/`getShimmer*Send`/`getBloomSend`/`getSizeBreathDepth`/
    `getDimensionalityTideDepth` (its nine getters are listed in the Existing components table). N-9
    forbids adding them. SC-010 therefore asserts read-back only on targets whose component *has* a
    getter, and covers the rest via (a) the `SeraphisAetherTargets` POD, which is Phase 7 code and is
    directly comparable against the FR-058 table's `base` values, and (b) the render-fingerprint
    equivalence clause. SC-016 likewise measures the reverb's life state through
    `AetherReverb::getEffectiveDelayLengthSamples(0)` (`:2506`) — the observable Phase 6 itself used —
    because there is no breath or tide accessor (`specs/seraphis-phase6-aether-space/spec.md:2160`).

---

## Success Criteria

Each is measurable, with the metric, the threshold and the test that produces it. All perf figures are
ns/block at 512 samples / 48 kHz (block budget **10 666 666.7 ns**), the constant every Seraphis perf TU
already derives.

- **SC-001 — Full-poly CPU budget.** Measured on **the composed chain**
  (`processStereoBlock → AetherReverb::processStereoBlock → processOutputStage`, FR-070's helper), the
  cost is **≤ 25 % of one core** (2 666 666.7 ns/block) — the roadmap's line-311 ceiling, kept in full,
  though at 8 voices rather than 16 (RA-1 and the phase owner's 2026-07-30 ruling, RQ-1; the deviation is
  recorded in Traceability).

  **The measured configuration is RA-1's normative worst-case scenario, stated exhaustively so the gate
  is reproducible.** A previous revision said "the Aether stage at its *default* configuration" while
  citing RA-1's *worst-case* prediction, and never stated the freeze state — a 4-point spread on a 25 %
  ceiling:
  - polyphony **8** (FR-040 shipped default), **all 8 voices sounding**, none idle. Under FR-010's
    cloud-only envelope (Clarifications Q1) this is the **steady state**, not a contrived peak: a released
    voice keeps rendering until it is quiescent, so a saturated pool is what normal playing produces;
  - all five macros at their **FR-060 neutral** (Gravity 0.5, the rest 0), so the sub-components sit at
    the FR-019 shipped voice defaults;
  - cloud at 64 active partials with drift, morph + entropy, body at its **worst measured material
    configuration** (the one behind `specs/seraphis-phase4-continuous-body/compliance.md:19`), spatial
    stage active;
  - atmosphere **frozen**, engaged through `SeraphisEngine::setAtmosphereFreeze(true)` (FR-030a) and
    **asserted** by checking `isFreezeCaptured()` on every voice before the measurement starts — a silent
    no-op capture (`atmosphere_engine.h:911-916`) would otherwise measure the cheaper unfrozen path. This
    is the 1.440 %/voice row — freeze is a first-class playing technique (roadmap line 74) and is worth
    3.1 points at 8 voices against the unfrozen 1.048 %;
  - `AetherReverb` at RA-1 row **(c)**: `PrepareConfig{numChannels = 16, shimmerEnabled, bloomEnabled,
    spectralDiffusionEnabled all true, diffusionFftSize = 4096}`, `setSize(1)`, `setDensity(1)`, 32 bloom
    resonators — the same configuration `aether_reverb_perf_test.cpp:329-330` labels (c).

  Per RA-1 the prediction for exactly this configuration is **20.36 %** (8 × 2.322 % + 1.787 %) before the
  output stage, the voice sum, the spatial stage and the macro matrix. Measured over ≥ 8 trials on an idle
  machine; the checked-in baseline is `ceil(worst × 1.05)` with the
  `static_assert(baseline × kRegressionFactor ≤ kReference)` tie.

  **MEASURED 2026-07-31 (i9-13900HX, idle, on AC, MSVC Release, best-of-16 × 100 blocks).** Ten
  consecutive runs, % of one core: 19.70, 19.15, 19.74, 20.07, 19.53, 19.97, 18.34, 19.27, 18.95, 20.05 —
  worst **20.0682 % (2 140 610 ns/block)**. The checked-in baseline is therefore
  `ceil(2 140 610 × 1.05) = 2 247 641 ns/block (21.07 %)`, inside `kMaxAdmissibleNs` (21.74 %), giving a
  run-time gate of 24.23 % against the 25 % reference. That figure **replaces** the derived stand-in
  (2 280 320) and moves the baseline DOWN, as the recording procedure requires.

  **A LOADED MACHINE MEASURES THIS SCENARIO ~33 % HIGHER, and the figures are recorded rather than
  discarded.** An earlier compliance pass measured 25.41 %, 26.57 % and 26.66 % for the same binary and
  the same scenario with other work running. That spread is larger than `kRegressionFactor` (1.15), so
  the `[.perf]` lane's own discipline — "wait and re-run on a quiet machine; it is not a licence to
  loosen the constant" — is what governs, and no choice of baseline can make the gate robust against a
  busy machine. CI does not run this lane.
  *Test:* `SeraphisEngine_FullPolyCpuBudget` `[.perf]`, `seraphis_engine_perf_test.cpp`.
  **If this fails, the lever is the shipped voice count (a re-derivation of RQ-1, which costs no ABI
  change because `kMaxVoices` is 16) or Phase 7's own composition cost — never a Phase 2/4/5/6 gate
  (N-10), and never the 25 % ceiling, which RQ-1 explicitly kept.**
- **SC-002 — Per-voice composition overhead.** One `SeraphisVoice::processStereoBlock` costs **≤ 110 %**
  of the arithmetic sum of **exactly these eight** sub-components measured standalone in the same TU under
  the **same** configuration: `HarmonicCloud`, `SpectralMorphEngine` (which carries `EntropyProcessor`),
  `ContinuousBody`, `AtmosphereEngine`, `MultiStageEnvelope`, `GrowthEnvelope`, `OrbitModulator`,
  `MidSideProcessor`. (A previous revision said "the four sub-components", leaving the denominator — and
  therefore pass/fail — undefined against FR-002's eight.) The 10 % allowance covers the scratch copies,
  the per-sample envelope multiply, the spatial stage's two multiplies and the morph→cloud handoff, and
  nothing else.
  *Shared configuration, pinned:* 64 active partials with drift, FR-019 shipped voice defaults, body at
  SC-001's worst material configuration, atmosphere at default density **unfrozen**, envelope in
  `Standard` mode gated on, spatial depth 0.5, 512-sample blocks at 48 kHz.
  *Measurement discipline, copied from SC-001:* ≥ 8 trials, **best-of-N** per subject, the ratio computed
  from the aggregated figures — never from single runs.
  *Test:* `SeraphisVoice_CompositionOverhead` `[.perf]`.
- **SC-003 — Voice-steal clicklessness.** Roadmap line 312. Measured on **the composed chain**.

  > **The obvious form of this criterion is one Phase 2 already measured, found unsatisfiable and formally
  > withdrew.** `specs/seraphis-phase2-harmonic-cloud/spec.md:727-737` records the withdrawal verbatim:
  > *"`maxPerSampleDelta(modulated) ≤ 1.5 × maxPerSampleDelta(control)` … Both clauses fail on a
  > click-free build … the frozen control is not a quieter version of the modulated render, it is a
  > **different signal regime**"*, measured ratio **1.785** against the 1.5 bound. A previous revision of
  > SC-003 reinstated exactly that shape ("a control render of the same material with no steals"), and it
  > has three independent defects: **(a)** the control is not constructible — "the same material with no
  > steals" needs a larger pool, which changes the sounding-voice count and hence FR-052's `1/√n` sum
  > gain, i.e. a different regime again; **(b)** the supports differ — the test statistic is a max over a
  > 20 ms window while the reference is a max over the whole control render, so the larger support
  > inflates the reference and the bound goes near-vacuous; **(c)** it carried no positive control, so it
  > could not distinguish "no clicks" from "metric not wired up". The matched-regime form below is
  > Phase 2's amended shape (`:775-777`) transposed to a steal.

  *Pass condition (matched-regime, same render).* Render a saturated pool for 60 s and force **32 steals**
  at randomised block offsets. All windows are drawn from **that one render** — same voice count, same
  `1/√n` gain, same material:
  1. *Test statistic.* For each steal, `maxPerSampleDelta` over the ±10 ms window centred on it,
     **positioned in the OUTPUT domain**: the window is centred on
     `dispatch sample + AetherReverb::getLatencySamples()` (`aether_reverb.h:2612`), not on the dispatch
     sample. *(Amended 2026-07-31.)* The composed chain is not latency-free — the reverb's
     spectral-diffusion stage is ON by default (`aether_reverb.h:1584`) and aligns the dry path to the
     wet one, so **both** are delayed by `diffusionFftSize` = 1024 samples (`:661-672`), i.e. 21.3 ms at
     48 kHz, which is more than the whole ±10 ms window. **Without the shift this clause measures the
     wrong 20 ms of audio and is near-vacuous:** driving positive control (b)'s deliberately broken
     hard-cut build through the un-shifted window gave a worst steal-window delta of 7.25986e-05 against
     a reference maximum of 7.61769e-05 — the broken build PASSED — while the same render measured over
     ±100 ms gave 1.04e-03, 2.91e-04, 3.75e-04, 1.03e-03, 7.17e-04, 8.49e-04, 7.47e-04, with the maximum
     at offset **+1024 exactly** for every steal that had a victim.
  2. *Reference.* **One window per measured event** of the **same length** (20 ms), drawn from the same
     render at offsets that are at least 50 ms clear of any event (in the same output domain), uniformly
     spaced over the render. *(Amended 2026-07-31; a previous revision said "64 windows" and took the
     **95th percentile**.)*
  3. *Bound.* `max(test statistics) ≤ 1.5 × max(reference statistics)`, with **the same number of draws
     on both sides**. A maximum over N draws compared against a *percentile* over a *different* number of
     draws is an extreme-value estimator, not a click detector, and the evidence is measured: on the
     64+64-event render of SC-004 the reference windows' own maximum already sat **1.333×** above their
     p95 over half as many draws, the event windows' p95 sat only **1.107×** above the reference p95 —
     i.e. the two distributions are the same one — and the reported ratio grew with the event count on
     an unchanged build, 1.118 at 16 events and 1.676 at 128. This is defect (b) recorded above for the
     withdrawn control-render form, with the asymmetry pointing the other way. Max-against-max at equal
     N is symmetric under the null and still fails on a **single** clicking event, which a
     percentile-against-percentile form would not.
  4. No sample of the composed chain's output exceeds `TruePeakLimiter`'s ceiling (SC-015's bound).

  *Positive controls (mandatory, both — Phase 2's rule at `:775-777`).*
  a. *Detector wiring.* The same statistic over a non-steal window with a deliberately injected
     one-sample step of **2× that window's own `maxPerSampleDelta`** must exceed the bound. Denominated in
     `maxPerSampleDelta`, not in peak — Phase 2 measured that a step below the signal's own natural
     per-sample swing is by construction not detectable (`:748-753`).
  b. *Criterion wiring.* A build in which FR-047's `silence()` ramp is replaced by a hard cut (the
     `kSilenceRampMs = 0` injection) must **fail** clause 3. **It is ASSERTED in the shipping test**
     *(amended 2026-07-31; a previous revision said "recorded as a measured figure, not asserted in the
     shipping test")*: the injection reaches `silenceRampSamples_` through a friend probe declared in
     `seraphis_voice.h` and defined only by the test TU — the shape
     `detail::SeraphisEngineNonFiniteProbe` already uses for FR-072 — so the control runs on every
     invocation instead of depending on someone hand-patching a header. A control that is only ever
     "recorded by hand" cannot rot loudly, and in fact never had a figure recorded at all. **Measured:
     worst steal-window delta 1.04115e-03 against a bound of 1.14265e-04, i.e. 13.67× the reference
     maximum**, against 1.10× for the shipped 1 ms ramp on the identical render.
  *Test:* `SeraphisEngine_VoiceStealIsClickless`, sections "clauses 1-4 and positive control (a)" and
  "positive control (b): the kSilenceRampMs = 0 hard cut FAILS clause 3".
- **SC-004 — Note-on/note-off clicklessness.** The **same matched-regime construction and the same two
  positive controls** as SC-003, applied across 64 note-ons (including retriggers on a still-sounding
  voice, which exercise `HarmonicCloud`'s non-quiescent branch at `:604-606`) and 64 note-offs, with the
  reference windows drawn at least 50 ms clear of any note event.
  *Growth-mode clause (FR-021).* With `EnvelopeMode::Growth` and a 10 s growth duration, the composite
  voice gain sampled through `getEnvelopeOutput()` over the first 10 s must match the `GrowthEnvelope`
  shape alone: **compared sample-for-sample against a real `GrowthEnvelope` advanced on the identical
  clock** — same `prepare()`, same `setDuration()`, `trigger()` on the same sample, `processBlock(64)`
  on the same chunk grid — to within 1e-4, plus monotone non-decreasing (largest downward step ≤ 1e-7)
  and reaching ≥ 0.99 of its final value only within the last **10 %** of the duration.
  *(Amended 2026-07-31; a previous revision said "within the last 5 %" and carried no direct comparison.)*
  **The 5 % figure is unsatisfiable against the shipped component and was inert against the defect it
  targets.** `GrowthEnvelope` is a normalised logistic with `kSteepness = 10`
  (`growth_envelope.h:18-26`, `:102`); solving y(τ) = 0.99 gives τ = 0.9085, i.e. 9.08 s of a 10 s
  duration — outside the last 5 % by construction. And if FR-021 had zeroed only stage 0 and left
  FR-020's 4 s stage-1 ramp in series, the composite would be 0.7 × growth(t) after 6 s: still monotone
  and still crossing 0.99-of-final at τ = 0.9085, so **both halves of the old clause passed on the very
  build they were written to fail**. The sample-for-sample comparison is "match the `GrowthEnvelope`
  shape alone" read literally, and a leftover pre-sustain ramp fails it loudly.
  *Test:* `SeraphisEngine_NoteLifecycleIsClickless`.
- **SC-005 — Seeded determinism.** Two composed chains (two `SeraphisEngine` + `AetherReverb` pairs) with
  the same seed, config and note script produce 30 s renders whose `RenderFingerprint`s satisfy
  `compareFingerprints(...).withinTolerance()` — `worstMetricRelativeError ≤ 1e-5` and
  `worstSampleError ≤ 1e-4` (`render_fingerprint.h:49-52`). Bit-exact comparison is forbidden (FR-084).
  *Test:* `SeraphisEngine_SeededRenderIsReproducible`.
- **SC-006 — Voice-seed distinctness.** Roadmap line 292. Two clauses, at two different levels, because
  the engine's note API cannot produce clause (b)'s setup:
  - **(a), at the engine level.** Across the full `kMaxVoices` pool, the derived stream seeds are
    pairwise distinct and non-zero, asserted against `getSeed()` plus the documented salt scheme
    (`deriveStreamSeed(engineSeed, kVoiceSaltBase + v)`, FR-050).
  - **(b), at the `SeraphisVoice` level.** Instantiate `kMaxVoices` standalone `SeraphisVoice` objects,
    seed voice `v` with the exact FR-050 expression `deriveStreamSeed(engineSeed, kVoiceSaltBase + v)`,
    drive them all with the same note, and require every pair of 30 s renders to be a **different
    signal**: their `RenderFingerprint`s must NOT satisfy `compareFingerprints(...).withinTolerance()`,
    **and** `max|a − b|` must be at least **0.1 × the peak of the louder render**. The same-seed control
    is **two voices constructed with an identical seed**, whose correlation must exceed 0.999.

    > **The "pairwise Pearson |ρ| ≤ 0.5" form of this clause is WITHDRAWN, on the Phase 2 precedent and
    > with the same kind of evidence** *(amended 2026-07-31; the previous revision required it and the
    > implementation recorded, in a banner comment, that it was not satisfiable)*. **Pearson correlation
    > of two deterministic quasi-periodic signals that share their partial frequencies is
    > cos(phase offset), not a measure of independence**, and the FR-019 neutral gives every voice the
    > same frequencies:
    > * FR-019 ships `setDriftDepthCents = 0.0`, so nothing detunes one voice from another, and raising
    >   it does not help — measured worst |ρ| over the six pairs: **0.970 at 0 cents, 0.970 at 10 cents,
    >   0.968 at 25 cents** — because `HarmonicCloud` weights drift by partial index
    >   (`driftAmount_[i] = pow((i+1)/kMaxPartials, kDriftIndexExponent)·u`), which pins the
    >   **fundamental** hardest, and the fundamental carries the energy;
    > * SineStack's n⁻¹ amplitude law puts 63 % of the excitation's power in partial 1 and the body
    >   concentrates it further (fully wet at the FR-019 neutral). Measured worst |ρ| for the six pairs:
    >   **0.98 through the full chain, 0.60 for the cloud alone** — i.e. the criterion already fails
    >   before the body exists;
    > * the life modulators cannot rescue it: the same six pairs of orbit **azimuth** trajectories
    >   measure |ρ| up to **0.9989** (0.99996 on width) over 5 s and 0.995 over 30 s, because
    >   `OrbitModulator` at coupling 0 is a slow deterministic orbit whose only per-voice variable is its
    >   start phase.
    >
    > Half of all pairs of such signals exceed |ρ| = 0.5 by construction, at **any** render length and
    > for **any** seed spread, so no implementation change inside Phase 7 could satisfy it. The
    > replacement is the strongest statistic that IS well posed here — the renders must be different
    > signals, separated by a tenth of peak rather than by a rounding-level difference (~1e-7 relative,
    > six decades below). The bound is 0.1 and not 0.5 of peak for the same reason the correlation form
    > fails: a pair that happens to sit near in phase still separates by 0.46 of peak (voices 0 and 5 of
    > the 16-voice grid, the worst of the 120 pairs), so a half-peak bound would be measuring the phase
    > lottery this clause exists to stop measuring.

  > **Why (b) is not at the engine level.** A previous revision required "all voices given the *same*
  > note" through `SeraphisEngine`. `VoiceAllocator::noteOn` maps a repeated note to a **retrigger of the
  > one existing slot** — *"FR-012: Same-note retrigger … `if (existingVoice < kMaxVoices) {
  > retriggerNote(existingVoice, note, velocity); return events(); }`"* (`voice_allocator.h:237-244`),
  > behaviour this spec itself records in Edge Case 14 — so sending the same note 16 times occupies
  > exactly **one** voice. The same-seed control was equally unreachable: FR-050 spreads *distinct*
  > derived seeds and no engine API forces two voices to share one.

  *Test:* `SeraphisVoice_VoicesDriftIndependently` (clause b) and
  `SeraphisEngine_VoiceSeedsAreDistinct` (clause a).
- **SC-007 — Zero allocation after prepare.** Roadmap line 495. With a global new/delete counting probe
  active, a 60 s engine render including note-ons, note-offs, steals, polyphony changes from 1 to 16 and
  back, and every macro swept, performs **0** allocations. The test must include a **liveness probe**
  proving the counter is wired (one deliberate allocation observed) before asserting zero, as the Phase 2
  case does (`specs/seraphis-phase2-harmonic-cloud/compliance.md:120`).
  *Test:* `SeraphisEngine_NoAllocInProcess`.
- **SC-008 — RT-safety sweep.** `grep -nE 'new |delete |malloc|std::vector|std::string|std::function|mutex|lock|throw|try \{|printf|fopen|std::cout|shared_ptr|unique_ptr|resize\(|push_back|emplace|std::isnan|std::isinf|isfinite'` over the three new headers returns **zero code hits** (comment/identifier text only, enumerated in the compliance record).
  **This is satisfiable because FR-004 gives `SeraphisVoice` and `SeraphisEngine` a
  `static constexpr std::size_t kMaxBlockSamples = 2048` and FR-013 sizes every scratch buffer — and
  FR-051's stereo bus — as `std::array` against it, with `SeraphisVoiceConfig::maxBlockSamples` clamped
  to it.** The precedent is `HarmonicCloud`, which passes the identical grep because all its state is
  fixed-size `std::array` bounded by `kMaxPartials = 64`
  (`specs/seraphis-phase2-harmonic-cloud/compliance.md:120`); the counter-example is `AtmosphereEngine`,
  which *does* have block-sized scratch and therefore *does* use vectors
  (`blurFifo_[ch] = std::vector<float>{}`, `atmosphere_engine.h:470-471`) — Phase 7 does not repeat that
  shape in its own headers. FR-003's permission to allocate is scoped to the transitive
  sub-component `prepare` calls, which are behind those components' own headers and outside this grep.
  *Test:* recorded in `compliance.md`, plus the FR-008 bit-pattern requirement enforced by
  `tools/lint-nonfinite-symbols.js`.
- **SC-009 — Macro axes are monotone and audible.** Roadmap line 313: *"each macro audibly moves the sound
  along its documented axis, no discontinuities"*. For each of the five macros, sweep 0 → 1 in 21 steps,
  render 4 s per step on the composed chain from a fixed seed and note, holding **each non-swept macro at
  its own FR-060 neutral** (Gravity 0.5, the rest 0) — not at 0, which for Gravity would mean full "air"
  during every other macro's sweep.

  Each macro is gated on a **primary** metric plus a **secondary observable per remaining roadmap
  sub-axis**, because a single spectral metric under-covers roadmap lines 303–309 (each macro has three or
  four sub-axes) and would let a whole row be dropped from the FR-058 table with the sweep still passing:

  | Macro | Primary metric | Required | Secondary observables (each must move in the stated direction over the sweep, ρ ≥ 0.9 in magnitude) |
  |---|---|---|---|
  | Dream | mean absolute deviation of detected partial frequencies from the pure harmonic grid | ↓ | wet-tail energy after note-off ↑ (reverb send); `getSpatialAzimuth()` total variation ↑ (life-mod depth); `morph().entropy()` read-back ↓ |
  | Bloom | spectral centroid | ↑ | L/R correlation ↓ and M/S side energy ↑ (stereo width); wet-tail energy in the +12/+7 shimmer bands ↑ (shimmer send); `getSpectralTiltDb()` read-back ↑ |
  | Dissolve | **atmosphere-band contribution**: energy **over the settled last second** of the render minus that of the identical render with `AtmosphereEngine::setLevel(0)`, as a fraction of total | ↑ | post-note-off tail energy at +2 s ↑ (envelope slew); attack slope of the first 200 ms ↓ (transient definition); blur-induced **L/R decorrelation of the atmosphere's own contribution** ↑ |
  | Gravity | high/low band-energy ratio (above vs below 1 kHz) | ↓ | `getRichness()` read-back ↓; **measured** body decay time in the 1–8 kHz band, on an isolated-damping arm ↓; `SeraphisAetherTargets::size` ↑ |
  | Entropy | spectral flatness | ↑ | `getDriftDepthCents()` read-back ↑; partial-frequency variance over the render ↑ |

  **Dissolve and Entropy no longer share a metric.** A previous revision keyed both on spectral flatness,
  so neither sweep could distinguish its own axis from the other's, and a mapping that routed one macro to
  the other's targets would pass both. Dissolve is now keyed on an atmosphere-specific differential
  measure and Entropy keeps flatness.

  **Dissolve's differential is taken over the SETTLED LAST SECOND, not the whole render**
  *(amended 2026-07-31)*, i.e. the same analysis segment the pinned partial detector below uses. The
  window matters here more than anywhere else in the case because Dissolve's own envelope-slew rows move
  the render's energy in **time**: `CloudAttackTimeSec` runs 0.05 → 2.0 s and `EnvStage0Ms`
  2000 → 6000 ms, so at the top of the sweep a whole-render integral is dominated by an attack the
  atmosphere is still a 4 s capture ring behind. MEASURED whole-render: 0.00943 → 0.03263 with a
  **plateau from step 12 to step 17** — the axis appeared to stop moving exactly where the slew rows took
  over.

  **Dissolve's blur observable is L/R DECORRELATION, not spectral spread** *(amended 2026-07-31; a
  previous revision said "blur-induced spectral spread ↑")*, and it is measured on the atmosphere's own
  contribution (`full − muted` per sample; both arms are deterministic and differ only in one output-gain
  setter, `atmosphere_engine.h:946-948`, so the difference IS the atmosphere path and it costs no extra
  render). The blur stage's own source says why: *"MAGNITUDE IS NEVER WRITTEN — only the phase moves, so
  the stage is a decoherer and not a filter"* (`atmosphere_engine.h:2050-2052`), and *"The draw is PER
  BIN PER CHANNEL from the one `blurRng_` stream, which is what makes blur produce progressive stereo
  decorrelation as well as fog"* (`:2062-2066`). A phase-only stage smears in **time**; it does not
  broaden a magnitude spectrum, and the 75 %-overlap OLA that reassembles it cancels the upper bins
  hardest, so spread moves the **wrong way by construction** — MEASURED on the atmosphere-only
  differential, spectral spread fell 686.1 → 525.4 Hz, Spearman ρ = **−1.0** against a "+0.9" gate. The
  observable is `1 − |ρ_LR|`, because the atmosphere already ships pan spread 0.7 and decorrelation 0.5,
  so the base correlation is **negative** (−0.328 measured) and it is the magnitude blur collapses.

  **Gravity's body-decay observable is MEASURED off the render, not read back**
  *(amended 2026-07-31)*. `ContinuousBody::getEngineT60Sec()` cannot see damping and its own source says
  so: *"FR-036: one law, three engines. Damping shapes `b3` (modal) / `S` (waveguide) / per-comb
  damping, none of which move the T60 reported here"* (`continuous_body.h:1574-1578`) —
  `updateEngineTargets()` derives `slot.engineT60` from `resonanceScale(resonance_)` alone
  (`:1579-1595`), and no Gravity row writes resonance. MEASURED over the 21-step sweep:
  `getEngineT60Sec()` read **4.56972 at every step**, Spearman ρ = 0, i.e. the observable is constant by
  construction. The replacement is the quantity the clause names — the decay time of the body's own
  ring, estimated from two RMS windows in the post-note-off tail — with three conditions, each forced:
  * **dry, no reverb**: Gravity's `AetherSize` row sweeps the room 0.05 → 0.95, so a composed-chain tail
    measures the room, not the body;
  * **the band 1–8 kHz**: damping shapes the modal `b3` term, which is *frequency-dependent* — it damps
    the upper modes and leaves the fundamental's T60 alone. MEASURED broadband: 4.53295 → 4.52963 s over
    the whole sweep, a 0.07 % move, ρ = −0.573;
  * **an isolated arm** that holds `CloudRichness` and `CloudSpectralTiltDb` at their FR-019 bases and
    mutes both the atmosphere and the body's **decay cloud**. The decay cloud is a parallel texture with
    its own fixed `setCloudDecaySec(4.0f)` that no macro row writes, i.e. a constant floor sitting
    exactly on the quantity being measured: MEASURED with it left in, the tail decay read
    3.89389 → 3.89081 s across the whole sweep (band-limited, as this observable is) — it reported `cloudDecaySec` and nothing else. Holding
    the two cloud rows leaves `BodyDamping` as the only thing moving, which is what makes the
    observable a proof that the row exists rather than a restatement of the primary.

  **Entropy's primary is measured on the CLOUD-ONLY arm** *(amended 2026-07-31)*, for the reason this
  criterion already grants Dream's primary: the stages that own the axis are in the cloud, and everything
  downstream contributes broadband energy Entropy does not write. MEASURED over the 21-step sweep, same
  table, three arms: composed chain 0.00620 → 0.00739, ρ = **0.661**; dry voice sum
  0.000374 → 0.000607, ρ = **0.869**; cloud only 0.000198 → 0.000270, ρ ≥ **0.9**. The composed chain's
  flatness is dominated by the Aether tail and the granular atmosphere — both stochastic, neither written
  by Entropy — and their realisation changes with every drift setting.

  **Dream's metric is now one its own mapping cannot fight.** With `setDriftDepthCents` removed from Dream
  (FR-061) the four remaining purity rows all reduce partial-frequency deviation, and FR-019 gives each of
  them a non-zero base to descend from. **During the Dream sweep the Aether `mix` target is held at its
  neutral** and the primary metric is measured on `processStereoBlock`'s **dry voice sum**, not on the
  composed chain, so reverb smearing cannot corrupt the partial detector; Dream's reverb-send sub-axis is
  covered by the secondary wet-tail observable on the composed chain instead.

  **Partial detector, pinned** (all deviation/frequency metrics above): 65 536-point FFT, Blackman-Harris
  window, analysis segment = the last 1 s of each 4 s step (so the 20 ms/100 ms smoothers have settled),
  peak picking with a −60 dB-from-max threshold and a minimum 20 dB peak-to-local-median SNR, parabolic
  interpolation on the log magnitude. Adjacent maxima closer than f₀/2 are one lobe group and the louder
  is kept; peaks off the [1, 96] grid entirely are excluded; the survivors are then matched to grid slots
  **ORDINALLY** — the k-th peak by ascending frequency to slot k — and a step whose detected peak count
  differs **in either direction** from the count the cloud is sounding fails the case outright rather
  than silently reducing or re-indexing the support.

  **The "matched to grid slots by nearest ratio" rule is withdrawn: it is not measurable**
  *(amended 2026-07-31; and the "fewer than 24 partials" figure with it — FR-041(a) fixes the active
  partial count at N(r) = clamp(round(64ʳ), 1, 64) and FR-019 ships richness 0.60, so the cloud sounds
  round(64^0.6) = **12** partials and no detector can find 24 in a 12-partial signal).* `round(f / f₀)`
  makes the per-partial deviation a residual **modulo f₀**, so it is bounded by f₀/2 = 55 Hz at this
  case's A2 whatever the real deviation is. FR-083's law is
  `f_n = f₀ · n^(1 + g·kGravityExponentRange) · √(1 + B·n²)` (`harmonic_cloud.h:1268-1270`,
  `:1331-1334`), and at the FR-019 base (g = 0.20, B = 0.030) partial 12 sits near **slot 29** — a real
  deviation of ~2.6 kHz the modular residual cannot express at all. MEASURED over the 21-step Dream sweep
  with nearest-ratio matching: 20.5, 23.6, 22.8, 28.4, 27.8, 27.7, 25.2, 19.8, 23.2, 26.9, 25.8, 23.4,
  21.1, 29.1, 20.4, 29.5, 25.2, 24.4, 22.7, 18.3, 0.00086 — uniform-looking noise in [0, 55] at every
  step at which any inharmonicity remains, then exactly 0 at Dream = 1. Spearman ρ = **−0.232** against
  the −0.9 gate, with an end-to-end effect size that passed trivially: the metric was reporting an
  aliasing artefact. Ordinal matching is strictly **stronger** — the deviation is unbounded above and
  `f_n − n·f₀ ≥ 0` is monotone in both g and B by construction — and the exact-count gate is what keeps
  it honest, since a spurious or missing peak re-indexes every slot above it. The lobe-group merge is
  standard partial tracking and is bounded by the same law: MEASURED on this case's dry arm, partial 2
  (236.08 Hz) is accompanied by a satellite 4.4 Hz above it, while the FR-083 grid never places two
  partials closer than f₂ − f₁ ≈ 124 Hz.

  **Minimum effect size (per macro) — the "audibly" half of roadmap line 313.** Spearman ρ is
  **scale-invariant**, so a row tuned to an amount of 0.001 trends perfectly and moves nothing anyone can
  hear; FR-058 deliberately leaves amounts to implementation (Clarifications Q3), so this clause is what
  keeps that freedom honest. For each macro the **end-to-end** change in the primary metric, measured
  between the first and last sweep step on the pinned detector below, must be at least:

  | Macro | Primary metric | Required end-to-end change (step 0 → step 20) |
  |---|---|---|
  | Dream | mean absolute deviation from the harmonic grid | falls to **≤ 50 %** of its value at Dream = 0 (at least halved) |
  | Bloom | spectral centroid | rises by **≥ 20 %** relative to its value at Bloom = 0 |
  | Dissolve | atmosphere-band contribution fraction | rises by **≥ 0.15 absolute** |
  | Gravity | high/low band-energy ratio (above vs below 1 kHz) | changes by **≥ 6 dB** between Gravity = 0 (air) and Gravity = 1 (stone) |
  | Entropy | spectral flatness (cloud-only arm) | rises by **≥ 25 % relative** to its value at Entropy = 0 |

  **Entropy's row is RELATIVE, on the same form as Bloom's** *(amended 2026-07-31; a previous revision
  said "rises by ≥ 0.10 absolute")*. **Spectral flatness 0.10 is a near-white spectrum, and FR-065 wires
  Entropy to sub-semitone frequency jitter only** — `EntropyProcessor`'s largest frequency perturbation
  is `kMaxScatterCents = 7` (`entropy_processor.h:76`) and the cloud's is `kMaxDriftCents = 50`
  (`harmonic_cloud.h:214`) — so no Seraphis can reach it. MEASURED end-to-end on all three arms:
  0.00620 → 0.00739 composed, 0.000374 → 0.000607 dry, 0.000198 → 0.000270 cloud-only, i.e. between two
  and three **orders of magnitude** below the absolute figure at every point of every sweep.

  A macro that trends correctly but misses its row **fails the case**; the remedy is to retune that
  macro's `amount`s in the FR-058 table, never to lower the figure here. Both remedies were taken in
  this phase and are recorded in the table itself: Dissolve's `AtmosBlur` amount was cut from 1.0 to
  0.40 (blur is a phase decoherer, so the 75 %-overlap OLA loses energy as it rises and subtracts exactly
  what `AtmosLevel` adds — measured, the atmosphere fraction peaked at step 9 and fell to step 17,
  ρ = 0.199), and Entropy's `MorphEntropy` amount from 0.80 to 0.30 (`EntropyProcessor`'s stage 3
  saturates flatness and stage 4 removes partials outright — measured, ρ = 0.521 at amount 0.80 and
  ρ ≥ 0.9 at 5, 11 and 21 sweep points at amount 0.30). The secondary observables are
  still gated on direction only (|ρ| ≥ 0.9) — they exist to prove no row was dropped, not to size it.

  **Monotonicity wording.** The gate is a **monotone trend, Spearman ρ ≤ −0.9 (decreasing) or ρ ≥ 0.9
  (increasing)** — not strict monotonicity. A previous revision said "strictly decreasing (Spearman
  ρ ≤ −0.9)", which is self-contradictory: |ρ| = 0.9 over 21 steps admits several reversals, so an
  implementer could satisfy either reading. Strictness is not required; smoothness is, and it is the
  separate clause below.

  **No-discontinuity clause.** Between consecutive steps the change in the primary metric never exceeds
  **3×** the mean step change. The factor is derived, not chosen: FR-057 restricts the table to `Linear`,
  `Exponential` and `SCurve`, and the steepest of those over a 21-step sweep is `Exponential` (x²,
  `modulation_curves.h:46`) at ~1.95× the mean step; 3× leaves ~1.5× headroom for the metric's own
  measurement noise. `ModCurve::Stepped` is excluded by FR-057 precisely because it would sit at ~6.7×.
  *Test:* `SeraphisEngine_MacroSweepsMoveTheirAxis` (uses the `testing-dsp-analysis` FFT helpers).
  Tagged `[.slow]` — see SC-020.
- **SC-010 — Shipped defaults and macro inertness at neutral.** FR-019 and FR-060. Four clauses:
  1. **The whole FR-019 surface, straight after `prepare` and before any macro is applied.** Every row of
     FR-019's complete table whose component exposes a getter reads exactly the table's shipped voice
     default — cloud (`getRichness` … `getSpectralGravity`, `harmonic_cloud.h:490-494`,
     `getDriftDepthCents`, `getStereoSpread`, `getAttackTimeSec` `:595`, `getDecayTimeSec` `:596`), morph
     (`entropy()` `:453`, `getBloom()` `:440`, `getTravelRate()` `:441`, `getTravelMode()` `:442`,
     `getStateCount()` `:443`), atmosphere (`getLevel`, `getBlur`, `getDensity`, `getGrainSeconds`,
     `getDriftDepth`, `getPanSpread`, `getDecorrelation`, `getFreezeMix`, and `isFreezeCaptured() == false`),
     spatial (`OrbitModulator::getDepth` / `getRate` / `getCoupling` / `getGrowth`,
     `orbit_modulator.h:190-193`, reached through `SeraphisVoice`'s forwarders), and the envelope
     (`getEnvelopeStageTimeMs` / `getEnvelopeReleaseMs` / `getEnvelopeMode`). **This clause is what makes
     FR-019 the normative definition of a neutral Seraphis** (Clarifications Q2) rather than a list of
     intentions: the two zero-travel fixes — spatial depth 0.35 and body damping 0.25 — are asserted here
     (damping through clause 4, see below).
  2. **Read-back at neutral.** With every macro at its documented neutral (Gravity 0.5, the rest 0),
     applying the matrix leaves every one of those same getters at exactly the same value as in clause 1 —
     i.e. `apply()` at neutral is the identity on the whole readable surface.
  3. **POD comparison, for the Aether rows.** `computeAetherTargets()` returns exactly the FR-058 table's
     `base` values for all eight fields. This clause replaces a read-back the reverb cannot provide:
     `AetherReverb` has **no** getter for `mix`, `size`, `width`, either shimmer send, `bloomSend`,
     `sizeBreathDepth` or `dimensionalityTideDepth` (its nine getters are enumerated in the Existing
     components table), and N-9 forbids adding one. The same applies to `ContinuousBody::setDamping`,
     `setResonance`, `setMix` and `setCloudMix`, which have no getters either — those rows are covered by
     clause 4 alone, and that limitation is stated rather than papered over.
  4. **Render equivalence.** A 4 s composed-chain render with the matrix applied every block
     fingerprint-matches a render with the matrix never applied at all. This clause is also the only
     assertion available for the getter-less FR-019 rows (`ContinuousBody`'s damping, resonance, mix and
     cloud mix): a build whose `prepare` shipped a different value for any of them renders differently and
     fails here.
  *Test:* `SeraphisMacroMatrix_NeutralIsInert`, with clause 1 in `SeraphisVoice_ShipsDocumentedDefaults`.
- **SC-011 — Steal policy correctness.** With a saturated pool at known, distinct levels: the stolen voice
  is the lowest-level `Releasing` voice; with no `Releasing` voice, the lowest-level `Active` voice; and a
  `Releasing` voice at or above `kAmnestyLevelThreshold = 0.0316` (−30 dBFS) is skipped while a candidate
  below it exists (FR-045, FR-046). Voice states are read through `SeraphisEngine::getVoiceState(i)`
  (FR-085) and levels through `getVoiceLevel(i)`, which is FR-033's detector — chunk peak, instant attack,
  `kLevelReleaseMs = 100` release. The "known, distinct levels" are established by rendering each voice
  for **at least 8 control chunks** (≥ 10.7 ms) at its intended level before the steal is forced, which is
  what the instant attack makes sufficient; the levels are then read back and asserted distinct **before**
  the steal, so the case cannot pass on a coincidence of chunk phase. **Additionally**, the allocator's returned `NoteOn`
  event must name the engine's chosen slot: `getLastStolenVoiceIndex()` equals the index the FR-045 rule
  selected *and* equals the slot the new note actually landed on — the assertion RA-4's mechanism depends
  on.
  *Test:* `SeraphisEngine_QuietestStealWithAmnesty`.
- **SC-012 — No premature voice reclaim.** Across a 60 s script with 10 s+ tails:
  1. a voice is never returned to `VoiceState::Idle` (`getVoiceState(i)`) while its own
     `getVoiceLevel(i)` is above `kTailSilenceThreshold = 1e-5` (−100 dBFS), **and** it is never dropped
     from `getRenderingVoiceCount()` while `SeraphisVoice::isFinished()` is false;
  2. every voice **is** eventually reclaimed (no leak): after the final note-off plus 45 s,
     `getActiveVoiceCount() == 0` and `getRenderingVoiceCount() == 0`. **The 45 s is derived, not
     chosen:** the longest tail the configuration can produce is the body's cloud decay
     (`kMaxCloudDecaySec = 30.0f`, `continuous_body.h:147`), and FR-033's detector adds
     `kLevelReleaseMs · ln(1/kTailSilenceThreshold) ≈ 1.15 s` plus FR-032's four chunks before retirement
     — ~31.2 s in total, leaving ~14 s of margin. A longer `kLevelReleaseMs` would eat that margin, which
     is why the constant is named and fixed rather than tuned at implementation time.
  **Polyphony shrink is exempt from clause 1's allocator-state half**, by FR-040: `setVoiceCount`
  force-idles the excess slots itself (`voice_allocator.h:347-352`) and the engine cannot prevent it. The
  `getRenderingVoiceCount()` half of clause 1 still applies during a shrink and is what proves the tail
  keeps being rendered.
  *Test:* `SeraphisEngine_VoiceReclaimIsCorrect`.
- **SC-013 — Sample-rate independence.** At 44.1 / 48 / 96 kHz, the same note script on the composed
  chain, with **`setDriftDepthCents` held at its FR-019 default of 0.0** so drift is not a source of
  spread during the measurement:
  1. **RMS and mean-abs agree within ±1 dB**, computed **per rate on that rate's own render** — no
     resampling, because both are rate-invariant aggregates and a low-quality rate conversion moves them
     by more than the figure they are meant to test.

     **The bound is INHERITED, not chosen** *(amended 2026-07-31; a previous revision said "within
     5 %")*. `ContinuousBody`'s own sample-rate criterion is *"steady-state output RMS within ±1 dB"*
     across exactly these three rates
     (`specs/seraphis-phase4-continuous-body/spec.md:1743-1748`, asserted at
     `continuous_body_test.cpp:6529-6531`), and **a composed criterion cannot be tighter than a
     component it contains** — the same rule this spec's SC-014 already applies when it takes "the looser
     of the two" from Phases 5 and 6. 5 % is 0.42 dB, i.e. less than half of what the body guarantees.
     MEASURED at 10 s, 48 kHz vs 96 kHz, with the voice's stages switched in one at a time (dry voice
     sum, sustained A3): cloud only (body bypassed, atmosphere muted) **rms 0.36 %, peak 0.13 %**;
     + body **rms 6.28 %, peak 2.38 %**; + atmosphere **rms 6.28 %, peak 8.18 %**. The whole of the
     spread is `ContinuousBody` exercising its own allowance — 6.28 % is 0.53 dB, comfortably inside
     ±1 dB and outside 5 %.
  2. **Peak is bounded separately at ±2 dB**, again per rate without resampling. Peak is the most
     resampler- and phase-sensitive of the three, so it does not share clause 1's bound; the 2× ratio
     the original 5 %/10 % pair expressed is kept.
  3. **Measured fundamental agrees within 1 cent**, estimated with the `testing-dsp-analysis` FFT-based
     estimator — 65 536-point FFT, Blackman-Harris window, parabolic log-magnitude interpolation — over
     the analysis window **[2.0 s, 3.0 s)** after note-on, i.e. after the 20 ms pitch smoother
     (`continuous_body.h:168`) and the FR-020 attack have settled.
  Absolute timing of grain-dependent detail is explicitly exempt (RA-8 records that
  `RollingCaptureBuffer`'s power-of-two rounding makes ring length in seconds rate-dependent —
  `specs/seraphis-phase5-atmosphere/spec.md:205-213`).
  *Test:* `SeraphisEngine_SampleRateIndependence`.
- **SC-014 — Block-size invariance.** The same 10 s composed-chain render produced in blocks of 1, 7, 64,
  65, 512 and 4096 samples is compared against the 512-sample reference by **maximum absolute per-sample
  difference over all samples**, bounded at **≤ 1e-5** — the form and the figure Phase 5's SC-011 uses
  (`specs/seraphis-phase5-atmosphere/spec.md:1433-1434`); Phase 6's equivalent is ≤ 1e-6
  (`specs/seraphis-phase6-aether-space/spec.md:1917`) and 1e-5 is taken as the looser of the two because
  the atmosphere is in the chain. The `render_fingerprint.h` comparison is kept as a **secondary**
  aggregate check only.
  **Why the fingerprint alone is not enough here.** It samples 32 evenly spaced checkpoints
  (`kRenderCheckpoints = 32`, `render_fingerprint.h:46`, sampled at `:83-86`) out of 480 000, so it can
  miss a localised divergence entirely; and in the other direction its `kMetricTolerance = 1e-5` relative
  bound on `totalVariation` (`:52`, `:76`) is *tighter* than what the sub-components guarantee — Phase 5
  permits 1e-5 per sample, which accumulated over 480 000 samples can move total variation far outside
  1e-5 relative. Those tolerances were measured for cross-toolchain spread of the *same* computation
  (`render_fingerprint.h:20-30`), not for a re-partitioned one.
  *Required coverage inside the case* (Phase 5's SC-011 rule): the parameter set must guarantee that at
  least one partition boundary falls **inside** a 64-sample control chunk and that at least one grain is
  born in that partial chunk, asserted via `AtmosphereEngine`'s grain-birth counter at a non-multiple of
  64 — otherwise FR-007's carry-over path is assumed rather than exercised.
  *Test:* `SeraphisEngine_BlockSizeInvariance`.
- **SC-015 — Output ceiling.** Over a 60 s adversarial render on **the composed chain** (16 voices, all
  macros at 1, maximum resonance, frozen atmosphere, infinite Aether decay), **no sample of
  `processOutputStage`'s output exceeds the `TruePeakLimiter` ceiling** (`kDefaultCeilingDb = -1.0f`,
  `true_peak_limiter.h:46`) by more than 0.1 dB, and the render contains no non-finite sample. The bound
  is asserted on the **post-`processOutputStage`** signal only; `processStereoBlock`'s voice sum and the
  reverb return are intermediate and are not limited (FR-054).
  *Test:* `SeraphisEngine_OutputNeverExceedsCeiling`.
- **SC-016 — Idle liveness.** Roadmap Key Design Decision 1 (lines 71–72). With **no notes ever played**,
  a 60 s composed-chain render — during which the test drives `processStereoBlock` every block, so every
  idle voice receives `advanceLifeOnly` per FR-051 — leaves the life state moving:
  1. **every** voice's `getSpatialAzimuth()` has a non-zero total variation over the render, **and**
  2. **every** voice's `getSpatialWidthPercent()` has a non-zero total variation. Clause 2 is what
     catches a width axis accidentally multiplied by `getGrowth()`'s neutral 0 (FR-025);
  3. the Aether stage's life state has advanced, measured as non-zero total variation of
     `AetherReverb::getEffectiveDelayLengthSamples(0)` (`aether_reverb.h:2506`) — the observable Phase 6
     itself used for this, because *"`AetherReverb` exposes no breath accessor"*
     (`specs/seraphis-phase6-aether-space/spec.md:2160`) and FR-086 did not ship one.
  The audio output is silent (no voices sounding) — liveness is asserted on state, not on level, and the
  test also asserts the audio path stayed at exactly 0.
  *Test:* `SeraphisEngine_LifeModulatorsRunAtIdle`.
- **SC-017 — Bloom follows the chord.** After `noteOn` on three notes, for each sounding voice the partial
  set recorded by `getLastBloomPartials(i)` / `getLastBloomCount(i)` (FR-085) — which is exactly the array
  handed to `bloomNoteOn` — matches the corresponding `HarmonicCloud` partial frequencies to within
  **0.1 cent**, for the `min(getActivePartialCount(), kMaxBloomResonators)` partials selected by FR-071's
  rule (the 32 of greatest current amplitude, ties by lower index, emitted ascending by frequency), and
  `getLastBloomCount(i)` equals that same `min`. After `noteOff` and reclaim, `bloomNoteOff` has been
  issued for that voice id.
  **The cap is why the criterion is phrased that way.** `bloomNoteOn` truncates at
  `kMaxBloomResonators = 32` (`aether_reverb.h:1442`, `:2377`, `:2398-2399`) while
  `HarmonicCloud::kMaxPartials = 64` (`harmonic_cloud.h:138`), so a previous revision's "for every active
  partial" was false by construction at any configuration above 32 active partials.
  *Test:* `SeraphisEngine_BloomTracksHeldChord`.
- **SC-018 — Non-finite recovery.** Injecting a non-finite value into one voice's state leaves **the
  composed chain's** output finite for every sample of the following 5 s, increments
  `getNonFiniteRecoveryCount()` exactly once, and the other voices' renders are unchanged
  (fingerprint-identical to a control).
  *Test:* `SeraphisEngine_NonFiniteIsContained`, in a TU carrying
  `-fno-fast-math -fno-finite-math-only` (FR-081) and building its non-finite inputs from bit patterns
  through a volatile sink.
- **SC-019 — Zero warnings, all compilers.** The three headers and every new TU compile with **zero**
  warnings under MSVC and under `g++ -Wall -Wextra -std=c++20` (WSL), and `node tools/check-portability.js`
  is clean on the staged tree.
- **SC-020 — Always-on wall clock.** The **always-on** (neither `[.perf]` nor `[.slow]`) portion of the
  Phase 7 test set adds **≤ 60 s** to `dsp_systems_tests`, the budget Phase 6 used and met
  (`specs/seraphis-phase6-aether-space/compliance.md:383`). The split is **decided here, not discovered at
  implementation time**, in the shape Phase 6's SC-017 uses (always-on core + `[.slow]` full grid).

  **Why the split is mandatory.** RA-1 puts the 8-voice composed chain at ~20.4 % of one core, i.e. ~4.9×
  faster than real time; 16 voices is ~2.6×. The criteria as written mandate, at minimum: SC-005 two 30 s
  renders; SC-006(b) 16 voice renders of 30 s plus a control; SC-007 60 s; SC-009 5 × 21 × 4 s = 420 s;
  SC-012 60 s plus 45 s of tail; SC-013 three rates; SC-014 six partitions of 10 s (of which the
  1-sample partition costs 480 000 process calls); SC-015 60 s at 16 voices; SC-016 60 s. That is well
  over 800 s of audio and **cannot** fit 60 s of wall clock at any of those ratios, before the
  ASan/valgrind lanes the repo also runs.

  | Criterion | Always-on portion | `[.slow]` portion |
  |---|---|---|
  | SC-003 / SC-004 | 8 steals / 16 note events over a **10 s** render, same matched-regime construction, both positive controls | full 32-steal / 64+64-event 60 s renders |
  | SC-005 | two **5 s** renders | two 30 s renders |
  | SC-006 | clause (a) in full (no audio); clause (b) for **4** voices over **5 s** | clause (b) for all 16 voices over 30 s |
  | SC-007 | 10 s script covering note-on/off, one steal, one polyphony change, liveness probe | full 60 s script with the 1↔16 sweep and all macros |
  | SC-009 | **1 macro × 5 steps × 1 s** as a wiring probe (each metric computed, each direction correct) | full 5 × 21 × 4 s grid |
  | SC-012 | 15 s script with a 10 s tail | full 60 s + 45 s |
  | SC-013 | 48 kHz vs 44.1 kHz over 3 s | all three rates over 10 s |
  | SC-014 | partitions {1, 64, 65} over **1 s** (the 1-sample partition is 48 000 calls, not 480 000) | all six partitions over 10 s |
  | SC-015 | 10 s at 8 voices | 60 s at 16 voices |
  | SC-016 | **24 s** — one full breath cycle at Phase 6's pinned 0.05 Hz, the same reasoning that let Phase 6's SC-017 clause 1a run always-on | 60 s |
  | SC-017, SC-018, SC-010, SC-019, SC-008 | in full — all short or non-render | — |

  Always-on audio totals ≈ **135 s** of render, ≈ 28 s of wall clock at the 4.9× ratio, leaving ~2× margin
  inside the 60 s budget. **The measured figure is recorded in `compliance.md`**; if it exceeds 60 s the
  response is a further demotion from the table above, recorded, never a silent one. Every `[.slow]` case
  runs in the nightly lane.

---

## Edge Cases

**Real-time boundaries**

1. **`processStereoBlock` before `prepare`** — zero-fill and return; no state advances, nothing is read
   from uninitialised coefficients. Matches `continuous_body.h:1174-1180`.
2. **Null output pointer** — write nothing, return, no state change (`continuous_body.h:1166-1169`).
3. **`numSamples == 0`** — no-op; **the control grid does not advance** (`atmosphere_engine.h:672-675`).
4. **Block larger than `maxBlockSamples`** — the render is chunked on the 64-sample grid regardless
   (FR-007), but the atmosphere's blur FIFO is sized from `maxBlockSamples`
   (`atmosphere_engine.h:373`). Oversized blocks are therefore split by `SeraphisVoice` into at most
   `maxBlockSamples` slices before reaching the sub-components. Never silently truncated.
5. **Second `prepare` while sounding** — legal; fully reconfigures and ends in `reset()`, i.e. silence.
   No allocation happens on any other path.
6. **`silence()` then continue rendering** — the atmosphere latches
   (`atmosphere_engine.h:641-643`) while the Aether stage resumes (`aether_reverb.h:2140-2143`). FR-034
   and FR-047 pin the `silence()` → `reset()` order that makes this deterministic; calling `silence()`
   twice is idempotent.

**Parameter extremes**

7. **Polyphony 1** — one voice; the `1/√n` sum gain is exactly 1; steal always targets voice 0.
8. **Polyphony reduced below the sounding count** — `VoiceAllocator::setVoiceCount` emits
   **`VoiceEvent::Type::NoteOff`** events (`voice_allocator.h:340-346`), *not* `Steal`, and force-idles
   each excess slot in the same loop (`:347-352`). Per FR-040 the engine treats each as a musical release
   (`voices_[i].noteOff()`) and **keeps rendering** that slot until its own `isFinished()`, so the tail
   decays instead of being cut. It does **not** run FR-047 on them: `silence()` → `reset()` → `noteOn()`
   with no note to start is a hard cut, the opposite of the intent. SC-012 exempts the allocator's
   `VoiceState` during a shrink and asserts the rendering half instead.
   **The slot is still guarded on reuse:** because the allocator now believes it is `Idle`, a later
   `noteOn` can land on it while the tail is still rendering, and FR-042 requires the FR-047
   `silence()` → `reset()` → `noteOn()` teardown in exactly that case (`isFinished() == false` on an
   allocator-`Idle` slot), so the new note never layers over an orphaned tail.
9. **All five macros at 1 simultaneously** — every target is summed then clamped (FR-057). No target may
   exit its setter's documented range, and SC-015 covers the level consequence. Contradictory pairs
   resolve to the sum — this is specified behaviour, not a conflict to detect. The three shared targets
   are named in FR-057: `SpectralMorphEngine::setEntropy` (Dream ↓ vs Entropy ↑),
   `HarmonicCloud::setRichness` (Bloom ↑ vs Gravity ↓) and `setSpectralTiltDb` (Bloom ↑ vs Gravity ↓).
   Note that "all macros at 1" puts **Gravity at full stone**, i.e. +0.5 from its neutral, not at an
   extreme of a 0-based knob (FR-060).
10. **`setSpatialDepth(0)`** — both orbit axes read 0, so the azimuth gains are exactly equal at 1.0 (the
    `√2` normalisation, FR-025) and width is exactly 100 %. The stage is **transparent to within 1e-6 per
    sample**, not bit-transparent: `MidSideProcessor`'s encode/decode round-trip at 100 % is the algebraic
    identity but not bit-exact in IEEE float (`midside_processor.h:196-207`). FR-026 states it as the
    measurable bound for that reason.
11. **Growth mode with duration 60 s and a 2 s note** — the rise is interrupted at whatever value it
    reached; release proceeds from there. A re-note within the tail continues the rise
    (`growth_envelope.h:158-166`), it does not restart it.
12. **Note frequency at the extremes** — `HarmonicCloud` clamps to
    [`kMinFundamentalHz = 20`, `kMaxFundamentalHz = 4000`] (`:184-185`) and `ContinuousBody` to
    [`kMinNoteHz = 20`, `kMaxNoteHz = 8000`] (`:118-119`). MIDI note 0 (8.18 Hz) and 127 (12 543 Hz)
    therefore clamp **differently** in the two engines. This is accepted and documented, not repaired: the
    body tracks as far as it can and the cloud stops at 4 kHz.
13. **Velocity 0 on note-on** — the allocator maps it to note-off (`voice_allocator.h:230-233`); the
    engine must not also treat it as a note-on.
14. **Same note retriggered while its voice is still `Releasing`** — the allocator revives that slot
    (`:41`, `:239-244`); the voice's cloud is non-quiescent so phases are preserved and no click occurs
    (`harmonic_cloud.h:604-606`). No steal happens, and **no FR-047 teardown**: the slot is live, not
    orphaned. With `RetriggerMode::Legato` (FR-020) the envelope returns to `Sustaining` **at its current
    level** (`multi_stage_envelope.h:106-121`) instead of restarting FR-020's 2000 ms attack, so the
    articulation continues rather than re-swelling — the difference the cloud's own click-freeness does
    not cover.
15. **All voices `Releasing`, new note arrives** — FR-045 step 1 applies; if every candidate is at or
    above `kAmnestyLevelThreshold`, the quietest is still taken (FR-046's "unless no candidate is below
    it" branch). The engine still frees the slot by RA-4's `voiceFinished` step before calling
    `allocator_.noteOn`, because the slot is already `Releasing` and that call is legal
    (`voice_allocator.h:288-292`).

**Sample rate and geometry**

16. **Sample-rate change** — expressed as a fresh `prepare`. All FFT sizes are re-snapped, the capture
    ring is re-rounded to a power of two (so its length in *seconds* changes — RA-8), and the engine ends
    silent. There is no in-place rate change.
17. **96 kHz memory** — every atmosphere capture byte figure doubles
    (`specs/seraphis-phase5-atmosphere/spec.md:214-215`). At FR-014's 4 s default and `kMaxVoices = 16`
    that is **67.2 MB** of capture ring, against 33.6 MB at 48 kHz. Both are ×16, because FR-041 prepares
    all slots; these are the numbers **RQ-2 was answered against** — 4 s per voice, shared ring
    rejected — not a ×8 figure and not the 48 kHz one alone.
18. **Latency** — voice latency is 0 by FR-015. The only reported latency is the Aether stage's
    `getLatencySamples()` (`aether_reverb.h:2612-2614`), which is `diffusionFftSize` when spectral
    diffusion is on and 0 otherwise. The atmosphere's blur latency is deliberately **not** compensated and
    **not** reported at the voice level (FR-015's reasoning).

**Determinism**

19. **Seed 0** — legal everywhere. `deriveStreamSeed` substitutes a non-zero constant if the hash lands on
    0 (`core/random.h:110`), and `Xorshift32` substitutes its own default for a 0 state
    (`continuous_body.h:1126-1127`). Seed 0 is not a "disable".
20. **Re-seeding mid-note** — `ContinuousBody::setSeed` is documented not to re-detune modes already
    ringing (`:1122-1124`). FR-017 therefore restricts seeding to `prepare`/`setSeed`-before-first-note.
    Re-seeding a sounding engine is defined as *"takes effect on the next note"*, not as an error.
21. **Determinism vs. call timing** — two renders with the same seed but different *note timing*
    legitimately differ, because the quiescent branch consumes phase draws
    (`harmonic_cloud.h:612-614`). SC-005 pins the whole call sequence, not just the seed.
22. **Non-finite input** — there is no audio input to this engine. The only non-finite risk is internal
    (feedback, resonance), covered by FR-072 and SC-018.

---

## Resolved Questions

The three decisions the roadmap or an earlier phase explicitly deferred to this spec. **All three were
ruled on by the phase owner on 2026-07-30** and are recorded here with the evidence that produced them;
the FR/SC body already states the decided behaviour, so nothing below is a live question.

- **RQ-1 (was OQ-1) — Shipped voice cap: 8, 12 or 16?** → **RULED: ship 8, gate on the worst case, keep
  the 25 % ceiling.** `kMaxVoices` is compiled at 16 so the cap can be re-derived without an ABI change.
  Roadmap Open Question 4 (line 511) deferred this to Phase 7 *"after budgets are real"*. They now are,
  and the answer turned on **which scenario the gate measures**:
  - **Worst-case scenario** (frozen atmosphere at 2.322 %/voice + Aether config (c) at 1.787 %) — **the
    scenario chosen**, and the one SC-001 measures: 16 → **38.94 %**, 12 → **29.65 %**,
    10 → **25.01 %**, 8 → **20.36 %**. Selects **8**, with 4.6 points spare for the unbudgeted output
    stage, voice sum, spatial stage and macro matrix.
  - **Default scenario** (unfrozen atmosphere at 1.930 %/voice + Aether default (b) at 1.023 %):
    16 → **31.90 %**, 12 → **24.19 %**, 10 → **20.32 %**, 8 → **16.46 %**. Would have selected **12** —
    **rejected**, because a shipped default has to survive the frozen-atmosphere playing technique
    (roadmap line 74) and Aether (c) is reachable from the Phase 8 parameter surface, so a 12-voice
    default would sit at 29.65 % in ordinary use.

  RA-1 and SC-001 state the *same* scenario, so the figures above are the ones the gate reproduces; a
  previous revision derived 8 from the worst-case table while SC-001 named the default Aether and left
  freeze unstated, which is how the two answers got conflated. Both tables use the **measured**
  Aether (c) = 1.787 %, not the padded regression baseline 1.876 % a previous revision carried.
  The third option — ship 12 or 16 and amend roadmap line 311's 25 % ceiling, which N-10 would permit for
  the **Phase 7 global** figure — was **also rejected**: the ceiling stands, and the deviation taken
  instead is the voice count, recorded in the Traceability row for line 311.
  Under FR-010's cloud-only envelope (Clarifications Q1) a released voice renders until it is quiescent,
  so 8 sounding voices is the steady state this ruling was made against, not an outlier.
- **RQ-2 (was OQ-2) — Atmosphere capture length per voice, or a shared ring?** → **RULED: 4 s per voice;
  the shared ring is rejected.** Phase 5's OQ-2 states verbatim that
  the shipped `captureSeconds` *"and any move to a shared ring, are **Phase 7** decisions"*
  (`specs/seraphis-phase5-atmosphere/spec.md:83-86`). FR-014 proposes **4 s per voice**: 2.10 MB/voice →
  **33.6 MB resident @ 48 kHz** and **67.2 MB @ 96 kHz**, both at `kMaxVoices = 16`, because FR-041
  prepares all slots regardless of the default polyphony of 8. (A previous revision quoted 16.8 MB, the
  ×8 figure, understating the resident cost by 2×; the correct basis is Phase 5's own ×16 column,
  `:199`.) The alternative Phase 5 named — one
  ring shared across voices — is **cheaper but changes the semantics**: "the organism feeds on itself"
  becomes "the organism feeds on the ensemble", and per-voice determinism (SC-005/SC-006) would have to be
  restated because voices would then read each other's history. **That is the stated reason it was
  rejected**; SC-005 and SC-006 therefore stand exactly as written, and FR-014's 4 s is the shipped value.
- **RQ-3 (was OQ-3) — Does the macro *mapping* live in DSP (Layer 3) or in the plugin?** → **RULED: DSP
  Layer 3, as `SeraphisMacroMatrix`; SC-009 and SC-010 stay Phase 7 criteria.** The roadmap contradicts
  itself: line 294 puts the macro system in Phase 7 *"as modulation-matrix presets over engine internals
  (reuse `ModulationEngine`)"*, while the reuse inventory's own row for modulation routing (line 94) lists
  the new component as *"macro system (plugin layer)"*. The ruling takes the **Layer 3** reading
  (`SeraphisMacroMatrix`, FR-056…FR-065) for three reasons: SC-009's render-verified macro sweeps are a
  Phase 7 success criterion and need the mapping to exist in Phase 7; the mapping is a pure function of
  five floats and needs no VST parameter IDs; and `ModulationEngine` cannot host it anyway (RA-7:
  `kMaxMacros = 4`, and its routings are keyed on VST parameter IDs). Phase 9 therefore registers five VST
  parameters and calls five setters — nothing more. The alternative (reassign the mapping to Phase 9 and
  move SC-009/SC-010 with it) was **rejected**, so Phase 7's success criteria do not shrink.

---

## Traceability

| Roadmap statement | Line | Requirements | Criteria |
|---|---|---|---|
| Compose phases 1–6 into the playable instrument core | 289 | FR-001…FR-015, FR-040…FR-055 | SC-002 |
| cloud → body → atmosphere tap → envelope → spatial | 285–286 | FR-010, FR-013, FR-025 | SC-002, SC-014 |
| Voice envelope is `MultiStageEnvelope` with slow defaults | 286–287 | FR-019, FR-020, FR-021 | SC-004 |
| **Growth** mode (`GrowthEnvelope` replaces attack) | 287 | FR-021, FR-022 | SC-004 (Growth clause) |
| Per-voice azimuth via `OrbitModulator` + stereo stage | 286 | FR-025…FR-027 (RA-3) | SC-016 |
| 8–16 voices via `VoiceAllocator` | 290 | FR-040…FR-044 | SC-001, SC-012 |
| Steal policy: quietest, with long-release amnesty | 290–291 | FR-045…FR-047 (RA-2, RA-4) | SC-003, SC-011 |
| Unified spread of per-voice life-modulator seeds | 292 | FR-016, FR-050 | SC-005, SC-006 |
| Voice sum → `AetherReverb` → output stage | 293 | FR-051, FR-052, FR-053a, FR-053, FR-054, FR-070 | SC-001, SC-015 |
| Harmonic bloom reinforces the held chord | 276–278 | FR-071 (incl. the `kMaxBloomResonators = 32` cap and its selection rule) | SC-017 |
| Macro system: five performance controls | 294–309 | FR-019, FR-019a, FR-056…FR-065 | SC-009, SC-010 |
| **Full-poly CPU: 16 voices, everything on, ≤ 25 % of one core @ 48 kHz** | 311 | FR-040 | SC-001 (RA-1, RQ-1) — **DEVIATION: gated at 8 voices, not 16.** RA-1's measured tally puts 16 at 38.94 % in SC-001's scenario (1.56× over) and 12 at 29.65 %; only 8 fits, at 20.36 %. The ≤ 25 % half of the roadmap criterion is **met and kept**; the 16-voice half is not, and no voice count satisfies both. `kMaxVoices = 16` is compiled so the cap can be re-derived without an ABI change. **The phase owner ruled on this deviation on 2026-07-30 (RQ-1 / Clarifications Q5): ship 8, gate on the worst case, keep the ceiling.** |
| Voice-steal clicklessness | 312 | FR-047 | SC-003 |
| Macro sweeps render-verified, no discontinuities | 313 | FR-058 | SC-009 |
| Determinism harness, `render_fingerprint.h` not bit-exact | 314–315 | FR-018, FR-084 | SC-005 |
| Nothing is ever static; life mods run at idle | 71–72 | FR-027 (`advanceLifeOnly`), FR-051 | SC-016 |
| Atmosphere freeze is a first-class playing technique | 74 | FR-030a (per-voice triggers + engine-wide `setAtmosphereFreeze`) | SC-001 (measured **with** freeze captured) |
| No aggressive distortion | 78 | FR-053 | SC-015 |
| RT safety: no alloc/lock/exception/IO; pools at prepare | 495 | FR-003, FR-008, FR-013 | SC-007, SC-008 |
| Layer discipline | 496 | FR-001, FR-056, FR-070 | — |
| ODR sweep before every new class name | 497 | New components table | — |
| CPU budgets are FRs | 498–499 | FR-083 | SC-001, SC-002 |
| No bit-exact float goldens | 500 | FR-084 | SC-005, SC-014 |
| Portability: `check-portability.js` | 501–502 | FR-082 | SC-019 |
| Voice count cap decided in Phase 7 | 511 | FR-040 | RQ-1 — **decided: 8 shipped, `kMaxVoices = 16` compiled** |

---

## Review notes

Record of the 2026-07-30 review pass. **No issue was rejected**; every blocker, major and minor was
applied. This section records the four places where an issue offered a choice of resolutions and the
chosen branch is not the first one listed, so a later reader does not re-open a settled decision.

- **Aether integration (three blockers, same defect).** Both offered shapes were viable: split the engine
  surface, or hold the reverb behind a forward-declared / Layer-0 `StereoBlockProcessor` interface. **The
  split is taken** (FR-051 + FR-053a): an abstract interface would put a virtual call on the per-block
  audio path and would still not let `SeraphisMacroMatrix` name the eight `AetherReverb` setters, so it
  solves half the problem at a runtime cost. The macro matrix's Aether half is a POD of floats
  (FR-056) for the same reason.
- **Gravity's neutral (major, FR-064 vs FR-060/SC-010).** The issue offered "default 0.5, bipolar" or
  "unipolar from 0, drop the bipolar sentence". **0.5 is taken.** Roadmap line 307 defines Gravity as an
  "air↔stone density axis", and an axis with only one reachable direction is not that axis; making the
  knob unipolar would have removed "air" from the instrument to satisfy a bookkeeping constraint.
  FR-060 is restated as "every knob at its documented neutral" and SC-009 holds non-swept macros at their
  own neutral, which is the cost of the choice.
- **Dream's base point (blocker, SC-009).** The issue offered non-zero shipped voice defaults, or making
  Dream bipolar around 0.5. **Non-zero defaults are taken** (FR-019), because the same defect affects
  Bloom's `setRichness` row (already at the clamp maximum) and Bloom's morph row (all four state slots
  identical) — one base-point FR fixes all three, whereas a bipolar Dream fixes only Dream. Dream stays
  unipolar; `setDriftDepthCents` is removed from its target list, per the issue's second suggestion.
- **SC-008 vs runtime-sized scratch (major).** The issue offered a compile-time `kMaxBlockSamples` with
  `std::array` scratch, or scoping the grep to "outside `prepare`". **The compile-time bound is taken**
  (FR-004/FR-013): it keeps SC-008's grep a flat, unarguable zero-hit check rather than one that has to
  reason about which lines are inside `prepare`, and 2048 is already `AtmosphereEngine`'s own
  `maxBlockSamples` default (`atmosphere_engine.h:373`), so the clamp costs nothing real.

Two further notes on scope, recorded because they change what a later reader should expect to find:

- **FR-019 and FR-019a are new requirements**, not restatements. They exist because roadmap-normative
  macro sub-axes (Dream's purity rows and spatial-depth row, Bloom's richness row, Bloom's morph row,
  Gravity's damping row) have **zero travel** from the shipped sub-component defaults, which the review
  demonstrated component by component. Any change to the FR-019 table moves SC-009's and SC-010's base
  point and must be made in both places.
- **`advanceLifeOnly`, `processOutputStage`, `collectHeldPartials`'s selection rule, the envelope
  forwarders and six FR-085 accessors are all new surface** introduced by this pass. None of them
  amends a Phase 1–6 component, so N-9 still holds.

Record of the 2026-07-30 **clarification** pass (the eight questions and the three Open Questions, logged
under **Clarifications**). It changed the spec in these places, and nothing else:

- **FR-010's step order** — the envelope moved to the excitation path (Q1). Overview, FR-020, FR-021,
  FR-023 and SC-001's "steady state" note follow from it. RA-2, FR-032, FR-046 and SC-012 were left
  **unchanged**, which was the point of the ruling.
- **FR-019** became a complete, closed table across cloud/morph/body/atmosphere/envelope/spatial (Q2),
  with two zero-travel fixes — spatial depth **0.35** and body damping **0.25** — plus deliberate moves on
  `setStereoSpread` (0.35) and `AtmosphereEngine::setLevel` (0.5). SC-010 grew a clause 1 that asserts the
  whole surface, and its old clauses renumbered 2–4.
- **SC-009** gained a per-macro **minimum end-to-end effect size** table (Q3); FR-058 gained the signed
  `amount` rule and the statement that amounts/curves are tuning, not spec.
- **FR-030a** is new surface (Q4): per-voice `captureFreeze`/`releaseFreeze`/`isFreezeCaptured` plus
  engine-wide `setAtmosphereFreeze(bool)` with a per-voice freeze-pending retry. `getVoice(i)` stayed
  **const** because of it. SC-001 now asserts the capture actually happened.
- **FR-052** pinned `n` to the current polyphony with `kSumGainSmoothMs` (Q6); **FR-033** pinned the level
  detector with `kLevelReleaseMs = 100` (Q7), from which FR-032's ~1.16 s retirement latency and SC-012's
  45 s reclaim window are now *derived* rather than asserted.
- **FR-020** sets `RetriggerMode::Legato` explicitly and **FR-042/FR-047** require the steal teardown on a
  `noteOn` landing on an orphaned post-shrink slot (Q8).
- The **Open Questions** section became **Resolved Questions** (RQ-1/2/3); the *Open Clarifications*
  section was deleted, since every question in it is now decided.
- New named constants introduced by this pass: `kLevelReleaseMs`, `kSumGainSmoothMs`. No Phase 1–6
  component was amended, so N-9 still holds.

