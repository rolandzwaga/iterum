# Tasks: Seraphis Phase 9 — Full Parameter Surface & State

**Spec:** `specs/seraphis-phase9-parameters/spec.md`
**Plan:** `specs/seraphis-phase9-parameters/plan.md` (authoritative — every task below cites the plan
section that decided it; where a task and the plan disagree, the plan wins)
**Branch:** `feat/seraphis-phase1-life-modulators` (all Seraphis phases share it — do not rename, do not
branch again)
**Status:** TASKS — no implementation
**Date:** 2026-08-01

---

## How to read this file

- Tasks are **T001…**, grouped into **ordered GROUPS**. Groups run in order; a group starts only when
  the previous group is green.
- Within a group, tasks marked **[P]** are parallel-safe — they create **new files only**, disjoint from
  every other `[P]` task in the group. Everything else is sequential and owns its group.
- Every task is **self-contained**: the executor has no other context. Exact files, the failing test
  first (file, `TEST_CASE` name, the assertions with their numbers), then the implementation, then the
  target that verifies it.
- Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
  For C++ header APIs a "failing test" usually fails to *compile* first; that is the intended failure.
- **No commit tasks.** Commits happen outside this workflow.

**Build commands (Windows, always the full CMake path):**

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
# perf / slow cases are excluded by default:
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.slow]" 2>&1 | tail -20
```

**One deliberate ordering note.** CMake test-list registration is a *single* task (**T002**) and it is
placed **early**, not in the final group: CMake cannot configure a source list naming files that do not
exist, so the ten new test TUs are created as compiling skeletons and registered in one edit before any
test is written into them. The final group carries the **registration audit** (T029) — every new TU
present in a list, every `TEST_CASE` discovered by `ctest` — which is the check the plan's R12 asks for.

**Standing constraints (violations are defects in every task).**
RT safety: no allocation/lock/exception/IO on the audio thread. Layer discipline: the four touched
`dsp/` files stay Layer 3; no Layer 4 include. ODR: sweep `grep -rn "class <Name>\|struct <Name>" dsp/
plugins/` before any new type. No `std::isnan` / `std::isinf` / `std::numeric_limits<>::infinity()`
anywhere new — bit-pattern finiteness only. No narrowing in brace init; designated initializers
everywhere. No bit-exact float goldens. Zero compiler warnings.

---

## GROUP 1 — Spec amendments (no code)

### T001 — Apply amendments A1–A8 to `spec.md`

**Files (edit):** `specs/seraphis-phase9-parameters/spec.md`

**Why first:** every later task's tests are written *from the spec*. A1 and A3 in particular change what
SC-007's class-(b) rows assert; a test written from the unamended text would fail a plan-conformant
implementation. Plan §12.1 forbids logging these as deviations — they are spec edits.

**Do (plan §12.1, verbatim intent):**

1. **A1** — FR-042 amendment 1 (`spec.md:1179-1183`) and its restatement (`:1389-1395`): replace
   "once per block … per block, never per slice" with the **absolute 64-sample control-chunk grid**
   wording: the push owning a class-(b) row runs on `SeraphisEngine::kControlChunkSamples` boundaries;
   `process()` caps its slice length at the distance to the next grid boundary **while and only while**
   any class-(b) smoother is un-settled, and stops as soon as all are settled; the grid is absolute
   across slices and `process()` calls; a per-**slice** ramp stays forbidden. Record the rationale
   (a once-per-block push delivers 93.0 % of the step at 512 samples, 99.99 % at 2048).
2. **A2** — FR-059(b) clause 1: replace *"exactly the shape `masterGain_` already uses"* with
   *"advanced by each sub-slice's own sample count and delivered on the absolute 64-sample
   control-chunk grid of A1"*.
3. **A3** — SC-007 class-(b) rows (`:1886-1898`): split `N` into **`N_chunk = 28`** (push-count rows)
   and **`N_block = 4`** (render-length columns of SC-003).
4. **A4** — FR-047 (`:1230-1232`) and FR-091 (`:1450-1455`): `setState()` raises a **single
   release-store request** consumed at the top of `process()` **before `pushGlobalParams()`**;
   `setupProcessing()` calls the shared helper directly with the audio thread stopped; add the
   `SurfaceInvalidation { Reprepared, PresetLoad }` scope, with seed/polyphony sentinels raised on the
   **Reprepared** path only.
5. **A5** — FR-041b clause 5 (`:1154-1156`): `getState()` serializes from the **published staging
   buffer** while a handoff is outstanding and from `factoryStates_[morphParams_.slot[i]]` otherwise;
   it **MUST NOT** read `spectralSlots_`. Add: *"`factoryStates_` is built at construction, not at
   prepare."*
6. **A6** — FR-072 (`:969-999`) gains a **fourteenth** row, `SeraphisEngine::getOutputSaturation()`
   (const forwarder to `TapeSaturator::getSaturation()`, `tape_saturator.h:283-285`, serving ID 2);
   FR-006 (`:891-898`) becomes **six groups, thirty-three public symbols** (1+1+3+1+13+14).
7. **A7** — FR-045 (`:1200-1203`): add *"the four `ENG` push counts MUST be observable through FR-041a
   test-only accessors, one per value"*, plus the matching SC-007 row and a Traceability row for
   FR-045.
8. **A8** — FR-061 (`:1422-1425`): add a Traceability row pointing at
   `Seraphis_ParameterSurface_IsComplete`'s formatting section.

A9 is **conditional** and lands in T028 only if SC-008's worst-case arm breaches 0.50 %.

**Verify:** `spec.md` contains the amended wording at all eight sites; no FR or SC still contradicts plan
§3.5.2, §3.4, §3.7, §1.6 or §7.3. `grep -n "per block, never per slice" specs/seraphis-phase9-parameters/spec.md`
returns nothing.

---

## GROUP 2 — Test-file skeletons + the single CMake registration

### T002 — Create the ten new test TUs as skeletons and register them

**Files (create — skeletons only, no `TEST_CASE`s yet):**

| Path | Target |
|---|---|
| `dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp` | `dsp_systems_tests` |
| `plugins/seraphis/tests/unit/parameter_surface_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/unit/state_v2_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/unit/morph_sync_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/param_reach_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/param_cadence_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/param_continuity_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/macro_wiring_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/param_character_test.cpp` | `seraphis_tests` |
| `plugins/seraphis/tests/integration/param_perf_test.cpp` | `seraphis_tests`, `[.perf]` |

Each skeleton is: a file banner naming the spec criteria it will own (from plan §7.0), the Catch2
include used by its sibling TUs in the same directory (copy the include block from
`plugins/seraphis/tests/unit/param_denorm_test.cpp` for plugin TUs and from
`dsp/tests/unit/systems/seraphis_macro_test.cpp` for the DSP TU), and **nothing else**. A TU with zero
`TEST_CASE`s links and runs fine.

**Files (edit):**

- `plugins/seraphis/tests/CMakeLists.txt` — add the **nine** plugin files to the
  `add_executable(seraphis_tests …)` source list (currently `:5-31`), keeping every existing entry and
  the second compilation of `../src/processor/processor.cpp` / `../src/controller/controller.cpp`
  untouched. Then extend the `-fno-fast-math -fno-finite-math-only`
  `set_source_files_properties` list (`:63-70`) with **exactly two** of the new files:
  `integration/param_continuity_test.cpp` and `unit/state_v2_test.cpp`.
  **`integration/param_perf_test.cpp` must NOT be on that list** — those flags move the figures its
  baselines are pinned to (same rule as `dsp/tests/CMakeLists.txt:735-740`).
- `dsp/tests/CMakeLists.txt` — add `unit/systems/seraphis_param_broadcast_test.cpp` to the
  `add_executable(dsp_systems_tests …)` list beside the five existing Seraphis Phase 7 entries
  (`:355-359`). It must **not** be added to the `-fno-fast-math` list at `:735-740`.

**Verify:** both targets configure and build clean; `dsp_systems_tests.exe` and `seraphis_tests.exe`
both still report all tests passing (nothing new ran yet). Zero warnings.

---

## GROUP 3 — `dsp/` step 1: forwarders and read-back accessors

### T003 — Thirteen `SeraphisVoice` forwarders + fourteenth-minus-two accessors on `SeraphisVoice`/`ContinuousBody`

**Files (edit):** `dsp/include/krate/dsp/systems/seraphis_voice.h`,
`dsp/include/krate/dsp/systems/continuous_body.h`,
`dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp`

**Test first** — add to `seraphis_param_broadcast_test.cpp`:

1. `TEST_CASE("SeraphisVoice_Phase9Forwarders_AreOneToOne")` — prepare a `SeraphisVoice` at 48 kHz.
   Thirteen sections, one per forwarder: push a value distinct from the default, assert **only** that
   forwarder's component getter moved and no sibling getter did.
   - `setCloudDriftSmoothness(0.9f)`, `setEnvelopeOffsetSpread(0.75f)` — cloud;
   - `setAtmosDriftSmoothness(0.25f)`, `setAtmosDriftRangeSemitones(7.0f)`,
     `setAtmosJitter(0.9f)`, `setAtmosPositionSeconds(12.0f)`, `setAtmosPositionSpread(0.8f)`,
     `setAtmosPitchSemitones(-7.0f)`, `setAtmosPitchSpread(0.6f)`,
     `setAtmosGrainEnvelope(GrainEnvelopeType::Blackman)` — read back through `AtmosphereEngine`'s six
     **existing** getters (`getJitter` `:803`, `getPositionSeconds` `:811`, `getPositionSpread` `:819`,
     `getPitchSemitones` `:826`, `getPitchSpread` `:833`, `getGrainEnvelope` `:962`);
   - `setWaypointInterval(9.0)` — read back `morph().getWaypointInterval() == 9.0`;
   - `setBodyInputAgcEnabled(false)` → `body().isInputAgcEnabled() == false`;
   - `setBodyResonatorBypass(true)` → `body().isResonatorBypass() == true` **on the first control step**
     — the *requested* state, not the ramp position (`bypassPos_` ramps over 10 ms; an accessor
     returning it would fail).
2. `TEST_CASE("ContinuousBody_Phase9Accessors_ReturnClampedStoredValues")` — for each of the twelve
   accessors, push in-range, out-of-range (e.g. `setDrive(9.0f)` → `getDrive() == 4.0f`;
   `setResonance(-1.0f)` → `getResonance() == 0.0f`) and non-finite values **built from bit patterns**
   through a `volatile` sink (never `std::numeric_limits`), asserting the setter's clamped store is what
   comes back. One section shows `getDrive()` (pushed user drive) and `getDriveGain()` (smoothed derived
   gain, `:1480-1484`) **differ** while the drive smoother is un-settled.

Both cases fail to compile until the implementation lands. That is the intended first failure.

**Implement (plan §1.5, §1.6):**

- `seraphis_voice.h`, appended to the existing forwarder block (`:637-693`), each a **single delegation
  with no guard of its own** (the block's "no one-to-one forwarders, no added clamping" banner holds):
  `setCloudDriftSmoothness`, `setEnvelopeOffsetSpread`, `setAtmosDriftSmoothness`,
  `setAtmosDriftRangeSemitones`, `setWaypointInterval(double)`, `setAtmosJitter`,
  `setAtmosPositionSeconds`, `setAtmosPositionSpread`, `setAtmosPitchSemitones`, `setAtmosPitchSpread`,
  `setAtmosGrainEnvelope(GrainEnvelopeType)`, `setBodyInputAgcEnabled(bool)`,
  `setBodyResonatorBypass(bool)`. The `setCloud…` / `setAtmos…` prefixes are mandatory — the bare
  `setDriftSmoothness` is ambiguous between `HarmonicCloud` (`harmonic_cloud.h:513`) and
  `AtmosphereEngine` (`atmosphere_engine.h:844`), both of which this facade reaches. Phase 7's existing
  bare `setDriftDepthCents` / `setDriftDepth` are **not** renamed (surgical-changes rule).
- `seraphis_voice.h`, appended to the accessor family (`:763-771`):
  `[[nodiscard]] const GrowthEnvelope& growth() const noexcept { return growth_; }` — the sixth
  sub-component accessor, the only route to `GrowthEnvelope::getDuration()` (`growth_envelope.h:149`)
  for ID 701.
- `continuous_body.h`, appended to the FR-007 introspection block (banner `:1445-1447`, updated in the
  same edit to cover the extended list): the **twelve** `[[nodiscard]] … const noexcept` pure member
  reads `getResonance`, `getDamping`, `getKeyTracking`, `getDrive`, `getMix`, `getCloudMix`,
  `getCloudDecaySec`, `getCloudSize`, `getCloudDamping`, `getWidth`, `isInputAgcEnabled`,
  `isResonatorBypass`. `getDrive()` returns `userDrive_` and is deliberately distinct from the existing
  `getDriveGain()`. `is`-prefixed names match the class's existing `isCrossfading()`.
  **The `continuous_body.h` carve-out is `const`-only** — no non-const behaviour, no member moves, no
  include changes.
- Each of the two groups carries the `@par Layer: 3 (systems/)` and `@par Real-Time Safety:` banners its
  siblings already use (FR-006).

**Verify:** `dsp_systems_tests` builds with zero warnings and runs **in full** green
(`continuous_body.h` is consumed by other cases in that exe). Both new cases pass.

---

## GROUP 4 — `dsp/` step 2: the voice-parameter broadcast

### T004 — `SeraphisVoiceParams`, `applyVoiceParams`, `applySpectralStates`, `getOutputSaturation`

**Files (edit):** `dsp/include/krate/dsp/systems/seraphis_engine.h`,
`dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp`

**Test first** — add four cases to `seraphis_param_broadcast_test.cpp`:

1. `TEST_CASE("SeraphisVoiceParams_DefaultsMatchPreparedVoice")` — a default-constructed POD's **37**
   fields equal what `SeraphisVoice::prepare()` step 6/7 installs (`seraphis_voice.h:284-364`) for the
   29 prepare-set fields, and the **component member initializers** for the eight 2026-08-01 additions:
   `atmosJitter 0.5`, `atmosPositionSeconds 1.0`, `atmosPositionSpread 0.3`, `atmosPitchSemitones 0.0`,
   `atmosPitchSpread 0.15`, `atmosGrainEnvelope Hann` (`atmosphere_engine.h:2292`, `:2352-2356`),
   `bodyInputAgc true`, `bodyResonatorBypass false` (`continuous_body.h:163-164`). Read back through the
   T003 accessors and `AtmosphereEngine`'s six existing getters. Also assert
   `SeraphisVoiceParams::kFieldCount == 37`.
2. `TEST_CASE("SeraphisVoiceParams_MapsEveryFieldToItsOwnSetter")` — **37 sections**; each pushes one
   field off its default, calls `applyVoiceParams`, and asserts **only that field's** read-back moved.
   Two pairs are pinned explicitly because they are easy to invert: `cloudDecaySec` → cloud
   `setDecayTimeSec` (ID 209) vs `bodyCloudDecaySec` → body `setCloudDecaySec` (ID 807); `bodyWidth` →
   body `setWidth` (ID 810) vs the voice width base `setVoiceWidthBasePercent` (ID 604), which is **not**
   a POD field.
3. `TEST_CASE("SeraphisEngine_ApplyVoiceParams_ReachesAllSixteenSlots")` — polyphony **8**, push a
   fully non-default POD, then assert the read-back on **every `i < SeraphisEngine::kMaxVoices` (16)**.
   Second section: shrink polyphony to 4 with a note still ringing, push, assert slots 4–15 also took
   it (the orphan-tail / newly-handed-out-slot hazard the `kMaxVoices` bound exists for).
4. `TEST_CASE("SeraphisEngine_ApplySpectralStates_WritesAllFourSlotsToAllSixteenVoices")` — quiescent
   engine, `count = 4`, four distinct `makeFactoryState` results; assert
   `getVoice(i).morph().getStateCount() == 4` for all 16. Section 2: with one voice sounding, that
   voice's `getRejectedConfigureTimeCallCount()` rises by **exactly 5** (one count + four slots) and the
   others still accept. Section 3 (`voiceMask`): the default argument writes all sixteen; a mask of
   `0x0001` moves voice 0's state count and leaves voices 1–15 at their previous count **and** leaves
   their rejection counters unmoved.

**Implement (plan §1.1–1.3, §1.6):**

- Add `#include <type_traits>` to `seraphis_engine.h`'s stdlib block (`:79-86`) — required by the
  `static_assert` below and **not** transitively guaranteed on libc++.
- `struct SeraphisVoiceParams` at namespace scope immediately after `SeraphisEngineConfig` (`:92-97`):
  **37 fields**, one per `VP` row of spec C-6, each with a default member initializer equal to the
  shipped default (the field list, types and per-field citations are plan §1.1's code block — copy it
  exactly, including `morphWaypointSeconds` being `double` and `atmosGrainEnvelope` being
  `Krate::DSP::GrainEnvelopeType`). Add
  `static_assert(std::is_trivially_copyable_v<SeraphisVoiceParams>, …)` and
  `static constexpr std::size_t kFieldCount = 37;`.
  **No field may name a `SeraphisMacroTarget`** — those 27 values travel through `setTargetBase`
  (T005); a second write path would double-apply them.
- `void SeraphisEngine::applyVoiceParams(const SeraphisVoiceParams&) noexcept` in the public section
  beside `setAtmosphereFreeze` (`:551`) — the 37-setter loop over **`v < kMaxVoices`**, verbatim from
  plan §1.2 including the banner that records why the bound is `kMaxVoices` and not `getPolyphony()`.
  It does **not** call `setSpectralState` / `setSpectralStateCount`.
- `void SeraphisEngine::applySpectralStates(const SpectralState* states, int count,
  std::uint16_t voiceMask = 0xFFFFu) noexcept` — plan §1.3 verbatim: null guard, per-voice mask test,
  `setSpectralStateCount(count)` then **all four** slots (not `count` of them). The mask is a
  **defaulted parameter on the same symbol**, not an overload.
- `[[nodiscard]] float SeraphisEngine::getOutputSaturation() const noexcept { return
  satL_.getSaturation(); }` beside `setOutputSaturation` (`:566`) — a **pure const forwarder** to
  `TapeSaturator::getSaturation()` (`tape_saturator.h:283-285`). **No new member** — a second copy of
  the value is explicitly rejected as a divergent source of truth.
- Banners: `@par Layer: 3 (systems/)` + `@par Real-Time Safety:` on every new public symbol.

**Verify:** `dsp_systems_tests` green in full; the four new cases pass; `seraphis_engine.h`'s diff
outside the POD / `applyVoiceParams` / `applySpectralStates` additions is **`const`-only**.

---

## GROUP 5 — `dsp/` step 3: macro base overrides

### T005 — `setTargetBase` / `resetTargetBases` / `getTargetBase` and the one `evaluateAll` line

**Files (edit):** `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`,
`dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp`

**Test first** — add two cases:

1. `TEST_CASE("SeraphisMacroMatrix_TargetBaseOverride_Composes")` —
   `setTargetBase(t, v)` → `getTargetBase(t) == v` for every one of the 27 `SeraphisMacroTarget`
   values; at the FR-060 macro neutral (`dream/bloom/dissolve/entropy = 0`, `gravity = 0.5`) `apply()`
   on a prepared engine writes **exactly `v`** into every voice for the 19 voice-owned targets, and
   `computeAetherTargets()` returns exactly `v` for the 8 aether-owned targets; `resetTargetBases()`
   restores the `kRows` literal for all 27; a **non-finite** argument built from a bit pattern through a
   `volatile` sink leaves the stored base unchanged.
2. `TEST_CASE("SeraphisMacroMatrix_DefaultBases_Unchanged")` (SC-002 clause 4) — a default-constructed
   matrix returns the `kRows` literal from `getTargetBase(t)` for all 27 targets, and `apply()` at the
   FR-060 neutral produces the identical voice state before and after `resetTargetBases()`.

**Implement (plan §1.4):** two new private members
(`std::array<float, kNumTargets> baseOverride_{}`, `std::array<bool, kNumTargets> hasOverride_{}`),
one private `static constexpr float literalBaseFor(SeraphisMacroTarget)` scanning `kRows`, and the
three public methods from plan §1.4's code block — `setTargetBase` rejecting a non-finite argument via
the class's **own** `isFiniteBits` (`:685-689`), never `std::isnan`. `evaluateAll()` changes **exactly
one line** (`:725`): `value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;`.
`apply()`, `computeAetherTargets()` and `contributionOf()` do **not** change shape.
**No headroom rescaling** is added — spec C-1 accepts saturation against a deep extreme.

**Verify:** `dsp_systems_tests` green in full; both new cases pass; `sizeof(SeraphisMacroMatrix)` grows
to ≈155 B (no assertion needed — `static_assert(sizeof(Processor) < 64 KiB)` at `processor.h:104` is the
gate and is checked in T019).

---

## GROUP 6 — Plugin IDs

### T006 — `plugin_ids.h`: 83 new IDs, range-dispatch bounds, state version symbols, frozen-type note

**Files (edit):** `plugins/seraphis/src/plugin_ids.h`

**Test first:** none of its own — this task is a declaration-only edit whose gate is T009's
`Seraphis_ParameterSurface_IsComplete`. Its *immediate* verification is that the header compiles and
the four `static_assert`s below hold.

**Implement (plan §2.1):**

(a) Rewrite the reserved-range comment (`:46-55`): bands 200–1399 marked **SHIPPED (Phase 9)**, 1400+
left as Phase 10, and the stale pre-Phase-9 roadmap citation corrected to the real spans — one line for
the "start at 0 with 100-ID gaps" decision plus three for the band list. (T031 re-verified these numbers
after the roadmap edit shifted them: **line 396** and **lines 399–401** as applied 2026-08-01.)

(b) `enum ParameterIDs` gains the **83** new IDs in band order, exactly as spec C-6 lists them. The
eight Phase 8 IDs (`:59-68`) are untouched. The bands:
`200-210` cloud (11) · `400-412` morph (13) · `600-604` + `700-704` life/envelope (10) ·
`800-812` body (13) · `1000-1016` atmosphere (17) · `1200-1217` aether (18) · `3` = `kSeedId` (1).

(c) Range-dispatch bounds, extending the two that exist (`:79-80`):

```cpp
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd  =  100;  // existing
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd   =  200;  // existing
constexpr Steinberg::Vst::ParamID kCloudParamRangeEnd   =  400;
constexpr Steinberg::Vst::ParamID kMorphParamRangeEnd   =  600;
constexpr Steinberg::Vst::ParamID kLifeModParamRangeEnd =  800;  // 600-799: life mods + envelope
constexpr Steinberg::Vst::ParamID kBodyParamRangeEnd    = 1000;
constexpr Steinberg::Vst::ParamID kAtmosParamRangeEnd   = 1200;
constexpr Steinberg::Vst::ParamID kAetherParamRangeEnd  = 1400;
```

The Life-Modulator band carries **two** sub-blocks but **one** pack and **one** range-end constant.

(d) State-version symbols, so the migration is not written against literals:

```cpp
constexpr Steinberg::int32 kStateVersion1       = 1;  // Phase 8's 36-byte layout
constexpr Steinberg::int32 kCurrentStateVersion = 2;  // Phase 9 (spec C-8), 2532 bytes
```

(e) Extend the frozen-type note (`:71-76`) to enumerate the registered type of **all 91** IDs grouped by
type: `R` plain `Vst::Parameter`, `L` `StringListParameter`, `T` stepped toggle (`stepCount = 1`).
Name the two new `T` toggles (811, 812) and the **ten** new `L` dropdowns (3, 403, 406, 408, 409, 410,
411, 412, 800, 1016) explicitly — a `RangeParameter ↔ StringListParameter` swap at a live ID breaks editor
load in DAWs that cache parameter metadata.

**Verify:** `seraphis_tests` builds with zero warnings (the header is compiled by the second
compilation of `processor.cpp` / `controller.cpp`). No ID appears twice; every new ID lies inside its
band.

---

## GROUP 7 — Dropdown tables

### T007 — `dropdown_mappings.h` (eight label tables + index↔enum converters)

**Files (create):** `plugins/seraphis/src/parameters/dropdown_mappings.h`

**ODR sweep before writing:** `grep -rn "dropdown_mappings" plugins/` — three plugins already carry a
file of this name (gradus, iterum, ruinae), each in its own namespace. A fourth in `namespace Seraphis`
collides with none. No new *type* is introduced (only `inline constexpr` tables and `inline` functions).

**Test first:** none of its own; the `static_assert`s below are the immediate gate, and T009's
formatting section is the behavioural one.

**Implement (plan §2.2, spec C-7 and C-10):** eight tables, **each read by BOTH registration and
formatting** so a label list cannot exist in two places and drift:

| Table | Entries | Consumer |
|---|---|---|
| `kSeedLabels` / `kSeedValues` | 16 | ID 3 |
| `kTravelModeLabels` | 2 — `External`, `Spline` | ID 403 |
| `kSyncNoteLabels` / `kSyncNoteBeats` / `kSyncNoteIsBarDenominated` | 8 | ID 406 + FR-056 |
| `kStateCountLabels` | 3 — `2`, `3`, `4` | ID 408 |
| `kSpectralStateLabels` | 5 — `Sine Stack`, `Bell`, `Choir`, `Glass`, `Breath` | IDs 409–412 |
| `kEnvelopeModeLabels` | 2 — `Standard`, `Growth` | ID 700 |
| `kBodyMaterialLabels` | 5 — `Glass`, `Strings`, `Metal Plate`, `Chamber`, `Ice` | ID 800 |
| `kGrainEnvelopeLabels` | 6 — `Hann`, `Trapezoid`, `Sine`, `Blackman`, `Linear`, `Exponential` | ID 1016 |

Sync-note table (spec C-7, the single transcription — FR-056 may **not** re-derive it):
index 0 `1/16` 0.25 beats · 1 `1/8` 0.5 · 2 `1/4` 1.0 · 3 `1/2` 2.0 · 4 `1 Bar` **1 × barBeats**
(default) · 5 `2 Bars` 2 × · 6 `4 Bars` 4 × · 7 `8 Bars` 8 ×. `kSyncNoteBeats` is
`std::array<double, 8>` holding the *bar multiple* for indices 4–7, with
`kSyncNoteIsBarDenominated` marking them.

Seed table — **C-10's curated, checked-in constants, NOT `index + 1`**, copied verbatim from plan §2.2
with `kSeedValues[0] = 1u` pinned. Labels are **ordinal** (`"Seed 1" … "Seed 16"`), never the raw
constants.

Mandatory `static_assert`s:
`kGrainEnvelopeLabels.size() == Krate::DSP::AtmosphereEngine::kEnvelopeTypeCount` (6);
`kSeedValues[0] == 1u`; `kSeedLabels.size() == kSeedValues.size()`;
`kBodyMaterialLabels.size() == Krate::DSP::ContinuousBody::kNumMaterials` (5).

Converters are plain `inline` functions with a bounds clamp, one pair per enum-backed table
(`toBodyMaterial` / `fromBodyMaterial`, `toTravelMode` / `fromTravelMode`, `toGrainEnvelopeType` /
`fromGrainEnvelopeType`, `toSpectralStateId` / `fromSpectralStateId`, `toEnvelopeMode` /
`fromEnvelopeMode`), in the shape of plan §2.2's `toBodyMaterial` example.

> **The sixteen seed constants are a starting table, not a result.** T027 measures the pairwise spread
> and **re-picks** any constant that is too close, then re-measures. Lowering SC-020's gate is not an
> available remedy.

**Verify:** `seraphis_tests` builds clean once a TU includes the header (T010–T015 do).
`node tools/check-portability.js` clean on the new file.

---

## GROUP 8 — Global pack: the seed

### T008 — Extend `global_params.h` with `kSeedId` and the separately-positioned seed state trio

**Files (edit):** `plugins/seraphis/src/parameters/global_params.h`

**Test first:** covered by T009's `Seraphis_ParameterSurface_IsComplete` (ID 3 registered, `L`,
`stepCount == 15`, default index 0) and `Seraphis_RegisteredDefaults_AreExact`; the state half is
covered by T020's SC-010/SC-011. Write no new test file here.

**Implement (plan §2.3, §5.2):**

- `GlobalParams` gains **one** field: `std::atomic<int> seedIndex{0}`.
- `handleGlobalParamChange` gains the `kSeedId` case: `clamp(int(v * 15.0 + 0.5), 0, 15)`.
- `registerGlobalParams` registers ID 3 through
  `Krate::Plugins::createDropdownParameterWithDefault` with `kSeedLabels` (pointer+count overload,
  `parameter_helpers.h:118`), default index **0**.
- `formatGlobalParam` **must not claim ID 3** — it is a `StringListParameter` and formats itself
  (FR-061).
- **`saveGlobalParams` / `loadGlobalParams` / `loadGlobalParamsToController` keep their Phase 8
  three-field shape unchanged** (`float | int32 | int32`). The seed is carried by a **separate,
  explicitly-positioned trio in the same header**, written **after** the `[macro]` block (plan §5.2 —
  a fourth field inside `[global]` would eat `dream`'s bytes on every v1 stream, a shape divergence
  FR-093's EOF-safety cannot detect):

```cpp
inline void saveGlobalSeed(const GlobalParams&, Steinberg::IBStreamer&);
inline bool loadGlobalSeed(GlobalParams&, Steinberg::IBStreamer&);            // EOF-safe
template <typename SetParamFunc>
inline void loadGlobalSeedToController(Steinberg::IBStreamer&, SetParamFunc);
```

Neither `loadGlobalParams` nor its controller twin gains a version parameter.

**Verify:** `seraphis_tests` builds clean; existing Phase 8 state tests still pass (the v1 stream shape
is unchanged by this task).

---

## GROUP 9 — The surface test, written before the packs

### T009 — `unit/parameter_surface_test.cpp`: SC-001, SC-014, SC-015, SC-022 (all four fail on arrival)

**Files (edit):** `plugins/seraphis/tests/unit/parameter_surface_test.cpp` (the T002 skeleton)

These are pure table tests — no render, no engine. They are written **now** so the packs, the controller
and the uidesc are implemented against a red test.

**Cases (plan §7.3):**

1. `TEST_CASE("Seraphis_ParameterSurface_IsComplete")` (SC-001) — `Controller::initialize(nullptr)`,
   then `getParameterCount() == 91`; iterate `getParameterInfo(i)` and assert the ID set equals spec
   C-6's **exactly** (no duplicate, none outside the reserved bands); each `stepCount` matches the
   *Type* column — `0` for `R`, `1` for `T`, `n-1` for an `n`-entry `L`.
   **Plus a `getParamStringByValue` section (FR-061):**
   - for **every `L` ID** — enumerated: **3, 403, 406, 408, 409, 410, 411, 412, 800, 1016 = ten** — at
     **every** index `i`, `getParamStringByValue(id, i / double(n-1), buf)` returns `kResultOk` and
     `buf` equals the `dropdown_mappings.h` label at `i`, i.e. the single table both registration and
     formatting read, so a label list existing in two places fails here.
     *(Plan §7.3 and §2.1(e) both write "nine" over a list of ten — 409–412 is four IDs sharing one
     label table, not one ID. Ten is the count; assert over ten.)*
   - for a sample of `R` IDs spanning all six new packs — **200, 400, 601, 804, 1003, 1207** — at
     normalized 0.0, 0.5 and 1.0, the returned string is **non-empty**;
   - for **every** dropdown ID, each of the six `format<Section>Param` functions, called directly,
     returns something **other than** `kResultOk` — the only way to see that no formatter claimed the ID
     before `EditControllerEx1` could format it.
2. `TEST_CASE("Seraphis_Phase8Parameters_AreFrozen")` (SC-014) — for each of the eight Phase 8 IDs
   (0, 1, 2, 100, 101, 102, 103, 104), compare `getParameterInfo` field-by-field (`id`, `stepCount`,
   `defaultNormalizedValue`, `units`, `flags`) against a **checked-in table of the eight infos**.
3. `TEST_CASE("Seraphis_UidescControlTags_MatchRegisteredIds")` (SC-015) — parse
   `SERAPHIS_RESOURCES_DIR "/editor.uidesc"` with `Krate::Test::extractControlTagMap`
   (`tests/test_helpers/uidesc_reachability.h:43`) and assert **set equality** of tag values with the 91
   registered IDs **in both directions** (no missing tag, no orphan tag); then assert the eight existing
   `<view>` elements still bind and each bound view's class still matches C-6's type
   (`CSlider` / `COptionMenu` / `CCheckBox`).
   **Do not use `Krate::Test::unreachableParams`** — Phase 9 adds 83 tags with no view on purpose and
   that helper would report all 83 as unreachable.
4. `TEST_CASE("Seraphis_RegisteredDefaults_AreExact")` (SC-022) — for **each** of the 91 IDs, feed
   `getParameterInfo(i).defaultNormalizedValue` through that ID's own
   `handle<Section>ParamChange(params, id, value)` and compare the stored plain value with **exact float
   `==`, no tolerance**, against a checked-in table of C-6's *Default* column. Rows for **811, 812 and
   1011–1016** carry the **component member initializer** values (`continuous_body.h:163-164`;
   `atmosphere_engine.h:798`, `:805-806`, `:813-814`, `:820`, `:828-829`, `:952`), **not**
   `SeraphisVoice::prepare()`'s — `prepare()` touches none of the eight.

**Verify:** `seraphis_tests` builds clean; **all four cases FAIL** (91 vs 8 registered, tags missing).
Record the failure — a case that passes here means it asserts nothing.

---

## GROUP 10 — The six parameter packs (parallel: six disjoint new files)

All six follow the **six-function contract** the Phase 8 packs already implement
(`global_params.h:72, 102, 129, 160, 167, 189`):

```
void handle<Section>ParamChange(<Section>Params&, Vst::ParamID, Vst::ParamValue);
void register<Section>Params(Vst::ParameterContainer&);
Steinberg::tresult format<Section>Param(Vst::ParamID, Vst::ParamValue, Vst::String128);
void save<Section>Params(const <Section>Params&, Steinberg::IBStreamer&);
bool load<Section>Params(<Section>Params&, Steinberg::IBStreamer&);            // EOF-safe
template <typename SetParamFunc>
void load<Section>ParamsToController(Steinberg::IBStreamer&, SetParamFunc);
```

**Rules that bind every one of T010–T015 (plan §2.3.1–2.3.4):**

- **Exactly four denormalization forms, no pack invents a fifth:**
  `lin [a,b]` → `clamp(a + v*(b-a), a, b)` / inverse `(plain - a) / (b - a)`;
  `log [mn,mx]` → `Krate::Plugins::logMapFromNormalized(v, mn, mx)` (`parameter_helpers.h:80`) /
  `logMapToNormalized` (`:85`); `L` (n entries) → `clamp(int(v*(n-1) + 0.5), 0, n-1)` /
  `index / double(n-1)`; `T` → `v >= 0.5` / `on ? 1.0 : 0.0`.
- **FR-018:** every `handle…` clamps into the C-6 plain range **before** storing. An unclamped store
  makes the FR-042 change detector fire forever.
- **Every `mn` / `mx` bound is `static_cast<double>(<the DSP constant>)`, never a re-typed literal**
  (e.g. ID 404's `mn` is `static_cast<double>(SpectralMorphEngine::kMinTravelRate)`, not `1.0/600.0`).
- **Every registered `defaultNormalizedValue` is computed from the plain default through the same
  mapping**, never hand-typed (`logMapToNormalized(0.5, kMinDecaySec, kMaxDecaySec)` for ID 209, not
  `0.3247`). This is what makes SC-022's exact `==` achievable.
- Every `L` parameter is registered through `createDropdownParameterWithDefault` reading the
  `dropdown_mappings.h` table (never a hand-rolled `StringListParameter`, never a `RangeParameter` with
  a step count), and **no `format…Param` may claim a dropdown ID**.
- **No pack reads the sample rate.** Times stay in seconds / ms / Hz / semitones / grains-per-second;
  conversion to samples happens inside the DSP component that owns the rate. Mechanically checked later
  by `grep -n "sampleRate\|sampleRate_\|getSampleRate" plugins/seraphis/src/parameters/*.h` returning
  **empty**.
- Every pack's `save`/`load` field order is exactly plan §5.1's block for that section.

### T010 [P] — `cloud_params.h` (IDs 200–210)

**Files (create):** `plugins/seraphis/src/parameters/cloud_params.h`
**ODR sweep:** `grep -rn "struct CloudParams\|class CloudParams" dsp/ plugins/` → must be 0 hits.

`struct CloudParams` — **11 float atomics**. Rows (C-6): 200 richness `lin 0..1` d`0.60`;
201 inharmonicity `lin 0..0.1` d`0.030`; 202 tilt `lin -12..+12` d`0.0`; 203 mutation `lin 0..1`
d`0.25`; 204 gravity `lin -1..+1` d`0.20`; 205 drift depth `lin 0..50` cents d`0.0`;
206 drift smoothness `lin 0..1` d`0.5`; 207 stereo spread `lin 0..1` d`0.35`;
208 attack **`log`** `0.05..30 s` d`0.05`; 209 decay **`log`** `0.05..60 s` d`0.5`;
210 env offset spread `lin 0..1` d`0.0`. Bounds come from `HarmonicCloud::kMaxInharmonicity`,
`kMinTiltDbPerOct`/`kMaxTiltDbPerOct`, `kMaxDriftCents`, `kMinAttackSec`/`kMaxAttackSec`,
`kMinDecaySec`/`kMaxDecaySec`. State block: **11 floats, 44 B**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

### T011 [P] — `morph_params.h` (IDs 400–412) — includes the NAMED contract exception

**Files (create):** `plugins/seraphis/src/parameters/morph_params.h`
**ODR sweep:** `grep -rn "struct MorphParams\|class MorphParams" dsp/ plugins/` → 0 hits.

`struct MorphParams` — **5 float + 4 int + 4 int** atomics. Rows: 400 entropy `lin 0..1` d`0.20`;
401 bloom `lin 0..0.6` (`SpectralMorphEngine::kMaxBloomFraction`) d`0.0`; 402 position `lin 0..3`
d`0.0`; 403 travel mode `L` 2 d`External`; 404 travel rate **`log`**
`kMinTravelRate..kMaxTravelRate` d`kMinTravelRate`; 405 sync `T` d`off`; 406 sync note `L` 8 d index
**4** (`1 Bar`); 407 waypoint interval **`log`** `0.5..30 s` (`SplineTrajectory::kMinInterval` /
`kMaxInterval`) d`2.0`; 408 state count `L` 3 d index 0 (= 2); 409–412 factory state `L` 5, defaults
**SineStack, Glass, SineStack, SineStack**.

**The two loaders are a NAMED exception to the six-function contract (plan §2.3.0) — both must be
implemented, and the controller one is load-bearing:**

```cpp
/// PROCESSOR side. THIRD PARAMETER: the four 541-byte payloads land in a
/// Processor-owned staging buffer, never in MorphParams (FR-041b forbids a
/// SpectralState inside an atomic pack).
bool loadMorphParams(MorphParams&, Steinberg::IBStreamer&,
                     std::array<Krate::DSP::SpectralState, 4>& destination);

/// CONTROLLER side. The signature IS the contract's, and the body MUST STILL
/// CONSUME the 4 x 541 = 2164 payload bytes into a 541-byte scratch and DISCARD
/// them - the controller has nowhere to put a SpectralState. A loader that stops
/// after the 13 scalars leaves the cursor 2164 bytes short and the following
/// [life]/[body]/[atmos]/[aether] blocks - 55 parameters - read garbage.
template <typename SetParamFunc>
void loadMorphParamsToController(Steinberg::IBStreamer&, SetParamFunc);
```

The payload encode/decode itself is plan §5.4 and lands with T020; this task provides the loader shape
and the discard loop. State block: **5 floats (20 B) + 4 int32 (16 B) + 4 int32 slot ids (16 B) +
4 × 541 B (2164 B)**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

### T012 [P] — `life_mod_params.h` (IDs 600–604, 700–704)

**Files (create):** `plugins/seraphis/src/parameters/life_mod_params.h`
**ODR sweep:** `grep -rn "struct LifeModParams\|class LifeModParams" dsp/ plugins/` → 0 hits.

`struct LifeModParams` — **9 float + 1 int** atomics. Rows: 600 spatial depth `lin 0..1` d`0.35`;
601 spatial rate **`log`** `0.01..0.5 Hz` (`OrbitModulator::kMinRate`/`kMaxRate`) d`0.1`;
602 coupling `lin 0..1` d`0.0`; 603 growth `lin -1..+1` d`0.0`; 604 voice width `lin 50..150 %`
d`100`; 700 envelope mode `L` 2 d`Standard`; 701 growth duration **`log`** `1..60 s`
(`GrowthEnvelope::kMinDuration`/`kMaxDuration`) d`10.0`; 702/703/704 stage0 / stage1 / release
**`log`** **`1 … 10000 ms`** d`2000` / `4000` / `8000`.

> **The 1 ms floor on 702–704 is load-bearing, not cosmetic.** `logMapFromNormalized` is
> `clamp(mn * pow(mx/mn, u), mn, mx)`; at `mn == 0` the ratio is `+inf`, `pow(+inf, u)` is `+inf`, and
> `0 * inf` is **NaN**, which `std::clamp` propagates. All three are `MB`-routed, so FR-003's
> `isFiniteBits` rejection would silently keep the `kRows` literal and leave three parameters
> permanently inert. `MultiStageEnvelope::setStageTime` clamps to `[0, kMaxStageTimeMs]`, so 1 ms is
> inaudible at the DSP end.

State block: **9 floats (36 B) + 1 int32 (4 B)**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

### T013 [P] — `body_params.h` (IDs 800–812)

**Files (create):** `plugins/seraphis/src/parameters/body_params.h`
**ODR sweep:** `grep -rn "struct BodyParams\|class BodyParams" dsp/ plugins/` → 0 hits.

`struct BodyParams` — **10 float + 1 int + 2 bool** atomics. Rows: 800 material `L` 5 d`Glass`;
801 resonance `lin 0..1` d`0.7`; 802 damping `lin 0..1` d`0.25`; 803 key tracking `lin 0..1` d`1.0`;
804 drive `lin 0..4` d`1.0`; 805 mix `lin 0..1` d`1.0`; 806 cloud mix `lin 0..1` d`0.25`;
807 cloud decay **`log`** `0.1..30 s` d`4.0`; 808 cloud size `lin 0..1` d`1.0`; 809 cloud damping
`lin 0..1` d`0.3`; 810 width `lin 0..1` d`1.0`; **811 input AGC `T` d`on`**;
**812 resonator bypass `T` d`off`** (`continuous_body.h:163-164`).
State block: **10 floats (40 B) + 3 int32 (12 B)**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

### T014 [P] — `atmosphere_params.h` (IDs 1000–1016)

**Files (create):** `plugins/seraphis/src/parameters/atmosphere_params.h`
**ODR sweep:** `grep -rn "struct AtmosphereParams\|class AtmosphereParams" dsp/ plugins/` → 0 hits.

`struct AtmosphereParams` — **15 float + 1 int + 1 bool** atomics. Rows: 1000 level `lin 0..2`
(`kMaxLevel`) d`0.5`; 1001 blur `lin 0..1` d`0.0`; 1002 density **`log`** `0.1..20` grains/s
(`kMinDensity`/`kMaxDensity`) d`4.0`; 1003 grain seconds **`log`** `0.05..30 s`
(`kMinGrainSeconds`/`kMaxGrainSeconds`) d`4.0`; 1004 drift depth `lin 0..1` d`0.3`; 1005 pan spread
`lin 0..1` d`0.7`; 1006 decorrelation `lin 0..1` d`0.5`; 1007 freeze mix `lin 0..1` d`0.0`;
1008 freeze `T` d`off`; 1009 drift smoothness `lin 0..1` d`0.7`; 1010 drift range `lin 0..12`
semitones (`kMaxDriftRangeSemitones`) d`2.0`; 1011 jitter `lin 0..1` d`0.5`;
**1012 position `lin 0..30 s` — `lin`, NOT `log`** (its range starts at 0; the `0 * inf` NaN above, and
a non-zero floor would make position 0 unreachable) d`1.0`; 1013 position spread `lin 0..1` d`0.3`;
1014 pitch `lin -24..+24` semitones (`kMaxPitchSemitones`) d`0.0`; 1015 pitch spread `lin 0..1`
d`0.15`; 1016 grain envelope `L` 6 d`Hann`.
State block: **15 floats (60 B) + 2 int32 (8 B)**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

### T015 [P] — `aether_params.h` (IDs 1200–1217)

**Files (create):** `plugins/seraphis/src/parameters/aether_params.h`
**ODR sweep:** `grep -rn "struct AetherParams\|class AetherParams" dsp/ plugins/` → 0 hits.

`struct AetherParams` — **17 float + 1 bool** atomics. Rows: 1200 mix `lin 0..1` d`0.35`; 1201 size
`lin 0..1` d`0.50`; 1202 density `lin 0..1` d`0.70`; 1203 decay **`log`** `0.5..60 s`
(`kDecayMinSeconds`/`kDecayMaxSeconds`) d`4.0`; 1204 freeze `T` d`off`; 1205 dimensionality `lin 0..1`
d`0.35`; 1206 damping `lin 0..1` d`0.40`; 1207 pre-delay `lin 0..200 ms` (`kMaxPreDelayMs`) d`0.0`;
1208 mod depth `lin 0..1` d`0.25`; 1209 mod smoothness `lin 0..1` d`0.60`; 1210 shimmer octave
`lin 0..1` d`0.0`; 1211 shimmer fifth `lin 0..1` d`0.0`; 1212 bloom send `lin 0..1` d`0.0`;
1213 bloom decay `lin 0..1` d`0.50`; 1214 spectral diffusion `lin 0..1` d`0.0`; 1215 size breath depth
`lin 0..1` d`0.20`; 1216 tide depth `lin 0..1` d`0.20`; 1217 width `lin 0..1` d`1.0`.
State block: **17 floats (68 B) + 1 int32 (4 B)**.
**Verify:** `seraphis_tests` builds clean, zero warnings.

---

## GROUP 11 — Controller

### T016 — Registration, formatting and `setComponentState` for the whole surface

**Files (edit):** `plugins/seraphis/src/controller/controller.cpp`
(`controller.h` is **unchanged** — no new interface is implemented)

**Test first:** T009's four cases are already red; this task turns three of them green (SC-015 stays red
until T017).

**Implement (plan §4):** three parallel band-ordered lists and nothing else.

- `initialize()` calls, in band order:
  `registerGlobalParams` (now 4 IDs: 0, 1, 2, 3) · `registerMacroParams` (100–104) ·
  `registerCloudParams` (200–210) · `registerMorphParams` (400–412) · `registerLifeModParams`
  (600–604, 700–704) · `registerBodyParams` (800–812) · `registerAtmosphereParams` (1000–1016) ·
  `registerAetherParams` (1200–1217) ⇒ `getParameterCount() == 91`.
- `getParamStringByValue` delegates to each `format<Section>Param` in band order and **falls through**
  to `EditControllerEx1::getParamStringByValue` so every `StringListParameter` formats itself.
- `setComponentState` keeps the version gate, then calls every `load<Section>ParamsToController`
  **in exactly `getState`'s order** (plan §5.1: global, macro, **seed**, cloud, morph, life, body,
  atmos, aether), each using `setParamNormalized`. Every denormalization's inverse is exercised, not
  approximated — the four forms of GROUP 10 invert exactly.
- **`INoteExpressionController` is NOT added** — unconditionally, per RQ-2.
- **No Phase 8 parameter changes type, ID, default or unit.**

**Verify:** `seraphis_tests` builds clean; `Seraphis_ParameterSurface_IsComplete`,
`Seraphis_Phase8Parameters_AreFrozen` and `Seraphis_RegisteredDefaults_AreExact` **pass**;
`Seraphis_UidescControlTags_MatchRegisteredIds` still fails (83 tags missing); the existing
`unit/controller/editor_lifecycle_test.cpp` still passes.

---

## GROUP 12 — uidesc control tags

### T017 — `editor.uidesc`: a `<control-tag>` for every one of the 91 IDs

**Files (edit):** `plugins/seraphis/resources/editor.uidesc`

**Test first:** T009's `Seraphis_UidescControlTags_MatchRegisteredIds` is already red; this task turns
it green.

**Implement (plan §6):** add a `<control-tag>` entry for each of the 83 new IDs, named after the enum
without the `k` / `Id` affixes (`CloudRichness`, `AetherTideDepth`, `MorphSyncNote`, …). The existing
eight tags (`:15-24`) keep their names **verbatim**.
**No new `<view>` is added** — the 420 × 300 placeholder template (`:25-69`) with its six `CSlider`, one
`COptionMenu` and one `CCheckBox` stays exactly as it is; layout is Phase 11's.

**Verify:** all four cases in `unit/parameter_surface_test.cpp` pass; the editor-lifecycle test still
passes; `seraphis_tests` green in full.

---

## GROUP 13 — The aether push helper

### T018 — `applyAetherParams` free function

**Files (edit):** `plugins/seraphis/src/engine/seraphis_engine_config.h`

**Test first:** exercised by T019's SC-003 `AE` rows (driven through `IParameterChanges`, never by
calling this function directly) and by T028's perf TU (which has no `Processor` behind it).

**Implement (plan §2.4):** a **free function** beside the existing `applyAetherTargets` (`:93-103`),
pushing the **ten non-macro** reverb controls in ID order:
`setDensity` (1202) · `setDecaySeconds` (1203) · `setFreeze` (1204) · `setDimensionality` (1205) ·
`setDamping` (1206) · `setPreDelayMs` (1207) · `setModDepth` (1208) · `setModSmoothness` (1209) ·
`setBloomDecay` (1213) · `setSpectralDiffusion` (1214).
The eight **macro-owned** controls (1200, 1201, 1210, 1211, 1212, 1215, 1216, 1217) belong to
`applyAetherTargets` — the two sets are **disjoint by construction** and must stay so.
`AetherReverb::setSeed` is **not** here (it is `ENG`-routed, pushed from `pushGlobalParams()` in T019).
Carry the `@par Real-Time Safety:` banner recording that of the four setters that bypass `applyControl`,
exactly **two** are reached here (`setFreeze`, a self-guarding latch; `setModSmoothness`, which loops
over 8 channels) — which is why spec C-3 calls this **on change only** and not every slice.

**Verify:** `seraphis_tests` builds clean, zero warnings.

---

## GROUP 14 — Processor: dispatch, routing and the pre-slice push block

### T019 — Members, `markDirty`/`routeOf`, `processParameterChanges`, the pre-slice pushes, `ENG` trackers

**Files (edit):** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/integration/param_reach_test.cpp` (the T002 skeleton)

**Test first** — write `TEST_CASE("Seraphis_EveryParameter_ReachesDsp")` (SC-003), data-driven over an
**83-row table** with columns *precondition · render length · observable · threshold*. The route rows
sum to the 83 new IDs: **37 `VP` + 19 `MB-voice` + 8 `MB-aether` + 5 `CFG` + 10 `AE` + 2 new `ENG`
(3, 1008) + 2 new processor-local (405, 406) = 83**. IDs 1 and 2 are additionally exercised but are not
among the 83.

Blanket rules:

- **`VP` (37)** — no precondition, **1 block**, read back the matching getter through
  `engineForTest()->getVoice(i)` for **every `i < kMaxVoices`**, exact equality with the pushed plain
  value after the component's own clamp.
- **`MB-voice` (19)** — **polyphony pinned to 16 before these rows**, so `apply()`'s own
  `getPolyphony()` bound coincides with `for every i < kMaxVoices`. Primary observable = the voice-side
  read-back after one `renderSlice`; secondary = `macroMatrixForTest().getTargetBase(...)`.
  **An `MB` row may NEVER be gated on `getTargetBase` alone** — that is FR-003's own storage and would
  pass even if `macros_.apply()` were never invoked.
- **`ENG` (3, 1008, plus 1)** — `getSeed()`, `getAtmosphereFreeze()`, `getPolyphony()`, exact, 1 block.
- **`AE` (10) and `MB-aether` (8)** — driven **through the fixture's `IParameterChanges` on the
  `Processor`**, on the spec's per-row render windows, with the spec's per-row observables (echo
  density, T60, onset shift, morph position, L/R correlation). **These eighteen rows may NOT be driven
  by calling `applyAetherParams` / `setTargetBase` directly** — a direct call exercises none of
  `handleAetherParamChange`'s denormalization, the range dispatch, the generation pair or the on-change
  push. Where the rendered observable is weak, add a **secondary**:
  `applyAetherParamsCallCountForTest()` incrementing plus a re-push comparison.
- **`CFG` (5)** — the quiescent-window ordering: push while quiescent, assert acceptance
  (`getRejectedConfigureTimeCallCount()` did not rise on any voice, `spectralStatesPendingForTest()`
  cleared, `morph().getStateCount()` equals the pushed count), **then** note-on, render ≥ 1 s, assert a
  spectral differential **≥ 1 % relative RMS** against the same note-on render at the default slot
  assignment.
- **Two rows deviate on render length:** IDs **801** and **802** are class (b), so they render
  **`N_block = 4` blocks** before the read-back, which is then **exact** (the `wasVoiceClassBSettling_`
  latch of T022 is what makes it exact rather than ~1e-4 short). IDs 1215/1216 already carry ≥ 40 s /
  ≥ 60 s windows.
- **Carve-out rows reproduced verbatim:** 402 (rate = `kMaxTravelRate`, External, ≥ 1.5 s,
  `getTravelPosition()` within `1e-3`); 411/412 (state count 4, rate 1.0, External, position 2.0 / 3.0,
  ≥ 1.7 s / ≥ 2 s); 2 (stages pinned to 1 ms, 8 notes held, 2 s, third+fifth-harmonic energy over the
  settled last second); 404/405/406's sync preconditions.
- **Two thresholds are pinned by measurement in T028**, in the `floor(min observed / 1.05)` shape:
  ID 1202's echo-density factor (spec floor ≥ 1.5 ×) and ID 2's harmonic-energy floor (8 repeats,
  floored below the run-to-run spread). Until then, write them as named constants with a `// MEASURED
  IN STEP 13` marker.

**Implement (plan §3.1, §3.2, §3.3):**

*`processor.h` new members* — the six pack instances by value; `factoryStates_` as
`std::array<SpectralState, 5>` **filled in the CONSTRUCTOR** by `makeFactoryStateTable()` (immutable
thereafter — deferring it to prepare leaves a window in which `getState()` writes four *valid, empty*
payloads); `spectralSlots_[4]`; `spectralSlotsStaging_[3][4]`; `spectralSlotsHandoff_` /
`spectralSlotsConsuming_` as `std::atomic<int>{-1}`; `stagingWriteCursor_`; `spectralStatesPending_`;
`spectralRetryMask_`; the two generation-counter pairs; the `lastPushed*` trackers
(`lastPushedBase_[27]` + one `lastPushedBaseValid_` flag, `lastPushedMacros_`/valid,
`lastPushedSeedIndex_ = -1`, `lastPushedSlotStateId_{-1,-1,-1,-1}`, `lastPushedFreeze_`/valid,
`lastPushedSoftLimitValid_`); `forcePushAllPending_` as `std::atomic<bool>`; `lastSyncedTravelRate_ =
-1.0f`; and the FR-041a **test-only counters** with their `[[nodiscard]] … const noexcept` accessors:
`applyVoiceParamsCallCountForTest`, `applySpectralStatesCallCountForTest` (successes),
`applySpectralStatesAttemptCountForTest`, `applyAetherParamsCallCountForTest`,
`setTargetBasePushCountForTest`, `macroMatrixForTest`, `spectralStatesPendingForTest`,
`spectralSlotForTest`, `spectralHandoffConsumeCountForTest`, and the **four `ENG` counters**
`engSeedPushCountForTest` / `engPolyphonyPushCountForTest` (a named alias of the existing
`setPolyphonyCallCountForTest`) / `engSoftLimitPushCountForTest` / `engFreezePushCountForTest`.
The class-(b) smoothers and their machinery land in T022; the staging publication in T020.
`static_assert(sizeof(Processor) < 64 KiB)` at `:104` must still hold (budget ≈ 12.1 KiB).

*`processParameterChanges`* — extend the existing range ladder, preserving both established behaviours
(the **last point** of each queue via `getPoint(numPoints - 1)`, and an ID outside every band
**ignored**): the eight `else if (id < k…RangeEnd)` arms of plan §3.2, each calling its
`handle<Section>ParamChange` then `markDirty(id)`.

*`markDirty(Vst::ParamID)`* — one private helper owning the route classification in **one** place, over
a `constexpr routeOf(id)` built from spec C-6:
`VP` → `++voiceParamGeneration_`; **`MB` → NO bump** (it is pushed by `pushMacroSurfaces()`, which
change-detects on `lastPushedBase_[]` and never reads the counter — a bump here would run a
37-setter × 16-voice fan-out on every deep `MB` edit); `AE` → `++aetherParamGeneration_`;
`CFG` → `refreshSpectralSlotFromFactory(id)`; `ENG` / `Local` → nothing.

*`refreshSpectralSlotFromFactory(id)`* — reads the pack's already-clamped slot atomic, returns early if
it equals `lastPushedSlotStateId_[slot]`, else copies 540 B out of `factoryStates_` and raises
`spectralStatesPending_` + `spectralRetryMask_ = 0xFFFF`. **It must NOT call `makeFactoryState()`** —
that is ~200 `std::pow`/`std::exp` per changed slot per block inside `processParameterChanges`, a region
no SC-008 arm measures.

*`process()` pre-slice work* — two blocks, **once per `process()` call, never per slice**:
**(A)** the force-push consume, placed **immediately after the not-ready silence path (`:345-350`) and
BEFORE `pushGlobalParams()` (`:358`)** — the position is normative (a consume below `pushGlobalParams()`
leaves the sentinels alive into the next block and re-seeds engine + reverb for an unchanged seed).
The body is `if (forcePushAllPending_.exchange(false, acquire)) pushAllSurfaces(PresetLoad);` — the
helper itself lands in T020; stub it as a declared no-op **only** if T020 does not follow immediately.
**(B)** between `pushGlobalParams()` and the master-gain read: the staging handoff consume (T020),
`updateSyncedTravelRate(data.processContext)` (T023), `pushAetherParamsIfDirty()`,
`pushSpectralStatesIfPending()`, `setParamSmootherTargets()` (T022).

*`pushVoiceParams()` / `pushAetherParamsIfDirty()` / `pushSpectralStatesIfPending()`* — plan §3.3's
three bodies verbatim. `pushSpectralStatesIfPending()` snapshots each voice's
`getRejectedConfigureTimeCallCount()` **before** the call, passes `spectralRetryMask_`, increments
`applySpectralStatesAttempts_`, clears the bit of every voice whose counter did **not** move, and only
when the mask reaches 0 clears `spectralStatesPending_` and increments the success counter.
**The retry is per-voice, never whole-pool** — a whole-pool retry costs ~3840 `std::log2` per block for
the whole of a held note. **The parameter atomics are never touched in response to a rejection.**

*`pushGlobalParams()` extension (FR-045)* — the seed tracker (`seedIndex != lastPushedSeedIndex_` →
`engine_->setSeed(kSeedValues[clampSeedIndex(idx)])` **and** `reverb_->setSeed(same)`) and the freeze
tracker (`!lastPushedFreezeValid_ || freeze != lastPushedFreeze_` → `engine_->setAtmosphereFreeze`),
each incrementing its own counter. Both are on-change only because both setters are documented
configure-time.

*`renderSlice()`'s body is unchanged* — `macros_.apply(*engine_)` and
`applyAetherTargets(*reverb_, macros_.computeAetherTargets())` stay at `:623-624`, **every slice**.

**Verify:** `seraphis_tests` builds with zero warnings; `Seraphis_EveryParameter_ReachesDsp` passes on
all 83 rows plus IDs 1 and 2; every previously green case still passes.

---

## GROUP 15 — State format version 2

### T020 — `pushAllSurfaces`, the three-buffer staging ring, and the 2532-byte v2 stream

**Files (edit):** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/src/parameters/morph_params.h` (payload encode/decode inside the two loaders),
`plugins/seraphis/tests/unit/state_v2_test.cpp` (the T002 skeleton)

**Test first** — four cases in `unit/state_v2_test.cpp`:

1. `TEST_CASE("Seraphis_StateRoundTrip_IsExact")` (SC-010) — for a randomized-but-valid setting of all
   91 parameters (fixed seed), `getState` → `setState` → `getState` produces **byte-identical** streams
   of length **exactly 2532**, and every controller-side value after `setComponentState` equals the
   processor's **within `1e-6` normalized**. Plus one **localized** extra assertion: the controller-side
   stream position immediately after the `[morph]` block equals the processor-side position at the same
   point — the direct check on the 2164-byte discard loop, without which the failure mode is 55
   parameters reading garbage.
2. `TEST_CASE("Seraphis_StateVersion_MigratesAndRefuses")` (SC-011) — a hand-built **36-byte version-1**
   stream loads without error; the eight Phase 8 parameters take their stream values; **all 83 Phase 9
   parameters read back at their registered defaults**; the subsequent 4 s render satisfies **SC-002's
   pass condition using SC-002's construction verbatim** (same-binary, same-TU Arm B from the Phase 8
   shipped defaults, per-sample `maxAbsDiff ≤ 1.0e-5` over both channels, `compareFingerprints`
   warn-only, **no checked-in fingerprint reference**). A **version-3** stream is refused
   (`kResultFalse`) with **no state mutated**. Version-2 streams truncated at **12 chosen byte offsets**
   — deliberately including one inside a 541-byte payload, one at a block boundary, and one byte short
   of the end — load without crash and leave the remainder at defaults.
3. `TEST_CASE("Seraphis_SpectralStateSlots_RoundTripExactly")` (SC-012) — each of the five factory
   states assigned to a slot, saved and reloaded, compares **equal field-by-field** (ratios, amplitudes,
   name, tilt, inharmonicity, numPartials). **The comparison is against `Processor`'s copy through
   `spectralSlotForTest(slot)` — never against the engine slot**, which stores *sanitized* (log2 ratios,
   name/tilt/inharmonicity discarded) and exposes no per-slot getter. A slot fed **541 bytes of
   garbage** deserializes to `false` and leaves `spectralSlots_[slot]` **bitwise unchanged**.
4. `TEST_CASE("Seraphis_PresetLoadAfterPrepare_ReachesDsp")` (SC-023 clauses 1–6) +
   `TEST_CASE("Seraphis_PresetLoadBeforePrepare_ReachesDsp")` (clause 6a) +
   `TEST_CASE("Seraphis_SampleRateChange_RePushesEverySurface")` (clause 7) — full construction in
   plan §7.13. Key numbers: prepare, render **one** block; `setState()` a stream in which **every one
   of the 91 parameters** differs from its registered default (this criterion's **own** table — plan
   §7.13's, i.e. §7.9's table with the **20** overrides listed there); render **one** block; assert
   every route's read-back (37 `VP` via `getVoice(i)` for all 16; 27 `MB` via `getTargetBase` **and**
   the post-slice voice read-back with polyphony pinned to 16; 10 `AE` via
   `applyAetherParamsCallCountForTest()` + re-push comparison, **with ID 1207 additionally asserted by
   a 200 ms onset shift ± one control chunk and ID 1203 by a 60 s T60**; 4 `ENG` via `getPolyphony()`,
   `getAtmosphereFreeze()`, `getSeed()`, **`getOutputSaturation()`**; 5 `CFG` via
   `spectralSlotForTest(slot)` field-by-field and `getVoice(i).morph().getStateCount()`).
   Clause 5: `spectralHandoffConsumeCountForTest()` is **0 immediately after `setState()` returns** and
   **exactly 1 after the clause-3 block**. Clause 6: with `requestPushAllSurfaces()` stubbed out of
   `setState()` (a compile-time test-TU switch), the same assertions **must fail**.
   Clause 6a: `setState()` on a **fresh, unprepared** processor, then `setupProcessing()`, then one
   block — the four payloads and `getStateCount()` hold the **preset's** states, not the defaults.
   Clause 7: prepared at **44 100 Hz**, re-`setupProcessing()` at **96 000 Hz**, one block; assert
   (a) `getVoice(i).body().getEngineSampleCount(e) == 0` for every voice and engine immediately after
   the re-prepare, (b) `AetherReverb::isPrepared()` and `getEffectiveDelayLengthSamples(ch)` changed in
   proportion to the rate ratio on every channel, (c) clause 4 repeated **verbatim**, (d) with
   `pushAllSurfaces()` stubbed out of `setupProcessing()`, (c) **must fail** while (a) and (b) pass.
   Add the **construction check**: the table is a `constexpr` array of `{id, plainValue}` and the TU
   asserts **per row, over all 91 rows**, that `plainValue` differs from that ID's registered default
   (exact `!=` for scalars, index inequality for `L`/`T`).

**Implement (plan §3.4, §3.7, §5):**

- `enum class SurfaceInvalidation { Reprepared, PresetLoad };` and
  `void Processor::pushAllSurfaces(SurfaceInvalidation) noexcept` — **pure invalidation**, plan §3.4's
  body verbatim: bump both generation counters, set both `lastApplied*` to `kGenerationSentinel`, clear
  `lastPushedBaseValid_` / `lastPushedMacrosValid_` / `lastPushedSoftLimitValid_` /
  `lastPushedFreezeValid_`, `lastSyncedTravelRate_ = -1.0f`, `snapParamSmoothers_ = true`,
  `wasVoiceClassBSettling_ = false`, `lastPushedSlotStateId_.fill(-1)`; **Reprepared-only**
  `lastPushedSeedIndex_ = -1` and `lastPushedPolyphony_ = kPolyphonySentinel`; and the spectral raise
  **only when a slot id actually moved or the engine was re-prepared** (capture the four ids before the
  `.fill(-1)`, or run that block first — the ordering is stated so it is not rediscovered).
- `void Processor::requestPushAllSurfaces() noexcept` — a **single release store**; it is the only thing
  `setState()` writes toward the audio thread.
- `setupProcessing()` order: `engine_->prepare(...)`; `reverb_->prepare(...)`;
  `pushAllSurfaces(Reprepared)`; then Phase 8's **one documented exception line**
  `lastPushedPolyphony_ = engine_->getPolyphony();` and the soft-limit re-seed. Also seed
  `spectralSlots_` **from the CURRENT ATOMICS**, not the registered defaults
  (`factoryStates_[clampFactoryIndex(morphParams_.slot[slot].load())]`) and set
  `lastPushedSlotStateId_[slot]` to that id — otherwise a `setState()` that arrived **before**
  `setupProcessing()` is silently overwritten with nothing ever re-deriving it.
- **The three-buffer staging ring** (plan §3.7's five-step table): `setState()` picks `w` = the first of
  `{cursor, cursor+1, cursor+2} mod 3` equal to **neither** `spectralSlotsHandoff_` **nor**
  `spectralSlotsConsuming_`; deserializes into `spectralSlotsStaging_[w]`; publishes with a release
  store. The audio thread, at the top of `process()`, loads `handoff` with **acquire**, stores
  `consuming = idx` **first**, then clears `handoff`, then copies the 2.1 KiB, then clears `consuming`,
  then raises `spectralStatesPending_` / `spectralRetryMask_ = 0xFFFF` and increments
  `spectralHandoffConsumes_`. **That store order is the entire interlock** — do not reorder it.
- **`getState()` never reads `spectralSlots_`.** It serializes from the published staging buffer while a
  handoff is outstanding and from `factoryStates_[clampFactoryIndex(morphParams_.slot[i].load())]`
  otherwise (plan §3.7's code block).
- **The v2 layout, exactly plan §5.1**, write order: version int32 (2) · `[global]` 12 B · `[macro]`
  20 B · **`[seed]` 4 B (after `[macro]`, normative)** · `[cloud]` 44 B · `[morph]` 20 + 16 + 16 +
  2164 B · `[life]` 36 + 4 B · `[body]` 40 + 12 B · `[atmos]` 60 + 8 B · `[aether]` 68 + 4 B =
  **2532 B** (73 floats + 18 int32 + version + 4 × 541).
- **Payload encode/decode (plan §5.4):** write into a 541-byte `std::array<std::byte,
  kSpectralStateBytes>`; if `serializeSpectralState` returns 0, write **541 zero bytes**; on read, a
  short `readRaw` **breaks** (EOF-safe: this and later slots stay factory), and
  `deserializeSpectralState` leaves `out` bitwise untouched on rejection. A slot whose
  `kSpectralStateFormatVersion` differs is refused **per slot** — one bad slot is not a bad preset.
- **Every loader stays EOF-safe** in the Phase 8 sense: a short stream leaves unread fields at their
  registered defaults and returns `false`, which is **not** an error.
- `setState()` control flow is plan §5.5's block, ending in `requestPushAllSurfaces()`.

**Verify:** `seraphis_tests` builds clean; the four new cases pass. **`state_roundtrip_test.cpp` is now
red — T021 is part of the same landing and must run immediately.** Do not claim a green suite until
T021 completes.

### T021 — Migrate the three existing Phase 8 state tests (same landing as T020)

**Files (edit):** `plugins/seraphis/tests/unit/state_roundtrip_test.cpp`,
`plugins/seraphis/tests/integration/param_flow_test.cpp`,
`plugins/seraphis/tests/unit/lifecycle_test.cpp`,
`plugins/seraphis/tests/unit/param_denorm_test.cpp` (comment only)

**Do (plan §7.10 step 8a — exhaustively):**

1. `unit/state_roundtrip_test.cpp` is **kept and migrated, not deleted** (it covers Phase 8's
   fresh-processor default stream, byte stability, truncation ladder and future-version refusal over
   the *scalar prefix*, which `state_v2_test.cpp` does not reproduce):
   - `kStateBytes` **36 → 2532**, and a new `constexpr int32 kV1StateBytes = 36;` beside it, named for
     what it now is: the **strict v1 prefix** of a v2 stream;
   - the helper at `:145` is renamed `decodeV1Prefix` (its banner at `:144` says so) and its
     `REQUIRE(s.getSize() == kStateBytes)` (`:146`) now compares against **2532**;
   - the two other size assertions (`:215`, `:237`) take **2532**;
   - `CHECK(int32AtOffset(*s, 0) == 1)` (`:219`) becomes `== 2`, **kept** beside the symbolic
     `CHECK(int32AtOffset(*s, 0) == Seraphis::kCurrentStateVersion)` (`:218`) — the literal is what
     makes the symbolic assertion non-vacuous;
   - the truncation ladder's final rung `if (n >= kStateBytes)` (`:316`) becomes
     `if (n >= kV1StateBytes)` — `entropy` completes at byte 36, not 2532;
   - the future-version sections (`:330-331` and `:403`) become
     `future.version = Seraphis::kCurrentStateVersion + 1; REQUIRE(future.version == 3);`.
2. `integration/param_flow_test.cpp:211` and `unit/lifecycle_test.cpp:88` write
   **`Seraphis::kStateVersion1`** instead of `kCurrentStateVersion`. Both bodies stay 36-byte v1
   streams; relabelling makes each an honest v1 stream that additionally exercises FR-093's migration.
   Their layout comments (`:186-192`, `:80-84`) gain the words "version 1".
3. `unit/param_denorm_test.cpp` needs **no functional edit** — its `readParams` (`:94-109`) reads the
   first nine fields and asserts nothing about total length. Correct its layout comment (`:72-76`) to
   say it reads the v2 stream's **prefix**. That is the whole diff.
4. **No CMake change** — all four files are already in the `seraphis_tests` list.

**Verify:** **`seraphis_tests` green IN FULL**, not just the new cases.

---

## GROUP 16 — Class-(b) continuity

### T022 — 64-sample-grid class-(b) smoothing, `kContinuityMechanism[]`, the FR-059a probe

**Files (edit):** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/integration/param_continuity_test.cpp` (the T002 skeleton)

**Test first** — `TEST_CASE("Seraphis_ParameterAutomation_IsClickFree")` (SC-005) in
`param_continuity_test.cpp`, which also **owns** the checked-in `kContinuityMechanism[]` array and
**defines** `Seraphis::detail::SeraphisParamSmootherBypassProbe`.

Construction, per in-scope ID: a **2 s** render at 48 kHz / block 512 with the parameter automated
extreme-to-extreme in **64 equal steps**.

1. **Test statistic** — per automation step, `maxPerSampleDelta` over the **±10 ms window centred on
   `step sample + AetherReverb::getLatencySamples()`** (1024 samples = 21.3 ms — more than the whole
   window; without the shift the clause measures the wrong 20 ms).
2. **Reference** — one window per measured step, the **same 20 ms length**, from the **same render**, at
   offsets **≥ 50 ms** clear of any step, uniformly spaced.
3. **Bound** — `max(test) ≤ 1.5 × max(reference)`, with the **same number of draws on both sides**.
4. **Non-finite clause — all 91 IDs, no exemptions** — no sample is non-finite, tested by **bit
   pattern**, never `std::isnan`.

**Both positive controls are mandatory:**
(a) *detector wiring* — the same statistic over a non-step window with a deliberately injected
one-sample step of **2 ×** that window's own `maxPerSampleDelta` must **exceed** the bound;
(b) *criterion wiring* — with the probe setting `paramSmootherBypass_ = true` so a class-(b) smoother
**snaps** instead of ramping, the same render must **FAIL** clause 3. **ID 801 is the subject.** Record
the measured bypassed-vs-smoothed ratio; the design predicts **3.53 ×** (100 % vs 28.35 % per chunk)
against the 1.5 × bound. **A ratio near 1.075 × is the signature of an un-hoisted
`setParamSmootherTargets()` — fix the ordering, do not loosen the bound.**

*Exemptions from clauses 1–3 (clause 4 still applies):* `kSeedId` (3) and the five `CFG` IDs
(408–412). **85 IDs remain in scope.**

*The render set carries **three** edge combinations:* `kAtmosGrainSecondsId = 30 s`;
`kAetherDecayId = 60 s` + `kAetherFreezeId = on`; and **`kBodyResonatorBypassId = on`**, under which
IDs **804** and **811** are re-measured (their unsmoothed `cloudDriveGain()` consumer is multiplied by
`bypassGain`, which is **exactly 0** at ID 812's default, so a one-ID-at-a-time construction
structurally cannot reach it otherwise).

`kContinuityMechanism[]` is the checked-in array of plan §3.5.3 — **one row per in-scope ID** with
`{id, class, evidence, citation}` and a **mandatory file:line citation** on every row. Two gates:
`static_assert(std::size(kContinuityMechanism) == 85, …)` **and** a runtime **set** check that its ID
set equals (registered IDs) − {3} − {408, 409, 410, 411, 412}, asserted in both directions against the
same `getParameterInfo` enumeration T009 uses, with no ID twice.

**Implement (plan §3.5):**

- **Class (b) is exactly nine IDs: 100, 101, 102, 103, 104, 801, 802, 1215, 1216.** All nine have plain
  span `D = 1.0`.
- Nine `Krate::DSP::OnePoleSmoother` members (`resonanceSm_{0.7f}`, `bodyDampingSm_{0.25f}`,
  `breathDepthSm_{0.20f}`, `tideDepthSm_{0.20f}`, `macroSm_[5]` seeded `{0,0,0,0.5,0}`), plus
  `snapParamSmoothers_ = true`, `paramSmootherBypass_ = false`, `controlPhase_` (`std::uint64_t`) and
  `wasVoiceClassBSettling_`.
- `inline constexpr float kParamSmoothMs = 20.0f;` — **one number**, the same 20 ms family as
  `kMasterGainSmoothMs`, `SeraphisEngine::kSumGainSmoothMs` and `ContinuousBody::kPitchSmoothMs`.
  `OnePoleSmoother`'s argument is **time-to-99 %**, so tau = 4 ms; per-chunk delivery
  `1 − e^(−64/192) = 0.2835`; `N_chunk = ceil(36.84 ms / 1.3333 ms) = 28`;
  `N_block = ceil(36.84 / 10.667) = 4`. `configure(kParamSmoothMs, sampleRate_)` for all nine runs in
  `setupProcessing()` beside `masterGain_.configure(...)`.
- **`setParamSmootherTargets()` runs ONCE per `process()`, in the §3.3 pre-slice block, BEFORE the slice
  loop.** It must **not** be called from inside `advanceParamSmoothers()` — the slice loop evaluates
  `anyClassBSmootherUnsettled()` *before* advancing, so a target set inside the advance leaves the
  predicate reading the stale target on the first slice after every change, no subdivision happens, and
  **93.0 %** of the step is delivered in one push. The hoist is valid for the same stated reason the
  master-gain target hoist at `processor.cpp:360-367` is: `processParameterChanges()` took the **last**
  point of every queue, so no atomic can change within a `process()` call.
- Slice loop gains **one rule**: while `anyClassBSmootherUnsettled()`, cap `n` at the distance to the
  next **absolute** 64-sample boundary (`kControlChunkSamples - controlPhase_ % kControlChunkSamples`,
  treating 0 as a full chunk), then `advanceParamSmoothers(n)` **before** the pushes,
  `pushVoiceParams()`, `pushMacroSurfaces()`, `renderSlice(n)`, `controlPhase_ += n`. When every
  class-(b) smoother is settled the structure is **exactly Phase 8's**.
- `advanceParamSmoothers(n)` snaps when `snapParamSmoothers_ || paramSmootherBypass_`, else advances all
  nine by `n`. **`classBSmoothers()` returns `std::array<Krate::DSP::OnePoleSmoother*, 9>` BY VALUE** —
  the type is pinned because this is the hottest new audio-thread path (twice per sub-slice, up to 32
  times per 2048-sample block while settling) and an unstated return type is where a `std::vector`
  creeps in.
- `pushVoiceParams()`'s **third clause is not redundant**: `wasVoiceClassBSettling_` latches the
  settling→settled transition so the **exact** target is pushed once. Without it the converging chunk is
  skipped and the voice is left permanently ~1e-4 short, breaking T019's exact read-back for ID 801.
- `pushMacroSurfaces()` (plan §3.5.5) — the five macro knobs behind `macrosEqual()` (a field-by-field
  compare; `SeraphisMacroValues` has **no** `operator==` and C++20 does not synthesise one, and
  defaulting it would touch a fifth `dsp/` file), then the **27 bases** with a **per-target** settling
  predicate (`targetClassBUnsettled(t)` — `BodyDamping`, `AetherSizeBreathDepth`,
  `AetherDimensionalityTideDepth` only). A single global settling flag is a defect: it would push all
  27 targets per chunk (up to 756 increments) and falsify SC-007's own table. The macro push **owns**
  the macro smoothers and deliberately does **not** increment `setTargetBasePushes_`.
  `baseValueForTarget()` is the **only** place C-6's 27 `MB` rows are mapped.
- The FR-059a probe seam (plan §3.8): declare `namespace detail { struct
  SeraphisParamSmootherBypassProbe; }` above the class and `friend struct` it inside. **The library
  never defines it**; only the SC-005 TU does. ODR sweep: `grep -rn
  "SeraphisParamSmootherBypassProbe" dsp/ plugins/` → 0 hits before this task.

**Verify:** `seraphis_tests` green in full; SC-005 passes **with both positive controls**; the measured
bypassed-vs-smoothed ratio is recorded and is **> 1.5 ×**.

---

## GROUP 17 — Host-synced morph travel

### T023 — `updateSyncedTravelRate` and the SC-018 case

**Files (edit):** `plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/seraphis_test_fixture.h`,
`plugins/seraphis/tests/unit/morph_sync_test.cpp` (the T002 skeleton)

**Test first** — `TEST_CASE("Seraphis_MorphSync_DerivesAndFallsBack")` (SC-018). The observable for
clauses 1–4 is **`engineForTest()->getVoice(0).morph().getTravelRate()`**
(`spectral_morph_engine.h:441`), which returns the pushed rate exactly — **never** an inferred rate
from travel position (at clause 3's 1.0417e-2 journeys/s the position moves ~1.1e-4 per block and the
inference is dominated by quantisation and `advanceTravel`'s slew cap).
`applyVoiceParamsCallCountForTest()` is the **secondary** on every clause (the rate must have been
*pushed*, not merely computed).

1. **Derivation** — 120 BPM, sync on, `1 Bar` (index 4), 4/4 → `120/(60·4) = 0.5` journeys/s within
   `1e-5`.
2. **Upper clamp** — 200 BPM, `1/16` (index 0, 0.25 beats) → 13.33 clamps to
   `kMaxTravelRate = 1.0`.
3. **No clamp at the slow end** — 20 BPM, `8 Bars` (index 7, 32 beats at 4/4) → `20/(60·32) =
   1.0417e-2`, above `kMinTravelRate = 1.667e-3`; the pushed rate equals it within `1e-5` and **no
   clamp engages**.
4. **Time signature** — `kTimeSigValid` set, 6/8 → `barBeats = 6 · (4/8) = 3`, so `1 Bar` at 120 BPM
   gives `120/(60·3) = 0.667`; with the flag clear the same setting falls back to `barBeats = 4` and
   **0.5**.
5. **Fallback** — `processContext == nullptr`, and separately an invalid tempo flag: the free-running
   ID 404 value is used unchanged and the render **does not go silent**.

*Fixture change (plan §7.0):* `seraphis_test_fixture.h` currently sets `data_.processContext = nullptr`
(`:285`). Add an owned `Steinberg::Vst::ProcessContext context_{}`, a `bool useContext_ = false`, and
`setTempo(double bpm, int sigNum, int sigDen, bool tempoValid, bool sigValid)`, so
`withOutputChannels()` can attach it. Everything else in the fixture is reused unchanged.

**Implement (plan §3.6):**

- **Tempo sample point: once per `process()` call**, from `data.processContext`, before the pre-slice
  push block — never per slice.
- `inline constexpr float kSyncedRateEpsilon = Krate::DSP::SpectralMorphEngine::kMinTravelRate *
  1.0e-3f;` (= 1.6667e-6 — 0.1 % of the minimum rate, ~10 × above the division's float noise).
- `updateSyncedTravelRate(const Vst::ProcessContext*)` is plan §3.6's body: sync off → sentinel −1;
  null context / no `kTempoValid` / non-positive tempo → sentinel −1 (**the fallback is the free-running
  ID 404 value — never silence, never zero, never a retained stale synced rate**); else `barBeats` from
  the time signature when `kTimeSigValid` and both fields are strictly positive, else 4.0; `beats =
  kSyncNoteBeats[idx] * (kSyncNoteIsBarDenominated[idx] ? barBeats : 1.0)`; rate = `clamp(tempo /
  (60·beats), kMinTravelRate, kMaxTravelRate)`; and **`++voiceParamGeneration_` only when the rate moved
  by more than `kSyncedRateEpsilon`** (or the sentinel was set).
- `buildVoiceParams()` takes `morphTravelRate = (lastSyncedTravelRate_ >= 0.0f) ? lastSyncedTravelRate_
  : <ID 404's stored value>`.

**Verify:** `seraphis_tests` green in full; SC-018's five clauses pass.

---

## GROUP 18 — Macro wiring and the Phase 8 inertness deletion

### T024 — `MacroParams` → `setMacros`, FR-050 banner rewrite, FR-051 test deletion

**Files (edit):** `plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/src/parameters/macro_params.h`,
`plugins/seraphis/tests/integration/processor_audio_test.cpp` (deletion),
`plugins/seraphis/tests/integration/macro_wiring_test.cpp` (the T002 skeleton)

**Test first** — four cases in `macro_wiring_test.cpp`:

1. `TEST_CASE("Seraphis_Phase9Defaults_MatchPhase8Render")` (SC-002).
   **Arm A** — 4 s render of note 60 through `Processor` at **registered defaults**, 48 kHz, block 512,
   `kSeedId` at index 0.
   **Arm B** — the same 4 s render produced **in the same translation unit and the same binary** by
   configuring a `SeraphisEngine` + `AetherReverb` pair directly with the Phase 8 shipped defaults
   (`makeSeraphisEngineConfig` / `makeSeraphisReverbConfig`) and driving the Phase 8 chain through
   `Krate::DSP::TestUtils::renderSeraphisChain` (`tests/test_helpers/seraphis_chain.h:147`) — **no
   Phase 9 push path engaged at all**.
   *Gate:* per-sample `maxAbsDiff` over **all** samples of **both** channels **≤ 1.0e-5**.
   `compareFingerprints` runs as a **secondary, warn-only** aggregate and **must not gate**; **a
   checked-in fingerprint reference is forbidden.**
2. `TEST_CASE("Seraphis_MacroSweep_MovesItsAxis")` (SC-004 Arm 1, `[.slow]`) — per macro, sweep
   **0 → 1 in 21 steps**, **4 s per step**, fixed seed and note, each non-swept macro at its **own**
   FR-060 neutral (Gravity 0.5, the rest 0). Gate = Phase 7 SC-009 **in full**: primary metric at
   **|ρ| ≥ 0.9** (Spearman **trend**, not monotonicity) on Phase 7's pinned arms (Dream on the dry voice
   sum with Aether mix at neutral; Dissolve's atmosphere-band contribution over the settled last second;
   Gravity's body-decay observable on a dry, isolated-damping, 1–8 kHz arm; Entropy's flatness on the
   cloud-only arm); every secondary at |ρ| ≥ 0.9 in its stated direction; minimum end-to-end effect
   sizes **Dream ≤ 50 %** of its Dream = 0 value, **Bloom centroid +≥ 20 %** relative, **Dissolve
   +≥ 0.15** absolute, **Gravity ≥ 6 dB**, **Entropy +≥ 25 %** relative; no step > **3 ×** the mean step
   change; pinned partial detector (**65 536-point FFT, Blackman-Harris, last 1 s of each step, −60 dB
   peak threshold, 20 dB peak-to-local-median SNR, parabolic interpolation, ordinal grid matching with
   an exact-count gate**).
3. `TEST_CASE("Seraphis_MacroAndDeepParameter_Compose")` (Arm 2, `[.slow]`) — repeat Arm 1 with one deep
   parameter pushed to its headroom-preserving value: **Dream ↔ 201 at 0.060**, **Bloom ↔ 200 at 0.45**,
   **Dissolve ↔ 1000 at 0.30**, **Gravity ↔ 802 at 0.40**, **Entropy ↔ 400 at 0.10**. Gate: |ρ| ≥ 0.9
   with the **same sign** as Arm 1, end-to-end effect size **≥ 50 % of Arm 1's**, and
   `macroMatrixForTest().getTargetBase(target)` equal to the pushed deep value **exactly**.
4. `TEST_CASE("Seraphis_MacroSaturatesAgainstDeepExtreme")` (Arm 3, `[.slow]`) — `kCloudRichnessId`
   pushed to **1.0**; the Bloom sweep's primary metric is asserted **monotone non-decreasing** (largest
   downward step ≤ the detector's own noise floor, measured on a no-op sweep in the same render) and
   `getTargetBase(CloudRichness) == 1.0`. **No effect size is required on this arm** — Bloom's +0.40
   span is entirely consumed by `setRichness`' `[0,1]` clamp, which spec C-1 documents and accepts.

**Implement (plan §11 step 12):**

- `pushMacroSurfaces()`'s macro half (already built in T022) is now fed from `macroParams_` —
  `readSmoothedMacros()` reads `macroSm_[i].getCurrentValue()` for the five knobs and hands a
  `SeraphisMacroValues` to `macros_.setMacros(...)`.
- **FR-050:** rewrite `macro_params.h`'s inertness banner (`:5-9`) to describe the Phase 9 wiring. The
  Phase 8 sentence forbidding any file from reading `MacroParams` into `SeraphisMacroMatrix` is exactly
  what this phase inverts.
- **FR-051:** **delete `SECTION("Seraphis_MacroParametersAreInert")` at
  `plugins/seraphis/tests/integration/processor_audio_test.cpp:856`, together with its `:843-855`
  banner.** It now asserts the opposite of shipped behaviour. Its own banner says "rewrite it to assert
  that the two renders DIFFER" — FR-051 supersedes that, because SC-004 already asserts the differ-case
  with a Spearman-ρ gate over a 21-step sweep and an inverted `fingerprintsMatch` would be the weaker
  duplicate.

**Verify:** `seraphis_tests` green in full; SC-002 passes; the three `[.slow]` SC-004 cases pass
(`seraphis_tests.exe "[.slow]"`); `grep -n "MacroParametersAreInert" plugins/seraphis/tests/` returns
**nothing**.

---

## GROUP 19 — The remaining behavioural test TUs (parallel: three disjoint new files)

### T025 [P] — `integration/param_cadence_test.cpp`: SC-006, SC-007, SC-013

**Files (edit):** `plugins/seraphis/tests/integration/param_cadence_test.cpp` (the T002 skeleton)

Implementation-free — every seam it consumes exists after T019–T024.

1. `TEST_CASE("Seraphis_ParameterPush_IsAllocationFree")` (SC-006) — with
   `TestHelpers::AllocationScope` active and readings from
   `AllocationDetector::instance().getAllocationCount()`, a **4 s** render during which **all 91
   parameters** are automated **every block** records **exactly 0** allocations after
   `setupProcessing()` returns. The render **also calls `setState()` on the prepared processor** at
   least once inside the measured window, so the 2.1 KiB staging handoff and `pushAllSurfaces()` are
   both covered. Warm the fixture first (`reserveCapture` + one warm-up render).
2. `TEST_CASE("Seraphis_ParameterPush_IsOnChangeOnly")` (SC-007) — quiescent engine, **constant
   tempo**, every class-(b) smoother settled; render **200 blocks** with no parameter change, then:

   | Counter | After 200 unchanged blocks | After one change of… | Δ |
   |---|---|---|---|
   | `applyVoiceParams` | **exactly 1** | class-(a) `VP` ID | **+1** |
   | | | class-(b) `VP` ID (801) | **+1 … +28** (`N_chunk`), must **stop rising**, and the settled read-back equals the target **exactly** |
   | | | **any `MB` ID** | **+0** |
   | `applySpectralStates` (successes) | **exactly 1** | one `CFG` ID | **+1** |
   | `applySpectralStates` (**attempts**) | **exactly 1** | one `CFG` ID while quiescent | **+1** |
   | `applyAetherParams` | **exactly 1** | one `AE` ID | **+1** |
   | `setTargetBase` | **exactly 27** | class-(a) `MB` ID | **+1** |
   | | | class-(b) `MB` ID (802) | **+1 … +28**, must **stop rising**; the other **26 targets untouched** |
   | | | any macro knob (100–104) | **+0** |
   | each of the four `ENG` counters | **exactly 1** | ID 3 (seed) or ID 1008 (freeze) | **+1 on that counter, +0 on the other three** |

   Plus the **three separation clauses**: an `AE` change must not increment `applyVoiceParams`; a `VP`
   change must not increment `applyAetherParams`; and an **`MB`-only change must not increment
   `applyVoiceParams`** (the third is what catches a `markDirty` that bumps `voiceParamGeneration_` on
   `MB`).
   Plus the **`setState()` sub-clause**: a stream whose seed index and polyphony **equal** the current
   values, followed by one block, leaves `engSeedPushCountForTest()` and
   `engPolyphonyPushCountForTest()` **unmoved**; the same stream with a **different** seed index moves
   the seed counter by exactly 1.
   Plus the **moving-tempo clause**: with `kMorphSyncId` on and a ramped `processContext` tempo,
   `applyVoiceParams` increments on **every block in which the derived rate moved by more than
   `kSyncedRateEpsilon` and on no other block**; a **constant** tempo with sync on must not increment it
   at all; `applyAetherParams` and `applySpectralStates` must not increment on any of them.
   Plus the **retry-bound clause**: with a `CFG` change pushed while **one** voice sounds and fifteen
   are idle, the **attempt** counter rises by 1 per block, but the fifteen idle voices'
   `getRejectedConfigureTimeCallCount()` is unchanged after the first attempt **and** their bits left
   `spectralRetryMask_` on that first attempt.
3. `TEST_CASE("Seraphis_SpectralStateAssignment_HonoursGate")` (SC-013) — (1) assign a new state while a
   voice is **sounding**: that voice's audible spectrum is unchanged for the note and
   `getRejectedConfigureTimeCallCount()` rises; (2) `spectralStatesPendingForTest()` **stays set** while
   any targeted voice keeps rejecting and the parameter atomics are never cleared in response; (3) on
   the **first block after every voice has become quiescent** the retry succeeds — flag clears, rejection
   counter stops rising on every voice, `morph().getStateCount()` equals the pushed count, the next
   note-on renders the new spectrum, and **all sixteen voices hold the same state**.

**Verify:** `seraphis_tests` green in full.

### T026 [P] — `integration/param_character_test.cpp`: SC-019, SC-020

**Files (edit):** `plugins/seraphis/tests/integration/param_character_test.cpp` (the T002 skeleton),
and — if the seed measurement requires it — `plugins/seraphis/src/parameters/dropdown_mappings.h`

1. `TEST_CASE("Seraphis_ParameterSurface_IsSampleRateIndependent")` (SC-019) — the **settled last
   second** of a 4 s render of note 60 at fixed seed and identical parameter settings, **65 536-point
   FFT, Blackman-Harris**, metrics over the **20 Hz – 16 kHz** band only. Thresholds: output **RMS
   within 1.0 dB** across **44.1 / 48 / 96 kHz**; band-limited **spectral centroid within 5 %**;
   **spectral flatness is recorded, not gated** (it is dominated by the stochastic atmosphere and reverb
   tails, whose realisation changes with `RollingCaptureBuffer`'s power-of-two capacity rounding).
2. `TEST_CASE("Seraphis_Seed_IsDeterministicAndDistinct")` (SC-020) — operating point **pinned**: note
   **60**, velocity **100**, held **3 s** then released, **4 s** total, **48 kHz**, block **512**,
   polyphony **8**, registered defaults **except** `kCloudDriftDepthId` (205) at **25 cents** and
   `kBodyMaterialId` (800) at **Glass**. Both deviations are required (drift depth defaults to 0.0
   cents; `ContinuousBody::setSeed` drives the per-voice modal micro-detune on the **three modal
   materials only**, with Strings and Chamber documented seed-independent).
   *Clause 1:* two `Processor` instances with identical parameters including `kSeedId` render within
   `render_fingerprint.h` tolerance.
   *Clause 2:* two instances differing **only** in `kSeedId` differ in total variation by more than a
   gate **derived from measurement** — render **all 16 entries of `kSeedValues`** at this operating
   point, record the pairwise spread, set the gate at **`floor(min observed spread / 1.05)`**, and check
   the measured table in beside the constant. **A small spread is a defect of the checked-in table
   (C-10): re-pick the offending constant in `dropdown_mappings.h` and re-measure. Lowering the gate is
   not an available remedy, and neither is re-examining the engine's seed derivation.**
   *Clause 3:* `kSeedValues[0] == 1u` asserted directly.

**Verify:** `seraphis_tests` green in full; the measured seed-spread table recorded for
`compliance.md`.

### T027 [P] — `integration/param_perf_test.cpp`: SC-008 and SC-009 (structure only; figures land in T028)

**Files (edit):** `plugins/seraphis/tests/integration/param_perf_test.cpp` (the T002 skeleton)

Write both `[.perf]` cases with their subjects, their optimization barriers and **placeholder baseline
constants marked `// PINNED IN T028`**. T028 runs them and checks in the measured numbers.

1. `TEST_CASE("Seraphis_ParameterPush_CpuBudget", "[.perf]")` (SC-008) — **measured directly, never by
   subtracting two whole-chain renders** (0.05 % of a block is 5.3 µs against a ≈180 µs/block
   whole-chain spread — 34 × larger than the quantity). **Three arms:**
   - *steady state* — one `process()`-entry pass with every generation counter unchanged, every
     class-(b) smoother settled, tempo constant: the tracker comparisons, the settled-check and the
     synced-rate comparison and nothing else. Two bounds, both binding: **≤ 0.05 %** of the block budget
     (**5.3 µs** at 512/48 kHz, block budget 10 666 666.7 ns) **and** a checked-in
     `ceil(measured worst × 1.05)` baseline.
   - *worst case* — build a `SeraphisVoiceParams` from the atomics + `applyVoiceParams` at polyphony
     **8 and 16** (both reported, the gate is the worse), 27 × `setTargetBase`, `applyAetherParams`, and
     one `applySpectralStates` **with the full `0xFFFF` mask over a quiescent pool** (the genuine
     whole-pool fan-out: **4096 `std::log2`** plus 64 `isValidSpectralState` scans plus 64 × 128-float
     comparisons). **≤ 0.50 %** (**53.3 µs**) **and** ≤ the checked-in baseline.
   - *class-(b) settling arm* — a 512-sample block rendered while a class-(b) ID is under **continuous**
     automation, so the block runs as **eight 64-sample sub-slices**; measures **whole-block wall time**,
     reported against the same block rendered undivided in the same trial set, gated on a checked-in
     `ceil(worst × 1.05)` baseline. **No absolute ceiling here.**
   **Every arm consumes its result through a `volatile` sink and asserts a strictly non-zero elapsed
   time — an arm reporting 0 ns FAILS.** Measurement discipline: best-of-16 per subject, ≥ 8 trials,
   idle machine, `static_assert(baseline × kRegressionFactor ≤ kReference)`. **No compiled-out arm.**
2. `TEST_CASE("Seraphis_FullPoly_CpuBudget_WithFullSurface", "[.perf]")` (SC-009) — **the subject is a
   hand-built `SeraphisEngine` + `AetherReverb` pair in this TU, NOT `Processor`** (RA-1 row (c) needs
   `numChannels = 16` and `diffusionFftSize = 4096`, which `makeSeraphisReverbConfig` structurally
   cannot produce). Scenario pinned from Phase 7 SC-001: polyphony **8**, **all 8 voices sounding**,
   atmosphere **frozen** via `setAtmosphereFreeze(true)` and **asserted** by `isFreezeCaptured()` on
   every voice before measurement; `AetherReverb` at RA-1 row (c) (`numChannels = 16`, shimmer + bloom +
   spectral diffusion all on, `diffusionFftSize = 4096`, `setSize(1)`, `setDensity(1)`, 32 bloom
   resonators); 512-sample blocks at 48 kHz on the composed chain. *Gate:* **≤ 25 % of one core
   (2 666 666.7 ns/block)**, best-of-16, ≥ 8 trials, `ceil(worst × 1.05)` checked in. **The 16-voice
   figure is measured and recorded as a non-gating number.** If the gate fails, the lever is the shipped
   voice count or Phase 9's own push cost — **never the 25 % ceiling.**
   The **91-row non-default parameter table** is checked in as a `constexpr` array and applied through
   the four DSP routes plus the direct `ENG` setters — plan §7.9's table verbatim, including its
   **three declared exception classes** (class 1 = 8 processor-local rows; class 2 = 5 scenario-pinned
   rows; class 3 = the 10 rows whose most-expensive end **coincides with** the registered default). Row
   **800 = Metal Plate** must be confirmed by recording `getActiveModeCount()` for all five materials
   and taking the maximum; if another material wins, the row changes and the measurement is redone.

**Verify:** the TU builds and both cases run (with placeholder baselines they may report rather than
gate); `seraphis_tests` green in full for the non-`[.perf]` set.

---

## GROUP 20 — Integration: measurement, registration audit, gates, docs

### T028 — Measurement pass: pin every deferred threshold and baseline

**Files (edit):** `plugins/seraphis/tests/integration/param_perf_test.cpp`,
`plugins/seraphis/tests/integration/param_reach_test.cpp`,
`plugins/seraphis/tests/integration/param_character_test.cpp`,
and — **only if** SC-008's worst-case arm breaches 0.50 % —
`plugins/seraphis/src/processor/processor.cpp` + `specs/seraphis-phase9-parameters/spec.md`

**Do:**

1. Run `seraphis_tests.exe "[.perf]"` on an idle machine. Pin **all three SC-008 baselines** and the
   SC-009 baseline as `ceil(worst × 1.05)`; record the 8-voice gating figure **and** the 16-voice
   non-gating figure.
2. Pin SC-003's **two measured floors** as `floor(min observed / 1.05)`: ID 1202's echo-density factor
   (spec floor ≥ 1.5 ×) and ID 2's harmonic-energy floor (8 repeats, floored below the run-to-run
   spread). Write both into the TU as named constants beside the measurement note.
3. Pin SC-020 clause 2's seed-spread gate from T026's 16-seed measurement.
4. Record SC-005's measured bypassed-vs-smoothed ratio (design prediction 3.53 ×).
5. **If SC-008's worst-case arm breaches 0.50 %:** adopt the one-directional remedy — bound the
   per-block fan-out so `pushSpectralStatesIfPending()` writes at most
   `kSpectralFanOutVoicesPerBlock` voices per block (**start at 4**), clearing their mask bits as they
   accept, so a whole-pool raise amortises over `ceil(16 / k)` blocks — **and apply amendment A9 to
   `spec.md` in the same change**: SC-013 clause 3's *"on the first block after every voice has become
   quiescent"* → *"within `ceil(16 / kSpectralFanOutVoicesPerBlock)` blocks"*. **Raising the 0.50 %
   ceiling is not an available remedy, and neither is dropping the §3.4 identity guard.**
   If it does **not** breach, record that explicitly with the measured µs/block beside it.

**Verify:** `[.perf]` and `[.slow]` runs green with the pinned constants; every figure captured to a log
on its **first** run (never re-run a slow suite to look at output).

**Outcome (T028 2026-08-01, completed 2026-08-02) — step 1's two deviations, both recorded rather than
silently absorbed:**

- **The three SC-008 baselines pinned as written**, `ceil(worst × 1.05)` over T028's six-run dataset
  (`param_perf_test.cpp:397/:409/:424/:431/:437`). The seven-run cold dataset of 2026-08-02 lands under
  every one of them (arm 1 worst 82.40 vs gate 111.55; arm 2 worst 32 496 vs 40 459.3; arm 3 worst
  1 378 910 / 1 292 840 vs 1 836 810 / 1 828 710), so **none was re-pinned** — the discipline re-pins
  only when a measurement exceeds a pin.
- **The SC-009 baseline could NOT be pinned as `ceil(worst × 1.05)` and is CEILING-DERIVED instead.**
  T028 escalated (`ceil(worst × 1.05) = 2 711 699` failed both `static_assert`s); the escalation was
  closed on **2026-08-02** by a seven-run cold-machine dataset (19.91–20.91 %, median 20.52 %) plus a
  no-Phase-9-code control validating the machine state. `ceil(2 230 830 × 1.05) = 2 342 372` still
  collides with the 25 % ceiling through `kRegressionFactor`, so per **FR-057 amendment A12
  (`spec.md`, 2026-08-02)** the baseline is `floor(25 % ÷ 1.15) = 2 318 840`
  (`param_perf_test.cpp:456`), with `kSc009BaselinePinned = true` (`:402`). **The 25 % ceiling,
  `kRegressionFactor`, `kBaselineHeadroom`, both `static_assert`s and the 8-voice count are unchanged.**
- **Step 5 did not trigger:** SC-008's worst-case arm does not breach 0.50 % (cold worst 32 496 ns =
  **0.3047 %** of one core), so `kSpectralFanOutVoicesPerBlock` was not introduced and **spec amendment
  A9 was not applied**; SC-013 clause 3 keeps its original wording. Recorded in the TU banner with the
  measured figure, as this step requires.

### T029 — Registration audit and the full-suite run

**Files:** none edited unless the audit finds a gap (then
`plugins/seraphis/tests/CMakeLists.txt` and/or `dsp/tests/CMakeLists.txt`)

**Do:**

1. Confirm **all ten** new test TUs appear in their target's source list (neither list is globbed — a
   file not listed silently drops and its cases never run), and that
   `integration/param_continuity_test.cpp` and `unit/state_v2_test.cpp` are on the
   `-fno-fast-math -fno-finite-math-only` list while **`integration/param_perf_test.cpp` is not**, and
   `unit/systems/seraphis_param_broadcast_test.cpp` is **not** on `dsp/tests/CMakeLists.txt:735-740`.
2. Count the `TEST_CASE`s discovered by `ctest` against the list of cases this phase adds; a missing
   case means a missing CMake entry.
3. Full runs, each captured to a log on the **first** invocation:

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests seraphis_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5   # IN FULL
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5      # IN FULL
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.slow]" 2>&1 | tail -20
```

`dsp_systems_tests` must be run **in full** — `continuous_body.h` is consumed by other cases in that
exe. The other four per-layer exes do not include any of the four touched headers; if the local build
tree says otherwise, run them too.

**Verify:** both exes report all tests passing; zero warnings on the build.

### T030 — Portability, lints, clang-tidy, pluginval, ASan/valgrind lifecycle

**Files:** none edited unless a gate fails.

**Do (plan §7.12, §8.4):**

1. `node tools/check-portability.js` — **clean**. A green Windows build is not evidence about the Linux
   or macOS legs.
2. `node tools/lint-odr.js`, `lint-layers.js`, `lint-float-bit-goldens.js`,
   `lint-arch-guarded-includes.js`, `lint-simd-aligned-loadstore.js` — all clean.
3. `./tools/run-clang-tidy.ps1 -Target seraphis` **and** `-Target dsp` — clean, output captured to a log
   on the first run. Fix **all** warnings, not only those on new lines.
4. Build the bundle and run pluginval, capturing to a log on the first run:

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis
tools/pluginval.exe --strictness-level 5 --validate \
  "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

   Must exit 0, including its automated sweep over all 91 parameters.
5. SC-016: `unit/controller/editor_lifecycle_test.cpp` is **unchanged in shape**
   (`exerciseEditorLifecycle(controller, "editor", uidescPath, 3)`) and must complete with **zero
   reports** under (a) the valgrind-nightly editor-lifecycle job and (b) a **local
   `-DENABLE_ASAN=ON` Debug run**. There is no ASan CI lane and adding one is out of scope.
6. Confirm the FR-019 mechanical check: `grep -n "sampleRate\|sampleRate_\|getSampleRate"
   plugins/seraphis/src/parameters/*.h` returns **empty**. The verbatim output is evidence for
   `compliance.md`; a claim without it is not.

**Verify:** every gate green; logs retained for `compliance.md`.

### T031 — Roadmap and plugin documentation

**Files (edit):** `specs/Seraphis-roadmap.md`, `plugins/seraphis/CLAUDE.md`,
`plugins/seraphis/CHANGELOG.md`, `plugins/seraphis/src/plugin_ids.h` (citation re-verification)

**Do (plan §9.1's six clauses, §9.2):**

1. **Amend roadmap line 313 in place**, in the dated-parenthetical shape the Phase 5 budget amendment
   already used (`:250-254`): the gate is **8 voices**, not 16; the 2026-07-30 owner ruling that set it;
   the **measured** 8-voice figure; and the **measured** 16-voice figure SC-009 records as non-gating.
2. **Strike Open Question 2** (line 514 pre-amendment, 564 after) as resolved by Phase 3 — mandatory
   and unconditional.
3. **Write the Phase 11 inheritance into the Phase 11 entry:** Phase 11 owns the three `SpectralState`
   authoring mutators (`setPartial`, `blendStates`, `tiltState`) **and** Phase 3's
   validity-preservation criterion over them, alongside the per-partial editing surface that is their
   only consumer, and `HarmonicCloud::setSpectralTarget` / `setPartialPosition` / `setPartialMask`.
4. **Move Open Question 5** (line 517 pre-amendment, 572 after) **to a new named phase** (do not strike it): per-note expression
   ships, and that phase owns **both** the `SeraphisVoice` per-voice expression inputs and
   `INoteExpressionController`; record that the controller-FUID host-cache hazard is **accepted**.
5. **Record** that polyphony values 9…16 are user-reachable and outside the budgeted scenario.
   Reducing the registered maximum below 16 is out of scope (a range change at a shipped ID) and
   raising the gate to 16 by relaxing the 25 % ceiling is forbidden.
6. **After clauses 1–5 land, re-verify every roadmap-line citation** in `spec.md`, in
   `specs/seraphis-phase9-parameters/*` and in `plugins/seraphis/src/plugin_ids.h:46`. Citations
   **before** line 313 (113, 148, 186, 245) are unaffected; citations **after** it (383, 386–388,
   500–508, 511, 514, 516, 517) shift by the number of lines added and **must** be corrected in the
   same change. A citation left stale by this edit is a defect of **this** phase. **Applied 2026-08-01:
   those eight became 396, 399–401, 550–558, 561, 564, 571 and 572**; line 313 does not move, because
   the amendment starts on it.
7. `plugins/seraphis/CLAUDE.md` — the param-ID table's Phase column: bands 200–1399 marked
   **9 — shipped**; 1400+ stays Phase 10. Rewrite the "MPE / note expression is a known **Phase 9**
   decision" entry under *Decisions that outlive Phase 8* to record RQ-2's actual ruling.
8. `plugins/seraphis/CHANGELOG.md` — a section for the version this phase ships, so
   `tools/check-changelog-coverage.js` finds it. `version.json` is the only other file a version bump
   touches; **generated files are never hand-edited.**

**Verify:** `node tools/check-changelog-coverage.js` finds the entry; every roadmap citation
re-checked; no stale `roadmap 383-386` string remains
(`grep -rn "383-386" specs/seraphis-phase9-parameters/ plugins/seraphis/src/plugin_ids.h`).

---

## Traceability — task → plan section → criteria

| Task | Plan § | Criteria / FRs |
|---|---|---|
| T001 | §12.1 | A1–A8 |
| T002 | §8.1, §7.0 | FR-101 |
| T003 | §1.5, §1.6 | FR-070, FR-072, FR-006 |
| T004 | §1.1–1.3, §1.6 | FR-001, FR-002, FR-005, FR-072 |
| T005 | §1.4 | FR-003, FR-004, SC-002 cl. 4 |
| T006 | §2.1 | FR-010–FR-013, C-5, C-9 |
| T007 | §2.2 | FR-015, C-7, C-10 |
| T008 | §2.3, §5.2 | FR-014, FR-091a |
| T009 | §7.3 | SC-001, SC-014, SC-015, SC-022, FR-061 |
| T010–T015 | §2.3 | FR-014, FR-016–FR-019 |
| T016 | §4 | FR-060–FR-064 |
| T017 | §6 | FR-100, SC-015 |
| T018 | §2.4 | FR-049 |
| T019 | §3.1–3.3 | FR-040–FR-046, FR-048, SC-003 |
| T020 | §3.4, §3.7, §5 | FR-047, FR-041a/b, FR-090–FR-094, SC-010–SC-012, SC-023 |
| T021 | §7.10 step 8a | R17 |
| T022 | §3.5, §3.8 | FR-059, FR-059a, SC-005 |
| T023 | §3.6 | FR-056, C-7, SC-018 |
| T024 | §11 step 12, §7.1, §7.5 | FR-050, FR-051, SC-002, SC-004 |
| T025 | §7.7 | SC-006, SC-007, SC-013, FR-045 |
| T026 | §7.11 | SC-019, SC-020 |
| T027 | §7.8, §7.9 | SC-008, SC-009, FR-057 |
| T028 | §7.4, §7.8, §7.9, §7.11, §12.1 A9 | measured thresholds |
| T029 | §8.1, §8.2 | R12, FR-101 |
| T030 | §7.12, §8.4 | SC-016, SC-017, SC-021, FR-019, FR-104 |
| T031 | §9.1, §9.2 | FR-058, FR-103 |
