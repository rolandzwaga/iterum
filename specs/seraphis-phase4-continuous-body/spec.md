# Feature Specification: Seraphis Phase 4 — Continuous Resonant Body

**Spec slug:** `seraphis-phase4-continuous-body`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 4 (lines 198–222)
**Layers:** one new Layer 3 component (`ContinuousBody`), plus one strictly-additive amendment to an
existing Layer 2 component (`WaveguideString`)
**Depends on:** nothing in Phases 1–3 at compile time. The roadmap graph places Phase 4 after Phase 1
(line 470); in practice `ContinuousBody` instantiates no life modulator (N-2) and exposes modulation-safe
setters (FR-006) instead, so it consumes no Phase 1 header — the only Phase 1 borrowing is A-5 copying a
*value* (the 64/32-sample control cadence). Phases 2/3/5/6 are independent (roadmap lines 479–481);
Phase 4 consumes an audio stream, not a `HarmonicCloud` instance.
**Plugin work:** none. KrateDSP only, unit-tested. The Seraphis plugin starts at Phase 8.

---

## Overview

Phase 4 delivers the *body* the harmonic cloud speaks through: a **continuous** resonator — glass, string,
metal plate, chamber, ice — driven by a sustained input rather than struck by an impulse (roadmap lines
201–202). Membrum's resonator infrastructure already models struck bodies well; the genuinely new DSP work
here is the **continuous-excitation adapter**: energy normalization (input RMS tracking → resonator drive
compensation) and feedback-safe damping floors, so a permanently-driven high-Q bank never runs away
(roadmap lines 208–211). Around that sit three reused engines behind a crossfading material selector
(roadmap lines 206–208), a post-resonator **slow decay cloud** built on `DiffusionNetwork` with decay times
reaching 30 s (roadmap lines 212–213), and **key tracking** that lets the body sit anywhere between "fixed
body like a real instrument" and "fully tracked" (roadmap lines 214–215).

The single new component is `ContinuousBody` (Layer 3,
`dsp/include/krate/dsp/systems/continuous_body.h`), matching the roadmap's own header path (line 204).

This phase ships reusable, unit-tested, RT-safe DSP. It does **not** ship a voice, a plugin surface, or any
wiring between `HarmonicCloud` and `ContinuousBody` — that is Phase 7 (roadmap lines 283–287).

Every claim below about existing code was verified by opening the header in the session that produced this
document; each cites `file:line`. Where the roadmap names a component that does not exist under that name,
the discrepancy is recorded in **Roadmap-vs-Reality Corrections** rather than silently papered over.

---

## Clarifications

### Session 2026-07-27

- **Q1 — When `setResonatorBypass(true)` is engaged, does FR-033's `1/Ĝ` term still apply to the signal that reaches the decay cloud?** → **No.** FR-033 splits into an **engine drive** (carrying `1/Ĝ`) and a **cloud drive** (`rmsGain · userDrive` only): `Ĝ` corrects the *resonator's* own steady-state gain and has no referent when the resonator is bypassed. SC-005's cloud line item and SC-008's RT60 therefore measure a normal-level signal, and Phase 7's halo mode is usable without external makeup gain.
- **Q2 — How does `driveGain` move when `Ĝ` changes by orders of magnitude?** → **Per-slot `driveGain`, snapped at material assignment** (legal: the incoming slot is at zero crossfade gain when assigned, FR-024), **plus log10-domain smoothing for continuous moves** (glide, resonance, AGC, `setDrive`) at `kDriveSmoothMs` — a constant dB/ms slope, scale-invariant across the 3–5 decade `Ĝ` span.
- **Q3 — Is silencing the active engine the right response to a non-finite input chunk?** → **No; the policy splits in two.** A poisoned **input** chunk is substituted with zeros and the ring is **preserved** (no silence). `silence()` — over `kSlotReleaseMs`, with a ramped re-entry — fires **only** when `stateFinite()` shows the engine's **own state** went non-finite. SC-013 is restated accordingly; the clickless guarantee stays intact.
- **Q4 — Ranges, defaults, smoothing times, and the state of a freshly-`prepare()`d body?** → **Pin the full control table in this spec now** (FR-009): range, default and smoothing ms for every setter, plus the prepared state — material Glass, resonance 0.7, damping 0.0, keyTracking 1.0, noteHz 220, drive 1.0, mix 1.0, cloudMix 0.25, cloudDecaySec 4.0, cloudSize 1.0, cloudDamping 0.3, width 1.0, AGC on, resonator bypass off, seed 1. Phases 7 and 9 consume this table verbatim.
- **Q5 — Do smoothed Resonance/Damping values force a full `updateModes()` on every control step?** → **No.** Resonance and damping are applied **at the control step with no extra setter smoother** (the modal bank's own 2 ms coefficient smoother supplies continuity), behind a **relative** dirty gate `kDampingEpsilonRel = 0.005` (0.5 %) on **both** `b1` and `b3` — the same lever as FR-042's pitch epsilon. `updateModes()`'s transcendentals fire only on real movement.
- **Q6 — How is a control chunk that straddles a `processStereoBlock` boundary handled?** → **Full carry.** `Σx²` and the sample count accumulate across calls; every control step (follower advance, `Ĝ`, key-track retune, crossfade advance, cloud loop) fires only on the **absolute** 64-sample grid; engines and cloud are advanced in sub-chunks up to the boundary. Zero added latency; SC-011's odd-partition clauses hold as written.
- **Q7 — What is the operational definition of "steady state" for SC-007 and SC-015?** → **Defined once, self-sizing:** render for `max(5 s, 3 × getEngineT60Sec())` and take the **mean peak over the final 1 s**. Both criteria use this definition, and SC-007's absolute-level sanity clause (−20 dB … +3 dB of `kTargetPeak`) stays meaningful.
- **Q8 — Which material does each checked-in CPU baseline gate?** → **Four baselines** (one per configuration), each pinned to the **most expensive material's** measured number, with **every** material `REQUIRE`d against it. The existing per-material 0.7× spread clause supplies per-engine discrimination. Never raise a baseline to pass — levers first.
- **OQ-1 — Material → engine assignment.** → **One engine per material:** Glass / MetalPlate / Ice → modal bank, Strings → waveguide, Chamber → comb bank. No layered materials in Phase 4.
- **OQ-2 — Mode count.** → **Fixed at 32 modes** per modal material (a ceiling, truncated by FR-043's Nyquist prefix rule). **No quality tiers.**
- **OQ-3 — Decay-cloud placement.** → **Per-voice, inside `ContinuousBody`,** as the roadmap places it. CPU is measured against the 1 %/voice budget; RA-2's Phase 7 budget-reconciliation flag stands.
- **OQ-4 — `setMix`.** → **Kept.** The body owns its own dry/wet blend; FR-060 stays.
- **OQ-5 — Is RA-1's `retune()` the right shape?** → **Yes** — a strictly-additive `WaveguideString::retune(float)` on the shared component, inert unless called. **Cross-plugin impact is explicit:** `waveguide_string.h` is shared DSP, the plan must flag it, SC-014 regression-guards existing behaviour, and the build stage must run the suites of **every** consumer (verified by grep this session: `dsp_processors_tests`, `innexus_tests`, `membrum_tests`).

---

## Roadmap-vs-Reality Corrections

The roadmap's Phase 4 reuse list (line 89 and lines 206–213) names five things that do not exist under the
stated name or do not have the stated capability (C-1 … C-5), and two components behave opposite to what
their names imply (C-6, C-7). All seven were verified by reading the headers.

| # | Roadmap says | Reality (verified) | Consequence for this spec |
|---|---|---|---|
| **C-1** | "Composes existing `ModalResonatorBankSimd`" (line 206) | There is **no class** `ModalResonatorBankSimd`. The class is `ModalResonatorBank` (`processors/modal_resonator_bank.h:71`, `: public IResonator`, `kMaxModes = 96` at `:73`). `modal_resonator_bank_simd.h` declares exactly one **free function**, `processModalBankSampleSIMD(float*, float*, const float*, const float*, const float*, float, int)` (`:36-43`), which `ModalResonatorBank::processBlock` calls internally (`modal_resonator_bank.h:361-364`). | FR-021 composes `ModalResonatorBank`. The SIMD kernel comes along for free; `ContinuousBody` never calls it directly. |
| **C-2** | "`TimevarCombBank`" (lines 89, 207) | The header is `systems/timevar_comb_bank.h`; the **class** is `TimeVaryingCombBank` (`:81`), `kMaxCombs = 8` (`:87`). It is a **Layer 3** component. | FR-023 composes `TimeVaryingCombBank`. A Layer 3 component may include another Layer 3 header only if that does not create a cycle; FR-003 pins the rule. |
| **C-3** | "post-resonator diffusion stage (reuse `DiffusionNetwork`) with decay times up to 30 s" (lines 212–213) | `DiffusionNetwork` (`processors/diffusion_network.h:161`) is a **pure feed-forward** 8-stage Schroeder allpass cascade run in **series** per sample (`:351-383`). Its `process()` (`:327-403`) contains **no feedback path**, so it has no decay at all. Two distinct delay figures matter and must not be conflated: (i) each stage's *own buffer* is sized for `kBaseDelayMs (3.2) × kDelayRatiosL[7] (4.123) × kStereoOffset (1.127) + kMaxModDepthMs (2.0)` ≈ **16.9 ms** (`:202-205`) — this is the network's internal allocation, not its throughput delay; (ii) the **cascade's total group delay** is the sum over stages, `kBaseDelayMs × size × Σ kDelayRatiosL (= 17.777)` = **56.9 ms** on L and `× kStereoOffset` = **64.1 ms** on R at `size = 100 %` (`:366-374`). (A Schroeder allpass of delay `D` has mean group delay exactly `D`, so the cascade's mean delay is the sum of its stage delays.) | FR-050 wraps `DiffusionNetwork` in an explicit stereo feedback loop with a damping filter, and derives the loop gain from the requested RT60 **using the full loop time, cascade included** (FR-052) — the figure in (ii), not (i). The diffusion network is reused as the *smearing* element, exactly as the roadmap intends; the *decay* is the new loop. |
| **C-6** | (implicit) `IResonator::setBrightness` raises brightness | Verified inverted for `WaveguideString`: `setBrightness(b)` stores `brightness_` (`waveguide_string.h:148-152`), `process()` maps `S = brightness · 0.5` (`:168`) into the loss filter `rho·[(1−S)x + S·x[n−1]]` (`:197`) whose magnitude is `rho·√((1−S)² + 2S(1−S)cos ω + S²)` (`:379-382`) — flat at `S = 0` and a null at Nyquist at `S = 0.5`. **A larger `setBrightness` argument therefore darkens the string.** | FR-036 specifies the Strings damping mapping in terms of `S` and states the call as `setBrightness(2·S)`, so the inversion is written down once instead of being rediscovered as a bug. |
| **C-7** | (implicit) `DiffusionNetwork` is cheap enough to sit in a per-sample loop | `std::sin(lfoPhase_ + stagePhaseOffset)` is evaluated **inside the per-stage, per-sample loop** (`diffusion_network.h:362`) unconditionally on `modDepth`, and `kDefaultDensity = 100.0f` (`:173`) keeps all 8 stages enabled (`:353-356`). That is 8 transcendental calls per sample per instance — ~384 k/s at 48 kHz — which alone is a large fraction of the SC-005 budget. | RA-4: a bit-identical guard hoists the `std::sin` out when `modDepth == 0`, and FR-050 defaults the cloud to `modDepth = 0`. FR-050 additionally evaluates the loop on the 64-sample control chunk rather than per sample (legal because the shortest loop is 37 ms ≫ 64 samples), so `DiffusionNetwork::process` is called with `numSamples = 64`, not 1. |
| **C-4** | "retune smoothness under glide" applies to all materials (line 220) | `WaveguideString` (`processors/waveguide_string.h:38`) has **no continuous retune path**. `bridgeDelayFloat_` — the loop length that sets pitch — is assigned in exactly two places: `silence()` (`:243`) and `noteOn()` (`:325`). `setFrequency()` (`:134-140`) only re-targets `frequencySmoother_`, and `process()` (`:154-218`) uses the smoothed frequency **only** for the loss filter (`rho`, `S` at `:167-168`) — never to update the delay length. Calling `noteOn()` to retune would reset the loop and re-inject a noise burst (`:355-448`). | RA-1 (below): a strictly-additive `retune()` surface is added to `WaveguideString`. FR-080 series. |
| **C-5** | `iresonator`, `body_resonance`, `sympathetic_resonance[_simd]`, `feedback_network` listed as Phase-4 reuse (line 89) | All exist and were read. `IResonator` (`processors/iresonator.h:32-72`) is the correct abstraction for the modal/waveguide engines and both already implement it. `BodyResonance` (`processors/body_resonance.h:122`) is a fixed 3-preset violin/guitar/cello **colouration** stage with an 8-mode biquad bank (`:45`, `:74-113`) — it models an instrument corpus, not a playable tuned body, and has no tuning input. `SympatheticResonance` (`systems/sympathetic_resonance.h:96`) is a note-pool driven by `noteOn(int32_t voiceId, const SympatheticPartialInfo&)` (`:179`) — it is a *coupling* device between voices, which is Phase 6 shimmer/bloom territory (roadmap lines 265–268). `FeedbackNetwork` (`systems/feedback_network.h:57`) is a delay-flavoured stereo feedback with `prepare(double, size_t, float maxDelayMs)` (`:87`) and cross-feed. | FR-021 uses `IResonator` as the engine-slot *type*, but restricts the contract to `prepare`/`silence` (plus `process` for the waveguide) — its `process`, `getControlEnergy` and `getPerceptualEnergy` are wrong for `ModalResonatorBank` on the performant path; see FR-021. `BodyResonance`, `SympatheticResonance` and `FeedbackNetwork` are **NOT** used — see Non-Goals N-7, with the reason recorded so a later reviewer does not read the omission as an oversight. |

---

## Recorded Roadmap Amendments

Consequences of decisions taken here that touch shipped code or shipped budgets. Recorded so a later phase
does not inherit a silent contradiction. None is licence to relax a Phase 4 threshold.

### RA-1 — `WaveguideString` gains a continuous-retune surface

**What:** a new public method on `WaveguideString` (`processors/waveguide_string.h:38`) that recomputes the
loop length from a new fundamental **without** clearing loop state and **without** re-injecting excitation,
so the Strings material can glide. Specified in the FR-080 series.

**Why it is an amendment:** `WaveguideString` ships from spec `129-waveguide-string-resonance` (header
banner, `:10`) and is consumed by Innexus. Phase 4 extends a component outside its own phase, the same
pattern Phase 3 used for `HarmonicCloud::setSpectralTarget`.

**Containment:** the addition is inert unless called (FR-084) and no existing member changes behaviour, so
every consumer's test suite must pass unedited (SC-014).

**Cross-plugin impact (Q/OQ-5, explicit).** `waveguide_string.h` is **shared DSP**, not a Seraphis-local
file. Its consumers, enumerated by `grep -rn "waveguide_string.h" dsp/ plugins/ tools/` this session, are:

| Consumer | Site | Suite that must be run |
|---|---|---|
| Layer 2 unit tests | `dsp/tests/unit/processors/waveguide_string_test.cpp:10`, `waveguide_string_dc_blocker_test.cpp:7`, `bow_waveguide_coupling_test.cpp:10` | `dsp_processors_tests` |
| Innexus | `plugins/innexus/src/processor/innexus_voice.h:24`, `plugins/innexus/tests/unit/processor/waveguide_integration_test.cpp:10`, `tests/unit/vst/waveguide_param_test.cpp` | `innexus_tests` |
| **Membrum** | `plugins/membrum/src/dsp/drum_voice.h:41`, `plugins/membrum/src/dsp/bodies/string_body.h:22` | **`membrum_tests`** |
| `tools/membrum_preset_generator.cpp` | offline tool, links the same header | builds as part of the tools target |

The plan **must** carry this table forward as an explicit risk item, and the build stage **must** run every
suite in the right-hand column — `membrum_tests` in particular, which the earlier draft of SC-014 omitted.

**Roadmap action:** record under Phase 4 that `WaveguideString` gained `retune(float)` in this phase.

### RA-2 — the Phase 4 per-voice budget feeds an already-broken Phase 7 tally

Roadmap line 221 budgets this component at "CPU ≤ 1% per voice with body active". Phase 3's spec recorded
(as its own RA-2) that `16 × (0.5% + 1% + 1% + 0.15%) + 5% global = 47.4%` against the roadmap's 25%
full-poly ceiling (line 301) — a 1.9× overage.

**Verified this session:** `grep -n "Amendments applied by Phase 3" specs/Seraphis-roadmap.md` returns
**nothing**, and `specs/Seraphis-roadmap.md` is 502 lines with Phase 4 at lines 198–222 — i.e. the Phase 3
spec's "APPLIED 2026-07-27" disposition table does **not** match the roadmap file on disk. The Phase 7
contradiction is therefore **still unrecorded in the roadmap**.

**Phase 4 does not resolve this and does not pretend to.** SC-005 holds this component to the 1% figure as
written. **Roadmap action:** before Phase 7 is specced, apply Phase 3's RA-2 (or re-derive the budgets).

### RA-3 — `DiffusionNetwork` cannot supply the decay the roadmap credits it with

See correction C-3. Roadmap lines 212–213 imply the 30 s decay comes from `DiffusionNetwork`. It does not
and cannot; the decay is a new feedback loop specified in FR-050. **Roadmap action:** amend line 212 to
read "post-resonator diffusion **loop** (`DiffusionNetwork` inside a damped feedback path)".

### RA-4 — `DiffusionNetwork` gains a zero-modulation fast path

**What:** in `DiffusionNetwork::process`, the per-stage per-sample `std::sin` (`diffusion_network.h:362`)
is evaluated only when the smoothed `modDepth > 0`; otherwise `lfoValue` is taken as `0.0f`. See
correction C-7.

**Why it is bit-identical, not a behaviour change:** the value feeds exactly one expression,
`modMs = modDepth · kMaxModDepthMs · lfoValue` (`:363`). With `modDepth == 0` the product is `0.0f` for
every finite `lfoValue`, so `delayMsL/R` (`:369`, `:373`) are unchanged bit-for-bit. The LFO phase
accumulator (`:398-401`) still advances, so a later `modDepth > 0` resumes on the same phase.

**Why Phase 4 needs it:** without it the decay cloud alone spends ~384 k `std::sin` calls per second per
voice (C-7), which does not fit the SC-005 budget. "Reduce cost, never raise the baseline" is the
in-repo rule (`harmonic_cloud_perf_test.cpp:82-85`); this is the reduction.

**Containment:** the guard is a pure short-circuit; every existing `DiffusionNetwork`, Iterum and
Disrumpo test must pass unedited (SC-014).

**Roadmap action:** record under Phase 4 that `DiffusionNetwork` gained a zero-modulation fast path;
Phase 6's `AetherReverb` reuses the same network and inherits the saving.

### RA-5 — control-grid callers snap the sub-components' own smoothers

**What:** four shared DSP components gain an additive, opt-in "snap my smoothers / coefficients onto
their targets" entry point, and `ContinuousBody` calls each one on its 64-sample control grid:

| Component | New method | Called from |
|---|---|---|
| `ModalResonatorBank` | `snapCoefficients()` | `applyEngineRetune`, after `updateModes` |
| `DiffusionNetwork` | `snapSmoothers()` | `applyCloudGeometry`, at every control step |
| `TimeVaryingCombBank` | `snapSmoothers()` | `applyNonModalRetune` (comb) and material assignment |
| `WaveguideString` | `snapSmoothers()` | `applyNonModalRetune` (waveguide) |

**Why, and why it is not a loss of continuity.** FR-041 and FR-042a name each sub-component's *own*
per-sample smoother as the continuity mechanism. That is a **second** smoother in series with the one
`ContinuousBody` already runs — FR-009 pins `setNoteFrequencyHz`/`setKeyTracking` at 20 ms,
`setCloudSize` at 50 ms, `setWidth` at 20 ms — and FR-042a's own argument ("adding a second smoother in
front of it would buy nothing") applies verbatim. `bodyHz_` is already a control-grid staircase before
it reaches any engine, because `noteLog2Smoother_` is advanced with `advanceSamples(64)`. What the
second smoother does buy is cost, and two defects:

1. **Block-size dependence (SC-011).** `ModalResonatorBank::smoothCoefficients()` runs once per
   `processBlock` *call*, not once per sample, so the coefficient trajectory is a function of how the
   host partitioned the buffer — 16 calls for one 1024-sample block, 27 for the same 1024 samples
   delivered as `100+…+24`. Measured divergence **7.1e-2** against SC-011's 1e-4 bound, 1.4× the
   render's own peak. Snapping removes it at **zero** latency; the two alternatives (accumulate a full
   control chunk before calling `processBlock`, or record the deviation) cost up to 63 samples of
   latency and contradict FR-005a, or gut SC-011 on this path.
2. **CPU (SC-005).** A sub-component whose smoothers never settle can never take a hoisted path.
   Measured, one 512-sample block: the diffusion cascade **49,828 ns** unsettled against **15,197 ns**
   settled; the 6-comb bank **57,311** against **35,077**; the waveguide pays a `std::exp2` and a
   `std::pow` **per sample** for the whole of a glide. On a 53,333 ns budget that is the difference
   between fitting and missing by 30 %.

The residual step is bounded by the gates that were already specified: `kRetuneEpsilonCents = 0.5`
(0.03 % of a delay time) on pitch and `kDampingEpsilonRel = 0.005` on damping, evaluated 1.33 ms apart
at 48 kHz. **Clicklessness is measured, not assumed** — SC-004's glide and SC-012's full-range setter
sweep both cover all five materials and are green with these calls in place.

**Containment:** every method is strictly additive with no pre-existing caller, so every current
consumer of the four headers is unchanged. `ContinuousBody` additionally stops forwarding the *raw*
`cloudSize`/`width` to `DiffusionNetwork` and forwards its own smoothed value instead, which is what
FR-009's 50 ms / 20 ms columns require and what the shipped code was **not** doing (both smoothers were
advanced and read by nothing; the audible smoothing was the network's own 10 ms constant).

**Roadmap action:** record under Phase 4 that four shared DSP headers gained a control-grid snap entry
point; Phases 5–7 composing these components on the same 64-sample grid should use them.

### RA-6 — `ModalResonatorBank` pads the mode count handed to the SIMD kernel

**What:** `ModalResonatorBank::kSimdModeGranularity = 16`. The count passed to
`processModalBankSampleSIMD` is `numModes_` rounded up to a whole number of vectors, with the padding
lanes' `epsilon`/`radius`/`inputGain` **and** oscillator state held at zero by
`computeModeCoefficients`, so each padded lane evaluates `s_new = 0·(0 + 0·c) + 0·excitation = 0` and
contributes exactly `0.0f`.

**Why:** the Highway kernel's scalar tail costs roughly **nine times** the vector body. Measured on this
repo's MSVC/AVX2 Release build, 512 samples of an 8-mode bank cost 4,328 ns/block while a **9**-mode
bank — one scalar iteration more — cost 50,631 ns/block; every count that is a multiple of the lane
width measured 4,300–6,700 ns and every count that is not measured 41,000–58,000 ns. Padding restores
the vector-body figure (11 modes: 51,520 → 5,582 ns) with an unchanged output sum. Phase 4 needs it
because FR-043 truncates the mode count to whatever fits under the Nyquist guard — 11 for Glass and 29
for Metal Plate at 220 Hz — i.e. **never** a multiple of the lane width by construction.

**What changes numerically:** only the ORDER of the floating-point accumulation — the former scalar-tail
additions now land inside the vector reduction. The set of non-zero terms is identical. Not
bit-identical, therefore, and covered by SC-014's regression set rather than claimed as inert.

**Roadmap action:** record under Phase 4; Membrum's 48-mode kit voice and Innexus's physical-model mixer
share the kernel and inherit the fix.

---

## Scope

In scope, and nothing else:

1. **`ContinuousBody`** — new Layer 3 class at `dsp/include/krate/dsp/systems/continuous_body.h`
   (roadmap line 204). Stereo in, stereo out, block processing, RT-safe after `prepare()`.
2. **Five materials** — Glass, Strings, Metal Plate, Chamber, Ice; each a mode-ratio table plus a
   frequency-dependent damping law (roadmap lines 216–217).
3. **Engine pool + crossfading material selector** — `ModalResonatorBank`, `WaveguideString`,
   `TimeVaryingCombBank`; only the engine(s) a material actually uses consume CPU (roadmap lines 206–208).
4. **Continuous-excitation adapter** — input RMS tracking, configure-time resonator drive compensation,
   feedback-safe damping floors (roadmap lines 208–211). *This is the new DSP work.*
5. **Slow decay cloud** — `DiffusionNetwork` inside a damped stereo feedback loop, RT60 up to 30 s
   (roadmap lines 212–213). **Per-voice**: one instance per `ContinuousBody` (clarified 2026-07-27, OQ-3),
   costed against the 1 %/voice budget by SC-005's "cloud only" line item. A shared post-voice-sum instance
   is explicitly rejected — it would blur voices into each other and overlap Phase 6 (N-3).
6. **Key tracking** — 0 = fixed body, 1 = fully tracked, continuous in between (roadmap lines 214–215).
7. **`WaveguideString::retune()`** — the additive amendment required by 6 (RA-1) — and
   **`DiffusionNetwork`'s zero-modulation fast path** — the bit-identical amendment required by 5 (RA-4).
8. **Unit tests** in `dsp/tests/unit/systems/`, registered in `dsp/tests/CMakeLists.txt` (the systems list
   is at `:300-339`; sources are listed explicitly, never globbed).

## Non-Goals (owned by later phases, or deliberately excluded)

| # | Not in this phase | Owner / reason |
|---|---|---|
| N-1 | Composing `HarmonicCloud` → `ContinuousBody` | Phase 7 `SeraphisVoice` (roadmap lines 284–286). `ContinuousBody` takes a stereo buffer, not a cloud. |
| N-2 | Internal life modulators | Phase 7 routes Phase 1 modulators through `ModulationEngine`/`VoiceModRouter` (roadmap lines 127–128). `ContinuousBody` owns **no** `BrownianDrift`/`BreathingModulator`/… instance; it exposes plain smoothed setters that survive per-block external modulation (FR-006). |
| N-3 | Global reverb, shimmer, spectral diffusion | Phase 6 `AetherReverb` (roadmap lines 250–274). The Phase 4 decay cloud is a **per-voice** blur stage, not a room. |
| N-4 | Granular capture of the body output | Phase 5 `AtmosphereEngine` (roadmap lines 225–247). |
| N-5 | Voice envelope, spatial position, voice allocation | Phase 7. |
| N-6 | Any parameter registration, plugin ID, or UI | Phases 8–11. The roadmap reserves IDs 800–999 for the continuous body (line 372) — **not allocated here**. |
| N-7 | `BodyResonance`, `SympatheticResonance`, `FeedbackNetwork` | Deliberately unused; see correction C-5 for the per-component reason. |
| N-8 | Impulse/strike excitation, note-on transients | Membrum owns struck bodies. Phase 4 is *continuous* resonance by definition (roadmap line 202). `WaveguideString::noteOn` is used only as a tuning primitive at zero velocity (FR-022c). |
| N-9 | Serialization of body/material state | No roadmap statement requires it in Phase 4. Phase 9 owns plugin state (roadmap line 429). |

---

## Assumptions (recorded interpretations, not deferrals)

These are decisions this spec takes because the roadmap under-determines them. They are recorded as
assumptions rather than open questions because each has exactly one defensible reading, and the FRs below
are complete without further input.

- **A-1 — mono resonator core, stereo decay cloud.** The three engines are mono by construction
  (`ModalResonatorBank::processBlock(const float*, float*, int)` at `modal_resonator_bank.h:355`;
  `WaveguideString::process(float)` at `waveguide_string.h:154`; and `TimeVaryingCombBank::processStereo`
  itself sums to mono first — `timevar_comb_bank.h:660-661`, `const float monoInput = (left + right) *
  0.5f`, then pans the comb sum). Running two banks for stereo would double the dominant cost against a 1%
  budget for no physical justification: a real body is one object. The input is therefore summed to mono
  (×0.5), resonated once, and re-stereoized by the decay cloud (`DiffusionNetwork` is natively stereo,
  `diffusion_network.h:327-329`) plus per-engine stereo spread where the engine offers it for free
  (`TimeVaryingCombBank::setStereoSpread`, `:311`).
- **A-2 — one primary engine per material.** "Only active modules burn CPU" (roadmap line 208) is only
  achievable if a material names one engine. Three materials share the modal engine, differing by ratio
  table, damping law, `stretch` and `scatter` — all of which are already `setModes` arguments
  (`modal_resonator_bank.h:228-235`). **Decided, not assumed** (clarified 2026-07-27, OQ-1): the mapping is
  exactly Glass / MetalPlate / Ice → modal bank, Strings → waveguide, Chamber → comb bank, and **no
  material layers two engines** in Phase 4.
- **A-3 — mode count ceiling 32, not 96.** `ModalResonatorBank::kMaxModes` is 96 (`:73`). Phase 2 measured a
  64-partial cloud at ~29,464 ns/block ≈ 0.276% of one core (recorded in
  `specs/seraphis-phase3-spectral-morph/spec.md`, RA-3 disposition). A 96-mode bank plus a diffusion loop
  would not fit inside 1% with the margin SC-005 demands. 32 modes is the largest power-of-two mode count
  that leaves headroom for the decay cloud and the crossfade. **32 is a ceiling, not a fixed count:** the
  Glass/Ice ratio law grows ≈ n², so at `f_body = 220` Hz and 48 kHz it crosses the bank's Nyquist guard
  (`kNyquistGuard = 0.49`, `modal_resonator_bank.h:567`, cull at `:732-738`) at about n = 18. Modes above
  the guard are **not free** — `processBlock` passes `numModes_` to the SIMD kernel (`:362-364`) and
  `flushSilentModes` only decrements `numActiveModes_` (`:383-396`) — so FR-043 truncates the count instead
  of handing the bank zero-amplitude modes. The mode-count choice is therefore bounded by the ratio law and
  the pitch, not only by CPU. **32 is fixed** (clarified 2026-07-27, OQ-2): there are no quality tiers, no
  runtime mode-count control and no second set of SC-005 baselines — the only thing that ever reduces the
  count is FR-043's Nyquist prefix truncation.
- **A-4 — the body is a *mix* stage, not a replacement.** `setMix` (0 = input passthrough, 1 = fully
  resonated) is **roadmap-mandated, not spec-added**: roadmap line 208 specifies "selector + mix pattern
  from the Innexus roadmap", and that pattern is explicitly both halves —
  `specs/Innexus-physical-modelling-roadmap.md:62` ("**Selector + mix, not serial chain.** Each stage is a
  parallel selector with crossfade. Only active modules consume CPU") and `:1775`
  (`kBodyMixId` — "Dry/wet blend of body resonance. 0 = bypass (no body coloring), 1.0 = fully colored").
  FR-060 realises the *mix* half of line 208 and appears in the roadmap-coverage table for it. The project
  parameter table independently makes `Mix` the standard name for this control.
- **A-5 — control cadence 64 samples, phase-continuous across calls.** All configure-rate work (drive
  recompute, `Ĝ` recompute, key-track retune, engine crossfade gains, RMS follower advance, decay-cloud
  loop) runs on a 64-sample grid, matching `HarmonicCloud::kControlChunkSamples = 64`
  (`harmonic_cloud.h:144`) and `BrownianDrift::kControlRateInterval = 32` (`brownian_drift.h:105`), so
  Phase 7 can drive both components on one clock. The grid is driven by an internal sample counter that
  **persists across `processStereoBlock` calls**, so a control step falls on the same absolute sample index
  no matter how the host partitions the stream — a 1023 + 1 split and a single 1024 call step the controls
  at identical positions. This is what makes FR-005's block-size invariance hold for *arbitrary* block
  sizes rather than only multiples of 64 (SC-011). **The carry is total** (clarified 2026-07-27, Q6):
  FR-034's `Σx²` and sample-count accumulators carry across `processStereoBlock` calls too, a partial
  (sub-64) tail at the end of a block fires **no** control step, and the engines and the decay cloud are
  advanced in sub-chunks bounded by the distance to the next absolute grid point. Nothing is buffered, so
  the component adds **zero** latency. FR-005a states the rule normatively.
- **A-7 — two spec-added controls, disclosed here.** Two controls in the FRs are named by neither the
  roadmap nor the Innexus selector+mix pattern, and are recorded here rather than left to be discovered:
  **`setInputAgcEnabled(bool)`** (FR-034a) and **`setResonatorBypass(bool)`** (FR-063). Both exist because
  a success criterion is otherwise unwritable — SC-007 must measure the drive law with the AGC held out of
  the loop, and SC-008 must measure the decay cloud's RT60 without a 30 s body underneath it — and both are
  useful to Phase 7 (a raw-gain voice mode; a body-less halo). Neither is a *level* control: FR-061 (an
  output trim) was considered and **deleted**, because Phase 7 owns the output stage (roadmap line 290) and
  a user-settable ±dB gain sits directly on SC-007's measurement path.
- **A-6 — Aramaki's "damping law sells the material" (roadmap line 217) is implemented as the existing
  `ModalResonatorBank::DampingLaw{b1, b3}` form**, `decayRate_k = b1 + b3·f_k²`, `R_k =
  exp(−decayRate_k/fs)` (`modal_resonator_bank.h:81-89`, `:742-743`). This is the Chaigne–Lambourg
  two-parameter form the bank already implements and the same form
  `ModalResonator::MaterialCoefficients` uses (`modal_resonator.h:91-96`). Introducing a second damping
  parameterisation would be gratuitous.

---

## Functional Requirements

### FR-001 series — shared contract, layer, lifecycle, RT safety

**FR-001 — Component identity.** A single new class `ContinuousBody` in namespace `Krate::DSP`, defined in
`dsp/include/krate/dsp/systems/continuous_body.h` (roadmap line 204). Header-only, consistent with the
layer's convention (`dsp/CLAUDE.md`, "Header-only + SIMD conventions").

**FR-002 — Real-time safety.** Every method except `prepare()` is `noexcept` and performs no heap
allocation, no lock, no exception, no I/O. `prepare()` is the only method permitted to allocate, and every
buffer, delay line and pool is sized there (roadmap line 485). Verified by SC-006.

**FR-003 — Layer discipline.** `ContinuousBody` is Layer 3. It may include Layers 0–2 and other Layer 3
headers where no cycle results. The rule that is *enforced* is the lint, not the prose: `tools/lint-layers.js:6-8`
reads "a header in layer N may only `#include` from layers **<= N**", i.e. same-layer includes are legal, and
`systems/poly_synth_engine.h:40-41` (which includes `systems/voice_allocator.h` and `systems/synth_voice.h`)
is the in-tree precedent. `dsp/CLAUDE.md:6-8` states the stricter "only from layers **below**" wording; where
the two disagree the lint governs, and this FR is written against the lint. Its permitted includes are exactly:
`core/crossfade_utils.h`, `core/db_utils.h`, `core/random.h`, `core/math_constants.h`,
`primitives/dc_blocker.h`, `primitives/smoother.h`, `primitives/delay_line.h`, `primitives/one_pole.h`,
`processors/modal_resonator_bank.h`, `processors/waveguide_string.h`, `processors/diffusion_network.h`,
`processors/envelope_follower.h`, `systems/timevar_comb_bank.h`. No Layer 4 include. No plugin include.
No arch-guarded krate include (`node tools/lint-arch-guarded-includes.js` must stay clean).

> **Amended 2026-07-28, from the shipped header.** The list above was written before the component
> existed and got two entries wrong; both are Layer 0/1, so no layer rule was ever at risk, but an
> enumerated list that does not match the file is not a specification of anything.
> - **`core/db_utils.h` ADDED.** FR-050's decay-cloud feedback write is required to call
>   `detail::flushDenormal` (`db_utils.h:168`, risk R-6), and resolving that symbol only *transitively*
>   through `primitives/smoother.h:28` is exactly the fragility this list exists to prevent.
> - **`primitives/dc_blocker.h` ADDED.** FR-050 puts a `DCBlocker` in the cloud loop, per channel.
> - **`core/dsp_utils.h` REMOVED.** Nothing in the component calls `softClip`: FR-037's guard is a
>   `std::clamp`, and every soft clip on the path lives inside `ModalResonatorBank` or
>   `WaveguideString`. A header-only Layer 3 file that pulls `dsp_utils.h` in rebuilds a large part of
>   the tree for a symbol it never names.

**FR-004 — Lifecycle.**
- `void prepare(double sampleRate) noexcept` — re-derives every sample-rate-dependent coefficient, sizes
  every buffer for the worst case (max decay-cloud loop length, max waveguide period at 20 Hz), and calls
  `reset()`. Sample rates ≤ 1 are clamped to 1 (the `HarmonicCloud::prepare` idiom,
  `harmonic_cloud.h:283`). May allocate. May be called repeatedly.
- `void reset() noexcept` — clears every engine state, delay line, smoother and follower; snaps every
  smoother to its target; leaves parameters unchanged. RT-safe.
- Processing before `prepare()` writes silence and returns; it must never read uninitialised coefficients
  (the `harmonic_cloud.h:887-891` idiom — the `if (!prepared_)` block, verified this session).

**FR-005 — Processing entry point.**
```cpp
void processStereoBlock(const float* inLeft, const float* inRight,
                        float* outLeft, float* outRight,
                        std::size_t numSamples) noexcept;
```
Null input or output pointers cause an immediate return without writing; `numSamples == 0` is a no-op.
Output is block-size invariant for **arbitrary** partitions, not only multiples of 64: the control grid is
driven by a persistent sample counter (A-5), so 1×1024, 16×64, 1023+1 and 100-sample blocks all step the
controls at the same absolute sample indices and must agree within SC-011's tolerance. In-place operation
(`inLeft == outLeft`) is **not** supported and is documented as such.

**FR-005a — Control-grid boundary handling: full carry, zero latency.** A `std::uint64_t` sample counter,
cleared only by `prepare()`/`reset()`, indexes the absolute stream position. Inside `processStereoBlock`:

1. the block is walked in sub-chunks of `min(samplesRemainingInBlock, 64 − (counter mod 64))`;
2. the engines, the crossfade mix and the cloud read/write advance over each sub-chunk;
3. **a control step fires exactly when the counter crosses a multiple of 64 — never on a sub-64 tail.**
   The control step is, in order: RMS-follower advance (FR-034), `Ĝ` recompute (FR-032), drive recompute
   (FR-033), key-track retune (FR-040–FR-042), resonance/damping apply (FR-036, FR-042a),
   crossfade-position advance (FR-024/FR-024a), decay-cloud loop iteration (FR-050) and the cloud-bypass
   evaluation (FR-053a);
4. FR-034's `Σx²` accumulator **and** its sample count carry across `processStereoBlock` calls, so a
   control chunk split 36 + 28 by a block boundary yields exactly the same `chunkRms` as an unsplit 64.

No input or output is buffered: the component's latency is **0 samples** at every block size. This is the
mechanism SC-011's 1023 + 1, 100-sample and 7 × 146 + 2 partitions test — at the *same* tolerance as the
64-multiple partitions, which is only reachable with the carry.

**FR-006 — Parameter setters are smoothed and modulation-safe.** Every setter clamps to the range FR-009
pins for it, substitutes the FR-009 default if the argument is non-finite (checked by bit pattern, never
`std::isnan`), and re-targets an `OnePoleSmoother` (`primitives/smoother.h`, `configure(ms, sr)` /
`setTarget` / `process` / `snapTo`) at the FR-009 smoothing time rather than writing the coefficient
directly. Calling any setter once per 64-sample block for 60 s at any value must produce no click above
SC-012's threshold. This is what makes N-2 safe: Phase 7 modulates by calling setters, and needs no
cooperation from this class.

Three named exceptions, each with its own clickless mechanism instead of a setter-level smoother — FR-009's
table marks them and no other setter may join them:
- `setResonance` / `setDamping` — applied **at the control step** with no setter smoother; the modal bank's
  own 2 ms coefficient smoother (`modal_resonator_bank.h:569`, `:801-809`) carries continuity, and FR-042a's
  dirty gate keeps the transcendental cost off the hot path;
- `setMaterial` — an equal-power **crossfade** over `kMaterialCrossfadeMs` (FR-024/FR-024a);
- `setResonatorBypass` — an equal-power **ramp** over `kSlotReleaseMs` (FR-063).

**FR-007 — Introspection surface.** Public, non-`#ifdef`, part of the contract (the `harmonic_cloud.h:950-1035`
precedent). The list is exhaustive — a success criterion may not assert on a quantity absent from it:

| Accessor | Returns |
|---|---|
| `BodyMaterial getMaterial() const noexcept` | the material currently selected (the *incoming* one during a crossfade) |
| `int getActiveModeCount() const noexcept` | modes handed to the modal bank after FR-043's truncation |
| `float getModeFrequencyHz(std::size_t k) const noexcept` | configured frequency of mode `k`, 0 if `k` ≥ count |
| `float getBodyFrequencyHz() const noexcept` | `f_body` after FR-040 |
| `float getEngineT60Sec() const noexcept` | the T60 FR-036 currently targets for the active engine (all five materials — the value fed to the modal damping law / `setDecay` / the comb feedback solve) |
| `float getDriveGain() const noexcept` | FR-033's smoothed **engine** drive for the sounding slot (the *incoming* slot during a crossfade). The cloud drive (`rmsGain · userDrive`, FR-033) is deliberately not exposed separately — no success criterion measures it |
| `float getInputRms() const noexcept` | FR-034's tracked input RMS |
| `float getSteadyStateGainBound() const noexcept` | FR-032's `Ĝ` for the sounding slot (the *incoming* slot during a crossfade) |
| `bool isCrossfading() const noexcept`, `float getCrossfadePosition() const noexcept` | FR-024 state |
| `float getCloudFeedbackGain() const noexcept`, `float getCloudLoopSeconds() const noexcept` | FR-052's `fb` and the measured total loop time it was derived from |
| `std::uint64_t getClampEngagementCount() const noexcept` | number of individual **samples** on which FR-037's ±2.0 clamp altered the value. Counts the post-crossfade engine sum only (one count per sample, not per channel — the resonator core is mono, A-1). Cleared by `reset()` and `prepare()` and by nothing else, so a test reads it cumulatively and brackets a phase by subtracting the value read at the phase boundary |
| `std::uint64_t getEngineSampleCount(Engine e) const noexcept` | cumulative samples for which engine `e` was actually advanced. Incremented once per control chunk by the chunk length, per active engine — the functional (non-perf) evidence for FR-023. Cleared by `reset()`/`prepare()` |
| `bool stateFinite() const noexcept` | every engine, delay, smoother and follower state is finite |

`stateFinite()` checks the IEEE-754 exponent field by bit pattern, **never** `std::isnan`/`std::isinf`
(the macOS leg builds with `-ffast-math`, which folds them — root `CLAUDE.md`, "Cross-Platform
Compatibility").

**FR-008 — Constant scoping.** Every named constant is **class-scoped** (`static constexpr` inside
`ContinuousBody`), never namespace-scope. This is the rule `HarmonicCloud` states explicitly at
`harmonic_cloud.h:133-137`, and it is what keeps `kBodyModeCount` (namespace scope,
`body_resonance.h:45`), `kNumDiffusionStages` (`diffusion_network.h:36`) and `kBodyPresetCount`
(`body_resonance.h:48`) from colliding.

**FR-009 — The control surface, pinned in full.** Every setter's range, default and smoothing time, and the
state of a freshly-`prepare()`d `ContinuousBody` on which no setter has been called. **This table is
normative and complete** — no setter exists outside it, and Phases 7 and 9 consume it verbatim (a Phase 8
parameter registration must reproduce these ranges and defaults; a Phase 9 preset that omits a field must
land on the Default column). Every range is clamped inside the setter, and every non-finite argument is
replaced by the Default column value (FR-006).

| Setter | Range | Default (= prepared state) | Smoothing | Notes |
|---|---|---|---|---|
| `setMaterial(BodyMaterial)` | the five enumerators | `Glass` | `kMaterialCrossfadeMs = 500` crossfade | FR-014, FR-024/FR-024a; a crossfade, not a smoother |
| `setResonance(float)` | `[0, 1]` | `0.7` | **none** — applied at the control step | FR-036, FR-042a; the bank's own 2 ms coefficient smoother carries continuity |
| `setDamping(float)` | `[0, 1]` | `0.0` | **none** — applied at the control step | FR-036, FR-042a |
| `setKeyTracking(float)` | `[0, 1]` | `1.0` | `kPitchSmoothMs = 20` | FR-040; smoothed in the log-frequency domain |
| `setNoteFrequencyHz(float)` | `[20, 8000]` Hz | `220.0` | `kPitchSmoothMs = 20` | FR-040; smoothed in the log-frequency domain |
| `setDrive(float)` | `[0, 4]` | `1.0` | `kDriveSmoothMs = 50`, **log10 domain** | FR-033 |
| `setMix(float)` | `[0, 1]` | `1.0` | `kMixSmoothMs = 20` | FR-060, equal-power |
| `setCloudMix(float)` | `[0, 1]` | `0.25` | `kMixSmoothMs = 20` | FR-053, equal-power |
| `setCloudDecaySec(float)` | `[0.1, 30.0]` s | `4.0` | `kCloudSmoothMs = 50` (on the derived `fb`) | FR-052 |
| `setCloudSize(float)` | `[0, 1]` | `1.0` | `kCloudSmoothMs = 50` | FR-053; recomputes `loopSeconds`, hence `fb` |
| `setCloudDamping(float)` | `[0, 1]` | `0.3` | `kCloudSmoothMs = 50` | FR-053; maps 18 kHz → 800 Hz |
| `setWidth(float)` | `[0, 1]` | `1.0` | `kMixSmoothMs = 20` | FR-062 |
| `setInputAgcEnabled(bool)` | — | `true` | absorbed by `driveGain`'s own smoother | FR-034a |
| `setResonatorBypass(bool)` | — | `false` | `kSlotReleaseMs = 10` ramp | FR-063; a ramp, not a smoother |
| `setSeed(std::uint32_t)` | any | `1` | n/a (configure-time only) | FR-070; `0` → `Xorshift32`'s own default |

**Freshly-prepared state, restated as one line** so a test can assert it directly: material `Glass`,
resonance 0.7, damping 0.0, keyTracking 1.0, noteHz 220, drive 1.0, mix 1.0, cloudMix 0.25,
cloudDecaySec 4.0, cloudSize 1.0, cloudDamping 0.3, width 1.0, AGC **on**, resonator bypass **off**,
seed 1. `reset()` leaves every one of these unchanged (FR-004) and snaps every smoother to its target;
`prepare()` likewise does not restore them — a body that has been configured stays configured across a
sample-rate change.

Two properties of this default set are deliberate. It is **audible without configuration**: `mix = 1.0`
with `resonance = 0.7` gives Glass `b1_eff = 0.50 · 40^0.3 = 1.512` s⁻¹, i.e. a 4.6 s T60 (FR-036's law),
plus a small halo at `cloudMix = 0.25`. And it is the exact state SC-006, SC-010 and SC-011 start from, so
their "identical parameters" premise is a checkable claim rather than a convention.

### FR-010 series — the material model (roadmap lines 216–217)

**FR-010 — Material enumeration.** A **class-scoped** enum:
```cpp
enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice };
```
Exactly the five the roadmap names (line 216), in that order. The name `BodyMaterial` — not `Material` —
is mandatory: `Krate::DSP::Material` already exists at namespace scope
(`modal_resonator.h:81`, `enum class Material : uint8_t { Wood, Metal, Glass, Ceramic, Nylon }`). See the
New Components table.

**FR-011 — Material profile record.** Each material is a compile-time `constexpr` record, class-scoped as
`ContinuousBody::MaterialProfile`, carrying:

| Field | Meaning |
|---|---|
| `engine` | which engine drives it (`Modal`, `Waveguide`, `Comb`) — A-2 |
| `ratios[kMaxModes]` | mode frequency ratios relative to the body fundamental (modal materials only) |
| `amplitudeExponent` | mode amplitude profile `a_k = k^(−α)`, normalised so `Σa_k = 1` |
| `damping` | `ModalResonatorBank::DampingLaw{b1, b3}` (`modal_resonator_bank.h:81-89`) |
| `stretch`, `scatter` | passed straight to `setModes` (`:228-235`); `B = stretch²·0.01`, `C = scatter·0.10` (`:702-703`) |
| `referenceHz` | the material's fixed-body pitch, used at `keyTracking = 0` (FR-040) |
| `defaultModeCount` | ≤ `kModeCountCeiling` (32, A-3); the *ceiling* on the count, truncated further by FR-043 |
| `t60AtMaxResonanceSec` | the engine-native decay target at `setResonance(1)` — FR-036's anchor for **all five** materials, including the two non-modal ones |
| `hfDampingParam` | the engine-native frequency-dependent damping control: `damping.b3` (modal), loss-filter `S` (waveguide), per-comb damping (comb) — FR-013 |

**FR-011a — The five profiles, in full.** Every field, for every material. No material is defined by
prose. Modal materials use FR-012's ratio table; the other two have no ratio table because their engine
generates its own mode set (FR-013a).

| Field | Glass | Strings | Metal Plate | Chamber | Ice |
|---|---|---|---|---|---|
| `engine` | Modal | Waveguide | Modal | Comb | Modal |
| `referenceHz` | 660.0 | 196.0 | 330.0 | 110.0 | 880.0 |
| `defaultModeCount` | 32 | n/a (waveguide loop) | 32 | n/a (6 combs) | 32 |
| `ratios[]` | FR-012 Glass | — | FR-012 Metal Plate | — | FR-012 Glass |
| `amplitudeExponent` α | 1.0 | — | 0.7 | — | 0.9 |
| `stretch` | 0.0 | — | 0.15 | — | 0.5 |
| `scatter` | 0.0 | — | 0.10 | — | 1.0 |
| `t60AtMaxResonanceSec` | 13.8 | 8.0 | 23.0 | 2.5 | 11.5 |
| `hfDampingParam` | `b3 = 5.0e-8` | `S = 0.15` | `b3 = 1.0e-9` | comb damping 0.35 | `b3 = 3.0e-8` |

`a_k = k^(−α)` normalised so `Σ a_k = 1` (FR-011). `stretch`/`scatter` go straight to `setModes`
(`modal_resonator_bank.h:228-235`); the bank turns them into `B = stretch²·0.01` and `C = scatter·0.10`
(`:702-703`). Ice is *not* "Glass with a knob turned" in the loose sense the earlier draft implied: it
shares Glass's ratio table but differs in four numbered fields (α, stretch, scatter, damping) and in
`referenceHz`, and SC-003 measures the difference.

**Two of Ice's fields were re-valued against measurement during implementation** (2026-07-27), under
SC-003(a)'s own standing instruction — *change the profiles until the materials really are distinct, never
lower a threshold*:
- **α: 1.3 → 0.9.** `a_k = k^(−α)`, so an α *above* Glass's 1.0 starves Ice's upper modes and makes Ice the
  **darker** of the pair — the opposite of FR-013's "bright but scattered", and it inverted SC-003(d),
  whose entire claim is that Ice is the flatter spectrum. Measured at the SC-003 excitation with α = 1.3:
  spectral flatness Ice **0.177** vs Glass **0.203** (0.026 the wrong way against a +0.02 criterion) and
  centroid 2510 Hz vs 3110 Hz. Flatness is not monotone in α, so the value was swept: 0.5 → 0.220,
  0.7 → 0.222, **0.9 → 0.225** (Glass 0.203). 0.9 also keeps Ice brighter than Glass and keeps the two
  materials distinct in all four numbered fields.
- **scatter: 0.8 → 1.0** (the bank's own ceiling, `modal_resonator_bank.h:702`). See FR-012 below: the
  "≥ 6 of 8" in SC-003(c3) was derived from the scatter column *alone*, and Ice's `stretch` cancels two of
  the negative scatter terms. At 0.8 only five of eight peaks clear 2 %; at 1.0 six do.

**FR-012 — Modal mode-ratio tables.** Physically-motivated, not arbitrary:
- **Glass** — free-edge axisymmetric shell modes, `f_n ∝ n(n²−1)/√(n²+1)` normalised at `n = 2`, giving
  `1.000, 2.83, 5.42, 8.77, 12.85, 17.65, …` (Rossing, *Acoustics of the glass harmonica / wine glasses*).
  Extended to 32 by continuing the law.
- **Metal Plate** — free circular plate (Chladni) ratios `1.000, 1.730, 2.328, 4.061, 5.980, 6.710, 9.011,
  11.20, …` (Rossing, *The Science of Sound*, thin circular plate), extended to 32.
- **Ice** — the Glass table with `scatter = 1.0` and `stretch = 0.5` (FR-011a), producing the cracked,
  detuned-cluster character. Both are already bank arguments, so this adds no new maths. The bank's scatter
  is a *deterministic* golden-ratio displacement, `f_w ×= (1 + C·sin(k·kScatterD))` with
  `kScatterD = π(φ−1)` (`modal_resonator_bank.h:577-578`, `:729`) — not RNG.
  **The displacement SC-003(c3) measures is the product of BOTH warps, not the scatter column alone.** An
  earlier draft quoted only the scatter terms (`0 %, +7.5 %, −5.4 %, −3.5 %, +8.0 %, −2.2 %, −6.4 %,
  +6.8 %` at `C = 0.08`) and derived "≥ 6 of 8 clear 2 %" from them. Ice also carries `stretch = 0.5`
  (`B = 0.0025`), applied *before* the scatter at `modal_resonator_bank.h:753`, and it is a strictly
  positive, monotonically growing displacement Glass does not have: `+0.12, +0.50, +1.12, +1.98, +3.08,
  +4.40, +5.95, +7.70 %`. At `C = 0.08` the product cancels at `k = 3` (−1.60 %) and `k = 6` (−0.78 %),
  leaving **five** of eight. At the shipped `scatter = 1.0` (`C = 0.10`) the Ice↔Glass mode-frequency
  ratios are `+0.12, +9.87, −5.72, −2.50, +13.34, +1.49, −2.46, +16.91 %` — **six** clear 2 %, the two
  that carry the clause by 23 %. `k = 0` can never clear it (`sin 0 = 0` leaves the stretch warp alone),
  so six is this pair's ceiling, not a value with slack: any future edit to Ice's `stretch` or `scatter`
  must re-derive the list.

Both modal tables are strictly increasing in `k`, which is what makes FR-043's *prefix* truncation exact:
if mode `k` is above the Nyquist guard, so is every mode after it.

**FR-013 — Damping laws.** `b1`/`b3` are anchored to the values the repo already ships for the same
`b1 + b3f²` model in `kMaterialPresets` (`modal_resonator.h:99-110`):

| Material | `b1` (s⁻¹) | `b3` (s) | T60 at DC = `6.91/b1` | Damping at the top of its own mode set | Character |
|---|---|---|---|---|---|
| Glass | 0.50 | 5.0e-8 | 13.8 s | mode 32 at `f_body = 220` sits near 3.9 kHz → `b3f² = 0.76` s⁻¹, i.e. **2.5× the DC rate** | long, bright, ringing, HF dies first |
| Metal Plate | 0.30 | 1.0e-9 | 23.0 s | top mode ≈ 2.46 kHz → `b3f² = 0.006` s⁻¹, **2 % of the DC rate** | longest, near-flat HF damping |
| Ice | 0.60 | 3.0e-8 | 11.5 s | top mode ≈ 3.9 kHz → `b3f² = 0.46` s⁻¹ | bright but scattered |

The explicit-`DampingLaw` path applies **no upper bound** on `b3` — `computeModeCoefficients` does only
`const float b3 = std::max(b3In, 0.0f)` (`modal_resonator_bank.h:686`). `kLegacyMaxB3 = 4.0e-5` (`:93`) is
not a clamp on this path at all: it appears only in `dampingLawFromLegacy` (`:108`), which scales the
legacy `(decayTime, brightness)` conversion and which FR-022a does not use. The values above are quoted
against it only to show they are well inside the range that conversion produces.

**FR-013a — Frequency-dependent damping for the two non-modal materials.** Roadmap line 216–217 requires
*every* material to be "mode-ratio table + frequency-dependent damping law". The two non-modal engines
generate their own mode set (a waveguide's harmonic series; a comb's `f[n] = f0·√(1+n·spread)`,
`timevar_comb_bank.h:236-237`) and carry their damping law in engine-native form. Both are genuine
frequency-dependent laws in the same sense as `b1 + b3f²` — a decay rate that rises with frequency — and
both are given concrete values here rather than an em-dash:

| Material | Mode set | Damping law (verified expression) | Values |
|---|---|---|---|
| **Strings** (waveguide) | harmonic series of the loop, `f_n = n·f0`, dispersion-warped by `B = stiffness·0.002` (`waveguide_string.h:296`) | round-trip loss `\|H(ω)\| = rho·√((1−S)² + 2S(1−S)cos ω + S²)` (`:379-382`, applied at `:197`), `rho = 10^(−3/(T60·f0))` (`:476-481`), `S = brightness·0.5` (`:168`). Flat at `S = 0`; a null at Nyquist at `S = 0.5`. Decay rate therefore rises monotonically with frequency for any `S > 0` | `T60 = 8.0` s at `setResonance(1)`, `S = 0.15` (i.e. `setBrightness(0.30)` — see correction **C-6**: the argument *darkens*), `stiffness = 0.15`, `pickPosition = 0.22` |
| **Chamber** (comb) | `f[n] = f_body·√(1 + n·spread)` over 6 combs (`timevar_comb_bank.h:236-237`) | one-pole lowpass inside each comb's feedback path (`setCombDamping`, `:200-206`; applied per sample at `ch.comb.setDamping(...)`), so per-round-trip loss is `fb_n·\|H_lp(ω)\|` and higher partials of each comb decay faster | `T60 = 2.5` s at `setResonance(1)`, comb damping `0.35`, `spread = 0.45`, `numCombs = 6`, `stereoSpread = 0.6` |

FR-030 series defines how the user-facing Resonance/Damping controls scale **all five** of these.

**FR-014 — Material selection is one call.**
`void setMaterial(BodyMaterial m) noexcept` — RT-safe, may be called at any time, triggers FR-020's
crossfade. Selecting the material already active is a no-op (no crossfade, no state disturbance).

### FR-020 series — engine pool, selector, crossfade (roadmap lines 206–208)

**FR-020 — Engine pool.** `prepare()` constructs and prepares exactly:
- two `ModalResonatorBank` instances (slots A and B),
- one `WaveguideString`,
- one `TimeVaryingCombBank`.

Two modal banks are required because three materials use the modal engine (A-2) and a
Glass→Ice change must crossfade between two simultaneously-ringing modal states; `ModalResonatorBank` holds
one mode configuration at a time and `setModes` memsets `sinState_`/`cosState_` before recomputing
(`modal_resonator_bank.h:237-239`). Waveguide↔waveguide and comb↔comb transitions cannot occur (one
material each), so one instance of those suffices.

**Two is also a hard ceiling, not just a floor.** FR-024's collapse rule guarantees that **at most two
engine instances are advanced at any instant**, whatever sequence of `setMaterial()` calls arrives. That is
what makes SC-005's crossfade budget a bounded quantity rather than an open-ended one, and it is why a
third modal slot is neither provisioned nor needed.

**FR-021 — Engine slot contract.** `IResonator` (`processors/iresonator.h:32-72`) is the *type* the slot
machinery holds, but two of its members must **not** be used on the hot path, both verified this session:

- **The modal engine is driven by its concrete `processBlock(const float*, float*, int)`**
  (`modal_resonator_bank.h:355`) on the 64-sample control chunk — **never** through `IResonator::process`.
  `ModalResonatorBank::process` (`:492-501`) delegates to `processSample`, which calls
  `smoothCoefficients()` **every sample** (`:345-349`, `:801-809`, i.e. `3 × numModes` extra multiply-adds
  per sample) and then the *scalar* `processSampleCore` (`:814+`), bypassing `processModalBankSampleSIMD`
  entirely. Only `processBlock` reaches the SIMD kernel (`:362-364`) and smooths once per block (`:357`).
  Using `IResonator::process` here would be both unvectorised and per-sample-smoothed, against SC-005.
- **`getControlEnergy()` / `getPerceptualEnergy()` are not part of the slot contract.** For the modal bank
  they are updated *only* inside `process()` (`:494-500`); `processBlock` never touches
  `controlEnergy_`/`perceptualEnergy_`, so on the performant path they return stale values. Where the
  modal engine's energy is needed, `getModalEnergy()` (`:415`) is used — it is computed from state. The
  waveguide's `process()` *does* update them (`waveguide_string.h:209-212`), so they remain valid there,
  but nothing in this spec depends on them.

What the slot contract does use: `prepare(double)`, `silence()`, and — for the waveguide only —
`process(float)` (`waveguide_string.h:154-218`, its only processing entry point). Everything else goes
through the concrete type (`setModes`, `updateModes`, `setStiffness`, `setFundamental`, …).
`TimeVaryingCombBank` does **not** implement `IResonator` (verified: `timevar_comb_bank.h:81` has no base
class) and is therefore addressed concretely; the spec does not require it to be retro-fitted.

**FR-022 — Per-engine configuration.**
- **(a) Modal** — `setModes(freqs, amps, count, DampingLaw, stretch, scatter)`
  (`modal_resonator_bank.h:228-235`) on material assignment (clears state), and `updateModes(…)`
  (`:264-275`, state-preserving) for every subsequent retune. `setOutputSoftClipThreshold(1.0f)` (`:150`)
  and `setOutputGain(1.0f / getInputGainSum())` (`:140`, `:168`) are set at configure time: the historical
  0.707 clipper (`:571`) would sit pinned for the entire ring of a *sustained* input and mask damping
  modulation exactly as the header's own Phase-11 note warns (`:120-131`).
- **(b) Comb** — `setTuningMode(Tuning::Inharmonic)` (`:226`, `:43`), `setFundamental` (`:238`),
  `setSpread` (`:249`), `setNumCombs` (`:178`), `setCombFeedback` (`:197`), `setCombDamping` (`:206`),
  `setStereoSpread` (`:311`). Fundamental is clamped by the bank to `[20, 1000]` Hz (`:520`, limits at
  `:91-94`). Inharmonic tuning is `f[n] = fundamental·√(1 + n·spread)` (`:236-237`), which is what FR-036's
  comb feedback solve uses for `τ_n`.
- **(c) Waveguide** — tuned with `noteOn(f0, 0.0f)` (`waveguide_string.h:273`). At velocity 0 the
  excitation scale `velScale = velocity * excitationGain_` is 0 (`:393`, consumed at `:446`), so the loop is configured and
  the delay filled with silence: the *only* way, pre-RA-1, to set `bridgeDelayFloat_` (`:325`) without
  injecting a pluck. All subsequent pitch changes use `retune()` (FR-080). `setStiffness`
  (`:256`, `B = stiffness·0.002` at `:296`) and `setPickPosition` (`:264`) are frozen at assignment, as the
  component requires.

**FR-023 — Only active engines are processed.** In the steady state (no crossfade in flight) exactly one
engine's `process`/`processBlock` is called per sample. Inactive engines are not advanced at all — not
called with zero input, not called and discarded (roadmap line 208).

Verified **functionally, not by timing**: `getEngineSampleCount(Engine)` (FR-007) must read exactly
`numSamples` for the active engine and exactly **0** for every other engine across a render with no
material change, and the totals across all engines must equal `numSamples × (1 + crossfading samples)`
across a render with one. A timing comparison cannot distinguish "not advanced" from "advanced with zero
input", so SC-005's per-material spread is corroboration, not the proof.

**FR-024 — Material crossfade.** On `setMaterial()`:
1. the incoming material is assigned to a free instance of its engine type (modal alternates A/B);
2. an equal-power crossfade runs for `kMaterialCrossfadeMs = 500.0f`, gains from
   `equalPowerGains(position, fadeOut, fadeIn)` (`core/crossfade_utils.h:50`), position advanced by
   `crossfadeIncrement(kMaterialCrossfadeMs, sampleRate)` (`:89`);
3. the **outgoing** engine's input is muted at fade start, so it decays through its own damping law rather
   than being cut — the physically correct behaviour for a resonant body;
4. when the fade completes the outgoing engine is `silence()`d and stops being processed.

Both engines are processed during the fade — SC-005 explicitly budgets this window separately.

**FR-024a — Retarget: the collapse rule.** `setMaterial()` called *during* a fade cannot simply "make the
incoming engine the new outgoing engine": a Glass→Ice→MetalPlate sequence inside one 500 ms window would
then need three simultaneously-ringing modal states, and there are two slots (FR-020). Nor may a ringing
engine be `silence()`d to free its slot — that is a step equal to its current crossfade gain, i.e. a click
(SC-012). The rule is therefore:

1. If no fade is in flight, run the standard 500 ms fade (above).
2. If a fade **is** in flight, first **collapse** it: over `kSlotReleaseMs = 10.0f`, ramp the current
   `(fadeOut, fadeIn)` gain pair to `(0, 1)` with the same equal-power law. The crossfade position does not
   advance during the collapse. At the end the in-flight *incoming* engine is the sole sounding engine
   (it is the one whose state matches what is being heard now), the other slot is `silence()`d and free.
3. Then start the standard 500 ms fade from the collapsed engine into the newly requested material.

Consequences, all of them wanted: at most two engine instances are ever advanced (FR-020); no engine state
is ever destroyed while audible — a slot becomes reusable only after `silence()`, and `silence()` only
happens at zero gain; every gain change is a ramp of ≥ 10 ms, which is far below `ClickDetector`'s
sensitivity at 48 kHz; and a material change during a fade costs at most 10 ms of extra latency. The
three-materials-in-one-window case is named explicitly in SC-002 and SC-012.

### FR-030 series — continuous-excitation adapter (roadmap lines 208–211) — **the new DSP work**

**FR-031 — Why this exists (recorded, because the numbers are the requirement).** A mode of the coupled
form has radius `R_k = exp(−(b1 + b3f_k²)/fs)` (`modal_resonator_bank.h:742-743`). At the damping floor
this spec sets (FR-035, `kMinB1 = 0.23`) and `fs = 48 kHz`, `1 − R ≈ 4.79e-6`, so a mode's steady-state
magnitude at its own resonance is `≈ 1/(2(1−R)) ≈ 1.0×10⁵` times the drive (the factor 2 is derived in
FR-032; an earlier draft quoted `1/(1−R) ≈ 2×10⁵`, which is the same argument off by 2×). Membrum never
sees this because it excites with impulses. A sustained cloud does. Without compensation the bank
saturates permanently and every material sounds identical — the failure mode this series exists to prevent.

**FR-032 — Configure-time steady-state gain bound.** Whenever the mode set, damping law or body pitch
changes, `ContinuousBody` computes an upper bound `Ĝ` on the active engine's steady-state gain, in the
64-sample control step (never per sample). **`Ĝ` is per slot** (FR-033): the incoming slot's `Ĝ_slot` is
computed at material assignment, the outgoing slot's is frozen for the remainder of the fade (its input is
muted, FR-024 step 3), and `getSteadyStateGainBound()` reports the sounding slot's value.

**Modal — the bound must be derived from the recursion the bank actually runs.** `ModalResonatorBank` is
*not* a direct-form two-pole with a flat numerator. Its per-mode update is the coupled (magic-circle) form,
verified at `modal_resonator_bank.h:833-838` (and `:841-855`, and the SIMD kernel's documented contract at
`processors/modal_resonator_bank_simd.h:29`):

```
s[n] = R·(s[n−1] + ε·c[n−1]) + g·u[n]
c[n] = R·(c[n−1] − ε·s[n])          // uses the UPDATED s
y[n] = s[n]
```

Eliminating `c` gives the true transfer function

```
H(z) = g·(1 − R z⁻¹) / [ (1 − R z⁻¹)² + R²ε² z⁻¹ ]
     = g·(1 − R z⁻¹) / [ 1 − R(2 − Rε²) z⁻¹ + R² z⁻² ]
```

— a **zero at `z = R`**, poles of radius `R` at angle `θ` where `cos θ = 1 − Rε²/2`, and
`ε = 2 sin(π f/fs)` (`:746`). Two consequences, both load-bearing:

- The numerator zero is *not* flat. At resonance `|1 − R e^{−jθ}| ≈ 2 sin(θ/2) ≈ θ`, which almost exactly
  cancels the `1/(2 sin θ)` boost of the second pole factor. The true peak gain is therefore
  `≈ g/(2(1−R))` — the *naive* form, within a factor of 2 — while the flat-numerator expression an earlier
  draft specified (`Ĝ = Σ g_k/|1 − 2R cos θ e^{−jθ} + R² e^{−2jθ}|`) overestimates it by `≈ 1/(2 sin(θ/2))`:
  **≈ 35× (31 dB) for a 220 Hz mode at 48 kHz**, and frequency-dependently so. FR-033 divides by `Ĝ`, so
  that draft would have driven the body ~31 dB under target, by a material- and pitch-dependent amount.
- `cos θ = 1 − Rε²/2` equals `cos(2πf/fs)` exactly at `R = 1` and detunes slightly below it, so the pole
  angle is taken from `ε` and `R`, never assumed to be `2πf/fs`.

The bound is therefore, per active mode, with `g_k` the amplitude passed to `setModes` (`:754`,
`gain_k = amp`):

```
cθ_k  = 1 − R_k·ε_k²/2                       // pole angle cosine, no trig needed
c2θ_k = 2·cθ_k² − 1
Ĝ_k   = g_k · √(1 − 2R_k·cθ_k + R_k²)
             / [ (1 − R_k) · √(1 − 2R_k·c2θ_k + R_k²) ]     // exact |H| at z = e^{jθ_k}
Ĝ     = Σ_k Ĝ_k
```

Cost: two `sqrt` per mode and no transcendentals, at most once per control step and gated by FR-042's
dirty flag. Worked example (Glass, `f = 220` Hz, `fs = 48 kHz`, `b1 = 0.50`): `ε = 0.0287973`,
`1 − R = 1.0417e-5`, `Ĝ_k/g_k = 47,996` — versus `g_k/(2(1−R)) = 47,998` from the naive form and
`1.67×10⁶` from the flat-numerator form.

`Ĝ = Σ_k Ĝ_k` is the all-modes-in-phase worst case and is therefore a true **upper bound**, not a level
predictor: single-mode excitation lands well below it. SC-015 measures both halves of that claim
(validity *and* tightness), and SC-007 is written against measured level, not against `Ĝ`.

- **Waveguide:** `Ĝ = 1 / max(1 − gTotal, ε)` where `gTotal = rho·|H_loss(f0)|` with
  `rho = 10^(−3/(T60·f0))` (`waveguide_string.h:476-481`, `computeRho`) and the loss magnitude
  `√((1−S)² + 2S(1−S)cos ω + S²)` — the identical expression the header already computes for its own
  excitation normalisation (`:379-384`, `lossGainAtF0` / `gTotal`). `T60` here is the value **after**
  FR-035's `[0.05, 10.0]` s clamp, not the requested one.
- **Comb:** `Ĝ = Σ_n 1 / max(1 − fb_n, ε)` over active combs, with `fb_n` as solved in FR-036.

`ε = 1e-6`. `Ĝ` is exposed by `getSteadyStateGainBound()` (FR-007).

**FR-033 — Drive compensation: two drives, per-slot, smoothed in the log domain.** The `1/Ĝ` term corrects
the *resonator's* steady-state gain, so it applies to a signal entering an engine and to nothing else.
There are therefore **two** drive gains, built from the same terms:

```
engineDrive_slot = clamp(kTargetPeak / Ĝ_slot, kMinDriveGain, kMaxDriveGain) · rmsGain · userDrive
cloudDrive       = rmsGain · userDrive          // no 1/Ĝ: Ĝ has no referent with no resonator in the path
```

where:
- `kTargetPeak = kEngineHeadroomFrac · kEngineClipThreshold / kMaxUserDrive = 0.9/4 = 0.225f` — the
  steady-state output level a full-scale resonant input reaches at `userDrive = 1`;
- `kMinDriveGain = 1.0e-7f`, `kMaxDriveGain = 4.0f`;
- `rmsGain` is FR-034's slow AGC (held at exactly 1 when FR-034a disables it);
- `userDrive ∈ [0, 4]` from `setDrive(float)`, default 1 (FR-009).

> **`kTargetPeak` re-derived 2026-07-28; it was pinned at `1.0f` and that value is a defect SC-001
> catches.** `Ĝ` is an *upper bound* on an engine's steady-state gain and this law divides by it, so an
> engine that **attains** its bound comes out at exactly `kTargetPeak · rmsGain · userDrive`. The
> waveguide attains it exactly — a sine at `f_body` sits on a comb tooth, where `1/(1−g_total)` is the
> realised gain and not a bound — so at `kTargetPeak = 1.0` the drive law aims Strings at precisely
> `kEngineClipThreshold`, with **zero** headroom before `WaveguideString::process`'s own `softClip`.
> Measured at SC-001's settings (full-scale sine, resonance 1.0, drive 4.0, AGC on): steady-state peak
> **0.990** — the string's clipper, not the drive law, was setting the level, which is what SC-001's
> headroom clause exists to reject. The three modal materials hide the same defect because their `Ĝ`
> sums over all modes while a sine excites one, so they sit 5–15 dB under their bound.
> The replacement is **derived, not tuned**: at the *maximum* user drive a full-scale resonant input
> lands an attaining engine exactly on the headroom fraction and never inside its clipper. A
> `static_assert` in `continuous_body.h` pins the relation, so changing any of the three constants
> breaks the build rather than silently re-opening the defect. At the default `userDrive = 1` the
> nominal level is −13 dBFS and the Drive control's own range is what reaches saturation — which is what
> a control named Drive should do. Everything measured against it is unaffected: SC-007's absolute-level
> clause is written against `kTargetPeak` **symbolically**, and SC-007(i)/(iii) and SC-015 are ratios.

`cloudDrive` is the gain applied to the mono-summed input on the resonator-bypass path (FR-063). Keeping
`1/Ĝ` there would send a full-scale input into the cloud at ≈ 2e-5 (`Ĝ ≈ 4.8e4` for Glass at 220 Hz /
48 kHz, FR-032's worked example) — a −94 dBFS halo, which would force SC-008 to regress a log-envelope
94 dB down and force Phase 7 to add ~94 dB of external makeup to use a body-less halo. Splitting the drive
costs one expression and removes both problems.

**Per-slot, snapped at assignment.** Each engine slot carries its own `engineDrive`, computed from *its
own* `Ĝ_slot`:
- at **material assignment** the incoming slot's `engineDrive` is **snapped** to its computed value. This
  is legal and clickless by construction — the incoming slot is at zero crossfade gain at that instant
  (FR-024 step 1) — and it is *necessary*, because `Ĝ` spans 3–5 decades between engines (modal
  `Ĝ ≈ 4.8e4` vs the waveguide/comb `1/(1−g)` bounds) and a single shared smoother crossing that span would
  over-drive the incoming engine by tens of dB for tens of milliseconds, landing directly on SC-001's
  `getClampEngagementCount() == 0` clause and SC-012's clickless bound;
- the **outgoing** slot's `engineDrive` is frozen for the remainder of the fade: its input is muted at fade
  start (FR-024 step 3), so its drive has nothing to scale.

**Smoothed in log10, for continuous moves only.** For every move that is not a material assignment — glide
(FR-042), resonance/damping (FR-036), the AGC (FR-034), `setDrive` — the slot's drive is smoothed with an
`OnePoleSmoother` whose state is `log10(max(drive, kMinDriveGain))`, exponentiated on read, at
`kDriveSmoothMs = 50.0f`. A one-pole on the *linear* value spends most of its trajectory near the larger
endpoint, so the same 50 ms means something different at every scale; a one-pole on `log10` is a constant
dB/ms slope, so a decade and a factor of 1.1 both take 50 ms. The `max(·, kMinDriveGain)` guard keeps the
logarithm's argument strictly positive, which is also what `setDrive(0)` resolves to (−140 dB, inaudible).
`cloudDrive` uses the same mechanism and the same constant. `getDriveGain()` (FR-007) reports the sounding
slot's smoothed `engineDrive`.

**FR-034 — Input RMS tracking (roadmap line 210 verbatim: "input RMS tracking → resonator drive
compensation").** An `EnvelopeFollower` (`processors/envelope_follower.h:82`) in `DetectionMode::RMS`
(`setMode`, `:202`) with `setAttackTime(50.0f)` (`:220`) and `setReleaseTime(200.0f)` (`:227`) tracks the
mono-summed input, **advanced once per 64-sample control step**. Two things follow from that cadence and
both are requirements, not notes:

- **The follower is prepared at the control rate, not the audio rate:**
  `prepare(sampleRate / kControlChunkSamples, 1)` (`:106`). Its coefficients are derived per *call* from
  `sampleRate_` — `setAttackTime`/`setReleaseTime` → `updateAttackCoeff`/`updateReleaseCoeff`
  (`:222`, `:229`) → `calculateCoefficient(timeMs) = exp(−2π/(timeMs·0.001·sampleRate_))` (`:359-365`) —
  and `processSample` (`:164`) applies exactly one step per call. Preparing it at 48 kHz and then calling
  it once per 64 samples would stretch 50 ms into ~3.2 s and 200 ms into ~12.8 s, and SC-007's 1 s
  recovery clause would be unreachable.
- **It is fed the chunk RMS, not one sample of the chunk.** `ContinuousBody` computes
  `chunkRms = √(Σx²/N)` over the control chunk and passes *that* to `processSample`. `DetectionMode::RMS`
  squares its input and square-roots the smoothed result, so feeding equal-length chunk RMS values yields
  the true windowed RMS exactly (`√(mean of chunk mean-squares)` = `√(mean square)`). Handing it one raw
  sample per 64 would measure 1 sample in 64 and alias against the sine phase SC-007 uses.
- **The `Σx²` accumulator and its sample count carry across `processStereoBlock` calls** (FR-005a): `N` is
  always exactly 64 at the instant the follower advances, whatever partition the host used, and a sub-64
  tail at the end of a block advances nothing. Firing the follower on a 36-sample tail would make the
  host's block size audible — `processSample` advances the one-pole exactly once per call
  (`envelope_follower.h:164-188`) — and SC-011 would fail for a reason that is not a DSP defect.

`rmsGain = clamp(kTargetInputRms / max(rms, kRmsFloor), kMinRmsGain, kMaxRmsGain)` with
`kTargetInputRms = 0.25f`, `kRmsFloor = 1.0e-5f`, `kMinRmsGain = 0.05f`, `kMaxRmsGain = 4.0f`.
The bounded `kMaxRmsGain` is what stops the AGC winding up during silence and slamming the body when input
returns. `getInputRms()` exposes the tracked value.

**FR-034a — The AGC is switchable.** `void setInputAgcEnabled(bool) noexcept`, default `true`. When
`false`, `rmsGain` is held at exactly `1.0f` (the follower still tracks, so `getInputRms()` stays
meaningful) and the drive path is a pure gain. Spec-added, disclosed in A-7. It exists because SC-007's
"the compensation is a gain, not a compressor" clause is otherwise unmeasurable without editing the header
and recompiling, and because Phase 7 will want a raw-gain voice mode. The constants of FR-034 stay
`static constexpr` (FR-008) — this is a runtime bypass, not a mutable constant.

**FR-035 — Feedback-safe damping floors (roadmap line 211).** Three independent floors, all enforced by
`ContinuousBody` before the value reaches the engine:
- **Modal:** the effective `b1` after Resonance scaling is floored at `kMinB1 = 0.23f` s⁻¹
  (T60 = 6.91/0.23 ≈ 30.0 s, matching the roadmap's 30 s ceiling). The bank's own floor of
  `1/5` (`modal_resonator_bank.h:685`) remains the last line of defence and is never relied upon.
- **Waveguide:** T60 passed to `setDecay` is clamped to `[0.05, 10.0]` s. The ceiling is **10 s, not 30 s**:
  `WaveguideString::setDecay` itself does `decayTime_ = std::clamp(t60, 0.01f, 10.0f)`
  (`waveguide_string.h:142-145`), so anything above 10 s is silently truncated and a spec that claimed 30 s
  would be describing a value the component cannot hold. FR-036's Strings mapping tops out at 8.0 s, inside
  the clamp; FR-032's waveguide `Ĝ` and SC-003(c)'s ordering both use the post-clamp value. `rho < 1`
  strictly at every setting.
- **Comb:** per-comb feedback is clamped to `kMaxCombFeedback = 0.995f`, well inside the bank's own
  `±0.9999` clamp (`timevar_comb_bank.h:197`, applied at `:488`).

**FR-036 — Resonance and Damping, defined for all five materials.** Two user controls, one law each, with
a single unambiguous expression per engine. The earlier draft gave an *absolute* modal formula
(`b1 = kMinB1 + (1−r)(kMaxB1 − kMinB1)`) and then described it in prose as a multiplicative blend; those
are different operations, and the absolute one discards FR-013's per-material `b1` entirely — every modal
material would collapse to the same decay at every `r`, which contradicts roadmap line 217 and makes
SC-003(c) unsatisfiable by construction. The material term is therefore kept, and the *same* geometric
scale drives all three engines so there is exactly one Resonance law in the component:

```
scale(r) = kResonanceScaleAtZero ^ (1 − r)          // kResonanceScaleAtZero = 40.0f
```

`scale(1) = 1` (the material's own value) and `scale(0) = 40` (fast decay). It is strictly decreasing in
`r` for every material, so **monotonicity is a property of the law, not a hope**.

| Engine | Applied as | Clamp |
|---|---|---|
| Modal | `b1_eff = clamp(b1_material · scale(r), kMinB1, kMaxB1)` | `kMinB1 = 0.23f`, `kMaxB1 = 30.0f` |
| Waveguide | `T60_eff = clamp(t60AtMaxResonanceSec / scale(r), 0.05f, 10.0f)` → `setDecay` | the component's own `[0.01, 10]` never binds |
| Comb | `T60_eff = t60AtMaxResonanceSec / scale(r)`, then per comb `fb_n = min(10^(−3·τ_n/T60_eff), kMaxCombFeedback)` with `τ_n = 1/(f_body·√(1 + n·spread))` s, the bank's own Inharmonic tuning law (`timevar_comb_bank.h:236-237`) | `kMaxCombFeedback = 0.995f` |

Resulting T60, computed from FR-011a's `t60AtMaxResonanceSec` (modal: `6.91/b1_eff`):

| `r` | Metal Plate | Glass | Ice | Strings | Chamber |
|---|---|---|---|---|---|
| 0.0 | 0.58 s | 0.35 s | 0.29 s | 0.20 s | 0.063 s |
| 0.8 (`scale = 2.091`) | **11.0 s** | **6.61 s** | **5.51 s** | **3.83 s** | **1.20 s** |
| 1.0 | 23.0 s | 13.8 s | 11.5 s | 8.0 s | 2.5 s |

The `r = 0.8` row is exactly the ordering SC-003(c) asserts — `MetalPlate > Glass > Ice > Strings >
Chamber` — and it is now *derived from* the profile values rather than hoped for. No clamp binds anywhere
in the table (`b1_eff` spans 0.30 … 24.0, inside `[0.23, 30.0]`), so the ordering is strict at every `r`.

`setDamping(float d)`, `d ∈ [0,1]`, darkens without moving mode frequencies, again per engine:

| Engine | Applied as |
|---|---|
| Modal | `b3_eff = b3_material · (1 + kDampingB3Scale·d)`, `kDampingB3Scale = 32.0f` |
| Waveguide | `S_eff = S_material + d·(0.45 − S_material)` → `setBrightness(2·S_eff)` (correction **C-6**: the argument darkens). **The `retune()` that follows must be solved at a FIXED reference brightness** — see below. |
| Comb | `damping_eff = min(damping_material + d·(1 − damping_material), kMaxCombDamping)`, `kMaxCombDamping = 0.95f` → `setCombDamping(n, damping_eff)` |

Two implementation-measured corrections to this table (2026-07-27), both of which made a *documented user
control do nothing or run backwards*:

- **`kMaxCombDamping = 0.95` is load-bearing; `d = 1` is degenerate, not "maximally damped".** The comb's
  damper is the one-pole `LP(x) = (1−d)·x + d·LP_prev` fed from its own output (`comb_filter.h:346-347`).
  At `d = 1` the input term vanishes, the pole sits exactly on the unit circle, the filter **freezes** on
  its last sample, and the comb degenerates to `y = x + fb·constant` — a DC offset with no resonance.
  Measured on Chamber at `damping = 1` with the SC-003 excitation before the ceiling: the whole output
  collapsed into 20–100 Hz (band-energy fraction **1.000**, centroid **24 Hz** against 5650 Hz at
  `damping = 0`). This is the exact counterpart of the `kMaxCombFeedback = 0.995` clamp one term over.
- **The waveguide's `retune()` (RA-1) must be handed a FIXED reference brightness, not `S_eff`.** The
  string's round-trip HF loss is the product of *two* one-zero filters: the loss filter `(1−S) + S·z⁻¹`
  and the **linear fractional-delay interpolator** `(1−frac) + frac·z⁻¹`, which is the same form. What is
  audible is set by `S + frac`. `retune()` solves the exact resonance condition
  `D = period − 1 − dLoss(S) − dDisp − dDC` with `dLoss(S) ≈ S` samples, so `frac = frac(D_raw − S)` and
  `S + frac = D_raw − floor(D_raw − S)` — **independent of `S`** anywhere inside one integer step, and
  FR-036's whole span for Strings is 0.30 samples. The cancellation is algebraic. Measured at the
  SC-003(b) settings: centroid `d = 0` **5575 Hz** → `d = 1` **5565 Hz** (−0.2 %, and the HF band moved the
  *wrong* way) — a dead control, and the string's timbre instead tracked `frac(D_raw)`, i.e. jittered with
  pitch. With `retune()` solved at the midpoint of the material's own damping span the same sweep measures
  6237 → 5208 Hz (**−16.5 %**), monotone at every step. `WaveguideString::retune` itself is unchanged — it
  remains the exact budget for the brightness it is *handed*, so RA-1's own SC-009c table still holds; the
  cost is that the realised pitch is off by `(S_eff − reference)` ≤ 0.15 samples of loop length
  (1.2 cents at 220 Hz, 4.8 at 880), inside SC-009's 5-cent bound, which names only the three modal
  materials in any case.

`kDampingB3Scale = 32` rather than the earlier `8` so the control is measurable on Metal Plate too, whose
`b3` is deliberately tiny: at `d = 1` its top mode's `b3f²` reaches 0.20 s⁻¹ against `b1_eff = 0.63` at
`r = 0.8` — ~30 % of the decay rate, comfortably above measurement noise. Both controls must be
**monotone**: increasing `r` may never shorten the measured T60, increasing `d` may never raise the
measured spectral centroid (SC-003b).

**FR-037 — Output safety.** The engine sum is clamped to `±kOutputClamp = 2.0f` after the crossfade mix
(the `harmonic_cloud.h:935-936` idiom) and before the decay cloud. This is a last-resort guard, not the
level-control mechanism — SC-001 requires the signal to stay bounded *without* the clamp engaging in normal
operation, measured by `getClampEngagementCount()`, whose exact signature, counting unit and reset
semantics are pinned in FR-007's table (samples, not blocks; cleared by `reset()`/`prepare()` only).

**FR-038 — Non-finite *input* is substituted, never punished.** If a control-step's input chunk contains a
non-finite sample (detected by IEEE-754 exponent bit pattern, never `std::isnan` — the macOS leg builds
with `-ffast-math`), the **whole chunk is replaced by zeros** before it reaches the drive stage, and
processing continues normally. The active engine is **not** `silence()`d and its ring is preserved: a
poisoned host buffer is an input-hygiene event, and destroying a ringing 23 s Metal Plate in response is an
instantaneous output step at whatever amplitude the engine held — a click, and precisely the
"destroy audible state" that FR-024a's collapse rule exists to prevent. The chunk RMS accumulator (FR-034)
also takes the zeros, so the AGC sees a gap rather than a NaN. Recovery on the next finite chunk is
therefore automatic and complete: nothing was reset. This mirrors `TimeVaryingCombBank::processStereo`'s
own guard (`timevar_comb_bank.h:664-669`), which likewise substitutes rather than clears.

**FR-038a — Non-finite *state* is the only thing that silences an engine.** Input hygiene (FR-038) and
state recovery are two different mechanisms. Once per control step, after the engines have been advanced,
`stateFinite()` (FR-007, bit-pattern check) is evaluated. If it reports **false**:

1. the sounding engine (and, during a crossfade, both) is ramped to zero over `kSlotReleaseMs = 10 ms`
   with the equal-power law — the same mechanism as FR-024a's collapse and FR-063's bypass, so the
   recovery is itself clickless;
2. at zero gain the affected engine is `silence()`d, and the decay cloud's delay line, `DiffusionNetwork`,
   damping filter and DC blocker are cleared if the cloud's own state is what went non-finite;
3. the engine is re-entered with a `kSlotReleaseMs` ramp back to unity once `stateFinite()` reports true
   again — never as a step.

A `silence()` therefore only ever happens at zero gain, in this component as everywhere else in it. The
event is observable through `stateFinite()` returning true again on the following control step, which is
what SC-013 asserts.

### FR-040 series — key tracking and retune (roadmap lines 214–215)

**FR-040 — Body pitch law.** `setNoteFrequencyHz(float hz)` supplies the voice pitch;
`setKeyTracking(float amount)` with `amount ∈ [0,1]`. The body fundamental is interpolated in the
**log-frequency** domain, because pitch is geometric:
```
f_body = referenceHz · (clamp(noteHz, 20, 8000) / referenceHz) ^ keyTracking
```
`keyTracking = 0` → `f_body = referenceHz` (fixed body, like a real instrument — roadmap line 215);
`keyTracking = 1` → `f_body = noteHz` (fully tracked). Accuracy asserted by SC-009.

**FR-041 — Retune is state-preserving.** A change to `f_body` never clears engine state:
- Modal: `updateModes()` (`modal_resonator_bank.h:264-275`), followed by `snapCoefficients()` (RA-5).
- Comb: `setFundamental()` (`timevar_comb_bank.h:239`), followed by `snapSmoothers()` (RA-5).
- Waveguide: `retune()` (FR-080), **not** `noteOn()`, followed by `snapSmoothers()` (RA-5).

Continuity is `ContinuousBody`'s own FR-009 smoothers sampled on the 64-sample control grid, **not** each
sub-component's second per-sample smoother — see **RA-5** for the two defects the second smoother caused
(SC-011 block-size dependence measured at 7.1e-2 against a 1e-4 bound, and the SC-005 overage) and for
the bound on the residual step.

**FR-042 — Retune cadence.** `f_body` is recomputed at most once per 64-sample control step, and the modal
path additionally skips the `updateModes()` call when the new `f_body` differs from the applied one by less
than `kRetuneEpsilonCents = 0.5f` (the dirty-flag lever `HarmonicCloud` uses at `harmonic_cloud.h:255-257`
and `:1252`, adapted). This keeps a 1 s glide from costing 48,000 full mode recomputes.

**FR-042a — Resonance/damping cadence and dirty gate.** `setResonance`/`setDamping` carry **no setter-level
smoother** (FR-006's first exception): the values are latched by the setter and applied at the control step.
Clicklessness comes from the **control grid itself** — the apply is gated below, the step is bounded by
that gate, and the coefficients are snapped onto their targets (**RA-5**) rather than chased by the modal
bank's own 2 ms smoother. That smoother is a second one in series with `ContinuousBody`'s and it runs
once per `processBlock` *call*, which is what made the render depend on the host's block size (RA-5,
measured 7.1e-2 against SC-011's 1e-4 bound). Adding a *setter-level* smoother on top would buy nothing
either, and would guarantee a *continuously* moving `b1`/`b3`, i.e. a full `updateModes()` on every
control step forever.

The apply is gated exactly as FR-042 gates pitch, on a **relative** threshold because `b1` and `b3` differ
by eight orders of magnitude between materials:

```
dirty = |b1_new − b1_applied| > kDampingEpsilonRel · max(b1_applied, kMinB1)
     || |b3_new − b3_applied| > kDampingEpsilonRel · max(b3_applied, kB3Floor)
```

with `kDampingEpsilonRel = 0.005f` (0.5 %) and `kB3Floor = 1.0e-12f` guarding the division-free form
against a zero reference. The pitch-dirty flag (FR-042) and this damping-dirty flag are **OR-ed into one**
`updateModes()` call, so a control step costs **at most one** mode rewrite — `computeModeCoefficients` runs
a `sqrt`, two `sin` and an `exp` per mode (`modal_resonator_bank.h:726`, `:729`, `:743`, `:746`), ≈ 128
transcendentals for a 32-mode bank, which is the single largest lever on SC-005's *operating point*
configuration ("every setter stepped once per 64-sample control chunk"). With the gate, a static parameter
costs zero rewrites and a slow sweep costs one every few control steps rather than one per step.

Monotonicity (SC-003b) is unaffected: the gate only defers an apply until the accumulated change exceeds
0.5 %, and 0.5 % of `b1` is far below the 5 % T60 measurement tolerance SC-003(b) allows per step.
The waveguide and comb paths use the same gate on their engine-native quantities (`T60_eff`, `S_eff`,
`fb_n`, `damping_eff`), since `setDecay`/`setCombFeedback` are cheap but not free.

**FR-043 — Nyquist safety by truncating the count, not by zeroing amplitudes.** Zero-amplitude modes are
not free: `processBlock` passes `numModes_` to the SIMD kernel (`modal_resonator_bank.h:362-364`) and
`flushSilentModes` only decrements `numActiveModes_` (`:383-396`), so a mode above the guard would still
cost a SIMD lane for the life of the note. Instead:

- At **material assignment**, the configured count is
  `min(defaultModeCount, #{leading table entries whose frequency at 2·f_body is below kNyquistGuard·fs})`,
  i.e. one octave of glide headroom (`kNyquistHeadroomOctaves = 1.0f`; `kNyquistGuard = 0.49`,
  `modal_resonator_bank.h:567`). The tables are strictly increasing (FR-012), so this prefix truncation is
  exact. Worked: Glass at `f_body = 220` Hz / 48 kHz gets ~13 modes; at `f_body = 55` Hz it gets all 32.
  This is also why SC-005's per-material timings differ for a reason other than engine choice.
- On **retune inside the headroom window**, the count is unchanged and the bank's own cull
  (`:732-738`) silences any mode that crosses the guard — correct behaviour, at the cost of a few idle
  lanes, and reversible when the pitch comes back down.
- On **retune outside the window**, the count may only ever *increase* mid-ring (a decrease would truncate
  `numModes_` and drop ringing state instantaneously — a click). A decrease is deferred to the next
  material assignment, where `setModes` clears state anyway.

`f_body` itself is clamped to `[20, 8000]` Hz.

### FR-050 series — slow decay cloud (roadmap lines 212–213)

**FR-050 — Topology.** A stereo feedback loop containing in order:
`(input + fb·damped-feedback)` → `DelayLine` (`primitives/delay_line.h`) → `DiffusionNetwork`
(`processors/diffusion_network.h:161`, `process(const float*, const float*, float*, float*, size_t)` at
`:327-329`) → one-pole lowpass damping (`primitives/one_pole.h`) → DC blocker → feedback tap.
`DiffusionNetwork` is prepared with `prepare(float sampleRate, size_t maxBlockSize)` (`:197` — note the
`float` first parameter, unlike every other component's `double`) and configured with `setSize`,
`setDensity`, `setWidth`, `setModDepth`, `setModRate` (`:273-305`).

**The loop is evaluated on the 64-sample control chunk, not per sample.** The shortest loop delay is 37 ms
= 1776 samples at 48 kHz (296 even at 8 kHz), so reading a whole 64-sample chunk out of the delay line
before writing the processed result back is causal by a wide margin; `prepare()` asserts
`min(loop length in samples) ≥ kControlChunkSamples`. `DiffusionNetwork::process` is therefore called with
`numSamples = 64`, not 1 — it has no per-sample entry point and per-sample use would pay call overhead
on every sample.

`setModDepth(0.0f)` is the default, and RA-4's guard makes that free: without it the network evaluates
8 `std::sin` per sample per instance (correction C-7), which alone consumes most of the SC-005 budget.
`setDensity` defaults to `kCloudDensity = 100.0f` (all 8 stages, `:353-356`).

**FR-051 — Loop lengths and buffer sizing.** `kCloudLoopMsL = 37.0f`, `kCloudLoopMsR = 41.0f` — mutually
near-coprime so the two channels decorrelate. The `DelayLine` in each channel is sized in `prepare()` for
its own `kCloudLoopMs` **only**. `DiffusionNetwork` allocates its own per-stage buffers in its `prepare()`
(≈16.9 ms each, `:202-205`), and the cascade's ~57–64 ms of *throughput* delay is distributed across those
stages — it needs no additional buffer in `ContinuousBody`. The two figures are different quantities and
must not be added into one buffer size (correction **C-3**).

**FR-052 — RT60 mapping from the *measured* loop time.** `setCloudDecaySec(float seconds)` with
`seconds ∈ [0.1, 30.0]` (roadmap line 213: "decay times up to 30 s"). The feedback gain must be derived
from the **total** time round the loop, which is the delay line *plus* the diffusion cascade — deriving it
from the delay line alone under-damps by the ratio of the two, ~2.5×, and no ±15 % accuracy criterion can
survive that. Per channel:

```
cascadeSec_ch = kBaseDelayMs·1e-3 · cloudSize · Σ kDelayRatiosL · kCascadeDelayFactor
                                              · (ch == R ? kStereoOffset : 1)
loopSeconds_ch = kCloudLoopMs_ch·1e-3 + cascadeSec_ch
fb_ch = min(pow(10.0f, -3.0f * loopSeconds_ch / seconds), kMaxCloudFeedback)
```

> **`kCascadeDelayFactor = 1.32` added 2026-07-28 (OQ-A), under this FR's own escape clause.** The
> paragraph below already prescribes, for exactly this case, "calibrate `fb` against a measured tail at
> configure time, never widen SC-008" — this is that calibration, expressed as the one scalar the formula
> was missing rather than as a widened criterion. It does **not** claim the cascade is longer than
> `Σ kDelayRatiosL` says: the cascade's measured energy centroid is 56.83 ms against the formula's
> 56.886 ms at `cloudSize = 1`. It corrects for the fact that a traversal is a *random* time (each
> Schroeder allpass emits at `0, D, 2D, …`; eight in series give a measured σ of 25.7 ms, 27 % of the
> nominal loop), and a loop with dispersed traversal time decays slower than its mean predicts. At
> `factor = 1.0` the measured T60 error runs `+31.4 / +10.9 / +6.1 / +6.0 %` over the 0.5 / 2 / 10 / 30 s
> grid — the first point outside SC-008's ±15 %; at 1.32 it is `+10.2 / −5.6 / −10.9 / −11.2 %`, all
> inside. The `cloudSize = 0` column is untouched (the cascade term is multiplied by `cloudSize`), so
> SC-008 still discriminates against a `fb` derived from the delay line alone. Consequence, recorded:
> `getCloudLoopSeconds()` reports the **decay-effective** loop time, not the mean acoustic one; the two
> differ by this factor and `continuous_body.h`'s banner on the constant carries the full grid.

`Σ kDelayRatiosL = 17.777` (`diffusion_network.h:51-53`); a Schroeder allpass of delay `D` has mean group
delay exactly `D`, so the cascade's mean delay is the sum of its stage delays (`:366-374`). At
`cloudSize = 1.0`: `cascadeSec_L = 56.9 ms`, `cascadeSec_R = 64.1 ms`, so `loopSeconds_L = 93.9 ms`,
`loopSeconds_R = 105.1 ms`. `loopSeconds` is recomputed in `prepare()` and on every `setCloudSize()` (the
network bypasses below `size < 0.001`, `:344`, at which point `cascadeSec = 0` and the formula degrades
correctly to the plain-delay case). `kMaxCloudFeedback = 0.9995f`; at `seconds = 30` and
`loopSeconds_L = 0.0939`, `fb ≈ 0.97862` — far inside the cap, so the loop is provably contracting at every
setting. `getCloudFeedbackGain()` and `getCloudLoopSeconds()` expose both quantities.

The residual (one-pole damping and DC-blocker group delay, allpass interpolation) is not modelled; SC-008's
±15 % is the budget for it. Two scoping statements, so the criterion is honest rather than lucky: the
accuracy claim binds at decay settings ≥ 4× `loopSeconds` (SC-008's grid starts at 0.5 s, ≈ 5 loop
traversals); and if the measured T60 misses, the prescribed response is to **calibrate `fb` against a
measured tail at configure time**, never to widen SC-008.

**FR-053 — Cloud controls.** `setCloudMix(float)` `∈ [0,1]` (parallel blend of cloud against the dry
resonator output, equal-power via `crossfade_utils.h:64`); `setCloudDamping(float)` `∈ [0,1]` mapping the
in-loop lowpass cutoff from 18 kHz down to 800 Hz; `setCloudSize(float)` `∈ [0,1]` forwarded to
`DiffusionNetwork::setSize` as a percentage (the network takes 0–100, `:167-169`, and bypasses below
`size < 0.001`, `:344`).

**FR-053a — The cloud is bypassable, and bypasses itself when silent.** The decay cloud is the one stage
that is otherwise always on, so it must be able to cost nothing: when `cloudMix < kCloudBypassEpsilon`
(`1.0e-4f`) **and** the loop's own energy is below `kCloudSilenceFloor` (`1.0e-6f` peak over the previous
control chunk), the entire cloud path — delay line, `DiffusionNetwork`, damping, DC blocker — is skipped
for that chunk and the dry resonator output is passed through. Re-entry when `cloudMix` rises is ramped by
`setCloudMix`'s own smoother (FR-006), so the bypass can never click. SC-005 measures the cloud as its own
line item (`setResonatorBypass(true)`, FR-063) precisely so this cost is visible rather than folded into a
single number.

**FR-054 — The cloud never self-oscillates.** With `kMaxCloudFeedback = 0.9995` and an in-loop lowpass plus
DC blocker, loop gain is strictly < 1 at every frequency at every setting. SC-001 covers this jointly with
the resonator.

**FR-055 — Boundary against Phase 6.** The decay cloud has no pitch shifting, no spectral stage, no
freeze/unity-feedback mode, and no cross-channel matrix. Those are `AetherReverb` (roadmap lines 256–272)
and adding any of them here would violate N-3.

### FR-060 series — output stage

**FR-060 — Mix.** `setMix(float)` `∈ [0,1]`, equal-power: 0 = the input passed through unchanged,
1 = body+cloud only. Default 1.0.

**"body+cloud only" is a requirement on the ENGINE output, and two of the three engines leak the dry input
on their own** (measured 2026-07-27; the subtraction is applied in `advanceSlot`). `FeedbackComb` is
`y = x + g·LP(y[n−D])` (`comb_filter.h:352`) and the bank sums six of them at unity gain
(`timevar_comb_bank.h:360`, `:646`), so it returns `6·x` plus what it resonated; `WaveguideString::process`
taps the summing junction, `softClip(feedback + excitation)` (`:178-181`), so it returns `x` plus what it
resonated. The modal bank has no such term. Left in, the leak dominated: at the SC-003 excitation the
Chamber output measured spectral flatness **0.79** and centroid **10173 Hz** against the raw excitation's
10999 Hz — i.e. it was mostly *un-resonated input* — and Strings 0.58 / 8200 Hz. It also broke SC-003(b)
outright, because damping has no purchase on a dry passthrough: Chamber's centroid *rose* 8 % across the
Damping sweep. The engine output is therefore `engine(x·drive) − (dryGain·x·drive)` with `dryGain = 6` for
the comb bank and `1` for the string. FR-032's `Ĝ` remains a valid bound — the subtraction takes the
realised gain from `Σ 1/(1−fb_n)` to `Σ fb_n/(1−fb_n)` and from `1/(1−g)` to `g/(1−g)`, strictly *down*. This is the *mix* half of the "selector + mix pattern from the Innexus
roadmap" that roadmap line 208 mandates (A-4, `Innexus-physical-modelling-roadmap.md:62` and `:1775`); it
appears in the roadmap-coverage table under line 208 and is not a spec addition.

**FR-061 — deleted.** An output trim (`setOutputGainDb`) was specified in an earlier draft and is removed:
roadmap Phase 4 (lines 198–222) does not ask for one, Phase 7 owns the output stage (roadmap line 290), and
a user-settable ±dB gain sits directly on SC-007's measurement path. There is no level control in this
component. SC-007 is measured on the component's raw output, and there is no trim to set to 0 dB first.

**FR-062 — Stereo re-expansion.** The mono resonator output is written to both channels before the decay
cloud; the cloud's own stereo decorrelation (FR-051) plus `DiffusionNetwork::setWidth` (`:288`) supplies
the width. `setWidth(float)` `∈ [0,1]` forwards to it.

**FR-063 — Resonator bypass.** `void setResonatorBypass(bool) noexcept`, default `false`. When `true`, the
mono-summed input is scaled by FR-033's **`cloudDrive = rmsGain · userDrive`** — the `1/Ĝ` term is dropped,
because `Ĝ` bounds a resonator's steady-state gain and there is no resonator in the path — and feeds the
decay cloud directly, and **no** resonator engine is advanced (`getEngineSampleCount` stays flat for every
engine). The halo is therefore at normal level: SC-008 regresses a tail that starts near full scale rather
than 94 dB down, and Phase 7's body-less halo needs no external makeup gain. Toggling it ramps over `kSlotReleaseMs = 10 ms` with
the equal-power law and `silence()`s the engine at zero gain, exactly as FR-024a's collapse does — so the
toggle is clickless and never destroys audible state. Spec-added, disclosed in A-7. Two reasons it earns
its place: SC-008 cannot measure the cloud's RT60 through a body whose own T60 reaches 23 s, and SC-005
cannot attribute a cloud cost without it. Phase 7 gets a body-less halo mode for free.

### FR-070 series — determinism

**FR-070 — Seeding.** `void setSeed(std::uint32_t seed) noexcept`. Every stochastic element derives its
stream from this base via `deriveStreamSeed(std::uint32_t, std::size_t)` (`core/random.h:102`), so Phase 7
can give each voice a distinct seed and get non-identical drift (roadmap line 289).

**FR-070a — What the seed actually drives (because nothing else in this phase is stochastic).** Every
engine as this spec configures it is deterministic: the modal bank's scatter is a fixed golden-ratio
displacement, not RNG (`modal_resonator_bank.h:577-578`, `:729`); `DiffusionNetwork`'s modulation is a
deterministic sine LFO (`:362`) and is off by default (FR-050); `WaveguideString`'s RNG feeds only the
note-on noise burst, which FR-022c injects at velocity 0 — `velScale = velocity · excitationGain_ = 0`
(`waveguide_string.h:393`, consumed at `:446`), so the buffer written is entirely zero; and the comb bank's
RNG is hard-seeded (FR-071). A `setSeed` with no consumer would be inert and SC-010's anti-vacuity clause
would have nothing to apply to. The seed therefore drives one concrete, audible element:

**Per-voice modal micro-detune.** At `setModes`/`updateModes` time, mode `k`'s frequency is multiplied by
`exp2(j_k · kSeedDetuneCents / 1200)` where `j_k ∈ [−1, 1]` comes from an `Xorshift32`
(`core/random.h:41`, `nextFloat()` at `:59`) seeded with `deriveStreamSeed(seed, k)`, and
`kSeedDetuneCents = 3.0f`. It is recomputed only when the seed or the mode set changes — configure-time
cost only — and it is physically motivated: no two glass bowls are identical, and 16 voices with
bit-identical bodies phase-lock into a single artificial object. It applies to the **three modal
materials only**; Strings and Chamber are documented seed-independent (FR-071), and SC-010 asserts that
asymmetry in both directions rather than papering over it.

**FR-071 — Known limitation, recorded not worked around.** `TimeVaryingCombBank` seeds its per-comb
`Xorshift32` from the **hard-coded** `12345u + i*7919u` in both `prepare()` and `reset()`
(`timevar_comb_bank.h:429`, `:450`) and exposes no seed setter. The Chamber material's comb drift is
therefore identical across voices. Phase 4 does **not** amend `TimeVaryingCombBank` for this — it is
cosmetic at this stage and an amendment would exceed the phase's scope. Recorded so Phase 7 knows to
decorrelate Chamber voices by other means (per-voice `setModPhaseSpread`, `:285`) or to raise it then.
SC-010 asserts the resulting seed-*independence* rather than ignoring it.

**FR-072 — Determinism guarantee.** Same seed + same sample rate + same parameter sequence ⇒ identical
render, within `render_fingerprint.h` tolerances (never bit-exact — root `CLAUDE.md`; SC-010).

### FR-080 series — `WaveguideString::retune()` (RA-1)

**FR-080 — New method.**
```cpp
/// Retune the loop without clearing state or re-exciting.
void retune(float f0) noexcept;
```
It recomputes `bridgeDelayFloat_` from the loop's **exact resonance condition** —
`D = period − 1 − dLoss − dDisp − dDC`, every term evaluated at the new `f0`, with `period = sr/f0` — and
clamps it identically to `noteOn()` (`:325`). It does **not** touch `nutSideDelay_`, `bridgeSideDelay_`,
`dcBlocker_`, `dispersionFilters_`, `lossState_`, the energy followers, or
`frozenStiffness_`/`frozenPickPosition_`.

> **Amended 2026-07-27 (build stage), on measurement.** This clause originally required the **same**
> empirical expression `noteOn()` uses, `D = period − 1 − 0.55·dLoss − 0.96·dDisp` (`waveguide_string.h:321`),
> which omits the DC blocker's phase delay. Implemented verbatim it **fails SC-009c** — measured
> +5.26 cents at −12 st and −8.18 cents at +12 st (48 kHz, Strings material settings, SC-009's estimator).
> `noteOn()`'s factors are calibrated for a cascade re-designed at every onset, and FR-081 freezes the
> cascade: the unmodelled 4 % of `dDisp` stops tracking the pitch (frozen `dDisp` ≈ 14.2 samples where a
> matched one spans 7.7…27.7), and the omitted `dDC` grows as `1/f²` (−0.14 samples at 440 Hz, −2.21 at
> 110 Hz). The exact budget meets SC-009c with margin — worst case **1.50 cents** over the same sweep, and
> ≤ 3.9 cents across 44.1/48/96 kHz × stiffness 0…1 × brightness 0…1. `noteOn()` is **not** touched
> (FR-084/SC-014), so `retune(f)` and `noteOn(f)` differ by 0.56 cents at the same pitch, in `retune()`'s
> favour. Derivation and the full measurement tables live in the comment above `retune()` in
> `waveguide_string.h` and in `waveguide_string_retune_test.cpp`.

**FR-081 — Dispersion filters are not reconfigured.** `configureDispersionFilters(B, f0, sr)` is called
only from `noteOn()` (`:299`). `retune()` leaves the frozen dispersion cascade in place: reconfiguring
biquads mid-ring changes their state meaning and clicks. The consequence is that the cascade's phase delay
no longer tracks `f0`, which is exactly why FR-080's budget must compensate the cascade's **actual**
measured phase delay rather than `noteOn()`'s empirically-scaled one (see FR-080's amendment note). With
the exact budget the residual pitch error over the ±12 semitone range this phase requires is ≤ 1.5 cents,
inside SC-009's ±5 cent bound.

**FR-082 — Guards.** `retune()` is a no-op when `!prepared_` or `f0 < kMinFrequency` (`:49`, 20 Hz),
matching `noteOn()`'s own guard (`:275-276`). `f0` is clamped to `[kMinFrequency, sampleRate·0.45]` exactly
as `setFrequency` does (`:136-137`).

**FR-083 — Smoother coherence.** `retune()` also calls `frequencySmoother_.setTarget(std::log2(f0))` so
the loss-filter path (`:161-167`) converges on the same pitch rather than diverging from the delay length.
It uses `setTarget`, not `snapTo` — `noteOn()` snaps (`:288`), which is correct for a new note and wrong
for a glide.

**FR-084 — Inertness.** No existing `WaveguideString` member changes behaviour. A build that never calls
`retune()` is functionally identical. SC-014 asserts this against the unedited existing suites.

### Roadmap component coverage (completeness check)

| Roadmap statement (line) | Covered by |
|---|---|
| New Layer 3 component at `systems/continuous_body.h` (204) | FR-001, FR-003 |
| Composes `ModalResonatorBank[Simd]` (206) | FR-020, FR-021, FR-022a (C-1) |
| + `WaveguideString` (207) | FR-020, FR-022c, FR-080 series |
| + `TimeVaryingCombBank` (207) | FR-020, FR-021, FR-022b (C-2) |
| Material selector with crossfade (207) | FR-014, FR-024, FR-024a |
| "selector + **mix** pattern from the Innexus roadmap" (208) — the *mix* half (`Innexus-physical-modelling-roadmap.md:62`, `:1775`) | FR-060 (A-4) |
| "only active modules burn CPU" (208) — the *selector* half | FR-023 (functional, via `getEngineSampleCount`), FR-053a, SC-005 |
| Continuous-excitation adapter (208–210) | FR-031, FR-032, FR-033 |
| Energy normalization: input RMS → drive compensation (210) | FR-034 |
| Feedback-safe damping floors (211) | FR-035, FR-036 |
| Slow decay cloud, `DiffusionNetwork`, up to 30 s (212–213) | FR-050 – FR-054 (C-3, RA-3) |
| Body tuning follows the voice, key-tracking 0–1 (214–215) | FR-040, FR-041 |
| Materials Glass/Strings/Metal Plate/Chamber/Ice (216) | FR-010, FR-011, FR-011a (all five valued in full) |
| Mode-ratio table + frequency-dependent damping law (216–217) — modal materials | FR-012, FR-013 (A-6) |
| …the same, for the two non-modal materials | FR-013a (engine-native laws, valued) |
| Stability under sustained full-scale input at max resonance (219–220) | SC-001, SC-002 |
| Material A/B renders (220) | SC-003 |
| Retune smoothness under glide (220–221) | SC-004 |
| CPU ≤ 1% per voice with body active (221) | SC-005 (binding at compile time *and* run time; crossfade window inside the same 1%) |
| Continuous-excitation gain bound is real, not asserted | SC-015 |
| RT safety, no allocation after prepare (485) | FR-002, SC-006 |
| Layer discipline (486) | FR-003 |
| ODR sweep (487) | New Components table |
| CPU budgets are FRs (488–489) | SC-005 |
| No bit-exact float goldens (490) | FR-072, SC-010 |
| Portability (491–492) | SC-013 |

---

## Success Criteria

Every criterion names its metric, its threshold, and the test that measures it. Test names are sketches for
the plan; the plan may rename, but may not weaken a threshold.

**"Steady state", defined once and used by SC-007 and SC-015.** The settling time of the quantity under
test *is* the T60 under test — up to 23 s (Metal Plate at `r = 1.0`, FR-036), so a fixed window either
fails a correct implementation or measures a rising envelope. The definition is therefore **self-sizing**:

> Render for `max(5.0 s, 3 × getEngineT60Sec())` at the configuration under test, then take the **mean of
> the per-block peak magnitude over the final 1.0 s** of that render. That mean is "the steady-state peak".

`getEngineT60Sec()` is already in FR-007's introspection surface, so the render length is derived from the
component under test rather than guessed, and it is per-configuration: the `r = 0.2` case runs 5 s while
the `r = 1.0` Metal Plate case runs 69 s. `3 × T60` leaves the envelope within 0.1 % of its asymptote, so
the absolute-level clause of SC-007 (−20 dB … +3 dB of `kTargetPeak`) keeps its meaning instead of
measuring a transient. SC-001 keeps its own explicitly-stated windows (final 1 s vs seconds 9–10) and is
not restated by this definition.

**SC-001 — Bounded under sustained full-scale drive at maximum resonance** *(roadmap lines 219–220)*.
For **each** of the five materials, at `setResonance(1.0)`, `setDrive(4.0)`, `setCloudDecaySec(30.0)`,
`setCloudMix(1.0)`, at 48 kHz: drive 60 s of full-scale (peak 1.0) input — three signals per material:
(a) white noise, (b) a sine at the body fundamental, (c) a sine swept 20 Hz→8 kHz. Then:
- peak output magnitude over the whole render `≤ 1.5`;
- RMS of the final 1 s `≤ 1.10 ×` RMS of seconds 9–10 (no growth), **and** the least-squares slope of
  `log10(RMS)` over four consecutive 15 s windows is `≤ +0.025` per window (i.e. ≤ +0.5 dB/window,
  a tolerance that absorbs the window-to-window fluctuation of noise and swept excitation without
  admitting an actual upward trend);
- `stateFinite()` true throughout, sampled every block;
- `getClampEngagementCount()` (FR-007) is **0** across the whole render for **all three** signals,
  including (b) the sine at the body fundamental. There is no carve-out for the resonant worst case: it is
  the one excitation FR-031 exists for, and a criterion that exempts it cannot tell working drive
  compensation from permanent hard clipping. The arithmetic says 0 is reachable — FR-032's `Ĝ` sums over
  all modes while a single sine excites essentially one, so the compensated steady state lands *below*
  `kTargetPeak`, not above `kOutputClamp = 2.0`. The one configuration where the clamp *may* engage
  (`userDrive` and `rmsGain` both at maximum) is outside this parameter set and is covered in Edge Cases.
- the counter is read cumulatively and bracketed by subtracting the value read at the start of the render
  (FR-007 pins it as cleared only by `reset()`/`prepare()`).
*Test:* `ContinuousBody_SustainedDriveBounded` (parameterised over material × signal).

**SC-002 — Decays to silence when the input stops** *(the Membrum infinite-ring pattern, roadmap line 220)*.
Following each SC-001 render, feed exactly zero input and continue rendering. Peak magnitude in the final
block must fall below `1.0e-4` (−80 dBFS — the same threshold as
`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59`) within
**`cloudDecaySec + 6.91/kMinB1 + 5 s` = 30 + 30 + 5 = 65 s**, applied uniformly to all five materials.
The bound is stated from constants rather than from a per-material T60 so that it is computable before the
render: the earlier draft's `modalT60` is undefined for Strings and Chamber. `getEngineT60Sec()` (FR-007)
exists and may be used to tighten the bound per material, but 65 s is the criterion.
Repeated for a material-switch sequence: hit all five materials in
rapid succession under sustained input — including **three modal materials inside one 500 ms crossfade
window** (Glass → Ice → Metal Plate), the case FR-024a's collapse rule exists for — stop the input, assert
the same bound. The direct analogue of Membrum's kit-switch reproduction recipe.
*Test:* `ContinuousBody_DecaysToSilence`, `ContinuousBody_MaterialSwitchNoInfiniteRing`.

**SC-003 — Materials are measurably distinct, and in the intended direction** *(roadmap line 220,
"material A/B renders")*. Identical excitation (2 s of band-limited noise at fundamental 220 Hz,
`keyTracking = 1`, `cloudMix = 0`) rendered through each material:
**The analysis pipeline, stated once and used by (a) and (c).** Take the last 1.0 s of the render; one
8192-point Hann-windowed FFT; discard DC; the remaining 4096 bins are converted to dB relative to that
render's own peak bin and clamped at a **−80 dB floor**, then mapped to `[0,1]` as `(dB + 80)/80`. This
makes a bin's value scale-free and bounded, so the L1 numbers below have a legible unit: **mean absolute
difference per bin**, where 0.0125 = 1 dB average per-bin difference.

- **(a) pairwise distinction** — two clauses, one self-calibrating and one absolute:
  - *(a1)* every one of the 10 cross-material distances `≥ 4 ×` the **within-material** distance of that
    pair — i.e. `4 × max(within_i, within_j)`, where a within-material distance is the same material
    charged by two **disjoint 4-seed sets** and its two seed-averaged profiles compared. Both sides are
    measured on the **free ring** (charge with 1.0 s of the same band-limited noise, then analyse the
    8192-sample tail of the un-driven ring), with the same 8192-point Hann pipeline. This is the clause
    with teeth: it compares material difference against measurement noise on the same scale, so it cannot
    be satisfied by a scaling accident;
  - *(a2)* every cross-material distance `≥ 0.02` mean-per-bin (equivalently L1 `≥ 82` over 4096 bins), on
    the **driven** last-1.0 s window.
  If the measured matrix does not clear these, the response is to change the FR-011a profiles until the
  materials really are distinct — **not** to lower the thresholds.

  > **(a1)'s pipeline was corrected during implementation (2026-07-27); its `4 ×` factor was not.** As
  > first written, (a1) compared a cross-material distance measured at the *same* excitation seed against
  > a within-material distance measured at two *different* seeds — the second carries a single
  > periodogram's full chi-square variance (~6 dB mean absolute per-bin difference, **0.078** in these
  > units) while the first has it largely cancelled. The two were never on one scale. Measured on the
  > driven window with one periodogram each: within-material 0.074 … 0.094, cross-material Glass↔Ice
  > **0.084**, Glass↔MetalPlate **0.078**, MetalPlate↔Ice **0.081** — the three modal materials sit closer
  > to each other than any one of them sits to *itself* on another seed. **No FR-011a profile set can
  > clear `4 ×` there**, because the metric's floor is the excitation's own randomness, not the materials.
  > Two changes fix the comparison without touching a threshold: measure the **free ring** (mode
  > *frequencies* are deterministic there; only their starting amplitudes depend on the charge — the same
  > deviation, for the same measured reason, that (c)'s peak detection already takes), and **average over
  > 4 seeds** so the residual falls as `1/√N`. The noise reference is also made **per-pair**: Chamber's
  > comb bank carries its own LFO and per-comb random drift, so its ring is intrinsically the least
  > repeatable of the five (0.0180 against Glass's 0.0017), and charging the Glass↔Ice comparison with
  > Chamber's drift is a category error. Measured with the corrected pipeline: within-material 0.0017
  > (Glass), 0.0082 (Strings), 0.0052 (MetalPlate), 0.0180 (Chamber), 0.0016 (Ice); worst cross/within
  > ratio **6.17** (Strings↔Chamber), best 11.82 (Glass↔Ice) — all ten clear `4 ×`.
- **(b) monotone controls** — for every material, measured T60 is non-decreasing in `setResonance`
  across `r ∈ {0, 0.25, 0.5, 0.75, 1}` (within a 5 % measurement tolerance per step), and the spectral
  centroid (`extractAudioFeatures(...).centroidHz`, `tests/test_helpers/audio_features.h:37-92`) is
  non-increasing in `setDamping` across the same grid within a 2 % tolerance, **and** strictly lower at
  `d = 1` than at `d = 0` by `≥ 5 %` — so the control is not merely non-increasing but audible (FR-036).
- **(c) character ordering** — three claims, each derived from FR-011a/FR-036 rather than asserted:
  - *T60 at `resonance = 0.8`*: `MetalPlate (11.0 s) > Glass (6.61 s) > Ice (5.51 s) > Strings (3.83 s) >
    Chamber (1.20 s)`, each measured within ±15 % of the tabulated value and strictly ordered. These are
    FR-036's derived numbers, not aspirations.
  - *Inharmonicity*, defined as the **mean of `|ratio_k − k|` over the first 8 detected peaks, unnormalised
    and uncapped** (`ratio_k` = peak `k`'s frequency over the detected `f0`). The earlier draft's
    "deviation from the nearest integer" is saturating (capped at 0.5) and converges to 0.25 for the high
    modes, so it cannot discriminate these tables at all. Computed from FR-012's published ratios the
    required values are **Glass ≈ 8.19, Ice ≈ 8.29, Metal Plate ≈ 0.99, Strings ≈ 0.03**, so the criterion
    is: `inharmonicity(Glass) ≥ 5 × inharmonicity(MetalPlate)`, `inharmonicity(Ice) ≥ 5 ×
    inharmonicity(MetalPlate)`, `inharmonicity(MetalPlate) ≥ 3 × inharmonicity(Strings)`, and
    `inharmonicity(Strings) ≤ 0.15`. Glass and Ice are **not** ordered against each other by this metric —
    they share a ratio table and the scatter warp moves the mean by ~1 %, which is not a separation.
  - *Glass vs Ice* is instead asserted where the difference actually lives: at least 6 of the first 8
    detected peaks differ in frequency by `≥ 2 %` between the two materials. **The displacements that
    clear it are the product of the stretch and scatter warps, not the scatter column** — see FR-012,
    which records the arithmetic and the `scatter = 0.8 → 1.0` re-valuing it forced. At the shipped
    profiles the per-mode ratios are `+0.12, +9.87, −5.72, −2.50, +13.34, +1.49, −2.46, +16.91 %`
    (measured: `+0.11, +9.84, −5.71, −2.54, +13.34, +1.48, −2.46, +16.91 %`), i.e. exactly six, and six is
    the ceiling this pair can reach — `k = 0` is unscattered by construction and `k = 5` is where the two
    warps now cancel.
- **(d) flatness** — **Glass's** spectral flatness (`tests/test_helpers/signal_metrics.h:326`,
  `calculateSpectralFlatness`) exceeds **Ice's** by `≥ 0.02` at identical excitation and identical
  `resonance`/`damping`. This clause exists because an earlier draft cited the flatness helper with no
  consumer; either it is asserted or the citation goes. Its purpose is that Ice must not be "Glass with a
  knob turned", and it is the *separation* that carries that, not the sign.

  > **Direction corrected 2026-07-28; the magnitude was not touched.** The clause asserted
  > `flatness(Ice) ≥ flatness(Glass) + 0.02` "reflecting Ice's shallower α", on a sweep quoted in
  > FR-011a ("Ice flatness against Glass's 0.2034: α 1.3 → 0.1772, 0.5 → 0.2197, 0.7 → 0.2224,
  > 0.9 → 0.2252"). **That sweep does not reproduce, and it was measured with the two materials
  > transposed** — the quoted "Ice at α 0.9 = 0.2252" is Glass's own figure to four significant figures,
  > and the quoted "Glass = 0.2034" is Ice's at α ≈ 1.1. Re-measured against the shipped profiles through
  > SC-003's own pipeline (220 Hz, resonance 0.7, damping 0.0, seed A at amplitude 1.0, final 4096
  > samples), sweeping **only** Ice's `amplitudeExponent`:
  >
  > | α | 0.1 | 0.3 | 0.5 | 0.7 | 0.9 | 1.1 |
  > |---|---|---|---|---|---|---|
  > | Ice flatness | 0.1985 | 0.1979 | 0.1978 | 0.1990 | 0.2015 | 0.2035 |
  >
  > against Glass's **0.2253**. α moves Ice by 0.006 across its whole usable span, it moves it the *wrong
  > way* for the stated mechanism, and no value of it comes within 0.02 of Glass. The reason is that the
  > two fields which *define* Ice both **lower** flatness on this metric — measured at α 0.9: stretch 0.5
  > + scatter 1.0 (shipped) 0.2015, scatter alone 0.1751, stretch alone 0.1874, neither 0.2349
  > (i.e. Glass-like). The one field that lifts it materially is `b1`, and `b1` is not free: FR-036 ties
  > it to `T60 = 6.91/b1` and SC-003(c) pins Ice strictly between Glass (6.61 s) and Strings (3.83 s) at
  > resonance 0.8, confining `b1` to (0.500, 0.863) — worth about +0.015 against the 0.245 the old
  > direction would need.
  >
  > The **measured separation is 0.0237**, larger than the 0.02 the criterion asks for, so the magnitude
  > survives untouched; only the direction changes, to the one the shipped profiles produce. The
  > excitation, window and helper are unchanged, and the direction is *asserted* rather than left free,
  > so a profile change that collapsed the two materials together still fails.

**Peak detection, stated so two implementations cannot disagree.** `f0` = the frequency of the
highest-magnitude spectral peak below `1.5 · f_body` in the 8192-point spectrum above, refined by 3-point
parabolic interpolation on the log magnitudes. (Autocorrelation, cepstral and YIN estimators are *excluded
by name*: Glass/Metal Plate/Ice have no harmonic series, so those return something other than `f_body`.)
Peaks = local maxima at least 8 bins apart whose magnitude is within 40 dB of the render peak, taken in
frequency order, first 8 used.
*Test:* `ContinuousBody_MaterialsDistinct`, `ContinuousBody_MaterialCharacterOrdering`.

**SC-004 — Retune is smooth under glide** *(roadmap lines 220–221)*. With sustained noise input and
`keyTracking = 1`, glide the note frequency 110 Hz → 440 Hz linearly in log-frequency over 1.0 s, for each
material:
Both artifact clauses are **control-relative**, following the in-repo precedent
`dsp/tests/unit/systems/harmonic_cloud_test.cpp:4817-4836`. The control render is the same material, the
same excitation, the same seed and the same duration with the note frequency held **fixed** at 110 Hz — no
glide, nothing else changed. Absolute bounds are not usable here and the earlier draft's were false by
construction: a unit-amplitude 440 Hz sinusoid at 48 kHz already has
`max|x[n]−x[n−1]| = 2 sin(π·440/48000) = 0.0576 > 0.05`, and a Metal Plate top mode near 2.46 kHz gives
0.322 — 6× the old bound — on perfectly clean output. `ClickDetector` is likewise a 5σ outlier detector on
the first difference (`artifact_detection.h:42`, threshold at `:193`), so under the *noise* excitation this
criterion uses it reports order-10 detections from statistics alone; "zero" is unachievable and the
precedent does not ask for it.

- `ClickDetector::detect` (`tests/test_helpers/artifact_detection.h:99`, `:130`) configured explicitly with
  `sampleRate = 48000.0f` (**not** the default `44100.0f` at `:39`, which would mis-scope every
  `timeSeconds` by 8.8 %) and otherwise default `frameSize = 512` / `hopSize = 256` / `threshold = 5.0`:
  `detections(glide) ≤ detections(control)`, **except on the waveguide engine, where the bound is
  `1.6 × detections(control)`** (see the amendment below);
- `max|x[n]−x[n−1]|(glide) ≤ 1.5 × max|x[n]−x[n−1]|(control)` — **no allowance, on any material**;
- **non-vacuity** (also from the precedent, `:4817-4823`): the glide render must differ from the control
  render, otherwise both comparisons are trivially satisfied by a component that ignored the glide;
- output RMS over any 20 ms window stays within the **local** tolerance below of the *same window* in the
  control render (no dropout, no surge).

> **Three amendments, recorded 2026-07-28, all measured.**
>
> **(1) The control is BOTH endpoints, not "held fixed at 110 Hz".** As written the clause is
> unsatisfiable by any correct implementation, for the reason the paragraph above already gives for
> rejecting absolute bounds: `max|dx|` is proportional to frequency × amplitude and the glide *ends two
> octaves above the control*, so the criterion would be measuring pitch rather than artifacts. Measured
> `max|dx|` on L: Glass 8.01e-5 (control @110) / 4.69e-4 (glide) / 4.89e-4 (control @440) — 5.86× against
> the 110 Hz control and 0.96× against the 440 Hz one; Metal Plate 5.57× / 0.87×; Ice 3.28× / 1.16×;
> Strings and Chamber ≤ 1.0× against both. The glide's `max|dx|` never exceeds the *larger* endpoint
> control's and its per-block trajectory rises monotonically with pitch with no isolated spike anywhere.
> The baseline is therefore `worst({control@110, control@440})`. A step introduced *by* the glide is
> bounded by neither endpoint, so the clause keeps its full sharpness.
>
> **(2) The RMS clause is local-to-local, at a measured tolerance.** "Within ±3 dB of the pre-glide RMS"
> compares a 20 ms window against a whole-render average across a two-octave pitch change, so it measures
> the excitation's own short-window variance: a *control* render with nothing happening reaches
> **6.71 dB** (Chamber) of 20 ms-vs-200 ms local deviation. The clause is restated as the worst local
> deviation of a 20 ms window against its own 200 ms neighbourhood, hopped by 10 ms so "any 20 ms window"
> is covered rather than sampled, bounded by `max(7.0 dB, 1.5 × the control's own worst)`. A real dropout
> or surge is an order of magnitude past that.
>
> **(3) The count clause carries a 1.6× floor on the waveguide, and only there.** Measured over the three
> seeds, per material (a ratio of −1 means the *control* never cleared the detector's −60 dBFS energy
> gate, so no frame of either render was analysed): Glass −1, **Strings L 1.179 / R 1.517**, Metal Plate
> −1, Chamber L 0.607 / R 0.300, Ice −1. One channel of one material needs it. The cause is
> `WaveguideString::retune()` writing `bridgeDelayFloat_` **directly** (FR-080): over a 110→440 Hz glide
> the loop length moves 436→109 samples, i.e. ~0.44 samples per control step, applied as a step in a
> linearly-interpolated delay read. Two fixes were prototyped and neither removes it — driving the delay
> through a 20 ms one-pole made it *worse* (R 44→52) and a 1 ms linear ramp did nothing (R 44→45); the
> remaining candidate is a per-sample resampling retune of the waveguide loop, which is a Layer-2
> redesign bounded by FR-084/SC-014's no-regression requirement for every existing consumer. **The floor
> was 2.0 and is tightened to 1.6** — the smallest round value above the measured worst, 5.5 % of margin.
> It applies to the **count clause only**; `max|dx|` and non-vacuity carry no allowance on any material.
> If the ratio ever approaches 1.6 the response is the waveguide redesign, not a larger number.

*Test:* `ContinuousBody_GlideIsClickless` (parameterised over material).

**SC-005 — CPU ≤ 1% of one core per voice with the body active** *(roadmap line 221)*. Measured on the
`harmonic_cloud_perf_test.cpp` basis (that file's header comment, `:8-25`): **nanoseconds per 512-sample
block**. The absolute budget is **binding, not advisory** — roadmap lines 488–489 make CPU budgets FRs —
and it is bound the way that file binds it, which the earlier draft cited but did not reproduce.

```
kReferenceNsPerBlock            = 512/48000 × 1e9 × 0.01 = 106,667 ns   // roadmap's 1 %
kRegressionFactor               = 1.5
kMaxAdmissibleBaselineNsPerBlock = kReferenceNsPerBlock / kRegressionFactor = 71,111 ns
```

Every checked-in baseline carries **both** compile-time clauses, verbatim in intent from
`harmonic_cloud_perf_test.cpp:142-143` and `:149-151`:

```cpp
static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock, …);
static_assert(kBaselineNsPerBlock <= kMaxAdmissibleBaselineNsPerBlock, …);
```

and the run-time gate is the relative one, `REQUIRE(measured <= kBaselineNsPerBlock × kRegressionFactor)`
(the form at `:412`). The two compose: because a baseline that would let `measured` exceed 106,667 ns does
not **compile**, the run-time `REQUIRE` transitively binds `measured ≤ kReferenceNsPerBlock` on every run,
on every machine. The `[.perf]` tag keeps the *timing* out of CI; the `static_assert`s are evaluated by
every CI leg regardless of tags, which is precisely why the gate is placed there. **A measurement above
`kMaxAdmissibleBaselineNsPerBlock` is a spec failure — reduce cost, never raise the baseline** (the rule
stated at `harmonic_cloud_perf_test.cpp:82-85`, and the reason RA-4 exists). The per-block percentage is
still *reported* via `WARN` because percent-of-core is not machine-portable; it is no longer the only thing
the absolute figure gets.

Four configurations, four baselines:

| Configuration | Reference the baseline is asserted against |
|---|---|
| **steady state** — one material, cloud active, no crossfade, static parameters | 53,333 ns (0.5 %) |
| **operating point** — the Phase 7 cadence: every setter stepped once per 64-sample control chunk with the note frequency gliding | 53,333 ns (0.5 %) |
| **crossfade window** — during a material change, where FR-024 permits two engines and FR-024a caps it at two | **106,667 ns (1 %)** |
| **cloud only** — `setResonatorBypass(true)` (FR-063), so the decay cloud's cost is a visible line item rather than folded into the others; the cloud runs at normal level here because bypass drops the `1/Ĝ` term (FR-033) | 53,333 ns (0.5 %) |

**Each of the four baselines is the *most expensive* material's measured number, and every material is
`REQUIRE`d against it** (decided 2026-07-27, Q8). Five materials × four configurations = 20 measurements
against four checked-in constants: for each configuration, the baseline constant is set from whichever
material measures highest (expected: Metal Plate or Glass for the modal engine), both `static_assert`s are
written against that constant, and the run-time `REQUIRE(measured <= kBaselineNsPerBlock ×
kRegressionFactor)` is evaluated for **all five** materials against it. A worst-case gate is what makes the
`static_assert` composition bind for the cases that matter; an average or arbitrary material would leave
the 1 % ceiling unenforced for exactly the expensive ones. Twenty baselines were rejected as 20 constants
and 40 `static_assert`s to maintain for regression detection the per-material spread clause below already
provides. **A material that exceeds the worst-case baseline is a spec failure: reduce cost (mode count is
already truncated by FR-043; the levers are FR-042a's dirty gate, RA-4's fast path, FR-053a's cloud
bypass), never raise the baseline.**

The steady-state and operating-point references are **half** the roadmap figure on purpose: the crossfade
window runs two engines, and the roadmap's 1 % ceiling has no transient exemption. Budgeting the crossfade
at 2 % (as an earlier draft did) would put 16 voices at 32 % of one core against the roadmap's 25 %
full-poly ceiling (line 301) during a synchronous material change, and would hide the relaxation inside a
success criterion where Phase 7's tally would never see it. Halving the steady-state budget instead keeps
every number inside the roadmap and needs no amendment.

Additionally, per-material timings must differ **with a stated margin**: the cheapest material's
**engine-attributable** cost must measure `≤ 0.7 ×` the most expensive material's, where a material's
engine cost is *its own* steady-state figure minus *its own* cloud-only figure, both measured in the same
run. This is corroboration for FR-023, not its proof — a timing comparison cannot distinguish "not
advanced" from "advanced with zero input", so FR-023's actual verification is the `getEngineSampleCount`
assertion in SC-016.

> **Amended 2026-07-28: the ratio is taken on the engine cost, not on the raw steady-state figure.** The
> decay cloud is **per-voice** (FR-050, scope item 5) and is identical work for every material — the only
> thing that differs across the five steady-state figures is which engine is advanced. A ratio taken on
> the raw figures is therefore a ratio of `(shared cloud + engine)`, which tends to **1** as the shared
> term dominates: the criterion gets *harder* to satisfy the cheaper the engines become, and is
> satisfiable by making an engine slower. That is precisely backwards for a clause whose subject is
> "only active modules burn CPU". After the optimisation pass the raw ratio measures **0.77** while the
> engine-attributable ratio measures **0.29–0.44** across eight runs — the engines differ by a factor of
> two to four, and the raw form reported that as a failure.

> **Baseline pinning rule, made unambiguous 2026-07-28.** The two clauses as first written are in
> tension: "each of the four baselines is the most expensive material's measured number" and "a
> measurement above `kMaxAdmissibleBaselineNsPerBlock` is a spec failure" together demand
> `measured ≤ reference / 1.5`, i.e. **0.333 %** of a core for the three single-engine configurations —
> stricter than the 0.5 % this criterion states and than the 1 % the roadmap requires. The rule is
> therefore `baseline = min(round-up(worst measured), kMaxAdmissible*)`, and the **failure condition is
> a measurement above the applicable REFERENCE figure** (53,333 / 106,667 ns), which is exactly what the
> static_assert composition was introduced to bind. Where the cap binds, the regression bound is looser
> than a measurement-pinned one and `continuous_body_perf_test.cpp`'s provenance block states by how
> much, per configuration, with the measured numbers.

**Measured 2026-07-28** (13th Gen Intel Core i9-13900HX, MSVC Release, best-of-25 × 500 blocks, worst of
eight runs, worst material per configuration — Strings in all four):

| Configuration | Reference | Worst measured | % of one core | Baseline pinned |
|---|---|---|---|---|
| steady state | 53,333 ns | 35,669 ns | 0.334 % | 35,500 (capped) |
| operating point | 53,333 ns | 41,141 ns | 0.386 % | 35,500 (capped) |
| crossfade window | 106,667 ns | 53,883 ns | 0.505 % | 54,000 (measured) |
| cloud only | 53,333 ns | 26,370 ns | 0.247 % | 26,500 (measured) |

*Test:* `ContinuousBody_CpuBudget`.

**SC-006 — Zero heap allocation after `prepare()`** *(FR-002)*. Using the in-repo bracketing idiom
(`TestHelpers::AllocationDetector::instance().startTracking()` / `stopTracking()`, as documented at
`dsp/tests/unit/systems/harmonic_cloud_test.cpp:4840-4900`), with the **mandatory liveness clause first**
(a deliberate probe allocation read through a `volatile` pointer must be counted, or the zero clause proves
nothing because the operator overrides live in a different TU). Tracked window: 200 × 512-sample blocks
with every setter stepped per block, two material changes, and a glide. Allocation count must be 0.
*Test:* `ContinuousBody_NoAllocInProcess`.

**SC-007 — Drive normalization holds the level across decay settings and input levels** *(FR-032–FR-034a)*.
Sine at the body fundamental, `cloudMix = 0`, no output trim exists (FR-061 deleted), per material. Every
"steady-state output peak" below is **the quantity defined at the head of this section** — render
`max(5 s, 3 × getEngineT60Sec())`, mean per-block peak over the final 1 s — so each point on the resonance
grid is measured after its *own* envelope has settled, which is what makes the ±3 dB clause a statement
about drive compensation rather than about differing settling times:
- **level invariance across resonance** — across `resonance ∈ {0.2, 0.5, 0.8, 1.0}` the steady-state
  output peak varies by no more than **±3 dB about its own mean across that grid**. This is the clause the
  roadmap's "no unbounded growth, same instrument at every damping" intent actually needs: a 23 s body and
  a 0.35 s body must be the same loudness. It is *not* stated against `kTargetPeak = 1.0` absolutely,
  because `Ĝ` is an all-modes-in-phase upper bound (FR-032) while this test excites a single mode, so the
  absolute level is a per-configuration calibration and not something a closed-form bound predicts;
- **absolute level sanity** — the mean steady-state peak across that grid lies within **−25 dB … +3 dB** of
  `kTargetPeak` (FR-033) for every material, which is loose enough to accommodate the
  single-contributor-versus-sum gap and tight enough that a 31 dB error (the one the old FR-032 formula
  would have produced) fails;

  > **Floor moved from −20 dB to −25 dB, 2026-07-28, on the measurement that extending this criterion to
  > all five materials produced.** The clause had only ever been evaluated on the three modal materials,
  > and −20 dB was fitted to them — Metal Plate already sat 3.7 dB inside it. Measured grid mean relative
  > to `kTargetPeak`: **Strings −1.30, Glass −8.20, Ice −9.24, Metal Plate −16.35, Chamber −22.69 dB.**
  > The ordering is the number of contributors `Ĝ` sums over, exactly as FR-032 predicts: the waveguide
  > *attains* its bound (one loop, and the probe sine sits on the comb tooth); Glass/Ice sum 11
  > amplitude-normalised modes; Metal Plate sums 29; **Chamber sums six EQUAL-weight combs — there is no
  > amplitude normalisation across combs — so 10·log10(6) = 7.8 dB of its gap is the bound's shape
  > alone**, before the inharmonic spread that keeps five of the six teeth off the probe. Chamber's level
  > is therefore a correct consequence of FR-032 and FR-033 as specified, not a defect, and no tightening
  > of `Ĝ` is available that keeps it a valid upper bound for broadband input (SC-015). −25 dB admits the
  > worst material with 2.3 dB to spare. The clause's discriminating power is unchanged, and the level is
  > still pinned tightly by clause (i)'s ±3 dB across the resonance grid, per material.
- **the compensation is a gain, not a compressor** — with `setInputAgcEnabled(false)` (FR-034a) a 20 dB
  input level change produces a 20 dB ± 1 dB output change;
- **the AGC works** — with `setInputAgcEnabled(true)`, a 20 dB input drop is followed within 1 s by an
  output recovery to within 6 dB of the original, and `getInputRms()` tracks the true windowed RMS within
  ±10 %. (Both clauses are unmeasurable without FR-034a: `kMinRmsGain`/`kMaxRmsGain` are `static constexpr`
  per FR-008, so "disable the AGC" would otherwise mean editing the header and recompiling.)
*Test:* `ContinuousBody_DriveNormalization`.

**SC-008 — Decay-cloud RT60 accuracy up to 30 s** *(FR-052, roadmap line 213)*. Measured with
`setResonatorBypass(true)` (FR-063) — the *only* configuration in which the cloud's decay is attributable.
`setMix` cannot do this (0 = input passthrough, 1 = body+cloud; neither endpoint removes the body, FR-060)
and `setCloudMix` blends the cloud against the dry resonator output (FR-053), so without FR-063 the
log-envelope regression would be dominated by a body whose own T60 reaches 23 s.
Because bypass drops the `1/Ĝ` term (FR-033, FR-063), the cloud is excited at **normal level**: the tail
starts near full scale, the −5 dB … −35 dB regression span sits far above the noise floor, and the test
applies **no** makeup gain. (Under the rejected always-`1/Ĝ` reading the impulse would enter the cloud at
≈ −94 dBFS and the 30 s clause below would be unmeasurable.)
Impulse-excited, `cloudMix = 1`, cloud decay set to `{0.5, 2, 10, 30}` s: measured T60 (linear regression
on the log-envelope over the −5 dB…−35 dB span, extrapolated) within **±15 %** of the requested value, at
both `cloudSize = 1.0` (`loopSeconds ≈ 94/105 ms`) and `cloudSize = 0.0` (`loopSeconds = 37/41 ms`, the
network bypassed) — the two ends of FR-052's loop-time formula, so a regression that ignored the cascade
fails the first and passes the second. The 30 s case must be measurable — the tail must still be above the
noise floor at 20 s.
*Test:* `ContinuousBody_CloudDecayAccuracy`.

**SC-009 — Key-tracking law and pitch accuracy** *(FR-040, FR-081)*.
- **(a) law** — `getBodyFrequencyHz()` matches `referenceHz·(noteHz/referenceHz)^keyTracking` within
  **0.1 cent** across `keyTracking ∈ {0, 0.25, 0.5, 0.75, 1}` × `noteHz ∈ {55, 110, 220, 440, 880, 1760}`;
- **(b) modal realisation** — the detected fundamental of the rendered output is within **5 cents** of
  `f_body` for Glass/MetalPlate/Ice, using SC-003's named estimator (highest-magnitude peak below
  `1.5·f_body` in an 8192-point Hann-windowed FFT with parabolic interpolation) — not autocorrelation,
  cepstrum or YIN, which do not return `f_body` on an inharmonic spectrum;
- **(c) waveguide realisation after `retune()`** — within **5 cents** over a ±12 semitone retune from the
  `noteOn()` pitch. Held by FR-080's exact delay budget compensating the *frozen* cascade's actual phase
  delay (measured worst case 1.5 cents; see FR-080's amendment note). Same estimator; the
  waveguide's spectrum is harmonic so the peak below `1.5·f_body` is unambiguous.
*Test:* `ContinuousBody_KeyTrackingLaw`, `WaveguideString_RetunePitchAccuracy`.

**SC-010 — Determinism** *(FR-072)*. Two `ContinuousBody` instances with the same seed, sample rate and
parameter script produce renders that compare equal under
`compareFingerprints(fingerprintRender(a), fingerprintRender(b))`
(`tests/test_helpers/render_fingerprint.h:64`, `:101`) at the helper's default tolerances. **No bit-exact
float golden anywhere** (`node tools/lint-float-bit-goldens.js` must stay clean).
Anti-vacuity, stated per material because the seed's reach is asymmetric and FR-070a/FR-071 say exactly
where it stops:
- **Glass, Metal Plate, Ice** — seeds 1 and 2 must produce a **failing** `compareFingerprints`, and, with
  the measurement rather than the helper as the margin, the detected frequency of mode 8 must differ
  between the two seeds by `≥ 0.5 cents` (FR-070a's `kSeedDetuneCents = 3.0` gives up to ±3 cents per
  mode, so 0.5 is a comfortable floor);
- **Strings, Chamber** — seeds 1 and 2 must produce an **identical** render under `compareFingerprints`.
  The seed is documented inert for both (waveguide: the RNG feeds only the note-on burst, injected at
  velocity 0 → `velScale = 0`, `waveguide_string.h:393`/`:446`; comb: hard-seeded `12345u + i·7919u`,
  `timevar_comb_bank.h:429`, `:450`, FR-071). Asserting the *sameness* turns a known limitation into a
  covered one rather than an untested claim.
*Test:* `ContinuousBody_SeedDeterminism`.

**SC-011 — Sample-rate and block-size invariance.** At 44,100 / 48,000 / 96,000 Hz, starting from FR-009's
freshly-prepared state (Glass, resonance 0.7, damping 0.0, keyTracking 1.0, 220 Hz, mix 1.0, cloudMix 0.25,
cloudDecay 4.0 s, cloudSize 1.0, cloudDamping 0.3, width 1.0, AGC on, seed 1) with identical
parameters: measured T60 within ±10%, detected fundamental within 5 cents, steady-state output RMS within
±1 dB. Separately, at one rate: a 1024-sample render issued as 1×1024, 2×512, 16×64 **and — the cases that
can actually fail — 1023+1, 100+100+…+24, and 7×146+2** all agree to `kSampleTolerance = 1.0e-4`
(`render_fingerprint.h:49`), at the **same** tolerance for the non-multiple partitions. The first three are
exact multiples of the 64-sample control grid, so the chunking arithmetic is identical in all three and the
criterion cannot fail on a grid-alignment bug; real host buffers (441, 480, 1023) split control steps.
FR-005a's persistent control-phase counter and carried `Σx²` accumulator (A-5, clarified Q6) are what make
the same tolerance apply to all six — if an implementation instead restarts the control phase at each call,
or fires a control step on a sub-64 tail, the non-multiple partitions diverge and this criterion catches
it.
*Test:* `ContinuousBody_SampleRateInvariance`, `ContinuousBody_BlockSizeInvariance`.

**SC-012 — Material crossfade and parameter changes are clickless** *(FR-006, FR-024, FR-024a, FR-063)*.
Under sustained input: (i) all 20 ordered material transitions; (ii) the **retarget** cases FR-024a governs
— three modal materials inside one 500 ms window (Glass → Ice → Metal Plate) and a modal → waveguide →
comb chain at 100 ms spacing; (iii) `setResonatorBypass` toggled on and off; (iv) every setter swept across
its full range once per 64-sample block for 10 s. Measured exactly as SC-004 measures — **control-relative,
for the same reasons** (a 5σ first-difference detector reports order-10 detections on noise excitation with
no artifact present; an absolute 0.05 delta bound is exceeded by clean full-scale output above ~380 Hz):
- control render = same material, same excitation, same seed, same duration, **no** transition and no
  setter movement;
- `ClickDetector` configured with `sampleRate = 48000.0f`: `detections(test) ≤ detections(control)`;
- `max|x[n]−x[n−1]|(test) ≤ 1.5 × max|x[n]−x[n−1]|(control)`;
- non-vacuity: the test render must differ from the control render.

> **Clause (iv) carried a 2.0× waveguide floor on the count. It is GONE (2026-07-28).** Measured, every
> material including Strings now passes `detections(test) ≤ detections(control)` at the criterion exactly
> as written, with no per-engine exception. What removed it is **RA-5**'s control-grid snap on
> `WaveguideString`'s own parameter smoothers: the excess existed because a *second* 20 ms smoother was
> still gliding toward each control step's value when the next one arrived, so across a 10 s sweep of
> every setter the loop-loss gain never settled anywhere. All four clauses of SC-012 are now at the
> criterion, on all five materials.
*Test:* `ContinuousBody_CrossfadeClickless`, `ContinuousBody_RetargetClickless`,
`ContinuousBody_ParameterSweepClickless`.

**SC-013 — Portability and robustness.**
- `node tools/check-portability.js` clean on every changed TU (MSVC-green proves nothing —
  `dsp/CLAUDE.md`);
- `node tools/lint-arch-guarded-includes.js`, `lint-float-bit-goldens.js` and
  `lint-simd-aligned-loadstore.js` clean;
- no `std::isnan`/`std::isinf`/`std::numeric_limits::infinity()` in component or test code; non-finite test
  inputs are constructed from bit patterns through a `volatile` sink;
- **non-finite input is substituted, and the ring survives it** (FR-038): injecting NaN/Inf for one block
  under sustained excitation yields finite output, **no** `silence()` and an unbroken tail — output RMS
  over the 100 ms following the poisoned block is within **±1 dB** of the same window in an un-poisoned
  control render, and the injection point passes SC-012's control-relative click clauses, with the count
  clause carrying a **1.2×** allowance (below). Recovery on the next finite block is complete because
  nothing was reset;

  > **Allowance recorded and tightened 2026-07-28.** The excess is FR-034 doing its job, not an artifact:
  > FR-038 substitutes the poisoned chunk with **zeros**, which is a 10.67 ms hole in the excitation, and
  > the RMS follower reacts to it — that is what an input AGC is for. Measured on Strings around the
  > injection: a 0.2 dB RMS dip and a drive excursion peaking at **+8 %** that is back inside 0.1 %
  > within ~85 ms, with `max|dx|` unchanged to three significant figures (0.00225/0.00250/0.00191 vs
  > 0.00226/0.00250/0.00202) — there is no step anywhere, but a slow +8 % gain ramp moves a handful of
  > the marginal 5σ outliers a first-difference detector reports from statistics alone, and the clause is
  > a strict `≤` between two such counts. Measured ratios (−1 = the control never cleared the detector's
  > energy gate): Glass −1, **Strings L 1.063 / R 1.107**, Metal Plate −1, Chamber L 0.929 / R 1.000,
  > Ice −1. The constant was 1.5 — about 5× the measured excess — and is tightened to **1.2**, the
  > smallest round value above the worst, 8.4 % of margin. It is on the **count only**: the ±1 dB tail
  > clause and `max|dx|` carry no allowance. If it is ever approached the response is to look at FR-038's
  > substitution, not to raise the number;
- **non-finite state is recovered, clicklessly** (FR-038a): driving a **finite but enormous** input
  (±1e38 for one block — legal input, so FR-038 does not intercept it) until `stateFinite()` reports false
  must produce a 10 ms equal-power ramp to zero, a `silence()` at zero gain and a ramped re-entry; output
  is finite at every sample and `stateFinite()` is true again within **100 ms** of the offending block.
  This is the *only* path on which an engine is silenced by a robustness rule, and it is a ramp, never a
  step;
- zero compiler warnings.
*Test:* `ContinuousBody_NonFiniteInputRecovery`, `ContinuousBody_NonFiniteStateRecovery`, plus the lint
gates.

**SC-014 — No regression from either amendment** *(FR-084, RA-1, RA-4)*. Both amended headers are **shared
DSP** with consumers outside this phase, so the regression set is the union of their consumers' suites,
enumerated by grep this session (RA-1's cross-plugin impact table):

| Amended header | Consumers verified this session | Suites that must pass **unedited** |
|---|---|---|
| `processors/waveguide_string.h` (RA-1) | `dsp/tests/unit/processors/{waveguide_string_test,waveguide_string_dc_blocker_test,bow_waveguide_coupling_test}.cpp`; `plugins/innexus/src/processor/innexus_voice.h:24` (+ 2 Innexus tests); **`plugins/membrum/src/dsp/drum_voice.h:41` and `bodies/string_body.h:22`**; `tools/membrum_preset_generator.cpp` | `dsp_processors_tests`, `innexus_tests`, **`membrum_tests`** |
| `processors/diffusion_network.h` (RA-4) | `dsp/tests/unit/processors/diffusion_network_test.cpp:14`; `dsp/include/krate/dsp/effects/shimmer_delay.h:32`; `effects/freeze_mode.h:31`; `plugins/iterum/src/processor/processor.h:27` | `dsp_processors_tests`, `dsp_effects_tests`, `plugin_tests` + `approval_tests` (Iterum) |

`dsp_systems_tests` and `disrumpo_tests` are run as well — neither is a direct consumer, but both link
KrateDSP and cost nothing to include. **`membrum_tests` is the one an earlier draft omitted**, and Membrum
is the heaviest `WaveguideString` consumer in the repo; a build stage that skips it has not verified RA-1.
- **RA-1:** a build that never calls `retune()` produces renders identical to pre-amendment
  `WaveguideString` renders under `compareFingerprints` at default tolerances.
- **RA-4:** a `DiffusionNetwork` render at `modDepth = 0` is **bit-identical** before and after the guard
  (this is the one place a bit-exact comparison is legal — it compares the same binary's two code paths
  over the same inputs, not two toolchains), and a render at `modDepth > 0` matches under
  `compareFingerprints`, including across a `modDepth` `0 → 0.5` transition, which is what proves the LFO
  phase kept advancing while the fast path was taken.
*Test:* the existing suites, plus `WaveguideString_RetuneIsInert`, `DiffusionNetwork_ZeroModIsBitIdentical`.

**SC-015 — The steady-state gain bound is both valid and tight** *(FR-032)*. FR-032 is the new DSP work of
this phase and its central claim — that `Ĝ` upper-bounds the engine's steady-state gain — is asserted
nowhere else: SC-007 measures the *post-compensation* level, which would also pass if `Ĝ` were
systematically wrong in a way the AGC absorbed. For each modal material and each
`resonance ∈ {0.2, 0.5, 0.8, 1.0}`, drive a unit sine at each of the first 8 mode frequencies and compute
`measuredGain = steadyStatePeak / (getDriveGain() × inputAmplitude)`, where `steadyStatePeak` is **the
quantity defined at the head of this section** (render `max(5 s, 3 × getEngineT60Sec())`, mean per-block
peak over the final 1 s) and `getDriveGain()` is the sounding slot's smoothed **engine** drive (FR-033):
- **validity** — `measuredGain ≤ getSteadyStateGainBound()` at every one of the 8 modes;
- **tightness** — at mode 1, `measuredGain ≥ 0.1 × getSteadyStateGainBound()`. A bound derived from the
  wrong transfer function fails this: the flat-numerator form an earlier draft specified over-estimates by
  ≈35× at 220 Hz / 48 kHz (FR-032), i.e. `measuredGain / Ĝ ≈ 0.029`, well under 0.1.
No extra control is needed — `getDriveGain()` is already in FR-007, so the compensation is divided out
rather than switched off.
*Test:* `ContinuousBody_GainBoundValidAndTight`.

**SC-016 — Inactive engines are genuinely not advanced** *(FR-023)*. Functional, not timing-based, and not
`[.perf]`-tagged, so every CI leg evaluates it. Over a 200-block render with no material change:
`getEngineSampleCount(active) == numSamples` and `getEngineSampleCount(e) == 0` for every other engine.
Over a render containing one material change: the sum across engines equals
`numSamples + (samples spent crossfading)`, and no third engine is ever advanced (the FR-024a bound), which
is checked against the three-modal-materials-in-one-window sequence from SC-002. With
`setResonatorBypass(true)`, every engine's count stays flat.
*Test:* `ContinuousBody_OnlyActiveEnginesAdvance`.

---

## Edge Cases

**RT-safety boundaries**
- `processStereoBlock` called before `prepare()` → writes silence, returns; never reads uninitialised
  coefficients.
- Null `inLeft`/`inRight`/`outLeft`/`outRight` → immediate return, no write.
- `numSamples == 0` → no-op, and no control step is consumed (so a zero-length call cannot advance the
  crossfade or the RMS follower).
- Block far larger than any plausible host buffer (e.g. 32,768) → chunked on the 64-sample grid (A-5); no
  fixed-size stack buffer may assume a maximum block size.
- `prepare()` called while a crossfade is in flight → crossfade is abandoned, the incoming material becomes
  current at full gain, all engines silenced.
- `reset()` during a crossfade → same.
- Setters called from a non-audio thread concurrently with `processStereoBlock` are **not** supported;
  the class is single-threaded like every other KrateDSP component, and this is documented, not defended
  against with locks.

**Parameter extremes**
- `setResonance(1.0)` + `setDrive(4.0)` + `setCloudDecaySec(30)` + `setCloudMix(1)` simultaneously — the
  worst case; SC-001 renders exactly this.
- `setResonance(0.0)` → the shortest T60 each material reaches, and it is **material-dependent by design**
  (FR-036): 0.58 s Metal Plate, 0.35 s Glass, 0.29 s Ice, 0.20 s Strings, 0.063 s Chamber. The body must
  still be audible, not silent (drive compensation must scale *up*, bounded by `kMaxDriveGain`).
- `setResonatorBypass(true)` while a crossfade is in flight → the collapse ramp (FR-024a) and the bypass
  ramp are the same 10 ms mechanism; the bypass wins and both engines are silenced at zero gain.
- `setCloudMix(0)` with a still-ringing loop → FR-053a's bypass engages only once the loop's own peak is
  below `kCloudSilenceFloor`, so the tail is never truncated to save CPU.
- `setCloudDecaySec(0.1)` with `setCloudSize(0.0)` → `DiffusionNetwork` bypasses below `size < 0.001`
  (`diffusion_network.h:344`); the loop then degenerates to a plain delay. Must not click or self-oscillate.
- `setKeyTracking(1)` with `noteHz = 20` and Metal Plate: high mode ratios (×11.2) still land under
  Nyquist, but at `noteHz = 8000` most modes exceed it → FR-043 zeroes their amplitude rather than piling
  them up at the cull frequency.
- `setNoteFrequencyHz(0)` or negative → clamped to 20 Hz, no NaN.
- `setDrive(0)` → the log10-domain smoother's argument floors at `kMinDriveGain = 1e-7` (FR-033), i.e.
  −140 dB into the engine — inaudible, and finite, which is what the logarithm needs. The decay cloud must
  still ring out what it holds.
- `userDrive` and `rmsGain` both at maximum with a full-scale input → **this is the only configuration in
  which FR-037's clamp may engage.** It is outside SC-001's parameter set (which requires
  `getClampEngagementCount() == 0` for all three excitations, including the resonant sine — SC-001). Even
  here the clamp must not latch: engagement must fall to zero within 500 ms of the drive returning to
  `userDrive = 1`, asserted alongside SC-001.
- Every `float` setter fed ±inf / NaN → the setters bit-check the argument for finiteness first and
  substitute **the FR-009 Default column value** (`std::clamp` with NaN is implementation-defined, so the
  clamp alone is not a defence), then clamp to the FR-009 range as for ordinary values.

**Sample-rate changes**
- `prepare()` at a new rate mid-session → all coefficients re-derived, all buffers re-sized, state cleared.
  No stale `smoothCoeff_`.
- 96 kHz: `kMinB1 = 0.23` gives `R = exp(−0.23/96000)`, `1−R ≈ 2.4e-6` — the steady-state gain bound
  doubles versus 48 kHz. FR-032 recomputes `Ĝ` from the actual rate, so the drive compensation follows
  automatically; SC-011 verifies the loudness does not.
- 44.1 kHz: `WaveguideString`'s max period at 20 Hz is 2205 samples, inside its `1/kMinFrequency` = 50 ms
  allocation (`waveguide_string.h:108`).
- Very low rates (8 kHz, from an offline render or a test) → mode frequencies above `0.49·fs` are culled;
  a material may end up with only its fundamental. Must produce sound, not silence or NaN.

**Seed determinism**
- `setSeed()` called after `prepare()` but before any processing → deterministic from the first sample.
- `setSeed()` called mid-render → takes effect at the next control step; not required to be
  retro-deterministic, and documented as such.
- Seed `0` → substituted by `Xorshift32`'s own default (`core/random.h:41-59`), never left as a zero state.
- Chamber's comb drift is **seed-independent** (FR-071) — the determinism test must therefore not assert
  that different seeds diverge for Chamber, or it will fail for a documented reason.

**Engine-specific**
- `ModalResonatorBank::flushSilentModes()` (`modal_resonator_bank.h:383-396`) sets `active_[k] = false`
  for modes below `1e-12` energy, and `updateDampingLaw()` (`:280-294`) **skips inactive modes**. Under
  continuous excitation a mode can be flushed during a quiet passage and then miss a damping update. FR-041
  therefore always routes damping changes through `updateModes()` (which rewrites every mode
  unconditionally, `:705-770`), never through `updateDampingLaw()` — gated, but never replaced, by
  FR-042a's 0.5 % dirty threshold.
- `WaveguideString::process` returns 0 while `bridgeDelayFloat_ < kMinDelaySamples` (`:156-157`), which is
  the state before the first `noteOn()`. FR-022c's zero-velocity `noteOn()` at material assignment is what
  guarantees the Strings material is never silently mute.
- `TimeVaryingCombBank::setFundamental` clamps to `[20, 1000]` Hz (`:91-94`); a Chamber body tracked to a
  4 kHz note therefore saturates at 1000 Hz. Documented behaviour, asserted in SC-009(a) only for the
  modal materials.

---

## Existing Components (reused — verified this session)

| Component | Header path | What is reused (verified signature / fact) |
|---|---|---|
| `ModalResonatorBank` | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | `class ModalResonatorBank : public IResonator` (`:71`); `kMaxModes = 96` (`:73`); `struct DampingLaw { float b1; float b3; }` (`:81-89`); `void setModes(const float*, const float*, int, DampingLaw, float stretch, float scatter) noexcept` (`:228-235`); `void updateModes(…, DampingLaw, …) noexcept` state-preserving (`:264-275`); `void processBlock(const float* input, float* output, int numSamples) noexcept` (`:355`); `void setOutputGain(float)` (`:140`); `void setOutputSoftClipThreshold(float)` (`:150`); `float getInputGainSum() const` (`:168`); `float getModeFrequency(int) const` (`:448`); `void damp(float scale)` (`:429`); `float getModalEnergy() const` (`:415`). Damping maths: `decayRate_k = b1 + b3·f_w²`, `R_k = exp(−decayRate_k/fs)`, `eps_k = 2·sin(π f_w/fs)` (`:742-746`); `b1` floored at `1/5` (`:685`); `b3` has **no** upper clamp on the explicit-`DampingLaw` path, only `max(b3In, 0)` (`:686`) — `kLegacyMaxB3 = 4.0e-5` (`:93`) is used solely by `dampingLawFromLegacy` (`:108`); `B = stretch²·0.01`, `C = scatter·0.10` (`:702-703`) with the scatter warp `f_w ×= (1 + C·sin(k·kScatterD))`, `kScatterD = π(φ−1)` (`:577-578`, `:729`) — deterministic, not RNG; Nyquist cull at `kNyquistGuard = 0.49` (`:567`, `:732-738`); amplitude cull at `1e-4` (`:710`); default soft-clip `0.707` (`:571`); `kSilenceThreshold = 1e-12` (`:566`) with `flushSilentModes` decrementing only `numActiveModes_` (`:383-396`) while `processBlock` still passes `numModes_` to the kernel (`:362-364`). **The per-mode recursion is the coupled (magic-circle) form** `s[n] = R(s+εc) + g·u; c[n] = R(c − ε·s[n]); y += s[n]` (`:833-838`, `:841-855`), whose transfer function has a **zero at `z = R`** — the derivation FR-032 is built on. `process(float)` (`:492-501`) is the IResonator path: it calls `processSample` → `smoothCoefficients()` per sample (`:345-349`, `:801-809`) and the **scalar** `processSampleCore` (`:814+`), and is the only place `controlEnergy_`/`perceptualEnergy_` are updated (`:494-500`) — `processBlock` updates neither (FR-021). |
| `processModalBankSampleSIMD` | `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.h` | Free function, not a class: `float processModalBankSampleSIMD(float* sinState, float* cosState, const float* epsilon, const float* radius, const float* inputGain, float excitation, int numModes) noexcept` (`:36-43`). Called internally by `ModalResonatorBank::processBlock` (`modal_resonator_bank.h:361-364`). Not called by `ContinuousBody`. |
| `IResonator` | `dsp/include/krate/dsp/processors/iresonator.h` | `class IResonator` (`:32`) with pure virtuals `prepare(double)` (`:38`), `setFrequency(float)` (`:42`), `setDecay(float)` (`:46`), `setBrightness(float)` (`:50`), `process(float)` (`:55`), `getControlEnergy()` (`:59`), `getPerceptualEnergy()` (`:63`), `silence()` (`:67`), and virtual `getFeedbackVelocity()` defaulting to 0 (`:72`). Used as the engine-slot contract (FR-021). |
| `WaveguideString` | `dsp/include/krate/dsp/processors/waveguide_string.h` | `class WaveguideString : public IResonator` (`:38`); `prepare(double)` allocates for 20 Hz (`:105-122`); `prepareVoice(uint32_t)` seeds the per-voice RNG (`:125`); `setFrequency` re-targets the log2 smoother only (`:134-140`); `process(float)` (`:154-218`); `silence()` (`:230`); `setStiffness(float)` frozen at onset, `B = stiffness·0.002` (`:256`, `:296`); `setPickPosition(float)` (`:264`); `noteOn(float f0, float velocity)` (`:273`) — computes `D = period − 1 − 0.55·dLoss − 0.96·dDisp` (`:321`) and assigns `bridgeDelayFloat_` (`:325`); `computeRho(f0, t60) = 10^(−3/(t60·f0))` (`:476-481`); `velScale = velocity · excitationGain_` (`:393`) consumed at `:446`, so a velocity-0 `noteOn` writes an all-zero buffer (FR-022c); loss filter `rho·[(1−S)x + S·x[n−1]]` (`:197`) with `S = brightness·0.5` (`:168`) and `|H|` computed at `:379-382`. **`setDecay(float t60)` clamps to `[0.01, 10.0]` s (`:142-145`)** — the ceiling is 10 s, not 30 (FR-035). `process` soft-clips the junction at `kSoftClipThreshold` (`:181`, `:468-473`) and returns 0 while `bridgeDelayFloat_ < kMinDelaySamples` (`:156-157`, `:44`). **Verified gap:** `bridgeDelayFloat_` is assigned only at `:243` and `:325` — no continuous retune path (correction C-4 → RA-1). |
| `TimeVaryingCombBank` | `dsp/include/krate/dsp/systems/timevar_comb_bank.h` | `class TimeVaryingCombBank` (`:81`) — **not** `TimevarCombBank`, and it has **no** base class; `kMaxCombs = 8` (`:88`); `enum class Tuning` (`:43`); `prepare(double sampleRate, float maxDelayMs = 50.0f)` (`:154`); `setNumCombs(size_t)` (`:178`); `setCombDelay(size_t, float ms)` (`:189`); `setCombFeedback(size_t, float)` (`:197`), clamped to `[kMinCombCoeff, kMaxCombCoeff]` = `±0.9999` at `:488`; `setCombDamping(size_t, float)` (`:206`), applied per sample as a one-pole LP in the comb's feedback path (`:612`, `:632`); `setTuningMode(Tuning)` (`:226`); `setFundamental(float)` (`:238`), clamped `[20,1000]` at `:520`; Inharmonic tuning `f[n] = fundamental·√(1 + n·spread)` (`:236-237`); `setSpread(float)` (`:249`); `setModPhaseSpread(float)` (`:285`); `setStereoSpread(float)` (`:311`); `float process(float)` (`:328`); `void processStereo(float&, float&)` (`:338`) — sums to mono at `:660-661` then pans, so stereo costs the same as mono; NaN/Inf guard at `:664-669`; per-comb RNG seeded from the hard-coded `12345u + i*7919u` (`:429`, `:450`) with no seed setter (FR-071). |
| `DiffusionNetwork` | `dsp/include/krate/dsp/processors/diffusion_network.h` | `class DiffusionNetwork` (`:161`); `kNumDiffusionStages = 8` (`:36`); `kAllpassCoeff = 0.618…` (`:39`); `kBaseDelayMs = 3.2` (`:42`); `kMaxModDepthMs = 2.0` (`:45`); `kDelayRatiosL` (`:51-53`); `kStereoOffset = 1.127` (`:56`); `prepare(float sampleRate, size_t maxBlockSize)` — **`float`, not `double`** (`:197`); `setSize/Density/Width/ModDepth/ModRate` (`:273-305`); `process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples)` (`:327-329`) — block entry point only, no per-sample API; bypasses below `size < 0.001` (`:344`); `kDefaultDensity = 100.0f` (`:173`) with stages skipped only below `stageEnable < 0.001` (`:353-356`); per-stage delays `kBaseDelayMs·size·ratio[i]` on L and `×kStereoOffset` on R (`:366-374`), the 8 stages in **series** per sample (`:351-383`), so the cascade's group delay is `3.2·size·17.777` ms = 56.9 ms L / 64.1 ms R at `size = 1` — distinct from the 16.9 ms **per-stage buffer** at `:202-205` (correction C-3); `std::sin` evaluated per stage per sample, unconditionally on `modDepth` (`:362`) → correction C-7 and RA-4. **Verified: no feedback path anywhere in `process` (`:327-403`)**. |
| `EnvelopeFollower` | `dsp/include/krate/dsp/processors/envelope_follower.h` | `class EnvelopeFollower` (`:82`); `prepare(double sampleRate, size_t maxBlockSize)` (`:106`); `float processSample(float)` (`:164`) — advances the one-pole by exactly one step per call; `setMode(DetectionMode)` (`:202`); `setAttackTime(float ms)` `[0.1, 500]` (`:220`, `:88-89`); `setReleaseTime(float ms)` `[1, 5000]` (`:227`, `:90-91`); `getCurrentValue()` (`:192`). **Coefficients are per-sample and derived from `sampleRate_`**: `updateAttackCoeff`/`updateReleaseCoeff` (`:222`, `:229`, `:367-373`) → `calculateCoefficient(ms) = exp(−2π/(ms·0.001·sampleRate_))` (`:359-365`). Calling it once per 64-sample chunk therefore multiplies every time constant by 64 unless the follower is prepared at the control rate — FR-034 does exactly that. RMS mode is the input tracker of FR-034. |
| `equalPowerGains` / `crossfadeIncrement` | `dsp/include/krate/dsp/core/crossfade_utils.h` | `void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` (`:50`); `std::pair<float,float> equalPowerGains(float)` (`:64`); `float crossfadeIncrement(float durationMs, double sampleRate) noexcept` (`:89`). Drives FR-024 and FR-053. |
| `Xorshift32` / `deriveStreamSeed` | `dsp/include/krate/dsp/core/random.h` | `class Xorshift32` (`:41`), `constexpr float nextFloat()` (`:59`); `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t)` (`:102`) — added in Phase 3, used by FR-070. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | `configure(ms, sampleRate)`, `setTarget`, `snapTo`, `process`, `advanceSamples`, `getCurrentValue` — the API `BrownianDrift` (`brownian_drift.h:126-127`, `:187`, `:204`) and `DiffusionNetwork` (`diffusion_network.h:218-233`) both use. FR-006's smoothing substrate. |
| `ModulationSource` | `dsp/include/krate/dsp/core/modulation_source.h` | `class ModulationSource` (`:31`) with `getCurrentValue()` (`:37`) and `getSourceRange()` (`:41`). `ContinuousBody` does **not** implement it (it is a processor, not a modulation source) and owns no instance (N-2) — recorded here because the roadmap's cross-cutting constraint mentions the concept. |
| `BrownianDrift` | `dsp/include/krate/dsp/processors/brownian_drift.h` | `class BrownianDrift : public ModulationSource` (`:94`); `kControlRateInterval = 32` (`:105`); `prepare(double)` (`:121`); `setSeed(std::uint32_t)` (`:145`); `processBlock(size_t)` (`:194`); `getCurrentValue()` (`:212`). **Not instantiated by Phase 4** (N-2); cited because A-5's control cadence is chosen to match it. |
| `HarmonicCloud` | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | `class HarmonicCloud` (`:127`); `kMaxPartials = 64` (`:138`); `kControlChunkSamples = 64` (`:144`); `processStereoBlock(float*, float*, std::size_t)` (`:878`). **Not composed by Phase 4** (N-1); cited as the source of the class-scoped-constant rule (`:134-138`), the pre-prepare silence idiom (`:887-891` — the `if (!prepared_)` block; FR-004 cites the same range) and the control-chunk cadence (A-5). |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h:64` `fingerprintRender`, `:101` `compareFingerprints`, `:49` `kSampleTolerance = 1e-4`; `artifact_detection.h:99` `class ClickDetector` (`:88-90` is the comment banner), `:130` `detect`, `struct ClickDetectorConfig` at `:38` with `sampleRate = 44100.0f` (`:39`), `frameSize = 512` (`:40`), `hopSize = 256` (`:41`), `detectionThreshold = 5.0f` (`:42`) and the outlier rule `mean + threshold·stdDev` over one frame (`:187-193`) — a 5σ first-difference detector, which is why SC-004/SC-012 are control-relative; `signal_metrics.h:326` `calculateSpectralFlatness`; `audio_features.h:37` `extractAudioFeatures` → `centroidHz` (`:88`), used by SC-003(b); `allocation_detector.h:26` `AllocationDetector` (overrides live in a different TU — see SC-006). `spectral_analysis.h` was read and has **no** peak-picking or f0 estimator (it is aliasing-focused, `:40-415`), which is why SC-003 and SC-009 define the estimator inline instead of citing a helper. |
| Test registration | `dsp/tests/CMakeLists.txt` | Systems sources listed explicitly at `:300-339` (never globbed). New test files must be added there or they silently drop. |

## New Components (ODR-swept this session)

Sweep command, run against `dsp/` and `plugins/`:
`grep -rn "class <Name>\b\|struct <Name>\b\|enum class <Name>\b" dsp/ plugins/`

| Name | Kind | Layer | Location | ODR sweep result |
|---|---|---|---|---|
| `ContinuousBody` | class | 3 | `dsp/include/krate/dsp/systems/continuous_body.h` (new file — `ls` confirms it does not exist) | **0 hits.** Clear. |
| `ContinuousBody::BodyMaterial` | class-scoped `enum class : std::uint8_t` | 3 | same header | **0 hits** for `BodyMaterial` anywhere. Clear. **The name `Material` is FORBIDDEN**: `enum class Material : uint8_t { Wood, Metal, Glass, Ceramic, Nylon }` already exists at namespace scope (`processors/modal_resonator.h:81`), and `struct MaterialCoefficients` at `:91`. Class-scoping additionally removes any risk. |
| `ContinuousBody::MaterialProfile` | class-scoped `struct` | 3 | same header | **0 hits** for `MaterialProfile`. Clear. Note `struct BodyMode` **is taken** (`processors/body_resonance.h:60`), so the per-mode record must not be called that; `MaterialProfile` holds plain arrays instead of a per-mode struct. |
| `ContinuousBody::Engine` | class-scoped `enum class` (`Modal`, `Waveguide`, `Comb`) | 3 | same header | Not swept as a bare name because it is class-scoped and generic; the enclosing `ContinuousBody` sweep (0 hits) is sufficient. |
| `ContinuousBody::DecayCloud` | class-scoped private `struct` (FR-050) | 3 | same header | `DecayCloud` — **0 hits.** Clear. Kept nested and private: it is single-use and needs no independent existence (project rule: no abstractions for single-use code). |
| `ContinuousBody::DriveNormalizer` | class-scoped private `struct` (FR-032–FR-034) | 3 | same header | `DriveNormalizer` — **0 hits**; `ExcitationNormalizer` also 0 hits (alternative name, unused). Clear. |
| `WaveguideString::retune(float)` | new **method** on an existing class | 2 | `dsp/include/krate/dsp/processors/waveguide_string.h` | No new type, so no ODR risk. `grep -n "retune" waveguide_string.h` → no existing member of that name. RA-1. |

Additional sweeps run and clear (candidate names considered and rejected, recorded so a later phase does
not re-run them): `BodyModule` 0, `ContinuousBody` 0, `BodyMaterial` 0, `MaterialProfile` 0,
`DriveNormalizer` 0, `DecayCloud` 0, `ExcitationNormalizer` 0. Hits found and avoided: `BodyMode` 1
(`body_resonance.h:60`), `Material` 1 (`modal_resonator.h:81`).

**Namespace-scope constants that must NOT be redeclared** (FR-008): `kBodyModeCount`, `kBodyPresetCount`,
`kBodyFDNLines`, `kBodyFDNMaxDelay` (`body_resonance.h:45-54`); `kNumDiffusionStages`, `kAllpassCoeff`,
`kBaseDelayMs`, `kMaxModDepthMs`, `kDiffusionSmoothingMs`, `kDelayRatiosL`, `kStereoOffset`
(`diffusion_network.h:36-56`); `kMaterialPresets` (`modal_resonator.h:99`).

---

## Resolved Questions

Where the roadmap left the decision to this spec. **All five were answered in the 2026-07-27 clarification
session** and every answer is encoded in the FRs above — nothing here is open. Each is kept with its
rejected alternatives so a later phase does not re-litigate a settled choice.

**OQ-1 — Material → engine assignment.** The roadmap names three engines (line 207) and five materials
(line 216) but never maps them. This spec assigns Glass / Metal Plate / Ice → modal, Strings → waveguide,
Chamber → comb (A-2, FR-011). The alternative reading is that some materials **layer** two engines (e.g.
Chamber = modal + comb), which would roughly double that material's CPU and require FR-023 to be restated.
**Decided (2026-07-27): the one-engine-per-material assignment above** — Glass / MetalPlate / Ice → modal
bank, Strings → waveguide, Chamber → comb bank (A-2, FR-011a). The layered reading is rejected: no material
uses two engines in Phase 4, so FR-023 and SC-005 stand as written.

**OQ-2 — Mode count.** A ceiling of 32 modes per modal material (A-3), against the bank's 96 capacity
(`modal_resonator_bank.h:73`), truncated further by FR-043's Nyquist prefix rule (Glass gets ~13 modes at
`f_body = 220` Hz, all 32 at 55 Hz). Phase 2 answered its analogous question ("64 fixed vs quality tiers",
roadmap open question 1, line 498) with a fixed count driven by measured CPU. The same choice is available
here: ceiling 32, or a quality tier (16/32/64) that Phase 8 exposes. A tier costs a branch in the config
path and a second set of SC-005 baselines.
**Decided (2026-07-27): 32, fixed** (A-3). No quality tier, no runtime mode-count control and no second set
of SC-005 baselines; FR-043's Nyquist prefix truncation remains the only mechanism that lowers the count.

**OQ-3 — Decay-cloud placement: per-voice or one shared instance.** The roadmap places it inside the Phase
4 component (line 212), which makes it per-voice — 16 diffusion loops at full polyphony. A shared
post-voice-sum instance would be far cheaper but changes the character (voices would blur into each other
rather than each having its own halo) and starts to overlap Phase 6's Aether (N-3). This spec implements
per-voice as written.
**Decided (2026-07-27): per-voice**, inside `ContinuousBody`, as the roadmap places it (Scope item 5). The
cost is measured against the 1 %/voice budget by SC-005's "cloud only" line item, and RA-2's Phase 7
budget-reconciliation flag stands — this decision does not resolve it and does not pretend to.

**OQ-4 — withdrawn.** It asked whether the "spec-added" `setMix` should be deleted. `setMix` is not
spec-added: roadmap line 208 mandates the "selector + **mix** pattern from the Innexus roadmap", and that
pattern is both halves — `Innexus-physical-modelling-roadmap.md:62` and `:1775` (`kBodyMixId`, "Dry/wet
blend of body resonance"). Answering the question with its stated non-default would have put the phase out
of compliance with line 208. See A-4 and the roadmap-coverage table.
**Confirmed (2026-07-27): `setMix` stays in `ContinuousBody`** — the body owns its own dry/wet blend and
FR-060 is unchanged.

**OQ-5 — Whether RA-1's `retune()` is the right shape.** FR-080 adds a method to a shipped Innexus
component. The alternatives are (a) restrict Strings to `keyTracking = 0` and drop the amendment, or
(b) build a Seraphis-local waveguide instead of reusing `WaveguideString`, which contradicts roadmap line
207. (a) makes the Strings material fail SC-004 by construction; (b) duplicates ~700 lines.
**Decided (2026-07-27): the strictly-additive `retune(float)`** on the shared component, inert unless
called (RA-1, FR-080 series, FR-084). The decision carries an explicit, non-optional condition:
`waveguide_string.h` is **shared DSP**, so RA-1 now holds the verified consumer table, the plan must carry
it forward as a named risk item, SC-014 regression-guards existing behaviour, and the build stage must run
every consumer's suite — `dsp_processors_tests`, `innexus_tests` **and `membrum_tests`**.

---

## Verification Notes

Everything asserted about existing code in this document was read this session from the working tree at
`f:/projects/iterum`, branch `feat/seraphis-phase1-life-modulators`. The roadmap was read in full
(`specs/Seraphis-roadmap.md`, 502 lines; Phase 4 at lines 198–222). The headers opened and quoted:
`processors/modal_resonator_bank.h`, `processors/modal_resonator_bank_simd.h`, `processors/iresonator.h`,
`processors/waveguide_string.h`, `processors/diffusion_network.h`, `processors/body_resonance.h`,
`processors/modal_resonator.h`, `processors/envelope_follower.h`, `processors/brownian_drift.h`,
`systems/timevar_comb_bank.h`, `systems/sympathetic_resonance.h`, `systems/feedback_network.h`,
`systems/harmonic_cloud.h`, `systems/spectral_morph_engine.h`, `core/modulation_source.h`,
`core/crossfade_utils.h`, `core/random.h`; plus `dsp/tests/CMakeLists.txt`,
`dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp`,
`dsp/tests/unit/systems/harmonic_cloud_test.cpp` (SC-008 pattern),
`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp`, and the four `tests/test_helpers/`
headers cited.

Two roadmap claims were checked and found false, and are recorded rather than assumed:
`ModalResonatorBankSimd` and `TimevarCombBank` are not real class names (C-1, C-2), and `DiffusionNetwork`
has no feedback path and therefore no decay (C-3). One capability gap was found by reading rather than by
assumption: `WaveguideString` cannot retune continuously (C-4). Two components behave opposite to their
naming and are recorded so the implementation does not rediscover them as bugs: `setBrightness` darkens the
waveguide (C-6) and `DiffusionNetwork` evaluates a `std::sin` per stage per sample regardless of modulation
depth (C-7). One prior-phase bookkeeping error was found: Phase 3's "APPLIED" amendment dispositions are not
present in `specs/Seraphis-roadmap.md` (RA-2).

**Second pass (review remediation).** The following were additionally opened and quoted this session, and
every citation in this document was re-checked against the working tree:
`dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:80-151`, `:386-412` (the CPU-gate pattern SC-005 now
reproduces in full); `modal_resonator_bank.h:670-783`, `:801-855` (the coupled recursion behind FR-032);
`waveguide_string.h:125-218`, `:370-448`, `:462-481`; `diffusion_network.h:30-56`, `:160-219`, `:325-403`;
`envelope_follower.h:150-249`, `:355-373`; `timevar_comb_bank.h:174-252`, `:460-680`;
`tests/test_helpers/artifact_detection.h:25-144`, `:175-219`; `tests/test_helpers/signal_metrics.h:316-369`;
`tests/test_helpers/audio_features.h:20-92`; `tests/test_helpers/spectral_analysis.h` (no f0 estimator);
`dsp/tests/unit/systems/harmonic_cloud_test.cpp:4800-4837` (the control-relative click precedent);
`tools/lint-layers.js:1-30`; `systems/poly_synth_engine.h:35-42`;
`specs/Innexus-physical-modelling-roadmap.md:58-66`, `:1770-1776`;
`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59`.
Citation drift corrected in this pass (comment banners and neighbouring lines, now pointing at
declarations): `timevar_comb_bank.h` `kMaxCombs` :87→:88, `setCombFeedback` :194→:197, `setCombDamping`
:199→:206, `setFundamental` :239→:238, `setModPhaseSpread` :286→:285; `harmonic_cloud.h` `kMaxPartials`
:137→:138, `kControlChunkSamples` :143→:144, pre-prepare idiom reconciled to `:887-891` in both places it
is cited; `artifact_detection.h` `ClickDetector` :89→:99; `test_kit_switch_infinite_ring.cpp`
`kSilenceThreshold` :57→:59.

**Third pass (clarification session, 2026-07-27).** The eight clarification questions and the five
Resolved Questions were answered by the user and encoded above (see **Clarifications**). New verification
performed in this pass, by grep over the working tree rather than from memory: the consumer sets of both
amended headers. `waveguide_string.h` → `dsp/tests/unit/processors/waveguide_string_test.cpp:10`,
`waveguide_string_dc_blocker_test.cpp:7`, `bow_waveguide_coupling_test.cpp:10`,
`plugins/innexus/src/processor/innexus_voice.h:24`,
`plugins/innexus/tests/unit/processor/waveguide_integration_test.cpp:10`,
`plugins/membrum/src/dsp/drum_voice.h:41`, `plugins/membrum/src/dsp/bodies/string_body.h:22`,
`tools/membrum_preset_generator.cpp`. `diffusion_network.h` →
`dsp/tests/unit/processors/diffusion_network_test.cpp:14`,
`dsp/include/krate/dsp/effects/shimmer_delay.h:32`, `effects/freeze_mode.h:31`,
`plugins/iterum/src/processor/processor.h:27`. The Membrum consumption of `WaveguideString` was **not**
in the earlier SC-014 suite list and is now (RA-1's cross-plugin table, SC-014). No Disrumpo source
includes either header; `disrumpo_tests` is retained in SC-014 only as a zero-cost belt-and-braces run.

---

## Review notes

Issues raised in review that were **not** adopted as stated. Every one of them identified a real defect —
each is fixed above — but three carried arithmetic or citations that would have been wrong to copy into the
spec, and are recorded here so a later reader does not "restore" them.

**RN-1 — the diffusion cascade's in-loop delay is ~57–64 ms, not "several hundred ms."** The review derived
a Schroeder allpass's mean delay as `M(1+g)/(1−g) ≈ 4.24·M`, which is the *unnormalised* sum
`Σ k·D·|h_k|` without dividing by `Σ|h_k| = 1 + 2g`. The energy-weighted centroid of the impulse response
of `H(z) = (−g + z^−D)/(1 − g z^−D)` is exactly `D` (the energies are `g²` at 0 and `(1−g²)²g^{2(k−1)}` at
`kD`, summing to 1, with centroid `D`), and the mean group delay of any stable allpass of order `D` is `D`
samples. The cascade's mean loop delay is therefore `Σ D_i = 3.2·size·17.777 ms`, i.e. 56.9 ms L / 64.1 ms
R at full size — a **2.5×** error against the old 37/41 ms assumption, not 3–10×. The defect is real and
FR-052 is rewritten; only the magnitude is corrected.

**RN-2 — `harmonic_cloud_perf_test.cpp` has no runtime `REQUIRE(measured ≤ kMaxAdmissibleBaseline)`.** The
review cited `:403-405` for one; that range is a `WARN` inside `if (measured > kMaxAdmissibleBaselineNsPerBlock)`
(`:401-407`). The runtime gate is `REQUIRE(measured <= kAutomatedBaselineNsPerBlock * kRegressionFactor)` at
`:412`. The teeth are the two `static_assert`s at `:142-143` and `:149-151`, which make the *relative*
runtime gate transitively bind the absolute figure. SC-005 adopts that composition — both `static_assert`s
plus the relative `REQUIRE` — rather than inventing a stricter runtime rule the precedent does not have,
and states the composition explicitly so the binding is not left implicit.

**RN-3 — no inharmonicity metric orders `MetalPlate > Ice > Glass`, and the review's replacement does not
either.** Under the review's suggested metric (mean `|ratio_k − k|`, unnormalised and uncapped) the
computed values from FR-012's tables are Glass ≈ 8.19, Ice ≈ 8.29, Metal Plate ≈ 0.99 — Glass/Ice are ~8×
*more* inharmonic than Metal Plate, the same direction as under the review's nearest-integer figures
(0.22 vs 0.15), not the reverse. The old ordering is therefore not merely mis-stated but backwards for a
physical reason: Glass's `n(n²−1)/√(n²+1)` law diverges from the harmonic series far faster than a
Chladni plate's ratios do. SC-003(c) now states the ordering the tables actually produce, with ratio-based
thresholds (`≥ 5×`, `≥ 3×`) rather than a bare ordering, and Glass↔Ice — which share a ratio table and
differ by ~1 % on any inharmonicity metric — is separated by per-peak frequency displacement instead.

**RN-4 — `A-3`'s "32 is the largest power-of-two mode count" rationale is kept, not deleted.** The review
was right that most Glass modes land above Nyquist at 220 Hz, but the fix is FR-043's count truncation, not
a smaller ceiling: at low pitches all 32 modes are audible and wanted, and 32 remains the right ceiling for
the CPU reason A-3 gives. A-3 now records both bounds.
