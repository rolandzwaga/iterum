# Feature Specification: Seraphis Phase 9 — Full Parameter Surface & State

**Spec slug:** `seraphis-phase9-parameters`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 9 (roadmap lines 453–460, post-FR-058 amendment)
**Depends on:** Phase 8 (`plugins/seraphis/`, ✅ COMPLETE) and Phases 1–7 (`SeraphisVoice`, `SeraphisEngine`, `SeraphisMacroMatrix`, `AetherReverb`, `SpectralState`, ✅ COMPLETE)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-07-31

---

## Overview

Phase 8 shipped a plugin with **eight** parameters: three globals and five macros that are deliberately
*inert* — `plugins/seraphis/src/parameters/macro_params.h:5-9` states that no Phase 8 file may read
`MacroParams` into `SeraphisMacroMatrix`. Phase 9 is the inversion of that sentence and the completion of
the surface: every parameter the Phase 1–7 engine actually exposes becomes a registered, automatable,
denormalized, persisted VST3 parameter, and the five macros are wired to the matrix that already exists.

The roadmap's sentence for this phase (lines 444–447) names six deliverables: *"Every engine parameter
registered, denormalized in `processParameterChanges()`, atomics in processor, state save/load with
versioning, spectral-state serialization (Phase 3 round-trip), macro system wired. Editor-lifecycle harness
enrollment … Pluginval + full state round-trip tests."*

Two facts read out of the headers this session make this more than a transcription job, and they are what
this spec principally decides:

1. **There is no public mutable path from `SeraphisEngine` to its voices.** `getVoice()` is `const`
   (`dsp/include/krate/dsp/systems/seraphis_engine.h:696-698`), `voices_` is private (`private:` opens at
   `:734`), and the single non-const route is `friend class SeraphisMacroMatrix` (`:738`). A plugin cannot
   set Body Material or Atmosphere Density on a voice today at all.
2. **`SeraphisMacroMatrix::apply()` unconditionally overwrites nineteen voice targets every slice**
   (`seraphis_macro_matrix.h:626-659`), from a per-target `base` that is a **compile-time constant in
   `kRows`** (`:180`, bases at `:190`, `:196`, `:202`, …). Phase 8's `Processor::renderSlice` calls it every
   slice (`plugins/seraphis/src/processor/processor.cpp:623`). Registering a "Cloud Richness" parameter and
   writing it into a voice would therefore be erased within one slice by `base = 0.60f + bloom·0.40f`.

Phase 9 resolves both with two small, surgical `dsp/` additions — a voice-parameter broadcast POD on
`SeraphisEngine`, and per-target **base overrides** on `SeraphisMacroMatrix` so a deep parameter *is* the
base and a macro is a signed offset from it. No DSP algorithm changes; no new DSP behaviour at the shipped
defaults, which is asserted as a negative control (SC-002).

---

## Clarifications

### Session 2026-08-01

Every question the clarification scan raised was ruled on by the phase owner in this session, together
with the two **Open Questions** the roadmap deferred here (now *Resolved Questions* RQ-1 and RQ-2) and two confirmations of decisions
this spec had already taken. **Nothing in this spec is contingent on an unanswered question any more**;
the Conventions / FR / SC body below states the decided behaviour directly, and *Resolved Questions*
records the two roadmap-deferred rulings. This log is a record, not a source of behaviour: a reader must
never need it to know what the spec requires.

- **Q1 — What is the push cadence for a class-(b) (processor-side smoothed) parameter, and how does
  SC-007 count it?** *Decision:* the **settling push**. While any class-(b) smoother is un-settled, its
  push (`applyVoiceParams` / `setTargetBase`) runs **until that smoother settles** — on the engine's
  **absolute 64-sample control-chunk grid**, per amendment A1 of 2026-08-01, which withdrew this entry's
  original *"once per block"* for the reasons FR-042 amendment 1 records. SC-007's rows become "**+1**
  for a class-(a) change, **+1 … +`N_chunk`** for a class-(b) change", with `N_chunk` a **push** count
  and `N_block` the separate wall-clock block count SC-003 renders against; SC-008's steady-state arm is
  measured with every smoother settled.
  The **class-(b) time constant must be fixed explicitly** by the plan — one number, or a per-ID column
  of `kContinuityMechanism[]` — because SC-005's 1.5× reference bound is sensitive to it.
  *Encoded in:* C-3, FR-042, FR-059, SC-005, SC-007, SC-008.

  **Amendment A10, 2026-08-01 — the PER-ID COLUMN is the form that ships, and one Phase 7 constant
  moved with it.** The plan chose the single-number form (`kParamSmoothMs = 20 ms`) and recorded that
  the one-directional remedy for a surviving step is to lengthen it. SC-005 was then run and **two rows
  breached the `1.5 ×` bound**: **ID 1215** at 1.817 × and **ID 1** at 2.651 ×. Both remedies are the
  ones this spec already mandates — never an exemption, never a looser bound:
  - **ID 1215 (and by construction 1216 and the five macros that reach them)** takes a second constant,
    `kAetherDepthSmoothMs = 300 ms = AetherReverb::kSizeSmoothingMs`. Lengthening the *single* number
    does not work: the sweep is non-monotonic below the knee (20 → 1.817, 60 → 2.297, 100 → 1.847,
    200 → 1.172, 300 → 1.126, 500 → 1.093) because the depth scales an exponentially-mapped **delay
    read length**, so the discontinuity saturates until the per-chunk stair falls under one sample.
    The body rows (801, 802) keep 20 ms and their `N_chunk = 28` / `N_block = 4`. Consequently
    `N_chunk`/`N_block` are **per-family** in SC-007, and `kContinuityMechanism[]` carries a
    `smoothMs` column with a gate.
  - **ID 1** is class (a) and its smoother lives in `dsp/`, so there is no plugin-side remedy:
    `SeraphisEngine::kSumGainSmoothMs` moves **20 ms → 100 ms**, admitted under FR-071.
  - **SC-005's positive control (b) moves from ID 801 to ID 1215.** The criterion says "one class-(b)
    smoother" and does not name one; 801 was the wrong choice, because `continuous_body.h:2545-2558`
    states that a resonance retune steps a decay slope and an instantaneous frequency and that neither
    is an output discontinuity. Measured, 801 scores 1.044 × smoothed and **1.045 × snapped** — the
    control was structurally incapable of *passing*. On 1215 it is 1.126 × smoothed against 2.215 ×
    snapped, a **5.73 ×** separation of the raw statistics.
- **Q2 — Must `setState()` arriving AFTER `setupProcessing()` re-push every surface?** *Decision:* **yes,
  through one shared `pushAllSurfaces()` helper** reached from both `setupProcessing()` (directly, audio
  thread stopped) and `setState()` (via the release-store request amendment A4 mandates): it bumps `voiceParamGeneration_` and `aetherParamGeneration_`, force-pushes the 27 `MB` bases and the
  four `ENG` values (resetting the `lastPushed*` trackers to a sentinel), and sets
  `spectralStatesPending_`. A **new success criterion** loads a non-default preset into a *prepared*
  processor, renders one block and asserts every route's read-back.
  *Encoded in:* C-3, FR-047, FR-091, **SC-023**.
- **Q3 — When morph sync is ON, how often is the derived travel rate recomputed and pushed?**
  *Decision:* **every block**. `process()` recomputes the synced travel rate from `processContext` and
  dirties `voiceParamGeneration_` when it moves. SC-007's quiescent arm is reworded to "no parameter
  change **and constant tempo**". The plan must state the **tempo sample point** (per `process()` vs per
  slice) and the **epsilon** that counts as "changed".
  *Encoded in:* C-3, C-7, FR-042, FR-056, SC-007, SC-018.
- **Q4 — RQ-1: do the three `SpectralState` authoring mutators ship in Phase 9?** *Decision:* **no**.
  `setPartial`, `blendStates` and `tiltState` do **not** ship in Phase 9. **Phase 11** — which owns the
  per-partial editing surface that is their only consumer — inherits both the mutators **and** Phase 3's
  validity-preservation criterion, written into the roadmap's Phase 11 entry in the same change. The
  state format is unaffected: C-8 already persists the full 541-byte payload per slot.
  *Encoded in:* *Non-goals*, FR-058 cl. 4, *Resolved Questions* RQ-1.
- **Q5 — RQ-2: does Seraphis ship per-note expression (MPE / poly-aftertouch), and in which phase?**
  *Decision:* **it ships, but in a new named phase — not Phase 9.** That phase owns **both** the
  `SeraphisVoice` per-voice expression inputs and `INoteExpressionController`. Roadmap Open Question 5 is
  **moved to that phase** (not struck), and the controller-FUID host-cache hazard is **accepted and
  recorded**.
  *Encoded in:* FR-064, FR-058 cl. 5, *Resolved Questions* RQ-2.
- **Q6 — Over which voice indices must SC-003's `MB-voice` read-back hold?** *Decision:* the test **pins
  polyphony to 16** for the `MB-voice` rows, so `kMaxVoices` and `getPolyphony()` coincide and the shared
  `for every i < kMaxVoices` wording stands unchanged. C-2's recorded `apply()` residue stays exactly as
  it is.
  *Encoded in:* SC-003 `MB-voice` row.
- **Q7 — What are `kSeedId`'s sixteen entries?** *Decision:* a **curated, checked-in table of 16
  well-separated 32-bit seed constants**, index 0 pinned to `1u` (preserving the Phase 8 default and
  SC-002's negative control), labelled `"Seed 1" … "Seed 16"`. SC-020 clause 2's measured spread gate
  becomes a **property of the checked-in table**: a small spread is remedied by re-picking constants,
  never by lowering the gate.
  *Encoded in:* **C-10**, C-6 (ID 3), FR-015, SC-020.
- **Q8 — What is the concurrency contract for `spectralSlots_`?** *Decision:* **a staging ring plus an
  index handoff** (three buffers and two `std::atomic<int>`s after amendment A5; the ruling as first
  recorded said one buffer and one `std::atomic<bool>`, which has no writer-side interlock).
  Message-thread writes land in a separate staging array consumed at the
  top of `process()` (the same shape `spectralStatesPending_` already has). ~6.3 KiB extra member, no
  lock, no allocation. FR-041b's "no new synchronisation primitive is introduced" becomes
  **unconditionally true** rather than conditional on host behaviour.
  *Encoded in:* FR-041b, FR-091.
- **C-1 confirmation — the `SeraphisMacroMatrix::setTargetBase()` / `resetTargetBases()` /
  `getTargetBase()` base-override approach.** *Decision:* **CONFIRMED as specced.** No change to C-1,
  FR-003 or FR-004.
- **FR-070 confirmation — is the five-forwarder line right?** *Decision:* **EXPANDED — the five-forwarder
  line is rejected.** Phase 9 must **also** forward and register the **ATMOSPHERE SET**
  (`AtmosphereEngine` jitter, position, positionSpread, pitch, pitchSpread, grainEnvelope — 6 scalar
  parameters) and the **BODY SET** (`ContinuousBody` inputAgc and resonatorBypass — 2 toggles; the plan
  must explicitly check `inputAgc`'s interaction with the FR-033a excitation-comp estimator shipped in
  `ee408854`). The **CLOUD PER-PARTIAL SET** (`setSpectralTarget` / `setPartialPosition` /
  `setPartialMask`) stays **excluded** — per-partial arrays are Phase 11's editing surface. Net:
  **13** new `SeraphisVoice` forwarders instead of 5, and the parameter surface grows **83 → 91**, with
  matching IDs in the correct bands, state-format-version-2 fields **from the start** (no version 3),
  SC-003 read-back rows, SC-022 defaults and uidesc control-tags.
  *Encoded in:* *Scope*, *Non-goals* (inclusion criterion + candidate table), *New components*, C-2, C-6,
  C-8, C-9, FR-006, FR-010, FR-013, FR-014, FR-015, FR-060, FR-070, FR-072, FR-100, SC-001, SC-003,
  SC-005, SC-006, SC-009, SC-010, SC-017, SC-022.
- **C-5 confirmation — where do the voice-envelope parameters live?** *Decision:* **CONFIRMED** — IDs
  **700–799**, inside the Life Modulators band (600–799). No change to C-5 or C-6.
- **Editorial reconciliation (no new decision).** Six stale pre-expansion figures that the FR-070
  encoding missed were corrected in the same session, before the plan was written: FR-071's *"ten"*
  `const` accessors → **twelve** (agreeing with FR-072's twelve-on-`ContinuousBody` + one-on-
  `SeraphisVoice` = thirteen); SC-003's test line *"75-row table"* → **83-row** (matching the criterion's
  own opening); FR-059's *"77 in-scope IDs"* → **85** (matching SC-005's own arithmetic, 91 − `kSeedId`
  − 5 `CFG`); SC-023 clause 2 **decoupled** from SC-009's CPU table onto its own all-non-default value
  table (no pinned rows, no `n/a` rows, the eight processor-local IDs participating as persisted state);
  SC-003's `VP` row stripped of a stale *"minus the 2 carved out below"* (all four carve-outs are on other
  routes); and SC-003's `ENG` row corrected — ID 0 is processor-local per C-6, not `ENG` — with the
  route-row arithmetic now summing explicitly to the 83 new IDs.
  *Encoded in:* FR-059, FR-071, SC-003 (`VP` row, `ENG` row, new arithmetic paragraph, test line),
  SC-023 cl. 2.
- **Editorial reconciliation, round 2 (no new decision).** Three further internal inconsistencies were
  ruled on in the same session, again by pointing each at the artefact that was already authoritative:
  (i) **C-4 vs SC-002** — C-4 was the stale statement. Its *"within `render_fingerprint.h` tolerance of a
  freshly instantiated Phase 8 plugin"* wording is **removed** and replaced by SC-002's own gate verbatim:
  a **same-binary** comparison (Arm B configures a `SeraphisEngine` + `AetherReverb` pair directly with the
  Phase 8 shipped defaults **in the same translation unit**), per-sample `maxAbsDiff` over all samples of
  both channels **≤ 1.0e-5**, with `compareFingerprints` **secondary and warn-only, never gating** and a
  checked-in fingerprint reference **forbidden**. SC-002 governs if the two ever diverge again.
  (ii) **FR-063's verification pointer** — *"Verified by SC-012"* was wrong (SC-012 is the spectral-state
  serialization round-trip); it now reads **SC-014**, the `getParameterInfo`-vs-checked-in-table criterion
  for the eight Phase 8 IDs, and the missing **FR-063 → SC-014** Traceability row was added so the pair is
  cross-checked like every other.
  (iii) **The missing re-prepare assertion** — the *Sample-rate changes* edge case cited a *"dedicated
  re-prepare assertion"* that no criterion created. It is now **created**, as **SC-023 clause 7**: after
  rendering at 44.1 kHz with non-default values on every route, `setupProcessing()` at 96 kHz must be shown
  to have re-run `prepare()` on engine and reverb **and** to have re-delivered every route through
  `pushAllSurfaces()`, with clause 4's per-route read-backs repeated verbatim. It adds no introspection: it
  uses only the FR-072 accessors and existing getters.
  *Encoded in:* C-4, FR-063, **SC-023 cl. 7**, *Edge cases* → *Sample-rate changes*, *Traceability* (three
  rows).

---

## Scope

**In scope**

1. **83 new registered parameters** across seven sections (the full table in *Conventions*), taking the
   plugin from 8 to **91**. Every one is reachable through an existing public setter on `SeraphisVoice`,
   `SeraphisEngine` or `AetherReverb`, or through one of the **thirteen enumerated new `SeraphisVoice`
   forwarders** in FR-070.
2. **Six new parameter packs** in `plugins/seraphis/src/parameters/` following the Ruinae six-function
   contract already used by `global_params.h` / `macro_params.h`, plus `dropdown_mappings.h`.
3. **Macro wiring** — `MacroParams` → `SeraphisMacroMatrix::setMacros`, with deep parameters supplying the
   matrix's per-target bases so the two surfaces compose instead of fighting.
4. **Three `dsp/` additions** (Layer 3, additive only): the **voice-parameter broadcast**
   (`SeraphisVoiceParams` + `SeraphisEngine::applyVoiceParams()`, FR-001/FR-002), the
   **spectral-state fan-out** (`SeraphisEngine::applySpectralStates()`, FR-005), and the
   **macro base overrides** (`SeraphisMacroMatrix::setTargetBase()` / `resetTargetBases()` /
   `getTargetBase()`, FR-003/FR-004).
5. **Thirteen new `SeraphisVoice` forwarders** (FR-070), each admitted by the inclusion criteria
   stated under *Non-goals within Phase 9 itself* below and applied there to all sixteen candidates.
6. **State format version 2** with byte-exact **`SpectralState` serialization** (4 slots × 541 B, Phase 3's
   `serializeSpectralState` / `deserializeSpectralState`) and a v1 → v2 migration path.
7. **Controller** parity: registration, display formatting, `setComponentState` for the whole surface.
8. **`editor.uidesc` `<control-tags>`** entries for all 91 IDs (metadata only — no new views).
9. Editor-lifecycle harness kept green at the enlarged surface; pluginval strictness 5; full state
   round-trip tests.

**Non-goals — explicitly owned by later phases**

| Deferred to | What |
|---|---|
| Phase 10 (`seraphis-phase10-effects`) | The 1400+ Effects band: spectral freeze (global), spectral delay, tape-saturation *controls* beyond Phase 8's Soft Limit toggle, stereo wandering. Send/ordering topology. `getTailSamples()` / idle reporting, which Phase 8 deferred to the phase that owns the effects roster. |
| Phase 11 (`seraphis-phase11-ui`) | The real `editor.uidesc`: macro-first layout, panels, the cloud-view visualization, DataExchange piggyback, sub-controllers, custom views. Phase 9 adds `<control-tags>` **only** and leaves the eight-control placeholder template as it stands. **Also inherited (2026-08-01 ruling, Q4):** the three `SpectralState` authoring mutators (`setPartial`, `blendStates`, `tiltState`) **and** Phase 3's validity-preservation criterion over them, together with the per-partial editing surface that is their only consumer — and, with them, `HarmonicCloud::setSpectralTarget` / `setPartialPosition` / `setPartialMask`. |
| A **new named phase** (per-note expression) | Per-note expression ships (2026-08-01 ruling, Q5), but not here: that phase owns **both** the `SeraphisVoice` per-voice expression inputs and `INoteExpressionController`. See *Resolved Questions* RQ-2 and FR-058 clause 5. |
| Phase 12 (`seraphis-phase12-presets-release`) | Factory presets, the final preset category set, the preset validation harness, the release gate. |

**Non-goals within Phase 9 itself:**

- **No new DSP algorithm, and no change to any existing DSP behaviour at the shipped defaults.** The two
  `dsp/` additions are additive API only; SC-002 is the standing negative control.
- **No `SpectralState` authoring mutators** (`setPartial`, `blendStates`, `tiltState`), which Phase 3
  assigned to Phase 9 by name (`specs/seraphis-phase3-spectral-morph/spec.md:404-409`). **Decided
  2026-08-01 (Q4): they do not ship in Phase 9, and Phase 11 inherits them by name** — together with
  Phase 3's obligation that the mutators *"must preserve FR-012's validity invariants"*, whose criterion
  Phase 3 assigned to *"Phase 9's criterion to state"* and which this phase therefore states as
  **Phase 11's**. FR-058 clause 4 makes that inheritance land in `specs/Seraphis-roadmap.md`'s Phase 11
  entry in the same change; a deferral with no named owner is not a resolution. Phase 9 ships the
  **serialization** half — the state stream carries the full 541-byte payload per slot, so the format
  costs no version when Phase 11 makes states user-editable — and the **selection** half (a factory-state
  dropdown per slot). **The roadmap's Open Question 2 (line 564) is a separate matter and is already
  closed** — Phase 3 resolved it (`specs/seraphis-phase3-spectral-morph/spec.md:207-208`); Phase 9
  implements that answer and strikes the line (FR-058 clause 3).
- **No component setters that `SeraphisVoice` does not forward**, beyond FR-070's thirteen.

  **The inclusion criteria, stated once and applied to all sixteen candidates.** A component setter that
  Phase 7 did not forward is admitted into Phase 9 if **any** of the following holds:
  (a) the roadmap names the control itself;
  (b) it is the *first-class shape or range control* of a component the roadmap names as part of the
  Seraphis signal path, and that component has **no other plugin-visible surface** through which its
  character can be set;
  (c) **the phase owner ruled it in.**
  Clause (b) is stated explicitly because two of the first five admissions rest on it and a criterion of
  "named by a roadmap line" alone would not admit them: a roadmap that says *"Pitch drift per grain
  (`BrownianDrift` again)"* (line 245) and *"travel driven by `SplineTrajectory`"* (line 186) has named
  the modulator and left its one shaping control unreachable, which is not a decision the roadmap took —
  it is a gap. Clause (b) is deliberately narrow: it admits the control that sets the named modulator's
  *character* (smoothness, range, waypoint interval), never a diagnostic seam, an override, or an
  alternative entry point to something already reachable.

  **Clause (c) is a recorded ruling, not an escape hatch, and it was exercised exactly once** — in the
  2026-08-01 clarification session, over the eight setters of the **ATMOSPHERE SET** and the **BODY SET**
  below. The ruling's ground is stated so it is auditable: the six atmosphere controls are the grain
  stream's *placement and transposition* surface, which clause (b) declined only because the roadmap
  names neither, and the two body toggles were declined as "diagnostic seams" — a reading the owner
  overruled, because both are user-meaningful *character* switches (`setInputAgcEnabled` selects between
  automatic level compensation and a fixed-gain body; `setResonatorBypass` selects the direct
  cloud-drive path). The **CLOUD PER-PARTIAL SET** was **not** ruled in and stays out: per-partial arrays
  are Phase 11's editing surface, not a scalar parameter.

  **Applied to all sixteen candidates:**

  | Candidate setter | Verdict | Ground |
  |---|---|---|
  | `HarmonicCloud::setDriftSmoothness` (`harmonic_cloud.h:513`) | **IN** (FR-070 #1) | (a) — roadmap line 113 names *"smoothness control"* on `BrownianDrift`, and line 148 puts per-partial drift in the cloud |
  | `HarmonicCloud::setEnvelopeOffsetSpread` (`:580`) | **IN** (FR-070 #2) | (a) — roadmap line 148, *"individual attack/decay offsets"* |
  | `AtmosphereEngine::setDriftSmoothness` (`atmosphere_engine.h:844`) | **IN** (FR-070 #3) | (b) — roadmap line 245 names per-grain `BrownianDrift`; smoothness is that modulator's only character control and nothing else in the atmosphere band reaches it |
  | `AtmosphereEngine::setDriftRangeSemitones` (`:852`) | **IN** (FR-070 #4) | (a) — roadmap line 245, *"Pitch drift per grain"*: the range **is** the drift depth in pitch |
  | `SpectralMorphEngine::setWaypointInterval` (`spectral_morph_engine.h:385`) | **IN** (FR-070 #5) | (b) — roadmap line 186 names `SplineTrajectory` as the travel driver; the waypoint interval is that generator's only shape control and `setTravelRate` does not reach it |
  | `AtmosphereEngine::setJitter` (`atmosphere_engine.h:800`) | **IN** (FR-070 #6) | (c) — ATMOSPHERE SET, 2026-08-01 owner ruling |
  | `AtmosphereEngine::setPositionSeconds` (`:807`) | **IN** (FR-070 #7) | (c) — ATMOSPHERE SET |
  | `AtmosphereEngine::setPositionSpread` (`:815`) | **IN** (FR-070 #8) | (c) — ATMOSPHERE SET |
  | `AtmosphereEngine::setPitchSemitones` (`:822`) | **IN** (FR-070 #9) | (c) — ATMOSPHERE SET (a **fixed** transposition, distinct from #4's *drift range*; both ship) |
  | `AtmosphereEngine::setPitchSpread` (`:830`) | **IN** (FR-070 #10) | (c) — ATMOSPHERE SET |
  | `AtmosphereEngine::setGrainEnvelope` (`:959`) | **IN** (FR-070 #11) | (c) — ATMOSPHERE SET |
  | `ContinuousBody::setInputAgcEnabled` (`continuous_body.h:1276`) | **IN** (FR-070 #12) | (c) — BODY SET; the "diagnostic/safety seam" reading was overruled. **Interacts with FR-033a** — see FR-070 #12 |
  | `ContinuousBody::setResonatorBypass` (`:1300`) | **IN** (FR-070 #13) | (c) — BODY SET; the "test/diagnostic seam" reading was overruled |
  | `HarmonicCloud::setSpectralTarget` (`harmonic_cloud.h:769`) | OUT | an alternative entry point to a spectrum the morph engine already owns (IDs 408–412); CLOUD PER-PARTIAL SET, explicitly **not** ruled in |
  | `HarmonicCloud::setPartialPosition` (`:1069`) | OUT | per-partial authoring — Phase 11's editing surface (Q4) |
  | `HarmonicCloud::setPartialMask` (`:1084`) | OUT | as above |

  Phase 7 deliberately did not forward any of them (`seraphis_voice.h:638` calls the facade
  *"one-to-one forwarders, no added clamping"*); FR-070 adds exactly the **thirteen** that clear a
  criterion. The three that remain out are exactly the CLOUD PER-PARTIAL SET.
- **No parameter-type change at any of Phase 8's eight registered IDs.** `plugin_ids.h:71-76` freezes them;
  the project rule is absolute.
- **No new FUID, no new AU identity, no bus change.**

---

## Existing components (verified this session)

Every signature below was read from the file this session; line numbers are as they stand on
`feat/seraphis-phase1-life-modulators` at 2026-07-31.

| Component | Header / file | What Phase 9 reuses (verified signature) |
|---|---|---|
| `SeraphisVoice` — cloud facade | `dsp/include/krate/dsp/systems/seraphis_voice.h:641-650` | `void setRichness(float)`, `setInharmonicity(float)`, `setSpectralTiltDb(float)`, `setMutation(float)`, `setSpectralGravity(float)`, `setDriftDepthCents(float)`, `setStereoSpread(float)`, `setAttackTimeSec(float)`, `setDecayTimeSec(float)` — all `noexcept`, all forwarding to `cloud_` with **no added clamping** (banner at `:638`) |
| `SeraphisVoice` — morph facade | same file, `:652-669` | `void setEntropy(float)`, `setBloom(float)`, `setTargetPosition(float)`, `setTravelRate(float journeysPerSecond)`, `void setTravelMode(SpectralMorphEngine::TravelMode)`, `[[nodiscard]] SpectralMorphEngine::TravelMode getTravelMode() const` |
| `SeraphisVoice` — body facade | same file, `:671-683` | `void setMaterial(ContinuousBody::BodyMaterial)`, `setResonance(float)`, `setDamping(float)`, `setKeyTracking(float)`, `setDrive(float)`, `setMix(float)`, `setCloudMix(float)`, `setCloudDecaySec(float)`, `setCloudSize(float)`, `setCloudDamping(float)`, `setWidth(float)` |
| `SeraphisVoice` — atmosphere facade | same file, `:685-693` | `void setLevel(float)`, `setBlur(float)`, `setDensity(float grainsPerSecond)`, `setGrainSeconds(float)`, `setDriftDepth(float)`, `setPanSpread(float)`, `setDecorrelation(float)`, `setFreezeMix(float)` |
| `SeraphisVoice` — spatial | same file, `:614-635` | `void setSpatialDepth(float normalized)`, `setSpatialRate(float hz)`, `setSpatialCoupling(float normalized)`, `setSpatialGrowth(float)`, `void setVoiceWidthBasePercent(float pct)` (clamps to `[kMinVoiceWidthPct 50, kMaxVoiceWidthPct 150]`, `:629`), `[[nodiscard]] float getVoiceWidthBasePercent() const` (`:631`) |
| `SeraphisVoice` — envelope | same file, `:567-608` | `void setEnvelopeMode(EnvelopeMode)` (`enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }`, `:135`), `void setGrowthDurationSeconds(float)` (`:580`), `void setEnvelopeStageTimeMs(int stage, float ms)` (`:587`), `void setEnvelopeReleaseMs(float ms)` (`:594`), plus the four getters `:599-608` |
| `SeraphisVoice` — configure-time-gated | same file, `:699-722` | `void setSpectralState(int slot, const SpectralState&)` and `void setSpectralStateCount(int n)`, **both gated on `!hasSounded_ \|\| isFinished()`** and incrementing a rejection counter otherwise; `[[nodiscard]] std::uint32_t getRejectedConfigureTimeCallCount() const` (`:720`). The gate exists because `SpectralMorphEngine` documents "`setState()` and `setStateCount()` are NOT to be called while the consumer is sounding" (`spectral_morph_engine.h:198-207`). |
| `SeraphisVoice` — shipped defaults | same file, `:284-361` | `prepare()` step 6/7. The complete FR-019/FR-020 default table this spec's registered defaults must equal exactly: richness `0.60` (`:290`), inharmonicity `0.030` (`:291`), tilt `0.0` (`:292`), mutation `0.25` (`:293`), gravity `0.20` (`:294`), drift `0.0` (`:295`), spread `0.35` (`:296`), attack `0.05` (`:297`), decay `0.5` (`:298`); entropy `0.20` (`:301`), bloom `0.0` (`:302`), travel rate `kMinTravelRate` (`:303`); material `Glass` (`:306`), resonance `0.7` (`:307`), damping `0.25` (`:308`), key-track `1.0` (`:309`), drive `1.0` (`:310`), mix `1.0` (`:311`), cloudMix `0.25` (`:313`), cloudDecay `4.0` (`:314`), cloudSize `1.0` (`:315`), cloudDamping `0.3` (`:316`), width `1.0` (`:317`); atmos level `0.5` (`:320`), blur `0.0` (`:321`), density `4.0` (`:322`), grain `4.0` (`:323`), driftDepth `0.3` (`:324`), pan `0.7` (`:325`), decorrelation `0.5` (`:326`), freezeMix `0.0` (`:327`); orbit depth `0.35` (`:330`), rate `0.1` (`:331`), coupling `0.0` (`:332`), growth `0.0` (`:333`), width base `100.0` (`:334`); envelope `Standard` (`:341`), stage 0 `{1.0, 2000 ms}` (`:355`), stage 1 `{0.7, 4000 ms}` (`:356`), release `8000 ms` (`:359`), growth duration `10.0 s` (`:364`) |
| `SeraphisEngine` | `dsp/include/krate/dsp/systems/seraphis_engine.h` | `void setPolyphony(std::size_t) noexcept` (`:321`); `void setSeed(std::uint32_t) noexcept` (`:353`, re-derives every slot seed); `void setAtmosphereFreeze(bool) noexcept` (`:551`); `[[nodiscard]] bool getAtmosphereFreeze() const` (`:562`); `void setOutputSaturation(float) noexcept` (`:566`); `[[nodiscard]] std::size_t getPolyphony() const` (`:665`); `[[nodiscard]] const SeraphisVoice& getVoice(std::size_t) const noexcept` (`:696`) — **const**; `static constexpr std::size_t kMaxVoices = 16` (`:130`), `kControlChunkSamples = 64` (`:132`), `kMaxBlockSamples = 2048` (`:134`) |
| `SeraphisEngine` — the access wall | same file, `:734-740` | `private:` at `:734`; `friend class SeraphisMacroMatrix;` at `:738` with the comment *"the macro matrix needs NON-const voice access, which FR-085's const `getVoice()` deliberately is not"*. **This is the fact that forces FR-001/FR-002** (the voice-parameter broadcast) **and FR-005** (the spectral-state fan-out): there is no other route from the plugin to a mutable voice. |
| `SeraphisMacroMatrix` | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h:154` | `void setMacro(SeraphisMacro, float) noexcept` (`:554`, clamps `[0,1]`, non-finite → `neutralFor()`); `void setMacros(const SeraphisMacroValues&) noexcept` (`:599`); `[[nodiscard]] SeraphisMacroValues getMacros() const` (`:607`); `void apply(SeraphisEngine&) const noexcept` (`:623`); `[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept` (`:667`); `[[nodiscard]] static constexpr float neutralFor(SeraphisMacro) noexcept` (`:548`) |
| `SeraphisMacroMatrix` — the base mechanism | same file, `:130-146`, `:180`, `:712-731` | `struct SeraphisMacroRow { SeraphisMacro macro; SeraphisMacroTargetOwner owner; SeraphisMacroTarget target; float base; float amount; ModCurve curve; }` (`:131-146`); `static constexpr std::array<SeraphisMacroRow, kNumRows> kRows` with `kNumRows = 30` (`:163`, `:180`); `evaluateAll()` seeds `value[i] = row.base` on first sighting then adds one `contributionOf(row)` per row (`:719-731`). `static_assert(everyRowSharesOneBasePerTarget(kRows))` (`:752`) guarantees one base per target — which is exactly what makes a **per-target** override well-defined. |
| `SeraphisMacroTarget` | same file, `:56-88` | The 27-value enum (+`Count`): 19 Voice-owned (`CloudInharmonicity`, `CloudMutation`, `CloudSpectralGravity`, `CloudRichness`, `CloudSpectralTiltDb`, `CloudStereoSpread`, `CloudAttackTimeSec`, `CloudDriftDepthCents`, `MorphEntropy`, `MorphTargetPosition`, `BodyDamping`, `AtmosLevel`, `AtmosBlur`, `AtmosDriftDepth`, `SpatialDepth`, `VoiceWidth`, `EnvStage0Ms`, `EnvStage1Ms`, `EnvReleaseMs`) and 8 Aether-owned (`AetherMix`, `AetherSize`, `AetherWidth`, `AetherShimmerOctaveSend`, `AetherShimmerFifthSend`, `AetherBloomSend`, `AetherSizeBreathDepth`, `AetherDimensionalityTideDepth`) |
| `SeraphisMacroValues` | same file, `:122-128` | `float dream = 0.0f; bloom = 0.0f; dissolve = 0.0f; gravity = 0.5f; entropy = 0.0f;` — **Phase 7's FR-060 neutral** (`specs/seraphis-phase7-voice-engine/spec.md:1204-1210`; the arithmetic is at `seraphis_macro_matrix.h:120-128`, `:548`), already Phase 8's registered defaults. *Not* this spec's FR-060, which is a controller-registration requirement. |
| `SeraphisAetherTargets` | same file, `:110-119` | `mix 0.35f, size 0.50f, width 1.0f, shimmerOctaveSend 0.0f, shimmerFifthSend 0.0f, bloomSend 0.0f, sizeBreathDepth 0.20f, dimensionalityTideDepth 0.20f` |
| `HarmonicCloud` — ranges | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | `kMaxPartials = 64` (`:138`); `kMaxInharmonicity = 0.1f` (`:191`); `kMinTiltDbPerOct = -12.0f` / `kMaxTiltDbPerOct = 12.0f` (`:194-195`); `kMaxDriftCents = 50.0f` (`:214`); `kMinAttackSec = 0.05f` / `kMaxAttackSec = 30.0f` (`:218-219`); `kMinDecaySec = 0.05f` / `kMaxDecaySec = 60.0f` (`:220-221`); `kMaxEnvOffsetSec = 2.0f` (`:224`). Gravity clamps to `[-1, +1]` (`:482`); richness/mutation/spread to `[0,1]` |
| `HarmonicCloud` — the two un-forwarded setters | same file, `:513`, `:580`; defaults `:2131`, `:2135` | `void setDriftSmoothness(float s) noexcept` (clamp `[0,1]`, member default `driftSmoothness_ = 0.5f`) and `void setEnvelopeOffsetSpread(float spread) noexcept` (clamp `[0,1]`, member default `offsetSpread_ = 0.0f`). Both are FR-070 forwarder targets. |
| `SpectralMorphEngine` | `dsp/include/krate/dsp/systems/spectral_morph_engine.h` | `enum class TravelMode : std::uint8_t { External = 0, Spline }` (`:139`); `kMinStates = 2` / `kMaxStates = 4` (`:96-97`); `kMaxBloomFraction = 0.6f` (`:100`); `kMinTravelRate = 1.0f/600.0f` / `kMaxTravelRate = 1.0f` (`:101-102`); `void setState(int, const SpectralState&)` (`:292`), `void setStateCount(int)` (`:318`), `void setBloom(float)` (`:332`), `void setEntropy(float)` (`:341`), `void setTravelMode(TravelMode)` (`:345`), `void setTargetPosition(float)` (`:348`), `void setTravelRate(float)` (`:358`), `void setWaypointInterval(double seconds) noexcept` (`:385`, rejects non-finite, forwards to `spline_`), `[[nodiscard]] double getWaypointInterval() const` (`:394`) |
| `SplineTrajectory` | `dsp/include/krate/dsp/processors/spline_trajectory.h` | `kMinInterval = 0.5f` / `kMaxInterval = 30.0f` (`:117`, `:119`), `kDefaultInterval = 2.0f` (`:123`); `void setWaypointInterval(double) noexcept` clamps into that range (`:165-168`) |
| `EntropyProcessor` | `dsp/include/krate/dsp/processors/entropy_processor.h:57` | `void setEntropy(float e) noexcept` (`:230`) — reached only through `SpectralMorphEngine::setEntropy` (`spectral_morph_engine.h:341`), which is what `SeraphisVoice::setEntropy` forwards to. Phase 9 registers **one** Entropy parameter; there is no second surface. |
| `SpectralState` | `dsp/include/krate/dsp/processors/spectral_state.h:44-63` | `struct SpectralState { std::array<float,64> ratios, amplitudes; std::array<char,16> name; float tiltDbPerOct; float inharmonicity; int numPartials; }`, `static_assert(std::is_trivially_copyable_v<SpectralState>)` (`:65`) |
| `SpectralState` serialization | same file, `:182-306` | `inline constexpr std::uint8_t kSpectralStateFormatVersion = 1;` (`:182`); `inline constexpr std::size_t kSpectralStateBytes = 541;` (`:185-186`); `[[nodiscard]] inline std::size_t serializeSpectralState(const SpectralState& s, std::byte* dest, std::size_t capacity) noexcept` (`:238`) returning `kSpectralStateBytes` or **0 having written nothing**; `[[nodiscard]] inline bool deserializeSpectralState(const std::byte* src, std::size_t size, SpectralState& out) noexcept` (`:274`) leaving `out` bitwise untouched on rejection. Documented **exact** round-trip (`:270-272`). |
| Factory states | same file, `:313`, `:373` | `enum class SpectralStateId : std::uint8_t { SineStack = 0, Bell, Choir, Glass, Breath };` and `[[nodiscard]] inline SpectralState makeFactoryState(SpectralStateId) noexcept`; `kSpectralStateCount = 5` (`:315`) |
| `ContinuousBody` | `dsp/include/krate/dsp/systems/continuous_body.h` | `enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice }` (`:81`), `kNumMaterials = 5` (`:84`); `void setMaterial(BodyMaterial) noexcept` (`:1122`) — **live-safe**: self-guards on `m == material_` (`:1124`) and crossfades. Ranges: resonance `[0,1]` d`0.7` (`:122-124`), damping `[0,1]` d`0.0` (`:126-128`), keyTracking `[0,1]` d`1.0` (`:130-132`), drive `[0,4]` d`1.0` (`:134-136`), mix `[0,1]` d`1.0` (`:138-140`), cloudMix `[0,1]` d`0.25` (`:142-144`), cloudDecaySec `[0.1,30]` d`4.0` (`:146-148`), cloudSize `[0,1]` d`1.0` (`:150-152`), cloudDamping `[0,1]` d`0.3` (`:154-156`), width `[0,1]` d`1.0` (`:158-160`) |
| `ContinuousBody` — the **BODY SET** (FR-070 #12–#13) | same file, `:1276-1279`, `:1300-1322`; defaults `:163-164`, members `:4217-4218` | `void setInputAgcEnabled(bool) noexcept` — *"Enable the input AGC (FR-034), default true. Absorbed by the drive smoother, so toggling is clickless"* — `kDefaultAgcEnabled = true` (`:163`), member `agcEnabled_` (`:4217`). `void setResonatorBypass(bool) noexcept` — *"Bypass the resonator engines (FR-063), default false"*, **self-guarding** (`if (bypass == resonatorBypass_) return;`, `:1302-1304`) and applying a **10 ms equal-power ramp at the control step**, with the documented un-bypass re-tune of the waveguide. `kDefaultResonatorBypass = false` (`:164`), member `resonatorBypass_` (`:4218`). **Neither has a getter** — FR-072 adds the two. |
| `ContinuousBody` — the AGC ↔ FR-033a interaction | same file, `:2901-2919`, `:3029-3046`, `:3683-3689`, `:3998-4004` | `setInputAgcEnabled(false)` is **not** a bypass of one estimator, it changes three coupled things at once, all read this session: `seedLog2For()` returns `0.0f` (*"the AGC-off branch is not a guard, it is the contract"*, `:2901-2909`), so the per-material `kExcitationCompSeed` warm start shipped in **`ee408854`** is switched **off**; `updateExcitationComp()` early-returns having forced `excitationCompLog2_ = 0`, `excitationComp_ = kMinExcitationComp` and abandoned the measurement window (`:3031-3040`); and `controlStep()` / the recovery path set `rmsGain_ = 1.0f` instead of `clamp(kTargetInputRms / max(inputRms_, kRmsFloor), …)` (`:3686`, `:4000`). The component becomes a **fixed gain**. FR-070 #12 states the consequence normatively. |
| `AtmosphereEngine` | `dsp/include/krate/dsp/systems/atmosphere_engine.h` | `kMinGrainSeconds = 0.05f` / `kMaxGrainSeconds = 30.0f` (`:299-300`); `kMinDensity = 0.1f` / `kMaxDensity = 20.0f` (`:301-302`); `kMaxPositionSeconds = 30.0f` (`:303`); `kMaxPitchSemitones = 24.0f` (`:304`); `kMaxDriftRangeSemitones = 12.0f` (`:306`); `kMaxLevel = 2.0f` (`:314`). The two drift FR-070 targets: `void setDriftSmoothness(float) noexcept` (`:844`, non-finite → `0.7f`, member default `driftSmoothness_ = 0.7f` at `:2358`) and `void setDriftRangeSemitones(float) noexcept` (`:852`, member default `driftRangeSemitones_ = 2.0f` at `:2359`) |
| `AtmosphereEngine` — the **ATMOSPHERE SET** (FR-070 #6–#11) | same file, `:800-836`, `:959`; getters beside each setter | All six are already **complete** setter/getter pairs, so FR-072 creates **nothing** for them: `void setJitter(float) noexcept` / `getJitter()` (`:800-804`, clamp `[0,1]`, non-finite → `0.5f`, member default `jitter_ = 0.5f` at `:2352`); `setPositionSeconds(float)` / `getPositionSeconds()` (`:807-811`, clamp `[0, kMaxPositionSeconds]`, member default `positionSeconds_ = 1.0f` at `:2353`); `setPositionSpread(float)` / `getPositionSpread()` (`:815-819`, clamp `[0,1]`, member default `positionSpread_ = 0.3f` at `:2354`); `setPitchSemitones(float)` / `getPitchSemitones()` (`:822-827`, clamp `[−24, +24]`, member default `pitchSemitones_ = 0.0f` at `:2355`, *"SNAPSHOTTED at birth"*); `setPitchSpread(float)` / `getPitchSpread()` (`:830-834`, clamp `[0,1]`, member default `pitchSpread_ = 0.15f` at `:2356`, snapshotted at birth); `void setGrainEnvelope(GrainEnvelopeType) noexcept` / `getGrainEnvelope()` (`:959-963`, a plain store — *"allocation-free, bounded and cheap enough to drive from an automation lane at block rate"* — member default `envelopeType_ = GrainEnvelopeType::Hann` at `:2292`) |
| `GrainEnvelopeType` | `dsp/include/krate/dsp/core/grain_envelope.h:14-22` | `enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman, Linear, Exponential }` — **six** entries, matching `AtmosphereEngine::kEnvelopeTypeCount = 6` (`atmosphere_engine.h:197`); every window is generated once in `prepare()` (`:427`), so selection is a pointer swap and never allocates |
| `AetherReverb` | `dsp/include/krate/dsp/effects/aether_reverb.h` | Eighteen control setters, all `noexcept`. **Fourteen** funnel through `applyControl` (a clamp + smoother-target store); **four do not** and were verified individually this session: `setFreeze` is a bool latch with an early-out (`:2230-2237`), `setModSmoothness` stores `modSmoothness_` directly and then loops `drift_[j].setSmoothness(...)` over `kMaxChannels/2` = 8 channels (`:2268-2273`), `setSizeBreathDepth` (`:2320-2322`) and `setDimensionalityTideDepth` (`:2328-2330`) are direct clamped member stores with no smoother. The full list: `setSize` (`:2208`), `setDensity` (`:2211`), `setDecaySeconds` (`:2214`), `setFreeze(bool)` (`:2230`, **self-guarding** — redundant calls are dropped, `:2231-2233`), `setDimensionality` (`:2239`), `setDamping` (`:2244`), `setPreDelayMs` (`:2247`), `setModDepth` (`:2254`), `setModSmoothness` (`:2268`), `setShimmerOctaveSend` (`:2280`), `setShimmerFifthSend` (`:2285`), `setBloomSend` (`:2295`), `setBloomDecay` (`:2301`), `setSpectralDiffusion` (`:2310`), `setSizeBreathDepth` (`:2320`), `setDimensionalityTideDepth` (`:2328`), `setWidth` (`:2333`), `setMix` (`:2336`); plus `void setSeed(std::uint32_t) noexcept` (`:2361`) |
| `AetherReverb` — defaults | same file, `:2730-2779` | `kDefaultSize 0.50` (`:2730`), `kDefaultDensity 0.70` (`:2732`), `kDefaultDecaySeconds 4.0` with `kDecayMinSeconds 0.5` / `kDecayMaxSeconds 60.0` (`:2734-2736`), `kDefaultDimensionality 0.35` (`:2738`), `kDefaultDamping 0.40` (`:2740`), `kDefaultPreDelayMs 0.0` / `kMaxPreDelayMs 200.0` (`:2742-2743`), `kDefaultModDepth 0.25` (`:2746`), `kDefaultModSmoothness 0.60` (`:2748`), `kDefaultSizeBreathDepth 0.20` (`:2749`), `kDefaultTideDepth 0.20` (`:2750`), `kDefaultSend 0.0` (`:2760`), `kDefaultBloomDecay 0.50` (`:2762`), `kDefaultSpectralDiffusion 0.0` (`:2764`), `kDefaultWidth 1.0` (`:2777`), `kDefaultMix 0.35` (`:2779`) |
| `OrbitModulator` | `dsp/include/krate/dsp/processors/orbit_modulator.h` | `kMinRate = 0.01f` / `kMaxRate = 0.5f` (`:108`, `:110`), `kDefaultRate = 0.1f` (`:122`), `kDefaultDepth = 1.0f` (`:123`); `void setRate(float hz)` (`:167`), `setCoupling(float)` (`:173`), `void setGrowth(float) noexcept` clamped **`[-1,+1]`** (`:179-181`), `void setDepth(float normalized) noexcept` clamped `[0,1]` (`:185-187`) |
| `GrowthEnvelope` | `dsp/include/krate/dsp/processors/growth_envelope.h` | `kMinDuration = 1.0f` (`:96`), `kMaxDuration = 60.0f` (`:98`), `kDefaultDuration = 10.0f` (`:100`); `void setDuration(float seconds) noexcept` clamping into that range (`:145`) |
| `MultiStageEnvelope` | `dsp/include/krate/dsp/processors/multi_stage_envelope.h:61` | `kMinStages = 4` / `kMaxStages = 8` (`:63-64`), **`kMaxStageTimeMs = 10000.0f`** (`:65`); `void setStageTime(int, float)` (`:150`) and `void setReleaseTime(float ms) noexcept` (`:205`) both clamp to `[0, kMaxStageTimeMs]` (`:153`, `:207`) |
| Phase 8 global pack | `plugins/seraphis/src/parameters/global_params.h` | `struct GlobalParams` of `std::atomic<>` (`:34-47`); `clampPolyphony(int) -> std::size_t` (`:62`); `handleGlobalParamChange` (`:72`); `registerGlobalParams` (`:102`); `formatGlobalParam` (`:129`); `saveGlobalParams` (`:160`); `bool loadGlobalParams(...)` — EOF-safe, returns `false` on short read (`:167`); `template<typename SetParamFunc> loadGlobalParamsToController` (`:189`). **This six-function shape is the contract every new pack copies.** |
| Phase 8 macro pack | `plugins/seraphis/src/parameters/macro_params.h` | `struct MacroParams { std::atomic<float> dream{0.0f}, bloom{0.0f}, dissolve{0.0f}, gravity{0.5f}, entropy{0.0f}; }` (`:34-40`) and the same six functions (`:46`, `:67`, `:87`, `:107`, `:116`, `:138`). The banner (`:5-9`) states the Phase 8 inertness that FR-050 inverts. |
| Phase 8 processor | `plugins/seraphis/src/processor/processor.cpp` | `processParameterChanges` dispatching by **ID range** using `kGlobalParamRangeEnd` / `kMacroParamRangeEnd` and taking `getPoint(numPoints - 1)` (`:525-556`); its comment at `:518-521` already anticipates Phase 9. `pushGlobalParams()` — the **on-change-only** tracker pattern (`:585-603`). `renderSlice()` steps 2–6 (`:617-677`), with `macros_.apply(*engine_)` at `:623` and `applyAetherTargets(...)` at `:624`. `setState` version gate (`:479-494`), `getState` (`:500-509`) |
| Phase 8 processor header | `plugins/seraphis/src/processor/processor.h` | `std::unique_ptr<Krate::DSP::SeraphisEngine> engine_` (`:76`), `std::unique_ptr<Krate::DSP::AetherReverb> reverb_` (`:77`), `Krate::DSP::SeraphisMacroMatrix macros_{}` (`:78`), `GlobalParams globalParams_{}` (`:80`), `MacroParams macroParams_{}` (`:81`), `static_assert(sizeof(Processor) < 64u*1024u)` (`:104`) |
| Engine config header | `plugins/seraphis/src/engine/seraphis_engine_config.h` | `kEngineSeed`/`kReverbSeed = 1u` (`:28-29`), `kMasterGainSmoothMs = 20.0f` (`:35`), `kMaxBlockSamples` (`:40`); `makeSeraphisEngineConfig(polyphony, seed, maxBlockSamples)` (`:43`); `makeSeraphisReverbConfig(maxBlockSamples)` (`:64`); `inline void applyAetherTargets(Krate::DSP::AetherReverb&, const Krate::DSP::SeraphisAetherTargets&) noexcept` (`:93-103`) — the free function FR-055 extends |
| Plugin IDs | `plugins/seraphis/src/plugin_ids.h` | `kCurrentStateVersion = 1` (`:20`); `enum ParameterIDs : Steinberg::Vst::ParamID` with the eight shipped IDs (`:57-69`); the frozen-type note (`:71-76`); `kGlobalParamRangeEnd = 100` / `kMacroParamRangeEnd = 200` (`:79-80`); the reserved-range comment (`:46-55`) |
| Controller | `plugins/seraphis/src/controller/controller.h:24` | `class Controller : public EditControllerEx1, public VSTGUI::VST3EditorDelegate` with `initialize`, `terminate`, `setComponentState`, `getParamStringByValue`, `createView`; `std::unique_ptr<Krate::Plugins::PresetManager> presetManager_` (`:44`). **No `INoteExpressionController`** (`:9-10`) — and none is added: **RQ-2** decided per-note expression into a later named phase, so FR-064 is unconditional. |
| Dropdown helpers | `plugins/shared/src/ui/parameter_helpers.h` | `createDropdownParameter(const TChar* title, ParamID, std::initializer_list<const TChar*>)` (`:23`); `createDropdownParameterWithDefault(const TChar*, ParamID, int32_t defaultIndex, std::initializer_list<const TChar*>)` (`:47`); the pointer+count overloads (`:97`, `:118`); `double logMapFromNormalized(double, double mn, double mx)` (`:80`) and `double logMapToNormalized(double, double mn, double mx)` (`:85`) — both clamp on the way in and out, so a round-trip is stable at the endpoints (`:75-78`) |
| Placeholder UI | `plugins/seraphis/resources/editor.uidesc` | `<control-tags>` block with the eight shipped tags (`:15-24`); template `"editor"`, `CViewContainer` 420×300 (`:25`); six `CSlider`, one `COptionMenu` (`:60`), one `CCheckBox` (`:67`). The banner (`:3-5`) says Phase 11 replaces it wholesale. |
| Test target | `plugins/seraphis/tests/CMakeLists.txt` | `add_executable(seraphis_tests …)` with an **explicit** source list (`:5-31`) — a new test file must be added here or it silently drops; second compilation of `../src/processor/processor.cpp` and `../src/controller/controller.cpp` (`:16-17`); `SERAPHIS_RESOURCES_DIR` (`:58-61`) |
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h:102-105` | `inline void exerciseEditorLifecycle(Steinberg::Vst::EditController&, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)` |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h` | `fingerprintRender(std::span<const float>)` (`:64`), `compareFingerprints(...)` (`:101`), `kSampleTolerance = 1.0e-4f` (`:49`), `kMetricTolerance = 1.0e-5` (`:52`) |
| Allocation detector | `tests/test_helpers/allocation_detector.h:75` | `class AllocationScope`; readings taken via `AllocationDetector::instance().getAllocationCount()` (the Phase 8 A9 amendment) |

---

## New components

**ODR sweep run this session** (`grep -rn "struct <Name>\|class <Name>\|<symbol>" dsp/ plugins/`, per root `CLAUDE.md` and `dsp/CLAUDE.md`).

### DSP additions (Layer 3, `Krate::DSP`)

| New type / symbol | Layer | Header | ODR sweep result |
|---|---|---|---|
| `SeraphisVoiceParams` (POD) | 3 (systems) | `dsp/include/krate/dsp/systems/seraphis_engine.h` (beside `SeraphisEngineConfig`, `:92-97`) | `grep -rn "SeraphisVoiceParams" dsp/ plugins/` → **0 hits**. CLEAR. Distinct from the existing `SeraphisVoiceConfig` (`seraphis_voice.h:105`), which is *prepare-time* and is not extended. |
| `SeraphisEngine::applyVoiceParams(const SeraphisVoiceParams&) noexcept` | 3 | same header, public section | `grep -rn "applyVoiceParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `SeraphisEngine::applySpectralStates(const SpectralState*, int) noexcept` (FR-005) | 3 | same header, public section | `grep -rn "applySpectralStates" dsp/ plugins/` → **0 hits**. CLEAR. Distinct from `SeraphisVoice::setSpectralState` / `setSpectralStateCount` (`seraphis_voice.h:706, :713`), which it calls. |
| `SeraphisMacroMatrix::setTargetBase(SeraphisMacroTarget, float) noexcept` + `resetTargetBases() noexcept` + `[[nodiscard]] float getTargetBase(SeraphisMacroTarget) const noexcept` | 3 | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` | `grep -rn "setTargetBase" dsp/ plugins/` → **0 hits**. CLEAR. |
| **Fourteen** read-back accessors (FR-072, A6): `SeraphisVoice::growth()` + `ContinuousBody::{getResonance, getDamping, getKeyTracking, getDrive, getMix, getCloudMix, getCloudDecaySec, getCloudSize, getCloudDamping, getWidth, isInputAgcEnabled, isResonatorBypass}` + `SeraphisEngine::getOutputSaturation()` | 3 | `seraphis_voice.h`, `continuous_body.h`, `seraphis_engine.h` | No new *type*. `grep -rn "getOutputSaturation" dsp/ plugins/` → **0 hits** on any accessor (only `SeraphisEngine::setOutputSaturation`, `:566`). CLEAR — and it is a **forwarder**, not a new member: `TapeSaturator::getSaturation()` already exists (`processors/tape_saturator.h:283-285`) and `saturation_` is written only by `setSaturation`, clamped, before the smoother target (`:248-252`), which is exactly the "amount last pushed, not the ramp position" semantics SC-023 clause 4 needs. Every name checked against the class's complete getter surface this session — `ContinuousBody`'s is `continuous_body.h:1450-1534` (`getMaterial`, `getActiveModeCount`, `getModeFrequencyHz`, `getBodyFrequencyHz`, `getEngineT60Sec`, `getDriveGain`, `getInputRms`, `getExcitationComp`, `getSteadyStateGainBound`, `isCrossfading`, `getCrossfadePosition`, `getCloudFeedbackGain`, `getCloudLoopSeconds`, `getClampEngagementCount`, `getEngineSampleCount`) and `SeraphisVoice`'s accessor family is `:763-771`. **Zero collisions.** `getDrive` vs the existing `getDriveGain` is the one near-miss and is deliberate: the former is the pushed control, the latter the smoothed derived gain. `isInputAgcEnabled` / `isResonatorBypass` use the `is` prefix because they return `bool`, matching the class's existing `isCrossfading()` (`:1500`); `grep -rn "isInputAgcEnabled\|isResonatorBypass" dsp/ plugins/` → **0 hits**. CLEAR. The six ATMOSPHERE-SET reads need **no** new accessor — `AtmosphereEngine` already ships all six getters (Existing components). |
| Thirteen `SeraphisVoice` forwarders (FR-070): `setCloudDriftSmoothness`, `setEnvelopeOffsetSpread`, `setAtmosDriftSmoothness`, `setAtmosDriftRangeSemitones`, `setWaypointInterval`, `setAtmosJitter`, `setAtmosPositionSeconds`, `setAtmosPositionSpread`, `setAtmosPitchSemitones`, `setAtmosPitchSpread`, `setAtmosGrainEnvelope`, `setBodyInputAgcEnabled`, `setBodyResonatorBypass` | 3 | `dsp/include/krate/dsp/systems/seraphis_voice.h` | No new *type*. Names checked against the existing facade (`:641-693`): `setCloudDriftSmoothness` / `setAtmosDriftSmoothness` are **deliberately prefixed** because the bare `setDriftSmoothness` would be ambiguous between `HarmonicCloud` (`:513`) and `AtmosphereEngine` (`:844`), which the facade reaches through the same class. The six ATMOSPHERE-SET names carry the same `setAtmos` prefix for consistency with that pair, and the two BODY-SET names carry `setBody` because the bare `setInputAgcEnabled` sits next to the body facade's existing bare `setMaterial` / `setResonance` / `setDamping` (`:671-683`) and the prefix is what keeps the atmosphere/body split legible on a facade that reaches both. Phase 7's existing bare names (`setDriftDepthCents` cloud, `setDriftDepth` atmosphere) are **not renamed** — surgical-changes rule. `setEnvelopeOffsetSpread` and `setWaypointInterval` are unambiguous (one owner each). `grep -rn "setAtmosJitter\|setAtmosPosition\|setAtmosPitch\|setAtmosGrainEnvelope\|setBodyInputAgcEnabled\|setBodyResonatorBypass" dsp/ plugins/` → **0 hits**. CLEAR. |

### Plugin-local additions (`namespace Seraphis`)

| New type | Header | ODR sweep result |
|---|---|---|
| `Seraphis::CloudParams` | `plugins/seraphis/src/parameters/cloud_params.h` | `grep -rn "struct CloudParams\|class CloudParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `Seraphis::MorphParams` | `plugins/seraphis/src/parameters/morph_params.h` | `grep -rn "struct MorphParams\|class MorphParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `Seraphis::LifeModParams` | `plugins/seraphis/src/parameters/life_mod_params.h` | `grep -rn "struct LifeModParams\|class LifeModParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `Seraphis::BodyParams` | `plugins/seraphis/src/parameters/body_params.h` | `grep -rn "struct BodyParams\|class BodyParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `Seraphis::AtmosphereParams` | `plugins/seraphis/src/parameters/atmosphere_params.h` | `grep -rn "struct AtmosphereParams\|class AtmosphereParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `Seraphis::AetherParams` | `plugins/seraphis/src/parameters/aether_params.h` | `grep -rn "struct AetherParams\|class AetherParams" dsp/ plugins/` → **0 hits**. CLEAR. |
| `plugins/seraphis/src/parameters/dropdown_mappings.h` (no new type — `inline constexpr` label tables + `inline` index↔enum converters) | same dir | Three plugins already carry a file of this name (`gradus`, `iterum`, `ruinae`), each with its own include-guard-free `#pragma once` and its own namespace. A fourth in `namespace Seraphis` collides with none. CLEAR. |

**No new class name anywhere in this phase collides.** The two `dsp/` names most at risk of a near-miss —
`SeraphisVoiceConfig` (existing, prepare-time) and `SeraphisVoiceParams` (new, run-time) — are
deliberately distinct types with disjoint fields; **FR-001** states the rule that keeps them so.

---

## Conventions decided in this spec

### C-1. Deep parameters supply the macro matrix's per-target **base**

This is the phase's central design decision and it is forced by evidence, not preference.
`SeraphisMacroMatrix::apply()` writes nineteen `SeraphisVoice` setters on every call
(`seraphis_macro_matrix.h:630-658`), and `Processor::renderSlice` calls it every slice
(`processor.cpp:623`). Any independent write of those nineteen values from a parameter would survive at
most one slice.

**Decision.** For the 27 parameters whose target appears in `SeraphisMacroTarget`, the processor writes the
denormalized parameter value into the matrix with `setTargetBase(target, value)` instead of into the voice.
`evaluateAll()` then computes `value = parameterValue + Σ macroContributions`
(`seraphis_macro_matrix.h:719-731`), so:

- at **Phase 7's FR-060 macro neutral** (`specs/seraphis-phase7-voice-engine/spec.md:1204-1210`; the
  arithmetic is `seraphis_macro_matrix.h:714-718` — *"a PROPERTY OF THE ARITHMETIC"*) every contribution
  is exactly `0`, and the parameter reaches the voice unmodified;
- at a non-neutral macro the parameter is the origin the macro moves *from*, which is what "macro-first
  performance surface with deep per-engine parameters underneath" (roadmap line 79) means.

The mechanism is sound because `static_assert(everyRowSharesOneBasePerTarget(kRows))`
(`seraphis_macro_matrix.h:752`) already guarantees exactly one base per target — a per-target override is
well-defined by construction, and `resetTargetBases()` restores the `kRows` literals verbatim.

**What "compose" means, and what happens at a clamp. This is normative; SC-004 is written against it.**
`kRows`' `amount` values were sized against the **default** base so that a full macro sweep lands exactly
on the component's own clamp — verified this session: Bloom → `CloudRichness` is `base 0.60, amount +0.40`
(`seraphis_macro_matrix.h:246-251`) and `HarmonicCloud::setRichness` clamps to `[0,1]`
(`harmonic_cloud.h:416`); Dream → `CloudInharmonicity` is `base 0.030, amount −0.030` with a floor of 0
(`:187-192`); Bloom → `MorphTargetPosition` is `base 0.0, amount +1.0`. Overriding the base therefore
moves the *reachable* span, and an override placed **at the clamp the macro travels toward** consumes the
whole span: set `kCloudRichnessId` to 1.0 and Bloom's sweep evaluates 1.0 → 1.4, clamps flat at 1.0, and
moves nothing at all.

Phase 9 **accepts** that behaviour rather than engineering around it — rescaling `amount` by the remaining
headroom would make the macro's effect depend on the deep parameter's value, i.e. would make the two
surfaces multiply instead of compose, and would change shipped macro behaviour at the defaults, which the
Scope forbids. Two consequences are binding:

1. **Composition is asserted where headroom exists.** A macro sweep against an overridden base is asserted
   as **monotone in the same direction with a stated minimum effect size** only when the override leaves
   headroom in the macro's direction of travel. SC-004 names the offsets it uses and they are chosen for
   that reason (e.g. Bloom is measured against `kCloudRichnessId` at **0.45**, *below* the 0.60 default, so
   the +0.40 span stays fully reachable — never above it).
2. **Saturation against a deep extreme is legal, and is asserted as such.** With a deep parameter pinned
   at the clamp its macro travels toward, the macro's contribution is asserted to be **monotone
   non-decreasing** (equivalently: never moves the metric the *wrong* way), not strictly monotone, and
   `getTargetBase` must equal the pushed value. Saturation is the correct reading of "the deep parameter
   is the origin the macro moves from" when there is nowhere left to move.

The eight Aether-owned targets work identically: `computeAetherTargets()` reads the same `evaluateAll()`
array (`:667-679`), so a base override reaches the reverb through the existing `applyAetherTargets`
(`seraphis_engine_config.h:93`) with no change to that function.

### C-2. Everything else reaches the voices through one new broadcast call

The remaining voice-owned parameters (material, resonance, key tracking, drive, mixes, cloud-decay tail,
the two body toggles, atmosphere density/grain/pan/decorrelation/freeze-mix and the whole ATMOSPHERE SET
— jitter, position, position spread, pitch, pitch spread, grain envelope — spatial rate/coupling/growth,
envelope mode, growth duration, cloud decay, morph bloom/travel-rate/travel-mode/waypoint interval, and
the FR-070 thirteen) have no macro row and therefore no base to override. They travel in a `SeraphisVoiceParams` POD through
`SeraphisEngine::applyVoiceParams()`, which fans out to **`voices_[0 .. kMaxVoices)` — all sixteen slots**,
matching `setSeed` (`seraphis_engine.h:355`), `setAtmosphereFreeze` (`:557`) and FR-005.

**`getPolyphony()` is the wrong bound and a previous revision of this spec had it.** Slots above the
current polyphony are **not** silent: `setPolyphony` force-idles an excess slot with `voices_[i].noteOff()`
and records `orphanTail_ |= voiceBit(i)` when `!voices_[i].isFinished()` (`seraphis_engine.h:337-349`,
whose own comment says *"the voice keeps rendering its tail because isRendering()'s second clause is
!isFinished()"*), and `processStereoBlock`'s loop bound is `v < kMaxVoices` **unconditionally** (`:437`,
`:464`), summing every slot for which `isRendering(v)` holds (`:465-486`). A `getPolyphony()` bound would
therefore leave an audibly-summed orphan on prepare-time defaults for the whole of its release — up to
8000 ms at the shipped default (`seraphis_voice.h:359`).

**One residue is left, deliberately and visibly.** `SeraphisMacroMatrix::apply()` keeps its own
`getPolyphony()` bound (`seraphis_macro_matrix.h:625-626`); FR-004/FR-071 forbid changing its shape, so the
27 `MB`-routed values still stop at the polyphony bound. That is **pre-existing Phase 7 behaviour**, not
something this phase introduces, and it is recorded here rather than silently inherited. Widening it is a
Phase 10+ item if the orphan-tail audibility ever measures as a defect.

### C-3. Push cadence: `apply()` every slice, every parameter surface **on change only**

| Surface | Cadence | Why |
|---|---|---|
| `macros_.setMacros(...)` + `setTargetBase(...)` | on change only | Matrix state is plain scalars; re-writing an unchanged value is free but pointless, and the on-change tracker is what SC-007 counts. |
| `macros_.apply(*engine_)` + `applyAetherTargets(...)` | **every slice** (unchanged from Phase 8) | FR-059 idempotence is documented (`seraphis_macro_matrix.h:150-153`); this is how the base override reaches the voices. |
| `engine_->applyVoiceParams(...)` | **on change only**, gated by a generation counter, **plus the settling push and the synced-tempo dirty below** | 37 setters × 16 voices per slice is real CPU for no benefit, and every one of them is idempotent, so there is nothing a per-slice cadence buys. |
| `engine_->applySpectralStates(...)` | **on pending only** (FR-046) — set on a `CFG` change, retried once per `process()` call until every voice accepts, then cleared | `setSpectralState` / `setSpectralStateCount` are configure-time-gated and increment `rejectedConfigCalls_` when the voice is sounding (`seraphis_voice.h:699-719`); calling them every slice would burn that counter forever. But a *pure* on-change cadence never converges, because a rejection would never be retried — hence the pending flag rather than a bare generation counter. |
| `applyAetherParams(*reverb_, ...)` | **on change only**, sharing the FR-042 generation counter | *(Corrected. A previous revision said "every slice" and justified it with "every setter funnels through `applyControl`, a clamp plus a smoother-target store". Verified this session, that is false for four of the eighteen: `setFreeze` is a latch with an early-out (`aether_reverb.h:2230-2237`), `setModSmoothness` loops `drift_[j].setSmoothness(...)` over 8 channels (`:2268-2273`), and `setSizeBreathDepth` / `setDimensionalityTideDepth` are direct member stores (`:2320`, `:2328`). Three of those four would do real per-call work on every slice, forever, for no benefit.)* The ten `AE` values are plain scalars behind an on-change tracker, exactly like the `VP` push. |

The generation counter is a plain `std::size_t` incremented by `processParameterChanges` on **any** write to
a voice-owned pack, and compared in `process()` before the first slice — exactly the shape
`pushGlobalParams()` already uses for polyphony and soft-limit (`processor.cpp:585-603`).

**Three amendments to "on change only", each decided 2026-08-01 and each normative. They are the only
ways a push may run without a parameter having been written this block.**

1. **The settling push (Q1), on the absolute 64-sample control-chunk grid** (amendment A1, 2026-08-01).
   A class-(b) parameter (FR-059) is smoothed **processor-side**, and a smoother only produces a ramp if
   the smoothed value is re-pushed while it settles. Therefore: **while any class-(b) smoother is
   un-settled, its owning push — `applyVoiceParams` for a `VP` row, `setTargetBase` for an `MB` row —
   runs on the engine's absolute 64-sample control-chunk grid** (`SeraphisEngine::kControlChunkSamples`),
   `process()` capping its slice length at the distance to the next boundary for exactly as long as any
   class-(b) smoother is un-settled, and stopping the moment every one of them reports settled. It is
   bounded — `N_chunk` pushes spanning `N_block` host blocks, per SC-007 — and in the settled steady
   state the slice structure is exactly Phase 8's, with no subdivision and no extra cost. This is **not**
   the per-slice cadence this table forbids: a slice boundary is event-driven and moves with MIDI
   placement, whereas this grid is a fixed absolute 64-sample rule no host block size and no performer's
   timing can move. SC-007's exact-count rows are written against it ("+1" for class-(a),
   "+1 … +`N_chunk`" for class-(b)) and SC-008's steady-state arm is measured **with every smoother
   settled**, which is the state the steady-state budget describes.
   *(The alternatives were rejected on the record: making class (b) empty forces `dsp/` churn FR-071
   forbids; pushing class-(b) rows every slice reintroduces the per-slice work this table argues against;
   and declaring 64-step automation granularity sufficient deletes SC-005's positive control (b).)*
2. **The synced-tempo dirty (Q3).** Tempo is **not** a parameter, so a host tempo ramp writes no atomic
   and would never reach `setTravelRate` under a literal on-change reading. Therefore, when
   `kMorphSyncId` is on, **`process()` recomputes the synced travel rate every block** from
   `processContext` per FR-056, and **dirties `voiceParamGeneration_` when the recomputed rate differs
   from the last pushed one by more than a stated epsilon**. A moving tempo consequently increments
   `applyVoiceParams` every block, which is why SC-007's quiescent arm reads *"no parameter change **and**
   constant tempo"*. The **tempo sample point** (once per `process()` vs once per slice) and the
   **epsilon** are fixed by the plan, not left to the implementer.
3. **`pushAllSurfaces()` (Q2).** `setState()` writes 90 of the 91 stored values through the pack loaders
   and increments **no** generation counter, so under a literal on-change reading a preset loaded after
   `setupProcessing()` would reach the DSP only when the user next touched a control — the classic
   silent-preset-load bug, and one no existing criterion covered. Therefore `setupProcessing()` and
   `setState()` both reach **one shared `pushAllSurfaces(SurfaceInvalidation)` helper** (FR-047) — the
   former calling it directly with the audio thread stopped, the latter raising a single release-store
   request that `process()` consumes before `pushGlobalParams()` (A4) — which bumps both generation
   counters, force-pushes the 27 `MB` bases and the four `ENG` values by resetting the `lastPushed*`
   trackers to a sentinel, and sets `spectralStatesPending_`. SC-023 is the criterion.

### C-4. Registered defaults are **bit-identical** to the shipped engine defaults

Every registered parameter's default normalized value must denormalize to exactly the value
`SeraphisVoice::prepare()` step 6/7 writes (`seraphis_voice.h:284-364`), `AetherReverb`'s `kDefault*`
constants (`aether_reverb.h:2730-2779`), or the component member default cited in the table.

**The negative control is a SAME-BINARY comparison, and SC-002 is authoritative over this paragraph.** A
freshly instantiated Phase 9 plugin at registered defaults (Arm A) must render to within a **per-sample
`maxAbsDiff` of `1.0e-5` over all samples of both channels** of a reference arm (Arm B) built *in the same
translation unit and the same binary*: a `SeraphisEngine` + `AetherReverb` pair configured directly with the
**Phase 8 shipped defaults** (`makeSeraphisEngineConfig` / `makeSeraphisReverbConfig`,
`seraphis_engine_config.h:43`, `:64`) and driven through the Phase 8 chain with **no Phase 9 push path
engaged at all**. `compareFingerprints` may run only as a **secondary, warn-only** aggregate and MUST NOT
gate, and **a checked-in fingerprint reference is forbidden** — see SC-002, which states the construction
normatively and whose wording governs if this summary and it ever diverge. This is the negative control that
keeps "wire up the parameters" from silently becoming "retune the instrument".

### C-5. Parameter-ID map (roadmap lines 396, 399–401; Phase 8 `plugin_ids.h:46-55`)

*(Citations re-verified after FR-058's roadmap amendment, which shifted every roadmap line after 313 by
+13 here: the "start at 0 with 100-ID section gaps" decision is roadmap line **396** (was 383); the
eight-band reserve list runs lines **399–401** (was 386–388). The pre-Phase-9 span in
`plugins/seraphis/src/plugin_ids.h:46` stopped before six of the eight bands; FR-010 rewrote that block
and the citation now reads 396 / 399–401.)*

The reserved bands are honoured exactly. **Every parameter sits in the band of the *component it controls*,
without exception** — the band is a property of the target, never of the modulator family a control
happens to belong to. Two intra-band sub-blocks are introduced and recorded here because the roadmap's
band list has no home for them:

- **600–699 Life Modulators** proper (the per-voice orbit: depth, rate, coupling, growth, voice width) and
  **700–799 Voice Envelope** (envelope mode, growth duration, the two stage times, release) — **confirmed
  by the phase owner on 2026-08-01**: the voice-envelope parameters live at 700–799 *inside* the
  Life Modulators band (600–799), not in a band of their own. The envelope
  lives in the Life-Modulator band because `GrowthEnvelope` *is* one of Phase 1's six life modulators
  (roadmap line 123) and replaces the attack, and because three of the four envelope parameters are already
  macro targets (`EnvStage0Ms`, `EnvStage1Ms`, `EnvReleaseMs`) sitting alongside `SpatialDepth` and
  `VoiceWidth` in the same table. `kEnvGrowthDurationId` is **701**, inside the Voice-Envelope sub-block,
  because it is an envelope control; the sub-block split above is worded to match C-6's table, which is the
  normative record.
- **Global band 0–99** gains one parameter (`kSeedId`, ID 3), which Phase 8 assigned to Phase 9 by name
  (`seraphis_engine_config.h:21-23`: *"NOT a parameter in Phase 8 … Phase 9 owns any per-instance seed"*).

> **CORRECTED — the two atmosphere drift controls are in the atmosphere band, not the life band.** A
> previous revision placed `AtmosphereEngine::setDriftSmoothness` / `setDriftRangeSemitones` (FR-070 #3/#4)
> at IDs **605/606** under the names `kLifeAtmosDriftSmoothnessId` / `kLifeAtmosDriftRangeId`. That
> contradicted the "honoured exactly" sentence above: both forward into `AtmosphereEngine`
> (`atmosphere_engine.h:844`, `:852`), which the roadmap places at **1000–1199** (line 388), and the
> placement was internally inconsistent with the two sibling controls of the same per-grain `BrownianDrift`
> — `kAtmosDriftDepthId` at **1004** and the cloud's analogous drift-smoothness control at **206**, both
> already in their target's band. Because parameter IDs are permanent the moment this phase ships (C-9),
> this could not have been corrected later. They are now **`kAtmosDriftSmoothnessId` = 1009** and
> **`kAtmosDriftRangeId` = 1010** (1000–1008 were the only atmosphere IDs in use), owned by
> `atmosphere_params.h`, and their two floats move from C-8's `[life]` block to `[atmos]`. Nothing else
> changes: at the time of that correction the `VP` route count stayed 29, the byte total stayed 2500, and
> the 68-float / 15-int32 counts were unchanged. *(Those three figures are the pre-expansion ones; the
> 2026-08-01 FR-070 ruling later moved them to 37 / 2532 / 73-float / 18-int32. The correction itself —
> that both drift controls belong in the atmosphere band — is unaffected, and IDs 1009/1010 stand.)*

### C-6. The complete parameter table

**Legend.** *Route* — `MB` = macro base (`setTargetBase`, C-1), `VP` = `SeraphisVoiceParams` (C-2),
`ENG` = a direct `SeraphisEngine` setter, `AE` = a direct `AetherReverb` setter, `CFG` = configure-time
gated (`seraphis_voice.h:706-719`). *Type* — `R` = plain `Vst::Parameter`, `L` = `StringListParameter`
via `createDropdownParameterWithDefault`, `T` = stepped toggle (`stepCount = 1`).
*Map* — `lin` = linear over the stated plain range, `log` = `logMapFromNormalized/ToNormalized`
(`parameter_helpers.h:80,85`).

**Global (0–99)** — Phase 8's three are unchanged; one added.

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 0 | `kMasterGainId` | R | 0 … 2 linear gain | lin | 1.0 | processor |
| 1 | `kPolyphonyId` | L | 1 … 16 | index | 8 | `ENG setPolyphony` |
| 2 | `kSoftLimitId` | T | off / on | — | on | `ENG setOutputSaturation` |
| **3** | **`kSeedId`** | L | 16 curated seeds, **C-10**'s table | index | index 0 (`1u`) | `ENG setSeed` + `AE setSeed` |

**Macros (100–199)** — unchanged IDs, **no longer inert** (FR-050).

| ID | Enum | Type | Default | Route |
|---|---|---|---|---|
| 100–104 | `kMacroDreamId` … `kMacroEntropyId` | R | 0.0, 0.0, 0.0, **0.5**, 0.0 | `macros_.setMacros` |

**Harmonic Cloud (200–399)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 200 | `kCloudRichnessId` | R | 0 … 1 | lin | 0.60 | MB `CloudRichness` |
| 201 | `kCloudInharmonicityId` | R | 0 … 0.1 | lin | 0.030 | MB `CloudInharmonicity` |
| 202 | `kCloudTiltId` | R | −12 … +12 dB/oct | lin | 0.0 | MB `CloudSpectralTiltDb` |
| 203 | `kCloudMutationId` | R | 0 … 1 | lin | 0.25 | MB `CloudMutation` |
| 204 | `kCloudGravityId` | R | −1 … +1 | lin | 0.20 | MB `CloudSpectralGravity` |
| 205 | `kCloudDriftDepthId` | R | 0 … 50 cents | lin | 0.0 | MB `CloudDriftDepthCents` |
| 206 | `kCloudDriftSmoothnessId` | R | 0 … 1 | lin | 0.5 | VP (FR-070 #1) |
| 207 | `kCloudStereoSpreadId` | R | 0 … 1 | lin | 0.35 | MB `CloudStereoSpread` |
| 208 | `kCloudAttackId` | R | 0.05 … 30 s | log | 0.05 | MB `CloudAttackTimeSec` |
| 209 | `kCloudDecayId` | R | 0.05 … 60 s | log | 0.5 | VP |
| 210 | `kCloudEnvOffsetSpreadId` | R | 0 … 1 | lin | 0.0 | VP (FR-070 #2) |

**Spectral Morph / Entropy (400–599)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 400 | `kMorphEntropyId` | R | 0 … 1 | lin | 0.20 | MB `MorphEntropy` |
| 401 | `kMorphBloomId` | R | 0 … 0.6 | lin | 0.0 | VP |
| 402 | `kMorphPositionId` | R | 0 … 3 | lin | 0.0 | MB `MorphTargetPosition` |
| 403 | `kMorphTravelModeId` | L | External / Spline | index | External | VP |
| 404 | `kMorphTravelRateId` | R | 1/600 … 1.0 journeys/s | log | 1/600 | VP |
| 405 | `kMorphSyncId` | T | Free / Synced | — | Free | processor (C-7) |
| 406 | `kMorphSyncNoteId` | L | the 8 note values enumerated in **C-7** | index | **1 bar** (index 4) | processor (C-7) |
| 407 | `kMorphWaypointIntervalId` | R | 0.5 … 30 s | log | 2.0 | VP (FR-070 #5) |
| 408 | `kMorphStateCountId` | L | 2 / 3 / 4 | index | 2 | CFG |
| 409 | `kMorphState0Id` | L | 5 factory states | index | SineStack | CFG |
| 410 | `kMorphState1Id` | L | 5 factory states | index | **Glass** | CFG |
| 411 | `kMorphState2Id` | L | 5 factory states | index | SineStack | CFG |
| 412 | `kMorphState3Id` | L | 5 factory states | index | SineStack | CFG |

**Life Modulators (600–699) and Voice Envelope (700–799)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 600 | `kLifeSpatialDepthId` | R | 0 … 1 | lin | 0.35 | MB `SpatialDepth` |
| 601 | `kLifeSpatialRateId` | R | 0.01 … 0.5 Hz | log | 0.1 | VP |
| 602 | `kLifeSpatialCouplingId` | R | 0 … 1 | lin | 0.0 | VP |
| 603 | `kLifeSpatialGrowthId` | R | −1 … +1 | lin | 0.0 | VP |
| 604 | `kLifeVoiceWidthId` | R | 50 … 150 % | lin | 100 | MB `VoiceWidth` |
| 700 | `kEnvModeId` | L | Standard / Growth | index | Standard | VP |
| 701 | `kEnvGrowthDurationId` | R | 1 … 60 s | log | 10.0 | VP |
| 702 | `kEnvStage0MsId` | R | **1** … 10000 ms | log | 2000 | MB `EnvStage0Ms` |
| 703 | `kEnvStage1MsId` | R | **1** … 10000 ms | log | 4000 | MB `EnvStage1Ms` |
| 704 | `kEnvReleaseMsId` | R | **1** … 10000 ms | log | 8000 | MB `EnvReleaseMs` |

> **The 1 ms floor on 702–704 is load-bearing, not cosmetic.** A previous revision specified these three
> as `log` over `0 … 10000 ms`, which FR-017's mandated helpers cannot express: `logMapFromNormalized` is
> `std::clamp(mn * std::pow(mx / mn, clamped), mn, mx)` (`parameter_helpers.h:80-83`), so at `mn == 0`
> the ratio `mx/mn` is `+inf`, `pow(+inf, x)` is `+inf` for every `x > 0`, and `0 * inf` is **NaN** —
> which `std::clamp` propagates, because both of its comparisons are false. The inverse
> (`:85-88`) is `log(u/0) / log(mx/0)` = `inf/inf` = NaN. Every non-zero normalized value on all three
> parameters would denormalize to NaN; FR-018's clamp would not rescue it; and because all three are
> `MB`-routed, FR-003's mandated `isFiniteBits` rejection (`seraphis_macro_matrix.h:685-689`) would
> silently keep the `kRows` literal, leaving three parameters **permanently inert** and failing SC-003.
> `MultiStageEnvelope::setStageTime` / `setReleaseTime` clamp to `[0, kMaxStageTimeMs]`
> (`multi_stage_envelope.h:153`, `:207`), so a 1 ms floor is inaudible at the DSP end — but it is stated
> here rather than inferred, because the failure is silent.

**Continuous Body (800–999)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 800 | `kBodyMaterialId` | L | Glass / Strings / Metal Plate / Chamber / Ice | index | Glass | VP |
| 801 | `kBodyResonanceId` | R | 0 … 1 | lin | 0.7 | VP |
| 802 | `kBodyDampingId` | R | 0 … 1 | lin | 0.25 | MB `BodyDamping` |
| 803 | `kBodyKeyTrackingId` | R | 0 … 1 | lin | 1.0 | VP |
| 804 | `kBodyDriveId` | R | 0 … 4 | lin | 1.0 | VP |
| 805 | `kBodyMixId` | R | 0 … 1 | lin | 1.0 | VP |
| 806 | `kBodyCloudMixId` | R | 0 … 1 | lin | 0.25 | VP |
| 807 | `kBodyCloudDecayId` | R | 0.1 … 30 s | log | 4.0 | VP |
| 808 | `kBodyCloudSizeId` | R | 0 … 1 | lin | 1.0 | VP |
| 809 | `kBodyCloudDampingId` | R | 0 … 1 | lin | 0.3 | VP |
| 810 | `kBodyWidthId` | R | 0 … 1 | lin | 1.0 | VP |
| **811** | **`kBodyInputAgcId`** | T | off / on | — | **on** | VP (FR-070 #12) |
| **812** | **`kBodyResonatorBypassId`** | T | off / on | — | **off** | VP (FR-070 #13) |

**Granular Atmosphere (1000–1199)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 1000 | `kAtmosLevelId` | R | 0 … 2 | lin | 0.5 | MB `AtmosLevel` |
| 1001 | `kAtmosBlurId` | R | 0 … 1 | lin | 0.0 | MB `AtmosBlur` |
| 1002 | `kAtmosDensityId` | R | 0.1 … 20 grains/s | log | 4.0 | VP |
| 1003 | `kAtmosGrainSecondsId` | R | 0.05 … 30 s | log | 4.0 | VP |
| 1004 | `kAtmosDriftDepthId` | R | 0 … 1 | lin | 0.3 | MB `AtmosDriftDepth` |
| 1005 | `kAtmosPanSpreadId` | R | 0 … 1 | lin | 0.7 | VP |
| 1006 | `kAtmosDecorrelationId` | R | 0 … 1 | lin | 0.5 | VP |
| 1007 | `kAtmosFreezeMixId` | R | 0 … 1 | lin | 0.0 | VP |
| 1008 | `kAtmosFreezeId` | T | off / on | — | off | `ENG setAtmosphereFreeze` |
| 1009 | `kAtmosDriftSmoothnessId` | R | 0 … 1 | lin | 0.7 | VP (FR-070 #3) |
| 1010 | `kAtmosDriftRangeId` | R | 0 … 12 semitones | lin | 2.0 | VP (FR-070 #4) |
| **1011** | **`kAtmosJitterId`** | R | 0 … 1 | lin | **0.5** | VP (FR-070 #6) |
| **1012** | **`kAtmosPositionId`** | R | 0 … 30 s | **lin** | **1.0** | VP (FR-070 #7) |
| **1013** | **`kAtmosPositionSpreadId`** | R | 0 … 1 | lin | **0.3** | VP (FR-070 #8) |
| **1014** | **`kAtmosPitchId`** | R | −24 … +24 semitones | lin | **0.0** | VP (FR-070 #9) |
| **1015** | **`kAtmosPitchSpreadId`** | R | 0 … 1 | lin | **0.15** | VP (FR-070 #10) |
| **1016** | **`kAtmosGrainEnvelopeId`** | L | Hann / Trapezoid / Sine / Blackman / Linear / Exponential | index | **Hann** | VP (FR-070 #11) |

> **`kAtmosPositionId` is `lin`, not `log`, and that is forced.** Its plain range starts at **0**
> (`setPositionSeconds` clamps to `[0, kMaxPositionSeconds]`, `atmosphere_engine.h:807-811`), and
> FR-017's mandated `logMapFromNormalized` is `mn * pow(mx/mn, u)` (`parameter_helpers.h:80-83`), which at
> `mn == 0` evaluates `0 * inf` = **NaN** for every `u > 0` — the identical failure the 1 ms floor note
> under the Life Modulators table records for IDs 702–704. A log feel here would require a non-zero
> floor, and a non-zero floor would make position 0 (grains born at the write head) unreachable, which is
> a musically meaningful setting. Linear it is.
>
> **The six ATMOSPHERE-SET defaults are the component's own member initializers, not `prepare()`'s.**
> `SeraphisVoice::prepare()` step 6 sets eight atmosphere values (`seraphis_voice.h:319-327`) and touches
> **none** of these six, so C-4's bit-identity requirement binds against `atmosphere_engine.h:2352-2356`
> and `:2292`: `jitter_ 0.5`, `positionSeconds_ 1.0`, `positionSpread_ 0.3`, `pitchSemitones_ 0.0`,
> `pitchSpread_ 0.15`, `envelopeType_ Hann`. The same is true of the two body toggles, whose defaults are
> `continuous_body.h:163-164` (`kDefaultAgcEnabled = true`, `kDefaultResonatorBypass = false`).

**Aether Space (1200–1399)**

| ID | Enum | Type | Plain range | Map | Default | Route |
|---|---|---|---|---|---|---|
| 1200 | `kAetherMixId` | R | 0 … 1 | lin | 0.35 | MB `AetherMix` |
| 1201 | `kAetherSizeId` | R | 0 … 1 | lin | 0.50 | MB `AetherSize` |
| 1202 | `kAetherDensityId` | R | 0 … 1 | lin | 0.70 | AE |
| 1203 | `kAetherDecayId` | R | 0.5 … 60 s | log | 4.0 | AE |
| 1204 | `kAetherFreezeId` | T | off / on | — | off | AE `setFreeze` |
| 1205 | `kAetherDimensionalityId` | R | 0 … 1 | lin | 0.35 | AE |
| 1206 | `kAetherDampingId` | R | 0 … 1 | lin | 0.40 | AE |
| 1207 | `kAetherPreDelayId` | R | 0 … 200 ms | lin | 0.0 | AE |
| 1208 | `kAetherModDepthId` | R | 0 … 1 | lin | 0.25 | AE |
| 1209 | `kAetherModSmoothnessId` | R | 0 … 1 | lin | 0.60 | AE |
| 1210 | `kAetherShimmerOctaveId` | R | 0 … 1 | lin | 0.0 | MB `AetherShimmerOctaveSend` |
| 1211 | `kAetherShimmerFifthId` | R | 0 … 1 | lin | 0.0 | MB `AetherShimmerFifthSend` |
| 1212 | `kAetherBloomSendId` | R | 0 … 1 | lin | 0.0 | MB `AetherBloomSend` |
| 1213 | `kAetherBloomDecayId` | R | 0 … 1 | lin | 0.50 | AE |
| 1214 | `kAetherSpectralDiffusionId` | R | 0 … 1 | lin | 0.0 | AE |
| 1215 | `kAetherSizeBreathDepthId` | R | 0 … 1 | lin | 0.20 | MB `AetherSizeBreathDepth` |
| 1216 | `kAetherTideDepthId` | R | 0 … 1 | lin | 0.20 | MB `AetherDimensionalityTideDepth` |
| 1217 | `kAetherWidthId` | R | 0 … 1 | lin | 1.0 | MB `AetherWidth` |

**Total: 91 registered parameters** (8 shipped + 83 new), routed as:

| Route | Count | IDs |
|---|---|---|
| `MB` macro base | **27** | 200–205, 207, 208; 400, 402; 600, 604; 702–704; 802; 1000, 1001, 1004; 1200, 1201, 1210–1212, 1215–1217 |
| `VP` voice params | **37** | 206, 209, 210; 401, 403, 404, 407; 601–603; 700, 701; 800, 801, 803–812; 1002, 1003, 1005–1007, 1009–1016 |
| `CFG` configure-time | **5** | 408–412 |
| `AE` direct to reverb | **10** | 1202–1209, 1213, 1214 |
| `ENG` direct to engine | **4** | 1, 2, 3, 1008 |
| processor-local | **8** | 0 (master gain); 100–104 (macros → `setMacros`); 405, 406 (sync pair, C-7) |

27 + 37 + 5 + 10 + 4 + 8 = **91**. The `MB` count is exactly the 19 Voice-owned + 8 Aether-owned entries of
`SeraphisMacroTarget` (`seraphis_macro_matrix.h:56-88`) and is **unchanged** by the 2026-08-01 expansion —
none of the eight new controls has a macro row, so all eight are `VP` and the `MB`/`VP` sets stay disjoint
by FR-055.

### C-7. Host-synced morph travel

Phase 3 assigned this to Phase 9 by name (`specs/seraphis-phase3-spectral-morph/spec.md:195-202`: *"Owner:
Phase 9 … which registers the morph-rate parameter with a free/synced selector and a note-value list,
exactly as every other synced rate in this repo is done"*). The DSP half already exists —
`setTravelRate(float journeysPerSecond)` is the ingestion point and the conversion is one division on the
caller's side. Phase 9 therefore adds `kMorphSyncId` + `kMorphSyncNoteId`, and when synced computes
`journeysPerSecond = tempoBPM / (60 · beatsPerJourney)` from `ProcessData::processContext`, clamping into
`[kMinTravelRate, kMaxTravelRate]` before the push. No `dsp/` change.

**The eight note values, and the bar → beat rule, stated here because nothing else in the spec can supply
them.** A previous revision said only *"8 note values"* with default *"1 bar"*, which left the label list,
the `beatsPerJourney` values and the meaning of "bar" all undefined — `dropdown_mappings.h` (FR-015) and
the SC-018 test are both unwritable without them. The table is normative and is the single source
FR-015's `inline constexpr` array transcribes:

| Index | Label (`kMorphSyncNoteLabels`) | `beatsPerJourney` |
|---|---|---|
| 0 | `1/16` | 0.25 |
| 1 | `1/8` | 0.5 |
| 2 | `1/4` | 1.0 |
| 3 | `1/2` | 2.0 |
| 4 | `1 Bar` *(default)* | 1 · `barBeats` (= 4.0 at 4/4) |
| 5 | `2 Bars` | 2 · `barBeats` (= 8.0 at 4/4) |
| 6 | `4 Bars` | 4 · `barBeats` (= 16.0 at 4/4) |
| 7 | `8 Bars` | 8 · `barBeats` (= 32.0 at 4/4) |

**`barBeats` is derived from the host time signature, and the fallback is stated.** The four
bar-denominated entries (4–7) scale with
`barBeats = timeSigNumerator · (4 / timeSigDenominator)` when `processContext.state` carries
`ProcessContext::kTimeSigValid` **and** both fields are strictly positive; otherwise `barBeats = 4.0`
(common time). The four note-denominated entries (0–3) are in beats already and never consult the time
signature. This is the whole rule: no other reading of "bar" is permitted, and the fallback path must be
exercised by SC-018.

`beatsPerJourney` is a `double` and is never zero by construction (the smallest entry is 0.25 at
`barBeats`-independent index 0), so FR-056's division cannot produce a non-finite rate before the clamp.

### C-8. State format version 2

`kCurrentStateVersion` goes `1` → `2`. Layout, little-endian, in registration order, written by
`getState` and read by both `Processor::setState` and `Controller::setComponentState`:

```
          int32 version = 2                                                           4 B
[global]  float masterGain | int32 polyphony | int32 softLimit                       12 B
[macro]   5 × float  (dream, bloom, dissolve, gravity, entropy)                      20 B
          ---- end of a version-1 stream: 36 B, a STRICT PREFIX of a v2 stream ----
[globalv2] int32 seed                                                                 4 B
[cloud]   11 × float (IDs 200-210)                                                   44 B
[morph]   5 × float  (400, 401, 402, 404, 407)                                       20 B
          4 × int32  (403 travelMode, 405 sync, 406 syncNote, 408 stateCount)        16 B
          4 × int32  factoryStateId per slot (409-412)                               16 B
          4 × kSpectralStateBytes (541) serialized SpectralState                   2164 B
[life]    9 × float  (600-604, 701-704) | 1 × int32 (700 envMode)                    40 B
[body]    10 × float | 3 × int32 (800 material, 811 inputAgc, 812 resonatorBypass)    52 B
[atmos]   15 × float (1000-1007, 1009-1015) | 2 × int32 (1008 freeze,
                                                         1016 grainEnvelope)         68 B
[aether]  17 × float | 1 × int32 (1204 freeze)                                       72 B
                                                                          total = 2532 B
```

The 91 parameters account for **73 floats + 18 int32**, plus the version int32 and the four state payloads:
73·4 + 18·4 + 4 + 4·541 = 292 + 72 + 4 + 2164 = **2532**. SC-010 asserts that exact length, so a field
added without a spec change fails the test rather than silently widening the format.

**The eight parameters added by the 2026-08-01 FR-070 expansion are in version 2 from the start. Burning
a version 3 for them is forbidden.** They were decided before any Phase 9 code shipped, so there is no
released version-2 stream for them to be absent from; writing them into the v2 layout now costs nothing,
whereas a v3 would force a second migration path through `Processor::setState` and
`Controller::setComponentState` for a format that never existed in the wild. The `[body]` and `[atmos]`
blocks above are the version-2 layout, not an amendment to it.

**`kSeedId` is written AFTER the macro block, not inside `[global]`. This placement is normative.** A
previous revision inserted `int32 seed` as the fourth field of `[global]`, which is unloadable: Phase 8's
`saveGlobalParams` writes exactly `float | int32 | int32` and `loadGlobalParams` is a **fixed three-field
sequential reader with no version parameter** (`global_params.h:160-183`, read this session). A v2 reader
that consumed a fourth field there would eat `dream`'s four bytes as `seed` on every v1 stream, shift the
whole macro block by one field, and hit EOF before `entropy` — a **shape divergence mid-stream**, which
FR-093's EOF-safety mechanism addresses only for *truncation* and cannot detect at all. Writing the seed
after `[macro]` makes a v1 stream a **strict byte prefix** of a v2 stream, so the existing EOF-safe reader
chain handles the migration with no version-aware branch anywhere. `saveGlobalParams` /
`loadGlobalParams` / `loadGlobalParamsToController` therefore keep their Phase 8 three-field shape
unchanged, and the seed is written and read by a separate, explicitly-positioned pair. The total is
unaffected: 2532 B either way.

Three rules are binding:

- **Every loader stays EOF-safe** in the Phase 8 sense (`global_params.h:166-183`): a short stream leaves
  unread fields at their registered defaults and returns `false`, which is not an error.
- **A version-1 stream (36 bytes) loads correctly** — global + macro are read, everything else stays at
  its registered default, and the result is exactly the Phase 8 sound. This is FR-093 / SC-011.
- **The four `SpectralState` payloads are serialized from the Processor's own copy**, not read back out of
  the DSP — there is no getter to read them back from (`SpectralMorphEngine` exposes `setState` (`:292`),
  `setStateCount` (`:318`) and `getStateCount` (`:443`) and **no per-slot getter**, and stores the slot
  *sanitized* anyway: `:285-315` records that "tiltDbPerOct, inharmonicity and name are structurally
  incapable of reaching the audio path" and the ratios are stored log2-transformed). FR-041b creates that
  Processor-side copy and makes it the single source for both serialization and `applySpectralStates`.

The spectral-state block carries the **full serialized payload**, not just the factory ID, even though
Phase 9 only *selects* factory states. This is what makes the roadmap's *"spectral-state serialization
(Phase 3 round-trip)"* literally true, and it is the only way **Phase 11** — which RQ-1 hands the
authoring mutators to — can make states user-editable without a second format version.

### C-9. Frozen registered types

Phase 8 froze the types at its eight IDs (`plugin_ids.h:71-76`). Phase 9's 83 new IDs are frozen the same
way the moment this phase ships: the *Type* column of C-6 is the record. The specific hazard —
`RangeParameter` ↔ `StringListParameter` swaps at a live ID breaking editor load in DAWs that cache
parameter metadata — applies identically to every new ID, including the two new `T` toggles (811, 812)
and the new `L` dropdown (1016).

### C-10. The `kSeedId` table

`kSeedId` (ID 3) is a **`StringListParameter` of exactly sixteen entries** (C-6, frozen by C-9). Its
contents are decided here rather than left to the implementation, because the registered type and entry
count cannot be revisited after ship.

- **A curated, checked-in table of sixteen well-separated 32-bit constants.** `dropdown_mappings.h`
  (FR-015) holds `inline constexpr std::array<std::uint32_t, 16> kSeedValues` alongside the label array;
  the index→seed mapping is that table and nothing else. It is **not** `index + 1`.
- **Index 0 is pinned to `1u`.** This preserves `kEngineSeed == kReverbSeed == 1u`
  (`plugins/seraphis/src/engine/seraphis_engine_config.h:28-29`) as the registered default, which is what
  keeps SC-002's negative control and SC-022's exact-defaults check valid: a fresh Phase 9 instance seeds
  the engine and the reverb exactly as Phase 8 did.
- **Labels are `"Seed 1"` … `"Seed 16"`** — ordinal, not the numeric constants, so the display never
  implies the raw value is meaningful or editable.
- **Well-separated is a measured property of the table, not a hope.** SC-020 clause 2's spread gate is
  therefore a property of *this table*: the plan renders all sixteen entries at SC-020's pinned operating
  point, records the pairwise seed-to-seed total-variation spread, and — if any pair is too close —
  **re-picks the offending constant and re-measures**. Lowering the gate is not an available remedy.
  Consecutive small integers were rejected on exactly this ground: whether `1, 2, 3 …` decorrelate depends
  on the engine's per-slot seed derivation, and a table removes that dependency instead of testing it.

---

## Functional Requirements

### A. DSP additions (Layer 3, additive only)

- **FR-001** `dsp/include/krate/dsp/systems/seraphis_engine.h` MUST declare a POD
  `struct SeraphisVoiceParams` at namespace scope beside `SeraphisEngineConfig` (`:92-97`), carrying one
  field per `VP`-routed row of C-6, each with a **default member initializer equal to the shipped voice
  default** cited in the Existing-components table. It MUST be trivially copyable and MUST NOT contain any
  field that also appears in `SeraphisMacroTarget` (C-1's split), which a `static_assert`-able field count
  and a unit test both check.
- **FR-002** `SeraphisEngine` MUST gain
  `void applyVoiceParams(const SeraphisVoiceParams& p) noexcept`, public, iterating
  **`v < kMaxVoices`** — all sixteen slots, matching `setSeed` (`seraphis_engine.h:355`),
  `setAtmosphereFreeze` (`:557`) and FR-005 — and calling exactly the forwarders named in C-6's `VP` rows
  on `voices_[v]`. It MUST be allocation-free, lock-free and exception-free, and MUST NOT call
  `setSpectralState` / `setSpectralStateCount` (FR-005 owns those). **The bound is `kMaxVoices` and not
  `getPolyphony()` because a slot idled by a polyphony reduction keeps rendering its tail and keeps being
  summed** — `setPolyphony` records it as an orphan (`:337-349`) and `processStereoBlock` sums
  unconditionally over `kMaxVoices` (`:437`, `:464-486`). See C-2.
- **FR-003** `SeraphisMacroMatrix` MUST gain
  `void setTargetBase(SeraphisMacroTarget target, float base) noexcept`,
  `void resetTargetBases() noexcept` and
  `[[nodiscard]] float getTargetBase(SeraphisMacroTarget) const noexcept`. `setTargetBase` MUST reject a
  non-finite argument by leaving the stored base unchanged, using the class's existing bit-pattern
  `isFiniteBits` helper (`seraphis_macro_matrix.h:685-689`) — **never `std::isnan`**, which `-ffast-math`
  folds away on the macOS leg.
- **FR-004** `evaluateAll()` MUST seed each target from the **overridden** base when one has been set and
  from `kRows`' literal otherwise, leaving every other line of the function unchanged. A default-constructed
  `SeraphisMacroMatrix` MUST evaluate bit-identically to today's, which the existing
  `everyRowSharesOneBasePerTarget` static_assert (`:752`) and SC-002 together pin. `apply()` and
  `computeAetherTargets()` MUST NOT change shape.
- **FR-005** `SeraphisEngine` MUST gain a configure-time fan-out
  `void applySpectralStates(const SpectralState* states, int count) noexcept` that calls
  `voices_[v].setSpectralStateCount(count)` then `voices_[v].setSpectralState(slot, states[slot])` for
  `v < kMaxVoices` — **all sixteen slots**, not `getPolyphony()`, because a slot the allocator hands out
  later must already carry the states. The per-voice gate (`seraphis_voice.h:706-719`) is the *only* guard;
  the engine adds none and swallows no rejection.
- **FR-006** **Every** `dsp/` addition in this section MUST be documented with the layer banner each
  sibling carries (`@par Layer: 3 (systems/)`, `@par Real-Time Safety:`) and MUST introduce **no** include
  of any Layer 4 header. Enumerated so no count is ambiguous — **six groups, thirty-three public
  symbols** (1 + 1 + 3 + 1 + 13 + **14**): (1) the `SeraphisVoiceParams` POD (FR-001);
  (2) `applyVoiceParams` (FR-002); (3) `setTargetBase`, `resetTargetBases`, `getTargetBase` (FR-003);
  (4) `applySpectralStates` (FR-005); (5) FR-070's **thirteen** `SeraphisVoice` forwarders; (6) FR-072's
  **fourteen read-back accessors** (twelve on `ContinuousBody`, one on `SeraphisVoice`, one on
  `SeraphisEngine` — the last added by amendment A6, 2026-08-01, which took this group and this total
  from thirteen and thirty-two). Each has an ODR-sweep row in *New components*.
- **FR-070** `SeraphisVoice` MUST gain exactly **thirteen** new one-to-one forwarders, matching the
  existing facade's "no added clamping" contract (`seraphis_voice.h:638`). Five clear inclusion criterion
  (a) or (b); the remaining eight — the **ATMOSPHERE SET** (#6–#11) and the **BODY SET** (#12–#13) — were
  ruled in by the phase owner on 2026-08-01 under criterion (c). The three CLOUD PER-PARTIAL setters are
  **not** forwarded and belong to Phase 11.
  1. `void setCloudDriftSmoothness(float s) noexcept` → `cloud_.setDriftSmoothness` (`harmonic_cloud.h:513`).
     *Roadmap trace:* line 113 names smoothness as a `BrownianDrift` control — criterion **(a)**.
  2. `void setEnvelopeOffsetSpread(float spread) noexcept` → `cloud_.setEnvelopeOffsetSpread` (`:580`).
     *Roadmap trace:* line 148, *"individual attack/decay offsets"* per partial — criterion **(a)**.
  3. `void setAtmosDriftSmoothness(float s) noexcept` → `atmos_.setDriftSmoothness`
     (`atmosphere_engine.h:844`). *Roadmap trace:* line **245**, per-grain `BrownianDrift` —
     criterion **(b)**. *(Line corrected: 245 is "Pitch drift per grain (`BrownianDrift` again),
     density…"; 246 is its continuation, "(per-grain pan spread + decorrelation via `stereo_utils`)".)*
  4. `void setAtmosDriftRangeSemitones(float st) noexcept` → `atmos_.setDriftRangeSemitones` (`:852`).
     *Roadmap trace:* line **245**, *"Pitch drift per grain"* — criterion **(a)**.
  5. `void setWaypointInterval(double seconds) noexcept` → `morph_.setWaypointInterval`
     (`spectral_morph_engine.h:385`). *Roadmap trace:* line 186, travel *"driven by `SplineTrajectory`"*
     — criterion **(b)**.

  **The ATMOSPHERE SET (criterion (c), 2026-08-01 ruling).** Six one-to-one forwarders into
  `atmos_`, each hitting a setter that already ships a matching getter, so FR-072 creates nothing for
  them and SC-003's blanket `VP` read-back rule applies unmodified:

  6. `void setAtmosJitter(float amount) noexcept` → `atmos_.setJitter` (`atmosphere_engine.h:800`).
     Serves ID **1011**; read back with `getJitter()` (`:804`).
  7. `void setAtmosPositionSeconds(float seconds) noexcept` → `atmos_.setPositionSeconds` (`:807`).
     Serves ID **1012**; read back with `getPositionSeconds()` (`:811`).
  8. `void setAtmosPositionSpread(float spread) noexcept` → `atmos_.setPositionSpread` (`:815`).
     Serves ID **1013**; read back with `getPositionSpread()` (`:819`).
  9. `void setAtmosPitchSemitones(float semitones) noexcept` → `atmos_.setPitchSemitones` (`:822`).
     Serves ID **1014**; read back with `getPitchSemitones()` (`:827`). This is a **fixed**
     transposition and is deliberately distinct from #4's drift *range*; both ship.
  10. `void setAtmosPitchSpread(float spread) noexcept` → `atmos_.setPitchSpread` (`:830`).
      Serves ID **1015**; read back with `getPitchSpread()` (`:834`).
  11. `void setAtmosGrainEnvelope(GrainEnvelopeType type) noexcept` → `atmos_.setGrainEnvelope`
      (`:959`). Serves ID **1016**; read back with `getGrainEnvelope()` (`:963`). The setter is a plain
      store over windows `prepare()` already generated (`:427`), so it is allocation-free at block rate —
      which is what makes it registerable at all.

  **The BODY SET (criterion (c), 2026-08-01 ruling).** Two toggle forwarders into `body_`:

  12. `void setBodyInputAgcEnabled(bool enabled) noexcept` → `body_.setInputAgcEnabled`
      (`continuous_body.h:1276`). Serves ID **811**, default **on** (`kDefaultAgcEnabled`, `:163`).

      **Its interaction with the FR-033a excitation-comp estimator is normative and the plan MUST verify
      it explicitly.** `ee408854` seeded that estimator per material for a fast cold start
      (`kExcitationCompSeed` = 78.8 / 20.6 / 382.2 / 89.1 / 69.9 for Glass / Strings / MetalPlate /
      Chamber / Ice) and added the HOLD clause-5 stationarity gate. Turning the AGC **off** switches all
      of that off at once, by contract and not by accident: `seedLog2For()` returns `0.0f` with the
      in-body note *"the AGC-off branch is not a guard, it is the contract"* (`continuous_body.h:2901-2909`);
      `updateExcitationComp()` early-returns having forced unity and abandoned the measurement window
      (`:3031-3040`); and `controlStep()` sets `rmsGain_ = 1.0f` rather than the tracked
      `kTargetInputRms / max(inputRms_, kRmsFloor)` (`:3686`, and again on the recovery path `:4000`).
      Three consequences bind on this phase:
      (i) a level change on toggling — of the order of the deficit `ee408854`'s message records — is
      **correct behaviour, not a defect**, and no criterion may assert level continuity across this
      toggle;
      (ii) ID 811 is nevertheless in scope for SC-005 clauses 1–3, because the header states the toggle is
      *"absorbed by the drive smoother, so toggling is clickless"* (`:1275-1276`) — the plan must classify
      it under FR-059 with that citation, or move it to class (b);
      (iii) SC-002's negative control is unaffected, because the registered default is **on**, which is
      the shipped state.
  13. `void setBodyResonatorBypass(bool bypass) noexcept` → `body_.setResonatorBypass`
      (`continuous_body.h:1300`). Serves ID **812**, default **off** (`kDefaultResonatorBypass`, `:164`).
      The setter is **self-guarding** (`:1302-1304`) and applies its own 10 ms equal-power ramp at the
      control step, including the mandatory waveguide re-tune on un-bypass — so the forwarder adds
      nothing and the plan must not add a second guard.

  The prefixed names on #1, #3 and #6–#13 are mandatory (see New components). No existing forwarder is
  renamed.
- **FR-072** **The read-back accessors SC-003 depends on.** SC-003's `VP` and `MB` rules assert a pushed
  value against "the matching getter". For **thirteen** of the routed IDs no such getter exists anywhere,
  and SC-023 clause 4 names a **fourteenth** read-back — the soft-limit state (ID 2, `ENG`) — for which no
  *engine-level* route exists (amendment A6, 2026-08-01; the row is last in the table below). Verified
  this session by reading the complete const surfaces:
  `ContinuousBody`'s getters are `continuous_body.h:1450-1534` and contain **only** `getMaterial()`
  (`:1450`) of the eleven body controls — `getDriveGain()` (`:1480`) returns the *smoothed derived* gain
  `exp10Fast(driveLog10.getCurrentValue())` floored at `kMinDriveGain`, **not** the pushed drive; and
  `SeraphisVoice`'s component accessors are exactly `cloud()`, `morph()`, `body()`, `atmos()`
  (`seraphis_voice.h:763-766`) and `orbit()` (`:771`) — there is **no** `growth()`, so
  `GrowthEnvelope::getDuration()` (`growth_envelope.h:149`) is unreachable. The two BODY-SET toggles
  added on 2026-08-01 are in the same position: `agcEnabled_` (`:4217`) and `resonatorBypass_` (`:4218`)
  are private with no accessor anywhere on the class. **The six ATMOSPHERE-SET controls need nothing** —
  `AtmosphereEngine` already ships `getJitter`, `getPositionSeconds`, `getPositionSpread`,
  `getPitchSemitones`, `getPitchSpread` and `getGrainEnvelope` beside their setters. `SeraphisVoice` MUST
  therefore gain **one** accessor, `ContinuousBody` **twelve**, and `SeraphisEngine` **one** (A6) —
  **fourteen** in total, all `[[nodiscard]] … const noexcept`, all pure member reads of values the setters
  already store clamped (the engine's being a one-line forwarder that adds **no** state, so
  `seraphis_engine.h`'s Phase 9 addition outside FR-001–FR-005 stays `const`-only), all additive:

  | New accessor | Returns | Backing member (verified) | Serves ID |
  |---|---|---|---|
  | `SeraphisVoice::growth()` → `const GrowthEnvelope&` | the envelope | `growth_`, alongside the five existing accessors at `seraphis_voice.h:763-771` | 701 (`VP`) |
  | `ContinuousBody::getResonance()` | `float` | `resonance_` (`continuous_body.h:4206`, stored clamped at `:1166`) | 801 (`VP`) |
  | `ContinuousBody::getDamping()` | `float` | `damping_` (`:4207`, `:1175`) | 802 (`MB`) |
  | `ContinuousBody::getKeyTracking()` | `float` | `keyTracking_` (`:4208`, `:1184`) | 803 (`VP`) |
  | `ContinuousBody::getDrive()` | `float` | `userDrive_` (`:4209`, `:1205`) — **distinct from `getDriveGain()`** | 804 (`VP`) |
  | `ContinuousBody::getMix()` | `float` | `mix_` (`:4210`, `:1214`) | 805 (`VP`) |
  | `ContinuousBody::getCloudMix()` | `float` | `cloudMix_` (`:4211`, `:1224`) | 806 (`VP`) |
  | `ContinuousBody::getCloudDecaySec()` | `float` | `cloudDecaySec_` (`:4212`, `:1235`) | 807 (`VP`) |
  | `ContinuousBody::getCloudSize()` | `float` | `cloudSize_` (`:4213`, `:1246`) | 808 (`VP`) |
  | `ContinuousBody::getCloudDamping()` | `float` | `cloudDamping_` (`:4214`, `:1259`) | 809 (`VP`) |
  | `ContinuousBody::getWidth()` | `float` | `width_` (`:4215`, `:1269`) | 810 (`VP`) |
  | `ContinuousBody::isInputAgcEnabled()` | `bool` | `agcEnabled_` (`:4217`, stored at `:1278`) | **811** (`VP`) |
  | `ContinuousBody::isResonatorBypass()` | `bool` | `resonatorBypass_` (`:4218`, stored at `:1305`) | **812** (`VP`) |
  | `SeraphisEngine::getOutputSaturation()` | `float` | **a pure `const` forwarder**, `satL_.getSaturation()` (`tape_saturator.h:283-285`) — no new member | **2** (`ENG`) |

  **Because the setters clamp before storing, the accessor returns the *clamped* value.** SC-003's `VP`
  and `MB` thresholds are therefore stated against the **clamped** pushed value, and every C-6 plain range
  for these thirteen `VP`/`MB` IDs already lies inside the component's own clamp range
  (`continuous_body.h:122-160`), so the two coincide for every value the parameter can produce. The two
  `bool` accessors are exact by construction — there is nothing to clamp.

  **`isResonatorBypass()` returns the requested state, not the ramp position.** `setResonatorBypass`
  stores `resonatorBypass_` immediately and then ramps `bypassPos_` over 10 ms (`:1300-1322`, `:2369`).
  SC-003 asserts the **stored request**, which is what the parameter pushed; the ramp is SC-005's subject,
  not SC-003's. An accessor returning `bypassPos_` would conflate the two and would fail SC-003 for a
  correct implementation on the first block after the push.

  ODR sweep run this session: none of the thirteen names exists on its class — `ContinuousBody`'s getter
  list is reproduced above in full, and `SeraphisVoice`'s at `:763-771`. `getDrive` is deliberately
  distinct from the existing `getDriveGain` (`:1480`); `isInputAgcEnabled` / `isResonatorBypass` take the
  `is` prefix of the class's existing `isCrossfading()` (`:1500`) because they return `bool`.
- **FR-071** No `dsp/` file may be modified except the three named by FR-001/FR-002/FR-003/FR-005/FR-070
  (`seraphis_engine.h`, `seraphis_macro_matrix.h`, `seraphis_voice.h`) and — **by this phase's single
  explicit carve-out** — `continuous_body.h`, which FR-072 extends with **twelve** `const` accessors and
  nothing else (the thirteenth is `SeraphisVoice::growth()` and the fourteenth
  `SeraphisEngine::getOutputSaturation()`, both on files already named above). No existing DSP signature may change, no existing member may move, and no non-`const` behaviour
  may be added on the carve-out. The carve-out is admitted on exactly the ground FR-041a is: *a success
  criterion may not depend on introspection that no requirement creates.* The fourteen accessors are
  additive `const` surface, so the layer story (`continuous_body.h` stays Layer 3, includes unchanged) and
  the ODR story are both unchanged, and SC-002 is the standing regression gate that they alter nothing.
  Verified by a diff review in the compliance pass, which MUST record the full list of touched `dsp/`
  files.

  **One VALUE change on a named file, recorded here because no other FR authorises it** (2026-08-01).
  `SeraphisEngine::kSumGainSmoothMs` moves **20 ms → 100 ms**. It is not a signature change, not a
  member move and not new behaviour — the delivery shape (read once, held for a whole control chunk,
  `seraphis_engine.h:1079-1080`) is byte-for-byte what it was, and `prepare()` still snaps — but it
  IS a Phase 7 constant and this clause is where that is admitted. It was forced by **SC-005 on ID 1**,
  which measured **2.651 ×** against the `1.5 ×` bound: the held value reaches the bus as a staircase
  whose first stair was 28.35 % of a polyphony 1 → 2 sum-gain change (1.0 → 0.7071), i.e. 8.3 % of the
  bus level in one sample. SC-005's remedy rule forbids exempting the ID and forbids loosening the
  bound, and the smoother that covers this row lives in `dsp/`, so there is no plugin-side remedy.
  100 ms is the measured knee (20 → 2.651, 100 → 1.143, 200 → 1.143, 300 → 1.144, 500 → 1.147).

### B. Parameter IDs and packs

- **FR-010** `plugins/seraphis/src/plugin_ids.h` MUST extend `enum ParameterIDs` with exactly the 83 new
  IDs of C-6, in band order, and MUST update the reserved-range comment (`:46-55`) to mark bands 200–1399
  as **SHIPPED** and 1400+ as Phase 10. That rewrite MUST also correct the comment's stale roadmap
  citation at `:46` — the band list is roadmap lines **399–401**, and the "start at 0" decision line
  **396**, both post-FR-058-amendment (C-5).
- **FR-011** `plugin_ids.h` MUST declare one `constexpr ParamID k<Section>ParamRangeEnd` per band, in the
  shape of the existing `kGlobalParamRangeEnd` / `kMacroParamRangeEnd` (`:79-80`), so FR-040's dispatch
  stays a range ladder and never becomes a 91-case switch.
- **FR-012** `plugin_ids.h` MUST set `constexpr Steinberg::int32 kCurrentStateVersion = 2;` and MUST retain
  a named constant for version 1 so the migration path in FR-093 is expressed against a symbol, not a
  literal.
- **FR-013** `plugin_ids.h`'s frozen-type note (`:71-76`) MUST be extended to enumerate the registered type
  of every one of the 91 IDs, grouped by type, per C-9.
- **FR-014** Six new headers MUST be created in `plugins/seraphis/src/parameters/` — `cloud_params.h`,
  `morph_params.h`, `life_mod_params.h`, `body_params.h`, `atmosphere_params.h`, `aether_params.h` — each
  declaring a `struct <Section>Params` of `std::atomic<>` fields with initializers equal to the C-6 defaults,
  and each exposing the **six-function contract** the Phase 8 packs already implement
  (`global_params.h:72, 102, 129, 160, 167, 189`):
  `handle<Section>ParamChange`, `register<Section>Params`, `format<Section>Param`, `save<Section>Params`,
  `bool load<Section>Params`, `template<typename SetParamFunc> load<Section>ParamsToController`.
- **FR-015** `plugins/seraphis/src/parameters/dropdown_mappings.h` MUST hold **one** table per dropdown
  (body material, morph travel mode, morph state count, factory spectral state, envelope mode, sync note
  value, seed, **grain envelope**) as `inline constexpr` arrays of `const Steinberg::Vst::TChar*`, plus
  `inline` index↔enum converters. Registration (FR-014) and formatting MUST both read those tables, so a
  label list cannot exist in two places and drift — the failure the shared pointer+count overloads
  (`parameter_helpers.h:97, 118`) exist to prevent. Two tables carry extra obligations:
  - **grain envelope** — six labels in the declaration order of `GrainEnvelopeType`
    (`dsp/include/krate/dsp/core/grain_envelope.h:14-22`), with a `static_assert` tying the array size to
    `AtmosphereEngine::kEnvelopeTypeCount` (`atmosphere_engine.h:197`) so an enum extension cannot
    silently desynchronise the list from the windows `prepare()` generates;
  - **seed** — `inline constexpr std::array<std::uint32_t, 16> kSeedValues` **beside** the sixteen
    `"Seed 1" … "Seed 16"` labels, per **C-10**, with `kSeedValues[0] == 1u` asserted at compile time so
    the Phase 8 default cannot drift.
- **FR-016** Every dropdown MUST be registered through
  `Krate::Plugins::createDropdownParameterWithDefault` (`parameter_helpers.h:47` or the pointer+count
  overload at `:118`), never a hand-rolled `StringListParameter`, and never a `RangeParameter` with a step
  count.
- **FR-017** Every `log`-mapped row of C-6 MUST use `Krate::Plugins::logMapFromNormalized` /
  `logMapToNormalized` (`parameter_helpers.h:80, 85`) with the stated `mn`/`mx`, never a locally
  reimplemented `pow`/`log` pair.
- **FR-018** Every `handle<Section>ParamChange` MUST clamp into the plain range of C-6 **before** storing,
  so a hostile or corrupt normalized value cannot reach the DSP setters as a non-finite or out-of-range
  number. This duplicates the DSP-side clamp deliberately: the plugin's stored value is compared against
  itself by FR-042's change detector, and an unclamped store would make the detector fire forever — the
  exact failure `clampPolyphony` was introduced for (`global_params.h:52-59`).

- **FR-019** **No denormalization anywhere in the six new packs may read the sample rate.** Every
  time-domain parameter is stored in seconds, milliseconds, Hz, semitones or grains-per-second and is
  converted to samples **inside** the DSP component that owns the rate. Verified as a review item with
  file:line citations recorded in the compliance pass, and mechanically by the absence of any
  `sampleRate` / `sampleRate_` / `getSampleRate` token in `plugins/seraphis/src/parameters/*.h`
  (a `grep` the compliance pass records the output of). *This was previously a clause inside SC-019 — a
  review action inside a measurement, which is not a success criterion.*

  **This FR deliberately has no success criterion, and the Traceability table says so rather than
  pointing at one that does not cover it.** The `grep` command and its **verbatim output** MUST be pasted
  into this phase's `compliance.md`; an empty result set is the pass condition, and a claim without the
  pasted output is not evidence.

### C. Processor wiring

- **FR-040** `Processor::processParameterChanges` MUST dispatch by **ID range** using FR-011's constants,
  extending the existing ladder (`processor.cpp:549-554`) and preserving its two established behaviours:
  the last point of each queue is taken (`getPoint(numPoints - 1)`, `:544`), and an ID outside every band is
  ignored rather than misrouted.
- **FR-041** `Processor` MUST own one instance of each of the six new packs as a by-value member. The
  `static_assert(sizeof(Processor) < 64u * 1024u)` (`processor.h:104`) MUST still hold.
- **FR-041a** `Processor` MUST expose the **test-only read surfaces this phase's criteria depend on**,
  under the existing banner *"Test-only read surfaces (NEVER called from `process()`)"* (`processor.h:48-55`)
  and in the shape of the one accessor already there (`setPolyphonyCallCountForTest()`, `:53-55`). Each is
  `[[nodiscard]]`, `noexcept`, and reachable from no audio-thread path:
  - `std::size_t applyVoiceParamsCallCountForTest() const` and
    `std::size_t applySpectralStatesCallCountForTest() const` — SC-007 counts *successful applications*
    through these;
  - `std::size_t applyAetherParamsCallCountForTest() const` and
    `std::size_t setTargetBasePushCountForTest() const` — the cadences FR-042's **second** counter pair,
    FR-043 and FR-044 mandate. Without them three on-change-only requirements have no criterion and no
    seam, and a per-slice `applyAetherParams` — the cadence C-3 justifies at length, because three of the
    four non-`applyControl` reverb setters do real per-call work (`aether_reverb.h:2230-2237`,
    `:2268-2273`, `:2320`, `:2328`) — would ship green. `setTargetBasePushCountForTest()` counts
    **`setTargetBase` invocations**, not changed targets, so a per-slice re-push is visible;
  - `const Krate::DSP::SeraphisMacroMatrix& macroMatrixForTest() const` — the only route to
    `getTargetBase` (`macros_` is private, `processor.h:78`, and Phase 8 exposes only `engineForTest()` /
    `reverbForTest()`, `:51-52`);
  - `bool spectralStatesPendingForTest() const` — FR-046's retry flag, which SC-013 is written against;
  - `const Krate::DSP::SpectralState& spectralSlotForTest(int slot) const` — FR-041b's authoritative
    copy, which SC-012 compares field-by-field;
  - `std::size_t engSeedPushCountForTest() const`, `std::size_t engPolyphonyPushCountForTest() const`,
    `std::size_t engSoftLimitPushCountForTest() const` and
    `std::size_t engFreezePushCountForTest() const` — **one per `ENG` value** (amendment A7,
    2026-08-01), which is what gives FR-045's "on change only" a criterion. The polyphony one is a named
    alias of the existing `setPolyphonyCallCountForTest()` (`:53-55`), not a second counter.

  This FR exists because three success criteria previously named introspection that no requirement
  created. The spec already establishes the practice for the DSP side (FR-049 makes `applyAetherParams`
  a free function *"so a test can drive it directly"*); this is the same decision on the plugin side.
- **FR-041b** `Processor` MUST own `std::array<Krate::DSP::SpectralState, 4> spectralSlots_{}` as the
  **single authoritative copy** of the four spectral states, and it MUST be:
  - **populated** from `makeFactoryState(...)` (`spectral_state.h:373`) — at `setupProcessing()` for all
    four slots, and whenever a `CFG` dropdown (IDs 409–412) changes;
  - the **only argument source** for `applySpectralStates` (FR-005).

  It is **not** a serialization source and **not** a deserialization destination: FR-091 deserializes
  into the staging ring (clause 2 below) and FR-090/FR-092 serialize from the message-thread-safe sources
  of clause 5 (amendment A5, 2026-08-01 — this bullet previously claimed both roles for
  `spectralSlots_`, which is audio-thread-owned).

  This is required because nothing can read a slot back out of the DSP: `SpectralMorphEngine` has no
  per-slot getter (its const surface is `spectral_morph_engine.h:392-456`), `SeraphisVoice` exposes only
  the two setters (`seraphis_voice.h:706, :713`), `SeraphisEngine::getVoice()` is const
  (`seraphis_engine.h:696`), and `setState` stores the slot **sanitized** — log2-transformed ratios, and
  `tiltDbPerOct` / `inharmonicity` / `name` discarded (`spectral_morph_engine.h:285-315`). Without a
  processor-side copy, nothing in the plugin holds 2164 of C-8's 2532 bytes at all. It cannot live in a
  FR-014 pack: those are `std::atomic<>` fields and a 541-byte `SpectralState` is not lock-free.
  `Processor` MUST additionally own the immutable factory table
  `std::array<Krate::DSP::SpectralState, 5> factoryStates_`, **filled in the constructor** (clause 5),
  which is what lets `getState()` reach the same content without reading `spectralSlots_`.

  **Threading — the message thread never writes `spectralSlots_` directly.** A 540-byte struct has no
  atomic access, so an argument that "`setState` and `process()` are already serialized" would rest on
  host behaviour rather than on the code, and hosts (and pluginval's own stress paths) do call `setState`
  concurrently with `process()`. Decided 2026-08-01 (Q8) and amended the same day (A5), the contract is
  therefore a **staging ring plus an index handoff**, and it is normative:
  1. `Processor` owns a **three-deep** staging ring
     `std::array<std::array<Krate::DSP::SpectralState, 4>, 3> spectralSlotsStaging_{}`, a published index
     `std::atomic<int> spectralSlotsHandoff_{-1}`, an in-flight index
     `std::atomic<int> spectralSlotsConsuming_{-1}`, and a message-thread-only `int stagingWriteCursor_`.
     *(Amendment A5, 2026-08-01: a single buffer behind a `std::atomic<bool>` has no writer-side
     interlock — a second `setState()` may write staging while the audio thread's copy of the first is
     still in flight, which pluginval's state-stress paths and any host doing rapid preset changes hit.
     Two buffers do not suffice either, because one may be published-and-unconsumed while the other is
     being copied. Spinning the message thread on the flag is not available, because `setState()` may
     legally arrive with the audio thread stopped.)*
  2. **Message-thread writes land in the staging ring only.** `setState()` picks `w`, the first of
     `{cursor, cursor+1, cursor+2} mod 3` equal to **neither** `spectralSlotsHandoff_` **nor**
     `spectralSlotsConsuming_` (both acquire-loaded; at most two indices are excluded, so the search
     always terminates), deserializes the four payloads into `spectralSlotsStaging_[w]`, stores `w` into
     `spectralSlotsHandoff_` with **release** ordering, and advances the cursor. It never touches
     `spectralSlots_`.
  3. **The audio thread consumes the handoff at the top of `process()`**, before the first slice and
     before FR-046's push: acquire-load `spectralSlotsHandoff_`; when it is `>= 0`, store that index into
     `spectralSlotsConsuming_` (release) **first**, then store `-1` into `spectralSlotsHandoff_`
     (release), then copy `spectralSlotsStaging_[idx]` into `spectralSlots_` (a 2.1 KiB
     `memcpy`-equivalent of a trivially copyable POD — allocation-free, lock-free, bounded), then store
     `-1` into `spectralSlotsConsuming_`; finally set `spectralStatesPending_`. **That store order is the
     whole interlock**: the message thread loads `handoff` before `consuming`, so the dangerous state —
     both read as `-1` while buffer `idx` is mid-copy — is unreachable.
  4. **`spectralSlots_` itself is audio-thread-only** thereafter: written by
     `processParameterChanges()` (a `CFG` change) and by clause 3, read by FR-046's push. There is no
     concurrent access left to reason about.
  5. **`getState()` serializes from the *published* staging buffer while a handoff is outstanding, and
     from `factoryStates_[morphParams_.slot[i]]` otherwise. It MUST NOT read `spectralSlots_`**, which
     is audio-thread-owned (amendment A5, 2026-08-01; this clause previously named `spectralSlots_` as
     the no-handoff source). A save issued between a `setState` and the next `process()` therefore still
     writes back what was loaded, and a save issued while a host automates a `CFG` dropdown — which
     pluginval stress-tests — is no longer an unsynchronised message-thread read of a non-atomic array
     the audio thread writes, whose visible symptom is a **torn `SpectralState` in the saved preset**.
     The substitute is **bitwise identical by construction**: in Phase 9's factory-selection-only design
     every slot is a `makeFactoryState(...)` result and that function is *"Deterministic and stateless"*
     (`spectral_state.h:349-351`), so FR-094's byte-identity is unaffected. **`factoryStates_` is built
     at construction, not at prepare** — `makeFactoryState` takes no sample rate, and deferring it leaves
     the table all-zero for the whole window between `initialize()` and the first `setupProcessing()`, a
     window in which a host may legally call `getState()`; a zeroed `SpectralState` **passes**
     `isValidSpectralState` (`spectral_state.h:82-145`), so that window would save four *valid, empty*
     payloads which reload cleanly into four silent slots — a silent corruption no criterion looks for,
     rather than the rejection it would be mistaken for.

  The two `std::atomic<int>`s are **not** a new synchronisation primitive in the sense FR-048 and this FR
  forbid — neither is a lock or an allocation, both are lock-free on every target, and the design already
  carries a `bool` flag of the same role. With them, FR-041b's "no new synchronisation primitive is
  introduced and none is permitted" is **unconditionally true** rather than conditional on the host.
  `sizeof(Processor) < 64 KiB` (`processor.h:104`) still holds with room to spare: the staging ring is
  **three** further 2.1 KiB copies, for ~8.4 KiB total. One copy is **4 × 540 B ≈ 2.1 KiB**
  (`sizeof(SpectralState)` is 256 + 256 + 16 + 4 + 4 + 4 = **540 B**, `spectral_state.h:57-63`; the
  **541** B figure used elsewhere in this spec is the *serialized* form, which adds the format-version
  byte — `kSpectralStateBytes = 1 + 4 + 4 + 4 + 256 + 256 + 16`, `spectral_state.h:185`. A previous
  revision wrote "4 × ~1.1 KiB ≈ 4.4 KiB", conflating the two and doubling both).
- **FR-042** `Processor` MUST maintain a `std::size_t voiceParamGeneration_` incremented by
  `processParameterChanges` whenever a **`VP`-routed** parameter is written, and a
  `lastAppliedVoiceParamGeneration_` compared once per `process()` call **before the first slice**. When
  they differ, the processor builds a `SeraphisVoiceParams` from the atomics and calls
  `engine_->applyVoiceParams(...)` exactly once, then equalises the trackers. A **second** counter pair
  of the same shape (`aetherParamGeneration_` / `lastAppliedAetherParamGeneration_`) covers the ten
  `AE`-routed values and drives FR-044's `applyAetherParams` push from the same pre-slice position; the
  two are separate so an `AE` change does not force a 37-setter × 16-voice fan-out and vice versa. This is
  C-3, and it is the same on-change-only shape as `pushGlobalParams()` (`processor.cpp:585-603`).

  **Three amendments to "on change only", all normative, all decided 2026-08-01 (C-3):**
  1. **Settling push (Q1), on the absolute 64-sample control-chunk grid (amendment A1, 2026-08-01).**
     While any **class-(b)** smoother (FR-059) is un-settled, the push that owns it —
     `applyVoiceParams` for a `VP` row, `setTargetBase` for an `MB` row — MUST run on the engine's
     **absolute 64-sample control-chunk grid** (`SeraphisEngine::kControlChunkSamples`), regardless of
     the generation comparison. To reach that grid, `process()` MUST cap its slice length at the
     distance to the next grid boundary **while — and only while — any class-(b) smoother is
     un-settled**, and MUST stop as soon as every class-(b) smoother reports settled, at which point the
     slice structure is exactly Phase 8's. The grid is **absolute across slices and across `process()`
     calls**, so the delivered ramp is host-block-size independent by construction. The processor MUST
     expose the settled/un-settled condition to the same pre-slice decision that reads the generation
     counters. A per-**slice** ramp — whose boundaries are event-driven and move with MIDI placement —
     remains **forbidden**; a fixed 64-sample rule is not one, because no host block size and no
     performer's timing can move it.

     *Rationale, recorded because this clause previously mandated a once-per-block push and forbade a
     per-slice one in the same breath.* At the class-(b) time constant FR-059(b) clause 2 mandates (20 ms time-to-99 %, i.e.
     `tau = 4 ms = 192 samples` at 48 kHz), a once-per-block push delivers `1 − e^(−512/192)` =
     **93.0 %** of the step in a single jump at 512 samples and **99.99 %** at 2048. That is a
     staircase, not a ramp: it removes essentially none of the discontinuity SC-005 clause 3 measures,
     and it would compare a 1.000·D bypassed step against a 0.930·D smoothed one — a ratio of **1.075**
     against SC-005's `1.5 ×` bound — making that criterion's positive control (b) **structurally
     incapable of failing**. It was also host-block-size dependent, which is the defect
     `processor.cpp:634-636` names in so many words.
  2. **Synced-tempo dirty (Q3).** When `kMorphSyncId` is on, `process()` MUST recompute the synced travel
     rate per FR-056 and MUST increment `voiceParamGeneration_` when the recomputed rate differs from the
     last pushed one by more than the plan's stated epsilon. Tempo is not a parameter, so without this a
     tempo ramp never reaches `setTravelRate`.
  3. **`pushAllSurfaces()` (Q2).** `setState()` MUST invalidate both counter pairs through FR-047's
     shared helper — by raising that helper's release-store request, per amendment A4 — rather than
     relying on a subsequent parameter edit.
- **FR-043** `Processor` MUST push the 27 `MB`-routed values via `macros_.setTargetBase(...)` on change
  only, and MUST push `MacroParams` via `macros_.setMacros(...)` on change only. Both are ordinary
  scalar stores; neither is allowed inside the per-slice loop.
- **FR-044** `Processor::renderSlice` MUST keep `macros_.apply(*engine_)` and
  `applyAetherTargets(...)` at their existing positions and cadence (`processor.cpp:623-624`) —
  every slice. The new `applyAetherParams(*reverb_, aetherParams_)` for the ten non-macro reverb controls
  MUST be called **on change only**, from the same pre-slice position as FR-042's voice-param push and
  under FR-042's second generation-counter pair — **not** every slice (C-3: four of the eighteen reverb setters do
  real per-call work outside the `applyControl` smoother model). Steps 3–6 of the chain (`:627-676`)
  MUST NOT move.
- **FR-045** The four `ENG`-routed parameters MUST be pushed from `pushGlobalParams()` on change only,
  extending the existing tracker pattern: `setPolyphony` and `setOutputSaturation` are already there
  (`processor.cpp:589, 599`); `setAtmosphereFreeze` (`seraphis_engine.h:551`) and the seed pair
  (`setSeed` on both engine `:353` and reverb `:2361`) are added.

  **The four `ENG` push counts MUST be observable through FR-041a test-only accessors, one per value**
  (amendment A7, 2026-08-01): `engSeedPushCountForTest()`, `engPolyphonyPushCountForTest()`,
  `engSoftLimitPushCountForTest()` and `engFreezePushCountForTest()`. Without them "on change only" has
  no criterion on this surface — a `ENG` push that ran every block, or one value's push firing on
  another value's change, would ship green. `engPolyphonyPushCountForTest()` is a **named alias** of the
  existing `setPolyphonyCallCountForTest()` (`processor.h:53-55`), not a second counter. SC-007's `ENG`
  row is what consumes all four.
- **FR-046** The five `CFG`-routed parameters (state count + four state slots) MUST be applied through
  `engine_->applySpectralStates(...)` (FR-005), and the processor MUST NOT attempt to detect voice
  quiescence itself — the per-voice gate is authoritative (`seraphis_voice.h:706-719`). The application is
  driven by a **pending flag, not by the generation counter alone**:
  1. `Processor` maintains `bool spectralStatesPending_`, **set** by `processParameterChanges` on any
     write to a `CFG` parameter and at `setupProcessing()` (FR-047) — which sets the flag and pushes
     nothing itself, so the prepare-derived application and the retry are the same code path.
  2. While it is set, `process()` calls `applySpectralStates(...)` **once per `process()` call**, before
     the first slice.
  3. It is **cleared only when every targeted voice accepted the write**, determined by reading
     `getRejectedConfigureTimeCallCount()` (`seraphis_voice.h:720`) across all `kMaxVoices` before and
     after the call: unchanged total ⇒ clear; increased ⇒ leave set and retry next block.
  4. A rejected application MUST leave the parameter atomics unchanged.

  **The retry is not optional.** A previous revision said the application happens "only when their
  generation changes" *and* that "a rejected application … [lets] the next quiescent moment retry" — but
  nothing re-triggered a push at that moment, so SC-013's "after the note finishes, the next application
  succeeds" was unreachable, and FR-005's per-voice gate would otherwise leave the sixteen voices holding
  **different** spectral states (accepted on quiescent slots, rejected on sounding ones) with no
  requirement that they ever converge. The pending flag is what makes them converge, and clause 3 is what
  makes "converge" checkable.
- **FR-047** `setupProcessing()` MUST seed **all** pack-derived engine state from the current parameter
  atomics before the first block, the way FR-023 clause 2 of Phase 8 already seeds polyphony: the macro
  bases, the voice params and the reverb params are all pushed once at prepare, and every on-change
  tracker is reset to what was pushed. `setState()` may legally arrive **before** `setupProcessing()`.

  **That seeding lives in exactly one helper, `pushAllSurfaces()`, and both entry points reach the same
  body (decided 2026-08-01, Q2; the entry mechanism amended 2026-08-01, A4).** `setupProcessing()` calls
  the helper **directly**, with the audio thread stopped. `setState()` MUST NOT call it: it MUST raise a
  **single release-store request**, which `process()` consumes at the top of the next block **before
  `pushGlobalParams()`** and before the first slice. The helper body is shared, it runs on the audio
  thread (or with the audio thread stopped), and it is **the only place the on-change trackers are
  invalidated**. It performs, in this order:
  1. bump `voiceParamGeneration_` **and** `aetherParamGeneration_`, and reset
     `lastAppliedVoiceParamGeneration_` / `lastAppliedAetherParamGeneration_` to a **sentinel** that
     cannot compare equal — so the next `process()` pre-slice pass rebuilds and pushes both;
  2. force-push the **27 `MB` bases** via `setTargetBase(...)` and `MacroParams` via `setMacros(...)`,
     bypassing (or resetting to the same sentinel) FR-043's on-change trackers;
  3. force-push the **four `ENG` values** through the `pushGlobalParams()` tracker pattern, likewise
     reset to a sentinel — **subject to the scope rule below**, which raises the seed and polyphony
     sentinels on the re-prepare path only;
  4. raise `spectralStatesPending_` — on the re-prepare path unconditionally, and on the preset-load
     path only when a slot's factory selection actually differs from the last pushed one, since a raise
     that changes nothing costs 16 voices × 4 slots of re-sanitization for a state every voice already
     holds — and push **nothing** spectral itself (the exception clause below). A preset that *does*
     change a slot still delivers it, because FR-041b clause 3's handoff consume raises the flag on its
     own.

  **The helper takes one argument — `SurfaceInvalidation { Reprepared, PresetLoad }` — and it
  distinguishes two *situations*, not two bodies (A4).** After a **re-prepare** the DSP objects really
  were re-initialised, so every surface is re-pushed unconditionally. After a **preset load** the atomics
  already carry the new values and the ordinary compare-against-tracker path delivers whatever changed;
  forcing an unchanged value through a setter is a gratuitous discontinuity. The **seed and polyphony
  sentinels are therefore raised on the `Reprepared` path only**: forcing an unchanged seed through
  `setSeed()` is the drift/tide discontinuity `aether_reverb.h:2351-2358` documents, and forcing an
  unchanged polyphony re-arms `sumGain_` and walks the allocator's excess-slot loop
  (`seraphis_engine.h:321-350`) for nothing. Both values still reach the DSP on the `PresetLoad` path,
  because `setState()` writes their atomics **before** the release store and `pushGlobalParams()`'s
  ordinary compare sees the change.

  **Why `setState()` may not call the helper itself (A4).** `setState()` can legally run concurrently
  with `process()` — the *Edge cases* → *State* bullet says so, and pluginval's stress paths do it — so
  writing the ~40 tracker scalars and calling engine setters from the message thread would be a data race
  on every one of them. The release-store request is one atomic write, and it is the only thing the
  message thread does. *(This clause and FR-091 previously said `setState()` calls `pushAllSurfaces()`
  directly and named it one of "its only two callers".)*

  **Why `setState()` must trigger it.** `setState()` writes 90 of the 91 stored values through the pack
  loaders and increments **no** generation counter; FR-042's counters are incremented by
  `processParameterChanges` only. Without this clause a preset loaded into an already-prepared processor —
  the ordinary case, every host preset switch — would change 90 stored values and reach the DSP with
  **none** of them until the user next touched a control, and no criterion would have caught it (SC-010 is
  a byte round-trip; SC-011 checks registered defaults). Relying on the host to re-send every parameter
  change after a `setState` is **not** permitted: VST3 does not guarantee it. **SC-023** is the criterion.

  **One helper, not two code paths.** The prepare-time seeding and the preset-load seeding are the same
  sequence and MUST NOT be written twice; a divergence between them is the failure mode this clause
  exists to prevent.

  **The spectral states are the one exception, and the ordering is normative because SC-007 counts it.**
  `setupProcessing()` **sets `spectralStatesPending_` and performs no `applySpectralStates` call of its
  own**; the single prepare-derived application is the **first in-`process()` push** that FR-046 clause 2
  mandates, which succeeds immediately because a freshly prepared voice is configurable
  (`isConfigurable()` = `!hasSounded_ || isFinished()`, `seraphis_voice.h:831`) and therefore clears the
  flag. *(A previous revision listed "the spectral states" among the prepare-time pushes **and** left
  FR-046 clause 2 unconditional, which produces two successful applications before the first `process()`
  returns and makes SC-007's "exactly once" unsatisfiable. Exactly one of the two may push; this clause
  picks the in-`process()` one, because it is the path that must exist anyway for the retry.)*

  **No polyphony special case is needed.** A previous revision required `kPolyphonyId` to also dirty the
  voice-param generation, because `applyVoiceParams` was bounded at `getPolyphony()` and a newly-allocatable
  slot would otherwise sound with prepare-time defaults. FR-002's `kMaxVoices` bound removes the hazard at
  its source, in both directions (a shrink's orphan tail and a grow's new slot). `kPolyphonyId` remains an
  `ENG`-routed FR-045 push and nothing more.
- **FR-048** Nothing added by this phase may allocate, lock, throw or do I/O on the audio thread. Every new
  push path is a sequence of `noexcept` scalar setters over pre-existing objects.
- **FR-049** `applyAetherParams(Krate::DSP::AetherReverb&, const AetherParams&) noexcept` MUST be added to
  `plugins/seraphis/src/engine/seraphis_engine_config.h` as a **free function** beside the existing
  `applyAetherTargets` (`:93`), for the same stated reason: the reverb has no getters for these controls, so
  a free function is the only surface a test can drive directly with non-neutral values.
- **FR-050** The macros MUST cease to be inert. `plugins/seraphis/src/parameters/macro_params.h`'s banner
  (`:5-9`) MUST be rewritten to describe the Phase 9 wiring, and `MacroParams` MUST reach
  `SeraphisMacroMatrix::setMacros` (`seraphis_macro_matrix.h:599`) exactly as FR-043 specifies.
- **FR-051** Phase 8's SC-023 negative control (macros inert) is **superseded** by SC-004 (macros audibly
  effective). The obsolete assertion MUST be deleted, not left asserting the opposite of the shipped
  behaviour.
- **FR-055** Macro-owned and deep-parameter values MUST compose per C-1 and MUST NOT be summed twice: a
  `MB`-routed parameter is written **only** through `setTargetBase` and **never** also through
  `SeraphisVoiceParams`. FR-001's field-set exclusion is what makes this checkable by construction.
- **FR-057** **CPU budgets are requirements of this phase, not aspirations** (roadmap line 553). Two
  budgets bind, both at 48 kHz / block 512 on an idle reference machine, both measured by `[.perf]` tests
  with a checked-in baseline in the `ceil(worst × 1.05)` shape Phase 7 uses
  (`specs/seraphis-phase7-voice-engine/spec.md:1418-1427`):
  1. **Push machinery.** The Phase 9 parameter-push path costs **≤ 0.05 % of one core** in steady state
     (no parameter changing, **constant tempo, and every class-(b) smoother settled** — the state C-3's
     three amendments each exit into) and **≤ 0.50 % of one core** in the worst case (all 91 parameters
     changing on every block). Measured directly as a microbenchmark of the push sequence, per SC-008 — never by
     subtracting two whole-chain renders, whose run-to-run spread is ~34× the quantity.
  2. **Full-poly ceiling, unbroken.** With the whole parameter surface engaged at the pinned non-default
     operating point of SC-009, the composed chain at Phase 7's pinned scenario (polyphony **8**, all
     eight voices sounding, atmosphere frozen, `AetherReverb` at RA-1 row (c)) stays **≤ 25 % of one
     core** — Phase 7's SC-001 ceiling, kept in full, at the voice count Phase 7 formally deviated to
     (`spec.md:1390-1394`). The **16-voice figure MUST also be measured and recorded** as a non-gating
     number, and it is one of the two figures **FR-058 clause 1 writes into the roadmap amendment**. The
     deviation from the roadmap's stated *"16 voices"* (line 313) is not this phase's to make silently:
     FR-058 amends the roadmap in place, in the shape the Phase 5 budget amendment already used on that
     document (`specs/Seraphis-roadmap.md:250-254`). Until that edit lands, the roadmap and this spec
     disagree — and the repo's own precedent is that the amendment goes into the roadmap, not only into
     the phase spec.

  **Amendment A12 (2026-08-02) — the SC-009 baseline is ceiling-derived, because `ceil(worst × 1.05)`
  collides with the 25 % ceiling.** The `ceil(worst × 1.05)` recording convention this FR mandates is a
  *recording* convention; the *binding* constraint is the 25 % ceiling, reached through
  `kRegressionFactor` (baseline × 1.15 ≤ 25 %). On the seven-run cold-machine dataset of 2026-08-02
  (fresh boot, idle; `plugins/seraphis/tests/integration/param_perf_test.cpp`, banner "SC-009: HOW ITS
  BASELINE WAS PINNED"), SC-009 poly-8 measured 2 136 070 / 2 150 320 / 2 206 890 / 2 215 600 /
  2 230 830 / 2 123 410 / 2 189 100 ns/block (19.91 %–20.91 %, median 20.52 %), every run inside the 25 %
  ceiling with ≥ 4 points of margin, and the machine state was validated by a control containing **no
  Phase 9 code** (Phase 7's `SeraphisEngine_FullPolyCpuBudget` at 20.0104 / 19.5613 / 17.6045 %, all
  passing its own gate). `ceil(worst × 1.05) = 2 342 372` nevertheless overshoots
  `25 % ÷ 1.15 = 2 318 840.6` by 1.0 %. **The baseline is therefore pinned at the largest value the
  checked-in `static_assert`s admit — `floor(25 % ÷ 1.15) = 2 318 840` — and not at the recording
  convention's value.** This is a genuine bound, not a fiction: the cold worst (2 230 830) is 3.8 %
  *under* the pinned baseline, so the gate has real teeth; it is simply weaker than
  `ceil(worst × 1.05)` would have been, and the test TU's banner discloses that in those words. **The
  25 % ceiling itself, `kRegressionFactor`, `kBaselineHeadroom`, the two `static_assert`s and the
  8-voice gate are all unchanged by this amendment**, and the runtime `REQUIRE` against the 25 % ceiling
  is untouched. The earlier hot-machine breaches (28.30 % median) that motivated the escalation are
  established as whole-machine thermal/power degradation by the same control re-read at
  23.623 / 24.784 / 24.4679 %; that evidence chain is in `compliance.md`.

  A previous revision stated neither budget in any FR and pointed the roadmap-line-503 and roadmap-line-313
  traceability rows at FR-048, which is an RT-safety requirement containing no budget, no percentage and
  no measurement. FR-048 keeps only the RT-safety row.
- **FR-056** Host-synced morph travel per C-7: when `kMorphSyncId` is on and
  `ProcessData::processContext` carries a valid tempo, the pushed travel rate MUST be
  `clamp(tempoBPM / (60 · beatsPerJourney), kMinTravelRate, kMaxTravelRate)`; when sync is off, or the
  context is absent or carries no valid tempo, the free-running `kMorphTravelRateId` value MUST be used
  unchanged. The fallback MUST NOT be silence, a zero rate, or a retained stale synced rate.
  `beatsPerJourney` is read from **C-7's eight-row table**, and `barBeats` is derived by **C-7's stated
  rule** — `timeSigNumerator · (4 / timeSigDenominator)` under `ProcessContext::kTimeSigValid` with both
  fields strictly positive, else 4.0. Neither the table nor the rule may be re-derived anywhere else;
  FR-015's `dropdown_mappings.h` table is the single transcription of the labels and beat counts.

  **Cadence (decided 2026-08-01, Q3): the synced rate is recomputed EVERY BLOCK, not only when a
  parameter changes.** Tempo is not a parameter and writes no atomic, so a host tempo ramp — or any tempo
  change after the last parameter edit — would otherwise never reach `setTravelRate`, while SC-018's five
  clauses would all still pass on a single post-edit push. Therefore, while `kMorphSyncId` is on,
  `process()` MUST recompute `clamp(tempoBPM / (60 · beatsPerJourney), kMinTravelRate, kMaxTravelRate)`
  and, when it differs from the last pushed rate by more than a stated epsilon, MUST dirty
  `voiceParamGeneration_` so FR-042's ordinary pre-slice push carries it (FR-042 amendment 2). Two things
  the plan MUST state, because leaving either open makes the requirement unmeasurable:
  - **the tempo sample point** — once per `process()` call, or once per slice; and
  - **the epsilon** that counts as "changed" — either `kMinTravelRate`-relative or exact float inequality,
    stated as a number or an expression, not as a principle.

  Because a moving tempo consequently increments `applyVoiceParams` every block, SC-007's quiescent arm is
  stated as "no parameter change **and constant tempo**".
- **FR-058** **The shipped polyphony maximum exceeds the budgeted scenario, and that is recorded, not
  hidden.** `kPolyphonyId` is a user-selectable 1 … 16 dropdown (`global_params.h:104-112`, with
  `clampPolyphony` bounding at `SeraphisEngine::kMaxVoices == 16`, `:62-67`), while FR-057 clause 2 and
  SC-009 gate at **8** voices — the count Phase 7 formally deviated to
  (`specs/seraphis-phase7-voice-engine/spec.md:1390-1394`). Phase 9 therefore MUST, **in the same change
  that ships this phase**:
  1. **Amend `specs/Seraphis-roadmap.md` line 313 in place**, in the dated-parenthetical shape the Phase 5
     budget amendment already established on this same document (`specs/Seraphis-roadmap.md:250-254`,
     *"amended 2026-07-28 from ≤ 1% by user budget decision …"*). The amendment records: that the gate is
     **8 voices**, not 16; the owner ruling that set it (2026-07-30, RQ-1, recorded in Phase 7's
     Traceability); the **measured** 8-voice figure; and the **measured** 16-voice figure SC-009 records
     as a non-gating number. Until that edit lands, roadmap line 313 states an unamended 16-voice ceiling
     that nothing in the repo satisfies — Phase 7's own deviation was recorded only in the Phase 7 spec.
  2. **Re-verify every roadmap-line citation** in this spec, in `specs/seraphis-phase9-parameters/*` and
     in `plugins/seraphis/src/plugin_ids.h:46` after **all** of this phase's roadmap edits — clause 1's
     amendment and clauses 3–5's Open-Question and Phase-11 edits, which shift lines in both directions
     and must be applied before the sweep, not during it. Citations **before** line 313 (113,
     148, 186, 245) are unaffected; citations **after** it (383, 386–388, 500–508, 511, 514, 516, 517)
     shift by the number of lines the amendment adds and MUST be corrected in the same change. A
     citation left stale by this edit is a defect of this phase, not of a later one. **Applied
     2026-08-01: those eight became 396, 399–401, 550–558, 561, 564, 571 and 572**; line 313 itself did
     not move, because the amendment starts on it.
  3. **Strike roadmap Open Question 2** (line 514 before the clause-1 amendment, line 564 after it)
     from the Open Questions list, marked resolved by
     Phase 3 — mandatory and unconditional, per the *Resolved Questions* preamble.
  4. **Write the Phase 11 inheritance into the roadmap's Phase 11 entry** (Q4, RQ-1): Phase 11 owns the
     three `SpectralState` authoring mutators (`setPartial`, `blendStates`, `tiltState`) **and** Phase 3's
     validity-preservation criterion over them, alongside the per-partial editing surface that is their
     only consumer. "Deferred" with no named owner is not a resolution.
  5. **Move roadmap Open Question 5** (line 517 before the clause-1 amendment, line 572 after it)
     **to a new named phase** (Q5, RQ-2) rather than striking it:
     per-note expression ships, and that phase owns **both** the `SeraphisVoice` per-voice expression
     inputs and `INoteExpressionController`. The roadmap entry MUST record that the controller-FUID
     host-cache hazard is **accepted**, so the decision is not re-litigated later as a discovery.
  6. Record here, normatively, that **polyphony values 9 … 16 are reachable by the user and are outside
     the budgeted scenario.** They are shipped deliberately — the roadmap's own voice-count range is
     *"8–16"* (line 75) and Open Question 4 (line 571) capped it at Phase 7 — and SC-009's non-gating
     16-voice measurement is what keeps the size of the deviation visible. **Reducing the registered
     maximum below 16 is out of scope for Phase 9** (it would be a parameter-range change at a shipped
     ID, C-9), and **raising the gate to 16 by relaxing the 25 % ceiling is forbidden** (FR-057's own
     lever clause).
- **FR-059** **Parameter-push continuity, and the mechanism that provides it.** SC-005 requires that
  automating any of its **85** in-scope IDs (SC-005's own arithmetic: 91 registered, less `kSeedId` and
  the five `CFG` IDs) be free of steps in the output. No previous revision stated *how*
  that is achieved, and all the smoothing that would provide it lives inside components FR-071 freezes.
  The mechanism is now normative. Exactly one of two classes applies to each in-scope ID, and the
  classification is a **checked-in per-ID table in the SC-005 test TU** (`kContinuityMechanism[]`), each
  row carrying the file:line evidence:
  - **(a) component-internal.** The target component already smooths the pushed value, and Phase 9 adds
    nothing. Verified examples: `AetherReverb::applyControl` (`aether_reverb.h:2950-2958`) covers
    fourteen of the eighteen reverb setters; `ContinuousBody` smooths key tracking, mix, cloud mix, cloud
    size, cloud damping, width and drive (`continuous_body.h:4222-4230`, targets set at `:1185`, `:1215`,
    `:1225`, `:1247`, `:1270`, `:1759`, and `driveLog10` at `:3249`); `AtmosphereEngine` smooths blur and
    level (`atmosphere_engine.h:2322`, `:2338-2339`); `HarmonicCloud` runs its kernel-amplitude smoother
    on every amplitude-affecting control (`harmonic_cloud.h:164`, `:1602-1604`).
  - **(b) processor-side.** Where (a) does **not** hold, `Processor` MUST smooth the **pushed plain
    value** with `Krate::DSP::OnePoleSmoother` before it reaches the setter, **advanced by each
    sub-slice's own sample count and delivered on the absolute 64-sample control-chunk grid of FR-042
    amendment 1**. This is plugin-side and therefore needs no `dsp/` change and no FR-071 carve-out.

    *(Amendment A2, 2026-08-01. This clause previously read "in exactly the shape `masterGain_` already
    uses (`processor.cpp:260`, `:376`, `:642`)". `masterGain_`'s shape is per-**output-sample** — `const
    float g = masterGain_.process();` inside `renderSlice`'s sample loop, `processor.cpp:641-645`, under
    a comment that forbids `advanceSamples(n)` and per-slice advance outright (`:633-636`) — which a
    push-based surface cannot have. The old wording and the cadence clause below were therefore in
    direct conflict, and this edit removes the conflict rather than resolving it silently at
    implementation time.)*

    **Two things the plan MUST fix for class (b), decided 2026-08-01 (Q1):**
    1. **The cadence is the settling push, on the absolute 64-sample control-chunk grid** (amendment
       A1, 2026-08-01). A smoother produces a ramp only if the smoothed value is re-pushed while it
       settles, and C-3/FR-042 push on change only. Therefore, while a class-(b) smoother is un-settled,
       its owning push (`applyVoiceParams` for a `VP` row, `setTargetBase` for an `MB` row) runs on
       `SeraphisEngine::kControlChunkSamples` boundaries — `process()` capping its slice length at the
       distance to the next boundary while, and only while, any class-(b) smoother is un-settled — and
       stops as soon as every class-(b) smoother reports settled (FR-042 amendment 1). The grid is
       **absolute across slices and across `process()` calls**, so the ramp is host-block-size
       independent; a per-**slice** ramp remains forbidden. The cadence is bounded by **`N_chunk`
       pushes** per change, spanning **`N_block` host blocks** of wall clock — the two numbers SC-007's
       class-(b) rows define, and which no criterion may conflate — and SC-008's steady-state arm is
       measured with every smoother settled, the state in which this clause does no work at all.
    2. **The time constant is a stated number, not a deferral.** The plan MUST fix it as either a single
       value shared by all class-(b) IDs **or** a per-ID column of `kContinuityMechanism[]`. It cannot be
       left to the implementation: SC-005 clause 3's `1.5 ×` reference bound is sensitive to it (too fast
       and a step survives; too slow and `N_chunk` grows without bound), and neither SC-007's `N_chunk`
       nor its `N_block` is computable without it.

  **Class (b) is known to be non-empty**, which is what makes FR-059a's positive control constructible:
  `kBodyResonanceId` (801) and `kBodyDampingId` (802) reach `ContinuousBody::setResonance` / `setDamping`,
  which store `resonance_` / `damping_` raw (`continuous_body.h:1166`, `:1175`) and are consumed directly
  in the coefficient recompute (`:1826`, `:1854`, `:1860`, `:2663`, `:2739`) — neither appears in the
  ten-smoother list at `:4222-4230`. The plan stage produces the full classification with evidence; an ID
  may not be moved into class (a) without a file:line citation of the smoother that covers it.
- **FR-059a** **The seam SC-005's positive control (b) needs.** `Processor` MUST declare
  `friend struct detail::SeraphisParamSmootherBypassProbe;` — a **test-TU-only** probe, in the shape
  Phase 7 uses for `detail::SeraphisVoiceSilenceRampProbe` (`seraphis_voice.h:97`, `:775`) — whose sole
  capability is to snap one **class-(b)** smoother from FR-059 to instant (coefficient 0, i.e. a hard
  write). Nothing in `plugins/seraphis/src/` other than this declaration may reference it, no `process()`
  path may call it, and it MUST NOT be defined outside the test translation unit. It is declared
  plugin-side precisely so that no `dsp/` file is touched: the Phase 7 precedent lives in `dsp/`, and
  adding a bypass seam there is what FR-071 forbids. ODR sweep run this session:
  `grep -rn "SeraphisParamSmootherBypassProbe" dsp/ plugins/` → 0 hits. CLEAR.

### D. Controller wiring

- **FR-060** `Controller::initialize()` MUST call every `register<Section>Params(parameters)` in band order,
  so `getParameterCount()` is **91**.
- **FR-061** `Controller::getParamStringByValue` MUST delegate to each pack's `format<Section>Param` in band
  order and return `kResultFalse` only when no pack claims the ID, preserving the Phase 8 shape
  (`global_params.h:129`, `macro_params.h:87`). Dropdown IDs format themselves via `StringListParameter` and
  MUST NOT be claimed by a formatter.

  **Verified by the `getParamStringByValue` formatting section of `Seraphis_ParameterSurface_IsComplete`
  (SC-001), and the Traceability table now carries the row** (amendment A8, 2026-08-01 — this FR
  previously had no criterion at all). That section asserts, on an initialized `Controller`: every label
  of every `L` ID round-trips from `dropdown_mappings.h`; a sample of `R` IDs across all six new packs
  returns a non-empty string; and **each of the six `format<Section>Param` functions, called directly,
  declines every dropdown ID**. Without it, a formatter that claimed a dropdown ID — the exact failure
  this clause names — would render `"0.400"` instead of `"Metal Plate"` in every host and pass every
  other criterion in the phase.
- **FR-062** `Controller::setComponentState` MUST read the version int32, refuse a stream whose version
  exceeds `kCurrentStateVersion`, and then call every `load<Section>ParamsToController` in exactly the
  getState order of C-8, using `setParamNormalized` for each. The inverse mapping of every denormalization
  MUST be exercised, not approximated.
- **FR-063** No parameter registered by Phase 8 may change type, ID, default or unit. Verified by
  **SC-014** — the criterion that compares `getParameterInfo` for each of the eight Phase 8 IDs against a
  checked-in table of their infos. *(Corrected: a previous revision pointed at SC-012, which is the
  spectral-state serialization round-trip and says nothing about registered parameter metadata.)*
- **FR-064** `Controller` MUST NOT gain `INoteExpressionController` in this phase. **This is now
  unconditional** — OQ-2 was resolved on 2026-08-01 (Q5): per-note expression **does** ship, but in a
  **new named phase** that owns both halves of it, the `SeraphisVoice` per-voice expression inputs and the
  `INoteExpressionController` implementation. Splitting the two across phases is what this clause
  forbids: the engine's note API is `noteOn(std::uint8_t, std::uint8_t)` / `noteOff(std::uint8_t)`
  (`seraphis_engine.h:370`, `:415`) with no per-note expression input at all, so an interface shipped here
  would have nothing to drive. The recorded host-cache caveat (`plugins/seraphis/CLAUDE.md`, "Decisions
  that outlive Phase 8") — that adding an interface to an already-released controller FUID can invalidate
  host-cached class metadata — is **accepted and recorded** as the price of that phasing, per FR-058
  clause 5.

### E. State

- **FR-090** `Processor::getState` MUST write exactly the C-8 layout, in registration order, extending
  `processor.cpp:500-509`. The four `SpectralState` payloads are serialized from the message-thread-safe
  sources FR-041b clause 5 names — the published staging buffer while a handoff is outstanding, and
  `factoryStates_[morphParams_.slot[i]]` otherwise — and **never from `spectralSlots_`** (amendment A5;
  this clause previously called `spectralSlots_` "the only readable source for them", which it is not:
  the slot content is a pure function of the four `CFG` atomics and an immutable factory table).
- **FR-091** `Processor::setState` MUST read it back, keeping the existing version gate
  (`:479-485`) and the EOF-safe loader contract, and MUST deserialize the four payloads into
  **`spectralSlotsStaging_`** and publish them with the release store of `spectralSlotsHandoff_`
  (FR-041b) — never into `spectralSlots_` directly, which is audio-thread-owned. It MUST then raise a
  **single release-store request** for FR-047's shared helper, which `process()` consumes at the top of
  the next block **before `pushGlobalParams()`**, invalidating every tracker under
  `SurfaceInvalidation::PresetLoad` (amendment A4, 2026-08-01 — `setState()` MUST NOT run the helper
  body itself, because it can legally run concurrently with `process()`). That request is what makes the
  loaded preset reach the DSP without waiting for the user to touch a control, and the helper sets
  `spectralStatesPending_` as part of its sequence.
- **FR-091a** `int32 seed` MUST be written and read **after** the `[macro]` block, per C-8, so a
  version-1 stream is a strict byte prefix of a version-2 stream. `saveGlobalParams`, `loadGlobalParams`
  and `loadGlobalParamsToController` (`global_params.h:160`, `:167`, `:189`) MUST keep their Phase 8
  three-field shape unchanged; the seed is carried by a separate, explicitly-positioned save/load pair.
  Neither `loadGlobalParams` nor its controller twin may gain a version parameter, because with this
  placement neither needs one — and a version-aware fixed-sequence reader is the failure mode C-8 records.
- **FR-092** The morph block MUST serialize each of the four spectral-state slots as
  `kSpectralStateBytes = 541` bytes produced by `serializeSpectralState` (`spectral_state.h:238`) and read
  back with `deserializeSpectralState` (`:274`). A slot whose serialization returns **0** (invalid state)
  MUST be written as 541 zero bytes and MUST deserialize to a rejection. A slot whose deserialization
  returns `false` MUST leave the FR-041b slot **bitwise untouched**, which is the documented behaviour of
  the DSP function (`:263-268`).

  **In Phase 9's factory-selection-only design this zero path is unreachable, and that is what makes
  FR-094 hold.** Every slot of `spectralSlots_` is a `makeFactoryState(...)` result (FR-041b), so it always
  satisfies `isValidSpectralState` (declared `spectral_state.h:82`; enforced at the serializer's guard,
  `:240`) and `serializeSpectralState` never returns 0.
  The clause above is stated for robustness against a corrupt in-memory slot only. It matters because the
  zero path would otherwise **break FR-094**: a rejected 541-zero payload leaves the runtime slot at its
  previous valid contents, so the *second* `getState` would write 541 non-zero bytes for that slot and the
  two streams would not be byte-identical. **When Phase 11 makes states user-editable (RQ-1), that phase
  must either preserve the validity invariant — which is exactly the criterion RQ-1 hands it — or add an
  explicit FR-094 carve-out.** It cannot leave this implicit, and this sentence is why RQ-1's
  validity-preservation criterion travels with the mutators rather than being dropped.
- **FR-093** A **version-1** stream (Phase 8's 36 bytes) MUST load: global and macro blocks are read, every
  Phase 9 field stays at its registered default, and the resulting sound is the Phase 8 default sound.
  A version-2 stream truncated at any byte boundary MUST load as far as it goes and leave the remainder at
  defaults, never crash and never leave a partially-decoded `SpectralState` in a slot.
- **FR-094** State round-trip MUST be **exact for stored values**: `getState` → `setState` → `getState`
  MUST produce byte-identical streams. This is legitimate because the bytes are stored parameter values,
  not arithmetic results (`dsp/CLAUDE.md`, *"Digests over a serialized byte stream … are fine"*), and
  because `SpectralState`'s round trip is documented exact (`spectral_state.h:270-272`).

### F. UI metadata and tests

- **FR-100** `plugins/seraphis/resources/editor.uidesc` MUST gain a `<control-tag>` entry for **every** one
  of the 91 IDs, named after the enum without the `k`/`Id` affixes. It MUST NOT gain any new `<view>` —
  layout is Phase 11's (roadmap lines 471–479). The existing eight tags and eight views stay as they are.
- **FR-101** `plugins/seraphis/tests/CMakeLists.txt` MUST list every new test file explicitly; the source
  list is not globbed (`tests/CMakeLists.txt:5-31`).
- **FR-102** The editor-lifecycle harness enrollment MUST stay green at the enlarged surface, unchanged in
  shape: `exerciseEditorLifecycle(controller, "editor", uidescPath, 3)`
  (`tests/test_helpers/editor_lifecycle_harness.h:102-105`).
- **FR-103** `plugins/seraphis/CLAUDE.md`'s param-ID table MUST be updated to mark bands 200–1399 shipped in
  Phase 9, and `plugins/seraphis/CHANGELOG.md` MUST gain a section for the version this phase ships, so
  `tools/check-changelog-coverage.js` finds it.
- **FR-104** `node tools/check-portability.js` MUST pass on every changed C++ file before commit, and
  `tools/run-clang-tidy.ps1 -Target seraphis` (and the `.sh` equivalent) MUST be clean.

---

## Success Criteria

Every criterion names its metric, its threshold and the test that measures it. Test names are sketches for
`seraphis_tests` unless a `dsp_systems_tests` target is named.

- **SC-001 — Surface completeness.** `Controller::initialize()` followed by
  `getParameterCount()` returns **91**; iterating `getParameterInfo(i)` for all 91 yields IDs that are
  exactly the set in C-6, with no duplicate ID and no ID outside the reserved bands. Each info's
  `stepCount` matches C-6's *Type* column (0 for `R`, 1 for `T`, `n−1` for an `n`-entry `L`).
  *Test:* `Seraphis_ParameterSurface_IsComplete`.
- **SC-002 — Negative control: defaults are unchanged (the phase's safety net).**

  **The reference arm is constructed, same-build, in the same TU — never a checked-in fingerprint.** A
  previous revision said "compared against the same render produced by the Phase 8 code path", which does
  not exist at test time: FR-040 rewrites `processParameterChanges`, FR-043/FR-044 change `renderSlice`'s
  pushes and FR-050 rewires `macro_params.h`, all in place. The other reading — pinning a
  `RenderFingerprint` from the pre-change commit — is the cross-toolchain gate this plugin's own test has
  already demoted: `plugins/seraphis/tests/unit/midi_event_test.cpp:426-435` records that
  *"`compareFingerprints` samples only 32 checkpoints … and its `kMetricTolerance = 1e-5` relative bound
  was measured for the cross-toolchain spread of the SAME computation, not of a re-partitioned one"*, and
  `render_fingerprint.h:20-30` confirms the tolerances were measured on **phaser and flanger** cases, not
  on a 4 s stochastic granular + reverb chain. **A checked-in fingerprint as the SC-002 reference is
  forbidden.**

  *Pass condition.*
  1. **Arm A** — 4 s render of note 60 through `Processor` at **registered defaults**, 48 kHz, block 512,
     fixed seed.
  2. **Arm B** — the same 4 s render produced *in the same translation unit and the same binary* by
     configuring a `SeraphisEngine` + `AetherReverb` pair directly with the Phase 8 shipped defaults
     (`makeSeraphisEngineConfig` / `makeSeraphisReverbConfig`, `seraphis_engine_config.h:43`, `:64`) and
     driving the Phase 8 chain — **no Phase 9 push path engaged at all**: no `applyVoiceParams`, no
     `setTargetBase`, no `applyAetherParams`, no `applySpectralStates`.
  3. **Gate** — per-sample `maxAbsDiff` over **all** samples of both channels **≤ 1.0e-5**, the shape
     `midi_event_test.cpp:420-424` already ships. `compareFingerprints` runs as a **secondary,
     warn-only** aggregate, exactly as at `:426-435`; it must not gate.
  4. Separately, a default-constructed `SeraphisMacroMatrix` with no base override evaluates every one of
     the 27 targets to the same float as before FR-004.

  *Tests:* `Seraphis_Phase9Defaults_MatchPhase8Render`, `SeraphisMacroMatrix_DefaultBases_Unchanged`
  (`dsp_systems_tests`).
- **SC-003 — Every parameter reaches the DSP.** For each of the 83 new IDs, driving it from its default to
  the opposite end of its range through `IParameterChanges` changes the observable the parameter targets.
  **No parameter is inert under its stated precondition.**

  The criterion is an **83-row table**, and every row carries four columns — *precondition*, *render
  length*, *observable*, *threshold* — because a single blanket rule is false for at least thirteen of the
  rows. The rules by route:

  | Route | Precondition | Render | Observable | Threshold |
  |---|---|---|---|---|
  | `VP` (37) | none | 1 block | the matching `SeraphisVoice` read-back — the envelope/spatial getters at `seraphis_voice.h:599-635`, `getTravelMode()` at `:667`, or a component accessor at `:763-771` reaching a component getter (incl. FR-072's thirteen and `AtmosphereEngine`'s six existing ATMOSPHERE-SET getters) — for every `i < kMaxVoices` through `SeraphisEngine::getVoice(i)` | exact equality with the pushed plain value, **after the target component's own clamp** (FR-072) |
  | `MB-voice` (19) | **polyphony pinned to 16** (see below), and at least one `renderSlice` has run, so `macros_.apply(*engine_)` has executed | 1 block | **primary:** the voice-side read-back through `getVoice(i)` after that slice, same surface as the `VP` row. **Secondary:** `getTargetBase` via FR-041a's `macroMatrixForTest()` | primary: equality with the pushed value at Phase 7's FR-060 macro neutral; secondary: equality with the pushed value |
  | `MB-aether` (8) | as `MB-voice`, plus `applyAetherTargets(...)` has run for that slice | **per-row**, see the `MB-aether` table below | **per-row** — *no voice-side getter can apply: these targets never touch a voice* | **per-row**; `getTargetBase` is the secondary on every row |
  | `ENG` (**2 new**: 3, 1008 — plus Phase-8 ID 1, covered here explicitly; ID 2 has its own row below) | none | 1 block | `getSeed()` / `getAtmosphereFreeze()`, and `getPolyphony()` for ID 1 | exact |
  | `CFG` (5) | **quiescent engine** — see the dedicated clause below | ≥ 1 s **after** a subsequent note-on | `morph().getStateCount()` (`spectral_morph_engine.h:443`) plus a rendered spectral differential | count exact; differential ≥ 1 % relative RMS |
  | `AE` (10) | none | **per-row**, see the table below | **per-row**, see the table below | **per-row** |

  **The route rows sum to the 83 new IDs, and here is the arithmetic.** 37 `VP` + 19 `MB-voice` +
  8 `MB-aether` + 5 `CFG` + 10 `AE` + **2** new `ENG` (3 `kSeedId`, 1008 `kAtmosFreezeId`) + **2**
  new processor-local (405 `kMorphSyncId`, 406 `kMorphSyncNoteId`, whose rows are the *"inert by design"*
  clause below) = **83**. C-6's route table counts `ENG` as 4 and processor-local as 8 because both routes
  also contain **Phase-8** IDs — 1 and 2 on `ENG`, 0 and 100–104 processor-local — and those are not among
  the 83 this criterion opens with. Two of them are nonetheless exercised, because this criterion names
  them explicitly: **ID 1** (`kPolyphonyId`) on the `ENG` row's observable list, and **ID 2**
  (`kSoftLimitId`) in its own carve-out row below. That is coverage **in addition to** the 83, never a
  substitute for any of them, and it does not change the criterion's count. The remaining six Phase-8 IDs
  (0 and 100–104) are out of this criterion's scope entirely — master gain is Phase 8's own, and the five
  macros are SC-004's subject.

  **The `MB-voice` rows pin polyphony to 16, and that is why both rows can share one wording**
  (decided 2026-08-01, Q6). `SeraphisMacroMatrix::apply()` keeps its own `getPolyphony()` bound
  (`seraphis_macro_matrix.h:625-626`), which FR-004/FR-071 forbid changing, so at the default polyphony of
  8 voices 8–15 never receive an `MB` value and a test written to the shared `for every i < kMaxVoices`
  wording would fail a correct implementation on all nineteen `MB-voice` IDs. The test therefore **sets
  `kPolyphonyId` to 16 before the `MB-voice` rows**, at which point `getPolyphony() == kMaxVoices` and the
  two bounds coincide. *(The alternatives were rejected on the record: bounding the assertion at
  `getPolyphony()` would fork the wording of two rows that are otherwise identical, and widening
  `apply()`'s loop is a `dsp/` behaviour change needing an FR-071 carve-out.)* **C-2's recorded residue —
  that the 27 `MB`-routed values stop at the polyphony bound in ordinary operation — stands exactly as
  written and is not affected by this test-side pin.**

  **Four IDs are carved out of their blanket rule and carry their own rows below**, because the blanket
  rule is unsatisfiable for a correct implementation in each case: `kMorphPositionId` (402, `MB-voice` —
  no `getTargetPosition()` exists and `getTravelPosition()` is slew-limited), `kMorphState2Id` /
  `kMorphState3Id` (411/412, `CFG` — inert at the default state count of 2) and `kSoftLimitId` (2, `ENG` —
  one block of a 2 s attack is near-silence). The thirteen body/growth IDs need **no** carve-out: FR-072
  creates the read-backs the blanket `VP` / `MB-voice` rule names, which is why that FR exists.

  **The eight IDs added on 2026-08-01 need no carve-out either, and their read-backs are named here so
  the 83-row table is complete.** All eight are `VP` and all eight are covered by the blanket `VP` rule —
  exact equality against the component's clamped stored value, on every `i < kMaxVoices`:

  | ID | Push | Read-back | Note |
  |---|---|---|---|
  | 811 `kBodyInputAgcId` | `setBodyInputAgcEnabled` | `body().isInputAgcEnabled()` (FR-072) | boolean, exact. **No level assertion**: FR-070 #12 records that turning the AGC off is a documented level change, not a defect |
  | 812 `kBodyResonatorBypassId` | `setBodyResonatorBypass` | `body().isResonatorBypass()` (FR-072) | boolean, exact; the accessor returns the **requested** state, not the 10 ms ramp position (FR-072) |
  | 1011 `kAtmosJitterId` | `setAtmosJitter` | `atmos().getJitter()` (`atmosphere_engine.h:804`) | existing getter |
  | 1012 `kAtmosPositionId` | `setAtmosPositionSeconds` | `atmos().getPositionSeconds()` (`:811`) | existing getter |
  | 1013 `kAtmosPositionSpreadId` | `setAtmosPositionSpread` | `atmos().getPositionSpread()` (`:819`) | existing getter |
  | 1014 `kAtmosPitchId` | `setAtmosPitchSemitones` | `atmos().getPitchSemitones()` (`:827`) | existing getter |
  | 1015 `kAtmosPitchSpreadId` | `setAtmosPitchSpread` | `atmos().getPitchSpread()` (`:834`) | existing getter |
  | 1016 `kAtmosGrainEnvelopeId` | `setAtmosGrainEnvelope` | `atmos().getGrainEnvelope()` (`:963`) | existing getter; compares the **enum**, and the index↔enum conversion is FR-015's table |

  **The `MB` rows may not be gated on `getTargetBase` alone.** `getTargetBase` is FR-003's own storage, so
  asserting it passes even if `macros_.apply(*engine_)` is never invoked — it cannot substantiate this
  criterion's claim that nothing is inert. It is a *secondary* assertion; on the `MB-voice` rows the
  primary is the value read back off the voice after a slice, and on the `MB-aether` rows it is the
  rendered observable in that table.

  **Why `MB-aether` needs its own table.** Eight of the 27 `SeraphisMacroTarget` entries are Aether-owned
  (`seraphis_macro_matrix.h:79-87`: `AetherMix`, `AetherSize`, `AetherWidth`, `AetherShimmerOctaveSend`,
  `AetherShimmerFifthSend`, `AetherBloomSend`, `AetherSizeBreathDepth`, `AetherDimensionalityTideDepth`)
  — registered IDs **1200, 1201, 1210, 1211, 1212, 1215, 1216, 1217**. `apply()` writes only the nineteen
  Voice-owned targets (`:626-659`); the Aether half leaves through `computeAetherTargets()` (`:667-679`)
  → `applyAetherTargets()` (`seraphis_engine_config.h:93-103`) into `AetherReverb`, whose entire const
  surface (`aether_reverb.h:2486-2612`, read this session) has **no** getter for mix, size, width, either
  shimmer send, the bloom send, the breath depth or the tide depth. A voice-side getter cannot exist for
  any of them, and the ten-row `AE` table below covers the `AE`-routed IDs only. A previous revision left
  these eight rows pointing at an observable that cannot exist.

  **The eight `MB-aether` rows, individually.** Each drives the value through `macros_.setTargetBase(...)`
  + one `renderSlice` (so `computeAetherTargets` → `applyAetherTargets` runs), at Phase 7's FR-060 macro
  neutral so the contribution is exactly 0 and the base reaches the reverb unmodified.

  | ID | Observable | Render | Threshold |
  |---|---|---|---|
  | 1200 `kAetherMixId` | wet fraction `RMS(out − dryReference) / RMS(out)` over the settled last second, dry reference captured from the same render with mix pinned at 0 | ≥ 2 s | ≥ 0.20 absolute change, 0.0 → 1.0 |
  | 1201 `kAetherSizeId` | `getModalDensityPerHz()` (`:2511`) — `effectiveDelay_` is *"Size + drift + breath scaled"* (`:4483`), so Size **does** reach it | ≥ 1.5 s (≥ 5 × `kSizeSmoothingMs = 300 ms`, `:2731`) | ≥ 20 % relative change, 0.0 → 1.0 |
  | 1210 `kAetherShimmerOctaveId` | wet-tail energy in a ±1 semitone band about **2 f₀** relative to the band about f₀, settled last second | ≥ 4 s | ≥ 6 dB rise, 0.0 → 1.0 |
  | 1211 `kAetherShimmerFifthId` | the same statistic about **1.5 f₀** | ≥ 4 s | ≥ 6 dB rise, 0.0 → 1.0 |
  | 1212 `kAetherBloomSendId` | wet-tail energy at the held note's partial frequencies relative to broadband wet-tail energy, settled last second. **Secondary:** `getActiveBloomResonatorCount()` (`:2583`) ≥ 1 | ≥ 4 s | ≥ 3 dB rise, 0.0 → 1.0 |
  | 1215 `kAetherSizeBreathDepthId` | total variation of `getModalDensityPerHz()` sampled once per control chunk (breath scales `effectiveDelay_`, `:3037`) | ≥ **40 s** (2 × the 20 s breath period; `kBreathRateHz = 0.05`, `:2754`) — `[.slow]` | ≥ 2 × the depth-0 reading |
  | 1216 `kAetherTideDepthId` | total variation of `getCurrentMorphPosition()` (`:2526`) sampled once per control chunk — the tide term is the *only* other summand in `updateMorph()` (`:3161-3164`) | ≥ **60 s** (2 × the 30 s base tide period, `kTideRateNormalised`, `:2759`) — `[.slow]`; `kAetherDimensionalityId` pinned so `dimSm_` is settled | ≥ 2 × the depth-0 reading |
  | 1217 `kAetherWidthId` | normalized L–R correlation of the wet tail, settled last second | ≥ 2 s | ≥ 0.20 absolute change, 0.0 → 1.0 |

  **`AetherReverb::getLatencySamples` is NOT an observable and is struck from this criterion.** Its body
  is `spectralEnabled_ ? diffusionFftSize_ : 0` and it is documented *"Constant for a prepared
  configuration — no setter changes it"* (`aether_reverb.h:2607-2614`); Phase 8 already depends on that
  constancy (`processor.h:62-66`). It can observe none of the ten `AE` parameters.

  **The ten `AE` rows, individually.** `AetherReverb`'s entire const surface is
  `getMatrixOrthogonalityError`, `getEffectiveDelayLengthSamples`, `getModalDensityPerHz`,
  `getMaxSizeScale`, `getCurrentMorphPosition`, `getStateEnergy`, `getActiveBloomResonatorCount`,
  `getNonFiniteRecoveryCount`, `getLatencySamples` (`:2503-2614`, read this session) — **none** reads
  decay, damping, pre-delay, mod depth, mod smoothness, bloom decay, spectral diffusion or density. Each
  row therefore drives FR-049's `applyAetherParams` **directly** and measures on a render window long
  enough to expose the effect. "One block" (10.67 ms) is not such a window for any of them: pre-delay
  0 → 200 ms cannot change the first 10.67 ms of output at all, and decay 4 s → 60 s changes the tail
  envelope by parts in 1e-3 over 10 ms.

  | ID | Observable | Render | Threshold |
  |---|---|---|---|
  | 1202 `kAetherDensityId` | **echo density of the wet impulse response** — the count of samples exceeding 20 % of the running peak in the first 250 ms, dry muted. *(`getModalDensityPerHz()` is **not** an observable for this ID and a previous revision wrongly named it: its body is `sum(effectiveDelay_[i]) / sampleRate_` (`:2511-2518`) and `effectiveDelay_` is written only from the Size/drift/breath geometry (`:3045`, `:3054`, member comment `:4483`), while `setDensity` (`:2211`) feeds `densitySm_`, whose only consumer is `diffuser_.setDensity(...)` at `:3194`. The paragraph above this table already says no getter reads density; the row contradicted it.)* | ≥ 500 ms, impulse-excited | strictly increasing 0.0 → 1.0 and **≥ 1.5 ×** end to end; the exact factor is pinned at the plan stage by measurement, in the `floor(min observed / 1.05)` shape SC-020 uses |
  | 1203 `kAetherDecayId` | measured **T60** of the wet tail after an impulse-excited note-off | ≥ 4 s | T60 at 60 s ≥ 3 × T60 at 0.5 s |
  | 1204 `kAetherFreezeId` | wet-tail RMS slope over the last 2 s | 6 s | slope ≥ −0.5 dB frozen vs a decaying tail unfrozen |
  | 1205 `kAetherDimensionalityId` | `getCurrentMorphPosition()` (`:2526`). **Secondary:** `getMatrixOrthogonalityError()` (`:2503`) stays within its Phase 6 bound. **Preconditions, both required:** `kAetherTideDepthId` (1216) pushed to **0** — `updateMorph()` computes `clamp(dimSm_.getCurrentValue() + tideDepth_ · tide_.getCurrentValue(), 0, 1)` (`:3161-3164`) and the header's own note says the identity holds only where the tide term is zero (`:3153-3155`), while the registered default tide depth is **0.20** (`kDefaultTideDepth`, `:2750`) — and the render is long enough for `dimSm_` to settle | ≥ **1 s** (≥ 5 × `kDimSmoothingMs = 200 ms`, `:2739`). *(A previous revision said "1 control chunk" = 64 samples ≈ 1.3 ms, at which point the smoother has moved a fraction of a percent.)* | morph position tracks the pushed value within 1e-3 |
  | 1206 `kAetherDampingId` | high/low wet-tail band-energy ratio (above vs below 2 kHz), settled last second | ≥ 2 s | ≥ 3 dB change end to end |
  | 1207 `kAetherPreDelayId` | onset index of the wet path for an impulse-excited render, dry muted | ≥ 400 ms | onset shifts by 200 ms ± 5 ms |
  | 1208 `kAetherModDepthId` | total variation of the instantaneous frequency of a sustained wet sine tail | ≥ 4 s | ≥ 2 × the depth-0 reading |
  | 1209 `kAetherModSmoothnessId` | autocorrelation time of the same instantaneous-frequency track | ≥ 20 s (the class's `kTauMax` is 30 s, `:2263`) | ≥ 2 × the smoothness-0 reading |
  | 1213 `kAetherBloomDecayId` | ring-down time of a driven bloom resonator, measured with `getActiveBloomResonatorCount()` (`:2583`) ≥ 1 | ≥ 2 s | ≥ 2 × change end to end |
  | 1214 `kAetherSpectralDiffusionId` | L/R decorrelation of the wet tail, settled last second | ≥ 2 s | ≥ 0.1 absolute change |

  **The five `CFG` rows have their own clause, and it is the complement of SC-013 — not a contradiction
  of it.** `SeraphisVoice::setSpectralState` / `setSpectralStateCount` are gated on
  `isConfigurable() = !hasSounded_ || isFinished()` (`seraphis_voice.h:699-719`), so with a note sounding
  the write is **rejected** (which is exactly what SC-013 asserts), and with no note sounding the render
  is silence, so a "relative RMS difference" against a silent default render is 0/0 and undefined. The
  ordering is therefore explicit:
  1. push the `CFG` parameter while the engine is **quiescent** (no note has sounded, or every voice is
     finished);
  2. assert the write was **accepted**: `getRejectedConfigureTimeCallCount()` (`:720`) did not rise on
     any voice, `spectralStatesPendingForTest()` (FR-041a) has cleared, and `morph().getStateCount()`
     equals the pushed count for ID 408;
  3. then note-on and render **≥ 1 s**, and assert the spectral differential against the same
     note-on render taken at the default slot assignment.
  SC-013 covers the complementary (sounding-voice) case.

  **IDs 411 and 412 carry a further precondition without which they are inert for a correct
  implementation.** `kMorphStateCountId` (408) has registered default **2** (C-6), and
  `SpectralMorphEngine` blends only the two slots bracketing the position: `currentSegment()` clamps to
  `[0, numStates_ − 1]` and `slotContributes()` admits `k` and `min(k + 1, numStates_ − 1)`
  (`spectral_morph_engine.h:553-563`). At `numStates_ = 2`, slots 2 and 3 **never** contribute to the
  output, so driving `kMorphState2Id` or `kMorphState3Id` at the default state count produces no spectral
  differential at all. Their rows therefore read, in this order, all inside the same quiescent window:

  | ID | Preconditions (pushed before the slot parameter, engine quiescent) | Render after note-on |
  |---|---|---|
  | 409 `kMorphState0Id` | none beyond the `CFG` clause (slot 0 brackets the default position 0.0) | ≥ 1 s |
  | 410 `kMorphState1Id` | none beyond the `CFG` clause (slot 1 is the upper bracket of position 0.0) | ≥ 1 s |
  | 411 `kMorphState2Id` | `kMorphStateCountId` = **4**; `kMorphTravelRateId` = `kMaxTravelRate` (1.0); `kMorphTravelModeId` = External; `kMorphPositionId` = **2.0** | ≥ **1.7 s** — `advanceTravel`'s cap is `travelRate · (numStates − 1) · dt` (`spectral_morph_engine.h:716`), i.e. **3 units/s** at rate 1.0 with 4 states, so 0.0 → 2.0 takes **0.67 s**, plus 1 s settled |
  | 412 `kMorphState3Id` | as 411, with `kMorphPositionId` = **3.0** | ≥ **2 s** (0.0 → 3.0 at 3 units/s = **1 s** travel, plus 1 s settled) |

  The differential for 411/412 is taken against the same note-on render at the **same** state count and
  position with the default slot assignment, so the only difference between the arms is the slot content.

  **ID 402 `kMorphPositionId` has its own row: the blanket `MB-voice` rule is unsatisfiable for it.**
  `SpectralMorphEngine` exposes **no** `getTargetPosition()` — its complete const surface is
  `spectral_morph_engine.h:392-456`, and `targetPosition_` (`:740`) is private with no accessor. The only
  readable quantity is `getTravelPosition()` (`:434`), which returns the **slew-limited** `position_`:
  `advanceTravel` caps the per-chunk movement at `travelRate_ · (numStates_ − 1) · dt`
  (`spectral_morph_engine.h:716-725`), so at the registered default rate `kMinTravelRate = 1/600` (`:101`)
  and default state count 2, one 512-sample block at 48 kHz moves the position by **1.78e-5** and an
  equality assertion after "1 block" fails for a correct implementation. The row is therefore:

  | ID | Preconditions | Render | Observable | Threshold |
  |---|---|---|---|---|
  | 402 `kMorphPositionId` | `kMorphTravelRateId` pushed to `kMaxTravelRate` = **1.0**; `kMorphTravelModeId` = External (its default), so the target is the pushed value and not the spline's; state count at its default **2**, so the reachable end of the range is **1.0**, which is the value pushed | ≥ **1.5 s** (the journey `(numStates − 1)/travelRate` = 1 s, plus settling) | **primary:** `getVoice(i).morph().getTravelPosition()` for every `i < kMaxVoices`. **Secondary:** `getTargetBase(MorphTargetPosition)` | primary: within **1e-3** of the pushed value; secondary: exact equality |

  Positions above `numStates − 1` clamp inside the DSP and are covered under *Edge cases*, not here.

  **The `ENG` soft-limit row is split out of the blanket `ENG` rule, which is internally inconsistent for
  it.** One block at 48 kHz is 10.67 ms, but the registered default envelope stage 0 is `{1.0, 2000 ms}`
  (`seraphis_voice.h:355`), so ~10 ms after note-on the voice output is a small fraction of its eventual
  peak; and the toggle only moves `TapeSaturator::setSaturation` between `kOutputSaturation = 0.15`
  (`seraphis_engine.h:142`, applied in `processOutputStage`, `:512-522`) and 0. A soft saturator at drive
  0.15 acting on a near-silent signal differs by parts in 1e-6, not the 1 % relative RMS the blanket rule
  demands. Its row is:

  | ID | Preconditions | Render | Observable | Threshold |
  |---|---|---|---|---|
  | 2 `kSoftLimitId` | `kEnvStage0MsId` and `kEnvStage1MsId` pinned to **1 ms** (their C-6 floor) so the envelope is at its sustain level within 2 ms; `kMasterGainId` = 1.0; polyphony 8 with **8 notes held** so the voice sum actually drives the saturator | **2 s**, measured over the **settled last 1 s** | third- + fifth-harmonic energy relative to the fundamental, 65 536-point FFT, Blackman-Harris — the same detector SC-004 and SC-019 pin | strictly higher with the limit **on**, by more than the run-to-run spread of the same measurement over **8 repeats**; the numeric floor is pinned at the plan stage by that measurement, in the `floor(min observed / 1.05)` shape SC-020 uses. *(Relative RMS is not used: the saturator is a shaper, and at drive 0.15 its RMS effect is far below its harmonic signature.)* |

  **Three IDs are inert by design under some conditions, and carry explicit preconditions.** FR-056 makes
  `kMorphSyncNoteId` (406) have no effect unless `kMorphSyncId` (405) is on **and**
  `ProcessData::processContext` carries a valid tempo, and makes `kMorphTravelRateId` (404) have no effect
  while sync is on. `kMorphSyncId` (405) itself has no effect without a valid tempo. Their rows therefore
  read: **406** — sync on, `processContext` present with `kTempoValid` and 120 BPM; **404** — sync off
  (its own default); **405** — `processContext` present with `kTempoValid` and 120 BPM. Driving 406 at
  the default sync setting changes nothing anywhere and a blanket clause would fail a correct
  implementation.

  *Test:* `Seraphis_EveryParameter_ReachesDsp` (data-driven over the 83-row table above).
- **SC-004 — Macros are audibly effective, and compose with deep parameters.** Two arms.

  **Arm 1 — at defaults, Phase 7's SC-009 reproduced, not paraphrased.** For each of the five macros:
  sweep it **0 → 1 in 21 steps**, 4 s per step, fixed seed and note, **holding each non-swept macro at its
  own Phase 7 FR-060 neutral** (Gravity 0.5, the rest 0). The gate is Phase 7 SC-009
  (`specs/seraphis-phase7-voice-engine/spec.md:1612-1770`) **in full**, including its measurement arms,
  and it is a **Spearman ρ trend, not monotonicity**:
  - primary metric and required direction per macro, at **|ρ| ≥ 0.9**, on the arms Phase 7 pins — Dream's
    primary on the **dry voice sum** with the Aether mix held at neutral (`:1690-1696`), Dissolve's
    atmosphere-band contribution over the **settled last second** (`:1635-1642`), Gravity's body-decay
    observable on a **dry, isolated-damping, 1–8 kHz** arm (`:1658-1680`), Entropy's flatness on the
    **cloud-only** arm (`:1682-1688`);
  - every secondary observable of Phase 7's table (`:1622-1628`) at |ρ| ≥ 0.9 in its stated direction;
  - the **per-macro minimum end-to-end effect size** table (`:1730-1736`) — Dream ≤ 50 % of its Dream = 0
    value, Bloom centroid **+≥ 20 % relative**, Dissolve **+≥ 0.15 absolute**, Gravity **≥ 6 dB**, Entropy
    **+≥ 25 % relative**;
  - the no-discontinuity clause: between consecutive steps the primary metric never changes by more than
    **3×** the mean step change (`:1760-1765`);
  - the pinned partial detector (`:1704-1712`): 65 536-point FFT, Blackman-Harris, last 1 s of each step,
    −60 dB peak threshold, 20 dB peak-to-local-median SNR, parabolic interpolation, **ordinal** grid
    matching with an exact-count gate.

  *(A previous revision said "moves its documented primary metric monotonically, reproducing Phase 7's
  SC-009 thresholds". Phase 7 SC-009 does not require monotonicity — `:1755-1759` withdraws that wording
  explicitly — and "reproducing the thresholds" left the arms, the secondaries and the effect sizes
  unstated.)*

  **Arm 2 — composition, with the offsets named and a floor of its own.** For each macro, one of its
  target parameters is pushed to a stated non-default value **chosen to preserve headroom in that macro's
  direction of travel** (C-1), and Arm 1's sweep is repeated:

  | Macro | Deep parameter pushed | Default | Arm-2 value | Headroom preserved |
  |---|---|---|---|---|
  | Dream | `kCloudInharmonicityId` (201) | 0.030 | **0.060** | Dream travels −0.030 toward the 0 floor; 0.060 leaves the full span |
  | Bloom | `kCloudRichnessId` (200) | 0.60 | **0.45** | Bloom travels +0.40 toward the 1.0 clamp; 0.45 → 0.85 stays reachable |
  | Dissolve | `kAtmosLevelId` (1000) | 0.5 | **0.30** | Dissolve raises level toward `kMaxLevel = 2.0` |
  | Gravity | `kBodyDampingId` (802) | 0.25 | **0.40** | Gravity's row is signed about 0.5; both halves stay inside `[0,1]` |
  | Entropy | `kMorphEntropyId` (400) | 0.20 | **0.10** | Entropy travels +0.30 toward the 1.0 clamp |

  *Gate:* the primary metric moves with **|ρ| ≥ 0.9 and the same sign as Arm 1**, and the **end-to-end
  effect size is ≥ 50 % of Arm 1's**, and `getTargetBase(target)` (via FR-041a's `macroMatrixForTest()`)
  equals the pushed deep value exactly. A bare "still moves in the same direction" would pass on a
  0.001 % move and would not gate the composition property at all.

  **Arm 3 — saturation is legal, and is asserted as such (C-1 clause 2).** With `kCloudRichnessId` pushed
  to **1.0**, the Bloom sweep's primary metric is asserted **monotone non-decreasing** (largest downward
  step ≤ the detector's own noise floor, measured on a no-op sweep in the same render) and
  `getTargetBase(CloudRichness) == 1.0`. No effect size is required on this arm: Bloom's +0.40 span is
  entirely consumed by `setRichness`' `[0,1]` clamp (`harmonic_cloud.h:416`), which is the documented and
  accepted behaviour, not a defect.

  **Amendment A11, 2026-08-01 — four Arm-1 observables are ESTIMATED differently from Phase 7's
  literal construction. No gate, no bound and no effect-size floor moves.** Arm 1 was run in full and
  three of its rows failed: Dream's wet-tail secondary at ρ = 0.809091 against ≥ 0.9, and the
  no-discontinuity clause on Dissolve's and Entropy's primaries; fixing the second of those exposed a
  fourth row (item 4) that no run had ever reached. Each was traced to root cause
  **before** anything was changed, and in all four the defect is in how the quantity is *estimated*,
  not in the macro → DSP mapping. All four reproduce in Phase 7's own SC-009 case with **no plugin
  code in the path at all** — `dsp_systems_tests.exe "SeraphisEngine_MacroSweepsMoveTheirAxis_Full"`
  fails, on this date, at Dream ρ = 0.802597, Bloom's L/R-correlation secondary ρ = −0.855844 and
  Dissolve worst/mean = 4.06 — so they are **inherited Phase 7 measurement defects, not Phase 9
  wiring defects**. (Phase 7's own case is left failing by this phase and is flagged for the owner;
  the four fixes below port to it unchanged.)

  1. **Dream's "wet-tail energy after note-off ↑ (reverb send)" is measured on the WET FIELD, nulled
     out of the composed render** — not on total tail energy. `AetherReverb::setMix` is a
     **crossfade**, `dry·(1−m) + wet·m` (`aether_reverb.h:2336`), and Dream's AetherMix row is base
     0.35 amount +0.35, so the dry field loses `(1−m)²` exactly as fast as the wet field gains `m²`,
     while the voice's 8000 ms release keeps the dry field loud through the measured window. MEASURED
     as total tail energy the series **falls 62 % over its first five steps while the send is rising**
     (0.0959165, 0.078103, 0.0589534, 0.0520102, 0.0363219) and then rises to 2.21437 — U-shaped, and
     unreachable by any correct implementation at ρ ≥ 0.9. The reference arm is the identical step with
     `kAetherDecayId` (AE-routed, no macro row writes it, so a base push holds) at its 0.5 s floor;
     both arms share the same binary, seed, note and crossfade position, so their dry fields are
     bit-identical and the per-sample difference is exactly `m·(wet_long − wet_short)`. MEASURED:
     0.000131193 → 0.137118, **ρ = 0.972727**.
  2. **Dissolve's primary differential is taken against a DENSITY-muted reference arm.** A level-muted
     arm is unreachable during a Dissolve sweep — AtmosLevel is Dissolve's own target and at base 0 the
     evaluated level is still `1.50·d` — so the level differential isolates only a fixed 0.50 slice
     **plus a cross term** `2·(L_f−L_0)·∫S·A` with no fixed sign. MEASURED, that cross term dominates:
     with nothing else changed but the step length, the fraction comes out **negative** over the bottom
     of the sweep (−0.0142 at 5 s/step, −0.0497 at 7 s/step). At the pinned 4 s it scored ρ = 0.998701
     with **worst/mean = 3.48571** against the 3.0 bound. `kAtmosDensityId` is VP-routed (the three
     atmosphere targets are AtmosLevel, AtmosBlur, AtmosDriftDepth), so a base push holds for the whole
     sweep, and at `kAtmosDensityMin = 0.1` grains/s the reference arm launches one grain per ten
     seconds. MEASURED, same window, same band, same 4 s geometry: **ρ = 1, worst/mean = 2.95005**,
     end-to-end 0.052595 → 0.657805 (**+0.605 absolute** against the ≥ 0.15 floor).
     *That there is no discontinuity in the mapping was established separately, before the metric was
     touched:* the identical level-muted sweep rendered at **8 s and 16 s per step** is strictly
     monotone at ρ = 1 with worst/mean = **1.9201** and **1.96039** — the 4 s raggedness is the analysis
     window meeting Dissolve's own `CloudAttackTimeSec` (0.05 → 2.0 s) and envelope-slew rows. Those
     lengths are **not** shipped, because their end-to-end effect sizes are 0.0489 and 0.0595, far under
     the 0.15 floor, and **this amendment lowers no floor**.
  3. **Entropy's spectral flatness is a four-segment WELCH estimate of the same pinned tail** — same
     last second, same 20 Hz–8 kHz band, same 65 536-point Blackman-Harris transform, four half-length
     sub-windows averaged instead of one. This is the one row where the failing statistic **was**
     measuring noise, and the evidence is arithmetic: the single-periodogram series carries a per-step
     trend of 3.57 × 10⁻⁶ and **four downward steps in its first six** (−1.02, −2.38, −0.62,
     −2.61 × 10⁻⁶), i.e. a step-to-step noise amplitude ≈ 70 % of the step signal, and it scored
     **worst/mean = 3.00286** against the 3.0 bound — for a series whose step noise is that fraction of
     its step signal, `worst/mean` is a property of the noise distribution (≈ 3 for twenty draws of
     `|N(0,σ)|`), so the clause was a coin flip rather than a discontinuity test. There is no
     discontinuity to find: `EntropyProcessor`'s four stage weights are continuous ramps
     (`entropy_processor.h:66-69`, `:235-238`) and Entropy's row spans entropy 0.20 → 0.50, crossing no
     stage floor except `kStage3Lo = 0.50` at the last step, where `stageWeight()` is 0 by construction.
     MEASURED, same render, four candidates: single periodogram ρ = 0.968831 / worst/mean 3.00286; mean
     of the [2,3) s and [3,4) s windows 0.996104 / 3.53526; band edge 3 kHz 0.997403 / 2.89313;
     **Welch-4 0.997403 / 1.81517**; Welch-8 0.998701 / 2.80624. Welch-4 is the minimum change that
     clears the bound with margin, and it touches only the estimator — not the band, not the analysis
     segment.
  4. **Dissolve's blur secondary is scored as the M/S SIDE-ENERGY FRACTION of the atmosphere's own
     contribution, not as `1 − |ρ_LR|`.** This row had never been *reached* by any run — Catch2 aborts
     a section at its first failed `REQUIRE`, and item 2's continuity failure sits three assertions
     above it — so it is being evaluated here for the first time, in this phase and in Phase 7's.
     Phase 7 chose `1 − |ρ|` on the reasoning that *"the atmosphere already ships pan spread 0.7 and
     decorrelation 0.5, so the base correlation is NEGATIVE and it is the MAGNITUDE that blur
     collapses"*. MEASURED, that premise is **refuted**: blur does not collapse the magnitude, it drives
     the correlation **further anti-phase** — signed ρ_LR runs monotonically −0.20101 → −0.401331 across
     the sweep — which is a *wider* image and is exactly what `atmosphere_engine.h:2062-2066`'s
     "progressive stereo decorrelation" means. `1 − |ρ|` scores that swing at **ρ = −0.944156** against
     a +0.9 gate, i.e. it reports a widening image as a narrowing one, because `|−0.4| > |−0.2|`. The
     side-energy fraction has no such blind spot and is the same helper Bloom's stereo-width secondary
     already uses: MEASURED **0.591072 → 0.675788, ρ = 0.961039**. Phase 7's table row —
     *"blur-induced L/R decorrelation of the atmosphere's own contribution ↑"* — is unchanged; only the
     statistic that estimates it is.

  **What this amendment does NOT change, stated so it can be checked:** the |ρ| ≥ 0.9 gate, the 3×
  no-discontinuity factor, every row of the effect-size table, the 21-step / 4 s-per-step geometry, the
  pinned partial detector and its constants, the fixed seed and note, and Arms 2 and 3 in their
  entirety.

  *Tests:* `Seraphis_MacroSweep_MovesItsAxis`, `Seraphis_MacroAndDeepParameter_Compose`,
  `Seraphis_MacroSaturatesAgainstDeepExtreme`. All `[.slow]`.
- **SC-005 — No zipper, no click, at any parameter.**

  > **Automation of a class-(b) ID engages FR-042's settling push** (Q1): the pushed plain value is
  > smoothed processor-side and re-pushed on the **absolute 64-sample control-chunk grid** of FR-042
  > amendment 1 until it settles, which is precisely the mechanism clauses 1–3 measure. The class-(b)
  > time constant FR-059 mandates is what sets both the ramp this criterion sees and the `N_chunk`
  > SC-007 counts, so the two criteria are pinned to the same number.

  > **The absolute-threshold form of this criterion is one this project has already measured and
  > withdrawn.** `specs/seraphis-phase7-voice-engine/spec.md:1455-1467` records Phase 2's verbatim
  > withdrawal of raw `maxPerSampleDelta` gates and the three defects of the control-render form. A
  > previous revision of SC-005 said *"no sample-to-sample discontinuity greater than 0.05"* — and on
  > **this very composed chain** Phase 7 measured a reference max per-sample delta of **1.14265e-04** and
  > a deliberately BROKEN build (`kSilenceRampMs = 0`, a hard cut) at **1.04115e-03** (`:1512-1514`).
  > Both are ~50× and ~500× *below* 0.05, so the 0.05 bound **passes a known-clicking build**. It was
  > near-vacuous, and it carried no positive control, so it could not distinguish "no clicks" from
  > "metric not wired up".

  *Pass condition (matched-regime, same render — Phase 7 SC-003's construction, applied to parameter
  automation instead of voice steals).* For each ID in scope, a 2 s render at 48 kHz / block 512 in which
  the parameter is automated from one extreme to the other in **64 equal steps**:
  1. *Test statistic.* For each automation step, `maxPerSampleDelta` over the **±10 ms window** centred on
     it, **positioned in the OUTPUT domain**: centred on
     `step sample + AetherReverb::getLatencySamples()` (`aether_reverb.h:2612` — 1024 samples, 21.3 ms at
     48 kHz, more than the whole window; without the shift the clause measures the wrong 20 ms of audio,
     which Phase 7 measured and recorded at `spec.md:1475-1483`).
  2. *Reference.* **One window per measured step**, of the **same 20 ms length**, drawn from the **same
     render** at offsets at least **50 ms clear of any step** (same output domain), uniformly spaced.
  3. *Bound.* `max(test statistics) ≤ 1.5 × max(reference statistics)`, with the **same number of draws
     on both sides**.
  4. *Non-finite clause (all 91 IDs, no exemptions).* No sample of the render is non-finite, tested by
     bit pattern (`isFiniteBits`), never `std::isnan` — `-ffast-math` folds it away on the macOS leg.

  *Positive controls (mandatory, both — Phase 2's rule, cited by Phase 7 at `:1500-1504`).*
  a. *Detector wiring.* The same statistic over a non-step window with a deliberately injected one-sample
     step of **2× that window's own `maxPerSampleDelta`** must **exceed** the bound.
  b. *Criterion wiring.* With **FR-059a**'s `detail::SeraphisParamSmootherBypassProbe` snapping one
     **class-(b)** smoother (FR-059) to instant — a deliberate un-smoothed write, the plugin-side analogue
     of the probe Phase 7 uses at `:1508-1511` — the same render must **fail** clause 3. The probe is
     declared plugin-side on purpose: the Phase 7 precedent is a friend struct inside `dsp/`
     (`seraphis_voice.h:97`, `:775`), and adding a bypass seam there is exactly what FR-071 forbids. A
     previous revision named this control with no requirement creating the seam and no smoothing
     requirement for it to bypass.

  **Clauses 1–3 are satisfiable because FR-059 states the mechanism.** Every in-scope ID is classified
  (a) component-internal or (b) processor-side smoothed, with file:line evidence, in the checked-in
  `kContinuityMechanism[]` table. If a step is found on some ID, the in-scope remedy is to move that ID
  into class (b) — a plugin-side `OnePoleSmoother` on the pushed value — never to exempt the ID and never
  to loosen clause 3's `1.5 ×` bound.

  **Six IDs are exempt from clauses 1–3 and carry only clause 4, on documented grounds.**
  - `kSeedId` (3). It routes to `SeraphisEngine::setSeed` (`seraphis_engine.h:353-358`) →
    16 × `SeraphisVoice::setSeed` → `applySeeds()` → `morph_.setSeed(...)` (`seraphis_voice.h:796-802`),
    and `SpectralMorphEngine` documents `setSeed` as a **configuration-time** call *"NOT to be called
    while the consumer is sounding … setSeed() redraws all 64 scatter offsets (a step of up to
    2 * kMaxScatterCents = 14 cents per partial in one chunk). They are named exemptions in FR-044's
    continuity list"* (`spectral_morph_engine.h:198-207`). `AetherReverb::setSeed` carries the same
    warning: *"Mid-render this is therefore a discontinuity in the drift and tide"* (`aether_reverb.h:2355-2364`).
    A continuity bound on `kSeedId` is unsatisfiable by construction for a correct implementation. Its
    audible effect is asserted by **SC-020** instead.
  - The five `CFG` IDs (408–412). Their gate (`seraphis_voice.h:699-719`) exists precisely because
    mid-note application is undefined; a sounding-voice push is *rejected*, so there is nothing to be
    continuous about. Asserted by **SC-013** and SC-003's `CFG` clause instead.

  The remaining **85** IDs are in scope for clauses 1–3 (91 registered, less `kSeedId` and the five
  `CFG` IDs), and SC-005's render set must include the
  `kAtmosGrainSecondsId = 30 s` and `kAetherDecayId = 60 s` + `kAetherFreezeId = on` combinations named
  under *Edge cases*.
  *Test:* `Seraphis_ParameterAutomation_IsClickFree`, with the two positive-control sections named
  explicitly.
- **SC-006 — Nothing allocates on the audio thread.** With `AllocationScope` active and readings taken via
  `AllocationDetector::instance().getAllocationCount()`, a 4-second render during which **all 91
  parameters** are automated every block records **exactly 0** allocations after `setupProcessing()`
  returns. The render MUST also exercise **`setState()` on the prepared processor** at least once, so
  FR-041b's staging-copy handoff and FR-047's `pushAllSurfaces()` are both inside the measured window —
  the 2.1 KiB staging copy is a fixed-size POD copy and must be shown to allocate nothing.
  *Test:* `Seraphis_ParameterPush_IsAllocationFree`.
- **SC-007 — On-change-only push is real, on all four surfaces.** Counted through FR-041a's
  `applyVoiceParamsCallCountForTest()`, `applySpectralStatesCallCountForTest()`,
  `applyAetherParamsCallCountForTest()` and `setTargetBasePushCountForTest()`. The first two count
  **successful applications** (for `applySpectralStates`, a call after which `spectralStatesPending_`
  cleared); the last two count **invocations**, so a per-slice re-push is visible.

  **The quiescent arm requires no parameter change AND constant tempo** (Q3). Tempo is not a parameter,
  and FR-042 amendment 2 dirties `voiceParamGeneration_` whenever the synced travel rate moves, so a
  moving tempo legitimately increments `applyVoiceParams` every block. The arm therefore holds
  `processContext` at a **fixed** tempo (or supplies none); a separate clause below asserts the moving-tempo
  behaviour rather than treating it as a violation.

  With a **quiescent** engine — no note sounding, so FR-046's gate accepts — **constant tempo**, and
  **every class-(b) smoother settled**, rendering **200 blocks** with no parameter change yields:

  | Counter | Expected after 200 unchanged blocks | Then, after one change of… | Expected delta |
  |---|---|---|---|
  | `applyVoiceParams` | **exactly 1** (FR-047's prepare-time push) | one **class-(a)** `VP` ID | **+1** |
  | | | one **class-(b)** `VP` ID | **+1 … +`N_chunk`**, the push-count bound defined below from FR-059's stated time constant |
  | `applySpectralStates` | **exactly 1** (the first in-`process()` push FR-047 assigns the prepare-derived application to; `setupProcessing()` pushes nothing itself) | one `CFG` ID | **+1** |
  | `applyAetherParams` | **exactly 1** (FR-047's prepare-time push) | one `AE` ID | **+1** |
  | `setTargetBase` | **exactly 27** (FR-047's prepare-time push, one per `MB` target) | one **class-(a)** `MB` ID | **+1** |
  | | | one **class-(b)** `MB` ID | **+1 … +`N_chunk`**, same bound |
  | **each of the four `ENG` push counters** (FR-041a, FR-045, amendment A7) | **exactly 1** each — the prepare-time push | one change of ID **3** (seed) or ID **1008** (freeze) | **+1 on that value's counter and +0 on the other three** |

  **The class-(b) rows are a bounded range, not a free pass** (Q1), and the bound is a **push count, not
  a block count** — amendment A3, 2026-08-01, which split the single `N` a previous revision used and
  which conflated the two quantities below:

  - **`N_chunk = ceil(tau · ln(D / kCompletionThreshold) / chunkSeconds)`** — the number of **pushes** a
    single class-(b) change may produce, where `tau` is FR-059(b) clause 2's stated time constant, `D`
    the plain span of the change, `kCompletionThreshold` `OnePoleSmoother`'s own settle threshold, and
    `chunkSeconds` the period of FR-042 amendment 1's 64-sample control-chunk grid. **This is the number
    the push-count rows above assert.**
  - **`N_block = ceil(settling time / block)`** — the same settling time expressed in **host blocks** of
    wall clock. **This is the number SC-003's render-length column uses** for a class-(b) row. It is not
    a push count, and asserting a counter against it is a defect.

  Neither is read off the run: both are computed from FR-059's stated time constant and the pinned block
  size. **FR-059(b) clause 2's time constant is a PER-ID COLUMN (amendment A10, 2026-08-01), so both
  numbers are per-family**, and a class-(b) row is asserted against the pair of *its own* family. At
  `D = 1.0` and 512 samples @ 48 kHz:

  | family | IDs | constant | `N_chunk` | `N_block` |
  |---|---|---|---|---|
  | body coefficients | 801, 802 | `kParamSmoothMs = 20 ms` | **28** | **4** |
  | aether depths + macros | 100–104, 1215, 1216 | `kAetherDepthSmoothMs = 300 ms` | **415** | **52** |

  The aether-depth family is entirely `MB`, so its longer tail moves `setTargetBase` (one target, one
  scalar store) and never the 37 × 16 `applyVoiceParams` fan-out. The plan §3.5.2 records the
  measurement that forced the split and the rule that generalises it. The assertion is `1 ≤ Δ ≤ N_chunk`, **and** the counter
  MUST stop rising once the smoother settles — a push that never stops fails this row even though every
  individual increment is "within range". A class-(a) change MUST produce exactly +1; a class-(b) change
  MUST produce at least +1. The classification is `kContinuityMechanism[]` (FR-059), the same checked-in
  table SC-005 uses, so the two criteria cannot disagree about which class an ID is in.

  **Two separation clauses, which are the whole reason FR-042 carries two counter pairs.** An `AE`
  change MUST NOT increment `applyVoiceParams`, and a `VP` change MUST NOT increment
  `applyAetherParams`. A single shared counter passes every row above and fails both of these.

  **One moving-tempo clause** (Q3). With `kMorphSyncId` on and a `processContext` tempo ramped across
  blocks, `applyVoiceParams` increments on **every block in which the derived rate moved by more than
  FR-056's epsilon**, and on no other block — in particular, a *constant* tempo with sync on must not
  increment it at all. `applyAetherParams` and `applySpectralStates` MUST NOT increment on any of them.
  *Test:* `Seraphis_ParameterPush_IsOnChangeOnly`.
- **SC-008 — Parameter-push CPU budget (FR-057 clause 1).**

  > **The push cost is measured directly, never by subtracting two whole-chain renders.** A previous
  > revision defined the steady-state figure as *"the delta between a run with no parameter changes and
  > the same render with the push paths compiled out via a test seam"*. 0.05 % of one core at 512/48 kHz
  > is **5.3 µs/block** against a block budget of 10 666 666.7 ns
  > (`specs/seraphis-phase7-voice-engine/spec.md:1385-1387`) — and Phase 7 recorded ten consecutive
  > best-of-16 runs of that same chain on an idle machine spanning **18.34 %–20.07 %** (`:1421-1423`), a
  > ~1.7-point (≈180 µs/block) spread, **~34× larger than the quantity being measured**. The subtraction
  > is a coin flip, not a gate. It also depended on a compile-out seam that no FR mandates and that
  > changes inlining in the surrounding code, so the two arms would not be the same binary.

  *Pass condition.* A `[.perf]` microbenchmark timing **N iterations of the actual push sequence** with no
  audio rendering in the loop:
  - **steady state** — one `process()`-entry pass with every generation counter unchanged, **every
    class-(b) smoother settled and the tempo constant** (Q1/Q3: those are the two conditions under which
    C-3's amendments do no work, and "steady state" means exactly that state): the tracker comparisons,
    the settled-check and the synced-rate comparison, and nothing else. Two bounds, **both** binding: the
    FR-057 absolute ceiling of
    **0.05 %** of the 10 666 666.7 ns block budget (5.3 µs/block), **and** a checked-in regression
    baseline at `ceil(measured worst × 1.05)` in the same shape as the worst-case arm. *The absolute
    ceiling alone is near-vacuous — the measured subject is two or three `std::size_t` comparisons and a
    bool, ~1000× under 5.3 µs — and a threshold that cannot fail is the same defect this spec documents
    and withdraws at SC-005. The regression baseline is what actually gates the clause; the ceiling
    survives only as the FR-057 statement it implements.*
  - **worst case** — the full sequence: build a `SeraphisVoiceParams` from the atomics +
    `applyVoiceParams` at polyphony 8 **and** 16 (both reported; the gate is the worse), 27 ×
    `setTargetBase`, `applyAetherParams`, and one `applySpectralStates`. Cost × 1 call/block ≤ **0.50 %**
    of the block budget, **and** ≤ the checked-in `ceil(measured worst × 1.05)` baseline.

  **Both arms MUST defeat dead-code elimination, and MUST prove they did.** A microbenchmark of
  side-effect-free tracker comparisons with no rendering in the loop is a prime DCE target and would
  otherwise measure nothing at all. Every arm therefore (i) consumes its result through an optimization
  barrier — a `volatile` sink or the repo's `benchmark::DoNotOptimize`-equivalent — and (ii) **asserts a
  strictly non-zero elapsed time** for the timed region. An arm that reports 0 ns fails the criterion; it
  does not pass it.

  Measurement discipline is Phase 7's: **best-of-16 per subject, ≥ 8 trials**, idle machine, with a
  checked-in baseline at `ceil(worst × 1.05)` and the
  `static_assert(baseline × kRegressionFactor ≤ kReference)` tie (`spec.md:1418-1427`). **No compiled-out
  arm.** *Test:* `Seraphis_ParameterPush_CpuBudget` (`[.perf]`).
- **SC-009 — Full-poly budget still holds (FR-057 clause 2).**

  > **The 16-voice form of this criterion is unsatisfiable and re-imposes a budget the engine was formally
  > exempted from.** Phase 7 deviated: `specs/seraphis-phase7-voice-engine/spec.md:1390-1394` keeps the
  > 25 % ceiling *"though at 8 voices rather than 16 (RA-1 and the phase owner's 2026-07-30 ruling, RQ-1;
  > the deviation is recorded in Traceability)"*. The measured figure at 8 voices is **20.0682 %** with a
  > run-time gate of 24.23 % (`:1421-1426`), and RA-1's model is 8 × 2.322 % + 1.787 % = 20.36 % (`:1417`)
  > — so 16 voices predicts ≈ **38.9 %**, 1.5× over the ceiling *before Phase 9 adds anything*. A previous
  > revision also said "all 83 parameters at non-default values", which leaves the implementer free to
  > pick cheap non-defaults (atmosphere density 0.1 vs 20 grains/s is a whole different cost class), so
  > even a passing run would not be reproducible.

  > **The measurement subject is a hand-built engine + reverb pair in the perf TU, NOT `Processor`.** A
  > previous revision demanded both "the Phase 9 surface engaged" (which only `Processor` can produce)
  > and `AetherReverb` at RA-1 row (c) — `numChannels = 16`, `diffusionFftSize = 4096` — which
  > `Processor` **structurally cannot** produce: `makeSeraphisReverbConfig` fixes `numChannels = 8` and
  > `diffusionFftSize = 1024` with the comment *"MUST stay 1024 -> 1024-sample latency"*
  > (`plugins/seraphis/src/engine/seraphis_engine_config.h:68`, `:77`), and that latency constancy is
  > already load-bearing for Phase 8 (`plugins/seraphis/src/processor/processor.h:62-72`). No Phase 9
  > parameter can change either field. The two demands are only compatible if the measurement is built
  > directly.

  *Pass condition — Phase 7's SC-001 scenario, pinned verbatim, with the Phase 9 surface engaged:*
  - **Construction.** A `SeraphisEngine` + `AetherReverb` pair built **directly in the `[.perf]` TU**, not
    through `Processor`. The reverb is prepared with RA-1 row **(c)** as a **deliberate worst case above
    the shipped `makeSeraphisReverbConfig`**, which is the only way to reproduce Phase 7's gate; the fact
    that the shipped plugin prepares a cheaper reverb is a margin, and is recorded as such alongside the
    figure.
  - polyphony **8**, **all 8 voices sounding**, none idle;
  - atmosphere **frozen**, engaged via `SeraphisEngine::setAtmosphereFreeze(true)` and **asserted** by
    `isFreezeCaptured()` on every voice before the measurement starts;
  - `AetherReverb` at RA-1 row **(c)**: `PrepareConfig{numChannels = 16, shimmerEnabled, bloomEnabled,
    spectralDiffusionEnabled all true, diffusionFftSize = 4096}`, `setSize(1)`, `setDensity(1)`, 32 bloom
    resonators;
  - 512-sample blocks at 48 kHz, measured on the composed chain
    (`processStereoBlock → AetherReverb::processStereoBlock → processOutputStage`);
  - **the Phase 9 addition:** an **exhaustively enumerated non-default parameter table — one stated plain
    value per ID, all 91 rows, checked into the test TU as a constant array**. It is a spec artefact, not
    an implementer's choice; the plan stage writes it and it is reviewed as part of this phase. The table
    is applied through the **four DSP routes** — `SeraphisMacroMatrix::setTargetBase`,
    `SeraphisEngine::applyVoiceParams`, `applyAetherParams` (FR-049) and
    `SeraphisEngine::applySpectralStates` — plus the direct `ENG` setters. Two exception classes are
    stated **per row** in the table:
    - **Not applicable — processor-local (8 rows).** IDs **0** (master gain), **100–104** (the macros, whose
      matrix values are set directly here) and **405/406** (the sync pair, which is a `Processor`
      computation feeding `kMorphTravelRateId`). These have no DSP route and are marked `n/a`.
    - **Pinned by the scenario (5 rows).** ID **1** (polyphony) = 8; ID **1008** (atmosphere freeze) = on;
      ID **812** (`kBodyResonatorBypassId`) = **off**, because bypassing the resonators would remove the
      body engines from the very chain this criterion budgets and would make the measurement meaningless;
      ID **1201** (`kAetherSizeId`) = 1.0 and ID **1202** (`kAetherDensityId`) = 1.0, which are RA-1 row
      (c)'s `setSize(1)` / `setDensity(1)`. *(A previous revision named "the four `AE` prepare-shaping
      values" here. There are none: `numChannels`, `diffusionFftSize`, `shimmerEnabled` and `bloomEnabled`
      are `PrepareConfig` fields, not parameters, and none of the ten registered `AE` IDs — 1202–1209,
      1213, 1214 — shapes the prepare.)*
  - **Gate: ≤ 25 % of one core** (2 666 666.7 ns/block), with Phase 7's baseline discipline
    (best-of-16, ≥ 8 trials, `ceil(worst × 1.05)` checked in).

  **The 16-voice figure is measured and recorded, not optional** (FR-057 clause 2): the same scenario is
  re-run at polyphony 16 with all 16 voices sounding, reported as a **non-gating** number, and written
  into the roadmap amendment FR-058 clause 1 mandates — so the size of the deviation from roadmap line
  313 is on the roadmap, not only in a phase spec. If the gate fails, the lever is the shipped voice count
  or Phase 9's own push cost — never the 25 % ceiling, and never a Phase 2/4/5/6 gate.
  *Test:* `Seraphis_FullPoly_CpuBudget_WithFullSurface` (`[.perf]`).
- **SC-010 — State round-trip is byte-exact.** For a randomized-but-valid setting of all 91 parameters
  (fixed seed), `getState` → `setState` → `getState` produces **byte-identical** streams of the C-8 length,
  and every controller-side value after `setComponentState` equals the value the processor holds, within
  `1e-6` normalized. *Test:* `Seraphis_StateRoundTrip_IsExact`.
- **SC-011 — Version migration.** A hand-built **36-byte version-1** stream (the Phase 8 layout) loads
  without error; all Phase 8 parameters take their stream values; all 83 Phase 9 parameters read back at
  their registered defaults; and the subsequent 4 s render **satisfies SC-002's pass condition, using
  SC-002's construction verbatim** — Arm B built same-build, same-TU from the Phase 8 shipped defaults,
  gate per-sample `maxAbsDiff ≤ 1.0e-5` over both channels, `compareFingerprints` warn-only. *(A previous
  revision said "matches SC-002's Phase 8 fingerprint", naming an artifact that does not exist and that
  SC-002 explicitly outlaws: SC-002 produces no fingerprint reference and states in bold that "a
  checked-in fingerprint as the SC-002 reference is forbidden". Read literally, the old wording
  re-introduced the cross-toolchain float golden this spec's own traceability row bans. The word
  "fingerprint" does not belong in this clause.)* A version-3
  stream is **refused** (`kResultFalse`) with no state mutated. Version-2 streams truncated at each of 12
  chosen byte offsets load without crash and leave the remainder at defaults.
  *Test:* `Seraphis_StateVersion_MigratesAndRefuses`.
- **SC-012 — Spectral-state serialization round-trips exactly.** Each of the five factory states
  (`makeFactoryState`, `spectral_state.h:373`) assigned to a slot, saved and reloaded, compares **equal
  field-by-field** (ratios, amplitudes, name, tilt, inharmonicity, numPartials) to the original — the
  documented exact round trip (`:270-272`).

  **The comparison is against `Processor`'s FR-041b `spectralSlots_` copy, read through
  `spectralSlotForTest(slot)` (FR-041a) — never against the engine slot, which cannot express it.**
  `SpectralMorphEngine::setState` stores the slot **sanitized**: *"only the FR-041-filled log2(ratio)
  array, the amplitude array zeroed at i >= numPartials, and the count. tiltDbPerOct, inharmonicity and
  name are structurally incapable of reaching the audio path"* (`spectral_morph_engine.h:285-315`), and
  the class exposes **no per-slot getter at all** (full const surface at `:392-456`). Three of the six
  named fields are unreadable there and the ratios are log2-transformed.

  A slot fed 541 bytes of garbage deserializes to `false` and leaves `spectralSlots_[slot]` **bitwise
  unchanged**. *Test:* `Seraphis_SpectralStateSlots_RoundTripExactly`.
- **SC-013 — Configure-time gating is respected, and the write converges.** Three clauses, written
  against FR-046's pending flag:
  1. Assigning a new spectral state while a voice is **sounding** leaves that voice's audible spectrum
     unchanged for the note and increments `getRejectedConfigureTimeCallCount()`
     (`seraphis_voice.h:720`).
  2. `spectralStatesPendingForTest()` (FR-041a) **stays set** for as long as any targeted voice keeps
     rejecting, and the processor never clears or resets the parameter atomics in response to a
     rejection.
  3. On the **first block after every voice has become quiescent**, the retry succeeds:
     `spectralStatesPendingForTest()` clears, `getRejectedConfigureTimeCallCount()` stops rising on every
     voice, `morph().getStateCount()` equals the pushed count, and the next note-on renders the new
     spectrum. **All sixteen voices hold the same state at that point** — asserted across
     `getVoice(i).morph().getStateCount()` for `i < kMaxVoices`.

  Clause 3 is why FR-046 carries a pending flag rather than a bare generation counter: with generation-only
  application, nothing would re-trigger the push and this clause would be unreachable.
  *Test:* `Seraphis_SpectralStateAssignment_HonoursGate`.
- **SC-014 — Phase 8 IDs are untouched.** For each of the eight Phase 8 IDs, `getParameterInfo` returns the
  same `id`, `stepCount`, `defaultNormalizedValue`, `units` and flags as the Phase 8 build, compared against
  a checked-in table of the eight infos. *Test:* `Seraphis_Phase8Parameters_AreFrozen`.
- **SC-015 — uidesc control-tags match the registered set exactly.** Parsing
  `SERAPHIS_RESOURCES_DIR "/editor.uidesc"` yields a `<control-tag>` tag-value set **equal** to the set of
  91 registered IDs — no missing tag, no orphan tag. The eight existing `<view>` elements still bind, and
  the type of each bound view still matches C-6 (`CSlider`/`COptionMenu`/`CCheckBox`).
  *Test:* `Seraphis_UidescControlTags_MatchRegisteredIds`.
- **SC-016 — Editor lifecycle stays clean.** `exerciseEditorLifecycle(controller, "editor", uidescPath, 3)`
  completes with the enlarged parameter set, with **zero reports**, under both:
  (a) the **valgrind-nightly editor-lifecycle job**, which already builds and runs `seraphis_tests`
  (`.github/workflows/valgrind-nightly.yml:273-283`), and
  (b) a **local `-DENABLE_ASAN=ON` Debug run**.
  *(Corrected: there is no ASan CI lane. `.github/workflows/` contains only `ci.yml`, `docs.yml`,
  `release.yml` and `valgrind-nightly.yml`, and a case-insensitive search for `sanitiz`/`asan` across them
  returns only prose comments. ASan is a local opt-in build per root `CLAUDE.md`. Adding an ASan lane is
  not in this phase's scope; if a later phase wants one it must be an FR of that phase.)*
  *Test:* `Seraphis_EditorLifecycle_SurvivesFullSurface`.
- **SC-017 — pluginval strictness 5 is clean.**
  `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`
  exits 0, including its automated parameter sweep over all 91 parameters. Recorded as the captured log.
- **SC-018 — Host-synced travel is correct and degrades safely.** Five clauses, computed against C-7's
  eight-row note-value table:
  1. **Derivation.** 120 BPM, sync on, note value `1 Bar` (index 4), 4/4: the pushed travel rate equals
     `120 / (60 · 4) = 0.5` journeys/s within `1e-5`.
  2. **Upper clamp.** 200 BPM, note value `1/16` (index 0, `beatsPerJourney = 0.25`):
     `200 / (60 · 0.25) = 13.33` journeys/s, which clamps to `kMaxTravelRate = 1.0`
     (`spectral_morph_engine.h:102`).
  3. **No clamp at the slow end, asserted as an exact value.** 20 BPM, note value `8 Bars` (index 7,
     32 beats at 4/4): `20 / (60 · 32) = 1.0417e-2` journeys/s, which is **above**
     `kMinTravelRate = 1/600 = 1.667e-3` (`:101`); the pushed rate equals that value within `1e-5` and
     **no clamp engages**.
  4. **Time signature.** With `kTimeSigValid` set and 6/8, `barBeats = 6 · (4/8) = 3`, so `1 Bar` at
     120 BPM gives `120 / (60 · 3) = 0.667` journeys/s; with the flag clear, the same setting falls back
     to `barBeats = 4` and 0.5 journeys/s.
  5. **Fallback.** With `processContext == nullptr` or an invalid tempo flag the free-running
     `kMorphTravelRateId` value is used unchanged and the render does not go silent.

  > **The `kMinTravelRate` clamp is unreachable through the sync path, and a previous revision asserted
  > it anyway.** That revision required *"at 20 BPM with a 16-bar value the clamp to `kMinTravelRate`
  > engages"*. 16 bars at 4/4 is 64 beats, giving `20/(60·64) = 5.21e-3` journeys/s — **three times
  > above** `kMinTravelRate`. Engaging the lower clamp needs `beatsPerJourney > 10 · BPM`: 200 beats
  > (50 bars) at 20 BPM, which no plausible eight-entry musical list reaches, and C-7's longest entry is
  > 32 beats. A correct implementation would have failed that clause. The lower clamp survives as a
  > defensive bound on a non-finite or hostile tempo, and is exercised by clause 5's fallback path
  > instead; clause 2 asserts the clamp that **is** reachable.

  **These five clauses check the *derivation*, not the *cadence*, and are satisfiable by a single push
  after a parameter change — which is why the every-block recompute FR-056 mandates (Q3) is asserted by
  **SC-007's moving-tempo clause** instead. Neither criterion covers the other; both are required.
  *Test:* `Seraphis_MorphSync_DerivesAndFallsBack`.
- **SC-019 — Sample-rate independence.**

  *Analysis, pinned* (a previous revision named no band, FFT size or window, and spectral centroid
  computed to Nyquist is not comparable between 44.1 and 96 kHz **by construction**): the **settled last
  second** of a 4 s render of note 60 at fixed seed and identical parameter settings, 65 536-point FFT,
  **Blackman-Harris** window, metrics computed over the **20 Hz – 16 kHz** band only — a band all three
  rates resolve.

  *Thresholds, consistent with the components inside the chain* (a previous revision demanded 2 % on all
  three metrics, **6× tighter** than Phase 5's own rate criterion for a component in this very signal
  path — `specs/seraphis-phase5-atmosphere/spec.md:1354-1372` allows output RMS within **1.0 dB** and mean
  concurrent grain count within **5 %**, and records that `RollingCaptureBuffer::prepare` rounds capacity
  **up to the next power of two**, giving an **8.8 %** rate-dependent spread in ring seconds — 11.89 s at
  44.1 kHz vs 10.92 s at 48/96 kHz — that propagates straight into the atmosphere's output):
  - output **RMS** within **1.0 dB** across 44.1 / 48 / 96 kHz;
  - band-limited **spectral centroid** within **5 %**;
  - **spectral flatness is not gated** — it is recorded as a measurement. It is dominated by the
    stochastic atmosphere and reverb tails, whose realisation is exactly what the power-of-two ring
    rounding changes between rates.

  The "no denormalization reads `sampleRate`" clause has moved to **FR-019**, where a review/lint action
  belongs. *Test:* `Seraphis_ParameterSurface_IsSampleRateIndependent`.
- **SC-020 — Seed determinism.**

  *Operating point, pinned* (a previous revision pinned nothing — no note, duration, rate, block size or
  parameter setting — so neither clause was reproducible): note **60**, velocity **100**, held 3 s then
  released, **4 s** total, **48 kHz**, block **512**, polyphony 8, registered defaults **except**
  `kCloudDriftDepthId` (205) at **25 cents** and `kBodyMaterialId` (800) at **Glass**. Both deviations are
  required, not cosmetic: at the registered defaults cloud drift depth is **0.0 cents**
  (`seraphis_voice.h:295`), and `ContinuousBody::setSeed` drives *"exactly one thing — the per-voice modal
  micro-detune … on the three MODAL materials only"*, with Strings and Chamber documented
  **seed-independent** (`continuous_body.h:1335-1348`). Measuring at the defaults on a non-modal material
  would switch off most of the seed's influence and then assert it is audible.

  *Clause 1 (determinism).* Two `Processor` instances with identical parameters including `kSeedId`
  produce renders matching within `render_fingerprint.h` tolerance.

  *Clause 2 (distinctness) — a property of C-10's checked-in seed table.* Two instances differing
  **only** in `kSeedId` produce renders whose total-variation metric differs by more than a threshold
  **derived from measurement, not chosen**: the plan stage renders all **16 entries of C-10's
  `kSeedValues`** at the operating point above, records the pairwise seed-to-seed total-variation spread,
  and sets the gate at `floor(min observed spread / 1.05)` in the same ceil/floor style Phase 7 uses for
  its perf baselines (`specs/seraphis-phase7-voice-engine/spec.md:1418-1427`). The measured table is
  checked into the test TU alongside the constant.

  **Because the sixteen seeds are curated constants (C-10, decided 2026-08-01), a small spread is a
  defect of the table and is fixed by re-picking the offending constant and re-measuring.** Lowering the
  gate is not an available remedy, and neither is re-examining the engine's seed derivation — that is the
  dependency the curated table exists to remove. **A threshold that is not backed by that measurement is
  not shippable.**

  *Clause 3 (default preservation).* `kSeedValues[0] == 1u`, so `kSeedId` at its registered default seeds
  the engine and the reverb exactly as Phase 8's `kEngineSeed` / `kReverbSeed` do
  (`seraphis_engine_config.h:28-29`). Asserted directly, because SC-002's negative control depends on it.
  *Test:* `Seraphis_Seed_IsDeterministicAndDistinct`.
- **SC-022 — Registered defaults are exact, per ID (C-4's own criterion).** For **each** of the 91 IDs,
  `getParameterInfo(i).defaultNormalizedValue` fed through that ID's own
  `handle<Section>ParamChange(params, id, value)` stores a plain value that compares `==` — exact float
  equality, no tolerance — to a **checked-in table of C-6's *Default* column**. No render, no engine, no
  audio: a pure table test over the six packs plus the two Phase 8 packs.

  *This exists because C-4 is a normative **per-parameter bit-identity** requirement and its only
  criterion was SC-002, an aggregate 4 s render gate at `maxAbsDiff ≤ 1.0e-5`. That gate can pass while
  several defaults are off by an ULP or more — an `mn`/`mx` pair whose normalized default does not invert
  exactly is the obvious case — and when it fails it localizes nothing, reporting one worst sample index
  across 91 parameters. SC-022 is the direct check; SC-002 goes back to being what it is meant to be, the
  negative control on **behaviour**.*

  **The eight IDs added on 2026-08-01 carry their default from the component's own member initializer,
  not from `SeraphisVoice::prepare()`** — `prepare()` touches none of them (verified this session:
  `seraphis_voice.h:319-327` sets eight atmosphere values, none of them these). The checked-in table's
  rows for 811, 812 and 1011–1016 are therefore the values C-6's note cites: `continuous_body.h:163-164`
  and `atmosphere_engine.h:2352-2356`, `:2292`.
  *Test:* `Seraphis_RegisteredDefaults_AreExact`.
- **SC-023 — A preset loaded into a prepared processor reaches the DSP (FR-047's `pushAllSurfaces()`).**
  This criterion exists because it is the phase's headline deliverable and, before the 2026-08-01 ruling
  (Q2), nothing covered it: SC-010 is a byte round-trip and SC-011 checks registered defaults, so a
  `setState` that changed 90 stored values and pushed **none** of them would have shipped green.

  *Pass condition.*
  1. Prepare a `Processor` (`setupProcessing()` completes), then render one block so every prepare-time
     push has already happened and its trackers are equalised.
  2. Build a **non-default** state stream in which **every one of the 91 parameters** differs from its
     registered default — the C-8 layout, written by the same `getState` path, from a processor whose
     atomics were driven to **this criterion's own value table**, checked into its test TU. Feed it to
     `setState()`.

     **The table is SC-023's, not SC-009's, and it has no pinned and no `n/a` rows.** SC-009's table is a
     CPU-measurement artefact: it pins five IDs to the values its scenario requires (polyphony **8**,
     `kBodyResonatorBypassId` **off**, atmosphere freeze **on**, `kAetherSizeId` / `kAetherDensityId`
     **1.0**) and marks eight rows `n/a` because they have no DSP route. Reusing it here would make this
     criterion vacuous at exactly the rows most likely to break: a `setState` that silently dropped
     polyphony would still pass, because 8 is the registered default. **SC-023 is a state round-trip
     criterion, not a CPU criterion**, so every row carries a value that differs from the registered
     default — in particular `kPolyphonyId` = **16** (default 8) and `kBodyResonatorBypassId` = **on**
     (default off), the two SC-009 pins the other way.

     **The eight processor-local IDs (0, 100–104, 405, 406) participate.** They have no DSP route, but
     they *are* persisted state fields (C-8), so they carry non-default stored values in this table like
     every other row — that is what makes the stream genuinely non-default across the whole C-8 layout.
     Clause 4's route assertions do not reach them (there is no route to read back); their persistence is
     SC-010's subject and their audible effect SC-004's. They may **not** be omitted from the stream:
     leaving them at their defaults would leave the byte range that most exercises FR-093's reader
     untested. "All except the pinned ones" is **not** an acceptable weakening of this clause — there are
     no pinned ones.
  3. Render **one** block.
  4. Assert, for **every route**, that the DSP now holds the preset value — using the same read-back
     surfaces SC-003 names, and no others:
     - the 37 `VP` rows through `getVoice(i)` for every `i < kMaxVoices`;
     - the 27 `MB` rows through `macroMatrixForTest().getTargetBase(...)` **and** the post-slice voice-side
       read-back (polyphony pinned to 16, per SC-003's `MB-voice` clause);
     - the 10 `AE` rows through the `AE` table's observables, or — where SC-003 shows no getter exists —
       through the value FR-049's `applyAetherParams` was called with, captured by
       `applyAetherParamsCallCountForTest()` incrementing plus a re-push comparison;
     - the 4 `ENG` rows through `getPolyphony()` / `getAtmosphereFreeze()` / `getSeed()` / the soft-limit
       state;
     - the 5 `CFG` rows through `spectralSlotForTest(slot)` field-by-field **and**
       `getVoice(i).morph().getStateCount()` once `spectralStatesPendingForTest()` has cleared (the
       engine is quiescent here, so it clears on that first block).
  5. Assert `spectralSlotsHandoff_` was consumed exactly once — the FR-041b staging copy reached
     `spectralSlots_` on the audio thread and not on the message thread.
  6. **Negative control.** With the `pushAllSurfaces()` call removed from `setState()` (a compile-time
     test-TU switch, or by driving `setState` on a processor whose helper is stubbed), the same assertions
     MUST fail. Without this, the criterion cannot distinguish "the preset reached the DSP" from "the DSP
     already held those values".
  7. **The re-prepare arm — a sample-rate change must re-deliver every surface.** This clause exists
     because the *Edge cases* → **Sample-rate changes** bullet names re-prepare as "the single most likely
     implementation slip in the phase" and, before it, no criterion asserted it: SC-002 cannot (defaults
     match defaults) and clauses 1–6 never call `setupProcessing()` twice.

     Continuing from the processor of clauses 1–5 — prepared at **44 100 Hz**, holding this criterion's
     non-default table on every route, and having rendered at least one block — call `setupProcessing()`
     again at **96 000 Hz** and render **one** block. Assert:
     - **(a) `prepare()` re-ran on the engine.** For every `i < kMaxVoices` and every
       `ContinuousBody::Engine`, `getVoice(i).body().getEngineSampleCount(e)` reads **0** immediately after
       the re-prepare and before the new block — the counter is documented *"Cleared by reset()/prepare()"*
       (`continuous_body.h:1532-1537`) and was non-zero at the end of clause 3's render.
     - **(b) `prepare()` re-ran on the reverb, at the new rate.** `AetherReverb::isPrepared()`
       (`aether_reverb.h:2486`) is true and `getEffectiveDelayLengthSamples(ch)` (`:2506`) has changed in
       proportion to the rate ratio for every channel — the tank lengths are stored at
       `kReferenceSampleRate` and rate-scaled inside `prepare()` (`:1561`), so an un-re-prepared reverb
       reads the 44.1 kHz lengths back unchanged.
     - **(c) `pushAllSurfaces()` re-delivered every route.** Repeat **clause 4 verbatim** — the same
       per-route read-back assertions, over the same 37 `VP` / 27 `MB` / 10 `AE` / 4 `ENG` / 5 `CFG` rows,
       against the same non-default value table. A freshly prepared voice carries
       `SeraphisVoice::prepare()`'s defaults, so every row that fails to be re-pushed reads its **default**
       and the assertion fails loudly.
     - **(d) Negative control.** With `pushAllSurfaces()` stubbed out of `setupProcessing()` (the same
       test-TU switch clause 6 uses), (c) MUST fail while (a) and (b) still pass — which is what
       distinguishes "the surfaces were re-pushed" from "the components were merely re-prepared".

     **This clause creates no new introspection.** It uses only the FR-072 accessors and getters that
     already exist on the shipped components (`getEngineSampleCount`, `isPrepared`,
     `getEffectiveDelayLengthSamples`) plus the FR-041a/FR-041b/FR-049 test seams clause 4 already names,
     honouring the spec's rule that no criterion may depend on introspection no requirement creates.

  *Tests:* `Seraphis_PresetLoadAfterPrepare_ReachesDsp` (clauses 1–6),
  `Seraphis_SampleRateChange_RePushesEverySurface` (clause 7).
- **SC-021 — Portability and lint gates.** `node tools/check-portability.js` clean;
  `node tools/lint-odr.js`, `lint-layers.js`, `lint-float-bit-goldens.js`,
  `lint-arch-guarded-includes.js` and `lint-simd-aligned-loadstore.js` clean; clang-tidy `-Target seraphis`
  and `-Target dsp` clean; zero compiler warnings on all three OS legs.

---

## Edge cases

**RT-safety boundaries**

- `applyVoiceParams` on a **prepared but never-rendered** engine: `getPolyphony()` is already the
  prepare-time value (`seraphis_engine.h:203`), so the fan-out is well-defined from the first call.
- `applyVoiceParams` called with `getPolyphony()` reduced between calls: **the slots above the new
  polyphony are still summed into the output.** `setPolyphony` force-idles them with `voices_[i].noteOff()`
  and records `orphanTail_ |= voiceBit(i)` when `!voices_[i].isFinished()` (`seraphis_engine.h:337-349`,
  whose own comment says *"the voice keeps rendering its tail because isRendering()'s second clause is
  !isFinished()"*), and `processStereoBlock`'s loop bound is `v < kMaxVoices` **unconditionally**
  (`:437`, `:464-486`). A previous revision of this bullet asserted the opposite — *"those slots are
  neither summed nor allocatable"* — which is false in both halves: the allocator may hand an orphan out
  again at any moment (`:335-338`). FR-002 therefore bounds `applyVoiceParams` at `kMaxVoices`, which
  covers a shrink's orphan tail **and** a grow's newly-allocatable slot, and removes the need for the
  "polyphony increase re-dirties the voice-param generation" special case a previous revision carried
  (see FR-047).
- The 27 `MB`-routed values still stop at `apply()`'s own `getPolyphony()` bound
  (`seraphis_macro_matrix.h:625-626`), because FR-004/FR-071 forbid changing that function's shape. That
  is pre-existing Phase 7 behaviour, recorded in C-2 rather than silently inherited.
- `applySpectralStates` fans out to all `kMaxVoices` (FR-005) for the same reason, plus one of its own: a
  slot the allocator hands out later must already carry the states.
- A parameter change arriving in the **same block** as a note-on: `processParameterChanges` runs before any
  slice (Phase 8's `process()` order), so the note is voiced with the new value. Deliberate.
- `process()` called before `setupProcessing()`: Phase 8's not-ready early-out already zero-fills and sets
  `silenceFlags = 3`; no Phase 9 push may run on that path.

**Parameter extremes**

- `kCloudRichnessId` at 0 with `kBodyMixId` at 1: the cloud is at its minimum partial count and the body is
  fully wet. Output must be non-silent-or-silent by physics, never non-finite. Covered by SC-005's
  non-finite clause.
- `kAtmosGrainSecondsId` at 30 s against the shipped `captureSeconds = 4.0f`
  (`seraphis_engine_config.h:49`): the grain length exceeds the capture ring. `AtmosphereEngine` owns this
  case (its own `kMaxGrainSeconds = 30.0f`, `atmosphere_engine.h:300`); the plugin must not second-guess it,
  and SC-005 must include this combination.
- `kAetherDecayId` at 60 s with `kAetherFreezeId` on: freeze is the "infinite" mode
  (`aether_reverb.h:2736`); the two are independent controls and both must be reachable simultaneously
  without energy growth. Phase 6's freeze energy-conservation criterion (±0.5 dB over 60 s) still governs.
- `kMorphPositionId` above `kMorphStateCountId − 1`: `setTargetPosition` clamps to `[0, numStates−1]`
  (Phase 3 FR). The registered range is fixed at 0…3, so the parameter is *addressable* beyond the active
  count and simply clamps. **The formatter displays the RAW 0…3 value and the DSP clamps silently.** A
  previous revision required the clamped value to be displayed, which the mandated formatter shape cannot
  do: FR-014/FR-061 pin the Phase 8 signature
  `format<Section>Param(Vst::ParamID, Vst::ParamValue, Vst::String128)` (`global_params.h:129-133`), which
  receives **one** ID and **one** value and has no access to `kMorphStateCountId`'s current value. Widening
  it for one parameter would break the six-function contract every pack shares, for a cosmetic gain.
- `kEnvStage0MsId` / `kEnvStage1MsId` at 0 in **Growth** mode: `setEnvelopeMode(Growth)` already forces
  every pre-sustain stage to 0 ms (`seraphis_voice.h:562-577`) while preserving the shadow, so the
  parameter must still read back what was set (`getEnvelopeStageTimeMs`, `:599`). The formatter shows the
  stored value; the audible envelope is Growth's.
- Master gain 2.0 with everything maximal: the output stage's true-peak limiter is unconditional
  (`global_params.h:41-45`) and Phase 8's SC-006 ceiling bound still applies.

**Sample-rate changes**

- `setupProcessing()` at a new rate re-runs `prepare()` on engine and reverb (the only allocating paths) and
  must then re-push **every** surface per FR-047 — otherwise the freshly prepared voices carry
  `SeraphisVoice::prepare()`'s defaults, not the user's preset. This is the single most likely
  implementation slip in the phase and SC-002 does **not** catch it (defaults match defaults); SC-010's
  controller-parity clause and **SC-023 clause 7** — the re-prepare arm, 44.1 kHz → 96 kHz, which asserts
  both that `prepare()` re-ran on engine and reverb and that `pushAllSurfaces()` re-delivered every route —
  do.
- No denormalization may read the sample rate (SC-019). Every time-domain parameter is stored in seconds or
  milliseconds and converted inside the DSP component that owns the rate.

**Seed determinism**

- `kSeedId` reaches `SeraphisEngine::setSeed` (`:353`), which re-derives every slot seed, and
  `AetherReverb::setSeed` (`:2361`). `SeraphisVoice::setSeed` → `applySeeds()` → `ContinuousBody::setSeed`
  rebuilds a detune cache (`continuous_body.h:1345-1348`) and is documented **configure-time**, so the seed
  must be pushed on change only and its audible effect on the body is defined only from the next note.
  The spec does not claim retroactive re-seeding of a ringing body, and SC-020 measures a fresh instance.
- Two plugin instances at the same seed are intentionally identical (Phase 8 recorded this at
  `seraphis_engine_config.h:21-23`); `kSeedId` is precisely the escape hatch, which is why it is a
  **preset-visible, persisted** parameter and not a hidden constant.

**State**

- A stream written by a build with a different `kSpectralStateFormatVersion` is refused per slot by
  `deserializeSpectralState`'s version byte check (`spectral_state.h:284-286`), leaving that slot at its
  factory default while the rest of the preset loads. The plugin must not treat one bad slot as a bad
  preset.
- A DAW that calls `setState` before `setupProcessing` (legal, and already handled for polyphony by Phase 8
  FR-023 clause 2) must have every Phase 9 value applied at prepare, not at the next parameter change.
- A DAW that calls `setState` **after** `setupProcessing` — the ordinary preset switch, and the more
  common of the two — must have every Phase 9 value applied on the **next `process()` call**, not at the
  next parameter change. FR-047's shared `pushAllSurfaces()` — reached from `setState()` through A4's
  release-store request — is the mechanism and **SC-023** is the criterion; before the 2026-08-01 ruling
  (Q2) this case was uncovered by every criterion in the phase.
- A host that calls `setState` **concurrently with `process()`** (some do, and pluginval's stress paths
  do) writes only `spectralSlotsStaging_`, the index handoff and FR-047's release-store request; the
  audio thread's `spectralSlots_` and its ~40 on-change trackers are never written from the message
  thread and never torn. FR-041b's staging-ring contract and A4's request seam are what make this safe
  without a lock.

---

## Resolved Questions

Both questions the roadmap deferred to this spec were **ruled on by the phase owner on 2026-08-01** and
are recorded here as decisions. Nothing in this section is open; the FR/SC body above states the decided
behaviour directly, and this section exists so the reasoning and the roadmap obligations are on the record.

> **OQ-CLOSURE (satisfied, and still binding on the implementation change).** Roadmap line 561 says open
> questions are *"resolve[d] in the relevant spec"*, and **Phase 9 was the last phase either of these was
> assigned to** — roadmap Phases 10–12 own effects, UI and presets, and neither question appeared in any
> of them. RQ-1 was assigned to Phase 9 **by name** by Phase 3
> (`specs/seraphis-phase3-spectral-morph/spec.md:404-409`); RQ-2 was a "Phase 8/9 scope call" that Phase 8
> punted here. Therefore:
>
> 1. The resolution of each question is written into **this document** as a numbered decision, with its
>    consequences reflected in the FRs and SCs. ✅ *(RQ-1 → *Non-goals* + FR-058 cl. 4; RQ-2 → FR-064 +
>    FR-058 cl. 5.)*
> 2. Because **both** answers are "not in Phase 9", `specs/Seraphis-roadmap.md` MUST be edited **in the
>    same change** to give each question a **named owning phase** — Phase 11 for RQ-1, the new per-note
>    expression phase for RQ-2. **FR-058 clauses 4 and 5 carry that obligation**, alongside clause 3's
>    mandatory strike of roadmap Open Question 2 (line 564). A deferral that leaves no spec and no roadmap line owning the
>    question is not a resolution and does not close this phase.
> 3. Neither answer is "still open at release", which is the one outcome that was not available — RQ-2
>    especially, because deciding it after release carries a permanent host-cache hazard on the controller
>    FUID.

> **Roadmap Open Question 2 is already resolved and is NOT RQ-1.** A previous revision re-opened it as
> an open question and mapped it in Traceability, conflating a **closed roadmap question** with an
> **inherited deliverable**. Read this session: roadmap Open Question 2 (line 564, was 514) asks *"Spectral state authoring: factory-only or
> user-morphable/savable states — Phase 3/9"*, and Phase 3 answered it —
> `specs/seraphis-phase3-spectral-morph/spec.md:207-208` records the mutators as *"Deferred to Phase 9 by
> Clarifications C-9, **which resolves the roadmap's Open Question 2**"*, and C-9 (`:404-409`) states the
> authoring surface: **`SpectralState` is assignable and serializable; nothing in the library derives
> one**, and a capture path is *"rejected outright"*. Phase 9's job is not to answer that question again;
> it is to (i) implement the answer — assign + serialize in the DSP, factory selection at the plugin,
> which C-8/FR-092 and C-6's IDs 408–412 do — and (ii) **strike roadmap Open Question 2 (line 564) from the Open Questions
> list in the same change, marked resolved by Phase 3**, per OQ-CLOSURE clause 2 and **FR-058 clause 3**.
> That strike is **mandatory**, not conditional on RQ-1's answer, and it is subject to FR-058 clause 2's
> citation re-verification sweep.

- **RQ-1 — Do the three `SpectralState` authoring mutators ship in Phase 9?**
  **DECIDED 2026-08-01: no. Phase 11 inherits them, by name, together with the validity criterion.**

  Phase 3's C-9 assigned `setPartial(index, ratio, amplitude)`, `blendStates(A, B, t)` and
  `tiltState(state, dB)` — as free functions over the struct — to **Phase 9 by name**
  (`specs/seraphis-phase3-spectral-morph/spec.md:404-409`), on the reasoning that Phase 9 is *"the phase
  that first has a user-facing reason for them"*. It is not: the only user-facing consumer is a
  per-partial editing surface, which is a **UI** deliverable and belongs to Phase 11, so shipping the
  mutators here would ship dead API — which this project's simplicity rule forbids. Striking them
  entirely was also rejected: it would foreclose a Phase 11 feature the state format was deliberately
  sized for.

  **Phase 3's attached obligation lands with them.** Phase 3 states that the mutators *"must preserve
  FR-012's validity invariants, and that preservation is **Phase 9's criterion to state**"*. Phase 9
  states it, and states it as **Phase 11's**: when Phase 11 ships the three free functions it MUST carry
  an SC asserting that, for a table of adversarial inputs (out-of-range ratios, non-monotone ratios,
  amplitudes outside `[0,1]`, `numPartials` outside `[0,64]`, non-finite arguments built from bit
  patterns per the `-ffast-math` rule), each mutator leaves its `SpectralState` satisfying
  `isValidSpectralState` (`spectral_state.h:82`). **FR-058 clause 4** writes that inheritance into the
  roadmap's Phase 11 entry in the same change; FR-071's carve-out list is **not** extended, and
  `spectral_state.h` is not touched by this phase.

  *Consequence to the state format: none.* C-8 persists the full 541-byte payload per slot (FR-092)
  precisely so this answer costs no format version — Phase 11 can make states user-editable without a
  version 3.
- **RQ-2 — MPE / poly-aftertouch.**
  **DECIDED 2026-08-01: it ships, in a new named phase — not Phase 9, and not "no".**

  Roadmap Open Question 5 (line 572, was 517) reads *"MPE / poly-aftertouch support (natural fit for Bloom-per-note)
  — Phase 8/9 scope call."* Phase 8 resolved it as **Phase 9's** and recorded the reasoning and the hazard
  in `plugins/seraphis/CLAUDE.md`: the engine's note API is
  `noteOn(std::uint8_t note, std::uint8_t velocity)` / `noteOff(std::uint8_t note)`
  (`seraphis_engine.h:370`, `:415`) with **no per-note expression input at all**, so implementing
  `INoteExpressionController` today would have nothing to drive; and adding an interface to an
  already-released controller FUID can invalidate host-cached class metadata.

  The ruling: **per-note expression is wanted**, so "decided no" is off the table; but it cannot be Phase 9
  either, because the DSP half — per-voice expression inputs on `SeraphisVoice` — is exactly the kind of
  `dsp/` change FR-071 forbids, on an already-large phase. **A new named phase owns both halves**: the
  `SeraphisVoice` per-voice expression inputs *and* `INoteExpressionController`. Roadmap Open Question 5 is
  **moved** to that phase, not struck (**FR-058 clause 5**), and **the controller-FUID host-cache hazard is
  accepted and recorded** there, so it is not rediscovered later as a surprise. FR-064's prohibition on
  adding `INoteExpressionController` in Phase 9 is now **unconditional**.

---

## Traceability

| Roadmap statement (Phase 9, lines 453–460 post-amendment) | Requirements | Criteria |
|---|---|---|
| "Every engine parameter registered" | FR-010, FR-014, FR-016, FR-060, FR-070 (13 forwarders), FR-072 (**14** accessors, A6) | SC-001, SC-003 |
| "denormalized in `processParameterChanges()`" | FR-017, FR-018, FR-040 | SC-003, SC-005 |
| "atomics in processor" | FR-014, FR-041, FR-041a, FR-041b, FR-048 | SC-006 |
| "state save/load with versioning" | FR-012, FR-047 (`pushAllSurfaces()`), FR-090, FR-091, FR-091a, FR-093 | SC-010, SC-011, **SC-023** |
| "spectral-state serialization (Phase 3 round-trip)" | FR-041b, FR-092, FR-094 | SC-012 |
| "macro system wired" | FR-003, FR-004, FR-043, FR-050, FR-051, FR-055 | SC-002, SC-004 |
| "Editor-lifecycle harness enrollment" | FR-100, FR-102 | SC-015, SC-016 |
| "Pluginval + full state round-trip tests" | FR-101, FR-104 | SC-017, SC-021 |
| Roadmap line 186 "host-synced slow ramp" (assigned to Phase 9 by Phase 3) | FR-056, C-7 | SC-018 |
| Roadmap line 113 "smoothness control" (BrownianDrift) | FR-070 #1 | SC-003 |
| Roadmap line 148 "individual attack/decay offsets" | FR-070 #2 | SC-003 |
| Roadmap line **245** "Pitch drift per grain" *(was 246 — that line is the pan/decorrelation continuation)* | FR-070 #3, #4 | SC-003 |
| Roadmap line 186 "SplineTrajectory" travel | FR-070 #5 | SC-003 |
| Roadmap line 313 full-poly CPU budget, **as amended in place by FR-058 cl. 1** (8-voice gate, **Phase 7's** RQ-1 ruling of 2026-07-30 — not this document's RQ-1 — and both measured figures). The unamended line states 16 voices and nothing in the repo satisfies it. | **FR-057 cl. 2, FR-058** | SC-009 |
| Roadmap line 75 "8–16 voices" vs the shipped 1…16 `kPolyphonyId` dropdown (`global_params.h:104-112`) — polyphony 9…16 is user-reachable and outside the budgeted scenario | **FR-058 cl. 6** | SC-009 (non-gating 16-voice measurement) |
| Roadmap line 564 (OQ 2, was line 514) — **already resolved by Phase 3** (`specs/seraphis-phase3-spectral-morph/spec.md:207-208`); Phase 9 implements the answer and strikes the line | C-8, FR-092, C-6 IDs 408–412, **FR-058 cl. 3** | SC-012 |
| Phase 3's *inherited deliverable* — the three authoring mutators and their validity-preservation criterion (`spec.md:404-409`) | **RQ-1** (decided: not Phase 9), **FR-058 cl. 4** | **Phase 11 by name**, in the roadmap's Phase 11 entry |
| Roadmap line 572 (OQ 5, was line 517) — per-note expression | **RQ-2** (decided: ships, new named phase), **FR-064** (unconditional), **FR-058 cl. 5** | — (owned by the new phase; the FUID hazard is recorded there) |
| Clarification Q1 — class-(b) push cadence and its time constant | **FR-042** amendment 1, **FR-059(b)** cl. 1–2, C-3 | SC-005, **SC-007** (bounded `+1 … +N` rows), SC-008 (settled steady state) |
| Clarification Q2 — `setState` after prepare must reach the DSP | **FR-047** (`pushAllSurfaces()`), FR-042 amendment 3, FR-091 | **SC-023** |
| Clarification Q3 — synced travel rate recomputed every block | **FR-056** (cadence + epsilon + sample point), FR-042 amendment 2, C-3 | SC-007 (constant-tempo arm + moving-tempo clause), SC-018 |
| Clarification Q6 — `MB-voice` read-back voice range | — (test-side pin, no FR) | **SC-003** `MB-voice` clause (polyphony pinned to 16); C-2's residue unchanged |
| Clarification Q7 — the sixteen `kSeedId` entries | **C-10**, FR-015 (`kSeedValues` + `static_assert`) | **SC-020** cl. 2 (table property) and cl. 3 (`kSeedValues[0] == 1u`) |
| Clarification Q8 — `spectralSlots_` concurrency | **FR-041b** (three-deep staging ring + `std::atomic<int>` handoff/consuming pair, A5), FR-090, FR-091 | SC-006 (allocation-free across `setState`), SC-023 cl. 5 |
| FR-070 expansion ruling (2026-08-01) — ATMOSPHERE SET + BODY SET, surface 83 → 91 | **FR-070 #6–#13**, FR-072 (2 new accessors), FR-010, FR-013, FR-015, C-6, C-8 (v2 from the start) | SC-001, SC-003 (8 new rows), SC-005, SC-006, SC-009, SC-010, SC-017, SC-022 |
| FR-070 #12's interaction with FR-033a (`ee408854`) | **FR-070 #12** cl. (i)–(iii) | SC-003 (no level assertion), SC-005 (FR-059 classification), SC-002 (default is `on`) |
| C-4 registered defaults are bit-identical to the shipped engine defaults | C-4, FR-014 | **SC-022** (per-ID, exact), SC-002 (aggregate negative control, **same-binary Arm B**, `maxAbsDiff ≤ 1e-5`) |
| Phase 8's eight registered IDs keep their type, ID, default and unit | **FR-063**, C-9, FR-010 | **SC-014** (`getParameterInfo` vs the checked-in table of eight infos) |
| Edge case "Sample-rate changes" — re-prepare must re-push every surface | **FR-047** (`pushAllSurfaces()` called from `setupProcessing()`), FR-072 | **SC-023 cl. 7** (44.1 k → 96 k re-prepare arm), SC-010 (controller parity) |
| The four `ENG` values are pushed on change only, and it is checkable (amendment A7) | **FR-045**, FR-041a (the four `eng*PushCountForTest()` accessors) | **SC-007** (`ENG` row: exactly 1 after 200 unchanged blocks; +1 on the changed value and +0 on the other three) |
| Display formatting: every pack formats its own band, and **no** formatter claims a dropdown ID (amendment A8) | **FR-061**, FR-015 (`dropdown_mappings.h` is the single label table) | **SC-001** — the `getParamStringByValue` formatting section of `Seraphis_ParameterSurface_IsComplete` |
| SC-003's read-back observables (13 IDs have none) | **FR-072**, FR-071 carve-out | SC-003 |
| SC-005's continuity mechanism and its positive-control seam | **FR-059, FR-059a** | SC-005 |
| C-7 sync note values + bar→beat rule | FR-015, FR-056, C-7 table | SC-018 |
| Cross-cutting: RT safety (line 550) | FR-002, FR-048 | SC-006 |
| Cross-cutting: layer discipline (line 551) | FR-006, FR-071 | SC-021 |
| Cross-cutting: ODR sweep (line 552) | New components table (all six DSP groups of FR-006, incl. `applySpectralStates`, the 13 forwarders and the **14** accessors) | — |
| Cross-cutting: CPU budgets are FRs (line 553) | **FR-057** | SC-008, SC-009 |
| Cross-cutting: no bit-exact float goldens (line 555) | FR-094 (byte stream, legitimate) | SC-002 (same-build `maxAbsDiff ≤ 1e-5`, fingerprint warn-only; a checked-in fingerprint reference is **forbidden**), SC-010 (byte stream, legitimate) |
| Cross-cutting: sample-rate handling — *rate independence of the surface* | — | SC-019 |
| Cross-cutting: sample-rate handling — *no denormalization reads `sampleRate`* | FR-019 | **— (compliance-pass review item: file:line citations + the recorded `grep` output, per FR-019)**. SC-019 explicitly disowns this clause, so the table must not claim it as coverage. |
| Phase-9 test seams (no SC may depend on introspection no FR creates) | FR-041a, FR-041b, FR-049 | SC-003, SC-004, SC-007, SC-008, SC-012, SC-013 |
| Cross-cutting: portability (line 556) | FR-104 | SC-021 |
| Cross-cutting: `k{Section}{Parameter}Id` naming (line 558) | FR-010, C-6 | SC-001 |

---

## Review notes

Issues raised in review that were **not** applied exactly as suggested, with the reason. Everything else
in each review round was applied.

**Round 2 (2026-08-01):** every blocker and major was applied in full; no issue was rejected. Three
entries below record where the *mechanism* of the fix differs from the literal suggestion — the roadmap
amendment (encoded as FR-058 rather than applied by this revision) and the inherited Phase 3 citation
error (recorded, not edited, because Phase 3's document is outside this phase's surgical scope). Where a
suggestion offered alternatives, the choice taken is stated in the FR or SC itself: SC-003's missing
read-backs took option **(a)** (FR-072 creates them, FR-071 gains one named carve-out) rather than
rewriting eleven rows as rendered differentials, because the accessors are additive `const` surface and
leave the layer and ODR stories untouched; FR-019's Traceability row took the *"— (compliance-pass review
item)"* option rather than inventing a new lint.

- **REJECTED — "FR-070 #2 cites the wrong roadmap line; 'individual attack/decay offsets' is line 147, not
  148."** It is line **148**. Read from `specs/Seraphis-roadmap.md` this session, the Phase 2 bullet is:

  ```
  147  - Composes `HarmonicOscillatorBankSimd` (per-partial freq/amp/phase already proven in Innexus) with
  148    new per-partial state: stereo position, drift amount, individual attack/decay offsets.
  149  - Per-partial `BrownianDrift` (shared-state, decimated: one drift evaluation per partial per block).
  ```

  Line 147 is the bullet's opening clause and names no offsets; the quoted phrase is on line 148. The
  review's own supporting claim — that "line 148 is `Per-partial BrownianDrift` (shared-state,
  decimated…)" — is off by one: that is line **149**. FR-070 #2 and its Traceability row keep 148
  unchanged.

  The **other** two line corrections in the same issue were correct and **have** been applied: the pitch-drift
  citation is line **245** (246 is its continuation, *"(per-grain pan spread + decorrelation via
  `stereo_utils`)"*), and C-5's band-list citation is **399–401** post-amendment. FR-070 #3/#4, the
  Traceability rows, C-5 and FR-010's mandate over `plugin_ids.h:46` all carry the corrected numbers.

- **APPLIED, with the scope stated — "amend `specs/Seraphis-roadmap.md:313` in the same change".** The
  ruling is taken (option (a)): the 8-voice gate is a deviation that belongs on the roadmap, in the shape
  the Phase 5 amendment already used there (`:250-254`). It is encoded as **FR-058**, a requirement of
  this phase, rather than applied to the roadmap by this spec revision — the same mechanism the
  OQ-CLOSURE box already uses for the two open questions (*"`specs/Seraphis-roadmap.md` MUST be edited in
  the same change"*). A spec revision is not the phase's implementation change; FR-058 is what makes the
  roadmap edit land with it, and FR-058 clause 2 carries the citation re-verification sweep that the
  insertion forces, because every roadmap citation after line 313 shifts. Applying the edit here and now
  would have silently invalidated ten citations in this document alone.

- **NOTED AS PRE-EXISTING, not fixed here — the inherited "roadmap line 184" in
  `specs/seraphis-phase3-spectral-morph/spec.md:197`.** The corrected line for *"travel driven by
  `SplineTrajectory` or host-synced slow ramp"* is **186** (verified this session), and every citation of
  it **in this document** has been corrected — FR-070 #5, the inclusion-criterion paragraph under
  *Non-goals*, C-7's rationale and both Traceability rows. The identical error in the Phase 3 spec is out
  of this phase's surgical scope: Phase 3 is complete and its document is not one this phase edits. It is
  recorded here so it is not mistaken for a fresh defect, and FR-058 clause 2's sweep — which must in any
  case re-walk every roadmap citation after the line-313 amendment — is the natural place to pick it up
  if the phase owner wants it corrected.

