# Implementation Plan: Seraphis Phase 3 — Spectral States & Morphing Engine

**Spec:** `specs/seraphis-phase3-spectral-morph/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 3 (lines 170–192)
**Deliverables:** two new Layer 2 headers, one new Layer 3 header, two additive Layer 0 helpers,
one additive amendment to the Phase 2 Layer 3 component, six new/extended test TUs.
**Plugin work:** none.

Every existing-code citation below was read in this session at the line given. Where the spec's
arithmetic or wording could not be implemented as literally written, the deviation is recorded in
§11 with the reason — nothing is silently reinterpreted.

---

## 0. Reused components — verified signatures (read this session)

| What | Where (verified) | Exact form reused |
|---|---|---|
| `Xorshift32` | `dsp/include/krate/dsp/core/random.h:40` | `explicit constexpr Xorshift32(uint32_t = 1)` (:44, substitutes `kDefaultSeed = 2463534242u` :84 for 0); `next()` :49; `nextFloat()` → `[-1,1]` :58; `nextUnipolar()` → `[0,1]` :66; `seed(uint32_t)` :72 (same substitution); `state()` :78. **The whole file is 94 lines and includes only `<cstdint>` (:17).** |
| `HarmonicCloud::deriveSeed` | `dsp/include/krate/dsp/systems/harmonic_cloud.h:651-660` | `[[nodiscard]] static constexpr std::uint32_t deriveSeed(std::uint32_t base, std::size_t salt) noexcept`; body `h = base ^ ((salt+1)*0x9E3779B9u)`, `h^=h>>16`, `h*=0x7FEB352Du`, `h^=h>>15`, `h*=0x846CA68Bu`, `h^=h>>16`, `return (h != 0u) ? h : 0x2545F491u;` |
| `semitonesToRatio` / `ratioToSemitones` | `dsp/include/krate/dsp/core/pitch_utils.h:23`, `:31` | `[[nodiscard]] inline float semitonesToRatio(float) noexcept` = `std::pow(2.0f, s/12.0f)` (:25); `ratioToSemitones` guarded at `ratio <= 0` (:33-35). `frequencyToCentsDeviation` (:175) wraps to ±50 and is **not** usable. |
| `detail::isNaN` / `detail::isInf` | `dsp/include/krate/dsp/core/db_utils.h:54`, `:174` | `constexpr bool isNaN(float) noexcept` — `((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0)` over `std::bit_cast`; `isInf` — `(bits & 0x7FFFFFFFu) == 0x7F800000u`. `namespace detail` opens :39, closes :179. |
| `detail::constexprExp` / `constexprLn` | `dsp/include/krate/dsp/core/db_utils.h:121`, `:80-112` | `constexpr float constexprExp(float) noexcept` (range-reduced Taylor, 16 terms, :138-142); `constexprLn` (atanh series, 12 terms, :106-111). **These are what make the transcendental constants of §4.1 genuinely `constexpr` under C++20**, where `std::exp2`/`std::log2` are not. |
| `BrownianDrift` | `dsp/include/krate/dsp/processors/brownian_drift.h:94` | **Public** constants, reused by name: `kTauMin = 0.2f` :97, `kTauMax = 30.0f` :99, `kInternalStd = 0.5f` :101, `kDriftOutputSmoothMs = 150.0f` :103, `kControlRateInterval = 32` :105. Discretisation: `tau = kTauMin + smoothness*(kTauMax-kTauMin)` :231-234, `a = exp(-dt/tau)` :235, `g = kInternalStd*sqrt(1-a²)` :237-239, **all in `double` intermediates** (:230-240, verified this session). Step: three **sequenced** `nextFloat()` draws summed (Irwin-Hall) :257-260, `x = mean + a*(x-mean) + g*z` :262, clamp to `±kWalkLimit` :263, denormal flush :264-266, `smoothTgt = clamp(depth*x, -1, 1)` :269 + :249-251. |
| `BrownianDrift`'s walk bounds — **transcribed, not reused** | `brownian_drift.h:221` opens `private:`; `kWalkLimit = 4.0f` :226 and `kDenormalFloor = 1e-20f` :228 are **below it** (verified this session) | They are **not** part of `BrownianDrift`'s public surface and `EntropyProcessor` cannot name them. Phase 2 hit the same wall and solved it by re-declaring its own copies — `HarmonicCloud::kDriftWalkLimit = 4.0f` / `kDriftDenormalFloor = 1e-20f` at `harmonic_cloud.h:156-157`, with the comment `Random-walk hard bound and denormal floor (brownian_drift.h:226,228)`. This plan follows that precedent exactly (§4.1). **They are NOT promoted to `public:` in `brownian_drift.h`** — that would be a fourth amendment to a COMPLETE Phase 1 component and would need RA-1 treatment for no benefit. |
| `SplineTrajectory` | `dsp/include/krate/dsp/processors/spline_trajectory.h:114` | `prepare(double)` :136 (floors rate at 1 Hz :137); `reset()` :144; `setSeed(uint32_t)` :156; `setWaypointInterval(double)` :165 clamped to `[kMinInterval 0.5f :117, kMaxInterval 30.0f :119]`; `setDepth(float)` :174; `processBlock(size_t)` :193 (0 is a no-op :194-196); `getCurrentValue()` :204; `getSourceRange()` → `{-1,+1}` :209. `kWaypointMax = 0.8f` :121, `kDefaultInterval = 2.0f` :123. Waypoints drawn as `rng_.nextFloat() * kWaypointMax` :218-220. `du_ = 1/(interval_*sampleRate_)` :215. `advance()` rotates as many waypoints as needed :262-269. |
| `HarmonicCloud` | `dsp/include/krate/dsp/systems/harmonic_cloud.h:122` | `kMaxPartials = 64` :133, `kControlChunkSamples = 64` :139, `kOutputClamp = 2.0f` :169, `kTargetOscRms = 0.5f` / `kMaxNormGain = 20.0f` :172-173, `kNormGainSmoothMs = 20.0f` :176, `kMaxInharmonicity = 0.1f` :186, `kMinTiltDbPerOct/kMaxTiltDbPerOct = ∓12.0f` :189-190, `kGravityExponentRange = 0.1f` :193, `kMaxDriftCents = 50.0f` :209. `prepare(double)` :255, `reset()` :286, `setFundamentalHz` :341 with the `isNaN/isInf` reject idiom :342-344, `noteOn()` :593, `noteOff()` :621, `setSeed` :665, `processStereoBlock` :682 (chunk loop :713-746, `updateControl(chunk)` :716), `stateFinite()` :858. Injection points: `recalculateFrequencies()` :1064 (law :1082-1092), `tiltGain()` :1105, `recalculateAmplitudes()` :1134 (count law :1138-1139, rolloff×tilt :1155-1156, tail high-water :1163-1164, `normGain_.setTarget` **last** :1172), dirty-flag consumption :1313-1321, `updateAntiAliasGain` :1229-1244 (`fade = 0` at `fEff >= nyquist_` :1237-1238), `currentNormGainTarget()` :1453. |
| `HarmonicCloud::DriftLanes` | `harmonic_cloud.h:929-952` | The **lane-batched OU** pattern this plan copies for the two entropy banks: SoA `walk`/`smoothCur`/`smoothTgt` + `std::array<LaneRng, 64> rng` (`LaneRng` :925-927 exists because `Xorshift32`'s only ctor is `explicit`), shared `samplesUntilControl` :937, `cachedPowN`/`cachedPowValue` memo :950-951. Helpers: `updateDriftCoefficients` :1519, `advanceControlStepAllLanes` :1544, `advanceSmootherAllLanes` :1620 (a **literal transcription** of `OnePoleSmoother::advanceSamples`, incl. the `isComplete` early-`continue` :1632-1633, `flushDenormal` :1636, and the post-advance snap :1637-1639), `advanceDriftLanes` :1655, `resetDriftLanes` :1670. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h:134` | `configure(float ms, float sr)` :160, `setTarget` :170, `getCurrentValue` :191, `advanceSamples(size_t)` :243, `snapTo(float)` :263; shared `kCompletionThreshold = 0.0001f` :55; `detail::flushDenormal` used at :250. **Its time convention, read this session and load-bearing for FR-044:** `calculateOnePolCoefficient` (:91) is `coeff = detail::constexprExp(-5000.0f / (clampedTime * sampleRate))`, documented at :86-90 as "time to 99 % ≈ 5·tau" — i.e. **`tau = smoothTimeMs / 5000` seconds, not `smoothTimeMs / 1000`**. A `smoothTimeMs` of 150 is a 30 ms time constant, and `brownian_drift.h:47-56` restates the same figure as `1 − coeff = 6.94e-4` **per sample** at 150 ms / 48 kHz. §5.1 derives every per-chunk smoother step from this expression rather than from the millisecond number. |
| `HarmonicSnapshot` L2-normalisation idiom | `dsp/include/krate/dsp/processors/harmonic_snapshot.h:99-107` | `if (sumSquares > 0.0f) { invNorm = 1/sqrt(sumSquares); … *= invNorm; }` — copied verbatim by FR-014. The struct itself is **not** reused (96-slot, analysis-typed). |
| `SpectralMorphFilter::applyMagnitudeInterpolation` | `dsp/include/krate/dsp/processors/spectral_morph_filter.h:590-606` | `blendedMag = magA * invMorph + magB * morphAmount` with `invMorph = 1.0f - morphAmount` (:594, :601). **The class is not used**; only this two-term form, adopted verbatim by FR-041's amplitude law. |
| `render_fingerprint.h` | `tests/test_helpers/render_fingerprint.h` | `kRenderCheckpoints = 32` :46, `kSampleTolerance = 1.0e-4f` :49, `kMetricTolerance = 1.0e-5` :52, `struct RenderFingerprint` :54 (`checkpoints` :59), `compareFingerprints` :101, `withinTolerance()` :96. Namespace `TestUtils` :43. |
| `harmonic_cloud_perf_test.cpp` gate shape | `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp` | `kRegressionFactor = 1.5` :76, `kReferenceNsPerBlock ≈ 53,333` :80 (**REPORTED only**, :78-79), `kMaxAdmissibleBaselineNsPerBlock = kReferenceNsPerBlock / kRegressionFactor` :101, BASELINE PROVENANCE block :103-122, `kAutomatedBaselineNsPerBlock = 26000.0` :140, the two `static_assert`s :142 and :149, configuration guards :157-161. |

### 0.1 Verified non-reuse (traps this plan must not fall into)

1. **`crossfade_utils.h`** (`equalPowerGains` :50, `crossfadeIncrement` :89) is **not used**. FR-041's state blend is magnitude-linear (two coherent spectra sharing partial slots, not two decorrelated signals) and FR-047's absorption ramp advances on `chunkSeconds`, not per sample.
2. **`harmonic_types.h:21`** declares `Krate::DSP::kMaxPartials = 96` at **namespace** scope. `SpectralState::kStatePartials = 64` is therefore class-scoped, and no new namespace-scope partial-count constant is introduced (C-8).
3. **`SpectralMorphFilter`** allocates `std::vector`s in `prepare` and adds FFT-size latency — concept reference only.
4. **There are TWO `calculateSpectralFlatness` functions, and the sweep must name both** (corrected this
   session — an earlier draft named only the first and drew an unsupported conclusion):
   - `Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness(const float*, size_t, float)`
     (`tests/test_helpers/signal_metrics.h:326`, namespaces at :30-32 and :60) takes a **time-domain**
     signal, applies its own Hann window and caps the FFT at 4096 (:337). **Unusable** for SC-004 m.3,
     which needs a 65536-point transform.
   - `Krate::DSP::calculateSpectralFlatness(const float* magnitudes, size_t numBins)`
     (`dsp/include/krate/dsp/primitives/spectral_utils.h:335`, namespace at :31-32) is
     **magnitude-domain with no FFT-size cap** — exactly the shape SC-004 m.3 has after its bin-wise
     magnitude average. It is **not** adopted, for one stated reason: it skips bins with
     `magnitudes[i] <= 1e-10f` and divides the arithmetic mean by `validBins` rather than `numBins`
     (:345-357), so on a near-silent high-bin region its denominator shrinks with the signal and the
     metric stops being comparable across entropy settings — which is the one comparison SC-004 m.3
     makes. SC-004 m.3 therefore computes `exp(mean log m)/mean m` inline over the **fixed** bin range
     `[2, 16384)` with no skipping, and the test records this two-function sweep in a comment.
   - Consequence for the render TU: if it ever includes both headers, the two overloads are in scope
     under different namespaces and **every call must be qualified**.
5. **`allocation_detector.h`** is inert on its own (the global operator replacements are commented out, :108-136). `allocation_operator_overrides.h` is already linked into `dsp_systems_tests` (via `unit/systems/selectable_oscillator_test.cpp:388`) and `dsp_processors_tests` (via `unit/processors/brownian_drift_test.cpp:28`). **No new test file in this phase may include it.**
6. **`HarmonicCloud` has no phase setter.** `getPartialSinState` :819 / `getPartialCosState` :823 are read-only; the only writer outside the kernel is the private `redrawPhases()` :970, reachable only from a quiescent `noteOn()` :593-595. C-5's ratio-domain decoherence is therefore forced, not chosen.
7. **`std::exp2` / `std::log2` are not `constexpr` in C++20.** Every derived transcendental constant uses `detail::constexprExp` / `detail::constexprLn` (§4.1) and is pinned by a runtime equivalence test.

---

## 1. Blocking prerequisites (task T0 — before any header is written)

These are ordering constraints, not suggestions. Two of them are unsatisfiable after the fact.

- **T0.1 — capture the pre-amendment `HarmonicCloud` fingerprints.** SC-014 clause 1 compares the
  post-amendment render against the *pre*-amendment build, which will not exist at test-run time.
  On the current `main` build, before **any** edit to `harmonic_cloud.h` or `core/random.h`, run a
  throwaway TU that captures `TestUtils::RenderFingerprint` over the documented grid
  (≥ 3 Richness × `{gravity 0, ±1}` × `{B 0, 0.05}` × `{tilt 0, ±12}` × `{mutation 0, 1}` ×
  `{drift 0, max}` = 3·3·2·3·2·2 = **216 cells**) and check the values in as
  `kPreAmendmentFingerprints[216]` with a provenance block naming commit, machine, build config and
  date, in the shape of `harmonic_cloud_perf_test.cpp:104-119`.
- **T0.2 — the SC-010 / RA-3 spike.** SC-010 requires the target-active cost to be spike-measured on
  the Phase 2 baseline machine *before this plan is written* (spec.md:1764-1772). **It has not been
  run.** Rather than leave the phase blocked on an undecided question, this plan takes a position:
  1. **The prerequisite's timing is amended, and the amendment is recorded, not assumed.** Measurement
     moves from "before the plan is written" to **T7**, with machine, build config, trial shape and
     date written into SC-010 at that point in the shape of `harmonic_cloud_perf_test.cpp:104-119`.
     This is spec text, so it is a spec edit: it is listed as **RA-4** and lands in T9 alongside
     RA-1/RA-2/RA-3. The plan does not silently ignore the prerequisite.
  2. **The response to a clause-1 miss is pre-decided** (§8): levers 1–3 are adopted from the start,
     lever 4 (`centsToPitchRatioFast`) and the new **lever 5** (entropy OU control interval 32 → 64
     samples) are pre-designed with their costs and their consequences. `kMorphReferenceNsPerBlock`
     is **not** raised — SC-010 forbids it and §8 keeps that prohibition.
  So the phase cannot stall on a measurement nobody has taken; it can only produce a recorded finding.
  The derived cost model in §8 stands as the falsifiable expectation the measurement is entered with.
- **T0.3 — roadmap edits RA-1/RA-2/RA-3.** Still outstanding per the spec's own review notes
  (spec.md:2283-2288). They are documentation edits to `specs/Seraphis-roadmap.md` and are listed
  in §10 as the last task of the phase, not the first, so a failed measurement does not leave a
  wrong number in the roadmap.

---

## 2. Layer 0 additions

Three additions to existing Layer 0 headers. The first two are ODR-swept clear (spec.md:2206-2208);
the third (§2.3) is a symbol the earlier draft of this plan introduced without a sweep and is swept
here (§7).

### 2.1 `core/random.h` — `deriveStreamSeed` (FR-006, RA-1 second edit)

```cpp
#include <cstddef>   // REQUIRED and new: the file currently includes only <cstdint> (:17).
                     // MSVC and libstdc++ supply <cstddef> transitively, so a green Windows
                     // build cannot catch its absence. node tools/check-portability.js can.

/// @brief Derive a guaranteed-non-zero per-stream seed from a base seed and a salt.
/// lowbias32 finaliser. The non-zero substitution is load-bearing: Xorshift32::seed()
/// silently replaces 0 with its own default (:72-74), so two lanes hashing to 0 would
/// COLLAPSE ONTO ONE STREAM.
[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base,
                                                       std::size_t salt) noexcept {
    std::uint32_t h = base ^ (static_cast<std::uint32_t>(salt + 1u) * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return (h != 0u) ? h : 0x2545F491u;
}
```

The body is character-for-character the body verified at `harmonic_cloud.h:653-659`.
`HarmonicCloud::deriveSeed` (`:651-660`) is then rewritten as:

```cpp
[[nodiscard]] static constexpr std::uint32_t deriveSeed(std::uint32_t base,
                                                        std::size_t salt) noexcept {
    return deriveStreamSeed(base, salt);   // FR-006: the hash moved to Layer 0.
}
```

Signature, `public static constexpr` linkage and the doc comment are unchanged, so every Phase 2
call site and every Phase 2 test compiles and behaves identically. Covered by SC-014 clause 1 and
by SC-012's `deriveSeed == deriveStreamSeed` clause.

### 2.2 `core/pitch_utils.h` — `centsToPitchRatio` (FR-072)

```cpp
/// @brief Convert a cent offset to a pitch ratio, accurate over the whole float range.
/// Deliberately NOT named centsToRatio: that identifier is a local variable at
/// processors/multi_pitch_detector.h:96. Deliberately NOT HarmonicCloud's
/// detail::centsToDriftRatio (systems/harmonic_cloud.h:105): that is Layer 3 and is
/// documented accurate only on [-50, +50] cents (:104).
[[nodiscard]] inline float centsToPitchRatio(float cents) noexcept {
    constexpr float kInvCentsPerOctave = 1.0f / 1200.0f;
    return std::exp2(cents * kInvCentsPerOctave);
}
```

**Deviation D2 (§11):** FR-072 specifies the body as `semitonesToRatio(cents / 100.0f)`, i.e. one
`std::pow(2.0f, s/12.0f)` (`pitch_utils.h:25`). `std::exp2(c/1200)` is the same real number,
satisfies FR-072's stated requirement ("accurate over the full `float` range rather than a narrow
window"), and is 2–4× cheaper — which SC-010's 16,000 ns/block budget needs (§8). The equivalence
to the specified definition is a **checked property**, not a claim: `pitch_utils_test.cpp` gains a
case asserting `|centsToPitchRatio(c) − semitonesToRatio(c/100)| ≤ 1e-6` relative over
`c ∈ {−4800, −1200, −100, −11, −1, 0, 1, 11, 100, 1200, 4800}`, plus `centsToPitchRatio(0) == 1.0f`
bitwise.

### 2.3 `core/db_utils.h` — `detail::kLn2` (one shared definition)

Every `detail::constexprExp`-derived constant in this phase is of the form
`constexprExp(x · ln 2)`, so three headers need `ln 2` at compile time: `entropy_processor.h`
(§4.1, `kMinRatioSpacingFactor`), `spectral_morph_engine.h` (§5.1, `kFillSpacingFactor`,
`kMaxOutputRatio`, `kOutputCentsSpan`) and `harmonic_cloud.h` (§6.1, `kTargetRatioRelEpsilon`).

**Verified this session by sweep of `dsp/include/`: there is no namespace-scope `kLn2` in
`Krate::DSP`.** The only occurrences are two **function-local** `constexpr float kLn2 =
0.693147181f;` inside `detail::constexprLn` (`db_utils.h:84`) and `detail::constexprExp`
(`db_utils.h:130`), plus a class-scoped `TanhADAA::kLn2` (`tanh_adaa.h:171`). An earlier draft of
this plan declared it as "a file-local `constexpr float kLn2`" in `entropy_processor.h` and then
used the bare name in two other headers. That is wrong twice over: `harmonic_cloud.h` does not (and
must not) include `entropy_processor.h`, so it would not compile at all; and repeating the
declaration in more than one header puts two definitions of the same namespace-scope name into any
TU that includes both — which the SC-011 and SC-014 TUs do.

The fix is **one** definition, in the Layer 0 header all three consumers already include:

```cpp
// dsp/include/krate/dsp/core/db_utils.h, inside namespace detail (opens :39, closes :179),
// immediately beside kLn10 (:60) and kInvLn10 (:64) and in the same linkage form:

/// Natural log of 2. Shared by every constexprExp(x * ln 2) constant.
constexpr float kLn2 = 0.693147181f;
```

`constexpr` at namespace scope is implicitly internal-linkage, so this is the same one-definition-
per-TU form `kLn10`/`kInvLn10` already have and carries no ODR hazard. The two **function-local**
copies at `:84` and `:130` are **deleted** in the same edit and the enclosing functions use the
shared `kLn2` unqualified (both are already inside `namespace detail`). Deleting rather than
shadowing is deliberate: leaving a local of the same name would be a `-Wshadow`/clang-tidy finding
in a repo that requires zero warnings.

The literal is unchanged, so `constexprLn`/`constexprExp` are bit-identical before and after:
`0.693147181f` and the draft's `0.6931471805599453f` both round to the same `float`
(0.693147182464599609375). §4.1, §5.1 and §6.1 all refer to it as **`detail::kLn2`**.
Swept in §7.

---

## 3. `SpectralState` — Layer 2, plain data

**Header:** `dsp/include/krate/dsp/processors/spectral_state.h`
**Includes:** `<krate/dsp/core/db_utils.h>` (the `detail::isNaN`/`isInf` definition site — FR-007
forbids reaching for the Layer 3 use site at `harmonic_cloud.h:342-344`), `<array>`, `<cstddef>`,
`<cstdint>`, `<cmath>`, `<cstring>`, **`<type_traits>`**. Layer 0 + stdlib only.

`<type_traits>` is required by §3.1's `static_assert(std::is_trivially_copyable_v<SpectralState>)`
and is listed explicitly for the same reason `<cstddef>` is listed in §2.1: MSVC and libstdc++ supply
it transitively (through `<array>`/`<bit>`/`<limits>`), so a green Windows build cannot catch its
absence. `node tools/check-portability.js` is run on this header as part of **T2**, not only at T8.

### 3.1 The struct (FR-011)

```cpp
struct SpectralState {
    static constexpr std::size_t kStatePartials  = 64;   // == HarmonicCloud::kMaxPartials (:133), C-8
    static constexpr std::size_t kStateNameBytes = 16;

    static constexpr float kMinStateRatio        = 0.5f;    // FR-012: finite log-ratio span
    static constexpr float kMaxStateRatio        = 128.0f;
    static constexpr float kMinStateTiltDbPerOct = -12.0f;  // spectral_tilt.h:98-101 convention
    static constexpr float kMaxStateTiltDbPerOct = 12.0f;
    static constexpr float kMaxStateInharmonicity = 0.1f;   // HarmonicCloud::kMaxInharmonicity :186

    std::array<float, kStatePartials> ratios{};
    std::array<float, kStatePartials> amplitudes{};
    std::array<char,  kStateNameBytes> name{};
    float tiltDbPerOct  = 0.0f;
    float inharmonicity = 0.0f;
    int   numPartials   = 0;
};

static_assert(std::is_trivially_copyable_v<SpectralState>);
static_assert(SpectralState::kStatePartials == 64);
```

Aggregate-initialisable in C++20 despite the default member initialisers, and trivially copyable
(NSDMIs affect trivial *default construction*, not trivial copyability). Default value is valid,
finite, silent and anonymous. **No member functions** — every operation is a free function, which
is what makes Phase 9's deferred authoring mutators (C-9) pure additions.

### 3.2 Free functions

```cpp
[[nodiscard]] bool isValidSpectralState(const SpectralState& s) noexcept;   // FR-012
void            normalizeSpectralState(SpectralState& s) noexcept;          // FR-014

enum class SpectralStateId : std::uint8_t { SineStack = 0, Bell, Choir, Glass, Breath };
inline constexpr std::size_t kSpectralStateCount = 5;
[[nodiscard]] SpectralState makeFactoryState(SpectralStateId id) noexcept;  // FR-021

inline constexpr std::uint8_t kSpectralStateFormatVersion = 1;              // FR-032
inline constexpr std::size_t  kSpectralStateBytes = 1 + 4 + 4 + 4 + 256 + 256 + 16;  // = 541
[[nodiscard]] std::size_t serializeSpectralState(const SpectralState& s, std::byte* dest,
                                                 std::size_t capacity) noexcept;      // FR-031
[[nodiscard]] bool deserializeSpectralState(const std::byte* src, std::size_t size,
                                            SpectralState& out) noexcept;             // FR-031
```

`isValidSpectralState` checks, in order and with early exit:
`numPartials ∈ [0, 64]`; for `i < numPartials`: `!isNaN && !isInf`, `ratios[i] ∈ [kMinStateRatio,
kMaxStateRatio]`, `ratios[i] > ratios[i-1]` (strict), `amplitudes[i]` finite in `[0, 1]`;
`name` contains at least one `'\0'` and no byte before it is `< 0x20` or `== 0x7F`;
`tiltDbPerOct` finite in `[-12, +12]`; `inharmonicity` finite in `[0, 0.1]`.
Entries at `i ≥ numPartials` are not examined (FR-012).

`normalizeSpectralState` is the `harmonic_snapshot.h:99-107` idiom over `[0, numPartials)`:
accumulate `sumSquares`, and only `if (sumSquares > 0.0f)` multiply by `1/sqrt(sumSquares)`.
All-zero and `numPartials == 0` states are left untouched (no NaN).

### 3.3 Serialization (FR-031 – FR-033)

Layout, little-endian, fixed order, total **541** bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `kSpectralStateFormatVersion` |
| 1 | 4 | `numPartials` (`std::int32_t`) |
| 5 | 4 | `tiltDbPerOct` (float bits) |
| 9 | 4 | `inharmonicity` (float bits) |
| 13 | 256 | `ratios[0..63]` |
| 269 | 256 | `amplitudes[0..63]` |
| 525 | 16 | `name[0..15]` verbatim |

`static_assert(kSpectralStateBytes == 541)`. Every scalar is moved with `std::memcpy` of its bit
pattern — no `reinterpret_cast`, no text formatting, no arithmetic on `std::byte` (which has
implementation-defined signedness hazards; cast through `unsigned char` where a byte value is
needed). Both functions are `noexcept`, allocation-free and RT-safe.

`serializeSpectralState` returns **0** and writes nothing if `dest == nullptr`,
`capacity < kSpectralStateBytes`, or `!isValidSpectralState(s)`.
`deserializeSpectralState` returns **false** and leaves `out` untouched if `src == nullptr`,
`size < kSpectralStateBytes`, `src[0] != kSpectralStateFormatVersion`, or the decoded payload fails
`isValidSpectralState`. It therefore decodes into a **local** `SpectralState` and only copies into
`out` on success.

Round-trip is exact (`==` on float bits) because the bytes are stored values, not arithmetic
results — FR-033. SC-007's FNV digest is over the **byte stream**, which `dsp/CLAUDE.md` explicitly
permits, and the test must label it as such.

### 3.4 Factory states (FR-021 – FR-023) — constants pinned, arithmetic done

`makeFactoryState` is deterministic and consumes **no RNG** (FR-023): every value is a closed-form
function of the partial index. Each state is built by (1) writing `ratios[i]` for `i < numPartials`
from its ratio law, (2) writing the **FR-041 geometric continuation** into `ratios[i]` for
`i ≥ numPartials` (§5.4), (3) writing `amplitudes[i]` from its amplitude law for `i < numPartials`
and `0.0f` above, (4) `normalizeSpectralState`, (5) writing the NUL-padded ASCII label.

| Id | `numPartials` | `ratio_n` (1-based n) | `amp_n` before normalisation |
|---|---|---|---|
| `SineStack` | 64 | `n` | `n^(−1)` |
| `Bell` | 24 | `n · sqrt(1 + kBellB·n²)`, **`kBellB = 0.04f`** | `n^(−1.4)` |
| `Choir` | 64 | `n` | `n^(−0.8) · (kChoirFloor + Σ_{k=0..2} g_k · exp(−(n − c_k)² / (2σ_k²)))` |
| `Glass` | 64 | `n · (1 + kGlassStretch·n)`, **`kGlassStretch = 0.004f`** | `n^(−0.5) · (n even ? kGlassEvenAtten : 1)` |
| `Breath` | 64 | `n` | `n^(−0.25) · (1 − kBreathLowDepth · exp(−(n − 1)/kBreathLowScale))` |

Remaining constants, **pinned here** (FR-022 leaves them to the plan but binds them to SC-008):

```
kChoirFloor      = 0.12f
kChoirCentres[3] = { 3.0f,  8.0f, 17.0f }     // formant peaks in the partial-index domain
kChoirSigmas[3]  = { 1.2f,  2.0f,  3.0f }
kChoirGains[3]   = { 1.0f,  0.8f,  0.5f }
kGlassEvenAtten  = 0.35f
kBreathLowDepth  = 0.9f
kBreathLowScale  = 3.0f
```

**Computed this session** (double precision, `scratchpad/factory.js`), against SC-008's a-priori
`kMinFactoryStateDistance = 0.4` and FR-022's `ρ ≤ 0.92` constraint on the three `ratio_n = n`
states:

| Pair | `d` | amplitude term | ratio term | `ρ` |
|---|---|---|---|---|
| SineStack / Bell | 1.5723 | 0.2629 | 1.3094 | 0.9654 |
| **SineStack / Choir** | **0.5258** | 0.5258 | 0.0000 | **0.8618** |
| SineStack / Glass | 0.7406 | 0.5674 | 0.1732 | 0.8390 |
| SineStack / Breath | 1.0932 | 1.0932 | 0.0000 | **0.4025** |
| Bell / Choir | 2.0245 | 0.7151 | 1.3094 | 0.7443 |
| Bell / Glass | 1.9742 | 0.7347 | 1.2395 | 0.7301 |
| Bell / Breath | 2.5841 | 1.2747 | 1.3094 | 0.1876 |
| Choir / Glass | 0.8824 | 0.7091 | 0.1732 | 0.7486 |
| **Choir / Breath** | 1.0538 | 1.0538 | 0.0000 | **0.4447** |
| Glass / Breath | 0.9517 | 0.7785 | 0.1732 | 0.6970 |

All ten clear 0.4. The three `ratio_n = n` pairs are `ρ = 0.8618 / 0.4025 / 0.4447`, all ≤ 0.92 —
i.e. the Choir formant bumps and the Breath low-partial attenuation carry the distinctness, exactly
as FR-022 requires. **`kMeasuredClosestPairDistance = 0.5258` (SineStack / Choir), ±10 % band**, is
the SC-008 clause 2 regression pin.

Validity, also computed: all five strictly increasing over all 64 slots; `max_i ratios[i]` =
SineStack 64.00, **Bell 240.32**, Choir 64.00, Glass 80.38, Breath 64.00; minimum adjacent spacing
= 27.264 / 28.000 / 27.264 / 32.79 / 27.264 cents. Bell's authored region tops out at
`ratio_24 = 24·sqrt(1 + 0.04·576) = 117.67 ≤ 128` ✔ (`ratio_23 = 108.27`), so the state is
FR-012-valid; its 240.32 lies entirely in the **fill** region (`i ≥ numPartials`), which FR-012 does
not constrain and which FR-041 bounds at `kMaxOutputRatio = 360.37` (§5.1). See deviation **D6** (§11) —
SC-008 clause 1's `max_i ratios[i] ≤ kMaxStateRatio` must be scoped to `i < numPartials` or it is
unsatisfiable for Bell by construction.

---

## 4. `EntropyProcessor` — Layer 2

**Header:** `dsp/include/krate/dsp/processors/entropy_processor.h`
**Includes:** `<krate/dsp/core/db_utils.h>` (for `detail::isNaN`/`isInf`, `detail::constexprExp`
and `detail::kLn2` — §2.3), `<krate/dsp/core/pitch_utils.h>`, `<krate/dsp/core/random.h>`,
`<krate/dsp/primitives/smoother.h>` (for `calculateOnePolCoefficient` :91, `kCompletionThreshold`
:55 and `detail::flushDenormal`), **`<krate/dsp/processors/brownian_drift.h>`** — *OU coefficient
constants only (`kTauMin`, `kTauMax`, `kInternalStd`, `kControlRateInterval`,
`kDriftOutputSmoothMs`); intra-Layer-2, legal, and required at compile time by §4.1 and §4.4, which
derive `tau` through `BrownianDrift`'s own smoothness mapping rather than writing `3.0`/`8.0`
literals* — and `<krate/dsp/processors/spectral_state.h>` (intra-Layer-2, the established pattern at
`harmonic_snapshot.h:15` and `modal_resonator_bank.h:14-15`), plus
`<algorithm> <array> <cmath> <cstddef> <cstdint>`.

Including `brownian_drift.h` also pulls in `modulation_source.h`. `EntropyProcessor` **must not**
derive from `ModulationSource`: it is a spectral transform with an array-in/array-out contract, not
a scalar modulation source, and giving it that base would put it in `ModulationEngine`'s routing
surface where it does not belong.

### 4.1 Constants (all class-scoped)

```cpp
class EntropyProcessor {
public:
    static constexpr std::size_t kPartials = SpectralState::kStatePartials;   // 64

    // FR-071 stage ramps
    static constexpr float kStage1Lo = 0.00f, kStage1Hi = 0.35f;   // amplitude jitter
    static constexpr float kStage2Lo = 0.25f, kStage2Hi = 0.60f;   // phase decoherence
    static constexpr float kStage3Lo = 0.50f, kStage3Hi = 0.85f;   // ratio scatter
    static constexpr float kStage4Lo = 0.75f, kStage4Hi = 1.00f;   // death / rebirth

    // FR-072 magnitudes
    static constexpr float kMaxAmpJitter        = 0.5f;
    static constexpr float kMaxDecoherenceCents = 4.0f;
    static constexpr float kMaxScatterCents     = 7.0f;

    // FR-046 spacing floor — OWNED HERE so FR-074's static_assert and the engine's
    // repair share exactly one definition (the engine aliases these).
    static constexpr float kMinRatioSpacingCents = 24.0f;
    static constexpr float kMinRatioSpacingLog2  = kMinRatioSpacingCents / 1200.0f;   // 0.02
    static constexpr float kMinRatioSpacingFactor =
        detail::constexprExp(kMinRatioSpacingLog2 * detail::kLn2);   // 1.0139595

    static_assert(2.0f * (kMaxDecoherenceCents + kMaxScatterCents) < kMinRatioSpacingCents,
                  "FR-074: two neighbours must not be able to close the FR-046 spacing floor. "
                  "Any increase to the cent constants must be paid for by raising "
                  "kMinRatioSpacingCents — never by deleting this assert.");

    // Transcribed from BrownianDrift's PRIVATE section (brownian_drift.h:221 opens
    // private:, kWalkLimit :226, kDenormalFloor :228) — they cannot be named from
    // here. Same precedent and same values as HarmonicCloud::kDriftWalkLimit /
    // kDriftDenormalFloor (harmonic_cloud.h:156-157). See §0.
    static constexpr float kWalkLimit     = 4.0f;
    static constexpr float kDenormalFloor = 1e-20f;

    // FR-072 OU bank configuration (tau expressed through BrownianDrift's own
    // smoothness mapping so a lane-batched bank stays bit-comparable to a reference
    // BrownianDrift; see §4.4)
    static constexpr float kAmpJitterSmoothness   = 0.09396f;   // tau = 3.0 s
    static constexpr float kDecoherenceSmoothness = 0.26174f;   // tau = 8.0 s

    // Output smoothing times, in OnePoleSmoother's OWN convention (time to 99 %,
    // tau = ms/5000 s — smoother.h:86-91). See deviation D12.
    //   decoherence: BrownianDrift's own value, so this bank stays bit-comparable
    //                to a reference BrownianDrift (the §9 equivalence test).
    //   amp jitter:  5 x that value, i.e. tau = 0.150 s EXACTLY, which is the time
    //                constant FR-044's amplitude table row is derived with
    //                (spec.md:817-819). At 150 ms the true tau is 0.030 s and the
    //                FR-044 amplitude budget cannot be met (§5.1).
    static constexpr float kEntropyCentsSmoothMs = BrownianDrift::kDriftOutputSmoothMs;   // 150
    static constexpr float kEntropyAmpSmoothMs   = 5.0f * BrownianDrift::kDriftOutputSmoothMs; // 750

    /// Fractional approach of a OnePoleSmoother over `chunkSamples` samples, in the
    /// smoother's own convention (smoother.h:91). Public and constexpr because
    /// SpectralMorphEngine's FR-044 static_asserts (§5.1) are expressed through it
    /// and must not re-derive the exponent by hand.
    [[nodiscard]] static constexpr float onePoleChunkStep(float smoothTimeMs,
                                                          float chunkSamples,
                                                          float sampleRate) noexcept {
        return 1.0f - detail::constexprExp(-5000.0f * chunkSamples
                                           / (smoothTimeMs * sampleRate));
    }

    // FR-073 lifecycle
    static constexpr float kMaxDeathRatePerSecond = 0.05f;
    static constexpr float kMinDeathFadeSec   = 0.5f, kMaxDeathFadeSec   = 2.0f;
    static constexpr float kMinDeadDwellSec   = 0.2f, kMaxDeadDwellSec   = 1.0f;
    static constexpr float kMinRebirthFadeSec = 0.5f, kMaxRebirthFadeSec = 2.0f;

    enum class LifePhase : std::uint8_t { Alive = 0, Dying, Dead, Reborn };
```

`detail::kLn2` is the **single shared definition added to `core/db_utils.h` in §2.3** — not a
file-local constant here. Every transcendental constant in this phase is formed with
`detail::constexprExp` / `detail::constexprLn` (`db_utils.h:121`, `:80`) because `std::exp2`/`std::log2`
are not `constexpr` in C++20, and each is pinned by a runtime equivalence assertion
(§9, `EntropyProcessor_ConstantsMatchTranscendentals`) at `1e-6` relative.

### 4.2 Public API (FR-071, FR-075, FR-008)

```cpp
    void prepare(double sampleRate) noexcept;      // NOT RT-safe by contract; floors rate at 1 Hz
    void reset() noexcept;                         // configuration-time (FR-005): may step output
    void setSeed(std::uint32_t seed) noexcept;     // configuration-time (FR-006): may step output
    void setEntropy(float e) noexcept;             // clamped [0,1]; rejects NaN/Inf (FR-007)

    /// FR-075. Advances every walk and lifecycle by numSamples, then applies all four
    /// stages IN PLACE.
    ///
    /// THE ADVANCE IS UNCONDITIONAL IN numSamples AND INDEPENDENT OF count. Internal
    /// lane state after N advanced samples is a function of N alone — not of how N was
    /// partitioned into chunks, and not of whether the caller had any partials to
    /// perturb on a given chunk. Both SC-012 (determinism) and SC-013 (chunk-length
    /// invariance) rest on that, and `count == 0` is a REACHABLE configuration: the
    /// engine passes `max(A.numPartials, B.numPartials)`, and FR-012 permits
    /// `numPartials == 0` (a default-constructed SpectralState, FR-011).
    ///
    /// numSamples == 0 applies the stages WITHOUT advancing anything — which is what
    /// SpectralMorphEngine::prepare()/reset() use to populate their output arrays with
    /// no advance (FR-005, SC-002 clause 5). Null pointers or count == 0 skip the
    /// stage application only; the advance still happens.
    void processChunk(float* ratios, float* amplitudes,
                      std::size_t count, std::size_t numSamples) noexcept;

    // FR-008 introspection — public contract, cheap, not #ifdef scaffolding
    [[nodiscard]] float          getEntropy()                 const noexcept;
    [[nodiscard]] float          getStageWeight(int stage)    const noexcept;  // stage 1..4
    [[nodiscard]] float          getAmpJitterFactor(std::size_t i)  const noexcept;
    [[nodiscard]] float          getDecoherenceCents(std::size_t i) const noexcept;
    [[nodiscard]] float          getAppliedScatterCents(std::size_t i) const noexcept; // w3*7*s_i
    [[nodiscard]] float          getRawScatterDraw(std::size_t i)      const noexcept; // s_i
    [[nodiscard]] std::uint32_t  getScatterRedrawCount(std::size_t i)  const noexcept;
    [[nodiscard]] LifePhase      getLifePhase(std::size_t i)   const noexcept;
    [[nodiscard]] float          getLifeAmplitudeFactor(std::size_t i) const noexcept; // L_i
    [[nodiscard]] bool           stateFinite()                 const noexcept;
```

Out-of-range indices return `0.0f` / `LifePhase::Alive` rather than reading past the array.
The applied-vs-raw scatter split is load-bearing (FR-008): SC-005's "still exactly 0" clauses read
`getAppliedScatterCents`, SC-006's redraw clause reads `getRawScatterDraw` + `getScatterRedrawCount`.

### 4.3 State layout

```cpp
private:
    struct LaneRng { Xorshift32 rng{1}; };   // harmonic_cloud.h:925-927: Xorshift32's only
                                            // ctor is explicit, so std::array<Xorshift32,N>{}
                                            // is ill-formed.
    struct OuBank {                          // mirrors HarmonicCloud::DriftLanes :929-952
        alignas(32) std::array<float, kPartials> walk{};
        alignas(32) std::array<float, kPartials> smoothCur{};
        alignas(32) std::array<float, kPartials> smoothTgt{};
        std::array<LaneRng, kPartials> rng{};
        float a = 0.0f, g = 0.0f;
        float smoothCoeff = 0.0f;            // PER BANK — the two banks smooth at
                                             // different times (§4.1, deviation D12)
        int   samplesUntilControl = 0;       // SHARED across the bank's 64 lanes
        int   cachedPowN = 0;  float cachedPowValue = 0.0f;   // powf memo, :950-951
    };

    OuBank jitter_;        // bank (a), tau 3 s
    OuBank decohere_;      // bank (b), tau 8 s

    alignas(32) std::array<float, kPartials>         scatterDraw_{};    // s_i ∈ [-1,+1]
    std::array<std::uint32_t, kPartials>             scatterRedraws_{};
    std::array<LaneRng, kPartials>                   lifeRng_{};
    std::array<LifePhase, kPartials>                 phase_{};
    alignas(32) std::array<float, kPartials>         life_{};           // L_i
    alignas(32) std::array<float, kPartials>         phaseTimer_{};     // seconds remaining
    alignas(32) std::array<float, kPartials>         phaseLength_{};    // seconds, for the ramp

    float entropy_ = 0.0f;
    float w1_ = 0.0f, w2_ = 0.0f, w3_ = 0.0f, w4_ = 0.0f;
    double sampleRate_ = 44100.0;
    float  invSampleRate_ = 1.0f / 44100.0f;
    std::uint32_t configuredSeed_ = kDefaultEntropySeed;
```

Fixed-size, ~5 KB. No allocation anywhere; `prepare` is the only non-RT method.

### 4.4 OU banks — exact discretisation, lane-batched

Both banks are the **lane-batched** form (`HarmonicCloud::DriftLanes`), not 128 `BrownianDrift`
objects: the cloud already proved the batched form equivalent, and 128 objects would carry 128
`OnePoleSmoother`s and 128 counters. Per bank:

```cpp
// EVERY INTERMEDIATE IS double; only the final a / g are narrowed to float.
// This is a literal transcription of BrownianDrift::updateCoefficients
// (brownian_drift.h:230-240) via its Phase 2 copy HarmonicCloud::updateDriftCoefficients
// (harmonic_cloud.h:1519-1531), INCLUDING the `variance > 0.0` guard.
const double controlDt = static_cast<double>(BrownianDrift::kControlRateInterval)   // 32 samples
                       / sampleRate_;
const double tau = static_cast<double>(BrownianDrift::kTauMin)
                 + static_cast<double>(smoothness)
                     * (static_cast<double>(BrownianDrift::kTauMax)
                        - static_cast<double>(BrownianDrift::kTauMin));
const double a = std::exp(-controlDt / tau);
bank.a = static_cast<float>(a);
const double variance = 1.0 - (a * a);
bank.g = static_cast<float>(static_cast<double>(BrownianDrift::kInternalStd)
                            * std::sqrt(variance > 0.0 ? variance : 0.0));
bank.smoothCoeff = calculateOnePolCoefficient(smoothMs, static_cast<float>(sampleRate_));
```

**The `double` intermediates are not a stylistic preference — they are what makes the §9 equivalence
test achievable in the first place.** `harmonic_cloud.h:1510-1515` records the reason verbatim, and
it applies here unchanged: *"Computing tau/a/g in float instead would move the coefficients in the
last bits and put `HarmonicCloud_DriftLaneMatchesBrownianDrift` near its 1e-5 tolerance for no
reason: the walk is an AR(1) recursion, so a coefficient difference is re-applied at every one of
the 90,000 control steps that test drives."* `EntropyProcessor_OuBankMatchesBrownianDrift` drives
≥ 60 s at 48 kHz — 90,000 control steps at `kControlRateInterval = 32` — i.e. the identical exposure.
Computing these in `float` makes that test a coin flip across MSVC / GCC / Clang.

**`tau` is derived through the smoothness mapping, not written as 3.0 / 8.0 directly.** That is
what keeps a lane bit-comparable against a reference `BrownianDrift` configured with
`setSmoothness(kDecoherenceSmoothness)`, which is how the equivalence test (§9) is written; writing
`tau = 8.0f` gives 7.99999 through the mapping and the two would diverge in the last bits. The test
feeds the reference **the same class constant** (`kDecoherenceSmoothness`), never a re-typed literal,
so both sides traverse the identical mapping.

Control step (`advanceControlStepAllLanes`, transcribing `brownian_drift.h:253-270`):

```
z0,z1,z2 = three SEQUENCED nextFloat() draws        // operands of + are unsequenced in C++;
z        = z0 + z1 + z2                             // a different draw order is a different stream
x        = a * walk[i] + g * z                      // mean is 0 for both banks
x        = clamp(x, -kWalkLimit, +kWalkLimit)       // EntropyProcessor::kWalkLimit = 4.0f;
                                                    // transcribed from the PRIVATE
                                                    // brownian_drift.h:226 (§0, §4.1)
if (|x| < kDenormalFloor) x = 0                     // EntropyProcessor::kDenormalFloor
                                                    // = 1e-20f, from private :228
walk[i]      = x
smoothTgt[i] = clamp(1.0f * x, -1, +1)              // depth is pinned at 1.0 (FR-072 table)
```

Output smoother advance (`advanceSmootherAllLanes`) is the **literal transcription** of
`OnePoleSmoother::advanceSamples` used at `harmonic_cloud.h:1620-1641`, including all three parts
the naive exponential identity omits: the `isComplete` early-`continue` (:1632-1633), the
`detail::flushDenormal` (:1636), and the post-advance snap below `kCompletionThreshold` (:1637-1639).
`coeff^N` is formed by the same `std::pow(coefficient, static_cast<float>(numSamples))` expression
(`smoother.h:248`) and memoised on `numSamples` (`cachedPowN`) — never from a precomputed `coeff^k`
table, for the strength-reduction reason documented at `harmonic_cloud.h:1582-1594`.
The coefficient is **per bank** (`bank.smoothCoeff`), because the two banks smooth at different
times: `kEntropyCentsSmoothMs = 150` for decoherence and `kEntropyAmpSmoothMs = 750` for amplitude
jitter (§4.1, deviation **D12**). Only the decoherence bank is therefore configured identically to
a stock `BrownianDrift`, which is why §9's equivalence test is written against **that** bank — the
lane-batching code under test is shared by both, so one bank exercises it fully.

Bank advance is `advanceDriftLanes`'s structure (`harmonic_cloud.h:1655-1667`): a shared
`samplesUntilControl` counter, so lane state after N advanced samples is a function of N alone and
not of how N was partitioned.

**Per-lane seeds** (FR-006), off the Layer 0 `deriveStreamSeed`, with four disjoint salt ranges over
one base seed — which is exactly the `4 × 64 = 256`-salt cross product SC-012 asserts:

| Stream | Salt |
|---|---|
| bank (a), amplitude jitter | `i` |
| bank (b), decoherence | `kPartials + i` |
| static scatter `s_i` | `2·kPartials + i` |
| death/rebirth lifecycle | `3·kPartials + i` |

### 4.5 Stage weights (FR-071)

`w_k(e) = clamp((e − lo_k) / (hi_k − lo_k), 0, 1)`, recomputed in `setEntropy` (config rate), stored
in `w1_..w4_`. Each is continuous, monotone non-decreasing, and `w_k(0) = 0` — including `w_1`,
whose interval starts at 0 (`(0 − 0)/0.35 = 0`).

### 4.6 `processChunk` — fixed order

```cpp
void processChunk(float* r, float* a, std::size_t count, std::size_t numSamples) noexcept {
    // ADVANCE FIRST, AND UNCONDITIONALLY IN count. See the FR-075 contract in §4.2:
    // the lane/lifecycle clocks must be a function of elapsed samples alone.
    if (numSamples > 0) {
        advanceBank(jitter_,   numSamples);
        advanceBank(decohere_, numSamples);
        advanceLifecycles(static_cast<float>(numSamples) * invSampleRate_);
    }
    // Only the STAGE APPLICATION is gated on having something to write to.
    if (r == nullptr || a == nullptr || count == 0) return;
    applyStages(r, a, std::min(count, kPartials));
}
```

**Why the order matters, concretely.** The engine calls
`entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, numSamples)` with
`outCount_ = max(slotNumPartials_[A], slotNumPartials_[B])` (§5.5). FR-012 permits
`numPartials ∈ [0, 64]` and `SpectralState` default-constructs to `numPartials = 0` (§3.1), so a
caller who loads two default-constructed states via `setState` makes `outCount_` exactly 0. Under
the earlier draft's ordering that stalled **both** 64-lane OU banks and all 64 lifecycle FSMs for
the entire duration — making the entropy processor's state history depend on which spectral states
happened to be loaded, and directly contradicting the invariant this section states two paragraphs
above ("lane state after N advanced samples is a function of N alone"). SC-012 gains a case that
interleaves an `outCount_ == 0` configuration into an otherwise ordinary run and asserts the lane
state matches an uninterrupted run of the same total length (§9.2).

`applyStages` per partial `i < count`, in FR-072's fixed order:

```
d_i  = clamp(jitter_.smoothCur[i],   -1, +1)
c_i  = clamp(decohere_.smoothCur[i], -1, +1)

(a) a[i] *= (1.0f + w1_ * kMaxAmpJitter * d_i)            // strictly positive: 0.5 < 1
(4) a[i] *= life_[i]                                      // L_i, FR-073
(b,c) cents_i = w2_ * kMaxDecoherenceCents * c_i
              + w3_ * kMaxScatterCents     * scatterDraw_[i]
      r[i] *= centsToPitchRatio(cents_i)
```

**Deviation D3 (§11):** FR-072 writes (b) and (c) as two successive multiplications by
`centsToPitchRatio`. Summing the two cent terms and converting once is the same real number
(`f(x)·f(y) = f(x+y)`), differs only in float rounding (~1e-7 relative ≈ 1.7e-4 cents — four orders
below `kTargetRatioEpsilonCents = 0.05`), preserves FR-074's `±11.0` cent bound exactly, and halves
the transcendental count in the hottest loop of the phase (§8).

`getDecoherenceCents(i)` returns `w2_ * kMaxDecoherenceCents * c_i` and `getAppliedScatterCents(i)`
returns `w3_ * kMaxScatterCents * scatterDraw_[i]`, so SC-005's per-stage clauses remain readable
even though the two are applied together.

### 4.7 Death / rebirth (FR-073) — the FSM, exactly

Per partial, advanced once per chunk by `dt = numSamples / sampleRate`:

```
Alive:   life_[i] = 1.0f;                                  // EXPLICIT assignment, not arithmetic
         if (w4_ > 0.0f) {
             p = w4_ * kMaxDeathRatePerSecond * dt;
             if (lifeRng_[i].nextUnipolar() < p) {          // draw 1 (always, when w4_ > 0)
                 phaseLength_[i] = lerp(kMinDeathFadeSec, kMaxDeathFadeSec,
                                        lifeRng_[i].nextUnipolar());   // draw 2 (only on death)
                 phaseTimer_[i]  = phaseLength_[i];
                 phase_[i] = Dying;
             }
         }
Dying:   phaseTimer_[i] -= dt;
         life_[i] = clamp(phaseTimer_[i] / phaseLength_[i], 0.0f, 1.0f);   // linear 1 -> 0
         if (phaseTimer_[i] <= 0.0f) {
             life_[i] = 0.0f;                              // EXACTLY 0 before anything else
             phaseLength_[i] = lerp(kMinDeadDwellSec, kMaxDeadDwellSec, nextUnipolar());
             phaseTimer_[i]  = phaseLength_[i];
             phase_[i] = Dead;
             scatterDraw_[i] = lifeRng_[i].nextFloat();    // FR-073 redraw, at L_i == 0.0f
             ++scatterRedraws_[i];
         }
Dead:    life_[i] = 0.0f;
         phaseTimer_[i] -= dt;
         if (phaseTimer_[i] <= 0.0f) {
             phaseLength_[i] = lerp(kMinRebirthFadeSec, kMaxRebirthFadeSec, nextUnipolar());
             phaseTimer_[i]  = phaseLength_[i];
             phase_[i] = Reborn;
         }
Reborn:  phaseTimer_[i] -= dt;
         life_[i] = clamp(1.0f - phaseTimer_[i] / phaseLength_[i], 0.0f, 1.0f);  // 0 -> 1
         if (phaseTimer_[i] <= 0.0f) { life_[i] = 1.0f; phase_[i] = Alive; }
```

The RNG draw order is fixed and documented (Edge Cases: `reset()` must reproduce it exactly).
The redraw happens on the chunk that enters `Dead`, **after** `life_[i]` has been set to exactly
`0.0f`, so SC-006's "every redraw occurred at `L_i == 0.0f` (bitwise)" holds by construction; and
because the dead dwell is ≥ 0.2 s ≈ 150 further 64-sample chunks at 48 kHz, the redraw is also
strictly inside the window in the ordinary sense.

**Deviation D5 (§11):** FR-073 says "at `w_4 = 0` every partial is `Alive` with `L_i` exactly
`1.0f`". Implemented as: `w_4 == 0` **starts no new deaths** and forces `L_i = 1.0f` for partials
in `Alive`; a lifecycle already in flight runs to completion (bounded by 5.0 s). A literal
force-to-`Alive` would make `setEntropy(0)` during a `Dead` window a step of `L_i` from 0 to 1 in
one chunk — 40× `kMaxAmpDeltaPerChunk` — and SC-001 clause 1 exercises `setEntropy` mid-sweep.
SC-005's clauses are unaffected: they are measured on a processor whose entropy never exceeded
stage 4's onset, where the phase is `Alive` and `L_i` is exactly `1.0f`.

### 4.8 prepare / reset contract

`prepare(sampleRate)`: floor at 1 Hz (`sampleRate > 1.0 ? sampleRate : 1.0`, matching
`brownian_drift.h:122` and `spline_trajectory.h:137`); recompute `invSampleRate_` and, **per bank**,
`a`/`g` (in `double`, §4.4) and `smoothCoeff` — from `kEntropyAmpSmoothMs = 750` for `jitter_` and
`kEntropyCentsSmoothMs = 150` for `decohere_` (D12); then `reset()`.
`reset()`: re-seed every one of the 4×64 lanes from `deriveStreamSeed(configuredSeed_, salt)`;
`walk`/`smoothCur`/`smoothTgt` zeroed, `samplesUntilControl = 0`, `cachedPowN = 0`;
draw all 64 `scatterDraw_` values in index order; `scatterRedraws_` zeroed; every `phase_ = Alive`,
`life_ = 1.0f`, timers zeroed. Parameters (`entropy_`, `w1_..w4_`, `configuredSeed_`) are **not**
touched — matching `BrownianDrift::reset()` (:133-135), which keeps smoothness/depth.
`setSeed(s)` stores `s` and performs the same re-seed + redraw as `reset()`'s once-per-seed half.

Both are documented in the header as **configuration-time calls, not to be made while the consumer
is sounding** (FR-005, FR-006, FR-044's named exemptions).

---

## 5. `SpectralMorphEngine` — Layer 3

**Header:** `dsp/include/krate/dsp/systems/spectral_morph_engine.h`
**Includes:** `<krate/dsp/core/db_utils.h>` (for `detail::constexprExp`/`constexprLn` and
`detail::kLn2` — §2.3), `<krate/dsp/core/random.h>`, `<krate/dsp/processors/spectral_state.h>`,
`<krate/dsp/processors/entropy_processor.h>`, **`<krate/dsp/processors/brownian_drift.h>`**
(*`kDriftOutputSmoothMs` only, needed at compile time by the FR-044 `static_assert`s below;
Layer 3 → Layer 2, legal*), `<krate/dsp/processors/spline_trajectory.h>`, plus
`<algorithm> <array> <cmath> <cstddef> <cstdint>`.
**It must not include `harmonic_cloud.h`** (FR-003, Non-Goals) — a lint-visible property.

### 5.1 Constants

```cpp
class SpectralMorphEngine {
public:
    static constexpr int         kMinStates     = 2;
    static constexpr int         kMaxStates     = 4;
    static constexpr std::size_t kStatePartials = SpectralState::kStatePartials;   // 64

    static constexpr float kMaxBloomFraction   = 0.6f;
    static constexpr float kMinTravelRate      = 1.0f / 600.0f;   // journeys per second
    static constexpr float kMaxTravelRate      = 1.0f;
    static constexpr float kStateChangeFadeSec = 2.0f;

    // FR-041 fill
    static constexpr float kMaxFillGrowth    = 2.0f;
    static constexpr float kMaxFillRatio     = SpectralState::kMaxStateRatio;      // 128
    static constexpr float kFillSpacingCents = 28.0f;
    static constexpr float kFillSpacingLog2  = kFillSpacingCents / 1200.0f;
    static constexpr float kFillSpacingFactor =
        detail::constexprExp(kFillSpacingLog2 * detail::kLn2);                     // 1.0163049
    // FR-046 (aliases of the single definition in EntropyProcessor)
    static constexpr float kMinRatioSpacingCents  = EntropyProcessor::kMinRatioSpacingCents;
    static constexpr float kMinRatioSpacingLog2   = EntropyProcessor::kMinRatioSpacingLog2;

    // FR-044 derivation, all compile-time.
    // kStatePartials (64), NOT kStatePartials - 1 (63): the worst case needs 63 chained
    // float multiplies to REACH the ceiling, so a mathematically tight ceiling can be
    // missed by accumulated rounding. See deviation D13.
    static constexpr float kMaxOutputRatio =
        kMaxFillRatio * detail::constexprExp(static_cast<float>(kStatePartials)
                                             * kFillSpacingLog2 * detail::kLn2);    // 360.37
    static constexpr float kOutputCentsSpan =
        1200.0f * detail::constexprLn(kMaxOutputRatio / SpectralState::kMinStateRatio)
                / detail::kLn2;                                                     // 11392.0
    static constexpr float kMaxAmpDeltaPerChunk        = 0.025f;
    static constexpr float kMaxRatioDeltaCentsPerChunk = 125.0f;

    enum class TravelMode : std::uint8_t { External = 0, Spline };   // NESTED — see D8
```

**The FR-044 contributor table is a `static_assert`, not a comment.** With `T = 64/48000`,
`R = kMaxTravelRate * (kMaxStates - 1) = 3.0`:

```cpp
static constexpr float kFr044SampleRate   = 48000.0f;
static constexpr float kFr044ChunkSamples = 64.0f;
static constexpr float kFr044ChunkSeconds = kFr044ChunkSamples / kFr044SampleRate;

static constexpr float kFr044TravelAmp  = (kMaxTravelRate * float(kMaxStates - 1))
                                        * kFr044ChunkSeconds / (1.0f - kMaxBloomFraction);
static constexpr float kFr044StateAmp   = kFr044ChunkSeconds / kStateChangeFadeSec;
static constexpr float kFr044DeathAmp   = kFr044ChunkSeconds / EntropyProcessor::kMinDeathFadeSec;

// THE SMOOTHER'S OWN CONVENTION, ROUTED THROUGH THE SHARED HELPER SO IT CANNOT DRIFT AGAIN:
// OnePoleSmoother's parameter is time-to-99% and coeff = exp(-5000/(ms*fs)) (smoother.h:91),
// so tau = ms/5000 s. Writing `1 - exp(-T / (ms * 0.001))` treats ms as a time constant and
// understates the per-chunk step by a factor of 4.91 at 150 ms. See deviations D12/D14.
static constexpr float kFr044AmpOuStep   = EntropyProcessor::onePoleChunkStep(
    EntropyProcessor::kEntropyAmpSmoothMs,   kFr044ChunkSamples, kFr044SampleRate);  // 8.8495e-3
static constexpr float kFr044CentsOuStep = EntropyProcessor::onePoleChunkStep(
    EntropyProcessor::kEntropyCentsSmoothMs, kFr044ChunkSamples, kFr044SampleRate);  // 4.3471e-2

static constexpr float kFr044JitterAmp = EntropyProcessor::kMaxAmpJitter * 2.0f * kFr044AmpOuStep;
static_assert(kFr044TravelAmp + kFr044StateAmp + kFr044DeathAmp + kFr044JitterAmp
                  <= kMaxAmpDeltaPerChunk, "FR-044 amplitude budget");
static_assert((kFr044TravelAmp + kFr044StateAmp) * kOutputCentsSpan
                  + EntropyProcessor::kMaxDecoherenceCents * 2.0f * kFr044CentsOuStep
                  <= kMaxRatioDeltaCentsPerChunk, "FR-044 cents budget");
```

Recomputed this session with the corrected step and the widened `kOutputCentsSpan`:

| Term | Value | Source |
|---|---|---|
| `kFr044TravelAmp` | `1.00000e-2` | `3.0 · 1.33333e-3 / 0.4` |
| `kFr044StateAmp` | `6.66667e-4` | `1.33333e-3 / 2.0` |
| `kFr044DeathAmp` | `2.66667e-3` | `1.33333e-3 / 0.5` |
| `kFr044AmpOuStep` | `8.84950e-3` | `1 − exp(−5000·64 / (750 · 48000))`, tau = 0.150 s |
| `kFr044JitterAmp` | `8.84950e-3` | `0.5 · 2 · kFr044AmpOuStep` |
| **amplitude sum** | **`2.21829e-2 ≤ 0.025`** ✔ | 11.3 % slack |
| `kFr044CentsOuStep` | `4.34712e-2` | `1 − exp(−5000·64 / (150 · 48000))`, tau = 0.030 s |
| travel + state cents | `121.515` | `1.06667e-2 · 11392.0` |
| decoherence cents | `0.34777` | `4.0 · 2 · kFr044CentsOuStep` |
| **cents sum** | **`121.863 ≤ 125.0`** ✔ | 2.5 % slack |

**The amplitude budget is met by fixing the smoother, not by moving the bound.** With the
`kEntropyAmpSmoothMs = 750` correction the jitter term is `8.8495e-3` — *exactly* the `8.85e-3` the
spec's own FR-044 table publishes (spec.md:815), because that row is derived with
`kDriftOutputSmoothSec = 0.150` **as a time constant** (spec.md:817-819) and 750 ms in
`OnePoleSmoother`'s time-to-99 % convention *is* tau = 0.150 s. The earlier draft of this plan fed
the smoother 150 ms, whose true tau is 0.030 s: the step becomes `4.3471e-2`, the jitter term
`4.3471e-2`, and the amplitude sum `5.680e-2` — **2.3× over `kMaxAmpDeltaPerChunk` and a hard
compile failure of the first `static_assert`**. `kMaxAmpDeltaPerChunk` is *not* raised to absorb
it: the spec defines that constant as the sum of the enumerated contributors, and the contributor
that was wrong is the implementation's smoothing time, not the bound.

The decoherence bank deliberately keeps `BrownianDrift`'s own 150 ms so it stays bit-comparable to
a stock `BrownianDrift` for §9's equivalence test; its cents contribution rises from the spec's
0.071 to 0.348, which the cents budget absorbs with 3.1 cents to spare. Recorded as **D12**.

### 5.2 Public API (FR-005, FR-008, FR-042, FR-043, FR-051, FR-061)

```cpp
    void prepare(double sampleRate) noexcept;      // NOT RT-safe by contract
    void reset() noexcept;                         // configuration-time (FR-005)
    void setSeed(std::uint32_t seed) noexcept;     // configuration-time (FR-006)

    void setState(int slot, const SpectralState& s) noexcept;   // see the FR-042 contract below
    void setStateCount(int n) noexcept;                          // clamped to [2,4]
    void setBloom(float bloom) noexcept;                         // clamped [0,1]
    void setEntropy(float e) noexcept;                           // forwards to the owned processor
    void setTravelMode(TravelMode mode) noexcept;
    void setTargetPosition(float p) noexcept;                    // clamped [0, numStates-1]
    void setTravelRate(float journeysPerSecond) noexcept;        // clamped [kMin,kMax]TravelRate

    void updateChunk(std::size_t numSamples) noexcept;           // FR-043, the ONLY array writer

    // FR-008 zero-copy output accessors — the signatures Phase 7 consumes
    [[nodiscard]] const float* getOutputRatios()     const noexcept;  // kStatePartials floats
    [[nodiscard]] const float* getOutputAmplitudes() const noexcept;
    [[nodiscard]] std::size_t  getOutputCount()      const noexcept;
    [[nodiscard]] const float* getCleanRatios()      const noexcept;  // pre-entropy
    [[nodiscard]] const float* getCleanAmplitudes()  const noexcept;

    // FR-008 scalar introspection
    [[nodiscard]] float         getTravelPosition()          const noexcept;
    [[nodiscard]] float         getCompletionFraction(std::size_t i) const noexcept;  // u_i
    [[nodiscard]] float         getBloom()                   const noexcept;
    [[nodiscard]] float         getTravelRate()              const noexcept;
    [[nodiscard]] TravelMode    getTravelMode()              const noexcept;
    [[nodiscard]] int           getStateCount()              const noexcept;
    [[nodiscard]] std::uint32_t getRepairEngagementCount()   const noexcept;   // FR-046
    [[nodiscard]] std::uint64_t getLimiterActiveChunks()     const noexcept;   // SC-002 clause 4
    [[nodiscard]] std::uint64_t getTotalChunks()             const noexcept;
    [[nodiscard]] bool          isStateFadeActive()          const noexcept;   // FR-047
    [[nodiscard]] bool          stateFinite()                const noexcept;
    [[nodiscard]] const EntropyProcessor& entropy()          const noexcept;   // forwards SC-005/006 reads
```

Both output pointers address **stable member storage** whose address never changes over the
instance's lifetime; only the contents move, and only inside `updateChunk`. That is what makes
FR-086 copy-free and gives FR-085 lever 1 a stable comparand.

**FR-042's `setState` rejection contract — stated here, and given a criterion.** `setState(slot, s)`
returns immediately, writing **nothing**, if `slot ∉ [0, kMaxStates)` or
`!isValidSpectralState(s)`. This is a *different and stricter* rejection set than
`setSpectralTarget`'s (§6.3), and FR-012 / FR-081 diverge on purpose (spec.md:604-611): `setState`
enforces the `amplitude ≤ 1`, `ratios[i] ∈ [kMinStateRatio, kMaxStateRatio]` and
strict-monotonicity clauses that `setSpectralTarget` must **not**. An earlier draft of this plan
stated the behaviour only in a trailing comment and gave it no test row, so an implementation that
silently accepted a non-monotone or `amplitude = 5.0` state would have passed every criterion in
§9.2. The SC-015 row now enumerates the `setState` reject set explicitly, alongside the
mirror-image assertion that the *same* arrays are **accepted** by `setSpectralTarget`.

### 5.3 State layout

```cpp
private:
    // Per slot — SANITIZED arrays, not SpectralState copies (deviation D10)
    std::array<std::array<float, kStatePartials>, kMaxStates> slotLog2Ratio_{};
    std::array<std::array<float, kStatePartials>, kMaxStates> slotAmp_{};
    std::array<int, kMaxStates> slotNumPartials_{};
    int numStates_ = kMinStates;

    // Travel
    float      position_       = 0.0f;
    float      targetPosition_ = 0.0f;
    float      travelRate_     = kMinTravelRate;
    TravelMode mode_           = TravelMode::External;
    SplineTrajectory spline_;
    std::uint64_t limiterActiveChunks_ = 0, totalChunks_ = 0;

    // Bloom
    float bloom_ = 0.0f;
    alignas(32) std::array<float, kStatePartials> invCompletionPoint_{};   // 1 / e_n
    alignas(32) std::array<float, kStatePartials> completion_{};           // u_i (FR-008)

    // Working / output
    alignas(32) std::array<float, kStatePartials> logRatio_{};   // post-repair, post-absorption
    alignas(32) std::array<float, kStatePartials> cleanRatio_{}; // exp2(logRatio_)
    alignas(32) std::array<float, kStatePartials> cleanAmp_{};
    alignas(32) std::array<float, kStatePartials> outRatio_{};
    alignas(32) std::array<float, kStatePartials> outAmp_{};
    std::size_t outCount_ = kStatePartials;

    // FR-047 absorption
    alignas(32) std::array<float, kStatePartials> departLogRatio_{};
    alignas(32) std::array<float, kStatePartials> departAmp_{};
    float fadeX_ = 1.0f;

    // FR-042 identical-state detection scratch (config rate only)
    alignas(32) std::array<float, kStatePartials> scratchLog2_{};
    alignas(32) std::array<float, kStatePartials> scratchAmp_{};

    std::uint32_t     repairCount_ = 0;
    EntropyProcessor  entropy_;
    double            sampleRate_ = 44100.0;
    float             invSampleRate_ = 1.0f / 44100.0f;
    std::uint32_t     configuredSeed_ = kDefaultMorphSeed;   // 1u, matching random.h:44's default
    bool              prepared_ = false;
```

**Deviation D10 (§11):** the engine stores sanitized per-slot arrays rather than `SpectralState`
copies. `setState` writes the FR-041-filled `log2(ratio)` for all 64 slots and **zeroes**
`slotAmp_[s][i]` for `i ≥ numPartials` (FR-012 explicitly does not constrain those entries, so a
caller-supplied state may carry garbage there). A consequence worth naming: `tiltDbPerOct`,
`inharmonicity` and `name` are structurally incapable of reaching the audio path, which is exactly
what FR-013 requires and what SC-008 clause 3 asserts. It also removes 4 × 544 bytes per instance.

### 5.4 FR-041 fill rule — the exact recurrence

Computed inside `setState`, for `j` from `max(numPartials, 0)` to 63, over the linear ratio array
`r` (which already holds the state's real ratios below `numPartials`):

```cpp
float grown;
if (numPartials >= 2) {
    // Geometric continuation of the state's own last spacing, growth-clamped and
    // ceilinged. `j >= 2` always holds here: the loop starts at j = numPartials >= 2.
    const float g = std::clamp(r[j-1] / r[j-2], 1.0f, kMaxFillGrowth);
    grown = std::min(r[j-1] * g, kMaxFillRatio);
} else {
    // FR-041's sparse-state rule, for EVERY j — not only j < 2.
    grown = static_cast<float>(j + 1);
}
const float floorV = (j >= 1) ? r[j-1] * kFillSpacingFactor : static_cast<float>(j + 1);
r[j] = std::max(grown, floorV);
```

**Deviation D9 (§11):** FR-041 says the fill is `r_j = j + 1` when the state has fewer than two real
ratios, and Edge Cases adds "and the clamps still apply". Applying the `max(..., r_{j-1} ·
kFillSpacingFactor)` floor — which *is* one of those clamps — is what keeps the array monotone for
the `numPartials = 1, ratios[0] = 128` corner, where the bare `j + 1` rule would emit
`128, 2, 3, …`. Without it FR-046's repair would have to rescue every chunk of that configuration,
which SC-002 clause 2 does not cover but SC-015 does exercise.

An earlier draft wrote the sparse arm as `(numPartials >= 2 || j >= 2) ? … : float(j + 1)`, whose
`|| j >= 2` disjunct turned the **geometric** rule on from slot 2 onward *regardless* of
`numPartials`: for `numPartials = 0` it emitted `1, 2, 4, 8, …, 128` and then the staircase — a
doubling progression, not `j + 1`, and not what D9 justified. The rule above is D9 exactly, and
SC-015 gains an arm that recomputes the documented sequences element-wise:

| State | Filled `r[0..63]` (documented, and recomputed in the test) |
|---|---|
| `numPartials = 0` | `1, 2, 3, …, 62`, then `r[62] = 63.011`, `r[63] = 64.038` — the `j + 1` rule up to `j = 61`, after which the 28-cent floor `r_{j−1}·1.0163049` overtakes it (`j·1.0163049 > j + 1` once `j > 61.3`). |
| `numPartials = 1, ratios[0] = 1` | Identical to the row above (slot 0 already holds 1.0). |
| `numPartials = 1, ratios[0] = 128` | `128`, then the pure 28-cent staircase `128 · 1.0163049^j`, reaching `128 · 2^1.47 = 354.59` at slot 63 — inside `kMaxOutputRatio = 360.37` with 1.6 % of headroom, which is precisely why §5.1 buys that headroom (**D13**). |

Verified against the factory set: Bell's fill runs `117.67 → 127.89 → 129.98 → 132.10 → …`
(the `kMaxFillRatio` cap binds for exactly one slot before the `kFillSpacingCents` staircase takes
over, since the floor `127.89 · 1.0163 = 129.98` exceeds the cap), reaching **240.32** at slot 63,
inside `kMaxOutputRatio = 360.37`. Minimum adjacent spacing in the fill region is exactly
28.00 cents, above `kMinRatioSpacingCents = 24.0`, so FR-046 is inert there — which is what makes
SC-002 clause 2's engagement-count-of-zero satisfiable for the four Bell pairs.

`slotLog2Ratio_[s][i] = std::log2(r[i])` — 64 `log2` per `setState`, config rate.

### 5.5 The chunk pipeline — everything in log2 until one `exp2`

**Deviation D1 (§11).** FR-041, FR-046 and FR-047 are each stated in the linear ratio domain. They
are implemented in the **log2 domain**, which is the same function in every case, and which reduces
the per-chunk transcendental count from three per partial to one:

| Spec form (linear) | Implemented form (log2) | Same function because |
|---|---|---|
| `r_i = exp2(log2 A·(1−u_i) + log2 B·u_i)` | `L_i = log2A·(1−u_i) + log2B·u_i` | identical; the `exp2` is deferred |
| `r_i ← max(r_i, r_{i−1}·kMinRatioSpacingFactor)` | `L_i ← max(L_i, L_{i−1} + kMinRatioSpacingLog2)` | `log2` is strictly increasing; `max` commutes with it |
| `r_i = exp2(log2 D_i·(1−x) + log2 N_i·x)` | `L_i = LD_i·(1−x) + L_i·x` | identical |

Continuity, strict monotonicity and the 24-cent floor are all preserved verbatim — the floor
argument in particular becomes *additive*: a convex combination of two arrays whose adjacent log
gaps are each ≥ `kMinRatioSpacingLog2` has adjacent gaps ≥ `kMinRatioSpacingLog2`, by induction over
chunks since `departLogRatio_` is itself a previous post-repair, post-absorption array.

```cpp
void updateChunk(std::size_t numSamples) noexcept {
    if (numSamples == 0) return;                  // Edge Cases: no-op, state UNADVANCED
    advanceTravel(numSamples);                    // FR-061/062/063
    recomputeCompletion();                        // FR-051
    interpolate();                                // FR-041  -> logRatio_, cleanAmp_
    repairSpacing();                              // FR-046  (log domain), repairCount_
    applyAbsorption(numSamples);                  // FR-047  -> logRatio_, cleanAmp_
    for (i < kStatePartials) cleanRatio_[i] = std::exp2(logRatio_[i]);   // the ONE exp2
    outRatio_ = cleanRatio_;  outAmp_ = cleanAmp_;                       // 2 x 64-float copies
    entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, numSamples);   // FR-070
}
```

The whole pipeline runs over all 64 slots; `outCount_ = max(A.numPartials, B.numPartials)` is
metadata that gates what a consumer reads and what `processChunk` perturbs. Slots at or above
`outCount_` carry interpolated amplitude exactly 0 (both contributing states are zeroed there by
`setState`) and a monotone continuation ratio, so SC-015's "no output element is non-finite" is
satisfiable over the full array.

#### `recomputeCompletion` (FR-051, FR-052)

`invCompletionPoint_` is recomputed only in `setBloom` (config rate):

```
if (bloom_ == 0.0f) invCompletionPoint_.fill(1.0f);          // EXPLICIT branch: -ffast-math
else for n = 1..64:
    e_n = 1.0f - bloom_ * kMaxBloomFraction * (1.0f - float(n - 1) / float(kStatePartials - 1));
    invCompletionPoint_[n-1] = 1.0f / e_n;                    // e_n >= 0.4, never near zero
```

Per chunk: `u = position_ − floor(position_)`; `completion_[i] = clamp(u * invCompletionPoint_[i],
0, 1)`. At `bloom = 0` the multiply is by exactly `1.0f`, so `u_i == u` **bitwise** — which is what
SC-003's `1e-7` clause and SC-002 clause 2's `u_i = u` argument both rest on. Travelling backwards
uses the same expression, so high partials lead on the way back (FR-052) with no ratchet state.

#### `interpolate` (FR-041)

```
k = clamp(int(floor(position_)), 0, numStates_ - 1);
u = position_ - float(k);
A = k;  B = min(k + 1, numStates_ - 1);
invU_i = 1.0f - completion_[i];
cleanAmp_[i] = slotAmp_[A][i] * invU_i + slotAmp_[B][i] * completion_[i];      // spectral_morph_filter.h:601
logRatio_[i] = slotLog2Ratio_[A][i] * invU_i + slotLog2Ratio_[B][i] * completion_[i];
outCount_    = max(slotNumPartials_[A], slotNumPartials_[B]);
```

At `position_ == numStates_ − 1` exactly: `k = numStates_ − 1`, `u = 0`, `B == A` — the segment is
degenerate and the output is state `A`, which is what makes `p = numStates−1` well-defined
(Edge Cases). The `A == B` case is also the FR-005 default (all four slots identical).

#### `repairSpacing` (FR-046)

```
bool changed = false;
for (i = 1; i < kStatePartials; ++i) {
    const float floorLog = logRatio_[i-1] + kMinRatioSpacingLog2;
    if (logRatio_[i] < floorLog) { logRatio_[i] = floorLog; changed = true; }
}
if (changed) ++repairCount_;
```

64 compares and at most 63 adds. Continuous (composition of `max` over continuous functions).
Inert on the factory set: the tightest clean adjacent spacing anywhere in the five states is
**27.264 cents** (partials 63→64 of the three `ratio_n = n` states — see deviation D7), above
`kMinRatioSpacingCents = 24.0` with 3.264 cents of slack; the fill region carries its own 28.0-cent
floor.

#### `applyAbsorption` (FR-047)

```
if (fadeX_ >= 1.0f) return;                                   // nothing in flight
fadeX_ = std::min(1.0f, fadeX_ + float(numSamples) * invSampleRate_ / kStateChangeFadeSec);
const float inv = 1.0f - fadeX_;
for (i) {
    logRatio_[i] = departLogRatio_[i] * inv + logRatio_[i] * fadeX_;
    cleanAmp_[i] = departAmp_[i]      * inv + cleanAmp_[i] * fadeX_;
}
```

Armed by `setState` (on a slot that currently contributes, i.e. `A` or `B`) and by `setStateCount`:
snapshot `departLogRatio_ = logRatio_`, `departAmp_ = cleanAmp_` **from the current post-absorption
clean arrays**, then `fadeX_ = 0.0f`. A second qualifying call while `fadeX_ < 1` re-snapshots from
the current arrays, so the output stays continuous through any number of overlapping changes. The
ramp advances on `chunkSeconds`, so it is sample-rate and chunk-length independent (SC-013).

`setState` with an **identical** state is a no-op (Edge Cases): the candidate's sanitized arrays are
built into `scratchLog2_`/`scratchAmp_` and compared element-wise against the stored slot; on a full
match the function returns without arming a fade and without touching `isStateFadeActive()`.
`setStateCount(n)` with `n == numStates_` is likewise a no-op.

#### `advanceTravel` (FR-061, FR-062, FR-063)

```
const float dt = float(numSamples) * invSampleRate_;
float target;
if (mode_ == TravelMode::Spline) {
    spline_.processBlock(numSamples);                                  // spline_trajectory.h:193
    const float s = spline_.getCurrentValue();                         // :204
    const float unit = std::clamp((s / SplineTrajectory::kWaypointMax + 1.0f) * 0.5f, 0.0f, 1.0f);
    target = unit * float(numStates_ - 1);                             // FR-061 range rescale
} else {
    target = targetPosition_;
}
const float cap   = travelRate_ * float(numStates_ - 1) * dt;          // slewCap * dt
const float delta = target - position_;
++totalChunks_;
if (std::abs(delta) > cap) {
    position_ += (delta > 0.0f ? cap : -cap);
    ++limiterActiveChunks_;
} else {
    position_ = target;
}
position_ = std::clamp(position_, 0.0f, float(numStates_ - 1));
```

One position state, one limiter, shared by both modes — which is exactly why FR-062's mode switch
costs nothing extra: it changes the limiter's *target*, never its output. The `kWaypointMax` rescale
is what makes `p = 0` and `p = numStates−1` reachable (SC-002 clause 3); the clamp is required
because uniform Catmull-Rom overshoots its control points (`spline_trajectory.h:56-66` bounds `|q|`
at 1.0, not at `kWaypointMax`).

Analytic check of SC-002 clause 4: at `kDefaultInterval = 2.0` (:123) the worst-case mapped rate is
`0.75 · (numStates−1) · (2.0/interval) = 2.25` units/s at 4 states, against
`slewCap = kMaxTravelRate · 3 = 3.0` units/s — the limiter is inactive by construction, and the
clause measures that it is.

### 5.6 prepare / reset contract

```
CONSTRUCTOR:  for slot in 0..3: setState-equivalent load of makeFactoryState(SineStack)  (FR-005)
              numStates_ = 2;  bloom_ = 0.0f;  invCompletionPoint_.fill(1.0f);
              mode_ = External;  travelRate_ = kMinTravelRate;  entropy_.setEntropy(0.0f);
              (then the same rewind reset() performs, below)

prepare(sr):  sampleRate_ = sr > 1.0 ? sr : 1.0;  invSampleRate_ = 1/sampleRate_;
              spline_.prepare(sampleRate_);        // spline_trajectory.h:136
              entropy_.prepare(sampleRate_);
              reset();
              prepared_ = true;

reset():      // STOCHASTIC AND TRAVEL STATE ONLY — no configured parameter is touched.
              position_ = targetPosition_ = 0.0f;
              fadeX_ = 1.0f;  repairCount_ = 0;  limiterActiveChunks_ = totalChunks_ = 0;
              departLogRatio_.fill(0.0f);  departAmp_.fill(0.0f);
              spline_.setSeed(deriveStreamSeed(configuredSeed_, kSplineBaseSalt));  spline_.reset();
              entropy_.setSeed(deriveStreamSeed(configuredSeed_, kEntropyBaseSalt)); entropy_.reset();
              refreshOutputs();
```

`kEntropyBaseSalt = 0x1000`, `kSplineBaseSalt = 0x1001` — distinct base salts so the two
sub-components' streams cannot correlate (FR-045).

**`reset()` rewinds; it does not reconfigure.** The FR-005 default load moved to the **constructor**,
and `reset()` leaves slots, `numStates_`, `bloom_`, `mode_`, `travelRate_` and `entropy` exactly as
the caller set them. Three reasons, each concrete:

1. **It is what FR-005 actually says.** "`reset()` rewinds to the exact post-`prepare` state
   including every RNG stream, **matching `BrownianDrift::reset()` (`:133`) and
   `SplineTrajectory::reset()` (`:144`)**" (spec.md:441-443). `BrownianDrift::reset()` (`:133-135`,
   read this session) keeps smoothness and depth; it rewinds the walk. FR-005's default **table** is
   scoped in its own heading to "after default construction and after `prepare(sampleRate)` with
   **no** parameter call" (spec.md:450-451) — it is not a post-`reset()` claim.
2. **`EntropyProcessor::reset()` behaves the same way and the two must agree** (§4.8: `entropy_`,
   `w1_..w4_` and `configuredSeed_` survive `reset()`, matching `BrownianDrift::reset()`). An
   engine `reset()` that wiped its own configuration but forwarded no `setEntropy(0)` would leave
   the pair internally inconsistent — which is exactly the state that made SC-012's clause
   unsatisfiable in the earlier draft: the criterion is measured at the test's own `entropy = 1`
   configuration, so post-`reset()` the scatter draws are redrawn and applied at `w_3 = 1`, while
   the "post-`prepare`" arrays it is compared against were clean at entropy 0. A faithful
   implementation could not pass it. §9.2's SC-012 row is restated accordingly.
3. **Phase 7 calls `reset()` at voice allocation** for a per-note restart (spec.md:118-124). Under
   the earlier draft that would silently reset the voice's four spectral identities to SineStack,
   its state count to 2 and its travel rate to `kMinTravelRate` — i.e. every note would erase the
   patch. Recorded as deviation **D15**.

SC-002 clause 5 still holds after a bare `prepare()`: the constructor performs the default load, and
`prepare()`'s `reset()` does not undo it. The constructor is the one place where a `SpectralState`
is materialised on the stack (544 bytes ×1, sequentially, four times) — acceptable, it is not the
audio thread.

`refreshOutputs()` runs the §5.5 pipeline **without advancing anything**: `recomputeCompletion()`
(at `position_ = 0`, so `u = 0`), `interpolate()`, `repairSpacing()`, `applyAbsorption` skipped
(`fadeX_ = 1`), the `exp2`, the copies, and `entropy_.processChunk(out…, outCount_, 0)` — which
FR-075's `numSamples == 0` path defines as "apply the stages, advance nothing". That is what makes
SC-002 clause 5's "the arrays are populated with no advance, and 200 further `updateChunk(64)` calls
leave every element bitwise unchanged" satisfiable: with all four slots identical the interpolation
is degenerate and, at `entropy = 0`, every stage weight is 0 and every `L_i` is exactly `1.0f`.

`setSeed(s)` stores `s`, re-seeds the spline and the entropy processor from the two derived base
salts, and calls `refreshOutputs()`. It is documented as a configuration-time call (FR-006,
FR-044's named exemption) — it steps up to `2 · kMaxScatterCents = 14` cents per partial.

### 5.7 RT-safety

**Every method — `prepare` included — is `noexcept`**, and SC-011's
`static_assert(noexcept(...))` covers the **full** public surface, which is what spec.md:1904-1905
requires; the `prepare(double) noexcept` declaration in §5.2 is the normative one. What sets
`prepare` apart is not `noexcept` but its **contract**: it is the only method permitted to do
config-rate work (transcendentals, coefficient re-derivation, a `reset()`), so it is documented as
**not RT-safe by contract** and must not be called from the audio thread. The same wording applies
to `EntropyProcessor::prepare` (§4.2). Everything else is allocation-free, lock-free and I/O-free.
All storage is
fixed-size members (~14 KB per instance including the owned `EntropyProcessor` — 224 KB at 16
voices). `updateChunk` performs no branch on unbounded data and no unbounded loop: the travel
limiter is a single compare, the spline's waypoint rotation is bounded by the chunk length
(`spline_trajectory.h:262-269`), and the entropy banks' control loop is bounded by
`numSamples / kControlRateInterval`. `static_assert(noexcept(...))` over the entire public surface
is part of SC-011.

---

## 6. `HarmonicCloud` amendment (FR-080 series)

Strictly additive. Every change is inert while `hasTarget_` is false, which is what makes SC-014
clause 1 a structural property rather than a measurement.

### 6.1 New members

```cpp
    alignas(32) std::array<float, kMaxPartials> targetRatio_{};   // ratio_override, latest supplied
    alignas(32) std::array<float, kMaxPartials> targetAmp_{};     // amp_override,  latest supplied

    // The values the LAST RECOMPUTE ACTUALLY CONSUMED. The dirty test compares against
    // THESE, never against targetRatio_/targetAmp_. See §6.3.
    alignas(32) std::array<float, kMaxPartials> committedRatio_{};
    alignas(32) std::array<float, kMaxPartials> committedAmp_{};

    std::uint64_t freqSlotDirty_ = 0;      // FR-085 lever 3, one bit per partial
    std::uint64_t ampSlotDirty_  = 0;
    bool          hasTarget_     = false;

    static constexpr float kTargetRatioEpsilonCents = 0.05f;
    static constexpr float kTargetRatioRelEpsilon   =
        detail::constexprExp(kTargetRatioEpsilonCents / 1200.0f * detail::kLn2) - 1.0f;  // 2.887e-5
    static constexpr float kTargetAmpEpsilon        = 1e-5f;
```

`hasTarget_` and the target arrays are **not** cleared by `reset()` (a target is configuration, like
Richness).

**`reset()` (`:286`) sets both masks to `~std::uint64_t{0}`, i.e. it goes through
`markFreqDirty()` / `markAmpDirty()` — it does NOT clear them.** This is load-bearing and the
earlier draft had it backwards. Verified this session: `HarmonicCloud::reset()` calls
`recalculateFrequencies()` and `recalculateAmplitudes()` **directly and unconditionally**
(`harmonic_cloud.h:309-310`) and only clears `freqDirty_`/`ampDirty_` afterwards (`:321-322`) — it
never goes through the flags. With the §6.4/§6.5 guard `if (hasTarget_ && (freqSlotDirty_ & bit) == 0)
continue;` and the masks zeroed, both loops would iterate 64 times and **write nothing**. That is
not a corner case: `prepare()` recomputes `nyquist_`/`invSampleRate_` and then calls `reset()`
(`:255-283`), so a sample-rate change on a target-active cloud would leave every `epsilon_[i]`
derived from the **old** `invSampleRate_` — every partial rendering at the wrong pitch — and
`recalculateAntiAliasing()` at `:320` would then compute fade/correction from that stale epsilon.
`reset()` is documented RT-safe and publicly callable (`:285`), so it is reachable at runtime, and
SC-014 clause 1 could not catch it because that clause never calls `setSpectralTarget`.

Setting the masks to all-ones makes a `reset()` a full recompute, which is what it was before the
amendment — the amendment stays strictly additive. The other two `recalculate*` call sites are
already flag-guarded and need no change (`:599-606` in `noteOn()`, `:1313-1321` in `updateControl`).
SC-014 gains a clause that sets a target, calls `prepare(96000)` and asserts the rendered partial
frequencies track the new rate (§9.2).

### 6.2 Dirty-flag helpers (surgical, greppable)

```cpp
    void markFreqDirty() noexcept { freqDirty_ = true; freqSlotDirty_ = ~std::uint64_t{0}; }
    void markAmpDirty()  noexcept { ampDirty_  = true; ampSlotDirty_  = ~std::uint64_t{0}; }
```

**The site inventory, enumerated from `grep -n "freqDirty_ = true\|ampDirty_ = true"` run this
session** — there are **four and four**, not "six and some" as an earlier draft claimed, and two of
the `ampDirty_` sites are not setters at all, so a blanket textual replacement would have changed
behaviour at the chunk boundary rather than at configuration time. T5's diff is therefore fully
determined by this table:

| Line | Context | Becomes |
|---|---|---|
| `:360` | `setFundamentalHz` | `markFreqDirty()` |
| `:379` | Richness setter (`N(r)` may move) | `markFreqDirty()` |
| `:380` | Richness setter (rolloff exponent) | `markAmpDirty()` |
| `:393` | inharmonicity setter | `markFreqDirty()` |
| `:406` | tilt setter | `markAmpDirty()` |
| `:445` | gravity setter | `markFreqDirty()` |
| `:602` | inside `noteOn()`, after its guarded `recalculateFrequencies()` | `markAmpDirty()` — a frequency recompute invalidates **every** amplitude slot, so `~0` is the correct mask |
| `:1316` | inside `updateControl`'s step-0 flag consumption (`if (freqDirty_) { recalculateFrequencies(); freqDirty_ = false; ampDirty_ = true; }`) | `markAmpDirty()` — same reason |

A parametric change invalidates every slot; only `setSpectralTarget` sets a partial mask. The
per-slot skip is applied **only when `hasTarget_` is true**, so with no target the two recompute
loops are byte-for-byte the shipped loops.

### 6.3 `setSpectralTarget` / `clearSpectralTarget` / `hasSpectralTarget` (FR-081)

```cpp
void setSpectralTarget(const float* ratios, const float* amplitudes,
                       std::size_t count) noexcept {
    // FR-081 rejection list — AUTHORITATIVE for this entry point (FR-012 defers to it).
    if (ratios == nullptr || amplitudes == nullptr || count == 0 || count > kMaxPartials) return;
    for (std::size_t i = 0; i < count; ++i) {
        if (detail::isNaN(ratios[i])     || detail::isInf(ratios[i])     ||
            detail::isNaN(amplitudes[i]) || detail::isInf(amplitudes[i]) ||
            ratios[i] <= 0.0f || amplitudes[i] < 0.0f) {
            return;                       // wholesale rejection, nothing written
        }
    }
    std::uint64_t fMask = 0, aMask = 0;
    for (std::size_t i = 0; i < kMaxPartials; ++i) {
        const float r = (i < count) ? ratios[i]     : static_cast<float>(i + 1);
        const float a = (i < count) ? amplitudes[i] : 0.0f;
        // COMPARE AGAINST THE COMMITTED VALUE — the one the last recompute consumed —
        // NEVER against targetRatio_/targetAmp_, which this loop is about to overwrite.
        if (!hasTarget_ ||
            std::abs(r - committedRatio_[i]) > committedRatio_[i] * kTargetRatioRelEpsilon) {
            fMask |= (std::uint64_t{1} << i);
        }
        if (!hasTarget_ || std::abs(a - committedAmp_[i]) > kTargetAmpEpsilon) {
            aMask |= (std::uint64_t{1} << i);
        }
        targetRatio_[i] = r;      // latest supplied value; always stored
        targetAmp_[i]   = a;
    }
    hasTarget_ = true;
    if (fMask != 0) { freqDirty_ = true; freqSlotDirty_ |= fMask; }
    if (aMask != 0) { ampDirty_  = true; ampSlotDirty_  |= aMask; }
}
```

**Why the comparison baseline is a separate array — the arithmetic that forced it.** An earlier
draft compared the incoming value against `targetRatio_[i]` and then overwrote `targetRatio_[i]`
unconditionally, including for slots the mask left undirtied — while `recalculateFrequencies()`
skips exactly those slots. The baseline then advances with the input while the computed
`frequencyHz_`/`baseAmplitude_` does not, so **sub-epsilon per-chunk motion accumulates forever and
never triggers a recompute**. At `numStates = 2` and the FR-005 default `kMinTravelRate = 1/600`
journeys/s, Δp per 64-sample chunk at 48 kHz is `2.22e-6` units; for the SineStack→Bell pair,
partial 24 spans `1200·log2(117.67/24) = 2753` cents per unit of `p`, i.e. **0.0061 cents/chunk** —
permanently below `kTargetRatioEpsilonCents = 0.05`. A slot would only ever unfreeze for journeys
faster than ≈ 73 s (partial 24) or ≈ 3.4 s (partial 2), so across the entire range the instrument is
designed for — 10 s to 10 min journeys, FR-061 — most partials would sit frozen at their start
frequency for the whole journey and the phase's central feature would silently not render.

`committedRatio_`/`committedAmp_` are written **only** inside `recalculateFrequencies()` /
`recalculateAmplitudes()`, and only for slots those functions actually recomputed (§6.4, §6.5), so
the comparison baseline can never drift away from the values the audio path is using. Since
`reset()` marks every slot dirty (§6.1), the two arrays are re-synchronised on every reset and
never need separate clearing.

No criterion in the earlier draft would have caught this: SC-001 cl.2 and SC-009 rendered only at
fast travel, SC-004 m.3/m.4 froze travel, SC-014 used static identity arrays and SC-010 measured
only time. §9.2's SC-009 row therefore gains a **slow-travel arm** (clause 3, a 60 s journey)
whose endpoint spectrum assertion fails outright under the stale-baseline form.

`ratios[i] <= 0.0f` rejects `-0.0f` (SC-015 requires it); `amplitudes[i] < 0.0f` accepts `-0.0f`,
which is what FR-081 says. Amplitudes above 1 and above `1 + kMaxAmpJitter` are **accepted** — that
is the whole point of the surface. Nothing is recomputed inside the setter; the masks are the
comparison work, which is 128 compares.

**Deviation D4 (§11):** FR-085 lever 3 specifies "a per-slot compare in the precomputed log domain,
i.e. `|log2 r_new − log2 r_old| > kTargetRatioEpsilonCents / 1200`". Implemented as the equivalent
relative test `|r_new − r_old| > r_old · (2^(0.05/1200) − 1)`, because computing a `log2` per slot
per chunk would cost strictly more than the `exp2` the lever exists to save. The two tests agree to
first order over the whole `[kMinStateRatio, kMaxOutputRatio]` range, and both are four orders below
the smallest perturbation this phase produces.

`clearSpectralTarget()` sets `hasTarget_ = false; markFreqDirty(); markAmpDirty();` — the return to
the parametric path goes through the same dirty-flag path and the same amplitude smoother, so it
cannot click (FR-084, SC-014 clause 3). `hasSpectralTarget()` returns `hasTarget_`.

### 6.4 `recalculateFrequencies()` with a target (FR-082)

The shipped body at `:1064-1093` becomes:

```cpp
void recalculateFrequencies() noexcept {
    const float exponent = 1.0f + gravity_ * kGravityExponentRange;      // UNCHANGED (:1065)
    const bool gravityIsZero = (gravity_ == 0.0f);                       // UNCHANGED (:1082)
    for (std::size_t i = 0; i < kMaxPartials; ++i) {
        if (hasTarget_ && (freqSlotDirty_ & (std::uint64_t{1} << i)) == 0) continue;   // FR-085 lever 3
        const float n = static_cast<float>(i + 1);
        float ratio;
        committedRatio_[i] = targetRatio_[i];   // this slot IS being recomputed now
        if (hasTarget_ && targetRatio_[i] != n) {
            // FR-082: the branch is scoped to the WARP FACTOR ALONE.
            const float warp = gravityIsZero
                ? 1.0f
                : std::exp2(gravity_ * kGravityExponentRange * detail::kHarmonicCloudLog2N[i]);
            ratio = targetRatio_[i] * warp;
        } else {
            // No target, or the FR-082 identity guard: fall back to the UNMODIFIED
            // parametric ratioG of :1085-1086 INCLUDING its own gravityIsZero branch.
            // Falling back to the std::exp2 arm alone would evaluate exp2(1.0f*log2N[i]),
            // which is exactly the rewrite the comment at :1066-1072 warns hands back
            // 31.999998 for n = 32 under -ffast-math — destroying the bit-exactness the
            // guard exists to protect, invisibly to SC-014's fingerprint tolerances.
            ratio = gravityIsZero ? n
                                  : std::exp2(exponent * detail::kHarmonicCloudLog2N[i]);
        }
        const float stretch = std::sqrt(1.0f + inharmonicity_ * n * n);   // UNCHANGED (:1087)
        const float f = fundamentalHz_ * ratio * stretch;                 // UNCHANGED order (:1088)
        frequencyHz_[i] = f;                                              // UNCHANGED (:1089)
        epsilon_[i] = std::clamp(2.0f * std::sin(kPi * f * invSampleRate_),
                                 -kMaxEpsilon, kMaxEpsilon);              // UNCHANGED (:1090-1091)
    }
    freqSlotDirty_ = 0;
}
```

Phase accumulators are still never touched (:1062-1063), so a frequency change stays
phase-continuous by construction.

### 6.5 `recalculateAmplitudes()` with a target (FR-083)

Only the one factor at `:1155-1156` changes:

```cpp
        if (hasTarget_ && (ampSlotDirty_ & (std::uint64_t{1} << i)) == 0) continue;
        committedAmp_[i]  = targetAmp_[i];      // this slot IS being recomputed now
        baseAmplitude_[i] = hasTarget_
            ? targetAmp_[i] * tiltGain(i)
            : std::exp2(-exponent * detail::kHarmonicCloudLog2N[i]) * tiltGain(i);
```

**The function ends with `ampSlotDirty_ = 0;` immediately before
`normGain_.setTarget(currentNormGainTarget())`** — mirroring `freqSlotDirty_ = 0;` at the end of
§6.4, and placed there because FR-083 requires `setTarget` to remain the *last* statement. Without
it the mask saturates after the first few chunks (`setSpectralTarget` accumulates with
`ampSlotDirty_ |= aMask`) and every subsequent call recomputes all 64 slots — the amplitude half of
FR-085 lever 3 would be dead code. SC-010 clause 3 would **not** catch that: an unchanged target
raises no dirty flag at all, so the function is never entered. §9.2's SC-010 clause-2 row therefore
states that the measured configuration exercises **both** masks.

Unchanged: the Richness count law `N(r)` (`:1138-1139`) and its `activeCount_` write, the zeroing of
slots at or above `activeCount_` (`:1147-1150` — which must run **before** the dirty-slot skip so a
Richness reduction still silences a slot), the FR-043 tail high-water logic (`:1163-1164`), and the
`normGain_.setTarget(currentNormGainTarget())` that must remain the **last statement of the
function** (`:1166-1172`). Richness's rolloff exponent has no effect while a target is active — that
is deliberate (C-3): multiplying a state's own shape by `n^(−p)` would erase the timbral distinction
SC-008 exists to protect.

The normalizer's input is the **entropy-perturbed** amplitude set, so entropy is level-neutral by
construction and the dissolve is purely spectral (FR-083, C-4/Q4). SC-004 metric 4 records the
consequence as a number.

### 6.6 FR-086 — the composition cadence, documented in both headers

The `spectral_morph_engine.h` class-level doc and the `setSpectralTarget` doc comment in
`harmonic_cloud.h` both carry, verbatim:

```cpp
// A consumer driving HarmonicCloud from a SpectralMorphEngine MUST do so in slices of
// <= HarmonicCloud::kControlChunkSamples (= 64) samples, in this order:
//
//   for (each slice of <= 64 samples) {
//       engine.updateChunk(n);
//       cloud.setSpectralTarget(engine.getOutputRatios(),
//                               engine.getOutputAmplitudes(),
//                               engine.getOutputCount());
//       cloud.processStereoBlock(left + offset, right + offset, n);
//   }
//
// WHY A BOUND AND NOT A SUGGESTION: processStereoBlock restarts its internal 64-sample
// control grid on every call (harmonic_cloud.h:713-716) and setSpectralTarget only raises
// freqDirty_/ampDirty_, consumed at the head of the FIRST updateControl of that call
// (:1313-1321). A target supplied once per 512-sample host block is therefore frozen for
// all 8 internal chunks and the morph's effective resolution silently becomes the host
// block size.
```

No production component in this phase includes both headers (Non-Goals); the composition lives only
in SC-004 metric 3's, SC-009's and SC-010's harnesses, which drive exactly this shape.

---

## 7. Layer discipline and ODR

| New symbol | Layer | Header | Sweep |
|---|---|---|---|
| `SpectralState`, `SpectralStateId`, `isValidSpectralState`, `normalizeSpectralState`, `makeFactoryState`, `serializeSpectralState`, `deserializeSpectralState`, `kSpectralStateCount`, `kSpectralStateFormatVersion`, `kSpectralStateBytes` | 2 | `processors/spectral_state.h` | clear (spec.md:2169, 2174-2179) |
| `EntropyProcessor` (+ nested `LifePhase`) | 2 | `processors/entropy_processor.h` | clear (spec.md:2171) |
| `SpectralMorphEngine` (+ nested `TravelMode`) | 3 | `systems/spectral_morph_engine.h` | clear (spec.md:2170) |
| `deriveStreamSeed` | 0 | `core/random.h` | clear (spec.md:2177) |
| `centsToPitchRatio` | 0 | `core/pitch_utils.h` | clear (spec.md:2177) |
| `detail::kLn2` (namespace-scope constant, §2.3) | 0 | `core/db_utils.h` | **swept this session** over `dsp/` and `plugins/`: no namespace-scope `kLn2` exists in `Krate::DSP`. The only hits are two **function-local** `constexpr float kLn2` inside `detail::constexprLn` (`db_utils.h:84`) and `detail::constexprExp` (`db_utils.h:130`) — both deleted by this edit and replaced by the shared constant — plus a class-scoped `TanhADAA::kLn2` (`tanh_adaa.h:171`), which is a different entity and is untouched. **Exactly one definition**, so no TU can see two. |
| `EntropyProcessor::kWalkLimit`, `EntropyProcessor::kDenormalFloor` (§4.1) | 2 | `processors/entropy_processor.h` | **Class-scoped**, so no namespace-scope collision is possible. Recorded here because they are *transcriptions* of `BrownianDrift`'s **private** `:226`/`:228`, not reuses; the identical precedent is `HarmonicCloud::kDriftWalkLimit`/`kDriftDenormalFloor` (`harmonic_cloud.h:156-157`). `brownian_drift.h` is **not** modified. |
| `EntropyProcessor::kEntropyAmpSmoothMs`, `kEntropyCentsSmoothMs`, `onePoleChunkStep` (§4.1) | 2 | `processors/entropy_processor.h` | Class-scoped; no sweep hazard. Listed so §7 stays a complete inventory of what this phase adds. |
| `HarmonicCloud::committedRatio_`, `committedAmp_` (§6.1) | 3 | `systems/harmonic_cloud.h` | Private members of an existing class; no sweep hazard. |

Include direction, asserted by reading each new header's include block:
`spectral_state.h` → Layer 0 only; `entropy_processor.h` → Layer 0/1 + `brownian_drift.h` and
`spectral_state.h` (both Layer 2, the established intra-layer pattern);
`spectral_morph_engine.h` → Layer 0/1/2 only (`brownian_drift.h` included, Layer 3 → 2), and
**never** `harmonic_cloud.h`.

**Any additional class, struct, free function or namespace-scope constant introduced during
implementation must be re-swept before it is written** (roadmap line 485). In particular no new
namespace-scope partial-count constant may be added beside `Krate::DSP::kMaxPartials = 96`
(`harmonic_types.h:21`).

---

## 8. CPU budget (SC-010) — cost model and the ordered levers

**T0.2's spike has not been run**, so this is a derived model, not a measurement. It is written down
so the first measurement is entered with an expectation that can be falsified. Per §1 T0.2 the
prerequisite's *timing* is amended (RA-4, landing in T9) and the response to a miss is pre-decided
below — the phase cannot stall on an undecided question.

**Clause 1 budget:** 0.15 % of one core = `kMorphReferenceNsPerBlock ≈ 16,000 ns` per 512-sample
block at 48 kHz (8 control chunks).

Derived per-block cost of `SpectralMorphEngine` at SC-010's measured configuration (4 states,
`bloom = 1`, `entropy = 1`, Spline travel):

| Item | Per block | Estimate |
|---|---|---|
| Two 64-lane OU banks (16 control steps × 2 × 64 lanes, 3 RNG draws each; 32 smoother advances × 64 lanes) | — | **~8,500–10,500 ns** |
| Morph `exp2` (8 chunks × 64) | 512 | ~2,000 ns |
| Entropy cents→ratio (8 chunks × 64, one combined call — D3) | 512 | ~2,000 ns |
| Interpolation + repair + absorption + copies (8 × ~400 float ops) | — | ~1,000 ns |
| Lifecycle FSM + Spline advance | — | ~500 ns |
| **Total** | | **~14,000–16,000 ns** |

The OU figure is not a guess: `harmonic_cloud_perf_test.cpp`'s BASELINE PROVENANCE block records the
**quiescent** path — which is two 64-lane banks and nothing else, plus two 512-float fills — at
9,166–10,998 ns/block on the reference machine (:116). Phase 3 needs exactly two such banks.

**This clause is on the line, and that is the phase's principal risk (R1).** Levers, in the order
they are to be spent, each pre-designed here so the implementer does not improvise under budget
pressure:

1. **Log-domain morph pipeline** (D1) — one `exp2` per partial per chunk instead of three.
   **Adopted from the start**, not conditional.
2. **Combined stage-2/3 conversion** (D3) — halves the entropy transcendental count.
   **Adopted from the start.**
3. **Exact-zero fast paths.** `bloom == 0.0f` skips the completion recompute (`invE` is exactly
   `1.0f`); `entropy_ == 0.0f` skips `applyStages` entirely; `fadeX_ >= 1.0f` skips absorption;
   `A == B` (the FR-005 default and any single-state configuration) skips the interpolation lerps.
   None of these fire in clause 1's measured configuration, but all of them are what Phase 7 pays in
   practice, and each is an explicit branch (`-ffast-math`).
4. **`centsToPitchRatioFast` (conditional, and the one that moves clause 1).** If the measurement
   misses 16,000 ns, promote the bounded-domain degree-4 Horner of
   `HarmonicCloud::detail::centsToDriftRatio` (`harmonic_cloud.h:105-110`, measured worst case
   6.15e-08 relative on `[-50, +50]` cents per its own comment at :95-100) into `core/pitch_utils.h`
   under the new Layer 0 name `centsToPitchRatioFast`, and rewrite `detail::centsToDriftRatio` as a
   one-line forward — the same shape as FR-006's `deriveSeed` forward, covered by the same SC-014
   clause 1 regression gate. Entropy's cent domain is `±11.0`, 4.5× inside the polynomial's
   documented window. Expected saving ~1,700 ns/block. **This is a third amendment to a COMPLETE
   Phase 2 component and needs the same recording treatment as RA-1 — see §14 open item 2.**
5. **Entropy OU control interval 32 → 64 samples (conditional, the largest remaining lever).**
   The dominant cost is the two 64-lane OU banks' control steps: 16 per block at
   `BrownianDrift::kControlRateInterval = 32`, each drawing 3 `nextFloat()` values on each of 64
   lanes (6,144 draws/block). Taking the entropy banks' own control interval to **64** samples
   halves that to 8 steps/block and **~3,000–4,500 ns/block** — comfortably more than the gap the
   model predicts. It costs nothing musically: the two walks have `tau` of 3 s and 8 s, so a 1.33 ms
   control grid is still ~2,250× faster than the fastest of them, and the exact discretisation
   (§4.4) simply re-derives `a`/`g` from the doubled `dt` — it is not an approximation.
   Consequences, stated so the implementer does not discover them under budget pressure:
   (a) `EntropyProcessor` gains its own `kEntropyControlInterval` and stops reusing
   `BrownianDrift::kControlRateInterval`; (b) the two banks are then no longer step-comparable to a
   stock `BrownianDrift`, so §9's `EntropyProcessor_OuBankMatchesBrownianDrift` is **replaced** in
   the same commit by an explicit-coefficient check (`a == exp(-dt/tau)`, `g == kInternalStd·sqrt(1-a²)`
   at `dt = 64/fs`, at `1e-6` relative) plus the unchanged draw-order and lane-seed assertions —
   the check must be replaced, never deleted; (c) SC-013's chunk-length invariance grid must be
   re-run, since the control grid changed.
6. **There is no lever 6, and the baseline is never raised.** SC-010 states the response to a missed
   clause-1 measurement is to reduce cost, never to raise `kMorphReferenceNsPerBlock`. RA-3's escape
   is explicitly scoped to clause 2 only. If levers 1–5 are spent and clause 1 still misses, that is
   an honest finding requiring a spec amendment, reported as such rather than absorbed.

**Pre-decided ladder for a clause-1 miss at T7** (so the phase cannot stall): spend lever 4, re-run;
if still over, spend lever 5 and re-run the SC-013 grid; if still over, stop and report — with the
measured figures and the levers already spent — as a finding against SC-010, and do **not** touch
`kMorphReferenceNsPerBlock`.

**Clause 2** (cloud with a changing target every chunk, inside Phase 2's existing 0.5 % envelope):
the per-slot dirty mask (§6.2/§6.3) is the mechanism. Under a slow travel (`kMinTravelRate` = a
10-minute journey) most slots move less than `kTargetRatioEpsilonCents = 0.05` cents per chunk and
are skipped; at `kMaxTravelRate` most slots move and the full recompute is paid. The clause is gated
by a new checked-in `kCloudChangingTargetBaselineNs` under **both** shipped relations,
`static_assert`ed in the shape of `harmonic_cloud_perf_test.cpp:142` and `:149`:

```
kCloudChangingTargetBaselineNs * 1.5 <= kReferenceNsPerBlock          // <= 53,333 ns
kCloudChangingTargetBaselineNs       <= kMaxAdmissibleBaselineNsPerBlock   // <= 35,555.6 ns
kCloudChangingTargetBaselineNs / 26,000 <= 1.36                       // vs the shipped no-target baseline
```

**Clause 3** (`measuredUnchangedTargetNs ≤ measuredCloudBaselineNs × 1.10`) is structurally satisfied
by the same mask: an unchanged target sets no bits, raises no dirty flag, and costs 128 compares per
chunk. The three figures are **per-run measurements**, named without a `k` prefix, gated against
each other within one run of one TU — never two checked-in literals.

Measurement basis is identical to Phase 2 SC-007: ns per 512-sample block, best-of-N, percentage
against the 10.667 ms wall-clock budget, `[.perf]`-tagged (no CI leg evaluates perf cases —
`.github/workflows/ci.yml` filters `'~[performance]~[perf]~[benchmark]~[!benchmark]'`).

---

## 9. Test plan

### 9.1 Files

| File | Target | Covers |
|---|---|---|
| `dsp/tests/unit/core/random_test.cpp` (**extend**) | `dsp_core_tests` | `deriveStreamSeed` Layer 0 properties |
| `dsp/tests/unit/core/pitch_utils_test.cpp` (**extend**) | `dsp_core_tests` | `centsToPitchRatio` equivalence + identity |
| `dsp/tests/unit/processors/spectral_state_test.cpp` (**new**) | `dsp_processors_tests` | SC-007, SC-008 clauses 1/2/4 |
| `dsp/tests/unit/processors/entropy_processor_test.cpp` (**new**) | `dsp_processors_tests` | SC-005, SC-006, SC-016, constant pins |
| `dsp/tests/unit/systems/spectral_morph_engine_test.cpp` (**new**) | `dsp_systems_tests` | SC-001 cl.1, SC-002, SC-003, SC-004 m.1–2, SC-011, SC-012, SC-013, SC-015 |
| `dsp/tests/unit/systems/spectral_morph_render_test.cpp` (**new**) | `dsp_systems_tests` | SC-001 cl.2, SC-004 m.3–4, SC-008 cl.3, SC-009, SC-014 |
| `dsp/tests/unit/systems/spectral_morph_perf_test.cpp` (**new**) | `dsp_systems_tests` | SC-010 (`[.perf]`) |

**Pinned seed set, shared by SC-004, SC-005, SC-006, SC-016 and SC-002 clauses 3–4:**
`kSeeds[8] = { 1, 7, 13, 29, 101, 257, 1009, 65537 }`, declared once per TU as a file-scope
`constexpr std::array<std::uint32_t, 8>`. SC-001 clause 2 uses its first four.

**Note lifecycle — applies to EVERY rendered row (SC-001 cl.2, SC-004 m.3/m.4, SC-008 cl.3, SC-009,
SC-014).** Each render calls **`cloud.noteOn()` before the first slice and never calls `noteOff()`**
for the duration. This is not harness detail: verified this session, a freshly-prepared
`HarmonicCloud` has `gate_ = false` and every envelope idle, so `isQuiescent()` is true
(`harmonic_cloud.h:844-853`) and `processStereoBlock` takes the early-out that fills both channels
with zeros (`:701-709`). Without the note, every rendered criterion measures silence and SC-009's
`RMS ≥ −60 dBFS over every 100 ms window` gate fails on a correct implementation. The FR-086 doc
block in §6.6 is the **per-slice loop only** and presumes an already-gated cloud; the render rows
below pin the note around it.

### 9.2 Per-criterion assertion strategy

| SC | File · `TEST_CASE` | Assertion strategy |
|---|---|---|
| **SC-001 cl.1** | engine · `SpectralMorph_TravelIsContinuous` | 18 configurations (`bloom ∈ {0,.5,1}` × `entropy ∈ {0,.5,1}` × `TravelMode`), plus the pinned state loads (2-state: SineStack/Bell; 4-state: SineStack/Bell/Glass/Breath; one adversarial arm `{numPartials=2, ratios={0.5,128}}` vs SineStack). Full out-and-back sweep at `kMaxTravelRate`, 64-sample chunks at 48 kHz. Per chunk: `max_i |Δa_i| ≤ kMaxAmpDeltaPerChunk`; `max_i |1200·log2(r_i/r_i^prev)| ≤ kMaxRatioDeltaCentsPerChunk` **for `L_i > 0` only**. `setState`, `setStateCount` and a `setTravelMode` switch are performed mid-sweep. `setSeed`/`reset` are **never called** (stated in a comment beside the loop). The two constants are pinned by the §5.1 `static_assert`s, so the test cannot be satisfied by loosening them. |
| **SC-001 cl.2** | render · `SpectralMorph_TravelIsContinuous_Rendered` | SineStack/Bell, `numStates=2`, f0 110 Hz, 48 kHz, `bloom=0.5`, External at `kMaxTravelRate`, 20 s, `entropy ∈ {0,1}`, seeds `{1,7,13,29}`; each render paired with a **travel-frozen, drift-live** control at the journey midpoint, same seed/entropy. `ClickDetectorConfig` pinned: `sampleRate=48000.0f` (the struct default is 44100 — must be overridden), `frameSize=512`, `hopSize=256`, `detectionThreshold=5.0f`, `energyThresholdDb=-60.0f`, `mergeGap=5`. Pass rule per pair and per entropy-arm/channel median: `moving ≤ 1.15·frozen + 5`. **The 1.15/+5 figures must be re-derived from the measured 4-seed spread during implementation and written into the test with the observed numbers**; if the spread does not fit, raise the seed count to 8 — never widen the margin. |
| **SC-002 cl.1** | engine · `SpectralMorph_EndpointsAreExact` | Factory five only, all 10 pairs, `bloom=entropy=0`. At `p=0`: for `i < state[0].numPartials`, ratio and amplitude within `1e-6` relative of `state[0]`; for `i ≥ numPartials`, amplitude **exactly** `0.0f` and ratio equal to the FR-041 continuation (recomputed in the test from the same recurrence). Symmetrically at `p = numStates−1`. Monotone `u_i` over an increasing `p` sweep: violation count must be 0. |
| **SC-002 cl.2** | engine · same | `bloom=0`, all 10 factory pairs, full sweep: `getRepairEngagementCount()` delta must be **0**. At `bloom=1` the count is **reported, not gated**, and clause 1's endpoint tolerances re-asserted. |
| **SC-002 cl.3** | engine · `SpectralMorph_SplineTravelReachesEndpoints` | Spline, `numStates=2`, **`setTravelRate(kMaxTravelRate)`**, `setWaypointInterval(2.0)`, `setDepth(1.0)`, **≥ 1200 s** per seed × 8 seeds: position comes within `0.02` of both `0` and `1` at least once per seed, and stays in `[0,1]` throughout. Run length is derived (0.98^600 ≈ 5.4e-6 per endpoint per seed); if shortened the arithmetic must be redone, never the tolerance widened. |
| **SC-002 cl.4** | engine · `SpectralMorph_SplineLimiterHasHeadroom` | Spline, `numStates=4`, `setTravelRate(kMaxTravelRate)`, `setWaypointInterval(SplineTrajectory::kDefaultInterval)`, `setDepth(1.0)`, ≥ 300 s × 8 seeds: `getLimiterActiveChunks()/getTotalChunks() < 0.01`, measured fraction reported. Honest expectation is exactly 0 (2.25 u/s analytic worst case vs a 3.0 u/s cap). |
| **SC-002 cl.5** | engine · `SpectralMorph_DefaultsAreAudible` | Default-constructed + `prepare(48000)`, **no** parameter call. Asserted at that point **and again after one `updateChunk(64)`**: `getOutputCount()==64`; `getOutputRatios()[i] == float(i+1)` within `1e-6` rel; `getOutputAmplitudes()[i]` within `1e-6` rel of `makeFactoryState(SineStack).amplitudes[i]` and **non-zero for every i**; travel position, bloom, entropy exactly `0.0f`; mode `External`; rate `kMinTravelRate`. Then 200 further `updateChunk(64)`: every element **bitwise** unchanged. |
| **SC-003** | engine · `SpectralMorph_BloomStaggersLowToHigh` | `bloom=1` at `u=0.5`: `u_1 == 1.0f`, `u_64 < 1.0f`, sequence non-increasing in index, `u_1 − u_64 ≥ kMinBloomSpread = 0.3`. `bloom=0`: all 64 equal to `u` within `1e-7` (the exact branch makes it bitwise). Bounds `u_i ∈ [0,1]` at every bloom × reachable `u`. **Join clause as limit + handoff:** at `u = 0.99999` every `u_i ≥ 1 − 1e-5`; stepping `p = k+0.99999 → k+1` yields state `k+1` within `1e-6` rel (scoped exactly as SC-002 cl.1) with the FR-044 delta bound satisfied across the crossing. Never asserted at the unreachable `u = 1`. |
| **SC-004 m.1–2** | engine · `EntropyProcessor_DisorderIncreasesMonotonically` | ≥ 11 settings incl. 0, 0.25, 0.35, 0.50, 0.60, 0.75, 0.85, 1. Metric 1 = mean over partials of `|1200·log2(r_i/r_i^clean)|` from `getOutputRatios()` vs `getCleanRatios()`. Metric 2 = mean over partials with `a_i^clean > 1e-4` of `|a_i − a_i^clean|/a_i^clean`. Strict increase over the driving stage's interval by **≥ 5× the test's own reported standard error**; non-decrease elsewhere at 1× SE. Averaged over **8 seeds × ≥ 10 τ** (τ = 8 s ⇒ ≥ 80 s per seed), first 2 τ discarded. Anchor: `metric2(0.35) ∈ [0.15, 0.21]` with the `kMaxAmpJitter·kInternalStd·sqrt(2/π) = 0.1995` derivation recorded beside it, **plus the D12 correction**: the amplitude bank's output smoother (tau 0.150 s) attenuates the walk's stationary std by `sqrt(tau_walk/(tau_walk + tau_smooth)) = sqrt(3.0/3.15) = 0.976`, so the expected value is `0.1947`, still well inside the band. |
| **SC-004 m.3** | render · `EntropyProcessor_FlatnessRisesWithEntropy` | Pinned signal: `numStates=2` with **SineStack in both slots**, External, travel frozen at `p=0`, `bloom=0`, 8 seeds averaged. Rendered through the FR-086 shape (≤64-sample slices) at f0 110 Hz / 48 kHz, ≥ 10 s per setting, cloud config = SC-009's. ≥ 6 non-overlapping 65536-point Blackman-Harris windows via `spectral_analysis.h`, bin-wise magnitude average, then `flatness = exp(mean_k log m_k)/mean_k m_k` over bins `[2, 16384)` **computed inline** — with §0.1 item 4's **two-function** sweep recorded in a comment (the time-domain `SignalMetrics` overload caps the FFT at 4096; the magnitude-domain `Krate::DSP::calculateSpectralFlatness(spectral_utils.h:335)` fits the shape but skips `<= 1e-10f` bins and divides by `validBins`, so its denominator moves with the signal and the metric stops being comparable across settings, which is the one comparison this row makes). Gate: `flatness(0.75) ≥ 1.25 · flatness(0)`, enforced over `[0, 0.75]` only; over `[0.75, 1]` the only requirement is `flatness(1) ≥ flatness(0)`. A first measurement below 1.25 is a finding about the FR-072 cent constants being too small — raise them inside FR-074's 12-cent budget, never lower the ratio. |
| **SC-004 m.4** | render · `EntropyProcessor_IsLevelNeutral` | Exactly the m.3 renders. Broadband stereo RMS in dBFS at each of the ≥ 11 settings, first 0.5 s discarded (the 20 ms `kNormGainSmoothMs` smoother settles). **Every value reported.** Gate `max − min ≤ 3.0 dB`. A spread above 3.0 dB is a finding about the composition, not a threshold to widen. |
| **SC-005** | entropy · `EntropyProcessor_StagesEngageInOrder` | 8 seeds. `e=0.10`: every ratio deviation **bitwise 0**, every `getLifePhase == Alive` with `getLifeAmplitudeFactor` exactly `1.0f`, at least one `getAmpJitterFactor != 1.0f`. `e=0.40`: ratio deviation non-zero, **`getAppliedScatterCents(i)` exactly `0.0f` for every i**, no deaths. `e=0.65`: applied scatter non-zero for ≥ 1 i, no deaths. `e=0.90`, ≥ 60 s: ≥ 1 partial completed `Dying→Dead→Reborn` and its **`getRawScatterDraw`** differs across the cycle. (Derivation: `P(none of 64 dies) ≈ 1e-46`.) |
| **SC-006** | entropy · `EntropyProcessor_BoundedAtEverySetting` | 11 settings × 8 seeds × ≥ 60 s of chunks. Every amplitude finite, `≥ 0`, `≤ 1.5 ×` its input; every ratio finite, `> 0`, within `±11.0` cents of input; array strictly increasing **every chunk**. Amplitude delta bound holds for every partial and for `L_i` itself. Ratio (cents) delta bound asserted **only where `L_i > 0`**. Complementary clause: **every** redraw observed through `getScatterRedrawCount(i)` occurred on a chunk where `L_i` was **bitwise** `0.0f`. Non-finite detection by bit pattern, never `std::isnan`. |
| **SC-007** | state · `SpectralState_SerializationRoundTrips` | 5 factory + ≥ 3 edge states (`numPartials` 0 / 1 / 64 with extremal metadata). `serialize` returns exactly `kSpectralStateBytes`; `deserialize` returns true and reproduces every field **bitwise**; the byte-stream FNV digest matches a checked-in golden per factory state, **labelled in the test as a stored-value digest, explicitly not a render golden**. Negatives: capacity one byte short → 0, nothing written; corrupted version byte → false, `out` untouched; FR-012-violating payload (non-monotone ratios) → false. |
| **SC-008 cl.1** | state · `SpectralState_FactoryStatesAreDistinct` | All five `isValidSpectralState` true and L2-normalised to `1e-5`. `max_{i < numPartials} ratios[i] ≤ kMaxStateRatio` (**scoped — deviation D6**); separately, over all 64 slots, strictly increasing and `≤ kMaxOutputRatio`. `name` non-empty and NUL-terminated. |
| **SC-008 cl.2** | state · same | All 10 pairs: `d(A,B) > 0.4` with the amplitude term over all 64 slots and the ratio term over `[0, min(A.np, B.np))`, `λ=1.0`. Plus the regression pin `kMeasuredClosestPairDistance = 0.5258f` ±10 % on the SineStack/Choir pair. |
| **SC-008 cl.3** | render · `SpectralState_MetadataNeverReachesAudio` | Load a factory state into an engine, render through the SC-009 cloud config; repeat with `tiltDbPerOct = +12`, `inharmonicity = 0.1`, `name` overwritten. The two renders must be **bitwise identical**. (Structurally guaranteed by D10, but asserted anyway — FR-013 otherwise has no criterion that would fail.) |
| **SC-008 cl.4** | state · same | Two `makeFactoryState(id)` calls separated by `10^6` draws from a shared `Xorshift32` are bitwise identical for all five ids, and `rng.state()` (`random.h:78`) is unchanged **across each call**. |
| **SC-009** | render · `SpectralMorph_FactoryPairRenders` | All 10 pairs, engine → cloud **in the FR-086 shape** (≤64-sample slices, zero-copy through the FR-008 accessors) at f0 110 Hz / 48 kHz, `bloom=0.5`, `entropy=0`, External. Cloud pinned: `richness=1.0` (so `N(r)=64`), `spectralTiltDb=0`, `mutation=0`, `spectralGravity=0`, `inharmonicity=0`, drift depth 0, spread 0, envelope times minimum, **`noteOn()` before the first slice, no `noteOff()`**. **The render SHAPE is pinned, because the criterion's own measurement depends on it** (an earlier draft pinned everything except the two things that matter and was not measurable — see below): **`setTravelRate(0.125f)`** so `slewCap = 0.125 · (numStates−1) = 0.125` units/s and the 1-unit journey occupies exactly **8 s**, and a **three-phase render** — `setTargetPosition(0)` and hold **2.5 s**, then `setTargetPosition(1)` and the **8 s** journey, then hold **2.5 s** at `p = 1` — **13.0 s total**. Endpoint transforms are taken **inside** the frozen windows, starting 0.5 s in (so the 20 ms `kNormGainSmoothMs` smoother and the envelopes have settled) and running 65536 samples = 1.365 s, which fits the 2.5 s window with 0.635 s to spare. **Clause 1:** no non-finite sample (bit pattern); `peak < 0.9 × kOutputClamp`; RMS over every non-overlapping 100 ms window `≥ -60.0 dBFS`; endpoint spectra match the two amplitude sets within **`kEndpointMagnitudeToleranceDb` = 1.0 dB per partial** on **normalized** magnitudes (each divided by partial 1's, so the FR-017 gain cancels). The bracketing sample is taken from a **separate 2.5 s render frozen at `p = 0.5`** (deviation **D16**): a 65536-point transform is 1.365 s, 17 % of the journey, so a mid-journey window taken *during* travel smears across a moving spectrum and the per-partial claim is not measurable at all — least of all for the Bell pairs, whose high slots move by hundreds of cents. Bracketing is asserted for `i < min(A.np, B.np)` only, anti-alias-faded partials excluded and the exclusion count asserted. **Clause 2:** one render per pair at drift depth `kMaxDriftCents` and mutation 1.0 asserting **only** finiteness, peak and non-silence — deliberately no spectral-shape claim. **Clause 3 — the slow-travel arm (new; it is the criterion that fails under the §6.3 stale-baseline defect).** SineStack/Bell only, same cloud pin, **`setTravelRate(1.0f/60.0f)`** so the 1-unit journey occupies **60 s**: 2.5 s frozen at `p = 0` → 60 s journey → 2.5 s frozen at `p = 1`, 65 s total, endpoint transforms taken in the frozen windows exactly as clause 1. Assert the `p = 1` endpoint spectrum reaches **state B** within `kEndpointMagnitudeToleranceDb`. Under a `setSpectralTarget` whose dirty test compares against the *stored* target rather than the *committed* one, per-chunk motion at this rate is ~0.05–0.5 cents for the low partials and 0.006 cents for partial 24, so most slots never unfreeze and this assertion fails outright. Each render additionally pinned with `render_fingerprint.h` at its published tolerances, **labelled as a regression pin, not a correctness proof**. |
| **SC-010** | perf · `SpectralMorph_CpuBudget` `[.perf]` | Three clauses per §8. Clause 1 config: 4 states, `bloom=1`, `entropy=1`, Spline at `kDefaultInterval`. Clauses 2–3: Phase 2 SC-007's cloud configuration (both drift banks live, mutation 1.0, the 64-sample chunked loop), differing only in whether/how a target is supplied. **Clause 2's changing-target configuration must exercise BOTH per-slot masks** — the supplied targets move in ratio *and* in amplitude every chunk (a mid-journey `bloom = 0.5` traversal at `kMaxTravelRate` does exactly that) — so a missing `ampSlotDirty_ = 0` (§6.5) shows up as a cost, not as silent dead code. Checked-in `k*BaselineNs` constants carry a provenance block in the shape of `harmonic_cloud_perf_test.cpp:104-119` and both `static_assert`s in the shape of `:142`/`:149`. Per-run measurements use no `k` prefix. Clause 3 gates a measurement against another measurement from the same run. The injection cost `measuredChangingTargetNs − measuredCloudBaselineNs` is **reported only** — never a gate (a difference of two sampled minima is a biased statistic). |
| **SC-011** | engine · `SpectralMorph_NoAllocInSteadyState` | Liveness first (Phase 2 pattern): a real `new int[16]` inside an `AllocationScope` with `REQUIRE(livenessCount >= 1)`, proving the overrides are linked. Then a steady-state loop over `updateChunk`, `processChunk`, `setEntropy`, `setBloom`, `setTargetPosition`, `setState`, `setSpectralTarget`, `processStereoBlock` with `REQUIRE(count == 0)` after `prepare()`, plus a non-vacuity RMS > 0. **This TU must not include `allocation_operator_overrides.h`** — both binaries already have it (§0.1 item 5). Plus `static_assert(noexcept(...))` over the full public surface of both new classes. |
| **SC-012** | engine · `SpectralMorph_DeterministicUnderSeed` | Two instances, same seed, same `prepare(48000)`, same call sequence, `entropy=1`, Spline: output arrays **bitwise identical** over ≥ 500 chunks. **Rewind clause, restated to match §5.6's `reset()` semantics:** `reset()` reproduces the arrays that a **freshly-prepared instance carrying the same parameter set** produces — *not* the arrays of a bare post-`prepare` instance. Stated as written the criterion is unsatisfiable by a faithful implementation: the test's own configuration is `entropy = 1`, so after `reset()` the scatter draws are redrawn and applied at `w_3 = 1`, while a bare post-`prepare` instance was clean at entropy 0. Concretely: instance X is prepared, configured (states, count, bloom, entropy, mode, rate, seed), advanced ≥ 500 chunks, then `reset()`; instance Y is prepared and given the **identical** configuration calls in the identical order; the two output arrays must then be **bitwise identical**, and X's configuration getters must be unchanged across its `reset()` (asserted explicitly — it is what proves `reset()` rewinds rather than reconfigures). Seed 0 is safe. All `4 × 64 = 256` derived stream seeds pairwise distinct and non-zero, asserted **directly on the Layer 0 `deriveStreamSeed`**, for at least the 8 pinned seeds. Second clause: `HarmonicCloud::deriveSeed(b,s) == deriveStreamSeed(b,s)` over the same input set — the proof that FR-006's forwarding rewrite left Phase 2's streams untouched. **Third clause — the FR-075 advance invariant (§4.6):** run A advances an `EntropyProcessor` for N chunks of 64 samples at a fixed `count = 64`; run B does the same but for chunks `k ∈ [100, 200)` passes `count = 0` (the `outCount_ == 0` configuration two default-constructed states produce). Every lifecycle phase, `getRawScatterDraw`, `getScatterRedrawCount`, `getAmpJitterFactor` and `getDecoherenceCents` must be **bitwise identical** between the two runs at the end. |
| **SC-013** | engine · `SpectralMorph_SampleRateInvariant` | Rates `{44100, 48000, 96000}` × chunk lengths `{1, 7, 64, 512, 4096, **65536**}`. **The 65536 entry is required by FR-063, not padding** (spec.md:985-987 names "chunk lengths far longer than a waypoint interval — `SplineTrajectory::advance` rotates as many waypoints as needed"). The earlier grid topped out at 4096 samples = 85 ms, while `SplineTrajectory`'s **shortest legal** interval is `kMinInterval = 0.5 s` = 24,000 samples at 48 kHz (`spline_trajectory.h:117`), so no listed case ever exercised the rotation path and a broken multi-waypoint rotation would have passed every criterion. The 65536 case runs in **Spline** mode at `setWaypointInterval(SplineTrajectory::kMinInterval)` — 1.365 s per chunk, ≥ 2 waypoints rotated per call — asserting the position stays finite and in `[0, numStates−1]` and **still advances** (a non-zero position change over the run). Journey clause: `numStates=2`, External at `kMaxTravelRate` ⇒ nominal 1.000 s; measured duration within `max(0.5 % of nominal, one chunk duration)`, **both terms reported** so it is visible which bound. Entropy stationary metric agrees across rates within `max(5 % relative, 5 × the larger reported SE)`. FR-044 bound met at every rate/length, with the test **scaling** the constants by `chunkSeconds` rather than re-deriving. `prepare()` with a new rate leaves no stale coefficient. |
| **SC-014** | render · `HarmonicCloud_SpectralTargetIsNeutralWhenIdentity` | **cl.1:** no `setSpectralTarget` ever called; render over the 216-cell grid compared against `kPreAmendmentFingerprints[...]` captured in T0.1, via `compareFingerprints` at `kSampleTolerance`/`kMetricTolerance`; and the entire existing `harmonic_cloud_test.cpp` / `harmonic_cloud_spectral_test.cpp` suites pass **unedited** (an edit there is a failure of this clause). **cl.2:** identity arrays (`ratios[i]=i+1`, `amplitudes[i]=exp2(-p(r)·log2(i+1))`) match the parametric render at ≥ 3 Richness × `{gravity 0,±1}` × `{B 0,0.05}` × `{tilt 0,±12}`. **cl.3:** `clearSpectralTarget()` mid-render produces no click by the SC-001 cl.2 differential detector. **cl.4 (new — the `reset()`/`prepare()` recompute path, §6.1):** with a target active and non-identity (SineStack ratios scaled by 1.5), call `prepare(96000)` — which recomputes `nyquist_`/`invSampleRate_` and then calls `reset()` (`harmonic_cloud.h:255-283`, `reset()` at `:281`, its unconditional `recalculateFrequencies()`/`recalculateAmplitudes()` at `:309-310`) — then render and assert via a 65536-point transform that every partial's rendered frequency tracks the **new** rate, i.e. equals `f0 · targetRatio[i]` within 0.1 %. Under an amended `reset()` that *clears* the per-slot masks instead of setting them, `epsilon_[]` stays derived from the old `invSampleRate_` and every partial renders at half pitch — this clause is what fails. |
| **SC-015** | engine · `SpectralMorph_ExtremesStayFinite` | Grid: `entropy {0,1}` × `bloom {0,1}` × rate `{min,max}` × states `{2,4}` × 4 seeds × the extremal factory states, plus the adversarial `{0.5, 128}` two-partial state. No output element and no rendered sample non-finite; every setter rejection leaves getters and output **bitwise** unchanged; `stateFinite()` true throughout. **`setSpectralTarget` rejection set enumerated** (must reject: null pointers, `count==0`, `count>64`, any non-finite, any `ratios[i] <= 0` incl. `-0.0f`, any `amplitudes[i] < 0`) **and its acceptance set enumerated and asserted** (non-monotone ratios; a ratio above `kMaxStateRatio`; a ratio below `kMinStateRatio`; an amplitude above 1 up to `1 + kMaxAmpJitter`) so an over-zealous implementation fails. **`setState` rejection set enumerated too, mirroring `isValidSpectralState` — the divergence from `setSpectralTarget` is the point (FR-012 vs FR-081, spec.md:604-611), and the earlier draft's "NaN/Inf/out-of-range arguments fed to every setter" grid never reached a malformed `SpectralState` aggregate.** Must reject, each asserted to leave `getOutputRatios()`, `getOutputAmplitudes()` and `isStateFadeActive()` **bitwise** unchanged: `slot` outside `[0, kMaxStates)`; `numPartials` out of `[0, 64]`; a non-monotone ratio pair; a ratio outside `[kMinStateRatio, kMaxStateRatio]`; an amplitude `> 1`; a negative amplitude; a non-finite `tiltDbPerOct`/`inharmonicity`/ratio/amplitude; `tiltDbPerOct` or `inharmonicity` out of range; a `name` with no NUL byte. Mirror-image assertion: the **same** ratio/amplitude arrays that `setState` rejects for non-monotonicity, for a ratio above `kMaxStateRatio` and for an amplitude above 1 **are accepted** by `setSpectralTarget`. **Fill-recurrence arm (§5.4, D9):** load `{numPartials = 0}`, `{numPartials = 1, ratios[0] = 1}` and `{numPartials = 1, ratios[0] = kMaxStateRatio}` and assert `getCleanRatios()` at `p` on that slot equals the documented sequence **element-wise**, recomputed in the test from `max(j + 1, r_{j−1} · kFillSpacingFactor)` — including `r[62] = 63.011`, `r[63] = 64.038` for the sparse cases and `r[63] = 128 · kFillSpacingFactor^63` for the third, which is also the `kMaxOutputRatio` headroom corner (D13). Non-finite inputs built from bit patterns through a `volatile` sink — never `std::numeric_limits<float>::quiet_NaN()`. |
| **SC-016** | entropy · `EntropyProcessor_PhaseDecoheres` | **`entropy = 0.45`** (not 0.50), ≥ 120 s, 8 seeds. The test **asserts `getStageWeight(3) == 0.0f`** at that point so a move of the FR-071 boundaries fails loudly. (a) Each partial's mean ratio deviation within `kMeanRatioDriftCents = 2.0` cents (derivation: `σ ≈ 1.14` cents, SE `= σ·sqrt(2τ/T) = 0.42` cents, so 2.0 is 4.8 σ_SE). (b) Variance **across the 64 partials** of accumulated phase error at ≥ 40 evenly spaced times; linear fit slope positive and exceeding its own standard error by ≥ 5×, on **each** seed. **Comparison arm moved from `entropy = 0.85` to `entropy = 0.74`, with the binomial arithmetic redone (deviation D17).** At 0.85 the spec's derivation (`P(|7·s_i| > 2.0) = 1 − 2/7 = 0.714`, expected 45.7 of 64) assumes each partial carries **one static** scatter offset for the whole run — but at 0.85 stage 4 is live (`w_4 = (0.85 − 0.75)/0.25 = 0.40`, death rate `0.4 · kMaxDeathRatePerSecond = 0.02/s`), so over a ≥ 120 s run each partial dies ≈ 2.4 times and FR-073 **redraws `s_i` on every death**. The run-mean becomes the time-average of ~3 independent `U[−7, +7]` draws: `σ` falls from 4.04 to ≈ 2.33 cents, `P(|mean| > 2.0) ≈ 0.39`, expected **≈ 25 of 64** — below the spec's `≥ 32` threshold, i.e. a faithful implementation fails on each of the 8 seeds. At `e = 0.74`: `w_4 = 0` (stage 4 opens at 0.75) so `s_i` is static as the derivation assumes, `w_3 = (0.74 − 0.50)/0.35 = 0.6857` so the offset is `4.80 · s_i ~ U[−4.80, +4.80]`, and `w_2 = 1.0` (clamped) so decoherence contributes a zero-mean OU term of `σ = 2.0` cents whose run-mean SE over 120 s is `2.0·sqrt(2·8/120) = 0.73` cents. `P(|offset + noise| > 2.0) = 1 − 4.0/9.6 = 0.5833` (the ±2.0 boundary sits ≥ 3.8 SE inside the uniform's support, so the convolution does not move it), mean **37.33 of 64**, `sd = sqrt(64·0.5833·0.4167) = 3.944`. Gate, both asserted and both derived in a comment beside them: **per seed ≥ 24 of 64** (3.38 sd below the mean, `P(fail) ≈ 3.6e-4` per seed, ≈ 2.9e-3 over 8 seeds) and **pooled ≥ 256 of 512** across the 8 seeds (3.8 sd below the pooled mean of 298.7). The test also asserts **`getStageWeight(4) == 0.0f`** and `getStageWeight(3) ≈ 0.6857` at this setting, so a move of the FR-071 boundaries fails loudly — the same protection the 0.45 arm has. At 0.45 the count remains **0** (`w_3 = 0`, so the deviation is decoherence-only at `σ = 1.143` cents and run-mean SE 0.417; `P(|mean| > 2.0) = 1.6e-6` per partial, 8e-4 expected over all 512). |
| Layer 0 | core · `DeriveStreamSeed_IsNonZeroAndDistinct`, `CentsToPitchRatio_MatchesSemitonesToRatio` | `deriveStreamSeed` never returns 0 over a wide base × salt grid and is pairwise distinct over the 256-salt cross product. `centsToPitchRatio(c)` within `1e-6` relative of `semitonesToRatio(c/100)` over the stated grid; `centsToPitchRatio(0) == 1.0f` bitwise. |
| Constants | entropy · `EntropyProcessor_ConstantsMatchTranscendentals` | Every `detail::constexprExp`/`constexprLn`-derived constant (`kMinRatioSpacingFactor`, `kFillSpacingFactor`, `kMaxOutputRatio`, `kOutputCentsSpan`, `kTargetRatioRelEpsilon`) within `1e-6` relative of its `std::exp2`/`std::log2` form. This is what keeps the C++20 `constexpr` workaround honest. |
| OU equivalence | entropy · `EntropyProcessor_OuBankMatchesBrownianDrift` | A single lane of the **decoherence** bank against a reference `BrownianDrift` with the same seed, `setSmoothness(EntropyProcessor::kDecoherenceSmoothness)` — **the class constant itself, never a re-typed literal**, so both sides traverse the identical `tau = kTauMin + s·(kTauMax − kTauMin)` mapping — and `setDepth(1.0)`, over ≥ 60 s: agreement within `1e-5`. This is the standing check that the lane-batched form did not silently desynchronise; the Phase 2 analogue is `HarmonicCloud_DriftLaneMatchesBrownianDrift`. **Only the decoherence bank is compared**, because it is the one configured identically to a stock `BrownianDrift`: the amplitude-jitter bank smooths at `kEntropyAmpSmoothMs = 750` (D12) and would differ by construction. The lane-batching code under test is shared by both banks, so one bank exercises it fully. **The `1e-5` tolerance is only achievable because §4.4 computes `controlDt`, `tau`, `a` and `g` with `double` intermediates** (`harmonic_cloud.h:1519-1531`, and its comment at `:1510-1515` on exactly this test's exposure — 90,000 AR(1) control steps over 60 s). A float derivation makes this row a coin flip across MSVC/GCC/Clang; the test states that in a comment. A second, non-statistical arm asserts `bank.a` and `bank.g` equal `exp(−dt/tau)` and `kInternalStd·sqrt(1−a²)` recomputed in `double` in the test, at `1e-6` relative, for **both** banks. |

### 9.3 Assertion hygiene applying to every test

- No bit-exact float golden over rendered audio. The only exact digest is SC-007's, over a
  serialized byte stream, and it is labelled as such.
- Non-finite inputs constructed from bit patterns via a `volatile` sink.
- Non-finite detection by `detail::isNaN`/`detail::isInf`, never `std::isnan`.
- Every threshold either derived in a comment beside it or measured and recorded with its
  provenance. No threshold is loosened to make a test pass.
- No test file adds `allocation_operator_overrides.h`.

---

## 10. Build integration

**`dsp/tests/CMakeLists.txt`** — sources are listed explicitly, never globbed; an unlisted file
silently drops.

- `dsp_core_tests` (list starts :22): **no change** — the two Layer 0 additions extend the existing
  `unit/core/random_test.cpp` and `unit/core/pitch_utils_test.cpp`.
- `dsp_processors_tests` (list around :275-281, beside `brownian_drift_test.cpp` and
  `spline_trajectory_test.cpp`): **add** `unit/processors/spectral_state_test.cpp` and
  `unit/processors/entropy_processor_test.cpp`.
- `dsp_systems_tests` (list around :298-334, beside `harmonic_cloud_test.cpp`): **add**
  `unit/systems/spectral_morph_engine_test.cpp`, `unit/systems/spectral_morph_render_test.cpp`,
  `unit/systems/spectral_morph_perf_test.cpp`.

No source properties are needed: SC-015 builds non-finite values from bit patterns, so no TU
requires `-fno-fast-math`. Both targets already link `test_helpers`, which is what puts
`<render_fingerprint.h>`, `<artifact_detection.h>`, `<spectral_analysis.h>` and
`<allocation_detector.h>` on the include path (flat include form, per
`harmonic_cloud_spectral_test.cpp:20`).

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release \
         --target dsp_core_tests dsp_processors_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_core_tests.exe       2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe    2>&1 | tail -5
# perf cases are hidden by [.perf]; run explicitly:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph_CpuBudget*"
# Phase 2 regression gate (SC-014 clause 1) — must pass UNEDITED:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5
```

Portability gates before any commit:

```bash
node tools/check-portability.js
node tools/lint-arch-guarded-includes.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

No plugin sources change, so pluginval is not required for this phase.

---

## 11. Recorded deviations (nothing silent)

| # | Spec text | Implemented as | Why |
|---|---|---|---|
| **D1** | FR-041/046/047 stated in the linear ratio domain | Evaluated in the log2 domain, one `exp2` per partial per chunk | Same function in all three cases (§5.5 table); reduces the phase's hottest transcendental count 3× — SC-010 clause 1 is on the line (§8). Continuity, strict monotonicity and the 24-cent floor are preserved verbatim. |
| **D2** | FR-072: `centsToPitchRatio` = `semitonesToRatio(cents/100.0f)` | `std::exp2(cents * (1/1200))` | Same real number; satisfies FR-072's own stated requirement (full-range accuracy); 2–4× cheaper. Equivalence to the specified definition is asserted at `1e-6` relative in `pitch_utils_test.cpp`, so the deviation is checked, not claimed. |
| **D3** | FR-072: (b) and (c) as two successive `centsToPitchRatio` multiplications | The two cent terms summed, one conversion | `f(x)·f(y) = f(x+y)`; divergence ~1.7e-4 cents, four orders below `kTargetRatioEpsilonCents`; FR-074's ±11-cent bound unaffected. Halves the entropy transcendental count. Per-stage introspection accessors still expose the two terms separately, so SC-005 is unaffected. |
| **D4** | FR-085 lever 3: per-slot compare "in the precomputed log domain" | Equivalent relative-epsilon compare in the linear domain | A per-slot `log2` per chunk costs strictly more than the `exp2` the lever exists to save, i.e. the literal form is self-defeating. Same test to first order over the whole ratio range. |
| **D5** | FR-073: "at `w_4 = 0` every partial is Alive with `L_i` exactly 1.0f" | `w_4 == 0` starts no new deaths and pins `L_i = 1.0f` for `Alive` partials; in-flight cycles complete (≤ 5.0 s) | A literal force would make `setEntropy(0)` during a Dead window a step of `L_i` from 0 to 1 in one chunk — 40× `kMaxAmpDeltaPerChunk` — and SC-001 clause 1 exercises `setEntropy` mid-sweep. SC-005's clauses are measured from a fresh processor and are unaffected. |
| **D6** | SC-008 clause 1: "`max_i ratios[i] ≤ kMaxStateRatio` for all five" | Scoped to `i < numPartials`; a separate clause asserts `≤ kMaxOutputRatio` and strict increase over all 64 | Unsatisfiable as written: FR-022 requires `makeFactoryState` to fill `i ≥ numPartials` with the FR-041 continuation, whose `kFillSpacingCents` floor deliberately climbs past `kMaxFillRatio` (that is what `kMaxOutputRatio = 360.37` exists to bound). Bell's filled array reaches **240.32**. The arithmetic the clause actually cites — Bell's `ratio_24` at `B = 0.06` — is an **authored** slot inside `numPartials`, so the scoped form still catches it. |
| **D7** | `1200·log2(64/63) = 27.32` cents (FR-046, SC-002 cl.2, Edge Cases) | **27.264** cents | Arithmetic correction, computed this session. Every inequality survives: `27.264 > kMinRatioSpacingCents = 24.0` (slack 3.264, not 3.32) and `kFillSpacingCents = 28.0 > 27.264`. The plan and tests use 27.264. |
| **D8** | FR-061: `enum class TravelMode` (scope unstated) | Nested as `SpectralMorphEngine::TravelMode` | `TravelMode` is a very generic identifier for `Krate::DSP` namespace scope; nesting removes the collision surface entirely, at no cost to the API. |
| **D9** | FR-041: fill is `r_j = j + 1` when the state has fewer than two ratios | `r_j = max(j + 1, r_{j−1} · kFillSpacingFactor)` **for every `j`**, whenever `numPartials < 2` | Edge Cases already says "and the clamps still apply"; this **is** one of them. Without it, `numPartials = 1, ratios[0] = 128` emits `128, 2, 3, …` and FR-046's repair has to rescue every chunk. The `j + 1` arm applies to the whole fill, not only to slots 0–1 (§5.4 states the resulting sequences and SC-015 recomputes them element-wise). |
| **D10** | (implicit) the engine holds `SpectralState`s | The engine stores sanitized per-slot `log2(ratio)` / amplitude arrays and `numPartials` only | Makes FR-013 structural (metadata cannot reach the audio path), forces the required zeroing of amplitudes at `i ≥ numPartials` (which FR-012 leaves unconstrained and a caller may fill with garbage), and saves 4 × 544 bytes per instance. |
| **D11** | (implicit) transcendental constants | Formed with `detail::constexprExp`/`constexprLn` (`db_utils.h:121`, `:80`) | `std::exp2`/`std::log2` are not `constexpr` in C++20, and FR-044/FR-074 require `static_assert`s over them. Each constant is pinned by a runtime equivalence test at `1e-6` relative. |
| **D12** | FR-044's OU rows use `kDriftOutputSmoothSec = 0.150` **as a time constant** (spec.md:815-819), implying both entropy banks smooth at `BrownianDrift::kDriftOutputSmoothMs = 150` | The **amplitude-jitter** bank smooths at `kEntropyAmpSmoothMs = 5 × 150 = 750 ms`; the **decoherence** bank keeps 150 ms | `OnePoleSmoother`'s parameter is time-to-99 %, `coeff = exp(−5000/(ms·fs))` (`smoother.h:86-91`), i.e. `tau = ms/5000` s — a 150 ms setting is a **30 ms** time constant, not 150 ms. Feeding the amplitude bank 150 ms makes the per-chunk step `4.347e-2` instead of `8.849e-3` (×4.91) and the FR-044 amplitude sum `5.680e-2` against `kMaxAmpDeltaPerChunk = 0.025` — the `static_assert` fails to compile. 750 ms delivers `tau = 0.150 s` **exactly**, which is the time constant the spec's own table row is derived with, so the published `8.85e-3` becomes literally true and the bound is met at `2.218e-2` **without moving any threshold**. The decoherence bank keeps 150 ms so one bank stays bit-comparable to a stock `BrownianDrift` for §9's equivalence test; its cents contribution rises 0.071 → 0.348, absorbed by the cents budget with 3.1 cents to spare. |
| **D13** | (implicit) `kMaxOutputRatio` = the mathematically tight `kMaxFillRatio · 2^(63·kFillSpacingLog2)` = 354.59 | `kMaxFillRatio · 2^(64·kFillSpacingLog2)` = **360.37** (one extra step, +1.6 %) | The FR-012-valid corner `{numPartials = 1, ratios[0] = kMaxStateRatio}` produces `r[63] = 128 · kFillSpacingFactor^63` — the *same real number* as the tight constant, but reached by **63 chained float multiplies** (~4e-6 accumulated relative error) versus one `constexprExp`. SC-008 cl.1's and SC-015's `≤ kMaxOutputRatio` assertions would then fail on rounding alone, on some toolchains and not others. The headroom is derived, not guessed, and `kOutputCentsSpan` moves with it (11364 → 11392), which the FR-044 cents `static_assert` absorbs (121.86 ≤ 125). |
| **D14** | FR-085 lever 3's dirty test compares the incoming target against the stored target | Compares against a separate `committedRatio_`/`committedAmp_` — the values the last recompute actually consumed | With the stored target as baseline, sub-epsilon per-chunk motion accumulates forever without ever crossing `kTargetRatioEpsilonCents`, so at every travel rate the instrument is designed for (10 s – 10 min journeys, FR-061) most partials freeze at their start frequency for the whole journey. Arithmetic and the new SC-009 clause 3 that detects it are in §6.3. |
| **D15** | FR-005's default table read as the post-`reset()` state as well as the post-`prepare` state | `reset()` rewinds stochastic + travel state only; the default slot load moved to the **constructor** | FR-005 says `reset()` matches `BrownianDrift::reset()` (`:133`), which keeps configuration; and the default table's own heading scopes it to "after default construction and after `prepare(sampleRate)` with **no** parameter call" (spec.md:450-451). The wiping form also made SC-012's rewind clause unsatisfiable at its own `entropy = 1` configuration and would have erased the patch on every Phase 7 voice allocation (spec.md:118-124). Full reasoning in §5.6. |
| **D16** | SC-009's bracketing sample is "mid-journey" | Taken from a **separately frozen** 2.5 s render at `p = 0.5` | A 65536-point Blackman-Harris transform is 1.365 s — 17 % of the 8 s journey — so a window taken during travel smears across a moving spectrum and the per-partial claim is not measurable at all, least of all for the Bell pairs whose high slots move hundreds of cents. Freezing at `p = 0.5` measures the same quantity the criterion is about (the interpolated spectrum halfway along) without the smear. |
| **D17** | SC-016's comparison arm at `entropy = 0.85`, "≥ 32 of 64 partials exceed 2.0 cents" | `entropy = 0.74`, per seed **≥ 24 of 64** and pooled **≥ 256 of 512** | The spec's `P = 1 − 2/7 = 0.714` derivation presumes one static scatter offset per partial, but at 0.85 stage 4 is live (`w_4 = 0.40`, 0.02 deaths/s), so over ≥ 120 s each partial dies ≈ 2.4 times and FR-073 redraws `s_i` each time: `σ` falls 4.04 → 2.33 cents and the expected count drops to ≈ 25 — a faithful implementation fails on all 8 seeds. At 0.74, `w_4 = 0` so the offset really is static, and the thresholds are re-derived from `P = 1 − 4.0/9.6 = 0.5833`. Full arithmetic in the SC-016 row. |

---

## 12. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| **R1** | **SC-010 clause 1 (0.15 % = 16,000 ns/block) is on the line.** Two 64-lane OU banks alone measured 9,166–10,998 ns/block in Phase 2's own quiescent path (`harmonic_cloud_perf_test.cpp:116` — the quiescent path is exactly two 64-lane drift-bank advances plus two fills, `harmonic_cloud.h:701-709`, verified this session), and Phase 3 needs exactly two. | Levers 1–3 of §8 are adopted from the start; lever 4 (`centsToPitchRatioFast`, ~1,700 ns) and **lever 5** (entropy OU control interval 32 → 64, ~3,000–4,500 ns) are pre-designed, pre-costed, and their consequences (including which test each one retires or replaces) are written down. §8's pre-decided ladder means a miss produces a recorded finding, never a stall. `kMorphReferenceNsPerBlock` is never raised: SC-010 forbids it and RA-3's escape is scoped to clause 2 only. |
| **R2** | **T0.2's spike has not been run**, so clause 2 is entered without the number SC-010 requires "before the plan is written". | The prerequisite's timing is **amended, not ignored**: measurement moves to T7 and the amendment is recorded as **RA-4** in T9 alongside RA-1/RA-2/RA-3 (§1 T0.2). Machine/build/trial-shape/date are written into SC-010 at T7 in the shape of `harmonic_cloud_perf_test.cpp:104-119`, and RA-3 remains the only escape for clause 2 once all three FR-085 levers are spent. |
| **R3** | SC-014 clause 1 is **unexecutable** unless the pre-amendment fingerprints are captured before any edit to `harmonic_cloud.h` / `core/random.h`. | T0.1 is the first task of the phase; §13's ordering makes it blocking. |
| **R4** | Numerical stability of the fill recurrence — the bare geometric continuation overflows to `Inf` by slot 19 on the FR-012-legal `{1, 128}` two-partial state. | `kMaxFillGrowth = 2.0` and `kMaxFillRatio = 128` cap it; `kMaxOutputRatio = 360.37` is `static_assert`ed as the hard ceiling **with 1.6 % of headroom over the tightest reachable array** (D13, so 63 chained float multiplies cannot round past it), and SC-001 cl.1 / SC-015 both load that exact adversarial state. |
| **R5** | Denormals in the OU walks and in the log-domain arrays under long silent runs. | Both banks carry the walk flush at `EntropyProcessor::kDenormalFloor = 1e-20f` and the clamp at `kWalkLimit = 4.0f` — **class-scoped transcriptions**, because `BrownianDrift`'s own `:226`/`:228` are below its `private:` at `:221` and cannot be named (§0, §4.1); the identical precedent is `HarmonicCloud::kDriftWalkLimit`/`kDriftDenormalFloor` at `harmonic_cloud.h:156-157`. The values and the application points are `brownian_drift.h:263` / `:264-266` verbatim, plus the smoother transcription's `detail::flushDenormal` (`smoother.h:250`). Log-domain ratios live near 0..8.5, nowhere near denormal. |
| **R6** | `-ffast-math` on the macOS leg folding identity branches. | Explicit branches, never arithmetic consequences, at: `bloom == 0` (`invE` exactly 1), `entropy == 0` (stage weights 0), `w_4 == 0` (`L_i` exactly 1), `gravity == 0` (`warp` exactly 1), `fadeX_ >= 1` (no absorption). Same reasoning as `harmonic_cloud.h:1379-1388` and `:1066-1072`. |
| **R7** | Portability: MSVC-green proves nothing. Specific traps here — `core/random.h`'s missing `<cstddef>`; `spectral_state.h`'s `<type_traits>` (needed by `std::is_trivially_copyable_v`, supplied transitively on MSVC/libstdc++); `brownian_drift.h` used but not included by either new header; `std::byte` arithmetic in the serializer; narrowing in brace init. | `#include <cstddef>` is an explicit part of §2.1 and `<type_traits>` of §3; `brownian_drift.h` is in **both** new headers' include lists (§4, §5) with its reason, since MSVC would hide the omission behind `entropy_processor.h`'s transitive reach. The serializer uses `std::memcpy` only, never arithmetic on `std::byte`. Designated initializers everywhere in test fixtures. `node tools/check-portability.js` at **T2** on `spectral_state.h` and before every commit. |
| **R8** | SC-001 clause 2's `1.15`/`+5` margin is a placeholder until measured; the detector has a nonzero false-detection floor (Phase 2 measured 126 L / 141 R over 30 s on a click-free build). | Re-derive from the measured 4-seed spread during implementation and write the observed numbers into the test. If the spread exceeds `±15 % + 5`, raise the seed count to 8 — **never** widen the margin. |
| **R9** | SC-004 metric 3's `1.25` flatness ratio is fixed a priori and may not be met on first measurement. | The prescribed response is to raise the FR-072 cent constants inside FR-074's 12-cent budget (which then requires re-running the §5.1 `static_assert`s and SC-016's derivation), never to lower the ratio. |
| **R10** | SC-002 clause 3 needs ≥ 1200 s of Spline advance × 8 seeds, and SC-004 needs 8 seeds × ≥ 80 s at 11 settings. These are slow tests in a suite that runs on every CI leg. | Advance the engine with large `updateChunk` values where the criterion permits (SC-013 proves chunk-length invariance), and keep the rendered criteria (SC-001 cl.2, SC-004 m.3–4, SC-009) to the pinned durations only. If total suite time becomes a problem, tag the long statistical cases `[.slow]` and run them explicitly — do **not** shorten a run whose length carries a false-failure derivation. |
| **R11** | The `HarmonicCloud` amendment touches a COMPLETE component; a subtle change to the parametric path would be a Phase 2 regression. | Every amendment is guarded by `hasTarget_`, so the no-target loops are byte-for-byte the shipped loops; SC-014 clause 1 is the standing gate, and the existing Phase 2 suites must pass **unedited**. |

---

## 13. Implementation order

Each step ends with a build + the named suite green. No step starts before its predecessor is green.

| # | Task | Verify |
|---|---|---|
| **T0.1** | Capture `kPreAmendmentFingerprints[216]` on the current `main` build, with provenance. **Blocking — must precede any `harmonic_cloud.h` / `random.h` edit.** | Constants checked in; grid documented in the test. |
| **T1** | Layer 0: `deriveStreamSeed` + `<cstddef>` in `core/random.h`; `centsToPitchRatio` in `core/pitch_utils.h`; **`detail::kLn2` in `core/db_utils.h` with the two function-local copies at `:84`/`:130` deleted (§2.3)**; rewrite `HarmonicCloud::deriveSeed` as a forward. Extend the two core test files. | `dsp_core_tests` green; `dsp_systems_tests` "HarmonicCloud_*" green **unedited** (this is also the gate that `constexprLn`/`constexprExp` are bit-identical after the `kLn2` consolidation). |
| **T2** | `spectral_state.h` — struct, validity, normalisation, factory laws with the §3.4 constants, serialization. | `spectral_state_test.cpp` green: SC-007, SC-008 cl.1/2/4 including the 10-pair distance table. Plus `node tools/check-portability.js` on the new header **here**, not deferred to T8 (R7). |
| **T3** | `entropy_processor.h` — constants + `static_assert`s, the two lane-batched OU banks, stage weights, the lifecycle FSM, `processChunk`. | `entropy_processor_test.cpp` green: SC-005, SC-006, SC-016, the constant pins, the `BrownianDrift` equivalence case. |
| **T4** | `spectral_morph_engine.h` — slots + fill + log2 precompute, travel driver, bloom, the log-domain chunk pipeline, absorption, introspection, prepare/reset. | `spectral_morph_engine_test.cpp` green: SC-001 cl.1, SC-002 (all five clauses), SC-003, SC-004 m.1–2, SC-011, SC-012, SC-013, SC-015. |
| **T5** | `HarmonicCloud` FR-080 amendment — members, `markFreqDirty`/`markAmpDirty`, `setSpectralTarget`/`clear`/`has`, the two recompute branches, the FR-086 doc block in both headers. | `dsp_systems_tests` "HarmonicCloud_*" green **unedited**; SC-014 clauses 1–3 green. |
| **T6** | Render TU: SC-001 cl.2, SC-004 m.3–4, SC-008 cl.3, SC-009, SC-014. Re-derive SC-001 cl.2's margin from the measured spread and record it. | `spectral_morph_render_test.cpp` green with the observed numbers written in. |
| **T7** | Perf TU: run the T0.2 spike, set the checked-in baselines with provenance, write both `static_assert`s per clause, and record clause 2's `/26,000` multiplier. Spend §8's levers **in order** (4 then 5) if clause 1 misses, per the pre-decided ladder. | `SpectralMorph_CpuBudget*` green; measured figures, machine/build/trial-shape/date, and spent levers recorded in SC-010. If levers 4 and 5 are both spent and clause 1 still misses, stop and report — do not touch `kMorphReferenceNsPerBlock`. |
| **T8** | Gates: `check-portability`, the three lints, `run-clang-tidy -Target dsp`, full five-layer suite. | All green, zero warnings. |
| **T9** | Roadmap edits RA-1 (two Phase 2 notes), RA-2 (the Phase 7 line-299 tally), RA-3 **only if** clause 2's measurement triggered it, and **RA-4** — the SC-010 prerequisite wording amended from "spike-measured before the plan is written" to "spike-measured and recorded at T7" (§1 T0.2), plus lever 4's Phase 2 amendment note if it was spent. | `specs/Seraphis-roadmap.md` and `spec.md`'s SC-010 updated; compliance table written from verified evidence, not memory. |

---

## 14. Open items for the user

These are **notifications with a default already chosen**, not blockers: each carries the position
this plan takes, so implementation proceeds unless the user overrides.

1. **The SC-010 / RA-3 spike (T0.2) has not been run.** The spec requires it "before the plan is
   written"; it requires a build and a measurement run, which this planning pass did not perform.
   **Position taken:** the prerequisite's *timing* is amended (RA-4, landed in T9) so measurement
   happens at T7 with full provenance, and §8 pre-decides the response to a clause-1 miss — lever 4,
   then lever 5, then an honest finding, never a raised baseline. The phase therefore cannot stall.
   Override only if the spike must precede T2.
2. **§8 lever 4 is a third amendment to a COMPLETE Phase 2 component.** Promoting
   `detail::centsToDriftRatio` to Layer 0 as `centsToPitchRatioFast` is, on the current cost model,
   likely to be *required* for SC-010 clause 1 rather than optional. It would need the same RA-1
   treatment (a recorded roadmap amendment). Pre-authorise it, or should T7 stop and come back for
   approval if the measurement demands it? (Lever 5 — the entropy OU control interval — touches only
   this phase's own new component and needs no such approval.)
3. **Deviation D6 changes a success criterion's scope.** SC-008 clause 1's unscoped
   `max_i ratios[i] ≤ kMaxStateRatio` is unsatisfiable for Bell by construction (its fill reaches
   240.32 against a `kMaxOutputRatio` of 360.37, and FR-012 does not constrain fill slots).
   Confirm the scoped form (`i < numPartials`, plus a separate all-64 `≤ kMaxOutputRatio` +
   strict-increase clause) is the intended reading, or the spec needs the amendment instead.
4. **Three success criteria are restated by this revision, each because a faithful implementation
   fails them as written** — SC-012's rewind clause (D15), SC-016's comparison arm (D17) and
   SC-009's render shape (D16, plus the new slow-travel clause 3). The arithmetic for each is in
   §9.2 beside the row. They are corrections of the criteria's own derivations, not relaxations: no
   threshold is loosened, and D17's replacement thresholds are re-derived from scratch. They are
   listed here so the spec can be brought into line in the same edit as RA-4.
5. **Deviation D12 changes an implementation constant the spec's FR-044 table depends on**
   (`kEntropyAmpSmoothMs = 750`). It makes the spec's published `8.85e-3` amplitude row literally
   true rather than aspirational, so `kMaxAmpDeltaPerChunk = 0.025` survives unchanged. FR-044's
   prose describing `kDriftOutputSmoothSec = 0.150` as "`BrownianDrift::kDriftOutputSmoothMs`"
   should be amended to say "tau = 0.150 s, i.e. 750 ms in `OnePoleSmoother`'s time-to-99 %
   convention" so the derivation and the code agree in writing.

---

## 15. Review notes — resolution ledger for the 2026-07-26 review

**No issue was rejected.** Every blocker, major and minor was applied. Several were raised twice or
three times by different lenses; each is resolved once, at the place named below.

| Review issue | Resolved in | Form of the fix |
|---|---|---|
| §6.3 dirty test compares against the stored target (blocker) | §6.1, §6.3, §6.4, §6.5, §9.2 SC-009 cl.3, D14 | Separate `committedRatio_`/`committedAmp_`, written only by the recompute functions and only for recomputed slots; new slow-travel render clause that fails under the old form. |
| `HarmonicCloud::reset()` skips every slot with a target active (blocker, raised twice) | §6.1, §9.2 SC-014 cl.4 | `reset()` sets both masks to `~0` (i.e. `markFreqDirty()`/`markAmpDirty()`) before the unconditional `recalculate*()` at `:309-310`; new `prepare(96000)` criterion. |
| `BrownianDrift::kWalkLimit` / `kDenormalFloor` are private (blocker, raised twice) | §0 table, §4.1, §4.4, §7, R5 | Class-scoped `EntropyProcessor::kWalkLimit` / `kDenormalFloor` transcribed with citations; `brownian_drift.h` untouched. |
| `kFr044OuStep` uses the wrong one-pole time constant (blocker) | §0 `OnePoleSmoother` row, §4.1, §5.1, D12 | Step routed through `EntropyProcessor::onePoleChunkStep`, which encodes `smoother.h:91` literally; amplitude bank re-timed to `kEntropyAmpSmoothMs = 750` so tau is the 0.150 s the spec derives with. `kMaxAmpDeltaPerChunk` **not** raised. |
| `kLn2` has no reachable/single definition (major, raised three times) | §2.3, §4.1, §5.1, §6.1, §7 | One `detail::kLn2` in `core/db_utils.h`; the two function-local copies deleted; swept in §7. |
| `reset()` scope vs FR-005 / SC-012 / Phase 7 (major) | §5.6, §9.2 SC-012, D15 | `reset()` rewinds stochastic + travel state only; FR-005's default load moved to the constructor; SC-012's rewind clause restated. |
| SC-016's comparison arm is unpassable at `e = 0.85` (major) | §9.2 SC-016, D17 | Moved to `e = 0.74` with the binomial arithmetic redone (per-seed ≥ 24/64, pooled ≥ 256/512) and a `getStageWeight(4) == 0` guard. |
| SC-009's render shape unmeasurable (major) | §9.2 SC-009, D16 | Travel rate, three-phase render, frozen endpoint windows and a separately frozen `p = 0.5` bracketing render all pinned. |
| FR-042 `setState` rejection has no criterion (major) | §5.2, §9.2 SC-015 | Rejection set enumerated with the mirror-image `setSpectralTarget` acceptance assertion. |
| T0.2 spike not run, no defined response (major) | §1 T0.2, §8, R2, T7, T9, §14.1 | Prerequisite timing amended as RA-4; lever 5 added; pre-decided ladder; baseline still never raised. |
| `brownian_drift.h` used but not included (major, raised twice) | §4 and §5 include lists, R7 | Added to both with the reason; the contradictory prose deleted. |
| OU coefficients need `double` intermediates (major) | §4.4, §9.2 OU-equivalence row | Transcribed from `harmonic_cloud.h:1519-1531` including the `variance > 0.0` guard; the reference is fed the class constant. |
| Fill recurrence abandons `r_j = j + 1` (major/minor, raised twice) | §5.4, §9.2 SC-015, D9 | The `or j >= 2` disjunct dropped; the three sparse sequences written out and recomputed in the test. |
| Missing `ampSlotDirty_ = 0` (minor) | §6.5, §9.2 SC-010 | Added immediately before `normGain_.setTarget`, with the FR-083 ordering constraint restated. |
| `processChunk` early return stalls the clocks (minor) | §4.2, §4.6, §9.2 SC-012 cl.3 | Guard split; advance is unconditional in `count`. |
| §5.7 `noexcept` prose contradicts SC-011 (minor) | §5.7 | Reworded: every method including `prepare` is `noexcept`; `prepare` is non-RT **by contract**. |
| No render row calls `noteOn()` (minor) | §9.2 preamble | Note lifecycle pinned for every rendered row, with the `isQuiescent()` early-out cited. |
| SC-013 never reaches the waypoint-rotation path (minor) | §9.2 SC-013 | 65536-sample chunk added in Spline mode at `kMinInterval`. |
| §0.1 item 4's flatness sweep incomplete (minor) | §0.1 item 4, §9.2 SC-004 m.3 | Both overloads named; the magnitude-domain one rejected for a stated reason (`validBins` denominator), not by omission. |
| `kMaxOutputRatio` has no float margin (minor) | §5.1, §5.4, R4, D13 | Ceiling widened by one spacing step (354.59 → 360.37); `kOutputCentsSpan` follows to 11392 and the cents budget still clears at 121.86 ≤ 125. |
| `<type_traits>` missing (minor, raised twice) | §3, R7, T2 | Added, with `check-portability` pulled forward to T2. |
| §6.2 site inventory wrong (minor) | §6.2 | Corrected to 4 + 4 with all eight line numbers and the two non-setter sites called out. |
