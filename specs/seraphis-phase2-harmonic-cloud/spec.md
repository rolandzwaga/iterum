# Feature Specification: Seraphis Phase 2 — Harmonic Cloud Oscillator

**Spec slug:** `seraphis-phase2-harmonic-cloud`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 2 (lines 137–164)
**Layer:** one new component at Layer 3 (`dsp/include/krate/dsp/systems/harmonic_cloud.h`)
**Depends on:** Phase 1 (`seraphis-phase1-life-modulators`, COMPLETE — `BrownianDrift` shipped)
**Plugin work:** none (KrateDSP-only, unit-tested; the Seraphis plugin starts at Phase 8)

## Overview

The Harmonic Cloud is Seraphis's primary sound source: a 64-partial additive bank in which every partial
is an individual living entity with its own stereo position, drift amount, and envelope offset. Phase 2
adds exactly one new Layer 3 component, `HarmonicCloud`, which composes the already-proven SIMD MCF
kernel from Innexus — `processMcfBatchSIMD(...)`, declared in
`dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.h:33-46` — with new per-partial state and
per-partial `BrownianDrift` life modulation (`dsp/include/krate/dsp/processors/brownian_drift.h:94`).
Unlike Innexus, which *analyses* recorded sound into partials, Seraphis *generates* partial worlds from
five macro parameters: Richness, Inharmonicity, Spectral tilt, Mutation, and Spectral gravity (roadmap
lines 149–154). There is deliberately **no analysis pipeline and no `HarmonicFrame` dependency** (roadmap
lines 157–159) — only the SoA/SIMD synthesis layout is shared. This is a foundations phase: it delivers a
reusable, unit-tested, RT-safe DSP building block plus FFT-based spectral verification of the three
parameter laws; it does not deliver a voice, an envelope architecture, morphing, or any plugin surface.

## Scope

In scope for this phase:

- One new Layer 3 component, `HarmonicCloud`, at `dsp/include/krate/dsp/systems/harmonic_cloud.h`,
  covering exactly the roadmap's Phase 2 component list (lines 145–155) and nothing more.
- Reuse of the existing SIMD MCF batch kernel and the existing MCF/anti-alias/amplitude-smoothing math
  (verified in `harmonic_oscillator_bank.h`, see Existing Components) — re-implemented over
  Seraphis-owned SoA state rather than inherited from `HarmonicOscillatorBank`, because that class's
  entire input contract is `HarmonicFrame` (`loadFrame`, `harmonic_oscillator_bank.h:254`).
- Per-partial `BrownianDrift` life modulation in **two independent banks** (detune and mutation, 128
  instances per cloud), decimated to one drift read per partial per **64-sample internal control chunk**
  (FR-032).
- The five macro parameter mappings, each with an FFT-verifiable definition.
- Stereo output via per-partial equal-power pan.
- A small, permanent **test/introspection surface** on the component (FR-008) — per-partial accessors plus a
  partial-solo facility — without which several success criteria are unmeasurable.
- Unit tests for the roadmap's success criteria (frequency accuracy, zipper-freedom, CPU budget) plus
  RT-safety, determinism, sample-rate change, anti-aliasing, and per-macro behavioural criteria for
  Richness, drift, mutation bounds and onset phase incoherence (SC-014 … SC-018).

## Non-Goals (owned by later phases)

- **Spectral states, morphing, and the entropy macro.** `SpectralState`, `SpectralMorphEngine` and
  `EntropyProcessor` are Phase 3 (roadmap lines 168–190). Phase 2's Mutation macro is a *local* slow
  re-weighting of partial amplitudes (roadmap line 152), not a travel between named states, and must not
  anticipate Phase 3's data model.
- **The resonant body and granular atmosphere.** Phases 4 and 5. `HarmonicCloud` produces a stereo signal;
  what consumes it is not this phase's concern.
- **Voice envelope, note allocation, per-voice seed spreading, spatial azimuth wandering.** Phase 7
  (roadmap lines 281–288). Phase 2's per-partial attack/decay offsets are *relative* offsets inside the
  cloud; the voice-level `MultiStageEnvelope`/`GrowthEnvelope` amplitude envelope stays Phase 7's. The one
  committed Phase-2 → Phase-7 entry point is FR-008's `setPartialPosition(index, position)` setter
  (Clarifications Q4), which per-voice azimuth wandering will write through; nothing else in this spec
  anticipates Phase 7.
- **Modulation routing.** Wiring `BrownianDrift` (or the cloud's parameters) into `ModulationEngine` /
  `VoiceModRouter` enum slots was explicitly deferred to Phase 7 by Phase 1's Clarifications OQ1
  (`specs/seraphis-phase1-life-modulators/spec.md:49-51`). Phase 2 owns its drift instances directly.
- **Any analysis, `HarmonicFrame`, `HarmonicSnapshot`, or `harmonic_frame_utils` dependency** (roadmap
  lines 157–159). Those Innexus types are analysis-side and are explicitly *not* reused; see the Existing
  Components table for the verified reason.
- **Any plugin, parameter ID, UI, preset, or macro-system work** (Phases 8–12). The five macros here are
  plain C++ setters in normalized/physical units, not VST3 parameters.

## Clarifications

Decisions that close the roadmap Open Questions assigned to this spec. They are binding; nothing here is
deferred to implementation time.

- **OQ-1 — Exact partial count: fixed 64, or 32/64/128 quality tiers? (roadmap line 494) — DECIDED: fixed
  64.** `HarmonicCloud` ships a compile-time constant `kMaxPartials = 64`, matching the roadmap's headline
  figure ("64-partial additive bank", line 140) and its Phase 2 CPU criterion ("64 partials + drift ≤ 0.5%
  of one core", line 162). A 32/128 quality tier — and therefore any runtime partial-count-change path — is
  **out of scope for Phase 2**. Two later-phase re-open paths exist, neither of them Phase-2 work: (a) if the
  SC-007 measurement lands at or below 50% of the 53.3 µs/block budget (≥ 2× headroom), Phase 7 may revisit
  the capacity upward; (b) if SC-007 measures *close* to the budget, a later phase may introduce quality
  tiers **as a static cap chosen at `prepare()`** — never as a change of the active maximum while sounding.
  Neither is needed now: Richness (FR-041) already scales the *active* partial count from 1 to 64
  dynamically, and inactive partials cost no CPU.
  *Why decide now rather than after measuring:* the roadmap says the count is "driven by measured CPU", but
  a runtime tier is a **new code path** (changing the active maximum while sounding, which FR-043 would have
  to cover click-free) that no FR describes, and leaving it open would leave the component's core dimension —
  and its array sizes — undecided at plan time. The measurement still has teeth: it constrains SC-007's
  baseline (see SC-007), it just no longer decides the capacity.
  *Consequence:* FR-012, SC-007 and SC-011 are each stated at a single partial count, 64. No FR anywhere in
  this spec describes a partial-count change path, and none may be added without re-opening this decision.

### Session 2026-07-25 — spec-internal clarification interview

Eight spec-internal underspecifications (raised by the clarification scan) plus the roadmap Open Question,
all decided by the user. Every decision below is already encoded in the FRs, SCs and Assumptions named in
its row — this log is provenance, not a place behaviour is defined.

| ID | Question | Decision |
|---|---|---|
| Q1 | Does Mutation read the *same* per-partial `BrownianDrift` instance that drives detune, or its own? | **Its own.** A second per-partial drift bank (128 OU walks per cloud), own derived seed per partial, depth pinned at 1.0 internally; the Mutation control scales the applied weight. Mutation and Drift are fully independent — drift depth 0 must NOT disable Mutation. Encoded in FR-031, FR-035, FR-071, FR-072; added cost covered by SC-007's gate; independence measured by SC-016. |
| Q2 | What is the exact Richness law (control → active count, amplitude rolloff)? | **Exponential count plus rolloff-exponent interpolation:** `N(r) = round(64^r)` (saturates at `kMaxPartials` at the top of the range) and `a_n = n^(−p)` with `p` linear from 3.0 at r = 0 to 0.5 at r = 1. Active count is explicit state exposed via FR-008; inactive partials cost no CPU. Encoded in FR-041, FR-042, FR-008; SC-003's pinned Richness setting and SC-014's thresholds restated against it. |
| Q3 | What exactly is the FR-023 envelope — what does "decay" mean, and what is the segment curve? | **Linear AR.** Linear attack to target (attack time = time-to-100%), hold at target while gated, linear fall to 0 over the decay time after gate-off — **decay *is* the release**; there is no separate release stage. This is the reading under which every SC-013 clause is satisfiable. Encoded in FR-023 and Assumption 2. |
| Q4 | How is each partial's stereo position assigned, and what API lets SC-012 measure the pinned grid? | `position_i = spread × s_i` with `s_i` drawn once per seed from U[−1, +1], **plus** a public `setPartialPosition(index, position)` on FR-008's surface. SC-012 exercises the real shipped conversion path at the exact grid {−1, −0.5, 0, +0.5, +1}; the setter is the committed Phase 7 per-voice azimuth hook. Encoded in FR-008, FR-021, SC-012, Non-Goals. |
| Q5 | What happens on a note-on that arrives while the cloud is still sounding (retrigger)? | **Re-randomize phases only when quiescent** — gate off AND every partial's `currentAmplitude` below a documented floor. Otherwise keep phases and re-open the envelope from its current value. Zero-click by construction; determinism still holds under SC-009's fixed call sequence. Encoded in FR-016, FR-023; SC-006 gains a retrigger clause using the same differential click test; Edge Cases updated. |
| Q6 | What is FR-017's normalization basis, and are Mutation/drift inside or outside its input? | **Expected-RMS over the UN-mutated post-Richness/post-tilt target amplitudes:** `gain = kTargetOscRms / sqrt(Σ a_i² / 2)`, capped and one-pole smoothed, following the proven pattern at `harmonic_oscillator_bank.h:338-357` (`kTargetOscRms = 0.5f` :94, `kMaxNormGain = 20.0f` :97). Mutation weights, drift and the per-partial envelope are explicitly **outside** the normalizer input, stated in the header, so SC-016 and SC-018 keep their teeth. Encoded in FR-017. |
| Q7 | Is the FR-032 "one drift read per block" literal at any block size, or chunked internally? | **Chunked.** The cloud subdivides its own render loop at a documented 64-sample internal control chunk and reads each partial's drift once per chunk, so the sound is block-size-invariant. SC-015 clause 2 is restated as reads-per-chunk (`ceil(blockSize / 64)` reads per partial per block). Encoded in FR-032, FR-034, FR-074, SC-015, Edge Cases. |
| Q8 | How is each partial's FR-022 drift *amount* assigned? | **One documented law, index-scaled AND seeded scatter:** `amount_i = (n / kMaxPartials)^k × u_i` with `k = kDriftIndexExponent = 1.0` and `u_i ~ U[0.5, 1.0]` drawn once per seed. Upper partials wander more (physical decoherence) and partials are individually distinct. Encoded in FR-022; bound and liveness measured by SC-015 clause 3. |
| OQ1 | Exact partial count — fixed 64, or 32/64/128 quality tiers? (roadmap line 494) | **Fixed 64.** No quality tiers in Phase 2 — Richness already scales the active count 1 → 64 dynamically. If SC-007 measures close to budget, tiers may be introduced by a later phase as a **static** cap; not now. Encoded in the OQ-1 entry above and FR-012. |

## Assumptions (recorded interpretations, not deferrals)

These readings of ambiguous roadmap wording are stated here so the plan does not have to re-derive them.
They are *decisions*, not open questions.

1. **"Per-partial `BrownianDrift` (shared-state, decimated…)" (roadmap line 147)** is read as: every
   partial owns its own `BrownianDrift` instance with its own seed (so partials drift independently — the
   whole point of "each partial is an individual living entity"), but all instances of a bank *share
   configuration* (depth, smoothness) set once from the cloud's drift controls, and each is advanced via
   `BrownianDrift::processBlock(size_t)` (`brownian_drift.h:194`) once per internal control chunk
   (FR-032). "Shared-state" therefore means shared *parameter* state, not a shared random walk — a single
   shared walk would move all partials in lockstep and contradict line 145.
   **Two banks, not one** (Clarifications Q1): the detune bank of FR-031 and a second, independently
   configured mutation bank of FR-072 — 128 `BrownianDrift` instances per cloud. A single shared bank would
   make the Mutation macro a slave of the drift-depth control (`BrownianDrift::setDepth` scales the emitted
   value, `brownian_drift.h:159-161`, clamped into the fixed range at :212-214), so drift depth 0 would
   silence Mutation at every setting — which every mutation bound in this spec already assumes it does not.
2. **"individual attack/decay offsets" (roadmap line 146)** implies the cloud owns a per-partial amplitude
   envelope with a *cloud-level* base attack time and base decay time plus a per-partial offset spread,
   gated by a cloud-level note-on/note-off. **This is deliberately broader than roadmap line 146, which
   names only the offsets**, and it is a Phase-2 *testability* decision, recorded here rather than smuggled
   into an FR: an offset is only measurable relative to a base time, and SC-013 has to observe staggered
   onsets with no voice in existence (Phase 7 owns the voice).
   **How Phase 7 supersedes it:** the voice-level amplitude envelope stays Phase 7's (`MultiStageEnvelope`
   with a `GrowthEnvelope` attack mode, roadmap lines 283–284) and multiplies the cloud's stereo output; at
   that point the cloud's base times stop being the primary shaping control and act purely as the reference
   the per-partial stagger is expressed against, and the cloud gate becomes a slave of the voice gate. No
   Phase-2 API is removed by that transition — the base-time setters survive with a narrower role, so there
   is nothing for Phase 7 to unwind. To keep it that way, Phase 2's envelope is **exactly** {attack time,
   decay time, offset spread, gate} and its shape is a **linear AR** (Clarifications Q3): linear attack to
   the target amplitude — the attack time is the time-to-100%, not a time constant — hold at target while
   the gate is on, linear fall to 0 over the decay time after gate-off. **Decay *is* the release**: there is
   no *separate* release stage, no sustain-level control, no multi-stage segments, no velocity or key
   scaling, and no exponential-segment variant. This is the reading under which every SC-013 clause
   (including the "≥ 95% of target" clause, which needs a steady state to be 95% of) is satisfiable.
3. **"reuse `spectral_tilt` math" (roadmap line 151)** is read as reusing the *per-partial gain law*
   `gain(n) = 10^(tiltDb · log2(n) / 20)` verified in `additive_oscillator.h:480-490`, **not** the
   `SpectralTilt` IIR dual-shelf filter (`spectral_tilt.h:88`). For an additive bank the closed-form
   per-partial gain is exact and free; the IIR filter would add phase and only approximates the slope
   (`spectral_tilt.h:349-386` uses a heuristic `kReferenceMultiplier = 1.5f`). `SpectralTilt`'s parameter
   *range convention* (±12 dB/oct, `spectral_tilt.h:98-101`) is still adopted.
4. **"Spectral gravity — pulls partial ratios toward/away from pure harmonic grid" (roadmap line 153)** is
   read through the roadmap's own gloss on the following line — "(0 = pure harmonic, ± = stretched/compressed
   toward inharmonic clusters)" (line 154) — which is taken as authoritative where the two differ. Gravity is
   therefore an explicit power-law warp of the *integer* grid (FR-081), whose only on-grid setting is
   gravity 0. "Toward the grid" is realized as the continuous return to the exact integer grid as
   |gravity| → 0; "away from the grid" is |gravity| → max, the sign selecting stretch (+) or compression (−).
   **Recorded deviation from roadmap line 153:** Phase 2 does *not* implement an operator that pulls an
   already-inharmonic spectrum (`B ≠ 0` per FR-051) back onto the integer grid. Gravity and Inharmonicity
   compose multiplicatively in the fixed order of FR-083 and no gravity setting cancels a non-zero `B`.
   Re-harmonizing an arbitrary spectrum, if ever wanted, belongs with Phase 3's `SpectralState` machinery.
5. **"per-partial freq/amp/phase" (roadmap line 145)** requires a *source* for initial phase that Seraphis
   does not have. The one in-repo precedent seeds each partial's MCF state from an analysis frame
   (`float phase = partial.phase; sinState_[i] = std::sin(phase); cosState_[i] = std::cos(phase);`,
   `harmonic_oscillator_bank.h:288-290`, again at :467-469), and roadmap lines 157–159 forbid any
   `HarmonicFrame` input, so that source is gone with no replacement in the roadmap. Defaulting all 64
   partials to phase 0 would make them sum coherently at onset (peak ≈ Σ amplitudes), colliding with FR-006's
   output clamp and reading as an onset discontinuity under SC-005/SC-006. **Decision:** initial phase is
   drawn from the seeded cloud RNG — FR-016, measured by SC-018.

## Functional Requirements

Each requirement is testable and traces to a specific roadmap line.

### FR-001 series — shared contract and lifecycle

- **FR-001** — `HarmonicCloud` is a Layer 3 component at
  `dsp/include/krate/dsp/systems/harmonic_cloud.h`, including only Layer 0/1/2 headers and stdlib. Trace:
  roadmap line 143; layer discipline (roadmap line 482).
- **FR-002** — `HarmonicCloud` is real-time safe: all processing and parameter methods are `noexcept` and
  perform no heap allocation, locks, exceptions, or I/O. All partial state is fixed-size member storage
  sized at compile time; `prepare()` is the only non-RT method. Trace: roadmap line 481.
- **FR-003** — `HarmonicCloud` exposes `prepare(double sampleRate) noexcept` and `reset() noexcept`.
  `prepare` re-derives every sample-rate-dependent coefficient (MCF epsilon, amplitude-smoothing
  coefficient, envelope rates, anti-alias fade points) and propagates the sample rate to every owned
  `BrownianDrift` via its `prepare(double)` (`brownian_drift.h:121`). `reset()` silences all partial state
  without changing configuration. After `prepare`, processing is well-defined with no prior parameter
  call.
- **FR-004** — `HarmonicCloud` renders stereo audio through a block method that fills separate left and
  right buffers for `numSamples` samples, matching the existing bank's stereo contract
  (`HarmonicOscillatorBank::processStereoBlock(float*, float*, size_t)`,
  `harmonic_oscillator_bank.h:803`). A zero-length block is a no-op; null buffers are rejected without
  writing. Trace: roadmap line 155 (stereo output).
- **FR-005** — All stochastic behaviour (per-partial drift, mutation re-weighting, any per-partial scatter)
  is driven by seeded `Xorshift32` streams (`dsp/include/krate/dsp/core/random.h:40`) reachable through an
  explicit cloud-level seed setter, so a given seed plus a given call sequence reproduces the same render.
  Trace: Phase 1's determinism criterion (roadmap line 131, "determinism with seeded `random.h`") carried
  forward; Phase 7's determinism-harness success criterion (roadmap lines 300–301) depends on it. *(There is
  no cross-cutting determinism constraint — the cross-cutting block is roadmap lines 481–489 and contains
  none; this trace is to the two phase-level criteria only.)*
- **FR-006** — Output is bounded: the summed stereo output is hard-clamped to a documented safety limit
  (the existing bank uses `kOutputClamp = 2.0f`, `harmonic_oscillator_bank.h:90`) and no NaN/Inf may reach
  the output. The quantifier is made finite and testable by SC-017, which defines the exact parameter grid
  over which "any parameter combination" is verified; FR-006 is satisfied when SC-017's grid passes.
  Non-finite detection, in guards and in tests, uses bit-pattern inspection, never `std::isnan` (macOS CI
  builds `-ffast-math`); the existing bank's `stateFinite()` bit test
  (`harmonic_oscillator_bank.h:622-633`) is the pattern to follow.
- **FR-007** — Every parameter setter clamps its input to a documented range. Non-finite input is
  **rejected**, not "steered to a default": on a NaN or Inf argument the setter returns without touching
  any state, so the corresponding getter and the rendered output are bit-identical to what they were before
  the call. (One behaviour, one assertion — the two-outcome phrasing would have made this unverifiable.)
  The rejection idiom is the finite-first comparison used by
  `AdditiveOscillator::setSpectralTilt`/`setInharmonicity` (`additive_oscillator.h:318-340`), which works
  under `-ffast-math` because a NaN fails every comparison. Verified by SC-017.
- **FR-008** — `HarmonicCloud` exposes a **test/introspection surface** so SC-001, SC-003, SC-012, SC-013,
  SC-015 and SC-018 are measurable at all. Without it those criteria specify measurements the rest of this
  spec's API cannot perform (Richness sets count *and* rolloff jointly per FR-041, so it cannot isolate
  partial 8, 32 or 64; pan gains and per-partial envelopes are otherwise invisible). Required, all `const
  noexcept` unless noted, all cheap enough to call from a test loop, none of them on the audio path:
  - per-partial **synthesized frequency** in Hz, **current amplitude**, **target amplitude**, the
    **un-mutated target amplitude** (the post-Richness/post-tilt/post-normalization value that FR-017's gain
    is derived from, so SC-016 can form the mutation weight as a ratio), **panLeft/panRight**, **stereo
    position**, and **current drift detune** (as a frequency multiplier);
  - a **partial-solo/mask** facility (`setPartialMask(...)` or `soloPartial(index)`) that forces every
    partial's target amplitude to zero except the chosen index or set — configuration-only, RT-safe, and
    itself subject to FR-014's smoother so using it cannot click;
  - a **`setPartialPosition(std::size_t index, float position) noexcept`** setter (the one non-`const`
    member of this surface) that places one partial at an exact stereo position in [-1, +1], overriding the
    FR-021 seeded scatter for that index until the next spread change, re-seed or `reset()`. Without it
    nothing in the component can place a partial at exactly −0.5 and SC-012's pinned grid is unmeasurable
    on the shipped conversion path. It is also the committed Phase-7 per-voice azimuth hook
    (Clarifications Q4). Out-of-range positions clamp per FR-007; an out-of-range index is a no-op;
  - the effective **active partial count** — under FR-041 this is explicit state (`N(r) = round(64^r)`), not
    a measurement derived from the rendered spectrum.
  The reference class exposes exactly this kind of surface (`getStereoSpread()`
  `harmonic_oscillator_bank.h:614`, `stateFinite()` :622), so this is not a new pattern. These accessors are
  part of the component's public contract, not `#ifdef TESTING` scaffolding.

### FR-010 series — partial bank core (roadmap line 145)

- **FR-011** — `HarmonicCloud` synthesizes its partials with the Gordon-Smith Modified Coupled Form
  recurrence (`sNew = s + eps·c; cNew = c - eps·sNew`) driven by the existing SIMD batch kernel
  `processMcfBatchSIMD(...)` (`harmonic_oscillator_bank_simd.h:33-46`), over Seraphis-owned SoA arrays
  laid out to that kernel's parameter contract (`sinState`, `cosState`, `epsilon`, `detuneMultiplier`,
  `currentAmplitude`, `targetAmplitude`, `antiAliasGain`, `panLeft`, `panRight`, `ampSmoothCoeff`,
  `numPartials`, `sumL`, `sumR`). The `sinState`/`cosState` arrays are *initialized* per FR-016 (seeded
  random phase), not zeroed. Trace: roadmap line 145 ("Composes `HarmonicOscillatorBankSimd`");
  roadmap line 159 ("the bank internals and SIMD layout are shared").
- **FR-012** — The cloud's partial count is a **fixed compile-time `kMaxPartials = 64`** (Clarifications
  OQ-1); there is no runtime tier and no partial-count-change path. SoA arrays are declared `alignas(32)` as
  the existing bank does (`harmonic_oscillator_bank.h:1155-1163`). The kernel is called with unaligned
  Highway loads/stores (`hn::LoadU`/`hn::StoreU`, verified `harmonic_oscillator_bank_simd.cpp:80-110`), so
  any *active* partial count is safe — no alignment assumption may be introduced. Trace: roadmap line 140
  ("64-partial additive bank").
- **FR-013** — The cloud is pitched by a fundamental-frequency setter in Hz, clamped to a documented
  **supported fundamental range of 20 Hz – 4000 Hz** (referenced by FR-052, SC-011 and the Edge Cases;
  4 kHz keeps the fundamental itself well below Nyquist at 44.1 kHz while letting high partials exercise the
  FR-015 fade). Changing the fundamental recomputes every partial's MCF epsilon **without resetting phase
  accumulators**. Phase continuity alone is not sufficient for large jumps: recomputing epsilon changes the
  MCF orbit eccentricity, which is why the reference arms a short output crossfade when the pitch ratio
  exceeds one semitone (`crossfadeThresholdRatio_ = semitonesToRatio(1.0f)`,
  `harmonic_oscillator_bank.h:190`; jump detection at :385-401; `kDefaultCrossfadeTimeSec = 0.003f` at :81;
  applied to the summed output at :782-788). **Phase 2 adopts that mechanism too:** a fundamental change
  whose ratio exceeds a documented threshold arms an equally short output crossfade. Verified by SC-006's
  fundamental-step clause. Trace: roadmap line 145 (per-partial freq/amp/phase).
- **FR-014** — Per-partial amplitude changes reach the output through a one-pole amplitude smoother
  (the kernel's `ampSmoothCoeff` input, derived as in `harmonic_oscillator_bank.h:136-137`), so no
  parameter change produces a step in partial amplitude. That coefficient is a **single scalar shared by
  every partial** (`float ampSmoothCoeff`, `harmonic_oscillator_bank_simd.h:43`, applied uniformly as
  `amp += coeff * (target*aa − amp)`, :27): it is a uniform de-zippering stage *downstream* of everything
  else, **not** an envelope mechanism and not a place per-partial timing can live (see FR-023). Its time
  constant follows the reference's ~2 ms (`kAmpSmoothTimeSec = 0.002f`,
  `harmonic_oscillator_bank.h:83-84`) and must stay short relative to the shortest supported per-partial
  attack time. Trace: roadmap SC "no zipper noise under mutation/drift" (lines 161–162).
- **FR-015** — Partials whose synthesized frequency approaches or exceeds Nyquist are amplitude-suppressed
  before they can alias: full gain below a documented fraction of Nyquist, fading to zero at Nyquist, with
  the MCF elliptical-orbit correction `cos(π·f/fs)` applied, and epsilon clamped to the MCF stability bound
  |eps| < 2 — the mechanism verified in `harmonic_oscillator_bank.h:1047-1090` (`kAntiAliasFadeStart = 0.8f`
  at :87, `kMaxEpsilon = 1.99f` at :1050). Trace: **spec-added, not a roadmap line.** Aliasing is not
  among the three verifications the roadmap names for Phase 2 (line 164 lists tilt, inharmonicity law and
  gravity mapping) and appears nowhere in lines 137–164. It is a hard Phase-2 requirement because the
  FR-050 and FR-080 macros both push high partials upward in frequency by design, and because the mechanism
  is already inherited from the reuse target. Measured by SC-011.
- **FR-016** — **Per-partial initial phase is randomized** from the seeded cloud RNG (FR-005): each partial's
  MCF state is initialized as `sinState[i] = sin(φᵢ)`, `cosState[i] = cos(φᵢ)` with φᵢ drawn i.i.d. uniform
  on [0, 2π) from the cloud's `Xorshift32` stream (`nextUnipolar()`, `random.h:66`), the distribution
  documented in the header. Rationale and the missing roadmap source are in Assumption 5. Consequence:
  partials do **not** sum coherently at onset, so the onset peak is far below `Σ amplitudes` and below
  FR-006's clamp. Because the phases come from the seeded stream, FR-005's determinism and SC-009 are
  unaffected. Trace: roadmap line 145 ("per-partial freq/amp/**phase**"); Assumption 5. Measured by SC-018.
  - **When the redraw happens (retrigger rule, Clarifications Q5):** phases are redrawn at `reset()` and at
    a note-on **only while the cloud is quiescent** — gate off **and** every partial's `currentAmplitude`
    below a documented floor `kQuiescenceAmplitude` (−100 dBFS, i.e. `1.0e-5f`, documented in the header).
    A note-on arriving while the cloud is still sounding **keeps every partial's phase** and merely re-opens
    the envelope (FR-023), so retrigger is click-free *by construction* rather than by masking: no MCF state
    is stepped discontinuously, so there is no discontinuity for FR-013's crossfade to hide. Two renders
    with the same seed but different retrigger timing legitimately differ; determinism is unaffected because
    SC-009 pins the call sequence. Measured by SC-006's retrigger clause.
- **FR-017** — **Cloud output level is normalized**, so that no combination of Richness (FR-041) and Tilt
  (FR-061) can drive the summed output into FR-006's clamp. The basis is pinned (Clarifications Q6), not
  left to implementation: **expected RMS over the un-mutated partial amplitudes**, following the verified
  pattern at `harmonic_oscillator_bank.h:338-357`:

  > `a_i = richnessRolloff(i) · tiltGain(i)` (the FR-041 and FR-061 laws only)
  > `gain = min(kTargetOscRms / sqrt(Σ a_i² / 2), kMaxNormGain)`, with `kTargetOscRms = 0.5f`
  > (`harmonic_oscillator_bank.h:94`) and `kMaxNormGain = 20.0f` (:97)

  **The amplitude composition chain is fixed and documented in the header:**
  `targetAmplitude_i = gain_smoothed · a_i · w_i(mutation) · env_i(t)`. The normalizer's input is the
  `a_i` set **only** — the mutation weights `w_i` (FR-071), the per-partial drift, and the per-partial
  envelope `env_i` are explicitly **outside** it. That exclusion is the requirement, not an implementation
  detail: recomputing the gain from mutated amplitudes would cancel exactly the level movement SC-016's
  ±3 dB window exists to observe, and would make the Mutation macro inaudible in level terms.
  The gain is **smoothed** (a cloud-level `OnePoleSmoother`, `smoother.h:134`) so normalization changes are
  themselves click-free, and it is a *single scalar across all partials*, so it cannot change the measured
  tilt slope or any partial ratio.
  *Headroom.* An RMS basis does not bound the peak arithmetically; it leaves **11.1 dB of crest headroom**
  between the 0.5 target RMS and SC-018's `0.9 · kOutputClamp = 1.8` ceiling, and FR-016's incoherent
  initial phases are what keep the crest factor of a 64-partial sum inside that headroom. SC-018 measures
  the onset peak and SC-017 the whole parameter grid; a coherent-phase (peak-basis) alternative was
  rejected because it is ~8× quieter at 64 partials and makes SC-018's peak-to-RMS clause trivially true.
  Trace: **spec-added**, required by FR-006 (bounded output) and by SC-003's need to fit a slope on an
  unclipped render — at +12 dB/oct the raw law gives `gain(64) = 10^(12·log2(64)/20) ≈ 3981×` the
  fundamental, which without normalization clips every render at the criterion's own stated extreme.

### FR-020 series — per-partial living state (roadmap lines 145–146)

- **FR-021** — Each partial carries an independent **stereo position** in [-1, +1] (see FR-091 for how it
  becomes gain). The law is explicit (Clarifications Q4):

  > `position_i = spread · s_i`, with `s_i` drawn **once per seed** i.i.d. uniform on [-1, +1] from the
  > cloud's `Xorshift32` stream (`nextFloat()`, `random.h:58`, whose range is already [-1, 1]).

  So spread 0 centres every partial (FR-093), spread 1 uses the full field, and no two partials sit at the
  same point at a non-zero spread. The scatter is redrawn only on re-seed or `reset()`, never per block.
  FR-008's `setPartialPosition` overrides one partial's position until the next spread change, re-seed or
  `reset()`. Trace: roadmap line 146 ("stereo position"). Measured by SC-012.
- **FR-022** — Each partial carries an independent **drift amount** scaling how strongly its
  `BrownianDrift` output detunes it (FR-030 series). The law is explicit (Clarifications Q8) and combines
  index scaling with seeded scatter:

  > `amount_i = (n / kMaxPartials)^k · u_i`, with 1-based partial index `n`, documented constant
  > `k = kDriftIndexExponent = 1.0`, and `u_i` drawn **once per seed** i.i.d. uniform on **[0.5, 1.0]**.

  The index term makes upper partials wander more than low ones — the physical decoherence behaviour of a
  real inharmonic body — while the seeded scatter keeps partials individually distinct rather than lying on
  a smooth curve. The `[0.5, 1.0]` floor keeps every partial alive (no partial is silently inert), and
  `amount_i ≤ 1` makes FR-033's cents depth a true upper bound. `n / kMaxPartials` uses the **fixed**
  capacity, not the FR-041 active count, so changing Richness does not re-scale existing partials' drift.
  Trace: roadmap line 146 ("drift amount"). Measured by SC-015 clause 3.
- **FR-023** — Each partial carries **individual attack and decay offsets**: the cloud owns a per-partial
  amplitude envelope gated by note-on/note-off, with cloud-level base attack and decay times and an
  offset-spread control that staggers per-partial onset and release times. At offset spread 0, every
  partial's envelope timing is identical; as spread rises, per-partial onset times separate measurably.
  **Shape (Clarifications Q3): linear AR.** Each partial's envelope rises linearly from its current value to
  1.0 over `attackTime + attackOffset_i` — the attack time is the **time-to-100%**, not a time constant —
  holds at 1.0 for as long as the gate is on, and falls linearly to 0 over `decayTime + decayOffset_i` after
  gate-off. **Decay is the release**; there is no separate release stage and no sustain-level control
  (Assumption 2). **Retrigger (Clarifications Q5):** a note-on while the cloud is still sounding re-opens the
  envelope **from its current value** rather than restarting from 0, so no partial amplitude steps.
  The envelopes are evaluated **by the cloud, once per internal control chunk (FR-032) — hence at least once
  per block — and written into the per-partial `targetAmplitude[]` array** the kernel consumes; the kernel's
  single scalar `ampSmoothCoeff` (`harmonic_oscillator_bank_simd.h:43`) is a uniform de-zippering stage
  downstream of them (FR-014) and cannot express per-partial timing at all. The maximum offset spread and the
  shortest supported attack are documented in the header, and the shortest supported attack must be **at
  least 20× the FR-014 smoother time constant** (≥ 40 ms at the reference ~2 ms), so the smoother's lag on a
  linear ramp (≈ one time constant) cannot by itself keep a partial below SC-013 clause 3's 95% mark at the
  end of its attack. Scope is fixed at {attack, decay, offset spread, gate} per Assumption 2.
  Trace: roadmap line 146 ("individual attack/decay offsets"); Assumption 2. Measured by SC-013.
- **FR-024** — Per-partial state is stored SoA in fixed-size `alignas(32)` arrays parallel to the
  oscillator arrays, keeping the SIMD-friendly layout the roadmap calls for. Trace: roadmap line 159.

### FR-030 series — per-partial `BrownianDrift` (roadmap line 147)

- **FR-031** — Each partial owns an **independent detune Ornstein–Uhlenbeck drift lane** whose recurrence,
  coefficients and clamps are exactly `BrownianDrift`'s (`brownian_drift.h:94`), with a distinct seed
  derived from the cloud seed so partials drift independently. Concretely, each lane implements:
  `τ = kTauMin + smoothness · (kTauMax − kTauMin)` (`:231-234`); `a = exp(−Δt/τ)` (`:235`);
  `g = kInternalStd · sqrt(max(1−a², 0))` (`:237-239`); an increment `z` formed from **three explicitly
  sequenced** `Xorshift32::nextFloat()` draws (Irwin-Hall, `:257-260`); `x ← clamp(a·x + g·z, ±kWalkLimit)`
  with the `kDenormalFloor` flush (`:262-266`); an output target `clamp(depth·x, ±1)` (`:249-251`); and a
  **150 ms one-pole output smoother advanced exactly as `OnePoleSmoother::advanceSamples` advances it**
  (`smoother.h:243-254`) — including its `isComplete()` skip, its `detail::flushDenormal`, and its hard snap
  to target below `kCompletionThreshold = 1e-4f` (`smoother.h:55`). The cloud owns **two** such banks — the
  detune bank here and the independent mutation bank of FR-072 (Clarifications Q1) — 2 × `kMaxPartials` =
  128 lanes, all fixed-size members per FR-002, all prepared in `prepare()` per FR-003. Seed derivation must
  give all 128 lanes distinct streams. Trace: roadmap line 147; Assumption 1.

  *Whether the lanes are 128 `BrownianDrift` objects or a structure-of-arrays transposition of the same
  recurrence is an implementation choice governed by SC-007.* It is measured, not assumed: 128
  `BrownianDrift` instances cost 44,402 ns per 512-sample block on the reference machine, 1.80× SC-007's own
  baseline gate of 35,533 ns, while the SoA form costs 9,426 ns (plan §6.1). Equivalence is **verified, not
  asserted**, by a dedicated test that drives a real `BrownianDrift` and the shipped lane from the same seed,
  smoothness and sample rate through an identical chunk schedule and requires their value sequences to agree
  within 1e-5 at every chunk over 60 s, at smoothness {0, 0.5, 1}, on both banks
  (`HarmonicCloud_DriftLaneMatchesBrownianDrift`). Any implementation that fails that test violates FR-031.
- **FR-032** — Drift is **decimated**: the cloud reads each partial's drift once per **internal control
  chunk**, not once per sample, where "evaluation" means the cloud's *read*, not the walk's internal step.
  **The cloud subdivides its own render loop at a documented `kControlChunkSamples = 64`**
  (Clarifications Q7): `processStereoBlock(L, R, numSamples)` is rendered as `ceil(numSamples / 64)` chunks
  of at most 64 samples; per chunk, each partial's drift lane is **advanced by exactly `chunkLength`
  samples** — with the same internal structure as `BrownianDrift::processBlock`
  (`brownian_drift.h:194-206`): an OU control step every `kControlRateInterval = 32` samples and an
  output-smoother advance over each intervening span — and its smoothed value is **read exactly once**,
  and the resulting frequency multiplier is held constant across that chunk. Reads per partial per
  block are therefore `ceil(numSamples / 64)`, not 1.
  *Why chunked rather than literally per block:* a literal per-block read makes the sound a function of the
  host's buffer size — at a 16384-sample block a partial's detune would be frozen for 341 ms. The 64-sample
  chunk makes the rendered result **block-size-invariant up to the chunk grid** while still costing 1/64th
  of a per-sample read.
  `BrownianDrift` internally steps its OU walk at its own control rate — `processBlock` loops
  `while (remaining > 0)` and calls `advanceControlStep()` every `kControlRateInterval = 32` samples
  (`brownian_drift.h:105`, :194-206) — so a 64-sample chunk performs 2 internal OU steps, not one, and the
  walk's state after N total advanced samples is independent of how those samples were partitioned. That
  rate is deliberately independent of the host block size and is what makes the walk's autocorrelation time
  a property of seconds rather than of buffer length (Phase 1's SC-003 contract); the cloud must not try to
  collapse it to one step per chunk. What Phase 2 decimates is the per-partial read and the epsilon
  recomputation it triggers. Trace: roadmap line 147 ("decimated: one drift evaluation per partial per
  block"), read at chunk granularity per Clarifications Q7. Measured by SC-015 clause 2.
- **FR-033** — The drift value modulates partial **detune** (a frequency multiplier fed to the kernel's
  `detuneMultiplier` input), with a cloud-level drift-depth control in cents that bounds the maximum
  detune. The combination with FR-022's per-partial amount is explicit:

  > `detuneMultiplier_i = semitonesToRatio(centsDepth · amount_i · d_i / 100)`, with `d_i ∈ [-1, +1]` the
  > partial's detune-bank drift value and `amount_i ∈ (0, 1]` from FR-022.

  Because `BrownianDrift`'s range is fixed at [-1, +1] regardless of its own depth setting
  (`brownian_drift.h:216-219`), the cents bound is applied by the cloud, not by re-reading the source
  range; `amount_i ≤ 1` therefore makes `centsDepth` a true upper bound over all partials. The cents depth
  is a documented cloud-level control with a documented maximum; cents→ratio uses `semitonesToRatio`
  (`pitch_utils.h:23`). Trace: roadmap line 114 ("The workhorse: per-partial detune drift, …") + line 147.
  Measured by SC-015.
- **FR-034** — The chunk-rate detune update must not click: the effective per-partial frequency changes at
  most once per control chunk (FR-032), and the resulting output discontinuity is bounded (partial phase is
  continuous through a detune change because the MCF state is not reset — only `epsilon` changes). Trace:
  roadmap SC "no zipper noise under mutation/drift" (line 161–162).
- **FR-035** — All **detune**-bank instances share a single configuration path: setting the cloud's drift
  smoothness and drift depth applies to every instance of that bank — one cloud-level smoothness value feeds
  every detune lane's `τ`, and one cloud-level depth feeds the applied cents bound, with no per-partial API
  call in the audio path (the semantics of `BrownianDrift::setSmoothness`/`setDepth`,
  `brownian_drift.h:152,159`). The **mutation** bank is configured independently (FR-072) and is
  deliberately *not* reached by these two setters, so drift depth 0 does not disable Mutation
  (Clarifications Q1). Trace: roadmap line 147 ("shared-state"); Assumption 1.

### FR-040 series — Richness macro (roadmap line 149)

- **FR-041** — A single normalized **Richness** control `r ∈ [0, 1]` jointly sets the number of active
  partials and the amplitude rolloff shape. Both laws are explicit (Clarifications Q2), matching the
  every-other-macro standard set by FR-051/FR-061/FR-081:

  > (a) **Active count:** `N(r) = round(kMaxPartials^r) = round(64^r)`, clamped to [1, 64].
  > (b) **Amplitude rolloff:** `a_n = n^(−p(r))` for `n ∈ [1, N(r)]`, with `p(r) = 3.0 − 2.5 · r`
  >     (linear from `p = 3.0` at `r = 0` to `p = 0.5` at `r = 1`).

  The count mapping is **exponential, not linear**, so the count grows perceptually evenly and saturates at
  `kMaxPartials` at the top of the range (`round(64^r) = 64` for `r ≳ 0.998`) rather than only at the single
  point `r = 1` — which is what FR-042's "saturates near the top of the range" wording describes.
  `N(0) = 1` (fundamental only) and `N(1) = 64`.
  The **active count is explicit state**, exposed by FR-008 and passed to the kernel as its `numPartials`
  argument (`harmonic_oscillator_bank_simd.h:44`), so **inactive partials cost no CPU** — this, not a
  quality tier, is how Phase 2 scales cost with material (Clarifications OQ-1). `a_n` is the un-mutated
  amplitude that FR-061's tilt multiplies and FR-017's normalizer is derived from, in that fixed order.
  Trace: roadmap line 149. Measured by SC-014.
- **FR-042** — Increasing Richness is **monotonically non-decreasing** in both (a) the count of partials
  whose measured level is above the **audibility floor, defined as −60 dB relative to the strongest
  partial**, and (b) the fraction of total spectral energy carried by partials above the fundamental.
  Non-decreasing rather than strictly increasing, because the active count necessarily saturates at
  `kMaxPartials` near the top of the range; the discriminating requirement is at the endpoints —
  **Richness 1 must exceed Richness 0 by at least 16 partials above the floor and by at least 20 dB in the
  above-fundamental energy fraction**. FR-041's law satisfies both with large margin — `N(0) = 1` versus
  `N(1) = 64` is a 63-partial span, and at `r = 1` (`p = 0.5`, so `a_n² = 1/n`) the above-fundamental energy
  fraction is `(H₆₄ − 1)/H₆₄ ≈ 0.79` (−1.0 dB) against SC-014's −80 dB floor at `r = 0` — so these
  thresholds discriminate a broken implementation rather than merely describing the intended one. Trace:
  roadmap line 149 (testable form of "number of active partials + amplitude rolloff shape"). Measured by
  SC-014.
- **FR-043** — Changing Richness while sounding is click-free: partials entering or leaving fade through
  the amplitude smoother (FR-014) rather than switching on/off, and partials dropped from the active count
  are faded out rather than truncated — the tail-fade pattern verified in
  `harmonic_oscillator_bank.h:746-779`. Trace: roadmap SC line 161.

### FR-050 series — Inharmonicity macro (roadmap line 150)

- **FR-051** — An **Inharmonicity** control sets the stretched-partial coefficient `B` in the piano/bell
  law `f_n = f0 · n · sqrt(1 + B·n²)`, matching the verified reference implementation
  `AdditiveOscillator::calculatePartialFrequency` (`additive_oscillator.h:466-474`,
  `stretch = sqrt(1 + B·n²)`). `B = 0` yields exactly harmonic ratios. Trace: roadmap line 150.
- **FR-052** — The control's range is clamped to a documented maximum; the existing reference clamps to
  `kMaxInharmonicity = 0.1f` (`additive_oscillator.h:90,339`). Any wider range chosen here must still
  satisfy FR-015 (no aliasing) at the maximum with 64 partials and the highest supported fundamental
  (4000 Hz per FR-013).
- **FR-053** — Inharmonicity changes are smooth: sweeping `B` produces continuously moving partial
  frequencies with no phase reset and no amplitude step (partial epsilon is recomputed; MCF state is
  preserved, as in FR-013). Trace: roadmap SC line 161.

### FR-060 series — Spectral tilt macro (roadmap line 151)

- **FR-061** — A **Spectral tilt** control expressed in dB/octave applies the per-partial gain law
  `gain(n) = 10^(tiltDb · log2(n) / 20)` verified in
  `AdditiveOscillator::calculateTiltFactor` (`additive_oscillator.h:480-490`), where `n` is the 1-based
  partial index. Trace: roadmap line 151 ("dB/octave slope (reuse `spectral_tilt` math)"); Assumption 3.
- **FR-062** — Tilt is clamped to a documented range; the adopted convention is `SpectralTilt`'s
  ±12 dB/octave (`spectral_tilt.h:98-101`). Tilt 0 dB/oct leaves partial amplitudes unmodified.
- **FR-063** — Tilt is applied to *target* amplitudes and therefore reaches the output through the
  amplitude smoother (FR-014), so automating tilt is click-free. Trace: roadmap SC line 161.

### FR-070 series — Mutation macro (roadmap line 152)

- **FR-071** — A **Mutation** control [0, 1] applies a slow random re-weighting of partial amplitudes:
  each partial's target amplitude is multiplied by a bounded, seeded weight `w_i = 1 + m · kMaxMutationDepth
  · d_i`, where `m` is the control, `kMaxMutationDepth = 0.75` and `d_i ∈ [-1, +1]` is that partial's
  **mutation-bank** `BrownianDrift` output (FR-072) — **not** the FR-031 detune value. At Mutation 0 the
  weight is exactly 1.0 and the spectrum is static; the Mutation control is the *only* thing that scales the
  applied weight, so Mutation is independent of the drift-depth control at every setting (Clarifications Q1).
  "Slowly varying" is quantified, not adjectival: the weight inherits `BrownianDrift`'s 150 ms output
  smoother (`brownian_drift.h:103`) and its 32-sample control interval (:105), which bounds
  `|dw/dt| ≤ 2 · kMaxMutationDepth / 0.150 s ≈ 10 s⁻¹` at full depth — the operational form of the
  roadmap's "bounded, slow, and smooth" rule (line 78). `w_i` multiplies the FR-017 chain **downstream of
  the normalizer**, so mutation's level movement survives to the output. Trace: roadmap line 152.
- **FR-072** — The re-weighting is **life-modulated** by a **second, dedicated per-partial drift bank** with
  the FR-031 lane behaviour (Clarifications Q1) — 64 further lanes alongside FR-031's detune bank, 128 per
  cloud in total:
  - each mutation instance gets its **own derived seed** from the cloud seed (FR-005), distinct from every
    detune-bank seed, so mutation weight is not 100% correlated with that partial's detune;
  - its `setDepth` is **pinned at 1.0 internally** and is never touched by the FR-035 drift-depth control;
    the Mutation macro scales the *applied weight* in FR-071 instead. Consequence, stated as a requirement:
    **drift depth 0 must not disable or attenuate Mutation** — including SC-017's `{0 drift depth} ×
    {max Mutation}` grid cell;
  - its smoothness is a documented cloud-level constant, independent of the FR-035 drift-smoothness control,
    so the two macros never interact;
  - it is advanced and read on the same FR-032 chunk cadence as the detune bank.

  Driving the weights from a life modulator rather than per-block white noise is what makes the motion read
  as organic drift and keeps the roadmap's "Entropy, not chaos" rule (line 78): bounded, slow, smooth at
  every setting. The added cost (128 OU walks per cloud instead of 64) is inside SC-007's gate and is
  measured there, not assumed. Trace: roadmap line 152 ("(life-modulated)"); Clarifications Q1. Independence
  measured by SC-016.
- **FR-073** — Mutation never silences or unbounds the spectrum. Numeric bounds, not placeholders:
  the per-partial weight stays within **[0.25, 1.75]** at every setting (the FR-071 law with
  `kMaxMutationDepth = 0.75` and `|d_i| ≤ 1` makes this exact, and `BrownianDrift::getCurrentValue()` is
  itself clamped to [-1, +1] at `brownian_drift.h:212-214`), and the block RMS of the output over any 1 s
  window stays **within ±3 dB of the Mutation-0 RMS** of the same configuration as Mutation sweeps 0 → 1.
  Trace: roadmap line 78 ("bounded, slow, and smooth"). Measured by SC-016.
- **FR-074** — Mutation is zipper-free at every setting and every rate: the per-partial weight reaches the
  oscillator through the amplitude smoother (FR-014) and changes at most once per **control chunk**
  (FR-032), which also makes the weight trajectory block-size-invariant. Trace: roadmap SC
  line 161–162 (the explicit "no zipper noise under mutation/drift" criterion).

### FR-080 series — Spectral gravity macro (roadmap lines 153–154)

- **FR-081** — A bipolar **Spectral gravity** control `g ∈ [-1, +1]` warps the partial ratios off the pure
  harmonic grid. The law is explicit, not descriptive:

  > `ratio_g(n) = n^(1 + g · kGravityExponentRange)`, with `kGravityExponentRange = 0.1` (documented
  > constant, 1-based partial index `n`).

  At `g = 0` this is exactly `n` — the pure integer grid, for every partial, independent of Inharmonicity
  (which is a separate multiplicative stretch applied afterwards, FR-083). `g > 0` stretches (upper partials
  pushed up); `g < 0` compresses. `ratio_g(1) = 1` for all `g` by construction, so the fundamental never
  moves — a property the success criteria must respect rather than contradict (SC-004). The roadmap's
  "toward" direction is the continuous return to the grid as `|g| → 0`; see Assumption 4 for the recorded
  deviation from roadmap line 153. Trace: roadmap lines 153–154.
- **FR-082** — Gravity's effect is a continuous, **strictly** monotone function of |gravity| for every
  partial `n ≥ 2`: `|ratio_g(n) − n|` strictly increases as |g| increases, and the sign of `ratio_g(n) − n`
  is positive for `g > 0` and negative for `g < 0`. For `n = 1` the deviation is identically 0 at every
  setting (FR-081) and no monotonicity or sign claim is made about it. Trace: roadmap line 154 (testable
  form of "0 = pure harmonic, ± = stretched/compressed"). Measured by SC-004.
- **FR-083** — Gravity composes with Inharmonicity in one fixed, documented order: **gravity warps the grid,
  then the inharmonicity stretch multiplies it**, so the synthesized frequency of partial `n` is

  > `f_n = f0 · ratio_g(n) · sqrt(1 + B · n²)` = `f0 · n^(1 + g·kGravityExponentRange) · sqrt(1 + B·n²)`

  which reduces exactly to FR-051's `f0 · n · sqrt(1 + B·n²)` at `g = 0` and exactly to FR-081's grid warp
  at `B = 0`. Any implementation ordering that produces a different combined frequency is a defect. The
  combination is subject to FR-015 (anti-aliasing) and FR-007 (clamping) at every combined extreme.
  Trace: roadmap lines 150 + 153. Measured by SC-004's non-zero-`B` case.
- **FR-084** — Gravity changes are phase-continuous and click-free by the same mechanism as FR-053.

### FR-090 series — stereo output (roadmap line 155)

- **FR-091** — Each partial's stereo position is converted to a pair of gains by an **equal-power** pan law,
  so `panLeft² + panRight² = 1` and **both gains are non-negative** for every partial at every position.
  The two verified in-repo formulations are the same *curve* over **different domains**, and mixing them up
  is a real failure mode the equal-power identity cannot detect:
  - `HarmonicOscillatorBank::recalculatePanPositions` takes **position ∈ [-1, +1]** —
    `angle = π/4 + pos·π/4`, `panLeft = cos(angle)`, `panRight = sin(angle)`
    (`harmonic_oscillator_bank.h:1102-1128`). This matches FR-021's position domain directly.
  - The Layer 0 helper `equalPowerGains(float position, float& fadeOut, float& fadeIn)` takes
    **position ∈ [0, 1]** — `fadeOut = cos(position·kHalfPi)`, `fadeIn = sin(position·kHalfPi)`
    (`crossfade_utils.h:50-53`) — and its contract states explicitly: *"Does NOT clamp position - caller is
    responsible for keeping it in [0, 1]"* (:41).

  **If the Layer 0 helper is used, the FR-021 position MUST first be remapped** as
  `p01 = std::clamp((pos + 1.0f) * 0.5f, 0.0f, 1.0f)`. Feeding a bipolar position straight in is silently
  wrong in two ways: at `pos = 0` it yields L = 1 / R = 0 (hard left, not centre), and at `pos = -1` it
  yields `panRight = sin(-π/2) = -1`, a full-level **polarity-inverted** right channel — while
  `panLeft² + panRight²` still equals 1, so the equal-power identity passes. SC-012's non-negativity and
  monotonicity clauses exist to catch exactly this. Trace: roadmap line 155.
- **FR-092** — The gains are stored as two SIMD-friendly gain vectors (`panLeft[]`, `panRight[]`) matching
  the kernel's `panLeft`/`panRight` inputs (`harmonic_oscillator_bank_simd.h:41-42`), and stereo
  accumulation is done inside the kernel (`sumL += amp·s·panL; sumR += amp·s·panR`,
  documented at `harmonic_oscillator_bank_simd.h:29`). Trace: roadmap line 155
  ("SIMD-friendly: two gain vectors").
- **FR-093** — At a stereo spread of 0 every partial is centred and the left and right outputs are
  identical. As spread increases, inter-channel correlation of the rendered output decreases
  monotonically. Trace: roadmap line 155.

### Roadmap component coverage (completeness check)

| Roadmap Phase 2 statement | Line | Covered by |
|---|---|---|
| Composes `HarmonicOscillatorBankSimd` (per-partial freq/amp/**phase**) | 145 | FR-011, FR-012, FR-013, FR-016 (phase) |
| New per-partial state: stereo position, drift amount, attack/decay offsets | 146 | FR-021, FR-022, FR-023, FR-024 |
| Per-partial `BrownianDrift`, shared-state, decimated per block | 147 | FR-031 … FR-035 |
| Richness — partial count + rolloff shape | 149 | FR-041, FR-042, FR-043 |
| Inharmonicity — `f_n = f0·n·sqrt(1+B·n²)` | 150 | FR-051, FR-052, FR-053 |
| Spectral tilt — dB/octave slope | 151 | FR-061, FR-062, FR-063 |
| Mutation — slow random re-weighting, life-modulated | 152 | FR-071 … FR-074 |
| Spectral gravity — pull toward/away from harmonic grid | 153–154 | FR-081 … FR-084 |
| Stereo output, per-partial equal-power pan, two gain vectors | 155 | FR-091, FR-092, FR-093 |
| No analysis pipeline / no `HarmonicFrame` dependency | 157–159 | Non-Goals; FR-001 |
| Exact partial count (Open Question 1) | 494 | Clarifications OQ-1 (fixed 64); FR-012 |

Requirements with **no roadmap line** (spec-added, listed here so scope additions stay visible): FR-008
(test/introspection surface — required to make SC-001/SC-003/SC-012/SC-013/SC-015/SC-018 measurable at
all), FR-015 (anti-aliasing — driven by FR-050/FR-080, mechanism inherited from the reuse target), FR-017
(output normalization — driven by FR-006 and SC-003), and SC-010 (sample-rate invariance — mirrors Phase 1's
coverage). Each states the fact in its own trace field.

## Success Criteria

Every criterion states its metric, threshold, and measurement. No criterion may be satisfied by a
bit-exact float golden (roadmap line 486); where a render must be pinned, it is pinned with
`tests/test_helpers/render_fingerprint.h` (aggregate metrics + spaced checkpoints, tolerances
`kSampleTolerance = 1.0e-4f`, `kMetricTolerance = 1.0e-5` at `render_fingerprint.h:49-52`).

- **SC-001 (Static frequency accuracy).** With drift, mutation, gravity and inharmonicity all at zero, each
  measured partial's synthesized frequency deviates from `f0 · n` by **< 0.1 cent**.
  *Metric:* per-partial frequency error in cents, computed as
  `cents = 100.0f * ratioToSemitones(fMeasured / (f0 * n))` (`pitch_utils.h:31`) — an unwrapped,
  unbounded-magnitude metric. (`frequencyToCentsDeviation` is **not** usable here: it measures deviation
  from the nearest chromatic note centre and is confined to [-50, +50] cents, `pitch_utils.h:169-186`.)
  *Measurement:* isolate one partial via FR-008's partial-solo facility (all other target amplitudes zero),
  render ≥ 10 s at 48 kHz, and estimate its frequency to sub-bin resolution (quadratic-interpolated FFT peak
  or long-window zero-crossing period estimate); the estimator's own resolution must be documented in the
  test and be at least 10× finer than 0.1 cent.
  *Which (n, f0) pairs:* the matrix is a **constraint, not a cross-product** — every measured partial must
  satisfy `f0 · n < 0.8 · Nyquist` at the test rate, because FR-015 suppresses partials above that fade
  start and a suppressed partial has no frequency to estimate. At 48 kHz (fade start 19.2 kHz) the measured
  set is therefore: **n ∈ {1, 8, 32, 64} at f0 = 55 Hz** (55·64 = 3.52 kHz), **n ∈ {1, 8, 32} at 440 Hz**
  (440·32 = 14.08 kHz), **n ∈ {1, 8, 16} at 1 kHz** (1000·16 = 16 kHz). The test must assert the constraint
  itself so the matrix cannot silently drift back into the fade band.
  *Threshold:* **max error < 0.1 cent** (hard). Test sketch:
  `HarmonicCloud_PartialFrequencyAccuracyWithin0p1Cent`. Trace: roadmap line 161.
- **SC-002 (Inharmonicity law).** For ≥ 3 non-zero values of `B` within the supported range, the measured
  frequency of each partial matches `f0 · n · sqrt(1 + B·n²)` within a **measured** tolerance derived from
  the estimator resolution at the stated render length (documented in the test, not guessed). Metric: max
  relative frequency error across partials. Measurement: FFT peak estimation per partial as in SC-001.
  Threshold: **≤ the documented estimator tolerance, and ≤ 1 cent** at every measured partial. Test sketch:
  `HarmonicCloud_InharmonicityFollowsPianoLaw`. Trace: roadmap lines 150, 163–164.
- **SC-003 (Spectral tilt law).** For tilt settings across the supported range (at least −12, −6, 0, +6,
  +12 dB/oct), the measured partial-amplitude spectrum has a least-squares slope in dB per octave of
  partial index that matches the setting.
  *Configuration (pinned, because the extremes are where this criterion breaks):* `f0 = 110 Hz`, **32 active
  partials** (so partial 32 at 3.52 kHz stays far below the FR-015 fade band and is not amplitude-suppressed
  — a suppressed partial would bend the fit), obtained by holding **Richness at `r = log₆₄(32) = 5/6`**,
  which FR-041 maps to `N = round(64^(5/6)) = 32` and rolloff exponent `p = 3.0 − 2.5·(5/6) = 0.9167`;
  drift/mutation/gravity/inharmonicity at zero, sample rate 48 kHz, render ≥ 4 s, **FFT size 65536 with a
  Blackman-Harris window** (≈ −92 dB sidelobes, needed because at −12 dB/oct partial 32 sits 60 dB below the
  fundamental).
  *Isolating tilt from the Richness rolloff (required by FR-041's law).* The rendered spectrum carries both
  the tilt gain and the FR-041 rolloff `a_n = n^(−p)`, which contributes a fixed
  `−20·p·log₁₀(2) = −5.52 dB/octave` at this Richness. The criterion is therefore evaluated **differentially
  against the tilt-0 render of the identical configuration**: the tilt-0 fitted slope is subtracted from
  each setting's fitted slope, and per-partial gains are compared to the tilt-0 render's per-partial gains.
  This isolates the FR-061 law exactly and is not a relaxation — the tilt-0 render's own fitted slope must
  additionally match `−5.52 dB/octave` within the same 0.5 dB/octave threshold, which is what pins the
  Richness rolloff law itself.
  *Precondition:* the render must be verified unclipped before any fit —
  `REQUIRE(peak < 0.9f * kOutputClamp)` — so an FR-017 normalization failure fails loudly instead of
  silently flattening the fitted slope. At +12 dB/oct the raw gain law gives `gain(32) ≈ 10^3 ×` the
  fundamental, so FR-017 is what makes this criterion measurable at all.
  *Metric:* tilt-isolated fitted slope error in dB/octave (this setting's fitted slope minus the tilt-0
  fitted slope), and per-partial gain error against `10^(tiltDb·log2(n)/20)` after removing both the tilt-0
  reference gains and the single global FR-017 normalization gain (both scalar offsets in dB, neither of
  which can change the slope). *Measurement:* FFT magnitude at each partial bin.
  *Threshold:* **fitted slope within 0.5 dB/octave** of the setting, **tilt-0 fitted slope within
  0.5 dB/octave of −5.52 dB/octave** (the FR-041 rolloff at this Richness), and **per-partial gain error
  ≤ 0.5 dB**.
  Test sketch: `HarmonicCloud_TiltSlopeMatchesSetting`. Trace: roadmap lines 151, 163–164.
- **SC-004 (Gravity mapping and composition order).** Three parts.
  1. *Zero setting.* At gravity 0 the measured partial ratios equal the integer grid within the SC-001
     tolerance.
  2. *Monotonicity.* Across a symmetric sweep of **≥ 5 gravity settings** (e.g. −1, −0.5, 0, +0.5, +1) with
     `B = 0`, the mean `|ratio − n|` over partials `n ≥ 2` is **strictly increasing in |g|**, with each step
     increasing it by **at least 5× the SC-001 estimator tolerance** expressed in the same units (so the
     ordering cannot be produced by measurement noise). "Strictly increasing" is the single wording — FR-082
     uses the same word; there is no "non-decreasing" alternative reading.
  3. *Sign, restricted.* For every measured partial with **n ≥ 2** whose `|ratio − n|` at |g| = max exceeds
     the SC-001 estimator tolerance, the sign of `(ratio − n)` at `+g` is opposite to its sign at `−g`.
     **Partial n = 1 is excluded**: FR-081 fixes `ratio_g(1) = 1` for every `g`, so its deviation is
     identically 0 and can never invert — a universal "every measured partial" clause would be unsatisfiable
     by a correct implementation.
  4. *Composition order.* Repeat the |g| = max settings with a **non-zero `B`** (at least one value, e.g.
     `B = 0.05`) and assert each measured partial frequency matches FR-083's combined law
     `f0 · n^(1+g·kGravityExponentRange) · sqrt(1 + B·n²)` within the SC-002 tolerance. This is what pins
     the composition order; without it the order is merely documented, not measured.
  *Metric:* mean `|ratio − n|` per setting; sign of `(ratio − n)`; combined-law relative error.
  *Measurement:* FFT peak estimation per partial as in SC-001, subject to the same
  `f0 · ratio < 0.8 · Nyquist` constraint. Test sketch: `HarmonicCloud_GravityMapsMonotonically`. Trace:
  roadmap lines 153–154, 163–164.
- **SC-005 (No zipper noise under mutation and drift).** With Mutation and per-partial drift at maximum, a
  30 s render at 48 kHz contains no click/discontinuity artifacts **beyond what the same signal produces
  with nothing moving**.
  *Detector configuration (pinned, not "the documented default"):* `ClickDetectorConfig`
  (`artifact_detection.h:38`) with `sampleRate = 48000.0f` (**must match the render**; the struct's default
  is 44100), `frameSize = 512`, `hopSize = 256`, `detectionThreshold = 5.0f`, `energyThresholdDb = -60.0f`,
  `mergeGap = 5`; detector `ClickDetector` (:99), `detect(...)` (:130).
  *Why an absolute "0 detections" is not the pass condition:* `ClickDetector` is a **within-frame
  statistical outlier test** — `threshold = mean(|dx|) + detectionThreshold · stddev(|dx|)` over each frame
  (`artifact_detection.h:186-193`). For a broadband 64-partial sum with *independently drifting* partials the
  derivative is near-Gaussian by CLT, so a 5σ-above-mean threshold is ≈ 3.8σ in absolute terms — a per-sample
  exceedance probability on the order of 1e-4, i.e. **hundreds** of *false* detections over a 30 s render
  with no artifact present. MEASURED on the modulated render: **126 (L) / 141 (R)**. An absolute-zero
  threshold would therefore be satisfiable only by accident of spectral shape.
  *Corollary the amendment below rests on:* the CLT argument is specific to the *aperiodic* case. Freeze the
  drift and the same 64 partials become an exactly **periodic** waveform whose first difference has no
  within-frame outliers at all — measured **0** detections — so the frozen render cannot serve as a null
  model for a *count*.
  > **AMENDED 2026-07-26 (implementation measurement).** The pass condition below replaces an earlier one
  > that a correct implementation cannot satisfy. The withdrawn wording was:
  >
  > 1. `detections(modulated) ≤ detections(control)`, and
  > 2. `maxPerSampleDelta(modulated) ≤ 1.5 × maxPerSampleDelta(control)`,
  >
  > with `control` = the same configuration rendered with drift and mutation **frozen**. Both clauses fail
  > on a click-free build, and the reason is the same for both: the frozen control is not a quieter version
  > of the modulated render, it is a **different signal regime**. 64 exactly-harmonic partials at fixed
  > phases make a *periodic* waveform; ±50 cents of independent per-partial drift makes the sum *aperiodic*
  > and gives it the crest statistics of a random-phase sum. MEASURED on the pinned configuration
  > (`HarmonicCloud_NoZipperUnderMutationAndDrift`, "withdrawn-clause guard"):
  >
  > - clause 1 — frozen control **0** detections against the modulated render's **267** (both channels
  >   summed). A periodic signal's first difference has no within-frame outliers at all, so the clause sets
  >   an upper bound of zero that no aperiodic signal can meet, click-free or not. The count is also not
  >   measuring zipper: sweeping drift smoothness over its whole range — a 150× change in how far the detune
  >   moves per control chunk, which is exactly what zipper is proportional to — leaves the count flat
  >   (L: 121/140/126/114/129, R: 145/161/141/111/141 at smoothness 0/0.25/0.5/0.75/1.0).
  > - clause 2 — measured ratio against the frozen control **1.785**, over the 1.5 bound. That figure is the
  >   crest factor of an aperiodic sum against a periodic one, not a step.
  > - the positive control's denominator — "10% of peak" is **0.0748** on this render while the render's own
  >   largest first difference is **0.1710**, and injecting it produces **0** detections. `ClickDetector` is
  >   a within-frame outlier test on `|dx|` (`artifact_detection.h:186-193`); it is scale-free but says
  >   nothing about peak, and a step below the signal's own natural per-sample swing is by construction not
  >   an outlier. Peak and max `|dx|` are unrelated quantities on a 64-partial sum.
  >
  > Each of the three findings is pinned by a standing assertion in the test — phrased as the **negation**
  > of the withdrawn clause — so if any of them ever inverts, the test fails and the original wording is to
  > be restored rather than the substitute kept.

  *Pass condition (differential, as amended).* Render the identical configuration twice — once **modulated**
  (mutation and drift at maximum) and once as a **control** — and judge on **where** the detections land and
  on a **matched-regime** slew comparison:

  1. *No control-grid signature.* Of the modulated render's detections, the fraction landing on the
     64-sample control-chunk grid (`kControlChunkSamples`, ±1 sample) must not exceed **2.25× the uniform
     expectation** for that grid. A stepped per-chunk update repeats on that grid by construction, so a real
     zipper concentrates there; a broadband detector's statistical floor does not. Measured on a click-free
     build: **6 of 126 (4.76%) left, 5 of 141 (3.55%) right, against a 14.06% limit**.
  2. *Matched-regime slew.* `maxPerSampleDelta(modulated) ≤ 1.5 × maxPerSampleDelta(slowControl)`, where
     `slowControl` carries the **same** Mutation and the **same** drift depth — hence the same aperiodic
     spectrum — and differs only in drift **smoothness**, set to 1.0 (τ = 30 s, the slowest the component
     offers). Any genuine per-chunk step scales with the per-chunk movement and so survives this comparison,
     while the crest-factor difference that broke the frozen-control version cancels. Measured: **1.098 (L),
     1.089 (R)** against the 1.5 bound.

  Clause 2 also replaces the pure-sine slew reference of an earlier revision, which could not fail: at
  f_max = 15 kHz and fs = 48 kHz that bound is `P·2π·f_max/fs ≈ 1.96·P`, so an audible one-sample step of
  0.5·P passed it.
  *Positive controls (mandatory, both).* Without these the criterion cannot distinguish "no artifacts" from
  "metric not wired up" — the same rule the repo applies to render fingerprints (verify the metric still
  fails on an injected bug, `dsp/CLAUDE.md`, render-fingerprint section).
  1. *Detector wiring.* The same detector and config, run over the control render with a deliberately
     injected one-sample step of **2× the render's own `maxPerSampleDelta`**, must report **≥ 1 detection**
     at that sample index. (Denominated in `maxPerSampleDelta`, not peak — see the amendment note.)
  2. *On-grid metric wiring.* A **synthetic zipper** — a step at *every* control-chunk boundary of the
     control render, at half its own largest first difference — must be caught and caught **on the grid**:
     **≥ 32 detections**, on-grid fraction above clause 1's enrichment limit **and ≥ 0.9**. Measured: **166
     detections, 166 of them on-grid**.

  Test sketch: `HarmonicCloud_NoZipperUnderMutationAndDrift`. Trace: roadmap lines 161–162 (explicit SC).
- **SC-006 (Click-free macro automation).** Sweeping each macro (Richness, Inharmonicity, Tilt, Mutation,
  Gravity) continuously from min to max over 5 s while sounding, **plus** a fundamental step of ≥ 1 octave
  (FR-013's crossfade path), produces no click artifact attributable to the parameter movement — the same
  detector, the same pinned config and the same mandatory positive controls as SC-005, applied to each swept
  parameter independently.

  > **AMENDED 2026-07-26 (implementation measurement).** The withdrawn threshold was
  > **`detections(swept) ≤ detections(control)`** against "a frozen-parameter control render of the same
  > configuration". It is not a criterion, because its verdict is decided by a choice the wording never
  > makes: *which* frozen value is "the same configuration"? A sweep has two endpoints, both of them
  > parameter-free renders of the same cloud, and on this signal the detector's count tracks **how many
  > partials are sounding**, not how many clicks there are. MEASURED
  > (`HarmonicCloud_MacroSweepsAreClickFree`, "withdrawn-clause guard"): a static render at the Richness
  > sweep's **min** endpoint scores **0** detections; at its **max** endpoint, **43**. The Richness sweep
  > itself scores **5** (0 L / 5 R). So the withdrawn clause *passes* against one legitimate control
  > (5 ≤ 43) and *fails* against the other (5 ≤ 0) for the same click-free render. A standing assertion
  > requires the two endpoint counts to keep disagreeing by more than the swept render's whole count; if
  > they ever converge, the original wording becomes well-defined and is to be restored.
  >
  > This is the same root cause as SC-005's amendment — the detector's *count* on a broadband additive sum
  > is a spectral-brightness statistic, not an artifact statistic — so SC-006 adopts the same instrument:
  > **where** the detections land.

  *Pass condition (as amended).* Parameter movement happens once per 512-sample block, so a click caused by
  a parameter step lands **on the block grid**. Threshold: **zero detections on the 512-sample block grid**
  (±1 sample) for each of the five macro sweeps, on both channels, and for the fundamental step. The uniform
  expectation on that grid is 3/512 = 0.586%, i.e. 0.08 expected detections for a 13-detection render, so
  zero is what a click-free render looks like and any nonzero result is worth failing on. Measured on a
  click-free build: **0 on-grid out of totals of 0–13 per channel** across all five sweeps, and **0 on-grid
  out of 1 per channel** for the 220 → 440 Hz step.
  *Positive controls (mandatory, both).* SC-005's detector-wiring injection, plus an on-grid-metric control:
  a **synthetic per-block zipper** — one step at every block boundary of the control render at half its own
  largest first difference — must produce **≥ 16 detections with ≥ 90% of them on-grid**. Measured: **22
  detections, 22 on-grid**. Unlike SC-005's frozen render, this control's peak-denominated injection *is*
  detectable here (10% of peak = 0.0828 → 1 detection), so SC-005's literal 10%-of-peak form is additionally
  asserted in this case.
  *Retrigger clause (FR-016's quiescence rule, Clarifications Q5).* A **sounding retrigger** — a second
  note-on issued while the gate is on and every partial is at full amplitude, i.e. the non-quiescent path —
  is measured with the same detector, the same pinned config and the same mandatory positive control:
  **detections(retriggered) ≤ detections(control)**. This clause keeps the **count** comparison the two
  amendments above withdrew, and is unaffected by them: retriggered and control are the *same* configuration
  in the *same* signal regime — same Mutation, same drift depth, same partial count, nothing frozen — and
  differ only by the extra note-on, so the count is a like-for-like comparison here in a way it is not
  against a frozen or an endpoint control. Measured on a click-free build: **3 vs 3 (L), 4 vs 4 (R)**. The
  test additionally
  asserts the mechanism, not just the outcome: across the retrigger, every partial's phase state is
  **unchanged** at the retrigger sample (sampled via FR-008's accessors before and after), which is what
  makes the retrigger click-free by construction. A second case exercises the **quiescent** path (gate off,
  render until every `currentAmplitude` is below `kQuiescenceAmplitude`, then note-on) and asserts that the
  phases *did* change — otherwise a never-redraw implementation would pass the first case vacuously and
  break SC-018's seeded-onset behaviour.
  Test sketch: `HarmonicCloud_MacroSweepsAreClickFree` (retrigger cases may live in
  `HarmonicCloud_RetriggerIsClickFree`). Trace: FR-013/FR-016/FR-043/FR-053/FR-063/FR-074/FR-084; roadmap
  line 161.
- **SC-007 (CPU budget).** 64 partials with per-partial drift active cost **≤ 0.5% of one core at 48 kHz**
  per cloud instance (roadmap line 162 — a CPU budget is an FR, roadmap line 484, not an aspiration).
  *What the measured configuration must include (this is the gate that covers the clarification decisions).*
  The benchmarked configuration runs **both** per-partial drift banks — detune (FR-031) and mutation
  (FR-072), i.e. **128 OU walks per cloud** — with Mutation at 1.0 and drift depth at maximum, and renders
  through the **64-sample chunked loop** of FR-032 (`ceil(512 / 64) = 8` chunks per 512-sample block, so
  8 × 128 = 1024 drift reads per block). The second bank and the chunked cadence are the two costs
  Clarifications Q1 and Q7 added; they are inside this gate by measurement, not by assumption.
  *Measurement basis.* A "% of one core" figure is not reproducible across machines, so the basis is pinned
  exactly as Phase 1 pinned its SC-007 (`specs/seraphis-phase1-life-modulators/spec.md:338-349`): the metric
  is **nanoseconds per 512-sample block**, and the percentage is derived against the fixed
  512-samples-at-48 kHz wall-clock budget (512 / 48000 ≈ 10.667 ms). 0.5% of that budget is
  `kReferenceNsPerBlock ≈ 53,300 ns`.
  *What is actually enforced.* The **only enforced gate is the relative one**: best-of-N ns/block
  `≤ kBaselineNsPerBlock × kRegressionFactor`, with `kRegressionFactor = 1.5`. There is **no designated perf
  runner in this repository** — every CI test leg excludes perf-tagged cases
  (`.github/workflows/ci.yml:328`, `:574`, `:951` each run the exe with
  `'~[performance]~[perf]~[benchmark]~[!benchmark]'`, and `valgrind-nightly.yml:202` does the same), so no
  job ever evaluates an absolute figure. The absolute 53.3 µs number is **WARN-reported** on every local run
  and is otherwise informational.
  *The arithmetic that keeps the relative gate honest (binding).* The checked-in baseline **MUST** satisfy

  > `kBaselineNsPerBlock × kRegressionFactor ≤ kReferenceNsPerBlock` (i.e. `baseline ≤ 35,533 ns`)

  enforced as a `static_assert` in the perf test TU, exactly as Phase 1 does
  (`dsp/tests/unit/processors/life_modulators_perf_test.cpp:60-73`, where `kBaselineNsPerBlock = 3000` was
  chosen precisely so `3000 × 1.5 = 4500 ns < 5333 ns` "so the test is no weaker than the SC-007 reference
  figure"). Without this constraint a baseline first recorded at, say, 90 µs/block would satisfy the gate
  forever while sitting ~1.7× over the roadmap budget. **If the first measurement cannot meet the
  constraint, the phase is over budget** — the response is to reduce cost (or, failing that, re-open
  Clarifications OQ-1 and resolve the partial count *downward*), never to raise the baseline.
  *Baseline provenance.* `kBaselineNsPerBlock` is a `constexpr double` in the perf test TU
  (`dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp`), generated on the dev machine — Windows 11, MSVC
  Release, `build/windows-x64-release` — with the machine and date recorded in a comment beside it, matching
  Phase 1's convention. Measured at the fixed capacity of 64 partials (Clarifications OQ-1) with both drift
  banks active; there are no tiers to measure.
  Test sketch: `HarmonicCloud_CpuBudget` (`[.perf]`). Trace: roadmap lines 162–163, 484.
- **SC-008 (RT safety / allocation-free).** A steady-state render loop (including macro automation and
  drift) performs **zero heap allocations** after `prepare()`. Metric: allocation count from
  `AllocationDetector` / `AllocationScope` (`tests/test_helpers/allocation_detector.h:26,75`).
  *Wiring constraint — the detector is inert without it.* The global `operator new`/`delete` replacements
  are **commented out** inside `allocation_detector.h` (:99-110); counting only happens if
  `allocation_operator_overrides.h` is linked into the test binary, and that header must be included from
  **exactly one** translation unit per binary. `dsp_systems_tests` already provides it via
  `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`, so the new `HarmonicCloud` TU **must NOT
  include it again** (duplicate-symbol link error) and **must not** rely on including only
  `allocation_detector.h`, which would observe 0 allocations unconditionally and pass vacuously.
  *Liveness assertion (mandatory, first):* before the render loop, the test asserts that a deliberate heap
  allocation inside an `AllocationScope` is counted (`count ≥ 1`). Only then does it assert **0
  allocations** across the render loop. Test sketch: `HarmonicCloud_NoAllocInProcess`. Trace: roadmap
  line 481.
- **SC-009 (Determinism).** Two `HarmonicCloud` instances given the same seed, same parameter sequence and
  same block schedule produce renders that agree within `render_fingerprint.h` tolerances; a different seed
  produces a materially different render.
  *Configuration (pinned, so the negative control cannot pass vacuously):* drift depth, Mutation and stereo
  spread are **all non-zero, each at ≥ 50% of its range**, and initial partial phases are seeded per FR-016.
  With any of them at its legal default of 0 the seed would influence nothing and the different-seed render
  would be identical — proving nothing.
  Metric: `compareFingerprints` (`render_fingerprint.h:101`). Threshold: **all metrics within tolerance for
  the same seed**; for a different seed, **`worstMetricRelativeError > 10 × kMetricTolerance`** (not merely
  "outside tolerance"). Explicitly **not** a bit-exact digest. Test sketch:
  `HarmonicCloud_SeededRenderIsReproducible`. Trace: roadmap lines 300–301 (Phase 7's determinism-harness
  SC, which this feeds), 486.
- **SC-010 (Sample-rate invariance).** Rendering the same musical configuration at 44.1 kHz, 48 kHz and
  96 kHz yields the same *physical* result: each partial's measured frequency in Hz agrees across rates
  within the SC-001 estimator tolerance, and the per-partial amplitude spectrum agrees within 0.5 dB.
  *Amplitude comparison is scoped, because FR-015 makes an unscoped version fail on a correct
  implementation:* the anti-alias fade starts at a fixed *fraction* of Nyquist (`kAntiAliasFadeStart = 0.8f`,
  `harmonic_oscillator_bank.h:87`), i.e. 17.64 kHz at 44.1 kHz but 38.4 kHz at 96 kHz, so a partial at
  20 kHz correctly has `aaGain = (22050−20000)/4410 = 0.465` (−6.6 dB) at 44.1 kHz and 0 dB at 96 kHz.
  Amplitudes are therefore compared **only for partials whose synthesized frequency is below
  0.8 × Nyquist at the lowest rate under test (17.64 kHz)**; partials in or above the fade band are excluded
  here and are covered by SC-011 instead. The test configuration is additionally chosen so no active partial
  exceeds that bound (e.g. `f0 = 110 Hz`, 64 partials → 7.04 kHz top partial), and the exclusion is asserted
  rather than assumed. Frequency comparison is unaffected — the fade changes amplitude, not frequency.
  Metric: per-partial frequency (Hz) and amplitude (dB) differences across rates. Threshold: **frequency
  within the SC-001 tolerance; amplitude within 0.5 dB over the in-scope partials**. Test sketch:
  `HarmonicCloud_SampleRateInvariant`. Trace: **spec-added — no roadmap line.** The cross-cutting block
  (roadmap lines 481–489) contains no sample-rate constraint and Phase 2's body (137–164) does not mention
  one; this mirrors Phase 1's coverage ("plus RT-safety and sample-rate-change behaviour",
  `specs/seraphis-phase1-life-modulators/spec.md:68`).
- **SC-011 (Anti-aliasing).** At the worst case for aliasing — **fundamental 4000 Hz (the FR-013 maximum),
  64 partials (FR-012), maximum inharmonicity and maximum |gravity|** — energy in bins reachable only by
  aliased partials is at least **60 dB below** the fundamental's bin energy.
  *Measurement (computed from the cloud's own partial frequencies, not from a harmonic-series helper):*
  1. For each partial `i`, compute its true synthesized frequency `f_i` from FR-083's combined law.
  2. Mark `f_i` aliasing-capable when `f_i > Nyquist`; its image lands at
     `|f_i − round(f_i / fs) · fs|` (`calculateAliasedFrequency`, `spectral_analysis.h:58`, applied to the
     actual `f_i` rather than to `f0 · n`).
  3. Convert with `frequencyToBin` (`spectral_analysis.h:40`) and sum with `detail::sumBinPower`
     (`spectral_analysis.h:207`), **excluding bins within ±2 bins of any legitimately synthesized
     sub-Nyquist partial**.

  `getAliasedBins` / `AliasingTestConfig` (`spectral_analysis.h:168-183`, :112-118) are **explicitly not
  used**: that helper enumerates fold-back bins for *integer harmonics* of a single fundamental
  (`freq = config.testFrequencyHz * n`) and was written for waveshaper tests (it carries a `driveGain`
  field, default `maxHarmonic = 10`). At this criterion's own worst case FR-051 and FR-081 make the partial
  frequencies non-integer multiples of `f0`, so it would compute bins where this component's partials never
  fold — passing a genuinely aliasing build.
  Threshold: **≤ −60 dB** (tightened if measurement shows more headroom; may only be loosened with a
  documented measurement). Test sketch: `HarmonicCloud_NoAliasingAtExtremes`. Trace: FR-015 (itself
  spec-added — the roadmap's line-164 verification list names tilt, inharmonicity and gravity, not
  aliasing); mechanism inherited from `harmonic_oscillator_bank.h:1047-1090`.
- **SC-012 (Stereo equal-power, polarity and spread).** Measured on **the shipped conversion path**
  (Clarifications Q4): each partial is placed at each grid position with FR-008's
  `setPartialPosition(index, position)` and the resulting `panLeft`/`panRight` are read back through
  FR-008's accessors — the test must **not** re-implement the pan law, since the bug FR-091 documents lives
  in the component's own position→gain conversion. Over the pinned position grid
  **{−1, −0.5, 0, +0.5, +1}**, for every partial:
  1. `|panLeft² + panRight² − 1| ≤ 1e-6` (equal power);
  2. `panLeft ≥ 0` **and** `panRight ≥ 0` at every position (no polarity inversion);
  3. `panLeft` is monotonically **decreasing** and `panRight` monotonically **increasing** across the grid;
  4. `|panLeft − panRight| ≤ 1e-6` at position 0 (centre is centre).

  Clauses 2–4 exist because clause 1 alone cannot discriminate a correct pan law from the domain-mismatch
  bug FR-091 documents: feeding a bipolar position into `equalPowerGains` gives `pan = (0, −1)` at
  `pos = −1`, for which `|0 + 1 − 1| = 0` passes clause 1 while the right channel is full-level and
  phase-inverted.
  *Spread behaviour (FR-021's law, measured on a freshly seeded cloud with no `setPartialPosition` override
  in effect):* at spread 0, `max|L[i] − R[i]| ≤ 1e-7` over a full render; as spread increases across
  ≥ 4 settings, the inter-channel correlation coefficient of the rendered output decreases **strictly
  monotonically**. Because `position_i = spread · s_i` with `s_i` fixed per seed, the same seed at two
  spreads gives proportional positions — the ordering under test is therefore a property of the law, not of
  a re-draw. Test sketch: `HarmonicCloud_EqualPowerPanAndSpread`. Trace: roadmap line 155; FR-091.
- **SC-013 (Per-partial envelope offsets).** Measured via FR-008's per-partial current-amplitude accessor,
  sampled once per block through a note-on, with **Mutation and drift at 0** so each partial's target
  amplitude is static and the crossing marks below are well defined. The envelope under test is FR-023's
  **linear AR** (Clarifications Q3): the "steady state" clauses 1–3 refer to is the hold-at-target phase that
  exists while the gate is on.
  *Crossing threshold (defined, not "a defined threshold"):* the first sample at which a partial's current
  amplitude reaches **50% of that partial's steady-state target amplitude**.
  *Thresholds (absolute, not relative — `≥ 5 ×` the zero-spread spread is degenerate because the
  zero-spread spread is by definition 0):*
  1. At offset spread 0: `max − min` crossing time across all active partials **≤ 1 block**.
  2. At maximum offset spread: `max − min` crossing time **≥ 100 ms** and **≤ the documented maximum offset**
     (FR-023).
  3. At both settings, every active partial reaches **≥ 95% of its target amplitude** within the documented
     attack time plus its own offset — i.e. no partial is stranded. Reachable because the attack is linear
     to 100% and FR-023 pins the shortest supported attack at ≥ 20× the FR-014 smoother time constant.
  4. *Decay is the release (FR-023, Clarifications Q3).* After gate-off, every active partial's current
     amplitude is **monotonically non-increasing** and falls to **≤ 1% of its target** within
     `decayTime + that partial's decay offset + 5 × the FR-014 smoother time constant`. No partial sustains
     after gate-off and none is cut off before its decay time elapses.

  Test sketch: `HarmonicCloud_PartialEnvelopeOffsetsStagger`. Trace: roadmap line 146; FR-023; FR-008.
- **SC-014 (Richness law).** Richness is one of the five roadmap macros (line 149) and needs a criterion
  that measures its *defined behaviour*, not only its click-freeness (SC-006).
  *Measurement:* steady render at `f0 = 110 Hz`, 48 kHz, drift/mutation/gravity/inharmonicity at zero, FFT
  magnitude per partial bin, over the **pinned Richness settings {0, 0.25, 0.5, 0.75, 1.0}**.
  *Law check (FR-041, Clarifications Q2).* At each setting, FR-008's reported **active partial count equals
  `round(64^r)` exactly** — 1, 3, 8, 23, 64 respectively — and the measured count above the audibility floor
  **equals that same number**, because at these settings the weakest active partial's rolloff amplitude
  `N^(−p(r))` is −22.7, −31.6, −30.6 and −18.1 dB relative to the fundamental (the `r = 0.75` figure follows
  from `p(0.75) = 1.125` and `N = round(64^0.75) = 23`: `23^(−1.125)` = 0.0294 = −30.6 dB; plan D7), all far
  above the −60 dB floor. A rolloff exponent that does not follow `p(r) = 3.0 − 2.5·r` shows up here as a
  count mismatch or as a per-partial amplitude that misses `n^(−p(r))` by more than 0.5 dB, which is
  asserted per partial.
  *Metrics:* (a) count of partials at or above the **audibility floor of −60 dB relative to the strongest
  partial** (FR-042); (b) the above-fundamental energy fraction, in dB, **floored at −80 dB** so the
  difference stays finite when Richness 0 leaves only the fundamental.
  *Thresholds:* both metrics **monotonically non-decreasing** across the settings (non-decreasing, because
  the count saturates at `kMaxPartials`), **and** Richness 1 exceeds Richness 0 by **≥ 16 partials** above
  the floor and by **≥ 20 dB** in the above-fundamental energy fraction. Test sketch:
  `HarmonicCloud_RichnessAddsPartialsAndEnergy`. Trace: roadmap line 149; FR-041, FR-042.
- **SC-015 (Per-partial drift: independence, decimation, bound).** The roadmap's central Phase-2 mechanism
  (line 147) needs a criterion of its own — SC-005 only asserts the *absence* of clicks with drift at
  maximum, which a no-op drift implementation also satisfies.
  1. *Independence (FR-031).* Log every partial's drift detune once per block for 60 s at 48 kHz via
     FR-008's drift accessor. The mean pairwise Pearson correlation across all partial pairs is
     **|r| ≤ 0.2**, and no single pair exceeds **|r| ≤ 0.5**. A single shared walk would give r ≈ 1.
     (Pearson correlation is scale-invariant, so FR-022's per-partial `amount_i` cannot mask a shared walk
     here.)
  2. *Decimation and block-size invariance (FR-032, Clarifications Q7).* Render the same total sample count
     `N` three ways — one block of `N`, `N/512` blocks of 512, and blocks of 577 (deliberately neither a
     multiple of `kControlChunkSamples = 64` nor of `kControlRateInterval = 32`) — and assert:
     (a) each partial's drift value after the last block agrees across all three within **1e-5** (the OU
     walk's state depends only on the total samples advanced, not on how they were partitioned);
     (b) the number of cloud-side drift **reads per partial** equals **`Σ ceil(blockSize / 64)`** for each
     schedule — `N/64`, `N/64` and `10` per 577-sample block respectively — not one per block;
     (c) the **rendered output** of the single-block and 512-block schedules agrees within
     `render_fingerprint.h` tolerances, because both partition into identical 64-sample chunks. This is the
     assertion that makes the sound block-size-invariant; a literal one-read-per-block implementation fails
     it outright (its 512-sample schedule holds detune 8× longer than its single-block schedule).
     Together these pin FR-032's real contract (one *read* per partial per chunk; `BrownianDrift` steps its
     OU walk internally at its own rate) rather than the impossible "one OU step per block" reading.
  3. *Bound and per-partial amount law (FR-033, FR-022).* Over a 60 s render at maximum drift depth:
     (a) `max |measured partial detune|` in cents is **≤ the configured cents depth** within the SC-001
     estimator tolerance — a true upper bound because `amount_i ≤ 1` — and is **exactly 0** at depth 0;
     (b) *liveness:* the maximum over all partials is **≥ 0.25 × the configured cents depth**, so a no-op or
     uniformly-tiny drift implementation fails;
     (c) *index scaling:* the mean `|detune|` over partials `n ∈ [33, 64]` is **≥ 4×** the mean over
     `n ∈ [1, 8]` (FR-022's law gives ≈ 10.8×, so the threshold discriminates rather than describes);
     (d) *seeded scatter:* the per-partial mean `|detune|` sequence over `n = 1 … 64` is **not** monotonically
     increasing — it contains at least one inversion, which a pure index-scaled law (no scatter) cannot
     produce.
  4. *Shared configuration (FR-035).* After one cloud-level `setDriftSmoothness` / `setDriftDepth` call,
     every partial's observed drift respects the new bound — asserted for all 64, not a sample. Neither
     setter may alter the FR-072 mutation bank; that independence is measured by SC-016.

  Test sketch: `HarmonicCloud_DriftIsIndependentDecimatedAndBounded`. Trace: roadmap line 147;
  FR-031 … FR-035.
- **SC-016 (Mutation bounds, level stability and independence from Drift).** *Measurement:* 60 s renders at
  Mutation **0, 0.5 and 1.0**, drift active, sampling every partial's mutation weight once per block as
  `weight = targetAmplitude ÷ unmutatedTargetAmplitude` from FR-008's two accessors. Sampling starts **after
  the documented attack time has elapsed** with the gate held, so `env_i = 1` and the ratio is exactly `w_i`
  rather than `w_i · env_i` (FR-017's composition chain).
  *Thresholds:* per-partial weight extrema stay within **[0.25, 1.75]** at every setting (FR-073); the block
  RMS over any **1 s** window stays **within ±3 dB of the Mutation-0 RMS** of the same configuration; and
  the per-block weight change never exceeds the FR-071 rate bound
  (`|Δw| ≤ 10 s⁻¹ × blockDuration`, from `BrownianDrift`'s 150 ms output smoother,
  `brownian_drift.h:103`). At Mutation 0 every weight is **exactly 1.0**.
  *Independence from Drift (FR-072, Clarifications Q1) — two further configurations:*
  1. **Drift depth 0, Mutation 1.0.** Weights must still move: `max |w_i − 1| ≥ 0.1` for **at least half the
     active partials** over the render, and the ±3 dB level-stability bound still holds. A shared-instance
     implementation freezes every weight at exactly 1.0 here and fails.
  2. **Drift depth max, Mutation 0.** Every weight is **exactly 1.0** while the measured detune moves,
     confirming the drift bank does not leak into the amplitude path.

  Additionally, for each partial the Pearson correlation between its weight series and its own detune series
  is **|r| ≤ 0.3** (distinct seeds give r ≈ 0; a shared instance gives r ≈ 1). Test sketch:
  `HarmonicCloud_MutationStaysBoundedAndLevelStable`. Trace: roadmap lines 78, 152; FR-071, FR-072, FR-073.
- **SC-017 (Bounded, finite output over the parameter grid; setter hygiene).** Makes FR-006's unbounded
  "any parameter combination" finite, and turns FR-007 into a single assertion.
  *Grid (Cartesian, fully enumerated):* {min, mid, max} × each of the five macros (Richness, Inharmonicity,
  Tilt, Mutation, Gravity) × {min, max} fundamental (20 Hz, 4000 Hz per FR-013) × {0, max} drift depth ×
  {0, max} stereo spread × {0, max} envelope offset spread — each combination rendered **1 s** at 48 kHz
  through a note-on. The `{0 drift depth} × {max Mutation}` cells are live mutation cells, not inert ones,
  under FR-072's independent mutation bank (Clarifications Q1).
  *Thresholds:* **every** output sample is finite by bit-pattern test (never `std::isnan`; the
  `stateFinite()` idiom at `harmonic_oscillator_bank.h:622-633`), `|output| ≤ kOutputClamp`, and
  `stateFinite()`-equivalent internal state holds at the end of each render.
  *Setter hygiene (FR-007):* for **every** setter — fundamental, the five macros, drift depth, drift
  smoothness, stereo spread, envelope attack/decay/offset-spread, and FR-008's `setPartialPosition` — call it
  with a bit-pattern-constructed NaN and with ±Inf
  (built via a `volatile` sink, not `std::numeric_limits`, which folds under `-ffast-math` — see
  `reference_fastmath_nan_in_tests`), then assert the corresponding getter returns **exactly** its
  pre-call value and a subsequent render is identical to the pre-call render within
  `render_fingerprint.h` tolerances. Test sketch: `HarmonicCloud_ParameterGridStaysFiniteAndBounded`.
  Trace: FR-006, FR-007; roadmap line 481.
- **SC-018 (Onset is non-coherent and below the clamp).** With **64 partials at maximum Richness**, maximum
  Tilt toward the upper partials, and FR-016's seeded initial phases, a note-on render's **peak in the first
  100 ms is below `kOutputClamp = 2.0f`** (`harmonic_oscillator_bank.h:90`) with a documented margin
  (**peak ≤ 0.9 × kOutputClamp**) — this is the measurement of FR-017's 11.1 dB crest headroom above the
  0.5 target RMS, which the expected-RMS normalization basis (Clarifications Q6) leaves rather than
  guarantees arithmetically — and the onset **peak-to-RMS ratio over that window is ≤ 6 dB above** the
  steady-state peak-to-RMS of the same render. A phase-0 (coherent) implementation sums all partials in
  phase at t = 0 and fails both. Additionally, across **≥ 8 distinct seeds**, the measured onset peak varies
  — a fixed onset peak across seeds means the phases are not actually seeded. Test sketch:
  `HarmonicCloud_OnsetIsPhaseIncoherent`. Trace: FR-016; Assumption 5; roadmap line 145.

## Edge Cases

- **RT-safety boundaries.** `processStereoBlock(..., 0)` is a no-op leaving state unchanged; null output
  pointers are rejected without writing. **Any** block size — including sizes that are not multiples of
  `kControlChunkSamples = 64` or of `BrownianDrift`'s `kControlRateInterval = 32` (`brownian_drift.h:105`),
  and very large blocks (e.g. 16384 samples) — is rendered as `ceil(numSamples / 64)` internal chunks, with
  one `processBlock(chunkLength)` call and one read per partial per chunk (FR-032). A 16384-sample block is
  therefore 256 chunks, not one frozen 341 ms detune value. The number of *internal* OU steps varies with
  chunk length by design (`brownian_drift.h:194-206`) and only the cloud's per-partial read is decimated.
  A final short chunk (`numSamples % 64`) is handled like any other. Very large blocks must not overrun any
  fixed array. Gated by SC-015 clause 2. Processing before `prepare()` outputs silence rather than reading uninitialized coefficients
  (the existing bank's `prepared_`/`frameLoaded_` guard pattern, `harmonic_oscillator_bank.h:684-687`).
- **Parameter extremes.** Richness at minimum (one partial) and maximum (64 partials) both render bounded
  audio; fundamental at the low end (20 Hz) and high end (4000 Hz, the FR-013 maximum — at 44.1 kHz its 6th
  partial already exceeds Nyquist) must not alias or produce NaN, and the upper partials must fade to
  silence smoothly via FR-015 rather than switch off. Inharmonicity and |gravity| at maximum
  simultaneously, at the maximum fundamental, is the SC-011 worst case and must remain stable (epsilon clamp
  per FR-015). Tilt at −12 and +12 dB/oct with 64 partials must not overflow the output clamp — FR-017's
  normalization is what guarantees this, and SC-003's `peak < 0.9 × kOutputClamp` precondition is what
  proves it. Mutation at 1.0 must never drive a partial weight outside [0.25, 1.75] (FR-073, SC-016). The
  whole extreme surface is enumerated and rendered by SC-017.
- **Sample-rate changes.** Calling `prepare()` again with a different sample rate re-derives MCF epsilons,
  amplitude-smoothing coefficient, anti-alias fade points, envelope rates, and propagates to every
  `BrownianDrift`. Behaviour is defined in Hz and seconds, never in samples (SC-010). A sample-rate change
  mid-note may reset state (it is not an audio-thread operation) but must not leave stale coefficients.
- **Gate edges and retrigger.** A note-on while the cloud is **not** quiescent keeps every partial's phase
  and re-opens the envelope from its current value (FR-016, FR-023) — no phase redraw, no envelope restart
  from 0, hence no click (SC-006's retrigger clause). A note-on while quiescent (gate off and every
  `currentAmplitude` below `kQuiescenceAmplitude`) redraws phases. A note-off during the attack starts the
  decay from wherever the envelope currently is, and a note-on during the decay reverses it upward from the
  same value; neither may snap. Repeated note-on with no intervening note-off is idempotent apart from
  re-opening the envelope.
- **Seed determinism.** Seed 0 is handled safely — `Xorshift32`'s constructor substitutes a default
  (`random.h:44`) — and each of the **128** drift instances' derived seeds (64 detune, FR-031; 64 mutation,
  FR-072) must remain distinct after that substitution. `reset()` returns the cloud to its exact
  post-`prepare` state including every drift instance's RNG stream, since `BrownianDrift::reset()` rewinds
  to the configured seed (`brownian_drift.h:133`, `initState()` at :242-247). The three once-per-seed draws
  — FR-016's initial phases, FR-021's stereo scatter `s_i`, FR-022's drift amounts `u_i` — are taken from
  the cloud stream in a **fixed, documented order** so `reset()` reproduces all of them exactly. Two renders
  separated by `reset()` are therefore comparable under SC-009.
- **Non-finite hygiene.** No NaN/Inf may reach the output for any parameter combination, including
  non-finite values passed to setters (FR-007). Guards use bit-pattern tests, never `std::isnan`
  (`-ffast-math` on the macOS leg); the existing `stateFinite()` bit test
  (`harmonic_oscillator_bank.h:622-633`) is the reference. Denormal partial amplitudes are flushed rather
  than allowed to stall the FPU.
- **Portability.** The component must pass `node tools/check-portability.js`; no narrowing in brace
  initialization; any new SIMD must use unaligned loads/stores unless alignment is proven, matching the
  existing kernel's `hn::LoadU`/`hn::StoreU` (`harmonic_oscillator_bank_simd.cpp:80-110`).

## Existing Components (reused — verified this session)

| Component | Header (verified) | Real signature / what is reused |
|---|---|---|
| SIMD MCF batch kernel | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.h:33-46` | **Not a class.** A single free function: `void processMcfBatchSIMD(float* sinState, float* cosState, const float* epsilon, const float* detuneMultiplier, float* currentAmplitude, const float* targetAmplitude, const float* antiAliasGain, const float* panLeft, const float* panRight, float ampSmoothCoeff, int numPartials, float& sumL, float& sumR) noexcept`. Does amplitude smoothing + MCF advance + stereo pan accumulation across partials; explicitly does **not** handle bandwidth modulation (:31-32). Implementation uses `hn::LoadU`/`hn::StoreU` with `hn::ScalableTag<float>` (`..._simd.cpp:62-110`) — unaligned, so any partial count is safe. The roadmap's "`HarmonicOscillatorBankSimd`" (line 145) resolves to this function. |
| `HarmonicOscillatorBank` (L2) | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h:74` | **Math and structure reused, class NOT inherited or instantiated.** Verified reusable mechanisms: SoA `alignas(32)` layout (:1155-1163); amp-smooth coefficient derivation (:136-137); epsilon from frequency with `kMaxEpsilon = 1.99f` clamp (:1047-1057); anti-alias fade + MCF elliptical correction `cos(π·f/fs)` with `kAntiAliasFadeStart = 0.8f` (:1062-1090); constant-power pan table (:1102-1128); tail fade-out of deactivated partials (:746-779); output clamp `kOutputClamp = 2.0f` (:90); `-ffast-math`-safe `stateFinite()` bit test (:622-633); `processStereoBlock(float*, float*, size_t)` (:803). Not reused as a class because its whole input contract is `loadFrame(const HarmonicFrame&, float, bool)` (:254) and its capacity is `kMaxPartials = 96` from the analysis pipeline. |
| `BrownianDrift` (L2, Phase 1) | `dsp/include/krate/dsp/processors/brownian_drift.h:94` | `class BrownianDrift : public ModulationSource`. Used verbatim: `prepare(double)` (:121), `reset()` (:133), `setSeed(std::uint32_t)` (:145), `setSmoothness(float)` (:152), `setDepth(float)` (:159), `setMean(float)` (:165), `processBlock(size_t)` (:194), `getCurrentValue()` (:212), `getSourceRange()` → fixed `{-1,+1}` (:217). `kControlRateInterval = 32` (:105); output smoothing 150 ms (:103). |
| `AdditiveOscillator` (L2, math reference) | `dsp/include/krate/dsp/processors/additive_oscillator.h:62` | **Laws reused, class not used** (it is IFFT/overlap-add, unsuitable for a per-voice cloud). Inharmonicity: `calculatePartialFrequency` → `ratio * fundamental * sqrt(1 + B·n²)` (:466-474), clamp `kMaxInharmonicity = 0.1f` (:90, :339). Tilt: `calculateTiltFactor` → `pow(10, tiltDb·log2(n)/20)` (:480-490). Setter NaN/Inf rejection pattern (:318-340). |
| `SpectralTilt` (L2, convention only) | `dsp/include/krate/dsp/processors/spectral_tilt.h:88` | `class SpectralTilt` is a **dual-shelf IIR** (`updateCoefficients` :349-386, heuristic `kReferenceMultiplier = 1.5f`), not a per-partial gain law. Reused only for its range convention: `kMinTilt = -12.0f` / `kMaxTilt = +12.0f` (:98-101), `kDefaultPivot = 1000.0f` (:124). See Assumption 3. |
| `equalPowerGains` (L0) | `dsp/include/krate/dsp/core/crossfade_utils.h:50` | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` → `cos(position·kHalfPi)` / `sin(position·kHalfPi)` (:51-52); pair overload at :64. Direct equal-power pan law for FR-091. |
| `Xorshift32` (L0) | `dsp/include/krate/dsp/core/random.h:40` | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` (:44); `nextFloat()` → [-1,1] (:58); `nextUnipolar()` → [0,1] (:66); `seed(uint32_t)` (:72); `state()` (:78). Seed derivation for per-partial drift/scatter. |
| `OnePoleSmoother` (L1) | `dsp/include/krate/dsp/primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (:160); `setTarget(float)` (:170); `process()` (:197); `advanceSamples(size_t)` (:243); `snapTo(float)` (:263); `getCurrentValue()` (:191). Available for any cloud-level smoothed control; per-partial amplitude smoothing is the kernel's own `ampSmoothCoeff` path. |
| `midiNoteToFrequency` (L0) | `dsp/include/krate/dsp/core/midi_utils.h:71` | `[[nodiscard]] constexpr float midiNoteToFrequency(...)`, `kA4FrequencyHz = 440.0f` (:33). Optional convenience for test fixtures; the cloud's own pitch input is Hz (FR-013). |
| `pitch_utils` (L0) | `dsp/include/krate/dsp/core/pitch_utils.h` | `semitonesToRatio(float)` (:23) — cents→ratio for the FR-033 drift-depth bound (`ratio = semitonesToRatio(cents / 100.0f)`). `ratioToSemitones(float)` (:31) — SC-001's cent metric is `100.0f * ratioToSemitones(fMeasured / (f0 * n))`, i.e. `1200·log2(fMeasured / (f0·n))`. **`frequencyToCentsDeviation(float)` (:175) is NOT usable and is not reused:** it returns deviation from the nearest *chromatic 12-TET note centre* (`midiNote − round(midiNote)`, ×100) and is documented as ranging only [-50, +50] cents (:169-170), so it both re-references to the wrong target and wraps — it cannot express an error of arbitrary magnitude against an arbitrary target `f0·n`. Named in the roadmap reuse inventory (line 86). |
| `ModulationSource` (L0) | `dsp/include/krate/dsp/core/modulation_source.h:31` | Pure virtuals `getCurrentValue() const noexcept` (:37) and `getSourceRange() const noexcept` (:41). Relevant only as the interface `BrownianDrift` already implements; `HarmonicCloud` is a sound source, not a modulation source, and does **not** implement it. |
| `harmonic_types.h` (L2) | `dsp/include/krate/dsp/processors/harmonic_types.h:21,36,54` | **Explicitly NOT reused.** `kMaxPartials = 96` (:21), `struct Partial` (:36), `struct HarmonicFrame` (:54) are the analysis-pipeline data contract; roadmap lines 157–159 forbid a `HarmonicFrame` dependency. Verified so the plan does not reach for `kMaxPartials`. |
| `harmonic_snapshot.h` / `harmonic_frame_utils.h` (L2) | `.../harmonic_snapshot.h:30`, `.../harmonic_frame_utils.h:38` | **Explicitly NOT reused.** `struct HarmonicSnapshot` (:30) stores captured Innexus analysis state; `lerpHarmonicFrame(const HarmonicFrame&, const HarmonicFrame&, float)` (:38) operates on `HarmonicFrame`. Both are analysis-side. The roadmap lists them in the Phase-2 reuse row (line 86) but the Phase-2 body overrides that (lines 157–159); the related-but-distinct Phase 3 `SpectralState` will re-examine `HarmonicSnapshot` for ODR overlap (roadmap line 176). |

Test-side helpers verified this session for the success criteria:

- `render_fingerprint.h:46-101` — `RenderFingerprint`, `fingerprintRender`, `compareFingerprints`,
  tolerances at :49-52 (`kSampleTolerance = 1.0e-4f`, `kMetricTolerance = 1.0e-5`).
- `allocation_detector.h:26,75` — `AllocationDetector`, `AllocationScope`. **Inert on its own:** the global
  `operator new`/`delete` replacements in that header are commented out (:99-110); counting requires
  `allocation_operator_overrides.h`, which must be included from exactly one TU per binary and is already
  provided to `dsp_systems_tests` by `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`. See SC-008.
- `artifact_detection.h:38,99,130` — `ClickDetectorConfig` (:38), `ClickDetector` (:99),
  `ClickDetector::detect(...)` (:130); the within-frame threshold is
  `mean(|dx|) + detectionThreshold·stddev(|dx|)` at :186-193. The same file also defines `LPCDetector`
  (:306, with its own `detect(...)` at :336) and `SpectralAnomalyDetector` (:534, `detect(...)` at :566) —
  **neither is used by SC-005/SC-006**; the line numbers matter because following a wrong one lands on a
  different detector.
- `spectral_analysis.h:40,58,207` — `frequencyToBin` (:40), `calculateAliasedFrequency` (:58),
  `detail::sumBinPower` (:207, note the `detail` namespace, opened at :189). **`getAliasedBins` (:168-183)
  and `AliasingTestConfig` (:112-118) are deliberately NOT used** — see SC-011 for why they compute the
  wrong bins for a non-integer partial series.
- `signal_metrics.h:326` — `calculateSpectralFlatness` (available; not required by any criterion here).

## New Components (ODR-swept this session)

ODR sweep executed this session:
`grep -rn "class HarmonicCloud|struct HarmonicCloud|HarmonicCloud" dsp/ plugins/ tools/` and
`grep -rn "class CloudPartial|struct CloudPartial|class PartialCloud|class SpectralGravity|struct CloudParams|class CloudVoice" dsp/ plugins/`.

| Class | Layer | Header path (new) | ODR sweep result |
|---|---|---|---|
| `HarmonicCloud` | 3 | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | **Clear** — zero matches for `HarmonicCloud` anywhere in `dsp/`, `plugins/`, or `tools/` (not even in comments). |

Near-name hazards checked and cleared for candidate helper names, should the plan need one:
`CloudPartial`, `PartialCloud`, `SpectralGravity`, `CloudParams`, `CloudVoice` — **all clear** (zero
matches in `dsp/` and `plugins/`).

Near-name hazards that **exist** and must not be collided with or confused: `HarmonicOscillatorBank`
(`harmonic_oscillator_bank.h:74`), `AdditiveOscillator` (`additive_oscillator.h:62`), `HarmonicSnapshot`
(`harmonic_snapshot.h:30`), `SpectralTilt` (`spectral_tilt.h:88`), `ResonatorBank` / `ModalResonator` /
`GranularEngine` (roadmap ODR note, line 96–97). Any additional class or struct the plan introduces must
be re-swept before it is written (roadmap line 483).

## Open Questions

**None.** The only roadmap Open Question assigned to this phase — OQ-1, exact partial count (roadmap
line 494) — is **closed in the Clarifications section above**: fixed `kMaxPartials = 64`, no runtime tier,
re-opened in Phase 7 only if SC-007 measures ≥ 2× headroom. No requirement or criterion in this document is
conditional on a future measurement.

## Review notes

All issues raised in review were accepted and applied; none were rejected. Two were resolved by taking the
reviewer's explicitly-offered alternative branch rather than the first-listed suggestion, recorded here so
the choice is visible:

1. **FR-023 (cloud-owned envelope base times).** The reviewer offered either narrowing FR-023 to
   caller-supplied base times and gate, or documenting in Assumption 2 that cloud-owned base times are
   required for Phase-2 testability and stating how Phase 7 supersedes them. The second branch was taken:
   an offset is only measurable relative to a base time, SC-013 must observe staggering with no voice in
   existence, and Assumption 2 now records the supersession path plus a hard scope fence ({attack, decay,
   offset spread, gate} and nothing else) so Phase 7 has nothing to unwind.
2. **FR-080 series ("toward" the harmonic grid).** The reviewer offered either defining a contraction law
   over the inharmonicity displacement, or implementing only the "away" half and recording the deviation in
   Assumptions. Both halves of the second branch were taken *and* an explicit law was written: FR-081 now
   defines `ratio_g(n) = n^(1 + g·kGravityExponentRange)` and FR-083 fixes the composition order with
   Inharmonicity, while Assumption 4 records that no gravity setting re-harmonizes a spectrum already
   displaced by `B ≠ 0`. A contraction law over the inharmonicity displacement was rejected on its merits:
   it would make gravity a **no-op at `B = 0`**, which contradicts roadmap line 154 ("0 = pure harmonic,
   ± = stretched/compressed") and would make SC-004's standalone gravity sweep unmeasurable.
