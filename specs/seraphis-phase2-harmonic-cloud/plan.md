# Implementation Plan: Seraphis Phase 2 — Harmonic Cloud Oscillator

**Spec:** `specs/seraphis-phase2-harmonic-cloud/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` lines 137–164
**Layer:** one new component at **Layer 3**, `dsp/include/krate/dsp/systems/harmonic_cloud.h`
**Plugin work:** none. KrateDSP-only, unit-tested. Voice/envelope/morphing are Phases 3–7.

This plan is written so an implementer never guesses the math or an API. Every reused signature below was
read from its header **this session** and is quoted with `file:line`. Every FR/SC has a concrete test file,
`TEST_CASE` name and assertion strategy. The CPU budget (SC-007) was **measured this session**, not
estimated — see §6, which contains the one decision this plan escalates.

---

## 0. Reused components — verified signatures (read this session)

| Component | Header:line | Exact reused signature / fact |
|---|---|---|
| SIMD MCF kernel (L2) | `processors/harmonic_oscillator_bank_simd.h:33-46` | Free function, **not a class**: `void processMcfBatchSIMD(float* sinState, float* cosState, const float* epsilon, const float* detuneMultiplier, float* currentAmplitude, const float* targetAmplitude, const float* antiAliasGain, const float* panLeft, const float* panRight, float ampSmoothCoeff, int numPartials, float& sumL, float& sumR) noexcept`. Header includes **only `<cstddef>`** (`:19`) — including it drags in nothing else. |
| …its implementation | `processors/harmonic_oscillator_bank_simd.cpp:78-135` | `hn::LoadU`/`hn::StoreU` with `hn::ScalableTag<float>` (`:62-63`, `:80-110`) — **unaligned**, so any `numPartials` is safe. Per partial per sample: `amp += coeff*(target*aa − amp)` (`:91-93`); `ampSample = sin*amp` (`:96`); `sumL += ampSample*panL`, `sumR += ampSample*panR` (`:99-100`); **`epsEff = clamp(eps*detune, ±1.99)`** (`:103`, scalar tail `:124`); `sNew = s + epsEff*c`, `cNew = c − epsEff*sNew` (`:104-105`). The public wrapper **accumulates**: `sumL += outSumL` (`:182-183`), so callers must zero their accumulators per sample. |
| `HarmonicOscillatorBank` (L2) | `processors/harmonic_oscillator_bank.h:74` | **Math reused, class NOT included** (see §1.1). `kAmpSmoothTimeSec = 0.002f` (`:84`); `kAntiAliasFadeStart = 0.8f` (`:87`); `kOutputClamp = 2.0f` (`:90`); `kTargetOscRms = 0.5f` (`:94`); `kMaxNormGain = 20.0f` (`:97`); `kDefaultCrossfadeTimeSec = 0.003f` (`:81`). Amp-smooth coeff: `1 − exp(−1/(kAmpSmoothTimeSec·sr))` (`:136-137`). Epsilon: `eps = 2·sin(π·f/fs)` clamped to `kMaxEpsilon = 1.99f` (`:1050-1055`). Anti-alias: fade `(nyq−f)/(0.2·nyq)` + MCF correction `max(cos(π·f/fs),0)` on the **detuned** frequency (`:1062-1090`). Pan: `angle = π/4 + pos·π/4; L = cos(angle); R = sin(angle)` (`:1123-1126`). Pitch-jump crossfade: `crossfadeThresholdRatio_ = semitonesToRatio(1.0f)` (`:190`), armed at `:388-396`, applied at `:782-788`. Tail fade of deactivated partials with high-water mark (`:746-779`, silence threshold `1e-8f` at `:756`). Normalizer: `expectedRms = sqrt(Σa²·0.5)`, `gain = min(kTargetOscRms/expectedRms, kMaxNormGain)`, smoothed (`:338-357`). `stateFinite()` bit test (`:622-633`). Test hooks precedent: `panRecomputeCount()` (`:606`), `detuneRecomputeCount()` (`:609`), `getStereoSpread()` (`:614`). |
| `BrownianDrift` (L2, Phase 1) | `processors/brownian_drift.h:94` | `class BrownianDrift : public ModulationSource`. `prepare(double)` (`:121`), `reset()` (`:133`), `setSeed(uint32_t)` (`:145`), `setSmoothness(float)` (`:152`), `setDepth(float)` (`:159`), `processBlock(size_t)` (`:194-206`), `getCurrentValue()` → `clamp(smoother, ±1)` (`:212-214`), `getSourceRange()` → `{-1,1}` (`:217`). Constants: `kTauMin = 0.2f` (`:97`), `kTauMax = 30.0f` (`:99`), `kInternalStd = 0.5f` (`:101`), `kDriftOutputSmoothMs = 150.0f` (`:103`), `kControlRateInterval = 32` (`:105`), `kWalkLimit = 4.0f` (`:226`), `kDenormalFloor = 1e-20f` (`:228`). Exact AR(1): `a = exp(−Δt/τ)`, `g = kInternalStd·sqrt(1−a²)`, `x ← μ + a(x−μ) + g·z`, `z` = sum of **three sequenced** `nextFloat()` draws (`:230-240`, `:253-270`); `τ = lerp(kTauMin, kTauMax, smoothness)` (`:231-234`). |
| `AdditiveOscillator` (L2, laws only) | `processors/additive_oscillator.h:62` | Class **not used**. Inharmonicity: `stretch = sqrt(1 + B·n²)`, `f = ratio·f0·stretch` (`:466-474`); clamp `kMaxInharmonicity = 0.1f` (`:339`). Tilt: `pow(10, tiltDb·log2(n)/20)`, identity for `n ≤ 1` (`:480-490`). Setter rejection idiom: `if (detail::isNaN(v) \|\| detail::isInf(v)) return;` then clamp (`:318-325`, `:333-340`). |
| `equalPowerGains` (L0) | `core/crossfade_utils.h:50-53` | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` → `fadeOut = cos(position·kHalfPi)`, `fadeIn = sin(position·kHalfPi)`. Contract at `:41`: **"Does NOT clamp position — caller is responsible for keeping it in [0,1]"**. |
| `Xorshift32` (L0) | `core/random.h:40` | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` (`:44`) — substitutes `kDefaultSeed = 2463534242u` for 0 (`:45`, `:73`). `nextFloat()` → [−1,1] (`:58`), `nextUnipolar()` → [0,1] (`:66`), `seed(uint32_t)` (`:72`). |
| `OnePoleSmoother` (L1) | `primitives/smoother.h:134` | `configure(float smoothTimeMs, float sampleRate)` (`:160-164`) → `coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_)` (`:163`); `setTarget(float)` — NaN→`target_=current_=0`, Inf→±1e10, otherwise a plain assignment (`:170-181`); `getCurrentValue()` (`:191`); `[[nodiscard]] float process()` (`:197`); `snapTo(float)` (`:263`). **`advanceSamples(size_t)` (`:243-254`) is NOT a bare exponential** — read literally it is: (a) `if (numSamples == 0 \|\| isComplete()) return;` where `isComplete()` is `\|current_ − target_\| < kCompletionThreshold` (`:232-234`) and the early return leaves `current_` **unchanged, not snapped**; (b) `current_ = target_ + diff·std::pow(coefficient_, N)` (`:247-249`); (c) `current_ = detail::flushDenormal(current_)` (`:250`); (d) `if (\|current_ − target_\| < kCompletionThreshold) current_ = target_;` (`:251-253`). All four steps are load-bearing for §4.5 — see D1 and §4.5. |
| `kCompletionThreshold` (L1, free) | `primitives/smoother.h:55` | `inline constexpr float kCompletionThreshold = 0.0001f;` — namespace scope, already visible through `smoother.h`, which §1.1 includes. The drift lanes use **this** symbol, not a redeclared copy. |
| `calculateOnePolCoefficient` (L1, free) | `primitives/smoother.h:77-93` | `[[nodiscard]] constexpr float calculateOnePolCoefficient(float smoothTimeMs, float sampleRate) noexcept` → `detail::constexprExp(-5000.0f/(clampedTime·sampleRate))`. The single source of truth for one-pole coefficients — the drift lanes must use it, not a hand-written `std::exp` (§4.5). |
| `pitch_utils` (L0) | `core/pitch_utils.h:23,31` | `semitonesToRatio(float) → pow(2, s/12)` (`:23-26`); `ratioToSemitones(float) → 12·log2(ratio)`, returns 0 for `ratio ≤ 0` (`:31-37`). |
| `detail::isNaN` / `isInf` / `flushDenormal` (L0) | `core/db_utils.h:54,174,167` | `constexpr bool isNaN(float)` via `std::bit_cast<uint32_t>` exponent+mantissa test (`:54-58`); `constexpr bool isInf(float)` (`:174-177`); `flushDenormal(float)` (`:167-169`). **These are the `-ffast-math`-safe replacements for `std::isnan`.** |
| `math_constants` (L0) | `core/math_constants.h:28,32,36` | `kPi`, `kTwoPi`, `kHalfPi`. |

### 0.1 Verified non-reuse (traps)

- **`Krate::DSP::kMaxPartials = 96` is `inline constexpr` at NAMESPACE scope** (`processors/harmonic_types.h:21`).
  `HarmonicCloud::kMaxPartials = 64` **must be a class-scoped `static constexpr`**. A namespace-scope
  `kMaxPartials` in `harmonic_cloud.h` is a hard redefinition error the moment both headers land in one TU —
  and they will, in `dsp_systems_tests`.
- **Do not include `harmonic_oscillator_bank.h`.** It pulls `harmonic_types.h` (hence `kMaxPartials = 96`),
  `FilterDesign`, `BiquadCoefficients` and the whole analysis data model. Only
  `harmonic_oscillator_bank_simd.h` is needed, and it includes just `<cstddef>` (`:19`).
- **`std::array<Xorshift32, N>{}` does not compile, but the fix is a one-line wrapper — do NOT copy the RNG.**
  `Xorshift32`'s only constructor is `explicit` (`random.h:44`), so value-initialising the array elements is
  copy-initialisation from `{}` and is rejected. Reproduced this session with
  `clang++ -std=c++20 -fsyntax-only -I dsp/include`:
  ```
  error: chosen constructor is explicit in copy-initialization
      struct B { std::array<Xorshift32, 64> raw{}; };
  note: explicit constructor declared here  random.h:44
  ```
  The **same probe** confirms the fix compiles clean:
  ```cpp
  struct LaneRng { Xorshift32 rng{1}; };            // implicit default ctor, non-explicit
  std::array<LaneRng, kMaxPartials> lanes{};        // OK — verified this session
  ```
  Use that. **Do not transcribe `Xorshift32::next()`/`nextFloat()` into `harmonic_cloud.h`.** Duplicating
  Layer-0 RNG internals in a Layer-3 header means a future change to `random.h`'s shift constants or its
  int→float mapping silently desynchronises the cloud's streams from `BrownianDrift`'s, with **no compile-time
  signal** — breaking SC-009's determinism goldens *and* T-DRIFT-EQUIV (§7.4), the sole honesty gate for D1.
  Both the drift lanes (§4.5) and the cloud's config/phase streams therefore hold real `Xorshift32` objects;
  the lanes do so through `LaneRng`, seeded by `deriveSeed(...)` in `reseed()` (§4.6).
- **`frequencyToCentsDeviation`** (`pitch_utils.h:175`) is not usable for SC-001 — the spec already records why.
- **`getAliasedBins` / `AliasingTestConfig`** (`spectral_analysis.h:168-183`, `:112-118`) are not used — SC-011
  records why. `calculateAliasedFrequency(fundamentalHz, harmonicNumber, sampleRate)` (`:58-78`) computes
  `fundamentalHz * harmonicNumber` internally, so SC-011 calls it as
  `calculateAliasedFrequency(f_i, 1, fs)` to fold an **arbitrary** partial frequency.

---

## 1. Component design — `HarmonicCloud`

### 1.1 Placement, layer legality, ODR

- Path: `dsp/include/krate/dsp/systems/harmonic_cloud.h` (header-only, matching every other Layer 3 component).
- Includes, and nothing else (FR-001):
  ```cpp
  #include <krate/dsp/core/crossfade_utils.h>   // L0 equalPowerGains
  #include <krate/dsp/core/db_utils.h>          // L0 detail::isNaN/isInf/flushDenormal
  #include <krate/dsp/core/math_constants.h>    // L0 kPi/kTwoPi/kHalfPi
  #include <krate/dsp/core/pitch_utils.h>       // L0 semitonesToRatio
  #include <krate/dsp/core/random.h>            // L0 Xorshift32
  #include <krate/dsp/primitives/smoother.h>    // L1 OnePoleSmoother (norm gain only)
  #include <krate/dsp/processors/harmonic_oscillator_bank_simd.h>  // L2 kernel
  #include <algorithm> <array> <cmath> <cstddef> <cstdint>
  ```
  Highest layer included is 2 — legal for a Layer 3 component.
- **ODR sweep executed this session** (`grep -rn <name> dsp/ plugins/ tools/`, verified live against a known
  hit): `HarmonicCloud` **0**, `PartialDriftBank` **0**, `DriftBank` **0**, `CloudEnvelope` **0**,
  `PartialEnvelope` **0**, `CloudPartialState` **0**. All clear. `HarmonicCloud` is the **only** new
  class/struct name this plan introduces; the drift lanes are a private nested `struct DriftLanes` (§4.5), so
  no additional top-level name enters `Krate::DSP`.

### 1.2 Constants (all `static constexpr` **inside** `HarmonicCloud`)

| Constant | Value | Source / FR |
|---|---|---|
| `kMaxPartials` | `64` | FR-012, Clarifications OQ-1. **Class-scoped** — see §0.1. |
| `kControlChunkSamples` | `64` | FR-032 (Clarifications Q7) |
| `kDriftControlInterval` | `32` | mirrors `BrownianDrift::kControlRateInterval` (`brownian_drift.h:105`) |
| `kDriftTauMin` / `kDriftTauMax` | `0.2f` / `30.0f` s | `brownian_drift.h:97,99` |
| `kDriftInternalStd` | `0.5f` | `brownian_drift.h:101` |
| `kDriftOutputSmoothMs` | `150.0f` | `brownian_drift.h:103` — the FR-071 rate bound depends on it |
| `kDriftWalkLimit` / `kDriftDenormalFloor` | `4.0f` / `1e-20f` | `brownian_drift.h:226,228` |
| `kAmpSmoothTimeSec` | `0.002f` | FR-014, `harmonic_oscillator_bank.h:84` |
| `kAntiAliasFadeStart` | `0.8f` | FR-015, `…bank.h:87` |
| `kMaxEpsilon` | `1.99f` | FR-015, `…bank.h:1050` |
| `kOutputClamp` | `2.0f` | FR-006, `…bank.h:90` |
| `kTargetOscRms` / `kMaxNormGain` | `0.5f` / `20.0f` | FR-017, `…bank.h:94,97` |
| `kNormGainSmoothMs` | `20.0f` | FR-017 (cloud-level `OnePoleSmoother`) |
| `kMinFundamentalHz` / `kMaxFundamentalHz` | `20.0f` / `4000.0f` | FR-013 |
| `kCrossfadeTimeSec` | `0.003f` | FR-013, `…bank.h:81` |
| `kMaxInharmonicity` | `0.1f` | FR-052, `additive_oscillator.h:339` |
| `kMinTiltDbPerOct` / `kMaxTiltDbPerOct` | `-12.0f` / `+12.0f` | FR-062, `spectral_tilt.h:98-101` |
| `kGravityExponentRange` | `0.1f` | FR-081 |
| `kRichnessMinExponent` / `kRichnessMaxExponent` | `3.0f` / `0.5f` | FR-041(b): `p(r) = 3.0 − 2.5r` |
| `kMaxMutationDepth` | `0.75f` | FR-071 |
| `kMutationSmoothness` | `0.5f` | FR-072 (documented cloud constant → τ ≈ 15.1 s) |
| `kDriftIndexExponent` | `1.0f` | FR-022 |
| `kMaxDriftCents` | `50.0f` | FR-033 documented maximum |
| `kMinAttackSec` / `kMaxAttackSec` | `0.05f` / `30.0f` | FR-023. **50 ms = 25× `kAmpSmoothTimeSec`**, satisfying FR-023's ≥20× rule with margin (§4.4). |
| `kMinDecaySec` / `kMaxDecaySec` | `0.05f` / `60.0f` | FR-023 |
| `kMaxEnvOffsetSec` | `2.0f` | FR-023 documented maximum offset |
| `kQuiescenceAmplitude` | `1.0e-5f` | FR-016 (−100 dBFS) |
| `kTailSilenceThreshold` | `1.0e-8f` | FR-043, `…bank.h:756`. Also the **denormal guard threshold on `currentAmplitude_`** — §4.9. |

One constant is **not** redeclared here and must not be: the drift lanes' output smoother uses the shared
namespace-scope `Krate::DSP::kCompletionThreshold = 0.0001f` (`primitives/smoother.h:55`) directly, because
§4.5 is a literal transcription of `OnePoleSmoother::advanceSamples` and must snap on exactly the same
threshold value that `BrownianDrift`'s smoother snaps on. `smoother.h` is already in §1.1's include set.

### 1.3 State layout (SoA, `alignas(32)`, FR-024)

```cpp
// --- kernel-facing SoA (exact parameter contract of processMcfBatchSIMD) ---
alignas(32) std::array<float, kMaxPartials> sinState_{};          // FR-016: seeded, NOT zeroed
alignas(32) std::array<float, kMaxPartials> cosState_{};
alignas(32) std::array<float, kMaxPartials> epsilon_{};           // 2·sin(π·f_i/fs), clamped ±1.99
alignas(32) std::array<float, kMaxPartials> detuneMultiplier_{};  // per-chunk, from drift
alignas(32) std::array<float, kMaxPartials> currentAmplitude_{};  // kernel-owned
alignas(32) std::array<float, kMaxPartials> targetAmplitude_{};   // gain·a_i·w_i·env_i
alignas(32) std::array<float, kMaxPartials> antiAliasGain_{};     // fade × MCF correction
alignas(32) std::array<float, kMaxPartials> panLeft_{};
alignas(32) std::array<float, kMaxPartials> panRight_{};

// --- per-partial living state (FR-021..FR-024) ---
alignas(32) std::array<float, kMaxPartials> frequencyHz_{};       // FR-083 combined law, undetuned
alignas(32) std::array<float, kMaxPartials> baseAmplitude_{};     // a_i = rolloff·tilt (FR-041×FR-061)
alignas(32) std::array<float, kMaxPartials> unmutatedTarget_{};   // gainSmoothed·a_i (FR-008/FR-017)
alignas(32) std::array<float, kMaxPartials> panPosition_{};       // FR-021, [-1,+1]
alignas(32) std::array<float, kMaxPartials> positionScatter_{};   // s_i ~ U[-1,1], once per seed
std::array<bool,  kMaxPartials> positionOverridden_{};            // FR-008 setPartialPosition
alignas(32) std::array<float, kMaxPartials> driftAmount_{};       // amount_i (FR-022)
alignas(32) std::array<float, kMaxPartials> attackOffsetSec_{};   // FR-023
alignas(32) std::array<float, kMaxPartials> decayOffsetSec_{};
alignas(32) std::array<float, kMaxPartials> envValue_{};          // [0,1]
std::array<std::uint8_t, kMaxPartials> envStage_{};               // 0 Idle 1 Attack 2 Hold 3 Release
std::array<bool,  kMaxPartials> masked_{};                        // FR-008 solo/mask

// --- two drift lane banks (§4.5), SoA, no BrownianDrift objects ---
struct LaneRng { Xorshift32 rng{1}; };   // §0.1 trap 3 — the REAL Layer-0 RNG, wrapped so the array
                                         // can be value-initialised. Never a hand-copied xorshift.
struct DriftLanes {                 // 64 lanes; two instances: detuneLanes_, mutationLanes_
    alignas(32) std::array<float, kMaxPartials> walk{};      // x_i
    alignas(32) std::array<float, kMaxPartials> smoothCur{}; // one-pole current
    alignas(32) std::array<float, kMaxPartials> smoothTgt{}; // one-pole target
    std::array<LaneRng, kMaxPartials> rng{};                 // see §0.1 trap 3
    float a = 0.0f, g = 0.0f;                                // AR(1) coefficients
    float depth = 1.0f;                                      // BrownianDrift::setDepth semantics
    int   samplesUntilControl = 0;                           // SHARED across lanes (§4.5)
};
DriftLanes detuneLanes_{};     // FR-031 bank
DriftLanes mutationLanes_{};   // FR-072 bank
std::array<float, kDriftControlInterval + 1> smoothPowTable_{}; // coeff^k, k = 0..32

// --- cloud-level scalars ---
double sampleRate_ = 44100.0;  float nyquist_ = 0.0f;  float invSampleRate_ = 0.0f;
float  fadeStart_ = 0.0f;      float invFadeRange_ = 0.0f;   // FR-015 fade band (§2, §5)
float  ampSmoothCoeff_ = 0.0f;
int    activeCount_ = 1;       int kernelCount_ = 1;   // kernelCount_ ≥ activeCount_ (tail, FR-043)
int    tailHighWater_ = 1;                             // FR-043 high-water mark (§3)
bool   freqDirty_ = false;     bool ampDirty_ = false; // §4.8 deferred config-rate recompute
OnePoleSmoother normGain_;                              // FR-017, ONE instance
Xorshift32 configRng_{1};                               // per-seed draws, fixed order (§4.6)
Xorshift32 phaseRng_{1};                                // FR-016 phase redraws only
std::uint32_t configuredSeed_ = kDefaultCloudSeed;
bool  prepared_ = false;  bool gate_ = false;
float lastOutL_ = 0.0f, lastOutR_ = 0.0f;
float crossfadeOldL_ = 0.0f, crossfadeOldR_ = 0.0f;
int   crossfadeRemaining_ = 0, crossfadeLengthSamples_ = 1;
float crossfadeThresholdRatio_ = 1.0f;
std::uint64_t driftReadCount_ = 0;                      // SC-015 clause 2(b) test hook
// parameter shadow: fundamentalHz_, richness_, inharmonicity_, tiltDb_, mutationAmount_, gravity_,
// driftCents_, driftSmoothness_, stereoSpread_, attackSec_, decaySec_, offsetSpread_
```

**Names are pinned here and used identically in every section below.** The Mutation macro's shadow is
`mutationAmount_` (§3, §4.2, §4.8); the FR-072 lane bank is `mutationLanes_`; the FR-031 lane bank is
`detuneLanes_`. There is **no** member called `mutation_` — an earlier draft had both a lane bank and a
parameter shadow under that name, which is a redefinition error. `fadeStart_`, `invFadeRange_` (assigned in
§5, read in §2) and `tailHighWater_` (read in §3) are declared above; all three were used before being
declared in an earlier draft.

Footprint ≈ **7.6 KB** per cloud (25 float[64] arrays + 2 lane banks + scalars). Fits L1 for one voice; at
Phase 7's 16 voices the per-voice working set is still hot for the duration of that voice's block, which is
the access pattern the measurement in §6 exercises.

### 1.4 Public API (real signatures)

```cpp
namespace Krate::DSP {

/// @par Layer: 3 (systems/). Dependencies: Layer 0/1/2 + stdlib only.
/// @par Real-Time Safety: everything except prepare() is noexcept, allocation-free, lock-free.
class HarmonicCloud {
public:
    static constexpr std::size_t kMaxPartials = 64;   // FR-012 — CLASS-SCOPED (see plan §0.1)
    // … remaining constants per §1.2 …

    HarmonicCloud() noexcept = default;
    HarmonicCloud(const HarmonicCloud&) = delete;
    HarmonicCloud& operator=(const HarmonicCloud&) = delete;
    HarmonicCloud(HarmonicCloud&&) noexcept = default;
    HarmonicCloud& operator=(HarmonicCloud&&) noexcept = default;

    // ---- lifecycle (FR-003) ----
    void prepare(double sampleRate) noexcept;   // NOT RT-safe; re-derives every sr-dependent coeff
    void reset() noexcept;                      // RT-safe; silences state, reseeds, keeps configuration

    // ---- pitch (FR-013) ----
    void  setFundamentalHz(float hz) noexcept;                     // clamp [20, 4000]
    [[nodiscard]] float getFundamentalHz() const noexcept;

    // ---- five macros ----
    void  setRichness(float r) noexcept;            // FR-041, [0,1]
    void  setInharmonicity(float B) noexcept;       // FR-051/052, [0, 0.1]
    void  setSpectralTiltDb(float dbPerOct) noexcept; // FR-061/062, [-12, +12]
    void  setMutation(float m) noexcept;            // FR-071, [0,1]
    void  setSpectralGravity(float g) noexcept;     // FR-081, [-1,+1]
    [[nodiscard]] float getRichness() const noexcept;
    [[nodiscard]] float getInharmonicity() const noexcept;
    [[nodiscard]] float getSpectralTiltDb() const noexcept;
    [[nodiscard]] float getMutation() const noexcept;
    [[nodiscard]] float getSpectralGravity() const noexcept;

    // ---- drift (FR-033/FR-035) ----
    void  setDriftDepthCents(float cents) noexcept;  // [0, kMaxDriftCents]; detune bank ONLY
    void  setDriftSmoothness(float s) noexcept;      // [0,1];             detune bank ONLY
    [[nodiscard]] float getDriftDepthCents() const noexcept;
    [[nodiscard]] float getDriftSmoothness() const noexcept;

    // ---- stereo (FR-021/FR-093) ----
    void  setStereoSpread(float spread) noexcept;    // [0,1]
    [[nodiscard]] float getStereoSpread() const noexcept;

    // ---- per-partial envelope (FR-023) ----
    void  setAttackTimeSec(float seconds) noexcept;   // [0.05, 30]
    void  setDecayTimeSec(float seconds) noexcept;    // [0.05, 60]
    void  setEnvelopeOffsetSpread(float spread) noexcept; // [0,1]
    [[nodiscard]] float getAttackTimeSec() const noexcept;
    [[nodiscard]] float getDecayTimeSec() const noexcept;
    [[nodiscard]] float getEnvelopeOffsetSpread() const noexcept;
    void  noteOn()  noexcept;   // FR-016/FR-023 gate on  (retrigger rule §4.7)
    void  noteOff() noexcept;   // gate off → release

    // ---- determinism (FR-005) ----
    void  setSeed(std::uint32_t seed) noexcept;      // redraws all once-per-seed state (§4.6)
    [[nodiscard]] std::uint32_t getSeed() const noexcept;

    // ---- render (FR-004) ----
    void processStereoBlock(float* leftOutput, float* rightOutput,
                            std::size_t numSamples) noexcept;

    // ---- FR-008 test/introspection surface (public contract, not #ifdef) ----
    [[nodiscard]] std::size_t getActivePartialCount() const noexcept;     // N(r), explicit state
    [[nodiscard]] float getPartialFrequencyHz(std::size_t i) const noexcept;   // undetuned, FR-083
    [[nodiscard]] float getPartialCurrentAmplitude(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialTargetAmplitude(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialUnmutatedTargetAmplitude(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialAntiAliasGain(std::size_t i) const noexcept; // plan addition, §1.5
    [[nodiscard]] float getPartialPanLeft(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialPanRight(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialPosition(std::size_t i) const noexcept;
    [[nodiscard]] float getPartialDriftDetune(std::size_t i) const noexcept;  // frequency MULTIPLIER
    [[nodiscard]] float getPartialSinState(std::size_t i) const noexcept;     // plan addition, §1.5
    [[nodiscard]] float getPartialCosState(std::size_t i) const noexcept;     // plan addition, §1.5
    [[nodiscard]] float getDriftLaneValue(std::size_t i) const noexcept;      // plan addition, §1.5
    [[nodiscard]] float getMutationLaneValue(std::size_t i) const noexcept;   // plan addition, §1.5
    [[nodiscard]] std::uint64_t getDriftReadCount() const noexcept;           // plan addition, §1.5
    [[nodiscard]] bool  isQuiescent() const noexcept;                         // plan addition, §1.5
    [[nodiscard]] bool  stateFinite() const noexcept;                         // bit test, ±ffast-math safe

    void setPartialPosition(std::size_t index, float position) noexcept;  // FR-008, Phase-7 hook
    void setPartialMask(std::size_t index, bool active) noexcept;         // FR-008 mask
    void soloPartial(std::size_t index) noexcept;                         // FR-008 solo
    void clearPartialMask() noexcept;
};

} // namespace Krate::DSP
```

Every `const` accessor returns `0.0f` for `index >= kMaxPartials`; every mutator with an out-of-range index
is a no-op (FR-008). Every setter follows the FR-007 idiom (§4.8).

### 1.5 Seven accessors this plan adds to FR-008's surface, with justification

FR-008 enumerates a *required minimum*. These seven are additions, each because a named SC clause is
otherwise unmeasurable on the shipped path:

1. **`getPartialAntiAliasGain(i)`** — the kernel's steady state is `currentAmplitude → targetAmplitude·aaGain`
   (`…_simd.cpp:91-93`). SC-013's four clauses are phrased against a partial's *steady-state target
   amplitude*. At SC-013's own configuration (`f0 = 110 Hz`, 64 partials, top partial 7.04 kHz) the MCF
   correction alone gives `aa ≈ 0.895`, so comparing `current` against a reference that omits `aa` would fail
   a correct implementation by 10 %. SC-011 also wants the fade value directly.

   **What "steady-state target" resolves to, precisely.** It is **not** `getPartialTargetAmplitude(i)`:
   §1.3:141 and §4.2 define `targetAmplitude_[i] = unmutatedTarget_[i]·w_i·env_i`, which *moves with the
   envelope*. Using it as the crossing reference makes "50 % of target" fire when `env ≈ 2 × (smoother lag)`,
   not at 50 % of the sounding level, and makes SC-013 clause 4 ("≤ 1 % of its target" after gate-off, where
   `env → 0` drives `target → 0`) degenerate. SC-013 pins Mutation = drift = 0, hence `w_i ≡ 1`, so the
   correct reference — excluding both `w_i` and `env_i` — is

   > `steadyStateTarget(i) = getPartialUnmutatedTargetAmplitude(i) * getPartialAntiAliasGain(i)`

   which is exactly the level the partial holds during the FR-023 Hold stage. `getPartialUnmutatedTarget-
   Amplitude` is already an FR-008-required accessor. **All four SC-013 clauses use this expression**
   (§7.4). SC-016's row already reads the right quantity by construction (it samples with the gate held after
   the attack, so `env = 1`).
2. **`getPartialSinState(i)` / `getPartialCosState(i)`** — SC-006's retrigger clause asserts *the mechanism*:
   "every partial's phase state is **unchanged** at the retrigger sample (sampled via FR-008's accessors
   before and after)". FR-008's listed accessors expose no phase state, so that clause cannot be written.
3. **`getDriftLaneValue(i)` / `getMutationLaneValue(i)`** — return `std::clamp(detuneLanes_.smoothCur[i],
   -1.0f, 1.0f)` and the `mutationLanes_` equivalent: the **exact shape of `BrownianDrift::getCurrentValue()`**
   (`brownian_drift.h:212-214`). T-DRIFT-EQUIV (§7.4) is D1's only honesty gate and must compare a raw lane
   value against `BrownianDrift::getCurrentValue()`. It cannot be run through `getPartialDriftDetune(i)`,
   which is documented as a *frequency multiplier* — `semitonesToRatio(driftCents_·driftAmount_[i]·d_i/100)`
   (§4.2). Recovering `d_i` from it means dividing out `driftCents_·driftAmount_[i]` and inverting
   `semitonesToRatio`, and at the index the gate would naturally use the arithmetic does not survive float32:
   FR-022 gives `driftAmount_[0] = (1/64)^1·u_0 ∈ [0.0078, 0.0156]`, so at `kMaxDriftCents = 50` partial 0's
   multiplier lies within `4.5e-4` of 1.0, where one float ULP is `1.19e-7`; with
   `dcents/dmultiplier = 1200/ln 2 ≈ 1731` the recovered lane value carries `≈ 1731·1.19e-7/0.78 ≈ 2.6e-4` of
   error — **26× the 1e-5 tolerance T-DRIFT-EQUIV asserts**. (At index 63, `driftAmount ≈ 1`, the same
   arithmetic gives `≈ 4e-6`, which would fit — but the accessor removes the derivation entirely and lets the
   gate run at *every* index.) The mutation counterpart is required because `mutationLanes_` is configured
   from a fixed cloud constant (`kMutationSmoothness`) that no other accessor exposes, and D1 replaces
   `BrownianDrift` in **both** banks.
4. **`getDriftReadCount()`** — SC-015 clause 2(b) asserts "the number of cloud-side drift **reads per
   partial** equals `Σ ceil(blockSize/64)`". That count is not derivable from any other accessor. Precedent
   for a pure counting test hook exists on the reference class (`panRecomputeCount()`
   `harmonic_oscillator_bank.h:606`, `detuneRecomputeCount()` `:609`). Counted as reads **per partial** —
   incremented once per chunk, not once per lane.
5. **`isQuiescent()`** — `!gate_ && allBelow(currentAmplitude_, kQuiescenceAmplitude)`, the predicate §4.7
   already computes. It is the guard the §4.1 silent-render early-out keys on, so a test must be able to
   observe it, and it is the retirement signal Phase 7's `SeraphisVoice`/`VoiceAllocator` needs for a voice
   whose 10 s+ release has finished (roadmap lines 286–288).

---

## 2. Frequency pipeline (FR-013, FR-051, FR-081, FR-083, FR-015)

Recomputed at **configuration rate only** (fundamental / inharmonicity / gravity / richness change), never
per sample and never per chunk.

```
for n = 1 … kMaxPartials:                       // 1-based partial index
    ratio_g(n) = pow(n, 1 + g · kGravityExponentRange)          // FR-081, exactly n at g = 0
    stretch(n) = sqrt(1 + B · n²)                                // FR-051, additive_oscillator.h:472
    f_n        = f0 · ratio_g(n) · stretch(n)                    // FR-083, fixed order
    frequencyHz_[n-1] = f_n
    epsilon_[n-1]     = clamp(2·sin(π · f_n · invSampleRate_), ±kMaxEpsilon)   // …bank.h:1054-1055
```

`ratio_g(1) = 1` exactly for every `g` (`pow(1, x) == 1`), so the fundamental never moves — SC-004 clause 3
excludes `n = 1` for exactly this reason.

**Anti-aliasing (FR-015) is per-chunk, because it depends on the drifting detune** (the reference computes it
on the detuned frequency, `…bank.h:1073`). Computed without a `cos` call, using the identity
`cos(π f/fs) = sqrt(1 − (ε/2)²)` for `ε = 2 sin(π f/fs)`:

```
epsEff = epsilon_[i] · detuneMultiplier_[i]              // same product the kernel forms (…_simd.cpp:103)
q      = 0.5f · epsEff
corr   = (1 − q·q) > 0 ? sqrt(1 − q·q) : 0.0f            // == max(cos(π·fEff/fs), 0), …bank.h:1078-1079
fEff   = frequencyHz_[i] · detuneMultiplier_[i]
fade   = fEff <= fadeStart_ ? 1.0f
       : fEff >= nyquist_   ? 0.0f
       :                      (nyquist_ − fEff) · invFadeRange_
antiAliasGain_[i] = fade · corr
```

with `fadeStart_ = kAntiAliasFadeStart · nyquist_` and `invFadeRange_ = 1/(nyquist_ − fadeStart_)`. It
replaces a `cosf` (≈8 ns) with a `sqrtss` (≈1 ns) — 512 evaluations per 512-sample block, so the substitution
is worth ≈3.5 µs/block against a 53.3 µs budget.

**Exactly how this relates to the reference (do not call it "identical" — it is not, under detune).** The
reference evaluates `cos(π·f·detune/fs)` on the *nominal detuned frequency* (`…bank.h:1073`, `:1078`); the
form above evaluates `sqrt(1 − (ε·detune/2)²)` where `ε = 2·sin(π·f/fs)` (`…bank.h:1054`). Since
`detune·sin θ ≠ sin(detune·θ)`, the two agree **only at `detune = 1`** — where both reduce to `cos(π f/fs)`
exactly — and diverge as detune moves away from 1. Measured at `fs = 48 kHz` and the maximum
`kMaxDriftCents = 50` (`detune = 1.0293`):

| partial frequency | plan form | reference form | difference |
|---|---|---|---|
| 7 040 Hz (SC-010's top partial) | 0.88913 | 0.88964 | **0.005 dB** |
| 19 000 Hz | 0.22364 | 0.28617 | **2.09 dB** |

The plan form is nevertheless the **more correct** one for this component: the kernel synthesizes on the
orbit defined by the *clamped* effective coefficient `epsEff = clamp(ε·detune, ±1.99)`
(`…_simd.cpp:103`), i.e. an orbit whose true half-angle is `asin(epsEff/2)`, and `sqrt(1 − (epsEff/2)²)` is
the exact MCF amplitude correction *for that orbit*. The reference's `cos(π·f·detune/fs)` corrects for a
frequency the kernel does not actually run at once the clamp engages. This is recorded as D5, restated.

*Effect on the criteria.* SC-010 renders with drift at 0, so `detune ≡ 1` and both forms are the same
function — its 0.5 dB amplitude tolerance is untouched (the measured 44.1-vs-96 kHz spread at its pinned
7.04 kHz top partial is ≈0.009 dB). SC-011 is the criterion that exercises the detuned path at the extreme,
and it asserts the *rendered* alias floor, not the correction value, so it is agnostic to which form is used.

**Fundamental change (FR-013).** `setFundamentalHz` recomputes epsilon **without touching `sinState_`/
`cosState_`**, and arms a per-channel output crossfade when the pitch ratio exceeds one semitone, mirroring
`…bank.h:388-396` + `:782-788`:

```
ratio = max(newHz/oldHz, oldHz/newHz);
if (oldHz > 0 && ratio > crossfadeThresholdRatio_) {         // semitonesToRatio(1.0f), …bank.h:190
    crossfadeOldL_ = lastOutL_;  crossfadeOldR_ = lastOutR_;  // per channel (see note)
    crossfadeRemaining_ = crossfadeLengthSamples_;             // 3 ms, …bank.h:81,185-187
}
```

*Deviation, deliberate:* the reference snapshots a **mono** `lastOutputSample_ = (L+R)/2` (`…bank.h:794`) and
fades that scalar into both channels (`:785-786`), which collapses the image for 3 ms. Phase 2 snapshots L and
R separately. This is the same mechanism (a 3 ms linear fade of the pre-jump level into the post-jump output),
applied per channel; nothing in FR-013 or SC-006 depends on the mono collapse.

---

## 3. Amplitude chain (FR-041, FR-061, FR-017, FR-071, FR-023)

The composition order is **fixed and documented in the header** (FR-017):

```
a_i               = richnessRolloff(i) · tiltGain(i)                    // config rate
gainTarget        = min(kTargetOscRms / sqrt(Σ_{i<N} a_i² · 0.5), kMaxNormGain)   // …bank.h:342-347
                    → normGain_.setTarget(gainTarget)                   // CONFIG RATE — see below
gainSmoothed      = normGain_.getCurrentValue()                         // per chunk, after advanceSamples
unmutatedTarget_i = gainSmoothed · a_i                                  // per chunk, FR-008 accessor
targetAmplitude_i = unmutatedTarget_i · w_i(mutation) · env_i(t)        // per chunk
currentAmplitude_i ← kernel: += ampSmoothCoeff·(targetAmplitude_i·antiAliasGain_i − currentAmplitude_i)
```

**`normGain_.setTarget(currentNormGainTarget())` is the last statement of `recalculateAmplitudes()`, and
omitting it silently disables FR-017 entirely.** `reset()` calls `normGain_.snapTo(...)` (§5), after which
`current_ == target_`; `OnePoleSmoother::advanceSamples` then early-returns on
`isComplete()` (`smoother.h:244`) forever, so without a `setTarget` on every Richness/Tilt change the gain is
frozen at its reset value for the life of the instance — Richness and Tilt would not renormalize at all. The
failure is quiet (no NaN, no clip, just a wrong level), and it would surface as SC-016's ±3 dB level
stability and SC-006's Richness/Tilt sweeps failing for a reason no assertion names. Therefore: `setTarget`
runs at **config rate** inside `recalculateAmplitudes()` (i.e. whenever `baseAmplitude_` or `activeCount_`
change); `snapTo` appears **only** in `reset()`; §4.2 step 4's `advanceSamples(chunk)` is what tracks it.

- **Richness (FR-041).** `N(r) = clamp(round(pow(64.0f, r)), 1, 64)`; `p(r) = 3.0f − 2.5f·r`;
  `a_n = pow(n, −p(r))` for `n ∈ [1, N]`, `a_n = 0` for `n > N`. Verified against SC-014's pinned
  expectations: `r = 0, .25, .5, .75, 1 → N = 1, 3, 8, 23, 64`.
- **Tilt (FR-061).** `tiltGain(n) = (tiltDb == 0 || n <= 1) ? 1 : pow(10, tiltDb·log2(n)/20)`, the identity
  branch copied from `additive_oscillator.h:481-489`.
- **Normalizer input is `a_i` only** (FR-017 / Clarifications Q6). Mutation weights, drift and the envelope
  are **outside** it, by requirement — recomputing the gain from mutated amplitudes would cancel the ±3 dB
  level movement SC-016 exists to observe. The gain is one scalar for all partials, so it cannot bend the
  measured tilt slope (SC-003 relies on that).
- **Mutation (FR-071).** `w_i = 1 + mutationAmount_ · kMaxMutationDepth · d_i`, `d_i ∈ [−1,+1]` from
  `mutationLanes_`. `mutationAmount_ = 0 ⇒ w_i ≡ 1.0f` **exactly** (the multiply is by `1 + 0·…`; implement as
  an explicit `if (mutationAmount_ <= 0.0f) w = 1.0f;` so no rounding can produce `0.9999999` and fail
  SC-016's "exactly 1.0").
  Bound: `w_i ∈ [1 − 0.75, 1 + 0.75] = [0.25, 1.75]` (FR-073) because `|d_i| ≤ 1` by lane clamp (§4.5).
- **Masking (FR-008).** `masked_[i] == true ⇒ targetAmplitude_i = 0`, applied at the *end* of the chain so the
  FR-014 smoother fades the partial out and solo cannot click.

**Tail handling for FR-043.** Rather than a scalar tail loop (the reference's `…bank.h:746-779`), the cloud
passes `kernelCount_ = max(activeCount_, tailHighWater_)` to the kernel and sets `targetAmplitude_[i] = 0`
for `i ≥ activeCount_`. The kernel's own uniform `ampSmoothCoeff` then fades the departing partials
(FR-043's requirement, "faded out rather than truncated") **inside the SIMD path** — simpler and faster than a
scalar tail, and it removes the reference's per-sample branch. When every `currentAmplitude_[i]` for
`i ∈ [activeCount_, tailHighWater_)` drops below `kTailSilenceThreshold`, `tailHighWater_` is lowered to
`activeCount_` (checked once per chunk, not per sample).

---

## 4. Algorithms

### 4.1 Chunked render loop (FR-004, FR-032, FR-034, Edge Cases)

```cpp
void processStereoBlock(float* L, float* R, std::size_t n) noexcept {
    if (L == nullptr || R == nullptr || n == 0) return;          // FR-004, Edge Cases
    if (!prepared_) { std::fill_n(L, n, 0.0f); std::fill_n(R, n, 0.0f); return; }  // …bank.h:809-815

    if (isQuiescent()) {                          // §4.7 predicate — see "Quiescent early-out" below
        advanceDriftLanes(detuneLanes_,   n);     // lanes keep free-running (FR-001 "nothing is static")
        advanceDriftLanes(mutationLanes_, n);
        driftReadCount_ += (n + kControlChunkSamples - 1) / kControlChunkSamples;  // SC-015 2(b) unaffected
        std::fill_n(L, n, 0.0f); std::fill_n(R, n, 0.0f);
        return;
    }

    std::size_t done = 0;
    while (done < n) {
        const std::size_t chunk = std::min(kControlChunkSamples, n - done);
        updateControl(chunk);                    // §4.2 — one drift read per partial per chunk
        for (std::size_t s = 0; s < chunk; ++s) {
            float sl = 0.0f, sr = 0.0f;          // MUST zero: the kernel ACCUMULATES (…_simd.cpp:182-183)
            processMcfBatchSIMD(sinState_.data(), cosState_.data(),
                                epsilon_.data(), detuneMultiplier_.data(),
                                currentAmplitude_.data(), targetAmplitude_.data(),
                                antiAliasGain_.data(), panLeft_.data(), panRight_.data(),
                                ampSmoothCoeff_, static_cast<int>(kernelCount_), sl, sr);
            if (crossfadeRemaining_ > 0) {       // FR-013, …bank.h:782-788
                const float p = static_cast<float>(crossfadeRemaining_)
                              / static_cast<float>(crossfadeLengthSamples_);
                sl = crossfadeOldL_ * p + sl * (1.0f - p);
                sr = crossfadeOldR_ * p + sr * (1.0f - p);
                --crossfadeRemaining_;
            }
            L[done + s] = std::clamp(sl, -kOutputClamp, kOutputClamp);   // FR-006
            R[done + s] = std::clamp(sr, -kOutputClamp, kOutputClamp);
        }
        lastOutL_ = L[done + chunk - 1];  lastOutR_ = R[done + chunk - 1];
        done += chunk;
    }
}
```

Every block size is `ceil(n / 64)` chunks with a possibly-short final chunk; a 16384-sample block is 256
chunks, not one frozen 341 ms detune (Edge Cases). Nothing in the loop is keyed to the caller's block size, so
the render is **block-size-invariant up to the 64-sample grid** — the property SC-015 clause 2(c) measures.

**Guard paths are behaviour, not defensive noise, and are tested (§7.4 `HarmonicCloud_GuardPaths`).** The
three lines above implement three separate spec statements: FR-004's "a zero-length block is a no-op; null
buffers are rejected without writing"; Edge Cases' "processing before `prepare()` outputs silence rather than
reading uninitialized coefficients"; and FR-003's "after `prepare`, processing is well-defined with no prior
parameter call" (§5). A regression that dropped the null check would surface as a host crash in Phase 8, not
as a red test, unless something asserts it here.

**Quiescent early-out (`isQuiescent()`).** Without it, a cloud whose release has completed still performs
512 `processMcfBatchSIMD` calls over `kernelCount_` partials plus 2 × 64 lane updates per block — essentially
the full ≈28,900 ns (§6) to produce guaranteed silence. `kernelCount_ = max(activeCount_, tailHighWater_)`
never drops below 1, so nothing else retires the work. This matters at Phase 7, whose voice-steal policy has
"long-release amnesty since releases are 10 s+" (roadmap line 287) and therefore keeps finished voices
resident. The early-out **still advances both lane banks by `n` samples and still increments
`driftReadCount_` by `ceil(n/64)`**, so free-running life-modulation continues (roadmap Key Design Decision 1)
and SC-015's partition-invariance (2a), read-count (2b) and fingerprint (2c) clauses are unaffected — a
silent render and a sounding render of the same length leave identical lane state. `isQuiescent()` is public
(§1.5) so Phase 7 can retire the voice and so §7.4's `[.perf]` case can assert the silent cost is materially
below the sounding cost.

### 4.2 `updateControl(chunk)` — the once-per-chunk control pass

Ordered, and the order is load-bearing:

0. **Consume the config-rate dirty flags** (§4.8) — at most one recompute of each per chunk, regardless of
   how many setters ran since the last chunk:
   ```
   if (freqDirty_) { recalculateFrequencies(); freqDirty_ = false; ampDirty_ = true; }  // N(r) may have moved
   if (ampDirty_)  { recalculateAmplitudes();  ampDirty_  = false; }                    // ends in normGain_.setTarget
   ```
1. `advanceDriftLanes(detuneLanes_, chunk)` and `advanceDriftLanes(mutationLanes_, chunk)` (§4.5).
2. `++driftReadCount_` (once — the count is *per partial*, SC-015 clause 2(b)).
3. Per active partial `i` (0-based), reading each lane's smoothed value **exactly once**:
   ```
   d      = clamp(detuneLanes_.smoothCur[i], -1.0f, 1.0f);     // == BrownianDrift::getCurrentValue, :212-214
   cents  = driftCents_ · driftAmount_[i] · d;                 // FR-033, |cents| ≤ driftCents_
   detuneMultiplier_[i] = semitonesToRatio(cents / 100.0f);    // pitch_utils.h:23
   → recompute antiAliasGain_[i] per §2
   dm     = clamp(mutationLanes_.smoothCur[i], -1.0f, 1.0f);
   w      = (mutationAmount_ <= 0.0f) ? 1.0f
                                      : 1.0f + mutationAmount_ · kMaxMutationDepth · dm;   // FR-071
   env    = advanceEnvelope(i, chunk);                          // §4.4
   unmutatedTarget_[i] = gainSmoothed · baseAmplitude_[i];      // gainSmoothed from step 4's PREVIOUS chunk
   targetAmplitude_[i] = masked_[i] ? 0.0f : unmutatedTarget_[i] · w · env;
   ```
4. `normGain_.advanceSamples(chunk)` (one call, not 64), then `gainSmoothed = normGain_.getCurrentValue()`
   for the next chunk. FR-017's target is set at config rate in step 0's `recalculateAmplitudes()` (§3).
5. Tail high-water check (§3), then the `currentAmplitude_` denormal guard (§4.9).

Only `detuneMultiplier_` and `targetAmplitude_` change per chunk; `epsilon_` does **not** (FR-034 — the
kernel multiplies `eps·detune` itself, `…_simd.cpp:103`, so partial phase is continuous through a detune
change and there is nothing to click).

### 4.3 Pan (FR-091, FR-092) — config rate

```cpp
panPosition_[i] = positionOverridden_[i] ? panPosition_[i]                  // FR-008 override survives
                                         : stereoSpread_ * positionScatter_[i];   // FR-021
const float p01 = std::clamp((panPosition_[i] + 1.0f) * 0.5f, 0.0f, 1.0f);  // FR-091 MANDATORY remap
equalPowerGains(p01, panLeft_[i], panRight_[i]);                            // crossfade_utils.h:50
```

The remap is not optional. `equalPowerGains` does not clamp (`crossfade_utils.h:41`); feeding a bipolar
position straight in yields `panRight = sin(−π/2) = −1` at `pos = −1` — a full-level polarity-inverted right
channel that still satisfies `L² + R² = 1`. SC-012 clauses 2–4 exist to catch exactly that.

`setStereoSpread` clears every `positionOverridden_` flag (FR-008: the override lasts "until the next spread
change, re-seed or `reset()`").

### 4.4 Per-partial envelope (FR-023) — linear AR, per chunk

Offsets, drawn once per seed:

```
attackOffsetSec_[i] = offsetSpread_ · kMaxEnvOffsetSec · oa_i,   oa_i ~ U[0,1]
decayOffsetSec_[i]  = offsetSpread_ · kMaxEnvOffsetSec · od_i,   od_i ~ U[0,1]
```

(`oa_i`, `od_i` are stored per seed; the `offsetSpread_` factor is re-applied whenever the spread changes, so
the *ordering* of partial onsets is a property of the seed and does not re-draw.)

Per chunk, per active partial, with `dt = chunk / fs`:

```
attackSec_i = attackSec_ + attackOffsetSec_[i]      // time-to-100 %, NOT a time constant
decaySec_i  = decaySec_  + decayOffsetSec_[i]
switch (envStage_[i]):
  Attack : envValue_[i] += dt / attackSec_i;  if (envValue_[i] >= 1) { envValue_[i] = 1; stage = Hold; }
  Hold   : envValue_[i] = 1.0f;
  Release: envValue_[i] -= dt / decaySec_i;   if (envValue_[i] <= 0) { envValue_[i] = 0; stage = Idle; }
  Idle   : envValue_[i] = 0.0f;
```

Constant slope in both directions. Consequences the SCs depend on:

- From 0, attack reaches 1.0 at exactly `attackSec_i` → SC-013 clause 3's "≥95 % of target within the
  documented attack time plus its own offset" is reachable. The FR-014 smoother lags a linear ramp by one
  time constant, so the *observed* `currentAmplitude` at `t = attackSec_i` is
  `1 − kAmpSmoothTimeSec/attackSec_i`. At `kMinAttackSec = 0.05 s` that is **0.960**, clearing the 95 % bar
  with margin. FR-023's "≥20× the smoother time constant" would give exactly 0.95 at 40 ms — hence the
  50 ms floor chosen here.
- Release falls at `1/decaySec_i` per second regardless of the starting value, so a partial released from
  value `v` reaches 0 in `v · decaySec_i ≤ decaySec_i` → SC-013 clause 4's "≤1 % of target within
  `decayTime + offset + 5τ`" holds, and nothing sustains after gate-off.
- Retrigger enters `Attack` **from the current `envValue_[i]`** (FR-023, Clarifications Q5) — no step, so no
  click and nothing for the FR-013 crossfade to hide.
- `noteOff()` during attack enters `Release` from the current value; `noteOn()` during release re-enters
  `Attack` from the current value. Neither snaps (Edge Cases).

SC-013 clause 2 sanity check: at `offsetSpread_ = 1` with 64 active partials, the 50 %-crossing spread is
`0.5 · kMaxEnvOffsetSec · (max oa − min oa) ≈ 0.5 · 2.0 · 0.97 ≈ 970 ms` — inside `[100 ms, kMaxEnvOffsetSec]`.

### 4.5 Drift lanes (FR-031, FR-032, FR-035, FR-072) — SoA Ornstein–Uhlenbeck

**Math is `BrownianDrift`'s, verbatim** (`brownian_drift.h:230-270`), transposed to SoA. The exact AR(1)
discretisation of `dX = (1/τ)(μ − X)dt + σ dW` over the fixed step `Δt = kDriftControlInterval / fs`:

```
τ    = kDriftTauMin + smoothness · (kDriftTauMax − kDriftTauMin)      // brownian_drift.h:231-234
a    = exp(−Δt / τ)                                                    // :235
g    = kDriftInternalStd · sqrt(max(1 − a², 0))                        // :237-239
```

Per control step, per lane `i` (μ = 0, as `BrownianDrift`'s default `mean_`):

```
z0 = rng[i].rng.nextFloat(); z1 = rng[i].rng.nextFloat(); z2 = rng[i].rng.nextFloat();  // SEQUENCED, :257-259
x  = a · walk[i] + g · (z0 + z1 + z2);                        // Irwin-Hall, zero-mean unit-variance
x  = clamp(x, −kDriftWalkLimit, +kDriftWalkLimit);            // :263
if (|x| < kDriftDenormalFloor) x = 0;                         // :264-266
walk[i] = x;
smoothTgt[i] = clamp(depth · x, −1.0f, +1.0f);                // outputTarget(), :249-251
```

(`rng[i].rng` is the real `Xorshift32` — §0.1 trap 3. `setTarget` needs no transcription: `smoothTgt` is
finite by the clamp above, and for finite input `OnePoleSmoother::setTarget` is a plain assignment,
`smoother.h:170-181`.)

**Advancing the 150 ms output one-pole — transcribe `advanceSamples`, do NOT write the exponential
identity.** This is the single highest-risk line in the plan, and an earlier draft got it wrong. The naive
closed form

```
smoothCur[i] = smoothTgt[i] + (smoothCur[i] − smoothTgt[i]) · smoothPowTable_[k]     // WRONG — incomplete
```

is **not** what `BrownianDrift` does. `BrownianDrift::processBlock` calls
`outputSmoother_.advanceSamples(advance)` (`brownian_drift.h:204`), and `OnePoleSmoother::advanceSamples`
(`smoother.h:243-254`) wraps that multiply in three further operations: an `isComplete()` early **return**
(leaving `current_` untouched, *not* snapped), a `detail::flushDenormal`, and a post-advance **hard snap**
`current_ = target_` whenever the gap falls under `kCompletionThreshold = 1e-4f` (`smoother.h:55`). That snap
is a nonlinear, path-dependent step an order of magnitude larger than T-DRIFT-EQUIV's 1e-5 tolerance, and
with a 150 ms pole chasing a target that reverses every few control steps the gap crosses 1e-4 constantly.
**Measured this session** — the naive form built verbatim against a real `BrownianDrift` at 48 kHz, seed
`0x1234ABCD`, depth 1, 60 s of 64-sample chunks — worst `|diff|` was `1.00e-4` (s = 0.25), `1.00e-4`
(s = 0.50), `1.64e-4` (s = 0.75), `1.00e-4` (s = 1.0), first breaching 1e-5 at t = 1.17 s for s = 0.75. So
the naive lane **fails T-DRIFT-EQUIV on a correct implementation of everything else**, T6 blocks, and the
predictable response would be to loosen the tolerance — which would void the only evidence that D1 preserves
behaviour. Adding exactly the three missing operations gives worst `|diff| = 0.000e+00` (bit-identical) at
every smoothness. Therefore `advanceSmootherAllLanes(bank, k)` is, per lane:

```cpp
const float diff0 = bank.smoothCur[i] - bank.smoothTgt[i];
if (std::abs(diff0) < kCompletionThreshold) continue;              // smoother.h:244 + :232-234 — SKIP, no snap
bank.smoothCur[i] = bank.smoothTgt[i] + diff0 * smoothPowTable_[k];    // :247-249
bank.smoothCur[i] = detail::flushDenormal(bank.smoothCur[i]);          // :250  (db_utils.h:167)
if (std::abs(bank.smoothCur[i] - bank.smoothTgt[i]) < kCompletionThreshold) {
    bank.smoothCur[i] = bank.smoothTgt[i];                             // :251-253
}
```

`kCompletionThreshold` is the shared `Krate::DSP` constant from `smoother.h:55` — not a redeclared copy
(§1.2) — and `detail::flushDenormal` is already in the include set (§1.1). Note the asymmetry that makes
the early-out a `continue` and not a snap: `advanceSamples` returns *without* assigning when `isComplete()`
is true on entry. Transcribing it as a pre-multiply snap is a different function and reintroduces divergence.

T-DRIFT-EQUIV stays at **1e-5** (measured achievable: 0.0) and additionally sweeps smoothness ∈ {0, 0.5, 1}
so a lane that skips the snap fails loudly rather than marginally (§7.4).

`smoothPowTable_[k] = coeff^k`, and **`coeff` MUST come from the shared free function**
`calculateOnePolCoefficient(kDriftOutputSmoothMs, float(sampleRate_))`
(`primitives/smoother.h:77-93`, `constexpr`, public, namespace scope — it is exactly what
`OnePoleSmoother::configure` calls at `:163`, and it uses `detail::constexprExp`, **not** `std::exp`). Deriving
the coefficient with `std::exp(-5000.0f/(T*sr))` instead would drift from `BrownianDrift`'s smoother in the
last bits and could put `HarmonicCloud_DriftLaneMatchesBrownianDrift` (§7.4) near its 1e-5 tolerance for no
reason. The table is built once in `prepare()` for `k = 0 … kDriftControlInterval`; `k ≤ 32` always, because
the advance is chunked at control boundaries.

`advanceDriftLanes(bank, numSamples)` is a **verbatim structural copy of `BrownianDrift::processBlock`**
(`brownian_drift.h:194-206`), with the counter shared across all 64 lanes of the bank (every lane is advanced
by the same sample counts, so one counter is sufficient and correct):

```cpp
int remaining = static_cast<int>(numSamples);
while (remaining > 0) {
    if (bank.samplesUntilControl <= 0) {
        bank.samplesUntilControl = kDriftControlInterval;
        advanceControlStepAllLanes(bank);            // the 64-lane loop above
    }
    const int advance = std::min(remaining, bank.samplesUntilControl);
    bank.samplesUntilControl -= advance;
    remaining -= advance;
    advanceSmootherAllLanes(bank, advance);          // smoothPowTable_[advance]
}
```

This is what makes SC-015 clause 2(a) true: the lane state after `N` advanced samples depends only on `N`,
not on how `N` was partitioned. A 64-sample chunk performs **2** internal OU steps, not one — FR-032
explicitly forbids collapsing them.

**Configuration split (FR-035 / FR-072), the property SC-016 measures:**

| | detune bank | mutation bank |
|---|---|---|
| smoothness | `driftSmoothness_` (cloud control) | `kMutationSmoothness = 0.5f` (fixed constant) |
| depth | `1.0f` — the cents bound is applied by the cloud (FR-033), never by `setDepth` | `1.0f`, pinned |
| seeds | `deriveSeed(seed, i)` | `deriveSeed(seed, i + kMaxPartials)` |
| scaled by | `driftCents_ · driftAmount_[i]` | `mutationAmount_ · kMaxMutationDepth` |

`setDriftDepthCents(0)` therefore leaves the mutation bank untouched — the SC-017 grid cell
`{0 drift depth} × {max Mutation}` is a **live** mutation cell (FR-072).

**Per-partial drift amount (FR-022):** `driftAmount_[i] = pow(n / kMaxPartials, kDriftIndexExponent) · u_i`
with 1-based `n = i + 1` and `u_i ~ U[0.5, 1.0]` drawn once per seed. `n / kMaxPartials` uses the **fixed**
capacity, not `activeCount_`, so changing Richness does not re-scale existing partials' drift. Because
`driftAmount_[i] ≤ 1`, `driftCents_` is a true upper bound over all partials (SC-015 clause 3(a)).

### 4.6 Seeding — fixed, documented draw order (FR-005, Edge Cases)

`reseed()` is called from `prepare()`, `reset()` and `setSeed()`, and does exactly this, in this order:

```
configRng_.seed(configuredSeed_);
phaseRng_.seed(deriveSeed(configuredSeed_, 0xF0F0u));
for i in 0..63:  positionScatter_[i] = configRng_.nextFloat();               // FR-021, already [-1,1]
for i in 0..63:  u_i                 = 0.5f + 0.5f * configRng_.nextUnipolar();  // FR-022, [0.5,1]
for i in 0..63:  oa_i                = configRng_.nextUnipolar();            // FR-023 attack offset
for i in 0..63:  od_i                = configRng_.nextUnipolar();            // FR-023 decay offset
for i in 0..63:  detuneLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i));         // Xorshift32::seed, random.h:72
for i in 0..63:  mutationLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i + 64));
redrawPhases();                                                              // FR-016, from phaseRng_
```

`redrawPhases()`: `φ = phaseRng_.nextUnipolar() * kTwoPi; sinState_[i] = sin(φ); cosState_[i] = cos(φ);`
(the initialisation shape at `harmonic_oscillator_bank.h:288-290`, with a seeded φ instead of an analysed one).
Phase redraws use a **separate stream** so a retrigger never perturbs the once-per-seed draws — `reset()`
therefore reproduces all of them exactly (Edge Cases).

Seed derivation must give **128 distinct non-zero streams**, and must not rely on `Xorshift32`'s 0-substitution
(`random.h:45`) to fix collisions — two lanes both hashing to 0 would collapse onto the same default stream:

```cpp
[[nodiscard]] static constexpr std::uint32_t deriveSeed(std::uint32_t base, std::size_t salt) noexcept {
    std::uint32_t h = base ^ (static_cast<std::uint32_t>(salt + 1u) * 0x9E3779B9u);
    h ^= h >> 16; h *= 0x7FEB352Du;      // lowbias32 finaliser
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h != 0u ? h : 0x2545F491u;    // never hand 0 to a xorshift lane
}
```

SC-009's distinctness clause asserts all 128 derived values are pairwise different for seed 0 and for a
handful of ordinary seeds.

### 4.7 Retrigger / quiescence (FR-016, Clarifications Q5)

```cpp
[[nodiscard]] bool isQuiescent() const noexcept {       // §1.5, §4.1 early-out, Phase-7 retirement signal
    return !gate_ && allBelow(currentAmplitude_, kQuiescenceAmplitude);
}
void noteOn() noexcept {
    if (isQuiescent()) redrawPhases();                  // quiescent path
    gate_ = true;
    for (i < kMaxPartials) envStage_[i] = Attack;   // re-open FROM envValue_[i], never from 0
}
void noteOff() noexcept {
    gate_ = false;
    for (i < kMaxPartials) if (envStage_[i] != Idle) envStage_[i] = Release;
}
```

`allBelow` scans `currentAmplitude_` over `[0, kernelCount_)`. A sounding retrigger keeps every MCF state
untouched, so it is click-free **by construction**, not by masking — that is the mechanism SC-006's retrigger
clause asserts directly via `getPartialSinState`/`getPartialCosState`.

### 4.8 Setter hygiene (FR-007) — one idiom, everywhere

```cpp
void setSpectralGravity(float g) noexcept {
    if (detail::isNaN(g) || detail::isInf(g)) return;      // db_utils.h:54,174 — bit tests, NOT std::isnan
    const float v = std::clamp(g, -1.0f, 1.0f);            // FR-007 half 1: clamp to a DOCUMENTED range
    if (v == gravity_) return;                             // no-op guard — see "Config-rate cost" below
    gravity_ = v;
    freqDirty_ = true;                                     // deferred to the next chunk boundary (§4.2 step 0)
}
```

The rejection test precedes **every** assignment, so a NaN/Inf argument leaves the getter and the rendered
output bit-identical (FR-007's single testable behaviour). The pattern is
`AdditiveOscillator::setSpectralTilt`'s (`additive_oscillator.h:318-325`). It works under `-ffast-math`
because it inspects the IEEE-754 bit pattern via `std::bit_cast`, never floating-point predicates.

**FR-007 has two halves and both are requirements.** Besides non-finite rejection, "every parameter setter
clamps its input to a documented range" — a finite out-of-range argument must come back from the getter as
the exact documented bound. The documented bounds are §1.2's constant pairs, and they are the table SC-017's
out-of-range pass iterates (§7.4): `setFundamentalHz → [20, 4000]`, `setRichness/setMutation/
setDriftSmoothness/setStereoSpread/setEnvelopeOffsetSpread → [0, 1]`, `setInharmonicity → [0, 0.1]`,
`setSpectralTiltDb → [−12, +12]`, `setSpectralGravity → [−1, +1]`, `setDriftDepthCents → [0, 50]`,
`setAttackTimeSec → [0.05, 30]`, `setDecayTimeSec → [0.05, 60]`, `setPartialPosition → [−1, +1]`.

**Config-rate cost: no-op guard + dirty flag, not an unconditional recompute.** Every macro setter ends with
`freqDirty_ = true` (fundamental, inharmonicity, gravity, richness) and/or `ampDirty_ = true` (richness,
tilt); `setMutation` sets neither. §4.2 step 0 consumes the flags, so **N setter calls inside one block cost
at most one `recalculateFrequencies()` plus one `recalculateAmplitudes()`**, and a setter called with the
value it already holds costs nothing at all. Without this, the Phase-7 operating point — richness, gravity,
tilt and inharmonicity all life-modulated plus `setFundamentalHz` under glide — pays 4 frequency recomputes
and 2 amplitude recomputes per block; costed in §6.4, that is 2–4 µs/block against 6.6 µs of headroom.
`setFundamentalHz` is the one exception to pure deferral: its FR-013 crossfade arming (§2) must happen at
call time, because the arming reads `lastOutL_/lastOutR_` — the pitch-ratio test and the crossfade arm stay
in the setter; only the epsilon recompute is deferred.

`setPartialPosition(index, position)` uses the same guard, is a no-op for `index >= kMaxPartials`, clamps
`position` to `[-1, +1]`, sets `positionOverridden_[index] = true`, and recomputes only that partial's pan
pair.

### 4.9 Non-finite and denormal hygiene (Edge Cases)

- `stateFinite()` is the reference bit test (`harmonic_oscillator_bank.h:622-633`) over
  `sinState_`/`cosState_` for `i < kernelCount_`.
- The MCF recurrence cannot diverge: `|epsEff| < 2` is enforced *inside the kernel* (`…_simd.cpp:103`, `:124`)
  and the determinant of the Gordon-Smith update is exactly 1, so amplitude is bounded in finite precision
  (`…bank.h:698-701`).
- OU lanes are bounded by construction: `a ∈ (0,1)`, `g` finite, hard clamp at `±kDriftWalkLimit`, denormal
  flush at `kDriftDenormalFloor`, output clamped to `[−1,+1]`.
- Envelope values are clamped into `[0,1]` at every stage transition.
- **The denormal guard goes on `currentAmplitude_`, which is the state that decays — not on
  `targetAmplitude_`.** An earlier draft flushed `targetAmplitude_` and claimed that prevented a denormal in
  the kernel's smoothing recurrence. It does not: the recurrence state *is* `currentAmplitude_`, and the
  kernel never flushes it (`vAmp = hn::MulAdd(vCoeff, vDiff, vAmp)`,
  `harmonic_oscillator_bank_simd.cpp:93`; scalar tail
  `currentAmplitude[i] += ampSmoothCoeff * (target - currentAmplitude[i])`, `:120`). Forcing
  `targetAmplitude_` to exactly 0 is in fact the condition that walks `currentAmplitude_` *through* the
  denormal range: at `kAmpSmoothTimeSec = 0.002f` the per-sample retention is ≈0.9896 at 48 kHz, so ≈8.4 k
  samples (≈175 ms) after a target reaches 0 the state is denormal and stays denormal for thousands more
  samples. The §3 tail high-water only covers `i ≥ activeCount_`; masked/soloed-out partials (§3, target 0)
  and every partial after `noteOff()` completes its release (§4.4 Idle ⇒ env 0 ⇒ target 0) sit **inside**
  `[0, kernelCount_)` and would otherwise be processed with a denormal amplitude indefinitely. Therefore,
  once per chunk, for `i < kernelCount_`:
  ```cpp
  if (targetAmplitude_[i] == 0.0f && currentAmplitude_[i] < kTailSilenceThreshold) {
      currentAmplitude_[i] = 0.0f;                 // the reference's 1e-8 idiom, …bank.h:756, generalised
  }                                                // beyond the tail to masked + released partials
  ```
  Keeping a `detail::flushDenormal` on `targetAmplitude_` as well is harmless, but it is **not** the
  mitigation and must not be described as one.
- **This guard cannot be verified by the default test environment**, which is why it gets a dedicated test
  (§7.4 `HarmonicCloud_DecaysToExactZero`): `dsp/tests/dsp_test_main.cpp:13` calls `enableFTZDAZ()` before
  any case runs, so SC-005 and the SC-007 baseline are both measured with denormals flushed **by the
  process**, not by the component. The dedicated test asserts every `getPartialCurrentAmplitude(i)` reaches
  exactly `0.0f` after a note-off-to-silence render, which is an assertion about the component's own
  arithmetic and holds regardless of the MXCSR state.
- Output is hard-clamped to `±kOutputClamp` (FR-006).

---

## 5. prepare / reset contract (FR-003)

```cpp
void prepare(double sampleRate) noexcept {       // NOT RT-safe (transcendentals, table build)
    sampleRate_      = sampleRate > 1.0 ? sampleRate : 1.0;
    nyquist_         = static_cast<float>(sampleRate_ * 0.5);
    invSampleRate_   = 1.0f / static_cast<float>(sampleRate_);
    fadeStart_       = kAntiAliasFadeStart * nyquist_;
    invFadeRange_    = 1.0f / (nyquist_ - fadeStart_);
    ampSmoothCoeff_  = 1.0f - std::exp(-1.0f / (kAmpSmoothTimeSec * float(sampleRate_)));  // …bank.h:136
    crossfadeLengthSamples_ = std::max(1, int(kCrossfadeTimeSec * float(sampleRate_)));     // …bank.h:185
    crossfadeThresholdRatio_ = semitonesToRatio(1.0f);                                      // …bank.h:190
    normGain_.configure(kNormGainSmoothMs, float(sampleRate_));
    buildSmoothPowTable();                       // coeff^k, k = 0..32   (§4.5)
    updateDriftCoefficients(detuneLanes_,   driftSmoothness_);
    updateDriftCoefficients(mutationLanes_, kMutationSmoothness);
    reset();
    prepared_ = true;
}

void reset() noexcept {                          // RT-safe
    sinState_/cosState_  ← redrawn by reseed()   // NOT zeroed (FR-016/FR-011)
    currentAmplitude_.fill(0); targetAmplitude_.fill(0); detuneMultiplier_.fill(1.0f);
    envValue_.fill(0); envStage_.fill(Idle); gate_ = false;
    detuneLanes_/mutationLanes_: walk.fill(0), smoothCur.fill(0), smoothTgt.fill(0), samplesUntilControl = 0;
    crossfadeRemaining_ = 0; lastOutL_ = lastOutR_ = 0; driftReadCount_ = 0;
    positionOverridden_.fill(false); masked_.fill(false);
    tailHighWater_ = activeCount_;
    reseed();                                    // §4.6 — fixed draw order, RNG rewound
    recalculateFrequencies(); recalculateAmplitudes(); recalculatePan(); recalculateAntiAliasing();
    freqDirty_ = false; ampDirty_ = false;       // everything just recomputed
    normGain_.snapTo(currentNormGainTarget());   // ONLY place snapTo appears — §3
}
```

`recalculateAmplitudes()` ends with `normGain_.setTarget(currentNormGainTarget())` (§3); `reset()` then
overrides that with `snapTo` so a reset render starts at the correct level instead of sliding into it over
20 ms. Everywhere else the gain tracks via `advanceSamples` (§4.2 step 4).

After `prepare()`, processing is well-defined with **no prior parameter call** (FR-003): the shadow parameters
default to `f0 = 220 Hz`, `richness = 0.5`, everything else at its neutral value, and `reset()` has already
run every recalculation. Calling `prepare()` again with a different rate re-derives every sample-rate-dependent
coefficient and resets (Edge Cases: a sample-rate change is not an audio-thread operation). Nothing in the
component is expressed in samples — every behaviour is in Hz and seconds (SC-010).

---

## 6. CPU budget (SC-007) — measured, and the one escalation

SC-007's binding arithmetic: the checked-in `kBaselineNsPerBlock × 1.5 ≤ kReferenceNsPerBlock = 53,333 ns`,
i.e. **`baseline ≤ 35,533 ns` per 512-sample block**, and *"if the first measurement cannot meet the
constraint, the phase is over budget — the response is to reduce cost … never to raise the baseline."*

### 6.1 What was measured

Windows 11, MSVC 19.4x `/O2 /MD`, `build/windows-x64-release` libs (`KrateDSP.lib` + `hwy.lib`), best-of-7 ×
200–500 iterations, 2026-07-25. Standalone probes, not the final component.

| Probe | ns / 512-sample block |
|---|---|
| MCF kernel, 512 samples × 64 partials | **11,099** |
| 128 `BrownianDrift` instances, 8 × `processBlock(64)` + `getCurrentValue()` | **44,402** |
| …same, one `processBlock(512)` each (identical 2048 OU steps) | 43,114 |
| …the `std::pow` inside `OnePoleSmoother::advanceSamples` alone (2048 calls) | **20,335** |
| 64 `BrownianDrift` instances (one bank only), chunked | 19,922 |
| 1024 × `semitonesToRatio` (`std::pow`) | 6,904 |
| 1024 × `std::exp2` (identical math) | 3,006 |
| **SoA OU lanes, 128 lanes, same 2048 steps, `coeff^32` tabulated** | **9,426** |
| SoA OU lanes, 64 lanes | 6,453 |

### 6.2 What that means

**The literal FR-031/FR-032 reading — 128 `BrownianDrift` objects, `processBlock(64)` per chunk — does not
fit.** Its total is `11,099 + 44,402 + 6,904 + ≈1,500 (per-chunk control work) ≈ 63,900 ns/block`, i.e.
**0.60 % of one core**: over the roadmap's 0.5 % headline *and* 1.80× over SC-007's baseline gate.

Nearly half the drift cost is a single avoidable call: `OnePoleSmoother::advanceSamples` evaluates
`std::pow(coefficient_, N)` on every call (`smoother.h:248`), and `N` is 32 and `coefficient_` is identical
for all 128 instances and all 2048 steps. Even after removing it, the AoS path costs ≈24,100 ns — the
per-object floor — for a total of ≈43,600 ns/block (0.41 % of a core: under the roadmap's absolute 0.5 %, but
still **1.23× over SC-007's baseline gate**, so no honest baseline can be checked in).

**The SoA lane bank (§4.5) fits with margin.** Same OU recurrence, same coefficients, same 150 ms output
smoother, same 32-sample control interval, same three sequenced Irwin-Hall draws, same clamps — only the
storage layout and the tabulated `coeff^k` differ:

| Cost centre | ns/block |
|---|---|
| MCF kernel, 64 partials × 512 samples | 11,099 |
| SoA OU lanes, 2 banks × 64 lanes, 2048 control steps | 9,426 |
| 1024 × `semitonesToRatio` (FR-033) | 6,904 |
| per-chunk control pass (aaGain via `sqrt`, mutation, envelope, target writes) | ≈1,500 |
| **projected total** | **≈28,900** (0.271 % of one core) |

`28,900 × 1.5 = 43,350 ns < 53,333 ns` ✅ — the SC-007 constraint holds with ≈23 % headroom. The remaining
lever, if the measured baseline comes in higher than projected, is `std::exp2(cents/1200)` in place of
`semitonesToRatio` (mathematically the same function — `pitch_utils.h:25` is `std::pow(2.0f, s/12.0f)` — and
measured 2.3× faster, worth a further ≈3,900 ns/block).

### 6.3 Configuration-rate cost is inside the budget, and must be measured there

The table above covers only the render path. It excludes every `recalculate*()` call — yet **continuous
modulation of exactly the parameters that trigger them is the component's documented use**: §2 recomputes
the frequency pipeline on any fundamental / inharmonicity / gravity / richness change, §3 recomputes the
amplitude chain on any richness / tilt change, and Phase 7 wires all five macros to life modulators
(roadmap lines 291–295) while `setFundamentalHz` moves under glide. SC-006's own procedure already steps
macros "once per 512-block", so the plan assumes this call pattern elsewhere. Measuring the budget in a
configuration the component will never be used in understates it, and roadmap line 484 makes the budget a
functional requirement.

Costed from this plan's own measurement (§6.1: 1024 `std::pow` = 6,904 ns ⇒ **6.7 ns per `pow`**):

| Recompute | Composition | ns |
|---|---|---|
| `recalculateFrequencies()` | 64 × `pow(n, 1+0.1g)` + 64 × `sqrt` + 64 × `sin` | ≈1,000 |
| `recalculateAmplitudes()` | 64 × `pow(n,−p)` + 64 × `pow(10, …)` + 64 × `log2` + 1 × `sqrt` | ≈1,050 |

**Unconditional recompute (the earlier draft's `setSpectralGravity` idiom):** one automated block fires
`setFundamentalHz` + `setRichness` + `setInharmonicity` + `setSpectralGravity` (4 frequency recomputes) and
`setRichness` + `setSpectralTiltDb` (2 amplitude recomputes) ⇒ **≈6,100 ns/block**, i.e. 92 % of the 6,600 ns
that separates the §6.2 projection from the 35,533 ns gate. That is not affordable.

**With §4.8's no-op guard + dirty flag:** at most one of each per chunk boundary, and in practice one of each
per block ⇒ **≈2,050 ns/block**, ≈31 % of headroom. Projected automated total **≈30,950 ns**;
`30,950 × 1.5 = 46,425 ns < 53,333 ns` ✅. This is why §4.8's guard is a requirement of the design and not a
micro-optimisation.

**Both baselines get measured and checked in** (§7.4): `HarmonicCloud_CpuBudget` (static configuration, the
§6.2 number) and `HarmonicCloud_CpuBudgetUnderAutomation` (all five macros plus the fundamental stepped once
per 512-block — the Phase-7 operating point). **The `static_assert(baseline × 1.5 ≤ kReferenceNsPerBlock)`
is applied to the automated baseline**, because that is the configuration the component actually runs in;
the static baseline is recorded alongside it for regression tracking.

### 6.4 The escalation

§4.5 replaces *"each partial owns a `BrownianDrift` instance"* (FR-031) and *"each partial's
`BrownianDrift::processBlock(chunkLength)` is called once"* (FR-032) with *"each partial owns an OU drift
lane whose recurrence, coefficients, smoothing and clamps are `BrownianDrift`'s"*. **Every observable
property survives**: independence (SC-015.1), partition-invariance and read cadence (SC-015.2), bound,
liveness, index scaling and scatter (SC-015.3), shared configuration (SC-015.4), mutation bounds, rate and
independence from drift (SC-016), determinism (SC-009). What changes is the class used on the hot path.

This is the same argument shape the spec already accepts for the oscillator itself: FR-011 re-implements
`HarmonicOscillatorBank`'s math over Seraphis-owned SoA arrays rather than instantiating the class, "because
that class's entire input contract is `HarmonicFrame`". Here the reason is a measured CPU budget that
roadmap line 484 makes a functional requirement.

**FR-031, FR-032, FR-035 and FR-072 name the class and its methods explicitly, so this design cannot ship
against the spec as written.** Four FRs would have to be recorded as *unmet* in a Completion-Honesty
compliance table — not because the design is wrong (the measurement says the literal design is 1.80× over
SC-007's own baseline gate) but because the spec is unamended. Flagging that and proceeding, as an earlier
draft did, is the failure mode Completion Honesty exists to prevent.

**Therefore: amending the spec is step T0 of §10 and blocks T6.** The verbatim replacement text is drafted in
**§12**, restating the four FRs in terms of *behaviour* (recurrence, coefficients, three-draw sequenced
Irwin-Hall increment, clamps, 150 ms output smoother including its completion snap, read cadence,
independence) rather than class identity, keeping FR-032's read-cadence clause and FR-072's independence
clauses verbatim, and carrying §6.1's measured numbers into the spec as the rationale. T-DRIFT-EQUIV (§7.4)
is the amendment's own verification clause: a test that drives a real `BrownianDrift` and a cloud lane from
the same seed, smoothness and sample rate through an identical chunk schedule and asserts their value
sequences agree to 1e-5 at every chunk over 60 s, at smoothness {0, 0.5, 1}, on both banks — so "the
recurrence is `BrownianDrift`'s" is a measured claim, not a comment. Measured achievable divergence with the
§4.5 transcription: **0.000e+00**.

*Alternative, if the amendment is refused:* keep 128 `BrownianDrift` objects and memoise `coeff^N` inside
`OnePoleSmoother::advanceSamples` (cache `{lastN, lastCoeffN}`, invalidated in `configure()` — an 8-byte,
behaviour-preserving Layer-1 change that also speeds up Phase 1's own SC-007). Projected ≈43,600 ns/block =
0.41 % of a core: inside the roadmap's absolute budget, outside SC-007's baseline arithmetic. Taking this
branch requires SC-007's `kRegressionFactor` or `kReferenceNsPerBlock` to be renegotiated in the spec — which
SC-007 explicitly forbids doing at implementation time. Either branch is a spec edit; there is no branch that
proceeds without one.

---

## 7. Test plan

All new test TUs live in `dsp/tests/unit/systems/` and build into **`dsp_systems_tests`**.

### 7.1 Files (3 new)

| File | Contents |
|---|---|
| `dsp/tests/unit/systems/harmonic_cloud_test.cpp` | SC-001, SC-002, SC-004 … SC-006, SC-008, SC-009, SC-012 … SC-018 (the bulk), plus `HarmonicCloud_DriftLaneMatchesBrownianDrift`, `HarmonicCloud_FrequencyEstimatorResolution`, `HarmonicCloud_GuardPaths`, `HarmonicCloud_MaskAndSoloAreClickFree`, `HarmonicCloud_DecaysToExactZero` |
| `dsp/tests/unit/systems/harmonic_cloud_spectral_test.cpp` | the FFT-heavy criteria — SC-003, SC-010, SC-011, SC-014 — kept separate because they build 64 K-point transforms and dominate wall time. **SC-010 lives here now**: its amplitude half is measured from the rendered spectrum, not from accessors (§7.4) |
| `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp` | SC-007's two cases — `HarmonicCloud_CpuBudget` (static) and `HarmonicCloud_CpuBudgetUnderAutomation` (gating, §6.3) — both `TEST_CASE(..., "[.perf]")` |

**Allocation-detector wiring (SC-008), mandatory:** `allocation_operator_overrides.h` is already included by
`dsp/tests/unit/systems/selectable_oscillator_test.cpp:388` for this binary. **No new TU may include it**
(duplicate-symbol link error). The SC-008 test includes only `allocation_detector.h` and relies on those
existing global replacements.

**Use the `AllocationDetector::instance()` idiom, not `AllocationScope`.** `AllocationScope` assigns its
count **in its destructor** — `~AllocationScope()` does `count_ = AllocationDetector::instance().
stopTracking();` and `getAllocationCount()` returns `count_` (`tests/test_helpers/allocation_detector.h:75-95`).
So `{ AllocationScope s; …; } REQUIRE(s.getAllocationCount() >= 1);` does not compile (`s` is out of scope),
and reading it *inside* the scope returns `0` unconditionally — which would make the liveness case pass
vacuously in exactly the mode it was added to prevent, and would also make the subsequent
`REQUIRE(count == 0)` pass regardless of behaviour. Every existing in-repo use avoids the class:
`selectable_oscillator_test.cpp:418,422` and `:749,753` bracket the code under test with
`TestHelpers::AllocationDetector::instance().startTracking()` … `stopTracking()` and assert on the returned
count. Both the SC-008 liveness case and the render-loop case follow that shape (§7.4), liveness first.

**No `-fno-fast-math` source property** is added for these files, matching the Phase-1 precedent
(`brownian_drift_test.cpp` is absent from the list at `dsp/tests/CMakeLists.txt:385-647`). Non-finite test
inputs are built from bit patterns through a `volatile` sink, never `std::numeric_limits` (which folds under
`-ffast-math`, `reference_fastmath_nan_in_tests`), and the component's own guards are the bit-based
`detail::isNaN`/`isInf`, which are immune.

### 7.2 Shared test helper: the frequency estimator

SC-001 demands **< 0.1 cent** with a documented estimator resolution **≥10× finer** (0.01 cent). An
FFT-peak-plus-quadratic estimate cannot reach that: even at 65536 points / 48 kHz the bin width is 0.732 Hz,
while 0.01 cent at `f0 = 55 Hz` is 3.2e-4 Hz. SC-001 explicitly permits the alternative ("or long-window
zero-crossing period estimate"). This plan specifies a **two-stage heterodyne phase-slope estimator**, defined
once in an anonymous namespace and reused by SC-001, SC-002, SC-004, SC-010 and SC-015.3:

```
estimateFrequency(const float* x, size_t N, double fs, double fRef) -> double
  stage 1 (bracket):  M = 4096
  stage 2 (refine):   M = N/2, fRef ← stage-1 result
  for each stage, with a Hann window w[0..M-1] and DOUBLE accumulators:
      S1 = Σ_{n<M}      w[n]     · x[n]     · exp(-i·2π·fRef·n/fs)
      S2 = Σ_{n<M}      w[n]     · x[M+n]   · exp(-i·2π·fRef·(M+n)/fs)
      Δφ = wrapToPi(arg(S2·conj(S1)))
      f  = fRef + Δφ·fs / (2π·M)
```

*Why it works and what its resolution is.* The two windows are symmetric and separated by exactly `M`
samples, so their window transforms contribute the same real factor and cancel in `arg(S2·conj(S1))`;
`Δφ = 2π(f − fRef)M/fs` exactly. Unambiguous while `|f − fRef| < fs/(2M)` — stage 1 gives ±5.86 Hz at
`M = 4096`, which brackets any plausible bug; stage 2 refines within that bracket. With a solo partial (no
other content), the only error sources are double-precision accumulation and the negative-frequency image,
which for `f ≥ 55 Hz` and `M ≥ 2·10⁵` sits >150 dB down through the Hann sidelobe rolloff. Measured resolution
over a synthetic reference sinusoid (asserted by the test itself, `T-ESTIMATOR-RESOLUTION`) is
**< 0.001 cent** — 100× finer than SC-001's threshold, satisfying its "document the estimator resolution"
clause. The test additionally **asserts the SC-001 matrix constraint** `f0·n < 0.8·Nyquist` per measured
partial so the matrix cannot silently drift into the FR-015 fade band.

**FFT availability note for SC-003/SC-011.** `FFT::prepare` validates only power-of-two (`fft.h:151`);
`kMaxFFTSize = 8192` (`fft.h:47`) is documentary and not enforced, and pffft handles 65536 (`2^16`, a multiple
of 32). SC-003's pinned 65536-point Blackman-Harris analysis is therefore buildable — but the test **must
assert `fft.isPrepared()` after `prepare(65536)`**, so that a future tightening of that bound fails loudly
instead of silently analysing a zero-size spectrum.

### 7.3 Crest-factor headroom at SC-003's and SC-018's configurations — measured, not assumed

SC-003's precondition (`REQUIRE(peak < 0.9f · kOutputClamp)`, i.e. peak < 1.8) and SC-018's identical bound
both rest on a crest margin that nothing else in this plan pins. FR-017 fixes the RMS at
`kTargetOscRms = 0.5` (§1.2, §3), so the bound demands crest ≤ 1.8/0.5 = 3.6 (11.1 dB) — and SC-018's spec
text explicitly says that 11.1 dB is what the expected-RMS basis *leaves*, not what it *guarantees*. At
SC-003's own pinned configuration the tilt law concentrates the energy at the top: `p(5/6) = 0.9167` and
`+12 dB/oct` ⇒ `a_n ∝ n^{−0.9167}·n^{1.9932} = n^{1.0765}`, so only ~8 components carry the signal; the
partials are exactly harmonic (`B = 0`, `g = 0`), so the render is periodic at 110 Hz and, with FR-016's
i.i.d. random phases, the peak over one period is a random variable in the seed.

**Measured this session** (Node model of §3's amplitude chain + §2's anti-alias + §4.3's pan, 48 kHz,
64 seeds, 0.2 s per render; worst-case peak over all seeds, per rendered channel):

| Configuration | spread | worst channel peak | margin to 1.8 |
|---|---|---|---|
| SC-003, tilt −12 | 0 / 1 | 0.590 / 0.789 | 9.69 / 7.17 dB |
| SC-003, tilt 0 | 0 / 1 | 1.114 / 1.181 | 4.17 / 3.66 dB |
| SC-003, tilt +6 | 0 / 1 | 1.394 / 1.511 | 2.22 / **1.52 dB** |
| SC-003, tilt +12 | 0 / 1 | 1.436 / 1.479 | **1.96** / 1.70 dB |
| SC-018, 64 partials, tilt +12 | 0 / 1 | 1.249 / 1.354 | 3.18 / 2.47 dB |

Two consequences, both binding on the tests:

1. **SC-003's worst margin is 1.5–2.0 dB, which is inside seed variance.** The same model at 24 seeds
   already produced a *mono* peak of 2.031 at +12 dB/oct — above `kOutputClamp` itself. SC-003 therefore
   **pins its seed and pins `setStereoSpread(0.0f)`** (the spread costs a further 0.3–0.7 dB by pushing
   partials off-centre onto one channel), so the precondition is deterministic rather than a coin flip on a
   correct implementation. This is not a relaxed threshold — the 0.9·clamp bound is unchanged; only the
   configuration is made reproducible, exactly as SC-003 already pins `f0`, richness, FFT size and window.
2. **SC-018 keeps its ≥8-seed sweep** (the seed variation *is* the criterion) but pins an explicit seed
   **array** rather than drawing seeds arbitrarily, and records the measured worst channel peak over that
   array beside the assertion. Its 2.5–3.2 dB margin is comfortable enough that the hard `0.9 · kOutputClamp`
   bound stays as specified.

The numbers above come from an idealised steady-state model (no envelope, no drift, no FR-014 smoother
ramp-in — all of which *reduce* the onset peak). **T3 re-measures them on the real component** and replaces
this table's figures with the measured ones in a comment beside each threshold, the same treatment §6 gives
the CPU budget. If a measured margin comes in below ~1.5 dB, the response is to pin the seed harder or widen
the render window — never to move the 0.9 factor, which is spec text.

### 7.4 Per-criterion assertion strategy

| ID | File | `TEST_CASE` | Assertion strategy |
|---|---|---|---|
| SC-001 | `harmonic_cloud_test.cpp` | `HarmonicCloud_PartialFrequencyAccuracyWithin0p1Cent` | Drift/mutation/gravity/inharmonicity = 0. For each `(f0, n)` in {55: 1,8,32,64}, {440: 1,8,32}, {1000: 1,8,16}: `REQUIRE(f0*n < 0.8f*nyquist)`; `soloPartial(n-1)`; render 10 s @48 kHz into a mono mixdown (`0.5*(L+R)`); `cents = 100.0f*ratioToSemitones(fMeas/(f0*n))` (`pitch_utils.h:31`); `REQUIRE(std::abs(cents) < 0.1)`. Skip the first 200 ms so the FR-014 smoother has settled. |
| SC-002 | `harmonic_cloud_test.cpp` | `HarmonicCloud_InharmonicityFollowsPianoLaw` | `B ∈ {0.01, 0.05, 0.1}`; per partial (subject to the `0.8·Nyquist` constraint) assert `\|100·ratioToSemitones(fMeas/(f0·n·sqrt(1+B n²)))\| ≤ 1.0` cent **and** ≤ the estimator tolerance from `T-ESTIMATOR-RESOLUTION`, whichever is looser. |
| SC-003 | `harmonic_cloud_spectral_test.cpp` | `HarmonicCloud_TiltSlopeMatchesSetting` | `f0 = 110`, `setRichness(5.0f/6.0f)` → `REQUIRE(getActivePartialCount() == 32)`; 48 kHz, ≥4 s render, FFT 65536 + `generateBlackmanHarris` (`window_functions.h:179`) after `REQUIRE(fft.isPrepared())`. **Seed and spread are PINNED (`setSeed(kSc003Seed)`, `setStereoSpread(0.0f)`) — §7.3's measured margin at +6/+12 dB/oct is only 1.5–2.0 dB, so an unpinned seed can red the precondition on a correct implementation.** **Precondition first:** `REQUIRE(peak < 0.9f*kOutputClamp)` measured on the **rendered channel buffers** (the same samples the FFT consumes), with the measured margin recorded in a comment beside it. Take the tilt-0 render as reference; for each tilt ∈ {−12,−6,0,+6,+12}: least-squares fit of per-partial magnitude dB vs `log2(n)`; assert `\|slope(t) − slope(0) − t\| ≤ 0.5` dB/oct, `\|slope(0) − (−5.52)\| ≤ 0.5` dB/oct, and per-partial `\|dB(t,n) − dB(0,n) − t·log2(n) − c\| ≤ 0.5` dB where `c` is the single fitted scalar offset. |
| SC-004 | `harmonic_cloud_test.cpp` | `HarmonicCloud_GravityMapsMonotonically` | (1) `g=0`: `\|ratio − n\|` within SC-001 tolerance. (2) `g ∈ {−1,−.5,0,+.5,+1}`, `B=0`: mean `\|ratio−n\|` over `n ≥ 2` strictly increasing in `\|g\|`, each step ≥ 5× estimator tolerance. (3) sign of `(ratio−n)` at `+g` opposite to `−g`, **for `n ≥ 2` only** and only where the deviation exceeds tolerance. (4) `B = 0.05`, `\|g\| = 1`: each measured `f` matches `f0·n^(1+0.1g)·sqrt(1+B n²)` within the SC-002 tolerance — this is what pins FR-083's order. |
| SC-005 | `harmonic_cloud_test.cpp` | `HarmonicCloud_NoZipperUnderMutationAndDrift` | `ClickDetectorConfig{ .sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5 }` (`artifact_detection.h:38-45`) — **designated initialisers, no narrowing** (Clang rejects narrowing in brace init). `ClickDetector` (`:99`), `prepare()`, `detect()` (`:130`). Two 30 s renders: modulated (mutation 1, drift max) and control (drift depth 0, mutation 0). Assert `detections(mod) <= detections(ctl)` and `maxPerSampleDelta(mod) <= 1.5f * maxPerSampleDelta(ctl)`. **Positive control, first and mandatory:** inject a one-sample step of 10 % of peak into a copy of the control render and `REQUIRE(detections >= 1)` at that index. **SUPERSEDED 2026-07-26 — see spec.md SC-005/SC-006 "AMENDED" blocks.** The count/slew thresholds in this row were withdrawn by measurement (frozen control 0 detections vs 267; frozen-slew ratio 1.785 vs the 1.5 bound; 10 %-of-peak injection 0 detections; SC-006 endpoint controls 0 vs 43). The shipped criteria are on-grid ones and the shipped tests carry standing "withdrawn-clause guard" assertions. The SC-006 **retrigger** clause is unaffected and still asserts the count comparison. |
| SC-006 | `harmonic_cloud_test.cpp` | `HarmonicCloud_MacroSweepsAreClickFree`, `HarmonicCloud_RetriggerIsClickFree` | Same detector/config/positive-control as SC-005. Five 5 s min→max sweeps (one per macro, stepped once per 512-block) plus a ≥1-octave fundamental step; each asserts `detections(swept) ≤ detections(control)`. Retrigger case: capture all 64 `getPartialSinState`/`getPartialCosState` immediately before a **sounding** `noteOn()` and immediately after → `REQUIRE(after == before)` bitwise; assert `detections(retrig) ≤ detections(ctl)`. Quiescent case: `noteOff()`, render until every `getPartialCurrentAmplitude` < `kQuiescenceAmplitude`, `noteOn()` → assert the phase state **did** change (otherwise a never-redraw implementation passes case 1 vacuously and breaks SC-018). **SUPERSEDED 2026-07-26 — see spec.md SC-005/SC-006 "AMENDED" blocks.** The count/slew thresholds in this row were withdrawn by measurement (frozen control 0 detections vs 267; frozen-slew ratio 1.785 vs the 1.5 bound; 10 %-of-peak injection 0 detections; SC-006 endpoint controls 0 vs 43). The shipped criteria are on-grid ones and the shipped tests carry standing "withdrawn-clause guard" assertions. The SC-006 **retrigger** clause is unaffected and still asserts the count comparison. |
| SC-007 (static) | `harmonic_cloud_perf_test.cpp` | `HarmonicCloud_CpuBudget` `[.perf]` | ns per 512-sample block, best-of-N, at `kMaxPartials = 64`, mutation 1.0, drift depth max, both banks live, 8 chunks/block, **no setter calls**. Recorded as `kStaticBaselineNsPerBlock` (`constexpr double`, dev machine + date in a comment beside it) for regression tracking; **projected ≈29,000 ns (§6.2); MEASURED 2026-07-26 on the dev machine (i9-13900HX, MSVC Release, best-of-25 × 500 blocks) at 20,641–23,154 ns, baseline checked in at 24,000**. The `static_assert` lives on the *automated* case below — see §6.3. |
| SC-007 (automated) | `harmonic_cloud_perf_test.cpp` | `HarmonicCloud_CpuBudgetUnderAutomation` `[.perf]` | **The gating case.** Identical configuration plus the Phase-7 call pattern: all five macros **and** `setFundamentalHz` stepped once per 512-block (the cadence SC-006 already uses), so §4.8's no-op guard and dirty-flag deferral are exercised. `static_assert(kAutomatedBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock)` with `kRegressionFactor = 1.5`, `kReferenceNsPerBlock = (512.0/48000.0)*1e9*0.005` — the exact Phase-1 shape (`life_modulators_perf_test.cpp:54-73`). **Projected ≈30,950 ns, hard ceiling 35,533 ns (§6.3). MEASURED 2026-07-26: the first honest measurement was 37,002 ns — OVER the ceiling — and was brought to 21,917–25,262 ns (baseline checked in at 26,000, i.e. 0.244 % of budget, gate ×1.5 = 39,000) by spending §6.2's `exp2` lever plus a shared `log2(n)` table for the three config-rate power laws and a memo on the drift smoother's `coeff^N`; see `harmonic_cloud_perf_test.cpp`'s BASELINE PROVENANCE block. The projections above were never measured and must not be re-used as baselines.** The absolute 53,333 ns figure is `WARN`-reported only — every CI leg excludes `[.perf]` (`ci.yml:328`, `:574`, `:951`; `valgrind-nightly.yml:202`). Third measurement in the same case, no assertion beyond a `WARN`: the **quiescent** cost (post-`noteOff()`, `isQuiescent()` true) must be materially below the sounding cost, proving §4.1's early-out is wired. |
| SC-008 | `harmonic_cloud_test.cpp` | `HarmonicCloud_NoAllocInProcess` | **Liveness first, using the in-repo idiom (`selectable_oscillator_test.cpp:418-422`), NOT `AllocationScope` (§7.1):** `TestHelpers::AllocationDetector::instance().startTracking(); auto* p = new int[16]; delete[] p; const auto n = TestHelpers::AllocationDetector::instance().stopTracking(); REQUIRE(n >= 1);`. Then `prepare()`, then `startTracking()` around 200 blocks of 512 with macro automation and drift live, `stopTracking()` → `REQUIRE(count == 0)`. |
| SC-009 | `harmonic_cloud_test.cpp` | `HarmonicCloud_SeededRenderIsReproducible` | Drift depth, mutation and stereo spread each ≥50 % of range (pinned — otherwise the negative control is vacuous). Same seed, same call sequence, same block schedule → `compareFingerprints(...).withinTolerance()` (`render_fingerprint.h:101,95`). Different seed → `REQUIRE(cmp.worstMetricRelativeError > 10.0 * kMetricTolerance)`. Plus a `reset()`-then-re-render case asserting the post-`reset` render matches the original (Edge Cases). Plus `T-SEED-DISTINCT`: all 128 `deriveSeed` values pairwise distinct for seeds {0, 1, 0xFFFFFFFF, 12345}. |
| SC-010 | `harmonic_cloud_spectral_test.cpp` | `HarmonicCloud_SampleRateInvariant` | `f0 = 110`, 64 partials (top partial 7.04 kHz), drift 0 so `detune ≡ 1` (§2's D5 note). At 44.1/48/96 kHz: per-partial frequency within the SC-001 tolerance; amplitudes compared **only** for partials whose synthesized frequency is `< 0.8 × 22050 = 17.64 kHz` — and the test **asserts** that every active partial satisfies it rather than assuming (a correct FR-015 implementation fails an unscoped comparison). **Amplitude is measured from the rendered signal — FFT magnitude at each partial bin, as SC-010 specifies ("the per-partial amplitude *spectrum*") — NOT via `getPartialCurrentAmplitude / getPartialAntiAliasGain`.** That accessor ratio is vacuous here: the kernel drives `currentAmplitude → targetAmplitude·antiAliasGain` (`…_simd.cpp:91-93`), whose steady state is independent of `ampSmoothCoeff_`, and dividing by `antiAliasGain` cancels the only sample-rate-dependent factor left, so `targetAmplitude = gain·a_i` (§3) — which contains no sample-rate term at all — comes back bit-identical at all three rates and the test passes even when a coefficient is wrongly expressed in samples. Tolerance 0.5 dB. **Plus one sample-rate-sensitive timing assertion**, which is the FR-003 failure SC-010 exists to catch (`ampSmoothCoeff_`, `crossfadeLengthSamples_`, §4.4's `dt = chunk/fs`): with offset spread 0, the 50 %-crossing time of one partial's attack **expressed in seconds** must agree across the three rates within one control chunk (64/44100 s). |
| SC-011 | `harmonic_cloud_spectral_test.cpp` | `HarmonicCloud_NoAliasingAtExtremes` | `f0 = 4000`, 64 partials, `B = 0.1`, `\|g\| = 1`, 44.1 kHz. For each partial read `getPartialFrequencyHz(i)`; if `> nyquist`, fold with `calculateAliasedFrequency(f_i, 1, fs)` (`spectral_analysis.h:58` — harmonic number 1 so it folds the actual frequency, not `f0·n`); map with `frequencyToBin` (`:40`); sum with **`TestUtils::detail::sumBinPower`** (`:207`, in the `detail` namespace opened at `:189`), **excluding bins within ±2 of any legitimate sub-Nyquist partial**. The `TestUtils::` qualification is mandatory, not stylistic — see §7.5. `REQUIRE(aliasDb - fundamentalDb <= -60.0f)`. `getAliasedBins`/`AliasingTestConfig` are deliberately unused. |
| SC-012 | `harmonic_cloud_test.cpp` | `HarmonicCloud_EqualPowerPanAndSpread` | Over the pinned grid {−1,−0.5,0,+0.5,+1} via `setPartialPosition(i, p)` then read `getPartialPanLeft/Right` — **the test never re-implements the pan law**. (1) `\|L²+R²−1\| ≤ 1e-6`; (2) `L ≥ 0 && R ≥ 0`; (3) `L` strictly decreasing, `R` strictly increasing across the grid; (4) `\|L−R\| ≤ 1e-6` at 0. Spread: fresh seed, no override — at spread 0 `max\|L[n]−R[n]\| ≤ 1e-7` over a full render; over ≥4 increasing spreads the inter-channel Pearson correlation strictly decreases. |
| SC-013 | `harmonic_cloud_test.cpp` | `HarmonicCloud_PartialEnvelopeOffsetsStagger` | Mutation and drift 0 (⇒ `w_i ≡ 1`), richness 1 (64 active). Sample `getPartialCurrentAmplitude(i)` once per 512-block. **Steady-state target = `getPartialUnmutatedTargetAmplitude(i) * getPartialAntiAliasGain(i)`** — the accessor that excludes *both* `w_i` and `env_i` (§1.5 justification 1). It is **not** `getPartialTargetAmplitude(i)`, which is `unmutated·w·env` (§1.3, §4.2) and therefore moves with the envelope: using it would fire the 50 % crossing at `env ≈ 2 × smoother-lag ≈ 0.08` rather than at half the sounding level, and would make clause 4 degenerate (after gate-off `env → 0` ⇒ `target → 0`, so "≤ 1 % of its target" is unsatisfiable). Crossing = first sample at 50 % of the steady-state target. All four clauses use the same reference: (1) spread 0 → `max−min ≤ 1` block. (2) spread 1 → `100 ms ≤ max−min ≤ kMaxEnvOffsetSec`. (3) each partial ≥95 % of steady-state target within `attackSec + its offset`. (4) after `noteOff()`, each partial monotonically non-increasing and ≤1 % **of that same steady-state target** within `decaySec + its offset + 5·kAmpSmoothTimeSec`. |
| SC-014 | `harmonic_cloud_spectral_test.cpp` | `HarmonicCloud_RichnessAddsPartialsAndEnergy` | `f0 = 110`, 48 kHz, `r ∈ {0,.25,.5,.75,1}`. `REQUIRE(getActivePartialCount() == {1,3,8,23,64})`. Per-partial amplitude within 0.5 dB of `n^{−p(r)}` after removing the single FR-017 scalar gain. Metrics: (a) count above −60 dB rel. strongest, (b) above-fundamental energy fraction in dB **floored at −80 dB**. Both monotonically non-decreasing; `r=1` exceeds `r=0` by ≥16 partials and ≥20 dB. *Note:* the law `a_n = n^{−p}` is the assertion target, not the spec's tabulated dB figures — the `r = 0.75` entry there (−37.4 dB) does not follow from `23^{−1.125} = −30.6` dB, and both are far above the −60 dB floor either way. **T0 corrects the spec figure to −30.6 dB (§12.2)**, so a later compliance pass does not have to re-derive it. |
| SC-015 | `harmonic_cloud_test.cpp` | `HarmonicCloud_DriftIsIndependentDecimatedAndBounded` | (1) Log `getPartialDriftDetune(i)` once per block for 60 s; mean pairwise Pearson `\|r\| ≤ 0.2`, max pair `\|r\| ≤ 0.5`. (2a) Same total `N` (a multiple of 512) as 1×N, N/512×512, and ⌈N/577⌉×577 → each partial's final drift value agrees within 1e-5. (2b) `getDriftReadCount()` equals `Σ ceil(blockSize/64)` for each schedule. (2c) fingerprints of the 1-block and 512-block renders agree within `render_fingerprint.h` tolerances — **the clause a literal one-read-per-block implementation fails outright**. (3) 60 s at max depth: (a) `max\|cents\| ≤ driftCents_` within estimator tolerance and **exactly 0** at depth 0; (b) `max ≥ 0.25 × driftCents_`; (c) mean `\|detune\|` over `n∈[33,64]` ≥ 4× that over `n∈[1,8]`; (d) the per-partial mean-`\|detune\|` sequence contains ≥1 inversion. (4) after one `setDriftSmoothness`/`setDriftDepthCents` call, all 64 respect the new bound; both banks re-checked so neither setter touched the mutation bank. |
| SC-016 | `harmonic_cloud_test.cpp` | `HarmonicCloud_MutationStaysBoundedAndLevelStable` | `w = getPartialTargetAmplitude(i) / getPartialUnmutatedTargetAmplitude(i)`, sampled once per block **after the attack has elapsed with the gate held** so `env = 1`. Mutation ∈ {0, 0.5, 1}: `w ∈ [0.25, 1.75]`; 1 s-window block RMS within ±3 dB of the mutation-0 RMS; `\|Δw\|` per block ≤ `10 s⁻¹ × blockDuration`; at mutation 0, `w == 1.0f` exactly. Independence: (1) drift depth 0 + mutation 1 → `max\|w−1\| ≥ 0.1` for ≥half the active partials, ±3 dB still holds; (2) drift max + mutation 0 → every `w` exactly 1.0 while `getPartialDriftDetune` moves. Per-partial Pearson(weight, own detune) `\|r\| ≤ 0.3`. |
| SC-017 | `harmonic_cloud_test.cpp` | `HarmonicCloud_ParameterGridStaysFiniteAndBounded` | Full Cartesian grid: {min,mid,max}⁵ macros × {20,4000} Hz × {0,max} drift × {0,max} spread × {0,max} offset spread, each 1 s @48 kHz through a `noteOn()` (3⁵·2⁴ = 3888 renders — keep the render short and reuse one instance). Every sample finite by bit test (never `std::isnan`), `\|out\| ≤ kOutputClamp`, `stateFinite()` true at the end of each render. Setter hygiene, **both halves of FR-007** (§4.8): (a) *non-finite rejection* — for **every** setter incl. `setPartialPosition`, call with bit-pattern NaN and ±Inf built through a `volatile` sink, then assert the getter returns its **exact** pre-call value and a re-render matches the pre-call fingerprint; (b) *finite out-of-range clamping* — the half nothing else tests. Iterate §4.8's documented-bounds table; for each setter call it with `(min − 1)` and `(max + 1)` and `REQUIRE` the getter returns **exactly** the documented bound (e.g. `setSpectralTiltDb(100.0f)` → `+12.0f`, `setInharmonicity(5.0f)` → `0.1f`, `setFundamentalHz(1.0f)` → `20.0f`, `setSpectralGravity(-9.0f)` → `-1.0f`). Same loop, two more calls per setter. Without (b) a setter that assigned without `std::clamp` passes every other planned assertion. |
| SC-018 | `harmonic_cloud_test.cpp` | `HarmonicCloud_OnsetIsPhaseIncoherent` | Richness 1, tilt +12, over an **explicitly enumerated array of ≥8 seeds** (`constexpr std::array kSc018Seeds{…}`) rather than arbitrary draws, with the measured worst channel peak over that array recorded beside the threshold (§7.3: 1.249 at spread 0, 1.354 at spread 1 in the model; T3 replaces these with real numbers). Per seed: `REQUIRE(peak(first 100 ms) <= 0.9f * kOutputClamp)` on the rendered channels; onset peak-to-RMS ≤ steady-state peak-to-RMS + 6 dB. Across the seed array the measured onset peak varies (`stddev > 0`, and `max−min` ≥ a documented fraction of the mean). A phase-0 implementation fails all three. |
| FR-031/032 equivalence | `harmonic_cloud_test.cpp` | `HarmonicCloud_DriftLaneMatchesBrownianDrift` | **§6.4's honesty gate and §12's verification clause.** Construct a `BrownianDrift` (`brownian_drift.h:94`) with seed `deriveSeed(S, i)`, smoothness `s`, depth 1, `prepare(48000)`. Drive both it and the corresponding cloud lane through the identical chunk schedule; compare `BrownianDrift::getCurrentValue()` against **`getDriftLaneValue(i)`** (§1.5 addition 3 — the raw lane value, *not* `getPartialDriftDetune(i)`, which is a frequency multiplier from which `d_i` cannot be recovered to better than ≈2.6e-4 at index 0). `REQUIRE` agreement within `1e-5` at every chunk over 60 s. Run over **smoothness ∈ {0, 0.5, 1}** and over **both banks** (`getMutationLaneValue` against a `BrownianDrift` at `kMutationSmoothness`, seed `deriveSeed(S, i + 64)`), so a lane that omits `advanceSamples`' completion snap (§4.5) fails loudly instead of marginally. Measured achievable divergence with the §4.5 transcription: **0.000e+00**; with the naive closed form: up to 1.64e-4. This is the only test that includes `brownian_drift.h`. |
| FR-004 / FR-003 / Edge Cases | `harmonic_cloud_test.cpp` | `HarmonicCloud_GuardPaths` | Three guard behaviours the spec states explicitly and nothing else tests (§4.1). (1) Fill both buffers with a poison value; call `processStereoBlock(nullptr, R, 512)`, `(L, nullptr, 512)` and `(L, R, 0)`; `REQUIRE` every sample still equals the poison and `getDriftReadCount()` is unchanged. (2) On a **default-constructed, un-`prepare`d** instance, render 512 samples into a poisoned buffer → all-zero output, no crash. (3) `prepare(48000)` then `noteOn()` then render immediately **with no setter call at all** → every sample finite (bit test) and the block RMS > 0, which is FR-003's "processing is well-defined with no prior parameter call". A regression that dropped the null check would otherwise surface as a host crash in Phase 8, not as a red test. |
| FR-008 mask/solo | `harmonic_cloud_test.cpp` | `HarmonicCloud_MaskAndSoloAreClickFree` | FR-008 requires the solo/mask facility to be "itself subject to FR-014's smoother so using it cannot click", and §3 implements it by zeroing `targetAmplitude_i` at the **end** of the chain. Nothing else covers it: SC-006 sweeps only the five macros plus the fundamental, and SC-001/SC-012 use `soloPartial` as a measurement tool without asserting click-freeness. Same pinned `ClickDetectorConfig`, differential pass condition and mandatory positive control as SC-005: toggle `soloPartial(...)` / `setPartialMask(...)` / `clearPartialMask()` mid-render and assert `detections(masked) ≤ detections(control)`. Also `REQUIRE` that immediately after `soloPartial(k)` a non-soloed partial's `getPartialCurrentAmplitude` is still non-zero and only *decays* over the following blocks — an implementation that zeroed `currentAmplitude_` directly instead of `targetAmplitude_` passes the click detector on some seeds but fails this. |
| §4.9 denormal guard | `harmonic_cloud_test.cpp` | `HarmonicCloud_DecaysToExactZero` | The §4.9 guard is invisible to every other test because `dsp_test_main.cpp:13` calls `enableFTZDAZ()` before any case runs, so the *process* flushes denormals rather than the component. Assert the component's own arithmetic instead: `noteOn()`, render to steady state, `noteOff()`, render `decaySec + 2 s`, then `REQUIRE(getPartialCurrentAmplitude(i) == 0.0f)` **exactly**, for every `i < kMaxPartials`, and `REQUIRE(isQuiescent())`. Repeat with `soloPartial(0)` held for the whole render, asserting the same for every masked `i` — masked partials sit inside `[0, kernelCount_)` and are the case the tail high-water does not cover. This holds under FTZ and without it. |
| estimator | `harmonic_cloud_test.cpp` | `HarmonicCloud_FrequencyEstimatorResolution` | Synthesize a double-precision reference sinusoid at several known frequencies; assert the estimator recovers each within **0.001 cent** — the documented resolution SC-001 requires, and the proof it is ≥10× finer than 0.1 cent. |

### 7.5 Assertion hygiene applying to every test

- No bit-exact float golden anywhere (roadmap line 486). Where a render is pinned it is pinned with
  `render_fingerprint.h` (`fingerprintRender` `:64`, `compareFingerprints` `:101`,
  `kSampleTolerance = 1e-4f` `:49`, `kMetricTolerance = 1e-5` `:52`).
- Every threshold is either quoted from the spec or measured and recorded in a comment beside the constant.
- Designated initialisers for every aggregate (`ClickDetectorConfig`, any local config struct) — Clang errors
  on narrowing in brace init.
- **Every `test_helpers` symbol is written `TestUtils::`-qualified in these TUs.** `harmonic_cloud.h` includes
  `smoother.h`, which transitively pulls `core/db_utils.h` and therefore `Krate::DSP::detail`
  (`db_utils.h:39`); `spectral_analysis.h` opens `Krate::DSP::TestUtils::detail` (`:189`). A TU carrying both
  `using namespace Krate::DSP;` (for `HarmonicCloud`, `Complex`) and `using namespace Krate::DSP::TestUtils;`
  (for `frequencyToBin`, `calculateAliasedFrequency`, `ClickDetector`) makes the bare name `detail`
  ambiguous — verified with `clang++ -std=c++20`: *"error: reference to 'detail' is ambiguous — candidate
  found by name lookup is 'Krate::DSP::detail' … candidate found by name lookup is
  'Krate::DSP::TestUtils::detail'"*. So `TestUtils::detail::sumBinPower(...)`, never `detail::sumBinPower(...)`.
- `node tools/check-portability.js` is run before commit; MSVC-green proves nothing about the GCC/AppleClang
  legs.

---

## 8. Build integration

Two edits, both in `dsp/tests/CMakeLists.txt`:

1. **`dsp_systems_tests` source list** (currently ends at `unit/systems/sympathetic_resonance_test.cpp`,
   line 331). Append:
   ```cmake
       unit/systems/harmonic_cloud_test.cpp
       unit/systems/harmonic_cloud_spectral_test.cpp
       unit/systems/harmonic_cloud_perf_test.cpp
   ```
   The list is explicit — there is no GLOB — so a file omitted here **silently drops from the build**
   (`dsp/tests/CMakeLists.txt:18-19`).
2. **No change** to the `-fno-fast-math` `set_source_files_properties` block (`:385-647`) — see §7.1.
3. **No change** to `catch_discover_tests(dsp_systems_tests REPORTER console)` (`:665`); it already covers the
   target.

Nothing outside `dsp/` changes: no plugin, no CI roster, no clang-tidy target list (`dsp` already covers
`dsp/include/`).

Commands:

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
# perf case is hidden by [.perf]; run it explicitly:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_CpuBudget"
# regression on the reused kernel:
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
node tools/check-portability.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

---

## 9. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | **CPU budget.** The literal FR-031/FR-032 design measured 63,900 ns/block against a 35,533 ns baseline gate. | §6: SoA lane bank, projected ≈28,900 ns. Escalated in §6.4 as a spec-amendment request (drafted in §12, applied as blocking step T0), with T-DRIFT-EQUIV as the honesty gate and a fallback branch costed. Baseline is checked in **only after** the real measurement, and only if `baseline × 1.5 ≤ 53,333`. |
| R2 | **`kMaxPartials` ODR/redefinition.** `Krate::DSP::kMaxPartials = 96` is `inline constexpr` at namespace scope (`harmonic_types.h:21`) and will be in the same TU. | Class-scoped `HarmonicCloud::kMaxPartials = 64`; `harmonic_cloud.h` never includes `harmonic_types.h` or `harmonic_oscillator_bank.h` (§0.1). A test that includes both headers is part of `harmonic_cloud_test.cpp` so the collision is caught at compile time on every leg. |
| R3 | **Pan domain mismatch.** `equalPowerGains` takes [0,1] and does not clamp (`crossfade_utils.h:41`); a bipolar position yields a polarity-inverted right channel that still passes `L²+R²=1`. | Mandatory remap `(pos+1)*0.5` clamped (§4.3); SC-012 clauses 2–4 are the detector. |
| R4 | **Kernel accumulates.** `processMcfBatchSIMD` does `sumL += outSumL` (`…_simd.cpp:182-183`). A caller that forgets to zero per sample gets a silently ramping DC output. | The render loop zeroes `sl`/`sr` inside the sample loop (§4.1); SC-017's `\|out\| ≤ kOutputClamp` and SC-018's onset peak both fail loudly if it regresses. |
| R5 | **Aliasing on the drift path.** `antiAliasGain` must be computed on the *detuned* frequency (`…bank.h:1073`); using the undetuned one lets an over-detuned high partial sustain at clamped epsilon. | Recomputed per chunk from `epsilon·detune` via the `sqrt(1−(ε/2)²)` identity (§2). SC-011 at the worst case (`f0 = 4 kHz`, `B` max, `\|g\|` max, 44.1 kHz) is the gate. |
| R6 | **`-ffast-math` on the macOS leg.** `std::isnan` folds to false. | Component guards use `detail::isNaN`/`isInf` (`db_utils.h:54,174`, `std::bit_cast` on the exponent field); tests build NaN/Inf through a `volatile` sink, never `std::numeric_limits` (`reference_fastmath_nan_in_tests`). No `-fno-fast-math` property needed. |
| R7 | **Denormals.** 64 partials decaying toward zero park denormals in the kernel's smoothing recurrence (`currentAmplitude_`, which the kernel never flushes — `…_simd.cpp:93`, `:120`) and in the OU walks. Masked/soloed-out and released partials stay inside `[0, kernelCount_)`, so the §3 tail high-water does not reach them. **FTZ/DAZ in `dsp_test_main.cpp:13` hides this from every other test**, so a guard that exists only in the environment would ship. | Per-chunk guard on `currentAmplitude_` (not `targetAmplitude_`) over `i < kernelCount_` at `kTailSilenceThreshold` (§4.9, the `…bank.h:756` idiom generalised); `kDriftDenormalFloor` on each walk (`brownian_drift.h:264-266`) plus the lane smoother's own `flushDenormal` (§4.5). Verified by `HarmonicCloud_DecaysToExactZero`, which asserts exact `0.0f` — an assertion about the component's arithmetic that holds with or without FTZ. |
| R8 | **`std::array<Xorshift32, N>{}` does not compile** (explicit-only constructor, `random.h:44`) — reproduced this session with `clang++ -std=c++20 -fsyntax-only`. The tempting fix, transcribing `next()`/`nextFloat()` into the Layer-3 header, duplicates Layer-0 RNG internals: a later change to `random.h` would silently desynchronise the cloud's streams from `BrownianDrift`'s, breaking SC-009 and T-DRIFT-EQUIV **with no compile-time signal**. | Keep the Layer-0 type. `struct LaneRng { Xorshift32 rng{1}; };` and `std::array<LaneRng, kMaxPartials>` — value-initialises cleanly (same probe, verified this session). No RNG math is copied anywhere in `harmonic_cloud.h` (§0.1 trap 3, §1.3, §4.5). |
| R9 | **SC-003's 65536-point FFT.** `kMaxFFTSize = 8192` (`fft.h:47`) is documentary, not enforced — a future tightening would make the test analyse a zero-size spectrum and pass vacuously. | `REQUIRE(fft.isPrepared())` after `prepare(65536)` (§7.2). |
| R10 | **Allocation detector inert or read wrong.** `allocation_detector.h`'s global replacements are commented out (`:99-138`); counting needs `allocation_operator_overrides.h`, which must appear in exactly one TU per binary. Separately, `AllocationScope::getAllocationCount()` returns a value assigned only in the destructor (`:75-95`), so reading it in scope always yields 0 and reading it out of scope does not compile — the liveness assertion would pass vacuously either way. | No new TU includes the overrides header (`selectable_oscillator_test.cpp:388` owns it). SC-008 uses the `AllocationDetector::instance().startTracking()` / `stopTracking()` idiom (`selectable_oscillator_test.cpp:418-422`), **not** `AllocationScope` (§7.1), and asserts a deliberate-allocation liveness case **before** the zero-allocation assertion. |
| R11 | **SIMD alignment.** Any new SIMD must use unaligned loads/stores; the aligned-load lint enforces it. | No new SIMD is written — the existing kernel already uses `hn::LoadU`/`hn::StoreU` (`…_simd.cpp:80-110`). `alignas(32)` on the SoA arrays is a locality choice, never an alignment assumption (FR-012). |
| R12 | **Numerical stability of the MCF at high partials.** `epsilon·detune` can exceed the `\|ε\| < 2` bound. | The kernel clamps the *effective* coefficient itself (`…_simd.cpp:103`, `:124`); the base epsilon is clamped to `kMaxEpsilon` at config time (§2); determinant is exactly 1 so no renormalisation is needed (`…bank.h:698-701`). `stateFinite()` over an SC-017 render is the gate. |
| R13 | **Config-rate cost outside the measured budget.** §6.2's projection covers only the render path, while Phase 7 life-modulates all five macros plus the fundamental. Unconditional recompute costs ≈6,100 ns/block — 92 % of the headroom between the projection and SC-007's 35,533 ns gate. | §4.8's no-op guard + dirty-flag deferral (≈2,050 ns/block); §6.3 costs both; the `static_assert` is applied to `HarmonicCloud_CpuBudgetUnderAutomation`, not to the static case. |
| R14 | **Crest-factor margin at SC-003/SC-018.** FR-017 pins RMS at 0.5 while the `0.9 · kOutputClamp` precondition demands crest ≤ 11.1 dB; at SC-003's +6/+12 dB/oct settings the measured worst-seed margin is only 1.5–2.0 dB, so an unpinned seed can red the precondition on a correct implementation. | §7.3's measured table; SC-003 pins its seed **and** `setStereoSpread(0.0f)`; SC-018 pins an explicit seed array and records the measured distribution. T3 re-measures on the real component. Thresholds are unchanged — only the configuration is made reproducible. |
| R15 | **`detail` is ambiguous in the spectral test TUs** (`Krate::DSP::detail` via `db_utils.h:39` vs `Krate::DSP::TestUtils::detail` via `spectral_analysis.h:189`) — verified to be a hard clang error. | §7.5: every `test_helpers` symbol is `TestUtils::`-qualified; SC-011's row says `TestUtils::detail::sumBinPower`. |

---

## 10. Implementation order

0. **T0 — spec amendment. BLOCKING; nothing else starts until it lands.** Apply §12's drafted replacements to
   `spec.md`: FR-031, FR-032, FR-035 and FR-072 restated behaviourally (D1), and SC-014's tabulated `r = 0.75`
   figure corrected to −30.6 dB (D7). Without this, four FRs are unmet by construction and the
   Completion-Honesty compliance table cannot be filled honestly. → *verify:* the four FRs and SC-014 in
   `spec.md` read as §12 specifies; §12's verification clause names `HarmonicCloud_DriftLaneMatchesBrownian-
   Drift`. If the amendment is **refused**, take §6.4's alternative branch (128 `BrownianDrift` objects +
   `advanceSamples` memoisation) and renegotiate SC-007 in the spec — do not start T6 on the SoA design.
1. **T1 — skeleton.** `harmonic_cloud.h` with constants, SoA state (names per §1.3, including `fadeStart_`,
   `invFadeRange_`, `tailHighWater_`, `freqDirty_`, `ampDirty_`), `prepare`/`reset`, `processStereoBlock`
   guard paths + quiescent early-out, and the FR-008 accessors returning defaults. Wire the three test TUs
   into `dsp/tests/CMakeLists.txt`; add `T-ESTIMATOR-RESOLUTION`, `HarmonicCloud_GuardPaths`, and a
   compile-only test that includes both `harmonic_cloud.h` and `harmonic_types.h` (R2). → *verify:* builds,
   estimator + guard-path tests green.
2. **T2 — frequency + kernel.** §2 pipeline, chunked render loop, seeded phases (§4.6 partial: phases only),
   FR-006 clamp. → *verify:* SC-001, SC-002 (B only), SC-018 partially.
3. **T3 — amplitude chain.** Richness, tilt, normalizer (**including `normGain_.setTarget` at the end of
   `recalculateAmplitudes()` — §3**), tail fade. **Then measure the crest factor** at SC-003's and SC-018's
   pinned configurations across a seed sweep and record the real peak/RMS numbers in comments beside the
   `0.9 · kOutputClamp` thresholds, replacing §7.3's modelled table (same treatment §6 gives the CPU budget).
   Pin SC-003's seed + `setStereoSpread(0.0f)` and SC-018's seed array from that measurement. → *verify:*
   SC-003, SC-014, SC-018.
4. **T4 — gravity.** FR-081/083 composition. → *verify:* SC-004, SC-011.
5. **T5 — pan + spread.** §4.3, full `reseed()` draw order. → *verify:* SC-012, SC-009.
6. **T6 — drift lanes.** *Requires T0.* §4.5 both banks (`LaneRng`-held `Xorshift32`, the four-step
   `advanceSamples` transcription including the `kCompletionThreshold` snap), `getDriftLaneValue` /
   `getMutationLaneValue`, detune application, per-chunk anti-alias. → *verify:*
   `HarmonicCloud_DriftLaneMatchesBrownianDrift` **first**, at smoothness {0, 0.5, 1} on both banks and at
   the full 1e-5 tolerance (measured achievable: 0.0 — a failure here means the transcription is incomplete,
   **never** that the tolerance is wrong), then SC-015.
7. **T7 — mutation.** FR-071/072/073. → *verify:* SC-016.
8. **T8 — envelope + gate + retrigger.** §4.4, §4.7, `isQuiescent()`. → *verify:* SC-013 (against
   `getPartialUnmutatedTargetAmplitude · getPartialAntiAliasGain`), SC-006 retrigger clauses.
9. **T9 — hygiene.** FR-007 **both halves** across every setter (non-finite rejection *and* finite
   out-of-range clamping) with the §4.8 no-op guard + dirty flags, `stateFinite`, the §4.9
   `currentAmplitude_` denormal guard, mask/solo smoothing. → *verify:* SC-017 (incl. the out-of-range pass),
   SC-008, SC-010, SC-005, SC-006 sweeps, `HarmonicCloud_MaskAndSoloAreClickFree`,
   `HarmonicCloud_DecaysToExactZero`.
10. **T10 — perf.** Record `kStaticBaselineNsPerBlock` **and** `kAutomatedBaselineNsPerBlock`, each with
    machine + date. The `static_assert` gates on the automated one (§6.3). **If it exceeds 35,533 ns, the
    phase is over budget** — apply the `std::exp2` lever (§6.2), then re-measure; do not raise the baseline.
    Also `WARN`-report the quiescent cost to confirm §4.1's early-out is live.
11. **T11 — gates.** `node tools/check-portability.js`, `run-clang-tidy -Target dsp`, full
    `dsp_systems_tests` + `dsp_processors_tests`.

---

## 11. Deviations recorded (nothing silent)

| # | Deviation | Where | Why |
|---|---|---|---|
| D1 | Drift lanes are a cloud-private SoA OU bank, not 128 `BrownianDrift` objects (FR-031, FR-032, FR-035, FR-072 name the class and its methods). | §4.5, §6.4, §12 | Measured: the literal design is 1.80× over SC-007's own baseline gate. Same recurrence, coefficients, three-draw sequenced Irwin-Hall increment, clamps, control interval, and — after the §4.5 correction — a **bit-identical** output smoother (`advanceSamples` transcribed with its `isComplete` skip, `flushDenormal` and `kCompletionThreshold` snap; measured divergence 0.000e+00). **Spec amendment drafted in §12 and applied as blocking step T0** — this is no longer merely "flagged". Gated by `HarmonicCloud_DriftLaneMatchesBrownianDrift` at 1e-5, smoothness {0, 0.5, 1}, both banks. |
| D2 | Seven accessors added to FR-008's surface (`getPartialAntiAliasGain`, `getPartialSinState`, `getPartialCosState`, `getDriftLaneValue`, `getMutationLaneValue`, `getDriftReadCount`, `isQuiescent`). | §1.5 | SC-013's steady-state clauses, SC-006's retrigger-mechanism clause, SC-015 clause 2(b), D1's honesty gate (the lane value cannot be recovered from `getPartialDriftDetune` to better than ≈2.6e-4 at index 0, vs a 1e-5 tolerance) and §4.1's quiescent early-out are otherwise unwritable/unobservable. Precedent: `panRecomputeCount()` (`…bank.h:606`). |
| D3 | FR-013's crossfade snapshots L and R separately instead of the reference's mono `(L+R)/2`. | §2 | Same 3 ms mechanism, without collapsing the stereo image; no FR or SC depends on the mono form. |
| D4 | FR-043's tail fade runs inside the SIMD kernel via `kernelCount_ ≥ activeCount_` rather than the reference's scalar tail loop (`…bank.h:746-779`). | §3 | Identical behaviour (departing partials fade through the shared `ampSmoothCoeff`), fewer branches, no scalar path. |
| D5 | `antiAliasGain`'s MCF correction uses `sqrt(1−(ε·detune/2)²)` instead of `cos(π·f·detune/fs)`. | §2 | **Identical only at `detune = 1`** — the earlier "algebraically identical for `f ≤ fs/2`" claim was wrong, because `detune·sin θ ≠ sin(detune·θ)`. Under drift it evaluates the MCF correction on the orbit the kernel actually synthesizes (`asin(epsEff/2)`, `…_simd.cpp:103`) rather than on the nominal detuned frequency, which is the *more* correct target. Measured divergence at the maximum ±50-cent drift: 0.005 dB at 7.04 kHz, 2.09 dB at 19 kHz. SC-010 renders drift-free so its 0.5 dB tolerance is untouched; SC-011 asserts the rendered alias floor, not the correction. ~8× cheaper, which is what puts the per-chunk recompute inside the CPU budget. |
| D6 | SC-001/SC-002/SC-004/SC-010's frequency measurement uses a two-stage heterodyne phase-slope estimator rather than a quadratic-interpolated FFT peak. | §7.2 | SC-001 explicitly permits the alternative; an FFT peak cannot resolve 0.01 cent at 55 Hz. Resolution is asserted, not claimed. |
| D7 | SC-014 asserts per-partial amplitudes against the law `a_n = n^{−p(r)}`, not against the spec's tabulated dB values, **and the spec's table is corrected in T0**. | §7.4, §12 | The tabulated `r = 0.75` figure (−37.4 dB) does not follow from `p(0.75) = 1.125`, `N = 23`, `23^{−1.125} = 0.0294` = **−30.6 dB** (the other three check out: `3^{−2.375}` = −22.7, `8^{−1.75}` = −31.6, `64^{−0.5}` = −18.1). The law is the requirement (FR-041); all four values clear the −60 dB floor, so no threshold moves. Leaving the wrong number in the spec would force a later compliance pass to re-derive it, so §12 amends it. |
| D8 | Every macro setter carries a `if (v == shadow_) return;` no-op guard and defers its recompute through `freqDirty_`/`ampDirty_` to the next chunk boundary. | §4.8, §6.3 | Without it the Phase-7 call pattern (five macros + fundamental automated per block) costs ≈6,100 ns/block = 92 % of the headroom between §6.2's projection and SC-007's 35,533 ns gate. With it, ≈2,050 ns. Behaviour is unchanged up to the 64-sample control grid, which is already the component's documented resolution (FR-032). |
| D9 | `processStereoBlock` returns early with a zero-filled buffer when `isQuiescent()`, after advancing both lane banks by `numSamples` and incrementing `driftReadCount_`. | §4.1, §4.7 | A finished voice otherwise burns the full ≈28,900 ns/block to produce silence, and Phase 7 keeps released voices resident for 10 s+ (roadmap line 287). Advancing the lanes before returning keeps free-running life-modulation (roadmap Key Design Decision 1) and leaves SC-015's clauses 2(a)/2(b)/2(c) bit-for-bit unaffected. |

---

## 12. Required spec amendments (step T0 — blocking)

These are edits to `specs/seraphis-phase2-harmonic-cloud/spec.md`, not to this plan. They must land **before
T6**. Two of them (D1, D7) are the reason T0 exists; without them four FRs are unmet by construction and a
Completion-Honesty compliance table would have to record FR-031, FR-032, FR-035 and FR-072 as ❌.

### 12.1 D1 — restate FR-031/032/035/072 behaviourally

The defect is the unamended spec, not the design: §6.1's measurement shows 128 `BrownianDrift` instances cost
44,402 ns/block against SC-007's own baseline gate of 35,533 ns (1.80×), while the SoA lanes cost 9,426 ns.
Carry those numbers into the spec as the rationale. In each FR, replace *class identity* with *behaviour*, and
keep every other clause verbatim.

**FR-031** — replace the first sentence with:

> Each partial owns an **independent detune Ornstein–Uhlenbeck drift lane** whose recurrence, coefficients
> and clamps are exactly `BrownianDrift`'s (`brownian_drift.h:94`), with a distinct seed derived from the
> cloud seed so partials drift independently. Concretely, each lane implements: `τ = kTauMin + smoothness ·
> (kTauMax − kTauMin)` (`:231-234`); `a = exp(−Δt/τ)` (`:235`); `g = kInternalStd · sqrt(max(1−a², 0))`
> (`:237-239`); an increment `z` formed from **three explicitly sequenced** `Xorshift32::nextFloat()` draws
> (Irwin-Hall, `:257-260`); `x ← clamp(a·x + g·z, ±kWalkLimit)` with the `kDenormalFloor` flush (`:262-266`);
> an output target `clamp(depth·x, ±1)` (`:249-251`); and a **150 ms one-pole output smoother advanced
> exactly as `OnePoleSmoother::advanceSamples` advances it** (`smoother.h:243-254`) — including its
> `isComplete()` skip, its `detail::flushDenormal`, and its hard snap to target below
> `kCompletionThreshold = 1e-4f` (`smoother.h:55`). The cloud owns **two** such banks — the detune bank here
> and the independent mutation bank of FR-072 (Clarifications Q1) — 2 × `kMaxPartials` = 128 lanes, all
> fixed-size members per FR-002, all prepared in `prepare()` per FR-003. Seed derivation must give all 128
> lanes distinct streams.
>
> *Whether the lanes are 128 `BrownianDrift` objects or a structure-of-arrays transposition of the same
> recurrence is an implementation choice governed by SC-007.* It is measured, not assumed: 128
> `BrownianDrift` instances cost 44,402 ns per 512-sample block on the reference machine, 1.80× SC-007's own
> baseline gate of 35,533 ns, while the SoA form costs 9,426 ns (plan §6.1). Equivalence is **verified, not
> asserted**, by a dedicated test that drives a real `BrownianDrift` and the shipped lane from the same seed,
> smoothness and sample rate through an identical chunk schedule and requires their value sequences to agree
> within 1e-5 at every chunk over 60 s, at smoothness {0, 0.5, 1}, on both banks
> (`HarmonicCloud_DriftLaneMatchesBrownianDrift`). Any implementation that fails that test violates FR-031.

**FR-032** — keep the entire requirement verbatim except the two method-name clauses. Replace

> per chunk, each partial's `BrownianDrift::processBlock(chunkLength)` (`brownian_drift.h:194`) is called
> once and `getCurrentValue()` (`brownian_drift.h:212`) is read **exactly once**

with

> per chunk, each partial's drift lane is **advanced by exactly `chunkLength` samples** — with the same
> internal structure as `BrownianDrift::processBlock` (`brownian_drift.h:194-206`): an OU control step every
> `kControlRateInterval = 32` samples and an output-smoother advance over each intervening span — and its
> smoothed value is **read exactly once**

The read-cadence clause (`ceil(numSamples / 64)` reads per partial per block), the "why chunked rather than
literally per block" paragraph, the "2 internal OU steps, not one" statement and the partition-invariance
statement all stand **unchanged** — they are the observable contract and SC-015 clause 2 measures them.

**FR-035** — replace *"via `setSmoothness`/`setDepth` (`brownian_drift.h:152,159`)"* with:

> — one cloud-level smoothness value feeds every detune lane's `τ`, and one cloud-level depth feeds the
> applied cents bound, with no per-partial API call in the audio path (the semantics of
> `BrownianDrift::setSmoothness`/`setDepth`, `brownian_drift.h:152,159`)

Everything else in FR-035, including the mutation-bank independence clause, stands verbatim.

**FR-072** — replace *"a **second, dedicated per-partial `BrownianDrift` bank**"* with *"a **second,
dedicated per-partial drift bank** with the FR-031 lane behaviour"*, and *"64 further instances"* with
*"64 further lanes"*. The four sub-bullets — own derived seed; **depth pinned at 1.0 internally**, never
touched by the FR-035 drift-depth control, so drift depth 0 must not disable or attenuate Mutation; smoothness
a documented cloud-level constant independent of FR-035's; advanced and read on the FR-032 chunk cadence —
stand **verbatim**. They are the independence contract SC-016 measures.

### 12.2 D7 — correct SC-014's tabulated dB figure

In SC-014's *Law check* paragraph, the parenthetical currently reads

> the weakest active partial's rolloff amplitude `N^(−p(r))` is −22.7, −31.6, −37.4 and −18.1 dB relative to
> the fundamental

The third figure is wrong: at `r = 0.75`, `p(0.75) = 3.0 − 2.5·0.75 = 1.125` and `N = round(64^0.75) = 23`,
so `23^{−1.125} = 0.0294` = **−30.6 dB**, not −37.4 dB. The other three check out
(`3^{−2.375}` = −22.7 dB, `8^{−1.75}` = −31.6 dB, `64^{−0.5}` = −18.1 dB). Replace −37.4 with **−30.6**,
citing plan D7. No threshold changes — all four values remain far above SC-014's −60 dB audibility floor,
which is what the parenthetical exists to justify — but leaving the wrong number standing forces a later
compliance pass to re-derive it, and the same table is the justification for the floor being clear.

---

## Review notes

Every issue raised in review was accepted and applied. Two are recorded here because the *fix applied*
differs from the *fix suggested*, and an implementer following the suggestion literally would reintroduce
the defect.

1. **The drift-lane smoother snap (§4.5).** Two reviewers reported the same blocker from different lenses,
   with different remedies. One suggested `if (|smoothCur − smoothTgt| < kCompletionThreshold) { smoothCur =
   smoothTgt; }` **before** the multiply. That is not what `OnePoleSmoother::advanceSamples` does:
   `smoother.h:244` early-**returns** on `isComplete()` and leaves `current_` **unchanged** — it does not
   snap on entry. A pre-multiply snap is a different function and would reintroduce divergence at exactly the
   points the gate is measuring. §4.5 therefore transcribes the real control flow — `continue` on entry,
   multiply, `flushDenormal`, post-snap — which the second reviewer measured as bit-identical (worst
   `|diff| = 0.000e+00`) against a real `BrownianDrift`. The finding is accepted in full; only the suggested
   mechanism is corrected.

2. **The tilt exponent in the SC-003 crest analysis.** The review's derivation gave `a_n ∝ n^{1.49}` at
   `r = 5/6`, tilt +12 dB/oct. The correct exponent is `n^{1.0765}`: 12 dB per doubling of `n` is
   `n^{12/(20·log₁₀2)} = n^{1.9932}`, less the rolloff `n^{−0.9167}`. **This does not weaken the finding** —
   §7.3 measures the crest factor directly rather than deriving it, and the measurement confirms the review's
   conclusion: the worst-seed margin at +6/+12 dB/oct is 1.5–2.0 dB, inside seed variance, and a 24-seed
   model run already produced a mono peak of 2.031 against a `kOutputClamp` of 2.0. SC-003's seed and stereo
   spread are pinned accordingly, and T3 re-measures on the real component.

Nothing was resolved by relaxing a threshold. The two thresholds the review put pressure on — T-DRIFT-EQUIV's
1e-5 and SC-003/SC-018's `0.9 · kOutputClamp` — are both **kept as written**, and are now backed by a
measurement showing they are achievable (0.000e+00 divergence; 1.5–3.2 dB of crest margin at pinned
configurations) rather than by assumption.
