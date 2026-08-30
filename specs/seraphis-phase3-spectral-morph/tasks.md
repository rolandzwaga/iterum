# Tasks: Seraphis Phase 3 — Spectral States & Morphing Engine

**Spec:** `specs/seraphis-phase3-spectral-morph/spec.md`
**Plan:** `specs/seraphis-phase3-spectral-morph/plan.md` (authoritative for every constant, law and
deviation quoted below — where a task quotes a plan section, read that section before writing code)
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 3 (lines 170–192)
**Plugin work:** none. No `plugins/` file is edited in this phase; pluginval is not run.

---

## How to read this document

- Tasks are grouped. **Groups run strictly in order.** A group does not start until every task in the
  previous group is green.
- Within a group, tasks marked **[P]** are parallel-safe: they touch fully disjoint **new** files.
  Unmarked tasks in a group run in the listed order.
- Every task that edits an **existing** file (`core/random.h`, `core/pitch_utils.h`,
  `core/db_utils.h`, `systems/harmonic_cloud.h`, `dsp/tests/CMakeLists.txt`, the two Layer 0 test
  TUs) is alone in its group. Two tasks never edit the same file concurrently.
- **Canonical order inside every task:** write the failing test first → run it and see it fail →
  implement → zero compiler warnings → the named target green.
- **No commit tasks.** Commits happen outside this workflow.

### Ordering deviation (stated, not silent)

`dsp/tests/CMakeLists.txt` is a shared file with **exactly one owning task**. That task (**T002**) is
placed **early** rather than in the final group, because `dsp/tests/CMakeLists.txt` lists test sources
explicitly (never globbed — verified this session, `dsp/tests/CMakeLists.txt:32,40,275,279,332-334`),
so an unregistered TU is silently not compiled and **no** task from T007 onward could satisfy its own
"run the test and see it fail" step. The final group therefore carries a CMake **audit** task
(**T023**), not a second registration edit.

### Build / run commands (Windows — always the full CMake path)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests

build/windows-x64-release/bin/Release/dsp_core_tests.exe       2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe    2>&1 | tail -5

# One case (positional arg — NOT -c, which filters SECTIONs):
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph_TravelIsContinuous"
# [.perf] cases are hidden by default and must be named explicitly:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph_CpuBudget"
```

### Cross-cutting rules that apply to every task

- **RT safety:** no allocation, lock, exception or I/O on the audio thread. `prepare()` is the only
  method permitted config-rate work, and it is documented *"not RT-safe by contract"* while still
  being declared `noexcept` (SC-011 `static_assert`s over the full public surface).
- **Layer discipline:** Layer 0 `core/` → stdlib only; Layer 2 `processors/` → Layer 0/1 (+ intra-L2);
  Layer 3 `systems/` → Layer 0/1/2. `spectral_morph_engine.h` **must never** include
  `harmonic_cloud.h`.
- **ODR:** before writing ANY new class/struct/free-function/namespace-scope constant not already
  listed in plan §7, run `grep -rn "class <Name>\|struct <Name>" dsp/ plugins/` and record the result.
  (Swept clean this session for `SpectralState`, `EntropyProcessor`, `SpectralMorphEngine`,
  `deriveStreamSeed`, `centsToPitchRatio` — zero hits across `dsp/` and `plugins/`.)
- **No bit-exact float goldens over rendered audio.** The *only* exact digest in this phase is
  SC-007's, over a **serialized byte stream**, and the test must label it as such. Renders are pinned
  with `tests/test_helpers/render_fingerprint.h` (`kSampleTolerance = 1.0e-4f`,
  `kMetricTolerance = 1.0e-5`, `kRenderCheckpoints = 32`).
- **Portability:** MSVC-green proves nothing. Include what you use (`<cstddef>`, `<type_traits>`,
  `brownian_drift.h`); never `std::isnan`/`std::isinf` — use `detail::isNaN`/`detail::isInf`
  (`core/db_utils.h:54`, `:174`); build non-finite test inputs from **bit patterns via a `volatile`
  sink**, never `std::numeric_limits<float>::quiet_NaN()`; no narrowing in brace init (use designated
  initializers); `hn::LoadU`/`StoreU` if any SIMD is added.
- **No test file in this phase may include `allocation_operator_overrides.h`** — both
  `dsp_processors_tests` and `dsp_systems_tests` already link it (via
  `unit/processors/brownian_drift_test.cpp:28` and `unit/systems/selectable_oscillator_test.cpp:388`).
- **Naming:** classes PascalCase, functions camelCase, members trailing underscore, constants
  `kPascalCase`.
- **Zero compiler warnings.** `C4244` → add `f` suffix; `C4267` → explicit cast; `C4100` →
  `[[maybe_unused]]`.
- **Pinned seed set** — shared by SC-002 cl.3/4, SC-004, SC-005, SC-006, SC-016. Declare once per TU
  as a file-scope constant, exactly:
  ```cpp
  constexpr std::array<std::uint32_t, 8> kSeeds{ 1u, 7u, 13u, 29u, 101u, 257u, 1009u, 65537u };
  ```
  SC-001 clause 2 and SC-015 use its first four.
- **Note lifecycle for every rendered row** (T019–T021): call `cloud.noteOn()` before the first slice
  and **never** `noteOff()`. A freshly-prepared `HarmonicCloud` has `gate_ = false`, so
  `isQuiescent()` is true (`harmonic_cloud.h:844-853`) and `processStereoBlock` takes the
  zero-fill early-out (`:701-709`) — without the note every rendered criterion measures silence.
- **Never loosen a threshold to make a test pass.** Each threshold is either derived in a comment
  beside it or measured and recorded with provenance.

---

## GROUP A — Blocking prerequisite (nothing else may start)

### T001 — Capture `kPreAmendmentFingerprints[216]` on the pre-amendment build

**Blocking. Must complete before ANY edit to `dsp/include/krate/dsp/systems/harmonic_cloud.h` or
`dsp/include/krate/dsp/core/random.h`.** SC-014 clause 1 compares the post-amendment render against
the *pre*-amendment build, which will not exist at test-run time (plan §1 T0.1, risk R3).

**Files**

- CREATE (permanent, checked in):
  `dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h`
- CREATE then DELETE (throwaway capture TU):
  `dsp/tests/unit/systems/harmonic_cloud_fingerprint_capture.cpp`
- EDIT then REVERT: `dsp/tests/CMakeLists.txt` — temporarily append the capture TU to the
  `dsp_systems_tests` source list (the list ends at `unit/systems/harmonic_cloud_perf_test.cpp`,
  line 334). **Revert this line before the task ends.**

**Work**

1. Confirm the working tree has **no** edit to `harmonic_cloud.h` / `random.h` (`git status`,
   `git diff --stat` on those two paths must be empty).
2. Write the capture TU: a Catch2 `TEST_CASE("HarmonicCloud_CaptureFingerprints", "[.capture]")`
   that walks the **216-cell grid** and for each cell renders and prints a
   `TestUtils::RenderFingerprint` (`tests/test_helpers/render_fingerprint.h:54`) as a C++
   initializer-list line.
   Grid (3 · 3 · 2 · 3 · 2 · 2 = **216**), in this exact nesting order:
   - `richness ∈ {0.0f, 0.5f, 1.0f}` (`setRichness`, `harmonic_cloud.h:370`)
   - `spectralGravity ∈ {-1.0f, 0.0f, 1.0f}` (`setSpectralGravity`, `:436`)
   - `inharmonicity ∈ {0.0f, 0.05f}` (`setInharmonicity`, `:384`)
   - `spectralTiltDb ∈ {-12.0f, 0.0f, 12.0f}` (`setSpectralTiltDb`, `:397`)
   - `mutation ∈ {0.0f, 1.0f}` (`setMutation`, `:410`)
   - `driftDepthCents ∈ {0.0f, HarmonicCloud::kMaxDriftCents}` (`setDriftDepthCents`, `:459`)
   Per cell: fresh `HarmonicCloud`, `setSeed(1u)`, `prepare(48000.0)`, `setFundamentalHz(110.0f)`,
   the six setters above, `noteOn()`, then render **2.0 s** (96,000 samples) in 512-sample blocks
   through `processStereoBlock`, fingerprinting the left channel.
3. Build `dsp_systems_tests`, run
   `dsp_systems_tests.exe "HarmonicCloud_CaptureFingerprints"`, capture stdout **to a file on the
   first run** (do not re-run to re-read output).
4. Paste the 216 rows into `harmonic_cloud_pre_amendment_fingerprints.h` as
   `constexpr std::array<TestUtils::RenderFingerprint, 216> kPreAmendmentFingerprints{ … };`
   (or a POD mirror if `RenderFingerprint` is not literal — check before assuming), guarded with
   `#pragma once`, inside `namespace SeraphisPhase3TestData`.
5. Immediately above the array, write a **PROVENANCE block in the shape of
   `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:104-122`**, naming: git commit SHA
   (`git rev-parse HEAD`), machine, OS, compiler + version, build config (`windows-x64-release`,
   Release), date, and the grid definition verbatim.
6. Delete the capture TU and revert the `dsp/tests/CMakeLists.txt` line.

**Verify:** `dsp_systems_tests` builds clean after the revert; the header compiles standalone
(`node tools/check-portability.js` clean); the array has exactly 216 entries and the provenance block
is complete. `git diff` shows **only** the new header (plus the deleted capture TU already gone).

---

## GROUP B — Test-target registration (single owner of `dsp/tests/CMakeLists.txt`)

### T002 — Register the five new test TUs and create compiling stubs

**File (EDIT, sole owner):** `dsp/tests/CMakeLists.txt`

- Append to the **`dsp_processors_tests`** source list (currently ends at
  `unit/processors/life_modulators_perf_test.cpp`, line 281):
  ```
  unit/processors/spectral_state_test.cpp
  unit/processors/entropy_processor_test.cpp
  ```
- Append to the **`dsp_systems_tests`** source list (currently ends at
  `unit/systems/harmonic_cloud_perf_test.cpp`, line 334):
  ```
  unit/systems/spectral_morph_engine_test.cpp
  unit/systems/spectral_morph_render_test.cpp
  unit/systems/spectral_morph_perf_test.cpp
  ```

**Files (CREATE, stubs):** the five `.cpp` paths above. Each stub is exactly:

```cpp
// <filename> — Seraphis Phase 3. Populated by a later task; see specs/seraphis-phase3-spectral-morph/tasks.md
#include <catch2/catch_test_macros.hpp>
```

**Do NOT add any source property.** No TU in this phase needs `-fno-fast-math`: every non-finite test
input is built from bit patterns (plan §10).
**Do NOT switch any list to a glob.** Sources are listed explicitly; an unlisted file drops silently.

**Verify:** `dsp_processors_tests` and `dsp_systems_tests` both build clean and run; test counts are
unchanged from before (stubs register no cases).

---

## GROUP C — `core/random.h` (existing Layer 0 header)

### T003 — `deriveStreamSeed` (FR-006)

**Test first.** EDIT `dsp/tests/unit/core/random_test.cpp` — add
`TEST_CASE("DeriveStreamSeed_IsNonZeroAndDistinct", "[random][seraphis]")`:

- Over `base ∈ {0u, 1u, 7u, 13u, 29u, 101u, 257u, 1009u, 65537u, 0x9E3779B9u, 0xFFFFFFFFu}` and
  `salt ∈ [0, 256)`: `REQUIRE(deriveStreamSeed(base, salt) != 0u)` — **including `base == 0`**,
  which is the case `Xorshift32::seed()` silently substitutes away (`core/random.h:72-74`).
- For each of the 8 pinned seeds: collect all 256 results into a `std::array<std::uint32_t, 256>`,
  sort, and `REQUIRE(std::adjacent_find(...) == end)` — i.e. **all 256 pairwise distinct**. This is
  the `4 × 64 = 256` cross product `EntropyProcessor` uses (plan §4.4).
- `STATIC_REQUIRE(deriveStreamSeed(1u, 0u) != 0u)` — proves it is usable in a constant expression.

Run it; it must **fail to compile** (symbol absent).

**Implement.** EDIT `dsp/include/krate/dsp/core/random.h`:

1. Add `#include <cstddef>` beside the existing `<cstdint>` (line 17). **This is required and new** —
   MSVC and libstdc++ supply `<cstddef>` transitively, so a green Windows build cannot catch its
   absence; `node tools/check-portability.js` can (plan §2.1, risk R7).
2. Add, at namespace scope in `Krate::DSP`, exactly the body verified at `harmonic_cloud.h:653-659`:

```cpp
/// @brief Derive a guaranteed-non-zero per-stream seed from a base seed and a salt.
/// lowbias32 finaliser. The non-zero substitution is load-bearing: Xorshift32::seed()
/// silently replaces 0 with its own default (random.h:72-74), so two lanes hashing to 0
/// would COLLAPSE ONTO ONE STREAM.
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

**Verify:** `dsp_core_tests` green; `node tools/check-portability.js` clean.

---

## GROUP D — `core/pitch_utils.h` (existing Layer 0 header)

### T004 — `centsToPitchRatio` (FR-072, deviation D2)

**Test first.** EDIT `dsp/tests/unit/core/pitch_utils_test.cpp` — add
`TEST_CASE("CentsToPitchRatio_MatchesSemitonesToRatio", "[pitch_utils][seraphis]")`:

- `REQUIRE(centsToPitchRatio(0.0f) == 1.0f)` — **bitwise**, not `Approx`.
- Over `c ∈ {-4800, -1200, -100, -11, -1, 0, 1, 11, 100, 1200, 4800}` (floats):
  `REQUIRE(std::abs(centsToPitchRatio(c) - semitonesToRatio(c / 100.0f))
           <= 1e-6f * std::abs(semitonesToRatio(c / 100.0f)))`
  i.e. **1e-6 relative**. This is what makes D2 a checked property rather than a claim: FR-072
  specifies the body as `semitonesToRatio(cents / 100.0f)` (`pitch_utils.h:23,25`) and we ship
  `std::exp2(cents / 1200)` because it is the same real number and 2–4× cheaper (SC-010 needs it).
- Monotonicity spot check: `centsToPitchRatio(1.0f) > 1.0f` and `centsToPitchRatio(-1.0f) < 1.0f`.

**Implement.** EDIT `dsp/include/krate/dsp/core/pitch_utils.h` — add beside `semitonesToRatio` (`:23`):

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

**Verify:** `dsp_core_tests` green.

---

## GROUP E — `core/db_utils.h` (existing Layer 0 header)

### T005 — One shared `detail::kLn2`, two function-local copies deleted

Three headers in this phase need `ln 2` at compile time (`entropy_processor.h`,
`spectral_morph_engine.h`, `harmonic_cloud.h`), and `harmonic_cloud.h` must not include
`entropy_processor.h`. Repeating the declaration would put two definitions of the same
namespace-scope name into any TU that includes both — which the SC-011 and SC-014 TUs do (plan §2.3).

**Sweep already run this session:** there is **no** namespace-scope `kLn2` in `Krate::DSP`. The only
hits are two **function-local** `constexpr float kLn2 = 0.693147181f;` at `db_utils.h:84`
(inside `detail::constexprLn`) and `db_utils.h:130` (inside `detail::constexprExp`), plus a
class-scoped `TanhADAA::kLn2` (`tanh_adaa.h:171`) which is a different entity and is **untouched**.

**File (EDIT):** `dsp/include/krate/dsp/core/db_utils.h`

1. Inside `namespace detail` (opens `:39`, closes `:179`), immediately beside `kLn10` (`:60`) and
   `kInvLn10` (`:64`) and in the same linkage form, add:
   ```cpp
   /// Natural log of 2. Shared by every constexprExp(x * ln 2) constant.
   constexpr float kLn2 = 0.693147181f;
   ```
2. **Delete** the function-local declarations at `:84` and `:130`; both enclosing functions are
   already inside `namespace detail`, so the unqualified name resolves to the shared constant.
   Deleting rather than shadowing is deliberate — a local of the same name is a
   `-Wshadow`/clang-tidy finding in a zero-warning repo.

The literal is unchanged (`0.693147181f`), so `constexprLn` / `constexprExp` are **bit-identical**
before and after.

**Test (existing suites are the gate — no new case).**

**Verify:**
- `dsp_core_tests` green (this includes `unit/core/db_utils_test.cpp`).
- `dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5` green **with the Phase 2 test files
  unedited** — this is the gate that the consolidation left the two transcendental helpers
  bit-identical.
- Zero warnings.

---

## GROUP F — `systems/harmonic_cloud.h` seed forward

### T006 — Rewrite `HarmonicCloud::deriveSeed` as a one-line forward (FR-006)

**File (EDIT):** `dsp/include/krate/dsp/systems/harmonic_cloud.h`, at `:651-660`.

Replace the body — keeping the **signature, `public static constexpr` linkage and doc comment
unchanged** — with:

```cpp
[[nodiscard]] static constexpr std::uint32_t deriveSeed(std::uint32_t base,
                                                        std::size_t salt) noexcept {
    return deriveStreamSeed(base, salt);   // FR-006: the hash moved to Layer 0 (core/random.h).
}
```

Confirm `harmonic_cloud.h` already includes `<krate/dsp/core/random.h>`; if not, add it.

**Test first.** EDIT `dsp/tests/unit/core/random_test.cpp` is **not** the home for this — it would
force a Layer-0 test to include a Layer-3 header. Instead add to the **engine** TU later (T016,
SC-012 clause 2). For **this** task the gate is the existing Phase 2 suite:

**Verify:**
- `dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5` green with
  `harmonic_cloud_test.cpp` / `harmonic_cloud_spectral_test.cpp` **unedited** — any edit there is a
  failure of SC-014 clause 1.
- Zero warnings.

---

## GROUP G — `processors/spectral_state.h`: struct, validity, normalisation

### T007 — `SpectralState` data model (FR-011, FR-012, FR-014)

**Files**

- CREATE: `dsp/include/krate/dsp/processors/spectral_state.h` (Layer 2)
- EDIT (stub → content): `dsp/tests/unit/processors/spectral_state_test.cpp`

**Test first.** In the TU, add `TEST_CASE("SpectralState_ValidityAndNormalisation", "[spectral_state][seraphis]")`:

- `STATIC_REQUIRE(std::is_trivially_copyable_v<SpectralState>);`
- `STATIC_REQUIRE(SpectralState::kStatePartials == 64);`
- Default-constructed state: `isValidSpectralState(s) == true`, `s.numPartials == 0`,
  every `ratios[i] == 0.0f`, every `amplitudes[i] == 0.0f`, `s.name[0] == '\0'`,
  `s.tiltDbPerOct == 0.0f`, `s.inharmonicity == 0.0f`.
- **Reject grid** — each must return `false`, one defect at a time, starting from a known-valid state:
  `numPartials = -1`; `numPartials = 65`; `ratios[1] == ratios[0]` (non-**strict** increase);
  `ratios[1] < ratios[0]`; `ratios[0] = 0.4f` (< `kMinStateRatio = 0.5f`);
  `ratios[0] = 128.5f` (> `kMaxStateRatio = 128.0f`); `amplitudes[0] = -1e-6f`;
  `amplitudes[0] = 1.0001f`; a NaN and an Inf in `ratios[0]`, `amplitudes[0]`, `tiltDbPerOct`,
  `inharmonicity` (**built from bit patterns via a `volatile` sink**, never
  `std::numeric_limits<float>::quiet_NaN()`); `tiltDbPerOct = 12.5f`; `tiltDbPerOct = -12.5f`;
  `inharmonicity = -1e-6f`; `inharmonicity = 0.1001f`; `name` filled with 16 non-NUL bytes
  (no terminator); `name = {'A', 0x07, '\0', …}` (control byte before the NUL);
  `name = {'A', 0x7F, '\0', …}` (DEL byte before the NUL).
- **Accept:** entries at `i >= numPartials` set to garbage (`ratios[63] = -5.0f`,
  `amplitudes[63] = 99.0f`) with `numPartials = 4` must still return `true` — FR-012 does **not**
  examine them.
- `normalizeSpectralState`: a state with `numPartials = 4`, `amplitudes = {1,1,1,1}` becomes
  `{0.5, 0.5, 0.5, 0.5}` within `1e-6` and `Σ a² == 1` within `1e-5`. An **all-zero** state and a
  `numPartials == 0` state are left **bitwise unchanged** (no NaN — the `sumSquares > 0.0f` guard).

**Implement.** Header contents per plan §3.1/§3.2 (verbatim constants):

```cpp
// Includes — Layer 0 + stdlib ONLY:
//   <krate/dsp/core/db_utils.h>   (detail::isNaN :54 / detail::isInf :174 — FR-007 forbids
//                                  reaching for the Layer 3 use site at harmonic_cloud.h:342-344)
//   <array> <cstddef> <cstdint> <cmath> <cstring> <type_traits>
// <type_traits> is REQUIRED and must be listed explicitly: MSVC/libstdc++ supply it transitively
// through <array>/<bit>/<limits>, so a green Windows build cannot catch its absence (risk R7).

struct SpectralState {
    static constexpr std::size_t kStatePartials   = 64;   // == HarmonicCloud::kMaxPartials (:133)
    static constexpr std::size_t kStateNameBytes  = 16;
    static constexpr float kMinStateRatio         = 0.5f;
    static constexpr float kMaxStateRatio         = 128.0f;
    static constexpr float kMinStateTiltDbPerOct  = -12.0f;
    static constexpr float kMaxStateTiltDbPerOct  = 12.0f;
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

**No member functions** — every operation is a free function, which is what makes Phase 9's deferred
authoring mutators a pure addition.

`isValidSpectralState(const SpectralState&) noexcept` checks **in this order, with early exit**:
`numPartials ∈ [0, 64]`; then for `i < numPartials`: `!isNaN && !isInf`,
`ratios[i] ∈ [kMinStateRatio, kMaxStateRatio]`, `ratios[i] > ratios[i-1]` (**strict**),
`amplitudes[i]` finite in `[0, 1]`; then `name` contains at least one `'\0'` and no byte before it is
`< 0x20` or `== 0x7F`; then `tiltDbPerOct` finite in `[-12, +12]`; then `inharmonicity` finite in
`[0, 0.1]`. Entries at `i >= numPartials` are **not examined**.

`normalizeSpectralState(SpectralState&) noexcept` is the `harmonic_snapshot.h:99-107` idiom over
`[0, numPartials)`: accumulate `sumSquares`, and **only** `if (sumSquares > 0.0f)` multiply every
entry by `1.0f / std::sqrt(sumSquares)`.

**Do not add a namespace-scope partial-count constant** — `Krate::DSP::kMaxPartials = 96` already
exists at `harmonic_types.h:21`, which is why `kStatePartials` is class-scoped (C-8).

**Verify:** `dsp_processors_tests.exe "SpectralState_*"` green; **`node tools/check-portability.js`
run on the new header HERE, not deferred to T025** (risk R7).

---

## GROUP H — `processors/spectral_state.h`: factory states

### T008 — `makeFactoryState` (FR-021 – FR-023, SC-008 clauses 1/2/4)

**Files:** EDIT `dsp/include/krate/dsp/processors/spectral_state.h`;
EDIT `dsp/tests/unit/processors/spectral_state_test.cpp`.

**Test first.** Two cases.

`TEST_CASE("SpectralState_FactoryStatesAreDistinct", "[spectral_state][seraphis]")` — SC-008 cl.1/2:

- All five ids: `isValidSpectralState` true; L2 norm of `amplitudes[0..numPartials)` equals 1 within
  `1e-5`; `name` non-empty and NUL-terminated.
- **Deviation D6 — the max-ratio clause is SCOPED:**
  `max_{i < numPartials} ratios[i] <= kMaxStateRatio` (128). Separately, over **all 64 slots**:
  strictly increasing, and `<= SpectralMorphEngine::kMaxOutputRatio` (360.37) — assert this as a
  literal `360.37f` here (the engine header does not exist yet) with a comment naming plan §5.1.
  Pin the computed maxima: SineStack **64.00**, Bell **240.32**, Choir **64.00**, Glass **80.38**,
  Breath **64.00**, each within 0.5 % relative.
- Pin the minimum adjacent spacing per state in cents
  (`1200·log2(ratios[i]/ratios[i-1])`, min over `i ∈ [1,64)`): **27.264 / 28.000 / 27.264 / 32.79 /
  27.264** within 0.05 cents. (Deviation D7: `1200·log2(64/63) = 27.264`, **not** 27.32.)
- **All 10 pairs**, distance `d(A,B)` with `λ = 1.0`, amplitude term over all 64 slots and ratio term
  over `[0, min(A.numPartials, B.numPartials))`: `REQUIRE(d > 0.4f)`. Assert the table computed in
  plan §3.4 within 2 % relative:
  | Pair | d |
  |---|---|
  | SineStack/Bell | 1.5723 |
  | SineStack/Choir | 0.5258 |
  | SineStack/Glass | 0.7406 |
  | SineStack/Breath | 1.0932 |
  | Bell/Choir | 2.0245 |
  | Bell/Glass | 1.9742 |
  | Bell/Breath | 2.5841 |
  | Choir/Glass | 0.8824 |
  | Choir/Breath | 1.0538 |
  | Glass/Breath | 0.9517 |
- Regression pin: `kMeasuredClosestPairDistance = 0.5258f` on the **SineStack/Choir** pair, **±10 %**.
- Cosine-similarity clause for the three `ratio_n = n` states (FR-022 requires `ρ ≤ 0.92`):
  SineStack/Choir `ρ = 0.8618`, SineStack/Breath `ρ = 0.4025`, Choir/Breath `ρ = 0.4447` — each
  within 2 % and each `<= 0.92`.

`TEST_CASE("SpectralState_FactoryConsumesNoRng", "[spectral_state][seraphis]")` — SC-008 cl.4:

- Create a shared `Xorshift32 rng{1u}`. For each id: record `rng.state()` (`core/random.h:78`), call
  `makeFactoryState(id)`, `REQUIRE(rng.state() == before)`.
- Draw `10^6` values from `rng`, call `makeFactoryState(id)` again, and `REQUIRE` the two states are
  **bitwise identical** (memcmp of the whole struct) for all five ids.

**Implement.** Add to the header, per plan §3.4:

```cpp
enum class SpectralStateId : std::uint8_t { SineStack = 0, Bell, Choir, Glass, Breath };
inline constexpr std::size_t kSpectralStateCount = 5;
[[nodiscard]] SpectralState makeFactoryState(SpectralStateId id) noexcept;
```

Deterministic, **consumes no RNG** (FR-023) — every value is a closed-form function of the partial
index. Build order per state: (1) `ratios[i]` for `i < numPartials` from the ratio law; (2) the
**FR-041 geometric continuation** into `ratios[i]` for `i >= numPartials` (the recurrence of plan
§5.4, reproduced below); (3) `amplitudes[i]` from the amplitude law for `i < numPartials`, `0.0f`
above; (4) `normalizeSpectralState`; (5) NUL-padded ASCII label.

Laws (1-based `n = i + 1`):

| Id | `numPartials` | `ratio_n` | `amp_n` (pre-normalisation) | name |
|---|---|---|---|---|
| `SineStack` | 64 | `n` | `n^(-1)` | `"SineStack"` |
| `Bell` | 24 | `n · sqrt(1 + kBellB·n²)`, `kBellB = 0.04f` | `n^(-1.4)` | `"Bell"` |
| `Choir` | 64 | `n` | `n^(-0.8) · (kChoirFloor + Σ_{k=0..2} g_k·exp(−(n − c_k)²/(2σ_k²)))` | `"Choir"` |
| `Glass` | 64 | `n · (1 + kGlassStretch·n)`, `kGlassStretch = 0.004f` | `n^(-0.5) · (n even ? kGlassEvenAtten : 1)` | `"Glass"` |
| `Breath` | 64 | `n` | `n^(-0.25) · (1 − kBreathLowDepth · exp(−(n − 1)/kBreathLowScale))` | `"Breath"` |

Constants (pinned — do **not** re-derive):

```
kChoirFloor      = 0.12f
kChoirCentres[3] = { 3.0f, 8.0f, 17.0f }
kChoirSigmas[3]  = { 1.2f, 2.0f, 3.0f }
kChoirGains[3]   = { 1.0f, 0.8f, 0.5f }
kGlassEvenAtten  = 0.35f
kBreathLowDepth  = 0.9f
kBreathLowScale  = 3.0f
kBellB           = 0.04f
kGlassStretch    = 0.004f
```

Fill recurrence for `j` from `max(numPartials, 0)` to 63 over the linear ratio array `r`
(identical to the engine's, plan §5.4 — `kMaxFillGrowth = 2.0f`, `kMaxFillRatio = 128.0f`,
`kFillSpacingFactor = 1.0163049f` = `exp2(28/1200)`):

```cpp
float grown;
if (numPartials >= 2) {
    const float g = std::clamp(r[j-1] / r[j-2], 1.0f, kMaxFillGrowth);
    grown = std::min(r[j-1] * g, kMaxFillRatio);
} else {
    grown = static_cast<float>(j + 1);      // D9: the j+1 arm applies for EVERY j when numPartials < 2
}
const float floorV = (j >= 1) ? r[j-1] * kFillSpacingFactor : static_cast<float>(j + 1);
r[j] = std::max(grown, floorV);
```

Bell's authored region tops out at `ratio_24 = 24·sqrt(1 + 0.04·576) = 117.67 <= 128` ✔; its **fill**
reaches 240.32 at slot 63, inside `kMaxOutputRatio = 360.37` — which is exactly why D6 scopes the
`<= kMaxStateRatio` clause to `i < numPartials`.

**Verify:** `dsp_processors_tests.exe "SpectralState_*"` green. If any distance in the table misses
by more than 2 %, the **constants** are wrong, not the thresholds — recompute and report; do not
widen the band.

---

## GROUP I — `processors/spectral_state.h`: serialization

### T009 — Serialization round-trip (FR-031 – FR-033, SC-007)

**Files:** EDIT `dsp/include/krate/dsp/processors/spectral_state.h`;
EDIT `dsp/tests/unit/processors/spectral_state_test.cpp`.

**Test first.** `TEST_CASE("SpectralState_SerializationRoundTrips", "[spectral_state][seraphis]")`:

- `STATIC_REQUIRE(kSpectralStateBytes == 541);`
- Corpus: the **5 factory states** + **≥ 3 edge states**: `numPartials = 0` (default);
  `numPartials = 1, ratios[0] = kMinStateRatio, amplitudes[0] = 1.0f`;
  `numPartials = 64` with `tiltDbPerOct = -12.0f`, `inharmonicity = 0.1f` and a full 15-char name.
- For each: `serializeSpectralState(s, buf, 541)` returns **exactly 541**;
  `deserializeSpectralState(buf, 541, out)` returns `true`; every field reproduces **bitwise**
  (`std::memcmp` on the whole struct == 0).
- **Byte-stream FNV-1a digest** per factory state, checked in as a golden.
  **The test must carry a comment labelling this as a digest over STORED VALUES (a serialized byte
  stream), explicitly NOT a render golden** — `dsp/CLAUDE.md` permits exactly this and forbids the
  other.
- Negatives, each asserting the destination/`out` is untouched:
  - `capacity = 540` → returns `0`, and every byte of a pre-poisoned `buf` is unchanged.
  - `dest == nullptr` → returns `0`.
  - an invalid state (non-monotone ratios) → returns `0`.
  - `src[0] = 2` (bad version) → `deserialize` returns `false`, `out` **bitwise** unchanged.
  - `size = 540` → `false`.
  - `src == nullptr` → `false`.
  - a well-formed 541-byte stream whose decoded payload violates FR-012 (write a non-monotone ratio
    pair directly into the buffer at offsets 13/17) → `false`, `out` **bitwise** unchanged.

**Implement.** Add to the header:

```cpp
inline constexpr std::uint8_t kSpectralStateFormatVersion = 1;
inline constexpr std::size_t  kSpectralStateBytes = 1 + 4 + 4 + 4 + 256 + 256 + 16;  // = 541
static_assert(kSpectralStateBytes == 541);

[[nodiscard]] std::size_t serializeSpectralState(const SpectralState& s, std::byte* dest,
                                                 std::size_t capacity) noexcept;
[[nodiscard]] bool deserializeSpectralState(const std::byte* src, std::size_t size,
                                            SpectralState& out) noexcept;
```

Layout — **little-endian, fixed order, 541 bytes**:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `kSpectralStateFormatVersion` |
| 1 | 4 | `numPartials` (`std::int32_t`) |
| 5 | 4 | `tiltDbPerOct` (float bits) |
| 9 | 4 | `inharmonicity` (float bits) |
| 13 | 256 | `ratios[0..63]` |
| 269 | 256 | `amplitudes[0..63]` |
| 525 | 16 | `name[0..15]` verbatim |

Every scalar moves with `std::memcpy` of its bit pattern — **no `reinterpret_cast`, no text
formatting, and no arithmetic on `std::byte`** (implementation-defined signedness hazards; cast
through `unsigned char` where a byte value is needed). Both functions `noexcept`, allocation-free,
RT-safe.

`serializeSpectralState` returns **0** and writes **nothing** if `dest == nullptr`,
`capacity < kSpectralStateBytes`, or `!isValidSpectralState(s)`.
`deserializeSpectralState` decodes into a **local** `SpectralState` and copies into `out` **only on
success**; it returns `false` if `src == nullptr`, `size < kSpectralStateBytes`,
`src[0] != kSpectralStateFormatVersion`, or the decoded payload fails `isValidSpectralState`.

Round-trip is exact because the bytes are stored values, not arithmetic results (FR-033).

**Verify:** `dsp_processors_tests.exe "SpectralState_*"` green; zero warnings.

---

## GROUP J — `processors/entropy_processor.h`: constants and skeleton

### T010 — Constants, `static_assert`s, prepare/reset/setEntropy, introspection surface

**Files**

- CREATE: `dsp/include/krate/dsp/processors/entropy_processor.h` (Layer 2)
- EDIT (stub → content): `dsp/tests/unit/processors/entropy_processor_test.cpp`

**ODR note:** `EntropyProcessor::kWalkLimit` and `kDenormalFloor` are **class-scoped
transcriptions**, not reuses — `BrownianDrift`'s own `kWalkLimit = 4.0f` (`brownian_drift.h:226`) and
`kDenormalFloor = 1e-20f` (`:228`) sit **below its `private:` at `:221`** and cannot be named. The
identical precedent is `HarmonicCloud::kDriftWalkLimit` / `kDriftDenormalFloor`
(`harmonic_cloud.h:156-157`). **`brownian_drift.h` is NOT modified** — promoting those to `public:`
would be a fourth amendment to a COMPLETE Phase 1 component for no benefit.

**Test first.** `TEST_CASE("EntropyProcessor_ConstantsMatchTranscendentals", "[entropy][seraphis]")`:

- `REQUIRE(std::abs(EntropyProcessor::kMinRatioSpacingFactor - std::exp2(24.0f/1200.0f))
          <= 1e-6f * std::exp2(24.0f/1200.0f))` — expected **1.0139595**.
- `STATIC_REQUIRE(2.0f * (EntropyProcessor::kMaxDecoherenceCents + EntropyProcessor::kMaxScatterCents)
                  < EntropyProcessor::kMinRatioSpacingCents);` — FR-074, i.e. `22.0 < 24.0`.
- `onePoleChunkStep(750.0f, 64.0f, 48000.0f)` within `1e-6` relative of **8.84950e-3**;
  `onePoleChunkStep(150.0f, 64.0f, 48000.0f)` within `1e-6` relative of **4.34712e-2**.
- `kEntropyCentsSmoothMs == BrownianDrift::kDriftOutputSmoothMs` (150) and
  `kEntropyAmpSmoothMs == 5.0f * BrownianDrift::kDriftOutputSmoothMs` (750), bitwise.

`TEST_CASE("EntropyProcessor_StageWeightsAreContinuous", "[entropy][seraphis]")`:

- For `e` on a 0.001 grid over `[0,1]`: each `getStageWeight(k)` for `k ∈ [1,4]` is in `[0,1]`,
  **monotone non-decreasing** in `e`, and `|w_k(e) − w_k(e − 0.001)| <= 0.005`.
- `w_k(0) == 0.0f` **bitwise** for all four — including `w_1`, whose interval starts at 0.
- `w_1(0.35) == 1.0f`, `w_2(0.25) == 0.0f`, `w_2(0.60) == 1.0f`, `w_3(0.50) == 0.0f`,
  `w_3(0.85) == 1.0f`, `w_4(0.75) == 0.0f`, `w_4(1.0) == 1.0f`, each bitwise.
- `getStageWeight(0)` and `getStageWeight(5)` return `0.0f` (out-of-range).
- `setEntropy` rejects NaN/Inf (bit-pattern inputs): `getEntropy()` **bitwise** unchanged; clamps
  `-1.0f → 0.0f` and `2.0f → 1.0f`.
- Out-of-range index on every getter returns `0.0f` / `LifePhase::Alive` (never reads past the array).

**Implement.** Header per plan §4.1/§4.2/§4.3/§4.8.

Includes — `<krate/dsp/core/db_utils.h>` (`detail::isNaN`, `detail::isInf`, `detail::constexprExp`,
`detail::kLn2`), `<krate/dsp/core/pitch_utils.h>`, `<krate/dsp/core/random.h>`,
`<krate/dsp/primitives/smoother.h>` (`calculateOnePolCoefficient` `:91`, `kCompletionThreshold` `:55`,
`detail::flushDenormal` `:250`), **`<krate/dsp/processors/brownian_drift.h>`** (OU coefficient
constants only — intra-Layer-2, legal, **and it must be listed explicitly**: MSVC would hide the
omission transitively), `<krate/dsp/processors/spectral_state.h>`, plus
`<algorithm> <array> <cmath> <cstddef> <cstdint>`.

**`EntropyProcessor` must NOT derive from `ModulationSource`** (which `brownian_drift.h` pulls in) —
it is a spectral transform with an array-in/array-out contract, not a scalar modulation source.

Constants — all class-scoped, verbatim from plan §4.1, plus `kDefaultEntropySeed = 1u`
(matching `Xorshift32`'s own default at `core/random.h:44`; the plan references this name in §4.3
without defining it):

```cpp
static constexpr std::size_t kPartials = SpectralState::kStatePartials;   // 64
static constexpr float kStage1Lo = 0.00f, kStage1Hi = 0.35f;   // amplitude jitter
static constexpr float kStage2Lo = 0.25f, kStage2Hi = 0.60f;   // phase decoherence
static constexpr float kStage3Lo = 0.50f, kStage3Hi = 0.85f;   // ratio scatter
static constexpr float kStage4Lo = 0.75f, kStage4Hi = 1.00f;   // death / rebirth
static constexpr float kMaxAmpJitter        = 0.5f;
static constexpr float kMaxDecoherenceCents = 4.0f;
static constexpr float kMaxScatterCents     = 7.0f;
static constexpr float kMinRatioSpacingCents = 24.0f;
static constexpr float kMinRatioSpacingLog2  = kMinRatioSpacingCents / 1200.0f;
static constexpr float kMinRatioSpacingFactor =
    detail::constexprExp(kMinRatioSpacingLog2 * detail::kLn2);   // 1.0139595
static_assert(2.0f * (kMaxDecoherenceCents + kMaxScatterCents) < kMinRatioSpacingCents,
              "FR-074: two neighbours must not be able to close the FR-046 spacing floor. "
              "Any increase to the cent constants must be paid for by raising "
              "kMinRatioSpacingCents — never by deleting this assert.");
static constexpr float kWalkLimit     = 4.0f;    // transcribed, brownian_drift.h:226 (private)
static constexpr float kDenormalFloor = 1e-20f;  // transcribed, brownian_drift.h:228 (private)
static constexpr float kAmpJitterSmoothness   = 0.09396f;   // tau = 3.0 s
static constexpr float kDecoherenceSmoothness = 0.26174f;   // tau = 8.0 s
static constexpr float kEntropyCentsSmoothMs = BrownianDrift::kDriftOutputSmoothMs;        // 150
static constexpr float kEntropyAmpSmoothMs   = 5.0f * BrownianDrift::kDriftOutputSmoothMs; // 750
static constexpr std::uint32_t kDefaultEntropySeed = 1u;

[[nodiscard]] static constexpr float onePoleChunkStep(float smoothTimeMs, float chunkSamples,
                                                      float sampleRate) noexcept {
    return 1.0f - detail::constexprExp(-5000.0f * chunkSamples / (smoothTimeMs * sampleRate));
}

static constexpr float kMaxDeathRatePerSecond = 0.05f;
static constexpr float kMinDeathFadeSec   = 0.5f, kMaxDeathFadeSec   = 2.0f;
static constexpr float kMinDeadDwellSec   = 0.2f, kMaxDeadDwellSec   = 1.0f;
static constexpr float kMinRebirthFadeSec = 0.5f, kMaxRebirthFadeSec = 2.0f;
enum class LifePhase : std::uint8_t { Alive = 0, Dying, Dead, Reborn };
```

**Why `kEntropyAmpSmoothMs = 750` and not 150 (deviation D12 — do not "simplify" it back):**
`OnePoleSmoother`'s parameter is **time-to-99 %**, `coeff = exp(-5000/(ms·fs))`
(`primitives/smoother.h:86-91`), i.e. `tau = ms/5000` s. A 150 ms setting is a **30 ms** time
constant. FR-044's published amplitude row `8.85e-3` is derived with `tau = 0.150 s`, which in this
convention is **750 ms**. Feeding 150 ms makes the per-chunk step `4.347e-2`, the jitter term
`4.347e-2` and the FR-044 amplitude sum `5.680e-2` against `kMaxAmpDeltaPerChunk = 0.025` — a **hard
compile failure** of the engine's first `static_assert` in T014. The decoherence bank keeps 150 ms so
one bank stays bit-comparable to a stock `BrownianDrift` (T011's equivalence test).

Public API (plan §4.2) — declare the whole surface now, including every FR-008 accessor
(`getEntropy`, `getStageWeight`, `getAmpJitterFactor`, `getDecoherenceCents`,
`getAppliedScatterCents`, `getRawScatterDraw`, `getScatterRedrawCount`, `getLifePhase`,
`getLifeAmplitudeFactor`, `stateFinite`). The applied-vs-raw scatter split is load-bearing:
SC-005 reads `getAppliedScatterCents`, SC-006 reads `getRawScatterDraw` + `getScatterRedrawCount`.

State layout per plan §4.3 (two `OuBank`s + `scatterDraw_`, `scatterRedraws_`, `lifeRng_`, `phase_`,
`life_`, `phaseTimer_`, `phaseLength_`, `entropy_`, `w1_..w4_`, `sampleRate_`, `invSampleRate_`,
`configuredSeed_`). `struct LaneRng { Xorshift32 rng{1}; };` is required because `Xorshift32`'s only
constructor is `explicit` (`core/random.h:44`), so `std::array<Xorshift32, N>{}` is ill-formed — the
same workaround as `harmonic_cloud.h:925-927`. Fixed-size, ~5 KB, **no allocation anywhere**.

`prepare(double)`: floor at 1 Hz (`sampleRate > 1.0 ? sampleRate : 1.0`, matching
`brownian_drift.h:122`); recompute `invSampleRate_` and per-bank coefficients (T011); then `reset()`.
`reset()`: re-seed all 4×64 lanes, zero the walks/smoothers/timers, `phase_ = Alive`, `life_ = 1.0f`.
**Parameters (`entropy_`, `w1_..w4_`, `configuredSeed_`) are NOT touched** — matching
`BrownianDrift::reset()` (`:133-135`).
Both are documented in the header as **configuration-time calls, not to be made while the consumer is
sounding** (FR-005, FR-006).

Stage weights: `w_k(e) = clamp((e − lo_k) / (hi_k − lo_k), 0, 1)`, recomputed in `setEntropy` only.

**Verify:** `dsp_processors_tests.exe "EntropyProcessor_*"` green; zero warnings;
`node tools/check-portability.js` clean.

---

## GROUP K — `processors/entropy_processor.h`: the two OU banks

### T011 — Lane-batched OU banks, exact discretisation (FR-072)

**Files:** EDIT `dsp/include/krate/dsp/processors/entropy_processor.h`;
EDIT `dsp/tests/unit/processors/entropy_processor_test.cpp`.

**Test first.** `TEST_CASE("EntropyProcessor_OuBankMatchesBrownianDrift", "[entropy][seraphis]")`:

- **Arm 1 (statistical equivalence).** One lane of the **decoherence** bank against a reference
  `BrownianDrift` seeded identically, configured with
  `setSmoothness(EntropyProcessor::kDecoherenceSmoothness)` — **pass the class constant itself, never
  a re-typed `0.26174f`**, so both sides traverse the identical
  `tau = kTauMin + s·(kTauMax − kTauMin)` mapping — and `setDepth(1.0f)`. Drive **≥ 60 s at 48 kHz**
  (90,000 control steps at `kControlRateInterval = 32`) and require agreement within **1e-5** at
  every sampled point. Add a comment: *the 1e-5 tolerance is only achievable because the coefficients
  are computed with `double` intermediates (plan §4.4, `harmonic_cloud.h:1519-1531` and its comment
  at `:1510-1515`); a `float` derivation makes this row a coin flip across MSVC/GCC/Clang.*
  Only the decoherence bank is compared — the amplitude bank smooths at 750 ms (D12) and would differ
  by construction; the lane-batching code under test is shared, so one bank exercises it fully.
- **Arm 2 (non-statistical, BOTH banks).** Recompute in `double` inside the test, at 48 kHz:
  `controlDt = 32.0 / 48000.0`, `tau = kTauMin + smoothness·(kTauMax − kTauMin)`,
  `a = exp(-controlDt/tau)`, `g = kInternalStd·sqrt(1 − a²)`; require the bank's stored `a` and `g`
  match within **1e-6 relative** for `kAmpJitterSmoothness` (expected `tau ≈ 3.0 s`) and
  `kDecoherenceSmoothness` (expected `tau ≈ 8.0 s`). Assert `tau` itself is within `1e-3` of 3.0 and
  8.0 respectively.
- **Arm 3 (seed disjointness).** For each pinned seed, assert the 256 lane seeds
  (`deriveStreamSeed(base, salt)` for the four salt ranges below) are pairwise distinct and non-zero.

**Implement.** Per plan §4.4 — copy `HarmonicCloud::DriftLanes` (`harmonic_cloud.h:929-952`), **not**
128 `BrownianDrift` objects.

Coefficients — **every intermediate in `double`; only the final `a`/`g` narrowed to float**, a literal
transcription of `BrownianDrift::updateCoefficients` (`brownian_drift.h:230-240`) via its Phase 2 copy
`HarmonicCloud::updateDriftCoefficients` (`harmonic_cloud.h:1519-1531`), **including the
`variance > 0.0` guard**:

```cpp
const double controlDt = static_cast<double>(BrownianDrift::kControlRateInterval) / sampleRate_;
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

**`tau` is derived through the smoothness mapping, never written as `3.0` / `8.0`** — writing `8.0f`
gives 7.99999 through the mapping and Arm 1 diverges in the last bits.

Control step, transcribing `brownian_drift.h:253-270`:

```
z0,z1,z2 = three SEQUENCED nextFloat() draws   // operands of + are unsequenced in C++;
z        = z0 + z1 + z2                        // a different draw order is a different stream
x        = a * walk[i] + g * z                 // mean is 0 for both banks
x        = clamp(x, -kWalkLimit, +kWalkLimit)
if (|x| < kDenormalFloor) x = 0
walk[i]      = x
smoothTgt[i] = clamp(1.0f * x, -1, +1)         // depth pinned at 1.0 (FR-072 table)
```

Output smoother advance is the **literal transcription** of `OnePoleSmoother::advanceSamples` used at
`harmonic_cloud.h:1620-1641` — including all three parts the naive exponential identity omits: the
`isComplete` early-`continue` (`:1632-1633`), `detail::flushDenormal` (`:1636`), and the post-advance
snap below `kCompletionThreshold` (`:1637-1639`). `coeff^N` uses the same
`std::pow(coefficient, static_cast<float>(numSamples))` expression (`smoother.h:248`) and is memoised
on `numSamples` (`cachedPowN`/`cachedPowValue`) — **never** from a precomputed `coeff^k` table
(strength-reduction reason at `harmonic_cloud.h:1582-1594`). The coefficient is **per bank**.

Bank advance follows `advanceDriftLanes` (`harmonic_cloud.h:1655-1667`): one **shared**
`samplesUntilControl` counter per bank, so lane state after N advanced samples is a function of **N
alone**, never of how N was partitioned into chunks.

Per-lane seeds off the Layer 0 `deriveStreamSeed` (FR-006), four disjoint salt ranges over one base:

| Stream | Salt |
|---|---|
| bank (a), amplitude jitter | `i` |
| bank (b), decoherence | `kPartials + i` |
| static scatter `s_i` | `2·kPartials + i` |
| death/rebirth lifecycle | `3·kPartials + i` |

**Verify:** `dsp_processors_tests.exe "EntropyProcessor_*"` green; zero warnings.

---

## GROUP L — `processors/entropy_processor.h`: `processChunk` and stages 1–3

### T012 — Stage application, fixed order (FR-071, FR-072, FR-075; SC-005, SC-016)

**Files:** EDIT `dsp/include/krate/dsp/processors/entropy_processor.h`;
EDIT `dsp/tests/unit/processors/entropy_processor_test.cpp`.

**Test first.** Two cases, both over the 8 pinned seeds.

`TEST_CASE("EntropyProcessor_StagesEngageInOrder", "[entropy][seraphis]")` — SC-005. Feed a clean
input array (`ratios[i] = i + 1`, `amplitudes[i] = 1/(i+1)`) each chunk, run 64-sample chunks at
48 kHz, and at each setting assert:

- `e = 0.10`: every ratio deviation **bitwise 0** (`out[i] == in[i]`); every `getLifePhase(i) ==
  LifePhase::Alive` with `getLifeAmplitudeFactor(i)` **exactly `1.0f`**; at least one
  `getAmpJitterFactor(i) != 1.0f`.
- `e = 0.40`: ratio deviation non-zero for ≥ 1 partial; **`getAppliedScatterCents(i)` exactly `0.0f`
  for every `i`** (stage 3 opens at 0.50); no deaths (`getScatterRedrawCount(i) == 0` for all `i`).
- `e = 0.65`: `getAppliedScatterCents(i)` non-zero for ≥ 1 `i`; still no deaths.
- (The `e = 0.90` death arm belongs to T013.)

`TEST_CASE("EntropyProcessor_PhaseDecoheres", "[entropy][seraphis]")` — SC-016, **two arms**:

- **Arm A, `entropy = 0.45`** (not 0.50), ≥ 120 s, 8 seeds. First assert
  `getStageWeight(3) == 0.0f` **bitwise** — so a move of the FR-071 boundaries fails loudly.
  (a) Each partial's mean ratio deviation is within `kMeanRatioDriftCents = 2.0` cents. Derivation to
  put in a comment: `σ ≈ 1.143` cents, run-mean SE `= σ·sqrt(2τ/T) = 0.417` cents, so 2.0 is 4.8
  SE. (b) Variance **across the 64 partials** of accumulated phase error at ≥ 40 evenly spaced
  times; linear-fit slope positive and exceeding its own standard error by **≥ 5×**, on **each**
  seed. (c) The count of partials whose run-mean deviation exceeds 2.0 cents must be **0**
  (`P ≈ 1.6e-6` per partial, 8e-4 expected over all 512).
- **Arm B, `entropy = 0.74`** (deviation **D17** — *not* the spec's 0.85). Assert
  `getStageWeight(4) == 0.0f` **bitwise** and `getStageWeight(3) ≈ 0.6857` within `1e-4`. Count
  partials whose run-mean deviation exceeds 2.0 cents. Gates, **both** asserted with the derivation
  in a comment beside them: **per seed ≥ 24 of 64** and **pooled ≥ 256 of 512** across the 8 seeds.
  Derivation: at `w_4 = 0` the scatter offset is static, `w_3 = 0.6857` ⇒ offset `~U[−4.80, +4.80]`,
  `w_2 = 1.0` ⇒ a zero-mean OU term of `σ = 2.0` cents whose run-mean SE over 120 s is 0.73 cents;
  `P(|offset + noise| > 2.0) = 1 − 4.0/9.6 = 0.5833`, mean **37.33/64**, `sd = 3.944`; 24 is 3.38 sd
  below the mean (`P(fail) ≈ 3.6e-4` per seed). **Why not 0.85:** there `w_4 = 0.40` ⇒ 0.02 deaths/s
  ⇒ ≈ 2.4 deaths per partial over 120 s, and FR-073 **redraws `s_i` on every death**, so `σ` falls
  4.04 → 2.33 cents and the expected count drops to ≈ 25 — the spec's `≥ 32` fails on a **faithful**
  implementation.

**Implement.** Per plan §4.6.

```cpp
void processChunk(float* r, float* a, std::size_t count, std::size_t numSamples) noexcept {
    // ADVANCE FIRST, AND UNCONDITIONALLY IN count. The lane/lifecycle clocks must be a
    // function of elapsed samples ALONE — SC-012 and SC-013 both rest on that, and
    // count == 0 is a REACHABLE configuration (the engine passes
    // max(A.numPartials, B.numPartials), and FR-012 permits numPartials == 0).
    if (numSamples > 0) {
        advanceBank(jitter_,   numSamples);
        advanceBank(decohere_, numSamples);
        advanceLifecycles(static_cast<float>(numSamples) * invSampleRate_);
    }
    if (r == nullptr || a == nullptr || count == 0) return;   // gate the STAGE APPLICATION only
    applyStages(r, a, std::min(count, kPartials));
}
```

`numSamples == 0` applies the stages **without advancing anything** — which is what
`SpectralMorphEngine::prepare()`/`reset()` use to populate their output arrays with no advance
(FR-005, SC-002 clause 5). Document that contract in the header.

`applyStages` per partial `i < count`, **in this fixed order**:

```
d_i = clamp(jitter_.smoothCur[i],   -1, +1)
c_i = clamp(decohere_.smoothCur[i], -1, +1)

(a) a[i] *= (1.0f + w1_ * kMaxAmpJitter * d_i)     // strictly positive: 0.5 < 1
(4) a[i] *= life_[i]                                // L_i, FR-073 (wired in T013)
(b,c) cents_i = w2_ * kMaxDecoherenceCents * c_i
              + w3_ * kMaxScatterCents     * scatterDraw_[i]
      r[i] *= centsToPitchRatio(cents_i)
```

**Deviation D3:** FR-072 writes (b) and (c) as two successive `centsToPitchRatio` multiplications;
summing and converting once is the same real number (`f(x)·f(y) = f(x+y)`), differs only in float
rounding (~1.7e-4 cents, four orders below `kTargetRatioEpsilonCents = 0.05`), preserves FR-074's
±11.0-cent bound exactly, and halves the transcendental count in the hottest loop of the phase.
`getDecoherenceCents(i)` returns `w2_·kMaxDecoherenceCents·c_i` and `getAppliedScatterCents(i)`
returns `w3_·kMaxScatterCents·scatterDraw_[i]`, so the two remain separately readable.

**Exact-zero fast path (explicit branch — `-ffast-math`):** `entropy_ == 0.0f` skips `applyStages`
entirely.

**Verify:** `dsp_processors_tests.exe "EntropyProcessor_*"` green.

---

## GROUP M — `processors/entropy_processor.h`: death/rebirth lifecycle

### T013 — Stage 4 FSM and boundedness (FR-073; SC-005 death arm, SC-006)

**Files:** EDIT `dsp/include/krate/dsp/processors/entropy_processor.h`;
EDIT `dsp/tests/unit/processors/entropy_processor_test.cpp`.

**Test first.**

Extend `EntropyProcessor_StagesEngageInOrder` with the `e = 0.90` arm, ≥ 60 s, 8 seeds:
≥ 1 partial completed a full `Dying → Dead → Reborn` cycle (observed via `getLifePhase`), and its
**`getRawScatterDraw(i)`** differs before vs after the cycle. Derivation comment:
`P(none of 64 dies) ≈ 1e-46`.

`TEST_CASE("EntropyProcessor_BoundedAtEverySetting", "[entropy][seraphis]")` — SC-006.
**11 settings × 8 seeds × ≥ 60 s** of 64-sample chunks at 48 kHz, with a clean input array re-fed
every chunk. Per chunk, per partial:

- amplitude **finite** (bit pattern via `detail::isNaN`/`detail::isInf`, never `std::isnan`),
  `>= 0.0f`, and `<= 1.5f ×` its input;
- ratio finite, `> 0.0f`, and within **±11.0 cents** of its input
  (`|1200·log2(out/in)| <= 11.0f`);
- the output ratio array **strictly increasing every chunk**;
- the FR-044 amplitude delta bound holds for every partial **and for `L_i` itself**;
- the ratio (cents) delta bound is asserted **only where `L_i > 0`**;
- **complementary clause:** every redraw observed through `getScatterRedrawCount(i)` occurred on a
  chunk where `getLifeAmplitudeFactor(i)` was **bitwise `0.0f`**.

**Implement.** Per plan §4.7, advanced once per chunk by `dt = numSamples / sampleRate`:

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

**The RNG draw order is fixed and must be documented in the header** — `reset()` must reproduce it
exactly. The redraw happens on the chunk that enters `Dead`, **after** `life_[i]` is set to exactly
`0.0f`, so SC-006's bitwise clause holds by construction.

**Deviation D5 (do not "fix" this to the literal spec wording):** `w_4 == 0` **starts no new deaths**
and forces `L_i = 1.0f` for partials in `Alive`; a lifecycle already in flight **runs to completion**
(bounded by 5.0 s). A literal force-to-`Alive` would make `setEntropy(0)` during a `Dead` window a
step of `L_i` from 0 to 1 in one chunk — **40× `kMaxAmpDeltaPerChunk`** — and SC-001 clause 1
exercises `setEntropy` mid-sweep.

`reset()` must additionally: draw all 64 `scatterDraw_` values **in index order**, zero
`scatterRedraws_`, set every `phase_ = Alive`, `life_ = 1.0f`, timers zeroed.
`setSeed(s)` stores `s` and performs the same re-seed + redraw as `reset()`'s once-per-seed half.

**Verify:** `dsp_processors_tests.exe 2>&1 | tail -5` — whole processors suite green; zero warnings.

---

## GROUP N — `systems/spectral_morph_engine.h`: skeleton, slots, fill

### T014 — Constants, FR-044 `static_assert`s, slot storage, FR-041 fill, prepare/reset/defaults

**Files**

- CREATE: `dsp/include/krate/dsp/systems/spectral_morph_engine.h` (Layer 3)
- EDIT (stub → content): `dsp/tests/unit/systems/spectral_morph_engine_test.cpp`

**Includes** — `<krate/dsp/core/db_utils.h>` (`detail::constexprExp`/`constexprLn`, `detail::kLn2`),
`<krate/dsp/core/random.h>`, `<krate/dsp/processors/spectral_state.h>`,
`<krate/dsp/processors/entropy_processor.h>`, **`<krate/dsp/processors/brownian_drift.h>`**
(`kDriftOutputSmoothMs`, needed at compile time by the FR-044 `static_assert`s — Layer 3 → 2, legal,
and **must be listed explicitly** or MSVC hides the omission), `<krate/dsp/processors/spline_trajectory.h>`,
plus `<algorithm> <array> <cmath> <cstddef> <cstdint>`.
**It must NOT include `harmonic_cloud.h`** (FR-003) — a lint-visible property.

**Test first.**

`TEST_CASE("SpectralMorph_DefaultsAreAudible", "[spectral_morph][seraphis]")` — SC-002 clause 5.
Default-construct, `prepare(48000.0)`, make **no** parameter call. Assert **at that point and again
after one `updateChunk(64)`**:

- `getOutputCount() == 64`;
- `getOutputRatios()[i] == float(i + 1)` within `1e-6` relative, all 64;
- `getOutputAmplitudes()[i]` within `1e-6` relative of `makeFactoryState(SpectralStateId::SineStack)
  .amplitudes[i]`, and **non-zero for every `i`**;
- `getTravelPosition() == 0.0f`, `getBloom() == 0.0f`, `entropy().getEntropy() == 0.0f` (bitwise);
- `getTravelMode() == TravelMode::External`; `getTravelRate() == kMinTravelRate` (bitwise);
- `getStateCount() == kMinStates` (2);
- then **200 further `updateChunk(64)` calls leave every output element BITWISE unchanged**.

`TEST_CASE("SpectralMorph_FillRecurrenceMatchesSpec", "[spectral_morph][seraphis]")` — SC-015 fill arm
(deviations D9, D13). For each of the three sparse states, load into slot 0 **and** slot 1, set
`numStates = 2`, `bloom = 0`, `entropy = 0`, `prepare(48000)`, one `updateChunk(64)`, then compare
`getCleanRatios()` **element-wise** against the recurrence **recomputed in the test** from
`r[j] = max(j + 1, r[j-1] · kFillSpacingFactor)` (`kFillSpacingFactor = 1.0163049f`):

| Loaded state | Expected `r[0..63]` |
|---|---|
| `numPartials = 0` | `1, 2, 3, …, 62` up to `j = 61`, then `r[62] = 63.011`, `r[63] = 64.038` — the 28-cent floor overtakes `j + 1` once `j > 61.3` |
| `numPartials = 1, ratios[0] = 1.0f` | identical to the row above |
| `numPartials = 1, ratios[0] = 128.0f` | `128`, then the pure 28-cent staircase `128 · 1.0163049^j`, reaching `128 · 2^1.47 = 354.59` at slot 63 — inside `kMaxOutputRatio = 360.37` with 1.6 % headroom (**D13**) |

Tolerance `1e-4` relative. Also `REQUIRE(r[63] <= kMaxOutputRatio)` for all three.

`TEST_CASE("SpectralMorph_SetStateRejects", "[spectral_morph][seraphis]")` — SC-015 `setState` arm.
Each of the following must be **rejected**, each asserted to leave `getOutputRatios()`,
`getOutputAmplitudes()` and `isStateFadeActive()` **bitwise** unchanged: `slot = -1`;
`slot = kMaxStates`; `numPartials = -1`; `numPartials = 65`; a non-monotone ratio pair; a ratio
outside `[kMinStateRatio, kMaxStateRatio]`; an amplitude `> 1.0f`; a negative amplitude; a non-finite
`tiltDbPerOct` / `inharmonicity` / ratio / amplitude (bit patterns via a `volatile` sink);
`tiltDbPerOct` or `inharmonicity` out of range; a `name` with no NUL byte.
This is a **stricter** rejection set than `setSpectralTarget`'s — the divergence is the point
(FR-012 vs FR-081); the mirror-image acceptance assertion lands in T018/T017.
Also: `setState` with an **identical** state is a no-op (`isStateFadeActive()` stays false);
`setStateCount(n)` with `n == getStateCount()` is a no-op.

**Implement.** Constants per plan §5.1, verbatim:

```cpp
static constexpr int         kMinStates     = 2;
static constexpr int         kMaxStates     = 4;
static constexpr std::size_t kStatePartials = SpectralState::kStatePartials;   // 64
static constexpr float kMaxBloomFraction   = 0.6f;
static constexpr float kMinTravelRate      = 1.0f / 600.0f;   // journeys per second
static constexpr float kMaxTravelRate      = 1.0f;
static constexpr float kStateChangeFadeSec = 2.0f;
static constexpr float kMaxFillGrowth    = 2.0f;
static constexpr float kMaxFillRatio     = SpectralState::kMaxStateRatio;      // 128
static constexpr float kFillSpacingCents = 28.0f;
static constexpr float kFillSpacingLog2  = kFillSpacingCents / 1200.0f;
static constexpr float kFillSpacingFactor =
    detail::constexprExp(kFillSpacingLog2 * detail::kLn2);                     // 1.0163049
static constexpr float kMinRatioSpacingCents = EntropyProcessor::kMinRatioSpacingCents;
static constexpr float kMinRatioSpacingLog2  = EntropyProcessor::kMinRatioSpacingLog2;
// kStatePartials (64), NOT 63: the worst case needs 63 chained float multiplies to REACH the
// ceiling, so a mathematically tight ceiling can be missed by accumulated rounding (D13).
static constexpr float kMaxOutputRatio =
    kMaxFillRatio * detail::constexprExp(static_cast<float>(kStatePartials)
                                         * kFillSpacingLog2 * detail::kLn2);    // 360.37
static constexpr float kOutputCentsSpan =
    1200.0f * detail::constexprLn(kMaxOutputRatio / SpectralState::kMinStateRatio)
            / detail::kLn2;                                                     // 11392.0
static constexpr float kMaxAmpDeltaPerChunk        = 0.025f;
static constexpr float kMaxRatioDeltaCentsPerChunk = 125.0f;
enum class TravelMode : std::uint8_t { External = 0, Spline };   // NESTED (D8)
static constexpr std::uint32_t kDefaultMorphSeed = 1u;           // matching random.h:44
static constexpr std::size_t   kEntropyBaseSalt  = 0x1000;
static constexpr std::size_t   kSplineBaseSalt   = 0x1001;
```

**The FR-044 contributor table is a `static_assert`, not a comment** — write it exactly as plan §5.1
gives it (`kFr044SampleRate = 48000`, `kFr044ChunkSamples = 64`, `kFr044TravelAmp`, `kFr044StateAmp`,
`kFr044DeathAmp`, `kFr044AmpOuStep`/`kFr044CentsOuStep` **routed through
`EntropyProcessor::onePoleChunkStep`**, `kFr044JitterAmp`, then the two asserts). Expected values:

| Term | Value |
|---|---|
| `kFr044TravelAmp` | `1.00000e-2` |
| `kFr044StateAmp` | `6.66667e-4` |
| `kFr044DeathAmp` | `2.66667e-3` |
| `kFr044AmpOuStep` = `kFr044JitterAmp` | `8.84950e-3` |
| **amplitude sum ≤ 0.025** | **`2.21829e-2`** ✔ 11.3 % slack |
| `kFr044CentsOuStep` | `4.34712e-2` |
| travel+state cents | `121.515` |
| decoherence cents | `0.34777` |
| **cents sum ≤ 125.0** | **`121.863`** ✔ 2.5 % slack |

**If the amplitude assert fails to compile, the bug is `kEntropyAmpSmoothMs` (T010), not the bound.**
`kMaxAmpDeltaPerChunk` is **never** raised.

State layout per plan §5.3. **Deviation D10:** the engine stores **sanitized per-slot arrays**
(`slotLog2Ratio_`, `slotAmp_`, `slotNumPartials_`), never `SpectralState` copies — this makes FR-013
structural (`tiltDbPerOct` / `inharmonicity` / `name` are *incapable* of reaching the audio path),
forces the required zeroing of `slotAmp_[s][i]` for `i >= numPartials` (FR-012 leaves those
unconstrained and a caller may fill them with garbage), and saves 4 × 544 bytes per instance.

`setState(slot, s)` returns immediately writing **nothing** if `slot ∉ [0, kMaxStates)` or
`!isValidSpectralState(s)`. Otherwise: copy ratios below `numPartials`, run the **FR-041 fill**
(same recurrence as T008; `j >= 2` always holds in the geometric arm because the loop starts at
`j = numPartials >= 2`), zero `slotAmp_` at `i >= numPartials`, and
`slotLog2Ratio_[s][i] = std::log2(r[i])` (64 `log2` per call — config rate).

Constructor / prepare / reset per plan §5.6:

```
CONSTRUCTOR:  all four slots ← makeFactoryState(SineStack);  numStates_ = 2;  bloom_ = 0;
              invCompletionPoint_.fill(1.0f);  mode_ = External;  travelRate_ = kMinTravelRate;
              entropy_.setEntropy(0.0f);  then the same rewind reset() performs.
prepare(sr):  sampleRate_ = sr > 1.0 ? sr : 1.0;  invSampleRate_ = 1/sampleRate_;
              spline_.prepare(sampleRate_);  entropy_.prepare(sampleRate_);  reset();  prepared_ = true;
reset():      // STOCHASTIC AND TRAVEL STATE ONLY — no configured parameter is touched.
              position_ = targetPosition_ = 0.0f;  fadeX_ = 1.0f;
              repairCount_ = 0;  limiterActiveChunks_ = totalChunks_ = 0;
              departLogRatio_.fill(0.0f);  departAmp_.fill(0.0f);
              spline_.setSeed(deriveStreamSeed(configuredSeed_, kSplineBaseSalt));  spline_.reset();
              entropy_.setSeed(deriveStreamSeed(configuredSeed_, kEntropyBaseSalt)); entropy_.reset();
              refreshOutputs();
```

**Deviation D15 — `reset()` rewinds, it does not reconfigure.** FR-005 says `reset()` matches
`BrownianDrift::reset()` (`brownian_drift.h:133-135`), which keeps smoothness and depth; FR-005's
default *table* is scoped to "after default construction and after `prepare()` with **no** parameter
call". A wiping `reset()` would (a) make SC-012's rewind clause unsatisfiable at its own
`entropy = 1` configuration and (b) erase the patch on **every Phase 7 voice allocation**.

`refreshOutputs()` runs the T015 pipeline **without advancing anything** and ends with
`entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, 0)` — the `numSamples == 0` path.
This is what makes SC-002 clause 5 satisfiable.

`setSeed(s)` stores `s`, re-seeds spline + entropy from the two derived base salts, and calls
`refreshOutputs()`. Document it as a **configuration-time** call (it steps up to
`2 · kMaxScatterCents = 14` cents per partial).

**Verify:** `dsp_systems_tests.exe "SpectralMorph_DefaultsAreAudible" "SpectralMorph_FillRecurrenceMatchesSpec"
"SpectralMorph_SetStateRejects"` green; zero warnings.

---

## GROUP O — `systems/spectral_morph_engine.h`: the chunk pipeline

### T015 — Travel, bloom, interpolation, repair, absorption (FR-041, FR-046, FR-047, FR-051, FR-061–063)

**Files:** EDIT `dsp/include/krate/dsp/systems/spectral_morph_engine.h`;
EDIT `dsp/tests/unit/systems/spectral_morph_engine_test.cpp`.

**Test first.** Four cases.

`TEST_CASE("SpectralMorph_EndpointsAreExact", "[spectral_morph][seraphis]")` — SC-002 cl.1 + cl.2.
Factory five only, **all 10 pairs**, `bloom = entropy = 0`:
- At `p = 0`: for `i < state[0].numPartials`, `getCleanRatios()[i]` and `getCleanAmplitudes()[i]`
  within **`1e-6` relative** of state 0; for `i >= numPartials`, amplitude **exactly `0.0f`** and
  ratio equal to the FR-041 continuation **recomputed in the test** from the same recurrence.
  Symmetrically at `p = numStates − 1`.
- Monotone `u_i` over an increasing `p` sweep: violation count **0**.
- cl.2: `bloom = 0`, all 10 pairs, full sweep ⇒ `getRepairEngagementCount()` delta must be **exactly
  0** (the tightest clean adjacent spacing anywhere in the five states is **27.264** cents against
  `kMinRatioSpacingCents = 24.0`; the fill region carries its own 28.0-cent floor). At `bloom = 1`
  the count is **reported, not gated**, and cl.1's endpoint tolerances are re-asserted.

`TEST_CASE("SpectralMorph_BloomStaggersLowToHigh", "[spectral_morph][seraphis]")` — SC-003.
- `bloom = 1` at `u = 0.5`: `u_1 == 1.0f`; `u_64 < 1.0f`; the sequence is **non-increasing in index**;
  `u_1 − u_64 >= kMinBloomSpread = 0.3`.
- `bloom = 0`: all 64 equal `u` within `1e-7` (the exact branch makes it **bitwise**).
- `u_i ∈ [0,1]` at every bloom × reachable `u`.
- **Join clause as a limit + handoff, never asserted at the unreachable `u = 1`:** at `u = 0.99999`
  every `u_i >= 1 − 1e-5`; stepping `p = k + 0.99999 → k + 1` yields state `k+1` within `1e-6`
  relative (scoped exactly as SC-002 cl.1) with the FR-044 delta bound satisfied across the crossing.

`TEST_CASE("SpectralMorph_SplineTravelReachesEndpoints", "[spectral_morph][seraphis]")` — SC-002 cl.3.
Spline, `numStates = 2`, **`setTravelRate(kMaxTravelRate)`**,
`setWaypointInterval(2.0)`, `setDepth(1.0)`, **≥ 1200 s per seed × 8 seeds**: position comes within
**0.02** of both `0` and `1` at least once per seed, and stays in `[0,1]` throughout. Run length is
derived (`0.98^600 ≈ 5.4e-6` per endpoint per seed) — **if shortened, redo the arithmetic; never
widen the tolerance.**

`TEST_CASE("SpectralMorph_SplineLimiterHasHeadroom", "[spectral_morph][seraphis]")` — SC-002 cl.4.
Spline, `numStates = 4`, `setTravelRate(kMaxTravelRate)`,
`setWaypointInterval(SplineTrajectory::kDefaultInterval)` (2.0, `spline_trajectory.h:123`),
`setDepth(1.0)`, **≥ 300 s × 8 seeds**: `getLimiterActiveChunks() / getTotalChunks() < 0.01`, with the
measured fraction **reported**. Honest expectation is exactly 0 (analytic worst case 2.25 u/s against
a 3.0 u/s cap).

**Implement.** Per plan §5.5.

**Deviation D1 — everything runs in the log2 domain until ONE `exp2`.** FR-041/046/047 are each stated
in the linear ratio domain; the log2 form is the same function in every case (`log2` is strictly
increasing, so `max` commutes with it; a convex combination of arrays whose adjacent log gaps are each
`>= kMinRatioSpacingLog2` has adjacent gaps `>= kMinRatioSpacingLog2`), and it cuts the per-chunk
transcendental count from three per partial to one. SC-010 clause 1 is on the line — this is
**adopted from the start**, not conditional.

```cpp
void updateChunk(std::size_t numSamples) noexcept {
    if (numSamples == 0) return;                  // no-op, state UNADVANCED
    advanceTravel(numSamples);                    // FR-061/062/063
    recomputeCompletion();                        // FR-051
    interpolate();                                // FR-041  -> logRatio_, cleanAmp_
    repairSpacing();                              // FR-046  (log domain), repairCount_
    applyAbsorption(numSamples);                  // FR-047  -> logRatio_, cleanAmp_
    for (i < kStatePartials) cleanRatio_[i] = std::exp2(logRatio_[i]);   // the ONE exp2
    outRatio_ = cleanRatio_;  outAmp_ = cleanAmp_;
    entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, numSamples);   // FR-070
}
```

Sub-steps, verbatim from plan §5.5:

- **`recomputeCompletion`** — `invCompletionPoint_` recomputed **only in `setBloom`** (config rate):
  `if (bloom_ == 0.0f) invCompletionPoint_.fill(1.0f);` (**explicit branch** — `-ffast-math`), else
  `e_n = 1 − bloom_·kMaxBloomFraction·(1 − (n−1)/(kStatePartials−1))`, `invCompletionPoint_[n-1] = 1/e_n`
  (`e_n >= 0.4`, never near zero). Per chunk: `u = position_ − floor(position_)`;
  `completion_[i] = clamp(u * invCompletionPoint_[i], 0, 1)`. At `bloom = 0` the multiply is by
  exactly `1.0f`, so `u_i == u` **bitwise**. Travelling backwards uses the same expression, so high
  partials lead on the way back (FR-052) with **no ratchet state**.
- **`interpolate`** — `k = clamp(int(floor(position_)), 0, numStates_ − 1)`; `u = position_ − k`;
  `A = k`, `B = min(k+1, numStates_ − 1)`; `invU_i = 1 − completion_[i]`;
  `cleanAmp_[i] = slotAmp_[A][i]·invU_i + slotAmp_[B][i]·completion_[i]`
  (the two-term form verbatim from `spectral_morph_filter.h:594,601`);
  `logRatio_[i] = slotLog2Ratio_[A][i]·invU_i + slotLog2Ratio_[B][i]·completion_[i]`;
  `outCount_ = max(slotNumPartials_[A], slotNumPartials_[B])`. At `position_ == numStates_ − 1`
  exactly, `B == A` — the degenerate segment outputs state `A`, which is what makes
  `p = numStates − 1` well-defined and is also the FR-005 default.
  **`crossfade_utils.h` is deliberately NOT used** — this is a magnitude-linear blend of two coherent
  spectra sharing partial slots, not two decorrelated signals.
- **`repairSpacing`** — for `i ∈ [1, 64)`: `floorLog = logRatio_[i-1] + kMinRatioSpacingLog2`;
  `if (logRatio_[i] < floorLog) { logRatio_[i] = floorLog; changed = true; }`;
  `if (changed) ++repairCount_`. 64 compares, at most 63 adds.
- **`applyAbsorption`** — `if (fadeX_ >= 1.0f) return;` (explicit branch);
  `fadeX_ = min(1, fadeX_ + numSamples·invSampleRate_ / kStateChangeFadeSec)`; `inv = 1 − fadeX_`;
  `logRatio_[i] = departLogRatio_[i]·inv + logRatio_[i]·fadeX_`, likewise `cleanAmp_`.
  Armed by `setState` (**only** on a slot that currently contributes, i.e. `A` or `B`) and by
  `setStateCount`: snapshot `departLogRatio_ = logRatio_`, `departAmp_ = cleanAmp_` **from the current
  post-absorption clean arrays**, then `fadeX_ = 0.0f`. A second qualifying call while `fadeX_ < 1`
  **re-snapshots from the current arrays**, so the output stays continuous through any number of
  overlapping changes. The ramp advances on `chunkSeconds`, so it is sample-rate and chunk-length
  independent (SC-013). **`crossfade_utils.h` is not used** — this ramp advances per chunk, not per
  sample.
- **`advanceTravel`** —
  ```
  dt = numSamples * invSampleRate_;
  if (mode_ == Spline) {
      spline_.processBlock(numSamples);                     // spline_trajectory.h:193
      s    = spline_.getCurrentValue();                     // :204
      unit = clamp((s / SplineTrajectory::kWaypointMax + 1.0f) * 0.5f, 0.0f, 1.0f);
      target = unit * float(numStates_ - 1);
  } else { target = targetPosition_; }
  cap   = travelRate_ * float(numStates_ - 1) * dt;
  delta = target - position_;
  ++totalChunks_;
  if (|delta| > cap) { position_ += (delta > 0 ? cap : -cap); ++limiterActiveChunks_; }
  else               { position_ = target; }
  position_ = clamp(position_, 0.0f, float(numStates_ - 1));
  ```
  **One position state, one limiter, shared by both modes** — which is exactly why FR-062's mode
  switch costs nothing extra: it changes the limiter's *target*, never its output. The
  `kWaypointMax` rescale is what makes `p = 0` and `p = numStates−1` reachable; the clamp is
  **required** because uniform Catmull-Rom overshoots its control points
  (`spline_trajectory.h:56-66` bounds `|q|` at 1.0, not at `kWaypointMax = 0.8f` `:121`).

**Exact-zero fast paths (explicit branches, `-ffast-math`, adopted from the start):** `bloom == 0.0f`
skips the completion recompute; `fadeX_ >= 1.0f` skips absorption; `A == B` skips the interpolation
lerps; `entropy_ == 0.0f` skips `applyStages` (already in T012).

Also add the FR-008 accessors (`getOutputRatios`, `getOutputAmplitudes`, `getOutputCount`,
`getCleanRatios`, `getCleanAmplitudes`, `getTravelPosition`, `getCompletionFraction`, `getBloom`,
`getTravelRate`, `getTravelMode`, `getStateCount`, `getRepairEngagementCount`,
`getLimiterActiveChunks`, `getTotalChunks`, `isStateFadeActive`, `stateFinite`, `entropy()`).
**Both output pointers address stable member storage whose address never changes over the instance's
lifetime** — that is what makes FR-086 copy-free.

**Verify:** `dsp_systems_tests.exe "SpectralMorph_*"` green; zero warnings.

---

## GROUP P — engine: continuity, invariance, determinism

### T016 — SC-001 cl.1, SC-013, SC-012

**File (EDIT):** `dsp/tests/unit/systems/spectral_morph_engine_test.cpp` (implementation fixes to
`spectral_morph_engine.h` only if a criterion fails).

`TEST_CASE("SpectralMorph_TravelIsContinuous", "[spectral_morph][seraphis]")` — SC-001 cl.1.
**18 configurations** (`bloom ∈ {0, 0.5, 1}` × `entropy ∈ {0, 0.5, 1}` × `TravelMode`), plus the
pinned state loads: 2-state SineStack/Bell; 4-state SineStack/Bell/Glass/Breath; **one adversarial
arm** `{numPartials = 2, ratios = {0.5f, 128.0f}}` vs SineStack. Full out-and-back sweep at
`kMaxTravelRate`, 64-sample chunks at 48 kHz. Per chunk:

- `max_i |Δa_i| <= kMaxAmpDeltaPerChunk` (0.025);
- `max_i |1200·log2(r_i / r_i^prev)| <= kMaxRatioDeltaCentsPerChunk` (125.0) **for `L_i > 0` only**.

`setState`, `setStateCount` and a `setTravelMode` switch are performed **mid-sweep**.
`setSeed` / `reset` are **never called** — state that fact in a comment beside the loop (they are
FR-044's named configuration-time exemptions). The two constants are pinned by the §5.1
`static_assert`s, so the test **cannot** be satisfied by loosening them.

`TEST_CASE("SpectralMorph_SampleRateInvariant", "[spectral_morph][seraphis]")` — SC-013.
Rates `{44100, 48000, 96000}` × chunk lengths `{1, 7, 64, 512, 4096, 65536}`.

- **The 65536 entry is required by FR-063, not padding.** `SplineTrajectory`'s *shortest legal*
  interval is `kMinInterval = 0.5 s` (`spline_trajectory.h:117`) = 24,000 samples at 48 kHz, so a grid
  topping out at 4096 (85 ms) never exercises the waypoint-rotation path
  (`spline_trajectory.h:262-269`). Run the 65536 case in **Spline** mode at
  `setWaypointInterval(SplineTrajectory::kMinInterval)` — 1.365 s per chunk, ≥ 2 waypoints rotated
  per call — asserting the position stays finite and in `[0, numStates−1]` **and still advances**
  (non-zero position change over the run).
- **Journey clause:** `numStates = 2`, External at `kMaxTravelRate` ⇒ nominal **1.000 s**; measured
  duration within `max(0.5 % of nominal, one chunk duration)`, with **both terms reported** so it is
  visible which bound applied.
- Entropy stationary metric agrees across rates within `max(5 % relative, 5 × the larger reported SE)`.
- The FR-044 bound is met at every rate/length, with the test **scaling** the constants by
  `chunkSeconds` rather than re-deriving them.
- `prepare()` with a new rate leaves **no stale coefficient** (re-assert the T011 Arm-2 coefficient
  identities after a rate change).

`TEST_CASE("SpectralMorph_DeterministicUnderSeed", "[spectral_morph][seraphis]")` — SC-012, four clauses.

1. Two instances, same seed, same `prepare(48000)`, same call sequence, `entropy = 1`, Spline:
   output arrays **bitwise identical** over ≥ 500 chunks. Seed `0` is safe (assert it explicitly).
2. **Rewind clause, matching §5.6's `reset()` semantics (D15).** Instance X: `prepare`, configure
   (states, count, bloom, entropy, mode, rate, seed), advance ≥ 500 chunks, `reset()`. Instance Y:
   `prepare` + the **identical** configuration calls in the identical order. The two output arrays
   must then be **bitwise identical**, and X's configuration getters must be **unchanged across its
   `reset()`** (assert explicitly — that is what proves `reset()` rewinds rather than reconfigures).
   *Do not restate this as "matches a bare post-`prepare` instance": at `entropy = 1` a faithful
   implementation cannot satisfy that form.*
3. All `4 × 64 = 256` derived stream seeds pairwise distinct and non-zero, asserted **directly on the
   Layer 0 `deriveStreamSeed`**, for at least the 8 pinned seeds. Second half:
   `HarmonicCloud::deriveSeed(b, s) == deriveStreamSeed(b, s)` over the same input set — the proof
   that T006's forwarding rewrite left Phase 2's streams untouched. (This TU may include
   `harmonic_cloud.h`; the **production header** may not.)
4. **FR-075 advance invariant.** Run A advances an `EntropyProcessor` for N chunks of 64 samples at a
   fixed `count = 64`. Run B does the same but for chunks `k ∈ [100, 200)` passes **`count = 0`** —
   the configuration two default-constructed `SpectralState`s produce. At the end, every
   `getLifePhase`, `getRawScatterDraw`, `getScatterRedrawCount`, `getAmpJitterFactor` and
   `getDecoherenceCents` must be **bitwise identical** between the two runs.

**Verify:** `dsp_systems_tests.exe "SpectralMorph_*"` green; zero warnings.

---

## GROUP Q — engine: entropy metrics, RT safety, extremes

### T017 — SC-004 m.1–2, SC-011, SC-015 extremes grid

**File (EDIT):** `dsp/tests/unit/systems/spectral_morph_engine_test.cpp`.

`TEST_CASE("EntropyProcessor_DisorderIncreasesMonotonically", "[spectral_morph][seraphis]")` —
SC-004 m.1–2, measured on the **engine's** FR-008 arrays (output vs clean), not on the processor.

- **≥ 11 settings** including `0, 0.25, 0.35, 0.50, 0.60, 0.75, 0.85, 1`.
- Metric 1 = mean over partials of `|1200·log2(r_i / r_i^clean)|`, from `getOutputRatios()` vs
  `getCleanRatios()`.
- Metric 2 = mean over partials with `a_i^clean > 1e-4` of `|a_i − a_i^clean| / a_i^clean`.
- Strict increase over the driving stage's interval by **≥ 5 × the test's own reported standard
  error**; non-decrease elsewhere at 1 × SE.
- Averaged over **8 seeds × ≥ 10 τ** (τ = 8 s ⇒ **≥ 80 s per seed**), first **2 τ discarded**.
- Anchor: `metric2(0.35) ∈ [0.15, 0.21]`, with the derivation recorded beside it —
  `kMaxAmpJitter · kInternalStd · sqrt(2/π) = 0.1995`, **times the D12 smoother correction**
  `sqrt(tau_walk/(tau_walk + tau_smooth)) = sqrt(3.0/3.15) = 0.976` ⇒ expected **0.1947**.

`TEST_CASE("SpectralMorph_NoAllocInSteadyState", "[spectral_morph][seraphis]")` — SC-011.

- **Liveness first** (Phase 2 pattern): a real `new int[16]` inside an `AllocationScope` with
  `REQUIRE(livenessCount >= 1)`, proving the overrides are linked.
- Then a steady-state loop over `updateChunk`, `EntropyProcessor::processChunk`, `setEntropy`,
  `setBloom`, `setTargetPosition`, `setState`, `setSpectralTarget`, `processStereoBlock` with
  `REQUIRE(count == 0)` after `prepare()`, plus a **non-vacuity** check (`RMS > 0`).
- **This TU must NOT include `allocation_operator_overrides.h`** — `dsp_systems_tests` already links
  it via `unit/systems/selectable_oscillator_test.cpp:388`.
- `static_assert(noexcept(...))` over the **full public surface** of both new classes, `prepare`
  included. `prepare` is `noexcept` **and** documented "not RT-safe by contract" — those are
  different claims and both hold.

`TEST_CASE("SpectralMorph_ExtremesStayFinite", "[spectral_morph][seraphis]")` — SC-015 (the
`setState`-rejection and fill arms already landed in T014; this is the remainder).

- Grid: `entropy {0,1}` × `bloom {0,1}` × rate `{kMinTravelRate, kMaxTravelRate}` × states `{2,4}` ×
  4 seeds × the extremal factory states, plus the adversarial `{0.5f, 128.0f}` two-partial state.
- No output element non-finite (bit-pattern check); `stateFinite()` true throughout; every setter
  rejection leaves getters and output **bitwise** unchanged.
- **`setSpectralTarget` rejection set enumerated** (must reject: null pointers, `count == 0`,
  `count > 64`, any non-finite, any `ratios[i] <= 0.0f` **including `-0.0f`**, any
  `amplitudes[i] < 0.0f`) **and its acceptance set enumerated and asserted** — non-monotone ratios; a
  ratio above `kMaxStateRatio`; a ratio below `kMinStateRatio`; an amplitude above 1 up to
  `1 + kMaxAmpJitter`. An over-zealous implementation must fail here.
  *(This clause depends on T018; if T018 has not landed, split it out and add it at the end of T018 —
  do not weaken it.)*
- **Mirror-image assertion:** the **same** ratio/amplitude arrays that `setState` rejects for
  non-monotonicity, for a ratio above `kMaxStateRatio` and for an amplitude above 1 **are accepted**
  by `setSpectralTarget`. FR-012 and FR-081 diverge **on purpose** (spec.md:604-611).
- Non-finite inputs built from bit patterns through a `volatile` sink — never
  `std::numeric_limits<float>::quiet_NaN()`.

**Verify:** `dsp_systems_tests.exe 2>&1 | tail -5` — whole systems suite green; zero warnings.

---

## GROUP R — `systems/harmonic_cloud.h` FR-080 amendment

### T018 — Spectral-target injection into `HarmonicCloud` (FR-081 – FR-086)

**Strictly additive. Every change is inert while `hasTarget_` is false** — which is what makes SC-014
clause 1 a structural property rather than a measurement. This touches a **COMPLETE** Phase 2
component (risk R11).

**Files**

- EDIT: `dsp/include/krate/dsp/systems/harmonic_cloud.h`
- EDIT: `dsp/include/krate/dsp/systems/spectral_morph_engine.h` (the FR-086 doc block only)

**Test first.** The SC-014 assertions live in the render TU (T019). For **this** task the immediate
gates are: the existing Phase 2 suites pass **unedited**, and T017's `setSpectralTarget`
acceptance/rejection clause compiles and passes.

**Implement — 1. New members** (plan §6.1):

```cpp
alignas(32) std::array<float, kMaxPartials> targetRatio_{};     // latest supplied
alignas(32) std::array<float, kMaxPartials> targetAmp_{};
// The values the LAST RECOMPUTE ACTUALLY CONSUMED. The dirty test compares against THESE,
// never against targetRatio_/targetAmp_.
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

**2. Dirty-flag helpers, and `reset()` sets the masks to ALL-ONES.**

```cpp
void markFreqDirty() noexcept { freqDirty_ = true; freqSlotDirty_ = ~std::uint64_t{0}; }
void markAmpDirty()  noexcept { ampDirty_  = true; ampSlotDirty_  = ~std::uint64_t{0}; }
```

**This is load-bearing and easy to get backwards.** `reset()` (`:286`) calls
`recalculateFrequencies()` and `recalculateAmplitudes()` **directly and unconditionally**
(`:309-310`) and only clears `freqDirty_`/`ampDirty_` afterwards (`:321-322`) — it never goes through
the flags. With the per-slot guard added below and the masks **zeroed**, both loops would iterate 64
times and **write nothing**. `prepare()` recomputes `nyquist_`/`invSampleRate_` and *then* calls
`reset()` (`:255-283`), so a sample-rate change on a target-active cloud would leave every
`epsilon_[i]` derived from the **old** rate — every partial at the wrong pitch. `reset()` must
therefore go through `markFreqDirty()`/`markAmpDirty()`, making a `reset()` a full recompute exactly
as it was before the amendment.

**3. Replace the eight existing dirty-flag assignments** — the inventory below was enumerated from
`grep -n "freqDirty_ = true\|ampDirty_ = true"` this session. There are **four and four**, and two of
the `ampDirty_` sites are **not setters**:

| Line | Context | Becomes |
|---|---|---|
| `:360` | `setFundamentalHz` | `markFreqDirty()` |
| `:379` | Richness setter (`N(r)` may move) | `markFreqDirty()` |
| `:380` | Richness setter (rolloff exponent) | `markAmpDirty()` |
| `:393` | inharmonicity setter | `markFreqDirty()` |
| `:406` | tilt setter | `markAmpDirty()` |
| `:445` | gravity setter | `markFreqDirty()` |
| `:602` | inside `noteOn()`, after its guarded `recalculateFrequencies()` | `markAmpDirty()` |
| `:1316` | inside `updateControl`'s step-0 flag consumption | `markAmpDirty()` |

A parametric change invalidates **every** slot; only `setSpectralTarget` sets a partial mask.

**4. `setSpectralTarget` / `clearSpectralTarget` / `hasSpectralTarget`** — write plan §6.3's body
verbatim. Key points that must not be simplified:

- FR-081 rejection list is **authoritative for this entry point** (FR-012 defers to it): reject
  wholesale, writing nothing, on null pointers, `count == 0`, `count > kMaxPartials`, any NaN/Inf,
  `ratios[i] <= 0.0f` (which rejects `-0.0f`), `amplitudes[i] < 0.0f` (which **accepts** `-0.0f`).
  Amplitudes above 1 and above `1 + kMaxAmpJitter` are **accepted** — that is the point of the
  surface.
- Slots `i >= count` are filled with `ratio = i + 1`, `amp = 0.0f`.
- **The comparison baseline is `committedRatio_` / `committedAmp_`, never `targetRatio_` /
  `targetAmp_`** (deviation **D14**). The arithmetic that forces it: at `numStates = 2` and the
  FR-005 default `kMinTravelRate = 1/600`, Δp per 64-sample chunk at 48 kHz is `2.22e-6` units; for
  the SineStack→Bell pair, partial 24 spans 2753 cents per unit of `p`, i.e. **0.0061 cents/chunk** —
  permanently below `kTargetRatioEpsilonCents = 0.05`. With the stored target as baseline the
  baseline advances with the input while the recompute is skipped, so **sub-epsilon motion
  accumulates forever and most partials freeze at their start frequency for the whole journey**.
- **Deviation D4:** FR-085 specifies a per-slot compare "in the precomputed log domain"; implemented
  as the equivalent relative test `|r_new − r_old| > r_old · kTargetRatioRelEpsilon`, because a
  per-slot `log2` per chunk costs strictly more than the `exp2` the lever exists to save.
- `clearSpectralTarget()` sets `hasTarget_ = false; markFreqDirty(); markAmpDirty();` — the return to
  the parametric path goes through the same dirty-flag path and the same amplitude smoother, so it
  cannot click (FR-084).

**5. `recalculateFrequencies()` (`:1064-1093`)** — add, as the first statement of the per-partial
loop, `if (hasTarget_ && (freqSlotDirty_ & (1ull << i)) == 0) continue;`, write
`committedRatio_[i] = targetRatio_[i]`, and branch the **warp factor alone** per plan §6.4. End the
function with `freqSlotDirty_ = 0;`.

**The `else` arm must fall back to the UNMODIFIED parametric `ratioG` of `:1085-1086`, INCLUDING its
own `gravityIsZero` branch:**

```cpp
ratio = gravityIsZero ? n : std::exp2(exponent * detail::kHarmonicCloudLog2N[i]);
```

Falling back to the `std::exp2` arm alone would evaluate `exp2(1.0f * log2N[i])`, which is exactly
the rewrite the comment at `:1066-1072` warns hands back **31.999998 for n = 32** under
`-ffast-math` — destroying the bit-exactness the guard exists to protect, **invisibly to SC-014's
fingerprint tolerances**.

Everything else in the function is **unchanged**: `stretch` (`:1087`), the multiply order of `f`
(`:1088`), `frequencyHz_[i]` (`:1089`), the `epsilon_[i]` clamp (`:1090-1091`). Phase accumulators are
still never touched (`:1062-1063`), so a frequency change stays phase-continuous by construction.

**6. `recalculateAmplitudes()` (`:1134-1172`)** — add
`if (hasTarget_ && (ampSlotDirty_ & (1ull << i)) == 0) continue;` and
`committedAmp_[i] = targetAmp_[i];`, then change **only** the one factor at `:1155-1156`:

```cpp
baseAmplitude_[i] = hasTarget_
    ? targetAmp_[i] * tiltGain(i)
    : std::exp2(-exponent * detail::kHarmonicCloudLog2N[i]) * tiltGain(i);
```

**Add `ampSlotDirty_ = 0;` immediately before `normGain_.setTarget(currentNormGainTarget())`** —
FR-083 requires `setTarget` to remain the **last statement of the function**. Without the reset the
mask saturates after a few chunks (`setSpectralTarget` accumulates with `|=`) and the amplitude half
of lever 3 becomes dead code.

**Unchanged:** the Richness count law `N(r)` (`:1138-1139`) and its `activeCount_` write; the zeroing
of slots at or above `activeCount_` (`:1147-1150`) — **which must run BEFORE the dirty-slot skip** so
a Richness reduction still silences a slot; the FR-043 tail high-water logic (`:1163-1164`).
Richness's rolloff exponent has **no effect while a target is active** — deliberate (C-3):
multiplying a state's own shape by `n^(−p)` would erase the timbral distinction SC-008 protects.

**7. FR-086 doc block** — paste plan §6.6's block **verbatim** into both the
`spectral_morph_engine.h` class-level doc **and** the `setSpectralTarget` doc comment in
`harmonic_cloud.h`. It states the ≤ 64-sample slice cadence and *why it is a bound and not a
suggestion*: `processStereoBlock` restarts its internal 64-sample control grid on every call
(`:713-716`) and `setSpectralTarget` only raises `freqDirty_`/`ampDirty_`, consumed at the head of the
**first** `updateControl` of that call (`:1313-1321`).

**Verify:**
- `dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5` green with `harmonic_cloud_test.cpp` and
  `harmonic_cloud_spectral_test.cpp` **UNEDITED**. An edit to either is a failure of SC-014 clause 1.
- `dsp_systems_tests.exe "SpectralMorph_ExtremesStayFinite"` green (the acceptance/rejection clause).
- Zero warnings; `node tools/check-portability.js` clean.

---

## GROUP S — render TU: SC-014

### T019 — `HarmonicCloud_SpectralTargetIsNeutralWhenIdentity` (four clauses)

**File (EDIT, stub → content):** `dsp/tests/unit/systems/spectral_morph_render_test.cpp`
Includes `harmonic_cloud_pre_amendment_fingerprints.h` (T001).

**Namespace hazard (plan §0.1 item 4):** if this TU ever includes both `signal_metrics.h` and
`spectral_utils.h`, the two `calculateSpectralFlatness` overloads are in scope under different
namespaces and **every call must be qualified**.

`TEST_CASE("HarmonicCloud_SpectralTargetIsNeutralWhenIdentity", "[spectral_morph][seraphis]")`:

- **Clause 1 — the Phase 2 regression gate.** `setSpectralTarget` is **never called**. Render the
  **same 216-cell grid** T001 captured (same setters, same order, same seed, same 2.0 s / 512-sample
  shape) and compare each against `kPreAmendmentFingerprints[...]` via
  `TestUtils::compareFingerprints` at `kSampleTolerance = 1.0e-4f` / `kMetricTolerance = 1.0e-5`.
  Additionally: the entire existing `harmonic_cloud_test.cpp` / `harmonic_cloud_spectral_test.cpp`
  suites must pass **unedited** (an edit there is a failure of this clause).
- **Clause 2 — identity target.** `ratios[i] = i + 1`,
  `amplitudes[i] = exp2(-p(r) · log2(i + 1))` where `p(r)` is Richness's rolloff exponent
  (`harmonic_cloud.h:196-197`, `kRichnessMinExponent = 3.0f`, `kRichnessMaxExponent = 0.5f`).
  Must match the parametric render at ≥ 3 Richness × `{gravity 0, ±1}` × `{B 0, 0.05}` ×
  `{tilt 0, ±12}`, via `compareFingerprints` at the same tolerances.
- **Clause 3 — no click on clear.** `clearSpectralTarget()` mid-render produces no click by the
  T021 differential detector (same `ClickDetectorConfig` pin).
- **Clause 4 — the `reset()` / `prepare()` recompute path.** With a target **active and non-identity**
  (SineStack ratios scaled by 1.5), call `prepare(96000.0)` — which recomputes
  `nyquist_`/`invSampleRate_` and then calls `reset()` (`:255-283`, `reset()` at `:281`, its
  unconditional `recalculateFrequencies()`/`recalculateAmplitudes()` at `:309-310`) — then render and
  assert **via a 65536-point transform** that every partial's rendered frequency equals
  `f0 · targetRatio[i]` within **0.1 %**, i.e. it tracks the **new** rate. Under an amended `reset()`
  that *clears* the per-slot masks instead of setting them, every partial renders at half pitch —
  this clause is what fails.

**Note lifecycle:** `noteOn()` before the first slice, never `noteOff()`.

**Verify:** `dsp_systems_tests.exe "HarmonicCloud_SpectralTargetIsNeutralWhenIdentity"` green.

---

## GROUP T — render TU: SC-009

### T020 — `SpectralMorph_FactoryPairRenders` (three clauses)

**File (EDIT):** `dsp/tests/unit/systems/spectral_morph_render_test.cpp`.

All 10 factory pairs, engine → cloud **in the FR-086 shape** (≤ 64-sample slices, zero-copy through
the FR-008 accessors) at f0 **110 Hz** / **48 kHz**, `bloom = 0.5`, `entropy = 0`, External.

**Cloud pin:** `richness = 1.0f` (so `N(r) = 64`), `spectralTiltDb = 0`, `mutation = 0`,
`spectralGravity = 0`, `inharmonicity = 0`, drift depth 0, stereo spread 0, envelope times minimum,
**`noteOn()` before the first slice, no `noteOff()`**.

**The render SHAPE is pinned, because the criterion's own measurement depends on it:**
`setTravelRate(0.125f)` ⇒ `slewCap = 0.125 · (numStates − 1) = 0.125` units/s ⇒ the 1-unit journey
occupies exactly **8 s**. Three-phase render: `setTargetPosition(0)` and hold **2.5 s** →
`setTargetPosition(1)` and the **8 s** journey → hold **2.5 s** at `p = 1`. **13.0 s total.**
Endpoint transforms are taken **inside the frozen windows**, starting **0.5 s in** (so the 20 ms
`kNormGainSmoothMs` smoother and the envelopes have settled) and running **65536 samples = 1.365 s**,
which fits the 2.5 s window with 0.635 s to spare.

- **Clause 1.** No non-finite sample (bit pattern); `peak < 0.9 × HarmonicCloud::kOutputClamp`
  (i.e. `< 1.8`); RMS over **every** non-overlapping 100 ms window `>= -60.0 dBFS`; endpoint spectra
  match the two amplitude sets within **`kEndpointMagnitudeToleranceDb = 1.0` dB per partial** on
  **normalized** magnitudes (each divided by partial 1's, so the FR-017 gain cancels).
  **Bracketing sample (deviation D16):** taken from a **separate 2.5 s render frozen at `p = 0.5`**,
  not from a mid-journey window — a 65536-point transform is 1.365 s = 17 % of the journey, so a
  window taken during travel smears across a moving spectrum and the per-partial claim is not
  measurable at all. Bracketing is asserted for `i < min(A.numPartials, B.numPartials)` only;
  anti-alias-faded partials are excluded and the **exclusion count is asserted**.
- **Clause 2.** One render per pair at drift depth `HarmonicCloud::kMaxDriftCents` (50) and
  `mutation = 1.0`, asserting **only** finiteness, peak and non-silence — deliberately **no**
  spectral-shape claim.
- **Clause 3 — the slow-travel arm.** This is the criterion that fails under a stale-baseline
  `setSpectralTarget` (D14). SineStack/Bell only, same cloud pin, **`setTravelRate(1.0f/60.0f)`** ⇒
  the 1-unit journey occupies **60 s**: 2.5 s frozen at `p = 0` → 60 s journey → 2.5 s frozen at
  `p = 1`, **65 s total**, endpoint transforms taken in the frozen windows exactly as clause 1.
  Assert the `p = 1` endpoint spectrum reaches **state B** within `kEndpointMagnitudeToleranceDb`.

Each render is additionally pinned with `render_fingerprint.h` at its published tolerances,
**labelled in the test as a regression pin, not a correctness proof**.

**Verify:** `dsp_systems_tests.exe "SpectralMorph_FactoryPairRenders"` green.

---

## GROUP U — render TU: SC-001 cl.2, SC-004 m.3–4, SC-008 cl.3

### T021 — Click-free travel, flatness, level neutrality, metadata isolation

**File (EDIT):** `dsp/tests/unit/systems/spectral_morph_render_test.cpp`.

`TEST_CASE("SpectralMorph_TravelIsContinuous_Rendered", "[spectral_morph][seraphis]")` — SC-001 cl.2.

SineStack/Bell, `numStates = 2`, f0 110 Hz, 48 kHz, `bloom = 0.5`, External at `kMaxTravelRate`,
**20 s**, `entropy ∈ {0, 1}`, seeds `{1, 7, 13, 29}`. Each render is paired with a **travel-frozen,
drift-live control** at the journey midpoint, same seed and entropy.

`ClickDetectorConfig` **pinned** (the struct default is 44100 and **must be overridden**):
```
sampleRate = 48000.0f, frameSize = 512, hopSize = 256,
detectionThreshold = 5.0f, energyThresholdDb = -60.0f, mergeGap = 5
```
Pass rule, per pair and per entropy-arm/channel median: `moving <= 1.15 · frozen + 5`.
**The 1.15 / +5 figures are placeholders: re-derive them from the measured 4-seed spread during
implementation and write the observed numbers into the test.** If the spread exceeds `±15 % + 5`,
raise the seed count to 8 — **never widen the margin** (the detector has a nonzero false-detection
floor; Phase 2 measured 126 L / 141 R over 30 s on a click-free build).

`TEST_CASE("EntropyProcessor_FlatnessRisesWithEntropy", "[spectral_morph][seraphis]")` — SC-004 m.3.

- **Pinned signal:** `numStates = 2` with **SineStack in BOTH slots**, External, travel frozen at
  `p = 0`, `bloom = 0`, 8 seeds averaged.
- Rendered through the FR-086 shape (≤ 64-sample slices) at f0 110 Hz / 48 kHz, **≥ 10 s per
  setting**, cloud config = T020's.
- **≥ 6 non-overlapping 65536-point Blackman-Harris windows** via `spectral_analysis.h`, bin-wise
  magnitude average, then `flatness = exp(mean_k log m_k) / mean_k m_k` over bins **`[2, 16384)`**,
  **computed inline**.
- **Record the two-function sweep in a comment beside it:**
  `TestUtils::SignalMetrics::calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`)
  is time-domain and **caps the FFT at 4096** (`:337`) — unusable here;
  `Krate::DSP::calculateSpectralFlatness` (`dsp/include/krate/dsp/primitives/spectral_utils.h:335`)
  is magnitude-domain with no cap but **skips bins `<= 1e-10f` and divides by `validBins` rather than
  `numBins`** (`:345-357`), so on a near-silent high-bin region its denominator shrinks with the
  signal and the metric stops being comparable across entropy settings — which is the one comparison
  this row makes.
- **Gate:** `flatness(0.75) >= 1.25 · flatness(0)`, enforced over `[0, 0.75]` only. Over `[0.75, 1]`
  the only requirement is `flatness(1) >= flatness(0)`.
- A first measurement below 1.25 is a **finding about the FR-072 cent constants being too small** —
  raise them inside FR-074's 12-cent budget (which then requires re-running the T014
  `static_assert`s and T012's SC-016 derivation), **never lower the ratio**.

`TEST_CASE("EntropyProcessor_IsLevelNeutral", "[spectral_morph][seraphis]")` — SC-004 m.4.
Exactly the m.3 renders. Broadband stereo RMS in dBFS at each of the **≥ 11** settings, first **0.5 s
discarded** (the 20 ms `kNormGainSmoothMs` smoother settles). **Every value reported.**
Gate: `max − min <= 3.0 dB`. A spread above 3.0 dB is a finding about the composition, **not a
threshold to widen**.

`TEST_CASE("SpectralState_MetadataNeverReachesAudio", "[spectral_morph][seraphis]")` — SC-008 cl.3.
Load a factory state into an engine and render through the T020 cloud config; repeat with
`tiltDbPerOct = +12`, `inharmonicity = 0.1`, `name` overwritten. The two renders must be **bitwise
identical**. (Structurally guaranteed by D10, but asserted anyway — FR-013 otherwise has no criterion
that would fail.)

**Verify:** `dsp_systems_tests.exe 2>&1 | tail -5` green; zero warnings.

---

## GROUP V — perf TU

### T022 — `SpectralMorph_CpuBudget` `[.perf]` (SC-010, three clauses) + the T0.2 spike

**File (EDIT, stub → content):** `dsp/tests/unit/systems/spectral_morph_perf_test.cpp`.

**This task also discharges plan §1's T0.2 prerequisite**, whose *timing* is amended (RA-4, recorded
in T026): measurement happens here, with machine / build config / trial shape / date written into
SC-010 in the shape of `harmonic_cloud_perf_test.cpp:104-122`.

Measurement basis is identical to Phase 2 SC-007: **ns per 512-sample block, best-of-N**, percentage
against the 10.667 ms wall-clock budget, `[.perf]`-tagged (no CI leg evaluates perf cases —
`.github/workflows/ci.yml` filters `'~[performance]~[perf]~[benchmark]~[!benchmark]'`).

- **Clause 1 — engine alone.** Budget `kMorphReferenceNsPerBlock ≈ 16,000 ns` (0.15 % of one core).
  Config: 4 states, `bloom = 1`, `entropy = 1`, Spline at `SplineTrajectory::kDefaultInterval`.
  Derived model to enter the measurement with (plan §8): two 64-lane OU banks ~8,500–10,500 ns;
  morph `exp2` ~2,000; entropy cents→ratio ~2,000; interpolation/repair/absorption/copies ~1,000;
  lifecycle + spline ~500 ⇒ **~14,000–16,000 ns total**. The OU figure is not a guess — Phase 2's
  BASELINE PROVENANCE block records the quiescent path (two 64-lane banks plus two fills) at
  **9,166–10,998 ns/block** on the reference machine (`harmonic_cloud_perf_test.cpp:116`).
- **Clause 2 — cloud with a changing target every chunk**, inside Phase 2's existing 0.5 % envelope.
  Use Phase 2 SC-007's cloud configuration (both drift banks live, mutation 1.0, the 64-sample
  chunked loop). **The changing-target configuration MUST exercise BOTH per-slot masks** — the
  supplied targets move in ratio *and* in amplitude every chunk (a mid-journey `bloom = 0.5`
  traversal at `kMaxTravelRate` does exactly that) — so a missing `ampSlotDirty_ = 0` (T018 step 6)
  shows up as a **cost**, not as silent dead code.
  Gate with a new checked-in `kCloudChangingTargetBaselineNs` under **both** shipped relations,
  `static_assert`ed in the shape of `harmonic_cloud_perf_test.cpp:142` and `:149`:
  ```
  kCloudChangingTargetBaselineNs * 1.5 <= kReferenceNsPerBlock             // <= 53,333 ns
  kCloudChangingTargetBaselineNs       <= kMaxAdmissibleBaselineNsPerBlock // <= 35,555.6 ns
  kCloudChangingTargetBaselineNs / 26,000 <= 1.36                          // vs the shipped baseline
  ```
- **Clause 3 — unchanged target is ~free.** `measuredUnchangedTargetNs <= measuredCloudBaselineNs ×
  1.10`. Structurally satisfied by the same mask (an unchanged target sets no bits and costs 128
  compares per chunk). **These three figures are per-run measurements, named WITHOUT a `k` prefix,
  gated against each other within one run of one TU — never two checked-in literals.**
  The injection cost `measuredChangingTargetNs − measuredCloudBaselineNs` is **reported only, never a
  gate** (a difference of two sampled minima is a biased statistic).

**Levers, spent in order, only if clause 1 misses (pre-decided ladder — do not improvise):**

1. Log-domain morph pipeline (D1) — **already adopted** (T015).
2. Combined stage-2/3 conversion (D3) — **already adopted** (T012).
3. Exact-zero fast paths — **already adopted** (T012, T015).
4. **`centsToPitchRatioFast`** — promote the bounded-domain degree-4 Horner of
   `HarmonicCloud::detail::centsToDriftRatio` (`harmonic_cloud.h:105-110`, measured worst case
   6.15e-08 relative on `[-50, +50]` cents per `:95-100`) into `core/pitch_utils.h` under that new
   Layer 0 name, and rewrite `detail::centsToDriftRatio` as a one-line forward. Entropy's cent domain
   is ±11.0, 4.5× inside the polynomial's documented window. Expected saving ~1,700 ns/block.
   **This is a third amendment to a COMPLETE Phase 2 component and needs RA-1-style recording** —
   see the open questions at the end of this document.
5. **Entropy OU control interval 32 → 64 samples.** Halves the control steps to 8/block and saves
   ~3,000–4,500 ns/block; costs nothing musically (τ of 3 s and 8 s against a 1.33 ms grid). It is an
   exact re-derivation of `a`/`g` from the doubled `dt`, **not** an approximation. Consequences that
   must land **in the same change**: (a) `EntropyProcessor` gains its own `kEntropyControlInterval`
   and stops reusing `BrownianDrift::kControlRateInterval`; (b) T011's
   `EntropyProcessor_OuBankMatchesBrownianDrift` Arm 1 is **replaced** (never deleted) by an
   explicit-coefficient check at `dt = 64/fs` at `1e-6` relative, with the draw-order and lane-seed
   arms unchanged; (c) T016's SC-013 grid must be **re-run**.
6. **There is no lever 6, and `kMorphReferenceNsPerBlock` is NEVER raised.** SC-010 forbids it and
   RA-3's escape is scoped to clause 2 only. If levers 4 and 5 are both spent and clause 1 still
   misses: **stop and report** the measured figures and the levers spent as an honest finding against
   SC-010.

Checked-in `k*BaselineNs` constants carry a **PROVENANCE block** in the shape of
`harmonic_cloud_perf_test.cpp:104-122`.

**Verify:** `dsp_systems_tests.exe "SpectralMorph_CpuBudget"` green (perf cases are hidden by
`[.perf]` and must be named explicitly); measured figures + machine/build/trial-shape/date + any
levers spent recorded for T026.

---

## GROUP W — Integration

### T023 — CMake registration audit (single owner of `dsp/tests/CMakeLists.txt`)

**File (READ + EDIT only if a gap is found):** `dsp/tests/CMakeLists.txt`.

- Confirm all five Phase 3 TUs are present exactly once:
  `unit/processors/spectral_state_test.cpp`, `unit/processors/entropy_processor_test.cpp`,
  `unit/systems/spectral_morph_engine_test.cpp`, `unit/systems/spectral_morph_render_test.cpp`,
  `unit/systems/spectral_morph_perf_test.cpp`.
- Confirm **no stub file remains** (every one of the five contains real `TEST_CASE`s).
- Confirm T001's temporary capture-TU line was reverted and
  `unit/systems/harmonic_cloud_fingerprint_capture.cpp` does not exist.
- Confirm **no list was converted to a glob** and **no source property** was added (no TU in this
  phase needs `-fno-fast-math`).
- Confirm no Phase 3 test file includes `allocation_operator_overrides.h`:
  `grep -rn "allocation_operator_overrides" dsp/tests/unit/processors/spectral_state_test.cpp
  dsp/tests/unit/processors/entropy_processor_test.cpp dsp/tests/unit/systems/spectral_morph_*.cpp`
  must return nothing.
- Confirm `spectral_morph_engine.h` does **not** include `harmonic_cloud.h`:
  `grep -n "harmonic_cloud" dsp/include/krate/dsp/systems/spectral_morph_engine.h` must return
  nothing.

**Verify:** clean configure + build of `dsp_processors_tests` and `dsp_systems_tests`.

### T024 — Full five-layer suite [P with T025]

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release \
  --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
# Phase 2 regression gate (SC-014 clause 1) — must pass UNEDITED:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_*" 2>&1 | tail -5
```

Capture output **to a log file on the first run**; never re-run a slow suite just to re-read output.
All five green. **Own every failure** — "pre-existing" is not an accepted answer.

### T025 — Portability, lints, clang-tidy [P with T024]

```bash
node tools/check-portability.js
node tools/lint-arch-guarded-includes.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

All clean, **zero warnings**. Redirect clang-tidy to a log on the first run.
Specific traps this phase creates, each already handled but worth re-checking:
`core/random.h`'s `<cstddef>`; `spectral_state.h`'s `<type_traits>`; `brownian_drift.h` explicitly
included by **both** new headers; no arithmetic on `std::byte` in the serializer; no narrowing in
brace init.

**No plugin source changed, so pluginval is not run for this phase.**

### T026 — Roadmap / spec amendments and the compliance table

**Files (EDIT):** `specs/Seraphis-roadmap.md`, `specs/seraphis-phase3-spectral-morph/spec.md`;
**CREATE:** `specs/seraphis-phase3-spectral-morph/compliance.md`.

- **RA-1** — the two Phase 2 amendment notes (`deriveSeed` forwarding; the FR-080 injection surface).
- **RA-2** — the Phase 7 line-299 budget tally.
- **RA-3** — **only if** clause 2's measurement triggered it.
- **RA-4** — amend SC-010's prerequisite wording from *"spike-measured before the plan is written"* to
  *"spike-measured and recorded at T022"*, with the machine / build config / trial shape / date from
  T022 written in.
- Plus **lever 4's Phase 2 amendment note if it was spent** (T022).
- Bring the spec into line with the four restated criteria — **SC-012**'s rewind clause (D15),
  **SC-016**'s comparison arm at 0.74 (D17), **SC-009**'s render shape (D16 + the new slow-travel
  clause 3), **SC-008 cl.1**'s scoped max-ratio clause (D6) — and with **D12**'s FR-044 wording
  (`kDriftOutputSmoothSec = 0.150` restated as *"tau = 0.150 s, i.e. 750 ms in `OnePoleSmoother`'s
  time-to-99 % convention"*).
- Mark Phase 3 complete in the roadmap **only after** the compliance table is filled.
- **Compliance table discipline (mandatory):** for each FR, open the implementation file and cite
  file + line. For each SC, run the test and copy the **actual** output; compare against the spec
  threshold with real numbers. No `✅` that was not verified this session. Honest `❌` beats an
  unverified `✅`.

**Verify:** every roadmap/spec edit present; `compliance.md` carries file:line and measured-number
evidence for every FR and SC.

---

## Traceability

| Task | Deliverable | Criteria covered | Target |
|---|---|---|---|
| T001 | `kPreAmendmentFingerprints[216]` | SC-014 cl.1 (enabler) | — |
| T002 | CMake registration + stubs | — | both |
| T003 | `deriveStreamSeed` | FR-006, SC-012 (partly) | `dsp_core_tests` |
| T004 | `centsToPitchRatio` | FR-072, D2 | `dsp_core_tests` |
| T005 | `detail::kLn2` | (enabler for D11) | `dsp_core_tests`, `dsp_systems_tests` |
| T006 | `deriveSeed` forward | FR-006, SC-014 cl.1 | `dsp_systems_tests` |
| T007 | `SpectralState` struct/validity/normalise | FR-011, FR-012, FR-014 | `dsp_processors_tests` |
| T008 | factory states | FR-021–023, SC-008 cl.1/2/4 | `dsp_processors_tests` |
| T009 | serialization | FR-031–033, SC-007 | `dsp_processors_tests` |
| T010 | entropy constants + skeleton | FR-071, FR-074, D11, D12 | `dsp_processors_tests` |
| T011 | OU banks | FR-072, FR-006 | `dsp_processors_tests` |
| T012 | `processChunk` stages 1–3 | FR-071/072/075, SC-005, SC-016 | `dsp_processors_tests` |
| T013 | lifecycle FSM | FR-073, SC-005 (death), SC-006 | `dsp_processors_tests` |
| T014 | engine skeleton + fill | FR-005, FR-041, FR-042, FR-044, SC-002 cl.5, SC-015 (fill/reject) | `dsp_systems_tests` |
| T015 | chunk pipeline | FR-041/046/047/051/052/061–063, SC-002 cl.1–4, SC-003 | `dsp_systems_tests` |
| T016 | continuity/invariance/determinism | SC-001 cl.1, SC-012, SC-013 | `dsp_systems_tests` |
| T017 | metrics + RT safety + extremes | SC-004 m.1–2, SC-011, SC-015 | `dsp_systems_tests` |
| T018 | `HarmonicCloud` FR-080 amendment | FR-081–086, SC-015 (accept set) | `dsp_systems_tests` |
| T019 | SC-014 render | SC-014 cl.1–4 | `dsp_systems_tests` |
| T020 | SC-009 render | SC-009 cl.1–3 | `dsp_systems_tests` |
| T021 | click / flatness / level / metadata | SC-001 cl.2, SC-004 m.3–4, SC-008 cl.3 | `dsp_systems_tests` |
| T022 | perf | SC-010 cl.1–3 | `dsp_systems_tests` `[.perf]` |
| T023–T026 | integration + docs | all | all |

---

## Open items carried from plan §14 (decide before or during T022 / T026)

1. **The SC-010 / RA-3 spike has not been run.** Position taken: its *timing* is amended (RA-4) so
   measurement happens at **T022** with full provenance; §8's ladder pre-decides the response to a
   clause-1 miss. Override only if the spike must precede T007.
2. **Lever 4 is a third amendment to a COMPLETE Phase 2 component.** On the current cost model it is
   likely *required* rather than optional. Pre-authorise it, or should T022 stop and come back for
   approval if the measurement demands it? (Lever 5 touches only this phase's own component and needs
   no approval.)
3. **Deviation D6 changes SC-008 clause 1's scope.** Confirm the scoped form (`i < numPartials`, plus
   a separate all-64 `<= kMaxOutputRatio` + strict-increase clause) is the intended reading, or the
   spec needs the amendment instead.
4. **Four success criteria are restated because a faithful implementation fails them as written** —
   SC-012's rewind clause (D15), SC-016's comparison arm (D17), SC-009's render shape (D16 + new
   clause 3) and SC-008 cl.1's scope (D6). These are corrections of the criteria's own derivations,
   not relaxations; no threshold is loosened.
5. **Deviation D12 changes a constant the spec's FR-044 table depends on** (`kEntropyAmpSmoothMs =
   750`). FR-044's prose should be amended so the derivation and the code agree in writing.
