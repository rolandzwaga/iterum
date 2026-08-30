# Tasks: Seraphis Phase 2 — Harmonic Cloud Oscillator

**Spec:** `specs/seraphis-phase2-harmonic-cloud/spec.md`
**Plan:** `specs/seraphis-phase2-harmonic-cloud/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` lines 137–164
**Layer:** ONE new component at **Layer 3** — `dsp/include/krate/dsp/systems/harmonic_cloud.h`. May include
Layer 0 (`core/`), Layer 1 (`primitives/`), Layer 2 (`processors/`) and stdlib **only**. No Layer 3/4 include.
**Test target:** `dsp_systems_tests` (`build/windows-x64-release/bin/Release/dsp_systems_tests.exe`).
**Plugin work:** none. Nothing outside `dsp/` changes.

---

## Conventions every task follows

- **Canonical order inside each task:** write the failing test FIRST → implement to make it pass → fix ALL
  compiler warnings (zero-warnings gate) → run the target's tests and confirm green. No task is complete with
  a warning outstanding.
- **CMake is registered once, in the last group (T020).** The three new test TUs are *not* in
  `dsp/tests/CMakeLists.txt` until then — the list is explicit, there is no GLOB
  (`dsp/tests/CMakeLists.txt:18-19`), so an unregistered file silently drops from the build. To iterate before
  T020, an executor may **temporarily** append only its own test-file line to the `dsp_systems_tests` source
  list (`dsp/tests/CMakeLists.txt`, after `unit/systems/sympathetic_resonance_test.cpp` at line 331) and
  **must revert that edit** before finishing the task. T020 is the single authoritative CMake change.
- **File map (all new except where noted):**
  | Path | Role |
  |---|---|
  | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | the component (**shared** — any task editing it is sequential) |
  | `dsp/tests/unit/systems/harmonic_cloud_test.cpp` | bulk of the criteria (**shared**) |
  | `dsp/tests/unit/systems/harmonic_cloud_spectral_test.cpp` | FFT-heavy criteria: SC-003, SC-010, SC-011, SC-014 (**shared**) |
  | `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp` | SC-007, both `[.perf]` cases (**shared**) |
  | `dsp/tests/CMakeLists.txt` | EXISTING — edited once, T020 |
  | `specs/seraphis-phase2-harmonic-cloud/spec.md` | EXISTING — edited once, T001 |
- **Test TU header block** (copy verbatim into each new test TU; `test_helpers` headers are on the include path
  as bare names — verified `dsp/tests/unit/effects/shimmer_delay_test.cpp:1164`,
  `dsp/tests/unit/core/chebyshev_test.cpp:18`):
  ```cpp
  #include <krate/dsp/systems/harmonic_cloud.h>
  #include <catch2/catch_test_macros.hpp>
  #include <catch2/catch_approx.hpp>
  using Catch::Approx;
  using namespace Krate::DSP;
  ```
- **`TestUtils::`-qualify every test-helper symbol.** `harmonic_cloud.h` pulls `Krate::DSP::detail`
  (via `smoother.h` → `core/db_utils.h:39`); `spectral_analysis.h` opens `Krate::DSP::TestUtils::detail`
  (`:189`). A TU with both `using namespace Krate::DSP;` and `using namespace Krate::DSP::TestUtils;` makes the
  bare name `detail` a hard clang error. Write `TestUtils::detail::sumBinPower(...)`, `TestUtils::ClickDetector`,
  `TestUtils::fingerprintRender`, `TestHelpers::AllocationDetector`, never the unqualified form.
- **Never `std::isnan` / `std::numeric_limits<float>::infinity()`** — the macOS leg builds `-ffast-math`.
  Component guards use `detail::isNaN` / `detail::isInf` (`core/db_utils.h:54,174`, `std::bit_cast` on the
  exponent field). Tests build NaN/±Inf from bit patterns through a `volatile` sink. **No
  `-fno-fast-math` source property** is added for these files (Phase-1 precedent: `brownian_drift_test.cpp` is
  absent from `dsp/tests/CMakeLists.txt:385-647`).
- **No bit-exact float goldens** (roadmap line 486). Pinned renders use `render_fingerprint.h`:
  `TestUtils::fingerprintRender(std::span<const float>)`, `TestUtils::compareFingerprints(actual, reference)`
  → `.withinTolerance()`, `.worstMetricRelativeError`; `kSampleTolerance = 1.0e-4f`,
  `kMetricTolerance = 1.0e-5` (verified `tests/test_helpers/render_fingerprint.h:46-101`).
- **Designated initialisers for every aggregate** (`ClickDetectorConfig` etc.) — Clang errors on narrowing in
  brace init.
- **Verify command** (every task unless it says otherwise):
  ```bash
  CMAKE="/c/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
  build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5
  ```
  Catch2 prints `All tests passed (N assertions in M test cases)` on the last line — do not grep, do not re-run
  to look at output.
- **ODR:** the sweep is already done and clear (plan §1.1): `HarmonicCloud`, `PartialDriftBank`, `DriftBank`,
  `CloudEnvelope`, `PartialEnvelope`, `CloudPartialState` → 0 hits in `dsp/ plugins/ tools/`. `HarmonicCloud`
  is the ONLY new top-level name; `LaneRng`/`DriftLanes` are private nested structs. Any *additional* class or
  struct a task wants to introduce must be re-swept (`grep -rn "class <Name>\|struct <Name>" dsp/ plugins/`)
  before it is written.
- **No commit tasks** — commits happen outside this workflow.

### Reused signatures — quote these, never invent (all read from the header, cited by the plan)

| Symbol | Header:line | Signature / fact |
|---|---|---|
| `processMcfBatchSIMD` | `processors/harmonic_oscillator_bank_simd.h:33-46` | Free function, **not a class**: `void processMcfBatchSIMD(float* sinState, float* cosState, const float* epsilon, const float* detuneMultiplier, float* currentAmplitude, const float* targetAmplitude, const float* antiAliasGain, const float* panLeft, const float* panRight, float ampSmoothCoeff, int numPartials, float& sumL, float& sumR) noexcept`. Header includes only `<cstddef>` (`:19`). Uses `hn::LoadU`/`hn::StoreU` (unaligned) so any `numPartials` is safe. **It ACCUMULATES** (`sumL += outSumL`, `..._simd.cpp:182-183`) — the caller must zero `sl`/`sr` every sample. Internally: `amp += coeff*(target*aa − amp)`; `epsEff = clamp(eps*detune, ±1.99)`; `sNew = s + epsEff*c`, `cNew = c − epsEff*sNew`. |
| `BrownianDrift` | `processors/brownian_drift.h:94` | `prepare(double)` `:121`, `reset()` `:133`, `setSeed(uint32_t)` `:145`, `setSmoothness(float)` `:152`, `setDepth(float)` `:159`, `processBlock(size_t)` `:194-206`, `getCurrentValue()` → `clamp(smoother, ±1)` `:212-214`. Constants `kTauMin=0.2f` `:97`, `kTauMax=30.0f` `:99`, `kInternalStd=0.5f` `:101`, `kDriftOutputSmoothMs=150.0f` `:103`, `kControlRateInterval=32` `:105`, `kWalkLimit=4.0f` `:226`, `kDenormalFloor=1e-20f` `:228`. **Included by exactly one test TU** (T009). |
| `equalPowerGains` | `core/crossfade_utils.h:50-53` | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` → `cos(position·kHalfPi)` / `sin(position·kHalfPi)`. Contract `:41`: **"Does NOT clamp position"** — domain is **[0,1]**. |
| `Xorshift32` | `core/random.h:40` | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` `:44` (substitutes `kDefaultSeed` for 0). `nextFloat()`→[−1,1] `:58`, `nextUnipolar()`→[0,1] `:66`, `seed(uint32_t)` `:72`. |
| `OnePoleSmoother` | `primitives/smoother.h:134` | `configure(float ms, float sr)` `:160`, `setTarget(float)` `:170`, `getCurrentValue()` `:191`, `advanceSamples(size_t)` `:243-254`, `snapTo(float)` `:263`. |
| `calculateOnePolCoefficient` | `primitives/smoother.h:77-93` | `constexpr float calculateOnePolCoefficient(float smoothTimeMs, float sampleRate) noexcept` — the single source of truth for one-pole coefficients. |
| `kCompletionThreshold` | `primitives/smoother.h:55` | `inline constexpr float kCompletionThreshold = 0.0001f;` — namespace scope. **Use this symbol; never redeclare it.** |
| `semitonesToRatio` / `ratioToSemitones` | `core/pitch_utils.h:23,31` | `pow(2, s/12)` / `12·log2(ratio)` (returns 0 for `ratio ≤ 0`). **`frequencyToCentsDeviation` (`:175`) is NOT usable** — it wraps to [−50,+50] cents against the nearest chromatic note. |
| `detail::isNaN` / `isInf` / `flushDenormal` | `core/db_utils.h:54,174,167` | `-ffast-math`-safe bit-pattern predicates. |
| `ClickDetectorConfig` / `ClickDetector` | `tests/test_helpers/artifact_detection.h:38,99` | Fields `sampleRate`(default 44100), `frameSize`, `hopSize`, `detectionThreshold`, `energyThresholdDb`, `mergeGap`. `ClickDetector(const ClickDetectorConfig&)`, `prepare()`, `[[nodiscard]] std::vector<ClickDetection> detect(const float* audio, size_t numSamples) noexcept` `:130`. Threshold is a **within-frame** `mean(|dx|) + detectionThreshold·stddev(|dx|)` (`:186-193`) — hence every click criterion is **differential**, never "0 detections". `LPCDetector` (`:306`) and `SpectralAnomalyDetector` (`:534`) are NOT used. |
| `AllocationDetector` | `tests/test_helpers/allocation_detector.h:26` | `TestHelpers::AllocationDetector::instance().startTracking()` / `.stopTracking()` → `size_t`. **Do NOT use `AllocationScope`** — it assigns its count in its *destructor* (`:75-95`), so reading it in scope always yields 0 and out of scope does not compile. The global `new`/`delete` replacements live in `allocation_operator_overrides.h`, already included by `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388` for this binary — **no new TU may include it** (duplicate-symbol link error). Idiom precedent: `selectable_oscillator_test.cpp:418-422`. |
| `frequencyToBin` / `calculateAliasedFrequency` / `detail::sumBinPower` | `tests/test_helpers/spectral_analysis.h:40,58,207` | `getAliasedBins` (`:168-183`) and `AliasingTestConfig` (`:112-118`) are **deliberately unused** — they enumerate fold-back bins for integer harmonics of one fundamental and would compute bins this component never folds into. |
| `FFT` | `primitives/fft.h:147,186,255` | `prepare(size_t)` validates power-of-two only; `kMaxFFTSize = 8192` (`:47`) is **documentary, not enforced** — pffft handles 65536. Always `REQUIRE(fft.isPrepared())` after `prepare(65536)`. |
| `generateBlackmanHarris` | `core/window_functions.h:179` | `inline void generateBlackmanHarris(float* output, size_t size) noexcept`. |
| FTZ/DAZ | `dsp/tests/dsp_test_main.cpp:13` | `enableFTZDAZ()` runs before any case → the *process* flushes denormals. Any denormal assertion must be about the component's own arithmetic (exact `0.0f`), never about observing a denormal. |

---

## GROUP 1 — Spec amendment (BLOCKING: nothing else starts)

### T001 — Apply plan §12's spec amendments

**Edit (existing, shared):** `specs/seraphis-phase2-harmonic-cloud/spec.md`. No code, no tests.

**Why this blocks everything.** Plan §6.1 measured, on the reference machine, that the literal FR-031/FR-032
design — 128 `BrownianDrift` objects, `processBlock(64)` per chunk — costs **44,402 ns per 512-sample block**
against SC-007's own baseline gate of **35,533 ns** (1.80× over), while the structure-of-arrays lane bank
costs **9,426 ns**. FR-031, FR-032, FR-035 and FR-072 name the class and its methods explicitly, so the
shipped design would have to be recorded as **four unmet FRs** in a Completion-Honesty compliance table. The
amendment restates them *behaviourally* so the requirement is what it always meant — the recurrence, not the
class identity.

**Apply, verbatim, plan §12.1 and §12.2:**

1. **FR-031** — replace the first sentence with plan §12.1's replacement paragraph (each partial owns an
   independent detune **Ornstein–Uhlenbeck drift lane** whose recurrence, coefficients and clamps are exactly
   `BrownianDrift`'s, spelling out `τ = kTauMin + smoothness·(kTauMax − kTauMin)`, `a = exp(−Δt/τ)`,
   `g = kInternalStd·sqrt(max(1−a²,0))`, the **three explicitly sequenced** `Xorshift32::nextFloat()`
   Irwin-Hall increment, `x ← clamp(a·x + g·z, ±kWalkLimit)` with the `kDenormalFloor` flush, output target
   `clamp(depth·x, ±1)`, and a **150 ms one-pole output smoother advanced exactly as
   `OnePoleSmoother::advanceSamples` advances it (`smoother.h:243-254`) — including its `isComplete()` skip,
   its `detail::flushDenormal`, and its hard snap to target below `kCompletionThreshold = 1e-4f`**), plus the
   trailing paragraph carrying the 44,402 / 35,533 / 9,426 ns measurement and naming
   `HarmonicCloud_DriftLaneMatchesBrownianDrift` as the verification clause.
2. **FR-032** — replace only the two method-name clauses with §12.1's "advanced by exactly `chunkLength`
   samples … read exactly once" wording. **Everything else in FR-032 stands verbatim**: the
   `ceil(numSamples / 64)` read-cadence clause, the "why chunked rather than literally per block" paragraph,
   the "2 internal OU steps, not one" statement, and the partition-invariance statement.
3. **FR-035** — replace `"via setSmoothness/setDepth (brownian_drift.h:152,159)"` with §12.1's
   "one cloud-level smoothness value feeds every detune lane's `τ`, and one cloud-level depth feeds the applied
   cents bound, with no per-partial API call in the audio path (the semantics of
   `BrownianDrift::setSmoothness`/`setDepth`)". The mutation-bank independence clause stands verbatim.
4. **FR-072** — replace *"a second, dedicated per-partial `BrownianDrift` bank"* with *"a second, dedicated
   per-partial drift bank with the FR-031 lane behaviour"*, and *"64 further instances"* with *"64 further
   lanes"*. **All four sub-bullets stand verbatim** — they are the independence contract SC-016 measures.
5. **SC-014** — in the *Law check* paragraph, replace the tabulated **−37.4 dB** with **−30.6 dB**
   (`p(0.75) = 1.125`, `N = round(64^0.75) = 23`, `23^(−1.125) = 0.0294` = −30.6 dB), citing plan D7. The other
   three figures (−22.7, −31.6, −18.1 dB) are correct and unchanged. **No threshold moves** — all four remain
   far above the −60 dB audibility floor.

**Verify:** re-read the four FRs and SC-014 in `spec.md`; each reads as §12 specifies; FR-032's read-cadence,
chunk-rationale, 2-OU-step and partition-invariance clauses are still present word-for-word; the amended
FR-031 names `HarmonicCloud_DriftLaneMatchesBrownianDrift`.

**If the amendment is refused:** do **not** start T009 on the SoA design. Take plan §6.4's alternative branch
(keep 128 `BrownianDrift` objects and memoise `coeff^N` inside `OnePoleSmoother::advanceSamples` — an 8-byte,
behaviour-preserving Layer-1 change, projected ≈43,600 ns/block) and renegotiate SC-007's
`kRegressionFactor` / `kReferenceNsPerBlock` in the spec first. Either branch is a spec edit; there is no
branch that proceeds without one.

---

## GROUP 2 — Skeleton, estimator, guard paths

### T002 — `HarmonicCloud` skeleton + frequency estimator + guard paths

**Create (new):**
- `dsp/include/krate/dsp/systems/harmonic_cloud.h`
- `dsp/tests/unit/systems/harmonic_cloud_test.cpp`

**FRs:** FR-001, FR-002, FR-003, FR-004, FR-008, FR-012, FR-024. **Plan:** §1.1–§1.5, §4.1, §5, §7.2.

**Write the failing test first** — `harmonic_cloud_test.cpp`, with the conventions header block, plus
`#include <krate/dsp/processors/harmonic_types.h>` (the R2 collision probe) and `<array> <cmath> <complex>
<cstdint> <vector>`:

- **R2 ODR compile guard** (file scope, no `TEST_CASE`):
  ```cpp
  static_assert(HarmonicCloud::kMaxPartials == 64, "class-scoped");
  static_assert(Krate::DSP::kMaxPartials == 96, "namespace-scoped analysis constant, unchanged");
  ```
  `Krate::DSP::kMaxPartials = 96` is `inline constexpr` at **namespace** scope (`processors/harmonic_types.h:21`).
  If `harmonic_cloud.h` declares its own at namespace scope this TU fails to compile — which is the point.
- **`TEST_CASE("HarmonicCloud_FrequencyEstimatorResolution")`** — validates the shared estimator all frequency
  criteria depend on. Define, in an **anonymous namespace** at the top of the TU, the two-stage heterodyne
  phase-slope estimator (plan §7.2):
  ```
  estimateFrequency(const float* x, size_t N, double fs, double fRef) -> double
    stage 1: M = 4096;   stage 2: M = N/2 with fRef ← stage-1 result
    per stage, Hann window w[0..M-1], DOUBLE accumulators:
      S1 = Σ_{n<M} w[n]·x[n]     · exp(-i·2π·fRef·n/fs)
      S2 = Σ_{n<M} w[n]·x[M+n]   · exp(-i·2π·fRef·(M+n)/fs)
      Δφ = wrapToPi(arg(S2·conj(S1)));   f = fRef + Δφ·fs/(2π·M)
  ```
  Assertions: synthesize **double-precision** reference sinusoids at 55.0, 440.0, 3520.0 and 7040.0 Hz,
  10 s at 48 kHz, cast to `float`; for each,
  `REQUIRE(std::abs(100.0 * ratioToSemitones(float(fEst / fTrue))) < 0.001)` — i.e. **< 0.001 cent**, 100×
  finer than SC-001's 0.1 cent threshold, which is what documents the estimator resolution SC-001 demands.
  (Unambiguous while `|f − fRef| < fs/(2M)` = ±5.86 Hz at stage 1, which brackets any plausible bug.)
- **`TEST_CASE("HarmonicCloud_GuardPaths")`** — clauses 1 and 2 only in this task (clause 3 is added by T003,
  which is the first task that renders audio):
  1. `prepare(48000.0)`; fill `L` and `R` (512 floats each) with a poison value `-12345.0f`; call
     `processStereoBlock(nullptr, R.data(), 512)`, `processStereoBlock(L.data(), nullptr, 512)` and
     `processStereoBlock(L.data(), R.data(), 0)`. `REQUIRE` **every** sample in both buffers still equals
     `-12345.0f`, and `REQUIRE(cloud.getDriftReadCount() == before)`.
  2. On a **default-constructed, un-`prepare`d** instance, render 512 samples into poisoned buffers →
     `REQUIRE` every output sample is exactly `0.0f` and no crash.

**Implement `harmonic_cloud.h`:**

- Includes, **exactly** (plan §1.1 — highest layer is 2, legal for Layer 3):
  `<krate/dsp/core/crossfade_utils.h>`, `<krate/dsp/core/db_utils.h>`, `<krate/dsp/core/math_constants.h>`,
  `<krate/dsp/core/pitch_utils.h>`, `<krate/dsp/core/random.h>`, `<krate/dsp/primitives/smoother.h>`,
  `<krate/dsp/processors/harmonic_oscillator_bank_simd.h>`, `<algorithm> <array> <cmath> <cstddef> <cstdint>`.
  **Never include `harmonic_oscillator_bank.h` or `harmonic_types.h`** — they drag in `kMaxPartials = 96`,
  `FilterDesign`, `BiquadCoefficients` and the analysis data model.
- Doc block: `/// @par Layer: 3 (systems/). Dependencies: Layer 0/1/2 + stdlib only.` and
  `/// @par Real-Time Safety: everything except prepare() is noexcept, allocation-free, lock-free.`
- **All constants `static constexpr` INSIDE the class** (plan §1.2), with the source cited per line:
  `kMaxPartials=64`, `kControlChunkSamples=64`, `kDriftControlInterval=32`, `kDriftTauMin=0.2f`,
  `kDriftTauMax=30.0f`, `kDriftInternalStd=0.5f`, `kDriftOutputSmoothMs=150.0f`, `kDriftWalkLimit=4.0f`,
  `kDriftDenormalFloor=1e-20f`, `kAmpSmoothTimeSec=0.002f`, `kAntiAliasFadeStart=0.8f`, `kMaxEpsilon=1.99f`,
  `kOutputClamp=2.0f`, `kTargetOscRms=0.5f`, `kMaxNormGain=20.0f`, `kNormGainSmoothMs=20.0f`,
  `kMinFundamentalHz=20.0f`, `kMaxFundamentalHz=4000.0f`, `kCrossfadeTimeSec=0.003f`, `kMaxInharmonicity=0.1f`,
  `kMinTiltDbPerOct=-12.0f`, `kMaxTiltDbPerOct=12.0f`, `kGravityExponentRange=0.1f`,
  `kRichnessMinExponent=3.0f`, `kRichnessMaxExponent=0.5f`, `kMaxMutationDepth=0.75f`,
  `kMutationSmoothness=0.5f`, `kDriftIndexExponent=1.0f`, `kMaxDriftCents=50.0f`, `kMinAttackSec=0.05f`,
  `kMaxAttackSec=30.0f`, `kMinDecaySec=0.05f`, `kMaxDecaySec=60.0f`, `kMaxEnvOffsetSec=2.0f`,
  `kQuiescenceAmplitude=1.0e-5f`, `kTailSilenceThreshold=1.0e-8f`.
  **Do NOT redeclare `kCompletionThreshold`** — use the shared `Krate::DSP::kCompletionThreshold`
  (`smoother.h:55`).
- **SoA state exactly per plan §1.3**, names pinned (`fadeStart_`, `invFadeRange_`, `tailHighWater_`,
  `freqDirty_`, `ampDirty_`, `mutationAmount_`, `detuneLanes_`, `mutationLanes_` — there is **no** member
  called `mutation_`). `alignas(32) std::array<float, kMaxPartials>` for every float array. `alignas(32)` is a
  locality choice, **never** an alignment assumption — the kernel uses `hn::LoadU`/`hn::StoreU`.
- **`struct LaneRng { Xorshift32 rng{1}; };`** and `std::array<LaneRng, kMaxPartials>` inside
  `struct DriftLanes`. `std::array<Xorshift32, N>{}` **does not compile** (`Xorshift32`'s only constructor is
  `explicit`, `random.h:44`; value-init of the elements is copy-init from `{}`). **Never transcribe
  `Xorshift32::next()`/`nextFloat()` into this header** — duplicating Layer-0 RNG internals would silently
  desynchronise the cloud's streams from `BrownianDrift`'s with no compile-time signal.
- **Full public API per plan §1.4** — every setter, getter, `noteOn`/`noteOff`, `setSeed`/`getSeed`,
  `processStereoBlock`, and the whole FR-008 + §1.5 accessor surface (`getActivePartialCount`,
  `getPartialFrequencyHz/CurrentAmplitude/TargetAmplitude/UnmutatedTargetAmplitude/AntiAliasGain/PanLeft/
  PanRight/Position/DriftDetune/SinState/CosState`, `getDriftLaneValue`, `getMutationLaneValue`,
  `getDriftReadCount`, `isQuiescent`, `stateFinite`, `setPartialPosition`, `setPartialMask`, `soloPartial`,
  `clearPartialMask`). Copy-deleted, move-defaulted. **In this task the bodies may return stored defaults** —
  later tasks fill them in — but every signature must be final so no later task changes the API shape.
  Every `const` accessor returns `0.0f` for `index >= kMaxPartials`; every mutator with an out-of-range index
  is a no-op.
- **`prepare(double)` per plan §5**: clamp `sampleRate_` to `> 1.0`; derive `nyquist_`, `invSampleRate_`,
  `fadeStart_ = kAntiAliasFadeStart·nyquist_`, `invFadeRange_ = 1/(nyquist_ − fadeStart_)`,
  `ampSmoothCoeff_ = 1 − exp(−1/(kAmpSmoothTimeSec·sr))`, `crossfadeLengthSamples_ = max(1, int(0.003·sr))`,
  `crossfadeThresholdRatio_ = semitonesToRatio(1.0f)`; `normGain_.configure(kNormGainSmoothMs, sr)`;
  build `smoothPowTable_[k] = pow(calculateOnePolCoefficient(kDriftOutputSmoothMs, sr), k)` for `k = 0..32`;
  then `reset()`; then `prepared_ = true`. Not RT-safe — the only non-RT method.
- **`reset()` per plan §5** (RT-safe): zero `currentAmplitude_`/`targetAmplitude_`/`envValue_`, fill
  `detuneMultiplier_` with `1.0f`, `envStage_` Idle, `gate_ = false`, both lane banks zeroed with
  `samplesUntilControl = 0`, `crossfadeRemaining_ = 0`, `lastOutL_/R_ = 0`, `driftReadCount_ = 0`,
  `positionOverridden_`/`masked_` false, `tailHighWater_ = activeCount_`; then `reseed()` (a stub in this
  task), then the four `recalculate*()` calls (stubs), then `freqDirty_ = ampDirty_ = false`, then
  `normGain_.snapTo(currentNormGainTarget())`. **`snapTo` appears ONLY here.**
- **`processStereoBlock` guard paths + quiescent early-out per plan §4.1**: `if (L == nullptr || R == nullptr
  || n == 0) return;` then `if (!prepared_) { fill both with 0; return; }` then the `isQuiescent()` early-out
  which advances both lane banks by `n` (stubs for now), does `driftReadCount_ += (n + 63) / 64`, zero-fills
  and returns. The chunked render loop body is T003's.
- `isQuiescent()` = `!gate_ && every currentAmplitude_[i] < kQuiescenceAmplitude for i < kernelCount_`.
- Parameter shadow defaults so FR-003 holds with no prior setter call: `fundamentalHz_ = 220.0f`,
  `richness_ = 0.5f`, everything else neutral (`inharmonicity_ = 0`, `tiltDb_ = 0`, `mutationAmount_ = 0`,
  `gravity_ = 0`, `driftCents_ = 0`, `driftSmoothness_ = 0.5f`, `stereoSpread_ = 0`, `attackSec_ = 0.05f`,
  `decaySec_ = 0.5f`, `offsetSpread_ = 0`).

**Verify:** `dsp_systems_tests` builds with **zero warnings**;
`dsp_systems_tests.exe "HarmonicCloud_FrequencyEstimatorResolution"` and `"HarmonicCloud_GuardPaths"` both pass.

---

## GROUP 3 — Frequency pipeline, render loop, seeded phases

### T003 — Frequency law, chunked render, anti-aliasing, random initial phase

**Edit:** `dsp/include/krate/dsp/systems/harmonic_cloud.h`, `dsp/tests/unit/systems/harmonic_cloud_test.cpp`.

**FRs:** FR-011, FR-013, FR-015, FR-016, FR-051, FR-052, FR-081, FR-083, FR-006. **Plan:** §2, §4.1, §4.6
(phases only). **SCs:** SC-001, SC-002.

**Write the failing tests first** (append to `harmonic_cloud_test.cpp`, reusing the T002 estimator):

- **`TEST_CASE("HarmonicCloud_PartialFrequencyAccuracyWithin0p1Cent")`** (SC-001). Drift, mutation, gravity,
  inharmonicity all 0; 48 kHz. The measured set is a **constraint, not a cross-product** — every measured
  partial must satisfy `f0·n < 0.8·Nyquist` (= 19,200 Hz), because FR-015 suppresses anything above that and a
  suppressed partial has no frequency to estimate:
  `n ∈ {1, 8, 32, 64}` at `f0 = 55`; `n ∈ {1, 8, 32}` at `f0 = 440`; `n ∈ {1, 8, 16}` at `f0 = 1000`.
  For each pair: `REQUIRE(f0 * n < 0.8f * 24000.0f)` (assert the constraint itself so the matrix cannot
  silently drift into the fade band); `cloud.soloPartial(n - 1)`; render **10 s**; mix down `0.5f*(L[i]+R[i])`;
  **skip the first 200 ms** so the FR-014 smoother has settled; then
  `REQUIRE(std::abs(100.0 * ratioToSemitones(float(fEst / (f0 * n)))) < 0.1)`.
- **`TEST_CASE("HarmonicCloud_InharmonicityFollowsPianoLaw")`** (SC-002). `B ∈ {0.01, 0.05, 0.1}`, `f0 = 110`,
  gravity 0, drift 0. Measure partial-by-partial via `soloPartial(n-1)` (same estimator, same 10 s render,
  same 200 ms skip), only for partials whose **expected** frequency `f0·n·sqrt(1+B·n²)` is `< 0.8·Nyquist`
  (assert that constraint). Assert
  `REQUIRE(std::abs(100.0 * ratioToSemitones(float(fEst / (f0*n*std::sqrt(1.0 + B*n*n))))) <= 1.0)` — 1 cent,
  which is ≥ the 0.001-cent estimator resolution proved in T002.
- **Append clause 3 to `HarmonicCloud_GuardPaths`**: `prepare(48000)`, `noteOn()`, render 512 samples
  immediately **with no setter call at all** → every sample finite by bit test
  (`REQUIRE(!detail::isNaN(s) && !detail::isInf(s))`) and block RMS `> 0.0f`. This is FR-003's "processing is
  well-defined with no prior parameter call".

**Implement:**

- **`recalculateFrequencies()` (config rate only, plan §2)**, for 1-based `n = 1 … kMaxPartials`:
  ```
  ratio_g(n) = pow(n, 1 + gravity_ · kGravityExponentRange)       // FR-081; exactly n at g = 0
  stretch(n) = sqrt(1 + inharmonicity_ · n²)                       // FR-051, additive_oscillator.h:472
  frequencyHz_[n-1] = fundamentalHz_ · ratio_g(n) · stretch(n)     // FR-083, THIS fixed order
  epsilon_[n-1] = clamp(2·sin(kPi · f_n · invSampleRate_), ±kMaxEpsilon)
  ```
  `ratio_g(1) = 1` exactly for every `g` — the fundamental never moves.
- **`recalculateAntiAliasing()` — per chunk**, because it depends on the drifting detune. Computed **without a
  `cos` call** via `cos(π f/fs) = sqrt(1 − (ε/2)²)`:
  ```
  epsEff = epsilon_[i] · detuneMultiplier_[i]           // the same product the kernel forms
  q      = 0.5f · epsEff
  corr   = (1 − q·q) > 0 ? sqrt(1 − q·q) : 0.0f
  fEff   = frequencyHz_[i] · detuneMultiplier_[i]
  fade   = fEff <= fadeStart_ ? 1.0f : (fEff >= nyquist_ ? 0.0f : (nyquist_ − fEff) · invFadeRange_)
  antiAliasGain_[i] = fade · corr
  ```
  This is **not** identical to the reference's `cos(π·f·detune/fs)` (`detune·sin θ ≠ sin(detune·θ)`); they
  agree only at `detune = 1`. It is deliberately the *more* correct target — it corrects for the orbit the
  kernel actually synthesizes, `asin(epsEff/2)` after the kernel's own `±1.99` clamp. Recorded as plan D5.
- **Chunked render loop, plan §4.1 verbatim.** `while (done < n)`, `chunk = min(64, n − done)`,
  `updateControl(chunk)`, then a per-sample loop that **zeroes `sl`/`sr` every sample** (the kernel
  ACCUMULATES: `sumL += outSumL`, `..._simd.cpp:182-183` — forgetting this gives a silently ramping DC output),
  calls `processMcfBatchSIMD(...)` with `static_cast<int>(kernelCount_)`, applies the FR-013 crossfade if
  armed, then `L[done+s] = std::clamp(sl, -kOutputClamp, kOutputClamp)` and the same for `R`. Update
  `lastOutL_/lastOutR_` at the end of each chunk.
- **`setFundamentalHz(float)`** — FR-007 idiom (T014 generalises it; write it correctly here): reject
  non-finite via `detail::isNaN`/`isInf`, clamp to `[kMinFundamentalHz, kMaxFundamentalHz]`, and — **at call
  time, not deferred** — `ratio = max(new/old, old/new); if (old > 0 && ratio > crossfadeThresholdRatio_) {
  crossfadeOldL_ = lastOutL_; crossfadeOldR_ = lastOutR_; crossfadeRemaining_ = crossfadeLengthSamples_; }`.
  The arming must be in the setter because it reads `lastOutL_/R_`; only the epsilon recompute is deferred.
  Snapshot **L and R separately** (plan D3), not the reference's mono `(L+R)/2`. Recomputing epsilon must
  **not** touch `sinState_`/`cosState_`.
- **`setInharmonicity(float)`** clamped `[0, kMaxInharmonicity]`; **`setSpectralGravity(float)`** clamped
  `[-1, +1]`. Both mark the frequency pipeline dirty (an unconditional recompute is acceptable in this task;
  T014 adds the no-op guard and dirty flags).
- **`redrawPhases()` (FR-016)**: `φ = phaseRng_.nextUnipolar() * kTwoPi; sinState_[i] = std::sin(φ);
  cosState_[i] = std::cos(φ);` — the shape at `harmonic_oscillator_bank.h:288-290` with a seeded φ.
  Called from `reset()` (via `reseed()`, which in this task seeds `phaseRng_` and redraws phases only).
  Document the distribution in the header. **`sinState_`/`cosState_` are seeded, never zeroed.**
- **Interim amplitude state so the render is non-silent** (T004 replaces it): `activeCount_ = kernelCount_ =
  kMaxPartials`, `baseAmplitude_[i] = 1.0f`, `unmutatedTarget_[i] = baseAmplitude_[i]`,
  `targetAmplitude_[i] = masked_[i] ? 0.0f : unmutatedTarget_[i]`, `panLeft_ = panRight_ = 0.70710678f`,
  gate ignored (env ≡ 1). Both frequency criteria measure one soloed partial at a time, so no normalization is
  required yet and the summed peak stays under `kOutputClamp`.
- `getPartialFrequencyHz`, `getPartialSinState`, `getPartialCosState`, `getPartialAntiAliasGain` now return
  real state.

**Verify:** zero warnings; `dsp_systems_tests.exe "HarmonicCloud_*"` — SC-001, SC-002 and all three
`GuardPaths` clauses green.

---

## GROUP 4 — Amplitude chain

### T004 — Richness, tilt, RMS normalizer, tail fade, masking

**Edit:** `harmonic_cloud.h`, and **create** `dsp/tests/unit/systems/harmonic_cloud_spectral_test.cpp`.

**FRs:** FR-014, FR-017, FR-041, FR-042, FR-043, FR-061, FR-062, FR-063, FR-008 (mask). **Plan:** §3.
**SC:** SC-014.

**Write the failing test first** — new TU `harmonic_cloud_spectral_test.cpp` with the conventions header block
plus `#include <krate/dsp/primitives/fft.h>`, `#include <krate/dsp/core/window_functions.h>`,
`#include <spectral_analysis.h>`:

- **`TEST_CASE("HarmonicCloud_RichnessAddsPartialsAndEnergy")`** (SC-014). `f0 = 110`, 48 kHz,
  drift/mutation/gravity/inharmonicity 0, `r ∈ {0, 0.25, 0.5, 0.75, 1.0}`, render ≥ 4 s, FFT magnitude at each
  partial bin. Assertions:
  1. `REQUIRE(cloud.getActivePartialCount() == expected)` with `expected = {1, 3, 8, 23, 64}` —
     `round(64^r)`, **exactly**.
  2. Per active partial, `|dB(n) − dB(1) − (−20·p(r)·log10(n))| ≤ 0.5` dB with `p(r) = 3.0 − 2.5·r`
     (referencing partial 1 removes the single FR-017 scalar gain, which cannot bend a slope).
  3. Metric (a): count of partials at or above the **audibility floor, −60 dB relative to the strongest
     partial** — **monotonically non-decreasing** across the five settings.
  4. Metric (b): above-fundamental energy fraction in dB, **floored at −80 dB** so the difference stays finite
     at `r = 0` — also monotonically non-decreasing.
  5. `REQUIRE(count(r=1) - count(r=0) >= 16)` and
     `REQUIRE(energyFractionDb(r=1) - energyFractionDb(r=0) >= 20.0f)`.
  Sanity anchors (the weakest active partial's rolloff, per T001's corrected spec table): −22.7, −31.6,
  **−30.6**, −18.1 dB at `r = 0.25 … 1` — all far above the −60 dB floor.

**Implement (plan §3), composition order fixed and documented in a header comment:**

```
a_i               = richnessRolloff(i) · tiltGain(i)                                  // config rate
gainTarget        = min(kTargetOscRms / sqrt(Σ_{i<N} a_i² · 0.5), kMaxNormGain)       // config rate
                    → normGain_.setTarget(gainTarget)   // LAST STATEMENT of recalculateAmplitudes()
gainSmoothed      = normGain_.getCurrentValue()                                       // per chunk
unmutatedTarget_i = gainSmoothed · a_i                                                // per chunk
targetAmplitude_i = unmutatedTarget_i · w_i · env_i    (w_i ≡ 1, env_i ≡ 1 for now)   // per chunk
```

- **Richness (FR-041):** `N(r) = clamp(round(pow(64.0f, r)), 1, 64)` → `activeCount_`;
  `p(r) = kRichnessMinExponent − 2.5f·r`; `a_n = pow(n, −p(r))` for `n ∈ [1, N]`, `a_n = 0` for `n > N`.
  `activeCount_` is **explicit state**, returned by `getActivePartialCount()` and passed as the kernel's
  `numPartials` — inactive partials cost no CPU.
- **Tilt (FR-061):** `tiltGain(n) = (tiltDb_ == 0.0f || n <= 1) ? 1.0f : pow(10.0f, tiltDb_·log2(n)/20.0f)`,
  the identity branch copied from `additive_oscillator.h:481-489`. `setSpectralTiltDb` clamps to `[−12, +12]`.
- **Normalizer (FR-017):** input is the `a_i` set **only** — mutation weights, drift and the envelope are
  explicitly **outside** it. That exclusion is a requirement, not a detail: recomputing from mutated
  amplitudes would cancel exactly the ±3 dB level movement SC-016 exists to observe. One scalar for all
  partials, so it cannot bend the SC-003 tilt slope.
  **`normGain_.setTarget(currentNormGainTarget())` MUST be the last statement of `recalculateAmplitudes()`.**
  Omitting it silently disables FR-017: `reset()` calls `snapTo`, after which `advanceSamples` early-returns
  on `isComplete()` (`smoother.h:244`) forever, freezing the gain for the life of the instance. The failure is
  quiet — no NaN, no clip, just a wrong level — and would surface as SC-016 and SC-006 failing for a reason no
  assertion names.
- **Per-chunk step 4:** `normGain_.advanceSamples(chunk)` **once** (not 64 times), then cache
  `gainSmoothed` for the next chunk.
- **Tail (FR-043, plan D4):** pass `kernelCount_ = max(activeCount_, tailHighWater_)` to the kernel and set
  `targetAmplitude_[i] = 0` for `i ≥ activeCount_`; the kernel's uniform `ampSmoothCoeff` fades departing
  partials **inside the SIMD path**. When every `currentAmplitude_[i]` for `i ∈ [activeCount_, tailHighWater_)`
  drops below `kTailSilenceThreshold`, lower `tailHighWater_` to `activeCount_` — checked **once per chunk**,
  never per sample.
- **Masking (FR-008):** `masked_[i] ⇒ targetAmplitude_i = 0`, applied at the **end** of the chain so the
  FR-014 smoother fades the partial and solo cannot click. Implement `setPartialMask`, `soloPartial`,
  `clearPartialMask`.
- `getPartialTargetAmplitude`, `getPartialUnmutatedTargetAmplitude`, `getActivePartialCount` now return real
  state.

**Verify:** zero warnings; `dsp_systems_tests.exe "HarmonicCloud_RichnessAddsPartialsAndEnergy"` green, and
`"HarmonicCloud_*"` still green (SC-001/SC-002 must survive the amplitude chain).

---

## GROUP 5 — Crest measurement, tilt law, onset incoherence

### T005 — Measure crest factor, pin seeds, then SC-003 + SC-018

**Edit:** `harmonic_cloud_spectral_test.cpp` (SC-003), `harmonic_cloud_test.cpp` (SC-018). Header only if the
measurement exposes a defect.

**FRs:** FR-016, FR-017, FR-061. **Plan:** §7.3. **SCs:** SC-003, SC-018.

**Step 1 — measure, do not assume.** Plan §7.3's crest table came from an idealised Node model. Re-measure on
the **real component** at both criteria's pinned configurations across a seed sweep (≥ 24 seeds, 0.2 s per
render, worst peak per rendered channel), and record the measured numbers in a comment beside each threshold —
the same treatment §6 gives the CPU budget. Modelled figures, for orientation only:

| Configuration | spread 0 | spread 1 | margin to 1.8 |
|---|---|---|---|
| SC-003, tilt −12 | 0.590 | 0.789 | 9.7 / 7.2 dB |
| SC-003, tilt 0 | 1.114 | 1.181 | 4.2 / 3.7 dB |
| SC-003, tilt +6 | 1.394 | 1.511 | 2.2 / **1.5 dB** |
| SC-003, tilt +12 | 1.436 | 1.479 | **2.0** / 1.7 dB |
| SC-018, 64 partials, tilt +12 | 1.249 | 1.354 | 3.2 / 2.5 dB |

The margin at +6/+12 dB/oct is inside seed variance (a 24-seed model run already produced a **mono peak of
2.031**, above `kOutputClamp` itself). Therefore **SC-003 pins its seed (`kSc003Seed`) and pins
`setStereoSpread(0.0f)`**. This is not a relaxed threshold — the `0.9 · kOutputClamp` bound is unchanged; only
the configuration is made reproducible, exactly as SC-003 already pins `f0`, richness, FFT size and window.
If a measured margin comes in below ~1.5 dB, pin the seed harder or widen the render window — **never** move
the 0.9 factor, which is spec text.

**Write the failing tests:**

- **`TEST_CASE("HarmonicCloud_TiltSlopeMatchesSetting")`** (SC-003, spectral TU). `f0 = 110`;
  `setRichness(5.0f/6.0f)` then `REQUIRE(getActivePartialCount() == 32)` (partial 32 at 3.52 kHz, far below the
  FR-015 fade band — a suppressed partial would bend the fit); `setSeed(kSc003Seed)`; `setStereoSpread(0.0f)`;
  drift/mutation/gravity/inharmonicity 0; 48 kHz; render ≥ 4 s; **FFT 65536 with a Blackman-Harris window**
  (`generateBlackmanHarris`, ≈ −92 dB sidelobes — needed because at −12 dB/oct partial 32 sits 60 dB below the
  fundamental), after `REQUIRE(fft.isPrepared())` (`kMaxFFTSize = 8192` is documentary, not enforced; without
  this a future tightening would analyse a zero-size spectrum and pass vacuously).
  **Precondition first:** `REQUIRE(peak < 0.9f * HarmonicCloud::kOutputClamp)` measured on the **rendered
  channel buffers** (the same samples the FFT consumes), with the measured margin in a comment. Then, for
  tilt ∈ {−12, −6, 0, +6, +12}, evaluated **differentially against the tilt-0 render of the identical
  configuration**:
  1. `REQUIRE(std::abs(slope(t) - slope(0) - t) <= 0.5f)` dB/oct;
  2. `REQUIRE(std::abs(slope(0) - (-5.52f)) <= 0.5f)` dB/oct — this pins the FR-041 rolloff itself
     (`−20·p·log10(2)` at `p = 0.9167`);
  3. per partial, `REQUIRE(std::abs(dB(t,n) - dB(0,n) - t*log2(n) - c) <= 0.5f)` dB, `c` a single fitted
     scalar offset.
- **`TEST_CASE("HarmonicCloud_OnsetIsPhaseIncoherent")`** (SC-018, main TU). Richness 1 (64 partials), tilt
  +12, over an **explicitly enumerated `constexpr std::array kSc018Seeds{…}` of ≥ 8 seeds**, with the measured
  worst channel peak over that array recorded beside the threshold. Per seed:
  1. `REQUIRE(peakFirst100ms <= 0.9f * HarmonicCloud::kOutputClamp)` on the rendered channels — this is the
     measurement of FR-017's 11.1 dB crest headroom above the 0.5 target RMS, which the expected-RMS basis
     *leaves* rather than guarantees;
  2. onset peak-to-RMS over that window `≤` steady-state peak-to-RMS `+ 6 dB`;
  3. across the seed array the measured onset peak **varies** (`stddev > 0`, and `max − min ≥` a documented
     fraction of the mean) — a fixed onset peak across seeds means the phases are not actually seeded.
  A phase-0 (coherent) implementation sums all partials in phase at `t = 0` and fails all three.

**Implement:** normally nothing — T003 and T004 already provide the behaviour. If a criterion fails, fix the
**component**, not the threshold.

**Verify:** zero warnings; both new cases green; `"HarmonicCloud_*"` still green.

---

## GROUP 6 — Spectral gravity

### T006 — Gravity warp and its composition with Inharmonicity

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs:** FR-081, FR-082, FR-083, FR-084. **Plan:** §2. **SC:** SC-004.

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_GravityMapsMonotonically")`** (SC-004), four parts, all using the T002 estimator
  with `soloPartial(n-1)` and subject to the `f0·ratio < 0.8·Nyquist` constraint (assert it):
  1. **Zero setting.** At `g = 0`, `|ratio − n|` within the SC-001 tolerance for every measured partial.
  2. **Monotonicity.** `g ∈ {−1, −0.5, 0, +0.5, +1}` with `B = 0`: the mean `|ratio − n|` over partials
     `n ≥ 2` is **strictly increasing in |g|**, each step increasing it by **at least 5× the SC-001 estimator
     tolerance** so the ordering cannot come from measurement noise.
  3. **Sign, restricted.** For every measured partial with **`n ≥ 2`** whose `|ratio − n|` at `|g| = 1`
     exceeds the estimator tolerance, `sign(ratio − n)` at `+g` is opposite to its sign at `−g`.
     **Partial `n = 1` is excluded** — `ratio_g(1) = 1` for every `g`, so its deviation is identically 0 and a
     universal clause would be unsatisfiable by a correct implementation.
  4. **Composition order.** With `B = 0.05` and `|g| = 1`, each measured partial frequency matches
     `f0 · n^(1 + g·0.1) · sqrt(1 + B·n²)` within the SC-002 tolerance (≤ 1 cent). **This is what pins
     FR-083's order** — without it the order is documented, not measured.

**Implement:** `setSpectralGravity` (already stubbed in T003) is now exercised; confirm
`recalculateFrequencies()` applies `pow(n, 1 + gravity_·kGravityExponentRange)` **before** the inharmonicity
stretch multiplies it, and that a gravity change recomputes epsilon **without resetting phase accumulators**
(FR-084, same mechanism as FR-053). No gravity setting cancels a non-zero `B` — that is a recorded deviation
(spec Assumption 4), not a defect.

**Verify:** zero warnings; `"HarmonicCloud_GravityMapsMonotonically"` green; `"HarmonicCloud_*"` still green.

---

## GROUP 7 — Anti-aliasing at the extremes

### T007 — SC-011

**Edit:** `harmonic_cloud_spectral_test.cpp` only. No header edit expected.

**FR:** FR-015. **SC:** SC-011.

**Write the failing test:**

- **`TEST_CASE("HarmonicCloud_NoAliasingAtExtremes")`** — the worst case for aliasing: `f0 = 4000` (the FR-013
  maximum), 64 partials, `B = 0.1`, `|g| = 1`, **44.1 kHz**. Measurement is computed from the cloud's **own**
  partial frequencies, never from a harmonic-series helper:
  1. for each partial read `getPartialFrequencyHz(i)`;
  2. if `f_i > nyquist`, fold with `TestUtils::calculateAliasedFrequency(f_i, 1, fs)` — harmonic number **1**,
     so it folds the *actual* frequency rather than `f0·n` (the helper multiplies internally,
     `spectral_analysis.h:58-78`);
  3. map with `TestUtils::frequencyToBin` (`:40`) and sum with **`TestUtils::detail::sumBinPower`** (`:207`,
     `detail` namespace opened at `:189` — the `TestUtils::` qualification is mandatory, not stylistic; the
     bare name is ambiguous against `Krate::DSP::detail`), **excluding bins within ±2 bins of any legitimately
     synthesized sub-Nyquist partial**;
  4. `REQUIRE(aliasDb - fundamentalDb <= -60.0f)`.
  `getAliasedBins` / `AliasingTestConfig` are **deliberately unused** — they enumerate fold-back bins for
  integer harmonics of one fundamental (and carry a `driveGain` field, default `maxHarmonic = 10`); at this
  criterion's own worst case FR-051 and FR-081 make the partial frequencies non-integer multiples of `f0`, so
  they would compute bins where this component's partials never fold and would pass a genuinely aliasing build.
  The threshold may be **tightened** if measurement shows more headroom; it may only be loosened with a
  documented measurement.

**Implement:** normally nothing (T003 shipped the FR-015 fade + MCF correction + epsilon clamp). If it fails,
fix `recalculateAntiAliasing()` — the likeliest defect is computing the fade on the **undetuned** frequency.

**Verify:** zero warnings; `"HarmonicCloud_NoAliasingAtExtremes"` green.

---

## GROUP 8 — Pan, spread, seeding

### T008 — Equal-power pan, seeded scatter, `deriveSeed`, full draw order

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs:** FR-005, FR-008 (`setPartialPosition`), FR-021, FR-022, FR-091, FR-092, FR-093. **Plan:** §4.3, §4.6.
**SC:** SC-012 (+ the seed-distinctness and reset clauses of SC-009).

**Write the failing tests first:**

- **`TEST_CASE("HarmonicCloud_EqualPowerPanAndSpread")`** (SC-012). Measured on **the shipped conversion
  path** — the test **must not re-implement the pan law**, because the bug FR-091 documents lives in the
  component's own position→gain conversion. Over the pinned grid **{−1, −0.5, 0, +0.5, +1}** via
  `setPartialPosition(i, p)`, then read back `getPartialPanLeft(i)` / `getPartialPanRight(i)`:
  1. `REQUIRE(std::abs(L*L + R*R - 1.0f) <= 1e-6f)` (equal power);
  2. `REQUIRE(L >= 0.0f); REQUIRE(R >= 0.0f)` at every position (**no polarity inversion**);
  3. `L` strictly **decreasing** and `R` strictly **increasing** across the grid;
  4. `REQUIRE(std::abs(L - R) <= 1e-6f)` at position 0 (centre is centre).
  Clauses 2–4 exist because clause 1 alone cannot discriminate a correct pan law from the domain-mismatch bug:
  feeding a bipolar position into `equalPowerGains` gives `(L, R) = (0, −1)` at `pos = −1`, for which
  `|0 + 1 − 1| = 0` **passes clause 1** while the right channel is full-level and phase-inverted.
  **Spread behaviour**, on a freshly seeded cloud with no override in effect: at spread 0,
  `max|L[i] − R[i]| ≤ 1e-7` over a full render; across ≥ 4 increasing spreads the inter-channel Pearson
  correlation of the rendered output decreases **strictly monotonically**.
- **`TEST_CASE("HarmonicCloud_SeedDerivationIsDistinct")`** (T-SEED-DISTINCT, FR-005 / Edge Cases) — all
  **128** derived seeds pairwise distinct for seeds `{0, 1, 0xFFFFFFFF, 12345}`. Seed 0 must not collapse:
  `Xorshift32`'s constructor substitutes a default for 0 (`random.h:44-45`), so `deriveSeed` must never *hand*
  a lane 0 rather than relying on that substitution to fix collisions.
- **`TEST_CASE("HarmonicCloud_ResetReproducesSeededState")`** (SC-009, partial — the drift/mutation half lands
  in T017). With `setStereoSpread(0.7f)`: render N blocks, fingerprint; `reset()`; render N blocks again →
  `REQUIRE(compareFingerprints(second, first).withinTolerance())`.

**Implement:**

- **`recalculatePan()` (config rate), plan §4.3:**
  ```cpp
  panPosition_[i] = positionOverridden_[i] ? panPosition_[i] : stereoSpread_ * positionScatter_[i];
  const float p01 = std::clamp((panPosition_[i] + 1.0f) * 0.5f, 0.0f, 1.0f);   // MANDATORY remap
  equalPowerGains(p01, panLeft_[i], panRight_[i]);
  ```
  **The remap is not optional.** `equalPowerGains` does not clamp and its domain is [0,1]
  (`crossfade_utils.h:41`). `setStereoSpread` **clears every `positionOverridden_` flag** (FR-008: the override
  lasts "until the next spread change, re-seed or `reset()`").
- **`setPartialPosition(index, position)`** — FR-007 guard, no-op for `index >= kMaxPartials`, clamp position
  to `[−1, +1]`, set `positionOverridden_[index] = true`, recompute **only that partial's** pan pair. This is
  the committed Phase-7 per-voice azimuth hook.
- **`reseed()`, plan §4.6 — the draw order is fixed and documented, because `reset()` must reproduce all of it
  exactly:**
  ```
  configRng_.seed(configuredSeed_);
  phaseRng_.seed(deriveSeed(configuredSeed_, 0xF0F0u));
  for i in 0..63:  positionScatter_[i] = configRng_.nextFloat();                    // FR-021, already [-1,1]
  for i in 0..63:  u_i  = 0.5f + 0.5f * configRng_.nextUnipolar();                  // FR-022, [0.5,1.0]
  for i in 0..63:  oa_i = configRng_.nextUnipolar();                                // FR-023 attack offset
  for i in 0..63:  od_i = configRng_.nextUnipolar();                                // FR-023 decay offset
  for i in 0..63:  detuneLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i));
  for i in 0..63:  mutationLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i + 64));
  redrawPhases();                                                                   // from phaseRng_
  ```
  Phase redraws use a **separate stream** so a retrigger never perturbs the once-per-seed draws.
- **`deriveSeed`** exactly as plan §4.6 (lowbias32 finaliser, `return h != 0u ? h : 0x2545F491u;`).
- **`driftAmount_[i] = pow(float(i + 1) / float(kMaxPartials), kDriftIndexExponent) * u_i`** (FR-022) — the
  **fixed** capacity in the denominator, not `activeCount_`, so changing Richness does not re-scale existing
  partials' drift. `driftAmount_[i] ≤ 1` is what makes `driftCents_` a true upper bound (SC-015 clause 3a).
- `setSeed(uint32_t)` stores `configuredSeed_` and calls `reseed()`; `getSeed()` returns it.
- `getPartialPanLeft/PanRight/Position` now return real state.

**Verify:** zero warnings; the three new cases green; `"HarmonicCloud_*"` still green (SC-018's onset peak is
sensitive to pan — re-check it).

---

## GROUP 9 — Drift lanes + the equivalence gate

### T009 — SoA Ornstein–Uhlenbeck lanes, both banks

**Requires T001.** **Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs (as amended by T001):** FR-031, FR-032, FR-033, FR-034, FR-035, FR-072. **Plan:** §4.5, §6.4, §12.

**Write the failing test FIRST — it is the phase's honesty gate, and it must pass before SC-015 is attempted:**

- **`TEST_CASE("HarmonicCloud_DriftLaneMatchesBrownianDrift")`** — **the only test in this phase that includes
  `<krate/dsp/processors/brownian_drift.h>`.** Construct a real `BrownianDrift` with
  `setSeed(deriveSeed(S, i))`, `setSmoothness(s)`, `setDepth(1.0f)`, `prepare(48000.0)`. Drive it and the
  corresponding cloud lane through an **identical chunk schedule** (64-sample chunks, 60 s). Compare
  `BrownianDrift::getCurrentValue()` against **`cloud.getDriftLaneValue(i)`** —
  **not** `getPartialDriftDetune(i)`, which is a *frequency multiplier*: recovering `d_i` from it at index 0
  carries ≈ 2.6e-4 of float32 error (`driftAmount_[0] ∈ [0.0078, 0.0156]`, so at 50 cents the multiplier lies
  within 4.5e-4 of 1.0, one ULP is 1.19e-7, and `dcents/dmultiplier ≈ 1731`), which is **26× the tolerance**.
  `REQUIRE(std::abs(lane - reference) <= 1e-5f)` at **every chunk**, over **smoothness ∈ {0, 0.5, 1}** and
  over **both banks** (`getMutationLaneValue(i)` against a `BrownianDrift` at `kMutationSmoothness = 0.5f`,
  seed `deriveSeed(S, i + 64)`).
  **Measured achievable divergence with the §4.5 transcription: `0.000e+00` (bit-identical). A failure here
  means the transcription is incomplete — NEVER that the tolerance is wrong.** With the naive closed-form
  smoother the measured worst divergence is up to **1.64e-4**, first breaching 1e-5 at t = 1.17 s at
  smoothness 0.75.

**Implement (plan §4.5) — the math is `BrownianDrift`'s, verbatim, transposed to SoA:**

- Coefficients, per bank: `τ = kDriftTauMin + smoothness·(kDriftTauMax − kDriftTauMin)`;
  `a = exp(−Δt/τ)` with `Δt = kDriftControlInterval / fs`; `g = kDriftInternalStd·sqrt(max(1 − a², 0))`.
- Per control step, per lane:
  ```
  z0 = rng[i].rng.nextFloat(); z1 = rng[i].rng.nextFloat(); z2 = rng[i].rng.nextFloat();  // SEQUENCED, 3 draws
  x  = a·walk[i] + g·(z0 + z1 + z2);                       // Irwin-Hall, mean 0
  x  = clamp(x, ±kDriftWalkLimit);
  if (|x| < kDriftDenormalFloor) x = 0;
  walk[i] = x;
  smoothTgt[i] = clamp(depth · x, -1.0f, +1.0f);
  ```
- **The 150 ms output smoother — transcribe `OnePoleSmoother::advanceSamples`, do NOT write the exponential
  identity.** This is the single highest-risk line in the phase. The naive
  `smoothCur = smoothTgt + (smoothCur − smoothTgt)·coeff^k` is **not** what `BrownianDrift` does:
  `advanceSamples` (`smoother.h:243-254`) wraps that multiply in three further operations — an `isComplete()`
  early **return** that leaves `current_` **unchanged (not snapped)**, a `detail::flushDenormal`, and a
  post-advance **hard snap** to target below `kCompletionThreshold = 1e-4f`. That snap is a nonlinear,
  path-dependent step an order of magnitude larger than the 1e-5 tolerance, and a 150 ms pole chasing a target
  that reverses every few control steps crosses 1e-4 constantly. Per lane, exactly:
  ```cpp
  const float diff0 = bank.smoothCur[i] - bank.smoothTgt[i];
  if (std::abs(diff0) < kCompletionThreshold) continue;                 // smoother.h:244 — SKIP, do not snap
  bank.smoothCur[i] = bank.smoothTgt[i] + diff0 * smoothPowTable_[k];   // :247-249
  bank.smoothCur[i] = detail::flushDenormal(bank.smoothCur[i]);         // :250
  if (std::abs(bank.smoothCur[i] - bank.smoothTgt[i]) < kCompletionThreshold) {
      bank.smoothCur[i] = bank.smoothTgt[i];                            // :251-253
  }
  ```
  **A pre-multiply snap is a different function** and reintroduces divergence at exactly the points the gate
  measures. `kCompletionThreshold` is the shared `Krate::DSP` constant (`smoother.h:55`), never a copy.
- `smoothPowTable_[k] = pow(calculateOnePolCoefficient(kDriftOutputSmoothMs, float(sampleRate_)), k)` for
  `k = 0..32`, built in `prepare()`. **The coefficient MUST come from that shared free function**
  (`smoother.h:77-93`, which uses `detail::constexprExp`, not `std::exp`) — a hand-written
  `std::exp(-5000.0f/(T*sr))` drifts in the last bits and would put the gate near its tolerance for no reason.
- **`advanceDriftLanes(bank, numSamples)` — a verbatim structural copy of `BrownianDrift::processBlock`**
  (`brownian_drift.h:194-206`), with `samplesUntilControl` **shared across the bank's 64 lanes** (every lane
  advances by the same counts):
  ```cpp
  int remaining = static_cast<int>(numSamples);
  while (remaining > 0) {
      if (bank.samplesUntilControl <= 0) { bank.samplesUntilControl = kDriftControlInterval;
                                           advanceControlStepAllLanes(bank); }
      const int advance = std::min(remaining, bank.samplesUntilControl);
      bank.samplesUntilControl -= advance;  remaining -= advance;
      advanceSmootherAllLanes(bank, advance);
  }
  ```
  A 64-sample chunk therefore performs **2** internal OU steps, not one — FR-032 explicitly forbids collapsing
  them. This is what makes the lane state after N advanced samples depend only on N, not on the partition.
- **Configuration split (FR-035 / FR-072):**
  | | detune bank | mutation bank |
  |---|---|---|
  | smoothness | `driftSmoothness_` (cloud control) | `kMutationSmoothness = 0.5f` (fixed constant) |
  | depth | `1.0f` — the cents bound is applied by the cloud | `1.0f`, pinned |
  | seeds | `deriveSeed(seed, i)` | `deriveSeed(seed, i + kMaxPartials)` |
  `setDriftDepthCents` / `setDriftSmoothness` touch the **detune bank only**. `setDriftDepthCents(0)` must
  leave the mutation bank untouched.
- **Detune application (FR-033), in `updateControl(chunk)` step 3:**
  ```
  d     = clamp(detuneLanes_.smoothCur[i], -1.0f, 1.0f);     // == BrownianDrift::getCurrentValue, :212-214
  cents = driftCents_ · driftAmount_[i] · d;                 // |cents| ≤ driftCents_ since amount ≤ 1
  detuneMultiplier_[i] = semitonesToRatio(cents / 100.0f);
  → recompute antiAliasGain_[i]                              // per chunk, on the DETUNED frequency
  ```
  `epsilon_` does **not** change per chunk (FR-034) — the kernel forms `eps·detune` itself, so partial phase is
  continuous through a detune change and there is nothing to click.
- `++driftReadCount_` **once per chunk** (the count is *per partial*, not per lane), in `updateControl`.
- `getDriftLaneValue(i)` / `getMutationLaneValue(i)` return `std::clamp(bank.smoothCur[i], -1.0f, 1.0f)` — the
  **exact shape** of `BrownianDrift::getCurrentValue()`. `getPartialDriftDetune(i)` returns the frequency
  **multiplier**.
- `setDriftDepthCents` clamps `[0, kMaxDriftCents]`; `setDriftSmoothness` clamps `[0, 1]` and recomputes the
  detune bank's `a`/`g`.

**Verify:** zero warnings; **`"HarmonicCloud_DriftLaneMatchesBrownianDrift"` green first**, at full 1e-5
tolerance on both banks at all three smoothness settings; then `"HarmonicCloud_*"` still green.

---

## GROUP 10 — Drift criteria

### T010 — SC-015: independence, decimation, block-size invariance, bound

**Edit:** `harmonic_cloud_test.cpp` only. Header edits only if a clause exposes a defect.

**FRs:** FR-022, FR-031, FR-032, FR-033, FR-035. **SC:** SC-015.

**Write the failing test:**

- **`TEST_CASE("HarmonicCloud_DriftIsIndependentDecimatedAndBounded")`**, four clauses:
  1. **Independence (FR-031).** Log `getPartialDriftDetune(i)` once per block for **60 s at 48 kHz**. Mean
     pairwise Pearson correlation across all partial pairs `|r| ≤ 0.2`; no single pair `|r| > 0.5`. A single
     shared walk gives `r ≈ 1`. (Pearson is scale-invariant, so FR-022's per-partial `amount_i` cannot mask a
     shared walk.)
  2. **Decimation and block-size invariance (FR-032).** Render the same total `N` (a multiple of 512) three
     ways — one block of `N`; `N/512` blocks of 512; blocks of **577** (deliberately neither a multiple of
     `kControlChunkSamples = 64` nor of `kDriftControlInterval = 32`) — and assert:
     (a) each partial's final drift value agrees across all three within **1e-5** (the OU state depends only
     on total samples advanced, not on the partition);
     (b) `getDriftReadCount()` equals `Σ ceil(blockSize / 64)` for each schedule — `N/64`, `N/64`, and
     **10 per 577-sample block** — **not one per block**;
     (c) the **rendered output** of the single-block and 512-block schedules agrees within
     `render_fingerprint.h` tolerances. **This is the clause a literal one-read-per-block implementation fails
     outright** (its 512-sample schedule would hold detune 8× longer than its single-block schedule).
  3. **Bound and per-partial amount law (FR-033, FR-022),** 60 s at maximum drift depth, measuring per-partial
     detune in cents:
     (a) `max|cents| ≤ driftCents_` within the SC-001 estimator tolerance, and **exactly 0** at depth 0;
     (b) *liveness:* the maximum over all partials `≥ 0.25 × driftCents_` (a no-op or uniformly-tiny drift
     fails);
     (c) *index scaling:* mean `|detune|` over `n ∈ [33, 64]` is **≥ 4×** that over `n ∈ [1, 8]` (FR-022's law
     gives ≈ 10.8×, so the threshold discriminates rather than describes);
     (d) *seeded scatter:* the per-partial mean-`|detune|` sequence over `n = 1 … 64` is **not** monotonically
     increasing — it contains **≥ 1 inversion**, which a pure index-scaled law cannot produce.
  4. **Shared configuration (FR-035).** After one cloud-level `setDriftSmoothness` / `setDriftDepthCents`
     call, **all 64** partials respect the new bound (not a sample), and **neither setter altered the mutation
     bank** — re-read `getMutationLaneValue(i)` across the call and assert the bank's trajectory is unchanged.

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green.

---

## GROUP 11 — Mutation

### T011 — Mutation weights from the second lane bank

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs:** FR-071, FR-072, FR-073, FR-074. **Plan:** §3, §4.2. **SC:** SC-016.

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_MutationStaysBoundedAndLevelStable")`** (SC-016). Sample every partial's weight
  once per block as `w = getPartialTargetAmplitude(i) / getPartialUnmutatedTargetAmplitude(i)`, **starting
  after the documented attack time has elapsed with the gate held** so `env_i = 1` and the ratio is exactly
  `w_i`. 60 s renders, drift active, Mutation ∈ {0, 0.5, 1.0}:
  - per-partial weight extrema stay within **[0.25, 1.75]** at every setting (FR-073);
  - block RMS over any **1 s** window stays within **±3 dB** of the Mutation-0 RMS of the same configuration;
  - per-block `|Δw| ≤ 10 s⁻¹ × blockDuration` (the FR-071 rate bound from the 150 ms output smoother:
    `2 · kMaxMutationDepth / 0.150 s ≈ 10 s⁻¹`);
  - at Mutation 0, every `w` is **exactly `1.0f`**.
  **Independence from Drift (the point of the second bank):**
  1. **Drift depth 0, Mutation 1.0** — weights must still move: `max|w_i − 1| ≥ 0.1` for **at least half** the
     active partials, and the ±3 dB level bound still holds. *A shared-instance implementation freezes every
     weight at exactly 1.0 here and fails.*
  2. **Drift depth max, Mutation 0** — every `w` is **exactly 1.0** while `getPartialDriftDetune(i)` moves,
     confirming the drift bank does not leak into the amplitude path.
  Plus, per partial, Pearson correlation between its weight series and its **own** detune series
  `|r| ≤ 0.3` (distinct seeds give ≈ 0; a shared instance gives ≈ 1).

**Implement:**

- In `updateControl(chunk)` step 3, after the detune read:
  ```
  dm = clamp(mutationLanes_.smoothCur[i], -1.0f, 1.0f);
  w  = (mutationAmount_ <= 0.0f) ? 1.0f : 1.0f + mutationAmount_ · kMaxMutationDepth · dm;
  targetAmplitude_[i] = masked_[i] ? 0.0f : unmutatedTarget_[i] · w · env_i;
  ```
  **Implement the `mutationAmount_ <= 0` branch explicitly** so no rounding can produce `0.9999999` and fail
  SC-016's "exactly 1.0". Bound `[0.25, 1.75]` follows from `kMaxMutationDepth = 0.75` and `|dm| ≤ 1`.
- `w_i` multiplies the FR-017 chain **downstream of the normalizer** — the normalizer's input stays the
  un-mutated `a_i` set, so mutation's level movement survives to the output (this is what SC-016's ±3 dB
  window observes).
- `setMutation(float)` clamps `[0, 1]` and sets **neither** dirty flag (the weight is applied per chunk).
- The mutation bank is advanced and read on the **same chunk cadence** as the detune bank (FR-072), and its
  smoothness stays at `kMutationSmoothness` regardless of `setDriftSmoothness` (FR-074's zipper-freedom rides
  on the FR-014 smoother plus the once-per-chunk update).

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green.

---

## GROUP 12 — Per-partial envelope

### T012 — Linear AR envelope, offsets, gate

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FR:** FR-023. **Plan:** §4.4. **SC:** SC-013.

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_PartialEnvelopeOffsetsStagger")`** (SC-013). Mutation and drift **0** (so
  `w_i ≡ 1` and each partial's target is static), Richness 1 (64 active). Sample
  `getPartialCurrentAmplitude(i)` once per 512-block through a note-on.
  **Steady-state target reference (this exact expression, all four clauses):**
  ```
  steadyStateTarget(i) = getPartialUnmutatedTargetAmplitude(i) * getPartialAntiAliasGain(i)
  ```
  It is **not** `getPartialTargetAmplitude(i)`, which is `unmutated·w·env` and therefore *moves with the
  envelope*: using it would fire the 50 % crossing at `env ≈ 2 ×` the smoother lag rather than at half the
  sounding level, and would make clause 4 degenerate (after gate-off `env → 0` ⇒ `target → 0`, so "≤ 1 % of
  its target" is unsatisfiable). The `antiAliasGain` factor is required because the kernel's steady state is
  `currentAmplitude → targetAmplitude·antiAliasGain`; at this configuration (`f0 = 110`, top partial 7.04 kHz)
  the MCF correction alone is ≈ 0.895, so omitting it would fail a correct implementation by 10 %.
  **Crossing = the first sample at which a partial's current amplitude reaches 50 % of its steady-state
  target.** Thresholds (absolute — a relative "≥ 5× the zero-spread spread" is degenerate because the
  zero-spread spread is 0 by definition):
  1. offset spread 0 → `max − min` crossing time across all active partials **≤ 1 block**;
  2. maximum offset spread → `max − min` crossing time **≥ 100 ms** and **≤ `kMaxEnvOffsetSec`**
     (sanity: at spread 1, `0.5 · 2.0 · (max oa − min oa) ≈ 970 ms`);
  3. at both settings, every active partial reaches **≥ 95 %** of its steady-state target within
     `attackSec + its own offset` — no partial stranded;
  4. after `noteOff()`, every active partial's current amplitude is **monotonically non-increasing** and falls
     to **≤ 1 % of that same steady-state target** within
     `decaySec + its decay offset + 5 · kAmpSmoothTimeSec`. Nothing sustains after gate-off; nothing is cut off
     before its decay time elapses.

**Implement (plan §4.4) — linear AR, evaluated per chunk with `dt = chunk / fs`:**

```
attackOffsetSec_[i] = offsetSpread_ · kMaxEnvOffsetSec · oa_i     // oa_i, od_i drawn once per seed (T008)
decayOffsetSec_[i]  = offsetSpread_ · kMaxEnvOffsetSec · od_i     // re-applied whenever the spread changes,
                                                                  // so the ORDER of onsets is a seed property
attackSec_i = attackSec_ + attackOffsetSec_[i]    // time-to-100 %, NOT a time constant
decaySec_i  = decaySec_  + decayOffsetSec_[i]
switch (envStage_[i]):
  Attack : envValue_[i] += dt / attackSec_i;  if (>= 1) { = 1; stage = Hold; }
  Hold   : envValue_[i] = 1.0f;
  Release: envValue_[i] -= dt / decaySec_i;   if (<= 0) { = 0; stage = Idle; }
  Idle   : envValue_[i] = 0.0f;
```

- Constant slope in both directions. **Decay IS the release** — no separate release stage, no sustain level,
  no multi-stage segments, no velocity or key scaling, no exponential variant. Scope is exactly
  {attack, decay, offset spread, gate}.
- `kMinAttackSec = 0.05f` is **25×** `kAmpSmoothTimeSec`, satisfying FR-023's ≥20× rule with margin: the
  FR-014 smoother lags a linear ramp by one time constant, so the observed amplitude at `t = attackSec_i` is
  `1 − 0.002/attackSec_i` = **0.960** at 50 ms, clearing clause 3's 95 % bar (40 ms would give exactly 0.95).
- `setAttackTimeSec` clamps `[0.05, 30]`, `setDecayTimeSec` clamps `[0.05, 60]`, `setEnvelopeOffsetSpread`
  clamps `[0, 1]`.
- `noteOff()`: `gate_ = false`; every partial not Idle → Release **from its current value** (a note-off during
  the attack starts the decay from wherever the envelope is — it must not snap).
- Envelope values are clamped into `[0, 1]` at every stage transition.
- Envelopes are evaluated **by the cloud, once per chunk**, and written into `targetAmplitude_[]`; the
  kernel's single scalar `ampSmoothCoeff` is a uniform de-zippering stage downstream and cannot express
  per-partial timing at all.

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green.

---

## GROUP 13 — Retrigger and quiescence

### T013 — `noteOn` retrigger rule + SC-006's retrigger clauses

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs:** FR-016 (quiescence rule), FR-023 (re-open from current value). **Plan:** §4.7, §4.1. **SC:** SC-006
(retrigger clauses).

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_RetriggerIsClickFree")`**, using the **same pinned `ClickDetectorConfig`,
  differential pass condition and mandatory positive control** as SC-005 (see T016 for the exact config; write
  it identically here):
  1. **Sounding retrigger** (the non-quiescent path): gate on, every partial at full amplitude, issue a second
     `noteOn()`. Assert `detections(retriggered) <= detections(control)`. **Additionally assert the
     mechanism**, not just the outcome: capture all 64 `getPartialSinState(i)` / `getPartialCosState(i)`
     immediately before and immediately after the `noteOn()` → `REQUIRE` they are **bitwise unchanged**. That
     is what makes retrigger click-free *by construction* — no MCF state is stepped discontinuously, so there
     is no discontinuity for FR-013's crossfade to hide.
  2. **Quiescent path:** `noteOff()`, render until every `getPartialCurrentAmplitude(i)` is below
     `kQuiescenceAmplitude`, then `noteOn()` → assert the phase state **did** change. Without this case a
     never-redraw implementation passes case 1 vacuously and breaks SC-018's seeded-onset behaviour.

**Implement (plan §4.7):**

```cpp
[[nodiscard]] bool isQuiescent() const noexcept {
    return !gate_ && allBelow(currentAmplitude_, kQuiescenceAmplitude);   // scans [0, kernelCount_)
}
void noteOn() noexcept {
    if (isQuiescent()) redrawPhases();                       // quiescent path ONLY
    gate_ = true;
    for (i < kMaxPartials) envStage_[i] = Attack;            // re-open FROM envValue_[i], NEVER from 0
}
```

- Document `kQuiescenceAmplitude = 1.0e-5f` (−100 dBFS) in the header as the floor.
- Repeated `noteOn()` with no intervening `noteOff()` is idempotent apart from re-opening the envelope.
- Two renders with the same seed but different retrigger timing legitimately differ; determinism is unaffected
  because SC-009 pins the call sequence.
- **Wire the §4.1 quiescent early-out for real now** that the lanes exist: on `isQuiescent()`, advance **both**
  lane banks by `numSamples`, add `ceil(n/64)` to `driftReadCount_`, zero-fill and return. Advancing before
  returning keeps free-running life-modulation (roadmap Key Design Decision 1) and leaves SC-015's clauses
  2(a)/2(b)/2(c) bit-for-bit unaffected — a silent render and a sounding render of the same length must leave
  identical lane state. Re-run T010's SC-015 case to confirm.

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green — **especially SC-015**, whose
read-count and partition clauses the early-out could break.

---

## GROUP 14 — Setter hygiene and the parameter grid

### T014 — FR-007 both halves, no-op guards, dirty flags, SC-017

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**FRs:** FR-006, FR-007. **Plan:** §4.8, §6.3, D8. **SC:** SC-017.

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_ParameterGridStaysFiniteAndBounded")`** (SC-017), three passes:
  1. **Grid.** Fully enumerated Cartesian product: {min, mid, max} × each of the five macros × {20, 4000} Hz
     fundamental × {0, max} drift depth × {0, max} stereo spread × {0, max} envelope offset spread —
     `3⁵ · 2⁴ = 3888` renders of **1 s at 48 kHz** through a `noteOn()`. Reuse **one** instance and keep the
     render short. Per render: **every** output sample finite by bit test (`detail::isNaN`/`isInf`, never
     `std::isnan`), `|out| ≤ kOutputClamp`, and `REQUIRE(cloud.stateFinite())` at the end.
     Note the `{0 drift depth} × {max Mutation}` cells are **live** mutation cells under FR-072's independent
     bank — not inert ones.
  2. **Non-finite rejection (FR-007 half 1).** For **every** setter — `setFundamentalHz`, the five macros,
     `setDriftDepthCents`, `setDriftSmoothness`, `setStereoSpread`, `setAttackTimeSec`, `setDecayTimeSec`,
     `setEnvelopeOffsetSpread`, and `setPartialPosition` — call it with a **bit-pattern-constructed** NaN and
     with ±Inf (built through a `volatile` sink, **never** `std::numeric_limits`, which folds under
     `-ffast-math`), then assert the corresponding getter returns **exactly** its pre-call value and a
     subsequent render matches the pre-call render within `render_fingerprint.h` tolerances.
  3. **Finite out-of-range clamping (FR-007 half 2 — the half nothing else tests).** Iterate the documented
     bounds table; for each setter call it with `(min − 1)` and `(max + 1)` and `REQUIRE` the getter returns
     **exactly** the documented bound:
     `setFundamentalHz(1.0f) → 20.0f`; `setFundamentalHz(9000.0f) → 4000.0f`;
     `setRichness / setMutation / setDriftSmoothness / setStereoSpread / setEnvelopeOffsetSpread → [0, 1]`;
     `setInharmonicity(5.0f) → 0.1f`; `setSpectralTiltDb(100.0f) → +12.0f`;
     `setSpectralGravity(-9.0f) → -1.0f`; `setDriftDepthCents(999.0f) → 50.0f`;
     `setAttackTimeSec(0.001f) → 0.05f`; `setDecayTimeSec(999.0f) → 60.0f`;
     `setPartialPosition(0, -9.0f) → -1.0f`.
     **Without this pass a setter that assigned without `std::clamp` passes every other planned assertion.**

**Implement (plan §4.8) — one idiom, everywhere:**

```cpp
void setSpectralGravity(float g) noexcept {
    if (detail::isNaN(g) || detail::isInf(g)) return;   // bit tests (db_utils.h:54,174), NOT std::isnan
    const float v = std::clamp(g, -1.0f, 1.0f);         // documented range
    if (v == gravity_) return;                          // no-op guard
    gravity_ = v;
    freqDirty_ = true;                                  // deferred to the next chunk boundary
}
```

- **The rejection test precedes every assignment**, so a NaN/Inf argument leaves the getter and the rendered
  output bit-identical. It works under `-ffast-math` because it inspects the IEEE-754 bit pattern via
  `std::bit_cast`, never floating-point predicates.
- **No-op guard + dirty flags are a requirement of the design, not a micro-optimisation.** `freqDirty_` is set
  by fundamental / inharmonicity / gravity / richness; `ampDirty_` by richness / tilt; `setMutation` sets
  neither. `updateControl(chunk)` **step 0** consumes them:
  ```
  if (freqDirty_) { recalculateFrequencies(); freqDirty_ = false; ampDirty_ = true; }   // N(r) may have moved
  if (ampDirty_)  { recalculateAmplitudes();  ampDirty_  = false; }                     // ends in setTarget
  ```
  so **N setter calls inside one block cost at most one frequency plus one amplitude recompute**, and a setter
  called with the value it already holds costs nothing. Measured stakes: unconditional recompute costs
  ≈ 6,100 ns/block under the Phase-7 call pattern — **92 % of the 6,600 ns of headroom** between the §6.2
  projection and SC-007's 35,533 ns gate; with the guard, ≈ 2,050 ns.
- **`setFundamentalHz` is the one exception to pure deferral** — its FR-013 crossfade arming must happen at
  call time because it reads `lastOutL_`/`lastOutR_`. Only the epsilon recompute defers.
- `setPartialPosition` uses the same guard, is a no-op for `index >= kMaxPartials`, clamps to `[−1, +1]`, sets
  `positionOverridden_[index]`, and recomputes only that partial's pan pair.
- `stateFinite()` — the reference bit test (`harmonic_oscillator_bank.h:622-633`) over `sinState_`/`cosState_`
  for `i < kernelCount_`.

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green.

---

## GROUP 15 — Denormal hygiene

### T015 — `currentAmplitude_` guard + `HarmonicCloud_DecaysToExactZero`

**Edit:** `harmonic_cloud.h`, `harmonic_cloud_test.cpp`.

**Plan:** §4.9, R7.

**Write the failing test first:**

- **`TEST_CASE("HarmonicCloud_DecaysToExactZero")`**. This guard is invisible to every other test because
  `dsp/tests/dsp_test_main.cpp:13` calls `enableFTZDAZ()` before any case runs — the **process** flushes
  denormals, not the component, so a guard that exists only in the environment would ship. Assert the
  component's **own arithmetic** instead:
  1. `prepare(48000)`, `noteOn()`, render to steady state, `noteOff()`, render `decaySec + 2 s`, then
     `REQUIRE(cloud.getPartialCurrentAmplitude(i) == 0.0f)` **exactly**, for **every** `i < kMaxPartials`, and
     `REQUIRE(cloud.isQuiescent())`.
  2. Repeat with `soloPartial(0)` held for the whole render, asserting the same for every masked `i` — masked
     partials sit **inside** `[0, kernelCount_)` and are exactly the case the tail high-water does not cover.
  Both clauses hold with or without FTZ.

**Implement (plan §4.9) — the guard goes on `currentAmplitude_`, which is the state that decays, NOT on
`targetAmplitude_`:**

```cpp
// once per chunk, for i < kernelCount_
if (targetAmplitude_[i] == 0.0f && currentAmplitude_[i] < kTailSilenceThreshold) {
    currentAmplitude_[i] = 0.0f;    // the harmonic_oscillator_bank.h:756 idiom, generalised
}
```

Flushing `targetAmplitude_` does **not** prevent the denormal: the smoothing recurrence's state *is*
`currentAmplitude_`, and the kernel never flushes it (`vAmp = hn::MulAdd(vCoeff, vDiff, vAmp)`,
`..._simd.cpp:93`; scalar tail `:120`). Forcing `targetAmplitude_` to exactly 0 is in fact the condition that
walks `currentAmplitude_` *through* the denormal range — at `kAmpSmoothTimeSec = 0.002f` the per-sample
retention is ≈ 0.9896 at 48 kHz, so ≈ 8.4 k samples (≈ 175 ms) after a target reaches 0 the state is denormal
and stays denormal for thousands more samples. The §3 tail high-water only covers `i ≥ activeCount_`;
**masked/soloed-out partials and every partial after `noteOff()` completes its release sit inside
`[0, kernelCount_)`**. Keeping a `detail::flushDenormal` on `targetAmplitude_` as well is harmless but is
**not** the mitigation and must not be described as one. The OU lanes have their own guards
(`kDriftDenormalFloor` on each walk plus the lane smoother's `flushDenormal`).

**Verify:** zero warnings; the case green; `"HarmonicCloud_*"` still green.

---

## GROUP 16 — Click-freeness

### T016 — SC-005, SC-006 macro sweeps, mask/solo

**Edit:** `harmonic_cloud_test.cpp`. Header only if a detection exposes a defect.

**FRs:** FR-014, FR-034, FR-043, FR-053, FR-063, FR-074, FR-084, FR-008 (mask). **SCs:** SC-005, SC-006
(macro-sweep clauses).

**Pinned detector config — used identically by all three cases in this task and by T013:**
```cpp
TestUtils::ClickDetectorConfig cfg{ .sampleRate = 48000.0f,   // MUST match the render; the struct default is 44100
                                    .frameSize = 512, .hopSize = 256,
                                    .detectionThreshold = 5.0f,
                                    .energyThresholdDb = -60.0f, .mergeGap = 5 };
```
Designated initialisers, no narrowing. `ClickDetector det(cfg); det.prepare(); auto d = det.detect(buf, n);`

**Why the pass condition is differential and never "0 detections":** `ClickDetector` is a **within-frame
statistical outlier test** — `threshold = mean(|dx|) + 5·stddev(|dx|)` per frame
(`artifact_detection.h:186-193`). For a broadband 64-partial sum the derivative is near-Gaussian by CLT, so a
5σ-above-mean threshold is ≈ 3.8σ in absolute terms — a per-sample exceedance probability on the order of
1e-4, i.e. **tens of false detections over a 30 s render with no artifact present**.

> **SUPERSEDED 2026-07-26 — read `spec.md` SC-005/SC-006 first.** The count and slew thresholds written
> below were the pre-implementation guess and have been **withdrawn by measurement**: on a click-free build
> the frozen control scores **0** detections against the modulated render's 267, the frozen-control slew
> ratio is **1.785** against a 1.5 bound, the "10 % of peak" positive control yields **0** detections
> (0.0748 against the render's own 0.1710 max `|dx|`), and for SC-006 the two equally-valid frozen endpoint
> controls score **0** and **43** for the same sweep. The shipped criteria are the on-grid ones in
> `spec.md`'s "AMENDED" blocks; the shipped tests carry standing "withdrawn-clause guard" assertions that
> re-derive each of those numbers on every run. The task text below is retained as the historical record of
> what was attempted, not as an instruction.

**Write the failing tests:**

- **`TEST_CASE("HarmonicCloud_NoZipperUnderMutationAndDrift")`** (SC-005). Two **30 s** renders at 48 kHz of
  the identical configuration: **modulated** (Mutation 1.0, drift depth max) and **control** (drift depth 0,
  Mutation 0, no parameter movement). Assert:
  1. `detections(modulated) <= detections(control)`;
  2. `maxPerSampleDelta(modulated) <= 1.5f * maxPerSampleDelta(control)`.
  **Positive control, first and mandatory:** inject a one-sample step of **10 % of peak** into a copy of the
  control render and `REQUIRE(detections >= 1)` at that sample index. Without it the criterion cannot
  distinguish "no artifacts" from "detector not wired up".
- **`TEST_CASE("HarmonicCloud_MacroSweepsAreClickFree")`** (SC-006). Five **5 s** continuous min→max sweeps,
  one per macro (Richness, Inharmonicity, Tilt, Mutation, Gravity), each stepped once per 512-block, **plus** a
  ≥ 1-octave fundamental step (the FR-013 crossfade path). Same detector, same config, same positive control.
  `REQUIRE(detections(swept) <= detections(control))` for each of the six, **independently**.
- **`TEST_CASE("HarmonicCloud_MaskAndSoloAreClickFree")`** (FR-008). Nothing else covers it — SC-006 sweeps
  only the five macros plus the fundamental, and SC-001/SC-012 use `soloPartial` as a measurement tool without
  asserting click-freeness. Toggle `soloPartial(...)` / `setPartialMask(...)` / `clearPartialMask()` mid-render;
  same detector, differential condition and positive control; `detections(masked) <= detections(control)`.
  **Plus the mechanism assertion:** immediately after `soloPartial(k)`, a non-soloed partial's
  `getPartialCurrentAmplitude` is still **non-zero** and only *decays* over the following blocks. An
  implementation that zeroed `currentAmplitude_` directly instead of `targetAmplitude_` passes the click
  detector on some seeds but fails this.

**Verify:** zero warnings; all three cases green; `"HarmonicCloud_*"` still green.

---

## GROUP 17 — Independent verification (parallel)

Three tasks, three **disjoint** files, none touching `harmonic_cloud.h` or CMake.

### T017 [P] — SC-008 (no allocation) + SC-009 (determinism)

**Edit:** `dsp/tests/unit/systems/harmonic_cloud_test.cpp` only.

**FRs:** FR-002, FR-005. **SCs:** SC-008, SC-009.

- **`TEST_CASE("HarmonicCloud_NoAllocInProcess")`** (SC-008). **Do NOT include
  `allocation_operator_overrides.h`** — it is already included by
  `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388` for this binary and a second inclusion is a
  duplicate-symbol link error. Including only `allocation_detector.h` without those global replacements would
  observe 0 allocations unconditionally and pass **vacuously**, so the liveness case below is mandatory and
  must come first. Use the in-repo idiom (`selectable_oscillator_test.cpp:418-422`), **not** `AllocationScope`:
  ```cpp
  TestHelpers::AllocationDetector::instance().startTracking();
  auto* p = new int[16]; delete[] p;
  const auto live = TestHelpers::AllocationDetector::instance().stopTracking();
  REQUIRE(live >= 1);                        // liveness FIRST
  // ... prepare(48000), noteOn() ...
  TestHelpers::AllocationDetector::instance().startTracking();
  // 200 blocks of 512 with macro automation and drift live
  const auto n = TestHelpers::AllocationDetector::instance().stopTracking();
  REQUIRE(n == 0);
  ```
- **`TEST_CASE("HarmonicCloud_SeededRenderIsReproducible")`** (SC-009). **Configuration pinned so the negative
  control cannot pass vacuously:** drift depth, Mutation and stereo spread each at **≥ 50 % of range** (with
  any of them at its legal default of 0 the seed would influence nothing and a different-seed render would be
  identical, proving nothing). Same seed + same call sequence + same block schedule →
  `REQUIRE(TestUtils::compareFingerprints(a, b).withinTolerance())`. Different seed →
  `REQUIRE(cmp.worstMetricRelativeError > 10.0 * TestUtils::kMetricTolerance)` — not merely "outside
  tolerance". Explicitly **not** a bit-exact digest.

**Verify:** zero warnings; `dsp_systems_tests.exe "HarmonicCloud_NoAllocInProcess"` and
`"HarmonicCloud_SeededRenderIsReproducible"` green.

### T018 [P] — SC-010 (sample-rate invariance)

**Edit:** `dsp/tests/unit/systems/harmonic_cloud_spectral_test.cpp` only.

**FR:** FR-003. **SC:** SC-010.

- **`TEST_CASE("HarmonicCloud_SampleRateInvariant")`**. `f0 = 110`, 64 partials (top partial 7.04 kHz),
  **drift 0** so `detune ≡ 1` (the plan-D5 anti-alias form is exactly the reference form there). Render the
  same musical configuration at **44.1 / 48 / 96 kHz**.
  1. **Frequency:** each partial's measured frequency in Hz agrees across rates within the SC-001 estimator
     tolerance (use the T002 estimator with `soloPartial`).
  2. **Amplitude, scoped:** compared **only** for partials whose synthesized frequency is
     `< 0.8 × 22050 = 17,640 Hz`, and the test must **assert** that every active partial satisfies it rather
     than assume — an unscoped comparison fails a *correct* FR-015 implementation, because the fade starts at a
     fixed *fraction* of Nyquist (17.64 kHz at 44.1 kHz vs 38.4 kHz at 96 kHz). Tolerance **0.5 dB**.
     **Amplitude must be measured from the rendered signal — FFT magnitude at each partial bin — NOT via
     `getPartialCurrentAmplitude / getPartialAntiAliasGain`.** That accessor ratio is vacuous here: the
     kernel drives `currentAmplitude → targetAmplitude·antiAliasGain`, whose steady state is independent of
     `ampSmoothCoeff_`, and dividing by `antiAliasGain` cancels the only sample-rate-dependent factor left, so
     `targetAmplitude = gain·a_i` — which contains no sample-rate term at all — comes back bit-identical at all
     three rates and the test would pass even when a coefficient is wrongly expressed in samples.
  3. **One sample-rate-sensitive timing assertion** — the FR-003 failure this criterion exists to catch
     (`ampSmoothCoeff_`, `crossfadeLengthSamples_`, the envelope's `dt = chunk/fs`): with offset spread 0, one
     partial's attack **50 %-crossing time expressed in seconds** must agree across the three rates within one
     control chunk (`64/44100 s`).

**Verify:** zero warnings; `dsp_systems_tests.exe "HarmonicCloud_SampleRateInvariant"` green.

### T019 [P] — SC-007 (CPU budget), both baselines

**Create:** `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp` (new, disjoint).

**SC:** SC-007. **Plan:** §6.1–§6.3.

Both cases are `TEST_CASE(..., "[.perf]")` — every CI leg excludes perf-tagged cases
(`.github/workflows/ci.yml:328`, `:574`, `:951`; `valgrind-nightly.yml:202`), so no job ever evaluates an
absolute figure. Metric is **nanoseconds per 512-sample block**, best-of-N; the percentage is derived against
the fixed `512/48000 ≈ 10.667 ms` wall-clock budget, so 0.5 % is `kReferenceNsPerBlock ≈ 53,333 ns`.

- **`TEST_CASE("HarmonicCloud_CpuBudget", "[.perf]")`** — static configuration: `kMaxPartials = 64`, Mutation
  1.0, drift depth max, **both** lane banks live, 8 chunks per 512-block (so 8 × 128 = 1024 drift reads per
  block), **no setter calls**. Record `kStaticBaselineNsPerBlock` as a `constexpr double` with the machine and
  date in a comment beside it (Windows 11, MSVC Release, `build/windows-x64-release` — Phase-1 convention).
  **Projected ≈ 29,000 ns** (§6.2). No `static_assert` on this one.
- **`TEST_CASE("HarmonicCloud_CpuBudgetUnderAutomation", "[.perf]")` — THE GATING CASE.** Identical
  configuration **plus the Phase-7 call pattern**: all five macros **and** `setFundamentalHz` stepped once per
  512-block (the cadence SC-006 already uses), so the no-op guard and dirty-flag deferral are exercised.
  Measuring the budget in a configuration the component will never be used in understates it, and roadmap
  line 484 makes the budget a functional requirement.
  ```cpp
  constexpr double kRegressionFactor    = 1.5;
  constexpr double kReferenceNsPerBlock = (512.0 / 48000.0) * 1e9 * 0.005;   // ≈ 53,333 ns
  constexpr double kAutomatedBaselineNsPerBlock = /* MEASURED — machine + date in this comment */;
  static_assert(kAutomatedBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock,
                "baseline must be no weaker than the SC-007 reference figure");
  ```
  Exact shape of `dsp/tests/unit/processors/life_modulators_perf_test.cpp:54-73`. **Hard ceiling 35,533 ns;
  projected ≈ 30,950 ns.** The enforced gate is the **relative** one (`best-of-N ≤ baseline × 1.5`); the
  absolute 53,333 ns figure is `WARN`-reported only.
  **If the first measurement exceeds 35,533 ns the phase is over budget.** The response is to reduce cost —
  apply the `std::exp2(cents/1200)` lever in place of `semitonesToRatio` (mathematically the same function,
  `pitch_utils.h:25` is `std::pow(2.0f, s/12.0f)`; measured **2.3× faster**, worth ≈ 3,900 ns/block) and
  re-measure — **never** to raise the baseline, and never to renegotiate `kRegressionFactor` at implementation
  time.
  **Third measurement in the same case, `WARN` only, no assertion:** the **quiescent** cost (post-`noteOff()`,
  `isQuiescent()` true) must be materially below the sounding cost, proving the §4.1 early-out is wired.

**Verify:** zero warnings; run explicitly (the tag hides it from the default run):
```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_CpuBudget*"
```

---

## GROUP 18 — Integration

### T020 — CMake registration (the single authoritative edit)

**Edit:** `dsp/tests/CMakeLists.txt` (existing, shared).

Append to the **`dsp_systems_tests`** source list, which currently ends at
`unit/systems/sympathetic_resonance_test.cpp` (line 331; the target opens at line 294):

```cmake
    unit/systems/harmonic_cloud_test.cpp
    unit/systems/harmonic_cloud_spectral_test.cpp
    unit/systems/harmonic_cloud_perf_test.cpp
```

The list is **explicit — there is no GLOB** (`dsp/tests/CMakeLists.txt:18-19`), so a file omitted here silently
drops from the build.

- **No change** to the `-fno-fast-math` `set_source_files_properties` block (`:385-647`) — the component's
  guards are bit-based and the tests build non-finite inputs from bit patterns, so no source property is
  needed (Phase-1 precedent).
- **No change** to `catch_discover_tests(dsp_systems_tests REPORTER console)` (`:665`) — it already covers the
  target.
- Nothing outside `dsp/` changes: no plugin, no CI roster, no clang-tidy target list (`dsp` already covers
  `dsp/include/`).
- Confirm no task left a temporary source-list line behind: the three lines above must appear **exactly once**
  each.

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
```
builds clean with zero warnings, and `ctest --test-dir build/windows-x64-release -C Release -N` lists the
`HarmonicCloud_*` cases.

### T021 — Full-suite run

No file edits.

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_CpuBudget*" 2>&1 | tail -20
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

`dsp_processors_tests` is run because this phase reuses the SIMD MCF kernel and `BrownianDrift` — a regression
there is in scope. Capture clang-tidy output to a log on the **first** run; never re-run a slow tool just to
look at its output. **Every** warning is owned, including ones in adjacent code the change surfaced —
"pre-existing" is not an excuse. **No test failure may be dismissed as pre-existing or flaky** (Constitution
VIII): halt and fix.

**Verify:** both suites report `All tests passed`; clang-tidy `dsp` clean.

### T022 — Portability gate

No file edits.

```bash
node tools/check-portability.js
```

A green MSVC build proves **nothing** about the GCC/AppleClang legs. This gate catches what MSVC accepts and
they reject. Re-check by hand, in the diff, that:
- no `std::isnan` / `std::isinf` / `std::numeric_limits<float>::infinity()` anywhere in the component or its
  tests;
- no narrowing in brace initialisation (Clang errors; every aggregate uses designated initialisers);
- no new SIMD was written — the reused kernel already uses `hn::LoadU`/`hn::StoreU`, and `alignas(32)` on the
  SoA arrays is a locality choice, never an alignment assumption;
- `HarmonicCloud::kMaxPartials` is **class-scoped**; `harmonic_cloud.h` includes neither `harmonic_types.h` nor
  `harmonic_oscillator_bank.h`.

**Verify:** `check-portability.js` exits clean.

---

## Traceability

| Requirement | Task |
|---|---|
| FR-001, FR-002, FR-003 (lifecycle), FR-012, FR-024 | T002 |
| FR-004 (block contract, guards) | T002, T003 |
| FR-005 (seeded determinism) | T008, T017 |
| FR-006 (bounded output) | T003, T014 |
| FR-007 (setter hygiene, both halves) | T014 |
| FR-008 (introspection surface) | T002 (shape), T003/T004/T008/T009 (real state) |
| FR-011, FR-013, FR-016 | T003, T013 (retrigger) |
| FR-014, FR-017, FR-041–043, FR-061–063 | T004 |
| FR-015 (anti-aliasing) | T003, T007 |
| FR-021, FR-022, FR-091–093 | T008 |
| FR-023 (per-partial envelope) | T012 |
| FR-031–035 (as amended) | T001, T009, T010 |
| FR-051–053 | T003, T006 |
| FR-071–074 | T011 |
| FR-081–084 | T003, T006 |
| SC-001, SC-002 | T003 |
| SC-003, SC-018 | T005 |
| SC-004 | T006 |
| SC-005, SC-006 (sweeps) | T016 |
| SC-006 (retrigger) | T013 |
| SC-007 | T019 |
| SC-008, SC-009 | T017 |
| SC-010 | T018 |
| SC-011 | T007 |
| SC-012 | T008 |
| SC-013 | T012 |
| SC-014 | T004 |
| SC-015 | T010 |
| SC-016 | T011 |
| SC-017 | T014 |
| Edge Cases (guards, denormals, seed 0, rate change) | T002, T008, T013, T015, T018 |
| Build/portability gates | T020, T021, T022 |
