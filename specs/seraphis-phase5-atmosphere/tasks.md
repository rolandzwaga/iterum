# Tasks: Seraphis Phase 5 — Granular Atmosphere Engine

**Spec:** `specs/seraphis-phase5-atmosphere/spec.md`
**Plan:** `specs/seraphis-phase5-atmosphere/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 5 (lines 227–248)
**Branch:** `feat/seraphis-phase1-life-modulators` (every Seraphis phase lands on this one branch — do not
rename it, do not create a new one)

**Deliverables:** one new Layer 3 header (`dsp/include/krate/dsp/systems/atmosphere_engine.h`), one
strictly-additive amendment to a shared Layer 1 header
(`dsp/include/krate/dsp/primitives/rolling_capture_buffer.h`), four new test TUs under
`dsp/tests/unit/systems/`, an extension of `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp`,
and two edits to `dsp/tests/CMakeLists.txt`. **No plugin work.**

---

## How to read this file

- Tasks run in **group order**. Inside a group, tasks marked **[P]** touch fully disjoint file sets and may
  run concurrently; everything else is sequential.
- Every task is **self-contained**: exact files, the **failing test to write first** (file, `TEST_CASE`
  name, assertions with their numbers), then the implementation intent, then the target that verifies it.
- Canonical order inside every task: **write the failing test → run it and watch it fail → implement →
  zero compiler warnings → run the named target green.**
- **No commit tasks.** Commits happen outside this workflow. (Plan §19's commit granularity still applies
  when they are made.)
- Build and run (Windows, always the full CMake path):
  ```bash
  CMAKE="/c/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
  build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_X*" 2>&1 | tail -5
  ```
  Catch2 filters are **positional case names**, not `-c`; tags go in `[brackets]`. Never
  `ctest -R dsp_systems_tests` — `catch_discover_tests` registers case names, not exe names, so it matches
  nothing and reports success.
- Capture slow output (full suites, perf cases, lints) to a log on the **first** run; never re-run a suite
  just to look at its output.

**Cross-cutting rules — violating one is a defect in that task, and they are not restated per task:**

1. No allocation, lock, exception or I/O anywhere except `AtmosphereEngine::prepare()`.
2. Every `AtmosphereEngine` constant is `static constexpr` **inside the class**; nothing is added at
   namespace scope (plan §1).
3. **No `std::isnan` / `std::isinf` / `std::numeric_limits<float>::infinity()` / `quiet_NaN()`** in the new
   header or in test code. Finiteness uses `Krate::DSP::detail::isNaN` (`core/db_utils.h:54-57`) and
   `detail::isInf` (`:175-178`); non-finite test values are built by the `volatile` + `memcpy` form
   (plan §15.7). The macOS leg builds `-ffast-math`.
4. Layer discipline: `systems/atmosphere_engine.h` includes Layer 0/1/2 only — never a `systems/` or
   `effects/` header, never `stereo_utils.h`, never `brownian_drift.h` (FR-002, plan §3).
   `node tools/lint-layers.js` governs.
5. No bit-exact float goldens — `tests/test_helpers/render_fingerprint.h` tolerances only
   (`kSampleTolerance = 1.0e-4f`, `render_fingerprint.h:49`).
6. **None of the four new TUs may include `<allocation_operator_overrides.h>`.** The single owner in
   `dsp_systems_tests` is `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`; a second include is
   a duplicate-symbol link error. They include `allocation_detector.h` only.
7. Zero compiler warnings.

**Deviation from the standard workflow shape, stated once.** The single `dsp/tests/CMakeLists.txt`
registration task is **T002 (Group B)**, not the final group. That file lists test sources **explicitly and
never globs** (`dsp/tests/CMakeLists.txt:337-346` for the systems target) — an unregistered TU silently
drops and the exe still reports "All tests passed". Registering last would make every failing-test-first
task from T004 onward unverifiable. Exactly one task in this phase edits that file; the final group
(Group P) **audits** the registration instead of performing it.

---

## Group A — Blocking prerequisites

### T001 — ODR sweep, CMake fact check, green baseline

**Files:** none created or edited. This is a gate.

**Do:**

1. Re-run the plan §1 ODR sweep. A name that was free when the plan was written is not guaranteed free now:
   ```bash
   grep -rn "class AtmosphereEngine\|struct AtmosphereEngine" dsp/ plugins/ tools/
   grep -rn "AtmosphereGrain" dsp/ plugins/ tools/
   grep -rn "GrainDriftLanes" dsp/ plugins/ tools/
   grep -rn "DriftLaneRng" dsp/ plugins/ tools/
   grep -rn "struct PrepareConfig\|class PrepareConfig" dsp/ plugins/ tools/
   ls dsp/include/krate/dsp/systems/atmosphere_engine.h
   grep -n "readStereoLinear" dsp/include/krate/dsp/primitives/rolling_capture_buffer.h
   node tools/lint-odr.js
   ```
   **Expected: 0 hits for every name, "No such file" for the header, 0 hits for `readStereoLinear`,
   `lint-odr.js` clean.** Any non-zero hit is a **stop** — escalate, do not rename silently.

2. Record the names that are **taken at namespace scope** and must never be used: `Grain`
   (`primitives/grain_pool.h:23`), `GrainPool` (`:39`), `GrainScheduler` (`processors/grain_scheduler.h:29`),
   `GrainProcessor` (`processors/grain_processor.h:37`), `GranularEngine` (`systems/granular_engine.h:30`),
   `SlicePool` (`primitives/slice_pool.h:137`). Class-nested and therefore not ODR hazards but renamed
   anyway for readability: `HarmonicCloud::DriftLanes` / `LaneRng` (`systems/harmonic_cloud.h:1125`, `:1121`),
   `EntropyProcessor::LaneRng` (`processors/entropy_processor.h:423`).

3. Verify the four CMake facts the later tasks depend on (all confirmed in the session that produced this
   list — re-confirm, do not assume):
   - `dsp/tests/CMakeLists.txt:117` — `unit/primitives/test_rolling_capture_buffer.cpp` is registered in
     `dsp_primitives_tests`.
   - `dsp/tests/CMakeLists.txt:463` — the same file is inside the `-fno-fast-math -fno-finite-math-only`
     block that opens at `:400` (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`) / `:401`
     (`set_source_files_properties(`) and closes at `:665`
     (`PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"`).
   - `dsp/tests/CMakeLists.txt:343-346` — the Phase 4 systems block
     (`# Seraphis Phase 4 …` + three `continuous_body_*` entries), which T002 appends after, and `:347` is
     the closing `)`.
   - `dsp/tests/CMakeLists.txt:664` — `unit/systems/continuous_body_test.cpp` is the last entry of the
     `-fno-fast-math` list, which T002 appends after.

4. Establish a **green baseline before touching shared DSP**, so any later failure is attributable:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release \
     --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
   for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do
     build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
   done
   node tools/check-portability.js
   node tools/lint-layers.js
   node tools/lint-float-bit-goldens.js
   node tools/lint-arch-guarded-includes.js
   node tools/lint-simd-aligned-loadstore.js
   node tools/lint-allocation-operator-overrides.js
   ```

**Verify:** every suite reports "All tests passed"; every lint clean. A pre-existing failure is **owned**,
not dismissed — fix it or escalate before T002.

---

## Group B — Test-target registration (the single shared-file task)

### T002 — Create the four TU stubs and register them in `dsp/tests/CMakeLists.txt`

**Files created:**
- `dsp/tests/unit/systems/atmosphere_engine_test.cpp`
- `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp`
- `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp`
- `dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp`

**File edited:** `dsp/tests/CMakeLists.txt` — **this is the only task in the entire phase that edits it.**

**Do:**

1. Create each stub as a compiling, non-empty TU: a banner citing spec slug `seraphis-phase5-atmosphere`
   and the criteria it will carry (plan §15's table), `#include <catch2/catch_all.hpp>`, and one
   placeholder `TEST_CASE("AtmosphereEngine_Placeholder_<tu>", "[atmosphere]") { SUCCEED(); }` that later
   tasks delete. **Do not** include `atmosphere_engine.h` yet — it does not exist until T004.
   TU ownership (plan §15):
   | TU | Will carry |
   |---|---|
   | `atmosphere_engine_test.cpp` | SC-001, SC-002, SC-008, SC-009, SC-010, SC-011, the §15.8 FR cases, **plus** the two clauses that must run under fast-math (SC-014's fourth clause and SC-012 sub-case 6) |
   | `atmosphere_engine_spectral_test.cpp` | SC-003, SC-005, SC-006, SC-007 |
   | `atmosphere_engine_perf_test.cpp` | SC-004 + FR-022's micro-benchmark, both `[.perf]` |
   | `atmosphere_engine_nonfinite_test.cpp` | SC-014 only |

2. **Edit 1** — append to the `dsp_systems_tests` source list, immediately after the Phase 4 block at
   `dsp/tests/CMakeLists.txt:343-346` and before the closing `)` at `:347`:
   ```cmake
       # Seraphis Phase 5 (specs/seraphis-phase5-atmosphere): AtmosphereEngine
       unit/systems/atmosphere_engine_test.cpp
       unit/systems/atmosphere_engine_spectral_test.cpp
       unit/systems/atmosphere_engine_perf_test.cpp
       unit/systems/atmosphere_engine_nonfinite_test.cpp
   ```

3. **Edit 2** — append **exactly one** of the four to the `-fno-fast-math -fno-finite-math-only` list,
   after `unit/systems/continuous_body_test.cpp` at `:664`, with this comment verbatim (it is the only
   thing preventing a later reader from "fixing" the omission):
   ```cmake
           # Seraphis Phase 5: SC-014's clauses (a)-(c) inject NaN/Inf via bit patterns
           # in this TU and need the FP semantics preserved to assert on them.
           # ONLY this one of the four Phase 5 TUs is listed. The other three must NOT
           # be, and for atmosphere_engine_test.cpp that is DELIBERATE AND LOAD-BEARING,
           # not an omission: SC-014's fourth clause
           # (AtmosphereEngine_NonFiniteGuardSurvivesFastMath) and SC-012's sub-case 6
           # live there precisely so the ITERUM_NOINLINE guard and the
           # ordered-comparison clamp are proved under the /fp:fast + -ffast-math
           # settings the header actually ships in. Adding it here would silently
           # disable the only check that has teeth. The perf TU must stay out too:
           # -fno-fast-math would change the figures the baselines are pinned to.
           unit/systems/atmosphere_engine_nonfinite_test.cpp
   ```
   Make **no** change for `test_rolling_capture_buffer.cpp` — T001 confirmed it is already in both lists
   (`:117`, `:463`).

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe --list-tests | grep AtmosphereEngine_Placeholder
```
All four placeholder names are listed; `dsp_systems_tests` green with zero warnings.

---

## Group C — RA-1: the shared Layer 1 amendment

### T003 — `RollingCaptureBuffer::readStereoLinear` + SC-012

**Failing test first —** append to `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp`
(**extended, never replaced**; it is already in `dsp_primitives_tests` and already `-fno-fast-math`):
`TEST_CASE("RollingCaptureBuffer_ReadStereoLinear", "[rolling_capture_buffer]")` with six `SECTION`s.
Fixture for 1–3, 5, 6: `prepare(48000.0, 1.0f)` → capacity **65536** (`nextPowerOf2(48000)`); write a
known ramp via `writeStereo(i * 1e-4f, -i * 1e-4f)` so every sample is distinguishable.

1. **Length 1 (the only case where "same offset" is true):** with `A = 100`,
   `readStereoLinear(100.0f, l, r)` equals **exactly** the single sample from
   `extractSlice(&l2, &r2, /*lengthSamples=*/1, /*offsetSamples=*/100)`.
2. **Length > 1:** for `L = 8`, `O = 5`, `extractSlice(outL, outR, 8, 5)`:
   `outL[i] == readStereoLinear(5 + 8 - 1 - i)` asserted at `i = 0` (age 12) and `i = 7` (age 5), exactly.
   `extractSlice` anchors from the **end** (`rolling_capture_buffer.h:161-162`), so a test written from
   the naive "same offset" wording fails a **correct** implementation for any `L > 1`.
3. **Fractional:** `readStereoLinear(10.5f)` equals `0.5f * (read(10) + read(11))` within **1e-6** on both
   channels.
4. **Degenerate (FR-081's guard):** on a **default-constructed, never-prepared** buffer, and again
   immediately after `prepare`, and again after exactly **one** `writeStereo`, `readStereoLinear` at ages
   `0.0f` and `1.0f` returns `(0.0f, 0.0f)` every time and reads nothing out of bounds (visible under
   ASan/valgrind). This is the `getAvailableSamples() < 2` case in which a bare `available - 2` underflows
   `size_t` to ~2⁶⁴.
5. **Wraparound:** write `2 * 65536` ramp samples, then assert `readStereoLinear(65534.0f)` (= `C - 2`) is
   the oldest live sample and that sweeping ages `0 … C-2` yields the monotone ramp with no sample from
   the wrong side of the write head.
6. **Non-finite argument** (the §2 ordered-comparison contract): a NaN age and a `-Inf` age both land on
   **age 0**, i.e. the result equals `readStereoLinear(0.0f)`; a `+Inf` age lands on age
   `getAvailableSamples() - 2`, i.e. equals the largest legal finite age. **Not `(0,0)`.** Build the values
   with `makeNonFinite` (`volatile std::uint32_t` + `std::memcpy`; `0x7FC00000` NaN, `0x7F800000` +Inf,
   `0xFF800000` −Inf).

**Also:** copy sub-case 6 verbatim into `dsp/tests/unit/systems/atmosphere_engine_test.cpp` as
`TEST_CASE("RollingCaptureBuffer_ReadStereoLinearFastMath", "[atmosphere]")`. That TU is **not** in the
`-fno-fast-math` list, which is the whole point: a guard that only works under `-fno-fast-math` is a guard
that never works in a shipped build.

**Implement —** `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h`, exactly the body in plan §2,
placed immediately after `extractSlice` (`:170`) inside the "Slice Extraction (Real-Time Safe)" section,
with the full doc comment from plan §2 (it is the only place the two anchoring conventions are contrasted):

- `void readStereoLinear(float ageSamples, float& outLeft, float& outRight) const noexcept`
- **Guard first:** `if (capacity_ == 0 || getAvailableSamples() < 2) { out* = 0.0f; return; }`.
- Then `maxAge = static_cast<float>(available - 2)` and a clamp written as **two ordered comparisons in
  this order** — `if (!(age >= 0.0f)) age = 0.0f;` (takes negatives, −Inf and NaN, since an unordered
  compare is false) then `if (age > maxAge) age = maxAge;` (takes +Inf). No FP classification predicate, so
  `-ffinite-math-only` has nothing to fold, and no `ITERUM_NOINLINE` call boundary — this sits on the
  engine's innermost loop (~65 k calls per block at saturation).
- `i0 = (writeIndex_ + capacity_ - 1 - ageInt) & mask_`; `i1 = (i0 + capacity_ - 1) & mask_` (one sample
  **older**, written additively so `size_t` never underflows before the mask).
- New include: **`<cmath>` only** (for `std::floor`). Not `db_utils.h` — the ordered-comparison guard
  removes the need, keeping one fewer edge on a shared Layer 1 header.
- FR-084: no existing member, default or behaviour changes.

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_effects_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe     2>&1 | tail -5
node tools/lint-layers.js
```
SC-012's regression half: `dsp_primitives_tests` (owns the existing cases at
`test_rolling_capture_buffer.cpp:20`) and `dsp_effects_tests` (owns `PatternFreezeMode`,
`effects/pattern_freeze_mode.h:40`) both green with **no existing test edited**.
**Negative control (do it, then revert):** delete the `available < 2` guard → sub-case 4 must fail; delete
either ordered comparison → sub-case 6 must fail.

---

## Group D — Engine skeleton

### T004 — Header skeleton: banner, constants, `PrepareConfig`, state, lifecycle, setters, accessors

**Files created:** `dsp/include/krate/dsp/systems/atmosphere_engine.h` (Layer 3, `namespace Krate::DSP`).
**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing tests first —** in the main TU:

`TEST_CASE("AtmosphereEngine_LifecycleAndGuards", "[atmosphere]")` (FR-003, FR-004):
- A default-constructed, un-`prepare`d engine renders **exactly `0.0f`** for 512 samples.
- `processStereoBlock` with **any** of the four pointers null writes nothing (fill the output with a
  sentinel `-7.0f` first and assert it is untouched) and returns.
- `numSamples == 0` is a no-op **and advances no control step** — assert `getTotalGrainsBorn()` and the two
  skip counters are unchanged across 1000 zero-length calls.
- A second `prepare` at a different rate/config fully reconfigures: `getCaptureCapacitySamples()` and
  `getLatencySamples()` both change to the new configuration's values.

`TEST_CASE("AtmosphereEngine_ControlTableClamps", "[atmosphere]")` (FR-009):
- Every setter clamps to the FR-009 range and a **non-finite argument lands on the field's default**:
  `grainSeconds` [0.05, 30] def 4.0; `density` [0.1, 20] def 4.0; `jitter` [0,1] def 0.5;
  `positionSeconds` [0,30] def 1.0; `positionSpread` [0,1] def 0.3; `pitchSemitones` [−24,+24] def 0.0;
  `pitchSpread` [0,1] def 0.15; `driftDepth` [0,1] def 0.3; `driftSmoothness` [0,1] def 0.7;
  `driftRangeSemitones` [0,12] def 2.0; `panSpread` [0,1] def 0.7; `decorrelation` [0,1] def 0.5;
  `blur` [0,1] def 0.0; `freezeMix` [0,1] def 0.0; `level` [0,2] def 1.0.
- `setDensity(0.01f)` lands at **0.1** — the same bound `GrainScheduler::setDensity` enforces
  (`processors/grain_scheduler.h:47`), so table and component agree instead of the component silently
  overriding.
- `PrepareConfig` FFT snapping, reflected in `getLatencySamples()`: `blurFftSize = 3000` → **2048**;
  `blurFftSize = 100` → **256**; `blurFftSize = 8000` → clamp 4096 → **4096**;
  `freezeFftSize = 3000` → **2048**. Order is clamp → `std::bit_floor` → re-clamp to the lower bound.
- A freshly prepared engine is **silent** (empty ring) with every value at its default.

*Two clauses of this case are deferred and are added by the task named here, not forgotten:* the blur
smoother-cadence settling clause → **T012**; the FR-064 `level = 0` exact-silence clause → **T008**.

**Implement —** plan §3 (include set), §4 (constants, `static_assert`s, `PrepareConfig`), §5 (state),
§6 (lifecycle), §7 (setters), §14.2 (accessors). `processStereoBlock` writes silence in this task; the
grain/blur/freeze paths arrive in T006/T012/T013.

- **Banner (FR-001, FR-073):** layer; spec slug; roadmap lines 227–248; the RT-safety contract; RA-2's
  memory table (4 s → 2.10 MB, 8 s → 4.19 MB, 16 s → 8.39 MB, 30 s → 16.8 MB per voice at 48 kHz; ×2 at
  96 kHz); the `density × grainSeconds ≤ kMaxGrains` operating rule; the blur latency (RA-3); the
  `sampleRate ≥ 44 100` conditioning of §9.6's `Δ < 1` bound; and `kMaxGrains = 64` flagged
  **provisional, measurement-backed** (FR-022, resolved at T017/T018).
- **Includes (FR-002), exactly:** `core/db_utils.h`, `core/grain_envelope.h`, `core/math_constants.h`,
  `core/pitch_utils.h`, `core/random.h`; `primitives/rolling_capture_buffer.h`, `primitives/smoother.h`,
  `primitives/spectral_buffer.h`, `primitives/stft.h`; `processors/grain_scheduler.h`,
  `processors/spectral_freeze_oscillator.h`; `<algorithm> <array> <bit> <cmath> <cstddef> <cstdint>
  <vector>`. **Absent on purpose:** `stereo_utils.h`, `brownian_drift.h`, anything from `systems/` or
  `effects/`.
- **Constants** exactly as plan §4, including `kMaxGrains = 64`, `kEnvelopeTableSize = 4096`,
  `kEnvelopeTailZeroEntries = 2`, `kMinAgeSamples = 64`, `kInterpMarginSamples = 2`,
  `kControlChunkSamples = 64`, `kDriftControlInterval = 32`, the five smoothing times, the six drift
  coefficients transcribed from `brownian_drift.h:97-105`/`:226-228`, the ranges, and the four seed salts
  (`kGrainSalt 0x1000`, `kBlurSalt 0x2000`, `kSchedulerSalt 0x3000`, `kDriftSaltBase 0x4000`).
- **The six `static_assert`s** from plan §4 verbatim (chunk is a whole number of OU steps;
  `kMinAgeSamples >= kControlChunkSamples`; `kMaxGrains <= 255`; `kMaxGrains <= 64`; tail-run bounds; salt
  ranges disjoint).
- **Copy/move:** delete copy, default move — `SpectralBuffer` has no copy (`spectral_buffer.h:51-52`) and
  `STFT`/`OverlapAdd` delete it (`stft.h:41-44`, `:187-190`).
- **`prepare`** in plan §6.1's order, ending `prepared_ = true; reset();`. It is the only non-RT-safe
  method.
- **`reset`** in plan §6.2's order. Two items are load-bearing and easy to omit:
  (a) `scheduler_.reset()` **and then** `scheduler_.seed(deriveStreamSeed(seed_, kSchedulerSalt))` —
  `GrainScheduler::reset()`/`prepare()` never touch `rng_` (`grain_scheduler.h:33-42`), only `seed()` does
  (`:97`); (b) `minObservedAge_` seeded at `captureCapacity_`, never at an infinity sentinel.
- **`silence()`** — the FR-007 latch state machine (`Running → Silencing → Latched`), idempotent while
  latched; `reset()` (or `prepare()`) is the only re-entry; there is no `resume()`.
- **Setters** per plan §7, each sanitising a non-finite argument to the field's default via §13.2's
  `ITERUM_NOINLINE isFinite` helper (a **composition** of `detail::isNaN`/`detail::isInf`, never a fifth
  bit test — `ContinuousBody` already carries a fourth at `systems/continuous_body.h:1346-1358`).
  `setGrainEnvelope` carries the **idempotence guard** (`if (type == envelopeType_) return;`).
- **All accessors** from plan §14.2 (16 of them, including the five D-3 additions), returning defaults
  until the paths that fill them land.

**Verify:** `dsp_systems_tests` builds zero-warning; `AtmosphereEngine_LifecycleAndGuards` and
`AtmosphereEngine_ControlTableClamps` pass. Delete the T002 placeholder case from this TU.

---

## Group E — Drift lanes

### T005 — `GrainDriftLanes`: the `kMaxGrains`-lane OU bank (FR-030, C-5)

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing test first —** a `SECTION("drift-lane equivalence")` inside
`TEST_CASE("AtmosphereEngine_GrainLiveness", "[atmosphere]")` (the rest of that case arrives in T006), the
one place that includes `<krate/dsp/processors/brownian_drift.h>` (the engine header must not):

- Engine: `prepare(48000.0, {})`, `setSeed(1234)`, `setDriftSmoothness(0.7f)`, `setDriftDepth(1.0f)`,
  `setDensity(1.0f)`, `setGrainSeconds(0.2f)`; chosen slot = `kMaxGrains - 1` = **63**.
- Reference: a `BrownianDrift`, `prepare(48000.0)`, `setSeed(deriveStreamSeed(1234, kDriftSaltBase + 63))`,
  `setSmoothness(0.7f)`, `setDepth(1.0f)`, `reset()`, then driven with **`processBlock(64)` once per engine
  control chunk**.
- Assert `engine.getDriftLaneValue(63)` tracks `ref.getCurrentValue()` to within **1e-6** at every chunk
  over at least 200 chunks.
- **Window precondition:** the slot must have **no birth at any point in the render** — a birth zeroes the
  lane's walk state without re-seeding its stream (FR-030), whereas `BrownianDrift::reset()` re-seeds
  (`brownian_drift.h:133-135` → `:243`), so any birth desynchronises the two permanently. Assert
  `REQUIRE(engine.getTotalGrainsBorn() < kMaxGrains - 1)` at the end of the window.

**Implement —** plan §10:

- `struct DriftLaneRng { Xorshift32 rng{1}; }` (exists because `Xorshift32`'s only ctor is `explicit`,
  `core/random.h:45`), and `struct GrainDriftLanes` — SoA `alignas(32) std::array<float, kMaxGrains>` for
  `walk`/`smoothCur`/`smoothTgt`, `std::array<DriftLaneRng, kMaxGrains> rng`, scalars `a`, `g`, `depth`,
  **one shared** `int samplesUntilControl`, and the `cachedPowN`/`cachedPowValue` memo. Never
  `kMaxGrains` `BrownianDrift` objects.
- **Coefficients in double** (plan §10.1): `controlDt = 32 / sampleRate_`,
  `tau = 0.2 + smoothness * (30.0 - 0.2)`, `a = exp(-controlDt / tau)`,
  `g = 0.5 * sqrt(max(0, 1 - a*a))`. Computing these in float moves the AR(1) coefficient in the last bits
  and it is re-applied at every step — the equivalence gate above would sit at its tolerance for no reason.
- **Control step** (plan §10.2): three `nextFloat()` draws into **named locals** (operands of `+` are
  unsequenced in C++; a different draw order is a different stream, not a rounding difference), summed
  Irwin–Hall; `x = a*walk + g*z`; clamp ±4.0; flush below 1e-20; `smoothTgt = clamp(depth*x, -1, +1)`.
  **Every lane steps, live or not.**
- **Smoother advance** (plan §10.4): a transcription of `OnePoleSmoother::advanceSamples`
  (`primitives/smoother.h:243-254`) including the `isComplete()` early **continue** (which leaves the value
  unchanged — it does not snap), `detail::flushDenormal`, and the post-advance snap under
  `kCompletionThreshold`. `coeff^N` via `std::pow(driftSmoothCoeff_, static_cast<float>(n))` hoisted out of
  the lane loop and memoised on `n` — **never** a precomputed `coeff^k` table (constant exponents get
  strength-reduced under `/fp:fast`, which measured 1.02e-4 of divergence on `HarmonicCloud`).
- **`advanceDriftLanes(n)`** (plan §10.3): the `BrownianDrift::processBlock` structure with the shared
  carry-over counter. Called from `runControlStep()` with exactly `kControlChunkSamples` — **never** "once
  per block" by `numSamples` (that alone makes SC-011 unsatisfiable).
- Seeding at `prepare`/`reset`/`setSeed` **only**, from `deriveStreamSeed(seed_, kDriftSaltBase + i)`.
- `getDriftLaneValue(slot)` returns `smoothCur[slot]`, 0 out of range.
- Add `runControlStep()` and the FR-005 absolute-grid walker in `processStereoBlock` (plan §8): `toGrid`
  from `sampleCounter_ % kControlChunkSamples`, control step only on an exact boundary, `n ≤ 64` always.
  Output is still silence in this task.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness*"` — the equivalence section passes to
1e-6. Zero warnings.

---

## Group F — Grain engine core

### T006 — Capture, scheduling, birth, the FR-025 liveness arithmetic, per-sample accumulation

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing tests first —** in the main TU. Every cell of every clause carries
`REQUIRE(engine.getTotalGrainsBorn() > 0)` **before any other assertion**: `reset()` seeds
`minObservedAge_ = captureCapacity_` and `maxObservedAge_ = 0`, so a birth-free cell satisfies the bounds
*vacuously*.

`TEST_CASE("AtmosphereEngine_GrainLiveness", "[atmosphere]")` — four more `SECTION`s alongside T005's:

1. **Invariant, always.** `getMinObservedGrainAgeSamples() >= 64` and
   `getMaxObservedGrainAgeSamples() <= getCaptureCapacitySamples() - 2`, sampled every block across
   `grainSeconds ∈ {0.05, 1, 5, 15, 30}` × `pitchSemitones ∈ {−24, −12, 0, +12, +24}` ×
   `captureSeconds ∈ {1, 8, 30}` × `driftDepth ∈ {0.0, 1.0}` × `decorrelation ∈ {0.0, 1.0}`. Render each
   cell long enough for the ring to fill **and** for one birth — the tightest cell
   (`captureSeconds = 1` ⇒ `C = 65536` at 48 kHz, `grainSeconds = 30`, `pitchSemitones = +24`) admits only
   once the ring is completely full, ~1.37 s, because `a₀` clamps into `[65530, 65534]`.
2. **Closed form, drift-free.** `driftDepth = 0`, `pitchSpread = 0`, `positionSpread = 0` **and
   `decorrelation = 0`** (D-1: `dR` enters the headroom, so the closed form is exact only at `dR = 0`).
   `getLastBornGrainLifetimeSamples()` **equals** `⌊(C − 2 − 64 − 2)/|1 − r|⌋` wherever truncation binds,
   and equals the requested lifetime where it does not. The trailing `− 2` is D-12's reserved ceiling
   slack — writing the assertion from the spec's unreserved `⌊(C − 2 − g)/w⌋` fails a **correct**
   implementation by one sample. Shadow model `a(t) = a₀ + (1 − r)·t` from
   `getLastBornGrainBirthAgeSamples()` / `getLastBornGrainRatioAtBirth()`.
3. **Envelope, drift-on.** `driftDepth = 1`, `driftRangeSemitones ∈ {2, 12}`, `decorrelation = 0`. Shadow
   model is the **bound** `a(t) ∈ [a₀ + min(0, 1−rMax)·t, a₀ + max(0, 1−rMin)·t]`; observed min/max lie
   inside it; `getLastBornGrainLifetimeSamples() == ⌊(C − 2 − 64 − 2)/w⌋` with
   **`w = (rMax − 1)⁺ + (1 − rMin)⁺`** — the **sum**, never the maximum.
   - **Must include the straddling cell** `pitchSemitones = 0`, `driftRangeSemitones = 2`:
     `rMin = 0.8909`, `rMax = 1.1225`, `w = 0.2316` against a maximum-based `0.1225`. It is the only case
     where the two candidate definitions differ; sweeping only non-straddling envelopes passes a
     maximum-based implementation.
   - Worked check available to the test: at `captureSeconds = 8` (`C = 524288`), `pitchSemitones = 0`,
     `driftRangeSemitones = 12` ⇒ `rMin = 0.5`, `rMax = 2`, `w = 1.5`, `L' = ⌊524220/1.5⌋ = **349480**`
     (≈7.28 s at 48 kHz).
   - **Window-non-emptiness clause (D-12), which no other clause would see:** recompute
     `aLo = ⌈wUp·L'⌉ + 64` and `aHi = C − 2 − ⌈wDown·L'⌉` from the observed `L'` and
     `REQUIRE(aLo <= aHi)` **and** `REQUIRE(a₀ ∈ [aLo, aHi])`. Without the reserved slack the two ceils can
     sum to `H + 1`, inverting the window, and `std::clamp(a0, aLo, aHi)` with `lo > hi` is UB — which can
     produce a plausible `a₀` and pass every bound-style assertion on the machine it runs on.
4. **Snapshot clause (FR-009/FR-030).** `grainSeconds = 30`, `captureSeconds = 30`: birth a long grain,
   then call `setPitchSemitones(+24)` and `setDriftRangeSemitones(12)` **while it is in flight**. That
   grain's recorded `L'` is unchanged, its observed age bounds stay inside the envelope computed from the
   values in force **at its birth**, and the widened settings appear only in the **next** grain born
   (via `getLastBornGrainRatioAtBirth()`). Clauses 1–3 sweep static configurations only, so a live-reading
   implementation passes all of them.

`TEST_CASE("AtmosphereEngine_SkipNeverSteal", "[atmosphere]")` (FR-020, FR-022, FR-023):
- `getActiveGrainCount()` never exceeds **64** at any block boundary, at `density = 20`,
  `grainSeconds = 30`, `captureSeconds = 30`.
- **`getTotalGrainsRetired() + getActiveGrainCount() == getTotalGrainsBorn()` at every block boundary**,
  with `retired` counted independently — derived as `born − active` the identity is a tautology that a
  *stealing* implementation also satisfies, so it could not detect FR-023 failing.
- `getSkippedTriggerCountPoolFull() > 0` in that configuration.
- **Round-robin from the birth sequence:** record `getLastBornGrainSlot()` after each `getTotalGrainsBorn()`
  increment and assert every slot index in `[0, 64)` appears within **2·kMaxGrains = 128** births — a
  first-free allocator concentrates births on the low ~16 slots and fails this. Cross-check with
  `getActiveSlotMask()`.

`TEST_CASE("AtmosphereEngine_CaptureAndColdRing", "[atmosphere]")` (FR-010 … FR-014):
- No birth before the ring holds `⌈a₀ + dR⌉ + kInterpMarginSamples` (**+2**, D-11 — not `+64`);
  `getSkippedTriggerCountRingCold()` climbs meanwhile and `getSkippedTriggerCountPoolFull()` stays 0.
- A grain born in block *k* can read audio written in block *k* (self-granulation): with
  `positionSeconds` set to ~1 ms and the input a step, the output responds within the same block.
- **FR-062 dry-pass-through clause:** full-scale input, `positionSeconds = 30`, `density = 0.1` so the
  first birth is far away — every output sample is **exactly `0.0f`** for at least 1 s before
  `getTotalGrainsBorn()` first becomes non-zero. Any dry leak, however small, is a non-zero sample.

**Implement —** plan §9.1–§9.5, §9.7–§9.8:

- **Capture (§9.1)**, before any ring read for that sample: the **two-pass chunk-granular** sanitiser —
  one `probe = Σ(inL[i] + inR[i])` over the chunk and a single `isFinite(probe)` call on the common path,
  with a per-sample substitution fallback only when the sum is non-finite. Non-finites propagate through
  `+` and `(+Inf) + (−Inf)` is NaN, so nothing can cancel. `capture_.writeStereo(...)` then
  `++writeCounter_` (FR-013's monotonic `std::uint64_t`; `getSamplesWritten()` saturates at capacity,
  `rolling_capture_buffer.h:119-121`, and cannot serve).
- **Scheduling (§9.2):** `if (scheduler_.process()) tryBirthGrain();` per sample. **Slot sweep first**
  (64 integer tests, no RNG), then the four birth draws — this keeps the grain RNG a function of
  *successful* births, which is what makes `getGrainRngState()` a usable determinism probe. The
  **ring-cold** rejection is evaluated *after* the draws (its threshold depends on drawn `a₀`/`dR`), so a
  ring-cold skip **does** consume four draws. Two separate counters, never conflated.
- **Round-robin (§9.3):** scan forward from `nextSlot_` modulo 64; on success `nextSlot_ = slot + 1`; on a
  full sweep `++skipPoolFull_` and return — **no grain is ever reset, truncated or reused**. Active list
  maintained by append/swap-remove, never rebuilt by scanning (contrast `GrainPool::activeGrains()`,
  `primitives/grain_pool.h:107-116`, called once per sample by `GranularEngine`).
- **Birth (§9.4), draw order fixed and documented:** `uPos = nextFloat()`, `uPitch = nextFloat()`,
  `uPan = nextFloat()`, `uDec = nextUnipolar()`. Then (a) snapshot `s`, `d`, `semisLo/Hi` (both clamped to
  ±36 so `r ∈ [0.125, 8]` for life), `rMin`, `rMax`; (b) `dR = decorrelation_ · 30 ms · sampleRate · uDec`;
  (c) the liveness arithmetic — `wUp = max(rMax−1, 0)`, `wDown = max(1−rMin, 0)`, `w = wUp + wDown`,
  `H = C − 2 − 64 − dR`, reject if `H ≤ 2`, `Hs = H − 2`, `L' = (w·L > Hs) ? ⌊Hs/w⌋ : L`, reject if
  `L' < 2`, `aLo = ⌈wUp·L'⌉ + 64`, `aHi = C − 2 − ⌈wDown·L'⌉ − dR`; (d) `a₀` from `positionSeconds` ±
  spread, clamped into `[aLo, aHi]`; (e) admission `available ≥ ⌈a₀ + dR⌉ + 2`; (f) equal-power pan
  computed **once** (`panNorm = (pan+1)*0.5`, `cos/sin(panNorm · kHalfPi)` — the law at
  `processors/grain_processor.h:101-103`); (g) commit with `readIndexInt = writeCounter_ − 1 − ⌈a₀⌉` and
  `readFrac = ⌈a₀⌉ − a₀` (the `ceil` form keeps `readFrac ≥ 0`), `envPhaseInc = 1/(L'−1)`; (h) zero lane
  `slot`'s `walk`/`smoothCur`/`smoothTgt` **without re-seeding its stream**; (i) update the six
  introspection scalars and `++totalBorn_`.
- **Ratio refresh (§9.5)** at each control step for every live grain:
  `ratio = ratioAtPitch(clamp(staticSemis + smoothCur[slot]·driftSemis, semisLo, semisHi))`. The clamp to
  the **snapshotted** endpoints is load-bearing: `s + lane·d` can round 1 ULP above `s + d`, and over
  1.44 M samples that is ≈0.14 samples of extra age — enough to fail clause 1 when the floor division
  leaves no slack. Route both the birth envelope and the per-step ratio through the one
  `ratioAtPitch()` helper so the monotone consistency is structural.
- **Per-sample loop (§9.7)** exactly as written there: `ageL = (writeCounter_−1 − readIndexInt) − readFrac`;
  envelope lookup at `ageSamples · envPhaseInc`; `readStereoLinear(ageL, …)` and, only when
  `decorrAge > 0`, a second read at `ageL + decorrAge` for R; accumulate `env·panL·l0` and `env·panR·rr`;
  advance `readFrac += ratio` with integer carry; retire on the **integer** compare
  `++ageSamples >= lifetime` with `++totalRetired_` at the swap-remove site. **No per-grain amplitude
  term** — it would be identically 1 (FR-034).
- **Observed ages (§9.8):** folded once per control chunk per live grain from the chunk's first and last
  sample, both channels, plus at birth and retirement. Exact, not sampled: `ratio` is constant within a
  chunk, so `age(t)` is affine.
- Envelope table: generate `Hann` into the `prepare`-allocated 4096-entry table for now; T007 adds the
  forcing and the full type sweep. Bus goes straight to the output (no `1/√n`, no level) in this task.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_GrainLiveness*"`,
`"AtmosphereEngine_SkipNeverSteal*"`, `"AtmosphereEngine_CaptureAndColdRing*"` all green; zero warnings.

---

### T007 — Envelope table: forced endpoints + the `L' − 1` phase denominator (FR-027, D-4b/D-13)

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing test first —** `TEST_CASE("AtmosphereEngine_EnvelopeEndpointsForced", "[atmosphere]")`:

- For **every** `GrainEnvelopeType` (`Hann`, `Trapezoid`, `Sine`, `Blackman`, `Linear`, **`Exponential`**)
  and at `grainSeconds ∈ {0.05, 30}` (the extremes of the phase-resolution range), render a **single**
  grain (`density = 0.1`, one birth) and assert its **first two and last two** output samples are 0 to
  within **1e-6**.
  Two at each end, not one: asserting only the endpoints still passes an implementation that kept the
  `1/L'` denominator, under which the maximum phase is `(L'−1)/L' < 1`, `table[4095]` is never read at all,
  and `Exponential`'s last emitted sample is ≈**0.0188** (at `grainSeconds = 0.05`, `L' = 2400` at 48 kHz,
  the final lookup lands at `index0 = 4093`, `frac ≈ 0.29`).
- **Terminal-step bound for `Exponential`:** `max |y[n] − y[n−1]|` over the last 8 samples of the grain is
  below `0.02 · max|y|` — the ≈0.010 bound plan §9.6 derives, with margin.

**Implement —** plan §9.6, both halves (either alone is insufficient):

1. `regenerateEnvelope()` calls `GrainEnvelope::generate(envelopeTable_.data(), 4096, envelopeType_)`
   (`core/grain_envelope.h:33`) **in place** — never `resize` (contrast `GrainProcessor::prepare`,
   `processors/grain_processor.h:49`) — then forces `envelopeTable_[0] = 0.0f` **and a tail run of
   `kEnvelopeTailZeroEntries = 2`** entries to 0. Five of the six types already end at exactly 0;
   `Exponential` ends at `exp(-4) ≈ 0.0183` (`grain_envelope.h:144-150`).
2. The envelope phase denominator is **`L' − 1`**, so the last emitted sample has phase exactly 1.0,
   `indexFloat = 4095`, and the lookup returns the forced entry. This is the same denominator `generate`
   uses to lay the table out (`grain_envelope.h:41`). `L' ≥ 2` is already guaranteed at birth (T006 step c).
   Phase is **multiplied** (`ageSamples · envPhaseInc`), never accumulated — a float accumulator over
   1.44 M additions drifts up to ~4 % of full scale and retires a grain at envelope ≈0.02, which is the
   click SC-003 exists to catch.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_EnvelopeEndpointsForced*"` green; T006's cases still
green; zero warnings.

---

## Group G — Output stage

### T008 — `1/√n`, level, silence latch, denormals, non-finite hygiene

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing tests first —** in the main TU:

`TEST_CASE("AtmosphereEngine_PopulationGain", "[atmosphere]")` (FR-028, FR-034):
- Output RMS within **1 dB** across `density ∈ {1, 4, 16}` at fixed `grainSeconds = 4`, i.e. the `1/√n`
  law tracks the **live** population.
- A grain born into a crowd does not stay quiet as the crowd thins: run at `density = 16` until saturated,
  drop to `density = 1`, and assert the RMS recovers to the `density = 1` reference within 1 dB after the
  population settles. A birth-time snapshot of `1/√n` fails this and nothing else would see it.

`TEST_CASE("AtmosphereEngine_PanAndDecorrelation", "[atmosphere]")` (FR-032, FR-033):
- At `panSpread = 0` **and** `decorrelation = 0` the two output channels are identical to within 1e-6.
- At `decorrelation = 1` the inter-channel correlation coefficient drops measurably below the
  `decorrelation = 0` value.
- **Per-grain equal-power law:** read `getLastBornGrainPanGains(l, r)` after each birth across
  `panSpread ∈ {0, 0.5, 1}` and at least **200 births**, asserting `l² + r² ≈ 1` to within **1e-6** — on
  real drawn values, not assumed from the formula.

`TEST_CASE("AtmosphereEngine_SilenceLatchAndReset", "[atmosphere]")` (FR-007):
- After `silence()` the ramp is ≈**10 ms** (`kSilenceRampMs`); from its end **every** output sample is
  exactly `0.0f` and `getActiveGrainCount()` reads 0.
- Across the latched span neither skip counter advances and `getTotalGrainsBorn()` is frozen.
- A second `silence()` while latched is a no-op.
- `reset()` is the only re-entry: after it the engine renders non-silent audio again once the ring refills
  (window RMS > −60 dBFS).

Add to `AtmosphereEngine_ControlTableClamps` (deferred from T004) the **FR-064 clause**: after
`setLevel(0.0f)` and 100 ms of settling with grains live, every output sample is **exactly `0.0f`** and no
sample is denormal (bit test: exponent field 0 with non-zero mantissa).
Add to `AtmosphereEngine_CaptureAndColdRing` its second half: with `level = 0` and grains live the output
stays exactly `0.0f`, so no path bypasses the level trim.

**Implement —** plan §9.9, §13.1, §13.2, §13.3:

- `1/√n`: target `1/√max(1, activeCount_)` refreshed at control steps, `gainSmoother_` configured at
  `kGainSmoothMs = 50 ms` against the **audio** rate and advanced by **one `process()` per output sample**,
  applied as a **single multiply on the summed stereo bus** after every grain has been accumulated. Never
  captured per grain (that would leave a grain born into a crowd quiet for its whole 30 s life, and would
  invalidate SC-008's incoherent-sum argument).
- Level: `levelSmoother_.process()` per output sample, range [0, 2], 20 ms.
- Silence ramp and latch per plan §6.3/§13.1, with `totalRetired_ += activeCount_` **before** the count is
  zeroed so the retirement identity holds through the latch.
- `detail::flushDenormal` on both outputs (FR-064).
- FR-063 internal path: `busPoisonAccum_ += busL_[i] + busR_[i]` per sample (**two adds, no calls**), tested
  **once** at the control-chunk boundary as `chunkPoisoned_ = !isFinite(busPoisonAccum_)`, then zeroed; if
  set, fire `silence()` and retire under the ramp. Per-sample accumulation with a chunk-rate test cannot
  miss a transient, and it costs one `isFinite` call per 64 samples instead of 128 — the call boundary is
  ~2 ns and the naive placement is ≈4 % of the SC-004 reference.
- **No width control** (FR-060/N-9): no `setWidth`, no `stereoCrossBlend`, no `stereo_utils.h`.

**Verify:** the four cases above plus T006/T007's, all green; zero warnings.

---

## Group H — Grain-boundary clicks

### T009 — SC-003: no clicks at any lifetime, with the latch and `reset()` inside the window

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp` (and
`atmosphere_engine.h` only if a defect is found).

**Failing test first —** `TEST_CASE("AtmosphereEngine_NoGrainBoundaryClicks", "[atmosphere]")`:

- **Input (pinned — the metric is relative, so an unpinned input makes the criterion unreproducible):**
  a file-local `makeHarmonicStack()` — fundamental 220 Hz with partials at 2×…9× at `1/n` amplitude, all
  sine, zero phase, band-limited below 2 kHz, scaled to peak 0.5. No such generator exists in
  `tests/test_helpers/test_signals.h` (sine/noise/sweep/square/saw only). **Shared with SC-005** (T013).
- **Metric:** `Krate::DSP::TestUtils::ClickDetector::detect()`
  (`tests/test_helpers/artifact_detection.h:99-160`) with the config verbatim, as
  `shimmer_delay_test.cpp:1224-1231` does:
  `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256,
  .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`.
- **Threshold: 0 detections** at every `grainSeconds ∈ {0.05, 0.2, 1, 5, 30}` × every `GrainEnvelopeType`
  including `Exponential`, at `density = 20`, over a **60 s** render.
- **Precondition, scoped (D-17 — a blanket one fails a correct implementation):**
  | Cells | Precondition |
  |---|---|
  | `grainSeconds ∈ {5, 30}` (`density × grainSeconds` = 100 / 600 > 64) | `REQUIRE(getSkippedTriggerCountPoolFull() > 0)` — proves FR-023's path |
  | `grainSeconds ∈ {0.05, 0.2, 1}` (maxima 1 / 4 / 20 concurrent, so `skipPoolFull_` is structurally 0) | `REQUIRE(getTotalGrainsBorn() > 0)` **and** `getActiveGrainCount() > 0` observed at least once |
- **Latch clause (fixes when `silence()` is invoked):** `silence()` at **40 s** — after the pool-saturation
  precondition has been observed; from the end of its 10 ms ramp every sample is exactly `0.0f` and
  `getActiveGrainCount()` is 0. `reset()` at **50 s**; the final 10 s is non-silent again (window RMS
  > −60 dBFS once the ring refills). The detector runs over the whole 60 s, so both transitions are inside
  the 0-detection threshold.
- **Secondary bound, a ratio not an absolute:** `max |Δy|` of the engine output ≤ **1.5 ×** `max |Δy|` of a
  reference render with the same input and seed, without pool saturation and without `silence()`. An
  absolute `≤ 0.05` would constrain input bandwidth rather than the engine (a 5 kHz sine at 0.5 already has
  per-sample deltas of 0.33 at 48 kHz).
- **If the reference render itself shows detections** (the detector flags `mean + 5σ` of `|Δy|` **within
  each 512-sample frame**, `artifact_detection.h:186-193`, `:209-218`, which on near-Gaussian output is
  ≈3.81σ half-normal ⇒ P ≈ 1.4e-4 ⇒ hundreds of false positives over 60 s): raise `detectionThreshold` to
  the **smallest** value giving 0 detections on the **reference** render, and record that value **and its
  measured false-positive floor in the TU header**. Never relax the 0-detection requirement on the engine
  render.

**Implement —** nothing new is expected; this case gates T006–T008. A failure here is a real defect —
likeliest causes, in order: the `L'−1` phase denominator or the forced tail run (T007), an accumulated
rather than multiplied envelope phase, a stolen grain (FR-023), or the `1/√n` smoother cadence.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_NoGrainBoundaryClicks*"` green; the reference-render
false-positive floor recorded in the TU header.

---

## Group I — Determinism, allocation and bounds

### T010 — SC-010, SC-011 and `setSeed(0)`

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_test.cpp` (header only if a defect is found).

**Failing tests first:**

`TEST_CASE("AtmosphereEngine_SeedDeterminism", "[atmosphere]")` (SC-010, FR-071):
- `fingerprintRender` / `compareFingerprints` (`tests/test_helpers/render_fingerprint.h:64`, `:101`) over a
  **20 s** render. Same seed ⇒ `withinTolerance()` (`kSampleTolerance = 1.0e-4f`, `:49`;
  `kMetricTolerance = 1e-5`, `:52`). **Never a bit-exact digest** (`node tools/lint-float-bit-goldens.js`
  gates it).
- **Negative half (without it the case passes on a silent engine):** different seeds ⇒ **not** within
  tolerance.
- **FR-044 clause, asserted on grain-birth parameters, not on the birth count:** render twice with
  identical seed and setter history, once at `blur = 0` and once at `blur = 1`, both with
  `blurEnabled = true`; require `getGrainRngState()` **identical** after both renders **and**
  `getTotalGrainsBorn()` equal. Birth *timing* comes from `GrainScheduler`'s own private
  `Xorshift32 rng_{12345}` (`grain_scheduler.h:110`, drawn at `:82`), not from the engine's grain RNG — so
  a shared stream would shift birth *parameters* while leaving the count untouched, and a count-only
  comparison would pass a genuinely broken implementation. (Runs meaningfully once blur lands in T012;
  until then assert the state equality with `blurEnabled = false` and tighten it in T012.)
- **`reset()` clause (FR-006 edge case):** render, `reset()`, render again ⇒ fingerprints within tolerance.
  This is what fails if `reset()` omits the explicit `scheduler_.seed(...)` call.

`TEST_CASE("AtmosphereEngine_BlockPartitionInvariance", "[atmosphere]")` (SC-011, FR-005):
- One 4096-sample call versus the same render split into `{1, 7, 64, 65, 511, 512, 1000}`-sample calls,
  same seed. **Bit-identical is not required and not asserted:** RMS difference ≤ **−100 dBFS** and max
  per-sample difference ≤ **1e-5**.
- **Required coverage:** at least one grain is born inside a **partial** control chunk — assert it via
  `getTotalGrainsBorn()` transitions at non-multiples of 64 — so FR-030's carry-over path is exercised
  rather than assumed. An implementation that anchors control steps to block starts, or that advances the
  drift bank once per block by `numSamples`, fails this by orders of magnitude.

`TEST_CASE("AtmosphereEngine_SeedZeroIsValid", "[atmosphere]")` (FR-070 edge):
- `setSeed(0)` and `setSeed(1)` produce **different** renders (fingerprints not within tolerance), proving
  `deriveStreamSeed`'s non-zero substitution (`core/random.h:102-111`) did not collapse two streams onto
  one via `Xorshift32::seed`'s own 0-substitution (`:73-75`).

**Verify:** all three green; zero warnings.

### T011 — SC-001 (zero allocation) and SC-008 (bounded under stress)

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_test.cpp` (header only if a defect is found).

**Failing tests first:**

`TEST_CASE("AtmosphereEngine_NoAllocationAfterPrepare", "[atmosphere]")` (SC-001):
- `TestHelpers::AllocationScope` (`tests/test_helpers/allocation_detector.h:75`) — **`allocation_detector.h`
  only**, never `allocation_operator_overrides.h` (rule 6).
- Configuration: `captureSeconds = 30`, `grainSeconds = 30`, `density = 20`, blur on, freeze on — sustained
  pool exhaustion **and** sustained trigger skipping — 10 s of `processStereoBlock` at 48 kHz in 512-sample
  blocks.
- **Pinned call schedule** (not incidental): the full setter sweep, `setSeed()`, `captureFreeze()` and
  `releaseFreeze()` across t ∈ [0, 4) s; `reset()` at **t = 4 s**; `silence()` at **t = 9 s**.
- Threshold: `REQUIRE(scope.getAllocationCount() == 0)`.
- **Precondition, not optional:** `REQUIRE(poolFullBeforeReset > 0)` where `poolFullBeforeReset` is
  `getSkippedTriggerCountPoolFull()` captured **immediately before** the `reset()` call — `reset()` zeroes
  the counter, and re-saturating afterwards costs ~1 s of ring refill plus ~3.2 s of births, so asserting
  the live counter at the end would be a timing lottery.
- The engine is `prepare`d **outside** the scope, and so are the I/O buffers.
- (Blur/freeze legs land in T012/T013 — re-run this case at the end of T013 with them enabled.)

`TEST_CASE("AtmosphereEngine_BoundedUnderStress", "[atmosphere]")` (SC-008):
- Fully pinned: full-scale white noise input, `captureSeconds = 30`, `density = 20`, `grainSeconds = 30`,
  `blur = 1.0`, freeze crossfading, `driftDepth = 1.0`, **`level = 1.0`** (pinned because it multiplies the
  threshold directly; the FR-009 maximum of 2.0 would double the bound with no defect present). There is
  no `width` to pin.
- Threshold: peak absolute output over a **10-minute** render **< 4.0**, and **every** sample finite via
  `detail::isNaN`/`detail::isInf` — never `std::isnan`.
- Note in the TU why 4.0 is the *statistical* bound and `√64 · 1 = 8` is the coherent worst case that no
  realistic configuration reaches: grains are born at independent ring positions, pitches and
  decorrelation offsets, so they sum **incoherently** with ≈unit variance under `1/√n`, and the peak over
  N samples grows as ≈`σ√(2 ln N)` ≈ 5.1σ at 10 minutes / 48 kHz. If a run approaches 4.0, investigate
  coherence — do **not** raise the threshold.

**Verify:** both green; zero warnings.

---

## Group J — Spectral blur

### T012 — Blur stage: geometry, FIFO, pump, phase randomisation, latency

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp` (the two deferred clauses).

**Failing tests first:**

`TEST_CASE("AtmosphereEngine_BlurTransparentAtZero", "[atmosphere]")` (SC-006) — **write and pass this
first; transparency proves the plumbing:**
- Per-sample difference between `blurEnabled = true, blur = 0` (delay-compensated by
  `getLatencySamples()`) and `blurEnabled = false`, same seed and identical setter history.
- Threshold: RMS difference ≤ **−60 dBFS** relative to signal RMS, after discarding the first
  `2 · fftSize` samples of OverlapAdd warm-up.
- A wrong `applySynthesisWindow`, a wrong hop, a mis-initialised FIFO, or FR-043's pull moved outside the
  drain loop each fail this loudly.

`TEST_CASE("AtmosphereEngine_BlurDisabledIsFree", "[atmosphere]")` (FR-045, FR-046, main TU):
- `blurEnabled = false` ⇒ `getLatencySamples() == 0` and `setBlur(1.0f)` changes nothing (bit-identical
  output against a `setBlur(0)` render with the same seed).
- A second `prepare` with `blurEnabled = false` allocates strictly less than with it true.

Add to `AtmosphereEngine_ControlTableClamps` (deferred from T004) the **blur smoother-cadence clause**
(D-15): with `blurEnabled = true`, step `setBlur` 0 → 1 and measure the samples until the applied blur
reaches `1 − 1/e`; it must match `kBlurSmoothMs = 50 ms` within **10 %**, and must **not** land near
**25 ms** — which is exactly what an `advanceSamples(blurHopSize_)` left inside the per-channel loop
produces, and which SC-005 (settled values only) cannot see.

**Implement —** plan §11:

- Geometry: `fftSize = blurFftSize_` (snapped), `hopSize = fftSize/4` (**75 % overlap**),
  `WindowType::Hann`, `applySynthesisWindow = true` — mandatory at this overlap
  (`primitives/stft.h:201-204`) and forbidden at 50 %. Assert `hopSize == fftSize/4` in `prepare`.
- **Pump loop order is load-bearing (FR-043):** frame-major, channel-minor, and the
  `pullSamples(fifoScratch_[ch].data(), blurHopSize_)` **must be inside** the drain loop —
  `OverlapAdd::synthesize` always accumulates at `outputBuffer_[0 .. fftSize)` with no offset
  (`stft.h:277-285`) and the hop offset comes **only** from `pullSamples` shifting the buffer left
  (`:309-323`); two synthesizes without an intervening pull stack both frames and destroy COLA.
- **Chunk-bounded pushing (D-2):** at most `kControlChunkSamples = 64` samples are pushed between drains,
  which keeps `STFT::samplesAvailable_ ≤ fftSize + 64` at every legal geometry. `pushSamples` has **no
  overflow guard** (`stft.h:104-113`) and its input buffer is `8·fftSize`.
- **Phase randomisation (FR-042):** `blurSmoother_.advanceSamples(blurHopSize_)` **once per frame-pair,
  outside the per-channel loop**, and the one value it yields is used by L and R. Then per channel, L
  first, `for (k = 1; k + 1 < numBins; ++k) setPhase(k, getPhase(k) + blur · kPi · blurRng_.nextFloat())`.
  DC and Nyquist are untouched **and consume no draw** (D-6 — the whole stream is pinned by SC-010).
  Magnitude is never written. The draw is **per bin per channel**, from the one `blurRng_` stream, which is
  what produces progressive L/R decorrelation as well as fog.
- **FIFO (§11.3):** `fifoCapacity = bit_ceil(blurFftSize_ + max(maxBlockSamples_, 64) + blurHopSize_)`
  (4096 at the defaults), power-of-two with `& mask_`, ring invariant
  `blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_` stated at the declarations.
- **`reset()` FIFO state (D-14), the single easiest thing to get wrong:** `blurFifoRead_ = 0`,
  `blurFifoWrite_ = blurFftSize_ & blurFifoMask_`, `blurFifoCount_ = blurFftSize_`, all guarded on
  `blurEnabled_`. Setting both cursors to 0 with a non-zero count leaves the reader permanently
  `blurFftSize_` indices ahead: real latency becomes the FIFO capacity (4096), `getLatencySamples()`
  under-reports by 3072, and SC-006 and SC-007 fail **as a COLA/windowing bug**.
- `blurRng_` is a **separate** `Xorshift32` from `grainRng_` (FR-044).
- `getLatencySamples()` returns `blurEnabled_ ? blurFftSize_ : 0` — the latency of the **whole layer**,
  both crossfade legs (T013 delay-matches the freeze leg to it). The freeze oscillator's own
  `getLatencySamples()` (`processors/spectral_freeze_oscillator.h:421-423`) is **not** added.

**Verify:** SC-006 green **first**, then `AtmosphereEngine_BlurDisabledIsFree` and the cadence clause;
re-run T010's SC-010 FR-044 clause with `blurEnabled = true` and both blur values. Zero warnings.

### T013 — SC-005: blur monotonicity, decorrelation and crest factor

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp`.

**Failing test first —** `TEST_CASE("AtmosphereEngine_BlurMonotonicity", "[atmosphere]")`:

- **Input:** T009's pinned `makeHarmonicStack()` (220 Hz, partials 2×…9× at `1/n`, peak 0.5).
- **Metric:** `Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness(signal, n, sampleRate)`
  (`tests/test_helpers/signal_metrics.h:326`) — **fully qualified**, because a same-named 2-argument
  overload exists at `dsp/include/krate/dsp/primitives/spectral_utils.h:335` and an unqualified call can
  bind the wrong one.
- **Measurement method (binding — the naïve call cannot see the signal).** The helper picks `fftSize` as
  the largest power of two ≤ `n` capped at 4096 (`:336-339`) and fills its window from
  `signal[0 .. fftSize)` **only** (`:350-352`). Under this spec's defaults those samples are **exactly
  zero** (`positionSeconds = 1.0` ⇒ no birth before 48 000 samples, plus 1024 of blur latency), and on an
  all-zero window it returns `0.0f` (`arithMean < 1e-10` early return, `:376-378`) — so every flatness is
  0, "non-decreasing" holds trivially and the ratio clause reduces to `0 ≥ 0`: **the criterion would pass
  on a silent engine.** Therefore:
  1. render past `settleSamples ≥ positionSeconds · sampleRate + 2 · blurFftSize`;
  2. pass `render.data() + settleSamples` with a length of **exactly 8192**, so the helper selects
     `fftSize = 4096` — deliberately longer than the 1024 blur FFT, so inter-frame decoherence widens
     lines into skirts;
  3. average over **four disjoint 8192-sample windows** from the settled region;
  4. assert the **non-silence precondition before any threshold**: `REQUIRE(rmsDb(window) > -40.0f)` on
     every window analysed **and** `REQUIRE(flatness(0.0) > 0.0f)`.
- **Threshold:** over `blur ∈ {0.0, 0.25, 0.5, 0.75, 1.0}` the averaged flatness is **non-decreasing** at
  every step (2 % epsilon) and `flatness(1.0) ≥ 1.25 · flatness(0.0)`.
- **Stereo-decorrelation clause (FR-042):** over the same windows, normalised inter-channel correlation
  `ρ(L, R)` is **non-increasing** across the sweep (2 % epsilon) and `ρ(1.0)` is at least **0.2** below
  `ρ(0.0)`. An implementation applying one draw to both channels is a defect no other criterion sees.
- **Crest-factor clause, with its input pinned too:**
  `…::SignalMetrics::calculateCrestFactorDb` (`signal_metrics.h:222`) on a file-local `makeImpulseTrain()`
  — otherwise-silent stereo at 48 kHz with `x[n] = 1.0f` on both channels at every multiple of **24 000**
  (one impulse every 0.5 s), rendered **20 s**, measured over samples
  `[settleSamples, settleSamples + 480 000)`. Crest at `blur = 1.0` at least **3 dB** below `blur = 0.0`.
- **All three floors (1.25×, 0.2, 3 dB) are minimums (O-2):** replace each with the **measured** value less
  a stated margin, record all three measured values and their margins in the TU header, and only ever move
  them **up**.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_BlurMonotonicity*"` green with the measured floors
recorded; zero warnings.

---

## Group K — Pure freeze

### T014 — Freeze leg: capture, delay match, crossfade, bypass, `reset()`

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h`,
`dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing tests first:**

`TEST_CASE("AtmosphereEngine_FreezeStability", "[atmosphere]")` (SC-007, spectral TU):
- **Configuration: `blurEnabled = true`**, so the delay-matched leg is in the path — an uncompensated
  1024-sample offset presents as a step at the crossfade and this is the only place in the spec that
  would see it.
- Metric: after `captureFreeze()` and a settled `freezeMix = 1.0`, cut the render into successive
  non-overlapping 1 s windows; `peak(k) = max |y[n]|` over window `k`.
- Threshold: windows **2 … 60** each within **±1.0 dB** of `peak(2)`, and `peak(2) ≥ −60 dBFS`. The
  reference is the **second** window: the first necessarily contains the 100 ms `LinearRamp` crossfade
  **and** the oscillator's own overlap-add pre-fill
  (`processors/spectral_freeze_oscillator.h:261-287`).
- Plus: crossfading `freezeMix` 0 → 1 → 0 produces **0** `ClickDetector` detections using T009's pinned
  config.

`TEST_CASE("AtmosphereEngine_FreezeCaptureAndRelease", "[atmosphere]")` (FR-050 … FR-054, main TU):
- `captureFreeze()` before the ring holds `freezeOsc_[0].getFftSize()` samples is a **no-op**, not a
  partial capture (output unchanged).
- After a valid capture, `freezeMix = 1` is non-silent.
- `releaseFreeze()` fades within one hop.
- With `freezeEnabled = false`, `setFreezeMix` / `captureFreeze` / `releaseFreeze` are all inert and
  nothing is allocated for the leg.

**Implement —** plan §12:

- **Two** `SpectralFreezeOscillator` instances (L and R) — `freeze()` and `processBlock()` are mono
  (`spectral_freeze_oscillator.h:217`, `:317`) — each prepared at the **snapped** `freezeFftSize`.
- `captureFreeze()`: extraction length is the oscillator's own `getFftSize()` (`:426-428`), **never** the
  requested `config.freezeFftSize` — `freeze()` truncates at `fftSize_` (`:222-223`), so a longer capture
  silently discards the newest audio and a shorter one zero-pads. Source is
  `capture_.extractSlice(freezeCapture_[0].data(), freezeCapture_[1].data(), need, 0)`.
- **Delay-matched leg (FR-052):** when `blurEnabled && freezeEnabled`, route the drone through a
  `prepare`-allocated **`blurFftSize_`-sample** stereo delay before the crossfade, so both legs share one
  latency. The drone is **never** routed through the STFT stage (the spectral hold stays pure). The delay
  **is still advanced with zeros while in hard bypass** (D-8) — one load + one store per sample per
  channel, which is what makes leaving bypass click-free without an O(fftSize) memset spike.
- **Hard bypass at settled `m = 0` only.** There is **no symmetric bypass at `m = 1`**: at a settled
  `freezeMix = 1.0` the grain layer keeps running in full — scheduler, ageing, ring reads, `1/√n` and blur
  — so releasing the freeze is seamless (a bypass would restart from an empty pool and swell back over
  `density × grainSeconds` seconds) and SC-004(d) measures the honest grain+freeze worst case.
- Crossfade is **linear**, per output sample, from a 100 ms `LinearRamp`.
- **`reset()` (D-10):** call `freezeOsc_[ch].reset()` — public
  (`spectral_freeze_oscillator.h:173-196`; `public:` at `:81`, `private:` at `:435`), documented
  "Real-time safe", nine `std::fill`s plus `workingSpectrum_`/`formantPreserver_` resets, early-out on
  `!prepared_`. **Do not** substitute `unfreeze()` + a one-hop drain: `unfreeze()` sets
  `unfadeSamplesRemaining_ = hopSize_` (`:299`) and `processBlock` reaches `frozen_ = false` (`:352-357`)
  only on the sample *after* it hits zero, so draining exactly `getHopSize()` samples leaves
  `frozen_ == true` with state uncleared. Then zero the freeze-delay ring and `freezeDelayIdx_`.

**Verify:** both cases green; **re-run** T011's SC-001 with blur **and** freeze enabled (its configuration
requires them) and T010's SC-010 `reset()` clause — the latter is exactly what the deleted `unfreeze()`
rewind would have failed. Zero warnings.

---

## Group L — Non-finite hygiene and sample-rate independence

### T015 — SC-014: non-finite hygiene, the latch, and the fast-math clause

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp`,
`dsp/tests/unit/systems/atmosphere_engine_test.cpp` (fourth clause), and
`atmosphere_engine.h` only if O-1 resolves to a test hook.

**Failing tests first —** `TEST_CASE("AtmosphereEngine_NonFiniteHygiene", "[atmosphere]")` in the
**nonfinite** TU (the one carrying `-fno-fast-math -fno-finite-math-only` from T002):

- **Construction:** `makeNonFinite(bits)` — `volatile std::uint32_t` + `std::memcpy` (defeats constant
  folding); `0x7FC00000` quiet NaN, `0x7F800000` +Inf, `0xFF800000` −Inf. Never
  `std::numeric_limits<float>::quiet_NaN()`/`infinity()`.
- **(a)** Inject NaN and ±Inf input samples: **every** output sample is finite by
  `detail::isNaN`/`detail::isInf`.
- **(b) The ring is preserved** — the deliberate difference from a silence-on-NaN policy: after the
  injection, feed silence and confirm grains still reproduce the **pre-injection** audio (correlation
  **≥ 0.99** against the same render without injection, over a window whose birth read age predates the
  injection), and `getCaptureCapacitySamples()` is unchanged.
- **(c) 0** `ClickDetector` detections across the injection window, using T009's pinned config.
- **Separate sub-case — the internal path and the latch:** force the engine's **own** state non-finite,
  then assert `silence()` fires, grains retire under the 10 ms ramp, the output returns to exact zero and
  **stays** exactly `0.0f` with `getActiveGrainCount() == 0` for the remainder of the render (no
  auto-resume), until `reset()`, after which the engine renders non-silent audio again once the ring
  refills.
  **Resolve O-1 here, in this order:** (i) find a legitimate arithmetic route (an extreme
  `captureSeconds`/`sampleRate`/coefficient combination) — preferred, because it tests a real hazard;
  (ii) if none exists, add a `#if defined(KRATE_TESTING)` injection point on the bus accumulator, declared
  in the header next to `chunkPoisoned_`, and record that fact in the compliance document. **Do not drop
  the clause** — FR-063's second half and FR-007's latch are otherwise unmeasured, and an implementation
  that silently resumes would look identical under every other criterion.
- **Negative control (do it, then revert):** replace the latch with an auto-resume → the sub-case must fail.

**Fourth clause, in the MAIN TU** —
`TEST_CASE("AtmosphereEngine_NonFiniteGuardSurvivesFastMath", "[atmosphere]")`: repeat clause (a)'s
injection and clause (b)'s ring-preservation check verbatim in `atmosphere_engine_test.cpp`, which is
**deliberately absent** from the `-fno-fast-math` list and therefore builds with MSVC `/fp:fast` and the
macOS leg's `-ffast-math`. Assertions: every output sample finite; `getCaptureCapacitySamples()` unchanged.
The three clauses above only prove the guards work in the one configuration the shipped header will never
be compiled in; SC-013's grep sees symbol names, not whether the branch survived optimisation. If
`ITERUM_NOINLINE isFinite` or the ordered-comparison clamp is folded away, **this** case is where it
surfaces.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_NonFinite*"` green; zero warnings.

### T016 — SC-009: sample-rate independence

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_test.cpp`.

**Failing test first —** `TEST_CASE("AtmosphereEngine_SampleRateIndependence", "[atmosphere]")`:

- Measure at **44 100 / 48 000 / 96 000 Hz** with identical settings and seed: grain lifetime in
  **seconds** from `getLastBornGrainLifetimeSamples() ÷ that rate` (never inferred from block-granular
  active-count transitions), mean concurrent grain count, and output RMS.
- **Clause 1 — non-truncating sweep (stated as a precondition):** only configurations satisfying
  `w·L ≤ C − 2 − g` are swept here. Lifetime within **0.5 %** of the requested seconds at every rate; mean
  concurrent count within **5 %**; output RMS within **1.0 dB**.
- **Clause 2 — truncating, rate-aware:** for configurations where truncation binds (e.g.
  `grainSeconds = 30`, `pitchSemitones = ±12`), measured `L'` within **0.5 %** of
  `⌊(C − 2 − g − 2)/w⌋ / sampleRate` computed from **that rate's own** `getCaptureCapacitySamples()`.
  A single rate-invariant expectation is a false failure: `RollingCaptureBuffer::prepare` rounds capacity
  **up to the next power of two** (`rolling_capture_buffer.h:83`, `:210-220`), so `captureSeconds = 8`
  yields **11.89 s** of ring at 44.1 kHz but **10.92 s** at 48 and 96 kHz — an 8.8 % spread in
  `C/sampleRate` that propagates straight into `L'`.
- **Allocation clause (D-18), stated as the property it protects, not as an equality that cannot hold:**
  1. `REQUIRE(secondPrepareCount <= freshPrepareCount)` — a re-`prepare` at a new rate can only allocate a
     **subset** of what a fresh prepare allocates (every path uses `resize`/`assign` on already-sized
     buffers: `rolling_capture_buffer.h:86-87`, `spectral_buffer.h:63-65`, eleven `resize`s in
     `SpectralFreezeOscillator::prepare`); a count **above** it means a buffer is being reallocated rather
     than reused. Requiring **equality** fails green code.
  2. `REQUIRE(renderScope.getAllocationCount() == 0)` around a **full render at the new rate** immediately
     after that second `prepare()` — the property that matters: re-prepare must leave nothing undersized,
     and an undersized buffer shows up as an audio-thread allocation on the very first block.
- Then the engine must be silent-but-usable (FR-014's cold ring): assert a non-silent render after the
  ring refills.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_SampleRateIndependence*"` green; zero warnings.

---

## Group M — CPU budget

### T017 — `AtmosphereEngine_GrainSampleCost` (runs first; it decides O-3)

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp`.

**Do:**

- `TEST_CASE("AtmosphereEngine_GrainSampleCost", "[.perf]")`, same trial shape as T018: best-of-**25** ×
  **500** blocks after **400** warm-up blocks, 512-sample blocks at 48 kHz.
- Report **ns per grain-sample** = `(ns/block) / (activeCount × 512)` at the **worst case** —
  `captureSeconds = 30`, `decorrelation = 1.0`, `positionSpread = 1.0`, pool saturated, so the read points
  are maximally scattered — and at the **8 s default** for contrast.
- This case **reports** via `WARN`/`INFO`; it does not gate. It exists because the arithmetic ceiling
  (106 667 ns / (64 × 512) = **3.25 ns per grain-sample**) is not a cost model: with 64 grains × two
  decorrelated read points × two channels there are up to ~128 independent non-sequential streams into a
  ring of up to 16.8 MB, and one L3/DRAM miss exceeds the whole allowance.
- Its number is what decides whether SC-004 lever (5) (reduce `kMaxGrains`) is spent in T019.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_GrainSampleCost*"` runs and reports; the two ns
figures recorded.

### T018 — `AtmosphereEngine_CpuBudget`: five measured baselines

**Files edited:** `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp`.

**Do:**

- Constants, from `harmonic_cloud_perf_test.cpp:69-101` / `continuous_body_perf_test.cpp:108-137`:
  ```cpp
  constexpr double kSr48 = 48000.0;
  constexpr std::size_t kBlockSize = 512;
  constexpr double kBlockBudgetNs   = (512.0 / 48000.0) * 1e9;   // 10,666,666.67
  constexpr double kRegressionFactor = 1.5;
  constexpr double kReference1PctNs = kBlockBudgetNs * 0.01;     // 106,666.67
  ```
- Trial shape: best-of-**25** × **500** blocks after **400** warm-up blocks (many short trials — the dev
  machine is a hybrid part and the dominant noise source is a trial migrating onto an E-core; affinity
  pinning was tried and rejected in both precedent files).
- **Five configurations, each with its own baseline, every one gated against the same 1 % reference — no
  configuration exempted:**
  (a) defaults, blur off, freeze off — the roadmap's "default density", 4 grains/s × 4 s = **16
  concurrent** (OQ-1);
  (b) defaults, blur on;
  (c) pool **saturated** (64 concurrent), blur on, `blurFftSize = 256` — the most expensive blur geometry,
  8 frames per 512-block;
  (d) `freezeMix = 1.0` **with the grain layer still running** — the honest grain+freeze worst case, the
  number Phase 7 inherits for a frozen voice;
  (e) configuration (b) plus `setGrainEnvelope` called once per block with an **alternating** type — the
  case the §7 idempotence guard cannot elide, so the full 4096-entry regeneration runs every block.
- **Two distinct compile-time clauses per baseline, plus the runtime bound:**
  ```cpp
  static_assert(kBaselineX * kRegressionFactor <= kReference1PctNs,
                "SC-004 (x): baseline must be no weaker than the 1 % reference");
  static_assert(kBaselineX >= kReference1PctNs / 50.0,
                "SC-004 (x): a baseline below reference/50 was recorded from a no-op or "
                "misconfigured run - the measurement, not the threshold, is wrong");
  REQUIRE(measured <= kBaselineX * kRegressionFactor);
  ```
  The **floor** is not decorative: a baseline accidentally recorded from a no-op run satisfies the headroom
  clause trivially and then makes the runtime `REQUIRE` fail spuriously on slower CI hardware.
- **Structural clauses**, so the TU stops compiling rather than silently measuring something else:
  ```cpp
  static_assert(kBlockSize % AtmosphereEngine::kControlChunkSamples == 0);
  static_assert(kBlockSize / AtmosphereEngine::kControlChunkSamples == 8);
  static_assert(AtmosphereEngine::kMaxGrains == 64,
                "SC-004(c) measures the saturated pool; if kMaxGrains moved under lever (5), "
                "this number and FR-073's documented operating region move with it");
  ```
- **Baselines are measurements, not allowances:** each is the **worst (largest) of eight consecutive
  best-of-25 runs** on the machine named in a **BASELINE PROVENANCE** block in the TU header, with **no
  padding**. All five measured ns/block figures go verbatim into the phase's compliance document — RA-4:
  the roadmap's per-phase budgets sum to 45 % against its own 25 % Phase 7 ceiling, so Phase 7 needs five
  real numbers from here, not a ceiling nobody approached.

**Verify:** `dsp_systems_tests.exe "AtmosphereEngine_CpuBudget*"` green; five figures recorded.

### T019 — Spend an SC-004 lever, only if T018 shows one is needed

**Files edited:** `dsp/include/krate/dsp/systems/atmosphere_engine.h` (+ the perf TU's constants).

**Do —** in this order, **never** raising a baseline and **never** relaxing the reference:

1. Verify FR-052's freeze hard-bypass actually engages at settled `m = 0`.
2. Verify FR-005's control-step decimation fires — a bug that refreshes per sample pays the
   scheduler/drift-lane cost 64× over.
3. Drop `blurFftSize` default to **512** (halves frame count and per-frame cost, at coarser frequency
   resolution) — a *specified capability trade*, flagged in the header banner and the compliance document
   (O-4).
   3b. Swap `ratioAtPitch` to `centsToPitchRatio(semitones * 100.0f)` (`core/pitch_utils.h:33-36`, one
   `std::exp2`) — 512 `powf` per block at saturation is the largest arithmetic term, and this is the exact
   lever `HarmonicCloud` spent. **Note:** `centsToPitchRatioFast` (`:41-43`, `:59-68`) is **unusable
   here** — a degree-4 Horner polynomial accurate only on ±50 cents, against Phase 5's ±3600 cent domain.
   Expected saving is therefore smaller than `HarmonicCloud`'s and is **unmeasured**.
4. Confirm the equal-power pan `cos`/`sin` are still birth-time only.
5. **Reduce `kMaxGrains`** below 64 and shrink FR-073's documented `density × grainSeconds` operating
   region to match — FR-022 declares 64 provisional precisely so this lever exists. If taken, the header
   banner, `static_assert(kMaxGrains == 64)` in the perf TU, SC-004(c)'s "saturated" configuration and
   SC-003's D-17 precondition table all move **together**.
6. Only then escalate.

**Verify:** re-run T018 and **every other suite** — a lever that changes `kMaxGrains` or the ratio helper
changes renders, so SC-002, SC-003, SC-005, SC-010 and SC-011 must all be re-run green.

---

## Group P — Integration

### T020 [P] — CMake registration audit

**Files:** none edited. Read-only gate.

**Do:**
```bash
grep -n "atmosphere_engine" dsp/tests/CMakeLists.txt
grep -n "test_rolling_capture_buffer" dsp/tests/CMakeLists.txt
build/windows-x64-release/bin/Release/dsp_systems_tests.exe --list-tests | grep -c AtmosphereEngine
```
Assert **all** of:
1. All four TUs are in the `dsp_systems_tests` source list (after the Phase 4 block at `:343-346`).
2. **Only** `unit/systems/atmosphere_engine_nonfinite_test.cpp` of the four is in the `-fno-fast-math`
   list — `atmosphere_engine_test.cpp` must **not** be there (SC-014's fourth clause and SC-012's
   sub-case 6 lose their teeth if it is), and neither must the perf TU (`-fno-fast-math` would change the
   figures the baselines are pinned to).
3. `unit/primitives/test_rolling_capture_buffer.cpp` is still in **both** lists (`:117`, `:463`) and was
   not moved or duplicated.
4. Every `TEST_CASE` named in this task list is discovered by `--list-tests`; no T002 placeholder case
   survives.

### T021 — Full-suite matrix and zero-warning build

**Files:** none edited.

**Do:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release \
  --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests 2>&1 | tee /tmp/p5-build.log
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_CpuBudget*"
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_GrainSampleCost*"
```
Assert: **zero** compiler warnings across all five targets in the captured log; every suite "All tests
passed"; both `[.perf]` cases green when named explicitly (they are tag-excluded from the default run).
`dsp_primitives_tests` and `dsp_effects_tests` are the RA-1 regression gate (SC-012) and must be green with
**no existing test file edited**. A failure anywhere is **owned** — never dismissed as pre-existing.

### T022 [P] — Portability, lint and symbol gates (SC-013)

**Files:** none edited.

**Do:**
```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-float-bit-goldens.js
node tools/lint-arch-guarded-includes.js
node tools/lint-simd-aligned-loadstore.js
node tools/lint-allocation-operator-overrides.js
rg -n "std::isnan|std::isinf|numeric_limits<float>::infinity|numeric_limits<double>::infinity" \
   dsp/include/krate/dsp/systems/atmosphere_engine.h && exit 1 || true
```
Plus a **WSL g++ syntax check** of the changed TUs — MSVC-green proves nothing about the GCC/Clang legs
(`std::bit_ceil`/`bit_floor`/`has_single_bit` need `<bit>`; narrowing in brace-init is an error on Clang and
a warning on MSVC).

**Verify:** every command clean; the `rg` gate exits 0 with **no** match. It is a **scripted** gate, not a
review step — a manual check is not a gate.

---

## Coverage map

| Criterion / requirement group | Task |
|---|---|
| ODR sweep, CMake facts, baseline | T001 |
| Test registration (the one `dsp/tests/CMakeLists.txt` edit) | T002 |
| FR-080 … FR-084, SC-012 | T003 |
| FR-001 … FR-009 (surface), FR-072 accessors | T004 |
| FR-030 (drift lanes), SC-002 lane-equivalence gate | T005 |
| FR-010 … FR-014, FR-020 … FR-026, FR-029 … FR-034, SC-002 clauses 1–4 | T006 |
| FR-027 (envelope endpoints, `L'−1` phase) | T007 |
| FR-028, FR-060 … FR-064, FR-007 latch | T008 |
| SC-003 | T009 |
| FR-005, FR-006, FR-044, FR-070, FR-071, SC-010, SC-011 | T010 |
| SC-001, SC-008 | T011 |
| FR-040 … FR-046, SC-006 | T012 |
| SC-005 | T013 |
| FR-050 … FR-054, SC-007 | T014 |
| FR-063 + FR-007 latch under both FP settings, SC-014 | T015 |
| SC-009 | T016 |
| FR-022 micro-benchmark (O-3) | T017 |
| SC-004 (five baselines) | T018 |
| SC-004 levers (O-3, O-4) | T019 |
| Registration audit | T020 |
| Full matrix, zero warnings | T021 |
| SC-013 | T022 |

The compliance document (plan §19 T14) — every FR cited to a `file:line` in the delivered header, every SC
to a test name and its **actual** measured output, the five SC-004 ns/block figures verbatim, SC-005's and
SC-007's measured floors (O-2), and whichever levers were spent — is produced after this task list, not as
part of it.
