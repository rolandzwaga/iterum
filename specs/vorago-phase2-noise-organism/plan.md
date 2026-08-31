# Implementation Plan: Vorago Phase 2 — Noise Organism

**Spec:** `specs/vorago-phase2-noise-organism/spec.md` (1621 lines, read in full this session)
**Roadmap:** `specs/Vorago-roadmap.md` Part A → Phase 2 (lines 178–197), cross-cutting constraints
(lines 468–483)
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/noise_organism.h`, three
additive amendments to shipped components, four new test TUs plus three new cases in existing TUs.
**Test targets:** `dsp_systems_tests` (new TUs), `dsp_processors_tests` / `dsp_primitives_tests`
(amendment cases), `membrum_tests` (regression gate).
**Plugin work:** none.

---

## S0. Verification ledger — every signature quoted below was read this session

Nothing in this plan is quoted from the spec without re-opening the header. Where the spec and the
code disagree, the code wins and the disagreement is recorded in **S14 (Spec corrections)**.

| Claim used by this plan | Verified at |
|---|---|
| `NoiseGenerator::prepare(float sampleRate, size_t maxBlockSize) noexcept`, ends by calling `reset()` | `processors/noise_generator.h:135`, `:182` |
| `reset()` reseeds `rng_.seed(rng_.next() ^ 0xDEADBEEF)` | `noise_generator.h:189` |
| `Xorshift32 rng_{12345}` — fixed construction seed, no `setSeed` | `noise_generator.h:593` |
| `kNumNoiseTypes = 13`; `NoiseType` roster | `noise_generator.h:44-61` |
| level smoothers configured at 5 ms, all targets 0 at prepare | `noise_generator.h:140-144` |
| Velvet emits `±1 × velvetGain` at impulses, exactly `0.0f` between | `noise_generator.h:521-534` |
| `ModulationNoise` is floor-less (`whiteNoise * envelope`, zero sidechain ⇒ 0) | `noise_generator.h:551-561` |
| `TapeHiss` / `Asperity` floors `floorGain + (1−floorGain)·env·sens` | `noise_generator.h:407-412`, `:452-457` |
| Per-type gate `if (noiseEnabled_[idx])` (drops full amplitude next sample) | `noise_generator.h:388` … `:568` |
| `updateLevelTarget` sets smoother target to 0 on disable | `noise_generator.h:578-584` |
| `kBrownLeak = 0.98f`; blue = 1-sample diff of pink; violet = 1-sample diff of white | `noise_generator.h:467`, `:483`, `:498` |
| `setVelvetDensity` clamps `[100, 20000]` | `noise_generator.h:315-317` |
| `ResonatorBank::reset()` is a **configuration wipe** (`enabled_[i] = false`, 440 Hz, default Q) | `resonator_bank.h:213-245` |
| `process()` returns `input` when un-prepared; `input*mix + wetSum*(1−mix)`, `exciterMix_ = 0` ⇒ fully wet | `resonator_bank.h:468`, `:511`, `:589` |
| per-sample loop `if (!enabled_[i]) continue;` | `resonator_bank.h:484-486` |
| `setDecay` and `setQ` both write `qValues_[index]` | `resonator_bank.h:349` vs `:383` |
| `setFrequency` does **not** re-derive Q (FR-099's defect) | `resonator_bank.h:328-332` |
| `setGain(index, dB)` writes gain only — **no** coefficient recompute | `resonator_bank.h:364-369` |
| `rt60ToQ = clamp((π·f·RT60)/ln1000, 0.1, 100)` | `resonator_bank.h:92-98` |
| `calculateTiltGain` early-returns 1.0 at tilt 0, else `log2` + `dbToGain` per resonator per sample | `resonator_bank.h:120-126`, called `:504` |
| `kMinResonatorFrequency = 20`, `kMaxResonatorFrequencyRatio = 0.45`, Q `[0.1, 100]`, decay `[0.001, 30]` | `resonator_bank.h:42,45,48,51,54,57` |
| `TimeVaryingCombBank::setNumCombs` clamps to `[1, kMaxCombs]` — cannot take 0 | `timevar_comb_bank.h:501-505` |
| `setCombDelay` forces `tuningMode_ = Tuning::Custom`, clamps ms to `[1, maxDelayMs_]` | `timevar_comb_bank.h:511-520` |
| `prepare` / `reset` hard-seed `ch.rng.seed(12345u + i*7919u)`; `modDepth_`/`randomModAmount_` default `0.0f` | `timevar_comb_bank.h:466`, `:487`, `:414`, `:416` |
| inharmonic law `f[n] = fundamental·sqrt(1 + n·spread)`, `delayMs = 1000/f[n]` | `timevar_comb_bank.h:958-963` |
| `processBlock` takes the **hoisted** path only when every smoother `isComplete()` and `modDepth_ == 0` | `timevar_comb_bank.h:728-741` |
| `snapSmoothers()` exists and its doc endorses control-chunk parameter pushes | `timevar_comb_bank.h:352-365` |
| per-sample path measured **50,849 ns / 512-block for 6 combs** | `timevar_comb_bank.h:704` |
| hoisted path measured **6 combs 74,580 → ~23,000 ns / 512-block** | `continuous_body_perf_test.cpp:204-205` |
| `StochasticFilter::process` calls `filterA_.setCutoff/​setResonance` **every sample** | `stochastic_filter.h:244-248` |
| `SVF::setCutoff` → `computeG` → `std::tan(π·hz/sr)` every call | `svf.h:204-212`, `:489-491` |
| `SVF::kMinQ/kMaxQ = 0.1/30`, `kMinCutoff = 1.0` | `svf.h:120-126` |
| `StochasticFilter` defaults: `cutoffRandomEnabled_ = true`, others false, rate 1 Hz, 2 octaves | `stochastic_filter.h:555-557`, `:103`, `:111` |
| `BrownianDrift::setSmoothness(float normalized)` — **normalized `[0,1]`, not seconds** | `brownian_drift.h:149-154` |
| OU discretisation `tau = kTauMin + s·(kTauMax−kTauMin)`, `a = exp(−dt/tau)`, `g = 0.5·sqrt(1−a²)` | `brownian_drift.h:` `updateCoefficients()` body |
| walk `x = mean + a(x−mean) + g·z`, `z` = 3 × `nextFloat()` (Irwin–Hall), clamp ±4, denormal floor | `brownian_drift.h:` `advanceControlStep()` body |
| `BrownianDrift::processBlock(size_t)` is bit-identical to N `process()` calls | `brownian_drift.h:191-206` |
| `PerlinNoiseSource::processBlock` documented bit-identical for **any** partitioning | `perlin_noise_source.h:286-291` |
| `BreathingModulator::processBlock(n)` advances phase **once per call** — *not* partition-invariant | `breathing_modulator.h:208-215` |
| `BreathingModulator` output is bipolar `[-1,+1]`, depth default `1.0f` | `breathing_modulator.h:103`, `:112`, `:222-229` |
| `NoiseOscillator::process()` has **no** case for `Velvet`/`RadioStatic` → `default:` white | `primitives/noise_oscillator.h:241-267` |
| `NoiseOscillator` members / helpers (`processPink/Brown/Blue/Violet/Grey`, `resetFilterState`) | `noise_oscillator.h:145-198` |
| `NoiseColor` roster of 8 | `core/pattern_freeze_types.h:124-133` |
| `deriveStreamSeed(base, salt)` lowbias32, guaranteed non-zero | `core/random.h:102-113` |
| `GrainEnvelope::generate` / `::lookup` (NaN-safe ordered clamp) | `core/grain_envelope.h:33`, `:165` |
| `detail::isFinite(float)` — fast-math-immune exponent test | `core/db_utils.h:118-123` |
| `detail::flushDenormal(float)` | `core/db_utils.h:245` |
| `HarmonicCloud` guard ladder + `kControlChunkSamples = 64` | `systems/harmonic_cloud.h:878-891`, `:144` |
| `HarmonicCloud::updateControl` chunks **relative to the block**, not an absolute grid | `harmonic_cloud.h:` `processStereoBlock` loop |
| `AtmosphereEngine::PrepareConfig` shape + salt-range `static_assert` precedent | `systems/atmosphere_engine.h:360-376` |
| `DelayLine::prepare` buffer = `nextPowerOf2(static_cast<size_t>(sr·sec) + 1)` floats — a **truncating** cast, not a round | `primitives/delay_line.h:267-278`, cast at `:269` |
| `Krate::DSP::LinearRamp` **already exists** as a shipped Layer 1 primitive | `primitives/smoother.h:305`, namespace `:30-31` |
| its law is constant-**duration**, not the constant *rate* its class doc claims: `increment = delta / (rampTimeMs · sr / 1000)`, recomputed on every `setTarget` | `smoother.h:100-108`, `:342-354` |
| `LinearRamp` surface: `configure(rampTimeMs, sr)` `:329`, `setTarget` `:342`, `getCurrentValue` `:364`, `process()` `:370`, `isComplete()` `:409`, `snapToTarget()` `:414`, `snapTo(v)` `:421`, `reset()` `:434` | `smoother.h` |
| `LinearRamp::setTarget` neutralises NaN → 0 and saturates Inf → ±1e10, marked `ITERUM_NOINLINE` so the check survives `/fp:fast`; `process()` flushes denormals and clamps overshoot | `smoother.h:342-354`, `:379-386` |
| `TimeVaryingCombBank::processBlock` **zero-fills** `output[n]` before accumulating any comb | `timevar_comb_bank.h:761-763` |
| `NoiseGenerator::setNoiseLevel` clamps its argument to `[kMinLevelDb, kMaxLevelDb] = [−96, +12]` | `noise_generator.h:235-241`, `:104-105` |
| `setMean` exists **only** on `BrownianDrift`; `PerlinNoiseSource` and `BreathingModulator` declare no mean/bias setter at all | `brownian_drift.h:165`; full public APIs read at `perlin_noise_source.h:233-262`, `breathing_modulator.h:164-190` |
| the perf-baseline precedent pairs a **ceiling** clause with a **floor** clause `static_assert(kBaselineX >= kReferenceNs / 50.0)`, whose documented purpose is catching "a baseline recorded from a no-op or misconfigured run" | `atmosphere_engine_perf_test.cpp:34-42` |
| `dsp/lint_all_headers.cpp` is an **enumerated** list (166 `#include`s; the Layer 3 block is `:149-175`) — a new header is **not** picked up automatically | `dsp/lint_all_headers.cpp:149-175` |
| `AllocationDetector` counts only; operator-new replacements discard `size` | `tests/test_helpers/allocation_detector.h:83-89` |
| `render_fingerprint.h`: `kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `compareFingerprints(..., metricTol, sampleTol)` | `tests/test_helpers/render_fingerprint.h:57-61`, `:124-126` |
| `Krate::Test::extractAudioFeatures(const std::vector<float>&, double)`, 5 bands | `tests/test_helpers/audio_features.h:23-37` |
| `dsp_systems_tests` source list is enumerated, not globbed | `dsp/tests/CMakeLists.txt:306-390` |
| the single `-fno-fast-math -fno-finite-math-only` block (GCC/Clang only) | `dsp/tests/CMakeLists.txt:482-778` |
| ODR sweep for `NoiseOrganism`, `NoiseOrganismModel`, `DustGrain` over `dsp/ plugins/ tools/` | run this session — **0 hits** |
| ODR sweep for `LinearRamp` over `dsp/ plugins/ tools/` | run this session — **1 hit**: the shipped `smoother.h:305` primitive. The organism **reuses** it (S1.3); no nested type of that name is declared, so nothing shadows it |

---

## S1. Component: `NoiseOrganism`

### S1.1 Header, layer, includes

`dsp/include/krate/dsp/systems/noise_organism.h`, **Layer 3 (Systems)**, header-only,
`namespace Krate::DSP`. Banner form copied from `timevar_comb_bank.h:1-10`.

```cpp
#include <krate/dsp/core/db_utils.h>            // L0 detail::isFinite, flushDenormal, dbToGain
#include <krate/dsp/core/grain_envelope.h>      // L0 GrainEnvelope::generate / ::lookup
#include <krate/dsp/core/pattern_freeze_types.h>// L0 NoiseColor
#include <krate/dsp/core/random.h>              // L0 Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/noise_oscillator.h>   // L1 dust carrier
#include <krate/dsp/primitives/smoother.h>           // L1 LinearRamp (level + gate ramps, S1.3)
#include <krate/dsp/primitives/svf.h>                // L1 SVFMode, SVF::kMinQ/kMaxQ
#include <krate/dsp/processors/breathing_modulator.h>// L2
#include <krate/dsp/processors/brownian_drift.h>     // L2
#include <krate/dsp/processors/noise_generator.h>    // L2
#include <krate/dsp/processors/perlin_noise_source.h>// L2
#include <krate/dsp/processors/resonator_bank.h>     // L2
#include <krate/dsp/processors/stochastic_filter.h>  // L2
// L3 - same layer, permitted: tools/lint-layers.js fails only on an UPWARD reach.
// Precedent: systems/continuous_body.h:42 includes this exact header the same way.
#include <krate/dsp/systems/timevar_comb_bank.h>     // L3
```

`node tools/lint-layers.js` and `node tools/lint-odr.js` must exit 0 (SC-012).

### S1.2 Public API (real signatures — this is the surface to implement)

```cpp
enum class NoiseOrganismModel : std::uint8_t {
    Direct = 0, FilteredWind = 1, GranularDust = 2, MetallicHiss = 3
};   // append-only, never reordered (FR-011)

class NoiseOrganism {
public:
    // ---- capacities and constants (FR-010, FR-034, FR-051, FR-054, FR-069, FR-074) ----
    static constexpr std::size_t kMaxSources             = 4;
    static constexpr std::size_t kMaxResonatorsPerSource = 4;
    static constexpr std::size_t kMaxCombsPerSource      = 4;
    static constexpr std::size_t kMaxDustGrains          = 24;   // per slot
    static constexpr std::size_t kControlChunkSamples    = 64;   // == harmonic_cloud.h:144
    static constexpr std::size_t kDustEnvelopeTableSize  = 2048;
    static constexpr float kDefaultWanderRateHz = 0.03f;
    static constexpr float kCombFeedbackCap     = 0.9f;   // FR-090, below the bank's own limit
    static constexpr float kBreathGainSpan      = 0.45f;  // FR-070
    static constexpr float kQWanderSpan         = 0.9f;   // FR-064
    static constexpr float kGainRampMs          = 50.0f;  // FR-073 / FR-013
    static constexpr float kOutputClamp         = 4.0f;   // FR-074

    struct PrepareConfig {                       // AtmosphereEngine::PrepareConfig shape
        std::size_t maxBlockSamples = 2048;      // clamped [64, 8192]
        float       maxCombDelayMs  = 50.0f;     // clamped [5, 200]
        std::size_t numSources      = 2;         // clamped [1, kMaxSources]
    };

    NoiseOrganism() noexcept = default;
    NoiseOrganism(const NoiseOrganism&) = delete;          // deep buffers; never copy on the RT thread
    NoiseOrganism& operator=(const NoiseOrganism&) = delete;
    NoiseOrganism(NoiseOrganism&&) noexcept = default;
    NoiseOrganism& operator=(NoiseOrganism&&) noexcept = default;

    // ---- lifecycle (FR-002..FR-005) ----
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;  // ONLY allocator
    void reset() noexcept;                                                  // config-preserving
    void setSeed(std::uint32_t seed) noexcept;
    void processBlock(float* output, std::size_t numSamples) noexcept;      // mono, OVERWRITES

    // ---- slots (FR-010..FR-014) ----
    void setNumSources(std::size_t n) noexcept;                             // [1, kMaxSources]
    void setSourceModel(std::size_t slot, NoiseOrganismModel model) noexcept;
    void setSourceNoiseType(std::size_t slot, NoiseType type) noexcept;     // ModulationNoise → TapeHiss
    void setSourceLevel(std::size_t slot, float dB) noexcept;               // [-96, +12]

    // ---- chain (FR-051..FR-057) ----
    void setNumResonators(std::size_t slot, std::size_t n) noexcept;        // [0, 4]; 0 = stage skipped
    void setResonatorAnchor(std::size_t slot, std::size_t index, float hz) noexcept;
    void setResonatorDecay(std::size_t slot, float seconds) noexcept;       // [0.001, 30]
    void setNumCombs(std::size_t slot, std::size_t n) noexcept;             // [0, 4]; 0 = stage skipped
    void setCombTuning(std::size_t slot, float fundamentalHz, float spread) noexcept;
    void setCombFeedback(std::size_t slot, float feedback) noexcept;        // [0, kCombFeedbackCap]
    void setFilterBaseCutoff(std::size_t slot, float hz) noexcept;
    void setFilterBaseResonance(std::size_t slot, float q) noexcept;        // [SVF::kMinQ, SVF::kMaxQ]

    // ---- models (FR-032, FR-035, FR-041) ----
    void setDustCarrierColor(std::size_t slot, NoiseColor c) noexcept;      // Velvet rejected → Brown
    void setDustGrainMs(std::size_t slot, float ms) noexcept;               // [5, 200] then ceiling
    void setDustDensity(std::size_t slot, float impulsesPerSecond) noexcept;// [100, 20000]
    void setHissBright(std::size_t slot, bool violet) noexcept;             // Blue ↔ Violet

    // ---- wander (FR-061..FR-069) ----
    void setResonatorWander(std::size_t slot, float semitones, float smoothnessSeconds) noexcept;
    void setResonatorQWander(std::size_t slot, float amount) noexcept;      // [0, 1]
    void setFilterWander(std::size_t slot, float octaves, float smoothnessSeconds) noexcept;
    void setFilterResonanceWander(std::size_t slot, float amount, float smoothnessSeconds) noexcept;
    void setCombWander(std::size_t slot, float percent, float ratePerSecond) noexcept;
    void setWanderEnabled(bool enabled) noexcept;                           // organism-wide
    void setWanderRate(float hz) noexcept;                                  // organism-wide (FR-069)

    // ---- breathing and event hooks (FR-070..FR-073) ----
    void setSourceBreathing(std::size_t slot, float rateHz, float depth, float irregularity) noexcept;
    void setSourceDormant(std::size_t slot, bool dormant) noexcept;
    void setSourceWake(std::size_t slot, float amount) noexcept;            // [0, 1]

    // ---- read surface, all [[nodiscard]] ... const noexcept (FR-015) ----
    [[nodiscard]] std::size_t        getNumSources() const noexcept;
    [[nodiscard]] NoiseOrganismModel getSourceModel(std::size_t slot) const noexcept;
    [[nodiscard]] NoiseType          getSourceNoiseType(std::size_t slot) const noexcept; // EFFECTIVE
    [[nodiscard]] float              getSourceLevel(std::size_t slot) const noexcept;
    [[nodiscard]] bool               isSourceDormant(std::size_t slot) const noexcept;
    [[nodiscard]] float              getSourceWakeAmount(std::size_t slot) const noexcept;
    [[nodiscard]] std::size_t        getNumResonators(std::size_t slot) const noexcept;
    [[nodiscard]] std::size_t        getNumCombs(std::size_t slot) const noexcept;
    [[nodiscard]] float              getCombFundamental(std::size_t slot) const noexcept;
    [[nodiscard]] float              getCombSpread(std::size_t slot) const noexcept;
    [[nodiscard]] float              getCombFeedback(std::size_t slot) const noexcept;
    [[nodiscard]] float              getDustDensity(std::size_t slot) const noexcept;     // EFFECTIVE
    [[nodiscard]] float              getDustGrainMs(std::size_t slot) const noexcept;     // EFFECTIVE
    [[nodiscard]] NoiseColor         getDustCarrierColor(std::size_t slot) const noexcept;// EFFECTIVE
    [[nodiscard]] bool               isWanderEnabled() const noexcept;
    [[nodiscard]] float              getWanderRate() const noexcept;
    // applied-state echo — the deterministic observables the criteria measure
    [[nodiscard]] float getSourceGain(std::size_t slot) const noexcept;      // level × breath × gate
    [[nodiscard]] float getResonatorCurrentFrequency(std::size_t slot, std::size_t index) const noexcept;
    [[nodiscard]] float getResonatorCurrentQ(std::size_t slot, std::size_t index) const noexcept;
    [[nodiscard]] float getFilterCurrentCutoff(std::size_t slot) const noexcept;
    [[nodiscard]] float getCombCurrentDelayMs(std::size_t slot, std::size_t index) const noexcept;
    [[nodiscard]] float getSourceRms(std::size_t slot) const noexcept;       // SOURCE stage, see S5.6
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept;
    [[nodiscard]] std::size_t   getAllocatedBytes() const noexcept;
    [[nodiscard]] bool          isPrepared() const noexcept;
};
```

Out-of-range `slot` / `index`: setters are silent no-ops, getters return the documented neutral
(`0.0f`, `Direct`, `Brown`, `false`) — the `resonator_bank.h:329` idiom.

### S1.2a Non-finite argument contract (FR-008) — normative, reproduced verbatim in the header

`std::clamp` does **not** reject NaN: with `v = NaN` both `v < lo` and `hi < v` are false, so `v` is
returned unchanged. A clamp-only setter therefore admits NaN into configuration state, and the trace
is fatal: `setResonatorAnchor(slot, i, NaN)` → `anchorHz[i] = NaN` → S6.2's
`driftedHz = clampFreq(NaN · exp2(…)) = NaN` → `ResonatorBank::setFrequency` clamps with a bare
`std::clamp` (`resonator_bank.h:539`) → `BiquadCoefficients::calculate` clamps with another bare
`std::clamp` (`biquad.h:155`) → NaN `omega` → NaN coefficients. `Biquad::process` resets only on a
non-finite *input sample* (`biquad.h:354`), never on non-finite coefficients, so the resonator emits
NaN forever, and FR-074's output clamp is itself a `std::clamp` and propagates it. The identical
path exists for `setFilterBaseCutoff` → `StochasticFilter::setBaseCutoff` → `SVF::setCutoff` →
`std::tan(NaN)`, and for `setCombTuning` → `setCombDelay` (`timevar_comb_bank.h:514`).

**Blanket rule — one private helper, used by every float-taking public setter, as its first
statement, *before* any clamp:**

```cpp
/// Returns `v` when finite, otherwise the documented neutral for this parameter.
/// detail::isFinite is the fast-math-immune exponent test (core/db_utils.h:118) — a plain
/// `v != v` or std::isnan folds away on the macOS -ffast-math leg.
[[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
    return detail::isFinite(v) ? v : neutral;
}
```

The neutral is the parameter's **FR-016 default**, and the substituted value is then clamped and
stored exactly as a legal write would be — so the write is observable through the FR-015 read
surface (which is what SC-015 (a) asserts) and cannot leave a half-applied state (SC-015 (c)).
`std::size_t` and `bool` and enum arguments cannot be non-finite and take no guard.

| Setter | Argument(s) | Neutral substituted on a non-finite argument |
|---|---|---|
| `setSourceLevel` | `dB` | `-12.0f` |
| `setResonatorAnchor` | `hz` | the FR-016 anchor default for that `index` (`70/140/260/500`) |
| `setResonatorDecay` | `seconds` | `1.5f` |
| `setCombTuning` | `fundamentalHz`, `spread` | `60.0f`, `0.35f` — guarded **independently**, so a finite `spread` with a NaN `fundamentalHz` keeps the spread |
| `setCombFeedback` | `feedback` | `0.55f` |
| `setFilterBaseCutoff` | `hz` | `800.0f` |
| `setFilterBaseResonance` | `q` | `0.7f` |
| `setDustGrainMs` | `ms` | `40.0f` |
| `setDustDensity` | `impulsesPerSecond` | `100.0f` |
| `setResonatorWander` | `semitones`, `smoothnessSeconds` | `2.0f`, `1/kDefaultWanderRateHz` |
| `setResonatorQWander` | `amount` | `0.25f` |
| `setFilterWander` | `octaves`, `smoothnessSeconds` | `1.5f`, `1/kDefaultWanderRateHz` |
| `setFilterResonanceWander` | `amount`, `smoothnessSeconds` | `0.2f`, `1/kDefaultWanderRateHz` |
| `setCombWander` | `percent`, `ratePerSecond` | `12.0f`, `kDefaultWanderRateHz` |
| `setWanderRate` | `hz` | `kDefaultWanderRateHz` |
| `setSourceBreathing` | `rateHz`, `depth`, `irregularity` | `kDefaultWanderRateHz`, `0.25f`, `0.3f` |
| `setSourceWake` | `amount` | `1.0f` |
| `prepare` | `sampleRate` (`double`) | `48000.0` (guarded by the `double` overload of `detail::isFinite`, `db_utils.h:125`) |
| `PrepareConfig::maxCombDelayMs` | | `50.0f` |

**Derived values are guarded too, not just the setters.** A finite configuration value can still
produce a non-finite *derived* value on the control grid (`anchorHz · exp2(…)`,
`combBaseDelayMs · (1 + …)`, `rt60ToQ(…) · qFactor`, `dbToGain(…)`, `1 / (grainMs · 0.001 · sr)`).
Every value written into a chained component on the control grid therefore passes through
`sanitise(value, <that stage's base value>)` immediately before the call, in addition to its clamp —
S5.2 step 2 (lane outputs), S6.2, S6.3 and S6.4 each name the guard at the write site. This is the
design element FR-008 requires; SC-015 is the gate on it, not the source of it.

### S1.3 State layout

Per-slot POD-of-components, `std::array<Slot, kMaxSources>`; nothing is `std::vector` at the
organism level.

```cpp
struct DustGrain { float phase; float phaseIncrement; float gain; bool active; };  // FR-034

struct Slot {
    // sources
    NoiseGenerator   generator;                 // trigger for GranularDust, audio otherwise
    NoiseOscillator  carrier;                   // dust carrier only
    // chain
    ResonatorBank        resonators;
    TimeVaryingCombBank  combs;
    StochasticFilter     filter;
    // wander lanes (all salted, FR-005)
    std::array<BrownianDrift, kMaxResonatorsPerSource> resFreqLane;   // freq AND Q (FR-064)
    BrownianDrift        cutoffLane;
    BrownianDrift        resonanceLane;
    std::array<PerlinNoiseSource, kMaxCombsPerSource> combLane;
    BreathingModulator   breathing;
    // configuration (organism-owned, survives reset())
    NoiseOrganismModel model        = NoiseOrganismModel::Direct;
    NoiseType          requestedType = NoiseType::Brown;   // remembered across model changes
    NoiseType          activeType    = NoiseType::Brown;   // what is enabled on `generator` now
    float levelDb = -12.0f;
    std::size_t numResonators = 2, numCombs = 2;
    std::array<float, kMaxResonatorsPerSource> anchorHz{70.f, 140.f, 260.f, 500.f};
    float decaySeconds = 1.5f;
    float combFundamental = 60.0f, combSpread = 0.35f, combFeedback = 0.55f;
    std::array<float, kMaxCombsPerSource> combBaseDelayMs{};      // derived in S6.3
    std::array<float, kMaxCombsPerSource> appliedCombDelayMs{};   // last value pushed to the bank,
                                                                  // the S6.3 slew limiter's previous
                                                                  // state AND getCombCurrentDelayMs
    float filterBaseCutoffHz = 800.0f, filterBaseQ = 0.7f;
    float resWanderSemis = 2.0f, resQWander = 0.25f;
    float cutoffWanderOct = 1.5f, resonanceWander = 0.2f, combWanderPct = 12.0f;
    float breathDepth = 0.25f, breathIrregularity = 0.3f;
    float dustGrainMsRequested = 40.0f, dustDensityRequested = 100.0f;
    float dustGrainMsEffective = 40.0f, dustDensityEffective = 100.0f;
    float dustGrainGain = 1.0f;                 // FR-036, recomputed on density/length change
    NoiseColor dustColor = NoiseColor::Brown;
    bool  hissViolet = false;
    bool  dormant = false;
    float wakeAmount = 1.0f;
    // applied state (control-grid outputs, read by the FR-015 echo)
    std::array<float, kMaxResonatorsPerSource> appliedResHz{}, appliedResQ{};
    float appliedCutoffHz = 800.0f;
    float breathGain = 1.0f;                    // FR-070 affine map, per control step
    float sourceRmsSmoothed = 0.0f;             // S5.6
    // ramps (per-sample, linear in gain — FR-073)
    LinearRamp levelRamp;                       // → dbToGain(levelDb)
    LinearRamp gate;                            // → dormant/dropped/wake/duck target
    // duck FSM (FR-013)
    enum class Duck : std::uint8_t { Idle, Down, Up } duckState = Duck::Idle;
    bool  duckPending = false;
    NoiseOrganismModel pendingModel; NoiseType pendingType;
    // dust pool
    std::array<DustGrain, kMaxDustGrains> grains{};
    std::size_t grainCursor = 0;                // steal-oldest ring cursor
};
```

Organism-level members: `double sampleRate_`, `PrepareConfig config_`, `bool prepared_`,
`std::uint32_t seed_`, `bool wanderEnabled_ = true`, `float wanderRateHz_`,
`std::size_t controlPhase_` (absolute grid, S5.1), `std::uint32_t clampEngagements_`,
`std::size_t allocatedBytes_`, `std::array<float, kDustEnvelopeTableSize> dustEnvelope_`,
and two shared scratch buffers `std::array<float, kControlChunkSamples> scratchA_, scratchB_`.

**`LinearRamp` is the shipped Layer 1 primitive, reused — not a new nested type.**
`Krate::DSP::LinearRamp` already exists at `primitives/smoother.h:305` (namespace confirmed at
`:30-31`), so declaring a nested type of that name inside `NoiseOrganism` would shadow the library
type: any unqualified `LinearRamp` written in this class would silently bind to the weaker local
one, and a later phase reaching for the library ramp here would get the wrong type with no
diagnostic. The ODR sweep for the name is recorded in S0.

The shipped type is the right one on the merits, not merely to dodge the collision. Despite a class
doc that says "constant rate", its law is constant **duration**:
`increment_ = calculateLinearIncrement(target − current, rampTimeMs_, sampleRate_)` with
`calculateLinearIncrement(delta, ms, sr) = delta / (ms · 0.001 · sr)` (`smoother.h:100-108`,
recomputed on every `setTarget`, `:342-354`) — i.e. exactly the fixed-50 ms-regardless-of-step-size
law SC-009 (a) and SC-018 need. It also carries two guards a hand-rolled POD would have dropped:
`setTarget` neutralises NaN → 0 and saturates Inf → ±1e10 under `ITERUM_NOINLINE` so the check
survives `/fp:fast` (`:342-354`), and `process()` flushes denormals and clamps overshoot
(`:379-386`), which is what makes the landing exact and monotone. A NaN target can therefore never
poison a slot gain permanently — the failure mode the nested copy would have had.

Usage, all through the real surface:

| Purpose | Call |
|---|---|
| configure both ramps in `prepare` | `levelRamp.configure(kGainRampMs, sr)`, `gate.configure(kGainRampMs, sr)` (`:329`) |
| snap at `prepare` / `reset` | `levelRamp.snapTo(dbToGain(levelDb))`, `gate.snapTo(gateSteady)` (`:421`) |
| retarget on a level / dormancy / wake / `setNumSources` change | `configure(kGainRampMs, sr)` then `setTarget(v)` |
| retarget on a duck leg (half duration) | `gate.configure(kGainRampMs * 0.5f, sr)` then `gate.setTarget(v)` |
| advance one sample in `renderChunk` | `process()` (`:370`) |
| read without advancing (`getSourceGain`, duck zero-detection) | `getCurrentValue()` (`:364`), `isComplete()` (`:409`) |

Two ramps per slot is the whole de-zippering story: **level** and **gate**. Both are per-sample and
linear in gain; both reach their target in `rampSamples_ = round(0.050 × sampleRate)` samples (the
duck legs in half that), so SC-009 (a)'s "0–100 % duration 50 ms ± 5 ms" and SC-018's "total
duration 50 ms ± 5 ms" are exact by construction, not by tuning. Note the corollary the review
caught in the criteria: a linear-in-gain ramp of 0–100 % duration `D` has a **10–90 % duration of
`0.8 D`**, so a 50 ms ramp is 40 ms 10–90 %. Every criterion in this plan states the ramp bound in
**0–100 %** terms; see S14.7.

---

## S2. Lifecycle contract

### S2.1 `prepare(double sampleRate, const PrepareConfig& config)` — the only allocator

1. `sampleRate_ = std::max(1.0, sampleRate)` (`brownian_drift.h:122` idiom); clamp
   `maxBlockSamples` to `[64, 8192]`, `maxCombDelayMs` to `[5, 200]`, `numSources` to
   `[1, kMaxSources]`. Store in `config_`.
2. `rampSamples_ = max<size_t>(1, lround(kGainRampMs * 0.001 * sampleRate_))`.
3. `GrainEnvelope::generate(dustEnvelope_.data(), kDustEnvelopeTableSize, GrainEnvelopeType::Hann)`
   (`core/grain_envelope.h:33`) — once, prepare-time only (FR-033).
4. For every slot (all `kMaxSources`, not just the active ones — a later `setNumSources` must not
   need a re-prepare):
   `generator.prepare(static_cast<float>(sampleRate_), config_.maxBlockSamples)` — **the one
   narrowing cast, here, never in the render path** (`noise_generator.h:135` takes `float`);
   `carrier.prepare(sampleRate_)`; `resonators.prepare(sampleRate_)`;
   `combs.prepare(sampleRate_, config_.maxCombDelayMs)`;
   `filter.prepare(sampleRate_, config_.maxBlockSamples)`; every lane `.prepare(sampleRate_)`.
5. Reset all configuration members to the **FR-016 defaults table** (S3), then run
   `applyConfiguration()` (S2.3) — the same routine `reset()` uses.
6. `clampEngagements_ = 0`; `controlPhase_ = 0`; `levelRamp.configure(kGainRampMs, sr)` and
   `gate.configure(kGainRampMs, sr)` (`smoother.h:329`), then snap both
   (`levelRamp.snapTo(dbToGain(levelDb))`, `gate.snapTo(1.0f)`, `:421`). The slot level lives here
   and **only** here — the generator never carries it (S9.1).
7. `allocatedBytes_` = accumulate the sizes requested (S12).
8. `prepared_ = true`.
9. Re-apply `setSeed(seed_)` **last** — `NoiseGenerator::prepare` ends with `reset()`
   (`noise_generator.h:182`), and un-latched `reset()` scrambles the RNG (`:189`), so seeding
   before `prepare` would be discarded.

Re-preparing at any time is legal and fully re-initialises (FR-002). `prepare()` is the **only**
path back to the FR-016 defaults.

### S2.2 `reset()` — configuration-preserving (FR-004)

```
for each slot:
    generator.reset();  carrier.reset();  resonators.reset();  combs.reset();  filter.reset();
    every lane .reset();  breathing.reset();
    grains: all inactive; grainCursor = 0; sourceRmsSmoothed = 0;
    duckState = Idle; duckPending = false;
controlPhase_ = 0;  clampEngagements_ = 0;
applyConfiguration();          // MANDATORY, load-bearing — see below
setSeed(seed_);                // re-latch every stream (NoiseGenerator::reset scrambles otherwise)
snap levelRamp and gate to their configured steady values
```

The re-apply is not an optimisation. `ResonatorBank::reset()` sets every resonator to 440 Hz,
`kDefaultDecayTime`, unity gain, `kDefaultResonatorQ` and **`enabled_[i] = false`**
(`resonator_bank.h:226-232`, doc at `:212`), so a forwarded reset without re-application renders
**silence** on every slot with resonators (SC-006 (b) is the assertion that catches exactly this).
`TimeVaryingCombBank::reset()` (`:482-495`) preserves base delays but re-seeds its RNGs and snaps
its smoothers, so it also needs the delay/feedback re-push; `StochasticFilter::reset()` (`:193`)
clears state but keeps configuration.

`setSeed` inside `reset()` is required because `NoiseGenerator::reset()` scrambles
(`noise_generator.h:189`) unless the FR-081 latch is set — and the latch only re-seeds to the
latched value, so the stream is reproducible only if we re-assert the derived per-slot seeds.

### S2.3 `applyConfiguration()` (private)

One routine, called from `prepare`, `reset`, and after any setter that changes a *configuration*
(not a per-control-step) quantity. It pushes, per slot:

* `generator`: `setNoiseEnabled(t, t == activeType)` for the *one* active type, and
  `setNoiseLevel(activeType, kSourceReferenceDb + kSourceDriveDb[activeType])` (S9.1),
  `setMasterLevel(0.0f)`, plus the model-specific parameter forwards (`setVelvetDensity`,
  `setCrackleParams`, `setTapeHissParams`, `setAsperityParams`) where they apply.
  **The generator level is a constant per active type. It never carries `levelDb`** — the user's
  slot level is carried by the mix-stage `levelRamp` alone (S8.1, S9.1). This is the single owner
  of slot level, and it is the identical expression at every one of the three sites that push it
  (here, the S8.3 duck swap, and `setHissBright`'s Blue↔Violet swap).
* `carrier`: `setColor(dustColor)`.
* `resonators`: `setEnabled(i, i < numResonators)` for all 16 (`:398`); for each enabled `i`,
  `setFrequency(i, anchorHz[i])` then `setDecay(i, decaySeconds)` **once** (S6.2), then the S6.2
  control-step write. `setSpectralTilt` / `setDamping` / `setExciterMix` are never touched (FR-055).
* `combs`: `setNumCombs(clamp(numCombs, 1, 4))` **only when `numCombs > 0`**; recompute
  `combBaseDelayMs[]` from the FR-042 law (S6.3); `setCombDelay(i, base)`,
  `setCombFeedback(i, combFeedback)`, `setCombGain(i, 0 dB)`, `setCombDamping(i, 0)`; then
  `snapSmoothers()` (S6.3).
* `filter`: `setMode(RandomMode::Walk)`, `setBaseFilterType(...)`, `setBaseCutoff(...)`,
  `setBaseResonance(...)`, `setCutoffRandomEnabled(wanderEnabled_)`,
  `setResonanceRandomEnabled(false)`, `setTypeRandomEnabled(false)`,
  `setCutoffOctaveRange(model == FilteredWind ? 2.0f : 1.0f)`,
  `setSmoothingTime(model == FilteredWind ? 400.0f : 200.0f)`,
  `setChangeRate(wanderRateHz_)`.
* every lane: `setDepth(1.0f)` — the organism scales the `[-1,+1]` output itself, so lane depth stays
  at unity and only the organism's own span constants change the excursion — and the FR-069 rate
  mapping (S7.2). All three lane types declare `setDepth` (`brownian_drift.h`,
  `perlin_noise_source.h:256`, `breathing_modulator.h:177`), and `1.0f` is already each one's
  library default, so the write is an explicit no-op that documents the invariant rather than
  relying on it. That is consistent with S3's "`BreathingModulator::setDepth` is deliberately left
  at its library `1.0f`": what the organism must never do is forward the FR-070 `breathDepth` into
  the lane, because the affine map in S8.2 applies it already and forwarding would square it.
* `setMean(0.0f)` is pushed to the **`BrownianDrift` lanes only** — `resFreqLane[]`, `cutoffLane`,
  `resonanceLane`. `setMean` is a member of `BrownianDrift` alone (`brownian_drift.h:165`);
  `PerlinNoiseSource` (full public API read at `perlin_noise_source.h:233-262`) and
  `BreathingModulator` (`breathing_modulator.h:164-190`) declare no mean/bias setter of any kind,
  so `combLane[n].setMean(0.0f)` and `breathing.setMean(0.0f)` would not compile. Both are
  zero-mean by construction anyway (Perlin lattice, sinusoidal breath), which is why no equivalent
  call is needed. Recorded in S14.8.

### S2.4 `setSeed` and the salt table (FR-005)

```cpp
// Compile-time salt bases. APPEND ONLY: a later phase adding a lane must take a NEW base,
// never renumber these, or every Phase-2 render silently changes.
static constexpr std::size_t kSaltNoiseGen      = 0;    // + slot
static constexpr std::size_t kSaltDustCarrier   = 16;   // + slot
static constexpr std::size_t kSaltChainFilter   = 32;   // + slot
static constexpr std::size_t kSaltResonatorLane = 48;   // + slot*kMaxResonatorsPerSource + index
static constexpr std::size_t kSaltFilterCutoff  = 64;   // + slot
static constexpr std::size_t kSaltFilterReso    = 80;   // + slot
static constexpr std::size_t kSaltCombLane      = 96;   // + slot*kMaxCombsPerSource + index
static constexpr std::size_t kSaltBreathing     = 112;  // + slot
static constexpr std::size_t kSaltNextFree      = 128;
static_assert(kSaltResonatorLane + kMaxSources * kMaxResonatorsPerSource <= kSaltFilterCutoff,
              "resonator lane salts must not overlap the cutoff block");
static_assert(kSaltCombLane + kMaxSources * kMaxCombsPerSource <= kSaltBreathing,
              "comb lane salts must not overlap the breathing block");
static_assert(kSaltBreathing + kMaxSources <= kSaltNextFree, "salt table overflow");
```
(the `atmosphere_engine.h:360` `static_assert` precedent). Every seeding call is
`x.setSeed(deriveStreamSeed(seed_, base + offset))` (`core/random.h:102`). A `seed` of 0 is legal:
`deriveStreamSeed` substitutes `0x2545F491u` for a zero hash (`random.h:112`) and `Xorshift32::seed`
substitutes its own default for 0 (`random.h:44-45`).

`TimeVaryingCombBank` takes **no** salt: it has no `setSeed` and hard-seeds
`12345u + i*7919u` (`timevar_comb_bank.h:466`, `:487`). FR-042 pins `setModDepth` /
`setRandomModulation` at their `0.0f` defaults (`:414,416`) so none of that unsalted motion reaches
the audio; all comb motion is the salted Perlin lane (S7.1).

---

## S3. FR-016 normative default table → code

Encoded as one `private: void applyDefaults() noexcept` writing exactly the FR-016 table, with the
library value it overrides quoted in a trailing comment at each line. Defaults that are *derived*
from `kDefaultWanderRateHz` (three Brownian smoothnesses, the Perlin rate, the filter change rate,
the breathing rate) are **not** written independently — they come from one call to
`applyWanderRate(kDefaultWanderRateHz)`, so the FR-069 single-owner property is structural.

Overrides that must be explicit (a stock instance would otherwise wander an order of magnitude too
fast): `StochasticFilter::setChangeRate` (library `kDefaultChangeRate = 1.0f`,
`stochastic_filter.h:103`) and `setCutoffOctaveRange` (library `kDefaultOctaveRange = 2.0f`, `:111`).
`BreathingModulator::setDepth` is deliberately **left** at its library `1.0f` (`:112`) — the
organism owns depth in the FR-070 affine map, and forwarding it too would square it.

---

## S4. Model roster

| Model | Base type | Chain deltas |
|---|---|---|
| `Direct` | slot's `requestedType` (`ModulationNoise` → `TapeHiss`, FR-012) | defaults |
| `FilteredWind` | `NoiseType::Brown` (`noise_generator.h:49`) | filter `SVFMode::Bandpass`, octave range 2.0, smoothing 400 ms |
| `GranularDust` | `NoiseType::Velvet` as **trigger only** | dust engine (S6.1) upstream of the chain |
| `MetallicHiss` | `NoiseType::Blue` (`:50`) or `Violet` (`:51`) via `setHissBright` | comb feedback 0.75, organism-computed inharmonic ratios |

`requestedType` survives a model change; switching to a composed model and back restores it
(FR-012). Every model/type write goes through the FR-013 duck (S8).

---

## S5. Render contract

### S5.1 `processBlock` and the absolute 64-sample control grid (FR-003, FR-007)

```cpp
void processBlock(float* output, std::size_t numSamples) noexcept {
    if (output == nullptr) return;                       // nothing written, NOTHING advanced
    if (numSamples == 0) return;                         // no control step consumed
    if (!prepared_) { std::fill_n(output, numSamples, 0.0f); return; }   // no state advance

    std::size_t done = 0;
    while (done < numSamples) {
        if (controlPhase_ == 0) updateControl();                       // once per 64 ABSOLUTE samples
        const std::size_t chunk = std::min(numSamples - done,
                                           kControlChunkSamples - controlPhase_);
        renderChunk(output + done, chunk);
        controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
        done += chunk;
    }
}
```

**This deliberately differs from `HarmonicCloud`.** Its loop is `chunk = min(64, numSamples - done)`
relative to the block start (`harmonic_cloud.h:` `processStereoBlock`), which makes a 36+28 split
run *two* control steps where an unsplit 64 runs one. SC-016 requires `max|difference| = 0` across
the irregular partition `36, 28, 1000, 1, 511, 2048`, so the residual `controlPhase_` counter is
mandatory; copying the cloud verbatim fails SC-016. Recorded in S14.

The guard ladder ordering matters for SC-017: the null and zero checks precede *everything*, so no
lane advances and `controlPhase_` does not move.

### S5.2 `updateControl()` — one control step, per slot

1. Advance every lane by exactly `kControlChunkSamples`: `lane.processBlock(64)`. Fixed-size
   advances are required — `BreathingModulator::processBlock(n)` advances phase once per call
   (`breathing_modulator.h:208-215`) and is therefore *not* partition-invariant; advancing it only
   in 64-sample units makes it so. `BrownianDrift`/`PerlinNoiseSource` are bit-identical under any
   partitioning (`brownian_drift.h:191`, `perlin_noise_source.h:286`), but use the same 64 for
   uniformity. Lanes advance **even when their depth is 0 and even when the slot is dormant**
   (FR-066, FR-071) — this is what makes SC-010 (a) and the "no jump on re-enable" rule true.
2. Read each lane once with `getCurrentValue()` (already clamped to `[-1,+1]`,
   `brownian_drift.h:212`), guard with `detail::isFinite` (`db_utils.h:118`) and substitute the
   lane's neutral `0.0f` if not finite (FR-008).
3. Compute and write the drifted parameters (S6.2, S6.3, S6.4), then `combs.snapSmoothers()`.
4. Compute `breathGain` (S8.2) and, for dust slots, refresh `dustGrainGain` if density or length
   changed.
5. Update the FR-015 applied-state echo fields (`appliedResHz`, `appliedResQ`, `appliedCutoffHz`,
   `appliedCombDelayMs`) from the values just written.

**`sourceRmsSmoothed` is deliberately *not* updated here.** `updateControl` runs *before*
`renderChunk` for that control step, and `scratchA_` is a single organism-level shared buffer
(S1.3) reused by every slot in sequence — so at `updateControl` time it holds the **last** slot's
audio from the **previous** chunk. Computing the FR-015 source RMS from it here would make every
slot but the last report a different slot's level, and SC-010 (a) could not see it (both of its arms
alias identically, and at `numSources == 1` the aliasing vanishes entirely). The accumulation lives
inside `renderChunk` instead, on the slot's own data — S5.6.

Cost note: everything in `updateControl` is paid once per 64 samples, i.e. 8 times per 512-block.

### S5.3 `renderChunk(float* out, std::size_t n)` — `n ≤ 64`

**Step 0, before any slot is considered: `std::fill_n(out, n, 0.0f)`.** Then every contributing slot
accumulates with `+=`; there is no "first slot writes with `=`" special case.

This is not a micro-optimisation question — it is FR-003's overwrite contract
(spec.md:221: `processBlock` "**overwrites** `output` (it does not accumulate)"). Under a
first-slot-writes-with-`=` rule, a dormant slot 0 (S5.4 skips its steps 2–5 entirely), or a slot 0
dropped by `setNumSources`, or an all-dormant organism, leaves the caller's buffer untouched; the
FR-074 tail then *reads* it, so the scale-and-clamp operates on — and the organism returns —
whatever the host left there, arbitrary and possibly non-finite. That is not a corner case for this
phase's own criteria: SC-007 isolates slots with `setSourceDormant(other, true)`, so slot 0 is
dormant in three of its four isolation renders and its Pearson correlations would be computed over
stale buffer data; SC-004 (e) times an all-dormant render, i.e. it would be timing a read of
uninitialised memory. The unconditional fill costs `n ≤ 64` stores per chunk. The shipped idiom is
identical — `TimeVaryingCombBank::processBlock` zeroes `output[n]` before accumulating any comb
(`timevar_comb_bank.h:761-763`).

Per slot, using the shared 64-sample scratch buffers (no per-slot buffers, slots are sequential):

```
0. mix buffer : std::fill_n(out, n, 0.0f);                            // ONCE, before the slot loop
1. source     : scratchA[0..n)  ← generator.process(scratchA, n)      (Direct/Wind/Hiss)
                                  or the dust engine (S6.1) writes scratchA
1b. source RMS: sumSq += scratchA[s]^2 over the chunk  →  fold into this SLOT's
                sourceRmsSmoothed at chunk end (S5.6) — here, on the slot's OWN data
2. resonators : if (numResonators > 0) resonators.processBlock(scratchA, n)   // in-place
                else                   /* stage skipped — see FR-051 */
3. combs      : if (numCombs > 0)      combs.processBlock(scratchA, scratchB, n)
                else                   scratchB ← scratchA
4. filter     : filter.processBlock(scratchB, n)                              // in-place
5. gain+mix   : for s in [0,n): out[s] += scratchB[s] * levelRamp.process()
                                          * breathGain * gate.process() * modelTrimGain;
```

`modelTrimGain = dbToGain(kModelTrimDb[model])` (S9.1) is a per-model calibration constant, hoisted
per chunk and rewritten only at the S8.3 duck swap — i.e. only at an instant when `gate` is exactly
0, so it can introduce no step. It is deliberately **excluded** from `getSourceGain`, which FR-015
defines as level × breath × gate; the trim is a property of the source stage's calibration, not a
gain the caller set, and `kModelTrimDb[Direct] == 0` keeps SC-018's "never leaves 1.0" arm exact.

Step 1b runs for **every** slot including a fully dormant one (whose step 1 still runs, S5.4), which
is what makes SC-010 (a)'s source-stage comparison meaningful across the dormant and awake arms.

Step 2 at `numResonators == 0` **must** skip the call, not forward a zero count: with nothing
enabled, `process()` returns `input*mix + wetSum*(1−mix)` with `wetSum == 0` and
`currentMix == exciterMix_ == 0` (`resonator_bank.h:511`, `:589`) — i.e. **silence, not bypass**.
Step 3 at `numCombs == 0` likewise: `TimeVaryingCombBank::setNumCombs` floors at 1
(`timevar_comb_bank.h:502`) and `process` has no dry path (`:630-693`), so a forwarded 0 would leave
one comb running.

After the last slot the chunk is scaled by `1/sqrt(kMaxSources)` and clamped to `±kOutputClamp`,
incrementing `clampEngagements_` (saturating) on any engagement (FR-074). Because of step 0 that
tail always operates on defined data, whatever the dormancy pattern. Denormal flush via
`detail::flushDenormal` (`db_utils.h:245`) on the mix.

### S5.4 Dormancy (FR-071, Q6 — "source runs, chain skipped")

A dormant slot still runs step 1 into `scratchA` (so the RNG **and** the colour filters —
brown's leaky integrator `noise_generator.h:467`, pink's Kellet chain — stay exactly where an
always-awake slot's would be), still runs step 1b so its `getSourceRms` keeps tracking, and still
runs `updateControl` for its lanes. Steps 2–5 are skipped and the slot contributes nothing to the
mix. Note that `renderChunk`'s step 0 fill is *outside* the slot loop and therefore unconditional,
so "contributes nothing" means "adds zero to an already-zeroed chunk", never "leaves the buffer
untouched" (S5.3).

**Chain-skip is gated on the gate ramp reaching zero, not on the flag.** While
`gate.current > 0 || gate.remaining > 0` the chain still runs, so `setSourceDormant(slot, true)`
fades the *chain output* down over 50 ms instead of cutting it (FR-073). Only once the gate has
landed on exactly 0 does the slot enter the cheap path. Waking reverses it: the chain is re-enabled
on the sample the gate leaves 0.

### S5.5 RT safety (FR-006)

`processBlock`, every setter and every getter are `noexcept`; no allocation, lock, exception, I/O,
`std::function`, or virtual dispatch on the per-sample path. The lanes derive from
`ModulationSource`, whose only virtuals are `getCurrentValue()`/`getSourceRange()`
(`brownian_drift.h:212`, `:217`) — both are called at most once per lane per control step, never
per sample, and through the concrete type (no base-pointer indirection: lanes are held by value).

### S5.6 `getSourceRms` measures the **source stage**, pre-chain (decision)

FR-015 says "the slot's smoothed output level"; SC-010 (a) requires the dormant-then-woken and
always-awake arms to agree on this trajectory within 0.5 dB **over the first 250 ms after wake**.
Those two arms' *chain* states cannot agree there — that is precisely what SC-010 (b)'s settle
window exists for — so the only reading under which SC-010 (a) is satisfiable on a correct
implementation is the **source stage output** (post-source, pre-resonator, pre-gain). That is also
the quantity SC-010 (a) says it is proving ("the source RNG streams advanced identically") and the
quantity Phase 8's ecosystem energy sensing wants.

**Where it is computed matters as much as what it measures.** It is accumulated in `renderChunk`
step 1b — inside the slot's own iteration, immediately after step 1 has written `scratchA_` for
*that* slot — as a running sum of squares over the chunk, folded into that slot's own
`sourceRmsSmoothed` by a one-pole at chunk end, and returned in dBFS. It is **not** computed in
`updateControl`: that routine runs *before* `renderChunk` for the step, and `scratchA_` is one
organism-level buffer shared by every slot in sequence (S1.3), so reading it there would hand every
slot but the last the previous chunk's *last* slot's level. Cost is one multiply-add per sample per
slot plus one `log10` per slot per control step. Recorded in S14 and raised in Open Questions.

---

## S6. Per-slot chain — exact call sequences

### S6.1 Sources

**Direct / FilteredWind / MetallicHiss** — `generator.process(scratchA, n)`
(`noise_generator.h:332`, zero sidechain). `TapeHiss` and `Asperity` therefore sit at their
configured floor (`:407-412`, `:452-457`), which is documented, not worked around.
`ModulationNoise` is unreachable (FR-012) because it is floor-less (`:551-561`) and would render
exactly `0.0f`.

**GranularDust** — two-source composite:

```
generator.process(trigger, n);                 // only NoiseType::Velvet enabled
carrier.processBlock(carrierBuf, n);           // NoiseOscillator, dustColor
for s in [0,n):
    if (trigger[s] != 0.0f) {                  // exact impulse detector (noise_generator.h:521-534)
        // Allocation policy (deterministic, no unconditional overwrite — see below):
        //   1. scan forward from grainCursor for the first INACTIVE slot; if found, take it
        //   2. otherwise steal the grain with the LARGEST phase (the one nearest its own
        //      Hann zero, so the truncation step is the smallest available)
        DustGrain& g = grains[acquireGrain()];
        grainCursor = (grainCursor + 1) % kMaxDustGrains;
        g.phase = 0.0f;
        g.phaseIncrement = 1.0f / (dustGrainMsEffective * 0.001f * sampleRate_);
        g.gain = (trigger[s] > 0.0f ? +1.0f : -1.0f) * dustGrainGain;   // FR-036 sign = velvet polarity
        g.active = true;
    }
    float env = 0.0f;
    for (DustGrain& g : grains) if (g.active) {
        env += GrainEnvelope::lookup(dustEnvelope_.data(), kDustEnvelopeTableSize, g.phase) * g.gain;
        g.phase += g.phaseIncrement;
        if (g.phase >= 1.0f) g.active = false;
    }
    scratchA[s] = carrierBuf[s] * env;
```

`dustGrainGain = 1 / sqrt(max(1, expectedConcurrency))`,
`expectedConcurrency = dustDensityEffective × dustGrainMsEffective / 1000`, recomputed **only** when
density or grain length changes (FR-036). In-flight grains keep their birth gain and their birth
`phaseIncrement` — a parameter change never rewrites a live grain. Level continuity at a grain's own
birth and natural death is structural: the Hann table starts and ends at exactly 0
(`core/grain_envelope.h:47-52`).

**Grain allocation is not an unconditional ring overwrite, and the pool can genuinely saturate.**
`acquireGrain()` above prefers a free slot and only steals when all `kMaxDustGrains` are live,
stealing the **largest-phase** grain so the truncation happens at the smallest envelope value
available. Writing `grains[grainCursor]` unconditionally would truncate a live grain's Hann envelope
mid-flight at an arbitrary non-zero value — a step discontinuity, and the direct contradiction of
the paragraph above. This is common rather than rare at the top of the FR-035 range: the ceiling
`grainCeilingMs = 1000 · kMaxDustGrains / density` sets *mean* concurrency to exactly
`kMaxDustGrains`, and velvet arrivals are a stochastic Bernoulli train
(`noise_generator.h:521-534`), so instantaneous concurrency exceeds 24 roughly half the time there —
i.e. at the top of SC-019 (b)'s density sweep, steals of live grains are the norm. The policy is
still O(`kMaxDustGrains`) worst case, allocation-free and branch-deterministic; SC-019 (b) gains an
arm at the 20 000 imp/s ceiling that measures the 25 ms-frame envelope `maxΔ` against the SC-009 (b)
bound, so the policy is proven rather than asserted.

Effective-value clamp (FR-035), applied on every density/length write, density first:
```
dustDensityEffective = clamp(requestedDensity, 100.0f, 20000.0f);       // noise_generator.h:315-317
grainCeilingMs       = 1000.0f * kMaxDustGrains / dustDensityEffective;
dustGrainMsEffective = min(clamp(requestedGrainMs, 5.0f, 200.0f), grainCeilingMs);
```
then `generator.setVelvetDensity(dustDensityEffective)` (`:315`). `carrierBuf` reuses `scratchB`
(the comb stage has not run yet), so no third buffer is needed.

### S6.2 Resonators — the single Q owner (FR-052, FR-053, FR-064)

Configuration time (`applyConfiguration`), per enabled resonator `i`:
`setFrequency(i, anchorHz[i])` then `setDecay(i, decaySeconds)` — **`setDecay` is called here and
never again**, so `getDecay` keeps reporting the nominal RT60.

Every control step, per enabled resonator `i`:
```
d          = clamp(resFreqLane[i].getCurrentValue(), -1, 1);            // one lane, freq AND Q
driftedHz  = clampFreq(sanitise(anchorHz[i] * exp2(resWanderSemis * d / 12.0f),
                                anchorHz[i]));                          // [20, 0.45*sr], S1.2a
qFactor    = 1.0f - kQWanderSpan * resQWander * (1.0f + d) * 0.5f;      // FR-064: DOWNWARD ONLY
targetQ    = clamp(sanitise(rt60ToQ(driftedHz, decaySeconds) * qFactor,
                            rt60ToQ(driftedHz, decaySeconds)),
                   kMinResonatorQ, kMaxResonatorQ);                     // resonator_bank.h:92-98
makeupDb   = resonatorMakeupDb(targetQ);                                // S9.2
resonators.setFrequency(i, driftedHz);      // resonator_bank.h:328
resonators.setQ(i, targetQ);                // :381  — the ONLY per-step writer of qValues_
resonators.setGain(i, makeupDb);            // :364  — gain only, no coefficient recompute
appliedResHz[i] = driftedHz; appliedResQ[i] = targetQ;
```

Why `setQ` is the sole writer: `setDecay` assigns `qValues_[index] = rt60ToQ(frequencies_[index],
decays_[index])` (`:349`) and `setQ` assigns the clamped argument (`:383`) — the same variable, so
alternating them destroys one setting with the other. Why the organism re-derives `rt60ToQ` itself:
`ResonatorBank` exposes no Q reader that would let it compose the wander factor onto the library's
value (there is `getQ` at `:390`, but reading back a value the organism just wrote is a
round trip with no benefit, and FR-099's new internal derivation inside `setFrequency` is
immediately superseded by the `setQ` on the next line — verified at `:328-332` vs `:345-352`).

Why downward-only is not a style choice: `rt60ToQ` saturates at `kMaxResonatorQ = 100` for every
`f × RT60 > 100·ln1000/π = 219.9` (`:92-98`), and at the FR-016 default decay of 1.5 s the anchors
70/140/260/500 Hz give `f·RT60` = 105/210/390/750 — the top two anchors are already saturated, and
at `kMaxDecayTime = 30 s` all four are. An upward factor would be clipped away; SC-021 measures a
≥3× bandwidth change, which only the downward factor can deliver.

Cost: two `Biquad::configure` calls per enabled resonator per control step
(`resonator_bank.h:545-556`), i.e. 24 configures per 64 samples in the SC-004 (c) reference
configuration. `setGain` adds one `dbToGain` (`:364-369`).

### S6.3 Combs — organism-computed inharmonic ratios and the hoisted path (FR-042, FR-063)

Base delays, recomputed whenever fundamental/spread change (the bank's own law, evaluated by us so
the bank's `Tuning` mode is irrelevant — `timevar_comb_bank.h:958-963`):

```
f[n]              = combFundamental * sqrt(1 + n * combSpread);
combBaseDelayMs[n]= clamp(1000.0f / f[n], 1.0f, config_.maxCombDelayMs);
```

Every control step, per comb `n < numCombs`:
```
p          = clamp(combLane[n].getCurrentValue(), -1, 1);
targetMs   = sanitise(combBaseDelayMs[n] * (1.0f + 0.01f * combWanderPct * p),
                      combBaseDelayMs[n]);                      // S1.2a derived-value guard
targetMs   = clamp(targetMs, 1.0f, config_.maxCombDelayMs);
targetMs   = slewLimit(appliedCombDelayMs[n], targetMs, kMaxCombDelayStepMs);   // see below
combs.setCombDelay(n, targetMs);            // timevar_comb_bank.h:511 — forces Tuning::Custom
appliedCombDelayMs[n] = targetMs;           // slew state AND getCombCurrentDelayMs (FR-015)
```

The limiter's "previous" state is `Slot::appliedCombDelayMs[]` (S1.3) — the same array the FR-015
read surface returns through `getCombCurrentDelayMs`, so the trajectory the limiter actually
produced is observable rather than inferred. It is snapped to `combBaseDelayMs[]` in
`applyConfiguration()` so a re-configuration does not slew from a stale value.
then, once per slot per control step, `combs.snapSmoothers()` (`timevar_comb_bank.h:358`).

**`snapSmoothers()` is load-bearing, not an optimisation.** `TimeVaryingCombBank::processBlock`
takes its hoisted path only when `modDepth_ == 0` **and every smoother reports `isComplete()`**
(`:728-741`). Writing `setCombDelay` on every control step keeps the 20 ms delay smoother
(`kDelaySmoothingMs`, `:109`) permanently unsettled, so without the snap the bank is pinned to the
per-sample path forever — measured at **50,849 ns per 512-block for 6 combs**
(`timevar_comb_bank.h:704`) versus **~23,000 ns** hoisted (`continuous_body_perf_test.cpp:204-205`).
At the SC-004 (c) reference configuration (4 slots × 2 combs = 8 combs) that is ~99,000 ns against a
106,666 ns budget for the combs *alone*. The bank's own doc endorses exactly this usage: "Calling
this after each parameter push makes the bank's coefficients piecewise-constant over the caller's
control chunk; continuity is then the caller's own smoother, sampled on its grid"
(`timevar_comb_bank.h:352-357`) — and `ContinuousBody` is the shipped precedent.

Snapping moves the continuity obligation to the organism, which is why the slew limit above exists.
It is not "an extra slew limiter added on top of the bank's" (which FR-063 declines) — it *replaces*
the smoothing the snap removes. **This is an amendment to FR-063's stated reasoning, recorded in
S14.4 and put to the user in Open Question 3, not merely an open question**: as written FR-063 says
no extra slew limiter is added because the bank already smooths delay changes, and after
`snapSmoothers()` the bank does not.

**The bound is measured, not asserted, because the obvious conservative choice throttles the lane.**
A bound of 0.25 samples of delay per control step (5.2 µs per 1.33 ms at 48 kHz) is far below
anything a fractional-delay read can click on — but at the extreme legal setting (50 % depth,
5 cells/s Perlin) the *unlimited* step reaches ~8 samples per control step, so that bound clips the
trajectory by ~32× and turns the comb lane into a near-static parameter at fast rates. Calling that
"a slightly lagged trajectory" was wrong, and no criterion as originally planned could see it:
SC-002 runs at the default rate (12 % of a 16.7 ms base delay over a 33 s period ≈ 0.004
samples/step, three orders under the bound, so the limiter is invisible), SC-005 runs at maximum
rate but asserts only finiteness/peak/non-silence, and SC-020 sets comb wander depth to 0.

Therefore:

1. **`kMaxCombDelayStepSamples` is produced by the `[.calibration]` case**, not chosen. The case
   sweeps per-control-step delay steps at the worst-case configuration (feedback at
   `kCombFeedbackCap = 0.9`, `numCombs = 4`, base delay at the FR-016 default) and records the
   largest step whose 25 ms-frame envelope `maxΔ` stays inside the SC-009 (b) bound. `0.25 samples`
   is the conservative *starting point* of the sweep and the fallback if the measurement cannot be
   made; the measured value and its date go in the header beside `kSourceDriveDb`.
   `kMaxCombDelayStepMs = kMaxCombDelayStepSamples / sampleRate_ × 1000`, so the bound is a
   *distance in samples* and is sample-rate independent (SC-008 (c) depends on that).
2. **The throttle is measured too.** SC-002 gains an arm at the maximum wander rate and maximum
   depth that reads `getCombCurrentDelayMs` per control step over 120 s and REQUIREs the realised
   peak-to-peak excursion is at least **25 %** of the configured `setCombWander` span
   (`0.01 × combWanderPct × combBaseDelayMs[n]`, doubled for the bipolar lane). 25 % is the floor
   below which the control misrepresents itself to the user — a knob set to 50 % that moves the
   delay by a tenth of a percent is a broken control, not a lagged one. The realised figure is
   recorded in `compliance.md` at T11 whether it passes or not, so the cost of the bound is a
   number rather than a claim.

SC-009 (b) measures the artefact side of the same trade.

Feedback is written once at configuration time, clamped to `kCombFeedbackCap = 0.9` (FR-090, below
the bank's own limit). `setModDepth` / `setRandomModulation` are never called — they stay at their
`0.0f` library defaults (`:414`, `:416`), which is also what keeps the hoisted path reachable
(`modDepth_ <= 0.0f` is the first hoist condition, `:728`).

Nothing asserts `getTuningMode() == Inharmonic`: `setCombDelay` sets `Tuning::Custom` on the first
control step (`:515`), by design (FR-042). SC-020 measures the *peak ratios* instead.

### S6.4 Chain filter (FR-056, FR-062, FR-067)

Every control step:
```
c   = clamp(cutoffLane.getCurrentValue(), -1, 1);
hz  = clamp(sanitise(filterBaseCutoffHz * exp2(cutoffWanderOct * c), filterBaseCutoffHz),
            20.0f, 0.45f * sampleRate_);                                         // S1.2a
r   = clamp(resonanceLane.getCurrentValue(), -1, 1);
q   = clamp(sanitise(filterBaseQ * (1.0f + resonanceWander * r), filterBaseQ),
            SVF::kMinQ, SVF::kMaxQ);                                             // svf.h:120-123
filter.setBaseCutoff(hz);        // stochastic_filter.h:343 (clamps again to [1, sr*0.495])
filter.setBaseResonance(q);      // :350
appliedCutoffHz = hz;
```

The filter's **internal** randomiser stays on by default (`cutoffRandomEnabled_ = true`,
`stochastic_filter.h:555`) per FR-023 — it is a series of discrete `RandomMode::Walk` jumps around
whatever the base currently is, smoothed over `setSmoothingTime`, which is a different *kind* of
motion from the external lane's continuous mean-reverting walk of the base itself. Since FR-069 they
share a nominal tempo; the distinction is shape, not speed. `setWanderEnabled(false)` (FR-068) is
the only way to get a genuinely static render: it zeroes every external lane's contribution **and**
calls `setCutoffRandomEnabled(false)` on every slot.

---

## S7. Wander lanes — exact math

### S7.1 What each lane is, and why

| Target | Lane type | Reason |
|---|---|---|
| resonator frequency **and** Q | `BrownianDrift`, one per resonator (≤16) | mean-reverting ⇒ bounded by construction; Q shares the lane (FR-064: 16 more lanes buy no audible independence) |
| chain filter cutoff | `BrownianDrift`, one per slot | continuous base motion |
| chain filter resonance | `BrownianDrift`, one per slot | FR-067; the roadmap's "**all** filter parameters wander" |
| comb delay | `PerlinNoiseSource`, one per comb (≤16) | band-limited by construction — the roadmap pairs it here (line 190); also the only comb motion, since the bank's own PRNGs are unsalted |
| slot level | `BreathingModulator`, one per slot | roadmap line 192 |

**Exact OU discretisation (already shipped, do not re-implement).** `BrownianDrift` computes
`tau = kTauMin + smoothness·(kTauMax − kTauMin)`, `a = exp(−dt/tau)`,
`g = kInternalStd·sqrt(1 − a²)` with `dt` the 32-sample control interval, then
`x ← clamp(mean + a(x − mean) + g·z, ±4)` with `z` the sum of three `nextFloat()` draws
(Irwin–Hall, zero-mean unit-variance), followed by a 150 ms output smoother
(`brownian_drift.h:` `updateCoefficients`/`advanceControlStep`, constants at `:97-105`). The
organism supplies only `smoothness`, `depth = 1`, `mean = 0`, and scales the `[-1,+1]` output itself.

### S7.2 `setWanderRate(float hz)` → the four lane kinds (FR-069)

```
hz = clamp(hz, StochasticFilter::kMinChangeRate, StochasticFilter::kMaxChangeRate);  // [0.01, 100]

// (1) three Brownian lane kinds — tau in SECONDS, converted to the NORMALIZED argument
tau        = clamp(1.0f / hz, BrownianDrift::kTauMin, BrownianDrift::kTauMax);   // [0.2, 30] s
smoothness = (tau - BrownianDrift::kTauMin)
           / (BrownianDrift::kTauMax - BrownianDrift::kTauMin);                  // → [0, 1]
lane.setSmoothness(clamp(smoothness, 0.0f, 1.0f));

// (2) comb Perlin
combLane[n].setRate(hz);                       // perlin_noise_source.h:242, clamps [0.005, 5]

// (3) chain filter internal randomiser
filter.setChangeRate(hz);                      // stochastic_filter.h:417, clamps [0.01, 100]

// (4) breathing
breathing.setRate(hz);                         // breathing_modulator.h:170, clamps [0.01, 0.5]
```

**The seconds→normalized conversion in (1) is mandatory and the spec's literal wording is wrong.**
`BrownianDrift::setSmoothness(float normalized)` takes a **normalized `[0,1]`** argument, not a tau
in seconds (`brownian_drift.h:149-153`, and the mapping `tau = kTauMin + s·(kTauMax − kTauMin)` in
`updateCoefficients`). FR-069's table says the tau is "forwarded to `BrownianDrift::setSmoothness`";
forwarding it literally would clamp to `s = 1` for every `hz ≤ 1` and would give
`tau = 0.2 + 0.2·29.8 = 6.2 s` for a requested 0.2 s at `hz = 5`. At the default
`kDefaultWanderRateHz = 0.03` both readings coincide (`1/0.03 = 33.3 > kTauMax` ⇒ `s = 1` ⇒
`tau = 30 s`), which is why SC-002 stands either way — but every other rate is wrong under the
literal reading. The conversion above realises the *stated intent* (`= clamp(1/hz, 0.2, 30) s`, and
"the three Brownian lanes run at the clamp ceiling (30 s tau)"), and the header documents it. See
S14.

Per-lane `smoothnessSeconds` arguments (`setResonatorWander`, `setFilterWander`,
`setFilterResonanceWander`) use the **same seconds domain** and the same conversion, for the same
reason. Precedence between them and `setWanderRate`: **last writer wins on the same lane state**;
`prepare`/`reset` re-apply `setWanderRate(wanderRateHz_)`, which is therefore the baseline. Raised
in Open Questions.

### S7.3 Depth and freeze semantics (FR-066, FR-068)

Every lane runs at `setDepth(1.0f)` permanently (the write is an explicit no-op at each lane type's
library default — S2.3); the organism's own span constants (`resWanderSemis`, `cutoffWanderOct`,
`resonanceWander`, `combWanderPct`, `resQWander`) scale the `[-1,+1]` output. A span of 0 therefore
freezes the parameter at its base value while the lane keeps advancing (FR-066 — the
`harmonic_cloud.h:897-901` quiescent-lane rule), so raising the span back up does not jump.

**FR-066's guarantee is a behaviour, so it gets a gate.** A naive implementation that skips
`lane.processBlock(64)` when a span is 0 — purely as a CPU saving — passes every other case in
S13.2, because SC-010 (a) proves freewheeling only under *dormancy*, a different branch. The
freeze-then-restore arm added to `NoiseOrganism_DormantLanesFreewheel` (S13.2) closes it.

`setWanderEnabled(false)` multiplies every span above by 0 **and** calls
`setCutoffRandomEnabled(false)` on every slot's chain filter; lanes still advance.

**It does not touch breathing, and that is deliberate.** FR-068 zeroes every *external wander* lane
(FR-061…FR-067); FR-070's breathing is a separate per-slot lane owned by `setSourceBreathing`, and
nothing in FR-068 names it. So a `setWanderEnabled(false)` render still breathes at
`breathDepth = 0.25`, i.e. ±0.92 dB per slot — which is by design the *dominant* contributor to
broadband level variation (FR-016 / SC-001). The consequence for the criteria is real and is fixed
in the test fixture, not by amending FR-068: SC-002 (b)'s control arm asks for a broadband RMS CV
at least 3× below the wander-on arm, and with identical breathing in both arms that separation
would have to come entirely from the spectral lanes' residual level contribution, which nothing
estimates. **SC-002 (b)'s control arm therefore also calls `setSourceBreathing(slot, rate, 0.0f,
irr)` on every slot**, making it genuinely static (S13.2), and T11 records both measured CV figures
in `compliance.md` so the 3× clause is shown to be reachable rather than assumed. Zeroing breathing
inside `setWanderEnabled` was the alternative and is rejected here because it would change shipped
behaviour to suit a test.

---

## S8. Gain chain, duck FSM, wake

### S8.1 Composition (FR-050)

`appliedGain(slot) = levelRamp.getCurrentValue() × breathGain × gate.getCurrentValue()`, and
`getSourceGain(slot)` returns exactly that (FR-015). **`levelRamp` is the sole carrier of the user's
slot level** — the generator carries only the per-type calibration constant (S2.3, S9.1), so the
level is applied exactly once in the chain. Per sample, `levelRamp.process()` and `gate.process()`
(`smoother.h:370`) advance; `breathGain`
is held across the control chunk (its own motion is ≤ 0.1 dB per chunk at any legal setting, so it
carries no zipper — SC-009 (b) measures the envelope to confirm).

### S8.2 Breathing (FR-070)

```
b          = clamp(breathing.getCurrentValue(), -1, 1);   // BIPOLAR: breathing_modulator.h:103, :227
breathGain = 1.0f + kBreathGainSpan * breathDepth * b;    // kBreathGainSpan = 0.45
```
`breathGain ∈ [1 − 0.45·depth, 1 + 0.45·depth] ⊆ [0.55, 1.45]`: strictly positive, never zero, never
sign-changing, exactly 1.0 when `b == 0` so `depth == 0` is neutral rather than a constant duck.
`BreathingModulator::setDepth` is left at its library `1.0f` (`:112`) — the organism owns depth, and
forwarding it as well would square it. SC-001 (d) asserts the bound and the sign.

### S8.3 Gate target and the duck FSM (FR-013, FR-071, FR-072, FR-073)

```
gateSteady(slot) = (dormant || slot >= numSources) ? 0.0f : wakeAmount;
```
Any change to `dormant`, `wakeAmount` or `numSources` calls
`gate.configure(kGainRampMs, sr)` then `gate.setTarget(gateSteady)` — a per-sample, linear-in-gain,
50 ms 0–100 % ramp. The dropped-slot ramp in `setNumSources` is the same mechanism (FR-072), which
is why SC-009 (a)'s randomised transition set includes `setNumSources` reductions and increases.

The duck is a three-state FSM per slot:

```
setSourceModel(slot, m) / setSourceNoiseType(slot, t):
    effective = (t == ModulationNoise ? TapeHiss : t)          // FR-012 substitution first
    if (requested value == current EFFECTIVE value) return;    // Q5: change-detected NO-OP
    pendingModel/pendingType = new value;  duckPending = true;
    if (duckState == Idle || duckState == Up) {                // RE-ARM from Up — see below
        duckState = Down;
        gate.configure(kGainRampMs * 0.5f, sr);  gate.setTarget(0.0f);
    }
    // else (duckState == Down): the Down leg is still running and has not reached the swap point,
    //     so the pending target is simply updated and the ramp is NOT restarted
    //     (Q5 coalescing: a burst of writes BEFORE the swap costs exactly ONE 50 ms duck)

per sample, when duckState == Down and gate.getCurrentValue() == 0.0f:
    apply the swap:  generator.setNoiseEnabled(activeType, false);
                     activeType = pendingType; model = pendingModel;
                     generator.setNoiseLevel(activeType,
                                             kSourceReferenceDb + kSourceDriveDb[activeType]);
                     generator.setNoiseEnabled(activeType, true);
                     applyModelConfiguration(slot);         // filter type / octave range / comb feedback
    duckPending = false;  duckState = Up;
    gate.configure(kGainRampMs * 0.5f, sr);  gate.setTarget(gateSteady);

when the Up ramp lands (gate.isComplete()): duckState = Idle.
```

**Why `Up` must re-arm.** Without it a genuine change arriving during the Up leg is silently
dropped: it sets `duckPending`/`pendingModel`/`pendingType`, but the Down leg is over so the swap
condition never fires again, and "when the Up ramp lands: `duckState = Idle`" discards the pending
write. The slot then keeps rendering the old model/type while `getSourceModel`/`getSourceNoiseType`
report the *old* effective value — a lost write, not a delayed one. SC-018's coalescing arm as
originally worded could not see it, because "exactly one 50 ms duck" is also what a *dropped* second
change produces. Re-arming from `Up` costs a second duck for a change that arrives after the swap
point, which is correct: Q5's coalescing guarantee is about a burst arriving *before* the swap, and
a post-swap change is a genuinely new transition. SC-018 gains a randomised-offset arm (S13.2) that
writes the second change anywhere inside the whole 50 ms duck and asserts the getters equal the
**last** value written once the trajectory settles.

Note the level line in the swap: it pushes `kSourceReferenceDb + kSourceDriveDb[activeType]`, the
same constant expression as `applyConfiguration` (S2.3), with **no `levelDb` term**. An earlier
draft of this plan had the two sites disagree — `applyConfiguration` pushing drive only while the
swap pushed `levelDb + drive` — which would have rendered a −12 dB slot at −24 dB after its first
model change, and no criterion could have caught it (the error is uniform across all 13 types, so
SC-019 (a)'s ±3 dB-vs-White arm is blind to it, and SC-001 (c)'s (−60, −3] dBFS window is wide
enough to absorb 12 dB). SC-019 (a) gains the arm that would: absolute slot RMS at two different
`setSourceLevel` settings must differ by exactly the requested dB.

The swap happens on the exact zero sample rather than waiting for the next control-step boundary.
Both readings satisfy FR-013; the exact-zero form keeps the total duration exactly `rampSamples_`
(50 ms), which is what SC-018's ±5 ms and SC-009 (a)'s monotonicity want. The swap itself is
allocation-free: `setNoiseEnabled` writes a bool and a smoother target (`noise_generator.h:255-261`).

Why the duck exists at all: each type's contribution is gated on `if (noiseEnabled_[idx])`
(`noise_generator.h:388` … `:568`), so disabling a type removes a full-amplitude broadband
contribution on the very next sample while `updateLevelTarget`'s ramp to zero (`:578-584`) never
gets to run. SC-018's naive-path arm asserts that.

A `setSourceLevel` change calls `levelRamp.configure(kGainRampMs, sr)` then
`levelRamp.setTarget(dbToGain(clamp(sanitise(dB, -12.0f), -96.0f, +12.0f)))`, and does **not** arm
the duck — it changes no generator state, so there is nothing to duck around. It also never touches
`generator.setNoiseLevel`: if it did, a slot's generator-side level would additionally be stale
until the next duck, which is an unducked step the moment one arrives.

---

## S9. Calibration tables (FR-017, FR-018)

Three tables — `kSourceDriveDb[13]` (per `NoiseType`), `kModelTrimDb[4]` (per
`NoiseOrganismModel`, S9.1) and `resonatorMakeupDb(Q)` (S9.2) — plus the measured
`kMaxCombDelayStepSamples` bound (S6.3). All are **measured**, not authored, by the one hidden
`[.calibration]` case. All are constant-per-step and monotone, so none can break `getSourceGain`'s
SC-009 (a) monotonicity, and none introduces a control loop.

### S9.1 `kSourceDriveDb[kNumNoiseTypes]` (FR-017)

Procedure, recorded verbatim in the header and implemented as a hidden Catch2 case
`NoiseOrganism_MeasureSourceDrive` tagged `[.calibration]`:

1. For each of the 13 `NoiseType`s: a bare `NoiseGenerator`, `prepare(48000.0f, 512)`, `setSeed(1)`,
   only that type enabled at its own `kDefaultLevelDb` (`noise_generator.h:106`), render 5 s.
2. `rms = extractAudioFeatures(render, 48000.0).rmsDbfs` (`audio_features.h:37`).
3. `kSourceDriveDb[type] = rmsDbfs(White) − rmsDbfs(type)`.

**Who owns slot level (the single-owner rule).** FR-017's normative sentence — "every
`setNoiseLevel(type, requestedDb)` call the organism issues actually passes
`requestedDb + kSourceDriveDb[type]`" (spec.md FR-017) — is honoured with **`requestedDb` bound to a
single organism-internal constant**, not to the user's slot level:

```cpp
/// The generator-side level every source runs at, before its per-type drive.
/// The user's slot level is NOT here: it is carried exclusively by the mix-stage levelRamp
/// (S8.1). Chosen as NoiseGenerator::kDefaultLevelDb, the level the drive table was measured
/// at (noise_generator.h:106), so kSourceReferenceDb + kSourceDriveDb[White] == that default
/// exactly and every other type is equalised to it.
static constexpr float kSourceReferenceDb = NoiseGenerator::kDefaultLevelDb;   // -20.0f
```

so every organism-issued call is `setNoiseLevel(type, kSourceReferenceDb + kSourceDriveDb[type])` —
a constant per type, identical at all three push sites (S2.3's `applyConfiguration`, S8.3's duck
swap, `setHissBright`'s Blue↔Violet swap), and `levelDb` appears in none of them. FR-014 agrees:
`setSourceLevel` "sets the slot's contribution **to the mix**" (spec.md:332).

The alternative — generator carries `levelDb + drive` *and* the mix stage applies
`dbToGain(levelDb)` — double-applies the level: a −12 dB slot renders at −24 dB. Nothing in the
originally planned criteria could catch it (the error is uniform across all 13 types, so SC-019
(a)'s ±3 dB-vs-White comparison is blind, and SC-001 (c)'s (−60, −3] dBFS window absorbs 12 dB), so
SC-019 (a) gains the arm that can: **absolute slot RMS measured at two different `setSourceLevel`
settings must differ by exactly the requested dB difference, ±0.5 dB.** Under a double application
a 12 dB request moves the render 24 dB and the arm fails.

**Headroom, re-derived without a slot-level term.** The generator clamps its level argument to
`[kMinLevelDb, kMaxLevelDb] = [−96, +12]` (`noise_generator.h:235-241`, `:104-105`), so the binding
constraint is `kSourceReferenceDb + kSourceDriveDb[type] ≤ +12`, i.e. `kSourceDriveDb[type] ≤ 32 dB`
at `kSourceReferenceDb = −20`. Velvet is the worst case: at the 100 imp/s floor it is a sparse train
whose RMS is `≈ peak · sqrt(density/fs) = sqrt(100/48000)` ⇒ ~26.8 dB below its peak, against
white's ~4.8 dB (uniform ±1), so its drive entry is ≈ +22 dB — inside the 32 dB allowance with about
10 dB to spare. The earlier "the −12 dB slot level leaves 24 dB of room" argument was an artefact of
the double-application reading and does not survive it; this is the replacement.

Because a *measured* table could still overflow that allowance on some type, the clamp is not left
as a silent backstop: **the `[.calibration]` case REQUIREs
`kSourceReferenceDb + kSourceDriveDb[t] ∈ [kMinLevelDb, kMaxLevelDb]` for every selectable type**
and fails loudly with the measured value if a type cannot be calibrated inside the generator's
range. Silently clamping would put that type outside SC-019 (a)'s ±3 dB window with no explanation;
failing surfaces the choice (lower `kSourceReferenceDb`, or exclude the type) to the user.

**`kModelTrimDb[4]` — the composed models need their own constant (FR-017 cannot reach them).**
`kSourceDriveDb` is a per-`NoiseType` offset applied at `NoiseGenerator::setNoiseLevel`, and three of
the four models do not take their level from there: `GranularDust` uses only the velvet impulse
*sign* (S6.1) and derives level from `NoiseOscillator` carrier RMS × Hann-window energy ×
`dustGrainGain`, none of which FR-017 touches at all; `FilteredWind` adds a 2-octave bandpass and
`MetallicHiss` a 0.75-feedback comb, whose chain gains neither FR-017 (per type) nor FR-018 (per Q)
compensates. So a fourth entry is measured by the same `[.calibration]` case:

```cpp
static constexpr float kModelTrimDb[4] = { /* Direct == 0 by construction */ ... };
```

measured as `rmsDbfs(Direct/White reference slot) − rmsDbfs(model)` at the FR-016 defaults (for
`GranularDust`, at the FR-016 default density of 100 imp/s), applied at the **mix stage** alongside
`levelRamp` — not at the generator, which the dust path bypasses. `kModelTrimDb[Direct] = 0` exactly.
This is what makes SC-019 (a)'s ±3 dB window satisfiable for all four models rather than for
`Direct` only. It is a constant per model: deterministic, compile-time, monotone, no control loop —
the same properties FR-017 requires of its own table.

**The matrix SC-019 (a) actually walks is 12 + 3, not 48.** FR-012 makes type selection effective
only for `Direct` slots (the composed models pin their own base type), so 36 of the 48 type×model
cells are duplicates of 3 distinct renders. SC-019 (a) therefore walks **12 `Direct` type cells**
(each within ±3 dB of the White reference, via `kSourceDriveDb`) plus **3 composed-model cells**
(each within ±3 dB of the same reference, via `kModelTrimDb`). Same coverage, no vacuous repeats.

A placeholder in either table fails immediately: SC-019 (a)'s ±3 dB arm and SC-001 (c)'s window are
both gates on measured values, and the case that produces the values is checked in.

### S9.2 `resonatorMakeupDb(float q)` (FR-018)

A bandpass with constant 0 dB peak gain (`biquad.h`, configured at `resonator_bank.h:548`) admits
a broadband source's power in proportion to its noise bandwidth, `ENBW ∝ f0/Q`, so slot RMS falls
as `1/sqrt(Q)`. The compensating law is therefore **`makeupDb(Q) = kMakeupSlope·log10(Q/kQRef)`**
with an analytic starting point of `kMakeupSlope = 10.0` (10·log10 of a power ratio) and
`kQRef = rt60ToQ(anchor[0], 1.5 s)`. Because the sources are not white (three of the reference
models pin Brown/Blue/Violet, FR-021/FR-041) the coefficients are **fitted, not assumed**: the same
`[.calibration]` case sweeps `Q ∈ {1, 3, 10, 30, 100}` at each FR-016 anchor, records slot RMS, and
fits `kMakeupSlope` and an offset by least squares on `dB vs log10(Q)`. The fitted constants and the
measurement date go in the header.

Applied via `ResonatorBank::setGain(i, makeupDb)` (`:364`) on the same control step, immediately
after `setQ` — `setGain` writes only `gainsDb_`/`gains_` and does **not** recompute filter
coefficients (`:364-369`), so it is ~1 `dbToGain` per resonator per control step.

Clamped: `makeupDb ≤ kMakeupCeilingDb`, chosen so a `kMaxResonatorQ = 100` resonator (which FR-064's
downward-only factor guarantees is the *quiet* end) cannot push a slot above SC-001 (c)'s −3 dBFS.

---

## S10. Shared-component amendments

Three amendments, each additive, each with a no-change guarantee, each gated by an existing consumer
suite (SC-011).

### S10.1 `NoiseGenerator::setSeed` (FR-080–FR-083)

```cpp
/// @brief Seed the PRNG for deterministic, decorrelated instances.
/// Signature copied from NoiseOscillator::setSeed (primitives/noise_oscillator.h:109).
void setSeed(std::uint32_t seed) noexcept {
    seedLatched_ = true;
    configuredSeed_ = seed;
    rng_.seed(seed);
}
[[nodiscard]] std::uint32_t getSeed() const noexcept { return configuredSeed_; }
```
and in `reset()` (`noise_generator.h:186-189`) the one-line change:
```cpp
if (seedLatched_) rng_.seed(configuredSeed_);            // reproducible for opt-in callers
else              rng_.seed(rng_.next() ^ 0xDEADBEEF);   // UNCHANGED historical scramble
```
Two new members (`bool seedLatched_ = false; std::uint32_t configuredSeed_ = 12345;`). No other
behaviour, signature or default changes; `kNumNoiseTypes` stays 13. The five existing consumers
(`systems/character_processor.h`, `systems/tape_machine.h`, `effects/pattern_freeze_mode.h`,
`primitives/noise_oscillator.h` — indirect, and `plugins/membrum/src/dsp/exciters/noise_burst_exciter.h`)
never call `setSeed`, so all five see byte-identical behaviour.

Without this, four slots emit **bit-identical** noise (all `Xorshift32 rng_{12345}`, `:593`,
advanced identically) and sum coherently at +12 dB instead of +6 dB — roadmap line 185's "N
simultaneous noise sources" would be one source at four times the level. SC-007's control arm
reproduces exactly that pre-amendment condition in-process.

### S10.2 `NoiseOscillator` Velvet / RadioStatic (FR-098)

`process()` currently has no `case` for either colour and falls through `default:` to white
(`primitives/noise_oscillator.h:241-267`). Fix:

```cpp
case NoiseColor::Velvet:      return processVelvet(white);
case NoiseColor::RadioStatic: return processRadioStatic(white);
```
plus two private helpers and their state:
* `processVelvet(white)`: `p = (white + 1) * 0.5f` (reuse of the draw already made — no extra RNG
  consumption in the non-impulse case); if `p < kVelvetDensityHz / sampleRate_` emit
  `rng_.nextUnipolar() < 0.5f ? +1.0f : -1.0f`, else `0.0f`. `kVelvetDensityHz = 2000.0f` is a
  documented compile-time constant — the class has no density setter and adding one is out of scope.
  The law mirrors `noise_generator.h:521-534`.
* `processRadioStatic(white)`: one `Biquad radioLowPass_` configured in `prepare()` as
  `configure(FilterType::Lowpass, 5000.0f, 0.707f, 0.0f, sr)` — the same configuration
  `noise_generator.h:180` uses — and reset in `resetFilterState()`.
* class doc updated from "six noise colors" to eight (`noise_oscillator.h:29-40`).

Zero-regression-risk, verified this session: every consumer pins a colour other than
Velvet/RadioStatic (`ring_modulator.h:295,298`; Membrum's `noise_body.h`, `click_layer.h`,
`clap_exciter.h`, `feedback_exciter.h`, `noise_burst_exciter.h`), and the one runtime-selectable
path, `noise_layer.h:81`'s `denormColor`, is hard-capped to Brown/Pink/White/Violet
(`noise_layer.h:306-309`).

### S10.3 `ResonatorBank::setFrequency` re-derives Q (FR-099)

```cpp
void setFrequency(size_t index, float hz) noexcept {
    if (index >= kMaxResonators) return;
    frequencies_[index] = clampFrequency(hz);
    qValues_[index] = rt60ToQ(frequencies_[index], decays_[index]);   // NEW: match setDecay (:349)
    updateFilterCoefficients(index);
}
```
so a drifting frequency no longer silently changes the effective RT60. Zero-regression-risk: a
word-bounded sweep for the exact class `ResonatorBank` (excluding the unrelated
`ModalResonatorBank` declared in the same header) finds no consumer outside
`dsp/tests/unit/processors/resonator_bank_test.cpp` and the compile-only `dsp/lint_all_headers.cpp`.
It is invisible to the organism's own behaviour (S6.2 writes `setQ` on the next line), and it makes
the library self-consistent for Phase 3, which will drift frequencies on the same class.

---

## S11. CPU model — the one place this phase can fail on arrival

FR-095's budget is **106 666 ns per 512-sample block at 48 kHz** (1 % of one core). The measured
figures already in this repo make the reference configuration SC-004 (c) genuinely uncertain, so the
plan front-loads the measurement instead of discovering it at the end.

**Estimate, from figures measured in this repo (labelled as estimates where they are scaled):**

| Stage (SC-004 (c): 4 slots, 3 resonators + 2 combs each) | ns / 512-block | Basis |
|---|---|---|
| combs, hoisted (8 combs) | ~30 700 | `continuous_body_perf_test.cpp:204-205` (6 combs → ~23 000), scaled |
| combs, **per-sample** (8 combs) — what you get **without** `snapSmoothers()` | ~99 000 | `timevar_comb_bank.h:704` (6 combs → 50 849) — but that figure is the pre-optimisation shape; `:835` records 74 580, scaled ⇒ 99 440 |
| `StochasticFilter` × 4 | ~20 000 – 40 000 | one `std::tan` + one reciprocal per sample per slot (`stochastic_filter.h:244-248` → `svf.h:204-212`, `:489-491`); 2048 `tan` calls per block |
| `NoiseGenerator` × 4 | ~20 000 – 60 000 | 13 level smoothers + master smoother + an unconditional pink-filter step per sample (`noise_generator.h:387-576`, `:393`), regardless of which single type is enabled |
| `ResonatorBank` × 4 (12 biquads/sample) | ~10 000 – 20 000 | `resonator_bank.h:484-506` |
| dust engine (1 slot) | ~12 000 – 18 000 | **24 pool iterations + 24 branch tests per sample**, plus up to 24 envelope lookups (`grain_envelope.h:165`). S6.1 iterates the whole fixed pool (`for (DustGrain& g : grains) if (g.active)`) regardless of live count, so the cost is 12 288 iterations per 512-block for one dust slot and does **not** scale down with concurrency. The earlier "4 envelope lookups/sample ⇒ ~3 000 ns" row under-counted by 4–6× in the load-bearing direction |
| control-rate work (8 steps/block) | < 2 000 | 24 `Biquad::configure` + lane advances |
| **estimated total** | **~94 000 – 170 000** | |

The dust row is bounded work (the FR-035 ceiling caps concurrency at `kMaxDustGrains`), so this is
an estimate defect rather than an RT-safety one — but with the total already straddling the
106 666 ns budget it materially changes the T0 stop-and-surface decision, which is why it is
corrected here rather than discovered at T8. If T0 shows the pool scan is the deciding term, the
cheap structural fix is to compact the pool (`activeCount_`, swap-with-last on death) so the loop
becomes O(live grains); that is a change to S6.1 only and needs no spec amendment. **T0's stand-in
dust loop must iterate the full 24-slot pool**, not a mean-concurrency subset, or the probe measures
a shape the design does not render.

Two conclusions follow, and both are structural, not tuning:

1. **`snapSmoothers()` on every comb bank every control step is mandatory** (S6.3). Without it the
   combs alone are ~93 % of the budget.
2. **The reference configuration may still miss.** The `StochasticFilter` per-sample `std::tan` and
   the `NoiseGenerator` 14-smoother loop are both inside shipped components this spec's Non-Goals
   decline to refactor.

**Therefore the first implementation task is a measurement, not a component** (S13, T0): a
`[.perf]`-tagged probe that measures each of the six stages above standalone, at the exact
SC-004 (c) shape, and prints the per-stage breakdown. That breakdown is *precisely* the artefact
FR-095's stop-and-surface policy requires on a miss, and producing it before the organism exists
costs a day instead of a phase.

**If the probe (or SC-004 (c)) misses 106 666 ns**, per FR-095 / OQ-CPU-POLICY the build **stops and
surfaces** with the measured ns and the per-stage table. No implementing agent may lower
`kMaxSources` / `kMaxResonatorsPerSource` / `kMaxCombsPerSource` / `kMaxDustGrains`, raise the
budget, or relax a threshold. The options to put to the user, each with its cost, are:

* **A — `StochasticFilter` hoisted path (a fourth shared-component amendment).** Add
  `snapSmoothers()` + a `processBlock` that hoists the per-sample `setCutoff`/`setResonance` when
  both smoothers report `isComplete()`, exactly mirroring the `TimeVaryingCombBank` change already
  blessed in this repo (`continuous_body_perf_test.cpp:204-205`, item 3). Bit-identical under the
  same guard argument. Estimated saving 15 000–35 000 ns. Requires a no-change guarantee for
  `stochastic_filter_test.cpp` and any other consumer.
* **B — `NoiseGenerator` enabled-only smoother path.** Skip the 12 disabled types' smoother steps.
  Explicitly excluded by this spec's Non-Goals ("Refactoring `NoiseGenerator`'s per-sample
  structure"); would need a spec amendment and a five-consumer no-change guarantee.
* **C — cap or budget change.** Forbidden unilaterally; user decision only.

---

## S12. Memory (FR-096, SC-014)

Only significant term: comb delay lines. `TimeVaryingCombBank::prepare` sizes **all**
`kMaxCombs = 8` lines regardless of `setNumCombs` (`:154`, `:88`), and `DelayLine::prepare` sizes to
`nextPowerOf2(static_cast<size_t>(sr × seconds) + 1)` floats — a **truncating** cast, not a round
(`delay_line.h:267-278`, cast at `:269`); `TimeVaryingCombBank::prepare` feeds it
`maxDelayMs_/1000.0f` seconds (`timevar_comb_bank.h:436-440`), so the comb-bank sizing this formula
depends on is truncation-based too. `nextPowerOf2` absorbs the ±1-sample difference in every
configuration cited below, so no KiB figure changes — but the formula now describes the API that is
actually being reused.

```
combBytesPerSlot = kMaxCombs * nextPowerOf2(trunc(sr * maxCombDelayMs/1000) + 1) * sizeof(float)
allocatedBytes_  = kMaxSources * combBytesPerSlot + kDustEnvelopeTableSize * sizeof(float)
```
Default (48 kHz, 50 ms): `2400+1 → 4096` ⇒ 16 KiB per comb ⇒ 128 KiB per slot ⇒ **512 KiB** per
organism, plus the 8 KiB dust table (which is a member array, not heap — counted anyway so the
header's documented figure and `getAllocatedBytes()` agree). Worst case (192 kHz, 200 ms):
`38400+1 → 65536` ⇒ 256 KiB per comb ⇒ **8 MiB** per organism; documented in the header so a
Phase-10 voice count is chosen against the real number.

`getAllocatedBytes()` computes this in `prepare` from the lengths it requests.
`AllocationDetector` is used only for the **count** — it has no byte accounting
(`allocation_detector.h:83-89`; the operator-new replacements discard `size`,
`allocation_operator_overrides.h:66-95`).

---

## S13. Test plan

### S13.1 New translation units

| TU | Target | Contents | `-fno-fast-math`? |
|---|---|---|---|
| `dsp/tests/unit/systems/noise_organism_test.cpp` | `dsp_systems_tests` | SC-003, SC-006, SC-007, SC-010 (incl. the per-slot RMS and FR-066 freeze arms), SC-013, SC-014, SC-016, SC-017 (incl. the overwrite arm), SC-018 (incl. the lost-write and FR-012 remembered-type arms), SC-021, SC-005 (a), plus `NoiseOrganism_ControlSurfaceClamps` (FR-032/FR-035/FR-090/FR-011) and the remaining FR-level cases | **No** — must build in the FP mode the header ships in |
| `dsp/tests/unit/systems/noise_organism_spectral_test.cpp` | `dsp_systems_tests` | SC-001, SC-002, SC-005 (b), SC-008, SC-009, SC-019, SC-020 (the `[long]` set) | No |
| `dsp/tests/unit/systems/noise_organism_perf_test.cpp` | `dsp_systems_tests` | SC-004 (a)–(e) + the T0 stage probe | **No** — `-fno-fast-math` would move the figures the baselines pin |
| `dsp/tests/unit/systems/noise_organism_nonfinite_test.cpp` | `dsp_systems_tests` | SC-015 only | **Yes** — the only new TU in that block (FR-097) |

Cases added to **existing** TUs (SC-011):
`dsp/tests/unit/processors/noise_generator_test.cpp` → `NoiseGenerator_SetSeedIsOptInAndReproducible`;
`dsp/tests/unit/primitives/noise_oscillator_test.cpp` → `NoiseOscillator_VelvetRadioStaticFixed`;
`dsp/tests/unit/processors/resonator_bank_test.cpp` → `ResonatorBank_SetFrequencyRederivesQ`.
All three files are already registered (`dsp/tests/CMakeLists.txt:167`, `:140`, `:192`) and all three
already sit in the `-fno-fast-math` list (`:575`, `:568`, `:663`) — no CMake change for them.

### S13.2 Criterion → case → assertion

| SC | Case (TU) | Assertion strategy |
|---|---|---|
| SC-001 | `NoiseOrganism_LongRenderStationarity` `[long]` (spectral) | 10 min @48 k, FR-016 defaults, 60 windows of 10 s. `extractAudioFeatures(window, 48000).rmsDbfs` (`audio_features.h:37`); (a) every window within ±3.0 dB of the median (`statistical_utils.h:90 computeMedian`); (b) least-squares slope within ±0.5 dB/10 min; (c) every window ∈ (−60, −3] dBFS; (d) sample `getSourceGain` with level+wake pinned, assert `breathGain ∈ [1−0.45d, 1+0.45d]`, `> 0`, no sign change |
| SC-002 | `NoiseOrganism_SpectralMotion` `[long]` (spectral) | 350 s (`≥10·T`, `T = 1/0.03 = 33.3 s`). Per 100 ms frame, the five `AudioFeatures::band` fractions; mean-remove, normalised ACF, first zero crossing `L`, **capped at 0.25 × record length** (an unmeasurable lag is a failure). (a) ≥3 bands with `L ∈ [0.4T, 3.0T]`; (b) control arm via `setWanderEnabled(false)` **plus `setSourceBreathing(slot, rate, 0.0f, irr)` on every slot** — `setWanderEnabled` does not touch the FR-070 breathing lane (S7.3), and with breathing left at the FR-016 default depth of 0.25 (±0.92 dB per slot, the designed dominant contributor to broadband level variation) the required 3× CV separation would have to come entirely from the spectral lanes' residual level contribution, which nothing estimates; with breathing zeroed the control arm is genuinely static. Assert every band `L < 0.4T` **and** broadband RMS CV ≥3× below the wander-on CV; T11 records both measured CV figures in `compliance.md` so the clause is shown reachable, not assumed; (c) broadband RMS CV ≤ 0.06 while ≥1 band fraction CV ≥ 0.10; **(d) comb-lane excursion arm** (FR-063, S6.3): 120 s at maximum `setCombWander` depth and rate, sample `getCombCurrentDelayMs(slot, n)` per control step, REQUIRE the realised peak-to-peak excursion ≥ **25 %** of the configured span `2 × 0.01 × combWanderPct × combBaseDelayMs[n]`, and record the realised percentage — this is the arm that makes the S6.3 slew bound's cost visible instead of letting it silently freeze the lane |
| SC-003 | `NoiseOrganism_NoAllocationAfterPrepare` (main) | `AllocationScope` (`allocation_detector.h:111`) around 20 000 × 512-blocks that also call **every public setter declared in S1.2** once per block, plus `reset()`. REQUIRE count **0**. The enumeration is deliberately *not* written out in the test: it is driven by one `touchEverySetter(NoiseOrganism&, std::size_t block)` helper that calls each setter in S1.2's declaration order, so a setter added by a later phase is covered by construction. The earlier hand-written list silently omitted `setSourceBreathing`, `setResonatorAnchor`, `setResonatorDecay`, `setFilterBaseCutoff`, `setFilterBaseResonance`, `setDustCarrierColor` and `setHissBright` — several of which trigger `applyConfiguration()` (S2.3), the routine most likely to reach a sub-component allocation path, so the gap was in the load-bearing direction |
| SC-004 | `NoiseOrganism_CpuBudget` `[.perf]` (perf) | ns/512-block @48 k, best-of-25 × 500 blocks after 400 warm-up, per the `atmosphere_engine_perf_test.cpp:22-70` idiom. Five configurations (a)–(e); (a)(b)(c) gated at `baseline × 1.5` with the precedent's **two different** compile-time clauses — a ceiling `static_assert(kBaselineX * kRegressionFactor <= kReferenceNs, …)` **and a floor** `static_assert(kBaselineX >= kReferenceNs / 50.0, …)` (`atmosphere_engine_perf_test.cpp:34-42`, read this session), for each of the (a)–(c) baselines. The floor is not a restatement of the ceiling: its documented purpose is catching "a baseline recorded from a no-op or misconfigured run", and without it a baseline taken from a probe that renders nothing — an un-`prepare`d organism, which S5.1 makes fill silence and advance nothing — compiles and passes SC-004 forever. The earlier plan text wrote the same inequality twice (`baseline * 1.5 <= 106666` and `baseline <= 106666/1.5`) and so shipped no anti-no-op guard at all; (d) and (e) tracked against their own baselines only, with (e) also asserting a **measured saving** vs (c) so FR-071's dormancy claim is a number, not a claim |
| SC-005 (a) | `NoiseOrganism_BoundedShort` (main, **untagged**) | 60 s at configuration (d), all depths max, fastest rate, feedback at 0.9, decay 30 s, Q-wander 1.0, seeded wake/dormant schedule. Every sample `detail::isFinite` (`db_utils.h:118`) — never `std::isnan`; peak < 4.0; `getClampEngagementCount() == 0`; no 1 s window below −60 dBFS |
| SC-005 (b) | `NoiseOrganism_BoundedSoak` `[long]` (spectral) | 30 min, same fixture, all of (a) plus final-minute RMS within ±6 dB of the first minute after a 30 s settle |
| SC-006 | `NoiseOrganism_SeedDeterminism` (main) | same seed/config/rate ⇒ `max|diff| == 0` over 10 s; a third differently-seeded instance ⇒ `|Pearson r| ≤ 0.05`. (a) `reset()` with no setter since `prepare` reproduces the post-`prepare` stream exactly; (b) after configuration changes, `reset()` renders **non-silent** and sample-exactly matches a fresh `prepare` + the same calls — the arm that catches a forwarded `ResonatorBank::reset()` (`:226-232`) |
| SC-007 | `NoiseOrganism_SourceDecorrelation` (main) | 4 identically configured slots isolated with `setSourceDormant(other, true)` (**not** `setSourceLevel(-96)`); all 6 pairwise `|r| ≤ 0.05` over 10 s. Anti-vacuity arm built in-process: two bare `NoiseGenerator`s with **no** `setSeed` ⇒ REQUIRE `|r| > 0.99`; then distinct `setSeed(deriveStreamSeed(seed, salt))` ⇒ `|r| ≤ 0.05` |
| SC-008 | `NoiseOrganism_SampleRateInvariance` `[long]` (spectral) | 60 s at 44.1/48/96/192 k, same seed. (a) RMS within ±1.0 dB; (b) finite + no 1 s window < −60 dBFS; (c) SC-002 lag `L` of the strongest-moving band agrees within ±15 %, and the `setSourceWake(0→1)` **0–100 %** duration read from `getSourceGain` is **50 ms ± 5 ms** at every rate; (d) mid-render `prepare()` at a new rate stays finite and non-silent. Spectral shape deliberately **not** asserted (FR-093) |
| SC-009 | `NoiseOrganism_NoZipperUnderDrift` `[long]` (spectral) | (a) gain domain: `getSourceGain` sampled every control step across 100 randomised level steps / dormant toggles / wake transitions / type+model changes / **`setNumSources` reductions and increases** — monotone through each ramp, 0–100 % duration 50 ms ± 5 ms. The `setNumSources` transitions are FR-010's and FR-072's only gate: reducing the count must silence the dropped slots over the same click-free ramp (S8.3's `gateSteady(slot) = (dormant || slot >= numSources) ? 0 : wake`), and the criterion additionally REQUIREs the dropped slot's `getSourceGain` ramps **monotonically to exactly 0** over 50 ms ± 5 ms. SC-003 exercises `setNumSources` only for allocation counting, so without this arm the dropped-slot ramp has no test at all. (b) envelope domain: 25 ms-frame RMS in dB over 5 min; first **measure** the estimator noise floor on a fixed-gain render and REQUIRE the acceptance threshold ≥3σ above it; then `maxΔ ≤ 1.5 ×` the same statistic on a `setWanderEnabled(false)` render. `ClickDetector` explicitly not used (it 5σ-thresholds the first derivative, `artifact_detection.h:38-99`, which flags every sample of broadband noise) |
| SC-010 | `NoiseOrganism_DormantLanesFreewheel` (main) | (a) `getResonatorCurrentFrequency` / `getResonatorCurrentQ` / `getFilterCurrentCutoff` trajectories agree within 1e-5 relative across 60 s dormant vs awake, and `getSourceRms` (S5.6, source stage) agrees within 0.5 dB over the first 250 ms after wake; (b) after `tSettle = max(decay, 8·maxCombDelayMs/(1−fb))` = 1.5 s at the FR-016 defaults, RMS agrees within ±1.0 dB and each band fraction within ±0.05 over the following 10 s. No sample-identity clause. **(c) per-slot RMS aliasing arm:** `numSources = 2` with deliberately different per-slot levels and noise types (e.g. slot 0 White at −6 dB, slot 1 Brown at −24 dB), REQUIRE `getSourceRms(0)` and `getSourceRms(1)` differ by the expected offset (±1.5 dB, the offset being a source-stage quantity so the level term is exact and only the type term is measured). This is the arm that fails if the RMS is computed in `updateControl` from the shared `scratchA_` (S5.2/S5.6): (a) alone cannot detect that, since both of its arms alias identically and the aliasing vanishes entirely at `numSources == 1`. **(d) FR-066 freeze-then-restore arm:** render 30 s with `setFilterWander(slot, 0.0f, s)`, then restore the depth, and REQUIRE the `getFilterCurrentCutoff` trajectory after restore matches an always-on reference within 1e-5 relative — and, negatively, that the first post-restore sample is **not** the base cutoff. Without it, an implementation that skips `lane.processBlock(64)` whenever a span is 0 (a plausible CPU saving) passes every other case in S13.2, because (a) proves freewheeling only under *dormancy*, a different branch |
| SC-011 | three cases in the existing TUs + full-suite run | `NoiseGenerator_SetSeedIsOptInAndReproducible`: (i) two un-seeded instances identical; (ii) two successive `reset()`s on an un-seeded instance differ; (iii) `reset()` after `setSeed` reproduces exactly; (iv) different seeds ⇒ `|r| ≤ 0.05`. `NoiseOscillator_VelvetRadioStaticFixed` (v): Velvet, and separately RadioStatic, no longer match a White instance from the same seed. `ResonatorBank_SetFrequencyRederivesQ` (vi): after `setDecay` + `setFrequency`, the impulse response's envelope-fit RT60 tracks the configured decay within a measured tolerance. Then `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `dsp_primitives_tests`, `membrum_tests` all green with no edits to existing cases |
| SC-012 | CI gates, recorded in `compliance.md` | `node tools/lint-odr.js`, `lint-layers.js`, `check-portability.js`, `lint-simd-aligned-loadstore.js` all exit 0 |
| SC-013 | `NoiseOrganism_RenderFingerprint` (main) | `fingerprintRender` over 30 s of SC-004 (c) at a pinned seed; `compareFingerprints(actual, ref, kMetricTolerance, measuredSampleTol)` (`render_fingerprint.h:124-126`) with `measuredSampleTol` derived from a three-toolchain probe (MSVC, `g++ -O3 -ffast-math`, `clang++ -O2`) and recorded in `compliance.md`; the shared `kSampleTolerance` is **not** loosened. **Acceptance requires showing it fails** on an injected defect (collide the comb lane salt with the resonator lane salt) |
| SC-014 | `NoiseOrganism_PrepareFootprint` (main) | `getAllocatedBytes() ≤ 640 KiB` at the default config, cross-checked against the S12 formula recomputed in the test; header figure within 5 %; `AllocationScope` count over `prepare` ≤ 64 |
| SC-015 | `NoiseOrganism_NonFiniteSetterInputs` (nonfinite TU) | every public setter fed NaN/+Inf/−Inf **built from bit patterns through a volatile sink** (never `std::numeric_limits`); (a) each is replaced by **the neutral named in S1.2a's normative table**, read back through the FR-015 surface — the table is the design element this arm asserts against, and without it (a) and (c) had no defined expectation at all; (b) every rendered sample finite (exponent test); (c) a rejected value must not perturb state: post-injection RMS within ±0.5 dB of an uninjected reference, and every *other* getter unchanged from its pre-injection value. Note (b) is a real trace, not a formality — `std::clamp` returns NaN unchanged, and S1.2a records the `setResonatorAnchor(NaN)` → NaN biquad coefficients → permanent NaN output path it would otherwise open |
| SC-016 | `NoiseOrganism_BlockSizeInvariance` (main) | 240 000 samples three ways from identical fresh instances: one call; 469 × 512; the irregular cycle 36, 28, 1000, 1, 511, 2048. REQUIRE `max|diff| == 0`. Same binary, same process — not a stored golden |
| SC-017 | `NoiseOrganism_GuardLadder` (main) | (a) `processBlock(nullptr, 512)` writes nothing and the following render is bit-identical to an uninterrupted reference at the same absolute position; (b) `processBlock(out, 0)` same; (c) before `prepare`, exactly `numSamples` zeros and nothing advances; (d) 100 000 samples rendered in **one** call equal the same 100 000 rendered as **195 × 512 + one 160-sample block** (`195 × 512 = 99 840`; `+160 = 100 000`), `max|diff| == 0`. The spec's "196 × 512" is 100 352 — 352 samples longer than the single block, so the comparison as transcribed does not close; (e) **overwrite arm** (FR-003, S5.3): pre-poison the output buffer with a non-zero bit pattern, render with slot 0 dormant and separately with all `kMaxSources` slots dormant, and REQUIRE every returned sample is exactly `0.0f` — never the sentinel. Without step 0's unconditional `std::fill_n` the buffer is never written and this arm fails |
| SC-018 | `NoiseOrganism_ModelChangeContinuity` (main) | `getSourceGain` on the control grid + the SC-009 (b) envelope across 100 model and 100 type changes: duck present, monotone each direction, total **0–100 %** duration 50 ms ± 5 ms per leg. **Naive-path arm**: with the duck removed, the envelope `maxΔ` exceeds the bound. **Coalescing arm** (Q5): 1000 identical writes ⇒ `getSourceGain` never leaves 1.0. **Coalescing arm, pre-swap**: a second genuine change written while the *Down* leg is still running ⇒ exactly **one** 50 ms duck. **Lost-write arm (new):** write a second genuine change at a **randomised offset anywhere inside the whole 50 ms duck** — Up leg included — and REQUIRE, once the trajectory settles, that `getSourceModel`/`getSourceNoiseType` equal the **last** value written and that the render is non-silent. A change arriving during the Up leg legitimately costs a second duck, so this arm asserts the *final state*, not the duck count; the "exactly one duck" arm above is scoped to the Down leg. Without S8.3's `duckState == Up` re-arm the pending write is discarded and this arm fails — while "exactly one 50 ms duck" would have *passed*, since a dropped change produces one duck too |
| SC-019 | `NoiseOrganism_ModelRosterAndDustLevel` (spectral) | (a) **15 cells, not 48**: the 12 selectable `NoiseType`s on a `Direct` slot (calibrated by `kSourceDriveDb`) plus the 3 composed models (calibrated by `kModelTrimDb`, S9.1). FR-012 makes type selection effective only for `Direct`, so 36 of the 48 type×model cells are duplicates of 3 distinct renders. Each cell: 5 s isolated slot, RMS > −60 dBFS **and** within ±3 dB of the White reference — this is what makes both tables non-placeholder, and the ±3 dB window is now backed by a mechanism for the composed models, which `kSourceDriveDb` alone cannot reach (`GranularDust` never uses the generator's amplitude at all). Plus `setSourceNoiseType(ModulationNoise)` snaps to `TapeHiss` via `getSourceNoiseType`, and a bare `NoiseGenerator` with only `ModulationNoise` and a zero sidechain renders exactly `0.0f`; plus a 0-resonator/0-comb slot renders > −60 dBFS (catches a forwarded zero count). **Level-ownership arm (new):** render one slot at `setSourceLevel(-24)` and at `setSourceLevel(-12)`, everything else identical, and REQUIRE the absolute RMS difference is exactly the requested 12 dB, ±0.5 dB. This is the only arm that catches a double-applied slot level (generator *and* mix stage), which would move the render 24 dB; the ±3 dB-vs-White comparison cannot, because the error is uniform across all types. (b) dust sweep 100/400/1600/6400/20000 imp/s at an **explicitly requested 40 ms**: RMS varies ≤6 dB peak-to-peak, no adjacent step > 3 dB; **plus at the 20 000 imp/s ceiling** — where mean grain concurrency equals `kMaxDustGrains` and instantaneous concurrency exceeds it about half the time, so S6.1's steal policy is exercised — the 25 ms-frame envelope `maxΔ` must stay inside the SC-009 (b) bound, proving the largest-phase steal rather than asserting it |
| SC-020 | `NoiseOrganism_MetallicHissInharmonicity` (spectral) | 20 s isolated `MetallicHiss`, comb wander depth 0; magnitude spectrum; ≥3 peaks; every ratio to the lowest peak ≥4 % from the nearest integer; measured peaks match `f[n] = fundamental·sqrt(1 + n·spread)` within 3 %. `getTuningMode()` deliberately not asserted (`:515` makes it `Custom`) |
| SC-021 | `NoiseOrganism_QWanderAudible` (main) | **Measured off the applied state, because the lane extreme is unreachable.** `qFactor = 1 − kQWanderSpan·resQWander·(1+b)/2` where `b = resFreqLane[i].getCurrentValue()` is a free-running OU output (S7.1): `setResonatorQWander(0)` reaches `qFactor = 1`, but nothing in S1.2's public API pins `b = +1`, and `setWanderEnabled(false)` zeroes every span and *also* yields `qFactor = 1`. So "hold the factor at `1.0` and at `1 − 0.9`" cannot be written. Instead: one resonator, all other depths 0, `setResonatorQWander(1.0)` at the default (slowest) wander rate; **fixture anchored at 70 Hz with `setResonatorDecay(1.0)`**, giving `rt60ToQ(70, 1.0) = π·70/ln1000 ≈ 31.8` (`resonator_bank.h:92-98`) — comfortably below `kMaxResonatorQ = 100`, so the downward factor is fully observable rather than eaten by the clamp (at the FR-016 default decay of 1.5 s the top anchors are already saturated). Render 600 s (20 τ at τ = 30 s, long enough that the OU excursion is statistically assured); sample `getResonatorCurrentQ` per control step; select the two 10 s segments with the highest and lowest mean Q; measure each segment's −3 dB bandwidth around the peak. REQUIRE (i) the observed Q ratio between the two segments is **≥ 3** — this is the anti-inert guard that keeps FR-064 from being vacuous — and (ii) the measured bandwidth ratio matches the observed Q ratio within ±25 %, and (iii) both segments render non-silent. Together (i) and (ii) deliver the spec's "≥3× bandwidth change" without depending on an unreachable lane value |

Two further cases in the main TU, both render-free, covering normative clauses that no criterion
above reaches:

| Case (TU) | Covers | Assertion |
|---|---|---|
| `NoiseOrganism_ControlSurfaceClamps` (main) | FR-032, FR-035, FR-090/FR-057, FR-011/FR-015 | Four compact setter-then-getter groups. (i) `setDustCarrierColor(slot, NoiseColor::Velvet)` ⇒ `getDustCarrierColor(slot) == NoiseColor::Brown` (FR-032's rejection is observable, not silent). (ii) `setDustDensity(slot, 20000)` then `setDustGrainMs(slot, 200)` ⇒ `getDustDensity(slot) == 20000` **and** `getDustGrainMs(slot) == 1.2f` (the FR-035 ceiling `1000·kMaxDustGrains/density`), i.e. the clamp is bidirectional and both effective values are readable. (iii) `setCombFeedback(slot, 0.99f)` ⇒ `getCombFeedback(slot) == kCombFeedbackCap == 0.9f` (FR-090). (iv) every setter called with `slot == kMaxSources` and with `index == kMaxResonatorsPerSource` is a silent no-op — no getter anywhere changes — and every getter called out of range returns the S1.2 documented neutral (`0.0f`, `Direct`, `Brown`, `false`). Costs no render time and turns four normative clauses from prose into gates |
| `NoiseOrganism_ModelChangeContinuity`, extra arm | FR-012's remembered type | `setSourceNoiseType(0, NoiseType::Pink)` → `setSourceModel(0, MetallicHiss)` → `setSourceModel(0, Direct)` ⇒ REQUIRE `getSourceNoiseType(0) == NoiseType::Pink` and the render is non-silent. FR-012's "the selected type is remembered across model changes" is designed (S1.3's `requestedType` vs `activeType`) but SC-018 measures only gain-trajectory continuity and SC-019 (a) only non-silence and the `ModulationNoise` snap, so nothing else asserts it |

### S13.3 Seeds, tolerances, determinism hygiene

Every case pins `setSeed(k)` with a case-local constant; no case depends on another's RNG state. No
stored float goldens anywhere — SC-013 is the only render pin and it uses `render_fingerprint.h`
with a measured, per-comparison sample tolerance; `tools/lint-float-bit-goldens.js` must stay green.
All finiteness checks use `detail::isFinite` / an explicit exponent-field test; no `std::isnan`,
no `std::numeric_limits<float>::quiet_NaN()`/`infinity()` (they fold under the macOS `-ffast-math`
leg).

---

## S14. Spec corrections applied by this plan

Each item is a place where the spec's literal wording would produce a defect if implemented as
written. None changes a requirement's intent or relaxes a threshold.

1. **`BrownianDrift::setSmoothness` takes a normalized `[0,1]`, not a tau in seconds**
   (`brownian_drift.h:149-153`, mapping in `updateCoefficients`). FR-069's table and FR-061/FR-062/
   FR-067 say the tau is "forwarded to `setSmoothness`". S7.2 inserts the inverse mapping
   `s = (tau − kTauMin)/(kTauMax − kTauMin)`. At the default rate both readings coincide, so SC-002
   is unaffected; at every other rate the literal reading is wrong.
2. **The `HarmonicCloud` chunk loop is block-relative, not an absolute grid** (`harmonic_cloud.h:`
   `processStereoBlock`). FR-007/SC-016 demand `max|diff| = 0` under a 36+28 split, so S5.1 adds the
   residual `controlPhase_` counter. Copying the cited idiom verbatim would fail SC-016.
3. **`getSourceRms` must measure the source stage, not the slot's mix contribution** (S5.6), or
   SC-010 (a)'s 0.5 dB agreement over the first 250 ms after wake is unsatisfiable by a correct
   implementation — the two arms' chain states are precisely what SC-010 (b) allows to differ.
4. **`TimeVaryingCombBank::snapSmoothers()` must be called on every control step** (S6.3). FR-063
   says no extra slew limiter is added "because the bank already smooths delay changes"; that
   smoothing is exactly what pins the bank to the per-sample path and costs ~99 000 ns of a
   106 666 ns budget. The snap moves continuity to the organism, and S6.3 adds the matching
   per-control-step slew bound (0.25 samples of delay per step) so nothing is lost, only relocated.
   This is the shipped `ContinuousBody` pattern and the bank's own documented usage
   (`timevar_comb_bank.h:352-357`). **FR-063 is amended, not merely questioned:** its stated reason
   for adding no slew limiter — "the bank already smooths delay changes" — stops holding the moment
   the snap is called, so the organism-side bound is a required replacement. Its size is
   **measured** by the `[.calibration]` case rather than assumed, because the conservative 0.25
   samples/step figure throttles the lane by ~32× at the fastest legal setting; SC-002 (d) measures
   the realised excursion so the cost is a recorded number. See S6.3 and Open Question 3.
5. **FR-013's swap point** is taken at the exact zero *sample* rather than "at the zero point on a
   control step", so the duck's total duration is exactly 50 ms (SC-018's ±5 ms, SC-009 (a)'s
   monotonicity). The setter pair is allocation-free (`noise_generator.h:255-261`).
6. **`setSeed` must be re-applied at the end of `prepare()` and inside `reset()`**, because
   `NoiseGenerator::prepare` ends with `reset()` (`:182`) and `reset()` scrambles the RNG (`:189`)
   unless the FR-081 latch is set. The spec's salt table implies this but does not order it.
7. **SC-008 (c)'s "10–90 % duration is 50 ms ± 5 ms" is wrong on a correct implementation, and the
   spec contains its own counter-derivation.** SC-009 (a) already says so for the same ramp:
   FR-073's per-sample linear-in-gain ramp has a 10–90 % duration of `0.8 × 50 = 40 ms`, which is
   why SC-009 (a) was restated in 0–100 % terms. The shipped `LinearRamp` (S1.3) is exactly linear
   over `rampSamples_`, so an SC-008 arm written to the spec's wording fails by 10 ms. **All three
   rows (SC-008 (c), SC-009 (a), SC-018) use the single wording "0–100 % duration is 50 ms ±
   5 ms."** No threshold moves; only the measurement convention is made consistent.
8. **`setMean` is not on every lane.** S2.3's "every lane gets `setMean(0.0f)`" would not compile:
   `setMean` is a member of `BrownianDrift` alone (`brownian_drift.h:165`); `PerlinNoiseSource`
   (full public API at `perlin_noise_source.h:233-262`) and `BreathingModulator`
   (`breathing_modulator.h:164-190`) declare no mean or bias setter, and `Slot::combLane` /
   `Slot::breathing` are exactly those two types. The call is scoped to the three `BrownianDrift`
   lane kinds; both other lanes are zero-mean by construction.
9. **FR-017's `requestedDb` is an organism constant, not the user's slot level** (S9.1). Read with
   `requestedDb = levelDb` while the mix stage also applies `dbToGain(levelDb)`, the slot level is
   applied twice and a −12 dB slot renders at −24 dB. FR-014 is the tiebreaker — `setSourceLevel`
   "sets the slot's contribution **to the mix**" (spec.md:332) — so the mix stage owns the level and
   the generator carries `kSourceReferenceDb + kSourceDriveDb[type]`, a constant per type. FR-017's
   normative sentence holds literally under this binding. A new `kModelTrimDb[4]` covers the three
   composed models, which `kSourceDriveDb` structurally cannot reach.
10. **FR-003's overwrite contract requires an unconditional zero-fill** (S5.3). "First slot writes
    with `=`" leaves the caller's buffer untouched whenever slot 0 is dormant or dropped, which is
    the configuration three of SC-007's four renders and all of SC-004 (e) use. The shipped idiom
    (`timevar_comb_bank.h:761-763`) zero-fills first and accumulates throughout.
11. **`LinearRamp` is reused, not redeclared** (S1.3). `Krate::DSP::LinearRamp` already exists at
    `primitives/smoother.h:305`; a nested type of that name would shadow it, and the hand-rolled
    copy would have dropped the shipped `setTarget` NaN/Inf guard (`:342-354`) and the `process()`
    denormal flush (`:386`). The shipped ramp's law is constant-*duration* despite its class doc,
    which is precisely what SC-009 (a)/SC-018 require.

---

## S15. Build integration

* `dsp/tests/CMakeLists.txt` — add the four new TUs by name to the `dsp_systems_tests` source list
  (the block ending at `:389`; the list is enumerated, not globbed, and `:360-361` says so), each
  with a comment naming the spec and the criteria it owns.
* `dsp/tests/CMakeLists.txt` — add **only** `unit/systems/noise_organism_nonfinite_test.cpp` to the
  `-fno-fast-math -fno-finite-math-only` block (near `:776`), with the standard comment explaining
  why its three siblings are deliberately absent (FR-097): the ordinary TUs must prove the FR-008
  guards in the FP mode the header ships in, and the perf TU's baselines are pinned to figures
  `-fno-fast-math` would change.
* No change to `dsp/CMakeLists.txt` — the component is header-only. But `dsp/lint_all_headers.cpp`
  is an **enumerated list**, verified this session: 166 explicit `#include` lines, with the Layer 3
  (Systems) block at `:149-175` (e.g. `:166` `systems/timevar_comb_bank.h`). It picks nothing up
  automatically. **T3 therefore adds `#include <krate/dsp/systems/noise_organism.h>` to that Layer 3
  block**, and SC-012's compliance row cites the added line number. Without it,
  `./tools/run-clang-tidy.ps1 -Target dsp` gives the new header zero strict-tidy coverage and
  SC-012's clang-tidy gate passes vacuously for it.
* No plugin, CI, clang-tidy or CMake-preset changes: this phase adds no plugin and no new target.

Build and run:
```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release \
  --target dsp_systems_tests dsp_processors_tests dsp_primitives_tests dsp_effects_tests membrum_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tail -20
```
Pre-commit gates: `node tools/check-portability.js`, `node tools/lint-layers.js`,
`node tools/lint-odr.js`, `node tools/lint-float-bit-goldens.js`,
`node tools/lint-simd-aligned-loadstore.js`, then `./tools/run-clang-tidy.ps1 -Target dsp`.

---

## S16. Implementation order

Each step ends green; nothing later depends on a step's cleanup.

| # | Step | Verify |
|---|---|---|
| **T0** | **Stage-cost probe** (`noise_organism_perf_test.cpp`, `[.perf]`): measure `NoiseGenerator`, `ResonatorBank` (3 enabled), `TimeVaryingCombBank` (2 combs, per-sample **and** snapped/hoisted), `StochasticFilter`, and a stand-in dust loop **that iterates the full 24-slot pool per sample** (the shape S6.1 renders, not a mean-concurrency subset — see S11), at 512 blocks / 48 kHz, and print the per-stage table | numbers exist; if the ×4 sum exceeds 106 666 ns, **stop and surface** per FR-095 before writing the component |
| T1 | Failing tests for the three shared-component amendments (SC-011 (i)–(vi)) in the three existing TUs | all three fail for the right reason |
| T2 | Apply FR-080, FR-098, FR-099 | the three new cases pass; `dsp_processors_tests`, `dsp_primitives_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `membrum_tests` all green with no edits to existing cases |
| T3 | Header skeleton: constants, `PrepareConfig`, `Slot` (incl. `appliedCombDelayMs[]`), the S1.2a `sanitise` helper and its neutral table, salt table + `static_assert`s, full public API with bodies returning neutrals — reusing `Krate::DSP::LinearRamp` from `primitives/smoother.h`, declaring no type of that name; register the four TUs in CMake **and add `#include <krate/dsp/systems/noise_organism.h>` to `dsp/lint_all_headers.cpp`'s Layer 3 block (`:149-175`)** | builds; `lint-layers`/`lint-odr` green; clang-tidy actually covers the new header |
| T4 | Lifecycle: `prepare`/`reset`/`setSeed`/`applyConfiguration`, guard ladder, absolute control grid, silent render | SC-017, SC-016 (on silence), SC-014, SC-003 |
| T5 | `Direct` slot end-to-end: source → resonators → combs (+`snapSmoothers`) → filter → gain | SC-019 (a) for `Direct`, SC-005 (a), SC-013 recorded |
| T6 | Wander lanes + FR-069 rate mapping + `setWanderEnabled` | SC-021, SC-010 (a), SC-002 first arm |
| T7 | Gain chain: level ramp, breathing affine map, gate, duck FSM with change-detection and coalescing | SC-009 (a), SC-018, SC-001 (d) |
| T8 | `FilteredWind`, `MetallicHiss` (organism-computed ratios), `GranularDust` (pool, concurrency clamp, FR-036 gain) | SC-019, SC-020 |
| T9 | Calibration pass: run the `[.calibration]` case, transcribe `kSourceDriveDb`, `kModelTrimDb`, the FR-018 fit and `kMaxCombDelayStepSamples` into the header with the measurement date; the case REQUIREs every `kSourceReferenceDb + kSourceDriveDb[t]` lands inside `[kMinLevelDb, kMaxLevelDb]` and fails loudly with the measured value otherwise | SC-019 (a)'s ±3 dB arm and its level-ownership arm, SC-001 (c), SC-002 (d) |
| T10 | SC-004 baselines pinned from measurement, each with **both** compile-time clauses — the ceiling `kBaselineX * 1.5f <= 106666` *and* the anti-no-op floor `kBaselineX >= 106666 / 50.0` (`atmosphere_engine_perf_test.cpp:34-42`) | SC-004 (a)–(e) |
| T11 | The `[long]` set: SC-001, SC-002 (incl. (b)'s breathing-zeroed control arm and (d)'s comb-excursion arm), SC-005 (b), SC-008, SC-009 (b). Record in `compliance.md`: both SC-002 (b) CV figures, and the realised comb excursion as a percentage of the configured span | all green, with the recorded figures showing the 3× CV clause and the 25 % excursion clause are reachable rather than assumed |
| T12 | SC-013 injected-defect arm (salt collision) proves the fingerprint can fail; SC-015; lints; `compliance.md` with real numbers | every SC row cited with a file:line or a measured value |

---

## S17. Risks and mitigations

| Risk | Why it is real here | Mitigation |
|---|---|---|
| **SC-004 (c) misses the 1 % budget** | The S11 estimate spans 85 000–155 000 ns against 106 666. Two of the four dominant stages are inside shipped components the Non-Goals decline to refactor | T0 measures before anything is built; `snapSmoothers` removes the largest term structurally; on a miss, FR-095's stop-and-surface with the per-stage table, and the three options in S11 put to the user |
| Comb bank silently pinned to the per-sample path | The hoist condition is invisible from the caller — it just gets slow (`timevar_comb_bank.h:728-741`) | SC-004's per-configuration baselines catch it; the T0 probe measures both paths explicitly so the difference is a known number, not a surprise |
| **Denormals in the comb feedback loops and resonator biquads** at low signal levels with 0.9 feedback and 30 s decay | Long-tail feedback structures are the classic denormal trap; the DSP test main enables FTZ/DAZ (`dsp_test_main.cpp`) but a host may not | The bank flushes its own output (`timevar_comb_bank.h:689`); the organism flushes the mix with `detail::flushDenormal` (`db_utils.h:245`); `BrownianDrift` has its own `kDenormalFloor` (`:228`). SC-004's steady-state measurement would show the 10–100× stall if any path escaped |
| **Numerical stability at max Q + max feedback** | Configuration (d): Q saturated at 100, feedback 0.9, decay 30 s, all wander at max rate | SC-005 (a) untagged per-push arm (60 s, finiteness + peak + clamp counter) and (b) 30 min soak. Every stochastic element is bounded by construction (OU mean reversion, Perlin lattice) and the clamp counter makes the FR-074 backstop *observable* rather than assumed |
| **`-ffast-math` folds a finiteness check** on the macOS leg | The whole FR-008 guard set depends on it | Only `detail::isFinite`/`isNaN`/`isInf` (`db_utils.h:99-127`, opaque-bits barrier) are used; SC-015's TU is the only one built `-fno-fast-math`, so the guards are *also* proved in the shipped FP mode by the other three TUs |
| **MSVC-green proves nothing** | GCC/Clang reject what MSVC accepts; three of the four new TUs build under `-ffast-math` on the other legs | `node tools/check-portability.js` before every commit; no narrowing in brace init (designated initialisers for `PrepareConfig`); no SIMD added, so the aligned-load lint is trivially satisfied |
| **Bit-exact golden creep** | It is tempting to pin a 30 s render | SC-013 uses `render_fingerprint.h` with a measured per-comparison tolerance and an injected-defect proof; `tools/lint-float-bit-goldens.js` is a gate |
| **`reset()` regression** — forwarding to `ResonatorBank::reset()` renders silence | `resonator_bank.h:226-232` wipes configuration and disables every resonator | SC-006 (b) asserts the post-`reset` render is non-silent *and* sample-exactly matches a fresh `prepare` + the same configuration calls |
| Salt renumbering in a later phase silently changes every Phase-2 render | New lanes are the obvious future edit | Compile-time salt bases with `static_assert` range checks (S2.4) and an "APPEND ONLY" banner; SC-013's fingerprint fails loudly if a salt moves |
| FR-017/FR-018 tables shipped as guessed placeholders | The easy failure mode of any "measured constant" | SC-019 (a)'s ±3 dB arm and SC-001 (c)'s window make a guessed table fail; the `[.calibration]` case that produces them is checked in |
| **Slot level applied twice** (generator *and* mix stage) | Two plausible readings of FR-017's `requestedDb`; the error is uniform across all 13 types so every level-relative criterion absorbs it | S9.1 names one owner (`levelRamp`) and one constant expression at all three generator push sites; SC-019 (a)'s new level-ownership arm measures an *absolute* dB difference, which is the only assertion a uniform offset cannot hide from |
| **A write lost in the duck FSM** | The Up leg is a real window (25 ms) and "exactly one duck" is satisfied by a *dropped* change as well as a coalesced one | S8.3 re-arms from `Up`; SC-018's lost-write arm asserts the final `getSourceModel`/`getSourceNoiseType` at a randomised offset across the whole duck, not the duck count |
| **The comb slew bound silently freezes the lane it protects** | 0.25 samples/step is ~32× tighter than the fastest legal trajectory, and every criterion as originally planned ran at the default rate or with comb wander at 0 | The bound is measured by the `[.calibration]` case against the SC-009 (b) click bound instead of chosen; SC-002 (d) measures the realised excursion at maximum rate and depth and records it |
| **Dust pool cost under-counted in the CPU model** | S6.1 scans all 24 grain slots per sample regardless of live count; the S11 row assumed mean concurrency | S11's row is restated as 24 iterations/sample and the estimate re-derived; T0's stand-in loop iterates the full pool, so the stop-and-surface decision is made against the real shape |

---

## S18. Open questions for the user

1. **Per-lane `smoothnessSeconds` vs `setWanderRate` precedence.** FR-061/FR-062/FR-067 each take a
   smoothness argument while FR-069 sets all lanes at once. This plan implements **last-writer-wins**
   on shared lane state, with `prepare`/`reset` re-applying `setWanderRate`. Confirm, or make the
   per-lane arguments *offsets* from the organism rate instead.
2. **`getSourceRms` semantics** — S5.6 defines it as the **source-stage** level (pre-chain), which is
   what makes SC-010 (a) satisfiable and what Phase 8's energy sensing wants. FR-015's wording
   ("the slot's smoothed output level") admits the post-chain reading, which SC-010 (a) cannot pass.
   Confirm the source-stage reading.
3. **`snapSmoothers()` on the comb bank, and the FR-063 amendment it forces (S6.3 / S14.4).** It
   relocates delay-change continuity from the bank's 20 ms smoother to an organism-side
   per-control-step slew bound, and it is what keeps the combs at ~30 700 ns instead of ~99 000 ns.
   It is the shipped `ContinuousBody` pattern and the bank's own documented usage, but FR-063's
   stated reason for adding no slew limiter stops holding once the snap is called, so this plan
   **amends** FR-063 rather than merely deviating from it: the bound exists, its size is measured by
   the `[.calibration]` case against the SC-009 (b) click bound (not assumed at 0.25 samples/step,
   which would throttle the lane ~32× at the fastest legal rate), and SC-002 (d) measures and
   records what excursion the lane actually realises. Confirm the amendment, or direct that the
   combs keep the bank's own smoothing and the CPU cost be paid instead.
4. **Pre-authorisation for the S11 option A** (a `StochasticFilter` hoisted-path amendment) *if and
   only if* T0/SC-004 (c) misses the budget — or confirm that a miss should stop for a fresh decision
   with the per-stage numbers in hand (FR-095's default).
5. **`kSourceReferenceDb` and `kModelTrimDb` (S9.1).** Resolving FR-017's double-application defect
   introduced one organism constant (`kSourceReferenceDb`, the generator-side level every source
   runs at) and one new measured table (`kModelTrimDb[4]`, the per-model trim `kSourceDriveDb`
   structurally cannot reach because `GranularDust` never uses the generator's amplitude). Both are
   compile-time, monotone and control-loop-free, i.e. they carry FR-017's own stated properties, but
   `kModelTrimDb` is a table the spec does not name. Confirm, or direct that the three composed
   models be held only to SC-019's "> −60 dBFS" clause with no level window.

---

## S19. Review notes

No issue from the review was rejected. Every blocker, major and minor is applied. This section
records only the **choices made where the review offered alternatives**, so a later reader does not
re-open a settled question.

1. **Slot-level owner (blocker, S9.1).** The review's recommended option is taken — the mix-stage
   `levelRamp` owns the user's slot level and the generator carries a per-type constant — but with
   one refinement, because taking it literally would have contradicted FR-017's normative sentence
   ("every `setNoiseLevel(type, requestedDb)` call the organism issues actually passes
   `requestedDb + kSourceDriveDb[type]`"). Binding `requestedDb` to the new organism constant
   `kSourceReferenceDb = NoiseGenerator::kDefaultLevelDb` satisfies FR-017 as written *and* leaves a
   single owner for the level. No threshold moved; the Velvet headroom argument was re-derived
   against `kMaxLevelDb = +12` from scratch (≈ 22 dB of drive inside a 32 dB allowance), and the
   calibration case now *fails* rather than silently clamping if a measured entry does not fit.
2. **Duck FSM (blocker, S8.3).** The review's first option is taken (`Up` re-arms, costing a second
   duck for a post-swap change). The alternative — one duck, applying the pending swap when the Up
   ramp lands — would apply a model change at full gain, which is exactly the click FR-013 exists to
   prevent.
3. **SC-021 (major).** The review's first option is taken (measure off the applied state via
   `getResonatorCurrentQ`). The alternative test-visible deterministic path was rejected as a public
   API added for a test's benefit. The fixture gained an anchor/decay choice the review did not
   name: at the FR-016 default decay the top anchors already saturate `rt60ToQ` at
   `kMaxResonatorQ = 100`, so the downward factor would be clamped away and the criterion would fail
   on a correct implementation; 70 Hz at 1.0 s decay gives Q ≈ 31.8 and full observability.
4. **SC-019 model coverage (major).** Both halves of the review's suggestion are taken: the matrix
   is reduced to 12 `Direct` type cells + 3 model cells, **and** `kModelTrimDb[4]` is added so the
   ±3 dB window still binds all four models. Dropping the composed models to a bare "> −60 dBFS"
   was the weaker alternative and would have relaxed the guard that keeps FR-017/FR-018 honest.
5. **FR-068 and breathing (major, S7.3).** The review's option (b) is taken: SC-002 (b)'s control arm
   zeroes breathing in the fixture. Option (a) — making `setWanderEnabled(false)` zero the FR-070
   lane — was rejected because it changes shipped behaviour to suit a test, and FR-068 deliberately
   enumerates FR-061…FR-067 only. The measured CV figures are recorded at T11 either way.
6. **`LinearRamp` (major, S1.3).** The review's option (a) is taken: the shipped
   `Krate::DSP::LinearRamp` (`primitives/smoother.h:305`) is reused rather than renamed-and-copied.
   Its law is constant-*duration* despite a class doc that says "constant rate"
   (`calculateLinearIncrement`, `smoother.h:100-108`), so it delivers SC-009 (a)/SC-018's
   "50 ms regardless of step size" directly, and reuse brings the NaN/Inf and denormal guards the
   nested copy would have dropped. Reuse-first is this project's stated posture; a
   `FixedDurationRamp` rename would have been a second implementation of a shipped primitive.
7. **Comb slew bound (major, S6.3).** Both halves of the review's suggestion are taken: the bound is
   derived from a *measured* click threshold rather than the assumed 0.25 samples, **and** SC-002
   gains arm (d) so the realised excursion is recorded. `Slot::appliedCombDelayMs[]` was added, and
   it doubles as the new `getCombCurrentDelayMs` read surface that arm (d) needs.
8. **Dust pool (minor, S6.1 / S11).** The steal policy is made explicit (prefer a free slot, else
   steal the largest-phase grain) and the "nothing is truncated" sentence corrected. The compaction
   alternative (`activeCount_`, swap-with-last) is recorded in S11 as the fix to reach for **if T0
   shows the pool scan is the deciding CPU term**, rather than adopted pre-emptively — S6.1's fixed
   scan is simpler, and the phase's first task is a measurement.
