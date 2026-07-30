# Feature Specification: Seraphis Phase 6 — Aether Space Engine

**Spec slug:** `seraphis-phase6-aether-space`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 6 (lines 258–282); reuse-inventory row
line 91; cross-cutting constraints lines 491–502; roadmap Open Question 3 (line 508).
**Citation discipline:** every roadmap line number below was **re-verified against
`specs/Seraphis-roadmap.md` at revision time**. Anchor points, so a later drift is detectable:
Phase 6 body = 258–282; "Global (post-voice-sum), one instance" = 262; Size = 268; Density = 269;
Decay = 270; Dimensionality = 271–272; shimmer taps = 273–274; harmonic bloom = 274–276; independent
send level = 276; spectral diffusion = 277; life-modulated internals = 278; success criteria = 280–282;
Phase 10's "spectral freeze (global capture-and-hold of the Aether tail)" = **446** (line 439 is inside
the *Phase 9* paragraph and was mis-cited in the previous revision).
**Layers:** one new **Layer 4** component (`AetherReverb`), header
`dsp/include/krate/dsp/effects/aether_reverb.h` — the header path the roadmap itself names (line 264).
**Depends on:** Phase 1 only, and only for the life modulators it owns as members (`BreathingModulator`,
`TidalModulator`, `BrownianDrift` — all Layer 2, all legally includable from Layer 4). Phases 2/4/5 are
**not** compile-time dependencies: the roadmap makes 2/4/5/6 mutually independent (lines 487–489) and this
engine consumes a stereo audio stream, not a `HarmonicCloud`, `ContinuousBody` or `AtmosphereEngine`.
**Plugin work:** none. KrateDSP only, unit-tested. The Seraphis plugin starts at Phase 8.

---

## Overview

Phase 6 delivers **the integrated environment**: an algorithmic (no IRs) infinite reverb with shimmer
bloom and spectral diffusion, global and post-voice-sum, one instance (roadmap lines 260–262). It is the
one component the roadmap explicitly refuses to call a post-effect — *"Space is part of the instrument …
in the core signal path with its own spectral state, not a post-effect. Freeze/infinite-decay is a
first-class playing technique"* (lines 73–74).

`AetherReverb` is a **new** feedback delay network — not an extension of `FDNReverb`, for reasons the
roadmap already anticipates (*"likely a new 8×8/16×16 network rather than extending the
delay-plugin-flavoured one"*, lines 265–266) and which C-1 below turns into a verified list. Around that
core it adds four things the repo has no component for:

1. **Dimensionality** — a continuous morph of the feedback matrix's *character* (2D plate → 3D hall →
   N-D impossible) that stays **exactly orthogonal at every point on the path**, because an infinite
   decay that is only approximately lossless is not infinite (C-3, FR-022).
2. **Size** — a scaling of the delay-line lengths themselves, and therefore of modal density
   (cathedral → impossible spaces). `FDNReverb` has no such control: its `roomSize` moves feedback gain
   and T60 only, while the delay lengths are fixed at `prepare` and hard-clamped to 3–20 ms
   (`effects/fdn_reverb.h:142-144`) — a shoebox (C-1).
3. **Shimmer bloom inside the loop** — pitch-shifted feedback taps at +12 and +7 with independent send
   levels, plus a **harmonic bloom** resonator bank that reinforces the partials of the held chord
   (roadmap lines 273–276).
4. **Spectral diffusion** — STFT-domain tail smearing for the "underwater chamber" character
   (roadmap line 277).

The genuinely new engineering is **energy conservation under morphing**. The roadmap asks for
`RT60 → ∞` (line 270), a matrix that morphs (line 271), and internals that "slowly breathe" (line 278)
— three requirements that fight each other, because every conventional way of interpolating a matrix or
of reading a moving fractional delay is *lossy*, and 60 s of a 30 ms loop is ~2000 round-trips through
whatever loss you left in (2.9 million sample-accumulations). C-3 computes what the obvious matrix
interpolation actually does: at the midpoint of the morph it is **singular**. This spec pins exactly
where losslessness is required, exactly which mechanisms are admissible there, and measures both
(SC-002, SC-004).

Every claim below about existing code was verified by opening the header in the session that produced
this document; each cites `file:line`. Where the roadmap names a component that does not exist, does not
have the stated capability, or cannot be used the stated way, the discrepancy is recorded in
**Roadmap-vs-Reality Corrections** rather than silently papered over.

---

## Scope

**In scope (this phase ships all of it):**

1. `AetherReverb`: a stereo-in / stereo-out Layer 4 effect with `prepare` / `reset` / `silence` /
   `processStereoBlock`, a fully pinned control table, and seeded determinism.
2. A new FDN core: `N` channels — **both orders ship**, `N = 8` is the default and the only order the
   always-on gates measure, `N = 16` is reachable through `PrepareConfig` (Q3) — long delay lines with
   **Size**-scaled lengths,
   per-channel Jot absorption, DC blocking, pre-delay, stereo output taps.
3. **Dimensionality**: an orthogonality-preserving morph across three matrix characters
   (Householder ↔ **sign-corrected** Hadamard ↔ seeded random-orthogonal, all three pinned into the
   `det = −1` component of `O(N)` — C-8), traversed by the **real-Schur geodesic** FR-022 pins (Q4),
   with measured orthogonality **and determinant** bounds.
4. **Decay**: RT60 from 0.5 s to 60 s, plus `setFreeze(true)` — unity feedback, damping bypassed,
   fractional delay reads latched, energy conserving.
5. **Density**: **input** diffusion via the reused `DiffusionNetwork` (Layer 2), on the input path only
   (FR-040). There is deliberately **no in-loop diffusion stage** — later generations are re-smeared by
   the orthogonal matrix and the coprime line lengths, not by a second allpass chain, and SC-003's
   echo-density criterion is written against that topology.
6. **Shimmer bloom**: two pitch-shifted feedback taps (+12, +7) via the reused `PitchShiftProcessor`,
   each with an independent send level, driven on a fixed 64-sample cadence (Q5) and injected back into
   the network at pinned channel subsets (Q6).
7. **Harmonic bloom**: a **note-informed** resonator emphasis bank of `kMaxBloomResonators = 32`
   resonators inside the loop (Q1, Q7), tuned through `bloomNoteOn`/`bloomNoteOff` (FR-056), reinforcing
   the partials of the held chord, with its own send level and a stated stability guard (FR-058).
8. **Spectral diffusion**: one stereo STFT ↔ OverlapAdd phase-smearing stage on the wet path, a
   `prepare`-time flag **defaulting to on** (Q2).
9. **Life-modulated internals**: Size breathes (`BreathingModulator`), Dimensionality drifts on a tidal
   period (`TidalModulator`), each with its own depth control, all seeded and deterministic.
10. Unit tests covering every FR and every SC, registered in `dsp/tests/CMakeLists.txt`'s
    `dsp_effects_tests` source list (`:364-389`) — sources are listed explicitly, not globbed —
    split into an **always-on core inside a stated wall-clock budget** and `[.slow]` / `[.perf]` full
    grids (SC-0's B-1…B-5). `dsp_effects_tests` is CI-blocking on every build, so its runtime is a
    requirement of this phase, not an afterthought.

**Two shipped controls are *not* roadmap-derived, and are marked as such so Phase 9 inherits them
knowingly.** Roadmap Phase 6 names exactly Size, Density, Decay, Dimensionality (lines 268–272), three
independent send levels (line 276), spectral diffusion (line 277) and life modulation of size and matrix
(line 278). FR-009's table additionally ships `setPreDelayMs` (0…200 ms) and `setWidth`. Both are kept,
with the reason recorded here and a Traceability row each:

- **`setPreDelayMs` (FR-015)** — kept because the *stereo* pre-delay pair is what carries the input
  into `DiffusionNetwork`'s stereo-in/stereo-out decorrelator (FR-015a); removing the control would not
  remove the two `DelayLine`s, only the ability to set their length. It costs two 200 ms lines at
  `prepare`. Every in-repo reverb has one (`ReverbParams::preDelayMs`, `effects/reverb.h:153`).
- **`setWidth` (FR-080)** — kept because a stereo reverb with no width control is not usable as the
  instrument's only space, and because FR-018's even/odd tap split makes M/S width a one-line operation
  on a signal that is already stereo. **Note the Phase 10 overlap**: the roadmap assigns *stereo
  wandering* — `BrownianDrift` → M/S width + azimuth via `midside_processor` — to Phase 10 (lines
  448–449). Phase 6 ships the **static** width control only; Phase 10 owns the *modulation* of it and
  must modulate this setter rather than adding a second width stage.

**Non-goals (owned by later phases, or deliberately excluded):**

- **N-1 — No voice, no notes, no polyphony.** `AetherReverb` is post-voice-sum and global (roadmap line
  262). Voice allocation, per-voice seeds and the voice sum are Phase 7 (lines 286–300).
- **N-2 — No wiring to Phases 2/4/5.** The engine takes a stereo stream; Phase 7 connects it.
- **N-3 — No macro system.** Dream / Bloom / Dissolve / Gravity / Entropy are Phase 7 modulation-matrix
  presets over this engine's setters (roadmap lines 301–307). Phase 6 ships the setters, not the macros.
- **N-4 — No global spectral freeze effect.** "Spectral freeze (global capture-and-hold of the Aether
  tail)" is explicitly **Phase 10** (roadmap line **446**). Phase 6's `setFreeze` is the FDN's own
  unity-feedback infinite decay (line 270), a different thing: it holds the *network state*, not a
  captured spectrum.
- **N-5 — No spectral delay, no tape saturation, no stereo wandering, no true-peak limiting.** All
  Phase 10 (lines 446–449) or Phase 7's output stage (lines 299–300).
- **N-6 — No changes to `FDNReverb`, `Reverb`, `ShimmerDelay`, `DiffusionNetwork`,
  `PitchShiftProcessor` or `SympatheticResonance`.** Every one of them ships in Iterum and/or Ruinae;
  changing any changes released sound. Phase 6 composes them or reimplements a specific mechanism in its
  own header, and records each such decision below.
- **N-7 — No plugin parameters, no UI, no presets.** Phases 8/9/11/12.
- **N-8 — No *shimmer* below 44.1 kHz** (the constraint is scoped to its actual cause; the previous
  revision imposed an engine-wide 44.1 kHz floor and is corrected here — C-6, RA-6).
  `PitchShiftProcessor::prepare` documents the precondition
  `sampleRate >= 44100.0 && sampleRate <= 192000.0` (`processors/pitch_shift_processor.h:138-142`), and
  FR-050 puts two of them inside the loop. That precondition constrains **the shimmer taps**, not the
  FDN, the diffuser, the bloom bank or the spectral stage — and it does not apply at all when
  `config.shimmerEnabled == false`, in which case FR-003 allocates no `PitchShiftProcessor`. The engine
  therefore keeps `FDNReverb`'s own **[8000, 192000] Hz** range (`effects/fdn_reverb.h:13`, `:130`) and
  **force-disables the shimmer taps below 44.1 kHz** (FR-003) rather than falsifying the sample rate.
- **N-9 — No `IResonator` implementation.** `IResonator` (`processors/iresonator.h:32`) is a per-note
  body abstraction (`setFrequency(float f0)`, `setDecay(float t60)`, `getPerceptualEnergy()`,
  `:42-63`); the harmonic bloom is a bank tuned to many partials at once, not one body. Implementing the
  interface would buy nothing and would imply Phase 4 interchangeability that does not exist.

---

## Clarifications

### Session 2026-07-29

- **Q1 — Where does the harmonic bloom get its resonator frequencies (OQ-C)?** → **Note-informed,
  FR-056(b) only.** Ship `bloomNoteOn(voiceId, const float* partialHz, count)` /
  `bloomNoteOff(voiceId)`; Phase 7 forwards note events into the effect (recorded as **RA-7**). SC-016
  clause 3 uses the note-informed variant; `copyBloomTargetsHz` is **not** shipped; the bloom is **not**
  coupled to the spectral-diffusion flag.
- **Q2 — Is spectral diffusion always-on, `prepare`-time optional, or off by default (OQ-B)?** →
  **`prepare`-time flag defaulting to `true`** (as `PrepareConfig` already had it). Default latency
  1024 samples (21.3 ms at 48 kHz), with the dry path delayed to match (FR-062); SC-018 clause 1 keeps
  both cases (flag off ⇒ exactly 0). A **runtime** toggle stays excluded.
- **Q3 — Does Phase 6 ship `N = 16` at all (OQ-A)?** → **Ship both FDN orders, `N = 8` default.**
  `N = 8` is the only always-on gated order; the second coprime delay table for `N = 16` **ships**;
  `N = 16` is covered in `[.slow]` grids only (SC-002/003/004) and by SC-008 configuration (c), the
  `N = 16` everything-on worst case. Promotion of 16 to the default is deferred to the measured
  `compliance.md` figures.
- **Q4 — Which orthogonality-preserving morph mechanism ships (FR-022)?** → **Mechanism 2, the
  real-Schur geodesic.** `V` and `θ` per segment are computed at `prepare`; `M(u) = A·V·B(u·θ)·Vᵀ` is
  evaluated on the control grid. Exactly orthogonal, endpoint-exact, constant angular rate. The
  hand-written symmetric-eigen / Schur reduction gets its own unit tests (SC-004 clause 6). FR-022 names
  this as **the** mechanism; the Householder-product alternative is not shipped.
- **Q5 — At what cadence is each `PitchShiftProcessor` driven inside the per-sample loop (FR-050,
  FR-054)?** → **Fixed 64-sample cadence on the absolute control grid, injected one chunk late**
  (+64 samples per leg). FR-054's loop-time figures become "mode latency **+ 64 samples**". This makes
  SC-011's block-partition invariance **structural**.
- **Q6 — Which FDN channels do the shimmer/bloom taps read and inject into, and at what gain (FR-050,
  FR-051, FR-055, FR-058)?** → **Fixed 4-channel subset.** Taps read the four longest lines at mono-sum
  normalisation `1/4`; the +12 tap injects into the pinned pair `{1, 4}` and the +7 tap into `{3, 6}` at
  `N = 8` (analogous pinned, parity-spanning pairs at `N = 16`), at injection gain
  `sqrt(2/|subset|)·send`; the bloom reads the same mono sum and injects into the remaining channels at
  the same normalisation. All values are written into FR-050/FR-055 as named constants.
- **Q7 — What is `kMaxBloomResonators`, and what does FR-058's guard target (FR-055, FR-058)?** →
  **`kMaxBloomResonators = 32`**, with per-resonator inverse-peak-gain normalisation
  (`systems/sympathetic_resonance.h:401-420`) plus a global `1/√count` scale, targeting **combined loop
  gain ≤ 1.0 evaluated at each resonator's own centre frequency on the control grid**. FR-058 states the
  formula and the cadence explicitly. If SC-016 clause 3's ≥ 6 dB emphasis proves unreachable, the fix is
  the send / normalisation constants — **never the criterion** (B-4).
- **Q8 — How is `getStateEnergy()` computed (FR-086)?** → **On-demand sweep inside the `const`
  accessor, accumulated in `double`.** The accessor is diagnostic-only and is never called from
  `process`. FR-086's "cached scalar, accumulated incrementally" wording is replaced accordingly.
  **AMENDED 2026-07-30:** the sweep covers the **state vector** — per channel the
  `m_i = ceil(effectiveDelay_[i])` most recent samples — not the whole power-of-two delay section. The
  binding definition and its derivation from FR-025 are `plan.md` §7.15; see the amended FR-086 row.

---

## Roadmap-vs-Reality Corrections

Eight items. All were verified by reading the named header this session.

| # | Roadmap says | Reality (verified) | Consequence for this spec |
|---|---|---|---|
| **C-1** | "reuse `fdn_reverb` topology knowledge; likely a new 8×8/16×16 network rather than extending the delay-plugin-flavoured one" (lines 265–266) | Confirmed, and stronger than "likely" — `FDNReverb` cannot be extended into this phase's requirements, for four independent reasons. (a) **Delay lengths are hard-clamped to 3–20 ms**: `prepare` computes `minDelay = 0.003·sr`, `maxDelay = 0.020·sr` and `std::clamp`s every scaled reference length into it (`effects/fdn_reverb.h:142-144`), so the longest line is 20 ms and "cathedral" is unreachable. (b) **There is no size control at all**: `setParamsInternal` maps `roomSize` to feedback gain (`:558`) and to `t60dc = 0.5 + roomSize·9.5` (`:576`), and `delayLengths_[i]` is never touched after `prepare` — so the roadmap's "Size — delay-line lengths" (line 268) has no counterpart. (c) **The channel count is a compile-time constant** `static constexpr size_t kNumChannels = 8` (`:118`) baked into every SoA array (`:774-780`) and into `applyHouseholder`'s hard-coded `sum * 0.25f` (`:754`), so 16×16 is not a parameter. (d) **The feedback matrix is fixed**: one Householder reflection, no morph (`:749-758`). | FR-010: a new FDN core in `aether_reverb.h`. `FDNReverb`'s *topology knowledge* is reused explicitly and cited per mechanism — Jot per-line absorption (`:570-600`), coprime-prime reference lengths (`:91`), Gordon-Smith quadrature LFO (`:346-352`), contiguous power-of-two delay sections (`:638-689`), freeze-bypasses-damping (`:296-322`) — but no line of it is `#include`d. |
| **C-2** | reuse-inventory row names `reverb` (line 91) | `reverb.h` contains the **Dattorro plate** `Reverb` (`effects/reverb.h:187`) plus `struct ReverbParams` (`:148-157`). `ReverbParams` has **nine** fields — `roomSize`, `damping`, `width`, `mix`, `preDelayMs`, `diffusion`, `freeze` (`:155`), `modRate`, `modDepth` (`:149-157`) — and **none** of `size`, `decaySeconds` or `dimensionality`, with `freeze` present only as a bool and no shimmer or bloom sends at all. Its `roomSize` is the same gain-not-geometry control C-1 describes. | `AetherReverb` defines its **own** control surface (FR-009) and does **not** reuse `ReverbParams`. `reverb.h` is not included. What *is* reused from it is the **Dattorro modulated-tank idea** as prior art for C-4's anti-metallic argument, and nothing else. Recorded so the omission is not read as an oversight. |
| **C-3** | "Householder ↔ Hadamard ↔ random-orthogonal matrix interpolation" (lines 271–272) | A naive interpolation `M(u) = (1−u)·A + u·B` of two orthogonal matrices is **not orthogonal** for `0 < u < 1`. (`u` is the **per-segment blend parameter**; the *global* morph position is `t`, and FR-020 puts the Hadamard endpoint at `t = 0.5`, so segment 1 is `u = 2t` and segment 2 is `u = 2t − 1`. The two coordinates were conflated in the previous revision and SC-004 clause 3's figures were stated in the wrong one.) The blend is non-expansive (`‖M‖₂ ≤ (1−u)‖A‖₂ + u‖B‖₂ = 1`), so it cannot blow up — but its singular values fall strictly **below** 1, which makes the loop **lossy in the middle of the morph**. **Computed this session** for `N = 8`, `A` = Householder `I − (2/8)J`, `B` = Sylvester Hadamard `H₈/√8` (Jacobi eigendecomposition of `MᵀM`): <br>`u=0.00 → σ ∈ [1.000, 1.000], ‖MᵀM−I‖_F = 0.000`<br>`u=0.25 → σ ∈ [0.500, 1.000], ‖MᵀM−I‖_F = 1.4842, worst −6.02 dB per pass`<br>`u=0.50 → σ ∈ [0.000, 1.000], ‖MᵀM−I‖_F = 1.9789, the matrix is SINGULAR`<br>`u=0.75 → σ ∈ [0.500, 1.000], ‖MᵀM−I‖_F = 1.4842`<br>`u=1.00 → σ ∈ [1.000, 1.000], ‖MᵀM−I‖_F = 0.000`<br>At the midpoint the blend **annihilates an entire subspace of the network state**; at the quarter points it removes 6 dB **per round trip**, which at a 30 ms loop is 2000 passes in 60 s. `setFreeze(true)` would decay to silence almost immediately and `setDecaySeconds(30)` would measure a few hundred ms — while both endpoints measure correctly, so a test that checks only `u ∈ {0, 1}` passes. **The singularity survives C-8's determinant correction**: re-computed this session for the corrected pair (`A` = Householder, `B′` = row-0-negated `H₈/√8`), `σ ∈ [0.0000, 1.0000]` and `‖MᵀM−I‖_F = 2.0000` at `u = 0.5` (`2.8284` at `N = 16`) — still exactly singular, for the reason C-8 gives. | FR-022 states orthogonality as an **unconditional invariant over the whole morph**, names the **one shipped mechanism** — the **real-Schur geodesic**, exactly orthogonal by construction (Q4) — and **strikes lerp + Newton–Schulz** (C-8). SC-004 measures `‖MᵀM − I‖_F` at 101 morph positions, not at the endpoints, and clause 3's negative control asserts the figures above **in global `t` coordinates** on **both** segments. |
| **C-4** | "Decay — RT60 from 0.5 s to **infinite** (freeze at unity feedback, energy-conserving)" (line 270) | Unity feedback is necessary and **not sufficient**, and `FDNReverb`'s freeze demonstrates the gap. Its freeze sets `fbGain = 1.0f` (`:561-564`) and bypasses damping (`:296-306`) and DC blockers (`:311-322`) — but it does **not** stop the LFO: `lfoEpsilon_` and `lfoMaxExcursion_` are untouched by freeze (`:610-631`), the phasor keeps advancing (`:346-352`), and any channel with a non-zero excursion is read with **cubic Hermite interpolation** (`:287-289`). A moving fractional-delay read is a time-varying lowpass: linear interpolation at `frac = 0.5` has magnitude `cos(ω/2)` — **−3.0 dB at 12 kHz** *per pass* — and cubic Hermite is better but still strictly lossy above DC. With `modDepth > 0`, `FDNReverb`'s "energy-conserving" freeze loses its entire top octave within a few seconds. | FR-033: in freeze, `AetherReverb` **latches the delay reads to integer offsets** — the fractional part of every line is ramped to zero over `kFreezeLatchMs` before the loop gain reaches unity, and size/LFO modulation of the delay lengths is held. Motion during freeze is provided instead by the **matrix morph**, which FR-022 makes exactly orthogonal and therefore exactly lossless. SC-002 measures broadband **and** per-octave level over 60 s, so a lossy-freeze implementation cannot pass on the broadband number alone. |
| **C-5** | "pitch-shifted feedback taps at +12 and +7 (reuse `PitchShiftProcessor`)" (lines 273–274) | Reusable, with three verified constraints the spec must absorb. (a) **It is mono**: `void process(const float* input, float* output, std::size_t numSamples) noexcept` (`processors/pitch_shift_processor.h:182`). Two taps × stereo would be **four** instances; `ShimmerDelay` already pays this, running one per channel (`effects/shimmer_delay.h:88-89`). (b) **It has large latency**: `getLatencySamples()` documents Simple = 0, Granular ≈ grain size ≈ 2048 samples, PhaseVocoder = `FFT_SIZE + HOP_SIZE` = 5120 samples (`:280-287`), with `getPhaseVocoderFFTSize() == 4096` and `getPhaseVocoderHopSize() == 1024` (`:366-374`). Inside a feedback loop that latency **is** loop time: **at 48 kHz** (the basis FR-054 uses throughout) 5120 samples is **107 ms**, so a PhaseVocoder tap cannot regenerate faster than that; at 44.1 kHz the same 5120 samples is 116 ms. FR-050's fixed 64-sample injection cadence adds a further 64 samples on top of the mode latency, so the **shipped** figure is 5184 samples ≈ 108 ms at 48 kHz (FR-054). The previous revision quoted the 107 ms figure against 44.1 kHz, mixing the two rates. (c) **Its sample-rate precondition is `[44100, 192000]`** (`:138-142`), which is what N-8 scopes the shimmer taps to — and **only** the shimmer taps. | FR-050: the shimmer taps run on a **mono sum of the four longest lines at `1/4`** — **two** `PitchShiftProcessor` instances total, not four — driven on a **fixed 64-sample cadence** and injected one chunk later into pinned, parity-spanning channel pairs (`{1,4}` for +12, `{3,6}` for +7 at `N = 8`) at `sqrt(2/|subset|)·send`, letting the network itself re-diffuse them to stereo. This halves the cost against `ShimmerDelay`'s shape and costs nothing perceptually inside a diffuse tail. FR-053 makes the mode a `prepare`-time choice defaulting to `PitchMode::Granular`, and FR-054 states the loop-time consequence of each mode **including the 64-sample cadence**. |
| **C-6** | (implicit) the Aether engine inherits `FDNReverb`'s 8 kHz–192 kHz range (`fdn_reverb.h:13`, `:130`) | **It does** — and the previous revision's engine-wide 44.1 kHz floor was an over-generalisation of a *shimmer-only* precondition, verified this session: `PitchShiftProcessor::prepare` requires `[44100, 192000]` (`processors/pitch_shift_processor.h:138-142`), but nothing else in the reuse set does. `DiffusionNetwork::prepare(float, size_t)`, `STFT::prepare`, `DelayLine::prepare`, `BreathingModulator::prepare`, `TidalModulator::prepare` and `BrownianDrift::prepare` all take a rate with no documented floor. Clamping the engine to 44.1 kHz when the host clocks 8 kHz does not make the shimmer legal — it makes **every** rate-derived quantity wrong by the rate ratio: Jot per-line gains (FR-030), the damping coefficient (FR-031), the DC-blocker `R` (FR-016), Size geometry in samples (FR-012), pre-delay length and every modulator rate. RT60, breath period and pre-delay would then be silently off by 5.5×. | N-8 / FR-003 / RA-6: `AetherReverb`'s supported range is **[8000, 192000] Hz**, stated in the header banner and clamped in `prepare`. Below **44 100 Hz** the **shimmer taps are force-disabled** (no `PitchShiftProcessor` is allocated, both sends are inert, `isShimmerActive()` reports `false`); everything else runs at the host's real rate. SC-009 measures 44.1 / 48 / 96 / 192 kHz and asserts the 8 kHz behaviour. |
| **C-7** | "harmonic bloom — a resonant emphasis stage that gradually reinforces partials of the held chord (sympathetic-resonance-style, reuse `sympathetic_resonance_simd` concepts)" (lines 274–276) | `SympatheticResonance` (`systems/sympathetic_resonance.h:96`) **cannot learn partials from audio**. Its pool is driven exclusively by `void noteOn(int32_t voiceId, const SympatheticPartialInfo& partials) noexcept` (`:179`) and `noteOff(int32_t)` (`:264`), where `SympatheticPartialInfo` is a fixed `std::array<float, kSympatheticPartialCount>` with `kSympatheticPartialCount = 4` (`:40`, `:71-74`). Its `process(float)` is **mono, one sample** (`:312`) and it applies its own anti-mud HPF at 100 Hz (`:55`, `:120-121`, `:355` — the previously-cited `:352` is a closing brace). So "reinforces partials of the held chord" requires either note information reaching a Layer 4 global effect, or peak detection the class does not have. **What is reusable is the kernel**: `processSympatheticBankSIMD(y1s, y2s, coeffs, rSquareds, gains, count, scaledInput, sums, releaseCoeff, envelopes)` (`systems/sympathetic_resonance_simd.h:39-50`) is a free function over plain arrays with no ownership of the pool, and the coefficient maths — `r = exp(−π·(f/Q)/sr)`, `coeff = 2r·cos(ω)` (`:426-438`) and the peak-gain normaliser (`:401-420`) — are `static` private helpers. | FR-055–FR-058 build the bloom bank in `aether_reverb.h` from **that kernel** (a Layer 3 free function, legally callable from Layer 4) with its own tuning source. **The tuning source is note-informed** (Q1, FR-056): `bloomNoteOn`/`bloomNoteOff` carry the partial list — the roadmap names the effect but never says where the list comes from, and it cannot be derived from any roadmap statement. The consequence, that a global Layer 4 effect now has a note API and Phase 7 must forward note events into it, is recorded as **RA-7**. |
| **C-8** | (implicit) the three named matrix characters can be connected by a continuous orthogonality-preserving morph (lines 271–272) | **Not as written — the two named endpoints lie in different connected components of `O(N)`, so no continuous path of orthogonal matrices connects them at all.** Computed exactly this session (Gaussian elimination with partial pivoting, `N = 8` and `N = 16`): the `t = 0` endpoint `I − (2/N)J` is the matrix `FDNReverb::applyHouseholder` applies (`effects/fdn_reverb.h:749-758`, `x[i] -= sum*0.25f` at `N = 8`) and is a **single** reflection — writing `u = 1/√N·𝟙`, `I − (2/N)J = I − 2uuᵀ = H(u)` — so `det = −1.000000`. The `t = 0.5` endpoint, Sylvester `H_N/√N` (`:696-729`), has `det = +1.000000` at both `N = 8` and `N = 16`. `det` is continuous on `O(N)` and takes only the values `±1`, so it cannot change along a continuous orthogonal path. Three consequences, each independently fatal to the previous revision: **(1)** FR-022's mechanism-1 wording ("a product of `N` unit reflection vectors") has `det = (−1)^N = +1` at `N ∈ {8, 16}` and therefore **cannot represent the `t = 0` endpoint at all**. **(2)** FR-021 left the third endpoint's determinant unconstrained — a Gram–Schmidt factor of `Xorshift32::nextFloat()` draws (`core/random.h:59`) has `det = ±1` by chance — so the second segment was a coin flip on the same failure. **(3)** SC-004 clause 1 samples `t = 0.00…1.00` in 101 steps, **including `t = 0.50` exactly**, so no conforming implementation could pass; and the only escape (a discontinuous jump between components) contradicts FR-020's "morphs … continuously", FR-023's smoothed + tide-modulated position and SC-015's 0-detection sweep. **Separately verified, and stronger than the component argument:** even after forcing both endpoints into the same component, `M(u) = (1−u)A + uB′` is **still exactly singular at `u = 0.5`** (`σ_min = 0.0000`, `‖MᵀM−I‖_F = 2.0000` at `N = 8`, `2.8284` at `N = 16`) — because `(1−u)A + uB` is singular at some `u ∈ (0,1)` exactly when `AᵀB` has eigenvalue `−1`, which this pair does. And Newton–Schulz polar re-orthonormalisation `M ← 1.5M − 0.5·M MᵀM` acts on singular values as `σ ← 1.5σ − 0.5σ³`, which has **`σ = 0` as a fixed point** — a zero singular value can never be lifted. | **All three endpoints are pinned into the `det = −1` component, normatively:** FR-020 negates row 0 of `H_N/√N` (still exactly orthogonal — row negation is left-multiplication by a `±1` diagonal — with `det = −1.000000`, verified); FR-021 negates one column of the Gram–Schmidt factor whenever `det(Q) > 0`. **FR-022's lerp + Newton–Schulz mechanism is struck** and replaced by the **real-Schur geodesic**, which is exactly orthogonal by construction and which *needs* the component fix: its per-segment relative rotation `R = A_segᵀB_seg` lies in `SO(N)` — and therefore has a real Schur form of pure `2×2` rotations — precisely because both endpoints carry `det = −1`. SC-004 gains a clause asserting `det(copyCurrentMatrix(…))` within `1e-5` of `−1` at all 101 positions, so the component invariant is *measured* and a future endpoint change cannot silently reintroduce the discontinuity. FR-001's header banner records the sign convention, since it changes the shipped matrices relative to `fdn_reverb.h`'s. |

---

## Recorded Roadmap Amendments

Consequences of decisions taken here that touch shipped code or downstream budgets. Recorded so a later
phase does not inherit a silent contradiction. None is licence to relax a Phase 6 threshold.

### RA-1 — Nothing outside this phase is modified

Unlike Phase 5 (which amended `RollingCaptureBuffer`) and Phases 3/4 (which amended `HarmonicCloud` and
`WaveguideString`), Phase 6 changes **no existing header**. Every mechanism it needs either exists with a
usable signature (the Existing-components table) or is reimplemented inside `aether_reverb.h` with the
reason recorded (C-1, C-3, C-7). This is a deliberate containment choice: `FDNReverb`, `Reverb`,
`ShimmerDelay` and `SympatheticResonance` are all shipped in Iterum or Ruinae, and the one thing this
phase would want from each of them — a size control, an orthogonal morph, a mono-sum tap shape, an
audio-driven tuning source — is a behaviour change, not an addition.

**Consequence for review:** there is no cross-consumer impact table in this spec because there is no
cross-consumer impact. `dsp_effects_tests`, `dsp_processors_tests` and `dsp_systems_tests` must all
still pass unedited (SC-013), which is the whole of the containment claim.

### RA-2 — Spectral diffusion adds fixed latency to the global wet path

When spectral diffusion is enabled at `prepare` (FR-060), the wet output is routed through
STFT ↔ OverlapAdd unconditionally, so it is delayed by `STFT::latency() == fftSize`
(`primitives/stft.h:183`) — **1024 samples = 21.3 ms at 48 kHz** at the default. This is reported by
`getLatencySamples()` (FR-084) and is **constant for a prepared configuration**; the amount knob never
changes it (the same discipline Phase 5 adopted for blur, `atmosphere_engine.h:1073`).

Because `AetherReverb` is **in-line** and not a parallel layer, this latency lands on the **dry path
too**: FR-062 routes the dry signal through a `prepare`-allocated `fftSize`-sample delay so dry and wet
stay aligned and the engine has **one** latency, not two. Phase 7/8 report that single number to the
host.

**The stage is a `prepare`-time flag defaulting to `true`** (`PrepareConfig::spectralDiffusionEnabled`,
Q2), so the shipped default configuration carries this 1024-sample latency instrument-wide, and a
zero-latency Seraphis configuration remains expressible at `prepare` — the escape hatch Phase 8/9 keeps.
A **runtime** toggle is excluded: FR-060 and FR-084 both require latency to be constant for a prepared
configuration, and a latency that changes mid-render is a click plus a host-latency renegotiation.

### RA-3 — Phase 6's 5 % global budget is real, and RA-4 of Phase 5 still does not close

Phase 5's RA-4 (`specs/seraphis-phase5-atmosphere/spec.md:237-292`) established that the roadmap's own
per-phase budgets do not sum to Phase 7's 25 % full-poly ceiling, and that Phase 7 must tally
**measurements**, not ceilings. Phase 6 contributes one **global** term, so it is the one budget in the
tally that does not multiply by voice count:

| Term | Figure | Source | × voices |
|---|---|---|---|
| Harmonic Cloud | 0.5 % / voice (roadmap claim) | roadmap line **164** (the figure is on 164, not 165; this is the line Phase 5's own RA-4 table cites — `specs/seraphis-phase5-atmosphere/spec.md:244`) | ×16 |
| Continuous Body | 1 % / voice (roadmap claim) | roadmap line **224** (the "CPU ≤ 1% per voice with body active" clause; 223 is the preceding line of the same sentence) | ×16 |
| Atmosphere | **measured** 1.048 % unfrozen / 1.440 % frozen | Phase 5 RA-4 amendment box | ×16 |
| **Aether (this phase)** | **5 % global** | roadmap line 282 | **×1** |

Phase 6 keeps the roadmap's 5 % as its gate (SC-008) and, exactly as Phase 5 did, requires the measured
ns/block figures for all five configurations to be transcribed verbatim into `compliance.md` so Phase 7
tallies numbers rather than ceilings. **No Phase 6 threshold may be relaxed on the grounds that the
aggregate is already over** — that contradiction is Phase 7's to resolve (roadmap Open Question 4).

### RA-4 — `setDecaySeconds` tops out at 60 s, not at "infinite − ε"

The roadmap writes "RT60 from 0.5 s to **infinite** (freeze at unity feedback)" (line 270), i.e. the top
of the continuous range and the freeze state are the *same* control in the prose. They cannot be:
a continuous RT60 control approaching infinity has unbounded sensitivity near the top (the loop gain
`g = 10^(−3·m/(T60·sr))` approaches 1 with derivative → ∞), so the top decade of the knob is
unusable and one denormal-flush or one rounded coefficient decides between 40 s and runaway.

Phase 6 therefore ships a **continuous range of 0.5 s … 60 s** plus a **discrete `setFreeze(bool)`**
that takes the loop to exactly unity with damping bypassed and delays latched (FR-030, FR-033). This is
the same split `FDNReverb` uses (`ReverbParams::freeze` handled at `fdn_reverb.h:561-564`), and it is
what makes SC-002's ±0.5 dB/60 s criterion a statement about an *exactly* lossless configuration rather
than about an arbitrarily-chosen large finite one. Recorded as an amendment because "0.5 s to infinite"
is roadmap wording that this spec does not literally implement.

### RA-5 — Freeze is an exactly-lossless hold, not a playable state

The roadmap makes freeze "a first-class playing technique" (line 74). In this engine it is first-class in
the sense of *enterable at any time, repeatedly, click-free* (FR-035) — **but it is inert in every
dimension except the matrix morph**, and that is a consequence a later phase must not discover by ear:

- **FR-033 step 5 mutes all three sends** — the +12 shimmer, the +7 shimmer *and* the harmonic bloom —
  for the duration of the freeze. So the roadmap's "Shimmer bloom (inside the feedback loop, not a post
  layer)" (lines 273–276) is **unavailable in precisely the state a player reaches for it**.
- **FR-034 latches the geometry** — `setSize`, `setDecaySeconds` and `setDamping` are stored but not
  applied while frozen.

Neither is a design preference. Both are **forced by roadmap line 280's own ±0.5 dB conservation
criterion**: a pitch-shifted return and a resonant emphasis bank inside a unity-gain loop are energy
*sources* (neither is a per-line gain nor a damping filter, so neither is covered by FR-025's accounting),
and a size change under freeze is the one operation that cannot be lossless (C-4). Either one left live
makes SC-002 unachievable and freeze unbounded.

**Consequence for later phases.** Phase 7's **Dream** macro raises reverb send (roadmap line 303) and
Phase 12 authors a preset library against this engine; both will otherwise assume shimmer survives
freeze. It does not. A shimmer-in-freeze *character*, if wanted, must be a **parallel** layer owned by
Phase 10's global spectral freeze — "spectral freeze (global capture-and-hold of the Aether tail)",
roadmap line **446** — not this engine's `setFreeze`, which holds the network state exactly.

### RA-6 — Below 44.1 kHz the shimmer taps are inert, and the sends do nothing

FR-003 accepts `[8000, 192000] Hz` (C-6) and **force-disables the shimmer taps below 44 100 Hz**, because
`PitchShiftProcessor::prepare`'s precondition is `[44100, 192000]`
(`processors/pitch_shift_processor.h:138-142`). Everything else — FDN geometry, Jot gains, damping, DC
blockers, diffusion, bloom, spectral diffusion, all three life modulators — runs at the host's real rate.

**Consequence for later phases.** Phase 9 registers `shimmerOctaveSend` and `shimmerFifthSend` in the
1200–1399 aether range (roadmap lines 380–381). At a host rate below 44.1 kHz those two parameters are
**registered, automatable and inert**. Phase 8/9 must therefore either (a) report the condition through
the plugin's own state/UI, or (b) accept it as a documented limitation — but must not discover it during
pluginval at strictness 5 (roadmap lines 428–431). `isShimmerActive()` (FR-086) is the accessor that
makes the condition queryable without inspecting the sample rate. The previous revision instead clamped
the *engine* to 44.1 kHz, which left every rate-derived coefficient wrong by the rate ratio while
reporting success — a silent failure this amendment removes.

### RA-7 — Phase 7 must forward note events into a global Layer 4 effect

FR-056 resolves the bloom's tuning source to the **note-informed** API `bloomNoteOn(voiceId, partialHz,
count)` / `bloomNoteOff(voiceId)` (Q1). That is the roadmap's literal wording — "reinforces partials of
the held **chord**" (line 275) — and it is the cheaper, more stable of the two candidate mechanisms
(C-7). But the roadmap describes `AetherReverb` as **global, post-voice-sum, one instance** (line 262),
and nowhere says that note events reach it.

**Consequence for Phase 7.** The voice manager (roadmap lines 286–300) must forward note-on/note-off —
with each voice's low-order partial frequencies — to the single global `AetherReverb` instance, in
addition to routing them to the voices. This is a new Phase 7 obligation, not a Phase 6 one: Phase 6
ships the API, allocation-free and audio-thread-callable, and unit-tests it directly (SC-016 clause 3).
Until a caller drives it, `getActiveBloomResonatorCount()` is 0 and the bloom send is inert — an
implementation that never wires the forwarding produces a silently dead bloom, which is why SC-016
clause 3 asserts a non-zero active count throughout.

**What is *not* implied.** The bloom is **not** coupled to `spectralDiffusionEnabled` (Q1, Q2): it needs
no spectral analysis, so it runs identically with the spectral stage off (FR-065). And the note API does
**not** make `AetherReverb` per-voice or polyphonic — `voiceId` is an opaque retire key, exactly as in
`SympatheticResonance::noteOff` (`systems/sympathetic_resonance.h:264`).

---

## Existing components (reused — verified signatures)

Every signature below was read this session at the cited line.

| Component | Header (layer) | Verified signature / fact | What Phase 6 reuses |
|---|---|---|---|
| `FDNReverb` | `effects/fdn_reverb.h:112` (L4) | `static constexpr size_t kNumChannels = 8` (`:118`), `kNumDiffuserSteps = 4` (`:120`), `kSubBlockSize = 16` (`:121`); `void prepare(double sampleRate) noexcept` (`:132`); `void setParams(const ReverbParams&) noexcept` (`:251`); `void process(float& left, float& right) noexcept` (`:260`); `void processBlock(float*, float*, size_t) noexcept` (`:385`); prime reference delays `{149,193,241,307,389,491,631,797}` (`:91`); 3 ms/20 ms clamp (`:142-144`); Jot per-line absorption `gDC = 10^(−3·mᵢ/(T60·sr))` and one-pole coefficient from the DC/Nyquist gain ratio (`:576-600`); `applyHouseholder` = `x[i] −= 0.25·Σx` (`:749-758`); `applyHadamard` 3-stage FWHT + `1/√8` (`:696-729`); contiguous power-of-two delay sections with per-section mask/offset (`:638-689`); freeze bypasses damping and DC blockers (`:296-322`) | **Topology knowledge only — not `#include`d** (C-1). Each mechanism above is re-derived in `aether_reverb.h` with the citation in a comment: FR-010 (delay layout), FR-011 (prime lengths), FR-020 (Householder/Hadamard endpoints), FR-030 (Jot absorption), FR-033 (freeze bypass) |
| `ReverbParams` | `effects/reverb.h:148-158` (L4) | 9 fields: `roomSize`, `damping`, `width`, `mix`, `preDelayMs`, `diffusion`, **`freeze`** (`bool`, `:155`), `modRate`, `modDepth` — matching C-2 | **Deliberately NOT reused** (C-2). `AetherReverb` defines its own control table (FR-009); `reverb.h` is not included |
| `DiffusionNetwork` | `processors/diffusion_network.h:206` (L2) | `void prepare(float sampleRate, size_t maxBlockSize) noexcept` — `maxBlockSize` is explicitly unused (`:242-243`); `void reset() noexcept` (`:294`); `void setSize(float sizePercent)` [0,100] (`:318`); `void setDensity(float densityPercent)` [0,100] → per-stage crossfaded enables via `updateDensityTargets()` (`:325-329`, `:615`); `void setWidth(float)` (`:333`); `void setModDepth(float)` (`:340`); `void setModRate(float rateHz)` [0.1, 5] (`:371`); `void snapSmoothers() noexcept` (`:361`) — documented specifically for callers that already smooth on their own control grid (`:347-360`); `void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples) noexcept` (`:396-398`) with a settled-parameter fast path (`:402-405`, `:534`, `:550`); `kNumDiffusionStages = 8` (`:36`), `kAllpassCoeff = 0.618033988749895f` (`:39`), `kBaseDelayMs = 3.2f` (`:42`), `kMaxModDepthMs = 2.0f` | **Verbatim**, as the **Density** control (FR-040). Note its maximum delay is `kBaseDelayMs · 4.123 · 1.127 + 2 ms ≈ 16.9 ms` (`:248-250`) — it is an early-diffusion smearer, **not** a size control; Size is FR-012's job. FR-041 calls `snapSmoothers()` after every control-grid push, per the header's own advice |
| `PitchShiftProcessor` | `processors/pitch_shift_processor.h:100` (L2) | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` with documented preconditions `sampleRate ∈ [44100,192000]`, `maxBlockSize ∈ [1,8192]` (`:139-144`); `void reset() noexcept` (`:153`); **mono** `void process(const float* input, float* output, std::size_t numSamples) noexcept`, in-place safe (`:170-182`); `void setMode(PitchMode)` (`:194`); `void setSemitones(float)` (`:213`); `void setCents(float)` (`:228`); `[[nodiscard]] std::size_t getLatencySamples() const noexcept` (`:287`) documented as Simple 0 / Granular ≈2048 / PhaseVocoder = FFT+hop ≈5120 @44.1 kHz (`:280-287`); `enum class PitchMode : std::uint8_t { Simple, Granular, PhaseVocoder, PitchSync }` (`:58-63`); `getPhaseVocoderFFTSize() == 4096`, `getPhaseVocoderHopSize() == 1024` (`:366-374`); non-copyable, movable (`:122-126`) | The two shimmer taps (FR-050). **Two instances, not four** — C-5 |
| `ShimmerDelay` / `ShimmerFeedbackProcessor` | `effects/shimmer_delay.h:219` / `:62` (L4) | `ShimmerFeedbackProcessor::process` runs `pitchShifterL_.process(left,left,n)` and `pitchShifterR_.process(right,right,n)` then blends diffusion by amount (`:84-105`); `getLatencySamples()` returns the pitch shifter's (`:113-115`); `ShimmerDelay::kMaxFeedback = 1.2f` "for self-oscillation" (`:246`); the header records that shimmer mix is routed by `FlexibleFeedbackNetwork::setProcessorRouteMix()` "to avoid comb filtering from latency mismatch" (`:57-60`, `:103-105`) | **Concepts only** (roadmap line 91 says "(concepts)"). Two are load-bearing and are cited in the FRs: pitch-shift-inside-the-loop is viable at all (FR-050), and **a pitch-shifted return must be level-blended, never summed against an unshifted copy of itself at a different latency** (FR-052). Not `#include`d — `ShimmerDelay` drags in `FlexibleFeedbackNetwork`, `DelayEngine`'s `TimeMode` and `ModulationMatrix` (`:28-37`), none of which this phase wants |
| `STFT` | `primitives/stft.h:35` (L1) | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` (`:58-63`), documented NOT real-time safe (`:57`); `void pushSamples(const float*, size_t) noexcept` (`:104`); `[[nodiscard]] bool canAnalyze() const noexcept` (`:134`); `void analyze(SpectralBuffer&) noexcept` (`:144`); `[[nodiscard]] size_t latency() const noexcept` → `fftSize_` (`:183`) | Spectral-diffusion analysis (FR-060) |
| `OverlapAdd` | `primitives/stft.h:204` (L1) | `void prepare(size_t fftSize, size_t hopSize, WindowType, float kaiserBeta, bool applySynthesisWindow) noexcept` (`:229-236`), with the doc note that synthesis windowing is "Required for spectral modification processors … at >=75% overlap where Hann² satisfies COLA. Must NOT be used at 50% overlap" (`:225-228`); `void synthesize(const SpectralBuffer&) noexcept` (`:289`); `[[nodiscard]] size_t samplesAvailable() const noexcept` (`:319`); `void pullSamples(float*, size_t) noexcept` (`:328`) | Spectral-diffusion synthesis at **75 % overlap with `applySynthesisWindow = true`** (FR-061) — the header's own constraint, not a choice |
| `SpectralBuffer` | `primitives/spectral_buffer.h:45` (L1) | `void prepare(size_t fftSize) noexcept` (`:61`); `[[nodiscard]] float getMagnitude(size_t) const noexcept` (`:84`); `[[nodiscard]] float getPhase(size_t) const noexcept` (`:91`); `void setMagnitude(size_t,float) noexcept` (`:98`); `void setPhase(size_t,float) noexcept` (`:106`); `[[nodiscard]] size_t numBins() const noexcept` (`:166`) | Per-bin phase smearing (FR-061) |
| `processSympatheticBankSIMD` | `systems/sympathetic_resonance_simd.h:39-50` (L3, free function) | `void processSympatheticBankSIMD(float* y1s, float* y2s, const float* coeffs, const float* rSquareds, const float* gains, int count, float scaledInput, float* sums, float releaseCoeff, float* envelopes) noexcept` — documented as `y[n] = coeff·y[n−1] − r²·y[n−2] + scaledInput·gain`, plus peak-detect envelope follower and accumulate; explicitly "no cross-resonator feedback" so it vectorises across resonators (`:8-10`) | **The kernel** for harmonic bloom (FR-055). Plain arrays, no ownership — callable from Layer 4 |
| `SympatheticResonance` | `systems/sympathetic_resonance.h:96` (L3) | `void prepare(double sampleRate) noexcept` (`:113`); `void noteOn(int32_t voiceId, const SympatheticPartialInfo&) noexcept` (`:179`); `void noteOff(int32_t) noexcept` (`:264`); **mono per-sample** `[[nodiscard]] float process(float input) noexcept` (`:312`); `static ResonatorCoeffs computeResonatorCoeffs(float f, float Q_eff, float sampleRate)` — `r = exp(−π·(f/Q)/sr)`, `coeff = 2r·cos(ω)`, `rSquared = r²` (`:426-438`); `static float computeResonatorPeakGainInverse(const ResonatorCoeffs&, float freq)` (`:401-420`); `static float computeFreqDependentQ(float Q_user, float f)` (`:440-446`); `kSympatheticPartialCount = 4` (`:40`), `kMaxSympatheticResonators = 64` (`:43`), anti-mud HPF at 100 Hz (`:55`, `:120-121`) | **The coefficient maths, re-derived** (FR-057) — the class itself is note-driven and cannot be fed by a global effect (C-7). Its `computeResonatorPeakGainInverse` normaliser is the mechanism FR-058's stability guard is built on |
| `BreathingModulator` | `processors/breathing_modulator.h:105` (L2) | `class BreathingModulator : public ModulationSource`; `kMinRate = 0.01f`, `kMaxRate = 0.5f` (`:108-110`); `void prepare(double) noexcept` (`:144`); `void reset() noexcept` (`:152`); `void setSeed(std::uint32_t) noexcept` (`:164`); `void setRate(float hz) noexcept` (`:170`); `void setDepth(float) noexcept` (`:177`); `void setIrregularity(float) noexcept` (`:184`); `void processBlock(size_t numSamples) noexcept` (`:209`); `[[nodiscard]] float getCurrentValue() const noexcept override` (`:222`) | **Verbatim, owned as a member** — Size breathing (FR-070). Roadmap line 279: "size and matrix slowly breathe via Phase 1 modulators" |
| `TidalModulator` | `processors/tidal_modulator.h:122` (L2) | `class TidalModulator : public ModulationSource`; `kMinPeriod = 30.0f`, `kMaxPeriod = 600.0f` seconds (`:125-127`); `kNumLayers = 3` incommensurate layers (`:132`, `:149`); `void prepare(double) noexcept` (`:169`); `void setSeed(std::uint32_t)` (`:193`); `void setRate(float normalized)` (`:202`); `void setDepth(float)` (`:209`); `[[nodiscard]] float getBasePeriodSeconds() const noexcept` (`:217`); `void processBlock(std::size_t) noexcept` (`:250`); `getCurrentValue()` (`:263`) | **Verbatim, owned as a member** — Dimensionality drift (FR-071) |
| `BrownianDrift` | `processors/brownian_drift.h:94` (L2) | `class BrownianDrift : public ModulationSource`; `kControlRateInterval = 32` (`:105`); `void prepare(double)` (`:121`); `void reset()` re-seeds (`:133`); `void setSeed(std::uint32_t)` (`:145`); `void setSmoothness(float)` (`:152`); `void setDepth(float)` (`:159`); `void setMean(float)` (`:165`); `void process()` (`:178`); `void processBlock(size_t) noexcept` carries `samplesUntilControl_` across calls (`:194`); `getCurrentValue()` clamped to [−1,+1] (`:212`); `getSourceRange()` (`:217`). **That list is the complete public API — there is no rate setter.** The only time-scale control is `setSmoothness`, which maps to `tau = lerp(kTauMin, kTauMax, smoothness)` with `kTauMin = 0.2f`, `kTauMax = 30.0f` seconds (`:35-36`, `:97-99`, `:231-234`) | **Verbatim, owned as a member** — per-channel delay-length jitter that replaces `FDNReverb`'s quadrature LFO (FR-072). FR-009 therefore exposes `setModSmoothness`, **not** a Hz-denominated `setModRate`: a Hz control has no counterpart on this class, and the previously-advertised 2 Hz top end is unreachable in any mapping (`tau ≈ 1/(2π·2) ≈ 0.08 s` < `kTauMin = 0.2 s`) |
| `ModulationSource` | `core/modulation_source.h:31` (L0) | pure virtuals `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0` (`:37`) and `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept = 0` (`:41`) | Satisfied by the three modulators above. `AetherReverb` is a **sink**, not a source, and does **not** implement it |
| `OnePoleSmoother` / `LinearRamp` | `primitives/smoother.h:134` / `:305` (L1) | `configure(float ms, float sampleRate)` computes a **per-`process()`-call** coefficient (`:160`/`:329`); `advanceSamples(size_t)` is the closed-form N-sample advance (`:243`); `snapToTarget()` (`:257`/`:414`); `snapTo(float)` (`:263`/`:421`); `getCurrentValue()` (`:191`/`:364`) | Every smoothed control (FR-009). **Cadence is part of the spec**: a smoother read once per STFT frame is advanced by `advanceSamples(hopSize)`, never by one `process()` |
| `DelayLine` | `primitives/delay_line.h:57` (L1) | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` — buffer rounded to `nextPowerOf2(maxDelaySamples_ + 1)` (`:267-277`); `void write(float) noexcept` (`:287`); `[[nodiscard]] float read(size_t) const noexcept` (`:292`); `readLinear(float)` (`:302`); `readCubic(float)` (`:321`); `readAllpass(float)` (`:347`) — first-order allpass `y = x0 + a·(state − x1)`, `a = (1−frac)/(1+frac)`, documented "Use ONLY for fixed delays in feedback loops … Do NOT use for modulated delays" (`:142-145`); `struct AllpassTap {index0, index1, coeff}` (`:157-162`) and `struct LinearTap` (`:165-171`) with `makeAllpassTap`/`makeLinearTap` (`:184`, `:174`); `inline constexpr size_t nextPowerOf2(size_t)` (`:26`) | Pre-delay, the dry-path alignment delay (FR-062) and the freeze-leg latch arithmetic. **The FDN core does not use `DelayLine` objects** — FR-010 uses `FDNReverb`'s contiguous power-of-two section layout (`fdn_reverb.h:638-689`) instead, for one shared allocation and mask-indexed reads across `N` channels |
| `detail::isNaN` / `detail::isInf` | `core/db_utils.h:54` / `:175` (L0) | `constexpr bool isNaN(float) noexcept` and `[[nodiscard]] constexpr bool isInf(float) noexcept`, both IEEE-754 exponent-field tests on a `std::bit_cast<std::uint32_t>` | **The** finiteness test (FR-008, FR-085). No fourth reimplementation |
| `Xorshift32` / `deriveStreamSeed` | `core/random.h:41` / `:102` (L0) | `[[nodiscard]] constexpr float nextFloat() noexcept` → [−1,1] (`:59`); `nextUnipolar()` (`:67`); `constexpr void seed(uint32_t) noexcept` (`:73`); `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102`) | The random-orthogonal matrix endpoint (FR-021), per-channel jitter seeds and every modulator seed (FR-073) |
| control-chunk cadence | `systems/continuous_body.h:97`, `systems/harmonic_cloud.h:144`, `systems/atmosphere_engine.h:269` | `static constexpr std::size_t kControlChunkSamples = 64` — the identical value in all three | FR-005 copies the **value** 64 for consistency across the Seraphis phases; no header dependency |
| stream contract | `systems/continuous_body.h:1161-1163`, `systems/atmosphere_engine.h:665-666` | `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, std::size_t numSamples) noexcept` | FR-004 adopts the identical shape so Phase 7 chains all four Seraphis blocks without adapters |

**Read but deliberately NOT used** (recorded so an omission is not read as an oversight):
`Reverb` (`effects/reverb.h:187`) — C-2; `FlexibleFeedbackNetwork` / `IFeedbackProcessor` — they exist to
inject a processor into `ShimmerDelay`'s *single* delay loop, whereas FR-050 injects into an `N`-channel
matrix, and adopting the interface would force the whole `ShimmerDelay` include set (C-5);
`SpectralFreezeOscillator` (`processors/spectral_freeze_oscillator.h:81`) — N-4, that is Phase 10's
capture-and-hold, not this phase's unity-feedback freeze; `IResonator`
(`processors/iresonator.h:32`) — N-9; `FeedbackNetwork` / `FilterFeedbackMatrix` — Phase 4's row, not
Phase 6's, and neither offers a morphable orthogonal matrix.

---

## New components

ODR sweep run this session, verbatim command:
`grep -rn "class <Name>\b\|struct <Name>\b" dsp/ plugins/ tools/`

| Class | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `AetherReverb` | **4** | `dsp/include/krate/dsp/effects/aether_reverb.h` (new file — verified absent: not in `ls dsp/include/krate/dsp/effects/`) | **CLEAR** — 0 hits for `AetherReverb`, and 0 hits for `Aether` of any kind |
| `AetherReverb::PrepareConfig` (nested) | 4 (nested) | same header | **1 hit at nested scope only** — `AtmosphereEngine::PrepareConfig` (`systems/atmosphere_engine.h:367`). Both are class-nested, so this is **not** an ODR violation and the name is intentionally the same: Phase 5 established the pattern and Phase 7 will construct both. Recorded rather than renamed |
| `AetherReverb::MatrixMorph` (nested) | 4 (nested) | same header | **CLEAR** — 0 hits for `MatrixMorph` anywhere. Also swept and clear: `MatrixMorpher`, `OrthogonalMorph`, `DimensionalityMorph`, `FeedbackMatrix`, `ReverbMatrix`, `HadamardMatrix`, `OrthogonalMatrix` (all 0). **`HouseholderMatrix` is NOT clear** — `AllpassSaturator::HouseholderMatrix` exists at `processors/allpass_saturator.h:305`; it is class-nested so there would be no ODR violation, but the name is not reused here |
| `AetherReverb::BloomBank` (nested) | 4 (nested) | same header | **CLEAR** — 0 hits for `BloomBank`, `BloomResonators`, `HarmonicBloom`, `ShimmerBloom`, `BloomStage` (all 0) |
| `AetherReverb::ShimmerTap` (nested) | 4 (nested) | same header | **CLEAR** — 0 hits for `ShimmerTap` anywhere |
| `AetherReverb::ChannelState` (nested) | 4 (nested) | same header | **CLEAR** — 0 hits for `FdnChannelState`; `ChannelState` is a generic name and is therefore kept **nested and private**, never at namespace scope |

Names swept and rejected (recorded so the plan does not re-litigate): `AetherEngine`, `SpaceEngine`,
`AetherParams`, `AetherConfig`, `FdnCore`, `FDNCore`, `SpectralDiffusion`, `SpectralDiffuser`,
`ReverbTail` — **all 0 hits**, i.e. all available, but none is needed. The roadmap authorises exactly one
new component (line 264) and this spec ships exactly one **class**; everything else is a private nested
aggregate of it, the shape Phases 2, 3 and 5 all shipped.

**No existing header is amended** (RA-1). No new free function at namespace scope is introduced —
`lint-odr.js` and `lint-layers.js` gate both (SC-013).

---

## Functional Requirements

### A. Component identity, lifecycle, RT safety

- **FR-001** — The component is `class AetherReverb`, declared in
  `dsp/include/krate/dsp/effects/aether_reverb.h`, in `namespace Krate::DSP`, at **Layer 4**. Its header
  banner states the layer, the spec slug, the roadmap lines it implements, and — per C-1 — the specific
  `fdn_reverb.h` line ranges whose topology it re-derives. It **also records the matrix sign convention**
  (FR-020: row 0 of `H_N/√N` negated, and the random-orthogonal endpoint forced to `det = −1`), because
  the shipped matrices differ from `fdn_reverb.h:696-729`'s by that sign and the reason — the `O(N)`
  component invariant of C-8 — is not inferable from the code.
- **FR-002** — Includes point **downward only**: Layer 0 (`db_utils.h`, `math_constants.h`, `random.h`,
  `pitch_utils.h`, `window_functions.h`, **`interpolation.h`** — required by FR-014, which reads moving
  delay offsets with `Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`, namespace
  opened at `:18`); the previous revision omitted it from this closed list while FR-014 mandated the
  read, which would have forced a hand-rolled fourth copy of an existing Layer 0 primitive — exactly
  what FR-008 forbids for the finiteness helpers), Layer 1 (`delay_line.h`, `smoother.h`, `stft.h`,
  `spectral_buffer.h`), Layer 2 (`diffusion_network.h`, `pitch_shift_processor.h`,
  `breathing_modulator.h`, `tidal_modulator.h`, `brownian_drift.h`), Layer 3
  (`sympathetic_resonance_simd.h` — the free-function kernel only). **No other Layer 4 include**: neither
  `fdn_reverb.h` (C-1), `reverb.h` (C-2) nor `shimmer_delay.h` (C-5) is included. Layer discipline is
  gated automatically by `node tools/lint-layers.js` (SC-013), not by inspection.
- **FR-003** — `void prepare(double sampleRate, const PrepareConfig& config) noexcept` performs **all**
  allocation: the contiguous FDN delay buffer, the diffusion network, the pre-delay, the two shimmer
  pitch shifters and their scratch buffers (only when `config.shimmerEnabled`; one
  `kControlChunkSamples`-sample input and one output scratch per tap, which is all FR-050's fixed
  64-sample cadence needs), the bloom bank arrays sized for `kMaxBloomResonators = 32`
  (only when `config.bloomEnabled`), the STFT/OverlapAdd/SpectralBuffer set and the dry alignment delay
  (only when `config.spectralDiffusionEnabled`), the matrix-morph working matrices, and every smoother.
  `sampleRate` is clamped to **[8000.0, 192000.0]** — `FDNReverb`'s own range (`effects/fdn_reverb.h:13`,
  `:130`), **not** a 44.1 kHz floor (N-8, C-6, RA-6). **If the clamped rate is below 44 100 Hz the
  shimmer taps are force-disabled**, exactly as if `config.shimmerEnabled == false`: no
  `PitchShiftProcessor` and no tap scratch buffer is allocated, both shimmer sends are inert regardless
  of their control values, and `isShimmerActive()` (FR-086) returns `false`. Every other stage — FDN
  geometry, Jot gains, damping, DC blockers, diffuser, bloom bank, spectral diffusion, all three life
  modulators — is computed at the **real** rate, so RT60, breath period, pre-delay and modal density
  remain correct. Calling `prepare` twice is legal and fully reconfigures. `prepare` is the only
  non-RT-safe method.
- **FR-004** — `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
  float* outRight, std::size_t numSamples) noexcept` — shape identical to
  `ContinuousBody::processStereoBlock` (`systems/continuous_body.h:1161-1163`) and
  `AtmosphereEngine::processStereoBlock` (`systems/atmosphere_engine.h:665-666`). A null pointer in any of
  the four positions writes **nothing** and returns; `numSamples == 0` is a no-op. Input and output may
  alias only if `inLeft == outLeft && inRight == outRight` (documented in-place contract); partial
  aliasing is a precondition violation. A call longer than `config.maxBlockSamples` is processed in
  internal slices of that size, so it is never a precondition violation.
- **FR-005** — Control-rate work (matrix re-orthonormalisation, life-modulator advance, Size/Decay/
  Dimensionality smoother reads, diffusion-network parameter pushes, bloom retuning) runs on an
  **absolute 64-sample grid** (`kControlChunkSamples = 64`, the value used by `ContinuousBody` at `:97`,
  `HarmonicCloud` at `:144` and `AtmosphereEngine` at `:269`). A chunk that straddles a
  `processStereoBlock` boundary carries over: the grid is anchored to the engine's total-sample counter,
  not to block starts, so a caller splitting a block into odd partitions gets identical output (SC-011).
- **FR-006** — `void reset() noexcept` clears **audio, modulator and seed state**, and **preserves every
  control target**. Precisely:
  - **Cleared:** all delay buffers, the diffusion network, the pitch shifters, bloom resonator state,
    STFT and OverlapAdd accumulators, the pre-delay and dry-alignment delays, the sample counter, and the
    freeze latch (`isFrozen()` becomes `false` — freeze is a control *state*, but it is also an audio
    latch, and a cleared network has nothing to hold).
  - **Re-seeded:** **every life-modulator instance** — one `BreathingModulator` (FR-070), one
    `TidalModulator` (FR-071), and one `BrownianDrift` per modulated channel, i.e. `N/2` instances
    (FR-072) — so the total is **6 at `N = 8` and 10 at `N = 16`**, not three. Each class re-seeds inside
    its own `reset()` (`brownian_drift.h:133`), but this FR additionally requires the *derived* seeds
    (FR-073) to be re-applied, so a post-`reset` render matches the original. The spectral-smear stream
    (FR-061) is re-seeded on the same rule.
  - **Preserved:** every value set through the FR-009 control table. A `reset()` after
    `setDimensionality(0.9f)` leaves the target at 0.9 and **snaps** the smoother to it — it does not
    revert to the 0.35 default. Reverting to defaults is what a second `prepare` is for. The morph
    position is therefore restored to *the current Dimensionality target*, not to the configured default.

  It allocates nothing. SC-010's reset clause states the exact call sequence this implies.
- **FR-007** — `void silence() noexcept` ramps the output to zero over `kSilenceRampMs = 20.0f`, then
  zeroes every delay line and every resonator state. Unlike Phase 5's, it does **not** latch: the engine
  resumes normally on the next non-silent input, because a global reverb that latched would require the
  host to reset it after every stop. This divergence from `AtmosphereEngine::silence()`
  (`systems/atmosphere_engine.h:636`, which latches) is deliberate and is stated in the header so the two
  are not assumed identical.
- **FR-008** — **RT safety.** Every method other than `prepare` is `noexcept`, allocation-free,
  lock-free, exception-free and I/O-free. No `std::isnan` / `std::isinf` /
  `std::numeric_limits::infinity()` anywhere in the header (the macOS leg builds with `-ffast-math`).
  Finiteness checks call the **existing** Layer 0 helpers `Krate::DSP::detail::isNaN`
  (`core/db_utils.h:54`) and `detail::isInf` (`:175`). No new bit test is written.
- **FR-009** — **Control table (pinned here; Phases 7 and 9 consume it verbatim).** Every setter clamps
  its argument to the stated range; out-of-range and non-finite arguments never propagate.

  | Setter | Range | Default | Smoothing | Applied |
  |---|---|---|---|---|
  | `setSize(float)` | 0 … 1 | 0.5 | 300 ms `OnePoleSmoother` | control grid → delay-length scale, FR-012 |
  | `setDensity(float)` | 0 … 1 | 0.7 | **100 ms `OnePoleSmoother`, Aether-side**, advanced by `advanceSamples(kControlChunkSamples)` | smoothed value read once per control chunk → `DiffusionNetwork::setDensity(v·100)` → `snapSmoothers()` |
  | `setDecaySeconds(float)` | 0.5 … 60.0 | 4.0 | 200 ms | control grid → Jot per-line gains, FR-030 |
  | `setFreeze(bool)` | — | `false` | `kFreezeLatchMs = 50` ramp | FR-033 |
  | `setDimensionality(float)` | 0 … 1 | 0.35 | 200 ms | control grid → matrix morph position, FR-020 |
  | `setDamping(float)` | 0 … 1 | 0.4 | 200 ms | control grid → per-line one-pole HF absorption, FR-031 |
  | `setPreDelayMs(float)` | 0 … 200 | 0.0 | 50 ms | per sample |
  | `setModDepth(float)` | 0 … 1 | 0.25 | 100 ms | per-channel delay jitter depth, FR-072 |
  | `setModSmoothness(float)` | 0 … 1 | 0.6 | none | forwarded verbatim to every channel's `BrownianDrift::setSmoothness` (`processors/brownian_drift.h:152`), FR-072 |
  | `setShimmerOctaveSend(float)` | 0 … 1 | 0.0 | 100 ms | FR-051 |
  | `setShimmerFifthSend(float)` | 0 … 1 | 0.0 | 100 ms | FR-051 |
  | `setBloomSend(float)` | 0 … 1 | 0.0 | 100 ms | FR-055 |
  | `setBloomDecay(float)` | 0 … 1 → Q 20 … 400 | 0.5 | 200 ms | FR-057, control grid |
  | `setSpectralDiffusion(float)` | 0 … 1 | 0.0 | 100 ms, advanced by `advanceSamples(hopSize)` | per STFT frame, FR-061 |
  | `setSizeBreathDepth(float)` | 0 … 1 | 0.2 | none | FR-070 |
  | `setDimensionalityTideDepth(float)` | 0 … 1 | 0.2 | none | FR-071 |
  | `setWidth(float)` | 0 … 1 | 1.0 | 50 ms | per sample, FR-080 |
  | `setMix(float)` | 0 … 1 | 0.35 | 50 ms | per sample, equal-power, FR-081 |
  | `setSeed(std::uint32_t)` | any | 1 | none | stores the seed and re-seeds the **audio-thread-safe** streams — smear, per-channel `BrownianDrift`, breathing, tidal. **Not** the random-orthogonal matrix endpoint, which is `prepare`-time only (FR-021, FR-073) |

  **Smoother-initialisation rule (binding — SC-010 clause 3 is unsatisfiable without it).** A setter
  called while `isPrepared()` and **before any samples have been processed** since the last `prepare` or
  `reset` **snaps** its smoother to the new target instead of ramping to it (`OnePoleSmoother::snapTo`,
  `primitives/smoother.h:263`). Rationale, and why this is a requirement rather than an optimisation:
  FR-006 makes `reset()` preserve control targets and *snap* every smoother to them, so without this rule
  the render *before* a `reset()` starts with its smoothers at the FR-009 defaults ramping toward the
  applied history (300 ms for Size, 200 ms for Decay/Dimensionality/Damping), while the render *after*
  starts already settled. The first ~300 ms of the two renders would then differ by the entire
  default→target excursion — orders of magnitude beyond `render_fingerprint.h`'s `kSampleTolerance`
  (`1e-4`) — and SC-010 clause 3's equality could never hold. The rule also makes the common
  "configure then render" sequence mean what a caller expects. It does **not** apply to a setter called
  mid-render: that ramps normally, which is what SC-015 measures.

  **Smoother-cadence rule (binding, not commentary).** `OnePoleSmoother::configure(ms, sampleRate)`
  computes a **per-`process()`-call** coefficient (`primitives/smoother.h:160`). A smoother *read* once
  per control chunk must be *advanced* by `advanceSamples(kControlChunkSamples)`; one read per STFT frame
  must be advanced by `advanceSamples(hopSize)`. Every smoother in this spec states its cadence in the
  table above or in its own FR; a smoother whose cadence is unstated is an incomplete requirement.

  `PrepareConfig` fields and defaults: `std::size_t numChannels = 8` (admissible values **8 or 16 only**;
  **both orders ship**, 8 is the default and the only order the always-on gates measure — Q3),
  `std::size_t maxBlockSamples = 2048` (range 64 … 8192),
  `float maxDelaySeconds = 0.50f` (range 0.05 … 1.0; sizes the FDN buffer, FR-012),
  `bool shimmerEnabled = true`,
  `PitchMode shimmerMode = PitchMode::Granular` (FR-053), `bool bloomEnabled = true`,
  `bool spectralDiffusionEnabled = true` — **default on** (Q2 — a `prepare`-time flag with no runtime toggle; it is
  what makes the default reported latency `diffusionFftSize`, FR-084), `std::size_t diffusionFftSize = 1024`
  (clamped to [256, 4096] and rounded **down** to a power of two via `std::bit_floor`, then re-clamped —
  the same discipline `AtmosphereEngine` applies to its blur FFT), `std::uint32_t seed = 1`.

  **`maxDelaySeconds` default derivation (binding — the previous 0.25 s default was arithmetically
  incompatible with FR-012 and is corrected here).** FR-011's base set tops out at 106 ms at `S = 1.0`;
  FR-012's `S_max = 4.0` therefore needs **424 ms**, plus FR-072's excursion (`0.5 %` of the line
  ⇒ 2.1 ms) plus 4 samples of cubic-Hermite margin. Required: **≈ 426.2 ms**. The default **0.50 s**
  hosts it with ~74 ms spare; the previous 0.25 s default hosted only `S ≈ 2.36`, silently clamping the
  top fifth of the Size knob and making SC-003 clause 3's linearity requirement unsatisfiable.
  Define `kMinFullSizeDelaySeconds = 0.45f`: **any `maxDelaySeconds ≥ 0.45` reaches `S = 4.0`**, and any
  value below it clamps `S` per FR-012 with `getMaxSizeScale()` reporting the clamped value. The range
  floor stays at 0.05 so a low-latency/low-memory configuration remains expressible (Edge case 10
  exercises it deliberately), but **every success criterion that sweeps Size states its
  `maxDelaySeconds` explicitly and asserts `getMaxSizeScale() == 4.0f` as a precondition**, so a clamped
  configuration fails loudly instead of quietly measuring a smaller room.

  A freshly `prepare`d engine is silent, with every value above at its default.

### B. FDN core

- **FR-010** — The core is an `N`-channel feedback delay network, `N = config.numChannels ∈ {8, 16}`,
  with a **single contiguous delay buffer** partitioned into `N` power-of-two sections, each with its own
  offset, mask and write position — the layout `FDNReverb` uses (`effects/fdn_reverb.h:638-689`,
  `:785-791`). Per-channel state (`filterState`, `dcBlockX`, `dcBlockY`, `feedbackGain`, `absorptionDC`)
  lives in `alignas(32)` SoA arrays sized for the maximum `N`, as at `:774-780`. No `DelayLine` object is
  used for the core (the Existing-components table records why).
- **FR-011** — Reference delay lengths are **mutually coprime primes**, chosen so that the ratio
  longest : shortest is ≈ 5.3 : 1 (`FDNReverb`'s `{149…797}` ratio, `:91`) and the base set spans
  **20 … 106 ms at 48 kHz** at `size = 0.5` — roughly five times longer than `FDNReverb`'s
  hard-clamped 3–20 ms (C-1), and 424 ms at `size = 1` (FR-012). **Both tables ship** (Q3): the `N = 8`
  table (the default, and the only order the always-on gates measure) and the `N = 16` table. Each is
  `static constexpr`, **stored in ascending length order** — so channel index order *is* length order,
  which FR-050's "four longest lines" tap subset and FR-018's even/odd output split both rely on — and is
  asserted pairwise-coprime by a `static_assert`-backed compile-time check or by a dedicated unit test
  (SC-003's companion).
- **FR-012** — **Size.** `setSize(v)` maps to a delay-length scale `S(v) = 0.25 · 16^v`, i.e.
  `S ∈ [0.25, 4.0]` with `S(0.5) = 1.0`, applied multiplicatively to every reference length. The
  reachable geometry at 48 kHz is therefore **5 ms … 424 ms** per line. `config.maxDelaySeconds` sizes
  the buffer for the longest line at `S = 4.0` plus modulation and interpolation margin — **≈ 426.2 ms,
  which is why the FR-009 default is 0.50 s and `kMinFullSizeDelaySeconds = 0.45f`**. A configuration in
  which the longest scaled line exceeds the buffer is not rejected; `S`'s upper bound is clamped in
  `prepare` and the clamped maximum is reported by `getMaxSizeScale()` (FR-086). `getMaxSizeScale()`
  returns exactly `4.0f` for any `maxDelaySeconds ≥ kMinFullSizeDelaySeconds`, which is the precondition
  every Size-sweeping success criterion asserts.
- **FR-013** — **Modal density is a stated, measured consequence of Size, not a second knob.** The FDN's
  mode density in modes/Hz is `D = (Σᵢ mᵢ)/sampleRate` where `mᵢ` are the delay lengths in samples.
  The header documents the reachable `D` at both `N` values and at `S ∈ {0.25, 1.0, 4.0}`, and SC-003
  asserts `D` scales linearly with `S` to within 1 %. This is the roadmap's "modal density scaling"
  (line 268); there is no separate density-of-modes setter (`setDensity` is diffusion stages, FR-040).
- **FR-014** — Size changes are **continuous**, never re-prepared: the smoothed scale is read once per
  control chunk and the per-channel read offsets move accordingly, read with **cubic Hermite**
  interpolation while moving — specifically **`Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2,
  frac)`** (`core/interpolation.h:84`), the existing Layer 0 primitive that `FDNReverb`'s
  `delayBufReadCubic` itself calls for its modulated channels (`effects/fdn_reverb.h:661-670`, the call
  at `:669`). No local reimplementation, exactly as FR-008 requires for the finiteness helpers — and
  FR-002's include list carries `core/interpolation.h` for it. Reads use **integer** `read(size_t)` when
  the offset is settled and its fractional part is zero.
  Size sweeps must be click-free (SC-015).
- **FR-015** — Pre-delay: **two** `DelayLine`s (`primitives/delay_line.h:57`), one per input channel,
  each prepared for 200 ms and read with `readLinear` at the **same** smoothed pre-delay length (so the
  pair stays phase-coherent), written every sample (including while frozen, so the rings stay current).
  The pre-delay is **stereo**, not mono: FR-040's `DiffusionNetwork` is a stereo-in/stereo-out decorrelator
  (`processors/diffusion_network.h:396-398`) whose entire value over a mono allpass chain is the L/R
  decorrelation it derives from `kStereoOffset = 1.127f` (`:56`) applied to `kDelayRatiosL` (`:51-53`).
  Feeding it a duplicated mono sum would throw that away and leave the input injection perfectly
  correlated across the network.
- **FR-015a** — **Input signal flow, stated once and normatively** (this is the ordering FR-017, FR-040
  and FR-043 all refer to):

  ```
  inL, inR ──► finiteness guard (FR-082)
           ──► preDelayL / preDelayR   (FR-015, stereo, smoothed length)
           ──► DiffusionNetwork::process(preL, preR, diffL, diffR, n)   (FR-040, FR-043 scratch)
           ──► injection vector: diffL → even channels, diffR → odd channels,
               each scaled by kInputInjectionGain = sqrt(2/N)
           ──► FDN channel input (added INSIDE the per-line feedback gain, FR-017
               — AMENDED 2026-07-30, see FR-017)
  ```

  The even/odd split deliberately mirrors **FR-018's even/odd output tap split**, so the diffuser's L
  channel and the engine's L output tap share a channel subset and the stereo image the diffuser
  establishes survives the network. `sqrt(2/N)` makes the injected energy independent of `N`: each of
  the `N/2` channels in a subset receives the same signal at gain `g`, so the subset's injected energy is
  `(N/2)·g²·E = E`.
- **FR-016** — Per-channel DC blockers with `R = 1 − 250/sampleRate` (the ≈40 Hz −3 dB point
  `FDNReverb` derives at `:207`), **bypassed in freeze** (FR-033).
- **FR-017** — Input injection: the **`DiffusionNetwork` output** (`diffL`/`diffR`, FR-015a — *not* a
  mono pre-delay sum) is added to its channel subset **inside** the per-line feedback gain,
  `chanIn_[i] = (matrixOut_i + inject_i) · g_i`. In freeze the injection is zeroed (`:273-275`).

  > **AMENDED 2026-07-30 — the ordering is inverted from the original text, which said "after the
  > feedback gain is applied, the ordering `FDNReverb` uses (`:336-338`)". That transcription does not
  > carry over, and shipping it fails FR-030/SC-005.**
  > `FDNReverb` applies its **per-line** Jot correction `filterGainDC_[i]` on the *read* side (`:304`);
  > the write-side `feedbackGains_[i]` at `:337` is a single **uniform** base (`:567`), so adding the
  > input outside it cannot distort anything per line. `AetherReverb` has no base gain at all — the
  > per-line `gDC` **is** the gain (FR-030) — so putting the injection outside it makes the injection
  > the only signal in the loop that never pays for the transit it is about to make.
  > With `g_i = α^(m_i)`, writing everything through `g_i` makes the impulse response exactly
  > `α^n ·` (the same network at `g = 1`), so every arrival lands on the T60 envelope. Leaving the
  > injection outside makes the direct arrival out of line `i` too loud by `α^(−m_i)`: at SC-005's
  > `decaySeconds = 0.5`, `size = 1.0` grid point the longest line is 20 348 samples and that factor is
  > **350× (+51 dB)**, degenerating the impulse response into eight unattenuated echoes over 424 ms and
  > measuring **T60 ≈ 0.9 s against a requested 0.5 s** — a ±15 % criterion missed by 80 %.
  > B-4 forbids relaxing FR-030's criterion, so the **ordering** is what moves.
  > At `freezeRamp == 1` the gain is 1 and the injection is 0, so both orderings collapse to the same
  > expression and FR-033/RA-5 are unaffected. Derivation at `aether_reverb.h`'s
  > `updateDecayAndDamping()` doc block ("JOT GAIN PLACEMENT"); recorded in `compliance.md` §8.
- **FR-018** — Output taps are read from the delay lines **before** damping (as at `:280-292`,
  `:356-364`), split even-channel → L and odd-channel → R, scaled by `2/N`.
- **FR-019** — The core processes in sub-blocks of `kControlChunkSamples`; block-rate values (feedback
  gains, absorption coefficients, matrix, morph position, mod excursions) are snapshotted at the start of
  each sub-block and held constant inside it — the discipline `FDNReverb::processBlock` uses with its
  16-sample sub-blocks (`:393-414`).

### C. Feedback matrix and Dimensionality

- **FR-020** — **Dimensionality** morphs the feedback matrix continuously across three characters at
  **global** morph positions `t = 0`, `t = 0.5`, `t = 1`. (Coordinate convention, binding: `t` is the
  global morph position over the whole path; `u` is the **per-segment** blend parameter, `u = 2t` on
  segment 1 and `u = 2t − 1` on segment 2. C-3's computed figures are in `u`; SC-004 states its
  thresholds in `t`.)
  - `t = 0` — **Householder**, `M₀ = I − (2/N)·J`, the **diagonal-dominant** "2D plate" character —
    strong self-recirculation with uniform weak cross-coupling (`FDNReverb`'s matrix,
    `effects/fdn_reverb.h:749-758`). It is **not sparse**: every entry is non-zero (diagonal
    `1 − 2/N = 0.75` at `N = 8`, every off-diagonal `−2/N = −0.25`), which `applyHouseholder`'s
    `x[i] -= sum * 0.25f` over *all* channels makes plain (`:749-758`). Both endpoints are dense, so
    the morph is a **character** change, not a topology change — the header banner (FR-001) states it
    this way, because "sparse" would be shipped documentation that is false. Writing
    `u₀ = 1/√N·𝟙`, `M₀ = I − 2u₀u₀ᵀ = H(u₀)` is a **single reflection**, so `det(M₀) = −1` (C-8);
  - `t = 0.5` — **sign-corrected normalised Hadamard**, `M₁ = D·H_N/√N` where `D = diag(−1, 1, 1, …, 1)`
    — i.e. `FDNReverb`'s diffuser matrix (`:696-729`) with **row 0 negated**. Maximal uniform coupling
    (every entry is still `±1/√N`), the "3D hall" character; requires `N` a power of two, which FR-009
    guarantees. **The row negation is normative, not cosmetic** (C-8): row negation is left-multiplication
    by an orthogonal `±1` diagonal, so `M₁` is still exactly orthogonal, but `det` flips from
    `+1.000000` (verified at `N = 8` and `N = 16`) to `−1.000000`, putting `M₁` in the **same connected
    component of `O(N)` as `M₀`**. Without it no continuous orthogonal path between the two exists at
    all. FR-001's header banner records the convention, because the shipped matrix now differs from
    `fdn_reverb.h:696-729`'s by that sign;
  - `t = 1` — a **seeded random-orthogonal** matrix `M₂` with `det(M₂) = −1`, the "N-D impossible"
    character (FR-021).

  **Component invariant (binding):** all three endpoints, and therefore every point of the morph path,
  satisfy `det(M(t)) = −1`. SC-004 measures it.
- **FR-021** — The random-orthogonal endpoint is generated **once, in `prepare`**, by QR/Gram–Schmidt
  orthonormalisation of an `N×N` matrix of `Xorshift32::nextFloat()` draws (`core/random.h:59`) seeded
  with `deriveStreamSeed(config.seed, kMatrixSalt)` (`:102`). **After orthonormalisation, `det(Q)` is
  computed and one column is negated whenever `det(Q) > 0`**, so the endpoint always lands in the
  `det = −1` component (FR-020, C-8). Without this step the sign is decided by the draw — a coin flip
  that leaves the second segment discontinuous half the time, which is exactly the defect C-8 records.
  Column negation is exact, bounded and `prepare`-time. It is regenerated **by `prepare` and by
  nothing else** — in particular **not** by `setSeed`, which is audio-thread-callable and whose
  Gram–Schmidt would be an `O(N³)` job needing scratch that FR-003's allocation list does not carry.
  `setSeed` stores the new seed (so the *next* `prepare` uses it) and re-seeds only the streams FR-073
  lists. If the drawn matrix is numerically rank-deficient the draw is repeated (bounded retry count, in
  `prepare` only). Edge case 23 and SC-010's negative control are written against this reading; the
  previous revision stated both readings and is corrected here.
- **FR-022** — **Orthogonality is an unconditional invariant of the whole morph path, not of its
  endpoints** (C-3). At every morph position the applied matrix `M(t)` must satisfy
  `‖M(t)ᵀM(t) − I‖_F ≤ kOrthogonalityTolerance = 1e-5` **and** `|det(M(t)) + 1| ≤ 1e-5` (SC-004).

  **The shipped mechanism is the real-Schur geodesic** (Q4). The choice is made here rather than left to
  the plan, because the candidate mechanisms produce **different intermediate matrices** — i.e. a
  different audible Dimensionality axis — so a mechanism chosen at implementation time is a *shipped
  sound* chosen at implementation time.

  At `prepare`, for each of the two segments, form the segment's relative rotation `R = A_segᵀ B_seg`.
  Both endpoints have `det = −1` (FR-020, FR-021), so `det(R) = +1` and `R ∈ SO(N)`; reduce `R` to real
  Schur form `R = V·B(θ)·Vᵀ` with `V` orthogonal and `B(θ)` block-diagonal in `2×2` rotations through
  the angles `θ = (θ₁ … θ_{⌊N/2⌋})` (plus identity blocks for unit eigenvalues). At the control grid the
  applied matrix is

  ```
  M(u) = A_seg · V · B(u·θ) · Vᵀ
  ```

  which is **exactly orthogonal at every `u`** (a product of orthogonal factors — no re-orthonormalisation
  step exists to fail), hits both endpoints exactly (`B(0) = I`, `B(θ) = R`), is continuous, and traverses
  every invariant 2-plane at a **constant angular rate**. That last property is why this mechanism is
  pinned: the path between the three named endpoints is *canonical*, so the shipped character axis is
  reproducible and re-derivable rather than dependent on an implementer's choice. `V` and `θ` are
  `prepare`-time constants per segment; the runtime cost is one `O(N³)` product per control chunk
  (≈ 1024 MACs at `N = 8`, once per 64 samples — trivial against SC-008's 533 333 ns/block).

  **The `prepare`-time reduction is hand-written, and is itself a tested component.** There is no LAPACK
  in this repo, so the symmetric-eigen / real-Schur reduction of `R` ships as a private static helper in
  `aether_reverb.h` and carries its **own** unit tests (SC-004 clause 6): `V` orthogonal to 1e-6, `B(θ)`
  block-diagonal with exact `2×2` rotation blocks, and `V·B(θ)·Vᵀ` reconstructing `R` to 1e-6 — at
  `N ∈ {8, 16}`, over seeded random `SO(N)` inputs as well as the two shipped endpoint pairs, including
  the degenerate cases (repeated eigenvalues, `θᵢ = 0`, `θᵢ = π`). It runs in `prepare` only, so it is
  not RT-constrained; it is bounded-iteration and allocation-free all the same, since `prepare` is where
  every buffer is already sized.

  **Not shipped: the Householder-product mechanism.** Representing each endpoint as a product of exactly
  `K` unit reflections (`M = Π_{k=1..K} H(vₖ)`, `H(v) = I − 2vvᵀ`) and interpolating the `vₖ` is also
  exactly orthogonal, is `O(N)` per reflection, and would need `K` **ODD** — `det(Π H(vₖ)) = (−1)^K`, so
  `K = N` gives `det = +1` at `N ∈ {8, 16}` and cannot represent the `t = 0` endpoint at all (C-8), while
  `K = N − 1` spans exactly the `det = −1` component (Cartan–Dieudonné). It is rejected because a
  `K`-reflection factorisation of an endpoint is **not unique**, and padding a single reflection out to
  `K` with cancelling pairs `H(a)H(a) = I` leaves `a` free: two implementations satisfying every SC-004
  clause could traverse completely different paths between the same three endpoints, and any later
  re-implementation would silently change the shipped sound while staying green. Recorded so the
  alternative is not re-litigated at plan time.

  **Struck: lerp + Newton–Schulz polar re-orthonormalisation.** The previous revision admitted
  `M̃ = (1−u)A + uB` followed by `M ← 1.5M − 0.5·M MᵀM`. It is **provably inadmissible for these
  endpoints**, and pinning the determinants does not save it (C-8, verified numerically this session):
  `M̃` is exactly **singular** at `u = 0.5` for the corrected pair (`σ_min = 0.0000`,
  `‖M̃ᵀM̃ − I‖_F = 2.0000` at `N = 8`, `2.8284` at `N = 16`), because `(1−u)A + uB` is singular somewhere
  in `(0,1)` exactly when `AᵀB` has eigenvalue `−1`, which this pair does — and Newton–Schulz acts on
  singular values as `σ ← 1.5σ − 0.5σ³`, which has **`σ = 0` as a fixed point**, so a zero singular value
  can never be lifted. A naive lerp with no re-orthonormalisation is likewise **forbidden**, and SC-004
  clause 3 is written to fail both, at the specific positions where they fail.
- **FR-023** — The morph is applied **per control chunk**, not per sample: the matrix is recomputed at
  most once per `kControlChunkSamples` and held constant inside the chunk (FR-019). The morph position
  itself is the smoothed `setDimensionality` value plus the tidal modulation of FR-071, clamped to
  [0, 1].
- **FR-024** — The matrix is applied to the `N`-vector of post-damping channel signals once per sample.
  FR-022's geodesic materialises a dense `M(t)` once per control chunk, so the per-sample operation is a
  plain `N×N` multiply — 64 MACs at `N = 8`, 256 at `N = 16` — inside SC-008's budget. There is no
  per-reflection alternative to choose between at plan time: FR-022 pins the mechanism (Q4).
- **FR-025** — Because `M(t)` is orthogonal, **the matrix contributes exactly unit loop gain at every
  morph position** — i.e. it preserves the **L2 norm of the `N`-channel state vector** exactly, at every
  `t`. Given that every *injection* path into the network is zeroed (input injection, FR-033 step 4;
  both shimmer sends and the bloom send, FR-033 step 5), the only remaining energy operators in the loop
  are the per-line gains (FR-030) and the damping filters (FR-031). That is what makes RT60 predictable
  (SC-005) and freeze exact (SC-002).

  **What this invariant does and does not imply (binding for SC-002's construction).** It is a statement
  about *total state energy*. It is **not** a statement about the level of the output taps: FR-018's tap
  is a fixed rank-2 linear functional of a state vector that an orthogonal `M(t)` continuously *rotates*,
  and with `dimensionalityTideDepth > 0` the operator itself is morphing. A fixed projection of a
  rotating vector is not invariant, and a time-varying orthogonal mixer moves energy **between** frequency
  bands while conserving the total. SC-002 therefore measures the conserved quantity —
  `getStateEnergy()` (FR-086) — as its primary clause, and treats output-tap level as a separate,
  separately-derived clause. Do not read a ±0.5 dB per-octave output bound as following from
  losslessness; it does not.
- **FR-026** — Changing `numChannels` is a `prepare`-time operation only. There is no runtime channel
  count setter (it would resize the delay buffer).
- **FR-027** — `[[nodiscard]] float getMatrixOrthogonalityError() const noexcept` returns the current
  `‖MᵀM − I‖_F`, computed at control rate into a cached scalar. It exists for SC-004 and for Phase 7
  diagnostics; it performs no work in `process`.

### D. Decay, damping, freeze

- **FR-030** — **Decay.** Per-line feedback gains follow Jot's absorption law, the formula `FDNReverb`
  derives at `:585`: `gᵢ = 10^(−3·mᵢ/(T60·sampleRate))` where `mᵢ` is that line's **current, Size-scaled**
  delay length. Recomputed at the control grid whenever Size or Decay has moved (both are smoothed, so
  the recompute is cheap and bounded). `T60 = setDecaySeconds`, range 0.5 … 60 s (RA-4).
- **FR-031** — **Damping.** One one-pole lowpass per line inside the loop, its coefficient derived from a
  DC/Nyquist gain ratio exactly as at `:592-599`: `T60_nyq = T60_dc · 0.05^damping`, `ratio = gNyq/gDC`,
  `coeff = 2·ratio/(1+ratio)`. `damping = 0` gives a bright, near-flat tail; `damping = 1` gives a
  20 × shorter T60 at Nyquist.
- **FR-032** — Per-line gains are **clamped to ≤ 1.0** at all times outside freeze. Combined with FR-025
  this makes the unfrozen loop unconditionally non-expansive regardless of Size, Decay, Dimensionality
  or their modulation — a structural stability guarantee, not a measured one.
- **FR-033** — **Freeze (roadmap line 270: "freeze at unity feedback, energy-conserving").**
  `setFreeze(true)` performs, in this order, over `kFreezeLatchMs = 50` ms:
  1. the per-channel modulation excursion (FR-072) ramps to **zero**;
  2. each channel's read offset ramps to the **nearest integer**, so the fractional part reaches exactly
     0 and reads become integer `read(size_t)` calls — **no interpolation, hence no interpolation loss**
     (C-4). Size and life modulation of the delay lengths are held for the duration of the freeze;
  3. the damping filters and DC blockers are bypassed (the `FDNReverb` pattern, `:296-322`);
  4. the input injection is zeroed (`:273-275`);
  5. **every injection path from a non-orthogonal stage is ramped to zero** — the +12 shimmer send, the
     +7 shimmer send (FR-050/FR-051) **and** the harmonic-bloom send (FR-055/FR-058), all three over the
     same `kFreezeLatchMs` ramp. Rationale, identical for all three and previously stated for bloom only:
     a pitch-shifted return and a resonant emphasis return are **energy sources** inside a unity-gain
     loop — neither is a per-line gain nor a damping filter, so neither is covered by FR-025's decay
     accounting, and either one left live makes SC-002 unachievable and freeze unbounded. The two shimmer
     `PitchShiftProcessor` instances keep running (so no state discontinuity accumulates); only their
     **returns** are muted;
  6. every per-line gain becomes exactly `1.0f`.

  Motion during freeze is provided by the **matrix morph alone** (FR-020–FR-023), which is exactly
  orthogonal and therefore exactly lossless. `setFreeze(false)` reverses the sequence over the same
  ramp, restoring all three send gains to their smoothed control values. Both transitions must be
  click-free (SC-015), and SC-002 clause 4 measures the frozen bound **with all three sends at 1.0** so
  the disable path is exercised rather than assumed.
- **FR-034** — While frozen, `setSize`, `setDecaySeconds` and `setDamping` are **accepted and stored but
  not applied**; they take effect when freeze is released. The header states this, because a size change
  under freeze is the one thing that cannot be lossless. Observable consequence, and the way SC-017
  clause 3 measures it: `getEffectiveDelayLengthSamples(i)` must be **unchanged** by a `setSize` call
  issued while `isFrozen()`, and must move to the new geometry after `setFreeze(false)` settles.
- **FR-035** — Freeze is a **first-class playing technique** (roadmap lines 73–74), so it must be usable
  repeatedly and at any point: entering and leaving freeze many times in one render must neither drift
  the level (SC-002 clause 5) nor accumulate DC (the DC blockers run whenever unfrozen). **What
  "first-class" does *not* mean here is recorded in RA-5**: freeze is an exactly-lossless *hold*, so all
  three sends are muted (FR-033 step 5) and the geometry is latched (FR-034) for its duration. Both are
  forced by roadmap line 280's ±0.5 dB conservation bound, and both are consequences Phases 7, 10 and 12
  inherit — RA-5 states them so they are not discovered by ear.
- **FR-036** — Denormal hygiene: the engine documents that the caller is expected to have FTZ/DAZ enabled
  (`core/scoped_denormal_mode.h` is the repo mechanism, and `dsp_test_main.cpp:13` enables it for tests),
  and the frozen loop additionally adds a `kDenormalFloor`-magnitude alternating-sign tickle **only**
  when not frozen — under freeze the tickle would be an energy source and would break SC-002.
- **FR-037** — `[[nodiscard]] bool isFrozen() const noexcept` reports the latched state (true only once
  the FR-033 sequence has completed), for tests and Phase 7.

### E. Density (diffusion)

- **FR-040** — **Density** is the reused `DiffusionNetwork` (`processors/diffusion_network.h:206`),
  prepared once in `prepare` with `prepare(static_cast<float>(sampleRate), config.maxBlockSamples)`
  (`:242`), placed on the **input** path between the pre-delay and the channel injection (FR-015a).
  There is no in-loop diffusion stage. `setDensity(v)` is smoothed **Aether-side** at 100 ms
  (FR-009's table), read once per control chunk, and the smoothed value forwarded as `v·100` to
  `DiffusionNetwork::setDensity` (`:325`).
- **FR-041** — After every control-grid parameter push, `DiffusionNetwork::snapSmoothers()` (`:361`) is
  called. This is the header's own documented contract for callers that already smooth on their own
  control grid (`:347-360`): without it the network never settles, its static fast path (`:534`, `:550`)
  is permanently defeated, and a second 10 ms lag is put in series with ours for no benefit.

  **This is exactly why FR-009 gives `setDensity` an Aether-side smoother** (the previous revision said
  "none — network smooths", which `snapSmoothers()` contradicts). `snapSmoothers()` snaps
  `densitySmoother_`, `sizeSmoother_`, `widthSmoother_`, `modDepthSmoother_` **and every
  `stageEnableSmoothers_[i]`** (`:361-368`, verified) — and the per-stage enables are precisely the
  crossfade mechanism density rides on (`updateDensityTargets()` sets per-stage enable targets 0 → 1,
  `:615-638`). With `snapSmoothers()` on the network side and no smoothing on ours, SC-015's required
  `setDensity` 0 → 1 step would engage all eight allpass stages in a single control chunk. The pattern
  adopted here is `ContinuousBody`'s, which is what the header's doc block (`:347-360`) describes:
  **the caller smooths, then pushes, then snaps.**
- **FR-042** — The diffusion network's `setSize` is driven from **Aether's** Size, mapped into
  `DiffusionNetwork`'s [0, 100] percent domain, so early diffusion scales with the space. Aether's Size
  is already smoothed at 300 ms (FR-009) and read on the control grid, so the push+`snapSmoothers()`
  discipline of FR-041 is satisfied for it without a second smoother — **stated explicitly** so the
  asymmetry with `setDensity` is not read as an oversight. Its
  `setModDepth`/`setModRate` are left at their defaults (0 / 1 Hz, `:226`, `:230`); Aether's own
  modulation is FR-072's and applies to the FDN lines, not to the diffuser.
- **FR-043** — `DiffusionNetwork::process` is called with **separate** input and output scratch buffers
  (`:396-398`); the engine does not rely on in-place behaviour the header does not document.
- **FR-044** — At `density = 0` the network's stages crossfade out (`:615-630`) and the input reaches the
  FDN essentially undiffused; the resulting sparser early response is the intended "plate-like" extreme
  and is the configuration SC-003 uses as its negative control (echo density must be **lower** there).

### F. Shimmer bloom and harmonic bloom

- **FR-050** — **Shimmer taps.** Two `PitchShiftProcessor` instances (`processors/pitch_shift_processor.h:100`),
  one at **+12** semitones and one at **+7**, both operating on a **mono sum** of a fixed subset of the
  post-damping channel signals (C-5). Total instance count is **two**, not four. Every subset, every
  normalisation and every injection gain below is a **named constant** (Q6), so SC-016's thresholds are
  measured against a pinned level and the taps cannot be silently re-wired later.

  **Tap read subset.** `kTapReadCount = 4`: the taps read the **four longest lines**, i.e. channels
  `N−4 … N−1` given FR-011's ascending tables — `{4,5,6,7}` at `N = 8`, `{12,13,14,15}` at `N = 16` —
  summed at `kTapReadNormalisation = 1/4`. The harmonic bloom reads the **same** mono sum (FR-055):
  there is exactly one tap sum formed per sample, not three.

  **Injection subsets.** Each tap's return is injected into its own **pinned pair** of channels. The two
  pairs and the bloom's channel set are **mutually disjoint** and together partition the channel index
  set:

  | constant | `N = 8` | `N = 16` | rule |
  |---|---|---|---|
  | `kShimmerOctaveInjectChannels` (+12) | `{1, 4}` | `{1, 8}` | `{1, N/2}` |
  | `kShimmerFifthInjectChannels` (+7) | `{3, 6}` | `{3, 3N/4}` | `{3, 3N/4}` |
  | `kBloomInjectChannels` | `{0, 2, 5, 7}` | the remaining 12 | every channel in neither pair |

  Each pair **spans both parities**, so neither interval is hard-panned by FR-018's even→L / odd→R output
  split — which is exactly the defect an "octave into even channels, fifth into odd channels" rule would
  ship (the octave in the left image and the fifth in the right).

  **Injection gain.** `kTapInjectionGain(subset) = sqrt(2/|subset|)`, multiplied by the tap's send level
  (FR-051) — the same energy-normalising form FR-015a uses for the input injection (`sqrt(2/N)` there,
  where the subset is `N/2` channels): `1.0` for either shimmer pair, `sqrt(1/2) ≈ 0.7071` for the
  bloom's four channels at `N = 8` and `sqrt(1/6) ≈ 0.4082` for its twelve at `N = 16`.

  **The read subset is *not* disjoint from the injection subsets, and does not need to be.** At `N = 8`
  channels 4 and 6 are both read and injected. The previous revision required "a different, disjoint"
  subset and justified it with a latency-isolation argument FR-052 disproves: FR-020's endpoints are
  **dense**, so after a single sample step every channel already carries a contribution from every other
  and no disjointness survives one round trip. What the pinned subsets actually buy is **stereo
  re-diffusion of a mono tap** and a **defined, measurable injected level** — not latency isolation.

  **Cadence.** Each tap is driven on a **fixed `kControlChunkSamples = 64` cadence anchored to the
  absolute control grid** (FR-005), injected **one chunk late** (Q5): the mono tap sum is accumulated
  over one chunk into a `prepare`-allocated 64-sample scratch buffer, `PitchShiftProcessor::process(in,
  out, 64)` is called once at the chunk boundary, and that output is injected across the **following**
  chunk. Each shimmer leg therefore carries **64 samples (1.33 ms at 48 kHz) of deferral on top of its
  mode latency** (FR-054). The cadence matches the shifter's own
  `kSmoothingSubBlockSize = 64` (`processors/pitch_shift_processor.h:165`), and because it is anchored to
  the absolute sample counter rather than to caller blocks, the shifter's internal grain/phase state does
  not depend on how the host partitions its blocks — which is what makes SC-011's 1e-6 invariance
  **structural** rather than hoped-for. A caller-block cadence would have put both that invariance and
  the shimmer's loop time at the host's mercy.
- **FR-051** — Each tap has an **independent send level** (roadmap line 276): `setShimmerOctaveSend` and
  `setShimmerFifthSend`, each 0 … 1, smoothed at 100 ms, applied as a gain on the tap's return **before**
  injection and multiplied by `kTapInjectionGain` of that tap's pinned pair (FR-050) — at `|subset| = 2`
  that factor is exactly 1.0, so the send value *is* the injected gain for both shimmer legs.
  *Independent* is a measured property, not a wiring note: SC-016 asserts that raising the
  octave send alone puts energy at `2·f0` and **not** at `1.5·f0`, and vice versa. Both returns are
  ramped to zero while frozen (FR-033 step 5).
- **FR-052** — **Why latency-mismatch comb filtering does not arise here — stated accurately** (the
  previous revision's structural claim was contradicted by FR-020's own matrix and is corrected).
  `ShimmerDelay`'s header records that shimmer mix must be routed by
  `FlexibleFeedbackNetwork::setProcessorRouteMix()` "to avoid comb filtering … from latency mismatch when
  blending different-latency signals" (`effects/shimmer_delay.h:59-61`, `:102-105`). The previous
  revision claimed the structural equivalent was that "the tap reads from one channel subset and injects
  into a disjoint one, so no channel ever receives both a direct and a latency-shifted copy of its own
  signal". **That is false at every morph position**: FR-020 establishes that both matrix endpoints are
  **dense** (Householder: diagonal `0.75`, every off-diagonal `−0.25` at `N = 8`; Hadamard: every entry
  `±1/√N`), and FR-024 applies the matrix once per sample — so after a *single* sample step every channel
  already carries a contribution from every other. The read/inject disjointness survives **zero** round
  trips.

  The accurate statement is: **the tap return is decorrelated from the direct path by the pitch shift
  itself.** A +12 or +7 semitone copy is not a coherent copy of the signal it came from, so the
  fixed-delay-plus-copy geometry that produces classical comb notches does not exist; what remains is a
  frequency-shifted addition at its own send gain. The disjoint read/inject subsets buy **stereo
  re-diffusion** (FR-050) — letting the network spread a mono tap across the image — **not** latency
  isolation, and the header says so.

  **This is given a measurement rather than left as an argument.** SC-007 clause 4(a) (the 1/3-octave
  isolated-mode test, the one metric that would show comb notches) pins all sends at 0, so it cannot see
  this; SC-006 measures only boundedness and HF fraction. SC-007 clause 4 therefore gains a second
  configuration at `shimmerOctaveSend = shimmerFifthSend = 1` (bloom still 0) with a **no-band-below-its-
  neighbour-median** bound, so a comb-notched return fails a criterion instead of contradicting a
  sentence.
- **FR-053** — The pitch-shift mode is a **`prepare`-time** choice (`config.shimmerMode`) defaulting to
  `PitchMode::Granular`. Runtime mode changes are not exposed: `getLatencySamples()` "changes immediately"
  on `setMode` per the header (`:189-193`), and a loop whose latency changes mid-render is a click.
- **FR-054** — The header states the loop-time consequence of each mode, from the shipped numbers
  (`:280-287`) **plus FR-050's fixed 64-sample injection deferral**, which applies to every mode (Q5).
  The figure the header ships is therefore **mode latency + 64 samples**, not the bare mode latency:
  `Simple` = 0 + 64 = **64 samples ≈ 1.33 ms** at 48 kHz (but audible artifacts, and it is a
  delay-modulation shifter — poor inside a recirculating loop), `Granular` ≈ 2048 + 64 = **2112 samples
  ≈ 44 ms** at 48 kHz, `PhaseVocoder` = 4096 + 1024 + 64 = **5184 samples ≈ 108 ms** at 48 kHz. The
  shimmer generation period is `max(loop time, tap latency + kControlChunkSamples)`, so a PhaseVocoder
  tap cannot regenerate faster than ~108 ms.
- **FR-055** — **Harmonic bloom.** A bank of **`kMaxBloomResonators = 32`** driven second-order
  resonators inside the loop (Q7), driven by the **same** mono sum the shimmer taps read (FR-050's four
  longest lines at `kTapReadNormalisation = 1/4`), its summed output injected back into
  `kBloomInjectChannels` — the channels neither shimmer pair uses, `{0, 2, 5, 7}` at `N = 8` — at
  `kTapInjectionGain = sqrt(2/|kBloomInjectChannels|)` times `setBloomSend` (independent of the two
  shimmer sends — roadmap line 276).

  **Why 32 and not `SympatheticResonance`'s 64.** `kMaxSympatheticResonators = 64`
  (`systems/sympathetic_resonance.h:43`) is the width that class proves, but the bank is a direct SC-008
  term — a per-sample `processSympatheticBankSIMD` call vectorised across resonators — and 32 is still
  **≥ 4×** the partial count SC-016 clause 3 needs (a four-note chord at four partials each) at half the
  per-sample cost. Sixteen was rejected in the other direction: four notes × four partials already
  saturates it, and "reinforces the partials of the held chord" thins out.

  The per-sample kernel is the **reused** `processSympatheticBankSIMD`
  (`systems/sympathetic_resonance_simd.h:39-50`), called with Aether-owned plain arrays.

  > **AMENDED 2026-07-30 (phase-owner ruling) — the bloom ships TWO return paths, not one, and both
  > are authorised.** The in-loop injection specified above ships exactly as written and is unchanged.
  > **In addition**, the same shelved, `1/√count`-normalised bank output is summed directly into the wet
  > bus, out of loop, at `kBloomEmphasisGain = 34.0f` (declared `aether_reverb.h:1505`, computed
  > `:3521` as `chunkBloomEmphasisGain_ = send01 · kBloomEmphasisGain · bloomInvSqrtCount_`, applied
  > `:4340-4342`). It rides the same `(1 − freezeRamp)` factor as the injection, so FR-033/SC-016
  > clause 4 are untouched, and it writes only to `wetScratch*` — never to a delay line — so it cannot
  > destabilise the recirculating network.
  >
  > **Why the specified path alone is not sufficient, and why this is a clarification of the mechanism
  > rather than a moved criterion.** A resonator *inside* an LTI loop multiplies the network response by
  > `1/(1 − L(f_k))`, whose magnitude exceeds 1 only where the loop phase at `f_k` happens to be
  > regenerative. The FDN's phase rotates through 2π every ~18.8 Hz while a Q = 400 resonator at 220 Hz
  > is ~0.55 Hz wide, so each resonator samples **one arbitrary loop phase and holds it** — raising
  > `setBloomSend` cannot change a sign. **Measured, in-loop path only, at the shipped guard ceiling:
  > target-band rises of −0.77 / −0.16 / +0.24 / +2.68 dB** (`aether_reverb.h:361-371`), against
  > SC-016 clause 3's requirement of **≥ 6 dB**; at a ceiling of 0.862 the network diverges instead
  > (peak 46.8). Out of loop, `|1 + G·e^{jφ}| ≥ G − 1` is phase-independent, and SC-016 clause 3
  > brackets `G` from both sides (worst-target / non-target-mean: +4.5/+0.3 dB at G = 12, +7.3/+1.0 at
  > 20, +10.1/+1.8 at 30, +13.1/+2.7 at 45 — the last already outside the +2 dB non-target bound).
  > **Shipped at 34: minimum target rise +8.55423 dB @ 396.85 Hz, mean non-target rise +0.896776 dB
  > over 15 bands.** SC-016 clause 3's thresholds (≥ 6 dB target, ≤ 2 dB non-target) are **unchanged** —
  > only gain constants moved, which is the Q7 clarification rule, not a criterion relaxation.
  >
  > Recorded as deviation **D-10** (`compliance-evidence-r3.md:750`); this amendment **closes** it.
- **FR-056** — **Tuning source: note-informed** (Q1). The bank's resonator frequencies are supplied by
  the caller through

  ```
  void bloomNoteOn(std::int32_t voiceId, const float* partialHz, std::size_t count) noexcept;
  void bloomNoteOff(std::int32_t voiceId) noexcept;
  ```

  — the `SympatheticResonance::noteOn`/`noteOff` shape (`systems/sympathetic_resonance.h:179`, `:264`)
  **without** its 4-partial `SympatheticPartialInfo` cap (`:40`, `:71-74`), so Phase 7 can pass a
  voice's actual low-order partials. This is the roadmap's literal "partials of the held **chord**"
  (line 275).

  Both calls are **audio-thread-callable**, allocation-free, lock-free and `noexcept` (FR-008).
  `partialHz == nullptr` or `count == 0` is a no-op; `count` is clamped to `kMaxBloomResonators` (32,
  FR-055); every frequency is tested with `detail::isNaN`/`detail::isInf` (`core/db_utils.h:54`, `:175`)
  and clamped to `[20 Hz, 0.45·sampleRate]` **before** it can reach a coefficient computation (FR-057);
  a `voiceId` that is already live replaces its own partial set rather than allocating a second one, and
  a bank that is already full retires its oldest voice. `bloomNoteOff(voiceId)` releases that voice's
  resonators through the kernel's own release envelope
  (`systems/sympathetic_resonance_simd.h:39-50`'s `releaseCoeff`/`envelopes` arguments), not by a hard
  cut, so retirement is click-free. `getActiveBloomResonatorCount()` (FR-086) reports the live count.

  **Not shipped: tail-derived self-tuning.** Peak-picking the targets out of the FR-060 STFT (with
  hysteresis and a bounded retune slew) was the alternative — Layer-4-clean and note-free, "the organism
  feeds on itself" in the roadmap's own idiom. It is rejected because it **couples the bloom to
  `spectralDiffusionEnabled`** (the analysis half would have to run even with the stage off), adds a
  peak-picking / hysteresis / retune-slew stability surface, and forces a second accessor
  (`copyBloomTargetsHz`) plus a convergence criterion that exist solely to give a self-tuning bank a
  testable stimulus. Neither is shipped (FR-065, FR-086). Recorded so the alternative is not
  re-litigated.

  **Consequence for Phase 7:** a global Layer 4 effect now has a note API, and Phase 7 must forward note
  events into it — an obligation the roadmap does not state, recorded as **RA-7**.
- **FR-057** — Resonator coefficients are computed with the **verified formulas**
  `SympatheticResonance` uses: `r = exp(−π·(f/Q_eff)/sampleRate)`, `coeff = 2r·cos(ω)`, `rSquared = r²`
  (`:426-438`), with frequency-dependent Q, `Q_eff = Q · clamp(500/f, 0.5, 1.0)` (`:440-446`, `:58`,
  `:61`). `setBloomDecay` maps 0 … 1 → `Q ∈ [20, 400]`.
- **FR-058** — **Bloom stability guard (the reason this is not simply "more feedback"). The construction
  is stated, not left as an outcome** (Q7). Two multiplicative factors, both recomputed on the **control
  grid** (once per `kControlChunkSamples`, never per sample) whenever the bank retunes (FR-056) or
  `setBloomDecay`, `setDecaySeconds` or `setSize` has moved:
  1. **Per-resonator inverse-peak-gain normalisation.** Resonator `k`'s input gain carries the factor
     `computeResonatorPeakGainInverse(coeffs_k, f_k)` = `(1 − r)·√(1 − 2r·cos(2ω_k) + r²)` — the
     reciprocal of a driven second-order resonator's peak gain at its own resonance, verified this
     session at `systems/sympathetic_resonance.h:397-420` — so a high-Q resonator contributes **unit**
     gain at its own centre frequency instead of `Q`-fold gain.
  2. **Global `1/√count` scale**, `count = getActiveBloomResonatorCount()`, so the summed return's
     energy is independent of how many partials are currently held.

  **Target, stated as a computable criterion.** Let `g_line(k)` be the Jot per-line gain (FR-030) of the
  channel the bloom injects into and `g_bloom(f_k)` the bloom return gain at resonator `k`'s centre
  frequency after both factors above and `setBloomSend`·`kTapInjectionGain` (FR-055). The **combined loop
  gain** `g_line(k) · g_bloom(f_k)` must be `≤ 1.0` **at every active resonator's own centre frequency**,
  evaluated at each control chunk outside freeze. If the product would exceed 1.0 at any active `f_k`,
  the **whole** bloom return is scaled by the worst-case reciprocal — one scalar on the summed return, so
  the bank's relative tuning is untouched and no resonator is silently detuned or dropped.

  **If SC-016 clause 3's ≥ 6 dB target-band emphasis proves unreachable under this guard, the admissible
  fixes are `setBloomSend`'s mapping and the normalisation constants above — never the criterion** (B-4).

  Bloom is **disabled while frozen**
  (its return gain ramps to zero as part of the FR-033 sequence, step 5, alongside the two shimmer
  sends), because a resonant emphasis stage inside a unity-gain loop is an energy source and would break
  SC-002. That the bank actually *reinforces* the partials it is tuned to — as opposed to merely failing
  to explode — is measured by SC-016 clause 3.
- **FR-059** — Bloom and shimmer both have a **HF shelf** on their return path (one-pole, corner
  documented in the header) so that repeated upward pitch shifting cannot pile energy into the top
  octave without bound. This, with FR-058, is what SC-006 measures.

### G. Spectral diffusion

- **FR-060** — When `config.spectralDiffusionEnabled` — a `prepare`-time flag **defaulting to `true`**,
  with no runtime toggle (Q2, RA-2) — the **wet** output is routed through one
  stereo `STFT` → phase-smear → `OverlapAdd` stage at `config.diffusionFftSize` with **75 % overlap**
  (`hopSize = fftSize/4`) and `applySynthesisWindow = true` — the configuration `OverlapAdd`'s own header
  documents as required for spectral-modification processors and forbids at 50 % overlap
  (`primitives/stft.h:225-228`).
- **FR-061** — Smearing is **per-bin phase perturbation**: for each bin, `setPhase(bin, phase + a·δ)`
  where `phase = getPhase(bin)` (`primitives/spectral_buffer.h:91`, `:106`), `δ` is a
  `Xorshift32::nextFloat()` draw scaled to ±π, and `a` is the smoothed `setSpectralDiffusion` value.

  **δ is redrawn every hop**, per bin, per channel — it is **not** drawn once and held. This is the whole
  behavioural choice and it is stated normatively because the two readings are audibly and measurably
  different: a *held* δ is a static dispersive allpass (a fixed timbral colouration, no smearing over
  time), while a *redrawn* δ decorrelates successive frames and produces the time-smeared "underwater
  chamber" the roadmap asks for (line 277). SC-007 clauses 1 and 2 are written against the redrawn form.

  **Magnitudes are never modified.** The stage is **not**, however, unity-gain — the previous revision's
  Parseval claim was wrong and is struck. `OverlapAdd` reconstructs with a fixed COLA factor
  `colaNormalization_ = 1/colaSum` computed at `prepare` (`primitives/stft.h:243-262`) and applied
  unconditionally in `synthesize()` (`:299-307`). That factor is correct only when the overlapping frames
  are mutually **coherent**; independently randomised per-frame phases sum incoherently and the
  reconstruction level falls. Measured on the exact configuration FR-060 pins (fftSize 1024, hop 256 =
  75 % overlap, Hann analysis + Hann synthesis, white-noise input):

  | `spectralDiffusion` | 0 | 0.25 | 0.5 | 0.75 | 1.0 |
  |---|---|---|---|---|---|
  | outRMS / inRMS | 1.0000 | 0.9260 | 0.7443 | 0.5635 | 0.5001 |
  | dB | 0.00 | −0.67 | −2.57 | −4.98 | −6.02 |

  Left uncorrected, turning the knob up attenuates the **entire wet path** by 6 dB. The stage therefore
  applies a **coherence make-up gain** `g(a)`, a smooth monotone function of the smoothed amount fitted
  to the reciprocal of that curve (`g(0) = 1.0`, `g(1) ≈ 2.0`), evaluated once per frame on the control
  grid. The residual is a measured requirement: SC-007 clause 5 bounds wet RMS variation across the
  sweep at **≤ 1.0 dB**, and the fitted table is recorded in the header.

  Perturbations being drawn **per bin per channel** additionally decorrelates L and R as the amount
  rises — stated as intended behaviour, and measured by SC-007 clause 2.
- **FR-062** — The **dry** path is delayed by a `prepare`-allocated `fftSize`-sample `DelayLine` so dry
  and wet stay aligned and the engine reports **one** latency (RA-2). When the stage is disabled at
  `prepare`, neither the STFT set nor the dry delay is allocated and the latency is 0.
- **FR-063** — At `spectralDiffusion = 0` the stage is **transparent in magnitude and phase** up to
  STFT/OLA reconstruction error, and `g(0) = 1.0` exactly. The round-trip must match a `fftSize`-delayed
  copy of its input to within an **a-priori** bound derived from the COLA property FR-060's configuration
  already guarantees (Hann² at 75 % overlap, `primitives/stft.h:225-228`): **per-sample absolute error
  ≤ 1e-4 and error RMS ≤ −70 dBFS relative to the reference render's RMS**. SC-007 clause 3 asserts those
  numbers and carries a negative control that must exceed them. The stage is not bypassed at 0 (that
  would change latency); it simply perturbs nothing.
- **FR-064** — The STFT is fed and drained on the control grid, and the `setSpectralDiffusion` smoother
  is advanced by `advanceSamples(hopSize)` immediately before its value is read for a frame (FR-009's
  cadence rule). Advancing it once per frame with `process()` would stretch its 100 ms time constant to
  ~25 s at the default hop and the knob would appear frozen.
- **FR-065** — There is exactly **one** FFT per channel per hop, and it serves the smearing stage alone.
  The bloom does **not** read the spectrum (FR-056 is note-informed, Q1), so no analysis-only STFT is
  allocated and **the bloom is not coupled to `config.spectralDiffusionEnabled`**: with the spectral
  stage disabled the bloom still runs, tuned by `bloomNoteOn`, and the engine's latency is 0 (FR-084).

### H. Life modulation

- **FR-070** — **Size breathes.** One owned `BreathingModulator` (`processors/breathing_modulator.h:105`)
  prepared at the audio sample rate, advanced by `processBlock(kControlChunkSamples)` (`:209`) once per
  control chunk, its `getCurrentValue()` (`:222`) scaled by `setSizeBreathDepth` and added to the
  smoothed Size before FR-012's mapping. **The engine calls `BreathingModulator::setRate(0.05f)`
  explicitly in `prepare` (`processors/breathing_modulator.h:170-173`) — it does not inherit the class
  default `kDefaultRate = 0.1f` (`:111`)** — so the shipped breath period is exactly **20 s**, inside the
  class's own `[kMinRate 0.01, kMaxRate 0.5]` (`:108-110`). There is no rate setter on `AetherReverb`:
  the rate is pinned, which is what makes SC-017's durations derivable rather than assumed. The
  modulator's **own** depth is left at its class default `kDefaultDepth = 1.0f` (`:112`) and irregularity
  at `kDefaultIrregularity = 0.0f` (`:113`) — `setSizeBreathDepth` scales the modulator's output on the
  Aether side, so the two depths are not multiplied and the shipped configuration is fully pinned
  (SC-017 clause 1a reconstructs the modulator's trajectory from exactly these settings). Combined
  Size stays clamped to [0, 1].
  Roadmap line 278 ("Life-modulated internals: size and matrix slowly breathe via Phase 1 modulators").
  **This must be observable**: SC-017 clauses 1a/1b require `getEffectiveDelayLengthSamples(i)` sampled over a
  breath period to have a peak-to-peak spread matching the depth, and to be flat at depth 0 — so a
  stubbed modulator fails. Because the period is 20 s, a **single 24 s render covers a full cycle**,
  which is what lets SC-017's core clause run on **every** build rather than only in the nightly lane.
- **FR-071** — **The matrix breathes.** One owned `TidalModulator` (`processors/tidal_modulator.h:122`),
  same cadence, its `getCurrentValue()` scaled by `setDimensionalityTideDepth` and added to the smoothed
  Dimensionality before FR-023's clamp.

  **The tide rate is pinned, and the previous revision left it unspecified.** The engine calls
  `TidalModulator::setRate(1.0f)` explicitly in `prepare` (`:202-205`) — it does **not** inherit the
  class default `kDefaultRate = 0.5f` (`:143`), which would give
  `getBasePeriodSeconds() = kMaxBasePeriod + 0.5·(kMinPeriod − kMaxBasePeriod) ≈ 188 s` (`:217-219`,
  `kMaxBasePeriod = kMaxPeriod/√3 ≈ 346.4 s`, `:156-157`) and therefore under one cycle in any test
  render this phase can afford. At `setRate(1.0f)` the base period is exactly `kMinPeriod = 30 s`
  (`:125`) and the three layer periods are `30 / 42.43 / 51.96 s` — `getLayerPeriodSeconds(k) =
  basePeriod · kLayerRatios[k]` with `kLayerRatios = {1, √2, √3}` (`:149-150`, `:226-229`). There is no
  tide-rate setter on `AetherReverb`: pinning it is what makes SC-017 clause 2's threshold derivable
  from the class's own numbers instead of guessed. The modulator's **own** depth is left at its class
  default `kDefaultDepth = 1.0f` (`:144`); `setDimensionalityTideDepth` scales the output on the Aether
  side, so the two are not multiplied.

  The three incommensurate layers (`:132`, `:149-150`) mean the morph never repeats exactly — the
  roadmap's "Nothing is ever static" (line 71) applied to the space itself. Observable by
  `getCurrentMorphPosition()` (SC-017 clauses 2a/2b), which is what makes a stubbed tide detectable. Note the
  time scale: even at the pinned fastest rate the slowest layer is ~52 s, so a *full-range* measurement
  needs a two-minute render — but a **10 s window already resolves a third of a cycle of the 30 s
  layer**, which is why SC-017's always-on core can measure the tide on every build (a 120° arc of a
  sine spans at least half its amplitude, whatever the starting phase).
- **FR-072** — **Per-channel delay jitter** replaces `FDNReverb`'s Gordon-Smith quadrature LFO
  (`effects/fdn_reverb.h:346-352`). One owned `BrownianDrift` (`processors/brownian_drift.h:94`) per
  modulated channel — modulating the **longest half** of the channels, as `FDNReverb` does (`:191-196`) —
  advanced by `processBlock(kControlChunkSamples)` (`:194`), each seeded distinctly via
  `deriveStreamSeed`. Excursion is `setModDepth · 0.5 %` of **that channel's own current length**.
  This deliberately differs from `FDNReverb`, which uses `modDepth · 5 %` of the **longest** line for
  every modulated channel (`:631`): at this phase's Size range the longest line is up to 424 ms
  (FR-012), so 5 % of it is 21 ms of excursion applied to a 5 ms line — nonsense. Per-line and ten times
  smaller is the correction, and the header records the reason.
  `BrownianDrift` rather than a sine because bounded
  organic drift is this instrument's identity (roadmap lines 71–72, 113–114) and because
  `getCurrentValue()` is hard-clamped to [−1, +1] (`:212`), which bounds the excursion structurally.
  **Zeroed during freeze** (FR-033 step 1).

  **Rate control — corrected.** `BrownianDrift` has **no rate setter**. Its complete public API is
  `prepare` (`:121`), `reset` (`:133`), `setSeed` (`:145`), `setSmoothness` (`:152`), `setDepth` (`:159`),
  `setMean` (`:165`), `process` (`:178`), `processBlock` (`:194`), `getCurrentValue` (`:212`),
  `getSourceRange` (`:217`) — verified this session. Its only time-scale control is `setSmoothness`,
  which maps into the mean-reversion time constant `tau = lerp(kTauMin, kTauMax, smoothness)` with
  `kTauMin = 0.2f`, `kTauMax = 30.0f` seconds (`:35-36`, `:97-99`, `:231-234`). The previous revision's
  `setModRate(float) Hz, 0.01 … 2.0` had **no counterpart on the class it delegates to**, and its top
  end was unreachable in any case (2 Hz ⇒ `tau ≈ 1/(2π·2) ≈ 0.08 s`, below `kTauMin = 0.2 s`).
  FR-009 therefore exposes **`setModSmoothness(float)` 0 … 1, default 0.6**, forwarded **verbatim** to
  every channel's `BrownianDrift::setSmoothness`. The mapping is the class's own — no Hz domain is
  invented and none is advertised. The reachable correlation-time range is stated in the header as
  **`tau ∈ [0.2 s, 30 s]`**, i.e. roughly 0.005 … 0.8 Hz of equivalent wander rate, and the default 0.6
  gives `tau ≈ 18 s`.
- **FR-073** — **Determinism.** `setSeed(std::uint32_t)` is audio-thread-callable. It stores the seed and
  re-seeds, via `deriveStreamSeed(seed, salt)` (`core/random.h:102`) with a distinct compile-time salt
  per stream, exactly these: the **spectral-smear stream** (FR-061), **one per modulated channel's
  `BrownianDrift`**, the **`BreathingModulator`** and the **`TidalModulator`**. The **random-orthogonal
  matrix endpoint is not in this list** — it is a `prepare`-time object only (FR-021), because
  regenerating it needs an `O(N³)` Gram–Schmidt over scratch that FR-003 does not allocate for the audio
  thread. The stored seed is used by the *next* `prepare`. Two engines with the same seed, sample rate,
  `PrepareConfig` and control history produce the same render within SC-010's tolerance.
  `deriveStreamSeed`'s non-zero substitution is what keeps `setSeed(0)` from collapsing streams onto one.
- **FR-074** — Life modulators are advanced **even when the input is silent** — the roadmap's "life
  modulators run free even with no notes held (the space engine breathes at idle)" (lines 71–72). The
  engine has no input-activity gate. Measured by **SC-017 clauses 1a and 2a**, whose always-on renders take **silence on both inputs** and
  require the modulator-driven introspection values to move (so a stubbed or input-gated modulator fails
  on every build, not only in the nightly lane), and by SC-017 clause 4, which repeats the `[.slow]`
  grids with signal present and requires the same spreads.

### I. Output stage and introspection

- **FR-080** — `setWidth(float)` applies mid/side width to the wet signal only (`mid ± width·side`), the
  shape `FDNReverb` uses at `:368-371`.
- **FR-081** — `setMix(float)` is an **equal-power** dry/wet blend, `dryGain = cos(mix·π/2)`,
  `wetGain = sin(mix·π/2)` (`:374-377`), computed once per sub-block, not per sample.
- **FR-082** — Input finiteness: every input sample is tested with `detail::isNaN`/`detail::isInf`
  (`core/db_utils.h:54`, `:175`) and replaced with `0.0f` before it can enter the loop — the guard
  `FDNReverb` applies at `:264-265`.
- **FR-083** — **Internal** finiteness: at each control chunk the engine tests a fixed set of loop state
  (per-channel filter states and the current matrix's diagonal) for non-finiteness; on detection it
  invokes `silence()` (FR-007) and increments a counter reported by
  `getNonFiniteRecoveryCount()` (FR-086). This is a last-resort net, not a substitute for FR-025/FR-032's
  structural bounds.
- **FR-084** — `[[nodiscard]] std::size_t getLatencySamples() const noexcept` returns
  `spectralDiffusionEnabled ? diffusionFftSize : 0`. It is constant for a prepared configuration and no
  setter changes it (RA-2). The shimmer taps' latency is **not** included: they live inside the feedback
  loop, and a recirculating path has no dry counterpart to align against. Both the returned value and
  FR-062's dry/wet alignment are measured by **SC-018**; neither was asserted anywhere in the previous
  revision, and a mis-delayed dry path is inaudible in the wet-only measurements every other criterion
  makes.
- **FR-085** — `[[nodiscard]] bool isPrepared() const noexcept`.
- **FR-086** — Introspection accessors, existing so the success criteria are measurable without
  friend-declaring the tests. **Each row names the criterion that would be unmeasurable without it**; an
  accessor with no consumer is not shipped, and a criterion with no accessor is not measurable.

  | Accessor | For |
  |---|---|
  | `[[nodiscard]] float getMatrixOrthogonalityError() const noexcept` | FR-027, SC-004 clause 1 |
  | `[[nodiscard]] bool isFrozen() const noexcept` | FR-037, SC-001, SC-002 |
  | `[[nodiscard]] float getEffectiveDelayLengthSamples(std::size_t channel) const noexcept` | FR-014, FR-034, SC-017 clauses 1a/1b and 3, and SC-003 clause 3(a)'s independent recomputation of the modal density |
  | `[[nodiscard]] float getModalDensityPerHz() const noexcept` | FR-013, SC-003 clause 3, SC-009 |
  | `[[nodiscard]] float getMaxSizeScale() const noexcept` | FR-012 — the **precondition assertion** every Size-sweeping criterion makes |
  | `[[nodiscard]] std::size_t getActiveBloomResonatorCount() const noexcept` | FR-055, SC-016 clause 3 |
  | **`[[nodiscard]] bool isShimmerActive() const noexcept`** — `true` iff the shimmer taps were allocated at `prepare`, i.e. `config.shimmerEnabled && sampleRate >= 44100.0`. | **SC-009's sub-44.1 kHz clause** (FR-003, C-6, RA-6) — the only way to assert that the taps are inert at 8 kHz without inspecting the sample rate, and the accessor Phase 8/9 needs to surface the condition |
  | `[[nodiscard]] std::size_t getNonFiniteRecoveryCount() const noexcept` | FR-083, SC-006, SC-012, SC-014 |
  | `[[nodiscard]] float getCurrentMorphPosition() const noexcept` | FR-023, FR-071, SC-017 clauses 2a/2b |
  | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | FR-084, SC-018 |
  | **`[[nodiscard]] float getStateEnergy() const noexcept`** — the sum of squares of the FDN's **state vector**: per channel, the `m_i = ceil(effectiveDelay_[i])` most recent samples. **AMENDED 2026-07-30** — the original text said "the entire FDN delay-line contents", which is **not** the quantity FR-025's orthogonality invariant conserves and which SC-002 clause 1 cannot be written against. Delay sections are `nextPowerOf2(ceil(ref_i·S_max·1.005) + 4)` (e.g. 32 768 floats for a 20 348-sample line at `S = 4`), so a whole-section sweep carries ~40 % stale history at `S = 4` and ~96 % at `S = 0.25`, and that stale span still holds **pre-freeze** content for the first ~0.7 s after a latch — landing straight inside clause 1's ±0.5 dB window. Per freeze step the network drops the sample at offset `m_i` and adds `‖M·read‖² = ‖read‖²`, so the `m_i` sum is invariant; summing to `sectionSize` drops the sample at offset `sectionSize` and conservation does not follow. The binding definition is `plan.md` §7.15, which the shipped accessor transcribes; under freeze `effectiveDelay_` is latched (FR-034) and the reads are integer (FR-033 step 2), so the summed window is the window the loop recirculates to within the one sample by which `ceil` and `round` can differ (~1e-4 of the total at `N = 8`, three orders inside the bound). **Computed as an on-demand sweep inside the accessor, accumulated in `double`** (Q8) — *not* an incrementally-updated cached scalar. The accessor is diagnostic-only and is never called from `process`; SC-002 samples it once per second, so an `N`-section sweep (≈ 160 k floats at the largest geometry) costs nothing on the audio thread. The sweep is chosen over an incremental accumulator because the incremental form has drift the ±0.5 dB (≈ 6 %) bound does not budget for over ~2.9 M sample updates, and — worse — an accumulator fed from the values the implementation *intends* to write is conserved by construction even while the loop is losing energy, i.e. it can produce false **passes**. | **SC-002 clause 1** — without it the freeze criterion measures a rank-2 projection of a rotating state vector and cannot be derived from losslessness (FR-025) |
  | **`void copyCurrentMatrix(float* dstRowMajor, std::size_t n) const noexcept`** — copies the currently applied `N×N` operator, row-major, into caller-provided storage; no-op if `n != N`. FR-022's geodesic already materialises a dense `M(t)` once per control chunk (Q4), so this is an `O(N²)` copy of the operator the signal path is actually using — no re-derivation, no possibility of the accessor and the audio path disagreeing. It is a `const` diagnostic accessor and is never called from `process`. | **SC-004 clause 1's independent recomputation** — the test must be able to compute `‖MᵀM − I‖_F` itself rather than trusting `getMatrixOrthogonalityError()` |
  | **`void applyCurrentMatrix(const float* in, float* out) const noexcept`** — applies the currently applied operator to a caller-supplied `N`-vector, using the same code path the signal uses. | **SC-004 clause 2's `‖M·x‖₂` over 64 random unit vectors**, and the cross-check that `copyCurrentMatrix` agrees with what is actually applied |

  All are `const noexcept`, allocation-free and **never called from `process`**. Most read a cached value
  or write caller-supplied storage; two are explicitly permitted to do work proportional to the state
  rather than read a cached scalar — `getStateEnergy()` (a full delay-buffer sweep in `double`, Q8) and
  `copyCurrentMatrix` (an `O(N²)` copy) — because both are diagnostic-only. `bloomNoteOn` /
  `bloomNoteOff` (FR-056) are **not** accessors and are specified there, not here.

---

## Success Criteria

Every criterion names its metric, its threshold, and the Catch2 case that measures it. New test files,
all registered in `dsp/tests/CMakeLists.txt`'s `dsp_effects_tests` source list (`:364-389` and the
mirrored list at `:574-593`) — sources are listed explicitly, not globbed, so an unregistered file
silently drops:

- `dsp/tests/unit/effects/aether_reverb_test.cpp` (behaviour, freeze, stability, life modulation
  (SC-017), latency and dry alignment (SC-018))
- `dsp/tests/unit/effects/aether_reverb_matrix_test.cpp` (orthogonality, morph)
- `dsp/tests/unit/effects/aether_reverb_spectral_test.cpp` (echo density, tail smoothness, diffusion,
  shimmer/bloom positive effect (SC-016))
- `dsp/tests/unit/effects/aether_reverb_perf_test.cpp` (SC-008, tagged `[.perf]`)
- `dsp/tests/unit/effects/aether_reverb_nonfinite_test.cpp` (SC-014 — a separate TU because it carries
  `-fno-fast-math -fno-finite-math-only` source properties, which must not be applied to the others)

### SC-0 — Shared definitions (cited by number; **no criterion defines its own input**)

The previous revision had SC-002 sourcing its input from "see SC-003", SC-003 defining no input at all,
and the generator actually specified inside SC-015. All generators, preconditions and the runtime budget
now live here, and every criterion cites **G-n / P-n / B-n** rather than another criterion.

**Pinned input generators.**

- **G-1 — harmonic stack.** Fundamental 220 Hz with partials at 2×…9× at `1/n` amplitude (all sine,
  zero phase), i.e. energy at 220–1980 Hz only, scaled to peak 0.5. Deliberately far from Gaussian, which
  is what makes SC-015's click statistic usable. **Excites the 250 Hz, 500 Hz, 1 kHz and 2 kHz octave
  bands and nothing else** — any criterion measuring per-band behaviour above 2 kHz or below 177 Hz must
  use G-2 instead.
- **G-2 — band-limited noise burst.** White noise band-limited to **80 Hz … 11 kHz** (4th-order
  Butterworth pair), scaled to peak 0.5, generated from `Xorshift32` at a pinned seed so the burst itself
  is reproducible. Excites every octave band SC-002 clause 3 measures.
- **G-3 — unit impulse**, amplitude 1.0 at sample 0, both channels, used for impulse-response criteria
  (SC-003, SC-005). Rendered **wet-only (`setMix(1)`)** so the dry spike is not the global peak.
- **G-4 — single partial.** One sine at `f0 = 220 Hz`, peak 0.5, 2 s, used by SC-016 where a spectrally
  isolated probe is required.
- **G-5 — band-limited white-noise reference.** The same generator as G-2 (80 Hz … 11 kHz, peak 0.5,
  pinned seed) but used as a *direct* signal, never through the engine. It exists so M-1 has a
  measured ceiling to record against.

**Pinned metrics.**

- **M-1 — banded, frame-averaged spectral flatness** (the metric SC-007 clause 1 uses; the shared
  helper is **not** used directly, for two verified reasons). `calculateSpectralFlatness`
  (`tests/test_helpers/signal_metrics.h:326`) (i) picks **one** FFT size capped at 4096 (`:337`) and
  windows only the **first `fftSize` samples** of the buffer handed to it (`:351`) — so "flatness over
  the last 2 s" would in fact measure ~85 ms at 48 kHz, with single-frame variance; and (ii) computes
  `geomMean/arithMean` over **all** non-DC bins (`:397`), so the ~11–24 kHz bins that G-2's 80 Hz–11 kHz
  input never excites dominate the geometric mean and crush the result. M-1 is defined here instead:

  > Split the analysis window into **non-overlapping 4096-sample frames**. Hann-window each frame, take
  > its real FFT, and keep only the bins whose centre frequency lies in **[80 Hz, 11 kHz]** (G-2's
  > excited band; the span is fixed in Hz, so M-1 is sample-rate independent). For each frame compute
  > `exp(mean(ln|X_k|)) / mean(|X_k|)` over those bins. **M-1 is the mean over frames**; the test also
  > records the frame count and the across-frame standard deviation, from which the standard error
  > `SE = stddev/√frames` is formed.

  **Ceiling, recorded so a threshold is never set above it.** M-1 is bounded in `(0, 1]` but its
  attainable maximum is well below 1: for an *ideal* flat spectrum the per-bin magnitudes of a single
  windowed frame are Rayleigh-distributed, giving `geomMean/mean = exp(−γ/2)/√(π/4)·… ≈ 0.845`, and the
  previous revision's absolute "flatness ≥ 0.85" was therefore **above the metric's own ceiling** — a
  criterion no implementation could pass. Every SC-007 run records `M-1(G-5)`, the value of M-1 on the
  band-limited white-noise reference put through the identical analysis, as the empirical ceiling.

**Shared preconditions.** Unless a criterion says otherwise it applies **all** of these, because each
perturbs a quantity some criterion measures more tightly than the default depth allows:

- **P-1 — life modulation off:** `setSizeBreathDepth(0.0f)`, `setDimensionalityTideDepth(0.0f)`,
  `setModDepth(0.0f)`. The FR-009 defaults are 0.2 / 0.2 / 0.25, and FR-070 adds the breath value to Size
  *before* FR-012's mapping — so at defaults every delay length, and therefore
  `getModalDensityPerHz()` and every T60, wanders. SC-003 clause 3's ±1 % and SC-009's ±2 % bounds are
  both **tighter than the default breath depth's effect**, so measuring them with life active would be
  measuring the modulator. SC-017 is the criterion that measures life modulation, and it turns it on.
- **P-2 — geometry reachable:** `config.maxDelaySeconds = 0.5f` and
  `REQUIRE(engine.getMaxSizeScale() == 4.0f)` asserted **before** any Size sweep, so a clamped
  configuration fails loudly instead of quietly measuring a smaller room (FR-012).
- **P-3 — wet-only where the metric is relative:** `setMix(1.0f)` for every impulse-response, tail and
  freeze measurement. The 0.35 default mix would put the dry spike at the global peak and shift every
  peak-relative threshold.
- **P-4 — default order:** `numChannels = 8` — the shipped default and the only order the always-on
  gates measure. `N = 16` ships too (Q3), but appears only in `[.slow]` grids and in SC-008
  configuration (c). Criteria that also exercise `N = 16` say
  so and do it in **one** cross-check configuration, not as a swept dimension (see B-1).

**B — Test-runtime budget (a requirement, not advice).**

- **B-1 — Always-on set ≤ 60 s wall, Release, reference machine.** The non-tagged Aether cases in
  `dsp_effects_tests` must complete within that. `dsp_effects_tests` is CI-blocking on every build
  (`dsp/tests/CMakeLists.txt:363-388`); the previous revision's sweeps specified **> 3800 s of rendered
  audio** in non-tagged cases (SC-002 alone: 5 dimensionality × 3 size × 2 N × 62 s = 1860 s), which at
  the engine's own 5 % budget is minutes of pure DSP before any analysis, and far worse in the
  Debug/ASan/valgrind lanes.
- **B-2 — Full grids are tagged `[.slow]`.** Every criterion below is written as an **always-on core**
  (which always contains at least one configuration at the *full* duration the roadmap states — the 60 s
  freeze, the 180 s shimmer tail — so no roadmap threshold is measured at reduced scope) plus a
  **`[.slow]` full grid**. `[.slow]` cases run in the nightly lane, not on every build. Perf cases stay
  `[.perf]`.
- **B-3 — ASan / valgrind lanes run the always-on core only**, and only at `numChannels = 8` with
  spectral diffusion at 1024. Recorded here so the nightly valgrind lane is not silently handed an hour
  of audio.
- **B-4 — No criterion may be pruned or relaxed at implementation time to fit B-1.** The only admissible
  responses are a shorter *analysis* window, a cheaper metric, or B-5's **pre-decided** demotion order —
  each recorded in `compliance.md`. Never a dropped clause, never a relaxed threshold, never an ad-hoc
  choice about *which* clause to move.
- **B-5 — The arithmetic is done here, not at implementation time.** The previous revision stated a
  60 s budget and an always-on set that did not fit it, and left the discrepancy to be discovered
  during the build. The always-on rendered-audio ledger, with the durations each criterion states:

  | Criterion | Always-on rendered audio | Note |
  |---|---|---|
  | SC-001 | 30 s | worst-case config (`N = 16`, everything on) — the most expensive second-for-second |
  | SC-002 | **124 s** | clause 1 at **one** full-60 s config (`size = 1`, `dimensionality = 1`) + clause 4's full-60 s sends-live config. Clauses 2–3 ride on those same renders. The other three clause-1 configs and clause 5's ten-cycle render move to `[.slow]` |
  | SC-003 | ≈ 22 s | 6 impulse renders ≤ 2 s + 5 density points |
  | SC-004 | ≈ 0 s | matrix-only; no audio is rendered |
  | SC-005 | **83 s** | `{0.5, 4}` s × 2 sizes (10.8 s) + **one** full-duration 60 s config at `size = 0.5` (72 s), which is B-2's required full-scope configuration. The second 60 s config moves to `[.slow]` |
  | SC-006 | **180 s** | one 180 s tail. Kept always-on: it is the roadmap's own stability criterion (line 281) and B-2's other required full-duration configuration. The largest single item in the ledger |
  | SC-007 | ≈ 110 s | 5 amounts × 10 s, clause 3's two references + negative control, clause 4's three sizes |
  | SC-009 | ≈ 28 s | 4 rates × (T60 + impulse) |
  | SC-010 | ≈ 40 s | 3 clauses, short renders |
  | SC-011 | 2 s | |
  | SC-012 | 60 s | |
  | SC-014 | ≈ 20 s | reference + subject |
  | SC-015 | **160 s** | 120 s transition render + a **30 s** no-transition reference render (was 120 s) + the 10 s positive control |
  | SC-016 | ≈ 90 s | 6 s tails (was 8 s), clause 4's frozen tail 15 s (was 30 s) |
  | SC-017 | ≈ 53 s | two 24 s silent renders (FR-070 + FR-071 + FR-074 measured in the **same** renders, since the introspection values do not depend on the input) + clause 3's short render |
  | SC-018 | ≈ 15 s | |
  | **Total** | **≈ 1 020 s of rendered audio** | |

  **Wall-clock estimate.** At the engine's own worst-case real-time factor — SC-008's **5 %** ceiling —
  1 020 s of audio is ≈ **51 s** of DSP; the offline analysis (FFTs, Schroeder integration, NED
  windowing, M-1 framing) is budgeted at a further ≈ 20 %, giving ≈ **61 s**. That sits on the B-1 line,
  and deliberately so: most always-on configurations run with shimmer, bloom and/or spectral diffusion
  **off**, where the measured factor is well below 5 %, so the realistic figure is lower. **The measured
  always-on wall clock is recorded in `compliance.md`.**

  **If it exceeds B-1, the demotion order is fixed here and is not a judgement call at build time:**
  (1) SC-006's tail shortens to 90 s always-on with the 180 s form moving to `[.slow]`;
  (2) SC-002 clause 4 moves to `[.slow]` (clause 1 keeps a full-60 s always-on config);
  (3) SC-005's 60 s configuration moves to `[.slow]`.
  Each step is a *demotion to the nightly lane*, never a deletion — every clause still runs, and B-4's
  prohibition on pruning and relaxation is unaffected.

- **SC-001 — Zero allocation after `prepare`.**
  *Metric:* allocation count inside an `AllocationScope` (`tests/test_helpers/allocation_detector.h:75`).
  *Threshold:* **exactly 0** allocations across 30 s of `processStereoBlock` at the worst case —
  `numChannels = 16`, shimmer on (`PitchMode::Granular`), bloom on, spectral diffusion on at
  `diffusionFftSize = 4096`, with `setSize`, `setDecaySeconds`, `setDimensionality`, `setFreeze(true)`,
  `setFreeze(false)`, `setSeed`, `silence()` and every remaining setter exercised mid-render.
  *Precondition assertion:* `REQUIRE(engine.isFrozen())` observed true at least once and false again, so
  the freeze/unfreeze path — the one that reconfigures read offsets — is inside the scope.
  *Test:* `AetherReverb_NoAllocationAfterPrepare`.

- **SC-002 — Freeze conserves energy: level within ±0.5 dB over 60 s (roadmap line 280).**
  *Preconditions:* P-2, P-3. **Not P-1** — clause 1 deliberately runs the tide (see below).
  *Input:* 2 s of **G-2** (band-limited noise, 80 Hz–11 kHz — chosen over G-1 precisely so clause 3's
  octave bands are excited; G-1's 220–1980 Hz content leaves the 125 Hz, 4 kHz and 8 kHz bands empty and
  turns a dB ratio into a ratio of numerical noise, which FR-036's no-tickle-while-frozen rule makes
  worse by allowing those bands to reach exactly zero). Then `setFreeze(true)`, then 60 s of zero input.
  *Clause 1 — the conserved quantity (primary).*
  *Metric:* `getStateEnergy()` (FR-086), sampled once per second, in dB relative to the first sample
  after the FR-033 latch completes.
  *Threshold:* every sample within **±0.5 dB**, over the full 60 s, with
  `dimensionalityTideDepth = 1` so the matrix is *morphing* throughout — the configuration C-3 shows a
  naive lerp cannot survive. This is the clause FR-025 actually implies: an orthogonal `M(t)` preserves
  the L2 norm of the state vector, and `getStateEnergy()` **is** that norm squared.
  *Always-on core:* **one** configuration at the full 60 s — `size = 1.0`, `dimensionality = 1`, `N = 8`
  (62 s), the largest geometry with the matrix at the random-orthogonal endpoint. This satisfies B-2's
  full-duration requirement; the ledger arithmetic that forces it to be one rather than four is B-5.
  *`[.slow]` grid:* `dimensionality ∈ {0, 0.25, 0.5, 0.75, 1}` × `size ∈ {0, 0.5, 1}` × `N ∈ {8, 16}`
  (both orders ship — Q3), which contains the three demoted core configurations
  (`size ∈ {0.25, 1.0}` × `dimensionality ∈ {0, 1}` less the one kept above).
  *Clause 2 — output-tap level (broadband, derived separately).*
  *Metric:* RMS of 1 s windows of the wet output, in dB, relative to the first full window after the
  latch.
  *Threshold:* **±1.0 dB**, not ±0.5. The extra 0.5 dB is **derived, not conceded**: FR-018's tap is a
  fixed rank-2 projection (even channels → L, odd → R, scaled `2/N`) of a state vector that the
  orthogonal morph continuously rotates, so the projection gain wanders even under exact losslessness.
  The budget is `0.5 dB` (the roadmap's conservation bound, carried by clause 1) `+ 0.5 dB` (tap-projection
  wander). **±1.0 dB is a hard bound.** The measured tap wander at `dimensionalityTideDepth = 1` is
  recorded in the test output and in `compliance.md` **as a figure, not as a threshold**: a measurement
  above ±1.0 dB is a **failure to be fixed in the implementation**, not an occasion to re-derive the
  bound. The previous revision's escape clause ("if it exceeds 0.5 dB the bound is re-derived from the
  measurement") is struck — it is exactly the construction SC-007 clause 3 rejects, and it made the
  clause unfalsifiable, since any wander at all was admissible so long as a derivation was written down.
  *Positive control (the anchor the ±1.0 dB is relative to):* the **same** measurement with
  `dimensionalityTideDepth = 0` must sit inside **±0.5 dB**. That is the always-on configuration; the
  ±1.0 dB form applies only with the tide at 1, and both figures are recorded side by side so the
  0.5 dB of headroom is visibly attributable to the tide and to nothing else.
  *Clause 3 — per octave.*
  *Configuration:* `dimensionalityTideDepth = 0`. A time-varying orthogonal mixer is a modulator: it
  moves energy **across** frequency while conserving the total, so a per-band bound does not follow from
  losslessness while the matrix is morphing. With the tide static the matrix is a fixed orthogonal map
  and the per-band statement is meaningful — and it is still exactly the statement C-4 needs, because a
  freeze that kept its fractional interpolation drains its top octave with the matrix held still.
  *Metric:* the same ±0.5 dB window bound applied independently to octave bands centred at 125, 250,
  500, 1k, 2k, 4k and 8k Hz, **with a noise-floor gate**: a band whose reference-window level is below
  **−80 dBFS** is skipped, and the test asserts that **at least 6 of the 7 bands qualified** (so a
  degenerate input cannot make the clause vacuous). The count of qualifying bands is recorded.
  *Clause 4 — the disable paths are exercised, not assumed.*
  *Configuration:* `setShimmerOctaveSend(1)`, `setShimmerFifthSend(1)`, `setBloomSend(1)`,
  `setBloomDecay(1)`, `spectralDiffusion = 0.5`, `density = 0.7`, **set before `setFreeze(true)`**.
  *Threshold:* clause 1's ±0.5 dB bound on `getStateEnergy()` holds unchanged for 60 s. This is the
  clause that catches FR-033 step 5 being omitted: with the sends live, a pitch-shifted return and a
  resonant bank inside a unity-gain loop are energy sources and the state energy grows without bound.
  The previous revision pinned none of these and therefore measured only the 0.0 defaults — i.e. it
  passed on a configuration no player uses (freeze is a first-class technique, FR-035).
  *Clause 5 — repeatability.* **`[.slow]`** (B-5's ledger).
  Ten enter/leave-freeze cycles in one render, each 5 s frozen, must leave the post-cycle
  `getStateEnergy()` within ±0.5 dB of the pre-cycle value, and must produce 0 click detections
  (SC-015's calibrated detector and config).
  *Test:* `AetherReverb_FreezeEnergyConservation`.

- **SC-003 — No metallic ringing at any size: echo density (roadmap line 281).**
  *Metric:* normalised echo density (NED) as **already implemented for `FDNReverb`** at
  `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` — 1 ms windows, RMS per window, fraction of
  windows whose RMS exceeds `peak · 0.01` (−40 dB) — computed on the mono sum of the impulse response.
  *Preconditions:* P-1, P-2, P-3, P-4. *Input:* **G-3** (unit impulse, wet-only).

  ***The measurement window is derived from the geometry, not fixed.*** A fixed window is arithmetically
  unsatisfiable at the top of the Size range, and the previous revision's move from 50 ms to 250 ms
  rescaled the problem without solving it. The arithmetic, stated so the threshold can be checked rather
  than trusted: at `size = 1`, `S = 4.0` and FR-011's shortest reference line is 20 ms, so **the FDN
  contributes nothing before 80 ms**; the only energy before that is the input diffuser, whose maximum
  delay is `kBaseDelayMs · 4.123 · 1.127 + 2 ms ≈ 16.9 ms` (`processors/diffusion_network.h:248-250`).
  A 250 ms window therefore has 80 unoccupiable windows out of 250 and a **ceiling of
  `(250 − 80)/250 = 0.68`** — below the 0.8 threshold **by construction**, so the criterion could only
  ever fail. At `size = 0.75` (`S = 2.0`) the ceiling is 0.84, which any inter-arrival gap breaks.

  *Window definition (binding):* let `m_short` and `m_long` be the shortest and longest **Size-scaled**
  line lengths in milliseconds (both readable via `getEffectiveDelayLengthSamples`). NED is measured over
  `[t_start, t_start + W]` where
  - `t_start` = the time of the **first wet sample whose magnitude exceeds `peak · 0.01`** (equivalently,
    the first occupied 1 ms window) — every window before onset is **excluded from the denominator**, and
    the excluded count is recorded in the test output;
  - `W = max(250 ms, 3 · m_long)`.

  With that definition the reachable ceiling is 1.0 at every Size, and `W` grows with the geometry so a
  large room is given the same number of round trips to fill as a small one (`3 · m_long` is three passes
  of the slowest line; at `size = 1` that is 1.27 s, at `size = 0.5` the 250 ms floor governs).

  *Threshold — three clauses:*
  1. **NED ≥ 0.8** over the derived window above.
     *Always-on core:* `size ∈ {0, 0.5, 1}` × `dimensionality ∈ {0, 1}`, `N = 8`, `density` at its 0.7
     default.
     *`[.slow]` grid:* `size ∈ {0, 0.25, 0.5, 0.75, 1}` × `dimensionality ∈ {0, 0.5, 1}` ×
     `N ∈ {8, 16}` (both orders ship — Q3).
     The test records, per configuration, `t_start`, `m_short`, `m_long`, `W`, the excluded window count
     and the measured NED — so a future failure is diagnosable as geometry or as density.
  2. **Monotonicity:** NED measured at `density ∈ {0, 0.25, 0.5, 0.75, 1}` (same derived window) is
     non-decreasing (FR-044's negative control: NED at `density = 0` must be strictly lower than at
     `density = 1`).
  3. **Modal density (FR-013) — restated, because the previous form could not fail.**
     `D = (Σᵢ mᵢ)/sampleRate` over the *current* lengths (FR-013) and `S(v)` is a single multiplier on
     every reference length (FR-012), so `D(S) = S·D(1)` is an **algebraic identity** and a 1 % linearity
     bound is satisfied by construction — the only deviation is integer rounding of `mᵢ`, ≈ 0.2 % on the
     shortest line at the smallest configuration and far less on the sum. It therefore provided no
     evidence about the roadmap's "modal density scaling" (line 268). Three sub-clauses replace it:
     (a) **Accessor contract, not algebra:** the test recomputes `D` **independently** from
     `getEffectiveDelayLengthSamples(i)` summed over `i` and divided by the sample rate, and requires
     agreement with `getModalDensityPerHz()` to within **0.5 %** at `size ∈ {0, 0.25, 0.5, 0.75, 1}`.
     This is what catches the real bug class — an accessor reporting the `prepare`-time geometry instead
     of the current, Size-scaled one — which a linearity check on a stale value passes trivially.
     (b) **Range:** `D(size = 1) / D(size = 0)` equals `S_max/S_min = 16` to within **1 %**, with
     `REQUIRE(engine.getMaxSizeScale() == 4.0f)` (P-2) asserted **first** — without it the `S` clamp
     saturates and the ratio is unsatisfiable against a saturating map, which is exactly what the
     previous revision's 0.25 s `maxDelaySeconds` default produced.
     (c) **Audible consequence, measured where it is resolvable:** at `density = 0` (FR-044's sparse
     extreme), the **mean inter-arrival time of the first `kEarlyArrivalCount = 3` onsets** of the
     impulse response scales with `S` to within **15 %** across `size ∈ {0.25, 1.0}`. An onset is an
     above-threshold 1 ms window whose predecessor is below threshold — the spec's unchanged 1 ms bins
     and −40 dB threshold — with each onset's time refined to the peak sample inside its window.
     Additionally, and as the direct check on the mapping, **each of those onsets must land within
     1 ms of the corresponding Size-scaled reference line length** at both sizes.
     Measured at `density = 0.7` this clause would be vacuous — clause 1's NED ≥ 0.8 means nearly every
     window is occupied and the mean inter-arrival saturates at 1 ms regardless of geometry — which is
     why the sparse configuration is pinned. An incorrect Size→geometry mapping breaks this; the
     identity in (a)–(b) does not.

     > **AMENDED 2026-07-30 — "the first 3 onsets" replaces "the impulse response's above-threshold
     > 1 ms windows", because the whole-window form is unsatisfiable by any implementation that also
     > passes clause 1.** The mean gap between occupied 1 ms windows is bounded below by the window
     > width; over the derived NED window it degenerates to exactly `1 ms / NED`. Measured on the
     > shipped engine at `density = 0`: NED 0.956 at `size = 0.25` ⇒ mean **1.04622 ms**, NED 0.535 at
     > `size = 1.0` ⇒ mean **1.86029 ms**, ratio **1.78** against the required 8. The ratio of the two
     > means is *identically* `NED(0.25)/NED(1.0)`, so demanding 8 demands
     > `NED(size = 1, density = 0) ≤ 0.125` — a large room that is 98.5 % empty, i.e. exactly the
     > sparse metallic tail this criterion exists to forbid. This is the same defect SC-003's own
     > preamble identifies for the fixed 250 ms window (the measurement being coarser than the thing
     > measured); the preamble derived the **window** from the geometry and left the **resolution**
     > fixed, and the resolution is the half that binds here.
     > Nothing else moves: same bins, same −40 dB threshold, same `density = 0`, same size pair, same
     > ±15 % tolerance, same expected value (the ratio of the two `S` values recomputed from FR-012).
     > 3 is the largest count at which both configurations resolve the same arrival set at 1 ms.
     > The added per-onset ±1 ms line-length assertion makes the amended clause **strictly stronger**
     > than a ratio alone: it pins each arrival to a named line rather than to an average.
     > Measured under the amended form: **3 ms → 24 ms, ratio 8.0 vs `S` ratio 8.0, 0 % error**; the
     > test also prints the whole-window figures above so the substitution stays visible.
     > Rationale at `aether_reverb_spectral_test.cpp`'s `kEarlyArrivalCount` doc block.

     Also asserted here, unchanged: the reference delay tables are pairwise coprime (FR-011).
  *Test:* `AetherReverb_EchoDensity`.

- **SC-004 — The feedback matrix is orthogonal across the whole morph, not at its endpoints (FR-022, C-3).**
  *Metric:* `getMatrixOrthogonalityError()` (FR-027) = `‖MᵀM − I‖_F`, plus an **independent
  recomputation in the test** from the matrix the engine materialises through
  `copyCurrentMatrix(dst, N)` (FR-086), and an independent application through
  `applyCurrentMatrix(in, out)` (FR-086). Both accessors exist for this criterion: the spec forbids
  friend-declaring tests, so without `copyCurrentMatrix` "the engine's own applied matrix" is not an
  object the test can reach and clauses 1–2 are not measurable. The test additionally asserts that `copyCurrentMatrix`
  and `applyCurrentMatrix` agree (`‖M·x − applyCurrentMatrix(x)‖ ≤ 1e-6`), so the materialised matrix is
  proven to be the one actually applied.
  *Coordinate convention (FR-020):* `t` is the **global** morph position; `u` is the **per-segment**
  blend parameter, `u = 2t` on segment 1 (`t ∈ [0, 0.5]`, Householder → sign-corrected Hadamard) and
  `u = 2t − 1` on segment 2 (`t ∈ [0.5, 1]`, Hadamard → random-orthogonal). The previous revision quoted
  C-3's `u`-coordinate figures as if they were `t`, which made clause 3's assertion **wrong on a correct
  negative control** — in global coordinates `t = 0.5` *is* the Hadamard endpoint, where a naive lerp's
  error is 0.000, and the singular point sits at `t = 0.25`.
  *Threshold — six clauses:*
  1. `≤ 1e-5` at **101 evenly-spaced morph positions** `t = 0.00, 0.01, …, 1.00`, at `N = 8`
     (always-on) and at `N = 16` (`[.slow]`; both orders ship — Q3). Sampling only `{0, 0.5, 1}` is
     explicitly insufficient — those are the three points a naive lerp gets right.
  2. **Loop-gain neutrality:** for 64 random unit input vectors at each of 21 morph positions,
     `‖applyCurrentMatrix(x)‖₂` is within **1e-4** of `‖x‖₂`. This is the physically meaningful statement
     of clause 1 and is what SC-002 clause 1 depends on.
  3. **Negative control (the test must be able to fail), in global coordinates, on both segments:** a
     locally-constructed naive lerp of the same endpoint pairs is measured with the same code and
     **must** exceed the clause-1 threshold, with every measured value recorded. *Segment 1* uses the
     shipped endpoints `A = I − (2/N)J` and `B′ = D·H_N/√N` (FR-020's sign correction), for which the
     figures were computed exactly this session and are asserted as **specific numbers**, not merely
     "greater than tolerance":

     | global `t` | segment `u` | `‖MᵀM−I‖_F`, `N = 8` | `‖MᵀM−I‖_F`, `N = 16` |
     |---|---|---|---|
     | 0.000 | 0.00 | 0.0000 | 0.0000 |
     | 0.0625 | 0.125 | 0.8750 | 1.2374 |
     | 0.125 | 0.25 | 1.5000 | 2.1213 |
     | 0.1875 | 0.375 | 1.8750 | 2.6517 |
     | **0.250** | **0.50** | **2.0000 — σ_min = 0.0000, the blend is SINGULAR** | **2.8284 — singular** |
     | 0.375 | 0.75 | 1.5000 | 2.1213 |
     | 0.500 | 1.00 | 0.0000 | 0.0000 |

     The test additionally asserts `σ_min ≤ 1e-6` and `|det| ≤ 1e-6` at `t = 0.25`, which is the property
     that makes Newton–Schulz recovery impossible (C-8) and therefore the reason FR-022 strikes it.
     *Segment 2* (`t ∈ [0.5, 1]`, Hadamard → random-orthogonal) is **also** covered — it was untested in
     the previous revision. Its endpoint is seed-dependent, so no fixed number is asserted; the
     requirement is that the naive lerp's error at `t = 0.75` exceeds the clause-1 threshold by at least
     four orders of magnitude, with the measured value recorded.
  4. **Endpoint identity (FR-020) — the matrices are the ones the roadmap names.** Clauses 1–2 are
     satisfied identically by *any* three orthogonal matrices, so without this clause an implementation
     that morphs between three arbitrary seeded orthogonal matrices — never building the Householder or
     the Hadamard — passes every criterion in the spec and the roadmap's "2D plate → 3D hall" character
     axis (lines 271–272) ships unverified. Using `copyCurrentMatrix(dst, N)`:
     (a) at `t = 0`, entrywise match to `I − (2/N)J` within **1e-6** (diagonal `0.75`, every off-diagonal
     `−0.25` at `N = 8`);
     (b) at `t = 0.5`, entrywise match to `D·H_N/√N` within **1e-6** (every entry `±1/√N`, with row 0's
     sign flipped relative to the Sylvester construction — FR-020's convention, asserted so a future
     endpoint change cannot silently drop it);
     (c) at `t = 1`, the matrix is orthogonal (clause 1 already), **seed-reproducible** (two engines
     prepared with the same `config.seed` agree within 1e-6) and **seed-sensitive** (two engines with
     different seeds differ by ≥ **0.1** in max-abs entrywise difference).
  5. **Component invariant (FR-020, FR-022, C-8):** `det(copyCurrentMatrix(dst, N))` is within **1e-5**
     of **−1** at all 101 morph positions of clause 1. This is the clause that makes the `O(N)`-component
     correction *measured* rather than asserted in prose: with the previous revision's endpoints the
     determinant runs from −1 to +1 and no continuous orthogonal path exists, so a future change to
     either endpoint's sign reintroduces the discontinuity and this clause is what catches it. The
     negative control is clause 3's lerp, whose determinant passes through 0.
  6. **The `prepare`-time Schur reduction is tested as a component (FR-022, Q4).** FR-022 ships a
     hand-written symmetric-eigen / real-Schur reduction (there is no LAPACK in this repo), and clauses
     1–5 only observe its *product*: a reduction that is wrong in a way the geodesic happens to absorb —
     mis-ordered angles, a `θᵢ` of the wrong sign, an identity block where a rotation belongs — still
     yields orthogonal matrices at every `t` and still hits both endpoints, while traversing a path the
     spec did not specify. Measured directly on the helper, at `N ∈ {8, 16}`, over (i) the two shipped
     endpoint pairs and (ii) at least 32 seeded random `SO(N)` inputs:
     (a) `V` is orthogonal — `‖VᵀV − I‖_F ≤ 1e-6`;
     (b) `B(θ)` is block-diagonal with exact `2×2` rotation blocks — every off-block entry `≤ 1e-6`, and
     every block satisfies `b₀₀ = b₁₁`, `b₀₁ = −b₁₀`, `b₀₀² + b₀₁² = 1` to 1e-6;
     (c) **reconstruction:** `‖V·B(θ)·Vᵀ − R‖_F ≤ 1e-6`;
     (d) **endpoint exactness of the interpolant:** `‖A·V·B(0·θ)·Vᵀ − A‖_F ≤ 1e-6` and
     `‖A·V·B(1·θ)·Vᵀ − B‖_F ≤ 1e-6`;
     (e) **degenerate inputs do not degrade (a)–(d):** repeated eigenvalues, `θᵢ = 0` (identity block)
     and `θᵢ = π` are each exercised explicitly, because those are the cases a hand-written reduction
     gets wrong and the ones a random `SO(N)` draw effectively never produces.
     No audio is rendered by this clause (B-5's ledger is unchanged).
  *Test:* `AetherReverb_MatrixOrthogonality` (clauses 1–5) and `AetherReverb_SchurReduction` (clause 6),
  both in `aether_reverb_matrix_test.cpp`.

- **SC-005 — RT60 accuracy (FR-030).**
  *Preconditions:* P-1 (**mandatory** — the 0.05 Hz breath at its 0.2 default moves the delay lengths
  over renders this long, and T60 is computed from them), P-2, P-3, P-4. *Input:* **G-3**.
  *Metric:* T60 estimated by Schroeder backward integration of the impulse response, wet-only,
  `damping = 0`. The record length is `≥ 1.2 × setDecaySeconds` so the integration has a tail to
  integrate.
  *Threshold:* measured T60 within **±15 %** of `setDecaySeconds`, and **monotone non-decreasing** in
  `setDecaySeconds` at every configuration.
  *Always-on core:* `{0.5, 4}` s × `size ∈ {0.25, 1.0}` × `dimensionality = 0.5` (4 configs, 10.8 s of
  audio) **plus one full-duration 60 s configuration at `size = 0.5`** (72 s) — kept because 60 s is the
  top of RA-4's range and the one where Jot's law is tightest, and because B-2 requires at least one
  full-scope configuration on every build. The *second* 60 s configuration is demoted to `[.slow]` by
  B-5's ledger: two of them cost 144 s of the 1 020 s always-on budget for a duplicated measurement.
  *`[.slow]` grid:* `{0.5, 1, 2, 4, 8, 16, 30, 60}` s × `size ∈ {0.25, 0.5, 1.0}` ×
  `dimensionality ∈ {0, 0.5, 1}`.
  The bound is a **stated tolerance**, not a fudge: Jot's per-line gain law is exact only for an ideal
  lossless matrix and a single-pole damping model, and FR-031's damping-off case is where it is
  tightest.
  *Test:* `AetherReverb_Rt60Accuracy`.

- **SC-006 — Shimmer regeneration is stable at maximum bloom (roadmap line 281).**
  *Preconditions:* P-1, P-2, P-3. *Input:* 5 s of **G-1**, then 175 s of silence.
  *Configuration:* `shimmerOctaveSend = 1`, `shimmerFifthSend = 1`, `bloomSend = 1`, `bloomDecay = 1`,
  `decaySeconds = 60`, `damping = 0`, `density = 1`.
  *Always-on core:* `size = 1.0`, `N = 8`, the full 180 s. *`[.slow]`:* `size ∈ {0.25, 1.0}` × both `N`
  (both orders ship — Q3).
  *Epochs, named once and used by clauses 2–3:* `E1` = the 20 s beginning at the moment input stops;
  `E2` = the 20 s beginning 20 s after input stops; `E_final` = the last 20 s of the render.
  *Threshold — four clauses:*
  1. **Bounded:** peak absolute output over the whole 180 s ≤ **4.0** (12 dB above a full-scale input),
     with the measured peak recorded.
  2. **Monotone decay, against a fixed earlier epoch.** Both peak and RMS of `E_final` must be
     **≤ 0.95 ×** the corresponding value of `E2`, **and** the sequence of per-20-s-window RMS values
     must be non-increasing from `E2` onward. The full measured sequence is recorded in the test output.
     *Why the restatement:* the previous form ("peak of the final 20 s ≤ peak of the 20 s window that
     contained the global peak") is a **tautology** — the window containing the global peak has, by
     definition, the largest peak of any window — so it asserted nothing about growth and left the
     roadmap's stability requirement resting on clause 1's absolute cap alone. `E2` rather than `E1` is
     the reference because the first 20 s after input stops still contains the shimmer's first
     regeneration generations (`max(loop time, tap latency)` per FR-054), which legitimately rise.
  3. **No spectral runaway — as a *fraction*, and against a sends-at-zero reference.** Let
     `HF(epoch)` = (energy above 8 kHz) / (total energy) over that epoch, and let
     `HFref`/`centroidRef` be the same quantities measured on a **reference render of the identical
     configuration with all three sends at 0**. Require
     `[HF(E_final)/HF(E1)] ≤ 1.25 × [HFref(E_final)/HFref(E1)]`, and the same reference-normalised
     form for the spectral centroid. **All four absolute figures and both ratios are recorded**, so
     the unnormalised numbers stay visible.

     > **AMENDED 2026-07-30 — the bound is normalised against a sends-at-zero reference render;
     > the absolute form (`HF(E_final) ≤ 1.25 × HF(E1)`, centroid within 25 %) is unsatisfiable by
     > any implementation that obeys FR-016.** FR-016's in-loop DC blocker is a first-order highpass
     > traversed once per round trip; at this criterion's grid point the energy-weighted traversal
     > rate is `N·sr/Σm_i = 8·48000/81712 = 4.70` per second, and the per-traversal passband droop is
     > 0.0898 dB at 220 Hz, 0.0233 dB at 440 Hz, 0.0061 dB at 1 kHz and 0.0000 dB at 8 kHz. Over the
     > 155 s `E1 → E_final` gap that is **≈ 24 dB of LF loss relative to 8 kHz**, which raises the HF
     > *fraction* by arithmetic alone. It is present **with all three sends at zero**: the measured
     > reference render gives `HFref` **5.3995e-07 → 3.51406e-06 (ratio 6.51)** and centroid
     > **827.62 → 1513.92 Hz (1.829)** on a shimmer-free engine. FR-016's constant is not tunable
     > either — banner item (5d)(i) measures the shimmer collapsing onto a 113 Hz rumble at a 9.5 Hz
     > corner — so no conforming implementation reaches the absolute bound.
     > The normalised form still fails if the shimmer adds HF, which is the clause's entire content,
     > and it is now falsified by a *second render* rather than by a re-derived constant.
     > Measured: subject **4.72507e-07 → 2.02672e-06 (4.29)** and **810.277 → 1284.78 Hz (1.586)** ⇒
     > normalised **0.659** and **0.867**, both ≤ 1.25 — i.e. the shimmer legs measurably *reduce*
     > relative HF growth against the reference. Derivation at `aether_reverb.h` banner item (5d)(ii).
     *Why:* at `decaySeconds = 60` the broadband level falls by many tens of dB over 180 s, so a
     comparison of **absolute** HF energy passes trivially even when the HF *fraction* is exploding —
     which is precisely the failure mode of upward pitch shifting in a loop, and precisely what FR-059's
     HF shelf exists to prevent. The previous absolute form could not detect it.
  4. **Finite:** 0 non-finite samples, and `getNonFiniteRecoveryCount() == 0` (the recovery net must not
     have been needed).
  *Note on scope:* this criterion measures only that shimmer and bloom **fail to explode**. That they
  **do anything at all** is SC-016 — without which an implementation wiring all three sends to zero gain
  passes SC-006 with room to spare.
  *Test:* `AetherReverb_ShimmerRegenerationStability`.

- **SC-007 — Tail smoothness and spectral diffusion (roadmap lines 277, 281).**
  *Metric:* **M-1** (SC-0's banded, frame-averaged spectral flatness) over the last 2 s of a 10 s tail,
  plus L/R correlation.
  *Preconditions:* P-1, P-2, P-3, P-4. *Input:* **G-2** (broadband, so the flatness metric has something
  to flatten).
  *Threshold — five clauses:*
  1. **Monotone smearing, on a metric that is neither saturating nor above its own ceiling.** Across
     `spectralDiffusion ∈ {0, 0.25, 0.5, 0.75, 1}`:
     (a) **M-1 is non-decreasing**;
     (b) the **per-bin spectral peak-to-median ratio in dB** — an unbounded metric — **falls by ≥ 3 dB**
     from amount 0 to amount 1. This is the clause that carries the magnitude of the effect;
     (c) **the increase is statistically real, not a rounding wobble:**
     `M-1(1) − M-1(0) ≥ 3·√(SE₀² + SE₁²)`, where `SEₓ` is M-1's across-frame standard error at that
     amount (SC-0 defines it, and the test records both, the frame count, and the empirical ceiling
     `M-1(G-5)`). This is a **significance** requirement, not a threshold defined by its own
     measurement: a noisier implementation raises the bar rather than lowering it, and a stub — which
     produces a difference of exactly 0 — fails it unconditionally.
     ***Why the previous "flatness ≥ 0.85 in absolute terms" is deleted rather than adjusted.***
     `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:320-325`) is documented as Wiener
     entropy "bounded in [0, 1]", but that is the bound of the *definition*, not of the *estimator*: it
     computes `geomMean/arithMean` over the non-DC bins of a **single** Hann-windowed frame (`:397`),
     and for an ideal flat spectrum single-frame bin magnitudes are Rayleigh-distributed, giving
     **≈ 0.845 — below 0.85**. Reproducing the helper's exact algorithm numerically confirms it: ideal
     white noise → **0.8446**, and the input this criterion actually pins (G-2, band-limited to
     80 Hz–11 kHz at 48 kHz, so the ~11–24 kHz bins are near-empty and crush the geometric mean) →
     **0.2318**; clause 4's `damping = 0.4` tail is lowpassed further still. No implementation could
     pass. M-1 removes both defects — it restricts the analysis to the excited band and averages over
     the whole 2 s window rather than measuring ~85 ms of it — and (c) replaces the impossible absolute
     with a falsifiable one. **No threshold is relaxed by this: the previous absolute was above the
     metric's ceiling, so it measured nothing.**
  2. **Decorrelation:** L/R correlation is non-increasing over the same sweep (FR-061's per-channel
     draws — stated as intended behaviour, so it is measured, not tolerated).
  3. **Transparency at 0 (FR-063), against an a-priori bound.** With `spectralDiffusion = 0`, the wet
     output must match a `diffusionFftSize`-delayed reference render made with
     `spectralDiffusionEnabled = false` to within **per-sample absolute error ≤ 1e-4 and error RMS
     ≤ −70 dBFS relative to the reference render's RMS**. The bound is derived from the COLA property
     FR-060's configuration already guarantees (Hann² at 75 % overlap, `primitives/stft.h:225-228`), not
     from the measurement.
     *Negative control (the clause must be able to fail):* the same comparison run against a
     deliberately mis-configured stage — 50 % overlap, which the `OverlapAdd` header explicitly forbids
     for synthesis-windowed spectral modification (`:225-228`) — **must exceed** both bounds, with the
     measured figures recorded. A threshold defined by its own measurement ("within the measured
     tolerance") is not a threshold, which is what the previous revision shipped and what left FR-063
     unverifiable.
  4. **No isolated modes — stated exactly.** For each 1/3-octave band `b` whose centre lies in
     **[100 Hz, 10 kHz]**, compute the median of the four bands `{b−2, b−1, b+1, b+2}` (the neighbours,
     **excluding** `b` itself) and require `level(b) ≤ median + 9 dB`. Bands whose centre falls outside
     [100 Hz, 10 kHz] are excluded, which also disposes of the edge bands that have no full neighbour
     window; the analysis span is fixed in Hz and therefore identical at every sample rate SC-009
     visits.
     *Configuration (a) — sends off:* `damping = 0.4`, `density = 0.7`, **`bloomSend = 0`** and
     `shimmerOctaveSend = shimmerFifthSend = 0`, at every `size` in SC-003 clause 1's always-on core.
     The bloom configuration is stated because SC-016 clause 3 requires the bloom bank to raise its
     target bands by a measurable margin, which is in direct tension with a 9 dB isolated-mode ceiling —
     the two criteria measure different configurations on purpose.
     *Configuration (b) — shimmer live: the comb-filter check FR-052 owes (new).* The same 1/3-octave
     analysis at `shimmerOctaveSend = shimmerFifthSend = 1`, `bloomSend = 0`, `size = 0.5`, with a
     **notch** bound in place of the peak bound: no band `b` in [100 Hz, 10 kHz] may sit **more than
     9 dB *below*** the median of `{b−2, b−1, b+1, b+2}`. This is the only measurement in the spec where
     a latency-mismatch comb between a shimmer return and the direct path would be visible —
     configuration (a) pins the sends at 0, and SC-006 measures only boundedness and HF fraction — so
     FR-052's corrected argument (the pitch shift itself decorrelates the return; the disjoint
     read/inject subsets buy stereo re-diffusion, **not** latency isolation, since FR-020's dense matrix
     couples every channel to every other in one sample) is **tested here rather than assumed**.
  5. **The diffusion knob does not change the wet level (FR-061).** Wet RMS over the last 2 s of the
     tail varies by **≤ 1.0 dB** across `spectralDiffusion ∈ {0, 0.25, 0.5, 0.75, 1}`, with the five
     measured values recorded. This clause exists because per-frame phase randomisation destroys the
     inter-frame coherence `OverlapAdd`'s fixed COLA factor assumes
     (`primitives/stft.h:243-262`, `:299-307`) and, uncompensated, attenuates the whole wet path by up to
     **6.02 dB** at amount 1 (FR-061's measured table). FR-061's coherence make-up gain `g(a)` is what
     this clause verifies; without the clause the loss ships silently, since clauses 1, 2 and 4 are all
     level-insensitive and clause 3 only exercises amount 0, where the loss is zero.
  *Test:* `AetherReverb_TailSmoothness`.

- **SC-008 — CPU ≤ 5 % of one core, global (roadmap line 282).**
  *Metric:* nanoseconds per 512-sample block at 48 kHz — the reproducible basis established by
  `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp` and reused by
  `continuous_body_perf_test.cpp:104-134` (`kSr48 = 48000.0`, `kBlockSize = 512`,
  `kBlockBudgetNs = (512/48000)·1e9 = 10 666 666.67`, `kRegressionFactor = 1.5`).
  *Reference:* `kBlockBudgetNs · 0.05 = **533 333.33 ns**` per block. Because
  `AetherReverb` is **global** (one instance), this figure does not multiply by polyphony (RA-3).
  *Configurations, each with its own checked-in baseline:*

  | # | configuration |
  |---|---|
  | (a) | `N = 8`, defaults, shimmer off, bloom off, spectral diffusion off |
  | (b) | `N = 8`, defaults, shimmer on (`Granular`), bloom on, spectral diffusion on @1024 — **the shipped default** |
  | (c) | `N = 16`, everything on, spectral diffusion @4096, `size = 1`, `density = 1`, `maxDelaySeconds = 0.5` (P-2), bloom at `kMaxBloomResonators = 32` active resonators — **the worst case, and the one cross-check configuration in which the shipped `N = 16` order is gated at all** (Q3). Its measured ns/block figure is what a later decision to promote `N = 16` to the default is made from, in `compliance.md`, rather than from an estimate |
  | (d) | (b) frozen (`setFreeze(true)` settled) — freeze must not be *more* expensive |
  | (e) | (b) with `dimensionality` swept continuously (matrix recomputed every control chunk) |
  *Threshold:* `REQUIRE(measured ≤ baseline · kRegressionFactor)` per configuration, with
  `static_assert(baseline ≤ reference / kRegressionFactor)` so no baseline can be checked in that would
  admit a measurement above the reference. **Baselines are measurements, not allowances**: each is the
  worst of at least eight consecutive best-of-N runs, padded by at most +5 % for run-to-run spread, with
  machine and trial shape recorded in a BASELINE PROVENANCE block in the TU and the five ns/block figures
  transcribed verbatim into `compliance.md` (RA-3).
  *If a configuration misses:* reduce cost — the lever list in the TU header, in order: SIMD of the
  per-sample `N×N` matrix multiply (FR-024) and of the channel loop, shimmer mode (FR-053),
  spectral-diffusion FFT size, active bloom resonator count, `N`. **The matrix *mechanism* is not a
  lever** — FR-022 pins the real-Schur geodesic (Q4), and its cost is one `O(N³)` product per 64 samples,
  not per sample; swapping it changes the shipped Dimensionality axis and is a spec change, not a
  performance tweak. **Never** raise a baseline, never relax the reference, never renegotiate
  `kRegressionFactor` at implementation time.
  *Test:* `AetherReverb_CpuBudget`, tagged `[.perf]`.

- **SC-009 — Sample-rate independence.**
  *Preconditions:* P-1 (**mandatory** — the ±2 % modal-density bound is far tighter than the default
  breath depth's effect on `S`), P-2, P-3, P-4.
  *Metric:* T60 (SC-005's estimator), NED (SC-003's, with SC-003's **derived** window — the window is in
  milliseconds, so it is sample-rate independent by construction), and the FR-013 modal density, measured
  at **44 100, 48 000, 96 000 and 192 000 Hz**.
  *Scope:* the always-on core measures one configuration (`size = 0.5`, `decaySeconds = 4`,
  `dimensionality = 0.5`) at all four rates; the cross-rate repetition of SC-003's and SC-005's full
  grids is `[.slow]` (B-2).
  *Threshold:* T60 within **±10 %** across rates at the same `setDecaySeconds`; NED ≥ 0.8 at every rate;
  modal density in modes/Hz within **±2 %** across rates at the same Size (delay lengths are scaled from
  a 48 kHz reference and rounded to integers, so exact equality is not achievable and the bound is
  derived from that rounding).
  *Also — the sub-44.1 kHz clause, corrected (N-8, C-6, RA-6).* The previous revision asserted that
  `prepare(8000.0, …)` **clamps to 44 100** and called that correct. It is not: a clamped engine computes
  every Size geometry, Jot per-line gain, damping coefficient, DC-blocker `R`, pre-delay length and
  modulator rate for 44.1 kHz while the host clocks blocks at 8 kHz, so RT60, breath period and
  pre-delay are wrong by the rate ratio — silently, and on a global in-line component every Seraphis
  voice passes through. The criterion now asserts the corrected behaviour:
  `prepare(8000.0, …)` **succeeds** (no clamp toward 44 100); `getLatencySamples()` and
  `getModalDensityPerHz()` are computed **at 8 kHz** (the test derives the expected modal density from
  the shipped reference table at 8 kHz and requires agreement to ±2 %); T60 at
  `setDecaySeconds(4)` measures within ±15 % of 4 s **at 8 kHz**; and
  `REQUIRE(engine.isShimmerActive() == false)` — with `setShimmerOctaveSend(1)` and
  `setShimmerFifthSend(1)` applied, the render must be bit-for-bit within `render_fingerprint.h`
  tolerance of the same render with both sends at 0, proving the taps are inert rather than merely
  unallocated. At 44 100 Hz and above, `isShimmerActive()` must be `true` for the same config.
  *Test:* `AetherReverb_SampleRateIndependence`.

- **SC-010 — Seeded determinism (roadmap line 498: no bit-exact float goldens).**
  *Metric:* `Krate::DSP::TestUtils::fingerprintRender` / `compareFingerprints`
  (`tests/test_helpers/render_fingerprint.h:64`, `:101`) at the helper's own tolerances
  (`kSampleTolerance = 1e-4`, `kMetricTolerance = 1e-5`, `:49`, `:52`).
  *Threshold — three clauses:*
  1. **Positive:** two engines with the same seed, `PrepareConfig` and control history produce
     fingerprints that compare equal.
  2. **Negative control:** two engines differing **only** in `setSeed` produce fingerprints that compare
     **unequal** (a seed that does nothing would otherwise pass). *Configuration, stated because it
     decides what the control actually exercises:* `setSeed` re-seeds only the modulator and smear
     streams — the random-orthogonal matrix endpoint is `prepare`-time only (FR-021, FR-073) — so this
     clause is run **twice**:
     (a) `setSeed` applied *before* `prepare`, at `dimensionality = 1.0` (pure random-orthogonal
     endpoint), which exercises the matrix path;
     (b) `setSeed` applied *after* `prepare`, at `dimensionality = 0.35` (the default), with
     `sizeBreathDepth = 1`, `dimensionalityTideDepth = 1`, `modDepth = 1` and
     `spectralDiffusion = 0.5` — the settings that give the modulator and smear streams authority over
     the render. Without (b)'s explicit configuration the clause could pass or fail on whichever stream
     happened to dominate at the defaults.
  3. **Reset,** with the call sequence spelled out because FR-006 preserves control targets:
     `prepare` → apply control history H → render A → `reset()` → render B (H is **not** re-applied,
     because `reset()` does not clear it) → `compareFingerprints(A, B)` must be equal.
     ***This clause depends on FR-009's smoother-initialisation rule and is unsatisfiable without it.***
     `reset()` snaps every smoother to its preserved target (FR-006), so render B starts settled at H;
     render A starts at the FR-009 defaults and would ramp toward H over up to 300 ms unless H, applied
     before any sample was processed, **snapped**. The first ~300 ms would otherwise differ by the whole
     default→H excursion — far beyond `render_fingerprint.h`'s `kSampleTolerance = 1e-4` and beyond
     `kMetricTolerance = 1e-5` on `rms`/`peak`/`totalVariation` — and no implementation could pass. The
     previous revision specified the two requirements in direct contradiction. Edge case 24 states the
     same dependency. A second
     assertion covers the other direction: `prepare` → H → render A → **`prepare` again with the same
     config** → apply H → render C → C equals A, confirming that `prepare` (not `reset`) is what
     restores defaults.
  **No FNV digest over float sample bits anywhere** — gated by `node tools/lint-float-bit-goldens.js`.
  *Test:* `AetherReverb_SeededDeterminism`.

- **SC-011 — Block-partition invariance (FR-005).**
  *Metric:* sample-wise max absolute difference between one 48 000-sample render and the same render
  split into irregular partitions (`{1, 7, 64, 65, 511, 512, 513, 2048}` repeating).
  *Threshold:* ≤ **1e-6**, at the (b) configuration with all life modulation active — **and with both
  shimmer sends at 1**, so the pitch shifters are inside the measurement.
  *Why it holds structurally, not by luck:* every internal decision is anchored to the absolute sample
  counter — FR-005's control grid, and FR-050's fixed 64-sample shimmer cadence with its one-chunk-late
  injection (Q5). Neither the control chunk boundaries nor the `PitchShiftProcessor::process` call
  boundaries move when the caller re-partitions its blocks, so the shifters' internal grain/phase state
  is partition-independent. A caller-block cadence would have made this criterion a hope.
  *Test:* `AetherReverb_BlockPartitionInvariance`.

- **SC-012 — Output is bounded under adversarial input.**
  *Metric:* peak absolute output and non-finite sample count.
  *Input:* full-scale white noise, then full-scale DC, then a 1 Hz square wave, then silence, 60 s
  total, at `decaySeconds = 60`, `damping = 0`, `size` swept 0 → 1 → 0, `dimensionality` swept, shimmer
  and bloom at maximum.
  *Configuration addendum (P-3):* rendered **wet-only (`setMix(1)`)** — the criterion is a statement
  about the recirculating network, and at the default mix up to 0.876 of the peak budget is spent on a
  dry copy of the full-scale input the criterion is not about.
  *Threshold:* peak ≤ **8.0** (**AMENDED 2026-07-30 from 4.0** — see below), 0 non-finite samples,
  `getNonFiniteRecoveryCount() == 0`, no DC offset above **1e-3** in the final second (FR-016's DC
  blockers), **and — the divergence clause an absolute cap does not test — over the silent final
  quarter of the render the per-5 s output peak must be strictly decreasing, with the last second below
  0.5 × the first.**

  > **AMENDED 2026-07-30 — the peak bound moves from 4.0 to 8.0, and a strictly-decreasing
  > silent-tail clause is added so the criterion is net *stronger*, not weaker.**
  > 4.0 ("12 dB above a full-scale input") is unreachable by any conforming FDN at this criterion's own
  > settings, and the arithmetic is the engine's, not the test's: a lossless-except-Jot FDN driven by
  > white noise reaches stored energy `E = P_in · τ_E` with `τ_E = T60·sr/ln(1e6) = 208 463` samples at
  > `T60 = 60 s`, 48 kHz. FR-015a injects at `sqrt(2/N)` into all `N` lines (`P_in = 2·E[x²]`) and
  > FR-018 taps `N/2` lines at `2/N`, so `E[y²] = 4·τ_E·E[x²]/(N·Σm_i)`. At the peak instant of this
  > render (`t = 3.79 s`, Size 0.126 ⇒ `S = 0.355`, `Σm_i = 4 165`) that is `E[y²] = 8.3`: a wet RMS of
  > 2.9 at equilibrium, 2.2 at the 58 % of equilibrium reached by 3.79 s. A near-Gaussian tail peaks
  > 3–4.5σ above its RMS over 1.8e5 samples ⇒ **6.6–9.9 before any shimmer or bloom**.
  > **Measured decomposition on this exact render** (peak |out|, wet-only): FDN core alone, with
  > shimmer/bloom/spectral disabled at `prepare`, **5.23**; + spectral **5.32**; + shimmer **5.87**;
  > + bloom (the full configuration) **6.19**. The features this criterion sets "at maximum" account
  > for 0.96 of it. At the FR-009 **default** mix the peak is **4.0049** — still over — so no reading
  > of the original threshold holds. `FDNReverb`'s own normalisation is 6 dB *louder* than this engine's
  > (`fdn_reverb.h:337` injects at unity into all 8 lines against `sqrt(2/N) = 0.5` here), so the
  > engine is not the outlier. N-5 forbids true-peak limiting, and no FR asks for the wet level to be
  > normalised against decay time, so there is no mechanism by which a conforming engine is quieter.
  > **8.0 (+18 dBFS) is the analytic 3σ figure rounded up** — derived, not fitted. It leaves 29 %
  > headroom on the measurement while remaining a genuine ceiling: a network that has left stability
  > crosses it within a second (banner item (5f) records a measured 7.0e13 from an unguarded bloom).
  > The added silent-tail clause is what now carries "does not diverge", and it is measured on 5 s
  > windows rather than per second because Size is still sweeping back to 0 after the input stops and
  > shortening the lines legitimately re-concentrates stored energy over a single second (the measured
  > trace ticks 0.26 → 0.27 at 48 → 49 s). Five seconds exceeds the longest round trip at any Size the
  > sweep reaches, so only an expansive loop can lift a window peak. Measured: **0.431246 → 0.164077**.
  > Rationale at `aether_reverb_test.cpp`'s clause comment block.
  > Phase-owner confirmed 2026-07-30.

  *Test:* `AetherReverb_BoundedUnderAdversarialInput`.

- **SC-013 — Portability, layer and lint gates.**
  *Threshold:* all clean, all run before commit —
  `node tools/check-portability.js`; `node tools/lint-layers.js` (FR-002 — the Layer 4 header must not
  include another Layer 4 header, which C-1 and C-2 make a real risk here);
  `node tools/lint-odr.js`; `node tools/lint-float-bit-goldens.js`;
  `node tools/lint-nonfinite-symbols.js` (FR-008);
  `node tools/lint-arch-guarded-includes.js`; `node tools/lint-simd-aligned-loadstore.js` (only if the
  plan adds SIMD to the channel loop under SC-008's lever list);
  `node tools/lint-allocation-operator-overrides.js`. Plus: `dsp_effects_tests`, `dsp_processors_tests`
  and `dsp_systems_tests` all green **unedited** — the whole of RA-1's containment claim.
  *Test:* CI gates, not a Catch2 case; recorded in `compliance.md` with the command output.

- **SC-014 — Non-finite hygiene is exercised, not assumed (FR-082, FR-083).**
  *Preconditions and configuration (pinned — clause 3's thresholds are meaningless without them, and
  the previous revision was the one criterion that cited no `P-n` and stated no settings):* **P-2**,
  **P-3** (`setMix(1.0f)`), `numChannels = 8`, `setDecaySeconds(4.0f)` (the FR-009 default),
  `setDamping(0.4f)` (ditto), `setSize(0.5f)`, `setDimensionality(0.35f)`, all three sends at **0**,
  `setSpectralDiffusion(0.0f)`, life modulation at **P-1**. Clause 3's quantity — how close the
  rebuilt tail is to the time-aligned reference one second after recovery — depends directly on T60
  (0.5 s versus 60 s changes the answer by tens of dB) and on mix (a dry-dominant render converges
  immediately and passes trivially), so both are pinned. The fault-injection time is
  **`t_f = 3.0 s`** into a **10 s** render, i.e. after the tail has reached steady state at the pinned
  4 s T60 and with ≥ 5 s of post-recovery window for the four 100 ms convergence windows plus the
  1.0 s measurement point.
  *Metric:* output finiteness and `getNonFiniteRecoveryCount()`.
  *Input:* NaN and ±Inf injected into the input stream, built **from bit patterns via a volatile sink**
  (never `std::numeric_limits`, which folds under the macOS leg's `-ffast-math`).
  *Threshold — three clauses:*
  1. **No non-finite value ever reaches the output**, at any point in the render.
  2. **`getNonFiniteRecoveryCount()` increments only for the *internal* path** (FR-083), not for the
     input guard (FR-082) — asserted by injecting input NaN/Inf only and requiring the count to stay 0.
  3. **Recovery is defined operationally.** "Returns to normal operation" needs a metric, because
     FR-083's response to *internal* non-finiteness is `silence()`, which zeroes every delay line and
     resonator — after which the tail is rebuilt from scratch and no fixed reference render exists to
     compare against at `t + 200 ms`. The measurable statement: render a **reference** that never
     receives a non-finite sample; render the **subject**, injecting the fault at the pinned
     `t_f = 3.0 s` above and resuming G-1 immediately; time-align the reference to the subject's
     recovery point (the first
     sample after `silence()`'s ramp completes). Require:
     (a) wet-output RMS over the 100 ms window ending at `recovery + 1.0 s` is **non-zero and within
     ±3 dB** of the time-aligned reference window;
     (b) that RMS difference is **monotonically shrinking** over the four 100 ms windows preceding it.
     The measured convergence time — the first window at which (a) holds — is **recorded**, and the
     header states it. The previous revision's "within 200 ms" is dropped as a threshold and kept only as
     a recorded figure, because 200 ms is shorter than the rebuild time of a tail whose T60 is up to
     60 s and was never derived from anything.
  *TU:* `aether_reverb_nonfinite_test.cpp`, carrying `-fno-fast-math -fno-finite-math-only` source
  properties in `dsp/tests/CMakeLists.txt`.
  *Test:* `AetherReverb_NonFiniteHygiene`.

- **SC-015 — No clicks on any transition.**
  *Input (pinned — the metric is relative, so an unpinned input makes the criterion unreproducible):*
  **G-1** (SC-0). *Preconditions:* P-2, P-4; life modulation is left at its **defaults** here, because a
  click detector must see the shipping configuration.
  *Metric:* `Krate::DSP::TestUtils::ClickDetector::detect()`
  (`tests/test_helpers/artifact_detection.h:99`, `:130`) with the config stated verbatim in this spec —
  designated-initialiser form, as `dsp/tests/unit/effects/shimmer_delay_test.cpp:1224-1231` writes it
  (that call site uses `.frameSize = 256, .hopSize = 128, .mergeGap = 3`; this phase uses the longer
  frame below because a reverb tail's transitions are slower than a delay's):
  `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256,
  .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`.
  *Threshold:* **0 detections** over a 120 s render containing, at pinned times: a full `size` sweep
  0 → 1 → 0 over 20 s; a full `dimensionality` sweep 0 → 1 → 0 over 20 s; `setFreeze(true)` and
  `setFreeze(false)` five times; `setDensity` stepped 0 → 1 in one call; `setDecaySeconds` stepped
  0.5 → 60 in one call; `setShimmerOctaveSend` stepped 0 → 1 in one call; `silence()` and resumption.
  *Note on the detector's statistics (why the input is pinned):* `ClickDetector` flags any `|Δy|` above
  `mean + 5·stddev` within each 512-sample frame; on the near-Gaussian output of a dense reverb tail
  that threshold sits near 3.8σ and produces false positives at a rate of ~1e-4 per sample. The pinned
  harmonic input is deliberately far from Gaussian so the statistic is usable. If the measured
  false-positive rate on a **30 s reference render with no transitions** is non-zero, the plan raises
  `detectionThreshold` to the smallest value giving 0 detections on that reference render **and records
  that value and its measured false-positive floor in the header** — it does not relax the 0-detection
  requirement on the transition render.
  ***Calibration is capped and must be proven not to have blinded the detector (new — the previous form
  was unfalsifiable).*** `ClickDetector` flags any `|Δy|` above `mean + detectionThreshold·stddev` per
  frame (`tests/test_helpers/artifact_detection.h:38-45`, `:99-140`), so raising the sigma multiplier far
  enough guarantees 0 detections on the *transition* render too: the "0 detections" threshold would
  survive while the detector's sensitivity had been tuned away, and nothing required the calibrated
  detector to still find a real discontinuity. Two additions close it:
  1. **Cap:** `detectionThreshold` may not exceed **8.0**. Beyond that the failure is fixed in the DSP,
     not in the detector, and the criterion fails.
  2. **Positive control:** after calibration, the *same* `ClickDetectorConfig` must report **≥ 1
     detection** on a 10 s control render into which a deliberate discontinuity has been injected — a
     single-sample step of amplitude **0.1** added to the otherwise-identical reference render at a
     pinned time. If the calibrated detector cannot see that, the calibration is rejected and the
     criterion fails. The measured detection count on the control render is recorded alongside the
     threshold.
  *Test:* `AetherReverb_NoTransitionClicks`.

- **SC-016 — Shimmer and bloom do something: positive-effect criteria (FR-050–FR-058).**
  *Why this exists:* every previously-specified criterion touching shimmer and bloom (SC-006) measured
  only **boundedness**. An implementation that wired all three sends to zero gain passed all of them —
  including SC-006 with room to spare — while delivering none of roadmap lines 273–276. The traceability
  table mapped both roadmap lines to SC-006 alone; it now maps them here as well.
  *Preconditions:* P-1, P-2, P-3, P-4. *Input:* **G-4** (single 220 Hz partial), 2 s, then 6 s of tail
  (B-5's ledger; 4 s of that tail is the analysis window, unchanged).
  *Metric:* energy in ±50-cent bands around named frequencies, and 1/3-octave band levels, each measured
  on the last 4 s of the tail and expressed in dB **relative to the same measurement on an otherwise
  identical render with the send under test at 0** (the *reference* render).

  ***Why a bare "rise in dB versus the reference" is not sufficient, and what replaces it.*** With G-4 as
  input, P-1 zeroing all delay modulation and `setSpectralDiffusion` at its 0 default, the reference
  render is a **linear, time-invariant** network driven by one sine — so its ±50-cent bands at 330 Hz and
  440 Hz hold nothing but numerical and FFT-leakage floor. A dB rise measured against that has a
  near-zero denominator: any granular-pitch-shifter sideband or windowing leakage can exceed +3 dB at the
  *non-target* frequency while the feature is perfectly specific, and conversely the ≥ 12 dB clause
  passes trivially on floor-versus-floor. SC-002 clause 3 gates exactly this hazard with a −80 dBFS band
  gate; SC-016 had none. Clauses 1–2 now carry an equivalent, expressed **relative to a real signal in
  the same render** rather than to full scale, so no absolute dBFS constant has to be guessed. **The
  reference band levels, the send-on band levels and `L(f0)` are recorded in the test output for every
  render**, so a floor-limited comparison is visible rather than inferred.
  *Threshold — four clauses:*
  1. **Octave send is real and specific.** With `shimmerOctaveSend = 1`, `shimmerFifthSend = 0`,
     `bloomSend = 0`, all three of the following must hold, where `L(·)` is a ±50-cent band level in dB
     on the send-on render and `L_ref(·)` the same band on the reference render:
     (a) `L(2·f0) ≥ L_ref(2·f0) + 12 dB` — the effect is present;
     (b) `L(2·f0) ≥ L(f0) − 20 dB` — the target band holds a **real** signal, not an amplified floor.
     `L(f0)` is the fundamental's own band in the *same* render, so the anchor is scale-free and needs no
     dBFS constant; at `send = 1` a +12 copy recirculating in the loop sits far above 20 dB down;
     (c) `L(1.5·f0) ≤ max(L_ref(1.5·f0) + 3 dB, L(2·f0) − 12 dB)` — the non-target band may rise, but
     must stay at least 12 dB below the target. That is the specificity statement, and unlike a bare
     +3 dB bound it does not fail when both sides are floor.
  2. **Fifth send is real and specific:** the exact mirror of clause 1 with the sends exchanged and
     `1.5·f0` / `2·f0` interchanged throughout. Clauses 1 and 2 together are the measurement of FR-051's
     *independent* send levels (roadmap line 276); a single shared gain feeding both taps fails both.
  3. **Bloom reinforces its targets (FR-056, note-informed — Q1).** Configuration: `bloomSend = 1`,
     `bloomDecay = 1`, and `getActiveBloomResonatorCount() > 0` asserted throughout, so a bank that never
     tuned anything cannot pass by leaking broadband gain. Because FR-056 ships the note API, the test
     **imposes** the partial set directly — it does not have to read one back, which is why
     `copyBloomTargetsHz` is not shipped (FR-086's no-accessor-without-a-consumer rule).
     Call `bloomNoteOn(0, {f0, 2f0, 3f0, 4f0}, 4)` before the render. The 1/3-octave bands containing
     those four partials must rise by **≥ 6 dB** versus the `bloomSend = 0` render, while the mean level
     of all non-target bands in [100 Hz, 10 kHz] rises by **≤ 2 dB**.
     *Guard interaction (FR-058, Q7):* the ≥ 6 dB target is measured **under** the shipped guard —
     per-resonator inverse-peak-gain normalisation plus the global `1/√count` scale, with combined loop
     gain ≤ 1.0 at each resonator centre. With four partials held, `count = 4` and `1/√count = 0.5`, and
     the emphasis must still reach 6 dB. **If it does not, the fix is `setBloomSend`'s mapping or the
     normalisation constants — never this threshold** (B-4).
     *Also asserted:* `bloomNoteOff(0)` releases the bank — after the note-off settles, the four target
     bands must fall back to within **2 dB** of the `bloomSend = 0` reference, so a bank that never
     retires its resonators fails.
     *Note the deliberate tension with SC-007 clause 4:* a 6 dB target-band emphasis is compatible with
     that clause's 9 dB isolated-mode ceiling, but only just — which is why SC-007 clause 4(a) pins
     `bloomSend = 0` and this clause pins it at 1. The two measure different configurations on purpose.
  4. **Freeze mutes all three** (the FR-033 step 5 path, from the effect side rather than the energy
     side): repeating clause 1's configuration and then freezing, the ±50-cent band around 2·f0 must
     **stop growing** — its level over the last 5 s of a 15 s frozen tail within ±0.5 dB of its level
     at the moment the latch completed (durations from B-5's ledger). **RA-5** records that this muting
     is a shipped behavioural limitation, not merely a test condition.
  *Test:* `AetherReverb_ShimmerBloomEffect`.

- **SC-017 — Life modulation is measurable, and freeze holds the geometry (FR-070, FR-071, FR-072,
  FR-074, FR-034).**
  *Why this exists:* these five FRs had **no criterion that would fail if they were omitted**. SC-002
  sets `dimensionalityTideDepth = 1` but measures level constancy, which a tide that does nothing
  satisfies *maximally*; SC-011 nominally renders "with all life modulation active" but over 1 s, during
  which `BreathingModulator` at FR-070's pinned 0.05 Hz traverses ~5 % of a cycle and
  `TidalModulator` (30–600 s layer periods, `processors/tidal_modulator.h:125-127`) is effectively
  static — so a stubbed modulator passes. SC-002 clause 4's sends-live check has the same shape: it
  bounds what happens, not that anything happens.
  *Preconditions:* P-2, P-3, P-4.

  ***The always-on core measures the presence of modulation, not only its absence (corrected).*** The
  previous revision tagged this criterion `[.slow]` in its entirety and left the always-on core running
  **clause 3 only** — which is FR-034's *freeze-holds-the-geometry* check, i.e. a measurement that the
  modulators are **not** moving. Combined with B-3 (ASan/valgrind lanes run the always-on core only), an
  implementation that stubbed `BreathingModulator`, `TidalModulator` and the FR-074 idle rule would ship
  green on **every build and every sanitizer lane**, with the roadmap's line 278 and Key Design Decision
  1 (lines 71–72) resting on a nightly job. The long renders were not forced either: FR-070 pins the
  breath at 0.05 Hz, a **20 s** period, so a full cycle costs 24 s of audio, not 120.

  *Threshold — six clauses (1a/1b, 2a/2b, 3, 4). Clauses **1a, 2a and 3 are always-on**; 1b, 2b and 4
  are `[.slow]`.*
  1. **Clause 1a — Size breathes, always-on (FR-070, FR-074 together).** One **24 s** render with
     **silence on both inputs** (the introspection values do not depend on the input, so this single
     render discharges FR-074 as well as FR-070 — see clause 4), at `setSizeBreathDepth(1.0f)`,
     `setSize(0.5f)`, breath rate at FR-070's pinned **0.05 Hz** (20 s period, so 24 s covers a full
     cycle plus 20 % margin), sampling `getEffectiveDelayLengthSamples(0)` **every 100 ms**. Two
     requirements: the peak-to-peak spread is **> 0** and is **≥ 80 %** of the depth-implied excursion;
     and a second 24 s render at `setSizeBreathDepth(0.0f)` is flat to **≤ 1 sample p-p**.
     ***The excursion formula, with the clamp applied (binding — the previous form was ambiguous by a
     factor of ~4).*** FR-070 adds `depth · b` to Size and then clamps the **combined** Size to [0, 1]
     before FR-012's mapping, and `BreathingModulator`'s output range is a fixed bipolar [−1, +1]
     (`processors/breathing_modulator.h:103-104`) that does *not* shrink with depth. Computed **without**
     the clamp at `depth = 1`, `size = 0.5` gives `v ∈ [−0.5, 1.5]` and `S ∈ [0.0156, 16]`, against an
     actually reachable `S ∈ [0.25, 4.0]` — so an 80 % requirement would be unsatisfiable. The expected
     peak-to-peak is therefore
     `S(clamp(size + depth·b_max, 0, 1)) − S(clamp(size + depth·b_min, 0, 1))`
     with `b_max`/`b_min` the modulator's **measured** extremes over the same window, and the clamped
     figure is recorded. ***How the test obtains `b_max`/`b_min`, since `AetherReverb` exposes no breath
     accessor and FR-086 does not ship one (an accessor with no consumer is not shipped):*** the test
     constructs its **own** `BreathingModulator`, `prepare`s it at the same sample rate, applies
     `setSeed(deriveStreamSeed(config.seed, kBreathSalt))` and `setRate(0.05f)` — the exact configuration
     FR-070 and FR-073 pin — and advances it with `processBlock(kControlChunkSamples)` on the same grid,
     recording `getCurrentValue()`. Determinism (FR-073) is what makes the two traces identical, so the
     expected excursion is computed from the modulator's real trajectory rather than from an assumed
     full-scale swing. This also keeps the clause a test of **the Size mapping**, not a re-test of
     `BreathingModulator`'s own output range, which is Phase 1's criterion. Clause 1a runs at `depth = 1`, where the clamp is active and the expected p-p is
     the full `S ∈ [0.25, 4.0]` range; clause 1b repeats it at `setSizeBreathDepth(0.3f)`, where the
     clamp is **inactive**, so the unclamped formula and the clamped one agree and the mapping is
     verified without the clamp masking it.
  2. **Clause 1b — Size breathes, full grid (`[.slow]`).** The 120 s render — **six** breath cycles at FR-070's
     0.05 Hz, not the three the previous revision stated — at `setSizeBreathDepth ∈ {0.3, 1.0}`,
     sampling once per second, with the same requirements and the clamp-inactive check described above.
  3. **Clause 2a — the matrix tides, always-on (FR-071).** Measured on the **same** 24 s silent renders as clause
     1a (the morph position is independent of the breath and of the input), at `setDimensionality(0.5f)`
     and `setDimensionalityTideDepth(1.0f)`, sampling `getCurrentMorphPosition()` every 100 ms over the
     **first 10 s**: peak-to-peak **≥ 0.05** in morph units, and **≤ 1e-6** on the depth-0 render.
     ***Threshold derivation, from the class's own constants at FR-071's pinned rate.*** At
     `TidalModulator::setRate(1.0f)` the base period is `kMinPeriod = 30 s`
     (`processors/tidal_modulator.h:125`, `:202-205`, `:217-219`) and the layer periods are
     `30 / 42.43 / 51.96 s` (`kLayerRatios = {1, √2, √3}`, `:149-150`, `:226-229`). Over 10 s, layer 0
     traverses 120° of phase; a 120° arc of a sine spans at least **half** its amplitude whatever the
     starting phase, and layer 0's amplitude is `kLayerWeight · kSinePairScale · 2 = 1/3` (`:136`,
     `:138`), so layer 0 alone contributes ≥ 0.167 of tide-unit p-p. The two slower layers (42 s, 52 s)
     cannot cancel a 30 s oscillation over that window. At depth 1 that is ≥ 0.167 morph units, so the
     **0.05 threshold carries better than 3× margin** — and a stubbed tide produces exactly 0.
  4. **Clause 2b — the matrix tides, full range (`[.slow]`).** The 120 s render, sampling once per second, at the
     same settings: peak-to-peak **≥ 0.20** in morph units.
     ***The previous justification was inverted and is struck.*** It read "120 s is below
     `TidalModulator`'s 30 s period floor times its slowest layer". At `setRate(1.0f)` the slowest layer
     is `getLayerPeriodSeconds(2) = 51.96 s`, which is **well below** 120 s — the render in fact covers
     2.3 cycles of the slowest layer and 4 of the fastest. It also referred to a `setRate` /
     `getBasePeriodSeconds` pair that `AetherReverb` does not expose: those are `TidalModulator`'s own
     (`:202`, `:217`) on an instance FR-071 makes a **private owned member**, and FR-009's control table
     — declared "pinned here" — contains no tide-rate setter and FR-086 no period accessor. FR-071 now
     **pins the rate at normalized 1.0 in `prepare`** instead, so the periods above are shipped constants
     and both thresholds are derived from them rather than from an unavailable API.
  5. **Clause 3 — freeze holds the geometry (FR-034), always-on, unchanged.** With the engine frozen,
     `setSize(1.0f)` must leave every `getEffectiveDelayLengthSamples(i)` **unchanged** (≤ 1e-6), and
     after `setFreeze(false)` settles they must all move to the `size = 1` geometry (within the same
     1-sample rounding). Also asserted: `setDecaySeconds` and `setDamping` during freeze leave the frozen
     level within SC-002 clause 1's bound. Note that this clause measures the **absence** of modulation,
     which is why it cannot stand as the always-on core on its own.
  6. **Clause 4 — modulators run at idle (FR-074).** Clauses 1a and 2a are **already rendered with silence on both
     inputs**, so the always-on core discharges FR-074 directly: a modulator gated on input activity
     produces zero p-p in both, failing 1a and 2a. The `[.slow]` half repeats the 120 s renders of 1b/2b
     **with G-2 input** and requires the peak-to-peak spreads to match the silent renders within **5 %**
     — i.e. the presence of signal changes nothing. The engine has no input-activity gate, and this is
     the criterion pair that proves it — the roadmap's "the space engine breathes at idle" (line 72).
  *Runtime:* always-on = two 24 s silent renders (≈ 48 s of audio, both discharging clauses 1a, 2a and 4)
  plus clause 3's short render; `[.slow]` = the 120 s grids of 1b, 2b and 4.
  *Test:* `AetherReverb_LifeModulation`.

- **SC-018 — Latency is reported and the dry path is aligned (FR-084, FR-062, RA-2).**
  *Why this exists:* `getLatencySamples()`'s return value and FR-062's dry/wet alignment were asserted
  nowhere. A dry path delayed by the wrong amount is **inaudible in every other criterion**, because
  every criterion that measures the tail runs wet-only (P-3).
  *Threshold — three clauses:*
  1. **Reported value:** `getLatencySamples() == config.diffusionFftSize` when
     `spectralDiffusionEnabled` and **exactly 0** when not, at `diffusionFftSize ∈ {256, 1024, 4096}`.
     **Both cases are kept** (Q2): the flag defaults to `true`, so the first is the shipped default —
     1024 samples, 21.3 ms at 48 kHz — and the second is the zero-latency escape hatch Phase 8/9 keeps.
     Constant across the whole FR-009 control table: every setter is exercised and the value re-read.
  2. **Dry alignment:** with `setMix(0.0f)` (dry only) and spectral diffusion enabled, the
     cross-correlation peak between the output and the input must occur at lag
     **`getLatencySamples()` ± 1 sample**, and the peak correlation must be ≥ 0.999. Repeated with the
     stage disabled, where the expected lag is 0.
  3. **One latency, not two:** with `setMix(0.5f)`, the same measurement against the input must show a
     **single** correlation peak — no secondary peak above 0.2 of the primary at any other lag within
     ±2·`diffusionFftSize`. A dry path that bypassed FR-062's alignment delay produces two.
  *Test:* `AetherReverb_LatencyAndDryAlignment`.

---

## Edge Cases

**RT-safety boundaries**

1. `processStereoBlock` with `numSamples = 0` → no-op, no state change, control grid does not advance.
2. `processStereoBlock` with any null pointer → writes nothing, returns; no partial write.
3. `numSamples` far larger than `config.maxBlockSamples` (e.g. 32 768) → processed in internal slices
   (FR-004); output identical to the same render in `maxBlockSamples` chunks (SC-011 covers the
   general case).
4. In-place call (`inLeft == outLeft`) → identical to the out-of-place render.
5. `process` before `prepare` → no-op, no crash, `isPrepared()` false.
6. `prepare` called twice with different `numChannels`, `diffusionFftSize` and `shimmerEnabled` → full
   reconfiguration, no leak, no stale state; the second render is deterministic from the new config.
7. Every setter called from inside a render loop at every sample boundary → 0 allocations (SC-001),
   0 clicks (SC-015).
8. `silence()` during freeze → the freeze latch is abandoned, state cleared, `isFrozen()` false;
   the engine resumes normally on the next input (FR-007, unlike Phase 5's latching `silence()`).

**Parameter extremes**

9. `setSize(0)` with `numChannels = 16` → the shortest line is ~5 ms; the 16 coprime lengths must remain
   distinct after integer rounding at 44.1 kHz (the worst case for collisions). Asserted in FR-011's
   companion test.
10. `setSize(1)` with `config.maxDelaySeconds` at its **range minimum** (0.05, well below
    `kMinFullSizeDelaySeconds = 0.45`) → `S` is clamped by FR-012 and `getMaxSizeScale()` reports the
    clamped value (`≈ 0.47`); no buffer overrun. This is the *only* place the clamp path is exercised —
    every success criterion asserts `getMaxSizeScale() == 4.0f` first (P-2), so a clamped configuration
    can never masquerade as a measurement.
11. `setDecaySeconds(60)` with `damping(0)` and `size(0)` → the shortest lines have per-line gains
    extremely close to 1; FR-032's clamp must hold and SC-012 must stay bounded.
12. `setDensity(0)` → FR-044's sparse extreme; NED is permitted to fall (SC-003 clause 2 requires it to).
13. `setSpectralDiffusion(1)` with `diffusionFftSize = 256` → maximal smearing at minimum resolution;
    magnitudes are unmodified but the stage is **not** unity-gain (FR-061's coherence loss), so the
    requirement is that FR-061's make-up gain `g(a)` keeps wet RMS inside SC-007 clause 5's ±1.0 dB at
    this FFT size too — the make-up table is fitted at 1024 and this is the configuration that tests
    whether it transfers. Must also pass SC-007 clause 1.
14. All three sends (`shimmerOctave`, `shimmerFifth`, `bloom`) at 1 with `decaySeconds = 60` →
    SC-006's configuration; must stay bounded by FR-058/FR-059 structurally, not by luck.
15. `setMix(0)` → the wet path still runs (so latency and CPU are unchanged) but contributes nothing;
    `setMix(1)` → dry contributes nothing and the FR-062 dry delay is still allocated and advanced.
16. `setDimensionality` swept at maximum rate (0 → 1 in one control chunk) → orthogonality still holds
    (SC-004 samples static positions; SC-015 covers the sweep's audibility).

**Sample-rate changes**

17. `prepare(44100)` → the **shimmer** floor; `PitchShiftProcessor` is at its documented minimum
    (`:138-142`) and `isShimmerActive()` is `true`.
18. `prepare(8000)` → **accepted, not clamped** (N-8, C-6, RA-6). Every rate-derived coefficient is
    computed at 8 kHz; the shimmer taps are force-disabled and `isShimmerActive()` is `false`, so both
    shimmer sends are inert regardless of their control values. Asserted by SC-009, which also asserts
    that the render is bit-identical (within `render_fingerprint.h` tolerance) with the sends at 1 and
    at 0. The previous revision clamped to 44 100 and asserted *that* as correct, which left RT60,
    breath period and pre-delay wrong by the 5.5× rate ratio while reporting success.
19. `prepare(192000)` → the ceiling; the FDN buffer is 4× the 48 kHz size at the same
    `maxDelaySeconds`, and `diffusionFftSize` is unchanged so the diffusion stage's **time** resolution
    quadruples. The header states this; the latency in samples is constant, in milliseconds it is not.
20. Re-`prepare` at a new rate mid-session → all coefficients (Jot gains, damping, DC blocker,
    resonators, modulator rates) recomputed; no stale sample-rate-dependent constant survives.

**Seed determinism**

21. `setSeed(0)` → `deriveStreamSeed`'s non-zero substitution (`core/random.h:102-111`) prevents stream
    collapse; the render differs from `setSeed(1)`'s (SC-010's negative control).
22. Two engines seeded identically but prepared with different `numChannels` → different renders,
    trivially; the determinism claim is per-configuration.
23. `setSeed` mid-render → the random-orthogonal endpoint is **not** regenerated (FR-021, FR-073 — the
    two now agree; the previous revision had FR-021 saying `setSeed` regenerates it and this edge case
    saying it does not). The stored seed applies at the next `prepare`; only the modulator and smear
    streams re-seed, and the change must be click-free (SC-015 does not include it, so FR-073 states the
    audible consequence: a discontinuity in drift, bounded by `BrownianDrift`'s [−1,+1] clamp and the
    100 ms send smoothers). SC-010 clause 2 runs the seed negative control in **both** forms
    (before-`prepare` and after) so each path is exercised at a configuration where it has authority.
24. `reset()` mid-render then re-render → matches the original render within `render_fingerprint.h`'s
    tolerances (SC-010 clause 3). This requires **two** things, not one: FR-006's explicit re-seed
    (rather than relying on each modulator's own `reset()`), **and** FR-009's smoother-initialisation
    rule. Without the latter the first render begins with its smoothers at the FR-009 defaults ramping
    toward the applied history while the post-`reset()` render begins snapped at it (FR-006 preserves
    targets and snaps), so the first ~300 ms differ by the whole default→target excursion and the two
    requirements contradict each other. The previous revision asserted exact equality with no rule
    making it reachable.

**Numerical**

25. 60 s of frozen loop with FTZ/DAZ **disabled** by the caller → denormals in the tail; the engine must
    not produce non-finite output, but the CPU figure is not guaranteed (FR-036 documents the
    expectation; `dsp_test_main.cpp:13` enables FTZ/DAZ for tests).
26. A 60 s frozen tail's accumulated float round-off → SC-002's ±0.5 dB bound is comfortably above the
    random-walk round-off of ~2.9 M single-precision accumulations; recorded so the bound is understood
    as a *design* bound, not a numerical-precision bound. **The measurement adds no drift of its own**:
    `getStateEnergy()` is an on-demand full sweep accumulated in `double` (FR-086, Q8), so the ±0.5 dB
    budget is spent entirely on the loop, not shared with a single-precision running accumulator.

**Harmonic-bloom note API (FR-056)**

27. `bloomNoteOn` with `partialHz == nullptr` or `count == 0` → no-op. With `count >
    kMaxBloomResonators` (32) → clamped, no allocation, no out-of-bounds write.
28. `bloomNoteOn` carrying a non-finite, zero, negative or above-Nyquist frequency → rejected/clamped to
    `[20 Hz, 0.45·sampleRate]` by FR-056 **before** FR-057's coefficient computation, so no non-finite
    coefficient can reach the kernel and `getNonFiniteRecoveryCount()` stays 0.
29. `bloomNoteOn` for a `voiceId` that is already live → replaces that voice's partial set; the bank does
    not accumulate a second copy. `bloomNoteOff` for a `voiceId` that was never noted on → no-op.
30. `bloomNoteOn` while frozen → accepted and stored, but the bloom return is muted for the duration of
    the freeze (FR-033 step 5, RA-5); the emphasis appears when freeze is released.
31. `bloomNoteOn` before `prepare` → no-op (`isPrepared()` false), consistent with edge case 5.

---

## Resolved Questions

**None is open.** All three were decided in the 2026-07-29 clarification session (Q1–Q3 above) and the
decisions are encoded in the requirements; this section records *what* was decided, *against what*, and
what a later phase may revisit. **OQ-A and OQ-B are the roadmap's own** — it defers them to this spec by
name (line 508: *"Aether FDN order (8×8 vs 16×16) and whether spectral diffusion is always-on —
Phase 6"*). **OQ-C was not deferred by the roadmap; it was underdetermined by it** — the roadmap names
the harmonic bloom's effect (lines 274–276) but never says where its partial list comes from, and C-7
shows the named component cannot supply one.

- **OQ-A — FDN order: 8×8 or 16×16 (roadmap line 508). RESOLVED: ship both, `N = 8` default** (Q3).
  `numChannels = 8` is the **default and the only order the always-on gates measure**; `N = 16` ships as
  a `PrepareConfig` option with exactly one cross-check configuration in the gates — SC-008 configuration
  (c), the worst case — plus `[.slow]` coverage in SC-002/SC-003/SC-004. FR-011 ships **both** coprime
  delay tables; FR-026 keeps its meaning; no clause anywhere is conditional any more.

  *Why not `N = 16` by default:* the cost difference is not the matrix (FR-022's geodesic is `O(N³)` once
  per 64 samples, and even the per-sample dense `16×16` multiply is 256 MACs ≈ 12 M MAC/s at 48 kHz,
  small against SC-008's 533 333 ns/block) but the **per-line work** — 16 interpolated delay reads,
  16 damping poles and 16 DC blockers instead of 8, i.e. roughly 2× the core cost. Modal density also
  doubles at the same Size, which serves "no metallic ringing" (SC-003) — but SC-003's NED threshold is
  met at `N = 8` by construction of FR-011's coprime set, so the density gain buys **headroom rather
  than a capability**, and 2× the core cost is not worth spending by default.

  *What a later phase may revisit:* **promotion of `N = 16` to the default**, and only from the measured
  SC-008 (c) ns/block figure transcribed into `compliance.md` — not from this estimate. Nothing else
  reopens.

- **OQ-B — Is spectral diffusion always on (roadmap line 508)? RESOLVED: `prepare`-time flag,
  default `true`** (Q2). `PrepareConfig::spectralDiffusionEnabled` defaults to on, so the shipped default
  reports 1024 samples (21.3 ms at 48 kHz) of latency, which lands on the **whole instrument** because
  the engine is in-line and FR-062 delays the dry path to match (RA-2). That is "always-on" from the
  player's point of view while leaving Phase 8/9 a **zero-latency escape hatch** at `prepare`, at no
  extra gate cost — SC-018 clause 1 measures both cases.

  *Not on the table, then or now:* a **runtime** toggle. A latency that changes while the host is running
  is a click plus a host-reported-latency renegotiation; FR-060 and FR-084 both require latency to be
  constant for a prepared configuration.

  *Coupling removed:* because OQ-C resolved to the note-informed bloom, disabling this stage does **not**
  disable the bloom and no analysis-only STFT is kept (FR-065). The two flags are independent.

- **OQ-C — Where does the harmonic bloom get its partials (underdetermined by the roadmap; C-7)?
  RESOLVED: note-informed, FR-056** (Q1). `bloomNoteOn(voiceId, partialHz, count)` /
  `bloomNoteOff(voiceId)` — the shape `SympatheticResonance` already proves in Ruinae
  (`systems/sympathetic_resonance.h:179`, `:264`), deliberately wider than its 4-partial
  `SympatheticPartialInfo` (`:40`, `:71-74`) so Phase 7 can pass a voice's actual low-order partials.
  It is the roadmap's literal wording ("partials of the held **chord**", line 275), the cheaper and more
  stable of the two mechanisms, and it decouples the bloom from OQ-B.

  *What was rejected:* tail-derived self-tuning (peak-picking from the FR-060 STFT). It would have
  coupled bloom to OQ-B, added a peak-picking / hysteresis / retune-slew stability surface, and required
  `copyBloomTargetsHz` plus a convergence criterion existing only to give a self-tuning bank a testable
  stimulus. Neither ships (FR-056, FR-086).

  *Cost accepted:* a global Layer 4 effect has a note API, and **Phase 7 must forward note events into
  it** — recorded as **RA-7**, since the roadmap does not state that obligation.

---

## Traceability

| Roadmap statement (line) | Covered by |
|---|---|
| "New component (Layer 4, `dsp/include/krate/dsp/effects/aether_reverb.h`)" (264) | FR-001, New-components table |
| "FDN … likely a new 8×8/16×16 network rather than extending the delay-plugin-flavoured one" (265–266) | C-1, FR-010, FR-011 (**both** coprime tables ship), FR-026; OQ-A **resolved**: `N = 8` default, `N = 16` shipped and gated by SC-008 (c) + `[.slow]` |
| "**Size** — delay-line lengths + modal density scaling (cathedral → impossible spaces)" (**268**) | FR-012, FR-013, FR-014; SC-003 clause 3 (with P-2's `getMaxSizeScale() == 4.0f` precondition) |
| "**Density** — diffusion stages engaged (reuse `DiffusionNetwork`)" (**269**) | FR-040–FR-044 (input path only); SC-003 clause 2 |
| "**Decay** — RT60 from 0.5 s to **infinite** (freeze at unity feedback, energy-conserving)" (**270**) | RA-4, **RA-5** (freeze is an exactly-lossless hold: all three sends muted, geometry latched — both forced by line 280's ±0.5 dB bound), FR-030, FR-031, FR-033–FR-037; SC-002, SC-005 |
| "**Dimensionality** — feedback-matrix character morph … Householder ↔ Hadamard ↔ random-orthogonal matrix interpolation" (**271–272**) | C-3, **C-8** (the two named endpoints lie in different components of `O(N)`; all three are pinned to `det = −1`), FR-020–FR-027 (**FR-022 pins the real-Schur geodesic as the one shipped mechanism**); SC-004 (six clauses: orthogonality across 101 positions, loop-gain neutrality, negative control on both segments, **endpoint identity**, **determinant invariant**, **`prepare`-time Schur reduction**) |
| "**Shimmer bloom** (inside the feedback loop, not a post layer): pitch-shifted feedback taps at +12 and +7 (reuse `PitchShiftProcessor`)" (**273–274**) | C-5, FR-050–FR-054 (pinned read/inject subsets and gains; fixed 64-sample cadence, +64 samples of loop time per leg); **SC-016 clauses 1–2 (does it work)**, SC-006 (does it stay bounded), SC-011 (the cadence makes partition invariance structural) |
| "**harmonic bloom** — a resonant emphasis stage that gradually reinforces partials of the held chord (… reuse `sympathetic_resonance_simd` concepts)" (274–276) | C-7, FR-055–FR-059 (`kMaxBloomResonators = 32`; stated guard construction), **OQ-C resolved: note-informed** `bloomNoteOn`/`bloomNoteOff` (**RA-7** — Phase 7 must forward note events); **SC-016 clause 3 (does it work)**, SC-006 (does it stay bounded) |
| "Each layer has independent send level" (276) | FR-051, FR-055 (three independent sends); **SC-016 clauses 1–2 measure the independence** |
| "**Spectral diffusion:** STFT-domain tail smearing for the 'underwater chamber' character" (277) | RA-2, FR-060–FR-065, **OQ-B resolved: `prepare`-time flag, default `true`** (default latency 1024 samples, dry path aligned; no runtime toggle); SC-007 (five clauses, incl. the level clause FR-061's coherence loss makes necessary) |
| "Life-modulated internals: size and matrix slowly breathe via Phase 1 modulators" (**278**) | FR-070–FR-074; **SC-017** |
| "energy conservation in freeze mode (level stays within ±0.5 dB over 60 s)" (280) | SC-002 (five clauses: conserved state energy, output-tap level, per-octave — C-4, sends-live, repeatability) |
| "no metallic ringing at any size (echo-density metric)" (281) | SC-003 (geometry-derived NED window), SC-007 clause 4(a) (no isolated peak) and **clause 4(b)** (no notch, shimmer live — the FR-052 check) |
| "shimmer regeneration stability at max bloom" (281) | SC-006 (monotone-decay and HF-*fraction* clauses) |
| "tail-smoothness spectral tests" (281) | SC-007 |
| "CPU ≤ 5 % global" (282) | RA-3, SC-008 |
| "Global (post-voice-sum), one instance" (**262**) | N-1, FR-004 (stream contract), RA-3 (no ×voices) |
| (implicit) reported latency and dry/wet alignment | RA-2, FR-062, FR-084; **SC-018** |
| **Not roadmap-derived — `setPreDelayMs`** (0…200 ms) | FR-009, FR-015, FR-015a; SC-018 clause 2. **Added because** the stereo pre-delay pair is the stage that feeds `DiffusionNetwork`'s stereo decorrelator (FR-015a) and every in-repo reverb exposes the length (`ReverbParams::preDelayMs`, `effects/reverb.h:153`). Cost: two 200 ms `DelayLine`s at `prepare`. Phase 9 inherits it as a parameter in the 1200–1399 aether range (roadmap lines 380–381) |
| **Not roadmap-derived — `setWidth`** (0…1) | FR-009, FR-080. **Added because** a stereo reverb that is the instrument's only space needs a width control, and FR-018's even/odd tap split makes M/S width one operation on an already-stereo signal. **Phase 10 overlap recorded:** the roadmap assigns *stereo wandering* (`BrownianDrift` → M/S width + azimuth via `midside_processor`) to Phase 10 (lines 448–449). Phase 6 ships the **static** control only; Phase 10 must modulate this setter rather than add a second width stage |
| **Roadmap constraint deliberately *not* inherited — sample-rate range** | N-8, C-6, **RA-6**: the engine keeps `FDNReverb`'s [8000, 192000] (`effects/fdn_reverb.h:13`, `:130`) and force-disables the shimmer taps below 44.1 kHz, rather than clamping the rate. Consequence for Phase 8/9 pluginval and host rates is recorded in RA-6; SC-009 measures it |
| Reuse-inventory row: `fdn_reverb`, `reverb`, `shimmer_delay` (concepts), `diffusion_network`, `pitch_shift_processor`, `stft`, `spectral_buffer` (91) | Existing-components table — each with a verified signature and an explicit reuse-or-not decision (C-1, C-2, C-5) |
| ODR note: "several near-name components already exist" (96–97) | New-components table; `HouseholderMatrix` collision recorded |
| Cross-cutting: RT safety (493) | FR-003, FR-008; SC-001 |
| Cross-cutting: layer discipline (494) | FR-002; SC-013 (`lint-layers.js`) |
| Cross-cutting: ODR sweep (495) | New-components table; SC-013 (`lint-odr.js`) |
| Cross-cutting: CPU budgets are FRs (496–497) | SC-008 |
| Cross-cutting: no bit-exact float goldens (498) | SC-010; SC-013 (`lint-float-bit-goldens.js`) |
| Cross-cutting: portability (499–500) | SC-013 |
| Open Question 3: "Aether FDN order (8×8 vs 16×16) and whether spectral diffusion is always-on — Phase 6" (508) | **OQ-A resolved** (default `N = 8`, `N = 16` shipped as a `PrepareConfig` option gated by SC-008 (c) + `[.slow]`; no clause is conditional) and **OQ-B resolved** (`prepare`-time flag, default `true`) — Clarifications, session 2026-07-29 |
| (implicit) Phase 7 must forward note events into a global effect | **RA-7**, FR-056; SC-016 clause 3 (asserts a non-zero active bloom count, so an unwired forwarding path is visible as a dead bloom) |

---

## Review notes

Recorded per the revision brief: issues resolved in a way that differs from the suggested resolution, and
the one place a stated figure moved.

- **SC-002 clause 2's bound moved from ±0.5 dB to ±1.0 dB — and this is not a relaxation of the
  roadmap's criterion.** Roadmap line 280 asks that "level stays within ±0.5 dB over 60 s" in freeze.
  That ±0.5 dB is now carried by **SC-002 clause 1**, on `getStateEnergy()` — the quantity FR-025's
  orthogonality invariant actually conserves — and by clause 3 per octave. Clause 2's ±1.0 dB applies
  only to the **output-tap projection** and only at `dimensionalityTideDepth = 1`, where a fixed rank-2
  projection of a state vector that the matrix is deliberately rotating provably wanders even under
  exact losslessness (FR-025). At tide depth 0 the output-tap bound is still ±0.5 dB, and that is the
  always-on configuration. The roadmap threshold is measured in more places than before, not fewer.
- **SC-014's "returns to normal operation within 200 ms" was dropped as a threshold and kept as a
  recorded figure.** The review offered this as one of two resolutions; it is taken because 200 ms was
  never derived from anything and is shorter than the rebuild time of a tail whose T60 reaches 60 s
  after FR-083's `silence()`. The clause is now falsifiable (±3 dB against a time-aligned reference,
  monotone convergence), which the previous form was not.
- **OQ-A was resolved provisionally rather than left to the grill stage.** The review offered both
  paths. Resolving it here is the roadmap's own instruction (line 504, "resolve in the relevant spec"),
  and leaving it open while the FRs shipped both orders was the actual defect. Both mechanisms from the
  review's option (ii) are still applied: every dependent FR/SC clause is explicitly marked conditional
  on confirmation, and a stated fallback covers the "`N = 8` only" outcome. Nothing is silently
  pre-empted in either direction. **Superseded by the 2026-07-29 clarification session (Q3):** the
  provisional resolution is confirmed as "ship both, gate one", so the conditional markings are removed
  throughout and FR-011's second delay table ships unconditionally.
- **The `maxDelaySeconds` range floor was left at 0.05, not raised.** The review suggested raising it
  alongside the default. Raising it would delete the only configuration in which FR-012's clamp path and
  `getMaxSizeScale()`'s reporting behaviour can be exercised (Edge case 10). The defect the review
  identified — criteria silently measuring a clamped engine — is instead closed at the measurement end,
  by precondition **P-2**, which every Size-sweeping criterion asserts. The default did move (0.25 →
  0.50 s) as the review required.
- **No issue was rejected** in the first review round. All blockers and majors were applied in full; the
  four minors (Scope item 5 wording, SC-007 clause 4's exact metric, FR-006's reset semantics, FR-020's
  "sparse-coupling") were applied as well, since each removes a real ambiguity from shipped documentation
  or from a test that two implementers would otherwise write two ways.

### Second review round

Also no rejections. Every blocker, major and minor is applied. Four places where the resolution differs
from — or goes beyond — the suggested one, and one place where a stated threshold was deleted:

- **The matrix-component blocker was resolved *more* strongly than suggested, because the suggested fix
  is insufficient on its own.** The review's diagnosis is correct: `I − (2/N)J` has `det = −1` and
  `H_N/√N` has `det = +1`, so no continuous orthogonal path connects them. Its fix — pin all three
  endpoints into the `det = −1` component — is adopted verbatim (FR-020 negates row 0 of the Hadamard,
  FR-021 negates a Gram–Schmidt column when `det(Q) > 0`, and SC-004 clause 5 measures `det = −1` at all
  101 positions; the third site, an odd reflection count in the then-admissible Householder-product
  mechanism, is moot since the 2026-07-29 session (Q4) selected the real-Schur geodesic as the **one**
  shipped mechanism). **But it does not rescue mechanism 2.** Computed exactly this session for the *corrected* pair: `(1−u)A + uB′` is still singular at
  `u = 0.5` (`σ_min = 0.0000`, `‖MᵀM−I‖_F = 2.0000` at `N = 8`, `2.8284` at `N = 16`), because
  `(1−u)A + uB` is singular somewhere in `(0,1)` exactly when `AᵀB` has eigenvalue `−1`, which this pair
  does — and the review's own observation that Newton–Schulz has `σ = 0` as a fixed point then applies
  unchanged. **Lerp + Newton–Schulz is therefore struck entirely** and replaced by a *real-Schur
  geodesic* (`M(u) = A·V·B(u·θ)·Vᵀ`, exactly orthogonal by construction, `O(N³)` once per control
  chunk). Both then-admissible mechanisms were exact-by-construction; none was approximate. This is
  recorded as C-8. **The 2026-07-29 session (Q4) then narrowed it to one:** the geodesic ships, the
  Householder product does not, because its `K`-reflection factorisation leaves the traversed path — and
  therefore the shipped Dimensionality character — underdetermined.
- **SC-007 clause 1(c)'s absolute 0.85 was *deleted*, not lowered, and replaced by a significance test
  rather than the suggested `≥ 0.90 × white-noise reference`.** The review's arithmetic is confirmed:
  the helper's single-frame Rayleigh statistic tops out near 0.845 on ideal white noise, so the threshold
  sat above its metric's ceiling. The suggested relative anchor was **not** adopted, because it would
  reintroduce the same defect in a subtler form: a reverb tail with FR-031 damping active has a tilted
  spectral envelope, whose geometric-to-arithmetic ratio is materially below a flat reference's, so a
  0.90 × ratio is plausibly unreachable too — and this spec has no implementation to measure it against.
  Shipping a second unverified absolute would repeat the mistake. Instead: (i) the metric itself is
  replaced by **M-1** (SC-0), which restricts the analysis to the excited 80 Hz–11 kHz band and averages
  over non-overlapping 4096-sample frames, fixing both the unexcited-bin problem and the review's
  separate minor about the helper reading only ~85 ms of a 2 s window; (ii) the absolute clause becomes
  `M-1(1) − M-1(0) ≥ 3·√(SE₀² + SE₁²)` — a 3σ significance requirement on data the test already has. A
  noisier implementation raises that bar rather than lowering it, so it cannot be gamed, and a stub
  (difference exactly 0) fails unconditionally. The white-noise reference `M-1(G-5)` is recorded as the
  empirical ceiling, as the review asked, but as a **figure**, not a threshold. Clause 1(b)'s unbounded
  peak-to-median fall of ≥ 3 dB continues to carry the magnitude of the effect.
- **SC-017's tide blocker took option (b), not option (a).** The review offered adding
  `setDimensionalityTideRate` / `getTidePeriodSeconds` or pinning the rate. Pinning is taken: FR-071 now
  calls `TidalModulator::setRate(1.0f)` explicitly in `prepare`, giving shipped layer periods of
  30 / 42.43 / 51.96 s, and both SC-017 clause-2 thresholds are derived from those constants. Adding two
  setters would have widened a control surface the review separately (and correctly) criticises for
  carrying two non-roadmap controls already. FR-070's breath rate is pinned the same way (0.05 Hz,
  explicit, not the class default 0.1 Hz), which is also what makes the new always-on clause affordable:
  a 20 s period means one full cycle costs a 24 s render, not the 120 s the review assumed necessary.
- **SC-017's always-on core covers FR-070, FR-071 *and* FR-074 in the same two renders.** The review
  asked for an always-on breath check and a separate always-on idle check. Because
  `getEffectiveDelayLengthSamples` and `getCurrentMorphPosition` do not depend on the input at all, both
  always-on renders are run with **silence on both inputs**, which discharges FR-074 directly rather than
  as a third render — 48 s of always-on audio instead of ~130 s.
- **The two non-roadmap controls (`setPreDelayMs`, `setWidth`) were kept, with the traceability the
  review asked for, rather than dropped.** The review offered both paths. Dropping `setPreDelayMs` would
  not remove the two `DelayLine`s (FR-015a still needs a stereo path into `DiffusionNetwork`), only the
  ability to set their length; dropping `setWidth` would leave the instrument's only space with no width
  control. Both now carry an explicit "not roadmap-derived — added because …" Traceability row and a
  Scope note, and `setWidth`'s row records the Phase 10 overlap so stereo wandering modulates this setter
  instead of adding a second width stage.
- **SC-003 clause 3 was rewritten rather than dropped, and *not* converted to the suggested inter-arrival
  test alone.** The review is right that `D(S) = S·D(1)` is an algebraic identity and the 1 % bound could
  not fail. But its suggested replacement — inter-arrival scaling — saturates at the configuration the
  clause runs in: SC-003 clause 1 requires NED ≥ 0.8 at `density = 0.7`, i.e. nearly every 1 ms window
  occupied, so mean inter-arrival is ~1 ms regardless of geometry. The clause therefore becomes three
  sub-clauses: (a) an **accessor-contract** check that independently recomputes `D` from
  `getEffectiveDelayLengthSamples(i)` (which catches the real bug class — an accessor reporting the
  `prepare`-time geometry rather than the current one — that a linearity check on a stale value passes);
  (b) the 16× range across the Size knob; and (c) the suggested inter-arrival scaling, pinned at
  `density = 0` where it is actually resolvable.
- **SC-002 clause 2 kept ±1.0 dB as a hard bound and deleted the re-derivation escape**, the first of the
  review's two options. No a-priori derivation of the tap-projection wander is offered in its place,
  because deriving it would require the shipped rank-2 projection and the realised per-60 s rotation
  angle — neither of which exists yet — and a derived-but-unverified number would be the same defect in
  a different costume. The measured wander is recorded as a figure; exceeding ±1.0 dB is an
  implementation failure.
- **The test-runtime ledger (B-5) is stated with arithmetic and a pre-decided demotion order.** The
  review's count is confirmed: the previous always-on set was ≈ 1 475 s of rendered audio against a 60 s
  wall-clock budget. Five criteria were demoted *now* rather than at implementation time (SC-002's core
  from four 60 s configs to one plus the sends-live config; SC-002 clause 5; SC-005's second 60 s config;
  SC-015's reference render from 120 s to 30 s; SC-016's tails from 8 s/30 s to 6 s/15 s), bringing the
  ledger to ≈ 1 020 s ≈ 61 s wall clock at the worst-case 5 % real-time factor. SC-006's 180 s tail is
  **kept** always-on against the review's suggestion, because it is the roadmap's own stability criterion
  and B-2 requires a full-duration configuration; if the measured wall clock misses B-1, B-5's fixed
  order demotes it first. Every demotion is to the nightly lane — no clause is deleted and no threshold
  moves, so B-4 is intact.
