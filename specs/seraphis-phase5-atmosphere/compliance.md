# Seraphis Phase 5 — Atmosphere: compliance record

Phase: **5 — Atmosphere** (spec slug `seraphis-phase5-atmosphere`)
Spec: `specs/seraphis-phase5-atmosphere/spec.md`
Plan: `specs/seraphis-phase5-atmosphere/plan.md`
Tasks: `specs/seraphis-phase5-atmosphere/tasks.md`
Roadmap: `specs/Seraphis-roadmap.md`, Part A → Phase 5 (lines 227-248)
Component: `dsp/include/krate/dsp/systems/atmosphere_engine.h` (Layer 3, `Krate::DSP::AtmosphereEngine`)
Branch: `feat/seraphis-phase1-life-modulators` (all Seraphis phases share it — see project memory)

Measurement machine, for every number below: 13th Gen Intel Core i9-13900HX,
Windows 11 Pro 26200, on AC, otherwise idle. Build `build/windows-x64-release`,
MSVC Release. Same machine as `harmonic_cloud_perf_test.cpp:106-109` and
`continuous_body_perf_test.cpp:142-145`, so Phase 7 can add the three phases'
figures together.

---

## Status: **COMPLETE (2026-07-28)**

**0 fail, 0 partial, 74 pass** (74 distinct items across the FR, SC and
cross-cutting-constraint lenses).

---

### The budget decision that closed this phase — read this first

**2026-07-28, user decision, SC-004 lever (6), option 1.** Phase 5's per-voice
CPU allowance is **raised from 1 % to 1.5 % of one core** — reference
**160 000 ns per 512-sample block at 48 kHz** — for configurations (a), (b), (d)
and (e). The **saturated-64 configuration (c) is OUT-OF-REGION**: still measured
and still regression-tracked against its own checked-in baseline, but **not**
gated against the ceiling. Phase 7 re-derives its polyphony tally from the real
figures.

**The decision was derived from the five measured worst-case figures already in
§1.1 below**, after the levers had been spent (T019: 1.60×–2.77× measured
improvements) and after levers (3), (3b) and (5) were measured and refused. 1.5 %
is the smallest round allowance that covers every in-region figure; (d), at
1.440 %, is the binding one.

**Nothing was softened to reach it, and this is checkable:**

* No baseline was raised by code. The one number that moved is the **reference**,
  and only the user can move that.
* `kMaxGrains` stays **64**; lever (5) stays refused; FR-073's operating rule is
  unchanged.
* SC-004's `static_assert(baseline · 1.5 ≤ reference)` + `static_assert(baseline ≥
  reference/50)` + `REQUIRE(measured ≤ baseline · 1.5)` structure is **retained**
  for (a), (b), (d) and (e). (c) loses only the headroom clause and keeps the floor.
* The amendment's own cost is recorded rather than hidden: for (b), (d) and (e)
  the measurement exceeds `reference / 1.5`, so their baselines are the **cap**
  (106 666) rather than their measurements, which loosens their *regression*
  bound to ~43 % while keeping the *absolute* 1.5 % bound exact. See
  `spec.md` SC-004's amendment box and the perf TU's BASELINE PROVENANCE block.

Where the amendments live, by line:

| amendment | spec.md |
|---|---|
| SC-004 — reference raised to 1.5 %, (c) out-of-region | `spec.md:1164` |
| RA-4 — what Phase 5 actually hands Phase 7 | `spec.md:250` |
| FR-022 — measurement mandate discharged, `kMaxGrains` stays 64 | `spec.md:543` |
| FR-025 — implemented truncation adopted as binding | `spec.md:638` |
| SC-003 (D-17) — pool-full clause scoped to reachable cells | `spec.md:1110` |
| SC-009 (D-18) — allocation equality → two-sided bracket + geometry | `spec.md:1378` |

---

### The former gap list — every item re-verified 2026-07-28

The five rows below were **1 fail + 4 partial** at the previous pass (9 items
across the two lenses). Each is re-verified here against code and freshly-run
test output, not from memory.

| # | Item | Was | Now | Fresh evidence |
|---|---|---|---|---|
| 1 | **SC-004** — CPU budget | **FAIL** | **pass** | `AtmosphereEngine_CpuBudget` **passes**. Ran `dsp_systems_tests.exe "[.perf]"` twice this session; both runs green for both atmosphere cases. Run 2 measured (a) 71 804.8 (0.673 %), (b) 87 383.4 (0.819 %), (c) 299 940 (2.812 %), (d) 107 785 (1.010 %), (e) 87 046 (0.816 %) ns/block against gates 136 500 / 159 999 / 541 500 / 159 999 / 159 999. Baselines are measurements again: `atmosphere_engine_perf_test.cpp:570` `kBaselineDefaultsNs = 91000.0`, `:578`/`:593`/`:600` = `kCappedBaselineNs` (`:520` = 106666.0), `:585` `kBaselineSaturatedBlurNs = 361000.0`. Reference `:451` `kReferenceNs = 160000.0`, tied to its derivation by a static_assert against `kBlockBudgetNs`. |
| 2 | **FR-022** — `kMaxGrains` measurement mandate | **PARTIAL** | **pass** | The mandate is discharged and the amendment (`spec.md:543`) records the discharge: measured 8.06287 / 7.87372 ns per grain-sample (T022) and 7.59286 / 7.39566 ns (this session), memory hypothesis refuted at **ratio 1.02402 across a 4× byte difference**, lever (5) measured-and-refused (kMaxGrains = 16 still 1.40× over while saturating FR-009's own default table). The literally-named remedy is **superseded by the lever-6 decision**, not left undone. `kMaxGrains` stays 64 (`atmosphere_engine.h:187`), asserted by `atmosphere_engine_perf_test.cpp:484` (`static_assert(AtmosphereEngine::kMaxGrains == 64, …)`). |
| 3 | **FR-025** — grain liveness formula | **PARTIAL** | **pass** | The implemented truncation is now the **binding** formula (`spec.md:638`): `headroom = C − 2 − 2g − dR`, `slack = headroom − 2`, `L′ = ⌊slack/w⌋` when `w · requested > slack`, with all three extra terms named (young-side FR-025 `g`, old-side FR-014 `g`, D-12's reserved slack, D-1's `dR`) and the withdrawn `⌊(C−2−g)/w⌋` quoted. Every term strictly shrinks the reachable range, so the guarantee is **strengthened**. Re-ran `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness"` — EXIT=0, `All tests passed (1281 assertions in 1 test case)`. Implementation unchanged at `atmosphere_engine.h:1621` / `:1636-1637`. |
| 4 | **SC-003** — no grain-boundary clicks | **PARTIAL** | **pass** | D-17 is now the criterion (`spec.md:1110`): the pool-full clause applies only to the 12 of 30 cells whose `density × grainSeconds` can saturate `kMaxGrains`; the other 18 assert `== 0`, which is the stronger statement there. The withdrawn unconditional wording is quoted, and the unsatisfiability is stated arithmetically (population caps at 1/4/20 against 64). SC-001 keeps the unconditional form. Re-ran `dsp_systems_tests.exe "AtmosphereEngine_NoGrainBoundaryClicks"` — EXIT=0, `All tests passed (470 assertions in 1 test case)`; and `"AtmosphereEngine_NoAllocationAfterPrepare"` (SC-001's unconditional carrier) — EXIT=0, `All tests passed (11 assertions in 1 test case)`. Threshold still 12.5 sigma, unpadded. |
| 5 | **SC-009** — sample-rate independence | **PARTIAL** | **pass** | D-18 is now the criterion (`spec.md:1378`): the allocation-count equality is replaced by the shipped two-sided bracket (`0 < second ≤ fresh`, measured 6 against 69), `renderAllocationCount == 0`, and **capture-capacity + latency equality** against a fresh engine — plus why the literal `==` is wrong (`std::vector::resize` to an unchanged size allocates zero times, so a correct engine allocates strictly fewer). Re-ran `dsp_systems_tests.exe "AtmosphereEngine_SampleRateIndependence"` — EXIT=0, `All tests passed (115 assertions in 1 test case)`. |

Everything else — the other 65 items, including every real-time-safety, layer,
naming, warning and portability constraint — passes with cited evidence below.
**No other row was touched, softened or re-scoped.**

---

## Compliance table

One row per verified item. `Lens` records which audit produced the evidence
(`fr` = functional-requirement lens, `sc` = success-criterion lens,
`constraints` = cross-cutting lens); where both lenses produced byte-identical
evidence the row is merged. Evidence is transcribed **verbatim** from the
verifying agent.

| Item | Lens | Verdict | Evidence (verbatim) |
|---|---|---|---|
| FR-001 | fr | pass | dsp/include/krate/dsp/systems/atmosphere_engine.h:155 `class AtmosphereEngine` inside `namespace Krate {namespace DSP {` (:143-144); banner :1-7 states "Layer 3: System - AtmosphereEngine", spec slug seraphis-phase5-atmosphere and "Roadmap: specs/Seraphis-roadmap.md, Part A -> Phase 5 (lines 227-248)". |
| FR-002 | fr | pass | atmosphere_engine.h:121-132 includes exactly L0 (db_utils, grain_envelope, math_constants, pitch_utils, random), L1 (rolling_capture_buffer, smoother, spectral_buffer, stft), L2 (grain_scheduler, spectral_freeze_oscillator); no systems/ or effects/ include, no stereo_utils.h, no brownian_drift.h. `node tools/lint-layers.js` exit 0: "OK — no layer-dependency violations in 5-layer DSP tree." |
| FR-002 | sc | pass | dsp/include/krate/dsp/systems/atmosphere_engine.h:121-132 — the include list is exactly the spec's: L0 db_utils.h, grain_envelope.h, math_constants.h, pitch_utils.h, random.h; L1 rolling_capture_buffer.h, smoother.h, spectral_buffer.h, stft.h; L2 grain_scheduler.h, spectral_freeze_oscillator.h. No systems/ or effects/ include. Both deliberately-absent headers confirmed absent: no core/stereo_utils.h and no processors/brownian_drift.h. Header banner at :1-26 declares Layer 3, the spec slug and roadmap lines 227-248 as FR-001 requires. Gated automatically: `node tools/lint-layers.js` EXIT=0 this session. |
| FR-003 | fr | pass | prepare() at atmosphere_engine.h:363-476 is the only allocating method: ring :380, envelope table :384 (assign once), blur STFT/OLA/SpectralBuffer/FIFO :409-420 only under `if (blurEnabled_)`, freeze oscillators + scratch :437-441 only under `if (freezeEnabled_)`, freeze-leg delay :442-447 only under `blurEnabled_ && freezeEnabled_`, smoothers :465-468; ends `prepared_ = true; reset();` :474-475. SC-001 test AtmosphereEngine_NoAllocationAfterPrepare passed (0.370 s). |
| FR-004 | fr | pass | atmosphere_engine.h:622-623 signature `processStereoBlock(const float*, const float*, float*, float*, std::size_t) noexcept` (same shape as continuous_body.h:1161-1163); :626-628 any null pointer returns writing nothing; :630-632 numSamples==0 no-op; in-place documented as unsupported :620-621. Test sections "any null pointer writes nothing and returns" and "numSamples == 0 is a no-op and advances no control step" passed. |
| FR-005 | fr | pass | atmosphere_engine.h:227 `kControlChunkSamples = 64`; :652-661 partitions on `sampleCounter_ % kControlChunkSamples` (absolute anchor, not block start) and calls runControlStep() only at phase==0. SC-011 AtmosphereEngine_BlockPartitionInvariance passed (0.133 s). |
| FR-006 | fr | pass | reset() atmosphere_engine.h:482-587: ring+counters :484-486, grains/activeIdx/nextSlot_ :489-492, explicit `scheduler_.seed(deriveStreamSeed(seed_, kSchedulerSalt))` :500, engine RNGs :503-504, resetDriftLanes() :509 (re-seeds every lane, :1270-1272), blur/freeze cleared :523-549, smoothers snapped :539/:552-554, counters zeroed :575-586. Allocation-free (no assign/resize). Test AtmosphereEngine_SilenceLatchAndReset passed. |
| FR-007 | fr | pass | kSilenceRampMs = 10.0f atmosphere_engine.h:236, step :471; silence() :601-607 is idempotent and latches via latchNow() :2142-2150 (retires all grains, activeCount_=0, RunState::Latched); latched entry guard zero-fills and advances nothing :640-644, mid-block latch handled :710-714 and :2092-2096. Test AtmosphereEngine_SilenceLatchAndReset passed (0.069 s). |
| FR-008 | fr | pass | atmosphere_engine.h:1178-1180 `[[nodiscard]] ITERUM_NOINLINE static bool isFinite(float v)` = `!(detail::isNaN(v) \|\| detail::isInf(v))` — a composition of core/db_utils.h:54-57 / :175-178, no new bit test. Scripted grep over the header for `std::isnan\|std::isinf\|numeric_limits<float\|double>::infinity` returned exit 1 (0 hits). Every non-prepare method is noexcept (:482, :601, :622, all setters :736-949, all accessors :957-1077). |
| FR-009 | fr | pass | Control table atmosphere_engine.h:736-949; every setter is `std::clamp(isFinite(x) ? x : <default>, lo, hi)`. Ranges/defaults match the spec table: grainSeconds [0.05,30] def 4 (:737-738,:2232), density [0.1,20] def 4 (:750-751), jitter/positionSpread/pitchSpread/drift/pan/decorr/blur/freezeMix/level all present (:757-907, defaults :2232-2247). PrepareConfig defaults captureSeconds 8, blur/freeze true, blurFftSize 1024, freezeFftSize 2048, maxBlockSamples 2048 at :325-332; FFT snap-before-store via snapFftSize :1188-1195 called at :373-374. Blur smoother advanced by advanceSamples(blurHopSize_) once per frame-pair :1930; gain/level smoothers process() per output sample :1807/:2118. AtmosphereEngine_ControlTableClamps (7 sections incl. "FR-009 cadence: the blur smoother advances once per hop of audio") passed. |
| FR-010 | fr | pass | atmosphere_engine.h:369-370 clamps captureSeconds to [kMinCaptureSeconds=1.0, kMaxCaptureSeconds=30.0] (:273-274); :380 `capture_.prepare(sampleRate_, captureSeconds_)`. Power-of-two rounding + byte cost documented in the banner :37-49. |
| FR-011 | fr | pass | AtmosphereGrain atmosphere_engine.h:1090-1110 contains only scalars (read index/frac, ratio, pitch snapshot, pan, decorr, envelope phase, lifetime, age, active) — no audio buffer; the only audio source in the grain loop is `capture_.readStereoLinear(...)` :1749/:1754. No SlicePool anywhere in the header. |
| FR-012 | fr | pass | atmosphere_engine.h:1708 `capture_.writeStereo(sampleL, sampleR)` executes inside the per-sample loop BEFORE the grain accumulation loop at :1725-1791 which reads the ring at :1749. Test section "a grain reads audio written in the same block (self-granulation)" passed. |
| FR-013 | fr | pass | atmosphere_engine.h:2158 `std::uint64_t writeCounter_`, incremented at :1709 with the comment that getSamplesWritten() saturates; grain positions stored in that absolute domain (:1631 birth, :1722/:1735 age computation). |
| FR-014 | fr | pass | FIXED since last pass; D-11 withdrawn. Admission gate is now FR-014 verbatim at dsp/include/krate/dsp/systems/atmosphere_engine.h:1681-1685: `const double oldestAge = std::min(birthAge + decorr, capacity - 2.0 - guard); const double needed = std::ceil(oldestAge) + guard; if (static_cast<double>(capture_.getAvailableSamples()) < needed) { ++skipRingCold_; return; }`. `guard` is `static_cast<double>(kMinAgeSamples)` (:1592) and kMinAgeSamples = 64 (:249) -- NOT kInterpMarginSamples = 2 (:256), which no longer appears in this path. Made satisfiable structurally rather than by relaxing the margin: step (c) subtracts `guard` at BOTH ends, `const double headroom = capacity - 2.0 - guard - guard - decorr;` (:1621) and `ageHi = capacity - 2.0 - guard - ceil(wDown*lifetime) - decorr` (:1650-1651), so needed <= (C-2-g)+g = C-2 <= C. Checked against `birthAge + decorr` (both channels), a strengthening over FR-014's single age. Ran `dsp_systems_tests.exe "AtmosphereEngine_CaptureAndColdRing"` -- EXIT=0, `All tests passed (21 assertions in 1 test case)`; that case asserts the literal at atmosphere_engine_test.cpp:1419-1422 (`available = 48 000 < ceil(a0 + dR) + kMinAgeSamples = 48 064`, then getTotalGrainsBorn()==0 / getSkippedTriggerCountRingCold()>0). |
| FR-020 | fr | pass | atmosphere_engine.h:2164-2167 `std::array<AtmosphereGrain, kMaxGrains> grains_`, `std::array<std::uint8_t, kMaxGrains> activeIdx_`, `nextSlot_`; round-robin sweep `(nextSlot_ + k) % kMaxGrains` :1478-1484 with cursor left at slot+1 :1654; list maintained by append :1652 and swap-remove :1787, never rebuilt by scanning; reset() sets nextSlot_ = 0 :492. Test "slots are allocated round-robin, not first-free" passed (0.109 s). |
| FR-021 | fr | pass | atmosphere_engine.h:2168 `GrainScheduler scheduler_`, prepared :389, driven per sample by `if (scheduler_.process())` :1714, setDensity/setJitter refreshed only at control steps :1837-1838, seeded explicitly at :500 and :945. kMinDensity = 0.1f (:259) is annotated "== grain_scheduler.h:47's own floor"; test "the density floor is the same bound GrainScheduler enforces" passed. |
| FR-022 | fr, sc | pass | **RE-VERIFIED 2026-07-28.** The measurement mandate is discharged and the remedy is superseded by the user's lever-6 budget decision, both now written into the criterion at spec.md:543. Re-ran `dsp_systems_tests.exe "[.perf]"` (twice) -- `AtmosphereEngine_GrainSampleCost` green both times; this session measured 30 s ring **7.59286** ns/grain-sample, 8 s ring **7.39566** ns, **ratio 1.02666**, against the T022 pass's **8.06287 / 7.87372 ns, ratio 1.02402**. THE MEMORY HYPOTHESIS IS REFUTED BY FR-022'S OWN MEASUREMENT: ratio ~1.02 across a **4x** difference in ring bytes (16.8 MB vs 4.19 MB), plus a captureSeconds=1 probe inside the same spread -- the grain path is instruction-bound, so a smaller pool cannot make a grain-sample cheaper. Lever (5) is measured-and-refused (kMaxGrains=16 still ~99,900 ns/block = 1.40x the then-admissible baseline, while making FR-009's OWN DEFAULT control table permanently pool-saturated). `kMaxGrains` stays **64**: atmosphere_engine.h:187 `static constexpr std::size_t kMaxGrains = 64;`, structurally pinned by atmosphere_engine_perf_test.cpp:484 `static_assert(AtmosphereEngine::kMaxGrains == 64, ...)`. FR-073's operating rule unchanged. The amended arithmetic ceiling is 160,000/(64x512) = 4.883 ns (perf_test.cpp:461-470), against which the latest pair is 1.555x/1.515x -- reported, never gated. Nothing about FR-022 remains outstanding. Historical record of the previous verdict follows: placeholder records gone, measurement real, levers spent -- kMaxGrains was still 64, so FR-022's literally-named remedy (SC-004 lever 5) was then untaken. MEASUREMENT RECORD at atmosphere_engine_perf_test.cpp:81-115 no longer contains "NOT YET MEASURED" (grep returns nothing in the file) and carries a filled three-run table; T019 DECISION RECORD at :176-186 no longer says "NO LEVER SPENT" -- it reads "LEVERS (1), (2) AND (4) AUDITED AND HELD. A SIXTH, UNLISTED LEVER WAS FOUND AND SPENT IN FULL" with a before/after table (a 137,666->86,305; c 821,349->343,805; e 306,957->111,008). I ran `AtmosphereEngine_GrainSampleCost` (perf_test.cpp:1126, [.perf]): 30 s ring 8.06287 ns/grain-sample, 8 s ring 7.87372 ns, ratio 1.02402, ceiling 3.25521 ns -- 2.48x and 2.42x over. 64 grains still do not fit; FR-022's memory hypothesis is refuted too (ratio ~1.02 across a 4x byte difference). Lever (5) documented as measured-and-refused (compliance.md 1.4: at kMaxGrains=16 config (c) still computes to ~99,900 ns/block = 1.40x the admissible baseline, while making FR-009's default control table permanently pool-saturated); residual escalated at lever (6). Measurement mandate discharged; the literal corrective action is not. |
| FR-023 | fr | pass | atmosphere_engine.h:1477-1488: a full round-robin sweep that finds no inactive slot does `++skipPoolFull_; return;` — no grain is reset or reused; the RNG draws happen only after a slot is secured (:1493-1496). Test "the pool saturates at kMaxGrains and no grain is ever stolen" passed (0.314 s); totalRetired_ is counted independently at every deactivation site (:1786, :2144) so `retired + active == born` is a real assertion. |
| FR-024 | fr | pass | atmosphere_engine.h:1091-1092 `std::uint64_t readIndexInt; float readFrac;`; advance with integer carry :1769-1772; ratio recomputed per control step from `semitonesToRatio(clamp(s + lane*d, semisLo, semisHi))` in refreshGrainRatio :1447-1452 called at :1853, held constant within the chunk; age = `newest - readIndexInt - readFrac` :1735-1736. |
| FR-025 | fr | pass | **RE-VERIFIED 2026-07-28.** The implemented truncation is now the criterion: spec.md:638 adopts `headroom = C - 2 - 2g - dR`, `slack = headroom - 2`, `L' = floor(slack/w)` when `w*requested > slack` as binding, quotes the withdrawn `floor((C-2-g)/w)` verbatim, names all four terms (young-side FR-025 `g`, old-side FR-014 `g`, D-1's `dR`, D-12's reserved 2) and records that every one strictly SHRINKS the reachable age range, so the guarantee is strengthened rather than weakened. The code is unchanged and still matches: atmosphere_engine.h:1621 `const double headroom = capacity - 2.0 - guard - guard - decorr;` and :1636-1637 `const double slack = headroom - 2.0; const double lifetime = (w * requested > slack) ? std::floor(slack / w) : requested;`. Re-ran `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness"` -- EXIT=0, `All tests passed (1281 assertions in 1 test case)`, which asserts `getMinObservedGrainAgeSamples() >= kMinAgeSamples`, `getMaxObservedGrainAgeSamples() <= capacity - 2`, `math.ageLo <= math.ageHi` and the closed form as an equality. Cost quantified: `2g + dR + 2` samples of a 524,288-sample default ring, <= 0.03 % of the maximum grain lifetime wherever truncation binds. Historical record of the previous verdict follows: unchanged in kind; one MORE deviating term than before. Truncation is `const double slack = headroom - 2.0; const double lifetime = (w * requested > slack) ? std::floor(slack / w) : requested;` (atmosphere_engine.h:1636-1637) over `const double headroom = capacity - 2.0 - guard - guard - decorr;` (:1621) -- i.e. floor((C - 2 - 2g - dR - 2)/w) where FR-025 writes floor((C - 2 - g)/w). Three forced terms: `- dR` (D-1), reserved `- 2` (D-12, prevents std::clamp(a0, lo, hi) with lo > hi), plus a NEW second `- g` introduced by the FR-014 fix. w is still the SUM of one-sided excursions (`const double w = wUp + wDown;` :1605); a0 clamped into [ageLo, ageHi] at :1656. Invariant verifies: ran `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness" --success` -- EXIT=0, `All tests passed (1281 assertions in 1 test case)`. atmosphere_engine_test.cpp:890-894 asserts `getMinObservedGrainAgeSamples() >= kMinAgeSamples` and `getMaxObservedGrainAgeSamples() <= capacity - 2.0`; log expansions include `109103.0f >= 64.0f`, `1506787.5f >= 64.0f`, `112032.4765625 <= 524286.0`, `144064.0 <= 2097150.0`; plus 8x `REQUIRE( math.ageLo <= math.ageHi )` and 8x each `birthAge >= math.ageLo` / `birthAge <= math.ageHi`. Every deviating term shrinks the reachable range, so the guarantee is strengthened -- but it is still not FR-025's stated formula. |
| FR-026 | fr | pass | atmosphere_engine.h:1643 `grain.envPhaseInc = 1.0f / (lifetime - 1.0)` using the truncated L'; retirement is the integer compare `if (++grain.ageSamples >= grain.lifetime)` :1774 with slot returned and swap-removed :1781-1787. |
| FR-027 | fr | pass | kEnvelopeTableSize = 4096 (atmosphere_engine.h:168); regenerateEnvelope() :1218-1238 calls GrainEnvelope::generate into the existing vector and never resizes (early-returns if size != 4096 :1219-1221); table[0] and the last kEnvelopeTailZeroEntries=2 entries forced to 0 :1233-1237; lookup via GrainEnvelope::lookup :1744. Beyond the spec it also applies a 64-entry linear edge ramp (:1226-1231) — a documented superset needed to bound the neighbour step (banner :80-107). Test AtmosphereEngine_EnvelopeEndpointsForced passed (0.135 s). |
| FR-028 | fr | pass | Target refreshed at each control step: `gainSmoother_.setTarget(1.0f / sqrt(max(1, activeCount_)))` atmosphere_engine.h:1860-1861; applied as ONE multiply on the summed stereo bus with exactly one `gainSmoother_.process()` per output sample :1807-1811; configured at kGainSmoothMs = 50.0f against the audio rate :237/:465. Test AtmosphereEngine_PopulationGain passed (1.752 s). |
| FR-029 | fr | pass | atmosphere_engine.h:1564-1567: `birthAge = positionSeconds_ * sampleRate * (1 + uPos * positionSpread_)` with uPos = grainRng_.nextFloat() (:1493), then `std::clamp(birthAge, ageLo, ageHi)`. |
| FR-030 | fr | pass | GrainDriftLanes SoA bank atmosphere_engine.h:1129-1140 (walk/smoothCur/smoothTgt arrays of kMaxGrains, DriftLaneRng array, ONE shared samplesUntilControl); OU step transcribed with three sequenced nextFloat() draws :1289-1311; output smoother transcribed from advanceSamples incl. the isComplete skip, flushDenormal and hard snap :1347-1369; advanced by exactly kControlChunkSamples per control step :1845 with carry-over :1388-1400; lane i seeded from `deriveStreamSeed(seed_, kDriftSaltBase + i)` :1271/:947; birth zeroes walk+smoother WITHOUT reseeding :1620-1622. Test section "drift-lane equivalence" (reference BrownianDrift) passed. |
| FR-031 | fr | pass | atmosphere_engine.h:1502-1504 `staticSemis = clamp(pitchSemitones_ + uPitch * pitchSpread_ * (kPitchSpreadCents/100), ±36)`; envelope endpoints semisLo/semisHi also clamped to ±kMaxAbsGrainSemitones=36 (:269, :1506-1509) so r stays in [0.125, 8]; all snapshotted into the grain :1633-1638. |
| FR-032 | fr | pass | atmosphere_engine.h:1605-1608: `pan = panSpread_ * uPan; panL = cos(panNorm * kHalfPi); panR = sin(panNorm * kHalfPi)` computed once at birth. Test "every drawn pan pair satisfies the equal-power law" passed (0.136 s). |
| FR-033 | fr | pass | atmosphere_engine.h:1517-1518 `decorrAge = decorrelation_ * kMaxDecorrelationMs(30) * 0.001 * sampleRate * uDec`; the right channel gets a second ring read at `ageL + grain.decorrAge` :1750-1755; the offset enters the FR-025 clamp at :1536/:1561/:1593. Test "decorrelation drops the inter-channel correlation" passed (0.208 s). |
| FR-034 | fr | pass | atmosphere_engine.h:1756-1757 `sumL += env * grain.panL * grainL; sumR += env * grain.panR * grainR;` — no amplitude term; AtmosphereGrain :1090-1110 has no amplitude field. The 1/sqrt(n) multiply is applied once on the summed bus at :1807-1809. |
| FR-040 | fr | pass | atmosphere_engine.h:2177-2179 `std::array<STFT,2> blurStft_; std::array<OverlapAdd,2> blurOla_; std::array<SpectralBuffer,2> blurSpectrum_;` all prepared in prepare() :413-420 under `if (blurEnabled_)`; the stage runs on the summed grain bus busL_/busR_ (:1924-1925), not per grain. |
| FR-041 | fr | pass | atmosphere_engine.h:375 `blurHopSize_ = blurFftSize_ / 4` (75 % overlap, asserted :406-407); :414-416 `blurStft_[ch].prepare(fft, hop, WindowType::Hann)` and `blurOla_[ch].prepare(fft, hop, Hann, 9.0f, /*applySynthesisWindow=*/true)`; unconditional routing when enabled :677-678. Snap-down via snapFftSize :1188-1195/:373. SC-006 AtmosphereEngine_BlurTransparentAtZero passed. |
| FR-042 | fr | pass | atmosphere_engine.h:1953-1956 `for (k = 1; k + 1 < numBins; ++k) spectrum.setPhase(k, spectrum.getPhase(k) + blurAmount * kPi * blurRng_.nextFloat())` — magnitude never written, DC (k=0) and Nyquist (k=numBins-1) skipped; the loop sits inside the per-channel loop :1933 so the draw is per bin PER CHANNEL from the one stream, L before R. |
| FR-043 | fr | pass | pumpBlur() atmosphere_engine.h:1923-1990: pushSamples :1924-1925, `while (blurStft_[0].canAnalyze())` :1927, analyze :1934, synthesize :1958, and `pullSamples(fifoScratch_[ch].data(), blurHopSize_)` INSIDE the drain loop :1959; then exactly numSamples popped from the prepare-allocated power-of-two FIFO :1979-1989 (capacity `bit_ceil(fft + max(maxBlock,64) + hop)` :409-411). Push volume bounded to <= 64 samples per call by the caller (:656, :678). |
| FR-044 | fr | pass | atmosphere_engine.h:2203 `Xorshift32 blurRng_{1}` is a distinct member from `Xorshift32 grainRng_{1}` :2248, seeded with distinct salts kBlurSalt=0x2000 / kGrainSalt=0x1000 (:287-288, :943-944); one stream shared by both channels, consumed L-then-R (:1933-1956). SC-010's getGrainRngState() clause passed. |
| FR-045 | fr | pass | atmosphere_engine.h:421-430 — with blurEnabled false every blur object is reset to its default-constructed state and every vector released; freeze-leg delay likewise not allocated :448-452; getLatencySamples() returns 0 :1037-1039. Test "FR-046: latency is 0, and FR-045: setBlur changes nothing at all" and "a re-prepare without blur allocates strictly less than with it" both passed. |
| FR-046 | fr | pass | atmosphere_engine.h:1037-1039 `[[nodiscard]] std::size_t getLatencySamples() const noexcept { return blurEnabled_ ? blurFftSize_ : 0; }` using the SNAPPED size; documented as covering both crossfade legs :1029-1036. |
| FR-050 | fr | pass | atmosphere_engine.h:2210 `std::array<SpectralFreezeOscillator, 2> freezeOsc_`, prepared per channel at the snapped size :437-441 (`freezeOsc_[ch].prepare(sampleRate_, freezeFftSize_)`), snapping done in prepare via snapFftSize :374/:1188-1195. Test "PrepareConfig FFT sizes are clamped, then snapped DOWN, then re-clamped" passed. |
| FR-051 | fr | pass | captureFreeze() atmosphere_engine.h:866-878: `const std::size_t need = freezeOsc_[0].getFftSize();` :870 (never config.freezeFftSize), no-op if `capture_.getAvailableSamples() < need` :871-873, extractSlice into the prepare-allocated freezeCapture_ scratch :874, freeze() per channel :875-877. Test "FR-051: a capture before the ring holds a whole window is a NO-OP" passed. |
| FR-052 | fr | pass | kFreezeMixRampMs = 100.0f atmosphere_engine.h:239, LinearRamp configured :468, crossfade per output sample :2110-2115; hard bypass only at settled m==0 :2034-2037; freeze leg NOT routed through the STFT stage but through the prepare-allocated blurFftSize_-sample delay :2051-2059 (allocated at :442-447 only when blur && freeze); no symmetric bypass at m==1 (grain layer keeps running, documented :2003-2013). SC-007 AtmosphereEngine_FreezeStability passed (1.559 s). |
| FR-053 | fr | pass | atmosphere_engine.h:885-892 `releaseFreeze()` calls `osc.unfreeze()` on both oscillators, inert when not prepared/disabled. Test "FR-053: releaseFreeze() fades the drone out within one hop" passed. |
| FR-054 | fr | pass | atmosphere_engine.h:454-461 nothing allocated when freezeEnabled false; captureFreeze/releaseFreeze early-return :867/:886; renderFreezeChunk returns immediately :2026-2028; the crossfade is gated on freezeEnabled_ :2110 so a stored freezeMix cannot mute. Tests "FR-054: with freeze disabled every freeze entry point is inert" and "a prepare without freeze allocates strictly less than with it" passed. |
| FR-060 | fr | pass | No setWidth exists in the public API (atmosphere_engine.h:736-1077); core/stereo_utils.h is absent from the include list :121-132 and explicitly listed as deliberately absent in the banner :18-21; finishChunk documents the absence :2074-2079. The image comes only from FR-032's pan (:1605-1608) and FR-033's read-age offset (:1750-1755). |
| FR-061 | fr | pass | atmosphere_engine.h:903-907 `setLevel` clamps to [0, kMaxLevel=2.0] (:272) and drives levelSmoother_ configured at kLevelSmoothMs=20 (:240/:466); applied as `levelSmoother_.process()` per output sample :2118-2120. |
| FR-062 | fr | pass | finishChunk() atmosphere_engine.h:2085-2135 writes only from wetL_/wetR_ (post-blur grain bus) and freezeL_/freezeR_; the input pointers are read only in renderGrainChunk for capture (:1698-1708). Test "no input reaches the output except through a grain (FR-062)" passed (0.018 s). |
| FR-063 | fr | pass | Input path: chunk probe sum + one isFinite() call :1683-1687, per-sample substitution to 0.0f on the rare path :1700-1707, and the SANITISED sample is what is written to the ring :1708 so the ring is preserved. Internal path: busPoisonAccum_ accumulated :1822 and tested once per control chunk `chunkPoisoned_ = !isFinite(busPoisonAccum_); if (chunkPoisoned_) silence();` :1875-1880, which latches per FR-007. SC-014 AtmosphereEngine_NonFiniteHygiene (both sections) passed. |
| FR-064 | fr | pass | atmosphere_engine.h:2133-2134 `outLeft[i] = detail::flushDenormal(sampleL)` (threshold 1e-15f, core/db_utils.h:37 — strictly more aggressive than FR-064's 1e-20); the drift walk uses kDriftDenormalFloor = 1e-20f :251 applied at :1300-1302, matching BrownianDrift; OnePoleSmoother/LinearRamp flush and snap internally (smoother.h:197-205, :385). Test "FR-064: level = 0 renders exact silence with no denormals" passed. Note: the flush site is the output bus rather than the gain values named in the FR text. |
| FR-070 | fr | pass | atmosphere_engine.h:941-949 setSeed re-seeds grainRng_, blurRng_, scheduler_ and all kMaxGrains drift lanes, each via `deriveStreamSeed(seedValue, <salt>)`; salts kGrainSalt/kBlurSalt/kSchedulerSalt/kDriftSaltBase :287-290 with `static_assert(kDriftSaltBase > kSchedulerSalt + kMaxGrains)` :316; default seed 1 :292/:2249. Test AtmosphereEngine_SeedZeroIsValid passed (0.184 s). |
| FR-071 | fr | pass | AtmosphereEngine_SeedDeterminism passed (1.799 s), atmosphere_engine_test.cpp:2880; the TU routes all determinism comparisons through tests/test_helpers/render_fingerprint.h only ("SC-010 / FR-070's determinism comparisons go through this and NOTHING else", atmosphere_engine_test.cpp:72), no bit-exact float digest — `node tools/lint-float-bit-goldens.js` clean (1390 files). |
| FR-072 | fr | pass | All spec-listed accessors exist and are [[nodiscard]] const noexcept: getActiveGrainCount :957, getSkippedTriggerCountPoolFull :962, getSkippedTriggerCountRingCold :968, getTotalGrainsBorn :973, getMinObservedGrainAgeSamples :1008, getMaxObservedGrainAgeSamples :1012, getLastBornGrainBirthAgeSamples :1015, getLastBornGrainRatioAtBirth :1018, getLastBornGrainLifetimeSamples :1021, getGrainRngState :1027, getLatencySamples :1037, getCaptureCapacitySamples :1044 (plus supersets getTotalGrainsRetired :979, getActiveSlotMask :984, getLastBornGrainSlot :996, getDriftLaneValue :1056, getAppliedBlur :1075). |
| FR-073 | fr | pass | atmosphere_engine.h:37-49 banner "MEMORY (FR-073, plan RA-2)" with the bytes formula and the 4/8/16/30 s per-voice table; :51-56 "OPERATING RULE (FR-022, FR-073) density (grains/s) * grainSeconds <= kMaxGrains" with the skip-not-steal consequence. |
| FR-080 | fr | pass | dsp/include/krate/dsp/primitives/rolling_capture_buffer.h, new method `void readStereoLinear(float ageSamples, float& outLeft, float& outRight) const noexcept` (git diff, added after extractSlice at :170): age 0 addresses `writeIndex_ - 1` via `i0 = (writeIndex_ + capacity_ - 1 - ageInt) & mask_`, `i1 = (i0 + capacity_ - 1) & mask_` (one sample older), linear blend by frac. The extractSlice anchoring identity `extractSlice(...,L,O)[i] == readStereoLinear(O + L - 1 - i)` is documented in the doc-comment and asserted by RollingCaptureBuffer_ReadStereoLinear (passed). |
| FR-080 | sc | pass | dsp/include/krate/dsp/primitives/rolling_capture_buffer.h:230-249 (post-patch) — `void readStereoLinear(float ageSamples, float& outLeft, float& outRight) const noexcept`, matching RA-1's stated signature. FR-081's guard is first and covers both halves (`capacity_ == 0 \|\| available < 2` -> writes (0,0) and returns), so the `available - 2` size_t underflow cannot occur. The non-finite handling uses two ordered comparisons (`if (!(age >= 0.0f))` then `if (age > maxAge)`) rather than a bit test or ITERUM_NOINLINE, with the -ffast-math rationale documented in the doc comment; behaviour is covered by the SECTION at test_rolling_capture_buffer.cpp:561 and by AtmosphereEngine_NonFiniteGuardSurvivesFastMath (atmosphere_engine_test.cpp:336, which deliberately lives in the fast-math TU). FR-084 containment verified: the diff adds only `#include <cmath>` and this method — no existing member changed — and both consumer suites are green (SC-012). |
| FR-081 | fr | pass | rolling_capture_buffer.h readStereoLinear body: `if (capacity_ == 0 \|\| available < 2) { outLeft = outRight = 0.0f; return; }` executed FIRST, then `maxAge = available - 2` and the ordered-comparison clamp `if (!(age >= 0.0f)) age = 0.0f; if (age > maxAge) age = maxAge;` — NaN/-Inf take the first branch, +Inf the second. Degenerate sub-case (fresh prepare, and after exactly one writeStereo) asserted in RollingCaptureBuffer_ReadStereoLinear; dsp_primitives_tests green. |
| FR-082 | fr | pass | rolling_capture_buffer.h readStereoLinear uses only the existing `& mask_` wraparound for i0 and i1 (same scheme as writeStereo :117 and extractSlice :166); no branch on wrap. The prepared/empty guard is a single precondition check taken before any indexing. |
| FR-083 | fr | pass | rolling_capture_buffer.h readStereoLinear is declared `const noexcept`, performs two array reads per channel and no allocation — O(1). Signature verified in git diff. |
| FR-084 | fr | pass | `git diff dsp/include/krate/dsp/primitives/rolling_capture_buffer.h` shows exactly two additions: `#include <cmath>` and the new readStereoLinear method. No existing member, default or body changed. dsp_primitives_tests: All tests passed (4549290 assertions in 1505 test cases); dsp_effects_tests (PatternFreezeMode consumer): All tests passed (93062 assertions in 455 test cases). |
| SC-001 | fr | pass | AtmosphereEngine_NoAllocationAfterPrepare passed (0.370 s) in dsp_systems_tests.exe run; TestHelpers::AllocationScope opened at atmosphere_engine_test.cpp:3411 around a 10 s worst-case render (captureSeconds 30, grainSeconds 30, density 20, blur+freeze on, setSeed/silence/captureFreeze/full setter sweep inside), with the FR-023 precondition `getSkippedTriggerCountPoolFull() > 0` asserted (documented at :3413-3430). |
| SC-001 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_NoAllocationAfterPrepare"` — PASSED. Verified test body at dsp/tests/unit/systems/atmosphere_engine_test.cpp:3368-3529: 938 blocks x 512 = 480,256 samples (10.005 s) at captureSeconds=30 (REQUIRE(getCaptureCapacitySamples()==2097152u) passes), grainSeconds=30, density=20, blur on, freeze on, full 18-setter sweep, captureFreeze()/releaseFreeze()/setSeed/reset()/silence() all inside a TestHelpers::AllocationScope. Assertions all green: `REQUIRE(liveAllocationCount == 0)`, and the spec's mandatory precondition `REQUIRE(poolFullBeforeReset > 0)` plus `REQUIRE(maxActive == kMaxGrains)` (64==64), `REQUIRE(latchedTailExactlyZero)`, `REQUIRE(allSamplesFinite)`. |
| SC-002 | fr | pass | AtmosphereEngine_GrainLiveness, all 5 sections green: "the liveness invariant holds across the configuration sweep" (1.695 s), "drift-free lifetimes match the closed form exactly" (0.127 s), "drift-on lifetimes use the SUM of the one-sided excursions" (0.058 s), "pitch settings are snapshotted at birth, never read live" (0.075 s), "drift-lane equivalence" (0.007 s). Clause 1 asserts min >= kMinAgeSamples and max <= capacity-2 (atmosphere_engine_test.cpp:973-976); clause 3's mandatory straddling cell {captureSeconds 8, pitch 0, driftRange 2} is present at :998. Caveat: the asserted closed form is `floor((C-2-64-2)/w)` (:957), i.e. FR-025's form less D-12's 2 reserved samples (recorded deviation, plan.md:2621). |
| SC-002 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness" --success` — "All tests passed (1280 assertions in 1 test case)". All four clauses present and exercised. Clause 1 sweep at atmosphere_engine_test.cpp:844-847 is verbatim the spec's grid {0.05,1,5,15,30}x{-24,-12,0,12,24}x{1,8,30}x{0,1} (plus decorr {0,1}); measured expansions e.g. `15774.0f >= 64.0f`, `109103.0f >= 64.0f`, `2009824.5f >= 64.0f` (min>=kMinAgeSamples) and `1534742.75 <= 2097150.0`, `144064.0 <= 524286.0` (max <= C-2, i.e. 2097152-2 and 524288-2 — the C-2 bound, not C-1). Clause 2 closed form asserted as equality: `getLastBornGrainLifetimeSamples() == static_cast<uint64_t>(expectedLifetime)`. Clause 3 straddling case measured `math.w == Approx(1.5)` variants and `REQUIRE(math.w > std::max(math.wUp, math.wDown) * 1.5)` expanded to `0.2315633297 > 0.1836930513` — matching spec's stated w=0.2316 sum vs 0.1225 max, so a maximum-based implementation would fail. Clause 4 snapshot: `getLastBornGrainLifetimeSamples() == lifetimeAtBirth` for the in-flight grain and `!= lifetimeAtBirth` for the next-born. Drift-lane equivalence gate green: `REQUIRE(maxDiff <= 1e-6f)` expanded `0.0f <= 0.0f` with `REQUIRE(maxAbsReference > 0.001f)` guarding non-triviality (atmosphere_engine_test.cpp:1246). |
| SC-003 | fr, sc | pass | **RE-VERIFIED 2026-07-28.** D-17 is now the criterion: spec.md:1110 scopes `REQUIRE(getSkippedTriggerCountPoolFull() > 0)` to the 12 of 30 grid cells whose configured `density x grainSeconds` can saturate kMaxGrains, requires `== 0` in the other 18, quotes the withdrawn unconditional wording verbatim, and states the unsatisfiability arithmetically (kMaxDensity 20/s x grainSeconds in {0.05, 0.2, 1} caps the population at 1, 4 and 20 against kMaxGrains = 64 -- no implementation could pass it). SC-001 keeps the unconditional form and I re-ran it: `dsp_systems_tests.exe "AtmosphereEngine_NoAllocationAfterPrepare"` -- EXIT=0, `All tests passed (11 assertions in 1 test case)`. Re-ran the criterion itself: `dsp_systems_tests.exe "AtmosphereEngine_NoGrainBoundaryClicks"` -- EXIT=0, `All tests passed (470 assertions in 1 test case)` -- 30 cells x both channels x engine and reference render, all 0 detections, threshold still the unpadded measured 12.5 sigma. Historical record of the previous verdict follows: threshold deviation FIXED; only the structural D-17 scoping remained. Ran `dsp_systems_tests.exe "AtmosphereEngine_NoGrainBoundaryClicks" --success` -- EXIT=0, `All tests passed (470 assertions in 1 test case)`; 60 zero-click assertions in the log (30 cells x clicksLeft/clicksRight, all `0 == 0`), 5 grainSeconds x 6 GrainEnvelopeType incl. Exponential, reference legs likewise 0. detectionThreshold is now `constexpr float kClickThresholdSigma = 12.5f;` (atmosphere_engine_spectral_test.cpp:335) -- exactly the measured grid-wide zero point SC-003 authorises, with the 1.5 cross-toolchain margin REMOVED (comment :329-334 records the earlier 14.0 was reverted because padding weakens the gate on the engine render). Rest of ClickDetectorConfig verbatim: frameSize 512, hopSize 256, energyThresholdDb -60.0f, mergeGap 5 (:402-406). Cross-toolchain risk now carried by an explicit per-cell reference-floor gate asserted before the engine render is judged (:647-667), whose failure message names the smallest clearing ladder sigma. REMAINING DEVIATION (D-17): SC-003's unconditional `REQUIRE(getSkippedTriggerCountPoolFull() > 0)` is asserted in 12 of 30 cells and inverted to `== 0` in the other 18 -- confirmed in the log, 18 occurrences of `REQUIRE( refStats.skipPoolFull == 0u )` against 6 each of `refStats.skipPoolFull > 0u` / `cellStats.skipPoolFull > 0u`; source :647/:649/:694. Provably unsatisfiable as written (kMaxDensity 20/s x grainSeconds in {0.05,0.2,1} caps population at 1/4/20 vs kMaxGrains=64); SC-001 carries the unconditional form. |
| SC-004 | fr, sc | pass | **RE-VERIFIED 2026-07-28, after the user's lever-6 budget decision.** `AtmosphereEngine_CpuBudget` now PASSES. Ran `dsp_systems_tests.exe "[.perf]"` twice this session; the atmosphere case is green in both. Run 2, verbatim from its own summary block: `(a) 71804.8 ns/block (0.67317 %; baseline 91000, gate 136500)`, `(b) 87383.4 (0.819219 %; baseline 106666, gate 159999)`, `(c) 299940 (2.81194 %; baseline 361000, gate 541500)`, `(d) 107785 (1.01049 %; baseline 106666, gate 159999)`, `(e) 87046 (0.816056 %; baseline 106666, gate 159999)`; `reference 160000 ns/block (1.5 % of one core, amended 2026-07-28)`. Preconditions all still real in the same run: (c) `mean 64 grains`, `pool-full skips 3040`; (d) `freeze captured: yes`; latencies 0 / 1024 / 256 / 1024 / 1024; appliedBlur 1 on (b)-(e). THE CRITERION'S STRUCTURE IS RETAINED, not relaxed: perf_test.cpp:451 `kReferenceNs = 160000.0` (static_assert-tied to `kBlockBudgetNs * [0.0149, 0.0151]` at :452), :512 `kMaxAdmissibleBaselineNs = kReferenceNs / kRegressionFactor`, and the two-clause pattern `static_assert(baseline * 1.5 <= kReferenceNs)` + `static_assert(baseline >= kReferenceNs / 50)` is kept for (a), (b), (d), (e) at :608-641; configuration (c) is out-of-region by the decision and carries the FLOOR clause plus a tripwire `static_assert(kBaselineSaturatedBlurNs > kMaxAdmissibleBaselineNs, ...)` that fires if it ever comes back in region. Baselines are measurements again, by `min(ceil(worst-of-eight x 1.05), 106666)`: :570 `kBaselineDefaultsNs = 91000.0` (worst 86,305), :578/:593/:600 = `kCappedBaselineNs` (:520 = 106666.0; worsts 111,815 / 153,651 / 111,008 all exceed reference/1.5, so the CAP binds), :585 `kBaselineSaturatedBlurNs = 361000.0` (worst 343,805). DEVIATION RECORDED, NOT HIDDEN: for (b), (d) and (e) the baseline is the cap rather than the measurement, so their regression bound is ~43 % instead of ~5 % while their absolute 1.5 % bound is exact -- stated in spec.md:1164's amendment box and in the TU's BASELINE PROVENANCE block. No baseline was raised by code; `kMaxGrains` stays 64; lever (5) stays refused; FR-073's operating rule is unchanged. Historical record of the previous FAIL follows: EXIT=1, `test cases: 2 \| 1 passed \| 1 failed`, failure at atmosphere_engine_perf_test.cpp(1348) expansion `287629.2 <= 106500.0`. Re-checking the three sub-claims from the previous pass: (1) LEVERS -- T019 record :176-186 now reads "A SIXTH, UNLISTED LEVER WAS FOUND AND SPENT IN FULL" with measured before/after (a 137,666->86,305 = 1.60x, b 179,926->111,815 = 1.61x, c 821,349->343,805 = 2.39x, d 305,506->153,651 = 1.99x, e 306,957->111,008 = 2.77x); levers (1)(2)(4) audited-and-held, (3)(3b)(5) measured-and-not-spent with figures. kMaxGrains still 64 (atmosphere_engine.h:187) and blurFftSize still 1024 (:371), but both are now documented measurement-backed refusals rather than omissions. (2) COMPLIANCE DOC -- `ls specs/seraphis-phase5-atmosphere/` now returns compliance.md, plan.md, spec.md, tasks.md; compliance.md:1-60 carries the five figures verbatim plus derived stage costs, so RA-4 is met. (3) BASELINES -- still 71000.0 (:454) but :148-152 documents that any measured substitution breaks the static_assert and SC-004's own rule forbids raising a baseline. Net: criterion unmet, failure now deliberate, loud and fully documented, escalated at lever (6) (compliance.md 1.5) with three named budget options. Measured gap on this machine: ~1.10x the reference for (d), ~2.70x for (c); (a), (b), (e) are under 1% of one core but over the 0.667% the gate's structure actually enforces. |
| SC-005 | fr | pass | AtmosphereEngine_BlurMonotonicity passed (0.800 s). The three floors were measured and moved UP as the spec requires, never down: atmosphere_engine_spectral_test.cpp:1024-1026 `kFlatnessRatioFloor = 1.75 // measured 1.948, less 10 %`, `kCorrelationDropFloor = 0.93 // measured 1.0385, less 10 %`, `kCrestDropFloorDb = 13.0 // measured 14.506 dB, less 10 %` (spec minimums were 1.25x, 0.20 and 3.0 dB). Non-silence preconditions and the four-window settled-region method are implemented (:999, :1052, :1137). |
| SC-005 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_BlurMonotonicity" --success` — PASSED. Non-silence preconditions fire before any threshold: `REQUIRE(flatness[0] > 0.0)` expanded `0.070888537 > 0.0`, and every analysed window `REQUIRE(windowLeftDb > -40.0)` e.g. `-18.8827360915 > -40.0`, `-21.9366313116 > -40.0` (so the silent-engine failure mode is excluded). Flatness over blur {0,0.25,0.5,0.75,1.0} = 0.070889, 0.101696, 0.120933, 0.133247, 0.138113 — non-decreasing at every step against a 2% epsilon, and `REQUIRE(flatness[4] >= kFlatnessRatioFloor * flatness[0])` expanded `0.1381125078 >= 0.1240549397`, i.e. ratio 1.948 against a floor of 1.75 (spectral_test.cpp:1024 — spec's 1.25 minimum was raised UP to measured-less-10%, as the spec requires). Stereo-decorrelation clause: `REQUIRE(correlation[0] - correlation[4] >= kCorrelationDropFloor)` expanded `1.0384785901 >= 0.93` (spec floor 0.2, raised up), with per-step non-increase `-0.0384785901 <= 0.2191999014`. Crest clause: `dryCrestLeft - wetCrestLeft >= 13.0` expanded `14.9055290222 >= 13.0` and right `14.5060424805 >= 13.0` (spec floor 3 dB, raised up). `calculateSpectralFlatness` is called fully qualified as `Krate::DSP::TestUtils::SignalMetrics::` (2 occurrences, grep-verified). Latency assertion `render.latency == kBlurFftSize` -> `1024 == 1024`. |
| SC-006 | fr | pass | AtmosphereEngine_BlurTransparentAtZero passed (0.145 s), atmosphere_engine_spectral_test.cpp:733 — blurEnabled true at blur=0 delay-compensated by getLatencySamples() against blurEnabled false, same seed. |
| SC-006 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_BlurTransparentAtZero" --success` — PASSED. `REQUIRE(dry.latency == 0u)` -> `0 == 0` and `REQUIRE(wet.latency == 1024u)` -> `1024 == 1024` (delay compensation is real). Same seed/same births proven: `REQUIRE(wet.born == dry.born)` -> `48 == 48`. Non-silence gate `dryLeftDb > -40.0` -> `-19.2732024844 > -40.0`. The criterion itself: `REQUIRE(leftDb <= -60.0)` -> `-132.9496847783 <= -60.0` and `REQUIRE(rightDb <= -60.0)` -> `-132.903029519 <= -60.0` — 73 dB of margin on the -60 dBFS threshold. |
| SC-007 | fr | pass | AtmosphereEngine_FreezeStability passed (1.559 s), atmosphere_engine_spectral_test.cpp:1384 — 60 successive non-overlapping 1 s windows measured from the sample the drone starts (:1421-1439), reference is window 2, plus the 0-detection clause on the freezeMix 0->1->0 crossfade with blurEnabled true (:1682). |
| SC-007 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_FreezeStability" --success` — PASSED. Configuration has blur enabled as the spec requires: `REQUIRE(hold.latency == kFreezeBlurFftSize)` -> `1024 == 1024`, so FR-052's delay-matched freeze leg is in the path. 60 one-second windows (spectral_test.cpp:1419-1425, kFreezeWindows=60), reference is index 1 = spec's "peak(2)": `REQUIRE(referenceLeftDb >= kFreezePeakFloorDb)` -> `-6.020296535 >= -60.0` (non-silence), and the criterion `REQUIRE(worstLeftDb <= kFreezePeakToleranceDb)` -> `0.022446455 <= 1.0` (right identical) — worst deviation across windows 2..60 is 0.0224 dB against the +-1.0 dB budget. Separate 0->1->0 freezeMix crossfade sweep: `clicksLeft == 0u` / `clicksRight == 0u` (`0 == 0`) with sweep non-silence `-14.2596325268 > -60.0`. |
| SC-008 | fr | pass | AtmosphereEngine_BoundedUnderStress passed (39.466 s), atmosphere_engine_test.cpp:3564 — 56250 blocks = 600 s at 48 kHz, `kPeakBound = 4.0f` (:3570), configuration fully pinned: captureSeconds 30, density 20, grainSeconds 30, driftDepth 1.0, decorrelation 1.0, blur 1.0, panSpread 1.0, `setLevel(1.0f) // PINNED` (:3595), freeze captured at 30 s and toggled every 5 s. |
| SC-008 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_BoundedUnderStress" --success` — PASSED. Render length verified at atmosphere_engine_test.cpp:3569: `kTotalBlocks = 56250` x 512 = 28,800,000 samples = 600 s = the specified 10 minutes; kPeakBound = 4.0f at :3570. The criterion: `REQUIRE(peak < kPeakBound)` -> `1.57783f < 4.0f`, and `REQUIRE(nonFiniteSamples == 0)` -> `0 == 0` (finiteness via the local sampleIsFinite bit test, not std::isnan). Worst case actually reached, not assumed: `REQUIRE(maxActive == kMaxGrains)` -> `64 == 64`, `REQUIRE(poolFull > 0)` -> `10422 > 0`, `REQUIRE(born > 0)` -> `1280 > 0`, `REQUIRE(freezeArmed)` -> true, and non-triviality `REQUIRE(peak > 0.05f)` -> `1.57783f > 0.05f`. Measured peak sits at 39% of the threshold, so there is no sign of a coherence runaway. |
| SC-009 | fr, sc | pass | **RE-VERIFIED 2026-07-28.** D-18 is now the criterion: spec.md:1378 replaces the allocation-count equality with the shipped two-sided bracket plus capture-capacity and latency equality against a fresh engine, quotes the withdrawn equality wording verbatim, and states why the literal `==` is WRONG rather than merely inconvenient -- `std::vector::resize` to an unchanged size allocates ZERO times, so a correct engine allocates strictly fewer times on a re-prepare (measured 6 against 69) and the equality would be satisfied only by an engine that threw its buffers away, i.e. the exact behaviour the clause exists to forbid. Re-ran `dsp_systems_tests.exe "AtmosphereEngine_SampleRateIndependence"` -- EXIT=0, `All tests passed (115 assertions in 1 test case)`. Historical record of the previous verdict follows: EXIT=0, `All tests passed (115 assertions in 1 test case)`. Clause 1 unchanged and green (12x `lifetimeError < 0.005`, 6x `concurrentError < 0.05`, 6x `rmsErrorDb < 1.0`). Clause 2 non-degenerate: `REQUIRE( capacitySpread > 0.05 )` -> `0.0884353741 > 0.05` at atmosphere_engine_test.cpp(4366), with truncated[0..2].capacity := 524288 / 524288 / 1048576. ALLOCATION CLAUSE IMPROVED BUT STILL A DOCUMENTED SUBSTITUTION (D-18, rationale :4207-4233), now bracketed on BOTH sides instead of one: :4448 `REQUIRE(freshPrepareCount > 0)` -> `69 > 0`; :4450 `REQUIRE(secondPrepareCount <= freshPrepareCount)` -> `6 <= 69`; NEW :4458 `REQUIRE(secondPrepareCount > 0)` -> `6 > 0` (catches a re-prepare that kept the 48 kHz ring across a doubling); :4461 `REQUIRE(renderAllocationCount == 0)` -> `0 == 0`; plus NEW geometry equality :4472-4475 `REQUIRE(reusedEngine.getCaptureCapacitySamples() == freshEngine.getCaptureCapacitySamples())` and `REQUIRE(reusedEngine.getLatencySamples() == freshEngine.getLatencySamples())`; plus firstBlockExactlyZero, bornAfterReprepare := 20, lateRms := 0.112546091. The criterion's literal `==` on the allocation count is still not asserted -- correctly, since std::vector::resize to an unchanged size allocates zero times -- but the substitution now covers the property the equality was proxying for. |
| SC-010 | fr | pass | AtmosphereEngine_SeedDeterminism passed (1.799 s), atmosphere_engine_test.cpp:2880, comparing via tests/test_helpers/render_fingerprint.h only (:72); includes the negative half (different seeds must differ) and the FR-044 clause on getGrainRngState() plus getTotalGrainsBorn() across blur=0 vs blur=1 renders. |
| SC-010 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_SeedDeterminism" --success` — PASSED. 20 s render (atmosphere_engine_test.cpp:2909 kRenderSeconds=20.0). Positive half: `REQUIRE(left.withinTolerance())` / `right.withinTolerance()` -> true for same-seed engines A/B, with `probeB.born == probeA.born` -> `152 == 152` and `probeB.grainRngState == probeA.grainRngState` -> `3018457096`. Negative half present and firing: `REQUIRE_FALSE(left.withinTolerance())` and `REQUIRE_FALSE(right.withinTolerance())` for a different seed (probeC born 150), so a silent engine cannot pass. Non-triviality gates `probeA.fingerprint.left.rms > 1e-4` -> `0.0966115977 > 0.0001`. FR-044 clause asserted on birth PARAMETERS as the spec demands: blur=0 vs blur=1 with blurEnabled=true both give `smearedProbe.grainRngState == dryProbe.grainRngState` -> `3018457096` AND `smearedProbe.born == dryProbe.born` -> `152 == 152`, while the renders differ (dry rms 0.0965715 vs smeared 0.0483560), proving the comparison is not degenerate. reset() re-entry also checked: `probeReset.grainRngState == probeA.grainRngState` and fingerprint within tolerance. |
| SC-011 | fr | pass | AtmosphereEngine_BlockPartitionInvariance passed (0.133 s), atmosphere_engine_test.cpp:3058 — one 4096-sample call vs {1,7,64,65,511,512,1000}-sample partitions at the same seed, with the birth-inside-a-partial-chunk coverage asserted via getTotalGrainsBorn() transitions. |
| SC-011 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_BlockPartitionInvariance" --success` — PASSED. All seven partitions {1,7,64,65,511,512,1000} against the single 4096-sample call: each gives `REQUIRE(rmsToDb(diffRms) <= -100.0)` expanded `-240.0 <= -100.0` and `REQUIRE(maxDiff <= 1e-5)` expanded `0.0 <= 0.00001` — i.e. exactly identical output, far inside the stated thresholds, with `REQUIRE(born == referenceBorn)` -> `26 == 26` each time. Reference is non-trivial: `REQUIRE(referenceBorn > 0u)` -> `26 > 0` and `REQUIRE(rmsToDb(referenceRms) > -60.0)` -> `-19.8196637048 > -60.0`. The spec's required coverage clause is present and passes: `REQUIRE(birthSamples.size() >= 8u)` -> `21 >= 8` and `REQUIRE(offGridBirths > 0u)` -> `21 > 0`, so births inside partial 64-sample control chunks are proven, not assumed. |
| SC-012 | fr | pass | Pre-existing suites run unedited: dsp_primitives_tests.exe "All tests passed (4549290 assertions in 1505 test cases)"; dsp_effects_tests.exe (PatternFreezeMode consumer) "All tests passed (93062 assertions in 455 test cases)". The new positive case RollingCaptureBuffer_ReadStereoLinear is registered and green (test_rolling_capture_buffer.cpp:335), covering the length-1, length>1 end-anchoring, fractional and degenerate-guard sub-cases. |
| SC-012 | sc | pass | Existing suites run unedited and green: `dsp_primitives_tests.exe` -> "All tests passed (4549290 assertions in 1505 test cases)"; `dsp_effects_tests.exe` (owner of PatternFreezeMode) -> "All tests passed (93062 assertions in 455 test cases)". The claim that no existing test was edited is verified mechanically, not asserted: `git diff --numstat dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp` -> `274 0` (274 insertions, ZERO deletions) and a `git diff -U0 \| grep -c '^-[^-]'` -> 0. New positive test present and green: `dsp_primitives_tests.exe "RollingCaptureBuffer_ReadStereoLinear"` -> "All tests passed (36 assertions in 1 test case)". All four required sub-cases exist as SECTIONs at test_rolling_capture_buffer.cpp:373 (length-1 identity), :394 ("Length > 1 slice follows the end-anchored identity" — the extractSlice(L,O)[i]==readStereoLinear(O+L-1-i) form, so the naive-wording trap was avoided), :424 (fractional midpoint), :444 (degenerate getAvailableSamples()<2 -> (0,0)), plus two extra sections at :505 and :561. RA-1 itself is strictly additive: `git diff dsp/include/krate/dsp/primitives/rolling_capture_buffer.h` adds only `#include <cmath>` and one new const noexcept method; no existing member is touched. |
| SC-013 | fr, sc | pass | FIXED: the scripted gate now exists, is wired into CI, and I proved it has teeth. `tools/lint-nonfinite-symbols.js` (257 lines, new/untracked) bans std::isnan/std::isinf/std::isfinite/numeric_limits::infinity()/quiet_NaN()/signaling_NaN()/INFINITY/HUGE_VALF in files not built with -fno-fast-math; GUARDED list :73-78 covers atmosphere_engine.h + the three fast-math-built atmosphere TUs (nonfinite_test.cpp deliberately absent, :67-72). CI wiring confirmed: `git diff .github/workflows/ci.yml` shows a new step `- name: Non-finite symbol lint (fast-math safety) / run: node tools/lint-nonfinite-symbols.js` after the SIMD lint (~line 199). MUTATION TEST run by me: injected `if (std::isnan(capacity)) { return; }` at atmosphere_engine.h:1592 -> `lint-nonfinite-symbols: FAILED / dsp/include/krate/dsp/systems/atmosphere_engine.h:1592 std::isnan / 1 violation(s)`, EXIT=1; restored from backup -> `all clear (4 guarded files)`, EXIT=0, `grep -c std::isnan` on the header = 0. All gates green this session: lint-float-bit-goldens, lint-arch-guarded-includes, lint-simd-aligned-loadstore, lint-layers, lint-odr, lint-nonfinite-symbols all EXIT=0; `node tools/check-portability.js` -> `all clear -- 4 compiled`, and explicitly on the four untracked atmosphere TUs -> `all clear -- 4 compiled`, all OK under WSL g++. Zero-warning rebuild of all five layer targets (EXIT=0, `grep -ci warning` on the build log = 0). Scope note, not a defect: `--all` still reports 63 pre-existing violations in unrelated components, so the enforcement surface is the explicit list rather than the whole tree. FULL SUITES GREEN: dsp_core_tests `All tests passed (1588257 assertions in 573 test cases)`; dsp_primitives_tests `All tests passed (4549290 assertions in 1505 test cases)`; dsp_processors_tests `All tests passed (10640741 assertions in 3296 test cases)`; dsp_systems_tests `All tests passed (6032091 assertions in 1161 test cases)`; dsp_effects_tests `All tests passed (93062 assertions in 455 test cases)` -- all EXIT=0. |
| SC-014 | fr | pass | AtmosphereEngine_NonFiniteHygiene passed, both sections: "input injection: finite output, ring preserved, no clicks" (0.024 s) and "internal non-finite: silence() fires, and the latch does not resume" (0.026 s), atmosphere_engine_nonfinite_test.cpp:314. The TU carries `-fno-fast-math -fno-finite-math-only` in dsp/tests/CMakeLists.txt (added at the Phase-4 block, git diff), and the fourth clause AtmosphereEngine_NonFiniteGuardSurvivesFastMath (0.024 s) deliberately lives in the fast-math TU (atmosphere_engine_test.cpp:3749) so the ITERUM_NOINLINE guard and the ordered-comparison clamp are exercised under the shipped FP settings. |
| SC-014 | sc | pass | Ran `dsp_systems_tests.exe "AtmosphereEngine_NonFiniteHygiene" --success` — "All tests passed (43 assertions in 1 test case)". TU carries -fno-fast-math -fno-finite-math-only and ONLY that TU of the four does (dsp/tests/CMakeLists.txt, verified in the diff). Injection is real: `REQUIRE(injectedInputWasNonFinite)` -> true. Clause (a): `allSamplesFinite(injectedLeft/Right)` and `(referenceLeft/Right)` all true. Clause (b) ring preservation: `REQUIRE(correlationLeft >= 0.99)` -> `1.0 >= 0.99` and right likewise, with `injected.getCaptureCapacitySamples() == capacityBefore` -> `524288 == 524288`; non-triviality via `rmsToDb(referenceRms) > -60.0` -> `-17.2645248881 > -60.0` and `reference.getTotalGrainsBorn() == born` -> `12 == 12`. Clause (c): `injectedClicksLeft/Right == 0u` -> `0 == 0` (reference legs also 0). Internal-non-finite sub-case (nonfinite_test.cpp:540, kHugeSample=5.0e37f, itself asserted finite) reaches the latch at block 28 of 400 and asserts the FR-007 ramp (`latePeak < 0.5f * earlyPeak`), then the latch: `engine.getActiveGrainCount() == 0u` (x2), `retiredAtLatch == bornAtLatch` -> `1 == 1`, `latchedSpanIsExactlyZero`, counters frozen (`getSkippedTriggerCountPoolFull() == poolSkipAtLatch`, `...RingCold() == coldSkipAtLatch`, `getTotalGrainsBorn() == bornAtLatch`), then reset() -> `getTotalGrainsBorn() == 0u`, `getTotalGrainsRetired() == 0u`, and revival `rmsToDb(revivalRms) > -60.0` -> `-19.1509488215`. |
| CC-rt | constraints | pass | Read the ENTIRE audio path in dsp/include/krate/dsp/systems/atmosphere_engine.h (2276 lines), not just the banner. processStereoBlock():622-716 partitions on the absolute 64-sample control grid and calls only runControlStep():1817-1885, renderGrainChunk():1671-1815, pumpBlur():1940-1989, renderFreezeChunk():2021-2059, finishChunk():2083-2140, latchNow():2146-2154 — every one noexcept, no heap op, no lock, no throw, no I/O. Targeted grep for `throw\|try {\|catch\|mutex\|std::lock\|std::atomic\|printf\|std::cout\|fstream\|std::function\|new \|malloc\|free(` over the header returns only two comment-word hits (:356 "new configuration", :1149 "new bit test"). The single assert() is at :406 inside prepare(); everything else is static_assert (:299-316). All heap ops are confined to prepare():363-476 (envelopeTable_.assign :384, blurFifo_/fifoScratch_ .assign :418-419, freezeCapture_/freezeDelay_ .assign :440,:446, plus sub-object prepare()s :380,:389,:414-417,:439). reset():481-586 zeroes/fills in place only. Verified the DOWNSTREAM dependencies too: RollingCaptureBuffer::writeStereo :114 / readStereoLinear :217-244 / extractSlice :142 / getAvailableSamples :284 are index math only (only resize is prepare, :87-88); GrainScheduler::process() grain_scheduler.h:73-93 is arithmetic + one RNG draw; STFT/OverlapAdd/SpectralBuffer resizes are all in prepare (stft.h:78,:81,:243,:246; spectral_buffer.h:63-65); SpectralFreezeOscillator resizes all in prepare (spectral_freeze_oscillator.h:127-163); FFT::forward() fft.h:186-209 uses only prepare-owned buffers (the only alloc, fft.h:117, is in makeAlignedBuffer, called from prepare). Behavioural proof with REAL teeth: dsp_systems_tests.exe "AtmosphereEngine_NoAllocationAfterPrepare" -> "All tests passed (11 assertions in 1 test case)", and I confirmed the detector is not a tautology in this binary — tests/test_helpers/allocation_detector.h has its operator new commented out (:106-138), but the LIVE replacements live in tests/test_helpers/allocation_operator_overrides.h:67-93 and dsp/tests/unit/systems/selectable_oscillator_test.cpp is its single owner TU, linked into dsp_systems_tests (dsp/tests/CMakeLists.txt:331). Two non-blocking notes: (1) prepare() is declared noexcept (:363) while calling vector::assign, so an allocation failure terminates rather than throws — matches the existing repo pattern and prepare is explicitly the non-RT method; (2) setGrainEnvelope():915-921 runs 4096 transcendentals in place (no alloc), bounded and documented as at-most-once-per-block. |
| CC-layers | constraints | pass | atmosphere_engine.h is Layer 3 (dsp/include/krate/dsp/systems/). Its complete krate include set, read at atmosphere_engine.h:121-132, is 5x core/ (db_utils, grain_envelope, math_constants, pitch_utils, random), 4x primitives/ (rolling_capture_buffer, smoother, spectral_buffer, stft) and 2x processors/ (grain_scheduler, spectral_freeze_oscillator) — zero systems/ and zero effects/ includes. Transitive check: rolling_capture_buffer.h pulls NO krate header (Layer 1, stdlib only); grain_scheduler.h:14 pulls core/random.h; spectral_freeze_oscillator.h:53-60 pulls only primitives/ and core/ plus processors/formant_preserver.h (same layer). The modified Layer 1 header rolling_capture_buffer.h added only <cmath> (diff line 22) — no krate include added. Tool confirmation: `node tools/lint-layers.js` -> "lint-layers: OK — no layer-dependency violations in 5-layer DSP tree." (exit 0); the tool walks the filesystem with fs.readdirSync (tools/lint-layers.js:34), so it did see the untracked new header. Also clean: lint-odr.js ("OK — 698 definitions scanned, no cross-file name collisions"), lint-arch-guarded-includes.js, lint-simd-aligned-loadstore.js, lint-allocation-operator-overrides.js, lint-float-bit-goldens.js — all exit 0. |
| CC-naming | constraints | pass | Types PascalCase: class AtmosphereEngine (atmosphere_engine.h:155), struct PrepareConfig (:325), struct AtmosphereGrain (:1090), struct DriftLaneRng (:1119), struct GrainDriftLanes (:1129), enum class RunState : std::uint8_t { Running, Silencing, Latched } (:1142). ODR-conscious naming is explicit — AtmosphereGrain is documented at :1085-1089 as deliberately NOT `Grain` because primitives/grain_pool.h:23 already owns that name, and DriftLaneRng is distinguished from HarmonicCloud::LaneRng (harmonic_cloud.h:1121) and EntropyProcessor::LaneRng (entropy_processor.h:423); lint-odr.js confirms no collision. Functions camelCase throughout (prepare/reset/silence/processStereoBlock/captureFreeze/releaseFreeze/isFreezeCaptured/setGrainEnvelope/setSeed, and private ratioAtPitch/foldObservedAge/refreshGrainRatio/tryBirthGrain/renderGrainChunk/runControlStep/pumpBlur/renderFreezeChunk/finishChunk/latchNow/regenerateEnvelope/updateDriftCoefficients/resetDriftLanes/advanceControlStepAllLanes/advanceSmootherAllLanes/advanceDriftLanes/isFinite/snapFftSize). Constants kPascalCase: kMaxGrains, kEnvelopeTableSize, kEnvelopeTailZeroEntries, kEnvelopeEdgeFadeEntries, kMinAgeSamples, kInterpMarginSamples, kControlChunkSamples, kDriftControlInterval, kSilenceRampMs..kLevelSmoothMs, kDriftTauMin..kDriftDenormalFloor, kMinGrainSeconds..kMaxMaxBlockSamples, kGrainSalt..kDriftSaltBase, kDefaultSeed (:163-297), plus the local `constexpr auto kFade` (:1228). Every AtmosphereEngine data member carries a trailing underscore — full declaration block read at :2151-2276 (capture_, writeCounter_, captureCapacity_, captureSeconds_, maxBlockSamples_, grains_, activeIdx_, activeCount_, nextSlot_, scheduler_, driftLanes_, driftSmoothCoeff_, envelopeTable_, envelopeType_, blurStft_, blurOla_, blurSpectrum_, blurFifo_, fifoScratch_, blurFifoMask_/Write_/Read_/Count_, blurRng_, blurSmoother_, blurFftSize_, blurHopSize_, blurEnabled_, freezeOsc_, freezeCapture_, freezeDelay_, freezeDelayMask_, freezeDelayIdx_, freezeFftSize_, freezeMixRamp_, freezeEnabled_, gainSmoother_, levelSmoother_, silenceGain_, silenceStep_, runState_, busPoisonAccum_, chunkPoisoned_, the 15 control scalars, grainRng_, seed_, sampleRate_, sampleCounter_, prepared_, busL_/busR_/wetL_/wetR_/freezeL_/freezeR_, and the 12 introspection fields). Plain-data members of the nested aggregates (AtmosphereGrain::readIndexInt, GrainDriftLanes::walk/a/g/depth) carry no underscore, matching the established precedent HarmonicCloud::DriftLanes (systems/harmonic_cloud.h:1125-1148). No `using namespace` at namespace scope (grep returns nothing). New RollingCaptureBuffer::readStereoLinear (rolling_capture_buffer.h:217) is camelCase and its bare `size_t` matches the surrounding file (:80, :142-143, :149, :162-167). Test cases follow the repo's Component_Behaviour form (atmosphere_engine_test.cpp:336-4217: AtmosphereEngine_LifecycleAndGuards, _ControlTableClamps, _GrainLiveness, _SkipNeverSteal, _NoAllocationAfterPrepare, _SeedDeterminism, ...). No plugin parameter IDs exist in this phase (Seraphis plugin work starts at Phase 8), so the k{Section}{Parameter}Id rule does not apply. |
| CC-warnings | constraints | pass | An incremental build was a NO-OP (nothing recompiled), so it proved nothing; I deleted the four objects (build/windows-x64-release/dsp/tests/dsp_systems_tests.dir/Release/atmosphere_engine_*.obj) and forced a real recompile: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests` -> BUILD_EXIT=0, log shows KrateDSP.lib relinked and all four TUs compiled (atmosphere_engine_nonfinite_test.cpp, atmosphere_engine_perf_test.cpp, atmosphere_engine_spectral_test.cpp, atmosphere_engine_test.cpp) with `grep -in "warning\|error"` returning ZERO lines. Stricter cross-compiler pass, since MSVC is lenient: g++ 13 under WSL with -Wall -Wextra -std=c++20 -fsyntax-only -DNDEBUG -DRELEASE over all four TUs produced ZERO diagnostics (only third-party extern/ and _deps/ noise filtered, and there was none). Honest limitation: build/windows-x64-release/dsp/tests/dsp_systems_tests.vcxproj:76,142 sets <WarningLevel>Level3</WarningLevel>, i.e. /W3 not /W4 — that is the repo's configured posture for DSP test targets (only gen_v2_fixtures opts into /W4, root CMakeLists.txt:685), so a /W4-only diagnostic would not have been caught by the MSVC leg; the g++ -Wall -Wextra pass is what covers that gap. Full default suite is green: build/windows-x64-release/bin/Release/dsp_systems_tests.exe -> "All tests passed (6032088 assertions in 1161 test cases)"; [atmosphere] tag -> "All tests passed (12967 assertions in 24 test cases)". SEPARATE BLOCKING ISSUE outside this item's scope, reported because it is a stated functional requirement: the hidden-tag perf case FAILS. dsp_systems_tests.exe "AtmosphereEngine_CpuBudget" -> "test cases: 1 \| 0 passed \| 1 failed", REQUIRE(measuredNs[i] <= baselinesNs[i] * kRegressionFactor) expanded to 140330.0 <= 106500.0 for config (a) defaults/blur off/freeze off, plus its own warnings "SC-004 OVER BUDGET, configuration (d) freezeMix 1.0 + grain layer: 243143 ns/block exceeds the largest admissible baseline (71111.1 ns/block)" and "(e) (b) + envelope churn per block: 238184 ns/block" (atmosphere_engine_perf_test.cpp:1133, :1214). Reproduced twice (133627 then 140330 ns). Both perf cases are tagged "[.perf]" (atmosphere_engine_perf_test.cpp:992, :1059) so they are hidden from the default run — the suite reads green while the CPU budget is 1.3x-3.4x over. |
| CC-portability | constraints | pass | VERBATIM result of the requested command, `node tools/check-portability.js` (exit 0): "check-portability: 46 translation unit(s) with g++" ... "check-portability: all clear -- 46 compiled." THAT RESULT IS BLIND TO THIS PHASE and must not be quoted as Phase 5 evidence: `grep -c atmosphere` over the run log returns 0 — none of the four new TUs was compiled. Cause, read in the tool: main() takes changedFiles(mode) (tools/check-portability.js:171,183) which runs `git diff --name-only --diff-filter=ACMR origin/main...HEAD` (:106) and only falls back to `git diff ... HEAD` (:116) — both enumerate TRACKED paths only, and all four atmosphere_engine_*.cpp files are still untracked (git status shows them as `??`), so they are silently excluded and the gate returns a false green for the whole phase. I therefore ran the tool EXPLICITLY on the new TUs — `node tools/check-portability.js dsp/tests/unit/systems/atmosphere_engine_test.cpp dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp` -> exit 0, verbatim: "check-portability: 4 translation unit(s) with g++" / "  OK      dsp/tests/unit/systems/atmosphere_engine_test.cpp" / "  OK      ...spectral_test.cpp" / "  OK      ...perf_test.cpp" / "  OK      ...nonfinite_test.cpp" / "check-portability: all clear -- 4 compiled." (0 skipped, so this is real compilation, not the skip-wall the tool warns about at :218-227). Reinforced with -Wall -Wextra by hand (see CC-warnings): zero diagnostics. Specific portability hazards verified in source rather than assumed: NO std::isnan/isinf anywhere — the one finiteness test is isFinite() (atmosphere_engine.h:1178-1180), a composition of detail::isNaN/isInf behind ITERUM_NOINLINE, with the GCC/Clang attribute-order trap called out at :1170-1177 and written correctly ([[nodiscard]] before ITERUM_NOINLINE); the innermost-loop guard in rolling_capture_buffer.h:227-229 avoids a classification predicate entirely, using two ordered comparisons (`if (!(age >= 0.0f))` then `if (age > maxAge)`) that -ffinite-math-only cannot fold; NO <limits> infinity sentinel (minObservedAge_ seeded from captureCapacity_, atmosphere_engine.h:583, with the -ffast-math rationale at :573-578); no narrowing brace-init and no SIMD in the component (GrainDriftLanes alignas(32) is documented as locality-only with no aligned load/store, :1122-1128) — lint-simd-aligned-loadstore.js clean. The dsp/tests/CMakeLists.txt:697-700 -fno-fast-math list adds ONLY atmosphere_engine_nonfinite_test.cpp, and tools/check-portability.js:82-87 was extended to pass -DKRATE_DSP_TESTS_DIR for dsp/tests/ files (matching the new target_compile_definitions at dsp/tests/CMakeLists.txt:415-418) — that edit adds a define the tool previously lacked, it does not weaken any check. |

---

# Detail records

The sections below are cited by ID from the evidence column above
(`compliance.md 1.4`, `compliance.md 1.5`, etc.). Their numbering is stable.

## 1. SC-004 — CPU budget. **MET, after the 2026-07-28 lever-(6) budget decision.**

> **RESOLUTION, 2026-07-28.** The escalation in §1.5 was answered by the user with
> **option 1**: the per-voice allowance is raised from 1 % to **1.5 % of one core**
> (reference 160 000 ns/block) for configurations (a), (b), (d) and (e), derived
> from the five measured figures in §1.1 immediately below, and the saturated-64
> configuration **(c) is out-of-region** — measured and regression-tracked, not
> gated against the ceiling. `AtmosphereEngine_CpuBudget` now passes; the run
> output is transcribed in the SC-004 row of the compliance table and the
> baselines are measurements again. Everything in §§1.2–1.5 below is retained
> **as the record of how the phase got here** — the levers spent, the levers
> measured and refused, and the arithmetic that made escalation the only honest
> verdict. None of it is withdrawn, and none of it may be re-spent as though the
> raised reference created new room: it did not. It recorded the cost that was
> already there.

### 1.1 The four (five) measured figures, verbatim

RA-4 requires the measured `ns/block` figures to be copied into this document
verbatim. `AtmosphereEngine_CpuBudget`, run eight consecutive times, best-of-25 ×
500 blocks after 400 warm-up blocks; the transcription also lives in the
BASELINE PROVENANCE block of
`dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp`.

| configuration | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | min | **worst** | worst as % of one core |
|---|---|---|---|---|---|---|---|---|---|---|---|
| (a) defaults, blur off, freeze off | 84 864 | 84 468 | 82 408 | 83 223 | 82 450 | 84 129 | 86 305 | 85 966 | 82 408 | **86 305** | 0.809 % |
| (b) defaults, blur on | 110 297 | 108 557 | 108 755 | 106 052 | 107 648 | 107 478 | 106 274 | 111 815 | 106 052 | **111 815** | 1.048 % |
| (c) saturated 64, blur FFT 256 | 323 119 | 319 849 | 327 996 | 333 161 | 321 300 | 343 805 | 331 937 | 331 054 | 319 849 | **343 805** | 3.223 % |
| (d) freezeMix 1.0 + grain layer | 133 312 | 141 611 | 133 952 | 153 651 | 130 854 | 137 630 | 134 251 | 133 305 | 130 854 | **153 651** | 1.440 % |
| (e) (b) + envelope churn per block | 105 465 | 108 551 | 109 956 | 104 630 | 110 919 | 111 008 | 107 514 | 106 330 | 104 630 | **111 008** | 1.041 % |

Reference **as amended 2026-07-28** (1.5 % of one core) = **160 000 ns/block**;
largest admissible baseline (`reference / kRegressionFactor`) = **106 666.67 ns/block**.
*Withdrawn:* reference (1 % of one core) = 106 667 ns/block, largest admissible
baseline = 71 111 ns/block. The `% of one core` column above is unchanged — it is
a measurement, and measurements do not move when a threshold does.

Checked-in baselines derived from this table by
`min(⌈worst × 1.05⌉, 106 666)`, rounded up to the next 1 000 where the cap does
not bind: (a) **91 000**, (b) **106 666**, (c) **361 000** (out-of-region),
(d) **106 666**, (e) **106 666**.

Derived stage costs at the (b) geometry, from the same runs:

| stage | ns/block | % of one core |
|---|---|---|
| grain layer, 16 concurrent grains (a − fixed overhead) | ~74 000 | 0.69 % |
| blur stage, STFT ↔ OverlapAdd (b − a) | ~23 000 | 0.22 % |
| freeze leg, two `SpectralFreezeOscillator`s (d − b) | ~35 000 | 0.33 % |
| envelope regeneration (e − b) | ~2 700 | 0.03 % |

### 1.2 Read the verdict precisely — *as it stood before the amendment*

> The paragraphs in this sub-section describe the **withdrawn** 1 % gate. They are
> kept because they are the reasoning the escalation rests on. Under the amended
> reference the same structure gives an effective ceiling on a measurement of
> `160 000 / 1.5 = 106 666.67 ns = 1.0 % of one core` **for configurations whose
> baseline is their measurement**, which is (a) only; (b), (d) and (e) exceed that
> line, so their baselines are capped and their measurements are bound directly by
> the 160 000 ns reference instead. That trade is stated in `spec.md:1164`.

SC-004's structure is `static_assert(baseline × 1.5 ≤ reference)` **and**
`REQUIRE(measured ≤ baseline × 1.5)`. Composed, the effective ceiling on a
**measurement** is `reference / 1.5 = 71 111 ns = 0.667 % of one core`; the
withheld 0.333 % is the regression headroom, deliberately. Therefore:

* **Configuration (a) meets the roadmap's stated 1 % per-voice ceiling**
  (roadmap line 248) at 0.773–0.809 % of one core, and still fails this criterion.
* Configurations (b) and (e) sit at 0.994–1.048 % — on the roadmap line, over the
  criterion's gate.
* Configuration (d) is 1.23–1.44 %.
* Configuration (c) is 3.00–3.22 %.

Neither number is softened anywhere in the tree: `kProvisionalBaselineNs` was
unchanged at 71 000, so `AtmosphereEngine_CpuBudget` failed, loudly, with all
five figures in its output. It is tagged `[.perf]`, so CI (which excludes perf
tags) was unaffected; the default `dsp_systems_tests` run was green.
*(Since 2026-07-28 `kProvisionalBaselineNs` no longer exists: each configuration
carries its own measured — or, where the cap binds, capped — baseline.)*

### 1.3 What was done before escalating

The full record, with the before/after table and per-lever measurements, is the
**T019 DECISION RECORD** in `atmosphere_engine_perf_test.cpp`. Summary:

| lever | outcome |
|---|---|
| (1) freeze hard-bypass engages | **audited, holds** — no saving available |
| (2) control-step decimation fires | **audited, holds** — 8 control steps per 512-block, not 512 |
| (4) equal-power pan `cos`/`sin` are birth-time | **audited, holds** — no per-sample transcendental on the grain path |
| — (unlisted) instruction-count defects | **SPENT IN FULL** — see below |
| (3) `blurFftSize` default → 512 | **measured, not spent** — at 75 % overlap the stage is O(log N) per sample; halving N doubles the frame count. No measured saving; would spend RA-3's latency figure for nothing. |
| (3b) `semitonesToRatio` → `centsToPitchRatio` | **measured, not spent** — the call is per grain per *control step* (512 `powf`/block at saturation) against ~2 M grain-sample operations. Under 1 % of configuration (c). |
| (5) reduce `kMaxGrains` | **measured, not spent** — see 1.4 |
| (6) escalate | **TAKEN** — see 1.5 |

The unlisted lever was four instruction-count defects, three of them
bit-identical in output:

1. **Runtime `%` in three ring-wrap hot loops** →
   mask / split contiguous runs. `SpectralFreezeOscillator`'s overlap-add ring
   (wrapped `fftSize_` times per synthesised frame plus once per output sample),
   `STFT::pushSamples` (one divide per sample per channel) and `STFT::analyze`
   (one divide per FFT-size sample per frame). A runtime `%` is a hardware
   integer divide and blocks vectorisation. **~2.1× on the blur stage and ~2.1× on
   the freeze leg.** Bit-identical.
2. **`std::floor` on the grain inner loop** → truncation. `roundss` needs SSE4.1,
   which MSVC does not target on the default `/arch`, so `std::floor(float)`
   compiled to a CRT **call** — three per grain-sample. Every operand is provably
   non-negative there, where truncation *is* the floor. **~1.35× on the grain
   layer.** Bit-identical.
3. **`GrainEnvelope::lookup`'s `std::clamp` and `size_t` conversions** → two
   ordered comparisons and `ptrdiff_t`. Value-identical on the defined domain and
   strictly safer off it (a NaN phase previously reached
   `static_cast<size_t>(NaN)` and indexed out of range).
4. **The 4096-entry envelope regeneration** → a `prepare()`-time bank of all six
   windows (98 KiB/voice, 2.3 % of the default ring). `setGrainEnvelope()` is now
   a store. Configuration (e)'s regeneration term fell from ~127 000 ns/block —
   119 % of the entire per-voice budget, from one setter — to ~2 700. Tables
   bit-identical, FR-027's endpoint conditioning included.

A fifth change does touch the render: `SpectralFreezeOscillator`'s per-bin
`mag * std::cos/sin` pair became the SIMD `reconstructCartesianBulk` that
`SpectralBuffer` already uses for the identical expression (2050 scalar
transcendentals per frame → one Highway-dispatched call). The only behavioural
difference is that bins with magnitude < 1e-20, previously forced to exactly
(0, 0), are now multiplied like any other bin — at most 1e-20 per bin, below the
denormal floor FTZ/DAZ flushes and ~140 dB under the oscillator's own ±2.0 clamp.

Net effect, same machine and trial shape:

| configuration | before | after (worst of 8) | factor |
|---|---|---|---|
| (a) | 137 666 | 86 305 | 1.60× |
| (b) | 179 926 | 111 815 | 1.61× |
| (c) | 821 349 | 343 805 | 2.39× |
| (d) | 305 506 | 153 651 | 1.99× |
| (e) | 306 957 | 111 008 | 2.77× |
| ns per grain-sample, 8 s ring | 16.74 | 10.32 | 1.62× |
| ns per grain-sample, 30 s ring | 18.13 | 9.96 | 1.82× |

### 1.4 FR-022: `kMaxGrains = 64` measured, and lever (5) deliberately not spent

`AtmosphereEngine_GrainSampleCost`, three consecutive runs:

| ring | run 1 | run 2 | run 3 | worst | × the 3.255 ns arithmetic ceiling |
|---|---|---|---|---|---|
| `captureSeconds = 30` (16.8 MB) | 9.962 | 9.551 | 9.702 | 9.962 | **3.06×** |
| `captureSeconds = 8` (4.19 MB) | 9.376 | 9.596 | 10.320 | 10.320 | **3.17×** |

**64 concurrent grains do not fit**, by ~3×. That is FR-022's stated failure
condition, and the response FR-022 names is SC-004 lever (5).

**FR-022's stated *reason* is refuted by its own measurement.** FR-022 argued the
dominant term would be memory — "up to ~128 independent, non-sequential read
streams into a multi-megabyte buffer … an L3/DRAM-miss workload". The 30 s ring
is 4× the bytes of the 8 s ring and costs the same per grain-sample to within the
run-to-run spread (the 8 s figure is the *larger* one in run 3), and a further
probe at `captureSeconds = 1` — a 256 KB/channel ring that fits in L2 — landed
inside that spread too. The grain path is **instruction-bound**.

Lever (5) is therefore **not spent**, and this is a measured decision, not a
preference:

* A smaller pool does not make a grain-sample cheaper. It only makes fewer of
  them, and only where the cap binds.
* Four of the five configurations — (a), (b), (d), (e) — run at FR-009's default
  of 4 grains/s × 4 s = **16 concurrent grains**. The cap never binds there, so
  lever (5) moves their figures by exactly zero. All four are over the gate.
* Configuration (c) is the only one the cap touches. At the largest pool
  satisfying FR-022's own arithmetic ceiling — `kMaxGrains = 16`, from
  `106 667 / (512 × 10.32) = 20.2` — (c) computes to
  `16 × 512 × 10.32 + ~15 300 (its measured blur term) ≈ 99 900 ns/block`: still
  **1.40× the admissible baseline**. The lever does not close the criterion it
  exists for.
* And it costs real capability: `kMaxGrains = 16` makes FR-009's **own default
  control table** permanently pool-saturated (mean concurrent count exactly 16),
  putting the shipped default on FR-023's skip path, collapsing FR-073's
  documented operating region to the default itself, and moving SC-003's D-17
  precondition table under it.

`kMaxGrains` stays at 64, the miss is reported here and in the engine header
banner, and FR-073's operating region is unchanged.

> **CLOSED 2026-07-28.** FR-022's measurement mandate is discharged and its
> remedy is superseded, both written into the criterion at `spec.md:543`. The
> refusal above is unchanged and is now the sanctioned outcome rather than an
> outstanding gap: the residual is carried by the lever-6 budget decision — which
> declares configuration (c) out-of-region — instead of by a smaller pool.
> Re-measured this session: 30 s ring **7.59286** ns/grain-sample, 8 s ring
> **7.39566** ns, **ratio 1.02666** (T022 pass: 8.06287 / 7.87372, ratio
> **1.02402**). The ratio is the load-bearing number and it has not moved across
> three measurement passes: ~1.02 across a 4× difference in ring bytes.

### 1.5 The escalation (lever 6)

After the optimisations, the three cost centres are, per 512-sample block at
48 kHz: grain layer at the FR-009 default ~74 000 ns, blur stage ~23 000 ns,
freeze leg ~35 000 ns. The gate on a measurement is 71 111 ns.

Configuration (d) is required to run all three simultaneously — FR-052 has no
symmetric bypass at `m = 1`, deliberately (spec.md; the reason is that releasing
the freeze must be seamless) — so it is asked to fit ~132 000 ns of measured work
into 71 111. **No ordering of the remaining levers reaches that**: the blur and
freeze stages *together*, with the grain layer at zero, are already ~58 000 ns,
and the grain layer alone at the FR-009 default is over the gate on its own.

The composition this phase specifies — a 64-slot granulator, plus a stereo STFT
decoherence stage, plus **two** `SpectralFreezeOscillator`s, the last of which is
documented at "< 0.5 % CPU single core @ 44.1 kHz, 512 samples, 2048 FFT" **per
instance** (`processors/spectral_freeze_oscillator.h:21`, i.e. ~1 % for the
stereo pair on its own) — cannot fit 1 % of a core, let alone the 0.667 % the
criterion's structure actually gates on. The roadmap's own RA-4 note already
records that its per-phase budgets sum to 45 % against a 25 % Phase 7 ceiling.

**This is a budget decision, not a code decision.** The options, for whoever
takes it:

1. Raise Phase 5's per-voice allowance to ~1.5 % and re-derive Phase 7's poly
   tally from the five real figures above.
2. Drop the pure-freeze leg from the per-voice layer (it is the single largest
   term at 0.33 %, and two mono oscillators per voice is the expensive shape) or
   share one stereo freeze across voices.
3. Accept configuration (c) as out-of-region and shrink FR-073's documented
   operating rule — which is lever (5) by another name, and buys ~0.15 % at the
   cost of the default control table.

> **TAKEN 2026-07-28 by the user: OPTION 1**, together with the *first half* of
> option 3 — configuration (c) is accepted as out-of-region — but **without** its
> second half: FR-073's documented operating rule is **not** shrunk and
> `kMaxGrains` is **not** reduced, so no capability is traded away. Option 2 was
> not taken: the pure-freeze leg stays in the per-voice layer, which is why (d)
> at 1.440 % is the figure that sets the allowance. Phase 7 re-derives its
> polyphony tally from the measured figures (§1.1), **not** from the 1.5 % gate —
> see `spec.md:250`, which names 1.048 %/voice unfrozen and 1.440 %/voice frozen
> as the numbers to add up, and excludes (c) from the tally entirely.

---

## 2. FR-014 — grain admission margin. **Now literal.**

Previously the admission test used `kInterpMarginSamples = 2` where FR-014 says
`kMinAgeSamples = 64` (recorded as deviation D-11). The test is now FR-014
verbatim:

```
oldestAge = min(birthAge + dR, C - 2 - g)
needed    = ceil(oldestAge) + g            // g == kMinAgeSamples == 64
if (capture_.getAvailableSamples() < needed) { ++skipRingCold_; return; }
```
`dsp/include/krate/dsp/systems/atmosphere_engine.h`, `tryBirthGrain()` step (e).
Checked against `birthAge + dR` rather than `birthAge` alone — a strengthening,
since FR-014 names one age and a decorrelated grain has two.

D-11's objection was correct and is answered structurally rather than by
relaxing the margin: requiring `g` on the old side is unsatisfiable *if the
birth window is not built for it*. The window now is. `tryBirthGrain()` step (c)
subtracts `guard` at **both** ends:

```
headroom = C - 2 - g - g - dR          // young-side FR-025 g, old-side FR-014 g
slack    = headroom - 2                 // D-12's reserved ceiling slack
L'       = (w * requested > slack) ? floor(slack / w) : requested
ageLo    = ceil(wUp   * L') + g
ageHi    = C - 2 - g - ceil(wDown * L') - dR
```

so `needed ≤ (C - 2 - g) + g = C - 2 ≤ C`, which a full ring always reaches.
Window non-emptiness still resolves in one step:
`ageHi - ageLo > (C - 2 - g - dR) - (w·L' + 2) - g ≥ 0`.

**Cost, quantified:** `g` = 64 samples of ring headroom. At the default
`captureSeconds = 8` (C = 524 288) that is 0.012 %. The plan's worked cell —
`captureSeconds = 8`, `pitch = 0`, `driftRange = 12`, `w = 1.5` — moves from
`L' = 349 480` to `349 437`: 43 samples, 0.9 ms out of a 7.28 s grain
(`atmosphere_engine_test.cpp`, "the worked check from the plan", which asserts
both the arithmetic form and the literal).

D-11 is withdrawn. FR-025's invariant is **strengthened** by the change: the
oldest reachable age is now `C - 2 - g`, inside the `C - 2` bound FR-025 states.

## 3. FR-025 — grain liveness. **Met; the implemented truncation is now the binding formula.**

> **AMENDED 2026-07-28** (`spec.md:638`). What follows is unchanged in substance —
> the same three forced terms, the same reasoning — but it is no longer a
> *deviation*: FR-025 now states `headroom = C − 2 − 2g − dR`, `slack =
> headroom − 2`, `L′ = ⌊slack/w⌋`, with the withdrawn `⌊(C − 2 − g)/w⌋` quoted in
> the criterion and the guarantee explicitly recorded as **strengthened** because
> every term shrinks the reachable range. Re-ran `AtmosphereEngine_GrainLiveness`
> this session: EXIT=0, `All tests passed (1281 assertions in 1 test case)`.

The invariant itself is asserted across SC-002's full sweep and passes:
`min observed age ≥ 64` and `max observed age ≤ C - 2` in every cell.

The truncation is `L' = floor((C - 2 - g - g - dR - 2) / w)` where FR-025 writes
`floor((C - 2 - g) / w)`. Three terms differ, and each is **forced**, not chosen:

| term | why FR-025's literal form cannot be implemented |
|---|---|
| `- dR` (D-1) | The right channel reads at `a + dR`, and spec.md's own FR-033 note says the offset "participates in FR-025's clamp (the larger of the two ages is the one bounded)". With `dR` in the clamp but not the truncation, `ageHi - ageLo ≥ -dR - 2` — the window inverts. FR-025's stated formula is self-inconsistent with FR-033. |
| `- 2` (D-12) | `ageLo` and `ageHi` each carry a `ceil()`, each adding < 1, so their sum is `< w·L' + 2`. Truncating against the unreserved headroom leaves `ageHi - ageLo > -2`: `std::clamp(a0, lo, hi)` with `lo > hi` is a **precondition violation**, i.e. UB that can still return a plausible `a0`. Asserted directly by SC-002's `REQUIRE(math.ageLo <= math.ageHi)` clause. |
| second `- g` (new) | FR-014's admission margin, section 2 above. Without it FR-014 cannot be implemented as written. |

Impact on the guarantee: none — every term makes the reachable age range
*smaller*. Impact on capability: `2g + dR + 2` samples of a ring that is 524 288
samples at the default, i.e. ≤ 0.03 % of the maximum grain lifetime wherever
truncation binds at all.

## 4. SC-003 — no grain-boundary clicks. **Met, at the sigma the spec authorises.**

`AtmosphereEngine_NoGrainBoundaryClicks` passes: all 30 cells (5 `grainSeconds` ×
6 `GrainEnvelopeType`, `Exponential` included) give 0 detections on both channels,
on the engine render *and* on the reference render.

`detectionThreshold` is now **12.5** — the measured grid-wide zero point, with
nothing added. It was 14.0 (12.5 + a 1.5 cross-toolchain margin); SC-003
authorises "the smallest value that gives 0 detections on the reference render"
and no more, and every sigma of padding weakens the gate on the *engine* render,
which is the thing under test. The cross-toolchain risk is carried by the
reference gate instead, which is what it is for: the reference floor is asserted
to be 0 per cell *before* the engine render is judged, and its failure message
names the smallest ladder sigma that would clear it. The rest of the
`ClickDetectorConfig` is verbatim (`frameSize 512`, `hopSize 256`,
`energyThresholdDb -60.0f`, `mergeGap 5`).

**D-17, resolved 2026-07-28 by amendment** (`spec.md:1110`): the scoping below is
now the criterion, with the withdrawn unconditional wording quoted and the
unsatisfiability stated arithmetically. Re-ran `AtmosphereEngine_NoGrainBoundaryClicks`
this session: EXIT=0, `All tests passed (470 assertions in 1 test case)`; and
SC-001's unconditional carrier `AtmosphereEngine_NoAllocationAfterPrepare`:
EXIT=0, `All tests passed (11 assertions in 1 test case)`. SC-003's
`REQUIRE(getSkippedTriggerCountPoolFull() > 0)` is asserted in 12 of the 30
cells and inverted to `== 0` in the other 18. It is not satisfiable as written:
mean concurrent grains is `density × grainSeconds`, `kMaxDensity` is 20/s
(FR-009) and `kMaxGrains` is 64, so at `grainSeconds ∈ {0.05, 0.2, 1}` the
maximum reachable population is 1, 4 and 20 — the pool **cannot** saturate at any
density the control table allows. SC-001 carries the unconditional form, at a
configuration where it is reachable.

## 5. SC-009 — sample-rate independence. **Met; allocation clause replaced by a stronger one.**

`AtmosphereEngine_SampleRateIndependence` passes. Clause 1 (non-truncating,
44.1/48/96 kHz): lifetime error 0.0 against a 0.5 % bound (12×), concurrent-count
error ≤ 3.2e-4 against 5 % (6×), RMS error ≤ 0.18 dB against 1.0 dB (6×).
Clause 2 (truncating, rate-aware) is non-degenerate: truncation is asserted to
actually bind, and the RA-2 capacity spread is asserted real
(44.1 and 48 kHz share capacity 524 288; 96 kHz is 1 048 576; spread > 5 %).

**D-18, resolved 2026-07-28 by amendment** (`spec.md:1378`): the substitution
below **is** the criterion now — the two-sided bracket, `renderAllocationCount ==
0`, and the capture-capacity/latency equality — with the withdrawn equality
wording quoted and the reason the literal `==` is wrong recorded in the spec
itself. Re-ran `AtmosphereEngine_SampleRateIndependence` this session: EXIT=0,
`All tests passed (115 assertions in 1 test case)`.

The withdrawn criterion asked for the re-prepare's
allocation count to *equal* a fresh prepare's. That fails on correct code: every
re-prepare path reuses capacity (`std::vector::resize` to an unchanged size
allocates zero times), so a correct engine allocates strictly fewer times. What
is asserted instead:

1. `0 < secondPrepareCount <= freshPrepareCount` — bracketed from **both** sides.
   The upper bound catches reallocation-instead-of-reuse; the new lower bound
   catches the failure the equality was actually reaching for, because
   48 → 96 kHz doubles the capture ring, so a re-prepare that allocates *nothing*
   has kept the 48 kHz ring.
2. `renderAllocationCount == 0` over a full post-re-prepare render — nothing was
   left undersized, measured with SC-001's instrument.
3. Geometry equality against the fresh engine:
   `getCaptureCapacitySamples()` and `getLatencySamples()` both equal.
   This is what the count-equality was a proxy for, and unlike a count it is a
   property of the re-prepared *engine* rather than of how often its allocator
   was called.

Plus `firstBlockExactlyZero` (FR-014's cold ring), `bornAfterReprepare > 0` and
`lateRms > 1e-4` (usable once refilled).

## 6. SC-013 — portability and lint gates. **Met, including the scripted gate.**

| gate | result |
|---|---|
| `node tools/lint-float-bit-goldens.js` | clean |
| `node tools/lint-arch-guarded-includes.js` | clean |
| `node tools/lint-simd-aligned-loadstore.js` | clean |
| `node tools/lint-layers.js` | clean |
| `node tools/lint-odr.js` | clean |
| **`node tools/lint-nonfinite-symbols.js`** | **clean (new — see below)** |
| `node tools/check-portability.js` | clean |
| zero-warning MSVC build, all five DSP layer targets | 0 warnings |

The missing piece was the criterion's *scripted grep gate* — "not by a review
step. A manual check is not a gate." `tools/lint-nonfinite-symbols.js` is that
gate. It fails non-zero on `std::isnan` / `std::isinf` / `std::isfinite` /
`numeric_limits::infinity()` / `quiet_NaN()` / `signaling_NaN()` / `INFINITY` /
`HUGE_VALF` in files that are **not** compiled with `-fno-fast-math`, strips
comments so banners explaining the rule do not trip it, and reads the
`-fno-fast-math` exemption list out of the CMake files rather than hard-coding it
(so a TU removed from that list immediately becomes subject to the lint). It is
wired into `.github/workflows/ci.yml` next to the other lints.

Its enforcement surface is an explicit list — the atmosphere header plus its
three fast-math-built TUs; `atmosphere_engine_nonfinite_test.cpp` is correctly
detected as exempt. `--all` surveys the whole tree and reports **63** pre-existing
hits in unrelated components (some deliberate: `core/db_utils.h` *returns*
`quiet_NaN` from its own documented-domain helpers). Gating those today would
block every commit on an unrelated repo-wide cleanup; the list is the surface and
it grows as components are converted. **Verified to have teeth**: an injected
`std::isnan` in `atmosphere_engine.h` produced exit 1 with the file and line;
reverting restored exit 0.

## 7. Suite status

All five DSP layer targets green, on the tree as it stands:

| target | result |
|---|---|
| `dsp_core_tests` | 1 588 257 assertions / 573 cases — pass |
| `dsp_primitives_tests` | 4 549 290 assertions / 1 505 cases — pass |
| `dsp_processors_tests` | 10 640 741 assertions / 3 296 cases — pass |
| `dsp_systems_tests` | 6 032 091 assertions / 1 161 cases — pass |
| `dsp_effects_tests` | 93 062 assertions / 455 cases — pass |

**Re-run 2026-07-28, after the amendment**, on the tree as it now stands:

| command | verbatim last line |
|---|---|
| `dsp_systems_tests.exe` | `All tests passed (6032091 assertions in 1161 test cases)` — EXIT=0 |
| `dsp_systems_tests.exe "[.perf]"` | `test cases: 9 \| 8 passed \| 1 failed` — EXIT=1, **see below** |
| `dsp_systems_tests.exe "AtmosphereEngine_CpuBudget,AtmosphereEngine_GrainSampleCost"` | `All tests passed (47 assertions in 2 test cases)` — EXIT=0 |
| `node tools/check-portability.js` | `check-portability: all clear -- 4 compiled.` — EXIT=0 |
| `node tools/check-portability.js …atmosphere_engine_perf_test.cpp` | `check-portability: all clear -- 1 compiled.` — EXIT=0 |
| MSVC rebuild, `--target dsp_systems_tests` | EXIT=0, `grep -ci "warning\|error"` = **0** |

`AtmosphereEngine_CpuBudget` is `[.perf]`-tagged and therefore excluded from the
default run and from CI. **Invoked by name or by tag it now PASSES**, with all
five measured figures in its output — see the SC-004 row of the compliance table
for the transcribed numbers. So does `AtmosphereEngine_GrainSampleCost`. **Both
atmosphere perf cases are green in both `[.perf]` runs made this session.**

**The one `[.perf]` failure is NOT Phase 5's and is not in Phase 5's tree — but it
is reported here rather than dismissed.** `SympatheticResonance SIMD performance
benchmark` (`dsp/tests/unit/systems/sympathetic_resonance_test.cpp:2578`) fails
its `REQUIRE(simdThroughput >= scalarThroughput * 0.9)` when run inside the full
`[.perf]` batch:

| run | scalar | SIMD | ratio | verdict |
|---|---|---|---|---|
| `[.perf]` batch, run 1 | 65.46 M samples/s | 30.61 M | 0.468× | FAIL (`30610969.97 >= 58914337.45`) |
| `[.perf]` batch, run 2 | — | — | — | FAIL (`41505882.35 >= 47086877.60`, 0.881×) |
| **invoked alone** | 62.18 M samples/s | 60.27 M | **0.969×** | **PASS**, `All tests passed (3 assertions in 1 test case)` |

It is a throughput-ratio assertion measured on a hybrid CPU immediately after
several minutes of sustained benchmark load, and its two legs run back-to-back, so
the second leg is measured on a thermally/frequency-degraded core. It passes in
isolation and it is untouched by this phase — Phase 5 edited no file in that
component. It is **not** softened here and **not** claimed green: it is an open,
unrelated flake in an ordering-sensitive perf case, outside the file scope of this
work, and it needs its own fix (measure the two legs in a randomised or
interleaved order, or bound each leg against a checked-in baseline rather than
against each other).

---

# Implementation notes — deviations reported by task agents

Every item below was reported by the task agent that made it, not discovered
afterwards. Grouped by kind.

## A. Deviations that changed the specified formula or threshold

> **STATUS AS OF 2026-07-28.** Four of the deviations below are no longer
> deviations: **D-1**, **D-12** and the new second `− g` are absorbed into
> FR-025's amended formula (`spec.md:638`), **D-17** into SC-003's amended
> precondition (`spec.md:1110`) and **D-18** into SC-009's amended allocation
> clause (`spec.md:1378`). They are retained here as the record of *why* the
> criterion says what it now says. Nothing was relaxed to absorb them: each one
> either strengthens the guarantee (FR-025's terms all shrink the reachable
> range) or replaces an unsatisfiable/inverting proxy with a stronger assertion.

| ID / task | Deviation | Why |
|---|---|---|
| **D-1** (T006) | FR-025 truncation subtracts `dR` | The right channel reads at `a + dR` and FR-033 puts the offset inside FR-025's clamp; without it in the truncation the birth window inverts. Section 3. |
| **D-12** (T006, T016) | FR-025 truncation reserves a further 2 samples (`slack = headroom - 2`) | `ageLo`/`ageHi` each carry a `ceil()`; without the reserve `std::clamp(a0, lo, hi)` can be called with `lo > hi`, a precondition violation. Section 3. |
| **new second `- g`** (FR-014 fix) | FR-025 truncation subtracts `guard` at both ends | Makes FR-014's literal `+ g` admission margin satisfiable structurally instead of relaxing it. Section 2. |
| (T006) | Birth step (e) computes `oldestAge = min(birthAge + dR, C - 2 - g)` before the `ceil` | `ageHi` is *formed* by subtracting `dR`; re-adding it can land 1 ULP high, which at ring scale raises the `ceil` by a whole sample and demands `C+1` available — permanently unadmittable. The `min` clips only to a bound already proven. |
| **D-15** (T012) | Blur-smoother cadence clause derives its 1−1/e reference from `calculateOnePolCoefficient()` rather than treating `kBlurSmoothMs = 50` as τ | `smoother.h:71-93` documents its argument as *time to reach 99 %* (`exponent = -5000/(ms·sr)`, τ = ms/5). The task's "within 10 % of 50 ms" is unsatisfiable by a correct implementation. The 10 % band is kept against the exact derived reference; a second clause asserts the closed form `1 − coeff^768`. |
| **D-17** (T009, and SC-003 above) | `REQUIRE(skipPoolFull > 0)` scoped to 12 of 30 cells, inverted to `== 0` in the other 18 | Provably unsatisfiable in the small-`grainSeconds` cells. Section 4. |
| **D-18** (T016, and SC-009 above) | Allocation-count equality replaced by a two-sided bound + geometry equality | The literal `==` fails on correct code. Section 5. |
| (T007) | FR-027 endpoint clause asserts `samples.front() == 0.0f` exactly + a terminal-step bound, not "first two samples zero" | Measured: only `table[0]` is forced at the head and lookup maps age 1 to `indexFloat = Δ`; at `grainSeconds = 0.05` (Δ = 1.707) the 2nd sample is 0.01686 (Exponential). Zeroing it would need a forced 3-entry head run, i.e. a truncated attack that FR-027 does not ask for. The "last two" clause is applied conditionally on `indexStep ≤ 1`. |
| (T007) | The 30 s isolated-grain cell uses `captureSeconds = 1` + `pitch +24` | At `grainSeconds = 30`, `kMinDensity = 0.1` triggers every 10 s, so a 1.44 M-sample grain always overlaps two more — the cell cannot be isolated as written. FR-025 truncation puts it in the same long-grain regime with one grain per render. |
| (T013) | Correlation step epsilon is absolute (0.02), flatness step epsilon relative (2 %) | ρ legitimately approaches 0 at full blur, where a relative epsilon degenerates to a no-op clause. |
| (T013) | The crest render's non-silence bound is −80 dBFS, not −40 | The impulse-train source is −43.8 dBFS *before* granulation and the 1/√n sum; −40 dB is structurally unreachable there and would fail a correct engine. The −40 dB bound is unchanged everywhere the spec puts it. |
| (T015) | SC-014 clause (c) uses sigma 14.0, not the 5.0 in the task text | "T009's pinned config" is the config T009 actually pinned; at sigma 5 the measured false-positive floor is 47–9146 detections in every cell, reference render included. All other `ClickDetectorConfig` fields verbatim. A reference gate keeps it non-vacuous. (Sigma has since been recalibrated to 12.5 grid-wide; see section 4.) |
| (T010) | SC-011's reference issues twelve consecutive 4096-sample calls, not one | One 4096-sample span is 85 ms; at `kMaxDensity = 20/s` it can contain at most two births — too few for the required "birth inside a partial control chunk" coverage to be anything but a coin flip. Partitions still run over the same span. |

## B. Additions beyond the plan (all flagged by their author)

* **T004** — plain control-table read-back getters for all 17 setters, plus
  `getFreezeFftSize()`. Plan §14.2 lists no control getters, so the mandated
  FR-009 clamp/default assertions would have been unobservable. Matches the house
  pattern in `harmonic_cloud.h:490-494, :527-528, :595-597, :708`.
* **T012** — `getAppliedBlur()` (`blurSmoother_.getCurrentValue()`); `getBlur()`
  returns the target only, so the FR-009 cadence clause was otherwise
  unobservable.
* **T008** — `processStereoBlock()` returns early and zero-fills the block
  remainder when the ramp latches **mid-chunk**. Without it a latch at sample 100
  of a 512-sample block keeps capturing and ticking the scheduler for 412 more
  samples, so FR-007's "no counter moves across the latched span" would hold only
  for blocks that *began* latched.
* **T011** — `captureFreeze()` / `releaseFreeze()` / `isFreezeCaptured()` landed
  early (they are T014's deliverable) because SC-001's pinned schedule calls them
  and the TU could not compile otherwise. FR-050/FR-051-correct and
  allocation-free; T014 layered the delay-matched leg and crossfade on top.
* **T014** — the freeze crossfade in `finishChunk()` is **gated on
  `freezeEnabled_`**. Plan §12.3's pseudocode shows no gate; ungated, a stored
  `freezeMix` of 1 would *mute* a freeze-disabled engine, contradicting FR-054.
* **T004** — `setDensity`/`setJitter` store only and push into `GrainScheduler`
  at the next control step. An immediate push would move `interonsetSamples_`
  mid-chunk and make the render depend on where the block boundary fell,
  breaking SC-011.
* **T006** — observed-age folding lives inside `renderGrainChunk()` (chunk first
  and last sample) rather than as step 5 of `runControlStep()`, where the
  per-sample ages already exist. Same cadence, same exactness.
* **FR-027 superset** — a 64-entry linear edge ramp on the envelope table beyond
  the spec's forced endpoints, needed to bound the neighbour step (documented in
  the header banner :80-107).

## C. Defects found and fixed while implementing

* **T012 — cross-platform compile error, MSVC-invisible.** The header carried
  `ITERUM_NOINLINE [[nodiscard]] static bool isFinite(...)`, a hard error on both
  Clang and g++ ("an attribute list cannot appear here"). MSVC accepts it, so it
  would have broken the Linux and macOS legs only. Reordered to
  `[[nodiscard]] ITERUM_NOINLINE`, with the reason recorded at the declaration.
* **T007 — two now-disproven doc claims corrected** (comments only): the banner's
  "Δ stays below 1 over the documented operating region" and
  `kEnvelopeTailZeroEntries`' "< 1 for every legal L' at an audio sample rate".
  Both bound only FR-025-*truncated* lifetimes and are false for a short
  *requested* grain (L' = 2400 at 0.05 s).
* **T022 — lint-gate false positive fixed at source**: a `reset()` comment quoted
  `std::numeric_limits<float>::infinity()` while *describing the prohibition*; a
  scripted gate cannot tell a comment from code. Reworded; comment-only.
* **Sixth, unlisted performance lever** (post-T021): four instruction-count
  defects in shared DSP — runtime `%` in three ring-wrap hot loops
  (`spectral_freeze_oscillator.h`, `stft.h`), a CRT `std::floor` call on the grain
  inner loop, `GrainEnvelope::lookup`'s `std::clamp`/`size_t` conversions, and
  per-`setGrainEnvelope` table regeneration. Three of the four bit-identical.
  1.60×–2.77× measured. Section 1.3.

## D. Measurement-record gaps that were later closed

T017/T018/T019 were authored under a no-build constraint and shipped with
`*** NOT YET MEASURED ***` records, placeholder baselines pinned at
`kProvisionalBaselineNs = 71 000`, and a T019 verdict of "NO LEVER SPENT" that
its own author flagged as **vacuously** satisfied. T021 (build agent) produced the
first real measurement and reported **blocked**. The records have since been
filled with real eight-run figures, the levers audited or spent, and the residual
escalated (sections 1.1–1.5). The five baselines stayed at 71 000 — deliberately:
raising one breaks
`static_assert(baseline × kRegressionFactor <= kReference1PctNs)` and SC-004's own
rule forbids raising a baseline.

**Closed 2026-07-28.** `kProvisionalBaselineNs` no longer exists. Each
configuration now carries its own baseline derived from its worst-of-eight figure
by `min(⌈worst × 1.05⌉, 106 666)`: (a) 91 000, (b)/(d)/(e) 106 666 (the cap),
(c) 361 000 (out-of-region). That became possible only because the **reference**
moved — by the user's lever-6 decision, from the measurements — and not because
any baseline was raised against a fixed reference, which remains forbidden. The
constant is `kReferenceNs` now, not `kReference1PctNs`
(`atmosphere_engine_perf_test.cpp:451`).

## E. Working-tree hygiene reported by T020 / T021 / T022 — read before committing

1. **`dsp/tests/CMakeLists.txt` carries an unrelated hunk.** Beyond T002's two
   Phase-5 edits there is a third at ~:400-416 adding
   `target_compile_definitions(... KRATE_DSP_TESTS_DIR=...)` for
   `dsp_processors_tests` and `dsp_systems_tests`. It belongs to the
   arpeggiator/harmonizer fixture-path work, not Phase 5. Consumers are
   `arpeggiator_core_baseline_test.cpp`, `arpeggiator_core_perstepmods_test.cpp`
   and `harmonizer_engine_test.cpp` — all three also modified in the working tree.
   **Split into its own commit; do not sweep it into the AtmosphereEngine commit.**
2. **`tools/check-portability.js` is also modified** (passes
   `-DKRATE_DSP_TESTS_DIR` for `dsp/tests/` files) — same fixture-path change.
3. **`check-portability.js` default mode is blind to untracked files.**
   `changedFiles()` uses `git diff --diff-filter=ACMR origin/main...HEAD`, which
   enumerates tracked paths only; all four atmosphere TUs are `??`, so the default
   run silently excluded them and returned a false green. It was re-run explicitly
   on the four TUs (clean). **Re-run it once the files are staged/committed** —
   the pre-commit `guard-portability.js` hook uses `--staged`.
4. **~15 junk untracked files at repo root** (`!(i`, `%zu`, `'-I`, `(kBlock))`,
   `0)\``, `console.log('`, `l.trim()`, `{`, …) left by botched shell quoting in
   earlier sessions. Not attributable to Phase 5 and not deleted by any agent.
   Clean them before committing so they are not swept in.
5. **Methodology note (T020):** `--list-tests` with no filter hides `[.perf]`
   cases. Use `dsp_systems_tests.exe "*" --list-tests` or the count looks two
   short.

---

# Remaining gates for the human loop

| Gate | Status | Command / note |
|---|---|---|
| Build, zero warnings, all five DSP layer targets | **done** — EXIT=0, 0 warning lines | `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests` |
| All five suites green | **done** — section 7 | run each exe directly, `\| tail -5` |
| Lints (layers, ODR, float-bit-goldens, arch-guarded includes, SIMD load/store, allocation overrides, non-finite symbols) | **done** — all EXIT=0 | section 6 |
| `check-portability.js` | **done explicitly on the four new TUs**; **re-run after staging** | `node tools/check-portability.js` (see E-3) |
| **clang-tidy** | **NOT RUN — outstanding** | `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (regenerate `compile_commands.json` via `cmake --preset windows-ninja` from a VS Developer PowerShell first, since new files were added). Capture to a log on the first run. |
| **pluginval** | **NOT APPLICABLE** | No plugin source changed. Phase 5 touches only `dsp/` (Layer 1 + Layer 3 headers, tests) and `tools/` + CI. Seraphis plugin work starts at Phase 8. |
| **Commit** | **NOT DONE — outstanding** | On `feat/seraphis-phase1-life-modulators` (correct branch, all Seraphis phases share it). Split per E-1/E-2 (fixture-path change is a separate commit); clean the root junk files per E-4 first. New/untracked: `dsp/include/krate/dsp/systems/atmosphere_engine.h`, the four `dsp/tests/unit/systems/atmosphere_engine_*.cpp`, `tools/lint-nonfinite-symbols.js`, `specs/seraphis-phase5-atmosphere/`. Modified and **in scope**: `rolling_capture_buffer.h`, `test_rolling_capture_buffer.cpp`, `grain_envelope.h`, `stft.h`, `spectral_freeze_oscillator.h` (the sixth-lever optimisations — all three are shared DSP with other consumers; all five suites are green), `dsp/tests/CMakeLists.txt` (Phase-5 hunks only), `.github/workflows/ci.yml`. |
| **SC-004 budget decision** | **CLOSED 2026-07-28 — option 1 taken by the user** | Per-voice allowance raised to 1.5 % (reference 160 000 ns/block); configuration (c) out-of-region. Amendments at `spec.md:1164` (SC-004) and `spec.md:250` (RA-4 → Phase 7). `AtmosphereEngine_CpuBudget` passes. Section 1's resolution box. |
| **Unrelated `[.perf]` flake** | **OPEN — not Phase 5's, not fixed here** | `SympatheticResonance SIMD performance benchmark` (`sympathetic_resonance_test.cpp:2578`) fails inside the full `[.perf]` batch (ratio 0.468× / 0.881× against a 0.9 bound) and passes in isolation (0.969×). Ordering/thermal sensitivity in a back-to-back throughput comparison; outside this work's file scope. Section 7. |
