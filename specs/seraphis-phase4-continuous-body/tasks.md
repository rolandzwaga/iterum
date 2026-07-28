# Tasks: Seraphis Phase 4 — Continuous Resonant Body

**Spec:** `specs/seraphis-phase4-continuous-body/spec.md`
**Plan:** `specs/seraphis-phase4-continuous-body/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 4 (lines 198–222)
**Branch:** `feat/seraphis-phase1-life-modulators` (all Seraphis phases land on this one branch — do not rename, do not create a new one)

**Deliverables:** one new Layer 3 header (`dsp/include/krate/dsp/systems/continuous_body.h`), two
strictly-additive amendments to shared Layer 2 headers, five new test TUs, one `dsp/tests/CMakeLists.txt`
edit. **No plugin work.**

---

## How to read this file

- Tasks run in **group order**. Inside a group, tasks marked **[P]** touch fully disjoint file sets and may
  run concurrently; everything else is sequential.
- Every task is **self-contained**: it names the exact files, the **failing test to write first** (file,
  `TEST_CASE` name, the assertions with their numbers), the implementation intent, and the test target that
  verifies it.
- Canonical order inside every task: **write the failing test → run it and see it fail → implement → zero
  compiler warnings → run the named target green.**
- **No commit tasks.** Commits happen outside this workflow.
- Build command (Windows, always the full CMake path):
  ```bash
  CMAKE="/c/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target <target>
  build/windows-x64-release/bin/Release/<target>.exe "TestName*" 2>&1 | tail -5
  ```
  Catch2 filters are **positional** case names, not `-c`; tags go in `[brackets]`.

**Cross-cutting rules that make a task a defect if violated** (they are not restated in every task):
no allocation / lock / exception / IO outside `prepare()`; every `ContinuousBody` constant is
`static constexpr` **inside the class** (FR-008); no `std::isnan` / `std::isinf` /
`std::numeric_limits<float>::infinity()` / `quiet_NaN()` in component **or** test code — bit-pattern checks
and `volatile`-sink construction only (the macOS leg builds `-ffast-math`); no bit-exact float golden except
the single sanctioned same-binary comparison in T004; layer includes point downward or sideways only
(`tools/lint-layers.js` governs, not the prose in `dsp/CLAUDE.md`); zero compiler warnings.

**Deviation from the standard workflow shape, stated once:** the single `dsp/tests/CMakeLists.txt`
registration task is **T002 (Group B)**, not the final group. `dsp/tests/CMakeLists.txt` lists test sources
**explicitly and never globs** — an unregistered TU silently drops and reports "all tests passed". Placing
the registration last would make every failing-test-first task from T003 onward unverifiable. The final
group (Group P) instead **audits** that registration and runs the full matrix.

---

## Group A — Blocking prerequisites

### T001 — ODR sweep and lint baseline

**Files:** none created or edited. This is a verification gate.

**Do:**

1. Re-run the ODR sweep (plan §1). A name that was free when the plan was written is not guaranteed free
   now:
   ```bash
   grep -rn "class ContinuousBody\b\|struct ContinuousBody\b" dsp/ plugins/
   grep -rn "class BodyMaterial\b\|struct BodyMaterial\b\|enum class BodyMaterial\b" dsp/ plugins/
   grep -rn "class MaterialProfile\b\|struct MaterialProfile\b" dsp/ plugins/
   grep -rn "class DecayCloud\b\|struct DecayCloud\b" dsp/ plugins/
   grep -rn "class DriveNormalizer\b\|struct DriveNormalizer\b" dsp/ plugins/
   grep -rn "class EngineSlot\b\|struct EngineSlot\b" dsp/ plugins/
   ls dsp/include/krate/dsp/systems/continuous_body.h
   grep -n "retune" dsp/include/krate/dsp/processors/waveguide_string.h
   ```
   **Expected: 0 hits for every name; "No such file" for the header; 0 hits for `retune`.**
   Any non-zero hit is a **stop** — escalate, do not rename silently.

2. Record the names that are **taken** and must never be used: `Material`
   (`processors/modal_resonator.h:81`, namespace scope), `MaterialCoefficients` (`:91`), `BodyMode`
   (`processors/body_resonance.h:60`), `ResonatorBank`, `ModalResonator`, `GranularEngine`.
   Namespace-scope constants that must **not** be redeclared: `kBodyModeCount`, `kBodyPresetCount`,
   `kBodyFDNLines`, `kBodyFDNMaxDelay` (`body_resonance.h:45-54`); `kNumDiffusionStages`, `kAllpassCoeff`,
   `kBaseDelayMs`, `kMaxModDepthMs`, `kDiffusionSmoothingMs`, `kDelayRatiosL`, `kStereoOffset`
   (`diffusion_network.h:36-56`); `kMinCombCoeff`, `kMaxCombCoeff` (`comb_filter.h:32`, `:35`);
   `kMaterialPresets` (`modal_resonator.h:99`).

3. Establish a **green baseline** before touching shared DSP, so a later failure is attributable:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release \
     --target dsp_processors_tests dsp_systems_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests
   for t in dsp_processors_tests dsp_systems_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests; do
     build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
   done
   node tools/check-portability.js
   node tools/lint-layers.js
   node tools/lint-arch-guarded-includes.js
   node tools/lint-float-bit-goldens.js
   node tools/lint-simd-aligned-loadstore.js
   node tools/lint-odr.js
   node tools/lint-allocation-operator-overrides.js
   ```
   Capture the output to a log file on the **first** run; do not re-run a suite just to look at its output.

**Verify:** every suite reports "All tests passed"; every lint clean. Any pre-existing failure is **owned**,
not dismissed — fix it or escalate before T002.

---

## Group B — Test-target registration (single shared-file task)

### T002 — Register the five new test TUs in `dsp/tests/CMakeLists.txt`

**Files created (stubs, so the build configures and the failing-test tasks that follow have a home):**
- `dsp/tests/unit/systems/continuous_body_test.cpp`
- `dsp/tests/unit/systems/continuous_body_spectral_test.cpp`
- `dsp/tests/unit/systems/continuous_body_perf_test.cpp`
- `dsp/tests/unit/processors/waveguide_string_retune_test.cpp`
- `dsp/tests/unit/processors/diffusion_network_zeromod_test.cpp`

**File edited:** `dsp/tests/CMakeLists.txt` (**this is the only task in the whole phase that edits it**).

**Do:**

1. Create each stub as a compiling, *non-empty* TU with a banner citing the spec/plan and a single
   placeholder case that is expected to be replaced:
   ```cpp
   // ==============================================================================
   // Layer 3: System Tests - ContinuousBody   (specs/seraphis-phase4-continuous-body)
   // ==============================================================================
   // Constitution Principle XII: Test-First Development.
   #include <catch2/catch_test_macros.hpp>
   #include <catch2/catch_approx.hpp>
   using Catch::Approx;
   ```
   Do **not** add a `main()` — `dsp_test_main.cpp` supplies it (and the FTZ/DAZ setup) for both targets.
   Do **not** include `tests/test_helpers/allocation_operator_overrides.h` in
   `continuous_body_test.cpp` (SC-006 requires the overrides to live in a different TU).

2. Add to the `add_executable(dsp_systems_tests …)` source list (the systems list ends with
   `unit/systems/spectral_morph_perf_test.cpp`; sources are listed explicitly, **never globbed**):
   ```
   unit/systems/continuous_body_test.cpp
   unit/systems/continuous_body_spectral_test.cpp
   unit/systems/continuous_body_perf_test.cpp
   ```

3. Add to the `add_executable(dsp_processors_tests …)` source list (it currently ends with
   `unit/processors/entropy_processor_test.cpp`):
   ```
   unit/processors/waveguide_string_retune_test.cpp
   unit/processors/diffusion_network_zeromod_test.cpp
   ```

4. Add `unit/systems/continuous_body_test.cpp` to the existing
   `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` → `set_source_files_properties(… PROPERTIES
   COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")` block (the one that currently ends at
   `unit/processors/test_modal_bank_frequency.cpp`). **Required:** T014 injects bit-pattern NaN/Inf in that
   TU. The other four TUs do not inject non-finite values and must **not** be added.

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests dsp_processors_tests
```
both link; then confirm the TUs are actually reachable — `dsp_systems_tests.exe "ContinuousBody_*"` must
report *matching cases run*, not "No tests ran". A silently-dropped TU is the failure mode this step exists
to prevent.

---

## Group C — Shared-DSP amendments (parallel: fully disjoint file sets)

> Both tasks amend shared Layer 2 headers consumed **outside** this phase. T003 touches
> `diffusion_network.h` + one new TU; T004 touches `waveguide_string.h` + one new TU. The two file sets do
> not intersect and neither task edits `dsp/tests/CMakeLists.txt` (T002 already did), so they are
> parallel-safe.

### T003 [P] — RA-4: `DiffusionNetwork` zero-modulation fast path

**Files edited:** `dsp/include/krate/dsp/processors/diffusion_network.h` (**one line: `:362` only**).
**Files written:** `dsp/tests/unit/processors/diffusion_network_zeromod_test.cpp`.

**Failing test first** — `dsp/tests/unit/processors/diffusion_network_zeromod_test.cpp`:

`TEST_CASE("DiffusionNetwork_ZeroModIsBitIdentical")`
- Build two `DiffusionNetwork` instances, `prepare(48000.0f, 512)`, `setSize(50)`, `setDensity(100)`,
  `setWidth(100)`, `setModDepth(0.0f)`, `reset()`. Render 4096 samples of a fixed pseudo-random stereo
  signal (seed the generator explicitly, e.g. `Xorshift32{0x4D0D0001u}`) in 512-sample blocks.
- **Clause 1 (the one sanctioned bit-exact comparison in this phase):** capture the `modDepth = 0` render
  as a reference array **before** the header edit, hard-code it as a small checkpoint set (32 evenly-spaced
  sample indices) *or* — preferred, and cheaper — assert bit-identity between the *guarded* and
  *unguarded* paths **inside the same binary** by rendering once at `setModDepth(0.0f)` and once at
  `setModDepth(1e-9f)` **is not** equivalent; instead compare the post-edit `modDepth = 0` render against a
  locally-recomputed reference produced by the same code with the guard forced off via a test-local copy of
  the loop. If that is impractical, use the pre-edit render captured in this same session as an array
  literal and add the justification comment `tools/lint-float-bit-goldens.js` recognises — this is the
  **only** place a bit-exact float comparison is legal, because it compares one binary's two code paths over
  identical inputs, not two toolchains. Assertion: **every sample equal bit-for-bit** (`REQUIRE(a[i] ==
  b[i])` over all 4096).
- **Clause 2 (LFO phase keeps advancing):** render 2048 samples at `setModDepth(0.0f)`, then
  `setModDepth(0.5f)` and render a further 2048. Compare against the same script run on a build where the
  guard is absent, using `compareFingerprints(fingerprintRender(a), fingerprintRender(b))`
  (`tests/test_helpers/render_fingerprint.h:64`, `:101`, default tolerances,
  `kSampleTolerance = 1e-4` at `:49`) → `withinTolerance()` **true**. This is what proves the phase
  accumulator at `:398-401` still advanced while the fast path was taken; if the guard had frozen the
  phase, the post-transition render diverges immediately.
- **Clause 3 (non-vacuity):** a `modDepth = 0.5` render must **differ** from a `modDepth = 0` render
  (`withinTolerance()` **false**), otherwise clauses 1–2 are trivially satisfiable.

Run it, watch it fail (or, for clause 1, watch it be un-runnable) **before** editing the header.

**Implement:** replace **line 362 only** — the `const float lfoValue = std::sin(lfoPhase_ +
stagePhaseOffset);` initialiser. Line `:361` (`const float stagePhaseOffset = static_cast<float>(i) *
(kPi / 4.0f);`) **already exists and must not be re-declared** — redeclaring it is a hard compile error in a
header consumed by Iterum, `shimmer_delay.h` and `freeze_mode.h`. The replacement:

```cpp
                // RA-4 (specs/seraphis-phase4-continuous-body): the sin() below was
                // evaluated per stage per sample UNCONDITIONALLY - 8 transcendentals
                // per sample per instance (~384 k/s at 48 kHz) even at modDepth = 0,
                // which is the default (kDefaultModDepth, :181). The guard is
                // BIT-IDENTICAL, not a behaviour change: lfoValue feeds exactly one
                // expression, `modMs = modDepth * kMaxModDepthMs * lfoValue` (:363),
                // and with modDepth == 0 that product is 0 for every finite lfoValue,
                // leaving delayMsL/R (:369, :373) unchanged bit-for-bit (baseDelayMs
                // = kBaseDelayMs * size is strictly positive after the :344 bypass,
                // so delayMsL/R can never be -0.0f). The LFO phase accumulator
                // (:398-401) is OUTSIDE this loop and still advances, so a later
                // modDepth > 0 resumes on the same phase.
                const float lfoValue = (modDepth > 0.0f)
                                     ? std::sin(lfoPhase_ + stagePhaseOffset)
                                     : 0.0f;
```

Nothing else in the file changes. `modDepth` is already the **smoothed** value read at `:337`, and
`OnePoleSmoother` snaps to target within `kCompletionThreshold = 1e-4` (`smoother.h:55`, `:199-202`), so
`setModDepth(0)` does reach exactly 0 and the guard does engage.

**Verify (this amendment's full consumer set — RA-4 touches shared DSP):**
```bash
"$CMAKE" --build build/windows-x64-release --config Release \
  --target dsp_processors_tests dsp_effects_tests plugin_tests approval_tests
for t in dsp_processors_tests dsp_effects_tests plugin_tests approval_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```
Consumers verified by grep: `dsp/tests/unit/processors/diffusion_network_test.cpp:14`,
`dsp/include/krate/dsp/effects/shimmer_delay.h:32`, `effects/freeze_mode.h:31`,
`plugins/iterum/src/processor/processor.h:27`. **`approval_tests` is Iterum's golden-output suite and is
not optional** — running only `plugin_tests` silently skips golden-output regression coverage.
`node tools/lint-float-bit-goldens.js` must stay clean (the justification comment is required if clause 1
uses a literal reference).

---

### T004 [P] — RA-1: `WaveguideString::retune(float)`

**Files edited:** `dsp/include/krate/dsp/processors/waveguide_string.h` (strictly additive — one new public
method, **no existing member changes**).
**Files written:** `dsp/tests/unit/processors/waveguide_string_retune_test.cpp`.

**Risk this task carries (RA-1, non-optional):** `waveguide_string.h` is shared DSP. Consumers verified by
grep: `dsp/tests/unit/processors/{waveguide_string_test,waveguide_string_dc_blocker_test,bow_waveguide_coupling_test}.cpp`;
`plugins/innexus/src/processor/innexus_voice.h:24` (+2 Innexus tests);
**`plugins/membrum/src/dsp/drum_voice.h:41` and `plugins/membrum/src/dsp/bodies/string_body.h:22`**;
`tools/membrum_preset_generator.cpp`. Membrum is the heaviest consumer in the repo and **`membrum_tests`
must be run**.

**Failing test first** — `dsp/tests/unit/processors/waveguide_string_retune_test.cpp`:

`TEST_CASE("WaveguideString_RetunePitchAccuracy")` (SC-009c)
- `prepare(48000.0)`, `setStiffness(0.15f)`, `setPickPosition(0.22f)`, `setDecay(8.0f)`,
  `setBrightness(0.30f)`, then `noteOn(220.0f, 1.0f)` (a real pluck — this case needs energy in the loop).
- Render 0.5 s to let the onset settle, then `retune(f)` for `f ∈ {110, 155.56, 220, 311.13, 440}` Hz
  (±12 semitones about 220). After each retune render 1.0 s and estimate the fundamental from the **final
  0.5 s** with an 8192-point Hann-windowed FFT: highest-magnitude peak below `1.5 · f`, refined by 3-point
  parabolic interpolation on the **log** magnitudes.
- Assert: `|1200 · log2(detected / f)| <= 5.0` cents at every point. 5 cents is the bound FR-081's frozen
  dispersion cascade permits — do not widen it.
- Assert additionally: the RMS of each post-retune second is **> 0**, and the RMS measured across the
  retune **boundary** — the 0.1 s immediately after against the 0.1 s immediately before — is within a
  factor of 4 (the retune must not re-excite and must not kill the loop). *Amended 2026-07-27 (build
  stage):* originally "within a factor of 4 of the pre-retune **second**". Over a whole second the string's
  own T60 decay contributes a ratio of 0.40 unaided (measured on an un-retuned control), so that form both
  tolerated ~10x re-excitation and failed legitimately when shortening the loop orphaned a third of the
  ring (311 → 440 Hz, D 139 → 94, measured 0.231). Across the boundary natural decay contributes 0.99 and
  the measured ratios span 0.587…1.008 over every probed configuration.

`TEST_CASE("WaveguideString_RetuneIsInert")` (SC-014, RA-1 clause)
- Script: `prepare(48000.0)` → configure → `noteOn(220, 1.0)` → render 2 s in 512-blocks, **never calling
  `retune()`**. Compare `fingerprintRender(render)` against a pre-amendment reference.
- Obtain the reference by **both** routes the plan recommends (OQ-B, options (a)+(b)):
  **(a)** capture a `RenderFingerprint` on the pre-amendment header during this task and check its four
  aggregate metrics + 32 checkpoints into the test as a reference struct — this is *not* a bit-exact golden,
  it is exactly what `render_fingerprint.h` exists for. `compareFingerprints(...).withinTolerance()` must be
  **true** at default tolerances.
  **(b)** assert containment structurally: no existing member's body changed (a diff of the header must show
  only an insertion), and the unedited existing consumer suites are the real regression evidence.

**Implement:** insert immediately after `noteOn` (ends `:448`), in the public "Note Lifecycle" section, so
the delay budget sits next to the one it mirrors:

```cpp
/// Retune the loop to a new fundamental WITHOUT clearing state or re-exciting.
/// Recomputes bridgeDelayFloat_ with the SAME budget expression noteOn() uses
/// (D = period - 1 - 0.55*dLoss - 0.96*dDisp, :321) and the same clamp (:325).
/// Deliberately does NOT touch nutSideDelay_, bridgeSideDelay_, dcBlocker_,
/// dispersionFilters_, lossState_, the energy followers, frozenStiffness_ or
/// frozenPickPosition_ - the loop keeps ringing through the retune.
/// The frozen dispersion cascade is NOT reconfigured (FR-081); the cost is a
/// small pitch error bounded to +/-5 cents over +/-12 semitones (SC-009c).
/// Inert unless called (FR-084). Added by specs/seraphis-phase4-continuous-body.
void retune(float f0) noexcept
{
    if (!prepared_ || f0 < kMinFrequency)          // FR-082, same guard as noteOn :275-276
        return;
    const float sr = static_cast<float>(sampleRate_);
    f0 = std::clamp(f0, kMinFrequency, sr * 0.45f);            // same clamp as setFrequency :136-137
    const float S      = brightness_ * 0.5f;                   // stored value, as noteOn uses at :303
    const float period = sr / f0;
    const float dLoss  = computeLossPhaseDelay(f0, sr, S);     // static, :486
    const float dDisp  = computeDispersionPhaseDelay(f0, sr);  // non-static const member, :595; FROZEN cascade
    const float D      = period - 1.0f - 0.55f * dLoss - 0.96f * dDisp;   // :321
    const float maxD   = static_cast<float>(bridgeSideDelay_.maxDelaySamples());
    bridgeDelayFloat_  = std::clamp(D, static_cast<float>(kMinDelaySamples), maxD);   // :325
    const auto bridgeN = static_cast<size_t>(std::round(bridgeDelayFloat_));
    bridgeDelaySamples_ = std::clamp(bridgeN, kMinDelaySamples, bridgeSideDelay_.maxDelaySamples());
    frequency_ = f0;
    frequencySmoother_.setTarget(std::log2(f0));   // FR-083: setTarget, NOT snapTo (noteOn snaps at :288)
}
```

Do **not** call `configureDispersionFilters` (FR-081). Do **not** touch `totalLoopDelay_` or any `debug*`
field other than `bridgeDelaySamples_`.

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release \
  --target dsp_processors_tests innexus_tests membrum_tests
for t in dsp_processors_tests innexus_tests membrum_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```
All three **unedited**. `membrum_tests` is mandatory.

---

## Group D — `ContinuousBody` data model

### T005 — Types, class-scoped constants, the five profiles, the two ratio tables

**Files written:** `dsp/include/krate/dsp/systems/continuous_body.h` (new).
**Files edited:** `dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing test first** — `TEST_CASE("ContinuousBody_MaterialTablesAreWellFormed")`:

- **Ratio tables strictly increasing** — for `k = 1 … 31`: `REQUIRE(kGlassRatios[k] > kGlassRatios[k-1])`
  and the same for `kPlateRatios`. (This is what makes FR-043's *prefix* truncation exact.)
- **Glass inharmonicity anchor** — mean of `|kGlassRatios[k] - (k+1)|` over `k = 0…7` equals
  **8.1908** within `Approx(8.1908).margin(0.001)`. This is the number SC-003(c) is derived from.
- **Metal Plate inharmonicity anchor** — the same mean over `kPlateRatios[0…7]` equals **0.9880** within
  `Approx(0.9880).margin(0.001)`.
- **Amplitude normalisation sums** (untruncated 32, `a_k = (k+1)^(-α)`): Glass α = 1.0 → **4.058495**,
  MetalPlate α = 0.7 → **6.693735**, Ice α = 1.3 → **2.758925**, each `Approx(...).epsilon(1e-5)`.
- **No entry falls under the bank's amplitude cull** — the worst case is Ice at N = 32:
  `32^(-1.3)/2.758925 = 4.01e-3`, `REQUIRE(worst > 1.0e-4f)` (`kAmplitudeThresholdLinear`,
  `modal_resonator_bank.h:568`, `:710`).
- **Profile table matches FR-011a exactly** — for each of the five materials assert `engine`,
  `referenceHz` (660 / 196 / 330 / 110 / 880), `t60AtMaxResonanceSec` (13.8 / 8.0 / 23.0 / 2.5 / 11.5),
  `amplitudeExponent` (1.0 / — / 0.7 / — / 1.3), `stretch` (0.0 / — / 0.15 / — / 0.5), `scatter`
  (0.0 / — / 0.10 / — / 0.8), `damping.b1` (0.50 / — / 0.30 / — / 0.60), `damping.b3`
  (5.0e-8 / — / 1.0e-9 / — / 3.0e-8), and that Glass and Ice **share** `kGlassRatios` while Strings and
  Chamber have `ratios == nullptr`.
- **Derived T60 table (FR-036), computed in the test, not hard-coded from prose** — with
  `scale(r) = 40^(1-r)` and modal `T60 = 6.91 / (b1 · scale)`, at `r = 0.8` (`scale = 2.0913`) require
  MetalPlate **11.0**, Glass **6.61**, Ice **5.51**, Strings **3.83**, Chamber **1.20** s each within 1 %,
  and require the ordering **strict**: `MetalPlate > Glass > Ice > Strings > Chamber`. At `r = 0`:
  0.576 / 0.345 / 0.288 / 0.200 / 0.0625 s. Require `b1_eff` stays inside `[kMinB1, kMaxB1] = [0.23, 30.0]`
  everywhere in the table (it spans 0.30 … 24.0), so **no clamp binds and the ordering is strict at every
  `r`**.

**Implement** — create the header, `namespace Krate { namespace DSP {`, header-only, Layer 3.

*Includes (exactly this set; FR-003 + plan D-7):*
`core/crossfade_utils.h`, `core/db_utils.h`, `core/math_constants.h`, `core/random.h`,
`primitives/dc_blocker.h`, `primitives/delay_line.h`, `primitives/one_pole.h`, `primitives/smoother.h`,
`processors/diffusion_network.h`, `processors/envelope_follower.h`, `processors/modal_resonator_bank.h`,
`processors/waveguide_string.h`, `systems/timevar_comb_bank.h`, plus `<algorithm> <array> <cmath>
<cstddef> <cstdint> <cstring>`.
**`core/dsp_utils.h` is deliberately absent** — nothing here calls `softClip` (FR-037's guard is a
`std::clamp`) and a header-only Layer 3 file pulling it rebuilds a large part of the tree.
**`core/db_utils.h` is deliberately present** — `detail::flushDenormal` (`db_utils.h:168`) is mandated on
the cloud feedback write (T013, risk R-6) and must not resolve only transitively through `smoother.h:28`.
Note `flushDenormal` returns NaN/Inf **unchanged**: it is a denormal guard, never a finiteness guard.
The same-layer include of `systems/timevar_comb_bank.h` is legal — `tools/lint-layers.js:74` flags only
strictly-upward includes; in-tree precedent `systems/poly_synth_engine.h:40-41`.

*Types (class-scoped):* `enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber,
Ice }` — the name `Material` is **forbidden** (`modal_resonator.h:81`); `enum class Engine : std::uint8_t
{ Modal = 0, Waveguide, Comb }`; `struct MaterialProfile { Engine engine; const float* ratios; int
defaultModeCount; float amplitudeExponent; ModalResonatorBank::DampingLaw damping; float stretch; float
scatter; float referenceHz; float t60AtMaxResonanceSec; float hfDampingParam; }`.
`kNumMaterials = 5`, `kNumEngines = 3`.

*Constants:* the complete FR-009 range/default/smoothing set plus the law, drive, dirty-gate, output-safety,
cloud and determinism constants — reproduce plan §4.2 verbatim, **all `static constexpr` inside the class**.
`enum class BodyMaterial` must be declared **above** `kDefaultMaterial`. Include the three `static_assert`s:
```cpp
static_assert(kControlChunkSamples == 64, "A-5: the Phase 7 shared control clock");
static_assert(kMinB1 > 1.0f / 5.0f,
              "FR-035: the component floor must sit ABOVE the bank's own b1 floor "
              "(modal_resonator_bank.h:685) so the bank's guard is never the thing that binds");
static_assert(kMaxFollowerInput * kMaxFollowerInput < 1.0e30f,
              "FR-034: EnvelopeFollower::processRMS squares in float (envelope_follower.h:313); "
              "the clamped input must not be able to overflow, or the follower latches forever");
```

*Ratio tables:* `public: static constexpr std::array<float, kModeCountCeiling> kGlassRatios` and
`kPlateRatios`, valued exactly as plan §5.3 pins them (Glass from `f_n ∝ n(n²-1)/√(n²+1)` normalised at
`n = 2`: `1.0000, 2.8284, 5.4233, 8.7706, 12.8663, 17.7088, 23.2974, 29.6319, …, 405.2876`; Metal Plate =
Rossing's published first eight `1.0000, 1.7300, 2.3280, 4.0610, 5.9800, 6.7100, 9.0110, 11.2000` then a
constant-modal-density linear continuation of slope **1.7309** anchored at `k = 8`, giving `12.9309,
14.6618, …, 52.7416`). `constexpr` static data members are implicitly inline in C++17+ — no out-of-line
definition, no ODR hazard. **Do not** substitute `tools/gen-plate-chladni.js`'s table (P = 1.7, κ = 0.11):
it models a different object and contradicts FR-012's published first eight.

*Profiles:* a `static constexpr std::array<MaterialProfile, kNumMaterials>` valued per plan §5.2, with
**explicit `f` suffixes on every float and designated initialisers** (Clang errors on narrowing in brace
init). Strings additionally carries `stiffness = 0.15f`, `pickPosition = 0.22f`; Chamber carries
`spread = 0.45f`, `numCombs = 6`. **No `stereoSpread` field** — plan D-12: `stereoSpread_` is read only by
`recalculatePanPositions` (`timevar_comb_bank.h:763`) → pan gains read only at `:715-716` inside
`processStereo`, and this component drives the comb through the **mono** `process(float)` (`:593-651`), so
the field would be provably inert.

**Verify:** `dsp_systems_tests` builds with zero warnings;
`dsp_systems_tests.exe "ContinuousBody_MaterialTablesAreWellFormed" 2>&1 | tail -5` green.
`node tools/lint-layers.js` and `node tools/lint-arch-guarded-includes.js` clean.

---

## Group E — Lifecycle, control grid, setters, introspection

### T006 — `prepare`/`reset`/`processStereoBlock` walker, the uniform setter shape, FR-007 surface

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

At the end of this task the component is a **pass-through with a correct control grid**: no engine is
advanced yet, `processStereoBlock` copies the mono-summed input to both outputs. That is deliberate — the
three tests below exercise the walker and the control surface and nothing else, and they are the ones that
catch the control-grid carry and the FR-006 substitution.

**Failing tests first** — in `continuous_body_test.cpp`:

`TEST_CASE("ContinuousBody_ControlSurfaceDefaults")` (FR-006 / FR-009)
- **(i) freshly-prepared state, asserted as one line:** after `prepare(48000.0)` and no setter call —
  `getMaterial() == BodyMaterial::Glass`; `getBodyFrequencyHz() == Approx(220.0f).epsilon(1e-4)` (keyTracking
  defaults to 1.0, noteHz to 220); `getEngineT60Sec() == Approx(4.57f).epsilon(0.05)`
  (Glass `b1_eff = 0.50 · 40^0.3 = 1.5122` → `6.91/1.5122 = 4.57` s); `getCloudLoopSeconds()` ≈ **0.0939** s
  (`37 ms + 3.2 ms · 1.0 · 17.777 = 93.9 ms`) within 1 %; `getCloudFeedbackGain()` = the value FR-052 gives
  at `cloudDecaySec = 4.0` and `loopSeconds_L = 0.0939`, i.e. `10^(-3·0.0939/4.0) = 0.85246`, within 1 %;
  `isCrossfading() == false`; `getCrossfadePosition() == 0.0f`; `stateFinite() == true`;
  `getClampEngagementCount() == 0`; `getEngineSampleCount(e) == 0` for all three engines.
  Defaults with no accessor (damping 0.0, drive 1.0, mix 1.0, cloudMix 0.25, cloudSize 1.0, cloudDamping
  0.3, width 1.0, AGC on, bypass off, seed 1) are asserted through rendered behaviour, not invented
  accessors — **FR-007's list is exhaustive and no test may assert on a quantity absent from it.**
- **(ii) non-finite substitution** — for **every** float setter (`setResonance`, `setDamping`,
  `setKeyTracking`, `setNoteFrequencyHz`, `setDrive`, `setMix`, `setCloudMix`, `setCloudDecaySec`,
  `setCloudSize`, `setCloudDamping`, `setWidth`), feed a bit-pattern NaN, a bit-pattern `+Inf` and a
  bit-pattern `-Inf` built through a `volatile` sink (never `std::numeric_limits`), and require the
  observable lands on the **FR-009 Default**, not on 0 and not on `OnePoleSmoother`'s own NaN→0 /
  Inf→±1e10 fallbacks (`smoother.h:170-181`). Example: `setNoteFrequencyHz(nanBits)` then a settled render
  → `getBodyFrequencyHz() == Approx(220.0f)`. `setResonance(nanBits)` → `getEngineT60Sec()` back at the
  0.7 value (4.57 s for Glass), **not** the `r = 0` value (0.345 s).
- **(iii) range clamps at both ends** with ordinary out-of-range finite arguments:
  `setNoteFrequencyHz(5.0f)` → `f_body` from 20 Hz; `setNoteFrequencyHz(20000.0f)` → from 8000 Hz;
  `setDrive(-1.0f)` → 0; `setDrive(99.0f)` → 4; `setCloudDecaySec(0.01f)` → 0.1; `setCloudDecaySec(120.0f)`
  → 30; and the `[0,1]` setters at −1 and +2.

`TEST_CASE("ContinuousBody_BlockSizeInvariance")` — **sub-case (α) only in this task** (sub-case β lands in
T007, which is where `updateModes` first exists).
- One 1024-sample render of a fixed pseudo-random stereo signal at 48 kHz from the FR-009 prepared state,
  issued six ways: **1×1024, 2×512, 16×64, 1023+1, 100+100+…+24, 7×146+2**.
- All six agree to `kSampleTolerance = 1.0e-4` (`render_fingerprint.h:49`) — **at the same tolerance for the
  non-multiple partitions**. The first three are exact multiples of the 64-sample control grid and cannot
  fail on a grid-alignment bug; the last three are what FR-005a's persistent counter and carried `Σx²` exist
  for. If an implementation restarts the control phase per call, or fires a control step on a sub-64 tail,
  the last three diverge and this case catches it.
- Also assert `getInputRms()` after a 36+28-split control chunk equals `getInputRms()` after an unsplit 64
  of the same samples, to `Approx(...).epsilon(1e-5)` — the carried-accumulator clause stated directly.

`TEST_CASE("ContinuousBody_NoAllocInProcess")` (SC-006)
- **Liveness clause FIRST and mandatory:** inside `AllocationDetector::instance().startTracking()` /
  `stopTracking()` (`tests/test_helpers/allocation_detector.h:26`), deliberately `new int[16]` and read it
  through a `volatile int*` (defeats N3664 elision), then `REQUIRE(count >= 1)`. Without this the zero
  clause proves nothing — the operator overrides live in a different TU. **This TU must not include
  `allocation_operator_overrides.h`.**
- Tracked window: 200 × 512-sample blocks with **every** setter stepped once per block, two material
  changes and a glide. `REQUIRE(count == 0)`.
- **No Catch2 macro, no stream formatting, no vector growth inside the tracked window** — capture, then
  assert after `stopTracking()`.

**Implement:**

*`prepare(double sampleRate)`* — the only method permitted to allocate, in this order (plan §13):
`sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0`; `modal_[0..1].prepare(sr)`,
`waveguide_.prepare(sr)`, `comb_.prepare(sr, 50.0f)`; cloud —
`delayL.prepare(sr, kCloudLoopMsL*1e-3f)`, `delayR.prepare(sr, kCloudLoopMsR*1e-3f)` **sized for their own
loop only** (the `DiffusionNetwork` allocates its own ≈16.9 ms per-stage buffers at `:202-205`; its
~57–64 ms of *throughput* delay is distributed across those stages — **the two figures are different
quantities and must never be summed into one buffer size**), then
`diffusion.prepare(float(sr), kControlChunkSamples)` → `setSize/setDensity/setWidth/setModDepth(0)` →
**`diffusion.reset()` again** (its `prepare` leaves `size_` at 50 % and its internal 10 ms smoothers would
otherwise glide for the first 480 samples of every render), `dampL/R.prepare(sr)`,
`dcL/R.prepare(sr, kCloudDcCutoffHz)`; `rmsFollower_.prepare(sr / double(kControlChunkSamples), 1)` +
`setMode(DetectionMode::RMS)` + `setAttackTime(kRmsAttackMs)` + `setReleaseTime(kRmsReleaseMs)` — **the
control rate, not the audio rate**; `configure()` every smoother; `loopSamplesL/R =
lround(kCloudLoopMs_ch·1e-3·sampleRate_)` — **the delay-line portion only, never derived from
`loopSeconds_ch`** (`DelayLine::read` clamps silently at `delay_line.h:212-218`, so an over-long index is a
wrong loop time with no fault) — then the `maxDelaySamples()` debug assertions, then `loopSecondsL/R`,
`fbL/R`, `cloudChunkCap_ = min(loopSamplesL, loopSamplesR, kControlChunkSamples)`; `reset()`;
`prepared_ = true`. Repeatable. **Parameters are not restored** across a `prepare()`.

*`reset() noexcept`* — clears every engine (`silence()`), delay line, `DiffusionNetwork`, damping filter,
DC blocker, `rmsFollower_.reset()`, `sampleCounter_`, `chunkSumSq_`, `chunkCount_`, `chunkPoisoned_`,
`engineSampleCount_[]`, `clampCount_`; **snaps every smoother to its target**; leaves parameters unchanged;
abandons any crossfade. **And clears every applied-value shadow** (`appliedBodyHz = 0`, `appliedB1 =
appliedB3 = 0`, `appliedT60 = appliedS = 0`, `appliedCombFb[] = {}`, `appliedCombDamp = 0`) so the next
control step's dirty gates fire unconditionally — this is not tidiness, it is the mechanism that rescues the
waveguide from `silence()` (T014, plan §10.1 path 3).

*`processStereoBlock`* — guards in order: any null pointer → **immediate return, no write**;
`numSamples == 0` → no-op and **no control step consumed**; `!prepared_` → write silence and return.
In-place operation is **not supported** and is documented as such. Then the walker (plan §11):
```
done = 0
while done < numSamples:
    toGrid   = kControlChunkSamples - (sampleCounter_ % kControlChunkSamples)
    subChunk = min(numSamples - done, toGrid, cloudChunkCap_)
    for s in [0, subChunk):
        m = 0.5f * (inLeft[done+s] + inRight[done+s])
        if (!isFiniteBits(m)) { chunkPoisoned_ = true; m = 0.0f; }
        if (chunkPoisoned_)  m = 0.0f            // sticky for the rest of the control chunk
        mono[s] = m
        chunkSumSq_ += double(m) * double(m)     // accumulate in DOUBLE
    chunkCount_ += subChunk
    if (chunkPoisoned_) chunkSumSq_ = 0.0        // ASSIGNMENT, never a subtraction
    renderSub(mono, outLeft+done, outRight+done, subChunk)
    sampleCounter_ += subChunk; done += subChunk
    if ((sampleCounter_ % kControlChunkSamples) == 0):
        controlStep(sqrt(chunkSumSq_ / double(chunkCount_)))   // chunkCount_ == 64 here
        chunkSumSq_ = 0.0; chunkCount_ = 0; chunkPoisoned_ = false
```
`sampleCounter_` is a `std::uint64_t` cleared **only** by `prepare()`/`reset()`. **Nothing is buffered: the
component's latency is 0 samples at every block size.** Every scratch buffer is a fixed
`std::array<float, kControlChunkSamples>` **member** — no fixed-size stack buffer may assume a maximum block
size (a 32,768-sample host block is 512 sub-chunks). In this task `renderSub` writes the mono signal to both
outputs and `controlStep` advances only the smoothers and the RMS follower.

*The uniform setter shape (FR-006), with no exceptions:*
```cpp
void setResonance(float v) noexcept {
    if (!isFiniteBits(v)) v = kDefaultResonance;   // SUBSTITUTE FIRST
    resonance_ = std::clamp(v, kMinResonance, kMaxResonance);
    // then: smoother.setTarget(...)   OR   nothing (the three FR-009 exceptions)
}
```
Three points that must not vary: **substitute before clamping** (`std::clamp(NaN, lo, hi)` returns NaN);
`OnePoleSmoother::setTarget` is **not** a substitute for the check (it maps to *its own* fallbacks, not
FR-009's Default column); the three FR-009 exceptions (`setResonance`, `setDamping`, and the
ramped/crossfaded `setMaterial`/`setResonatorBypass`) skip only the *smoother* step — they still substitute
and clamp.
```cpp
[[nodiscard]] static bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0; std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}
```

*FR-007 introspection surface* — public, non-`#ifdef`, **exactly** these and no more: `getMaterial`,
`getActiveModeCount`, `getModeFrequencyHz`, `getBodyFrequencyHz`, `getEngineT60Sec`, `getDriveGain`,
`getInputRms`, `getSteadyStateGainBound`, `isCrossfading`, `getCrossfadePosition`, `getCloudFeedbackGain`,
`getCloudLoopSeconds`, `getClampEngagementCount`, `getEngineSampleCount`, `stateFinite`. Stubs that return
the correct default are acceptable in this task; each is filled in by the task that owns its quantity.
`stateFinite()` is composed from **private** `engineStateFinite(int)`, `cloudStateFinite()`,
`controlStateFinite()` — the predicates stay private so FR-007's list is unchanged.

**Verify:** `dsp_systems_tests` zero warnings;
`dsp_systems_tests.exe "ContinuousBody_ControlSurfaceDefaults,ContinuousBody_BlockSizeInvariance,ContinuousBody_NoAllocInProcess"`
green.

---

## Group F — Modal engine path

### T007 — Modal slot configuration, mode-count truncation, key tracking, dirty gates

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing tests first:**

`TEST_CASE("ContinuousBody_ModeCountTruncation")` (FR-043 — **no criterion covered this and
`getActiveModeCount()` was asserted nowhere**)
- (i) `getActiveModeCount()` equals the computed table for `f_body ∈ {55, 110, 220, 440, 880}` Hz ×
  `{44100, 48000, 96000}` Hz, per Glass / Metal Plate / Ice. Pinned rows (48 kHz):
  55 Hz → **22 / 32 / 19**; 110 Hz → **15 / 32 / 14**; 220 Hz → **11 / 29 / 10**; 440 Hz → **7 / 16 / 7**;
  880 Hz → **5 / 9 / 4**. At 44.1 kHz / 220 Hz → **10 / 27 / 10**. At 96 kHz / 220 Hz → **15 / 32 / 14**.
- (ii) the count is **unchanged** across a retune that stays inside the one-octave headroom window.
- (iii) the count **never decreases** during an upward glide that leaves the window — sample
  `getActiveModeCount()` every control chunk over a 110 → 880 Hz glide and require monotone
  non-decreasing (it may increase). A mid-ring decrease would truncate `numModes_` and drop ringing state
  instantaneously — a click.
- (iv) `getModeFrequencyHz(k) == 0.0f` for every `k >= getActiveModeCount()` (FR-007's stated contract).

`TEST_CASE("ContinuousBody_KeyTrackingLaw")` (SC-009 a/b)
- **(a) law** — `getBodyFrequencyHz()` matches `referenceHz · (noteHz/referenceHz)^keyTracking` within
  **0.1 cent** across `keyTracking ∈ {0, 0.25, 0.5, 0.75, 1}` × `noteHz ∈ {55, 110, 220, 440, 880, 1760}`,
  **modal materials only** (Chamber saturates at the comb bank's 1000 Hz clamp, `timevar_comb_bank.h:521`,
  documented behaviour). Let the pitch smoothers settle (≥ 5 × `kPitchSmoothMs`) before reading.
- **(b) modal realisation** — the detected fundamental of the rendered output is within **5 cents** of
  `f_body` for Glass / MetalPlate / Ice, using the named estimator: highest-magnitude peak below
  `1.5 · f_body` in an 8192-point Hann-windowed FFT, refined by 3-point parabolic interpolation on log
  magnitudes. **Autocorrelation, cepstrum and YIN are excluded by name** — these spectra have no harmonic
  series and those estimators do not return `f_body`.

`TEST_CASE("ContinuousBody_BlockSizeInvariance")` — **add sub-case (β)**, which cannot be written before
`updateModes` exists and which is the **R-12/OQ-E gate**:
- The same six partitions as (α), but **with coefficients in flight**: a key-track glide plus a `setDamping`
  step running throughout, so every control step calls `updateModes`. Same `kSampleTolerance = 1e-4`.
- (β) is **mandatory**, because in (α) the targets never move — `epsilon_/radius_/inputGain_` equal their
  targets from the start (`reset()` memcpies them at `modal_resonator_bank.h:199-201`) and
  `smoothCoefficients` is a no-op for the whole render, making (α) **vacuous** for the block-rate-smoothing
  dependence. `smoothCoefficients()` runs **once per `processBlock` call** (`:357`), so 1024 samples is 16
  calls for 1×1024 and 17 for 1023+1.
- **If (β) exceeds 1e-4, do not widen the tolerance.** Escalate via OQ-E: either drive the modal bank on a
  partition-independent cadence (accumulate into a 64-sample scratch and call `processBlock` exactly once per
  *complete* control chunk, accepting and documenting the resulting latency for the modal path), or record a
  **measured** deviation with the actual maximum divergence.

**Implement:**

- **Modal engine advance** — `modal_[i].processBlock(driveBuf, engBuf, int(subChunk))`. **Never**
  `ModalResonatorBank::process()`: it calls `smoothCoefficients()` per sample (`:345-349`, `:801-809`) and
  the *scalar* `processSampleCore` (`:814`), bypassing the SIMD kernel entirely; only `processBlock` reaches
  `processModalBankSampleSIMD` (`:362-364`) and smooths once (`:357`). Never read
  `getControlEnergy()`/`getPerceptualEnergy()` — they are updated only inside `process()` (`:494-500`) and
  are stale on this path.
- **Material assignment (modal)** — pick the modal bank not in use; compute `f_body`, the truncated mode
  count, the seeded micro-detune slot (T014 fills the seed in; use `j_k = 0` here), the amplitudes; call
  `setModes(freqs, amps, count, DampingLaw{b1_eff, b3_eff}, stretch, scatter)`; then
  `setOutputSoftClipThreshold(1.0f)` and `setOutputGain(1.0f / getInputGainSum())`. The historical 0.707
  clipper (`:571`) would sit pinned for the entire ring of a sustained input and mask damping modulation.
- **Body pitch law (FR-040)** — `f_body = exp2f(log2f(referenceHz) + keyTracking · (log2f(noteHz) −
  log2f(referenceHz)))`, both `keyTracking` and `log2(noteHz)` smoothed at `kPitchSmoothMs = 20` **in the
  log-frequency domain**; `advanceSamples(chunk)` gives the per-chunk value in O(1). `f_body` clamped to
  `[20, 8000]`.
- **State-preserving retune (FR-041)** — modal: `updateModes(...)` (`:264-275`). **Never `setModes`** on a
  retune (`:238-239` memsets state) and **never `updateDampingLaw`** (`:280-294` skips `!active_[k]` modes
  and would permanently miss a mode `flushSilentModes` culled during a quiet passage — risk R-8; leave a
  code comment at the call site saying so).
- **Dirty gates (FR-042 / FR-042a)** — one `updateModes` call per control step **at most**, gated by an OR:
  ```
  pitchDirty   = |1200·log2(f_body / appliedBodyHz)| > kRetuneEpsilonCents            // 0.5 cents
  dampingDirty = |b1_new - appliedB1| > kDampingEpsilonRel · max(appliedB1, kMinB1)
              || |b3_new - appliedB3| > kDampingEpsilonRel · max(appliedB3, kB3Floor)
  ```
  Relative on damping because `b1`/`b3` differ by eight orders of magnitude between materials.
  `computeModeCoefficients` runs a `sqrt`, two `sin` and an `exp` per mode (`:726`, `:729`, `:743`, `:746`)
  — ~128 transcendentals for 32 modes — so this gate is the single largest CPU lever.
- **Mode count (FR-043)** — at material assignment,
  `N = min(defaultModeCount, #{leading k : ratio[k]·(2·f_body)·√(1+B(k+1)²)·(1+C·sin(k·kScatterD)) <
  kBankNyquistGuard·fs})`, i.e. **one octave of glide headroom**, applying the bank's own stretch and
  scatter warps so the prefix boundary matches what the bank computes. On retune **inside** the window the
  count is unchanged (the bank's own cull at `:732-738` handles crossings). On retune **outside** the
  window the count may only ever **increase** mid-ring; a decrease is deferred to the next material
  assignment.
- **Document in the header** that `ModalResonatorBank::applyTransientEmphasis` (`:359`, `:879-895`) is an
  un-disableable time-varying input gain on the `processBlock` path (≈ +0.06 dB residual ripple at unit
  amplitude for a steady sine, ≈ +0.55 dB across a 20 dB step) — risk R-5. If a later drive-law test
  misses by a fraction of a dB, the diagnosis is here.

**Verify:** `dsp_systems_tests.exe "ContinuousBody_ModeCountTruncation,ContinuousBody_KeyTrackingLaw,ContinuousBody_BlockSizeInvariance"`
green, zero warnings.

---

## Group G — Continuous-excitation adapter (the new DSP work)

### T008 — `Ĝ`, the two drives, log10 smoothing, AGC, damping floors, Resonance/Damping laws

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_spectral_test.cpp`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing tests first — SC-015 before SC-007**, because SC-015 validates the central maths in isolation
while SC-007 measures the *post-compensation* level and would also pass if `Ĝ` were systematically wrong in
a way the AGC absorbed.

**"Steady-state peak", defined once and used by both cases:** render
`max(5.0 s, 3 × getEngineT60Sec())` at the configuration under test, then take the **mean of the per-block
peak magnitude over the final 1.0 s**. Self-sizing: the `r = 0.2` case runs 5 s, the `r = 1.0` Metal Plate
case runs 69 s.

`TEST_CASE("ContinuousBody_GainBoundValidAndTight")` (SC-015, spectral TU)
- Per modal material × `resonance ∈ {0.2, 0.5, 0.8, 1.0}`: drive a unit sine at each of the first 8 mode
  frequencies (`getModeFrequencyHz(k)`), `cloudMix = 0`. Compute
  `measuredGain = steadyStatePeak / (getDriveGain() × inputAmplitude)`, where `getDriveGain()` is the
  sounding slot's smoothed **engine** drive.
- **Validity:** `measuredGain <= getSteadyStateGainBound()` at every one of the 8 modes.
- **Tightness:** at mode 1, `measuredGain >= 0.1 × getSteadyStateGainBound()`. The rejected flat-numerator
  form gives `measuredGain/Ĝ ≈ 0.029` and fails this; the correct form predicts a ratio near
  `g_1/Σg_k = a_1` (**0.246** Glass, **0.149** Plate, **0.362** Ice) times an O(1) factor.

`TEST_CASE("ContinuousBody_DriveNormalization")` (SC-007, main TU)
Sine at `getBodyFrequencyHz()`, `cloudMix = 0`, per material:
- **(i) level invariance across resonance** — across `resonance ∈ {0.2, 0.5, 0.8, 1.0}` the steady-state
  peak varies by no more than **±3 dB about its own mean across that grid**. A 23 s body and a 0.35 s body
  must be the same loudness.
- **(ii) absolute level sanity** — that mean lies within **−20 dB … +3 dB** of `kTargetPeak = 1.0` for every
  material. Loose enough for the single-mode-versus-sum gap, tight enough that a 31 dB error (the one the
  rejected `Ĝ` form produces) fails.
- **(iii) the compensation is a gain, not a compressor** — with `setInputAgcEnabled(false)`, a 20 dB input
  **decrease** (amplitude 1.0 → 0.1) gives **−20 ± 1 dB** output change. **The direction is pinned and an
  upward leg from unity is not usable**: with the AGC off the drive is a fixed gain, so +20 dB puts Glass at
  ≈ 2.46 into `applyOutputStage` at threshold 1.0 where `softClip(2.46) ≈ 0.998` — a measured **≈ +12 dB**
  on a *fully correct* implementation (plan D-11). If an upward leg is wanted it must run from a −20 dB
  reference (0.1 → 1.0).
- **(iv) the AGC works** — with `setInputAgcEnabled(true)`, a 20 dB input drop recovers to within **6 dB**
  within **1 s**, and `getInputRms()` tracks the true windowed RMS within **±10 %**.

**Implement:**

- **`Ĝ` — modal, derived from the recursion the bank actually runs.** `ModalResonatorBank` is the coupled
  (magic-circle) form (`:853-859`): `s[n] = R(s+εc) + g·u; c[n] = R(c − ε·s[n]); y = s[n]`, whose transfer
  function is `H(z) = g(1 − Rz⁻¹)/[1 − R(2 − Rε²)z⁻¹ + R²z⁻²]` — a **zero at `z = R`**. Evaluate `|H|` at
  the pole angle with no transcendentals:
  ```
  cθ  = 1 − R·ε²/2                      // pole-angle cosine; NEVER assume θ = 2πf/fs
  c2θ = 2·cθ² − 1
  Ĝ_k = g_k · √(1 − 2R·cθ + R²) / [(1 − R)·√(1 − 2R·c2θ + R²)]
  Ĝ   = Σ_k Ĝ_k                          // all-modes-in-phase worst case → a true upper bound
  ```
  Two `sqrt` per mode, no transcendentals, gated by the **same** dirty flag as `updateModes` (do **not**
  compute `Ĝ` unconditionally). The flat-numerator form over-estimates by ≈ **35× (31 dB)** at 220 Hz /
  48 kHz — that is what SC-015's tightness clause rejects.
- **`Ĝ` — waveguide:** `Ĝ = 1 / max(1 − gTotal, kGainBoundEps)` with
  `gTotal = rho·√((1−S)² + 2S(1−S)cos ω₀ + S²)`, `rho = 10^(−3/(T60_eff·f0))`
  (`waveguide_string.h:476-481`), `T60_eff` the value **after** the `[0.05, 10.0]` clamp.
  **Comb:** `Ĝ = Σ_n 1/max(1 − fb_n, kGainBoundEps)` over the 6 active combs, `τ_n` from the bank's
  **clamped** delay (`:737-741`, `[1, 50]` ms).
  `Ĝ` is **per slot**: computed at material assignment, recomputed at a control step only when that slot's
  dirty gate fires, **frozen** for the outgoing slot for the rest of a fade.
- **Two drives (FR-033):**
  ```
  engineDrive_slot = clamp(kTargetPeak / Ĝ_slot, kMinDriveGain, kMaxDriveGain) · rmsGain · userDrive
  cloudDrive       = rmsGain · userDrive          // no 1/Ĝ: it has no referent with no resonator in path
  ```
  **Per slot, snapped at assignment** — `driveLog10.snapTo(log10f(max(engineDrive, kMinDriveGain)))`. Legal
  and clickless because the incoming slot is at zero crossfade gain at that instant; *necessary* because `Ĝ`
  spans 3–5 decades between engines and a shared smoother crossing that span over-drives the incoming engine
  by tens of dB. **Smoothed in log10 for every continuous move** (glide, resonance/damping, AGC,
  `setDrive`): `setTarget(log10f(...))`, `advanceSamples(chunk)` (O(1)), read
  `exp10f = std::exp2(x · 3.32192809f)` — **never `std::pow(10, x)`**. Constant dB/ms slope, so a decade and
  a factor of 1.1 both take 50 ms. The gain applied **inside** a chunk is constant (one multiply per sample,
  no per-sample smoother).
- **AGC (FR-034 / FR-034a):** `EnvelopeFollower` in `DetectionMode::RMS`, **prepared at the control rate**
  (already done in T006). Feed it `chunkRms = min(√(chunkSumSq_/64), kMaxFollowerInput)` — the **chunk RMS,
  not one sample of the chunk**; `DetectionMode::RMS` squares its input and square-roots the smoothed
  result, so equal-length chunk RMS values yield the true windowed RMS exactly.
  **The `kMaxFollowerInput = 1e9` clamp is load-bearing, not garnish:** `processRMS` squares **in float**
  (`envelope_follower.h:313`), so a legal finite ±1e38 block overflows to `+Inf` and the branch at `:316-321`
  keeps `squaredEnvelope_` non-finite forever; the denormal flush at `:184-185` passes Inf through
  (`db_utils.h:168`), and only `reset()`/`prepare()` clears it. Without the clamp any `|m| > ~1.8e19`
  permanently pins `stateFinite()` false.
  `rmsGain = clamp(kTargetInputRms / max(rms, kRmsFloor), kMinRmsGain, kMaxRmsGain)`; held at **exactly
  1.0f** when `setInputAgcEnabled(false)` (the follower still tracks so `getInputRms()` stays meaningful).
- **Resonance / Damping (FR-036)** — one law, three engines, `scale(r) = kResonanceScaleAtZero^(1−r)`
  written as `exp2f((1−r)·5.321928f)`, **never `std::pow`**:

  | Engine | Resonance | Damping |
  |---|---|---|
  | Modal | `b1_eff = clamp(b1_material · scale(r), kMinB1, kMaxB1)` | `b3_eff = b3_material · (1 + kDampingB3Scale·d)` |
  | Waveguide | `T60_eff = clamp(t60AtMaxRes / scale(r), 0.05f, 10.0f)` → `setDecay` | `S_eff = S_material + d·(0.45 − S_material)` → **`setBrightness(2·S_eff)`** |
  | Comb | `T60_eff = t60AtMaxRes / scale(r)`; `fb_n = min(10^(−3·τ_n/T60_eff), 0.995f)` | `damping_eff = damping_material + d·(1 − damping_material)` → `setCombDamping(n, ·)` |

  **Correction C-6, written down once so it is not rediscovered as a bug: `WaveguideString::setBrightness`
  DARKENS.** `S = brightness·0.5` (`:168`) feeds the loss filter (`:197`) whose magnitude is flat at
  `S = 0` and has a null at Nyquist at `S = 0.5`. A larger argument = darker.
  `scale(r)` is strictly decreasing in `r` for every material, so **T60 monotonicity is a property of the
  law, not a hope**. `getEngineT60Sec()` returns `6.91/b1_eff` (modal) or `T60_eff` (waveguide, comb).
- **Damping floors (FR-035):** modal `b1_eff >= kMinB1 = 0.23` (T60 = 30.0 s, above the bank's own `1/5`
  floor at `:685` — asserted at compile time in T005); waveguide `T60 ∈ [0.05, 10.0]` s (the component's own
  clamp is `[0.01, 10.0]` at `:144`; FR-036 tops out at 8.0 s so neither binds; `rho < 1` strictly);
  comb `fb_n <= 0.995` (inside the bank's `±0.9999`).
- **Output safety (FR-037):** after the crossfade mix and **before** the decay cloud,
  `mono[s] = std::clamp(mono[s], -kOutputClamp, kOutputClamp)`, incrementing `clampCount_` once per sample
  on which the clamp altered the value (mono only — the resonator core is mono).
  **Read this before treating the counter as evidence of anything:** four of the five materials are bounded
  to ±1.0 **upstream** (modal via `applyOutputStage` → `softClip`, `modal_resonator_bank.h:366`, `:789-796`;
  waveguide via `softClip(junction)`, `waveguide_string.h:181`) and two equal-power gains sum to at most
  √2 ≈ 1.414 — so the counter is **structurally incapable of moving** for them. Only Chamber's comb has no
  output stage. Document this at `kOutputClamp` and at the increment site.

**Verify:** `dsp_systems_tests.exe "ContinuousBody_GainBoundValidAndTight"` then
`"ContinuousBody_DriveNormalization"`, both green, zero warnings.

---

## Group H — Waveguide and comb engines

### T009 — Waveguide + comb slot configuration, engine advance, `getEngineSampleCount`

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing test first** — `TEST_CASE("ContinuousBody_OnlyActiveEnginesAdvance")` (SC-016).
**Functional, not timing-based, and NOT `[.perf]`-tagged**, so every CI leg evaluates it. In this task write
the **no-material-change** and **bypass** clauses; the exact-arithmetic crossfade clause lands in T012.
- Over a 200-block × 512-sample render with **no** material change:
  `getEngineSampleCount(activeEngine) == numSamples` (102,400) and `getEngineSampleCount(e) == 0` for every
  other engine, for each of the five materials.
- With `setResonatorBypass(true)`: **every** engine's count stays flat across the render.
- A timing comparison cannot distinguish "not advanced" from "advanced with zero input" — this is FR-023's
  actual proof; SC-005's per-material spread is only corroboration.

**Implement:**

- **Waveguide assignment (FR-022c)** — `setStiffness(0.15f)`, `setPickPosition(0.22f)`, `setDecay(T60_eff)`,
  `setBrightness(2·S_eff)` **all before** `noteOn(f_body, 0.0f)`, because `noteOn` snaps all three smoothers
  (`:288-290`) and freezes stiffness/pick (`:283-284`). At velocity 0,
  `velScale = velocity · excitationGain_ = 0` (`:393`, consumed at `:446`), so the excitation buffer written
  is entirely **zero** — this is the only pre-RA-1 way to set `bridgeDelayFloat_` (`:325`) without injecting
  a pluck (N-8: Phase 4 is continuous resonance, not struck).
  Leave a header comment at this call site recording risk R-7: `noteOn` builds two
  `std::array<float, 4096>` on the stack (`:405`, `:425`) — 32 KB of frame on the audio thread, RT-legal
  (no allocation) but worth finding immediately in a stack-depth investigation.
- **Comb assignment (FR-022b)** — `setTuningMode(Tuning::Inharmonic)`, `setSpread(0.45f)`,
  `setNumCombs(6)`, `setFundamental(f_body)` (bank clamps to `[20, 1000]` Hz at `:521`), then per comb
  `setCombFeedback(n, fb_n)` and `setCombDamping(n, damping_eff)`. **No `setStereoSpread`** — provably inert
  on the mono `process()` path (plan D-12).
- **Engine advance (FR-023)** — per control chunk, for each **active** slot, in slot order:
  modal → `modal_[i].processBlock(driveBuf, engBuf, int(subChunk))`;
  waveguide → `for (s) engBuf[s] = waveguide_.process(driveBuf[s]);` (its only entry point, `:154`);
  comb → `for (s) engBuf[s] = comb_.process(driveBuf[s]);` (mono `process`, `:328`/`:593-651` — **not**
  `processStereo`, which would fork the mono clamp and counter for one material).
  Inactive engines are **not advanced at all** — not called with zero input, not called and discarded.
  `driveBuf` is the mono-summed, zero-substituted, drive-scaled input; for a slot whose `inputMuted` flag is
  set it is a **zeroed** scratch buffer so the engine rings out through its own damping law.
  Accumulate `engineSampleCount_[engine] += subChunk` once per active slot per sub-chunk.
- **State-preserving retune for the two new engines (FR-041)** — comb: `setFundamental(f_body)` (`:238`,
  smoothed by the bank's own 20 ms delay smoothers at `:109`); waveguide: **`retune(f_body)` from T004,
  never `noteOn`** (which would reset the loop and re-inject a noise burst). Both behind the same dirty gate
  as the modal path, on their engine-native quantities (`T60_eff`, `S_eff`, `fb_n`, `damping_eff`).

**Verify:** `dsp_systems_tests.exe "ContinuousBody_OnlyActiveEnginesAdvance"` green, zero warnings.

---

## Group I — Stability under sustained excitation

### T010 — SC-001 bounded-under-drive, SC-002 decays-to-silence

**Files edited:** `dsp/tests/unit/systems/continuous_body_test.cpp` (and
`dsp/include/krate/dsp/systems/continuous_body.h` only if a clause fails).

**Failing tests first:**

`TEST_CASE("ContinuousBody_SustainedDriveBounded")` (SC-001) — parameterised over **5 materials × 3
signals**: (a) white noise seeded `0x5E4A0001`, (b) a sine at `getBodyFrequencyHz()`, (c) a 20 Hz → 8 kHz
log sweep. Settings: `setResonance(1.0)`, `setDrive(4.0)`, `setCloudDecaySec(30.0)`, `setCloudMix(1.0)`,
48 kHz, **60 s** of full-scale (peak 1.0) input.
- peak output magnitude over the whole render **≤ 1.5**;
- RMS of the final 1 s **≤ 1.10 ×** RMS of seconds 9–10 (no growth), **and** the least-squares slope of
  `log10(RMS)` over four consecutive 15 s windows **≤ +0.025 per window** (≤ +0.5 dB/window — absorbs
  window-to-window fluctuation of noise and sweep excitation without admitting an upward trend);
- `stateFinite()` true throughout, sampled every block;
- `getClampEngagementCount()` delta **== 0** across the whole render for all three signals, **including
  (b) the resonant sine** — no carve-out; bracket it by subtracting the value read at render start (FR-007
  pins the counter as cleared only by `reset()`/`prepare()`);
- **the pre-clip headroom clause, which is the clause that actually discriminates** (plan §7.9 / D-9): a
  companion probe render per material × signal at the same settings but `cloudMix = 0`, `setMix(1.0)`, no
  crossfade in flight — so the output **is** the post-`applyOutputStage` engine sum. Steady-state peak
  **≤ 0.730** for Glass / Metal Plate / Ice / Strings, and **≤ 1.8** for Chamber.
  Derivation, so the number is not magic: requiring pre-clip `|modeSum| ≤ 0.9 = kEngineHeadroomFrac ·
  kEngineClipThreshold` and inverting the strictly-monotone `softClip` gives
  `softClip(0.9) = 0.9·(27+0.81)/(27+9·0.81) = 0.72996`. Chamber's comb has **no** output clipper
  (`timevar_comb_bank.h:593-651`), so its bound is `0.9 × kOutputClamp = 1.8` instead.
  Document in the test that the counter clause binds **only on Chamber**.
- **Edge-case clause, same shape:** `userDrive = 4` with `rmsGain` at maximum — on Chamber
  `getClampEngagementCount()` may engage and **must fall to zero within 500 ms** of `setDrive(1)`; on the
  other four the headroom metric may exceed 0.730 and must fall back below it within 500 ms. The clamp must
  never **latch**.

`TEST_CASE("ContinuousBody_DecaysToSilence")` (SC-002) — continue each SC-001 render with **exactly zero
input**. Final-block peak magnitude **< 1.0e-4** (−80 dBFS, the Membrum threshold at
`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59`) within
**`cloudDecaySec + 6.91/kMinB1 + 5 s` = 30 + 30 + 5 = 65 s**, applied **uniformly to all five materials**.
The bound is stated from constants, not from a per-material T60, so it is computable before the render.

**If a clause fails, the response is to fix the DSP, never to widen a threshold.** The levers are the
drive-law path (T008) and the mode set (T007), in that order.

**Note on wall time (risk R-11, open item OQ-C):** SC-001 + SC-002 render 5 materials × 3 signals ×
(60 s drive + up to 65 s ring-out) ≈ 31 minutes of audio per full run at 48 kHz. The **default is that these
run on every CI leg** — this is the criterion the roadmap names first (line 219), and Membrum's kit-switch
test sets the precedent. Tag them `[.slow]` **only** with recorded user sign-off (OQ-C).

**Verify:** `dsp_systems_tests.exe "ContinuousBody_SustainedDriveBounded,ContinuousBody_DecaysToSilence"`
green.

---

## Group J — Spectral characterisation

### T011 — SC-003 (a–d), the shared peak-detection helper

**Files edited:** `dsp/tests/unit/systems/continuous_body_spectral_test.cpp` (and
`dsp/include/krate/dsp/systems/continuous_body.h` only if a profile needs changing).

**Shared helper first**, in the spectral TU, so two implementations cannot disagree:
- `f0` = frequency of the highest-magnitude spectral peak **below `1.5 · f_body`** in an 8192-point
  Hann-windowed FFT, refined by 3-point parabolic interpolation on **log** magnitudes.
  **Autocorrelation, cepstral and YIN estimators are excluded by name.**
- Peaks = local maxima at least **8 bins** apart whose magnitude is within **40 dB** of the render peak,
  taken in frequency order, first 8 used.
- **The analysis pipeline, stated once and used by (a) and (c):** last 1.0 s of the render → one 8192-point
  Hann FFT → discard DC → the remaining 4096 bins → dB relative to that render's own peak bin → clamp at a
  **−80 dB floor** → map to `[0,1]` as `(dB + 80)/80`. Distance = **mean absolute difference per bin**
  (0.0125 = 1 dB average per-bin difference).

**Failing tests:**

`TEST_CASE("ContinuousBody_MaterialsDistinct")` (SC-003 a, d) — 2 s of band-limited noise, `f_body = 220`,
`keyTracking = 1`, `cloudMix = 0`, per material.
- **(a1)** every one of the **10** cross-material distances **≥ 4 ×** the largest **within-material**
  distance (same material, two different noise-excitation seeds). This is the clause with teeth: it compares
  material difference against measurement noise on the same scale.
- **(a2)** every cross-material distance **≥ 0.02** mean-per-bin (L1 ≥ 82 over 4096 bins).
- **(d)** `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`): **Ice exceeds Glass by
  ≥ 0.02** at identical excitation and identical `resonance`/`damping`.
- **If the matrix misses, change the FR-011a profiles until the materials really are distinct — never lower
  a threshold.**

`TEST_CASE("ContinuousBody_MaterialCharacterOrdering")` (SC-003 b, c)
- **(b) monotone controls** — per material: measured T60 **non-decreasing** in `setResonance` across
  `r ∈ {0, 0.25, 0.5, 0.75, 1}` within a **5 %** per-step tolerance; `extractAudioFeatures(...).centroidHz`
  (`tests/test_helpers/audio_features.h:37`, `:88`) **non-increasing** in `setDamping` across the same grid
  within **2 %**, **and strictly lower at `d = 1` than at `d = 0` by ≥ 5 %** — so the control is audible,
  not merely non-increasing.
- **(c1) T60 at `r = 0.8`** within **±15 %** of the derived table and **strictly ordered**:
  MetalPlate **11.0** > Glass **6.61** > Ice **5.51** > Strings **3.83** > Chamber **1.20** s.
- **(c2) inharmonicity** = **mean `|ratio_k − k|` over the first 8 detected peaks, unnormalised and
  uncapped** (`ratio_k` = peak `k`'s frequency over the detected `f0`). Required:
  `inharm(Glass) ≥ 5 × inharm(MetalPlate)`; `inharm(Ice) ≥ 5 × inharm(MetalPlate)`;
  `inharm(MetalPlate) ≥ 3 × inharm(Strings)`; `inharm(Strings) ≤ 0.15`. Computed reference values:
  Glass ≈ 8.19, Ice ≈ 8.29, Metal Plate ≈ 0.99, Strings ≈ 0.03.
  **Glass and Ice are deliberately NOT ordered against each other by this metric** — they share a ratio
  table and the scatter warp moves the mean by ~1 %, which is not a separation. (A "nearest-integer
  deviation" metric saturates at 0.5 and cannot discriminate these tables at all — do not substitute it.)
- **(c3) Glass vs Ice** is asserted where the difference lives: **at least 6 of the first 8 detected peaks
  differ in frequency by ≥ 2 %** between the two materials. The bank's scatter is a *deterministic*
  golden-ratio displacement (`f_w ×= (1 + C·sin(k·kScatterD))`, `kScatterD = π(φ−1)`,
  `modal_resonator_bank.h:577-578`, `:729`) — not RNG — and at `C = 0.08` the per-mode displacements for
  `k = 0…7` are `0 %, +7.5 %, −5.4 %, −3.5 %, +8.0 %, −2.2 %, −6.4 %, +6.8 %`, clearing 2 % by margin.

Also add **SC-009(b)** here if it was not already satisfied in T007: detected fundamental within 5 cents of
`f_body` for Glass / MetalPlate / Ice using the shared estimator.

**Verify:** `dsp_systems_tests.exe "ContinuousBody_MaterialsDistinct,ContinuousBody_MaterialCharacterOrdering"`
green.

---

## Group K — Material crossfade and the collapse rule

### T012 — `setMaterial` crossfade, FR-024a collapse, SC-012 (i)(ii), SC-016 exact arithmetic

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing tests first:**

`TEST_CASE("ContinuousBody_CrossfadeClickless")` (SC-012 i) — under sustained input, **all 20 ordered
material transitions**.
`TEST_CASE("ContinuousBody_RetargetClickless")` (SC-012 ii) — **three modal materials inside one 500 ms
window** (Glass → Ice → MetalPlate, the case FR-024a's collapse rule exists for) and a modal → waveguide →
comb chain at 100 ms spacing.
Both measured **control-relative**, following `dsp/tests/unit/systems/harmonic_cloud_test.cpp:4817-4836`:
- control render = same material, same excitation, same seed, same duration, **no** transition and no setter
  movement;
- `ClickDetector` (`tests/test_helpers/artifact_detection.h:99`, `:130`) configured **explicitly with
  `sampleRate = 48000.0f`** — *not* the `44100.0f` default at `:39`, which would mis-scope every
  `timeSeconds` by 8.8 % — otherwise default `frameSize = 512` / `hopSize = 256` / `threshold = 5.0`:
  `detections(test) <= detections(control)`;
- `max|x[n]−x[n−1]|(test) <= 1.5 × max|x[n]−x[n−1]|(control)`;
- **non-vacuity**: the test render must **differ** from the control render.
**Absolute bounds are not usable and must not be substituted:** a unit-amplitude 440 Hz sinusoid at 48 kHz
already has `max|Δ| = 2 sin(π·440/48000) = 0.0576`, and a Metal Plate top mode near 2.46 kHz gives 0.322 —
on perfectly clean output. `ClickDetector` is a 5σ first-difference outlier detector, so under noise
excitation it reports order-10 detections from statistics alone; "zero detections" is unachievable.

`TEST_CASE("ContinuousBody_MaterialSwitchNoInfiniteRing")` (SC-002, second case) — hit all five materials in
rapid succession under sustained input, including Glass → Ice → MetalPlate inside one 500 ms window, stop the
input, assert the same **< 1.0e-4 within 65 s** bound as T010.

`TEST_CASE("ContinuousBody_OnlyActiveEnginesAdvance")` — **add the exact-arithmetic clause** (SC-016).
A qualitative "no third engine is advanced" clause is **unfalsifiable**: `getEngineSampleCount` is keyed by
the `Engine` enum (3 values) while the collapse rule concerns **slots**, and Glass → Ice → MetalPlate is
entirely `Engine::Modal`, so all three collapse into one bucket — and only two `ModalResonatorBank`
instances exist, so a third cannot run *by construction*. Instead: place every `setMaterial` call on an
exact 64-sample control-chunk boundary and `REQUIRE` the exact total
```
getEngineSampleCount(Modal) == numSamples + (t2 − t1) + round64(kSlotReleaseMs) + round64(kMaterialCrossfadeMs)
```
for the Glass(t0) → Ice(t1) → MetalPlate(t2 < t1 + 500 ms) sequence, where each added term counts the
samples during which **two** slots were simultaneously advanced: `(t2 − t1)` for the interrupted fade,
`kSlotReleaseMs` for the collapse, `kMaterialCrossfadeMs` for the fade that follows. Every duration is a
whole number of control chunks by construction, so **no tolerance is admitted**. A broken collapse rule —
one that let the interrupted fade run to completion — over-counts by ≈ `500 − 10 = 490 ms` of samples and
fails. (A per-slot accessor was rejected: FR-007's list is exhaustive.)

**Implement (plan §6.1, §6.2):**

- **Pool:** two `ModalResonatorBank` (slots A/B — required because three materials share the modal engine
  and `setModes` memsets `sinState_`/`cosState_` at `:238-239`, so Glass→Ice must crossfade two
  simultaneously-ringing modal states), one `WaveguideString`, one `TimeVaryingCombBank`. **Two is also a
  hard ceiling**, guaranteed by the collapse rule.
- **`Slot`** carries `material, engine, modalIndex, active, gain, gainBound, engineT60, modeCount,
  inputMuted, OnePoleSmoother driveLog10`, and the applied-value shadows `appliedBodyHz, appliedB1,
  appliedB3, appliedT60, appliedS, appliedCombFb[6], appliedCombDamp`. Plus `soundingSlot_`,
  `outgoingSlot_ = -1`.
- **`setMaterial(m)`:**
  1. `m == incoming material` → **no-op** (no crossfade, no state disturbance). "Incoming" is
     `soundingSlot_` when idle and the fade target otherwise.
  2. No fade in flight → assign to a free instance of the incoming engine type, `crossfadePos_ = 0`,
     `outgoingSlot_ = soundingSlot_`, `soundingSlot_ = free slot`, and **mute the outgoing slot's input** so
     it decays through its own damping law rather than being cut — the physically correct behaviour for a
     resonant body.
  3. Fade in flight → set `pendingMaterial_` and enter **collapse**: over `kSlotReleaseMs = 10 ms`, ramp the
     current `(fadeOut, fadeIn)` pair to `(0, 1)` with the same equal-power law; `crossfadePos_` **does not
     advance** during the collapse. On completion the in-flight *incoming* engine is the sole sounding
     engine (its state is what is being heard now), the other slot is `silence()`d and freed, then step 2
     runs for `pendingMaterial_`.
  **Never `silence()` a ringing engine to free a slot** — that is a step equal to its current crossfade
  gain, i.e. a click. `silence()` only ever happens at zero gain, everywhere in this component.
- **Gains:** `equalPowerGains(crossfadePos_, fadeOut, fadeIn)` (`core/crossfade_utils.h:50`), position
  advanced by `crossfadeIncrement(kMaterialCrossfadeMs, sampleRate)` (`:89`), held **constant across the 64
  samples of one control chunk** — a step of at most `sin(π/2 · 64/24000) ≈ 0.0042` per chunk at 48 kHz,
  three orders below `ClickDetector`'s sensitivity.
- **Fade completion:** clamp `crossfadePos_` to 1, `silence()` the outgoing engine, free the slot,
  `outgoingSlot_ = -1`.
- **Both engines are processed during the fade** — SC-005 budgets that window separately (T016).
- `isCrossfading()` / `getCrossfadePosition()` now return real values.

**Verify:**
`dsp_systems_tests.exe "ContinuousBody_CrossfadeClickless,ContinuousBody_RetargetClickless,ContinuousBody_MaterialSwitchNoInfiniteRing,ContinuousBody_OnlyActiveEnginesAdvance"`
green, zero warnings.

---

## Group L — Decay cloud and output stage

### T013 — Decay cloud, `setResonatorBypass`, `setMix`/`setWidth` endpoints

> **THIS TASK IS GATED. Do not ship it until OQ-A is resolved** (see Open Items). SC-008's
> `(0.5 s, cloudSize = 1.0)` grid point measures **+30.9 %** against a ±15 % criterion on a *correct*
> implementation (5.3 loop traversals). FR-052 names exactly one sanctioned response to a miss —
> "calibrate `fb` against a measured tail at configure time, **never** widen SC-008" — and deleting a grid
> point is neither a calibration nor a rename. Resolve by **(a)** implementing the decay-dependent `fb`
> calibration, or **(b)** obtaining and recording explicit user sign-off (with date) on narrowing the
> `cloudSize = 1.0` grid to `{2, 10, 30}` s. `kCascadeDelayFactor` is the sanctioned constant lever.
> **Never widen the tolerance.**

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_spectral_test.cpp`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing tests first:**

`TEST_CASE("ContinuousBody_CloudDecayAccuracy")` (SC-008, spectral TU)
- Configuration: `setResonatorBypass(true)` — **the only configuration in which the cloud's decay is
  attributable** (`setMix` cannot do it: neither endpoint removes the body; `setCloudMix` blends the cloud
  against the dry resonator output). `cloudMix = 1`, **`cloudDamping = 0.0`** (plan D-6: at
  `cloudDamping = 1` the 800 Hz in-loop LP, not `fb`, sets the tail — measured T60 at a requested 30 s is
  **1.6 s**; that is the damping control doing its job, and the RT60 *accuracy* claim cannot bind there),
  broadband impulse. Because bypass drops the `1/Ĝ` term the cloud is excited at **normal level**, so the
  tail starts near full scale and the test applies **no** makeup gain.
- **T60 by Schroeder backward energy integration (EDC)** — `E[n] = Σ_{m≥n} y[m]²`, block `√E` at 512,
  convert to dB relative to its own max, least-squares over the **−5 dB … −35 dB** span, `T60 = −60/slope`.
  **A peak-envelope regression is forbidden**: measured on a *correct* implementation it reports **−53 %**
  at `(30 s, size 0.0)`; an RMS-envelope regression reports **+43 %** at the same point. Only EDC lands
  inside ±15 % (plan D-4).
- Grid `{0.5, 2, 10, 30}` s at `cloudSize = 0.0` (`loopSeconds = 37/41 ms`, network bypassed below
  `size < 0.001`) **and** at `cloudSize = 1.0` (`loopSeconds ≈ 93.9/105.1 ms`) — the two ends of FR-052's
  loop-time formula, so a derivation that ignored the cascade fails the first and passes the second.
  The `cloudSize = 1.0` grid is `{0.5, 2, 10, 30}` **unless OQ-A resolves otherwise**.
- **±15 %** at every retained grid point. The 30 s case must be **measurable — the tail must still be above
  the noise floor at 20 s**.

`TEST_CASE("ContinuousBody_GlideIsClickless")` (SC-004, per material) — sustained noise input,
`keyTracking = 1`, glide the note frequency **110 → 440 Hz linearly in log-frequency over 1.0 s**.
Measured **control-relative** exactly as SC-012 is (control = same material/excitation/seed/duration with
the note held **fixed at 110 Hz**): `detections(glide) ≤ detections(control)` with `ClickDetector` at
`sampleRate = 48000.0f`; `max|Δ|(glide) ≤ 1.5 × max|Δ|(control)`; **non-vacuity** (the glide render must
differ from the control); and output RMS over any 20 ms window within **±3 dB** of the pre-glide RMS (no
dropout, no surge).

`TEST_CASE("ContinuousBody_OutputStageEndpoints")` (FR-060 / FR-062 — **both setters had a design element
and no functional test**; SC-012(iv) sweeps them but measures clicklessness only, so a `setMix` that ignored
its argument or a `setWidth` wired to nothing would pass everything else).
- **(i) FR-060 endpoint:** with `setMix(0)` settled (≥ 5 × `kMixSmoothMs`) under a sustained stereo signal,
  `outLeft[n] == inLeft[n]` and `outRight[n] == inRight[n]` within `kSampleTolerance = 1.0e-4` — exact and
  non-vacuous given FR-005's no-in-place rule. And `setMix(1)` under **zero** input with a ringing body
  gives non-zero output.
- **(ii) FR-062 endpoint:** `setResonatorBypass(true)`, `cloudMix = 1`, broadband excitation — the L/R
  Pearson correlation of the final 1 s must be **≥ 0.999 at `setWidth(0)`** (`DiffusionNetwork` collapses to
  mid at `:385-391`) and **≤ 0.95 at `setWidth(1)`**, i.e. a correlation delta **≥ 0.049**. Report the
  measured delta via `WARN` so a future tightening has a number to start from.

**Implement (plan §9, §10):**

- **Topology (FR-050):** `in → [+] → DelayLine(L_ch) → DiffusionNetwork → OnePoleLP(damp) → DCBlocker →
  out`, with the output tapped back through `× fb_ch`. Nested **private** `struct DecayCloud { DelayLine
  delayL, delayR; DiffusionNetwork diffusion; OnePoleLP dampL, dampR; DCBlocker dcL, dcR; std::size_t
  loopSamplesL, loopSamplesR; float fbL, fbR, loopSecondsL, loopSecondsR, lastPeak; }`.
- **Cadence — this is the one place §9.1 governs over §11.** The read / diffuse / damp / DC-block / write
  pass runs inside **`renderSub`, batched over `subChunk`** — *not* in `controlStep`. `controlStep` fires
  only on the absolute 64-grid, so putting the loop there would emit **no cloud output at all** for any
  sub-64 tail — precisely the 1023+1, 100+…+24 and 7×146+2 partitions SC-011 exists to catch.
  `controlStep`'s cloud item is **coefficient update only** (`fb`, damping cutoff, size-smoother advance).
  `DiffusionNetwork::process` is a **block entry point with no per-sample API** (`:327-329`) — call it with
  `numSamples = subChunk`, never 1.
- **Batched read-before-write** (causal because the shortest loop is 37 ms = 1776 samples at 48 kHz, 296
  even at 8 kHz):
  ```cpp
  for (std::size_t s = 0; s < subChunk; ++s)
      tap[s] = cloud_.delayL.read(cloud_.loopSamplesL - 1 - s);      // oldest first
  // diffusion / damp / dcblock over tap[0..subChunk)
  for (std::size_t s = 0; s < subChunk; ++s)
      cloud_.delayL.write(detail::flushDenormal(in[s] + cloud_.fbL * wet[s]));   // R-6
  ```
  The explicit `flushDenormal` on the feedback write is **mandatory** (risk R-6): the cloud's delay line has
  no flush of its own, and a 30 s tail decays toward 1e-30.
  `cloudChunkCap_ = min(loopSamplesL, loopSamplesR, kControlChunkSamples)` caps every sub-chunk, so
  `loopSamples ≥ subChunk` holds by construction; at every sane rate the cap is 64.
- **`loopSamplesL/R` are derived from the delay line alone, never from `loopSecondsL/R`.** `loopSeconds` is
  the *acoustic* loop time **including** the diffusion cascade's distributed throughput delay; the
  `DelayLine` is sized for its own 37/41 ms only. `DelayLine::read` **clamps silently**
  (`delay_line.h:212-218`), so an over-long index does not fault — it reads the wrong tap and the loop time
  is silently wrong. Keep the debug `assert(loopSamples <= maxDelaySamples())`.
- **RT60 mapping (FR-052):**
  ```
  cascadeSec_ch  = kBaseDelayMs·1e-3 · cloudSize · 17.777 · kCascadeDelayFactor · (ch == R ? kStereoOffset : 1)
  loopSeconds_ch = kCloudLoopMs_ch·1e-3 + cascadeSec_ch
  fb_ch          = min(exp10f(-3.0f · loopSeconds_ch / seconds), kMaxCloudFeedback)
  ```
  `Σ kDelayRatiosL = 17.777` (`diffusion_network.h:51-53`); a Schroeder allpass of delay `D` has mean group
  delay exactly `D`, so the cascade's mean delay is the sum of its stage delays. **`cloudSize` is the
  normalised `[0,1]` value** matching the smoothed quantity the network itself multiplies by (`:335`,
  `:366`) — **not** the percent handed to `setSize`. At `cloudSize = 1.0`: `cascadeSec_L = 56.9 ms`,
  `cascadeSec_R = 64.1 ms`, `loopSeconds_L = 93.9 ms`, `loopSeconds_R = 105.1 ms`; at `seconds = 30`,
  `fb_L ≈ 0.97862`, far inside `kMaxCloudFeedback = 0.9995`, so the loop is **provably contracting at every
  setting** (FR-054). Recomputed in `prepare()` and on every `setCloudSize()`; below `size < 0.001` the
  network bypasses (`:344`) and `cascadeSec` degrades correctly to 0.
  **Deriving `fb` from the delay line alone under-damps by ~2.5× and no ±15 % criterion survives that.**
- **Cloud controls (FR-053):** `setCloudMix` — equal-power parallel blend against the dry resonator output;
  `setCloudDamping(d)` — `fc = 18000 · (800/18000)^d` (geometric), smoothed at `kCloudSmoothMs` **in the
  log-frequency domain**; `setCloudSize(s)` — `diffusion.setSize(s·100)` and recompute `loopSeconds`/`fb`;
  `setWidth(w)` — `diffusion.setWidth(w·100)`.
- **Bypass (FR-053a):** when `cloudMix < kCloudBypassEpsilon (1e-4)` **and** `cloud_.lastPeak <
  kCloudSilenceFloor (1e-6)`, skip the entire cloud path for that chunk and pass the dry resonator output
  through. The energy test gates the bypass independently of the mix, so **a tail is never truncated to save
  CPU**; re-entry is ramped by `setCloudMix`'s own smoother, so it cannot click.
- **`setResonatorBypass(bool)` (FR-063):** when true, the mono-summed input is scaled by
  `cloudDrive = rmsGain · userDrive` (**no `1/Ĝ`** — it bounds a resonator's steady-state gain and there is
  no resonator in the path) and feeds the cloud directly; **no** engine is advanced, so
  `getEngineSampleCount` stays flat. Toggling ramps over `kSlotReleaseMs = 10 ms` with the equal-power law
  and `silence()`s the engine at zero gain. **On un-bypass, if the sounding slot's engine is
  `Engine::Waveguide`, call `waveguide_.retune(f_body)` BEFORE the ramp back to unity** — see T014's §10.1
  rule; without it the string is bricked.
- **`setMix` (FR-060):** equal-power `out = gDry·in + gWet·(body + cloud)`. 0 = input passed through
  unchanged, 1 = body+cloud only. **There is no level control in this component** (FR-061 deleted — Phase 7
  owns the output stage, and a user trim would sit directly on SC-007's measurement path).
- **`setWidth` / FR-062:** the mono resonator output is written to **both** channels before the cloud; the
  cloud's own decorrelation (`kCloudLoopMsL/R` mutually near-coprime) plus `DiffusionNetwork::setWidth`
  (`:288`) supplies the width.
- **FR-055 boundary:** no pitch shifting, no spectral stage, no freeze/unity-feedback mode, no cross-channel
  matrix. Those are Phase 6's `AetherReverb`.

**Verify:**
`dsp_systems_tests.exe "ContinuousBody_CloudDecayAccuracy,ContinuousBody_GlideIsClickless,ContinuousBody_OutputStageEndpoints"`
green, zero warnings.

---

## Group M — Determinism, robustness, and the `silence()` recovery rule

### T014 — Seeding, non-finite handling, retune-after-`silence()`, SC-012 (iii)(iv)

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/continuous_body_test.cpp`.

**Failing tests first:**

`TEST_CASE("ContinuousBody_SeedDeterminism")` (SC-010)
- Two instances, same seed / sample rate / parameter script →
  `compareFingerprints(fingerprintRender(a), fingerprintRender(b)).withinTolerance()` **true**
  (`render_fingerprint.h:64`, `:101`, default tolerances). **No bit-exact float golden anywhere.**
- **Anti-vacuity, stated per material because the seed's reach is asymmetric:**
  - **Glass, Metal Plate, Ice** — seeds 1 and 2 must produce a **failing** `compareFingerprints`, **and**
    the detected frequency of mode 8 must differ between the two seeds by **≥ 0.5 cents**
    (`kSeedDetuneCents = 3.0` gives up to ±3 cents per mode, so 0.5 is a comfortable floor);
  - **Strings, Chamber** — seeds 1 and 2 must produce an **identical** render. The seed is documented inert
    for both (waveguide: the RNG feeds only the note-on burst, injected at velocity 0 → `velScale = 0`,
    `waveguide_string.h:393`/`:446`; comb: hard-seeded `12345u + i·7919u`, `timevar_comb_bank.h:429`,
    `:450`). Asserting the **sameness** turns a known limitation into a covered one.

`TEST_CASE("ContinuousBody_NonFiniteInputRecovery")` (SC-013 a, a2)
- **(a)** inject NaN/Inf for one block under sustained excitation → output finite, **no `silence()`**,
  unbroken tail: output RMS over the **following 100 ms within ±1 dB** of the same window in an un-poisoned
  control render, and the injection point passes SC-012's control-relative click clauses. Recovery on the
  next finite block is complete because nothing was reset.
- **(a2) partition × poison cross-case** (plan D-10): inject the **same absolute-sample-index** NaN under a
  **1×1024** partition and under a **36+28-split** partition, and require the two outputs to agree to
  `kSampleTolerance = 1.0e-4`. Without this, SC-011 (varies the partition, injects no poison) and SC-013(a)
  (poisons one partition) never intersect and the sub-chunk substitution unit is unmeasured.

`TEST_CASE("ContinuousBody_NonFiniteStateRecovery")` (SC-013 b, b2)
- **(b) poison the path that can actually be poisoned.** Do **not** drive ±1e38 through the engine path
  expecting `stateFinite()` to go false: `softClip(+Inf) = 1.0f` (`dsp_utils.h:107`), the modal predicate
  reads `getModalEnergy()` and the waveguide loop is self-bounding, so the observables never go non-finite
  there. Instead: `setResonatorBypass(true)`, `setCloudDecaySec(30)`, `setCloudMix(1)`, `setDrive(4)`, then
  **±1e38 (bit-pattern-built through a `volatile` sink) for one block** — the FR-063 path scales by
  `cloudDrive` with **no `1/Ĝ`** and **no** FR-037 clamp, so ≈ 2e37 enters the cloud delay line and the
  feedback accumulation overflows within a few traversals.
  Assert: `stateFinite()` goes **false**; the **cloud** is what was cleared and the engines are **not**
  silenced (observed through behaviour — FR-038a clause 2's discriminator); output finite at **every**
  sample; `stateFinite()` **true again within 100 ms**; and — the clause whose absence made the latch
  permanent — with `setResonatorBypass(false)` restored, sustained excitation produces **non-zero** output,
  proving `rmsFollower_.reset()` ran.
- **(b2) follower-overflow regression:** feed ±1e38 with the resonator **active** (bypass off) for one
  block; assert `stateFinite()` is **never** false and output RMS recovers to within ±1 dB of a control
  render within 100 ms. This is the case `kMaxFollowerInput` exists for; without the clamp it fails
  permanently.

`TEST_CASE("ContinuousBody_ParameterSweepClickless")` (SC-012 iv) — every setter swept across its full range
once per 64-sample block for **10 s**, measured control-relative exactly as SC-012 is.

**Add to `ContinuousBody_CrossfadeClickless` / `ContinuousBody_OnlyActiveEnginesAdvance` the Strings
liveness clauses** (SC-012 iii, SC-016): with material = **Strings**, toggle `setResonatorBypass` on and
off; RMS over the 500 ms **after** the round-trip must be **≥ 0.5 ×** RMS over the 500 ms **before** it and
**strictly non-zero**. Without this, the bricked-string defect passes every clickless criterion trivially —
**digital silence has no clicks**.

**Implement:**

- **Seeding (FR-070 / FR-070a):** `setSeed(std::uint32_t)`, configure-time only, takes effect at the next
  control step, **not** required to be retro-deterministic (document it). Seed 0 passes through to
  `Xorshift32`, which substitutes its own default (`random.h:45-46`). The seed drives **exactly one thing**:
  per-voice modal micro-detune — at `setModes`/`updateModes` time, mode `k`'s frequency is multiplied by
  `exp2f(j_k · kSeedDetuneCents / 1200.0f)` with `j_k ∈ [−1,1]` from
  `Xorshift32(deriveStreamSeed(seed_, k)).nextFloat()` (`random.h:102-111`, `:59-63`). Configure-time cost
  only. **Three modal materials only.** `deriveStreamSeed`'s non-zero substitution is load-bearing — two
  lanes hashing to 0 would collapse onto one stream.
- **FR-038 (input hygiene)** — already wired in T006's walker; confirm the guard is applied **in the loop**,
  never repaired afterwards, and that `chunkSumSq_` is **assigned** 0 on poison, never subtracted
  (`NaN − NaN = NaN` would flow straight into `processSample`, which is documented "Does NOT validate
  input", `envelope_follower.h:163-164`, and latches permanently). `chunkPoisoned_` is **sticky** for the
  remainder of the control chunk. The substitution must happen **upstream of the engines** because
  `TimeVaryingCombBank` (`:598-601`, `:664-669`) and `OnePoleLP` (`:104-107`) both `reset()` themselves on a
  non-finite input — they would destroy the ring on our behalf.
- **`stateFinite()` composition (private predicates):**
  | Subsystem | Observes | Why |
  |---|---|---|
  | Modal slot | `modal_[i].getModalEnergy()` (`:415`) | computed **directly from `sinState_`/`cosState_`** (`:418-419`), never through `outputGain_` or the soft clipper — the only public window on the bank's raw state. **Do not** observe engine *output*: it passes through `applyOutputStage` → `softClip`, which maps overflow to a *finite* 1.0. |
  | Waveguide slot | `getFeedbackVelocity()` + last engine sample | belt-and-braces; the loop is self-bounding |
  | Comb slot | last output | belt-and-braces; the bank self-heals at `:637-641` |
  | Cloud | last tap + both feedback-write values, captured during the chunk | the one subsystem a finite input can actually poison |
  | Control | every smoother's `getCurrentValue()` + `rmsFollower_.getCurrentValue()` | the follower is the one that latches |
  Cheap: a fixed set of scalars captured during the chunk, never a scan of internal arrays.
- **FR-038a recovery set — the rule: a state `stateFinite()` observes and the recovery does not clear is an
  unrecoverable latch by construction.** On false, at zero gain after a `kSlotReleaseMs` equal-power ramp:
  1. `engineStateFinite(slot)` false → `silence()` that engine, **then immediately re-tune it** (below);
  2. `cloudStateFinite()` false → clear `delayL/R`, `diffusion.reset()`, `dampL/R.reset()`, `dcL/R.reset()`,
     zero `cloud_.lastPeak`;
  3. `controlStateFinite()` false → `rmsFollower_.reset()` **and** re-snap `driveLog10` for every slot from
     the post-reset `rmsGain`, plus `snapToTarget()` on any smoother reading non-finite. The follower reset
     is the clause whose absence made the latch permanent.
  4. Ramp back to unity over `kSlotReleaseMs` once `stateFinite()` reports true — **never as a step.**
  `reset()`/`prepare()` run the **same** clearing set unconditionally, so the two paths cannot drift.
- **§10.1 — `silence()` on the waveguide MUST be followed by `retune()`.** `WaveguideString::silence()` sets
  `bridgeDelayFloat_ = 0.0f` (`waveguide_string.h:243`) and `process()` early-returns 0 whenever
  `bridgeDelayFloat_ < kMinDelaySamples` (`:156`); the field is written in exactly **three** places —
  `silence()` (`:243`), `noteOn()` (`:325`) and RA-1's `retune()`. §8.3's control-step retune does **not**
  rescue it: `retune` fires only on `pitchDirty`, and none of the silence paths moves the pitch.
  The three paths, exhaustively:
  1. **FR-038a state recovery** → `retune(f_body)` as the **first** step of the ramped re-entry;
  2. **FR-063 un-bypass** when the sounding slot's `engine == Engine::Waveguide` → `retune(f_body)` before
     the ramp to unity (wired in T013);
  3. **`reset()`** → clear the applied-value shadows (already specified in T006) so the next control step's
     dirty gate fires unconditionally and re-tunes.
  The fade-completion and collapse `silence()`s need no addition: they free the slot, and the next thing to
  touch it is a material assignment, which calls `noteOn`.

**Verify:**
`dsp_systems_tests.exe "ContinuousBody_SeedDeterminism,ContinuousBody_NonFiniteInputRecovery,ContinuousBody_NonFiniteStateRecovery,ContinuousBody_ParameterSweepClickless,ContinuousBody_CrossfadeClickless,ContinuousBody_OnlyActiveEnginesAdvance"`
green, zero warnings.

---

## Group N — Sample-rate invariance

### T015 — SC-011 sample-rate invariance

**Files edited:** `dsp/tests/unit/systems/continuous_body_test.cpp` (component fixes only if it fails).

**Failing test** — `TEST_CASE("ContinuousBody_SampleRateInvariance")`: at **44,100 / 48,000 / 96,000 Hz**,
starting from FR-009's freshly-prepared state (Glass, resonance 0.7, damping 0.0, keyTracking 1.0, 220 Hz,
mix 1.0, cloudMix 0.25, cloudDecay 4.0 s, cloudSize 1.0, cloudDamping 0.3, width 1.0, AGC on, seed 1) with
identical parameters:
- measured T60 within **±10 %**;
- detected fundamental within **5 cents** (shared estimator);
- steady-state output RMS within **±1 dB**.

Note the physics the criterion is guarding: at 96 kHz `kMinB1 = 0.23` gives `1−R ≈ 2.4e-6`, so the
steady-state gain bound **doubles** versus 48 kHz. FR-032 recomputes `Ĝ` from the actual rate, so the drive
compensation follows automatically — this test verifies the **loudness does not**.

Also cover the low-rate edge case as a sub-section: at 8 kHz, mode frequencies above `0.49·fs` are culled and
a material may end up with only its fundamental — it must **produce sound, not silence and not NaN**.

**Verify:** `dsp_systems_tests.exe "ContinuousBody_SampleRateInvariance"` green.

---

## Group O — CPU budget

### T016 — SC-005 baselines, measured and pinned

**Files edited:** `dsp/tests/unit/systems/continuous_body_perf_test.cpp`.

**Test** — `TEST_CASE("ContinuousBody_CpuBudget", "[.perf]")`. Basis: **nanoseconds per 512-sample block**,
best-of-25 × 500 blocks (the `harmonic_cloud_perf_test.cpp:191-193` shape, chosen for hybrid-core migration
rejection). Reproduce the gate shape from `harmonic_cloud_perf_test.cpp:80-151`, `:412`:

```cpp
constexpr double kBlockBudgetNs      = (512.0 / 48000.0) * 1e9;   // 10,666,667 ns
constexpr double kRegressionFactor   = 1.5;
constexpr double kReference1PctNs    = kBlockBudgetNs * 0.01;     // 106,667 ns
constexpr double kReferenceHalfPctNs = kBlockBudgetNs * 0.005;    //  53,333 ns
```

Each of the **four** baselines carries **both** compile-time clauses plus the relative runtime gate:
```cpp
static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock, "…");
static_assert(kBaselineNsPerBlock <= kReferenceNsPerBlock / kRegressionFactor, "…");
REQUIRE(measured <= kBaselineNsPerBlock * kRegressionFactor);
```
The two **compose**: a baseline that would let `measured` exceed the reference does not **compile**, so the
runtime `REQUIRE` transitively binds the absolute figure on every machine. `[.perf]` keeps the *timing* out
of CI; the `static_assert`s are evaluated by every CI leg regardless of tags — which is exactly why the gate
is placed there. The percent-of-core figure is **reported via `WARN`**, never asserted (it is not
machine-portable).

| Configuration | Reference |
|---|---|
| **steady state** — one material, cloud active, no crossfade, static parameters | 53,333 ns (0.5 %) |
| **operating point** — every setter stepped once per 64-sample chunk, note gliding | 53,333 ns (0.5 %) |
| **crossfade window** — during a material change, two engines (the FR-024/FR-024a cap) | 106,667 ns (1 %) |
| **cloud only** — `setResonatorBypass(true)` | 53,333 ns (0.5 %) |

The steady-state and operating-point references are **half** the roadmap figure on purpose: the crossfade
window runs two engines and the roadmap's 1 % ceiling has no transient exemption. Budgeting the crossfade at
2 % would put 16 voices at 32 % against the roadmap's 25 % full-poly ceiling during a synchronous material
change.

- **Each baseline is pinned to the MOST EXPENSIVE material's measured number, and all five materials are
  `REQUIRE`d against it.** Plan §8.3 establishes that this is **Metal Plate** (29 modes at 220 Hz vs
  Glass's 11 — its ratios grow linearly while Glass's grow as `n²`). 5 materials × 4 configurations = 20
  measurements against 4 checked-in constants.
- **Additionally: the cheapest material must measure ≤ 0.7 × the most expensive.** This is corroboration for
  FR-023, not its proof — SC-016 (T009/T012) is the proof.
- Record a **baseline provenance block** in the TU exactly as `harmonic_cloud_perf_test.cpp:104-122` does:
  machine, build config, trial shape, date, and the 8 consecutive runs the baseline was taken from.
- Assert (or `WARN` with a measured number) that the "cloud only" baseline **does not degrade during a 30 s
  tail** — risk R-6's denormal check.

**If a measurement is over budget: reduce cost, never raise the baseline** (the rule at
`harmonic_cloud_perf_test.cpp:82-85`). Levers in order:
1. verify FR-042a's relative dirty gate is actually firing (a bug that dirties every step costs ~128
   transcendentals/chunk);
2. verify RA-4's fast path — that `modDepth` reaches exactly 0 through the smoother;
3. verify FR-053a's cloud bypass engages at `cloudMix = 0` with a settled loop;
4. hoist `Ĝ` behind the **same** dirty flag as `updateModes` (they share inputs — never compute `Ĝ`
   unconditionally);
5. **raising** `kNyquistHeadroomOct` above 1.0 to truncate harder — this trades *specified* glide headroom
   for CPU and must be justified in the header (open item OQ-D);
6. only then escalate. **`kModeCountCeiling` is fixed at 32 and is not a lever.**

**Verify:** `dsp_systems_tests.exe "ContinuousBody_CpuBudget*" 2>&1 | tail -20`; both `static_assert`s
present per baseline; zero warnings.

---

## Group P — Integration

### T017 — CMake registration audit

**Files inspected:** `dsp/tests/CMakeLists.txt`.

Confirm T002's single edit is complete and nothing added later drifted out of it:
- all three systems TUs (`continuous_body_test.cpp`, `continuous_body_spectral_test.cpp`,
  `continuous_body_perf_test.cpp`) appear in the `dsp_systems_tests` source list;
- both processors TUs (`waveguide_string_retune_test.cpp`, `diffusion_network_zeromod_test.cpp`) appear in
  the `dsp_processors_tests` source list;
- `unit/systems/continuous_body_test.cpp` — and **only** that one of the five — appears in the
  `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block;
- **no glob was introduced anywhere** (sources are listed explicitly; a file not listed silently drops and
  the suite reports "all tests passed").

Then confirm the cases actually run, not just link:
```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "WaveguideString_Retune*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "DiffusionNetwork_ZeroMod*" 2>&1 | tail -5
```
Each must report matching cases run — **"No tests ran" is a failure of this task**, not a pass.

---

### T018 — Full-suite run: Phase 4 suites plus the SC-014 amendment regression matrix

Both amended headers are **shared DSP** with consumers outside this phase, so the regression set is the
union of their consumers' suites.

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Phase 4's own suites
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody_CpuBudget*" 2>&1 | tail -20

# SC-014 regression matrix
"$CMAKE" --build build/windows-x64-release --config Release \
  --target dsp_processors_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests disrumpo_tests
for t in dsp_processors_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests disrumpo_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done

# the remaining DSP layers, zero cost
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests
for t in dsp_core_tests dsp_primitives_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```

| Amended header | Consumers (verified by grep) | Suites that must pass **unedited** |
|---|---|---|
| `processors/waveguide_string.h` (RA-1) | `dsp/tests/unit/processors/{waveguide_string_test,waveguide_string_dc_blocker_test,bow_waveguide_coupling_test}.cpp`; `plugins/innexus/src/processor/innexus_voice.h:24` (+2 Innexus tests); **`plugins/membrum/src/dsp/drum_voice.h:41`, `bodies/string_body.h:22`**; `tools/membrum_preset_generator.cpp` | `dsp_processors_tests`, `innexus_tests`, **`membrum_tests`** |
| `processors/diffusion_network.h` (RA-4) | `dsp/tests/unit/processors/diffusion_network_test.cpp:14`; `dsp/include/krate/dsp/effects/shimmer_delay.h:32`; `effects/freeze_mode.h:31`; `plugins/iterum/src/processor/processor.h:27` | `dsp_processors_tests`, `dsp_effects_tests`, `plugin_tests` **+ `approval_tests`** |

**`membrum_tests` is not optional** — Membrum is the heaviest `WaveguideString` consumer in the repo, and a
build stage that skips it has not verified RA-1. **`approval_tests` is not optional** — running only
`plugin_tests` silently skips Iterum's golden-output regression coverage. `disrumpo_tests` is a zero-cost
belt-and-braces run (no Disrumpo source includes either header).

**Every failure is owned.** "Pre-existing" and "flaky" are not acceptable dispositions — fix it or escalate.
Capture output to a log file on the **first** run; never re-run a slow suite just to read its output.

**No pluginval run** — this phase touches no plugin source. (`approval_tests` is the RA-4 guard, not a
plugin-behaviour change.)

---

### T019 — Portability, lint and static-analysis gates

```bash
node tools/check-portability.js                # MSVC-green proves NOTHING about GCC/Clang
node tools/lint-layers.js
node tools/lint-arch-guarded-includes.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/lint-odr.js
node tools/lint-allocation-operator-overrides.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

All must be clean. Specifically re-check by hand, because these are the classes of defect a green Windows
build cannot catch:
- **no `std::isnan` / `std::isinf` / `std::numeric_limits<float>::infinity()` / `quiet_NaN()`** anywhere in
  `continuous_body.h`, the amended headers, or any of the five new TUs — bit-pattern checks and
  `volatile`-sink construction only;
- **no narrowing in brace init** — every `MaterialProfile` field carries an explicit `f` suffix and uses
  designated initialisers (Clang errors where MSVC does not);
- **no bit-exact float golden** except the single justified same-binary comparison in
  `DiffusionNetwork_ZeroModIsBitIdentical` (with the comment `lint-float-bit-goldens.js` recognises);
- **`continuous_body.h` includes exactly the FR-003/D-7 set** — in particular `core/dsp_utils.h` is absent
  and `core/db_utils.h` is present as a **direct** include, not a transitive one through `smoother.h:28`;
- **zero compiler warnings** on every target built in T018.

---

## Open items that gate work in this list

| # | Item | Gates | Resolution required |
|---|---|---|---|
| **OQ-A** | SC-008's `(0.5 s, cloudSize = 1.0)` grid point measures **+30.9 %** against ±15 % on a correct implementation (5.3 loop traversals) | **T013 (hard gate)** | Either implement FR-052's prescribed decay-dependent `fb` calibration, **or** record explicit user sign-off (with date) on narrowing the `cloudSize = 1.0` grid to `{2, 10, 30}` s. The plan does **not** adopt the reduced grid by default, and widening the tolerance is not an option. |
| **OQ-B** | How `WaveguideString_RetuneIsInert` obtains its pre-amendment reference (the amendment ships in the same commit, so there is no pre-amendment binary at test time) | T004 | Recommended: **(a)** pin a `RenderFingerprint` measured on the pre-amendment header during implementation **plus (b)** structural containment + the unedited consumer suites. (a) is cheap; (b) is what actually covers Innexus and Membrum. |
| **OQ-C** | Test wall time: SC-001 + SC-002 render ≈ 31 minutes of audio per full run, plus SC-007's self-sizing renders (up to 69 s for Metal Plate at `r = 1.0` × 4 resonance points × 5 materials) | T010 (tagging only) | Default is **run on every CI leg** (the criterion the roadmap names first, line 219; Membrum's kit-switch precedent). Tag `[.slow]` only with recorded user sign-off. |
| **OQ-D** | `kNyquistHeadroomOct = 1.0` vs Metal Plate's 29 modes at 220 Hz (~2.6× Glass's cost) | T016 (only if a baseline misses) | Raising it trades one octave of *specified* clickless upward glide for CPU. Flagged now so the trade is never made silently at measurement time. |
| **OQ-E** | `ModalResonatorBank::smoothCoefficients()` runs once per `processBlock` call (`:357`), so the smoothing cadence depends on the host's block partitioning (16 calls for 1×1024, 17 for 1023+1) | T007 (only if SC-011 sub-case β misses) | Either drive the modal bank on a partition-independent cadence (one `processBlock` per **complete** control chunk, costing up to 63 samples of latency on the modal path and contradicting FR-005a's 0-sample guarantee), **or** record a **measured** deviation with the actual maximum divergence. **Widening SC-011's tolerance is not an option.** Raised now because the fix is a structural change to the §11 walker and would be expensive to retrofit at T016. |

---

## Dependency summary

```
A: T001  (sweep + green baseline)
     |
B: T002  (single CMakeLists edit + 5 stub TUs)
     |
C: T003 [P] RA-4 diffusion_network.h  |  T004 [P] RA-1 waveguide_string.h
     |
D: T005  data model (types, constants, profiles, ratio tables)
E: T006  lifecycle + control-grid walker + setters + FR-007 surface
F: T007  modal engine + key tracking + mode-count truncation + dirty gates   [SC-011 beta -> OQ-E]
G: T008  drive normaliser: G-hat, two drives, AGC, resonance/damping laws
H: T009  waveguide + comb engines + engine sample counts
I: T010  SC-001 bounded / SC-002 decays-to-silence                            [OQ-C]
J: T011  SC-003 spectral characterisation + peak helper
K: T012  crossfade + collapse rule + SC-016 exact arithmetic
L: T013  decay cloud + resonator bypass + output-stage endpoints              [GATED: OQ-A]
M: T014  seeding + non-finite handling + retune-after-silence
N: T015  SC-011 sample-rate invariance
O: T016  SC-005 perf baselines                                                [OQ-D]
P: T017 CMake audit -> T018 full-suite + SC-014 matrix -> T019 portability/lint/tidy
```

Groups D through O are strictly sequential: every one of them edits
`dsp/include/krate/dsp/systems/continuous_body.h` and/or `continuous_body_test.cpp`, so no two are
parallel-safe. Group C is the only parallel group in the phase.
