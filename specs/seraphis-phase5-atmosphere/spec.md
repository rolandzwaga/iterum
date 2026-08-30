# Feature Specification: Seraphis Phase 5 — Granular Atmosphere Engine

**Spec slug:** `seraphis-phase5-atmosphere`
**Roadmap source:** `specs/Seraphis-roadmap.md` → Part A → Phase 5 (lines 227–248); reuse-inventory row
line 90; cross-cutting constraints lines 485–496.
**Layers:** one new Layer 3 component (`AtmosphereEngine`), plus one strictly-additive amendment to an
existing Layer 1 component (`RollingCaptureBuffer`).
**Depends on:** Phase 1 only, and only for `BrownianDrift`'s Ornstein–Uhlenbeck recurrence
(`processors/brownian_drift.h:97-105`, `:228-262`), which Phase 5 reproduces as an SoA **per-grain** lane
bank exactly as Phase 2 already does (`systems/harmonic_cloud.h:1125-1148`) — see C-5. Phases 2/4
are *not* compile-time dependencies — like `ContinuousBody`, this engine consumes a stereo audio stream,
not a `HarmonicCloud` or `ContinuousBody` instance (roadmap lines 479–481 make 2/4/5/6 mutually
independent).
**Plugin work:** none. KrateDSP only, unit-tested. The Seraphis plugin starts at Phase 8.

---

## Overview

Phase 5 delivers the third generator: **frozen moments and cloud particles, not slicing** (roadmap line
230). `AtmosphereEngine` is a parallel per-voice texture layer that continuously captures the voice's own
cloud+body output into a rolling ring, then re-plays fragments of that history as **ultra-long grains
(50 ms – 30 s)** with per-grain pitch offset, stereo placement and decorrelation, optionally smeared into
fog by an STFT phase-randomisation stage, and optionally replaced by a pure spectral freeze drone
(roadmap lines 233–246).

The genuinely new engineering is **grain-lifetime management**: the existing grain infrastructure
(`GrainPool`, `GrainProcessor`, `GranularEngine`) assumes short grains reading a short delay line, and
breaks in three independent ways at 30 s — `float` read positions lose sub-sample precision, the
64-grain pool steals mid-flight (a click on a 30 s grain), and the read head can be lapped by the write
head. This spec pins an exact, testable **liveness invariant** for every grain over its whole life, and
sizes every pool at `prepare()` so nothing allocates on the audio thread (roadmap line 246).

The single new component is `AtmosphereEngine` (Layer 3,
`dsp/include/krate/dsp/systems/atmosphere_engine.h`), matching the roadmap's own header path (line 233).

Every claim below about existing code was verified by opening the header in the session that produced
this document; each cites `file:line`. Where the roadmap names a component that does not exist, does not
have the stated capability, or cannot be used the stated way inside the stated budget, the discrepancy is
recorded in **Roadmap-vs-Reality Corrections** rather than silently papered over.

---

## Clarifications

### Session 2026-07-28

- **Q1 — FR-025: what is `w` when the drift envelope straddles `r = 1`?** → `w` is redefined as the
  **sum of the one-sided excursions**, `w := (rₘₐₓ − 1)⁺ + (1 − rₘᵢₙ)⁺`. It reduces to `|1 − r|` exactly
  when drift is zero, so SC-002 clause 2's closed form is untouched; straddling grains get proportionally
  shorter closed-form lifetimes. FR-025 stays an **unconditional closed-form** guarantee — no runtime
  clamp.
- **Q2 — Are `pitchSemitones` / `pitchSpread` / `driftRangeSemitones` snapshotted at birth or read live?**
  → **Snapshotted** into `AtmosphereGrain` at birth (`s`, `rₘᵢₙ`, `rₘₐₓ` and `L′` frozen per grain); the
  drift lane value stays live but is scaled by the snapshotted range. The pitch knobs therefore affect
  only newly born grains. The FR-009/FR-030 contradiction is resolved in this direction.
- **Q3 — What is the engine's state after `silence()`'s ramp completes?** → **It latches.** After the
  10 ms ramp the engine is muted and no longer scheduling until an explicit `reset()` (or `prepare()`).
  `reset()` is the documented re-entry, and FR-063's internal-non-finite path uses the same recovery.
  No auto-resume.
- **Q4 — Where does the `1/√n` law apply?** → FR-034's per-grain `amplitude` term is **deleted** (it would
  be identically 1). The smoothed `1/√n` population compensation is a **single multiply on the summed
  stereo bus** (FR-028 as written). SC-008's statistical bound holds as stated.
- **Q5 — Is the freeze drone routed through blur, and is its latency compensated?** → The freeze leg
  **bypasses blur spectrally but runs through a `prepare`-allocated `blurFftSize`-sample stereo delay**,
  so both crossfade legs share one layer latency. `getLatencySamples()` reports the single honest number
  (`blurEnabled ? blurFftSize : 0`) for both legs. FR-050's pure-freeze semantics are preserved.
- **Q6 — At settled `freezeMix = 1.0`, is the grain layer still processed?** → **Yes** — scheduler,
  ageing, ring reads and blur all keep running. SC-004 configuration (d) therefore measures the honest
  grain+freeze worst case for Phase 7's budget tally, and freeze release is seamless because the grain
  population never lapsed.
- **Q7 — Does blur draw one phase perturbation per bin, or one per bin per channel?** → **Per bin per
  channel** (L then R, from the one blur stream). Blur produces fog **and** progressive stereo
  decorrelation; this is stated explicitly as intended behaviour and is reflected in SC-005. Per-source
  decorrelation is Phase 5's business; global width stays Phase 7's.
- **Q8 — Drift-lane binding at grain birth, and slot allocation.** → At each grain birth lane `i`'s OU
  walk state is **zeroed without re-seeding its RNG stream**, so every grain starts exactly at its
  snapshotted pitch and drifts away over its life (no birth-time pitch step). Slot allocation is
  **round-robin** over all `kMaxGrains`, so successive grains decorrelate across lanes.
- **OQ-1 — What is "default density" in the roadmap's CPU budget?** → density **4 grains/s ×
  `grainSeconds` 4 s = 16 concurrent**. SC-004 additionally gates the saturated 64-grain configuration
  against the **same** 1 % reference; both must pass.
- **OQ-2 — Per-voice capture length vs. polyphony (RA-2).** → `captureSeconds` stays a `prepare()`
  argument defaulting to **8 s** (4.19 MB/voice). The shipped value, and any move to a shared ring, are
  **Phase 7** decisions; RA-2's memory table stands as the Phase 7 reconciliation input. Long grains
  re-read the ring cyclically, bounded by FR-025's closed form.
- **OQ-3 — Is `blurFftSize = 1024` the right default?** → Yes: default **1024**, configurable over
  [256, 4096] at `prepare`. If SC-004(b) fails on measured hardware the **sanctioned** fallback is a
  default of 512 (a specified capability trade) — never a raised baseline.

---

## Scope

**In scope (this phase ships all of it):**

1. `AtmosphereEngine`: a stereo-in / stereo-out Layer 3 system with `prepare` / `reset` / `silence` /
   `processStereoBlock`, a fully pinned control table, and seeded determinism.
2. Self-granulating capture: a `RollingCaptureBuffer` written from the engine's own input, plus a
   strictly-additive fractional read accessor on that shared primitive (RA-1).
3. Ultra-long grain engine: 50 ms – 30 s lifetimes, fixed 64-grain pool, no stealing, exact
   integer+fraction read positions, per-grain pitch / pan / decorrelation, `1/√n` amplitude
   normalisation.
4. Spectral blur: one shared stereo STFT ↔ OverlapAdd stage on the summed grain output, phase
   randomisation amount 0–1, transparent at 0.
5. Pure-freeze mode: two `SpectralFreezeOscillator` instances (L/R) captured on demand from the ring,
   crossfaded against the grain layer, and delay-matched to the blur stage so the layer reports one
   latency (FR-052).
6. Life modulation: a **per-grain** Ornstein–Uhlenbeck drift lane bank (`kMaxGrains` SoA lanes, advanced
   on the control grid, walk state zeroed at each grain birth) continuously modulating each live grain's
   playback ratio — the roadmap's "pitch drift per grain" (line 243), not a single engine-wide walk.
7. Output stage: level and finiteness hygiene only — **no width control** (N-9).
8. Unit tests covering every FR and every SC, registered in `dsp/tests/CMakeLists.txt`.

**Non-goals (owned by later phases, or deliberately excluded):**

- **N-1 — No voice, no envelope, no note handling.** `AtmosphereEngine` has no `noteOn`. The voice
  envelope, spatial azimuth and per-voice seed spread are Phase 7 (roadmap lines 285–292).
- **N-2 — No wiring to `HarmonicCloud` / `ContinuousBody`.** The engine takes a stereo stream. Phase 7
  connects the tap.
- **N-3 — No dry/wet mix.** The roadmap calls this a "parallel layer, mixed in" (lines 48–52). The engine
  outputs the wet texture only; the caller mixes. No `setMix`.
- **N-4 — No plugin parameters, no UI, no presets.** Phases 8/9/11/12.
- **N-5 — No changes to `GranularEngine`, `GrainProcessor`, `GrainPool` or `GranularDelay`.** Correction
  C-6 records a defect found in `GrainProcessor`'s traversal arithmetic; fixing it would change Iterum's
  shipped granular-delay sound and is explicitly out of this phase's scope. Phase 5 does not consume that
  code path.
- **N-6 — No `SlicePool` use.** Correction C-2 shows the per-grain snapshot the roadmap suggests costs
  368 MB per voice; the shared ring replaces it.
- **N-7 — No global/engine-level freeze.** "Spectral freeze (global capture-and-hold of the Aether tail)"
  is Phase 10 (roadmap line 439). Phase 5's freeze is per-voice and reads the voice's own ring.
- **N-8 — No reverse grains, no grain quantisation, no texture/amplitude chaos.** `GranularEngine` has
  those (`granular_engine.h:123`, `:145`, `:156`); the roadmap's Phase 5 list does not, and this spec
  invents nothing.
- **N-9 — No stereo width control, and therefore no `stereo_utils` use.** The roadmap's Phase 5 feature
  list is exhaustive over three items — pitch drift per grain, density, spatial diffusion (lines
  243–244) — and names no output stage. Stereo width is a **Phase 7** responsibility ("spatial position",
  per-voice azimuth via `OrbitModulator` + `StereoField`, roadmap lines 287–288); adding a second width
  control here would put two knobs on one axis. `stereoCrossBlend` (`core/stereo_utils.h:41-49`) is
  therefore not used, and `stereo_utils.h` is absent from FR-002's include list (see C-7). The one
  output-stage control that survives is `setLevel` (FR-061): the engine sums up to `kMaxGrains` grains
  under a `1/√n` law and the caller cannot scale the result without a second buffer pass, so the trim has
  to live inside the engine. It is not a dry/wet mix (N-3).

---

## Roadmap-vs-Reality Corrections

Eight items. All were verified by reading the named header this session.

| # | Roadmap says | Reality (verified) | Consequence for this spec |
|---|---|---|---|
| **C-1** | "Extends grain infrastructure (`GrainPool`, `GrainScheduler`, `GrainEnvelope`) for ultra-long grains" (lines 236–237) | Of the three, only two are usable unchanged. `GrainEnvelope` (Layer 0, `core/grain_envelope.h`) is a free-function table generator/interpolating lookup (`:33`, `:165`) — fully reusable. `GrainScheduler` (`processors/grain_scheduler.h:29`) is a per-sample trigger clock with `setDensity` (`:46`), `setJitter` (`:64`), `process()` (`:73`) and `seed()` (`:97`) — fully reusable. **`GrainPool` is not**: `Grain::readPosition` is a `float` (`primitives/grain_pool.h:24`), and at a 30 s lifetime the read position reaches 1 440 000 samples at 48 kHz, where the `float` ULP is 2²¹·2⁻²⁴ = **0.125 samples** — sub-sample interpolation degrades to eighth-sample quantisation. Additionally `acquireGrain` **steals the oldest grain** when the pool is exhausted (`:71–91`) by hard-resetting it mid-envelope, and `activeGrains()` **rebuilds the active-pointer list on every call** (`:107–116`), which `GranularEngine` invokes once per sample (`systems/granular_engine.h:213`). | FR-020: `AtmosphereEngine` owns a private fixed `std::array` of a nested `AtmosphereGrain` struct with an **integer + fraction** read position (FR-024), a **skip-never-steal** exhaustion policy (FR-023), and a persistent active-index list. `GrainScheduler` and `GrainEnvelope` are reused verbatim (FR-021, FR-027). |
| **C-2** | "per-grain reference into a slice-pool snapshot — `SlicePool` pattern applies" (lines 238–239) | `SlicePool::prepare(size_t maxSlices, double sampleRate, size_t maxSliceSamples)` (`primitives/slice_pool.h:160-161`) gives **every** slice its own `std::vector<float>` L and R buffers of `maxSliceSamples` (`:89-94`, `:104-105`). A 30 s stereo snapshot at 48 kHz is 1 440 000 × 2 × 4 B = **11.5 MB**; a 32-slice pool is **368 MB per voice**, ×16 voices = 5.9 GB. Not viable at any polyphony. | N-6: `SlicePool` is not used. FR-011 makes the **ring itself** the snapshot and FR-025 proves liveness analytically, so per-grain storage is ~64 bytes (read position, envelope phase, pan gains, decorrelation offset, plus the pitch snapshot `s`, `d`, `rₘᵢₙ`, `rₘₐₓ`, `L′` — FR-009's snapshot rule) instead of 11.5 MB. |
| **C-3** | "Source: `RollingCaptureBuffer` tapping the voice's output" (lines 234–235) | `RollingCaptureBuffer` (`primitives/rolling_capture_buffer.h:50`) has exactly one read method: `extractSlice(float* outL, float* outR, size_t lengthSamples, size_t offsetSamples) const` (`:141-142`), which **copies** into caller-owned buffers. There is no random-access or fractional read, so a grain cannot read it in place. Its `samplesWritten_` also saturates at capacity (`:119-121`), so it exposes no monotonic total-written counter. | **RA-1**: a strictly-additive `readStereoLinear(float ageSamples, float&, float&) const noexcept` on `RollingCaptureBuffer` (FR-080 series). The monotonic counter is *not* added — `AtmosphereEngine` keeps its own `std::uint64_t` write counter (FR-013), since it is the writer. |
| **C-4** | "Spectral blur: **per-grain** STFT magnitude smearing … using existing `STFT`/`SpectralBuffer`" (lines 241–242) | A per-grain STFT is impossible inside the phase's own CPU budget. One `SpectralFreezeOscillator`-class STFT pipeline is documented at "< 0.5 % CPU single core @ 44.1 kHz, 512 samples, 2048 FFT" (`processors/spectral_freeze_oscillator.h:20`). With up to 64 concurrent grains (FR-022) that is ~32 % of one core **per voice** against a 1 % budget (roadmap line 247), and the memory is ~90 KB × 64 = 5.8 MB per voice (`spectral_freeze_oscillator.h:41-43`). | FR-040: blur is **one shared stereo STFT ↔ OverlapAdd stage on the summed grain output**, not per grain. The audible effect the roadmap asks for (phase decoherence → fog) is unchanged; only the placement moves. |
| **C-5** | "Pitch drift per grain (`BrownianDrift` again)" (line 243) | Per-grain drift is affordable, and the repo proves it at **half** this phase's budget. `HarmonicCloud` — roadmap budget 0.5 %/voice (line 164) — runs **two** SoA banks of 64 independent OU lanes: `struct DriftLanes` over `kMaxPartials = 64` (`systems/harmonic_cloud.h:1125-1148`, `:138`) at `kDriftControlInterval = 32` (`:148`), the same 32-sample rate, and it advances **both** banks every control chunk (`:1671-1672`) = 2 048 lane control-steps per 512-sample block. Its measured automated baseline is **26 000 ns/block** (`dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:140`) against a 53 333 ns reference (`:80`) — the *entire* cloud, 128 drift lanes included, at 0.24 % of one core. Phase 5 needs 1 024 lane-steps against a reference twice as large. Its own arithmetic agrees: 106 667 ns / 1 024 = **~104 ns available per lane step**, while a lane step is three `Xorshift32::nextFloat()` draws (`processors/brownian_drift.h:257-259`) plus one AR(1) update (`:262`) plus a `setTarget` — one to two orders of magnitude under 104 ns. A grain frozen in pitch for up to 30 s would also contradict the roadmap's first Key Design Decision, "Nothing is ever static" (lines 71–72), in the phase whose objects live longest. | FR-030: **per-grain** drift, as a `kMaxGrains`-lane SoA OU bank (never 64 `BrownianDrift` objects), advanced on FR-005's absolute control grid and seeded per slot via `deriveStreamSeed` (`core/random.h:102-111`). FR-025's liveness invariant is restated over the **bounded** drift envelope so it stays closed-form. Per-grain *static* spread (FR-031) is unchanged. SC-004 measures the lane cost in all four configurations. |
| **C-6** | (implicit) `GrainProcessor` can traverse a captured buffer at a pitch ratio | Verified defective for any ratio but one. `GrainProcessor::processGrain` reads `delayBuffer.readLinear(grain.readPosition)` and then advances `grain.readPosition += std::abs(grain.playbackRate)` (`processors/grain_processor.h:129-143`). `DelayLine::readLinear(d)` addresses `(writeIndex_ - 1 - d) & mask_` (`primitives/delay_line.h:296-298`) — a delay **relative to a write head that also advances one sample per call**. The absolute source index therefore moves by `1 − r` per output sample, not `r`. It is stationary (DC) at `r = 1.0` (0 semitones) and runs **backwards** for `r > 1`. Correct traversal requires `Δdelay = 1 − r`; the code uses `Δdelay = +|r|`, which agrees only at `r = 0.5` (−12 semitones). | N-5: **not fixed here** — `GrainProcessor` is shipped Iterum granular-delay code (`effects/granular_delay.h:22` → `systems/granular_engine.h:20`) and changing it changes released sound. Phase 5 does not use it; FR-024 states its own traversal in the **age** domain, where the arithmetic is `age += (1 − r)` and is unit-tested (SC-002). Recorded so a later reader does not "harmonise" the two. |
| **C-7** | "spatial diffusion (per-grain pan spread + decorrelation via `stereo_utils`)" (lines 243–244) | `core/stereo_utils.h` contains exactly **one** function, `stereoCrossBlend(inL, inR, crossAmount, outL, outR)` (`:41-49`), a width/ping-pong blend. It performs **no decorrelation** — a cross-blend of two correlated inputs stays correlated. The roadmap names `stereo_utils` *for decorrelation*, and the only thing in that header cannot decorrelate. | Decorrelation is implemented as a **per-grain L/R read-age offset** of 0–30 ms (FR-033), which is genuinely decorrelating and costs one extra ring read per grain-sample. `stereoCrossBlend` is **not** retained under another job title: the only role it could fill here is output width, which the roadmap does not ask Phase 5 for and Phase 7 owns (lines 287–288). See N-9; `stereo_utils.h` is therefore absent from FR-002's include list and FR-060 states that no width control exists. |
| **C-8** | "zero allocation after prepare (**30 s × density worst case pre-allocated** and asserted)" (lines 245–246) | The "zero allocation" half is met exactly (FR-003, SC-001). The parenthetical's *sizing* half is not reachable. At the control table's maxima the 30 s × density worst case is 30 s × 20 grains/s = **600** concurrent grains. 600 grains × 512 samples = 307 200 grain-samples per block; against SC-004's 106 667 ns reference that is **0.35 ns per grain-sample** — roughly one cycle at 3 GHz for two interpolated ring reads into a multi-megabyte buffer, one envelope lookup, one drift-lane read and four multiply-adds. The binding constraint is CPU, not memory: pre-allocating 600 slots is trivial (~38 KB at FR-020's ~64 B/grain) and would still be unserviceable. | The pool is capped at `kMaxGrains = 64` (FR-022 — provisional, measurement-backed), the excess is handled by FR-023's **skip-never-steal** policy, and FR-073 documents `density × grainSeconds ≤ kMaxGrains` as the intended operating region. This is a real deviation from a roadmap success criterion, so the Traceability row for lines 245–246 cites C-8, FR-022 and FR-023 next to SC-001 rather than showing an unqualified tick. |

---

## Recorded Roadmap Amendments

Consequences of decisions taken here that touch shipped code or downstream budgets. Recorded so a later
phase does not inherit a silent contradiction. None is licence to relax a Phase 5 threshold.

### RA-1 — `RollingCaptureBuffer` gains a fractional random-access read

**What:** one new `const` method on `RollingCaptureBuffer` (`primitives/rolling_capture_buffer.h:50`)
returning a linearly-interpolated stereo sample at a fractional age behind the write head, without
copying. Specified in the FR-080 series.

**Why it is an amendment:** `RollingCaptureBuffer` ships from spec `069-pattern-freeze` (header banner
`:16`) and is shared DSP, not a Seraphis-local file. Phase 5 extends a component outside its own phase —
the same pattern Phase 3 used for `HarmonicCloud::setSpectralTarget` and Phase 4 used for
`WaveguideString::retune`.

**Containment:** the addition is inert unless called; no existing member changes behaviour (FR-084).

**Cross-consumer impact (verified by `grep -rn "rolling_capture_buffer.h" dsp/ plugins/ tools/` this
session):**

| Consumer | Site | Suite that must be run unedited |
|---|---|---|
| `PatternFreezeMode` | `dsp/include/krate/dsp/effects/pattern_freeze_mode.h:40` | `dsp_effects_tests` |
| header-compile lint | `dsp/lint_all_headers.cpp:70` | (build) |
| existing unit tests | `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp:20` | `dsp_primitives_tests` |

No `plugins/` consumer exists. SC-012 regression-guards existing behaviour.

### RA-2 — Per-voice 30 s capture does not scale to 16 voices; Phase 7 must reconcile

`RollingCaptureBuffer::prepare` rounds capacity **up to the next power of two**
(`rolling_capture_buffer.h:83`, `:210-220`) and allocates two `float` vectors (`:86-87`). Exact figures:

| Capture seconds **requested** | Samples @48 kHz | Rounded capacity | **Ring length actually obtained** | Bytes / voice | ×16 voices |
|---|---|---|---|---|---|
| 4 s | 192 000 | 262 144 | 5.46 s | 2.10 MB | 33.6 MB |
| 8 s (default) | 384 000 | 524 288 | 10.92 s | 4.19 MB | 67.1 MB |
| 16 s | 768 000 | 1 048 576 | 21.85 s | 8.39 MB | 134 MB |
| 30 s (max) | 1 440 000 | 2 097 152 | 43.69 s | 16.8 MB | **268 MB** |

**The rounding makes the ring's length in *seconds* sample-rate dependent**, and this is load-bearing for
SC-009, not a footnote. `captureSeconds = 8` gives 352 800 → 524 288 = **11.89 s** at 44.1 kHz, but
384 000 → 524 288 = **10.92 s** at 48 kHz and 768 000 → 1 048 576 = **10.92 s** at 96 kHz — an **8.8 %**
spread in `C / sampleRate`. FR-025's truncation is stated in samples against that same `C`, so any
configuration in which truncation binds has a rate-dependent grain lifetime. SC-009 therefore separates
the non-truncating case (rate-invariant, tight threshold) from the truncating case (expectation computed
from that rate's own `getCaptureCapacitySamples()`). Callers who need a rate-invariant ring length must
request one whose sample count is already a power of two at every rate they support; Phase 5 does not
change `RollingCaptureBuffer`'s rounding (FR-084).

At 96 kHz every byte figure doubles. Phase 5 therefore makes capture length a **`prepare()`-time argument**
(FR-010) with an 8 s default, and the 30 s grain ceiling is reachable only when the caller prepares a
ring long enough to satisfy FR-025. **Flag for Phase 7:** either size capture down at high polyphony, or
share one capture buffer across voices (which changes "the organism feeds on itself" from per-voice to
per-instrument). Phase 5 does not decide this; it makes both reachable.

### RA-3 — Blur adds fixed latency to the atmosphere layer

When blur is enabled at `prepare()`, the grain sum is routed through STFT ↔ OverlapAdd unconditionally
(FR-041), so the layer's output is delayed by `STFT::latency() == fftSize` (`primitives/stft.h:160`) =
**1024 samples** (21.3 ms at 48 kHz). This is reported by `getLatencySamples()` (FR-046) and is
**constant for a prepared configuration** — the blur knob never changes it. Phase 7 must either
delay-compensate the parallel dry path by the same amount or accept 21 ms of layer offset (for a wash
texture, acceptable; the decision is Phase 7's, not this spec's).

**The layer has one latency, not two.** The freeze leg bypasses blur *spectrally* (FR-050's hold stays
pure) but is routed through a `prepare`-allocated `blurFftSize`-sample stereo delay before the crossfade
(FR-052), so both crossfade legs are aligned and `getLatencySamples()` describes **both**. The freeze
oscillator's own `getLatencySamples()` (`spectral_freeze_oscillator.h:421-423`) is not added to the
figure: the drone is synthesised, not a delayed copy of the input, so it has no dry counterpart to align
against.

### RA-4 — The roadmap's per-voice CPU budgets do not sum to its Phase 7 ceiling

Phase 7 gates "16 voices, everything on, ≤ 25 % of one core @ 48 kHz" and asserts that this figure "sets
per-voice budgets from phases 2/4/5 with headroom" (roadmap lines 303–304). It does not. The tally:

| Term | Roadmap figure | Source | ×16 voices |
|---|---|---|---|
| Harmonic Cloud | 0.5 % / voice | roadmap line 164 | 8 % |
| Continuous Body | 1 % / voice | roadmap line 223 | 16 % |
| **Atmosphere (this phase)** | **1 % / voice** | roadmap line 248 | **16 %** |
| Aether reverb | 5 % global | roadmap line 276 | 5 % |
| | | **Total** | **45 %** — 80 % over the 25 % ceiling |

> **AMENDED 2026-07-28 — what Phase 5 actually hands Phase 7.** The row above is the roadmap's
> *claim*; it is superseded as an input to Phase 7's tally by Phase 5's **measured** figures. Phase 5's
> gate is now 1.5 % / voice (see SC-004's dated amendment), but **Phase 7 must not tally 1.5 % either** —
> a gate is a ceiling, and RA-4's whole point is that tallying ceilings nobody reached is how this
> contradiction was manufactured. Tally the measurements:
>
> | what a Phase 5 voice actually costs | measured ns/block | % of one core | ×16 voices |
> |---|---|---|---|
> | grain + blur, the shipped default (b) | 111 815 | 1.048 % | 16.8 % |
> | grain + blur + freeze drone, worst in-region (d) | 153 651 | 1.440 % | 23.0 % |
>
> So Phase 5's honest contribution is **1.048 %/voice** for an unfrozen voice and **1.440 %/voice** for a
> frozen one — not 1 %, and not the 1.5 % gate. The saturated-64 configuration (c), at 3.223 %, is
> **out-of-region** and must not enter the tally at all; if Phase 7 wants it in region, that is a Phase 7
> decision about `kMaxGrains`/polyphony, not a Phase 5 threshold.

Even using Phase 4's *self-restricted* single-engine figure of 0.5 %
(`dsp/tests/unit/systems/continuous_body_perf_test.cpp:118-129`, which halves its own reference with the
explicit reasoning that 2 % per voice "would put 16 voices at 32 % of one core against the roadmap's 25 %
full-poly ceiling … and would hide the relaxation inside a success criterion where Phase 7's tally would
never see it"), the tally is 0.5 + 0.5 + 1.0 = 2 % × 16 = 32 %, plus 5 % global = **37 %**. Halving Phase
5 as well gives 24 % + 5 % = **29 %**. There is no assignment of the roadmap's own per-phase numbers that
closes at 16 voices.

**What Phase 5 does about it.** Nothing that touches its own gate, because the contradiction is not
Phase 5's to resolve and quietly shaving this phase would not close it either:

1. ~~SC-004 keeps the roadmap's **1 %** as the gate, applied to **all four** configurations with no
   configuration exempted, and forbids raising any baseline (its lever list).~~
   **Superseded 2026-07-28:** the gate is 1.5 % for (a), (b), (d), (e), and (c) is out-of-region. That
   change was made *after* the levers were spent and *by the user*, from the five measured figures — not
   by shaving the phase to fit an aggregate it cannot fix. The prohibition on raising a **baseline**
   stands unchanged, as does the lever list.
2. SC-004 additionally requires that every baseline be the **measured worst-of-N** figure with no padding
   (amended: at most +5 % for run-to-run spread, capped at `reference / 1.5`, with the capped
   configurations named), and that the five measured ns/block numbers be copied verbatim into this
   phase's compliance document. Phase 7's tally then works from five real numbers rather than from a
   ceiling nobody touched — see the amendment box above for which two of them it should actually add up.
3. **Flag for Phase 7 (blocking there, not here):** the 25 % full-poly ceiling requires a voice-count
   reduction (roadmap Open Question 4, line 503, offers 8/12/16), a per-engine reduction, or an amendment
   to the ceiling itself. Phase 5 does not resolve it, and no Phase 5 threshold may be relaxed on the
   grounds that the aggregate is already over.

---

## Existing components (reused — verified signatures)

| Component | Header (layer) | Verified signature / fact | What Phase 5 reuses |
|---|---|---|---|
| `RollingCaptureBuffer` | `primitives/rolling_capture_buffer.h:50` (L1) | `void prepare(double sampleRate, float maxDurationSeconds) noexcept` (`:75`); `void writeStereo(float,float) noexcept` (`:113`); `size_t getAvailableSamples() const noexcept` (`:204`); capacity rounded to next power of 2 (`:83`) | The capture ring itself (FR-010–FR-013), **plus RA-1's new read** |
| `GrainScheduler` | `processors/grain_scheduler.h:29` (L2) | `void prepare(double) noexcept` (`:33`); `void setDensity(float grainsPerSecond) noexcept` clamped ≥ 0.1 (`:46-49`); `void setJitter(float) noexcept` (`:64`); `[[nodiscard]] bool process() noexcept` (`:73`); `void seed(uint32_t) noexcept` (`:97`); `enum class SchedulingMode : uint8_t { Asynchronous, Synchronous }` (`:22-25`) | Grain trigger timing and jitter, verbatim (FR-021) |
| `GrainEnvelope` | `core/grain_envelope.h` (L0) | `enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman, Linear, Exponential }` (`:14-21`); `inline void generate(float* out, size_t size, GrainEnvelopeType, float attackRatio=0.1f, float releaseRatio=0.1f) noexcept` (`:33-34`); `[[nodiscard]] inline float lookup(const float* table, size_t tableSize, float phase) noexcept` — linearly interpolated, phase clamped to [0,1] (`:165-166`, `:172-181`) | Grain window table + interpolated lookup, verbatim (FR-027) |
| `BrownianDrift` | `processors/brownian_drift.h:94` (L2) | `class BrownianDrift : public ModulationSource`; `void prepare(double) noexcept` (`:121`); `void setSeed(std::uint32_t) noexcept` (`:145`); `void setSmoothness(float) noexcept` (`:152`); `void setDepth(float) noexcept` (`:159`); `void processBlock(size_t numSamples) noexcept` (`:194`) carries `samplesUntilControl_` across calls (`:197-205`); `[[nodiscard]] float getCurrentValue() const noexcept override` clamps to [−1,+1] (`:212`); `kControlRateInterval = 32` (`:105`); OU coefficients from `kTauMin`/`kTauMax`/`kInternalStd` (`:97-101`, `:228-240`); control step = three `nextFloat()` draws + AR(1) + `setTarget` (`:252-262`) | **The recurrence, not the class.** FR-030 reproduces it as a `kMaxGrains`-lane SoA bank, exactly as `HarmonicCloud::DriftLanes` does (`systems/harmonic_cloud.h:1125-1148`) — and, like `HarmonicCloud` (whose include list at `:17-23` has no `brownian_drift.h`), the Phase 5 **header does not include it**. The *test* does: SC-002's lane-equivalence gate builds a reference `BrownianDrift` seeded with `deriveStreamSeed(seed, slot)` and requires lane and reference to agree |
| `deriveStreamSeed` | `core/random.h:102-111` (L0) | `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser with an explicit non-zero substitution, because `Xorshift32::seed()` silently replaces 0 with its own default (`:72-74`) and two streams hashing to 0 would collapse onto one | Per-slot drift-lane seeds and the four top-level RNG streams (FR-030, FR-070) |
| `detail::isNaN` / `detail::isInf` | `core/db_utils.h:54-57` / `:175-178` (L0) | `constexpr bool isNaN(float) noexcept` and `[[nodiscard]] constexpr bool isInf(float) noexcept`, both IEEE-754 exponent-field tests on a `std::bit_cast<std::uint32_t>`; the `-ffast-math` rationale is documented at `:44-52`. Already relied on by `OnePoleSmoother::setTarget` (`primitives/smoother.h:170`) and `BrownianDrift` (`processors/brownian_drift.h:64-68`) | **The** finiteness test (FR-008, FR-063). Phase 5 does not write a fourth reimplementation of it |
| `STFT` | `primitives/stft.h:35` (L1) | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` (`:58-63`); `void pushSamples(const float*, size_t) noexcept` (`:104`); `[[nodiscard]] bool canAnalyze() const noexcept` (`:120`); `void analyze(SpectralBuffer&) noexcept` (`:130`); `[[nodiscard]] size_t latency() const noexcept` returns `fftSize_` (`:160`) | Blur analysis (FR-040–FR-043) |
| `OverlapAdd` | `primitives/stft.h:181` (L1) | `void prepare(size_t fftSize, size_t hopSize, WindowType, float kaiserBeta, bool applySynthesisWindow) noexcept` (`:206-212`); doc at `:201-204`: synthesis windowing "Required for spectral modification processors … at ≥75 % overlap where Hann² satisfies COLA. Must NOT be used at 50 % overlap"; `void synthesize(const SpectralBuffer&) noexcept` (`:266`); `void pullSamples(float*, size_t) noexcept` (`:305`); COLA normalisation computed in `prepare` (`:226-239`) | Blur synthesis at 75 % overlap with `applySynthesisWindow = true` (FR-041) |
| `SpectralBuffer` | `primitives/spectral_buffer.h:45` (L1) | `void prepare(size_t fftSize) noexcept` (`:61`); `[[nodiscard]] float getMagnitude(size_t) const noexcept` (`:84`); `[[nodiscard]] float getPhase(size_t) const noexcept` (`:91`); `void setMagnitude(size_t,float) noexcept` (`:98`); `void setPhase(size_t,float) noexcept` (`:106`); `[[nodiscard]] size_t numBins() const noexcept` (`:166`) | Per-bin phase randomisation (FR-042) |
| `SpectralFreezeOscillator` | `processors/spectral_freeze_oscillator.h:80` (L2) | `void prepare(double sampleRate, size_t fftSize = 2048) noexcept` (`:105`); `void freeze(const float* inputBlock, size_t blockSize) noexcept` (`:217`) — **mono**; `void unfreeze() noexcept` (`:295`); `void processBlock(float* output, size_t numSamples) noexcept` (`:317`) — **mono**; `[[nodiscard]] size_t getLatencySamples() const noexcept` (`:421`); `setPitchShift` (`:384`), `setSpectralTilt` (`:395`) | Pure-freeze mode, **two instances** (L and R) because the API is mono (FR-050–FR-053) |
| `OnePoleSmoother` / `LinearRamp` | `primitives/smoother.h:134` / `:305` (L1) | `configure(float ms, float sampleRate)` (`:160` / `:329`) computes a **per-`process()`-call** coefficient (`:160-164`), and `process()` advances **exactly one sample** (`:197`); `setTarget(float)` (`:170` / `:342`) sanitises NaN/Inf via `detail::isNaN`/`isInf`; `void advanceSamples(size_t)` is the closed-form N-sample advance (`:243-254`); `snapTo(float)` (`:263` / `:421`) | Amplitude-normalisation, level, blur and freeze-mix smoothing (FR-009). **Cadence is part of the spec**: a smoother read once per blur frame must be advanced by `advanceSamples(hopSize)`, not `process()`, or its 50 ms time constant becomes 50 ms × hopSize |
| `Xorshift32` | `core/random.h:41` (L0) | `[[nodiscard]] constexpr float nextFloat() noexcept` → [-1,1] (`:59`); `[[nodiscard]] constexpr float nextUnipolar() noexcept` (`:67`); `constexpr void seed(uint32_t) noexcept` (`:73`) | Per-grain birth draws (FR-031, FR-032, FR-070) |
| `semitonesToRatio` | `core/pitch_utils.h:23` (L0) | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Grain playback ratio (FR-024) |
| `ModulationSource` | `core/modulation_source.h:31` (L0) | pure virtuals `getCurrentValue()` (`:37`) and `getSourceRange()` (`:41`) only | Satisfied by `BrownianDrift`; `AtmosphereEngine` is a **sink**, not a source, and does **not** implement it |
| control-chunk cadence | `systems/continuous_body.h:97`, `systems/harmonic_cloud.h:144` | `static constexpr std::size_t kControlChunkSamples = 64` (identical value in both) | Phase 5 copies the **value** 64 for consistency (FR-005); no header dependency |
| Stream contract | `systems/continuous_body.h:1161-1163` | `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, std::size_t numSamples) noexcept` | FR-004 adopts the identical shape so Phase 7 chains them without adapters |

**Read but deliberately NOT used** (reasons recorded so an omission is not read as an oversight):
`GrainPool` (`primitives/grain_pool.h:39`) — C-1; `GrainProcessor` (`processors/grain_processor.h:37`) —
C-1/C-6; `GranularEngine` (`systems/granular_engine.h:30`) — it hard-wires `DelayLine`, 10–500 ms grain
sizes (`:90`) and 1–100 grains/s (`:96`), all outside this phase's ranges; `SlicePool`
(`primitives/slice_pool.h:137`) — C-2; `GranularFilter` / `GranularDistortion` — unrelated effects.

---

## New components

| Class | Layer | Header path | ODR sweep result (`grep -rn "class X\|struct X" dsp/ plugins/ tools/`, this session) |
|---|---|---|---|
| `AtmosphereEngine` | 3 | `dsp/include/krate/dsp/systems/atmosphere_engine.h` (new file — verified absent: `ls` → *No such file or directory*) | **CLEAR** — 0 hits for `AtmosphereEngine`, and 0 hits for `Atmosphere*` of any kind |
| `AtmosphereEngine::AtmosphereGrain` (nested) | 3 (nested) | same header | **CLEAR** — 0 hits at namespace scope. Deliberately **not** named `Grain`: `struct Grain` already exists at namespace scope (`primitives/grain_pool.h:23`); a nested `Grain` would compile but would make every `Grain` in the file ambiguous to a reader |
| `AtmosphereEngine::PrepareConfig` (nested) | 3 (nested) | same header | **CLEAR** — 0 hits for `PrepareConfig` anywhere in the repo |
| `AtmosphereEngine::GrainDriftLanes` (nested) | 3 (nested) | same header | **CLEAR** — 0 hits for `GrainDriftLanes` anywhere. Deliberately **not** named `DriftLanes`: `HarmonicCloud::DriftLanes` already exists (`systems/harmonic_cloud.h:1125`). Both are class-nested so a name clash would not be an ODR violation, but the two banks have different lane semantics (partials vs grains) and must not read as the same type |
| `AtmosphereEngine::DriftLaneRng` (nested) | 3 (nested) | same header | **CLEAR** — 0 hits for `DriftLaneRng` anywhere. Exists for the same reason `HarmonicCloud::LaneRng` (`:1121`) and `EntropyProcessor::LaneRng` (`processors/entropy_processor.h:423`) do: `Xorshift32`'s only constructor is `explicit` (`core/random.h:44`), which makes `std::array<Xorshift32, N>{}` ill-formed. Named distinctly from the two existing `LaneRng`s so a reader is never in doubt which one is in scope |

Names swept and rejected (recorded so the plan does not re-litigate): `LongGrainPool`, `LongGrain`,
`AtmosphereGrain` (namespace scope), `GrainSnapshot`, `SpectralBlur`, `SpectralBlurProcessor`,
`GrainLease`, `AtmosphereParams`, `GrainCloud`, `CloudGrain` — **all 0 hits**, i.e. all available, but
none is needed: the roadmap authorises exactly one new component (line 233) and this spec ships exactly
one **class**. `GrainDriftLanes` and `DriftLaneRng` are private nested aggregates of that class, not
components — the same shape Phase 2 shipped.

**Amended (not new):** `RollingCaptureBuffer` (`primitives/rolling_capture_buffer.h:50`) gains one method
per RA-1. No new type.

---

## Functional Requirements

### A. Component identity, lifecycle, RT safety

- **FR-001** — The component is `class AtmosphereEngine`, declared in
  `dsp/include/krate/dsp/systems/atmosphere_engine.h`, in `namespace Krate::DSP`, at **Layer 3**. Its
  header banner states the layer, the spec slug, and the roadmap lines it implements.
- **FR-002** — Includes point **downward only**: Layer 0 (`grain_envelope.h`, `random.h`, `pitch_utils.h`,
  `math_constants.h`, **`db_utils.h`** — for `detail::isNaN` / `detail::isInf`, FR-008/FR-063), Layer 1
  (`rolling_capture_buffer.h`, `stft.h`, `spectral_buffer.h`, `smoother.h`), Layer 2
  (`grain_scheduler.h`, `spectral_freeze_oscillator.h`). **No Layer 3 or Layer 4 include.** No
  `HarmonicCloud`, no `ContinuousBody` (N-2). Two headers are deliberately **absent**: `stereo_utils.h`
  (N-9, C-7 — nothing in it is used) and `brownian_drift.h` (FR-030 reproduces the recurrence as SoA
  lanes and includes no `BrownianDrift` object, mirroring `harmonic_cloud.h:17-23`). Layer discipline is
  gated automatically by `node tools/lint-layers.js` (SC-013), not by inspection.
- **FR-003** — `void prepare(double sampleRate, const PrepareConfig& config) noexcept` performs **all**
  allocation: capture ring, grain array, envelope table, STFT/OverlapAdd/SpectralBuffer pair and the blur
  output FIFO (only when `config.blurEnabled`, FR-043), freeze oscillators and their capture scratch
  buffers (only when `config.freezeEnabled`), the `blurFftSize`-sample stereo freeze-leg delay (only when
  `config.blurEnabled && config.freezeEnabled`, FR-052), and every smoother.
  Calling `prepare` twice is legal and fully reconfigures. `prepare` is the only non-RT-safe method.
- **FR-004** — `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
  float* outRight, std::size_t numSamples) noexcept` — shape identical to
  `ContinuousBody::processStereoBlock` (`systems/continuous_body.h:1161-1163`). A null pointer in any of
  the four positions writes **nothing** and returns; `numSamples == 0` is a no-op. Input and output may
  not alias (documented precondition; the engine reads all of the input for capture before writing
  output).
- **FR-005** — Control-rate work (**per-grain drift-lane advance**, scheduler density refresh,
  blur-parameter refresh, amplitude-normalisation target) runs on an **absolute 64-sample grid**
  (`kControlChunkSamples = 64`, the value used by `ContinuousBody` at `:97`). A chunk that straddles a
  `processStereoBlock` boundary carries over: the grid is anchored to the engine's total-sample counter,
  not to block starts, so a caller splitting a block into odd partitions gets identical output
  (SC-011).
- **FR-006** — `void reset() noexcept` returns the engine to its exact post-`prepare` state — ring
  cleared, all grains inactive, scheduler and drift lanes re-seeded from the configured seed, smoothers
  snapped to their current targets, STFT/OverlapAdd/freeze cleared, sample counter zeroed. It allocates
  nothing. **`reset()` must call `GrainScheduler::seed(derived)` explicitly**: neither
  `GrainScheduler::reset()` (`grain_scheduler.h:39-42`) nor `GrainScheduler::prepare()` (`:33-36`) touches
  `rng_` — only `seed()` (`:97`) does — so without that call the jitter stream resumes mid-sequence and
  the post-`reset` render does **not** match the original, in contradiction of the Edge Case that requires
  it. (`BrownianDrift::reset()` *does* re-seed, `brownian_drift.h:133-135` → `initState()` → `:243`; the
  drift lanes copy that behaviour.) All derived seeds come from `deriveStreamSeed` (`core/random.h:102-111`),
  whose non-zero substitution is what keeps `setSeed(0)` from collapsing streams together.
- **FR-007** — `void silence() noexcept` ramps the output to zero over `kSilenceRampMs = 10.0f` and then
  deactivates all grains, without clearing the capture ring. It is the clickless "stop" used when a voice
  is stolen.

  **It latches.** Once the ramp completes the engine is muted and stays muted: no grain is active, the
  scheduler is not run, no grain is born, no input is captured, and every output sample is exactly
  `0.0f`. There is **no auto-resume** and no `resume()` method. **`reset()` — or a fresh `prepare()` — is
  the one documented re-entry**, and it returns the engine to its exact post-`prepare` state (FR-006,
  which also clears the ring). `silence()` while already latched is a no-op. Across the latched span
  `getActiveGrainCount()` reads 0 and neither skip counter (FR-072) advances, so a latched engine costs
  nothing beyond the zero-fill. FR-063's internal-non-finite path fires this same `silence()` and
  therefore has this same — and only this — recovery, which is why the recovery is named here rather than
  left implicit.
- **FR-008** — **RT safety.** Every method other than `prepare` is `noexcept`, allocation-free, lock-free,
  exception-free and I/O-free. No `std::isnan` / `std::isinf` / `std::numeric_limits::infinity()`
  anywhere in the header (the macOS leg builds with `-ffast-math`). Finiteness checks call the **existing**
  Layer 0 helpers `Krate::DSP::detail::isNaN` (`core/db_utils.h:54-57`) and `detail::isInf` (`:175-178`) —
  both are exactly the constexpr exponent-field bit test, with the `-ffast-math` rationale documented at
  `:44-52`, and both are already what `OnePoleSmoother::setTarget` (`primitives/smoother.h:170`) and
  `BrownianDrift` (`processors/brownian_drift.h:64-68`) rely on. **No new bit test is written**; a fourth
  reimplementation of the same check is a defect, not a style choice.
- **FR-009** — **Control table (pinned here; Phases 7 and 9 consume it verbatim).** Every setter clamps
  its argument to the stated range; out-of-range and non-finite arguments never propagate.

  | Setter | Range | Default | Smoothing | Applied |
  |---|---|---|---|---|
  | `setGrainSeconds(float)` | 0.05 … 30.0 | 4.0 | none | at grain birth (snapshot) |
  | `setDensity(float)` grains/s | **0.1** … 20.0 | 4.0 | none | `GrainScheduler::setDensity` at control step |
  | `setJitter(float)` | 0 … 1 | 0.5 | none | `GrainScheduler::setJitter` |
  | `setPositionSeconds(float)` | 0.0 … 30.0 | 1.0 | none | birth read-age target |
  | `setPositionSpread(float)` | 0 … 1 | 0.3 | none | at birth, ± spread·position |
  | `setPitchSemitones(float)` | −24 … +24 | 0.0 | none | at birth — **snapshotted** into the grain |
  | `setPitchSpread(float)` | 0 … 1 → ±1200 cents | 0.15 | none | at birth — **snapshotted** into the grain |
  | `setDriftDepth(float)` | 0 … 1 | 0.3 | none | `BrownianDrift::setDepth` |
  | `setDriftSmoothness(float)` | 0 … 1 | 0.7 | none | `BrownianDrift::setSmoothness` |
  | `setDriftRangeSemitones(float)` | 0 … 12 | 2.0 | none | at birth — **snapshotted**; scales that grain's lane value for its whole life |
  | `setPanSpread(float)` | 0 … 1 | 0.7 | none | at birth |
  | `setDecorrelation(float)` | 0 … 1 → 0 … 30 ms | 0.5 | none | at birth |
  | `setBlur(float)` | 0 … 1 | 0.0 | 50 ms (`OnePoleSmoother`) | per blur frame, advanced by `advanceSamples(hopSize)` (`smoother.h:243`) immediately **before** the value is read — see note below |
  | `setFreezeMix(float)` | 0 … 1 | 0.0 | 100 ms (`LinearRamp`) | `process()` per output sample |
  | `setLevel(float)` | 0.0 … 2.0 | 1.0 | 20 ms | `process()` per output sample |
  | `setGrainEnvelope(GrainEnvelopeType)` | enum | `Hann` | none | regenerates table (**not** RT-safe if it reallocates — FR-027 forbids reallocation) |
  | `setSeed(std::uint32_t)` | any | 1 | none | re-seeds all RNGs (FR-070) |

  **Pitch controls are snapshotted at grain birth, never read live (binding).** `setPitchSemitones`,
  `setPitchSpread` and `setDriftRangeSemitones` are captured into `AtmosphereGrain` at birth, together
  with everything derived from them: the grain's static pitch `s` (FR-031), its drift range `d`, the
  ratio envelope `[rₘᵢₙ, rₘₐₓ]` and the truncated lifetime `L′` (FR-025). Changing any of the three
  affects **only grains born afterwards**; a 30 s grain keeps the pitch envelope its lifetime was
  computed for. This is what keeps FR-025 closed-form: no setter, and no host automation, can widen an
  in-flight grain's ratio envelope past the one its `L′` was truncated for, so the liveness invariant
  never needs a runtime clamp. The cost is deliberate and is stated so it is not later read as lag: at
  `grainSeconds = 30` the pitch controls take up to a grain lifetime to fully take effect. The drift
  **lane value** itself stays live (FR-030) — only the range that scales it is frozen. `setDriftDepth`
  and `setDriftSmoothness` are lane-bank settings, not per-grain ones, and are applied live to the whole
  bank.

  **Smoother-cadence rule (binding, not commentary).** `OnePoleSmoother::configure(ms, sampleRate)`
  computes a **per-`process()`-call** coefficient (`primitives/smoother.h:160-164`) and `process()`
  advances exactly one sample (`:197`). A smoother that is *read* once per blur frame must therefore be
  *advanced* by `advanceSamples(hopSize)` (`:243`), not by one `process()` call: at the default
  `blurFftSize = 1024` the hop is 256 samples, so one `process()` per frame stretches a 50 ms time
  constant to ~12.8 s and the blur knob appears frozen. Every smoother in this spec states its cadence in
  the table above or in its own FR; a smoother whose cadence is unstated is an incomplete requirement.

  **`setDensity`'s lower bound is the reused component's, not a taste choice.**
  `GrainScheduler::setDensity` does `density_ = std::max(0.1f, grainsPerSecond)`
  (`grain_scheduler.h:47`), and FR-021 reuses the scheduler verbatim. A range whose bottom half the
  scheduler silently clamps away would be a dead zone, and the Edge Case that exercises it would be
  unsatisfiable. Phase 5 does **not** amend `GrainScheduler` — sub-0.1 grains/s is one grain per >10 s,
  outside anything the roadmap asks for, and amending a shared Layer 2 component for it would need its own
  RA-style cross-consumer impact table (as RA-1 does for `RollingCaptureBuffer`) for no gain.

  `PrepareConfig` fields and defaults: `float captureSeconds = 8.0f` (range 1.0 … 30.0),
  `bool blurEnabled = true`, `bool freezeEnabled = true`, `std::size_t blurFftSize = 1024`,
  `std::size_t freezeFftSize = 2048`, `std::size_t maxBlockSamples = 2048` (range 64 … 8192; sizes the
  blur output FIFO, FR-043). A `processStereoBlock` call longer than `maxBlockSamples` is processed in
  `maxBlockSamples`-sized internal slices, so it is never a precondition violation.

  **`prepare` validates both FFT sizes before anything consumes them.** Each is clamped to its bounds
  (`blurFftSize` to [256, 4096], `freezeFftSize` to [256, 8192]) and then, if it is not a power of two,
  **rounded down** via `std::bit_floor` and re-clamped to the lower bound. This mirrors what
  `SpectralFreezeOscillator::prepare` already does internally (`spectral_freeze_oscillator.h:106-113`) —
  the point of doing it in `prepare` is that the *engine* must not keep the unsnapped request: FR-051
  extracts its capture length from the oscillator's own `getFftSize()` (`:426-429`), never from
  `config.freezeFftSize`, so a request of e.g. 3000 cannot leave the capture length (3000) and the
  analysis length (2048) disagreeing. The snapped values are what `getLatencySamples()` (FR-046) reports.

  A freshly `prepare`d engine is silent (empty ring), with all values above at their defaults.

### B. Capture source (self-granulation)

- **FR-010** — `prepare` sizes the capture ring via
  `RollingCaptureBuffer::prepare(sampleRate, config.captureSeconds)` (`rolling_capture_buffer.h:75`).
  `captureSeconds` is clamped to [1.0, 30.0]. The header documents the power-of-two rounding
  (`:83`, `:210-220`) and the resulting byte cost, per RA-2.
- **FR-011** — The ring **is** the grain snapshot. No per-grain audio storage exists (C-2, N-6).
- **FR-012** — Every input sample is written to the ring via `writeStereo` (`:113`) **before** any grain
  reads for that sample, so a grain may legitimately read audio produced in the same block — this is the
  self-granulation the roadmap asks for ("the organism feeds on itself", line 235).
- **FR-013** — The engine maintains its own monotonic `std::uint64_t` total-samples-written counter
  (`RollingCaptureBuffer::getSamplesWritten()` saturates at capacity, `:119-121`, and cannot serve).
  Grain read positions are stored in this absolute domain, so they never wrap (2⁶⁴ samples ≈ 12 million
  years at 48 kHz).
- **FR-014** — Grains may only be born once
  `getAvailableSamples()` (`:204`) ≥ the birth read age plus `kMinAgeSamples = 64`. Before that the
  scheduler's triggers are discarded (counted, FR-072) and the engine outputs silence.

### C. Grain engine

- **FR-020** — Grain state lives in a private `std::array<AtmosphereGrain, kMaxGrains>` with
  `kMaxGrains = 64`, plus a persistent active-index list maintained by activation/retirement (never
  rebuilt by scanning, unlike `GrainPool::activeGrains()` at `grain_pool.h:107-116`).

  **Slot allocation is round-robin over all `kMaxGrains` slots, not first-free.** A private cursor
  `nextSlot_` is scanned forward (modulo `kMaxGrains`) for the first inactive slot; the grain takes it and
  the cursor is left at `slot + 1`. If a full sweep finds no inactive slot the trigger is discarded per
  FR-023. First-free allocation would concentrate every grain on the low `density × grainSeconds` slots —
  ~16 of 64 at the defaults — so the same drift lanes (FR-030 binds lane `i` to slot `i`) would be reused
  over and over while the upper lanes idle, and successive grains would be serially correlated in pitch
  motion. Round-robin spreads births across all 64 lanes, which is what makes the cloud decorrelate.
  The cursor is part of the deterministic state: `reset()` returns it to 0 (FR-006).
- **FR-021** — Trigger timing comes from one reused `GrainScheduler` (`grain_scheduler.h:29`) in
  `SchedulingMode::Asynchronous`, driven per sample by `process()` (`:73`), with `setDensity`/`setJitter`
  refreshed at control steps. **Verbatim** means verbatim: its `density_ = std::max(0.1f, …)` clamp
  (`:47`) is the reason FR-009's density range starts at 0.1, and its private `Xorshift32 rng_{12345}`
  (`:110`) — the source of jitter, and therefore of grain-birth *timing* — is re-seeded only by `seed()`
  (`:97`), which FR-006 and FR-070 call explicitly.
- **FR-022** — **`kMaxGrains = 64` is a budget-derived ceiling and is *provisional until measured*.**
  SC-004's 1 % budget is 106 667 ns per 512-sample block at 48 kHz
  (`continuous_body_perf_test.cpp:108-116`) = 208 ns per output sample for the whole engine. A
  grain-sample costs two interpolated ring reads, one envelope lookup, one drift-lane read and four
  multiply-adds; 64 concurrent grains must therefore fit inside ~3.25 ns each.

  **That figure is an arithmetic ceiling, not a cost model, and the dominant term is memory, not
  arithmetic.** RA-2's table puts the ring at up to 16.8 MB per voice (30 s @ 48 kHz). With 64 grains ×
  two decorrelated read points (FR-033) × two channels there are up to ~128 independent, non-sequential
  read streams into a multi-megabyte buffer — an L3/DRAM-miss workload in which a *single* miss exceeds
  the whole 3.25 ns allowance. Instruction count says nothing useful about it.

  The plan must therefore **measure** it before `kMaxGrains` is treated as settled: a micro-benchmark
  `AtmosphereEngine_GrainSampleCost` (tagged `[.perf]`, same trial shape as SC-004) reporting ns per
  grain-sample at the **worst case** — `captureSeconds = 30`, `decorrelation = 1.0`, position spread at
  maximum, so the read points are maximally scattered — and at the 8 s default for contrast. If 64 grains
  do not fit, the response is SC-004 lever (5): **reduce `kMaxGrains`** and shrink the documented
  `density × grainSeconds` operating region to match. That is a specified capability trade, flagged in
  the header banner (FR-073) and in the compliance document — never a raised baseline and never a relaxed
  reference.

  The mean concurrent count is `density × grainSeconds`, so the control table's maxima
  (20 grains/s × 30 s = 600) deliberately **exceed** the pool: FR-023 defines what happens, C-8 records
  that this is a deviation from the roadmap's "30 s × density worst case pre-allocated" wording, and the
  header documents that `density × grainSeconds ≤ kMaxGrains` is the intended operating region.

  > **AMENDED 2026-07-28 — the measurement mandate is DISCHARGED and `kMaxGrains` stays 64.**
  >
  > FR-022 made 64 provisional *until measured*. It has been measured, three times, by
  > `AtmosphereEngine_GrainSampleCost` at the worst case (`captureSeconds = 30`, `decorrelation = 1.0`,
  > `positionSpread` max) and at the 8 s default:
  >
  > | pass | 30 s ring (16.8 MB) | 8 s ring (4.19 MB) | ratio |
  > |---|---|---|---|
  > | T017/T019 (worst of three runs) | 9.962 ns | 10.320 ns | — |
  > | T022 verification | **8.06287 ns** | **7.87372 ns** | **1.02402** |
  > | 2026-07-28 amendment | 7.59286 ns | 7.39566 ns | 1.02666 |
  >
  > **The verdict is unchanged: 64 concurrent grains do not fit.** Against the amended arithmetic
  > ceiling (`160 000 / (64 × 512)` = 4.883 ns) the latest pair is 1.555× and 1.515× over; against the
  > withdrawn 1 % ceiling of 3.255 ns the T022 pair was 2.48× and 2.42× over.
  >
  > **And FR-022's stated *reason* is refuted by FR-022's own measurement.** FR-022 argued the dominant
  > term would be memory — "up to ~128 independent, non-sequential read streams into a multi-megabyte
  > buffer … an L3/DRAM-miss workload in which a *single* miss exceeds the whole 3.25 ns allowance". The
  > 30 s ring is **4× the bytes** of the 8 s ring and costs the **same per grain-sample to within
  > run-to-run spread**: ratio **1.024** at T022 and 1.027 at this amendment, with a further probe at
  > `captureSeconds = 1` (a 256 KB/channel ring that fits in L2) landing inside the same spread. The
  > grain path is **instruction-bound**, so there is no cache-shaped saving to find and a smaller pool
  > does not make a grain-sample cheaper.
  >
  > **Lever (5) — reduce `kMaxGrains` — was therefore MEASURED AND REFUSED, not skipped.** At the largest
  > pool satisfying the old arithmetic ceiling (`kMaxGrains = 16`, from `106 667 / (512 × 10.32)` = 20.2)
  > configuration (c) computes to `16 × 512 × 10.32 + ~15 300` (its measured blur term) ≈ **99 900
  > ns/block — still 1.40×** the then-admissible baseline of 71 111. The lever does not close the
  > criterion it exists for, and it costs real capability: `kMaxGrains = 16` makes FR-009's **own default
  > control table** permanently pool-saturated (mean concurrent count exactly 16), putting the shipped
  > default on FR-023's skip path, collapsing FR-073's documented operating region to the default itself,
  > and moving SC-003's D-17 precondition table under it. Four of the five SC-004 configurations —
  > (a), (b), (d), (e) — run at 16 concurrent grains anyway, where the cap never binds, so lever (5)
  > moves their figures by exactly zero.
  >
  > **The remedy FR-022 names is superseded by the lever-6 budget decision.** SC-004's amendment raises
  > the reference to 1.5 % and declares the saturated-64 configuration (c) out-of-region, which is what
  > carries the residual. `kMaxGrains` stays **64**, FR-073's `density × grainSeconds ≤ kMaxGrains`
  > operating rule is **unchanged**, and the miss is reported in the header banner, in the TU's
  > MEASUREMENT RECORD and in `compliance.md` §1.4 — which is what FR-022 asks for when it calls the
  > constant "provisional *and measurement-backed*". FR-022's measurement obligation is closed; nothing
  > about it remains outstanding.
- **FR-023** — **Skip, never steal.** When all 64 slots are active, a scheduler trigger is **discarded**
  and a counter incremented (FR-072). No in-flight grain is ever reset, truncated or reused — stealing a
  30 s grain mid-envelope is a guaranteed click (contrast `GrainPool::acquireGrain`,
  `grain_pool.h:71-91`).
- **FR-024** — **Traversal is in the absolute-sample domain, not the delay domain.** A grain stores
  `std::uint64_t readIndexInt` and `float readFrac`; each output sample it advances by its **current**
  ratio `r` — recomputed at each control step from `semitonesToRatio(sᵢ + laneᵢ · dᵢ)`, where `sᵢ` and
  `dᵢ` are the grain's **snapshotted** static pitch and drift range (FR-009, FR-030, FR-031) and `laneᵢ`
  is its live drift-lane value (`pitch_utils.h:23`, FR-030) — held constant within the chunk, with
  integer carry, then reads the
  ring at age `age = writeCounter − readPosition`. Equivalently `age` moves by `1 − r` per sample — the
  arithmetic correction C-6 records. Integer+fraction is exact for the whole 30 s lifetime at any sample
  rate; a `float` position is not (C-1).
- **FR-025** — **Grain-liveness invariant (the core new engineering).** Let `C` = ring capacity in
  samples, `g = kMinAgeSamples = 64`, `L` = grain lifetime in samples, `a₀` = birth read age.

  **The bound is `C − 2`, not `C − 1`.** FR-080's reader interpolates between `⌊age⌋` and `⌊age⌋ + 1`, so
  an age of `C − 1` requires a sample at age `C`. `RollingCaptureBuffer` addresses with `& mask_`
  (`rolling_capture_buffer.h:117`, `:166`) and `mask_ = capacity_ − 1` (`:90`), so age `C` maps to
  `(writeIndex_ − 1 − C) & mask_ == (writeIndex_ − 1) & mask_` — the **newest** sample. At the invariant's
  own upper limit the grain would blend the oldest sample with the newest: exactly the wraparound
  discontinuity this FR exists to prevent. FR-081's independent clamp to `available − 2` would mask it
  silently rather than fix it, so the two must agree. Every bound below is `C − 2`.

  **The ratio is a bounded envelope, not a constant** (FR-030 restores the roadmap's per-grain drift, so a
  grain's ratio moves over its life). Let `s` be the grain's total static pitch in semitones (FR-031) and
  `d` its `driftRangeSemitones` — **both snapshotted at birth** (FR-009), so no setter can widen the
  envelope below while the grain is in flight. `BrownianDrift::getCurrentValue()` is clamped to [−1, +1]
  (`brownian_drift.h:212`) and the drift lanes copy that clamp, so the ratio is confined for life to
  `r ∈ [rₘᵢₙ, rₘₐₓ] = [semitonesToRatio(s − d), semitonesToRatio(s + d)]`. The age moves by `1 − r(t)` per
  sample, so per sample it moves within `[1 − rₘₐₓ, 1 − rₘᵢₙ]` and over `L` samples
  `a(t) ∈ [a₀ + min(0, 1−rₘₐₓ)·L , a₀ + max(0, 1−rₘᵢₙ)·L]`.

  **`w` is the sum of the two one-sided excursions, not their maximum:**

  ```
  w := (rₘₐₓ − 1)⁺ + (1 − rₘᵢₙ)⁺        where (x)⁺ = max(x, 0)
  ```

  The birth window below is `[ (rₘₐₓ−1)⁺·L + g , C − 2 − (1−rₘᵢₙ)⁺·L ]`, whose width is
  `C − 2 − g − ((rₘₐₓ−1)⁺ + (1−rₘᵢₙ)⁺)·L` — the **sum** is what has to fit. When the envelope straddles
  `r = 1` (`rₘᵢₙ < 1 < rₘₐₓ`) both terms are non-zero and the sum strictly exceeds the maximum: at
  `s = 0`, `d = 2` the figures are `rₘᵢₙ = 0.8909`, `rₘₐₓ = 1.1225`, sum `= 0.2316` against a maximum of
  `0.1225`. A maximum-based `w` under-truncates by a factor of ~2 there, leaves the window empty after
  substitution, and makes the truncation rule non-convergent in exactly the configuration SC-002 clause 3
  sweeps. Whenever the envelope does **not** straddle `r = 1` at most one term is non-zero, so the sum
  reduces to `|1 − r|` when `d = 0` and the drift-free closed form SC-002 clause 2 asserts is unchanged.

  At birth the engine **clamps `a₀`** into `[ (rₘₐₓ−1)⁺·L + g , C − 2 − (1−rₘᵢₙ)⁺·L ]`, and when that
  window is empty it **truncates `L`**, by the formula in the amendment box immediately below. No
  truncation when `w = 0` (`s = 0` and `d = 0`: the age is constant and any lifetime is safe).

  > **AMENDED 2026-07-28 — the implemented truncation is adopted as the binding formula.**
  >
  > **Withdrawn wording, quoted verbatim:**
  >
  > > […] and when that window is empty — which happens exactly when `w·L > C − 2 − g` — it
  > > **truncates `L`** to `L′ = ⌊(C − 2 − g) / w⌋`. Because `w·L′ ≤ C − 2 − g` by construction,
  > > substituting `L′` back makes the window non-empty, so the rule converges in one step for every
  > > envelope, straddling or not.
  >
  > **The binding formula is now the implemented one:**
  >
  > ```
  > headroom = C − 2 − 2g − dR
  > slack    = headroom − 2
  > L′       = (w · L > slack) ? ⌊slack / w⌋ : L
  > ```
  >
  > i.e. `L′ = ⌊(C − 2 − 2g − dR − 2) / w⌋` where the withdrawn form wrote `⌊(C − 2 − g)/w⌋`. The three
  > extra terms are **forced, not chosen**, and each is named:
  >
  > | term | what it is | why the withdrawn form cannot be implemented without it |
  > |---|---|---|
  > | first `− g` | **young-side FR-025 margin** — the `g` the withdrawn form already had | Unchanged: `ageLo = ⌈w↑·L′⌉ + g` keeps the youngest reachable age at or above `kMinAgeSamples`. |
  > | second `− g` | **old-side FR-014 admission margin** | FR-014 requires `needed = ⌈oldestAge⌉ + g` samples of ring before a grain may be born. Subtracting `g` at the *young* end only makes that unsatisfiable: the birth window must be built for it, so step (c) subtracts `guard` at **both** ends and then `needed ≤ (C−2−g) + g = C−2 ≤ C`, which a full ring always reaches. |
  > | `− dR` | **D-1, the FR-033 decorrelation offset** | The right channel reads at `a + dR`, and FR-033's own note says the offset "participates in FR-025's clamp (the larger of the two ages is the one bounded)". With `dR` in the clamp but not in the truncation, `ageHi − ageLo ≥ −dR − 2` and the window inverts. The withdrawn form is self-inconsistent with FR-033. |
  > | `− 2` (the `slack` line) | **D-12, reserved ceiling slack** | `ageLo` and `ageHi` each carry a `⌈·⌉`, each adding < 1, so their sum is `< w·L′ + 2`. Truncating against unreserved headroom leaves `ageHi − ageLo > −2`, i.e. `std::clamp(a₀, lo, hi)` called with `lo > hi` — a **precondition violation**, UB that can still return a plausible `a₀`. |
  >
  > **Every term strictly shrinks the reachable age range, so the guarantee is *strengthened*, never
  > weakened.** The oldest reachable age becomes `C − 2 − g`, strictly inside the `C − 2` bound this FR
  > states, and the youngest stays at or above `g`. Convergence in one step is unaffected:
  > `ageHi − ageLo > (C − 2 − g − dR) − (w·L′ + 2) − g ≥ 0` by construction of `slack`.
  >
  > **What it costs:** `2g + dR + 2` samples of a ring that is 524 288 samples at the default
  > `captureSeconds = 8`, i.e. ≤ 0.03 % of the maximum grain lifetime wherever truncation binds at all.
  > The plan's worked cell (`captureSeconds = 8`, `pitch = 0`, `driftRange = 12`, `w = 1.5`) moves from
  > `L′ = 349 480` to `349 437` — 43 samples, 0.9 ms out of a 7.28 s grain. SC-002 asserts the
  > implemented form as an equality and asserts `ageLo ≤ ageHi` directly.
  >
  > **Scope of this amendment:** it replaces the truncation formula **everywhere this spec writes
  > `⌊(C − 2 − g)/w⌋`**, including SC-002 clause 2's closed-form assertion and SC-009 clause 2's
  > rate-aware expectation. `w` is unchanged (still the **sum** of the one-sided excursions), the
  > `C − 2` bound is unchanged, and the invariant itself — no grain reads overwritten or unwritten
  > audio — is unchanged and unconditional.
  `s`, `d`, `rₘᵢₙ`, `rₘₐₓ` and `L′` are all snapshotted into the grain at birth, so **FR-025 is an
  unconditional closed-form guarantee**: there is no runtime age clamp and no per-sample saturation
  anywhere in the grain read path, and nothing a caller does mid-flight can invalidate a live grain's
  bound.

  Consequences the header must state: a 30 s grain is reachable at 0 semitones with zero drift and any
  ring ≥ its start age; at +12 semitones (`r = 2`) it needs `C ≥ 30 s`; at −12 semitones (`r = 0.5`) it
  needs `C ≥ 15 s`; and a non-zero `driftRangeSemitones` **widens `w`**, so drift makes long grains
  shorter, never less safe. **No grain ever reads a sample that the write head has overwritten, and no
  grain ever reads ahead of the write head** — the envelope is a bound, not an estimate, so the guarantee
  is unconditional rather than probabilistic.
- **FR-026** — A grain retires when its envelope phase reaches 1.0. Retirement returns its slot and
  removes it from the active list. Envelope phase advances by `1/L'` per sample using the **truncated**
  lifetime from FR-025, so the window still closes exactly at zero.
- **FR-027** — The grain window is `GrainEnvelope::generate` (`grain_envelope.h:33`) into a
  `prepare`-allocated table of `kEnvelopeTableSize = 4096` entries, read with the interpolating
  `GrainEnvelope::lookup` (`:165`). `setGrainEnvelope` regenerates **in place** into the existing table —
  it must not resize (contrast `GrainProcessor::prepare`, which `resize`s at
  `grain_processor.h:49`).

  **The endpoints are forced, not assumed.** After every `generate` call the engine writes
  `table[0] = 0.0f` and `table[kEnvelopeTableSize − 1] = 0.0f`. Five of the six shipped types already end
  at exactly 0 — `Hann` (`grain_envelope.h:47-50`), `Sine` (`:79-82`), `Blackman` (`:88-94`),
  `Trapezoid` (`:61-73`) and `Linear` (`:106-118`) — but **`Exponential` does not**: its release branch is
  `output[i] = std::exp(−t · kTimeConstant)` with `t = (i − sustainEnd) / releaseSamples` (`:144-150`), so
  at the last entry `t = (releaseSamples − 1) / releaseSamples < 1` and the value is `exp(−4) ≈ 0.0183`.
  `setGrainEnvelope` exposes the whole enum (`:14-21`), so without the forced endpoints a grain could
  terminate on a step of ~1.8 % of its amplitude while SC-003 requires **0** detections at every lifetime.
  Forcing two of 4096 entries changes no shipped component (the fix lives in the engine, not in
  `grain_envelope.h`) and is measured: SC-003 sweeps `Exponential` explicitly.
- **FR-028** — Overlapping-grain gain uses `1/√n` on the **active** count, with the target refreshed at
  control steps (`setTarget`) and fed through a `OnePoleSmoother` configured at
  `kGainSmoothMs = 50.0f` against the audio sample rate and advanced by **one `process()` call per output
  sample** (the same idea as `granular_engine.h:227-237`, but at a time constant suited to long grains,
  where `n` changes rarely and by ±1 out of ~16). The cadence is stated because it is the FR-009
  smoother-cadence rule: a per-sample `configure` advanced once per control chunk would give a 3.2 s
  effective time constant, and the resulting level lag is not visibly wrong in any single test.

  **The smoothed gain is one multiply on the summed stereo bus**, applied once per output sample after
  every live grain has been accumulated. It is **never** captured per grain: FR-034 has no per-grain
  amplitude term precisely so that the level tracks the *live* population instead of drifting with
  whatever population each grain happened to be born into (a birth-time snapshot leaves a grain born into
  a crowd quiet for its whole 30 s life as the crowd thins — a slow, non-restoring level error). SC-008's
  incoherent-sum argument depends on this placement.
- **FR-029** — Position: birth read age = `positionSeconds · sampleRate`, offset by
  `±positionSpread · positionSeconds · sampleRate` drawn from `Xorshift32::nextFloat()`
  (`random.h:59`), then clamped by FR-025.
- **FR-030** — **Per-grain pitch drift** (C-5; roadmap line 243), as a private nested
  `GrainDriftLanes` — an SoA bank of `kMaxGrains` independent Ornstein–Uhlenbeck walks, laid out exactly
  like `HarmonicCloud::DriftLanes` (`systems/harmonic_cloud.h:1125-1148`): parallel
  `std::array<float, kMaxGrains>` for the walk, the output-smoother current and its target, a
  `std::array<DriftLaneRng, kMaxGrains>`, and **one shared** `samplesUntilControl` counter for the whole
  bank. Never `kMaxGrains` `BrownianDrift` objects.

  *Coefficients and recurrence* are `BrownianDrift`'s, reproduced not re-invented: `kTauMin`/`kTauMax`
  from `setSmoothness` (`brownian_drift.h:97-99`), `kInternalStd = 0.5` (`:101`), `a = exp(−dt/τ)` and
  `g = kInternalStd·√(1−a²)` (`:228-240`), and a control step of three sequenced `nextFloat()` draws
  summed Irwin-Hall, `x = mean + a·(x−mean) + g·z`, clamped to ±`kWalkLimit`, denormal-flushed, then
  `setTarget(clamp(depth·x, −1, +1))` (`:252-262`). Lane control interval is
  `kDriftControlInterval = 32`, mirroring `brownian_drift.h:105` and `harmonic_cloud.h:148`.

  *Cadence* — this is the part SC-011 binds. The bank is advanced **at each 64-sample control chunk on
  FR-005's absolute grid**, by exactly that chunk's length; a chunk straddling a `processStereoBlock`
  boundary carries over in the shared `samplesUntilControl` counter, precisely as
  `BrownianDrift::processBlock` does across calls (`brownian_drift.h:194-206`). It is **never** advanced
  "once per block" by `numSamples`: `processBlock` advances the output smoother for the whole span before
  returning, so a value read after a 4096-sample advance is 4096 samples further along the walk than the
  same value read under 64-sample partitions. Since that value scales a grain's pitch, the two renders
  would diverge in pitch — orders of magnitude above SC-011's 1e-5 — and SC-011 would be unsatisfiable by
  construction.

  *Per-grain semantics.* Lane `i` belongs to grain slot `i` for the slot's whole life and is seeded from
  `deriveStreamSeed(seed, kDriftSaltBase + i)` (`core/random.h:102-111`) at `prepare`, `reset` and
  `setSeed`. A grain's instantaneous pitch is `sᵢ + laneᵢ · dᵢ`, where `sᵢ` and `dᵢ` are the grain's
  **snapshotted** static pitch and drift range (FR-009, FR-031) and `laneᵢ` is the live lane value. It is
  re-evaluated at each control step and held constant **within** a chunk; a grain born mid-chunk uses the
  lane value as of the **most recent completed control step**, so birth timing inside a chunk cannot
  change the value it sees. The ratio is recomputed from the pitch at each control step
  (`semitonesToRatio`, `pitch_utils.h:23`) and applied per sample by FR-024's integer+fraction advance.
  Because lane output is clamped to [−1, +1] (as `getCurrentValue()` is) and `dᵢ` is frozen, the ratio
  never leaves FR-025's `[rₘᵢₙ, rₘₐₓ]` envelope, which is what keeps the liveness invariant closed-form.

  *Birth semantics (binding).* **At every grain birth on slot `i` the lane's walk state is zeroed — the
  walk value, the output smoother's current value and its target all set to 0 — and its `Xorshift32`
  stream is *not* re-seeded.** Consequences, all intended: every grain starts at exactly its snapshotted
  static pitch `sᵢ` and drifts away over its life (the plainest reading of the roadmap's "pitch drift per
  grain", line 243), there is **no birth-time pitch step** of up to `±dᵢ` from whatever value a free-running
  lane happened to hold, and successive grains on the same slot are not handed the same walk sequence
  because the stream position is preserved. Combined with FR-020's round-robin allocation over all 64
  slots, successive grains are decorrelated both across lanes and along each lane. Zeroing is
  deliberately **not** a `BrownianDrift::reset()`-equivalent: that call re-seeds
  (`brownian_drift.h:133-135` → `initState()` → `:243`), which would make every grain on a slot replay
  one identical walk. Only `prepare`, `reset` and `setSeed` re-seed a lane (FR-006, FR-070).

  *Verification.* SC-002 includes a lane-equivalence gate: for a chosen slot, a reference `BrownianDrift`
  (constructed **in the test**, which includes `brownian_drift.h`; the engine header does not) seeded with
  the same `deriveStreamSeed(seed, kDriftSaltBase + i)` and advanced with the same chunk sequence must
  track the lane. This is the same gate Phase 2 wrote for its own lanes, and it is what stops a
  hand-rolled xorshift silently desynchronising from the shared Layer 0 RNG.
- **FR-031** — Per-grain **static** detune (unchanged by C-5's restoration of per-grain drift — the two
  are orthogonal): `±pitchSpread · 1200` cents from a fresh `Xorshift32` draw at birth, added to
  `pitchSemitones` to give the grain's static pitch `s`. Both inputs are read **once, at birth**, and `s`
  is stored in the grain (FR-009's snapshot rule), as is the drift range `d`. `s` is clamped to ±36
  semitones, and the snapshotted drift envelope `s ± d` (FR-030) is clamped to the same ±36, so `r` stays
  in [0.125, 8] at every instant of the grain's life and FR-025's `rₘᵢₙ`/`rₘₐₓ` are always well-defined
  and fixed for that life.
- **FR-032** — Per-grain pan: `pan = panSpread · Xorshift32::nextFloat()` ∈ [−spread, +spread], converted
  to gains by the equal-power law `gL = cos(θ)`, `gR = sin(θ)`, `θ = (pan+1)·π/4` — the same law
  `GrainProcessor` uses (`grain_processor.h:101-103`), computed once at birth.
- **FR-033** — Per-grain decorrelation (C-7): the right channel is read at an age offset of
  `decorrelation · 30 ms · u`, `u ∈ [0,1)` drawn at birth, i.e. L and R read **different points of the
  ring**. The offset participates in FR-025's clamp (the larger of the two ages is the one bounded).
- **FR-034** — Grain output is `env(phase) · (gL·ringL(ageL), gR·ringR(ageR))`, accumulated into the
  block sum. **There is no per-grain amplitude term**: it would be identically 1, so it does not exist as
  a field or as a multiply. The `1/√n` population compensation is a single multiply on the summed stereo
  bus (FR-028), and there is no per-grain random gain — that would be an amplitude-spread control, which
  the roadmap's Phase 5 list does not contain and which would need its own setter, range, default and
  test (N-8). No per-grain filtering, no per-grain STFT (C-4).

### D. Spectral blur

- **FR-040** — Blur is **one** stereo stage on the summed grain output (C-4): two `STFT`, two
  `OverlapAdd`, two `SpectralBuffer` (`stft.h:35`, `:181`, `spectral_buffer.h:45`), all allocated in
  `prepare` when `config.blurEnabled`.
- **FR-041** — Geometry: `fftSize = config.blurFftSize` (default 1024; clamped to [256, 4096] and then,
  if not already a power of two, **rounded down** with `std::bit_floor` and re-clamped to 256 — the
  direction is stated because FR-009's validation and this FR must not disagree),
  `hopSize = fftSize / 4` (**75 % overlap**), `WindowType::Hann`, and
  `applySynthesisWindow = true` — mandatory at this overlap per `stft.h:201-204`, and forbidden at 50 %.
  When blur is enabled the grain sum is routed through the stage **unconditionally**, so the path is
  transparent at `blur = 0` (Hann² COLA reconstruction, normalised at `stft.h:226-239`) and the
  latency never changes with the knob (RA-3).
- **FR-042** — Per frame, for every bin `k ∈ [0, numBins)`: magnitude is preserved
  (`SpectralBuffer::getMagnitude`, `:84`) and the synthesis phase is
  `phase(k) + blur · π · Xorshift32::nextFloat()` written back with `setPhase` (`:106`) — a
  ±`blur·π` uniform perturbation, so `blur = 0` is the identity and `blur = 1` is full decoherence. The
  bin-0 (DC) and Nyquist bins are left untouched (their phase is not free in a real spectrum).

  **The draw is per bin *per channel*, not per bin per frame.** Left is processed first and right second,
  both consuming from the one blur stream (FR-044), so the two channels receive **independent**
  perturbations. Blur therefore produces fog **and** progressive stereo decorrelation as it rises. That is
  intended, specified behaviour — measured by SC-005's decorrelation clause — and it is *not* a second
  width control: it is per-source decorrelation inside the atmosphere layer, the same axis FR-033 already
  occupies per grain, whereas global width remains Phase 7's (FR-060, N-9). The L-then-R consumption
  order is part of the determinism contract (FR-071, SC-010): swapping it changes the render.
- **FR-043** — Blur frames are pumped by `pushSamples` / `canAnalyze` / `analyze` / `synthesize` /
  `pullSamples` (`stft.h:104`, `:120`, `:130`, `:266`, `:305`), draining every available frame each block
  so `STFT::samplesAvailable_` cannot grow without bound (its input buffer is `fftSize * 8` with no
  overflow guard, `stft.h:78`, `:104-113`).

  **The loop order is load-bearing and is specified here, not left to the implementer.**
  `OverlapAdd::synthesize` always accumulates the IFFT frame at `outputBuffer_[0 .. fftSize)` with **no
  offset** (`stft.h:277-285`); the per-frame hop offset comes *only* from `pullSamples` shifting the
  buffer left by the number pulled (`:309-323`). Two `synthesize()` calls without an intervening pull of
  `hopSize` therefore stack both frames at the same offset and destroy COLA. The required shape, per
  channel:

  ```
  stft.pushSamples(grainSum, numSamples);
  while (stft.canAnalyze()) {
      stft.analyze(spectralBuffer);
      // FR-042 phase perturbation
      overlapAdd.synthesize(spectralBuffer);
      overlapAdd.pullSamples(fifoWriteCursor, hopSize);   // MUST be inside the loop
  }
  // then pop exactly numSamples from the FIFO into the block output
  ```

  A literal reading of "drain all frames, then pull `numSamples` once" fails SC-006's −60 dBFS
  transparency check, and the failure presents as a windowing bug rather than a loop-order bug — which is
  why it is pinned. The output FIFO is `prepare`-allocated with capacity ≥ `fftSize + maxBlockSize`
  (`PrepareConfig` therefore also carries `std::size_t maxBlockSamples = 2048`, clamped to
  [64, 8192]); `OverlapAdd`'s own buffer is only `2 · fftSize` (`stft.h:243`) and cannot serve as the FIFO.
- **FR-044** — The blur RNG is a **separate** `Xorshift32` instance from the grain RNG, so changing blur
  does not alter grain birth draws (a determinism requirement — SC-010). It is a **single** stream shared
  by both channels, consumed L-then-R per bin (FR-042) — not two per-channel streams — so the channel
  perturbations are independent without adding a second seeded stream to `setSeed`'s salt set (FR-070).
- **FR-045** — When `config.blurEnabled == false`, no STFT/OverlapAdd/SpectralBuffer is allocated, no
  blur output FIFO and **no freeze-leg delay** is allocated (there is no latency to match — FR-052),
  `setBlur` is inert, and latency is 0.
- **FR-046** — `[[nodiscard]] std::size_t getLatencySamples() const noexcept` returns
  `config.blurEnabled ? blurFftSize : 0` (RA-3). It is constant between `prepare` calls. It is the
  latency of the **whole layer — both crossfade legs**, because FR-052 delay-matches the freeze leg to
  the same figure; there is never a second, different latency for a caller to discover. The freeze
  oscillator's own `getLatencySamples()` (`spectral_freeze_oscillator.h:421-423`) is deliberately **not**
  added: the drone is synthesised, not a delayed copy of the input, so it has no dry counterpart to align
  against.

### E. Pure-freeze mode

- **FR-050** — When `config.freezeEnabled`, `prepare` constructs **two** `SpectralFreezeOscillator`
  (`spectral_freeze_oscillator.h:80`) — one per channel, because `freeze()` and `processBlock()` are mono
  (`:217`, `:317`) — each `prepare`d at the **snapped** `config.freezeFftSize` (default 2048, the
  component's own default at `:105`). `SpectralFreezeOscillator::prepare` does more than clamp to
  [256, 8192] (`:642-643` for the bounds): a non-power-of-two size is silently `std::bit_floor`ed
  (`:106-113`). FR-009 therefore snaps the value **before** anyone stores it, so the engine and the
  oscillator can never hold different numbers.
- **FR-051** — `void captureFreeze() noexcept` extracts the most recent samples per channel from the
  capture ring into two `prepare`-allocated scratch buffers and calls `freeze()` on each oscillator. The
  extraction length is **the oscillator's own `getFftSize()`** (`spectral_freeze_oscillator.h:426-429`),
  never the requested `config.freezeFftSize`: `freeze()` truncates its input at `fftSize_`
  (`:222-223`), so a capture length larger than the analysis length would silently discard the newest
  audio, and a smaller one would zero-pad. It allocates nothing. Calling it with insufficient ring history
  (FR-014) is a no-op, not a partial capture.
- **FR-052** — `setFreezeMix(m)` crossfades the engine output between the grain layer (`m = 0`) and the
  freeze drone (`m = 1`) over a `LinearRamp` at `kFreezeMixRampMs = 100.0f`. At `m = 0` **and** the ramp
  settled, neither oscillator's `processBlock` is called (hard bypass — the roadmap's "only active
  modules burn CPU" rule, line 207).

  **The freeze leg bypasses blur spectrally but is delay-matched to it.** The drone is never routed
  through the STFT ↔ OverlapAdd stage — FR-050's pure spectral hold stays pure, and SC-005/SC-007 keep
  measuring the two paths for what they are — but when `config.blurEnabled` the freeze leg is routed
  through a `prepare`-allocated stereo delay of exactly `blurFftSize` samples **before** the crossfade.
  Both legs then share one layer latency, FR-046 reports a single honest number, and a `freezeMix` sweep
  crossfades two time-aligned signals. Without the delay the legs would be offset by 21.3 ms at the
  default geometry and the crossfade would smear in time. When blur is disabled the delay is neither
  allocated nor traversed (FR-003, FR-045).

  **There is no symmetric bypass at `m = 1`.** At a settled `freezeMix = 1.0` the grain layer keeps
  running in full — scheduler, grain ageing, ring reads, `1/√n` compensation and the blur stage — even
  though its contribution is multiplied by zero. Two binding reasons: releasing the freeze is seamless
  because the grain population never lapsed (a bypass would restart from an empty pool and the level
  would swell back over `density × grainSeconds` seconds under FR-028's smoother), and SC-004
  configuration (d) therefore measures the honest grain+freeze worst case — one of the four measured
  numbers RA-4 hands to Phase 7, which would otherwise understate a frozen voice. The `m = 0` bypass
  above is deliberately **not** symmetric with this: the freeze oscillators hold nothing that has to stay
  warm, whereas the grain population does.
- **FR-053** — `void releaseFreeze() noexcept` calls `unfreeze()` (`:295`) on both oscillators, which
  crossfades them to silence over one hop.
- **FR-054** — When `config.freezeEnabled == false`, no oscillator is allocated, `setFreezeMix` /
  `captureFreeze` / `releaseFreeze` are inert, and the freeze path costs nothing.

### F. Output stage

- **FR-060** — **There is no width control** (N-9, C-7). `AtmosphereEngine` has no `setWidth`, does not
  call `stereoCrossBlend`, and does not include `stereo_utils.h`. Its stereo image is produced solely by
  FR-032's per-grain equal-power pan and FR-033's per-grain L/R read-age decorrelation — which is what
  the roadmap's "spatial diffusion" line asks for. Global width is Phase 7's (`StereoField`, roadmap
  lines 287–288). This is stated as a requirement rather than an omission so a later phase does not add
  one here "for symmetry" and end up with two controls on one axis.

  **Blur's per-channel phase draws (FR-042) do widen the image as `blur` rises, and that is not a width
  control.** It is per-source decorrelation inside the layer — the same axis FR-033 already occupies per
  grain — and it is inseparable from the fog: there is no parameter whose job is stereo width, nothing
  calls `stereoCrossBlend`, and a caller cannot set the image without also setting the amount of spectral
  smearing. Phase 7's global width still operates on the layer's output, unchanged.
- **FR-061** — Level: a final smoothed gain in [0, 2], `process()` per output sample. It is the one
  output-stage control the engine keeps, because the `1/√n` grain sum (FR-028) is produced inside the
  engine and the caller cannot trim it without a second pass over the buffer. It is a gain trim, not a
  dry/wet mix (N-3) and not a width control (N-9).
- **FR-062** — **Output is the wet texture only** (N-3). The input stream appears in the output only via
  grains and freeze, never as a dry pass-through.
- **FR-063** — **Non-finite hygiene.** A non-finite input sample — tested with
  `detail::isNaN` / `detail::isInf` (`core/db_utils.h:54-57`, `:175-178`; FR-008 forbids a new bit test)
  — is substituted with 0.0 for both capture and output, and the ring is **preserved** (no silence) — the
  policy Phase 4 settled on (`seraphis-phase4-continuous-body/spec.md`, Clarification Q3). If the
  engine's **own** state goes non-finite (checked at control steps on the block sum with the same two
  helpers), `silence()` fires and grains are retired with the FR-007 ramp. Because `silence()` **latches**
  (FR-007), that path leaves the engine muted and not scheduling until `reset()` — the same, and only,
  documented recovery. The engine never resumes on its own after an internal non-finite event; SC-014's
  sub-case asserts both the latch and the `reset()` re-entry. Both halves are measured by
  SC-014 — in particular the ring-preservation half, which is the deliberate difference from a
  silence-on-NaN policy and is exactly the behaviour that regresses unnoticed.
- **FR-064** — A denormal guard (flush below 1e-20) is applied to the smoothed gains, matching
  `BrownianDrift`'s `kDenormalFloor` practice (`brownian_drift.h:228`).

### G. Determinism & introspection

- **FR-070** — `setSeed(s)` deterministically re-seeds **all** RNG state: the grain-birth `Xorshift32`,
  the blur `Xorshift32`, `GrainScheduler::seed` (`grain_scheduler.h:97`) and every one of the
  `kMaxGrains` drift lanes (FR-030). Every derived value comes from `deriveStreamSeed(s, salt)`
  (`core/random.h:102-111`) with a distinct salt, so no two streams are correlated **and** none can be
  handed 0 — `Xorshift32::seed()` silently substitutes its own default for 0 (`:72-74`), so two streams
  hashing to 0 would collapse onto one. Default seed is 1.

  `setSeed` mid-render re-seeds every lane's **stream** but does **not** zero any lane's walk state (only
  a grain birth does that, FR-030), and it cannot touch a live grain's snapshotted `s`, `d`, `rₘᵢₙ`,
  `rₘₐₓ` or `L′` (FR-009, FR-025). A live grain's future walk therefore changes while its ratio stays
  inside the envelope its lifetime was truncated for, so FR-025's invariant holds across a `setSeed` at
  any point in a render — which is what lets SC-001 exercise `setSeed` mid-render without a caveat.
- **FR-071** — Two engines with identical `prepare`, identical seed and identical setter history produce
  identical output for identical input, on one machine (SC-010 uses `render_fingerprint.h`, never a
  bit-exact digest — roadmap line 492).
- **FR-072** — Introspection for tests, all `[[nodiscard]] … const noexcept`. The list is derived from
  what the success criteria must actually assert — a criterion that cannot be observed through this list
  is not measurable, and several below exist for exactly one criterion:

  | Accessor | Exists for |
  |---|---|
  | `getActiveGrainCount()` | SC-004(c) saturation, SC-009 concurrency |
  | `getSkippedTriggerCountPoolFull()` | SC-001/SC-003 preconditions — proves FR-023's path was reached |
  | `getSkippedTriggerCountRingCold()` | FR-014's path, kept **separate** so the two skip causes are never conflated (a single counter cannot distinguish "pool exhausted" from "ring still filling", and both criteria above need the former specifically) |
  | `getTotalGrainsBorn()` | SC-010, SC-002 sweep bookkeeping |
  | `getMinObservedGrainAgeSamples()` / `getMaxObservedGrainAgeSamples()` | SC-002's invariant bounds — directly assertable rather than inferred |
  | `getLastBornGrainBirthAgeSamples()` | SC-002's shadow model needs `a₀`, which is drawn from internal RNG at birth (FR-029) and is otherwise unobservable |
  | `getLastBornGrainRatioAtBirth()` | SC-002's shadow model needs `r`, drawn from FR-031's spread and FR-030's lane |
  | `getLastBornGrainLifetimeSamples()` | SC-002's closed-form truncation assertion and SC-009's lifetime measurement; `L′` cannot be resolved to the sample from block-granular active-count transitions |
  | `getGrainRngState()` | SC-010's FR-044 clause — the only way to prove the blur stage did not consume from the grain stream |
  | `getLatencySamples()` | SC-006 delay compensation, RA-3 |
  | `getCaptureCapacitySamples()` | SC-009's rate-aware truncation expectation (RA-2: `C` in *seconds* is rate-dependent) |

  The three `getLastBornGrain*` accessors are updated at each birth and are `const noexcept` reads of
  plain members — no state is added to the audio path beyond three scalars.
- **FR-073** — The header documents, in a banner block, the memory formula (RA-2's table) and the
  `density × grainSeconds ≤ kMaxGrains` operating rule (FR-022).

### H. RA-1 — `RollingCaptureBuffer` amendment

- **FR-080** — Add to `RollingCaptureBuffer` (`primitives/rolling_capture_buffer.h:50`):
  `void readStereoLinear(float ageSamples, float& outLeft, float& outRight) const noexcept;`
  Age 0.0 addresses the most recent written sample, index `writeIndex_ - 1`; increasing age moves back in
  time; the value is linearly interpolated between `⌊age⌋` and `⌊age⌋ + 1`.

  **This is the anchor of `extractSlice`'s *last* element, not its first.** `extractSlice` anchors from
  the **end** of the slice: `startOffset = offsetSamples + lengthSamples`,
  `startIndex = (writeIndex_ − startOffset + capacity_) & mask_` (`:159-162`), and `out[i]` is read at
  `startIndex + i` (`:165-169`), so `out[i]` has age `offsetSamples + lengthSamples − 1 − i`. Only
  `out[lengthSamples − 1]` has age exactly `offsetSamples`. The correspondence the tests must assert is
  therefore `readStereoLinear(offsetSamples + lengthSamples − 1 − i) == out[i]`, which collapses to
  "same offset" only at `lengthSamples == 1` (SC-012 states both forms).
- **FR-081** — Guard **first**, then clamp:
  `if (capacity_ == 0 || getAvailableSamples() < 2) { outLeft = outRight = 0.0f; return; }` — then clamp
  `ageSamples` to `[0.0f, static_cast<float>(getAvailableSamples() − 2)]`. A NaN/Inf argument likewise
  yields `(0.0f, 0.0f)` and touches nothing.

  Both halves of the guard are load-bearing, and neither is belt-and-braces:
  (a) `getAvailableSamples()` returns `size_t` (`:204-206`) and starts at 0 (`reset()` zeroes
  `samplesWritten_`, `:100`), so a bare `available − 2` **wraps to ~2⁶⁴** on a fresh `prepare`/`reset` —
  the clamp becomes a no-op in exactly the case it exists for, and the interval `[0, −2]` is empty and
  inverted. (b) On an unprepared buffer `capacity_ = mask_ = 0` and `bufferL_`/`bufferR_` are empty
  vectors (`:86-90`, `:223-230`), so an unguarded `bufferL_[idx & mask_]` is an out-of-bounds read on a
  **shared Layer 1 primitive** — an ASan/valgrind finding in `dsp_primitives_tests`, not a Seraphis-local
  bug.
- **FR-082** — In the steady-state path it uses the existing `& mask_` wraparound (`:117`, `:166`) — no
  new indexing scheme and no branch on wrap. This does **not** forbid FR-081's prepared/empty guard, which
  is a precondition check taken once before any indexing, not a per-wrap branch.
- **FR-083** — It is `const`, `noexcept`, allocation-free and O(1).
- **FR-084** — **Strictly additive**: no existing member, member function, default or observable
  behaviour of `RollingCaptureBuffer` changes. The method is inert unless called.

---

## Success Criteria

Every criterion names its metric, its threshold, and the Catch2 case that measures it. New test files:
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp` (SC-014 — a separate TU because it carries
`-fno-fast-math -fno-finite-math-only` source properties, which must not be applied to the others), all
registered in `dsp/tests/CMakeLists.txt` alongside the Phase 4 files (`:344-346`) — sources are listed
explicitly, not globbed, so an unregistered file silently drops. The perf TU carries both
`AtmosphereEngine_CpuBudget` (SC-004) and `AtmosphereEngine_GrainSampleCost` (FR-022's micro-benchmark),
both tagged `[.perf]`.

- **SC-001 — Zero allocation after `prepare` (roadmap line 246).**
  *Metric:* allocation count inside an `AllocationScope` (`tests/test_helpers/allocation_detector.h:75`).
  *Threshold:* **exactly 0** allocations across 10 s of `processStereoBlock` at the worst case —
  `captureSeconds = 30`, `grainSeconds = 30`, `density = 20` (i.e. sustained pool exhaustion and
  sustained trigger skipping), blur on, freeze on, plus `captureFreeze()`, `setSeed`, `silence()` and
  every setter exercised mid-render.
  *Precondition assertion (not optional):* `REQUIRE(engine.getSkippedTriggerCountPoolFull() > 0)` — with
  a conflated counter the test could pass having only exercised FR-014's cold-ring path and never
  FR-023's, which is the path this worst case exists to stress.
  *Test:* `AtmosphereEngine_NoAllocationAfterPrepare`.

- **SC-002 — Grain liveness: no grain ever reads overwritten or unwritten audio (FR-025).**
  *Metric:* `getMinObservedGrainAgeSamples()` and `getMaxObservedGrainAgeSamples()` (FR-072) sampled
  every block, plus a shadow model built in the test from `getLastBornGrainBirthAgeSamples()`,
  `getLastBornGrainRatioAtBirth()` and `getLastBornGrainLifetimeSamples()` (FR-072). Those three
  accessors exist because `a₀` and `r` are drawn from internal RNG at birth (FR-029, FR-031, FR-030) and
  are otherwise unobservable, and because `L′` cannot be resolved to the sample from block-granular
  active-count transitions.
  *Threshold — three clauses:*
  1. **Invariant, always:** `min ≥ kMinAgeSamples (64)` and `max ≤ capacity − 2` (FR-025's bound is
     `C − 2`, because FR-080's reader needs `⌊age⌋ + 1` in range) across the sweep
     `grainSeconds ∈ {0.05, 1, 5, 15, 30}` × `pitchSemitones ∈ {−24, −12, 0, +12, +24}` ×
     `captureSeconds ∈ {1, 8, 30}` × `driftDepth ∈ {0.0, 1.0}`.
  2. **Closed form, drift-free sub-sweep:** with `driftDepth = 0` **and** `pitchSpread = 0` **and**
     `positionSpread = 0`, `a₀` and `r` follow deterministically from the setters, the shadow model
     `a(t) = a₀ + (1−r)·t` is exact, and `getLastBornGrainLifetimeSamples()` is asserted **equal** to
     `⌊(C − 2 − g)/|1 − r|⌋` in every combination where truncation binds (and equal to the requested
     lifetime where it does not).
  3. **Envelope, drift-on sub-sweep:** with `driftDepth = 1`, `driftRangeSemitones ∈ {2, 12}`, the shadow
     model becomes the FR-025 **bound** `a(t) ∈ [a₀ + min(0,1−rₘₐₓ)·t, a₀ + max(0,1−rₘᵢₙ)·t]`, the
     observed min/max must lie inside it, and `getLastBornGrainLifetimeSamples()` is asserted equal to
     `⌊(C − 2 − g)/w⌋` with **`w = (rₘₐₓ − 1)⁺ + (1 − rₘᵢₙ)⁺`** (FR-025's sum form). The sub-sweep
     **must** include a *straddling* envelope — `pitchSemitones = 0` with `driftRangeSemitones = 2`,
     where `rₘᵢₙ = 0.8909 < 1 < 1.1225 = rₘₐₓ` and the sum (0.2316) is nearly double the maximum
     (0.1225) — because that is the only case in which the two candidate definitions of `w` differ.
     Sweeping only non-straddling envelopes would pass on a maximum-based implementation, which
     under-truncates and leaves the birth window empty.
  4. **Snapshot clause (FR-009/FR-030 — the pitch controls are frozen at birth):** in a dedicated
     sub-case a long grain is born (`grainSeconds = 30`, `captureSeconds = 30`), and then
     `setPitchSemitones(+24)` and `setDriftRangeSemitones(12)` are called **while it is in flight**. That
     grain's `getLastBornGrainLifetimeSamples()`-recorded `L′` is unchanged, its observed age bounds stay
     inside the envelope computed from the values in force **at its birth**, and the widened settings
     appear only in the **next** grain born (asserted via `getLastBornGrainRatioAtBirth()`). Clauses 1–3
     sweep static configurations only, so without this clause a live-reading implementation — which can
     widen an in-flight grain's envelope past the one its lifetime was truncated for and break the
     invariant — passes every other clause.
  *Also in this case — the drift-lane equivalence gate (FR-030):* a reference `BrownianDrift`
  (constructed in the test, which includes `processors/brownian_drift.h`) seeded with
  `deriveStreamSeed(seed, kDriftSaltBase + slot)` and advanced with the same control-chunk sequence must
  track the engine's lane for that slot to within 1e-6. Without it a hand-rolled xorshift can silently
  desynchronise from the shared Layer 0 RNG — the failure Phase 2 wrote the same gate to prevent.
  *Window for the gate (stated because the birth semantics changed it):* the comparison is measured over
  a span containing **no grain birth on the chosen slot**, asserted via `getTotalGrainsBorn()` and
  FR-020's round-robin cursor position. A birth zeroes the lane's walk state without re-seeding its
  stream (FR-030), whereas `BrownianDrift::reset()` re-seeds (`brownian_drift.h:133-135`); comparing
  across a birth would report specified behaviour as a failure. Density and `grainSeconds` are chosen so
  the round-robin cursor does not return to that slot inside the window.
  *Test:* `AtmosphereEngine_GrainLiveness`.

- **SC-003 — No clicks at grain boundaries at any lifetime (roadmap lines 246–247).**
  *Input (pinned — the metric is relative, so an unpinned input makes the criterion unreproducible):* a
  fixed harmonic stack, fundamental 220 Hz with partials at 2×…9× at `1/n` amplitude (all sine, zero
  phase), band-limited below 2 kHz, scaled to peak 0.5. The **same generator SC-005 uses.**
  *Metric:* `Krate::DSP::TestUtils::ClickDetector::detect()`
  (`tests/test_helpers/artifact_detection.h:99-160`) with the config stated verbatim, as
  `shimmer_delay_test.cpp:1224-1231` does:
  `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256,
  .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`.
  *Threshold:* **0 detections** at every `grainSeconds ∈ {0.05, 0.2, 1, 5, 30}` × every
  `GrainEnvelopeType` including **`Exponential`** (FR-027 forces the table endpoints to 0 precisely
  because `Exponential`'s release ends at ≈0.0183, `grain_envelope.h:144-150`), with density set so the
  pool saturates and with `silence()` invoked mid-render, over a 60 s render.
  *Precondition assertion:* per-cell, see the amendment box below.

  > **AMENDED 2026-07-28 (D-17) — the pool-full precondition is scoped to the cells where it is
  > reachable.**
  >
  > **Withdrawn wording, quoted verbatim:**
  >
  > > *Precondition assertion:* `REQUIRE(engine.getSkippedTriggerCountPoolFull() > 0)` — proves FR-023's
  > > skip path was reached rather than FR-014's cold-ring path.
  >
  > **What replaces it.** The clause applies **only to cells whose configured
  > `density × grainSeconds` can saturate `kMaxGrains`** — 12 of the 30 grid cells. In the other 18 the
  > test asserts `getSkippedTriggerCountPoolFull() == 0` instead, on both the engine and the reference
  > render.
  >
  > **Why the unconditional form is not merely unmet but unsatisfiable.** Mean concurrent grains is
  > `density × grainSeconds`; `kMaxDensity` is 20/s (FR-009) and `kMaxGrains` is 64. At
  > `grainSeconds ∈ {0.05, 0.2, 1}` the maximum population reachable **at any density the control table
  > allows** is 1, 4 and 20 respectively — the pool cannot saturate, at any setting, so no
  > implementation could make the assertion pass. Asserting `> 0` there would gate the criterion on
  > arithmetic rather than on the code.
  >
  > **The inverted form is not a weakening.** `== 0` is a *stronger* statement about those 18 cells than
  > silence would be: it proves the engine did not reach FR-023's skip path spuriously, i.e. that the
  > 0-detection result was produced by a pool that was never saturated. The zero-click threshold itself
  > is untouched, and every one of the 30 cells still asserts 0 detections on both channels of both the
  > engine and the reference render.
  >
  > **SC-001 keeps the unconditional form**, at a configuration where saturation is reachable
  > (`captureSeconds 30`, `grainSeconds 30`, `density 20` → 600 requested against 64 slots), so FR-023's
  > skip path is still gated unconditionally somewhere in the suite — it is just gated where it exists.
  *Latch clause (FR-007), which fixes when `silence()` is invoked:* `silence()` is called at **40 s**,
  i.e. after the pool-saturation precondition above has been observed. From the end of its 10 ms ramp
  every output sample must be exactly `0.0f` and `getActiveGrainCount()` must read 0 — the engine does
  not resume by itself. `reset()` is then called at **50 s** and the final 10 s must be non-silent again
  (window RMS > −60 dBFS once the ring has refilled, FR-014), which exercises the one documented
  re-entry. The click detector runs over the whole 60 s, so both transitions — into the latch and out of
  `reset()` — are inside the 0-detection threshold.
  *Secondary bound — stated as a ratio, not an absolute:* `max |Δy|` of the engine output must not exceed
  **1.5 ×** `max |Δy|` of a reference render of the same input, same seed, with pool saturation and
  `silence()` **not** exercised. An absolute `max |Δy| ≤ 0.05` would silently constrain input bandwidth
  and level rather than the engine (a 5 kHz sine at amplitude 0.5 already has per-sample deltas of 0.33 at
  48 kHz), i.e. it would be a property of the test signal, not of the code under test.
  *Note on the detector's statistics (why the input and config are pinned):* `ClickDetector` flags any
  `|Δy|` above `mean + 5·stddev` of `|Δy|` **within each 512-sample frame** (`:187-193`, `:209-218`). On
  the near-Gaussian output of a 16–64-grain wash the half-normal threshold sits at ≈3.81σ, giving
  P(exceed) ≈ 1.4e-4 — hundreds of expected detections over a 60 s render **with no click present**. The
  pinned harmonic input is deliberately far from Gaussian so the statistic is usable; if the measured
  false-positive rate on the reference render is non-zero, the plan raises `detectionThreshold` to the
  smallest value that gives 0 detections on the reference render **and records that value and its
  measured false-positive floor in the header** — it does not relax the 0-detection requirement on the
  engine render.
  *Test:* `AtmosphereEngine_NoGrainBoundaryClicks`.

- **SC-004 — CPU ≤ 1.5 % of one core per voice at default density (amended; roadmap line 248 said 1 %).**

  > **AMENDED 2026-07-28 — user budget decision, SC-004 lever (6), option 1.**
  >
  > **Withdrawn wording, quoted verbatim so the change is auditable:**
  >
  > > **SC-004 — CPU ≤ 1 % of one core per voice at default density (roadmap line 248).**
  > > *Metric:* nanoseconds per 512-sample block at 48 kHz — the reproducible basis established by
  > > `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:69-101` and reused by
  > > `continuous_body_perf_test.cpp:108-137`. Reference =
  > > `kBlockBudgetNs · 0.01 = 106 667 ns`.
  > > […] Four configurations, each with its own baseline: […] **Every one of the four is
  > > gated against the same 1 % reference** — no configuration gets a relaxed reference.
  >
  > **What replaces it.** Phase 5's per-voice allowance is **1.5 % of one core**, i.e. a reference of
  > **160 000 ns per 512-sample block at 48 kHz**, for configurations **(a), (b), (d) and (e)**.
  > Configuration **(c)** — the pool saturated at 64 concurrent grains with `blurFftSize = 256` — is
  > **out-of-region**: still measured, still regression-tracked against its own checked-in baseline
  > (`REQUIRE(measured ≤ baseline · 1.5)`), but carrying **no** headroom clause and **not** gated against
  > the reference. Phase 7 re-derives its polyphony tally from the measured figures.
  >
  > **The derivation is the five measured worst-case figures and nothing else** (worst of eight
  > consecutive best-of-25 runs, machine and trial shape in the TU's BASELINE PROVENANCE block;
  > transcribed verbatim in `compliance.md` §1.1):
  >
  > | configuration | worst ns/block | % of one core |
  > |---|---|---|
  > | (a) defaults, blur off, freeze off | 86 305 | 0.809 % |
  > | (b) defaults, blur on | 111 815 | 1.048 % |
  > | (c) saturated 64, blur FFT 256 | 343 805 | 3.223 % |
  > | (d) freezeMix 1.0 + grain layer | 153 651 | 1.440 % |
  > | (e) (b) + envelope churn per block | 111 008 | 1.041 % |
  >
  > 1.5 % is the smallest round allowance that covers every in-region figure — (d), at 1.440 %, is the
  > binding one — after the levers had already been spent (T019: 1.60×–2.77× measured improvements) and
  > after levers (3), (3b) and (5) were measured and refused. It is not headroom for future cost.
  >
  > **What the amendment costs, stated rather than hidden.** "Baselines are measurements, not
  > allowances" still holds for **(a)** (baseline 91 000 = ⌈86 305 × 1.05⌉ rounded up, gate 136 500).
  > It does **not** hold for **(b)**, **(d)** and **(e)**: their measurements exceed `reference / 1.5 =
  > 106 666.67`, so no baseline equal to a measurement can satisfy the headroom clause. Their baselines
  > are the **cap** (106 666), which makes their runtime gate the reference itself (159 999) and their
  > regression headroom ≈ 43 % instead of ≈ 5 %. They still enforce 1.5 % absolutely. If a later pass
  > brings them under 106 666, the baselines revert to being their measurements and the tight regression
  > bound returns.
  >
  > **What is *not* amended.** The `static_assert(baseline · kRegressionFactor ≤ reference)` /
  > `static_assert(baseline ≥ reference / 50)` / `REQUIRE(measured ≤ baseline · 1.5)` structure is
  > retained unchanged for (a), (b), (d), (e); (c) keeps the floor clause and loses only the headroom
  > clause. `kMaxGrains` stays **64** (FR-022). FR-073's operating rule is unchanged. Lever (5) stays
  > refused. No baseline may ever be raised by code, and lever (6) having been taken once does not
  > license taking it again.

  *Metric:* nanoseconds per 512-sample block at 48 kHz — the reproducible basis established by
  `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:69-101` and reused by
  `continuous_body_perf_test.cpp:108-137`. Reference =
  `kBlockBudgetNs · 0.015 = 160 000 ns` for configurations (a), (b), (d), (e); configuration (c) has
  no reference (out-of-region, above).
  *Threshold:* a checked-in baseline per configuration with **two distinct** compile-time clauses —
  a **headroom** clause `static_assert(baseline * kRegressionFactor <= reference)` (the Phase 4 form,
  `continuous_body_perf_test.cpp:136-137`) and a **floor** clause
  `static_assert(baseline >= reference / 50.0)` — plus the runtime `REQUIRE(measured ≤ baseline · 1.5)`.
  The floor is not decorative: `baseline·1.5 ≤ reference` and `baseline ≤ reference/1.5` are the *same*
  inequality, so asserting both adds no coverage, whereas a baseline accidentally recorded from a
  no-op/misconfigured run would satisfy the headroom clause trivially and then make the runtime `REQUIRE`
  fail spuriously on slower CI hardware. The floor catches that at compile time.
  Four configurations, each with its own baseline: (a) defaults, blur off, freeze off; (b) defaults,
  blur on; (c) pool saturated (64 concurrent grains), blur on, `blurFftSize = 256` (the most expensive
  blur geometry — 8 frames per 512-block); (d) freeze mix 1.0 **with the grain layer still running**
  (FR-052 has no symmetric bypass), i.e. the honest grain+freeze worst case rather than a freeze-only
  figure — this is the number Phase 7's tally inherits for a frozen voice. A fifth, (e), was added by
  the plan: (b) plus an alternating `setGrainEnvelope()` per block. **Every in-region configuration is
  gated against the same 160 000 ns reference** — (a), (b), (d) and (e) — so the budget cannot be met by
  choosing a flattering default: the frozen case (d) faces exactly the ceiling the 16-grain default (a)
  does. Configuration (a) is the roadmap's "default density": 4 grains/s × 4 s grains = 16 concurrent
  (FR-009). Configuration (c) is out-of-region per the 2026-07-28 amendment above and is gated only
  against its own baseline × 1.5.
  *Baselines are measurements, not allowances:* each is the worst (largest) of eight consecutive
  best-of-25 runs on the machine named in the TU's BASELINE PROVENANCE block, rounded up by at most
  5 % for run-to-run spread and **capped** at `reference / 1.5` where that binds (the amendment above
  names (b), (d) and (e) as the capped ones and states what the cap costs), and all five measured
  ns/block figures are copied verbatim into this phase's compliance document. RA-4 explains why:
  Phase 7's 25 % full-poly tally needs real numbers from this phase, not a ceiling nobody approached.
  *Test:* `AtmosphereEngine_CpuBudget`, tagged `[.perf]`.
  *If over budget, reduce cost — never raise a baseline and never relax the reference.* Documented levers,
  in order: (1) verify FR-052's freeze hard-bypass actually engages; (2) verify FR-005's control-step
  decimation is firing (a bug that refreshes per sample pays the scheduler/drift-lane cost 64× over);
  (3) drop `blurFftSize` to 512 (halves frame count and per-frame cost, at coarser frequency resolution —
  a specified trade, flag it in the header); (4) hoist the equal-power pan `cos`/`sin` — they are
  birth-time only, so a regression that moved them per-sample is the single largest lever;
  (5) **reduce `kMaxGrains`** below 64, and shrink FR-073's documented `density × grainSeconds` operating
  region to match. FR-022 declares 64 provisional precisely so this lever exists: it is a *specified
  capability trade*, flagged in the header banner and the compliance document, and it is the honest
  response if `AtmosphereEngine_GrainSampleCost` (FR-022) shows the ring's cache behaviour, not the
  instruction count, is what binds. (6) only then escalate.

- **SC-005 — Blur increases spectral smearing monotonically (roadmap line 247).**
  *Input (pinned; shared with SC-003):* the 220 Hz harmonic stack described in SC-003, peak 0.5.
  *Metric:* `Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness(signal, n, sampleRate)`
  (`tests/test_helpers/signal_metrics.h:30-32`, `:60`, `:326`) — **fully qualified**, because a
  same-named 2-argument overload exists at
  `dsp/include/krate/dsp/primitives/spectral_utils.h:335` and the Phase 3 spec already flagged that pair
  as a collision hazard; an unqualified call can bind the wrong one. Secondary metric:
  `…::SignalMetrics::calculateCrestFactorDb` (`:222`) on an impulsive input, which must **fall** as blur
  rises (temporal smearing).

  ***Measurement method (binding — the naïve call cannot see the signal).***
  `calculateSpectralFlatness` picks `fftSize` as the largest power of two ≤ `n`, capped at 4096
  (`:336-339`), and then fills its window from `signal[0 .. fftSize)` **only** (`:350-352`): it analyses
  the first ≤4096 samples of the render and discards the rest. Under this spec's own defaults those
  samples are guaranteed to be **exactly zero** — FR-009 defaults `positionSeconds = 1.0` and FR-014
  forbids any grain birth until `getAvailableSamples() ≥ birth read age + 64`, i.e. 48 000 samples at
  48 kHz, and RA-3 adds a further 1024 samples of blur latency. On an all-zero window the helper returns
  `0.0f` (`arithMean < 1e-10` → early return, `:376-378`), so
  `flatness(0.0) = … = flatness(1.0) = 0.0`: "non-decreasing at every step" holds trivially and
  `flatness(1.0) ≥ 1.25 · flatness(0.0)` reduces to `0 ≥ 0`. **The criterion would pass on a silent
  engine** — the failure mode SC-010's negative half explicitly guards against. The test therefore:
  1. renders long enough to reach steady state, with
     `settleSamples ≥ positionSeconds · sampleRate + 2 · blurFftSize`;
  2. passes a pointer **offset into the settled region** — `render.data() + settleSamples` — with a
     length of **exactly 8192**, so the helper selects `fftSize = 4096` (deliberately longer than the
     1024 blur FFT, so inter-frame phase decoherence widens spectral lines into skirts and is visible);
  3. averages the flatness over **four disjoint 8192-sample windows** taken from the settled region, to
     reduce variance from the stochastic grain population, and compares the averages;
  4. asserts a **non-silence precondition before any threshold**:
     `REQUIRE(rmsDb(window) > -40.0f)` on every window analysed **and** `REQUIRE(flatness(0.0) > 0.0f)`,
     so the criterion cannot be satisfied by zeros.
  *Threshold:* over `blur ∈ {0.0, 0.25, 0.5, 0.75, 1.0}`, the averaged flatness is **non-decreasing** at
  every step (allowing a 2 % measurement epsilon) and `flatness(1.0) ≥ 1.25 · flatness(0.0)`; crest
  factor at `blur = 1.0` is at least 3 dB below `blur = 0.0`. The 1.25× floor is a **minimum** — the plan
  must replace it with the measured value less a stated margin, and may only move it **up**.
  *Stereo-decorrelation clause (FR-042 — blur draws independently per channel):* over the same settled
  windows, the normalised inter-channel correlation coefficient `ρ(L, R)` must be **non-increasing**
  across the same blur sweep (2 % epsilon) and `ρ(1.0)` must be at least **0.2** below `ρ(0.0)`. Like the
  1.25× above this is a **floor**: the plan replaces it with the measured value less a stated margin and
  may only move it **up**. The clause exists because progressive L/R decorrelation is a specified,
  audible consequence of FR-042's per-channel draws — an implementation that applies one draw to both
  channels is a defect that no other criterion in this spec would see.
  *Test:* `AtmosphereEngine_BlurMonotonicity`.

- **SC-006 — Blur is transparent at 0 (FR-041).**
  *Metric:* per-sample difference between `blurEnabled = true, blur = 0` (delay-compensated by
  `getLatencySamples()`) and `blurEnabled = false`, same seed.
  *Threshold:* RMS difference ≤ −60 dBFS relative to the signal RMS, after discarding the first
  `2 · fftSize` samples of OverlapAdd warm-up. This is the COLA-reconstruction check; a wrong
  `applySynthesisWindow` or a wrong hop fails it loudly.
  *Test:* `AtmosphereEngine_BlurTransparentAtZero`.

- **SC-007 — Freeze mode holds level (roadmap "frozen moments", line 230).**
  *Configuration:* `blurEnabled = true`, so FR-052's delay-matched freeze leg is in the path and the
  crossfade check below actually exercises it — an uncompensated 1024-sample offset between the two legs
  presents as a step at the crossfade and is caught by the 0-detection clause, which is the only place in
  this spec that would see it.
  *Metric (statistic defined exactly — "mean peak" is not a defined statistic):* the render at
  `freezeMix = 1.0` after `captureFreeze()` is cut into successive non-overlapping 1 s windows; the
  statistic for window `k` is `peak(k) = max |y[n]|` over the samples of that window.
  *Threshold:* windows **2 … 60** each within **±1.0 dB** of `peak(2)`, and `peak(2) ≥ −60 dBFS`
  (non-silence). The reference is the **second** window, not the first: the first second necessarily
  contains FR-052's `LinearRamp` freeze-mix crossfade at `kFreezeMixRampMs = 100.0f` **and** the freeze
  oscillator's own overlap-add pre-fill (`spectral_freeze_oscillator.h:261-287`), so a first-window
  reference is measured on a partially-ramped signal and the whole ±1.0 dB budget is spent on a transient
  the criterion is not about. Separately, crossfading `freezeMix` 0 → 1 → 0 produces **0**
  `ClickDetector` detections, using SC-003's pinned config.
  *Test:* `AtmosphereEngine_FreezeStability`.

- **SC-008 — Output is bounded under adversarial input.**
  *Configuration (fully pinned):* full-scale white noise input, `captureSeconds = 30`, max density, max
  grain length, `blur = 1.0`, freeze crossfading, `driftDepth = 1.0`, and **`level = 1.0`** — the level
  is pinned because it multiplies the threshold directly and the FR-009 maximum of 2.0 would double the
  analytic bound without any defect being present. (There is no `width` to pin — FR-060/N-9.)
  *Metric:* peak absolute output over a 10-minute render.
  *Threshold:* `< 4.0`, and **every sample finite** via `detail::isNaN`/`detail::isInf`
  (`core/db_utils.h:54-57`, `:175-178`), never `std::isnan`.
  *Justification (the analytic and the statistical bound are different numbers, and the criterion uses
  the statistical one):* `1/√n` normalisation of `n` **coherent** grains bounds the sum at `√n · level`
  = 8 at `level = 1` and 16 at `level = 2` — so 4.0 is **not** implied by the normalisation law, and the
  earlier claim that it was is wrong. The real argument is that grains are born at independent ring
  positions with independent pitches and independent decorrelation offsets (FR-029/030/031/033), so they
  sum **incoherently**: with `1/√n` normalisation the sum has approximately unit variance regardless of
  `n`, and its peak over `N` samples grows as ≈ `σ·√(2 ln N)` — about 5.1σ over a 10-minute render at
  48 kHz. 4.0 is therefore a genuine statistical bound with margin against a runaway, while `√64 · 1` = 8
  is the coherent worst case that no realistic configuration reaches. If a measured run approaches 4.0
  the response is to investigate coherence (all grains reading the same position at `r = 1`), not to
  raise the threshold. The argument also depends on FR-034 having **no** per-grain amplitude term: with
  `1/√n` applied once on the summed bus (FR-028) every grain contributes with unit weight and the sum's
  variance is ≈1 regardless of `n`. A per-grain birth-time snapshot of `1/√n` would let the effective
  weight drift with the population and would invalidate this threshold — the other reason FR-034 pins the
  placement.
  This is the Membrum infinite-ring harness pattern applied to a layer that has no feedback path but does
  have a self-capture loop.
  *Test:* `AtmosphereEngine_BoundedUnderStress`.

- **SC-009 — Sample-rate independence.**
  *Metric:* grain lifetime in **seconds**, mean concurrent grain count, and output RMS, measured at
  44 100 / 48 000 / 96 000 Hz with identical settings and seed.
  *Lifetime metric source:* `getLastBornGrainLifetimeSamples()` (FR-072) divided by that rate's sample
  rate — not inferred from active-count transitions, which cannot resolve a floor-division to the sample.
  *Threshold — two clauses, because the ring's length in seconds is rate-dependent:*
  1. **Non-truncating sweep (precondition, stated in the criterion):** only configurations satisfying
     `w·L ≤ C − 2 − g` (FR-025's no-truncation condition) are swept here. Measured grain lifetime within
     **0.5 %** of the requested seconds at every rate; mean concurrent count within **5 %**; output RMS
     within **1.0 dB**.
  2. **Truncating clause (separate, rate-aware):** for configurations where truncation binds (e.g.
     `grainSeconds = 30`, `pitchSemitones = ±12`, which SC-002 sweeps), measured `L′` within **0.5 %** of
     `⌊(C − 2 − g)/w⌋ / sampleRate` computed from **that rate's own** `getCaptureCapacitySamples()`.
     A single rate-invariant expectation is unachievable here and would be a false failure:
     `RollingCaptureBuffer::prepare` rounds capacity **up to the next power of two** (`:83`, `:210-220`),
     so `captureSeconds = 8` yields 11.89 s of ring at 44.1 kHz but 10.92 s at 48 and 96 kHz — an 8.8 %
     spread in `C/sampleRate` (RA-2), which propagates straight into `L′`. Comparing across rates
     without recomputing from each rate's actual capacity would report an 8.8 % lifetime difference as a
     defect.
  *Allocation clause (metric and threshold, not a wish):* wrap a second `prepare` at the new rate in an
  `AllocationScope` (`tests/test_helpers/allocation_detector.h:75`); the assertions are in the amendment
  box below. The engine must then be silent-but-usable (FR-014's cold ring), asserted by a non-silent
  render after the ring refills.

  > **AMENDED 2026-07-28 (D-18) — the allocation-count *equality* is replaced by a two-sided bracket
  > plus geometry equality.**
  >
  > **Withdrawn wording, quoted verbatim:**
  >
  > > *Allocation clause (metric and threshold, not a wish):* wrap a second `prepare` at the new rate in
  > > an `AllocationScope` (`tests/test_helpers/allocation_detector.h:75`) and require its allocation
  > > count to **equal** the count observed when a fresh engine is prepared directly at that rate with
  > > the same `PrepareConfig`. […] "Must not allocate more than required" is not measurable and is
  > > replaced by this equality.
  >
  > **Why the literal `==` is wrong, not merely inconvenient.** It fails on *correct* code. Every
  > re-prepare path reuses capacity, and `std::vector::resize` to an unchanged size **allocates zero
  > times**. A correct engine therefore allocates *strictly fewer* times on a re-prepare than a fresh
  > engine does on its first prepare (measured: **6 against 69**). The equality would be satisfied only
  > by an engine that threw its buffers away and re-acquired them — precisely the behaviour the clause
  > exists to forbid. It is a proxy that inverts under test.
  >
  > **What is asserted instead — strictly more than the equality covered:**
  >
  > 1. `0 < secondPrepareCount ≤ freshPrepareCount` — bracketed on **both** sides. The upper bound is
  >    what the equality was reaching for (reallocation instead of reuse). The lower bound is new and
  >    catches the opposite failure the equality could not see: 48 → 96 kHz **doubles** the capture ring,
  >    so a re-prepare that allocates *nothing* has silently kept the 48 kHz ring.
  > 2. `renderAllocationCount == 0` over a full post-re-prepare render, measured with SC-001's
  >    instrument — proof that nothing was left undersized.
  > 3. **Geometry equality against a fresh engine:** `getCaptureCapacitySamples()` and
  >    `getLatencySamples()` must both be **equal**. This is the property the count-equality was a proxy
  >    for, and unlike a count it is a property of the re-prepared *engine* rather than of how often its
  >    allocator happened to be called.
  >
  > Plus `firstBlockExactlyZero` (FR-014's cold ring), `bornAfterReprepare > 0` and `lateRms > 1e-4`
  > (usable once refilled). Clauses 1 and 2 of the criterion are untouched.
  *Test:* `AtmosphereEngine_SampleRateIndependence`.

- **SC-010 — Seeded determinism (roadmap line 492: no bit-exact float goldens).**
  *Metric:* `fingerprintRender` / `compareFingerprints`
  (`tests/test_helpers/render_fingerprint.h:64`, `:101`) over a 20 s render.
  *Threshold:* two engines with the same seed compare **equal within the helper's tolerances**
  (`kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`, `:49`, `:52`); two engines with **different**
  seeds compare **unequal** (the negative half — without it the test passes on a silent engine).
  *The FR-044 clause — asserted on grain-birth **parameters**, not on the birth count.* Render twice with
  identical seed and setter history, once at `blur = 0` and once at `blur = 1`, both with
  `blurEnabled = true`, and require `getGrainRngState()` (FR-072) to be **identical** after both renders,
  plus `getTotalGrainsBorn()` equal. Comparing only `getTotalGrainsBorn()` cannot detect the defect
  FR-044 guards against: birth *timing* comes from `GrainScheduler`'s own private `Xorshift32 rng_{12345}`
  (`grain_scheduler.h:110`, jitter draw at `:82`), not from the engine's grain-birth RNG, so if the blur
  stage were sharing the grain-birth stream used for position/pitch/pan draws (FR-029/031/032) the number
  of births would be unchanged — only their parameters would shift — and the count comparison would pass
  on a genuinely broken implementation.
  *Test:* `AtmosphereEngine_SeedDeterminism`.

- **SC-011 — Block-partition invariance (FR-005).**
  *Metric:* per-sample difference between one 4096-sample call and the same render split into
  `{1, 7, 64, 65, 511, 512, 1000}`-sample calls, same seed.
  *Threshold:* **bit-identical is not required and not asserted**; RMS difference ≤ −100 dBFS and max
  per-sample difference ≤ 1e-5. (An implementation that anchors control steps to block starts instead of
  the absolute grid fails this by orders of magnitude — and so does one that advances the drift bank
  "once per block" by `numSamples`, which is why FR-030 forbids it.)
  *Required coverage inside the case:* the parameter set must guarantee that **at least one grain is born
  inside a partial control chunk** (the `{1, 7, 65, 511, 1000}` partitions all produce them; the case
  asserts it via `getTotalGrainsBorn()` transitions at non-multiples of 64), so FR-030's carry-over path
  is actually exercised rather than assumed.
  *Test:* `AtmosphereEngine_BlockPartitionInvariance`.

- **SC-012 — RA-1 changes nothing for existing consumers.**
  *Metric:* the pre-existing suites, run unedited.
  *Threshold:* `dsp_primitives_tests` (owns
  `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp:20`) and `dsp_effects_tests` (owns
  `PatternFreezeMode`, `effects/pattern_freeze_mode.h:40`) both green with **no test file edited**.
  *Plus a new positive test with the two anchoring conventions pinned, not assumed identical:*
  1. **Length 1 (the only case where "same offset" is true):** `readStereoLinear(A)` equals the single
     sample returned by `extractSlice(&l, &r, /*lengthSamples=*/1, /*offsetSamples=*/A)` exactly.
  2. **Length > 1:** for a slice of length `L` at offset `O`,
     `extractSlice(outL, outR, L, O)[i] == readStereoLinear(O + L − 1 − i)`, asserted at `i = 0` and
     `i = L − 1`. `extractSlice` anchors from the **end** of the slice
     (`startOffset = offsetSamples + lengthSamples`, `rolling_capture_buffer.h:161-162`), so the first
     extracted sample sits at `writeIndex_ − O − L`, not at `writeIndex_ − O − 1`; a test written from
     the naive wording with any `L > 1` compares different samples and fails a **correct**
     implementation.
  3. **Fractional:** at half-integer ages, the midpoint within 1e-6.
  4. **Degenerate (FR-081's guard):** immediately after `prepare` — and again after exactly one
     `writeStereo` — `readStereoLinear` at ages 0 and 1 returns `(0.0f, 0.0f)` both times. This is the
     `getAvailableSamples() < 2` case where a bare `available − 2` underflows `size_t`; without this
     clause the guard ships unspecified and untested.
  *Test:* `RollingCaptureBuffer_ReadStereoLinear` in
  `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp` (extended, not replaced).

- **SC-013 — Portability and lint gates.**
  *Metric:* `node tools/check-portability.js`, `node tools/lint-float-bit-goldens.js`,
  `node tools/lint-arch-guarded-includes.js`, `node tools/lint-simd-aligned-loadstore.js`,
  **`node tools/lint-layers.js`** and **`node tools/lint-odr.js`** (both present in `tools/`), and a
  zero-warning build of all five DSP layer test targets. The last two are added because they are the only
  *automated* checks for FR-002's layer discipline and for the ODR sweep — leaving them out made two
  cross-cutting roadmap constraints (lines 488–489) depend on someone remembering.
  *Threshold:* all clean; **zero** compiler warnings on MSVC and on the WSL g++ syntax check. No
  `std::isnan` / `std::isinf` / `numeric_limits::infinity()` in the new header — enforced by a **scripted
  grep gate** run alongside the lints (a one-line `rg` over
  `dsp/include/krate/dsp/systems/atmosphere_engine.h` that fails non-zero on a hit), not by a review
  step. A manual check is not a gate.
  *Test:* build/CI gates, not a Catch2 case.

- **SC-014 — Non-finite hygiene is exercised, not assumed (FR-063).**
  *Metric:* a render into which NaN and ±Inf input samples are injected. The non-finite values are built
  **from bit patterns via a `volatile` sink** — the repo's `-ffast-math`-safe construction — because
  `std::numeric_limits<float>::quiet_NaN()` / `infinity()` fold to finite garbage on the macOS leg. The
  TU carries `-fno-fast-math -fno-finite-math-only` source properties in `dsp/tests/CMakeLists.txt`, as
  every NaN-injecting TU in this repo does.
  *Threshold — three clauses, one per behaviour FR-063 specifies:*
  (a) **every** output sample finite, by `detail::isNaN`/`detail::isInf` (`core/db_utils.h:54-57`,
  `:175-178`);
  (b) **the ring is preserved** — the deliberate difference from a silence-on-NaN policy: after the
  injection, feed silence and confirm that grains still reproduce the *pre-injection* audio (correlation
  ≥ 0.99 against the same render without injection, over a window whose birth read age predates the
  injection), and that `getCaptureCapacitySamples()` is unchanged;
  (c) **0** `ClickDetector` detections across the injection window, using SC-003's pinned config.
  A separate sub-case forces the engine's *own* state non-finite (via an injected input sustained long
  enough to enter the ring and be read by a live grain) and asserts the FR-063 second half: `silence()`
  fires, grains retire under the FR-007 ramp, and the output returns to exact zero — and then **stays**
  exactly zero with `getActiveGrainCount() == 0` for the remainder of the render (the FR-007 latch: no
  auto-resume), until `reset()` is called, after which the engine renders non-silent audio again once the
  ring has refilled (FR-014). The latch and its single re-entry are asserted here because an
  implementation that silently resumes after an internal non-finite event would look identical under
  every other criterion.
  *Why it is needed:* SC-008 asserts finiteness only under **full-scale white noise**, which is finite
  input — it can never reach the substitution path or the ring-preservation clause.
  *Test:* `AtmosphereEngine_NonFiniteHygiene`.

---

## Edge Cases

**RT-safety boundaries**
- `processStereoBlock` with any null pointer → writes nothing, returns (FR-004). With `numSamples == 0` →
  no-op, and the control grid does not advance.
- Pool exhausted for the entire render (density 20 × 30 s grains) → triggers skipped, counter climbs, no
  allocation, no click, output level held by the `1/√n` smoother (SC-001, SC-003).
- `setGrainEnvelope` called on the audio thread → regenerates in place into the existing 4096-entry
  table; no `resize` (FR-027). The regeneration is O(4096) transcendentals, which is *slow but bounded and
  allocation-free*; the header states it should be called at most once per block.
- `silence()` during a 30 s grain → 10 ms ramp then retire; no discontinuity (SC-003). The engine then
  **latches** (FR-007): exact zero output, no scheduling, no capture, counters frozen, until `reset()`
  (or `prepare()`). A second `silence()` while latched is a no-op.
- `reset()` after `silence()` → the one documented re-entry; the engine returns to its post-`prepare`
  state, is silent until the ring refills (FR-014), and then renders again. There is no other way out of
  the latch, including after FR-063's internal-non-finite trip.
- `captureFreeze()` before the ring has `freezeFftSize` samples → no-op (FR-051), not a partial capture.

**Parameter extremes**
- `grainSeconds = 0.05` at `density = 20` → 1 concurrent grain; the engine is nearly a short-grain
  granulator and must not misbehave at the boundary between "shorter than a block" and the 64-sample
  control grid.
- `grainSeconds = 30`, `pitchSemitones = +24` (`r = 4`), zero drift → `w = 3` (the sum form with one
  non-zero term: `(4−1)⁺ + (1−4)⁺ = 3`) and FR-025 truncates to
  `⌊(C−2−g)/3⌋`; with the 8 s default ring (`C = 524 288`) that is ~3.6 s of grain. The engine must
  **truncate silently and correctly**, not clip the read.
- `pitchSemitones = −24` (`r = 0.25`) → age grows at 0.75 samples per sample; the long-grain case that
  hits the *overwrite* side of the invariant rather than the *future-read* side.
- `positionSeconds` > `captureSeconds` → clamped by FR-025's window, never an out-of-range read.
- `density = 0.1` (the minimum — see FR-009/FR-021) → one grain every **10 s**; the scheduler's interonset
  is `48000 / 0.1 = 480 000` samples at 48 kHz, well inside `float` (`grain_scheduler.h:100-103`), and the
  test must confirm no premature trigger. (A request below 0.1 is raised to 0.1 by
  `GrainScheduler::setDensity`'s own `std::max(0.1f, …)` at `:47` — the engine clamps to the same bound so
  the control table and the component agree rather than the component silently overriding the table.)
- `driftDepth = 1.0` with `driftRangeSemitones = 12` at `grainSeconds = 30`, `pitchSemitones = 0` → the
  envelope straddles `r = 1`: `rₘᵢₙ = 2^(−1) = 0.5`, `rₘₐₓ = 2^(+1) = 2`, so FR-025's **sum** form gives
  `w = (2 − 1) + (1 − 0.5) = 1.5` (a maximum-based `w` would read 1.0, under-truncate by 50 % and leave
  the birth window empty). With the 8 s default ring (`C = 524 288`) the lifetime truncates to
  `⌊(524 288 − 2 − 64) / 1.5⌋ = 349 481` samples ≈ **7.28 s** at 48 kHz. Drift shortening long grains is
  specified behaviour, not a defect; the test asserts the closed form, not the requested 30 s.
- A **mildly** straddling envelope — `pitchSemitones = 0`, `driftRangeSemitones = 2` → `rₘᵢₙ = 0.8909`,
  `rₘₐₓ = 1.1225`, `w = 0.2316` against a maximum-based 0.1225 — is the case that distinguishes the two
  candidate definitions of `w`, and SC-002 clause 3 sweeps it for exactly that reason.
- `setPitchSemitones` / `setPitchSpread` / `setDriftRangeSemitones` changed while a 30 s grain is in
  flight → that grain is unaffected: its `s`, `d`, `rₘᵢₙ`, `rₘₐₓ` and `L′` were snapshotted at birth
  (FR-009, FR-025, FR-030), so FR-025's invariant cannot be broken by a setter or by host automation.
  Only grains born afterwards see the new values (SC-002 clause 4). The control therefore feels laggy at
  long grain lengths, by design.
- `freezeMix = 1.0`, ramp settled → the grain layer keeps running in full (FR-052 has no symmetric
  bypass), so returning to `freezeMix = 0` is seamless and SC-004(d) measures grain+freeze rather than
  freeze alone.
- `blur = 1.0` with `blurFftSize = 256` (minimum) → 64-sample hop, 8 frames per 512-block; the most
  expensive blur configuration, and the one SC-004(c) must not be quietly measured without.
- `level = 0` → exact silence, no denormals (FR-064). (There is no `width` control to zero — FR-060/N-9.)

**Sample-rate changes**
- `prepare(96000, cfg)` after `prepare(44100, cfg)` → ring, envelope table, STFT and freeze oscillators
  all resized; grain array is fixed-size and only re-initialised. All ms-denominated constants
  recomputed. Engine is silent until the ring refills (FR-014).
- At 96 kHz a 30 s ring is 2 880 000 samples → rounded to 4 194 304 → **33.6 MB per voice** (RA-2). The
  header must state this; Phase 7 must not be surprised by it.
- Grain lifetimes, densities and decorrelation offsets are specified in **seconds/ms** and must be
  rate-invariant (SC-009); only the blur FFT geometry is specified in samples, so blur's frequency
  resolution changes with rate — documented, not a defect.

**Seed determinism**
- Same seed + same setter history → same render within `render_fingerprint` tolerances (SC-010).
- `reset()` re-seeds; a render after `reset()` matches the original render (FR-006). This requires
  `reset()` to call `GrainScheduler::seed(derived)` explicitly — `GrainScheduler::reset()`/`prepare()`
  never touch `rng_` (`grain_scheduler.h:33-42`, `:97`), so an implementation that only calls
  `scheduler_.reset()` leaves the jitter stream mid-sequence and this Edge Case fails.
- Different seeds must diverge — asserted as the negative half of SC-010, so the test cannot pass on a
  silent engine.
- Changing `blur` must not shift grain births (FR-044): the blur RNG is a separate stream. A shared RNG
  would make the whole grain layer depend on an unrelated knob.
- `setSeed(0)`: `Xorshift32::seed` substitutes its default for 0 (`random.h:73`), which would collapse
  two streams onto one if any derived value hashed to 0. FR-070 routes every derived seed through
  `deriveStreamSeed` (`random.h:102-111`), whose final `(h != 0u) ? h : 0x2545F491u` makes that
  unreachable; the header states that `setSeed(0)` is nonetheless a *valid, distinct* engine seed because
  the derivation, not the raw value, is what reaches `Xorshift32::seed`.

---

## Resolved Roadmap-Level Questions

Where the roadmap left a decision unstated *and* this spec could not settle it from measurement alone.
All three were decided in the 2026-07-28 clarification session; the decision is what the FR/SC text
implements, and the residual is named per item so a later phase does not reopen a settled point.

- **OQ-1 — What is "default density" in the roadmap's CPU budget (line 248)?** The roadmap gates ≤ 1 %
  per voice "at default density" without defining it, and the mean concurrent grain count — which *is*
  the cost — is `density × grainSeconds`, a product the roadmap never fixes.
  **Decided:** default density **4 grains/s × `grainSeconds` 4 s = 16 concurrent grains** (FR-009's
  defaults), measured as SC-004 configuration (a). SC-004 **additionally** gates the saturated 64-grain
  configuration (c) against the **same** 1 % reference, and both must pass, so the budget cannot be met
  by choosing a flattering default. See **RA-4**: the 1 % figure itself is the roadmap's, and the
  roadmap's per-phase budgets do not sum to its own Phase 7 ceiling — Phase 5 measures honestly against
  1 % and hands Phase 7 four real numbers rather than quietly absorbing an aggregate problem it cannot
  fix.
- **OQ-2 — Per-voice capture length vs. polyphony (RA-2).** A per-voice 30 s ring is 268 MB at 16 voices.
  The roadmap places the capture per voice (line 234) and sets voice count in Phase 7 (line 503).
  **Decided:** `captureSeconds` is a `prepare()` argument defaulting to **8 s** (4.19 MB per voice,
  67 MB at 16). The **shipped** value, and any move to a shared ring, are **Phase 7** decisions —
  RA-2's memory table is the input to that reconciliation and stands as written. Phase 5 makes both
  reachable and changes no FR either way. Long grains re-read the same ring history cyclically, and
  never lap the write head, because FR-025's closed form truncates the lifetime instead: capture length
  bounds grain length, not the other way round.
- **OQ-3 — Is `blurFftSize = 1024` the right default?** It trades frequency resolution (46.9 Hz at
  48 kHz) against the 21.3 ms fixed latency of RA-3 and against SC-004(b)'s budget.
  **Decided:** default **1024**, configurable in `PrepareConfig` over [256, 4096] at `prepare`. If
  SC-004(b) fails on the measured hardware, the **sanctioned** fallback is a default of **512**
  (SC-004 lever 3) — a *specified capability trade*, flagged in the header banner and the compliance
  document. It is never a raised baseline and never a relaxed reference.

---

## Traceability

| Roadmap statement (line) | Covered by |
|---|---|
| "New component (Layer 3, `systems/atmosphere_engine.h`)" (233) | FR-001, New-components table |
| "Source: `RollingCaptureBuffer` tapping the voice's cloud+body output (self-granulating)" (234–235) | FR-010–FR-014, C-3, RA-1 (FR-080–FR-084) |
| "plus optional pure-freeze mode via `SpectralFreezeOscillator`" (235–236) | FR-050–FR-054, SC-007 |
| "Extends grain infrastructure (`GrainPool`, `GrainScheduler`, `GrainEnvelope`) for ultra-long grains: 50 ms – 30 s" (236–238) | FR-020–FR-028, C-1, SC-002 |
| "needs buffer-lifetime management so a 30 s grain survives capture-buffer wraparound (per-grain reference into a slice-pool snapshot — `SlicePool` pattern applies)" (238–240) | FR-011, FR-025, C-2, N-6, SC-002 |
| "Spectral blur: per-grain STFT magnitude smearing (phase randomization amount) using existing `STFT`/`SpectralBuffer`" (241–242) | FR-040–FR-046, C-4, SC-005, SC-006 |
| "Pitch drift per grain (`BrownianDrift` again)" (243) | FR-030 (per-grain SoA OU lanes), FR-031, C-5, SC-002 (lane-equivalence gate), SC-011 |
| "density (grains/s, overlapping)" (243) | FR-009, FR-021, FR-022, OQ-1 |
| "spatial diffusion (per-grain pan spread + decorrelation via `stereo_utils`)" (243–244) | FR-032, FR-033, C-7 — **deviation:** `stereo_utils` cannot decorrelate, so decorrelation is a per-grain L/R read-age offset and `stereo_utils` is unused; no width control is added (FR-060, N-9) |
| "zero allocation after prepare" (245–246, first half) | FR-003, FR-020, SC-001 |
| "(30 s × density **worst case pre-allocated** and asserted)" (245–246, parenthetical) | **Deviation — see C-8.** The worst case at the control-table maxima is 600 concurrent grains, which is unserviceable inside SC-004's budget (0.35 ns/grain-sample). Covered by FR-022 (`kMaxGrains = 64`, provisional), FR-023 (skip-never-steal), FR-073 (`density × grainSeconds ≤ kMaxGrains` operating region) and SC-001, **not** by an unqualified tick |
| "no clicks at grain boundaries at any lifetime" (246–247) | FR-023, FR-027, FR-028, SC-003 |
| "blur metric tests (spectral flatness rises with blur)" (247) | SC-005 |
| "no clicks … at any lifetime", envelope endpoints (246–247) | FR-027 (forced table endpoints — `Exponential` ends at ≈0.0183, `grain_envelope.h:144-150`), SC-003 |
| "CPU budget ≤ 1 % per voice at default density" (248) | FR-022, SC-004, OQ-1, **RA-4** (the roadmap's own per-phase budgets do not sum to its Phase 7 25 % ceiling; flagged, not resolved here) |
| Cross-cutting: RT safety (487) | FR-003, FR-008, SC-001 |
| Cross-cutting: layer discipline (488) | FR-001, FR-002, SC-013 (`tools/lint-layers.js`) |
| Cross-cutting: ODR sweep (489) | New-components table, SC-013 (`tools/lint-odr.js`) |
| Cross-cutting: CPU budgets are FRs (490–491) | FR-022, SC-004 |
| Cross-cutting: no bit-exact float goldens (492) | FR-071, SC-010, SC-011 |
| Cross-cutting: portability (493–495) | FR-008, SC-013 (incl. `lint-layers.js`, `lint-odr.js`, scripted non-finite-symbol grep) |
| Cross-cutting: no `std::isnan` under `-ffast-math` | FR-008, FR-063 (reuse `core/db_utils.h:54-57`, `:175-178`), SC-013, SC-014 |

---

## Review notes

No review issue was rejected. Two carried a choice between offered resolutions; the choice and its reason
are recorded here so the next reader does not have to re-derive them.

- **RA-4 / SC-004 — why option (b), not (a).** The review offered either halving SC-004's single-engine
  reference to 0.5 % (the Phase 4 precedent) or explicitly flagging the aggregate for Phase 7. RA-4's
  table shows halving does not close the gap either: at 0.5 %/voice for phases 2/4/5 the tally is
  24 % + 5 % global = **29 %** against a 25 % ceiling. Adopting an unmeasured 0.5 % gate here would
  therefore trade a visible contradiction for an invisible one *and* risk making SC-004 unsatisfiable —
  the blur stage alone is two STFT pipelines, and one comparable pipeline is documented at "< 0.5 % CPU"
  (`spectral_freeze_oscillator.h:20`). Phase 5 keeps the roadmap's own 1 % for all four configurations
  (no configuration exempted, no baseline padded, no lever that raises a baseline), adds a no-padding and
  compliance-recording requirement so Phase 7 inherits four measured numbers, and states the unresolved
  aggregate as a blocking Phase 7 item. Nothing here is relaxed relative to the previous revision.
- **SC-004 — the duplicated `static_assert`.** `harmonic_cloud_perf_test.cpp:87-95` does carry both
  algebraically-identical clauses, with a stated rationale about error messages. That rationale is weak
  (the two fail together), and the review's suggestion is strictly better, so this spec now requires one
  headroom clause plus a genuine **floor** (`baseline >= reference / 50.0`). This is an addition of
  coverage, not a removal: the previous pair covered exactly one inequality.

Two knock-on changes were made that no issue asked for, because leaving them would have created new
inconsistencies:

- **`PrepareConfig` gains `maxBlockSamples`** (default 2048, range 64…8192). FR-043's now-explicit
  pull-inside-the-loop shape needs a `prepare`-allocated output FIFO of capacity ≥ `fftSize + maxBlockSize`;
  `OverlapAdd`'s own buffer is only `2 · fftSize` (`stft.h:243`) and cannot serve. Without a declared
  maximum the FIFO could not be sized at `prepare`, which would breach FR-003.
- **FR-025's bound moved from `C − 1` to `C − 2` everywhere**, including SC-002's threshold and the Edge
  Cases arithmetic (the `pitchSemitones = +24` figure was also wrong at ~2.7 s; it is ~3.6 s). This is a
  tightening, and it is what makes FR-025 and FR-081's independent clamp agree instead of one silently
  masking the other.
