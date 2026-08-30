# Feature Specification: Seraphis Phase 3 — Spectral States & Morphing Engine

**Spec slug:** `seraphis-phase3-spectral-morph`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 3 (lines 170–192)
**Layers:** two new Layer 2 components + one new Layer 3 component, plus one additive amendment to the
Phase 2 Layer 3 component
**Depends on:** Phase 1 (`seraphis-phase1-life-modulators`, COMPLETE — `BrownianDrift`, `SplineTrajectory`
shipped) and Phase 2 (`seraphis-phase2-harmonic-cloud`, COMPLETE — `HarmonicCloud` shipped)
**Plugin work:** none (KrateDSP-only, unit-tested; the Seraphis plugin starts at Phase 8)

## Overview

Phase 3 is the heart of the instrument: instead of sitting on one spectrum, the sound *travels* between
spectral identities (roadmap lines 173–174). It delivers three new components. `SpectralState` (Layer 2,
plain data) is a named target spectrum — 64 partial ratio/amplitude pairs plus tilt and inharmonicity
metadata — with five factory states (pure sine stack, bell, choir/formant, glass, breath) and a versioned
byte-stream serialization (roadmap lines 178–179). `SpectralMorphEngine` (Layer 3) holds 2–4 states and
travels between them along a life-modulated trajectory rather than a linear crossfade, with per-partial
morph completion offsets so low partials arrive before high ones ("bloom" motion) and travel driven either
by Phase 1's `SplineTrajectory` or by an externally-supplied slow ramp (roadmap lines 181–184).
`EntropyProcessor` (Layer 2) is the signature macro: one 0–1 control that applies, in order of increasing
entropy, partial amplitude jitter → phase decoherence → ratio scatter → partial death/rebirth, bounded and
smooth at every setting (roadmap lines 185–188).

*All roadmap line citations in this document were re-verified against `specs/Seraphis-roadmap.md` in the
revision that produced this text. An earlier draft carried a systematic +2/+3 offset throughout the Phase 3
and Cross-Cutting Constraints sections; every citation below is the corrected one.*

Because the roadmap's own Phase 3 success criteria demand rendered evidence ("audible A/B renders for each
factory state pair", line 192) and `HarmonicCloud` as shipped derives every partial ratio and amplitude
from its own five macros with no injection point (verified: `harmonic_cloud.h:1064-1093` and `:1134-1173`
are the only writers of `frequencyHz_`/`baseAmplitude_`, and the public surface at `:341-905` contains no
per-partial ratio or amplitude setter), Phase 3 also adds a strictly additive **spectral-target injection
surface** to `HarmonicCloud`. It is inert unless called, so every Phase 2 success criterion stays valid
unchanged. Phase 3 delivers reusable, unit-tested, RT-safe DSP; it does not deliver a voice, a plugin
surface, or the shipped wiring between the morph engine and the cloud (Phase 7).

Three of this phase's decisions **touch components and budgets the roadmap marks COMPLETE**. They are
collected in "Recorded Roadmap Amendments" below rather than buried in a clarification, because the
roadmap file is the authority and must be updated to match before Phase 7 is specced.

## Recorded Roadmap Amendments

All three are consequences of decisions taken in this spec. None is a licence to relax a Phase 3 threshold;
they exist so a later phase does not inherit a silent contradiction.

> **DISPOSITION (T026, 2026-07-27) — all four are now discharged against `specs/Seraphis-roadmap.md`:**
>
> | | Outcome | Where it landed in the roadmap |
> |---|---|---|
> | **RA-1** | **APPLIED**, both edits | new "Amendments applied by Phase 3" subsection under Phase 2 (after the Phase 2 success criteria) |
> | **RA-2** | **APPLIED** | new blockquote under Phase 7's success criteria |
> | **RA-3** | **NOT TRIGGERED** — measured 29,464 / 29,209 ns/block = 0.276% / 0.274%, inside the 0.5% envelope | roadmap line 164 stands unchanged; the measurement is recorded in the RA-1 subsection |
> | **RA-4** | **APPLIED** | this document — see SC-010's prerequisite paragraph below |
>
> **SUPERSEDED (SC-010 remediation, 2026-07-27) — levers 4 and 5 are BOTH SPENT, and SC-010 is now met on
> all three clauses.** The paragraph this replaces recorded them as unspent.
>
> | Lever | Disposition | Where |
> |---|---|---|
> | **4** — `centsToPitchRatioFast` | **SPENT.** The bounded-domain degree-4 Horner promoted to Layer 0 as `centsToPitchRatioFast`; `HarmonicCloud::detail::centsToDriftRatio` rewritten as a one-line forward to it; `EntropyProcessor::applyStages` calls it instead of an `exp2`. **This is a third amendment to a complete Phase 2 component** and is recorded as such in the roadmap's "Amendments applied by Phase 3" subsection | `core/pitch_utils.h`, `systems/harmonic_cloud.h`, `processors/entropy_processor.h`; gated by the unchanged Phase 2 case `HarmonicCloud_CentsToRatioMatchesExp2` through the forward, plus a new Layer 0 case `CentsToPitchRatioFast_MatchesExp2OnItsDomain` |
> | **5** — entropy OU control interval 32 → 64 | **SPENT.** `EntropyProcessor::kEntropyControlInterval = 64`; both banks re-derive `a`/`g` from the doubled `dt`. Consequences landed in the same change: the equivalence test's stream arm replaced (half-sample-rate reference), the coefficient arm restated at `dt = 64/fs`, SC-013's grid re-run | `processors/entropy_processor.h`; see FR-072's OU bank table |
> | **6** | Not needed, and there is no lever 6 | — |
>
> A **fourth** Phase 2 amendment landed with them, and it is a defect fix rather than an optimisation:
> **FR-085 lever 1's whole-array skip was specified but not implemented.** `setSpectralTarget` ran its full
> per-partial validation scan plus 128 epsilon compares and 128 stores even when the supplied arrays were
> bit-identical to the stored ones, which measured **12.1%** over the no-target cloud against SC-010
> clause 3's 1.10 bound. The skip now runs first, gated on `hasTarget_` and an equal count, and the clause
> measures a ratio of **0.97–1.02** across eight consecutive runs.
>
> `kMorphReferenceNsPerBlock` was **not** raised, `kMaxAdmissibleBaselineNsPerBlock` was not raised, and
> RA-3 stayed untriggered. Both checked-in baselines were **tightened** to the worst of eight measured runs
> (`kMorphBaselineNs` 10,666 → **9,300**; `kCloudChangingTargetBaselineNs` 35,000 → **29,200**), so the
> perf TU now gates regressions as well as the spec budget.
>
> **Roadmap line numbers moved.** RA-1 and RA-2 inserted text, so the line citations throughout this
> document and `plan.md` refer to the **pre-amendment** roadmap (commit `8d90d9ba`). Mapping for the
> load-bearing ones:
>
> | Anchor | Cited as | Now at |
> |---|---|---|
> | Phase 2 cloud budget (≤ 0.5%/voice) | line 164 | line 164 *(unchanged)* |
> | Phase 4 body budget (≤ 1%/voice) | line 219 | line 287 |
> | Phase 5 atmosphere budget (≤ 1%/voice) | line 244 | line 312 |
> | Phase 6 Aether budget (≤ 5% global) | line 272 | line 340 |
> | Phase 7 full-poly ceiling (≤ 25%) | line 299 | line 367 |
> | "CPU budgets are FRs" | line 486 | line 584 |
> | Phase 3 section (goal / components / criteria) | lines 170–192 | lines 217–262 |

- **RA-1 — `HarmonicCloud` (Phase 2, roadmap lines 145–166) gains an additive spectral-target injection
  surface in Phase 3, and its seed hash moves to Layer 0.** The roadmap assigns cloud composition to
  `SeraphisVoice` in Phase 7 (lines 283–286) and lists Phase 3's deliverables as three new L2/L3 components
  only (lines 178–188). The FR-080 series therefore extends a shipped component ahead of its scheduled
  phase. Justification and the rejected alternatives (including the test-local renderer) are in
  Clarifications C-1. **Roadmap action — two edits under Phase 2, not one. BOTH APPLIED 2026-07-27**, as
  the "Amendments applied by Phase 3" subsection under the Phase 2 success criteria:
  1. record that the component gained `setSpectralTarget` / `clearSpectralTarget` / `hasSpectralTarget` in
     Phase 3 **together with the documented ≤ 64-sample slice cadence and call order a consumer must use to
     drive it (FR-086)**, and that its ≤ 0.5%-per-voice budget (line 164) is intended to cover the
     target-active configuration at that cadence **subject to RA-3's measurement**;
  2. record that `HarmonicCloud::deriveSeed` (`harmonic_cloud.h:651-660`) was rewritten in Phase 3 as a
     one-line forward to the new **Layer 0** `deriveStreamSeed(std::uint32_t, std::size_t)` in
     `core/random.h`, body unchanged. That is a second edit to shipped Phase 2 code *and* an addition to a
     shared Layer 0 header, so it belongs in this register and not only in FR-006. It is covered by
     SC-012's `deriveSeed == deriveStreamSeed` equality clause and by SC-014 clause 1 (the whole Phase 2
     suite must pass unedited).
- **RA-2 — the Phase 7 full-poly ceiling (roadmap line 299) no longer follows from the per-voice budgets.**
  Roadmap line 299 reads "16 voices, **everything on**, ≤ 25% of one core", and "everything on" includes the
  global Aether engine, which roadmap line 272 budgets at "CPU ≤ 5% global". The full tally is therefore
  **`16 × 2.65% (per-voice: 0.5% Phase 2 + 1% Phase 4 + 1% Phase 5 + 0.15% Phase 3) + 5% (Phase 6 global,
  roadmap line 272) = 42.4% + 5% = 47.4%`** against the 25% ceiling of roadmap line 299 — not the 42.4% an
  earlier draft of this section stated, which counted only the per-voice sum. On top of that the Phase 7
  output stage (`TapeSaturator` + `TruePeakLimiter`, roadmap line 290) carries **no budget at all** in the
  roadmap and is additionally unaccounted for. Phase 3 does not resolve this and does not pretend to.
  **Roadmap action:** before Phase 7 is specced, either raise the global ceiling, lower the voice count,
  re-derive the per-voice budgets, or budget the output stage; see SC-010's honest note.
  **APPLIED 2026-07-27:** the tally, the 1.9× overage, the unbudgeted output stage and the four permitted
  resolutions are now a blockquote directly under Phase 7's success criteria in the roadmap, so Phase 7
  cannot be specced without reading it. The blockquote also carries the caveat that Phase 3's own 0.15%
  term is measured but not reliably met (0.135% / 0.142% / 0.158% on three consecutive runs), which moves
  the total to 47.5% at the worst measurement and changes nothing about the conclusion.
- **RA-3 — conditional: the Phase 2 cloud budget (roadmap line 164) may have to be restated at the measured
  target-active figure.** SC-010 clause 2 requires the FR-080 injection to fit **inside** the cloud's
  existing 0.5%/voice envelope. The available headroom is real but narrow, and is stated here rather than
  assumed: Phase 2's gating automated measurements reached **29,642.8 ns/block**
  (`specs/seraphis-phase2-harmonic-cloud/compliance.md:80`, SC-007 row) against
  `kMaxAdmissibleBaselineNsPerBlock = kReferenceNsPerBlock / kRegressionFactor = 35,555.6 ns`
  (`dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:101` over `:80`, `:76`) — i.e. **≈ 5,913 ns per
  512-sample block, ≈ 739 ns per 64-sample control chunk**, inside which a 64-partial
  `recalculateFrequencies()` + `recalculateAmplitudes()` must fit. SC-010 therefore requires that figure to
  be **spike-measured before the plan is written** and recorded in the criterion.
  **Trigger and roadmap action:** if the spike shows the target-active configuration cannot meet
  `baseline × kRegressionFactor ≤ kReferenceNsPerBlock` after FR-085's three cost levers have been applied,
  the resolution is *not* to strand the phase and *not* to silently exceed the roadmap: raise roadmap line
  164 from "≤ 0.5%" to the measured figure, record the measurement and the levers already spent, and carry
  the new number into RA-2's tally. This is the third permitted response, alongside FR-085's bit-identical
  skip and the FR-082 identity branch, and it is the only one that changes a roadmap number — so it is
  recorded here rather than being available as an implementation-time convenience.
  **NOT TRIGGERED — measured 2026-07-27 (T022's measurement, re-run at T026).** The trigger condition is
  `measured > kCloudChangingTargetBaselineNs × kRegressionFactor`, i.e. `> 52,500 ns/block`. The
  target-active cloud, driven in the FR-086 shape (8 × 64-sample slices per 512-sample block) with a target
  that moves in **both** ratio and amplitude every chunk, measured **29,464.0 ns/block** and
  **29,209.4 ns/block** on two runs that reached the clause — **0.276% and 0.274%** of one core at 48 kHz,
  against Phase 2's 0.5% (53,333 ns) envelope, with the mask coverage the clause requires actually moving
  (ratio-dirty slots min 63 / mean 63.0 of 64; amp-dirty slots min 45 / mean 51.8 of 64, so a missing
  `ampSlotDirty_` would have shown as a cost). Best-of-25 × 500 blocks, i9-13900HX / Windows 11 Pro 26200 /
  MSVC 19.44.35220.0, `build/windows-x64-release`. **Roadmap line 164 therefore stands unchanged at
  ≤ 0.5%** and no roadmap number was raised. Recorded in the roadmap's Phase 2 amendments subsection.
  *(RA-3's escape is scoped to clause 2 only. SC-010 clause 1 and clause 3 are separately not met — see
  SC-010 and compliance.md — and RA-3 does not and may not cover them.)*

> **AMENDMENT (2026-08-03, phase-owner ruling "relax the gate", recorded from
> `specs/seraphis-phase11-ui/spec.md` D1).** Phase 11's clarification session found that
> `SeraphisVoice::setSpectralState`/`setSpectralStateCount` (Phase 7,
> `dsp/include/krate/dsp/systems/seraphis_voice.h:770-776`, `:908`) reject a state push whenever the voice
> `hasSounded_` and is not `isFinished()`, citing this file's own *"CONFIGURATION-TIME CALLS: prepare(),
> reset(), setSeed(), setState() and setStateCount() are NOT to be called while the consumer is sounding"*
> class comment (`spectral_morph_engine.h:198-206`) as justification — with the effect that a UI ratio/
> amplitude edit landed inaudibly until the next note-on. That comment **over-groups** `setState`/
> `setStateCount` with `reset()`/`setSeed()`: this spec's own FR-042 and FR-044 already establish
> `setState`/`setStateCount` as **continuity-safe** while a voice sounds — both are absorbed by the FR-047
> absorption crossfade (`armStateFade()`, `spectral_morph_engine.h:311`-`:312`, gated on
> `slotContributes(slot)` at `:558`) and are explicitly **not** among FR-044's two named exemptions, which
> are only `setSeed()` and `reset()` (see FR-044 above: *"Exactly two public calls are exempt … `setSeed()`
> … and `reset()`"*). `SeraphisVoice`'s gate was therefore a stricter restriction than this spec requires,
> not a restriction this spec's contract demands. **Two edits are admitted by Phase 11's ruling, both
> outside this spec's own scope but recorded here because they touch a contract this document owns:**
> (1) `spectral_morph_engine.h:198-206`'s class comment is corrected to name only `prepare()`, `reset()`
> and `setSeed()` — comment-only, `setState`'s and `setStateCount`'s bodies are unchanged, and every
> existing test in this phase's suite (FR-044's continuity table, the FR-047 crossfade tests, SC-001
> through SC-013) is asserted to stay green **unmodified** (Phase 11 SC-030). (2) Phase 7's
> `SeraphisVoice::setSpectralState`/`setSpectralStateCount` stop routing through `isConfigurable()`; every
> other `isConfigurable()`-gated caller in Phase 7 is unchanged. Neither edit changes this phase's FR-042,
> FR-044, FR-047 or their measured thresholds. Full detail, click-freeness and audibility criteria:
> `specs/seraphis-phase11-ui/spec.md`, D1, FR-033a, SC-028 – SC-030.

## Scope

In scope for this phase:

- `SpectralState` — Layer 2 plain-data struct at `dsp/include/krate/dsp/processors/spectral_state.h`, with
  the five factory states, an L2 normalization helper, and versioned serialize/deserialize free functions.
- `SpectralMorphEngine` — Layer 3 component at `dsp/include/krate/dsp/systems/spectral_morph_engine.h`
  holding 2–4 state slots, producing per-chunk interpolated ratio and amplitude arrays under a bloom-shaped
  per-partial completion law and one of two travel drivers.
- `EntropyProcessor` — Layer 2 component at `dsp/include/krate/dsp/processors/entropy_processor.h`
  implementing the four staged entropy mechanisms on those arrays.
- An additive `setSpectralTarget(...)` / `clearSpectralTarget()` surface on the existing `HarmonicCloud`,
  plus the composition rules that define how an injected spectrum interacts with the cloud's existing
  Gravity, Inharmonicity, Tilt, Richness, Mutation, drift and envelope stages, **and the required call
  order and ≤ 64-sample slice cadence that joins the two halves** (FR-086).
- Unit tests for the roadmap's stated Phase 3 criteria (morph continuity, entropy monotonicity, state
  round-trip serialization, factory-state-pair A/B renders) plus RT-safety, determinism, sample-rate
  invariance, non-finite hygiene, parameter extremes, and a CPU budget.

## Non-Goals (owned by later phases)

- **The shipped morph-engine → cloud wiring.** `SeraphisVoice` owns the per-voice composition (roadmap
  lines 283–286). Phase 3 ships the two halves (the engine's zero-copy output accessors, FR-008; the cloud's
  injection surface, FR-081) **and the call sequence that joins them** — FR-086 specifies the order and the
  ≤ 64-sample slice cadence, and SC-009 / SC-004 metric 3 verify them composed **in tests** in exactly that
  shape. What remains Phase 7's is instantiating that sequence inside a voice: no production component in
  this phase includes both `spectral_morph_engine.h` and `harmonic_cloud.h`.
- **Per-note travel semantics — whether a note restarts the journey or continues it.** Phase 3 ships **no
  retrigger API**: `reset()` (a configuration-time call, FR-005) is the only rewind, and there is no
  `retrigger()` and no hard `setTravelPosition()`. **Owner: Phase 7** (`seraphis-phase7-voice-engine`,
  roadmap lines 281–290), which decides per-voice policy — calling `reset()` at voice allocation gives a
  per-note restart, not calling it gives continuation from wherever the previous note left `p`. Named here
  so the decision is not lost between phases, exactly as host-synced travel registration is assigned to
  Phase 9 further down this list.
- **The resonant body, granular atmosphere and Aether space.** Phases 4, 5 and 6.
- **Voice envelopes, allocation, per-voice seed spreading, spatial position.** Phase 7 (roadmap lines
  281–290).
- **Modulation routing.** Neither `SpectralMorphEngine` nor `EntropyProcessor` is registered as a
  `ModulationEngine` / `VoiceModRouter` source or destination; that wiring was deferred to Phase 7 by
  Phase 1's Clarifications OQ1 (`specs/seraphis-phase1-life-modulators/spec.md:49-51`) and Phase 2 kept it
  there (`specs/seraphis-phase2-harmonic-cloud/spec.md:59-61`). The engine owns its `SplineTrajectory`
  instance directly.
- **The Entropy *macro*, Dream/Bloom/Dissolve/Gravity as macros, and any parameter ID, UI, preset or plugin
  state work.** Phases 7–12. The controls here are plain C++ setters in normalized units.
- **Host-synced travel (deriving a travel rate from tempo + note value).** Roadmap line 184 names
  "host-synced slow ramp" as one of the two travel drivers. Phase 3 ships the **whole DSP half** of it:
  `setTravelRate(float journeysPerSecond)` (FR-061) is the ingestion point, and converting `(bpm,
  noteValue)` to journeys/s is one division on the caller's side — no engine API is missing and no later
  phase needs a further DSP change. **Owner: Phase 9** (`seraphis-phase9-parameters`, roadmap lines 425–427), which
  registers the morph-rate parameter with a free/synced selector and a note-value list, exactly as every
  other synced rate in this repo is done. Recorded here so the requirement is not lost between phases.
- **Runtime authoring of states from live audio or analysis.** No `HarmonicFrame`, no `HarmonicSnapshot`,
  no capture path (see Clarifications C-9 and the Existing Components table). `SpectralState` is
  assignable and serializable; it is not *derived* from anything in this phase.
- **State-authoring mutators** (`setPartial`, `blendStates`, `tiltState`, …). Deferred to Phase 9 by
  Clarifications C-9, which resolves the roadmap's Open Question 2 (line 497).
- **Any change to Phase 2 behaviour when the injection surface is not used.** The amendment is additive
  only; SC-014 is the standing regression gate on that.

## Clarifications

Binding decisions. Nothing here is deferred to implementation time. Each records *why*, so the plan does
not re-litigate it.

### Session 2026-07-26 — clarification interview

One line per question asked and the decision taken. Every decision below is encoded in the FR/SC bodies of
this document; this log is a record of *what was decided and when*, never the place a reader has to go to
learn the behaviour.

- **Q1 — What is the exact public shape of `SpectralMorphEngine`'s output-array accessors?** →
  **Zero-copy pointers into stable internal storage:** `const float* getOutputRatios() const noexcept`,
  `const float* getOutputAmplitudes() const noexcept`, `std::size_t getOutputCount() const noexcept`,
  feeding `HarmonicCloud::setSpectralTarget(const float*, const float*, std::size_t)` directly with no
  scratch buffer. Per-index scalar getters remain **only** for introspection quantities that criteria read
  singly (completion fractions, stage weights, `L_i`, scatter). FR-085's whole-array skip keeps a stable
  comparand as a consequence. Encoded in FR-008, FR-085, FR-086, Non-Goals.
- **Q2 — At what cadence must the composed engine → cloud path run, and is that cadence a requirement or a
  test convenience?** → **A requirement, written as an explicit FR in the FR-080 series (FR-086):** the
  consumer drives the cloud in ≤ 64-sample slices — `engine.updateChunk` → `cloud.setSpectralTarget` →
  `cloud.processStereoBlock`, per slice — with the call order spelled out in the header docs. SC-009 and
  SC-004 metric 3's harnesses exercise exactly that shape, so the tested path is the shipped path. Encoded
  in FR-086, FR-044, FR-085, SC-004, SC-009, SC-010.
- **Q3 — What is the engine's complete post-construction / post-`prepare()` default state?** → **All four
  state slots default to `makeFactoryState(SineStack)`**, `numStates = 2`, travel position `p = 0`,
  `bloom = 0`, `entropy = 0`, `TravelMode::External`, travel rate `kMinTravelRate`. The full set is
  enumerated as a table in FR-005, and SC-002 clause 5 asserts the post-`prepare` output arrays directly —
  an audible pure-harmonic stack, with the morph a no-op until configured. No silent default and no timbral
  opinion. Encoded in FR-005, SC-002 clause 5, Edge Cases.
- **Q4 — Should the cloud's FR-017 normalizer see the entropy-perturbed amplitudes or the clean pre-entropy
  set?** → **Keep FR-083 as written:** the normalizer consumes the entropy-perturbed amplitude set, so
  entropy is level-neutral and the dissolve is purely spectral — which is what keeps a 64-partial sum off
  `kOutputClamp`. **Plus** a new SC-004 metric 4 measuring *absolute* rendered RMS across the entropy sweep,
  so the level-neutrality is a recorded number rather than a Phase 7 surprise. Any real level decay belongs
  to Phase 7's voice envelope. Encoded in FR-083, SC-004 metric 4.
- **Q5 — Are `setSeed()` and `reset()` audio-thread calls that must be continuous, or configuration-time
  calls permitted to step the output?** → **Configuration-time calls.** Documented "not to be called while
  the consumer is sounding", explicitly **named** as exemptions in FR-044's continuity list (never silently
  omitted), and SC-001 states that it does not exercise them. This matches the Phase 1 precedent this phase
  reuses: neither `BrownianDrift::reset()` (`brownian_drift.h:133`) nor `SplineTrajectory::reset()`
  (`spline_trajectory.h:144`) makes a continuity claim. Phase 7 seeds at voice construction/`prepare`, never
  at note-on. Encoded in FR-004, FR-005, FR-006, FR-044, SC-001.
- **Q6 — Does the morph journey retrigger per note, and does Phase 3 owe an API for it?** → **No retrigger
  API in Phase 3.** `reset()` is the only rewind, and **Phase 7 by name** owns per-note travel policy
  (`reset()` at voice allocation ⇒ per-note restart; no call ⇒ continuation from wherever the previous note
  left `p`). Recorded in Non-Goals so the decision is not lost between phases. Encoded in Non-Goals, FR-005.
- **OQ2 (roadmap line 497) — spectral state authoring.** → **Assign + serialize only in Phase 3:** factory
  states or deserialized states assigned to slots, with a versioned round-trip. **No** authoring mutators
  and **no** capture-from-live-cloud — the latter rejected *permanently*, because it would re-open the
  `HarmonicFrame`/analysis dependency the roadmap closed for Seraphis (roadmap lines 159–161). If Phase 9
  introduces user-editable states, mutator free functions can be added then without changing this phase's
  contract. Confirms **C-9** as written; no FR or SC changes.

Four internal-consistency defects found in the same review pass were put to the user with the questions
below and decided in the same session. None relaxes a threshold.

- **GATE-FIX-1 — SC-002 clauses 3 and 4 are derived against a travel rate they never configure. Which
  rate?** → **`setTravelRate(kMaxTravelRate)` in both clause configurations, stated explicitly.** Clause 4's
  `slewCap = 3.0` units/s derivation already presupposes it (`slewCap = rate · (numStates − 1)`, FR-061) and
  clause 3's 1200 s run-length derivation requires the limiter to be inactive so the position attains each
  waypoint value. This does **not** change the FR-005 default — `kMinTravelRate` remains the default; the
  criteria simply pin the settings they measure at, as every other criterion here does. Encoded in SC-002
  clauses 3 and 4.
- **GATE-FIX-2 — the Existing Components table still points `crossfadeIncrement` and
  `OnePoleSmoother::snapTo` at "FR-062's absorption ramp", which FR-062 abolished. Keep or delete?** →
  **Delete both references.** FR-062 was resolved as "a mode switch changes the shared slew limiter's
  *target*, never the position" — no absorption ramp exists anywhere in this phase. `OnePoleSmoother`'s row
  is reworded to its remaining legitimate use (smoothed engine-level controls); `crossfade_utils`' row is
  reworded to record it as examined-but-unused, since FR-047's absorption crossfade is a linear `x` on
  `chunkSeconds`, not a per-sample increment. Encoded in Existing Components.
- **GATE-FIX-3 — FR-041 says `kFillSpacingCents = 28.0` "stays below the 27.32-cent figure", which is
  arithmetically false. Which way?** → **"stays *above*".** The fill's 28.0 is **looser** than the factory
  minimum 27.32, which is precisely why it never becomes the tightest spacing and why SC-002 clause 2's
  minimum-spacing argument is unchanged. A pure wording inversion — every constant and every derivation
  stays as it is. Encoded in FR-041.
- **GATE-FIX-4 — C-4 claims "every `EntropyProcessor` criterion is measured on it directly", which SC-004
  contradicts. Narrow it to what?** → **Name the criteria.** `EntropyProcessor` is measured directly by
  **SC-005, SC-006 and SC-016 only**; SC-004 metrics 1–2 read the *engine*'s FR-008 output arrays
  differenced against its clean (pre-entropy) arrays, and SC-004 metrics 3–4 render through the composed
  `HarmonicCloud` path. Encoded in C-4.

- **C-1 — Where the morph output lands: `HarmonicCloud` gains an injection surface. DECIDED.** Four
  alternatives were weighed: (a) give the morph engine its own partial bank; (b) test the morph engine only
  on its output arrays with no render; (c) render through a **test-local** additive oscillator bank driven
  from the engine's FR-008 arrays; (d) add an injection surface to `HarmonicCloud`.
  - (a) duplicates the SIMD MCF kernel and the FR-017 normalizer and creates a second thing Phase 7 must
    reconcile.
  - (b) cannot satisfy roadmap line 192 ("audible A/B renders for each factory state pair") or line 190
    ("morph continuity — no clicks"), both of which are properties of rendered audio.
  - (c) is the cheapest option and the one with the best phase hygiene: it produces rendered audio for
    SC-001 clause 2 and SC-009 without touching a shipped Phase 2 component, without the injection cost
    inside SC-010, and without needing SC-014 as a standing regression gate. It is **rejected**, for one
    reason: the load-bearing DSP question this phase must answer is *how an injected spectrum composes with
    the cloud's Gravity, Inharmonicity, Tilt, Richness-count, Mutation, drift and envelope stages* — that is
    the entire content of C-2 and C-3 and of FR-082/FR-083. A test-local renderer answers none of it; it
    would render the engine's arrays in a vacuum and leave Phase 7 to discover the composition rules with no
    spec, no tests and no neutrality gate. The rendered evidence would be evidence about a test fixture
    rather than about the instrument. The 0.35%-of-a-core it saves is measured, not hypothetical, but it is
    the cheaper of the two things being traded.
  - (d) is chosen. It is **additive**: the parametric path is untouched and remains the default, and
    supplying the identity arrays (`ratio_n = n`, `amp_n = n^(−p(r))`) must reproduce the parametric render
    (SC-014). Encoded in the FR-080 series.

  Because (d) amends a component and a budget the roadmap marks COMPLETE, it is recorded as an explicit
  roadmap amendment (RA-1) rather than as a clarification alone.
- **C-2 — Injected ratios replace the integer grid; Gravity and Inharmonicity still compose. DECIDED.**
  `HarmonicCloud::recalculateFrequencies()` currently forms
  `f_n = f0 · n^(1+g·kGravityExponentRange) · sqrt(1 + B·n²)` (`harmonic_cloud.h:1083-1092`), which
  factorises as `f_n = f0 · n · n^(g·kGravityExponentRange) · sqrt(1 + B·n²)`. Injection replaces **only
  the leading `n`**; the gravity warp and the inharmonic stretch still multiply in the same FR-083 order.
  Rationale: Gravity and Inharmonicity are cloud macros Phase 7 keeps live, and this factorisation is the
  unique one under which `ratio_n = n` is an identity at all.
  *What the shipped `gravityIsZero` branch actually selects — the point an earlier draft of this spec got
  wrong.* Verified this session at `harmonic_cloud.h:1065`, `:1082`, `:1085-1086`:
  `const float exponent = 1.0f + gravity_ * kGravityExponentRange;` … `const bool gravityIsZero =
  (gravity_ == 0.0f);` … `const float ratioG = gravityIsZero ? n : std::exp2(exponent *
  detail::kHarmonicCloudLog2N[i]);`. Because `exponent` carries the leading `1`, **`ratioG` is the whole
  `n · n^(g·range)` product, not the warp factor alone**, and the branch's `n` arm is the whole product too.
  So "replace the leading `n` and keep the `gravityIsZero` branch verbatim" is self-contradictory — keeping
  the branch and multiplying by `ratio_override[i]` counts `n` twice at gravity 0. The injected law is
  therefore stated over an explicitly **warp-only** factor:
  `warp_i = gravityIsZero ? 1.0f : std::exp2(gravity_ · kGravityExponentRange ·
  detail::kHarmonicCloudLog2N[i])`, `f_n = f0 · ratio_override[i] · warp_i · stretch(n)`. Encoded in FR-082.
  *Precision of the identity, stated honestly:* at `gravity == 0` the warp branch yields exactly `1.0f`, so
  supplying `ratio_override[i] = n` reproduces the parametric `f0 · n · stretch` **bit-exactly**. At
  `gravity ≠ 0` (which SC-014 clause 2 explicitly exercises at `±1`) the injected two-factor form
  `ratio_override[i] · exp2(g·range·log2N[i])` and the parametric one-factor form
  `exp2((1 + g·range)·log2N[i])` are the same real number but **not the same float expression**, so the
  identity is a `render_fingerprint.h`-tolerance match there — which is what that clause already asserts.
  FR-082's identity guard closes even that gap, but it must fall back to the **unmodified `ratioG`
  computation of `:1082-1086`, including its own `gravityIsZero` branch** — *not* to the `std::exp2` arm
  alone. Falling back to the `exp2` arm at gravity 0 would evaluate `exp2(1.0f · log2N[i])`, which is
  precisely the rewrite the shipped comment at `:1066-1072` warns hands back `31.999998` for `n = 32` under
  `-ffast-math`, i.e. the guard would destroy the very bit-exactness it exists to protect, and
  `render_fingerprint.h` tolerances in SC-014 would not catch it. Encoded in FR-082.
- **C-3 — Injected amplitudes replace the Richness rolloff; Tilt still multiplies; Richness still sets the
  active count. DECIDED.** `recalculateAmplitudes()` currently forms
  `a_n = n^(−p(r)) · tiltGain(n)` for `n ≤ N(r)` (`harmonic_cloud.h:1146-1157`). Injection replaces the
  `n^(−p(r))` factor only. Tilt is a global brightness macro that must keep working over any spectrum;
  Richness's **count** law `N(r) = clamp(round(64^r), 1, 64)` (`:1138-1139`) still gates which slots are
  active, so Richness keeps its most audible role and FR-043's click-free tail retirement
  (`:1159-1164`, `:1181-1192`) is untouched. Richness's **rolloff exponent** has no effect while a spectral
  target is active — that is deliberate: multiplying a state's own amplitude shape by `n^(−p)` would erase
  the timbral distinction between the factory states, which SC-008 exists to protect. Encoded in FR-083.
- **C-4 — `SpectralMorphEngine` owns the `EntropyProcessor`. DECIDED.** The L2 processor stays independently
  constructible and unit-testable — **`SC-005`, `SC-006` and `SC-016` measure it directly**, while SC-004's
  metrics 1–2 read the *engine*'s FR-008 output arrays differenced against its clean (pre-entropy) arrays
  and SC-004's metrics 3–4 render through the composed `HarmonicCloud` path — but the L3
  engine holds one instance and applies it to its own output as the last stage, forwarding a single
  `setEntropy(float)`. Rationale: Phase 7 then wires one object per voice and the roadmap's "Entropy —
  direct wire to Phase 3 `EntropyProcessor`" macro (line 297) is a single forwarding setter, not a second
  object to route. Encoded in FR-045 and FR-070.
- **C-5 — Entropy's "phase decoherence" is realized in the ratio domain as a zero-mean time-varying
  detune, not as a phase discontinuity. DECIDED.** `HarmonicCloud` exposes `getPartialSinState(std::size_t)`
  and `getPartialCosState(std::size_t)` (`harmonic_cloud.h:819`, `:823`) but **no phase setter** — the only
  writer of the MCF phase state outside the kernel is the private `redrawPhases()`
  (`harmonic_cloud.h:970-...`), reachable only from a quiescent `noteOn()` (`:593-595`). Adding a public
  phase setter would hand callers a guaranteed click generator: stepping `sinState`/`cosState` on a
  sounding partial is a waveform discontinuity by definition. A small zero-mean random walk on the partial's
  frequency accumulates into a phase error that grows without bound relative to the ideal partial — which
  *is* decoherence — while being continuous in the rendered waveform by construction. Encoded in FR-072;
  measured by SC-016. Recorded deviation from the roadmap's wording (line 186), which names the *effect*,
  not a mechanism.
- **C-6 — Entropy stages are overlapping monotone ramps over the single control, not hard bands.
  DECIDED.** Roadmap line 185 says "in order of increasing entropy" and line 188 says "bounded and smooth at
  every setting". Hard band edges would make each stage's onset a slope discontinuity in the disorder
  metrics and would make SC-004's monotonicity brittle at the joins. Each stage weight is a clamped linear
  ramp over its own sub-interval, with adjacent intervals overlapping. Encoded in FR-071 and its table.
- **C-7 — Amplitudes interpolate linearly in magnitude; ratios interpolate geometrically. DECIDED.**
  Magnitude-linear is the verified law from the component the roadmap names as the concept reference
  (`SpectralMorphFilter::applyMagnitudeInterpolation`, `magA·(1−u) + magB·u`,
  `spectral_morph_filter.h:591-606`) and is reused unchanged. Ratios are *pitch* quantities: linear
  interpolation from ratio 2 to ratio 4 passes through 3 (a minor-third-flat detour), whereas
  `exp2(lerp(log2 rA, log2 rB, u))` passes through 2.828 — the geometric midpoint, one octave-linear step.
  `log2(ratio)` is precomputed per state at assignment time (config rate), so the per-chunk cost is 64
  lerps + 64 `exp2`. Encoded in FR-041.
- **C-8 — 64 partials, fixed, matching Phase 2. DECIDED.** `SpectralState::kStatePartials = 64` equals
  `HarmonicCloud::kMaxPartials = 64` (`harmonic_cloud.h:133`), fixed by Phase 2 Clarifications OQ-1. There
  is no tier and no runtime capacity change. The constant is **class-scoped**, never namespace-scoped: a
  namespace-scope `kMaxPartials = 96` already exists in `Krate::DSP` (`harmonic_types.h:21`) and a second
  namespace-scope partial-count constant is an ODR/confusion hazard.
- **C-9 — Phase 3 ships *assign + serialize* authoring only. Authoring mutators are Phase 9's. DECIDED.**
  This resolves the roadmap's Open Question 2 (line 497: *"Spectral state authoring: factory-only or
  user-morphable/savable states — Phase 3/9"*), whose Phase 3 half is the **DSP capability** and whose
  Phase 9 half is the **plugin surface**. It was previously carried in this document as an open question,
  which contradicted this section's own preamble ("Binding decisions. Nothing here is deferred to
  implementation time") and would have been settled by default in the plan rather than by decision.
  **Decision:** `SpectralState` is a plain assignable struct (FR-011) with factory constructors (FR-021),
  a normalization helper (FR-014) and versioned serialization (FR-031). A caller may build one field by
  field; nothing in the library *derives* one. No FR or SC in this document changes as a result — this is
  exactly what is already specified.
  **What is deferred, and to whom, by name so it is not lost:** the authoring mutators
  (`setPartial(index, ratio, amplitude)`, `blendStates(A, B, t)`, `tiltState(state, dB)` and any siblings),
  as free functions over the struct, are assigned to **Phase 9** (`seraphis-phase9-parameters`, roadmap
  lines 425–427) — the same phase that owns host-synced travel registration, and the phase that first has a
  user-facing reason for them. They must preserve FR-012's validity invariants, and that preservation is
  Phase 9's criterion to state. Recorded in Non-Goals.
  **What is rejected outright:** a *capture path* deriving a `SpectralState` from a live `HarmonicCloud` or
  from analysis. It would re-open the `HarmonicFrame` question the roadmap closed for Seraphis at lines
  159–161 and is not available at any phase without a new roadmap decision.
  *Why deciding now costs nothing:* the mutators of the deferred option operate on a **plain aggregate with
  no invariants enforced at construction** (FR-011: trivially copyable, no constructor logic). Adding free
  functions over such a struct in a later phase is not a retrofit — it adds symbols and touches no existing
  one. Had `SpectralState` been a class with encapsulated state the calculus would be different; it is not.

## Assumptions (recorded interpretations, not deferrals)

1. **"a named target spectrum: 64 partial ratio/amp pairs + tilt/inharmonicity metadata" (roadmap line
   178)** is read as: the ratios and amplitudes are the *load-bearing* data (they are what the morph
   interpolates and what reaches the cloud), while `tiltDbPerOct` and `inharmonicity` are **metadata
   carried with the state, not applied by `SpectralState` itself**. They exist so a state can record the
   cloud settings it was authored against, and so Phase 9's preset layer has somewhere to put them. No FR
   in this phase reads them into the audio path; FR-013 states that explicitly so the plan does not invent
   a second application site for tilt (the cloud already owns `tiltGain`, `harmonic_cloud.h:1105-1111`).
2. **"Related to `harmonic_snapshot`, but source-agnostic — verify no ODR overlap" (roadmap line 180)** is
   read as: `SpectralState` is a *new, separate* struct and `HarmonicSnapshot` is neither reused nor
   extended. Verified reason (not assumed): `HarmonicSnapshot` is sized by the analysis pipeline's
   `kMaxPartials = 96` (`harmonic_snapshot.h:34-37` over `harmonic_types.h:21`), carries analysis-only
   fields (`f0Reference`, `residualBands`, `residualEnergy`, `spectralCentroid`, `brightness`,
   `inharmonicDeviation`, per-partial `phases`) and its only producers are `captureSnapshot(const
   HarmonicFrame&, const ResidualFrame&)` (`harmonic_snapshot.h:75`) and `recallSnapshotToFrame(...)`
   (`:125`), both `HarmonicFrame`-typed. Seraphis has no analysis pipeline (roadmap lines 159–161). The ODR
   sweep result is in the New Components table.
3. **"travels between them along a life-modulated trajectory (not a linear crossfade)" (roadmap lines
   181–182)** is read as two independent departures from a linear crossfade, both required: (a) the travel
   *coordinate* is driven by a life modulator or an external ramp rather than being a directly-set position
   (FR-060 series), and (b) the *per-partial* completion is staggered by the bloom law so the spectrum does
   not cross-fade as one rigid object (FR-050 series). Either alone would still be a crossfade in one of
   the two senses.
   *Departure (a) has to survive the rate limiter, and that is a criterion, not a hope.* A slew limiter that
   saturates most of the time turns the travel coordinate into a constant-rate ramp — which is exactly a
   linear-crossfade coordinate, i.e. departure (a) nullified. FR-061 therefore states the shared cap as a
   **journey-fraction** rate (`kMaxTravelRate · (numStates − 1)` units/s) rather than as an absolute
   units/s figure, and **SC-002 clause 4 measures the fraction of chunks on which the Spline limiter is
   active** at the component's own default waypoint interval and `numStates = 4`, against a stated bound.
   Without that clause the roadmap's "not a linear crossfade" has no criterion that can fail: the earlier
   absolute cap of 1.0 units/s saturated for **every** `numStates ≥ 3` at `SplineTrajectory`'s
   `kDefaultInterval = 2.0` (`spline_trajectory.h:123`), and neither SC touching Spline mode would have
   detected it.
4. **"holds 2–4 states" (roadmap line 181)** is read as a **chain**, not a simplex: the travel coordinate
   is a scalar `p ∈ [0, numStates−1]` and only the two states bracketing `p` contribute at any instant.
   A 3- or 4-way barycentric blend is not implemented. Rationale: the roadmap describes *travel* between
   identities ("the sound travels between spectral identities", line 173) and a one-dimensional journey is
   what `SplineTrajectory` — a scalar source (`spline_trajectory.h:204`) — and a "host-synced slow ramp"
   (line 184) both natively produce. A simplex would need a 2-D or 3-D driver that no Phase 1 component
   emits (`OrbitModulator` emits 2-D, but the roadmap does not name it here).
5. **"partial death/rebirth (partials fade out and re-emerge slightly detuned)" (roadmap lines 186–187)**
   is read as an amplitude-domain lifecycle with ramped edges, whose *rebirth* applies a new ratio offset
   drawn while the partial is silent. Rationale: re-emerging "slightly detuned" only makes sense if the
   detune changes across the death, and changing it while the partial is audible would be a pitch step; the
   silent window is the one place it can change for free. Encoded in FR-073.
6. **"morph continuity (no clicks, max per-block amp delta bounded)" (roadmap line 190)** is read as a
   bound on the **per-control-chunk** delta, not the per-`processBlock` delta. The consumer's block size is
   the host's business and Phase 2 already made the cloud block-size-invariant on a 64-sample internal grid
   (`harmonic_cloud.h:139` `kControlChunkSamples = 64`; `:713-746`). A bound stated per host block would
   scale with an arbitrary number. Encoded in FR-044 and SC-001.

## Functional Requirements

Each requirement is testable and traces to a specific roadmap line (or is listed in the completeness table
below as spec-added, with its reason).

### FR-001 series — shared contract, layering and lifecycle

- **FR-001** — `SpectralState` is a Layer 2 plain-data struct at
  `dsp/include/krate/dsp/processors/spectral_state.h`, including only Layer 0/1 headers and stdlib. It has
  no constructor logic, no virtuals, is trivially copyable, and is safe to hold by value on the audio
  thread. Trace: roadmap line 178 ("Layer 2, plain data"); layer discipline (roadmap line 484).
- **FR-002** — `EntropyProcessor` is a Layer 2 component at
  `dsp/include/krate/dsp/processors/entropy_processor.h`, including only Layer 0/1/2 headers and stdlib
  (intra-Layer-2 inclusion is the established pattern — e.g. `harmonic_snapshot.h:15`,
  `modal_resonator_bank.h:14-15`). Trace: roadmap line 185 ("Layer 2").
- **FR-003** — `SpectralMorphEngine` is a Layer 3 component at
  `dsp/include/krate/dsp/systems/spectral_morph_engine.h`, including only Layer 0/1/2 headers and, for the
  Layer 3 case, nothing (it does **not** include `harmonic_cloud.h` — Non-Goals). Trace: roadmap line 181
  ("Layer 3").
- **FR-004** — All three components are real-time safe: every processing and parameter method is
  `noexcept` and performs no heap allocation, locks, exceptions or I/O. All state is fixed-size member
  storage sized at compile time. `prepare()` is the only non-RT method on the two stateful components;
  `SpectralState` has none. Trace: roadmap line 483.
  *RT-safe is not the same as continuity-preserving, and the two are separated deliberately.* `setSeed()`
  and `reset()` are RT-safe by this FR (allocation-free, lock-free, `noexcept`) but are **configuration-time
  calls**: they are permitted to step the output and are named exemptions in FR-044's continuity list. See
  FR-005 and FR-006 for the documented contract, and SC-001 for the criterion that states it does not
  exercise them.
- **FR-005** — `SpectralMorphEngine` and `EntropyProcessor` each expose
  `prepare(double sampleRate) noexcept` and `reset() noexcept`. `prepare` re-derives every
  sample-rate-dependent coefficient (travel rate per sample, entropy walk coefficients, all owned
  `BrownianDrift`/`SplineTrajectory` instances via their own `prepare(double)`,
  `brownian_drift.h:121` / `spline_trajectory.h:136`) and floors the rate at 1 Hz as those components do.
  `reset()` rewinds to the exact post-`prepare` state including every RNG stream, matching
  `BrownianDrift::reset()` (`:133`) and `SplineTrajectory::reset()` (`:144`).
  *`reset()` is a configuration-time call.* It is documented in the header as **not to be called while the
  consumer is sounding**: a rewind of the travel position, every OU walk and every static scatter draw is a
  step by definition, and pretending otherwise would be a false contract. It is a named exemption in
  FR-044's continuity list and SC-001 states that its sweep does not exercise it. This is the same contract
  the two Phase 1 components this phase reuses already have — neither `BrownianDrift::reset()` nor
  `SplineTrajectory::reset()` makes a continuity claim. Phase 7 therefore rewinds at voice
  construction/`prepare` or at voice allocation, never mid-note (Non-Goals).
  *Post-construction and post-`prepare` defaults are enumerated, not implied.* After default construction
  and after `prepare(sampleRate)` with **no** parameter call, `SpectralMorphEngine` holds exactly:

  | Quantity | Default | Why |
  |---|---|---|
  | `state[0..3]` (all four slots) | `makeFactoryState(SpectralStateId::SineStack)` | Audible pure-harmonic stack. Not the default-constructed `SpectralState`, which is silent (FR-011, `numPartials = 0`) and would make a forgotten `setState` an invisible mute with no criterion failing. |
  | `numStates` | `2` | The minimum legal count (FR-042). |
  | travel position `p` | `0.0f` | Segment start; `k = 0`, `u = 0`. |
  | `bloom` | `0.0f` | `u_i = u` for every partial (FR-051) — no timbral opinion baked in. |
  | `entropy` | `0.0f` | Every stage weight `w_k(0) = 0` (FR-071) — "angelic purity", roadmap line 187. |
  | `TravelMode` | `TravelMode::External` | Deterministic and stationary until driven; the Spline driver is opt-in. |
  | travel rate | `kMinTravelRate` (= 1/600 journeys/s) | The slowest legal rate — nothing moves fast before it is asked to. |

  Because **all** slots hold the same state, the default configuration is exactly the "both slots identical
  ⇒ perfectly static output" case of Edge Cases — an already-required corner becomes the well-trodden path,
  and the morph is a verifiable no-op until a caller assigns a second identity. The four
  `makeFactoryState` calls are deterministic and consume no RNG (FR-023), so the defaults are bitwise
  reproducible; each slot's `log2(ratios)` precompute (FR-042), including the FR-041 fill entries, is
  performed for the defaults too. After `prepare`, the output arrays are **well-defined with no prior
  parameter call and with no advance** — they already hold the SineStack ratio/amplitude set rather than
  waiting for a first `updateChunk` to populate them — and SC-002 clause 5 asserts them directly.
- **FR-006** — All stochastic behaviour is driven by seeded `Xorshift32` streams
  (`dsp/include/krate/dsp/core/random.h:40`) reachable through an explicit `setSeed(std::uint32_t)` on each
  stateful component, so a given seed plus a given call sequence reproduces the same output.
  *`setSeed` is a configuration-time call, on the same contract as `reset()` (FR-005).* It redraws all 64
  FR-072c static scatter offsets — a step of up to `2 · kMaxScatterCents = 14` cents per partial in one
  chunk — so it is documented as **not to be called while the consumer is sounding**, is a named exemption
  in FR-044's continuity list, and is not exercised by SC-001. Phase 7 spreads per-voice seeds at voice
  construction/`prepare`, never at note-on (Non-Goals). It remains RT-safe in the FR-004 sense
  (allocation-free, `noexcept`), and SC-012 still exercises it for determinism — determinism and continuity
  are different properties.
  Per-partial
  streams are derived through a documented non-zero-guaranteeing hash. A lane must never be handed 0,
  because `Xorshift32::seed()` silently substitutes its own default (verified: `random.h:72-74`,
  `state_ = (seedValue != 0) ? seedValue : kDefaultSeed`) and two lanes hashing to 0 would collapse onto one
  stream.
  *The hash is promoted to Layer 0 by this phase.* The only such hash in the tree today is
  `HarmonicCloud::deriveSeed(std::uint32_t base, std::size_t salt) noexcept`, verified this session at
  `harmonic_cloud.h:651-660` — `public static constexpr`, lowbias32 finaliser, explicit non-zero
  substitution at `:659`. It is a **Layer 3** symbol, and FR-002 (Layer 2, no Layer 3 includes) and FR-003
  (must not include `harmonic_cloud.h`) both forbid the new components from reaching it. Rather than
  duplicating the body in two places, this phase adds
  `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept`
  to `dsp/include/krate/dsp/core/random.h` with the **identical body** (same constants, same shift
  sequence, same `0x2545F491u` non-zero substitution), and rewrites `HarmonicCloud::deriveSeed` as a
  one-line forward to it. That forward is behaviour-preserving by construction and is covered by SC-014
  clause 1 (the whole Phase 2 suite must pass unedited). `deriveStreamSeed` is ODR-swept in the New
  Components table. Every seed/determinism criterion in this document (SC-006, SC-012, SC-015) is stated
  against the **Layer 0** symbol.
  *Required include, stated because a green Windows build cannot catch its absence.* Verified this
  session: `dsp/include/krate/dsp/core/random.h` includes **only `<cstdint>`** (`random.h:17`; the file is
  94 lines and has no other include). The new function's `std::size_t` parameter requires `<cstddef>`, and
  MSVC and libstdc++ both usually supply it transitively from `<cstdint>` — so this is exactly the class of
  omission `tools/check-portability.js` exists for. `HarmonicCloud::deriveSeed` gets away with it only
  because `harmonic_cloud.h` pulls `<cstddef>` in via its other includes. **`core/random.h` must therefore
  add `#include <cstddef>` alongside the new function.**
  Trace: Phase 1's determinism criterion (roadmap line 131) carried forward; Phase 7's determinism harness
  (lines 300–303). Recorded as a roadmap amendment in RA-1.
- **FR-007** — Every parameter setter clamps its input to a documented range, and **rejects** non-finite
  input: on NaN or Inf the setter returns without touching any state, so the corresponding getter and the
  produced output are unchanged. The rejection idiom is the finite-first bit test, never `std::isnan` (the
  macOS leg builds `-ffast-math`). The helpers are **Layer 0**: `Krate::DSP::detail::isNaN` at
  `dsp/include/krate/dsp/core/db_utils.h:54` and `detail::isInf` at `:174` (namespace `detail` opens at
  `:39`), both `constexpr` over `std::bit_cast<std::uint32_t>`. `harmonic_cloud.h:342-344` is a **use
  site**, not the definition — it reaches them via its own `db_utils.h` include at `:18`. A Layer 2
  component must include `<krate/dsp/core/db_utils.h>` directly; reaching for the Layer 3 header would
  violate FR-002. Array-valued inputs (FR-081) are rejected **wholesale**: if any element is non-finite or
  out of range, the entire call is a no-op.
- **FR-008** — Both stateful components expose a **test/introspection surface**, `const noexcept` unless
  noted, cheap enough to call from a test loop, and part of the public contract (not `#ifdef` scaffolding).
  *The output arrays are read through zero-copy pointer accessors, and the signatures are pinned here
  because Phase 7 consumes them and three requirements depend on their shape.* `SpectralMorphEngine`
  exposes exactly:

  ```cpp
  [[nodiscard]] const float* getOutputRatios() const noexcept;      // kStatePartials floats, stable storage
  [[nodiscard]] const float* getOutputAmplitudes() const noexcept;  // kStatePartials floats, stable storage
  [[nodiscard]] std::size_t  getOutputCount() const noexcept;       // the FR-041 active count
  ```

  Both pointers address **stable internal member storage** whose address does not change over the
  component's lifetime; only the contents change, and only inside `updateChunk` (FR-043). Consequences,
  each load-bearing:
  - the FR-086 composition is copy-free and needs no scratch buffer — the pointers are handed straight to
    `HarmonicCloud::setSpectralTarget(const float*, const float*, std::size_t)` (FR-081), whose signature
    they were chosen to match;
  - FR-085 lever 1 (the bit-identical whole-array skip) has a **stable comparand**: the cloud compares the
    supplied arrays against its own stored copy, and the engine's storage does not move underneath it;
  - no consumer copies 128 floats per chunk per voice. At 16 voices and a 64-sample chunk that would be
    2048 stores per chunk of pure overhead against a 0.15% budget (SC-010 clause 1).

  **Per-index scalar getters exist only for the introspection quantities criteria read singly** — completion
  fractions, stage weights, `L_i`, the two scatter accessors below, the travel position, the repair counter.
  They are deliberately *not* the way the output arrays are read.
  Required minimum beyond the three accessors above: the **pre-entropy (clean) ratio and amplitude arrays**,
  exposed in the same zero-copy pointer shape (`getCleanRatios()` / `getCleanAmplitudes()`) so SC-004 and
  SC-005 can measure entropy's contribution differentially without a copy; the current travel
  position and per-partial completion fractions; and a monotonic
  **repair-engagement counter** incremented whenever FR-046 changed any ratio (`SpectralMorphEngine`);
  each entropy stage's current weight `w_k`, each partial's current amplitude-jitter factor, decoherence
  detune, death/rebirth lifecycle phase, its amplitude factor `L_i`, and a
  monotonic per-partial **scatter-redraw counter** (`EntropyProcessor`); and a
  `stateFinite() const noexcept` bit-pattern check on each, following `HarmonicCloud::stateFinite()`
  (`harmonic_cloud.h:858-...`). Spec-added: SC-002 (clauses 2–4), SC-003, SC-004, SC-005, SC-006 and SC-016
  are unmeasurable without it — in particular SC-006's "every redraw happened at `L_i == 0.0f`" clause
  needs the redraw counter and `L_i` in the same read, and SC-016 needs `w_3` to assert its measurement
  point is below stage 3's onset.
  *The static scatter is exposed twice, under two distinct names, and the distinction is load-bearing.*
  FR-072c draws `s_i` **unconditionally, once per seed**, independently of `w_3` — so "the static scatter
  offset" is ambiguous between the raw draw and the applied offset, and SC-005's clause at `entropy = 0.40`
  ("still exactly 0") is true under one reading and false under the other. The surface therefore names both:
  - `getAppliedScatterCents(std::size_t i) const noexcept` returns the **applied** offset in cents,
    `w_3 · kMaxScatterCents · s_i` — exactly 0 whenever `w_3` is 0, regardless of the draw. **This is the
    accessor SC-005 and SC-016's comparison arm are stated against.**
  - `getRawScatterDraw(std::size_t i) const noexcept` returns the raw `s_i ∈ [-1, +1]`, and
    `getScatterRedrawCount(std::size_t i) const noexcept` the monotonic redraw counter. **These are what
    SC-006's "every redraw occurred at `L_i == 0.0f`" clause reads**, because a redraw is observable
    through the counter whether or not `w_3` is nonzero.

### FR-010 series — `SpectralState` data model (roadmap line 178)

- **FR-011** — `SpectralState` declares a class-scoped
  `static constexpr std::size_t kStatePartials = 64` and a class-scoped
  `static constexpr std::size_t kStateNameBytes = 16`, and holds exactly:
  `std::array<float, kStatePartials> ratios`, `std::array<float, kStatePartials> amplitudes`,
  `std::array<char, kStateNameBytes> name`, `float tiltDbPerOct`, `float inharmonicity`, `int numPartials`.
  No other fields. It remains trivially copyable and aggregate-initializable. The default-constructed value
  is a valid, finite, silent, **anonymous** state (`numPartials = 0`, all arrays zero, `name` all `'\0'`).
  *Why `name` exists:* roadmap line 178 asks for "a **named** target spectrum", and the identity has to live
  on the struct rather than only in `SpectralStateId` — a state built field-by-field, deserialized, or
  authored through Phase 9's deferred mutators (C-9) would otherwise be anonymous, and Phase 9's preset
  layer would have nowhere to carry the label through a round-trip. Fixed-capacity `char` keeps FR-001's
  plain-data and RT-safety properties (no `std::string`, no allocation, no pointer). Trace: roadmap line 178.
- **FR-012** — Validity invariants, checked by a `[[nodiscard]] bool isValidSpectralState(const
  SpectralState&) noexcept` free function. **Enforced in full at FR-031 (deserialize) and FR-042
  (`setState`) only** — FR-081 (`setSpectralTarget`) applies the narrower list enumerated in FR-081 and is
  authoritative for that entry point; see the note below. The invariants:
  `numPartials ∈ [0, kStatePartials]`; every `ratios[i]` for `i < numPartials` is finite and in
  `[kMinStateRatio, kMaxStateRatio] = [0.5f, 128.0f]`; `ratios` is **strictly increasing** over
  `[0, numPartials)`; every
  `amplitudes[i]` is finite and in `[0, 1]`; `name` contains at least one `'\0'` (so it is always safe to
  read as a C string) and no byte before the first `'\0'` is a control character (`< 0x20`) or `0x7F`;
  `tiltDbPerOct` finite in `[-12, +12]` (the range convention adopted from
  `SpectralTilt::kMinTilt`/`kMaxTilt`, `spectral_tilt.h:98-101`); `inharmonicity` finite in `[0, 0.1]`
  (`HarmonicCloud::kMaxInharmonicity = 0.1f`, `harmonic_cloud.h:186`). Entries at `i ≥ numPartials` are
  ignored by every consumer and are not required to satisfy anything.
  *Why `kMinStateRatio` exists (it was not in an earlier draft, which required only `ratio > 0`).* Two
  downstream derivations need a **finite log-ratio span**, not just a positive one: FR-044's cents budget is
  `factor × 1200·log2(maxRatio / minRatio)`, and FR-046's repair headroom argument is stated in cents. With
  no lower bound, an FR-012-valid state such as `numPartials = 2, ratios = {1e-30, 128}` makes that span
  unbounded and `kMaxRatioDeltaCentsPerChunk` provably not a bound. `0.5f` is one octave below the
  fundamental — enough for a deliberate sub-octave partial, which is the only musical reason to go below 1
  — and gives the pinned span in FR-044. All five factory states have `ratios[0] = 1.0` and satisfy it with
  a full octave to spare.
  *Why FR-081 is deliberately narrower.* Full FR-012 enforcement at the cloud's injection point would be
  actively wrong: FR-074 permits the morph engine's output amplitudes to reach `(1 + kMaxAmpJitter)` times
  their input, so the entropy-perturbed arrays this phase is designed to feed the cloud would be rejected by
  the `amplitude ≤ 1` clause. `setSpectralTarget` therefore checks finiteness, `ratio > 0`, `amplitude ≥ 0`
  and the count range, and **not** the `≤ 1` amplitude bound, the
  `[kMinStateRatio, kMaxStateRatio]` bounds, or strict ratio
  monotonicity. A test written against FR-012 must not be pointed at `setSpectralTarget`; SC-015 enumerates
  exactly which malformed arrays that entry point must reject.
- **FR-013** — `tiltDbPerOct`, `inharmonicity` and `name` are **metadata only**: no component in this phase
  reads them into the audio path. They round-trip through serialization (FR-031) and are copied by state
  assignment; nothing else. This is a *testable negative*, not a comment: SC-008 asserts that mutating these
  three fields on a state already loaded into the engine leaves a render bitwise unchanged.
  Trace: Assumption 1.
- **FR-014** — `void normalizeSpectralState(SpectralState&) noexcept` L2-normalizes `amplitudes` over
  `[0, numPartials)` so `Σ a_i² = 1`, following the verified pattern at `harmonic_snapshot.h:100-107`
  (guarded by `sumSquares > 0`, no-op on an all-zero state). Normalization is what makes the FR-041 morph
  level-neutral and makes SC-008's pairwise distances comparable. All factory states ship normalized.

### FR-020 series — factory states (roadmap line 179)

- **FR-021** — `enum class SpectralStateId : std::uint8_t { SineStack = 0, Bell, Choir, Glass, Breath }`
  plus `kSpectralStateCount = 5`, and
  `[[nodiscard]] SpectralState makeFactoryState(SpectralStateId) noexcept` returning a by-value,
  FR-012-valid, FR-014-normalized state whose `name` field is set to the id's ASCII label
  (`"SineStack"`, `"Bell"`, `"Choir"`, `"Glass"`, `"Breath"`, NUL-padded). One entry point, not five free
  functions, to keep the new namespace-scope symbol count minimal. Trace: roadmap line 179.
  *Unused ratio slots are filled, not left zero.* For a state with `numPartials < kStatePartials`,
  `makeFactoryState` writes the **FR-041 geometric continuation** (clamped as FR-041 specifies) into
  `ratios[i]` for `i ≥ numPartials`, and 0 into `amplitudes[i]`. FR-012 does not *require* anything of those
  entries, but leaving them at 0 makes SC-008's ratio term `log2(0 / n) = −∞` and the whole distance metric
  Inf/NaN for every pair involving Bell, which is the one state with `numPartials < 64`. Filling them costs
  nothing (they are amplitude-0) and makes the metric executable for all 10 pairs.
- **FR-022** — The five states are defined by explicit, documented laws so they are reproducible and
  reviewable rather than magic tables. Each occupies all 64 slots unless stated:
  - **SineStack** — the pure harmonic reference: `ratio_n = n`, `amp_n = n^(−1)`. This is the state against
    which "angelic purity" is defined, and the one whose ratios are exactly the integer grid.
  - **Bell** — inharmonic metal: `ratio_n = n · sqrt(1 + B·n²)` at **`B = 0.04`**, `amp_n = n^(−1.4)`, with
    `numPartials = 24` (bells are sparse; the remaining slots are zero-amplitude).
    *`B` is pinned by arithmetic, not taste.* FR-012 caps every ratio at `kMaxStateRatio = 128.0f`, and the
    law's top slot must clear it: at `B = 0.04`, `ratio_24 = 24·sqrt(1 + 0.04·576) = 24·sqrt(24.04) =
    24·4.9031 = 117.7 ≤ 128` ✔ (and `ratio_23 = 23·sqrt(22.16) = 108.3`). At the originally-drafted
    `B = 0.06` the law breaches the cap two slots early — `ratio_23 = 23·5.7219 = 131.6` and
    `ratio_24 = 24·5.9632 = 143.1` — so `makeFactoryState(Bell)` could not have returned an FR-012-valid
    state and SC-008 could not have passed on a faithful implementation. The plan may not re-derive `B`;
    if it wants a different value it must re-run this arithmetic against `kMaxStateRatio` and record it
    here.
  - **Choir** — formant-weighted harmonic: `ratio_n = n`, amplitudes shaped by three Gaussian formant
    bumps in the partial-index domain (documented centres and widths) over a `n^(−0.8)` base.
  - **Glass** — bright, near-harmonic, odd-weighted: `ratio_n = n · (1 + 0.004·n)`, `amp_n = n^(−0.5)` with
    even partials attenuated by a documented factor.
  - **Breath** — dense, dark, near-flat upper region: `ratio_n = n`, `amp_n` from a shallow `n^(−0.25)`
    rolloff with a documented low-partial attenuation, `numPartials = 64`.
  The remaining constants (formant centres/widths, the Glass even-partial attenuation, the Breath
  low-partial attenuation) are the plan's to pin; the FR is that each is a **closed-form documented law**,
  not a literal table, and that the five satisfy SC-008's pairwise distinctness.
  *Binding design constraint on those constants.* All five states are L2-normalized (FR-014), so for the
  three states sharing `ratio_n = n` (SineStack, Choir, Breath) SC-008's distance reduces to
  `sqrt(2·(1 − ρ))` where `ρ` is the cosine similarity of their amplitude vectors. SC-008's a-priori
  threshold `kMinFactoryStateDistance = 0.4` therefore requires **`ρ ≤ 0.92` for every pair**. A bare
  rolloff difference does not achieve that (normalized `n^(−1)` against `n^(−0.8)` correlates above 0.99);
  the Choir formant bumps and the Breath low-partial attenuation are what must carry it. The plan must
  compute the 10 pairwise `ρ` values while choosing the constants, not after.
- **FR-023** — `makeFactoryState` is deterministic and stateless: two calls with the same id produce
  bitwise-identical states, and it consumes no RNG. This is what lets SC-007's serialization digest and
  SC-009's A/B renders be stable across runs.

### FR-030 series — serialization (roadmap line 192, "state round-trip serialization")

- **FR-031** — Two free functions over a raw byte stream:
  `[[nodiscard]] std::size_t serializeSpectralState(const SpectralState&, std::byte* dest, std::size_t
  capacity) noexcept` returning bytes written (0 on insufficient capacity or an FR-012-invalid state), and
  `[[nodiscard]] bool deserializeSpectralState(const std::byte* src, std::size_t size, SpectralState& out)
  noexcept` returning false — leaving `out` untouched — on a short buffer, an unknown version byte, or a
  payload that fails FR-012. Both are RT-safe and allocation-free.
- **FR-032** — The stream begins with `kSpectralStateFormatVersion = 1` (one byte) followed by a
  fixed-layout little-endian payload carrying **every** FR-011 field in a fixed order: `numPartials`
  (`std::int32_t`), `tiltDbPerOct`, `inharmonicity`, the 64 `ratios`, the 64 `amplitudes`, then the 16
  `name` bytes verbatim. `kSpectralStateBytes` is a compile-time constant equal to the exact serialized size
  (`1 + 4 + 4 + 4 + 256 + 256 + 16 = 541`) and is `static_assert`ed against the layout. Floats are written
  by `std::memcpy` of their bit pattern (no `reinterpret_cast` aliasing, no text formatting). The format
  version stays at 1 — nothing has shipped, so `name` is part of v1 rather than a bump.
- **FR-033** — Round-trip fidelity is **exact**: `deserialize(serialize(s)) == s` field-by-field with `==`
  on the float bits, because the bytes are stored values, not arithmetic results. This is the one place in
  this phase where a byte-stream digest is a legitimate golden (`dsp/CLAUDE.md`: digests over a serialized
  byte stream are fine); it is **not** a float-render golden and SC-007 must not be confused for one.

### FR-040 series — `SpectralMorphEngine` slots and morph law (roadmap lines 181–182)

- **FR-041** — With travel position `p ∈ [0, numStates−1]`, let `k = floor(p)`, `u = p − k`, and let
  `A = state[k]`, `B = state[min(k+1, numStates−1)]`. For each partial `i` the engine produces, using that
  partial's own completion fraction `u_i` from FR-051:
  - `amplitude_i = A.amplitudes[i]·(1 − u_i) + B.amplitudes[i]·u_i` — magnitude-linear, the law reused
    verbatim from `SpectralMorphFilter::applyMagnitudeInterpolation` (`spectral_morph_filter.h:591-606`);
  - `ratio_i = exp2( log2(A.ratios[i])·(1 − u_i) + log2(B.ratios[i])·u_i )` — geometric, per
    Clarifications C-7. `log2` of each slot's ratios is precomputed at assignment time (FR-042), so the
    per-chunk cost is a lerp plus one `exp2` per partial.
  - The engine's active count is `max(A.numPartials, B.numPartials)`. Slots beyond a state's own
    `numPartials` contribute **amplitude 0** and a ratio that **continues that state's own ratio sequence
    monotonically but boundedly**:

    ```
    g_j = clamp(r_{j−1} / r_{j−2}, 1.0f, kMaxFillGrowth)          // kMaxFillGrowth     = 2.0f
    r_j = max( min(r_{j−1} · g_j, kMaxFillRatio),                 // kMaxFillRatio      = kMaxStateRatio = 128.0f
               r_{j−1} · kFillSpacingFactor )                     // kFillSpacingCents  = 28.0f
                                                                  // kFillSpacingFactor = exp2(28/1200) ≈ 1.016258
    ```

    for `j ≥ numPartials`, seeded from the state's last two real ratios (`r_j = j + 1` when the state has
    fewer than two).
    *Why not `i + 1`:* the naive fill rule breaks FR-074's strictly-increasing invariant outright. With
    Bell in slot A (`numPartials = 24`, `ratio_23 = 108.3`, `ratio_24 = 117.7`) morphing against any
    64-partial state, at `u = 0` the output would read `…, 117.7, 25, 26, …` — a 4.7:1 *decrease* at the
    fill boundary.
    *Why the clamps, which an earlier draft omitted — this is the load-bearing correction.* The bare
    geometric continuation `r_j = r_{j−1}·(r_{j−1}/r_{j−2})` is an **unbounded** progression and FR-012
    admits states that make it overflow. `numPartials = 2, ratios = {1, 128}` is FR-012-valid (finite,
    strictly increasing, inside `[kMinStateRatio, kMaxStateRatio]`), so `setState` accepts it (FR-042), and
    the bare fill gives `r_j = 2^(7j)`: `r_18 = 2^126`, `r_19 = 2^133` — **`Inf` in `float`** at slot 19 of
    64. `log2(Inf)` then propagates through FR-041's interpolation and FR-074's "each output ratio is
    finite" and SC-015's "no output array element is non-finite" become unsatisfiable on a legal input. An
    ordinary `{1, 8}` two-partial state overflows by slot 43. The same rule also invalidated FR-044's cents
    budget: a `{1, 4}` two-partial state produces a per-partial log-ratio divergence of ~96,000 cents,
    ~11× the 8,400 cents the earlier derivation assumed. Nothing downstream repairs it — the cloud's
    `updateAntiAliasGain` (`harmonic_cloud.h:1229-1244`) silences partials at or above Nyquist but does not
    bound the ratio itself. `kMaxFillGrowth = 2.0f` caps the per-slot growth at one octave and
    `kMaxFillRatio = kMaxStateRatio = 128.0f` caps the absolute value at the largest ratio a real state may
    hold — the fill slots are amplitude-0 in their own state, so clamping costs nothing audible.
    The factory set is unaffected in character: Bell's fill runs 108.3 → 117.7 → 127.9 → 128 and from there
    climbs the `kFillSpacingCents` staircase.
  - *Why the `max(...)` floor, and why `kFillSpacingCents = 28.0` rather than reusing
    `kMinRatioSpacingCents = 24.0`.* Without the floor a capped fill goes **flat** at `kMaxFillRatio` (the
    `{1, 128}` case is flat from slot 2, Bell's from slot ~27), which breaks FR-074's strictly-increasing
    invariant and would force FR-046's repair to engage on every chunk of every factory pair involving
    Bell — destroying SC-002 clause 2's "repair never engages at `bloom = 0`" proof. Building the staircase
    into the fill instead keeps the repair provably inert. `28.0` sits **above** `kMinRatioSpacingCents`
    with 4 cents of slack for exactly the reason FR-046 keeps 3.32 cents of slack against the 27.32-cent
    `64/63` spacing: an *equal* comparison would let float rounding decide whether `max` engages. It also
    stays **above** the 27.32-cent figure that clause 2's "tightest spacing in the factory set" argument
    rests on being the minimum — the fill's 28.0 is looser than that minimum, so it never becomes the
    tightest spacing and the argument is unchanged.
  - Consequently the hard ceiling on any output ratio is
    `kMaxOutputRatio = kMaxFillRatio · exp2((kStatePartials − 1) · kFillSpacingCents / 1200)
    = 128 · 2^1.47 = 354.6`, a compile-time expression `static_assert`ed against the FR-044 derivation.
    (`kStatePartials − 1 = 63` staircase steps is a safe over-bound; the cap can be reached no earlier than
    slot 2.)
  - **Both interpolation inputs are strictly increasing arrays** after that fill rule and FR-046. The
    interpolated output is *not* automatically so — under bloom, `u_i` varies with `i`, and a large
    log-ratio divergence between A and B combined with the `u_i` stagger can invert an adjacent pair.
    FR-046 is what closes that.
    Trace: roadmap line 181.
- **FR-042** — `void setState(int slot, const SpectralState&) noexcept` for `slot ∈ [0, 3]` and
  `void setStateCount(int n) noexcept` for `n ∈ [2, 4]` (clamped). `setState` rejects an FR-012-invalid
  state wholesale (FR-007) and precomputes that slot's `log2(ratios)` (including the FR-041 fill entries).
  Changing a state or the count while travelling **must not step the output**; the mechanism that achieves
  that is FR-047's absorption crossfade, not a deferral to the next chunk. Trace: roadmap line 181 ("holds
  2–4 states").
  *Why a deferral is not enough — an earlier draft claimed it was.* "The newly-effective interpolation is
  applied from the next control chunk" does not shrink the step; it only delays it by one chunk. Swapping
  slot 1 from SineStack to Bell at `u_i = 0.5` changes each interpolated amplitude by
  `0.5·|a_Bell,i − a_Sine,i|`; both states are L2-normalized (FR-014) so partial 1 alone moves
  `≈ 0.5·|0.938 − 0.782| = 0.078`, **more than three times** `kMaxAmpDeltaPerChunk = 0.025` (and four times the 0.02 of the draft in which this defect was found). `setStateCount(4 → 2)`
  is worse: it re-clamps `p` from up to 3.0 down to 1.0, i.e. onto an entirely different state pair.
  Roadmap line 190 asks for no clicks and a bounded per-chunk amplitude delta, and a state swap is
  something Phase 7's macro and preset layers will do while the voice sounds — so the answer is a
  mechanism, not a weakened assertion.
- **FR-043** — `void updateChunk(std::size_t numSamples) noexcept` is the single per-control-chunk entry
  point, and the stage order inside it is fixed: advance the travel driver by `numSamples` (FR-061) →
  recompute `u_i` (FR-051) → recompute the interpolated arrays (FR-041) → run the monotone-spacing repair
  (FR-046) → apply the FR-047 absorption crossfade if one is in flight → apply the owned `EntropyProcessor`
  (FR-070). The repair sits **between** interpolation and
  entropy, which is what lets FR-074 state its no-crossing budget against a known 24-cent floor. It is the
  only method that changes the output arrays. Chunk length is the caller's; the engine's behaviour is defined in Hz and
  seconds, so any chunk length is legal (SC-013).
- **FR-044** — **Morph continuity.** Between two consecutive `updateChunk` calls of `≤ 64` samples at
  48 kHz, and with every driver at its fastest legal setting, the change in any partial's output amplitude
  is `≤ kMaxAmpDeltaPerChunk` and in any partial's output ratio is `≤ kMaxRatioDeltaCentsPerChunk` **cents**
  (not ratio units — an absolute ratio delta is meaningless across a range spanning `kMinStateRatio` to
  `kMaxOutputRatio`). No stage may introduce a discontinuity: every stage in FR-041, FR-046, FR-047,
  FR-051, FR-061 and the FR-070 series is a continuous function of `p` and of time.
  *The exemptions are named, not silently omitted.* Exactly two public calls are **exempt** from this bound,
  and they are listed here so a reader never has to infer the omission: **`setSeed()`** (FR-006) and
  **`reset()`** (FR-005). Both are configuration-time calls documented as not to be called while the
  consumer is sounding — `setSeed` redraws all 64 static scatter offsets (a step of up to
  `2 · kMaxScatterCents = 14` cents per partial in one chunk) and `reset()` is a rewind of the travel
  position, every OU walk and every once-per-seed draw, i.e. a step by definition. SC-001 clause 1 states
  explicitly that its sweep does not call them. Every **other** public call — `setState`, `setStateCount`,
  `setTravelMode`, `setTargetPosition`, `setTravelRate`, `setBloom`, `setEntropy`, `updateChunk` — is
  covered by the bound, through FR-047's absorption crossfade or FR-062's shared slew limiter where the
  underlying change would otherwise step.
  *The cents span, pinned before the table.* Every cents term below is a factor times the **maximum
  per-partial log-ratio divergence**, which FR-012's `kMinStateRatio` and FR-041's `kMaxOutputRatio`
  now make finite:

  ```
  kOutputCentsSpan = 1200 · log2(kMaxOutputRatio / kMinStateRatio)
                   = 1200 · log2(354.6 / 0.5) = 1200 · log2(709.2) = 11,364 cents
  ```

  An earlier draft used `1200·log2(kMaxStateRatio) = 8,400` cents, which silently assumed every output ratio
  lies in `[1, 128]` — false under both the FR-041 fill and FR-012's then-absent lower bound. The span is a
  compile-time expression, not a literal, so it moves automatically if either constant does.
  *The bound is the sum of every contributor, enumerated.* Both constants are compile-time and
  `static_assert`ed against this derivation; a term may not be dropped because it is "small". With
  `T = chunkSeconds = 64/48000 = 1.333 ms` and the shared travel cap
  `R = kMaxTravelRate · (kMaxStates − 1) = 1.0 · 3 = 3.0` units/s (FR-061):

  | Contributor | Amplitude term | Cents term | Value at `T` |
  |---|---|---|---|
  | Travel (External *and* Spline — both rate-limited to `R`, FR-061) | `R · T / (1 − kMaxBloomFraction)` × max amplitude span (≤ 1 after FR-014) | same factor × `kOutputCentsSpan` | amp 1.00e-2; cents 113.6 |
  | State/count change absorption (FR-047) | `T / kStateChangeFadeSec` × max amplitude span (≤ 1) | `T / kStateChangeFadeSec` × `kOutputCentsSpan` | amp 6.67e-4; cents 7.58 |
  | Death/rebirth ramp (FR-073) | `T / kMinDeathFadeSec` × partial amplitude | 0 (the ratio redraw happens at amplitude factor exactly 0 — FR-073, FR-074) | amp 2.67e-3 |
  | Amplitude-jitter OU (FR-072a) | `kMaxAmpJitter · 2 · (1 − exp(−T / kDriftOutputSmoothSec))` | 0 | amp 8.85e-3 |
  | Decoherence OU (FR-072b) | 0 | `kMaxDecoherenceCents · 2 · (1 − exp(−T / tau_decohere))` | cents 0.348 |
  | Static scatter (FR-072c) | 0 | 0 while alive (static); redrawn only at amplitude factor 0 | 0 |

  **`kDriftOutputSmoothSec = 0.150` means `tau = 0.150 s` — i.e. a `750` ms setting in `OnePoleSmoother`'s
  time-to-99% convention, NOT `BrownianDrift::kDriftOutputSmoothMs = 150` (RESTATED, deviation D12).** The
  OU walk value itself is bounded to `[-1, +1]`, so its smoothed output can traverse at most the full range
  at the one-pole rate.
  *Why the restatement (D12).* `OnePoleSmoother`'s parameter is **time to 99%**, with
  `coeff = exp(−5000/(ms · fs))` (`smoother.h:86-91`), i.e. `tau = ms/5000` seconds. A `150` ms setting is
  therefore a **30 ms** time constant, not 150 ms. The table above is derived at `tau = 0.150 s`, so the
  amplitude-jitter bank must be configured at **`kEntropyAmpSmoothMs = 5 × 150 = 750` ms**
  (`entropy_processor.h:117-118`) to deliver exactly the `tau` the row assumes. Read the other way — feeding
  that bank `150` ms — the per-chunk step becomes `4.347e-2` instead of `8.849e-3` (×4.91) and the amplitude
  sum becomes `5.680e-2` against `kMaxAmpDeltaPerChunk = 0.025`: the `static_assert` does not compile. The
  **decoherence** bank keeps the `150` ms setting (`kEntropyCentsSmoothMs`, `entropy_processor.h:116`) so
  that one bank stays bit-comparable to a stock `BrownianDrift` for the OU-equivalence test; its `tau` is
  therefore `0.030 s` and its cents contribution rises from `0.071` to `0.348`, which the cents budget
  absorbs with ~3.1 cents to spare. **No threshold moved:** `kMaxAmpDeltaPerChunk` stays at `0.025` and
  `kMaxRatioDeltaCentsPerChunk` stays at `125.0`. Summing:
  `kMaxAmpDeltaPerChunk = 0.025` (sum 2.219e-2, rounded up) and `kMaxRatioDeltaCentsPerChunk = 125.0`
  (sum 121.53 = 113.6 + 7.58 + 0.348, rounded up). Both scale with `chunkSeconds` at other chunk lengths and
  rates (SC-013).
  *(Footnote, recorded at T026 rather than silently propagated: the shipped `kOutputCentsSpan` is
  **11,392.0** cents, not the 11,364 above, because deviation D13 raised `kMaxOutputRatio` from 354.6 to
  **360.37** — the fill's worst case needs 63 chained float multiplies to reach the ceiling, so a
  mathematically tight ceiling can be missed by accumulated rounding. Re-summed at 11,392 the cents total is
  **121.83**, still under 125.0. Both bounds are enforced as real `static_assert`s at
  `spectral_morph_engine.h:181-188` over `kOutputCentsSpan` at `:127-129`, so the compiled arithmetic — not
  this prose — is the binding statement.)*
  *Why these are larger than the earlier draft's `0.02` / `32.0`, and why that is a re-derivation rather
  than a relaxation.* Three pinned constants changed, each for a reason recorded above, and the bound
  follows arithmetically from them: (i) the travel cap became a **journey-fraction** rate, `3.0` units/s at
  `numStates = 4` instead of a flat `1.0`, because the flat cap saturated the Spline driver for every
  `numStates ≥ 3` at the component's own default waypoint interval and thereby turned the "life-modulated
  trajectory" of roadmap lines 181–182 into a constant-rate ramp — i.e. the old constant was wrong against
  the roadmap, not merely inconvenient (Assumption 3); (ii) `kOutputCentsSpan` replaced an assumed
  `[1, 128]` range that FR-041's fill and FR-012's missing lower bound both broke; (iii) FR-047 added a
  contributor that did not previously exist because state changes were (incorrectly) assumed free. No
  contributor was removed and no factor was softened.
  Trace: roadmap line 190 ("no clicks, max per-block amp delta bounded"); Assumption 6.
- **FR-045** — The engine owns exactly one `EntropyProcessor` instance and forwards `setEntropy(float)`,
  `setSeed(...)` (deriving a distinct salt for the entropy streams), `prepare(...)` and `reset()` to it
  (Clarifications C-4). The clean, pre-entropy arrays remain readable through FR-008 so SC-004 and SC-005
  can measure entropy's contribution differentially.
- **FR-046** — **Monotone-spacing repair (spec-added).** After FR-041's interpolation and **before** the
  FR-070 entropy stages, the engine runs one forward pass enforcing a minimum adjacent spacing:
  `r_i ← max(r_i, r_{i−1} · kMinRatioSpacingFactor)` for `i ≥ 1`, with
  `kMinRatioSpacingCents = 24.0f` and `kMinRatioSpacingFactor = exp2(kMinRatioSpacingCents / 1200)
  ≈ 1.013937`. Properties, each testable:
  - The pass is a composition of `max` over continuous functions, so it is **continuous** in `p` and in
    time and cannot click; it costs 64 compares and at most 64 multiplies.
  - It guarantees the pre-entropy array is strictly increasing with **at least 24 cents** of headroom
    between neighbours, which is the property FR-074's entropy budget is stated against — so FR-074's
    no-crossing constraint becomes a `static_assert` on two constants rather than an unprovable claim
    about "the minimum spacing across all factory states".
  - It is **inert on the factory set**: the tightest clean spacing anywhere in the five states is partials
    63→64 of the three `ratio_n = n` states, `1200·log2(64/63) = 27.32` cents > 24.0, so the repair never
    engages on a clean morph between them and SC-002's `1e-6` endpoint tolerance is unaffected. The 3.32
    cents of slack is deliberate: it keeps float rounding at the boundary from tripping the `max`.
    That inertness now extends to the **fill region too**, which it did not in an earlier draft: FR-041's
    continuation carries its own `kFillSpacingCents = 28.0` floor, so Bell's capped tail (slots ~27–63 at
    `kMaxFillRatio`) is a 28-cent staircase rather than a flat run, and the repair does not engage on it
    either. Without that, every factory pair involving Bell would have tripped the repair on every chunk and
    SC-002 clause 2's engagement-count-of-zero would have been unsatisfiable.
  - It is **not** inert on an arbitrary caller-supplied state: FR-012 requires only strict increase, so a
    legal state with, say, 10-cent spacing between two partials **will** be rewritten at the endpoints as
    well as mid-journey. Endpoint exactness (SC-002 clause 1) is therefore asserted against the **repaired**
    array, and clause 1's state set is scoped to the factory five, where the 27.32-cent argument applies.
    This is part of the engine's contract, not a leak: FR-046 is what makes FR-074's no-crossing proof a
    `static_assert` instead of a claim about caller data.
  - It engages only where interpolation would otherwise have produced a non-monotone array (sparse or
    strongly stretched state pairs under a bloom stagger), which is exactly the case FR-074 could not
    otherwise satisfy.
  Spec-added: FR-041's interpolation of two monotone arrays under a per-partial-varying `u_i` is not
  monotone in general, so without this pass FR-074's strictly-increasing invariant and SC-006's per-chunk
  assertion are unsatisfiable.
- **FR-047** — **State-change absorption crossfade (spec-added).** Any call that would otherwise change the
  output arrays discontinuously — `setState` on a slot that currently contributes (`k` or `k+1`), or
  `setStateCount` — is absorbed as follows. The engine snapshots its **current post-FR-046, pre-entropy**
  ratio and amplitude arrays as a departure set `D`, adopts the new configuration immediately for the
  purpose of computing the arriving interpolation `N` (FR-041 + FR-046), and outputs
  `amplitude_i = D.a_i·(1 − x) + N.a_i·x` and `ratio_i = exp2( log2(D.r_i)·(1 − x) + log2(N.r_i)·x )`,
  where `x` ramps linearly from 0 to 1 over the pinned window
  **`kStateChangeFadeSec = 2.0f`** seconds and is held at 1 thereafter. A second qualifying call while
  `x < 1` re-snapshots `D` from the *current output* (not from the previous `D`), so the output stays
  continuous through any number of overlapping changes. The ramp advances on `chunkSeconds`, so it is
  sample-rate and chunk-length independent (SC-013).
  Properties: the crossfade is a convex combination of two continuous arrays and is therefore continuous;
  both endpoints are strictly increasing and the geometric interpolation of two strictly increasing arrays
  with a *uniform* `x` is strictly increasing, so FR-046's 24-cent floor survives the fade and FR-074's
  `static_assert` is unaffected. Its contribution to FR-044 is one table row with `T / kStateChangeFadeSec`
  as its factor. `2.0 s` is chosen so the amplitude term (6.67e-4) is the smallest in FR-044's table and the
  cents term (7.58) is under a fifteenth of the travel term — i.e. a state swap is never the binding contributor
  — while staying inside the instrument's own "slow autonomous evolution" character (roadmap line 22).
  Spec-added: FR-042 and SC-001 clause 1 are unsatisfiable without it.

### FR-050 series — per-partial bloom offsets (roadmap line 183)

- **FR-051** — Each partial `i` (1-based `n = i + 1`) has a **completion point** `e_n ∈ (0, 1]` within the
  current segment, and its own completion fraction is `u_i = clamp(u / e_n, 0, 1)` where `u` is the
  segment-local travel fraction from FR-041. The law is
  `e_n = 1 − bloom · kMaxBloomFraction · (1 − ((n − 1) / (kStatePartials − 1)))` with
  `kMaxBloomFraction = 0.6` and `bloom ∈ [0, 1]` set by `void setBloom(float) noexcept`. Consequences,
  each of which is a testable property:
  - at `bloom = 0`, `e_n = 1` for every `n` and `u_i = u` — a simultaneous morph;
  - at `bloom = 1`, `e_1 = 0.4` and `e_64 = 1.0` — partial 1 completes at 40% of the journey, partial 64 at
    100%, and every intermediate partial in index order between them ("low partials arrive before high
    partials");
  - `u_i` is continuous and monotone non-decreasing in `u` for every `i`, and `u_i → 1` for every `i` as
    `u → 1⁻` — so the segment resolves onto state B in the limit and the FR-044 bound is not violated at
    the join. (Trace: roadmap line 183.) Note that `u = 1` is **not reachable** through the public surface: FR-041 defines
    `u = p − floor(p)`, which lies in `[0, 1)`, and at `p = numStates−1` the decomposition yields
    `k = numStates−1`, `u = 0` with `B` clamped to `A`. The join property is therefore stated and tested as
    a limit plus a handoff (SC-003), never as an assertion at `u = 1`.
- **FR-052** — The bloom law applies **within a segment**, and is re-evaluated per segment. Travelling
  backwards (`u` decreasing) uses the same `u_i = clamp(u / e_n, 0, 1)` expression, so high partials lead
  on the way back — the motion is time-reversible and cannot ratchet. Spec-added: without this the
  backwards case is undefined and SC-001's reverse sweep is unspecifiable.

### FR-060 series — travel driver (roadmap line 184)

- **FR-061** — `enum class TravelMode : std::uint8_t { External = 0, Spline }`, selected by
  `void setTravelMode(TravelMode) noexcept`.
  *The rate is a journey fraction, not an absolute position rate.* Both drivers share one slew limiter, and
  its cap is
  `slewCap = rate · (numStates − 1)` units/s, where `rate ∈ [kMinTravelRate, kMaxTravelRate] = [1/600, 1.0]`
  is in **journeys per second** — a full journey from `p = 0` to `p = numStates−1` takes between 1 second
  and 10 minutes *regardless of how many states are loaded*. The worst-case cap used by FR-044 is therefore
  `R = kMaxTravelRate · (kMaxStates − 1) = 3.0` units/s.
  *Why not an absolute units/s cap — this is a correction, and it is load-bearing.* An earlier draft capped
  both drivers at a flat `1.0` units/s. Verified arithmetic against the shipped component: waypoints are
  `rng_.nextFloat() * kWaypointMax` with `kWaypointMax = 0.8f` (`spline_trajectory.h:218-219`, `:121`),
  `du/dt = 1/(interval_ · sampleRate_)` (`updateIncrement`, `:215`), and with `|ds/du| ≈ 2.4` the mapped
  rate is `dp/dt ≈ 0.75 · (numStates − 1) · (2.0 / interval)` units/s. At the component's own
  `kDefaultInterval = 2.0f` (`:123`) that is **1.5 u/s at 3 states and 2.25 u/s at 4** — so a flat 1.0 cap
  saturated for every `numStates ≥ 3` at the default setting, and at `kMinInterval = 0.5f` (`:117`) it
  saturated even at 2 states (3.0 u/s). **While saturated the travel coordinate is exactly a constant-rate
  ramp, i.e. a linear-crossfade coordinate** — which contradicts roadmap lines 181–182 ("not a linear
  crossfade") and nullifies Assumption 3's departure (a). Neither SC touching Spline mode would have caught
  it: SC-002's Spline clause pinned `numStates = 2` (the one configuration just *below* the old limit) and
  SC-010 clause 1 used 4 states but measured only CPU. The journey-fraction cap of `3.0` u/s at
  `numStates = 4` sits above the 2.25 u/s analytic worst case at `kDefaultInterval`, so the limiter is
  inactive there by construction — **and SC-002 clause 4 measures that it is**, so the failure mode has a
  criterion rather than an argument.
  - **External** — `void setTargetPosition(float p) noexcept` clamps to `[0, numStates−1]` and the engine
    ramps the current position toward it at `void setTravelRate(float journeysPerSecond) noexcept`, clamped
    to `[kMinTravelRate, kMaxTravelRate] = [1/600, 1.0]` journeys/s and converted to the shared `slewCap`
    above. This is the DSP half of the "host-synced slow ramp" of roadmap line 184 in
    full: `setTravelRate` is the ingestion point and converting `(bpm, noteValue)` to journeys/s is one
    division on the caller's side. Registering that as a synced parameter is Phase 9's — see Non-Goals,
    which names the owner so the roadmap requirement is not dropped between phases.
  - **Spline** — the raw driver output is `s`, the output of an owned `SplineTrajectory`
    (`spline_trajectory.h:114`), advanced by `processBlock(numSamples)` (`:193`) once per `updateChunk`;
    waypoint spacing via `setWaypointInterval(double)` (`:165`), depth via `setDepth(float)` (`:174`).
    Two corrections to the naive mapping, both required:
    1. **Range rescale.** `s` does **not** cover its nominal `{-1, +1}` source range (`:209-211`) in normal
       operation: waypoints are drawn as `rng_.nextFloat() * kWaypointMax` with `kWaypointMax = 0.8f`
       (verified, `spline_trajectory.h:218-219`, `:121`), so a naive `((s + 1) / 2) · (numStates − 1)` would
       leave the endpoints `p = 0` and `p = numStates−1` effectively unreachable and the instrument would
       never actually arrive at any of its spectral identities — contrary to roadmap line 173. The mapping
       is therefore `p_raw = clamp((s / kWaypointMax + 1) / 2, 0, 1) · (numStates − 1)`. The clamp is not
       decorative: uniform Catmull-Rom overshoots its control points, so `|s|` can exceed `kWaypointMax`.
       Measured by SC-002 clause 3.
    2. **Rate limit.** `p_raw` is the limiter's **target**, not the output: the engine's position is slewed
       toward `p_raw` at the shared `slewCap`. Without a limiter FR-044's bound is derived for a regime that
       does not exist: with alternating `±kWaypointMax` waypoints the Catmull-Rom form at
       `spline_trajectory.h:239-251` has `|ds/du| ≈ 2.4`, and `du/dt = 1/(interval_ · sampleRate_)`
       (`:215`, `:262-266`) at `kMinInterval = 0.5f` (`:117`) gives `|ds/dt| ≈ 4.8/s`; after the rescale and
       the affine map that is ≈ 9.0 units/s at `numStates = 4` — **three times** the journey-fraction cap
       `R = 3.0`, in exactly the mode SC-010 and SC-012 measure. With the limiter, FR-044's single travel
       term covers both drivers and SC-001's `static_assert` is writable; and because the cap now scales
       with `numStates`, the limiter is inactive at the default waypoint interval rather than pinned
       (SC-002 clause 4). The limiter is `max`/`min` on a continuous quantity, so C1 continuity
       from the component's own guarantee (`spline_trajectory.h:35-51`) is preserved as C0-with-bounded-
       slope, which is all FR-044 needs.
    Trace: roadmap line 184.
- **FR-062** — **Travel-mode switching costs nothing extra, because both modes drive the same slew
  limiter.** The engine holds **one** position state. `TravelMode` selects only what feeds that limiter's
  *target* — `setTargetPosition`'s clamped value in External mode, `p_raw` in Spline mode. A mode switch
  therefore changes a target, never the position, and the position remains a slew-limited continuous
  quantity across the switch. **The FR-044 travel row already covers it and no absorption ramp and no new
  FR-044 term is required.**
  *This replaces an earlier draft that was self-contradictory:* it claimed "Spline mode's output is
  offset-free because the position is recomputed continuously from `s`" and then armed "a documented short
  internal ramp to absorb any instantaneous difference" whose duration was documented nowhere and which had
  no FR-044 row — so the per-chunk delta across a switch was unbounded and SC-001 clause 1's assertion
  across a `TravelMode` switch had no derivable threshold. Recomputing the position *from* `s` is exactly
  what makes the switch a step: `p_raw` can differ from the current position by the full
  `numStates − 1 = 3` units. Feeding it to the limiter instead makes the step a bounded slew.
  Spec-added: mode switching is otherwise the one guaranteed click in the component.
- **FR-063** — The travel position is always in `[0, numStates−1]` and is finite for every input, every
  mode, every rate and every chunk length — including chunk lengths far longer than a waypoint interval
  (`SplineTrajectory::advance` rotates as many waypoints as needed, `spline_trajectory.h:262-269`).

### FR-070 series — `EntropyProcessor` (roadmap lines 185–188)

- **FR-071** — One control, `void setEntropy(float) noexcept`, clamped to `[0, 1]`, drives four stage
  weights, each a clamped linear ramp over its own sub-interval (Clarifications C-6). Each `w_k(e)` is
  continuous and monotone non-decreasing in `e`, and `w_k(0) = 0` for all `k`:

  | Stage | Mechanism | Ramp interval | FR |
  |---|---|---|---|
  | 1 | partial amplitude jitter | `e ∈ [0.00, 0.35]` | FR-072a |
  | 2 | phase decoherence | `e ∈ [0.25, 0.60]` | FR-072b |
  | 3 | ratio scatter | `e ∈ [0.50, 0.85]` | FR-072c |
  | 4 | partial death / rebirth | `e ∈ [0.75, 1.00]` | FR-073 |

  The ordering ("in order of increasing entropy", roadmap line 185) is exactly this table: each stage's
  onset is at or after the previous stage's onset, and the intervals overlap so no stage begins with a
  slope discontinuity in the aggregate disorder. Trace: roadmap lines 185–187.
- **FR-072** — Stages 1–3 are per-partial multiplicative perturbations applied to the arrays in place, in
  this fixed order:
  - **(a) Amplitude jitter.** `a_i ← a_i · (1 + w_1 · kMaxAmpJitter · d_i)` where `d_i ∈ [-1, +1]` is a
    per-partial slow OU walk and `kMaxAmpJitter = 0.5f` (`< 1`, so the factor stays strictly positive and
    no partial can be inverted or silenced by jitter alone).
  - **(b) Phase decoherence.** `r_i ← r_i · centsToPitchRatio(w_2 · kMaxDecoherenceCents · c_i)` with
    `kMaxDecoherenceCents = 4.0f`, where `c_i` is a **second, independent** per-partial zero-mean OU walk.
    Being zero-mean and time-varying, this leaves the long-run mean frequency of every partial unchanged
    while its accumulated phase relative to the ideal partial performs an unbounded random walk — which is
    decoherence (Clarifications C-5). Measured by SC-016.
  - **(c) Ratio scatter.** `r_i ← r_i · centsToPitchRatio(w_3 · kMaxScatterCents · s_i)` with
    `kMaxScatterCents = 7.0f`, where `s_i = rng.nextFloat()` (uniform on `[-1, +1]`) is drawn **once per
    seed** and is *static* thereafter — a fixed displacement off the grid, distinct in kind from (b)'s
    wander. Redrawn only by `setSeed`, `reset()` or a rebirth (FR-073).
    The draw of `s_i` is **unconditional** — it does not depend on `w_3` — so the *applied* offset
    `w_3 · kMaxScatterCents · s_i` and the *raw* draw `s_i` are different quantities and FR-008 exposes them
    under different names. Criteria that assert "scatter is still 0" read the applied accessor; criteria
    that count redraws read the raw draw and the redraw counter.

  *The cent constants are pinned here, not by the plan*, because FR-074's no-crossing `static_assert`
  and SC-016's `kMeanRatioDriftCents` derivation are both computed from them:
  `kMaxDecoherenceCents + kMaxScatterCents = 11.0 < kMinRatioSpacingCents / 2 = 12.0` ✔ (FR-046, FR-074).
  The split favours **scatter over decoherence** deliberately: SC-016 must separate a static offset from a
  zero-mean wander over a finite run, which requires the static term to dominate the wander's sampling
  error. The derivation is in SC-016.

  *`centsToPitchRatio` — the conversion helper.* No `Krate::DSP` function performs cents→ratio today: the
  only tree-wide match for `centsToRatio` is a *local variable* at
  `dsp/include/krate/dsp/processors/multi_pitch_detector.h:96`, `pitch_utils.h` offers only
  `semitonesToRatio` (:23) / `ratioToSemitones` (:31), and `HarmonicCloud`'s `detail::centsToDriftRatio`
  (`harmonic_cloud.h:105`) lives in a Layer 3 header and is documented accurate only over `[-50, +50]`
  cents. This phase adds `[[nodiscard]] inline float centsToPitchRatio(float cents) noexcept` to
  `dsp/include/krate/dsp/core/pitch_utils.h` (Layer 0, so both new components may use it), defined as
  `semitonesToRatio(cents / 100.0f)` and therefore accurate over the full `float` range rather than a
  narrow window. The name deliberately differs from `centsToRatio` to avoid shadowing the
  `multi_pitch_detector.h` local. Swept and clear — see the New Components table.

  *OU bank configuration — pinned, not left to implementation.* Only stages (a) and (b) are OU banks;
  (c) is a single draw per seed and stage 4 (FR-073) is Bernoulli draws plus randomized ramp lengths, so
  there are **two** banks of 64 streams, not four. Each stream is configured and advanced exactly as
  `BrownianDrift` does (`brownian_drift.h:230-270`), whose `τ = kTauMin + smoothness·(kTauMax − kTauMin)
  = 0.2 + smoothness·29.8` s (verified, `:231-234`):

  | Bank | Streams | τ | `setSmoothness` value | `setDepth` | Control interval |
  |---|---|---|---|---|---|
  | (a) amplitude jitter | 64 | **3.0 s** | `(3.0 − 0.2)/29.8 = 0.09396f` | 1.0 | `EntropyProcessor::kEntropyControlInterval = 64` |
  | (b) decoherence | 64 | **8.0 s** | `(8.0 − 0.2)/29.8 = 0.26174f` | 1.0 | 64 |

  *The control interval is the processor's own, not `BrownianDrift::kControlRateInterval = 32`
  (`brownian_drift.h:105`) — SC-010's spend ladder, lever 5, SPENT.* The two banks' control steps are this
  component's dominant cost: at 32 samples a 512-sample block runs 16 of them, each drawing three
  `nextFloat()` values on each of 64 lanes (6,144 draws/block); at 64 it runs 8. The change is an **exact
  re-derivation**, not an approximation — `a = exp(−dt/τ)` and `g = kInternalStd·sqrt(1−a²)` are formed
  from the doubled `dt`, so both banks keep their τ and their stationary variance and only the sampling
  grid of the walk moves. It costs nothing musically: at 48 kHz a 64-sample grid is 1.33 ms against τ of
  3 s and 8 s, ~2,250× faster than the faster of the two. **Consequence, recorded because it is not
  obvious:** a lane is no longer step-comparable with a stock `BrownianDrift`, so the equivalence test's
  stream arm was **replaced, never deleted** — it now prepares its reference `BrownianDrift` at **half the
  sample rate** and drives it at *its* 32-sample interval, which is the identical real `dt` (and the
  identical `double`, since `32/24000` and `64/48000` divide the same real value from exactly
  representable operands) — and the explicit-coefficient arm is now stated at `dt = 64/fs`.

  Output smoothing is `BrownianDrift`'s own fixed `kDriftOutputSmoothMs = 150.0f` (`:103`) in both banks.
  These three numbers (τ per bank, depth, control interval) determine SC-004's stationarity window,
  SC-006's per-chunk delta and SC-016's variance slope; leaving them free would make all three
  irreproducible. They are fixed constants in the header, following how Phase 2 pinned
  `kMutationSmoothness = 0.5f` (`harmonic_cloud.h:200`) rather than exposing a setter.

  The processor may own 64 `BrownianDrift` instances per bank or an equivalent lane-batched implementation
  of the same AR(1) recurrence, at the plan's discretion, provided the two are behaviourally equivalent
  (the lane-batched form is what `HarmonicCloud` chose, `harmonic_cloud.h:929-...`, `:1544-1567`).
  Per-partial seeds come from `deriveStreamSeed` (FR-006) with a distinct salt range per bank.
- **FR-073** — **Partial death and rebirth** (roadmap lines 186–187). Each partial owns a lifecycle:
  `Alive → Dying → Dead → Reborn → Alive`, and an amplitude factor `L_i ∈ [0, 1]` multiplying the partial's
  amplitude. While `w_4 > 0`, an alive partial's per-chunk death probability is
  `w_4 · kMaxDeathRatePerSecond · chunkSeconds`, drawn from its own stream, with
  **`kMaxDeathRatePerSecond = 0.05f`** (one death per partial per 20 s at `w_4 = 1`). Transitions are
  **ramps, not steps**: `Dying` falls `L_i` linearly from 1 to 0 over a per-partial randomized window in
  `[kMinDeathFadeSec, kMaxDeathFadeSec] = [0.5, 2.0]` s; `Dead` holds `L_i = 0.0f` for a randomized dwell in
  `[kMinDeadDwellSec, kMaxDeadDwellSec] = [0.2, 1.0]` s; `Reborn` ramps `L_i` from 0 back to 1 over a
  randomized window in `[kMinRebirthFadeSec, kMaxRebirthFadeSec] = [0.5, 2.0]` s. Worst-case full cycle:
  5.0 s.
  The partial's static scatter offset `s_i` (FR-072c) is **redrawn strictly inside the Dead window, at a
  point where `L_i` is exactly `0.0f`** — "re-emerge slightly detuned" without a pitch step (Assumption 5).
  That "exactly 0" is the load-bearing part, and it is why FR-074 and SC-006 scope the per-chunk *ratio*
  delta bound to partials with `L_i > 0`: a redraw is by definition a step of up to `2·kMaxScatterCents`
  in one chunk, and the only thing that makes it inaudible — and the only property worth testing — is that
  it happens while the partial contributes no energy at all.
  **RESTATED (deviation D5) — the `w_4 = 0` clause is scoped to partials that are already `Alive`, and an
  in-flight lifecycle runs to completion.** At `w_4 = 0`: **no new death is started**, and every partial in
  `Alive` has `L_i` set to exactly `1.0f` by an explicit assignment (not an arithmetic consequence — the
  macOS leg builds `-ffast-math`, the same reason `HarmonicCloud` branches its mutation weight at
  `harmonic_cloud.h:1386-1388`). A partial that is in `Dying`, `Dead` or `Reborn` when `w_4` reaches 0
  finishes its cycle on the ramps above, which is bounded by the 5.0 s worst case stated earlier in this
  requirement.

  *Why the scope, and why this is a correction of the requirement's own derivation rather than a
  relaxation.* The unscoped form — "at `w_4 = 0` every partial is `Alive` with `L_i` exactly `1.0f`" — is
  **unsatisfiable together with FR-044**, which is a hard bound in the same document. `setEntropy(0.0f)`
  is legal at any moment, including while a partial sits in the `Dead` window at `L_i = 0`. Forcing that
  partial to `Alive` with `L_i = 1.0f` is a step of **1.0 in one chunk** against
  `kMaxAmpDeltaPerChunk = 0.025` — **40×** the bound — and FR-044 lists exactly one death/rebirth
  contributor, `chunkSeconds / kMinDeathFadeSec`, i.e. a *ramp*. SC-001 clause 1 drives `setEntropy`
  mid-sweep and would fail on the literal form; so would SC-006's per-chunk amplitude-delta clause, which
  bounds `L_i`'s own motion. The two requirements cannot both hold as originally written, and the one that
  gives way is the one whose purpose (a silent, dead-quiet entropy floor) is still fully served: within at
  most 5.0 s of `w_4` reaching 0 the processor is in exactly the state the unscoped clause describes, and
  it gets there without a click. The implementation records this at `entropy_processor.h`'s
  `advanceLifecycles` doc comment, and the bound is the pinned ramp constants, not an implementation
  detail.
  The ramp constants are pinned here because FR-044's death term (`chunkSeconds / kMinDeathFadeSec`) and
  SC-005's completion derivation are both computed from them.
- **FR-074** — **Boundedness at every setting** (roadmap line 188, "bounded and smooth at every setting —
  controlled decay, never Ruinae chaos"). For every entropy value and every seed:
  - each output amplitude is finite, `≥ 0`, and `≤ (1 + kMaxAmpJitter)` times its input amplitude;
  - each output ratio is finite, `> 0`, and within
    `±(kMaxDecoherenceCents + kMaxScatterCents) = ±11.0` cents of its input ratio;
  - the output ratio array remains **strictly increasing**. This is now a two-line proof rather than an
    aspiration: FR-046 guarantees the pre-entropy array has at least `kMinRatioSpacingCents = 24.0` cents
    between neighbours, and each partial moves by at most 11.0 cents, so two neighbours can close at most
    22.0 cents of a 24.0-cent gap. The relation
    `2 · (kMaxDecoherenceCents + kMaxScatterCents) < kMinRatioSpacingCents` is an actual `static_assert`
    in the entropy header. **Any increase to the cent constants must be paid for by raising
    `kMinRatioSpacingCents`**, not by deleting the assert.
  - the per-chunk change in any **amplitude** obeys FR-044's `kMaxAmpDeltaPerChunk`, and the per-chunk
    change in any **ratio** obeys FR-044's `kMaxRatioDeltaCentsPerChunk` **for every partial whose
    death/rebirth factor `L_i` is `> 0`**. The ratio bound is deliberately not asserted while `L_i == 0.0f`:
    FR-073 redraws the static scatter inside that window, which is a step by construction, and the
    inaudibility argument is that the partial is silent — not that the step is small. The test asserts the
    complementary property instead: **every** scatter redraw occurs on a chunk where that partial's `L_i` is
    exactly `0.0f`, and `L_i` itself obeys the amplitude delta bound at every chunk (which is what proves
    death/rebirth ramps rather than steps). So entropy cannot click.
- **FR-075** — `void processChunk(float* ratios, float* amplitudes, std::size_t count, std::size_t
  numSamples) noexcept` applies all four stages in place and advances all internal walks and lifecycles by
  `numSamples`. Null pointers or `count == 0` make it a no-op leaving internal state **unadvanced** (so a
  rejected call cannot silently desynchronize a caller's time base). `count` is clamped to
  `SpectralState::kStatePartials`.

### FR-080 series — `HarmonicCloud` spectral-target injection (spec-added; Clarifications C-1)

This series **amends the Phase 2 component**. Every requirement here is additive and inert until
`setSpectralTarget` is called.

- **FR-081** — `HarmonicCloud` gains
  `void setSpectralTarget(const float* ratios, const float* amplitudes, std::size_t count) noexcept`,
  `void clearSpectralTarget() noexcept` and `[[nodiscard]] bool hasSpectralTarget() const noexcept`.
  `setSpectralTarget` copies the arrays into fixed member storage and marks the existing `freqDirty_` /
  `ampDirty_` flags so the recompute happens at most once at the next control chunk
  (`harmonic_cloud.h:1313-1321`) — never inside the setter. Slots `≥ count` take ratio `i+1` and
  amplitude 0.
  *This rejection list is authoritative for this entry point* (FR-012 defers to it). The call is rejected
  wholesale (FR-007) if and only if: a pointer is null; `count == 0`; `count > kMaxPartials`; any element in
  `[0, count)` is non-finite; any `ratios[i] ≤ 0`; or any `amplitudes[i] < 0`. It **does not** check
  `amplitude ≤ 1`, `ratio ≤ kMaxStateRatio`, or strict ratio monotonicity — the first because FR-074
  permits the engine's own output to reach `(1 + kMaxAmpJitter)` and rejecting it would break the very
  composition this surface exists for, the others because the cloud's downstream stages are well-defined
  for any positive finite ratio and the injected array's ordering is the caller's invariant (FR-046), not
  the cloud's. SC-015 enumerates the malformed arrays the test must feed it.
- **FR-082** — With a target active, `recalculateFrequencies()` computes, per partial `i` (`n = i + 1`),
  **exactly this expression and no other**:

  ```
  warp_i = gravityIsZero ? 1.0f
                         : std::exp2(gravity_ * kGravityExponentRange * detail::kHarmonicCloudLog2N[i]);
  f_n    = fundamentalHz_ * ratio_override[i] * warp_i * stretch(n);   // stretch as shipped, :1087
  ```

  The branch is scoped to the **warp factor alone**. Everything else is unchanged: the epsilon derivation
  and clamp (`:1090-1091`), the write to `frequencyHz_[i]` (`:1089`), and the fact that phase accumulators
  are never touched (`:1062-1063`).
  *Why the wording had to change.* An earlier draft said the injected law "replaces the leading integer `n`
  … and nothing else changes, including the `gravityIsZero` identity branch (`:1082`)". Verified this
  session, that is self-contradictory: `exponent = 1.0f + gravity_ * kGravityExponentRange` (`:1065`)
  carries the leading `1`, so the shipped `ratioG = gravityIsZero ? n : std::exp2(exponent *
  detail::kHarmonicCloudLog2N[i])` (`:1085-1086`) is the **whole `n · n^(g·range)` product**, and its
  `gravityIsZero` arm returns the whole product too. Retaining that branch verbatim in the injected path
  yields `f = f0 · ratio_override[i] · n · stretch` at gravity 0 — `n` counted twice, and the FR-084
  identity destroyed rather than preserved.
  *Bit-exact identity guard, correctly targeted.* When `ratio_override[i]` is exactly
  `static_cast<float>(i + 1)`, the implementation must fall back to the **unmodified parametric `ratioG`
  computation of `:1082-1086`, including its own `gravityIsZero` branch**, and use `f0 · ratioG · stretch`.
  It must **not** fall back to the `std::exp2(exponent · kHarmonicCloudLog2N[i])` arm alone: at gravity 0
  that evaluates `exp2(1.0f · log2N[i])`, which is precisely the rewrite the shipped comment at
  `:1066-1072` warns hands back `31.999998` for `n = 32` under `-ffast-math` — so the guard would break the
  bit-exactness it exists to protect, and SC-014 clause 2's `render_fingerprint.h` tolerances would not
  catch it. One float compare per partial per recompute. Clarifications C-2.
- **FR-083** — With a target active, `recalculateAmplitudes()` computes
  `a_n = amp_override[n] · tiltGain(n)` for `n < N(r)`, replacing `n^(−p(r))` at
  `harmonic_cloud.h:1155-1156` and nothing else: the Richness count law `N(r)` (`:1138-1139`), the FR-043
  tail high-water logic (`:1159-1164`), and the FR-017 normalizer whose input is the `a_i` set and whose
  `setTarget` must remain the last statement of the function (`:1166-1172`) are all unchanged.
  Clarifications C-3.
  *The normalizer's input is the **entropy-perturbed** amplitude set, and that is the decision, not an
  oversight.* `currentNormGainTarget()` (`harmonic_cloud.h:1453-1463`) returns
  `kTargetOscRms / sqrt(Σ a_i² · 0.5)` clamped at `kMaxNormGain`, driven by a smoother configured at
  `kNormGainSmoothMs = 20.0f` (`:176`, `:267`), and FR-073's death/rebirth ramps run 0.5–2.0 s — so the
  smoother tracks them completely and survivors are boosted as partials die. **Entropy is therefore
  level-neutral by construction and the dissolve is purely spectral**, which is exactly what keeps a
  64-partial sum off `kOutputClamp = 2.0f` (`:169`) and is why no second (clean) amplitude array is added
  to the injection surface: splitting the normalizer's input would be a materially wider Phase 2 amendment
  (RA-1) for an effect that belongs elsewhere. The consequence is **measured, not assumed**: SC-004
  metric 4 records absolute rendered RMS across the entropy sweep and bounds its spread, so the
  level-neutrality is a checked-in number rather than a Phase 7 surprise.
  *Where audible level decay belongs:* Phase 7's voice envelope (roadmap lines 283–286). Phase 3 does not
  make Entropy a gain control.
- **FR-084** — **Identity neutrality.** Calling `setSpectralTarget` with `ratios[i] = i + 1` and
  `amplitudes[i] = exp2(−p(r)·log2(i+1))` reproduces the parametric render, and `clearSpectralTarget()`
  returns the cloud to the parametric path with no step in the rendered output (the change goes through the
  same dirty-flag path and the same FR-014 amplitude smoother, so it cannot click). Measured by SC-014.
- **FR-085** — **Cost containment — three levers, and the third is the one that works on a moving target.**
  A target may be re-supplied every control chunk (that is the intended Phase 7 cadence), which makes
  `recalculateFrequencies()` a per-chunk cost rather than a per-setter cost. The implementation must avoid
  re-deriving anything a chunk did not change:
  1. **Whole-array skip.** The recompute is skipped when the supplied arrays are bit-identical to the
     stored ones. Measured by SC-010 clause 3. The comparand is **stable** by FR-008: the engine's output
     accessors return pointers into member storage whose address never changes and whose contents change
     only inside `updateChunk`, so the cloud's stored copy and the supplied arrays are comparable
     chunk-to-chunk without a scratch buffer in between.
  2. **Identity branches.** The `gravityIsZero` (FR-082) and `tiltDb_ == 0` branches stay in place, so the
     common configuration costs one multiply rather than an `exp2` per partial.
  3. **Per-slot dirty test (the lever that operates while the target is changing).** A slot is recomputed
     only if its supplied ratio differs from the stored one by more than
     **`kTargetRatioEpsilonCents = 0.05f`** cents (a per-slot compare in the precomputed log domain,
     i.e. `|log2 r_new − log2 r_old| > kTargetRatioEpsilonCents / 1200`), and likewise its amplitude only if
     it differs by more than **`kTargetAmpEpsilon = 1e-5f`**. `0.05` cents is two orders below Phase 2's own
     `< 0.1` cent frequency-accuracy criterion (roadmap line 163) and four orders below the smallest
     perturbation this phase produces (`kMaxDecoherenceCents = 4.0`), so nothing audible is skipped.
     Levers 1 and 2 are both **inert in SC-010 clause 2's configuration** — a changing morph output is never
     bit-identical to the previous chunk and is never `ratio_override[i] == i + 1` — so without a third
     lever the only two named responses to an over-budget measurement would be dead in exactly the arm that
     is measured. Under a slow travel (`kMinTravelRate` = a 10-minute journey) most slots are static from
     chunk to chunk at this epsilon, so lever 3 is where the real saving lives.
  *The whole-array skip has its own timing clause* (SC-010 clause 3), because SC-010's headline
  configuration re-supplies a **changing** target every chunk — precisely the case in which lever 1 never
  fires, so that measurement would prove nothing about it. Clause 3 measures the unchanged-target path
  separately and requires it to cost no more than 10% over the no-target baseline, which is only achievable
  if the skip exists.
  *If all three levers together still miss the budget*, the response is RA-3's recorded roadmap amendment —
  never a silent overrun and never abandoning the phase. See SC-010 clause 2.
- **FR-086** — **Composition cadence and call order (spec-added; a requirement, not a test convenience).**
  A consumer that drives `HarmonicCloud` from a `SpectralMorphEngine` **must** do so in slices of
  **≤ `HarmonicCloud::kControlChunkSamples` = 64 samples** (`harmonic_cloud.h:139`), and the call order per
  slice is fixed:

  ```cpp
  for (each slice of <= 64 samples) {
      engine.updateChunk(n);                                    // FR-043: advance travel, bloom, entropy
      cloud.setSpectralTarget(engine.getOutputRatios(),         // FR-081 / FR-008: zero-copy, marks dirty
                              engine.getOutputAmplitudes(),
                              engine.getOutputCount());
      cloud.processStereoBlock(left + offset, right + offset, n);  // consumes the dirty flags, renders
  }
  ```

  This order and this bound are **documented in both headers' class-level docs** (`spectral_morph_engine.h`
  and the `setSpectralTarget` doc comment in `harmonic_cloud.h`), not only here.
  *Why a bound rather than a suggestion — verified this session.* `processStereoBlock` restarts its internal
  64-sample control grid on every call (`harmonic_cloud.h:713-716`: `while (done < numSamples) { chunk =
  min(kControlChunkSamples, …); updateControl(chunk); … }`) and `setSpectralTarget` only raises
  `freqDirty_`/`ampDirty_`, which are consumed at the head of the **first** `updateControl` of that call
  (`:1310-1321`, read this session). A target supplied once per 512-sample host block is therefore **frozen
  for all 8 internal chunks**: the morph's effective resolution silently becomes the host block size, and
  FR-044's per-chunk bound — which Assumption 6 deliberately states per control chunk and not per host
  block — would stop describing the shipped path.
  *Consequences that make this testable rather than advisory:* SC-009's render harness and SC-004 metric 3's
  render harness both drive the composition in exactly this shape, so **the tested path is the shipped
  path**; SC-010 clause 2's "a target re-supplied every control chunk" is this cadence and not an invented
  stress case; and FR-085's three cost levers are stated against this call rate.
  *What is explicitly not done:* pushing the chunking inside the cloud (a pull hook or callback so the
  engine advances once per internal control chunk regardless of block length). That would be a structural
  change to a shipped Phase 2 component well beyond the additive injection RA-1 records, and the ≤ 64-sample
  slice loop obtains the same audio behaviour on the consumer's side at the cost of 8 extra call
  boundaries per 512-sample host block — a cost SC-010 clause 2 measures rather than assumes.
  Spec-added: FR-044 and SC-010 clause 2 both presuppose a cadence that was previously nowhere required.

### Roadmap component coverage (completeness check)

| Roadmap Phase 3 statement | Line | Covered by |
|---|---|---|
| `SpectralState` (L2, plain data) — a **named** target spectrum: 64 ratio/amp pairs + tilt/inharmonicity metadata | 178 | FR-001, FR-011 (incl. the `name` field), FR-012, FR-013 |
| Factory states: pure sine stack, bell, choir/formant, glass, breath | 179 | FR-021, FR-022, FR-023; SC-008 |
| Related to `harmonic_snapshot`, source-agnostic — verify no ODR overlap | 180 | Assumption 2; New Components table |
| `SpectralMorphEngine` (L3) — holds 2–4 states | 181 | FR-003, FR-042, FR-047; Assumption 4 |
| Travels along a life-modulated trajectory, not a linear crossfade | 181–182 | FR-041, FR-051, FR-061; Assumption 3; SC-002 clause 4 |
| Per-partial morph time offsets — low before high ("bloom") | 183 | FR-051, FR-052; SC-003 |
| Travel driven by `SplineTrajectory` or host-synced slow ramp | 184 | FR-061 (Spline / External), FR-062, FR-063. The DSP half of host sync ships here (`setTravelRate`); the tempo→journeys/s registration is assigned to Phase 9 by name in Non-Goals so it is not lost. |
| `EntropyProcessor` (L2) — one 0–1 control | 185 | FR-002, FR-071 |
| Stage order: amp jitter → phase decoherence → ratio scatter → death/rebirth | 185–187 | FR-071 table, FR-072, FR-073; SC-005 |
| Low = angelic purity, high = slowly dissolving dream texture | 187–188 | FR-071 (`w_k(0) = 0`), SC-004, SC-006 |
| Bounded and smooth at every setting — controlled decay, never chaos | 188 | FR-074, FR-044; SC-006 |
| Morph continuity — no clicks, max per-block amp delta bounded | 190 | FR-044, FR-047; SC-001; Assumption 6 |
| Entropy monotonicity — flatness / partial-deviation metrics | 190–191 | SC-004 |
| State round-trip serialization | 192 | FR-031, FR-032, FR-033; SC-007 |
| Audible A/B renders for each factory state pair | 192 | SC-009 (via the FR-080 series) |
| Open Question 2 — spectral state authoring, factory-only vs user-morphable/savable (Phase 3 half) | 497 | Clarifications C-9 (DECIDED: assign + serialize; mutators → Phase 9) |

Requirements with **no roadmap line** (spec-added, listed here so scope additions stay visible):
FR-008 (introspection surface — SC-002/003/005/006/016 are unmeasurable without it), FR-014 (state
normalization — required for FR-041 level neutrality and SC-008 comparability), FR-046 (monotone-spacing
repair — without it FR-074's strictly-increasing invariant is unsatisfiable under a bloom stagger),
FR-047 (state-change absorption crossfade — without it FR-042's "must not step the output" and SC-001
clause 1 are unsatisfiable by any faithful implementation),
FR-052 (reverse travel — otherwise undefined), FR-062 (travel-mode switch continuity — otherwise the one
guaranteed click), the Layer 0 promotion of `deriveStreamSeed` and the Layer 0 addition of
`centsToPitchRatio` (both under FR-006 / FR-072 — without them the layer discipline of FR-002/FR-003 and
the requirements that reuse those helpers are mutually unsatisfiable), the
whole **FR-080 series** (cloud injection — required by roadmap line 192's rendered evidence; justified in
Clarifications C-1) **including FR-086** (composition cadence and call order — FR-044 and SC-010 clause 2
both presuppose a cadence nothing previously required; Session 2026-07-26 Q2), SC-002 clause 5
(post-`prepare` defaults — FR-005's default table needs a criterion that fails if a slot is left silent;
Q3), SC-004 metric 4 (absolute rendered RMS — makes FR-083's level-neutrality a recorded number; Q4),
SC-010 (CPU budget — the roadmap states none for Phase 3; see SC-010), SC-011
(RT safety), SC-012 (determinism), SC-013 (sample-rate invariance), SC-014 (Phase 2 neutrality regression
gate) and SC-015 (non-finite hygiene / extremes). Each states the fact in its own trace field.

## Success Criteria

Every criterion states its metric, threshold and measurement. No criterion may be satisfied by a bit-exact
float golden over rendered audio (roadmap line 488); where a render must be pinned, it is pinned with
`tests/test_helpers/render_fingerprint.h` (`RenderFingerprint` at :54 with its fields at :55-59, tolerances
at :49/:52 — `kSampleTolerance = 1.0e-4f` at :49, `kMetricTolerance = 1.0e-5` at :52,
`kRenderCheckpoints = 32` at :46; all four re-verified by `grep -n` this session, correcting an earlier
draft's off-by-one). The one
permitted exact digest is over the FR-031 **serialized byte stream** (SC-007), which is stored values, not
arithmetic.

- **SC-001 (Morph continuity — no clicks, bounded per-chunk delta).** Two clauses, both required.
  1. *Analytic/array clause.* Over a full travel sweep — `p` from 0 to `numStates−1` and back, at
     `kMaxTravelRate`, in 64-sample chunks at 48 kHz, at `bloom ∈ {0, 0.5, 1}` × `entropy ∈ {0, 0.5, 1}` ×
     `TravelMode ∈ {External, Spline}` (18 configurations — **both drivers**, because FR-061's rate limiter
     is what makes one derivation cover both and an unswept Spline arm would leave that untested) — the
     maximum absolute change in any partial's output amplitude between consecutive chunks is
     `≤ kMaxAmpDeltaPerChunk`, and the change in any partial's output ratio is
     `≤ kMaxRatioDeltaCentsPerChunk` cents for every partial with `L_i > 0` (FR-044, FR-074).
     *Calls not exercised, stated rather than left implicit.* The sweep **never calls `setSeed()` or
     `reset()`**. Both are configuration-time calls (FR-005, FR-006) and named exemptions in FR-044's
     continuity list — `setSeed` redraws all 64 static scatter offsets and `reset()` is a rewind, so each is
     a step by construction. Asserting a per-chunk delta bound across them would be asserting something
     false; their contract is covered by SC-012 (determinism) instead. Every other public setter **is**
     exercised mid-sweep, as the paragraphs below require.
     *States loaded, pinned (an earlier draft left this open, so the sweep's worst case was not pinned):*
     the two-state arms use `SineStack` (slot 0) and `Bell` (slot 1) — the largest FR-022 divergence and the
     only sparse state, i.e. the arm that exercises the FR-041 fill; the four-state arms use
     `SineStack, Bell, Glass, Breath` in that order. Additionally, one arm loads the FR-012-legal adversarial
     pair `{numPartials = 2, ratios = {kMinStateRatio, kMaxStateRatio}}` against `SineStack`, which is the
     input that broke the earlier unbounded fill rule (FR-041) and which must now produce finite,
     `≤ kMaxOutputRatio`, strictly increasing output.
     Both constants
     are `static_assert`ed against the **full** analytic worst case — the six-row contributor table in
     FR-044, i.e. `kMaxTravelRate · (kMaxStates − 1)`, `kMaxBloomFraction`, `kOutputCentsSpan` (and through
     it `kMinStateRatio`, `kMaxFillRatio`, `kFillSpacingCents`), the FR-014 normalization,
     `kStateChangeFadeSec`, `kMinDeathFadeSec`,
     `kMaxAmpJitter`, `kMaxDecoherenceCents` and `kDriftOutputSmoothMs` — so the test cannot be satisfied by
     loosening them silently and no contributor can be quietly dropped. Also asserted across a `setState`, a
     `setStateCount` and a `TravelMode` switch performed mid-sweep — which is satisfiable **because** FR-047
     absorbs the first two over `kStateChangeFadeSec` and FR-062 routes the third through the shared slew
     limiter, and both contribute a row (or an existing row) to FR-044. Without those two mechanisms this
     sentence would be unsatisfiable by any faithful implementation: a `SineStack → Bell` swap at
     `u_i = 0.5` moves partial 1's amplitude by ≈ 0.078, four times the earlier `kMaxAmpDeltaPerChunk`, and
     deferring the swap to the next chunk does not shrink it.
  2. *Rendered clause.* **Render pinned:** states `SineStack` (slot 0) and `Bell` (slot 1) — the pair with
     the largest FR-022 spectral divergence, i.e. the hardest case; `numStates = 2`; `f0 = 110 Hz`;
     48 kHz; `bloom = 0.5`; External travel at `kMaxTravelRate`; a 20 s render covering a full out-and-back
     journey; entropy arms `{0, 1}`; seeds `{1, 7, 13, 29}` (4 seeds × 2 entropy arms = 8 renders, each with
     its own frozen-travel control at the journey midpoint, same seed, same entropy).
     *Pass rule (a margin, not a bare inequality):* for **each** of the 8 render/control pairs,
     `detections_moving ≤ 1.15 · detections_frozen + 5`; and, per entropy arm and per channel, the
     **median** of `detections_moving` across the 4 seeds satisfies the *same margined* relation,
     `median_moving ≤ 1.15 · median_frozen + 5`.
     *Why a margin:* the detector has a nonzero false-detection floor on this material (Phase 2 measured
     126 (L) / 141 (R) over 30 s on a click-free build), and the two arms are different stochastic
     realizations, so a bare "no more than" comparison between them is a coin flip at the margin — a
     click-free build can score 130 against a control's 128 purely from OU-walk realization. The `1.15`/`+5`
     figures must be **re-derived from the measured spread across the 4 seeds** during implementation and
     recorded in the test with the observed numbers; if the measured spread does not fit inside them, the
     correct response is to widen the *seed count*, not the margin.
     *Why the median clause carries the margin too — an earlier draft imposed it bare, which was the very
     coin flip the paragraph above rejects.* Under the null hypothesis (a click-free build, two different
     stochastic realizations of the same false-detection process) a bare `median_moving ≤ median_frozen`
     holds with probability ≈ 0.5, and it was required for 2 entropy arms × 2 channels = 4 near-independent
     comparisons, so a **correct** implementation would have failed with probability
     `1 − 0.5⁴ ≈ 94%`. With the same `1.15·x + 5` margin the median clause inherits the per-pair rule's
     false-failure budget rather than dominating it. Four seeds is enough for the *margined* form because
     the margin, not the sample size, is what absorbs realization noise; the seed count exists so the
     `1.15`/`+5` figures can be re-derived from an observed spread rather than assumed. If the observed
     per-seed spread exceeds `±15% + 5` on a click-free build, raise the seed count to 8 (shared with
     SC-006's pinned set) and re-derive — never widen the margin.
     *Detector configuration (pinned):* `ClickDetectorConfig` (`artifact_detection.h:38`) with
     `sampleRate = 48000.0f` (must match the render; the struct default is 44100), `frameSize = 512`,
     `hopSize = 256`, `detectionThreshold = 5.0f`, `energyThresholdDb = -60.0f`, `mergeGap = 5`; detector
     `ClickDetector` (:99), `detect(...)` (:130). *Why a differential and not an absolute zero:*
     `ClickDetector` is a within-frame statistical outlier test
     (`threshold = mean(|dx|) + detectionThreshold·stddev(|dx|)`, :186-193), which on a 64-partial sum
     yields a nonzero false-detection floor — Phase 2 measured 126 (L) / 141 (R) over 30 s on a
     click-free build (`specs/seraphis-phase2-harmonic-cloud/spec.md:721`). **Both arms of this clause are
     aperiodic** (the frozen-travel control still has drift and, at `entropy > 0`, entropy motion), which is
     exactly the regime mismatch that forced Phase 2's SC-005 amendment; the frozen control must therefore
     be frozen in *travel only*, never in drift. Test sketch: `SpectralMorph_TravelIsContinuous`.
     Trace: roadmap line 190.
- **SC-002 (Morph endpoints, monotone progress, Spline coverage, limiter headroom, and post-`prepare`
  defaults).** Five clauses.
  1. *Endpoint exactness, scoped to the entries that actually exist.* **Slots loaded: the five factory
     states only** (all 10 pairs), because clause 2's 27.32-cent inertness argument is what makes the `1e-6`
     tolerance meaningful and that argument is proved only for the factory set (FR-046 rewrites any
     caller-supplied state with sub-24-cent spacing, at the endpoints as well as mid-journey — FR-012
     permits such a state, so an unscoped clause 1 would fail on legal input). With `bloom = 0` and
     `entropy = 0`, at `p = 0`:
     - for every `i < state[0].numPartials`, the output ratio and amplitude equal `state[0]`'s within `1e-6`
       relative;
     - for every `i ≥ state[0].numPartials` up to the active count, the output amplitude is **exactly 0**
       and the output ratio equals the **FR-041 continuation** (not `state[0].ratios[i]`, which FR-012 does
       not constrain, and not `i + 1`).

     Symmetrically at `p = numStates−1` against the last state. Comparison is against the **post-FR-046**
     array, which is the engine's contract (FR-046) and which is provably the same array on the factory set.
     And for a monotone increasing `p` sweep, each partial's interpolation fraction `u_i` (FR-008) is
     monotone non-decreasing. Metric: max relative endpoint error; count of monotonicity violations (must
     be 0).
     *Why the scoping is required, not cosmetic.* FR-041 sets the active count to
     `max(A.numPartials, B.numPartials)` and fills the sparse state's tail with the continuation. With Bell
     (`numPartials = 24`) in a slot against any 64-partial state, slots 24–63 output the continuation while
     `state[0].ratios[24..63]` are whatever `makeFactoryState` wrote there — so an unscoped "every output
     ratio equals `state[0]`'s" cannot pass for any pair involving Bell, and clause 2 immediately below
     sweeps all 10 factory pairs.
  2. FR-046's repair is **inert where it is provably inert**: at `bloom = 0`, over a full sweep across all
     10 factory pairs, the number of chunks on which the repair changed any ratio is **0**. *Why this is a
     proof and not a hope:* with `bloom = 0` every `u_i = u`, so the log-domain output spacing is
     `(1 − u)·Δlog2 r^A_i + u·Δlog2 r^B_i ≥ min(Δ^A_i, Δ^B_i)`, and the tightest adjacent spacing anywhere
     in the factory set is the `1200·log2(64/63) = 27.32` cents of the three `ratio_n = n` states — above
     `kMinRatioSpacingCents = 24.0`. The FR-041 continuation of the sparse Bell state is covered by the same
     argument because the continuation carries its own `kFillSpacingCents = 28.0` floor (also above 24.0);
     without that floor the capped fill would go flat at `kMaxFillRatio` and this clause would be
     unsatisfiable for the four Bell pairs. This is what makes clause 1's `1e-6` tolerance meaningful, and
     it fails loudly if `kMinRatioSpacingCents` is ever raised past 27.32.
     At `bloom = 1` the per-partial `u_i` stagger can compress spacing on strongly divergent pairs, so the
     engagement count is **reported, not gated** — but clause 1's endpoint tolerances must still hold
     (at the segment endpoints the output *is* one of the input arrays, where the repair is provably inert
     by the same 27.32-cent argument).
  3. *Spline coverage.* In Spline mode with `numStates = 2`, **`setTravelRate(kMaxTravelRate)`** (= 1.0
     journeys/s), over a **≥ 1200 s** advance at
     `setWaypointInterval(2.0)` and `setDepth(1.0)`, across the 8 SC-006 seeds: the empirical travel
     position reaches within `0.02` of **both** `0` and `numStates−1` at least once per seed, and stays
     inside `[0, numStates−1]` at all times. This is what the FR-061 range rescale exists for — without it
     the position would be confined to roughly the middle 80% and the instrument would never arrive at a
     spectral identity in Spline mode.
     *The travel-rate call is stated because the run-length derivation below is derived against it.* The
     derivation assumes the position **attains** each qualifying waypoint value, which holds only while the
     FR-061 slew limiter is inactive; at the FR-005 default of `kMinTravelRate` (1/600 journeys/s) the
     limiter would pin the coordinate and the clause would fail for reasons unrelated to the range rescale
     it exists to measure. Configuring the rate here does **not** change the FR-005 default — this criterion
     pins the settings it measures at, exactly as every other criterion in this document does.
     *Run-length derivation (the earlier draft's 300 s had none and would have failed ~55% of the time).*
     Uniform Catmull-Rom interpolates its control points, so `s` attains each waypoint value exactly.
     Waypoints are `rng_.nextFloat() * kWaypointMax` with `kWaypointMax = 0.8f`
     (`spline_trajectory.h:218-219`, `:121`) and `nextFloat()` is uniform on `[-1, 1]` (`random.h:58`).
     With `numStates = 2`, `p ≤ 0.02` requires `s ≤ −0.768`, i.e. `nextFloat() ≤ −0.96`: probability
     **0.02 per waypoint**. At `setWaypointInterval(2.0)` a 300 s run draws 150 waypoints, so
     `P(no qualifying waypoint) = 0.98^150 ≈ 4.8%` per endpoint per seed, and requiring both endpoints on
     each of 8 seeds gives a false-failure probability of roughly `1 − (1 − 2·0.048)^8 ≈ 55%`. At 1200 s the
     draw count is 600 and `P(miss) = 0.98^600 ≈ 5.4e-6` per endpoint per seed, so the whole clause's
     false-failure probability is `≈ 1 − (1 − 2·5.4e-6)^8 ≈ 8.6e-5`. Catmull-Rom overshoot can only help and
     is **not** relied on in this derivation. If the run length is ever shortened, this arithmetic must be
     redone — the tolerance may not be widened instead, because `0.02` is what makes "arrives at a spectral
     identity" mean anything.
  4. *Limiter headroom — the criterion that stops the life-modulated trajectory degenerating into a ramp.*
     In Spline mode with `numStates = 4`, **`setTravelRate(kMaxTravelRate)`** (= 1.0 journeys/s),
     `setWaypointInterval(SplineTrajectory::kDefaultInterval)` (= 2.0 s,
     `spline_trajectory.h:123`), `setDepth(1.0)`, over ≥ 300 s per seed across the 8 SC-006 seeds: the
     fraction of control chunks on which the FR-061 slew limiter was **active** (i.e. `|Δp|` was clipped to
     `slewCap · chunkSeconds`) is **< 0.01**, and the test reports the measured fraction.
     *Derivation, and why the travel-rate call is stated:* `slewCap = rate · (numStates − 1)` (FR-061), so
     the `3.0` units/s figure below **presupposes** `rate = kMaxTravelRate`; at the FR-005 default of
     `kMinTravelRate` the cap would be `0.005` units/s and the limiter would be active on essentially every
     chunk. The clause therefore configures the rate it measures at, which does **not** change the FR-005
     default. The analytic worst-case mapped rate at these settings is
     `0.75 · (numStates − 1) · (2.0 / interval) = 0.75 · 3 · 1.0 = 2.25` units/s against
     `slewCap = kMaxTravelRate · (numStates − 1) = 3.0` units/s, so the limiter is inactive by construction
     and the honest expectation is exactly 0; `0.01` is float/estimation slack, not a design allowance.
     *Why this clause exists:* under the earlier flat `kMaxTravelRate = 1.0` units/s cap this fraction would
     have been ≈ 1.0 — the coordinate pinned at a constant rate, i.e. a linear-crossfade coordinate,
     contradicting roadmap lines 181–182 and nullifying Assumption 3's departure (a). No existing clause
     detected it: clause 3 pins `numStates = 2` (just below the old limit) and SC-010 clause 1 uses 4 states
     but measures only CPU. Metric: `activeChunks / totalChunks` from the FR-008 travel introspection.
  5. *Post-`prepare` defaults are asserted on the output arrays, not on the getters.* On a
     default-constructed engine, after `prepare(48000.0)` with **no** parameter call whatsoever, and again
     after **one** `updateChunk(64)` (both points are asserted — FR-005 requires the arrays to be populated
     with no advance, so a first-chunk-populates implementation must fail here), the FR-008 output arrays
     must satisfy, directly:
     `getOutputCount() == 64`; `getOutputRatios()[i] == static_cast<float>(i + 1)` within `1e-6` relative
     for every `i` (the SineStack law, FR-022); `getOutputAmplitudes()[i]` equal to
     `makeFactoryState(SineStack).amplitudes[i]` within `1e-6` relative — i.e. **non-zero for every
     partial**, so a silent default is a failure and not a silent mute; and the travel position exactly
     `0.0f`, `bloom` exactly `0.0f`, `entropy` exactly `0.0f`, `TravelMode::External`, travel rate
     `kMinTravelRate` through the FR-008 introspection reads. Additionally, advancing 200 further
     `updateChunk(64)` calls with no parameter call leaves **every** output element bitwise unchanged —
     the FR-005 all-slots-identical default is a provable no-op morph, which is the Edge Cases
     "both slots holding the same state ⇒ perfectly static output" property exercised as the default path.
     *Why this is a criterion and not a comment:* FR-011 makes a default-constructed `SpectralState` silent
     (`numPartials = 0`) and FR-041 sets the active count to `max(A.numPartials, B.numPartials)`, so
     without FR-005's default table a defaults-untouched engine emits an empty spectrum and a Phase 7 voice
     that misses one `setState` is inaudible with nothing failing. Trace: FR-005; Session 2026-07-26 Q3.
  Test sketch: `SpectralMorph_EndpointsAreExact`; clause 5 as `SpectralMorph_DefaultsAreAudible`.
  Trace: roadmap lines 173, 181, 181–182.
- **SC-003 (Bloom ordering — low partials arrive first).** At `bloom = 1`, sampled at `u = 0.5`: the
  completion fraction `u_1` of partial 1 is `1.0` (it completed at `e_1 = 0.4`), `u_64 < 1.0`, and the
  sequence `u_1 … u_64` is **non-increasing in partial index** with at least `kMinBloomSpread = 0.3`
  separating `u_1` from `u_64`. At `bloom = 0`, all 64 completion fractions are equal to `u` within `1e-7`.
  Additionally: at every `bloom` and every reachable `u ∈ [0, 1)`, no `u_i` exceeds 1 or falls below 0.
  *Segment-join clause, restated to be reachable.* `u = 1` is unreachable through the public surface
  (FR-041 defines `u = p − floor(p) ∈ [0, 1)`), so the join is asserted as a limit plus a handoff:
  at `u = 0.99999` every `u_i ≥ 1 − 1e-5`; and stepping `p` across a segment boundary
  (`p = k + 0.99999 → p = k + 1`) produces an output equal to `state[k+1]` within `1e-6` relative on both
  arrays, with the FR-044 per-chunk delta bound satisfied across the crossing. That handoff — not an
  assertion at an unreachable coordinate — is the property FR-044 needs at the join.
  Metric: per-partial completion fractions from FR-008. Test sketch:
  *The handoff clause is scoped exactly as SC-002 clause 1 is:* "equal to `state[k+1]`" means, for every
  `i < state[k+1].numPartials`, both arrays within `1e-6` relative; and for `i ≥ state[k+1].numPartials`,
  amplitude exactly 0 and ratio equal to the FR-041 continuation. States loaded are the factory five, for
  the same FR-046-inertness reason.
  Metric: per-partial completion fractions from FR-008. Test sketch:
  `SpectralMorph_BloomStaggersLowToHigh`. Trace: roadmap line 183.
- **SC-004 (Entropy monotonicity).** Across **≥ 11 entropy settings** spanning `[0, 1]` (including 0, 1 and
  every FR-071 interval endpoint: 0.25, 0.35, 0.50, 0.60, 0.75, 0.85), **three** disorder metrics plus a
  fourth, non-disorder **level-neutrality** measurement (metric 4, Q4).
  *Why the metrics are split by domain.* The FR-071 stages do not all act in the same domain: stages 1 and
  4 are purely **amplitude**-domain and stages 2 and 3 are purely **ratio**-domain. A single ratio-cents
  metric therefore cannot be strictly increasing over stage 1's interval `[0.00, 0.35]` — SC-005 asserts
  that ratio deviation is *bitwise zero* below `e = 0.25` — nor over stage 4's interval `[0.75, 1.00]`,
  where FR-073 only redraws `s_i` from the same distribution and adds no systematic ratio deviation. Each
  metric is therefore held to a strict-increase rule **only over the intervals of the stages that drive
  it**, and to non-decrease elsewhere.

  1. *Ratio-disorder metric (cents).* Mean over partials of `|1200·log2(r_i / r_i^clean)|`, computed on the
     FR-008 arrays against the pre-entropy arrays. **Strictly increasing** across stage 2's interval
     `[0.25, 0.60]` and stage 3's interval `[0.50, 0.85]`, by at least **5× the measurement's own standard
     error** (computed and reported by the test, not guessed). **Non-decreasing** across the whole `[0, 1]`
     sweep, with a tolerance of 1× the standard error so an interval that is flat by construction cannot
     fail on sampling noise.
  2. *Amplitude-disorder metric (relative).* Mean over partials with `a_i^clean > kAmpMetricFloor = 1e-4`
     of `|a_i − a_i^clean| / a_i^clean`, on the same arrays. A linear-relative form is used rather than dB
     because stage 4 drives amplitudes to exactly 0, where a log metric diverges; this form is bounded
     (a dead partial contributes exactly 1.0). **Strictly increasing** across stage 1's interval
     `[0.00, 0.35]` and stage 4's interval `[0.75, 1.00]`, by the same 5×-standard-error rule;
     non-decreasing across the whole sweep at the 1×-standard-error tolerance. Stage 1 saturates at
     `e = 0.35` and stages 2–3 do not touch amplitudes, so over `[0.75, 1.00]` this metric moves **only**
     because of death/rebirth — which is the point.
     *Sanity anchors, derived rather than asserted.* The metric is exactly 0 at `e = 0` (all stage weights
     are 0, FR-071). At `e = 0.35`, `w_1 = 1` and FR-072a gives `a_i = a_i^clean·(1 + 0.5·d_i)`, so the
     metric is exactly `mean_i |kMaxAmpJitter · d_i|`. The OU walk's stationary std is
     `BrownianDrift::kInternalStd = 0.5f` (`brownian_drift.h:101`) with approximately Gaussian Irwin-Hall
     increments, so `E|d| = kInternalStd · sqrt(2/π) = 0.5 · 0.79788 = 0.39894` and the stationary metric
     value is `kMaxAmpJitter · kInternalStd · sqrt(2/π) = 0.5 · 0.5 · 0.79788 = **0.1995**`, which the
     150 ms output smoother (`kDriftOutputSmoothMs`, `:103`) can only reduce. The anchor is therefore
     `metric(0.35) ∈ [0.15, 0.21]`, with the derivation recorded beside it in the test so a later change to
     `kMaxAmpJitter`, `kInternalStd` or the bank depth is caught.
     *An earlier draft asserted `≥ 0.2`, i.e. a bar at or above the design's own expected value* — a
     faithful implementation converges to 0.1995 and fails it, and SC-004's averaging window (8 seeds ×
     10 τ) is long enough that it converges rather than fluctuating above the bar.
  3. *Spectral flatness (rendered).* **The signal is pinned as tightly as the measurement, because flatness
     is a function of the spectrum being rendered and an earlier draft pinned only the cloud macros — which
     left `numStates`, the loaded states, the travel mode and the travel position free, so the metric's
     value and therefore the 1.25 threshold were undefined and two conforming implementations could differ
     by more than the threshold.** Pinned signal: `numStates = 2` with **`SineStack` in both slots**,
     `TravelMode::External`, travel **frozen at `p = 0`** (`setTargetPosition(0)` before the first
     `updateChunk`, never moved), `bloom = 0`, seed set = the 8 SC-006 seeds averaged. With both slots equal
     and travel frozen, the pre-entropy arrays are constant for the whole render, so **entropy is the only
     thing that varies between sweep points** and the flatness rise is attributable to the entropy stages
     alone. Measurement pinned **in the test**, not delegated to a helper: render
     a `HarmonicCloud` driven through the FR-080 surface at `f0 = 110 Hz`, 48 kHz, using the SC-009 pinned
     cloud configuration, ≥ 10 s per setting, **driven in the FR-086 shape** — ≤ 64-sample slices,
     `updateChunk` → `setSpectralTarget` → `processStereoBlock` per slice — so the measured path is the
     shipped path and not a coarser one; take ≥ 6 non-overlapping 65536-point Blackman-Harris windows
     via `tests/test_helpers/spectral_analysis.h`; average the magnitude spectra bin-wise; then compute
     `flatness = exp(mean_k log m_k) / mean_k m_k` over bins `[2, 16384)` (skipping DC and the first bin,
     and stopping at 12 kHz where partial 64 of the densest state lands) directly in the test.
     *`calculateSpectralFlatness` is deliberately NOT used.* Verified this session, its real signature is
     `calculateSpectralFlatness(const float* signal, size_t n, float sampleRate)` — three parameters, a
     **time-domain** input, its own **Hann** window (`Window::generateHann`), and an internally-chosen FFT
     size hard-capped at 4096 (`while (fftSize * 2 <= n && fftSize < 4096)`), `signal_metrics.h:326-399`.
     Every element of the measurement above conflicts with it, and at 4096 points / 48 kHz the 11.7 Hz bin
     spacing cannot resolve the few-Hz detunes stages 2–3 produce on low partials, so the helper would be
     blind to most of what this metric exists to see.
     *Threshold, fixed a priori:* `flatness(0.75) ≥ kFlatnessRiseRatio · flatness(0)` with
     **`kFlatnessRiseRatio = 1.25`**, chosen from the design rather than from the first run: stages 1–3 are
     all saturated at `e = 0.75`, every partial peak is broadened by up to
     `kMaxDecoherenceCents + kMaxScatterCents = 11` cents and amplitude-jittered by up to ±50%, and a rise
     below 25% would mean that disorder is not spectrally visible at all — which is the failure this metric
     exists to catch. If the first measurement cannot meet 1.25, the finding is that the FR-072 cent
     constants are too small; the response is to raise them inside FR-074's 12-cent budget, **never** to
     lower the ratio.
     *Enforcement interval, and the sign of stage 4.* The rise clause is enforced over `[0, 0.75]` only.
     Over `[0.75, 1.00]` death/rebirth **removes** partial energy, which drives the geometric mean of the
     magnitude spectrum toward the noise floor and is expected to move flatness **downward**, not upward;
     no monotone claim is made there. The only requirement over that interval is
     `flatness(1) ≥ flatness(0)` — i.e. stage 4 must not undo the spectral disorder stages 1–3 created.
     Stage 4's monotone evidence is metric 2, which is why three *disorder* metrics exist rather than two.
  4. *Absolute rendered RMS (level neutrality of Entropy) — a recorded number, not an assumption.*
     On **exactly** the metric-3 renders (same pinned signal, same cloud configuration, same FR-086 drive
     shape, same 8 seeds, same ≥ 10 s per setting), compute the broadband RMS of the rendered stereo output
     in dBFS at each of the ≥ 11 entropy settings, discarding the first 0.5 s of each render so the FR-017
     normalization smoother (`kNormGainSmoothMs = 20.0f`, `harmonic_cloud.h:176`, `:267`) is settled.
     **Every measured value is reported by the test**, and the gate is
     `max_e RMS_dB(e) − min_e RMS_dB(e) ≤ kEntropyLevelSpreadDb = 3.0` dB.
     *Why this clause exists (Session 2026-07-26 Q4):* FR-083 deliberately feeds the cloud's FR-017
     normalizer the **entropy-perturbed** amplitude set, which is what keeps a 64-partial sum off
     `kOutputClamp = 2.0f` (`:169`). The consequence is that Entropy is level-neutral — "slowly dissolving
     dream texture" (roadmap line 187) renders as spectral reweighting rather than as thinning — and **no
     other criterion in this document can see it**: metric 3's flatness is scale-invariant and SC-009's
     endpoint comparison normalizes each partial to partial 1. This clause makes the consequence a
     checked-in measurement so Phase 7 inherits a number, not a surprise.
     *Why 3.0 dB, derived rather than picked:* with the normalizer active the rendered level is pinned to
     `kTargetOscRms` up to (i) the `kMaxNormGain` clamp and (ii) the residual the 20 ms smoother cannot
     track. FR-073's ramps are 0.5–2.0 s and the FR-072a jitter walks at `τ = 3.0` s, both far slower than
     20 ms, so the smoother tracks them essentially completely and the honest expectation is a spread well
     under 1 dB; 3.0 dB is headroom for the clamp binding at `entropy = 1`, where stage 4 can leave few
     partials alive. **A measured spread above 3.0 dB is a finding about the composition, not a threshold
     to widen** — it would mean either the normalizer is clamping harder than expected or entropy is
     changing level materially, and both belong in the record before Phase 7 builds a voice envelope on top.

  *Averaging window for metrics 1 and 2 (stationarity).* Both are averaged over **≥ 8 pinned seeds × ≥ 10 τ
  each**, where `τ = 8.0 s` is the slowest configured entropy walk (FR-072 bank (b)) — i.e. ≥ 80 s of
  control chunks per seed, ≥ 640 s of walk time in total, with the first `2 τ` of each run discarded as
  burn-in. The previously-drafted "≥ 200 control chunks" is 200 × 64 / 48000 = **0.267 s**, a small fraction
  of one time constant: `BrownianDrift::initState()` sets `x_ = mean_` (`:242-251`) and the output is
  additionally one-pole smoothed at 150 ms (`:103`), so such an average measures the seeded initial
  transient rather than the stationary distribution, and the monotonicity result would be a function of the
  seed. The test reports the standard error of each averaged metric so the 5× rule is checkable.
  *Why three disorder metrics:* flatness alone saturates on a dense 64-partial spectrum and is known to
  mislead on wash-like material; the array metrics are direct but do not prove the disorder is *audible*.
  All three are required. Metric 4 is not a disorder metric at all — it is the recorded consequence of
  FR-083's normalizer decision, and it is gated rather than merely reported so a regression in the
  composition cannot pass unnoticed. Test sketch:
  `EntropyProcessor_DisorderIncreasesMonotonically`; metric 4 as `EntropyProcessor_IsLevelNeutral`.
  Trace: roadmap lines 190–191 ("entropy monotonicity tests (spectral flatness / partial-deviation metrics
  increase monotonically with the control)").
- **SC-005 (Entropy stage ordering).** The four stages engage in the FR-071 table's order and not before.
  Seeds are pinned to the same 8 used by SC-006; every clause must hold for each.
  At `entropy = 0.10` (inside stage 1 only): every ratio deviation is **exactly 0** (bitwise), the
  death/rebirth lifecycle of every partial is `Alive` with factor exactly `1.0f`, and the amplitude jitter
  factor of at least one partial differs from 1.0. At `entropy = 0.40`: ratio deviation is nonzero, but
  `getAppliedScatterCents(i)` (FR-008) is still exactly `0.0f` for every `i` and no partial has died.
  At `entropy = 0.65`: `getAppliedScatterCents(i)` is nonzero for at least one `i` and no partial has died.
  At `entropy = 0.90`
  over a ≥ 60 s advance: at least one partial has completed a full `Dying → Dead → Reborn` cycle and its
  `getRawScatterDraw(i)` after rebirth differs from its value before.
  *Every clause here names the accessor it reads, deliberately.* FR-072c draws `s_i` **unconditionally**,
  independently of `w_3`, so "the static scatter component is 0 at `e = 0.40`" is false under a raw-draw
  reading and true under an applied-offset reading. FR-008 exposes both under separate names; this criterion
  is stated against `getAppliedScatterCents` for the zero clauses (where `w_3 = 0` is the reason) and
  against `getRawScatterDraw` for the redraw clause (where the draw, not its scaling, is the observable).
  *Derivation for the 60 s window* (so the last clause is an argument, not a hope): at `e = 0.90`,
  `w_4 = (0.90 − 0.75)/0.25 = 0.60`, so each partial's death rate is
  `0.60 · kMaxDeathRatePerSecond = 0.03/s` (FR-073). A full cycle takes at most
  `kMaxDeathFadeSec + kMaxDeadDwellSec + kMaxRebirthFadeSec = 5.0 s`, so only deaths beginning before
  `t = 55 s` are guaranteed to complete: expected deaths per partial in that window `= 0.03 · 55 = 1.65`,
  `P(a given partial never dies) = exp(−1.65) = 0.192`, and across 64 independent partials
  `P(none dies) = 0.192^64 ≈ 10^−46`. The clause is safe by ~46 orders of magnitude at the pinned rate; if
  `kMaxDeathRatePerSecond` is ever lowered, this arithmetic must be redone.
  Metric: FR-008 introspection reads. Test sketch: `EntropyProcessor_StagesEngageInOrder`.
  Trace: roadmap lines 185–187.
- **SC-006 (Entropy boundedness and smoothness at every setting).** Over a grid of **11 entropy settings ×
  8 seeds** (seeds pinned in the test and reused by SC-004, SC-005 and SC-016), each advanced ≥ 60 s of
  control chunks: every output amplitude is finite, `≥ 0` and `≤ (1 + kMaxAmpJitter)` × its input; every
  output ratio is finite, `> 0`, within `±11.0` cents of its input, and the array is **strictly
  increasing** at every chunk.
  *Per-chunk delta clauses, scoped as FR-074 requires:*
  - the **amplitude** delta bound of SC-001 clause 1 holds for every partial at every chunk, and the
    death/rebirth factor `L_i` itself obeys the same bound — this is what proves death/rebirth ramps rather
    than steps;
  - the **ratio** (cents) delta bound holds for every partial whose `L_i > 0`. It is **not** asserted while
    `L_i == 0.0f`, because FR-073 redraws the static scatter there and that redraw is a step by
    construction (up to `2 · kMaxScatterCents = 14` cents in one chunk). A bare "ratio never steps" clause
    and FR-073 cannot both hold.
  - the complementary property is asserted instead, and is the one that carries the inaudibility argument:
    **every** scatter redraw observed over the grid — counted through FR-008's
    `getScatterRedrawCount(i)`, which is independent of `w_3` — occurred on a chunk where that partial's
    `L_i` was exactly `0.0f` (bitwise), strictly inside the Dead window.
  Non-finite detection uses bit-pattern tests, never `std::isnan`.
  Test sketch: `EntropyProcessor_BoundedAtEverySetting`. Trace: roadmap line 188; FR-073, FR-074.
- **SC-007 (State round-trip serialization).** For all five factory states plus ≥ 3 procedurally-built
  edge states (`numPartials = 0`, `numPartials = 1`, `numPartials = 64` with extremal metadata):
  `serializeSpectralState` returns exactly `kSpectralStateBytes`; `deserializeSpectralState` returns true
  and reproduces every field **bitwise** (`==` on the float bits — legitimate here, FR-033).

  **RESTATED (deviation D18) — the checked-in FNV golden per factory state is scoped to the
  arithmetic-free bytes of the stream.** Three digest clauses, all over **stored values**, all labelled as
  such in the test so a future reader does not mistake any of them for a render golden:
  1. *Format pin.* One whole-stream digest over an **arithmetic-free probe state** whose every field is an
     exactly representable binary32 dyadic rational. This pins field order, offsets, widths, padding and
     endianness — the whole of FR-031.
  2. *Per-factory pin.* For **each of the five factory states**, a checked-in digest over the two
     arithmetic-free regions of its stream in stream order — `[0, 13)` (version, `numPartials`,
     `tiltDbPerOct`, `inharmonicity`) and `[525, 541)` (`name`), 29 bytes. This pins the partial count, the
     label, the format version, the fact that a factory state writes neither tilt nor inharmonicity, and
     the position of the name field. The five digests must also differ from one another.
  3. *Array bytes.* The remaining 512 bytes (ratios and amplitudes) are pinned by **reproducibility and
     mutual distinctness** — the same state serializes identically every time, and no two factory streams
     are equal — with their **values** pinned portably by SC-008 (ten pairwise spectral distances within
     2%, plus per-state max-ratio and ratio-sum pins).

  *Why the scope, and why it is a correction rather than a relaxation.* A whole-stream digest per factory
  state is **unsatisfiable on two of the three CI legs**, and not because of an implementation choice. The
  factory amplitudes come out of `std::pow` and `std::exp` and are then divided by
  `sqrt(sum-of-squares)` (FR-014). Those transcendentals are not correctly rounded and differ in their last
  bits between MSVC's CRT, glibc and Apple's libm; the macOS leg additionally builds `-ffast-math`, under
  which the reduction may be reassociated and `1/sqrt` lowered to `rsqrt` + Newton. One ULP anywhere in
  those 512 bytes changes the digest completely. A hard-coded per-factory whole-stream digest would
  therefore be **a bit-exact float golden wearing a byte-stream costume** — green on Windows and
  structurally incapable of passing the other two legs. That is precisely the failure `dsp/CLAUDE.md`
  ("Never pin a render with a bit-exact digest over float samples") and `tools/lint-float-bit-goldens.js`
  exist to prevent, and its carve-out for serialized byte streams is written for streams of **stored**
  values — which clauses 1 and 2 are and clause 3's 512 bytes are not. The replacement is strictly more
  than the original had a portable right to: every byte that can carry an exact golden now does, and the
  ones that cannot say so in the test with the derivation beside them.

  Negative cases: a capacity one byte short returns 0 and writes nothing;
  a corrupted version byte returns false and leaves `out` untouched; a payload violating FR-012 (e.g.
  non-monotone ratios) returns false. Test sketch: `SpectralState_SerializationRoundTrips`.
  Trace: roadmap line 192.
- **SC-008 (Factory-state distinctness, validity, and the metadata negatives).** Four clauses.
  1. *Validity.* **RESTATED (deviation D6) — the max-ratio clause is scoped to the authored slots.** Each
     of the five factory states satisfies FR-012 and is L2-normalized to within `1e-5`. Explicitly
     asserted, not assumed:
     - **`max_{i < numPartials} ratios[i] ≤ kMaxStateRatio`** — scoped to the **authored** partials. This is
       the arithmetic that failed at Bell's originally-drafted `B = 0.06` (see FR-022), and the scoped form
       still catches it, because Bell's `ratio_24` is an authored slot inside `numPartials`.
     - separately, over **all 64 slots**: `ratios` strictly increasing, and `ratios[i] ≤ kMaxOutputRatio`.
     - `name` non-empty and NUL-terminated.

     *Why the scope (D6).* The unscoped form — "`max_i ratios[i] ≤ kMaxStateRatio` for all five" — is
     **unsatisfiable by a faithful implementation**, and this is a correction of the criterion's own
     derivation, not a relaxation. FR-022 requires `makeFactoryState` to fill `i ≥ numPartials` with the
     FR-041 geometric continuation (it must, or SC-008 clause 2's ratio term evaluates `log2(0/n) = −∞` on
     every Bell pair). That continuation's `kFillSpacingCents` floor deliberately climbs past
     `kMaxStateRatio = 128` — which is exactly why FR-041 defines a *separate*, larger
     `kMaxOutputRatio = 360.37` to bound it. Bell's filled array reaches **240.32**. Asserting
     `≤ kMaxStateRatio` over all 64 slots would therefore assert that the FR-022 fill does not happen. The
     replacement clause is strictly **more** demanding overall: it keeps the authored-slot bound at 128 and
     adds two all-64 assertions (strict increase, and `≤ kMaxOutputRatio`) that the original had only one
     of. Measured: `spectral_state_test.cpp:498` pins `kMaxOutputRatio = 360.37f` for the all-64 arm.
  2. *Distinctness, against an a-priori threshold.* For all **10 pairs**, the spectral distance

     ```
     d(A,B) = sqrt( Σ_{i ∈ [0, kStatePartials)}      (a_i − b_i)² )
            + λ · mean_{i ∈ [0, min(A.numPartials, B.numPartials))} |1200·log2(rA_i / rB_i)| / 1200
     ```

     with **`λ = 1.0`** exceeds **`kMinFactoryStateDistance = 0.4`**.
     *The two summation ranges are different, and stating them is not pedantry — without them this metric
     does not execute.* The **amplitude** term runs over all `kStatePartials` slots, which is well defined
     because unused amplitudes are exactly 0 (FR-011, FR-022). The **ratio** term runs only over the slots
     both states really populate. An earlier draft stated no range at all; over the full 64 slots the four
     Bell pairs (`Bell.numPartials = 24`) would evaluate `log2(rBell_i / rOther_i)` on entries FR-012
     explicitly does not constrain — and if those were left at 0, `log2(0 / n) = −∞` makes `d` Inf/NaN for
     every Bell pair and the criterion cannot run at all. FR-022 additionally requires `makeFactoryState` to
     fill those slots with the FR-041 continuation, so the arrays are finite either way; the scoped range is
     what makes the *comparison* meaningful, since a continuation ratio is an extrapolation rather than an
     authored partial.
     *Derivation (the threshold is fixed before the measurement, not from it).* All five states are
     L2-normalized, so for the three sharing `ratio_n = n` the ratio term is exactly 0 and the amplitude
     term reduces to `sqrt(2·(1 − ρ))` with `ρ` the cosine similarity. `d ≥ 0.4` is therefore exactly
     `ρ ≤ 0.92` — a state pair correlating above 0.92 is a near-copy, and one correlating at or below 0.92
     is audibly a different timbre. This is a **design constraint on FR-022's constants**, which is why it
     is repeated there — and it is stated there, correctly, as covering only the three `ratio_n = n` states
     (SineStack, Choir, Breath), which are the pairs whose ratio term is exactly 0. Bell's and Glass's
     ratio terms are large and nonzero, so those pairs clear 0.4 on the ratio term alone. The previously-
     drafted "recorded from the first measurement of the closest pair"
     formulation could not fail: any five states, however similar, produce a threshold they clear by
     construction.
     The measured closest-pair distance is additionally checked in and asserted as a **separately labelled
     regression pin** (`kMeasuredClosestPairDistance`, with a ±10% band) so a later change to FR-022 that
     silently converges two states is caught even though it still clears 0.4.
  3. *FR-013 negative — metadata never reaches the audio path.* Load a factory state into a
     `SpectralMorphEngine`, render through the SC-009 pinned cloud configuration, then repeat with the same
     state's `tiltDbPerOct` set to `+12`, `inharmonicity` set to `0.1`, and `name` overwritten. The two
     renders must be **bitwise identical**. Without this clause FR-013 has no criterion that would fail if
     it were violated (SC-007 round-trips the fields whether or not they are also applied).
  4. *FR-023 negative — `makeFactoryState` consumes no RNG.* Two `makeFactoryState(id)` calls separated by
     `10^6` draws from a shared `Xorshift32` produce bitwise-identical states for all five ids, and the
     RNG's `state()` (`random.h:78`) is unchanged across each call.
  Test sketch: `SpectralState_FactoryStatesAreDistinct`. Trace: roadmap line 179; FR-013; FR-023.
- **SC-009 (Audible A/B renders for each factory state pair).** For all **10 pairs**, a
  `SpectralMorphEngine` with those two states in slots 0 and 1 drives a `HarmonicCloud` through the FR-080
  surface **in the FR-086 shape** — ≤ 64-sample slices, `engine.updateChunk(n)` →
  `cloud.setSpectralTarget(engine.getOutputRatios(), engine.getOutputAmplitudes(),
  engine.getOutputCount())` → `cloud.processStereoBlock(…, n)` per slice, zero-copy through FR-008's
  accessors — at `f0 = 110 Hz`, 48 kHz, `bloom = 0.5`, `entropy = 0`,
  External travel across the full journey over 8 s. **The drive shape is part of the criterion**, not a
  detail of the harness: it is what makes the rendered evidence roadmap line 192 asks for evidence about the
  *shipped* composition rather than about a coarser test path (FR-086).
  *Render shape — RESTATED (deviation D16); pinned, because the criterion's own measurement depends on it.*
  An earlier draft pinned the cloud exhaustively and left the render shape unstated, which made every
  spectral clause below unmeasurable. Now pinned: **`setTravelRate(0.125f)`**, so
  `slewCap = 0.125 · (numStates − 1) = 0.125` units/s and the 1-unit journey occupies exactly **8 s**;
  `noteOn()` before the first slice and no `noteOff()`; and a **three-phase render** —
  `setTargetPosition(0)` held **2.5 s**, then `setTargetPosition(1)` and the **8 s** journey, then **2.5 s**
  held at `p = 1`; **13.0 s** total. Endpoint transforms are taken **inside the frozen windows**, starting
  0.5 s in (so the 20 ms `kNormGainSmoothMs` smoother and the per-partial envelopes have settled) and
  running 65,536 samples = 1.365 s, which fits the 2.5 s window with 0.635 s to spare.
  *Cloud configuration — pinned, because everything between the injected amplitudes and the render depends
  on it.* `richness = 1.0` (so `N(r) = clamp(round(64^1), 1, 64) = 64` and no slot is gated off,
  `harmonic_cloud.h:1138-1139`), `spectralTiltDb = 0`, `mutation = 0`, `spectralGravity = 0`,
  `inharmonicity = 0`, drift depth `0`, stereo spread `0`, per-partial envelope times at their minimum.
  Without this pin, "matching the amplitude sets" is not a defined measurement: the rendered amplitude is
  `amp_override[n] · tiltGain(n)` gated by `N(r)` (`:1138-1157`), then scaled by the FR-017 normalizer
  (`:1166-1172`), then by mutation weight and envelope (`:1377-1400`).
  *Clause 1 — spectral correctness. Every threshold is stated here as a number; none is deferred.* Each
  render must contain no non-finite sample (bit-pattern test);
  have peak `< 0.9 × HarmonicCloud::kOutputClamp` (2.0f, `harmonic_cloud.h:169`) so an FR-017 normalization
  failure fails loudly; be non-silent throughout, measured as **RMS over every non-overlapping 100 ms
  window ≥ `kRenderRmsFloorDbfs = −60.0` dBFS** (the same floor `ClickDetectorConfig::energyThresholdDb`
  uses in SC-001 clause 2, so "audible" means one thing across this spec); and show the endpoint
  spectra matching the two states' amplitude sets within
  **`kEndpointMagnitudeToleranceDb = 1.0` dB per partial**. The
  endpoint comparison is over **normalized** per-partial magnitudes — each partial's measured magnitude
  divided by partial 1's — so the FR-017 normalizer's unknown global gain cancels exactly and the criterion
  measures spectral *shape*, which is what "matching the amplitude set" means.
  *(1.0 dB is chosen against Phase 2's own precedent: its SC-003/SC-010 spectral criteria hold per-partial
  magnitudes to 0.5 dB on a 65536-point Blackman-Harris transform of a single static spectrum
  (`compliance.md:74`, `:80`). Here the two neighbouring states' partials can sit within the same
  analysis window's skirts near the top of the range, so the tolerance is doubled rather than reused.)*
  *Bracketing clause, scoped — and RESTATED (deviation D16): the sample is taken from a **separately
  frozen** render, not from the travelling one.* The mid-journey spectrum must lie between the endpoints —
  per-partial normalized amplitude bracketed by the two, allowing the FR-051 stagger — **for
  `i < min(A.numPartials, B.numPartials)` only**, measured on a **separate 2.5 s render frozen at
  `p = 0.5`**.
  *Why a frozen render (D16).* The 65,536-point Blackman-Harris transform the endpoint clause uses spans
  1.365 s — **17% of the 8 s journey**. A window taken *during* travel therefore smears across a moving
  spectrum, and the per-partial claim is not measurable at all: least of all for the four Bell pairs, whose
  high slots move by hundreds of cents inside that window. Freezing at `p = 0.5` measures exactly the
  quantity the criterion is about — the interpolated spectrum halfway along — without the smear. This is a
  correction of how the criterion is sampled; the claim, the tolerance and the scope are unchanged.
  *Why the scope.* For the four pairs involving Bell (`numPartials = 24` against a 64-partial state), slots
  `≥ 24` interpolate a **continuation** ratio against a real one, at nonzero amplitude mid-journey
  (`amplitude = u_i · a_i`, since A contributes 0). At `u_i = 0.5` and `i = 63` the geometric midpoint of
  Bell's capped continuation and SineStack's 64 lands near ratio 118, i.e. ~13 kHz at the pinned
  `f0 = 110 Hz` — inside Nyquist after the FR-041 clamp, but an extrapolated partial with no authored
  counterpart in either state, so "bracketed by the endpoints" is not a meaningful claim about it. Partials
  whose rendered frequency reaches the cloud's anti-alias fade are additionally excluded and the exclusion
  count is asserted, following Phase 2's SC-010 pattern: `HarmonicCloud::updateAntiAliasGain`
  (`harmonic_cloud.h:1229-1244`) does gate ultrasonic partials (`fade = 0.0f` at `fEff >= nyquist_`), so
  they are silenced rather than folded — but a silenced partial has no magnitude to bracket.
  *Clause 2 — robustness under the cloud's own life modulation, kept separate.* One additional render per
  pair with the cloud's drift depth at **maximum** (`kMaxDriftCents = 50`, `harmonic_cloud.h:209`) and
  mutation at 1.0. This render asserts **only** non-finiteness, the peak bound and non-silence. It
  deliberately makes **no** spectral-matching claim: ±50 cents of independent per-partial drift is nearly
  twice the 27.3-cent spacing of adjacent high partials, so the rendered spectrum is expected to smear and
  reorder there, and asserting a shape match against it would be asserting something false.
  *Clause 3 — the slow-travel arm (NEW, added with D16).* SineStack/Bell only, same cloud pin, but
  **`setTravelRate(1.0f / 60.0f)`** so the 1-unit journey occupies **60 s**: 2.5 s frozen at `p = 0` → 60 s
  journey → 2.5 s frozen at `p = 1`, **65 s** total, endpoint transforms taken in the frozen windows exactly
  as in clause 1. Assert that the `p = 1` endpoint spectrum reaches **state B** within
  `kEndpointMagnitudeToleranceDb`.
  *Why this clause exists, stated as the defect it catches.* It is the only criterion in this document that
  fails under a `setSpectralTarget` whose FR-085 lever-3 dirty test compares the incoming target against the
  **stored** target rather than against the **committed** one. At this rate the per-chunk motion is
  ~0.05–0.5 cents for the low partials and 0.006 cents for partial 24 — below the per-slot epsilon — so
  under the stale-baseline form most slots never unfreeze, the render never reaches state B, and this
  assertion fails outright. At clause 1's 8 s journey the per-chunk motion is 7.5× larger and the defect
  hides. The shipped order is documented at `harmonic_cloud.h:331` ("this ORDER is load-bearing").
  Each render is additionally pinned with `render_fingerprint.h` at its published tolerances. **These
  fingerprints are regression pins, not correctness proofs** — the correctness content is the
  bracketing/endpoint assertions in clauses 1 and 3.
  Test sketch: `SpectralMorph_FactoryPairRenders`. Trace: roadmap line 192.
- **SC-010 (CPU budget).** Spec-added: **the roadmap states no CPU budget for Phase 3** (contrast lines
  164, 219, 244, 272). One is set here because a per-voice control-rate stage that is not budgeted becomes
  Phase 7's problem, and roadmap line 486 makes budgets functional requirements.
  *Prerequisite — a spike measurement, **spike-measured and recorded at T022** (amendment RA-4; the
  original wording was "spike-measured before the plan is written", which was not achievable and was
  re-timed rather than dropped).* Clause 2 asks the FR-080 injection to fit
  inside a shipped budget whose remaining headroom is narrow and known. Phase 2's gating automated
  measurements reached **29,642.8 ns/block** (`specs/seraphis-phase2-harmonic-cloud/compliance.md:80`)
  against `kMaxAdmissibleBaselineNsPerBlock = 35,555.6 ns` (`harmonic_cloud_perf_test.cpp:101`) — **≈ 5,913
  ns per 512-sample block**, i.e. **≈ 739 ns per 64-sample control chunk** for a 64-partial
  `recalculateFrequencies()` + `recalculateAmplitudes()` with all three FR-085 levers applied. So that
  clause 2 is entered with a number rather than a hope, the figure is measured by
  `SpectralMorph_CpuBudget` itself and recorded here:

  | Provenance | Value |
  |---|---|
  | Machine | 13th Gen Intel(R) Core(TM) i9-13900HX (the Phase 2 reference machine, `harmonic_cloud_perf_test.cpp:104-122`) |
  | OS | Microsoft Windows 11 Pro, build 26200 |
  | Compiler | MSVC 19.44.35220.0, x64 (Visual Studio 17 2022) |
  | Build config | `build/windows-x64-release`, Release, `--target dsp_systems_tests` |
  | Git SHA at measurement | `8d90d9ba` + the uncommitted Phase 3 working tree |
  | Trial shape | best-of-25 trials × 500 blocks per trial (`spectral_morph_perf_test.cpp:314`, `:316`), ns per 512-sample block |
  | Date | 2026-07-27 |
  | Runs | 3 consecutive, same binary, no rebuild between them |

  | Figure | Run 1 | Run 2 | Run 3 | Gate |
  |---|---|---|---|---|
  | clause 1 — engine alone | 15,164 ns (0.142%) | 16,851 ns (0.158%) | 14,432.6 ns (0.135%) | ≤ 16,000 ns (0.15%) |
  | clause 2 — cloud, changing target | 29,464.0 ns (0.276%) | *(not reached — run aborted at clause 1)* | 29,209.4 ns (0.274%) | ≤ 52,500 ns (0.5% envelope) |
  | clause 3 — cloud, unchanged target | 26,488.8 ns, ratio **1.2096** | *(not reached)* | 23,330.6 ns, ratio **1.0499** | ratio ≤ 1.10 |
  | *(context)* cloud, no target | 21,899.8 ns | *(not reached)* | 22,221.6 ns | — |
  | *(reported only, never a gate)* injection cost | 7,564.2 ns | — | 6,987.8 ns | — |

  **Outcome AT T026: clause 2 passed with margin (RA-3 not triggered); clauses 1 and 3 were NOT met.**

  ### SC-010 remediation (2026-07-27) — all three clauses now met

  The spend ladder was worked **in order**, and no reference figure, ceiling or bound was moved.

  | Step | What changed | Effect on clause 1 |
  |---|---|---|
  | Lever 4 | `centsToPitchRatioFast` promoted to Layer 0; `EntropyProcessor::applyStages` calls it instead of an `exp2` | inside the run-to-run noise at the old `N = 25` trial shape |
  | Lever 5 | `kEntropyControlInterval` 32 → 64: 8 OU control steps per 512-sample block instead of 16, i.e. 3,072 `nextFloat()` draws instead of 6,144 | 14,190–14,663 ns → **8,569–9,292 ns** |
  | FR-085 lever 1 | The specified whole-array skip was **missing** from `setSpectralTarget` and was implemented (bit-identical arrays, gated on `hasTarget_` and an equal count) | clause 3's ratio 1.05–1.21 → **0.97–1.02** |
  | Trial shape | `kTrials` 25 → 100, because clause 3 gates a **quotient of two sampled minima** and 25 trials left it straddling 1.10 on a path whose real cost difference is two 256-byte `memcmp`s per chunk. **The 1.10 bound is untouched** | clause-1 spread narrowed from ±11% to ±4% |

  Eight consecutive runs of the same binary on the same machine as the table above, best-of-100 × 500
  blocks, ns per 512-sample block:

  | Figure | min | max | Gate | Verdict |
  |---|---|---|---|---|
  | clause 1 — engine alone | 8,568.6 (0.080%) | 9,291.8 (0.087%) | ≤ 16,000 ns (0.15%) | **MET**, 1.7× margin |
  | clause 2 — cloud, changing target | 27,552 (0.258%) | 29,119 (0.273%) | ≤ 43,800 ns (baseline × 1.5) | **MET** |
  | clause 3 — unchanged/no-target ratio | 0.971 | 1.017 | ≤ 1.10 | **MET**, 8 of 8 runs |
  | *(context)* cloud, no target | 21,382 | 22,224.8 | — | — |

  The perf TU's `BASELINE PROVENANCE` block is now **filled** with that eight-run table, and both
  baselines were **tightened** from their provisional ceilings to the worst measured run rounded up —
  `kMorphBaselineNs` 10,666 → **9,300**, `kCloudChangingTargetBaselineNs` 35,000 → **29,200**. The TU
  therefore gates regressions as well as the spec budget, which is what the provisional labels said had to
  happen before it could.
  *Budget — three clauses.*
  1. `SpectralMorphEngine` (including its owned `EntropyProcessor` and `SplineTrajectory`) costs
     **≤ 0.15% of one core at 48 kHz** per instance. This is Phase 3's own new budget.
  2. `HarmonicCloud` **with a spectral target re-supplied every control chunk — i.e. driven in the FR-086
     shape, which is the shipped cadence and not a stress case** — must fit **inside Phase 2's
     existing 0.5%/voice envelope** (roadmap line 164), not beside it. Stated against the **shipped gate
     construction** rather than against the reported reference figure: the criterion is a new checked-in
     baseline `kCloudChangingTargetBaselineNs` for the target-active configuration, subject to **both** of
     the shipped relations —

     ```
     kCloudChangingTargetBaselineNs × kRegressionFactor ≤ kReferenceNsPerBlock       // 1.5 × baseline ≤ 53,333 ns
     kCloudChangingTargetBaselineNs ≤ kMaxAdmissibleBaselineNsPerBlock               // ≤ 35,555.6 ns
     ```

     — both `static_assert`ed, exactly as `harmonic_cloud_perf_test.cpp:142` and `:149` already do for
     `kAutomatedBaselineNsPerBlock`. The measured best-of-N must then satisfy
     `measured ≤ kCloudChangingTargetBaselineNs × kRegressionFactor`. Additionally, the new baseline must be
     **recorded relative to the shipped no-target baseline**: `kAutomatedBaselineNsPerBlock = 26,000.0`
     (`:140`), so the criterion reports `kCloudChangingTargetBaselineNs / 26,000` as the injection's
     multiplicative cost and requires it `≤ 1.36` (which is what `35,555.6 / 26,000` permits).
     *Correction to an earlier draft.* That draft stated clause 2 as
     "`kCloudChangingTargetNs × 1.5 ≤ kReferenceNsPerBlock`" and described `kReferenceNsPerBlock` as
     "already the live gate in the shipped Phase 2 perf test". Verified this session, it is not:
     `harmonic_cloud_perf_test.cpp:78-80` labels it "SC-007's absolute reference … **REPORTED only** — see
     the header comment", and the live gate is the measured best-of-N against the checked-in
     `kAutomatedBaselineNsPerBlock` (`:140`) times `kRegressionFactor = 1.5` (`:76`), with
     `kMaxAdmissibleBaselineNsPerBlock` (`:101`) `static_assert`ed at `:142`/`:149`. Stating the clause the
     earlier way would have permitted a target-active baseline of 35,555 ns — **37% above the shipped
     no-target baseline** — while describing it as the same gate.
     *If the measurement does not fit, there are now three responses, not two.* Make the injection cheaper
     via FR-085's **three** levers (whole-array skip, identity branches, **per-slot dirty test** — the third
     is new precisely because the first two are inert in this clause's changing-target configuration); or,
     if all three are spent and the figure still misses, invoke **RA-3** and raise roadmap line 164 to the
     measured figure, recording the measurement and the spent levers. What is *not* permitted is exceeding
     0.5% silently, or leaving the phase blocked with no defined escape. An earlier draft named only the
     first two levers and forbade any budget change — and both of those levers are provably dead in this
     configuration (a changing morph output is never bit-identical to the previous chunk and never satisfies
     `ratio_override[i] == i + 1`), so the phase could have been blocked with no permitted response at all.
  3. *FR-085's whole-array skip must be provable — as a within-run relation between measurements, not a
     comparison of two checked-in literals.* Three figures are **measured in the same run of the same TU**
     and named without a `k` prefix so they cannot be confused for constants:
     `measuredCloudBaselineNs` (no target), `measuredUnchangedTargetNs` (an identical target re-supplied
     every chunk), `measuredChangingTargetNs` (a different target every chunk). Required, on the
     measurements: **`measuredUnchangedTargetNs ≤ measuredCloudBaselineNs × 1.10`**. This is the only clause
     that exercises FR-085's lever 1 at all — clause 2's configuration supplies a *changing* target every
     chunk, so the bit-identical-array skip never fires there and that measurement says nothing about it.
     *Correction to an earlier draft.* It named the same three figures `kCloudBaselineNs`,
     `kCloudUnchangedTargetNs`, `kCloudChangingTargetNs` and simultaneously described them as "measured in
     the same TU" and as checked-in baselines. Under the checked-in reading the required relation is a
     compile-time comparison of two literals, satisfiable by editing the literals, proving nothing about
     whether the skip exists — and since this is the only clause that touches FR-085's lever 1, the
     mechanism would have been entirely untested. The checked-in `k*BaselineNs` constants exist only for the
     separate absolute regression bounds; the ratio gate is on the measurements.
  *Honest note on the roadmap's own arithmetic (not a criterion, recorded so Phase 7 does not inherit a
  surprise — see RA-2):* roadmap line 299 is "16 voices, **everything on**, ≤ 25% of one core", and
  "everything on" includes the global Aether engine budgeted at 5% by roadmap line 272. The full tally is

  ```
  16 × 2.65%  (per voice: 0.5% Ph2 + 1% Ph4 + 1% Ph5 + 0.15% Ph3, clause 1)   = 42.4%
  +  5.0%     (Phase 6 Aether, global, roadmap line 272)                      =  5.0%
  ------------------------------------------------------------------------------------
                                                                     total    = 47.4%   vs a 25% ceiling
  ```

  plus the Phase 7 output stage (`TapeSaturator` + `TruePeakLimiter`, roadmap line 290), which carries **no
  roadmap budget at all**. The per-voice budgets of Phases 2/4/5 alone (2.5%) already exceeded the implied
  1.5625%/voice; Phase 3 **does make it worse**, by clause 1's 0.15%. Clause 2 is what keeps the damage to
  0.15% rather than 0.50%. Two earlier drafts were wrong here in opposite directions — one claimed Phase 3
  "declines to make it worse" (false), the next stated the overage as 42.4% by counting only the per-voice
  sum (understating it by the 5 points of roadmap line 272). The roadmap's Phase 7 ceiling (line 299) must
  be amended, or the voice count reduced, or the per-voice budgets re-derived, or the output stage budgeted,
  before Phase 7 is specced (RA-2).
  *Measurement basis.* Identical construction to Phase 2 SC-007
  (`specs/seraphis-phase2-harmonic-cloud/spec.md:843-862`, whose own Phase 1 pinning reference is at
  `:852`) and Phase 1 SC-007: the metric is **nanoseconds per 512-sample block** (8 × 64-sample control
  chunks), and the percentage is derived against the fixed 512-at-48 kHz wall-clock budget of 10.667 ms.
  0.15% is `kMorphReferenceNsPerBlock ≈ 16,000 ns`; 0.5% is the cloud's existing
  `kReferenceNsPerBlock ≈ 53,333 ns`.
  *What is enforced, and how the three clauses are constructed.* Every clause is measured as an **absolute
  best-of-N ns/block figure with its own checked-in baseline**, each gated relatively against its own
  baseline (`best-of-N ≤ kXBaselineNs × kRegressionFactor`, `kRegressionFactor = 1.5`) — because no CI leg
  evaluates perf-tagged cases (every leg filters `'~[performance]~[perf]~[benchmark]~[!benchmark]'`;
  `.github/workflows/ci.yml:328`, `:574`, `:951`, `valgrind-nightly.yml:202`). Absolute figures are
  WARN-reported locally.
  **Two kinds of identifier, kept typographically distinct so they cannot be confused.**
  *Checked-in constants* (`k`-prefixed, edited only with a provenance block): `kMorphBaselineNs` (clause 1)
  and `kCloudChangingTargetBaselineNs` (clause 2). *Per-run measurements* (no `k` prefix):
  `measuredMorphNs`, `measuredCloudBaselineNs`, `measuredUnchangedTargetNs`, `measuredChangingTargetNs`.
  Clauses 1 and 2 gate a **measurement against a constant**; clause 3 gates a **measurement against another
  measurement from the same run**.
  **No clause is expressed as a differential of two best-of-N minima.** That construction was in an earlier
  draft ("the *additional* cost the FR-080 series adds") and is unsound: the difference of two sampled
  minima is a biased, possibly negative statistic that a multiplicative regression factor cannot bound.
  The injection cost is instead reported as `measuredChangingTargetNs − measuredCloudBaselineNs` for
  information, while the *gates* are as stated in clauses 1–3 above.
  *Binding arithmetic.* Each checked-in baseline MUST satisfy
  `kBaselineNsPerBlock × kRegressionFactor ≤ kReferenceNsPerBlock` **and**
  `kBaselineNsPerBlock ≤ kMaxAdmissibleBaselineNsPerBlock` for its own reference figure
  (`kMorphReferenceNsPerBlock` for clause 1, the cloud's `kReferenceNsPerBlock` for clause 2), both enforced
  by actual `static_assert`s in the perf TU — the same two-assert shape the shipped Phase 2 TU uses at
  `harmonic_cloud_perf_test.cpp:142` and `:149`.
  This is a **strengthening** of the in-repo precedent, not a copy of it: Phase 1 states the same
  arithmetic only in a comment beside its constants — "3000 x 1.5 = 4500 ns < 5333 ns, so the test is no
  weaker than the SC-007 reference figure",
  `dsp/tests/unit/processors/life_modulators_perf_test.cpp:63-66`, over
  `kReferenceNsPerBlock` (:58), `kBaselineNsPerBlock = 3000.0` (:70) and `kRegressionFactor = 1.5` (:73) —
  and a grep for `static_assert` in that file returns nothing, so the relation is currently unenforced and
  a future baseline edit could silently break it. If the first measurement cannot meet the relation, the
  phase is over budget and the response is to reduce cost — never to raise the baseline. (For clause 2
  only, RA-3 provides one further, explicitly recorded response after all three FR-085 levers are spent.)
  *Configuration measured:* clause 1 — 4 states, `bloom = 1`, `entropy = 1` (all four stages live, both OU
  banks advancing, death/rebirth active), Spline travel mode at `kDefaultInterval`. Clauses 2 and 3 — the
  same cloud
  configuration as Phase 2's SC-007 (both drift banks live, Mutation at 1.0, the 64-sample chunked loop —
  which is the FR-086 slice cadence), differing only in whether and how a target is supplied. The 8 extra
  call boundaries per 512-sample block that FR-086 costs are therefore inside the measured figure, not
  outside it.
  Test sketch: `SpectralMorph_CpuBudget` (`[.perf]`). Trace: roadmap line 486; RA-2; RA-3.
- **SC-011 (RT safety / allocation-free).** A steady-state loop — `updateChunk`, `processChunk`,
  `setEntropy`, `setBloom`, `setTargetPosition`, `setState`, `setSpectralTarget`, `processStereoBlock` —
  performs **zero** heap allocations after `prepare()`. Measured with
  `tests/test_helpers/allocation_detector.h` (`AllocationDetector` :26, `AllocationScope` :75). That header
  is **inert on its own** — its global `operator new`/`delete` replacements are commented out (:108-136,
  inside the commented `#ifdef ENABLE_ALLOCATION_TRACKING` block that opens at :106 and closes at :138;
  verified by `grep -n` this session, and cited identically in the Test-side helpers list below) —
  and counting requires `allocation_operator_overrides.h` included from **exactly one TU per binary**. Both
  binaries this phase touches already have that TU, so **no new test file in this phase may include it**:
  `dsp_systems_tests` via `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`, and
  `dsp_processors_tests` via `dsp/tests/unit/processors/brownian_drift_test.cpp:28` (the neighbouring
  Phase 1 files record the duplicate-symbol hazard explicitly — `breathing_modulator_test.cpp:13`,
  `growth_envelope_test.cpp:12`, `orbit_modulator_test.cpp:11`, `life_modulators_perf_test.cpp:22`). All
  methods are `noexcept`, verified by `static_assert(noexcept(...))` on the full public surface.
- **SC-012 (Determinism under seed).** Two instances with the same seed, the same `prepare(48000)` and the
  same call sequence produce **bitwise-identical** output arrays over ≥ 500 chunks, at `entropy = 1` and
  Spline travel (i.e. every stochastic path live).
  *Rewind clause — RESTATED (deviation D15).* `reset()` returns an advanced instance to the arrays that a
  **freshly-prepared instance carrying the same parameter set** produces — **not** to the arrays of a bare
  post-`prepare` instance. Concretely: instance X is prepared, configured (states, count, bloom, entropy,
  mode, rate, seed), advanced ≥ 500 chunks, then `reset()`; instance Y is prepared and given the
  **identical** configuration calls in the identical order; the two output arrays must then be **bitwise
  identical**. X's configuration getters must additionally be unchanged across its own `reset()`, asserted
  explicitly — that is what proves `reset()` **rewinds** rather than **reconfigures**.
  *Why (D15).* As originally written ("returns … to its exact post-`prepare` arrays") the clause is
  **unsatisfiable by a faithful implementation at this criterion's own configuration**. The test runs at
  `entropy = 1`, so after `reset()` the 64 static scatter offsets are redrawn and applied at `w_3 = 1`,
  while a bare post-`prepare` instance is clean at entropy 0 — the two can never match. FR-005 states that
  `reset()` matches `BrownianDrift::reset()` (`brownian_drift.h:133`), which **keeps** configuration, and
  FR-005's default table scopes itself to "after default construction and after `prepare(sampleRate)` with
  **no** parameter call". The wiping reading would also have erased the patch on every Phase 7 voice
  allocation. `reset()` therefore rewinds stochastic and travel state only, and FR-005's default slot load
  moved to the constructor. No tolerance changed: the comparison is still bitwise.
  Seed 0 is safe (`Xorshift32` substitutes a default, `random.h:44`,
  `:72-74`) and every derived per-partial stream seed remains **pairwise distinct and non-zero** across all
  four salt ranges (amp-jitter, decoherence, scatter, death/rebirth) — asserted directly on the **Layer 0**
  `deriveStreamSeed` (FR-006, `core/random.h`), over the full `4 × 64 = 256`-salt cross product for at
  least the 8 SC-006 seeds. A second, small clause asserts `HarmonicCloud::deriveSeed(b, s) ==
  deriveStreamSeed(b, s)` for that same input set, which is what proves the FR-006 forwarding rewrite left
  Phase 2's streams untouched. Test sketch: `SpectralMorph_DeterministicUnderSeed`. Trace: roadmap line 131
  carried forward; lines 300–303.
- **SC-013 (Sample-rate and chunk-length invariance).** All behaviour is defined in Hz and seconds. At
  44.1 / 48 / 96 kHz, and with chunk lengths `{1, 7, 64, 512, 4096}`:
  *Journey clause, with the rate and journey pinned.* `numStates = 2` (a 1.0-unit journey), External travel
  at `kMaxTravelRate = 1.0` **journeys/s** — `slewCap = 1.0 · (2 − 1) = 1.0` units/s at this state count —
  so the nominal journey is **1.000 s**. The measured journey duration
  must be within **`max(0.5% of the journey duration, one chunk duration)`** of nominal. The
  whichever-is-larger form is required, not a convenience: a 4096-sample chunk at 48 kHz is 85.3 ms, an
  8.5% quantization floor that a flat 0.5% tolerance can never clear, while at the slow end
  (`kMinTravelRate = 1/600`) a flat 0.5% is trivially satisfied at 0.014%. The test reports both terms so
  it is visible which one bound each case.
  *Other clauses.* An entropy sweep's stationary deviation metric (SC-004 metric 1) agrees across rates
  within **`max(5% relative, 5 × the larger of the two measurements' reported standard errors)`** — the
  number is stated here rather than deferred, and it is derived from SC-004's own machinery rather than
  guessed. `BrownianDrift`'s discretisation is exact (`a = exp(−dt/τ)`, `g = kInternalStd·sqrt(1−a²)`,
  `brownian_drift.h:230-240`), so the *stationary distribution* is rate-independent by construction; what
  differs across 44.1 / 48 / 96 kHz is only the **sampling** of it, because the walks advance at
  `BrownianDrift::kControlRateInterval = 32` samples (`:105`) and therefore take a different number of
  steps per second. Over SC-004's window (8 seeds × 10 τ at τ = 8 s) the metric has roughly
  `(80/8) × 64 × 8 ≈ 5100` effectively independent samples, giving a standard error near 1.4% of the mean
  and `5 × SE ≈ 7%`; the flat 5% floor covers the case where the test's own reported SE comes out smaller
  than that. The whichever-is-larger form is required for the same reason SC-013's journey clause uses one.
  The FR-044 per-chunk bound is met at every rate and chunk length (the
  constants are stated at 48 kHz / 64 samples and every contributor in FR-044's table scales with
  `chunkSeconds`, so the test scales the bound rather than re-deriving it). Re-calling `prepare()` with a
  new rate re-derives every coefficient and leaves no stale value.
  Test sketch: `SpectralMorph_SampleRateInvariant`.
- **SC-014 (Phase 2 neutrality — the standing regression gate on the FR-080 amendment).** Three clauses.
  1. With **no** `setSpectralTarget` call ever made, a `HarmonicCloud` render across a documented parameter
     grid is identical to the pre-amendment build under `render_fingerprint.h` tolerances, and the entire
     existing `harmonic_cloud_test.cpp` / `harmonic_cloud_spectral_test.cpp` suites pass unchanged (no test
     edits permitted as part of this phase — an edit there is a failure of this clause).
     *How the pre-amendment side is measured, since that build does not exist at test-run time.* The
     existing suite only ever compares two renders produced **within the same run** (e.g.
     `dsp/tests/unit/systems/harmonic_cloud_test.cpp:1141-1157`), and no checked-in pre-amendment
     fingerprints exist — so as an earlier draft stated it, this clause could not be executed at all.
     Ordering requirement, binding on the plan: **before any FR-080 or FR-006 edit to `harmonic_cloud.h`
     lands**, the `RenderFingerprint` values for the documented grid must be captured on the current
     `main` build and checked in as named constants (`kPreAmendmentFingerprints[...]`) with a provenance
     block naming **commit, machine, build configuration and date**, in the same shape as
     `harmonic_cloud_perf_test.cpp:104-119`. Clause 1 is then the comparison of the post-amendment render
     against those constants under `TestUtils::kSampleTolerance` (:49) and `kMetricTolerance` (:52) via
     `compareFingerprints`. The grid is documented in the test and must include at least: 3 Richness values
     × `{gravity 0, ±1}` × `{B = 0, 0.05}` × `{tilt 0, ±12}` × `{mutation 0, 1}` × `{drift 0, max}`.
  2. With `setSpectralTarget` supplied the FR-084 identity arrays, the render matches the parametric render
     under the same fingerprint tolerances, at ≥ 3 Richness values × `{gravity 0, ±1}` × `{B = 0, 0.05}` ×
     `{tilt 0, ±12}`.
  3. `clearSpectralTarget()` mid-render produces no click by the SC-001 clause-2 differential detector.
  Test sketch: `HarmonicCloud_SpectralTargetIsNeutralWhenIdentity`. Trace: Clarifications C-1.
- **SC-015 (Non-finite hygiene and parameter extremes).** Over an enumerated grid — entropy `{0, 1}` ×
  bloom `{0, 1}` × travel rate `{min, max}` × states `{2, 4}` × seeds `{4}` × the extremal factory states,
  plus NaN/Inf/out-of-range arguments fed to every setter and to `setSpectralTarget`'s arrays — no output
  array element and no rendered sample is non-finite, every setter rejection leaves the getters and the
  output bitwise unchanged (FR-007), and `stateFinite()` reports true throughout.
  *`setSpectralTarget`'s rejection set, enumerated (FR-081 is authoritative, not FR-012).* Must be
  **rejected wholesale**: `ratios == nullptr`; `amplitudes == nullptr`; `count == 0`;
  `count > kMaxPartials`; any element non-finite; any `ratios[i] ≤ 0` (including `-0.0f`); any
  `amplitudes[i] < 0`. Must be **accepted**, and the test asserts acceptance explicitly so an
  over-zealous implementation fails: non-monotone ratios; a ratio above `kMaxStateRatio`; a ratio below
  `kMinStateRatio` (both bounds are `SpectralState` invariants, not cloud ones); an amplitude
  above 1 (up to `1 + kMaxAmpJitter`, which is what the morph engine's own entropy-perturbed output
  produces and what this surface exists to consume). Non-finite test inputs
  are constructed from bit patterns via a volatile sink, never from
  `std::numeric_limits<float>::quiet_NaN()` (the macOS leg's `-ffast-math` folds those to finite garbage).
  Test sketch: `SpectralMorph_ExtremesStayFinite`.
- **SC-016 (Phase decoherence is real).** Measured at **`entropy = 0.45`**, over a ≥ 120 s advance, across
  the 8 SC-006 seeds.
  *Why 0.45 and not 0.5.* At `e = 0.45` the FR-071 stage weights are `w_2 = (0.45 − 0.25)/0.35 = 0.571`
  and `w_3 = 0` with 0.05 of margin below stage 3's onset at 0.50. The originally-drafted `e = 0.5` sat
  **exactly on stage 3's onset**, where `w_3` clamps to 0 with zero margin, and its parenthetical
  ("stage 2 fully engaged") was wrong twice over — stage 2's ramp ends at 0.60, so `w_2` is 0.714 there,
  not 1. The criterion would have survived only by the clamp and would have been silently invalidated by
  any adjustment to the FR-071 interval boundaries. **The test asserts `w_3 == 0.0f` at the chosen point**
  (through FR-008's stage-weight introspection) so it fails loudly rather than quietly if those boundaries
  move.
  *(a) Zero-mean.* Each partial's **mean** ratio deviation over the run is within
  `kMeanRatioDriftCents = 2.0` cents of its clean ratio — the perturbation is zero-mean and the partial
  does not walk off pitch.
  *Derivation of 2.0 cents (from the FR-072 pinned constants, not from a first run).* At `w_2 = 0.571` the
  decoherence amplitude is `0.571 · kMaxDecoherenceCents = 2.29` cents times an OU walk whose stationary
  std is `kInternalStd = 0.5` (`brownian_drift.h:101`) — so `σ ≈ 1.14` cents. The sample mean of one OU
  trajectory of length `T = 120 s` at `τ = 8.0 s` (FR-072 bank (b)) has standard error
  `σ·sqrt(2τ/T) = 1.14 · sqrt(16/120) = 0.42` cents. `kMeanRatioDriftCents = 2.0` is **4.8 σ_SE**, so the
  probability that any of 64 partials breaches it by chance is ≈ `64 · 2 · Φ(−4.8) ≈ 1e-4` per seed.
  *(b) Variance grows with time — with the ensemble named.* The accumulated phase error is the running
  integral of `1200·log2(r_i / r_i^clean)` converted to radians. The variance is taken **across the 64
  partials** at each of ≥ 40 evenly spaced time points, and the test additionally repeats the whole fit
  across the 8 seeds and requires the result on each. A linear fit of that variance versus elapsed time must
  have a positive slope exceeding its own standard error by ≥ 5×. Naming the ensemble is not pedantry: a
  single run gives one sample of accumulated phase per partial per time point, so "variance" is otherwise
  undefined. Positive slope is the defining property of a random-walk phase and is what distinguishes
  decoherence from a static detune.
  *Comparison arm — stage 3 is a different mechanism. RESTATED at `entropy = 0.74` (deviation D17); the
  originally-drafted `entropy = 0.85` arm is unpassable by a faithful implementation.* At
  **`entropy = 0.74`**: **per seed, at least 24 of the 64 partials** have a mean deviation exceeding
  `kMeanRatioDriftCents`, and **pooled across the 8 seeds, at least 256 of 512**; while at `e = 0.45` the
  count is **0**. The test additionally asserts `getStageWeight(4) == 0.0f` and
  `getStageWeight(3) ≈ 0.6857` at this setting, so a move of the FR-071 interval boundaries fails loudly —
  the same protection the 0.45 arm has.
  *Why 0.85 fails (D17).* The original derivation — `P(|7·s_i| > 2.0) = 1 − 2/7 = 0.714`, expected 45.7 of
  64, threshold ≥ 32 — assumes each partial carries **one static** scatter offset for the whole run. At
  `e = 0.85` that assumption is false: stage 4 is live (`w_4 = (0.85 − 0.75)/0.25 = 0.40`, i.e. a death rate
  of `0.4 · kMaxDeathRatePerSecond = 0.02/s`), so over a ≥ 120 s run each partial dies ≈ 2.4 times and
  FR-073 **redraws `s_i` on every death**. The run-mean becomes the time-average of ~3 independent
  `U[−7, +7]` draws: `σ` falls from 4.04 to ≈ 2.33 cents, `P(|mean| > 2.0) ≈ 0.39`, expected **≈ 25 of 64** —
  below the ≥ 32 threshold, on every one of the 8 seeds.
  *Derivation at 0.74 (redone from scratch, not scaled from the old one).* `w_4 = 0` (stage 4 opens at 0.75),
  so `s_i` really is static as the derivation assumes. `w_3 = (0.74 − 0.50)/0.35 = 0.6857`, so the offset is
  `0.6857 · kMaxScatterCents · s_i = 4.80 · s_i ~ U[−4.80, +4.80]`. `w_2 = 1.0` (clamped), so decoherence
  contributes a zero-mean OU term of `σ = 2.0` cents whose run-mean standard error over 120 s is
  `2.0·sqrt(2·8/120) = 0.73` cents. The ±2.0 boundary sits ≥ 3.8 SE inside the uniform's support, so the
  convolution does not move it: `P(|offset + noise| > 2.0) = 1 − 4.0/9.6 = 0.5833`, mean **37.33 of 64**,
  `sd = sqrt(64 · 0.5833 · 0.4167) = 3.944`. The per-seed gate of ≥ 24 is **3.38 sd** below the mean
  (`P(fail) ≈ 3.6e-4` per seed, ≈ 2.9e-3 over 8 seeds); the pooled gate of ≥ 256 of 512 is 3.8 sd below the
  pooled mean of 298.7. **No threshold was loosened** — the mechanism the arm separates is unchanged, and
  both replacement gates are derived a priori rather than read off a run.
  At `e = 0.45` the count remains **0**: `w_3 = 0`, so the deviation is decoherence-only at `σ = 1.143`
  cents with a run-mean SE of 0.417, giving `P(|mean| > 2.0) = 1.6e-6` per partial and 8e-4 expected over
  all 512.
  This is also why FR-072 pins `kMaxScatterCents = 7.0` against `kMaxDecoherenceCents = 4.0` rather than the
  reverse: the static term must dominate the wander's sampling error for the two mechanisms to be
  separable over a finite run at all.
  Test sketch: `EntropyProcessor_PhaseDecoheres`. Trace: roadmap line 186; Clarifications C-5.

## Edge Cases

- **RT-safety boundaries.** `updateChunk(0)` and `processChunk(..., 0, ...)` are no-ops leaving state
  **unadvanced**; null array pointers are rejected without writing and without advancing. Any chunk length
  is legal, including 1, lengths that are not multiples of `BrownianDrift::kControlRateInterval = 32`
  (`brownian_drift.h:105`), and very large chunks (16384+) — an entropy walk advances internally at its own
  control rate (`brownian_drift.h:194-206`) and a `SplineTrajectory` rotates as many waypoints as needed
  (`spline_trajectory.h:262-269`), so no fixed array can be overrun and no value can freeze for the length
  of a giant block. Calling any process method before `prepare()` produces the post-default-construction
  arrays rather than reading uninitialized coefficients, mirroring `HarmonicCloud`'s `prepared_` guard
  (`harmonic_cloud.h:691-695`).
- **FR-041 fill degeneracies.** A state with `numPartials < 2` has no ratio pair to seed the continuation
  from, so the fill is `r_j = j + 1` (FR-041) and the clamps still apply. A state whose last two ratios are
  equal cannot occur (FR-012 requires strict increase), but `g_j` is clamped at `1.0` from below anyway so
  an equal pair would degenerate to the `kFillSpacingFactor` staircase rather than to a flat run. A state
  whose last pair grows faster than an octave is clamped to `kMaxFillGrowth`; a fill that reaches
  `kMaxFillRatio` continues on the `kFillSpacingCents` staircase. In every case the fill is finite, strictly
  increasing, and bounded by `kMaxOutputRatio` — asserted directly in SC-015 over the adversarial
  `{kMinStateRatio, kMaxStateRatio}` two-partial state.
- **Parameter extremes.** `numStates = 2` with both slots holding the *same* state must produce a
  perfectly static output (every delta 0) rather than dividing by a zero segment length. **This is the
  default configuration, not a corner:** FR-005 defaults all four slots to
  `makeFactoryState(SineStack)`, so the identical-slots path is the one every un-configured engine takes
  and SC-002 clause 5 exercises it directly. And, with FR-047,
  a `setState` that installs an *identical* state must be recognised as a no-op rather than arming a
  2-second crossfade from an array to itself (a crossfade between identical arrays is a no-op numerically,
  but the FR-008 introspection must not report a fade in flight). `numStates = 4`
  with `p` exactly at an integer boundary must not oscillate between bracketing pairs (the `min(k+1,
  numStates−1)` clamp in FR-041 is what makes `p = numStates−1` well-defined). `bloom = 1` with a travel
  rate at maximum is SC-001's worst case. `entropy = 1` with a sparse state (`numPartials = 1`) must not
  kill the only partial permanently — the death/rebirth dwell is bounded, and a state with
  `numPartials = 0` (all-silent) must remain silent and finite rather than producing NaN through the FR-014
  normalization guard. Travel rate at minimum (`kMinTravelRate = 1/600` journeys/s) over a short render must still move
  monotonically rather than quantizing to zero.
- **Sample-rate changes.** `prepare()` with a new rate re-derives the travel increment, every OU
  coefficient (`a = exp(−dt/τ)`, `g = σ·sqrt(1−a²)`, `brownian_drift.h:230-240`), every death/rebirth ramp
  length in samples, and propagates to the owned `SplineTrajectory` and every drift lane. A rate change
  mid-travel may reset state (it is not an audio-thread operation) but must not leave a stale coefficient.
  All constants that read as times are seconds, never samples (SC-013).
- **Seed determinism.** Seed 0 is safe via `Xorshift32`'s substitution (`random.h:44`), but derived lane
  seeds must never *be* 0 — the Layer 0 `deriveStreamSeed` (FR-006) guarantees that explicitly, carrying
  over the `(h != 0u) ? h : 0x2545F491u` substitution verified at `harmonic_cloud.h:659`, and is reused
  rather than re-derived. Every once-per-seed draw (the FR-072c static scatter set, each
  partial's death-dwell and ramp randomizations) is taken in a **fixed, documented order** so `reset()`
  reproduces them exactly. Two instances that received the same calls in a different *order* are not
  required to agree; SC-012 pins the sequence, not just the seed.
- **Interaction with the cloud's own life modulation.** With a target active, the cloud's Mutation
  (`harmonic_cloud.h:1377-1388`) and per-partial detune drift (`:1354-1356`) still apply downstream of the
  injected values. That is intended — but it means entropy's ratio perturbation and the cloud's drift are
  *additive in cents*.
  **No-crossing is scoped to the engine's own output array, and only there.** That is the array Phase 3
  controls, where FR-046 establishes a 24-cent floor and FR-074's `static_assert` proves 11 cents of
  entropy cannot close it. It is **arithmetically impossible** to extend the guarantee through the cloud's
  drift, and an earlier draft of this edge case demanded exactly that: drift is applied per partial as
  `cents = driftCents_ · driftAmount_[i] · d` with `d ∈ [-1, +1]` (`harmonic_cloud.h:1354-1356`) and
  `driftAmount_[i] = pow((i+1)/64, kDriftIndexExponent) · u`, `u ∈ [0.5, 1.0]` (`:1007-1010`), so at
  `kMaxDriftCents = 50.0f` (`:209`) the top partials receive essentially the full ±50 cents from
  *independent* lanes — against the `1200·log2(64/63) = 27.32` cents that separates partials 63 and 64.
  Two adjacent high partials can therefore cross **under Phase 2's own drift with entropy at exactly
  zero**, and no choice of `kMaxDecoherenceCents + kMaxScatterCents` can prevent it. Requiring FR-074's
  constants to "account for" `kMaxDriftCents` would have made FR-074 unsatisfiable without changing Phase 2
  behaviour, which is a stated Non-Goal.
  What is required of the rendered case instead is a **bounded-displacement** statement, which is true and
  testable: with a target active, no partial's rendered frequency may deviate from its injected ratio by
  more than `kMaxDecoherenceCents + kMaxScatterCents + kMaxDriftCents = 61` cents. Reordering of adjacent
  high partials under maximum drift is **expected and accepted**; SC-009 clause 2 renders that
  configuration and asserts only finiteness, peak and non-silence, deliberately making no spectral-shape
  claim about it.
  *Forwarded to Phase 7:* the combined drift + entropy cent budget is a per-voice decision (roadmap lines
  283–297 put both under `SeraphisVoice`/the macro layer). If top-octave partial ordering turns out to
  matter audibly, Phase 7 caps `driftCents` — Phase 3 does not, because doing so would change Phase 2
  behaviour.
- **Non-finite hygiene.** No NaN/Inf may reach any output for any input, including non-finite setter
  arguments (FR-007) and non-finite array elements (FR-081). Guards use bit-pattern tests, never
  `std::isnan` (`-ffast-math` on the macOS leg). The helpers are **Layer 0**: `detail::isNaN`
  (`dsp/include/krate/dsp/core/db_utils.h:54`) and `detail::isInf` (`:174`), which the Layer 2 components
  include directly — `harmonic_cloud.h:342-344` is a use site, not the definition, and reaching for that
  Layer 3 header would violate FR-002. `HarmonicCloud::stateFinite()` (`harmonic_cloud.h:858`) is the
  pattern for the aggregate check. Denormal walk states are flushed to zero
  as `BrownianDrift` does (`kDenormalFloor = 1e-20f`, `brownian_drift.h:228`, applied at :264-266).
- **Portability.** `node tools/check-portability.js` must pass; no narrowing in brace initialization
  (use designated initializers); no arch-guarded krate includes; if any SIMD is introduced it uses
  `hn::LoadU`/`hn::StoreU` unless alignment is proven. `std::byte` arithmetic in the serializer must not
  rely on implementation-defined signedness.
- **Test registration.** Each new test file must be added explicitly to the correct target list in
  `dsp/tests/CMakeLists.txt` — the processors-layer list (around `:275-281`, where
  `brownian_drift_test.cpp`, `spline_trajectory_test.cpp` and `life_modulators_perf_test.cpp` live) for
  `SpectralState` / `EntropyProcessor`, and the systems-layer list (around `:298-334`, where
  `harmonic_cloud_test.cpp` lives) for `SpectralMorphEngine` and the FR-080 cloud tests. Sources are listed
  explicitly, not globbed; an unlisted file silently drops.

## Existing Components (reused — verified this session)

| Component | Header (verified) | Real signature / what is reused |
|---|---|---|
| `HarmonicCloud` (L3, Phase 2) | `dsp/include/krate/dsp/systems/harmonic_cloud.h:122` | **Amended, not replaced.** Verified anchors the FR-080 series attaches to: `kMaxPartials = 64` (:133), `kControlChunkSamples = 64` (:139), `kOutputClamp = 2.0f` (:169), `kMaxInharmonicity = 0.1f` (:186), `kGravityExponentRange = 0.1f` (:193), `kMaxDriftCents = 50.0f` (:209); `prepare(double)` (:255), `reset()` (:286), `setFundamentalHz(float)` (:341) with its NaN/Inf rejection idiom (:342-344), `setRichness/​setInharmonicity/​setSpectralTiltDb/​setMutation/​setSpectralGravity` (:370, :384, :397, :410, :436), `noteOn()` (:593), `noteOff()` (:621), `static constexpr deriveSeed(std::uint32_t, std::size_t)` (:651-660 — **rewritten by FR-006 as a forward to the new Layer 0 `deriveStreamSeed`**, body unchanged, covered by SC-014 clause 1), `setSeed(std::uint32_t)` (:665), `processStereoBlock(float*, float*, std::size_t)` (:682), the introspection surface (:754-905 incl. `getPartialSinState` :819 / `getPartialCosState` :823 — **read-only, no phase setter**, the fact Clarifications C-5 rests on), `stateFinite()` (:858). Injection points: `recalculateFrequencies()` (:1064, law at :1083-1092), `recalculateAmplitudes()` (:1134, law at :1146-1157, normalizer `setTarget` last at :1172), the dirty-flag consumption at the head of `updateControl` (:1310-1321). |
| `SplineTrajectory` (L2, Phase 1) | `dsp/include/krate/dsp/processors/spline_trajectory.h:114` | `class SplineTrajectory : public ModulationSource`. Used verbatim as the Spline travel driver: `prepare(double)` (:136), `reset()` (:144), `setSeed(std::uint32_t)` (:156), `setWaypointInterval(double)` (:165, clamped to `[kMinInterval 0.5, kMaxInterval 30.0]` :117-119), `setDepth(float)` (:174), `processBlock(size_t)` (:193), `getCurrentValue()` (:204), `getSourceRange()` → fixed `{-1, +1}` (:209). C1 continuity is the component's own documented guarantee (:35-51) and is why FR-061 needs no extra smoothing. |
| `BrownianDrift` (L2, Phase 1) | `dsp/include/krate/dsp/processors/brownian_drift.h:94` | `class BrownianDrift : public ModulationSource`. The entropy walks (FR-072) reuse its exact-OU discretisation: `a = exp(−dt/τ)`, `g = kInternalStd·sqrt(1−a²)`, Irwin-Hall `z` from three `nextFloat()` draws (:230-240, :253-270); `kTauMin = 0.2f` / `kTauMax = 30.0f` (:97-99), `kInternalStd = 0.5f` (:101), `kDriftOutputSmoothMs = 150.0f` (:103), `kControlRateInterval = 32` (:105), `kWalkLimit = 4.0f` (:226), `kDenormalFloor = 1e-20f` (:228). API: `prepare(double)` (:121), `reset()` (:133), `setSeed` (:145), `setSmoothness` (:152), `setDepth` (:159), `setMean` (:165), `processBlock(size_t)` (:194), `getCurrentValue()` (:212). |
| `SpectralMorphFilter` (L2) | `dsp/include/krate/dsp/processors/spectral_morph_filter.h:67` | **Concept reference only — the class is NOT used.** It is an STFT/overlap-add filter that morphs *audio* spectra (`prepare(double, std::size_t)` :125 allocates `std::vector`s; `processBlock(const float*, const float*, float*, std::size_t)` :234; latency = FFT size :461), which is the wrong domain for a partial-domain morph and would add `fftSize` samples of latency per voice. Reused: the **magnitude-interpolation law** `magA·(1−u) + magB·u` (`applyMagnitudeInterpolation` :591-606) adopted verbatim by FR-041, and the tilt **range convention** `kMinSpectralTilt/kMaxSpectralTilt = ∓12.0f` (:87-88). Named by the roadmap reuse row (line 87) as "(concept)"; verified here that is all it can be. `enum class PhaseSource` (:54) already exists in `Krate::DSP` — a near-name to avoid. |
| `crossfade_utils` (L0) | `dsp/include/krate/dsp/core/crossfade_utils.h:50,64,89` | `inline void equalPowerGains(float, float&, float&) noexcept` (:50) → `cos(position·kHalfPi)` / `sin(position·kHalfPi)` (:51-52); pair overload (:64); `crossfadeIncrement(float durationMs, double sampleRate)` (:89). **Named by the roadmap reuse row (line 87) and examined this session, but NOT reused by any requirement.** Not used for the FR-041 state blend: an equal-power law is for two *decorrelated* signals, whereas two spectral states share partial slots and blend coherently — magnitude-linear is correct there (C-7). Not used for FR-047's state-change absorption either, which is a linear `x` advanced on `chunkSeconds` for sample-rate and chunk-length independence (SC-013), not a per-sample increment. And **no absorption ramp exists for FR-062**: that requirement was resolved the other way — a mode switch changes the slew limiter's *target*, never the position, so there is nothing to absorb. |
| `harmonic_types.h` (L2) | `dsp/include/krate/dsp/processors/harmonic_types.h:21,36,54` | **Named by the roadmap reuse row (line 87) but NOT reused.** `kMaxPartials = 96` (:21) is a *namespace-scope* constant sized by the Innexus analysis pipeline; `struct Partial` (:36) and `struct HarmonicFrame` (:54) are analysis contracts. Verified here for two reasons: so the plan does not reach for `kMaxPartials` (C-8 pins a class-scoped 64 instead), and so no new namespace-scope partial-count constant is introduced beside it. |
| `HarmonicSnapshot` (L2) | `dsp/include/krate/dsp/processors/harmonic_snapshot.h:30` | **Examined for ODR/overlap per roadmap line 180; NOT reused and NOT extended.** `struct HarmonicSnapshot` holds `std::array<float, kMaxPartials>` (=96) `relativeFreqs` / `normalizedAmps` / `phases` / `inharmonicDeviation` (:34-37) plus residual and analysis metadata (:39-44), and its only producer/consumer pair is `captureSnapshot(const HarmonicFrame&, const ResidualFrame&)` (:75) and `recallSnapshotToFrame(const HarmonicSnapshot&, HarmonicFrame&, ResidualFrame&)` (:125) — both `HarmonicFrame`-typed, i.e. analysis-side. Its **L2 normalization** idiom (:100-107) is the pattern FR-014 copies. |
| `Xorshift32` (L0) | `dsp/include/krate/dsp/core/random.h:40` | `explicit constexpr Xorshift32(uint32_t = 1) noexcept` (:44) — substitutes `kDefaultSeed` for 0; `next()` (:49), `nextFloat()` → [-1,1] (:58), `nextUnipolar()` → [0,1] (:66), `seed(uint32_t)` (:72, same 0-substitution), `state()` (:78). All entropy and travel randomness. **This header is amended by FR-006:** the new free function `deriveStreamSeed(std::uint32_t, std::size_t)` is added here (Layer 0) with `HarmonicCloud::deriveSeed`'s exact body, and `HarmonicCloud::deriveSeed` (`harmonic_cloud.h:651-660`, verified `public static constexpr`, lowbias32, non-zero substitution at `:659`) is rewritten as a one-line forward. Both Layer 2 components need the hash and neither may include the Layer 3 header (FR-002, FR-003). **The header currently includes only `<cstdint>` (`:17`) — 94 lines, no other include — so FR-006 also requires adding `#include <cstddef>` for the new function's `std::size_t` parameter; MSVC and libstdc++ supply it transitively, so a green Windows build cannot catch its absence.** |
| `db_utils.h` (L0) | `dsp/include/krate/dsp/core/db_utils.h:39,54,174` | **The actual definition site of the non-finite guards.** `namespace detail` opens at `:39`; `constexpr bool isNaN(float) noexcept` at `:54` (exponent-all-ones + non-zero-mantissa test over `std::bit_cast<std::uint32_t>`); `constexpr bool isInf(float) noexcept` at `:174`; `detail` closes at `:179`. `harmonic_cloud.h:342-344` is a **use** site that reaches them through its own `db_utils.h` include at `:18`. FR-007 and the Edge Cases cite this header, not the Layer 3 one — a Layer 2 component following the old citation would have violated FR-002. |
| `OnePoleSmoother` (L1) | `dsp/include/krate/dsp/primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (:160), `setTarget(float)` (:170, sanitizes NaN/Inf), `getCurrentValue()` (:191), `process()` (:197, `[[nodiscard]]`), `advanceSamples(size_t)` (:243), `snapTo(float)` (:263); shared `kCompletionThreshold` (:55). For smoothed engine-level controls. **Not** for a FR-062 absorption ramp — no such ramp exists: FR-062 was resolved as "a mode switch changes the shared slew limiter's target, never the position", so `snapTo` has no absorption role here. |
| `ModulationSource` (L0) | `dsp/include/krate/dsp/core/modulation_source.h:31` | Pure virtuals `getCurrentValue() const noexcept` (:37), `getSourceRange() const noexcept` (:41). Relevant only as the interface `SplineTrajectory` and `BrownianDrift` already implement. Neither new component in this phase implements it — they are spectrum producers, not modulation sources (Non-Goals). |
| `pitch_utils` (L0) | `dsp/include/krate/dsp/core/pitch_utils.h:23,31` | `semitonesToRatio(float)` (:23, `std::pow(2, s/12)`) and `ratioToSemitones(float)` (:31, guarded at `ratio <= 0`) for the cent metrics in SC-004/SC-006/SC-016 (`cents = 100·ratioToSemitones(r/r_clean)`). `frequencyToCentsDeviation` (:175) is **not** usable — it measures deviation from the nearest 12-TET note centre and wraps to [-50, +50] (:169-170). **This header is amended by FR-072:** `centsToPitchRatio(float) noexcept` is added here as `semitonesToRatio(cents / 100.0f)`. Verified this session that no cents→ratio function exists in `Krate::DSP` today — the only tree-wide `centsToRatio` match is a *local variable* at `processors/multi_pitch_detector.h:96`, which is why the new name differs; and `HarmonicCloud`'s `detail::centsToDriftRatio` (`harmonic_cloud.h:105`, `kCentsToNatLog` at :106, 4-term Horner at :109-110) is Layer 3 and documented accurate only on [-50, +50] cents (:104). |
| `spectral_tilt` (L2) | `dsp/include/krate/dsp/processors/spectral_tilt.h:88,98-101` | **Convention only.** `class SpectralTilt` is a dual-shelf IIR, unsuitable for per-partial gains (the same finding Phase 2 recorded). Reused: the `kMinTilt = -12.0f` / `kMaxTilt = +12.0f` range convention adopted by FR-012 for `SpectralState::tiltDbPerOct`. |

Test-side helpers verified this session:

- `tests/test_helpers/render_fingerprint.h:46-59` — `kRenderCheckpoints = 32` (:46),
  `kSampleTolerance = 1.0e-4f` (:49), `kMetricTolerance = 1.0e-5` (:52), `struct RenderFingerprint` (:54)
  with `rms/peak/meanAbs/totalVariation/checkpoints` (:55-59). *(Re-verified by `grep -n` this session; an
  earlier draft cited :45/:48/:51/:53, one line low throughout.)*
- `tests/test_helpers/artifact_detection.h:38,99,130,186-193` — `ClickDetectorConfig` (:38),
  `ClickDetector` (:99), `detect(...)` (:130), within-frame threshold
  `mean(|dx|) + detectionThreshold·stddev(|dx|)` (:186-193). `LPCDetector` (:306) and
  `SpectralAnomalyDetector` (:534) are **not** used by any criterion here.
- `tests/test_helpers/signal_metrics.h:326-399` — `calculateSpectralFlatness(const float* signal, size_t n,
  float sampleRate)`. **Read this session and deliberately NOT used by any criterion.** The real signature
  takes **three** parameters over a **time-domain** signal (:326-330), chooses its own FFT size with a hard
  cap (`while (fftSize * 2 <= n && fftSize < 4096) fftSize *= 2;`, :337), applies its own **Hann** window
  (`Window::generateHann`, :348) and skips the DC bin (:359-367). SC-004 metric 3 needs a frequency-domain
  input, a Blackman-Harris window and a 65536-point transform — every element conflicts — and the
  4096-point cap gives 11.7 Hz bins at 48 kHz, too coarse for the few-Hz detunes stages 2–3 produce on low
  partials. SC-004 metric 3 therefore computes `exp(mean(log m_k)) / mean(m_k)` inline in the test. The
  library-side equivalent at `dsp/include/krate/dsp/primitives/spectral_utils.h:335` is not used either,
  for the same reason plus the no-production-header-for-a-test-metric rule.
- `tests/test_helpers/allocation_detector.h:26,75,106-138` — `AllocationDetector` (:26),
  `AllocationScope` (:75); the
  global operator replacements are commented out (**:108-136**, inside the commented
  `#ifdef ENABLE_ALLOCATION_TRACKING` block spanning :106-138), so `allocation_operator_overrides.h` must be
  present in exactly one TU per test binary (`dsp_systems_tests` already has it via
  `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`). See SC-011, which cites the identical range.
  *(An earlier draft cited this two different ways — ":108-134" in SC-011 and ":99-110" here, the latter
  being the section-header comment rather than the operators. Both are now :108-136, verified by `grep -n`
  this session.)*
- `tests/test_helpers/spectral_analysis.h` — FFT/window/peak/bin helpers; the source of the
  Blackman-Harris 65536-point magnitude spectra for SC-004 metric 3 and the SC-009 endpoint measurements,
  used the same way Phase 2 used them.

## New Components (ODR-swept this session)

ODR sweep executed this session (`grep -rn "class <Name>\|struct <Name>" dsp/ plugins/ tools/`, plus a
loose identifier sweep for each name across the same trees):

| Class / struct | Layer | Header path (new) | ODR sweep result |
|---|---|---|---|
| `SpectralState` | 2 | `dsp/include/krate/dsp/processors/spectral_state.h` | **Clear** — zero `class`/`struct` matches. One *unrelated* loose hit: a free function `writeSpectralState(BinaryWriter&, const SpectralPreset&)` at `tools/preset_generator.cpp:294`, in a standalone build tool, over Iterum's `SpectralPreset` (spec 033) — different name, different type, not in `Krate::DSP`, not linked into the library. No conflict. |
| `SpectralMorphEngine` | 3 | `dsp/include/krate/dsp/systems/spectral_morph_engine.h` | **Clear** — zero matches anywhere in `dsp/`, `plugins/`, `tools/`. |
| `EntropyProcessor` | 2 | `dsp/include/krate/dsp/processors/entropy_processor.h` | **Clear** — zero matches anywhere. |

Supporting new names swept and clear (zero matches of any kind in `dsp/`, `plugins/`, `tools/`, re-run this
session): `SpectralStateId`, `TravelMode`, `kStatePartials`, `kStateNameBytes`,
`kSpectralStateFormatVersion`, `makeFactoryState`, `normalizeSpectralState`, `serializeSpectralState`,
`deserializeSpectralState`, `EntropyStageWeights`, `kMaxBloomFraction`, `kMaxStateRatio`,
`kMinRatioSpacingCents`, `kMinRatioSpacingFactor`, `deriveStreamSeed`, `centsToPitchRatio`,
`SpectralStateLibrary`, `MorphTrajectory`, `EntropyStage`, `SpectralIdentity`, `SpectralTarget`,
`StateMorph`, `EntropyEngine`, `SpectralMorph`.

Names **added by this revision** and swept clear on the same trees at the same time (all class-scoped
constants, so the sweep is a belt-and-braces check rather than an ODR requirement — but roadmap line 485
asks for it before any new symbol): `kMinStateRatio`, `kMaxFillRatio`, `kMaxFillGrowth`,
`kFillSpacingCents`, `kFillSpacingFactor`, `kMaxOutputRatio`, `kOutputCentsSpan`, `kStateChangeFadeSec`,
`kMaxStates`, `kTargetRatioEpsilonCents`, `kTargetAmpEpsilon`, `kEndpointMagnitudeToleranceDb`,
`kRenderRmsFloorDbfs`, and the three FR-008 accessors `getAppliedScatterCents` / `getRawScatterDraw` /
`getScatterRedrawCount` (member functions on `EntropyProcessor`, forwarded by `SpectralMorphEngine`).

Names added by the **2026-07-26 clarification session** and swept clear on the same trees at the same time
(`grep` over `dsp/`, `plugins/`, `tools/` for each identifier — **zero matches of any kind**): the FR-008
zero-copy output accessors `getOutputRatios` / `getOutputAmplitudes` / `getOutputCount` and the matching
clean-array accessors `getCleanRatios` / `getCleanAmplitudes` (member functions on `SpectralMorphEngine`),
and the SC-004 metric-4 constant `kEntropyLevelSpreadDb` (test-local). No new class, struct or
namespace-scope symbol was introduced by that session.

**Correction to a previously-claimed-clear name.** `PartialTarget` was listed above as having zero matches;
that was wrong and is withdrawn. A loose identifier sweep returns **15 hits across 3 files** —
`HarmonicCloud::getPartialTargetAmplitude` (`harmonic_cloud.h:767`) plus references in
`dsp/tests/unit/systems/harmonic_cloud_test.cpp` (13) and `harmonic_cloud_spectral_test.cpp` (1). The bare
class name `PartialTarget` is still free (no `class`/`struct` declaration matches), so nothing is blocked —
but the name is a substring of an existing public accessor and is **dropped from the candidate list**
rather than kept, because a table whose only value is verification must not contain a false row.
(`SpectralState` remains correctly reported: exactly the two unrelated `tools/preset_generator.cpp` hits at
:294 and :490.)

Two of the new names above are **additions to existing Layer 0 headers**, not new headers, and are recorded
here because roadmap line 485 requires a sweep before any new namespace-scope symbol:
`deriveStreamSeed` → `core/random.h` (FR-006), `centsToPitchRatio` → `core/pitch_utils.h` (FR-072).

Near-name hazards that **exist** and must not be collided with or confused:

- `SpectralMorphFilter` (`spectral_morph_filter.h:67`) and its `enum class PhaseSource` (:54) — both in
  `Krate::DSP`. `PhaseSource` is why FR-061's enum is named `TravelMode`, not `MorphSource`.
- `MorphEngine` (`plugins/disrumpo/src/dsp/morph_engine.h:45`) — namespace `Disrumpo` (:28), so no ODR
  conflict, but the name `MorphEngine` is taken in this repo's vocabulary; `SpectralMorphEngine` is the
  qualified form and must not be abbreviated in code or docs.
- `HarmonicPhysics` with an `entropy_` control (`plugins/innexus/src/dsp/harmonic_physics.h:39,:356`) —
  namespace `Innexus` (:23). Innexus's "Entropy" is a *decay rate for unreinforced partial energy*
  (`:296-306`), a completely different meaning from Seraphis's disorder macro. No code conflict; recorded
  so the two are never described interchangeably in docs or commit messages.
- `HarmonicSnapshot` (`harmonic_snapshot.h:30`), `kMaxPartials = 96` (`harmonic_types.h:21`),
  `SpectralTilt` (`spectral_tilt.h:88`), `HarmonicCloud` (`harmonic_cloud.h:122`), plus the roadmap's
  standing list `ResonatorBank` / `ModalResonator` / `GranularEngine` (roadmap lines 96–97).

Any additional class, struct, free function or namespace-scope constant the plan introduces must be
re-swept before it is written (roadmap line 485). In particular, **no new namespace-scope partial-count
constant** may be added beside the existing `kMaxPartials = 96` (Clarifications C-8).

## Open Questions

**None.** The roadmap assigns exactly one Open Question to this phase — line 497, *"Spectral state
authoring: factory-only or user-morphable/savable states — Phase 3/9"* — and its Phase 3 half (the **DSP
capability**; the plugin surface is Phase 9's by the roadmap's own split) is **resolved as Clarifications
C-9**: assign + serialize only, with the authoring mutators deferred to Phase 9 by name and the capture
path rejected outright.

A previous revision left it here as an open question with a recommendation. That contradicted the
Clarifications preamble ("Binding decisions. Nothing here is deferred to implementation time") and, worse,
would have let a roadmap-assigned decision be settled by default in the plan rather than by decision. It is
now a binding clarification with the deferral recorded in Non-Goals and in the roadmap coverage table.

The six questions raised by the clarification scan (Q1–Q6: output-accessor shape, composition cadence,
post-`prepare` defaults, the normalizer's input, the `setSeed`/`reset()` contract, and per-note retrigger)
were put to the user and **all six are decided**. They are logged in Clarifications → *Session 2026-07-26*
and, more importantly, encoded in the FR/SC bodies — FR-004, FR-005, FR-006, FR-008, FR-044, FR-083,
FR-085, the new FR-086, Non-Goals, SC-001, SC-002 clause 5, SC-004 metrics 3–4, SC-009 and SC-010 — so a
reader never needs the log to know the behaviour. The same session decided the four internal-consistency
defects logged as GATE-FIX-1..4 (SC-002 clauses 3–4's unstated travel rate, the two stale "FR-062
absorption ramp" table references, FR-041's inverted `kFillSpacingCents` comparison, and C-4's overbroad
measurement claim); all four are encoded in SC-002, Existing Components, FR-041 and C-4 respectively, and
none relaxes a threshold.

No requirement or criterion in this document is deferred to implementation time; the one figure that must
be *measured* before the plan is written (SC-010's spike, RA-3) is named with its trigger and its
consequence. No other roadmap Open Question (lines 494–501) is assigned to this phase.

## Review notes

Recorded for issues raised in review that were **not** applied as suggested, with the reason. Everything
else in the review was applied in full.

- **"Move FR-081..FR-085 and SC-014 into Phase 7 and render through a test-local additive renderer"
  (FR-080 series / C-1) — partially rejected.** The factual half of the issue is accepted and acted on: the
  fourth alternative was missing from C-1 and is now weighed there explicitly, and the fact that this phase
  amends a COMPLETE component ahead of its scheduled phase is now recorded as an explicit roadmap amendment
  (RA-1) rather than being buried in a clarification. The *suggested resolution* — adopt the test-local
  renderer — is rejected on substance: the load-bearing DSP content of the FR-080 series is not "produce
  some rendered audio", it is the composition rules between an injected spectrum and the cloud's Gravity,
  Inharmonicity, Tilt, Richness-count, Mutation, drift and envelope stages (C-2, C-3, FR-082, FR-083). A
  test-local renderer answers none of those and would leave Phase 7 to discover them with no spec, no tests
  and no neutrality gate, while the rendered evidence roadmap line 192 asks for would be evidence about a
  test fixture rather than about the instrument. The issue's own alternative resolution ("record it as an
  explicit roadmap amendment") is the one taken. The cost concern behind the issue is separately resolved:
  SC-010 clause 2 now absorbs the injection cost inside Phase 2's existing 0.5% envelope rather than adding
  0.35% beside it, so the FR-080 series no longer moves the cloud's budget at all.
- **"Add `setTravelRateFromTempo(bpm, NoteValue)` to FR-061" (host-synced travel) — resolved the other way
  offered.** No such conversion belongs in a Layer 3 DSP component: it would drag a note-value enumeration
  and a tempo-ingestion point into a header whose entire behaviour is otherwise defined in Hz and seconds,
  and every other synced rate in this repo is computed on the plugin side. `setTravelRate(unitsPerSecond)`
  **is** the complete DSP surface — the conversion is one division — so nothing is missing and no later
  phase needs a further DSP change. The real gap the issue identified was the absent forward assignment,
  which is fixed: Non-Goals and the coverage table now name Phase 9 as the owner.
- **Roadmap file not edited.** RA-1, RA-2 and RA-3 state precisely what must change in
  `specs/Seraphis-roadmap.md` (two Phase 2 notes — the injection surface plus its budget coverage, and the
  `deriveSeed` → Layer 0 `deriveStreamSeed` rewrite; a Phase 7 line-299 revision; and, conditionally, a
  line-164 revision at the measured target-active figure).
  This revision was scoped to `spec.md`, so those roadmap edits are outstanding and must be made before
  Phase 7 is specced.

### Review notes — second review round

- **"`plugins/innexus/src/dsp/harmonic_physics.h`: `class HarmonicPhysics` is at :40, not :39" — REJECTED
  on fact.** Re-verified this session: `grep -n "class HarmonicPhysics"` returns **`39:class
  HarmonicPhysics {`**. The spec's existing citation of `:39` is correct and is left unchanged. Every other
  line-number correction in that issue (`render_fingerprint.h` → :46/:49/:52/:54-59;
  `allocation_detector.h` unified) was verified and applied.
- **"`HarmonicCloud` has no Nyquist gate … so those partials fold to arbitrary low frequencies rather than
  being silenced" (SC-009 clause 1) — resolution APPLIED, premise CORRECTED.** The suggested fix (scope the
  bracketing comparison to `i < min(A.numPartials, B.numPartials)`, and clamp the FR-041 continuation) is
  adopted in full — both were real defects. The stated *reason* is not: `HarmonicCloud::updateAntiAliasGain`
  (`harmonic_cloud.h:1229-1244`, read this session) sets `fade = 0.0f` whenever
  `fEff >= nyquist_` and ramps from `fadeStart_`, so an ultrasonic partial is **silenced**, not folded, and
  no folded energy contaminates neighbouring bins. (The same review's FR-041 issue states this correctly —
  "the cloud only zeroes ultrasonic partials via `updateAntiAliasGain`" — so the two issues disagreed with
  each other.) The scoping is still required, for the accurate reason now written into SC-009: a silenced
  or extrapolated partial has no magnitude to bracket. No new FR was added for a Nyquist gate, because one
  already exists in shipped Phase 2 code and adding a second would be a change to Phase 2 behaviour (a
  stated Non-Goal) with no benefit.
- **"Make `kMaxTravelRate` a journey-fraction rate … or raise it and re-derive FR-044" (FR-061 /
  Assumption 3) — first option taken, and FR-044's constants re-derived rather than the issue dodged.**
  `kMaxTravelRate` is now journeys/s and the shared slew cap is `kMaxTravelRate · (numStates − 1)`. This
  makes `kMaxAmpDeltaPerChunk` and `kMaxRatioDeltaCentsPerChunk` **larger** (0.02 → 0.025 and 32.0 → 125.0),
  which is recorded explicitly in FR-044 as a re-derivation from three changed pinned constants, not as a
  relaxation: the old travel cap was demonstrably wrong against roadmap lines 181–182, the old cents span
  assumed a `[1, 128]` output range that FR-041's fill and FR-012's missing lower bound both broke, and
  FR-047 is a contributor that previously did not exist. SC-002 clause 4 is the new criterion that fails if
  the limiter ever saturates again.
- **"FR-042 / SC-001 clause 1 … either (a) add an absorption crossfade or (b) restate the criterion
  honestly" — option (a) taken.** Option (b) would have dropped the per-chunk amplitude-delta assertion
  across `setState` / `setStateCount`, which is a weakening: roadmap line 190 asks for no clicks and a
  bounded per-chunk amplitude delta, and Phase 7's macro and preset layers will swap states while a voice
  sounds. FR-047 pins a `kStateChangeFadeSec = 2.0` crossfade with its own FR-044 row and its own
  arithmetic instead.
- **"FR-062 … state that the switch is absorbed by the same `kMaxTravelRate` slew limiter both modes
  already share (in which case … no new term is needed), or pin an absorption ramp" — first option taken.**
  It is both simpler and truer to the design: the engine holds one position state and `TravelMode` selects
  only what feeds the limiter's target. FR-062 no longer arms an undocumented ramp and adds no FR-044 row.
