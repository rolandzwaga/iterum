# Feature Specification: Seraphis Phase 10 — Integrated Effects

**Spec slug:** `seraphis-phase10-effects`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 10 (roadmap lines 462–469), plus the *Effects* row
of the Reuse Inventory (roadmap line 93).
**Depends on:** Phase 8 (`plugins/seraphis/`, ✅ COMPLETE), Phase 9 (91-parameter surface, state version 2,
✅ COMPLETE), Phases 1–7 (`SeraphisEngine`, `SeraphisVoice`, `AetherReverb`, `BrownianDrift`, ✅ COMPLETE).
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-08-02

---

## Overview

Phase 9 completed the *engine* surface: every parameter the Phase 1–7 DSP exposes is registered,
denormalized, persisted and macro-wired, and the signal chain in `Processor::renderSlice` is exactly

> voice sum → `AetherReverb` → master gain → output stage (`TapeSaturator` → `TruePeakLimiter`)

(`plugins/seraphis/src/processor/processor.cpp:1144-1173`). Phase 10 is the last stage of the global bus:
the four effects the roadmap names — **spectral freeze**, **spectral delay**, **tape-like saturation** and
**stereo wandering** — plus the thing the roadmap says is the actual deliverable of this phase:
*"Ordering and sends defined here, not ad hoc."* (roadmap line 469).

The Reuse Inventory prescribes **"thin wiring only"** for this row (roadmap line 93). This spec holds to
that literally: **no new DSP class is created at any layer.** Everything ships as plugin-local parameter
packs plus processor members composed from components that already exist and were read this session.

Six facts read out of the headers this session are what make this more than a transcription job, and they
are what this spec principally decides:

1. **Tape saturation is already in the chain and is already the last stage.** `SeraphisEngine` owns
   `TapeSaturator satL_/satR_` and `TruePeakLimiter limiter_` (`seraphis_engine.h:1481-1483`), prepared at
   `kOutputSaturation = 0.15f` / `kOutputDriveDb = 0.0f` (`:248`, `:250`, pushed at `:334-337`), and exposes
   exactly one control: `void setOutputSaturation(float amount) noexcept` (`:672-675`), whose banner states
   *"Exposes the saturation amount only; the drive is not user-exposed at Layer 3."* Phase 9 registered **no**
   ID for it (`plugins/seraphis/src/plugin_ids.h:73-182` has no saturation ID). Phase 10's tape-saturation
   deliverable is therefore **registering the control that already exists**, not adding a saturator — and
   the "gentle ceiling, no aggressive distortion" requirement is already structural, because the drive is a
   compile-time `0.0 dB` constant no parameter can reach.

2. **The limiter must stay last, and that is load-bearing, not stylistic.** `renderSlice`'s own comment
   states that a post-limiter multiply is **FORBIDDEN** because at master gain 2.0 it produces peaks up to
   ~1.78 and makes the ceiling bound *"unsatisfiable by construction"*
   (`processor.cpp:1159-1161`), and `seraphis_engine.h:617` states *"The limiter is ALWAYS LAST"*. Every
   Phase 10 stage that changes level — the delay return, the M/S width, the azimuth pan — therefore lands
   **before** `processOutputStage`, never after it.

3. **`SpectralDelay`'s internal dry path is not latency-aligned.** `getLatencySamples()` returns `fftSize_`
   (`spectral_delay.h:542-544`), but the dry/wet blend reads `dryBufferL_[i]`, which is *this block's*
   input, against a wet that is `fftSize` samples late (`:341`, `:378`). Using its own `setDryWetMix()`
   inline would put a smeared comb on the whole bus. This spec therefore runs it **fully wet as a parallel
   send**, blends outside it, and does **not** add its latency to the plugin's reported latency — the send
   is a delay, so its FFT latency is absorbed into its own delay time.

4. **`SpectralDelay` is not deterministic as prepared.** Its RNG is seeded from
   `reinterpret_cast<uintptr_t>(this) ^ sampleRate` (`spectral_delay.h:223-224`), i.e. from an ASLR-dependent
   address, and `reset()` re-randomizes 2 × `numBins` stereo phases from it (`:279-284`). Phase 7's
   determinism harness and `tests/test_helpers/render_fingerprint.h` require reproducible renders, so the
   processor **must** call the component's own `seedRng(uint32_t)` (`:297`) from the shipped `kSeedId` table
   before it is ever rendered.

5. **`SpectralDelay::process` is NOT partition-invariant, so it cannot be called on a variable-length
   slice.** Its output stream position depends on how many analysis frames happened to be ready when the
   call was made: `STFT::canAnalyze()` requires `samplesAvailable_ >= fftSize_` (`stft.h:134-137`), each
   `analyze` consumes `hopSize_` (`:171`), each `synthesize` marks `samplesReady_ += hopSize_` (`:311`),
   and `process` pulls `toPull = min(numSamples, availableL, availableR)` (`spectral_delay.h:366`), writing
   `dryBuffer * dryMix` — silence at 100 % wet — into whatever it cannot supply (`:383-386`). Worked at
   `fftSize` 1024 / hop 512: a **single 2048-sample call** has three analyses ready and lands wet-stream
   sample 0 at output index 0; the **same 2048 samples as four 512-blocks** lands it at output index 512.
   That is a permanent one-hop offset of the whole send, not a start-up transient — and the Seraphis chain
   is documented as block-size invariant to ~1e-5 (`processor_audio_test.cpp:93-95`, echoed at
   `processor.cpp:1157`). The send therefore runs behind a **fixed-size accumulator** (C-2 clause 5,
   FR-003a), never directly on a MIDI-bounded slice.

6. **The component's per-bin feedback can exceed unity even when the registered feedback maximum does
   not.** `calculateTiltedFeedback` computes `tiltFactor = 1 + tilt·(normalizedBin − 0.5)·2` — range
   `[0,2]` — and clamps the *product* to `kMaxFeedback = 1.2f`, **not** to the registered range
   (`spectral_delay.h:603-614`, `:99`). At feedback 0.95 with tilt +1 every bin above
   `normalizedBin = 0.5263` (243 of 513 bins at `fftSize` 1024) gets a loop gain > 1, and the per-bin
   recursion `feedbackMag = tanh(delayedMag · binFeedback)` written back into the bin's delay line
   (`:751-767`, no cross-bin coupling — diffusion at `:617-637` blurs the *input* spectrum only) has a
   **stable non-zero fixed point** for gain > 1, so those bins sustain forever. Registering a 0.95 maximum
   is therefore *not* sufficient to bound the loop; the processor must pre-compensate (C-7 clause 2,
   FR-016a).

No DSP algorithm changes. No new behaviour at the shipped defaults — asserted as a negative control
(SC-002). The CPU argument runs against the **pinned, currently-shipping** gate, not the withdrawn hot
dataset: the baseline of **Phase 9's SC-009 arm** (`param_perf_test.cpp`; not this spec's SC-009, which
is the state-v3 round-trip) is `kBaselineFullPolyNs = 2318840.0` ns
(`plugins/seraphis/tests/integration/param_perf_test.cpp:456`), its run-time gate is
`kBaselineFullPolyNs × kRegressionFactor (1.15) = 2 666 666` ns against the absolute
`kFullPolyCeilingNs = 2 666 666.7` ns (`:376`, `:379`, `:472-479`), and the worst of the seven-run
2026-08-02 **cold** dataset that pinned it is **2 230 830 ns = 20.91 % of one core** (`:443-456`, table at
`:148-156`). The headroom Phase 10 has to fit inside is therefore **4.09 percentage points**, not the
0.79 that the superseded T028 hot worst (`:83`) implies.

---

## Clarifications

### Session 2026-08-02

Eight scan questions plus the three standing Open Questions were put to the phase owner and answered.
Every answer is encoded in the spec body below; this log is a provenance record, **not** the place a
reader has to go to learn the behaviour.

- **Q1 — How do the 16 new IDs enter Phase 9's SC-005 continuity contract
  (`plugins/seraphis/tests/integration/param_continuity_test.cpp`)?** → **Split class-(a)/class-(b),
  over all sixteen IDs.**
  **Class (a) `ComponentInternal` — thirteen IDs: 1400, 1411–1419, 1430, 1440 and 1442**, `smoothMs = 0`,
  each carrying the file:line citation `ContinuityRow::citation` makes mandatory
  (`param_continuity_test.cpp:141-167`) — `SpectralDelay`'s own `*Smoother_.setTarget` calls for
  1411, 1412, 1414–1419; `MidSideProcessor::widthSmoother_` (`midside_processor.h:133-136`) for 1440;
  **1400** via `TapeSaturator`'s own `saturationSmoother_` (`tape_saturator.h:248-252`); **1442** as
  `CoefficientOnly` via `BrownianDrift::setSmoothness` → `updateCoefficients()`
  (`brownian_drift.h:152-155`); with 1413 `Structural` and 1430 `Ramp`, because neither is covered by a
  smoother. **Class (b) `ProcessorSmoothed` — three IDs: 1410, 1441 and 1443**, plugin-owned, with
  processor-side `OnePoleSmoother`s at `kParamSmoothMs = 20 ms` (`processor.h:119`). **13 + 3 = 16**;
  `kContinuityMechanism` grows 85 → 101 and `kClassBIds` grows 9 → 12 (`:478-479`, `:501`), and
  `classBSmoothers()`'s `std::array<OnePoleSmoother*, 9>` (`processor.h:312`),
  `setParamSmootherTargets()` (`:285`), `advanceParamSmoothers()` (`:301`) and
  `anyClassBSmootherUnsettled()` (`:327`) all widen with it. FR-038a is additionally amended to
  enumerate **all** of `param_continuity_test.cpp`'s breaking assertions, not only the four it named:
  the `:478` `static_assert` on 85, the `:501` `static_assert` on 9, `REQUIRE(count == 91)` at **both**
  `:712` and `:811`, the both-ways exhaustiveness gate at `:749`, and the per-ID automation sweep at
  `:822-826`. **FR-038b's table is the authoritative classification** — this log line was amended on
  2026-08-02 to reconcile with it after an earlier revision listed only eleven class-(a) IDs.
  → **FR-038a, new FR-038b, SC-001a.**
- **Q2 — During the FR-009a drain window, what is the send fed?** → **Silence.** The send's own feedback
  decays the tail, and `kFxSendDrainFloor = 1e-6` ends the window early, so the worst-case cost of a
  bypass excursion is bounded **by energy, not by wall clock**. FR-007's prohibition is reworded to
  apply only when the send is **neither active nor draining**, which is what makes FR-007, FR-009a and
  SC-011a mutually consistent. → **C-3, FR-007, FR-009a, SC-011a, Edge cases.**
- **Q3 — The wander stage's constants are never given values.** → `kWanderWidthSpanPercent = 50`
  (width typically 75–125 %, extremes 50–150 %, never a mono collapse on a random excursion);
  depth is a **plugin-side multiply** of `getCurrentValue()`, so it stays a plain scalar available to
  FR-010's bypass predicate; `BrownianDrift::setMean(0.0f)` is pushed explicitly; the two azimuth gain
  smoothers run at `kParamSmoothMs = 20 ms`. All four are named `inline constexpr` with their header
  lines cited, per FR-015. → **C-5, FR-024, FR-024a.**
- **Q4 — The "isolated send return" was defined through the nonlinear output stage.** → **Add a sixth
  FR-041 test seam, `preOutputTapForTest()`** — a test-only buffer copy filled immediately before
  `engine_->processOutputStage()`, in the shape of Phase 9's existing `*ForTest()` seams
  (`processor.h:162-240`). The isolated-return criteria measure at that tap, so `TapeSaturator` +
  `TruePeakLimiter` are out of the measured quantity — **with one deliberate carve-out: SC-003(a)
  measures the TRUE PLUGIN OUTPUT**, because its whole subject is the limiter-last invariant and reading
  it before the limiter would make it vacuous. So **SC-003(b) onward, SC-005, SC-007, SC-011a and
  SC-019 use the tap; SC-003(a) does not** (it uses the tap only to establish its precondition, that the
  pre-limiter peak exceeds the ceiling). This log line was amended on 2026-08-02: an earlier revision
  said "all five criteria measure at the tap", which overstated the ruling.
  → **FR-041 clause 6, SC-003, SC-005, SC-007, SC-011a, SC-019.**
- **Q5 — Accumulator lifecycle across a bypass excursion.** → **Free-running grid.** A sample counter
  advances every block regardless of bypass; the FIFOs are cleared only by FR-008's deferred `reset()`;
  engage alignment is a deterministic function of the render start. This matches the master-gain ramp
  precedent (`processor.cpp:1154-1157`) and is the only reading under which FR-008's "absolute grid"
  clause means anything. → **FR-003a, FR-008.**
  **SUPERSEDED 2026-08-02 by the phase owner's ruling on plan OQ-3 (D-3)** — see *Phase-owner rulings on
  the plan's escalations* below. The free-running counter is **struck**: it is not deliverable alongside
  FR-007 (while bypassed the input FIFO is not written, so its fill cannot advance), and the half of this
  answer that survives is the second clause — the FIFOs are cleared **only** by FR-008's deferred
  `reset()`, never re-zeroed and never re-phased at engage.
- **Q6 — What is the "most-expensive end" of the discrete effects rows in `kNonDefaultTable`?** →
  **Operational definition.** For each CPU-ambiguous row (1413, 1418, 1419, 1430 discrete; 1411, 1412,
  1415 flat), measure each candidate once and transcribe the **costlier** value into the table under a
  BASELINE PROVENANCE banner beside it, so SC-014 provably measures the worst case rather than an
  asserted one. → **FR-038a clause 4, SC-014.**
- **Q7 — Under what machine precondition are SC-013 and SC-014 measured?** → **Write the cold protocol
  in explicitly**: fresh boot, idle machine, seven consecutive runs, best-of-16 per estimate, worst
  reported — the 2026-08-02 dataset's own protocol (`param_perf_test.cpp:133-156`). Both criteria stay
  out of the CI gate, as `[.perf]` already is. RQ-1's ruling and its headroom arithmetic are preserved
  honestly rather than by re-sizing the budget.
  **Extended 2026-08-02: that seven-run cold protocol is the SINGLE AUTHORITATIVE PROTOCOL for every CPU
  figure in this spec.** The Success Criteria preamble's blanket "worst of **six** consecutive runs"
  rule is struck — it was anchored to `param_perf_test.cpp:65-84`, the **withdrawn** T028 hot banner the
  spec disowns in C-3 and RQ-1 — and SC-011 and SC-012 now each state the seven-run protocol explicitly
  ("worst-of-six" → "worst-of-seven under the SC-013 protocol"). **No CPU criterion cites the six-run
  shape.** `:65-84` survives only as the *formatting shape* of a BASELINE PROVENANCE banner.
  → **Success Criteria preamble, SC-011, SC-012, SC-013, SC-014, RQ-1.**
- **Q8 (restated OQ-4) — Does any modulation or macro reach into the effects surface ship here?** →
  **Ship inert as specced.** `kFxDelayMixId`, `kFxWanderDepthId` and `kFxAzimuthDepthId` all default to
  0, SC-002 keeps its exact-equality bit-exact negative control, and "thin wiring only" holds. Roadmap
  KDD-1 is discharged by **Phase 11** (macro reach into the effects surface) and **Phase 12** (shipped
  patches with non-zero sends); the deviation is recorded with those named owners. → **RQ-4.**
- **OQ-1 — Does `FrequencyShifter` ship in Phase 10?** → **Confirmed NO.** It stays a recorded non-goal,
  available to a later phase if a musical design emerges; the Reuse Inventory's mention (roadmap line 93)
  does not obligate this phase. → **Non-goals, RQ-2.**
- **OQ-3 — Does tape-saturation *drive* become user-facing?** → **Confirmed AMOUNT-ONLY.** Phase 10
  registers the existing `SeraphisEngine::setOutputSaturation` control; drive stays the compile-time
  `kOutputDriveDb = 0.0f`. No additive `dsp/` setter. The "gentle ceiling" claim remains structural.
  → **Non-goals, C-7 clause 3, FR-021, RQ-3.**
- **OQ-2** was already resolved as **RQ-1** in the previous revision; Q7's answer supplies the machine
  precondition RQ-1 was measured under, so the composed "everything on" figure is gated under that
  protocol only and the defaults configuration remains the everyday gate.

### Editorial reconciliation — 2026-08-02

Five consistency defects were found while encoding the answers above. **None is a new decision**; each
makes the spec body say what the answers already decided. Recorded so a later reader can see the log and
the body were reconciled rather than drifting.

1. **One protocol, not two.** The Success Criteria preamble carried a blanket "best-of-16, worst of
   **six** consecutive runs" rule citing `param_perf_test.cpp:65-84` — the **withdrawn** T028 hot banner
   — while SC-013/SC-014 required the **seven**-run cold protocol (`:133-156`). The six-run rule is
   struck; the seven-run cold protocol is the only one this spec recognises (Q7, extended).
2. **SC-011 and SC-012 now state it.** Both said "worst-of-six"; both now say **worst-of-seven under the
   SC-013 protocol**, and SC-011 names the protocol rather than implying one.
3. **`SC-009` cross-references disambiguated.** SC-012 and SC-013 said "the SC-009 MIDI script", but
   *this* spec's SC-009 is the state-v3 round-trip. Both are now qualified as **Phase 9's SC-009 arm in
   `param_perf_test.cpp`**, exactly as SC-014 already was; the Overview and SC-013's budget derivation
   are qualified the same way. This spec's own SC-009 references (C-8, FR-038a clause 3's quoted
   `static_assert` text) are unchanged and still mean the state-v3 criterion.
4. **The Q1 log entry now accounts for all sixteen IDs.** It listed eleven class-(a) IDs; **FR-038b's
   table is authoritative** at thirteen class-(a) (adding **1400**, `Smoother` via
   `tape_saturator.h:248-252`, and **1442**, `CoefficientOnly` via `brownian_drift.h:152-155`) plus three
   class-(b), i.e. 13 + 3 = 16. The log was incomplete, not the requirement.
5. **The Q4 log entry no longer overstates the tap.** "All five criteria measure at the tap" is now the
   carve-out the ruling actually made: **SC-003(a) deliberately measures the true plugin output**,
   because the limiter *is* its subject. FR-041 clause 6 is amended to match.

### Phase-owner rulings on the plan's escalations — 2026-08-02

`plan.md` §1 escalated **four** decisions that contradict this spec as written (plan **D-1**…**D-4**,
raised there as *Open Questions 1–4* at `plan.md:89`, `:170`, `:211`, `:255`) and recorded **two** further
departures that this spec's own success criteria already force but its FR text did not admit (plan
**D-7**, **D-8**). All six were put to the phase owner and answered on 2026-08-02, **in favour of the
plan's recommendation in every case**. Each answer is encoded in the body below — the amended clause text
is in place — and this log is provenance only.

**Label collision, stated so two sets are not conflated:** these are the *plan's* OQ-1…OQ-4. They are
**not** the OQ-1 / OQ-3 / OQ-4 of the *Session 2026-08-02* log above, which were this spec's own
initial-draft questions and are now **RQ-2 / RQ-3 / RQ-4**.

- **Plan OQ-1 (D-1) — `kFxDelaySyncNoteLabels`'s ten strings name periods the component does not
  produce.** → **RULED: ship the behaviour-describing labels, and move the registered default index
  4 → 7.** `SpectralDelay::setNoteValue` stores `noteValueIndex_ = std::clamp(index, 0, 9)`
  (`spectral_delay.h:532-534`) and its **only** consumer is `dropdownToDelayMs(noteValueIndex_, tempo)`
  (`:330`) → `getNoteValueFromDropdown` (`dsp/include/krate/dsp/core/note_value.h:182-190`) →
  `kNoteValueDropdownMapping` (`:136-164`, `kNoteValueDropdownCount = 30`, `:123`), whose first ten rows
  are — verbatim from that header's own comments — `1/64T, 1/64, 1/64D, 1/32T, 1/32, 1/32D, 1/16T, 1/16,
  1/16D, 1/8T`. **Index 4 is `1/32` (0.125 beats), not `1/8`.** The doc comment at `spectral_delay.h:530`
  and the *"Default to 1/8 note (index 4)"* comment at `:893` are factually wrong about the component's
  own behaviour, and the clamp to 9 caps the reachable synced range at `1/8T` (0.333 beats). Remapping
  inside `dsp/` — widening the clamp, or indexing `kNoteValueDropdownMapping[3..21]` — is a `dsp/`
  behaviour change the *Non-goals* forbid, so **the labels move instead**. The registered default becomes
  **index 7 = `1/16` = 0.25 beats = exactly 125.0 ms at 120 BPM** (`note_value.h:147`, `:226-241`,
  `:259-265`) — the musically sane default inside the reachable range. `dropdownToDelayMs` is
  `constexpr` (`note_value.h:259`), so the table is additionally tied to the behaviour by a
  **compile-time** gate beside it, and a permuted or aspirational table fails the build rather than a
  user's ears. → **FR-017, C-6 row 1419, SC-001, SC-019.**
- **Plan OQ-2 (D-2) — three writers on `SeraphisEngine::setOutputSaturation`.** → **RULED: one writer,
  over a composed value.** Two writers already ship — the prepare-time push at `processor.cpp:538-539`
  (which decides what the *first* post-prepare block renders with, under its own KNOWN-RESIDUAL banner at
  `:526-537`) and `pushGlobalParams()`'s on-change block at `:1090-1097` — and FR-021 as written added a
  third. Two independent on-change trackers on one setter is last-writer-wins with **no convergence**:
  toggle `kSoftLimitId` off→on after setting ID 1400 to 0.8 and the engine silently reverts to
  `kOutputSaturation = 0.15f` (`seraphis_engine.h:248`) until ID 1400 next moves. `pushEffectsParams()`
  becomes the **sole** writer, over `soft ? effectsParams_.saturation : 0.0f`; ID 2 keeps its shipped
  meaning as a **gate** and ID 1400 supplies the amount that gate passes; the `pushGlobalParams()` block
  is **removed**; the prepare-time push is composed the same way and seeds the effects tracker exactly as
  step 4 already seeds `lastPushedPolyphony_` / `lastPushedSoftLimit_` (`:516-517`). At the C-6 defaults
  this is bit-identical to today (`soft == true`, `saturation == 0.15f == kOutputSaturation`), pushed the
  same number of times through the same counter (`engSoftLimitPushCountForTest()`, `processor.h:235`), so
  SC-002 and every Phase 9 cadence assertion are unaffected. FR-021's "and nothing else" is amended to
  "and nothing else **writes that setter**". → **FR-021.**
- **Plan OQ-3 (D-3) — FR-008's "free-running absolute chunk grid" is not deliverable.** → **RULED: delete
  the second counter; the sole grid is the input FIFO's own occupancy.** While bypassed the processor may
  not write the input FIFO at all (FR-007), so its fill does not advance and chunk boundaries sit at fixed
  offsets from *engage*, not from the render start; a separate free-running `fxPhase_` does not rescue the
  claim, it only relocates it (the reset would still land on a fill-chunk boundary whose absolute position
  depends on bypass history), and it could not be written anyway — `controlPhase_ += n` runs *after*
  `renderSlice()` returns (`processor.cpp:825`), so inside the send stage such a counter holds only the
  slice-start value, and under the 64-sample subdivision a slice may contain no multiple of 512 at all,
  leaving SC-018(a)'s "+1 per qualifying transition" nothing definite to fire on. The amended guarantee,
  written as what the design delivers rather than as an aspiration: **the reset lands on a fill-chunk
  boundary; within a continuously-engaged span the chunk phase is a pure function of the samples consumed
  since engage and is therefore independent of how the host partitions them (the property SC-017 tests);
  across a bypass excursion the phase is a function of engage history.** Residual — up to 511 samples of
  absolute drift per excursion, measured by nothing and measurable by nothing, since SC-017 forbids
  transitions in its render precisely because a transition lands where the host puts it — is plan **R-12**.
  This **supersedes** the *Session 2026-08-02* Q5 answer ("Free-running grid"), which is struck.
  → **FR-003a, FR-007, FR-008, C-3, SC-017, *New components* member list.**
- **Plan OQ-4 (D-4) — the azimuth pan pair must be centre-normalised.** → **RULED: multiply both gains by
  `kFxAzimuthCentreComp = 1.41421356f` (√2).** `equalPowerGains` is a *crossfade* law
  (`crossfade_utils.h:50-53`): it preserves energy when its two gains are applied to two **different**
  signals that are then summed. Applied to the two channels of **one** stereo bus, the quantity that must
  be constant is `L²·gL² + R²·gR² ≈ x²·(gL² + gR²)` for a correlated bus, so `gL = gR = cos(π/4) = 0.7071`
  at centre drops the whole bus **−3.01 dB** the instant `kFxAzimuthDepthId` leaves 0 — and jumps back
  when FR-010's skip re-engages. That is a **steady-state level step as a function of a depth control**,
  which no smoother removes because it is not a transient (and which SC-008's `maxPerSampleDelta` would
  therefore *not* flag — FR-010a spreads it over ~960 samples). With the compensation `gL² + gR² = 2` at
  every position — still position-independent, which is the property SC-006's argument actually needs —
  and centre is exactly unity per channel, so the FR-010 skip boundary is continuous. **Peak per-channel
  gain at full deflection is √2 = +3.01 dB**, bounded by the limiter, which is what SC-006 gates. C-5's
  and the *Edge cases*' energy sentences are amended to say "energy-preserving **up to a fixed centre
  normalisation**". → **C-5, FR-024a clause 4, Edge cases.**
- **Plan D-7 (editorial, not escalated) — FR-040's probe has THREE capabilities.** FR-040 said the probe's
  "sole capability" is the runtime skip of C-1 steps 4 and 5, but **SC-003(a) mandates** a positive control
  "with FR-040's probe configured to run step 5 **after** step 6" and **SC-008's positive control (b)
  mandates** "FR-040's probe snapping the 20 ms return-gain ramp (FR-008/FR-009) to instant". An
  implementer working from FR-040 alone builds a one-capability probe and then cannot write two mandatory
  positive controls. FR-040 is corrected to enumerate all three, all test-TU-only and inert on every ship
  path. This is an editorial correction to an FR this spec's own criteria already overrode — not a scope
  change. → **FR-040.**
- **Plan D-8 (editorial, not escalated) — FR-041's seam set is seven surfaces plus a truncation flag.**
  Three corrections, each forced by something the plan found. (1) Clause 1's block counter is renamed
  **`effectsStageProcessCallsForTest()`** and incremented **once per `process()` call**, never per slice:
  SC-012 and SC-013 divide the accumulated stage nanoseconds by it and compare against **per-block**
  budgets, while `renderSlice()` runs once per *slice* — the loop subdivides on every MIDI event
  (`processor.cpp:759-786`), on the 2048 cap (`:792`) and, while any class-(b) smoother is unsettled, on
  the 64-sample grid (`:811-815`) — so a per-slice divisor under-reports by up to **8×** and makes
  SC-013's budget structurally unable to fail. (2) A seventh surface, **`sendChunkCountForTest()`**, one
  increment per `spectralDelay_.process()` call: FR-007's prohibition is otherwise observable **only**
  through SC-012's `[.perf]` threshold, which is outside the CI gate, and SC-002 cannot see it at all
  (at mix 0 the mix loop adds `fxOut[i] * 0.0f`, so a fully-running send still leaves the bus
  bit-identical). (3) **`preOutputTapTruncatedForTest()`**, a `bool`: FR-041 pins the tap buffers to
  `kMaxBlockSamples = 2048` while the processor explicitly supports larger host blocks
  (`processor.cpp:787-792`), so without the flag a 4096-sample block silently yields a half-length tap and
  every tap-based criterion measures half a render with no error signal. No threshold moves.
  → **FR-041, SC-012, SC-018 clause (e), SC-003's isolated-return definition.**

---

## Scope

Phase 10 ships, and nothing else:

1. **Chain order and send topology** for the global bus (C-1), stated once and enforced by a test, so it is
   never "ad hoc" (roadmap line 469).
2. **Tape-like saturation**, registered: one control onto the existing `SeraphisEngine::setOutputSaturation`.
3. **Spectral delay**, as a parallel send off the post-master-gain bus, driving the existing
   `Krate::DSP::SpectralDelay` (Layer 4) with ten registered controls.
4. **Spectral freeze**, as the capture-and-hold toggle on that same send instance
   (`SpectralDelay::setFreezeEnabled`), which is what makes it *"global capture-and-hold of the Aether tail"*
   (roadmap line 466) — the send is fed post-`AetherReverb`.
5. **Stereo wandering**: two `BrownianDrift` instances driving a global `MidSideProcessor` width and an
   equal-power azimuth pan, with four registered controls.
6. **16 new parameter IDs** in the reserved `1400+` Effects band (`plugin_ids.h:71`), with the Phase 9
   six-function pack contract, a new `effects_params.h`, dropdown tables, `editor.uidesc` control-tags, and
   **state format version 3** as a strict byte-suffix extension of version 2.
7. **Determinism plumbing** for `SpectralDelay` (fact 4 above): `seedRng()` driven from the existing
   `kSeedId` table.
8. **Tests**: denormalization, state round-trip and migration, chain-order, RT-safety, stability, freeze
   behaviour, determinism, partition invariance, and CPU budgets — including a **re-run of Phase 7/9's
   25 % gate with the effects section ACTIVE**, i.e. at the roadmap's "everything on" operating point
   (roadmap lines 313–314), unchanged and unrelaxed. See SC-014 and *Resolved Question* RQ-1.
9. **Test-only seams** on `Processor` — a runtime probe with **three** capabilities (FR-040 as amended by
   plan D-7), a scoped stage timer with a per-`process()`-call divisor plus **five** cadence
   counters, and a **pre-output-stage tap** (`preOutputTapForTest()`, with its truncation flag) that keeps every isolated-return
   measurement clear of `TapeSaturator` + `TruePeakLimiter` — because six requirements and five criteria
   are otherwise unobservable and five more would otherwise be measured through a nonlinearity (FR-040,
   FR-041), in the shape Phase 9 established with `detail::SeraphisParamSmootherBypassProbe`
   (`specs/seraphis-phase9-parameters/spec.md` → FR-059a) and its `*ForTest()` accessors
   (`plugins/seraphis/src/processor/processor.h:162-240`).

---

## Non-goals (what other phases own)

- **No new DSP class, at any layer.** The Reuse Inventory says "thin wiring only" (roadmap line 93). If an
  implementation finds itself writing `class …` under `dsp/`, the design has left this spec.
- **No `dsp/` behaviour change.** Phase 9's FR-071 froze `dsp/` to admitted additive changes; Phase 10
  admits **none**. In particular `SeraphisEngine` gains no `setOutputDrive()`: the phase owner confirmed
  **amount-only** on 2026-08-02, so the saturation *drive* stays the compile-time `kOutputDriveDb = 0.0f`
  (`seraphis_engine.h:250`) and the "gentle ceiling" property stays structural — see **RQ-3**.

  > **SCOPE AMENDMENT (2026-08-03, phase-owner ruling "Accept both").** Implementation surfaced a latent
  > **heap-corruption defect** in `SpectralDelay::process()` — multiple frames synthesized without an
  > intervening drain stack at offset 0 and drive `samplesReady_` past `outputBuffer_.size()`, so a
  > sufficiently long `process()` call (3+ frames ready; 4096 samples at the 1024-point default makes
  > seven) reads past the end and zero-fills before the buffer. Two `dsp/` edits were admitted by ruling:
  > (1) `effects/spectral_delay.h` — drain-one-hop-per-frame loop (the contract every other spectral
  > component already observes) **plus** the one-frame delay compensation (`delayFrames − 1`, clamped at
  > 0): the loop structurally costs one hop, so uncompensated echoes landed `hopSize` late — an
  > FFT-size-dependent 10.7–42.7 ms offset from the beat a synced user dialed; SC-019 passes because of
  > this. (2) `primitives/stft.h` — a capacity guard in `pullSamples()` making the mis-use inert instead
  > of undefined. Evidence at ruling time: `dsp_effects_tests` (1505 cases), `dsp_primitives_tests`
  > (477), Iterum `plugin_tests` (241) + `approval_tests` (3/3; none exercise SpectralDelay rendering),
  > `ruinae_tests` (739) — all pass, zero build warnings. Consumers Iterum/Ruinae inherit both fixes;
  > their SpectralDelay echoes move one hop earlier, onto the labeled beat.
- **`FrequencyShifter` does not ship.** It appears in the Reuse Inventory's Effects row (roadmap line 93)
  but the Phase 10 body (roadmap lines 466–469) names four effects and gives it no role, no send, and no
  parameter. Shipping an unnamed fifth effect would be inventing scope. The phase owner **confirmed NO**
  on 2026-08-02; it remains available to a later phase if a musical design emerges. See **RQ-2**.
- **`StereoField` does not ship.** It is in the same inventory row, but it is a *delay-based* stereo
  processor (`dsp/include/krate/dsp/systems/stereo_field.h`, **Layer 3** — four `DelayLine` members at
  `:214-215` and `:218-219`) built for delay plugins, and the roadmap's
  stereo-wandering sentence names `midside_processor`, not `stereo_field` (roadmap line 468). `SeraphisVoice`
  already owns a per-voice `MidSideProcessor ms_` (`seraphis_voice.h:1154`); Phase 10's is the **global**
  one. `SeraphisVoice`'s `static_assert` explicitly forbids a `StereoField` member (`:1213`).
- **`TruePeakLimiter` is not re-registered.** It ships inside `processOutputStage` at
  `TruePeakLimiter::kDefaultCeilingDb` (`seraphis_engine.h:348`, `true_peak_limiter.h:46`) and Phase 8
  already registered `kSoftLimitId = 2` as its user-facing switch. No new ID.
- **No macro-matrix change.** `SeraphisMacroMatrix`'s nineteen voice targets and its Aether target set are
  Phase 7/9 property; Phase 10 adds no macro row and no new macro target. Macro reach into effects is out
  of scope. **This is a recorded deviation from the roadmap, not an omission** — Key Design Decision 1
  ("Nothing is ever static. Every audible parameter is a modulation target", roadmap lines 71–73) and
  KDD-5 (the macros are the primary performance surface, lines 79–80) both point the other way, and the
  Aether stage *does* have macro rows (`SeraphisMacroTargetOwner::Aether`,
  `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h:52`, `:79-80`, rows at `:218-231`). The phase
  owner ruled on 2026-08-02 that the stage **ships inert as specced**, with KDD-1 discharged by
  **Phase 11** (macro reach into the effects surface) and **Phase 12** (shipped patches with non-zero
  sends). The deviation and its named later owners are **RQ-4**.
- **No UI layout.** Phase 11 owns `editor.uidesc` layout and the cloud view. Phase 10 adds
  **control-tag metadata only**, exactly as Phase 9 did (`editor.uidesc:20-121`, 91 tags today).
- **No presets.** Phase 12 owns the factory library and the category set.
- **No per-note expression.** Phase 13 owns it (roadmap lines 505–526).
- **No spectral-state authoring mutators, no per-partial surface.** Phase 11 owns them (roadmap lines 481–493).

---

## Existing components (verified this session)

Every row was opened this session; signatures are quoted verbatim from the cited line.

| Component | Header (layer) | What Phase 10 reuses — verified signature |
|---|---|---|
| `SeraphisEngine` | `dsp/include/krate/dsp/systems/seraphis_engine.h` (L3) | `void setOutputSaturation(float amount) noexcept` (`:672`) and `[[nodiscard]] float getOutputSaturation() const noexcept` (`:695`). Output stage is `void processOutputStage(float* l, float* r, std::size_t n) noexcept` (`:618`); shipped constants `kOutputSaturation = 0.15f` (`:248`), `kOutputDriveDb = 0.0f` (`:250`), `kMaxBlockSamples = 2048` (`:215`), `kMaxVoices = 16` (`:211`). |
| `TapeSaturator` | `dsp/include/krate/dsp/processors/tape_saturator.h` (L2) | Reached **only** through `SeraphisEngine`. `void setSaturation(float amount) noexcept` clamps to `[0,1]` (`:248-249`); `void setDrive(float dB) noexcept` (`:239`) is **not** exposed. `void process(float* buffer, size_t numSamples) noexcept` is mono, in place (`:335`), and `prepare` ignores its block-size argument (`:141`). |
| `TruePeakLimiter` | `dsp/include/krate/dsp/processors/true_peak_limiter.h` (L2) | Untouched, but its position is an invariant: `void processBlock(float* left, float* right, int numSamples) noexcept` (`:104`), `kDefaultCeilingDb = -1.0f` (`:46`). Chunks internally so an oversized block cannot overrun (`:110-118`). |
| `AetherReverb` | `dsp/include/krate/dsp/effects/aether_reverb.h` (L4) | Untouched. Phase 10 taps its **output**. `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, …)` (`:2164`); `[[nodiscard]] std::size_t getLatencySamples() const noexcept { return spectralEnabled_ ? diffusionFftSize_ : std::size_t{0}; }` (`:2612-2614`); `void setFreeze(bool on) noexcept` (`:2230`) is already ID 1204 and is **a different feature** from Phase 10's spectral freeze (C-4). Its *"ONE LATENCY, BOTH PATHS"* banner (`:661-672`) is the precedent this spec cites for latency reporting. |
| `SpectralDelay` | `dsp/include/krate/dsp/effects/spectral_delay.h` (L4) | The spectral delay **and** the spectral freeze. `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (`:131`); `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` — **in place** (`:315-316`); `void reset() noexcept` (`:242`); `void seedRng(uint32_t seed) noexcept` (`:297`); `void setFreezeEnabled(bool enabled) noexcept` (`:479`); `setBaseDelayMs` (`:425`), `setSpreadMs` (`:432`), `setSpreadDirection` (`:439`), `setFeedback` (`:460`), `setFeedbackTilt` (`:468`), `setDiffusion` (`:489`), `setDryWetMix` (`:500`), `setStereoWidth` (`:512`), `setTimeMode(int)` (`:524`), `setNoteValue(int)` (`:532`) with the 0–9 mapping *"1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1"* documented at `:529-531`; `[[nodiscard]] std::size_t getLatencySamples() const noexcept { return fftSize_; }` (`:542-544`); `void snapParameters() noexcept` (`:550`), **called from inside `prepare()` at `:206`** — the ordering fact FR-004 turns on. Ranges: `kDefaultFFTSize = 1024` (`:89`), `kMaxDelayMs = 2000.0f` (`:92`), `kDefaultDelayMs = 250.0f` (`:93`), `kMaxSpreadMs = 2000.0f` (`:96`), `kMaxFeedback = 1.2f` (`:99`), tilt `[-1,1]` (`:101-102`), diffusion/dryWet/stereoWidth `[0,1]` (`:104-113`), `kDefaultDryWet = 0.5f` (`:109`), `kFreezeCrossfadeTimeMs = 75.0f` (`:906`). `enum class SpreadDirection` has **three** enumerators — `LowToHigh`, `HighToLow`, `CenterOut` (`:53-57`) — all three live branches in `calculateBinDelayMs` (`:588-596`), and the header declares **no** enumerator-count sentinel. |
| `MidSideProcessor` | `dsp/include/krate/dsp/processors/midside_processor.h` (L2) | The global width stage. `void prepare(float sampleRate, size_t maxBlockSize) noexcept` (`:96`); `void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples) noexcept` — **in-place supported**, documented at `:181`; `void setWidth(float widthPercent) noexcept` (`:133`) with `kMinWidth = 0.0f` / `kMaxWidth = 200.0f` / `kDefaultWidth = 100.0f` (`:65-67`). Its width smoother substitutes 0 for NaN (`seraphis_engine.h:53-55` records this). It has **no** pan/azimuth control — hence C-5. |
| `BrownianDrift` | `dsp/include/krate/dsp/processors/brownian_drift.h` (L2) | The wander source, twice. `void prepare(double sampleRate) noexcept` (`:121`); `void reset() noexcept` (`:133`); `void setSeed(std::uint32_t seedValue) noexcept` (`:145`); `void setSmoothness(float normalized) noexcept` (`:152`); `void setDepth(float normalized) noexcept` (`:159`); `void setMean(float mean) noexcept` (`:165`); `void processBlock(size_t numSamples) noexcept` (`:194`); `[[nodiscard]] float getCurrentValue() const noexcept override` (`:212`) and `getSourceRange()` (`:217`) — i.e. it **is** a `ModulationSource` (`dsp/include/krate/dsp/core/modulation_source.h:30-41`). Constants: `kTauMin = 0.2f` / `kTauMax = 30.0f` (`:97`, `:99`), `kDriftOutputSmoothMs = 150.0f` (`:103`), `kControlRateInterval = 32` (`:105`), `kDefaultDriftSeed = 0xB17Eu` (`:109`). |
| `equalPowerGains` | `dsp/include/krate/dsp/core/crossfade_utils.h` (L0) | Azimuth pan law. `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` → `cos(position·π/2)`, `sin(position·π/2)` (`:50-53`); pair overload at `:64`. |
| `BlockContext` | `dsp/include/krate/dsp/core/block_context.h` (L0) | `SpectralDelay::process`'s fourth argument. `struct BlockContext` (`:57`) with `double sampleRate` (`:62`), `double tempoBPM = 120.0` (`:69`), `bool isPlaying` (`:77`). The Seraphis processor does **not** construct one today (no hit in `plugins/seraphis/src/`) — FR-030. |
| Parameter-pack contract | `plugins/seraphis/src/parameters/aether_params.h` | The **six-function contract** Phase 10 copies verbatim in shape: `handleAetherParamChange` (`:100`), `registerAetherParams` (`:161`), `formatAetherParam` (`:221`), `saveAetherParams` (`:274`), `loadAetherParams` (`:297`), `loadAetherParamsToController` (`:347`), over a `struct AetherParams` of `std::atomic<>` fields (`:73`). |
| Dropdown tables | `plugins/seraphis/src/parameters/dropdown_mappings.h` | `inline constexpr std::array<…>` label tables + `static_assert` size gates (`:89-211`). `kSeedValues` is the 16-entry seed table (`:89`) Phase 10 reuses for `SpectralDelay::seedRng`. `kSyncNoteLabels` has **8** entries (`:135`) and is **not** reusable for ID 1419 — `SpectralDelay` has its own 10-entry mapping (`spectral_delay.h:529`). |
| Processor chain | `plugins/seraphis/src/processor/processor.cpp` | The insertion point. `renderSlice` steps 2–6 (`:1138-1198`); master gain per output sample on the reverb return (`:1162-1166`); `processOutputStage` in place, ALWAYS LAST (`:1170`); latency reported as `reverb_->getLatencySamples()` (`:850-854`); tempo sampled **once per `process()` call, never per slice** (`:1563`). |
| ID map / dispatch | `plugins/seraphis/src/plugin_ids.h` | Band reservation `1400+ Effects (Phase 10)` (`:71`); the range-dispatch ladder ends at `kAetherParamRangeEnd = 1400` (`:252`); `kCurrentStateVersion = 2` (`:26`) and `kStateVersion1 = 1` (`:25`); the frozen-type legend (`:184-240`) and the two band `static_assert`s (`:256-283`). |
| Test helpers | `tests/test_helpers/` | `seraphis_chain.h` (the canonical chain model the processor comment cites at `processor.cpp:1126`), `render_fingerprint.h` (measured-tolerance renders — never bit-exact; `kSampleTolerance = 1.0e-4f`, `kMetricTolerance = 1.0e-5`, `:48-52`, over the measured cross-toolchain spread at `:20-30`), `editor_lifecycle_harness.h`. |
| Test seam precedent | `plugins/seraphis/src/processor/processor.h` | Phase 9's `*ForTest()` accessors (`:162-240` — `applyVoiceParamsCallCountForTest`, `applyAetherParamsCallCountForTest`, `engSeedPushCountForTest`, …) driven by `tests/integration/param_cadence_test.cpp`, and the friend probe `detail::SeraphisParamSmootherBypassProbe` (`:246`). Phase 10 copies both shapes (FR-040, FR-041). |
| Existing 91-counts | `plugins/seraphis/tests/` | Checked-in surface-count assertions in **four** files break when the surface grows: `REQUIRE(controller.getParameterCount() == 91)` (`unit/controller/editor_lifecycle_test.cpp:242`, `:254`), `CHECK(controller.getParameterCount() == 91)` (`unit/parameter_surface_test.cpp:480`), `static_assert(kNonDefaultTable.size() == 91, …)` (`integration/param_perf_test.cpp:902`) with its `RowClass` counts at `:904-910` over the enum at `:682-687`, and five in `integration/param_continuity_test.cpp` — `static_assert(std::size(kContinuityMechanism) == 85, …)` (`:478-479`), `static_assert(std::size(kClassBIds) == 9, …)` (`:501`), `REQUIRE(count == 91)` (`:712` and `:811`), the both-ways gate `CHECK(table == expected)` (`:749`) and the per-ID automation sweep (`:822-826`). FR-038a enumerates them; FR-038b decides the classification the continuity fixes need. |
| Continuity contract | `plugins/seraphis/tests/integration/param_continuity_test.cpp` | Phase 9's SC-005 table. `struct ContinuityRow` (`:141-167`) — `ParamID id`, `Class {ComponentInternal, ProcessorSmoothed}`, `Evidence {Smoother, Ramp, SnapshotAtBirth, CoefficientOnly, PhaseContinuous, Structural}`, `const char* citation;  // file:line, MANDATORY`, `float smoothMs = 0.0f`. `kExemptIds` is exactly six IDs (`:488`). The implementation side is `classBSmoothers()` returning `std::array<Krate::DSP::OnePoleSmoother*, 9>` (`processor.h:312`), `setParamSmootherTargets()` (`:285`), `advanceParamSmoothers()` (`:301`), `anyClassBSmootherUnsettled()` (`:327`), and `inline constexpr float kParamSmoothMs = 20.0f;` (`:119`). |
| Limiter-ceiling precedent | `plugins/seraphis/tests/` | Every shipped Seraphis ceiling assertion is a **raw sample peak** against `constexpr float kLimiterCeilingLin = 0.8912509f` (`integration/param_flow_test.cpp:63`, `integration/processor_audio_test.cpp:150`), optionally with `kCeilingAllowanceDb = 0.1f` (`processor_audio_test.cpp:153`, applied at `:810`). SC-006 follows it. |

---

## New components

**No new class is created at any DSP layer.** The ODR sweep below was run this session over `dsp/` and
`plugins/` for every name the design considered, including the ones it rejected, so a later reader can see
that the "thin wiring only" conclusion was tested rather than assumed.

### ODR sweep — `grep -rn "class <Name>\b|struct <Name>\b" dsp/ plugins/`

| Candidate name | Sweep result | Disposition |
|---|---|---|
| `EffectsParams` | **no match** (name is free) | **CLAIMED** — plugin-local `struct Seraphis::EffectsParams` in `plugins/seraphis/src/parameters/effects_params.h`. Not a DSP class; matches `AetherParams` (`aether_params.h:73`). |
| `EffectsChain` | no match | rejected — the chain is six lines inside `renderSlice`; a class would be an abstraction for single-use code. |
| `SeraphisEffects` | no match | rejected — same reason. |
| `StereoWander` | no match | rejected — two `BrownianDrift` members + two smoothers on the processor; no class needed. |
| `StereoWanderer` | no match | rejected — same. |
| `SpectralFreeze` | no match | rejected — the feature **is** `SpectralDelay::setFreezeEnabled` (C-4). A wrapper would be a second source of truth for one bool. |
| `SpectralFreezeStage` | no match | rejected — same. |
| `EffectsStage` | no match | rejected — same. |

Near-name collisions that already exist and are **not** reused (recorded so the sweep is honest):
`FreezeMode` (`dsp/include/krate/dsp/effects/freeze_mode.h:173`) is a *delay-line* freeze with its own
5 s delay, pitch shifter and diffusion network — a different effect with a colliding word;
`SpectralFreezeOscillator` (`dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h:81`) captures a
**single** FFT frame and resynthesizes a drone, and is already Phase 5's per-voice pure-freeze source
(roadmap line 238) — reusing it globally would create a second, differently-behaving freeze the user
cannot distinguish.

### Plugin-local additions (`namespace Seraphis`)

| Addition | File | Nature |
|---|---|---|
| `struct EffectsParams` | `plugins/seraphis/src/parameters/effects_params.h` (new) | 12 `std::atomic<float>` + 2 `std::atomic<int>` + 2 `std::atomic<bool>`, plus the six pack functions. |
| `kFxSpreadDirectionLabels` (**3** entries), `kFxDelaySyncNoteLabels` (10 entries), `kSpreadDirectionCount` | `plugins/seraphis/src/parameters/dropdown_mappings.h` (extended) | Two `inline constexpr std::array` label tables with `static_assert` size gates, plus a plugin-local `inline constexpr std::size_t kSpreadDirectionCount = 3;` — the sentinel `spectral_delay.h` does not declare (FR-017). |
| 16 `ParameterIDs` enumerators + `kEffectsParamRangeEnd` | `plugins/seraphis/src/plugin_ids.h` (extended) | C-6. |
| `kStateVersion2`, `kCurrentStateVersion = 3` | `plugins/seraphis/src/plugin_ids.h` (extended) | C-8. |
| `SpectralDelay spectralDelay_`, `MidSideProcessor globalMs_`, `BrownianDrift widthDrift_`, `BrownianDrift azimuthDrift_`, two `OnePoleSmoother` azimuth-gain smoothers at `kParamSmoothMs` (C-5), the fixed-size send accumulator (input FIFO + output FIFO + one `kFxSendChunkSamples` scratch pair, C-2 clause 5), the input FIFO's own fill counter — **the sole chunk grid; there is no separate free-running counter** (C-3, plan D-3) — the return-gain `OnePoleSmoother` — which **is** ID 1410's class-(b) smoother (FR-038b) — two further class-(b) `OnePoleSmoother`s for IDs 1441 and 1443, one drain countdown, `EffectsParams effectsParams_` | `plugins/seraphis/src/processor/processor.{h,cpp}` (extended) | Members only. All sized/prepared in `setupProcessing()`. The three class-(b) smoothers join `classBSmoothers()`, widening its `std::array<OnePoleSmoother*, 9>` (`processor.h:312`) to 12. |
| `detail::SeraphisEffectsStageBypassProbe` (friend declaration), `effectsStageNsForTest()`, `spectralDelayResetCountForTest()`, `effectsPushCountForTest()`, `widthDriftBlockCountForTest()`, `bypassPredicateEvalCountForTest()`, `preOutputTapForTest()` | `plugins/seraphis/src/processor/processor.{h,cpp}` (extended) | Test-only seams beside Phase 9's (`processor.h:162-240`, `:246`). FR-040, FR-041. |

---

## Conventions decided in this spec

### C-1. The chain order — decided once, here, and asserted by a test

Roadmap line 469 makes ordering this phase's deliverable. The global bus is, in exact order:

```
  1  voices                    engine_->processStereoBlock(dry)          engine.h:547
  2  Aether                    reverb_->processStereoBlock(dry -> wet)   reverb.h:2164
  3  master gain               per OUTPUT sample on the reverb return    processor.cpp:1162-1166
  4  spectral-delay SEND       tap wet -> fixed-size accumulator -> SpectralDelay(100 % wet)
                               -> output FIFO -> sum back at kFxDelayMix        C-2 clause 5
  5  stereo wandering          MidSideProcessor width, then azimuth pan, in place on the bus
  6  output stage              TapeSaturator -> TruePeakLimiter          engine.h:618  ALWAYS LAST
  7  bloom lifecycle           unchanged                                  processor.cpp:1175-1197
```

Steps 1–3, 6 and 7 are Phase 9's and their **observable behaviour is unchanged** (FR-001, proven by
SC-002). Steps 4 and 5 are the whole of Phase 10's audio work. Step 4's accumulator is what makes the
send's contribution independent of how the host and the MIDI-slice loop happen to partition the render
(Overview fact 5); the accumulator's FIFOs are drained and refilled per slice, so `SpectralDelay::process`
itself only ever sees `kFxSendChunkSamples` samples.

Three placements are load-bearing and each has a code-cited reason:

- **Step 5 before step 6.** A width or azimuth change after the limiter re-inflates peaks above the ceiling.
  `processor.cpp:1159-1161` states a post-limiter multiply is FORBIDDEN for exactly this reason, and
  `seraphis_engine.h:617` states the limiter is ALWAYS LAST.
- **Step 4 after step 3 (post-master-gain).** The send level then tracks the fader, so master gain remains a
  single uniform scalar on everything the limiter sees — which is what keeps Phase 9's ceiling argument
  intact.
- **Step 4 before step 5.** The wander must image the *whole* bus, delay returns included; a delay whose
  returns sit outside the stereo field would read as a separate, statically-imaged effect.

### C-2. The spectral delay is a parallel **SEND**, not an inline mix

`SpectralDelay`'s internal dry/wet blends a *current-block* dry (`spectral_delay.h:341`, `:378`) against a
wet that is `fftSize` samples late (`:542-544`). Using `setDryWetMix()` inline would comb the bus. Therefore:

1. `setDryWetMix(1.0f)` is pushed **once, immediately BEFORE `prepare()`**, and is never a registered
   parameter. The component runs 100 % wet, always. **The ordering is load-bearing, not stylistic:**
   `setDryWetMix` only sets a smoother *target* (`spectral_delay.h:500-503`); that smoother is advanced
   exactly **once per `process()` call** (`:373`, `:389`) despite being configured with a per-sample 50 ms
   coefficient (`:184-194`), so a push *after* prepare creeps toward 1.0 by ~0.04 % of the remaining
   distance per call from `kDefaultDryWet = 0.5f` (`:109`) — tens of seconds of un-aligned current-block
   dry leaking into the bus, which is the exact comb this convention exists to prevent. Pushed **before**
   prepare, `prepare()`'s own `setTarget` (`:202`) and `snapParameters()` (`:206`, `:550`) snap it to 1.0.
   This is the same rule C-7 clause 3 already states for `setFFTSize`, and the same hazard
   `seraphis_engine.h:331-333` records for the saturator ("The setters run BEFORE prepare() so the
   saturator's parameter smoothers are SNAPPED to them … instead of ramping in from the ctor defaults").
2. The processor copies the post-master-gain bus into the send accumulator (clause 5), calls `process()`
   in place on the accumulator's chunk scratch, and adds the accumulator's output back at
   `kFxDelayMixId`. The bus itself is never overwritten by the send.
3. **The send's latency is not reported.** `Processor::getLatencySamples()` keeps returning
   `reverb_->getLatencySamples()` (`processor.cpp:850-854`), unchanged. The justification is that the send
   *is* a delay: its `fftSize`/48 kHz ≈ 21.3 ms is indistinguishable from delay time and is absorbed into it.
   Reporting it would add ~21 ms of latency to an instrument even for users who never enable the effect, and
   making the report *conditional* would mean a runtime latency change (`restartComponent(kLatencyChanged)`)
   on a parameter move — which is worse than either.
4. Consequently there is **no dry-alignment delay line** and no `AetherReverb`-style "ONE LATENCY, BOTH
   PATHS" construction here. That pattern (`aether_reverb.h:661-672`) exists because the reverb's dry and wet
   are *mixed*; a send is not mixed against an aligned dry.
5. **The send runs behind a fixed-size accumulator.** `SpectralDelay::process` MUST be called with a
   constant `numSamples == kFxSendChunkSamples` — a named plugin constant equal to the component's hop
   size at `kDefaultFFTSize = 1024`, i.e. **512** (`spectral_delay.h:134`, `:89`) — and never with a
   MIDI-slice length. The processor pushes the post-master-gain bus into an input FIFO, runs the component
   whenever a whole chunk is available, and reads the return out of an output FIFO pre-filled with one
   chunk of silence. The reason is Overview fact 5: called with a variable `n`, the component's output
   stream position depends on how many analysis frames happened to be ready, so the *same* audio delivered
   as one 2048-sample call and as four 512-sample calls comes back a whole hop apart — permanently. The
   accumulator's one-chunk pipeline delay is a fixed **512 samples**, absorbed into the send's delay time
   exactly as clause 3 absorbs the `fftSize` latency, and it is likewise **not** reported (FR-005).

### C-3. Bypass is real, and it is what protects Phase 7's CPU gate

Phase 7/9's full-poly gate is 25 % of one core at 8 voices. The **currently pinned** figure is the
2026-08-02 cold dataset's worst, **2 230 830 ns/block = 20.91 %** (`param_perf_test.cpp:443-456`, table at
`:148-156`), gated at `kBaselineFullPolyNs = 2318840.0` × `kRegressionFactor = 1.15` = 2 666 666 ns against
`kFullPolyCeilingNs = 2 666 666.7` ns (`:456`, `:379`, `:376`). That leaves **4.09 percentage points** for
everything Phase 10 adds — which is what SC-013's 2.5 % stage budget is sized against and what makes
SC-014's "everything on ≤ 25 %" gate arithmetically reachable (20.91 + 2.5 = 23.41 %, 1.59 points of
margin). The earlier T028 hot worst of 24.21 % (`:83`, `:107`, `:113`) is a **withdrawn** dataset — the
banner at `:103-131` records why — and no argument in this spec rests on it.

Phase 10 must nevertheless cost **nothing** at its defaults:

- **Send bypass.** When the send is **neither active nor draining** the processor does not copy into the
  accumulator, does not call `SpectralDelay::process`, and does not touch the send buffers. This is the
  only reason the default configuration is free — at the C-6 defaults the send has never been active, so
  it is never draining either.
- **Engage protocol.** On the bypassed → active transition the processor ramps the return gain 0 → target
  over 20 ms (`kFxReturnRampMs`). It calls `spectralDelay_.reset()` (`spectral_delay.h:242`,
  allocation-free) **only** if the send has been continuously bypassed for longer than the drain window
  below; otherwise the tail and any captured freeze survive the excursion. The reset's cost is bounded and
  is measured (SC-011): it resets `4 × numBins = 2052` per-bin `DelayLine`s, re-randomizes `2 × numBins`
  stereo phases (`:279-284`) and fills four scratch vectors.
- **The chunk grid is the input FIFO's OWN occupancy — there is no second counter.** A chunk runs whenever
  the input FIFO's fill reaches `kFxSendChunkSamples`, and FR-008's deferred reset fires on that chunk-loop
  iteration. Within a **continuously-engaged span** the chunk phase is a pure function of the samples
  consumed since engage, so it is **independent of how the host partitions them** — which is the
  partition-independence SC-017 and SC-018(a) actually rely on, and the same rule the master-gain ramp
  already states in code (`processor.cpp:1154-1157`: *"a ramp advanced per slice is partition-dependent BY
  CONSTRUCTION"*). The FIFOs are cleared **only** by FR-008's deferred `reset()` — never re-zeroed and
  never re-phased at engage; a re-phasing engage would add a second reset-like event and a fresh
  512-sample pipeline fill on every crossing.
  **A free-running ABSOLUTE grid was claimed by an earlier revision and is STRUCK** (plan D-3, ruled
  2026-08-02): while the send is neither active nor draining the bus is not copied in (FR-007), so the
  fill does not advance, and chunk boundaries therefore sit at fixed offsets from **engage**, not from the
  render start. Across a bypass excursion the phase is a function of engage history — up to 511 samples
  of absolute drift per excursion, which is plan **R-12** and which no criterion measures or can measure
  (SC-017 forbids transitions in its render precisely because a transition lands where the host puts it).
- **Drain protocol, replacing an unconditional reset.** On the active → bypassed transition the return
  ramps to 0 over 20 ms (FR-009) and the send **keeps processing, fed SILENCE** — the bus is no longer
  copied in; zeros are pushed into the input FIFO — for a bounded drain window `kFxSendDrainMs = 2000` ms
  — one `kMaxDelayMs` (`spectral_delay.h:92`) — or until the accumulator's output peak falls below
  `kFxSendDrainFloor = 1e-6` (linear), whichever comes first; only then does processing stop and `reset()`
  become due on the next engage. Feeding silence rather than the live bus is what makes the **worst-case
  cost of a bypass excursion bounded by ENERGY rather than by wall clock**: the send's own feedback decays
  the tail into the floor, and at the SC-011a operating point (feedback 0.6, delay 250 ms) the early exit
  fires long before the 2 s cap, so parking the mix at 0 does not cost the full SC-013 budget for two
  seconds. The price is that the tail is attenuated in proportion to the excursion length, which is why
  SC-011a's ±2.0 dB clause is written against an excursion *shorter* than the window and its control
  against one *longer*. Without the window at all, the predicate is `kFxDelayMixId == 0.0f` **exactly**,
  so any bipolar LFO, macro sweep or fader ride that merely *touches* zero on its way through would
  annihilate the delay tail and any captured spectrum every time it crossed. SC-011a asserts an excursion
  shorter than the window preserves both.
- **Wander bypass.** When `kFxWidthId == 100 %` **and** `kFxWanderDepthId == 0` **and**
  `kFxAzimuthDepthId == 0`, the processor skips `globalMs_.process()` and the azimuth multiply entirely.
  The skip is exact **because the stage does not run**. Running `MidSideProcessor` at width 100 % would be
  an *algebraic* identity (`mid = (L+R)·0.5`, `side = (L−R)·0.5` at `midside_processor.h:200-201`,
  reconstructed as `mid ± side` at `:225-226`) but **not** a bit-identity in IEEE-754 — each of those
  three operations rounds, so e.g. `L = 1.0f, R = 2⁻³⁰` reconstructs `R_out = 0.0f`. FR-010's skip is
  therefore **mandatory, not an optimisation**: SC-002 asserts exact equality and an implementer who
  trusts a claimed "identity" and leaves the stage running will fail it.
- **The drift sources still advance while bypassed** (`BrownianDrift::processBlock`), so re-engaging does not
  restart a wander that was conceptually running. They are ~32-sample-decimated AR(1) updates
  (`brownian_drift.h:105`) and are two orders of magnitude below the budget. This is a pure call-cadence
  property with no audible seam, so FR-041's `widthDriftBlockCountForTest()` is what makes it testable
  (SC-018).

### C-4. Two freezes, and they are different features

| | ID | Target | What it does |
|---|---|---|---|
| **Aether Freeze** (Phase 9) | 1204 `kAetherFreezeId` | `AetherReverb::setFreeze` (`aether_reverb.h:2230`) | Latches the FDN to unity feedback — an *infinite decay*, energy-conserving, with a 50 ms latch (`kFreezeLatchMs`, `:1388`). |
| **Spectral Freeze** (Phase 10) | 1430 `kFxSpectralFreezeId` | `SpectralDelay::setFreezeEnabled` (`spectral_delay.h:479`) | **Captures** the current magnitude/phase spectrum of the send input at the moment of engagement (`:677-688`), holds it, and crossfades in over `kFreezeCrossfadeTimeMs = 75 ms` (`:906`; the per-frame increment is derived at `:210-212`, i.e. ~7 hops at 48 kHz / `fftSize` 1024 — **not** one hop), with a slow phase drift once fully frozen so it does not read as a static resonance (`:698-706`). |

Because the send is fed from the post-Aether bus (C-1 step 4), the captured spectrum **is** the Aether
tail — which is exactly the roadmap's wording, *"global capture-and-hold of the Aether tail"* (line 466).
The two are independently reachable and independently automatable; nothing in this spec couples them. The
labels in `editor.uidesc` must keep them distinguishable ("Aether Freeze" vs "Spectral Freeze").

**The spectral freeze must be reachable on its own, and at the shipped defaults it otherwise is not.**
`kFxDelayMixId`'s C-6 default is 0, which is the send's full-bypass predicate (FR-007), so on the default
patch toggling 1430 would produce no audio and no state change at all — a dead control on a first-class
roadmap effect (line 466). Therefore: **engaging `kFxSpectralFreezeId` forces the send active** at
`kFxFreezeMinReturnGain = 0.5` whenever the registered mix is below it, ramped over the same 20 ms, and
that forced engage **suppresses** the `reset()` of C-3's engage protocol — otherwise the reset would clear
`wasFrozen_`/`freezeCrossfade_` and the frozen spectrum buffers (`spectral_delay.h:256-257`, `:276-277`)
in the same block the capture is supposed to happen in. Releasing the toggle returns the send to whatever
`kFxDelayMixId` says, through the normal drain protocol. FR-023a states this; SC-007 arm (a) starts from
the C-6 defaults and asserts audible, held output.

### C-5. Stereo wandering: how width and azimuth are produced

`MidSideProcessor` has no pan (its only controls are width, mid gain, side gain, solo — `:133-167`), so
azimuth is a separate, explicit stage:

- **Width.** `globalMs_.setWidth(clamp(base + depth · d_w · kWanderWidthSpanPercent, 0, 200))`, where
  `base = kFxWidthId`, `depth = kFxWanderDepthId`, and `d_w = widthDrift_.getCurrentValue()`
  (`brownian_drift.h:212`, range from `getSourceRange()` at `:217`).
  **`inline constexpr float kWanderWidthSpanPercent = 50.0f;`** — a named plugin constant, never a literal
  at the use site. 50 rather than 100 because `BrownianDrift` is bipolar `[-1,+1]` with
  `kInternalStd = 0.5` (`brownian_drift.h:100`), so at depth 1 the walk normally stays inside about ±0.5:
  a span of 50 puts width typically in **75–125 %** with extremes at **50–150 %**, which is the roadmap's
  *"bounded, slow, and smooth"* (line 76) and can never collapse the bus toward mono on a random
  excursion. A span of 100 would reach 0–200 % in the tails and read as an unstable image.
- **Depth is a PLUGIN-SIDE MULTIPLY, not `BrownianDrift::setDepth()`.** Both `kFxWanderDepthId` and
  `kFxAzimuthDepthId` scale `getCurrentValue()` in the processor; `setDepth()` (`brownian_drift.h:159`)
  is left at its prepared value on both drifts. The reason is FR-010: the bypass predicate needs the depth
  as a **plain scalar it can compare against 0** in the same block the host wrote it. Routing depth
  through the drift instead would put it behind that component's `kDriftOutputSmoothMs = 150 ms` output
  smoother (`:103`), so a depth of exactly 0 would still be emitting a decaying non-zero for ~150 ms and
  FR-010's skip — which SC-002 requires to be exact — could not be taken on the block the value arrived.
- **`setMean(0.0f)` is pushed explicitly** on both drifts at prepare (`brownian_drift.h:165`), so the walk
  is centred and the width/azimuth mapping above is symmetric about `base` and about centre. It is pushed
  rather than assumed: the mapping's symmetry is load-bearing for the edge cases, and a non-zero mean
  would bias the image permanently.
- **Azimuth.** `position = 0.5 + 0.5 · depthAz · d_a`, clamped to `[0,1]`, fed to
  `equalPowerGains(position, gL, gR)` (`crossfade_utils.h:50`) and **both gains multiplied by
  `kFxAzimuthCentreComp`** (below). The two products are **targets** for two
  `OnePoleSmoother`s **at `kParamSmoothMs = 20.0f` ms** (`processor.h:119`) — the same time constant the
  class-(b) continuity rows use (FR-038b) — and are applied per sample; the `cos`/`sin` pair is evaluated
  **once per 64-sample control chunk**, on the same absolute grid the engine and the reverb already use
  (`aether_reverb.h:1384-1386` names `kControlChunkSamples = 64` and cites `continuous_body.h:97`,
  `harmonic_cloud.h:144`, `atmosphere_engine.h:269`). Per-sample transcendentals are forbidden.
- **Rate.** `kFxWanderRateId` maps to `BrownianDrift::setSmoothness` (`:152`), whose correlation time spans
  `kTauMin = 0.2 s` … `kTauMax = 30 s` (`:97`, `:99`). Both drifts share the rate control; they differ only
  in seed, so width and azimuth never move in lockstep.
- **Seeds.** `widthDrift_.setSeed(kSeedValues[i] ^ kFxWidthDriftSalt)` and
  `azimuthDrift_.setSeed(kSeedValues[i] ^ kFxAzimuthDriftSalt)` with two distinct named salts, following the
  reverb's own salt convention (`aether_reverb.h:1546-1552`). `kSeedValues` is the shipped 16-entry table
  (`dropdown_mappings.h:89`) already driven by `kSeedId`.
- **Azimuth is energy-preserving UP TO A FIXED CENTRE NORMALISATION, and the normalisation is
  mandatory.** The pan pair is `equalPowerGains(position) × kFxAzimuthCentreComp`, where

  ```cpp
  /// Unity at centre. equalPowerGains(0.5) returns cos(pi/4) = sin(pi/4) = 0.70710678
  /// on BOTH channels (crossfade_utils.h:50-53), so an uncompensated pan law drops the
  /// whole bus 3.01 dB the instant kFxAzimuthDepthId leaves 0 - which FR-010's skip
  /// boundary would then expose as a permanent step in steady-state level.
  inline constexpr float kFxAzimuthCentreComp = 1.41421356f;  // sqrt(2) = 1 / cos(pi/4)
  ```

  so `gL² + gR² = **2**` at every position — constant, which is the property SC-006's argument actually
  needs — and centre is **exactly unity per channel**. *(Amended 2026-08-02 by the phase-owner ruling on
  plan OQ-4/D-4; the previous claim `gL² + gR² = 1` was the wrong invariant here.)* `equalPowerGains` is a
  **crossfade** law: it preserves energy when its two gains are applied to two *different* signals that
  are then summed. Applied to the two channels of **one** stereo bus the quantity that must be constant is
  `L²·gL² + R²·gR²`, which for a correlated bus is `≈ x²·(gL² + gR²)` — so the raw law **loses 3.01 dB**
  relative to bypass rather than being neutral, and that loss appears and disappears across FR-010's skip
  boundary as a **steady-state** level step, which no smoother removes because it is not a transient (and
  which SC-008's `maxPerSampleDelta` would therefore not flag, FR-010a having spread it over ~960
  samples). **Peak per-channel gain at full deflection is `√2` (+3.01 dB)** on one channel with the other
  at 0; the limiter is last and SC-006 gates that configuration.

### C-6. The parameter table — 16 IDs in the reserved `1400+` band

Ranges and defaults are transcribed from the DSP headers cited in the *Source* column. Types are **frozen
for the life of the plugin** (`plugin_ids.h:184-190`): R = plain `Vst::Parameter`, L =
`StringListParameter` (via `createDropdownParameterWithDefault`), T = stepped toggle (`stepCount = 1`).

| ID | Name | Type | Range / units | Default | Route | Target — source |
|---|---|---|---|---|---|---|
| 1400 | `kFxSaturationId` | R | lin `[0,1]` | **0.15** | ENG | `SeraphisEngine::setOutputSaturation` (`seraphis_engine.h:672`); default **is** `kOutputSaturation` (`:248`) — C-7. |
| 1410 | `kFxDelayMixId` | R | lin `[0,1]` | **0.0** | FX | Send return gain (plugin-owned). 0 ⇒ full bypass (C-3). |
| 1411 | `kFxDelayTimeId` | R | lin `[0,2000]` ms | 250.0 | FX | `setBaseDelayMs` (`spectral_delay.h:425`); bounds `kMinDelayMs`/`kMaxDelayMs` (`:91-92`), default `kDefaultDelayMs` (`:93`). |
| 1412 | `kFxDelaySpreadId` | R | lin `[0,2000]` ms | 0.0 | FX | `setSpreadMs` (`:432`); bounds `:95-96`. |
| 1413 | `kFxDelaySpreadDirectionId` | L | **3 entries** {Low→High, High→Low, Center→Out} | 0 | FX | `setSpreadDirection` (`:439`), `enum class SpreadDirection` (`:53-57`) — **three** enumerators, all three live branches at `:588-596`. Registering only two would strand `CenterOut` permanently: the type is frozen at registration (`plugin_ids.h:184-190`, FR-020), so a later phase could not widen the list. |
| 1414 | `kFxDelayFeedbackId` | R | lin `[0,0.95]` | 0.35 | FX | `setFeedback` (`:460`), pushed **tilt-compensated** (FR-016a). **Registered max is 0.95, not the component's `kMaxFeedback = 1.2f` (`:99`)** — C-7 clause 2. |
| 1415 | `kFxDelayTiltId` | R | lin `[-1,+1]` | 0.0 | FX | `setFeedbackTilt` (`:468`); bounds `:101-102`. |
| 1416 | `kFxDelayDiffusionId` | R | lin `[0,1]` | 0.30 | FX | `setDiffusion` (`:488`); bounds `:104-105`. |
| 1417 | `kFxDelayWidthId` | R | lin `[0,1]` | 0.50 | FX | `setStereoWidth` (`:512`); bounds `:112-113`. |
| 1418 | `kFxDelaySyncId` | T | off/on | off | FX | `setTimeMode(int)` (`:524`). |
| 1419 | `kFxDelaySyncNoteId` | L | 10 entries | **7 (1/16)** | FX | `setNoteValue(int)` 0–9 (`:532-534`), whose index reaches `kNoteValueDropdownMapping[0..9]` via `dropdownToDelayMs` (`:330`; `core/note_value.h:136-164`, `:182-190`). New 10-entry table naming the periods the component **actually produces** — `1/64T … 1/8T` (FR-017); the mapping documented at `:530` is wrong and is not transcribed. Default index **7 = `1/16` = 0.25 beats = 125.0 ms at 120 BPM**, moved from the previously-specified 4 (which is `1/32`, not `1/8`) by the phase-owner ruling on plan OQ-1/D-1, 2026-08-02. `kSyncNoteLabels` has 8, in a **different** order (`dropdown_mappings.h:135`). |
| 1430 | `kFxSpectralFreezeId` | T | off/on | off | FX | `setFreezeEnabled` (`:479`). |
| 1440 | `kFxWidthId` | R | lin `[0,200]` % | 100.0 | FX | `MidSideProcessor::setWidth` (`midside_processor.h:133`); bounds `kMinWidth`/`kMaxWidth`/`kDefaultWidth` (`:65-67`). |
| 1441 | `kFxWanderDepthId` | R | lin `[0,1]` | 0.0 | FX | width-drift depth (C-5). |
| 1442 | `kFxWanderRateId` | R | lin `[0,1]` | 0.50 | FX | `BrownianDrift::setSmoothness` on **both** drifts (`brownian_drift.h:152`); default is `kDefaultSmoothness` (`:107`). |
| 1443 | `kFxAzimuthDepthId` | R | lin `[0,1]` | 0.0 | FX | azimuth-drift depth (C-5). |

Type tally: **12 R + 2 L + 2 T = 16**. Surface grows **91 → 107**.

Band constant: `constexpr Steinberg::Vst::ParamID kEffectsParamRangeEnd = 1500;` appended to the dispatch
ladder (`plugin_ids.h:245-252`), with both existing `static_assert`s extended
(`kAetherParamRangeEnd < kEffectsParamRangeEnd`; `kFxSaturationId >= kAetherParamRangeEnd` and
`kFxAzimuthDepthId < kEffectsParamRangeEnd`).

**Route `FX` is new.** Phase 9's `Route` enum is `VP / MB / AE / CFG / ENG / Local`, **declared** at
`processor.cpp:131` with its classifier `routeOf` spanning `:133-249`; `markDirty`'s consuming switch is
at `:1214-1228`. Fifteen of the sixteen IDs are consumed by processor-owned members and take a
new `Route::FX` arm; ID 1400 is `Route::ENG` because its target is `SeraphisEngine`. `FX` bumps no
generation counter — the effects members are pushed directly by a `pushEffectsParams()` helper, on change
only, on the same cadence as `pushGlobalParams()`.

### C-7. Registered defaults are bit-identical to shipped behaviour, and one range is deliberately narrower

1. **Every default in C-6 is the value the chain already produces.** ID 1400's default is the literal
   `SeraphisEngine::kOutputSaturation = 0.15f` (`seraphis_engine.h:248`), so registering it changes nothing.
   IDs 1410, 1441, 1443 default to 0 and ID 1440 to 100 %, which is what C-3's bypass rule makes an exact
   identity. This is what SC-002 asserts.
2. **`kFxDelayFeedbackId` registers `[0, 0.95]`, and the processor pushes it TILT-COMPENSATED.**
   `kMaxFeedback = 1.2f` (`spectral_delay.h:99`) is documented *"Allow slight overdrive"* and is a per-bin
   loop gain above unity — i.e. a sustaining bin in a **global, always-summed** bus feeding a limiter.
   Seraphis' identity is *"Entropy, not chaos … bounded, slow, and smooth"* (roadmap line 76).

   **A registration cap alone does NOT bound the loop, and an earlier revision of this clause claimed it
   did.** `calculateTiltedFeedback` (`:603-614`) returns
   `clamp(globalFeedback · (1 + tilt·(normalizedBin − 0.5)·2), 0, kMaxFeedback)`; the tilt factor spans
   `[0, 2]` and `kFxDelayTiltId` registers the full `[-1, +1]` (row 1415). At feedback 0.95 with tilt +1,
   every bin above `normalizedBin = 0.5263` — **243 of the 513 bins** at `fftSize` 1024 — receives a loop
   gain > 1.0, saturating at 1.2. The per-bin recursion is `feedbackMag = tanh(delayedMag · binFeedback)`
   written straight back into that bin's own delay line (`:751-767`), with no cross-bin coupling inside
   the loop (diffusion at `:617-637` blurs the *input* spectrum only), so for gain > 1 the magnitude map
   has a **stable non-zero fixed point** (g = 1.2 → m\* ≈ 0.69) and those bins never decay.

   **The bound is therefore established by construction, in the processor, not by the range.** FR-016a
   requires the pushed value to be `setFeedback(fb / (1 + |tilt|))`, so the worst tilted bin lands at
   exactly `fb ≤ 0.95 < 1.0` at every tilt setting, and tilt 0 is unchanged. The registration cap stays at
   0.95 because it is what makes that arithmetic close; the component's own clamp is left untouched.
   SC-005 derives the worst-case bin gain from the pushed value and measures decay at **both** tilt
   extremes as two separate runs.
3. **Frozen internals, never registered:** `setDryWetMix(1.0f)` (C-2 clause 1, pushed **before**
   `prepare`), `setFFTSize(kDefaultFFTSize = 1024)`
   (`:89`, `:408`) pushed once before `prepare`, `setSpreadCurve(SpreadCurve::Logarithmic)` (`:448`, the
   component's own default, documented *"perceptually more even"* at `:63`), `TapeSaturator` drive at
   `kOutputDriveDb = 0.0f` (`seraphis_engine.h:250`) — **confirmed amount-only by the phase owner on
   2026-08-02, RQ-3**, so the roadmap's "gentle ceiling — no aggressive distortion" (line 467) is a
   structural property of a compile-time constant no parameter can reach — and the limiter ceiling at
   `kDefaultCeilingDb`.

### C-8. State format version 3

`kCurrentStateVersion` moves 2 → 3 (`plugin_ids.h:26`); `kStateVersion2 = 2` is added as a named constant
beside `kStateVersion1` (`:25`) so the migration path is expressed against symbols, never literals.

- The 16 effects fields are appended **after** every version-2 field, so a version-2 stream stays a **strict
  byte prefix** of a version-3 stream — the same property `plugin_ids.h:22-24` records for v1→v2, and the
  same property that lets the EOF-safe loader chain migrate with **no version-aware branch**.
- Field order: the C-6 table order (1400, 1410…1419, 1430, 1440…1443). Wire types: 12 × `float`,
  2 × `int32` (1413, 1419), 2 × `bool` (1418, 1430) — matching how `aether_params.h` writes its
  17 float + 1 bool set (`:274-296`).
- Loading a version-1 or version-2 stream leaves every effects field at its C-6 default, which by C-7 is the
  behaviour that stream already had. SC-009 asserts this on a real v2 blob.
- `pushAllSurfaces()` (`processor.h:270`) gains the effects surface, so a `setState()` arriving after
  `setupProcessing()` re-pushes it — Phase 9's Q2 answer applies unchanged.

---

## Functional Requirements

### A. Chain and routing

- **FR-001.** `Processor::renderSlice` MUST implement C-1's seven steps in exactly that order. The
  **observable behaviour** of steps 1–3, 6 and 7 MUST be unchanged from Phase 9
  (`processor.cpp:1144-1197`) — verified by SC-002, not by inspection; the only insertions are steps 4 and
  5, between the master-gain loop (`:1162-1166`) and `processOutputStage` (`:1170`). *(A previous revision
  demanded those steps be "byte-for-byte the Phase 9 code", which no runtime test can check and which is
  self-contradictory in the strict reading: step 6 today is `processOutputStage` followed immediately by
  `std::copy_n` at `:1170-1173`, and inserting anything before it necessarily moves those lines.)*
- **FR-002.** No Phase 10 stage may run after `engine_->processOutputStage()`. The limiter is last
  (`seraphis_engine.h:617`), and a post-limiter level change is forbidden (`processor.cpp:1159-1161`).
- **FR-003.** The spectral delay MUST be wired as a parallel send per C-2: the bus is copied into a
  prepare-time scratch pair, `SpectralDelay::process` runs **in place on the scratch**
  (`spectral_delay.h:315`), and the result is summed back into the bus scaled by `kFxDelayMixId`. The bus
  MUST NOT be passed to `SpectralDelay::process` directly.
- **FR-003a.** `SpectralDelay::process` MUST be invoked with a **constant** `numSamples ==
  kFxSendChunkSamples = 512`, through the fixed-size accumulator of C-2 clause 5, and MUST NOT be invoked
  with a MIDI-slice length or a host block length. The accumulator's output FIFO MUST be pre-filled with
  one chunk of silence at prepare so its pipeline delay is a fixed 512 samples independent of partition.
  Rationale: Overview fact 5 — the component's output stream position is a function of how many analysis
  frames were ready, so a variable `n` makes the whole send permanently partition-dependent
  (`stft.h:134-137`, `:171`, `:311`; `spectral_delay.h:366`, `:383-386`). SC-017 gates it.
  There MUST be **exactly one chunk grid and one counter: the input FIFO's own occupancy.** A chunk runs
  whenever that fill reaches `kFxSendChunkSamples`; **no separate free-running phase counter may exist**
  (plan D-3, ruled 2026-08-02). Within a continuously-engaged span the chunk phase is therefore a pure
  function of the samples consumed since engage and is **independent of how the host partitions them** —
  the property SC-017 tests; across a bypass excursion the phase is a function of engage history, not of
  the render start (plan **R-12**). The FIFOs MUST NOT be zeroed or re-phased on engage — they are cleared
  **only** by FR-008's deferred `reset()`.
- **FR-004.** `spectralDelay_.setDryWetMix(1.0f)` MUST be pushed during `setupProcessing()`
  **before** `spectralDelay_.prepare(...)`, so `prepare()`'s `snapParameters()` (`spectral_delay.h:206`,
  `:550`) snaps the dry/wet smoother to 1.0 rather than leaving it to creep from `kDefaultDryWet = 0.5f`
  at one smoother step per `process()` call (`:373`, `:389`). It MUST NOT be reachable from any parameter
  (C-2 clause 1, C-7 clause 3). Same ordering rule as `setFFTSize`.
- **FR-005.** `Processor::getLatencySamples()` MUST continue to return `reverb_->getLatencySamples()`
  unchanged (`processor.cpp:850-854`). Neither the send's `fftSize` latency nor the accumulator's
  `kFxSendChunkSamples` pipeline delay MAY be added, and the reported value MUST remain constant for a
  prepared configuration (C-2 clauses 3 and 5).
- **FR-006.** Stereo wandering MUST run in place on the bus: `globalMs_.process(l, r, l, r, n)` — the
  component documents in-place support (`midside_processor.h:181`) — followed by the per-sample azimuth gain
  pair.

### B. Bypass and transitions

- **FR-007.** When the send is **neither active nor draining** — i.e. `kFxDelayMixId == 0.0f`, the
  FR-023a freeze force is not engaged, and the FR-009a drain window has already ended — the processor MUST
  NOT call `SpectralDelay::process`, MUST NOT copy the bus into the send scratch, and MUST NOT read or
  write the scratch buffers. *(The "neither active nor draining" qualification is what makes FR-007,
  FR-009a and SC-011a mutually consistent; an earlier revision predicated both this requirement and
  FR-009a on `mix == 0` alone and they contradicted each other.)* There is **no free-running chunk-grid
  counter to exempt** (FR-003a, plan D-3): the sole grid is the input FIFO's own occupancy, which does not
  advance in this state precisely because the bus is not copied in. `sendChunkCountForTest()` (FR-041
  clause 7) MUST therefore stay unmoved across such a span — which is how SC-018 clause (e) observes this
  requirement inside the CI-gated suite, SC-002 being structurally blind to it (at mix 0 the mix loop adds
  `fxOut[i] * 0.0f`, so a fully-running send still leaves the bus bit-identical).
- **FR-008.** On the bypassed → active transition of the send, the processor MUST ramp the return gain
  from 0 to its target over `kFxReturnRampMs = 20` ms, and MUST call `spectralDelay_.reset()`
  (`spectral_delay.h:242`) **exactly once, on that block, before the first `process()` call, if and only
  if** (a) the send has been continuously bypassed for longer than `kFxSendDrainMs` (FR-009a) **and**
  (b) the engage was not forced by `kFxSpectralFreezeId` (FR-023a). In every other case the reset MUST NOT
  run: it clears `wasFrozen_`/`freezeCrossfade_` and the frozen spectrum buffers
  (`spectral_delay.h:256-257`, `:276-277`) along with all 2052 per-bin delay lines, so an unconditional
  reset destroys the tail and any captured freeze on every automation curve that touches zero.
  The reset trigger MUST be evaluated on a **fill-chunk boundary** — the accumulator's own
  `kFxSendChunkSamples` chunk-loop iteration, which by construction runs exactly once per 512 accumulated
  samples (FR-003a) — and never on a slice boundary, which is the same rule `processor.cpp:1154-1157`
  states for the master-gain ramp. **What that guarantees, stated as what the design delivers rather than
  as an aspiration** (plan D-3, ruled 2026-08-02): *the reset lands on a fill-chunk boundary; within a
  continuously-engaged span the chunk phase is a pure function of the samples consumed since engage and is
  therefore independent of how the host partitions them (the property SC-017 tests); across a bypass
  excursion the phase is a function of engage history.* An **absolute, free-running** grid whose "phase
  depends only on the render start" was claimed by an earlier revision and is **struck**: it is not
  deliverable alongside FR-007, which forbids writing the input FIFO while the send is neither active nor
  draining, so chunk boundaries sit at fixed offsets from *engage*. The residual — up to 511 samples of
  absolute drift per bypass excursion — is plan **R-12**. `reset()` is the **only** thing that ever clears
  the accumulator FIFOs.
- **FR-009.** On the active → bypassed transition, the return gain MUST ramp to 0 over
  `kFxReturnRampMs = 20` ms; the send MUST NOT be cut in one sample.
- **FR-009a.** After the FR-009 ramp completes, the send MUST **continue** to be processed for a drain
  window of `kFxSendDrainMs = 2000` ms (one `kMaxDelayMs`, `spectral_delay.h:92`) or until the
  accumulator's output peak falls below `kFxSendDrainFloor = 1e-6`, whichever comes first, and only then
  stop. Both constants MUST be named `inline constexpr`, never literals at a use site.
  **During the drain the send MUST be fed SILENCE**: the bus MUST NOT be copied into the input FIFO once
  the send is bypassed; zeros are pushed instead, and the component's own per-bin feedback decays the
  tail. The `kFxSendDrainFloor` early exit is therefore not a nicety — it is what bounds the worst-case
  cost of a bypass excursion **by energy rather than by wall clock**, so a mix parked at 0 does not hold
  the stage at its full SC-013 budget for a further 2 s. Feeding the live bus instead (the rejected
  alternative) would keep the tail perfectly coherent but would cost the full budget for 2 s after
  **every** bypass; pausing the send entirely would turn an excursion into a time-freeze, which is
  audibly a different effect from a delay.
  A mix excursion to 0 shorter than that window MUST leave the tail and any active freeze intact — with
  the attenuation the silent feed implies, which is why SC-011a's tolerance is ±2.0 dB and its control
  excursion is longer than the window (C-3, SC-011a).
- **FR-010.** When `kFxWidthId == 100.0f && kFxWanderDepthId == 0.0f && kFxAzimuthDepthId == 0.0f`, the
  processor MUST skip both `globalMs_.process()` and the azimuth multiply. The skip is **mandatory**, not
  an optimisation: running the stage at width 100 % is an algebraic identity but not a bit-identity
  (C-3), and SC-002 asserts exact equality.
- **FR-010a.** The wander-bypass predicate's engage and disengage transitions MUST be treated as
  transitions for click purposes: on **disengage → engage**, `globalMs_`'s width smoother has not advanced
  during the skipped blocks (`midside_processor.h:186-188` — it advances only inside `process`), so the
  processor MUST push the current width and snap the smoother before the first processed sample, and MUST
  crossfade the azimuth gain pair in over `kFxReturnRampMs = 20` ms. Both transitions are in SC-008's
  transition list.
- **FR-011.** `widthDrift_` and `azimuthDrift_` MUST advance every block via `processBlock(n)`
  (`brownian_drift.h:194`) regardless of any bypass state (C-3, final clause). Observable only through
  FR-041's `widthDriftBlockCountForTest()`; gated by SC-018.
- **FR-012.** Every bypass predicate MUST be evaluated **once per `process()` call**, never per slice, on the
  same hoisting rationale Phase 9 records at `processor.cpp:696-699` (parameter atomics cannot change within
  a `process()` call). Observable only through FR-041's `bypassPredicateEvalCountForTest()`; gated by
  SC-018.

### C. Parameters, packs and dropdowns

- **FR-013.** `plugin_ids.h` MUST gain the 16 enumerators of C-6 with exactly those values,
  `kEffectsParamRangeEnd = 1500`, and both band `static_assert`s extended (C-6).
- **FR-014.** A new `plugins/seraphis/src/parameters/effects_params.h` MUST implement the six-function
  contract in the shape `aether_params.h` uses (`:100`, `:161`, `:221`, `:274`, `:297`, `:347`) over a
  `struct EffectsParams` of `std::atomic<>` fields.
- **FR-015.** Every registered range and default MUST equal the C-6 row. Any range constant transcribed from
  a DSP header MUST be a named `inline constexpr` with the source line cited beside it — never re-typed at a
  use site (the discipline `aether_params.h:15-23` records).
- **FR-016.** `kFxDelayFeedbackId` MUST register a maximum of **0.95** (C-7 clause 2). Raising it to the
  component's 1.2 is forbidden.
- **FR-016a.** The processor MUST push the feedback **tilt-compensated**:
  `spectralDelay_.setFeedback(fb / (1.0f + std::abs(tilt)))`, where `fb` is `kFxDelayFeedbackId`'s
  denormalized value and `tilt` is `kFxDelayTiltId`'s, so that the worst per-bin loop gain
  `calculateTiltedFeedback` can produce — `(fb / (1 + |tilt|)) · (1 + |tilt|) = fb` — is bounded by the
  registered maximum 0.95 < 1.0 at **every** tilt setting (`spectral_delay.h:603-614`, `:99`). The push
  MUST be recomputed whenever *either* ID changes, and the divisor MUST be a named helper, not an inline
  literal. Without this the registration cap bounds nothing and SC-005 is unpassable at tilt = +1
  (C-7 clause 2).
- **FR-017.** Two new label tables MUST be added to `dropdown_mappings.h`: `kFxSpreadDirectionLabels`
  (**3** entries — `{"Low → High", "High → Low", "Center → Out"}`, in `SpreadDirection`'s declaration
  order, `spectral_delay.h:53-57`) and `kFxDelaySyncNoteLabels` (**10** entries, naming the periods the
  component **actually produces**, in index order:
  `1/64T, 1/64, 1/64D, 1/32T, 1/32, 1/32D, 1/16T, 1/16, 1/16D, 1/8T`).
  **Those ten strings are `kNoteValueDropdownMapping`'s first ten rows** (`core/note_value.h:136-164`),
  which is what `setNoteValue`'s index reaches: `noteValueIndex_` (`spectral_delay.h:532-534`) is consumed
  only by `dropdownToDelayMs(noteValueIndex_, tempo)` (`:330`) → `getNoteValueFromDropdown`
  (`note_value.h:182-190`) → that table. The mapping *"1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2,
  1/1"* documented at `spectral_delay.h:530` — and the *"Default to 1/8 note (index 4)"* comment at
  `:893` — are **factually wrong about the component's own behaviour** and MUST NOT be transcribed:
  index 4 is `1/32` (0.125 beats), not `1/8`, and the clamp to 9 caps the reachable synced range at
  `1/8T` (0.333 beats). Correcting the component instead is a `dsp/` behaviour change the *Non-goals*
  forbid (phase-owner ruling on plan OQ-1/D-1, 2026-08-02).
  The table MUST additionally carry a **compile-time** gate beside it tying a label to the period the
  component produces at that index — `dropdownToDelayMs` is `constexpr` (`note_value.h:259-265`) — so a
  permuted or aspirational table fails the build rather than a user's ears:
  ```cpp
  static_assert(Krate::DSP::dropdownToDelayMs(kFxDelaySyncNoteDefaultIndex, 120.0) == 125.0f,
                "FR-017: kFxDelaySyncNoteLabels[7] must name the period the component produces");
  ```
  where `kFxDelaySyncNoteDefaultIndex = 7` is the C-6 default (`1/16`, 0.25 beats = exactly 125.0 ms at
  120 BPM — `note_value.h:147`, `:226-241`) and MUST itself be a named `inline constexpr`, never a
  literal at the registration site.
  `kSyncNoteLabels` (8 entries, a **different** ordering, `dropdown_mappings.h:135`) MUST NOT be reused
  for ID 1419.
  `spectral_delay.h` declares **no** enumerator-count sentinel (unlike `ContinuousBody::kNumMaterials`,
  which `dropdown_mappings.h:198` asserts against), and Non-goals forbids adding one under `dsp/`.
  Therefore a plugin-local `inline constexpr std::size_t kSpreadDirectionCount = 3;` MUST be declared in
  `dropdown_mappings.h` with `spectral_delay.h:53-57` cited beside it, and the table gated by
  `static_assert(kFxSpreadDirectionLabels.size() == kSpreadDirectionCount, …)` **and** by
  `static_assert(kSpreadDirectionCount == static_cast<std::size_t>(SpreadDirection::CenterOut) + 1, …)`,
  which is what actually ties the plugin-local count to the enum.
- **FR-018.** `processParameterChanges()` MUST route `1400 ≤ id < kEffectsParamRangeEnd` to the effects pack
  through the existing `if (id < X)` ladder (`plugin_ids.h:245-252`) — never a 107-case switch.
- **FR-019.** `markDirty()` (`processor.cpp:1214-1228`) MUST classify ID 1400 as `Route::ENG` and IDs
  1410–1443 as the new `Route::FX`, added to the enum at `processor.cpp:131` and to `routeOf`
  (`:133-249`). `Route::FX` MUST bump **no** generation counter — neither `voiceParamGeneration_` nor
  `aetherParamGeneration_`. Observable only through FR-041's counters and Phase 9's existing
  `applyVoiceParamsCallCountForTest()` / `applyAetherParamsCallCountForTest()` (`processor.h:172`, `:186`);
  gated by SC-018.
- **FR-020.** No registered type may change at any pre-existing ID. The C-6 types are frozen on registration
  (`plugin_ids.h:184-190`); the frozen-type legend in that header MUST be extended with the 16 new rows.

### D. Effect wiring

- **FR-021.** ID 1400 MUST drive `SeraphisEngine::setOutputSaturation` (`seraphis_engine.h:672`), and
  **nothing else MUST write that setter.** *(Amended 2026-08-02 from "and nothing else" by the
  phase-owner ruling on plan OQ-2/D-2: two writers already shipped, so the original wording described a
  state the code was not in and the FR as written would have created a third.)* The contract is a
  **single writer over a composed value**:
  1. `pushEffectsParams()` is the **sole** writer, pushing `soft ? effectsParams_.saturation : 0.0f` on
     change only, where `soft` is `kSoftLimitId`. ID 2 keeps its shipped meaning as a **gate** (off ⇒ no
     output saturation at all); ID 1400 supplies the **amount** that gate passes.
  2. The on-change block in `pushGlobalParams()` (`processor.cpp:1090-1097`) MUST be **removed**. Its
     counter `engSoftLimitPushes_` / `engSoftLimitPushCountForTest()` (`processor.h:235`) is **kept** and
     incremented from the new site, so no Phase 9 cadence assertion moves.
  3. The prepare-time push at `processor.cpp:538-539` MUST be composed the same way and MUST seed the
     effects tracker exactly as step 4 already seeds `lastPushedPolyphony_` / `lastPushedSoftLimit_`
     (`:516-517`): the **value** seeded, the `…Valid_` flag left `false`, so the first `process()` still
     pushes once and counts once. Without this, a prepare with ID 1400 at 0.8 installs 0.15 at `:538` and
     converges only on the first `process()`; that is an **extension of the residual already recorded at
     `:526-537`**, disclosed here rather than inherited silently.
  Two independent on-change trackers on one setter is last-writer-wins with **no convergence**: toggling
  ID 2 off→on after setting ID 1400 to 0.8 silently reverts the engine to `kOutputSaturation = 0.15f`
  (`seraphis_engine.h:248`) until ID 1400 next moves. At the C-6 defaults the composed form is
  bit-identical to today (`soft == true`, `saturation == 0.15f == kOutputSaturation`), pushed the same
  number of times, so SC-002 is unaffected.
  No new `dsp/` setter may be added for drive — **amount-only, confirmed 2026-08-02**
  (Non-goals; RQ-3).
- **FR-022.** IDs 1411–1419 MUST drive the `SpectralDelay` setters named in C-6, on change only.
- **FR-023.** ID 1430 MUST drive `SpectralDelay::setFreezeEnabled` (`:479`) and MUST be independent of
  ID 1204 `kAetherFreezeId` — neither may read or write the other (C-4).
- **FR-023a.** ID 1430 MUST be independently reachable from the shipped defaults. While
  `kFxSpectralFreezeId` is engaged, the send MUST be treated as active with an effective return gain of
  `max(kFxDelayMixId, kFxFreezeMinReturnGain)` where `kFxFreezeMinReturnGain = 0.5` is a named constant,
  ramped over `kFxReturnRampMs`; the forced engage MUST suppress FR-008's `reset()`, so the capture
  happens at the moment of engagement rather than against buffers that were just cleared
  (`spectral_delay.h:256-257`, `:276-277`). Disengaging returns the send to `kFxDelayMixId` through
  FR-009/FR-009a. Without this the roadmap's first-named Phase 10 effect (line 466) is a dead control on
  the default patch, because `kFxDelayMixId`'s default 0 is the bypass predicate (C-4, SC-007 arm (a)).
- **FR-024.** IDs 1440–1443 MUST drive the width/azimuth construction of C-5, with the `cos`/`sin` pan pair
  evaluated at most **once per 64-sample control chunk** and the gains applied per sample through two
  `OnePoleSmoother`s at `kParamSmoothMs = 20.0f` ms (`processor.h:119`).
- **FR-024a.** The five wander constants C-5 fixes MUST each be a named `inline constexpr` with the header
  line that justifies it cited beside the declaration (FR-015), and MUST NOT appear as literals at a use
  site:
  1. `kWanderWidthSpanPercent = 50.0f` — the width span in percent per unit depth-scaled drift
     (`brownian_drift.h:100`, `kInternalStd = 0.5`; `midside_processor.h:65-67`).
  2. The azimuth gain smoothers' time constant, which MUST be `kParamSmoothMs` (`processor.h:119`).
  3. `kFxWidthDriftSalt` and `kFxAzimuthDriftSalt` (FR-026), which MUST be distinct.
  4. `kFxAzimuthCentreComp = 1.41421356f` (= `√2` = `1 / cos(π/4)`, `crossfade_utils.h:50-53`) — the
     centre normalisation **both** azimuth gains MUST be multiplied by (C-5). It is not optional: without
     it `equalPowerGains`' centre pair `gL = gR = 0.7071` drops the whole bus 3.01 dB the instant
     `kFxAzimuthDepthId` leaves 0, turning FR-010's skip boundary into a permanent steady-state level
     step. With it, `gL² + gR² = 2` at every position and centre is exactly unity per channel
     (phase-owner ruling on plan OQ-4/D-4, 2026-08-02).
  In addition: `kFxWanderDepthId` and `kFxAzimuthDepthId` MUST be applied as a **plugin-side multiply** of
  `BrownianDrift::getCurrentValue()` (`brownian_drift.h:212`) and MUST NOT be routed through
  `BrownianDrift::setDepth()` (`:159`), because FR-010's bypass predicate requires the depth to be a plain
  scalar comparable to 0 on the block the host wrote it, not a value still decaying through the drift's
  `kDriftOutputSmoothMs = 150 ms` output smoother (`:103`) — SC-002 requires that skip to be exact.
  `BrownianDrift::setMean(0.0f)` (`:165`) MUST be pushed explicitly to **both** drifts in
  `setupProcessing()`, so the width and azimuth mappings are symmetric about `base` and about centre.
- **FR-025.** `kFxWanderRateId` MUST be pushed to **both** drifts; the two MUST differ only by seed salt
  (C-5).
- **FR-026.** Both drift seeds MUST be derived from the shipped `kSeedValues` table
  (`dropdown_mappings.h:89`) XORed with two distinct named salts, and MUST be re-pushed whenever `kSeedId`
  changes — on the same path that already re-seeds the engine and reverb (`processor.cpp:1111-1112`).
- **FR-027.** `spectralDelay_.seedRng(kSeedValues[i])` (`spectral_delay.h:297`) MUST be called after
  `prepare()` and on every `kSeedId` change, followed by `reset()`, because the component otherwise seeds
  from `reinterpret_cast<uintptr_t>(this) ^ sampleRate` (`:223-224`) and re-randomizes `2 × numBins`
  phases on every `reset()` (`:279-284`). The `kSeedId` path runs on the **audio thread**, inside
  `pushGlobalParams()` (`processor.cpp:1105-1112`, reached from `process()` at `:693`), and `seedRng` +
  `reset()` together draw ~2052 RNG values and reset 2052 delay lines — a second bounded burst site
  alongside FR-008's. SC-011's harness MUST therefore automate `kSeedId` as well, and the seed-change
  block is held to the same per-block ceiling.

### E. Lifecycle, RT safety, state

- **FR-028.** All Phase 10 members MUST be prepared in `setupProcessing()` and MUST allocate nowhere else:
  `spectralDelay_.prepare(sr, kMaxBlockSamples)` (`spectral_delay.h:131`, with
  `SeraphisEngine::kMaxBlockSamples = 2048`, `seraphis_engine.h:215`), `globalMs_.prepare(float(sr),
  kMaxBlockSamples)` (`midside_processor.h:96`), `widthDrift_.prepare(sr)` / `azimuthDrift_.prepare(sr)`
  (`brownian_drift.h:121`), and the send scratch vectors sized to `kMaxBlockSamples`.
- **FR-029.** Nothing on the audio thread may allocate, lock, throw or perform I/O. Scratch buffers MUST be
  indexed through `.data()`/`operator[]`, never `.at()` (the discipline `processor.cpp:1135-1137` records).
- **FR-030.** The processor MUST construct a `Krate::DSP::BlockContext` (`block_context.h:57`) **once per
  `process()` call**, populated from `data.processContext` with `sampleRate` and `tempoBPM`, and pass it to
  `SpectralDelay::process`. The tempo sample point MUST be the one Phase 9 already uses — once per
  `process()`, never per slice (`processor.cpp:1563`). The validity guard MUST be Phase 9's **three-part**
  guard verbatim in shape — `ctx != nullptr && (ctx->state & Vst::ProcessContext::kTempoValid) != 0 &&
  ctx->tempo > 0.0` (`processor.cpp:1585-1586`) — writing `120.0` into `BlockContext::tempoBPM` otherwise.
  Relying on the component's own fallback is **not** sufficient: it fires only on `tempo <= 0.0`
  (`spectral_delay.h:325-327`), and a host may leave `ProcessContext::tempo` holding a stale positive
  value while `kTempoValid` is clear.
- **FR-031.** `kCurrentStateVersion` MUST become 3 and `kStateVersion2` MUST be added as a named constant;
  the `static_assert` chain at `plugin_ids.h:285-286` MUST be extended to
  `kStateVersion1 < kStateVersion2 < kCurrentStateVersion`.
- **FR-032.** The 16 effects fields MUST be appended after all version-2 fields, preserving the strict
  byte-prefix property (C-8), and MUST be saved/loaded by `saveEffectsParams` / `loadEffectsParams` /
  `loadEffectsParamsToController`.
- **FR-033.** Loading a version-1 or version-2 stream MUST leave every effects field at its C-6 default and
  MUST NOT fail.
- **FR-034.** `pushAllSurfaces()` (`processor.h:270`) MUST re-push the effects surface, so a `setState()`
  arriving after `setupProcessing()` reaches the DSP (Phase 9 Q2).
- **FR-035.** `reset()`/`setActive(false)` paths MUST clear `spectralDelay_`, `globalMs_`, both drifts and
  the return-gain ramp, leaving the chain in the same state a fresh `setupProcessing()` would.

### F. Metadata and tests

- **FR-036.** `editor.uidesc` MUST gain 16 `<control-tag>` entries inside the existing `<control-tags>` block
  (`editor.uidesc:20-121`), grouped under an `<!-- Effects (1400+) -->` comment. **No layout change** — Phase
  11 owns layout.
- **FR-037.** The controller MUST register all 16 IDs with the C-6 types, ranges, defaults and units, and
  MUST implement `getParamStringByValue`/`getParamValueByString` for them through `formatEffectsParam`.
- **FR-038.** `plugins/seraphis/CLAUDE.md`'s parameter-band table MUST have its `1400+` row updated from
  "10" to "10 — shipped", and `CHANGELOG.md` MUST gain the matching entry in the same change as any
  `version.json` bump.
- **FR-038a.** Growing the surface 91 → 107 breaks checked-in assertions in **four** test files. The
  **nine obligations below MUST all be discharged in this phase**. *(An earlier revision enumerated only
  the first four and
  missed `param_continuity_test.cpp` entirely; that file's `static_assert`s fail the build the moment the
  16 IDs register, so an implementer who works from the short list stops at a compile error with no
  guidance. FR-038b decides the classification the fixes need.)*
  1. `REQUIRE(controller.getParameterCount() == 91)` → `== 107`
     (`plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp:242` and `:254`);
  2. `CHECK(controller.getParameterCount() == 91)` → `== 107`
     (`plugins/seraphis/tests/unit/parameter_surface_test.cpp:480`);
  3. `static_assert(kNonDefaultTable.size() == 91, "SC-009: the table is EXHAUSTIVE over the
     91-parameter surface")` → `== 107`, **with its message text updated to say 107**
     (`plugins/seraphis/tests/integration/param_perf_test.cpp:902`). This one does **not** fail to
     compile on its own — the table is hand-written — so an unrevised SC-014 would silently ship a table
     whose own self-description had become false.
  4. The 16 effects rows MUST be **added** to `kNonDefaultTable` at their **most-expensive end**, under
     the table's existing rule (`RowClass::NonDefault`, `param_perf_test.cpp:682-687`). Any row whose
     most-expensive end coincides with its registered default MUST be declared
     `RowClass::CoincidesWithDefault` and the corresponding `countRows(...)` assertion at `:904-910`
     updated. This is what makes SC-014 measure the roadmap's "everything on" scenario rather than a
     defaults scenario.
     **"Most-expensive end" is defined OPERATIONALLY: the candidate value that maximises measured block
     wall time.** For the four discrete rows (1413 spread direction, 1418 sync, 1419 sync note, 1430
     freeze) and the three continuous rows with no obvious CPU gradient (1411 delay time, 1412 spread,
     1415 tilt), each candidate MUST be measured once and the **costlier** value transcribed into the
     table, with the measurements recorded in a **BASELINE PROVENANCE banner beside the table**, in the
     shape of `param_perf_test.cpp:65-84`. A structurally-argued choice is not sufficient: SC-014's whole
     claim is that the table *is* the worst case, and only a measurement makes that provable.
  5. `static_assert(std::size(kContinuityMechanism) == 85, "SC-005: 91 registered, less kSeedId and the
     five CFG IDs")` (`plugins/seraphis/tests/integration/param_continuity_test.cpp:478-479`) → **101**,
     with the message text becoming `"SC-005: 107 registered, less kSeedId and the five CFG IDs"`. All 16
     effects IDs are in scope; none is exempt (FR-038b clause 4).
  6. `static_assert(std::size(kClassBIds) == 9, "plan 3.5.3: class (b) is EXACTLY nine IDs …")`
     (`param_continuity_test.cpp:501`) → **12**, with the message text updated to say twelve and to name
     the three Phase 10 additions (FR-038b clause 2).
  7. `REQUIRE(count == 91)` → `== 107` at **both** sites — `param_continuity_test.cpp:712` (the
     `Seraphis_ContinuityMechanism_CoversEveryInScopeId` case) and `:811` (the per-ID automation sweep).
  8. The both-ways exhaustiveness gate `CHECK(table == expected)` (`param_continuity_test.cpp:749`) MUST
     be satisfied by adding all 16 rows to `kContinuityMechanism`; it fails in **both** directions, so
     neither a missing row nor a stray one can be shipped.
  9. The per-ID automation sweep `for (const ParamID id : registered) { … checkContinuity(…) }`
     (`param_continuity_test.cpp:822-826`) renders **every registered ID** under Phase 9's 1.5× click
     bound. All 16 effects IDs are therefore measured by it, in addition to SC-008's six transitions;
     no effects ID is added to `kExemptIds` (`:488`).
- **FR-038b.** Each of the 16 new IDs MUST be given a `ContinuityRow` in `kContinuityMechanism`
  (`param_continuity_test.cpp:141-167`) with a `Class`, an `Evidence`, a **mandatory file:line
  `citation`** and a `smoothMs`, split as follows:
  1. **Class (a) `ComponentInternal` — the thirteen component-backed IDs**, `smoothMs = 0.0f`. The
     citation is not optional decoration: the gate at `param_continuity_test.cpp:748-751` exists precisely
     to reject an uncited class-(a) claim, and the table's own remedy rule (`:135-140`) is
     **one-directional** — an ID may not be moved *into* class (a) without a file:line citation of the
     smoother that covers it. Each row's `Evidence` MUST be the mechanism that actually covers it, which
     is **not** `Smoother` for all of them:

     | ID | `Evidence` | Citation — read this session |
     |---|---|---|
     | 1400 | `Smoother` | `SeraphisEngine::setOutputSaturation` (`seraphis_engine.h:670-675`) forwards to `TapeSaturator::setSaturation`, which sets `saturationSmoother_`'s target (`tape_saturator.h:248-252`), configured at `kDefaultSmoothingMs` (`:160`). |
     | 1411 | `Smoother` | `baseDelaySmoother_.setTarget` (`spectral_delay.h:427`). |
     | 1412 | `Smoother` | `spreadSmoother_.setTarget` (`spectral_delay.h:434`). |
     | 1413 | `Structural` | `setSpreadDirection` is a **plain assignment** (`spectral_delay.h:440`); the enum is read only inside `calculateBinDelayMs`'s `switch` (`:587-596`), so the change is a re-mapping of per-bin delays whose continuity comes from the interpolated per-bin reads and the FFT overlap-add. **No smoother exists — do not cite one.** |
     | 1414 | `Smoother` | `feedbackSmoother_.setTarget` (`spectral_delay.h:462`). |
     | 1415 | `Smoother` | `tiltSmoother_.setTarget` (`spectral_delay.h:470`). |
     | 1416 | `Smoother` | `diffusionSmoother_.setTarget` (`spectral_delay.h:491`). |
     | 1417 | `Smoother` | `stereoWidthSmoother_.setTarget` (`spectral_delay.h:514`). |
     | 1418 | `Smoother` | `setTimeMode` is a plain assignment (`spectral_delay.h:525`), but the mode is consumed inside `process()`, which pushes the synced time through the **same** `baseDelaySmoother_.setTarget(syncedMs)` (`:322-336`) that ID 1411 uses, read at `:646`. |
     | 1419 | `Smoother` | `setNoteValue` is a plain assignment (`spectral_delay.h:533`); same path as 1418 (`:330`, `:336`, `:646`). |
     | 1430 | `Ramp` | `setFreezeEnabled` is a plain assignment (`spectral_delay.h:480`), but engagement crossfades over `kFreezeCrossfadeTimeMs = 75.0f` ms (`:906`), whose per-frame increment is derived at `:210-212` and applied at `:698-706`. **`Smoother` is the wrong evidence here.** |
     | 1440 | `Smoother` | `MidSideProcessor::setWidth` sets `widthSmoother_`'s target (`midside_processor.h:133-136`), advanced per sample inside `process` (`:186-188`). See FR-010a: the smoother does **not** advance while the wander stage is skipped. |
     | 1442 | `CoefficientOnly` | `BrownianDrift::setSmoothness` clamps and calls `updateCoefficients()` (`brownian_drift.h:152-155`) — it retunes the walk's correlation time and never steps its output. |
  2. **Class (b) `ProcessorSmoothed` — IDs 1410, 1441 and 1443**, `smoothMs = kParamSmoothMs = 20.0f`
     (`processor.h:119`). These three are **plugin-owned quantities with no component smoother to cite**:
     1410 is the send return gain, 1441 and 1443 are plugin-side depth multipliers (FR-024a). Each gets a
     processor-side `Krate::DSP::OnePoleSmoother` delivered on the engine's absolute 64-sample
     control-chunk grid, exactly as Phase 9's nine class-(b) IDs are.
     **ID 1410's class-(b) smoother IS the FR-008/FR-009 return-gain ramp — one smoother, not two.**
     `kFxReturnRampMs = 20` ms and `kParamSmoothMs = 20.0f` ms are the same duration by construction, and
     a second smoother in series on the same quantity would double the effective time constant and break
     both requirements.
  3. Consequently `kClassBIds` grows 9 → 12 (`param_continuity_test.cpp:495-501`) and, on the
     implementation side, `classBSmoothers()`'s return type widens from
     `std::array<Krate::DSP::OnePoleSmoother*, 9>` to `…, 12>` (`processor.h:312`), with
     `setParamSmootherTargets()` (`:285`), `advanceParamSmoothers()` (`:301`) and
     `anyClassBSmootherUnsettled()` (`:327`) updated to cover the three new entries.
  4. **Row tally: 13 class (a) + 3 class (b) = 16**, so `kContinuityMechanism` goes 85 → **101** rows
     (FR-038a clause 5) and `kClassBIds` goes 9 → **12** (clause 6). **No effects ID is added to
     `kExemptIds`** (`param_continuity_test.cpp:488`): the exemption exists for `kSeedId` and the five CFG
     IDs, whose continuity bound is unsatisfiable by construction, and no Phase 10 ID is in that
     position. Exempting the band — the rejected alternative — would leave the effects surface covered
     only by SC-008's six named transitions and not by the per-ID sweep.
- **FR-039.** New tests MUST be registered in `plugins/seraphis/tests/CMakeLists.txt` under the existing
  `seraphis_tests` target; **no new test executable is created**, which is precisely why SC-002 and SC-012
  are same-binary runtime comparisons and not two-build comparisons — a second variant of `processor.cpp`
  with the Phase 10 call sites `#if`-ed out cannot coexist in `seraphis_tests`, which already compiles the
  plugin `.cpp`s a second time into the test exe (roadmap lines 418–424). Their mechanism is FR-040.
- **FR-040.** `Processor` MUST declare `friend struct detail::SeraphisEffectsStageBypassProbe;` — a
  **test-TU-only** probe in the shape of Phase 9's `detail::SeraphisParamSmootherBypassProbe`
  (`processor.h:246`, `specs/seraphis-phase9-parameters/spec.md` → FR-059a) — **whose only capabilities
  are the three below**, all test-TU-only and inert on every ship path:
  1. make `renderSlice` skip C-1 steps 4 and 5 **at runtime** — forced by this FR, SC-002 and SC-012;
  2. run C-1 step 5 **after** step 6 — forced by SC-003(a)'s *mandatory* positive control;
  3. snap the FR-008/FR-009 20 ms return-gain ramp to **instant** — forced by SC-008's *mandatory*
     positive control (b). This is the mechanism Phase 9 already ships for its own class-(b) ramps
     (`paramSmootherBypass_`, consumed at `processor.cpp:1767-1771`), i.e. a flag on an existing branch,
     not a new one.

  *(Amended 2026-08-02, plan D-7. An earlier revision said the probe's "**sole** capability" was
  capability 1 — but two of this spec's own success criteria mandate the other two, so an implementer
  working from that wording builds a one-capability probe and then cannot write two mandatory positive
  controls. This is an editorial correction to an FR the criteria already overrode, not a scope change.)*
  Nothing in `plugins/seraphis/src/` other than this declaration may reference the probe, no shipping
  `process()` path may take any of the three branches, and it MUST NOT be defined outside the test
  translation unit. It is what SC-002, SC-003(a)'s positive control, SC-008's positive control (b) and
  SC-012 are measured against.
- **FR-041.** `Processor` MUST declare **seven** test-only read surfaces beside Phase 9's
  (`processor.h:162-240`), **plus one truncation flag**; all but clause 6 are plain
  `std::size_t`/`double`/`bool` members written only from the audio thread and read only after the render
  completes. *(Amended 2026-08-02, plan D-8: an earlier revision pinned six surfaces and counted clause
  1's divisor per slice. No threshold moves — the seam set is strengthened, not re-sized.)*
  1. `effectsStageNsForTest()` — accumulated wall time of C-1 steps 4 and 5 only, from a scoped timer
     around them, accumulated **per slice** — together with its divisor
     **`effectsStageProcessCallsForTest()`, which MUST be incremented exactly once per `process()` CALL,
     never once per slice**, so a per-block figure can be derived. This is the measurement point for
     SC-012 and SC-013; a whole-render delta cannot be, because the whole-render figure's own run-to-run
     spread is 377 740 ns (`param_perf_test.cpp:78-84`), ~35× the SC-012 threshold.
     **The per-call divisor is load-bearing, not bookkeeping:** SC-012 and SC-013 compare against
     **per-block** budgets, but `renderSlice()` runs once per *slice* — the loop subdivides on every MIDI
     event (`processor.cpp:759-786`), on the 2048 cap (`:792`) and, while any class-(b) smoother is
     unsettled, on the absolute 64-sample grid (`:811-815`), which after FR-038b widens
     `classBSmoothers()` to twelve is every block for ~20 ms after any 1410/1441/1443 automation point and
     for the whole of every send engage/bypass ramp. SC-012's render is Phase 9's SC-009 MIDI script
     (several slices per block) and SC-013's is *required* to carry such an automation point, so a
     per-slice divisor would report up to **8×** below the true per-block cost on a 512-sample block and
     make SC-013's 2.5 % budget — which SC-014's 25 % arithmetic depends on — structurally unable to fail.
  2. `spectralDelayResetCountForTest()` — FR-008.
  3. `effectsPushCountForTest()` — FR-022/FR-024 pushes actually issued.
  4. `widthDriftBlockCountForTest()` — FR-011.
  5. `bypassPredicateEvalCountForTest()` — FR-012.
  6. **`preOutputTapForTest()` — the PRE-OUTPUT-STAGE TAP.** A test-only copy of the stereo bus taken
     **immediately before `engine_->processOutputStage()`** (C-1 step 6, `processor.cpp:1170`), into a
     prepare-time buffer pair sized to `SeraphisEngine::kMaxBlockSamples = 2048`
     (`seraphis_engine.h:215`), exposed as a read-only span in the same shape as Phase 9's `*ForTest()`
     accessors. It exists because **every** isolated-return criterion is otherwise measured through
     `TapeSaturator` + `TruePeakLimiter`, which are nonlinear in the quantity being measured: SC-003's
     difference render `render(mix = 1) − render(mix = 0)` differences two *summed buses*, and SC-005,
     SC-007, SC-011a and SC-019 inherited that definition without even SC-003(b)'s linear-region
     precondition. **SC-003(b) onward, SC-005, SC-007, SC-011a and SC-019 now measure at this tap**, so
     the output stage is out of the measured path for every one of them. **The one deliberate carve-out
     is SC-003(a)**, which measures the true plugin output because the limiter *is* its subject —
     reading it before `processOutputStage` would make the clause vacuous; it uses the tap only to
     establish its precondition (that the pre-limiter peak provably exceeds
     `kLimiterCeilingLin`). The alternative of propagating a linear-region
     precondition to all five was rejected: `TapeSaturator` at saturation 0.15 is still level-dependent,
     and the 5 % spectral-centroid clauses are the least robust to it. Putting a probe-driven branch on
     the output stage itself was also rejected — a copy behind a test-only accessor touches no shipping
     control flow.
  7. **`sendChunkCountForTest()`** — incremented exactly once per `spectralDelay_.process()` call.
     **This is the only CI-gated observation of FR-007.** That prohibition is otherwise visible only
     through SC-012's `[.perf]`-tagged threshold, which is explicitly outside the CI gate, and SC-002 is
     structurally blind to it: at mix 0 the mix loop adds `fxOut[i] * 0.0f`, so a fully-running send still
     leaves the bus bit-identical and `max |a − b| == 0.0f` still holds. One `std::size_t` moves FR-007
     into the CI-gated suite (SC-018 clause (e)).

  **Plus `preOutputTapTruncatedForTest()` — a `bool`**, `true` when the most recent `process()` call
  delivered more samples than the tap buffers hold. Clause 6 pins those buffers to
  `SeraphisEngine::kMaxBlockSamples = 2048`, but the processor explicitly supports larger host blocks
  (`processor.cpp:787-792` — *"This is the branch a host block larger than 2048 enters"*), so without the
  flag a 4096-sample block silently yields a half-length tap and every tap-based criterion measures half a
  render with no error signal. **Every criterion that measures at the tap MUST assert the flag reads
  `false` for its render, and MUST use blocks ≤ 2048**; one test `SECTION` MUST additionally exercise a
  block **larger** than 2048 and assert the flag then reads `true` with a 2048-sample tap, so the flag is
  a real gate rather than an unread member.
  The scoped timer and the tap MUST compile to nothing observable in the shipping configuration beyond
  two clock reads, an add and one buffer copy, and MUST NOT allocate, lock or throw (FR-029). The tap's
  buffers are allocated in `setupProcessing()` like every other Phase 10 member (FR-028).

---

## Success Criteria

Each criterion names its metric, threshold and the test that measures it.

**One protocol governs every CPU figure in this spec.** SC-011, SC-012, SC-013 and SC-014 are each
measured under the **seven-run fresh-boot cold protocol** stated in full at SC-013 and referred to
throughout as *the SC-013 protocol*: a **fresh-boot, idle machine**, **seven** consecutive whole-suite
runs of `seraphis_tests.exe "[.perf]"`, each figure itself a **best-of-16**, and the **worst of the
seven** is the reported figure. That is verbatim the protocol Phase 9's shipped SC-009 baseline was
pinned under on 2026-08-02 (`param_perf_test.cpp:133-156`). Like every `[.perf]` arm, none of these four
criteria is part of the CI gate; the everyday gate is the defaults configuration (SC-012).

*(A previous revision stated a blanket "best-of-16, worst of **six** consecutive runs" rule here, citing
the BASELINE PROVENANCE banner at `param_perf_test.cpp:65-84`. That banner is the **withdrawn T028 hot
dataset**, which this spec disowns in C-3 and RQ-1 and on which no argument here rests — so a
measurement rule anchored to it contradicted the very criteria it governed. It is struck.
`param_perf_test.cpp:65-84` survives in this spec only as the **formatting shape** a BASELINE PROVENANCE
banner takes — never as a measurement protocol.)*

- **SC-001 — Full surface registered.** The controller reports exactly **107** parameters; all 16 new IDs are
  present with the C-6 type, min, max, default and step count. A normalized→plain→normalized round-trip over
  {0, 0.25, 0.5, 0.75, 1} for every new ID returns the input within `1e-6`, and every dropdown ID returns a
  distinct non-empty string for every index. **Additionally**, the label strings themselves are asserted
  literally, in order: `kFxSpreadDirectionLabels` equals `{"Low → High", "High → Low", "Center → Out"}` and
  `kFxDelaySyncNoteLabels` equals `{"1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16",
  "1/16D", "1/8T"}` — `kNoteValueDropdownMapping`'s first ten rows (`core/note_value.h:136-164`), i.e. the
  periods `dropdownToDelayMs(index, tempo)` actually produces for `setNoteValue`'s 0–9 index
  (`spectral_delay.h:330`, `:532-534`) — **not** the ten strings documented at `spectral_delay.h:530`,
  which name periods the component does not produce (plan OQ-1/D-1, ruled 2026-08-02). ID 1419's default
  index is asserted to be **7** (`1/16`). Without this clause a permuted or aspirational table — the most
  likely error, since the existing `kSyncNoteLabels` is a *different* 8-entry ordering
  (`dropdown_mappings.h:135`) and the component's own doc comment is a *third* ordering — passes unchanged
  while the label a user reads does not describe the delay they hear. Paired with FR-017's `constexpr`
  build-time gate and with SC-019, which ties index ↔ label ↔ measured period together.
  *Test:* `plugins/seraphis/tests/unit/param_denorm_test.cpp` — `"Seraphis effects parameters denormalize"`.

- **SC-001a — Phase 9's continuity contract still covers the whole surface.** `kContinuityMechanism` has
  exactly **101** rows and `kClassBIds` exactly **12** (FR-038a clauses 5–6), the both-ways exhaustiveness
  gate `CHECK(table == expected)` (`param_continuity_test.cpp:749`) passes with all 16 effects IDs
  present and none stray, every effects row carries a non-empty file:line `citation`
  (`param_continuity_test.cpp:141-167`, the gate at `:748-751`), every class-(b) row's `smoothMs` equals
  `kParamSmoothMs` and every class-(a) row's is `0.0f`, and **no effects ID appears in `kExemptIds`**
  (`:488`). The per-ID automation sweep (`:822-826`) then measures all 16 under Phase 9's unchanged 1.5×
  click bound, in addition to SC-008's six transitions. This criterion is what turns FR-038b from a
  classification claim into a checked one.
  *Test:* `plugins/seraphis/tests/integration/param_continuity_test.cpp` (existing cases, tables grown).

- **SC-002 — Negative control: defaults change nothing.** **One build, one process, one `Processor`
  instance.** Two renders of an identical 10 s MIDI sequence (8 voices, all Phase 9 parameters at their
  shipped defaults, all 16 effects parameters at their C-6 defaults): one with FR-040's
  `detail::SeraphisEffectsStageBypassProbe` **engaged** (C-1 steps 4 and 5 skipped) and one with it
  **disengaged**. The two are **sample-identical**, i.e. `max |a[i] − b[i]| == 0.0f` over every sample of
  both channels. Exact equality is legitimate *here specifically* because both sides are the same
  compiled code path on the same instance — identical codegen, so the only question asked is whether
  C-3/C-7's default path is the algebraic identity it claims (bypassed send, skipped M/S, saturation at
  the same 0.15). It is **not** a golden, and it is not a cross-build comparison: a Phase 9-vs-Phase 10
  *build* comparison would demand bit-identical FP across toolchains, which
  `tests/test_helpers/render_fingerprint.h:20-30` measures at 2.9e-5 per sample and the cross-cutting
  constraints forbid (roadmap line 555) — and it is not implementable under FR-039 anyway.
  *Test:* `plugins/seraphis/tests/integration/effects_chain_test.cpp` — `"Effects defaults are a no-op on the same build"`.

- **SC-003 — Chain order is what C-1 says.** All three clauses are measured on the **isolated send
  return**, whose definition is fixed here and inherited verbatim by SC-005, SC-007, SC-011a and SC-019:

  > **Isolated send return** — the sample-aligned difference `render(kFxDelayMixId = 1) −
  > render(kFxDelayMixId = 0)`, taken on the same instance with the same MIDI script and the same seed,
  > **read from FR-041 clause 6's `preOutputTapForTest()`**, i.e. from C-1 step 5's output, *before*
  > `engine_->processOutputStage()`. Every render that reads the tap MUST use blocks **≤ 2048** and MUST
  > assert `preOutputTapTruncatedForTest()` (FR-041) is `false` — otherwise a larger host block silently
  > yields a half-length tap and the criterion measures half a render with no error signal (plan D-8).

  Reading at the tap rather than at the plugin output is what makes the difference a **linear** operation
  on the quantity being measured: both renders otherwise pass through `TapeSaturator` +
  `TruePeakLimiter` (`processor.cpp:1170`), so a difference of two summed buses is a difference of two
  nonlinearly-shaped signals, and the ±1.0 dB / ±2.0 dB / 5 %-centroid clauses downstream inherit that
  error. The tap therefore removes the output stage from **every** isolated-return measurement in this
  spec. *(SC-003(a) is the one clause that deliberately measures the true plugin output instead — its
  whole subject is the limiter.)* With the send at mix 1.0, delay time 0, feedback 0 and the wander at
  width 200 %:
  - **(a) step 5 precedes step 6.** *This clause alone is measured on the true plugin output*, because
    the limiter is its subject. *Precondition:* the probe signal is driven so the **pre-limiter** peak —
    directly readable at `preOutputTapForTest()` — exceeds `kLimiterCeilingLin = 0.8912509f`
    (`param_flow_test.cpp:63`), i.e. the limiter is provably in gain reduction; otherwise the clause is
    vacuous. *Assertion:* the raw sample peak of the output still respects the ceiling (SC-006's bound).
    *Positive control (mandatory):* with FR-040's probe configured to run step 5 **after** step 6, the
    same render must **fail** this clause.
  - **(b) step 4 follows step 3.** Measured at the tap, so the proportionality claim is not taken through
    gain reduction by construction. *Precondition (retained as a redundant guard, since the tap already
    excludes the limiter):* master gain is held in the limiter's linear region (`kSoftLimitId` off and
    the probe level chosen so the output peak stays ≥ 3 dB under the ceiling) —
    `processor.cpp:1157-1161` records that master gain 2.0 drives peaks to ~1.78 and makes exactly this
    kind of proportionality claim unsatisfiable at the output.
    *Assertion:* doubling master gain scales the isolated return's RMS by 6.02 dB ± **0.1 dB**.
  - **(c) step 4 precedes step 5.** *Assertion:* the **isolated return's** own M/S side energy scales with
    `kFxWidthId` across {0 %, 100 %, 200 %} monotonically and within 0.5 dB of the ideal factor. Measured
    on the isolated return, not the bus, so a dry-only width change cannot satisfy it.
  *Test:* `effects_chain_test.cpp` — `"Effects chain order matches C-1"`.

- **SC-004 — Reported latency is unchanged and constant.** `getLatencySamples()` returns the identical value
  before and after every Phase 10 parameter is driven to a non-default value, and equals
  `reverb_->getLatencySamples()` in every case. Sweeping `kFxDelayMixId` 0 → 1 → 0 produces **zero**
  `restartComponent(kLatencyChanged)` calls.
  *Test:* `plugins/seraphis/tests/unit/processor_bus_test.cpp` — `"Phase 10 does not change reported latency"`.

- **SC-005 — The spectral delay decays at its registered maximum, at BOTH tilt extremes.** Every RMS
  below is the **isolated send return as SC-003 defines it**, read at FR-041 clause 6's
  `preOutputTapForTest()`, so a 60 dB decay is not confounded by the output stage's level-dependent
  shaping. Three parts:
  1. *Derived bound (compile-time / arithmetic, no render).* With FR-016a's compensation, the worst
     per-bin loop gain `calculateTiltedFeedback` can return is
     `(fb / (1 + |tilt|)) · (1 + |tilt|) = fb`; the test asserts, over `tilt ∈ {−1, −0.5, 0, +0.5, +1}` and
     `fb = 0.95`, that the maximum over all 513 bins of
     `clamp((fb/(1+|tilt|))·(1 + tilt·(b/512 − 0.5)·2), 0, kMaxFeedback)` is **< 1.0**. Without FR-016a
     this bound is 1.2 and the decay clauses below are unpassable, which is the defect C-7 clause 2 now
     records.
  2. *Decay, as **two separate runs** — tilt = −1 and tilt = +1.* `kFxDelayFeedbackId` at its registered
     maximum 0.95, diffusion 1.0, and **`kFxDelayTimeId` pinned at `kDefaultDelayMs = 250` ms**
     (`spectral_delay.h:93`) with spread 0. Pinning the delay time is required because the criterion is
     delay-time dependent: feedback applies once per delay-line traversal (`:751-767`), so 120 s at 0.95
     gives −214 dB at 250 ms but only −53 dB at 1000 ms and −27 dB at `kMaxDelayMs = 2000` ms. Each run:
     a 1 s burst followed by 120 s of silence has an isolated-return RMS that falls **≥ 60 dB** below its
     peak within 120 s, i.e. within 480 delay-line traversals.
  3. *Shape.* Over 5 s windows after the first 5 s, no window's RMS rises more than **0.5 dB** above its
     predecessor. (A tolerance, not strict monotonicity: a dispersive per-bin decay with diffusion is not
     guaranteed monotone.)
  No sample is non-finite (checked by bit pattern, not `std::isnan` — the `-ffast-math` rule).
  *Test:* `effects_chain_test.cpp` — `"Spectral delay decays at registered max feedback"`.

- **SC-006 — True-peak ceiling holds with every effect active.** Over a 30 s render with all 16 effects
  parameters at maxima (delay mix 1.0, feedback 0.95, width 200 %, wander and azimuth depth 1.0, saturation
  1.0), master gain at maximum and 8 voices held, no **raw output sample** exceeds
  `kLimiterCeilingLin = 0.8912509f` — the linear form of `TruePeakLimiter::kDefaultCeilingDb = −1.0 dBFS`
  (`true_peak_limiter.h:46`) — allowing `kCeilingAllowanceDb = 0.1f`, exactly as every shipped Seraphis
  ceiling assertion does (`param_flow_test.cpp:59-63`, `processor_audio_test.cpp:148-153`, applied at
  `:810`). The measurement is deliberately **not** an independently-written 4× reconstruction: the limiter
  bounds the signal at *its own* 4× oversampled resolution through its internal polyphase upsampler
  (`true_peak_limiter.h:38-42`, `:110-125`), and a differently-written test-side interpolator reports
  inter-sample peaks a fraction of a dB apart from it, turning a correct implementation into a failure.
  *Test:* `effects_chain_test.cpp` — `"Effects at maxima respect the true-peak ceiling"`.

- **SC-007 — Spectral freeze captures and holds.** Every quantity is measured on the **isolated send
  return** (SC-003's difference render, read at `preOutputTapForTest()`), so neither the nonlinear output
  stage nor the still-decaying Aether tail can be mistaken for the freeze — the ±1.0 dB and 5 %-centroid
  clauses below are the least robust in the spec to a level-dependent nonlinearity, which is why the tap
  is mandatory here rather than a convenience. Two arms:
  - **(a) From the C-6 DEFAULTS** — `kFxDelayMixId = 0`, i.e. the send bypassed, which is the shipped
    patch. Engaging `kFxSpectralFreezeId` alone MUST produce audible held output: the isolated return's
    RMS 5 s after note-off is **> −60 dBFS** and is within **±1.0 dB** of the RMS measured 200 ms after
    engagement. This arm is what proves FR-023a; without it the roadmap's first-named Phase 10 effect is
    a dead control on the default patch.
  - **(b) With the send already at mix 1.0** and a held chord, engaging `kFxSpectralFreezeId` and then
    releasing all notes: the isolated return's RMS 5 s after note-off is within **±1.0 dB** of the RMS
    measured 200 ms after the freeze engaged — a measurement point deliberately **> `kFreezeCrossfadeTimeMs
    = 75 ms`** (`spectral_delay.h:906`), so the crossfade is complete with 125 ms of margin — and the
    return's spectral centroid over the same interval moves by less than **5 %**. With freeze off, the
    same measurement decays by **≥ 30 dB**.
  Both arms additionally assert the FR-023a ordering explicitly: toggling 1430 while the send is bypassed
  must capture at *engage* time, not at toggle time.
  *Test:* `effects_chain_test.cpp` — `"Spectral freeze holds the Aether tail"`.

- **SC-008 — Every Phase 10 transition is click-free.** Written in the shape of Phase 9's SC-005
  (`specs/seraphis-phase9-parameters/spec.md:2094-2123`), which this criterion previously weakened by
  dropping all three of its load-bearing parts.
  *Transitions in scope (six):* freeze-on, freeze-off, send-engage, send-bypass, wander-bypass engage,
  wander-bypass disengage (FR-010a).
  1. *Test statistic.* For each transition, `maxPerSampleDelta` over the **±10 ms window** centred on
     `event sample + AetherReverb::getLatencySamples() (1024) + kFxSendChunkSamples (512) + fftSize
     (1024)` — i.e. **positioned in the OUTPUT domain**. Phase 10 stacks the send's accumulator delay and
     its FFT latency on top of the reverb's; an unshifted window is ~53 ms off the event at 48 kHz and
     measures the wrong audio entirely (Phase 9 recorded the same defect for the reverb alone,
     `spec.md:2100-2104`).
  2. *Reference.* **One window per measured transition**, of the **same 20 ms length**, drawn from the
     **same render** at offsets at least **50 ms clear of any transition** in the same output domain,
     uniformly spaced. "Quiescent" means exactly that — not silence.
  3. *Bound.* `max(test statistics) ≤ 1.5 × max(reference statistics)`, with the **same number of draws on
     both sides**.
  4. *Non-finite clause.* No sample of the render is non-finite, tested by bit pattern, never
     `std::isnan`.
  *Positive controls (both mandatory).*
  a. *Detector wiring.* The same statistic over a non-transition window with a deliberately injected
     one-sample step of **2× that window's own `maxPerSampleDelta`** must **exceed** the bound.
  b. *Criterion wiring.* With FR-040's probe snapping the 20 ms return-gain ramp (FR-008/FR-009) to
     instant, the same render must **fail** clause 3.
  *Test:* `effects_chain_test.cpp` — `"Effects transitions are click-free"`.

- **SC-009 — State version 3 round-trips and migrates.** (a) `getState`/`setState` over a table of 16
  all-non-default effects values returns every field exactly. (b) A checked-in **version-2** blob loads
  without error and leaves all 16 effects fields at their C-6 defaults, and the rest of the surface at the
  values the blob encodes. (c) A checked-in **version-1** blob still loads. (d) The v3 blob's bytes **from
  offset 4 onward** have the v2 blob's bytes from offset 4 onward as a strict prefix, and the two blobs
  differ **only** in the leading `int32`. *(The prefix property is over the payload, not the whole blob:
  the version `int32` is the first field written (`processor.cpp:950`) and the first field read
  (`:877-878`), so a v2 blob and a v3 blob differ at offset 0 by construction — the stronger claim an
  earlier revision made is false and its test would fail.)*
  *Test:* `plugins/seraphis/tests/unit/state_v3_test.cpp`.

- **SC-010 — Determinism.** Two renders of the same seeded 20 s sequence with the send active and freeze
  exercised, produced by **two independently heap-allocated `Processor` instances** — whose `SpectralDelay`
  members therefore sit at different addresses — rendered in the same process, agree within
  `tests/test_helpers/render_fingerprint.h`'s measured tolerance (`kSampleTolerance = 1.0e-4f`,
  `kMetricTolerance = 1.0e-5`, `:48-52`). *Two separate instances is the whole point and replaces an
  earlier "two separate process invocations" clause that is not realizable in a Catch2 case:* the defect
  FR-027 closes is the seed `reinterpret_cast<uintptr_t>(this) ^ sampleRate` (`spectral_delay.h:223-225`),
  so re-rendering on the **same** instance leaves `this` unchanged and passes even on a build where
  `seedRng()` was never wired. This clause is therefore also the **negative control**: it fails if FR-027
  is not implemented. The test additionally asserts that two different `kSeedId` indices produce
  **different** fingerprints, discriminated as a relative aggregate-metric difference
  **> 100 × `kMetricTolerance`** (i.e. > 1e-3), so the seed is proven to reach `SpectralDelay::seedRng`
  and both drifts (FR-026, FR-027). **No bit-exact float golden.**
  *Test:* `effects_chain_test.cpp` — `"Effects renders are seed-deterministic"`.

- **SC-011 — RT safety and burst cost.** Under the allocation-tracking harness the whole Phase 10 stage
  performs **zero** allocations, locks and exceptions across a 60 s render that toggles every bypass
  predicate 100 times **and automates `kSeedId` across ≥ 16 index changes** (FR-027's second burst site).
  Both burst kinds are gated: over **≥ 16 events of each kind**, the **worst** block containing a
  `spectralDelay_.reset()` (FR-008) and the **worst** block containing a `seedRng()` + `reset()` pair
  (FR-027) each cost **≤ 5.0 %** of one core at 48 kHz / 512-sample blocks, i.e. **≤ 533 333 ns** against
  the 10 666 667 ns block period, with the **median** of each also recorded in the banner. Every other
  block costs **≤ 266 667 ns** (SC-013's per-block ceiling, restated here in ns rather than by
  cross-reference, because a single per-block wall time and a **worst-of-seven** best-of-16 aggregate are
  not the same quantity).
  *Measurement protocol:* the burst and per-block figures are reported **worst-of-seven under the SC-013
  protocol** — fresh-boot, idle machine, seven consecutive whole-suite runs, best-of-16 per estimate
  (`param_perf_test.cpp:133-156`). The allocation/lock/exception clause is protocol-independent and is a
  hard failure on any machine.
  *Test:* `plugins/seraphis/tests/integration/effects_perf_test.cpp` — `"Effects stage is RT-safe"`.

- **SC-011a — A mix excursion through zero does not destroy the tail.** All quantities are the isolated
  send return read at `preOutputTapForTest()` (SC-003's definition). With the send active at mix 1.0,
  feedback 0.6, delay time 250 ms and a captured freeze, driving `kFxDelayMixId` to exactly 0 and back to
  1.0 over an interval of **200 ms** — comfortably shorter than `kFxSendDrainMs`, and short enough that
  the silent drain (FR-009a) attenuates the tail by well under the tolerance at this feedback and delay
  time — leaves the isolated return's RMS 500 ms after re-engagement within **±2.0 dB** of the RMS
  measured 500 ms before the excursion, and leaves the frozen spectrum's centroid within **5 %** of its
  pre-excursion value. *(The excursion length is pinned because the criterion is length-dependent by
  design: the drain is fed silence, so the tail decays through it at the component's own per-bin feedback
  — a multi-second excursion is expected to fall outside ±2.0 dB and asserting otherwise would forbid
  FR-009a's chosen behaviour.)* A control excursion **longer** than `kFxSendDrainMs` is asserted to
  *reset* (return RMS falls ≥ 30 dB and rebuilds), proving the window is the discriminator and not a
  vacuous pass (FR-008, FR-009a).
  *Test:* `effects_chain_test.cpp` — `"A short mix excursion preserves the send tail"`.

- **SC-012 — Zero-cost at defaults.** With all 16 effects parameters at their C-6 defaults, at the pinned
  operating point (**8 voices held, the MIDI script of Phase 9's SC-009 arm in `param_perf_test.cpp`,
  48 kHz, 512-sample blocks**), the Phase 10
  stage's own per-block wall time — read from FR-041's `effectsStageNsForTest()` scoped timer around C-1
  steps 4 and 5, accumulated over the render and divided by **`effectsStageProcessCallsForTest()`**
  (FR-041 clause 1 — `process()` **calls**, never slices; the case additionally asserts that divisor
  equals the number of `process()` calls the harness itself made, so the two can never drift) — is
  **≤ 0.10 %** of one core,
  i.e. **≤ 10 667 ns/block**. Reported **worst-of-seven best-of-16 under the SC-013 protocol**
  (`param_perf_test.cpp:133-156`). *It is deliberately
  NOT a whole-render delta:* the whole-render figure's own run-to-run spread is **107 420 ns** in the
  live cold dataset (`param_perf_test.cpp:148-156`, the Phase 9 SC-009 poly-8 row ranging 2 123 410 …
  2 230 830) — **10×** this threshold — and was 377 740 ns in the withdrawn T028 dataset (`:78-84`),
  i.e. 35×. The argument holds on the live dataset alone: a delta of two such renders cannot resolve
  10 667 ns.
  *Test:* `effects_perf_test.cpp` — `"Effects cost nothing at defaults"`.

- **SC-013 — Effects-stage budget when fully active.** Same measurement point as SC-012 — FR-041's scoped
  timer over C-1 steps 4 and 5, over the same `effectsStageProcessCallsForTest()` divisor with the same
  harness-call-count assertion, which is load-bearing *here* because this criterion's render is required
  to carry an automation point on 1410/1441/1443 and therefore renders as 64-sample sub-slices — but at
  the **full-poly operating point with voices sounding** (8 voices
  held, the MIDI script of **Phase 9's SC-009 arm in `param_perf_test.cpp`** — not this spec's SC-009,
  which is the state-v3 round-trip), not at zero voices, because zero voices is a configuration that
  never occurs in use and because SC-014's arithmetic has to compose the two figures. With all 16 effects
  parameters at maxima, the Phase 10 stage costs **≤ 2.5 %** of one core at 48 kHz — i.e.
  **≤ 266 667 ns** per 512-sample block, with the per-run table transcribed into the test file under a
  BASELINE PROVENANCE banner in the shape of `param_perf_test.cpp:65-84`.
  ***Measurement protocol — binding, the SINGLE protocol every CPU criterion in this spec is measured
  under (SC-011, SC-012, SC-013, SC-014), and the same one RQ-1's headroom was established under:*** a
  **fresh-boot, idle machine**, **seven** consecutive whole-suite runs of `seraphis_tests.exe "[.perf]"`,
  each figure itself a **best-of-16**, and the **worst of the seven** is the reported figure. This is
  verbatim the protocol Phase 9's shipped SC-009 baseline was pinned under on 2026-08-02
  (`param_perf_test.cpp:133-156`), and it is stated here rather than assumed because the *same*
  configuration measured 24.21 % on the earlier hot T028 machine (`:83`, `:103-131`) — a 3.3-point swing
  that is larger than this whole budget. **This criterion is `[.perf]`-tagged and stays OUT of the CI
  gate**, exactly as the existing perf arms are; a breach on an ordinary dev or CI machine is not a
  failure of this criterion, and re-measuring under the stated protocol is the first response to one.
  *Budget derivation (this is where the 2.5 % comes from):* the pinned cold worst of **Phase 9's SC-009
  arm** is 2 230 830 ns =
  **20.91 %** (`param_perf_test.cpp:443-456`); the absolute ceiling is `kFullPolyCeilingNs` = 2 666 666.7 ns
  = 25 % (`:376`). 20.91 + 2.5 = **23.41 %**, leaving 1.59 points of margin inside the ceiling — which is
  what makes SC-014 reachable. If the stage measures above 2.5 %, the levers are the stage's own cost and
  the shipped defaults; **the 25 % ceiling is never the lever** (roadmap lines 313–326).
  *Test:* `effects_perf_test.cpp` — `"Effects stage stays inside its 2.5 % budget"`.

- **SC-014 — Phase 7's 25 % gate still passes with the effects section ACTIVE, unrelaxed.**
  `param_perf_test.cpp`'s SC-009 arm is re-run on the Phase 10 build at the Phase 9 full-surface operating
  point with `kNonDefaultTable` grown to **107 rows**, the 16 effects rows sitting at their
  **most-expensive end** per the table's own rule (FR-038a clause 4) — i.e. at the roadmap's *"8 voices,
  everything on"* configuration (roadmap lines 313–314), which after Phase 10 ships **includes** these
  effects. "Most-expensive end" is the **operational** definition FR-038a clause 4 fixes: for each of the
  seven CPU-ambiguous rows (1413, 1418, 1419, 1430 discrete; 1411, 1412, 1415 flat) both/each candidate
  is measured once and the **costlier** value is the one in the table, with those measurements recorded
  in the banner beside it. That is what makes this criterion's central claim — that the table *is* the
  worst case — provable rather than asserted. It must satisfy the absolute **25 % of one core at
  8 voices** ceiling,
  `kFullPolyCeilingNs = 2 666 666.7` ns (`param_perf_test.cpp:376`), and the run-time baseline gate
  `kBaselineFullPolyNs (2 318 840.0) × kRegressionFactor (1.15) = 2 666 666` ns (`:456`, `:379`, `:472-479`).
  **Relaxing the ceiling is never the lever** (roadmap lines 313–326); neither is raising
  `kBaselineFullPolyNs`, which `:454-455` records is already the maximum both `static_assert`s admit.
  A companion figure at **16 voices** stays explicitly **non-gating**, following the roadmap's own
  poly-16 precedent (roadmap lines 318–321) — that precedent applies to *polyphony outside the budgeted
  scenario*, and does **not** license demoting effects-on at 8 voices, which is inside it. See
  *Resolved Question* **RQ-1**.
  ***Measurement protocol — the SC-013 protocol, identical and binding:*** fresh-boot, idle machine,
  seven consecutive whole-suite runs, best-of-16 per estimate, worst of the seven reported
  (`param_perf_test.cpp:133-156`). No other protocol is admissible for this figure. Like every other
  `[.perf]` arm, **this criterion is not part of the CI gate**; the everyday gate remains the defaults configuration. RQ-1's 4.09 points of headroom exist
  only against that cold protocol — the same 91-row configuration measured 24.21 % hot (`:83`) — so
  stating it is what keeps RQ-1's arithmetic honest rather than machine-dependent. If the composed figure
  breaches 25 % **under this protocol**, the levers are unchanged: the stage's own cost and the shipped
  defaults, never the ceiling and never `kBaselineFullPolyNs`.
  *Test:* `param_perf_test.cpp` (existing arm, table grown to 107 rows, baseline re-verified).

- **SC-015 — Portability and lint.** `node tools/check-portability.js` is clean; the build is warning-free on
  MSVC, GCC and AppleClang; `clang-tidy` `-Target seraphis` is clean on both
  `tools/run-clang-tidy.ps1` and `tools/run-clang-tidy.sh`.

- **SC-016 — Host validation.** `tools/pluginval.exe --strictness-level 5 --validate` on the built
  `Seraphis.vst3` is clean, and the editor-lifecycle harness
  (`tests/test_helpers/editor_lifecycle_harness.h`) still passes 10 open/close cycles.

- **SC-017 — Partition invariance with the send ACTIVE.** With `kFxDelayMixId = 1.0`, feedback 0.6, delay
  time 250 ms, the wander engaged, and **steady parameter values throughout** (no bypass, freeze or seed
  transition during the render), a 4 s render delivered as block sizes **1, 2, 3, 7, 512, 2048 and one
  oversized (4096) call** each agree with a single contiguous render within `render_fingerprint.h`
  tolerance. This is the criterion FR-003a's accumulator exists to make satisfiable; without it the send
  is off by a whole hop between partitions, permanently (Overview fact 5). The render is required to be
  transition-free because FR-008's reset re-randomizes `2 × numBins` phases (`spectral_delay.h:279-284`)
  and would otherwise make the comparison partition-dependent by construction — the reset trigger is on a
  fill-chunk boundary (FR-008 as amended by plan D-3), which is partition-independent **within** this
  render precisely because the render is continuously engaged, but a *parameter* transition still lands
  where the host puts it.
  A **negative control** renders the same case with the accumulator bypassed (send fed the raw slice
  length) and asserts it **fails**, so the criterion cannot pass vacuously.
  *Test:* `effects_chain_test.cpp` — `"The effects send is block-size invariant"`.

- **SC-018 — Cadence: the call-count requirements.** In the shape of Phase 9's
  `param_cadence_test.cpp`, using FR-041's counters:
  (a) `spectralDelayResetCountForTest()` increases by exactly **1** per bypassed→active transition that
  satisfies FR-008's two conditions and by **0** otherwise — including a freeze-forced engage (FR-023a) and
  a sub-`kFxSendDrainMs` excursion (FR-009a);
  (b) `widthDriftBlockCountForTest()` equals the `process()` block count under **both** bypass states
  (FR-011);
  (c) `bypassPredicateEvalCountForTest()` equals the `process()` **call** count over a render whose blocks
  are subdivided into multiple MIDI slices — not the slice count (FR-012);
  (d) driving any of IDs 1410–1443 leaves `applyVoiceParamsCallCountForTest()` and
  `applyAetherParamsCallCountForTest()` (`processor.h:172`, `:186`) **unmoved**, proving `Route::FX` bumps
  no generation counter (FR-019); driving ID 1400 moves neither either (it is `Route::ENG`);
  **(e) FR-007, added 2026-08-02 (plan D-8):** over a render held entirely at the C-6 defaults
  `sendChunkCountForTest()` (FR-041 clause 7) stays **0**, and over a render that engages and later
  bypasses the send it advances **only** while the send is active or draining — the last increment lands
  no later than the block on which the drain window ends. This clause is what moves FR-007 into the
  CI-gated suite: SC-012's threshold is `[.perf]`-tagged and outside the gate, and SC-002 cannot see the
  violation at all, because at mix 0 the mix loop adds `fxOut[i] * 0.0f` and a fully-running send still
  leaves the bus bit-identical.
  *Test:* `plugins/seraphis/tests/integration/param_cadence_test.cpp` — `"Effects push cadence"`.

- **SC-019 — Tempo sync produces the tempo-derived period, and the LABEL names it.** With
  `kFxDelaySyncId` on and `kFxDelaySyncNoteId` at two different indices — one of which MUST be the
  registered default **7 (`1/16`)** — at two host tempi (90 and 140 BPM), the measured echo
  period of the **isolated send return** (SC-003's difference render, read at `preOutputTapForTest()` —
  the limiter's gain reduction otherwise smears the echo peaks the period is measured between) matches
  the component's own
  `dropdownToDelayMs(index, tempo)` mapping (`spectral_delay.h:330`) within **± one hop = ± 512 samples**.
  **The criterion is written against FR-017's amended label table** (plan OQ-1/D-1, ruled 2026-08-02): for
  each index measured, the test additionally asserts that the *measured* period equals the period the
  string at `kFxDelaySyncNoteLabels[index]` names — e.g. index 7 reads `"1/16"` and measures
  `0.25 × 60000/tempo` ms (125.0 ms at 120 BPM), never the `"1/8"` of the component's incorrect doc
  comment. This is the runtime half of the pairing; FR-017's `constexpr static_assert` is the build-time
  half.
  A render whose `ProcessContext` is absent, or present with `kTempoValid` clear but a stale non-zero
  `tempo`, falls back to the **120 BPM** period within the same tolerance — which is the clause that
  discriminates FR-030's three-part guard from the component's weaker `tempo <= 0.0` check
  (`spectral_delay.h:325-327`). Without this criterion, a build passing a default-constructed
  `BlockContext` (`tempoBPM = 120.0`, `block_context.h:69`) would satisfy every other criterion while
  silently ignoring host tempo.
  *Test:* `effects_chain_test.cpp` — `"Synced delay tracks host tempo"`.

---

## Edge cases

**RT-safety boundaries**

- `SpectralDelay::process` is called with a **constant** `n = kFxSendChunkSamples = 512` (FR-003a), well
  under `SeraphisEngine::kMaxBlockSamples = 2048` (`seraphis_engine.h:215`), which the slice loop already
  bounds slices at (`processor.cpp:792`). A larger call would index past the prepared
  `tempBufferL_`/`dryBufferL_` (`spectral_delay.h:215-218`); the component is nonetheless prepared with
  2048 so the ceiling is the same constant everywhere and a future chunk-size change cannot overrun.
- `spectralDelay_.reset()` on the audio thread (FR-008) resets `4 × numBins = 2052` per-bin `DelayLine`s and
  fills four scratch vectors (`spectral_delay.h:259-289`). It is bounded and allocation-free, but it is a
  burst — SC-011 gates the block that contains it.
- The M/S width smoother substitutes 0 for NaN (`midside_processor.h:133-136`, via `smoother.h`), and
  `BrownianDrift`'s output goes through `OnePoleSmoother` too, so no legal parameter value can inject a
  non-finite into the bus through the wander stage.

**Parameter extremes**

- `kFxDelayMixId` exactly 0 is the bypass predicate (FR-007). A host writing `1e-9` is **not** bypass and
  must still be click-free — the predicate MUST be `== 0.0f` on the denormalized value, and the 20 ms
  return-gain ramp covers the sub-audible case. Because the predicate is exact, an automation curve or
  macro sweep that merely *passes through* zero would, under an unconditional reset, wipe the tail and any
  captured freeze on every crossing; FR-009a's drain window is what prevents that, and SC-011a measures
  it in both directions. **During the drain the send is fed silence, not the live bus** (FR-009a): the
  tail therefore decays through the excursion at the component's own per-bin feedback rather than staying
  perfectly coherent, and `kFxSendDrainFloor = 1e-6` ends the window as soon as it has, which is what
  bounds the excursion's worst-case cost by energy rather than by the full 2 s wall clock.
- `kFxWidthId` at 0 collapses to mono and at 200 % doubles side energy; combined with `kFxWanderDepthId = 1`
  and `kWanderWidthSpanPercent = 50` (C-5) the modulated width spans `base ± 50 %` at the drift's clamp
  and typically `base ± 25 %`, and the clamp in C-5 keeps the pushed value inside
  `[kMinWidth, kMaxWidth]` (`midside_processor.h:65-66`) at both base extremes. At `base = 0` the wander
  can only widen and at `base = 200 %` it can only narrow — in neither case can it push the pushed width
  outside the component's own range.
- `kFxAzimuthDepthId = 1` with the drift at its walk limit puts `position` at 0 or 1, i.e. full L or full R.
  The pan pair is energy-preserving **up to a fixed centre normalisation** (C-5): with
  `kFxAzimuthCentreComp = √2` applied to both gains, `gL² + gR² = 2` at every position — constant, so the
  limiter's job is position-independent — and centre is exactly unity per channel, which is what keeps
  FR-010's skip boundary continuous in steady-state level. **Peak per-channel gain at full deflection is
  +3.01 dB**, bounded by the limiter, which is what SC-006 gates. *(Amended 2026-08-02, plan OQ-4/D-4: the
  earlier "equal-power keeps total energy constant" sentence applied a crossfade law's invariant to the
  two channels of one bus, where it means a permanent −3.01 dB instead.)*
- `kFxDelaySpreadId` at 2000 ms with `kFxDelayTimeId` at 2000 ms asks for per-bin delays beyond the
  component's `kMaxDelayMs`; the component clamps internally (`spectral_delay.h:425-427`) and the result must
  still decay (SC-005).
- `kFxDelayFeedbackId` cannot reach the sustaining region by construction — but the mechanism is
  **FR-016a's tilt compensation**, not FR-016's registration cap. The registration cap alone leaves 243 of
  513 bins above unity loop gain at tilt +1 (C-7 clause 2). A **preset or state blob carrying an
  out-of-range value** must be clamped on load, not trusted, and the clamp must be applied *before* the
  compensation divide.

**Sample-rate and block-size changes**

- `setupProcessing()` may be called repeatedly; every Phase 10 member is re-prepared and re-seeded, and
  `pushAllSurfaces()` re-pushes the effects surface (FR-034).
- `SpectralDelay`'s per-bin delay lines are sized in **frames** at `frameRate = sampleRate / hopSize`
  (`spectral_delay.h:158`), so delay **time** is sample-rate independent; the reported latency is
  `fftSize` **samples**, so it is *not* — which is a further reason FR-005 keeps it out of the reported
  number.
- At 96 kHz and 192 kHz the send's FFT latency in milliseconds halves and quarters. Nothing in the spec
  depends on its absolute value.
- Partition invariance is **SC-017**, not a loose note: block sizes 1, 2, 3, 7, 512, 2048 and one
  oversized call must all produce the same output as a single contiguous render within
  `render_fingerprint.h` tolerance, **with the send active**, which is the property Phase 9's SC-008
  already gates for the rest of the chain (`processor_audio_test.cpp:93-95`). It is satisfiable **only**
  because of FR-003a's fixed-size accumulator, and the invariance render is required to contain no bypass,
  freeze or seed transition (SC-017).

**Seed determinism**

- `SpectralDelay` seeds from an ASLR-dependent address unless `seedRng` is called (`spectral_delay.h:223-225`)
  — FR-027 exists solely to close this, and SC-010 is what proves it closed. The proof requires **two
  different instances**: on one instance `this` is constant, so a build that never calls `seedRng()` would
  still reproduce itself (SC-010).
- `reset()` re-randomizes phases from that RNG (`:279-284`), so FR-027's ordering matters: `seedRng()` **then**
  `reset()`, never the reverse.
- The two wander salts must be distinct; identical salts would make width and azimuth move in lockstep, which
  reads as a single control and defeats the feature (C-5).

**Interaction edge cases**

- Aether Freeze (1204) **on** while Spectral Freeze (1430) is **off**: the send tracks an infinite,
  slowly-breathing tail. Both on: the send holds a snapshot of a frozen tail. Neither may be inferred from
  the other (FR-023).
- Freeze engaged while the send is bypassed: per **FR-023a** this is not a stored no-op — engaging 1430
  **forces** the send active at `kFxFreezeMinReturnGain`, ramped over 20 ms, with FR-008's `reset()`
  suppressed so `wasFrozen_`/`freezeCrossfade_` and the frozen spectrum buffers
  (`spectral_delay.h:256-257`, `:276-277`) are not cleared in the same block the capture must happen in.
  The capture therefore happens at *engage* time. SC-007 arm (a) exercises this from the C-6 defaults,
  which is the only configuration in which the bug it guards against is reachable.
- Polyphony change and voice steal while the send is active: the send sees only the summed bus and is
  unaffected; no Phase 10 state is per-voice.

---

## Resolved Questions

**There are no open questions.** Every question this spec raised — the three carried from the initial
draft and the eight from the 2026-08-02 clarification scan — was answered by the phase owner on
2026-08-02 and is encoded in the body above; the *Clarifications* section logs the decisions.

- **RQ-1 — "All effects active at the full Phase 9 surface, 8 voices" is a **GATED** figure. Resolved
  2026-08-02, in favour of the roadmap's plain reading.** *(This was OQ-2 in the previous revision, which
  shipped the opposite assumption.)*

  The roadmap's headline gate is *"full-poly CPU budget: **8 voices**, everything on, ≤ 25 % of one core
  @ 48 kHz"* (roadmap lines 313–314), reaffirmed at 313–326 with *"relaxing the ceiling is never the
  lever"*. After Phase 10 ships, **"everything on" includes these effects.** A criterion that keeps the
  25 % number while narrowing the *scenario* to the C-6 defaults — send bypassed, M/S skipped, saturation
  at the value already shipping — gates the one configuration that cannot break, and leaves the
  configuration the roadmap actually names bounded by nothing. The poly-16 precedent (roadmap lines
  318–321) does **not** license it: polyphony 9…16 is explicitly *"outside the budgeted scenario"*,
  whereas effects-on at 8 voices is inside it.

  **The arithmetic closes, against the correct dataset.** The withdrawn T028 hot worst (24.21 %,
  `param_perf_test.cpp:83`) implied only 0.79 points of headroom, which is where the previous revision's
  reluctance came from. That dataset was superseded: the shipped gate was pinned from the 2026-08-02
  **cold** dataset, whose worst is 2 230 830 ns = **20.91 %** (`:443-456`), against
  `kFullPolyCeilingNs = 2 666 666.7` ns (`:376`). Headroom is **4.09 points**, SC-013's stage budget is
  2.5 %, and 20.91 + 2.5 = **23.41 %** — inside the ceiling with 1.59 points to spare.

  **Consequences, all binding:** SC-014 gates the effects-**active** configuration; `kNonDefaultTable`
  grows to 107 rows with the 16 effects rows at their most-expensive end (FR-038a clause 4); SC-013 is
  measured **with voices sounding**, not at zero voices, so the two figures compose; and the only
  non-gating companion left is the 16-voice figure, which the roadmap itself put outside the budgeted
  scenario. If the composed figure breaches 25 %, the levers are the stage's own cost and the shipped
  defaults — never the ceiling, and never `kBaselineFullPolyNs`, which `param_perf_test.cpp:454-455`
  records is already the maximum both `static_assert`s admit.

  **Machine precondition, added 2026-08-02 (Q7).** Those 4.09 points exist *only* against the cold
  dataset's protocol, and the same 91-row configuration measured 24.21 % hot on T028 (`:83`). SC-013
  therefore now states that protocol explicitly — fresh boot, idle machine, seven runs, best-of-16 per
  estimate, worst reported (`:133-156`) — and **it is the single protocol every CPU figure in this spec
  is measured under**: SC-011, SC-012, SC-013 and SC-014 all cite it, the Success Criteria preamble's
  earlier blanket six-run rule (anchored to the withdrawn T028 banner, `:65-84`) is struck, and all four
  stay outside the CI gate, as `[.perf]` already is.
  Sizing the stage budget against the hot dataset instead (~0.75 %) was rejected: it would force a
  cheaper send — a smaller `fftSize`, a decimated chunk cadence, or a mono send — on the strength of a
  measurement the repo has already superseded.

- **RQ-2 — `FrequencyShifter` does NOT ship in Phase 10. Confirmed 2026-08-02.** *(Was OQ-1.)* The Reuse
  Inventory's Effects row names it (roadmap line 93), but the Phase 10 body names four effects and gives
  it no role, no send and no parameter (roadmap lines 466–469); an inventory mention does not obligate the
  phase. It stays a recorded non-goal, **available to a later phase if a musical design emerges**. Adding
  it would require its own send position in C-1, its own ID block, and its own CPU line inside what is
  left of the 4.09-point headroom after SC-013's 2.5 % — a scope decision, not an addition.

- **RQ-3 — Tape saturation is AMOUNT-ONLY. Confirmed 2026-08-02.** *(Was OQ-3.)* Phase 10 registers the
  control that already exists — `SeraphisEngine::setOutputSaturation` (`seraphis_engine.h:672`), whose
  banner states the drive is deliberately not exposed at Layer 3 (`:670-675`) — and the drive stays the
  compile-time `kOutputDriveDb = 0.0f` (`:250`). **No additive `dsp/` setter is added**, so the Non-goals'
  freeze on `dsp/` behaviour holds and the roadmap's "gentle ceiling — no aggressive distortion"
  (line 467) remains a **structural** property of a constant no parameter can reach, not a convention a
  later change could erode. → C-6 ID 1400, C-7 clause 3, FR-021.

- **RQ-4 — The effects stage SHIPS INERT; KDD-1 is discharged by Phases 11 and 12. Confirmed
  2026-08-02.** *(Was OQ-4, restated as Q8 in the clarification scan.)* Roadmap Key Design Decision 1 is
  unconditional — *"Nothing is ever static. Every audible parameter is a modulation target; life
  modulators run free even with no notes held"* (lines 71–73) — and KDD-5 makes the five macros the
  primary performance surface (lines 79–80). Of Phase 10's sixteen parameters only two (width, azimuth)
  receive any life modulation, and only via their own depth controls, which C-6 defaults to 0;
  `kFxDelayMixId` also defaults to 0. The precedent runs the other way: the Aether stage, also a global
  post-voice stage, **does** have macro rows (`SeraphisMacroTargetOwner::Aether`,
  `seraphis_macro_matrix.h:52`, `:79-80`, rows at `:218-231`). The ruling, and the deviation it records:
  1. **The stage ships inert exactly as specced.** `kFxDelayMixId`, `kFxWanderDepthId` and
     `kFxAzimuthDepthId` all default to 0, **SC-002 keeps its exact-equality bit-exact negative control**,
     and the Reuse Inventory's "thin wiring only" (roadmap line 93) holds.
  2. **Macro reach into effects does not ship in Phase 10.** A `Dissolve → kFxDelayMixId` /
     `kFxWanderDepthId` mapping is the natural fit given Phase 7's documented Dissolve axis
     (roadmap line 309) and could be done plugin-side without touching `SeraphisMacroMatrix` — but it is
     new routing, not "thin wiring", and it would put a second, competing writer on parameters the host
     also automates. It belongs with the phase that owns the performance surface.
  3. **Named later owners: Phase 11 (UI)** for the macro-reach half — macro reach into the effects
     surface — and **Phase 12 (Factory Presets)** for the shipped-patch half, since the patches a user
     actually loads, not the registered defaults, are what KDD-1 speaks about. Both are named here so the
     deviation has an owner rather than being an omission.
  4. Rejected alternatives and their cost, recorded so the ruling is not re-litigated: a non-zero
     `kFxWanderDepthId` default forfeits SC-002's exact-equality form (the M/S stage would then run on
     every block, and running it is an algebraic but not a bit identity — C-3) and moves SC-012's,
     SC-013's and SC-014's operating points; plugin-side macro reach is new routing with a second writer;
     real `SeraphisMacroMatrix` rows are a `dsp/` behaviour change the Non-goals forbid.

---

## Traceability

| Roadmap statement | Where it lands |
|---|---|
| line 466 — "spectral freeze (global capture-and-hold of the Aether tail)" | C-1 step 4, C-4, FR-023, **FR-023a**, SC-007 (both arms), SC-008 |
| line 467 — "spectral delay (`spectral_delay`)" | C-2, C-6 IDs 1410–1419, FR-003, **FR-003a**, FR-004, FR-005, FR-016a, FR-022, SC-005, SC-017, SC-019 |
| line 467 — "tape-like saturation … gentle ceiling — no aggressive distortion" | C-6 ID 1400, C-7 clause 3, FR-021, **RQ-3** |
| line 468 — "stereo wandering (`BrownianDrift` → M/S width + azimuth via `midside_processor`)" | C-5, C-6 IDs 1440–1443, FR-006, FR-010, **FR-010a**, FR-011, FR-024, **FR-024a**, FR-025, FR-026, SC-018 |
| line 469 — "Ordering and sends defined here, not ad hoc" | C-1, C-2, FR-001, FR-002, SC-003 |
| line 93 — Effects row: "thin wiring only" | *New components* (no DSP class), ODR sweep table |
| line 71 (`plugin_ids.h`) — "1400+ Effects (Phase 10)" | C-6, FR-013, FR-018 |
| lines 71–73 / 79–80 — KDD-1 "nothing is ever static", KDD-5 macro-first | **RQ-4** (recorded deviation; owners Phase 11 + Phase 12) |
| lines 313–326 — 25 % at 8 voices, "everything on", "relaxing the ceiling is never the lever" | SC-014 (**effects-active**), SC-013, SC-012, SC-011, FR-038a clause 4, **RQ-1** — all four CPU criteria measured under the single **SC-013 seven-run cold protocol** (`param_perf_test.cpp:133-156`) |
| Phase 9 SC-005's continuity contract — `param_continuity_test.cpp:478-501`, `:749`, `:822-826` (inherited, not a roadmap line) | **FR-038a** (nine assertions), **FR-038b**, **SC-001a** |
| lines 550–559 — cross-cutting constraints (RT safety, layers, ODR, CPU-as-FR, no bit-exact goldens, portability, naming) | FR-028, FR-029, ODR sweep, SC-010–SC-019 |

---

## Review notes

Recorded for the two review points that were **not** adopted as stated, and for one bookkeeping note. Every
other issue raised in the 2026-08-02 review was applied in full; the substantive ones are visible as
FR-003a, FR-009a, FR-010a, FR-016a, FR-023a, FR-038a, FR-038b, FR-040, FR-041, SC-001a, SC-011a, SC-017,
SC-018, SC-019, RQ-1 and RQ-4. The 2026-08-02 clarification scan's eight questions were answered in the
same session and are logged under *Clarifications*.

1. **REJECTED (partially) — "the `StereoField` `DelayLine` members are at `:213-214` and `:217-218`, not
   `:214-219`."** Re-read this session: `grep -n "DelayLine" dsp/include/krate/dsp/systems/stereo_field.h`
   returns `214: DelayLine delayL_`, `215: DelayLine delayR_`, `218: DelayLine offsetDelayL_`,
   `219: DelayLine offsetDelayR_`. The original `:214-219` span is correct and the proposed correction is
   off by one in both pairs. The **other half** of that issue was adopted: the bullet now gives the full
   path `dsp/include/krate/dsp/systems/stereo_field.h` and names it **Layer 3**, so no reader looks for it
   under `effects/`.

2. **REJECTED as a Phase 10 change, RECORDED as RQ-4 — "give the wander a non-zero default depth and/or
   route a macro axis into the effects surface."** The roadmap conflict (KDD-1, lines 71–73) is real and is
   now stated in Non-goals and RQ-4 rather than left implicit, with named later owners (Phase 11 for macro
   reach, Phase 12 for the shipped-patch half) and the explicit cost of doing it here: a non-zero
   `kFxWanderDepthId` default makes the M/S stage run on every block, which forfeits SC-002's
   exact-equality form (running `MidSideProcessor` at width 100 % is an algebraic identity but not a
   bit-identity — C-3) and moves SC-012's and SC-014's operating points. The review explicitly offered
   "record it as a fourth Open Question with a named later owner" as an acceptable resolution; that is
   what was done, and the phase owner **confirmed the ruling on 2026-08-02** (Q8), converting it from a
   working assumption into RQ-4.

3. **Bookkeeping — FR-001's "byte-for-byte" requirement was downgraded, not deleted.** It is now stated as
   an observable-behaviour requirement verified by SC-002, because no runtime test can check a diff
   property and the strict reading contradicts itself (inserting steps 4 and 5 necessarily moves
   `processor.cpp:1170-1173`). The diff expectation survives as reviewer guidance inside FR-001's own
   parenthetical.
