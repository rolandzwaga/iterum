# Tasks: Seraphis Phase 6 — Aether Space Engine

**Spec:** `specs/seraphis-phase6-aether-space/spec.md`
**Plan:** `specs/seraphis-phase6-aether-space/plan.md`
**Ships:** one new Layer 4 header `dsp/include/krate/dsp/effects/aether_reverb.h` + five new test TUs.
**Amends:** nothing outside this phase (RA-1). No existing DSP header is edited.

---

## How to read this document

- Tasks are **T001…T018**, grouped into **ordered groups**. Groups run in order; a group starts only
  when the previous group is green.
- Tasks marked **[P]** inside a group are parallel-safe: they touch **fully disjoint files**.
  Every task that edits `aether_reverb.h` (the one shared file in this phase) is in a group of its own.
- **Every task follows the canonical order:** failing test first → implement → zero compiler warnings →
  tests pass. A task is not done until `dsp_effects_tests` builds **warning-free** and the named cases
  pass.
- **No commits.** Commits happen outside this workflow.

**Build / run commands (Windows, full CMake path is mandatory):**

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "AetherReverb_*" 2>&1 | tail -5
```

Run one case: `dsp_effects_tests.exe "AetherReverb_MatrixOrthogonality"` (positional arg, **not** `-c`).
Run the perf lane: `dsp_effects_tests.exe "[.perf]"`. Run the nightly grids: `dsp_effects_tests.exe "[.slow]"`.

**Assumed answers to plan §12 open questions** (recorded here so an executor is never blocked; if the
user overrides any of them, the affected task is listed):

| plan §12 | assumed answer | affects |
|---|---|---|
| 1. `kBloomSendMax` / `kBloomLoopGainCeiling` | tuning **these two constants and FR-059's shelf only** is the admissible response if SC-016 clause 3 misses 6 dB. Never the criterion (B-4) | T010 |
| 2. `lint_all_headers.cpp` + `dsp/CMakeLists.txt` header list | **register** `aether_reverb.h` (breaks with the Phase 4/5 precedent, +2 lines, closes a strict-clang-tidy gap) | T015 |
| 3. GCC `-O2` cap | apply **pre-emptively**, in a **separate** `set_source_files_properties` call with the property string `"-O2"` alone | T001 |
| 4. FR-083 test hook | **ship** the `KRATE_DSP_AETHER_TEST_HOOKS`-gated `injectNonFiniteStateForTest()` | T001, T012 |
| 5. `[.slow]` demotions | measure the always-on wall clock, then take **only** the demotion steps the measurement requires, in the pre-decided order (SC-006 tail first, then SC-002 clause 4) | T016 |

**Deviation from the requested task ordering, stated so it is a decision and not an omission:** the
CMake registration of the five test TUs (plan §1.2, four sites) is done in **T001**, not in the final
integration group. `dsp_effects_tests`'s source list is explicit, not globbed — an unregistered TU
silently drops and **nothing in this phase can be built or run**. The final integration group (T015)
carries the *remaining* registration (plan §1.3) and re-verifies all four §1.2 sites.

---

## GROUP 1 — Scaffold, ODR, build wiring

### T001 — ODR sweep, header skeleton, CMake registration, five empty TUs

**Files created**

- `dsp/include/krate/dsp/effects/aether_reverb.h`
- `dsp/tests/unit/effects/aether_reverb_test.cpp`
- `dsp/tests/unit/effects/aether_reverb_matrix_test.cpp`
- `dsp/tests/unit/effects/aether_reverb_spectral_test.cpp`
- `dsp/tests/unit/effects/aether_reverb_perf_test.cpp`
- `dsp/tests/unit/effects/aether_reverb_nonfinite_test.cpp`

**Files edited**

- `dsp/tests/CMakeLists.txt` (four sites — see below)

**Step 0 — ODR sweep (run it, paste the output into the task log):**

```bash
grep -rn "class AetherReverb\|struct MatrixMorph\|struct BloomBank\|struct ShimmerTap\|struct ChannelState" dsp/ plugins/ tools/
```

Expected: zero hits for all five (spec "New components" table). `AtmosphereEngine::PrepareConfig`
(`dsp/include/krate/dsp/systems/atmosphere_engine.h:367`) is a **known, accepted** nested-scope name
collision — do not rename. `AllpassSaturator::HouseholderMatrix`
(`dsp/include/krate/dsp/processors/allpass_saturator.h:305`) exists — that name is **not** reused here.

**Failing test first.** Put one smoke case in each of the five TUs so the build wiring is proved before
any algorithm exists:

- `aether_reverb_test.cpp`: `TEST_CASE("AetherReverb_Construction", "[effects][aether]")` —
  `AetherReverb r; REQUIRE_FALSE(r.isPrepared());` then
  `r.prepare(48000.0, AetherReverb::PrepareConfig{}); REQUIRE(r.isPrepared());`
  `REQUIRE(r.getLatencySamples() == 1024u);` (default `spectralDiffusionEnabled = true`,
  `diffusionFftSize = 1024`).
- `aether_reverb_matrix_test.cpp`: `TEST_CASE("AetherReverb_MatrixSmoke", "[effects][aether]")` —
  `REQUIRE(AetherReverb::kMaxBloomResonators == 32);`
  `REQUIRE(AetherReverb::kControlChunkSamples == 64u);`
- `aether_reverb_spectral_test.cpp`: `TEST_CASE("AetherReverb_SpectralSmoke", "[effects][aether]")` —
  prepare with `spectralDiffusionEnabled = false`, `REQUIRE(r.getLatencySamples() == 0u);`
- `aether_reverb_perf_test.cpp`: `TEST_CASE("AetherReverb_PerfSmoke", "[.perf][effects][aether]")` —
  prepare + one 512-sample `processStereoBlock` of zeros, `REQUIRE(true)`.
- `aether_reverb_nonfinite_test.cpp`: `TEST_CASE("AetherReverb_NonFiniteSmoke", "[effects][aether]")` —
  prepare, then `REQUIRE(r.getNonFiniteRecoveryCount() == 0u);`

These must **fail to compile** before the header exists, and pass after step 1.

**Implementation intent (plan §2, §2.3, §2.4, §3).**

1. Header banner with the eleven items plan §2.1 enumerates, in that order — notably (2) the
   `fdn_reverb.h` line ranges whose topology is *re-derived, not included*
   (`:576-600` Jot, `:91` prime lengths, `:638-689` section layout, `:749-758` Householder,
   `:696-729` Hadamard FWHT, `:296-322` freeze bypass, `:207` DC-blocker `R`, `:354-371` even/odd taps +
   M/S width, `:374-377` equal-power mix); (3) the matrix sign convention (row 0 of `H_N/√N` negated,
   random endpoint forced to `det = −1`, all three in the `det = −1` component of `O(N)`); (6) the
   `[8000, 192000] Hz` range and the sub-44.1 kHz shimmer force-disable; (7) `silence()`'s **non-latching**
   divergence from `AtmosphereEngine::silence()` (`systems/atmosphere_engine.h:636-644`).
2. Includes **exactly** plan §2.2's 15-entry list, downward only. Do **not** include `fdn_reverb.h`,
   `reverb.h`, `shimmer_delay.h`, `window_functions.h` or `pitch_utils.h`.
3. `public static constexpr` constants exactly as plan §2.3 (cadence/lifecycle, geometry, injection,
   seed salts). `kMaxBloomResonators` is **`int`**, not `std::size_t` — it is passed straight to
   `processSympatheticBankSIMD`'s `int count` parameter
   (`dsp/include/krate/dsp/systems/sympathetic_resonance_simd.h:39-50`) and a `size_t` would narrow
   (C4267 / `-Wconversion`) against the zero-warning gate. Seed salts are **public** — SC-017 clause 1a
   reconstructs the breath trajectory from `kBreathSalt`.
4. Nested `struct PrepareConfig` exactly as plan §2.4 (`numChannels = 8`, `maxBlockSamples = 2048`,
   `maxDelaySeconds = 0.50f`, `shimmerEnabled = true`, `shimmerMode = PitchMode::Granular`,
   `bloomEnabled = true`, `spectralDiffusionEnabled = true`, `diffusionFftSize = 1024`, `seed = 1`).
5. Full public API from plan §3, **bodies empty or trivially returning defaults** except `prepare`
   (which sets `prepared_`, `sampleRate_`, the config snapshot and `getLatencySamples()`'s inputs) and
   the trivial introspection accessors the five smoke cases read. Copy deleted / move defaulted, exactly
   as plan §3 (`STFT`/`OverlapAdd` delete copy at `primitives/stft.h:41-44`, `:210-213`;
   `PitchShiftProcessor` is movable at `processors/pitch_shift_processor.h:125-126`).
6. Include the `#if defined(KRATE_DSP_AETHER_TEST_HOOKS)` block declaring
   `void injectNonFiniteStateForTest() noexcept;` with plan §3's full doc comment (its control-chunk-
   boundary precondition is normative). Body may be empty until T012.
7. Private state layout from plan §4 may be stubbed now or grown per task; `static constexpr
   std::size_t kMaxChannels = 16` and the `alignas(32)` SoA discipline are fixed from the start.

**CMake edits — exactly four sites in `dsp/tests/CMakeLists.txt` (verified this session):**

1. **Source list**, after `unit/effects/fdn_reverb_test.cpp` (currently the last entry of the
   `add_executable(dsp_effects_tests …)` list): append all five new TUs.
2. **`-fno-fast-math` block** (inside `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`, the call ending
   `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at ~`:701-703`): add **only**
   `unit/effects/aether_reverb_nonfinite_test.cpp`, with the comment discipline Phases 4/5 used.
   The other four TUs **must not** be listed.
3. **A NEW, THIRD `set_source_files_properties` call**, still inside the same `if(...)` guard, after the
   existing `-O2` call (which ends at ~`:711`, immediately before `endif()`):

   ```cmake
   set_source_files_properties(
       unit/effects/aether_reverb_test.cpp
       unit/effects/aether_reverb_spectral_test.cpp
       PROPERTIES COMPILE_FLAGS "-O2"
   )
   ```

   **Do NOT join the existing `-O2` call** — its property string is
   `"-fno-fast-math -fno-finite-math-only -O2"` (verified at `dsp/tests/CMakeLists.txt:710`), and
   `COMPILE_FLAGS` is a single string property (a second call replaces, it does not append). Joining it
   would build those two TUs under IEEE semantics on the exact legs where `-ffast-math` is the thing
   being guarded against (plan R-5). Carry plan §1.2 item 3's comment verbatim. Do **not** add the perf
   TU (an `-O2` cap changes the figures its baselines pin) and do **not** add the matrix TU (no audio).
4. **Target-wide define**, immediately after `target_link_libraries(dsp_effects_tests …)`:

   ```cmake
   target_compile_definitions(dsp_effects_tests PRIVATE KRATE_DSP_AETHER_TEST_HOOKS)
   ```

   Target-wide, **never** per-source: the macro changes the class definition, so every TU in the image
   must see the same one (plan R-12). `catch_discover_tests(dsp_effects_tests REPORTER console)` already
   exists at `:721` — nothing further is needed for CTest.

**Verify:** `dsp_effects_tests` compiles, links and runs the five smoke cases green, with **zero**
warnings. `node tools/lint-layers.js` clean. `node tools/lint-odr.js` clean.

**Test target:** `dsp_effects_tests`.

---

## GROUP 2 — Geometry: delay tables, Size, modal density

### T002 — Reference tables, `S(v)`, `maxSizeScale_`, buffer sectioning, geometry accessors

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_spectral_test.cpp`

**Failing test first** — in `aether_reverb_spectral_test.cpp`, add
`TEST_CASE("AetherReverb_GeometryAndModalDensity", "[effects][aether]")`, applying P-1
(`setSizeBreathDepth(0)`, `setDimensionalityTideDepth(0)`, `setModDepth(0)`) and P-2
(`maxDelaySeconds = 0.5f`, `REQUIRE(engine.getMaxSizeScale() == 4.0f)` **before** any Size sweep):

1. **FR-011 coprimality (runtime companion to the compile-time check):** for every pair `i < j` in both
   shipped tables, `std::gcd(kRefDelays8[i], kRefDelays8[j]) == 1` and likewise for `kRefDelays16`.
   Both tables strictly ascending.
2. **Size endpoints, N = 8, 48 kHz:** after `setSize(v)` and one 64-sample zero block,
   `getEffectiveDelayLengthSamples(0)` ≈ `967 · S(v)` within 0.5 %, for `v ∈ {0.0, 0.5, 1.0}` ⇒
   `241.75`, `967.0`, `3868.0` samples. `getEffectiveDelayLengthSamples(7)` ≈ `5087 · S(v)` ⇒
   `1271.75`, `5087.0`, `20348.0` samples. (`S(v) = 0.25·exp2f(4v)`: `S(0)=0.25`, `S(0.5)=1`, `S(1)=4`.)
3. **SC-003 clause 3(a) — the stale-accessor catch:** at five Sizes `{0, 0.25, 0.5, 0.75, 1}`, the test
   recomputes `D = Σᵢ getEffectiveDelayLengthSamples(i) / sampleRate` and requires agreement with
   `getModalDensityPerHz()` to **0.5 %**.
4. **SC-003 clause 3(b):** `D(size=1) / D(size=0) == 16.0` within **1 %** (ratio `S(1)/S(0) = 4/0.25`).
5. **Header table cross-check, N = 8 @ 48 kHz:** `getModalDensityPerHz()` = `0.106 / 0.426 / 1.702`
   at `S = 0.25 / 1.0 / 4.0`, within 1 %. Repeat with `numChannels = 16`:
   `0.210 / 0.841 / 3.363`, within 1 %.
6. **`maxSizeScale_` clamp path (Edge case 10):** prepare with `maxDelaySeconds = 0.05f` at 48 kHz ⇒
   `getMaxSizeScale()` ≈ **0.47** within 2 %. This is the **only** place the clamp is exercised;
   every other criterion asserts `== 4.0f` first.

**Implementation intent (plan §7.1, §7.2, §7.3, §5.1 steps 4–5).**

- Ship both `static constexpr` tables verbatim:
  `kRefDelays8 = {967, 1217, 1543, 1973, 2477, 3163, 4001, 5087}`;
  `kRefDelays16 = {967, 1087, 1201, 1361, 1511, 1693, 1879, 2099, 2347, 2621, 2927, 3271, 3659, 4079, 4561, 5087}`.
  Add a **compile-time** pairwise-coprimality `static_assert` over a `constexpr` gcd fold, so a future
  table edit cannot silently break FR-011.
- `refDelaySamples_[i] = kRefDelays<N>[i] · sampleRate_ / 48000.0` at `prepare`.
- `S(v) = 0.25f · exp2f(4.0f · v)` — `exp2f`, **not** `powf`; evaluated once per control chunk.
- `maxSizeScale_ = min(4.0f, (maxDelaySeconds·sr − kInterpMarginSamples) / (refDelaySamples_[N−1] ·
  (1 + kModExcursionFraction)))`, `kInterpMarginSamples = 4`, `kModExcursionFraction = 0.005f`.
- Section sizing uses the **clamped** `maxSizeScale_`:
  `sectionSize_i = nextPowerOf2(ceil(ref_i · maxSizeScale_ · 1.005) + 4)`
  (`nextPowerOf2` from `dsp/include/krate/dsp/primitives/delay_line.h:26`), one contiguous
  `delayBuffer_.assign(total, 0.0f)` with per-channel `sectionOffset_` / `sectionMask_` / `writePos_` —
  the `fdn_reverb.h:638-689` layout, re-derived.
- `getEffectiveDelayLengthSamples(i)` returns `effectiveDelay_[i]`; `getModalDensityPerHz()` returns
  `Σᵢ effectiveDelay_[i] / sampleRate_` computed **from the current Size-scaled lengths**, never from
  `prepare`-time geometry.
- `runControlStep()` step 4's geometry block (plan §6.2) may land here in its no-modulation form:
  `effectiveDelay_[i] = refDelaySamples_[i] · S(sizeSm)`.

**Verify:** case green; zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_GeometryAndModalDensity"`.

---

## GROUP 3 — Feedback matrix, Dimensionality morph

### T003 — `schurReduceSO`, the three endpoints, `MatrixMorph`, matrix introspection

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_matrix_test.cpp`

No audio is rendered by this task's criteria — SC-004 is fully measurable before the FDN loop exists.

**Failing test first** — two cases in `aether_reverb_matrix_test.cpp`:

**`TEST_CASE("AetherReverb_MatrixOrthogonality", "[effects][aether]")`** (SC-004 clauses 1–5).
Materialise `M(t)` at 101 positions `t = 0.00, 0.01, …, 1.00` via
`reset(); setDimensionality(t); processStereoBlock(64 zeros); copyCurrentMatrix(dst, N);`
with P-1 applied (so `t` is exactly the target and FR-009's smoother-initialisation rule makes it
settled on the first chunk).

1. `‖MᵀM − I‖_F ≤ **1e-5**` at all 101 positions, **recomputed in the test**; also
   `REQUIRE(engine.getMatrixOrthogonalityError() ≤ 1e-5)` and agreement with the test's own value to
   1e-6. `N = 8` always-on; `N = 16` in a `[.slow]` sibling section.
2. Norm preservation: for 64 seeded random unit vectors at 21 positions,
   `| ‖applyCurrentMatrix(x)‖₂ − ‖x‖₂ | ≤ 1e-4`; and
   `‖copyCurrentMatrix·x − applyCurrentMatrix(x)‖ ≤ 1e-6`.
3. **Negative control** (this is the clause that proves the geodesic is not a lerp): build a naive
   `M(u) = (1−u)A + uB` of the *same* shipped endpoints locally in the test, measure with the same code,
   and assert the exact table **in global `t` coordinates**, `N = 8`:

   | `t` | `u` | `‖MᵀM − I‖_F` |
   |---|---|---|
   | 0.0625 | 0.125 | 0.8750 |
   | 0.1250 | 0.250 | 1.5000 |
   | 0.1875 | 0.375 | 1.8750 |
   | **0.2500** | **0.500** | **2.0000**, plus `σ_min ≤ 1e-6` and `|det| ≤ 1e-6` (singular) |
   | 0.3750 | 0.750 | 1.5000 |
   | 0.5000 | 1.000 | 0.0000 |

   Tolerance 1e-3 on each figure. At `N = 16` the `u = 0.5` figure is **2.8284**. Additionally, segment 2
   sampled at `t = 0.75` must exceed clause 1's 1e-5 threshold by **≥ 4 orders of magnitude**.
4. **Endpoint identity:** `t = 0` entrywise equals `I − (2/N)·J` to 1e-6 (diagonal `0.75`, every
   off-diagonal `−0.25` at `N = 8` — it is **dense**); `t = 0.5` entrywise equals `D·H_N/√N` with
   `D = diag(−1, 1, …, 1)` to 1e-6, **including row 0's flipped sign**; `t = 1` is orthogonal,
   reproducible across two `prepare`s at the same seed to 1e-6, and **seed-sensitive** — max-abs
   entrywise difference between two different seeds ≥ 0.1.
5. **Component invariant:** `|det(M(t)) + 1| ≤ 1e-5` at **all 101** positions (`det` computed in the
   test by LU with partial pivoting).

**`TEST_CASE("AetherReverb_SchurReduction", "[effects][aether]")`** (SC-004 clause 6, plan R-7).
Direct on the `public static` `AetherReverb::schurReduceSO(rRowMajor, n, vRowMajor, thetas)`, for
`n ∈ {8, 16}`, over (i) the two shipped endpoint pairs `R = A_segᵀB_seg` and (ii) **≥ 32** seeded random
`SO(n)` inputs (Gaussian-ish `Xorshift32` draws → Gram–Schmidt → negate a column if `det < 0`):

- (a) `‖VᵀV − I‖_F ≤ 1e-6`;
- (b) `B(θ)` block-diagonal to 1e-6, each block satisfying `b₀₀ = b₁₁`, `b₀₁ = −b₁₀`,
  `b₀₀² + b₀₁² = 1` to 1e-6;
- (c) `‖V·B(θ)·Vᵀ − R‖_F ≤ 1e-6`;
- (d) `‖A·V·B(0)·Vᵀ − A‖_F ≤ 1e-6` and `‖A·V·B(θ)·Vᵀ − B‖_F ≤ 1e-6` (endpoint exactness);
- (e) **degenerate inputs exercised explicitly** — repeated eigenvalues, `θᵢ = 0`, `θᵢ = π` — because
  those are what a hand-written reduction gets wrong and what a random `SO(n)` draw effectively never
  produces. Construct them directly (e.g. `V·B·Vᵀ` with a hand-chosen block set).
- `schurReduceSO` returns **false** for an input not numerically in `SO(n)` (feed it a `det = −1`
  orthogonal matrix and a non-orthogonal matrix).

**Implementation intent (plan §7.4, §7.5).**

- **`M₀` (`t = 0`, Householder / 2D plate):** `M₀ = I − (2/N)·J`, i.e. `x[i] -= (2/N)·Σx`
  (`effects/fdn_reverb.h:749-758`). `det(M₀) = −1` (a single reflection).
- **`M₁` (`t = 0.5`, sign-corrected Hadamard / 3D hall):** build `H_N/√N` with the same FWHT butterfly
  `FDNReverb::applyHadamard` uses (`effects/fdn_reverb.h:696-729`), then **negate row 0**. Row negation
  is left-multiplication by an orthogonal `±1` diagonal ⇒ still exactly orthogonal, `det` flips to `−1`.
- **`M₂` (`t = 1`, seeded random-orthogonal / N-D impossible):** modified Gram–Schmidt over `N×N`
  `Xorshift32::nextFloat()` draws (`core/random.h:59`) seeded
  `deriveStreamSeed(config.seed, kMatrixSalt)` (`core/random.h:102`); compute `det(Q)` by LU with
  partial pivoting and **negate one column whenever `det(Q) > 0`**; redraw if any pivot norm < 1e-4,
  bounded at 8 attempts. Regenerated by `prepare` **and by nothing else** — `setSeed` does not touch it.
- **`schurReduceSO`** — `public static`, plan §7.5's five steps: symmetric part `S = (R+Rᵀ)/2`; cyclic
  Jacobi eigendecomposition to `‖offdiag‖ < 1e-9` with a **hard cap of 30 sweeps**; sort `Λ` descending
  permuting `Q`'s columns; cluster with `|λ_a − λ_b| ≤ 1e-6`; per cluster emit `2×2` blocks using
  `J = (C − λI)/s` with `u₂ = J·u₁` (canonical orientation `[[cosθ, −sinθ],[sinθ, cosθ]]`), handling
  `λ ≈ +1 ⇒ θ = 0` and `λ ≈ −1 ⇒ θ = π` as explicit branches. Allocation-free (`prepare` sized
  everything), bounded iteration.
- **`MatrixMorph`** (nested, private state): at `prepare`, per segment compute `R = A_segᵀB_seg`, run
  `schurReduceSO`, precompute `AV_seg = A_seg·V_seg`. On the control grid,
  `M(u) = AV_seg · B(u·θ) · Vᵀ` with `u = 2t` (segment 1, `t < 0.5`) or `u = 2t − 1` (segment 2).
  Exactly orthogonal by construction — **no re-orthonormalisation step**. Gate the recompute on
  `|t − lastMorphPosition_| > kMorphEpsilon (1e-6)`.
- `copyCurrentMatrix(dst, n)`, `applyCurrentMatrix(in, out)`, `getMatrixOrthogonalityError()` (the
  cached `‖MᵀM − I‖_F` recomputed whenever `matrix_` is materialised), `getCurrentMorphPosition()`.
- **Not shipped, and not to be re-litigated:** lerp + Newton–Schulz (singular at `u = 0.5`; NS has
  `σ = 0` as a fixed point) and the Householder-product factorisation (non-unique path).

**Verify:** both cases green at `N = 8` always-on and `N = 16` under `[.slow]`; zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_Matrix*"` and
`dsp_effects_tests.exe "AetherReverb_SchurReduction"`.

---

## GROUP 4 — FDN core loop

### T004 — Control grid, delay reads, damping/Jot, DC, matrix apply, taps, width, mix, pre-delay, diffuser, injection

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_test.cpp`

**Failing test first** — `TEST_CASE("AetherReverb_Rt60Accuracy", "[effects][aether]")` (SC-005,
FR-030, **FR-031**), P-1 + P-2 + P-3 + P-4:

- Method: Schroeder backward integration of the **G-3** unit impulse response (1.0 at sample 0, both
  channels), wet-only (`setMix(1)`), `damping = 0`, recorded for `≥ 1.2 × decaySeconds`.
- **Clauses A–C (always-on):** measured T60 within **±15 %** of `setDecaySeconds` for
  `decaySeconds ∈ {0.5, 4}` × `size ∈ {0.25, 1}` × `dimensionality = 0.5`; and the measured T60 is
  **monotone non-decreasing** in `setDecaySeconds` across `{0.5, 1, 2, 4, 8}`.
- **Clause C2 (always-on, B-2's full-scope requirement):** one **full 60 s** configuration at
  `size = 0.5`, `setDecaySeconds(60)`, ±15 %.
- **Clause D (always-on — FR-031's only teeth anywhere in the suite):** at `decaySeconds = 4`,
  `size = 0.5`, P-1, **G-2** input (band-limited noise, 80 Hz…11 kHz, peak 0.5, pinned `Xorshift32`
  seed), 5 s recorded per point. Measure banded Schroeder T60 in the **8 kHz** octave and the **250 Hz**
  octave at `damping ∈ {0, 1}`; with `ratio(d) = T60_8k(d) / T60_250(d)`, require
  **`ratio(1) ≤ 0.25 · ratio(0)`**. (FR-031's law is `T60_nyq = T60_dc · 0.05^damping`, i.e. 20×
  shorter at Nyquist at `damping = 1` — 4× is a generous margin.) **Record both ratios and all four raw
  T60s in the test output.** Without this clause nothing in the suite fails if the per-line damping
  one-pole is a no-op.

**Implementation intent (plan §6.1, §6.2, §6.3, §7.6, §7.8, §7.13).**

- **`processStereoBlock` skeleton (plan §6.1) verbatim:** null-pointer guard returns; `n == 0` returns
  with **no** state change; unprepared fills zeros. Then the chunking loop —
  `phase = sampleCounter_ % 64`; `runControlStep()` **only** at `phase == 0`, always advancing every
  modulator/smoother by a **full 64**, never by the slice length; `slice = min(n − done, 64 − phase)`.
  This is the structural basis of SC-011 (plan R-4): `BreathingModulator::processBlock(n)` is
  `advancePhase(n); setTarget(shape); advanceSamples(n)`
  (`processors/breathing_modulator.h:209-216`), so 36+28 ≠ 64.
- **`runControlStep()` order is normative** (plan §6.2 steps 1–11). This task lands steps 2, 4, 5, 6, 7,
  11 (life modulators, freeze machine, shimmer, bloom and the non-finite sweep arrive in later tasks).
- **`renderSlice` order is normative** (plan §6.3 A–C, then per-sample 1–10, then D–F). This task lands
  A (input finiteness guard → `preScratch*`), B (stereo pre-delay, **same** smoothed length on both
  channels), C (`diffuser_.process(preL, preR, diffL, diffR, slice)`), per-sample steps 1–4 and 8–10,
  and slice-level D (width), F (equal-power mix).
- **Damping/Jot (plan §7.6), the `fdn_reverb.h:576-600` formulas re-derived:**
  `T60_nyq = decaySeconds · powf(0.05f, damping)`; per channel with `m = effectiveDelay_[i]`,
  `gDC = powf(10, −3m/(decaySeconds·sr))`, `gNyq = powf(10, −3m/(T60_nyq·sr))`,
  `feedbackGain_[i] = min(gDC, 1.0f)`,
  `ratio = clamp(gDC > 1e-10 ? gNyq/gDC : 1, 0, 1)`,
  `dampCoeff_[i] = clamp(2·ratio/(1+ratio), 0.001f, 1.0f)`.
  **There is no separate base `fbGain`** — the per-line gain *is* `gDC`, which is what makes FR-032's
  "≤ 1.0 at all times outside freeze" structural. Gate the whole recompute on `S`/`decaySeconds`/
  `damping` moving by more than **1e-7** — `16·N` `powf` calls is the heaviest control-grid item.
- **DC blocker:** `dcBlockR_ = 1.0f − 250.0f/sampleRate_` (`effects/fdn_reverb.h:207`).
- **Delay reads:** integer `read(size_t)` when the fractional part is 0 **and** settled; otherwise
  `Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`).
- **Output taps taken BEFORE damping** (`fdn_reverb.h:280-292`, `:356-364`):
  `wetL = (2/N)·Σ delRead[even i]`, `wetR = (2/N)·Σ delRead[odd i]`.
- **Input injection (plan §7.8):** `diffL → EVEN` channels, `diffR → ODD`, each at
  `kInputInjectionGain = sqrt(2/N)` (exactly 0.5 at `N = 8`), added **after** the feedback gain
  (the `fdn_reverb.h:336-338` ordering). Mirrors the even/odd output split so the diffuser's stereo
  image survives the network.
- **Diffuser wiring (FR-041, FR-042):** `diffuser_.prepare(float(sr), maxBlockSamples_)`
  (`processors/diffusion_network.h:242`); per control chunk `setSize(sizeCombined·100)`,
  `setDensity(densitySm·100)`, then **`diffuser_.snapSmoothers()`**
  (`processors/diffusion_network.h:361-369` — its own doc block at `:347-360` requires this of a caller
  that already smooths on its own grid; without it the static fast path at `:534`, `:550` is permanently
  defeated and a second 10 ms lag sits in series). Leave `setModDepth`/`setModRate` at their `0 / 1 Hz`
  defaults — that is also what keeps `DiffusionNetwork` partition-invariant for SC-011.
- **Width + mix (plan §7.13, D-3):** mid/side width on the **wet** signal only
  (`fdn_reverb.h:368-371`), then equal-power `dryGain = cos(mix·π/2)`, `wetGain = sin(mix·π/2)`
  (`:374-377`). All three are **computed once per control chunk** from their smoothers and held constant
  across the slice (FR-019/FR-081 discipline; FR-009's table says "per sample" — plan delta D-3 resolves
  in favour of FR-019).
- **Setter contract, uniform (plan §3):** every setter runs `clamp(isFinite(x) ? x : <FR-009 default>,
  lo, hi)` and writes a smoother target. The private `isFinite` helper is **`ITERUM_NOINLINE`**
  (defined at `primitives/smoother.h:39-45`) and composes `detail::isNaN` / `detail::isInf`
  (`core/db_utils.h:54`, `:175`) — **no fourth reimplementation of a bit test**;
  `node tools/lint-nonfinite-symbols.js` gates it. It is load-bearing, not style: without
  `ITERUM_NOINLINE` the guard is folded away under `-ffast-math` on the macOS leg.
- **Smoother-initialisation rule (binding):** a setter called while `isPrepared()` and before any sample
  has been processed since the last `prepare()`/`reset()` calls **`snapTo(target)`**
  (`primitives/smoother.h:263`) instead of `setTarget`. Track with one `bool anySamplesProcessed_`, set
  on the first non-zero-length `processStereoBlock`, cleared by `prepare()` and `reset()`.
  SC-010 clause 3 and SC-004's materialisation procedure are unsatisfiable without it.
- **Smoother-cadence rule (binding):** every `OnePoleSmoother` is advanced once per control chunk with
  `advanceSamples(64)` (`primitives/smoother.h:243`) — **except** `spectralSm_` (T011).

**Verify:** case green; zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_Rt60Accuracy"`.

---

## GROUP 5 — Echo density (test-only against T004's core)

### T005 — SC-003 echo density and Size-scaled arrival statistics

**Files edited:** `dsp/tests/unit/effects/aether_reverb_spectral_test.cpp` (test only — no header edit)

**Test:** extend `TEST_CASE("AetherReverb_EchoDensity", "[effects][aether]")` (new case in the same TU),
P-1 + P-2 + P-3 + P-4.

Normalised echo density (NED) implemented **exactly as for `FDNReverb`** at
`dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` — 1 ms windows, RMS per window, fraction of windows
above `peak·0.01` — on the **mono sum** of the G-3 impulse response.

**Window derivation (not a fixed constant):** `t_start` = the first 1 ms window above `peak·0.01`
(earlier windows are excluded from the denominator and the excluded count is **recorded**);
`W = max(250 ms, 3·m_long)` with `m_long` read from `getEffectiveDelayLengthSamples(N−1)`.
At `size = 1`, `m_long = 20348` samples = 423.9 ms ⇒ `W = 1.27 s` (matches SC-003's own stated figure).
Note plan delta **D-5**: at `size = 0.5` the derived term (318 ms) governs, not the 250 ms floor — use
the formula as written; only the spec's prose example is wrong. **No threshold moves.**

1. **NED ≥ 0.80** for `size ∈ {0, 0.5, 1} × dimensionality ∈ {0, 1}` at `N = 8` (always-on); the
   5 × 3 × 2 grid under `[.slow]`.
2. NED **non-decreasing** over `density ∈ {0, 0.25, 0.5, 0.75, 1}`, and **strictly lower at 0 than at 1**
   (FR-044's plate-like extreme is SC-003's negative control).
3. **Clause 3(c):** at `density = 0`, the mean inter-arrival time of above-threshold windows scales
   with `S` to within **15 %** across `size ∈ {0.25, 1.0}` (i.e. the ratio of mean inter-arrival times
   ≈ `S(1.0)/S(0.25) = 4/0.25 = 16`… measured as the ratio of the two means against the ratio of the two
   `S` values, 15 % tolerance).

(Clauses 3(a) and 3(b) already landed in T002's `AetherReverb_GeometryAndModalDensity`; do not duplicate
them — cross-reference in a comment.)

**Verify:** case green; zero warnings; always-on renders ≤ ~22 s of audio.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_EchoDensity"`.

---

## GROUP 6 — Freeze and energy conservation

### T006 — Freeze latch (six per-sample crossfade steps), `isFrozen`, FR-034 geometry latch, `getStateEnergy`

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_test.cpp`

**Failing test first** — `TEST_CASE("AetherReverb_FreezeEnergyConservation", "[effects][aether]")`
(SC-002, FR-025, FR-033, C-4), P-2 + P-3 + P-4:

- **Clause 1 (primary, always-on):** config `size = 1`, `dimensionality = 1`, `tideDepth = 1`, `N = 8`.
  Excite, `setFreeze(true)`, let the 50 ms latch complete, then sample `getStateEnergy()` **once per
  second for 60 s**; every sample within **±0.5 dB** of the first post-latch sample.
  **This derivation holds only under plan §7.15's summation window** (`m_i` samples per line, not the
  whole power-of-two section) — state that in a test comment and on the accessor's doc block.
- **Clause 2 (always-on):** wet 1 s-window RMS over the same 60 s within **±1.0 dB** at `tideDepth = 1`
  (hard bound — do not re-derive); and a **positive control** at `tideDepth = 0` within **±0.5 dB`.
- **Clause 3 (always-on):** per-octave level (125 Hz … 8 kHz) at `tideDepth = 0` within **±0.5 dB** over
  60 s, with a **−80 dBFS** reference-window gate and `REQUIRE(qualifiedOctaves >= 6)`.
  **Clauses 2's positive control and 3 need their OWN always-on ~62 s render at `tideDepth = 0`** —
  they cannot ride clause 1's render, which pins `tideDepth = 1` (plan delta **D-10**).
- **Clause 4:** all three sends at 1 + `bloomDecay = 1` + `spectralDiffusion = 0.5` set **before**
  freeze; clause 1's ±0.5 dB bound unchanged. (Sends are muted by FR-033 step 5, which is the point.)
  *Tag this clause `[.slow]` if T016's wall-clock measurement requires demotion step (2).* Clause 4's
  send-dependent assertions become meaningful only after T008/T010; land the structure now with the
  sends at 0 and switch them on in T010.
- **Clause 5 `[.slow]`:** ten enter/leave freeze cycles — ±0.5 dB and **0** `ClickDetector` detections.

Also add `TEST_CASE`-local coverage of **SC-017 clause 3** here (it is a freeze property):
frozen and settled, `setSize(1.0f)` leaves **every** `getEffectiveDelayLengthSamples(i)` unchanged
(≤ 1e-6), and they move again after `setFreeze(false)` settles.

**Implementation intent (plan §7.7, §7.15, §3's ramp-cadence rule).**

- `freezeRamp_` is a `LinearRamp` over `kFreezeLatchMs = 50 ms`, target 1 when frozen, **advanced and
  read PER SAMPLE** inside `renderSlice`'s sample loop with `LinearRamp::process()`
  (`primitives/smoother.h:370-389`). **`LinearRamp` has no `advanceSamples`** — verified: its complete
  public API is `configure/setTarget/getTarget/getCurrentValue/process/processBlock/isComplete/
  snapToTarget/snapTo/reset/setSampleRate` (`primitives/smoother.h:329, 342, 358, 364, 370, 394, 409,
  414, 421, 434, 442`); `advanceSamples` exists only on `OnePoleSmoother` (`:243`). RA-1 forbids adding
  one. A per-chunk advance would step the delay **read pointer** every 64 samples — the staircase
  SC-015 asserts against (plan delta **D-12**).
- **All six FR-033 steps as crossfades on that per-sample ramp:**
  1. `excursion *= (1 − freezeRamp)`
  2. `d = lerp(dynamicDelay, roundf(latchedDelay), freezeRamp)` — at ramp 1 the fractional part is
     **exactly 0**, so reads become integer `read(size_t)`: no interpolation, hence no interpolation
     loss (C-4)
  3. `y = lerp(filtered, raw, freezeRamp)` for **both** the damping one-pole and the DC blocker
  4. `inject *= (1 − freezeRamp)`
  5. **all three sends** `*= (1 − freezeRamp)` — the two `PitchShiftProcessor`s keep running (no state
     discontinuity); only their returns are muted
  6. `g = lerp(feedbackGain_[i], 1.0f, freezeRamp)`
- **FR-036's denormal tickle applies only when `freezeRamp < 1`** — under freeze it is an energy source
  and breaks SC-002.
- `isFrozen()` returns `freezeTarget_ && freezeRamp_.getCurrentValue() >= 1.0f`.
- **FR-034:** while frozen, `setSize`, `setDecaySeconds`, `setDamping` are **accepted and stored but not
  applied** — plan §6.2 steps 4/5 are skipped entirely and `effectiveDelay_` keeps its latched values.
- **`getStateEnergy()` (plan §7.15, delta D-9) — the summation window is normative:**

  ```cpp
  double e = 0.0;
  for (i = 0; i < numChannels_; ++i) {
      const std::size_t m = static_cast<std::size_t>(std::ceil(effectiveDelay_[i]));
      for (std::size_t k = 0; k < m; ++k) {
          const std::size_t idx = sectionOffset_[i] + ((writePos_[i] - 1u - k) & sectionMask_[i]);
          const double s = static_cast<double>(delayBuffer_[idx]);
          e += s * s;
      }
  }
  return static_cast<float>(e);
  ```

  On-demand full sweep inside the `const` accessor, accumulated in `double` (Q8), **never** called from
  `process()`. Summing the whole power-of-two section instead would include up to ~60 % stale history at
  `S = 4` (and ~96 % at `S = 0.25`), plus ~0.7 s of pre-freeze content landing straight inside clause 1's
  ±0.5 dB window.

**Verify:** clauses 1–3 green always-on, clause 5 green under `[.slow]`; zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_FreezeEnergyConservation"`.

---

## GROUP 7 — Life modulation, seeding, determinism

### T007 — `BreathingModulator` / `TidalModulator` / `BrownianDrift` wiring, `setSeed`, `reset()` re-seed

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_test.cpp`

**Failing test first** — two cases in `aether_reverb_test.cpp`:

**`TEST_CASE("AetherReverb_SeededDeterminism", "[effects][aether]")`** (SC-010, FR-073, FR-006), using
`TestHelpers::fingerprintRender` / `compareFingerprints`
(`tests/test_helpers/render_fingerprint.h:64`, `:101`) at the helper's own
`kSampleTolerance = 1.0e-4f` (`:49`) and `kMetricTolerance = 1.0e-5` (`:52`):

1. Same seed, same control sequence ⇒ fingerprints **equal**.
2. (a) `setSeed` **before** `prepare` at `dimensionality = 1` ⇒ **unequal** (exercises the `M₂` path);
   (b) `setSeed` **after** `prepare` at the 0.35 default with
   `sizeBreathDepth = tideDepth = modDepth = 1`, `spectralDiffusion = 0.5` ⇒ **unequal**
   (exercises the modulator and smear streams).
3. `prepare → apply control set H → render A → reset() → render B` (H **not** re-applied) ⇒
   **equal** — this depends on FR-009's smoother-initialisation rule; plus
   `prepare → H → A → prepare(same config) → H → C` ⇒ `C == A`.

**`TEST_CASE("AetherReverb_LifeModulation", "[effects][aether]")`** (SC-017 clauses 1a/2a, FR-070–FR-074):

- **1a (always-on):** one **24 s silent** render (both inputs zero) at `sizeBreathDepth = 1`,
  `size = 0.5`; sample `getEffectiveDelayLengthSamples(0)` every 100 ms; peak-to-peak **> 0** and
  **≥ 80 %** of the depth-implied excursion. **The expectation is in SAMPLES** (plan delta **D-11**):
  `refDelaySamples_[0] · (S(clamp(size + depth·b_max, 0, 1)) − S(clamp(size + depth·b_min, 0, 1)))`.
  Obtain `refDelaySamples_[0]` as `getEffectiveDelayLengthSamples(0) / S(size)` measured at
  `sizeBreathDepth = 0`. `b_max`/`b_min` come from a **test-owned `BreathingModulator`** prepared at the
  same rate, seeded `deriveStreamSeed(config.seed, AetherReverb::kBreathSalt)`, `setRate(0.05f)`,
  advanced by `processBlock(64)` on the same grid. **Record the numeric expectation next to the
  measurement.**
- **1a controls (second 24 s render):** `sizeBreathDepth = 0`, `size = 0.5`, `setModDepth(1)`,
  `setModSmoothness(0.0)` (⇒ `tau = 0.2 s`, `processors/brownian_drift.h:97`, so the drift fully
  resolves inside the window), `tideDepth = 0`:
  (i) channel **0** flat to ≤ **1 sample** p-p (the FR-070 depth-0 control; channel 0 is by construction
  *not* drift-modulated); (ii) `getEffectiveDelayLengthSamples(N−1)` p-p in
  `(0, 1.2 · kModExcursionFraction · refDelaySamples_[N−1] · S(0.5)]` — at `N = 8`, 48 kHz that is
  **`(0, 30.5]` samples**.
- **Third render, 2 s, same config but `setModDepth(0)`:** channel `N−1` flat to ≤ **1e-6** samples.
  Clauses (ii)+(iii) are the **only** place `setModDepth` / `setModSmoothness` are exercised
  functionally.
- **2a (always-on):** on the same renders, sample `getCurrentMorphPosition()` every 100 ms over the
  first 10 s: p-p **≥ 0.05** at `tideDepth = 1` (derivation: layer 0's 30 s period traverses 120° in
  10 s; a 120° arc of a sine spans ≥ half its amplitude; layer 0's amplitude is
  `kLayerWeight · kSinePairScale · 2 = 1/3` per `processors/tidal_modulator.h:136-138` ⇒ ≥ 0.167, >3×
  margin) and **≤ 1e-6** at depth 0.
- **1b/2b/4 `[.slow]`:** the 120 s grids; clause 4 repeats them with **G-2** input requiring the spreads
  to match the silent renders within **5 %**.

(SC-017 clause 3 landed in T006.)

**Implementation intent (plan §7.12, §5.1 step 12, §5.2).**

| | source | rate | depth control | applied |
|---|---|---|---|---|
| Size breathes | `BreathingModulator` | **pinned `setRate(0.05f)`** ⇒ 20 s period | `setSizeBreathDepth` | added to smoothed Size **before** the `S(v)` mapping; combined value clamped to [0,1] |
| Matrix tides | `TidalModulator` | **pinned `setRate(1.0f)`** ⇒ base 30 s, layers 30 / 42.43 / 51.96 s | `setDimensionalityTideDepth` | added to smoothed Dimensionality **before** the [0,1] clamp |
| Delay jitter | `BrownianDrift` × `N/2` (channels `i ≥ N/2`) | **no rate control exists** — `setModSmoothness` ⇒ `tau = lerp(0.2, 30) s` | `setModDepth` | `± modDepth · kModExcursionFraction · that channel's own current length` |

- Both rates are **pinned in `prepare`, not inherited**: the class defaults are `0.1f`
  (`processors/breathing_modulator.h:111`) and `0.5f` (`processors/tidal_modulator.h:143`). Pinning is
  what makes SC-017's thresholds derivable from the classes' own constants. There is **no** rate setter
  on `AetherReverb` for either.
- Both modulators' **own** depths stay at their class defaults `1.0f` so nothing is multiplied twice.
- `BrownianDrift` has **no rate setter** — verified, its complete public API is
  `prepare/reset/setSeed/setSmoothness/setDepth/setMean/process/processBlock/getCurrentValue/
  getSourceRange` (`processors/brownian_drift.h:121, 133, 145, 152, 159, 165, 178, 194, 212, 217`).
  `setModSmoothness` forwards **verbatim**; no Hz domain is invented or advertised. The header states
  the reachable `tau ∈ [0.2 s, 30 s]`; the 0.6 default gives `tau ≈ 18 s`.
- **FR-072 deliberately differs from `FDNReverb`**, which applies `modDepth · 5 %` of the **longest**
  line to every modulated channel (`effects/fdn_reverb.h:631`): at this phase's Size range the longest
  line is up to 424 ms, so 5 % of it is 21 ms of excursion applied to a 5 ms line. Per-line and ten
  times smaller (`kModExcursionFraction = 0.005f`) is the correction; the header records the reason.
- **FR-074: modulators advance unconditionally** — there is **no** input-activity gate. SC-017 renders
  silence and requires the introspection values to move, so a stubbed or input-gated modulator fails on
  **every** build.
- **`setSeed` / `reset()` (plan §5.2):** `reset()` re-seeds `breath_` (`kBreathSalt`), `tide_`
  (`kTideSalt`), every `drift_[j]` (`kDriftSaltBase + j`), `smearRngL_/R_` (`kSmearSaltL/R`) via
  `deriveStreamSeed(seed_, salt)` and then calls each object's own `reset()`. The **derived** seed must
  be re-applied explicitly — that is what makes a post-`reset` render match the original. `reset()`
  **preserves** every FR-009 control target (`snapToTarget()` on each smoother) and restores the morph
  position to the **current Dimensionality target**, not the 0.35 default. `M₂` is **not** regenerated by
  `reset()` or `setSeed` — `prepare` only.

**Verify:** both cases green always-on; zero warnings.
**Test target:** `dsp_effects_tests` —
`dsp_effects_tests.exe "AetherReverb_SeededDeterminism"`, `dsp_effects_tests.exe "AetherReverb_LifeModulation"`.

---

## GROUP 8 — Shimmer taps

### T008 — Two `PitchShiftProcessor` taps, 64-sample cadence, pinned injection subsets, HF shelf

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_test.cpp`,
`dsp/tests/unit/effects/aether_reverb_spectral_test.cpp`

**Failing test first** — two cases:

**`TEST_CASE("AetherReverb_ShimmerRegenerationStability", "[effects][aether]")`** (SC-006, FR-058,
FR-059) in `aether_reverb_test.cpp`: 5 s **G-1** (220 Hz + 2×…9× at `1/n`, all sine, zero phase,
peak 0.5) then **175 s of silence**, all sends at 1, `decay = 60`, `damping = 0`, `density = 1`.
Let `E1` = the excited segment, `E2` = the window at `t = 10…30 s`, `E_final` = the last 20 s.

1. peak ≤ **4.0**;
2. peak and RMS of `E_final` ≤ **0.95 ×** those of `E2`, **and** the 20 s-window RMS sequence
   non-increasing from `E2` onward;
3. `HF(E_final) ≤ 1.25 × HF(E1)` as a **fraction** of total energy, plus spectral centroid
   ≤ **1.25 ×** that of `E1`;
4. **0** non-finite output samples and `getNonFiniteRecoveryCount() == 0`.
   **Record every measured figure.** (If T016's wall-clock measurement requires demotion step (1), the
   always-on form becomes a 90 s tail and the 180 s form moves to `[.slow]` — never deleted.)

**`TEST_CASE("AetherReverb_ShimmerBloomEffect", "[effects][aether]")` clauses 1–2** (SC-016, FR-050–FR-054)
in `aether_reverb_spectral_test.cpp`: input **G-4** (one 220 Hz sine, peak 0.5, 2 s) + 6 s tail, analysis
on the last 4 s; all levels expressed relative to an otherwise identical render with the send under test
at 0.

1. **Octave send at 1:** (a) `L(2f₀) ≥ L_ref(2f₀) + 12 dB`; (b) `L(2f₀) ≥ L(f₀) − 20 dB` (the
   scale-free "this is a real signal, not an amplified floor" anchor); (c)
   `L(1.5f₀) ≤ max(L_ref(1.5f₀) + 3 dB, L(2f₀) − 12 dB)`.
2. **The exact mirror for the fifth send** — together these measure FR-051's *independent* sends; a
   single shared gain fails both. **Record every band level and `L(f₀)`.**

(Clause 3 = bloom is T010; clause 4 = freeze mute is T010.)

**Implementation intent (plan §7.9, §5.1 steps 3 and 9).**

- **Two `PitchShiftProcessor` instances total, not four.** They run on a **mono sum**, so stereo costs
  nothing extra (`ShimmerDelay` pays one per channel at `effects/shimmer_delay.h:88-89`; this halves it).
- `shimmerAllocated_ = config.shimmerEnabled && sampleRate_ >= 44100.0`. **If false, no
  `PitchShiftProcessor` is prepared and no tap scratch is allocated** (RA-6 — it saves ≈ 0.8–1.0 MiB,
  not just CPU: `PitchShiftProcessor::prepare` unconditionally prepares all four internal shifters,
  `processors/pitch_shift_processor.h:1213-1216`, including the phase vocoder's fixed 4096-point STFT).
- `prepare`: `shifterOctave_.prepare(sampleRate_, kControlChunkSamples)`,
  `setMode(config.shimmerMode)`, `setSemitones(12.0f)`, `setCents(0.0f)`; the fifth at `7.0f`.
  `maxBlockSize = 64` satisfies the documented `[1, 8192]` precondition
  (`processors/pitch_shift_processor.h:139-142`).
- **Pinned channel subsets and gains:**

  | constant | `N = 8` | `N = 16` |
  |---|---|---|
  | read subset (four longest) | `{4,5,6,7}` | `{12,13,14,15}` |
  | `kShimmerOctaveInjectChannels` (+12) | `{1, 4}` | `{1, 8}` |
  | `kShimmerFifthInjectChannels` (+7) | `{3, 6}` | `{3, 12}` |
  | tap read normalisation | `kTapReadNormalisation = 0.25f` | same |
  | injection gain | `sqrt(2/2) = 1.0` — **the send IS the injected gain** (FR-051) | same |

  Each pair **spans both parities** (1 odd + 4 even; 3 odd + 6 even; at `N = 16`, 1+8 and 3+12), so
  neither interval is hard-panned by the even→L / odd→R output split.
- **Cadence (Q5), structural:** the mono tap sum is accumulated over one control chunk into
  `tapSumScratch_[0..64)`; at the **next** chunk boundary `process(in, out, 64)` is called once per tap
  and the result injected across that chunk. Each leg carries **64 samples of deferral on top of its
  mode latency**. 64 matches the shifter's own `kSmoothingSubBlockSize`
  (`processors/pitch_shift_processor.h:165`), and anchoring to `sampleCounter_` (not to caller blocks)
  is what makes SC-011 **structural**.
- **Header loop-time table (FR-054), at 48 kHz** — mode latency (`:280-287`) **+ 64**:
  `Simple` 0+64 = 1.33 ms (documented as artifact-prone in a recirculating loop);
  **`Granular` (default)** ≈2048+64 = 2112 ≈ **44 ms**; `PhaseVocoder` 4096+1024+64 = 5184 ≈ **108 ms**.
  Mode is a **`prepare`-time choice only** (FR-053) — `getLatencySamples()` changes immediately on
  `setMode` (`:189-193`) and a loop whose latency changes mid-render is a click.
- **FR-059 HF shelf** on each return path (one state per path): first-order high shelf, corner **6 kHz**
  (clamped ≤ `0.45·sr`), HF gain **0.5 (−6 dB)**. This is plan R-1's **only admissible lever** if SC-006
  clause 3's HF-fraction bound fails — lower the corner and/or the shelf gain and **record the shipped
  values in the header and `compliance.md`**. It is a header constant, not a spec threshold.
- **FR-052, stated accurately in the header:** comb filtering does not arise **not** because the read and
  inject subsets are disjoint (the endpoints are dense — after one sample step every channel already
  carries a contribution from every other), but because **a +12 or +7 copy is not a coherent copy of the
  signal it came from**. The pinned subsets buy stereo re-diffusion of a mono tap and a measurable
  injected level.

**Verify:** both cases green; zero warnings.
**Test target:** `dsp_effects_tests` —
`dsp_effects_tests.exe "AetherReverb_ShimmerRegenerationStability"`,
`dsp_effects_tests.exe "AetherReverb_ShimmerBloomEffect"`.

---

## GROUP 9 — Block-partition invariance

### T009 — SC-011 (test-only against T004+T007+T008)

**Files edited:** `dsp/tests/unit/effects/aether_reverb_test.cpp` (test only — no header edit)

**Test:** `TEST_CASE("AetherReverb_BlockPartitionInvariance", "[effects][aether]")` (SC-011, FR-005,
FR-050).

Render **48 000 samples** twice from an identically-prepared, identically-seeded engine: once as a
single call, once in the repeating partition `{1, 7, 64, 65, 511, 512, 513, 2048}`. Sample-wise max
absolute difference ≤ **1e-6** on both channels.

Configuration (b): **all life modulation active** (`sizeBreathDepth = 1`, `tideDepth = 1`,
`modDepth = 1`) **and both shimmer sends at 1**. Input G-2.

If this fails, the cause is one of the three documented traps (plan R-4): control work not anchored to
`sampleCounter_ % 64 == 0`; a modulator advanced by the slice length instead of a full 64
(`BreathingModulator::processBlock` inserts a `setTarget` per call,
`processors/breathing_modulator.h:209-216`, so 36+28 ≠ 64 — the same applies to `TidalModulator`
`:250-257` and `BrownianDrift` `:194-206`); or the shimmer `process(…, 64)` call not on the absolute
grid. **Do not relax the bound** — fix the cadence.

**Verify:** case green (≈2 s of audio); zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_BlockPartitionInvariance"`.

---

## GROUP 10 — Harmonic bloom

### T010 — Bloom bank (SIMD kernel, note API, stability guard, reclaim), `kBloomSendMax` tuning

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_spectral_test.cpp`,
`dsp/tests/unit/effects/aether_reverb_test.cpp` (SC-002 clause 4 sends switched on)

**Failing test first** — extend `TEST_CASE("AetherReverb_ShimmerBloomEffect", …)` in the spectral TU:

3. **Bloom:** `bloomNoteOn(0, {f₀, 2f₀, 3f₀, 4f₀}, 4)` before the render, `bloomSend = bloomDecay = 1`,
   `REQUIRE(getActiveBloomResonatorCount() > 0)` throughout. The four 1/3-octave target bands rise
   **≥ 6 dB** relative to the send-0 reference, while the mean of **all** non-target bands in
   [100 Hz, 10 kHz] rises **≤ 2 dB**. Then `bloomNoteOff(0)` and, after settling, the four bands fall
   back within **2 dB** of the reference.
   **Clause 3 runs in TWO configurations** — the default (spectral stage enabled) **and**
   `spectralDiffusionEnabled = false`, with the **same** ≥ 6 dB emphasis required in both, plus
   `REQUIRE(getLatencySamples() == 0)` in the second. That is **FR-065's only teeth**: no other case
   combines an active bloom with the spectral stage disabled.
4. **Freeze mutes all three sends:** after freezing, the ±50-cent band at `2f₀` stops growing — its
   level over the last 5 s of a 15 s frozen tail is within **±0.5 dB** of its level at latch completion.

Also switch SC-002 clause 4 (T006) to its real configuration: all three sends at 1, `bloomDecay = 1`,
`spectralDiffusion = 0.5` set **before** freeze; clause 1's ±0.5 dB bound unchanged.

**Implementation intent (plan §7.10).**

- **Kernel:** the reused free function
  `processSympatheticBankSIMD(y1s, y2s, coeffs, rSquareds, gains, count, scaledInput, sums,
  releaseCoeff, envelopes)` (`dsp/include/krate/dsp/systems/sympathetic_resonance_simd.h:39-50`) —
  plain arrays, no ownership, legally callable from Layer 4. Called with
  `count = kMaxBloomResonators` **unconditionally**; inactive slots hold `coeff = gain = y1 = y2 = 0`
  and contribute nothing (exactly as `SympatheticResonance::process` does,
  `systems/sympathetic_resonance.h:326-333`).
- **Coefficients (FR-057), re-derived** because the originals are `private static` below
  `systems/sympathetic_resonance.h:383`:

  ```
  Q       = 20.0f * powf(20.0f, bloomDecay)          // 0..1 -> [20, 400]
  Q_eff   = Q * clamp(500.0f / f, 0.5f, 1.0f)        // :440-446, :58, :61
  r       = expf(-kPi * (f / Q_eff) / sr)            // :430
  coeff   = 2r*cos(2*pi*f/sr) ;  rSquared = r*r      // :431-435
  peakInv = (1 - r) * sqrt(1 - 2r*cos(2w) + r*r)     // :401-420, cos(2w) via the double-angle
                                                     //   identity on coeff/(2r), kDenormalGuard
                                                     //   from core/audio_constants.h:40
  ```

  `bloomReleaseCoeff_ = expf(-1.0f / (0.010f * sr))` — the 10 ms release
  `SympatheticResonance::prepare` uses (`systems/sympathetic_resonance.h:117`).
- **Note API (FR-056, RA-7)** — `bloomNoteOn(std::int32_t voiceId, const float* partialHz,
  std::size_t count)` / `bloomNoteOff(std::int32_t voiceId)`, both audio-thread-callable,
  allocation-free, `noexcept`:
  - `partialHz == nullptr`, `count == 0`, or `!isPrepared()` ⇒ **no-op**;
  - `count` clamped to `kMaxBloomResonators` (**32**);
  - every frequency tested with `detail::isNaN` / `detail::isInf` and clamped to `[20 Hz, 0.45·sr]`
    **before any coefficient computation**, so no non-finite coefficient can reach the kernel;
  - a `voiceId` already live **replaces** its own partial set; a full bank (`kMaxBloomVoices = 8`)
    retires its **oldest** voice by `bloomVoiceAge_`;
  - `bloomNoteOff` sets those slots' `gains[k] = 0` and `bloomDriven_[k] = false`; the resonator rings
    down naturally (`r < 1`) and the **control-grid** reclaim pass frees the slot when
    `bloomEnv_[k] < kReclaimThresholdLinear = 1.585e-5f`
    (`systems/sympathetic_resonance.h:52`; the reclaim loop at `:337-352` is moved to control rate here
    because per-sample branching would defeat the SIMD loop). Click-free by construction;
  - `getActiveBloomResonatorCount()` counts **driven** slots, so it drops on note-off;
  - accepted while frozen, but the return is muted for the freeze's duration.
- **Stability guard (FR-058), as a computable criterion** — two multiplicative factors, both recomputed
  on the control grid: (1) per-resonator `gains[k] = peakInv(coeffs_k, f_k)`, so a high-Q resonator
  contributes **unit** gain at its own centre frequency; (2) a global `1/√count` on the summed return.
  Then per control chunk:

  ```
  g_bloom(f_k) = kTapReadNormalisation (0.25) * (1/sqrt(count)) * sendGain
               * kTapInjectionGain(bloomSubset) * hfShelfMagnitude(f_k)
  worst        = max over active k of  feedbackGain_[bloomChannel] * g_bloom(f_k)
  bloomGuardScale_ = (worst > kBloomLoopGainCeiling) ? kBloomLoopGainCeiling / worst : 1.0f
  ```

  `kBloomLoopGainCeiling = 0.95f` (strictly inside FR-058's "≤ 1.0", with margin for smoother lag and
  single-precision rounding). One scalar on the summed return, so the bank's relative tuning is
  untouched and no resonator is silently detuned or dropped.
- **Injection subset:** `kBloomInjectChannels = {0, 2, 5, 7}` at `N = 8` (the remaining 12 at `N = 16`),
  injection gain `sqrt(2/|subset|)` = **0.7071** at `N = 8`, **0.4082** at `N = 16`.
- **`kBloomSendMax` — the one constant this phase expects to tune against a measurement (plan R-2).**
  `sendGain = v · kBloomSendMax`. With the pinned normalisations, `v = 1` and four held partials gives
  `g_bloom ≈ 0.25 · 0.5 · 0.7071 · kBloomSendMax = 0.0884 · kBloomSendMax`, and the steady-state
  emphasis at `f_k` is ≈ `1/(1 − g_line·g_bloom)`. **Start at `kBloomSendMax = 8.0f`** (targets ≈0.71
  loop gain ⇒ ≈10.7 dB, comfortably above 6 dB and below the guard ceiling). **If clause 3 does not reach
  6 dB, the admissible fixes are this constant and the normalisations — NEVER the criterion (B-4).**
  Record the finally-shipped value and the measured emphasis in the header **and** in `compliance.md`.

**Verify:** SC-016 clauses 1–4 green; SC-002 clause 4 still green with sends live; SC-006 still green
(the bloom is inside the same loop); zero warnings.
**Test target:** `dsp_effects_tests` — `dsp_effects_tests.exe "AetherReverb_ShimmerBloomEffect"` then
`dsp_effects_tests.exe "AetherReverb_FreezeEnergyConservation"` and
`dsp_effects_tests.exe "AetherReverb_ShimmerRegenerationStability"` as regressions.

---

## GROUP 11 — Spectral diffusion, latency, dry alignment

### T011 — STFT ↔ OverlapAdd phase smear, `g(a)` make-up, dry-path alignment, `getLatencySamples`

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_spectral_test.cpp`,
`dsp/tests/unit/effects/aether_reverb_test.cpp`

**Failing test first** — two cases:

**`TEST_CASE("AetherReverb_TailSmoothness", "[effects][aether]")`** (SC-007, FR-060–FR-063, FR-052,
FR-080) in the spectral TU. Metric **M-1** implemented **locally** (non-overlapping 4096-sample frames,
Hann, real FFT, bins in **[80 Hz, 11 kHz]**, per frame `exp(mean(ln|X|))/mean(|X|)`; M-1 is the mean
over frames, and the test records the frame count and `SE = stddev/√frames`).
**Do not delegate to `calculateSpectralFlatness`** (`tests/test_helpers/signal_metrics.h:326`): it picks
one FFT size capped at 4096 and windows only the **first** `fftSize` samples (`:337`, `:351`), and it
computes `geomMean/arithMean` over **all** non-DC bins (`:397`) — its own ceiling on ideal white noise
is ≈0.845, below the 0.85 an absolute threshold would need.

1. Across `spectralDiffusion ∈ {0, 0.25, 0.5, 0.75, 1}` with G-2 input:
   (a) M-1 **non-decreasing**; (b) per-bin peak-to-median in dB **falls by ≥ 3 dB** from amount 0 to 1;
   (c) `M-1(1) − M-1(0) ≥ 3·√(SE₀² + SE₁²)` — a significance requirement, so a stub (difference exactly
   0) fails unconditionally. **Record `M-1(G-5)`** (the same generator used directly, never through the
   engine) as the empirical ceiling.
2. L/R correlation **non-increasing** over the sweep.
3. At amount **0**, the wet output matches a `fftSize`-delayed reference rendered with
   `spectralDiffusionEnabled = false` to **per-sample ≤ 1e-4** and **error RMS ≤ −70 dBFS**, with a
   **negative control** at 50 % overlap (which `primitives/stft.h:225-228` forbids for
   synthesis-windowed modification) that must exceed **both** bounds.
4. (a) 1/3-octave bands with centres in [100 Hz, 10 kHz]:
   `level(b) ≤ median{b−2, b−1, b+1, b+2} + 9 dB`, at `damping = 0.4`, `density = 0.7`, **all sends 0**.
   (b) **FR-052 comb check:** the same analysis at both shimmer sends = 1, `bloomSend = 0`, `size = 0.5`,
   with a **notch** bound — no band more than **9 dB below** its neighbour median.
5. Wet RMS over the last 2 s varies ≤ **1.0 dB** across the five amounts. **This is what verifies
   `g(a)`** — without it the ~6 dB coherence loss ships silently.
6. **FR-080 / `setWidth` (no new render — swept on clause 3's amount-0 G-2 render):** L/R correlation of
   the wet tail at `setWidth ∈ {0, 1}`; **≥ 0.999 at width 0** (M/S collapse: `wetL = wetR = mid`
   exactly) and **strictly lower at width 1**. Record both values. Without this clause an unwired
   `setWidth` ships green.

**`TEST_CASE("AetherReverb_LatencyAndDryAlignment", "[effects][aether]")`** (SC-018, FR-084, FR-062,
FR-015) in `aether_reverb_test.cpp`:

1. `getLatencySamples() == diffusionFftSize` at `{256, 1024, 4096}` and **exactly 0** when disabled;
   constant across the whole control table (drive every setter and re-read).
2. `setMix(0)` ⇒ input↔output cross-correlation peak at lag `getLatencySamples() ± 1` with peak
   correlation **≥ 0.999**; repeated with the stage disabled (expected lag **0**).
3. `setMix(0.5)` ⇒ a **single** peak — no secondary above **0.2** of the primary within `±2·fftSize`.
4. **FR-015 / `setPreDelayMs` (the only place it is measurable):** `setMix(1.0f)`,
   `spectralDiffusionEnabled = false`, `density = 0`, **G-3** impulse — the first wet sample above
   `peak·0.01` occurs at **100 ms ± 1 ms** with `setPreDelayMs(100.0f)`, and at ≈0 ms with
   `setPreDelayMs(0.0f)`. Clause 2 structurally cannot see the pre-delay (it renders dry-only while the
   pre-delay sits on the **wet** path), so without this clause an entirely unwired `setPreDelayMs` ships
   green.
5. **FR-062's warm-up offset:** with `setMix(1.0f)` and the stage enabled, the first
   `getLatencySamples()` wet samples are **exactly `0.0f`**.

**Implementation intent (plan §7.11).**

- **Topology:** `prepare`-time flag `spectralDiffusionEnabled`, default **true**, **no runtime toggle**
  (a latency that changes mid-render is a click plus a host renegotiation). One stereo
  `STFT → phase-smear → OverlapAdd` stage on the **wet** path at `fftSize = diffusionFftSize_`,
  `hop = fftSize/4` (75 % overlap), with **`applySynthesisWindow = true`** — mandatory at 75 % and
  **forbidden** at 50 % per `primitives/stft.h:225-228`.
- **Pump, per slice:** `pushSamples(wet, slice)`; `while (stftL_.canAnalyze())` →
  `spectralSm_.advanceSamples(diffusionHopSize_)` (FR-064's cadence), read `a`, `analyze(spec)`,
  `smear`, `synthesize(spec)`, pull `hop` samples, scale by `g(a)`, push into `wetFifo_`; then read
  `slice` samples out. `STFT::analyze` consumes exactly `hopSize` and `OverlapAdd::synthesize` marks
  `hopSize` ready, so the pump is self-balancing and analysis boundaries depend only on the **total**
  pushed count ⇒ partition-invariant.
- **FIFO underflow rule (normative):** `canAnalyze()` requires `samplesAvailable_ >= fftSize_`
  (`primitives/stft.h:134-138`), so no frame exists until `fftSize` samples have been pushed while the
  consumer demands one output per input from the first slice. When `wetFifoCount_ < slice`: **emit
  `0.0f`** for the missing samples and do **not** advance `wetFifoRead_` past `wetFifoWrite_`. This
  zero-fill is exactly what establishes the `fftSize` offset the dry path aligns to (SC-018 clause 5).
- **Smear (FR-061), redrawn EVERY hop — not drawn once and held:**
  `spec.setPhase(bin, spec.getPhase(bin) + a * rng.nextFloat() * kPi)` for every bin, per channel, with
  independent `smearRngL_/R_`. **Magnitudes are never modified.** A held draw is a static dispersive
  allpass (fixed colouration, no time smearing); a redrawn draw decorrelates successive frames. The two
  are audibly and measurably different — this is normative.
- **Coherence make-up `g(a)`:** independently randomised per-frame phases sum incoherently, so
  `OverlapAdd`'s fixed COLA factor (`primitives/stft.h:243-262`, applied unconditionally at `:299-307`)
  is wrong by a level growing with `a`. Ship these five knots and interpolate with
  `Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`), end tangents clamped:

  | `a` | 0 | 0.25 | 0.5 | 0.75 | 1.0 |
  |---|---|---|---|---|---|
  | measured `outRMS/inRMS` | 1.0000 | 0.9260 | 0.7443 | 0.5635 | 0.5001 |
  | **`g(a)`** | **1.0000** | **1.0799** | **1.3435** | **1.7746** | **1.9996** |

  Applied as a **scalar on the pulled time-domain samples**, never on the bins, so FR-061 holds
  literally. The interpolant is monotone (Catmull-Rom tangents 0.1718 / 0.3474 / 0.3281 are each inside
  the Fritsch–Carlson bound). The table is expected to transfer unchanged to `fftSize ∈ {256, 4096}`
  because the coherence loss depends on window and overlap count, not on `fftSize`.
- **Latency and dry alignment:** `getLatencySamples()` returns
  `spectralEnabled_ ? diffusionFftSize_ : 0`, **constant for a prepared configuration**. The dry path
  runs through `dryAlignL_/R_` at exactly `fftSize` samples so the engine reports **one** latency.
  `dryAlign*.prepare(sampleRate_, static_cast<float>(static_cast<double>(diffusionFftSize_) /
  sampleRate_))` — the cast is **explicit**: `DelayLine::prepare` is `(double, float)`
  (`primitives/delay_line.h:86`), and `float(fftSize)/sr` would pass a `double` (C4244). The shimmer
  taps' latency is **not** included — they live inside the feedback loop.
- **`spectralSm_` is the one `OnePoleSmoother` advanced per STFT frame** with
  `advanceSamples(hopSize_)`, **never** once per `process()` — that would stretch its 100 ms constant to
  ~25 s at the default hop.

**Verify:** both cases green; zero warnings.
**Test target:** `dsp_effects_tests` —
`dsp_effects_tests.exe "AetherReverb_TailSmoothness"`, `dsp_effects_tests.exe "AetherReverb_LatencyAndDryAlignment"`.

---

## GROUP 12 — `silence()`, `emergencyClear()`, non-finite hygiene

### T012 — Amortized state clear, FR-082/FR-083 guards, the `KRATE_DSP_AETHER_TEST_HOOKS` fault hook

**Files edited:** `dsp/include/krate/dsp/effects/aether_reverb.h`,
`dsp/tests/unit/effects/aether_reverb_nonfinite_test.cpp`

**Failing test first** — two cases in the `-fno-fast-math` TU. **NaN and ±Inf are built from bit
patterns through a `volatile` sink — never `std::numeric_limits`**, which folds under the macOS leg's
`-ffast-math`.

**`TEST_CASE("AetherReverb_NonFiniteHygiene", "[effects][aether]")`** (SC-014). Pinned configuration
(clause 3's quantity is meaningless without it): P-2, P-3, `N = 8`, `decay = 4`, `damping = 0.4`,
`size = 0.5`, `dimensionality = 0.35`, all sends 0, `spectralDiffusion = 0`, life modulation at P-1.
Fault at **`t_f = 3.0 s`** of a **10 s** render.

1. **No** non-finite value ever reaches the output, at any point.
2. Injecting **input** NaN/Inf only ⇒ `getNonFiniteRecoveryCount()` stays **0** (FR-082 is not FR-083).
3. **Recovery from an INTERNAL fault via the hook.** Call `injectNonFiniteStateForTest()` at
   `t_f = 3.0 s` — at 48 kHz that is sample **144 000 = 64 × 2250**, exactly a control-chunk boundary
   (the hook's documented precondition). Render a clean **reference** (no injection) and the **subject**
   (fault at `t_f`, G-1 continuing uninterrupted). Assert `getNonFiniteRecoveryCount() == 1` (it was 0
   before). Time-align the reference to the subject's **recovery point** (the first sample at which both
   `clearPending_ == false` and the fade-in has completed). Then (a) wet RMS over the 100 ms window
   ending at `recovery + 1.0 s` is non-zero and within **±3 dB** of the aligned reference window;
   (b) that difference **shrinks monotonically** over the four preceding 100 ms windows. **The measured
   convergence time is recorded, not thresholded.**
4. **Setter guards under IEEE semantics** (FR-009's clamp, FR-008's non-finite rule) — the only place in
   the suite where the `ITERUM_NOINLINE isFinite` guard is exercised with a non-finite argument.
   For **every** float setter in FR-009's table (`setSize`, `setDensity`, `setDecaySeconds`,
   `setDimensionality`, `setDamping`, `setPreDelayMs`, `setModDepth`, `setModSmoothness`,
   `setShimmerOctaveSend`, `setShimmerFifthSend`, `setBloomSend`, `setBloomDecay`,
   `setSpectralDiffusion`, `setSizeBreathDepth`, `setDimensionalityTideDepth`, `setWidth`, `setMix`):
   (a) **NaN sub-case** — call every setter with bit-pattern NaN, render 1 s of G-1: **0** non-finite
   output samples, `getNonFiniteRecoveryCount() == 0`, **and** `compareFingerprints` **equal** to a
   render of the same engine with **no setter ever called**. That equality is what proves the argument
   fell back to the FR-009 **default** rather than landing on a clamp endpoint.
   (b) **±Inf and ±1e9 sub-cases** — three further 1 s renders: 0 non-finite output,
   `getNonFiniteRecoveryCount() == 0`, peak ≤ **4.0**. These clamp to range endpoints, so **no**
   fingerprint equality is asserted.

**`TEST_CASE("AetherReverb_BloomNoteApi", "[effects][aether]")`** (FR-056's five normative guards,
Edge cases 27–31 — none of them tested anywhere else):

- (a) `bloomNoteOn(0, nullptr, 4)`, `bloomNoteOn(0, partials, 0)` and a call **before `prepare()`** are
  all no-ops — `getActiveBloomResonatorCount() == 0`, and `isPrepared() == false` in the third;
- (b) `bloomNoteOn(0, partials, 64)` clamps to **32** active slots with **no out-of-bounds write** —
  run inside the §8.2 bracketing `AllocationScope` idiom, and under the ASan lane's always-on core;
- (c) partials of bit-pattern NaN, ±Inf, `0.0f`, `−440.0f` and `0.9·sr`, followed by a 1 s G-1 render ⇒
  **0** non-finite output samples and `getNonFiniteRecoveryCount() == 0`;
- (d) a repeat `bloomNoteOn(0, …)` for the same `voiceId` leaves `getActiveBloomResonatorCount()`
  **unchanged**, not doubled; `bloomNoteOff(7)` for a `voiceId` never noted on is a no-op.

**Implementation intent (plan §5.3, §7.14).**

- **`silence()` — three-phase, NON-latching (FR-007):**
  1. `gate_ = FadingOut`; `outputGate_.setTarget(0)` over `kSilenceRampMs = 20 ms` **per sample**;
     `clearPending_ = true`, `clearCursor_ = 0`, `clearStage_ = 0`. The O(N)- and
     O(`kMaxBloomResonators`)-sized scalar state is cleared **immediately** (a few hundred floats).
  2. The bulk clear runs **amortized across the fade**, one work unit per `runControlStep()`: a
     `std::fill` of the next `clearQuotaFloats_` slab of `delayBuffer_`, **and** at most one deferred
     sub-object `reset()` in the fixed order `{preDelayL_, preDelayR_, diffuser_, stftL_, stftR_, olaL_,
     olaR_, specL_, specR_, wetFifoL_/R_, dryAlignL_, dryAlignR_, shifterOctave_, shifterFifth_}`.
     `clearQuotaFloats_ = ceilDiv(delayBuffer_.size(), max(1, (kSilenceRampMs·sr/1000)/64))`, sized at
     `prepare`. **This is a wall-clock requirement, not a nicety:** the clear is 1–5 MiB of `memset`
     while `maxBlockSamples` admits 64, i.e. a deadline of **1.33 ms at 48 kHz and 0.33 ms at 192 kHz**.
  3. When `outputGate_ == 0` **and** `clearPending_ == false` (both tested at the next control step):
     `gate_ = FadingIn`, `outputGate_.setTarget(1)` over the same 20 ms, then `gate_ = Open`.
  While `clearPending_`, the render loop writes **literal `0.0f`** into the delay lines instead of
  `chanIn_` and forces the wet contribution to **literal `0.0f`** — **assignments, not `× 0` products**,
  so a value not yet reached by the cursor cannot leak through. `silence()` during freeze abandons the
  latch (`freezeTarget_ = false`, `freezeRamp_.snapTo(0)`). A second call while fading is idempotent.
  **This deliberately diverges from `AtmosphereEngine::silence()`**
  (`systems/atmosphere_engine.h:636-644`, which latches and has no resume) — the header says so.
- **FR-082 (input):** every input sample tested with `detail::isNaN`/`detail::isInf` and replaced with
  `0.0f` before it can enter the loop (`fdn_reverb.h:264-265`). This path does **not** increment
  `getNonFiniteRecoveryCount()`.
- **FR-083 (internal):** once per control chunk, test `filterState_[0..N)` and the current matrix's
  diagonal `matrix_[i·N+i]`. On detection: `emergencyClear()` and `++nonFiniteRecoveries_`.
- **`emergencyClear()` (plan delta D-4 — a refinement of FR-083's "invokes `silence()`", documented as
  such in the header):** ramping a non-finite value down is not possible, so there is **no fade-out**.
  (1) zero the O(N)/O(32) scalar state immediately — that is where the detected NaN lives;
  (2) `clearPending_ = true`, `outputGate_.snapTo(0)`, `gate_ = FadingIn`, `setTarget(1)` over
  `kSilenceRampMs`; (3) the bulk clear amortized exactly as `silence()`; (4) the literal-zero rule
  above holds meanwhile. SC-014 clause 3's recovery point is the end of the fade-in.
- **The fault hook (plan delta D-8, R-12):** `injectNonFiniteStateForTest()` writes a bit-pattern NaN
  (through a `volatile` sink) into `filterState_[0]` — the exact array FR-083 sweeps. Compiled **only**
  when `KRATE_DSP_AETHER_TEST_HOOKS` is defined, which T001 did **target-wide** on `dsp_effects_tests`
  (never per-source, so no ODR hazard). **Absent from the shipping build**; the header states that next
  to the FR-083 contract. Without it, FR-083's detect → `emergencyClear()` → counter branch is
  unreachable dead code: every input path is sealed (FR-082 zeroes non-finite input, every setter falls
  back to its default, `bloomNoteOn` clamps every partial, and FR-025 + FR-032 make the unfrozen loop
  structurally non-expansive).

**Verify:** both cases green; zero warnings; `node tools/lint-nonfinite-symbols.js` clean.
**Test target:** `dsp_effects_tests` —
`dsp_effects_tests.exe "AetherReverb_NonFiniteHygiene"`, `dsp_effects_tests.exe "AetherReverb_BloomNoteApi"`.

---

## GROUP 13 — Remaining behavioural criteria and the perf lane (parallel)

Both tasks are test-only and touch **disjoint** files.

### T013 [P] — SC-001, SC-009, SC-012, SC-015 (main behaviour TU)

**Files edited:** `dsp/tests/unit/effects/aether_reverb_test.cpp` (only)

**`TEST_CASE("AetherReverb_NoAllocationAfterPrepare", "[effects][aether]")`** (SC-001, FR-003, FR-008).
30 s at the worst case (`N = 16`, shimmer `Granular`, bloom on, spectral @4096) with every setter,
`setFreeze` both ways, `setSeed`, `bloomNoteOn`/`bloomNoteOff` and `silence()` exercised mid-render.

**The `AllocationScope` idiom is binding — the naive form is vacuous.** `AllocationScope` assigns
`count_` only in its **destructor** (`tests/test_helpers/allocation_detector.h:81-83`), so
`scope.getAllocationCount()` (`:85-87`) returns 0 for the object's entire lifetime; once the scope ends
the object is gone. `REQUIRE(scope.getAllocationCount() == 0)` therefore **can never fail**. Use the
in-repo bracketing pair (the trap is documented at
`dsp/tests/unit/systems/harmonic_cloud_test.cpp:4864-4870` and
`dsp/tests/unit/systems/atmosphere_engine_test.cpp:2279-2288`):

```cpp
std::size_t allocs = 0;
bool sawFrozen = false, sawUnfrozen = false;
{
    [[maybe_unused]] const TestHelpers::AllocationScope scope;
    // render only; record observations into plain bools/PODs
    allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
}
REQUIRE(allocs == 0);
REQUIRE(sawFrozen);
REQUIRE(sawUnfrozen);
```

**Nothing but the component runs inside the tracked window** — no Catch2 macro (`INFO` builds a
`ScopedMessage`, `REQUIRE` decomposes into strings; both allocate), no `std::vector` growth, no stream
formatting. Buffers are `std::array` or pre-`reserve`d, and **one warm-up block is rendered before
tracking starts** so first-call runtime dispatch is not charged to the loop.

**`TEST_CASE("AetherReverb_SampleRateIndependence", "[effects][aether]")`** (SC-009, FR-003, RA-6):
T60 within **±10 %**, NED **≥ 0.8**, modal density within **±2 %** across 44.1 / 48 / 96 / 192 kHz at one
configuration. **Sub-44.1 clause:** `prepare(8000.0, …)` **succeeds** (no clamp);
`getModalDensityPerHz()` matches the table computed at 8 kHz within ±2 %; T60 at `setDecaySeconds(4)`
within ±15 % **at 8 kHz**; `REQUIRE(engine.isShimmerActive() == false)`; and the render with both
shimmer sends at 1 compares **equal** to the render with both at 0 via `compareFingerprints` — proving
the taps are *inert*, not merely unallocated. At ≥ 44.1 kHz `isShimmerActive()` must be **true**.

**`TEST_CASE("AetherReverb_BoundedUnderAdversarialInput", "[effects][aether]")`** (SC-012): 60 s of
noise → DC → 1 Hz square → silence at `decay = 60`, `damping = 0`, `size` swept 0→1→0, `dimensionality`
swept, shimmer and bloom at max. Peak ≤ **4.0**; **0** non-finite; `getNonFiniteRecoveryCount() == 0`;
`|DC|` in the final second ≤ **1e-3**.

**`TEST_CASE("AetherReverb_NoTransitionClicks", "[effects][aether]")`** (SC-015). `ClickDetector`
(`tests/test_helpers/artifact_detection.h:99`) with **exactly**
`ClickDetectorConfig{.sampleRate=48000.0f, .frameSize=512, .hopSize=256, .detectionThreshold=5.0f,
.energyThresholdDb=-60.0f, .mergeGap=5}` in designated-initialiser form (as at
`dsp/tests/unit/effects/shimmer_delay_test.cpp:1224-1231`). Input **G-1**, pinned: the detector flags
`|Δy| > mean + kσ` per frame (`artifact_detection.h:193`), and a near-Gaussian reverb tail gives false
positives at ~1e-4/sample.

120 s render carrying a size sweep, a dimensionality sweep, **5 freeze cycles**, a `setDensity` 0→1
step, a `setDecaySeconds` 0.5→60 step, a `setShimmerOctaveSend` 0→1 step, a **`setSpectralDiffusion`
0→1 step**, and `silence()` + resumption ⇒ **0 detections**.

**Calibration is capped and must be proven:** if a 30 s no-transition reference render shows a non-zero
false-positive floor, raise `detectionThreshold` to the smallest value giving 0 there — **cap 8.0** —
and the *same* calibrated config must then report **≥ 1 detection** on a 10 s control render carrying a
single-sample step of amplitude 0.1. **Record the threshold, the floor and the control-render count.**

- **Clause S (FR-007, `silence()` does NOT latch):** two 3 s G-1 renders at the FR-009 default mix,
  identical except that the subject calls `silence()` at `t = 1.0 s`. Require (a) the wet RMS of the
  40 ms window starting at the call is below **−80 dBFS**, and (b) the wet RMS of the final 200 ms is
  within **±3 dB** of the same window of the never-silenced reference. **A latching implementation
  scores 0 clicks and 0 allocations and passes every other clause in the suite** — this is the only
  clause that sees it.
- **Clause F (FR-064's smoother cadence):** after the `setSpectralDiffusion` 0→1 step, the wet RMS must
  reach within **1 dB** of its settled value no later than **500 ms** after the step. Advancing
  `spectralSm_` once per `process()` instead of by `advanceSamples(hopSize_)` stretches its 100 ms
  constant to ~25 s — two orders of magnitude past this bound. This is the **only** place the cadence is
  observable.

**Verify:** all four cases green; zero warnings.
**Test target:** `dsp_effects_tests`.

### T014 [P] — SC-008 CPU budget, six configurations with checked-in baselines

**Files edited:** `dsp/tests/unit/effects/aether_reverb_perf_test.cpp` (only)

`TEST_CASE("AetherReverb_CpuBudget", "[.perf][effects][aether]")`, modelled directly on
`dsp/tests/unit/systems/continuous_body_perf_test.cpp:100-260`:

```cpp
constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;
constexpr double kBlockBudgetNs = (kBlockSize / kSr48) * 1.0e9;   // 10,666,666.67
constexpr double kRegressionFactor = 1.5;
constexpr double kReferenceNs = kBlockBudgetNs * 0.05;            // 533,333.33  (roadmap line 282)
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;  // 355,555.56
```

**Six** configurations, each with its own checked-in baseline and **two** `static_assert`s
(`baseline * kRegressionFactor <= kReferenceNs` and `baseline <= kMaxAdmissibleNs`):

| # | configuration |
|---|---|
| (a) | `N=8`, defaults, shimmer/bloom/spectral **off** |
| (b) | `N=8`, shimmer `Granular` + bloom + spectral @1024 — **the shipped default** |
| (c) | `N=16`, everything on, spectral @4096, `size=1`, `density=1`, `maxDelaySeconds=0.5`, 32 active bloom resonators — the worst case and the **only** gated `N=16` configuration |
| (d) | (b) frozen and settled — freeze must not be *more* expensive |
| (e) | (b) with `dimensionality` swept continuously — the matrix recomputed every control chunk |
| **(f)** | **the state-clear burst**: configuration (c) with `silence()` called at a control-chunk boundary; the metric is the **single worst control chunk** during the clear, **not** the block mean, measured at `maxBlockSamples = 64` so the figure is against the tightest deadline the API admits (1.33 ms at 48 kHz) |

Trial shape: **best-of-25 × 500 blocks, eight consecutive runs, worst run rounded up, padded ≤ +5 %**.
A **BASELINE PROVENANCE** comment block records machine, build, trial shape, date and the eight
per-configuration figures (the shape at `continuous_body_perf_test.cpp:140-220`).

**Lever list in the TU header, in order, for a configuration that misses:** SIMD the per-sample `N×N`
multiply and the channel loop → shimmer mode → spectral FFT size → active bloom count → `N`.
If the SIMD lever is taken, **`hn::LoadU`/`StoreU` unless alignment is proven** —
`node tools/lint-simd-aligned-loadstore.js` enforces it, and an aligned load on an AVX-512 runner is the
known cause of intermittent Linux-CI-only SIGSEGV.
**Never** raise a baseline, relax the reference or renegotiate `kRegressionFactor`.
**The matrix mechanism is not a lever** — FR-022 pins the geodesic, and swapping it changes the shipped
Dimensionality axis.

**Verify:** `dsp_effects_tests.exe "[.perf]"` green; zero warnings.
**Test target:** `dsp_effects_tests` (perf lane).

---

## GROUP 14 — Integration

Sequential. Each task edits shared, repo-level files or runs whole-suite gates.

### T015 — Remaining build registration (plan §1.3) and re-verification of the four §1.2 sites

**Files edited:** `dsp/lint_all_headers.cpp`, `dsp/CMakeLists.txt`

1. Add `#include <krate/dsp/effects/aether_reverb.h>` to the **Layer 4** block of
   `dsp/lint_all_headers.cpp`. This is the only TU that gives `./tools/run-clang-tidy.ps1 -Target dsp`
   visibility of the header under the **root** `.clang-tidy` (strict) config
   (`dsp/CMakeLists.txt:197-200`).
2. Add `include/krate/dsp/effects/aether_reverb.h` to `dsp/CMakeLists.txt`'s effects header list
   (`:170-182`).
   **Precedent note, so this is a decision not an oversight:** Phases 4 and 5 registered *neither*
   `continuous_body.h` nor `atmosphere_engine.h` in these two lists. This task breaks with that
   precedent (plan §12 Q2, assumed answer above) — it costs two lines and closes the gap.
3. **Re-verify all four T001 §1.2 sites** by reading `dsp/tests/CMakeLists.txt`: the five TUs in the
   `dsp_effects_tests` source list; **only** `aether_reverb_nonfinite_test.cpp` in the
   `-fno-fast-math -fno-finite-math-only` block; the **separate** `"-O2"` call carrying exactly
   `aether_reverb_test.cpp` + `aether_reverb_spectral_test.cpp` (and **not** joined to the
   `"-fno-fast-math -fno-finite-math-only -O2"` call); the target-wide
   `target_compile_definitions(dsp_effects_tests PRIVATE KRATE_DSP_AETHER_TEST_HOOKS)`.

**Verify:** `dsp_effects_tests` and the `dsp_lint_stub` target both build clean.

### T016 — Full-suite run, always-on wall-clock measurement, `[.slow]` demotions

**Files edited:** the five Aether test TUs (tag changes only, if the measurement requires them)

1. Build and run **all three** suites that RA-1's containment claim depends on, **unedited**:

   ```bash
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_effects_tests dsp_processors_tests dsp_systems_tests
   build/windows-x64-release/bin/Release/dsp_effects_tests.exe 2>&1 | tail -5
   build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
   ```

   All three must report `All tests passed`. **Do not grep the output** — Catch2's last line is the
   summary.
2. **Measure the always-on Aether wall clock** (`dsp_effects_tests.exe "AetherReverb_*"`, Release,
   timed). B-1's budget is **≤ 60 s**. Plan §8.7's corrected ledger is ≈1 120 s of rendered audio
   ⇒ ≈67 s, so demotion is **expected**.
3. Apply **only** the demotion steps the measurement requires, in the pre-decided order — never a
   deletion, never a threshold relaxation (B-4):

   | step | action | audio removed | new wall clock |
   |---|---|---|---|
   | (1) | SC-006's tail → **90 s** always-on, the 180 s form tagged `[.slow]` | −90 s | ≈62 s |
   | (2) | SC-002 clause 4 → `[.slow]` (clause 1 keeps its full-60 s always-on config) | −62 s | ≈58 s ✓ |
   | (3) | SC-005's 60 s configuration → `[.slow]` — **held in reserve** | −72 s | ≈54 s |

4. Run the nightly grids once: `dsp_effects_tests.exe "[.slow]"` — must be green.
5. **Record the measured wall clock and every demotion actually taken** for `compliance.md`.

**Verify:** three suites green; always-on Aether wall clock ≤ 60 s; `[.slow]` green.

### T017 — Portability, lint gates, clang-tidy

**Files edited:** none expected (fixes only, if a gate fails)

Run every gate plan §1.4 lists, capturing output to a log on the **first** run:

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-float-bit-goldens.js
node tools/lint-nonfinite-symbols.js
node tools/lint-arch-guarded-includes.js
node tools/lint-allocation-operator-overrides.js
node tools/lint-simd-aligned-loadstore.js     # only if T014's SIMD lever was taken
```

Then:

```powershell
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

**Zero findings, on the whole file — not just new lines.** Notes:
- `check-portability.js` **only compiles**; a green Windows build proves nothing about the Linux/macOS
  legs. It cannot see runtime platform gates.
- `lint-float-bit-goldens.js` is a CI gate and both legs have been broken by FNV-over-float-bits before.
  SC-010 must use `render_fingerprint.h` only.
- `lint-nonfinite-symbols.js` is what enforces reuse of `detail::isNaN` / `detail::isInf` instead of a
  fourth bit-test reimplementation.

**Verify:** all gates clean.

### T018 — `compliance.md`

**Files created:** `specs/seraphis-phase6-aether-space/compliance.md`

Fill the FR and SC compliance tables **from verified evidence only** — file paths, line numbers, test
case names and **actual measured numbers**. Generic "implemented" / "test passes" claims are not
acceptable. Specifically transcribe:

- **Every FR-001…FR-086 row:** the `aether_reverb.h` line(s) implementing it.
- **Every SC-001…SC-018 row:** the Catch2 case name and the **measured** value against the threshold.
- **The six SC-008 ns/block figures verbatim** (RA-3: Phase 7 tallies *measurements*, not ceilings) —
  including configuration (f)'s worst clear chunk.
- **The shipped `kBloomSendMax`, `kBloomLoopGainCeiling` and FR-059 shelf constants**, with the measured
  SC-016 clause 3 emphasis in dB.
- **The measured per-stage prepare-time memory footprint** (plan §4's table is the derivation, not a
  substitute for the measurement).
- **M-1's measured ceiling `M-1(G-5)`**, SC-015's calibrated `detectionThreshold`, the false-positive
  floor and the positive-control detection count.
- **The always-on wall clock** and **every `[.slow]` demotion taken** (T016).
- Any gap, honestly, as ❌ with the reason.

**Verify:** every row cites concrete evidence produced in this phase.

---

## Dependency graph

```
G1  T001  scaffold + CMake
 |
G2  T002  geometry / Size / modal density
 |
G3  T003  schurReduceSO + endpoints + morph        (SC-004 complete, no audio)
 |
G4  T004  FDN core loop                            (SC-005)
 |
G5  T005  echo density                             (SC-003)
 |
G6  T006  freeze + getStateEnergy                  (SC-002, SC-017 c3)
 |
G7  T007  life modulators + seeding                (SC-010, SC-017 c1a/2a)
 |
G8  T008  shimmer taps                             (SC-006, SC-016 c1-2)
 |
G9  T009  block-partition invariance               (SC-011)
 |
G10 T010  harmonic bloom                           (SC-016 c3-4, SC-002 c4 live)
 |
G11 T011  spectral diffusion + latency             (SC-007, SC-018)
 |
G12 T012  silence/emergencyClear/non-finite + hook (SC-014, bloom note API)
 |
G13 T013 [P] SC-001/009/012/015  ||  T014 [P] SC-008 perf
 |
G14 T015 -> T016 -> T017 -> T018
```

Plan §9 notes that steps 3 and 4 (here T003 and T004) are algorithmically independent; they are kept in
separate groups because both edit `aether_reverb.h`.
