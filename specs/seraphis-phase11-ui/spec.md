# Feature Specification: Seraphis Phase 11 — UI (Organism-First Editor)

**Spec slug:** `seraphis-phase11-ui`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 11 (roadmap lines 475–536), plus the *Modulation
routing* row of the Reuse Inventory (roadmap line 94) and Key Design Decision 5 (roadmap lines 79–80).
**Depends on:** Phase 8 (`plugins/seraphis/`, ✅), Phase 9 (91-parameter surface, state v2, spectral-state
serialization, ✅), Phase 10 (16 effects IDs, state v3, surface = 107, ✅), Phases 1–7 (`SeraphisEngine`,
`SeraphisVoice`, `HarmonicCloud`, `SpectralState`, ✅).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-08-03

---

## Overview

Every one of the 107 registered parameters exists, denormalizes, persists and reaches the DSP
(`plugins/seraphis/src/controller/controller.cpp:46-54`), and **not one of them has a usable editor**.
`resources/editor.uidesc` is still the Phase 8 placeholder: 420 × 300 px, eight bound views, and a banner
that says so in the file (`editor.uidesc:3-5`, `:15-20`). Phase 11 replaces that file wholesale and builds
the interface the roadmap has been describing since the Architecture Overview: the **cloud view** — a live
partial constellation that *is* the window — with the five macros as ring knobs orbiting it and every deep
parameter in a pull-up drawer along the bottom edge.

Six facts read out of the code this session are what make this more than a layout exercise, and they are
what this spec principally decides:

1. **The per-partial data the view draws already exists and needs no new DSP read API.**
   `SeraphisEngine::getVoice(std::size_t) const` returns `const SeraphisVoice&`
   (`dsp/include/krate/dsp/systems/seraphis_engine.h:955`), `SeraphisVoice::cloud() const` returns
   `const HarmonicCloud&` (`dsp/include/krate/dsp/systems/seraphis_voice.h:827`), and `HarmonicCloud`
   exposes `getPartialFrequencyHz` (`harmonic_cloud.h:955`), `getPartialCurrentAmplitude` (`:959`),
   `getPartialAntiAliasGain` (`:973`), `getPartialPosition` (`:986`), `getPartialDriftDetune` (`:991`) and
   `getActivePartialCount` (`:950`) as a documented *"public contract, not `#ifdef` scaffolding"*
   (`:945-947`). The producer is a read loop over `kMaxPartials = 64` (`:138`), nothing more.

2. **A ratio edit dragged onto the live cloud would be erased inside 64 samples.**
   `SeraphisVoice::processStereoBlock` re-pushes the morph engine's output into
   `cloud_.setSpectralTarget(...)` on every control chunk (`seraphis_voice.h:989`), and the FR-086 cadence
   banner makes that a **contract**, not an implementation detail (`harmonic_cloud.h:734-753`). So the
   roadmap's per-partial editing *cannot* write the cloud directly for ratio/amplitude: those edits must
   land in the **`SpectralState` slot**, which is exactly what the three inherited mutators do. **Pan and
   mask are different** — `setPartialPosition` (`:1069`) and `setPartialMask` (`:1084`) are not part of the
   spectral target, so nothing overwrites them, and they *are* written directly. C-4 states this split.
   **Landing in the slot is not, by itself, audible on a sounding voice** — `SeraphisVoice::setSpectralState`
   rejects the push unless `!hasSounded_ || isFinished()` (`seraphis_voice.h:770-776`, `:908`). The
   phase-owner ruling (D1) relaxes exactly this gate for the ratio/amplitude edit path, because the
   underlying primitive it forwards to, `SpectralMorphEngine::setState`, already arms a click-free
   absorption crossfade whenever the target slot contributes (`spectral_morph_engine.h:311`,
   `slotContributes` at `:558`) — Phase 11 reuses that machinery rather than building a second one. See
   *Clarifications* D1 and C-4.

3. **`SpectralMorphEngine::setState` rejects an invalid state wholesale** —
   `if (!isValidSpectralState(s)) { return; }` (`spectral_morph_engine.h:296-298`). An authoring mutator
   that left a state one epsilon outside `isValidSpectralState` (`spectral_state.h:82`) would therefore be
   **silently inert**, not audibly wrong. That is why Phase 3's validity criterion travels with the
   mutators (roadmap lines 524–533) and why it is SC-012 here rather than a note.

4. **`SpectralState::tiltDbPerOct` is structurally incapable of reaching the audio path.** `setState`
   stores the slot *sanitized* — *"only the FR-041-filled log2(ratio) array, the amplitude array zeroed at
   `i >= numPartials`, and the count. `tiltDbPerOct`, `inharmonicity` and `name` are structurally incapable
   of reaching the audio path"* (`spectral_morph_engine.h:285-289`). A `tiltState` that only wrote that
   field would be an **inaudible control**. It must bake the tilt into `amplitudes`. C-6 decides how, and
   the arithmetic that keeps the result valid is proved there, not asserted.

5. **The transport for the edits already exists and is proven.** The processor owns a **three**-deep
   staging ring for spectral slots — `spectralSlotsStaging_`, `spectralSlotsHandoff_`,
   `spectralSlotsConsuming_`, `stagingWriteCursor_` (`plugins/seraphis/src/processor/processor.h:858-866`),
   published with `store(..., std::memory_order_release)` at `processor.cpp:1416` and consumed on the audio
   thread at `:2823-2831`. Phase 9 built it for `setState`; Phase 11 adds a **second writer on the same
   message thread** and changes nothing about the interlock. And Phase 9 already persists the **full
   541-byte payload** per slot rather than the factory index, explicitly *"the only way Phase 11 … can make
   states user-editable without a second format version"* (`specs/seraphis-phase9-parameters/spec.md:853-856`).
   **State format version stays 3.**

6. **The audio-thread headroom is 2.68 percentage points, measured, not assumed.** Phase 10's SC-014
   worst-of-seven at the 8-voice gate is **2 380 980 ns/block = 22.32 % of one core** against the unchanged
   25 % ceiling `kFullPolyCeilingNs = 2 666 666.7` ns
   (`specs/seraphis-phase10-effects/compliance.md` SC-014; constants at
   `plugins/seraphis/tests/integration/param_perf_test.cpp:392`, `:395`, `:472`). Phase 11's only
   audio-thread addition is the snapshot producer, and SC-009 budgets it inside that headroom rather than
   against a hope.

   > **CORRECTION (2026-08-04) — this premise names the wrong subject, and it is what SC-009, SC-010,
   > SC-014 arm 7 and SC-031 fail against. See *Open Escalations* → OE-1.** `2 380 980 ns` is the
   > **chain-only** subject: `Seraphis_FullPoly_CpuBudget_WithFullSurface` times three calls on a
   > hand-built `SeraphisEngine`/`AetherReverb` pair, with **no `Processor`**, no effects stage and no
   > control-chunk slice loop in its timed region (that file's FINDING 1). The four criteria above then
   > measure **whole-`Processor::process()`**, which strictly *contains* the chain plus all of that. The
   > 2.68 points of headroom do not exist at the subject those criteria measure — whole-`process()` at the
   > same operating point is **30.69 %**, measured cold. The premise is not adjusted here and the ceiling
   > is not moved; the discrepancy is escalated intact.

Everything else in this phase is VSTGUI, in three custom view classes and one replaced `.uidesc`, exactly
as the roadmap's *"Custom-view surface is exactly three"* commitment requires (roadmap lines 516–517) —
plus two **non-UI** obligations: the macro reach into the effects surface Phase 10 handed forward by name
(C-10, FR-037, SC-021), and the phase-owner's D1 ruling relaxing one voice-level gate so a ratio/amplitude
edit reaches a sounding voice audibly (FR-033a, SC-028 – SC-030).

**A note on roadmap line numbers.** Every roadmap citation in this document was re-derived against
`specs/Seraphis-roadmap.md` as it stands on 2026-08-03. The Phase 11 prose block is **lines 479–485**, the
ASCII sketch **487–503**, the layout commitments **507–517**, the deferred questions **519–522**, and the
inherited-mutators paragraph **524–536**. An earlier revision of this spec carried a systematic ~2-line
drift in that first block; the citations below are the corrected ones.

---

## Clarifications

### Session 2026-08-03

- **Q1 (where the controller gets authoritative slot contents for the Edit surface):** → Controller-side
  mirror — `std::array<SpectralState, 4>` on the controller (C-11), re-seeded from `makeFactoryState()` on
  each 409–412 dropdown change and from the state stream (`morph_params.h`'s `loadMorphParamsToController`
  stops discarding the four 541-byte payloads — scope explicitly widened to include this file). Mutators
  run locally on the mirror **and** are sent via `EditMessage`. The mirror is **display-only**; the
  processor's staging ring remains the sole audio-thread authority.
- **Q2 (Blend A→B: which slot is A, and is it absolute or compounding):** → Absolute, via a latched
  pristine A — a gesture-begin message (`EditMessage` kind 7, `BlendBegin`) snapshots A on mouse-down;
  every subsequent `t` re-blends that snapshot into the selected slot, matching C-6's absolute,
  non-compounding contract for `tiltState`. Cost accepted: one extra message kind plus one 541-byte
  message-thread scratch.
- **Q3 (do pan/mask edits persist across save/load):** → Yes — append a `[partials]` block (**272 B**: 64
  pan floats = 256 B, plus two 64-bit masks = 16 B) **last** in the stream, with `kCurrentStateVersion`
  staying **3**, relying on the existing EOF-safe strict-prefix loader chain. Old streams still load; older
  binaries ignore the tail. (The size is exact, not approximate — see FR-034a for the arithmetic and for
  why the two masks move as one 64-bit field each rather than as pairs of 32-bit halves.)
- **Q4 (what guarantees an edit to the selected slot is audible):** → Non-blocking indicator — add the
  morph travel position to `CloudFrame` (one float) and the view marks the selected slot "not currently
  sounding" when it does not contribute. No parameter is written by the UI mode toggle; the user parks the
  journey themselves.
- **Q5 (how a user unmasks a partial they masked):** → Add `std::uint64_t maskBits` (plus an
  override-bits field) to `CloudFrame`, filled from the processor's `partialOverrides_` table. Masked
  partials draw as hollow rings at a fixed minimum radius; click toggles unmask. `sizeof(CloudFrame)` and
  its `static_assert` change; SC-006 gains an arm; the zero-amplitude edge case gets a hit target.
- **Q6 (the vertical drag's reference, and authoring with no note held):** → Edit mode uses a fixed
  reference pitch (C4 = 261.63 Hz) when no voice sounds, and draws the selected slot's authored ratios
  against it (via Q1's mirror); drift is **excluded** from the inverse map so a drag cannot bake momentary
  Brownian detune into the stored ratio. Authoring works with no note held.
- **Q7 (what happens when the host opens more than one editor view):** → Refcount on the **controller** —
  it sends "open" only on the 0→1 transition and "close" only on the 1→0 transition, and resets the count
  in `terminate()`. The processor keeps its single gate exactly as C-2 clause 6 specifies — a
  `std::atomic<bool>`, relaxed on both sides (D-6), not a plain `bool`, because the message thread writes
  it and the audio thread reads it; the seam SC-001/SC-010 assert against is unchanged.
- **Q8 (is a gesture's final edit-message value guaranteed to be sent):** → Yes — a 30 Hz throttle **and**
  a mandatory final flush on mouse-up: at most one message per 33 ms per gesture, plus exactly one terminal
  message carrying the gesture's final value.
- **OQ1 (morph state-slot A–D placement — reconfirms RQ-1):** → Adopt the roadmap suggestion, as RQ-1
  already resolved: the slot selectors and travel controls live inside the Morph drawer tab; the Edit-mode
  mini-toolbar displays the selected slot without duplicating the selector. No body change beyond this
  reconfirmation.
- **OQ2 (the freeze cluster — reconfirms RQ-2):** → Yes, ships, exactly as RQ-2 already resolved: an
  always-visible three-button header cluster in addition to the drawer-tab bindings. No body change beyond
  this reconfirmation.
- **OQ3 (window sizing — reconfirms RQ-3):** → Fixed at 1000 × 700 for this phase, exactly as RQ-3 already
  resolved: no `setAllowedZoomFactors`, no `onSize` relayout. No body change beyond this reconfirmation.

### Session 2026-08-03 (phase-owner rulings, plan §11)

- **D1 — a ratio/amplitude edit is inaudible on a sounding voice (plan D-1/OQ-A):** → **RELAXED, not
  disclosed.** Plan's default (A) — ship as-is with a "will apply on next note" indicator — is
  **overruled**. "Drag a partial and hear it move" is the product contract, so
  `SeraphisVoice::setSpectralState`/`setSpectralStateCount`'s `isConfigurable()` gate
  (`seraphis_voice.h:770-776`, `:908`) is **relaxed for these two calls only** — every other call gated by
  `isConfigurable()` is unchanged — so an edit lands on a currently-sounding voice instead of being
  rejected and counted in `rejectedConfigCalls_`. The push reaches `SpectralMorphEngine::setState`, which
  already arms its FR-047 absorption crossfade whenever the target slot contributes
  (`spectral_morph_engine.h:311`, `slotContributes` at `:558`) — Phase 11 reuses that existing click-free
  machinery rather than inventing a parallel fade. This widens Phase 11's closed `dsp/` set (*Non-goals*,
  FR-033a) by exactly this one relaxation. The "applies on next note" disclosure indicator is **not
  built**. **Encoded in:** Overview fact 2, *Non-goals* (SCOPE AMENDMENT), C-4, FR-029, FR-033a, SC-028,
  SC-029, SC-030, *Edge cases*. A dated amendment note is added to
  `specs/seraphis-phase3-spectral-morph/spec.md` (see that file) recording that its own FR-042/FR-044
  already establish `setState`/`setStateCount` as continuity-safe while sounding — the "NOT to be called
  while the consumer is sounding" class comment at `spectral_morph_engine.h:199-200` over-grouped them with
  `reset()`/`setSeed()`, and it was that over-grouping `SeraphisVoice`'s own gate cited as justification.
- **D2 — SC-021(c)'s named observable does not exist (plan D-2/OQ-B):** → **Accepted, replacement
  observable adopted.** `effectsPushes_` (`processor.cpp:1821`) is incremented only inside
  `pushEffectsParams()`, whose ID set does **not** include 1410 or 1441 — both are class-(b) smoothed
  (`:3051`, `:3052`, bypass predicate at `:2351`) — so a *correct* FR-038 implementation leaves it unchanged
  on a Dissolve/Entropy move and SC-021(c) as originally worded would fail on correct code. Replaced with:
  with the deep knob held still, moving `kMacroDissolveId` changes `composedFxDelaySendForTest()` and
  moving `kMacroEntropyId` changes `composedFxWanderDepthForTest()` on the **next** `process()` call, and
  `composedEffectsRecomputeCountForTest()` equals the `process()`-call count. **Encoded in:** FR-038,
  SC-021(c).
- **Composition cadence — one-block lag (plan §4.2):** → **Accepted as designed, no spec weakening.**
  `computeEffectsTargets()` is read once per `process()` call, **before** `pushMacroSurfaces()` refreshes
  the macro/base values for that call, so a macro or deep-knob move reaches the composed value on the
  **next** `process()` call (10.67 ms at 512/48 kHz) — inside the 20 ms class-(b) smoothing time both
  consumers already impose, so it is a target-arrival delay, never a discontinuity. SC-021(a)'s sweep is
  read allowing exactly one block of settle per point. **Encoded in:** SC-021(a).
- **fundamentalHz's source (plan §11 non-blocking note):** → **`frequencyHz[0]` is forbidden by name.**
  `CloudFrame::fundamentalHz` for the Q6 inverse map MUST come from the focus voice's tracked note —
  `dispatchEvent` bookkeeping or an engine per-voice note read-back, whichever the implementation verifies
  exists — and MUST NOT be `frequencyHz[0]`, which is drift-inclusive and would defeat Q6/SC-024's
  drift-exclusion guarantee. **Encoded in:** C-2 clause 3.
- **OQ-4 — `.amount` for C-10's two new macro rows, and SC-017(a)'s octave threshold:** → **Methodology and
  acceptance band decided now; the two numbers themselves stay pending the plan's pilot measurement
  (plan §10.4), measured then fixed, never chosen and never relaxed afterwards.** Starting values for the
  pilot: `0.35` Dissolve → `FxDelaySend`, `0.50` Entropy → `FxWanderDepth`. Acceptance band for the
  `.amount` pilot: the isolated send-return RMS at Dissolve = 1 MUST land between **−20 dB and −6 dB**
  relative to the dry sum, with the five-point sweep strictly monotone. The measured `.amount` values and
  the measured, rounded-down SC-017(a) octave figure MUST be written back into this spec (SC-017(a),
  C-10) **before compliance** — not left as a placeholder a compliance pass quietly keeps. This resolves
  the *Resolved questions → Still open* item; that heading is removed and OQ-4 moves into *Resolved
  questions* below, marked pending-measurement rather than pending-decision.
  **Write-back status (2026-08-04):** one of the three numbers is measured and written back
  (`Dissolve → FxDelaySend` = `0.20f`, −19.3 dB, in band); **two are still outstanding and block
  compliance** (the `Entropy → FxWanderDepth` five-point table, and SC-017(a)'s octave figure). The
  per-item table is in *Resolved questions* → OQ-4; the outstanding items are marked in place in C-10
  clause 1 and SC-017(a).
- **SC-008's tolerance policy:** → **Confirmed as already worded, no change.** `1e-5` relative stands
  unless the plan's pilot run measures a larger spread, in which case the **measured number** is recorded
  in this spec and the criterion re-stated with it — never relaxed to fit a failing run after the fact.
  SC-008 already states this; this entry records that the policy was reviewed and accepted, not amended.
- **The extra `dsp/` test TU (plan §13, T003):** → **Accepted.** `dsp/tests/unit/systems/
  seraphis_partial_fanout_test.cpp` is a dedicated TU for FR-033's `SeraphisVoice`/`SeraphisEngine`
  fan-out pass-throughs, rather than folding those cases into an existing TU — failing-test-first beats
  file-count minimalism here. No spec.md body change; this is a test-organization decision recorded in
  `plan.md` §13 and `tasks.md` T003.
- **The task-list CMake-registration pattern (plan §13, tasks.md T027):** → **Accepted as authored.**
  Each task that creates a new TU appends its own file to the relevant enumerated CMake list so it runs
  red-then-green from its own task; T027 stays the single authoritative audit-and-complete pass over both
  `CMakeLists.txt` files. No spec.md body change.

---

## Scope

Phase 11 ships, and nothing else:

1. **`resources/editor.uidesc`, replaced wholesale** — the organism-first layout of roadmap lines 487–503:
   slim header, full-window cloud view, five macro rings anchored around it, pull-up drawer with seven
   tabs. VSTGUI only; no platform-specific view, ever (root `CLAUDE.md`, *Cross-Platform Requirement*).
2. **Three custom views, and exactly three** (roadmap lines 516–517): `CloudView`, `MacroRingKnob` (one
   class, five instances), `DrawerContainer`. Drawer knobs stay plain uidesc controls. The drawer/cloud
   sub-tree also gets the roadmap's *"standard sub-controller treatment"* (roadmap line 484) — one
   `DelegationController`, which is **not** a custom view and does not count against the three (C-7).
3. **The cloud-frame data path**: a `CloudFrame` DataExchange payload published once per `process()` call
   by the processor and consumed by the controller — the Membrum `MetersBlock` piggyback pattern
   (`plugins/membrum/src/processor/meters_block.h`, `processor.cpp:808-873`, `:1136-1163`;
   `plugins/membrum/src/controller/controller.cpp:1694-1740`). **No new queue, no polling IMessage loop.**
4. **One controller → processor IMessage channel** carrying the editor-open gate (now controller-refcounted,
   Q7) and the five Edit-mode edit kinds plus the Blend gesture-begin kind (C-5). The processor has no
   `notify()` override today (grep for `notify` in `plugins/seraphis/src/processor/processor.cpp` returns
   nothing); Phase 11 adds one.
5. **Observe / Edit mode** on the cloud view, with the Edit-mode mini-toolbar: drag partials, Blend A→B
   slider, Tilt dB control (roadmap lines 512–515).
6. **The three inherited `SpectralState` authoring mutators** — `setPartial`, `blendStates`, `tiltState` —
   as Layer 2 **free functions** in `dsp/include/krate/dsp/processors/spectral_state.h`, which the header
   was deliberately shaped to accept (`:13-15`: *"HAS NO MEMBER FUNCTIONS — every operation is a free
   function, which is what keeps the deferred authoring mutators a pure addition later"*), **plus Phase 3's
   validity-preservation criterion over them** (roadmap lines 528–533 → SC-012).
7. **The per-partial engine surface** the mutators exist to drive: pan and mask reach
   `HarmonicCloud::setPartialPosition` / `setPartialMask` through two additive Layer 3 fan-outs; ratio and
   amplitude reach `setSpectralTarget` *indirectly*, through the slot the morph engine already pushes
   (C-4), landing on a **currently-sounding voice audibly** rather than deferred to the next note-on
   (D1, FR-033a). Roadmap lines 532–536.
8. **Macro reach into the cloud is shown, not simulated** (roadmap lines 507–508): the perturbation the
   rings produce is the real `SeraphisMacroMatrix` response read back out of the snapshot.
9. **Macro reach into the *effects* surface**, the obligation Phase 10's RQ-4 handed to this phase by name
   (`specs/seraphis-phase10-effects/spec.md:182-183`, `:1867-1876`): two new `SeraphisMacroMatrix` rows —
   Dissolve → the spectral-delay send and Entropy → the stereo-wander depth — through a fourth target
   owner built in the shape of the existing Aether owner. C-10, FR-037, FR-038, SC-021.
10. **Tests**: uidesc binding completeness, custom-view lifecycle under the shared harness, snapshot
    determinism and RT-safety, mutator validity over an adversarial table, macro-reaction observables
    (cloud *and* effects), and the re-run of Phase 7/9/10's 25 % gate with the producer active.
11. **A controller-side `SpectralState` mirror** (C-11, Q1): `std::array<SpectralState, 4>` on the
    controller, re-seeded from `makeFactoryState()` on every 409–412 dropdown change and from the state
    stream — which requires `morph_params.h`'s `loadMorphParamsToController` to stop discarding the four
    541-byte payloads into its scratch buffer (`:521-532`). This is a scope-widening change to that file,
    the only one this phase makes there. The mirror is **display-only**; it never reaches `process()` and
    is never serialized.
12. **Partial-edit persistence** (Q3): an appended `[partials]` block in the state stream, format version
    unchanged at **3** (FR-034a).

---

## Non-goals (what other phases own)

- **No new registered parameter, and no change to any registered parameter's type, range or default.**
  The 107-ID surface (`plugins/seraphis/tests/unit/parameter_surface_test.cpp:233-234`) is closed. The
  frozen-type legend (`plugin_ids.h:184-240`) and Phase 9's C-9 apply unchanged; the roadmap says it in
  four words — *"No param-type swaps on registered IDs, ever"* (roadmap line 486). UI state that is **not**
  a parameter (drawer open/closed, active tab, Observe/Edit mode, selected partial) is session state on the
  controller, never a `ParamID`, and never enters the state stream.
- **No state format version bump.** `kCurrentStateVersion` stays **3** (`plugin_ids.h:27`). Phase 9's
  full-payload slot serialization is what makes ratio/amplitude/tilt edits persist with no format change
  (Overview fact 5), and Phase 11's appended `[partials]` block (Q3, FR-034a) is what makes pan/mask edits
  persist the same way — **an append, never a new version.**
- **No `dsp/` behaviour change at the defaults.** Phase 11 admits a **closed, enumerated** set of additive
  `dsp/` changes, all listed in *New components*: three Layer 2 free functions, two Layer 3 fan-out method
  groups, C-10's fourth macro-target owner (two enum values, one POD, one `computeEffectsTargets()`,
  two table rows, two compile-time guards), and D1's one voice-level gate relaxation (FR-033a). **Not one
  existing line of DSP behaviour moves *at the shipped defaults***, and the composition C-10 adds is the
  algebraic identity at the shipped macro neutrals — the matrix's own banner says *"at neutral every term
  is exactly 0 - applyModCurve(c, 0) == 0"* (`seraphis_macro_matrix.h:778`), which is what lets SC-001 keep
  its exact-equality form. If an implementation finds itself editing an existing DSP function *body* beyond
  adding a new `case`/row **or FR-033a's one named relaxation**, the design has left this spec.

  > **SCOPE AMENDMENT (2026-08-03, phase-owner ruling "relax the gate", D1).** Clarification exposed that
  > `SeraphisVoice::setSpectralState`/`setSpectralStateCount` reject a state push whenever a voice
  > `hasSounded_` and is not `isFinished()` (`seraphis_voice.h:770-776`, the `isConfigurable()` predicate at
  > `:908`), citing `SpectralMorphEngine`'s own "configuration-time calls" comment
  > (`spectral_morph_engine.h:198-207`) as justification. But Phase 3's FR-042/FR-044 already establish
  > `setState`/`setStateCount` as **continuity-safe** while a voice sounds — they are absorbed by the
  > FR-047 crossfade and are *not* among FR-044's two named exemptions (`setSeed()`, `reset()`) — so
  > `SeraphisVoice`'s gate was stricter than the primitive it forwards to requires, and a live ratio/
  > amplitude edit was silently deferred to the next note-on rather than heard. **One `dsp/` edit is
  > admitted by ruling:** `SeraphisVoice::setSpectralState`/`setSpectralStateCount` (`:770-780`, `:908`)
  > stop routing through `isConfigurable()` and always forward to `morph_.setState`/`setStateCount` —
  > every *other* caller gated by `isConfigurable()` (construction-time seeding, freeze/steal paths) is
  > unchanged. `spectral_morph_engine.h:199-200`'s class comment is corrected in the same change to name
  > only `prepare()`, `reset()` and `setSeed()` as calls not to make while sounding — `setState`/
  > `setStateCount` are removed from that list, matching what FR-042/FR-044 already proved. See FR-033a,
  > SC-028 – SC-030, and the dated amendment note in
  > `specs/seraphis-phase3-spectral-morph/spec.md`.
- **No new DSP class at any layer.** The three new classes are VSTGUI views in `plugins/seraphis/src/ui/`;
  C-10 adds a POD `struct`, not a class with behaviour.
- **No *further* macro-matrix change beyond C-10's two rows.** The Phase 7/9 rows, bases, amounts and
  curves are untouched; no existing target's owner or base moves. Phase 10's RQ-4 named Phase 11 as the
  discharge point for *"macro reach into the effects surface"*
  (`specs/seraphis-phase10-effects/spec.md:182-183`), and this spec discharges it **by routing two macro
  axes into two effects targets** (C-10), not by re-reading the obligation as UI reachability. Exposing
  the sixteen effects IDs in the FX drawer tab is a *separate* deliverable (C-3) and does not discharge
  RQ-4 on its own. The other half of RQ-4 — *"shipped patches with non-zero sends"* — remains Phase 12's,
  exactly as RQ-4 assigned it.
- **No presets, no preset categories.** Phase 12 owns the library and the category set
  (`plugins/seraphis/CLAUDE.md`, *Preset categories are additive-only*). Phase 11 ships the **browser
  button and view instance** over the existing `PresetManager` (`controller.cpp:57-58`) and the existing
  single seeded category `Textures`.
- **No per-note expression.** Phase 13 (roadmap lines 548–569).
- **No resizable-window infrastructure beyond what RQ-3 resolves.** No `IPlugViewContentScaleSupport`
  work, no zoom-factor menu.
- **No metering beyond what the cloud view draws.** No VU meters, no spectrum analyser, no CPU readout.
  Membrum's `MetersBlock` is the *pattern* being reused, not the payload.

---

## Existing components (verified this session)

Every row was opened this session; signatures are quoted verbatim from the cited line.

| Component | Header / file (layer) | What Phase 11 reuses — verified signature |
|---|---|---|
| `HarmonicCloud` | `dsp/include/krate/dsp/systems/harmonic_cloud.h` (L3) | The whole snapshot source, all under the *"FR-008 test/introspection surface (public contract, not `#ifdef` scaffolding)"* banner (`:945-947`): `[[nodiscard]] std::size_t getActivePartialCount() const noexcept` (`:950`), `float getPartialFrequencyHz(std::size_t i) const noexcept` (`:955`), `getPartialCurrentAmplitude` (`:959`), `getPartialAntiAliasGain` (`:973`), `getPartialPosition` (`:986`, documented *"Stereo position of partial `i` in [-1, +1]"*), `getPartialDriftDetune` (`:991`, documented *"as a frequency MULTIPLIER"*). Edit targets: `void setPartialPosition(std::size_t index, float position) noexcept` (`:1069`), `void setPartialMask(std::size_t index, bool active) noexcept` (`:1084`), `void clearPartialMask() noexcept` (`:1101`). Capacity `static constexpr std::size_t kMaxPartials = 64` (`:138`); control grid `kControlChunkSamples = 64` (`:144`). |
| `HarmonicCloud` override lifetime | same header | **The three events that silently clear a pan override**, each cited because FR-030 exists for them: `setStereoSpread` → `positionOverridden_.fill(false)` (`:545`, comment at `:544`: *"a setPartialPosition override lasts until the next spread change"*); `setSeed` → `positionOverridden_.fill(false)` (`:703`, doc at `:699-701`); `reset()` → `positionOverridden_.fill(false); masked_.fill(false);` (`:331-332`). Mask survives spread and seed changes; it does **not** survive `reset()`. |
| `SeraphisVoice` | `dsp/include/krate/dsp/systems/seraphis_voice.h` (L3) | `[[nodiscard]] const HarmonicCloud& cloud() const noexcept` (`:827`) and `const SpectralMorphEngine& morph() const noexcept` (`:828`) — the read path, already public and const. `void setSpectralState(int slot, const SpectralState& s) noexcept` (`:770`) and `void setSpectralStateCount(int n) noexcept` (`:777`) — the write path the edit reuses. **The overwrite fact:** `cloud_.setSpectralTarget(morph_.getOutputRatios(), morph_.getOutputAmplitudes(), …)` at `:989`, per control chunk. **Modified this phase (D1, FR-033a):** both methods gate on `isConfigurable()` (`:770-776`, `:908`), rejecting the push and incrementing `rejectedConfigCalls_` while the voice `hasSounded_` and is not `isFinished()`. That gate is **relaxed for these two calls only** — every other `isConfigurable()`-gated caller is unchanged. |
| `SeraphisEngine` | `dsp/include/krate/dsp/systems/seraphis_engine.h` (L3) | `[[nodiscard]] const SeraphisVoice& getVoice(std::size_t index) const noexcept` (`:955`); `getActiveVoiceCount()` (`:927`); `getRenderingVoiceCount()` (`:939`); `float getVoiceLevel(std::size_t) const noexcept` (`:949`); `VoiceState getVoiceState(std::size_t) const noexcept` (`:952`); `[[nodiscard]] std::uint64_t getVoiceAllocationSerial(std::size_t index) const noexcept` (`:975`, documented *"Strictly increasing across note events … 0 means 'never allocated'"*) — the focus-voice tie-break of C-2. Constants `kMaxVoices = 16` (`:211`), `kMaxBlockSamples = 2048` (`:215`), `kControlChunkSamples = 64` (`:213`). **`getVoice` is `const`**, which is why C-4's fan-outs are additive rather than reachable today. |
| `SeraphisMacroMatrix` | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` (L3) | C-10's subject, and the shape C-10 copies. `enum class SeraphisMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Aether }` (`:52`); `enum class SeraphisMacroTarget` (`:55-89`) with the **Aether-owned block last, immediately before `Count`** and the banner *"each MUST have a 1:1 field in SeraphisAetherTargets"* (`:79`); `struct SeraphisAetherTargets` whose *"fields are declared in enum order, so this is a pure offset"* (`:442-445`, POD at `:110-119`); `void apply(SeraphisEngine& engine) const noexcept` (`:623`); `[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept` (`:667`), documented *"Pure function of the knobs and the table; writes nothing"* (`:662`). The four compile-time guards C-10 must extend: `everyRowOwnerIsValid` (`:457`, whose owner ladder at `:466-474` is the exact biconditional a fourth owner has to join), `everyAetherRowHasAPodField` (`:480`), `everyTargetInFr061to065IsPresent` (asserted `:822`), `everyRowSharesOneBasePerTarget` (asserted `:824`). The composition seam the deep params already use: `void setTargetBase(SeraphisMacroTarget, float) noexcept` (`:708`), `resetTargetBases()` (`:718`), `getTargetBase()` (`:724`). **The identity that keeps SC-001 exact:** *"at neutral every term is exactly 0 - applyModCurve(c, 0) == 0 for all three"* (`:778`), with `neutralFor()` returning 0.5 for Gravity and 0 for the rest (`:548-550`). There is **no** enable/bypass/depth control on this class — `setMacro` (`:554`), `setMacros` (`:599`) and `setTargetBase` (`:708`) are the whole mutator surface, which is why SC-017's negative control is a macro-at-neutral arm and not a suppression seam. |
| Effects param push | `plugins/seraphis/src/processor/processor.cpp` | Where C-10's composed values land. The Aether precedent runs **every slice**, unguarded: `macros_.apply(*engine_); applyAetherTargets(*reverb_, macros_.computeAetherTargets());` (`:1858-1859`), under the comment *"Applied EVERY SLICE even at Phase 8's neutral macro defaults (FR-034): computeAetherTargets() is what pushes the reverb's eight controls, and 'inert' describes the macro VALUES, not the push"* (`:1855-1857`). The effects push is the opposite shape — change-guarded against the **raw deep value** (`lastPushedFxWanderRate_` at `:1830-1833`, `lastPushedFxFreezeReady_` at `:1820`, `effectsPushes_` at `:1821`) — which is precisely why FR-038 has to re-point two of those guards at the *composed* value. |
| `SpectralState` + validator | `dsp/include/krate/dsp/processors/spectral_state.h` (L2) | The mutators' subject. `struct SpectralState` (`:44`) with `ratios`/`amplitudes` `std::array<float, kStatePartials>` (`:57-58`), `name` (`:59`), `tiltDbPerOct` (`:60`), `inharmonicity` (`:61`), `int numPartials` (`:62`); `kStatePartials = 64` (`:48`), `kMinStateRatio = 0.5f` / `kMaxStateRatio = 128.0f` (`:51-52`), `kMinStateTiltDbPerOct = -12.0f` / `kMaxStateTiltDbPerOct = 12.0f` (`:53-54`), `kMaxStateInharmonicity = 0.1f` (`:55`). `[[nodiscard]] inline bool isValidSpectralState(const SpectralState& s) noexcept` (`:82`) — the contract SC-012 asserts, requiring **strictly increasing** ratios (`:97-99`), amplitudes in `[0,1]` (`:106-108`), a NUL terminator and printable ASCII (`:111-127`). `inline void normalizeSpectralState(SpectralState& s) noexcept` (`:155`) — L2 normalisation, guarded on `sumSquares > 0` (`:169`). Serialization `serializeSpectralState` (`:238`) / `deserializeSpectralState` (`:274`), `kSpectralStateBytes == 541` (`:186`). `makeFactoryState(SpectralStateId)` (`:373`), `kSpectralStateCount = 5` (`:315`). **The header states the extension contract: `:13-15`.** |
| `SpectralMorphEngine` | `dsp/include/krate/dsp/systems/spectral_morph_engine.h` (L3) | Two facts are load-bearing: `void setState(int slot, const SpectralState& s) noexcept` **rejects wholesale** on `!isValidSpectralState(s)` (`:292-296`), and stores the slot *sanitized* so *"`tiltDbPerOct`, `inharmonicity` and `name` are structurally incapable of reaching the audio path"* (`:285-289`). There is **no per-slot getter** — Phase 9 recorded this (`specs/seraphis-phase9-parameters/spec.md:846-851`), which is why the processor's own `spectralSlots_` copy is the single source of truth. **`setState` already arms a click-free crossfade if the slot contributes** — `armStateFade()` at `:311`-`:312`, gated on `slotContributes(slot)` (`:558`) — which is the exact machinery D1 relies on and reuses rather than duplicating. **Modified this phase (D1):** the class-level *"CONFIGURATION-TIME CALLS"* comment (`:198-206`) is corrected to name only `prepare()`, `reset()` and `setSeed()` — `setState`/`setStateCount` are removed from that list, matching what Phase 3's own FR-042/FR-044 already prove (continuity-safe, not among FR-044's two named exemptions). Comment-only; `setState`'s body is unchanged. |
| `loadMorphParamsToController` | `plugins/seraphis/src/parameters/morph_params.h` | **Modified this phase (Q1, C-11).** Today it discards the four 541-byte `SpectralState` payloads into a scratch buffer, documented *"The controller has nowhere to put a SpectralState"* (`:521-532`). Phase 11 stops discarding them: it re-seeds `Controller::slotMirror_` (C-11) from them instead. This is the **only** `morph_params.h` change this phase makes. |
| Spectral-slot staging ring | `plugins/seraphis/src/processor/processor.{h,cpp}` | The transport, reused unchanged. `std::array<SpectralState, 4> spectralSlots_` (`processor.h:858`), `std::array<std::array<SpectralState, 4>, 3> spectralSlotsStaging_` (`:861`, *"THREE staging buffers, not one"*), `std::atomic<int> spectralSlotsHandoff_{-1}` (`:862`), `spectralSlotsConsuming_{-1}` (`:863`), `int stagingWriteCursor_` (`:864`, *"message-thread-only"*), `bool spectralStatesPending_` (`:865`), `std::uint16_t spectralRetryMask_` (`:870`). Publish at `processor.cpp:1416`; consume at `:2823-2831`. The factory-derivation the edit must displace is `spectralSlotsStaging_[w][s] = factoryStates_[clampFactoryIndex(stateId)]` (`:1383-1385`), guarded on change by `std::array<int,4> lastPushedSlotStateId_{-1,-1,-1,-1}` (`processor.h:886`). Test seam `const SpectralState& spectralSlotForTest(int slot) const noexcept` (`:343`). |
| DataExchange producer pattern | `plugins/membrum/src/processor/processor.cpp` | Verbatim shape to copy. `tresult PLUGIN_API Processor::connect(IConnectionPoint* other)` builds `DataExchangeHandler::Config` with `config.blockSize = sizeof(MetersBlock)`, `config.numBlocks = 4`, `config.alignment = 32`, `config.userContextID = …` and calls `onConnect(other, getHostContext())` (`:1136-1153`); `disconnect` calls `onDisconnect` + `reset()` (`:1155-1163`); `onActivate(setup)` / `onDeactivate()` on `setActive` (`:1111-1126`). Publish: `auto block = dataExchangeHandler_->getCurrentOrNewBlock(); if (block.blockID != Vst::InvalidDataExchangeBlockID && block.data != nullptr && block.size >= sizeof(MetersBlock)) { … memcpy …; sendCurrentBlock(); }` (`:849-872`), **once per `process()` call**. Member is `std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_` behind a forward declaration (`processor.h:33-34`, `:154`). |
| DataExchange payload pattern | `plugins/membrum/src/processor/meters_block.h` | The payload shape: a POD `struct` + `static_assert(sizeof(MetersBlock) == 44, …)` (`:33-34`) + `inline constexpr std::uint32_t kMetersDataExchangeUserContextId = 0x4D425452u;` (`:38`). Producer/consumer banner at `:8-11`. |
| DataExchange consumer pattern | `plugins/membrum/src/controller/controller.{h,cpp}` | `class Controller : … public Steinberg::Vst::IDataExchangeReceiver` (`controller.h:44`) with `DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)` (`:146`) and the member `Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this}` (`:365`) — *"Without this member the receiver … "*. Entry points `queueOpened` (setting `dispatchOnBackgroundThread = false`), `queueClosed`, `onDataExchangeBlocksReceived` (`controller.cpp:1696-1740`), which memcpy the **most recent** block into a cached POD. |
| Custom-view instantiation | `plugins/membrum/` | `class Controller : … public VSTGUI::VST3EditorDelegate` (`controller.h:43`) + `VSTGUI::CView* Controller::createCustomView(…)` (`controller.cpp:1037`, dispatching on `std::strcmp(name, "PadGridView") == 0` at `:1054`), bound from the uidesc as `<view class="CView" custom-view-name="PadGridView" …/>` (`plugins/membrum/resources/editor.uidesc:352`). Raw view pointers are cached and **zeroed in `willClose()`** (`controller.h:191-199`). `PadGridView` itself owns a **30 Hz `CVSTGUITimer`** cancelled in `removed()` (`plugins/membrum/src/ui/pad_grid_view.h:30-37`). |
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` (`Krate::Plugins`) | The macro ring's base. `class ArcKnob : public VSTGUI::CKnobBase` (`:49`), ctor `ArcKnob(const CRect&, IControlListener*, int32_t tag)` (`:53-55`), `CLASS_METHODS(ArcKnob, CKnobBase)` (`:214`). Registered through `struct ArcKnobCreator : VSTGUI::ViewCreatorAdapter` whose ctor calls `VSTGUI::UIViewFactory::registerViewCreator(*this)` (`:555-558`), `getViewName() -> "ArcKnob"` (`:560`), `getBaseViewName() -> UIViewCreator::kCControl` (`:562-564`), instantiated as `inline ArcKnobCreator gArcKnobCreator;` with the instruction *"Include this header from each plugin's `entry.cpp` to register the view type"* (`:714-716`). |
| `PresetBrowserView` | `plugins/shared/src/ui/preset_browser_view.h` (`Krate::Plugins`) | `class PresetBrowserView : public VSTGUI::CViewContainer, public IControlListener, public IKeyboardHook, public ITextEditListener` (`:53-56`); `PresetBrowserView(const CRect& size, PresetManager* presetManager, std::vector<std::string> tabLabels)` (`:58-60`); `open()` / `open(const std::string&)` / `openWithSaveDialog(…)` / `close()` / `isOpen()` (`:64-68`). |
| Seraphis controller | `plugins/seraphis/src/controller/controller.{h,cpp}` | Today: `IPlugView* PLUGIN_API Controller::createView(FIDString name)` returning `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (`controller.cpp:151-156`); `presetManager_` already constructed from `makeSeraphisPresetConfig()` (`:57-58`). **It already derives from `VSTGUI::VST3EditorDelegate`** — `class Controller : public Steinberg::Vst::EditControllerEx1, public VSTGUI::VST3EditorDelegate` (`controller.h:23-24`) — but overrides **neither** `createCustomView` **nor** `verifyView` **nor** `createSubController`, which the header's own banner states: *"NO createCustomView / verifyView overrides (FR-018, FR-056 - there are no custom views until Phase 11)"* (`:10-11`). **It is not an `IDataExchangeReceiver`** — that base, the overrides and the members are the Phase 11 additions; the `VST3EditorDelegate` base is **not**. |
| Seraphis processor | `plugins/seraphis/src/processor/processor.{h,cpp}` | `tresult PLUGIN_API Processor::process(Vst::ProcessData& data)` (`processor.cpp:957`) with the slice loop calling `renderSlice(outL + cursor, outR + cursor, n)` at `:1311` and `controlPhase_ += n` at `:1312`; `setupProcessing` (`:473`); `setActive` (`:801`). **No `connect`/`disconnect`/`notify` override exists** (grep returns nothing) — all three are Phase 11 additions. `std::unique_ptr<SeraphisEngine> engine_` (`processor.h:674`), never by value (`:671-672`: *"sizeof(SeraphisEngine) is 771 968 B"*). |
| uidesc + surface tests | `plugins/seraphis/tests/unit/parameter_surface_test.cpp` | SC-015's binding contract, which Phase 11 grows. `static_assert(kSurfaceRowCount == 107, …)` (`:233-234`); `CHECK(controller.getParameterCount() == 107)` (`:508`); the two-way tag/ID gate (`:700-709`); `extractBoundViews(xml)` with `CHECK(bound.size() == 8u)` (`:713-714`) and the per-view class check `CHECK(view.viewClass == std::string(expectedViewClass(row->kind)))` (`:731`) — **the assertion that a Phase 11 view class must satisfy or be allowlisted.** |
| uidesc reachability helper | `tests/test_helpers/uidesc_reachability.h` | `extractControlTagMap(const std::string& xml)` (`:43`) and `unreachableParams(xml, ids, allowlist)`. Its own banner states the limit Phase 11 must respect: params driven by a custom view *"have no control-tag and will be reported as 'unreachable' … Each per-plugin test must pass those params in via the allowlist"* (`:15-23`). C-3 sets that allowlist to **empty**. |
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h` | `Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", uidescPath, cycles)`; it builds *"the entire view tree from the .uidesc … and fires the controller's `verifyView()`/`didOpen()` delegate hooks"* and then `willClose()` (`:9-16`) — i.e. it exercises `createCustomView` and the raw-pointer teardown for free. Seraphis's enrollment is `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp:235-262` (10 cycles, `REQUIRE(controller.getParameterCount() == 107)` before and after). |
| CPU gate | `plugins/seraphis/tests/integration/param_perf_test.cpp` | `kFullPolyCeilingNs = kBlockBudgetNs * 0.25` (`:392`), `kRegressionFactor = 1.15` (`:395`), `kBaselineFullPolyNs = 2318840.0` (`:472`), `static_assert(kNonDefaultTable.size() == 107, "SC-009 / SC-014: the table is EXHAUSTIVE over the 107-parameter surface")` (`:1101` — `:1097` is the closing brace of `countRows`). Phase 10's measured worst-of-seven at the gate: **2 380 980 ns = 22.32 %** (`specs/seraphis-phase10-effects/compliance.md`, SC-014). |
| Test target | `plugins/seraphis/tests/CMakeLists.txt` | The source list is **ENUMERATED, not globbed** (`:16-18`, `:29-32`) and it compiles the plugin `.cpp`s a second time (`:36-38`). Every Phase 11 `.cpp` — plugin *and* test — must be added by hand or it silently drops. |
| Plugin target | `plugins/seraphis/CMakeLists.txt` | `smtg_add_vst3plugin(${PLUGIN_NAME} …)` source list (`:17-46`); `src/ui/` is not in it today. `plugins/seraphis/CLAUDE.md` records `ui/` as *"empty until Phase 11 (`.gitkeep` only)"*. |

---

## New components

### ODR sweep — run this session

Command form: `grep -rn "class <Name>\b|struct <Name>\b" dsp/ plugins/ tools/`. Free-function names were
swept as `grep -rn "\b<name>\s*(" dsp/ plugins/`.

| Candidate name | Sweep result | Disposition |
|---|---|---|
| `CloudView` | **0 matches** | **CLAIMED** — `Seraphis::UI::CloudView`, `plugins/seraphis/src/ui/cloud_view.{h,cpp}`. |
| `MacroRingKnob` | **0 matches** | **CLAIMED** — `Seraphis::UI::MacroRingKnob`, `plugins/seraphis/src/ui/macro_ring_knob.h`. |
| `DrawerContainer` | **0 matches** | **CLAIMED** — `Seraphis::UI::DrawerContainer`, `plugins/seraphis/src/ui/drawer_container.{h,cpp}`. |
| `CloudFrame` | **0 matches** | **CLAIMED** — `Seraphis::CloudFrame`, `plugins/seraphis/src/processor/cloud_frame.h` (POD payload, beside the processor exactly as Membrum's `meters_block.h` sits beside its own). |
| `setPartial` (free fn) | **0 matches** (`setPartialPosition` / `setPartialMask` exist but are distinct identifiers) | **CLAIMED** — `Krate::DSP::setPartial`, Layer 2, `spectral_state.h`. |
| `blendStates` (free fn) | **0 matches** | **CLAIMED** — `Krate::DSP::blendStates`, Layer 2, `spectral_state.h`. |
| `tiltState` (free fn) | **0 matches** | **CLAIMED** — `Krate::DSP::tiltState`, Layer 2, `spectral_state.h`. |
| `SeraphisEffectsTargets` | **0 matches** | **CLAIMED** — `Krate::DSP::SeraphisEffectsTargets`, Layer 3, `seraphis_macro_matrix.h`, beside `SeraphisAetherTargets` (`:110`). C-10. |
| `computeEffectsTargets` (member fn) | **0 matches** | **CLAIMED** — `SeraphisMacroMatrix::computeEffectsTargets`, beside `computeAetherTargets` (`:667`). C-10. |
| `SeraphisEditSubController` | **0 matches** | **CLAIMED** — `Seraphis::UI::SeraphisEditSubController`, `plugins/seraphis/src/ui/edit_sub_controller.{h,cpp}`. A `VSTGUI::DelegationController` subclass, **not** a `CView` — it does not count against the roadmap's three-custom-view budget (C-7, FR-026). |
| `PartialSnapshot` | **1 match** — `dsp/tests/unit/systems/spectral_morph_render_test.cpp:918` (`struct`, test-local) | **REJECTED as a payload name.** Even a test-TU-local collision is a name this spec will not reuse. `CloudFrame` is used instead. |
| `CloudSnapshot`, `SeraphisCloudFrame`, `CloudFrameBlock`, `CloudCanvas`, `CloudConstellationView`, `SeraphisCloudView`, `PartialConstellationView`, `SpectralEditorView`, `SpectralStateEditor`, `RingKnob`, `SeraphisRingKnob`, `MacroRing`, `SeraphisDrawer`, `SeraphisDrawerContainer`, `DrawerView`, `CloudViewCreator`, `CloudViewController`, `PartialEditState`, `PartialPoint`, `ObserveEditToggle`, `SeraphisUiBlock`, `CloudUiBlock`, `CloudBlock` | all **0 matches** | swept and **not claimed** — recorded so the conclusion is visibly tested rather than assumed. |

Near-name components that exist and are **not** reused, recorded so the sweep is honest:
`Krate::Plugins::ArcKnob` (`plugins/shared/src/ui/arc_knob.h:49`) **is** reused, as `MacroRingKnob`'s base;
`Krate::Plugins::XYMorphPad` (`xy_morph_pad.h:52`) is a two-axis *control*, not a scatter view, and drives
two `ParamID`s — it cannot express 64 points and is not the cloud view's base;
`Membrum::UI::PadGridView` is the *pattern* copied for the timer/glow/teardown discipline, never a base
class (different plugin namespace, different data).

### Additive `dsp/` surface — three free functions, two fan-out groups, one macro-target owner, one gate relaxation, nothing else

| Addition | Layer / header | Nature |
|---|---|---|
| `void setPartial(SpectralState& s, std::size_t index, float ratio, float amplitude) noexcept` | L2 `processors/spectral_state.h` | Free function beside `normalizeSpectralState` (`:155`). C-6. |
| `[[nodiscard]] SpectralState blendStates(const SpectralState& a, const SpectralState& b, float t) noexcept` | L2 `processors/spectral_state.h` | Free function; returns by value (`SpectralState` is trivially copyable, `:65`). C-6. |
| `void tiltState(SpectralState& s, float dbPerOct) noexcept` | L2 `processors/spectral_state.h` | Free function. C-6. |
| `void setPartialPosition(std::size_t index, float position) noexcept` / `void setPartialMask(std::size_t index, bool active) noexcept` / `void clearPartialMask() noexcept` | L3 `systems/seraphis_voice.h` | Three one-line pass-throughs to `cloud_`, in the shape of the existing `setRichness` … block (`:642-647`). |
| `void setPartialPositionAllVoices(std::size_t index, float position) noexcept` / `void setPartialMaskAllVoices(std::size_t index, bool active) noexcept` / `void clearPartialMaskAllVoices() noexcept` | L3 `systems/seraphis_engine.h` | Fan-outs over `voices_[0 … kMaxVoices)`. Needed because `getVoice()` is `const` (`:955`) and the macro matrix's non-const access is a `friend` (`:997`) the plugin cannot use. |
| `SeraphisMacroTargetOwner::Effects`; `SeraphisMacroTarget::FxDelaySend` and `::FxWanderDepth` **appended after the Aether block, immediately before `Count`**; `struct SeraphisEffectsTargets { float delaySend = 0.0f; float wanderDepth = 0.0f; };`; `[[nodiscard]] SeraphisEffectsTargets computeEffectsTargets() const noexcept`; two `kRows` entries; `effectsFieldIndex()` + `everyEffectsRowHasAPodField()` and the extended `everyRowOwnerIsValid()` ladder | L3 `systems/seraphis_macro_matrix.h` | C-10. Additive by construction: appending to the enum before `Count` leaves every existing target's index unchanged, so `aetherFieldIndex`'s `[kFirstAetherTarget, kFirstAetherTarget + kNumAetherTargets)` window (`:446-452`) is untouched and `SeraphisAetherTargets` keeps its offsets. `apply(SeraphisEngine&)` (`:623`) gains **no line** — Effects-owned targets are read by the plugin, like Aether's. |
| `SeraphisVoice::setSpectralState` / `setSpectralStateCount` (`:770-780`, `:908`) **stop routing through `isConfigurable()`** and always forward to `morph_.setState` / `setStateCount`. `spectral_morph_engine.h:198-206`'s class comment is corrected in the same change (comment-only; `setState`'s body is unchanged) to name only `prepare()`, `reset()`, `setSeed()`. | L3 `systems/seraphis_voice.h` (behaviour) + `systems/spectral_morph_engine.h` (comment only) | **D1.** The one **non-additive** entry in this table — an existing function's early-return condition is removed for these two calls, not a new `case`/row appended. Admitted by the phase-owner ruling (D1, *Non-goals* SCOPE AMENDMENT) precisely because the primitive it forwards to (`SpectralMorphEngine::setState`) was already continuity-safe per Phase 3's own FR-042/FR-044; the gate being relaxed was `SeraphisVoice`'s own extra restriction, not Phase 3's. Every other `isConfigurable()`-gated caller is unchanged. FR-033a. |

All six DSP files stay header-only and layer-legal: `spectral_state.h` includes only
`<krate/dsp/core/db_utils.h>` + stdlib (`:26-35`) and the new functions add **no include**;
`seraphis_macro_matrix.h`'s additions name **no Layer 4 type**, which is the constraint its own banner
records for the Aether POD (*"FR-056 forbids naming a Layer 4 type at all"*, `:105`) and which the
`SeraphisEffectsTargets` POD satisfies for the same reason — it carries plain floats, and the ranges
belong to the plugin-side setter that consumes them. `seraphis_voice.h`'s gate relaxation and
`spectral_morph_engine.h`'s comment correction add **no include** and name **no new type**.

### Plugin-local additions (`namespace Seraphis`)

| Addition | File | Nature |
|---|---|---|
| `struct CloudFrame` + `kCloudFrameUserContextId` | `src/processor/cloud_frame.h` (new) | POD payload + `static_assert` on `sizeof` (C-2). |
| `class UI::CloudView` | `src/ui/cloud_view.{h,cpp}` (new) | `VSTGUI::CView` subclass; Observe/Edit; 30 Hz timer. |
| `class UI::MacroRingKnob` | `src/ui/macro_ring_knob.h` (new) | `Krate::Plugins::ArcKnob` subclass, five instances. |
| `class UI::DrawerContainer` | `src/ui/drawer_container.{h,cpp}` (new) | `VSTGUI::CViewContainer` subclass; tab strip + slide animation. |
| `struct UI::EditMessage` + message-ID constants | `src/ui/edit_message.h` (new) | The C-5 controller → processor wire format. **Eight kinds, 0–7** (kind 7 = `BlendBegin`, Q2). |
| `class UI::SeraphisEditSubController` | `src/ui/edit_sub_controller.{h,cpp}` (new) | `VSTGUI::DelegationController` subclass. **Not a `CView`** — it owns `valueChanged` for every tag-less control in the cloud/drawer sub-tree (C-7). |
| `DataExchangeHandler` member, `connect`/`disconnect`/`notify`/`setActive` overrides, `publishCloudFrame()`, `applyEditMessage()`, `partialOverrides_` table, `computeEffectsTargets()` composition in the effects push, `[partials]` block (de)serialization in `getState`/`setState`, `BlendBegin` snapshot scratch | `src/processor/processor.{h,cpp}` (extended) | C-2, C-4, C-5, C-10, FR-030, FR-034a, FR-038. Q2, Q3. |
| `IDataExchangeReceiver` base (**the `VST3EditorDelegate` base already exists**, `controller.h:23-24`), `createCustomView`, `createSubController`, `willClose`, `cachedCloudFrame_`, `dataExchangeReceiver_`, `editedSlots_`, `subControllerInstances_`, `slotMirror_`, `editorOpenCount_` | `src/controller/controller.{h,cpp}` (extended) | C-2, C-5, C-7, **C-11** (Q1), FR-046 (Q1), FR-047 (Q7). The header's *"NO createCustomView / verifyView overrides"* banner (`controller.h:10-11`) must be rewritten in the same change, for the FR-052 reason. |
| Replaced `editor.uidesc` | `resources/editor.uidesc` | C-1, C-3. |
| `#include <ui/arc_knob.h>` in `entry.cpp` | `src/entry.cpp` (extended) | Registers `ArcKnobCreator` (`arc_knob.h:714-716`). C-7. |

---

## Conventions decided in this spec

### C-1. The layout, decided once

Roadmap lines 487–503 are the sketch; this is the binding reading of it. **The window is fixed at
1000 × 700 px** (RQ-3), which is what turns the roadmap's "~30 px" and "~40 %"
into the exact rectangles FR-023 states.

```
+----------------------------------------------------------------------+
| header  H = 32 px : "SERAPHIS" | [Preset v] | ......... | Seed | Poly | Limit |
+----------------------------------------------------------------------+
|  (DREAM)                CLOUD VIEW  (fills)               (BLOOM)    |
|                     x = pan, y = log2 f, size = amp                  |
|  (GRAVITY)                                              (DISSOLVE)   |
|                          (ENTROPY)                     [Obs | Edit]  |
+----------------------------------------------------------------------+
|  drawer handle strip, H = 30 px collapsed:                           |
|  [Cloud][Morph][Body][Atmos][Aether][FX][Life/Env]                   |
+----------------------------------------------------------------------+
```

- The cloud view is a **single view occupying the whole area between header and drawer strip**, and it is
  added to the template **first**, so the five rings and the Obs/Edit toggle draw over it (VSTGUI z-order
  is child order). It is never a panel among panels.
- The five rings sit at fixed anchors: Dream top-left, Bloom top-right, Gravity bottom-left, Dissolve
  bottom-right, Entropy bottom-centre — the roadmap's own arrangement (roadmap lines 491–498).
- **The drawer never covers the header and never unmounts the cloud view** (roadmap lines 509–511). Open, it
  occupies 40 % of the window height and the cloud view is *overlapped*, not removed and not resized: it
  keeps drawing, and keeps consuming frames. FR-024 states this as a testable property.
- **The exact rectangles**, in the fixed 1000 × 700 window, are the numbers FR-023 and SC-020 assert:

  | Element | Rect `(left, top, right, bottom)` | Derivation |
  |---|---|---|
  | Window | `(0, 0, 1000, 700)` | RQ-3 |
  | Header | `(0, 0, 1000, 32)` | C-1 sketch, `H = 32` |
  | Cloud view | `(0, 32, 1000, 670)` | header bottom → collapsed-strip top; **never changes**, open or closed |
  | Drawer, collapsed | `(0, 670, 1000, 700)` | 30 px tab strip (roadmap line 509) |
  | Drawer, open | `(0, 420, 1000, 700)` | 280 px = 40 % of 700 (roadmap line 509) |

### C-2. The cloud-frame data path — one payload, one direction, one publish per `process()`

Piggyback, exactly as the roadmap requires (roadmap lines 481–482) and as `plugins/membrum/CLAUDE.md`
records (*"don't add new queues/IMessage loops"*).

```cpp
namespace Seraphis {
struct CloudFrame {                       // POD, little-endian, memcpy'd
    std::uint32_t sequence          = 0;  // monotonically increasing; wrap is benign
    std::uint16_t activeVoices      = 0;  // SeraphisEngine::getActiveVoiceCount()  (:927)
    std::uint8_t  focusVoice        = 0;  // C-2 focus rule below
    std::uint8_t  partialCount      = 0;  // 0 .. HarmonicCloud::kMaxPartials (64)
    float         fundamentalHz     = 0.0f; // focus voice's UNDETUNED f0 (no drift multiplier —
                                             // Q6's fixed-reference authoring depends on this)
    float         voiceLevel        = 0.0f; // SeraphisEngine::getVoiceLevel(focus)   (:949)
    float         morphTravelPosition = 0.0f; // Q4: SpectralMorphEngine's current journey position
    float         frequencyHz[64]   = {}; // drift-inclusive, C-2 clause 3
    float         amplitude  [64]   = {}; // display amplitude,  C-2 clause 3
    float         position   [64]   = {}; // [-1, +1]
    std::uint64_t maskBits          = 0;  // Q5: bit i set <=> partial i is masked
    std::uint64_t overriddenBits    = 0;  // Q5: bit i set <=> partial i has an active
                                           // partialOverrides_ entry (pan and/or mask)
};
// 804 data bytes + 4 bytes compiler-inserted padding before `maskBits`, which forces the
// struct's alignment to 8 (the two std::uint64_t members) — computed, not assumed:
// 8 (header) + 12 (three floats) + 768 (three float[64] arrays) = 788, rounded up to the next
// 8-byte boundary (792) for `maskBits`, + 16 (two uint64_t) = 808.
static_assert(sizeof(CloudFrame) == 808);
inline constexpr std::uint32_t kCloudFrameUserContextId = 0x53434C44u; // 'SCLD'
}
```

1. **Handler config** mirrors Membrum's (`plugins/membrum/src/processor/processor.cpp:1141-1147`):
   `blockSize = sizeof(CloudFrame)`, `numBlocks = 4`, `alignment = 32`, `userContextID =
   kCloudFrameUserContextId`. Created in `connect()`, destroyed in `disconnect()`, `onActivate` /
   `onDeactivate` driven from `setActive` (`:1111-1126`). FR-011 requires all four; SC-006 arm (i) is the
   criterion.

   **Inherited-criterion narrowing — Phase 8's SC-026 clause 2 (D-9 row 9h).** That criterion reads
   *"`setActive(true)` performs **exactly 0** allocations"*
   (`specs/seraphis-phase8-plugin-scaffold/spec.md:1581-1590`), and after this clause it is **false as
   stated**: with a handler live, `onActivate → Impl::openQueue` in the SDK's fallback path (a host with
   no `IDataExchangeHandler`) performs `make_unique` + `Timer::create` + `aligned_alloc × numBlocks` +
   `allocateMessage` (`extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:76-105`). The
   criterion is **narrowed, not waived**, to: *no allocation on any **audio-thread-reachable** path.* The
   **host-thread queue open is out of scope** — `setActive` runs on the host thread with the audio thread
   stopped, which is the same window the deactivate branch's own banner already relies on
   (`processor.cpp:799-801`). Phase 8's test keeps its exact `== 0` form unchanged, because it measures a
   **disconnected** instance — i.e. exactly the audio-thread-reachable configuration, where
   `dataExchangeHandler_` is null and the queue-open branch is not taken. Nothing about this narrowing
   relaxes SC-011: no audio-thread path in this phase allocates, and SC-011 is the arm that proves it.
2. **Cadence: once per `process()` call, at the end, never per slice.** `renderSlice` runs once per MIDI
   slice — the loop subdivides on every event, on the 2048 cap and on the 64-sample grid
   (`plugins/seraphis/src/processor/processor.cpp:1311`) — so a per-slice publish would issue up to 8× the
   frames for one block and burn the 4-block queue on a single `process()`. This is the same divisor
   correction Phase 10 made for its stage counter (`specs/seraphis-phase10-effects/spec.md` → plan D-8).
3. **What each field carries, and why.**
   `frequencyHz[i] = getPartialFrequencyHz(i) * getPartialDriftDetune(i)` — the accessor at `:955` is
   documented *"Undetuned synthesized frequency"*, so publishing it alone would draw a **static**
   constellation for an instrument whose identity is drift (KDD-1, roadmap lines 71–72); `:991` supplies
   the multiplier. **Because the published frequency is drift-inclusive by definition, every criterion
   that measures a frequency out of the frame must state its drift control** — SC-013 and SC-017 do.
   `amplitude[i] = getPartialCurrentAmplitude(i) * getPartialAntiAliasGain(i)` — a partial the anti-alias
   gain has taken to zero is inaudible and must not be drawn bright.
   `position[i] = getPartialPosition(i)` (`:986`), already `[-1, +1]`.
   Entries at `i >= partialCount` are **zero-filled**, never stale.
   `fundamentalHz` is deliberately the **undetuned** f0 (no drift multiplier applied) — unlike
   `frequencyHz[]`, which is drift-inclusive by clause 3's own rule above. This distinction is load-bearing
   for Q6: the Edit-mode reference pitch (C-4, C-11) is built on `fundamentalHz` precisely because it
   excludes drift, so a ratio drag can never bake momentary Brownian detune into a stored ratio.
   **Its source MUST be the focus voice's tracked note** — `dispatchEvent` bookkeeping, or a per-voice
   note read-back if `SeraphisEngine` turns out to expose one, verified at implementation time — and MUST
   NOT be `HarmonicCloud::getPartialFrequencyHz(0)` / `CloudFrame::frequencyHz[0]`: both are drift-inclusive
   by clause 3's own rule, and using either here would silently defeat Q6/SC-024's drift-exclusion
   guarantee the instant partial 0 picks up any Brownian detune.
   `morphTravelPosition` mirrors `SpectralMorphEngine`'s current journey position, one read per publish
   (Q4); it is display-only and gates nothing — the view uses it to mark the selected slot "not currently
   sounding" when it does not contribute, and no parameter is written to make that true (Q4 rejects the
   alternative of a mode toggle that parks the journey).
   `maskBits`/`overriddenBits` mirror the processor's `partialOverrides_` table bit-for-bit (Q5): `maskBits`
   tells the view which partials are masked (so it can draw them as hollow rings rather than at zero
   radius, per *Edge cases*) and lets a click compute the toggled value instead of guessing it;
   `overriddenBits` marks which entries carry an active user override at all (pan and/or mask), which is
   `partialOverrides_`'s own per-entry flag (FR-030) read back out.
4. **Focus voice** (the frame carries one voice, not sixteen). The rule, in order: (a) among slots with
   `getVoiceState(v) != VoiceState::Idle` (`:952`), the one with the greatest
   `getVoiceAllocationSerial(v)` (`:975`); (b) if none is non-idle, the previous focus slot is retained
   while `getVoiceLevel(previous) > kCloudFrameSilenceLevel` so a release still animates; (c) otherwise
   slot 0. Ties are impossible — the serial is documented *"Strictly increasing across note events"*
   (`:966-974`). One voice, not sixteen, because sixteen would be a 12 KB payload and an unreadable
   1024-point scatter; the roadmap's sketch shows one constellation.
5. **Direction is one-way.** Nothing about editing travels on this queue. Edits go on C-5's channel.
6. **The producer is gated, and the gate is `std::atomic<bool>` (D-6).** It runs only while
   `cloudFrameEnabled_` is true, set by C-5's editor-open message. **The type is not a detail:** the
   message thread writes it inside `notify()` (C-5 kind 0) and the **audio** thread reads it every
   `process()` call, so a plain `bool` would be a data race — undefined behaviour under the C++ memory
   model regardless of what any particular compiler emits for a byte store. It is
   `std::atomic<bool> cloudFrameEnabled_{false}`, **relaxed on both sides**: it publishes nothing but
   itself, so a `load(std::memory_order_relaxed)` in `process()` and a
   `store(v, std::memory_order_relaxed)` in `notify()` are sufficient and cost the same as the plain
   access on x86-64 and arm64. A late-arriving gate flip costs one frame of animation and nothing else.
   **The sibling atomics, and where they deliberately differ:** `partialOverridesPending_` **does** publish
   other state (the staged pan/mask values the audio thread must see after it observes the flag), so it is
   release on the writer and acquire on the reader — `exchange(false, std::memory_order_acquire)` against
   `store(true, std::memory_order_release)`. The two override bitmasks (`partialMaskBits_`,
   `partialPanOverrideBits_`) and the 64-entry pan staging array (`partialPanStaging_`) are likewise
   `std::atomic` and are published *by* `partialOverridesPending_`'s release store, so they are relaxed.
   Only `std::atomic_flag` is *guaranteed* lock-free by the standard (root `CLAUDE.md`), so this is
   asserted rather than assumed: SC-011's lock-free arm checks `is_lock_free()` on all five at runtime
   (the pan array's element 0 stands for all 64 — one array of one type, so a locking implementation
   would apply to every element).
   With no editor open the processor pays one relaxed atomic load per `process()` call and nothing else —
   which is what makes SC-010's "costs nothing when closed" arm meaningful rather than decorative.
   For SC-001's negative control the gate MUST also be forceable **open** from a test seam
   (`setCloudFrameGateForTest(bool)`), so both arms of that criterion are the same build, the same
   process and the same `Processor` instance with only the gate differing — Phase 10's SC-002 shape
   (`specs/seraphis-phase10-effects/spec.md:1313-1325`).
7. **Two counters, and they count different things.** A publish can be *attempted* and still not land:
   `getCurrentOrNewBlock()` returns `InvalidDataExchangeBlockID` when the host queue is full (Edge cases),
   and at 512/48 kHz the ≈ 94 Hz publish rate deliberately outruns the 30 Hz consume rate (C-8), so
   skipped blocks are **expected steady-state behaviour**, not a fault. The processor therefore exposes
   **two** seams, and no criterion conflates them:
   - `cloudFramePublishAttemptCountForTest()` — incremented **once per `process()` call that reached the
     slice loop**, after that loop, **whenever the clause-6 gate is true — and on that condition alone
     (D-9 row 9e)**, independently of what `getCurrentOrNewBlock()` returns **and independently of whether
     a queue exists at all**. An earlier draft additionally required `dataExchangeHandler_ != nullptr`,
     which makes the seam unobservable in every plugin-side test this phase writes: `ProcessorFixture`
     does `initialize(nullptr) → setupProcessing → setActive(true)`
     (`plugins/seraphis/tests/seraphis_test_fixture.h:177-213`) and **never calls `connect()`**, so the
     handler is permanently null and the counter would read 0 on a correct build — SC-001's non-vacuity
     guard and SC-007's whole equality would be unsatisfiable. The frame is still **produced** (into
     `lastPublishedFrameForTest()`, which is what every criterion here reads); only the handoff to the
     queue is skipped, and that skip is what the second counter records. This is the cadence seam
     (SC-001, SC-007, SC-010, SC-026).
   - `cloudFrameSkippedBlockCountForTest()` — incremented when an attempt found no block. Recorded and
     reported, **never gating**: SC-007 asserts nothing about its value, so a headless environment with no
     host DataExchange support cannot turn a cadence assertion red.

### C-3. Every one of the 107 IDs is bound — 110 bindings, an EMPTY allowlist, and exactly three deliberate duplicates

`unreachableParams`'s allowlist exists for params a custom view edits programmatically
(`tests/test_helpers/uidesc_reachability.h:15-23`). Phase 11 needs **no entries**, because the five macro
rings are `CControl` subclasses carrying `control-tag` like any knob, and every deep parameter lives on a
plain uidesc control inside a drawer tab. The full assignment:

| Surface | IDs | Primary bindings | Home |
|---|---|---|---|
| Header | 0, 1, 2, 3 | 4 | slim header (`MasterGain`, `Polyphony`, `SoftLimit`, `Seed`) |
| Macro rings | 100–104 | 5 | five `MacroRingKnob` instances |
| Cloud tab | 200–210 | 11 | drawer |
| Morph tab | 400–412 | 13 | drawer (incl. the four slot selectors — RQ-1) |
| Life/Env tab | 600–604, 700–704 | 10 | drawer |
| Body tab | 800–812 | 13 | drawer |
| Atmos tab | 1000–1016 | 17 | drawer |
| Aether tab | 1200–1217 | 18 | drawer |
| FX tab | 1400–1443 | 16 | drawer |
| | | **107 primary** | every registered ID, exactly once |

**Plus the freeze cluster — three second bindings, and only three** (RQ-2):

| ID | Name | Primary binding | Second binding |
|---|---|---|---|
| 1008 | `kAtmosFreezeId` | Atmos tab | header freeze cluster |
| 1204 | `kAetherFreezeId` | Aether tab | header freeze cluster |
| 1430 | `kFxSpectralFreezeId` | FX tab | header freeze cluster |

**Total bound views: 110.** The set `{1008, 1204, 1430}` is the **complete, enumerated duplicate-binding
allowlist**; no other ID may appear on more than one view, and SC-002 asserts both halves of that (the
count *and* the allowlist membership), so an accidental duplicate is a red test rather than a rounding
error inside a `≥`. Binding one `ParamID` from two views needs no type change and no second registration
— the frozen-type legend (`plugin_ids.h:184-240`) and FR-004 are untouched — and both views observe the
same `Parameter` object, so they track each other with no extra code.

The `<control-tag>` block is carried over **verbatim** from the Phase 8/9/10 file (`editor.uidesc:21-139`);
Phase 11 adds views, never tags. The seven tab names are the roadmap's own (roadmap line 501).

**The per-view class rule survives.** `parameter_surface_test.cpp:731` asserts each bound view's class
matches its parameter's registered kind. Phase 11 grows that mapping: `R` → `ArcKnob` or `CSlider`,
`L` → `COptionMenu`, `T` → `CCheckBox` or `Krate::Plugins::ToggleButton`, plus the one exception
`MacroRingKnob` for the five `R` macro IDs. The exception is enumerated in the test, not waived.

### C-4. Ratio/amplitude edits go to the STATE; pan/mask edits go to the CLOUD

The load-bearing split, forced by `seraphis_voice.h:989`.

| Edit gesture | Reaches | Why not the other route |
|---|---|---|
| Drag a partial **vertically** (ratio) | `setPartial(slot, i, newRatio, s.amplitudes[i])` on the **selected morph slot's** `SpectralState` → staging ring → `SeraphisVoice::setSpectralState` (`:770`). `newRatio` is the inverse of the Edit-mode y-axis, which is drawn from the C-11 mirror's `ratios[i] * referenceHz` — **never** from `CloudFrame::frequencyHz[i]`, which is drift-inclusive. `referenceHz` is `fundamentalHz` (undetuned, C-2 clause 3) when a voice sounds, or the fixed **C4 = 261.63 Hz** reference when `activeVoices == 0` (Q6) — so the inverse map excludes drift in both cases and a drag can never bake momentary Brownian detune into the stored ratio, and authoring works identically with no note held. | A direct `HarmonicCloud::setSpectralTarget` write is overwritten by the morph engine within one 64-sample control chunk (`seraphis_voice.h:989`; the cadence is a documented contract at `harmonic_cloud.h:734-753`). |
| **Modifier-drag** a partial vertically (amplitude) | `setPartial(slot, i, s.ratios[i], newAmp)` — the same mutator, same route | `setPartial`'s signature is `(index, ratio, amplitude)` (roadmap line 525): a UI that never authored the third argument would leave one of the three dimensions the inherited mutator exists for unreachable from the only surface the roadmap gives it (roadmap lines 514–515). Both arms are one mutator call carrying the *unchanged* value in the other slot, so neither gesture can perturb the axis it is not driving. |
| Drag a partial **horizontally** (pan) | `SeraphisEngine::setPartialPositionAllVoices(i, x)` → `HarmonicCloud::setPartialPosition` (`:1069`) | Pan is not part of the spectral target; nothing overwrites it. Routing it through the state is impossible — `SpectralState` has no position field (`:57-62`). |
| Click a partial (mask **toggle**) | `setPartialMaskAllVoices(i, /*active=*/!desiredMasked)` → `setPartialMask` (`:1084`), where `desiredMasked = !currentMask` and `currentMask` is bit `i` of `CloudFrame::maskBits` (Q5) — the gesture toggles, it does not only mask. **The `active` argument is the INVERSE of "masked" (D-9 row 9a):** `HarmonicCloud::setPartialMask`'s body is `masked_[index] = !active` (`harmonic_cloud.h:1082-1089`), so `active == true` ⇒ **audible** and `active == false` ⇒ **silenced**, and `clearPartialMask()` is `masked_.fill(false)` (`:1101`) ⇒ everything audible. An earlier draft of this row wrote `setPartialMaskAllVoices(i, !currentMask)`, which is polarity-inverted: for an already-masked partial `!currentMask` is `false`, i.e. `active = false`, i.e. it **stays masked** — the documented unmask gesture would have been a no-op, and Q5's whole answer to "how does a user unmask" would have been unreachable. SC-033 is the criterion that fails on exactly that defect. | Same reason pan is direct. Masking is documented click-free — *"forces its target amplitude to zero at the END of the amplitude chain, so FR-014's smoother still applies"* (`:1081-1083`) — and a masked partial draws as a hollow ring at a fixed minimum radius rather than at zero radius (*Edge cases*), specifically so it stays a valid click target for the reverse gesture (Q5). |
| Blend A→B slider | `EditMessage` kind 7 (`BlendBegin`) on mouse-down snapshots the selected slot as a pristine A on the message thread; every subsequent `t` sends `blendStates(A_snapshot, slotB, t)` → written to the **selected** slot (Q2). | Absolute, not compounding: the selected slot is both the source of A and the destination, so without a latched snapshot every slider move would re-blend an already-blended result and `0 → 1 → 0` would not return — the same failure C-6 eliminates for `tiltState`. |
| Tilt dB control | `tiltState(slot, dB)` → selected slot | It is a state operation by construction; `dB` is absolute (C-6). |

**All voices, not the focus voice.** The pan/mask fan-outs write every slot in `[0, kMaxVoices)`, because a
voice allocated after the edit must inherit it — and because a per-voice override would make the same
gesture sound different depending on which slot the allocator happened to hand out.

**But the fan-out itself runs on the AUDIO thread, never from `notify()` (D-9 row 9b).** The two "Reaches"
cells above name the *destination*, not the calling thread. `setPartialPositionAllVoices` and
`setPartialMaskAllVoices` write `HarmonicCloud`'s `panPosition_`, `positionOverridden_`,
`panLeft_`/`panRight_` (`harmonic_cloud.h:1069-1079`, `updatePanGains` at `:1818-1834`) and `masked_`
(`:1084-1089`) — **every one of which `process()` reads and writes**. Calling them from `notify()` on the
message thread is a data race on engine-facing state, and it contradicts the plugin's own ownership
discipline (`spectralSlots_` is annotated *audio-thread-owned*, `processor.h:858`, and the three-buffer
staging ring exists precisely so a message-thread writer never touches engine state). So a kind-2/kind-3
message **stages** the value into `partialOverrides_` and release-stores `partialOverridesPending_`; the
**audio** thread acquires that flag at the top of `process()` and performs the fan-out there. The same
deferred path is what FR-030's `repushPartialOverrides()` uses after a clearing event, so there is exactly
one place in the build that calls the fan-outs and it is on the audio thread. `Processor::setActive` is
the sole exception to this rule anywhere in the plugin, and it states its own reason —
*"Both branches run on the host thread with the audio thread stopped"* (`processor.cpp:799-801`).

**The ratio/amplitude route reaches a sounding voice, audibly, not on the next note-on (D1).**
`SeraphisVoice::setSpectralState` gates on `isConfigurable()` — `!hasSounded_ || isFinished()`
(`seraphis_voice.h:770-776`, `:908`) — and rejects the push otherwise. Phase 11 **relaxes this gate for
`setSpectralState`/`setSpectralStateCount` only**: every other call it guards is unchanged. This is safe to
relax because the state it forwards into, `SpectralMorphEngine::setState`, already arms a click-free
absorption crossfade whenever the target slot contributes (`spectral_morph_engine.h:311`,
`slotContributes` at `:558`) — Phase 3's own FR-042/FR-044 prove `setState` continuity-safe, so the
`SeraphisVoice`-level gate was a stricter-than-necessary restriction, not a load-bearing one. FR-033a,
SC-028 – SC-030.

### C-5. One controller → processor IMessage channel

The processor has no `notify()` today. Phase 11 adds exactly one, handling one message ID
(`"SeraphisEdit"`) whose payload is a single binary attribute holding a POD `EditMessage`:

```cpp
struct EditMessage {
    std::uint8_t  kind;      // 0 EditorGate, 1 PartialRatioAmp, 2 PartialPan,
                             // 3 PartialMask, 4 BlendStates, 5 TiltState, 6 SlotSelect,
                             // 7 BlendBegin (Q2)
    std::uint8_t  slot;      // 0..3  (morph slot; ignored by kinds 0, 2, 3)
    std::uint16_t index;     // partial index, 0..63 (kinds 1, 2, 3)
    float         a;         // kind 1: ratio · kind 2: position · kind 4: t
                             // kind 5: ABSOLUTE dB/oct (C-6), never a delta
                             // kind 0: 0 = close, 1 = open — sent by the CONTROLLER'S refcount
                             //         only on a 0->1 / 1->0 transition (Q7), never per view
                             // kind 7: unused, reserved 0
    float         b;         // kind 1: amplitude (produced by FR-028's modifier-drag arm)
                             // kind 4: slot B index as float
                             // kind 7: slot B index as float — the snapshot source for the
                             //         gesture that follows (Q2); `slot` is the destination,
                             //         unambiguous for kinds 4 and 7 alike
};
```

1. **Message thread only.** `notify()` runs on the message thread, which is the same thread
   `Processor::setState` already writes the staging ring from — so the *"THREE staging buffers, not one …
   writer interlock"* argument (`processor.h:859-861`) holds unchanged, with no second interlock and no
   lock. Kinds 1, 4, 5 write the ring. **Kinds 2 and 3 STAGE — they do not call the C-4 fan-outs (D-9 row
   9b).** They write the staged value into `partialOverrides_` and then release-store
   `partialOverridesPending_ = true`; the **audio** thread acquires that flag at the top of `process()` and
   performs `setPartialPositionAllVoices` / `setPartialMaskAllVoices` there. An earlier draft of this clause
   said kinds 2 and 3 *"call the C-4 fan-outs directly"*, and that is a **data race**: those fan-outs write
   `HarmonicCloud`'s `panPosition_`, `positionOverridden_`, `panLeft_`/`panRight_` and `masked_`
   (`harmonic_cloud.h:1069-1079`, `:1084-1089`, `updatePanGains` at `:1818-1834`), all of which `process()`
   reads and writes on the audio thread. That the setters are `noexcept` and allocation-free is true and
   irrelevant — the hazard is concurrency, not allocation. The staging path costs one release store on the
   message thread and one acquire exchange per `process()` call on the audio thread (C-9, FR-042).
   **Kind 7 (Q2) writes neither** — it copies the
   selected slot into a message-thread-only 541-byte scratch (the pristine A) that only kind 4 messages in
   the same gesture read from; a kind 4 with no preceding kind 7 in the same gesture is dropped (clause 5).
2. **Never on the audio thread.** `notify()` allocates nothing and blocks nothing, but it is also never
   reached from `process()`.
3. **Coalescing is the controller's job, with a mandatory terminal flush (Q8).** A drag produces one
   message per mouse-move at most; the controller throttles to the cloud view's 30 Hz redraw — **at most
   one message per 33 ms per gesture** — so a fast drag cannot flood the queue, **and** sends exactly one
   additional, unthrottled message on gesture-end (mouse-up) carrying the gesture's final value. The
   throttle alone would let up to 33 ms of the gesture's tail go unsent; the flush is what guarantees the
   stored state and the pointer's last position never disagree.
4. **Kind 0 is the producer gate** (C-2 clause 6), and the controller sends it on a **refcounted**
   transition, not once per view (Q7): it increments a counter on `didOpen` and decrements it on
   `willClose`/`IPlugView::removed`, and sends kind 0 with `a = 1` only on the **0 -> 1** transition and
   `a = 0` only on the **1 -> 0** transition. The counter is reset to 0 in `terminate()`. A second editor
   opened while the first is still open therefore sends nothing, and closing the first while the second
   remains open sends nothing either — frames keep publishing until the last view closes. The processor's
   gate itself stays the plain bool C-2 clause 6 describes; only the controller's send logic changes.
5. **Unknown `kind`, out-of-range `slot`/`index`, and non-finite `a`/`b` are dropped silently.** A
   message is untrusted input; the mutators' own rejection (C-6) is the second line, not the first. A kind
   4 (`BlendStates`) received with no live kind-7 snapshot for the current gesture is dropped by this same
   rule (Q2).

### C-6. The three authoring mutators — contract and validity proof

Each is a Layer 2 free function in `Krate::DSP`, RT-safe (no allocation, no locks, no exceptions, no I/O)
and non-finite-safe via `detail::isNaN` / `detail::isInf` (`spectral_state.h:26`, `:91`) — **never
`std::isnan`**, per the header's own `-ffast-math` banner (`:21-23`).

**The contract is PRESERVATION, not establishment — and the distinction is normative.** None of the three
is a repair function. Given a state that already satisfies `isValidSpectralState`, each leaves (or
returns) a state that still does. Given a state that does **not** — `numPartials` outside `[0, 64]`,
descending ratios, an amplitude of `1.5`, a name with no NUL — `setPartial` and `tiltState` are **no-ops**
by the clauses below, which means they leave the state **byte-unchanged**, still invalid. A criterion that
demanded validity of the *output* for an invalid *input* would demand a repair function this spec
deliberately does not ship: silently rewriting a caller's `numPartials` or re-sorting their ratios would
destroy data the caller can still see and fix. SC-012 is stated as the conditional it must be, and asserts
the byte-unchanged property on the other branch — a no-op that quietly half-wrote the state would be a
defect the "returns invalid" reading cannot see. `blendStates` is the one exception and states its own
rule: it *selects* a valid input or a documented-valid default, so its return is unconditionally valid.

**`setPartial(SpectralState& s, std::size_t index, float ratio, float amplitude)`**
- **No-op on `!isValidSpectralState(s)` — the whole-state gate, and it is FIRST (D-9 row 9d).** An earlier
  draft's no-op list enumerated only the *local* rejections below, and that list and SC-012 clause 2
  disagreed about what the invalid branch is. SC-012's own coverage includes rows that are invalid
  **somewhere other than the edited index** — `amplitudes[5] = 1.5` (`spectral_state.h:106-108`), a
  descending ratio pair at index 30 (`:97-99`), a `name` field with no NUL (`:118-120`) — and every one of
  those rows *passes* every local check, so without the whole-state gate the function would store and the
  byte-unchanged assertion would fail on a state the contract calls a no-op. The gate is what makes
  "invalid in ⇒ byte-unchanged out" structural rather than tested-in, and it is the same gate `tiltState`
  already carries.
- No-op if `index >= static_cast<std::size_t>(s.numPartials)`, if `s.numPartials` is outside `[0, 64]`, or
  if either argument is non-finite.
- `amplitude` is clamped to `[0, 1]` (validator's requirement, `:106-108`).
- `ratio` is clamped to `[kMinStateRatio, kMaxStateRatio]` and then into the **strictly-monotone window**
  `[ratios[index-1] * kAuthorSpacing, ratios[index+1] / kAuthorSpacing]` (open ends omitted at the array
  edges), with `kAuthorSpacing = detail::factory::kFillSpacingFactor = 1.0163049f` (`:344`, the 28-cent
  spacing the header already uses for its geometric continuation). If that window is empty — neighbours
  closer than `kAuthorSpacing²` — the call is a **no-op**. Monotonicity is therefore preserved *by
  construction*, which is what SC-012 checks and what keeps `SpectralMorphEngine::setState` from rejecting
  the result (`spectral_morph_engine.h:296-298`).
- `name`, `tiltDbPerOct`, `inharmonicity`, `numPartials` are untouched.
- **Consequence the UI and SC-013 must both respect: the monotone window is TIGHT on a
  near-integer-harmonic state, and it binds hardest in the middle of the array.** Every factory state's
  authored ratios are integer or near-integer harmonics — `ratio = n` for SineStack, Choir and Breath
  (`spectral_state.h:398-413`) — so for partial `k` (0-based) the upper edge is
  `ratios[k+1] / kAuthorSpacing = (k + 2) / 1.0163049`. A perfect-fifth target of `1.5 * (k + 1)` exceeds
  that edge for **every `k >= 1`** (`k = 1`: target 3.000 vs edge 2.952), so the drag clamps and the
  partial does not move a fifth. Only `k = 0` (target 1.5, edge 1.968) and the **topmost authored slot**
  (whose upper neighbour is the geometric continuation, not `k + 2`) have a fifth of room. This is correct
  behaviour — C-6 clamps rather than swaps, per *Edge cases* — but it means any criterion asserting a
  named interval MUST pin the index. SC-013(a) does.

**`blendStates(const SpectralState& a, const SpectralState& b, float t) -> SpectralState`**
- `t` is clamped to `[0, 1]`; a non-finite `t` returns `a` unchanged.
- If exactly one input fails `isValidSpectralState`, the **valid** one is returned unchanged; if both fail,
  a default-constructed `SpectralState{}` is returned — documented valid (`:42-43`, *"The default value is
  valid, finite, silent and anonymous"*).
- `numPartials = min(a.numPartials, b.numPartials)`, so no unauthored slot leaks into the active range.
- **Ratios interpolate in `log2`**, the domain the morph engine itself stores (`spectral_morph_engine.h:286`),
  then exponentiate. *Validity proof:* `log2` is strictly increasing, a convex combination of two strictly
  increasing sequences is strictly increasing, `exp2` is strictly increasing — so the result is strictly
  increasing; and a convex combination of two values in `[0.5, 128]` lies in `[0.5, 128]`, so no clamp is
  needed and none is applied.
- Amplitudes interpolate **linearly**; both inputs lie in `[0, 1]`, so the result does.
- `tiltDbPerOct` and `inharmonicity` interpolate linearly (both inputs in range ⇒ result in range).
  Slots at `i >= numPartials` are filled by the same geometric continuation the header already defines
  (`:416-433`), so the result is byte-shaped like a factory state.
- `name` is the NUL-terminated ASCII literal `"Blend"`, which trivially satisfies `:111-127`.

**`tiltState(SpectralState& s, float dbPerOct)`**

**`dbPerOct` is ABSOLUTE, not a delta.** FR-028 drives it from a *control* whose displayed value is the
state's tilt, so `tiltState(s, -6)` must mean "this state now has a −6 dB/oct tilt" no matter how many
times it is called. A multiplicative-delta reading would make the audible bake compound while the stored
field saturated at `kMaxStateTiltDbPerOct` and then stopped moving — the field and the sound would
diverge permanently, and SC-013(c)'s monotone sweep over `{−12, −6, 0, +6, +12}` would not hold on a
correct implementation. Absoluteness is achieved by **undoing the state's current tilt before applying
the new one**, which is exact arithmetic on the same expression, not an approximation.

- No-op on non-finite `dbPerOct` or on `!isValidSpectralState(s)`.
- `dbPerOct` is clamped to `[kMinStateTiltDbPerOct, kMaxStateTiltDbPerOct]` (`:53-54`); call the clamped
  value `target` and the state's existing `s.tiltDbPerOct` `current`.
- **It bakes the tilt into `amplitudes`, because writing the field alone would be inaudible** — the slot is
  stored sanitized and *"`tiltDbPerOct` … structurally incapable of reaching the audio path"*
  (`spectral_morph_engine.h:285-289`). For `i < numPartials`:

  ```
  const float delta   = target - current;                 // 0 when nothing changes
  const float octaves = std::log2(ratios[i] / ratios[0]); // ratios[0] > 0 by validity
  amplitudes[i] *= std::pow(10.0f, (delta / 20.0f) * octaves);
  ```

  then `normalizeSpectralState(s)` (`:155`), then `s.tiltDbPerOct = target`.
- **`std::pow(10.0f, x)`, never `exp10f`.** `exp10f` is a **glibc GNU extension**, not standard C++ and
  not provided by MSVC; the repo already hit this and adopted a standing convention against the name —
  *"10^x without std::pow (FR-033). Deliberately NOT named `exp10f`: glibc declares a global `exp10f` as a
  GNU extension."* (`dsp/include/krate/dsp/systems/continuous_body.h:1643-1645`). This call site is
  **configuration-time, not audio-thread** (`makeFactoryState` already evaluates ~200 `std::pow` calls at
  the same cadence), so the portable form costs nothing and a Layer-2 copy of `continuous_body.h`'s
  `exp10Fast` is explicitly **not** taken — that would be a DSP addition beyond the enumerated set.
- *Validity proof:* after L2 normalisation every element satisfies `|x| <= ||x||₂ / ||x||₂ = 1`, since the
  L2 norm of a vector is never smaller than its largest element — so amplitudes land in `[0, 1]` with no
  clamp, and the `sumSquares > 0` guard (`:169`) leaves an all-zero state alone. Ratios are untouched, so
  monotonicity is untouched. `target` is in `[kMin, kMax]` by the clamp, so the assigned
  `s.tiltDbPerOct` satisfies the validator's range check with no second clamp.
- **The field and the bake never diverge**, which is the whole point of the absolute form: `s.tiltDbPerOct`
  is serialized (`:252`) and shown in the mini-toolbar, and after any call it is exactly the tilt that is
  baked into `amplitudes` relative to an untilted state. Normalisation rescales all amplitudes by one
  common factor and therefore changes no *relative* tilt.

### C-7. Custom-view instantiation, plus the sub-controller the roadmap names

Roadmap line 484 requires that *"Custom views get the standard sub-controller treatment (vst-guide
skill)"*, and in that skill "sub-controller" is a **specific named mechanism**, not a synonym for
"custom view": the uidesc `sub-controller` attribute causes VSTGUI to call
`IController::createSubController()` on the active controller
(`.claude/skills/vst-guide/UI-COMPONENTS.md:282`, `:315`), and the returned object is a
`VSTGUI::DelegationController` subclass (`:320-370`, `:433-449`) that owns tag mapping and control
listening for its sub-tree. Phase 11 adopts it, and this clause states what it owns.

**C-7a. Instantiation — two mechanisms, both deliberate:**

- **`CloudView` and `DrawerContainer`** are created through `VST3EditorDelegate::createCustomView`
  (Membrum's route, `controller.cpp:1037-1054`; uidesc `<view class="CView" custom-view-name="CloudView"/>`,
  `plugins/membrum/resources/editor.uidesc:352`). They need the controller anyway — for the cached frame
  and for the edit channel — so a factory-registered creator with no controller reference would only have
  to find it again.
- **`MacroRingKnob`** derives from `Krate::Plugins::ArcKnob` and is created by a `ViewCreatorAdapter` in
  the shipped `UIViewFactory` style (`arc_knob.h:555-564`, `:714-716`), because it must accept
  `control-tag` and every `CControl` attribute from the uidesc — which is what `getBaseViewName() ->
  kCControl` (`:562-564`) buys. `entry.cpp` gains `#include <ui/arc_knob.h>` **and** the Seraphis creator's
  header, because the inline creator objects only register in a TU that is actually linked.
  **This is the hazard the Phase 8 uidesc banner named** (`editor.uidesc:3-5`: *"a custom class name here
  would be dropped silently on a leg where the ViewCreator TU was not linked"*), and SC-004 is the gate
  that keeps it from recurring: the lifecycle harness builds the whole tree from the uidesc and the test
  asserts the views are of the expected concrete type, not merely that a view exists.

**C-7b. One sub-controller owns every tag-less control.** The editor tree carries several controls that
drive **session state, not a `ParamID`** — and a control with no `control-tag` has no listener unless
something claims it. Rather than scattering those listeners across three view classes and the controller,
**the TEMPLATE ROOT carries `sub-controller="SeraphisEdit"`** in the uidesc,
`Controller::createSubController` returns a `UI::SeraphisEditSubController`
(a `VSTGUI::DelegationController` subclass, `UI-COMPONENTS.md:433-449`), and **that object owns
`valueChanged` for all of them** — the roster is the table below.

**The root, not an intermediate container, and this is not a stylistic choice (D-4).** Two things depend
on it. (i) **Coordinates.** C-1's rect table, FR-023 and FR-024 all state drawer and cloud rects in the
1000 × 700 **absolute** template coordinate space, and `getViewSize()` returns a rect in the parent
container's space — so if the attribute sat on an intermediate container, the drawer would be that
container's child and its `getViewSize()` would be offset by that container's origin, making SC-020(c)/(e)'s
byte-equal rect comparisons assert numbers no correct build produces. With the attribute on the root, the
drawer and the cloud view are direct children of the 1000 × 700 root and every published rect is already
in the coordinate space the spec quotes. (ii) **Reach.** The header preset button (FR-007) sits in the
**header**, outside any cloud/drawer sub-tree, and it is tag-less. `createSubController` claims listeners
only for the sub-tree beneath the container that carries the attribute, so a container-level placement
would put that button out of the sub-controller's reach and force FR-045 to grow a carve-out for it. Root
placement makes FR-045 satisfiable **with no carve-out**, which is why no carve-out is taken.

| Control | Session tag | FR | What it drives |
|---|---|---|---|
| **Header preset button** | **`kPresetButtonTag` (9000)** | **FR-007** | opens/closes the `PresetBrowserView` (session) — D-5 |
| Obs/Edit toggle | `kModeToggleTag` (9001) | FR-027 | cloud-view mode (session) |
| Drawer handle | `kDrawerHandleTag` (9002) | FR-023 | open/collapsed (session) |
| Blend A→B slider | `kBlendTag` (9003) | FR-028 | `EditMessage` kind 4 |
| Tilt dB control | `kTiltTag` (9004) | FR-028 | `EditMessage` kind 5 |
| Seven drawer tab buttons | `kTabBaseTag + i` (9100…9106) | FR-022 | active tab index (session) |
| Morph slot selector buttons | `kSlotBaseTag + i` (9200…9203) | RQ-1 | `EditMessage` kind 6 (selected slot) |

**Every tag in that column is a SESSION tag, `≥ kSessionTagBase = 9000`, and can never collide with a
`ParamID`** — the registered surface tops out at 1443 (C-3) and the frozen-type legend forbids reusing a
registered ID for anything else, so a control carrying a session tag is also never counted as a parameter
binding by SC-002. A tag-less control declares its session tag through a **non-standard uidesc attribute**
(`session-tag`), which the view factory leaves alone and which therefore survives into `UIAttributes` for
the sub-controller's `verifyView` to read. One `valueChanged` switch then distinguishes every control in
this table without any of them owning a parameter.

**The header preset button is in the table, and FR-045 is NOT carved out (D-5).** An earlier draft left
FR-007's button tag-less with no listener owner named anywhere, which made FR-045's *"every tag-less
control in C-7b's table MUST have that object as its `IControlListener`"* either false or dependent on a
carve-out that excused it. Carving it out would have been the weakening resolution: it would have left the
one header control with no named owner and no criterion. D-4's root-level `sub-controller` placement is
what makes the button reachable by the same object as the drawer's controls, so it is simply a row in this
table like the others, and SC-022(b) and SC-022(d) assert it.

The sub-controller does **not** remap tags: unlike Disrumpo's per-band case (`UI-COMPONENTS.md:315`,
`:376-377`), Seraphis has no repeated template needing per-instance `ParamID`s, so
`getTagForName` is left to `DelegationController`'s forwarding default and only `valueChanged` and
`verifyView` are overridden. **`createSubController` is called once per template instantiation, in
document order** (`UI-COMPONENTS.md:370`), and the same source records the trap: the controller's
instance bookkeeping MUST be reset in `willClose()` or a reopened editor gets stale indices. FR-041
covers that alongside the raw-pointer rule.

The sub-controller is **not a `CView`** and therefore does not count against the roadmap's
*"Custom-view surface is exactly three"* (roadmap lines 516–517). FR-026 counts `CView` subclasses; FR-045
states the sub-controller obligation separately, and SC-022 measures both.

**C-7c. Teardown discipline, non-negotiable:** every raw view pointer the controller caches is zeroed in
`willClose()` (Membrum's rule, `controller.h:191-199`), every `CVSTGUITimer` a view owns is cancelled
in `removed()` (`pad_grid_view.h:37`), and the sub-controller instance counter is reset in `willClose()`.
SC-005 runs this under the shared harness; it only has teeth
under ASan/valgrind, which the nightly lane provides (`plugins/seraphis/CLAUDE.md`, and
`editor_lifecycle_test.cpp:222-228`).

### C-8. Redraw cadence and the Edit-mode freeze

- The cloud view owns a **30 Hz `CVSTGUITimer`** (Membrum's rate, `pad_grid_view.h:32-34`), reads the
  controller's cached `CloudFrame`, and calls `invalid()` **only when `sequence` changed** — so a stopped
  transport with no notes costs one timer callback and no redraw.
- Publish rate (≈ 94 Hz at 512 samples / 48 kHz) exceeds consume rate on purpose: the consumer takes the
  **most recent** block and discards older ones, which is the documented Membrum/Innexus rule
  (`plugins/membrum/src/controller/controller.cpp:1719-1726`). Dropped frames are correct behaviour, not
  a defect.
- **In Edit mode the constellation still animates.** Editing does not freeze the view; the dragged partial
  is drawn at the *pointer*, every other partial keeps following the frames. Freezing would hide exactly
  the feedback the edit exists to produce.

### C-9. Nothing in this phase changes what the plugin sounds like at the defaults

**At every default** — every macro at its FR-060 neutral (`seraphis_macro_matrix.h:548-550`) and the
editor closed — Phase 11 produces the same samples as Phase 10. The audio path gains no stage, no
smoother and no nonlinearity. The additions inside `process()` are **exactly four**, and this enumeration
is complete (D-9 row 9c — an earlier draft listed only the first three, which stopped being complete the
moment 9b moved the pan/mask fan-out onto the audio thread):

1. the C-2 clause 6 gate predicate (one relaxed atomic load);
2. the read-only snapshot loop plus `publishCloudFrame()`, only when the gate is open;
3. **`partialOverridesPending_.exchange(false, std::memory_order_acquire)`** — one acquire exchange per
   `process()` call, unconditional, and the **deferred pan/mask fan-out** it guards (9b), which runs only
   on the calls where the exchange returned `true` and is therefore silent on every block with no edit in
   flight;
4. C-10's composition,

the last of which at neutral adds **exactly** `0.0f`
to each of its two targets: the matrix's own banner states *"at neutral every term is exactly 0 -
applyModCurve(c, 0) == 0 for all three"* (`:778`), and `kFxDelayMixId` / `kFxWanderDepthId` both default
to `0` (Phase 10 C-6), so the composed value is the deep value bit-for-bit and the FR-010 send-stage skip
is taken on the same blocks it was before.

SC-001 is the negative control that proves it, in the shape Phase 10's SC-002 established: **one build,
one process, one `Processor` instance**, with the C-2 clause 6 gate as the only difference between the two
arms. It is deliberately **not** a Phase-10-build-versus-Phase-11-build comparison — that would demand
bit-identical floating point across toolchains, which `tests/test_helpers/render_fingerprint.h:20-30`
measures at 2.9e-5 per sample and roadmap line 598 forbids.

### C-10. Macro reach into the effects surface — the fourth target owner

Phase 10's RQ-4 named this phase, by name, as the discharge point: *"Roadmap KDD-1 is discharged by
**Phase 11** (macro reach into the effects surface) and **Phase 12** (shipped patches with non-zero
sends)"* (`specs/seraphis-phase10-effects/spec.md:182-183`, restated `:1867-1876`). No later phase claims
it — Phase 12 is presets/release (roadmap lines 538–546) and Phase 13 is per-note expression (roadmap
lines 548–569) — so it is discharged here, in the shape the matrix already ships for the Aether half.

1. **Two rows, two macros, two targets.** The choice follows each macro's documented character rather
   than convenience:

   | Macro | New target | Effects ID it composes into | Why this macro |
   |---|---|---|---|
   | Dissolve | `SeraphisMacroTarget::FxDelaySend` | `kFxDelayMixId` (1410) | Dissolve is *"atmosphere mix ↑, spectral blur ↑, transient definition ↓, envelope slew ↑"* (`seraphis_macro_matrix.h:312-313`) — smearing the voice sum into spectral echoes is the same gesture one stage later. |
   | Entropy | `SeraphisMacroTarget::FxWanderDepth` | `kFxWanderDepthId` (1441) | Entropy already owns **both** existing drift depths — `CloudDriftDepthCents` (`:422-427`) and `AtmosDriftDepth` (`:428-433`) — and the stereo wander is a `BrownianDrift` of exactly that family. The row makes the third drift move with the other two instead of being the one that does not. |

   Both rows use `.base = 0.0f` — **the shipped default of the parameter they compose into**, which is
   FR-060's rule and what makes the composition an identity at neutral (C-9) — and `ModCurve::Linear`
   (never `Stepped`, which `noRowUsesSteppedCurve` forbids at `:820`).

   **The two `.amount` values — OQ-4's write-back (measured, not chosen).** The ruled acceptance band is
   fixed and does not move: the isolated send-return RMS at **Dissolve = 1** must land in
   **[−20 dB, −6 dB]** relative to the dry sum, and the five-point sweep must be strictly monotone.

   | Row | Ruled pilot start | **Shipped `.amount`** | Status |
   |---|---|---|---|
   | `Dissolve → FxDelaySend` | `0.35f` | **`0.20f`** | **MEASURED and in band** — see the table below |
   | `Entropy → FxWanderDepth` | `0.50f` | **`0.50f`** | **MEASURED and monotone** — the pilot value survived the sweep; see the second table below |

   **Dissolve → `FxDelaySend`, the measurement that moved it off the pilot value.** `0.35f` put SC-005's
   ID-102 arm (`tests/integration/param_continuity_test.cpp`, "no zipper, no click") over its 1.5×
   bound. Measured ratio of `maxTest/maxRef` over the Dissolve automation render against `.amount`,
   everything else held:

   | `.amount` | SC-005 ratio (bound 1.5) | isolated return at Dissolve = 1 |
   |---|---|---|
   | `0.00` | 1.3532 (pre-Phase-11) | — (send never runs) |
   | `0.12` | 1.4232 | ≈ −23.7 dB — **outside the band** |
   | **`0.20`** | **1.4665** | **−19.3 dB — in band** |
   | `0.35` | 1.5138 — **FAILS** | −14.4 dB |

   `0.20f` is the **lowest in-band value** and therefore the one with the most SC-005 headroom. Both
   margins are thin — 2.2 % on SC-005, 0.7 dB inside the band — and the reason is **not** this row: ID
   102's SC-005 ratio was already 1.353 before Phase 11, against ≈ 1.0 for every other in-scope ID (Dream
   0.98, Bloom 1.03, Gravity 0.91, Entropy 1.01, 1410 1.02, 1441 1.12), so Dissolve's Phase 9 rows leave
   almost no room for any new audible reach. Retuning means editing this `.amount`; it never means
   lowering SC-005's bound or widening the band.

   **`Entropy → FxWanderDepth`, the measurement that KEPT it at the pilot value (2026-08-04).** Its
   acceptance has the same shape as the Dissolve row's: the five-point Entropy sweep must be **strictly
   monotone** in M/S **side** RMS, measured over `preOutputTapLForTest()` / `preOutputTapRForTest()` with
   `preOutputTapTruncatedForTest() == false` (C-10 clause 5). At `0.50f` it is monotone and very nearly
   linear:

   | Entropy | isolated M/S side RMS of the effects stage |
   |---|---|
   | 0.00 | `0` — **exactly** zero; the FR-010 ENGAGE predicate never fires |
   | 0.25 | `0.000464157` |
   | 0.50 | `0.000928180` |
   | 0.75 | `0.001394900` |
   | 1.00 | `0.001862240` |
   | **side(1.0) / side(0.25)** | **`4.01208`** — `4.0` would be exact linearity |

   The near-linearity is the expected shape rather than luck: FR-024a makes the depth a **plugin-side
   multiply**, so the underlying `BrownianDrift` trajectory is identical at every sweep point. The
   `.amount` therefore did **not** move — the rule was "if the sweep is not monotone the `.amount` moves,
   and the band does not", and the sweep is monotone. The table is emitted by the test itself, via `WARN`
   on a **passing** run (`Seraphis_MacroDissolve_ReachesEffects`, SC-021(a) Entropy section), reading the
   shipped `.amount` straight out of `SeraphisMacroMatrix::kRows` so the recorded table can never drift
   from the value it was measured at.

2. **A fourth owner, built exactly like the third.** `SeraphisMacroTargetOwner::Effects` joins
   `{ Voice, Engine, Aether }` (`:52`). The two new `SeraphisMacroTarget` values are appended **after the
   Aether block and immediately before `Count`**, which is what keeps the change additive: every existing
   target keeps its index, so `aetherFieldIndex`'s `[kFirstAetherTarget, kFirstAetherTarget +
   kNumAetherTargets)` window (`:446-452`) and `SeraphisAetherTargets`' field offsets are untouched.
   `struct SeraphisEffectsTargets { float delaySend = 0.0f; float wanderDepth = 0.0f; };` declares its
   fields **in enum order**, the same invariant the Aether POD documents at `:442-445`.
   `[[nodiscard]] SeraphisEffectsTargets computeEffectsTargets() const noexcept` is a copy of
   `computeAetherTargets` (`:667-679`) — *"Pure function of the knobs and the table; writes nothing"*
   (`:662`) — and `apply(SeraphisEngine&)` (`:623`) gains **no line**, because an Effects-owned target is
   read by the plugin, never pushed into the engine.

3. **The compile-time guards extend, they do not relax.** `everyRowOwnerIsValid`'s owner ladder
   (`:466-474`) currently encodes "Aether target ⟺ Aether owner, otherwise Voice or Engine"; it gains the
   matching biconditional for Effects, so a row that names an effects target with a Voice owner is a
   **compile error**, not a silent no-write. A new `everyEffectsRowHasAPodField` mirrors
   `everyAetherRowHasAPodField` (`:480`). `everyTargetInFr061to065IsPresent` (`:822`) and
   `everyRowSharesOneBasePerTarget` (`:824`) both continue to hold for the two new targets by the rows in
   clause 1. **No existing guard is weakened or removed** — that is the line between "additive" and
   "changed behaviour".

4. **The deep parameter is the base, not a competitor.** The effects params compose through the seam
   Phase 9 built for the other 91: the processor calls `setTargetBase(FxDelaySend, deepDelayMix)` /
   `setTargetBase(FxWanderDepth, deepWanderDepth)` when the deep value changes (`:708`), and reads the
   composed value back out of `computeEffectsTargets()`. There is exactly one write path per target, which
   is the property `setTargetBase`'s own banner calls *"a deep parameter IS the origin the macros move
   from rather than a second, competing write path"* (`:684-685`).

5. **Cadence: the composed value must be recomputed on the macro's cadence, not the deep param's — by
   substituting the reads, not re-pointing a change-guard (D2).** `effectsPushes_`
   (`processor.cpp:1821`) — the counter an earlier draft named as the guard to re-point — is incremented
   only inside `pushEffectsParams()`, and 1410/1441 are not in that function's ID set: both ride class-(b)
   smoothers instead (`fxReturnGainSm_`/`fxWanderDepthSm_`, read at `:2351`/`:3052`), so there is no
   raw-deep-value guard on either to re-point. The actual mechanism: `composedEffects_ =
   macros_.computeEffectsTargets()` is computed **once per `process()` call**, before the slice loop, and
   `updateEffectsBypassState`'s read at `:2351` and `updateParamSmootherTargets`'s read at `:3052` are
   substituted to read the clamped composed value in place of the raw deep atomic. This is a one-block
   lag between a macro/deep move and its effect on the composed value — inside the 20 ms class-(b)
   smoothing time both consumers already impose, so it is a target-arrival delay, never a discontinuity
   (*Clarifications*, composition-cadence entry). Cost: one `evaluateAll()` (32 rows × one
   `applyModCurve`) per `process()` call, three orders below SC-009's snapshot budget.

6. **What this does not do.** It ships **no non-zero default**: at the shipped neutrals both composed
   values are `0`, the send stage stays skipped, and Phase 10's SC-002 stays true of the Phase 10 code it
   describes. Non-zero *shipped patches* remain Phase 12's half of RQ-4.

### C-11. The controller-side `SpectralState` mirror — display-only (Q1)

The Edit surface needs the selected slot's current ratios, amplitudes and tilt to draw the y-axis (C-4,
Q6) and the absolute Tilt readout (FR-028), and the controller has no way to read them today: it derives
the DSP's morph parameters, but `loadMorphParamsToController` **discards** the four 541-byte payloads into
a scratch buffer (`morph_params.h:521-532`, *"The controller has nowhere to put a SpectralState"*), and
`SpectralMorphEngine` exposes no per-slot getter (`spectral_morph_engine.h:423-449`, only the blended
output arrays).

1. The controller owns `std::array<SpectralState, 4> slotMirror_`, one entry per morph slot.
2. **Re-seeded from two sources, and never derived from the `CloudFrame`:**
   - a `409–412` dropdown change re-seeds that slot from `makeFactoryState(stateId)` — the same source the
     processor's own factory-derivation path already uses (`processor.cpp:1383-1385`);
   - loading a state stream re-seeds all four slots from the four 541-byte payloads it carries, which is
     exactly the change `loadMorphParamsToController` needs (Q1; *Existing components*).
3. **Every authoring mutator (`setPartial`, `blendStates`, `tiltState`) runs twice per gesture step: once
   locally against `slotMirror_[slot]`, once via the `EditMessage` sent to the processor.** The two copies
   apply the same Layer 2 function to the same starting state and are expected to agree, but neither is
   ever read back from the other to reconcile them — the mirror is asserted **display-only** by
   construction, not merely by convention.
4. **The processor's staging ring remains the sole audio-thread authority.** `slotMirror_` never reaches
   `process()`, is never serialized, and a divergence between it and the processor's `spectralSlots_` is a
   **cosmetic** defect (a stale-looking readout) — the audio path only ever reads the processor's own copy,
   so it is never an audio defect.
5. Moving a slot's dropdown (FR-035) re-seeds `slotMirror_[slot]` from the factory table in the same
   message that discards the processor's edits to that slot, so the two copies do not diverge on that path
   either.

---

## Functional Requirements

### A. Layout and `editor.uidesc`

- **FR-001.** `resources/editor.uidesc` MUST be replaced wholesale with the C-1 layout. The Phase 8
  placeholder template (`editor.uidesc:140-184`) and its banner (`:3-5`) MUST be gone.
- **FR-002.** The `<control-tags>` block MUST be carried over unchanged, all 107 entries
  (`editor.uidesc:21-139`). No tag may be added, removed, renamed or re-numbered.
- **FR-003.** Every one of the 107 registered IDs MUST be bound by **at least one** view carrying its
  `control-tag`, per C-3's assignment table, and by **exactly one except** for the three IDs on C-3's
  enumerated duplicate-binding allowlist — `kAtmosFreezeId` (1008), `kAetherFreezeId` (1204),
  `kFxSpectralFreezeId` (1430) — each of which is bound **exactly twice**: once in its drawer tab and once
  in the header freeze cluster (RQ-2). The total bound-view count MUST be **exactly 110**. No ID outside
  that three-element set may appear on more than one view. The `unreachableParams` allowlist MUST be
  **empty** (that allowlist is for params reachable only through a custom view, which Phase 11 has none
  of — it is a different list from the duplicate-binding allowlist and the two must not be conflated).
- **FR-004.** No registered parameter's type, range, default, or `stepCount` may change. `plugin_ids.h`'s
  frozen-type legend (`:184-240`) is authoritative and untouched.
- **FR-005.** The template MUST use VSTGUI classes only. No Win32, Cocoa/AppKit or native popup appears in
  any Phase 11 source file. (Root `CLAUDE.md`, *Cross-Platform Requirement*.)
- **FR-006.** The cloud view MUST be the first child added to the editor template's content area and MUST
  span the full area between the header and the collapsed drawer strip, so the rings and the Obs/Edit
  toggle draw over it.
- **FR-007.** The header MUST carry, left to right: the title, a preset button opening a
  `Krate::Plugins::PresetBrowserView` over the existing `presetManager_` (`controller.cpp:57-58`), the
  **freeze cluster** (three toggles bound to 1008, 1204, 1430 — RQ-2), and the four global controls
  `kMasterGainId` (0), `kPolyphonyId` (1), `kSoftLimitId` (2), `kSeedId` (3).
  **Total header-bound views: 7** — four IDs owned exclusively by the header, plus the three freeze IDs
  whose primary binding lives in their drawer tabs (C-3). The preset button carries no `control-tag` and
  is not counted.

### B. The cloud view and its data path

- **FR-010.** `Seraphis::CloudFrame` MUST be defined as C-2 specifies, as a POD with a `static_assert` on
  `sizeof`, in `src/processor/cloud_frame.h`, with the producer/consumer banner Membrum's payload header
  carries (`meters_block.h:8-11`).
- **FR-011.** The processor MUST own a `std::unique_ptr<Steinberg::Vst::DataExchangeHandler>` created in a
  new `connect()` override and destroyed in a new `disconnect()` override, with `onActivate` /
  `onDeactivate` driven from `setActive` — the Membrum lifecycle verbatim
  (`plugins/membrum/src/processor/processor.cpp:1111-1163`).
- **FR-012.** `Processor::process` MUST publish **at most one** `CloudFrame` per call, after the slice
  loop, guarded on `dataExchangeHandler_ != nullptr` **and** the C-2 clause 6 gate. It MUST NOT publish
  from `renderSlice`.
- **FR-013.** The frame's fields MUST be filled exactly as C-2 clause 3 specifies, from the cited
  `HarmonicCloud` accessors, with entries at `i >= partialCount` zero-filled. This includes
  `morphTravelPosition` (mirrored from `SpectralMorphEngine`'s current journey position, Q4) and
  `maskBits`/`overriddenBits` (mirrored bit-for-bit from the processor's `partialOverrides_` table, Q5).
- **FR-014.** The focus voice MUST be selected by C-2 clause 4's three-step rule, evaluated once per
  publish.
- **FR-015.** The snapshot MUST be RT-safe: no allocation, no lock, no exception, no I/O, no `std::sort`,
  no transcendental. It is a bounded read loop over at most `HarmonicCloud::kMaxPartials = 64` entries
  (`harmonic_cloud.h:138`) plus a `memcpy` into the exchange block.
- **FR-016.** The controller MUST implement `IDataExchangeReceiver` with the Membrum member and interface
  wiring (`plugins/membrum/src/controller/controller.h:44`, `:146`, `:365`), set
  `dispatchOnBackgroundThread = false` in `queueOpened`, and cache **only the most recent** block per
  delivery (`controller.cpp:1719-1726`).
- **FR-017.** `UI::CloudView` MUST draw one point per active partial with `x` from `position[i]`, `y` from
  `log2(frequencyHz[i])` mapped over a fixed 20 Hz – 20 kHz span, and radius from `amplitude[i]`. The
  mapping MUST be monotone in each axis and MUST clamp rather than wrap at the span edges. **Exception
  (Q5):** a partial whose `maskBits` bit is set MUST be drawn as a hollow ring at a fixed minimum radius
  regardless of `amplitude[i]`, so a masked partial (whose amplitude has smoothed to 0) stays a valid click
  target for the unmask gesture instead of disappearing.
- **FR-018.** `UI::CloudView` MUST own a 30 Hz `CVSTGUITimer`, redraw only on a changed
  `CloudFrame::sequence`, and cancel the timer in `removed()` (C-8, C-7).
- **FR-019.** With no frame ever received — no host DataExchange support, or the editor opened before the
  processor connected — the cloud view MUST render an empty constellation and MUST NOT be blank-broken,
  crash, or spin. `activeVoices == 0` is a valid frame and renders as an empty field.

### C. Macro rings

- **FR-020.** `UI::MacroRingKnob` MUST derive from `Krate::Plugins::ArcKnob` (`arc_knob.h:49`), be
  registered through a `ViewCreatorAdapter` with `getBaseViewName() -> UIViewCreator::kCControl`
  (`:562-564`), and be instantiated **five** times — one per macro ID 100–104 — from one class.
- **FR-021.** The perturbation a ring produces in the constellation MUST be the **real DSP response**, read
  back out of `CloudFrame`. No view-local animation, no synthetic displacement, no interpolation toward a
  target the DSP is not producing. (Roadmap lines 507–508; the payoff is only real if it is real.)

### D. The drawer

- **FR-022.** `UI::DrawerContainer` MUST present seven tabs, named and ordered exactly:
  **Cloud, Morph, Body, Atmos, Aether, FX, Life/Env** (roadmap line 501).
- **FR-023.** In the fixed 1000 × 700 window (RQ-3), the drawer's `getViewSize()` MUST be **exactly**
  `(0, 670, 1000, 700)` collapsed — a 30 px tab strip — and **exactly** `(0, 420, 1000, 700)` open — 280 px,
  i.e. 40 % of 700 (roadmap line 509). The handle toggles between those two rects and no others; the state
  is session-only and never a parameter. (The roadmap's "~30 px" and "~40 %" are pinned to these integers
  here so the requirement is measurable rather than approximate.)
- **FR-024.** Opening the drawer MUST NOT remove, hide, unmount or stop the cloud view. It overlaps: the
  cloud view's `getViewSize()` MUST remain `(0, 32, 1000, 670)` in both drawer states.
  FR-018's timer keeps running and FR-017 keeps drawing (roadmap line 511).
- **FR-025.** Drawer contents MUST be plain uidesc controls — `ArcKnob` / `CSlider` / `COptionMenu` /
  `CCheckBox` — with no additional custom class (roadmap lines 516–517). Exactly one tab's page is visible
  at a time; the other six are inactive template content or hidden children, never separate `.uidesc`
  files.
- **FR-026.** The total **custom-view class** count introduced by this phase MUST be **exactly three**:
  `CloudView`, `MacroRingKnob`, `DrawerContainer` (roadmap lines 516–517). "Custom-view class" means a
  class under `plugins/seraphis/src/ui/` deriving, directly or transitively, from `VSTGUI::CView`.
  `UI::SeraphisEditSubController` derives from `VSTGUI::DelegationController`, not `CView`, and is
  therefore **not** one of the three — it is required separately by FR-045.

### E. Observe / Edit and the authoring mutators

- **FR-027.** The cloud view MUST carry an Observe/Edit toggle at its bottom-right (roadmap lines 512–515).
  Observe is the default on every editor open; the mode is session state, never a parameter. The toggle
  carries no `control-tag`; its listener is C-7b's sub-controller.
- **FR-028.** In Edit mode the view MUST support **four** gestures, routed exactly as C-4's table
  specifies: vertical drag of a partial (**ratio**), **modifier-drag** vertically (**amplitude**),
  horizontal drag (**pan**), and click (**mask**). The amplitude arm is not optional — `setPartial`'s
  inherited signature is `(index, ratio, amplitude)` (roadmap line 525) and the cloud view is *"the sole
  consumer of the inherited mutators"* (roadmap lines 514–515), so without it one of the mutator's three
  dimensions has no producer anywhere in the plugin and `EditMessage::b` would be a dead field. The
  modifier is a plain VSTGUI `CButtonState` alt-key test — never a platform key API (FR-005). The view
  MUST also carry a mini-toolbar with a **Blend A→B** slider driving `blendStates` and a **Tilt dB**
  control driving `tiltState`, whose value is the **absolute** tilt of the selected slot (C-6), not a
  delta; both are tag-less and listened to by C-7b's sub-controller.
  - **The vertical drag's reference (Q6).** The axis it drags against is drawn from the selected slot's
    C-11 mirror — `ratios[i] * referenceHz` — never from `CloudFrame::frequencyHz[i]`, which is
    drift-inclusive; `referenceHz` is `fundamentalHz` (undetuned, C-2 clause 3) when a voice sounds, or the
    fixed **C4 = 261.63 Hz** reference when `activeVoices == 0`. The inverse map used to compute a drag's
    `newRatio` divides by that same reference in both cases, so a drag can never bake momentary Brownian
    detune into the stored ratio, and Edit-mode authoring MUST work identically with no note held.
  - **The click gesture is a toggle (Q5).** Its new mask value MUST be the logical NOT of
    `CloudFrame::maskBits`'s current bit for that partial — never an unconditional mask — and a masked
    partial's hollow-ring rendering (FR-017) MUST keep it clickable so the toggle is reversible.
  - **The Blend slider is gated by a gesture-begin message (Q2).** Mouse-down on the slider MUST send
    `EditMessage` kind 7 (`BlendBegin`, C-5) before any kind 4, snapshotting the selected slot as a
    pristine A on the message thread; every subsequent slider `t` re-blends that snapshot, matching C-6's
    absolute, non-compounding contract for `tiltState`. This is what keeps `0 → 1 → 0` reversible instead of
    a one-way ratchet.
- **FR-029.** Ratio and amplitude edits MUST reach the DSP through the morph slot — controller →
  `EditMessage` → `Processor::notify` → `spectralSlotsStaging_` → `spectralSlotsHandoff_` →
  `SeraphisVoice::setSpectralState` — and MUST NOT call `HarmonicCloud::setSpectralTarget` directly
  (C-4, forced by `seraphis_voice.h:989`). The controller's local write to `slotMirror_[slot]` (C-11, Q1)
  happens in the same call and is display-only; it MUST NOT substitute for the `EditMessage` send this FR
  requires. **The push MUST land on a currently-sounding voice, audibly, not deferred to the next note-on
  (D1, FR-033a):** the retry machinery (`pushSpectralStatesIfPending()`, `processor.cpp:2790-2810`) already
  re-pushes to every voice each block until all accept, so no edit is lost; FR-033a is what makes a
  sounding voice accept it on the first push instead of rejecting it.
- **FR-030.** Pan and mask edits MUST be re-applied by the processor after every event that clears them.
  The processor MUST own a `partialOverrides_` table (64 positions + 64 mask bits + a per-entry
  "is-overridden" flag) and MUST re-push it after: a `kCloudStereoSpreadId` (207) change
  (`harmonic_cloud.h:545`), a `kSeedId` (3) change (`:703`), any engine `reset()` / `silence()`
  (`:331-332`), and a polyphony increase that brings a previously-unwritten slot into use. Without this the
  user's pan edit silently vanishes the next time they touch the spread knob. This in-memory re-push is
  distinct from, and does not substitute for, the on-disk persistence FR-034a requires: this FR is what
  survives everything short of a project reload; the `[partials]` block is what survives that.
- **FR-031.** `Krate::DSP::setPartial`, `blendStates` and `tiltState` MUST be added to
  `dsp/include/krate/dsp/processors/spectral_state.h` as **free functions** with exactly the contracts of
  C-6, adding no include and remaining Layer 2 (`:17-19`).
- **FR-032.** Over the adversarial input space — out-of-range ratios, non-monotone ratios, amplitudes
  outside `[0,1]`, `numPartials` outside `[0,64]`, out-of-range indices, and non-finite arguments **built
  from bit patterns** per the `-ffast-math` rule (`:21-23`; `tests/test_helpers` precedent) — each mutator
  MUST satisfy **both** halves of C-6's preservation contract:
  1. **Preservation.** If the input state satisfies `isValidSpectralState` (`:82`), the post-call state
     MUST satisfy it too. `blendStates` MUST return a valid state **unconditionally**, by its own
     select-or-default rule (C-6).
  2. **No half-writes.** If the input state does **not** satisfy `isValidSpectralState`, `setPartial` and
     `tiltState` MUST leave it **byte-unchanged** (`std::memcmp` against a pre-call copy returns 0).
     Neither may repair, re-sort, re-count or partially rewrite it.

  This is Phase 3's inherited criterion (roadmap lines 528–533), stated as the conditional it has to be:
  demanding validity of the output for an invalid input would demand a repair function this spec
  deliberately does not ship (C-6). SC-012 measures exactly these two clauses.
- **FR-033.** `SeraphisVoice` MUST gain `setPartialPosition`, `setPartialMask` and `clearPartialMask`
  pass-throughs, and `SeraphisEngine` MUST gain `setPartialPositionAllVoices`, `setPartialMaskAllVoices`
  and `clearPartialMaskAllVoices` fanning out over `[0, kMaxVoices)`. Together with FR-031, FR-033a and
  FR-037, these are the **only** `dsp/` changes this phase ships; the enumerated set is closed and
  *Non-goals* states it.
- **FR-033a.** `SeraphisVoice::setSpectralState`/`setSpectralStateCount` MUST accept a state push while the
  voice is sounding — i.e. MUST NOT reject it through `isConfigurable()` (`seraphis_voice.h:770-776`,
  `:908`) — so that a ratio/amplitude edit lands on, and audibly changes, a currently-sounding voice (D1).
  Every other call `isConfigurable()` gates (construction-time seeding, freeze/steal paths) MUST remain
  gated exactly as it is today; this relaxation is scoped to these two calls only. The push MUST continue
  to reach `SpectralMorphEngine::setState`/`setStateCount` unchanged, reusing the existing FR-047 absorption
  crossfade (`spectral_morph_engine.h:311`, `slotContributes` at `:558`) rather than a new fade mechanism.
  Phase 3's `spectral_morph_engine.h:198-206` class comment MUST be corrected in the same change to name
  only `prepare()`, `reset()` and `setSeed()` as calls not to make while sounding — a comment-only edit; no
  behaviour of `setState`/`setStateCount` itself changes. **Criteria: SC-028, SC-029, SC-030.**
- **FR-034.** An edited slot MUST persist across save/load with **no state-format change**: `getState`
  already writes the full 541-byte payload per slot (Phase 9 C-8;
  `specs/seraphis-phase9-parameters/spec.md:853-856`), and `kCurrentStateVersion` MUST remain **3**
  (`plugin_ids.h:27`).
- **FR-034a.** An edited pan/mask table MUST persist across save/load (Q3): `getState`/`setState` MUST
  append a `[partials]` block — **exactly 272 bytes** — **last** in the stream, with
  `kCurrentStateVersion` staying **3**. The layout is fixed:

  | Offset | Size | Field |
  |---|---|---|
  | 0 | 256 | 64 × `float` pan position, index order |
  | 256 | 8 | `std::uint64_t panOverrideBits` |
  | 264 | 8 | `std::uint64_t maskBits` |

  i.e. `64 × sizeof(float)` = **256 B** plus **two 64-bit masks** = **16 B**, total **272 B**. Every field
  is a **stored value, never an arithmetic result**, which is what carries Phase 9's FR-094 byte-identity
  through unchanged. `maskBits` is the same quantity `CloudFrame` publishes (C-2); `panOverrideBits` is the
  pan half of `CloudFrame::overriddenBits`, which is the union of the pan and mask override flags — so the
  stream stores the two override sources separately and the frame publishes their union, and neither side
  re-derives the other's layout.
  **Each mask moves as ONE 64-bit field, and the rationale an earlier draft gave for splitting it is
  withdrawn.** That draft claimed `IBStreamer` has no 64-bit integer accessor; it **does** — `IBStreamer`
  publicly inherits `FStreamer` (`extern/vst3sdk/base/source/fstreamer.h:202`), whose public
  `writeInt64u`/`readInt64u` are declared at `fstreamer.h:103-104` inside the `int64` block at `:97-106`,
  and the sibling signed pair `writeInt64`/`readInt64` is already used in this repo at
  `plugins/disrumpo/src/processor/processor_state.cpp:356` and `:908`. Splitting each mask into four
  `int32`s would be unmotivated work: **two 64-bit masks are 16 bytes either way**, so the split changed
  no arithmetic and the whole-field form is taken simply because it reads back without a reassembly step.
  The existing EOF-safe strict-prefix loader chain MUST remain the only version-detection mechanism; no
  version-aware branch is added. A stream truncated immediately before the block (as a pre-Phase-11 build
  would read it) MUST still load successfully, with every override reported absent.
- **FR-035.** Moving a slot's dropdown (409–412) MUST reload that slot from the factory table and
  **discard** the slot's edits — the existing `lastPushedSlotStateId_`-guarded path
  (`processor.cpp:1383-1385`, `processor.h:886`) is left exactly as it is. This is documented user-visible
  behaviour, not a silent loss: it is the only reading under which the dropdown keeps meaning what its
  label says, and a dropdown that stopped applying would be worse. The same message MUST re-seed
  `slotMirror_[slot]` (C-11, Q1) from the factory table, so the controller's mirror does not diverge from
  the processor's discard.
- **FR-036.** `Processor::notify` MUST validate every `EditMessage` field per C-5 clause 5 and drop
  malformed messages silently, without touching the staging ring. This includes kind 7 (`BlendBegin`, Q2)
  and a kind 4 with no live kind-7 snapshot for the current gesture.

### E2. Macro reach into the effects surface (Phase 10 RQ-4)

- **FR-037.** `SeraphisMacroMatrix` MUST gain, and MUST gain nothing beyond, C-10 clause 2's additions:
  `SeraphisMacroTargetOwner::Effects`; the two target values `FxDelaySend` and `FxWanderDepth` appended
  **after the Aether block, immediately before `Count`**; `struct SeraphisEffectsTargets` with its fields
  in enum order; `computeEffectsTargets()`; C-10 clause 1's two `kRows` entries with `.base = 0.0f` and
  `ModCurve::Linear`; and C-10 clause 3's extended guards. No existing row, base, amount, curve, target
  index or guard may change, and `apply(SeraphisEngine&)` MUST gain no line.
- **FR-038.** The processor MUST compose the two effects targets through the same seam the other 91 deep
  parameters use: `setTargetBase(FxDelaySend, ·)` / `setTargetBase(FxWanderDepth, ·)`
  (`seraphis_macro_matrix.h:708`) on a deep-parameter change, and the value pushed into `SpectralDelay`'s
  mix and `BrownianDrift`'s depth read from `computeEffectsTargets()`, recomputed **once per `process()`
  call**, before the slice loop, and exposed for test as `composedFxDelaySendForTest()` /
  `composedFxWanderDepthForTest()` / `composedEffectsRecomputeCountForTest()`. **The mechanism is
  substituted reads, not re-pointed guards (D2):** `effectsPushes_` (`processor.cpp:1821`) is incremented
  only inside `pushEffectsParams()`, whose ID set does not include 1410/1441 — both are class-(b) smoothed
  (`:3051`, `:3052`, bypass predicate at `:2351`) — so re-pointing that counter's guard at the composed
  value is not the mechanism; instead, `updateEffectsBypassState`'s read at `:2351` and
  `updateParamSmootherTargets`'s read at `:3052` are substituted to read `composedEffects_` (clamped to
  `[0,1]`) in place of the raw deep atomic, so a macro move with a still deep knob is no longer silently
  inaudible, which is the exact failure RQ-4 exists to prevent.
- **FR-039.** At every macro's FR-060 neutral (`seraphis_macro_matrix.h:548-550`) the composed value MUST
  equal the deep value **bit-for-bit**, so the Phase 10 send-stage skip (its FR-010) is taken on exactly
  the same blocks and SC-001 keeps its exact-equality form. No headroom rescaling is applied — the
  `setTargetBase` banner's standing rule (`:703-707`).

### E3. Controller-side mirror and multi-editor message discipline (Q1, Q7, Q8)

- **FR-046.** The controller MUST own `std::array<SpectralState, 4> slotMirror_` (C-11, Q1), re-seeded
  from `makeFactoryState()` on every 409–412 dropdown change and from the four 541-byte payloads a loaded
  state stream carries. `morph_params.h`'s `loadMorphParamsToController` MUST stop discarding those
  payloads into its scratch buffer — this is the only change this phase makes to that file. Every
  authoring mutator MUST be applied to the mirror in addition to being sent via `EditMessage`; the mirror
  MUST NOT be read back from the processor to reconcile the two, and MUST NOT be serialized or reach
  `process()`.
- **FR-047.** The controller MUST gate the C-5 kind-0 editor-open message on a refcounted transition, not
  on a per-view basis (Q7): increment on `didOpen`, decrement on `willClose`/`IPlugView::removed`, send
  `kind 0, a = 1` only on the 0 → 1 transition and `kind 0, a = 0` only on the 1 → 0 transition, and reset
  the count to 0 in `terminate()`. The processor's own gate MUST remain the single
  `std::atomic<bool>` C-2 clause 6 describes (D-6) — one gate, written from the message thread and read
  from the audio thread, never a plain `bool` and never a per-view count on the processor side.
- **FR-048.** The controller MUST throttle `EditMessage` sends for a drag/slider gesture to **at most one
  message per 33 ms** (C-8's 30 Hz rate) **and** MUST send exactly one additional, unthrottled message on
  gesture-end (mouse-up) carrying the gesture's final value (Q8) — a throttle with no terminal flush MUST
  NOT ship, because it can silently drop up to 33 ms of the value the user released the drag at.

### F. Lifecycle, RT safety and cross-cutting

- **FR-040.** No Phase 11 code may allocate, lock, throw or perform I/O on the audio thread. The snapshot
  is the only audio-thread addition and FR-015 bounds it.
- **FR-041.** All raw view pointers cached by the controller MUST be zeroed in `willClose()`; all view-owned
  timers MUST be cancelled in `removed()`; the sub-controller instance counter MUST be reset in
  `willClose()` so a reopened editor does not inherit stale indices (C-7c;
  `.claude/skills/vst-guide/UI-COMPONENTS.md:370`). The editor-open refcount FR-047 adds is a **separate**
  counter, reset in `terminate()`, not `willClose()` (Q7) — `willClose()` fires once per closing view,
  `terminate()` once per plugin instance, and the refcount must survive the former to do its job.
- **FR-042.** The plugin MUST behave correctly with **no editor ever opened** — the C-2 clause 6 gate stays
  false and no frame is published. With the gate closed and every macro at its FR-060 neutral, `process()`
  produces the same samples as the Phase 10 path. The additions on that path are C-9's enumeration in full
  (D-9 row 9c), and with no editor ever opened they reduce to three: the clause-6 gate load; the
  `partialOverridesPending_.exchange(false, std::memory_order_acquire)` (which always returns `false`,
  because nothing can have staged an override with no editor to author one, so the deferred fan-out it
  guards never runs); and C-10's composition, which FR-039 requires to be a bit-exact identity at the
  neutrals. None of the three writes a sample. SC-001 measures the gate half; SC-021(b) measures the
  composition half.
- **FR-043.** Sample-rate and block-size changes MUST NOT invalidate any Phase 11 state: `CloudFrame`
  carries no rate-dependent quantity except `frequencyHz`, which is already absolute Hz, and the view's
  axis span is a fixed constant, not a Nyquist fraction. In particular `partialOverrides_` MUST survive a
  `setupProcessing` re-entry and be re-pushed after prepare (FR-030), because prepare reaches
  `cloud_.reset()` paths that clear both tables (`harmonic_cloud.h:331-332`). **Criterion: SC-014 arm 5.**
- **FR-044.** `node tools/check-portability.js` MUST be clean, `tools/lint-layers.js` MUST show no upward
  include, and clang-tidy `-Target seraphis` MUST be clean — all three before commit
  (root `CLAUDE.md`, *Workflow Requirements*).
- **FR-045.** `Controller` MUST override `IController::createSubController` and return a
  `UI::SeraphisEditSubController` (a `VSTGUI::DelegationController` subclass) for the uidesc's
  `sub-controller="SeraphisEdit"` container, and **every tag-less control in C-7b's table MUST have that
  object as its `IControlListener`** — no tag-less control's `valueChanged` may live on `Controller`, on a
  `CView` subclass, or nowhere. This is the roadmap's *"standard sub-controller treatment"* (roadmap line
  484) in the mechanism the cited skill defines, not a paraphrase of it.

### G. Build, registration and metadata

- **FR-050.** Every new plugin `.cpp` MUST be added to `plugins/seraphis/CMakeLists.txt`'s
  `smtg_add_vst3plugin` source list (`:17-46`) **and** to `plugins/seraphis/tests/CMakeLists.txt`'s
  enumerated list beside the second compilation of `processor.cpp`/`controller.cpp` (`:36-38`). The list is
  **not globbed** (`:16-18`) — an omitted TU drops out silently.
- **FR-051.** Every new test `.cpp` MUST be added to the same enumerated test list, for the same reason
  (`tests/CMakeLists.txt:29-32`).
- **FR-052.** `src/entry.cpp` MUST include the view-creator headers so the inline creator objects
  (`arc_knob.h:716`) are in a linked TU (C-7) — the Ruinae shape, whose entry file includes fourteen
  `ui/*.h` headers under the comment *"Shared UI controls - include triggers static ViewCreator
  registration"* (`plugins/ruinae/src/entry.cpp:18-32`). **The standing prohibition in Seraphis's own
  entry file MUST be rewritten in the same change:** `plugins/seraphis/src/entry.cpp:12-14` currently
  reads *"this file MUST NOT include any ui/\*.h header … Seraphis registers none (FR-018, FR-056 - no
  custom views until Phase 11)"*. Leaving that banner in place beside the new includes would leave the
  file contradicting itself for the next reader.
- **FR-053.** `plugins/seraphis/CLAUDE.md` MUST be updated: `ui/` is no longer *"empty until Phase 11"*, and
  the custom-view roster and the cloud-frame data path are recorded there — the leaf file is where a later
  agent working under `plugins/seraphis/` will actually read it.
- **FR-054.** `plugins/seraphis/CHANGELOG.md` MUST gain the entry (`tools/check-changelog-coverage.js`
  gates it).

---

## Success Criteria

**Measurement protocol for every CPU criterion below** is Phase 10's single authoritative one: fresh boot,
idle machine, **seven** consecutive runs, best-of-16 per estimate, **worst reported**
(`plugins/seraphis/tests/integration/param_perf_test.cpp:133-156`; the six-run shape is struck and no
criterion here cites it). CPU criteria carry Catch2's `[.perf]` tag and are outside the CI gate, as the
existing perf arms already are.

- **SC-001 — The producer changes no sample. One build, one process, one `Processor` instance.** Two
  renders of an identical 10-second MIDI script at the 8-voice operating point, every parameter at its
  shipped default, on the **same instance**: arm **A** with the C-2 clause 6 cloud-frame gate forced
  **open** via `setCloudFrameGateForTest(true)` (producer active, snapshot loop running, frames being
  published), arm **B** with it closed. The two are **sample-identical**: `max |a[i] − b[i]| == 0.0f` over
  every sample of both channels.
  **Why exact equality is legitimate here and is not a golden** — the same reasoning Phase 10's SC-002
  carries verbatim (`specs/seraphis-phase10-effects/spec.md:1318-1324`): both arms are the *same compiled
  code path on the same instance*, so identical codegen is guaranteed and the only question asked is
  whether the producer is the read-only observer C-2 claims. It is **not** a cross-build comparison. A
  Phase-10-build-versus-Phase-11-build sample-exact comparison is explicitly **forbidden** — it would
  demand bit-identical floating point across MSVC/GCC/AppleClang, which
  `tests/test_helpers/render_fingerprint.h:20-30` measures at 2.9e-5 per sample and roadmap line 598
  rules out — and no criterion in this spec asks for one.
  *Both arms must differ.* A test in which A and B take the same gate value is a tautology; the test
  therefore also asserts `cloudFramePublishAttemptCountForTest() > 0` in arm A and `== 0` in arm B before
  comparing the buffers, so a broken seam fails loudly instead of passing vacuously.
  *Test:* `Seraphis_Phase11_OpenGate_ChangesNoSample` (`tests/integration/ui_negative_control_test.cpp`).
- **SC-002 — The surface is still 107, and the binding count is exactly 110.**
  `controller.getParameterCount() == 107`; the `<control-tag>` set and the registered ID set are equal both
  ways; `unreachableParams(xml, ids, {})` returns **empty with an empty allowlist**; and
  `extractBoundViews(xml)` yields **exactly 110** entries (up from the placeholder's 8), of which the
  multiset of bound IDs contains `{1008, 1204, 1430}` **exactly twice each** and every other registered ID
  **exactly once** — asserted as an enumerated allowlist, so an accidental duplicate anywhere else is red
  (FR-003). *Test:* extended `parameter_surface_test.cpp` (`:508`, `:700-714`; the current
  `CHECK(bound.size() == 8u)` at `:714` becomes `== 110u`).
- **SC-003 — Every bound view's class matches its parameter's registered kind.** The per-view assertion at
  `parameter_surface_test.cpp:731` passes for all bound views, with `MacroRingKnob` enumerated as the
  permitted class for IDs 100–104 and no other exception. *Test:* same file.
- **SC-004 — All three custom views are actually instantiated from the uidesc, in the right places.**
  **Arm 1 — instantiation.** After `exerciseEditorLifecycle`, the built view tree contains exactly one
  `CloudView`, exactly one `DrawerContainer` and exactly **five** `MacroRingKnob` instances, identified by
  `dynamic_cast`, not by view count. This is the arm that would have caught the Phase 8 banner's hazard
  (`editor.uidesc:3-5`) — a creator TU that failed to link silently yields stock views.
  **Arm 2 — the seven tab titles, named and ordered (FR-022).** Walk the `DrawerContainer`'s tab buttons in
  child order and assert the title list is **exactly** `{"Cloud", "Morph", "Body", "Atmos", "Aether", "FX",
  "Life/Env"}` — string-equal, same order, size 7. FR-022 fixes both the names and the order and nothing
  else asserts either, so without this arm a build shipping seven tabs in the wrong order, or with a
  mislabelled tab, passes every other criterion. A `static_assert` on `kTabCount == 7` is **not** a
  substitute: it counts, it does not name.
  **Arm 3 — three one-line assertions on the same built tree, for three FRs that had no criterion at all.**
  (i) **FR-006 — the cloud view is the FIRST child, so the rings draw over it.** Arm 1 counts instances and
  SC-020 checks rects; **neither sees child order**, so a build that put the cloud view last — hiding every
  ring behind it — passed everything. Assert `dynamic_cast<UI::CloudView*>(root->getView(0)) != nullptr`:
  child **index 0** of the template root.
  (ii) **FR-025 — exactly one tab's page is visible at a time.** Walk the `DrawerContainer`'s seven page
  containers and assert exactly one reports visible; then `setActiveTab(i)` for each `i` and re-assert it is
  still exactly one **and** that it is page `i`.
  (iii) **FR-027 — Observe is the default on every editor open.** Immediately after `didOpen`, the cloud
  view's mode is `Observe` — asserted on **every** cycle of the lifecycle loop, not only the first, since
  the failure mode is a mode that survives a close.
  *Test:* `Seraphis_Phase11_CustomViews_AreInstantiated`
  (`tests/unit/controller/custom_view_test.cpp`).
- **SC-005 — Editor lifecycle is clean over 10 cycles with the full layout.** `exerciseEditorLifecycle(…,
  cycles=10)` passes, with `getParameterCount() == 107` before and after, and — under a
  `-DENABLE_ASAN=ON` Debug build and under the valgrind-nightly `[lifecycle]` lane — **zero** reports.
  *Test:* extended `editor_lifecycle_test.cpp:235-262`.
- **SC-006 — The cloud frame reports what the DSP actually holds.** For a rendered note, every
  `CloudFrame::frequencyHz[i]` equals `getPartialFrequencyHz(i) * getPartialDriftDetune(i)` and every
  `amplitude[i]` equals `getPartialCurrentAmplitude(i) * getPartialAntiAliasGain(i)` at the publish
  instant, within `1e-6` relative; `partialCount == getActivePartialCount()`; and entries at
  `i >= partialCount` are exactly `0.0f`.
  **(e) `maskBits`/`overriddenBits` mirror `partialOverrides_` exactly (Q5).** After masking partials
  `{3, 17}` and pan-overriding partial `9`, the published frame's `maskBits` has exactly bits 3 and 17 set,
  and `overriddenBits` has exactly bits 3, 9 and 17 set — bit-for-bit against the processor's table, at the
  publish instant.
  **(f) `morphTravelPosition` mirrors the morph engine's journey position (Q4).** With
  `kMorphPositionId` swept over `{0, 0.25, 0.5, 0.75, 1.0}`, the published `morphTravelPosition` tracks the
  engine's own current position within `1e-6` relative at each point.
  **Every arm above reads the producer's own frame at `lastPublishedFrameForTest()`, between `process()`
  calls — never through the DataExchange queue**, which a headless run may never fill (C-2 clause 7, D-9
  row 9e).
  **(g) The focus rule is all three of its clauses, not "slot 0" (FR-014).** This is the **only** criterion
  for FR-014; before it, a build whose focus rule always returned slot 0 passed every criterion in this
  spec. (1) Three overlapping notes: after each note-on, `focusVoice` equals the slot with the **greatest**
  `getVoiceAllocationSerial` among non-idle slots (`seraphis_engine.h:975`; ties are impossible, `:966-974`)
  — clause (a). (2) Release the newest note only: while `getVoiceLevel(previous) > kCloudFrameSilenceLevel`
  the focus slot is **retained**, asserted to hold for at least one published frame after note-off **and to
  be the released slot**, not the next-highest serial — clause (b), the release-retention arm. (3) All
  voices silent ⇒ `focusVoice == 0` — clause (c). (4) At `kPolyphonyId = 1` (the *Edge cases* degeneration)
  every arm above still terminates and `focusVoice` is always 0.
  *Test:* `Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial` (same file).
  **(h) The controller caches only the most recent block (FR-016).** Arms (a)–(g) are all producer-side and
  SC-020 writes the frame cache directly, so the **receiver** had no criterion at all. Call
  `Controller::onDataExchangeBlocksReceived` directly with one delivery of **three** blocks whose
  `sequence` values increase, and assert the cached frame's `sequence` equals the **last** one — the
  "most recent wins" rule (`plugins/membrum/src/controller/controller.cpp:1719-1726`). Then call
  `queueOpened` with its `TBool` seeded **`true`** and assert it comes back **`false`**
  (`dispatchOnBackgroundThread = false`, FR-016). Both are pure function calls: no processor, no host.
  *Test:* `Seraphis_Controller_CachesOnlyTheMostRecentBlock`
  (`tests/unit/controller/custom_view_test.cpp`).
  **(i) The handler follows the connection and the activation (FR-011).** FR-011 had no criterion anywhere:
  it requires the handler to be created in `connect()`, destroyed in `disconnect()` and `onActivate` /
  `onDeactivate` driven from `setActive`, yet arms (a)–(h) all pass with the handler permanently null
  (D-9 row 9e), so a build that never called `onDeactivate()` on `setActive(false)`, or that leaked the
  handler across `disconnect()`, was invisible. **This is the one case that builds a peer
  `IConnectionPoint`** — a minimal test double **local to that TU**, never added to `ProcessorFixture`.
  `connect(peer)` ⇒ `dataExchangeHandlerLiveForTest()` is **true**; `setActive(true) → setActive(false) →
  setActive(true)` leaves it true and does not double-open (the SDK's `openQueue` is idempotent per
  activation); `disconnect(peer)` ⇒ the seam returns **false** **and**
  `cloudFrameSkippedBlockCountForTest()` resumes rising on every gated publish, proving the transport was
  **released** rather than merely idled.
  *Test:* `Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation` (same file).
  *Test (arms a–f):* `Seraphis_CloudFrame_MirrorsCloudAccessors` (`tests/integration/cloud_frame_test.cpp`).
- **SC-007 — One publish attempt per `process()` call that reached the slice loop, never per slice.** Over
  a 60-second render containing MIDI events on non-block boundaries and automation that forces the
  64-sample subdivision, with the gate open:
  `cloudFramePublishAttemptCountForTest() == ` **the number of `process()` calls that reached the slice
  loop**, **exactly**; and `renderSliceCountForTest() > cloudFramePublishAttemptCountForTest()`, strictly.
  **The divisor is NOT the raw host call count, and the difference is not pedantry (D-9 row 9f).**
  `Processor::process()` has six pre-slice-loop early returns — no outputs, null `channelBuffers32`,
  `numChannels < 2`, `numSamples <= 0`, null L/R, not prepared (`processor.cpp:978`, `:981`, `:988`,
  `:992`, `:997`, `:1002-1008`) — and `publishCloudFrame()` sits **after** the loop, so on any of those
  calls no attempt is recorded and none should be. A controlled render never hits them; **pluginval at
  strictness 5 and real hosts do**, so an equality stated against the host call count is simply false as an
  invariant about a *correct* build. The divisor is `effectsStageProcessCalls_`'s accessor — incremented at
  `processor.cpp:1189`, which already carries exactly the "reached the slice loop" meaning — and **no
  seventh counter is added** for it.
  The counter is an **attempt** counter by C-2 clause 7 — incremented after the slice loop whenever the
  gate is true, **regardless of what `getCurrentOrNewBlock()` returns and regardless of whether a queue
  exists** (clause 7, D-9 row 9e). That
  distinction is load-bearing, not pedantry: `numBlocks = 4` drained at 30 Hz against a ≈ 94 Hz fill rate
  exhausts in ~64 ms by design (C-8), and a headless run with no host DataExchange support may land zero
  blocks — a success counter would read 0 and make this criterion unsatisfiable for a correct
  implementation. The test additionally **records** `cloudFrameSkippedBlockCountForTest()` in the Catch2
  output so queue exhaustion is observed rather than folded into the cadence number, and asserts nothing
  about it. *Test:* same file, using both seams beside Phase 9/10's `*ForTest()` accessors
  (`processor.h:162-240`).
- **SC-008 — Frames are deterministic under a fixed seed, on one build.** Two runs of the same seeded MIDI
  script, **in the same process on the same build**, produce frame sequences that agree on all four of the
  following, each computed over the whole sequence and compared **relatively**:

  | Quantity | Definition | Bound |
  |---|---|---|
  | mean pitch | mean over frames of `Σᵢ aᵢ·log2(fᵢ) / Σᵢ aᵢ`, `i < partialCount` | `1e-5` relative |
  | pitch total variation | `Σ_frames |mean pitch(n) − mean pitch(n−1)|` | `1e-5` relative |
  | mean amplitude | mean over frames of `Σᵢ aᵢ / partialCount` | `1e-5` relative |
  | mean position | mean over frames of `Σᵢ positionᵢ / partialCount` | `1e-5` relative |

  plus `sequence` strictly increasing and `partialCount` equal frame-for-frame.
  **`render_fingerprint.h`'s constants are deliberately NOT used here.** `fingerprintRender` compares a
  `std::span<const float>` of *audio samples* (`:66`) and `kSampleTolerance = 1.0e-4f` is an **absolute**
  per-checkpoint bound calibrated against a signal peak of 2.17 (`:25-29`). Pointed at `frequencyHz`,
  which is absolute Hz, 1e-4 would be 2.5e-8 relative on a 4 kHz partial — below float epsilon (~1.2e-7),
  i.e. a bound no correct implementation can meet. The four relative bounds above are the substitute; if a
  pilot run measures a spread above `1e-5` on any of them, **the measured number is recorded in this spec
  and the criterion re-stated with it** — the bound is derived, never relaxed to fit a failing run.
  **No bit-exact float golden**, and **no cross-toolchain claim**: this criterion is same-build
  determinism only, which is the only form defensible under `dsp/CLAUDE.md`'s *Never pin a render with a
  bit-exact digest over float samples* and Phase 10 SC-002's reasoning. *Test:*
  `Seraphis_CloudFrame_IsDeterministic`.
- **SC-009 — The producer fits the measured headroom.** (a) With the editor gate **open** at the 8-voice
  operating point, the full-poly gate still passes: worst-of-seven `≤ kFullPolyCeilingNs = 2 666 666.7` ns
  (`param_perf_test.cpp:392`), against Phase 10's pinned worst of 2 380 980 ns (22.32 %) — i.e. the
  producer must consume **less than the 2.68 remaining points**. (b) The snapshot stage alone, measured on
  its own scoped timer divided by the `process()`-call count, is **≤ 0.10 % of one core = 10 666 ns per
  512-sample block** — the same shape and figure as Phase 10's `kDefaultsBudgetNs`. **The ceiling is not a
  lever**: if (a) fails, the producer is made cheaper. *Test:*
  `Seraphis_CloudFrame_CpuBudget` + a re-run of `Seraphis_FullPoly_CpuBudget_WithFullSurface` with the gate
  open, both `[.perf]`.

  > **ARM (a) RESTATED (2026-08-04, phase-owner ruling "Hybrid", OE-1).** Arm (a)'s wording above is
  > **superseded and kept for the audit trail**; arm (b) is untouched and still gates. Arm (a) is now a
  > **differential**: the producer's **marginal** whole-`process()` cost, measured as
  > `gate OPEN − gate CLOSED` on **one fixture inside one interleaved, order-counterbalanced trial loop**,
  > must be **≤ 10 666.7 ns/block (0.10 point of one core)** — premise 6's stage budget, i.e. the producer
  > may cost no more inside whole-`process()` than arm (b) already requires the stage to cost on its own
  > timer. Why the absolute form was withdrawn: OE-1 fact 3 establishes that ~9.2 of the subject's points
  > are inherited Phase 8–10 plumbing, and fact 1 that the producer's own cost is below the noise floor.
  > The absolute figure is still **measured and reported** against the 25 % ceiling on every run; that
  > ceiling is now **Phase 11.5's** gate (roadmap). The pre-declared remedy is unchanged: if the delta
  > fails, the producer is made cheaper — never the bound.
- **SC-010 — The producer costs nothing when the editor is closed.** With the gate false, over a 60-second
  render: (a) `cloudFramePublishAttemptCountForTest() == 0`; and (b) the **whole-`process()`**
  best-of-16 ns/block at the 8-voice operating point is `≤ kRegressionFactor × kBaselineFullPolyNs`
  = `1.15 × 2 318 840 ns` (`param_perf_test.cpp:395`, `:472`), i.e. the closed-gate path has not regressed
  against Phase 10's pinned baseline at all.
  Arm (b) deliberately replaces the earlier "measured snapshot-stage nanoseconds are 0" wording: with the
  instrumented scope inside the gate, that number is zero **by construction** regardless of what the
  producer costs, so it measured the instrumentation's placement rather than the plugin. `[.perf]`.
  *Test:* same file.

  > **ARM (b) RESTATED (2026-08-04, phase-owner ruling "Hybrid", OE-1).** Arm (b)'s ceiling above is
  > **superseded and kept for the audit trail**; **arm (a) is UNCHANGED and still gates hard**. The
  > `1.15 × 2 318 840 ns` ceiling compares a **whole-`process()`** measurement against a **chain-only**
  > baseline and is unsatisfiable by construction on any machine (OE-1's central finding). Arm (b) is now
  > stated against a **whole-`process()`** baseline — the same subject it measures —
  > `kBaselineWholeProcessNs` in `ui_perf_test.cpp`, with `kRegressionFactor` unchanged at `1.15`. That
  > baseline is **PROVISIONAL at 3 385 600 ns/block (31.74 % of one core)**, transcribed from the worst
  > whole-`process()` figure in OE-1's **two**-pass cold table, because the seven-run fresh-boot idle
  > dataset the protocol requires does not exist for this subject yet. While
  > `constexpr bool kSc010BaselinePinned = false`, the absolute comparison **reports and does not gate** —
  > the `param_perf_test.cpp:2156-2175` mechanism, under a loud
  > *"PROVISIONAL — pin from 7-run fresh-boot cold set before release"* banner. **This arm is not
  > releasable in this state:** flipping the flag to `true` after recording the seven-run set is a release
  > gate, not an optional tidy-up.
- **SC-011 — The snapshot allocates nothing and throws nothing.** Over a 60-second render (5 625 blocks ×
  512 samples) with the gate open, inside `TestHelpers::AllocationScope`: `allocations == 0`,
  `exceptions == 0` counted through a real `try/catch(...)`, and a source scan reporting zero lock
  primitives and zero throw sites — Phase 10's instrument (`effects_perf_test.cpp:683-759`, `:872-879`)
  re-pointed, **with both of its anti-vacuity guards carried over, not dropped**:
  - **corpus:** exactly `{ src/processor/processor.cpp, src/processor/processor.h,
    src/processor/cloud_frame.h }` — the three audio-thread-reachable Phase 11 files. (`spectral_state.h`
    is *not* scanned: the three new free functions are message-thread-only by C-5, and adding it would
    scan ~600 lines of unrelated Phase 3 code.)
  - `scan.filesMissing == 0` and `scan.codeBytes > 0`, so a mistyped path fails instead of reporting a
    clean empty corpus (`effects_perf_test.cpp:688-691`);
  - a **witness** count `> 0` for the token `publishCloudFrame`, this phase's own audio-thread entry
    point — the same role `runSendStage` plays for Phase 10, whose comment states it exactly: *"It is the
    WITNESS that the corpus is the intended one: a scan pointed at six readable but wrong files would
    clear both token counts and this would be 0"* (`:692-695`).

  **Arm 2 — every Phase 11 atomic is actually lock-free, asserted at runtime.**
  `CHECK(processor.phase11AtomicsAreLockFreeForTest())`, a seam that ANDs `is_lock_free()` over
  `cloudFrameEnabled_`, `partialMaskBits_`, `partialPanOverrideBits_`, `partialOverridesPending_` and
  `partialPanStaging_[0]`. The constitution's rule is that **only `std::atomic_flag` is guaranteed
  lock-free**, so
  D-6's and 9b's "lock-free on x86-64 and arm64" claim is checked rather than assumed. A locking atomic on
  the audio thread is an RT violation, and this is the only arm that would find it — the allocation counter
  above does not see a futex.

  **Arm 3 — no platform API in any Phase 11 source file (FR-005).** FR-005 was mapped only to SC-019 —
  builds, portability, clang-tidy — and **a platform-guarded native popup compiles clean on all three
  legs**, so nothing detected it. Reuse this criterion's own source-scan instrument with corpus
  `src/ui/*.{h,cpp}` **plus** `src/processor/processor.{h,cpp}` and `src/controller/controller.{h,cpp}`,
  and a **forbidden-token** list: `windows.h`, `HWND`, `CreateWindow`, `MessageBox`, `NSView`, `NSWindow`,
  `NSAlert`, `#import`, `gtk_`, `XCreateWindow`. **Zero hits required.** Both anti-vacuity guards are
  carried again — `scan.filesMissing == 0`, `scan.codeBytes > 0`, and a **witness** count `> 0` for a token
  that must be present (`VSTGUI::`) — so a scan that found no files is red, not green.
  *Test:* `Seraphis_CloudFrame_AllocatesNothing` (arms 1–2) and `Seraphis_Phase11_UsesNoPlatformApi`
  (arm 3), both in the perf/scan TU.
- **SC-012 — The mutators preserve `isValidSpectralState` over an adversarial table. (INHERITED FROM
  PHASE 3, roadmap lines 528–533.)** For a table of at least 40 rows covering: ratios below
  `kMinStateRatio` and above `kMaxStateRatio`; non-monotone ratios (equal neighbours, descending
  neighbours, neighbours closer than `kAuthorSpacing²`); amplitudes `< 0`, `> 1`, exactly `0`, exactly `1`;
  `numPartials` of `-1`, `0`, `1`, `64`, `65`, `INT_MAX`; indices `0`, `numPartials-1`, `numPartials`,
  `63`, `64`, `SIZE_MAX`; `t` of `-1`, `0`, `0.5`, `1`, `2`; `dB` at both clamp edges and beyond; and
  **non-finite arguments built from bit patterns** (never `std::numeric_limits<float>::quiet_NaN()`, per
  `tests/test_helpers` and `spectral_state.h:21-23`) — **each row is asserted against the branch its
  input selects**, per FR-032:

  1. **Input valid ⇒ output valid.** If `isValidSpectralState(before)` is `true`, then after the call
     `isValidSpectralState(after)` is `true`.
  2. **Input invalid ⇒ output byte-unchanged.** If `isValidSpectralState(before)` is `false`, then for
     `setPartial` and `tiltState` `std::memcmp(&before, &after, sizeof(SpectralState)) == 0`.
     `SpectralState` is trivially copyable (`spectral_state.h:65`), so `memcmp` is well defined here.
  3. **`blendStates` is unconditional.** Its return satisfies `isValidSpectralState` for **every** row,
     valid inputs or not, because C-6 makes it select a valid input or return the documented-valid default
     (`:42-43`).

  **Why the criterion is conditional and not universal.** C-6 defines `setPartial` and `tiltState` as
  *no-ops* on exactly the invalid inputs this table supplies — "No-op if `s.numPartials` is outside
  `[0, 64]`", "No-op on … `!isValidSpectralState(s)`". A no-op on an already-invalid state leaves it
  invalid — `numPartials` outside `[0,64]` returns `false` at `spectral_state.h:83-85`, and
  `ratio <= previousRatio` at `:97-99` — so a universal "output is always valid" reading fails **by
  construction** on a correct implementation, and could be satisfied only by a silent repair function this
  spec deliberately does not ship (C-6). Clause 2 is the sharper assertion in its place: a no-op that
  half-wrote the state is a real defect the universal reading could not see.

  **Acceptance arm.** For every row whose post-call state is valid, `SpectralMorphEngine::setState(0, s)`
  MUST be observed to **accept** it — i.e. not take the `:296-298` rejection branch. The observation is
  `getOutputRatios()` / `getOutputAmplitudes()` compared before and after the call, with the journey parked
  on slot 0 so `slotContributes(0)` is true, because those arrays are the only outward-visible consequence
  of a stored slot. **`getStateCount()` MUST NOT be used**: it returns `numStates_`
  (`spectral_morph_engine.h:443`), which is written only by `setStateCount` (`:318-328`) and never by
  `setState` (`:292-314`) — it is invariant under `setState` and cannot distinguish acceptance from
  rejection. `isStateFadeActive()` (`:449`) MUST NOT be used **alone** either: `setState` returns early
  without arming on an identical state (`:302-305`, *"Identical -- no fade armed, isStateFadeActive()
  untouched"*) and arms only `if (slotContributes(slot))` (`:311`) — both branches this table hits. Rows
  whose post-call state is invalid are **excluded** from this arm; rejection is the documented correct
  outcome for them.
  *Test:* `SpectralState_AuthoringMutators_PreserveValidity`
  (`dsp/tests/unit/processors/spectral_state_test.cpp`, run by `dsp_processors_tests`) for clauses 1–3; the
  acceptance arm lives in `dsp/tests/unit/systems/` because it needs a Layer 3 type.
- **SC-013 — The mutators do what they claim, audibly.**
  **THE DRIFT PRECONDITION, stated in BOTH reachable forms (D-9 row 9i).** Every FFT arm in this spec that
  measures a frequency — SC-013's four arms, SC-017 and SC-028 — runs with `BrownianDrift` contributing no
  per-partial detune. Which form applies depends on where the test lives, and both are normative:
  - **In a `dsp/` TU** (where SC-013's arms live): `HarmonicCloud::setDriftDepthCents(0.0f)`
    (`dsp/include/krate/dsp/systems/harmonic_cloud.h:501`) on the cloud under test. **Plugin `ParamID`s do
    not exist in a `dsp/` translation unit and MUST NOT appear in one** — the earlier wording stated this
    precondition only in `ParamID` terms and was therefore unimplementable in the TU that hosts the
    criterion.
  - **In a plugin TU** (SC-017, SC-028): `kCloudDriftDepthId` (205) held at **0** with every macro at its
    FR-060 neutral — which also pins `kMacroEntropyId` (104), the macro that owns both existing drift
    depths.

  Without this the measured peaks move under drift alone — `CloudFrame::frequencyHz` is drift-inclusive
  **by definition** (C-2 clause 3) and drift runs free (KDD-1, roadmap lines 71–72), so a 2-cent bound
  taken under active drift would be measuring the drift.
  - **(a) `setPartial` moves the partial it names, and only that one.** On a slot loaded with
    `makeFactoryState(SpectralStateId::SineStack)` and the morph journey parked on it,
    `setPartial(s, /*index=*/0, 1.5f, s.amplitudes[0])` moves partial 0's rendered peak by
    **701.955 ± 5 cents** in a 4096-point FFT of a steady-state render, and no other partial's peak moves
    by more than **2 cents**.
    **The index is pinned at 0 deliberately, and the criterion would be unsatisfiable without pinning it.**
    C-6's monotone window caps a new ratio at `ratios[k+1] / kAuthorSpacing = (k + 2) / 1.0163049`, while a
    fifth from an integer harmonic targets `1.5 · (k + 1)`. For `k = 1` that is 3.000 against an edge of
    2.952, and the gap only widens with `k`, so **every `k ≥ 1` clamps** and never moves a fifth (C-6,
    *Consequence*). `k = 0` has room (target 1.5, edge 1.968), and is also the partial whose motion is
    least ambiguous in an FFT.
  - **(b) `blendStates` interpolates.** `blendStates(a, b, 0)` and `(a, b, 1)` render within
    `render_fingerprint.h` tolerance of `a` and `b` respectively — the *audio* comparison the helper is
    calibrated for — and `t = 0.5` lands strictly between them on spectral centroid.
  - **(c) `tiltState` is baked into amplitudes, and is ABSOLUTE.** For each `dB` in
    `{-12, -6, 0, +6, +12}`, applied **to a fresh copy of the same source state** (never accumulated onto
    the previous result), the rendered spectral centroid is **strictly monotonically increasing** in `dB`.
    The fresh-copy protocol is not a convenience: C-6 makes `dbPerOct` absolute, so a sweep that reused one
    state must produce the same five centroids as five fresh applications, and the test asserts that
    equivalence too — `tiltState(s, -6)` twice in a row leaves the render and `s.tiltDbPerOct` **equal to a
    single call**, which is the property that distinguishes the absolute contract from a compounding delta.
    Monotone movement of the *rendered* centroid is what proves the tilt reaches `amplitudes` rather than
    the inaudible field (C-6, `spectral_morph_engine.h:285-289`).
  - **(d) The amplitude arm has a producer.** `setPartial(s, 0, s.ratios[0], 0.25f)` followed by
    `setPartial(s, 0, s.ratios[0], 1.0f)` changes partial 0's rendered peak magnitude **monotonically and
    by at least 6 dB**, while its rendered frequency moves by less than **2 cents**. This is the arm that
    makes `EditMessage::b` and FR-028's modifier-drag gesture live rather than dead.

  *Test:* `SpectralState_AuthoringMutators_AreAudible`.
- **SC-014 — Pan and mask edits survive every clearing event FR-030 names, and a rate change.** After
  `setPartialPositionAllVoices(k, 0.8f)`, each of the following five events leaves partial *k*'s
  `CloudFrame::position[k]` back at `0.8 ± 0.01` on the next published frame, via FR-030's re-push:
  1. a `kCloudStereoSpreadId` (207) change (`harmonic_cloud.h:545`);
  2. a `kSeedId` (3) change (`:703`);
  3. an engine `reset()` (`:331-332`);
  4. **a polyphony increase** — raise `kPolyphonyId` from 1 to 8 and assert a newly allocated voice's
     partial *k* reports `0.8 ± 0.01`, not the default pan. This is FR-030's fourth event and the one
     whose failure mode (a fresh voice silently defaulting) is hardest to notice by ear;
  5. **a sample-rate change** — call `setupProcessing` with a different sample rate and assert the same.
     This is the only criterion for **FR-043**, and the *Edge cases* section names it as a real hazard
     because prepare reaches `cloud_.reset()` paths that clear both tables.

  6. **a macro-ring sweep that moves the composed stereo spread with the deep knob HELD STILL.** Sweep
     `kMacroBloomId` over `{0, 0.25, 0.5, 0.75, 1.0}` with `kCloudStereoSpreadId` (207) untouched, and
     assert `position[k]` is still `0.8 ± 0.01` at every point. This is a *distinct* clearing path from
     event 1, not a restatement of it: Bloom writes `CloudStereoSpread` with `.base = 0.35f,
     .amount = 0.60f` (`seraphis_macro_matrix.h:252-257`) through `macros_.apply()` every slice
     (`processor.cpp:1858` → `:635`), and `setStereoSpread` wipes `positionOverridden_` on any **value**
     change (`harmonic_cloud.h:535-547`) — so a clearing-event tracker keyed on `ParamID` 207 is blind to
     it, passes events 1–5, and silently wipes every user pan override the first time a shipped macro ring
     moves. The detection MUST therefore be keyed on the **cached composed `CloudStereoSpread` value**, not
     on the parameter ID. *Test:* `Seraphis_PartialOverrides_SurviveAMacroRingSweep` (same file).
  7. **the worst-case re-push cost is measured, not assumed.** Author **64** pan overrides (all bits set),
     then sweep `kMacroBloomId` across its full range so the composed spread changes on consecutive slices
     and the re-push fires repeatedly. Worst-of-seven **whole-`process()`** ns/block at the 8-voice
     operating point must still satisfy `kFullPolyCeilingNs = 2 666 666.7`
     (`param_perf_test.cpp:392`). This measures the real worst case — 64 indices × 16 voices × **2
     transcendentals** per index, because `updatePanGains` calls `equalPowerGains`, which is
     `std::cos`/`std::sin` (`crossfade_utils.h:50-53`), **not two `sqrt`** — i.e. **2048 trig evaluations
     inside one block**, against Phase 10's pinned 22.32 % of the 25 % ceiling. **If it fails the fan-out
     gets cheaper** (a dirty-index set, or coalescing the re-push to once per `process()` call); the
     ceiling does not move. `[.perf]`. *Test:* `Seraphis_PartialOverrides_RepushWorstCase`
     (`tests/integration/ui_perf_test.cpp`).

     > **ARM 7 RESTATED (2026-08-04, phase-owner ruling "Hybrid", OE-1).** The `kFullPolyCeilingNs`
     > comparison above is **superseded and kept for the audit trail**; arms 1–6 are untouched. Arm 7 is
     > now a **differential**: the re-push's **marginal** whole-`process()` cost, measured as *the
     > 64-override Bloom sweep* **minus** *the identical sweep with no overrides authored*, on one fixture
     > in one case, must be **≤ 285 866.7 ns/block (2.68 points of one core)** — premise 6's whole headroom
     > figure, against the +0.99 / +1.58-point deltas OE-1 measured, i.e. 1.70× headroom on the worst.
     > Keeping the sweep in **both** arms is what makes the delta the fan-out's cost: the macro-matrix
     > `apply()`, the smoother travel and the tracker comparison are in both figures and cancel, leaving
     > the only part `panBits` gates — the 64 × 16 `equalPowerGains` cos+sin calls
     > (`processor.cpp:3415-3418`). **Disclosed weakness:** the two windows are sequential (there is no
     > clearing `EditMessage`, so `panBits` cannot be un-authored and the baseline must precede the
     > authoring), so this delta carries a drift term measured at ±1.5 points on the reference machine —
     > which is why its bound is 27× SC-009(a)'s, and why a *negative* delta here is evidence of drift
     > rather than of a free fan-out. The absolute figure is still reported against the 25 % ceiling, which
     > is now Phase 11.5's gate.

  The mask edit is asserted across events 3, 4 and 5 (mask survives spread and seed changes by
  construction — *Existing components*, `HarmonicCloud` override lifetime — so 1, 2 and 6 are not mask
  events). *Test:* `Seraphis_PartialOverrides_SurviveClearingEvents`
  (`tests/integration/partial_edit_test.cpp`) for arms 1–5.
- **SC-015 — An edited state, AND its pan/mask overrides, persist across save/load with format version 3
  (Q3).** Edit slot 1's ratios (`setPartial`), pan-override partial 5 to `0.8`, and mask partial 9;
  `getState`, `setState` into a fresh processor, `getState` again: the two streams are **byte-identical**
  (Phase 9's FR-094), the first four bytes decode to version **3**, slot 1's 541-byte payload deserializes
  to the edited state, and the appended **272-byte** `[partials]` block (FR-034a) deserializes to the same
  pan/mask/override values. A stream truncated immediately before the `[partials]` block (simulating a
  pre-Phase-11 reader) still loads successfully, with every override reported absent — proving the
  EOF-safe strict-prefix chain, not a version branch, is what keeps old and new streams compatible in both
  directions. *Test:* `Seraphis_EditedState_RoundTripsAtV3` (extending `tests/unit/state_v3_test.cpp`).
  This is the criterion Phase 9 named when it wrote *"When Phase 11 makes states user-editable (RQ-1), that
  phase must either preserve the validity invariant … or add an explicit FR-094 carve-out"*
  (`specs/seraphis-phase9-parameters/spec.md:1679-1683`) — **no carve-out is taken**.
- **SC-016 — Moving a slot dropdown discards that slot's edits, and only that slot's.** After editing
  slots 0 and 1, moving slot 0's dropdown (ID 409) restores slot 0 to the named factory state exactly
  (`makeFactoryState` byte-compare) and leaves slot 1's edited payload byte-identical. The controller's
  `slotMirror_[0]` (C-11, Q1) is byte-identical to `makeFactoryState()`'s result after the same move.
  *Test:* same file (FR-035).
  **Arm 2 — the mirror also re-seeds from the STATE STREAM (FR-046's second source).** FR-046 names **two**
  re-seed sources; only the dropdown had a criterion, and the second is the sole justification for the
  signature widening to `loadMorphParamsToController` — this phase's only change to `morph_params.h`.
  Without this arm the failure is silent and user-visible: after a project reload the Edit-mode y-axis is
  drawn from default-constructed states while the processor holds the loaded ones. **(a)** Build a stream
  carrying four **edited** slot payloads, call `Controller::setComponentState` on it, and assert each
  `slotMirror_[i]` is **byte-identical** (`std::memcmp` over `sizeof(SpectralState)`) to the corresponding
  `deserializeSpectralState` result. **(b)** Corrupt one payload's version byte: that mirror entry must be
  **byte-unchanged from its pre-load value** (the deliberately ignored return value,
  `spectral_state.h:264-265`, `:300-305`) while the other three still load **and** the following
  parameters still read from the right stream offset — i.e. one bad payload desynchronises nothing.
  *Test:* `Seraphis_SlotMirror_ReSeedsFromTheStateStream` (same file).
- **SC-017 — Macro rings visibly perturb the constellation, and the perturbation is the real DSP.**
  Measured entirely on the producer, headless, on published `CloudFrame`s. The metric is
  **`P = Σᵢ aᵢ·log2(fᵢ / f₀) / Σᵢ aᵢ`** over `i < partialCount`, where `f₀` is the frame's own
  `fundamentalHz` — i.e. the amplitude-weighted mean pitch **in octaves above the fundamental**, a
  dimensionless quantity. The reference is pinned here on purpose: a percentage change of a bare
  `log2(frequencyHz)` is meaningless, because the number it is a percentage *of* depends on the arbitrary
  unit inside the logarithm (mean `log2(f)` for a 440 Hz stack is ≈ 8.8, so "+15 %" would silently mean
  +1.32 octaves — a 2.5× frequency shift).
  - **(a) Swept arm.** Over `kMacroBloomId` ∈ `{0, 0.25, 0.5, 0.75, 1.0}`, run under SC-013's drift
    precondition in its **plugin form** (`kCloudDriftDepthId` = 0, every macro at its FR-060 neutral, D-9
    row 9i) and with the morph travel rate raised so the Bloom row's `MorphTargetPosition` term can travel
    a measurable distance inside the render — a property of the **script**, applied identically to the
    swept arm, the negative control and the drift reference, and therefore not a threshold: `P` is
    **strictly monotonically increasing**, and `P(1.0) − P(0.0) ≥ **T** octaves`.

    **`T` = 2.58 octaves. MEASURED and WRITTEN BACK (OQ-4 step 2, 2026-08-04).** The measurement is
    emitted by the test itself: the swept arm prints the full five-point `P` table and the delta on a
    **passing** run (deliberately via `WARN`, not `INFO`, so a measurement whose whole purpose is to be
    read is not invisible exactly when it succeeds). The pilot run:

    | `kMacroBloomId` | `P` (octaves above f₀) | window spread |
    |---|---|---|
    | 0.00 | 1.81624 | 0.0869709 |
    | 0.25 | 2.78791 | 0.0582665 |
    | 0.50 | 3.50097 | 0.0438952 |
    | 0.75 | 4.03203 | 0.0426494 |
    | 1.00 | 4.40121 | 0.0404959 |
    | **`P(1.0) − P(0.0)`** | **2.58497** | — |

    | | Value | Status |
    |---|---|---|
    | `T` | **`2.58`** | **MEASURED** — `2.58497` rounded **down** to two decimals, written back here **and** into `cloud_frame_test.cpp`'s `kBloomOctaveThreshold` in the same change |

    The ruled pilot start was `0.35`; the measured figure is **7.4× larger**, so the criterion got
    *stronger*, not weaker — the rule "if the measured value is below the pilot the CRITERION moves, not
    the implementation" was never exercised. `T` clears its own noise floor by ~30×: the drift-only
    reference measures a window spread of `0.0869` octaves with nothing swept. Once written, `T` is fixed
    and is never lowered afterwards to fit a failing run.
  - **(b) Negative control, built from the API that exists.** The identical MIDI/parameter script is run
    with `kMacroBloomId` held at its FR-060 neutral (0) at all five sample points, every other macro also
    at neutral, and one **non-macro** ID (`kMasterGainId`) swept over the same five points instead. The
    control arm's `|ΔP|` MUST be `≤ 0.1 ×` the swept arm's `ΔP`, **and** below the drift-only spread
    measured over the same script with every macro fixed.
    **No suppression seam is invented.** `SeraphisMacroMatrix` has no enable/bypass/depth control — its
    entire mutator surface is `setMacro` (`seraphis_macro_matrix.h:554`), `setMacros` (`:599`) and
    `setTargetBase` (`:708`) — and building one (as Phase 10 had to for its
    `SeraphisEffectsStageBypassProbe`) would be a `dsp/` addition outside this spec's closed enumerated
    set (*Non-goals*). A macro-at-neutral arm asks the same question with the shipped API.
    **"No movement" is not asserted, and could not be.** Per-partial `BrownianDrift` runs unconditionally
    (KDD-1, roadmap lines 71–72) and `frequencyHz` is drift-inclusive by definition (C-2 clause 3), so the
    control arm always shows *some* movement. The bound above is a measured ratio, not zero.

  *Test:* `Seraphis_MacroRing_PerturbsConstellation` (`tests/integration/cloud_frame_test.cpp`).
- **SC-018 — Malformed edit messages cannot corrupt state.** Fuzzing `Processor::notify` with 10 000
  messages of random `kind`/`slot`/`index` and bit-pattern non-finite `a`/`b` leaves every slot satisfying
  `isValidSpectralState`, leaves `spectralSlotsHandoff_` in a legal state (`-1` or `[0,3)`), and produces a
  subsequent render that is finite everywhere. *Test:* `Seraphis_EditMessage_RejectsGarbage`.
- **SC-019 — Cross-platform and static-analysis gates are green.** `Seraphis.vst3` builds on all three OS
  legs; `seraphis_tests` green; `tools/pluginval.exe --strictness-level 5` clean;
  `node tools/check-portability.js` clean; `node tools/lint-layers.js` clean;
  `./tools/run-clang-tidy.ps1 -Target seraphis` clean; `auval -v aumu Srph KrAt` passes on macOS.
- **SC-020 — The drawer never stops the cloud view.** Headless. With the drawer **open**, feed
  `N = 60` synthetic `CloudFrame`s with **strictly increasing `sequence`** values through the controller's
  frame cache, invoking the view's timer callback **once per frame**. Then:
  (a) the view's `invalid()` call count is **exactly `N`** — one redraw per advanced `sequence`;
  (b) invoking the timer callback a further 30 times with **no** `sequence` change adds **zero** further
  `invalid()` calls (C-8's "redraw only on a changed `sequence`" — FR-018);
  (c) the view's `getViewSize()` is **byte-equal** to its collapsed-state rect, `(0, 32, 1000, 670)`
  (FR-024) — it is overlapped, not resized;
  (d) the same three hold with the drawer collapsed, so the drawer state provably changes nothing;
  (e) the **drawer's** own `getViewSize()` is **byte-equal** to `(0, 670, 1000, 700)` collapsed and
  `(0, 420, 1000, 700)` open — a comparison that is only meaningful because the drawer is a **direct child
  of the 1000 × 700 template root**, which is exactly what D-4's root-level `sub-controller` placement
  guarantees; on an intermediate-container placement these rects would be offset by that container's
  origin and no correct build would produce the numbers C-1's table states.
  **This measures the frame→redraw path, not the bare timer**, which is the property roadmap line 511
  states (*"The cloud view never stops rendering … it compresses or is overlapped, but stays alive"*). The
  earlier "redraw counter advances at ≥ 25 Hz over a 2-second window" wording is struck: a headless
  controller with no connected processor receives no frames, so `sequence` never advances and the counter
  never moves on a **correct** implementation — and where the test itself drives the timer, an observed
  rate is the rate the test chose, not a property of the code. The ≥ 25 Hz wall-clock feel is a manual
  DAW observation, recorded in the compliance notes, not an automated criterion.
  *Test:* `Seraphis_Drawer_DoesNotStopCloudView`.
  **(f) The axis map is monotone and CLAMPED, not wrapped (FR-017).** FR-017's *"monotone in each axis and
  MUST clamp rather than wrap at the span edges"* had no criterion — arms (a)–(e) count `invalid()` calls
  and compare rects, and SC-023 asserts zero points. Call the view's `hz → y` map over a swept list
  including `{1, 19.99, 20, 100, 1000, 20000, 20000.01, 44100}` and its `position → x` map over
  `{-2, -1, -0.5, 0, 0.5, 1, 2}`, and assert (i) strict monotonicity across the in-span interior; (ii)
  every sub-20 Hz input maps to the **same** `y` as 20 Hz and every super-20 kHz input to the same `y` as
  20 kHz — clamped, and demonstrably **not wrapped** (a wrap would put 44 100 near the 20 Hz end, which is
  the specific defect this arm exists to catch); (iii) `y` is inverted, i.e. higher Hz ⇒ smaller `y`;
  (iv) the same clamping at `x = ±1`.
  *Test:* `Seraphis_CloudView_AxisMapIsMonotoneAndClamped` (same file).
  **(g) A masked partial stays a click target (FR-017's Q5 clause).** *"A partial whose `maskBits` bit is
  set MUST be drawn as a hollow ring at a fixed minimum radius … so it stays a valid click target"* is the
  **entire** answer to how a user unmasks, and it had no criterion: a build that culled masked partials at
  zero radius passed every other test and made unmasking impossible from the only surface that offers it.
  Feed a synthetic frame with `maskBits` bit *i* set **and `amplitude[i] == 0`**, drive the timer callback,
  then call the view's **`renderForTest()` seam — not a bare `draw()`** (see SC-023: no `CDrawContext`
  exists in the headless harness, so `draw(nullptr)` is not a defined call) — and assert (i) partial *i* is
  counted among the drawn points, i.e. **not culled**; (ii) its drawn point is `hollow` with
  radius `kMaskedRingRadius` (`> 0`), not `kMinRadius`; (iii) a hit test at that point returns *i*.
  Complementary case: an **unmasked** partial with `amplitude == 0` draws at `kMinRadius`.
  *Test:* `Seraphis_CloudView_MaskedPartialStaysAClickTarget` (same file).
- **SC-021 — A macro axis reaches the effects surface, and it is the real matrix.** Headless, on the
  processor. Phase 10's RQ-4 discharge (C-10, FR-037 – FR-039).
  - **(a) Reach.** With `kFxDelayMixId` (1410) left at its shipped default of 0, sweeping
    `kMacroDissolveId` over `{0, 0.25, 0.5, 0.75, 1.0}`, **allowing one block of settle time per point**
    (the composition lags by exactly one `process()` call, C-10 clause 5) makes the **isolated send
    return** — Phase 10 SC-003's definition, read as the mean of the per-channel RMS over
    `preOutputTapLForTest()`/`preOutputTapRForTest()` (`processor.h:431`, `:434`) with
    `preOutputTapTruncatedForTest() == false` (`:444`) — grow **strictly monotonically** in RMS, from
    exactly `0.0` at the neutral point. The same for `kMacroEntropyId` against the stereo-wander stage,
    measured as M/S side-channel RMS.
  - **(b) Identity at neutral (FR-039).** With every macro at its FR-060 neutral, the value the processor
    pushes into `SpectralDelay`'s mix and `BrownianDrift`'s depth is **bit-equal** (`==` on the float) to
    the raw deep parameter value, for each of `{0, 0.25, 0.5, 1.0}` on the deep knob. This is what lets
    SC-001 keep its exact-equality form.
  - **(c) The composed value tracks the macro, not a re-pointed change-guard (D2, FR-038).** With the deep
    knob **held still**, moving `kMacroDissolveId` changes `composedFxDelaySendForTest()`, and moving
    `kMacroEntropyId` changes `composedFxWanderDepthForTest()`, on the **next** `process()` call. A build
    that read the raw deep atomic at `:2351`/`:3052` instead of the composed value would leave both
    unchanged — this arm fails on exactly the defect RQ-4 exists to prevent.
    `composedEffectsRecomputeCountForTest()` equals the `process()`-call count, so the composition is not
    accidentally change-guarded away. (`effectsPushes_`, `processor.cpp:1821`, is **not** the observable
    here — it is not incremented for IDs 1410/1441 on any correct implementation, class-(b)-smoothed as
    they are; see *Clarifications* D2.)
  - **(d) The guards did not relax.** `everyRowOwnerIsValid`, `everyEffectsRowHasAPodField`,
    `everyTargetInFr061to065IsPresent` and `everyRowSharesOneBasePerTarget` are all still `static_assert`ed
    (`seraphis_macro_matrix.h:816-825`), and a compile-time check asserts
    `static_cast<std::size_t>(SeraphisMacroTarget::Count)` equals the previous count **plus exactly 2** —
    so a third target cannot be added without amending this spec.

  *Test:* `Seraphis_MacroDissolve_ReachesEffects` (`tests/integration/effects_chain_test.cpp`, beside
  Phase 10's isolated-send-return machinery) and `SeraphisMacroMatrix_EffectsOwner_IsAdditive`
  (`dsp/tests/unit/systems/`).
- **SC-022 — The custom-view surface is exactly three classes, plus one sub-controller.**
  (a) **Source-level class count — a compile-time check FIRST, with the scan as a tripwire (FR-026).** The
  set of classes under `plugins/seraphis/src/ui/` deriving (directly or transitively) from
  `VSTGUI::CView` is **exactly** `{CloudView, MacroRingKnob, DrawerContainer}`. A pure token scan **cannot
  resolve transitive bases** and therefore cannot state this on its own: `MacroRingKnob` derives from
  `Krate::Plugins::ArcKnob` (`arc_knob.h:49`) → `VSTGUI::CKnobBase` → `CControl` → `CView`, a chain living
  entirely outside `src/ui/`, and a fourth class written `: public VSTGUI::CTextLabel` would be
  transitively a `CView` and invisible to it. **Arm 1 (compile-time):** the test TU includes every header
  under `src/ui/` and `static_assert`s `std::is_base_of_v<VSTGUI::CView, T>` for exactly those three types,
  plus `static_assert(!std::is_base_of_v<VSTGUI::CView, UI::SeraphisEditSubController>)`. **Arm 2 (scan as
  tripwire):** scan `src/ui/*.h` for every `class X : public B` and fail on any `B` **not on an enumerated
  allowlist** `{VSTGUI::CView, VSTGUI::CViewContainer, Krate::Plugins::ArcKnob,
  VSTGUI::DelegationController, VSTGUI::ViewCreatorAdapter}` — an **unknown base name is a red test**,
  never a silent pass — carrying SC-011's guards (`filesMissing == 0`, `codeBytes > 0`, witness count
  `> 0` for the token `CloudView`). Adding a fourth view class therefore forces either a new allowlist
  entry *and* a new `static_assert` (a visible spec amendment) or a failing build. SC-004 counts
  *instances* in a built tree and cannot see a class the uidesc happens not to reference.
  (b) **Sub-controller (FR-045):** after `exerciseEditorLifecycle`, `Controller::createSubController` has
  been called at least once and returned a non-null `UI::SeraphisEditSubController`; **each control in
  C-7b's table — including the header preset button (D-5) —** reports that object from `getListener()` and
  carries its assigned **session tag (`≥ 9000`, never a `ParamID`)**; and after `willClose()` the
  controller's sub-controller instance counter is back to 0, so a second open cycle starts from the same
  state as the first.
  (c) **The rings do not animate the cloud view locally (FR-021's view half).** FR-021 is a requirement
  about the **view** — *no view-local animation, no synthetic displacement, no interpolation toward a
  target the DSP is not producing* — and its only other criterion, SC-017, is measured **entirely on the
  producer**, so a `CloudView` that faked the constellation's reaction to a ring would leave `P` unchanged
  and SC-017 would still pass. With a **fixed** cached `CloudFrame` (constant `sequence`, never updated),
  drive `MacroRingKnob`'s value across its full range and assert the view's `invalid()` count **and** its
  drawn-point set are **unchanged**: no redraw, no moved point. The view must have no path from a macro
  value to a point position; its only input is the frame.
  *Test:* `Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally` (same file).
  (d) **The preset button opens the browser (FR-007).** Nothing anywhere exercised FR-007's button. After
  `exerciseEditorLifecycle`, drive the button's `valueChanged` **through the sub-controller** (D-5's row)
  and assert a `Krate::Plugins::PresetBrowserView` is present in the frame and bound to the controller's
  `presetManager_`; drive it again and assert the browser is gone. Run inside the ASan lifecycle lane so a
  browser still open at `willClose()` is a report, not luck.
  *Test:* `Seraphis_PresetButton_OpensTheBrowser` (same file).
  *Test (arms a, b):* `Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController`
  (`tests/unit/controller/custom_view_test.cpp`).
- **SC-023 — An editor that never receives a frame still works (FR-019).** With the C-2 clause 6 gate
  never opened and `cycles = 10`: the lifecycle completes; `controller.getParameterCount() == 107` before
  and after; and **per cycle, between `attached()` and `removed()`, the test calls the cloud view's
  `renderForTest()` seam once** and then asserts the view's draw count is `≥ 1` and its drawn-point count is
  **exactly 0** — an empty constellation rendered without dereferencing a null frame.
  **The seam replaces an unreachable claim, and the property tested is unchanged (D-9 row 9g).** An earlier
  wording required `draw()` to be *"entered at least once per cycle during `exerciseEditorLifecycle`"*, and
  that is **structurally impossible**: the harness calls only `IPlugView::attached(nullptr, …)` and
  `removed()` (`tests/test_helpers/editor_lifecycle_harness.h:98-133`) and its own banner records that the
  platform attach is a no-op — `CFrame::open(nullptr)` fails harmlessly (`:12-13`) — so no paint cycle and
  no `CDrawContext` ever exists and `draw()` is **never** entered. Calling `draw(nullptr)` by hand is not a
  defined call. Because `exerciseEditorLifecycle` owns its own cycle loop, this arm drives the open/close
  pair directly (the same three calls) and invokes the seam in between. Run under the `-DENABLE_ASAN=ON`
  Debug build so a null-frame dereference is a report, not luck.
  *Test:* `Seraphis_Editor_WorksWithNoFrameEverReceived`, extending `editor_lifecycle_test.cpp`.
- **SC-024 — Edit-mode authoring works identically with no voice held (Q6).** Two identical ratio-drag
  pointer-delta sequences on the same slot's partial: (A) with a voice sounding at exactly C4
  (`fundamentalHz == 261.63`) and BrownianDrift active; (B) with no voice sounding (`activeVoices == 0`,
  `fundamentalHz == 0`). Both arms produce the same stored `ratios[index]` within float epsilon, and arm
  (A)'s result contains no drift-baked error despite active drift — proving the inverse map excludes drift
  (C-4, C-11) rather than merely avoiding arm (B)'s divide-by-zero. *Test:*
  `Seraphis_EditMode_AuthoringWorksWithoutANote` (`tests/integration/partial_edit_test.cpp`).
- **SC-025 — The Blend A→B gesture is reversible, not a one-way ratchet (Q2).** A `BlendBegin` (kind 7)
  followed by the `t` sequence `0 → 0.5 → 1 → 0.5 → 0` leaves the selected slot byte-identical to the
  pristine-A snapshot taken at `BlendBegin` (`blendStates(A, B, 0) == A` is C-6's own rule). A second
  gesture (`BlendBegin` at the now-current state, then `t = 1`) lands on B, not on a state blended twice. A
  `t` (kind 4) message received with no preceding `BlendBegin` in the same gesture is dropped (FR-036) and
  leaves the slot unchanged. *Test:* `Seraphis_BlendGesture_IsAbsoluteNotCompounding`
  (`tests/integration/partial_edit_test.cpp`).
- **SC-026 — A second open editor keeps receiving frames after the first closes (Q7).** Two
  `didOpen` sequences on the controller followed by one `willClose` leave the editor-open refcount at 1
  and send **no** close message to the processor; the cloud-frame gate (C-2 clause 6) stays open and
  frames continue publishing (`cloudFramePublishAttemptCountForTest() > 0` continues incrementing).
  Closing the second view brings the count to 0 and sends exactly one close message. `terminate()` resets
  the count to 0 regardless of its prior value. *Test:* `Seraphis_MultiEditor_RefcountGatesCorrectly`
  (`tests/unit/controller/custom_view_test.cpp`).
- **SC-027 — A gesture's final value is never dropped by the throttle (Q8).** A synthetic drag emitting
  200 pointer-moves within a single 33 ms window, followed by mouse-up: the controller sends **at most
  one** throttled message for that window, **plus exactly one** additional terminal message whose payload
  equals the last pointer-move's value, sent unconditionally on mouse-up regardless of the throttle
  window's state. *Test:* `Seraphis_EditThrottle_FlushesFinalValue`
  (`tests/unit/controller/custom_view_test.cpp`).
- **SC-028 — A ratio/amplitude edit reaches a sounding voice, audibly, within the existing smoothing time
  constant (D1, FR-033a).** Start a voice, let it sound past `hasSounded_`, then send a `setPartial`
  ratio edit to the contributing slot mid-note. Within `SpectralMorphEngine`'s FR-047 absorption-crossfade
  window (the same time constant Phase 3 already measures its click-free swap against — no new time
  constant is introduced), the voice's rendered fundamental for that partial moves to within **5 cents** of
  the new authored ratio, measured the same way SC-013(a) measures a peak move. Before the edit,
  `SeraphisVoice::getRejectedConfigureTimeCallCount()` stays unchanged across the push — i.e. the push is
  **accepted**, not silently rejected and retried onto a future note. *Test:*
  `Seraphis_EditMode_RatioEditReachesSoundingVoice` (`tests/integration/partial_edit_test.cpp`).
- **SC-029 — The edit that reaches a sounding voice is click-free (D1, FR-033a).** Using the same
  per-chunk metrics Phase 3's FR-044 already establishes for this exact crossfade —
  `kMaxAmpDeltaPerChunk` and `kMaxRatioDeltaCentsPerChunk` (`spectral_morph_engine.h`, values per
  `specs/seraphis-phase3-spectral-morph/spec.md` FR-044) — a ratio edit landing mid-note via
  `SeraphisVoice::setSpectralState` produces **no chunk-to-chunk step exceeding either bound**, over the
  full absorption window, exactly as an in-DSP `setState` call already must. This is a **measured
  discontinuity bound, never a bit-exact comparison** (root `CLAUDE.md`, *Cross-Platform Compatibility*).
  *Test:* same file, extending Phase 3's own continuity harness rather than inventing a second one.
- **SC-030 — Phase 3's `setState`/fade tests stay green (D1, regression).** Every existing Phase 3 test
  exercising `SpectralMorphEngine::setState`, `setStateCount`, the FR-047 absorption fade and FR-044's
  continuity bound (`dsp_systems_tests`' `spectral_morph_*` suites) passes **unmodified** after FR-033a's
  `SeraphisVoice` gate relaxation and the `spectral_morph_engine.h:198-206` comment correction — the
  relaxation touches only `SeraphisVoice`'s call site and a comment, never `SpectralMorphEngine::setState`
  itself. *Test:* re-run of the existing Phase 3 suite; no new test file.
- **SC-031 — An edit gesture in flight on a held note still fits the budget (D1, FR-033a).** The arm the
  gate relaxation needs, and which no other criterion supplies: SC-009, SC-010 and SC-014 arm 7 all run
  with a **static** slot set, and SC-029 measures continuity, not time. With the gate relaxed, **every**
  voice now executes `isValidSpectralState` + `buildSanitized` — a 64-entry `std::log2` pass
  (`spectral_morph_engine.h:513`, `:537-543`) — **before** the identity early-out at `:302-305`, where a
  sounding voice previously rejected at one predicted branch; and `consumeSpectralSlotHandoff()` re-arms
  `spectralRetryMask_ = 0xFFFFu` (`processor.cpp:2834`), so one handoff costs 16 voices × 4 slots ≈ **4096
  `std::log2`**. Phase 11 also turns that from a rare event into a stream: FR-048's throttle admits one
  accepted `EditMessage` — hence one handoff — every 33 ms, i.e. roughly one such pass every third block at
  512 / 48 kHz.
  **Threshold:** hold a note at the 8-voice operating point, drive a **30 Hz** kind-1 partial-ratio drag,
  and require worst-of-seven **whole-`process()`** ns/block ≤ `kFullPolyCeilingNs = 2 666 666.7`
  (`param_perf_test.cpp:392`), against Phase 10's pinned 2 380 980 ns (22.32 %) — the same **2.68
  percentage points** of headroom every other CPU criterion here spends from. Measurement protocol as
  above: fresh boot, seven runs, best-of-16 per estimate, worst reported. `[.perf]`.
  **If it fails, the push gets cheaper and the ceiling does not move:** narrow `spectralRetryMask_` to the
  voices that can still reject (with the gate relaxed a blanket re-arm is pure waste), or add a
  pre-`applySpectralStates` identity check against the processor's last-pushed `spectralSlots_` so an
  unchanged slot never reaches `buildSanitized`. **Dropping the throttle below C-8's 30 Hz is not an
  available fix**, and neither is raising the ceiling.
  *Test:* `Seraphis_EditGestureInFlight_FitsTheBudget` (`tests/integration/ui_perf_test.cpp`).

  > **RESTATED (2026-08-04, phase-owner ruling "Hybrid", OE-1).** The `kFullPolyCeilingNs` threshold above
  > is **superseded and kept for the audit trail**. SC-031 is now a **differential**: the in-flight
  > gesture's **marginal** whole-`process()` cost, measured as *the 30 Hz kind-1 drag* **minus** *the same
  > warm fixture with no gesture at all*, must be **≤ 106 666.7 ns/block (1.00 point of one core)** —
  > against the +0.30 / −0.81-point deltas OE-1 measured. The no-gesture window is taken **first**, before
  > a single `EditMessage` is sent, so the baseline really is the static slot set SC-009 covers and the
  > delta is the whole cost of D1/FR-033a (the relaxed gate's `buildSanitized` pass plus the `0xFFFFu`
  > retry re-arm). **Disclosed weakness:** the two windows are sequential, so this delta carries the same
  > ±1.5-point drift term SC-014 arm 7's does — its bound is 10× SC-009(a)'s for that reason. Everything
  > else stands: the remedy order is unchanged, and **raising the bound, raising the ceiling and dropping
  > below the 30 Hz throttle are all forbidden**. The absolute figure is still reported against the 25 %
  > ceiling, which is now Phase 11.5's gate.
- **SC-032 — Each of FR-028's four gestures emits the right `EditMessage` (FR-028).** Three of the four had
  no criterion: every other editing test injects `EditMessage`s at `Processor::notify` rather than driving
  the view, and only the plain vertical ratio drag is covered at view level (SC-024). Drive the cloud
  view's mouse-down / mouse-moved / mouse-up entry points with synthetic point and button-state sequences
  against a fixed synthetic frame, and after each gesture read the controller's last-sent message. Assert
  **all four rows of C-4's table**: (1) plain vertical drag ⇒ `kind == 1`, `a == newRatio` from the inverse
  map, and **`b` equal to the mirror's existing `amplitudes[i]`, unchanged**; (2) **alt** + vertical drag —
  a plain VSTGUI `CButtonState` modifier, **never a platform key API** (FR-005) — ⇒ `kind == 1`, **`a`
  unchanged** and `b == newAmp`; (3) horizontal drag (`|dx| >= |dy|`) ⇒ `kind == 2` with `a ∈ [-1, +1]`
  equal to the clamped x-map; (4) a click within the click-slop radius ⇒ `kind == 3` with `a` the
  **toggled** value computed from `maskBits` — asserted in **both** directions, by running it twice against
  frames whose bit *i* is clear then set, giving `a == 1.0f` then `a == 0.0f`. `index` equals the
  hit-tested partial in all four. A view that emitted kind 2 for an alt-drag, never set `b`, or sent an
  unconditional mask passes every other criterion in this spec; this is what makes `EditMessage::b` a live
  field. *Test:* `Seraphis_CloudView_GesturesEmitTheRightEditMessage`
  (`tests/unit/controller/custom_view_test.cpp`).
- **SC-033 — Toggling a mask OFF restores the voice, on every slot (FR-028, FR-030, Q5).** The unmask half
  of the mask gesture, end to end, which no criterion reached: SC-006(e) compares `maskBits` against the
  processor's own table rather than the engine, and SC-014's mask arms only assert that a mask **survives**
  a clearing event. Combined with C-4's pre-9a polarity and a re-push body that walked only the **set**
  bits, clearing a bit produced **no engine call at all** and `HarmonicCloud::masked_[i]` stayed `true`
  forever (`harmonic_cloud.h:1084-1089`) — the user could mask a partial and never get it back.
  **Threshold:** hold a note; send kind 3 with `a = 1` for partial *k*; render past the amplitude smoother
  and assert, **through the engine on every voice in `[0, kMaxVoices)`**, that partial *k*'s current
  amplitude has decayed to ≈ 0 while partial *k+1*'s has not. Then send kind 3 with `a = 0` for the same
  *k*, render the same span, and assert partial *k*'s amplitude **recovers to within 1 % of its pre-mask
  value on every voice** — all sixteen slots, not just the allocated one, for the same reason FR-033's
  fan-out covers `kMaxVoices`. Finally assert the published frame's `maskBits` bit *k* is clear, so the
  frame and the engine agree about what the user is looking at.
  *Test:* `Seraphis_PartialMask_ToggleOffRestoresTheVoice` (`tests/integration/partial_edit_test.cpp`).

---

## Open Escalations

### OE-1 — SC-009, SC-010, SC-014 arm 7 and SC-031 all breach the 25 % ceiling, and Phase 11 is not the mechanism. ~~OPEN, phase-owner's call.~~ **RULED AND CLOSED 2026-08-04 — see the RULING block at the end of this section.**

**Status: ~~BLOCKING for compliance~~ RULED (2026-08-04, "Hybrid"). Nothing was relaxed to reach it.**
Everything from here to the RULING block is the escalation **as it was filed**, kept verbatim because it
is the evidence base the ruling cites. `kFullPolyCeilingNs`,
`kClosedGateCeilingNs`, `kBaselineFullPolyNs` and every `REQUIRE` in
`tests/integration/ui_perf_test.cpp` and `tests/integration/param_perf_test.cpp` are untouched; no arm was
retagged, skipped or deleted. The same escalation is recorded verbatim in `ui_perf_test.cpp`'s own banner
so a reader of the four failures gets the decomposition without re-deriving it.

**The machine state was validated, not assumed**, using the two controls `param_perf_test.cpp`'s cold-dataset
banner defines:

| Control | run 1 | run 2 | Reference | Verdict |
|---|---|---|---|---|
| SC-008 arm 1 calibrator | `78.55` ns | `73.55` ns | 2026-08-02 fresh-boot cold **worst** `82.40` ns (`param_perf_test.cpp:160`) | **0.95× / 0.89×** — at or *below* the cold reference |
| Phase 7 SC-001 (`dsp_systems_tests.exe "SeraphisEngine_FullPolyCpuBudget"`, contains **no** Phase 9/10/11 code) | `19.283 %` | — | Phase 7's recorded `18.34 %–20.07 %` band | **in band** |

An earlier pass read `3.7e6`–`4.6e6` ns on these arms; those were host contention — the same shape Phase 10's
compliance already reversed once. The figures below are the idle ones and they **still breach**.

**The cold dataset** — **two** consecutive idle passes; every figure a best-of-16:

| Subject | run 1 | run 2 | worst | |
|---|---|---|---|---|
| chain only, 107-row surface, undivided (`Seraphis_FullPoly_CpuBudget_WithFullSurface` — engine + reverb + output stage, **no `Processor`**, **no effects stage**) | 20.87 % | 22.04 % | **22.04 %** | PASSES |
| effects stage alone at maxima (Phase 10 SC-013) | 0.4475 % | 0.4484 % | **0.4484 %** | PASSES |
| whole-`process()`, defaults, undivided (SC-008 arm 3) | 11.89 % | 12.13 % | — | — |
| whole-`process()`, defaults, 8 × 64 slices (SC-008 arm 3) | 13.88 % | 12.56 % | — | subdivision ratio itself noisy: 1.168× then 1.036× |
| **SC-009(a)** whole-`process()`, gate **OPEN** | 30.69 % | 31.74 % | **31.74 %** | **BREACH** |
| **SC-010(b)** whole-`process()`, gate **CLOSED** | 31.19 % | 31.30 % | **31.30 %** | **BREACH** |
| **SC-014 arm 7** whole-`process()` + 64-override re-push | 31.68 % | 33.32 % | **33.32 %** | **BREACH** |
| **SC-031** whole-`process()` + 30 Hz gesture in flight | 30.99 % | 30.93 % | **30.99 %** | **BREACH** |

**What the arithmetic says — this is the whole finding:**

1. **The gate-CLOSED figure is not below the gate-OPEN one** — 31.19 vs 30.69 on run 1, 31.30 vs 31.74 on
   run 2, i.e. it lands on *either side* across two passes. The producer's own cost is therefore below the
   run-to-run noise floor, measured twice. **SC-010's actual claim — "the producer costs nothing when the
   editor is closed" — is TRUE.** What fails is the absolute comparison, on a path that contains no
   Phase 11 code at all.
2. **Phase 11's entire measurable audio-thread cost is 1.88 points at the worst**, taken as same-run
   deltas against SC-009(a): the 64-override re-push is **+0.99 then +1.58**; the in-flight gesture
   (D1/FR-033a) is **+0.30 then −0.81**, i.e. inside the noise. That is **inside** the 2.68 points
   premise 6 budgeted.
3. **The breach is 6.7–8.3 points and it is inherited.** chain (22.04 %) + effects (0.4484 %) = 22.5 %,
   against 31.74 % measured with the gate closed and no gesture. The ~9.2-point remainder is `Processor`
   plumbing — the 8 × 64 control-chunk subdivision and the per-slice parameter fan-out over a maxed
   107-row surface — i.e. Phase 8–10 code.

**The spec's premise is what is wrong, not the measurement.** Premise 6 derives "2.68 percentage points of
headroom" from Phase 10's SC-014 worst of 2 380 980 ns = 22.32 %, which is the **chain-only** subject.
SC-009(a), SC-010(b), SC-014 arm 7 and SC-031 measure **whole-`process()`**, which strictly *contains* the
chain plus the effects stage plus the slice loop plus the plumbing. SC-010(b) in particular — a
whole-`process()` figure against a ceiling derived from a **chain-only** baseline
(`1.15 × 2 318 840`) — is **unsatisfiable by construction**, on any machine, for any implementation.

**The pre-declared remedies cannot close it.** Each criterion names its own lever, and all three are now
measured:

| Sanctioned lever | Criterion that names it | Measured bound |
|---|---|---|
| make the producer cheaper | SC-009 | **0 points** — the producer is already free (fact 1) |
| make the pan/mask fan-out cheaper | SC-014 arm 7 | **1.58 points** |
| narrow `spectralRetryMask_` / add a pre-`buildSanitized` identity check | SC-031 | **0.30 points** |

Their **sum is 1.88 points against a 6.7-point breach**. No combination of the sanctioned levers passes
these arms — which is exactly why this is escalated rather than patched, the same call Phase 9's T028 made
(`param_perf_test.cpp:113-142`) when its remedy list could not reach its gate.

**What the owner is being asked to rule on** (none of these may be taken unilaterally):

1. Whether whole-`process()` is the right subject for the roadmap's *"8 voices, **everything on**, ≤ 25 % of
   one core"* (roadmap line 313). If it is — and it reads as though it is — then **the shipped plugin is at
   31.7 % and has been since Phase 10**, and the work is a `Processor::process()` optimisation phase, not a
   Phase 11 task.
2. Whether SC-010(b) is restated against a **whole-`process()`** baseline measured on this subject (it
   currently compares against a chain-only one and cannot pass), and if so, that baseline must be pinned
   from a seven-run cold dataset, not from this single pass.
3. Whether SC-009/SC-014 arm 7/SC-031 keep the absolute 25 % ceiling — in which case they stay red until
   (1) is done — or are restated as **differential** criteria against a whole-`process()` baseline, which
   is what they actually test (Phase 11's *marginal* cost) and what all three already measure cleanly.

**Protocol shortfall, stated rather than hidden:** the measurement discipline is worst-of-seven on a
fresh-boot idle machine. This is **two** cold passes, not seven. It is nowhere near the boundary — the
breach is 6.7–8.3 points against a run-to-run band that the 2026-08-02 cold dataset measured at ~1.0 point
— so a seven-run set would not change the verdict, but the pin for any restated baseline in ruling (2)
MUST come from a full seven-run set.

**RULING (2026-08-04, phase owner): Hybrid.** OE-1 is **CLOSED**. Everything above is kept verbatim as
the evidence base; nothing in it is withdrawn. The three questions are answered as follows.

**(a) SC-009(a), SC-014 arm 7 and SC-031 are RESTATED as DIFFERENTIAL criteria** — question 3's second
option. Each now measures its own Phase 11 feature's **marginal** whole-`process()` cost, as a delta
against a whole-`process()` baseline arm measured **in the same test case, on the same warm fixture**:

| Criterion | Subject | Same-run baseline | Differential bound |
|---|---|---|---|
| SC-009(a) | whole-`process()`, gate **open** | whole-`process()`, gate **closed**, interleaved trial-by-trial with counterbalanced order | ≤ **10 666.7 ns/block** (0.10 point of one core) — premise 6's stage budget, i.e. the producer may cost no more inside whole-`process()` than SC-009(b) already requires the stage to cost alone |
| SC-014 arm 7 | the 64-override macro Bloom sweep | the **same sweep with no overrides authored** | ≤ **285 866.7 ns/block** (2.68 points of one core) — premise 6's whole headroom figure, against a measured worst delta of 1.58 points |
| SC-031 | the 30 Hz kind-1 drag in flight | the same fixture with **no gesture** | ≤ **106 666.7 ns/block** (1.00 point of one core) — against a measured delta of +0.30 / −0.81 points |

These are same-run deltas, so they are machine-state robust and need **no pinned absolute baseline**. The
absolute whole-`process()` figure is still measured and still **reported** against the 25 % ceiling by
every one of the three; it is simply no longer a Phase 11 gate, for the reason fact 3 establishes.

*Unit note, recorded because the ruling stated the last two bounds twice:* the ruling's parenthetical
figures (`71467` and `26667` ns/block) are 2.68 % and 1.00 % of the **25 % ceiling**, not of one core —
i.e. 0.67 and 0.25 points. Against the ruling's own governing clause (*"choose per-arm differential
ceilings from the spec's own measured deltas with honest headroom"*) those parentheticals fail on the very
data they are derived from: the measured re-push delta is +0.99 / +1.58 points and the measured gesture
delta is +0.30 / −0.81 points. The bounds above therefore take the ruling's stated **unit** — points of
one core, which is the unit its own first bound (10 666 ns = 0.10 point of one core) uses and the unit
every other figure in this spec is expressed in.

**(b) SC-010(b) is RESTATED against a whole-`process()` absolute baseline** — question 2, answered yes.
The new constant is `kBaselineWholeProcessNs` (`ui_perf_test.cpp`), and the regression factor stays
`1.15`. It is **PROVISIONAL**: the seven-run fresh-boot idle dataset the protocol demands **does not
exist** for this subject — the table above is two passes and the machine has been under load since — so
`constexpr bool kSc010BaselinePinned = false` and the absolute comparison **reports rather than gates**,
exactly the mechanism `param_perf_test.cpp:2156-2175` defines and `kSc009BaselinePinned` used while
SC-009's own baseline was unpinned. The provisional value is **3 385 600 ns/block = 31.74 % of one
core**, the worst whole-`process()` figure in the two-pass table (the SC-009(a) gate-open run-2 reading;
the gate-closed worst is 31.30 %, and by fact 1 the two are the same quantity inside the noise, so the
worse of the pair is the conservative pick). **Arm (a) — `cloudFramePublishAttemptCountForTest() == 0`
with the gate closed — is UNCHANGED and stays a hard `REQUIRE`.**

**(c) The absolute 25 % whole-`process()` promise is NOT dropped** — question 1, answered: whole-
`process()` **is** the right subject for roadmap line 313, the shipped plugin **is** over it, and the work
is a `Processor::process()` optimisation phase rather than a Phase 11 task. That phase is now on the
roadmap as **Phase 11.5 — Processor whole-`process()` optimization**, inserted between Phase 11 and
Phase 12, scoped to the 8 × 64 slice loop and the per-slice parameter fan-out, citing this section as its
evidence base, and **Phase 12 must not ship before it is green**.

---

## Edge cases

**RT-safety boundaries**
- `dataExchangeHandler_->getCurrentOrNewBlock()` returns `InvalidDataExchangeBlockID` when the host's queue
  is full. The publish MUST be **skipped**, never retried in a loop and never blocked on — Membrum's guard
  (`processor.cpp:849-852`) is the shape. A skipped frame is one dropped animation step. This is why C-2
  clause 7 makes the cadence seam an **attempt** counter and records skips separately: at `numBlocks = 4`
  filled at ≈ 94 Hz and drained at 30 Hz, skips are the steady state, and a success-counting seam would
  make SC-007 unsatisfiable.
- A host that implements no DataExchange API at all: the SDK's IMessage fallback carries the block
  (`plugins/membrum/src/controller/controller.cpp:1743-1755`). If even that is unavailable, the cloud view
  renders empty (FR-019) and every parameter still works. **The editor must never require a frame to
  function.**
- Host block sizes above `SeraphisEngine::kMaxBlockSamples = 2048` (`seraphis_engine.h:215`) are already
  handled by the processor's own subdivision (`processor.cpp:1311`); the publish is per `process()` call,
  so an oversized block yields one frame, not several.
- `notify()` on the message thread concurrent with `setState()` on the message thread is impossible — same
  thread — which is precisely why C-5 places the second staging writer there and adds no interlock.
- Two editor views open at once (Q7): only the controller's refcount transitions (0→1, 1→0) send the C-5
  kind-0 gate message; the processor's own gate is unaware of view count and never toggles mid-session
  because of a second view opening or closing.

**Parameter extremes**
- `kPolyphonyId = 1`: the focus rule degenerates to slot 0 and still yields a live constellation.
- `kCloudRichnessId` at its minimum drives `getActivePartialCount()` low; `partialCount` follows and the
  zero-filled tail keeps the view from drawing stale points at their last positions (FR-013).
- A partial whose `amplitude[i]` is exactly `0` must still be drawn at **zero radius**, not culled with a
  discontinuity, so a fading partial dissolves rather than blinking out. **Exception:** a *masked* partial
  (Q5) — its `maskBits` bit is set — draws as a hollow ring at a fixed minimum radius instead, never at
  zero radius, specifically so it remains a click target for the unmask gesture; this is the one case
  where radius is not a monotone function of amplitude alone (FR-017).
- `kCloudStereoSpreadId = 0` collapses every `position[i]` to the same value: 64 coincident points. The
  view must remain legible (overdraw is acceptable; a divide-by-zero in an autoscale is not — the axis span
  is fixed, per FR-017).
- `activeVoices == 0` in Edit mode (Q6): the vertical drag's inverse map uses the fixed C4 = 261.63 Hz
  reference rather than dividing by `fundamentalHz == 0`, and the drawn axis is the selected slot's C-11
  mirror ratios against that same reference — authoring is fully functional with no note held.

**Sample-rate and configuration changes**
- A sample-rate change re-enters `setupProcessing` and re-prepares the engine. `partialOverrides_` MUST
  survive it and be re-pushed after prepare (FR-030, FR-043), because prepare reaches `cloud_.reset()`
  paths that clear both tables (`harmonic_cloud.h:331-332`). **SC-014 arm 5 is the criterion for this**;
  FR-043 is not left uncovered.
- The editor may be open across `setActive(false)` → `setActive(true)`. `onDeactivate`/`onActivate` are
  driven from there (FR-011); the view must tolerate a gap in frames with no visual discontinuity beyond a
  paused constellation.

**Seed determinism**
- Changing `kSeedId` clears every pan override (`harmonic_cloud.h:703`). FR-030 re-pushes; SC-014 gates it.
  The *scatter* legitimately changes — only the user's explicit overrides are restored.
- Two runs on the **same build** loading the same state with the same seed must produce frame streams that
  agree on SC-008's four relative aggregate metrics. **No cross-toolchain agreement is required or
  asserted.** The earlier "two hosts … within `render_fingerprint.h` tolerances" wording is struck twice
  over: those constants are absolute per-sample bounds on *audio* (`render_fingerprint.h:25-29`, calibrated
  against a peak of 2.17) and are meaningless applied to absolute Hz, and the repo's float rule forbids
  demanding bit-identical FP across MSVC/GCC/AppleClang in the first place (roadmap line 598). A
  cross-toolchain frame difference is expected and is not a defect; nothing in the plugin's behaviour
  depends on two machines drawing the same scatter.

**Editing extremes**
- Dragging a partial past its neighbour: C-6's monotone window clamps rather than swaps, so the ordering
  invariant holds and the gesture simply stops. A swap would silently renumber every partial above it.
- Dragging with `numPartials == 0` (an empty slot): every `setPartial` is a no-op; the view has nothing to
  drag and must not synthesize a partial.
- `blendStates` with A == B: returns A for every `t`, and the Blend slider is inert — correct, not a bug.
- `tiltState` on an all-zero-amplitude state: `normalizeSpectralState`'s `sumSquares > 0` guard
  (`spectral_state.h:169`) leaves it untouched, so no NaN is produced.
- Clicking a masked partial (Q5): the click reads the current `CloudFrame::maskBits` bit and sends the
  toggled value as the same `EditMessage` kind 3 masking uses — masking and unmasking are one gesture and
  one message kind, distinguished only by the bit the controller read before sending.
- Dragging or clicking a masked partial (Q5): the ratio/amplitude/pan gestures remain fully functional on a
  masked partial — masking only forces its amplitude to zero in the audio chain (`harmonic_cloud.h:1081-
  1083`); it does not disable authoring.
- A ratio/amplitude edit lands while the focus voice is mid-note (D1): the edit is **not** deferred to the
  next note-on — `SeraphisVoice::setSpectralState`'s gate is relaxed for this call (FR-033a) and the push
  is absorbed by `SpectralMorphEngine`'s existing FR-047 crossfade, the same click-free path an in-DSP
  `setState` call already uses. A voice that has **not** sounded yet, or has fully finished, is unaffected
  by this relaxation — `isConfigurable()` still gates every other caller exactly as before.
- A Blend slider move with no preceding `BlendBegin` in the current gesture, e.g. a reordered or dropped
  host message (Q2): `Processor::notify` drops it (C-5 clause 5, FR-036) rather than blending from a stale
  or absent snapshot.
- A drag ending exactly on a throttle boundary (Q8): the mandatory mouse-up flush is sent in addition to,
  never instead of, the last throttled message — at most one redundant identical message is sent, never
  zero and never a dropped final value.
- The controller-side mirror (C-11, Q1) and the processor's `spectralSlots_` can transiently disagree —
  e.g. immediately after a 409–412 dropdown change lands in one before the other's `EditMessage` arrives.
  This is display-only and never affects audio; SC-016 exercises the path where both eventually agree.

---

## Resolved questions

The roadmap left exactly three decisions to this spec — *"Left for the implementation spec to resolve:
morph state-slot A–D placement …, whether the three freeze controls … also get a floating always-visible
cluster or stay drawer-only, and fixed vs resizable window sizing"* (roadmap lines 519–522). "Resolve"
means resolve, so all three are **decided here and encoded in the body**; each keeps its reasoning so the
phase owner can overturn it with the argument in view. None of them is left as a recommendation the FRs
then contradict.

- **RQ-1 — Morph state-slot A–D placement. RESOLVED: inside the Morph tab**, as the roadmap suggested
  (roadmap lines 519–520). IDs 408–412 are already **five** of the Morph tab's thirteen
  (`kMorphStateCountId` = 408, `kMorphState0Id` … `kMorphState3Id` = 409–412, `plugin_ids.h:110-114`), and
  splitting them across two surfaces would mean the slot a Blend or Tilt edit targets is selected somewhere
  other than where its result is configured. The Edit-mode mini-toolbar *displays* which slot is selected
  and does not duplicate the selector. **Encoded in:** C-3's assignment table (Morph tab, 13 IDs), C-7b's
  sub-controller table (slot selector → `EditMessage` kind 6). *Reconfirmed in the 2026-08-03 clarification
  session (OQ1) — see Clarifications.*
- **RQ-2 — The freeze cluster. RESOLVED: it ships**, as three toggles in the header, and the three IDs
  stay bound in their drawer tabs as well (roadmap lines 520–521). Freeze is named a *"first-class playing
  technique"* (roadmap line 74), and a playing technique that requires opening a drawer and choosing
  between two different tabs is not first-class. Binding one `ParamID` from two views needs no type change
  and no second registration — both views observe the same `Parameter` object — so the cost is three extra
  views and nothing else. **Encoded in:** C-3's duplicate-binding table (bound-view total **110**, allowlist
  exactly `{1008, 1204, 1430}`), FR-003 ("at least one view", with the enumerated duplicates), FR-007
  (7 header-bound views), SC-002 (`== 110` and the allowlist check). *If the phase owner overturns this*,
  the change is mechanical and localised: drop the cluster from the header, set FR-003 back to "exactly
  one", FR-007's total to 4, C-3's total to 107, and SC-002's count to `== 107`. Nothing else moves.
  *Reconfirmed in the 2026-08-03 clarification session (OQ2) — see Clarifications.*
- **RQ-3 — Window sizing. RESOLVED: fixed at 1000 × 700** for this phase (roadmap lines 521–522: *"cloud
  view scales naturally; drawer knob rows are the constraint"*). Resizable means
  `VST3Editor::setAllowedZoomFactors` or a real `onSize` relayout, and the drawer's knob rows are exactly
  the part that cannot reflow without a second layout pass — i.e. the work is in the part the roadmap calls
  the constraint, not the part it says scales. A later phase can add zoom factors without touching any
  parameter, any state, or any of the three view classes' data paths. **Encoded in:** C-1's rect table and
  FR-023/FR-024's exact pixel rects, which is what makes SC-020(c) a byte comparison rather than a "~40 %"
  judgement call. *Reconfirmed in the 2026-08-03 clarification session (OQ3) — see Clarifications.*

- **OQ-4 — `.amount` for C-10's two new macro rows, and SC-017(a)'s octave threshold. RESOLVED (2026-08-03,
  see *Clarifications*): methodology and acceptance band decided; the two numbers are pending the plan's
  pilot measurement, not pending a decision.** Both are **measured, not chosen** — the plan runs the pilot
  sweep and the measured numbers are written back into this spec (SC-017(a), C-10 clause 1), rounded down,
  **before compliance**. The acceptance band is fixed here so the pilot has a pass/fail target rather than
  free rein: the isolated send-return RMS at Dissolve = 1 MUST land between **−20 dB and −6 dB** relative
  to the dry sum, with the five-point sweep strictly monotone; starting values for the pilot are `0.35`
  Dissolve → `FxDelaySend` and `0.50` Entropy → `FxWanderDepth`. Everything *structural* about C-10 was
  already decided. This is not left as a silent placeholder precisely because a threshold nobody measured
  is a threshold that gets relaxed at compliance time — the *measured* number is what gets written back,
  never a convenient one.

  **Write-back status — CLOSED (2026-08-04). All three items are measured and written back.**

  | OQ-4 item | State | Where it now lives |
  |---|---|---|
  | `Dissolve → FxDelaySend` `.amount` | **MEASURED and written back**: `0.20f`, isolated return −19.3 dB, in the ruled band, with the four-row `.amount` × SC-005-ratio table recorded | C-10 clause 1 |
  | `Entropy → FxWanderDepth` `.amount` | **MEASURED and written back**: `0.50f` — the pilot value *survived* the sweep, which is monotone (`0`, `4.64e-4`, `9.28e-4`, `1.39e-3`, `1.86e-3`) and near-linear (ratio `4.01208` vs `4.0`), so the "if not monotone the `.amount` moves" rule was never invoked. Full five-point M/S side-RMS table recorded | C-10 clause 1, and `seraphis_macro_matrix.h` beside the row |
  | SC-017(a)'s octave figure `T` | **MEASURED and written back**: `2.58` (`2.58497` rounded **down**), with the full five-point `P` table. The ruled pilot start was `0.35`, so the criterion got **7.4× stronger** | SC-017(a), and `cloud_frame_test.cpp`'s `kBloomOctaveThreshold` |

  None of the three was resolved by *choosing* a number: each was run as a sweep, the full table recorded,
  and the measured value written back — for `T`, rounded **down** to two decimals, in this spec **and** in
  the test's threshold constant in the same change. Both tables are emitted by the tests themselves, via
  `WARN` on a **passing** run, so a later retune that moves either number reports itself instead of going
  quiet.

---

## Traceability

Every line number below was re-derived against `specs/Seraphis-roadmap.md` on 2026-08-03. Phase 11's prose
block is **479–485**, the sketch **487–503**, the layout commitments **507–517**, the deferred questions
**519–522**, the inherited-mutators paragraph **524–536**.

| Roadmap statement (line) | Where it lands |
|---|---|
| `editor.uidesc` (VSTGUI only, cross-platform) (479) | FR-001, FR-005, SC-019 |
| Cloud view fills the whole window; *"it is the interface, not a panel among panels"* (479–481) | C-1, FR-006, FR-017, SC-020 |
| x = pan, y = freq, size = amp (480, and 497 in the sketch) | C-2 clause 3, FR-013, FR-017 |
| *"Fed via DataExchange piggyback (Membrum MetersBlock pattern — no new queues)"* (481–482) | C-2, FR-010 – FR-016, SC-006, SC-007 |
| *"The five macros are large custom ring knobs anchored at the corners/edges"* (482–483) | C-1, FR-020, SC-004 |
| *"deep parameter sections live in a pull-up drawer along the bottom edge"* (483–484) | C-1, FR-022 – FR-025 |
| *"Custom views get the standard sub-controller treatment (vst-guide skill)"* (484) | **C-7b** (`createSubController` → `DelegationController`), **FR-045**, **SC-022(b)**; teardown in C-7c/FR-041, SC-005 |
| *"No param-type swaps on registered IDs, ever"* (484–485) | FR-004, SC-002, SC-003 |
| Slim header with preset / seed / poly / limit (489, sketch) | FR-007, C-3 |
| Macro rings react — turning a macro perturbs nearby partials (507–508) | FR-021, SC-017 |
| Drawer: ~30 px collapsed, ~40 % open, 7 tabs, cloud view never stops (509–511) | C-1 rect table, FR-022 – FR-024, SC-020 |
| Obs/Edit toggle; drag → `setPartial`; Blend → `blendStates`; Tilt → `tiltState`; *"sole consumer of the inherited mutators"* (512–515) | C-4, FR-027 – FR-029, SC-013 |
| Drawer knobs stay plain uidesc controls; *"Custom-view surface is exactly three"* (516–517) | FR-025, FR-026, **SC-022(a)** |
| Morph slot placement / freeze cluster / window sizing left to this spec (519–522) | **RQ-1, RQ-2, RQ-3** (all resolved in the body: C-3, FR-003, FR-007, FR-023, SC-002, SC-020) |
| Phase 11 owns `setPartial(index, ratio, amplitude)`, `blendStates`, `tiltState` (524–528) | FR-031, C-6; the **amplitude** argument's producer is FR-028's modifier-drag arm + SC-013(d) |
| Phase 3's validity criterion travels with them (528–533) | FR-032, SC-012 |
| Phase 11 owns `setSpectralTarget`, `setPartialPosition`, `setPartialMask` (532–536) | C-4, FR-029, FR-030, FR-033, SC-014 |
| KDD-1 *"life modulators run free"* (71–72) | C-2 clause 3 (drift-inclusive frames); SC-013's drift precondition; SC-017(b)'s measured-ratio control |
| KDD-2 freeze is *"a first-class playing technique"* (74) | RQ-2 → C-3 freeze cluster, FR-007 |
| Cross-cutting: RT safety, layers, ODR, **no bit-exact float goldens** (598), portability (591–602) | FR-015, FR-040, FR-044, *New components* ODR table, SC-008, SC-011, SC-019; and SC-001's explicit statement of why same-instance exact equality is **not** a golden |

**Inherited obligation, not a roadmap line:** Phase 10's RQ-4 — *"Roadmap KDD-1 is discharged by **Phase
11** (macro reach into the effects surface) and **Phase 12** (shipped patches with non-zero sends)"*
(`specs/seraphis-phase10-effects/spec.md:182-183`, `:1867-1876`) → **C-10, FR-037 – FR-039, SC-021**.

**Inherited criterion narrowed, not waived:** Phase 8's **SC-026 clause 2** (*"`setActive(true)` performs
exactly 0 allocations"*, `specs/seraphis-phase8-plugin-scaffold/spec.md:1581-1590`) → narrowed by **C-2
clause 1** to *no allocation on any audio-thread-reachable path*; the host-thread DataExchange queue open
is out of scope and Phase 8's test keeps its exact `== 0` form (D-9 row 9h).

### FR → criterion coverage for the FRs that previously had none

Every row below names an FR whose **only** criterion is the arm in the right-hand column — added by D-9
row 9j precisely because the compliance table is filled against **this document's** Success Criteria list,
and an arm that lived only in `plan.md` was invisible at compliance time. A build could fail any of these
FRs and pass every criterion this spec previously stated.

| FR | What was uncovered | Sole criterion |
|---|---|---|
| **FR-005** (no platform API) | mapped only to SC-019 — builds/portability/clang-tidy — and a platform-guarded native popup compiles clean on all three legs | **SC-011 arm 3** (forbidden-token scan with both anti-vacuity guards) |
| **FR-006** (cloud view is the first child, rings draw over it) | a uidesc comment only; SC-004 arm 1 counts instances and SC-020 checks rects — neither sees child order | **SC-004 arm 3(i)** |
| **FR-011** (handler created in `connect`, destroyed in `disconnect`, activation-driven) | the whole DataExchange line mapped collectively to SC-006/SC-007, both of which pass with the handler permanently null | **SC-006 arm (i)** |
| **FR-014** (the three-clause focus rule) | nothing; a rule that always returned slot 0 passed everything | **SC-006 arm (g)** |
| **FR-016** (controller caches only the most recent block; no background dispatch) | every other arm is producer-side and SC-020 writes the frame cache directly | **SC-006 arm (h)** |
| **FR-017** (axis map monotone, clamped not wrapped) | SC-020 counted redraws; SC-023 asserted zero points | **SC-020 arm (f)** |
| **FR-017 / Q5** (masked partial stays a click target) | nothing — and it is the entire answer to how a user unmasks | **SC-020 arm (g)** |
| **FR-021** (the **view** half: no view-local animation) | SC-017 is measured entirely on the producer, so a faking view passed it | **SC-022 arm (c)** |
| **FR-022** (seven tab names **and** their order) | nothing; a `kTabCount == 7` assert counts, it does not name | **SC-004 arm 2** |
| **FR-025** (exactly one tab page visible at a time) | design prose only | **SC-004 arm 3(ii)** |
| **FR-027** (Observe is the default on **every** open) | design prose only; the failure mode is a mode that survives a close | **SC-004 arm 3(iii)** |
| **FR-028** (the four gestures' emitted messages) | only the plain vertical drag, and only indirectly (SC-024); everything else injected at `notify` | **SC-032** |
| **FR-030 / Q5** (mask toggle **off** restores the voice) | SC-006(e) compares against the processor's table, not the engine; SC-014 only asserts survival | **SC-033** |
| **FR-033a** (the D1 relaxation's CPU cost) | SC-009/SC-010/SC-014 arm 7 run a static slot set; SC-029 measures continuity, not time | **SC-031** |
| **FR-043** (sample-rate change does not invalidate overrides) | — | **SC-014 arm 5** |
| **FR-045** (the header preset button's listener) | the button was tag-less with no owner named (D-5) | **SC-022 arms (b) and (d)** |
| **FR-046** (re-seed from the **state stream**, the second source) | only the dropdown source had a criterion, yet the stream source is the sole justification for the `morph_params.h` widening | **SC-016 arm 2** |
| the macro clearing path (R-3b) | a tracker keyed on `ParamID` 207 is blind to a Bloom sweep and silently wipes every pan override | **SC-014 arm 6** |
| the re-push worst case | 64 × 16 × 2 = 2048 transcendentals in one block, previously assumed bounded | **SC-014 arm 7** |
| every Phase 11 atomic is lock-free | only `std::atomic_flag` is *guaranteed* lock-free (root `CLAUDE.md`) | **SC-011 arm 2** |

---

## Review notes

This revision resolves an adversarial review of the previous draft. Nothing below relaxes a threshold; the
two numbers that moved (SC-017's metric, SC-008's tolerances) moved because the originals were
dimensionally meaningless or below float epsilon, not because they were hard to hit.

1. **KDD-1 / RQ-4 — resolution (a) was taken, not (b).** The review offered either routing a macro axis
   into the effects targets, or recording a numbered Open Question with a *named* later owner plus a
   roadmap amendment. **(a) was chosen** because there is no honest later owner to name: the roadmap's
   remaining phases are 12 (factory presets / release, lines 538–546) and 13 (per-note expression, lines
   548–569), and a macro-routing job belongs to neither. The previous draft's reading — that exposing the
   effects controls in the FX drawer tab discharges *"macro reach into the effects surface"* — is
   withdrawn; C-10 routes Dissolve and Entropy into two effects targets, and *Non-goals* now says
   explicitly that drawer exposure alone does **not** discharge RQ-4. RQ-4's other half (shipped patches
   with non-zero sends) stays with Phase 12 exactly as RQ-4 assigned it, so no roadmap amendment is needed.
2. **Sub-controller — the pattern was adopted, not deviated from.** The previous draft chose
   `createCustomView` + a `ViewCreatorAdapter` and claimed the roadmap's sub-controller line in the
   Traceability table without implementing the mechanism the cited skill defines. C-7b now adds the
   `DelegationController`, names every tag-less control it owns, and FR-045 + SC-022(b) make it testable;
   the Traceability row points at those rather than at C-7/SC-004.
3. **The previous draft's OQ-2 (now RQ-2) was decided rather than left contradicting FR-003.** The freeze cluster ships (RQ-2), and every
   dependent number moved with it: FR-003 ("at least one view" + an enumerated three-element duplicate
   allowlist), FR-007 (7 header-bound views), C-3 (110 bindings), SC-002 (`== 110`, not `≥ 107`). The `≥`
   hedge is gone — it could detect neither a missing binding nor an unintended duplicate.
4. **Three unsatisfiable or vacuous criteria were rebuilt, and the reasons are recorded inline** so a later
   reader does not "restore" them: SC-001 (was a forbidden cross-build bit-exact comparison whose two arms
   were the same code path), SC-012 (was universal validity over a table whose inputs C-6 defines as
   no-ops, observed through `getStateCount`, which `setState` never writes), SC-017 (required a
   `SeraphisMacroMatrix` suppression seam that does not exist and could not be added inside this phase's
   scope, and stated a threshold as a percentage of a bare logarithm), SC-020 (drove the timer and then
   asserted a rate the test itself chose), SC-010 (asserted a number that is zero by construction), and
   SC-007 (asserted a success count that queue exhaustion makes unreachable).
5. **No issue was rejected.** Every finding in the review was applied. The two factual corrections
   (`VST3EditorDelegate` already present at `controller.h:23-24`; the `static_assert` at
   `param_perf_test.cpp:1101`, not `:1097`) and the arithmetic correction (IDs 408–412 are **five**, not
   six) were verified against the files this session before being applied.
6. **`exp10f` is banned by name in C-6**, with the repo's existing precedent cited
   (`continuous_body.h:1643-1645`), so FR-044's portability gate does not have to be the thing that finds it.


---

## AMENDMENT (2026-08-04, phase owner) — shared-component consistency pass

> **Directive:** the shipped editor used stock `CCheckBox` and capsule-styled `CTextButton` views where
> every other Krate plugin uses the shared component library. Ruled after ship, applied same day.
>
> 1. **Every toggle is `Krate::Plugins::ToggleButton`** (registered ViewCreator, `plugins/shared/src/ui/
>    toggle_button.h`): the header SoftLimit + freeze cluster, all seven drawer-page toggles, the
>    Obs|Edit mode toggle (session-tag `mode`, title "EDIT") and the drawer handle (session-tag
>    `drawerHandle`, chevron icon, off-orientation up / on-orientation down). `CCheckBox` appears
>    nowhere; SC-003's `T` class set is now `{"ToggleButton"}` (still a singleton — the criterion's
>    either-choice detection argument is preserved).
> 2. **The seven drawer tabs and four morph slot buttons are TWO `Krate::Plugins::IconSegmentButton`
>    bars** (the Ruinae MainTab idiom — flat, outlined, transparent): session-tags `tabs` and `slots`
>    replace `tab0..tab6` / `slot0..slot3` in C-7b; the selected index rides the control's normalized
>    value (`round(v * (N-1))`, IconSegmentButton's own convention). `kTabBarTag`/`kSlotBarTag` keep the
>    9100/9200 values. FR-022's seven names, in order, are the bar's `segment-names`; SC-004 arm 2 reads
>    them back off the built control.
> 3. **The header preset button is the shared flat-outline renderer** every plugin's Presets button
>    uses: `createCustomView("PresetButton")` returns a `Krate::Plugins::OutlineBrowserButton`
>    (session-tag `preset` still assigns tag 9000 + listener via verifyView; one valueChanged per
>    click). This instantiates a SHARED class — the phase's closed roster of NEW view classes
>    (CloudView, MacroRingKnob, DrawerContainer) is unchanged.
> 4. FR-025's permitted drawer-control list reads `ArcKnob / CSlider / COptionMenu / ToggleButton`
>    (+ the IconSegmentButton bars); the binding budget (110 over 107) and the freeze-cluster duplicate
>    allowlist are untouched.
> 5. `entry.cpp` gains the `<ui/toggle_button.h>` and `<ui/icon_segment_button.h>` creator includes
>    (inline-global registration requires a linked TU, same reason as ArcKnob/MacroRingKnob).

> **AMENDMENT ADDENDUM (2026-08-04, decision-coverage audit).** The audit that followed the Q6
> half-build found ONE more log-only clause: Q4's view half. The frame's `morphTravelPosition` and the
> producer write existed (T008), but no indicator was ever built. Now enforced as:
>
> - **SC-034 — the Edit-mode "not currently sounding" mark (Q4).** `CloudView::selectedSlotContributes()`
>   mirrors `SpectralMorphEngine::slotContributes` (slot == floor(pos) or floor(pos)+1,
>   `spectral_morph_engine.h:565-569`) against the frame's `morphTravelPosition`; with no live frame it is
>   trivially true (the drawn constellation IS the slot). Edit mode draws its border amber (2 px) when the
>   selected slot does not contribute, accent (1 px) when it does. *Test:*
>   `Seraphis_CloudView_SelectedSlotContributionIndicator` (`tests/unit/controller/custom_view_test.cpp`),
>   covering the no-frame case and both verdicts on each side of two journey positions.
> - Q6's drawing half is likewise now enforced by the regression case
>   `Seraphis_CloudView_EditModeDrawsTheSlotWhileSilent` (same TU): Edit mode with an EMPTY live frame
>   draws the selected slot's authored partials against the C4 fallback (centred column), gate
>   `partialCount == 0` so frames carrying release tails still draw as frames (SC-024 unaffected).
> - Every other Clarifications decision was audited against its implementation this session: Q1 (mirror),
>   Q2 (BlendBegin latch), Q3 ([partials] block), Q5 (maskBits + hollow rings), Q7 (controller refcount),
>   Q8 (throttle + terminal flush), OQ1-3 and D1 (SC-028/029/030) all verified built. The workflow now
>   requires every decision clause to cite an enforcing FR/SC (seraphis-phase.js, 2026-08-04).
