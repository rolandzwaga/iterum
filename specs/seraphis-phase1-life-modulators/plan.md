# Implementation Plan: Seraphis Phase 1 — Life Modulator Suite

**Spec:** `specs/seraphis-phase1-life-modulators/spec.md`
**Layer:** all six components are **Layer 2** (`dsp/include/krate/dsp/processors/`)
**Plugin work:** none. KrateDSP-only, unit-tested. No router wiring (deferred to Phase 7 per Clarifications OQ1).

This plan is written so an implementer never guesses the math or the API. Every reused signature is quoted
from a header read this session, with `file:line`. Every FR/SC has a concrete test file, `TEST_CASE` name,
and assertion strategy.

---

## 0. Shared design (applies to all six modulators)

### 0.1 Reused components (verified signatures, this session)

| Component | Header:line | Exact signature reused |
|---|---|---|
| `ModulationSource` (ABC, L0) | `core/modulation_source.h:31` | `class ModulationSource`; pure virtual `[[nodiscard]] virtual float getCurrentValue() const noexcept` (`:37`); `[[nodiscard]] virtual std::pair<float,float> getSourceRange() const noexcept` (`:41`). |
| `Xorshift32` (L0) | `core/random.h:40` | `explicit constexpr Xorshift32(uint32_t seedValue=1) noexcept` (`:44`); `nextFloat() → [-1,1]` (`:58`); `nextUnipolar() → [0,1]` (`:66`); `seed(uint32_t)` (`:72`); `state()` (`:78`). |
| `OnePoleSmoother` (L1) | `primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (`:160`); `setTarget(float)` — NaN→0, Inf→±1e10 sanitize (`:170`); `getCurrentValue()` (`:191`); `float process()` (`:197`); `advanceSamples(size_t)` O(1) closed form (`:243`); `snapTo(float)` (`:263`); `reset()` (`:275`). Coefficient: `coeff = exp(-5000/(smoothTimeMs·sr))` (`:90-92`). |
| `SlewLimiter` (L1) | `primitives/smoother.h:468` | `configure(float riseRatePerMs, float fallRatePerMs, float sr)` (`:501`) / `configure(float ratePerMs, float sr)` (`:512`); `float process()` (`:545`). Per-sample delta hard-capped at `ratePerMs/(1000·... )`. |
| `RandomSource` (L2, template) | `processors/random_source.h:34` | The pattern every modulator copies: `prepare(double)` (`:45`), `reset()` (`:53`), control-rate `processBlock(size_t)` (`:66`), per-sample `process()` (`:87`), `getCurrentValue() override` (`:114`), `getSourceRange()→{-1,1}` (`:118`); owns `Xorshift32 rng_{98765}` (`:158`) + `OnePoleSmoother outputSmoother_` (`:159`). |
| `ChaosModSource` (L2, decimation ref) | `processors/chaos_mod_source.h:35` | Control-rate decimation pattern: `kControlRateInterval=32` (`:43`); per-sample `process()` decrements `samplesUntilUpdate_` and updates on boundary (`:69-75`); `processBlock` advances the correct step count (`:80-91`). |
| `render_fingerprint.h` (test helper) | `tests/test_helpers/render_fingerprint.h:64` | `TestUtils::fingerprintRender(std::span<const float>) → RenderFingerprint`; `compareFingerprints(...)`; `kSampleTolerance=1e-4` (`:49`), `kMetricTolerance=1e-5` (`:52`). Linked into `dsp_processors_tests` via `test_helpers` (`dsp/tests/CMakeLists.txt:281`). |

**Layer legality:** each header includes only `core/modulation_source.h`, `core/random.h`,
`primitives/smoother.h`, and stdlib (`<algorithm> <cmath> <cstddef> <cstdint> <utility>`). No Layer 2/3/4
include — verified against the reuse table; matches `RandomSource`'s include block (`random_source.h:17-23`).

### 0.2 Common lifecycle contract (every modulator)

```cpp
class XxxModulator : public ModulationSource {
public:
    XxxModulator() noexcept = default;

    void prepare(double sampleRate) noexcept;   // FR-004: derives per-sample coeffs; output well-defined after
    void reset() noexcept;                       // FR-004 + SC-004: rewinds RNG to configuredSeed_, re-inits state
    void setSeed(uint32_t seed) noexcept;        // FR-005: sets configuredSeed_ + rng_.seed(seed).
                                                 // PLAIN non-virtual member — NOT an override: ModulationSource
                                                 // declares only getCurrentValue()/getSourceRange() as virtuals
                                                 // (modulation_source.h:37,41). Writing `override` here fails to
                                                 // compile ("marked override but does not override").

    void process() noexcept;                     // per-sample advance (drives SC-002 renders)
    void processBlock(size_t numSamples) noexcept; // FR-003: control-rate block advance (drives SC-006/007)

    [[nodiscard]] float getCurrentValue() const noexcept override;         // FR-001
    [[nodiscard]] std::pair<float,float> getSourceRange() const noexcept override; // FR-001/FR-006

    // per-modulator setters (rate/depth/…) — all std::clamp to documented ranges
};
```

- **Two advance methods, deliberately** (mirrors `RandomSource` `:66`/`:87`): `process()` materialises a smooth
  per-sample output so SC-002 can measure a real `|out[n]-out[n-1]|`; `processBlock()` is the efficient
  control-rate path (SC-006/SC-007). Both update the same state and are kept consistent by routing all
  stochastic state changes through a **control-rate decimation counter** (`ChaosModSource` pattern,
  `chaos_mod_source.h:69-91`).
- **`OnePoleSmoother::process()` is `[[nodiscard]]`** (`smoother.h:197`). Every per-sample advance that steps
  the output smoother MUST discard the return via `static_cast<void>(outputSmoother_.process())` exactly as
  `RandomSource` does (`random_source.h:110`). A bare `outputSmoother_.process();` emits C4834 (MSVC) /
  `-Wunused-result` (GCC/Clang) and fails this project's zero-warnings gate. The `process()`/`processBlock()`
  shorthand "always `outputSmoother_.process()`" used below is this discarded call, not a bare statement.
- **Control-rate decimation.** `static constexpr size_t kControlRateInterval = 32;` (same as `ChaosModSource`
  `:43`). `prepare()` computes `controlDtSeconds_ = kControlRateInterval / sampleRate_`. Stochastic draws
  (OU increment, waypoint refill, breath-cycle jitter) happen only on control boundaries; the per-sample
  output smoother bridges between them.
- **RT-safety (FR-002 / SC-006).** Every method above is `noexcept`; no `new`/`delete`, no locks, no I/O; all
  storage is fixed-size members (waypoint ring = `std::array<float,4>`). No `std::isnan` anywhere (`-ffast-math`
  rule) — non-finite hygiene comes from `OnePoleSmoother::setTarget`'s built-in sanitize (`smoother.h:170`)
  plus hard clamps and bounded recurrences (§ per component).

### 0.3 Depth / range / bounded-slew invariants (FR-006, SC-001, SC-002)

- **`getSourceRange()` is a fixed constant**, independent of every setting: `{-1.0f,+1.0f}` for all except
  `GrowthEnvelope` which returns `{0.0f,1.0f}` (Q1/Q2). Depth scales the signal *inside* that fixed range —
  the reported range never shrinks (FR-006), so a downstream depth knob cannot double-attenuate.
- **Boundedness is by construction, then clamped as a belt-and-braces.** Each component's raw signal is bounded
  analytically (§ per component); `getCurrentValue()` additionally returns `std::clamp(smoothed, lo, hi)`
  exactly as `RandomSource::getCurrentValue` does (`random_source.h:115`). Clamp is the SC-001 hard guarantee.
- **SC-002 guaranteed analytically by the output smoother.** For a `OnePoleSmoother`, the per-sample change is
  `|current-target|·(1-coeff) ≤ span·(1-coeff)`. SC-002's threshold is `1e-3 of the source-range span` = `2e-3`
  absolute (span=2). Solving `span·(1-coeff) ≤ 2e-3` with `coeff = exp(-5000/(T·sr))` (`smoother.h:90`) at
  48 kHz gives `T ≳ 120 ms` (matches the spec's derivation from `ModulationEngine`'s 120 ms amount smoother).
  - **`BrownianDrift`** (genuine per-control-step steps) configures its output smoother at
    `kDriftOutputSmoothMs = 150 ms` → `1-coeff ≈ 6.9e-4`, max per-sample delta `≤ 2·6.9e-4 ≈ 1.4e-3 < 2e-3`.
    This is the SC-002 proof; it holds *regardless of step size* because the smoother never jumps.
  - The five **inherently-smooth** modulators (Breathing, Tidal, Orbit, Spline, Growth) produce raw signals
    whose slope already yields per-sample deltas `≪ 2e-3` at their worst case (worked out per component). They
    route through a light `20 ms` output smoother purely for block-boundary safety/uniformity; SC-002 passes
    with wide margin. `SplineTrajectory` is additionally C1 by construction and is evaluated per-sample.
- **Seed / determinism (FR-005, SC-004).** Each stochastic modulator owns `Xorshift32 rng_` and a
  `uint32_t configuredSeed_`. `setSeed(s)` stores `s` and calls `rng_.seed(s)`. **Both `prepare()` and
  `reset()` call `rng_.seed(configuredSeed_)` and re-initialise all derived state**, so post-`prepare` and
  post-`reset` states are bit-identical and the same seed re-renders identically (SC-004 / OQ3). Seed 0 is
  safe — `Xorshift32` substitutes `kDefaultSeed` (`random.h:44,73`).
- **Sample-rate strategy (SC-005), fixed per modulator:**
  - Draw-free-or-cycle-boundary-draw modulators (`TidalModulator`, `OrbitModulator`, `BreathingModulator`,
    `GrowthEnvelope`) are defined in seconds and their RNG draws (if any) occur at wall-clock-aligned cycle
    boundaries → **option (a) like-for-like**: same wall-clock trajectory at 44.1/96 kHz within a tight
    tolerance (only Euler/rounding differences).
  - `BrownianDrift` and `SplineTrajectory` draw the RNG on the sample-count control grid, so the two rates are
    different stochastic realisations → **option (b) distributional**: compare RMS + decorrelation time
    **averaged over ≥8 seeds**, tolerance from the measured across-seed spread (start ±5 %, widen to measured).

---

## 1. `BrownianDrift` — `processors/brownian_drift.h`

**Roadmap:** lines 111-112. **FRs:** FR-011..FR-014. **ODR:** clear (spec table).

### 1.1 Algorithm — exact Ornstein–Uhlenbeck (FR-011)

Continuous OU: `dX = θ(μ−X)dt + σ dW`. Its **exact** (not Euler) discretisation over a fixed step `Δt`
(= `controlDtSeconds_`) is the AR(1) recurrence

```
a          = exp(-Δt / τ)                 // τ = decorrelation time (seconds), θ = 1/τ
X_{n+1}    = μ + a·(X_n − μ) + g·Z_n       // g = σ0·sqrt(1 − a²),  Z_n ~ zero-mean, unit-variance
```

- **`Z_n`** is drawn as the Irwin–Hall sum of three `rng_.nextFloat()` (each `∈[-1,1]`, variance 1/3), giving a
  zero-mean, unit-variance, approximately-Gaussian increment. The increment *distribution* is not load-bearing;
  the AR(1) coefficient `a` alone fixes the autocorrelation (`corr(lag k) = a^k`), so SC-003(a) holds for any
  zero-mean increment.
- **`σ0 = kInternalStd = 0.5`** — the stationary std of the internal walk. Occasional excursions toward ±1 are
  hard-clamped; excursions are rare at 0.5, so clamping is light and the motion stays organic.
- **Decorrelation time in seconds equals `τ` exactly**: `a^k = 1/e` at `k = τ/Δt` control steps = `τ` seconds.
  This is why SC-003(a) can assert monotonicity vs smoothness.
- **Smoothness → τ** (FR-012): `τ = lerp(kTauMin, kTauMax, smoothness)` with `kTauMin=0.2 s`, `kTauMax=30 s`.
  Higher smoothness → larger τ → slower, more correlated walk. (`kTauMin` sits above the 150 ms output-smoother
  time constant so the smoother never dominates the ordering.)
- **Output:** `out = clamp(depth · X, -1, 1)` (FR-013). `μ` default 0 (`setMean` optional, clamped `[-1,1]`).
- **Denormals:** flush `X` to 0 when `|X| < 1e-20f` (FTZ is also set by `dsp_test_main`, but flush explicitly so
  a long mean-reverting decay toward 0 cannot generate denormals in the recurrence).

### 1.2 Public API

```cpp
class BrownianDrift : public ModulationSource {
public:
    static constexpr float  kTauMin = 0.2f, kTauMax = 30.0f;      // seconds
    static constexpr float  kInternalStd = 0.5f;
    static constexpr float  kDriftOutputSmoothMs = 150.0f;        // SC-002 guarantee
    static constexpr size_t kControlRateInterval = 32;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setSeed(uint32_t seed) noexcept;

    void setSmoothness(float s01) noexcept;   // clamp[0,1] → τ
    void setDepth(float d01) noexcept;        // clamp[0,1]
    void setMean(float m) noexcept;           // clamp[-1,1]

    void process() noexcept;
    void processBlock(size_t numSamples) noexcept;
    [[nodiscard]] float getCurrentValue() const noexcept override; // clamp(smoother, -1, 1)
    [[nodiscard]] std::pair<float,float> getSourceRange() const noexcept override { return {-1.0f,1.0f}; }

private:
    void advanceControlStep() noexcept;  // one OU update + set smoother target = depth·X
    double sampleRate_ = 44100.0, controlDtSeconds_ = 0.0;
    float a_ = 0.0f, g_ = 0.0f;          // recomputed in prepare and on setSmoothness
    float mean_ = 0.0f, depth_ = 1.0f, smoothness_ = 0.5f, x_ = 0.0f;
    int samplesUntilControl_ = 0;
    Xorshift32 rng_{0xB17E};             // reseeded to configuredSeed_ in prepare/reset
    uint32_t configuredSeed_ = 0xB17E;
    OnePoleSmoother outputSmoother_;
};
```

### 1.3 prepare / reset / process contract

- **prepare:** store `sampleRate_`; `controlDtSeconds_ = kControlRateInterval/sr`; recompute `a_,g_` from τ;
  `outputSmoother_.configure(kDriftOutputSmoothMs, sr)`; `rng_.seed(configuredSeed_)`; `x_ = mean_`;
  `outputSmoother_.snapTo(depth_*x_)`; `samplesUntilControl_ = 0`.
- **reset:** identical re-init as prepare *except* keep `sampleRate_` — reseed RNG, `x_=mean_`, snap smoother,
  reset counter. (SC-004 rewind.)
- **process (per-sample):** decrement counter; on boundary `advanceControlStep()`; always
  `static_cast<void>(outputSmoother_.process())` (discard the `[[nodiscard]]` return — see §0.2).
- **processBlock(n):** advance in `ChaosModSource` style (`chaos_mod_source.h:80-91`) — while remaining>0, on
  each control boundary call `advanceControlStep()`, then `outputSmoother_.advanceSamples(chunk)` for the samples
  consumed to the next boundary. `processBlock(0)` is a no-op (loop guard).

### 1.4 RT-safety notes

No allocation; three `nextFloat()` + one `exp`-free recurrence per 32 samples; `advanceSamples` is O(1). All
`noexcept`.

---

## 2. `BreathingModulator` — `processors/breathing_modulator.h`

**Roadmap:** lines 113-114. **FRs:** FR-021..FR-024. **ODR:** clear.

### 2.1 Algorithm — fixed asymmetric breath shape (FR-021, Q6)

A phase `φ∈[0,1)` advances at `rate` Hz. The shape `y(φ)∈[0,1]` is **hardcoded**, 40 % inhale / 60 % exhale,
with distinct rise/fall curvature (not a single sine):

```
inhale  φ∈[0, 0.4):  u = φ/0.4;              y = 0.5·(1 − cos(π · u^0.8))   // skewed ease-in
exhale  φ∈[0.4, 1):  v = (φ−0.4)/0.6;        y = 0.5·(1 + cos(π · v^1.3))   // slower, longer tail
```

- `y=0` at `φ=0` and `φ→1`; `y=1` at `φ=0.4` — value-continuous across the whole cycle.
- **Rise time (0.4·T) ≠ fall time (0.6·T)** and the two exponents differ → FR-021's "rise ≠ fall" and
  "non-sinusoidal" both hold. The duty-asymmetry alone puts strong energy at 2f/3f (the non-sinusoidal test).
- **Output:** `out = depth · (2y − 1)` → bipolar, `∈[-depth,depth] ⊆ [-1,1]` (FR-023, bounded by construction).

### 2.2 Rate + irregularity

- **Rate** (FR-022): `setRate` clamps to `[kMinRate,kMaxRate] = [0.01, 0.5]` Hz. Phase increment per sample =
  `rate·effectiveJitter / sr`.
- **Irregularity** (FR-024): at each cycle wrap (`φ` crosses 1) draw `jitter = 1 + irregularity · 0.5 ·
  rng_.nextFloat()`, then `jitter = max(jitter, 0.1)` (guarantees positive period even at irregularity=1 —
  edge case in spec). The jitter multiplies the *next* cycle's phase increment. **At irregularity = 0 the jitter
  is exactly 1.0** (constant period — FR-024, and the SC-003(b) prediction basis). The draw is the only RNG use,
  and it happens at a wall-clock-aligned cycle boundary → SC-005 option (a).

### 2.3 Worst-case slew (SC-002)

Max `|dy/dφ|` of the shape is O(1); `dy/dt = (dy/dφ)·rate ≤ ~2·0.5 = 1 /s`; per sample `≤ 1/48000 ≈ 2e-5 ≪ 2e-3`.
A 20 ms output smoother bridges block boundaries. Passes with wide margin.

### 2.4 API sketch

```cpp
class BreathingModulator : public ModulationSource {
    static constexpr float kMinRate = 0.01f, kMaxRate = 0.5f;
    void setRate(float hz) noexcept;      // clamp
    void setDepth(float d01) noexcept;    // clamp[0,1]
    void setIrregularity(float i01) noexcept; // clamp[0,1]
    // prepare/reset/setSeed/process/processBlock/getCurrentValue as §0.2
    // getSourceRange → {-1,1}
private:
    double phase_ = 0.0, phaseInc_ = 0.0, sampleRate_ = 44100.0;
    float rate_ = 0.1f, depth_ = 1.0f, irregularity_ = 0.0f, cycleJitter_ = 1.0f;
    Xorshift32 rng_{0xB2EA}; uint32_t configuredSeed_ = 0xB2EA;
    OnePoleSmoother outputSmoother_;
};
```

`process()`/`processBlock()` advance `phase_` in double, wrap `while(phase_>=1){phase_-=1; drawJitter();}`,
recompute `phaseInc_`, set smoother target = shape output.

---

## 3. `TidalModulator` — `processors/tidal_modulator.h`

**Roadmap:** lines 115-116. **FRs:** FR-031..FR-033. **ODR:** clear.

### 3.1 Algorithm — 3 layers × beating sine pair, incommensurate ratios (FR-031/FR-032/Q3)

```
layer k (k=0,1,2):  L_k(t) = 0.5·[ sin(θ_k0) + sin(θ_k1) ]     // "sine pair": two detuned sines that beat
output               = depth · Σ_{k} w_k · L_k(t)
```

- **Fixed incommensurate ratios** (hardcoded, *not* seed-drawn — Q3/FR-032):
  `ratio = {1.0, √2 = 1.41421356, √3 = 1.73205081}`. Base period `P_base` comes from the single rate scalar;
  layer period `P_k = clamp(P_base·ratio_k, 30 s, 600 s)`; layer freq `f_k0 = 1/P_k`,
  `f_k1 = f_k0·(1 + kDetune)` with `kDetune = 0.02` (the beat). The irrationality of √2,√3 is the *design*
  justification for indefinite non-repetition; the *testable* claim (FR-032) is "no exact repeat within the
  longest render", asserted by `TidalModulator_NoExactRepeat`.
- **Weights** `w_k = 1/3`, so `Σ|w_k|·max|L_k| = 3·(1/3)·1 = 1` → `|output| ≤ depth ≤ 1` **analytically**
  regardless of phase alignment (FR-033 — the load-bearing bound; the render is only a sanity check).
- **Rate scalar** (FR-031): `setRate(r01)` maps `P_base = lerp(600 s → 30 s, r01)` (r01=0 → slowest). All three
  layers scale together. Dominant spectral period ≈ shortest layer period `P_0 = P_base` → SC-003(b) predicts it
  from the rate scalar alone.
- **Phase seeding** (FR-005): the six sine phases get seeded initial offsets from `rng_.nextUnipolar()·2π`.
  Period/statistics are seed-independent (Q3); only the phase offset varies, so SC-004 same-seed determinism
  still holds.

### 3.2 Numerical stability (critical — long renders)

SC-001 renders `TidalModulator` for **≥30 min** (≥3× the 10 min max period) = ~86 M samples at 48 kHz. **Phase
accumulators are `double`, wrapped `mod 2π` every block** — single-float phase would lose the low bits and drift.
`sin` is evaluated on the wrapped double, cast to float for output. `sin/cos` differ in last bits across
toolchains → **no bit-exact golden**; boundedness (clamp) and the analytic Σ|w| bound are toolchain-independent.

### 3.3 Worst-case slew (SC-002)

Worst case per the spec table (`spec.md:259`): **shortest period (30 s), max depth, all layers at full
amplitude.** `output = depth·Σ_k w_k·0.5·(sin θ_k0 + sin θ_k1)` = `depth·(1/6)·Σ (six sines)` (`w_k = 1/3`).
Each sine's rate is bounded by the fastest angular frequency in play, `ω_max = 2π/P_min = 2π/30 s ≈ 0.209 rad/s`
(the shortest layer period is `P_0 = P_base = 30 s`; the √2/√3 layers are slower, the +2 % detune partner is
negligibly faster). So

```
|d(output)/dt| ≤ depth·(1/6)·(6·ω_max) = depth·ω_max ≤ 0.209 /s
per sample @48 kHz ≤ 0.209/48000 ≈ 4.4e-6 ≪ 2e-3
```

The 20 ms output smoother bridges block boundaries; SC-002 passes with wide margin. This is the analytic
justification behind the `TidalModulator_MaxSlewBounded` case (§7.2, SC-002).

### 3.4 API sketch

```cpp
class TidalModulator : public ModulationSource {
    static constexpr float kMinPeriod = 30.0f, kMaxPeriod = 600.0f, kDetune = 0.02f;
    void setRate(float r01) noexcept;  // clamp[0,1] → P_base
    void setDepth(float d01) noexcept;
    // lifecycle as §0.2; getSourceRange → {-1,1}
private:
    double theta_[3][2]{}, inc_[3][2]{}, sampleRate_ = 44100.0;
    float depth_ = 1.0f, rate_ = 0.5f;
    Xorshift32 rng_{0x71DA}; uint32_t configuredSeed_ = 0x71DA;   // valid hex (was the invalid literal 0xT1DA)
    OnePoleSmoother outputSmoother_;   // 20 ms, block-boundary safety
};
```

---

## 4. `OrbitModulator` — `processors/orbit_modulator.h`

**Roadmap:** lines 117-118. **FRs:** FR-041..FR-043. **ODR:** clear.

### 4.1 Algorithm — Kuramoto two-oscillator + clamped radius (FR-041/FR-042/FR-043, Q5)

```
dφ1/dt = ω1 + k·sin(φ2 − φ1)          // ω1 = 2π·rate,  ω2 = 2π·rate·(1+kOscDetune)
dφ2/dt = ω2 + k·sin(φ1 − φ2)          // k = coupling
x = depth · r · sin(φ1)               // getCurrentValue()  (FR-042: base contract = x axis)
y = depth · r · sin(φ2)               // getY()
```

- **Rate** (FR-041): `setRate` clamps `[0.01, 0.5]` Hz. `kOscDetune = 0.1` so coupling has a phase difference to
  act on. Integrated with **forward Euler at control rate** (`dt = controlDtSeconds_`) — unconditionally stable
  here because `ω·dt ≤ 2π·0.5·(32/44100) ≈ 2.3e-3 ≪ 1` and `sin` coupling is bounded.
- **Radius envelope** (FR-043): `growth ∈[-1,1]`. Per control step `r += growth · kRadiusRate · dt`, then
  `r = clamp(r, kRadiusMin=0.05, 1.0)`. `growth=0` → `r` unchanged = **sustained orbit** (neutral). `growth<0` →
  spirals in toward `kRadiusMin` (never a stuck point at 0); `growth>0` → grows to 1 and clamps. The clamp is
  the boundedness proof: `|x|,|y| ≤ depth·1·1 = depth ≤ 1` (FR-042, SC-001 both axes).
- **Two output smoothers** (`xSmoother_`, `ySmoother_`), 20 ms; `getY()` returns `clamp(ySmoother_, -1, 1)`.
- **Seed** (FR-005): initial `φ1, φ2` seeded from `rng_.nextUnipolar()·2π`; `r` starts at 1.

### 4.2 API sketch

```cpp
class OrbitModulator : public ModulationSource {
    static constexpr float kMinRate = 0.01f, kMaxRate = 0.5f;
    void setRate(float hz) noexcept; void setDepth(float d01) noexcept;
    void setCoupling(float k01) noexcept;   // clamp[0,1]
    void setGrowth(float g) noexcept;       // clamp[-1,1]; 0 = sustain
    [[nodiscard]] float getY() const noexcept;   // second axis
    // lifecycle as §0.2; getCurrentValue → x; getSourceRange → {-1,1}
private:
    double phi1_=0, phi2_=0, sampleRate_=44100.0, controlDtSeconds_=0;
    float radius_=1.0f, rate_=0.1f, coupling_=0.0f, growth_=0.0f, depth_=1.0f;
    int samplesUntilControl_=0;
    Xorshift32 rng_{0x0B1F}; uint32_t configuredSeed_=0x0B1F;   // valid hex (was the invalid literal 0x0RB1)
    OnePoleSmoother xSmoother_, ySmoother_;
};
```

Worst-case slew (SC-002): `|d(sin φ)/dt| ≤ ω ≤ 2π·0.5 = 3.14 /s`; per sample `≤ 6.5e-5 ≪ 2e-3`.

---

## 5. `SplineTrajectory` — `processors/spline_trajectory.h`

**Roadmap:** lines 119-120. **FRs:** FR-051..FR-054. **ODR:** clear.

### 5.1 Algorithm — Catmull-Rom over a 4-waypoint ring (FR-051/FR-052/FR-053/FR-054, Q4)

Uniform Catmull-Rom segment between `p1` and `p2`, using neighbours `p0,p3`, parameter `u∈[0,1)`:

```
q(u) = 0.5·[ 2p1 + (−p0+p2)u + (2p0−5p1+4p2−p3)u² + (−p0+3p1−3p2+p3)u³ ]
```

- **Ring of 4** `std::array<float,4> wp_`; `u` advances per sample by `du = 1/(interval·sr)`. When `u≥1`:
  `u−=1`, rotate the ring (drop `wp_[0]`, shift, draw a fresh `wp_[3]`). A large block that consumes several
  waypoints loops this (Edge case: "arbitrarily large block advances that consume multiple waypoints").
- **Waypoint interval** (FR-052): `setWaypointInterval(double seconds)` clamps `[0.5, 30]`. Draws occur on the
  sample grid → SC-005 uses **option (b)** for this modulator.
- **C1 continuity** (FR-053): Catmull-Rom is C1 across segment joins by construction (tangent at `p_i` =
  `0.5(p_{i+1}−p_{i-1})`, shared by both adjoining segments). **Evaluated per-sample** (no block-step
  decimation of the *output*), so the delivered signal is genuinely C1 — this is why `SplineTrajectory` does
  **not** rely on the output smoother for SC-002/FR-053.
- **Boundedness by construction** (FR-054 — rigorous): waypoints are drawn in `[−wMax, wMax]` with
  **`wMax = 0.8`**. The Catmull-Rom Lebesgue bound `max_u Σ_i |b_i(u)| = 1.25` (attained at `u=0.5`:
  `|−0.0625|+|0.5625|+|0.5625|+|−0.0625| = 1.25`) gives `|q(u)| ≤ 1.25·max|p_i| = 1.25·0.8 = 1.0`. So the raw
  spline never leaves `[-1,1]` **without any clamp** (clamp kept only as an inert safety net, preserving C1).
  `out = depth·q(u) ⊆ [-1,1]`.
- **Seed** (FR-005): the initial 4 waypoints and every refill come from `rng_.nextFloat()·wMax`.

Worst-case slope (SC-002): `|dq/dt| ≤ (segment amplitude/interval)·~1.5 ≤ (2/0.5)·1.5 = 6 /s` → per sample
`≤ 1.25e-4 ≪ 2e-3`.

### 5.2 API sketch

```cpp
class SplineTrajectory : public ModulationSource {
    static constexpr float kMinInterval = 0.5f, kMaxInterval = 30.0f, kWaypointMax = 0.8f;
    void setWaypointInterval(double seconds) noexcept;  // clamp
    void setDepth(float d01) noexcept;
    // lifecycle as §0.2; getSourceRange → {-1,1}; process() evaluates q(u) per sample
private:
    std::array<float,4> wp_{}; double u_=0.0, du_=0.0, sampleRate_=44100.0, interval_=2.0;
    float depth_=1.0f;
    Xorshift32 rng_{0x5F11}; uint32_t configuredSeed_=0x5F11;   // valid hex (was the invalid literal 0x5PL1)
    // no output smoother needed (C1); optional 20 ms for uniformity if desired
};
```

---

## 6. `GrowthEnvelope` — `processors/growth_envelope.h`

**Roadmap:** lines 121-122. **FRs:** FR-061..FR-063. **ODR:** clear. **Unipolar `[0,1]`.**

### 6.1 Algorithm — normalized logistic rise-and-hold (FR-061/FR-062/FR-063, Q7)

Normalized so it hits exactly 0 at the start and exactly 1 at the end (a bare logistic never reaches either):

```
τ = elapsed / D  ∈[0,1]                 // D = duration (seconds), clamp[1,60]
L(τ) = 1 / (1 + exp(-k·(τ − 0.5)))      // k = kSteepness = 10
y(τ) = (L(τ) − L(0)) / (L(1) − L(0))    // exact 0 at τ=0, exact 1 at τ=1, monotone S-curve
```

- **State machine** (FR-062/FR-063): `enum { Idle, Rising, Complete }`.
  - `Idle` (before first trigger): `getCurrentValue()` = **0** (bottom of range, FR-063).
  - `trigger()`: `Idle → Rising` (sets `elapsed_=0`); `Rising → no-op` (continuation — never restarts, FR-062);
    `Complete → no-op` (holds at top, FR-062).
  - `Rising`: `process()` advances `elapsed_ += 1/sr`; when `elapsed_ ≥ D`, `elapsed_=D`, state=`Complete`,
    output=1. Output is monotonic non-decreasing (logistic is monotone in τ) and bounded `[0,1]`.
  - `Complete`: holds `1` forever. **No fall/release segment** (FR-063, Q7).
- **No RNG** — fully deterministic. `GrowthEnvelope` simply **omits `setSeed`** (there is no stream to seed).
  If a uniform API is wanted, provide it as an empty **plain non-virtual** `void setSeed(uint32_t) noexcept {}`
  — never `override` (`ModulationSource` has no virtual `setSeed`; see §0.2). SC-004/SC-005 for Growth are the
  deterministic like-for-like checks (no seed variance).
- **`reset()`** returns to `Idle`, `elapsed_=0`, output 0.

Worst-case slew (SC-002, min duration 1 s): logistic max slope `= k/4` in τ-units → `(k/4)/D = 2.5 /s`
per second → per sample `≤ 5.2e-5 ≪ 2e-3`. Route through a 20 ms smoother so block-decimated advance can't
introduce a boundary step; passes with margin.

### 6.2 API sketch

```cpp
class GrowthEnvelope : public ModulationSource {
    static constexpr float kMinDuration = 1.0f, kMaxDuration = 60.0f, kSteepness = 10.0f;
    void setDuration(float seconds) noexcept;   // clamp
    void trigger() noexcept;                    // Idle→Rising, else no-op
    void prepare(double sampleRate) noexcept; void reset() noexcept;
    void process() noexcept; void processBlock(size_t numSamples) noexcept;
    [[nodiscard]] float getCurrentValue() const noexcept override;      // 0 while Idle
    [[nodiscard]] std::pair<float,float> getSourceRange() const noexcept override { return {0.0f,1.0f}; }
private:
    enum class Phase { Idle, Rising, Complete } phase_ = Phase::Idle;
    double elapsed_=0.0, duration_=10.0, sampleRate_=44100.0; float l0_=0, invSpan_=1;
    OnePoleSmoother outputSmoother_;
};
```

`l0_ = L(0)` and `invSpan_ = 1/(L(1)-L(0))` are precomputed once (depend only on `k`).

---

## 7. Test plan

All tests live in `dsp/tests/unit/processors/` and register in the `dsp_processors_tests` target. Each file
`#include`s its header + `<catch2/catch_test_macros.hpp>` (+ `catch_approx.hpp`), `using namespace Krate::DSP;`
(pattern: `random_source_test.cpp:9-18`). Determinism uses **exact float equality within one build** (legal —
same compiler, not a cross-toolchain golden). Statistical/period/RMS checks use **measured tolerances**; where
a "shape unchanged" pin is wanted, `render_fingerprint.h` is available. No FNV-over-float-bits goldens
(constitution rule).

### 7.1 Files (7 new)

| File | Covers |
|---|---|
| `brownian_drift_test.cpp` | FR-001..006, FR-011..014, SC-001..006 for `BrownianDrift` |
| `breathing_modulator_test.cpp` | FR-021..024, SC-001..006 |
| `tidal_modulator_test.cpp` | FR-031..033, SC-001,002,003b,004,005,006 |
| `orbit_modulator_test.cpp` | FR-041..043, SC-001,002,004,005,006 |
| `spline_trajectory_test.cpp` | FR-051..054, SC-001,002,003a,004,005,006 |
| `growth_envelope_test.cpp` | FR-061..063, SC-001,002,004,005,006 |
| `life_modulators_perf_test.cpp` | SC-007 (combined `[.perf]`) + shared contract sweep |

### 7.2 Per-SC assertion strategy

- **SC-001 Boundedness** — `TEST_CASE("<Mod>_NeverExceedsRange")`. Grid over every parameter extreme
  (min/max rate·period, min/max depth, max irregularity/coupling/growth). **Render horizon = 3× the longest
  configured period** (Tidal@10min → ≥30 min; Breathing@0.01 Hz → ≥300 s; Growth@60 s → ≥180 s; fast settings
  ≥60 s). Assert `getCurrentValue()` (and `OrbitModulator::getY()`) `∈ [lo,hi]` from `getSourceRange()` every
  sample, **0 violations**. For `TidalModulator`, `TEST_CASE("TidalModulator_BoundedUnderPhaseAlignment")`
  *also* asserts the analytic `Σ|w_k| = 1` sum bound directly (the load-bearing guarantee), the long render
  being a sanity check. To keep 30-min renders fast, step the modulator with `processBlock(512)` and sample
  `getCurrentValue()` per block plus a per-sample pass on a short worst-alignment window.
- **SC-002 Smoothness** — `TEST_CASE("<Mod>_MaxSlewBounded")`, **one per modulator including
  `TidalModulator_MaxSlewBounded`**. Configure the **worst-case (max-slew) row** from the spec table
  (`spec.md:255-262`) — for Tidal that is **shortest period (30 s), max depth, all three layers at full
  amplitude** (`spec.md:259`, derived in §3.3) — render per-sample via `process()` for ≥ one configured period
  (full rise for Growth). Assert `max|out[n]-out[n-1]| ≤ 2.0e-3` (= 1e-3 × span 2). For Orbit assert on both
  `x` and `y`.
- **SC-003 Statistical character**
  - **(a)** `TEST_CASE("BrownianDrift_AutocorrTimeTracksSmoothness")` and
    `TEST_CASE("SplineTrajectory_AutocorrTimeTracksSpacing")`: render ≥ 10× the longest τ / interval per setting
    at ≥3 settings, **mean-detrend**, compute normalized autocorrelation, find first `1/e` crossing lag. Assert
    the crossing lag is **monotonically increasing** with smoothness / interval, is `≫ 1 sample` (distinct from
    white noise), and shows **no single dominant periodic peak** (distinct from an LFO — check the autocorrelation
    has no strong secondary maximum).
  - **(b)** `TEST_CASE("TidalModulator_PeriodTracksSetting")` and
    `TEST_CASE("BreathingModulator_PeriodMatchesRate")` (irregularity=0): render ≫ the longest period, take the
    dominant period via first autocorrelation **maximum** (or FFT peak). Assert measured period is within a
    **documented tolerance band = FFT bin width at the render length** of `P_base` (Tidal) / `1/rate` (Breathing),
    at ≥3 settings, monotone ordering. Plus `TEST_CASE("TidalModulator_NoExactRepeat")`: over the longest render,
    assert no sample window of length W re-appears bit-identically shifted by any lag (bounded finite-horizon
    claim, per FR-032).
- **SC-004 Determinism** — `TEST_CASE("<Mod>_SeededDeterminism")`: two instances, same seed, same
  prepare+processBlock sequence → **exact** equality of the `getCurrentValue()` sequence; a different seed →
  inequality. `TEST_CASE("<Mod>_ResetRewindsToSeed")`: render N blocks, `reset()`, render N again → **exact**
  equality (OQ3). Growth: deterministic-equality only (no seed variance).
- **SC-005 Sample-rate invariance** — `TEST_CASE("<Mod>_SampleRateInvariant")`.
  - Deterministic + cycle-draw (Tidal/Orbit/Breathing/Growth): **option (a)** — render the same wall-clock
    duration at 44.1 and 96 kHz, compare RMS + dominant period within a tight measured tolerance; mean compared
    with an **absolute** range-span tolerance (never relative — sources are ~zero-mean).
  - Brownian/Spline: **option (b)** — render at both rates for ≥8 seeds, compare **mean RMS and mean
    decorrelation time across seeds**; tolerance = measured across-seed spread (start ±5 %, widen to measured).
- **SC-006 RT-safety** — `TEST_CASE("<Mod>_NoAllocInProcess")`: a global `operator new`/`delete` counting guard
  around a steady-state `processBlock` loop asserts **0 allocations** after `prepare`. (The nightly
  ASan/valgrind lane provides the second line of defence.) Mark the TU `-fno-fast-math` only if it injects
  NaN/Inf — these do not.
- **SC-007 Control-rate cost** — `TEST_CASE("LifeModulators_ControlRateCost","[.perf]")`: time all six advanced
  once per 512-sample block, report **ns/block**; assert `≤ baseline × 1.5` against a checked-in ns/block
  baseline (relative regression bound), informational vs the `≤ 0.05 % of 10.67 ms` (`≈5.3 µs`) reference
  figure. `[.perf]` excluded from the default run; invoked explicitly.
- **FR-006 range-fixity** — `TEST_CASE("<Mod>_SourceRangeIndependentOfDepth")`: assert `getSourceRange()` is the
  same fixed pair at depth 0, 0.5, 1.0 (no shrink).
- **FR-004 well-defined-after-prepare** — `TEST_CASE("<Mod>_OutputDefinedAfterPrepare")` (one per modulator, or
  folded into `_SeededDeterminism`): call `prepare(sr)` then read `getCurrentValue()` (and `OrbitModulator::getY()`)
  **with no intervening `process`/`processBlock`**; assert the value is finite (bit-pattern check, not
  `std::isnan`) and inside `getSourceRange()`. Repeat immediately after `reset()`. Catches an unsnapped smoother
  or a NaN initial state that only manifests before the first block (spec.md:107-108).
- **FR-011 mean-reversion** (`BrownianDrift`) — `TEST_CASE("BrownianDrift_RevertsToConfiguredMean")`: with
  `setMean(m)` at a **nonzero** `m` (e.g. 0.4) and a moderate smoothness, render ≫ 10·τ and assert the long-run
  sample average converges to `depth·m` within a measured tolerance, while `setMean(0)` averages near 0. A
  `setMean` that is ignored (fixed-0 OU target) fails this; SC-001 boundedness alone would not catch it
  (spec.md:124-126).
- **FR-021 breath asymmetry + non-sinusoidal** (`BreathingModulator`) —
  `TEST_CASE("BreathingModulator_ShapeAsymmetricAndNonSinusoidal")`: at irregularity = 0, fixed rate, render one
  full cycle per-sample. (a) Find the trough→peak duration (rise) and peak→trough duration (fall); assert
  `rise ≠ fall` and that their ratio is ≈ 40/60 within a tolerance (the hardcoded 0.4/0.6 inhale/exhale split,
  §2.1). (b) FFT the cycle (or several cycles) and assert **significant energy at the 2f and 3f harmonics**
  relative to the fundamental — a symmetric single sine would leave the harmonics near zero. Directly encodes
  the spec's prescribed FR-021 test (spec.md:143-145); neither assertion passes for a symmetric-sine breath.
- **FR-024 irregularity jitter** (`BreathingModulator`) —
  `TEST_CASE("BreathingModulator_IrregularityJittersPeriod")`: at irregularity > 0 (e.g. 0.7, fixed seed),
  measure successive cycle periods (peak-to-peak or wrap-to-wrap) and assert they **vary** cycle-to-cycle and
  the variation is **bounded** (each within the `±irregularity·0.5` factor of the nominal, and always positive);
  at irregularity = 0 assert successive periods are **identical**. A stubbed jitter (always 1.0) fails the
  positive claim (spec.md:150-152).
- **FR-053 C1 continuity at waypoint joins** (`SplineTrajectory`) —
  `TEST_CASE("SplineTrajectory_C1AtWaypointJoins")`: render per-sample across several waypoint boundaries; form
  the **second difference** `d2[n] = out[n] − 2·out[n−1] + out[n−2]`. Assert `max|d2|` in a small window straddling
  each join is within a bound of the same order as `max|d2|` over segment interiors (no spike at the joins). A
  C0-but-not-C1 implementation (wrong tangent formula or an accidental linear interp at joins) produces a
  first-derivative kink → a one-sample `d2` spike at the join that this bound rejects; SC-002's first-difference
  metric would not (spec.md:203, spec.md:270). This is the dedicated FR-053 detector, since C1 is claimed "by
  construction" (§5.1).
- **FR-062/FR-063 Growth behavior** (`GrowthEnvelope`) —
  `TEST_CASE("GrowthEnvelope_RiseAndHoldBehavior")`: (1) before any `trigger()`, `getCurrentValue() == 0`
  (bottom of range, FR-063); (2) `trigger()` mid-rise keeps the output **non-decreasing and continuous** — sample
  the value just before and after a mid-rise retrigger, assert it does **not** drop toward 0 (FR-062
  continuation); (3) the rendered rise is **monotonic non-decreasing** to the top and then **holds at 1** with no
  fall (FR-063). Complements the existing post-completion no-op edge case (spec.md:214-223).
- **Edge cases** — `TEST_CASE("<Mod>_EdgeCases")`: `processBlock(0)` leaves state/output unchanged; a block
  larger than the Spline waypoint interval consumes multiple waypoints without overrun; Breathing irregularity=1
  never yields a non-positive period; Orbit growth extremes neither diverge nor stick; Growth `trigger()`
  after completion is a no-op; seed 0 is safe.

---

## 8. Build integration

1. **`dsp/tests/CMakeLists.txt`** — add the 7 files to the `dsp_processors_tests` source list, inserted before
   the closing `)` at **line 275** (after `unit/processors/test_modal_bank_frequency.cpp`, line 274). The target
   already links `test_helpers` (line 281 → `render_fingerprint.h` available) and `KrateDSP` + `Catch2`. No
   other CMake file changes (headers are header-only; no new library sources).
2. **Build:** `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release
   --target dsp_processors_tests`.
3. **Run:** `build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5` (full layer); or a
   single case positionally, e.g. `... "BrownianDrift_*"`. Perf: `... "[.perf]"`.
4. **Portability gate before commit:** `node tools/check-portability.js` (WSL g++ — catches MSVC-only
   constructs); the headers use only `<cmath>` transcendentals and `double` accumulators, no SDK constants,
   no narrowing brace-init, no SIMD.
5. No pluginval / clang-tidy plugin roster changes — this is DSP-only, no plugin touched.

---

## 9. Risks & mitigations

| Risk | Mitigation |
|---|---|
| **Long-render float phase drift** (Tidal ≥30 min = 86 M samples) | `double` phase accumulators, wrap `mod 2π` every block (§3.2). Output cast to float only at the end. |
| **Denormals** in the OU decay toward 0 | Flush `x_` to 0 below `1e-20f` in the recurrence; `dsp_test_main` also sets FTZ/DAZ. `OnePoleSmoother::process` already flushes (`smoother.h:208`). |
| **SC-002 threshold miss** | Bound is analytic, not empirical: Brownian output smoother at 150 ms gives `span·(1-coeff) ≈ 1.4e-3 < 2e-3` independent of step size; the other five have raw slopes `≪ 2e-3` (worked per component). |
| **Cross-toolchain `sin/cos/exp/pow` last-bit spread** | Never pin a render by float bits. SC-004 uses same-build exact equality; SC-003/005 use measured statistical tolerances; `render_fingerprint.h` (aggregate + checkpoints) only if a shape pin is later wanted. |
| **`-ffast-math` breaks `std::isnan`** | No `std::isnan` in any header. Non-finite hygiene via `OnePoleSmoother::setTarget` sanitize + hard clamps + bounded recurrences. |
| **SC-005 realisation mismatch (different RNG draw counts at 44.1 vs 96 kHz)** | Explicit per-modulator strategy (§0.3): deterministic/cycle-draw → option (a) like-for-like; Brownian/Spline → option (b) multi-seed distributional with measured tolerance. |
| **Catmull-Rom overshoot exceeding range** | Proven-tight: waypoints in `[-0.8,0.8]`, Lebesgue bound 1.25 → `|q| ≤ 1.0` with no clamp (§5.1); C1 preserved. |
| **Autocorrelation-time monotonicity masked by output smoother floor** | `kTauMin=0.2 s` (Brownian) and `kMinInterval=0.5 s` (Spline) sit above the smoother's ~24-30 ms time constant, so τ/interval dominate the measured decorrelation ordering. |
| **Layer violation via an accidental Layer-2 include** | Include set fixed to core + primitives + stdlib (§0.1); `node tools/lint-arch-guarded-includes.js` + review. |

---

## 10. Implementation order (suggested)

1. `BrownianDrift` (the workhorse; exercises the full decimation + OU + output-smoother + seed machinery).
2. `SplineTrajectory` (second stochastic; reuses the seed/interval pattern, adds Catmull-Rom).
3. `BreathingModulator`, `TidalModulator`, `OrbitModulator` (deterministic cores + light smoother).
4. `GrowthEnvelope` (state machine, no RNG).
5. `life_modulators_perf_test.cpp` + checked-in ns/block baseline last, once all six build.

Each component: write its `_test.cpp` boundedness+determinism cases first (they fail against an empty header),
implement to green, then add the remaining SC cases. Build `dsp_processors_tests` after every component; run
`node tools/check-portability.js` before the commit.

---

## Review notes

All review issues were accepted and applied; none rejected. Notes on provenance:

- The two "invalid hex seed literal" issues (`spec-coverage`/`reuse-reality` lenses) are the same defect
  reported twice; a single set of edits fixed all three malformed seeds — Tidal `0xT1DA→0x71DA` (§3.4),
  Orbit `0x0RB1→0x0B1F` (§4.2), Spline `0x5PL1→0x5F11` (§5.2), each in both the `rng_{...}` and
  `configuredSeed_` initializers. (`0xB17E`/`0xB2EA` were already valid and left unchanged.)
- Every code claim used in these edits was re-verified against a header read this session:
  `modulation_source.h:37,41` (only two virtuals — `setSeed` is non-virtual), `smoother.h:197`
  (`process()` is `[[nodiscard]]`), `random_source.h:110` (the `static_cast<void>` discard),
  `random.h:44,72` (`Xorshift32(uint32_t=1)` / `seed(uint32_t)`).
- No thresholds were relaxed. SC-002's `≤ 2.0e-3`, the depth/range fixity, and the C1 requirement are all
  tightened by the new detectors, not loosened.
