# Tasks: Seraphis Phase 7 — Voice & Engine

**Spec:** `specs/seraphis-phase7-voice-engine/spec.md`
**Plan:** `specs/seraphis-phase7-voice-engine/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part A → Phase 7 (lines 288–315)
**Ships:** three new Layer 3 headers + one test-only helper header + five new test TUs.
**Amends:** nothing outside this phase (N-9). No shipped Phase 1–6 header is edited. No plugin code.

---

## How to read this document

- Tasks are **T001…T016**, grouped into **ordered GROUPS**. A group starts only when the previous group
  is green (builds warning-free, its named cases pass).
- Tasks marked **[P]** are parallel-safe: they touch **fully disjoint NEW files only**. Every task that
  edits a shared file — any of the three headers, any TU another task also touches, or a CMake list — is
  in a **group of its own**.
- **Every task follows the canonical order:** write the failing test first → implement → fix all
  compiler warnings → all named tests pass. A task is not done until `dsp_systems_tests` builds with
  **zero warnings** and the listed `TEST_CASE`s pass.
- Each task is self-contained. The executor is assumed to have **no other context**: exact files, exact
  `TEST_CASE` names, exact assertions with numbers, then the implementation intent, then the verifying
  target.
- **No commits.** Commits happen outside this workflow.

**Build / run commands (Windows — the full CMake path is mandatory):**

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*" 2>&1 | tail -5
```

Run one case: `dsp_systems_tests.exe "SeraphisVoice_ShipsDocumentedDefaults"` (positional arg, **not**
`-c`, which filters sections). Perf lane: `dsp_systems_tests.exe "[.perf]"`. Nightly grids:
`dsp_systems_tests.exe "[.slow]"`.

---

## Files this phase creates

| Path | Kind | Layer | Created in |
|---|---|---|---|
| `dsp/include/krate/dsp/systems/seraphis_voice.h` | shipped header | 3 | T001 |
| `dsp/include/krate/dsp/systems/seraphis_engine.h` | shipped header | 3 | T001 |
| `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` | shipped header | 3 | T001 |
| `tests/test_helpers/seraphis_chain.h` | test-only header | n/a (may include Layer 4) | T009 |
| `dsp/tests/unit/systems/seraphis_voice_test.cpp` | test TU | — | T001 |
| `dsp/tests/unit/systems/seraphis_engine_test.cpp` | test TU | — | T001 |
| `dsp/tests/unit/systems/seraphis_macro_test.cpp` | test TU | — | T001 |
| `dsp/tests/unit/systems/seraphis_nonfinite_test.cpp` | test TU (IEEE FP) | — | T001 |
| `dsp/tests/unit/systems/seraphis_perf_test.cpp` | test TU (`[.perf]`) | — | T001 |

## Files this phase edits

`dsp/CMakeLists.txt` (header list), `dsp/lint_all_headers.cpp` (Layer 3 include block),
`dsp/tests/CMakeLists.txt` (`dsp_systems_tests` source list + the `-fno-fast-math` block).
**`tests/test_helpers/CMakeLists.txt` is NOT edited** — it declares `add_library(test_helpers INTERFACE)`
with a `target_include_directories` and enumerates no headers (verified this session), so dropping
`seraphis_chain.h` into the directory is the whole registration (plan §5, V-6).

---

## Deviation from the requested task ordering, stated so it is a decision and not an omission

The user's task format asks for CMake registration in the **last** group. It is done in **T001** instead,
as a single task covering all three sites. `dsp_systems_tests`' source list is enumerated, not globbed
(`dsp/tests/CMakeLists.txt:337-351` is the Phase 2–5 block) — an unregistered TU silently drops and
**no failing test in this phase can be written, built or run**. The final group (T016) carries the
*verification* of all three registration sites plus the full-suite / portability / lint sweep.

---

## Constants pinned by the plan (copy verbatim; do not re-derive)

`SeraphisVoice` (all class-scoped, plan §2.1):

```
kControlChunkSamples   = 64          kMaxBlockSamples      = 2048
kTailSilenceThreshold  = 1.0e-5f     kLevelReleaseMs       = 100.0f
kSilenceRampMs         = 1.0f        kQuiescentChunksToRetire = 4
kMinVoiceWidthPct      = 50.0f       kMaxVoiceWidthPct     = 150.0f
kSpatialSmoothMs       = 20.0f       kSqrt2                = 1.41421356f
kStageCurve            = EnvCurve::Exponential   (core/env_curve.h:24)
kCloudSalt 0x0100  kMorphSalt 0x0200  kBodySalt 0x0300  kAtmosSalt 0x0400  kOrbitSalt 0x0500
```

`SeraphisEngine` (plan §3.1):

```
kMaxVoices 16   kControlChunkSamples 64   kMaxBlockSamples 2048   kVoiceSaltBase 0x9000
kSumGainSmoothMs 20.0f   kAmnestyLevelThreshold 0.0316f   kOutputSaturation 0.15f
kOutputDriveDb 0.0f      kResetsPerControlChunk 1         kFreezeRetriesPerChunk 1
kBloomPartialCap 32      (duplicated from aether_reverb.h:1442 kMaxBloomResonators, with citation)
shipped default polyphony = 8
```

Perf (plan §6.2.1): `kBlockBudgetNs = (512/48000)·1e9 = 10 666 666.7`,
`kReferenceNs = kBlockBudgetNs · 0.25 = 2 666 666.7`, **`kRegressionFactor = 1.15`**,
`kBaselineHeadroom = 1.05` (recording convention only).

---

# GROUP 1 — Scaffold, ODR sweep, build wiring

## T001 — ODR sweep, three header skeletons, five empty TUs, CMake registration

**Files created**

- `dsp/include/krate/dsp/systems/seraphis_voice.h`
- `dsp/include/krate/dsp/systems/seraphis_engine.h`
- `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`
- `dsp/tests/unit/systems/seraphis_voice_test.cpp`
- `dsp/tests/unit/systems/seraphis_engine_test.cpp`
- `dsp/tests/unit/systems/seraphis_macro_test.cpp`
- `dsp/tests/unit/systems/seraphis_nonfinite_test.cpp`
- `dsp/tests/unit/systems/seraphis_perf_test.cpp`

**Files edited**

- `dsp/CMakeLists.txt` — add the three headers to `KRATE_DSP_SYSTEMS_HEADERS`, next to
  `include/krate/dsp/systems/poly_synth_engine.h` (line 164 shows the shape).
- `dsp/lint_all_headers.cpp` — add three `#include <krate/dsp/systems/seraphis_*.h>` lines in the
  Layer 3 block (lines 167–168 show the shape). **Missing either site is a silent CI gap (FR-080).**
- `dsp/tests/CMakeLists.txt` — add the five TUs to the `dsp_systems_tests` source list, immediately
  after the Phase 5 block that ends at line 351 (`unit/systems/atmosphere_engine_nonfinite_test.cpp`).
  **Additionally** add **only** `unit/systems/seraphis_nonfinite_test.cpp` to the
  `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (lines ~690–728), with a
  comment in the shape of the Phase 4/5/6 comments already there stating that the other four Phase 7
  TUs must **not** be listed — the perf TU especially, because `-fno-fast-math` moves the figures its
  baselines are pinned to.

**Step 0 — ODR sweep (must produce zero matches before any file is written)**

```bash
grep -rEn "(class|struct|enum class|using) (SeraphisVoice|SeraphisEngine|SeraphisMacroMatrix|SeraphisMacro|SeraphisVoiceConfig|SeraphisEngineConfig|SeraphisMacroValues|SeraphisAetherTargets|SeraphisMacroTargetOwner|SeraphisMacroTarget|SeraphisMacroRow|EnvelopeMode|BloomEvents)\b" dsp/ plugins/ tests/
```

Every one of those must be **0 matches**. `EnvelopeMode` and `BloomEvents` are declared **class-scoped**
(`SeraphisVoice::EnvelopeMode`, `SeraphisEngine::BloomEvents`) so they never enter namespace scope; the
`HarmonicCloud` precedent for class-scoping a collision-prone name is `harmonic_cloud.h:132-138`. Every
new constant in all three headers is likewise class-scoped.

**Failing test first**

Each of the five TUs gets one placeholder case so the build proves registration:

```cpp
TEST_CASE("SeraphisVoice_HeaderCompiles")        { SUCCEED(); }   // seraphis_voice_test.cpp
TEST_CASE("SeraphisEngine_HeaderCompiles")       { SUCCEED(); }   // seraphis_engine_test.cpp
TEST_CASE("SeraphisMacroMatrix_HeaderCompiles")  { SUCCEED(); }   // seraphis_macro_test.cpp
TEST_CASE("SeraphisEngine_NonFiniteTuCompiles")  { SUCCEED(); }   // seraphis_nonfinite_test.cpp
TEST_CASE("SeraphisEngine_PerfTuCompiles", "[.perf]") { SUCCEED(); } // seraphis_perf_test.cpp
```

Each TU includes its subject header. These placeholders are **deleted** by the task that lands the first
real case in that TU.

**Implement**

Three header skeletons carrying the **complete public API** of plan §2.1, §3.1 and §4.1 with empty or
trivially-returning bodies. Nothing else. Specifically:

1. `seraphis_voice.h` — `struct SeraphisVoiceConfig` (six fields, default member initialisers:
   `captureSeconds = 4.0f`, `blurEnabled = true`, `freezeEnabled = true`, `blurFftSize = 1024`,
   `freezeFftSize = 2048`, `maxBlockSamples = 2048`); `class SeraphisVoice` with the constants above,
   `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }`, deleted copy **and** move
   members (`ContinuousBody` user-declares a deleted copy ctor and no move members —
   `continuous_body.h:647-648` — so a `= default`ed move here would be *defined as deleted* while
   reading as movable; nothing in Phase 7 moves a voice), and every method listed in plan §2.1.
2. `seraphis_engine.h` — `struct SeraphisEngineConfig { SeraphisVoiceConfig voice{}; std::size_t
   polyphony = 8; std::uint32_t seed = 1u; }`; `class SeraphisEngine` with the constants above,
   `struct BloomEvents { std::uint32_t noteOnMask = 0u; std::uint32_t noteOffMask = 0u; }`, every method
   in plan §3.1, **and `friend class SeraphisMacroMatrix;`** (plan §4.4 / V-4 — declared now so T013
   never has to edit this header).
3. `seraphis_macro_matrix.h` — the four enums and three PODs of plan §4.1 plus `class
   SeraphisMacroMatrix` with `setMacro`/`getMacro`, `void apply(SeraphisEngine&) const noexcept` and
   `[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept`.

**Include direction (explicit, never transitive):** `seraphis_voice.h` includes its Layer 0/1/2 headers
**by name** — in particular `core/env_curve.h` (declares `EnvCurve`, `:24`) and
`primitives/envelope_utils.h` (declares `RetriggerMode`, `:64`) are **two different headers** and both
are included; `EnvCurve` currently reaches this TU only through `multi_stage_envelope.h`, which is
exactly the transitive dependency that breaks on a sibling refactor. Plus the Layer 3 peers it owns:
`harmonic_cloud.h`, `spectral_morph_engine.h`, `continuous_body.h`, `atmosphere_engine.h`.
`seraphis_engine.h` includes `seraphis_voice.h` + `systems/voice_allocator.h` +
`processors/tape_saturator.h` + `processors/true_peak_limiter.h`.
`seraphis_macro_matrix.h` includes `seraphis_engine.h` + `core/modulation_curves.h`.
**No Layer 4 header anywhere in the three** (FR-001, FR-056, FR-070).

**Verify**

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*" 2>&1 | tail -5
node tools/lint-layers.js
```

All five placeholders discovered and passing; `lint-layers.js` clean; **zero compiler warnings**.

---

# GROUP 2 — `SeraphisVoice`: prepare, shipped defaults, config, ownership

## T002 — `prepare` / `applySeeds` / `applyStage` / FR-019 + FR-019a defaults / reset semantics

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_voice.h`,
`dsp/tests/unit/systems/seraphis_voice_test.cpp`.

**Failing test first — `dsp/tests/unit/systems/seraphis_voice_test.cpp`**

All four cases construct the voice as a plain local (`SeraphisVoice` is ~47 KB — safe on the stack; only
`SeraphisEngine` must be heap-allocated, see T005).

`TEST_CASE("SeraphisVoice_ShipsDocumentedDefaults")` — after
`v.prepare(48000.0, SeraphisVoiceConfig{})`, assert the **whole** FR-019 table through the components'
own getters reached via the const accessors (SC-010 clause 1). Exact values:

| Read-back | Required |
|---|---|
| `v.cloud().getRichness()` | `Approx(0.60f)` |
| `v.cloud().getInharmonicity()` | `Approx(0.030f)` |
| `v.cloud().getSpectralTiltDb()` | `Approx(0.0f).margin(1e-6f)` |
| `v.cloud().getMutation()` | `Approx(0.25f)` |
| `v.cloud().getSpectralGravity()` | `Approx(0.20f)` |
| `v.cloud().getDriftDepthCents()` | `Approx(0.0f).margin(1e-6f)` |
| `v.cloud().getStereoSpread()` | `Approx(0.35f)` |
| `v.cloud().getAttackTimeSec()` | `Approx(0.05f)` |
| `v.cloud().getDecayTimeSec()` | `Approx(0.5f)` |
| `v.morph().entropy().getEntropy()` | `Approx(0.20f)` |
| `v.morph().getBloom()` | `Approx(0.0f).margin(1e-6f)` |
| `v.morph().getTravelRate()` | `Approx(1.0f/600.0f)` |
| `v.getTravelMode()` | `SpectralMorphEngine::TravelMode::External` |
| `v.morph().getStateCount()` | `== 2` |
| `v.atmos().getLevel()` | `Approx(0.5f)` |
| `v.atmos().getBlur()` | `Approx(0.0f).margin(1e-6f)` |
| `v.atmos().getDensity()` | `Approx(4.0f)` |
| `v.atmos().getGrainSeconds()` | `Approx(4.0f)` |
| `v.atmos().getDriftDepth()` | `Approx(0.3f)` |
| `v.atmos().getPanSpread()` | `Approx(0.7f)` |
| `v.atmos().getDecorrelation()` | `Approx(0.5f)` |
| `v.atmos().getFreezeMix()` | `Approx(0.0f).margin(1e-6f)` |
| `v.isFreezeCaptured()` | `== false` |
| `v.orbit().getDepth()` | `Approx(0.35f)` — the zero-travel fix |
| `v.orbit().getRate()` | `Approx(0.1f)` |
| `v.orbit().getCoupling()` | `Approx(0.0f).margin(1e-6f)` |
| `v.orbit().getGrowth()` | `Approx(0.0f).margin(1e-6f)` |
| `v.getEnvelopeStageTimeMs(0)` / `(1)` / `(2)` / `(3)` | `Approx(2000.0f)` / `Approx(4000.0f)` / `0.0f` / `0.0f` |
| `v.getEnvelopeReleaseMs()` | `Approx(8000.0f)` |
| `v.getEnvelopeMode()` | `SeraphisVoice::EnvelopeMode::Standard` |
| `v.getVoiceWidthBasePercent()` | `Approx(100.0f)` |
| `v.getRejectedConfigureTimeCallCount()` | `== 0u` |

The four getter-less `ContinuousBody` rows (`setDamping = 0.25`, `setResonance = 0.7`, `setMix = 1.0`,
`setCloudMix = 0.25`) are covered by the render differential in **T004**, not here.

`TEST_CASE("SeraphisVoice_ConfigIsClampedNeverRejected")` — FR-004. Four sub-sections, each preparing a
fresh voice and requiring it to be usable (never rejected):
- `captureSeconds = -5.0f` → prepared; `v.atmos().getCaptureCapacitySamples()` ≥ `48000` (i.e. the
  component's own 1 s floor, rounded up to a power of two).
- `captureSeconds = 1.0e9f` → prepared; `getCaptureCapacitySamples()` ≤ `2097152` (2^21 ≥ 30 s @ 48 kHz).
- `maxBlockSamples = 0` → prepared; `noteOn(220.0f, 1.0f)` then `processStereoBlock(l, r, 128)` produces
  at least one sample with `|x| > 0.0f`.
- `maxBlockSamples = 1000000` → same, and `getCaptureCapacitySamples()` unchanged from the default case.

`TEST_CASE("SeraphisVoice_ShipsFourSecondCapture")` — FR-014 / D6. `prepare(48000.0,
SeraphisVoiceConfig{})` → `REQUIRE(v.atmos().getCaptureCapacitySamples() == 262144u)` (4 s → the
`RollingCaptureBuffer` power-of-two round-up is 262 144 = 5.46 s; the assertion is on **capacity
samples**, never on the requested seconds).

`TEST_CASE("SeraphisVoice_LayerAndOwnership")` — FR-001/FR-002 positive half plus the size guard:
- the five const sub-component accessors exist and return **pairwise distinct addresses**:
  `&v.cloud()`, `&v.morph()`, `&v.body()`, `&v.atmos()`, `&v.orbit()`;
- `static_assert(sizeof(SeraphisVoice) <= SeraphisVoice::kVoiceSizeBound)` compiles;
- `static_assert(sizeof(SeraphisVoice) < 3 * 1024 * 1024)` compiles (FR-013 heap-free-giant guard).

**Implement** — plan §2.3, §2.4, and the `applyStage` helper of §2.3 step 7.

`prepare(double sampleRate, const SeraphisVoiceConfig& cfg)`, in exactly this order:

1. `sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;` (the `atmosphere_engine.h:406-408` idiom).
2. Clamp every `cfg` field; `maxBlockSamples` → `[1, kMaxBlockSamples]`. Never reject.
3. `cloud_.prepare(sampleRate_)`; `morph_.prepare(sampleRate_)`; `body_.prepare(sampleRate_)`;
   `atmos_.prepare(sampleRate_, AtmosphereEngine::PrepareConfig{ .captureSeconds = …, .blurEnabled = …,
   .freezeEnabled = …, .blurFftSize = …, .freezeFftSize = …, .maxBlockSamples = … })` — **designated
   initialisers only**, no narrowing in brace init; `mse_.prepare(static_cast<float>(sampleRate_))`
   (that one takes `float`); `growth_.prepare(sampleRate_)`; `orbit_.prepare(sampleRate_)`;
   `ms_.prepare(static_cast<float>(sampleRate_), kControlChunkSamples)`.
4. `applySeeds()` — plan §2.4: `cloud_/morph_/body_/atmos_/orbit_.setSeed(deriveStreamSeed(seed_,
   k*Salt))` (`core/random.h:102-111`). `GrowthEnvelope::setSeed` is a documented no-op
   (`growth_envelope.h:140`) and is **not** called. Seeds must be applied **before the first note** —
   `ContinuousBody::setSeed` is *"configure-time only, and deliberately NOT retro-deterministic"*
   (`continuous_body.h:1117-1124`).
5. **FR-019a** — the only place `setState`/`setStateCount` are ever called:
   `morph_.setState(0, makeFactoryState(SpectralStateId::SineStack))`,
   `morph_.setState(1, makeFactoryState(SpectralStateId::Glass))`, `morph_.setStateCount(2)`,
   `morph_.setTravelMode(SpectralMorphEngine::TravelMode::External)` (**fully qualified** — the enum is
   nested at `spectral_morph_engine.h:139`), `morph_.setTargetPosition(0.0f)`.
6. **FR-019** — write the complete table verbatim, including every *(unchanged)* row, so the table is the
   code. The nine rows that differ from the component default:
   `cloud_.setRichness(0.60f)`, `setInharmonicity(0.030f)`, `setMutation(0.25f)`,
   `setSpectralGravity(0.20f)`, `setStereoSpread(0.35f)`; `morph_.setEntropy(0.20f)`;
   `body_.setDamping(0.25f)`; `atmos_.setLevel(0.5f)`; `orbit_.setDepth(0.35f)`.
   Also `setVoiceWidthBasePercent(100.0f)`.
7. Envelope: `mse_.setNumStages(4)`, `setSustainPoint(2)`, then **through `applyStage`**:
   `applyStage(0, 1.0f, 2000.0f)`, `applyStage(1, 0.7f, 4000.0f)`, `applyStage(2, 0.7f, 0.0f)`,
   `applyStage(3, 0.0f, 0.0f)`; `mse_.setReleaseTime(8000.0f)`;
   `mse_.setRetriggerMode(RetriggerMode::Legato)` — **explicit**, the component default is `Hard`
   (`multi_stage_envelope.h:463`); `growth_.setDuration(10.0f)`.
   `applyStage` opens with the FR-059 idempotence guard
   `if (stageLevel_[st] == level && stageTimeMs_[st] == ms) return;`, writes the two shadow arrays
   (which are the **single source of truth**), then forwards to `mse_.setStage(st, level, ms,
   kStageCurve)` in Standard mode or when `st >= mse_.getSustainPoint()`, else
   `mse_.setStage(st, level, 0.0f, kStageCurve)`. **The shadows must be populated through this helper at
   prepare time** — a Standard → Growth → Standard round trip restores from them (T004), and a direct
   `mse_.setStage` here would leave them `{0, 0}` and silently install a 0 ms attack.
8. Derived constants: `levelReleaseCoeff_ = std::exp(-static_cast<float>(kControlChunkSamples) /
   (0.001f * kLevelReleaseMs * static_cast<float>(sampleRate_)))` — **do NOT use
   `calculateOnePolCoefficient`**, which treats its argument as time-to-99 % = 5τ (`smoother.h:86-88`)
   and would give τ = 20 ms instead of 100 ms, breaking FR-032's 1.15 s derivation (D2);
   `silenceRampSamples_ = max(1, round(0.001f * kSilenceRampMs * sr))`;
   `gainLSm_.configure(kSpatialSmoothMs, static_cast<float>(sampleRate_))` and the same for `gainRSm_`
   (the cast is required — the parameter is `float`, and MSVC raises C4244 without it).
9. `prepared_ = true; reset();`

`reset()` / `resetForSteal()` restore **run state only**: carry FIFO cleared
(`carryAvail_ = carryRead_ = 0`, `carryIsLifeOnly_ = true`), `level_ = 0`,
**`quiescentChunks_ = kQuiescentChunksToRetire`** (V-15 — *not* 0), `hasSounded_ = false`,
`renderedSinceNoteOn_ = false`, `lastOutL_ = lastOutR_ = 0`, and each sub-component's own `reset()`.
**Parameters are not restored** — that is the sub-components' own `reset()` contract
(`continuous_body.h:757`). The single difference: `reset()` clears the D3 fade tail
(`fadeTailL_ = fadeTailR_ = 0.0f; fadeRemaining_ = 0;`), `resetForSteal()` preserves it.

State layout is plan §2.2 verbatim: eight `std::array<float, kControlChunkSamples>` scratch buffers
(2 KB/voice total — plan §10 V-1 records the deliberate 64-not-2048 sizing), no `std::vector`, no
`std::function`, no smart pointer, no `std::string` anywhere.

**Record `kVoiceSizeBound` here.** Print `sizeof(SeraphisVoice)` once (a `WARN` in the test is fine),
set `static constexpr std::size_t kVoiceSizeBound = <ceil(measured × 1.05)>` in the header with the
eight declared members enumerated in the comment above it, and land the `static_assert`. Any of the six
forbidden members (`StereoField`, `VoiceModRouter`, `ModulationEngine`, `PolySynthEngine`, `SynthVoice`,
`AetherReverb`) would blow it.

**Verify:** `dsp_systems_tests.exe "SeraphisVoice_ShipsDocumentedDefaults"`,
`"SeraphisVoice_ConfigIsClampedNeverRejected"`, `"SeraphisVoice_ShipsFourSecondCapture"`,
`"SeraphisVoice_LayerAndOwnership"` — all pass, zero warnings.

---

# GROUP 3 — `SeraphisVoice`: render chain, carry FIFO, spatial stage, level detector, notes

## T003 — `renderOneChunk` (D1), `processStereoBlock`, `advanceLifeOnly`, spatial math, level detector, `noteOn`/`noteOff`

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_voice.h`,
`dsp/tests/unit/systems/seraphis_voice_test.cpp`.

**Failing test first — `seraphis_voice_test.cpp`**

`TEST_CASE("SeraphisVoice_ProcessGuards")` — FR-006, Edge Cases 1–3:
- before `prepare`: `processStereoBlock(l, r, 64)` zero-fills both buffers (every sample `== 0.0f`).
- after prepare and `noteOn`, `processStereoBlock(nullptr, r, 64)` and `processStereoBlock(l, nullptr,
  64)` write nothing — pre-fill both buffers with the sentinel `-7.0f` and require every entry still
  `== -7.0f`.
- `processStereoBlock(l, r, 0)` consumes **no** control step: `getSpatialAzimuth()` is bit-identical
  before and after, and so is `getCurrentLevel()`.

`TEST_CASE("SeraphisVoice_ControlGridIsPartitionInvariant")` — FR-007 / D1. Render **48 000 samples**
(1 s @ 48 kHz) after `setSeed(4242)`, `prepare`, `noteOn(220.0f, 1.0f)`, in partitions
`{1, 7, 64, 65, 512, 4096}` from six freshly prepared, identically seeded voices. Reference = the 512
partition. `REQUIRE` the **max absolute per-sample difference ≤ 1e-6f** on both channels for every other
partition. (Tighter than SC-014's 1e-5 because there is no reverb in the chain here; this is the gate
that catches D1's defect before the chain is assembled — R1.)

`TEST_CASE("SeraphisVoice_SpatialStageMath")` — FR-025/FR-026:
- `v.setSpatialDepth(0.0f)`, render 4096 samples: `getSpatialAzimuth() == Approx(0.0f).margin(1e-6f)`
  and `getSpatialWidthPercent() == Approx(100.0f).margin(1e-4f)`.
- unity-at-centre: in the TU, call `equalPowerGains(0.5f, gL, gR)` (`core/crossfade_utils.h:50-53`),
  multiply both by `SeraphisVoice::kSqrt2`, and `REQUIRE(gL == Approx(1.0f).margin(1e-6f))` and the same
  for `gR`. Also `REQUIRE(gL*gL + gR*gR == Approx(2.0f).margin(1e-5f))` at `panNorm ∈ {0.0f, 0.25f,
  0.5f, 0.75f, 1.0f}` — the law is constant-power at every position.
- M/S transparency at 100 %: drive a standalone `MidSideProcessor` (prepared at 48 kHz,
  `setWidth(100.0f)`) with 4096 samples of decorrelated noise and require
  `max|out − in| ≤ 1e-6f` per sample on both channels. This is FR-026's **measurable** bound, not a
  bit-transparency claim (`midside_processor.h:196-207` is the algebraic identity, not bit-exact).

`TEST_CASE("SeraphisVoice_AdvanceLifeOnlyMatchesRender")` — FR-027 / plan §2.8. Two identically seeded,
identically prepared voices; voice A is advanced 4096 samples exclusively through `advanceLifeOnly(n)`,
voice B exclusively through `processStereoBlock(l, r, n)` (both with **no** `noteOn`, so B renders
silence). For each `n ∈ {1, 7, 64, 65, 512}` (a fresh pair per `n`), require
`A.getSpatialAzimuth() == Approx(B.getSpatialAzimuth()).margin(1e-6f)` and the same for
`getSpatialWidthPercent()`. **Every `n`, not only multiples of 64** — both paths consume the same carry
clock (V-7).

`TEST_CASE("SeraphisVoice_LevelDetectorAndRetirement")` — FR-032/FR-033 (this is what pins D2's
coefficient):
- `noteOn(220.0f, 1.0f)`, render until `getCurrentLevel() >= 0.05f`; record `L0 = getCurrentLevel()`.
- then advance **only** through `advanceLifeOnly(64)` (chunk peak 0, so the release is isolated from the
  audio), counting samples until `getCurrentLevel() < kTailSilenceThreshold`.
- expected seconds `= 0.001f * kLevelReleaseMs * std::log(L0 / kTailSilenceThreshold)`;
  `REQUIRE(measuredSeconds == Approx(expectedSeconds).epsilon(0.05))` — **±5 %**. At `L0 ≈ 1.0` that is
  the spec's 1.15 s.
- after `mse_.gate(false)` has expired and the cloud is quiescent, `isFinished()` becomes true no later
  than `kQuiescentChunksToRetire` (= 4) further chunks (85.3 µs) after the crossing.
- **Seeding clause (V-15):** on a freshly prepared, never-rendered voice, `REQUIRE(v.isFinished())` —
  `quiescentChunks_` is seeded at `kQuiescentChunksToRetire`, so a never-rendered slot is finished from
  its first block. Repeat after `reset()` and after `resetForSteal()`.

`TEST_CASE("SeraphisVoice_NoteLifecycle")` — FR-023/FR-024:
- `noteOn(220.0f, 1.0f)`, render 1 s → RMS `> 1e-3f`.
- `noteOff()`, render a further 2 s → still non-silent (RMS `> 1e-5f`) — the body and atmosphere are the
  tail (RA-2).
- velocity clamp: a voice given `noteOn(220.0f, 2.0f)` and a voice given `noteOn(220.0f, 1.0f)` produce
  fingerprints satisfying `compareFingerprints(a, b).withinTolerance()`.

`TEST_CASE("SeraphisVoice_HasNoLatencyAccessor")` — FR-015. After `noteOn`, the very first
`processStereoBlock(l, r, 64)` contains at least one sample with `|x| > 0.0f`. (There is no
`getLatencySamples()` on `SeraphisVoice`; the absence is a decision, recorded in the header comment.)

`TEST_CASE("SeraphisVoice_SeedingIsDeterministic")` — FR-016/FR-017/FR-018. Two voices with
`setSeed(12345u)` before `prepare`, same call sequence, 2 s render →
`compareFingerprints(a, b).withinTolerance()`. Two voices with seeds `12345u` and `999u` →
`!compareFingerprints(a, b).withinTolerance()`. Plus a compile-time
`static_assert` that the five salts are pairwise distinct (already in the header from T001/T002).
Seed `0u` is legal — `deriveStreamSeed` substitutes `0x2545F491u` when the hash lands on 0
(`random.h:110`) — assert a seed-0 voice renders non-silent (Edge Case 19).

`TEST_CASE("SeraphisVoice_PrepareAndResetAreIdempotent")` — FR-003/FR-005:
- a second `prepare` while the voice is sounding → the next 512 samples are all `0.0f` and
  `isFinished()` is true.
- `reset()` on a sounding voice, then 1 s of render with the same note script, compared against a
  freshly prepared voice with the same seed and script → `compareFingerprints(...).withinTolerance()`.

`TEST_CASE("SeraphisVoice_VoicesDriftIndependently")` — SC-006(b), **always-on form**. Four standalone
voices seeded with `deriveStreamSeed(1u, SeraphisEngine::kVoiceSaltBase + v)` for `v = 0..3` (the exact
engine expression), same note, **5 s** each. Pairwise Pearson `|ρ| ≤ 0.5` over the six pairs. Control:
two voices with the **same** seed give `ρ > 0.999`. Add a `[.slow]`-tagged sibling
`TEST_CASE("SeraphisVoice_VoicesDriftIndependently_Full", "[.slow]")` doing all 16 voices over 30 s.

**Implement** — plan §2.5 through §2.9 (excluding the envelope mode switch, which is T004).

`processStereoBlock` is **the carry-FIFO serve loop of plan §1 D1**, verbatim in structure:

```cpp
if (outL == nullptr || outR == nullptr) return;
if (n == 0) return;
if (!prepared_) { std::fill_n(outL, n, 0.0f); std::fill_n(outR, n, 0.0f); return; }
std::size_t done = 0;
while (done < n) {
    if (carryAvail_ == 0) { renderOneChunk(); }   // ALWAYS exactly kControlChunkSamples
    const std::size_t take = std::min(n - done, carryAvail_);
    std::copy_n(carryL_.data() + carryRead_, take, outL + done);
    std::copy_n(carryR_.data() + carryRead_, take, outR + done);
    lastOutL_ = outL[done + take - 1];            // captured at SERVE time (D3), never at render time
    lastOutR_ = outR[done + take - 1];
    carryRead_ += take; carryAvail_ -= take; done += take;
}
```

**The voice never renders a partial chunk.** `HarmonicCloud`, `SpectralMorphEngine` and
`EntropyProcessor` each take **one** control step per call regardless of `n`
(`harmonic_cloud.h:908-912`, `:1677`, `:1690`; `spectral_morph_engine.h:405-412`;
`entropy_processor.h:269-280`), so passing sub-chunks down would give a 36+28 split two control steps
where an unsplit 64 gives one — orders of magnitude above the 1e-6 bound above.

`renderOneChunk()` — always exactly 64 samples, steps 1:1 onto FR-010:

0. `renderedSinceNoteOn_ = true;` (D4 rule 2, **before** any work).
1. `morph_.updateChunk(n)`; then unconditionally
   `cloud_.setSpectralTarget(morph_.getOutputRatios(), morph_.getOutputAmplitudes(),
   morph_.getOutputCount())` (FR-011/FR-012 — the whole-array skip at `harmonic_cloud.h:776-786` makes an
   unchanged target cheap; the voice does **not** duplicate that check).
2. `cloud_.processStereoBlock(cloudL_.data(), cloudR_.data(), n)`.
3. Excitation gate, **in place** on `cloudL_/cloudR_`: Standard → `g[s] = velocity_ * mse_.process()`
   per sample; Growth → `growth_.processBlock(n)` once at the chunk head, `gGrowth =
   growth_.getCurrentValue()` held across the chunk, `g[s] = velocity_ * gGrowth * mse_.process()`.
   `envOutput_ = g[n-1]` feeds `getEnvelopeOutput()`.
4. `body_.processStereoBlock(cloudL_, cloudR_, bodyL_, bodyR_, n)` — **not in place**
   (`continuous_body.h:1155-1156`).
5. `atmos_.processStereoBlock(bodyL_, bodyR_, atmosL_, atmosR_, n)` — the tap reads the **post-body**
   signal.
6. bus = `bodyL_[s] + atmosL_[s]` (and R) — a **plain sum, no second gain**. The atmosphere's own trim is
   already applied inside the component (`setLevel` is *"Output gain trim"*, `atmosphere_engine.h:944-949`,
   multiplied at `:2233`); multiplying by `getLevel()` again would square it and make FR-063's axis
   quadratic.
7. Spatial stage (plan §2.6) → `carryL_/carryR_`.
8. Silence fade tail (D3), **guarded**: `if (fadeRemaining_ > 0) { w = fadeRemaining_ /
   silenceRampSamples_; outL[s] += fadeTailL_*w; outR[s] += fadeTailR_*w; --fadeRemaining_; }`.
   The guard is load-bearing — the ramp is 48 samples @ 48 kHz inside a 64-sample chunk, and an
   unguarded post-decrement would run negative and add an inverted, magnitude-**growing** tail forever.
9. Level detector + retirement counter (plan §2.7):
   `chunkPeak = max over s of max(|carryL_[s]|, |carryR_[s]|)`;
   `level_ = (chunkPeak > level_) ? chunkPeak : chunkPeak + (level_ - chunkPeak) * levelReleaseCoeff_;`
   `quiescentChunks_ = (level_ < kTailSilenceThreshold) ? quiescentChunks_ + 1 : 0;`
10. `carryAvail_ = n; carryRead_ = 0; carryIsLifeOnly_ = false;` — **`lastOut*` is NOT assigned here.**

Spatial stage (plan §2.6), once per chunk: `orbit_.processBlock(n)`; `x = orbit_.getCurrentValue()`,
`y = orbit_.getY()`; `panNorm = (x + 1.0f) * 0.5f`; `equalPowerGains(panNorm, gL, gR)`;
`gL *= kSqrt2; gR *= kSqrt2;` `gainLSm_.setTarget(gL); gainRSm_.setTarget(gR);`
`widthPct_ = std::clamp(widthBase_ + y * widthSpan_, kMinVoiceWidthPct, kMaxVoiceWidthPct);`
`ms_.setWidth(widthPct_);` Then per sample `tmpL[s] = busL * gainLSm_.process()` (and R), then
`ms_.process(tmpL, tmpR, carryL_.data(), carryR_.data(), n)`.
`widthBase_` defaults to 100.0f and is settable via `setVoiceWidthBasePercent` (T013's `VoiceWidth` macro
target); `widthSpan_` is fixed at 50.0f and is **not** macro-writable. **`getGrowth()` is never read** —
its neutral is 0 and growth is already baked into the radius `getY()` returns.

`isFinished()` = `!mse_.isActive() && cloud_.isQuiescent() && quiescentChunks_ >= kQuiescentChunksToRetire`.

`advanceLifeOnly(n)` runs on the **same carry clock** (plan §2.8): consume-and-discard from
`carryAvail_`/`carryRead_`, refilling with `advanceOneChunkLifeOnly()` — which ticks
`orbit_.processBlock(kControlChunkSamples)`, releases the level detector at chunk peak 0, zero-fills
`carryL_/carryR_`, sets `carryAvail_ = kControlChunkSamples`, `carryIsLifeOnly_ = true`, and sets
`lastOutL_ = lastOutR_ = 0.0f` (a non-rendering voice emits silence).

`noteOn(freqHz, velocity)` / `noteOff()` — plan §2.9 verbatim, including the D4 rule 3 discard
(`if (carryIsLifeOnly_) { carryAvail_ = 0; carryRead_ = 0; }` — a **live** retrigger's carry is real
program material and is kept) and `renderedSinceNoteOn_ = false`, `hasSounded_ = true`.

**Verify:** the nine cases above pass; zero warnings. Then re-run
`"SeraphisVoice_ShipsDocumentedDefaults"` to confirm T002 did not regress.

---

# GROUP 4 — `SeraphisVoice`: envelope modes, forwarders, configure gate, `silence()`, `stateFinite`

## T004 — FR-020/021/022, FR-030/030a/031, FR-034, FR-035

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_voice.h`,
`dsp/tests/unit/systems/seraphis_voice_test.cpp`.

**Failing test first — `seraphis_voice_test.cpp`**

`TEST_CASE("SeraphisVoice_EnvelopeModesBehave")` — FR-020/021/022:
- **Standard:** `noteOn`, render; `getEnvelopeOutput()` reaches `≥ 0.99` only after **~2 s** (require it
  `< 0.99f` at 1.5 s and `≥ 0.99f` by 2.2 s).
- **Growth (the reference-envelope form — this is the only satisfiable statement of "match the
  `GrowthEnvelope` shape alone"):** `setEnvelopeMode(Growth)`, `setGrowthDurationSeconds(10.0f)`. Advance
  a standalone reference `GrowthEnvelope` alongside the voice — same `prepare(sampleRate)`, same
  `setDuration(10.0f)`, `trigger()` on the same sample, `processBlock(64)` on the same chunk grid — and
  for every control chunk from the **second** onward (the first covers the 0 ms pre-sustain stage walk)
  require
  `getEnvelopeOutput() == Approx(velocity * stageLevel[sustainPoint] * growthRef.getCurrentValue()).margin(1e-4f)`
  with `stageLevel[2] = 0.7f`.
- Secondary, derived from the real curve (`kSteepness = 10.0f`, `growth_envelope.h:18-26`, `:102`; solving
  the normalised logistic for 0.99 gives τ = **0.9085**): the composite is monotone non-decreasing and
  reaches `≥ 0.99` of its final value **only within the last 10 %** of the duration. (Do **not** use
  "last 5 %" — that is unsatisfiable against the shipped component.)
- **Legato retrigger:** a `gate(true)` on a `Releasing` envelope does not drop the composite below its
  pre-gate level on the next sample.
- **Round-trip clause (mandatory — the only detector for the `applyStage` shadow defect):** after
  `setEnvelopeMode(Growth)` then `setEnvelopeMode(Standard)`,
  `getEnvelopeStageTimeMs(0) == Approx(2000.0f)` and `(1) == Approx(4000.0f)`, and a gated render again
  reaches `≥ 0.99` only after ~2 s.

`TEST_CASE("SeraphisVoice_ForwardersAndConfigureTimeGate")` — FR-030/FR-031:
- every forwarder with a component getter round-trips: set a distinct non-default value, read it back
  through the const accessor. Includes
  `setTravelMode(SpectralMorphEngine::TravelMode::Spline)` → `getTravelMode()`.
- **Reject path:** on a **sounding** voice, `setSpectralState(1, makeFactoryState(SpectralStateId::Bell))`
  is rejected — `morph().getStateCount()` unchanged and
  `getRejectedConfigureTimeCallCount()` increments by exactly 1.
- **Accept path (mandatory — a gate that rejects unconditionally must fail a named test):** on a freshly
  prepared, never-noted voice, `setSpectralState(1, makeFactoryState(SpectralStateId::Bell))` +
  `setSpectralStateCount(3)` are observable as `morph().getStateCount() == 3` with
  `getRejectedConfigureTimeCallCount()` **unchanged**. Repeat after `noteOn`/`noteOff` + enough render
  for `isFinished()`.

`TEST_CASE("SeraphisVoice_SilenceHardClears")` — FR-034 (this is the only test of the hard clear):
on a **sounding** voice (rendered ≥ 1 s at full level), call `voice.silence()` then render 512 samples:
- samples `[silenceRampSamples_, 512)` are **all** `≤ kTailSilenceThreshold` on both channels;
- samples `[0, silenceRampSamples_)` are bounded in magnitude by the pre-silence `|lastOut|` and are
  monotonically non-increasing in magnitude;
- a subsequent `reset(); noteOn(...)` sounds normally (RMS `> 1e-3f` over the next second).

`TEST_CASE("SeraphisVoice_ChainOrderIsCloudEnvelopeBodyAtmosphere")` — FR-010. **All mutations go through
`SeraphisVoice`'s FR-030 forwarders** — `body()`/`atmos()` are const references, so `body().setMix(0)`
does not compile:
- with `voice.setMix(0.0f)` the output is the enveloped cloud + atmosphere (non-silent);
- with `voice.setLevel(0.0f)` the output is body-only (non-silent, and differs from the previous arm by
  more than `kMetricTolerance` under `compareFingerprints`);
- with `voice.setCloudDecaySec(30.0f)`, `noteOff()` followed by 5 s of render is still non-silent
  (RMS `> 1e-5f`) — **this is what proves the envelope is pre-body**.

`TEST_CASE("SeraphisVoice_MorphHandoffRunsEveryChunk")` — FR-011/FR-012. Drive `setTargetPosition` mid
render; assert `cloud().getPartialTargetAmplitude(i)` tracks `morph().getOutputAmplitudes()[i]` within
one control chunk (64 samples) for at least 16 partials.

`TEST_CASE("SeraphisVoice_BodyDefaultsAreAudible")` — SC-010 clause 4, the four getter-less
`ContinuousBody` rows. Three renders of **4 s** each from a fresh `prepare` at a fixed seed and identical
note script:
- **A** = `prepare` only;
- **B** = `prepare` + explicitly calling each of the four forwarders at the FR-019 table value:
  `setDamping(0.25f)`, `setResonance(0.7f)`, `setMix(1.0f)`, `setCloudMix(0.25f)`;
- **C** = `prepare` + `setDamping(0.60f)`.
`REQUIRE(compareFingerprints(A, B).withinTolerance())` **and**
`REQUIRE(!compareFingerprints(A, C).withinTolerance())` — the second is the mandatory positive control
proving the render is actually sensitive to the parameter, so B ≡ A is evidence that `prepare` shipped
0.25 rather than evidence that nothing is wired.

**Implement** — plan §2.10, §2.11, §2.12, plus the FR-030/FR-030a forwarders.

- `setEnvelopeMode` (plan §2.10): on entering Growth, force **every** stage from 0 up to and including
  `sustainPoint − 1` to 0 ms preserving level and curve — zeroing stage 0 alone is **not** enough,
  because `advanceToNextStage()` only enters `Sustaining` when `currentStage_ == sustainPoint_`
  (`multi_stage_envelope.h:386-389`), so FR-020's 4 s stage-1 ramp would still shape the composite. On
  leaving Growth, restore from the `stageTimeMs_`/`stageLevel_` shadows.
- `setEnvelopeStageTimeMs(stage, ms)` is re-expressed as `applyStage(stage, stageLevel_[stage], ms)` —
  one write path only. The shadow always takes the caller's value (so the getter reads back what was
  set, FR-030's "stored but not applied" in Growth mode) while the forward to `mse_` is gated on
  `envMode_ == Standard || stage >= mse_.getSustainPoint()`.
- Every FR-030 forwarder is one-to-one with **no added clamping** (clamping stays in the owning
  component): cloud ×9, morph ×5 (`setEntropy`, `setBloom`, `setTravelMode`, `setTargetPosition`,
  `setTravelRate`), body ×11, atmosphere ×8, plus `setEnvelopeStageTimeMs`, `setEnvelopeReleaseMs`,
  `setSpatialDepth/Rate/Coupling/Growth`, `setVoiceWidthBasePercent`.
- FR-030a per-voice freeze: `captureFreeze()`, `releaseFreeze()`, `isFreezeCaptured()` forwarding
  one-to-one to `atmosphere_engine.h:909`, `:928`, `:940`.
- **Configure-time gate (plan §2.11):** `setSpectralState` / `setSpectralStateCount` are gated on
  **`!hasSounded_ || isFinished()`**; otherwise `++rejectedConfigCalls_` and return without touching
  `morph_`. **Not `isFinished()` alone** — a freshly prepared, never-rendered voice would then reject
  every configure-time call it will ever receive. `setTargetPosition` is **not** gated (FR-062 needs it
  live).
- `silence()` (plan §1 D3): capture `fadeTailL_/R_` from `lastOutL_/R_`, arm
  `fadeRemaining_ = silenceRampSamples_`, then **hard-clear**: `cloud_.reset()`, `morph_.reset()`,
  `body_.reset()`, `mse_.reset()`, `growth_.reset()`, **`atmos_.reset()`** — *not* `atmos_.silence()`,
  which only sets `runState_ = Silencing` (`atmosphere_engine.h:644-650`) and keeps rendering the grain
  bed under a 10 ms decay (`kSilenceRampMs = 10.0f`, `:278`; per-sample decay `:2237-2242`) = 480
  samples @ 48 kHz, **ten times** the voice's own ramp, making the assertion window above unsatisfiable.
  Finally `carryAvail_ = carryRead_ = 0`. `orbit_` and `ms_` are **not** cleared — they are life state.
- `stateFinite()` (plan §2.12) = `body_.stateFinite() && morph_.stateFinite() && isFiniteBits(level_) &&
  isFiniteBits(lastOutL_) && isFiniteBits(lastOutR_) && isFiniteBits(gainLSm_.getCurrentValue()) &&
  isFiniteBits(gainRSm_.getCurrentValue())`, where `isFiniteBits` is a **private, plain, inlinable**
  `static bool` copying `continuous_body.h:1346-1351` verbatim (memcpy the float to `std::uint32_t`,
  return `(bits & 0x7F800000u) != 0x7F800000u`). **Not** the `ITERUM_NOINLINE` wrapper — that one's own
  header forbids per-sample use (`atmosphere_engine.h:1203-1206`). **`std::isnan`/`std::isinf`/
  `std::isfinite` appear nowhere.**

**Verify:** the six cases above plus everything from T002/T003; zero warnings.

---

# GROUP 5 — `SeraphisEngine`: pool, prepare, render loop, sum gain, output stage

## T005 — FR-040/041, FR-044, FR-050/051/052, FR-053/053a/054/055

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_engine.h`,
`dsp/tests/unit/systems/seraphis_engine_test.cpp`.

> **Every `SeraphisEngine` in every TU is heap-allocated: `auto engine =
> std::make_unique<SeraphisEngine>();`. Never a plain local.** `voices_` alone is
> `std::array<SeraphisVoice, 16>` ≈ 758 KB (plan §3.2's measured arithmetic: 47 380 B/voice), and SC-005
> constructs **two** engines plus two `AetherReverb`s — ~1.5 MB against MSVC's **1 MiB** default
> main-thread stack, with no `/STACK` set anywhere in `dsp/tests/CMakeLists.txt`. The sibling systems TUs
> declare their subjects as plain locals (e.g. `atmosphere_engine_nonfinite_test.cpp:368-369`); copying
> that pattern here stack-overflows before a single assertion runs.

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_PolyphonyAndPreparation")` — FR-040/FR-041:
- after `prepare(48000.0, SeraphisEngineConfig{})`, `getPolyphony() == 8`.
- `setPolyphony(16)` **mid-render** allocates nothing: wrap it in an `AllocationScope`
  (`tests/test_helpers/allocation_detector.h`) and require 0 allocations. Run the liveness probe (one
  deliberate allocation observed) first.
- `setPolyphony(0)` clamps to 1; `setPolyphony(99)` clamps to 16.
- **shrink:** with 8 voices sounding, `setPolyphony(4)`; the four excess slots keep rendering (their
  `getVoice(i).isFinished()` is false and `getRenderingVoiceCount() >= 4` for at least 1 s), and the
  allocator reports them `Idle`.

`TEST_CASE("SeraphisEngine_VoiceSumGain")` — FR-052. One note at polyphony 1 versus the same note at
polyphony 8; after the `kSumGainSmoothMs` smoother has settled (≥ 200 ms), the RMS ratio is
`Approx(1.0f/std::sqrt(8.0f)).epsilon(0.01)`. Second clause: the ratio does **not** drift as other
voices' tails retire — measure it again 10 s later with the same polyphony and require the same value
within 1 %.

`TEST_CASE("SeraphisEngine_OutputStageIsSeparate")` — FR-053/053a/054:
- drive the engine hard (16 voices, all sounding): `processStereoBlock`'s output **may** exceed
  `-1.0 dBFS` (assert it does at least once, so the case is not vacuous);
- the same buffer after `processOutputStage` never exceeds
  `TruePeakLimiter::kDefaultCeilingDb = -1.0f` by more than 0.1 dB.
- **FR-053 constants clause (mandatory — nothing else asserts drive 0 dB / saturation 0.15 / mix 1.0):**
  drive a −6 dBFS 1 kHz sine through `processOutputStage` at the shipped defaults and `REQUIRE`
  `calculateTHD(out, n, 1000.0f, sr)` (`tests/test_helpers/signal_metrics.h:111`, returns **percent**)
  below a bound **recorded from this measurement**; positive control: `setOutputSaturation(1.0f)` on the
  same input must exceed that bound by a stated margin. Without this the ceiling clause passes just as
  readily at saturation 1.0 (the limiter bounds the result either way) and the roadmap's "no aggressive
  distortion" traceability row is vacuous.

`TEST_CASE("SeraphisEngine_ResetAndSilence")` — FR-055. After `silence()` the **next block is exactly 0**
for every sample on both channels (`== 0.0f`, not a tolerance) — which holds only because
`SeraphisEngine::silence()` is per-voice `silence()` then the tail-**clearing** `reset()`, never
`resetForSteal()`. A subsequent note sounds normally (proves the atmosphere was `reset()`, not left
latched). **Positive control:** record (in a comment/`WARN`, not as a shipping assertion) the measured
non-zero peak a `resetForSteal()`-based variant produces.

`TEST_CASE("SeraphisEngine_DeferredVoiceFinished")` — FR-044. A voice in `VoiceState::Releasing` whose
`getVoice(i).isFinished()` is false is **never** returned to `Idle`: sample `getVoiceState(i)` every
block over a 15 s script with `setCloudDecaySec(30)` and require the `Releasing → Idle` transition
happens only on a block where `isFinished()` had just become true.

`TEST_CASE("SeraphisEngine_VoiceSeedsAreDistinct")` — FR-050 / SC-006(a). For all 16 slots,
`getVoice(v).getSeed() == deriveStreamSeed(engineSeed, SeraphisEngine::kVoiceSaltBase + v)`; the 16
values are pairwise distinct and all non-zero. Repeat after `setSeed(0xABCDEF01u)`.

**Implement** — plan §3.1 through §3.5.

- `prepare` (plan §3.3): prepares **all `kMaxVoices` slots**, not `polyphony_`, so `setPolyphony` can
  never allocate (FR-041). `static_cast<void>(allocator_.setVoiceCount(polyphony_));` — the real
  signature is `[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(std::size_t)`
  (`voice_allocator.h:326`), **not** the `void` the spec's Existing-components table records; a bare
  statement discards a `[[nodiscard]]` (MSVC C4834 / GCC `-Wunused-result`) against the zero-warning
  gate. `allocator_.setAllocationMode(AllocationMode::Oldest)`, `setStealMode(StealMode::Hard)` (FR-043).
  `satL_/satR_.prepare(sr, kControlChunkSamples)` with `setDrive(kOutputDriveDb)`,
  `setSaturation(kOutputSaturation)`, `setMix(1.0f)`. `limiter_.prepare(sr, kMaxBlockSamples)` at
  `kDefaultCeilingDb`. `sumGain_.configure(kSumGainSmoothMs, (float)sr)` and **snap** it to
  `1/√polyphony_` (no ramp from 0 on the first block).
- `processStereoBlock` (plan §3.4): the engine runs its **own absolute control grid** (the
  `aether_reverb.h:2181-2195` idiom) and passes slices to the voices, which absorb any partition via
  their FIFOs. The loop is plan §3.4's code verbatim, including:
  - `if (phase == 0u) runPreRenderControlStep();` before the slice, and
    `if (sampleCounter_ % kControlChunkSamples == 0u) runPostRenderControlStep();` after the slice that
    **completes** a chunk (the split is load-bearing for D4 — T008);
  - the render loop bound is `v < kMaxVoices` **unconditionally**. There is **no** `renderingHigh_`:
    a bookkeeping bound would leave spare slots receiving neither `processStereoBlock` nor
    `advanceLifeOnly` and would break SC-016's "**every** voice" clauses;
  - `isRendering(v)` = `v < polyphony_ ? (state != Idle || !voices_[v].isFinished())
    : !voices_[v].isFinished()`; non-rendering slots take `voices_[v].advanceLifeOnly(slice)`;
  - the per-sample non-finite guard at the accumulation point using the **plain inlinable**
    `isFiniteBits` duplicated privately in `SeraphisEngine` (never a `SeraphisVoice` private, which is
    not reachable): on a hit, `nonFinitePending_ |= (1u << v)`, `break` (that voice contributes 0 for the
    rest of the slice), **reset deferred** to `runPreRenderControlStep` — T008 wires the recovery;
  - `sumGainHeld_` read **once per control chunk** in `runPreRenderControlStep` step 1 as
    `sumGain_.advanceSamples(kControlChunkSamples - 1); sumGainHeld_ = sumGain_.process();` — the `- 1`
    is load-bearing because `OnePoleSmoother::process()` itself advances one sample
    (`smoother.h:197-210`).
- `runPostRenderControlStep()` step 5 (FR-044): for every slot whose `VoiceState` is `Releasing` **and**
  whose `voices_[v].isFinished()` is true → `allocator_.voiceFinished(v)` and `bloomOffMask_ |= bit`.
  Running this on the absolute control grid rather than "once per block" makes retirement timing
  partition-invariant (V-2).
- `processOutputStage` (plan §3.5): guard order first, then per-64-sample slices
  `satL_.process(l + done, slice)` / `satR_.process(...)`, then `limiter_.processBlock(l, r,
  static_cast<int>(n))` **always last**, over the whole block. The saturator's slicing is a **cadence
  choice, not a size constraint** — `TapeSaturator::prepare` ignores its block-size argument
  (`tape_saturator.h:141`); Phase 8 must not copy the loop as if it were a requirement.
- `reset()` = per-voice `reset()` + saturator/limiter clear. `silence()` = per-voice `silence()` then
  per-voice **`reset()`** (the tail-clearing one) + saturator/limiter clear. Both are documented at their
  declarations as **not** audio-thread operations (R13: each reaches a ~2 MiB capture-ring `std::fill`
  per voice, i.e. 32 MiB @ 48 kHz across the pool).
- `setPolyphony(n)` clamps to `[1, kMaxVoices]` and is the **only** place `sumGain_.setTarget(1/√n)`
  moves (FR-052). The shrink-event handling itself is T006.

**Record `sizeof(SeraphisEngine)` here.** Print it, land a `static_assert` on `ceil(measured × 1.05)`
beside `kVoiceSizeBound`'s, and carry the figure into `compliance.md` — it is what makes the heap rule
auditable rather than advisory.

**Verify:** the six cases above; zero warnings.

---

# GROUP 6 — `SeraphisEngine`: note dispatch, retrigger provenance, polyphony-shrink orphans

## T006 — FR-042/043/047, plan §3.6, §3.6.0, §3.6.2

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_engine.h`,
`dsp/tests/unit/systems/seraphis_engine_test.cpp`.

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_NoteDispatch")` — FR-042/FR-043:
- `noteOn(60, 0)` behaves as `noteOff(60)` (Edge Case 13).
- a same-note retrigger occupies **exactly one** slot.
- **Retrigger-provenance clause (mandatory — plan §3.6.0):** a same-note retrigger on a live sounding
  voice must
  (a) produce **exactly one** `voices_[i].noteOn()` on that slot — assert via the audible consequence:
      the render across the retrigger boundary shows no discontinuity larger than the surrounding
      `maxPerSampleDelta`, and the note frequency after the retrigger is the **new** note's, never the
      old one's;
  (b) leave `getVoice(i).atmos().isFreezeCaptured()` and the atmosphere's grain counter
      (`getTotalGrainsBorn()`, `atmosphere_engine.h:1009`) **untouched** across the retrigger — proving
      no `silence()`/`resetForSteal()` ran;
  (c) increment `getVoiceAllocationSerial(i)` by **exactly 1**.
  Without this, the allocator's retrigger-path `Steal` event (`voice_allocator.h:846-853`, followed by a
  `NoteOn` for the same slot at `:865-872`) maps onto the FR-047 teardown and every retrigger silently
  wipes a live voice.
- **Orphan clause:** with 8 voices sounding, `setPolyphony(4)`, then a `noteOn` that lands on one of the
  force-idled slots while its tail is still ringing → the new note's first control chunk contains **no
  residue** of the old tail (its level at the first chunk boundary is below the pre-shrink tail level by
  at least 20 dB). Contrast control: an ordinary retrigger onto a still-ringing **non-orphan** slot does
  **not** tear down (assert (b) again).

`TEST_CASE("SeraphisEngine_StealTeardownOrder")` — FR-034/FR-047. Force a steal and assert the
`silence()` → `resetForSteal()` → `noteOn()` sequence completes **inside one block**: the block
containing the steal ends with the new note already sounding, and
`getVoice(v).atmos().isFreezeCaptured()` is false afterwards (proving the atmosphere was re-entered via
the reset, not left latched).

**Implement** — plan §3.6, §3.6.0, §3.6.2.

`noteOn(note, velocity)`:

```
if (velocity == 0) { noteOff(note); return; }
retriggerSlot_ = -1;
for (i < polyphony_)
    if (allocator_.getVoiceState(i) != VoiceState::Idle && allocator_.getVoiceNote(i) == (int)note)
        { retriggerSlot_ = (int)i; break; }
if (retriggerSlot_ < 0 && noIdleVoice()) freeChosenVictimSlot();     // T007
dispatch(allocator_.noteOn(note, velocity));
retriggerSlot_ = -1;
```

Provenance must be established **before** the allocator call, using the allocator's own public read
surface (`getVoiceState` `:424`, `getVoiceNote` `:406`) — the same predicate `findVoicePlayingNote`
(`:830-841`) uses internally.

`dispatch(span)` follows `PolySynthEngine::dispatchPolyNoteOn` (`poly_synth_engine.h:597-620`):

| Event type | Action |
|---|---|
| `NoteOn` | if `(orphanTail_ & (1u << i))` → FR-047 teardown `silence(); resetForSteal(); noteOn(f, vel/127.0f)` and clear the bit; else plain `voices_[i].noteOn(event.frequency, event.velocity/127.0f)`. Either way set `bloomOnPending_ \|= bit` and `voiceSerial_[i] = nextSerial_++` **exactly once per dispatched span**. |
| `NoteOff` | `voices_[i].noteOff()` |
| `Steal`, `i == retriggerSlot_` | **Ignore the event entirely.** It is the allocator's bookkeeping for the outgoing note on an ordinary retrigger; the `NoteOn` that follows in the same span does the work. No `silence()`, no `resetForSteal()`, no extra serial bump. |
| `Steal`, `i != retriggerSlot_` | Engine-initiated steal: FR-047 teardown, then set `bloomOnPending_`, `bloomOffMask_`, `voiceSerial_`. **Unreachable by construction** under §3.6.1 (the victim slot is freed *before* `allocator_.noteOn`, so the allocator emits a plain `NoteOn`); retained as a defensive branch. |

`resetForSteal()`, **not** `reset()` — this is the one path that must keep the D3 anti-click fade tail
armed across the teardown. Every other reset caller in the engine uses the tail-clearing `reset()`.

`setPolyphony` shrink handler (plan §3.6.2) — the **sole writer of `orphanTail_`**: consume
`allocator_.setVoiceCount(n)`'s returned span, treat each `NoteOff` event as a **musical release**
(`voices_[i].noteOff()`), do **not** call `voiceFinished` on those slots, and for every slot the shrink
force-idled while `!voices_[i].isFinished()` set `orphanTail_ |= (1u << i)`. The bit is cleared when the
slot becomes `isFinished()` (in `runPostRenderControlStep` step 4) or when the `NoteOn` row tears it
down. **The predicate must be `orphanTail_`, not `allocator_.getVoiceState(i) == Idle`** — the latter can
never be true at dispatch time (`allocateNote` stores `Active` the moment `findIdleVoice()` returns the
slot, `voice_allocator.h:933-935`, and pushes the `NoteOn` only afterwards at `:1062`), so that predicate
is dead code. `!isFinished()` **alone** is not a substitute either — it is true for any live retrigger
target and re-introduces the retrigger defect above.

**Verify:** the two cases above plus every T005 case; zero warnings.

---

# GROUP 7 — `SeraphisEngine`: quietest steal, amnesty, allocation-serial tie-break

## T007 — FR-045/046, RA-4, plan §3.6.1

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_engine.h`,
`dsp/tests/unit/systems/seraphis_engine_test.cpp`.

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_QuietestStealWithAmnesty")` — SC-011, **all five clauses**. Establish a
saturated pool at known distinct levels by rendering **≥ 8 control chunks (≥ 10.7 ms)** per voice at its
intended level (sufficient because FR-033's attack is instant), then read `getVoiceLevel(i)` back and
`REQUIRE` the values distinct **before** forcing the steal.

1. the lowest-level `Releasing` voice is taken;
2. with none `Releasing`, the lowest-level `Active` voice is taken;
3. a `Releasing` voice at or above `kAmnestyLevelThreshold = 0.0316f` is **skipped** while a candidate
   below it exists;
4. **Edge Case 15 fallback:** with **every** voice `Releasing` and **every** level ≥
   `kAmnestyLevelThreshold`, a forced steal still takes the lowest-level `Releasing` slot. (Clauses 1–3
   all pass for an implementation that falls through to the `Active` branch or refuses to steal — this
   clause is the only one that catches it.)
5. **FR-045 step 4 tie-break:** two voices given `noteOn` then immediately `noteOff` with **no render in
   between** are both `Releasing` at exactly `level_ == 0.0f` (the reset value — the detector only
   updates inside a chunk step), an exact tie. A third note must steal the one with the **lower**
   `getVoiceAllocationSerial(i)`; serials must be strictly increasing in note-on order; and
   `getLastStolenVoiceIndex()` must equal **both** the FR-045 selection **and** the slot the allocator's
   returned `NoteOn` named.

`TEST_CASE("SeraphisEngine_VoiceReclaimIsCorrect")` — SC-012, **always-on form: a 15 s script with a 10 s
tail** (`setCloudDecaySec(30.0f)` on every voice):
- clause 1: a slot is never reported `Idle` by `getVoiceState(i)` while `getVoiceLevel(i) >
  kTailSilenceThreshold`, and is never dropped from `getRenderingVoiceCount()` while
  `getVoice(i).isFinished()` is false. **The polyphony-shrink case is exempt from the allocator-state
  half** (`voice_allocator.h:347-352` force-idles); the rendering half still applies.
- clause 2: after the last note-off plus enough render, `getActiveVoiceCount() == 0 &&
  getRenderingVoiceCount() == 0`. Add a `[.slow]` sibling
  `TEST_CASE("SeraphisEngine_VoiceReclaimIsCorrect_Full", "[.slow]")` with the full 60 s script + 45 s
  of tail (derived: 30 s cloud decay + 1.15 s detector release + 4 chunks ≈ 31.2 s, ~14 s margin).

**Implement** — plan §3.6.1, `freeChosenVictimSlot()`:

```
1. Pick victim v:
     candidates = { i : allocator_.getVoiceState(i) == Releasing }
     eligible   = { i in candidates : getVoiceLevel(i) < kAmnestyLevelThreshold }
     if eligible non-empty        -> v = argmin level over eligible
     else if candidates non-empty -> v = argmin level over candidates      // Edge Case 15
     else                         -> v = argmin level over { i : state == Active }
     ties -> lower voiceSerial_[i]                                        // FR-045 step 4
2. if getVoiceState(v) == Active:
       static_cast<void>(allocator_.noteOff(static_cast<std::uint8_t>(allocator_.getVoiceNote(v))));
       // events DISCARDED: this is bookkeeping, not a musical release
3. allocator_.voiceFinished(v);        // legal now that v is Releasing (:288-292 early-outs otherwise)
4. bloomOffMask_ |= (1u << v);         // FR-071's STOLEN half lives HERE, not in §3.6's Steal row
5. lastStolenVoice_ = static_cast<int>(v);
```

Step 4 is load-bearing: because the victim slot is freed *before* `allocator_.noteOn`, the allocator
returns a plain `NoteOn`, the `Steal` row never fires on a real steal, and
`runPostRenderControlStep` step 5 never runs for the slot (the allocator already idled it). Without step
4, `bloomNoteOff` is **never** issued for a stolen voice.

The tie-break key is **engine-owned** (`voiceSerial_[i] = nextSerial_++`, bumped exactly once per
dispatched span that lands a note on slot `i`): `VoiceAllocator`'s `timestamp` is a member of its
**private** internal voice struct (`voice_allocator.h:483`, `private:` at `:471`), and substituting
"lower voice index" is **not** equivalent — the allocator's `Oldest` walk ranks by timestamp
(`:575-576`) and only falls back to first-index on an exact tie.

After `allocator_.noteOn` returns, the dispatch loop **asserts** that the returned `NoteOn` event names
`v`. A mismatch is a defect, not a fallback (R6).

**Verify:** the two cases above plus T005/T006; zero warnings.

---

# GROUP 8 — `SeraphisEngine`: freeze fan-out, bloom collection, non-finite containment

## T008 — FR-030a (engine half), FR-071, FR-072, D4/D5, plan §3.7, §3.8

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_engine.h`,
`dsp/tests/unit/systems/seraphis_engine_test.cpp`.

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_FreezeFansOutAndRetries")` — FR-030a:
- `setAtmosphereFreeze(true)` on a **cold** pool → **no** voice reports `isFreezeCaptured()` immediately
  (the call arms and returns; capture happens on the control grid);
- `getAtmosphereFreeze() == true`;
- after ≥ `captureSeconds` (4 s) of render, **every** voice's `isFreezeCaptured()` is true;
- a voice stolen afterwards re-arms and eventually captures again;
- `setAtmosphereFreeze(false)` → every voice's `isFreezeCaptured()` is false.

`TEST_CASE("SeraphisEngine_BloomTracksHeldChord")` — SC-017:
- three `noteOn`s; render until each sounding voice's snapshot has been taken (D4: the first absolute
  chunk boundary after that voice completed a `renderOneChunk`);
- for each sounding voice, `getLastBloomPartials(i)` matches the FR-071 selection **recomputed from
  `getVoice(i).cloud()` at assertion time** to within **0.1 cent**;
  `getLastBloomCount(i) == std::min(getVoice(i).cloud().getActivePartialCount(), std::size_t{32})`;
  the emitted order is **ascending by frequency**;
- **Staleness positive control (mandatory):** record the FR-071 selection **before** the note-on and
  `REQUIRE` the snapshot differs from it by more than 0.1 cent on at least one partial. Without this the
  whole case passes on a snapshot taken one control chunk early;
- **Steal clause (mandatory):** force a steal on a voice with a live bloom and require
  `consumeBloomEvents().noteOffMask` to carry that slot in the same poll as — or an earlier poll than —
  its new `noteOnMask` bit. This is the only test of FR-071's "when the voice is **stolen**" half;
- **Note-off clause:** after `noteOff` + reclaim, the voice's bit appears in
  `consumeBloomEvents().noteOffMask`.

`TEST_CASE("SeraphisEngine_NonFiniteContainmentIsBounded")` — the engine-side half of FR-072 (the
composed-chain SC-018 case lives in the nonfinite TU, T015). Using the same bit-pattern/volatile-sink
construction, poison one voice's contribution and require: the engine's own `processStereoBlock` output
is finite for every sample of the next 1 s; `getNonFiniteRecoveryCount()` increments **exactly once**;
**at most one** voice reset happens per control chunk (assert by injecting into 4 voices in the same
block and requiring the recovery count to reach 4 only after ≥ 4 control chunks).

**Implement** — plan §3.7, §3.8, and the D4-split control step.

- `setAtmosphereFreeze(bool)` (plan §3.8): **arm only** — `freezeLatched_ = on;` and, when on,
  `freezePending_ = (1u << kMaxVoices) - 1u;` with **no** `captureFreeze()` call. When off, call
  `releaseFreeze()` on all 16 slots and clear `freezePending_`. Calling `captureFreeze()` on 16 slots
  inline would put up to 32 FFT(2048) into one caller call (R14).
- `runPreRenderControlStep()` step 2: service **at most `kFreezeRetriesPerChunk = 1`** pending voice per
  control chunk — the lowest set bit of `freezePending_`, round-robined from the last serviced index:
  `if (!voices_[v].isFreezeCaptured()) voices_[v].captureFreeze(); else clear the bit;`. All 16 rings
  fill from the same `reset()`, so an un-staggered retry lands 32 FFT(2048) inside one 1.33 ms chunk;
  staggering spreads them over ≥ 16 chunks (≥ 21 ms), which cannot threaten FR-030a's ≥ 4 s observable.
  `reset()`, `silence()` and a steal on voice `v` re-arm bit `v` while `freezeLatched_` is true.
- `runPreRenderControlStep()` step 3 (FR-072 recovery): service at most
  `kResetsPerControlChunk = 1` bit of `nonFinitePending_` → `voices_[v].reset(); ++nonFiniteRecoveries_;`
  clear the bit. **`reset()`, not `resetForSteal()`** — a poisoned voice must not carry a poisoned fade
  tail forward. The bound exists because each reset reaches a ~2 MiB capture-ring `std::fill` (R13); an
  inline reset inside the per-sample accumulation loop could trigger 16 of them (32 MiB) inside one
  1.33 ms chunk.
- `runPostRenderControlStep()` step 4 (D4 bloom collection): for each `v` in `bloomOnPending_`, **if and
  only if `voices_[v].hasRenderedSinceNoteOn()`**, snapshot into `lastBloomPartials_[v]` /
  `lastBloomCount_[v]`, set `bloomOnMask_ |= bit`, clear the pending bit. A voice whose flag is still
  false keeps its pending bit and is snapshotted at a later boundary. **This is what proves
  `cloud_.updateControl` has consumed `freqDirty_`** — `setFundamentalHz` only calls `markFreqDirty()`
  (`harmonic_cloud.h:402`) and `frequencyHz_[]` is recomputed only at the head of `updateControl`
  (`:1656-1661`), while `getPartialFrequencyHz` returns `frequencyHz_[i]` verbatim (`:955-957`). Also
  clear `orphanTail_` for any slot that has become `isFinished()`.
- `collectHeldPartials` (plan §3.7):
  `outCount = min(cloud.getActivePartialCount(), kBloomPartialCap, capacity)`; build
  `idx[0..active)` in a stack `std::array<std::uint8_t, HarmonicCloud::kMaxPartials>`; sort by
  `(getPartialCurrentAmplitude desc, index asc)`; take the first `outCount`; **re-sort those by
  `getPartialFrequencyHz` ascending**; write `dest[0..outCount)`. `std::sort` with a `noexcept`
  comparator over a 64-element stack array is allocation-free and exception-free (introsort/heapsort);
  it runs once per note-on inside a control step, **never** per sample (R12).
- `consumeBloomEvents()` returns `{bloomOnMask_, bloomOffMask_}` and clears both. **D5:** the caller polls
  it **after** `processStereoBlock` returns, so `AetherReverb` is driven `bloomNoteOn`-late by exactly
  one control chunk. That is the only ordering under which the partial frequencies are correct; ≤ 64
  samples on a resonant-emphasis stage is inaudible.

**Verify:** the three cases above plus all of T005–T007; zero warnings.

---

# GROUP 9 — Composed-chain test helper

## T009 — `tests/test_helpers/seraphis_chain.h` (FR-070)

**Files created:** `tests/test_helpers/seraphis_chain.h`.
**Files edited:** `dsp/tests/unit/systems/seraphis_engine_test.cpp`.
**No CMake change** — `test_helpers` is an INTERFACE library that enumerates no headers (V-6).

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_ComposedChainRuns")` — FR-070. Build a `SeraphisEngine` (heap), an
`AetherReverb` (heap) and a default `SeraphisMacroMatrix`; run `renderSeraphisChain` over a 3 s script
with two note-ons and one note-off at non-zero, non-block-aligned sample offsets; require the output is
**non-silent** (RMS `> 1e-4f`) and **finite** for every sample, and that
`AetherReverb::getActiveBloomResonatorCount()` (`aether_reverb.h:2583`) is `> 0` after the note-ons
(proving the bloom lifecycle actually ran).

**Implement** — plan §5, exactly.

```cpp
struct SeraphisChainScript {
    struct Event { double seconds;                       // SECONDS, not samples
                   enum class Kind { NoteOn, NoteOff, Freeze, Polyphony } kind;
                   std::uint8_t note, velocity; std::size_t value; };
    std::vector<Event> events;                           // TEST-ONLY: heap is fine, SC-008 does not scan this file
    [[nodiscard]] static std::uint64_t toSamples(double seconds, double sampleRate) noexcept;
};

void renderSeraphisChain(SeraphisEngine& engine, AetherReverb& reverb,
                         const SeraphisMacroMatrix& macros, const SeraphisChainScript& script,
                         double sampleRate, std::size_t blockSize, std::size_t totalSamples,
                         std::vector<float>& outL, std::vector<float>& outR);
```

**Two timing rules, both load-bearing:**

1. **Sub-divide every caller block at event boundaries.** Do **not** dispatch a whole block's events at
   the block head: split each block at every event's resolved sample index, dispatch the events due at
   that index, render to the next event index or the block end. `SeraphisEngine::noteOn`/`noteOff` have
   no sample-offset parameter, and sub-division is how a sample-accurate offset is delivered without one
   — it is also exactly what Phase 8's host event loop does with `sampleOffset`. **Without this SC-014
   cannot pass with any non-trivial script**: an event at sample S fires at `ceil(S/B)·B`, which agrees
   across `{1, 7, 64, 65, 512, 4096}` only when S is a multiple of 1 863 680 samples (≈ 38.8 s @ 48 kHz).
2. **Seconds, not samples.** `Event::seconds` is resolved per render via `toSamples(seconds, rate)`. A
   sample-denominated script is a *different piece of music* at 44.1 / 48 / 96 kHz, so SC-013 would not
   be comparing the same script. `renderSeraphisChain` asserts the events are sorted by `seconds` and
   that the resolved indices are non-decreasing.

**Allocation contract (SC-007 depends on it):** `outL`/`outR` and the internal `dryL`/`dryR`/`wetL`/`wetR`
scratch are sized **once, before the render loop**, to `blockSize`; `std::vector::resize` is never called
from inside the loop; `buf` for the bloom partials is a `std::array<float, 32>` held across the whole
render, not a per-slice local.

Per sub-slice, in this **fixed order** (Phase 8's processor reproduces it verbatim):

1. dispatch the script events due at this slice's start onto `engine`;
2. `macros.apply(engine); const auto at = macros.computeAetherTargets();` then push
   `reverb.setMix(at.mix)`, `setSize`, `setWidth`, `setShimmerOctaveSend`, `setShimmerFifthSend`,
   `setBloomSend`, `setSizeBreathDepth`, `setDimensionalityTideDepth`
   (`aether_reverb.h:2336, 2208, 2333, 2280, 2285, 2295, 2320, 2328`);
3. `engine.processStereoBlock(dryL, dryR, n)`;
4. `reverb.processStereoBlock(dryL, dryR, wetL, wetR, n)` (`:2164`);
5. `engine.processOutputStage(wetL, wetR, n)`;
6. `const auto be = engine.consumeBloomEvents();` — for each `noteOffMask` bit `reverb.bloomNoteOff(v)`
   (`:2473`); then for each `noteOnMask` bit `engine.collectHeldPartials(v, buf.data(), 32, count)` →
   `reverb.bloomNoteOn(static_cast<std::int32_t>(v), buf.data(), count)` (`:2392`).
   **Step 6 is after step 3, by D4/D5.**

The helper may include `krate/dsp/effects/aether_reverb.h`; `tools/lint-layers.js` scans only
`dsp/include/krate/dsp`, so this is out of scope by construction and needs no exclusion.

**Verify:** `dsp_systems_tests.exe "SeraphisEngine_ComposedChainRuns"`; zero warnings.

---

# GROUP 10 — Composed chain: determinism, allocation, sample-rate and block-size invariance

## T010 — SC-005, SC-007, SC-013, SC-014

**Files edited:** `dsp/tests/unit/systems/seraphis_engine_test.cpp` (only — the headers are frozen from
here on unless a defect is found).

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_SeededRenderIsReproducible")` — SC-005. Two `SeraphisEngine` +
`AetherReverb` pairs (**all four heap-allocated**), same seed, same config, same script, **5 s**
always-on → `compareFingerprints(a, b).withinTolerance()` (`worstMetricRelativeError ≤ 1e-5`,
`worstSampleError ≤ 1e-4`, `render_fingerprint.h:49-52`). **Bit-exact comparison is forbidden** (FR-084,
`tools/lint-float-bit-goldens.js`). `[.slow]` sibling at 30 s.

`TEST_CASE("SeraphisEngine_NoAllocInProcess")` — SC-007. `AllocationScope` from
`tests/test_helpers/allocation_detector.h`; **liveness probe first** (one deliberate allocation
observed), then **0 allocations** across a 10 s script covering note-on/off, one steal, one polyphony
change. **The scope wraps only** `engine.processStereoBlock`, `engine.processOutputStage`,
`engine.noteOn`/`noteOff`/`setPolyphony`/`setAtmosphereFreeze`, `engine.collectHeldPartials`/
`consumeBloomEvents`, and `macros.apply`/`computeAetherTargets` — **not** `renderSeraphisChain`'s setup
and **not** `AetherReverb`, so a helper-side slip is diagnosed as a helper slip rather than reported as
an engine defect. `[.slow]` sibling: the full 60 s script with the polyphony 1↔16 sweep and all macros
swept.

`TEST_CASE("SeraphisEngine_SampleRateIndependence")` — SC-013. Always-on form: **48 kHz vs 44.1 kHz over
3 s**, the **same seconds-denominated script** resolved per rate via `toSamples`, composed chain,
`setDriftDepthCents` held at its FR-019 default of **0**. RMS and mean-abs within **5 %** computed per
rate on that rate's own render (no resampling); peak bounded separately at **10 %**; measured
fundamental within **1 cent** over `[2.0 s, 3.0 s)` after note-on (past the 20 ms pitch smoother,
`continuous_body.h:168`, and the FR-020 attack) using a 65 536-point FFT with
`Window::generateBlackmanHarris`. Grain-dependent timing detail is explicitly exempt. `[.slow]` sibling:
all three rates (44.1 / 48 / 96 kHz) over 10 s.

`TEST_CASE("SeraphisEngine_BlockSizeInvariance")` — SC-014. Always-on form: partitions
`{1, 64, 65}` over **1 s**; `[.slow]` sibling: all six `{1, 7, 64, 65, 512, 4096}` over 10 s. Reference =
the 512 partition (always-on: 64). **Max absolute per-sample difference ≤ 1e-5f.**
`render_fingerprint.h` is kept only as a **secondary** aggregate check — it samples 32 of 480 000 points
and its 1e-5 relative `totalVariation` bound is tighter than the sub-components guarantee for a
re-partitioned computation.
**Two required in-case coverage clauses:**
- the parameter set must put at least one partition boundary **inside** a 64-sample control chunk **and**
  at least one grain birth in that partial chunk — assert via
  `getVoice(0).atmos().getTotalGrainsBorn()` (`atmosphere_engine.h:1009`) advancing at a sample count
  that is **not** a multiple of 64. Otherwise D1's FIFO path is assumed rather than exercised.
- the script **must** contain at least one note event after sample 0 (a note-on, a note-off and a steal),
  and those events **must** land at the same absolute sample index in every run: assert directly that
  `getVoiceAllocationSerial(v)` transitions and `getLastStolenVoiceIndex()` changes at **identical**
  sample counts across all partitions. (This is what §5 rule 1 delivers; it is a separate constraint
  from D1 and equally required.)

**Implement**

No production code is expected. If a case fails, the defect is in the T003/T005/T009 implementation and
is fixed there — **never** by relaxing a bound.

**Verify:** the four always-on cases pass; the `[.slow]` siblings pass under
`dsp_systems_tests.exe "[.slow]"`; zero warnings.

---

# GROUP 11 — Composed chain: clicklessness

## T011 — SC-003, SC-004

**Files edited:** `dsp/tests/unit/systems/seraphis_engine_test.cpp`.

**Failing test first — `seraphis_engine_test.cpp`**

Both cases use the **matched-regime, single-render** construction. The obvious "control render with no
steals" form is one Phase 2 already measured, found unsatisfiable and formally withdrew
(`specs/seraphis-phase2-harmonic-cloud/spec.md:727-737`, measured ratio 1.785 against a 1.5 bound) —
do not reinstate it.

`TEST_CASE("SeraphisEngine_VoiceStealIsClickless")` — SC-003. Always-on form: **8 steals over a 10 s**
saturated-pool render at randomised block offsets (`[.slow]` sibling: 32 steals over 60 s).
1. **Test statistic:** for each steal, `maxPerSampleDelta` over the ±10 ms window centred on it.
2. **Reference:** **64** windows of the **same length (20 ms)** drawn from the **same render** at offsets
   ≥ 50 ms clear of any steal, uniformly spaced; the reference is their **95th percentile** — a
   percentile, not a max, so one unlucky window cannot inflate the bound.
3. **Bound:** `max(test statistics) ≤ 1.5 × reference`. The support is identical on both sides.
4. No sample of the composed chain's output exceeds the `TruePeakLimiter` ceiling.
**Both positive controls are mandatory:**
   a. *Detector wiring* — the same statistic over a non-steal window with a deliberately injected
      one-sample step of **2× that window's own `maxPerSampleDelta`** must exceed the bound. Denominated
      in delta, **not** peak: Phase 2 measured that a step below the signal's natural per-sample swing is
      by construction undetectable (`:748-753`).
   b. *Criterion wiring* — a `kSilenceRampMs = 0` build must **fail** clause 3; record the measured
      figure in `compliance.md` (not asserted in the shipping test).
**Teardown-cost clause (§3.6.1 / R13, mandatory):** the same render records the per-teardown wall time in
µs **and the worst single-block wall time** over the whole run, and `REQUIRE`s that worst block to stay
inside the 512-sample budget (**10.67 ms @ 48 kHz**). This is the only place the FR-047 teardown's ~2 MiB
capture-ring memset is timed — SC-001's timed region contains no steals and SC-002 measures steady state.
Record both figures, and the K (note-ons per block) at which the budget is exceeded, in `compliance.md`.

`TEST_CASE("SeraphisEngine_NoteLifecycleIsClickless")` — SC-004. **Same construction and the same two
positive controls**, over 16 note-ons and 16 note-offs always-on (`[.slow]`: 64 + 64), **including
retriggers on a still-sounding voice** (which exercises `harmonic_cloud.h:604-606`).
**Growth clause:** use the FR-020/021/022 reference-`GrowthEnvelope` comparison from T004
(`getEnvelopeOutput() ≈ velocity · sustainLevel · growthRef.getCurrentValue()`, margin 1e-4, whole
duration) plus the re-derived threshold secondary (**≥ 0.99 of final only within the last 10 %** of the
duration, τ ≈ 0.909 plus the 20 ms `kOutputSmoothMs` lag). **Do not use "last 5 %"** — it is unsatisfiable
against the shipped component and inert against the defect it targets.

**Implement**

Shared fixtures in the TU's anonymous namespace: `maxPerSampleDelta(span)`, the percentile helper, and
the injected-step positive control. No production code expected; a failure is a defect in T003/T006/T007.

**Verify:** both always-on cases pass; `[.slow]` siblings pass; zero warnings.

---

# GROUP 12 — Composed chain: output ceiling and idle liveness

## T012 — SC-015, SC-016

**Files edited:** `dsp/tests/unit/systems/seraphis_engine_test.cpp`.

**Failing test first — `seraphis_engine_test.cpp`**

`TEST_CASE("SeraphisEngine_OutputNeverExceedsCeiling")` — SC-015. Always-on: **10 s at 8 voices**
(`[.slow]`: 60 s at 16 voices). Adversarial composed-chain render: **all five macros at 1** (note that
puts **Gravity at full stone**, +0.5 from neutral), maximum resonance, frozen atmosphere, infinite Aether
decay. No sample of **`processOutputStage`'s output** exceeds
`TruePeakLimiter::kDefaultCeilingDb = -1.0f` (`true_peak_limiter.h:46`) by more than **0.1 dB**; no
non-finite sample. The voice sum and the reverb return are intermediate and are explicitly **not**
bounded — assert on the post-`processOutputStage` signal only.

`TEST_CASE("SeraphisEngine_LifeModulatorsRunAtIdle")` — SC-016. **No notes ever played.** Always-on:
**24 s** — one full breath cycle at Phase 6's pinned 0.05 Hz (`[.slow]`: 60 s). Drive
`processStereoBlock` every block so every idle voice receives `advanceLifeOnly`.
1. **every** voice's `getSpatialAzimuth()` has non-zero total variation — **all 16 slots, not just the 8
   below the shipped polyphony**. Narrowing this to `v < polyphony_` would be exactly the quiet scope
   reduction the criterion exists to prevent, and it is why §3.4's render loop runs `v < kMaxVoices`.
2. **every** voice's `getSpatialWidthPercent()` has non-zero total variation — the guard against a width
   axis multiplied by `getGrowth()`'s neutral 0.
3. non-zero total variation of `AetherReverb::getEffectiveDelayLengthSamples(0)`
   (`aether_reverb.h:2506`) — the only life observable the class exposes.
4. the audio path is **exactly 0** for every sample.

**Implement**

No production code expected. A clause-1/2 failure means §3.4's loop bound or `isRendering` predicate
regressed; a clause-3 failure means the helper is not pushing `sizeBreathDepth`/
`dimensionalityTideDepth` (see T013's §4.1.1 drift hazard).

**Verify:** both always-on cases pass; `[.slow]` siblings pass; zero warnings.

---

# GROUP 13 — `SeraphisMacroMatrix`

## T013 — FR-056…FR-065, SC-009, SC-010

**Files edited:** `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`,
`dsp/tests/unit/systems/seraphis_macro_test.cpp`.
(The `friend class SeraphisMacroMatrix;` declaration is already in `seraphis_engine.h` from T001, so this
task touches **no** other header.)

**Failing test first — `seraphis_macro_test.cpp`**

`TEST_CASE("SeraphisMacroMatrix_TableIsWellFormed")` — FR-056/057/058:
- the four `static_assert`s over the table compile: `everyRowOwnerIsValid`,
  `everyAetherRowHasAPodField`, `noRowUsesSteppedCurve`, `everyTargetInFr061to065IsPresent`;
- each of FR-061…FR-065's named targets is present **with the stated sign**, asserted row by row;
- every Gravity row carries a **signed** amount;
- no row uses `ModCurve::Stepped`.

`TEST_CASE("SeraphisMacroMatrix_NeutralIsInert")` — FR-059/FR-060, SC-010 clauses 2–3:
- **clause 2:** `apply()` at the FR-060 neutral (`dream = bloom = dissolve = entropy = 0`,
  `gravity = 0.5`) on a freshly prepared engine is the **identity** on the whole FR-019 read-back surface
  from T002 — every value unchanged;
- **clause 3 (literal half):** `computeAetherTargets()` equals the table's `base` for all eight fields:
  `mix = 0.35f`, `size = 0.50f`, `width = 1.0f`, `shimmerOctaveSend = 0.0f`, `shimmerFifthSend = 0.0f`,
  `bloomSend = 0.0f`, `sizeBreathDepth = 0.20f`, `dimensionalityTideDepth = 0.20f`;
- **clause 3 (render half — the only detector for a drifted duplicated literal):** with the macros at
  neutral, a reverb driven by `computeAetherTargets()` **every block** and a reverb **never touched after
  `prepare`** must produce fingerprints satisfying `compareFingerprints(...).withinTolerance()`. A
  literal-vs-literal comparison cannot detect drift; a `base` of 0 for `sizeBreathDepth` would flatten
  SC-016 clause 3 and a `base` of 0 for `mix` would run the whole chain dry while every other criterion
  still passed;
- **FR-059 idempotence clause (mandatory, at a NON-neutral point — nothing else asserts FR-059 at all):**
  hold **Bloom = Dissolve = 0.7, Gravity = 0.8**, render 1 s calling `apply()` **every block**, and
  compare against a render calling `apply()` **once** before the loop;
  `REQUIRE(compareFingerprints(a, b).withinTolerance())`. The neutral-point renders cannot cover this —
  at the FR-060 neutral §4.3 writes `base` and cannot step anything by construction.

`TEST_CASE("SeraphisEngine_MacroSweepsMoveTheirAxis")` — SC-009. **Always-on probe:** 1 macro × 5 steps ×
1 s, with every metric computed and every direction correct. **`[.slow]` full grid:** 5 macros × 21 steps
× 4 s on the composed chain, fixed seed and note, each non-swept macro at **its own** FR-060 neutral.
- **Gate:** Spearman `|ρ| ≥ 0.9` (monotone **trend**, not strict); **no-discontinuity**: consecutive step
  change ≤ **3×** the mean step change; **minimum end-to-end effect size**: Dream ≤ **50 %** of its start
  value, Bloom ≥ **+20 %**, Dissolve ≥ **+0.15** absolute, Gravity ≥ **6 dB**, Entropy ≥ **+0.10**
  absolute.
- **Pinned detector:** 65 536-point FFT (`primitives/fft.h`) with `Window::generateBlackmanHarris`
  (`core/window_functions.h`) — the exact pair `harmonic_cloud_spectral_test.cpp:51, :157` uses — with
  `REQUIRE(fft.isPrepared())` so a future `kMaxFFTSize` tightening fails loudly. Analysis segment = the
  **last 1 s** of each step; peak picking at −60 dB-from-max with ≥ 20 dB peak-to-local-median SNR;
  parabolic interpolation on log magnitude; peaks matched to grid slots by nearest ratio, unmatched
  excluded; **fewer than 24 detected partials fails the case**. Flatness via `calculateSpectralFlatness`
  (`tests/test_helpers/signal_metrics.h:326`).
- **Dream's primary metric is measured on `processStereoBlock`'s dry voice sum with the Aether `mix`
  target held at neutral**, so reverb smearing cannot corrupt the partial detector; its reverb-send
  sub-axis is a wet-tail secondary on the composed chain.
- **Dissolve's primary differential goes through `SeraphisVoice`'s FR-030 forwarder, not the accessor** —
  `atmos()` is a const reference, so `AtmosphereEngine::setLevel(0)` on it does not compile. The zeroed
  arm applies `voice.setLevel(0.0f)` to **every** voice via the forwarder, and **the matrix is not
  applied on that arm** (applying it would rewrite `AtmosLevel` from the table's `base` and restore the
  very thing being zeroed).
- **Bloom's stereo-width secondary must isolate the `VoiceWidth` row:** sample `getSpatialWidthPercent()`
  at a fixed orbit phase (`setSpatialRate(0)` with a pinned seed, so y is constant across the sweep) and
  require it to rise monotonically with Bloom; measure the side-energy/correlation observable with
  `setStereoSpread` held at its FR-019 base. Otherwise `CloudStereoSpread` carries the whole secondary
  and a completely broken `VoiceWidth` row passes.

**Implement** — plan §4.

- Types exactly as plan §4.1: `SeraphisMacro`, `SeraphisMacroTargetOwner {Voice, Engine, Aether}`,
  `SeraphisMacroTarget` (the 19 voice-owned + 8 Aether-owned names listed there), `SeraphisAetherTargets`
  (defaults **are** the eight `base` values, so a default-constructed POD is already the FR-060 neutral),
  `SeraphisMacroValues { dream = 0, bloom = 0, dissolve = 0, gravity = 0.5f, entropy = 0 }`,
  `SeraphisMacroRow { macro, owner, target, base, amount, curve }`.
- The eight Aether `base` values are **duplicated literals with citations** (plan §4.1.1) — every
  `AetherReverb` default sits below `private:` at `aether_reverb.h:2724` and is unreachable from any
  consumer, and FR-056 forbids naming a Layer 4 type at all:
  `mix 0.35f (:2779)`, `size 0.50f (:2730)`, `width 1.0f (:2777)`, the three sends `0.0f (:2760)`,
  `sizeBreathDepth 0.20f (:2749)`, `dimensionalityTideDepth 0.20f (:2750)`.
- `VoiceWidth` routes to **`SeraphisVoice::setVoiceWidthBasePercent`** with `base = 100.0f` (plan §4.1.0),
  **not** to `ms_.setWidth` — §2.6 recomputes and pushes the M/S width once per control chunk, so a row
  writing into `ms_` directly is overwritten within ≤ 64 samples and **no criterion detects it**.
- Evaluation (plan §4.3): `acc = base(t)`; for each row on `t`,
  `acc += amount * applyModCurve(curve, m)`, except Gravity, where `g = (m - 0.5f) * 2.0f` and
  `acc += amount * applyModCurve(curve, std::abs(g)) * (g < 0 ? -1.0f : 1.0f)`; then write through the
  owning setter (which does its own clamping). Summation-then-clamp is `ModulationEngine`'s order
  (`modulation_engine.h:44-54`). **No `if (neutral) return;` shortcut** — inertness must be a property of
  the arithmetic so a mis-signed row cannot hide behind a fast path.
- Three targets are hit by **two** macros each and that is specified, not accidental: `CloudRichness`
  (Bloom ↑ / Gravity −), `CloudSpectralTiltDb` (Bloom ↑ / Gravity −), `MorphEntropy` (Dream − /
  Entropy ↑).
- `apply(SeraphisEngine&)` iterates `v < engine.getPolyphony()` and reaches `engine.voices_[v]` through
  the friendship declared in T001 (FR-085's `getVoice(i)` stays **const** for tests).
- **`amount` and `curve` are the tuning surface (Q3).** Expect iteration here against SC-009's minimum
  effect-size table. The checked-in table is the record of the tuning; only `Linear`, `Exponential` (x²)
  and `SCurve` (x²(3−2x)) may appear.

**Verify:** the three cases above (always-on forms) plus the `[.slow]` grid; re-run T002's
`SeraphisVoice_ShipsDocumentedDefaults` and T012's SC-016 to confirm the matrix has not moved the base
point; zero warnings.

---

# GROUP 14 — Perf and non-finite TUs (parallel-safe: fully disjoint new files)

## T014 [P] — Perf TU: SC-001, SC-002

**Files edited:** `dsp/tests/unit/systems/seraphis_perf_test.cpp` (only).

**Failing test first**

`TEST_CASE("SeraphisEngine_FullPolyCpuBudget", "[.perf]")` — SC-001. **The RA-1 normative worst-case
scenario, stated in code:**
- polyphony **8**, **all 8 voices sounding**, none idle;
- all five macros at their **FR-060 neutral** (Gravity 0.5, rest 0);
- cloud at **64 active partials with drift**, morph + entropy, spatial stage active;
- body at its **worst measured material configuration** — measure all five materials in the TU and use
  the worst, following `continuous_body_perf_test.cpp:936-940`;
- atmosphere **frozen**, engaged via `setAtmosphereFreeze(true)` and **asserted** with
  `REQUIRE(engine->getVoice(v).isFreezeCaptured())` for **every** voice **before** timing starts — a
  silent no-op capture (`atmosphere_engine.h:911-916`) would otherwise measure the cheaper unfrozen path
  (a 3.1-point difference at 8 voices);
- `AetherReverb` at RA-1 row **(c)**: `PrepareConfig{ numChannels = 16, shimmerEnabled = true,
  bloomEnabled = true, spectralDiffusionEnabled = true, diffusionFftSize = 4096 }`, `setSize(1)`,
  `setDensity(1)`, 32 bloom resonators — matching `aether_reverb_perf_test.cpp:329-330`;
- measured on **the composed chain** (`processStereoBlock → AetherReverb::processStereoBlock →
  processOutputStage`).

Constants, **two distinct numbers, named separately** (plan §6.2.1):

```cpp
constexpr double kBlockBudgetNs   = (512.0 / 48000.0) * 1e9;   // 10 666 666.7
constexpr double kReferenceNs     = kBlockBudgetNs * 0.25;     //  2 666 666.7  (the 25 % ceiling)
constexpr double kRegressionFactor = 1.15;                     // the RUN-TIME gate
constexpr double kBaselineHeadroom = 1.05;                     // recording convention ONLY
static_assert(kBaselineNs * kRegressionFactor <= kReferenceNs);
```

**Why 1.15 and not Phase 6's 1.5 or the 1.05 recording headroom:** at RA-1's predicted 20.36 % a baseline
recorded as `ceil(20.36 % × 1.05) = 21.38 %` gives `21.38 × 1.5 = 32.1 % > 25 %`, so the `static_assert`
would **fail to compile**; 1.05 compiles but collapses the run-time band to 5 % on a best-of-8 wall-clock
measurement, a flake generator. `21.38 × 1.15 = 24.59 % ≤ 25 %` holds with ~0.4 points of margin. The
admissible baseline ceiling is `25 % / 1.15 = 21.74 %`.
**≥ 8 trials, best-of-N, idle machine.** Transcribe the baseline-replacement procedure into the TU
header, following `aether_reverb_perf_test.cpp`. Prediction to beat: **20.36 %**.
**If the `static_assert` fails, the ONLY admissible responses are:** re-derive the shipped voice count
(RQ-1 — `kMaxVoices = 16` means no ABI change) or reduce Phase 7's own composition cost. **Never** a
Phase 2/4/5/6 gate (N-10), **never** the 25 % ceiling (RQ-1 kept it), **never** a quiet widening of
`kRegressionFactor` without the re-derived arithmetic above.

`TEST_CASE("SeraphisVoice_CompositionOverhead", "[.perf]")` — SC-002. Ratio of one
`SeraphisVoice::processStereoBlock` to the arithmetic sum of **exactly eight** standalone sub-components
measured in the same TU under the pinned shared configuration: `HarmonicCloud`, `SpectralMorphEngine`,
`ContinuousBody`, `AtmosphereEngine`, `MultiStageEnvelope`, `GrowthEnvelope`, `OrbitModulator`,
`MidSideProcessor`. Configuration: 64 partials + drift, FR-019 defaults, worst body material, atmosphere
at default density **unfrozen**, `Standard` envelope gated on, spatial depth 0.5, 512-sample blocks
@ 48 kHz. **Bound ≤ 1.10.** ≥ 8 trials, best-of-N per subject, ratio computed from the **aggregates**,
never from single runs.

Also record here (for `compliance.md`): the worst-case per-chunk freeze-retry cost (R14) and the
`sizeof(SeraphisEngine)` figure from T005.

**Verify:** `dsp_systems_tests.exe "[.perf]" 2>&1 | tail -20` — green with `static_assert`ed baselines;
zero warnings. The perf TU **must not** be in the `-fno-fast-math` block.

## T015 [P] — Non-finite TU: SC-018

**Files edited:** `dsp/tests/unit/systems/seraphis_nonfinite_test.cpp` (only). This TU **is** in the
`-fno-fast-math -fno-finite-math-only` block from T001.

**Failing test first**

`TEST_CASE("SeraphisEngine_NonFiniteIsContained")` — SC-018:
- non-finite inputs are built **from bit patterns through a volatile sink** — never
  `std::numeric_limits<float>::quiet_NaN()` / `infinity()`, which fold to finite garbage under the macOS
  leg's `-ffast-math`;
- inject into **one** voice; the **composed chain's** output is finite for **every sample of the next
  5 s**;
- `getNonFiniteRecoveryCount()` increments **exactly once**;
- the other voices' renders are **fingerprint-identical to a control** run without the injection
  (`compareFingerprints(...).withinTolerance()`);
- `stateFinite()` on the recovered voice is true afterwards.

**Verify:** `dsp_systems_tests.exe "SeraphisEngine_NonFiniteIsContained"`; zero warnings. Confirm the TU
carries the fast-math source property (grep `dsp/tests/CMakeLists.txt`).

---

# GROUP 15 — Integration

## T016 — Registration verification, full-suite run, portability, lints, wall clock, compliance

**Files edited:** none required unless a check fails; `specs/seraphis-phase7-voice-engine/compliance.md`
is created.

**Steps, in order**

1. **Verify all three registration sites** (FR-080/FR-081) — a green local build does not prove them:
   ```bash
   grep -n "seraphis" dsp/CMakeLists.txt              # 3 header entries
   grep -n "seraphis" dsp/lint_all_headers.cpp        # 3 include lines, Layer 3 block
   grep -n "seraphis" dsp/tests/CMakeLists.txt        # 5 TUs in dsp_systems_tests + nonfinite in the fast-math block
   ```
   Confirm `unit/systems/seraphis_nonfinite_test.cpp` is in the `-fno-fast-math -fno-finite-math-only`
   block and that the **other four Phase 7 TUs are not**.
2. **Full always-on suite:**
   ```bash
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
   ```
   Last line must read `All tests passed (…)`. **Time this run and the pre-Phase-7 run**; the delta is
   SC-020's figure and must be **≤ 60 s**. If it exceeds 60 s, take the next demotion from the spec's
   always-on/`[.slow]` table and **record it** — never a silent one.
3. **`[.slow]` and `[.perf]` lanes:**
   ```bash
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.slow]" 2>&1 | tail -10
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tail -20
   ```
4. **Portability and lints (FR-082/FR-084, SC-019):**
   ```bash
   node tools/check-portability.js
   node tools/lint-layers.js
   node tools/lint-nonfinite-symbols.js
   node tools/lint-float-bit-goldens.js
   node tools/lint-simd-aligned-loadstore.js
   ```
   All clean. `tools/check-portability.js` needs **no** exclusion — no Phase 7 TU carries an `#error`
   guard; if one is ever added, the exclusion at `:85-96` must be added with it.
5. **WSL warning sweep (SC-019):** compile the three headers and five TUs under
   `g++ -Wall -Wextra -std=c++20`. **A green MSVC build proves nothing about the Linux/macOS legs.**
6. **SC-008 compliance greps**, over the **three shipped headers only** (`seraphis_chain.h` is test-only
   and exempt):
   ```bash
   grep -nE 'new |delete |malloc|std::vector|std::string|std::function|mutex|lock|throw|try \{|printf|fopen|std::cout|shared_ptr|unique_ptr|resize\(|push_back|emplace|std::isnan|std::isinf|isfinite' \
     dsp/include/krate/dsp/systems/seraphis_voice.h dsp/include/krate/dsp/systems/seraphis_engine.h dsp/include/krate/dsp/systems/seraphis_macro_matrix.h
   grep -nE 'StereoField|VoiceModRouter|ModulationEngine|PolySynthEngine|SynthVoice|AetherReverb' \
     dsp/include/krate/dsp/systems/seraphis_voice.h dsp/include/krate/dsp/systems/seraphis_engine.h dsp/include/krate/dsp/systems/seraphis_macro_matrix.h
   ```
   **Zero code hits** on both. The second sweep is FR-002's negative half: all of those are Layer 3
   (except `AetherReverb`), so `lint-layers.js` cannot catch them and nothing else guards RA-3's argument
   for not instantiating `StereoField`. Prose citations inside comments are permitted and must be
   **enumerated** in `compliance.md`.
7. **Clang-tidy:**
   ```powershell
   ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
   ```
8. **Write `specs/seraphis-phase7-voice-engine/compliance.md`** — one row per FR (FR-001…FR-085) and per
   SC (SC-001…SC-020), each carrying **file:line evidence or a measured number copied from real test
   output**. Generic "implemented" / "test passes" claims are not acceptable. Figures that must appear:
   `sizeof(SeraphisVoice)`, `kVoiceSizeBound`, `sizeof(SeraphisEngine)`, the SC-001 measured percentage
   against the 25 % ceiling and the 21.74 % admissible-baseline ceiling, the SC-002 ratio, the FR-053 THD
   bound and its positive control, the SC-003 per-teardown µs / worst-block µs / the K at which the
   512-sample budget is exceeded, the R14 worst-case per-chunk freeze cost, the SC-020 always-on wall
   clock, and the SC-008 grep results.

**Pluginval is skipped** — no plugin source changed.

---

## Traceability: task → plan section → spec requirement

| Task | Plan | Spec |
|---|---|---|
| T001 | §0.1, §0.2, §7 | FR-001, FR-002, FR-056, FR-070, FR-080, FR-081 |
| T002 | §2.1–§2.4 | FR-003, FR-004, FR-005, FR-013, FR-014, FR-016, FR-017, FR-019, FR-019a, FR-020 |
| T003 | §1 D1, §2.5–§2.9 | FR-006, FR-007, FR-010, FR-011, FR-012, FR-015, FR-018, FR-023, FR-024, FR-025, FR-026, FR-027, FR-032, FR-033, SC-006(b) |
| T004 | §2.10–§2.12, §1 D3 | FR-021, FR-022, FR-030, FR-030a (voice half), FR-031, FR-034, FR-035, SC-010 clause 4 |
| T005 | §3.1–§3.5 | FR-040, FR-041, FR-044, FR-050, FR-051, FR-052, FR-053, FR-053a, FR-054, FR-055, SC-006(a) |
| T006 | §3.6, §3.6.0, §3.6.2 | FR-042, FR-043, FR-047 |
| T007 | §3.6.1 | FR-045, FR-046, SC-011, SC-012 |
| T008 | §3.7, §3.8, §1 D4/D5 | FR-030a (engine half), FR-071, FR-072, SC-017 |
| T009 | §5 | FR-070 |
| T010 | §5, §6.2 | SC-005, SC-007, SC-013, SC-014 |
| T011 | §6.2, §3.6.1 R13 | SC-003, SC-004 |
| T012 | §3.4, §6.2 | SC-015, SC-016 |
| T013 | §4 | FR-056…FR-065, SC-009, SC-010 |
| T014 | §6.2.1 | SC-001, SC-002, FR-083 |
| T015 | §6.2 | SC-018, FR-084 |
| T016 | §7 | FR-082, SC-008, SC-019, SC-020 |

---

## Standing rules for every task

- **RT safety:** every public method except `prepare` is `noexcept`, allocation-free, lock-free,
  exception-free, I/O-free. Pools are sized at `prepare`.
- **Layer discipline:** the three shipped headers include Layers 0–2 and Layer 3 peers only. **No Layer 4
  header, ever.** `tools/lint-layers.js` gates it.
- **Non-finite:** bit-pattern checks only (`isFiniteBits`, the `continuous_body.h:1346-1351` shape).
  `std::isnan` / `std::isinf` / `std::isfinite` appear nowhere in the three headers.
- **No bit-exact float goldens** — `render_fingerprint.h` / measured tolerances only.
- **No narrowing in brace init** — designated initialisers everywhere, especially
  `AtmosphereEngine::PrepareConfig` and `AetherReverb::PrepareConfig`.
- **Zero warnings** under MSVC **and** `g++ -Wall -Wextra -std=c++20`. A green MSVC build is not evidence.
- **Never relax a criterion to fit an implementation.** If a bound cannot be met, fix the implementation
  or raise it as a recorded deviation with the measurement that forces it.
