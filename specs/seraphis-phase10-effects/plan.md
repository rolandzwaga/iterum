# Implementation Plan: Seraphis Phase 10 — Integrated Effects

**Spec:** `specs/seraphis-phase10-effects/spec.md` (read in full, this session)
**Roadmap:** `specs/Seraphis-roadmap.md` → Phase 10 (lines 462–469), Reuse Inventory *Effects* row (line 93)
**Branch:** `feat/seraphis-phase1-life-modulators` (the one branch all Seraphis phases land on)
**Status:** PLAN — no implementation
**Date:** 2026-08-02

Every file:line in this document was opened and read in this session. Where a signature is quoted it is
quoted verbatim from the cited line. Where the plan **departs from the spec**, the departure is stated in
§1 with the evidence that forced it — it is never applied silently.

---

## 0. Reading order for an implementer

1. §1 — the eight plan decisions. **Four** of them (**D-1**, **D-2**, **D-3**, **D-4**) contradicted the
   spec as written and were escalated as open questions. **All four were RULED on 2026-08-02, in favour
   of this plan's recommendation in every case**, and the spec has been amended to match — see
   `spec.md` → *Clarifications* → *Phase-owner rulings on the plan's escalations*. Nothing in §1 is
   blocking any more; the task list below stands as written. **D-7** and **D-8** recorded two further
   departures (the probe's capability set and the FR-041 seam set) that the spec's own success criteria
   force but its FR text did not admit; **both have now landed in the spec** (FR-040, FR-041).
2. §2 — component-by-component design, in the order the files must be edited.
3. §3 — the algorithms, with the arithmetic worked out so nothing has to be re-derived.
4. §4 — test plan, one row per FR and per SC.
5. §5 — build integration. §6 — risks. §7 — task order.

---

## 1. Plan decisions (things the spec left implicit, or got wrong)

### D-1 (RESOLVED 2026-08-02 — recommendation ADOPTED). `kFxDelaySyncNoteLabels`'s ten strings do not describe what the component does

**The spec says:** FR-017 and SC-001 pin ten literal labels
`{"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T", "1/2", "1/1"}`, citing
`spectral_delay.h:529-531`, and C-6 row 1419 defaults to index 4 = "1/8".

**What the code does.** `SpectralDelay::setNoteValue(int index)` stores
`noteValueIndex_ = std::clamp(index, 0, 9)` (`spectral_delay.h:532-534`). The **only** consumer is
`process()`:

```cpp
float syncedMs = dropdownToDelayMs(noteValueIndex_, tempo);   // spectral_delay.h:330
```

`Krate::DSP::dropdownToDelayMs(int dropdownIndex, double tempoBPM)` (`core/note_value.h:259-265`) forwards
to `getNoteValueFromDropdown(dropdownIndex)` (`:182-190`), which indexes
`kNoteValueDropdownMapping` (`:136-164`) — a **30-entry** table (`kNoteValueDropdownCount = 30`, `:123`)
whose first ten rows are, verbatim from the header's own comments:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| actual | 1/64T | 1/64 | 1/64D | 1/32T | 1/32 | 1/32D | 1/16T | 1/16 | 1/16D | 1/8T |
| spec/header doc | 1/32 | 1/16T | 1/16 | 1/8T | 1/8 | 1/4T | 1/4 | 1/2T | 1/2 | 1/1 |

The doc comment at `spectral_delay.h:529-531` — and the "Default to 1/8 note (index 4)" comment at
`:893` — are **factually wrong about the component's own behaviour**. Index 4 is `1/32`
(0.125 beats), not `1/8`. The clamp to 9 also means the reachable synced range tops out at **1/8T**
(0.333 beats), so no synced setting can exceed ~333 ms even at 60 BPM.

**Why this matters and cannot be papered over.** SC-001 asserts the label strings literally; SC-019
asserts the measured echo period equals `dropdownToDelayMs(index, tempo)`. Both pass with the spec's
strings — and the user still reads "1/8" while hearing a 1/32. That is precisely the failure mode SC-001's
own rationale names ("the label a user reads does not describe the delay they hear").

**Recommended resolution (what this plan is written against).** Ship the labels that describe the
behaviour:

```cpp
inline constexpr std::array<const Steinberg::Vst::TChar*, 10> kFxDelaySyncNoteLabels = {
    STR16("1/64T"), STR16("1/64"), STR16("1/64D"), STR16("1/32T"), STR16("1/32"),
    STR16("1/32D"), STR16("1/16T"), STR16("1/16"), STR16("1/16D"), STR16("1/8T")};
```

with the **default index changed from 4 to 7** (`1/16`, 0.25 beats = 125 ms at 120 BPM — the musically
sane default in the reachable range), and SC-001's literal-string clause and SC-019 both rewritten
against this table. Fixing the mapping inside `dsp/` (extending `setNoteValue`'s clamp, or remapping
onto `kNoteValueDropdownMapping[3..21]`) is a `dsp/` behaviour change the Non-goals forbid.

**Gate that makes the choice self-checking**, whichever set ships — added beside the table:

```cpp
// Ties label i to the period dropdownToDelayMs(i, ·) actually produces, so a
// permuted or aspirational table fails at build time rather than in a user's ears.
static_assert(Krate::DSP::dropdownToDelayMs(7, 120.0) == 125.0f,   // "1/16" at 120 BPM
              "FR-017: kFxDelaySyncNoteLabels[7] must name the period the component produces");
```

(`dropdownToDelayMs` is `constexpr`, `note_value.h:259`, so this is a compile-time gate.)

**→ OPEN QUESTION 1 — RESOLVED 2026-08-02. The recommendation above was ADOPTED IN FULL.** The
behaviour-describing labels ship, the registered default index moves **4 → 7** (`1/16`, 0.25 beats,
125.0 ms at 120 BPM), and the `constexpr` gate lands beside the table. Encoded in `spec.md` →
**FR-017**, **C-6 row 1419**, **SC-001** and **SC-019**, with the ruling logged under *Clarifications* →
*Phase-owner rulings on the plan's escalations*. **T-2 is unblocked**; the tables in §2.2 take the
recommended set with no further change.

### D-2 (RESOLVED 2026-08-02 — recommendation ADOPTED). ID 1400 and shipped ID 2 are two writers on `setOutputSaturation`

**The spec says:** FR-021 — "ID 1400 MUST drive `SeraphisEngine::setOutputSaturation`
(`seraphis_engine.h:672`) **and nothing else**."

**What already ships — THREE writers, not two.** The enumeration below is exhaustive; an earlier
revision of this decision listed only the second and missed the prepare-time one, which is the writer
that decides what the *first* post-prepare block renders with.

1. **`setupProcessing()`, `processor.cpp:538-539`:**
   ```cpp
   engine_->setOutputSaturation(
       lastPushedSoftLimit_ ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);
   ```
   carrying its own KNOWN-RESIDUAL banner (`:521-537`) recording that this push **ramps** rather than
   snaps, because it necessarily runs after `SeraphisEngine::prepare()`.
2. **`pushGlobalParams()`, `processor.cpp:1090-1097`** — the on-change block quoted below.
3. **`pushEffectsParams()`** — new in this phase.

`pushGlobalParams()` writes the setter on change of `kSoftLimitId`:

```cpp
const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
if (!lastPushedSoftLimitValid_ || soft != lastPushedSoftLimit_) {  // ON CHANGE ONLY
    engine_->setOutputSaturation(soft ? Krate::DSP::SeraphisEngine::kOutputSaturation
                                      : 0.0f);
    ...
}
```
(`processor.cpp:1090-1097`; `GlobalParams::softLimit` defaults `true`, `global_params.h:52`.)

Two independent on-change trackers writing one setter means last-writer-wins with **no convergence**:
toggle ID 2 off→on after setting ID 1400 to 0.8 and the engine silently reverts to 0.15 until ID 1400
next moves. Two shipped tests already read this quantity back
(`engSoftLimitPushCountForTest()`, `processor.h:235`).

**Resolution (what this plan is written against): one writer, composed value.** `pushEffectsParams()`
becomes the sole writer, and the soft-limit switch keeps its shipped meaning as a **gate**:

```cpp
// ONE writer on SeraphisEngine::setOutputSaturation. ID 2 keeps its shipped
// meaning (off => no output saturation at all, processor.cpp:1090-1097); ID 1400
// supplies the AMOUNT that gate passes.
const float amount = soft ? effectsParams_.saturation.load(kRelaxed) : 0.0f;
if (!lastPushedSaturationValid_ || amount != lastPushedSaturation_) {
    engine_->setOutputSaturation(amount);
    lastPushedSaturation_ = amount;
    lastPushedSaturationValid_ = true;
    ++engSoftLimitPushes_;          // the EXISTING counter - no Phase 9 assertion moves
}
```

and the corresponding block is **removed** from `pushGlobalParams()`, with `lastPushedSoftLimit_` /
`lastPushedSoftLimitValid_` moving into the effects push (or kept and read by it — see §2.5.4).

**Writer 1 is composed too, and seeded.** The prepare-time push at `processor.cpp:538-539` becomes the
same composed expression, and seeds the effects tracker exactly as step 4 already seeds
`lastPushedPolyphony_` / `lastPushedSoftLimit_` (`processor.cpp:516-517`):

```cpp
const float amount = lastPushedSoftLimit_
                         ? effectsParams_.saturation.load(std::memory_order_relaxed)
                         : 0.0f;
engine_->setOutputSaturation(amount);
lastPushedSaturation_ = amount;
lastPushedSaturationValid_ = false;   // FR-045's shape: value seeded, cadence counter not
```

Without this, a prepare with `kFxSaturationId = 0.8` installs `0.15` at `:538` and only converges on the
first `process()` (via `pushAllSurfaces(Reprepared)`), so the first ~5 ms of every post-prepare render
carries the wrong saturation amount. That is an **extension of the residual already documented at
`:521-537`**, not a new one — but it is disclosed here rather than inherited silently. `lastPushedSaturationValid_`
is deliberately left `false` so the first `process()` still pushes once and counts once, which is the
cadence `engSoftLimitPushCountForTest()` (`processor.h:235`) is asserted against.

*At the C-6 defaults this is bit-identical to today:* `soft == true`, `saturation == 0.15f ==
SeraphisEngine::kOutputSaturation` (`seraphis_engine.h:248`), so the pushed value is the same `0.15f`,
pushed the same number of times. SC-002 is unaffected.

**→ OPEN QUESTION 2 — RESOLVED 2026-08-02. The recommendation above was ADOPTED IN FULL.** FR-021 now
reads "and **nothing else MUST write that setter**", with the composed single-writer form, the removal of
the `pushGlobalParams()` block (its counter kept and incremented from the new site) and the composed +
seeded prepare-time push all stated as numbered clauses. Encoded in `spec.md` → **FR-021**; ruling logged
under *Clarifications* → *Phase-owner rulings on the plan's escalations*. **T-8 (the saturation push) is
unblocked**; §2.5.4 stands as written.

### D-3 (RESOLVED 2026-08-02 — recommendation ADOPTED). There is no free-running absolute chunk grid, and FR-008 must stop claiming one

**The spec says:** FR-008 — the reset trigger "MUST be evaluated on the accumulator's chunk boundary (the
absolute `kFxSendChunkSamples` grid) … so it is partition-independent", and "that grid is **free-running**:
its sample counter advances on every block whether the send is active, draining or bypassed, so its phase
depends only on the render start and never on engage time". FR-003a adds "The FIFOs MUST NOT be zeroed or
re-phased on engage".

**Why the two cannot both hold.** While bypassed the processor may not write the input FIFO at all
(FR-007), so the FIFO's fill does not advance. Its chunk boundaries are therefore at fixed offsets from
the **engage** point, not from the render start. An earlier revision of this plan kept a separate
free-running counter `fxPhase_` and fired the reset when *it* crossed a multiple of `kFxSendChunkSamples` —
which does not rescue the claim, it only relocates it: the reset then fires at a fill-chunk boundary whose
absolute position still depends on the bypass history (up to 511 samples of drift per excursion), and that
history depends on how the host partitioned the blocks the excursion happened to land in. It also could
not be written: `fxPhase_ += n` sits beside `controlPhase_ += n` **after** `renderSlice()` returns
(`processor.cpp:825`), so inside the send stage the counter holds only the slice-start value, and under
D-6's 64-sample subdivision a slice may contain no multiple of 512 at all — the trigger would have no
defined firing block, and SC-018(a)'s "+1 per qualifying transition" nothing definite to assert against.

**Design (option (b) of the review's two).** `fxPhase_` is **deleted**. There is one counter and one grid:

- `fxChunkFill_` — the input FIFO's own occupancy. A chunk runs whenever it reaches
  `kFxSendChunkSamples`. This is what FR-003a protects, and §3.1 proves it keeps the pipeline delay at
  exactly 512 samples in every partition.
- **FR-008's deferred reset fires on the chunk-loop iteration itself** — the loop body already runs
  exactly once per 512 accumulated samples, so "the next fill-chunk boundary" is a concrete, single,
  unambiguous firing point with no second counter, no `%` on a stale value and no endpoint ambiguity
  (§3.1 states the exact placement: at the **top** of `runSendStage`, before any partial chunk-loop
  state is live).

**What this actually guarantees**, stated so the spec can be amended to it rather than to an aspiration:

> The reset lands on a fill-chunk boundary. Within a continuously-engaged span the chunk phase is a pure
> function of the samples consumed since engage and is therefore **independent of how the host partitions
> those samples** — which is the property SC-017 needs and tests. Across a bypass excursion the phase is a
> function of engage history, **not** of the render start.

**→ OPEN QUESTION 3 — RESOLVED 2026-08-02. The recommendation above was ADOPTED IN FULL.** `fxPhase_` is
deleted; the sole grid is `fxChunkFill_`; FR-008's reset fires on the fill-chunk boundary. FR-008's
"absolute … free-running … phase depends only on the render start" sentence and FR-003a's matching clause
are **struck** and replaced with the guarantee paragraph above; the *Session 2026-08-02* Q5 clarification
("Free-running grid") is marked **SUPERSEDED**. Encoded in `spec.md` → **FR-003a**, **FR-007**,
**FR-008**, **C-3**, **SC-017** and the *New components* member list; ruling logged under
*Clarifications* → *Phase-owner rulings on the plan's escalations*. **T-7 is unblocked.** Residual
tracked as **R-12** in §6, unchanged.

### D-4 (RESOLVED 2026-08-02 — recommendation ADOPTED). The azimuth pan pair MUST be centre-normalised, and C-5's energy claim is backwards

**The spec says:** C-5 — "Azimuth is energy-preserving by construction, because the pan law is `cos`/`sin`
— `gL² + gR² = 1` at every position. This is what keeps the limiter's job unchanged (SC-006)"
(`spec.md:552-553`), restated in Edge cases as "Equal-power keeps total energy constant, so the limiter's
job is unchanged" (`spec.md:1404-1405`).

**Why that is the wrong invariant here.** `equalPowerGains` is a *crossfade* law: it preserves energy when
the two gains are applied to **two different** signals that are then summed. Here they are applied to the
**two channels of one stereo bus**, so the quantity that must be constant is the bus's total energy
`L²·gL² + R²·gR²`, which for a correlated bus is `≈ x²·(gL² + gR²)`. `gL² + gR² = 1` therefore means the
stage **loses 3.01 dB** relative to bypass, not that it is neutral: at centre
`equalPowerGains(0.5f, …)` returns `gL = gR = cos(π/4) = 0.7071` (`crossfade_utils.h:50-53`), so the whole
bus drops −3.01 dB the moment `kFxAzimuthDepthId` becomes non-zero, and jumps back when FR-010's skip
re-engages.

**What that breaks — stated correctly.** An earlier revision of this decision justified the compensation
by SC-008. That justification does **not** hold and is withdrawn: FR-010a mandates crossfading the azimuth
gain pair in over 20 ms (`spec.md:736-737`) and §3.4 implements exactly that, so an uncompensated −3.01 dB
change is spread over ~960 samples and SC-008's `maxPerSampleDelta` statistic would not flag it. What the
raw law actually breaks is **level continuity across FR-010's skip boundary**: at `kFxAzimuthDepthId = 0`
the stage is skipped and the bus is untouched; at `kFxAzimuthDepthId = ε` it is 3.01 dB quieter, forever —
a step in steady-state level as a function of a depth control, which no smoother removes because it is not
a transient. That is a measurable property, and it is now measured (§4.2's FR-010/D-4 row).

**Design.** A named compensation constant, applied to both gains:

```cpp
/// Unity at centre. equalPowerGains(0.5) returns cos(pi/4) = sin(pi/4) = 0.70710678
/// on BOTH channels (crossfade_utils.h:50-53), so an uncompensated pan law drops
/// the whole bus 3.01 dB the instant kFxAzimuthDepthId leaves 0 - which FR-010's
/// skip boundary would then expose as a step (SC-008, FR-010a).
inline constexpr float kFxAzimuthCentreComp = 1.41421356f;  // sqrt(2) = 1 / cos(pi/4)
```

`gL² + gR²` is then the constant **2** at every position — still position-independent, which is the
property SC-006's argument actually needs — and `position = 0.5` is exact unity, which is what makes the
FR-010 skip boundary continuous. Peak per-channel gain at full deflection is `√2` (+3.01 dB) on one
channel with the other at 0; the limiter is last and SC-006 gates that configuration.

**→ OPEN QUESTION 4 — RESOLVED 2026-08-02. The recommendation above was ADOPTED IN FULL.** Both gains are
multiplied by `kFxAzimuthCentreComp = 1.41421356f`. C-5's energy bullet and the matching *Edge cases*
sentence are amended to the paragraph below, and the constant is added to **FR-024a** as clause 4 so it
is a named `inline constexpr` with its citation, never a literal at the use site. Encoded in `spec.md` →
**C-5**, **FR-024a clause 4**, **Edge cases**; ruling logged under *Clarifications* → *Phase-owner
rulings on the plan's escalations*. **T-8 (the wander) is unblocked.** The amended text:

> Azimuth is energy-preserving **up to a fixed centre normalisation**: the pan pair is
> `equalPowerGains(pos) × kFxAzimuthCentreComp`, so `gL² + gR² = 2` at every position — constant, and
> exactly unity per channel at centre. **Peak per-channel gain at full deflection is +3.01 dB**, bounded
> by the limiter, which is what SC-006 gates.

The compensation is **not** unmeasured under this plan: §4.2 adds a dedicated row asserting that stepping
`kFxAzimuthDepthId` from 0 to a small ε changes broadband RMS at the tap by **< 0.1 dB**, which an
uncompensated law fails outright by 3.01 dB.

### D-5. A freeze-forced engage must PRIME the send before `setFreezeEnabled(true)` is pushed

`processSpectralFrame` captures on the first frame where `freezing && !wasFrozen_`
(`spectral_delay.h:677-688`), reading `inputL/inputR` — the STFT's current analysis frame. From the C-6
defaults the send has been bypassed since prepare (FR-023a's whole subject), so at the moment of the
forced engage the STFT holds either zeros or a stale, fully-drained tail. Capturing that gives a silent
frozen spectrum, and SC-007 arm (a) ("RMS 5 s after note-off is > −60 dBFS") fails for an
implementation that follows FR-023a literally.

**Design.** A saturating counter of **live-fed** samples since the send last became active, and a named
priming constant:

```cpp
/// Live post-Aether samples the send must have consumed before setFreezeEnabled(true)
/// is pushed, so the frame processSpectralFrame captures (spectral_delay.h:677-688)
/// is assembled entirely from live bus rather than from pre-bypass residue.
/// 2 x kDefaultFFTSize = four hops = two analyses on wholly-live frames
/// (stft.h:134-137, :171; spectral_delay.h:89, :134).
inline constexpr std::uint64_t kFxFreezePrimeSamples =
    2u * Krate::DSP::SpectralDelay::kDefaultFFTSize;   // 2048 = 42.7 ms @ 48 kHz
```

`freezeReady = freezeOn && sendActive && fxLiveSamplesSinceEngage_ >= kFxFreezePrimeSamples`, pushed
on change only. Freeze-**off** is never deferred (`freezeReady` falls the instant `freezeOn` does).
42.7 ms + at most one block sits far inside SC-007's 200 ms measurement point, and it lands the capture
right where SC-008's freeze-on window already is (event + 1024 + 512 + 1024 = 2560 samples = 53.3 ms).

### D-6. `anyClassBSmootherUnsettled()` now covers the send/wander ramps — a stated cost

Widening `classBSmoothers()` 9 → 12 (FR-038b clause 3) puts the send return gain and the two depth
multipliers into the predicate at `processor.cpp:1813-1817` that drives the 64-sample slice
subdivision (`:811-815`). Consequence: for the ~20 ms after any 1410 / 1441 / 1443 automation point, and
for the whole of every send engage/bypass ramp, blocks render as 64-sample sub-slices. Measured cost of
that subdivision on the shipped chain is ≤ 11.7 % of whole-block wall time
(`param_perf_test.cpp:86-91`, the arm-3 ratio table). It is **wanted** — it is what delivers FR-008's
ramp on the absolute grid — but it must be inside SC-013's 2.5 % budget, so SC-013's render is required
to exercise it (§4).

### D-7. FR-040's probe has THREE capabilities, because two success criteria mandate the other two

**The spec contradicts itself.** FR-040 says the probe's "**sole capability**" is to make `renderSlice`
skip C-1 steps 4 and 5 at runtime (`spec.md:974-980`). But SC-003(a) mandates a positive control "with
FR-040's probe configured to run step 5 **after** step 6" (`spec.md:1093-1094`), and SC-008's positive
control (b) mandates "FR-040's probe snapping the 20 ms return-gain ramp (FR-008/FR-009) to instant"
(`spec.md:1190-1191`). An implementer working from FR-040 alone builds a one-capability probe and then
**cannot write two mandatory positive controls**.

**Design.** Three capabilities, all test-TU-only, all inert on every ship path:

| # | Capability | Forced by |
|---|---|---|
| 1 | skip C-1 steps 4 **and** 5 at runtime | FR-040, SC-002, SC-012 |
| 2 | run step 5 **after** step 6 | SC-003(a) positive control |
| 3 | snap the FR-008/FR-009 return-gain ramp to instant | SC-008 positive control (b) |

Capability 3 is the same mechanism Phase 9 already ships for its own class-(b) ramps
(`paramSmootherBypass_`, consumed at `processor.cpp:1767-1771`), so it is a flag on an existing branch,
not a new one.

**→ FR-040's "sole capability" wording must be corrected** to "whose only capabilities are the three
listed in plan D-7, all test-TU-only". This is recorded here rather than applied silently (§0's contract).
It is an editorial correction to an FR the spec's own criteria already overrode, not a scope change, so it
is **not** escalated as an open question — but it must land in the spec before T-10 (seams).
**LANDED 2026-08-02:** `spec.md` → **FR-040** now enumerates all three capabilities, with the "sole
capability" wording struck and the reason recorded inline. **T-10 is unblocked.**

### D-8. FR-041's seam set is amended: seven counters, one renamed, one truncation flag

FR-041 pins **six** test-only read surfaces. Three corrections, each forced by something the plan found:

1. **`effectsStageBlockCountForTest()` → `effectsStageProcessCallsForTest()`, incremented once per
   `process()` call, not per slice.** SC-012 and SC-013 divide the accumulated stage nanoseconds by this
   counter and compare against **per-block** budgets. `renderSlice()` runs once per *slice*: the slice
   loop subdivides on every MIDI event (`processor.cpp:759-786`), on the 2048 cap (`:792`), and — while
   any class-(b) smoother is unsettled — on the absolute 64-sample grid (`:811-815`). SC-012's render is
   Phase 9's SC-009 MIDI script (several slices per block) and SC-013 is *required* to carry an automation
   point on 1410/1441/1443, which by D-6 forces up to **8 sub-slices per 512-sample block**. A per-slice
   divisor would report up to 8× below the true per-block cost and make the 2.5 % budget — which SC-014's
   25 % arithmetic depends on — structurally unable to fail. `effectsStageNs_` still accumulates per
   slice; only the divisor moves.
2. **A seventh counter, `sendChunkCountForTest()`**, incremented once per `spectralDelay_.process()` call.
   FR-007's prohibition (no send process, no bus copy, no scratch touch while neither active nor draining)
   is otherwise observable **only** through SC-012's `[.perf]`-tagged threshold, which is explicitly
   outside the CI gate (`spec.md:1025-1026`). SC-002 cannot see it: at mix 0 the mix loop adds
   `fxOut[i] * 0.0f`, so a fully-running send still leaves the bus bit-identical and `max|a−b| == 0.0f`
   holds. One `std::size_t` moves FR-007 into the CI-gated suite (SC-018 clause (e), §4.1).
3. **`preOutputTapTruncatedForTest()`** — a `bool`. The tap buffers are pinned to
   `SeraphisEngine::kMaxBlockSamples = 2048` by FR-041 itself (`spec.md:993-995`), but the processor
   explicitly supports larger host blocks (`processor.cpp:787-792`, "This is the branch a host block
   larger than 2048 enters"). Without the flag a 4096-sample block silently yields a half-length tap and
   every tap-based criterion measures half a render with no error signal. The flag makes truncation a
   loud failure; §4 additionally pins every tap-measuring criterion to blocks ≤ 2048.

**→ FR-041's clause list must be extended to seven surfaces plus the truncation flag, and clause 1's
counter renamed.** Recorded here, not applied silently. Not escalated — it strengthens the seam set
without changing any threshold.
**LANDED 2026-08-02:** `spec.md` → **FR-041** now pins seven surfaces plus
`preOutputTapTruncatedForTest()`, with clause 1's divisor renamed `effectsStageProcessCallsForTest()` and
its per-`process()`-call cadence stated as load-bearing; the consequential wording landed in **SC-012**
and **SC-013** (the divisor), **SC-018 clause (e)** (the new `sendChunkCountForTest()` observation of
FR-007) and **SC-003**'s isolated-return definition (tap renders pinned to blocks ≤ 2048 with the
truncation flag asserted `false`). **T-10 is unblocked.**

---

## 2. Component-by-component design

No new DSP class at any layer (spec *New components*; the ODR sweep in the spec was re-checked this
session for the one name this plan claims: `grep -rn "struct EffectsParams" dsp/ plugins/` → no match).

### 2.1 `plugins/seraphis/src/plugin_ids.h` (extended) — FR-013, FR-020, FR-031

Append to `enum ParameterIDs` (`:73-182`), after `kAetherWidthId = 1217`:

```cpp
    // --- Effects (1400-1499) --- 16 IDs (Phase 10)
    kFxSaturationId            = 1400,
    kFxDelayMixId              = 1410,
    kFxDelayTimeId             = 1411,
    kFxDelaySpreadId           = 1412,
    kFxDelaySpreadDirectionId  = 1413,
    kFxDelayFeedbackId         = 1414,
    kFxDelayTiltId             = 1415,
    kFxDelayDiffusionId        = 1416,
    kFxDelayWidthId            = 1417,
    kFxDelaySyncId             = 1418,
    kFxDelaySyncNoteId         = 1419,
    kFxSpectralFreezeId        = 1430,
    kFxWidthId                 = 1440,
    kFxWanderDepthId           = 1441,
    kFxWanderRateId            = 1442,
    kFxAzimuthDepthId          = 1443,
```

Band + version constants and gates:

```cpp
constexpr Steinberg::Vst::ParamID kEffectsParamRangeEnd = 1500;  // IDs < 1500 -> effects pack

static_assert(kAetherParamRangeEnd < kEffectsParamRangeEnd, "...ladder strictly increasing...");
static_assert(kFxSaturationId >= kAetherParamRangeEnd
                  && kFxAzimuthDepthId < kEffectsParamRangeEnd,
              "spec C-5: effects IDs must lie inside the 1400+ band");

constexpr Steinberg::int32 kStateVersion2       = 2;  ///< Phase 9, 2532 bytes.
constexpr Steinberg::int32 kCurrentStateVersion = 3;  ///< Phase 10 (spec C-8), 2596 bytes.
static_assert(kStateVersion1 < kStateVersion2 && kStateVersion2 < kCurrentStateVersion,
              "FR-012 / FR-031: the version chain must be strictly increasing");
```

The `kGlobalParamRangeEnd … kAetherParamRangeEnd` ladder assertion at `:256-264` gains
`&& kAetherParamRangeEnd < kEffectsParamRangeEnd`. The frozen-type legend (`:184-240`) gains 16 rows:
R = 1400, 1410–1412, 1414–1417, 1440–1443 (12); L = 1413, 1419 (2); T = 1418, 1430 (2). New totals
**85 R + 14 L + 8 T = 107**.

### 2.2 `plugins/seraphis/src/parameters/dropdown_mappings.h` (extended) — FR-017

Two tables plus the plugin-local sentinel. `spectral_delay.h` declares **no** enumerator count (unlike
`ContinuousBody::kNumMaterials`, asserted at `dropdown_mappings.h:198`), so it is declared here:

```cpp
// ID 1413 - kFxDelaySpreadDirectionId
// Declaration order of Krate::DSP::SpreadDirection (spectral_delay.h:53-57):
// LowToHigh = 0, HighToLow, CenterOut. All three are live branches of
// calculateBinDelayMs's switch (:587-597).
inline constexpr std::size_t kSpreadDirectionCount = 3;
static_assert(kSpreadDirectionCount
                  == static_cast<std::size_t>(Krate::DSP::SpreadDirection::CenterOut) + 1u,
              "FR-017: an enum extension must not silently desynchronise this count "
              "from the directions the component ships (spectral_delay.h:53-57)");

inline constexpr std::array<const Steinberg::Vst::TChar*, 3> kFxSpreadDirectionLabels = {
    STR16("Low \xE2\x86\x92 High"), STR16("High \xE2\x86\x92 Low"), STR16("Center \xE2\x86\x92 Out")};
static_assert(kFxSpreadDirectionLabels.size() == kSpreadDirectionCount,
              "FR-017: one label per SpreadDirection enumerator");

// ID 1419 - kFxDelaySyncNoteId. Plan D-1, RULED 2026-08-02: these ten strings are
// kNoteValueDropdownMapping's first ten rows (core/note_value.h:136-164), i.e. the
// periods dropdownToDelayMs(index, tempo) ACTUALLY produces for setNoteValue's 0-9
// index (spectral_delay.h:330, :532-534). The mapping documented at
// spectral_delay.h:530 names different periods and is NOT transcribed.
inline constexpr std::array<const Steinberg::Vst::TChar*, 10> kFxDelaySyncNoteLabels = {
    STR16("1/64T"), STR16("1/64"), STR16("1/64D"), STR16("1/32T"), STR16("1/32"),
    STR16("1/32D"), STR16("1/16T"), STR16("1/16"), STR16("1/16D"), STR16("1/8T")};

// The default index is DECLARED HERE, beside the table it indexes, because the
// static_assert below needs it and dropdown_mappings.h is INCLUDED BY
// effects_params.h, not the other way round - a symbol defined there would not
// resolve here. effects_params.h aliases it (section 2.3).
inline constexpr int kFxDelaySyncNoteDefaultIndex = 7;  // "1/16", 0.25 beats (D-1, ruled)
static_assert(kFxDelaySyncNoteDefaultIndex >= 0
                  && kFxDelaySyncNoteDefaultIndex
                         < static_cast<int>(kFxDelaySyncNoteLabels.size()),
              "FR-017: the default index must address the shipped label table");
static_assert(Krate::DSP::dropdownToDelayMs(kFxDelaySyncNoteDefaultIndex, 120.0) == 125.0f,
              "FR-017 / plan D-1: label index must name the period the component produces");
```

The arithmetic of the ruled set holds exactly: `dropdownToDelayMs(7, 120.0)` is
`0.25 beats × 60000/120` computed in `double` with a single narrowing cast at the end
(`note_value.h:147`, `:226-241`, `:259-265`) = **exactly `125.0f`**, so the compile-time gate is sound
once the symbol resolves.

Plus one converter, in the shape of `toBodyMaterial` (`:221-224`):

```cpp
[[nodiscard]] inline Krate::DSP::SpreadDirection toSpreadDirection(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(kSpreadDirectionCount) - 1);
    return static_cast<Krate::DSP::SpreadDirection>(i);
}
[[nodiscard]] inline int fromSpreadDirection(Krate::DSP::SpreadDirection d) noexcept {
    return static_cast<int>(d);
}
```

The header gains `#include <krate/dsp/effects/spectral_delay.h>` (Layer 4 into a plugin header — legal;
`processor.h:21` already includes `aether_reverb.h`). `kSyncNoteLabels` (8 entries, `:135-137`) is **not**
touched and **not** reused (FR-017).

Registration uses the existing `addDropdownParam(parameters, title, id, defaultIndex, labels, count)`
overload (`:305-315`), which is the ONE path that pins `info.defaultNormalizedValue` (`:287-301`).

### 2.3 `plugins/seraphis/src/parameters/effects_params.h` (NEW) — FR-014, FR-015, FR-032

Shape copied verbatim from `aether_params.h`: named `inline constexpr` range/default constants with the
DSP line cited beside each, a `struct` of `std::atomic<>`, then the six functions.

```cpp
namespace Seraphis {

// --- C-6 ranges, transcribed ONCE, each with its source line ------------------
inline constexpr float kFxDelayTimeMinMs  = Krate::DSP::SpectralDelay::kMinDelayMs;   // :91  = 0
inline constexpr float kFxDelayTimeMaxMs  = Krate::DSP::SpectralDelay::kMaxDelayMs;   // :92  = 2000
inline constexpr float kFxDelaySpreadMinMs= Krate::DSP::SpectralDelay::kMinSpreadMs;  // :95  = 0
inline constexpr float kFxDelaySpreadMaxMs= Krate::DSP::SpectralDelay::kMaxSpreadMs;  // :96  = 2000
inline constexpr float kFxDelayTiltMin    = Krate::DSP::SpectralDelay::kMinTilt;      // :101 = -1
inline constexpr float kFxDelayTiltMax    = Krate::DSP::SpectralDelay::kMaxTilt;      // :102 = +1
inline constexpr float kFxWidthMinPercent = Krate::DSP::MidSideProcessor::kMinWidth;  // :65  = 0
inline constexpr float kFxWidthMaxPercent = Krate::DSP::MidSideProcessor::kMaxWidth;  // :66  = 200

/// C-7 clause 2 / FR-016. DELIBERATELY BELOW the component's kMaxFeedback = 1.2f
/// (spectral_delay.h:99). The cap alone bounds nothing; the compensation below is
/// what does (plan section 3.2).
inline constexpr float kFxDelayFeedbackMax = 0.95f;

/// FR-016a / C-7 clause 2. Lives HERE, beside the bound it enforces, and NOT in
/// processor.cpp: a constexpr function defined in a .cpp with no header
/// declaration is neither callable nor constant-evaluable from another TU, and
/// two test obligations call it from effects_chain_test.cpp (SC-005 clause 1's
/// 513-bin sweep and FR-016a's own SECTION). Derivation in plan section 3.2.
/// std::abs is NOT used: <cmath>'s float overloads are not constexpr before
/// C++23 on every leg, so the branchless form is mandatory.
[[nodiscard]] inline constexpr float tiltCompensatedFeedback(float fb, float tilt) noexcept {
    const float mag = (tilt < 0.0f) ? -tilt : tilt;
    return fb / (1.0f + mag);
}
static_assert(tiltCompensatedFeedback(kFxDelayFeedbackMax, 1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, -1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, 0.0f) == kFxDelayFeedbackMax,
              "FR-016a: the worst tilted bin must land back at the registered cap");

// --- C-6 defaults ------------------------------------------------------------
inline constexpr float kFxSaturationDefault   = Krate::DSP::SeraphisEngine::kOutputSaturation; // :248
inline constexpr float kFxDelayMixDefault     = 0.0f;
inline constexpr float kFxDelayTimeDefault    = Krate::DSP::SpectralDelay::kDefaultDelayMs;    // :93
inline constexpr float kFxDelaySpreadDefault  = 0.0f;
inline constexpr int   kFxDelaySpreadDirectionDefault = 0;
inline constexpr float kFxDelayFeedbackDefault= 0.35f;
inline constexpr float kFxDelayTiltDefault    = 0.0f;
inline constexpr float kFxDelayDiffusionDefault = 0.30f;
inline constexpr float kFxDelayWidthDefault   = 0.50f;
inline constexpr bool  kFxDelaySyncDefault    = false;
inline constexpr int   kFxDelaySyncNoteDefault= kFxDelaySyncNoteDefaultIndex;  // dropdown_mappings.h
inline constexpr bool  kFxSpectralFreezeDefault = false;
inline constexpr float kFxWidthDefault        = Krate::DSP::MidSideProcessor::kDefaultWidth;   // :67
inline constexpr float kFxWanderDepthDefault  = 0.0f;
inline constexpr float kFxWanderRateDefault   = Krate::DSP::BrownianDrift::kDefaultSmoothness; // :107
inline constexpr float kFxAzimuthDepthDefault = 0.0f;

struct EffectsParams {
    std::atomic<float> saturation{kFxSaturationDefault};       // 1400
    std::atomic<float> delayMix{kFxDelayMixDefault};           // 1410
    std::atomic<float> delayTimeMs{kFxDelayTimeDefault};       // 1411
    std::atomic<float> delaySpreadMs{kFxDelaySpreadDefault};   // 1412
    std::atomic<int>   spreadDirection{kFxDelaySpreadDirectionDefault};  // 1413
    std::atomic<float> delayFeedback{kFxDelayFeedbackDefault}; // 1414
    std::atomic<float> delayTilt{kFxDelayTiltDefault};         // 1415
    std::atomic<float> delayDiffusion{kFxDelayDiffusionDefault};// 1416
    std::atomic<float> delayWidth{kFxDelayWidthDefault};       // 1417
    std::atomic<bool>  delaySync{kFxDelaySyncDefault};         // 1418
    std::atomic<int>   delaySyncNote{kFxDelaySyncNoteDefault}; // 1419
    std::atomic<bool>  spectralFreeze{kFxSpectralFreezeDefault};// 1430
    std::atomic<float> width{kFxWidthDefault};                 // 1440
    std::atomic<float> wanderDepth{kFxWanderDepthDefault};     // 1441
    std::atomic<float> wanderRate{kFxWanderRateDefault};       // 1442
    std::atomic<float> azimuthDepth{kFxAzimuthDepthDefault};   // 1443
};
```

The six functions, all `inline`, exactly the `aether_params.h` shapes:

| Function | Model | Notes |
|---|---|---|
| `handleEffectsParamChange(EffectsParams&, ParamID, ParamValue)` | `:100-155` | one `switch`; each row denormalizes ONCE, through a named min/max, and **clamps**. `1414` clamps to `[0, kFxDelayFeedbackMax]` **before** any use (Edge cases: "a preset or state blob carrying an out-of-range value must be clamped on load … before the compensation divide"). `1413` / `1419` use `std::clamp(index, 0, N-1)`. `1418` / `1430` use `value >= 0.5`. |
| `registerEffectsParams(ParameterContainer&)` | `:161-215` | 12 × `parameters.addParameter(...)`; 2 × `addDropdownParam(parameters, title, id, defaultIndex, labels, count)` (`dropdown_mappings.h:305`); 2 × `addParameter(..., /*stepCount*/1, default ? 1.0 : 0.0, ...)`. Names: `"FX Saturation"`, `"Delay Mix"`, `"Delay Time"`, `"Delay Spread"`, `"Delay Spread Dir"`, `"Delay Feedback"`, `"Delay Tilt"`, `"Delay Diffusion"`, `"Delay Width"`, `"Delay Sync"`, `"Delay Sync Note"`, `"Spectral Freeze"`, `"Stereo Width"`, `"Wander Depth"`, `"Wander Rate"`, `"Azimuth Depth"`. Units: `"ms"` on 1411/1412, `"%"` on 1440, `""` elsewhere. |
| `formatEffectsParam(ParamID, ParamValue, String128)` | `:221-267` | `%.0f%%` for the `[0,1]` rows, `%.1f ms` for 1411/1412, `%+.2f` for 1415, `%.0f %%` for 1440, `"On"/"Off"` for 1418/1430, and the label string for 1413/1419. Returns `kResultFalse` for anything else. |
| `saveEffectsParams(const EffectsParams&, IBStreamer&)` | `:274-294` | **64 bytes**: 12 `writeFloat` in C-6 table order, then `writeInt32(spreadDirection)`, `writeInt32(delaySyncNote)`, `writeInt32(delaySync ? 1 : 0)`, `writeInt32(spectralFreeze ? 1 : 0)`. Order is fixed here and mirrored exactly by the two loaders. |
| `loadEffectsParams(EffectsParams&, IBStreamer&) -> bool` | `:297-340` | EOF-safe: each read guarded, `return false` on short stream, every unread field left at its C-6 default. Re-clamps 1414 and the two indices on the way in. |
| `loadEffectsParamsToController(IBStreamer&, SetParamFunc)` | `:347-388` | inverts every mapping; `if (streamer.readFloat(fv)) { … }` per field. |

### 2.4 `plugins/seraphis/src/processor/processor.h` (extended)

New includes: `"parameters/effects_params.h"`, `<krate/dsp/effects/spectral_delay.h>`,
`<krate/dsp/processors/brownian_drift.h>`, `<krate/dsp/processors/midside_processor.h>`,
`<krate/dsp/core/block_context.h>`, `<krate/dsp/core/crossfade_utils.h>`, plus `<span>` (FR-041's tap
accessors), `<chrono>` (FR-041's stage timer) and `<cstdint>` (the `std::int64_t` counters above).

**Named constants** (FR-015, FR-024a — each with the header line that justifies it):

```cpp
/// C-2 clause 5 / FR-003a. SpectralDelay's hop at kDefaultFFTSize = 1024
/// (spectral_delay.h:134 `hopSize_ = fftSize_ / 2`, :89). process() is called
/// with EXACTLY this many samples, never a slice length.
inline constexpr std::size_t kFxSendChunkSamples = Krate::DSP::SpectralDelay::kDefaultFFTSize / 2u;

/// FR-008 / FR-009. Send return-gain ramp. Identical BY CONSTRUCTION to
/// kParamSmoothMs (processor.h:119) - FR-038b clause 2: ONE smoother, not two.
inline constexpr float kFxReturnRampMs = kParamSmoothMs;      // 20 ms
static_assert(kFxReturnRampMs == kParamSmoothMs, "FR-038b cl.2: one smoother for ID 1410");

/// FR-009a. One kMaxDelayMs (spectral_delay.h:92).
inline constexpr float kFxSendDrainMs   = Krate::DSP::SpectralDelay::kMaxDelayMs;   // 2000
/// FR-009a. Linear peak below which the drain ends early - what bounds a bypass
/// excursion's cost by ENERGY rather than by wall clock.
inline constexpr float kFxSendDrainFloor = 1.0e-6f;
/// C-4 / FR-023a. Forced return gain while kFxSpectralFreezeId is engaged.
inline constexpr float kFxFreezeMinReturnGain = 0.5f;
/// plan D-5.
inline constexpr std::uint64_t kFxFreezePrimeSamples =
    2u * Krate::DSP::SpectralDelay::kDefaultFFTSize;                                // 2048

/// C-5. Width span in percent per unit depth-scaled drift. 50 rather than 100
/// because BrownianDrift is bipolar with kInternalStd = 0.5 (brownian_drift.h:101),
/// so at depth 1 the walk normally stays inside +/-0.5: width lands typically
/// 75-125 %, extremes 50-150 %, and can never collapse toward mono
/// (midside_processor.h:65-67).
inline constexpr float kWanderWidthSpanPercent = 50.0f;

/// plan D-4. Unity at centre; see the derivation there.
inline constexpr float kFxAzimuthCentreComp = 1.41421356f;

/// C-5 / FR-026. Two DISTINCT salts, in the shape of aether_reverb.h:1546-1552.
/// Identical salts would make width and azimuth move in lockstep.
inline constexpr std::uint32_t kFxWidthDriftSalt   = 0x5E11A001u;
inline constexpr std::uint32_t kFxAzimuthDriftSalt = 0x5E11A002u;
static_assert(kFxWidthDriftSalt != kFxAzimuthDriftSalt, "C-5: the two drift salts must differ");
```

**New `detail` probe declaration**, beside `SeraphisParamSmootherBypassProbe` (`:122-136`):

```cpp
namespace detail {
/// FR-040 as amended by plan D-7. DECLARED HERE, DEFINED ONLY BY THE PHASE 10
/// TEST TU (tests/integration/effects_chain_test.cpp). THREE capabilities, all
/// test-TU-only - FR-040's "sole capability" wording is corrected by D-7,
/// because SC-003(a) and SC-008(b) each MANDATE one of the other two:
///   1. skip C-1 steps 4 and 5 at runtime          (FR-040, SC-002, SC-012)
///   2. run step 5 AFTER step 6                    (SC-003(a) positive control)
///   3. snap the FR-008/FR-009 return-gain ramp    (SC-008 positive control (b))
/// ODR swept this session: grep -rn "SeraphisEffectsStageBypassProbe" dsp/ plugins/ -> 0 hits.
struct SeraphisEffectsStageBypassProbe;
}  // namespace detail
```

**Public test seams** (FR-041 as amended by plan D-8 — **seven** counters plus the truncation flag):

```cpp
[[nodiscard]] double effectsStageNsForTest() const noexcept { return effectsStageNs_; }
/// D-8 cl.1. Incremented once per process() CALL, in the pre-slice block - NOT
/// once per renderSlice(). SC-012/SC-013 divide effectsStageNsForTest() by this
/// and compare against PER-BLOCK budgets; renderSlice() runs once per SLICE and
/// a block carries up to 8 of them under D-6 (processor.cpp:759-786, :792,
/// :811-815), so a per-slice divisor would under-report by up to 8x.
[[nodiscard]] std::size_t effectsStageProcessCallsForTest() const noexcept {
    return effectsStageProcessCalls_;
}
[[nodiscard]] std::size_t spectralDelayResetCountForTest() const noexcept { return fxResetCount_; }
[[nodiscard]] std::size_t effectsPushCountForTest() const noexcept { return effectsPushes_; }
[[nodiscard]] std::size_t widthDriftBlockCountForTest() const noexcept { return widthDriftBlocks_; }
[[nodiscard]] std::size_t bypassPredicateEvalCountForTest() const noexcept { return bypassEvals_; }
/// D-8 cl.2 / FR-007. One increment per spectralDelay_.process() call. FR-007's
/// prohibition is otherwise visible ONLY through SC-012's [.perf] threshold,
/// which is outside the CI gate: at mix 0 the mix loop adds fxOut[i] * 0.0f, so
/// a fully-running send still leaves the bus bit-identical and SC-002 passes.
[[nodiscard]] std::size_t sendChunkCountForTest() const noexcept { return sendChunks_; }
/// FR-041 clause 6. The stereo bus as it stood IMMEDIATELY BEFORE
/// engine_->processOutputStage() (C-1 step 6, processor.cpp:1170), for the
/// process() call that most recently completed. The spans carry their own
/// length; there is no separate size accessor.
[[nodiscard]] std::span<const float> preOutputTapLForTest() const noexcept;
[[nodiscard]] std::span<const float> preOutputTapRForTest() const noexcept;
/// D-8 cl.3. TRUE when the most recent process() call delivered more than
/// kMaxBlockSamples and the tap therefore covers only its first 2048 samples.
/// FR-041 pins the tap buffers to 2048 (spec.md:993-995) but the processor
/// supports larger host blocks (processor.cpp:787-792), so without this a 4096
/// block yields a half-length tap and every tap-based criterion silently
/// measures half a render. Every tap-measuring criterion MUST assert this false.
[[nodiscard]] bool preOutputTapTruncatedForTest() const noexcept { return preOutTapTruncated_; }
```

**New private members** (all sized in `setupProcessing()`, FR-028):

```cpp
    EffectsParams effectsParams_{};

    // --- C-1 step 4: the send -------------------------------------------------
    Krate::DSP::SpectralDelay spectralDelay_{};   // heap-backed internals; sizeof ~ a few hundred B
    std::vector<float> fxInL_, fxInR_;            // input FIFO, capacity 4096 (power of two)
    std::vector<float> fxOutL_, fxOutR_;          // output FIFO, same capacity
    std::vector<float> fxChunkL_, fxChunkR_;      // ONE kFxSendChunkSamples scratch pair
    std::size_t fxInWrite_ = 0, fxInRead_ = 0, fxChunkFill_ = 0;   // D-3
    std::size_t fxOutWrite_ = 0, fxOutRead_ = 0, fxOutFill_ = 0;
    std::uint64_t fxBypassedSamples_ = 0;         // saturating; FR-008 condition (a)
    std::uint64_t fxLiveSamplesSinceEngage_ = 0;  // saturating; plan D-5
    std::int64_t  fxDrainRemaining_ = 0;          // FR-009a countdown, in samples
    std::int64_t  fxSendDrainSamples_ = 0;        // FR-009a window in samples; set in setupProcessing()
    /// FR-009a's energy exit. Peak |sample| of the chunk the send most recently
    /// produced IN THE CURRENT DRAIN. Set ABOVE the floor on the Active->Draining
    /// transition (section 3.3) so a stale value from a previous drain cannot
    /// terminate a new one on its first block - which would be exactly the tail
    /// annihilation FR-008/FR-009a exist to prevent.
    float fxDrainPeak_ = 1.0f;
    /// Effective return gain the FR-008/FR-009/FR-023a state machine computed for
    /// this process() call. setParamSmootherTargets() targets THIS, not the raw
    /// ID-1410 atomic (section 2.5.5).
    float fxEffectiveReturnGain_ = 0.0f;
    enum class FxSendState : std::uint8_t { Bypassed, Active, Draining };
    FxSendState fxSendState_ = FxSendState::Bypassed;
    bool fxResetDue_ = false;                     // FR-008, deferred to the next fill-chunk boundary (D-3)
    bool fxFifoClearDue_ = false;                 // section 3.1: the ONE deferred-clear flag
    Krate::DSP::BlockContext fxBlockCtx_{};       // FR-030, built once per process()

    // --- C-1 step 5: the wander ----------------------------------------------
    Krate::DSP::MidSideProcessor globalMs_{};
    Krate::DSP::BrownianDrift widthDrift_{};
    Krate::DSP::BrownianDrift azimuthDrift_{};
    Krate::DSP::OnePoleSmoother azimuthGainLSm_{1.0f};
    Krate::DSP::OnePoleSmoother azimuthGainRSm_{1.0f};
    bool fxWanderWasActive_ = false;              // FR-010a transition latch

    // --- three NEW class-(b) smoothers (FR-038b cl. 2) ------------------------
    // INVARIANT, and the reason the review found this: NO smoother may appear in
    // classBSmoothers() AND be .process()-ed. advanceParamSmoothers() advances
    // EVERY element of classBSmoothers() by advanceSamples(sliceSamples)
    // (processor.cpp:1775-1777) and runs at :821, immediately before
    // renderSlice() at :824 - so a member that is also .process()-ed per sample
    // advances 2n samples per n rendered, the ramp completes in ~10 ms instead of
    // kFxReturnRampMs = 20 ms, AND the rate becomes state-dependent (1x while the
    // send is bypassed, 2x while it runs). The shipped precedent is explicit:
    // masterGain_, the ONE smoother advanced per output sample
    // (processor.cpp:1163), is deliberately NOT in classBSmoothers()
    // (:1727-1730), and all nine current members are read with getCurrentValue().
    // RESOLUTION (option (b) of the review's two): all three of these ARE in
    // classBSmoothers(), so all three are read with getCurrentValue() ONLY -
    // hoisted once per 64-sample sub-chunk, never .process()-ed. D-6's
    // subdivision already makes that fine-grained, and it is what keeps
    // anyClassBSmootherUnsettled() honest about when the ramp is done.
    Krate::DSP::OnePoleSmoother fxReturnGainSm_{0.0f};    // ID 1410 == the FR-008/009 ramp
    Krate::DSP::OnePoleSmoother fxWanderDepthSm_{0.0f};   // ID 1441
    Krate::DSP::OnePoleSmoother fxAzimuthDepthSm_{0.0f};  // ID 1443

    // --- once-per-process() hoisted predicates (FR-012) -----------------------
    bool fxSendRuns_ = false;      // active OR draining
    bool fxWanderRuns_ = false;
    /// FR-010a's DISENGAGE arm (section 3.4). fxWanderRuns_ is evaluated on the
    /// RAW atomics; this latch keeps the stage running until every wander
    /// smoother has actually reached identity, so writing kFxWanderDepthId = 0
    /// does not step the stereo image in one block.
    bool fxWanderRunsEffective_ = false;
    /// FR-010a disengage-arm countdown. Re-armed to fxWanderSettleSamples_ on
    /// every block the RAW predicate is true; decremented by blockSamples
    /// otherwise. wanderAtIdentity() cannot return true until it reaches 0, so
    /// the disengage tail is a stated sample count, not a guess (section 3.4).
    std::int64_t fxWanderSettleRemaining_ = 0;
    std::int64_t fxWanderSettleSamples_ = 0;   // set in setupProcessing()
    float fxWidthBase_ = 100.0f;

    // --- on-change push trackers ---------------------------------------------
    float lastPushedSaturation_ = 0.0f;  bool lastPushedSaturationValid_ = false;  // D-2
    float lastPushedFeedbackComp_ = -1.0f;  // FR-016a; -1 is never a legal pushed value
    float lastPushedTilt_ = 2.0f;           // outside [-1,1]
    /* one tracker per remaining SpectralDelay setter, same shape */
    bool  lastPushedFxFreeze_ = false;
    int   lastPushedFxSeedIndex_ = -1;      // FR-026 / FR-027

    // --- FR-040 runtime flags (set ONLY by detail::SeraphisEffectsStageBypassProbe) ---
    bool effectsStageBypassed_ = false;   // D-7 capability 1
    bool effectsStageAfterOutput_ = false;// D-7 capability 2
    bool effectsReturnRampSnap_ = false;  // D-7 capability 3

    // --- FR-041 seams (D-8) ----------------------------------------------------
    std::vector<float> preOutTapL_, preOutTapR_;  // kMaxBlockSamples each (FR-041)
    std::size_t preOutTapCursor_ = 0, preOutTapSize_ = 0;
    bool preOutTapTruncated_ = false;             // D-8 cl.3
    double effectsStageNs_ = 0.0;
    std::size_t effectsStageProcessCalls_ = 0;    // D-8 cl.1 - per process() CALL
    std::size_t fxResetCount_ = 0, effectsPushes_ = 0, widthDriftBlocks_ = 0, bypassEvals_ = 0;
    std::size_t sendChunks_ = 0;                  // D-8 cl.2
```

**Constants used by the members above**, declared beside the ones already listed in this section:

```cpp
/// Section 3.1's capacity bound. A power of two so the ring index is a mask, and
/// >= kFxSendChunkSamples + kMaxBlockSamples = 2560 (seraphis_engine_config.h:43).
inline constexpr std::size_t kFxFifoCapacity = 4096;
static_assert((kFxFifoCapacity & (kFxFifoCapacity - 1u)) == 0u, "ring index is a mask");
static_assert(kFxFifoCapacity >= kFxSendChunkSamples + kMaxBlockSamples, "section 3.1 bound");
```

`kFxSendDrainSamples` is **not** a constant — it depends on the sample rate, so it is the member
`fxSendDrainSamples_` above, computed once in `setupProcessing()` (§3.3).

**New private member functions**, declared so nothing in §3 refers to something that does not exist:

```cpp
    void pushEffectsParams() noexcept;              // section 2.5.4
    void updateEffectsBypassState() noexcept;       // section 3.3
    void runSendStage(float* l, float* r, std::size_t n) noexcept;    // sections 3.1, 3.2
    void runWanderStage(float* l, float* r, std::size_t n) noexcept;  // section 3.4
    /// The ONE definition of the accumulator's start state. Body in section 3.1;
    /// three call sites: setupProcessing(), setActive(false), and the single
    /// deferred mid-render site at the top of runSendStage().
    void clearFifos() noexcept;
    /// FR-010a disengage arm. Body in section 3.4.
    [[nodiscard]] bool wanderAtIdentity() const noexcept;
```

There is **no `fxPhaseCrossedChunkBoundary()`** — D-3 deleted the counter it would have read, and the
reset fires on the fill-chunk boundary `runSendStage` already computes (§3.1).

`classBSmoothers()`'s return type widens to `std::array<Krate::DSP::OnePoleSmoother*, 12>` (`:312`);
`setParamSmootherTargets()` (`:285`), `advanceParamSmoothers()` (`:301`) and
`anyClassBSmootherUnsettled()` (`:327`) cover the three new entries. **`sizeof(Processor) < 64 KiB`
(`:489`) must still hold** — every buffer above is a `std::vector` (24 B each), so the growth is ~400 B
plus `sizeof(SpectralDelay)`, whose members are all heap-backed (`spectral_delay.h:841-935`) plus
`MidSideProcessor` (six floats + five smoothers) and two `BrownianDrift`. Verified by the existing
`static_assert`; if it ever breaches, move `spectralDelay_` behind a `std::unique_ptr` exactly as
`engine_`/`reverb_` are (`:343-344`).

### 2.5 `plugins/seraphis/src/processor/processor.cpp` (extended)

#### 2.5.1 `routeOf` + `markDirty` — FR-019

`enum class Route` (`:131`) gains `FX`; `routeOf` (`:133-249`) gains one arm listing IDs 1410–1443.
**ID 1400 gets its own explicit `case kFxSaturationId: return Route::ENG;`** in the existing Global/ENG
group. That case is not optional bookkeeping: `routeOf`'s `default:` arm returns `Route::Local`
(`processor.cpp:246-248`, under the comment `// --- Processor-local: 0, 100-104, 405, 406 ---`), so
without it 1400 would classify as `Local`, contradicting C-6's Route column. It is behaviourally
invisible today — `markDirty`'s `case Route::ENG:` and `case Route::Local:` share one `break;`
(`:1222-1225`) — which is exactly why it must be written down now: a later phase that gives the two arms
different bodies would silently mis-route 1400 with nothing to catch it. `markDirty`
(`:1213-1230`) gains:

```cpp
        case Route::FX:
            break;  // pushEffectsParams() has its own on-change trackers, exactly
                    // like Route::ENG. Bumping voiceParamGeneration_ here would run
                    // a 37-setter x 16-voice fan-out for a global bus control.
```

#### 2.5.2 `processParameterChanges` — FR-018

One new rung on the existing `if (id < X)` ladder (`:1021-1047`), after the aether rung:

```cpp
        } else if (id < kEffectsParamRangeEnd) {
            handleEffectsParamChange(effectsParams_, id, value);
            markDirty(id);
        }
```

#### 2.5.3 `setupProcessing()` — FR-004, FR-027, FR-028, FR-035

Inserted after step 5 (`:541-545`), **in this order** (the order is load-bearing at two points):

```cpp
    // 5b. THE SEND. setDryWetMix and setFFTSize run BEFORE prepare(), because
    //     prepare() ends in snapParameters() (spectral_delay.h:206, :550) and a
    //     post-prepare setDryWetMix only sets a smoother TARGET (:500-503) that is
    //     advanced ONCE PER process() CALL (:373, :389) despite a per-sample 50 ms
    //     coefficient (:184-194) - tens of seconds of un-aligned current-block dry
    //     leaking into the bus (C-2 clause 1, FR-004). Same rule the engine already
    //     records for the saturator (seraphis_engine.h:331-333).
    spectralDelay_.setFFTSize(Krate::DSP::SpectralDelay::kDefaultFFTSize);  // :408
    spectralDelay_.setDryWetMix(1.0f);                                      // :500
    spectralDelay_.setSpreadCurve(Krate::DSP::SpreadCurve::Logarithmic);    // :448
    spectralDelay_.prepare(sampleRate_, kMaxBlockSamples);                  // :131

    // 5c. FR-027. seedRng THEN reset, which is the order the header itself
    //     documents ("After seeding, call reset() to reinitialize phase buffers",
    //     :295-296). The rationale is NOT that the reverse order leaves stale
    //     ASLR-seeded phases - it does not: seedRng() re-seeds the RNG AND
    //     re-draws stereoPhaseL_/stereoPhaseR_ itself (:297-304), so reset()
    //     first would also end deterministic. The reason to use this order is
    //     that reset() then re-draws those buffers AGAIN from the freshly seeded
    //     stream (:279-284), which makes the post-prepare state a pure function
    //     of the seed and matches the component's documented usage - one order,
    //     stated once, so the seed-change burst in pushEffectsParams() can be
    //     literally the same two calls.
    const std::size_t si = clampSeedIndex(globalParams_.seedIndex.load(std::memory_order_relaxed));
    spectralDelay_.seedRng(kSeedValues[si]);   // :297
    spectralDelay_.reset();                    // :242
    lastPushedFxSeedIndex_ = static_cast<int>(si);

    // 5d. The wander stage.
    globalMs_.prepare(static_cast<float>(sampleRate_), kMaxBlockSamples);   // :96
    globalMs_.setWidth(effectsParams_.width.load(std::memory_order_relaxed));
    globalMs_.reset();                                                      // :114 - snaps
    widthDrift_.prepare(sampleRate_);   azimuthDrift_.prepare(sampleRate_); // :121
    widthDrift_.setMean(0.0f);          azimuthDrift_.setMean(0.0f);        // :165, FR-024a
    widthDrift_.setSeed(kSeedValues[si] ^ kFxWidthDriftSalt);               // :145
    azimuthDrift_.setSeed(kSeedValues[si] ^ kFxAzimuthDriftSalt);
    widthDrift_.reset();                azimuthDrift_.reset();              // :133
    azimuthGainLSm_.configure(kParamSmoothMs, static_cast<float>(sampleRate_));
    azimuthGainRSm_.configure(kParamSmoothMs, static_cast<float>(sampleRate_));
    azimuthGainLSm_.snapTo(1.0f);  azimuthGainRSm_.snapTo(1.0f);

    // 5e. The three class-(b) smoothers, beside the existing nine (:570-579).
    fxReturnGainSm_.configure(kFxReturnRampMs, sr);
    fxWanderDepthSm_.configure(kParamSmoothMs, sr);
    fxAzimuthDepthSm_.configure(kParamSmoothMs, sr);

    // 5f. Accumulator + tap. assign(), never resize() - the FIFOs must start zeroed.
    fxInL_.assign(kFxFifoCapacity, 0.0f);   /* ... fxInR_, fxOutL_, fxOutR_ ... */
    fxChunkL_.assign(kFxSendChunkSamples, 0.0f);  fxChunkR_.assign(kFxSendChunkSamples, 0.0f);
    preOutTapL_.assign(kMaxBlockSamples, 0.0f);   preOutTapR_.assign(kMaxBlockSamples, 0.0f);
    preOutTapCursor_ = preOutTapSize_ = 0;  preOutTapTruncated_ = false;

    clearFifos();   // section 3.1 - the ONE definition of the FIFO start state,
                    // shared verbatim with the two mid-render clear sites, so the
                    // section 3.1 invariant has exactly one establishing point.

    fxBypassedSamples_ = 0;  fxLiveSamplesSinceEngage_ = 0;
    fxDrainRemaining_ = 0;   fxSendState_ = FxSendState::Bypassed;
    fxResetDue_ = false;     fxFifoClearDue_ = false;
    fxDrainPeak_ = 1.0f;     // ABOVE kFxSendDrainFloor: a drain that has not yet
                             // run a chunk must never take the energy exit.
    fxEffectiveReturnGain_ = 0.0f;
    fxSendDrainSamples_ = std::llround(static_cast<double>(kFxSendDrainMs) * 0.001 * sampleRate_);
    fxWanderWasActive_ = false;  fxWanderRunsEffective_ = false;  fxWanderSettleRemaining_ = 0;
    fxWanderSettleSamples_ = std::llround(
        static_cast<double>(std::max(Krate::DSP::MidSideProcessor::kDefaultSmoothingMs,  // :73
                                     kParamSmoothMs))
        * 3.0 * 0.001 * sampleRate_);   // 3 time constants of the slower of the two
```

`setActive(false)` (`:600-616`) gains, beside the engine/reverb clears (FR-035):
`spectralDelay_.reset(); globalMs_.reset(); widthDrift_.reset(); azimuthDrift_.reset();` plus the
`clearFifos()` + state re-initialisation of 5f, `azimuthGainLSm_.snapTo(1.0f)` /
`azimuthGainRSm_.snapTo(1.0f)`, and `fxReturnGainSm_.snapTo(0.0f)`. This is the full list FR-035 names
("the send, the M/S stage, both drifts and the return-gain ramp") and it is what `unit/lifecycle_test.cpp`
asserts (§4.2, §5).

`pushAllSurfaces()` (`:1622-1675`) gains `lastPushedSaturationValid_ = false;` and invalidates every
effects push tracker, so FR-034's `setState()`-after-prepare path re-pushes the whole effects surface.

#### 2.5.4 `pushEffectsParams()` (new) — FR-016a, FR-021, FR-022, FR-023, FR-024, FR-025, FR-026, FR-027

Called **once per `process()` call**, from the pre-slice block beside `pushAetherParamsIfDirty()`
(`:702`). Every push is on-change only, against its own tracker; each push increments
`effectsPushes_` (FR-041 clause 3). It is allocation-free, lock-free, exception-free.

Body, in order:

1. **ID 1400 → `SeraphisEngine::setOutputSaturation`, composed with ID 2** (plan D-2).
2. **The seed**, if `globalParams_.seedIndex` moved since `lastPushedFxSeedIndex_`:
   `spectralDelay_.seedRng(kSeedValues[i]); spectralDelay_.reset(); ++fxResetCount_;` then
   `widthDrift_.setSeed(kSeedValues[i] ^ kFxWidthDriftSalt); widthDrift_.reset();` and the azimuth pair
   (FR-026, FR-027). This is FR-027's second burst site; SC-011 gates the block that contains it.
   Because `spectralDelay_.reset()` empties the STFT/OverlapAdd (`spectral_delay.h:242-291`) and the send
   then genuinely produces nothing for one full `fftSize` fill (`stft.h:133-137`, `:171`), the FIFOs must
   be re-established too — but **`pushEffectsParams()` does not clear them itself**. It sets
   `fxFifoClearDue_ = true`, and `runSendStage` performs the clear at its top, before any chunk-loop
   state is live (§3.1). Clearing here would be safe *today* only by accident of call order; the flag
   makes the single-clear-site rule structural. The two drift `setSeed` calls use **distinct salts**
   (`kFxWidthDriftSalt` / `kFxAzimuthDriftSalt`, §2.4) — FR-024a clause 3; SC-010 now tests the salt
   split observably (§4.1).
3. **IDs 1411, 1412, 1416, 1417** → `setBaseDelayMs` / `setSpreadMs` / `setDiffusion` /
   `setStereoWidth` (`:425`, `:432`, `:489`, `:512`), each guarded by its own `lastPushed*` float.
4. **IDs 1414 + 1415 together** — FR-016a. The helper is
   `Seraphis::tiltCompensatedFeedback(fb, tilt)`, **defined in
   `plugins/seraphis/src/parameters/effects_params.h`** beside `kFxDelayFeedbackMax`, the bound it
   enforces (§2.3) — *not* in `processor.cpp`. It is `inline constexpr` in a header precisely because two
   test obligations evaluate it from another TU (SC-005 clause 1's 513-bin sweep and FR-016a's own
   `SECTION`, both in `effects_chain_test.cpp`); a `constexpr` function defined in a `.cpp` with no
   header declaration is neither callable nor constant-evaluable from there, and both obligations would
   fail to compile. `processor.cpp` calls it; the derivation is §3.2. Recomputed and re-pushed whenever
   **either** ID moves. `setFeedbackTilt(tilt)` is pushed unchanged.
5. **IDs 1418 + 1419** → `setTimeMode(sync ? 1 : 0)` (`:524`) and `setNoteValue(index)` (`:532`).
6. **ID 1413** → `setSpreadDirection(toSpreadDirection(index))` (`:439`).
7. **ID 1430** → the primed freeze push of plan D-5, on change of `freezeReady` only.
8. **ID 1442** → `widthDrift_.setSmoothness(rate); azimuthDrift_.setSmoothness(rate);` (`:152`) — both
   drifts, one value (FR-025).

IDs **1410, 1440, 1441, 1443** are **not** pushed here: 1410/1441/1443 are the three class-(b)
smoothers (targets set in `setParamSmootherTargets()`), and 1440 is pushed inside the wander stage on
the 64-sample control grid (§3.4).

#### 2.5.5 `process()` — FR-012, FR-030

Inserted in the pre-slice block (`:695-724`), all **once per `process()` call**:

```cpp
    // FR-030. ONE BlockContext per process() call, from the SAME tempo sample point
    // Phase 9 already uses (:1563). The guard is Phase 9's three-part guard verbatim
    // in shape (:1585-1586): relying on the component's own `tempo <= 0.0` fallback
    // (spectral_delay.h:325-327) is NOT sufficient - a host may leave a STALE
    // positive tempo in ProcessContext while kTempoValid is clear.
    fxBlockCtx_.sampleRate = sampleRate_;
    fxBlockCtx_.blockSize  = static_cast<std::size_t>(data.numSamples);
    {
        const Vst::ProcessContext* pc = data.processContext;
        const bool tempoOk = pc != nullptr
                             && (pc->state & Vst::ProcessContext::kTempoValid) != 0
                             && pc->tempo > 0.0;
        fxBlockCtx_.tempoBPM = tempoOk ? pc->tempo : 120.0;
        fxBlockCtx_.isPlaying = pc != nullptr && (pc->state & Vst::ProcessContext::kPlaying) != 0;
    }

    updateEffectsBypassState();   // FR-012 - see section 3.3; ++bypassEvals_ ONCE
    pushEffectsParams();          // section 2.5.4

    // FR-011. The drift sources advance EVERY block regardless of any bypass state
    // (C-3 final clause), so re-engaging does not restart a wander that was
    // conceptually running. ~32-sample-decimated AR(1) (brownian_drift.h:105, :194).
    widthDrift_.processBlock(static_cast<std::size_t>(data.numSamples));
    azimuthDrift_.processBlock(static_cast<std::size_t>(data.numSamples));
    ++widthDriftBlocks_;

    preOutTapCursor_ = 0;   // FR-041 clause 6: the tap covers ONE process() call
    preOutTapTruncated_ = static_cast<std::size_t>(data.numSamples) > kMaxBlockSamples;  // D-8 cl.3
    ++effectsStageProcessCalls_;   // D-8 cl.1 - ONCE PER CALL, never per slice
```

`setParamSmootherTargets()` (`:1741-1754`) gains three lines — and ID 1410's target is the **effective**
return gain the state machine computed, not the raw atomic:

```cpp
    fxReturnGainSm_.setTarget(fxEffectiveReturnGain_);            // ID 1410 (FR-008/009/023a)
    fxWanderDepthSm_.setTarget(effectsParams_.wanderDepth.load(kRelaxed));   // ID 1441
    fxAzimuthDepthSm_.setTarget(effectsParams_.azimuthDepth.load(kRelaxed)); // ID 1443
```

#### 2.5.6 `renderSlice()` — FR-001, FR-002, FR-003, FR-006, FR-010, FR-041

Steps 2, 3, 4, 4b, 6, 7 are untouched. Between `4b` (`:1162-1166`) and `5` (`:1170`):

```cpp
    // ---- FR-041 clause 1: the scoped stage timer -------------------------------
    // The timer brackets the effects stage AND the tap copy. The copy is inside
    // deliberately: it is paid in SC-014's whole-render full-poly gate, which has
    // the least headroom of any budget in this phase (4.09 points), so hiding it
    // from SC-012/SC-013 would charge it to the one gate that was not sized for
    // it. Cost accounting for the granularity is in risk R-8.
    const auto fxT0 = std::chrono::steady_clock::now();

    if (!effectsStageBypassed_) {          // D-7 capability 1; false on every ship path
        runSendStage(wetL_.data(), wetR_.data(), n);     // C-1 step 4  (section 3.1/3.2)
        if (!effectsStageAfterOutput_) {                 // D-7 capability 2 (SC-003(a) control)
            runWanderStage(wetL_.data(), wetR_.data(), n);  // C-1 step 5 (section 3.4)
        }
    }

    // ---- FR-041 clause 6: the PRE-OUTPUT-STAGE TAP -----------------------------
    // A copy behind a test-only accessor; it touches no shipping control flow.
    // The guard cannot silently truncate: preOutTapTruncated_ was set in the
    // pre-slice block from the CALL's own numSamples (D-8 clause 3), so a host
    // block > kMaxBlockSamples is a loud, assertable failure rather than a short
    // span. Every tap-measuring criterion asserts it false (section 4).
    if (preOutTapCursor_ + n <= preOutTapL_.size()) {
        std::copy_n(wetL_.data(), n, preOutTapL_.data() + preOutTapCursor_);
        std::copy_n(wetR_.data(), n, preOutTapR_.data() + preOutTapCursor_);
        preOutTapCursor_ += n;
        preOutTapSize_ = preOutTapCursor_;
    }

    effectsStageNs_ += std::chrono::duration<double, std::nano>(
                           std::chrono::steady_clock::now() - fxT0).count();
    // NOTE: no counter increment here. The divisor SC-012/SC-013 use is
    // effectsStageProcessCalls_, incremented ONCE PER process() CALL in the
    // pre-slice block (section 2.5.5, D-8 clause 1).

    // 5. Output stage IN PLACE. ALWAYS LAST (seraphis_engine.h:617; FR-002).
    engine_->processOutputStage(wetL_.data(), wetR_.data(), n);

    if (effectsStageAfterOutput_) {        // D-7 capability 2, TEST PATHS ONLY
        runWanderStage(wetL_.data(), wetR_.data(), n);
    }
```

There is **no `fxPhase_`** (D-3): the reset trigger is the fill-chunk boundary inside `runSendStage`,
and nothing else needed an absolute counter. `controlPhase_ += n` at `processor.cpp:825` is unchanged and
remains the wander stage's absolute grid phase (§3.4).

### 2.6 `plugins/seraphis/src/controller/controller.cpp` — FR-037

Three one-line additions, mirroring the aether rows:
`registerEffectsParams(parameters);` after `:51`; `loadEffectsParamsToController(streamer, setParam);`
after `:100` (in `setComponentState`, in the same order `getState` writes); and
`if (formatEffectsParam(id, valueNormalized, string) == kResultOk) { return kResultOk; }` after `:133`.
`setComponentState`'s `version > kCurrentStateVersion` refusal (`:76`) needs no change — the constant
moved.

### 2.7 `plugins/seraphis/resources/editor.uidesc` — FR-036

16 `<control-tag>` entries appended inside `<control-tags>` (`:20-121`), under
`<!-- Effects (1400+) -->`, names `FxSaturation`, `FxDelayMix`, …, `FxAzimuthDepth`. **No layout
change** — Phase 11 owns layout, and a tag with no view is exactly what lets it add one without touching
this list (the file's own comment, `:18-19`).

### 2.8 `plugins/seraphis/src/processor/processor.cpp` state — FR-031, FR-032, FR-033, C-8

`getState` (`:943-981`): one line, **last**, after `saveAetherParams`: `saveEffectsParams(effectsParams_,
streamer);   // [effects] 64 B`. `setState` (`:866-931`): one line, **last**, after `loadAetherParams`:
`loadEffectsParams(effectsParams_, streamer);   // [effects] 64 B`. Both loaders are EOF-safe, so a v1 or
v2 stream simply stops before the block and every effects field keeps its C-6 default (FR-033) — **no
version-aware branch** (C-8). Stream size **2532 → 2596 bytes**.

---

## 3. Algorithms

### 3.1 The fixed-size send accumulator (C-2 clause 5, FR-003a)

**Why it exists.** `SpectralDelay::process` is not partition-invariant. `STFT::canAnalyze()` is
`samplesAvailable_ >= fftSize_` (`stft.h:134-137`), `analyze()` consumes `hopSize_` (`:171`),
`OverlapAdd::synthesize()` adds `samplesReady_ += hopSize_` (`:311`), and `process()` pulls
`toPull = min(numSamples, availableL, availableR)` (`spectral_delay.h:366`), writing `dryBuffer * dryMix`
— **silence at 100 % wet** — into everything it cannot supply (`:383-386`). At `fftSize` 1024 / hop 512 a
single 2048-sample call has three frames ready and lands wet sample 0 at output index 0; the same audio
as four 512-blocks lands it at index 512. A permanent one-hop offset, not a start-up transient.

**`clearFifos()` — ONE definition, and it re-establishes the invariant rather than breaking it.**
Written out here because it is the only thing that ever resets the accumulator, it has three call sites
(prepare, `setActive(false)`, and the deferred mid-render clear), and an implementation that zeroes the
counters instead of restoring the pre-fill **hangs the audio thread** — see the note under the invariant:

```cpp
/// Restores EXACTLY the state section 3.1's invariant is proved from:
/// inLen = 0, outLen = kFxSendChunkSamples. Any other post-clear state breaks
/// the proof. FR-003a's "the FIFOs are cleared only by FR-008's deferred
/// reset()" is about WHEN this runs, not about what it leaves behind.
void Processor::clearFifos() noexcept {
    std::fill(fxInL_.begin(),  fxInL_.end(),  0.0f);
    std::fill(fxInR_.begin(),  fxInR_.end(),  0.0f);
    std::fill(fxOutL_.begin(), fxOutL_.end(), 0.0f);
    std::fill(fxOutR_.begin(), fxOutR_.end(), 0.0f);
    fxInWrite_ = fxInRead_ = fxChunkFill_ = 0;
    fxOutRead_  = 0;
    fxOutWrite_ = fxOutFill_ = kFxSendChunkSamples;  // the one-chunk PRE-FILL
}
```

**Per-slice algorithm** (`runSendStage(l, r, n)`), executed only when `fxSendRuns_`:

```
clear:  // BEFORE anything else, so no partial chunk-loop state is ever live
        // across a clear. This is the ONLY mid-render call site.
        if (fxFifoClearDue_ || (fxResetDue_ && fxChunkFill_ + n >= kFxSendChunkSamples)):
            if (fxResetDue_) { spectralDelay_.reset(); ++fxResetCount_; fxResetDue_ = false; }
            clearFifos();  fxFifoClearDue_ = false
push:   for i in [0,n): fxIn[(fxInWrite_ + i) & MASK] = fxSendState_ == Active ? bus[i] : 0.0f
        fxInWrite_ += n; fxChunkFill_ += n           // FR-009a: SILENCE while draining
run:    while (fxChunkFill_ >= kFxSendChunkSamples):
            copy 512 from fxIn -> fxChunkL_/fxChunkR_;  fxInRead_ += 512; fxChunkFill_ -= 512
            spectralDelay_.process(fxChunkL_.data(), fxChunkR_.data(), kFxSendChunkSamples,
                                   fxBlockCtx_)                       // spectral_delay.h:315
            ++sendChunks_                                             // D-8 cl.2 / FR-007
            fxDrainPeak_ = max |fxChunk*|                             // FR-009a early exit
            copy fxChunk* -> fxOut;  fxOutWrite_ += 512;  fxOutFill_ += 512
mix:    assert(fxOutFill_ >= n)      // debug-only; see the underflow note below
        g = fxReturnGainSm_.getCurrentValue()         // smoother.h:191 - NOT process(); see 2.4
        for i in [0,n): bus[i] += fxOut[(fxOutRead_ + i) & MASK] * g
        fxOutRead_ += n;  fxOutFill_ -= n
end:    assert(fxChunkFill_ + fxOutFill_ == kFxSendChunkSamples)      // debug-only
```

**Where FR-008's deferred reset fires, and why here** (D-3). The `clear:` step is the reset's single
firing point: it runs at the **top** of the stage, on the first slice that will complete a fill chunk
(`fxChunkFill_ + n >= kFxSendChunkSamples`), i.e. on the next fill-chunk boundary. Placing it there —
rather than inside the `while` — is what makes it safe: inside the loop the guard
`fxChunkFill_ >= kFxSendChunkSamples` has already passed, so a clear that zeroes `fxChunkFill_` is
immediately followed by `fxChunkFill_ -= 512` on a `std::size_t`, which **wraps to ~2⁶⁴, keeps the loop
condition true forever, and calls `SpectralDelay::process()` without bound on the audio thread** — a hard
hang, not a glitch. The same argument applies to the seed burst (§2.5.4 item 2), which is why it only
raises `fxFifoClearDue_` and lets this step do the work.

**The return gain is read, not advanced, here.** `fxReturnGainSm_` is in `classBSmoothers()`, so
`advanceParamSmoothers()` already advanced it by `n` at `processor.cpp:821` before `renderSlice()` was
entered (`:824`). Calling `.process()` per sample would advance it a *second* time — 2n per n rendered
while the send runs and n while it does not — halving `kFxReturnRampMs` and making the ramp rate depend
on send state. See the invariant banner in §2.4.

**Invariant (proved, not asserted).** Let `inLen` be `fxChunkFill_` and `outLen` be `fxOutFill_` after a
slice. Initially `inLen = 0`, `outLen = 512` (the prepare-time pre-fill) — and, crucially,
**`clearFifos()` restores exactly that same initial condition**, so a clear *re-establishes* the
invariant instead of breaking it. One slice of `n` samples gives `inLen' = inLen + n`,
`k = floor(inLen'/512)` chunks, `inLen'' = inLen' − 512k`, `outLen'' = outLen + 512k − n`. Substituting
`outLen = 512 − inLen`:

> `outLen'' = 512 − inLen − n + 512k = 512 − inLen''`

so **`outLen + inLen == kFxSendChunkSamples` at every slice boundary**, `inLen'' < 512` ⇒ `outLen'' > 0`,
and the output FIFO can never underflow. The pipeline delay is therefore exactly **512 samples in every
partition** — the property SC-017 needs, and the reason FR-005 keeps it out of the reported latency
(it is a delay's own delay, absorbed into the delay time).

**Underflow is guarded, not merely proved.** `fxChunkFill_` and `fxOutFill_` are `std::size_t`, so a
violated invariant is not a glitch but a wrap to ~2⁶⁴ that silently invalidates every later occupancy
test and walks stale ring content into the bus. The two debug-only assertions above (the `fxOutFill_ >= n`
precondition at the mix step and the `inLen + outLen == 512` postcondition at the slice end) are the
gates; they cost nothing in Release and turn any future edit that breaks the proof into an immediate
Debug failure. The clear path is reachable in the shipping configuration — D-6 forces 64-sample
sub-slices for the whole of every engage/bypass ramp — so this is not a theoretical hazard.

**Capacity.** Peak input occupancy before the chunk loop is `511 + kMaxBlockSamples = 2559`; peak output
occupancy before the mix loop is `512 + 2048 = 2560`. `kFxFifoCapacity = 4096` (power of two → the ring
index is `& (kFxFifoCapacity − 1u)`, no modulo).

**RT safety.** Every buffer is a `setupProcessing()`-sized `std::vector` indexed through `.data()` /
`operator[]`, **never** `.at()` (`processor.cpp:1135-1137`). No allocation, no lock, no throw, no I/O.

### 3.2 Tilt-compensated feedback (FR-016a, C-7 clause 2, SC-005 clause 1)

`calculateTiltedFeedback(bin, numBins, fb, tilt)` (`spectral_delay.h:603-614`) is

> `clamp(fb · (1 + tilt·(b/(N−1) − 0.5)·2), 0, kMaxFeedback = 1.2f)`

The tilt factor spans `[0, 2]`. At `fb = 0.95, tilt = +1`, every bin with `b/(N−1) > 0.5263` — **243 of
513** at `fftSize` 1024 — has loop gain > 1, saturating at 1.2. The per-bin recursion is
`feedbackMag = tanh(delayedMag · binFeedback)` written back into that bin's own delay line
(`:751-767`); diffusion (`:617-637`) blurs the **input** spectrum only, so there is no cross-bin
coupling inside the loop and the scalar map `m ↦ tanh(g·m)` has a **stable non-zero fixed point** for
`g > 1` (at `g = 1.2`, `m* ≈ 0.69`). Those bins never decay.

Pushing `fb/(1+|tilt|)` makes the worst tilted bin `(fb/(1+|tilt|))·(1+|tilt|) = fb ≤ 0.95 < 1` at every
tilt, and is the identity at `tilt = 0`. `tanh(x) ≤ x`, so the realised decay is at least
`20·log₁₀(0.95) = −0.4455 dB` per traversal. At `kDefaultDelayMs = 250` ms and 120 s that is
**480 traversals = −213.8 dB**, comfortably past SC-005 clause 2's 60 dB. (At `kMaxDelayMs = 2000` ms it
is only −26.7 dB — which is exactly why SC-005 clause 2 **pins** the delay time.)

### 3.3 The send state machine (C-3, FR-007, FR-008, FR-009, FR-009a, FR-023a)

`updateEffectsBypassState()`, once per `process()` call (FR-012), `++bypassEvals_` exactly once:

```
mix        = effectsParams_.delayMix        // exact float; FR-007's predicate is == 0.0f
freezeOn   = effectsParams_.spectralFreeze
wantActive = (mix != 0.0f) || freezeOn                       // FR-023a forces the send active
fxEffectiveReturnGain_ = freezeOn ? std::max(mix, kFxFreezeMinReturnGain) : mix

switch (fxSendState_):
  Bypassed: if (wantActive):
                // FR-008: reset iff (a) bypassed longer than the drain window AND
                //                    (b) the engage was NOT freeze-forced.
                fxResetDue_ = (fxBypassedSamples_ > fxSendDrainSamples_) && !freezeOn;
                fxSendState_ = Active;  fxLiveSamplesSinceEngage_ = 0;
            else: fxBypassedSamples_ += blockSamples (saturating)
  Active:   if (!wantActive): fxSendState_ = Draining; fxDrainRemaining_ = fxSendDrainSamples_;
                              fxDrainPeak_ = 1.0f;   // ABOVE the floor - see below
            else:             fxLiveSamplesSinceEngage_ += blockSamples (saturating)
  Draining: if (wantActive): fxSendState_ = Active; fxLiveSamplesSinceEngage_ = 0;
            else if (fxDrainRemaining_ <= 0 || fxDrainPeak_ < kFxSendDrainFloor):
                              fxSendState_ = Bypassed; fxBypassedSamples_ = 0;
            else:             fxDrainRemaining_ -= blockSamples

fxSendRuns_ = (fxSendState_ != Bypassed)         // FR-007's exact prohibition predicate
```

`fxSendDrainSamples_` is a **member**, not a constant — it depends on the sample rate. It is computed
once in `setupProcessing()` as `std::llround(kFxSendDrainMs * 0.001 * sampleRate_)` (§2.5.3 5f) and
declared in §2.4.

**`fxDrainPeak_`'s lifetime is explicit** (§2.4 declares it, §2.5.3 initialises it to `1.0f`). It is
written only by the chunk loop (§3.1's `run:` step) and read only by the `Draining` arm's energy exit.
The `Active → Draining` transition **re-arms it above the floor**, because a send engaged and re-bypassed
inside a single chunk period runs no chunk at all: without the re-arm the predicate would read a stale
sub-floor value from a *previous* drain and terminate the new drain on its first block — precisely the
tail annihilation FR-008/FR-009a exist to prevent. The re-arm value `1.0f` is arbitrary except that it
must exceed `kFxSendDrainFloor`; the first chunk of the new drain overwrites it with a real measurement.

**What FR-007 forbids while `!fxSendRuns_`:** no `SpectralDelay::process`, no copy of the bus into
`fxIn*`, no read or write of any send buffer. Under D-3 there is no free-running counter to exempt —
`runSendStage` is simply not called. The prohibition is **observable**: `sendChunkCountForTest()`
(D-8 clause 2) must not advance, which SC-018 clause (e) asserts inside the CI-gated suite (§4.1).

**Why the drain is fed silence** and why the floor matters: the send's own per-bin feedback decays the
tail, so a bypass excursion's worst-case cost is bounded **by energy, not by wall clock** — at SC-011a's
operating point (feedback 0.6, delay 250 ms) the peak falls under `1e-6` after
`ln(1e-6)/ln(0.6) ≈ 27` traversals ≈ **6.8 s**… which is longer than the 2 s cap, so the cap fires
first there; at feedback 0.35 (the C-6 default) it is 13 traversals ≈ 3.3 s, again capped. The floor is
what protects the *low*-feedback and *short*-delay cases, and the cap protects the rest. Both are named
`inline constexpr` (FR-009a).

**Consequence for the test plan, and it is not optional.** At every operating point any *other* criterion
uses, the 2 s cap fires first — so an implementation that omits the floor check entirely would pass
SC-011a, SC-012, SC-013 and SC-018 unchanged, and FR-009a's load-bearing clause ("bounds the excursion's
cost **by energy rather than by wall clock**", `spec.md:718-722`) would be untested. §4.2 therefore adds a
row pinning a configuration where the floor provably fires first: **feedback 0.1, delay 50 ms** ⇒
`ln(1e-6)/ln(0.1) ≈ 6` traversals ≈ **0.3 s**, an order of magnitude inside the 2 s cap.

**Why the reset is conditional** (FR-008): `reset()` clears `wasFrozen_` / `freezeCrossfade_`
(`spectral_delay.h:276-277`) and the frozen spectrum buffers (`:256-257`) along with all
`4 × 513 = 2052` per-bin `DelayLine`s (`:259-273`), and re-randomizes `2 × numBins` phases (`:279-284`).
An unconditional reset would annihilate the tail and any captured freeze on **every** automation curve
that merely touches zero — and the FR-007 predicate is exact `== 0.0f`, so a bipolar LFO crosses it
twice per cycle.

### 3.4 The wander stage (C-5, FR-006, FR-010, FR-010a, FR-024, FR-024a)

`runWanderStage(l, r, n)`. The bypass predicate is hoisted (FR-012), and it has **two** parts — the raw
predicate and the disengage latch:

```
fxWanderRuns_ = !(width == 100.0f && wanderDepth == 0.0f && azimuthDepth == 0.0f)   // RAW atomics
fxWanderRunsEffective_ = fxWanderRuns_ || !wanderAtIdentity()
```

where

```cpp
/// TRUE only when every wander smoother has actually reached identity, so the
/// stage can be skipped without stepping the image.
[[nodiscard]] bool Processor::wanderAtIdentity() const noexcept {
    return fxWanderSettleRemaining_ <= 0                                          // see below
           && globalMs_.getWidth() == Krate::DSP::MidSideProcessor::kDefaultWidth // :236, :67
           && azimuthGainLSm_.isComplete() && azimuthGainRSm_.isComplete()        // smoother.h:232
           && azimuthGainLSm_.getCurrentValue() == 1.0f
           && azimuthGainRSm_.getCurrentValue() == 1.0f
           && fxWanderDepthSm_.isComplete() && fxWanderDepthSm_.getCurrentValue() == 0.0f
           && fxAzimuthDepthSm_.isComplete() && fxAzimuthDepthSm_.getCurrentValue() == 0.0f;
}
```

The plugin-side smoothers expose completion directly (`OnePoleSmoother::isComplete()`,
`primitives/smoother.h:232`), but `MidSideProcessor` exposes only `getWidth()` (`:236`) — the *target*,
not its internal `widthSmoother_`'s progress — and Non-goals forbid adding a `dsp/` accessor. The
component's own constant closes the gap: its smoothers are configured at
`kDefaultSmoothingMs = 10.0f` ms (`midside_processor.h:73`, `:101-105`). So the latch carries a plain
sample countdown, `std::int64_t fxWanderSettleRemaining_`, re-armed to the member
`fxWanderSettleSamples_` — three time constants of the slower of
`MidSideProcessor::kDefaultSmoothingMs` and `kParamSmoothMs`, computed once in `setupProcessing()`
(§2.5.3 5f) — on **every** block in which the raw predicate is true, and decremented by `blockSamples` on
every block in which it is false. `wanderAtIdentity()` cannot return true until it has run out, which
bounds the disengage tail at a **stated** number of samples instead of a guess. Both members are declared
in §2.4.

**Why the latch exists (FR-010a's DISENGAGE arm).** FR-010a puts **both** wander-bypass transitions in
SC-008's scope (`spec.md:733-738`), and D-4 repeats it — but only the engage arm is a snap. The raw
predicate is evaluated on the unsmoothed atomics, so the instant a host writes
`kFxWanderDepthId = 0` the stage would be skipped **on that very block**, while `globalMs_`'s internal
`widthSmoother_` still holds the last modulated width (it advances only inside `process`,
`midside_processor.h:186-192`), `azimuthGainLSm_/RSm_` still hold their last non-unity gains, and
`fxWanderDepthSm_` is still mid-ramp. Skipping applies exact identity, so the stereo image **steps in one
sample** — reintroducing at the disengage edge exactly the lag this section rejects
`BrownianDrift::setDepth()` over. The latch keeps the stage running until everything has settled, then
skips.

**SC-002 is unaffected:** at the C-6 defaults the raw predicate is false and every smoother is already at
identity from `setupProcessing()`, so `fxWanderRunsEffective_` is false from the first block and the
latch never arms. The skip remains bit-exact on the default patch.

`fxWanderSettleRemaining_` and `kFxWanderSettleSamples`'s sample-rate-dependent value are declared beside
the other send/wander state in §2.4 and initialised in §2.5.3 5f. `runWanderStage`'s early-return
predicate is `fxWanderRunsEffective_` — **not** `fxWanderRuns_`; the raw predicate is used only to arm and
re-arm the latch. §2.5.6 calls `runWanderStage` unconditionally and the skip is taken inside, exactly as
`runSendStage`'s is.

**The skip is MANDATORY, not an optimisation.** Running `MidSideProcessor` at width 100 % is an
*algebraic* identity — `mid = (L+R)·0.5`, `side = (L−R)·0.5` (`midside_processor.h:200-201`),
reconstructed as `mid ± side` (`:225-226`) — but **not** an IEEE-754 bit identity: each of those three
operations rounds, so e.g. `L = 1.0f, R = 2⁻³⁰` reconstructs `R_out = 0.0f`. SC-002 asserts
`max|a−b| == 0.0f`.

**FR-010a's ENGAGE arm**, on `!fxWanderWasActive_ && fxWanderRunsEffective_` (`fxWanderWasActive_` is
assigned `fxWanderRunsEffective_` at the end of every stage call): `globalMs_`'s width smoother
does **not** advance while the stage is skipped (it advances only inside `process`,
`midside_processor.h:186-192`), so the processor pushes the current width and **snaps**:

```cpp
    globalMs_.setWidth(currentWidth);   // :133 - sets the smoother TARGET
    globalMs_.reset();                  // :114 - snapTo on all five smoothers
    azimuthGainLSm_.snapTo(1.0f);  azimuthGainRSm_.snapTo(1.0f);   // then ramp in over 20 ms
```
`reset()` also snaps mid gain / side gain / both solos, all of which Seraphis never moves off their
prepared defaults, so the extra snaps are no-ops.

**Body**, looping over the absolute 64-sample control grid (`SeraphisEngine::kControlChunkSamples`,
`seraphis_engine.h:213`; the same grid `aether_reverb.h:1384-1386` names). The absolute position of
sample `s` inside the slice is `controlPhase_ + s`, because `controlPhase_` is incremented **after**
`renderSlice` (`processor.cpp:825`):

**The loop is INTERLEAVED with the audio, and it must be.** An earlier revision of this section ran the
whole control loop first and called `globalMs_.process(l, r, l, r, n)` plus the azimuth multiply once
*after* it. That shape delivers nothing: `MidSideProcessor::setWidth()` only stores `width_` and calls
`widthSmoother_.setTarget()` (`midside_processor.h:133-136`) and `OnePoleSmoother::setTarget()` only
stores a target (`primitives/smoother.h:170`), so **every iteration but the last is overwritten before a
single sample is touched** — and the iterations compute identical values anyway, because
`widthDrift_`/`azimuthDrift_` are advanced once per `process()` call in the pre-slice block (§2.5.5) and
`getCurrentValue()` (`brownian_drift.h:212`) is a pure read, while `fxWanderDepthSm_`/`fxAzimuthDepthSm_`
are advanced by `advanceParamSmoothers()` *outside* `renderSlice()` (`processor.cpp:821`). The net grid
would have been one update per **slice**, and slices reach `kMaxBlockSamples = 2048` whenever no
class-(b) smoother is unsettled (`processor.cpp:791-815`) — a ~43 ms grid at 48 kHz in the steady wander
state, not 64 samples. C-5's and FR-024's "at most once per 64-sample control chunk, on the same absolute
grid the engine and the reverb already use" would have been a claim the code did not deliver.

```
off = 0
while off < n:
    chunkLen = min(64 - ((controlPhase_ + off) % 64), n - off)   // ABSOLUTE grid phase

    // --- width: pushed per control chunk; the component smooths per sample -----
    d_w   = widthDrift_.getCurrentValue()                    // brownian_drift.h:212, range [-1,1]
    depthW= fxWanderDepthSm_.getCurrentValue()               // ID 1441, class (b) - READ ONLY (2.4)
    width = clamp(base + depthW * d_w * kWanderWidthSpanPercent,
                  MidSideProcessor::kMinWidth, MidSideProcessor::kMaxWidth)   // :65-66
    globalMs_.setWidth(width)                                // :133

    // --- azimuth: ONE cos/sin pair per control chunk; per-sample transcendentals
    //     are FORBIDDEN (C-5, FR-024).
    d_a   = azimuthDrift_.getCurrentValue()
    depthA= fxAzimuthDepthSm_.getCurrentValue()              // ID 1443, class (b) - READ ONLY
    pos   = clamp(0.5f + 0.5f * depthA * d_a, 0.0f, 1.0f)
    float gl, gr;  Krate::DSP::equalPowerGains(pos, gl, gr)  // crossfade_utils.h:50
    azimuthGainLSm_.setTarget(gl * kFxAzimuthCentreComp)     // plan D-4
    azimuthGainRSm_.setTarget(gr * kFxAzimuthCentreComp)

    // --- THE AUDIO FOR THIS SUB-CHUNK, before the next control update ----------
    globalMs_.process(l + off, r + off, l + off, r + off, chunkLen)  // IN PLACE, :181
    for i in [off, off + chunkLen):
        l[i] *= azimuthGainLSm_.process();  r[i] *= azimuthGainRSm_.process();

    off += chunkLen
```

`azimuthGainLSm_`/`azimuthGainRSm_` are **not** in `classBSmoothers()` — they are plugin-local ramps with
no ParamID, advanced only here — so `.process()` per sample is their sole advance and the §2.4 invariant
is not violated. `controlPhase_` is incremented after `renderSlice()` returns (`processor.cpp:825`), so
`controlPhase_ + off` is the correct absolute position of sample `off` inside this slice, and the grid is
the same one D-6's subdivision rule already aligns slices to (`:811-815`).

**Why depth is a plugin-side multiply and not `BrownianDrift::setDepth()`** (FR-024a): `setDepth`
(`:159`) feeds `outputTarget()` (`:249-251`), which is behind the drift's own
`kDriftOutputSmoothMs = 150 ms` output smoother (`:103`, `:126-127`). A depth of exactly 0 would still
be emitting a decaying non-zero for ~150 ms, so FR-010's skip — which SC-002 requires to be **exact** —
could not be taken on the block the host wrote the value. `setDepth` is left at its prepared value on
both drifts.

**Why `setMean(0.0f)` is pushed rather than assumed** (FR-024a): the mapping's symmetry about `base`
and about centre is load-bearing for the Edge cases (at `base = 0` the wander can only widen, at
`base = 200 %` only narrow, and in neither case can the clamped push leave
`[kMinWidth, kMaxWidth]`). `mean_` already defaults to `0.0f` (`brownian_drift.h:281`); the explicit
push makes that a stated property rather than an inherited one.

**Span justification** (C-5): `BrownianDrift` is bipolar `[-1, +1]` with stationary
`kInternalStd = 0.5` (`:101`), so at depth 1 the walk normally stays inside about ±0.5. A span of 50
puts width typically in **75–125 %** with clamped extremes at **50–150 %** — the roadmap's "bounded,
slow, and smooth" (line 76) — and it can never collapse the bus toward mono on a random excursion. A
span of 100 would reach 0–200 % in the tails and read as an unstable image.

### 3.5 The two freezes are independent (C-4, FR-023)

Nothing in `pushEffectsParams()` reads `aetherParams_.freeze` and nothing in `pushAetherParamsIfDirty()`
(`:1478`) reads `effectsParams_.spectralFreeze`. ID 1204 latches the FDN to unity feedback
(`aether_reverb.h:2230`, 50 ms latch); ID 1430 **captures** the current spectrum of the send input at
engagement (`spectral_delay.h:677-688`) and crossfades it in over `kFreezeCrossfadeTimeMs = 75 ms`
(`:906`), whose per-frame increment is derived at `:210-212` — `hopSize / (0.075 · sr)` ≈ **0.142 per
frame, i.e. ~7 hops**, not one — with a slow phase drift once fully frozen (`:698-706`) so it does not
read as a static resonance. Because the send is fed the **post-Aether** bus (C-1 step 4), the captured
spectrum *is* the Aether tail (roadmap line 466). Labels in `editor.uidesc` keep them distinguishable:
`AetherFreeze` (1204) vs `FxSpectralFreeze` (1430).

---

## 4. Test plan

New TUs: `plugins/seraphis/tests/integration/effects_chain_test.cpp`,
`plugins/seraphis/tests/integration/effects_perf_test.cpp`,
`plugins/seraphis/tests/unit/state_v3_test.cpp`. Existing TUs grown:
`unit/param_denorm_test.cpp`, `unit/processor_bus_test.cpp`, `unit/parameter_surface_test.cpp`,
`unit/lifecycle_test.cpp` (FR-035), `unit/controller/editor_lifecycle_test.cpp`,
`integration/param_cadence_test.cpp`,
`integration/param_continuity_test.cpp`, `integration/param_perf_test.cpp`. **No new executable**
(FR-039) — which is exactly why SC-002 and SC-012 are same-binary runtime A/B arms driven by FR-040's
probe, not two-build comparisons.

`detail::SeraphisEffectsStageBypassProbe` is **defined in `effects_chain_test.cpp` only**, in the shape
of `param_continuity_test.cpp:111-119`, with an RAII guard so a failed assertion cannot leave a mode set
for the rest of the suite.

### 4.1 Success criteria → test

**Two rules that apply across the table, stated once.**

1. **Every tap-measuring criterion renders with host blocks ≤ `kMaxBlockSamples` (2048) and asserts
   `!preOutputTapTruncatedForTest()`.** That is SC-003(b), SC-005, SC-007, SC-011a and SC-019, plus the
   FR-004 and FR-010/D-4 rows in §4.2. FR-041 pins the tap buffers to 2048 (`spec.md:993-995`) while the
   processor supports larger host blocks (`processor.cpp:787-792`), so on a 4096-sample call the second
   slice's copy is skipped; without both halves of this rule the criterion would silently measure half a
   render. (SC-017 *is* required to render one oversized 4096 call — it compares plugin output through
   `render_fingerprint.h`, not the tap, so it is unaffected.)
2. **The isolated-send-return definition is SC-003's** (`render(mix = 1) − render(mix = 0)` at the tap)
   **everywhere except SC-007 arm (a)**, which pins `kFxDelayMixId` at its default and differences on
   `kFxSpectralFreezeId` instead. See that row for why.

| SC | File | `TEST_CASE` | Assertion strategy |
|---|---|---|---|
| SC-001 | `unit/param_denorm_test.cpp` | `"Seraphis effects parameters denormalize"` | `getParameterCount() == 107`; per-ID `ParameterInfo` type/min/max/default/stepCount against a checked-in 16-row table; normalized→plain→normalized round-trip over `{0, .25, .5, .75, 1}` within `1e-6`; every dropdown index yields a distinct non-empty string; **literal label arrays asserted element-by-element** (D-1's ruled set: `1/64T … 1/8T`), and ID 1419's registered default index asserted to be **7**. |
| SC-001a | `integration/param_continuity_test.cpp` | existing cases, tables grown | `static_assert(std::size(kContinuityMechanism) == 101)`, `static_assert(std::size(kClassBIds) == 12)`; `CHECK(table == expected)` at `:751` with all 16 rows and none stray; every effects row's `citation` non-empty and containing `':'` (gate at `:728-733`); **the shipped time-constant gate, stated correctly:** `:772` admits exactly `kBodySmoothMs` **or** `kDepthSmoothMs` on class-(b) rows (`CHECK((row.smoothMs == kBodySmoothMs \|\| row.smoothMs == kDepthSmoothMs))`, where `kBodySmoothMs = Seraphis::kParamSmoothMs` at `:173` and `kDepthSmoothMs = Seraphis::kAetherDepthSmoothMs = 300 ms` at `:174`) and `== 0.0f` on class-(a) rows — **not** "class-(b) == kParamSmoothMs", which would break the seven existing 300 ms rows. The three new class-(b) rows (1410, 1441, 1443) carry `kBodySmoothMs`, and the per-ID pin at `:781-793` (which today names 100-104/1215/1216 → `kDepthSmoothMs` and 801/802 → `kBodySmoothMs`) gains the three IDs alongside 801/802. No effects ID in `kExemptIds`; the per-ID sweep (`:822-826`) then measures all 16 at the unchanged 1.5× bound. |
| SC-002 | `effects_chain_test.cpp` | `"Effects defaults are a no-op on the same build"` | **One build, one process, one `Processor`.** Two 10 s renders (8 voices, everything at shipped/C-6 defaults), probe engaged vs disengaged. `REQUIRE(maxAbsDiff == 0.0f)` over both channels. Exact equality is legitimate *here only* because both sides are the same compiled path on the same instance. |
| SC-003 | `effects_chain_test.cpp` | `"Effects chain order matches C-1"` | Isolated return = `render(mix=1) − render(mix=0)`, both read from `preOutputTapForTest()` concatenated per block. (a) true plugin output, precondition `peak(tap) > kLimiterCeilingLin` (`param_flow_test.cpp:63`), assert output peak ≤ ceiling + `kCeilingAllowanceDb`; **positive control**: probe runs step 5 after step 6 ⇒ must fail. (b) doubling master gain scales isolated-return RMS by `6.02 ± 0.1 dB`. (c) isolated-return M/S side energy monotone in `kFxWidthId ∈ {0, 100, 200}` and within 0.5 dB of ideal. |
| SC-004 | `unit/processor_bus_test.cpp` | `"Phase 10 does not change reported latency"` | `getLatencySamples()` identical before/after driving all 16 IDs off-default, and `== reverb_->getLatencySamples()` in every case. `kFxDelayMixId` swept 0→1→0 produces **zero** `restartComponent` calls (the processor has no `IComponentHandler` at all — `processor.h:329-339` — so this is asserted as "no such call site exists": a `grep`-style compile-level check plus the constant-latency assertion). |
| SC-005 | `effects_chain_test.cpp` | `"Spectral delay decays at registered max feedback"` | (1) arithmetic, no render: over `tilt ∈ {−1, −0.5, 0, +0.5, +1}` and `fb = 0.95`, `max over 513 bins of clamp(tiltCompensatedFeedback(fb,tilt)·(1 + tilt·(b/512 − 0.5)·2), 0, 1.2f) < 1.0`. (2) two runs (tilt −1, +1), feedback 0.95, diffusion 1.0, **delay pinned at 250 ms**, spread 0: 1 s burst + 120 s silence ⇒ isolated-return RMS falls ≥ 60 dB below peak. (3) 5 s windows after the first 5 s: no window's RMS rises > 0.5 dB above its predecessor. Non-finite checked **by bit pattern**, never `std::isnan`. |
| SC-006 | `effects_chain_test.cpp` | `"Effects at maxima respect the true-peak ceiling"` | 30 s, all 16 at maxima, master gain max, 8 voices held: no **raw output sample** exceeds `kLimiterCeilingLin = 0.8912509f` with `kCeilingAllowanceDb = 0.1f`. Deliberately **not** an independently written 4× reconstruction (`true_peak_limiter.h:38-42`, `:110-125`). |
| SC-007 | `effects_chain_test.cpp` | `"Spectral freeze holds the Aether tail"` | Both arms read `preOutputTapForTest()` (assert `!preOutputTapTruncatedForTest()`), but they use **two different difference definitions**, and the split is normative. **(a) has its own definition:** `kFxDelayMixId` is held at its C-6 default **0 throughout**, and the isolated quantity is `render(kFxSpectralFreezeId = on) − render(kFxSpectralFreezeId = off)`. SC-003's mix-differenced definition **does not apply to arm (a)** and applies to arm (b) only. Reason: arm (a)'s whole subject is the shipped-default configuration `mix = 0` (`spec.md:1154`), and SC-003's definition *mutates the very parameter the arm pins* — it would render one side at mix 1 (return gain 1.0) and the other at mix 0 (return gain forced to `kFxFreezeMinReturnGain = 0.5` by FR-023a) and report 0.5× the send, so a build in which FR-023a's forced engage worked **only when mix > 0** — the exact defect FR-023a exists to prevent — would still show a non-zero difference and pass "RMS > −60 dBFS". Assertions, arm (a): engaging 1430 alone ⇒ RMS 5 s after note-off `> −60 dBFS` and within `±1.0 dB` of RMS at 200 ms after engagement — the arm that proves FR-023a **and** plan D-5. **(b)** uses SC-003's definition: `mix = 1`, held chord, RMS 5 s after note-off within `±1.0 dB` of RMS at 200 ms after engage (measurement point deliberately > `kFreezeCrossfadeTimeMs = 75 ms`), centroid moves < 5 %; freeze off ⇒ same measurement decays ≥ 30 dB. Both assert capture happens at *engage*, not at toggle. |
| SC-008 | `effects_chain_test.cpp` | `"Effects transitions are click-free"` | Six transitions. Statistic `maxPerSampleDelta` over a ±10 ms window centred on `event + 1024 (reverb) + 512 (accumulator) + 1024 (fftSize)` — **output domain**. One 20 ms reference window per measured transition, from the same render, ≥ 50 ms clear of any transition, uniformly spaced, same number of draws both sides. Bound `max(test) ≤ 1.5 × max(reference)`. Non-finite by bit pattern. **Positive controls (both mandatory):** (a) injected one-sample step of 2× the window's own delta must exceed the bound; (b) probe snapping the return-gain ramp to instant must fail clause 3. |
| SC-009 | `unit/state_v3_test.cpp` | `"Seraphis_StateVersion3_RoundTripsAndMigrates"` | (a) 16 all-non-default values through `getState`/`setState`, every field exact. (b) checked-in **v2** blob loads, effects fields at C-6 defaults, rest at the blob's values. (c) checked-in **v1** blob still loads. (d) v3 bytes **from offset 4** have v2's bytes from offset 4 as a strict prefix, and the two differ **only** in the leading `int32` (the version is written first, `processor.cpp:950`, and read first, `:877-878`). Blob helpers modelled on `state_v2_test.cpp:749-800`. Stream size 2532 → **2596**. |
| SC-010 | `effects_chain_test.cpp` | `"Effects renders are seed-deterministic"` | Two **independently heap-allocated** `Processor` instances (two `ProcessorFixture`s — `proc` is a `unique_ptr`, `seraphis_test_fixture.h:165`), same 20 s seeded sequence, same process: `render_fingerprint.h` tolerances (`kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`). **The render configuration is PINNED and includes a live wander:** send active *and* freeze exercised *and* `kFxWidthId` off 100 % *and* `kFxWanderDepthId > 0` *and* `kFxAzimuthDepthId > 0`. Without the wander clause this criterion — the spec's **only** coverage of FR-026 — proves nothing about the drifts: at the C-6 defaults `kFxWanderDepthId = kFxAzimuthDepthId = 0` (`spec.md:576`, `:578`), so FR-010's mandatory skip removes the whole stage from the signal path and both `BrownianDrift`s contribute nothing to the output; a build that never calls `widthDrift_.setSeed`/`azimuthDrift_.setSeed` would produce identical fingerprints and pass. Two clauses beyond the round-trip: (i) two different `kSeedId` indices ⇒ relative aggregate-metric difference **> 100 × kMetricTolerance** (1e-3); (ii) **salt-swap control** — with the seed index held fixed, exchanging `kFxWidthDriftSalt` and `kFxAzimuthDriftSalt` must change the fingerprint by **> 100 × kMetricTolerance**, which is what makes identical-salt lockstep (forbidden by C-5 / FR-024a clause 3) observable at all. This is *also* the negative control for FR-027 — on one instance `this` is constant, so a build that never calls `seedRng()` would still reproduce itself. **No bit-exact golden.** |
| SC-011 | `effects_perf_test.cpp` | `"Effects stage is RT-safe"` | `allocation_detector.h` (`tests/test_helpers/`): zero allocations/locks/exceptions over 60 s toggling every bypass predicate 100× **and** automating `kSeedId` across ≥ 16 index changes. Over ≥ 16 events of each kind: worst block containing `spectralDelay_.reset()` and worst containing `seedRng()+reset()` each ≤ **533 333 ns**; every other block ≤ **266 667 ns**; medians recorded in the banner. `[.perf]`. |
| SC-011a | `effects_chain_test.cpp` | `"A short mix excursion preserves the send tail"` | Isolated return at the tap. mix 1.0, feedback 0.6, delay 250 ms, captured freeze: excursion to exactly 0 and back over **200 ms** ⇒ RMS 500 ms after re-engage within **±2.0 dB** of RMS 500 ms before, frozen centroid within **5 %**. **Control:** excursion **longer** than `kFxSendDrainMs` ⇒ RMS falls ≥ 30 dB and rebuilds. |
| SC-012 | `effects_perf_test.cpp` | `"Effects cost nothing at defaults"` | `effectsStageNsForTest() / effectsStageProcessCallsForTest()` (D-8 clause 1 — the divisor is `process()` **calls**, not slices; the case additionally asserts the divisor equals the number of `process()` calls the harness itself made, so the two can never drift again) ≤ **10 667 ns/block** at 8 voices held, Phase 9's SC-009 MIDI script, 48 kHz, 512 blocks, all 16 at C-6 defaults. Worst-of-seven best-of-16 under the SC-013 protocol. Explicitly **not** a whole-render delta (the live cold dataset's own spread is 107 420 ns, `param_perf_test.cpp:148-156` — 10× this threshold). `[.perf]`. |
| SC-013 | `effects_perf_test.cpp` | `"Effects stage stays inside its 2.5 % budget"` | Same seam and the same `effectsStageProcessCallsForTest()` divisor with the same harness-call-count assertion (D-8 clause 1 — load-bearing here, because this row's render is *required* to carry an automation point, which by D-6 forces up to 8 sub-slices per 512-sample block and a per-slice divisor would under-report by that factor). Full-poly operating point **with voices sounding**, all 16 at maxima: ≤ **266 667 ns/block**. Per-run table transcribed under a BASELINE PROVENANCE banner in the shape of `param_perf_test.cpp:65-84`. The render must include an automation point on 1410/1441/1443 so D-6's subdivision cost is inside the measured figure. `[.perf]`, out of the CI gate. |
| SC-014 | `integration/param_perf_test.cpp` | existing SC-009 arm | `kNonDefaultTable` grown to **107** rows; `static_assert(kNonDefaultTable.size() == 107, "SC-009: the table is EXHAUSTIVE over the 107-parameter surface")` **with the message text updated**; `countRows(...)` at `:904-910` updated for any effects row declared `CoincidesWithDefault`; `idsStrictlyIncreasing()` (`:916-924`) still holds (1400 > 1217 ✓). Gate unchanged: `kFullPolyCeilingNs = 2 666 666.7` and `kBaselineFullPolyNs (2 318 840) × 1.15`. Neither is a lever. |
| SC-015 | — | — | `node tools/check-portability.js`; zero warnings on MSVC/GCC/AppleClang; `./tools/run-clang-tidy.ps1 -Target seraphis` and the `.sh` form clean. |
| SC-016 | — | — | `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`; `editor_lifecycle_harness.h` 10 open/close cycles. |
| SC-017 | `effects_chain_test.cpp` | `"The effects send is block-size invariant"` | mix 1.0, feedback 0.6, delay 250 ms, wander engaged, **no bypass/freeze/seed transition**; 4 s delivered as blocks **1, 2, 3, 7, 512, 2048 and one oversized 4096 call** vs one contiguous render, `render_fingerprint.h` tolerance. **Negative control:** the same case with the accumulator bypassed (send fed the raw slice length) must **fail**. |
| SC-018 | `integration/param_cadence_test.cpp` | `"Effects push cadence"` | (a) `spectralDelayResetCountForTest()` +1 per qualifying bypassed→active transition, +0 otherwise (incl. freeze-forced engage and a sub-drain excursion); (b) `widthDriftBlockCountForTest()` == `process()` block count under **both** bypass states; (c) `bypassPredicateEvalCountForTest()` == `process()` **call** count over a render whose blocks carry several MIDI slices — not the slice count; (d) driving any of 1410–1443 leaves `applyVoiceParamsCallCountForTest()` (`processor.h:172`) and `applyAetherParamsCallCountForTest()` (`:186`) unmoved; driving 1400 moves neither; **(e) FR-007 (new, D-8 clause 2):** over a render held entirely at the C-6 defaults `sendChunkCountForTest()` stays **0**, and over a render that engages and later bypasses the send it advances **only** while the send is active or draining — the last increment lands no later than the block on which `fxSendState_` returns to `Bypassed`. This is what moves FR-007 into the CI-gated suite; SC-012's `[.perf]` threshold is outside the gate (`spec.md:1025-1026`) and SC-002 cannot see the violation at all, because at mix 0 the mix loop adds `fxOut[i] * 0.0f` and a fully-running send still leaves the bus bit-identical. |
| SC-019 | `effects_chain_test.cpp` | `"Synced delay tracks host tempo"` | `kFxDelaySyncId` on, two indices, tempi 90 and 140 via `ProcessorFixture::setTempo(bpm, 4, 4, true, true)` (`seraphis_test_fixture.h:233`): measured echo period of the isolated return matches `Krate::DSP::dropdownToDelayMs(index, tempo)` within **±512 samples**. Then `clearProcessContext()` (`:254`), and a context present with `kTempoValid` **clear** but a **stale non-zero** `tempo` — both must fall back to the **120 BPM** period. That last clause is what discriminates FR-030's three-part guard from the component's weaker `tempo <= 0.0` check (`spectral_delay.h:325-327`). |

### 4.2 Functional requirements with no dedicated criterion

| FR | Covered by |
|---|---|
| FR-004 (`setDryWetMix` before `prepare`) | `effects_chain_test.cpp` — a dedicated `SECTION` asserting the isolated return contains **no** current-block dry: render an impulse at `mix = 1` and assert the first `fftSize + kFxSendChunkSamples` output samples of the isolated return are < `1e-7`. A post-prepare push leaks ~50 % dry on frame 0 and fails. |
| FR-007 | SC-018 clause (e) — `sendChunkCountForTest()` (D-8 clause 2). |
| FR-009a's `kFxSendDrainFloor` | `effects_chain_test.cpp` — a dedicated `SECTION` at a **low-feedback / short-delay** operating point where the floor provably fires first: **feedback 0.1, delay 50 ms** ⇒ the peak falls under `1e-6` after `ln(1e-6)/ln(0.1) ≈ 6` traversals ≈ **0.3 s**, an order of magnitude inside `kFxSendDrainMs = 2000`. Assert via `sendChunkCountForTest()` that the send stops producing chunks **before 2 s of drain has elapsed** (chunk count frozen by ~0.5 s after bypass) and that `fxSendState_` has returned to `Bypassed`, observable as `sendChunkCountForTest()` no longer advancing. **This is the only place the floor is the discriminator** — at every other configuration the plan uses (§3.3's arithmetic: 6.8 s at SC-011a's feedback 0.6, 3.3 s at the C-6 default 0.35) the 2 s cap fires first, so an implementation that omitted the floor check entirely would pass SC-011a, SC-012, SC-013 and SC-018 unchanged. |
| FR-010 / plan D-4 (azimuth centre normalisation) | `effects_chain_test.cpp` — a `SECTION` at the tap (blocks ≤ 2048): with a steady held chord, stepping `kFxAzimuthDepthId` from **0 to a small ε** (which crosses FR-010's skip boundary) must change broadband RMS by **< 0.1 dB**. An uncompensated `equalPowerGains` pair fails this by **3.01 dB**; it is the property the skip boundary actually needs, and it is what makes D-4's constant measured rather than asserted. |
| FR-011 / FR-012 / FR-019 | SC-018 (b), (c), (d). |
| FR-014 / FR-015 / FR-017 / FR-020 | SC-001 + the `static_assert`s in §2.2. |
| FR-016a | SC-005 clause 1 (arithmetic) + a unit `SECTION` asserting `Seraphis::tiltCompensatedFeedback(0.95f, ±1.0f) == 0.475f`. The helper is reached through `parameters/effects_params.h` (§2.3), which is why it is `inline constexpr` in a header and not a `.cpp`-local `constexpr`. |
| FR-022 | `effects_chain_test.cpp` — a `SECTION` per setter: drive the ID off-default, run one `process()` call, and assert the corresponding `SpectralDelay` getter reports the pushed value — `getBaseDelayMs()` (`spectral_delay.h:429`), `getSpreadMs()` (`:436`), `getSpreadDirection()` (`:442`), `getFeedback()` (`:464`, against the **tilt-compensated** value), `getDiffusion()` (`:493`), `getStereoWidth()` (`:516`), `getTimeMode()` (`:527`), `getNoteValue()` (`:535`). Plus a **repeated-identical-write** case: writing the same value again must leave `effectsPushCountForTest()` unmoved (FR-022's "on change only"). Without this row a build that never calls `setSpreadMs`/`setDiffusion`/`setStereoWidth`/`setSpreadDirection` passes every criterion in §4.1 — SC-006 drives those IDs to maxima but asserts only the output ceiling, and SC-014 measures only wall time. This is also what finally consumes `effectsPushCountForTest()`, which FR-041 clause 3 declares for exactly this purpose (`spec.md:989`) and no SC reads. |
| FR-023 (independence) | `effects_chain_test.cpp` — a `SECTION` driving **1204** and asserting `spectralDelay_`'s freeze state is unmoved (observed through the isolated return: with 1430 off, toggling 1204 must not produce a held send tail), and the converse — driving **1430** must not move the Aether freeze (the reverb's own tail still decays). Neither push path reads the other's atomic; the test is what keeps that true. |
| FR-024 (control cadence) | `effects_chain_test.cpp` — a `SECTION` that renders one 2048-sample block with the wander live and asserts the azimuth gain pair took **at most `ceil(2048/64) = 32`** distinct target values, i.e. the `cos`/`sin` pair was evaluated at most once per 64-sample control chunk and **not** once per slice. This is the observable form of §3.4's interleaving fix; the pre-fix shape would report **one**. |
| FR-024a | The `static_assert`s in §2.4 (`kWanderWidthSpanPercent`, the salt inequality, the azimuth smoothers' `kParamSmoothMs`) + a `SECTION` asserting both drifts report `getMean() == 0.0f` (`brownian_drift.h:171`) after `setupProcessing()`, and that `kFxWanderDepthId`/`kFxAzimuthDepthId` never reach `BrownianDrift::setDepth()` — `getDepth()` (`:170`) stays at its prepared value across a full 0→1→0 sweep of both IDs, and driving either to 0 must make FR-010's skip take effect on **the same block** (which a 150 ms `kDriftOutputSmoothMs` path cannot do), measured as bit-exact identity against a probe-bypassed render. Salt distinctness is measured by SC-010's salt-swap clause. |
| FR-025 | `effects_chain_test.cpp` — a `SECTION` moving **1442** and asserting **both** `widthDrift_.getSmoothness()` and `azimuthDrift_.getSmoothness()` (`brownian_drift.h:169`) report the pushed value. A build that pushes only one drift is otherwise invisible. |
| FR-029 | SC-011's allocation harness. |
| FR-034 | `unit/state_v3_test.cpp` — call `setupProcessing()`, then `setState()` with an **all-non-default** effects blob, then render one block, then assert the **component getters** (`getBaseDelayMs`, `getFeedback`, `getDiffusion`, `getStereoWidth`, `getSpreadDirection`, `getTimeMode`, `getNoteValue`, and `globalMs_.getWidth()`) equal the blob's values. SC-009 alone cannot cover FR-034: it compares parameter values read back out of the `EffectsParams` atomics, so a build that loads state into the atomics and never re-pushes them to the DSP passes SC-009 while the loaded patch renders with prepare-time defaults. This row asserts the **push** happened, not just the atomic write. |
| FR-035 | `unit/lifecycle_test.cpp` (registered in §5, sequenced in §7 step 10) — after `setActive(false)`/`setActive(true)`, a render from the same script equals a fresh-`setupProcessing()` render within `render_fingerprint.h` tolerance, with explicit clauses for each of FR-035's four named subjects: the send (`sendChunkCountForTest()` restarts from the cleared FIFO state), the M/S stage (`globalMs_.getWidth() == kDefaultWidth`), both drifts, and the return-gain ramp (`fxReturnGainSm_` back at 0). |
| FR-038 | Doc edit; verified by review, not by a test. |
| FR-040 / FR-041 (as amended by D-7 / D-8) | Used by SC-002, SC-003, SC-008, SC-012, SC-013, SC-018. The tap additionally gets its own `SECTION`: `preOutputTapForTest()` size == **`min(blockSamples, kMaxBlockSamples)`** — *not* the block's sample count, which is false by construction on a larger block — and its content ≠ the plugin output when the limiter is in gain reduction. The same `SECTION` **must** exercise a **> 2048-sample block** and assert `preOutputTapTruncatedForTest()` is then `true` with `size() == 2048`, and `false` with `size() == blockSamples` on a 512-sample block. That pair is what makes D-8 clause 3's flag a real gate rather than an unread member. All three of the probe's D-7 capabilities are exercised: capability 1 by SC-002/SC-012, capability 2 by SC-003(a)'s positive control, capability 3 by SC-008's positive control (b). |

---

## 5. Build integration

- `plugins/seraphis/tests/CMakeLists.txt` — three sources added to the enumerated `seraphis_tests` list
  (`:5-44`; **not globbed**, an unregistered TU silently drops):
  `unit/state_v3_test.cpp`, `integration/effects_chain_test.cpp`, `integration/effects_perf_test.cpp`.
- The `-fno-fast-math -fno-finite-math-only` block (`:76-92`) gains **`integration/effects_chain_test.cpp`
  and `unit/state_v3_test.cpp`** — the first injects/checks non-finite payloads and measures per-sample
  step statistics that fast-math contraction would reshape, the second round-trips raw floats.
  **`integration/effects_perf_test.cpp` must stay OUT**, for the same stated reason
  `param_perf_test.cpp` does (`:85-87`): `-fno-fast-math` would move the figures its baselines pin.
- No `dsp/tests/CMakeLists.txt` change — Phase 10 adds no DSP component.
- Targets to build and run:
  ```bash
  CMAKE="/c/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
  build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
  build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20
  ```
  Plus the plugin target for pluginval (SC-016) and `node tools/check-portability.js` + clang-tidy
  (SC-015).

---

## 6. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **D-1's label/period mismatch ships as specced** and users read "1/8" while hearing 1/32. | **CLOSED 2026-08-02** — Open Question 1 ruled in favour of the behaviour-describing labels (spec FR-017, C-6 row 1419 default 4 → 7, SC-001, SC-019); the `constexpr static_assert` in §2.2 keeps the shipped set self-checking at build time. |
| R-2 | **D-2's double writer** silently reverts ID 1400 on a soft-limit toggle. | **CLOSED 2026-08-02** — Open Question 2 ruled for the single composed writer (spec FR-021); §2.5.4's composed single-writer form; SC-018(d) plus a dedicated `SECTION` asserting `getOutputSaturation()` (`seraphis_engine.h:695`) tracks 1400 across a 1↔0 toggle of ID 2. |
| R-3 | **Denormals in the send during a long silent drain.** 2052 per-bin delay lines decaying toward zero is the classic denormal generator. | `ScopedDenormalMode` is armed at the top of `process()` (`processor.cpp:624`, `core/scoped_denormal_mode.h`), which is per-thread and covers the whole render. `kFxSendDrainFloor = 1e-6` additionally stops the send long before the values reach `1e-38`. |
| R-4 | **`sizeof(Processor) < 64 KiB`** (`processor.h:489`) breached by the new members. | Every buffer is a `std::vector`; `SpectralDelay`'s internals are all heap-backed (`spectral_delay.h:841-935`). The existing `static_assert` is the gate; the documented remedy is `std::unique_ptr<SpectralDelay>`, matching `engine_`/`reverb_`. |
| R-5 | **D-6's subdivision cost** pushes SC-013 over 2.5 %. | SC-013's render is *required* to include an automation point on 1410/1441/1443 so the cost is inside the measured figure rather than discovered later. Measured subdivision overhead on the shipped chain is ≤ 11.7 % of whole-block time (`param_perf_test.cpp:86-91`). Lever list per SC-013: the stage's own cost and the shipped defaults — never the 25 % ceiling, never `kBaselineFullPolyNs` (`:454-455` records it is already the maximum both `static_assert`s admit). |
| R-6 | **Portability: `std::abs` on `float` is not `constexpr` before C++23** on every leg; MSVC accepts what GCC/Clang reject. | §3.2's helper uses the branchless form. `node tools/check-portability.js` before every commit; the `guard-portability.js` PreToolUse hook runs the `--staged` form. |
| R-7 | **`std::isnan` under `-ffast-math`.** | No `std::isnan` / `std::isinf` / `numeric_limits::infinity()` anywhere in Phase 10 source **or** tests; non-finite checks are bit-pattern based, and the two TUs that need IEEE semantics are listed under `-fno-fast-math` (§5). |
| R-8 | **`std::chrono` on the audio thread** (FR-041's scoped timer) — and the instrumentation's *granularity*, which an earlier revision of this row under-counted. | Two `steady_clock::now()` reads and one add **per slice**, plus one `2n`-float `std::copy_n` pair for the tap. Under D-6's subdivision rule (`processor.cpp:811-815`) a 2048-sample block can be **up to 32 sub-slices**, i.e. up to **64 clock reads and 32 copies per block** — the multiplier is stated here rather than left implicit. Two mitigations, both applied: (i) the tap copy is **inside** the timed region (§2.5.6), so SC-012/SC-013 measure what SC-014's whole-render full-poly gate pays for — the earlier shape charged the copy to the one budget with the least headroom (4.09 points) and hid it from the two that were sized for it; (ii) the divisor is per-`process()`-call (D-8 clause 1), so the reported figure is a true per-block cost and the multiplier cannot inflate the denominator. SC-012's threshold (10 667 ns) is ~3 orders above a clock read even at 64 reads/block, so the timer still cannot dominate its own measurement. If a future measurement shows otherwise, the documented remedy is a single `now()` per `process()` call bracketing the whole slice loop. |
| R-9 | **The `kSeedId` burst** (`seedRng` + `reset` + two drift reseeds) lands on the audio thread inside `pushGlobalParams()`'s existing path (`processor.cpp:1105-1112`). | SC-011 automates `kSeedId` across ≥ 16 index changes and gates the containing block at ≤ 533 333 ns. |
| R-10 | **Bit-exact float goldens.** | None anywhere. SC-002's exact equality is a **same-instance, same-code-path** A/B, not a checked-in reference; every cross-render comparison uses `render_fingerprint.h`. `node tools/lint-float-bit-goldens.js` is the gate. |
| R-11 | **`markDirty` is not debounced**, so a host automating 1413/1418/1419/1430 writes an atomic every block. | All four are consumed by `pushEffectsParams()`'s on-change trackers, so a repeated identical automation point costs one compare — the same shape `refreshSpectralSlotFromFactory` uses (`:1246-1248`, `:1258-1260`). FR-022's on-change-only clause is now asserted directly by §4.2's repeated-identical-write case on `effectsPushCountForTest()`. |
| R-12 | **D-3's residual: the send's chunk phase is a function of engage history, not of the render start.** Across a bypass excursion the phase shifts by up to 511 samples relative to any absolute grid, so FR-008's reset lands at an absolute position that depends on where the host put the blocks containing that excursion. | This is a **named risk, not a parenthetical**, and it is why D-3 was escalated (Open Question 3, **ruled 2026-08-02**): FR-008's "partition-independent / free-running / depends only on the render start" wording **has been amended** to what the design delivers, and the residual is now disclosed in FR-008 and C-3 rather than contradicted by them. The risk stays OPEN as a residual — the amendment makes it honest, it does not remove it. Within a continuously-engaged span the phase *is* partition-independent, which is the property SC-017 tests and the only property any criterion relies on. Nothing measures the cross-excursion case, and nothing can: SC-017 forbids transitions in its render precisely because a transition lands where the host puts it. The rejected alternative — zero-padding the FIFO at engage to re-phase it onto an absolute grid — violates FR-003a's "MUST NOT be zeroed or re-phased on engage" outright, and would annihilate the tail FR-009a exists to preserve. |
| R-13 | **The accumulator's FIFO clear is the phase's sharpest failure mode.** A clear that leaves the counters anywhere other than §3.1's initial condition wraps a `std::size_t` and either hangs the audio thread (`fxChunkFill_` cleared inside the chunk loop ⇒ `-= 512` wraps ⇒ unbounded `SpectralDelay::process()` calls) or walks stale ring content into the bus (`fxOutFill_` cleared without the pre-fill ⇒ `-= n` wraps on the first short slice). | One `clearFifos()` definition, written out in §3.1, restoring exactly the prepare-time state the invariant is proved from. **One** mid-render call site, at the top of `runSendStage` before any partial chunk-loop state is live, reached only through the `fxFifoClearDue_` / `fxResetDue_` flags — the seed burst raises a flag rather than clearing inline. Two debug-only assertions (`fxOutFill_ >= n` at the mix step, `fxChunkFill_ + fxOutFill_ == kFxSendChunkSamples` at the slice end) turn any future edit that breaks the proof into an immediate Debug failure. The path is reachable in the shipping configuration — D-6 forces 64-sample sub-slices for the whole of every engage/bypass ramp — so it is exercised by SC-008, SC-011a and SC-018, not merely reasoned about. |
| R-14 | **The wander's control grid is easy to write as a loop that delivers nothing.** `MidSideProcessor::setWidth` and `OnePoleSmoother::setTarget` only move targets (`midside_processor.h:133-136`, `primitives/smoother.h:170`), so a control loop that runs to completion *before* the audio call has every iteration but the last overwritten — and C-5/FR-024's 64-sample-grid claim silently becomes a per-slice (up to 2048-sample) grid. | §3.4's body is **interleaved**: each 64-sample sub-chunk computes its controls and then processes its own samples, using `controlPhase_ + off` for the absolute grid phase. §4.2's FR-024 row measures it directly — at most 32 distinct azimuth target values over a 2048-sample block, where the non-interleaved shape would report one. |

---

## 7. Task order

Each step ends in a clean build; steps 2–10 each end with `seraphis_tests` green.

1. **Resolve Open Questions 1–4** (D-1, D-2, D-3, D-4). Blocking. D-1 and D-2 block step 2; D-3 blocks
   step 7; D-4 blocks step 8. D-7's FR-040 wording correction and D-8's FR-041 seam-set amendment are
   editorial and must land in the spec before step 10, but they do not block coding.
   **DONE 2026-08-02.** All four were ruled in favour of this plan's recommendation and the spec is
   amended (FR-017, C-6 row 1419, SC-001, SC-019; FR-021; FR-003a/FR-007/FR-008/C-3/SC-017; C-5,
   FR-024a clause 4, Edge cases), with the rulings logged in `spec.md` → *Clarifications* → *Phase-owner
   rulings on the plan's escalations*. D-7 and D-8 landed in the same change (FR-040, FR-041, plus
   SC-012/SC-013's divisor, SC-018 clause (e) and SC-003's tap clause). **Steps 2–10 are unblocked and
   unchanged.**
2. `plugin_ids.h` — 16 IDs, `kEffectsParamRangeEnd`, `kStateVersion2`/`kCurrentStateVersion = 3`, both
   band `static_assert`s, the version chain `static_assert`, the frozen-type legend. → *verify:* compiles;
   the four surface-count TUs now fail on 91-vs-107, which is the expected red.
3. `dropdown_mappings.h` — two label tables, `kSpreadDirectionCount`, the converter, the D-1 gate.
   → *verify:* `static_assert`s pass.
4. `effects_params.h` — the struct and all six functions. → *verify:* compiles standalone.
5. Controller + `editor.uidesc` + the four surface-count assertions (FR-038a clauses 1–3) → **107**.
   → *verify:* SC-001 (`param_denorm_test`) and `parameter_surface_test` green.
6. `processor.h`/`processor.cpp` **wiring only**: pack member, ladder rung, `Route::FX`, `markDirty`,
   state save/load, `pushAllSurfaces`. → *verify:* SC-009 (`state_v3_test`) green.
7. **The send** — §2.5.3's prepare block, §3.1's accumulator **including `clearFifos()` and both debug
   assertions**, §3.3's state machine, §3.2's compensation (helper in `effects_params.h`, §2.3),
   FR-030's `BlockContext`, FR-027's seeding, and D-3's fill-chunk reset trigger (**no `fxPhase_`**).
   → *verify:* SC-004, SC-005, SC-017, SC-019 green, plus §4.2's FR-022, FR-023, FR-025 and
   `kFxSendDrainFloor` rows.
8. **The wander** — §3.4's **interleaved** control loop, D-4's compensation, FR-010a's **engage and
   disengage** arms (`fxWanderRunsEffective_` / `wanderAtIdentity()`). → *verify:* SC-003 green, plus
   §4.2's FR-010/D-4, FR-024 and FR-024a rows.
9. **Freeze** — FR-023a's forced engage, D-5's priming. → *verify:* SC-007 both arms green, arm (a)
   under its **own** freeze-differenced definition (§4.1 rule 2).
10. **Seams** — FR-040's probe with all **three** D-7 capabilities, FR-041's **seven** counters plus
    `preOutputTapTruncatedForTest()` (D-8), and `unit/lifecycle_test.cpp`'s FR-035 clauses (which need
    the seams to observe the send/M-S/drift/ramp clears). → *verify:* SC-002, SC-008, SC-018 (a)–(e)
    green, plus §4.2's FR-034, FR-035 and FR-040/FR-041 rows.
11. **Continuity** — `kContinuityMechanism` 85→101, `kClassBIds` 9→12, `classBSmoothers()` 9→12 and its
    three consumers (FR-038a clauses 5–9, FR-038b). → *verify:* SC-001a green, per-ID sweep green.
12. **Perf** — `effects_perf_test.cpp`, `kNonDefaultTable` 91→107 with FR-038a clause 4's **measured**
    most-expensive-end values transcribed under a BASELINE PROVENANCE banner. → *verify:* SC-012, SC-013,
    SC-014 under the seven-run cold protocol.
13. **Gates** — `node tools/check-portability.js`, clang-tidy `-Target seraphis` (both scripts), the
    generated-artifact/lint gates, pluginval strictness 5, editor-lifecycle harness. → SC-015, SC-016.
14. **Docs** — `plugins/seraphis/CLAUDE.md` band table `1400+` row → "16 — shipped"; `CHANGELOG.md`
    entry alongside any `version.json` bump (FR-038).

*(Note on FR-038: the spec's text says the row moves from "10" to "10 — shipped". The shipped table
reads `| 1400+ | Effects | 10 |`, where "10" is the **phase number**, not a count. The row therefore
becomes `| 1400+ | Effects | 10 — shipped |`, matching how every other row records its phase.)*

---

## 8. Review notes

Every issue raised in the plan review was **accepted**; none was rejected, and no threshold was relaxed
to close one. Two were resolved by choosing between alternatives the review itself offered, and the
choice is recorded here so it is not re-litigated:

- **D-3 (partition independence).** The review offered (a) escalate and amend FR-008, or (b) drop
  `fxPhase_` and state the trigger as "the next fill-chunk boundary". **Both were taken**: the counter is
  deleted (option b, because it also removes the undefined `fxPhaseCrossedChunkBoundary()` predicate and
  the endpoint ambiguity that made it unwritable), *and* the residual is escalated as Open Question 3 with
  the spec amendment spelled out, because the design still cannot deliver FR-008's literal wording. The
  residual is now R-12, a named risk rather than a parenthetical.
- **The pre-output tap on blocks > 2048.** The review offered (a) size the tap to the host's actual
  `maxSamplesPerBlock`, or (b) keep 2048 and expose a truncation flag. **(b) was taken**, because FR-041
  pins the buffers to `SeraphisEngine::kMaxBlockSamples = 2048` (`spec.md:993-995`) and re-sizing them
  would be a second, silent departure from the spec to fix a first one. The flag
  (`preOutputTapTruncatedForTest()`, D-8 clause 3) makes truncation loud, §4.1 rule 1 pins every
  tap-measuring criterion to blocks ≤ 2048, and §4.2's FR-040/FR-041 row asserts both halves of the flag's
  behaviour.
- **`fxReturnGainSm_`'s advance owner.** The review offered (a) drive it per sample with `process()` and
  keep it out of `classBSmoothers()` (the `masterGain_` precedent, `processor.cpp:1163`, `:1727-1730`), or
  (b) keep it in the twelve and read it with `getCurrentValue()`. **(b) was taken**: it keeps
  `anyClassBSmootherUnsettled()` honest about when the FR-008/FR-009 ramp is done — which is what drives
  D-6's 64-sample subdivision, and therefore what delivers the ramp on the absolute grid in the first
  place — and it keeps `kClassBIds` at 12, so SC-001a and FR-038b clauses 3/6 need no rewrite. The
  resulting rule ("no smoother may be in `classBSmoothers()` **and** be `.process()`-ed") is stated as an
  invariant beside the declaration in §2.4 so the next member cannot repeat the mistake.

Four items required spec changes and were tracked as such rather than applied silently: **Open Questions
1–4** (D-1, D-2, D-3, D-4) needed phase-owner rulings; **D-7** (FR-040's "sole capability" wording) and
**D-8** (FR-041's seam set) are editorial corrections the spec's own success criteria already force.

**All six are now closed (2026-08-02).** The four escalations were ruled **in favour of this plan's
recommendation in every case**, so no task in §7 changes; the two editorial corrections landed in the same
change. The spec carries the amended clause text and the ruling log (`spec.md` → *Clarifications* →
*Phase-owner rulings on the plan's escalations*). The one thing a ruling did **not** remove is D-3's
residual: **R-12 stays open** — the amendment made FR-008 honest about the send's chunk phase across a
bypass excursion, it did not make the phase absolute.
