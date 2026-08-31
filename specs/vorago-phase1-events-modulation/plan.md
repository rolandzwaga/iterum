# Implementation Plan: Vorago Phase 1 — Modulation Gap-Fill + Slow Event Engine

**Spec:** `specs/vorago-phase1-events-modulation/spec.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 1 (lines 154–172)
**Layers touched:** Layer 2 (`dsp/include/krate/dsp/processors/`) — two new headers, one extended;
Layer 1 (`dsp/include/krate/dsp/primitives/chaos_waveshaper.h`) — one enumerator + two switch arms.
**Test target:** `dsp_processors_tests` (new sources) + `dsp_primitives_tests` (must stay green, SC-007).
**Plugin work:** none. Vorago's plugin starts at roadmap Phase 11.

This plan is written so an implementer never guesses the math or an API. Every reused signature below
was **read from the working tree in this session** and is quoted with `file:line`. Every analytic
constant the spec relies on was **re-derived numerically in this session** (results in §1.5 and §2.3);
where a spec figure and the measurement disagree, the measurement is recorded and the difference is
called out in §8.

---

## 0. Shared design

### 0.1 Reused components — verified signatures

| Component | Header:line | Exact signature / fact reused |
|---|---|---|
| `ModulationSource` (ABC, L0) | `core/modulation_source.h:31` | `class ModulationSource`; `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0` (`:37`); `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept = 0` (`:41`). **Only these two are virtual** — `setSeed`/`prepare`/`process` are plain non-virtual members in every implementation; writing `override` on them fails to compile. |
| `Xorshift32` (L0) | `core/random.h:41` | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` (`:45`); `[[nodiscard]] constexpr uint32_t next() noexcept` (`:50`); `nextFloat() → [-1,1]` (`:59`); `nextUnipolar() → [0,1]` (`:67`); `constexpr void seed(uint32_t) noexcept` (`:73`); `state()` (`:79`). **`kToFloat = 1/(2³²−1)` (`:89`) and `next()` never returns 0**, so `nextUnipolar()` ∈ (0, 1] — the half-open detail FR-060's polarity rule depends on (§3.4). Seed 0 → `kDefaultSeed = 2463534242u` (`:74`, definition `:85`). |
| `deriveStreamSeed` (L0) | `core/random.h:102-111` | `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` — lowbias32 finaliser, guaranteed non-zero (`:110`). **Note the salt is narrowed:** `static_cast<std::uint32_t>(salt + 1u) * 0x9E3779B9u` (`:104`), so only the low 32 bits of the salt participate. Consequence for FR-011 (§1.3): the gradient lattice hash repeats with period 2³² ≈ 2.1e9 cells — 1800× beyond the 1.15e6 cells reachable in SC-017's 8 h window. |
| `detail::isFinite` / `detail::isNaN` (L0) | `core/db_utils.h:118` (`isFinite(float)`), `:126` (`isFinite(double)`), `:99` (`isNaN`) | Fast-math-immune bit-pattern checks behind an `opaqueFloatBits` optimisation barrier. **The house remedy** — `std::isnan/isinf/isfinite` are forbidden (`tools/lint-nonfinite-symbols.js`) and fold away under the macOS `-ffast-math` leg. Used both in the new setters (§0.3) and in every new test's finiteness assertion. |
| `OnePoleSmoother` (L1) | `primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (`:160`); `setTarget(float)` — NaN→0, ±Inf→±1e10 sanitised (`:170`); `getCurrentValue() const` (`:191`); `[[nodiscard]] float process()` (`:197`); `advanceSamples(size_t)` O(1) closed form (`:243`); `snapTo(float)` (`:263`). Coefficient `coeff = exp(−5000/(smoothTimeMs·sr))` (`:91-92`), i.e. **`smoothTimeMs` is time-to-99 %, τ = smoothTimeMs/5**; clamped to `[kMinSmoothingTimeMs, kMaxSmoothingTimeMs] = [0.1, 1000]` ms (`:58`, `:61`). **Load-bearing gotcha:** `process()` (`:199-201`) and `advanceSamples()` (`:251-253`) both *snap* `current_` to `target_` once `|current − target| < kCompletionThreshold = 1e-4` (`:55`) — see §1.6. (`process()` additionally flushes denormals at `:208`.) |
| `BrownianDrift` (L2) | `processors/brownian_drift.h:94` | **The shape template both new components copy verbatim:** `kControlRateInterval = 32` (`:105`); `prepare(double)` floors the rate — `sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0` (`:122`) — then ends in `initState()` (`:128`); `reset()` is just `initState()` (`:133-135`); `setSeed` is a plain member storing `configuredSeed_` **and** reseeding (`:145-148`); `process()` decrements-then-checks (`:178-188`); `processBlock()` checks-then-chunks (`:194-206`); `getCurrentValue()` returns the smoother through an inert clamp (`:212-214`); `getSourceRange()` is a fixed `{-1.f, 1.f}` (`:217-219`); `initState()` reseeds from `configuredSeed_` and `snapTo`s the smoother (`:242-247`). Denormal-floor idiom at `:264-266`. **Not modified.** |
| `SplineTrajectory` (L2) | `processors/spline_trajectory.h:114` | Fixed-ring, allocation-free precedent: `process()` (`:187`), `processBlock()` with an explicit `numSamples == 0` early-out (`:193-198`), `advance()` carrying the playhead in `double` and consuming whole segments in a `while` loop (`:262-269`). The scheduler's multi-transition loop (FR-063) is the same shape. **Not modified.** |
| `ChaosModSource` (L2) | `processors/chaos_mod_source.h:35` | **Extended.** `kMinSpeed = 0.05f` (`:37`), `kMaxSpeed = 20.0f` (`:38`), `kControlRateInterval = 32` (`:43`), per-model scales `:46-49`; `prepare` `:53-58`; `reset` `:60-65`; `process` `:69-75`; `processBlock` `:80-91`; `setInputLevel` **unclamped** `:119-121`. Three `switch (model_)` sites: `updateModelParams()` `:158-181`, `resetModelState()` `:183-200`, `updateAttractor()` `:218-231`. Coupling perturbation `state_.x += coupling_ * inputLevel_ * 0.1f` `:214-216`; output `normalizedOutput_ = std::clamp(std::tanh(state_.x / normalizationScale_), -1.0f, 1.0f)` `:236`; guard `checkAndResetIfDiverged()` `:306-312` compares against **`safeBound_ * 10.0f`**. Everything after `private:` (`:151`) is inaccessible — the reason FR-036 adds one accessor. |
| `ChaosModel` enum (L1) | `primitives/chaos_waveshaper.h:52-57` | `enum class ChaosModel : uint8_t { Lorenz=0, Rossler=1, Chua=2, Henon=3 }`. Shared with `ChaosWaveshaper`: validator `if (static_cast<uint8_t>(model) > static_cast<uint8_t>(ChaosModel::Henon)) model = ChaosModel::Lorenz;` (`:451-455`); **two `default:`-less switches** at `:640` (`updateAttractor`) and `:685` (`resetModelState`). |
| `FFT` (L1) | `primitives/fft.h:128` | `void prepare(size_t fftSize)` (`:147`) — validates power-of-two only; `void forward(const float*, Complex*)` (`:186`); `struct Complex { float real, imag; }` (`:55`). **`kMaxFFTSize = 8192` (`:47`)** — see §5.3: SC-003 needs 65 536 points, so the test uses a local double-precision FFT rather than this class. |
| Allocation harness | `tests/test_helpers/allocation_detector.h:48` | `TestHelpers::AllocationDetector::instance()`, `startTracking()` (`:53`), `stopTracking()` (`:59`). Usage precedent: `dsp/tests/unit/processors/brownian_drift_test.cpp:444-463`. **`brownian_drift_test.cpp:27-28` is the single owner of `<allocation_operator_overrides.h>` in `dsp_processors_tests`** — new TUs include **only** `<allocation_detector.h>` or the link fails on duplicate symbols (`life_modulators_perf_test.cpp:19-23`). |
| Perf-case idiom | `dsp/tests/unit/processors/life_modulators_perf_test.cpp:54,58,70,73` | `kBlockBudgetNs` (512/48 kHz in ns), `kReferenceNsPerBlock = kBlockBudgetNs * 0.0005`, `kBaselineNsPerBlock = 3000.0`, `kRegressionFactor = 1.5`, plus the documented invariant at `:60-66` that `baseline × factor` must stay **below** the reference figure. Fast-math-safe finiteness at `:77-81`. **Two further facts this plan reuses and an earlier draft dropped:** the case is `TEST_CASE("LifeModulators_ControlRateCost", "[.perf]")` (`:147`) — *hidden*, so it never runs in any CI lane (§5.1); and `sumOutputs()` (`:124-130`) reads every instance's output into a sink each measured block, with the stated reason "so the optimizer cannot dead-code the advance away. A real consumer reads these once per block too" (§5.2's SC-014 row). |
| C1-at-joins idiom | `dsp/tests/unit/processors/spline_trajectory_test.cpp:260-321` | Decimated second difference: `kStride = 240` = 100 points/segment (`:265`), interior vs straddling triples, `REQUIRE(interiorMax > 1.0e-5)` (`:314`), `REQUIRE(straddleMax <= 5.0 * interiorMax)` (`:320`). SC-009 copies this whole construction with a computed stride. |

### 0.2 Layer legality

Both new headers include **only** Layer 0/1 and stdlib:

```cpp
#include <krate/dsp/core/modulation_source.h>   // L0
#include <krate/dsp/core/random.h>              // L0
#include <krate/dsp/core/db_utils.h>            // L0  (detail::isNaN / isFinite)
#include <krate/dsp/primitives/smoother.h>      // L1  (PerlinNoiseSource only)
#include <algorithm> <array> <cmath> <cstddef> <cstdint> <utility>
```

`slow_event_scheduler.h` needs **no** `smoother.h`: its envelope is evaluated per sample in closed
form (§3.3), so there is nothing to smooth. `node tools/lint-layers.js` enforces this (SC-016).

### 0.3 NaN-safe parameter clamping (used by every new float setter)

`std::clamp(NaN, lo, hi)` returns **NaN** — it compares, it does not sanitise — and the Edge Cases
require non-finite setter input to be clamped. `std::isnan` is forbidden. Every `float` setter in both
new components therefore routes through one shared private helper:

```cpp
/// Clamp with NaN mapped to `lo`. std::clamp propagates NaN; detail::isNaN is
/// the fast-math-immune bit-pattern test (core/db_utils.h:99) — the house
/// remedy, because the macOS CI leg builds with -ffast-math.
[[nodiscard]] static constexpr float sanitizeClamp(float v, float lo, float hi) noexcept {
    if (detail::isNaN(v)) return lo;
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
```

`+Inf` clamps to `hi` and `−Inf` to `lo` by ordinary comparison — correct without an extra branch.
Integer setters (`setOctaves`, `setTargetCount`) use plain `std::clamp`.

### 0.4 ODR sweep (re-run this session)

`grep -rn "\(class\|struct\|enum class\) <Name>\b" dsp/ plugins/ tools/` → `PerlinNoiseSource`
**0 hits**, `SlowEventScheduler` **0 hits**, `Aizawa` **0 hits**. `ScheduledEvent` has 3 hits, all
test/tool-local structs outside `Krate::DSP` — the name stays **forbidden**; the nested POD is
`SlowEventScheduler::Event` (FR-053). Near-name hazards `PatternScheduler`
(`processors/pattern_scheduler.h:54`), `GrainScheduler` (`processors/grain_scheduler.h:29`) and
`RandomMode::Perlin` (`processors/stochastic_filter.h:48`) are distinct names and are **not touched**.

---

## 1. `PerlinNoiseSource` — Layer 2

**Header:** `dsp/include/krate/dsp/processors/perlin_noise_source.h` (new)
**Covers:** FR-001…FR-007, FR-011…FR-019; SC-001…SC-005, SC-013, SC-014, SC-016, SC-017.

### 1.1 Public API

```cpp
namespace Krate { namespace DSP {

/// @brief 1D gradient (Perlin) noise as a bounded modulation source, with an
///        octave-summed (fBm) roughness control.
/// @par Output Range: [-1.0, +1.0] — FIXED, independent of depth (FR-006).
class PerlinNoiseSource : public ModulationSource {
public:
    // --- Constants (brownian_drift.h:97-110 style) ---------------------------
    static constexpr float  kMinRate     = 0.005f;   ///< 200 s per lattice cell
    static constexpr float  kMaxRate     = 5.0f;     ///< 0.2 s per lattice cell
    static constexpr int    kMinOctaves  = 1;
    static constexpr int    kMaxOctaves  = 4;
    static constexpr float  kPersistence = 0.5f;     ///< a_k = kPersistence^k
    static constexpr float  kLacunarity  = 2.0f;     ///< l_k = kLacunarity^k
    /// Maps the analytic single-octave bound |n(x)| <= 0.5 onto [-1,+1] (FR-017).
    static constexpr float  kGradientNormalize = 2.0f;
    /// Output smoother, time-to-99 % (tau = 1 ms, cutoff ~159 Hz). FR-018 / §1.6.
    static constexpr float  kOutputSmoothMs = 5.0f;
    static constexpr std::size_t kControlRateInterval = 32;  ///< == brownian_drift.h:105

    static constexpr float         kDefaultRate       = 0.1f;
    static constexpr int           kDefaultOctaves    = 2;
    static constexpr float         kDefaultDepth      = 1.0f;
    static constexpr std::uint32_t kDefaultPerlinSeed = 0x9E37u;

    PerlinNoiseSource() noexcept { deriveOctaveSeeds(); }

    // --- Lifecycle (FR-002) --------------------------------------------------
    void prepare(double sampleRate) noexcept;   ///< full re-init; floors sr at 1 Hz
    void reset() noexcept;                       ///< identical in effect to prepare()

    // --- Configuration (FR-013/014/016/019) ----------------------------------
    void setSeed(std::uint32_t seedValue) noexcept;   ///< plain member, NOT an override
    void setRate(float hz) noexcept;                  ///< clamp [kMinRate, kMaxRate]
    void setOctaves(int n) noexcept;                  ///< clamp [kMinOctaves, kMaxOctaves]
    void setDepth(float normalized) noexcept;         ///< clamp [0,1]

    [[nodiscard]] float  getRate()     const noexcept { return rate_; }
    [[nodiscard]] int    getOctaves()  const noexcept { return octaves_; }
    [[nodiscard]] float  getDepth()    const noexcept { return depth_; }
    /// Octave-0 lattice position in cells (test/harness observable).
    [[nodiscard]] double getPosition() const noexcept { return positionCells_; }

    // --- Advance (FR-003) ----------------------------------------------------
    void process() noexcept;                             ///< one sample
    void processBlock(std::size_t numSamples) noexcept;  ///< O(control steps); (0) is a no-op

    // --- ModulationSource (FR-001) -------------------------------------------
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float,float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // --- FR-015 observability (const, non-RT; used by SC-004(c) and the harness)
    /// Raw gradient-lattice noise of ONE octave stream at the current position,
    /// scaled only by kGradientNormalize => [-1,+1]. EXCLUDES a_k, 1/sum(a_k),
    /// depth and the output smoother. Pure function of (seed, octaveIndex,
    /// position); valid for any octaveIndex < kMaxOctaves regardless of the
    /// configured octave count. Returns 0 for octaveIndex >= kMaxOctaves.
    [[nodiscard]] float getOctaveValue(std::size_t octaveIndex) const noexcept;
};

}}  // namespace Krate::DSP
```

### 1.2 State layout (all fixed-size members — FR-004)

```cpp
    double sampleRate_          = 44100.0;
    double cellsPerControlStep_ = kDefaultRate * kControlRateInterval / 44100.0;
    double positionCells_       = 0.0;   ///< UNWRAPPED octave-0 position (FR-007 carve-out)

    float  rate_         = kDefaultRate;
    int    octaves_      = kDefaultOctaves;
    float  depth_        = kDefaultDepth;
    double amplitudeSum_ = 1.5;          ///< sum(a_k) for octaves_ : 1, 1.5, 1.75, 1.875

    int    samplesUntilControl_ = 0;

    std::uint32_t configuredSeed_ = kDefaultPerlinSeed;
    std::array<std::uint32_t, kMaxOctaves> octaveSeeds_{};  ///< deriveStreamSeed(seed, k)
    OnePoleSmoother outputSmoother_;
```

No RNG object is stored: **FR-012 forbids a running stream** — gradients come from a stateless hash.
`std::array` is fixed capacity; nothing allocates. This is a deliberate, recorded departure from
FR-005's literal words ("an owned `Xorshift32`") — see **§8.11**, so the compliance pass does not
either tick FR-005 without evidence or open it as a defect.

### 1.3 Algorithm — exact math

**Gradient lattice (FR-011, FR-012, Clarifications Q6).**

```cpp
    /// Positive bias so a negative lattice index is well-defined rather than
    /// UB-by-omission. Positions only advance forward from 0, so the reachable
    /// index range is [0, ~1.2e6]; 2^31 leaves >2e9 cells of headroom either way.
    static constexpr std::int64_t kIndexBias = 1LL << 31;

    [[nodiscard]] float gradientAt(std::size_t octaveIndex, std::int64_t index) const noexcept {
        const std::uint32_t h = deriveStreamSeed(octaveSeeds_[octaveIndex],
                                                 static_cast<std::size_t>(index + kIndexBias));
        return (h & 1u) ? 1.0f : -1.0f;      // UNIT gradients -> FR-017 exactness
    }

    /// Raw 1D gradient noise of one octave at lattice position x, in [-1,+1].
    [[nodiscard]] float rawOctaveNoise(std::size_t octaveIndex, double x) const noexcept {
        const double fx = std::floor(x);
        const auto   i0 = static_cast<std::int64_t>(fx);
        const double t  = x - fx;                                     // [0,1)
        const double g0 = static_cast<double>(gradientAt(octaveIndex, i0));
        const double g1 = static_cast<double>(gradientAt(octaveIndex, i0 + 1));
        const double s  = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);  // 6t^5-15t^4+10t^3
        const double n  = (g0 * t) * (1.0 - s) + (g1 * (t - 1.0)) * s;
        return static_cast<float>(static_cast<double>(kGradientNormalize) * n);
    }
```

`s(t)` is **smootherstep**, whose first *and* second derivatives vanish at `t ∈ {0,1}` — that, not the
lerp, is what makes the noise C1 (in fact C2) as a function of position.

**Octave streams (FR-015).** `octaveSeeds_[k] = deriveStreamSeed(configuredSeed_, k)`, computed once
in `deriveOctaveSeeds()` from the constructor and from `setSeed`. **All `kMaxOctaves` seeds are always
derived**, independent of `octaves_` — this is exactly what makes `getOctaveValue(0)` bit-identical
between a 1-octave and a 4-octave instance (SC-004(c)) and makes `getOctaveValue(i)` legal for any
`i < kMaxOctaves`.

**fBm sum (FR-014).** Octave `k` is evaluated at `positionCells_ · 2ᵏ`. Because `kLacunarity = 2`, the
scale factor is an exact power of two, so the octave position carries **no** rounding error relative to
`positionCells_`:

```cpp
    [[nodiscard]] float evaluateFbm() const noexcept {
        double sum = 0.0, amp = 1.0;
        for (int k = 0; k < octaves_; ++k) {
            const double scaled = positionCells_ * static_cast<double>(1ULL << k);  // exact
            sum += amp * static_cast<double>(
                       rawOctaveNoise(static_cast<std::size_t>(k), scaled));
            amp *= static_cast<double>(kPersistence);
        }
        const double normalized = sum / amplitudeSum_;   // range-preserving (FR-014/FR-017)
        // Inert safety net (FR-006): |normalized| <= 1 analytically, so this clamp
        // never engages — SC-001's exact depth-scaling clause proves it.
        return std::clamp(static_cast<float>(static_cast<double>(depth_) * normalized),
                          -1.0f, 1.0f);
    }
```

`amplitudeSum_ = Σ_{k<n} 0.5ᵏ = 2 − 2^{1−n}` → **1, 1.5, 1.75, 1.875** for n = 1…4. Recomputed in
`setOctaves`.

**`getOctaveValue(i)`** is `rawOctaveNoise(i, positionCells_ * (1ULL << i))` with an
`i >= kMaxOctaves → 0.0f` guard. It deliberately does **not** consult `octaves_`, `amplitudeSum_`,
`depth_` or the smoother.

**Advance.** Copied verbatim from `brownian_drift.h:178-206`. The decrement-then-check /
check-then-chunk asymmetry is deliberate and the two forms are observationally equivalent: with
`samplesUntilControl_` starting at 0 both take a control step at sample 0 and thereafter at identical
absolute sample indices for any block partitioning — which is exactly FR-003's stated consequence and
why SC-004 needs a non-32-aligned discriminator.

```cpp
    void advanceControlStep() noexcept {
        positionCells_ += cellsPerControlStep_;      // double accumulator, never wrapped
        outputSmoother_.setTarget(evaluateFbm());
    }
    void process() noexcept {
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = static_cast<int>(kControlRateInterval);
            advanceControlStep();
        }
        static_cast<void>(outputSmoother_.process());  // process() is [[nodiscard]] (smoother.h:197)
    }
    void processBlock(std::size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (samplesUntilControl_ <= 0) {
                samplesUntilControl_ = static_cast<int>(kControlRateInterval);
                advanceControlStep();
            }
            const int advance = std::min(remaining, samplesUntilControl_);
            samplesUntilControl_ -= advance;
            remaining -= advance;
            outputSmoother_.advanceSamples(static_cast<std::size_t>(advance));
        }
    }
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), -1.0f, 1.0f);   // inert clamp
    }
```

**`setRate` is position-continuous.** It updates `cellsPerControlStep_ = rate_ · 32 / sampleRate_`
(computed in `double`) and **never touches `positionCells_`** — a rate change alters the speed of
travel, not the place, so no jump can occur. That is why the position is an accumulator rather than
`stepCount × cellsPerStep`.

### 1.4 Prepare / reset contract (FR-002, Clarifications Q4)

```cpp
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;      // brownian_drift.h:122
        updateIncrement();                                       // cellsPerControlStep_
        outputSmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
        initState();
    }
    void reset() noexcept { initState(); }
    void initState() noexcept {
        deriveOctaveSeeds();
        positionCells_ = 0.0;
        samplesUntilControl_ = 0;
        outputSmoother_.snapTo(evaluateFbm());   // n(0) = 0 exactly => output is 0
    }
```

`prepare()` is a **full re-initialisation**, identical in effect to `reset()`. At `x = 0` the noise is
exactly 0 for every seed (`t = 0` ⇒ `n = g₀·0·1 + g₁·(−1)·0 = 0`), so the post-`prepare` output is a
determinate `0.0f` — FR-002's "well-defined with no prior advance" and the post-construction Edge Case
are both satisfied without a special case.

### 1.5 Verified analytic constants

Re-derived numerically this session (200 001-point sweep over `t` for all four gradient sign
combinations; reproduce the derivation in the header banner):

| Quantity | Spec figure | Measured this session |
|---|---|---|
| `max |n(x)|`, unit gradients | 0.5 (FR-017) | **0.500000** at `t = 0.5`, `(g₀,g₁) = (−1,+1)` |
| `max |dn/dx|` per cell, raw | ≈1.35 | **1.347151** at `t = 0.7887` |
| `kMaxSlope = kGradientNormalize ×` that | 2.7 | **2.6943** |
| `fbmFactor(n) = Σ(aₖlₖ)/Σaₖ` | 1.000 … 2.133 | **1.0000 / 1.3333 / 1.7143 / 2.1333** |
| `α = 1 − exp(−5000/(5·48000))` | 2.0618e-2 | **2.0618e-2** |
| `kStepResponseGain = α/(1−(1−α)³²)` | 4.237e-2 | **4.2373e-2** |
| Raw control step, `kMaxRate`, n = 4 | 1.92e-2 | **1.916e-2** |
| Emitted per-sample delta (SC-002 upper band) | 8.14e-4 | **8.118e-4** |

All eight agree; SC-002's closed form may be written into the test as-is.

### 1.6 The `OnePoleSmoother` completion snap — documented, not a defect

`OnePoleSmoother::process()` (`smoother.h:199-201`) and `advanceSamples()` (`:251-253`) snap
`current_` to `target_` once `|current − target| < kCompletionThreshold = 1e-4` (`:55`).

* **At `kMaxRate`, n = 4** (SC-002's worst case) the raw control step is 1.92e-2 and only
  `(1−α)³² = 0.513` of it decays per control step, so the residual never falls under 1e-4 and the snap
  **never fires** — SC-002's two-sided band measures the closed form it claims to.
* **At low rates** (e.g. `kMinRate`, raw step ≈1.9e-5) the snap fires every control step and the
  emitted signal degenerates to the raw 32-sample staircase — with a step of ≤1e-4, i.e. 20× inside
  SC-002's 2.0e-3 threshold. This is why SC-002 pins its configuration to the worst case and why
  SC-001's excursion clause is scoped to `rate ≥ 0.1 Hz`.

Record this in the header banner so a future reader does not "fix" it.

---

## 2. Aizawa attractor — Layer 1 enum amendment + Layer 2 implementation

**Covers:** FR-031…FR-036; SC-006, SC-007.

### 2.1 `primitives/chaos_waveshaper.h` — three surgical edits

**1. Enum (`:52-57`)** — append, never insert:

```cpp
enum class ChaosModel : uint8_t {
    Lorenz  = 0,
    Rossler = 1,
    Chua    = 2,
    Henon   = 3,
    /// Aizawa system (a=0.95, b=0.7, c=0.6, d=3.5, e=0.25, f=0.1).
    /// Implemented by ChaosModSource (Layer 2) ONLY. ChaosWaveshaper's validator
    /// (chaos_waveshaper.h:451-455) still rejects it and substitutes Lorenz.
    Aizawa  = 4
};
```

**2. Validator (`:451-455`) — unchanged.** `setModel(Aizawa)` continues to fall back to `Lorenz`, so
`chaos_waveshaper_test.cpp:33-36` (index pinning), `:40` (underlying type) and `:579-583`
(invalid-enum fallback) stay green **unmodified** (FR-034, SC-007).

**3. Both `default:`-less switches** (`:640` `updateAttractor()`, `:685` `resetModelState()`) gain a
*grouped* label sharing the Lorenz arm — grouping, not fallthrough, so `-Wimplicit-fallthrough` stays
quiet:

```cpp
        // Unreachable by construction: setModel() (chaos_waveshaper.h:451-455)
        // substitutes Lorenz for anything above Henon, so model_ is never Aizawa
        // here. Present solely for -Wswitch exhaustiveness on the shared
        // ChaosModel enum. Aizawa is a ChaosModSource-only model (FR-034).
        case ChaosModel::Aizawa:
        case ChaosModel::Lorenz:
            updateLorenz();     // in resetModelState(): the Lorenz initialiser block
            break;
```

**Blast-radius verification (this session — evidence for FR-035).** `grep -rn "ChaosModel" plugins/
dsp/include/krate/dsp/systems/` returns **no `switch` over `ChaosModel` outside those two headers**.
Every plugin use is a bounded cast: `plugins/ruinae/src/engine/ruinae_voice.h:958`
(`std::clamp(model, 0, 3)`), `plugins/ruinae/src/parameters/distortion_params.h:17,84`
(`kChaosModelCount = 4`), `plugins/disrumpo/src/controller/controller.cpp:379` (`/ 3.0`),
`plugins/disrumpo/src/processor/processor_params.cpp:676-679`,
`plugins/disrumpo/src/processor/processor_state.cpp:244,681`. None can emit `4`, none switches, and
`ModulationEngine::setChaosModel/getChaosModel` (`systems/modulation_engine.h:493,613`) forward the
enum by value. **No plugin edit, no state-version bump, no parameter change is required.**

### 2.2 `processors/chaos_mod_source.h` — six edits

1. **Public constant** beside `kLorenzScale`…`kHenonScale` (`:46-49`):
   `static constexpr float kAizawaScale = 1.5f;  ///< attractor x-extent ~ +/-1.5`

2. **`updateModelParams()` (`:158-181`)** — new arm; literals inline to match the surrounding style,
   with the measurement recorded in-line:

```cpp
            case ChaosModel::Aizawa:
                // dt = baseDt_ * speed (chaos_mod_source.h:211) and kMaxSpeed = 20
                // (:38), so baseDt_ must satisfy baseDt_ * 20 <= 0.01. Forward-Euler
                // Aizawa was simulated over dt in [5e-4, 0.2] from four initial
                // states: chaotic (x-extent +/-1.5..1.6) for dt <= 0.015, and for
                // dt >= 0.02 it COLLAPSES onto the x = y = 0 fixed point
                // (z ~= -1.105), where the output is identically 0 — silently,
                // with no divergence and no guard reset. 5.0e-4 puts dt in
                // [2.5e-5, 1.0e-2], entirely inside the verified-chaotic region.
                baseDt_ = 5.0e-4f;
                normalizationScale_ = kAizawaScale;
                // The guard threshold is safeBound_ * 10 (:307-309). The coupling
                // path (:214-216; setInputLevel is unclamped at :119-121)
                // legitimately drives |state| to ~112 at kMinSpeed with a
                // full-scale DC input, so 25 -> threshold 250 never fires. The
                // Chua value (5 -> 50) fires ~2000x per 600 s render.
                safeBound_ = 25.0f;
                break;
```

3. **`resetModelState()` (`:183-200`)** — `case ChaosModel::Aizawa: state_ = {0.1f, 0.0f, 0.0f}; break;`

4. **`updateAttractor()` (`:218-231`)** — `case ChaosModel::Aizawa: updateAizawa(dt); break;`
   plus the new private method (forward Euler, matching `updateLorenz` `:239-251`):

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

```cpp
public:
    /// Number of times the divergence guard (:306-312) has reset the attractor
    /// since the last prepare()/reset(). Model-agnostic. Zero is the healthy
    /// value for every model; SC-006 asserts it for Aizawa.
    [[nodiscard]] std::uint32_t getDivergenceResetCount() const noexcept {
        return divergenceResetCount_;
    }
private:
    void checkAndResetIfDiverged() noexcept {
        if (std::abs(state_.x) > safeBound_ * 10.0f ||
            std::abs(state_.y) > safeBound_ * 10.0f ||
            std::abs(state_.z) > safeBound_ * 10.0f) {
            ++divergenceResetCount_;
            resetModelState();
        }
    }
    std::uint32_t divergenceResetCount_ = 0;
```

   Zeroed in `prepare()` (`:53-58`) and `reset()` (`:60-65`) **only** — *not* in `resetModelState()`,
   which the guard itself calls and which `setModel()` (`:103-109`) also calls.

   **The counter needs a POSITIVE test, or it is untestable-by-omission.** Every SC-006 clause that
   touches it asserts `getDivergenceResetCount() == 0u`, so an implementation that never increments —
   the `++divergenceResetCount_;` line dropped, or the accessor stubbed `return 0u;` — passes SC-006
   in full while silently restoring R4, the failure mode FR-036 exists to expose. The guard is
   forceable through the public API: `setCoupling` clamps to `[kMinCoupling, kMaxCoupling] = [0, 1]`
   (`chaos_mod_source.h:40-41,116`) but **`setInputLevel` is unclamped** (`:119-121`), and
   `updateAttractor()` adds `coupling_ * inputLevel_ * 0.1f` to `state_.x` whenever
   `coupling_ > 0 && |inputLevel_| > 0.001f` (`:213-216`). `setCoupling(1.0f)` with
   `setInputLevel(1.0e5f)` therefore displaces `x` by 1e4 every control step — 40× the
   `safeBound_ * 10 = 250` threshold — so the guard fires on the first step. Traced by hand from the
   header this session: the coupling add, the Euler step and `checkAndResetIfDiverged()` all happen
   inside one `updateAttractor()` call, and from `state_ = {0.1, 0, 0}` a single Aizawa step at
   `x = 1e4` reaches `|dz| ≈ 1e8` and `z ≈ −5e4` — large, but finite, and the guard resets before the
   next step, so the test cannot overflow. New sibling case
   `ChaosModSource_DivergenceCounterObservable` in §5.2 binds both halves of FR-036 (counts up; zeroed
   by `prepare()`/`reset()`).

6. **FR-002 sample-rate floor.** `prepare()` (`:54`) currently stores `sampleRate` unfloored. One line
   — `sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;` — makes FR-002 literally true for all three
   sources at zero behavioural risk: `sampleRate_` is stored but never read by the attractor math
   (`dt = baseDt_ * effectiveSpeed`, `:211`), so no existing output can change.

### 2.3 Simulation evidence (re-run this session; forward Euler, 2e6 steps per point)

| speed | dt | max‖state‖ | max\|out\| (2nd half) | σ(out) |
|---|---|---|---|---|
| 0.05 (`kMinSpeed`) | 2.5e-5 | 1.885 | 0.750 | 0.327 |
| 0.1 | 5.0e-5 | 1.885 | 0.755 | 0.331 |
| 1.0 | 5.0e-4 | 1.885 | 0.762 | 0.326 |
| 5.0 | 2.5e-3 | 1.887 | 0.768 | 0.294 |
| 20 (`kMaxSpeed`) | 1.0e-2 | 1.938 | 0.780 | 0.263 |

Collapse boundary: `dt = 0.015` still chaotic (σ = 0.222); **`dt ≥ 0.02` → `max|out| = 0.0000`,
`σ = 0.0000`, `x → 0`, `z → −1.105`** (note the sign — see §8.2).

Coupling worst case (`coupling = 1`, `inputLevel = 1.0`, 900 000 control steps = 600 s at 48 kHz):
`max‖state‖` = **111.86** at `kMinSpeed`, 19.22 at speed 1, 2.47 at `kMaxSpeed` — all far below the
`safeBound_ * 10 = 250` guard.

Every SC-006 threshold therefore holds with margin: `0.5 < max|out| < 0.99` (measured 0.750–0.780),
`σ > 0.1` (measured 0.263–0.331), `getDivergenceResetCount() == 0` at coupling 0 **and** coupling 1.

---

## 3. `SlowEventScheduler` — Layer 2

**Header:** `dsp/include/krate/dsp/processors/slow_event_scheduler.h` (new)
**Covers:** FR-001…FR-007, FR-051…FR-067; SC-008…SC-014, SC-016, SC-017, SC-018.

### 3.1 Public API

```cpp
namespace Krate { namespace DSP {

/// @brief Seeded stochastic scheduler for discrete, minutes-scale "happenings".
/// Fires one event per drawn period (onset-to-onset), each carrying an opaque
/// target index, a smootherstep attack/hold/release envelope (FR-056 as amended,
/// plan 8.7 -- C1 at every join, in fact C2), a depth and a
/// polarity. Polled, never callback-driven (FR-057).
/// @par Output Range: [-1.0, +1.0] — FIXED, independent of depth (FR-006).
class SlowEventScheduler : public ModulationSource {
public:
    enum class Phase : std::uint8_t { Idle = 0, Attack = 1, Hold = 2, Release = 3 };

    /// Nested POD. NOT named ScheduledEvent — that name has 3 pre-existing
    /// definitions outside Krate::DSP (see the spec's New Components table).
    struct Event {
        std::uint8_t target   = kNoTarget;
        float        depth    = 0.0f;
        std::int8_t  polarity = 1;
    };

    static constexpr std::uint8_t kNoTarget   = 0xFFu;
    static constexpr std::uint8_t kMaxTargets = 16u;

    static constexpr float kMinIntervalSeconds = 1.0f;
    static constexpr float kMaxIntervalSeconds = 600.0f;
    static constexpr float kMinSegmentSeconds  = 0.05f;
    static constexpr float kMaxSegmentSeconds  = 300.0f;
    static constexpr std::size_t kControlRateInterval = 32;  ///< == brownian_drift.h:105

    static constexpr float kDefaultMinInterval = 20.0f;   ///< roadmap line 167
    static constexpr float kDefaultMaxInterval = 90.0f;
    static constexpr float kDefaultAttack      = 5.0f;
    static constexpr float kDefaultHold        = 3.0f;
    static constexpr float kDefaultRelease     = 8.0f;
    static constexpr float kDefaultMinDepth    = 0.3f;
    static constexpr float kDefaultMaxDepth    = 1.0f;
    static constexpr float kDefaultBipolarProbability = 0.5f;
    static constexpr std::uint8_t  kDefaultTargetCount = 4u;
    static constexpr std::uint32_t kDefaultEventSeed   = 0x51E7u;

    SlowEventScheduler() noexcept = default;

    // --- Lifecycle (FR-002, FR-061, FR-067) ---------------------------------
    void prepare(double sampleRate) noexcept;   ///< full re-init; floors sr at 1 Hz
    void reset() noexcept;

    // --- Configuration; ALL latch per FR-066 --------------------------------
    void setSeed(std::uint32_t seedValue) noexcept;                       // FR-062
    void setIntervalRange(float minSeconds, float maxSeconds) noexcept;   // FR-052
    void setEnvelopeTimes(float attackSeconds, float holdSeconds,
                          float releaseSeconds) noexcept;                 // FR-055
    void setDepthRange(float minDepth, float maxDepth) noexcept;          // FR-060
    void setBipolarProbability(float probability) noexcept;               // FR-060
    void setTargetCount(std::uint8_t count) noexcept;                     // FR-059

    [[nodiscard]] float getMinIntervalSeconds() const noexcept;
    [[nodiscard]] float getMaxIntervalSeconds() const noexcept;
    [[nodiscard]] float getAttackSeconds()  const noexcept;   ///< CONFIGURED, pre-fit
    [[nodiscard]] float getHoldSeconds()    const noexcept;
    [[nodiscard]] float getReleaseSeconds() const noexcept;
    [[nodiscard]] float getMinDepth() const noexcept;
    [[nodiscard]] float getMaxDepth() const noexcept;
    [[nodiscard]] float getBipolarProbability() const noexcept;
    [[nodiscard]] std::uint8_t getTargetCount() const noexcept;

    // --- Advance (FR-003, FR-063) -------------------------------------------
    void process() noexcept;
    void processBlock(std::size_t numSamples) noexcept;   ///< (0) is a no-op

    // --- ModulationSource (FR-001) ------------------------------------------
    [[nodiscard]] float getCurrentValue() const noexcept override;  ///< polarity*depth*env(t)
    [[nodiscard]] std::pair<float,float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // --- Read surface (FR-058) ----------------------------------------------
    [[nodiscard]] std::uint8_t getActiveTarget()  const noexcept;  ///< kNoTarget while idle
    [[nodiscard]] bool         isEventActive()    const noexcept;
    [[nodiscard]] Phase        getEventPhase()    const noexcept;
    [[nodiscard]] float        getPeriodSeconds() const noexcept;  ///< cycle in flight
    [[nodiscard]] float        getEventDurationSeconds() const noexcept;  ///< effA+effH+effR
    [[nodiscard]] float        getEffectiveAttackSeconds()  const noexcept;
    [[nodiscard]] float        getEffectiveHoldSeconds()    const noexcept;
    [[nodiscard]] float        getEffectiveReleaseSeconds() const noexcept;
    [[nodiscard]] float        getEnvelopeValue()  const noexcept;  ///< unipolar [0,1]
    [[nodiscard]] float        getActiveDepth()    const noexcept;  ///< [0,1]
    [[nodiscard]] std::int8_t  getActivePolarity() const noexcept;  ///< +/-1
};

}}  // namespace Krate::DSP
```

`valueForTarget(uint8_t)` is deliberately **absent** (Clarifications Q5).

**Poll-rate contract — state it in the header banner, because SC-014 measures it.** Per FR-065
`getCurrentValue()` and `getEnvelopeValue()` are **per-sample** calls: they recompute the envelope from
`elapsedSamples_` on every invocation and nothing is cached by `process()`/`processBlock()`. That is
what gives the output its per-sample continuity (SC-009 polls per sample; a control-rate staircase
would make its `interiorMax`/`joinMax` ratio measure quantization instead of curvature). It also means
this component does **not** keep the contract `BrownianDrift` (`brownian_drift.h:212-214`) and
`ChaosModSource` (`chaos_mod_source.h:94-96`) keep, where `getCurrentValue()` is a plain member load:
a consumer polling this source per sample pays real arithmetic per sample, not a load. Two consequences
are binding on the rest of this plan:

1. The rise shape may not contain a transcendental (§3.3). At the roadmap's sizing — 4–8 voices with
   "multiple independent schedulers per voice" (roadmap line 167) — a `std::cos` per sample per
   scheduler exceeds SC-014's whole budget on its own.
2. SC-014's harness must **read** the schedulers at that rate, or it measures a component that (by
   this design) does almost nothing in `processBlock()` — see §5.2's SC-014 row.

The per-call cost is 6 multiplies, 3 adds and ≤ 4 comparisons; there is no allocation, branch on
external state, or virtual dispatch beyond the one ABC override.

### 3.2 State layout (fixed-size — FR-004)

```cpp
    double sampleRate_ = 44100.0;

    // --- stored configuration (written by setters, read ONLY at draw time) ---
    float minIntervalSeconds_ = kDefaultMinInterval;
    float maxIntervalSeconds_ = kDefaultMaxInterval;
    float attackSeconds_  = kDefaultAttack;
    float holdSeconds_    = kDefaultHold;
    float releaseSeconds_ = kDefaultRelease;
    float minDepth_ = kDefaultMinDepth;
    float maxDepth_ = kDefaultMaxDepth;
    float bipolarProbability_ = kDefaultBipolarProbability;
    std::uint8_t targetCount_ = kDefaultTargetCount;

    // --- latched, per cycle (written ONLY at an onset; FR-066) ---------------
    Event  active_{};
    double periodSamples_     = 0.0;  ///< onset-to-onset length of the cycle in flight
    double attackEndSamples_  = 0.0;  ///< effA
    double holdEndSamples_    = 0.0;  ///< effA + effH
    double releaseEndSamples_ = 0.0;  ///< effA + effH + effR
    bool   firstOnsetPending_ = true; ///< FR-067 pre-roll idle, before any event

    // --- clocks --------------------------------------------------------------
    double elapsedSamples_      = 0.0;  ///< since the current onset (or since prepare)
    int    samplesUntilControl_ = 0;

    std::uint32_t configuredSeed_ = kDefaultEventSeed;
    Xorshift32    rng_{kDefaultEventSeed};
```

Nine `double`s, eight `float`s, three small integers, one PRNG. **No pool, no ring, no container.**

### 3.3 Timeline — the single-clock design (FR-054, FR-056, FR-064, FR-065)

The whole component turns on one decision: **the envelope, the phase and the active flag are all pure
functions of `elapsedSamples_` and four latched boundary offsets.** Nothing about the output is
computed incrementally, so nothing can drift, step or accumulate.

```
  onset                                                     next onset
    |<-- effA -->|<-- effH -->|<-- effR -->|<---- idle --->|
    0        attackEnd     holdEnd     releaseEnd       periodSamples_
```

```cpp
    /// Smootherstep rise shape (FR-056, as amended -- see 8.7). Rising 0->1 over
    /// [0,1]; first AND second derivatives vanish at BOTH ends, so every join is
    /// C1 -- in fact C2 -- BY CONSTRUCTION. This is the SAME polynomial the
    /// Perlin lattice uses (1.3), so the phase has exactly one shape function.
    /// The clamp of u also absorbs the <= one control step of onset-detection
    /// latency without folding back.
    ///
    /// NOT a raised cosine: getCurrentValue() is a per-sample call (FR-065, 3.1),
    /// so a std::cos here is 2048 transcendental evaluations per 512-sample block
    /// across SC-014's four schedulers. Measured this session (WSL g++ 13 -O2,
    /// 2e7 iterations, two runs): 0.5-0.5*std::cos(pi*u) costs 4.84-4.94 ns/eval
    /// => ~10 000 ns/block, i.e. 0.94x SC-014's 10 667 ns reference and 1.41x the
    /// 7111 ns baseline ceiling, BEFORE the four PerlinNoiseSource instances and
    /// the Aizawa source. The polynomial costs 0.86-0.87 ns/eval (5.6x cheaper)
    /// => ~1780 ns/block, which is what makes SC-014 attainable.
    [[nodiscard]] static double riseShape(double u) noexcept {
        const double c = (u <= 0.0) ? 0.0 : (u >= 1.0) ? 1.0 : u;
        return c * c * c * (c * (6.0 * c - 15.0) + 10.0);   // 6c^5 - 15c^4 + 10c^3
    }

    [[nodiscard]] double envelopeAt(double t) const noexcept {
        if (firstOnsetPending_)    return 0.0;
        if (t < attackEndSamples_) return riseShape(t / attackEndSamples_);
        if (t < holdEndSamples_)   return 1.0;
        if (t < releaseEndSamples_)
            return riseShape((releaseEndSamples_ - t) /
                             (releaseEndSamples_ - holdEndSamples_));
        return 0.0;                 // idle stretch
    }

    [[nodiscard]] Phase phaseAt(double t) const noexcept {
        if (firstOnsetPending_ || t >= releaseEndSamples_) return Phase::Idle;
        if (t < attackEndSamples_) return Phase::Attack;
        if (t < holdEndSamples_)   return Phase::Hold;
        return Phase::Release;
    }

    [[nodiscard]] float getCurrentValue() const noexcept override {
        // Analytic bound (FR-006): |polarity| = 1, depth in [0,1], envelope in
        // [0,1] => |value| <= 1. The clamp is an inert net, never the source of
        // the bound (SC-011's exact-half-ratio clause proves it never engages).
        const double v = static_cast<double>(active_.polarity)
                       * static_cast<double>(active_.depth)
                       * envelopeAt(elapsedSamples_);
        return std::clamp(static_cast<float>(v), -1.0f, 1.0f);
    }
    /// Computed DIRECTLY — never by dividing polarity*depth back out (depth may be 0).
    [[nodiscard]] float getEnvelopeValue() const noexcept {
        return static_cast<float>(envelopeAt(elapsedSamples_));
    }
```

**Why this, and not a per-phase clock.** A design that keeps a separate `phaseElapsed_` and carries the
remainder at each segment boundary (`phaseElapsed_ -= segmentSamples_`) introduces a value step at
every Hold→Release join of `10·(r/effR)³` with `r < 32` (the smootherstep expansion
`f(1−ε) = 1 − 10ε³ + O(ε⁴)`); at the shortest legal segment (0.05 s = 2400 samples at 48 kHz) that is
**2.4e-5**. Under the earlier raised-cosine draft the same step was `¼π²·(r/effR)²` = **4.4e-4**, which
is what originally made this choice load-bearing for SC-009's margin. With the polynomial shape the
step is no longer the deciding factor, and the single-clock design is kept for the reasons that do not
depend on the shape: evaluating from `elapsedSamples_` against absolute offsets makes segment lengths
exact (FR-064's non-accumulation requirement is satisfied *by construction* rather than by a carry
rule), keeps `getCurrentValue()` a pure function of `elapsedSamples_` — which is what makes FR-066's
latch rule and SC-018 hold with no extra machinery — and leaves the control-rate loop exactly one job:
taking the onset.

**Onset detection stays control-rate**, which is what SC-008(d) and SC-012 are written against: the
comparison `elapsedSamples_ >= periodSamples_` is evaluated only on control boundaries, so an onset
lands within one control step (32/44100 ≈ 0.73 ms) of its exact wall-clock time and the remainder is
carried forward — `elapsedSamples_ -= periodSamples_`, **never** `= 0` (FR-064). The residual
`r ∈ [0, 32)` means the first sample of an attack reads `riseShape(r/effA) ≈ 10·(r/effA)³` rather than
exactly 0: at the shortest configuration that is ≤ **2.4e-5**, at the defaults ≤ **2.4e-11**, and in
both cases well inside SC-009's 2.0e-3 slew bound. (Under the earlier raised-cosine draft the same
figures were 4.4e-4 and 4.4e-8.) **This is the only control-grid artefact in the output.**

### 3.4 Draw sequence (FR-052, FR-059, FR-060, FR-061, FR-067)

```cpp
    /// Draws exactly four nextUnipolar() values in the fixed order
    /// period, target, depth, polarity (FR-060) and latches the cycle. Called
    /// once per onset. NEVER called from a setter.
    void drawCycle() noexcept {
        const float uPeriod   = rng_.nextUnipolar();
        const float uTarget   = rng_.nextUnipolar();
        const float uDepth    = rng_.nextUnipolar();
        const float uPolarity = rng_.nextUnipolar();

        const float period = minIntervalSeconds_
                           + uPeriod * (maxIntervalSeconds_ - minIntervalSeconds_);
        periodSamples_ = static_cast<double>(period) * sampleRate_;

        active_.target = static_cast<std::uint8_t>(std::min<std::uint32_t>(
            static_cast<std::uint32_t>(targetCount_) - 1u,
            static_cast<std::uint32_t>(uTarget * static_cast<float>(targetCount_))));
        active_.depth    = minDepth_ + uDepth * (maxDepth_ - minDepth_);
        active_.polarity = (uPolarity <= bipolarProbability_) ? std::int8_t{-1}
                                                             : std::int8_t{+1};

        fitSegments();   // FR-055, against the CURRENTLY STORED configuration only
    }

    /// FR-055 fit rule. Uniform scaling preserves the envelope shape (hence C1)
    /// and is order-independent. Scaled against minIntervalSeconds_, NOT against
    /// the drawn period, so events never overlap for ANY draw (FR-053/FR-054).
    void fitSegments() noexcept {
        const double total = static_cast<double>(attackSeconds_)
                           + static_cast<double>(holdSeconds_)
                           + static_cast<double>(releaseSeconds_);  // >= 0.15 s, never 0
        const double scale = std::min(1.0,
                                      static_cast<double>(minIntervalSeconds_) / total);
        const double a = static_cast<double>(attackSeconds_)  * scale * sampleRate_;
        const double h = static_cast<double>(holdSeconds_)    * scale * sampleRate_;
        const double r = static_cast<double>(releaseSeconds_) * scale * sampleRate_;
        attackEndSamples_  = a;
        holdEndSamples_    = a + h;
        releaseEndSamples_ = a + h + r;
    }
```

**`nextUnipolar()` returns (0, 1]** (`random.h:50,67,89` — `next()` never yields 0). Two deliberate
consequences:

* `uPolarity <= bipolarProbability_` (**`<=`, not `<`**) makes `setBipolarProbability(0)` produce
  every-positive and `setBipolarProbability(1)` every-negative, exactly as the Edge Cases require.
* the target draw needs the `std::min` guard because `uTarget` can be exactly 1.0.

**Pre-roll (FR-067).** `initState()` rewinds the RNG and consumes **one** `nextUnipolar()` for the
pre-roll period, sets `firstOnsetPending_ = true` and leaves `active_` cleared:

```cpp
    void initState() noexcept {
        rng_.seed(configuredSeed_);
        const float u = rng_.nextUnipolar();
        periodSamples_ = static_cast<double>(
            minIntervalSeconds_ + u * (maxIntervalSeconds_ - minIntervalSeconds_))
            * sampleRate_;
        active_ = Event{};                 // target = kNoTarget, depth 0, polarity +1
        attackEndSamples_ = holdEndSamples_ = releaseEndSamples_ = 0.0;
        elapsedSamples_ = 0.0;
        samplesUntilControl_ = 0;
        firstOnsetPending_ = true;
    }
```

The scheduler therefore idles a full drawn period before its first onset and emits exactly `0.0f`
throughout (FR-067; `firstOnsetPending_` short-circuits `envelopeAt`). **The pre-roll is drawn from the
interval range in force at `prepare()`/`reset()` time** — see §3.7's configuration ordering rule, which
three success criteria depend on. Every *event* still consumes
exactly one draw per attribute in the fixed order — the pre-roll is not an event — which is what keeps
two runs differing only in a range setting RNG-aligned (SC-011). See §8.6.

`getPeriodSeconds()` returns `periodSamples_ / sampleRate_`; during the pre-roll that is the pre-roll
length, which *is* "the period drawn for the cycle currently in flight". Say so in its doc comment.

### 3.5 Advance (FR-003, FR-063)

```cpp
    void takeOnsetIfDue() noexcept {
        // Bounded loop: one iteration per onset the caller's clock has passed.
        // Terminates because periodSamples_ >= kMinIntervalSeconds * sampleRate_
        // > 0 always, so elapsedSamples_ strictly decreases each iteration.
        while (elapsedSamples_ >= periodSamples_) {
            elapsedSamples_ -= periodSamples_;   // FR-064: carry, never zero
            firstOnsetPending_ = false;
            drawCycle();                          // relatches period + envelope
        }
    }

    void process() noexcept {
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = static_cast<int>(kControlRateInterval);
            takeOnsetIfDue();
        }
        elapsedSamples_ += 1.0;
    }

    void processBlock(std::size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (samplesUntilControl_ <= 0) {
                samplesUntilControl_ = static_cast<int>(kControlRateInterval);
                takeOnsetIfDue();
            }
            const int advance = std::min(remaining, samplesUntilControl_);
            samplesUntilControl_ -= advance;
            remaining -= advance;
            elapsedSamples_ += static_cast<double>(advance);
        }
    }
```

Same decrement-then-check / check-then-chunk asymmetry as `BrownianDrift`
(`brownian_drift.h:178-206`), giving observational equivalence between `n` `process()` calls and one
`processBlock(n)`: both take the first control step at sample 0 and thereafter at identical absolute
sample indices, and `elapsedSamples_` accumulates the same integer total.

`processBlock(10'000'000)` at the pinned 1 s period costs ≈312 500 loop iterations and ≈208
`drawCycle()` calls — allocation-free, exception-free, terminating. `processBlock(0)` casts to
`remaining = 0` and the loop body never runs.

**`elapsedSamples_` precision.** It is reduced by subtraction every period, so it never exceeds
`kMaxIntervalSeconds × sampleRate = 600 × 96000 = 5.76e7` — 26 bits, exact in `double` for integer
increments and with ≈1e-8-sample resolution for the fractional carry. FR-007's "never `float`" matters
here: a `float` accumulator loses 1-sample resolution above 1.7e7 samples (≈350 s at 48 kHz), i.e.
within a *single* cycle at the top of the interval range.

### 3.6 Setters (FR-052, FR-055, FR-059, FR-060, FR-062, FR-066)

Every setter writes stored configuration and **nothing else** — no redraw, no refit, no state-machine
touch. FR-066's latch rule then holds by construction, and SC-018's setter storm cannot perturb the
output at all, because the output reads only `active_`, the four boundary offsets and
`elapsedSamples_`.

```cpp
    void setIntervalRange(float minSeconds, float maxSeconds) noexcept {
        float lo = sanitizeClamp(minSeconds, kMinIntervalSeconds, kMaxIntervalSeconds);
        float hi = sanitizeClamp(maxSeconds, kMinIntervalSeconds, kMaxIntervalSeconds);
        // FR-052: "if max < min after clamping, both collapse to min" -- min is the
        // MIN ARGUMENT, so setIntervalRange(90, 20) yields a fixed 90 s period, NOT
        // 20 s. Raising hi to lo (never lowering lo to hi) is the reading that
        // matches FR-052's words; the spec's Edge Case list says "20 s" and is
        // wrong -- see 8.8. The 5.2 edge-case row asserts 90/90.
        if (hi < lo) hi = lo;
        minIntervalSeconds_ = lo; maxIntervalSeconds_ = hi;
    }
    void setEnvelopeTimes(float a, float h, float r) noexcept {
        attackSeconds_  = sanitizeClamp(a, kMinSegmentSeconds, kMaxSegmentSeconds);
        holdSeconds_    = sanitizeClamp(h, kMinSegmentSeconds, kMaxSegmentSeconds);
        releaseSeconds_ = sanitizeClamp(r, kMinSegmentSeconds, kMaxSegmentSeconds);
    }
    void setDepthRange(float lo, float hi) noexcept {
        float a = sanitizeClamp(lo, 0.0f, 1.0f);
        float b = sanitizeClamp(hi, 0.0f, 1.0f);
        if (b < a) b = a;
        minDepth_ = a; maxDepth_ = b;
    }
    void setBipolarProbability(float p) noexcept {
        bipolarProbability_ = sanitizeClamp(p, 0.0f, 1.0f);
    }
    void setTargetCount(std::uint8_t c) noexcept {
        targetCount_ = std::clamp<std::uint8_t>(c, 1u, kMaxTargets);
    }
    void setSeed(std::uint32_t s) noexcept {            // FR-062
        configuredSeed_ = s; rng_.seed(s);
    }
```

**SC-011's exact-half claim is exact in `float`.** With `setDepthRange(0.15f, 0.5f)` vs
`(0.3f, 1.0f)` the drawn depths are `0.15f + u·0.35f` and `0.3f + u·0.7f`; `float(0.3) == 2·float(0.15)`
and `float(0.7) == 2·float(0.35)` bit-exactly (doubling only increments the exponent), so the peak
ratio is 0.5 to the last bit and the 1e-4 tolerance is enormous headroom.

**Worst-case per-sample output slew (analytic; the proof behind SC-009's third clause and SC-018).**
Smootherstep's peak derivative is `f'(½) = 15/8 = 1.875` per unit, i.e. `1.875/T` per second, so with
`|depth| ≤ 1` and the shortest legal effective segment `T = kMinSegmentSeconds = 0.05 s`:

```
max |out[n] - out[n-1]| = 1.875 / (0.05 * 48000) = 7.81e-4     (8.50e-4 at 44.1 kHz)
```

— 2.56× inside SC-009's 2.0e-3 bound, and independent of every setter, which is exactly SC-018's
claim. (The earlier raised-cosine draft's peak derivative was `½π = 1.5708`, giving 6.54e-4; the shape
change costs 19 % of the slew margin and buys the 5.6× cost reduction §3.3 needs for SC-014.)

**Interior curvature (SC-009's `interiorMax > 1.0e-5` clause).** Smootherstep's peak second derivative
is `max|f''| = 10/√3 = 5.7735` per unit², against the raised cosine's `½π² = 4.9348` — so the
decimated-grid interior second difference at 100 points per segment *rises* from ≈4.93e-4 to
**≈5.77e-4**, ~58× above the clause's threshold and ~4800× above the float32 noise floor. The shape
change strengthens SC-009's anti-vacuity clause rather than weakening it.

### 3.7 Prepare / reset (FR-002, FR-061, Clarifications Q4)

```cpp
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;   // brownian_drift.h:122
        initState();
    }
    void reset() noexcept { initState(); }
```

`prepare()` ≡ `reset()` in effect. The whole post-`reset()` event stream is bit-identical to the
post-`prepare()` stream because `initState()` rewinds the RNG to `configuredSeed_` before drawing
(FR-061). A mid-flight sample-rate change is a full re-initialisation, not a rescale — the invariant
that survives is that periods and segment times are expressed in **seconds**, which is what SC-012
measures across two *independent* runs.

**Configuration ordering rule — binding on every test and every consumer.** `initState()` draws the
FR-067 pre-roll period from `minIntervalSeconds_` / `maxIntervalSeconds_` **as they stand at that
moment** (§3.4). Therefore:

> **All interval-range configuration must be applied BEFORE `prepare()`, or `reset()` must be called
> after it.** Configuring after `prepare()` leaves the scheduler idling for a pre-roll drawn from the
> *previous* range, and no later setter shortens it (FR-066 latches).

This is not a theoretical hazard; it silently empties three criteria. Simulated this session with the
real `Xorshift32`/`nextUnipolar` (`random.h:50-68`) and the §3.3/§3.4 scheduler:

| Seed | `u` (first draw) | Pre-roll at the 20–90 s defaults |
|---|---|---|
| `0x51E7` (`kDefaultEventSeed`) | 0.303378 | **41.24 s** |
| `0x1` | 0.000063 | 20.00 s |
| `0x3039` | 0.776939 | **74.39 s** |
| `0xBEEF` | 0.923705 | **84.66 s** |
| `0xABCD` | 0.727923 | **70.96 s** |

SC-013's tracked window is 2 180 096 samples = 45.42 s at 48 kHz. Measured rising edges inside it:
**45** when `setIntervalRange(1, 1)` is applied before `prepare()` (pre-roll 1.000 s, onsets every 1 s),
and **1** when it is applied after `prepare()` with `kDefaultEventSeed` — against the row's
`REQUIRE(risingEdges >= 20u)`. The same ordering governs SC-014's cycle-inclusive figure (four
schedulers would sit in pre-roll idle for the whole measurement) and SC-009's short-segment
configuration (i) (which needs ≥ 10 events). Each of those three rows in §5.2 now states the rule
explicitly; the header banner states it once for consumers.

---

## 4. Offline evaluation harness (FR-081, FR-082)

**File:** `dsp/tests/unit/processors/vorago_p1_harness.cpp`
**TEST_CASE:** `VoragoPhase1_TrajectoryHarness`, tag `"[.harness][processors][vorago]"`. The leading
dot hides it from the default run (precedent: `dsp/tests/unit/effects/aether_reverb_perf_test.cpp:976`
uses `[.perf]`), so FR-082's "writes nothing when not selected" is satisfied by the tag, not by a flag.

Output directory comes from the CMake-injected macro (Clarifications Q8):

```cpp
#ifndef VORAGO_P1_HARNESS_DIR
#  error "VORAGO_P1_HARNESS_DIR must be injected by dsp/tests/CMakeLists.txt"
#endif
    const std::filesystem::path dir{VORAGO_P1_HARNESS_DIR};
    std::filesystem::create_directories(dir);
```

Five files, one row per control step, rendered with `processBlock(kControlRateInterval)` at 48 kHz:

| File | Source | Configuration | Duration | Rows | Columns |
|---|---|---|---|---|---|
| `vorago_p1_perlin_oct1.csv` | `PerlinNoiseSource` | `kDefaultRate`, octaves 1, depth 1, `kDefaultPerlinSeed` | 60 s | 90 000 | `timeSeconds,value` |
| `vorago_p1_perlin_oct2.csv` | " | as above, octaves 2 | 60 s | 90 000 | " |
| `vorago_p1_perlin_oct4.csv` | " | as above, octaves 4 | 60 s | 90 000 | " |
| `vorago_p1_aizawa.csv` | `ChaosModSource` (`Aizawa`) | speed 1.0, coupling 0 | 60 s | 90 000 | " |
| `vorago_p1_slow_events.csv` | `SlowEventScheduler` | all defaults, `kDefaultEventSeed` | **900 s** | 1 350 000 | `timeSeconds,value,target,phase` |

`timeSeconds` is written as `step * 32 / 48000.0` with `std::setprecision(6)` — never a running `float`
sum. The scheduler file is ≈40 MB; that is expected and is why the whole case is opt-in.

---

## 5. Test plan

### 5.1 New test files (all in `dsp/tests/unit/processors/`, all in `dsp_processors_tests`)

| File | Covers | Tags |
|---|---|---|
| `perlin_noise_source_test.cpp` | SC-001…SC-005, SC-013 (Perlin), FR-001/FR-006/FR-019 bindings, Perlin edge cases | `[processors][perlin][vorago]` |
| `chaos_mod_source_aizawa_test.cpp` | SC-006, SC-007, SC-013 (Aizawa), FR-036 counter, Aizawa edge cases | `[processors][chaos][aizawa][vorago]` |
| `slow_event_scheduler_test.cpp` | SC-008…SC-013 (scheduler), SC-018, FR-058/FR-067 read surface, scheduler edge cases | `[processors][slow_events][vorago]` |
| `vorago_p1_perf_test.cpp` | SC-014 | `[.perf]` (see below) |
| `vorago_p1_longrun_test.cpp` | SC-017 | `[processors][vorago][long]` |
| `vorago_p1_harness.cpp` | SC-015 | `[.harness][processors][vorago]` |

**Perf-case tag rule.** `VoragoPhase1_ControlRateCost` is tagged **`"[.perf]"`** — hidden from every
default run, opt-in via `dsp_processors_tests.exe "[.perf]"` — copying
`life_modulators_perf_test.cpp:147` (`TEST_CASE("LifeModulators_ControlRateCost", "[.perf]")`)
exactly. An earlier draft of this plan invented `[perf-gate]`, which would have enrolled a hard
wall-clock `REQUIRE` in the per-push lane on all three shared-runner OS legs: the CI filter is
`FILTER='~[performance]~[perf]~[benchmark]~[!benchmark]~[long]'` (`.github/workflows/ci.yml:366`,
`:638`, `:1063`), Catch2 tag exclusion is exact-match, and `perf-gate` matches none of those (it also
appears nowhere in `tools/` or `.github/`). GitHub-hosted runners are not timing-stable, so that is a
flaky-CI generator. SC-014 is therefore a **developer-run gate, not a CI gate** — run it in step 4 of
§9 and whenever the advance path changes; the `static_assert` on the baseline (§5.2) is the part that
CI compiles on every build and is what keeps the constant honest between runs.

**Allocation-override rule.** Only the first three need the detector; each includes
**`<allocation_detector.h>` only**. `<allocation_operator_overrides.h>` stays exclusively in
`brownian_drift_test.cpp:28` — `life_modulators_perf_test.cpp:19-23` documents why a second include is
a duplicate-symbol link error.

**Finiteness rule.** Every finiteness assertion uses `Krate::DSP::detail::isFinite`
(`core/db_utils.h:118`). Never `std::isnan/isinf/isfinite` — `tools/lint-nonfinite-symbols.js` gates it
and `-ffast-math` folds it away on the macOS leg.

**No bit-exact float goldens anywhere.** Determinism is asserted as exact equality of two captured
`std::vector<float>`s produced by the same binary in the same run (the Seraphis precedent, and what
`tools/lint-float-bit-goldens.js` permits) — never as a checked-in digest.

### 5.2 FR / SC → test map

| Criterion | File | `TEST_CASE` | Assertion strategy |
|---|---|---|---|
| **SC-001** Perlin boundedness (analytic) | perlin | `PerlinNoiseSource_NeverExceedsRange` | Corner grid {rate ∈ {`kMinRate`,`kMaxRate`}} × {octaves 1…4} × {depth 0, 0.5, 1} × 8 seeds, 300 s each. `REQUIRE(std::abs(v) <= 1.0f)` and `REQUIRE(detail::isFinite(v))` at every captured value; at depth 0, `REQUIRE(v == 0.0f)` exactly. Non-tautology, asserted only where `rate >= 0.1f`: `REQUIRE(peakDepth1 > 0.5)` and `REQUIRE(std::abs(peakDepthHalf / peakDepth1 - 0.5) < 1e-4)`. Drive the grid with `processBlock(32)` (one capture per control step — the bound is not rate-sensitive) so the case stays ≈10 s; the depth-scaling pair may share the same grid. **FR-006 binding (missing from an earlier draft):** `REQUIRE(src.getSourceRange() == std::pair{-1.0f, 1.0f})` inside the corner loop at depth 0, 0.5 **and** 1 — the fixed-range property is asserted only for the scheduler (SC-011) otherwise, and "does not shrink with depth" is exactly the clause a depth-scaled range would fail. |
| **FR-001 / FR-019 binding** | perlin, scheduler | `PerlinNoiseSource_SharedContract`, folded into `SlowEventScheduler_EdgeCases` | FR-001 has no assertion anywhere in an earlier draft — a class with the same two member functions but **no base** passes every other row, and FR-001 is the roadmap-traced requirement (roadmap line 162). In each TU: `static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, Krate::DSP::PerlinNoiseSource>)` (and the same for `SlowEventScheduler`), then bind the value once through a base handle — `ModulationSource& ms = src; REQUIRE(ms.getCurrentValue() == src.getCurrentValue()); REQUIRE(ms.getSourceRange() == std::pair{-1.0f, 1.0f});` — which is what proves the overrides are virtual dispatch and not shadowing. FR-019 defaults: after **default construction** and again after `prepare(48000.0)` with no configuration call, `REQUIRE(getRate() == PerlinNoiseSource::kDefaultRate)`, `getOctaves() == kDefaultOctaves`, `getDepth() == kDefaultDepth`, and `getCurrentValue() == 0.0f` (§1.4's `n(0) = 0` result). |
| **SC-002** bounded derivative | perlin | `PerlinNoiseSource_MaxSlewBounded` | 120 s **per-sample** at 48 kHz, `rate = kMaxRate`, `octaves = 4`, `depth = 1`, pinned seed. Compute `predicted` in the test from `kMaxSlope = 2.7`, `fbmFactor(4) = 32.0/15.0`, `alpha = 1 - std::exp(-5000.0/(5.0*48000.0))`, `gain = alpha/(1 - std::pow(1-alpha, 32))`, `predicted = gain*kMaxSlope*fbmFactor*rate*32/sr`. `REQUIRE(measured <= predicted)` **and** `REQUIRE(measured >= 0.5*predicted)`. Repeat at n = 1 with `fbmFactor(1) = 1`. Never accelerated. **The `5.0` is a literal, NOT `kOutputSmoothMs`** — computing `predicted` from the class constant makes the band self-referential: changing the constant rescales both edges and the criterion can never fail. Honest limits of the literal, measured this session: the step-response gain `α/(1−(1−α)³²)` is only weakly dependent on the smoothing time (it tends to `1/32 = 3.125e-2` as `α → 0`), so with the literal in place `kOutputSmoothMs = 20` yields `measured/predicted ≈ 0.799` and `= 100` yields `≈ 0.749` — both still inside the `[0.5, 1.0]` band. This row therefore catches **under**-smoothing only (`kOutputSmoothMs = 1` gives ≈2.42 and fails the upper edge); the over-smoothing half of FR-018 is carried by SC-003 clause (c)'s absolute floor (§5.3), which is where that burden belongs. |
| **SC-003** spectral rolloff | perlin | `PerlinNoiseSource_SpectralRolloff` | See §5.3 — measurement stated completely. |
| **SC-004** determinism + position independence | perlin | `PerlinNoiseSource_SeededDeterminism` | Base: two instances, same seed, 400 captured blocks → `REQUIRE(a == b)` on the vectors; different seeds → `REQUIRE(a != b)`; `reset()` reproduces run 1 exactly. **(a)** 60 s driven by the repeating block sequence `{37,1,64,512}` vs a pure `process()` render, per-sample `REQUIRE(std::abs(d) <= 1e-6f)`. **(b)** the SC-005 comparison doubles as the cross-rate discriminator. **(c)** same seed, both advanced to the same control-step count, `p1.setOctaves(1)`, `p4.setOctaves(4)` → `REQUIRE(p1.getOctaveValue(0) == p4.getOctaveValue(0))` bit-exactly. Outputs are **not** compared. |
| **SC-005** sample-rate invariance | perlin | `PerlinNoiseSource_SampleRateInvariant` | `rate = kDefaultRate`, `octaves ∈ {1,4}`, depth 1, same seed. 120 s per-sample at 44 100 and 96 000; linearly resample the 44.1 k run onto the 96 k grid; `REQUIRE(maxAbsDiff <= 2.0e-3)` and `REQUIRE(std::abs(rms44/rms96 - 1.0) <= 0.02)`. Mean compared **absolutely** — `REQUIRE(std::abs(mean44 - mean96) <= 2.0e-4)`, never relative (zero-mean trap). **The bound is 2.0e-4, not the spec's 0.1 span units:** at 0.1 span the clause is arithmetically vacuous, because this row's own per-sample bound of 2.0e-3 already implies a mean difference ≤ 2.0e-3, i.e. 50× tighter than the clause it is supposed to reinforce. Measured this session (per-sample render, 120 s, seed `0x9E37`, linear resample of the 44.1 kHz run onto the 96 kHz grid): `maxAbsDiff` = 1.705e-4 (n = 1) / 2.972e-4 (n = 4); `rms` ratio deviation 0.000 % / 0.001 %; `|mean44 − mean96|` = **1.208e-6** (n = 1) / **6.080e-6** (n = 4) — so 2.0e-4 is a 33× margin on a correct implementation while being 10× inside the per-sample bound, which is what makes it an independent check rather than a restatement. Not run at `kMaxRate` (the criterion says why). |
| **SC-006** Aizawa | aizawa | `ChaosModSource_AizawaBoundedAndChaotic` | 1 h accelerated (`processBlock(4096)`) at speeds {`kMinSpeed`, 0.1, 1, 5, `kMaxSpeed`}. **(a)** coupling 0: `|out| <= 1`, `detail::isFinite`, `REQUIRE(src.getDivergenceResetCount() == 0u)`. **(b)** `REQUIRE(maxAbs > 0.5 && maxAbs < 0.99)` and `REQUIRE(stddev > 0.1)` over the second half. **(c)** autocorrelation crosses 1/e within 60 s at every speed; **and** two instances at `kMaxSpeed` whose initial `x` differs by **exactly 1e-4** — the magnitude SC-006(c) names and at which its "RMS 0.21 by 10 s" figure was measured — reach RMS difference > 0.1 within 60 s. **Perturbation recipe, checked against the header:** the coupling path adds `coupling_ * inputLevel_ * 0.1f` (`chaos_mod_source.h:213-216`), so use `setCoupling(0.1f)` + `setInputLevel(1.0e-2f)` for exactly one control step, then `setCoupling(0.0f)`. Note the two traps: `setCoupling(1e-3f)`/`setInputLevel(0.1f)` displaces by **1e-5**, an order below the criterion; and the gate is `std::abs(inputLevel_) > 0.001f` (`:214`) — strict — so `setInputLevel(1.0e-3f)` fires **no** perturbation at all and the two instances stay bit-identical forever, silently passing nothing. **(d)** coupling 1.0 with `setInputLevel(1.0f)`: bound + finiteness + `getDivergenceResetCount() == 0` **only** — (b)/(c) are explicitly not asserted. Put §2.3's measured numbers in the test comments. |
| **FR-036** divergence counter (positive) | aizawa | `ChaosModSource_DivergenceCounterObservable` | The anti-vacuity partner of SC-006's three `== 0u` clauses, which an implementation that never increments passes in full (§2.2 item 5). `prepare(48000.0)`; `setModel(ChaosModel::Aizawa)`; `REQUIRE(src.getDivergenceResetCount() == 0u)`; then `setCoupling(1.0f)`, `setInputLevel(1.0e5f)` and 512 × `processBlock(32)` → `REQUIRE(src.getDivergenceResetCount() > 0u)` and `REQUIRE(detail::isFinite(src.getCurrentValue()))`. Then bind FR-036's zeroing rule in both directions: `src.reset(); REQUIRE(src.getDivergenceResetCount() == 0u);` and again after `prepare(48000.0)`. Repeat the whole case for `ChaosModel::Lorenz` — the counter is model-agnostic, and asserting it on a pre-existing model is what proves the increment sits in the shared guard rather than in an Aizawa-only branch. |
| **SC-007** no regression | aizawa + out-of-suite | `ChaosModSource_AizawaNoRegression` | In-TU: `REQUIRE(static_cast<uint8_t>(ChaosModel::Lorenz) == 0)` … `Henon == 3`, `Aizawa == 4`; `ChaosWaveshaper ws; ws.setModel(ChaosModel::Aizawa); REQUIRE(ws.getModel() == ChaosModel::Lorenz);`. In-suite: `dsp_primitives_tests` and `dsp_processors_tests` green with `chaos_waveshaper_test.cpp` unmodified. Warning gate: §5.4. |
| **SC-008** inter-event distribution | scheduler | `SlowEventScheduler_IntervalDistribution` | 32 seeds × 500 events, defaults, `processBlock(32)`; sample `getPeriodSeconds()` at each `isEventActive()` rising edge. (a) all 16 000 in [20, 90]; (b) `REQUIRE(std::abs(mean - 55.0) <= 2.0)`; (c) 10-bin histogram within ±15 % of 1600 per bin; (d) `setIntervalRange(30,30)` (before `prepare()`) → consecutive rising-edge spacing within one control step of 30 s **and** `getPeriodSeconds() == 30.0f` at every onset. **(e) FR-067 pre-roll — the clause an earlier draft omitted entirely.** FR-067 names SC-008 as its enforcing criterion, but (a)–(c) build the histogram from `getPeriodSeconds()` sampled *at onsets* and (d) measures onset-to-onset spacing, so an implementation that fires at `t = 0` (or draws the pre-roll from some other distribution) passes SC-008, SC-010, SC-011 **and** SC-012 unchanged — SC-012 compares two sample rates that would both shift identically. Bind it directly: immediately after `prepare(48000.0)` with the defaults, `REQUIRE(!s.isEventActive())`, `REQUIRE(s.getActiveTarget() == SlowEventScheduler::kNoTarget)`, `REQUIRE(s.getCurrentValue() == 0.0f)`; capture `p0 = s.getPeriodSeconds()` and `REQUIRE(p0 >= 20.0f && p0 <= 90.0f)`; advance and `REQUIRE` the first rising edge lands at `p0` within one control step, and that every sample before it is exactly `0.0f`. With `kDefaultEventSeed` the drawn `p0` is **41.236 s** (`u = 0.303378`, computed this session from the real `Xorshift32`, `random.h:50-68`) — quote it in the comment so a change of draw order is visible. Repeat over the 32 seeds of clause (a) so the case is not pinned to one lucky draw. |
| **SC-009** C1 at joins | scheduler | `SlowEventScheduler_EnvelopeC1AtJoins` | Per-sample at 48 kHz, never accelerated, two configurations: (i) `setEnvelopeTimes(kMinSegmentSeconds, kMinSegmentSeconds, kMinSegmentSeconds)` + `setIntervalRange(1,1)`; (ii) FR-055 defaults. **Configuration (i)'s two setters are applied BEFORE `prepare()`** (§3.7's ordering rule) — configured after, the FR-067 pre-roll is drawn from the 20–90 s default (41.24 s for `kDefaultEventSeed`) and a render sized for ten 1 s cycles sees one event. ≥ 10 events each. `stride = std::lround(std::min({effA,effH,effR}) * sr / 100.0)`, reported via `WARN`. Second difference on the decimated grid; a triple straddles a join when any of the four boundaries (onset, attackEnd, holdEnd, releaseEnd) lies in `[i-2, i]`. `REQUIRE(interiorMax > 1.0e-5)` (analytic interior figure under the §3.3 shape: ≈5.77e-4, ~58× the threshold); `REQUIRE(joinMax <= 5.0 * interiorMax)`; `REQUIRE(maxPerSampleDelta <= 2.0e-3)` at the default configuration (analytic worst case 7.81e-4, §3.6). **FR-058 read-surface identities**, sampled every control step across ≥ 3 events in configuration (ii) — three accessors Q5 admitted specifically because `getCurrentValue()`'s product destroys information, and which no other row reads: `REQUIRE(env >= 0.0f && env <= 1.0f)`; `REQUIRE(std::abs(getCurrentValue() - static_cast<float>(getActivePolarity()) * getActiveDepth() * getEnvelopeValue()) <= 1e-6f)` — this is what rejects the plausible wrong implementations (`getEnvelopeValue()` returning the polarity-signed value, or `depth * env`, or 0); `REQUIRE(getEnvelopeValue() == 0.0f)` while `!isEventActive()`; and `REQUIRE(std::abs(getEventDurationSeconds() - (getEffectiveAttackSeconds() + getEffectiveHoldSeconds() + getEffectiveReleaseSeconds())) <= 1e-6f)`. |
| **SC-010** scheduler determinism | scheduler | `SlowEventScheduler_SeededDeterminism` | Same seed → identical `std::vector<float>` over 400 captured blocks **and** identical `std::vector<std::uint8_t>` target sequence and `std::vector<std::int8_t>` polarity sequence; different seeds differ; `reset()` reproduces the post-`prepare` stream exactly (FR-061); `setSeed()` mid-event leaves that event's remaining samples bit-identical to an un-reseeded reference (FR-062). |
| **SC-011** output boundedness | scheduler | `SlowEventScheduler_BoundedOverLongRun` | 2 h accelerated (`processBlock(4096)`) across the corner grid (interval extremes, segment extremes, depth extremes, bipolar 0/0.5/1, targetCount 1/16, 8 seeds): `|out| <= 1`, `detail::isFinite`, `getActiveTarget()` ∈ `[0,targetCount) ∪ {kNoTarget}`, `getSourceRange() == std::pair{-1.f,1.f}`. Non-tautology: `setDepthRange(0.15f,0.5f)` vs `(0.3f,1.0f)`, same seed → `REQUIRE(std::abs(peakA/peakB - 0.5) < 1e-4)`. |
| **SC-012** sample-rate invariance | scheduler | `SlowEventScheduler_SampleRateInvariant` | `processBlock(32)` at 44 100 and 96 000, same seed. Record the wall-clock second of each of the first 50 onsets; `REQUIRE(std::abs(t44[i] - t96[i]) <= 32.0/44100.0)` for **every** i (a cumulative bound on onset position). Event count over a 30 min window identical. |
| **SC-013** zero allocations | perlin, scheduler, aizawa | `PerlinNoiseSource_NoAllocInProcess`, `SlowEventScheduler_NoAllocInProcess`, `ChaosModSource_AizawaNoAllocInProcess` | `prepare` + untracked warm-up **outside** the tracked scope, then 500 × `processBlock(512)` + 4096 × `process()` + 40 × `processBlock(48'000)`; `REQUIRE(detector.stopTracking() == 0u)`. Scheduler pinned to `setIntervalRange(kMinIntervalSeconds, kMinIntervalSeconds)` + `setEnvelopeTimes(kMinSegmentSeconds ×3)`, **both applied BEFORE `prepare()`** (§3.7's ordering rule — this is load-bearing, not stylistic): the FR-067 pre-roll is drawn inside `initState()` from the range in force at that moment, so configuring after `prepare()` leaves a 41.24 s pre-roll at `kDefaultEventSeed` and the whole 45.4 s window holds **1** rising edge. Additionally `REQUIRE(risingEdges >= 20u)` — with the correct ordering the tracked window is 2 180 096 samples = 45.42 s at 48 kHz, pre-roll 1.000 s, onsets every 1 s; **measured this session: 45 rising edges**, so the guard has ≈2.25× margin and cannot go flaky, while a return to the 20 s default would drop it to ≈2 and fail loudly. |
| **SC-014** control-rate cost | perf | `VoragoPhase1_ControlRateCost` | Four `PerlinNoiseSource` (4 octaves) + four `SlowEventScheduler` + one Aizawa `ChaosModSource`. **The measured block is advance + read, not advance alone.** Per block: one `processBlock(512)` on each of the nine instances, **then** an output-read step accumulated into a returned/`volatile` sink, copying `life_modulators_perf_test.cpp:124-130`'s `sumOutputs()` and its stated reason ("Read every output … so the optimizer cannot dead-code the advance away. A real consumer reads these once per block too"). The read rates differ by component and must match the contract each one publishes: the four Perlin sources and the Aizawa source are read **once per block** (their value is written inside the control-rate update, FR-003), the four schedulers **512 times per block** — per sample, which is what FR-065 defines and what SC-009 polls. Without that second rate the row measures almost nothing for four of the nine objects: §3.3 puts the envelope, the phase and the active flag entirely in the const getters, so `SlowEventScheduler::processBlock()` is integer counter arithmetic plus a rare `drawCycle()`. That is exactly the omission spec.md:872-878 forbids ("the draws, the four transitions and the raised-cosine evaluation would never appear in the measurement"); the pinned configuration fixes the draw-rate half only. Constants mirror `life_modulators_perf_test.cpp:54,58,70,73`: `kBlockBudgetNs = 512.0/48000.0*1e9`, `kReferenceNsPerBlock = kBlockBudgetNs * 0.001` (**0.1 %** = 10 667 ns), `kBaselineNsPerBlock` (provisional **5000.0**, tightened to the measured dev-machine figure), `kRegressionFactor = 1.5`. `static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock);` — the invariant `life_modulators_perf_test.cpp:60-66` makes explicit; with 5000 that is 7500 ≤ 10 667, and the checked-in baseline may never exceed 7111. **Feasibility, measured this session** (WSL g++ 13 `-O2`, 2e7 iterations, two runs): the 2048 per-block envelope evaluations cost ≈1780 ns with the §3.3 polynomial (0.86–0.87 ns each) and would cost ≈10 000 ns with `0.5−0.5*std::cos(π·u)` (4.84–4.94 ns each) — i.e. 0.94× the reference and 1.41× the 7111 ns ceiling from the rise shape alone, before the Perlin and Aizawa work. The 5000 ns provisional baseline is reachable only under the polynomial. **Both** schedulers' configurations are applied **before `prepare()`** (§3.7). **Two** figures `WARN`-reported (idle-path at defaults; cycle-inclusive at the pinned 1 s / 0.05 s configuration); the gate applies to the **cycle-inclusive** figure. Tagged `[.perf]`, developer-run — see §5.1. |
| **SC-015** harness | harness | `VoragoPhase1_TrajectoryHarness` | Writes the five files; then re-opens each by name, `REQUIRE`s a header line, that every subsequent field parses as a number, and that `lastTimeSeconds` is within one control step of the stated duration. For `vorago_p1_slow_events.csv`: `REQUIRE(risingEdges >= 3)` (phase leaving 0) and at least one row with `target != 255`. The "default suite writes no files" half is carried by the `[.harness]` tag plus a manual default-suite run against an empty output dir. |
| **SC-016** repo gates | — | — | `node tools/check-portability.js`, `lint-layers.js`, `lint-odr.js`, `lint-nonfinite-symbols.js`, `lint-float-bit-goldens.js` all exit 0; `./tools/run-clang-tidy.ps1 -Target dsp` reports no new diagnostics. **See §6 items 3 and 5** — both the clang-tidy and the nonfinite clauses are *vacuous* for new files until the lint stub and the `GUARDED` list are updated. |
| **SC-017** long-run resolution | longrun | `VoragoPhase1_LongRunResolution` | 8 h accelerated (`processBlock(4096)`) at 48 kHz: `PerlinNoiseSource` at `kDefaultRate` and at `kMaxRate` (4 octaves) and `SlowEventScheduler` at defaults. **The measurement windows are 1 h, not 60 s, and the RMS/ZCR clauses apply to the Perlin renders only** — see §8.9 for the derivation and the measured evidence; SC-017 as written in the spec fails on a correct implementation and is undefined for the scheduler. **Perlin (both rates):** capture control-step values over the first hour and the last hour (5 400 000 points each) — (a) `REQUIRE(std::abs(rmsLast/rmsFirst - 1.0) <= 0.20)`, (b) same for zero-crossing *rate*. **Scheduler:** no RMS or ZCR clause; instead (d) `REQUIRE(std::abs(double(eventsHour8)/double(eventsHour1) - 1.0) <= 0.20)` with `REQUIRE(eventsHour1 >= 10u)` as the anti-vacuity guard, `REQUIRE(p >= 20.0f && p <= 90.0f)` on `getPeriodSeconds()` at the end, plus a **liveness** clause over a window guaranteed to contain ≥ 10 events — `REQUIRE(rmsLast900s > 0.0)` computed over the final 900 s (≈16 events at the 55 s mean). (c) `detail::isFinite` on every value of every render, checked inside the render loop so nothing is buffered for 8 h. Because the render is 8 h × 3 sources, keep only the four windowed buffers, not the whole trajectory. Tagged `[long]` — multi-second, and its assertions are toolchain-independent (the CLAUDE.md rule for that tag). |
| **SC-018** setter storm | scheduler | `SlowEventScheduler_SetterStormContinuity` | Per-sample at 48 kHz. Advance to mid-attack, then once per control step for the rest of the cycle call all five setters with deliberately different values (cycling a small deterministic table). `REQUIRE(maxPerSampleDelta <= 2.0e-3)` across the whole storm including the joins it crosses, **and** `REQUIRE` the three effective-segment getters and `getPeriodSeconds()` are unchanged for the event's duration — the direct FR-066 observation. |
| **FR-002 / FR-019 / Edge Cases** | perlin, scheduler | `PerlinNoiseSource_EdgeCases`, `SlowEventScheduler_EdgeCases` | `processBlock(0)` is a no-op (state compare); `processBlock(10'000'000)`; advance before `prepare()`; `prepare()` twice and mid-event; `setRate(0 / 1e9 / -1 / Inf / NaN)` clamps (NaN → `kMinRate` via `sanitizeClamp`); `setOctaves(0 / 99)`; **`setIntervalRange(90,20)` collapses to a fixed 90 s period** — `REQUIRE(getMinIntervalSeconds() == 90.0f && getMaxIntervalSeconds() == 90.0f)`, per FR-052's "both collapse to `min`" where `min` is the min *argument*, which is what §3.6's `if (hi < lo) hi = lo;` implements. (An earlier draft of this row asserted "collapses to 20", contradicting the code beside it and failing on the planned header; the spec's Edge Case list says 20 s and is likewise wrong — recorded in §8.8.); `setEnvelopeTimes(0,0,0)`; `setEnvelopeTimes(300,300,300)` with the default range → effective 6.67 s each at the next onset, then `setIntervalRange(600,600)` → 200 s each at the onset after, asserted **order-independent** (both call orders give the same effective times); `setTargetCount(0)` → 1; `prepare(0.0)` / `prepare(-1.0)` → finite output; `setSeed(0)` gives the same stream as `setSeed(2463534242u)`; adjacent seeds `n`/`n+1` cross-correlate < 0.2 over 300 s; 32 distinct seed pairs give distinct streams. |

### 5.3 SC-003 measurement — stated completely

The 65 536-point transform uses a **local double-precision iterative radix-2 FFT inside the test TU**
(~35 lines), not `Krate::DSP::FFT`: `fft.h:47` documents `kMaxFFTSize = 8192` while `prepare()`
(`:147-167`) validates only power-of-two, so passing 65 536 would rely on an undocumented path, and the
criterion's −129/−84 dB reference figures sit near a float32 FFT's noise floor at that length. There is
no RT constraint in a test, so `std::vector<double>` is fine.

* **Clauses (a), (b), (d) — `rate = 0.1 Hz`, depth 1, 48 kHz.** Render 600 s with
  `processBlock(kControlRateInterval)`, capturing one point per control step (1500 Hz); decimate by 15
  to **100 Hz** ⇒ 60 000 points. (The fastest content at n = 4 is `0.1 × 2³ = 0.8 Hz`, 60× below the
  50 Hz Nyquist, so no anti-alias filter is needed.) **Hann window — mandatory**: a rectangular
  window's leakage skirt would put transform-artefact energy above the band edges and break the 99 %
  figure for reasons that have nothing to do with the signal. Zero-pad to 65 536 ⇒ 1.526 mHz bins.
  * **(a)** at **every** n ∈ {1,2,3,4}: `REQUIRE(energyBelow(8*rate)/totalEnergy >= 0.99)` and
    `REQUIRE(maxBinDbAbove(32*rate) <= peakDb - 30.0)`.
  * **(b)** `fracAbove(4*rate)` strictly increasing over n = 1…4, and `REQUIRE(frac[3] >= 10.0*frac[1])`.
  * **(d)** at n = 1: autocorrelation of the decimated trajectory at lag `0.1/rate` s > 0.7, and
    `|autocorrelation|` at lag `2/rate` s < 0.35. Lag-1 is deliberately **not** used.
* **Clause (c) — `rate = kMaxRate` (5 Hz), same monotonicity assertion.** The decimation stride must
  change here: the top octave sits at 40 Hz and the band edge `32 × rate` is 160 Hz, both at or above a
  100 Hz grid's 50 Hz Nyquist, so re-using stride 15 would alias and measure the transform rather than
  the signal. Use the **undecimated control-rate trajectory (1500 Hz)**, first 65 536 points (43.7 s),
  Hann-windowed, no zero-padding ⇒ 22.9 mHz bins and a 750 Hz Nyquist.

  **This clause needs an ABSOLUTE floor, because the monotonicity it inherits from (b) cannot fail.**
  SC-003(c) is the sole enforcing criterion for FR-018's `kOutputSmoothMs = 5.0f` decision ("a 20 ms
  smoother … would delete exactly the roughness FR-014 exists to provide"), and monotonicity of
  `fracAbove` in the octave count is preserved by *any* low-pass — each added octave still adds HF
  energy, and the smoother scales numerator and denominator together. Measured this session (the
  §1.3 lattice math + the real `OnePoleSmoother` coefficient `exp(−5000/(ms·sr))`, `smoother.h:88-92`,
  advanced 32 samples per control step; 65 536-point double FFT, Hann; `rate = 5`, seed `0x9E37`),
  `fracAbove(4·rate = 20 Hz)` for n = 1…4:

  | `kOutputSmoothMs` | n = 1 | n = 2 | n = 3 | n = 4 | monotonic? |
  |---|---|---|---|---|---|
  | 1 | 1.027e-6 | 2.279e-5 | 8.544e-4 | 7.067e-3 | yes |
  | **5 (specified)** | 1.003e-6 | 2.230e-5 | 8.375e-4 | 6.878e-3 | yes |
  | 20 | 7.666e-7 | 1.691e-5 | 6.438e-4 | 4.940e-3 | yes |
  | 100 | 1.251e-7 | 2.807e-6 | 1.145e-4 | 7.677e-4 | yes |

  Monotonic at every value, including 100 ms — so the clause as previously planned is green on a
  smoother 20× too slow. The additional assertion is therefore an absolute floor, taken at the band
  edge where the 5 ms / 20 ms separation is widest relative to the leakage floor —
  **`fracAbove(8·rate = 40 Hz)` at n = 4, `REQUIRE(frac40 >= 1.30e-4)`**:

  | seed | 5 ms | 20 ms | ratio |
  |---|---|---|---|
  | `0x9E37` (`kDefaultPerlinSeed`) | 2.012e-4 | 9.588e-5 | 2.10 |
  | `0x1` / `0x7` / `0x3039` | 1.815e-4 / 1.706e-4 / 2.003e-4 | 8.609e-5 / 7.941e-5 / 9.456e-5 | 2.11 / 2.15 / 2.12 |
  | `0x3E7` / `0xBEEF` / `0x51E7` / `0xABCD` | 1.822e-4 / 1.981e-4 / 1.748e-4 / 1.719e-4 | 8.616e-5 / 9.391e-5 / 8.233e-5 / 8.057e-5 | 2.11 / 2.11 / 2.12 / 2.13 |

  1.30e-4 sits **1.31× below** the worst 5 ms seed and **1.36× above** the worst 20 ms seed — a
  balanced, seed-stable threshold on a fully deterministic measurement. (The obvious alternative, a
  floor on `fracAbove(20 Hz)`, separates the two by only 1.39× — 6.878e-3 vs 4.940e-3 — and any floor
  between them has < 1.25× margin on both sides; 40 Hz is chosen for that reason, not for a rounder
  number.) Write the measured figures into the test comment, and **verify the floor by injecting
  `kOutputSmoothMs = 20.0f` once and confirming the case goes red** before committing — an absolute
  threshold nobody has seen fail is the same defect one layer down. Recorded as a refinement in §8.1
  and a spec correction in §8.10.

### 5.4 SC-007's out-of-suite warning gate

`tools/check-portability.js` cannot catch `-Wswitch`: it invokes `g++ … -fsyntax-only -DNDEBUG
-DRELEASE` with **no warning flags and no `-Werror`** (`:229-231`), and `isCheckable()` accepts only
`.cpp|.cc` (`:203-208`) while the FR-034 change lives entirely in headers. Two commands replace it.

Create `dsp/tests/portability/chaos_enum_exhaustiveness.cpp` — a four-line TU that includes both
headers and defines nothing — then run under WSL from the repo root:

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

Both must exit 0. (Include roots taken from `tools/check-portability.js:39-50`.) The TU is **not**
added to any CMake target — it exists solely for these two commands.

The MSVC leg is a build-log diff: capture
`cmake --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_processors_tests`
to a log **before** and **after** the change and diff for new `C####` lines; zero additions required.
Capture to a file on the first run — never re-run a build just to look at its output.

---

## 6. Build integration

1. **`dsp/tests/CMakeLists.txt` — source list.** Insert the six new files before the closing `)` at
   `:287` (after `unit/processors/diffusion_network_zeromod_test.cpp` at `:286`), under a comment
   banner matching the Seraphis Phase 4 precedent at `:284`:

   ```cmake
       # Vorago Phase 1 (specs/vorago-phase1-events-modulation)
       unit/processors/perlin_noise_source_test.cpp
       unit/processors/chaos_mod_source_aizawa_test.cpp
       unit/processors/slow_event_scheduler_test.cpp
       unit/processors/vorago_p1_perf_test.cpp
       unit/processors/vorago_p1_longrun_test.cpp
       unit/processors/vorago_p1_harness.cpp
   ```

2. **`dsp/tests/CMakeLists.txt` — harness output dir (FR-081, Clarifications Q8).** Beside the existing
   `KRATE_DSP_TESTS_DIR` definition at `:458-459`:

   ```cmake
   # Vorago Phase 1 FR-081: the [.harness] trajectory writer resolves its output
   # directory from this macro, so CSVs land in the build tree regardless of the
   # launch working directory (same rationale as KRATE_DSP_TESTS_DIR above).
   target_compile_definitions(dsp_processors_tests
       PRIVATE VORAGO_P1_HARNESS_DIR="${CMAKE_BINARY_DIR}/vorago_p1/")
   ```

3. **`dsp/lint_all_headers.cpp`** — add the two new headers to the alphabetical Layer 2 block
   (`chaos_mod_source.h` is at `:95`, `pattern_scheduler.h` at `:120`):
   `#include <krate/dsp/processors/perlin_noise_source.h>` and
   `#include <krate/dsp/processors/slow_event_scheduler.h>`, each in its alphabetical slot. Without
   this, `run-clang-tidy.ps1 -Target dsp` never sees the new headers and SC-016's clang-tidy clause is
   vacuous. (`brownian_drift.h` is *not* in this file — a pre-existing gap from Seraphis Phase 1. Do
   **not** fix it here; out of scope.)

4. **`dsp/CMakeLists.txt`** — the `KRATE_DSP_PROCESSORS_HEADERS` list (`:128`, consumed by
   `target_sources(KrateDSP PRIVATE …)` at `:198`) is **IDE-visibility only** and is already partial
   (`brownian_drift.h`, `spline_trajectory.h`, `chaos_mod_source.h` are all absent). Following the
   Seraphis Phase 1 precedent: **no change**.

5. **`tools/lint-nonfinite-symbols.js`** — add **all eight** of the following to the `GUARDED` array
   (`:82-92`), repo-relative with forward slashes, under a `// Vorago Phase 1 (SC-016)` banner
   matching the Seraphis Phase 5/6 banners above it:

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

   The gate covers an **explicit list**, not a tree walk (`lint-nonfinite-symbols.js:43-52`, and the
   `GUARDED` declaration at `:82`), so every file left off it is unguarded. An earlier draft listed
   only the first two headers and three of the test TUs, which left the two files that carry the
   phase's *longest-running* finiteness assertions — SC-017(c)'s 8 h check in
   `vorago_p1_longrun_test.cpp` and SC-014's in `vorago_p1_perf_test.cpp` — outside the gate: a
   `std::isnan` there folds to `false` on the macOS `-ffast-math` leg with nothing red, which is
   precisely the vacuity SC-016 is supposed to close. `chaos_mod_source.h` is added because **this
   phase modifies it** (§2.2); it is not on the list today.
   (`vorago_p1_harness.cpp` is deliberately **absent**: it asserts parseability, not finiteness, and
   adding a file with no non-finite surface only dilutes the list's meaning. Add it if the harness
   ever grows a finiteness check.)

6. **No new CMake test target.** `dsp_processors_tests` is the only target whose sources change and it
   is already registered with `catch_discover_tests` at `:797`. `dsp_primitives_tests`,
   `dsp_systems_tests` and the Ruinae / Disrumpo plugin test targets **recompile** because
   `chaos_waveshaper.h` / `chaos_mod_source.h` changed, and must be rebuilt and rerun (SC-007).

**Build and run commands** (always the full CMake path on Windows):

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests dsp_primitives_tests dsp_systems_tests

build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe  2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe     2>&1 | tail -5

# focused iteration
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "PerlinNoiseSource_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SlowEventScheduler_*" 2>&1 | tail -5

# harness (opt-in only)
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[.harness]" 2>&1 | tail -5
```

Redirect long runs to a log file on the **first** run; never re-run a suite just to re-read its output.

---

## 7. Risks and mitigations

| # | Risk | Why it is real | Mitigation |
|---|---|---|---|
| R1 | **`OnePoleSmoother` completion snap** (`smoother.h:55,199-201,251-253`) silently converts the Perlin output to a raw staircase at low rates. | `kCompletionThreshold = 1e-4` exceeds the per-control-step change whenever `rate·fbmFactor ≲ 0.03 Hz`. | Documented in §1.6 and in the header banner. SC-002 pins the worst case, where the snap provably never fires; in the snapped regime the step is ≤ 1e-4, 20× inside the threshold. No code change. |
| R2 | **Perlin lattice-index wrap** would produce an exact repeat of the whole trajectory while passing every criterion (SC-017's RMS and ZCR clauses are satisfied by a perfect loop). | FR-007's general wrap permission invites it. | FR-007 carries an explicit carve-out; the plan uses an unwrapped `double` position and `std::int64_t` index (§1.3). The reachable index is ≤ 1.15e6 at `kMaxRate` over 8 h — 1800× inside the hash's 2³² salt period (§0.1). |
| R3 | **Aizawa `baseDt_` mis-set → dead modulator.** `dt ≥ 0.02` collapses to `x = y = 0` with output identically 0, silently, from every initial state. | Measured this session (§2.3). An earlier draft's `0.01f` gives `dt = 0.2` at `kMaxSpeed`. | `baseDt_ = 5.0e-4f` (`dt ∈ [2.5e-5, 1.0e-2]`, 1.5× margin to the collapse edge) plus SC-006(b)'s `σ > 0.1` and `0.5 < max|out| < 0.99` clauses, the only assertions that can catch it. |
| R4 | **`safeBound_` too small → the guard fires under legal coupling**, resetting the attractor thousands of times per render and turning the modulator into a sawtooth. | Measured: `safeBound_ = 5.0f` (the Chua value) fires ≈2000× per 600 s at `kMinSpeed`, coupling 1. | `safeBound_ = 25.0f` (threshold 250) against a measured worst case of 111.9. `getDivergenceResetCount()` (FR-036) makes the failure observable at all — without it the clause is unwritable against the public API. |
| R5 | **Shared-enum `-Wswitch` breaks the GCC/Clang CI legs** while MSVC stays green. | `chaos_waveshaper.h:640,685` have no `default:` arm; `check-portability.js` structurally cannot see it (`:203-208`, `:229-231`). | Grouped `case ChaosModel::Aizawa:` labels (§2.1) plus the explicit `-Wall -Wextra -Wswitch -Werror` WSL commands in §5.4. Verified this session that **no plugin switches over `ChaosModel`**, so the blast radius really is only those two sites. |
| R6 | **`std::clamp` propagates NaN**, so "non-finite input → clamped" quietly fails and a NaN rate poisons `cellsPerControlStep_` forever. | Edge Cases require the clamp; `std::isnan` is forbidden and folds under `-ffast-math`. | `sanitizeClamp` (§0.3) built on `detail::isNaN` (`db_utils.h:99`), which survives fast-math via an optimisation barrier. Asserted in the edge-case tests. |
| R7 | **`float` time accumulators freeze the components after hours** — the exact failure SC-017 exists to catch. | A `float` sample counter loses 1-sample resolution above 1.7e7 samples (≈350 s at 48 kHz). | Every accumulator is `double` (§1.2, §3.2); the scheduler's is additionally bounded by construction (subtracted each period, ≤ 5.76e7). SC-017 is a numbered criterion, not a note. |
| R8 | **Denormals** in the Perlin smoother state during long near-zero stretches. | `positionCells_` can sit in a flat region for minutes at `kMinRate`. | `OnePoleSmoother::process()` already calls `detail::flushDenormal` (`smoother.h:208`), and `dsp_test_main.cpp` enables FTZ/DAZ for the suite. No extra guard needed — noted so nobody adds a redundant one. |
| R9 | **SC-014's gate goes self-referential** if the checked-in baseline is set from an unconstrained measurement. | The precedent warns about exactly this (`life_modulators_perf_test.cpp:60-66`). | `static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock)` in the test TU; the baseline may never exceed 7111 ns. Both figures `WARN`-reported. |
| R10 | **SC-003 aliasing at `kMaxRate`** if the 0.1 Hz decimation stride is reused. | Top octave 40 Hz and band edge 160 Hz against a 50 Hz Nyquist. | Clause (c) uses the undecimated 1500 Hz control trajectory (§5.3). Recorded as a refinement in §8.1. |
| R11 | **65 536-point FFT via `Krate::DSP::FFT`** relies on an undocumented path (`kMaxFFTSize = 8192`, `fft.h:47`). | `prepare()` validates only power-of-two, so it happens to work today. | Local double-precision radix-2 FFT in the test TU (§5.3). No dependency on library behaviour outside its documented range. |
| R12 | **Duplicate `operator new/delete` symbols** if a new allocation test includes the overrides header. | Three new TUs need the detector. | Include **only** `<allocation_detector.h>`; `brownian_drift_test.cpp:27-28` remains the single owner (`life_modulators_perf_test.cpp:19-23`). |
| R13 | **SC-009's `joinMax ≤ 5 × interiorMax` degrades** if a per-phase clock design is chosen. | A carry-based per-phase clock adds a value step at Hold→Release of `10·(r/effR)³` = 2.4e-5 in the shortest configuration under the §3.3 polynomial (it was 4.4e-4 under the earlier raised-cosine draft, which is what originally made this decisive). | The single-clock design (§3.3) removes the step entirely; only the ≤ 2.4e-5 onset artefact remains, one-sided and bounded. The design is now kept for FR-064/FR-066-by-construction rather than for the join margin — see §3.3. |
| R14 | **SC-001's corner grid is expensive** if rendered per sample: 2 rates × 4 octaves × 3 depths × 8 seeds × 300 s at 48 kHz ≈ 1.4e10 samples. | Naive reading of "300 s per-sample render". | Drive the grid with `processBlock(32)`, one capture per control step — the bound, finiteness and depth-scaling clauses are not per-sample-rate-sensitive (unlike SC-002/SC-005/SC-009/SC-012, which the spec explicitly forbids accelerating). ≈3.4e7 control steps total. |
| R15 | **A transcendental in `getCurrentValue()` blows SC-014's whole budget**, because that getter is a per-sample call (FR-065) and nothing caches it. | Measured this session (WSL g++ 13 `-O2`, 2e7 iterations, two runs): `0.5−0.5*std::cos(π·u)` costs 4.84–4.94 ns, so SC-014's 4 schedulers × 512 samples = 2048 evaluations ≈ **10 000 ns/block** — 0.94× the 10 667 ns reference and 1.41× the 7111 ns baseline ceiling, before the four Perlin sources and the Aizawa source. The roadmap's 4–8 voices × "multiple schedulers per voice" (line 167) multiplies it further. | §3.3's smootherstep rise shape: 0.86–0.87 ns/eval (5.6× cheaper, ≈1780 ns/block), C2 rather than C1, and the same polynomial the Perlin lattice already uses. FR-056 amended in §8.7; the slew and curvature consequences are re-derived in §3.6 and stay inside every SC-009/SC-018 bound. |
| R16 | **Configuration applied after `prepare()` silently empties SC-013, SC-014 and SC-009(i)** — the FR-067 pre-roll is drawn inside `initState()` from the range in force at that moment, and FR-066 means no later setter shortens it. | Measured: with `kDefaultEventSeed` the default-range pre-roll is 41.24 s, so SC-013's 45.42 s tracked window holds **1** rising edge instead of 45 against a `>= 20` guard. Three criteria fail, and the "obvious" repair is to lower the guard. | §3.7 states the ordering rule as a binding contract with the measured pre-roll table; the SC-009, SC-013 and SC-014 rows each repeat it; the header banner states it for consumers. |
| R17 | **A perf case with a wall-clock `REQUIRE` lands in the per-push CI lane** and goes flaky on shared runners. | The filter is `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` (`ci.yml:366,638,1063`) and Catch2 tag exclusion is exact-match, so an invented tag such as `[perf-gate]` (0 hits in `tools/` or `.github/`) is *not* excluded — it runs on all three OS legs. | `"[.perf]"`, matching `life_modulators_perf_test.cpp:147` exactly (§5.1). SC-014 is a developer-run gate; the compiled `static_assert` is the part CI enforces continuously. |

---

## 8. Deviations, refinements and spec corrections recorded here

1. **SC-003 clause (c) decimation stride** (§5.3, R10). The spec says "same measurement repeated at
   `rate = kMaxRate`"; the 100 Hz grid it specifies for the 0.1 Hz case would alias at 5 Hz. The plan
   uses the undecimated 1500 Hz control trajectory for clause (c) and asserts only the monotonicity the
   clause scopes itself to. **No threshold changes.**
2. **Aizawa collapse fixed-point sign** (§2.3). The spec body records "z ≈ 1.105"; the measured Euler
   fixed point at `dt ≥ 0.02` is **z ≈ −1.105** (the root of `0.6 + 0.95z − z³/3 = 0` the trajectory
   actually lands on). Cosmetic — no requirement depends on the sign — but the header comment should
   record the measured value.
3. **Aizawa `max|out|` at `kMinSpeed`** measures **0.750**, marginally below the spec's stated
   0.760–0.780 band. SC-006(b)'s actual gate is `0.5 < max|out| < 0.99`, so nothing changes; the test
   comment should quote 0.750–0.780.
4. **`ChaosModSource::prepare()` gains the 1 Hz floor** (§2.2 item 6) so FR-002 is literally true for
   all three sources. Behaviourally inert — `sampleRate_` is stored but never read by the attractor
   math (`:211`).
5. **Scheduler phase / active / envelope are derived per sample** from `elapsedSamples_` rather than
   transitioned at control rate (§3.3). FR-065 assigns Idle/Attack/Hold/Release transitions to the
   control-rate loop; deriving them is strictly more accurate (zero lag rather than ≤ one control step
   of lag), sits inside every criterion's tolerance, and is what removes SC-009's join step. The
   **onset** remains control-rate — that is the transition every criterion actually measures.
6. **The pre-roll draw consumes one `nextUnipolar()`, not four** (§3.4). FR-060's "each event consumes
   exactly one draw per attribute in that fixed order" is satisfied for every *event*; the pre-roll idle
   is not an event. SC-011's RNG-alignment argument is unaffected because the draw order is identical
   in both compared runs.
7. **FR-056's rise shape is a C2 polynomial, not a raised cosine** (§3.3). *Amend FR-056 to read "a
   C1 rise shape with zero first derivative at both ends"* and name smootherstep
   `f(c) = 6c⁵ − 15c⁴ + 10c³` as the implementation; the roadmap's requirement is "event envelope
   continuity (C1, no clicks)" (roadmap line 171), which the polynomial satisfies strictly more than
   the cosine does (its second derivative vanishes at both ends too). **Reason this is not cosmetic:**
   FR-065 makes `getCurrentValue()` a per-sample call, and nothing caches it, so the shape function is
   evaluated 2048 times per 512-sample block in SC-014's four-scheduler workload. Measured this
   session (WSL g++ 13 `-O2`, 2e7 iterations, two runs): `0.5 − 0.5·std::cos(π·u)` costs
   4.84 / 4.94 ns per evaluation ⇒ ≈10 000 ns/block, which is **0.94× SC-014's 10 667 ns reference
   and 1.41× the 7111 ns ceiling its `static_assert` imposes on the baseline** — before the four
   `PerlinNoiseSource` instances and the Aizawa source are counted, i.e. SC-014 is unattainable with a
   cosine. The polynomial costs 0.86 / 0.87 ns ⇒ ≈1780 ns/block. Consequences, all re-derived and all
   inside their bounds: peak slope 1.875/T instead of ½π/T ⇒ worst-case per-sample slew **7.81e-4**
   (was 6.54e-4) against SC-009/SC-018's 2.0e-3; peak second derivative 10/√3 instead of ½π² ⇒
   decimated interior second difference **≈5.77e-4** (was ≈4.93e-4) against SC-009's
   `interiorMax > 1.0e-5`, i.e. the anti-vacuity clause gets *stronger*; onset residual artefact
   **≤ 2.4e-5** (was ≤ 4.4e-4). SC-003's and FR-018's smoother analysis is untouched — that concerns
   `PerlinNoiseSource`, which never had a cosine. **No threshold is relaxed by this change.**
8. **`setIntervalRange(90, 20)` collapses to a fixed 90 s period, not 20 s** (§3.6, §5.2 edge row).
   FR-052 says "if `max < min` after clamping, both collapse to `min`" and `min` is the min
   *argument*, so raising `hi` to `lo` is the implementation FR-052 describes. *The spec's Edge Cases
   list (spec.md:953) says "collapses to the fixed 20 s period" and is wrong* — it contradicts FR-052
   in the same document. An earlier draft of this plan carried the contradiction into the test row,
   which would have failed against the header printed three sections above it. Corrected in both
   places here; the spec's Edge Case line should be amended to 90 s.
9. **SC-017's measurement windows are 1 h (Perlin) and the RMS/ZCR clauses do not apply to the
   scheduler at all** (§5.2 SC-017 row). SC-017 as specified fails on a correct implementation and is
   arithmetically undefined for the scheduler. Simulated this session with the real `deriveStreamSeed`
   / `Xorshift32` (`random.h:50-68,102-111`), the §1.3 lattice math and the §3.3/§3.4 scheduler, 8 h at
   48 kHz, `processBlock`-equivalent control-step capture:
   * **Perlin at `kDefaultRate`, 60 s windows (as specified):** 60 s at 0.1 Hz is **6 lattice cells**
     and 11–21 zero crossings, so both statistics are sampling noise. Over 8 seeds the RMS deviation
     reaches **30.7 %** (seed `0x9E37`: 0.3311 → 0.2294) and the ZCR deviation **100 %** (seed
     `0x51E7`: 11 → 22) against a 20 % bound — 5 of 8 seeds fail on one clause or the other.
   * **Perlin, 1 h windows (the fix):** 360 cells at `kDefaultRate`. Worst over the same 8 seeds:
     RMS **5.3 %**, ZCR **7.7 %** — a 2.6× margin. At `kMaxRate` the same windows give ≤ 0.6 % / 0.7 %.
     The clauses keep their 20 % thresholds; only the window changes, and a frozen accumulator still
     drives both statistics to 0.
   * **Scheduler, 60 s windows:** the output is 0 except during an event, so a 60 s window holds 0–3
     zero crossings, and the FR-067 pre-roll (drawn from 20–90 s) exceeds 60 s for 3 of 8 seeds —
     `rmsFirst` is then exactly 0 and clause (a) divides by zero (`0x3039`: pre-roll 74.39 s;
     `0xBEEF`: 84.66 s; `0xABCD`: 70.96 s). Even at 900 s windows the ZCR swings 22 → 6 and 12 → 26.
     Both statistics are dropped for this source. What replaces them is **not weaker**: clause (d)'s
     event count is the direct FR-007 observable (measured hour-1 vs hour-8 deviation 1.5–6.9 % over
     8 seeds, against the unchanged 20 % bound, with `eventsHour1 >= 10` as the anti-vacuity guard),
     and the `rmsLast900s > 0` liveness clause is exactly the "scheduler stops firing" failure SC-017
     names. *Amend SC-017 in the spec to match.*
10. **SC-003 clause (c) gains an absolute floor**, `fracAbove(8·rate) >= 1.30e-4` at n = 4,
   `rate = kMaxRate` (§5.3). Clause (c) is FR-018's only enforcing criterion, and the strict
   monotonicity it inherits from clause (b) is preserved by *any* low-pass — measured this session as
   monotonic at 1, 5, 20 **and** 100 ms, so the clause could not fail on a smoother 20× too slow.
   Separately, *FR-018's parenthetical is arithmetically wrong*: `OnePoleSmoother`'s `smoothTimeMs` is
   time-to-99 %, τ = ms/5000 s (`smoother.h:86-92`), so a 20 ms smoother's cutoff is **39.8 Hz** and
   its attenuation at the 40 Hz top octave is **≈3.0 dB**, not "7.96 Hz" and "~14 dB" (7.96 Hz is the
   *100 ms* cutoff). The 5 ms figure the spec quotes — 159 Hz, 0.27 dB — is correct. The conclusion is
   unchanged (5 ms is still the right constant) but the margin is much smaller than FR-018 claims,
   which is exactly why clause (c) needed a measured absolute floor rather than a shape argument.
   *Amend FR-018's parenthetical to the correct figures.* **No threshold is relaxed.**
11. **FR-005's "owned `Xorshift32`" is satisfied by `SlowEventScheduler` only** (§1.2).
   `PerlinNoiseSource` owns no RNG object: FR-012 forbids a running stream, so it stores
   `configuredSeed_` plus the four `deriveStreamSeed`-derived octave seeds and hashes the lattice
   statelessly (§1.3). Determinism under seed — FR-005's actual intent, and what SC-004 measures —
   holds identically. *Either reword FR-005 to "an owned seed, consumed either as an `Xorshift32`
   stream or as a stateless `deriveStreamSeed` hash", or accept this entry as the FR-005 evidence for
   `PerlinNoiseSource` in the compliance table.* Nothing in the implementation changes.

### Review notes — where this revision departs from the suggested resolution

Both departures make the resolution stronger, not cheaper; neither relaxes a threshold.

* **SC-003 clause (c) floor (§5.3, §8.10).** The suggestion was `fracAbove(4·rate) >= 6.0e-3` at
  n = 4. Re-measured here, that band edge separates 5 ms (6.878e-3) from 20 ms (4.940e-3) by only
  1.39×, so any floor between them has < 1.25× margin on one side. The floor is placed at
  `fracAbove(8·rate) >= 1.30e-4` instead, where the separation is 2.10–2.15× and stable across 8
  seeds (1.31× / 1.36× margins). Both figures and the injection check are written into the plan.
* **SC-006(c) perturbation recipe (§5.2).** The suggestion was `setCoupling(1.0f)` +
  `setInputLevel(1.0e-3f)`. That produces **no** perturbation: the coupling path is gated on
  `std::abs(inputLevel_) > 0.001f` (`chaos_mod_source.h:214`), a strict comparison that `1.0e-3f`
  fails exactly, so the two instances would stay bit-identical and the clause would assert nothing.
  The plan uses `setCoupling(0.1f)` + `setInputLevel(1.0e-2f)`, which clears the gate and yields the
  same 1e-4 displacement the criterion names.

---

## 9. Implementation order

1. `perlin_noise_source.h` + `perlin_noise_source_test.cpp` → build `dsp_processors_tests`, run
   `"PerlinNoiseSource_*"`. Nothing else depends on it; it is the cleanest first target. Before moving
   on, run the SC-003 clause (c) **injection check**: set `kOutputSmoothMs = 20.0f`, confirm
   `PerlinNoiseSource_SpectralRolloff` goes **red** on the `frac40 >= 1.30e-4` floor, then restore 5.0f
   (§5.3). An absolute threshold nobody has seen fail is not yet a gate.
2. `chaos_waveshaper.h` enum + grouped switch arms, then `chaos_mod_source.h` Aizawa arms + FR-036
   counter + the 1 Hz floor → build **both** `dsp_primitives_tests` and `dsp_processors_tests`, and run
   the two WSL warning commands (§5.4) **before anything else**, because this is the only step that can
   break the Linux/macOS CI legs invisibly from Windows. `ChaosModSource_DivergenceCounterObservable`
   ships in the same commit as the counter — SC-006's three `== 0u` clauses are tautologies without it.
3. `slow_event_scheduler.h` + `slow_event_scheduler_test.cpp` → run `"SlowEventScheduler_*"`. Apply
   §3.7's configuration-ordering rule in every case that depends on cadence (SC-009(i), SC-013), and
   bind FR-067 (SC-008 clause (e)) and the FR-058 read-surface identities (SC-009 row) here rather than
   deferring them — they are the clauses that stop an otherwise-green implementation from shipping a
   scheduler that fires at `t = 0` or returns a wrong `getEnvelopeValue()`.
4. `vorago_p1_perf_test.cpp` (SC-014), tagged `"[.perf]"`, with the per-sample scheduler output reads
   in the measured block; then tighten `kBaselineNsPerBlock` to the measured dev-machine figure and
   re-check the `static_assert` (ceiling 7111 ns). If the measured cycle-inclusive figure exceeds
   ≈2000 ns/block for the four schedulers alone, check that `riseShape` really is the polynomial —
   §3.3's cost analysis is the reason SC-014 is attainable at all.
5. `vorago_p1_longrun_test.cpp` (SC-017, `[long]`).
6. `vorago_p1_harness.cpp` (SC-015) + the CMake compile definition.
7. Repo gates (SC-016): the five lints, `node tools/check-portability.js`,
   `./tools/run-clang-tidy.ps1 -Target dsp`, plus the `lint_all_headers.cpp` and
   `lint-nonfinite-symbols.js` `GUARDED` additions that make two of those gates non-vacuous.
8. Full-suite green: `dsp_processors_tests`, `dsp_primitives_tests`, `dsp_systems_tests`, and the
   Ruinae / Disrumpo plugin test targets (the other `ChaosModel` consumers).
