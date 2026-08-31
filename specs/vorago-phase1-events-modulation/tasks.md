# Tasks: Vorago Phase 1 — Modulation Gap-Fill + Slow Event Engine

**Spec:** `specs/vorago-phase1-events-modulation/spec.md`
**Plan:** `specs/vorago-phase1-events-modulation/plan.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 1 (lines 154–172)
**Test target:** `dsp_processors_tests` (all new sources) · `dsp_primitives_tests`,
`dsp_systems_tests`, `ruinae_tests`, `disrumpo_tests` must stay green (shared `ChaosModel` enum).
**Plugin work:** none. Vorago's plugin starts at roadmap Phase 11.

---

## How to read this file

- Tasks are **T001…T017**, grouped into **ordered GROUPS**. Complete a group before starting the next.
- Within a group, `[P]` marks tasks that are **parallel-safe** — they touch fully disjoint *new* files.
  Unmarked tasks in a group run **in listed order**.
- Every task is self-contained: exact files, the failing test to write **first** (file, `TEST_CASE`
  name, exact assertions with numbers), then the implementation intent, then the verifying target.
- Canonical repo order inside every implementation task: **failing test → implement → zero warnings →
  tests pass**.
- **No commit tasks.** Commits happen outside this workflow.

### Build / run commands (always the full CMake path on Windows)

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests dsp_primitives_tests dsp_systems_tests

build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "PerlinNoiseSource_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SlowEventScheduler_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "ChaosModSource_*"     2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[.perf]"              2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[.harness]"           2>&1 | tail -5
```

Redirect long runs to a log file on the **first** run; never re-run a suite just to re-read output.

### Rules that bind every task below

1. **Finiteness:** use `Krate::DSP::detail::isFinite` (`dsp/include/krate/dsp/core/db_utils.h:118`
   for `float`, `:126` for `double`). **Never** `std::isnan/isinf/isfinite` — `tools/lint-nonfinite-symbols.js`
   bans them and `-ffast-math` folds them on the macOS leg.
2. **Allocation harness:** new test TUs include **`<allocation_detector.h>` only**.
   `dsp/tests/unit/processors/brownian_drift_test.cpp:27-28` is the single owner of
   `<allocation_operator_overrides.h>` in `dsp_processors_tests`; a second include is a
   duplicate-symbol link error (documented at `life_modulators_perf_test.cpp:19-23`).
3. **No bit-exact float goldens.** Determinism is exact equality of two `std::vector<float>`s produced
   by the *same binary in the same run* — never a checked-in digest.
4. **Layer discipline:** both new headers are **Layer 2** and may include only Layer 0/1 + stdlib
   (`core/modulation_source.h`, `core/random.h`, `core/db_utils.h`, `primitives/smoother.h`).
   `slow_event_scheduler.h` needs **no** `smoother.h`.
5. **ODR (re-verified this session, 0 hits):** `grep -rn "class PerlinNoiseSource\|class SlowEventScheduler\|Aizawa" dsp/ plugins/ tools/`
   returned nothing. The name **`ScheduledEvent` is forbidden** (3 pre-existing definitions outside
   `Krate::DSP`); the nested POD is `SlowEventScheduler::Event`.
6. **NaN-safe clamping:** every `float` setter in both new components routes through a private
   `static constexpr float sanitizeClamp(float v, float lo, float hi) noexcept` that maps NaN → `lo`
   via `detail::isNaN` (`db_utils.h:99`), then compares. `std::clamp` **propagates** NaN. Integer
   setters use plain `std::clamp`.

### Deviation from the requested task shape (stated, not hidden)

The requested shape puts CMake registration in the **last** group. That is not executable here:
`dsp/tests/CMakeLists.txt` lists test sources explicitly, CMake **fails to configure** if a listed
source does not exist, and until a TU is listed it is not compiled — so no task in Groups 2–5 could be
verified. Registration is therefore front-loaded as **T001**, still a **single task on the shared
file**, covering all six TUs plus the `VORAGO_P1_HARNESS_DIR` compile definition in one edit. The last
group keeps the remaining integration work (shared lint-list edits, full-suite run, portability check,
repo gates).

---

## GROUP 1 — Build wiring (shared file; one task, no parallelism)

### T001 — Register the six new test TUs and the harness output macro

**Edit (shared):** `dsp/tests/CMakeLists.txt`
**Create (stubs):** six files under `dsp/tests/unit/processors/`

Nothing can be built or run until this lands. Create each of the six TUs as a **compilable stub** —
one line, `#include <catch2/catch_all.hpp>`, no `TEST_CASE` — so CMake configures and the target links
before any real content exists. Each later task replaces its own stub.

1. Create the six stubs:
   - `dsp/tests/unit/processors/perlin_noise_source_test.cpp`
   - `dsp/tests/unit/processors/chaos_mod_source_aizawa_test.cpp`
   - `dsp/tests/unit/processors/slow_event_scheduler_test.cpp`
   - `dsp/tests/unit/processors/vorago_p1_perf_test.cpp`
   - `dsp/tests/unit/processors/vorago_p1_longrun_test.cpp`
   - `dsp/tests/unit/processors/vorago_p1_harness.cpp`

2. In `dsp/tests/CMakeLists.txt`, insert the six paths into the `dsp_processors_tests` source list
   **before the closing `)` at line 287**, immediately after
   `unit/processors/diffusion_network_zeromod_test.cpp` (line 286), under a banner matching the
   Seraphis Phase 4 banner at line 284:

   ```cmake
       # Vorago Phase 1 (specs/vorago-phase1-events-modulation)
       unit/processors/perlin_noise_source_test.cpp
       unit/processors/chaos_mod_source_aizawa_test.cpp
       unit/processors/slow_event_scheduler_test.cpp
       unit/processors/vorago_p1_perf_test.cpp
       unit/processors/vorago_p1_longrun_test.cpp
       unit/processors/vorago_p1_harness.cpp
   ```

3. Add the FR-081 harness output macro **beside the existing `KRATE_DSP_TESTS_DIR` definition at
   lines 458-459**:

   ```cmake
   # Vorago Phase 1 FR-081: the [.harness] trajectory writer resolves its output
   # directory from this macro, so CSVs land in the build tree regardless of the
   # launch working directory (same rationale as KRATE_DSP_TESTS_DIR above).
   target_compile_definitions(dsp_processors_tests
       PRIVATE VORAGO_P1_HARNESS_DIR="${CMAKE_BINARY_DIR}/vorago_p1/")
   ```

**Do NOT touch:**
- `dsp/CMakeLists.txt` `KRATE_DSP_PROCESSORS_HEADERS` (line 128) — IDE-visibility only, already partial
  (`brownian_drift.h`, `spline_trajectory.h`, `chaos_mod_source.h` are all absent). Seraphis Phase 1
  precedent: **no change**.
- No new CMake test target. `dsp_processors_tests` is already registered with `catch_discover_tests`
  at line 797.

**Verify:** `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_processors_tests`
configures and builds clean (zero warnings). Existing suite still green:
`build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5`.

---

## GROUP 2 — `PerlinNoiseSource` (Layer 2)

Sequential. T002 must be **red** (does not compile — the header does not exist) before T003 starts.

### T002 — Failing tests for `PerlinNoiseSource`

**Replace:** `dsp/tests/unit/processors/perlin_noise_source_test.cpp`
**Tags:** `[processors][perlin][vorago]`
**Includes:** `<krate/dsp/processors/perlin_noise_source.h>`, `<krate/dsp/core/db_utils.h>`,
`<allocation_detector.h>` (**not** the operator-overrides header), Catch2.

Write these `TEST_CASE`s. Every numeric threshold below is the assertion — do not soften any of them.

**1. `PerlinNoiseSource_SharedContract`** — FR-001 / FR-019.
- `static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, Krate::DSP::PerlinNoiseSource>)`.
- Bind through a base handle (proves virtual dispatch, not shadowing):
  `ModulationSource& ms = src; REQUIRE(ms.getCurrentValue() == src.getCurrentValue());`
  `REQUIRE(ms.getSourceRange() == std::pair{-1.0f, 1.0f});`
- After **default construction** and again after `prepare(48000.0)` with no configuration call:
  `REQUIRE(src.getRate() == PerlinNoiseSource::kDefaultRate);` (0.1f)
  `REQUIRE(src.getOctaves() == PerlinNoiseSource::kDefaultOctaves);` (2)
  `REQUIRE(src.getDepth() == PerlinNoiseSource::kDefaultDepth);` (1.0f)
  `REQUIRE(src.getCurrentValue() == 0.0f);` — `n(0) = 0` exactly for every seed.

**2. `PerlinNoiseSource_NeverExceedsRange`** — SC-001.
- Corner grid: `rate ∈ {kMinRate (0.005f), kMaxRate (5.0f)}` × `octaves ∈ {1,2,3,4}` ×
  `depth ∈ {0.0f, 0.5f, 1.0f}` × **8 seeds**, 300 s each at 48 kHz.
- **Drive with `processBlock(32)`**, one capture per control step (≈3.4e7 control steps total). The
  bound/finiteness/depth-scaling clauses are not per-sample-rate-sensitive; a per-sample render would
  be ≈1.4e10 samples.
- At every captured value: `REQUIRE(std::abs(v) <= 1.0f)` and `REQUIRE(detail::isFinite(v))`.
- At `depth == 0.0f`: `REQUIRE(v == 0.0f)` exactly, at every sample.
- **FR-006 binding**, inside the corner loop at depth 0, 0.5 **and** 1:
  `REQUIRE(src.getSourceRange() == std::pair{-1.0f, 1.0f});` — the range must not shrink with depth.
- **Non-tautology, asserted only where `rate >= 0.1f`** (at `kMinRate` a 300 s render advances 1.5
  cells and the peak is seed-dependent, 0.26–0.36 — a guaranteed intermittent failure):
  `REQUIRE(peakDepth1 > 0.5)` and `REQUIRE(std::abs(peakDepthHalf / peakDepth1 - 0.5) < 1e-4)`.
  A clamp cannot produce exact half-scaling.

**3. `PerlinNoiseSource_MaxSlewBounded`** — SC-002. **Per-sample at 48 kHz, never accelerated.**
- 120 s, `rate = kMaxRate`, `octaves = 4`, `depth = 1`, pinned seed.
- Compute the prediction **in the test**, with `5.0` as a **literal, NOT `kOutputSmoothMs`** (using
  the class constant makes the band self-referential and unfailable):
  ```cpp
  const double kMaxSlope = 2.7;                 // kGradientNormalize 2.0 x per-cell raw max 1.35
  const double fbm4      = 32.0 / 15.0;         // = 2.1333  (Sum(a_k*l_k)/Sum(a_k), n = 4)
  const double alpha     = 1.0 - std::exp(-5000.0 / (5.0 * 48000.0));   // 2.0618e-2
  const double gain      = alpha / (1.0 - std::pow(1.0 - alpha, 32.0)); // 4.2373e-2
  const double predicted = gain * kMaxSlope * fbm4 * 5.0 * 32.0 / 48000.0;  // 8.118e-4
  ```
- `REQUIRE(measured <= predicted);` **and** `REQUIRE(measured >= 0.5 * predicted);`
  (two-sided: over-smoothed/frozen fails the lower edge, unbounded fails the upper.)
- Repeat at `octaves = 1` with `fbm1 = 1.0` (predicted 3.81e-4).
- Note in a comment: this row catches **under**-smoothing only; the over-smoothing half of FR-018 is
  carried by SC-003 clause (c)'s absolute floor.

**4. `PerlinNoiseSource_SpectralRolloff`** — SC-003. Uses a **local double-precision iterative radix-2
FFT written inside this TU** (~35 lines), **not** `Krate::DSP::FFT`: `fft.h:47` documents
`kMaxFFTSize = 8192` while `prepare()` validates only power-of-two, and the −129/−84 dB reference
figures sit near a float32 FFT's noise floor at 65 536 points.
- **Clauses (a)(b)(d) — `rate = 0.1f`, depth 1, 48 kHz.** Render 600 s with
  `processBlock(kControlRateInterval)`, capturing one point per control step (1500 Hz); **decimate by
  15 → 100 Hz** ⇒ 60 000 points; **Hann window (mandatory)**; zero-pad to **65 536** ⇒ 1.526 mHz bins.
  - **(a)** at **every** `n ∈ {1,2,3,4}`: `REQUIRE(energyBelow(8*rate) / totalEnergy >= 0.99);`
    (measured 1.0000 / 1.0000 / 0.99999 / 0.99979) and
    `REQUIRE(maxBinDbAbove(32*rate) <= peakDb - 30.0);` (measured −129 / −114 / −97 / −84 dB).
  - **(b)** `fracAbove(4*rate)` **strictly increasing** over n = 1…4 (measured 7e-9 → 3.0e-5 →
    9.0e-4 → 8.55e-3), and `REQUIRE(frac[3] >= 10.0 * frac[1]);` (measured ratio ≈285×).
  - **(d)** at n = 1: autocorrelation of the decimated trajectory at lag `0.1/rate` s **> 0.7**
    (measured 0.936) and `|autocorrelation|` at lag `2/rate` s **< 0.35** (measured 0.152).
    Lag-1 is deliberately **not** used (≈0.99999 for any smooth signal at 100 Hz).
- **Clause (c) — `rate = kMaxRate` (5 Hz).** The stride **must change**: the top octave is 40 Hz and
  the band edge `32*rate` is 160 Hz, both at/above a 100 Hz grid's 50 Hz Nyquist. Use the
  **undecimated 1500 Hz control trajectory**, first **65 536 points (43.7 s)**, Hann-windowed, no
  zero-padding ⇒ 22.9 mHz bins, 750 Hz Nyquist.
  - Same strict monotonicity of `fracAbove(4*rate)` over n = 1…4.
  - **Plus the absolute floor** (monotonicity alone is preserved by *any* low-pass and cannot fail —
    measured monotonic at 1, 5, 20 **and** 100 ms):
    `REQUIRE(fracAbove(8.0 * rate) >= 1.30e-4);` at **n = 4** (`8*rate` = 40 Hz).
    Write the measured table into the comment: at 5 ms the value is 1.706e-4…2.012e-4 across 8 seeds;
    at 20 ms it is 7.941e-5…9.588e-5 — the floor sits 1.31× below the worst 5 ms seed and 1.36× above
    the worst 20 ms seed.

**5. `PerlinNoiseSource_SeededDeterminism`** — SC-004.
- Base: two instances, same seed, **400 captured blocks** → `REQUIRE(a == b)` on the `std::vector<float>`s;
  different seeds → `REQUIRE(a != b)`; `reset()` reproduces run 1 exactly.
- **(a) Non-aligned block sequence:** render 60 s driving the source with the repeating block sequence
  `{37, 1, 64, 512}` and compare per-sample against a pure `process()` render —
  `REQUIRE(std::abs(d) <= 1e-6f)` at every sample. (Two multiples of 32 would **not** discriminate:
  per FR-003 the control counter is instance state, so control steps land at identical absolute
  sample indices for any such partitioning.)
- **(c) Octave-stream identity (FR-015):** same seed, both advanced to the same control-step count,
  `p1.setOctaves(1)`, `p4.setOctaves(4)` → `REQUIRE(p1.getOctaveValue(0) == p4.getOctaveValue(0))`
  **bit-exactly**. The **outputs are NOT compared** — they legitimately differ by the `1/Σaₖ`
  normalisation (1.0 vs 1.875).
- (b) is carried by `PerlinNoiseSource_SampleRateInvariant` below.

**6. `PerlinNoiseSource_SampleRateInvariant`** — SC-005. **Per-sample, never accelerated.**
- `rate = kDefaultRate` (0.1f), `octaves ∈ {1, 4}`, `depth = 1`, same seed. 120 s wall-clock at
  **44 100** and **96 000**; linearly resample the 44.1 k run onto the 96 k grid.
- `REQUIRE(maxAbsDiff <= 2.0e-3);` (measured 1.705e-4 at n = 1, 2.972e-4 at n = 4)
- `REQUIRE(std::abs(rms44 / rms96 - 1.0) <= 0.02);` (measured deviation 0.000 % / 0.001 %)
- `REQUIRE(std::abs(mean44 - mean96) <= 2.0e-4);` — **absolute**, never relative-to-mean (zero-mean
  trap). Measured 1.208e-6 / 6.080e-6.
- **Not run at `kMaxRate`** — the same 0.73 ms control-grid offset contributes ~2e-2 there and the
  criterion would fail on a correct implementation. Say so in a comment.

**7. `PerlinNoiseSource_NoAllocInProcess`** — SC-013 (Perlin half).
- `prepare(48000.0)` and an untracked warm-up **outside** the tracked scope, then the tracked window:
  `500 × processBlock(512)` + `4096 × process()` + `40 × processBlock(48'000)`.
- `REQUIRE(detector.stopTracking() == 0u);`

**8. `PerlinNoiseSource_EdgeCases`** — FR-002 / FR-019 / Edge Cases.
- `processBlock(0)` is a no-op — compare `getPosition()` and `getCurrentValue()` before/after.
- `processBlock(10'000'000)` → finite, in range.
- Advance methods called **before** `prepare()` → no crash, finite output.
- `prepare()` twice → no half-completed state; output finite and equal to the fresh-prepare value.
- `setRate(0.0f / 1e9f / -1.0f / +Inf / -Inf / NaN)` → clamped to `[kMinRate, kMaxRate]`;
  **NaN → `kMinRate`** (via `sanitizeClamp`), asserted through `getRate()`.
- `setOctaves(0)` → 1; `setOctaves(99)` → 4.
- `setDepth(0.0f)` → output exactly 0 for all time.
- `prepare(0.0)` / `prepare(-1.0)` → finite output (1 Hz floor).
- `setSeed(0)` produces the same stream as `setSeed(2463534242u)` (`random.h:73-74`, `kDefaultSeed`
  definition `:85`) — documented alias, **not** an error.
- Adjacent seeds `n` / `n+1`: cross-correlation over 300 s **< 0.2**.

**Verify:** the TU fails to compile (`perlin_noise_source.h` does not exist). That is the red state.

---

### T003 — Implement `PerlinNoiseSource`

**Create:** `dsp/include/krate/dsp/processors/perlin_noise_source.h` (Layer 2, header-only)
**Includes (only):** `core/modulation_source.h`, `core/random.h`, `core/db_utils.h`,
`primitives/smoother.h`, `<algorithm> <array> <cmath> <cstddef> <cstdint> <utility>`.

Make T002 green. The shape template is `dsp/include/krate/dsp/processors/brownian_drift.h:94`
(verified this session: `process()` at `:178-188`, `processBlock()` at `:194-206`, `getCurrentValue()`
at `:212-214`, `getSourceRange()` at `:217-219`, the 1 Hz floor at `:122`).

**Public surface** (`class PerlinNoiseSource : public ModulationSource`):
- Constants: `kMinRate = 0.005f`, `kMaxRate = 5.0f`, `kMinOctaves = 1`, `kMaxOctaves = 4`,
  `kPersistence = 0.5f`, `kLacunarity = 2.0f`, `kGradientNormalize = 2.0f`, `kOutputSmoothMs = 5.0f`,
  `kControlRateInterval = 32` (== `brownian_drift.h:105`), `kDefaultRate = 0.1f`,
  `kDefaultOctaves = 2`, `kDefaultDepth = 1.0f`, `kDefaultPerlinSeed = 0x9E37u`.
- `prepare(double) noexcept` · `reset() noexcept` · `setSeed(uint32_t) noexcept` (**plain member, NOT
  `override`** — only the two ABC methods are virtual, `modulation_source.h:37,41`) · `setRate(float)` ·
  `setOctaves(int)` · `setDepth(float)` · getters `getRate/getOctaves/getDepth/getPosition` ·
  `process()` · `processBlock(size_t)` · `getCurrentValue() const noexcept override` ·
  `getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }` ·
  `[[nodiscard]] float getOctaveValue(std::size_t) const noexcept`.

**State (all fixed-size; no RNG object stored):** `double sampleRate_`, `double cellsPerControlStep_`,
`double positionCells_` (**unwrapped**), `float rate_/depth_`, `int octaves_`, `double amplitudeSum_`,
`int samplesUntilControl_`, `uint32_t configuredSeed_`,
`std::array<std::uint32_t, kMaxOctaves> octaveSeeds_`, `OnePoleSmoother outputSmoother_`.

**Math:**
- `static constexpr std::int64_t kIndexBias = 1LL << 31;` — documented so negative lattice indices are
  well-defined rather than UB-by-omission.
- `gradientAt(k, i)`: `deriveStreamSeed(octaveSeeds_[k], static_cast<std::size_t>(i + kIndexBias)) & 1u`
  → `+1.0f` / `-1.0f`. **Unit** gradients — this is what makes `kGradientNormalize = 2.0f` exact.
  Stateless hash, **never** a running stream (FR-012).
- `rawOctaveNoise(k, x)`: `fx = floor(x)`, `i0 = (int64)fx`, `t = x - fx`,
  `s = t*t*t*(t*(t*6.0 - 15.0) + 10.0)` (smootherstep),
  `n = (g0*t)*(1.0 - s) + (g1*(t - 1.0))*s`, return `float(kGradientNormalize * n)`. All in `double`.
- `octaveSeeds_[k] = deriveStreamSeed(configuredSeed_, k)` for **all `kMaxOctaves`**, always —
  independent of `octaves_`. This is what makes `getOctaveValue(0)` bit-identical across octave counts
  (SC-004(c)) and `getOctaveValue(i)` legal for any `i < kMaxOctaves`.
- fBm: octave `k` evaluated at `positionCells_ * (1ULL << k)` (exact — `kLacunarity = 2`), summed with
  `amp *= kPersistence`, divided by `amplitudeSum_ = 2 − 2^(1−n)` → **1, 1.5, 1.75, 1.875**,
  recomputed in `setOctaves`. Multiply by `depth_`; terminal `std::clamp(..., -1.0f, 1.0f)` is an
  **inert net**, never the source of the bound.
- `getOctaveValue(i)` = `rawOctaveNoise(i, positionCells_ * (1ULL << i))` with an
  `i >= kMaxOctaves → 0.0f` guard. It must **not** consult `octaves_`, `amplitudeSum_`, `depth_` or
  the smoother.
- `advanceControlStep()`: `positionCells_ += cellsPerControlStep_;` then
  `outputSmoother_.setTarget(evaluateFbm());`
- `process()` / `processBlock()`: copy `brownian_drift.h:178-206` verbatim (decrement-then-check /
  check-then-chunk); discard the `[[nodiscard]]` `outputSmoother_.process()` with `static_cast<void>`.
- `setRate` updates `cellsPerControlStep_ = rate_ * 32 / sampleRate_` in `double` and **never touches
  `positionCells_`** — a rate change alters speed of travel, not place.
- `prepare(sr)`: floor `sr` at 1.0, `updateIncrement()`, `outputSmoother_.configure(kOutputSmoothMs, (float)sampleRate_)`,
  then `initState()`. `reset()` is `initState()` alone. `initState()`: re-derive octave seeds, zero
  `positionCells_`, zero `samplesUntilControl_`, `outputSmoother_.snapTo(evaluateFbm())`.

**Header banner must record** (so a future reader does not "fix" them):
1. FR-017's derivation — `|n(x)| <= 0.5` attained at `t = 0.5` with opposing gradients, hence
   `kGradientNormalize = 2.0f` maps one octave to exactly `[-1,+1]`.
2. The `OnePoleSmoother` **completion snap** (`smoother.h:55,199-201,251-253`,
   `kCompletionThreshold = 1e-4`): at `kMaxRate`/n = 4 the residual never falls under 1e-4 so the snap
   never fires; at low rates it fires every control step and the output degenerates to the raw
   32-sample staircase with a step ≤ 1e-4, i.e. 20× inside SC-002's threshold. **Documented, not a defect.**
3. FR-007's carve-out: the lattice index is **never wrapped** — any wrap is an exact repeat of the
   whole trajectory and would pass every criterion in this phase.

**Zero warnings.** **Verify:**
```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "PerlinNoiseSource_*" 2>&1 | tail -5
```
All `PerlinNoiseSource_*` cases green.

---

### T004 — SC-003(c) injection check (prove the absolute floor can fail)

**Temporarily edit:** `dsp/include/krate/dsp/processors/perlin_noise_source.h` (one constant, reverted)

An absolute threshold nobody has seen fail is the same defect one layer down.

1. Set `kOutputSmoothMs = 20.0f`.
2. Rebuild `dsp_processors_tests`; run `"PerlinNoiseSource_SpectralRolloff"`.
3. **REQUIRE the observation:** the case goes **red** on the `fracAbove(8*rate) >= 1.30e-4` floor
   (20 ms yields 7.941e-5…9.588e-5 across seeds). If it stays green, the floor or the measurement is
   wrong — fix it before proceeding, do not lower the threshold.
4. **Restore `kOutputSmoothMs = 5.0f`**, rebuild, re-run, confirm green.

**Verify:** `dsp_processors_tests.exe "PerlinNoiseSource_SpectralRolloff"` green after restore, and the
red observation from step 3 is recorded in the task output.

---

## GROUP 3 — Aizawa attractor (Layer 1 enum amendment + Layer 2 implementation)

Sequential. This is the **only** step in the phase that can break the Linux/macOS CI legs invisibly
from Windows (`-Wswitch` on two `default:`-less switches), so T008's warning gate runs immediately
after the code lands and before Group 4.

### T005 — Failing tests for Aizawa + the divergence counter

**Replace:** `dsp/tests/unit/processors/chaos_mod_source_aizawa_test.cpp`
**Tags:** `[processors][chaos][aizawa][vorago]`
**Includes:** `<krate/dsp/processors/chaos_mod_source.h>`, `<krate/dsp/primitives/chaos_waveshaper.h>`,
`<krate/dsp/core/db_utils.h>`, `<allocation_detector.h>`, Catch2.

**1. `ChaosModSource_AizawaBoundedAndChaotic`** — SC-006. 1 h **accelerated** render
(`processBlock(4096)`) at speeds `{kMinSpeed (0.05f), 0.1f, 1.0f, 5.0f, kMaxSpeed (20.0f)}`
(`chaos_mod_source.h:37,38`).
- **(a) coupling 0:** `REQUIRE(std::abs(out) <= 1.0f)`, `REQUIRE(detail::isFinite(out))`,
  `REQUIRE(src.getDivergenceResetCount() == 0u)` at every speed.
- **(b) non-tautology at coupling 0**, over the second half:
  `REQUIRE(maxAbs > 0.5 && maxAbs < 0.99);` (measured 0.750–0.780)
  `REQUIRE(stddev > 0.1);` (measured 0.263–0.331)
  This is the **only** clause that catches the `dt >= 0.02` fixed-point collapse, where the output is
  identically 0 while every bound and finiteness clause still passes. `tanh` bounds *any* finite input,
  so "output ∈ [-1,+1]" alone proves nothing.
- **(c) chaotic character at coupling 0:** autocorrelation of the trajectory falls below `1/e` within
  **60 s** of wall clock at every speed (measured worst case 8.9 s at `kMinSpeed`, 0.023 s at
  `kMaxSpeed`); **and** sensitive dependence at `kMaxSpeed` — two instances whose initial `x` differs
  by **exactly 1e-4** reach RMS output difference **> 0.1 within 60 s** (measured 0.21 by 10 s).
  **Perturbation recipe — checked against the header, two traps:** the coupling path adds
  `coupling_ * inputLevel_ * 0.1f` (`chaos_mod_source.h:213-216`) and is gated on
  `std::abs(inputLevel_) > 0.001f` (**strict**). Use `setCoupling(0.1f)` + `setInputLevel(1.0e-2f)`
  for **exactly one control step**, then `setCoupling(0.0f)`. `setInputLevel(1.0e-3f)` fires **no**
  perturbation at all (the two instances stay bit-identical forever, silently asserting nothing);
  `setCoupling(1e-3f)`/`setInputLevel(0.1f)` displaces by 1e-5, an order below the criterion.
- **(d) coupling 1.0 with `setInputLevel(1.0f)`:** bound + finiteness +
  `REQUIRE(src.getDivergenceResetCount() == 0u)` **only**. Clauses (b) and (c) are explicitly **not**
  asserted — a DC-biased input legitimately saturates the output, and the state legitimately reaches
  `|state| ≈ 112` at `kMinSpeed` against a guard threshold of 250.
- Put the §2.3 measured table into the test comments.

**2. `ChaosModSource_DivergenceCounterObservable`** — FR-036 positive test. **Without this, SC-006's
three `== 0u` clauses are tautologies** that a stubbed `return 0u;` passes in full.
- `prepare(48000.0)`; `setModel(ChaosModel::Aizawa)`; `REQUIRE(getDivergenceResetCount() == 0u)`.
- `setCoupling(1.0f)`; `setInputLevel(1.0e5f)`; `512 × processBlock(32)` →
  `REQUIRE(getDivergenceResetCount() > 0u)` and `REQUIRE(detail::isFinite(src.getCurrentValue()))`.
  (`setInputLevel` is **unclamped**, `chaos_mod_source.h:119-121`, so this displaces `x` by 1e4 per
  control step — 40× the `safeBound_ * 10 = 250` threshold — and the guard fires on the first step.)
- Zeroing rule, both directions: `src.reset(); REQUIRE(getDivergenceResetCount() == 0u);` and again
  after `prepare(48000.0)`.
- **Repeat the whole case for `ChaosModel::Lorenz`** — the counter is model-agnostic, and asserting it
  on a pre-existing model is what proves the increment sits in the shared guard rather than in an
  Aizawa-only branch.

**3. `ChaosModSource_AizawaNoRegression`** — SC-007 (in-TU half).
- `REQUIRE(static_cast<uint8_t>(ChaosModel::Lorenz) == 0)` … `Henon == 3`, **`Aizawa == 4`**.
- `ChaosWaveshaper ws; ws.setModel(ChaosModel::Aizawa); REQUIRE(ws.getModel() == ChaosModel::Lorenz);`
  — the validator's semantics are **unchanged** (`chaos_waveshaper.h:451-455`).

**4. `ChaosModSource_AizawaNoAllocInProcess`** — SC-013 (Aizawa third).
- `prepare` + untracked warm-up outside the tracked scope; tracked window
  `500 × processBlock(512)` + `4096 × process()` + `40 × processBlock(48'000)` in Aizawa mode;
  `REQUIRE(detector.stopTracking() == 0u);`

**Verify:** the TU fails to compile (`ChaosModel::Aizawa` and `getDivergenceResetCount()` do not
exist). That is the red state.

---

### T006 — Amend `ChaosModel` (Layer 1) — enum + two exhaustiveness arms

**Edit (shared, existing):** `dsp/include/krate/dsp/primitives/chaos_waveshaper.h`

Three surgical edits, nothing else in the file changes.

1. **Enum at lines 52-57** — **append, never insert** (`chaos_waveshaper_test.cpp:33-36` pins
   `Lorenz==0 … Henon==3`, `:40` static-asserts the `uint8_t` underlying type, and plugin state
   persists such enums by index):
   ```cpp
       Henon = 3,    ///< Henon map (a=1.4, b=0.3)
       /// Aizawa system (a=0.95, b=0.7, c=0.6, d=3.5, e=0.25, f=0.1).
       /// Implemented by ChaosModSource (Layer 2) ONLY. ChaosWaveshaper's validator
       /// (chaos_waveshaper.h:451-455) still rejects it and substitutes Lorenz.
       Aizawa = 4
   ```
2. **Validator at lines 451-455 — UNCHANGED.** `setModel(Aizawa)` continues to fall back to `Lorenz`.
3. **Both `default:`-less switches** — `updateAttractor()` (switch at line 640) and
   `resetModelState()` (switch at line 685) — gain a **grouped** label sharing the `Lorenz` arm
   (grouping, **not** fallthrough, so `-Wimplicit-fallthrough` stays quiet):
   ```cpp
           // Unreachable by construction: setModel() (chaos_waveshaper.h:451-455)
           // substitutes Lorenz for anything above Henon, so model_ is never Aizawa
           // here. Present solely for -Wswitch exhaustiveness on the shared
           // ChaosModel enum. Aizawa is a ChaosModSource-only model (FR-034).
           case ChaosModel::Aizawa:
           case ChaosModel::Lorenz:
               // ... existing Lorenz body, unchanged
   ```

**Do NOT modify** `dsp/tests/unit/primitives/chaos_waveshaper_test.cpp` — SC-007 requires it to pass
**unmodified**, including the invalid-enum fallback cases at `:579-583`.

**Blast radius (re-verified this session):** no `switch` over `ChaosModel` exists outside those two
headers. Every plugin use is a bounded cast — `plugins/ruinae/src/engine/ruinae_voice.h:958`
(`std::clamp(model, 0, 3)`), `plugins/ruinae/src/parameters/distortion_params.h:17,84`
(`kChaosModelCount = 4`), `plugins/disrumpo/src/controller/controller.cpp:379`,
`plugins/disrumpo/src/processor/processor_params.cpp:676-679`,
`plugins/disrumpo/src/processor/processor_state.cpp:244,681`. None can emit `4`; none switches.
`ModulationEngine::setChaosModel/getChaosModel` (`systems/modulation_engine.h:493,613`) forward by
value. **No plugin edit, no state-version bump, no parameter change.**

**Verify:** `--target dsp_primitives_tests` builds with **zero new warnings**;
`build/windows-x64-release/bin/Release/dsp_primitives_tests.exe 2>&1 | tail -5` green.

---

### T007 — Implement Aizawa in `ChaosModSource` (Layer 2)

**Edit (shared, existing):** `dsp/include/krate/dsp/processors/chaos_mod_source.h`

Six edits. Make T005 green.

1. **Public constant** beside `kLorenzScale`…`kHenonScale` (lines 46-49):
   `static constexpr float kAizawaScale = 1.5f;  ///< attractor x-extent ~ +/-1.5`
2. **`updateModelParams()` switch (lines 158-181)** — new arm, literals inline to match surrounding
   style, with the measurement recorded in-line:
   ```cpp
               case ChaosModel::Aizawa:
                   // dt = baseDt_ * speed (:211) and kMaxSpeed = 20 (:38), so
                   // baseDt_ * 20 <= 0.01 is required. Forward-Euler Aizawa was
                   // simulated over dt in [5e-4, 0.2] from four initial states:
                   // chaotic (x-extent +/-1.5..1.6) for dt <= 0.015, and for
                   // dt >= 0.02 it COLLAPSES onto the x = y = 0 fixed point
                   // (z ~= -1.105) where the output is identically 0 -- silently,
                   // with no divergence and no guard reset. 5.0e-4 puts dt in
                   // [2.5e-5, 1.0e-2], entirely inside the verified-chaotic region.
                   baseDt_ = 5.0e-4f;
                   normalizationScale_ = kAizawaScale;
                   // Guard threshold is safeBound_ * 10 (:306-309). The coupling
                   // path (:213-216; setInputLevel unclamped at :119-121)
                   // legitimately drives |state| to ~112 at kMinSpeed with a
                   // full-scale DC input, so 25 -> 250 never fires. The Chua value
                   // (5 -> 50) fires ~2000x per 600 s render.
                   safeBound_ = 25.0f;
                   break;
   ```
3. **`resetModelState()` switch (lines 183-200)**:
   `case ChaosModel::Aizawa: state_ = {0.1f, 0.0f, 0.0f}; break;`
4. **`updateAttractor()` switch (lines 218-231)**: `case ChaosModel::Aizawa: updateAizawa(dt); break;`
   plus a new private method beside `updateLorenz` (`:239`):
   ```cpp
       void updateAizawa(float dt) noexcept {
           constexpr float a = 0.95f, b = 0.7f, c = 0.6f;
           constexpr float d = 3.5f,  e = 0.25f, f = 0.1f;
           // All three derivatives are evaluated from the SAME state: updating
           // state_ in place mid-expression would silently make this a
           // Gauss-Seidel step and change the attractor.
           const float x = state_.x, y = state_.y, z = state_.z;
           const float dx = (z - b) * x - d * y;
           const float dy = d * x + (z - b) * y;
           const float dz = c + a * z - (z * z * z) / 3.0f
                          - (x * x + y * y) * (1.0f + e * z)
                          + f * z * (x * x * x);
           state_.x += dx * dt;
           state_.y += dy * dt;
           state_.z += dz * dt;
       }
   ```
5. **FR-036 divergence observability** — one member, one accessor, one increment:
   - `private: std::uint32_t divergenceResetCount_ = 0;`
   - `public: [[nodiscard]] std::uint32_t getDivergenceResetCount() const noexcept;`
   - `++divergenceResetCount_;` inside `checkAndResetIfDiverged()` (lines 306-312), **before** the
     `resetModelState()` call.
   - Zeroed in `prepare()` (`:53-58`) and `reset()` (`:60-65`) **only** — **not** in
     `resetModelState()`, which the guard itself calls and which `setModel()` (`:103-109`) also calls.
6. **FR-002 sample-rate floor.** `prepare()` (line 54) currently stores `sampleRate` unfloored. Change
   to `sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;` — behaviourally inert (`sampleRate_` is
   stored but never read by the attractor math, whose step is `baseDt_ * effectiveSpeed` at `:211`).

**Zero warnings.** **Verify:**
```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests dsp_primitives_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "ChaosModSource_*" 2>&1 | tail -5
```
All `ChaosModSource_*` cases green; `dsp_primitives_tests` and `dsp_systems_tests` green.

---

### T008 — SC-007 warning gate (GCC + Clang `-Wswitch -Werror`, out of suite)

**Create:** `dsp/tests/portability/chaos_enum_exhaustiveness.cpp`

`tools/check-portability.js` **structurally cannot** catch this: it invokes
`g++ -std=c++20 -fsyntax-only -DNDEBUG -DRELEASE` with **no warning flags and no `-Werror`**
(`:229-231`), and `isCheckable()` accepts only `.cpp|.cc` (`:203-208`) while the T006/T007 change lives
entirely in headers. Catch2 summary lines report test results, not diagnostics.

1. Write a four-line TU that `#include`s both `<krate/dsp/primitives/chaos_waveshaper.h>` and
   `<krate/dsp/processors/chaos_mod_source.h>` and defines nothing. **Do not add it to any CMake
   target** — it exists solely for the two commands below.
2. Run both from the repo root (include roots taken from `tools/check-portability.js:39-50`):
   ```bash
   wsl -e bash -c 'cd /mnt/f/projects/iterum && \
     g++ -std=c++20 -Wall -Wextra -Wswitch -Werror -fsyntax-only \
         -I . -I dsp/include -I dsp/tests -I tests -I tests/test_helpers \
         dsp/tests/portability/chaos_enum_exhaustiveness.cpp'

   wsl -e bash -c 'cd /mnt/f/projects/iterum && \
     clang++ -std=c++20 -Wall -Wextra -Wswitch -Werror -fsyntax-only \
         -I . -I dsp/include -I dsp/tests -I tests -I tests/test_helpers \
         dsp/tests/portability/chaos_enum_exhaustiveness.cpp'
   ```
   **Both must exit 0.** A non-zero exit means T006's grouped `case ChaosModel::Aizawa:` arms are
   missing or misplaced.
3. **MSVC leg — build-log diff.** Capture
   `cmake --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_processors_tests`
   to a log file on the **first** run, and diff for new `C####` warning lines against the pre-change
   log. **Zero additions required.** Never re-run a build just to look at its output.

**Verify:** both WSL commands exit 0; the MSVC log diff shows zero new `C####` lines.

---

## GROUP 4 — `SlowEventScheduler` (Layer 2)

Sequential. T009 must be red before T010 starts.

> **Configuration-ordering rule — binding on every case in this group and on T011/T012.**
> `initState()` draws the FR-067 pre-roll period from `minIntervalSeconds_`/`maxIntervalSeconds_`
> **as they stand at that moment**. **All interval-range configuration must be applied BEFORE
> `prepare()`, or `reset()` must be called after it.** Configuring after `prepare()` leaves the
> scheduler idling for a pre-roll drawn from the *previous* range, and no later setter shortens it
> (FR-066 latches). Measured pre-rolls at the 20–90 s defaults: `kDefaultEventSeed (0x51E7)` →
> **41.24 s** (u = 0.303378), `0x1` → 20.00 s, `0x3039` → 74.39 s, `0xBEEF` → 84.66 s,
> `0xABCD` → 70.96 s. Getting this wrong silently empties SC-009(i), SC-013 and SC-014 — SC-013's
> tracked window holds **45** rising edges with the correct ordering and **1** with the wrong one,
> against a `>= 20` guard.

### T009 — Failing tests for `SlowEventScheduler`

**Replace:** `dsp/tests/unit/processors/slow_event_scheduler_test.cpp`
**Tags:** `[processors][slow_events][vorago]`
**Includes:** `<krate/dsp/processors/slow_event_scheduler.h>`, `<krate/dsp/core/db_utils.h>`,
`<allocation_detector.h>`, Catch2.

**1. `SlowEventScheduler_IntervalDistribution`** — SC-008. 32 seeds × 500 events at the default
20–90 s range, accelerated with `processBlock(32)`, histogram built from `getPeriodSeconds()` sampled
at each `isEventActive()` rising edge.
- **(a)** every one of the 16 000 drawn periods ∈ `[20.0f, 90.0f]` — zero violations.
- **(b)** `REQUIRE(std::abs(mean - 55.0) <= 2.0);` (uniform mean 55, standard error ≈0.16 s → a >12σ
  band, cannot go flaky).
- **(c)** 10-bin histogram over the range flat within **±15 %** of 1600 per bin.
- **(d)** `setIntervalRange(30.0f, 30.0f)` **applied before `prepare()`** → consecutive rising-edge
  spacing within **one control step** of 30 s, and `REQUIRE(getPeriodSeconds() == 30.0f)` at every
  onset. (Under an end-of-release wait semantics this would read 30 + attack + hold + release and fail
  on a correct implementation — that identity is the point of the clause.)
- **(e) FR-067 pre-roll — the clause without which an implementation that fires at `t = 0` passes
  SC-008, SC-010, SC-011 and SC-012 unchanged.** Immediately after `prepare(48000.0)` with defaults:
  `REQUIRE(!s.isEventActive());`
  `REQUIRE(s.getActiveTarget() == SlowEventScheduler::kNoTarget);`
  `REQUIRE(s.getCurrentValue() == 0.0f);`
  capture `p0 = s.getPeriodSeconds()`; `REQUIRE(p0 >= 20.0f && p0 <= 90.0f);`
  advance and `REQUIRE` the first rising edge lands at `p0` within one control step, and that **every
  sample before it is exactly `0.0f`**. With `kDefaultEventSeed` the drawn `p0` is **41.236 s** —
  quote it in the comment so a change of draw order is visible. Repeat over the 32 seeds of clause (a).

**2. `SlowEventScheduler_EnvelopeC1AtJoins`** — SC-009. **Per-sample at 48 kHz, never accelerated.**
Two configurations, ≥ 10 events each:
- **(i)** `setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)` (0.05 s each)
  + `setIntervalRange(1.0f, 1.0f)` — **both applied BEFORE `prepare()`** (ordering rule above).
- **(ii)** the FR-055 defaults (5 / 3 / 8 s, 20–90 s range).
- Grid: `stride = std::lround(std::min({effA, effH, effR}) * sr / 100.0)` — **100 points per shortest
  segment**, the same construction as `spline_trajectory_test.cpp:265` (`kStride = 240`). Report the
  stride via `WARN`. A *per-sample* second difference is unmeasurable: for the rise shape it is
  8.6e-9 at the default attack and 8.6e-7 at 0.05 s — at or below the float32 noise floor (~1.2e-7).
- `REQUIRE(interiorMax > 1.0e-5);` — the precedent's value (`spline_trajectory_test.cpp:314`);
  analytic interior figure under the smootherstep shape is ≈**5.77e-4**, ~58× the threshold.
- `REQUIRE(joinMax <= 5.0 * interiorMax);` — a triple straddles a join when any of the four boundaries
  (onset, attackEnd, holdEnd, releaseEnd) lies in `[i-2, i]`. A C0-but-not-C1 join is `O(h·Δslope)`,
  one order larger in `h`, and is rejected.
- `REQUIRE(maxPerSampleDelta <= 2.0e-3);` at configuration (ii) (analytic worst case **7.81e-4** at
  48 kHz, 8.50e-4 at 44.1 kHz).
- **FR-058 read-surface identities**, sampled every control step across ≥ 3 events in configuration
  (ii) — these three accessors exist precisely because `getCurrentValue()`'s product destroys
  information, and no other case reads them:
  `REQUIRE(env >= 0.0f && env <= 1.0f);`
  `REQUIRE(std::abs(s.getCurrentValue() - static_cast<float>(s.getActivePolarity()) * s.getActiveDepth() * s.getEnvelopeValue()) <= 1e-6f);`
  — this rejects the plausible wrong implementations (`getEnvelopeValue()` returning the
  polarity-signed value, or `depth * env`, or 0);
  `REQUIRE(s.getEnvelopeValue() == 0.0f);` while `!isEventActive()`;
  `REQUIRE(std::abs(s.getEventDurationSeconds() - (s.getEffectiveAttackSeconds() + s.getEffectiveHoldSeconds() + s.getEffectiveReleaseSeconds())) <= 1e-6f);`

**3. `SlowEventScheduler_SeededDeterminism`** — SC-010.
- Same seed → identical `std::vector<float>` over **400 captured blocks**, **and** identical
  `std::vector<std::uint8_t>` target sequence and `std::vector<std::int8_t>` polarity sequence.
- Different seeds → different streams; 32 distinct seed pairs give distinct streams.
- `reset()` reproduces the post-`prepare` stream exactly (FR-061).
- `setSeed()` **mid-event** leaves that event's remaining samples **bit-identical** to an un-reseeded
  reference (FR-062).

**4. `SlowEventScheduler_BoundedOverLongRun`** — SC-011. 2 h accelerated (`processBlock(4096)`) across
the corner grid (interval extremes, segment extremes, depth extremes, bipolar probability 0/0.5/1,
`targetCount` 1/16, 8 seeds):
- `REQUIRE(std::abs(out) <= 1.0f)`, `REQUIRE(detail::isFinite(out))`,
  `getActiveTarget() ∈ [0, targetCount) ∪ {kNoTarget}` at every sample,
  `REQUIRE(getSourceRange() == std::pair{-1.0f, 1.0f})` at every setting.
- **Non-tautology:** `setDepthRange(0.15f, 0.5f)` vs `setDepthRange(0.3f, 1.0f)`, same seed →
  `REQUIRE(std::abs(peakA / peakB - 0.5) < 1e-4);` This is exact in `float` because
  `float(0.3) == 2*float(0.15)` and `float(0.7) == 2*float(0.35)` bit-exactly, and FR-060's fixed draw
  order keeps the two RNG streams aligned.

**5. `SlowEventScheduler_SampleRateInvariant`** — SC-012. `processBlock(32)` at **44 100** and
**96 000**, same seed, never at a reduced sample rate.
- Record the wall-clock second of each of the first **50** onsets;
  `REQUIRE(std::abs(t44[i] - t96[i]) <= 32.0 / 44100.0);` (= 7.256e-4 s) for **every** `i` — a
  **cumulative** bound on onset position, not a per-event bound.
- Event count over a fixed **30 min** wall-clock window identical.

**6. `SlowEventScheduler_NoAllocInProcess`** — SC-013 (scheduler half).
- **Pinned configuration, applied BEFORE `prepare()`:**
  `setIntervalRange(kMinIntervalSeconds, kMinIntervalSeconds)` (1 s) +
  `setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)`. At the defaults the
  tracked window (2 180 096 samples = 45.42 s at 48 kHz) is shorter than the 20 s minimum period and
  the whole run would sit idle, hiding an allocation in the draw path, in any state transition, or in
  the FR-063 multi-transition loop.
- `prepare` + untracked warm-up outside the tracked scope, then
  `500 × processBlock(512)` + `4096 × process()` + `40 × processBlock(48'000)`.
- `REQUIRE(detector.stopTracking() == 0u);`
- **Anti-vacuity guard:** `REQUIRE(risingEdges >= 20u);` — measured **45** with the correct ordering
  (≈2.25× margin); a return to the 20 s default drops it to ≈2 and fails loudly.

**7. `SlowEventScheduler_SetterStormContinuity`** — SC-018. Per-sample at 48 kHz.
- Advance to mid-attack, then **once per control step for the rest of the cycle** call all five
  setters — `setEnvelopeTimes`, `setIntervalRange`, `setDepthRange`, `setBipolarProbability`,
  `setTargetCount` — with deliberately different values (cycle a small deterministic table). Let the
  event complete naturally.
- `REQUIRE(maxPerSampleDelta <= 2.0e-3);` across the whole storm **including the joins it crosses**.
- **and** `REQUIRE` the three effective-segment getters and `getPeriodSeconds()` are **unchanged** for
  the event's duration — the direct FR-066 observation. An implementation that re-fits or re-draws
  mid-event fails at the instant the change is applied.

**8. `SlowEventScheduler_EdgeCases`** — FR-001 / FR-002 / Edge Cases.
- `static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, Krate::DSP::SlowEventScheduler>)`
  plus the base-handle binding (`ModulationSource& ms = s;` → `getCurrentValue()`/`getSourceRange()`
  agree with the derived calls).
- `processBlock(0)` is a no-op (state compare). `processBlock(10'000'000)` (≈3.5 min in one call) →
  finite, in range, correct number of events elapsed, zero allocation.
- Advance before `prepare()` → no crash, finite output. `prepare()` twice, and `prepare()` while an
  event is active → full re-initialisation, no half-completed segment that jumps.
- **`setIntervalRange(90.0f, 20.0f)` collapses to a fixed 90 s period:**
  `REQUIRE(s.getMinIntervalSeconds() == 90.0f && s.getMaxIntervalSeconds() == 90.0f);`
  FR-052's "both collapse to `min`" means the **min argument**, so `hi` is raised to `lo`.
  *(`spec.md` Edge Cases says "20 s" and is wrong — recorded as plan §8.8. Follow the header.)*
- `setIntervalRange(0, 0)` → clamped to `kMinIntervalSeconds` (1 s); fit rule scales the envelope
  inside 1 s; bounded, C1, allocation-free.
- `setEnvelopeTimes(0, 0, 0)` → each clamped to `kMinSegmentSeconds`; C1 still holds.
- `setEnvelopeTimes(300, 300, 300)` with the default 20–90 s range → effective **6.67 s** each at the
  next onset (scale = 20/900); then `setIntervalRange(600, 600)` → **200 s** each at the onset after.
  Assert **order-independence**: both call orders give the same effective times.
- `setTargetCount(0)` → 1, and `getActiveTarget()` then always returns 0 while active.
- `setBipolarProbability(0.0f)` → every event **positive**; `1.0f` → every event **negative**.
- NaN/±Inf into every float setter → clamped (NaN → the low bound), asserted through the getters.
- `prepare(0.0)` / `prepare(-1.0)` → finite output (1 Hz floor).
- `setSeed(0)` gives the same stream as `setSeed(2463534242u)`.

**Verify:** the TU fails to compile (`slow_event_scheduler.h` does not exist). That is the red state.

---

### T010 — Implement `SlowEventScheduler`

**Create:** `dsp/include/krate/dsp/processors/slow_event_scheduler.h` (Layer 2, header-only)
**Includes (only):** `core/modulation_source.h`, `core/random.h`, `core/db_utils.h`,
`<algorithm> <cmath> <cstddef> <cstdint> <utility>`. **No `smoother.h`** — the envelope is evaluated
per sample in closed form, so there is nothing to smooth.

Make T009 green.

**Public surface** (`class SlowEventScheduler : public ModulationSource`):
- `enum class Phase : std::uint8_t { Idle = 0, Attack = 1, Hold = 2, Release = 3 };`
- `struct Event { std::uint8_t target = kNoTarget; float depth = 0.0f; std::int8_t polarity = 1; };`
  — **the name `ScheduledEvent` is forbidden** (3 pre-existing definitions outside `Krate::DSP`).
- Constants: `kNoTarget = 0xFFu`, `kMaxTargets = 16u`, `kMinIntervalSeconds = 1.0f`,
  `kMaxIntervalSeconds = 600.0f`, `kMinSegmentSeconds = 0.05f`, `kMaxSegmentSeconds = 300.0f`,
  `kControlRateInterval = 32`, `kDefaultMinInterval = 20.0f`, `kDefaultMaxInterval = 90.0f`,
  `kDefaultAttack = 5.0f`, `kDefaultHold = 3.0f`, `kDefaultRelease = 8.0f`, `kDefaultMinDepth = 0.3f`,
  `kDefaultMaxDepth = 1.0f`, `kDefaultBipolarProbability = 0.5f`, `kDefaultTargetCount = 4u`,
  `kDefaultEventSeed = 0x51E7u`.
- Lifecycle `prepare(double)` / `reset()`; setters `setSeed`, `setIntervalRange`, `setEnvelopeTimes`,
  `setDepthRange`, `setBipolarProbability`, `setTargetCount`; configuration getters
  `getMinIntervalSeconds`, `getMaxIntervalSeconds`, `getAttackSeconds`/`getHoldSeconds`/
  `getReleaseSeconds` (**configured, pre-fit**), `getMinDepth`, `getMaxDepth`,
  `getBipolarProbability`, `getTargetCount`; advance `process()` / `processBlock(size_t)`;
  ABC `getCurrentValue()` / `getSourceRange()` → `{-1.0f, 1.0f}`; read surface `getActiveTarget`,
  `isEventActive`, `getEventPhase`, `getPeriodSeconds`, `getEventDurationSeconds`,
  `getEffectiveAttackSeconds`/`getEffectiveHoldSeconds`/`getEffectiveReleaseSeconds`,
  `getEnvelopeValue`, `getActiveDepth`, `getActivePolarity`.
- **`valueForTarget(uint8_t)` is deliberately absent** — one-line convenience with no information
  content; consumers write their own two-term target gate.

**State (fixed-size; no pool, no ring, no container):** `double sampleRate_`; stored configuration
(`minIntervalSeconds_`, `maxIntervalSeconds_`, `attackSeconds_`, `holdSeconds_`, `releaseSeconds_`,
`minDepth_`, `maxDepth_`, `bipolarProbability_`, `targetCount_`); latched-per-cycle (`Event active_`,
`double periodSamples_`, `attackEndSamples_`, `holdEndSamples_`, `releaseEndSamples_`,
`bool firstOnsetPending_`); clocks (`double elapsedSamples_`, `int samplesUntilControl_`);
`std::uint32_t configuredSeed_`; `Xorshift32 rng_`.

**Single-clock design — the whole component turns on this.** The envelope, the phase and the active
flag are **pure functions of `elapsedSamples_` and four latched boundary offsets**. Nothing about the
output is computed incrementally, so nothing can drift, step or accumulate; FR-064's non-accumulation
is satisfied *by construction*, and FR-066's latch rule and SC-018 hold with no extra machinery.

**Rise shape — smootherstep, NOT a raised cosine** (FR-056 amended, plan §8.7):
```cpp
    [[nodiscard]] static double riseShape(double u) noexcept {
        const double c = (u <= 0.0) ? 0.0 : (u >= 1.0) ? 1.0 : u;
        return c * c * c * (c * (6.0 * c - 15.0) + 10.0);   // 6c^5 - 15c^4 + 10c^3
    }
```
Rationale to record in the header banner: `getCurrentValue()` is a **per-sample** call (FR-065) and
nothing caches it, so the shape function runs 2048×/512-sample block in SC-014's four-scheduler
workload. Measured (WSL g++ 13 `-O2`, 2e7 iterations, two runs): `0.5 - 0.5*std::cos(pi*u)` costs
4.84–4.94 ns ⇒ ≈10 000 ns/block — **0.94× SC-014's 10 667 ns reference and 1.41× the 7111 ns baseline
ceiling, before the Perlin and Aizawa work**, i.e. SC-014 is unattainable with a cosine. The polynomial
costs 0.86–0.87 ns ⇒ ≈1780 ns/block, is C2 rather than C1, and is the same polynomial the Perlin
lattice uses. Consequences, all inside their bounds: peak slope `1.875/T` ⇒ worst-case per-sample slew
**7.81e-4** against 2.0e-3; peak second derivative `10/sqrt(3)` ⇒ decimated interior second difference
≈**5.77e-4** against `interiorMax > 1.0e-5`; onset residual artefact ≤ **2.4e-5**.

**Envelope / phase:**
```cpp
    [[nodiscard]] double envelopeAt(double t) const noexcept {
        if (firstOnsetPending_)    return 0.0;
        if (t < attackEndSamples_) return riseShape(t / attackEndSamples_);
        if (t < holdEndSamples_)   return 1.0;
        if (t < releaseEndSamples_)
            return riseShape((releaseEndSamples_ - t) / (releaseEndSamples_ - holdEndSamples_));
        return 0.0;   // idle stretch
    }
```
`getCurrentValue()` returns `clamp(float(polarity * depth * envelopeAt(elapsedSamples_)), -1, 1)` —
the clamp is an **inert net**, never the source of the bound. `getEnvelopeValue()` is computed
**directly** from `envelopeAt`, **never** by dividing `polarity*depth` back out (depth may be 0).

**Draw sequence** (`drawCycle()`, called once per onset, **never** from a setter): exactly four
`rng_.nextUnipolar()` values in the fixed order **period, target, depth, polarity** (FR-060 — this is
what keeps two runs differing only in a range setting RNG-aligned for SC-011).
`nextUnipolar()` returns **(0, 1]** (`random.h:50,67,89` — `next()` never returns 0), with two
deliberate consequences: the polarity test is `uPolarity <= bipolarProbability_` (**`<=`, not `<`**) so
probability 0 is all-positive and 1 is all-negative; and the target draw needs a `std::min` guard
because `uTarget` can be exactly 1.0. Then call `fitSegments()`.

**Fit rule** (`fitSegments()`, FR-055): `scale = min(1.0, minIntervalSeconds_ / (a + h + r))` — scaled
against `minIntervalSeconds_`, **not** against the drawn period, so events never overlap for **any**
draw. Uniform scaling preserves the shape (hence C1) and is order-independent. Evaluated against the
**currently stored** configuration only, at draw time, never under a running envelope.

**Pre-roll (FR-067):** `initState()` reseeds `rng_` from `configuredSeed_`, consumes **one**
`nextUnipolar()` for the pre-roll period, clears `active_`, zeroes the three boundary offsets,
`elapsedSamples_` and `samplesUntilControl_`, and sets `firstOnsetPending_ = true`. The scheduler
idles a full drawn period before its first onset and emits exactly `0.0f` throughout. The pre-roll is
**not** an event, so it consumes one draw rather than four. `getPeriodSeconds()` returns
`periodSamples_ / sampleRate_`; during the pre-roll that is the pre-roll length — say so in its doc
comment.

**Advance:** onset detection stays **control-rate** (that is the transition every criterion measures);
the loop is bounded and carries the remainder:
```cpp
    void takeOnsetIfDue() noexcept {
        // Bounded: periodSamples_ >= kMinIntervalSeconds * sampleRate_ > 0 always,
        // so elapsedSamples_ strictly decreases each iteration.
        while (elapsedSamples_ >= periodSamples_) {
            elapsedSamples_ -= periodSamples_;   // FR-064: carry, NEVER = 0
            firstOnsetPending_ = false;
            drawCycle();
        }
    }
```
`process()` / `processBlock()` copy `brownian_drift.h:178-206`'s decrement-then-check /
check-then-chunk asymmetry, accumulating `elapsedSamples_` instead of driving a smoother.

**Setters:** each writes stored configuration and **nothing else** — no redraw, no refit, no
state-machine touch. All float setters use `sanitizeClamp`. `setIntervalRange`: clamp both, then
`if (hi < lo) hi = lo;` (raise `hi`, never lower `lo`). `setDepthRange`: same pattern.
`setTargetCount`: `std::clamp<std::uint8_t>(c, 1u, kMaxTargets)`. `setSeed`: store **and** reseed.

**`prepare(sr)`:** floor at 1.0 then `initState()`. `reset()` is `initState()` alone.

**Header banner must state:** the poll-rate contract (per-sample getters, no caching — unlike
`BrownianDrift`'s plain member load at `brownian_drift.h:212-214`); the **configuration-ordering rule**
for consumers; and that seed 0 is a documented alias for `kDefaultSeed = 2463534242u`, not an error.

**Zero warnings.** **Verify:**
```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SlowEventScheduler_*" 2>&1 | tail -5
```
All `SlowEventScheduler_*` cases green.

---

## GROUP 5 — Perf, long-run and harness (three disjoint new TUs — parallel-safe)

All three depend on Groups 2–4 being green. Each owns exactly one file, already registered by T001.

### T011 [P] — SC-014 control-rate cost

**Replace:** `dsp/tests/unit/processors/vorago_p1_perf_test.cpp`
**`TEST_CASE("VoragoPhase1_ControlRateCost", "[.perf]")`** — the **leading dot**, copying
`life_modulators_perf_test.cpp:147` exactly. Do **not** invent a tag: the CI filter is
`~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` (`.github/workflows/ci.yml:366,638,1063`) and
Catch2 tag exclusion is exact-match, so an invented tag would enrol a hard wall-clock `REQUIRE` in the
per-push lane on all three shared-runner OS legs. SC-014 is a **developer-run gate**; the compiled
`static_assert` is the part CI enforces continuously.

- Workload: four `PerlinNoiseSource` (4 octaves each) + four `SlowEventScheduler` + one Aizawa
  `ChaosModSource`.
- **The measured block is advance + read, not advance alone.** Per block: one `processBlock(512)` on
  each of the nine instances, **then** an output-read step accumulated into a returned/`volatile`
  sink, copying `life_modulators_perf_test.cpp:124-130`'s `sumOutputs()` and its stated reason.
  **Read rates must match each component's published contract:** the four Perlin sources and the
  Aizawa source **once per block** (their value is written inside the control-rate update); the four
  schedulers **512 times per block** — per sample, which is what FR-065 defines and SC-009 polls.
  Without the second rate this row measures almost nothing for four of the nine objects, because
  `SlowEventScheduler::processBlock()` is integer counter arithmetic plus a rare `drawCycle()`.
- **Both schedulers' configurations applied BEFORE `prepare()`** (ordering rule in Group 4).
- Constants mirroring `life_modulators_perf_test.cpp:54,58,70,73`:
  ```cpp
  constexpr double kBlockBudgetNs      = 512.0 / 48000.0 * 1e9;   // 1.0667e7
  constexpr double kReferenceNsPerBlock = kBlockBudgetNs * 0.001; // 0.1 % = 10 667 ns
  constexpr double kBaselineNsPerBlock  = 5000.0;                 // provisional; tighten below
  constexpr double kRegressionFactor    = 1.5;
  static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock,
                "baseline x factor must stay below the reference figure");
  ```
  The checked-in baseline may **never** exceed **7111 ns**. Without this `static_assert` the gate is
  self-referential and any measured cost becomes acceptable (`life_modulators_perf_test.cpp:60-66`).
- **Report two figures with `WARN`** — idle-path ns/block (at the FR-052/FR-055 defaults) and
  **cycle-inclusive** ns/block (at the pinned `setIntervalRange(kMinIntervalSeconds, kMinIntervalSeconds)`
  + `setEnvelopeTimes(kMinSegmentSeconds ×3)` configuration, ≥ 1 full cycle per ~100 measured blocks).
  **The regression gate applies to the cycle-inclusive figure.**
- Finiteness of the sink via `detail::isFinite`.
- **Then tighten** `kBaselineNsPerBlock` to the measured dev-machine cycle-inclusive figure and
  re-check the `static_assert`. If the measured figure exceeds ≈2000 ns/block for the four schedulers
  alone, verify `riseShape` really is the polynomial — that is the reason SC-014 is attainable at all.

**Verify:** `dsp_processors_tests.exe "[.perf]" 2>&1 | tail -5` green; both `WARN` figures recorded;
`static_assert` compiles with the tightened baseline.

---

### T012 [P] — SC-017 long-run numerical resolution

**Replace:** `dsp/tests/unit/processors/vorago_p1_longrun_test.cpp`
**`TEST_CASE("VoragoPhase1_LongRunResolution", "[processors][vorago][long]")`** — `[long]` because it
costs > 15 s and its assertions are toolchain-**independent** (the CLAUDE.md rule for that tag).

8 h **accelerated** render (`processBlock(4096)`) at 48 kHz of `PerlinNoiseSource` at `kDefaultRate`
and at `kMaxRate` (4 octaves each) and of `SlowEventScheduler` at defaults.

**The measurement windows are 1 h, not the spec's 60 s, and the RMS/ZCR clauses apply to the Perlin
renders only** (plan §8.9 — SC-017 as written in `spec.md` fails on a correct implementation and is
arithmetically undefined for the scheduler). Evidence to record in the test comment:
- Perlin at `kDefaultRate` with 60 s windows is **6 lattice cells** and 11–21 zero crossings — both
  statistics are sampling noise. Over 8 seeds the RMS deviation reaches **30.7 %** and the ZCR
  deviation **100 %** against a 20 % bound; 5 of 8 seeds fail.
- Perlin with **1 h windows** (360 cells at `kDefaultRate`): worst over the same 8 seeds is RMS
  **5.3 %**, ZCR **7.7 %** — a 2.6× margin. At `kMaxRate`, ≤ 0.6 % / 0.7 %.
- Scheduler with 60 s windows: the output is 0 except during an event; the FR-067 pre-roll exceeds
  60 s for 3 of 8 seeds, so `rmsFirst` is exactly 0 and the RMS clause **divides by zero**.

Assertions:
- **Perlin (both rates)** — capture control-step values over the **first hour** and the **last hour**
  (5 400 000 points each):
  (a) `REQUIRE(std::abs(rmsLast / rmsFirst - 1.0) <= 0.20);`
  (b) same 20 % bound on the zero-crossing **rate**.
- **Scheduler** — no RMS or ZCR clause. Instead:
  (d) `REQUIRE(std::abs(double(eventsHour8) / double(eventsHour1) - 1.0) <= 0.20);` with
  `REQUIRE(eventsHour1 >= 10u);` as the anti-vacuity guard (measured hour-1 vs hour-8 deviation
  1.5–6.9 % over 8 seeds);
  `REQUIRE(p >= 20.0f && p <= 90.0f);` on `getPeriodSeconds()` at the end;
  **liveness:** `REQUIRE(rmsLast900s > 0.0);` over the final 900 s (≈16 events at the 55 s mean) —
  this is exactly the "scheduler stops firing" failure SC-017 names.
- (c) `REQUIRE(detail::isFinite(v))` on **every** value of **every** render, checked **inside the
  render loop** so nothing is buffered for 8 h. Keep only the four windowed buffers, never the whole
  trajectory.

The failure mode this exists to catch: a `float` accumulator whose ULP grows past the per-step
increment — the modulator silently freezes (RMS → 0, zero crossings → 0) or the scheduler stops
firing, while every other criterion in this phase still passes. No other criterion runs long enough.

**Verify:** `dsp_processors_tests.exe "VoragoPhase1_LongRunResolution" 2>&1 | tail -5` green.

---

### T013 [P] — SC-015 offline evaluation harness

**Replace:** `dsp/tests/unit/processors/vorago_p1_harness.cpp`
**`TEST_CASE("VoragoPhase1_TrajectoryHarness", "[.harness][processors][vorago]")`** — the leading dot
hides it from the default run, so FR-082's "writes nothing when not selected" is satisfied by the tag,
not by a flag (precedent: `dsp/tests/unit/effects/aether_reverb_perf_test.cpp:976` uses `[.perf]`).

Output directory from the T001 compile definition:
```cpp
#ifndef VORAGO_P1_HARNESS_DIR
#  error "VORAGO_P1_HARNESS_DIR must be injected by dsp/tests/CMakeLists.txt"
#endif
    const std::filesystem::path dir{VORAGO_P1_HARNESS_DIR};
    std::filesystem::create_directories(dir);
```

Write **exactly five** files, one row per control step, rendered with
`processBlock(kControlRateInterval)` at 48 kHz:

| File | Source | Configuration | Duration | Rows | Columns |
|---|---|---|---|---|---|
| `vorago_p1_perlin_oct1.csv` | `PerlinNoiseSource` | `kDefaultRate`, octaves 1, depth 1, `kDefaultPerlinSeed` | 60 s | 90 000 | `timeSeconds,value` |
| `vorago_p1_perlin_oct2.csv` | " | as above, octaves 2 | 60 s | 90 000 | " |
| `vorago_p1_perlin_oct4.csv` | " | as above, octaves 4 | 60 s | 90 000 | " |
| `vorago_p1_aizawa.csv` | `ChaosModSource` (`Aizawa`) | speed 1.0, coupling 0 | 60 s | 90 000 | " |
| `vorago_p1_slow_events.csv` | `SlowEventScheduler` | all defaults, `kDefaultEventSeed` | **900 s** | 1 350 000 | `timeSeconds,value,target,phase` |

- `timeSeconds` is written as `step * 32 / 48000.0` with `std::setprecision(6)` — **never** a running
  `float` sum.
- The scheduler window is 900 s because at the 20–90 s cadence a 60 s render contains zero or one
  event and would be a flat line of zeros across most seeds. Per FR-067's idle-first-onset rule, 900 s
  contains **≥ 9** events at the slowest possible draw and ~16 at the mean. The scheduler file is
  ≈40 MB — expected, and why the case is opt-in.

Then **re-open each file by name** and assert:
- a header line is present; every subsequent field parses as a number;
- `lastTimeSeconds` is within one control step of the stated duration;
- for `vorago_p1_slow_events.csv`: `REQUIRE(risingEdges >= 3);` (detected as `phase` transitioning out
  of 0) and at least one row with `target != 255`.

**Verify:**
```
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[.harness]" 2>&1 | tail -5
```
exits 0 and produces exactly the five files in `build/windows-x64-release/vorago_p1/`. Then delete the
directory and run the **default** suite once, confirming **no** files are written (FR-082).

---

## GROUP 6 — Integration

### T014 [P] — Make the clang-tidy gate non-vacuous

**Edit (shared):** `dsp/lint_all_headers.cpp`

Add the two new headers to the **alphabetical Layer 2 block** (`chaos_mod_source.h` is at line 95,
`pattern_scheduler.h` at line 120), each in its alphabetical slot:
```cpp
#include <krate/dsp/processors/pattern_scheduler.h>
#include <krate/dsp/processors/perlin_noise_source.h>
...
#include <krate/dsp/processors/sample_hold_source.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
```
Without this, `run-clang-tidy.ps1 -Target dsp` never sees the new headers and SC-016's clang-tidy
clause is vacuous.

**Do NOT** add `brownian_drift.h` — its absence is a pre-existing gap from Seraphis Phase 1 and is out
of scope for this phase.

**Verify:** `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` reports **no new
diagnostics**. Capture the output to a log file on the **first** run.

---

### T015 [P] — Make the non-finite-symbol gate non-vacuous

**Edit (shared):** `tools/lint-nonfinite-symbols.js`

Add **all eight** paths to the `GUARDED` array (declared at line 82; the block currently ends at
line 91 with the Seraphis Phase 6 aether entries), repo-relative with forward slashes, under a
`// Vorago Phase 1 (SC-016)` banner matching the banners above it:

```
dsp/include/krate/dsp/processors/perlin_noise_source.h
dsp/include/krate/dsp/processors/slow_event_scheduler.h
dsp/include/krate/dsp/processors/chaos_mod_source.h
dsp/tests/unit/processors/perlin_noise_source_test.cpp
dsp/tests/unit/processors/slow_event_scheduler_test.cpp
dsp/tests/unit/processors/chaos_mod_source_aizawa_test.cpp
dsp/tests/unit/processors/vorago_p1_longrun_test.cpp
dsp/tests/unit/processors/vorago_p1_perf_test.cpp
```

The gate covers an **explicit list, not a tree walk** (`lint-nonfinite-symbols.js:43-52`), so every
file left off it is unguarded. `vorago_p1_longrun_test.cpp` and `vorago_p1_perf_test.cpp` carry the
phase's longest-running finiteness assertions — a `std::isnan` there folds to `false` on the macOS
`-ffast-math` leg with nothing red. `chaos_mod_source.h` is added because **this phase modifies it**;
it is not on the list today. `vorago_p1_harness.cpp` is deliberately **absent** — it asserts
parseability, not finiteness.

**Verify:** `node tools/lint-nonfinite-symbols.js` exits 0.

---

### T016 — Full-suite green across every `ChaosModel` consumer

Sequential; run after T014/T015.

`chaos_waveshaper.h` and `chaos_mod_source.h` changed, so every target that includes them recompiles
and must be rebuilt and rerun (SC-007). A green `dsp_processors_tests` alone is **not** evidence.

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests dsp_primitives_tests dsp_systems_tests ruinae_tests disrumpo_tests

build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe  2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe     2>&1 | tail -5
build/windows-x64-release/bin/Release/ruinae_tests.exe          2>&1 | tail -5
build/windows-x64-release/bin/Release/disrumpo_tests.exe        2>&1 | tail -5
```

**Requirements:**
- Every suite's last line reads `All tests passed (...)`. Redirect to a log on the **first** run;
  never re-run a suite to re-read its output.
- **Zero compiler warnings** across the whole build. `dsp/tests/unit/primitives/chaos_waveshaper_test.cpp`
  must be green **unmodified**.
- Do **not** dismiss any failure as pre-existing. Fix it or stop.

**Verify:** all five suites green, zero warnings.

---

### T017 — Repo gates (SC-016)

Sequential; the last task.

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
```

All five must **exit 0**.

- `lint-layers.js` enforces that both new Layer 2 headers include only Layer 0/1 + stdlib.
- `lint-odr.js` enforces the `PerlinNoiseSource` / `SlowEventScheduler` / `SlowEventScheduler::Event`
  naming decisions.
- `lint-float-bit-goldens.js` enforces that no test in this phase pins a float render with a
  bit-exact digest.
- **`check-portability.js` alone does not prove warning-cleanliness** — it runs
  `g++ -fsyntax-only` with **no warning flags and no `-Werror`** (`:229-231`) and skips headers
  (`:203-208`). The `-Wswitch` evidence is **T008**, which must already be green.

Also confirm T014's clang-tidy run (`./tools/run-clang-tidy.ps1 -Target dsp`) reported no new
diagnostics.

**Verify:** all five lints exit 0; clang-tidy clean; T008's two WSL commands still exit 0.

---

## Dependency graph

```
T001 (CMake wiring)
  |
  +--> T002 -> T003 -> T004                (Group 2: PerlinNoiseSource)
  |
  +--> T005 -> T006 -> T007 -> T008        (Group 3: Aizawa)
  |
  +--> T009 -> T010                        (Group 4: SlowEventScheduler)
                |
   (2,3,4 all green)
                |
                +--> T011 [P]  T012 [P]  T013 [P]   (Group 5)
                                |
                                +--> T014 [P]  T015 [P]  -> T016 -> T017  (Group 6)
```

Groups 2, 3 and 4 are mutually independent once T001 lands and may be run in any order, but Group 5
needs all three green (T011 instantiates all three components; T013 renders all three).

## Coverage map

| Criterion | Task |
|---|---|
| SC-001, SC-002, SC-003(a)(b)(d), SC-004, SC-005 | T002 → T003 |
| SC-003(c) absolute floor + its injection check | T002 → T003 → **T004** |
| SC-006, SC-007 (in-TU), FR-036 positive test | T005 → T006 → T007 |
| SC-007 warning gate (GCC/Clang/MSVC) | **T008** |
| SC-008, SC-009, SC-010, SC-011, SC-012, SC-018 | T009 → T010 |
| SC-013 | T002/T003 (Perlin), T005/T007 (Aizawa), T009/T010 (scheduler) |
| SC-014 | **T011** |
| SC-017 | **T012** |
| SC-015, FR-081, FR-082 | T001 (macro) + **T013** |
| SC-016 | T014, T015, **T017** |
