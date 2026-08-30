# Tasks: Seraphis Phase 1 — Life Modulator Suite

**Spec:** `specs/seraphis-phase1-life-modulators/spec.md`
**Plan:** `specs/seraphis-phase1-life-modulators/plan.md`
**Layer:** all six components are **Layer 2** (`dsp/include/krate/dsp/processors/`) — may include Layer 0
(`core/modulation_source.h`, `core/random.h`) and Layer 1 (`primitives/smoother.h`) and stdlib **only**;
no Layer 2/3/4 include.
**Test target:** `dsp_processors_tests` (`build/windows-x64-release/bin/Release/dsp_processors_tests.exe`).

## Conventions every task follows

- **Canonical order inside each task:** write the failing test FIRST → implement the header to make it pass →
  fix all compiler warnings (zero-warnings gate) → confirm the target's tests pass. The test suite is only
  buildable/runnable after the CMake registration task (T007) has been run once; until then, an executor
  building a single modulator locally must temporarily add just its own test-file line to the source list to
  iterate, and **revert that local edit** so T007 remains the single authoritative CMake change. Do not leave
  a modulator task's edit in `dsp/tests/CMakeLists.txt`.
- **Header path:** `dsp/include/krate/dsp/processors/<name>.h`. **Test path:** `dsp/tests/unit/processors/<name>_test.cpp`.
- **Pattern reference (read before writing any modulator):** `dsp/include/krate/dsp/processors/random_source.h`
  (lifecycle: `prepare(double)` :45, `reset()` :53, control-rate `processBlock(size_t)` :66, per-sample
  `process()` :87, `getCurrentValue() override` :114, `getSourceRange()→{-1,1}` :118; owns
  `Xorshift32 rng_` + `OnePoleSmoother`; discards the `[[nodiscard]]` smoother return via
  `static_cast<void>(outputSmoother_.process())` :110). Test file pattern:
  `dsp/tests/unit/processors/random_source_test.cpp:9-18` (`#include` its header + `<catch2/catch_test_macros.hpp>`
  + `<catch2/catch_approx.hpp>`; `using namespace Krate::DSP;`).
- **Reused signatures (verified — quote, never invent):**
  - `ModulationSource` (L0) `core/modulation_source.h:31` — the ABC. **Only two virtuals:**
    `[[nodiscard]] virtual float getCurrentValue() const noexcept` (:37),
    `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept` (:41). `setSeed`, `prepare`,
    `reset`, `process`, `processBlock` are **plain non-virtual** members — writing `override` on them fails to
    compile.
  - `Xorshift32` (L0) `core/random.h:40` — `explicit constexpr Xorshift32(uint32_t seedValue=1) noexcept` (:44,
    substitutes a default when seed==0); `nextFloat()→[-1,1]` (:58); `nextUnipolar()→[0,1]` (:66);
    `seed(uint32_t)` (:72); `state()` (:78).
  - `OnePoleSmoother` (L1) `primitives/smoother.h:134` — `configure(float smoothTimeMs,float sampleRate)` (:160);
    `setTarget(float)` (NaN→0, Inf→±1e10 sanitize, :170); `getCurrentValue()` (:191); `[[nodiscard]] float process()`
    (:197); `advanceSamples(size_t)` O(1) closed form (:243); `snapTo(float)` (:263); `reset()` (:275). Coeff
    `= exp(-5000/(smoothTimeMs·sr))` (:90-92).
  - `SlewLimiter` (L1) `primitives/smoother.h:468` — alternative bounded-slew primitive (not required if the
    output smoother is used).
- **Cross-cutting invariants (defects if violated):** every method `noexcept`; no heap alloc / locks / exceptions /
  I/O; all storage fixed-size members; **no `std::isnan`** anywhere (`-ffast-math` rule — rely on
  `OnePoleSmoother::setTarget` sanitize + hard clamps + bounded recurrences); `double` phase accumulators wrapped
  `mod 2π`; no bit-exact float goldens (determinism tests use same-build exact equality, which is legal;
  statistical checks use measured tolerances). SC-002 threshold is **≤ 2.0e-3 absolute** (= 1e-3 × source-range
  span 2).
- **No commit tasks** — commits happen outside this workflow.

---

## GROUP 1 — `BrownianDrift` (foundation: establishes the shared lifecycle/decimation/seed pattern the others copy)

### T001 — `BrownianDrift` (Layer 2)

**Create (new, disjoint):**
- `dsp/include/krate/dsp/processors/brownian_drift.h`
- `dsp/tests/unit/processors/brownian_drift_test.cpp`

**Roadmap:** lines 111-112. **FRs:** FR-001..006, FR-011..014. **SCs:** SC-001..006. **ODR:** clear
(`grep -rn "class BrownianDrift\|struct BrownianDrift" dsp/ plugins/` → none; re-run before writing).

**Write the failing test first** — `brownian_drift_test.cpp`, `using namespace Krate::DSP;`, these `TEST_CASE`s:
- `TEST_CASE("BrownianDrift_NeverExceedsRange")` (SC-001, FR-013) — grid over extremes {smoothness 0 and 1,
  depth 0 and 1, mean −1/0/+1}; `prepare(48000)`, render ≥60 s stepping `processBlock(512)` and reading
  `getCurrentValue()` per block; assert every value ∈ `getSourceRange()` = `{-1.0f,+1.0f}`, **0 violations**.
- `TEST_CASE("BrownianDrift_MaxSlewBounded")` (SC-002) — worst case = **minimum smoothness (0), max depth (1)**;
  render ≥5 s per-sample via `process()`; assert `max|out[n]-out[n-1]| ≤ 2.0e-3`.
- `TEST_CASE("BrownianDrift_AutocorrTimeTracksSmoothness")` (SC-003a) — at smoothness {0.1, 0.5, 0.9},
  render ≥10× the longest τ, **mean-detrend**, compute normalized autocorrelation, find first 1/e-crossing lag;
  assert the crossing lag is **strictly increasing** across the three settings, is `≫ 1 sample`, and shows no
  single dominant secondary autocorrelation peak (distinct from an LFO).
- `TEST_CASE("BrownianDrift_SeededDeterminism")` (SC-004) — two instances, `setSeed(1234)`, identical
  `prepare(48000)`+`processBlock` sequence → **exact** float equality of the `getCurrentValue()` stream;
  `setSeed(1234)` vs `setSeed(9999)` → sequences differ.
- `TEST_CASE("BrownianDrift_ResetRewindsToSeed")` (SC-004/OQ3) — render N blocks, capture stream; `reset()`;
  render N blocks again → **exact** equality with the captured stream.
- `TEST_CASE("BrownianDrift_SampleRateInvariant")` (SC-005 option b) — render the same wall-clock duration at
  44100 and 96000 for **≥8 seeds**; compare **mean RMS and mean 1/e decorrelation-time across seeds**;
  tolerance = measured across-seed spread (start ±5%, widen to measured). Do **not** compare the mean relatively
  (source is ~zero-mean) — if the mean is compared at all use an absolute range-span tolerance.
- `TEST_CASE("BrownianDrift_NoAllocInProcess")` (SC-006) — global `operator new`/`delete` counting guard around a
  steady-state `processBlock(512)` loop after `prepare`; assert **0 allocations**.
- `TEST_CASE("BrownianDrift_SourceRangeIndependentOfDepth")` (FR-006) — assert `getSourceRange()` returns exactly
  `{-1.0f,+1.0f}` at depth 0, 0.5, 1.0 (does not shrink with depth).
- `TEST_CASE("BrownianDrift_OutputDefinedAfterPrepare")` (FR-004) — `prepare(48000)` then read `getCurrentValue()`
  with no intervening advance; assert the value is finite (bit-pattern check, **not** `std::isnan`) and inside
  `getSourceRange()`; repeat immediately after `reset()`.
- `TEST_CASE("BrownianDrift_RevertsToConfiguredMean")` (FR-011) — `setMean(0.4f)`, moderate smoothness, render
  ≫10·τ; assert the long-run sample average converges to `depth·0.4` within a measured tolerance; `setMean(0)`
  averages near 0. (A fixed-0 OU target fails this; SC-001 would not catch it.)
- `TEST_CASE("BrownianDrift_EdgeCases")` — `processBlock(0)` leaves state and `getCurrentValue()` unchanged;
  `setSeed(0)` is handled safely (Xorshift32 substitutes a default).

**Implement `brownian_drift.h`** — `class BrownianDrift : public ModulationSource`. Exact-discretisation OU
(AR(1)) at control rate:
- Constants: `kTauMin=0.2f`, `kTauMax=30.0f` (s), `kInternalStd=0.5f`, `kDriftOutputSmoothMs=150.0f`,
  `kControlRateInterval=32` (size_t).
- Recurrence per control step: `a = exp(-Δt/τ)`, `g = kInternalStd·sqrt(1−a²)`,
  `x = mean + a·(x−mean) + g·Z` where `Z` = Irwin–Hall sum of three `rng_.nextFloat()` (zero-mean,
  ~unit-variance). `Δt = controlDtSeconds_ = kControlRateInterval/sampleRate_`. `τ = lerp(kTauMin,kTauMax,smoothness)`.
- Flush `x` to 0 when `|x| < 1e-20f` (denormal guard).
- Output target each control step = `depth·x`; `getCurrentValue()` returns `std::clamp(outputSmoother_.getCurrentValue(), -1.0f, 1.0f)`.
- Setters (all `std::clamp`): `setSmoothness(float s01)` [0,1]→recompute `a,g`; `setDepth(float d01)` [0,1];
  `setMean(float m)` [-1,1]. `setSeed(uint32_t)` (plain non-virtual) stores `configuredSeed_` + `rng_.seed(seed)`.
- `prepare(double sr)`: store `sr`; `controlDtSeconds_`; recompute `a,g`; `outputSmoother_.configure(150,sr)`;
  `rng_.seed(configuredSeed_)`; `x=mean`; `outputSmoother_.snapTo(depth·x)`; counter=0.
- `reset()`: same re-init but keep `sampleRate_`.
- `process()`: decrement control counter; on boundary do one OU update + `outputSmoother_.setTarget(depth·x)`;
  always `static_cast<void>(outputSmoother_.process())`.
- `processBlock(size_t n)`: `ChaosModSource`-style loop (`chaos_mod_source.h:80-91`) — on each control boundary
  update OU + set target, then `outputSmoother_.advanceSamples(chunk)` for samples to the next boundary;
  `processBlock(0)` is a no-op.
- `getSourceRange()` returns `{-1.0f,+1.0f}`.

**SC-002 proof (record in a header comment):** `OnePoleSmoother` per-sample change ≤ `span·(1−coeff)`; at 150 ms,
48 kHz, `1−coeff ≈ 6.9e-4` → max delta `≤ 2·6.9e-4 ≈ 1.4e-3 < 2.0e-3`, independent of OU step size.

**Verify:** after T007 registers it, `dsp_processors_tests.exe "BrownianDrift_*"` — all cases pass, zero warnings.

---

## GROUP 2 — remaining five modulators (each is disjoint NEW files; all `[P]` — parallel-safe with one another)

> Every task in this group copies the T001 lifecycle/decimation/seed/output-smoother pattern. None edits a shared
> file. Run T001 first only so its pattern is settled; the five here have no ordering dependency among themselves.

### T002 [P] — `BreathingModulator` (Layer 2)

**Create:** `dsp/include/krate/dsp/processors/breathing_modulator.h`,
`dsp/tests/unit/processors/breathing_modulator_test.cpp`.
**Roadmap:** 113-114. **FRs:** FR-001..006, FR-021..024. **SCs:** SC-001,002,003b,004,005,006. **ODR:** clear
(`grep -rn "class BreathingModulator\|struct BreathingModulator" dsp/ plugins/` → none).

**Write the failing test first** — `TEST_CASE`s:
- `TEST_CASE("BreathingModulator_NeverExceedsRange")` (SC-001) — extremes {rate 0.01/0.5, depth 0/1,
  irregularity 0/1}; render horizon ≥3× longest period (at 0.01 Hz → ≥300 s); assert every `getCurrentValue()`
  ∈ `{-1,+1}`, 0 violations.
- `TEST_CASE("BreathingModulator_MaxSlewBounded")` (SC-002) — worst case **rate 0.5 Hz, depth 1, irregularity 1**;
  render ≥ one period per-sample; assert `max|Δ| ≤ 2.0e-3`.
- `TEST_CASE("BreathingModulator_PeriodMatchesRate")` (SC-003b, irregularity=0) — at rate {0.05, 0.1, 0.2} Hz,
  render ≫ longest period, dominant period via first autocorrelation maximum (or FFT peak); assert measured period
  is within a **documented tolerance band = FFT bin width at the render length** of `1/rate`, monotone ordering
  across the three settings.
- `TEST_CASE("BreathingModulator_ShapeAsymmetricAndNonSinusoidal")` (FR-021, irregularity=0) — render one cycle
  per-sample; (a) assert trough→peak (rise) duration ≠ peak→trough (fall) duration and their ratio ≈ 40/60 within
  tolerance; (b) FFT the cycle and assert **significant energy at 2f and 3f** relative to the fundamental (a
  symmetric sine leaves these near zero).
- `TEST_CASE("BreathingModulator_IrregularityJittersPeriod")` (FR-024) — at irregularity 0.7 fixed seed, measure
  successive cycle periods; assert they **vary** cycle-to-cycle, each stays within `±0.5·irregularity` of nominal
  and is **always positive**; at irregularity 0 assert successive periods are **identical**.
- `TEST_CASE("BreathingModulator_SeededDeterminism")`, `TEST_CASE("BreathingModulator_ResetRewindsToSeed")`
  (SC-004) — as T001.
- `TEST_CASE("BreathingModulator_SampleRateInvariant")` (SC-005 **option a** — cycle-boundary draw is
  wall-clock-aligned) — render same wall-clock duration at 44100/96000; compare RMS + dominant period within a
  tight measured tolerance; mean via **absolute** range-span tolerance only.
- `TEST_CASE("BreathingModulator_NoAllocInProcess")` (SC-006), `TEST_CASE("BreathingModulator_SourceRangeIndependentOfDepth")`
  (FR-006), `TEST_CASE("BreathingModulator_OutputDefinedAfterPrepare")` (FR-004) — as T001.
- `TEST_CASE("BreathingModulator_EdgeCases")` — `processBlock(0)` no-op; **irregularity=1 never yields a
  non-positive period**.

**Implement** — phase `φ∈[0,1)` at `rate` Hz. Hardcoded 40/60 asymmetric shape (`§2.1` of plan):
inhale `φ∈[0,0.4)`: `u=φ/0.4; y=0.5·(1−cos(π·u^0.8))`; exhale `φ∈[0.4,1)`: `v=(φ−0.4)/0.6; y=0.5·(1+cos(π·v^1.3))`.
Output `= depth·(2y−1)`. `kMinRate=0.01f`, `kMaxRate=0.5f`; `setRate` clamps. Irregularity: at each `φ` wrap draw
`jitter = 1 + irregularity·0.5·rng_.nextFloat()`, then `jitter = max(jitter, 0.1f)` (positive-period guard);
`jitter` scales the **next** cycle's increment; at irregularity 0 `jitter` is exactly 1.0. Control surface stays
exactly {rate, depth, irregularity}. Light 20 ms output smoother for block-boundary safety. `getSourceRange()`
→ `{-1,+1}`. Seed member `Xorshift32 rng_{0xB2EA}` / `configuredSeed_=0xB2EA`. Same prepare/reset/process/processBlock
contract as T001.

**Verify:** `dsp_processors_tests.exe "BreathingModulator_*"` after T007.

### T003 [P] — `TidalModulator` (Layer 2)

**Create:** `dsp/include/krate/dsp/processors/tidal_modulator.h`,
`dsp/tests/unit/processors/tidal_modulator_test.cpp`.
**Roadmap:** 115-116. **FRs:** FR-001..006, FR-031..033. **SCs:** SC-001,002,003b,004,005,006. **ODR:** clear.

**Write the failing test first** — `TEST_CASE`s:
- `TEST_CASE("TidalModulator_NeverExceedsRange")` (SC-001 sanity) — extremes {rate 0/1 → period 600 s/30 s,
  depth 0/1}; at the 10 min-max-period setting render ≥30 min by stepping `processBlock(512)` and sampling
  `getCurrentValue()` per block plus a short per-sample worst-alignment window; assert ∈ `{-1,+1}`, 0 violations.
- `TEST_CASE("TidalModulator_BoundedUnderPhaseAlignment")` (SC-001/FR-033, load-bearing) — assert directly that
  the layer weights satisfy `Σ|w_k| = 1` (`w_k=1/3`, three layers) so worst-case constructive alignment cannot
  exceed the range; plus a long-render sanity pass.
- `TEST_CASE("TidalModulator_MaxSlewBounded")` (SC-002) — worst case **shortest period (30 s), depth 1, all three
  layers at full amplitude**; render ≥ one 30 s period per-sample; assert `max|Δ| ≤ 2.0e-3`.
- `TEST_CASE("TidalModulator_PeriodTracksSetting")` (SC-003b) — at rate {0.2,0.5,1.0} (→ `P_base` mapped),
  render ≫ longest period; dominant period via first autocorrelation maximum / FFT peak; assert within the
  documented FFT-bin-width tolerance of `P_base`, monotone ordering.
- `TEST_CASE("TidalModulator_NoExactRepeat")` (FR-032) — over the longest render, assert no length-W sample window
  re-appears bit-identically shifted by any tested lag (bounded finite-horizon claim).
- `TEST_CASE("TidalModulator_SeededDeterminism")`, `TEST_CASE("TidalModulator_ResetRewindsToSeed")` (SC-004) —
  as T001 (seed varies only the initial phase offsets; period/statistics are seed-independent).
- `TEST_CASE("TidalModulator_SampleRateInvariant")` (SC-005 option a) — same wall-clock at 44100/96000; compare
  RMS + dominant period tight tolerance; mean absolute range-span tolerance.
- `TEST_CASE("TidalModulator_NoAllocInProcess")` (SC-006),
  `TEST_CASE("TidalModulator_SourceRangeIndependentOfDepth")` (FR-006),
  `TEST_CASE("TidalModulator_OutputDefinedAfterPrepare")` (FR-004),
  `TEST_CASE("TidalModulator_EdgeCases")` (`processBlock(0)` no-op; 10 min period stays continuous & bounded).

**Implement** — 3 layers, each a beating sine pair: `L_k = 0.5·(sin θ_k0 + sin θ_k1)`, output
`= depth·Σ_k (1/3)·L_k`. Fixed incommensurate ratios `{1.0, 1.41421356, 1.73205081}` (√2, √3 — hardcoded, NOT
seed-drawn). `kMinPeriod=30.0f`, `kMaxPeriod=600.0f`, `kDetune=0.02f`. `setRate(r01)` maps
`P_base = lerp(600→30, r01)`; `P_k = clamp(P_base·ratio_k, 30, 600)`; `f_k0=1/P_k`, `f_k1=f_k0·(1+kDetune)`.
**Phase accumulators `double`, wrapped `mod 2π` every block** (long-render drift guard); `sin` on the wrapped
double, cast to float at output. Six sine phases seeded from `rng_.nextUnipolar()·2π`. Light 20 ms output smoother.
`getSourceRange()` → `{-1,+1}`. Seed member `Xorshift32 rng_{0x71DA}` / `configuredSeed_=0x71DA` (valid hex).

**Verify:** `dsp_processors_tests.exe "TidalModulator_*"` after T007. (30 min renders: use `processBlock(512)` +
per-block sampling to keep runtime sane.)

### T004 [P] — `OrbitModulator` (Layer 2)

**Create:** `dsp/include/krate/dsp/processors/orbit_modulator.h`,
`dsp/tests/unit/processors/orbit_modulator_test.cpp`.
**Roadmap:** 117-118. **FRs:** FR-001..006, FR-041..043. **SCs:** SC-001,002,004,005,006. **ODR:** clear.

**Write the failing test first** — `TEST_CASE`s:
- `TEST_CASE("OrbitModulator_NeverExceedsRange")` (SC-001, both axes) — extremes {rate 0.01/0.5, coupling 0/1,
  growth −1/0/+1, depth 0/1}; render ≥60 s; assert **both** `getCurrentValue()` (x) and `getY()` ∈ `{-1,+1}`,
  0 violations.
- `TEST_CASE("OrbitModulator_MaxSlewBounded")` (SC-002) — worst case **max rate 0.5, max coupling 1, depth 1**;
  render per-sample ≥ one period; assert `max|Δ| ≤ 2.0e-3` on **both** x and y.
- `TEST_CASE("OrbitModulator_SeededDeterminism")`, `TEST_CASE("OrbitModulator_ResetRewindsToSeed")` (SC-004) —
  assert on both x and y streams.
- `TEST_CASE("OrbitModulator_SampleRateInvariant")` (SC-005 option a) — same wall-clock 44100/96000; RMS tight,
  mean absolute tolerance.
- `TEST_CASE("OrbitModulator_NoAllocInProcess")` (SC-006),
  `TEST_CASE("OrbitModulator_SourceRangeIndependentOfDepth")` (FR-006),
  `TEST_CASE("OrbitModulator_OutputDefinedAfterPrepare")` (FR-004 — check x AND `getY()`),
  `TEST_CASE("OrbitModulator_EdgeCases")` — `processBlock(0)` no-op; **growth=−1 never sticks at a point**
  (radius floors at `kRadiusMin`, not 0), **growth=+1 never diverges to Inf** (radius clamps at 1).

**Implement** — two Kuramoto-coupled phase oscillators: `dφ1/dt = ω1 + k·sin(φ2−φ1)`,
`dφ2/dt = ω2 + k·sin(φ1−φ2)`, `ω1=2π·rate`, `ω2=2π·rate·(1+kOscDetune)`, `kOscDetune=0.1`. Forward-Euler at
control rate (`dt=controlDtSeconds_`; unconditionally stable here). `x = depth·r·sin(φ1)` (base
`getCurrentValue()`), `y = depth·r·sin(φ2)` (`getY() const noexcept`). Radius: per control step
`r += growth·kRadiusRate·dt`, then `r = clamp(r, kRadiusMin=0.05f, 1.0f)`; `growth=0` sustains. `kMinRate=0.01f`,
`kMaxRate=0.5f`; `setRate` clamps; `setCoupling(k01)` [0,1]; `setGrowth(g)` [-1,1]; `setDepth(d01)` [0,1].
Two 20 ms output smoothers `xSmoother_`,`ySmoother_`; both `getCurrentValue()`/`getY()` return the clamped
smoother value. Initial `φ1,φ2` seeded from `rng_.nextUnipolar()·2π`; `r` starts at 1. `getSourceRange()`→`{-1,+1}`.
Seed member `Xorshift32 rng_{0x0B1F}` / `configuredSeed_=0x0B1F` (valid hex).

**Verify:** `dsp_processors_tests.exe "OrbitModulator_*"` after T007.

### T005 [P] — `SplineTrajectory` (Layer 2)

**Create:** `dsp/include/krate/dsp/processors/spline_trajectory.h`,
`dsp/tests/unit/processors/spline_trajectory_test.cpp`.
**Roadmap:** 119-120. **FRs:** FR-001..006, FR-051..054. **SCs:** SC-001,002,003a,004,005,006. **ODR:** clear.

**Write the failing test first** — `TEST_CASE`s:
- `TEST_CASE("SplineTrajectory_NeverExceedsRange")` (SC-001/FR-054) — extremes {interval 0.5/30 s, depth 0/1};
  render ≥60 s per-sample; assert ∈ `{-1,+1}`, 0 violations (waypoints in `[-0.8,0.8]`, Lebesgue bound 1.25 →
  `|q|≤1.0` by construction).
- `TEST_CASE("SplineTrajectory_MaxSlewBounded")` (SC-002) — worst case **shortest interval 0.5 s, depth 1**;
  render per-sample; assert `max|Δ| ≤ 2.0e-3`.
- `TEST_CASE("SplineTrajectory_C1AtWaypointJoins")` (FR-053) — render per-sample across several waypoint
  boundaries; form second difference `d2[n]=out[n]−2out[n−1]+out[n−2]`; assert `max|d2|` in a small window
  straddling each join is within the same order as `max|d2|` over segment interiors (no join spike). A
  C0-but-not-C1 impl produces a one-sample `d2` spike at joins that this rejects.
- `TEST_CASE("SplineTrajectory_AutocorrTimeTracksSpacing")` (SC-003a) — at `setWaypointInterval` {1, 4, 16} s,
  render ≥10× the longest interval, mean-detrend, first 1/e-crossing lag; assert **strictly increasing** with
  spacing, `≫ 1 sample`, no single dominant periodic peak.
- `TEST_CASE("SplineTrajectory_SeededDeterminism")`, `TEST_CASE("SplineTrajectory_ResetRewindsToSeed")` (SC-004).
- `TEST_CASE("SplineTrajectory_SampleRateInvariant")` (SC-005 **option b** — waypoint draws are on the sample
  grid) — render at 44100/96000 for **≥8 seeds**; compare mean RMS + mean decorrelation time across seeds;
  tolerance = measured across-seed spread.
- `TEST_CASE("SplineTrajectory_NoAllocInProcess")` (SC-006),
  `TEST_CASE("SplineTrajectory_SourceRangeIndependentOfDepth")` (FR-006),
  `TEST_CASE("SplineTrajectory_OutputDefinedAfterPrepare")` (FR-004),
  `TEST_CASE("SplineTrajectory_EdgeCases")` — `processBlock(0)` no-op; **a block larger than the waypoint
  interval consumes multiple waypoints without overrun** (loop the ring rotation).

**Implement** — uniform Catmull-Rom over a **4-waypoint ring** `std::array<float,4> wp_`, parameter `u∈[0,1)`:
`q(u)=0.5·[2p1 + (−p0+p2)u + (2p0−5p1+4p2−p3)u² + (−p0+3p1−3p2+p3)u³]`. `du = 1/(interval·sr)`; on `u≥1`:
`u−=1`, rotate ring (drop `wp_[0]`, shift, draw fresh `wp_[3] = rng_.nextFloat()·kWaypointMax`); loop for large
blocks. `kMinInterval=0.5f`, `kMaxInterval=30.0f`, `kWaypointMax=0.8f`; `setWaypointInterval(double)` clamps.
**Evaluated per-sample** (genuinely C1 — do not decimate the output through the smoother for continuity). Output
`= depth·q(u)`; clamp to `{-1,+1}` kept only as inert safety net (preserves C1). Initial 4 waypoints + every refill
from `rng_.nextFloat()·kWaypointMax`. `getSourceRange()`→`{-1,+1}`. Seed member `Xorshift32 rng_{0x5F11}` /
`configuredSeed_=0x5F11` (valid hex).

**Verify:** `dsp_processors_tests.exe "SplineTrajectory_*"` after T007.

### T006 [P] — `GrowthEnvelope` (Layer 2, **unipolar `[0,1]`**)

**Create:** `dsp/include/krate/dsp/processors/growth_envelope.h`,
`dsp/tests/unit/processors/growth_envelope_test.cpp`.
**Roadmap:** 121-122. **FRs:** FR-001..006, FR-061..063. **SCs:** SC-001,002,004,005,006. **ODR:** clear.

**Write the failing test first** — `TEST_CASE`s (no RNG — determinism is plain like-for-like equality):
- `TEST_CASE("GrowthEnvelope_NeverExceedsRange")` (SC-001) — extremes {duration 1/60 s}; at 60 s render ≥180 s
  after `trigger()`; assert every `getCurrentValue()` ∈ `getSourceRange()` = `{0.0f,1.0f}`, 0 violations.
- `TEST_CASE("GrowthEnvelope_MaxSlewBounded")` (SC-002) — worst case **min duration 1 s, full-range rise**;
  render the full rise per-sample; assert `max|Δ| ≤ 2.0e-3` (threshold is 1e-3 × span; span here = 1 → 1.0e-3).
  *(Note: unipolar span is 1, so use ≤ 1.0e-3 for this modulator.)*
- `TEST_CASE("GrowthEnvelope_RiseAndHoldBehavior")` (FR-062/FR-063) — (1) before any `trigger()`,
  `getCurrentValue() == 0.0f`; (2) `trigger()` mid-rise: sample value just before/after — output does **not**
  drop toward 0 (continuation), stays non-decreasing; (3) rendered rise is **monotonic non-decreasing** to the
  top then **holds at 1** with no fall; (4) `trigger()` after completion is a **no-op** (stays at 1).
- `TEST_CASE("GrowthEnvelope_Determinism")` (SC-004, deterministic) — two instances, identical
  `prepare`+`trigger`+`processBlock` sequence → exact equality; `reset()` returns to Idle/0 and a re-run matches.
- `TEST_CASE("GrowthEnvelope_SampleRateInvariant")` (SC-005 option a) — render the rise at 44100/96000; compare
  the value trajectory at matched wall-clock times within a tight measured tolerance.
- `TEST_CASE("GrowthEnvelope_NoAllocInProcess")` (SC-006),
  `TEST_CASE("GrowthEnvelope_SourceRangeIndependentOfDepth")` (FR-006 — `getSourceRange()` fixed `{0,1}`),
  `TEST_CASE("GrowthEnvelope_OutputDefinedAfterPrepare")` (FR-004 — reads 0, finite),
  `TEST_CASE("GrowthEnvelope_EdgeCases")` — `processBlock(0)` no-op; `trigger()` after completion no-op.

**Implement** — normalized logistic rise-and-hold. `τ = elapsed/D ∈[0,1]`, `L(τ)=1/(1+exp(−k·(τ−0.5)))`,
`k=kSteepness=10`, output `y=(L(τ)−L(0))/(L(1)−L(0))` (exact 0 at τ=0, exact 1 at τ=1; `l0_=L(0)`,
`invSpan_=1/(L(1)−L(0))` precomputed once). `kMinDuration=1.0f`, `kMaxDuration=60.0f`; `setDuration` clamps.
State machine `enum class Phase { Idle, Rising, Complete }`: Idle → `getCurrentValue()`=0; `trigger()`
Idle→Rising (`elapsed_=0`), Rising→no-op (continuation), Complete→no-op; Rising advances `elapsed_+=1/sr`, on
`elapsed_≥D` → Complete + output 1; **no fall/release segment**. `reset()` → Idle, elapsed 0, output 0. No RNG:
provide an empty **plain non-virtual** `void setSeed(uint32_t) noexcept {}` if a uniform API is wanted (never
`override`). Light 20 ms output smoother so block-decimated advance introduces no boundary step.
`getSourceRange()`→`{0.0f,1.0f}`.

**Verify:** `dsp_processors_tests.exe "GrowthEnvelope_*"` after T007.

---

## GROUP 3 — combined perf test (depends on all six headers existing)

### T007 — `life_modulators_perf_test.cpp` (SC-007, combined `[.perf]`)

**Create (new, disjoint):** `dsp/tests/unit/processors/life_modulators_perf_test.cpp`.
**Depends on:** all six headers (T001–T006) existing (includes all six).
**SC:** SC-007. **FRs:** shared-contract sweep.

**Write the test** — `#include` all six headers; `using namespace Krate::DSP;`:
- `TEST_CASE("LifeModulators_ControlRateCost","[.perf]")` — instantiate one of each of the six; `prepare(48000)`;
  time advancing all six once per **512-sample block** via `processBlock(512)` over many iterations; report
  **ns/block**. Assert `≤ baseline × 1.5` against a **checked-in ns/block baseline** written as a named constant
  in this file (relative regression bound). Informational reference: `≤ 0.05%` of the 512-samples-at-48 kHz
  budget (512/48000 ≈ 10.67 ms) ≈ **5.3 µs/block**. Tag `[.perf]` so it is excluded from the default run.
- Optional `TEST_CASE("LifeModulators_SharedContractSweep")` — for each of the six, assert `getSourceRange()`
  returns the expected fixed pair (`{-1,+1}` for five, `{0,1}` for Growth) and that a `prepare`+`processBlock`
  round leaves `getCurrentValue()` finite and in range.

**Set the checked-in baseline:** run once on the dev machine, record the measured ns/block into the constant with
a comment noting the machine; the ×1.5 bound is the regression gate.

**Verify:** `dsp_processors_tests.exe "[.perf]"` after T008 registers it.

---

## GROUP 4 — integration (single CMake task, then full run, then portability) — all sequential

### T008 — Register all 7 new test files in CMake, build, run the full layer suite

**Edit (shared file — this is the ONE authoritative CMake change):** `dsp/tests/CMakeLists.txt`.
Insert these 7 lines into the `dsp_processors_tests` source list **immediately before the closing `)` at line
275** (after `unit/processors/test_modal_bank_frequency.cpp` at line 274):

```
    unit/processors/brownian_drift_test.cpp
    unit/processors/breathing_modulator_test.cpp
    unit/processors/tidal_modulator_test.cpp
    unit/processors/orbit_modulator_test.cpp
    unit/processors/spline_trajectory_test.cpp
    unit/processors/growth_envelope_test.cpp
    unit/processors/life_modulators_perf_test.cpp
```

No other CMake change (headers are header-only; the target already links `KrateDSP`, `Catch2::Catch2`,
`test_helpers` — `render_fingerprint.h` available). Confirm no executor left a stray edit from GROUP 1–3.

**Build:** `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_processors_tests`.
Fix any compiler warnings to zero before running tests.

**Run (default, `[.perf]` excluded):** `build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5`
— assert the summary line is "All tests passed" (or the pass count matches with 0 failed).
**Run perf explicitly once** to seed/verify the baseline: `... "[.perf]"`.

**Verify:** `dsp_processors_tests` builds with zero warnings; full non-perf suite green; `[.perf]` case runs.

### T009 — Portability gate

**Run:** `node tools/check-portability.js` (WSL g++ — catches MSVC-only constructs; the six headers use only
`<algorithm> <cmath> <cstddef> <cstdint> <utility> <array>` transcendentals + `double` accumulators, no SDK
constants, no narrowing brace-init, no SIMD, no `std::isnan`). Also run
`node tools/lint-arch-guarded-includes.js` (no arch-guarded krate includes) and, since determinism/statistical
tests avoid float-bit goldens, `node tools/lint-float-bit-goldens.js`.

**Verify:** all three linters clean. This is the final Phase 1 gate (DSP-only — no pluginval / clang-tidy plugin
roster change, no plugin touched).
