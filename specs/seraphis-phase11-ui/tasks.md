# Tasks: Seraphis Phase 11 — UI (Organism-First Editor)

**Spec:** `specs/seraphis-phase11-ui/spec.md`
**Plan:** `specs/seraphis-phase11-ui/plan.md` (Revision 3, 2026-08-03 — this list is generated against
Revision 3 and supersedes every earlier tasks.md)
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 11 (lines 475–536)
**Branch:** `feat/seraphis-phase1-life-modulators` (the single Seraphis branch — do not rename, do not
create a new one)
**Status:** TASKS — no implementation yet
**Date:** 2026-08-03

---

## How to read this file

- Tasks are **T001 …**, collected into **ordered GROUPS**. Groups run in order; a group starts only when
  the previous group is green.
- `[P]` marks a task that is **parallel-safe within its group**: it creates **only new files** that no
  other task in that group touches, and edits **no** shared file. Every other task edits at least one
  pre-existing (shared) file — `processor.cpp`, `controller.cpp`, `editor.uidesc`, a CMake list — and
  therefore **sits alone in its own group**. Only **G7** has `[P]` tasks; the dependency chain through the
  processor and controller makes the rest of this phase sequential by construction, and pretending
  otherwise produces merge conflicts, not speed.
- Every task is **self-contained**: exact files, the **failing test written FIRST** (file, `TEST_CASE`
  name, the assertions with their numbers), then the implementation intent, then the target that verifies
  it. The executor is assumed to have no other context — but MUST still open the cited `file:line`
  before editing and never guess a signature.
- Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
- **No commit tasks.** Commits happen outside this workflow.
- **No threshold in this file is a lever.** Where a task says "if it fails, X gets cheaper", that is the
  only permitted response. Raising a ceiling, loosening a tolerance, or editing a Phase 3 test to make a
  Phase 11 change pass is a failure of the criterion, not a fix.

### Build and run commands (Windows — always the full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target <target>
build/windows-x64-release/bin/Release/<target>.exe 2>&1 | tail -5           # summary only
build/windows-x64-release/bin/Release/<target>.exe "TestCaseName*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/<target>.exe "[.perf]"       2>&1 | tail -20
```

Targets used here: `dsp_processors_tests`, `dsp_systems_tests`, `seraphis_tests`, `Seraphis`.
`dsp_core_tests`, `dsp_primitives_tests` and `dsp_effects_tests` are **not** touched by this phase and
need not be rebuilt.

### The CMake convention in this list

`plugins/seraphis/CMakeLists.txt` (`smtg_add_vst3plugin` source list, `:18-46`),
`plugins/seraphis/tests/CMakeLists.txt` (`add_executable(seraphis_tests …)` at `:5`, the
`set_source_files_properties` block at `:91-116`) and `dsp/tests/CMakeLists.txt`
(`add_executable(dsp_processors_tests …)`, `add_executable(dsp_systems_tests …)`) are **ENUMERATED, never
globbed** — an unregistered TU drops out of the build silently and Catch2 reports *"No tests ran"* instead
of a failure (plan R-9).

Two complementary rules, both mandatory:

1. **Each task that creates a new test TU appends that one file to the relevant enumerated list inside its
   own task**, so the new test runs red-then-green from within the task that writes it.
2. **T027 is the single authoritative audit pass** over both `CMakeLists.txt` files: every new file present
   exactly once, the new plugin `.cpp`s present in **both** the plugin source list and the test exe's
   second-compilation list, and the `-fno-fast-math` block correct.

Do not scatter any other CMake edit across tasks.

### Rulings already applied — there is no blocking input for this list

The phase owner's 2026-08-03 rulings are **in the spec** and are encoded in the tasks below. Recorded here
so no executor re-opens them:

- **D-1 → RELAXED, not disclosed.** `SeraphisVoice::setSpectralState` / `setSpectralStateCount` stop
  routing through `isConfigurable()` (**T003a**). The "applies on next note" indicator is **not** built.
- **D-2 → accepted with a replacement observable.** `effectsPushes_` provably cannot move for IDs 1410 /
  1441; SC-021(c) asserts `composedFxDelaySendForTest()` / `composedFxWanderDepthForTest()` /
  `composedEffectsRecomputeCountForTest()` instead (**T013**).
- **Composition cadence** — the one-block lag is accepted as designed; SC-021(a) reads its sweep allowing
  exactly one block of settle per point.
- **`fundamentalHz`** — `frequencyHz[0]` is forbidden by name; the source is
  `HarmonicCloud::getFundamentalHz()` (`dsp/include/krate/dsp/systems/harmonic_cloud.h:405`) (**T008**).
- **OQ-4** — methodology and acceptance band fixed; the two numbers stay pending measurement in **T025**
  and are written back into spec.md in **T026**.
- **The extra `dsp/` test TU** — `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` is its own file
  (**T003**).

---

## GROUP 1 — Preflight

### T001 — ODR sweep, baseline capture, anchors

**Files:** none created or edited. This task produces recorded facts only.

**Depends on:** nothing.

**Do:**

1. **Confirm the branch.** `git rev-parse --abbrev-ref HEAD` must print
   `feat/seraphis-phase1-life-modulators`. If it prints `main`, STOP — do not implement on `main`.
2. **ODR sweep.** For each name this phase claims, run
   `grep -rn "class <Name>\b\|struct <Name>\b" dsp/ plugins/ tools/` and require **0 matches**:
   `CloudView`, `MacroRingKnob`, `DrawerContainer`, `CloudFrame`, `SeraphisEditSubController`,
   `SeraphisEffectsTargets`, `EditMessage`, `EditThrottle`.
   Free functions — `grep -rn "\b<name>\s*(" dsp/ plugins/`, **0 matches**: `setPartial`, `blendStates`,
   `tiltState`, `computeEffectsTargets`, `publishCloudFrame`, `repushPartialOverrides`, `stageSlotEdit`.
   **Expected non-zero, and legal:** `setPartialPosition` / `setPartialMask` / `clearPartialMask` already
   exist on `HarmonicCloud` (`dsp/include/krate/dsp/systems/harmonic_cloud.h:1069`, `:1084`, `:1101`) —
   the new names are `…AllVoices` on `SeraphisEngine` plus same-name pass-throughs on `SeraphisVoice`,
   which is a different class and therefore not an ODR collision. Record that explicitly rather than
   letting the next reader rediscover it.
3. **Capture the green baseline** before touching anything:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests seraphis_tests Seraphis
   build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -3
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe    2>&1 | tail -3
   build/windows-x64-release/bin/Release/seraphis_tests.exe       2>&1 | tail -3
   ```
   Record the three `All tests passed (N assertions in M test cases)` lines — the **case counts** matter
   later (a new TU that was never registered leaves the count unmoved). A pre-existing failure is **not**
   dismissible: fix it or stop (constitution VIII).
4. **Capture Phase 3's `spectral_morph_*` summary specifically**, because T003a's SC-030 is a
   before/after diff of exactly this:
   ```bash
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph*" 2>&1 | tail -5
   ```
   Record the line verbatim.
5. **Record the perf anchors**, read out of `plugins/seraphis/tests/integration/param_perf_test.cpp`:
   `kFullPolyCeilingNs = kBlockBudgetNs * 0.25` (`:392`) = **2 666 666.7 ns/block**;
   `kRegressionFactor = 1.15` (`:395`); `kBaselineFullPolyNs = 2318840.0` (`:472`); and Phase 10's pinned
   worst-of-seven at the 8-voice gate, **2 380 980 ns = 22.32 %**
   (`specs/seraphis-phase10-effects/compliance.md`, SC-014). Remaining headroom: **2.68 percentage
   points**. The 25 % ceiling is **not a lever** at any point in this phase (plan R-12, R-18).

**Verify:** every sweep 0-match as listed (with the three expected `HarmonicCloud` hits recorded as
legal); four suite summaries recorded; the five perf numbers recorded; branch correct.

---

## GROUP 2 — Layer 2: the three authoring mutators

### T002 — `setPartial`, `blendStates`, `tiltState` in `spectral_state.h`

**Files**
- Edit: `dsp/include/krate/dsp/processors/spectral_state.h`
- Edit: `dsp/tests/unit/processors/spectral_state_test.cpp` (already registered to `dsp_processors_tests`
  in `dsp/tests/CMakeLists.txt` — **no CMake edit in this task**)

**Depends on:** T001.

**Test FIRST** — extend `dsp/tests/unit/processors/spectral_state_test.cpp`:

`TEST_CASE("SpectralState_AuthoringMutators_PreserveValidity", "[spectral_state][phase11]")`

A table of **at least 40 rows**. Per row: `SpectralState before` (built for that row); `SpectralState
after = before;` call the mutator on `after`; then assert the branch the input selects (FR-032, SC-012):

1. `isValidSpectralState(before) == true` → `CHECK(isValidSpectralState(after))`.
2. `isValidSpectralState(before) == false` → for `setPartial` and `tiltState`,
   `CHECK(std::memcmp(&before, &after, sizeof(SpectralState)) == 0)` — **byte-unchanged, no half-write.**
   (`SpectralState` is trivially copyable, `spectral_state.h:65`, so `memcmp` is well defined.)
3. `blendStates`' **return value** satisfies `isValidSpectralState` on **every** row, valid inputs or not.

Coverage the table must contain explicitly:
- ratios `< SpectralState::kMinStateRatio` (0.5f, `:51`) and `> SpectralState::kMaxStateRatio`
  (128.0f, `:52`);
- non-monotone ratios: equal neighbours, descending neighbours, and neighbours closer than
  `kAuthorSpacing²`;
- amplitudes `-0.5f`, `1.5f`, exactly `0.0f`, exactly `1.0f`;
- `numPartials ∈ {-1, 0, 1, 64, 65, INT_MAX}`;
- `index ∈ {0, numPartials-1, numPartials, 63, 64, SIZE_MAX}`;
- `t ∈ {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f}`;
- `dbPerOct` at both clamp edges (`SpectralState::kMinStateTiltDbPerOct = -12.0f`,
  `kMaxStateTiltDbPerOct = 12.0f`, `:53-54`) and beyond (`-30.0f`, `+30.0f`);
- **non-finite arguments built from bit patterns** — `std::memcpy` of `0x7FC00000` (NaN) and `0x7F800000`
  (Inf) into a `float` through a `volatile` sink. **Never**
  `std::numeric_limits<float>::quiet_NaN()`; the header's own `-ffast-math` banner at `:21-23` is the
  reason.
- **At least three rows that are invalid SOMEWHERE OTHER THAN the edited index** — these are the rows that
  prove step 0 below exists: `amplitudes[5] = 1.5f` (`:106-108`), a descending ratio pair at index 30
  (`:97-99`), and a `name` field with no NUL byte (`:118-120`), each combined with a `setPartial` call at
  index 0. Without the whole-state gate these three store and `memcmp != 0`.

Plus three targeted cases in the same file:
- `blendStates(a, b, 0.0f)` is **byte-identical** to `a` and `blendStates(a, b, 1.0f)` byte-identical to
  `b`, for two valid states with **different `name` fields and different `numPartials`** —
  `CHECK(std::memcmp(&result, &a, sizeof(SpectralState)) == 0)`. This is the case that is red without the
  exact-endpoint short-circuit, and SC-025's reversibility rests on it.
- `tiltState(s, -6.0f)` applied **twice** leaves `s` byte-identical to a single application (absoluteness).
- `setPartial` on a valid state with a target beyond the monotone window leaves `ratios` **clamped, never
  swapped**, and `name`, `tiltDbPerOct`, `inharmonicity`, `numPartials` byte-unchanged.

Observe the test **fail to compile** (the three functions do not exist) before implementing. After
implementing, **temporarily delete step 0** and confirm the three "invalid elsewhere" rows go red; restore
it. Same for the endpoint short-circuit and the byte-identity case. A criterion that was never observed
failing is not trusted.

**Implement** — in `spectral_state.h`, **immediately after `makeFactoryState`'s closing brace (`:483`)
and before the namespace close (`:485`)**. This placement is mandatory, not cosmetic: `kAuthorSpacing`
is initialised from `detail::factory::kFillSpacingFactor` (`:344`) and `blendStates`' tail fill names
`detail::factory::kFillMaxGrowth` (`:342`) and `kFillMaxRatio` (`:343`), all inside a namespace that does
not open until `:317`. A definition placed after `normalizeSpectralState` (`:155-175`) is a hard compile
error on every leg.

**The four range constants are `SpectralState`-scoped members** (`:51-54`), not namespace-scope names.
Every use in these three free functions must be written `SpectralState::kMinStateRatio`,
`SpectralState::kMaxStateRatio`, `SpectralState::kMinStateTiltDbPerOct`,
`SpectralState::kMaxStateTiltDbPerOct`. An unqualified use does not compile on any leg.

- `inline constexpr float kAuthorSpacing = detail::factory::kFillSpacingFactor;  // 1.0163049f`
- `inline void setPartial(SpectralState& s, std::size_t index, float ratio, float amplitude) noexcept`
  — plan §1.1's ordered clauses, **every rejection before the first store**:
  **step 0** `if (!isValidSpectralState(s)) return;` (the whole-state gate — load-bearing, see the three
  rows above); reject `numPartials` outside `[0,64]`; reject `index >= numPartials`; reject non-finite
  `ratio`/`amplitude` via `detail::isNaN` / `detail::isInf` (used at `:91`, `:103`) — **never
  `std::isnan`**; `amp = clamp(amplitude, 0, 1)`; `r = clamp(ratio, SpectralState::kMinStateRatio,
  SpectralState::kMaxStateRatio)`; then the monotone window
  `lo = index > 0 ? ratios[index-1] * kAuthorSpacing : SpectralState::kMinStateRatio`,
  `hi = index+1 < numPartials ? ratios[index+1] / kAuthorSpacing : SpectralState::kMaxStateRatio`,
  **`if (lo > hi) return;`** (no-op, no write), `r = clamp(r, lo, hi)`; finally write `ratios[index]` and
  `amplitudes[index]` **and nothing else**.
- `[[nodiscard]] inline SpectralState blendStates(const SpectralState& a, const SpectralState& b, float t) noexcept`
  — plan §1.2: select-or-default on validity (both invalid → `SpectralState{}`, documented valid
  `:42-43`; one invalid → return the other); non-finite `t` returns `a`; `u = clamp(t, 0, 1)`;
  **step 2a — `if (u == 0.0f) return a; if (u == 1.0f) return b;`** (the exact-endpoint short-circuit;
  the interior body cannot reproduce `a` byte-for-byte because step 7 rewrites `name`, step 6 regenerates
  the tail, and `exp2(log2(x))` is not the identity); `numPartials = min(a, b)`; **ratios interpolate in
  `log2` then `exp2`, with NO clamp** (a clamp is the one operation that can flatten two neighbours and
  break strict monotonicity); amplitudes / `tiltDbPerOct` / `inharmonicity` linear; slots at
  `i >= numPartials` filled by the **same** geometric continuation `makeFactoryState` uses (`:416-433` —
  the `count >= 2` recurrence, `kFillMaxGrowth` clamp, `kFillMaxRatio` ceiling, `kFillSpacingFactor`
  floor) with amplitudes left at `0.0f`; `name = "Blend"`. **No `normalizeSpectralState` call.**
  Returns by value: `sizeof(SpectralState) == 540` — **not** 541, which is `kSpectralStateBytes` (`:186`),
  the *serialized* size and a different quantity.
- `inline void tiltState(SpectralState& s, float dbPerOct) noexcept` — plan §1.3: no-op on non-finite
  `dbPerOct`; no-op on `!isValidSpectralState(s)`;
  `target = clamp(dbPerOct, SpectralState::kMinStateTiltDbPerOct, SpectralState::kMaxStateTiltDbPerOct)`;
  `delta = target - s.tiltDbPerOct`; if `numPartials <= 0` assign the field and return; else for
  `i < numPartials`, `amplitudes[i] *= std::pow(10.0f, (delta / 20.0f) * std::log2(ratios[i] / ratios[0]))`;
  then `normalizeSpectralState(s)` (`:155`); then `s.tiltDbPerOct = target`.
  **`std::pow(10.0f, x)`, never `exp10f`** — glibc GNU extension, absent on MSVC; the repo's standing
  prohibition is recorded at `dsp/include/krate/dsp/systems/continuous_body.h:1643-1645`. Do **not** copy
  `exp10Fast` into Layer 2.

All three are `noexcept`, allocation-free, exception-free, and **add no include** — the header already
carries `<algorithm> <array> <bit> <cmath> <cstddef> <cstdint> <cstring> <type_traits>` plus
`core/db_utils.h` at `:26-35`. Layer 2 discipline (`:17-19`) is unchanged.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "SpectralState_AuthoringMutators*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -3
```
Zero warnings; the new case green; the whole suite still green with its case count up by one.

---

## GROUP 3 — Layer 3: the per-partial fan-outs

### T003 — `SeraphisVoice` pass-throughs and `SeraphisEngine` all-voices fan-outs

**Files**
- Edit: `dsp/include/krate/dsp/systems/seraphis_voice.h`
- Edit: `dsp/include/krate/dsp/systems/seraphis_engine.h`
- Create: `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp`
- Edit: `dsp/tests/CMakeLists.txt` — append the new TU to the **`dsp_systems_tests`** `add_executable`
  list (the enumerated `unit/systems/…` block). T027 audits it.

**Depends on:** T001.

> **MASK POLARITY — read this before writing either the test or the implementation.**
> `HarmonicCloud::setPartialMask(std::size_t index, bool active)`'s body is **`masked_[index] = !active;`**
> (`dsp/include/krate/dsp/systems/harmonic_cloud.h:1082-1089`). So **`active == true` ⇒ AUDIBLE**,
> **`active == false` ⇒ SILENCED**, and `clearPartialMask()` is `masked_.fill(false)` (`:1101`) ⇒
> everything audible. The plugin-side `CloudFrame::maskBits` convention is the *opposite* sense by design
> (bit set ⇔ masked), so every plugin call reads
> `setPartialMaskAllVoices(i, /*active=*/((maskBits >> i) & 1) == 0)`. Spec C-4's mask row states the
> inverted polarity and is scheduled for correction in T026 (D-9 row 9a).

**Test FIRST** — create `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` (a **dedicated TU** by
phase-owner ruling — do not fold these cases into an existing file):

`TEST_CASE("SeraphisEngine_PartialFanOut_ReachesEveryVoice", "[seraphis_engine][phase11]")`

Prepare a `SeraphisEngine` at 48 kHz. Then:

1. **Pan arm.** `setPartialPositionAllVoices(7, 0.8f)`; assert
   `getVoice(v).cloud().getPartialPosition(7) == Approx(0.8f).margin(1e-6f)` for **every** `v` in
   `[0, SeraphisEngine::kMaxVoices)` — that is **all sixteen** slots (`kMaxVoices = 16`,
   `seraphis_engine.h:211`), not just allocated ones, because a slot the allocator hands out later must
   already carry the override.
2. **Mask arm, in the API's real polarity.** `setPartialMaskAllVoices(3, /*active=*/false)` plus a
   held-note block ⇒ partial 3's amplitude decays toward 0 on every voice while partial 4's does not.
   Then `setPartialMaskAllVoices(3, /*active=*/true)` ⇒ partial 3 **recovers** on every voice. Then
   `clearPartialMaskAllVoices()` ⇒ everything audible on every slot. The recovery assertion is the one
   the FR-030 re-push depends on; an implementation that silences on `true` passes arm 1 and fails here.
3. **Rejection arm.** Out-of-range index (`64`, `SIZE_MAX`) and a **bit-pattern** non-finite position
   leave every voice byte-unchanged. The owners already reject these (`harmonic_cloud.h:1070-1075`,
   `:1085-1087`), so the fan-outs add **no second guard** — this arm asserts the pass-through does not
   grow one and let the two surfaces disagree about what was stored.

Build and observe the TU **fail to compile** (the six methods do not exist) before implementing. If the
suite's case count does not move after registration, the CMake append was missed (R-9).

**Implement**

`seraphis_voice.h` — three one-line pass-throughs in the existing `-- HarmonicCloud (Phase 2)` forwarder
block (`:641-650`), on the same "no added clamping" contract the block banner states (`:638`):

```cpp
void setPartialPosition(std::size_t i, float p) noexcept { cloud_.setPartialPosition(i, p); }
void setPartialMask(std::size_t i, bool active) noexcept { cloud_.setPartialMask(i, active); }
void clearPartialMask() noexcept { cloud_.clearPartialMask(); }
```

`seraphis_engine.h` — three fan-outs beside `applySpectralStates` (`:811`), each looping
`for (std::size_t v = 0; v < kMaxVoices; ++v)`. **`kMaxVoices`, never `getPolyphony()`** — the same rule
`applySpectralStates`' banner states (`:785-787`); this discharges FR-030's polyphony-increase clearing
event by construction. These exist because `getVoice()` is `const` (`:955`) and the non-const path is
`friend class SeraphisMacroMatrix` (`:997`), which the plugin cannot use.

**Thread ownership — record it in the doc comment.** These three write `HarmonicCloud`'s `panPosition_`,
`positionOverridden_`, `panLeft_`/`panRight_` (`harmonic_cloud.h:1069-1079`, `updatePanGains` at
`:1818-1834`) and `masked_` (`:1084-1089`), all of which `process()` reads and writes. They are
**audio-thread-only** (or host-thread with the audio thread stopped). Calling them from the message thread
is a data race; T010/T011 defer every plugin-side call accordingly.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SeraphisEngine_PartialFanOut*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -3
```
Zero warnings; new case green; suite case count up by one.

---

## GROUP 4 — The D-1 gate relaxation (the phase's ONE non-additive `dsp/` edit)

### T003a — `setSpectralState` / `setSpectralStateCount` stop being configure-time gated

**Files**
- Edit: `dsp/include/krate/dsp/systems/seraphis_voice.h`
- Edit: `dsp/include/krate/dsp/systems/spectral_morph_engine.h` (**a comment only** — `setState`'s body is
  not touched)

**Depends on:** T003 (same file). Sits in its own group because it edits `seraphis_voice.h` again.

**Test FIRST — and the test is a BEFORE/AFTER diff, not a new file (SC-030).**

1. Re-run and capture, **before** the edit:
   ```bash
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph*" 2>&1 | tail -5
   ```
   (T001 already recorded this line; confirm it still matches.)
2. Make the edit.
3. Re-run the identical command and require the summary line to be **unchanged** — same case count, same
   assertion count, all passing. **Editing any Phase 3 test to make it pass IS the failure of SC-030**, not
   a fix for it.

SC-028 and SC-029 — the criteria this relaxation exists for — need a plugin-side edit path and land in
**T010**. This task ships the `dsp/` half only.

**Implement**

In `seraphis_voice.h`, delete the two early returns at `:770-776` and `:777-783` so both bodies become the
bare forward:

```cpp
/// Phase 11 FR-033a (D1). NOT configure-time gated: SpectralMorphEngine::setState
/// absorbs a live state swap through the FR-047 fade (spectral_morph_engine.h:312,
/// slotContributes() at :558), which Phase 3's FR-042/FR-044 already prove
/// continuity-safe. The Phase 9 gate was SeraphisVoice's own extra restriction and
/// made a Phase 11 partial edit inaudible until the next note-on.
void setSpectralState(int slot, const SpectralState& s) noexcept { morph_.setState(slot, s); }
void setSpectralStateCount(int n) noexcept { morph_.setStateCount(n); }
```

**Scope — exactly two call sites, and nothing else. KEEP-LIST (R-17):**
- `isConfigurable()` (`:908`) is **kept**; every other caller it guards (construction-time seeding, the
  freeze/steal paths) is **unchanged**.
- `rejectedConfigCalls_` (`:1208`) is **kept**.
- `getRejectedConfigureTimeCallCount()` (`:784-786`) is **kept** — SC-028 arm (ii) asserts that counter is
  *unchanged* across a live push, so deleting it as "now dead" deletes the criterion's observable. A build
  where it no longer compiles has removed the observable, not tidied up.

In `spectral_morph_engine.h:199-206`, strike **`setState()` and `setStateCount()`** from the
CONFIGURATION-TIME CALLS sentence, leaving `prepare()`, `reset()` and `setSeed()`. The surrounding prose
already only justifies `reset()` and `setSeed()`, and Phase 3's FR-044 names only those two. **Do not touch
`setState`'s body (`:292-315`)** — that is what makes SC-030 a pure re-run.

**Cost disclosure to carry into T023.** With the gate relaxed, every voice now executes
`isValidSpectralState` + `buildSanitized` (a 64-entry `std::log2` pass, `spectral_morph_engine.h:513`,
`:537-543`) **before** the identity early-out at `:302-305`, and `consumeSpectralSlotHandoff()` re-arms
`spectralRetryMask_ = 0xFFFFu` (`plugins/seraphis/src/processor/processor.cpp:2834`), so one handoff costs
16 voices × 4 slots ≈ **4096 `std::log2`**. **SC-031 (T023) is the measured gate on this**; if it is red,
the push gets cheaper (narrow `spectralRetryMask_`, or add a pre-`applySpectralStates` identity check
against the last-pushed `spectralSlots_`) — the ceiling does not move and the 30 Hz throttle does not drop.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -3
```
Summary line identical to the pre-edit capture; whole suite green; zero warnings;
`getRejectedConfigureTimeCallCount()` still compiles and is still referenced.

---

## GROUP 5 — Layer 3: the fourth macro-target owner (C-10)

### T004 — `SeraphisMacroMatrix` gains the `Effects` owner

**Files**
- Edit: `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`
- Edit: `dsp/tests/unit/systems/seraphis_macro_test.cpp` (already registered — no CMake edit)

**Depends on:** T001. Independent of T002/T003, but sequential because it is the only task in its group.

**Test FIRST** — extend `dsp/tests/unit/systems/seraphis_macro_test.cpp`:

`TEST_CASE("SeraphisMacroMatrix_EffectsOwner_IsAdditive", "[seraphis_macro][phase11]")`

1. **The enum append moved no offset.** Build a reference table of `computeAetherTargets()` outputs over a
   grid of macro settings **before** the change (hard-code the captured values into the test), and assert
   the post-change values are **bit-identical** (`==` on every float field). This is what proves appending
   before `Count` left `aetherFieldIndex`'s window `[kFirstAetherTarget, kFirstAetherTarget +
   kNumAetherTargets)` (`:446-452`) and every `SeraphisAetherTargets` field offset untouched.
2. `computeEffectsTargets()` returns **exactly** `{0.0f, 0.0f}` at the macro neutrals (`neutralFor()`
   returns 0.5 for Gravity and 0 for the rest, `:548-550`) — asserted with `==`, not `Approx`. This exact
   identity is what lets SC-001 keep its exact-equality form.
3. `static_cast<std::size_t>(SeraphisMacroTarget::Count) == 29` and
   `SeraphisMacroMatrix::kNumRows == 32`.
4. Moving `Dissolve` off neutral moves `computeEffectsTargets().delaySend` and nothing in
   `computeAetherTargets()`; moving `Entropy` moves `.wanderDepth` likewise.

**Implement** — plan §3, additive by construction; **no existing line of behaviour moves.**

- Enum: append **`FxDelaySend`, `FxWanderDepth` immediately before `Count`** (`:87-88`), after the Aether
  block. Appending before `Count` keeps every existing target's index unchanged.
- POD beside `SeraphisAetherTargets`, fields **in enum order**, both defaulting to the shipped parameter
  defaults so the composition is an identity at the FR-060 neutrals:
  ```cpp
  struct SeraphisEffectsTargets { float delaySend = 0.0f; float wanderDepth = 0.0f; };
  ```
  (`kFxDelayMixDefault = 0.0f`, `kFxWanderDepthDefault = 0.0f` —
  `plugins/seraphis/src/parameters/effects_params.h:105`, `:117`. No Layer 4 type is named, per `:105`'s
  rule.)
- Constants beside `kFirstAetherTarget` (`:166-168`): `kFirstEffectsTarget`, `kNumEffectsTargets = 2`,
  and `kNumRows = 32` (was 30, `:163`).
- Two rows appended to `kRows` (the table ends at `:434`), both `ModCurve::Linear` (`Stepped` is forbidden
  by `noRowUsesSteppedCurve`, `:495-503`, asserted `:820`) and both `.base = 0.0f`:
  | Macro | Owner | Target | `base` | `amount` |
  |---|---|---|---|---|
  | `Dissolve` | `Effects` | `FxDelaySend` | `0.0f` | `0.35f` — **pilot start; T025 measures and T026 writes back** |
  | `Entropy` | `Effects` | `FxWanderDepth` | `0.0f` | `0.50f` — **pilot start; same** |
- `effectsFieldIndex(SeraphisMacroTarget)` — a `constexpr` window test mirroring `aetherFieldIndex`,
  returning `-1` outside.
- `everyEffectsRowHasAPodField(rows)` — mirrors `:480`'s shape.
- `everyRowOwnerIsValid`'s ladder (`:466-474`) gains the **biconditional** for both owners, so a row that
  names an effects target with a Voice owner is a compile error:
  `isAetherTarget != isAetherOwner → false`; `isEffectsTarget != isEffectsOwner → false`; then the
  remaining owner must be `Voice` or `Engine`.
- `computeEffectsTargets() const noexcept` — a pure reader, a copy of `computeAetherTargets` (`:667-679`)
  for the Effects half. It writes nothing. **`apply(SeraphisEngine&)` (`:623`) gains no line** — an
  Effects-owned target is read by the plugin, exactly as Aether's are. `evaluateAll()` (`:782`) is
  unchanged; it is generic over `kNumTargets`.
- Two new namespace-scope assertions joining the six at `:814-825`:
  ```cpp
  static_assert(SeraphisMacroMatrix::everyEffectsRowHasAPodField(SeraphisMacroMatrix::kRows),
                "C-10: every Effects row must have a 1:1 SeraphisEffectsTargets field");
  static_assert(static_cast<std::size_t>(SeraphisMacroTarget::Count) == 29,
                "C-10 / SC-021(d): 27 pre-Phase-11 targets + EXACTLY 2; a third needs a spec amendment");
  ```
  27 = 19 Voice-owned (`CloudInharmonicity` … `EnvReleaseMs`) + 8 Aether-owned (`AetherMix` …
  `AetherDimensionalityTideDepth`), counted from `:55-89`. If the compiler disagrees with 29, **fix the
  literal to what the enum says and record why** — the assertion's job is to make a *third* addition a
  build break.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SeraphisMacroMatrix*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -3
```
Zero warnings; the additivity case green; every pre-existing macro case still green. **Any pre-existing
test that pins `kNumRows == 30` or `Count == 27` moves in this task** — grep before editing.

---

## GROUP 6 — DSP audibility of the mutators

### T005 — SC-012's acceptance arm and all four SC-013 audibility arms

**Files**
- Create: `dsp/tests/unit/systems/spectral_state_authoring_test.cpp`
- Edit: `dsp/tests/CMakeLists.txt` — append the new TU to the **`dsp_systems_tests`** list.

**Depends on:** T002 (the mutators), T003a (so the file is written against the relaxed voice).

**Why this TU and not `spectral_state_test.cpp`:** `unit/processors/spectral_state_test.cpp` is registered
to `dsp_processors_tests` and cannot host a Layer-3 render. Every arm below needs `SpectralMorphEngine` or
`HarmonicCloud`, i.e. Layer 3.

**Precondition for every FFT arm, stated in dsp-reachable terms:** call
`HarmonicCloud::setDriftDepthCents(0.0f)` (`dsp/include/krate/dsp/systems/harmonic_cloud.h:501`) on the
cloud under test so `BrownianDrift` contributes no detune. **Plugin `ParamID`s — `kCloudDriftDepthId`
(205), `kMacroEntropyId` (104) — do not exist in a `dsp/` TU and must not appear in this file.**

**Test FIRST** — two `TEST_CASE`s:

`TEST_CASE("SpectralState_AuthoredStates_AreAcceptedByMorphEngine", "[spectral_state][phase11]")`
— SC-012's acceptance arm. For every row of T002's table whose post-call state is **valid**: park the
journey on slot 0 (`setStateCount(2)`, `setTargetPosition(0)`, `updateChunk` until
`getTravelPosition() == 0`), snapshot `getOutputRatios()` / `getOutputAmplitudes()`
(`spectral_morph_engine.h:423-425`), call `setState(0, s)`, `updateChunk`, compare. Acceptance ⇔ the output
arrays moved to the new state.
**`getStateCount()` MUST NOT be used** — it returns `numStates_` (`:443`), written only by `setStateCount`
(`:318-328`) and never by `setState` (`:292-314`).
**`isStateFadeActive()` MUST NOT be used alone** — `setState` returns early without arming on an identical
state (`:302-305`) and arms only `if (slotContributes(slot))` (`:311`).
Rows whose post-call state is invalid are excluded; rejection is correct for them.

`TEST_CASE("SpectralState_AuthoringMutators_AreAudible", "[spectral_state][phase11]")` — four sections:

- **(a) `setPartial` moves a partial's pitch.** Load `makeFactoryState(SineStack)` into a slot, park the
  journey, `setPartial(s, 0, 1.5f, s.amplitudes[0])`, render steady state, 4096-point FFT: partial 0's
  peak moves **701.955 ± 5 cents**; no other partial's peak moves more than **2 cents**.
  **The index is pinned at 0 and must stay there** — the authoring window caps the upper edge at
  `(k + 2) / 1.0163049`, and a perfect fifth `1.5·(k+1)` exceeds it for every `k ≥ 1` (at `k = 1`:
  3.000 vs 2.952), so any other index clamps and cannot move a fifth. That is correct clamp-not-swap
  behaviour, not a bug to work around.
- **(b) `blendStates` endpoints render as the endpoints.** `blendStates(a,b,0)` and `(a,b,1)` render within
  `render_fingerprint.h` tolerance (`kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`, `:47-51`) of
  `a` and `b` respectively — the **audio** comparison that helper is calibrated for. `t = 0.5` lands
  strictly between them on spectral centroid.
- **(c) `tiltState` is monotone and absolute.** For `dB ∈ {-12, -6, 0, +6, +12}` applied **to a fresh copy
  of the same source state each time**, rendered spectral centroid is strictly monotonically increasing in
  `dB`. Absoluteness arm: `tiltState(s, -6)` **twice** leaves the render and `s.tiltDbPerOct` equal to a
  single call.
- **(d) `setPartial`'s amplitude argument is live.** `setPartial(s, 0, ratios[0], 0.25f)` then
  `(…, 1.0f)`: partial 0's rendered peak magnitude moves monotonically and by **≥ 6 dB**, while its
  rendered frequency moves **< 2 cents**. This is what makes `EditMessage::b` and FR-028's modifier-drag a
  live path rather than a dead field.

Observe each section red for the right reason before implementing/wiring (for (a): a mutator that stored
nothing leaves the peak where it was).

**Implement:** no production code — this task is tests plus one CMake line. If an arm cannot be made green
without changing T002's implementation, that is a T002 defect: fix it there and re-run T002's suite.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralState_Author*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -3
```
Suite case count up by two; zero warnings.

---

## GROUP 7 — Payload headers (parallel-safe)

Both tasks create **one new header each**, edit **no** shared file, and are registered in the plugin source
list by **T027** (a header needs no CMake entry to compile — the list is for IDE/install grouping). They
are therefore genuinely disjoint and may run in parallel.

### T006 [P] — `CloudFrame`, the DataExchange payload

**Files**
- Create: `plugins/seraphis/src/processor/cloud_frame.h`

**Depends on:** T001.

**Test FIRST** — the failing test is the header's own `static_assert`, plus one case added in **T008**
(this task ships no TU of its own; a header with a wrong `sizeof` fails to compile, which is the strongest
form of red).

**Implement** — plan §5.1. Banner shape copied from `plugins/membrum/src/processor/meters_block.h:8-11`.

```cpp
#pragma once
// Producer: Processor::publishCloudFrame(), audio thread, ONCE per process() call.
// Consumer: Controller::onDataExchangeBlocksReceived() (UI thread).
// One-way. Nothing about editing travels on this queue (C-2 clause 5).
#include <cstdint>

namespace Seraphis {

struct CloudFrame {                        // POD, little-endian, memcpy'd
    std::uint32_t sequence            = 0;    // monotonic; wrap is benign
    std::uint16_t activeVoices        = 0;
    std::uint8_t  focusVoice          = 0;
    std::uint8_t  partialCount        = 0;    // 0 .. HarmonicCloud::kMaxPartials (64)
    float         fundamentalHz       = 0.0f; // UNDETUNED f0
    float         voiceLevel          = 0.0f;
    float         morphTravelPosition = 0.0f;
    float         frequencyHz[64]     = {};   // DRIFT-INCLUSIVE
    float         amplitude  [64]     = {};
    float         position   [64]     = {};   // [-1, +1]
    std::uint64_t maskBits            = 0;    // bit i set <=> partial i masked
    std::uint64_t overriddenBits      = 0;    // bit i set <=> pan and/or mask override
};

// 8 (header) + 12 (three floats) + 768 (three float[64]) = 788, + 4 bytes of
// alignment padding before the first std::uint64_t (which forces alignof == 8)
// = 792, + 16 = 808.
static_assert(sizeof(CloudFrame) == 808, "C-2's pinned layout");

inline constexpr std::uint32_t kCloudFrameUserContextId = 0x53434C44u;  // 'SCLD'

}  // namespace Seraphis
```

**Field order is normative** — it is what produces the 808 and what both sides `memcpy`. Do not reorder to
"remove" the 4 interior padding bytes; T008 handles them by `memset`-once + field assignment so the padding
is deterministically zero in every published block.

**Include discipline:** `<cstdint>` and nothing else. This header lives under `src/processor/` but is
included by the **controller** — that is the sanctioned shared-POD exception (Membrum's `meters_block.h` is
the precedent), not a cross-boundary include, because it names no processor type.

**Verify:** `"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis` still builds
(the header is not yet included anywhere; a syntax error still shows up in T008). A standalone check is
acceptable: temporarily `#include` it from `processor.cpp`, build, revert.

---

### T007 [P] — `EditMessage`, the controller → processor wire format

**Files**
- Create: `plugins/seraphis/src/ui/edit_message.h`

**Depends on:** T001.

**Test FIRST** — the header's `static_assert(sizeof(EditMessage) == 12)`; the behavioural cases land in
T010 (`notify` validation) and T015 (gesture emission).

**Implement** — plan §6.1:

```cpp
#pragma once
#include <cstdint>

namespace Seraphis::UI {

inline constexpr const char* kSeraphisEditMessageId   = "SeraphisEdit";
inline constexpr const char* kSeraphisEditAttributeId = "payload";

struct EditMessage {          // POD; moved as ONE binary attribute
    std::uint8_t  kind  = 0;  // 0 EditorGate, 1 PartialRatioAmp, 2 PartialPan, 3 PartialMask,
                              // 4 BlendStates, 5 TiltState, 6 SlotSelect, 7 BlendBegin
    std::uint8_t  slot  = 0;  // 0..3 morph slot (ignored by kinds 0, 2, 3)
    std::uint16_t index = 0;  // partial index 0..63 (kinds 1, 2, 3)
    float         a     = 0.0f;
    float         b     = 0.0f;
};
static_assert(sizeof(EditMessage) == 12);
inline constexpr std::uint8_t kEditKindCount = 8;

}  // namespace Seraphis::UI
```

Per-kind field semantics, to be written as a comment block in the header (C-5's table):
`kind 0` — `a = 1` open, `a = 0` close. `kind 1` — `a = ratio`, `b = amplitude`.
`kind 2` — `a = position ∈ [-1,+1]`. `kind 3` — `a = 0/1`, the **toggled** value the controller computed
from `CloudFrame::maskBits`. `kind 4` — `a = t`, `b = slot B as float`. `kind 5` — `a = ABSOLUTE dB/oct`.
`kind 6` — `slot` = newly selected slot. `kind 7` — `b = slot B as float`, `slot` = destination.

**This `ui/` header is included by `processor.cpp` and that is deliberate**: it declares a POD and three
`constexpr` strings, includes only `<cstdint>`, and names no VSTGUI type. It is the *wire format*, and it
is the one header under `src/ui/` the processor may see.

**Verify:** same as T006.

---

## GROUP 8 — The cloud-frame producer

### T008 — DataExchange lifecycle, `publishCloudFrame()`, and the full seam set

**Files**
- Edit: `plugins/seraphis/src/processor/processor.h`
- Edit: `plugins/seraphis/src/processor/processor.cpp`
- Create: `plugins/seraphis/tests/integration/cloud_frame_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — append the new TU to `add_executable(seraphis_tests …)`.
  **Do NOT add it to the `set_source_files_properties` `-fno-fast-math` block** (§10.3: only
  `partial_edit_test.cpp` and `ui_negative_control_test.cpp` get the flag).

**Depends on:** T006 (`cloud_frame.h`).

**Test FIRST** — create `plugins/seraphis/tests/integration/cloud_frame_test.cpp` with four cases. All frame
reads go through **`lastPublishedFrameForTest()`, between `process()` calls, never through the DataExchange
queue** — `plugins/seraphis/tests/seraphis_test_fixture.h`'s `ProcessorFixture` does
`initialize(nullptr) → setupProcessing → setActive(true)` (`:177-213`) and **never calls `connect()`**, so
in every plugin-side test the handler is null and no block is ever handed out.

1. `TEST_CASE("Seraphis_CloudFrame_MirrorsCloudAccessors", "[cloud_frame][phase11]")` — SC-006.
   Render a held note with the gate open. For each `i < partialCount`:
   `frequencyHz[i] == getPartialFrequencyHz(i) * getPartialDriftDetune(i)` and
   `amplitude[i] == getPartialCurrentAmplitude(i) * getPartialAntiAliasGain(i)`, both within **1e-6
   relative**, read from the DSP side via `engineForTest()` (`processor.h:303`).
   `partialCount == getActivePartialCount()`. Entries at `i >= partialCount` are **exactly `0.0f`**.
   **(e)** after masking `{3, 17}` and pan-overriding `9`: `maskBits` has exactly bits 3 and 17;
   `overriddenBits` exactly 3, 9 and 17.
   **(f)** sweep `kMorphPositionId` over `{0, .25, .5, .75, 1}` and check `morphTravelPosition` tracks
   `getTravelPosition()` within 1e-6 relative.
2. `TEST_CASE("Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial", …)` — SC-006 arm (g), FR-014, the
   three-clause focus rule. (1) Three overlapping notes: after each note-on, `focusVoice` equals the slot
   with the **greatest** `getVoiceAllocationSerial` among non-idle slots (`seraphis_engine.h:975`; ties are
   impossible, `:966-974`). (2) Release the newest note only: while `getVoiceLevel(prev) >
   kCloudFrameSilenceLevel` the focus slot is **retained** — assert it holds for at least one published
   frame after note-off **and that it is the released slot**, not the next-highest serial. (3) All voices
   silent ⇒ `focusVoice == 0`. (4) `kPolyphonyId = 1`: every arm still terminates and `focusVoice` is
   always 0.
3. `TEST_CASE("Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation", …)` — SC-006 arm (i),
   FR-011. **The one case that builds a peer `IConnectionPoint`** — a minimal test double **local to this
   TU**, never added to `ProcessorFixture`. `connect(peer)` ⇒ `dataExchangeHandlerLiveForTest()` is true;
   `setActive(true) → setActive(false) → setActive(true)` leaves it true and does not double-open;
   `disconnect(peer)` ⇒ the seam returns **false** and `cloudFrameSkippedBlockCountForTest()` resumes rising
   on every gated publish, proving the transport was released rather than merely idled.
4. `TEST_CASE("Seraphis_CloudFrame_PublishesOncePerProcessCall", …)` — SC-007, FR-012. 60 s render with
   MIDI on non-block boundaries and automation forcing the 64-sample subdivision, gate open:
   `cloudFramePublishAttemptCountForTest()` equals **the number of `process()` calls that reached the slice
   loop** — use `effectsStageProcessCalls_`'s accessor as the divisor (incremented at `processor.cpp:1189`,
   which already carries exactly that meaning); **do not add a seventh counter**. And
   `renderSliceCountForTest() > cloudFramePublishAttemptCountForTest()` **strictly**.
   **The divisor is not the host call count**: `process()` has six pre-slice-loop early returns
   (`processor.cpp:978`, `:981`, `:988`, `:992`, `:997`, `:1002-1008`) and `publishCloudFrame()` sits after
   the loop, so an equality against the raw host call count is false about a *correct* build under
   pluginval-5 and real hosts. `cloudFrameSkippedBlockCountForTest()` is reported via `INFO` and asserted
   about **not at all**.

Build and observe: the TU fails to compile (no seams exist). **If, after implementing,
`cloudFramePublishAttemptCountForTest()` reads 0 in the fixture, the body-order rule below was skipped.**

**Implement**

*(a) Handler lifecycle — three `Processor` overrides, verbatim from Membrum
(`plugins/membrum/src/processor/processor.cpp:1136-1163`, `:1111-1126`):*
- forward-declare `namespace Steinberg::Vst { class DataExchangeHandler; }` in `processor.h`; member
  `std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;`
- `connect(IConnectionPoint*)` — call `AudioEffect::connect(other)`; on `kResultTrue` build the config with
  `blockSize = sizeof(CloudFrame)`, `numBlocks = 4`, `alignment = 32`,
  `userContextID = kCloudFrameUserContextId`; `make_unique<DataExchangeHandler>(this, cb)`;
  `onConnect(other, getHostContext())`.
- `disconnect(IConnectionPoint*)` — `onDisconnect(other)`, `.reset()`, then `AudioEffect::disconnect(other)`.
- `setActive(TBool)` (**already overridden**, `processor.cpp:801`) — on `true`, build a `Vst::ProcessSetup`
  from the stored sample rate / max block and call `onActivate(setup)`; on `false`, `onDeactivate()`.
- **Amend the comment at `processor.cpp:794-797`.** It currently says *"Activation does exactly ONE thing,
  and it allocates nothing (SC-026 clause 2)"*. That becomes false: in the SDK's fallback path
  `onActivate → Impl::openQueue` does `make_unique` + `Timer::create` + `aligned_alloc × numBlocks` +
  `allocateMessage` (`extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:76-105`). Rewrite it to
  say activation allocates **only** in the DataExchange queue-open path, on the host thread, inside the
  window `processor.cpp:799-801` already relies on, and that no audio-thread-reachable path gains an
  allocation.
- **Add a scope comment** to `Seraphis_SetActiveDoesNotAllocate`
  (`plugins/seraphis/tests/unit/lifecycle_test.cpp:768-791`) recording that it measures a *disconnected*
  instance — which is exactly the audio-thread-reachable configuration — and that a *connected* instance
  allocates in `onActivate` by SDK design and is out of the criterion's scope. **The test keeps its exact
  `REQUIRE(allocations == 0u)` form**; the criterion is narrowed in T026, not weakened here.

*(b) `publishCloudFrame()` — called ONCE per `process()` call, after the slice loop*, immediately before
the `data.outputs[0].silenceFlags = 0` line (`processor.cpp:~1325`). **Never from `renderSlice`** — the
slice loop subdivides on every MIDI event, on the 2048 cap and on the 64-sample grid (`:1298-1305`), so a
per-slice publish issues up to 8× the frames per block and exhausts the 4-block queue inside one call.

**Body order is NORMATIVE (plan §5.3):**
1. `if (!cloudFrameEnabled_.load(std::memory_order_relaxed)) { return; }` — **the gate is the ONLY
   short-circuit**, and it is an `std::atomic<bool>` (written from `notify` on the message thread, read here
   on the audio thread; a plain cross-thread `bool` is a data race).
2. `++cloudFramePublishAttempts_;` — the ATTEMPT counter, incremented whenever the **gate** is open,
   independently of whether a queue exists.
3. Focus voice by C-2 clause 4: (a) among non-idle slots the greatest allocation serial; (b) else retain the
   previous focus while `getVoiceLevel(prev) > kCloudFrameSilenceLevel`; (c) else slot 0.
4. Fill the **member** `pendingFrame_` field by field (never a stack local): `sequence`, `activeVoices`
   (`getActiveVoiceCount()`), `focusVoice`, `partialCount = min(getActivePartialCount(),
   HarmonicCloud::kMaxPartials)`, `voiceLevel` (`getVoiceLevel(focus)`), `morphTravelPosition`
   (`getVoice(focus).morph().getTravelPosition()`), and
   **`fundamentalHz = (activeVoices > 0) ? cloud.getFundamentalHz() : 0.0f`**
   (`dsp/include/krate/dsp/systems/harmonic_cloud.h:405`, reachable via `engine_->getVoice(focus)`
   (`seraphis_engine.h:955`) → `.cloud()` (`seraphis_voice.h:827`)). **Never `frequencyHz[0]`** — forbidden
   by name and drift-inclusive. The `0.0f`-with-no-voice branch is deliberate and is what SC-024 arm B
   asserts; the shadow otherwise retains the last note forever and the C4 fallback becomes unreachable.
   Then the `i < n` loop (`frequencyHz[i] = getPartialFrequencyHz(i) * getPartialDriftDetune(i)`,
   `amplitude[i] = getPartialCurrentAmplitude(i) * getPartialAntiAliasGain(i)`,
   `position[i] = getPartialPosition(i)`), then **zero-fill `i` in `[n, 64)`** so no entry is ever stale,
   then `maskBits` / `overriddenBits` from the T011 table.
5. **Transport only from here down.** `if (dataExchangeHandler_ == nullptr) { ++cloudFrameSkippedBlocks_;
   return; }`; else `getCurrentOrNewBlock()`, and on an invalid block / null data / `size < sizeof(CloudFrame)`
   also `++cloudFrameSkippedBlocks_; return;`. Otherwise `memcpy` + `sendCurrentBlock()`.

`pendingFrame_` is `std::memset`-to-zero **once in `setupProcessing()`** and only field-assigned thereafter,
so the 4 interior padding bytes are deterministically zero in every published block.

*(c) The seam set* (beside the Phase 9/10 `*ForTest()` block, `processor.h:162-240`; none is called from
`process()`):
```cpp
[[nodiscard]] std::size_t cloudFramePublishAttemptCountForTest() const noexcept;
[[nodiscard]] std::size_t cloudFrameSkippedBlockCountForTest()   const noexcept;
[[nodiscard]] std::size_t renderSliceCountForTest()              const noexcept;
void setCloudFrameGateForTest(bool open) noexcept;                 // writes the same atomic kind 0 writes
[[nodiscard]] bool dataExchangeHandlerLiveForTest() const noexcept;
[[nodiscard]] const CloudFrame& lastPublishedFrameForTest() const noexcept { return pendingFrame_; }
[[nodiscard]] std::uint32_t     cloudFrameSequenceForTest() const noexcept;
[[nodiscard]] bool phase11AtomicsAreLockFreeForTest() const noexcept;   // T022 fills this in
[[nodiscard]] double      cloudFrameStageNsForTest()           const noexcept;
[[nodiscard]] std::size_t cloudFrameStageProcessCallsForTest() const noexcept;
void setCloudFrameInstrumentedForTest(bool on) noexcept;               // per-instance, default OFF
```
`lastPublishedFrameForTest()` returns a `const&` to a member the audio thread writes: **every test must read
it between `process()` calls, never concurrently.** The headless suites are single-threaded and already work
that way.

*(d) Stage instrumentation* — modelled exactly on Phase 10's `effectsStageNsForTest()` (`processor.h:390`),
`effectsStageProcessCallsForTest()` (`:398`) and `setEffectsStageInstrumentedForTest(bool)` over a
`bool effectsStageInstrumented_ = false` (`:474`, `:1061-1063`). **The scoped timer opens OUTSIDE the
`cloudFrameEnabled_` predicate and the divisor counts every `process()` call, not every publish** — that is
what keeps SC-010(b)'s reasoning honest. Instrumentation is **off by default**.

**RT safety of the producer (FR-015, SC-011):** a bounded ≤ 64-iteration read loop over `const` accessors
that are array indexes with a bounds test, plus one 808-byte `memcpy`. No allocation, no lock, no exception,
no I/O, **no transcendental** — `fundamentalHz` is a plain member read, not a `440 * exp2(...)` computation.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_CloudFrame*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_DataExchangeHandler*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; four new cases green; suite case count up by four.

---

## GROUP 9 — The cloud-frame consumer

### T009 — `Controller` as `IDataExchangeReceiver`

**Files**
- Edit: `plugins/seraphis/src/controller/controller.h`
- Edit: `plugins/seraphis/src/controller/controller.cpp`
- Create: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — append the new TU to `add_executable(seraphis_tests …)`
  (no `-fno-fast-math` entry).

**Depends on:** T006, T008.

**Test FIRST** — create `plugins/seraphis/tests/unit/controller/custom_view_test.cpp` (later tasks extend
this same file) with:

`TEST_CASE("Seraphis_Controller_CachesOnlyTheMostRecentBlock", "[controller][phase11]")` — SC-006 arm (h),
FR-016. Both arms are pure function calls; **no processor and no host**.
- Call `Controller::onDataExchangeBlocksReceived` **directly** with one delivery of **three** blocks whose
  `sequence` values increase, and assert `cachedCloudFrame().sequence` equals the **last** one — the "most
  recent wins" rule (`plugins/membrum/src/controller/controller.cpp:1719-1726`). Older blocks are discarded,
  not queued.
- Call `queueOpened` with a `TBool` seeded to `true` and assert it comes back **`false`**
  (`dispatchOnBackgroundThread = false`, FR-016 — UI thread, no mutex needed).

**Implement** — plan §5.4 / §7.1, Membrum's wiring verbatim (`plugins/membrum/src/controller/controller.h:44`,
`:146`, `:365`; `controller.cpp:1696-1740`):

```cpp
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,          // ALREADY PRESENT (controller.h:23-24)
                   public Steinberg::Vst::IDataExchangeReceiver {
    …
    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)
    …
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};  // WITHOUT THIS, NEVER CALLED
    CloudFrame cachedCloudFrame_{};
    [[nodiscard]] const CloudFrame& cachedCloudFrame() const noexcept { return cachedCloudFrame_; }
};
```

- **All three `IDataExchangeReceiver` methods are pure virtual**
  (`extern/vst3sdk/pluginterfaces/vst/ivstdataexchange.h:184`, `:192`, `:207`). Omit any one and
  `Controller` stays abstract and `createInstance`'s `new Controller()` does not compile. Copy the exact
  signatures from Membrum (`controller.h:130-137`).
- `queueClosed(...)` is a no-op (`cachedCloudFrame_` is POD).
- `onDataExchangeBlocksReceived(...)` loops `numBlocks` and `memcpy`s each valid block into
  `cachedCloudFrame_`, so the **last** wins.
- `notify(IMessage*)` (new `EditControllerEx1` override) gains the SDK's IMessage fallback:
  `if (dataExchangeReceiver_.onMessage(message)) return kResultOk;` **before** delegating to
  `EditControllerEx1::notify` — the path a host with no native DataExchange takes
  (`plugins/membrum/src/controller/controller.cpp:1743-1755`).
- `cloud_frame.h` is included here. That is the sanctioned shared-POD exception (see T006), not a
  cross-boundary include.
- **Rewrite the banner at `controller.h:10-11`** — *"NO createCustomView / verifyView overrides (FR-018,
  FR-056 — there are no custom views until Phase 11)"* — in this change or T017 at the latest, whichever
  first contradicts it. FR-052: a file that contradicts itself misleads the next reader.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Controller_CachesOnly*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; the plugin links (a still-abstract `Controller` fails at `createInstance`, not at the header).

---

## GROUP 10 — The edit channel

### T010 — `Processor::notify`, `applyEditMessage`, `stageSlotEdit`, and SC-028/SC-029

**Files**
- Edit: `plugins/seraphis/src/processor/processor.h`
- Edit: `plugins/seraphis/src/processor/processor.cpp`
- Create: `plugins/seraphis/tests/integration/partial_edit_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — append the new TU to `add_executable(seraphis_tests …)`
  **and** to the `set_source_files_properties` `-fno-fast-math` block (it injects bit-pattern non-finite
  payloads).

**Depends on:** T007 (`edit_message.h`), T008 (the processor seams), T003a (the relaxed voice — SC-028/029
are only satisfiable after it).

**Test FIRST** — create `plugins/seraphis/tests/integration/partial_edit_test.cpp`:

1. `TEST_CASE("Seraphis_EditMessage_RejectsGarbage", "[partial_edit][phase11]")` — SC-018, FR-036.
   Fuzz `Processor::notify` with **10 000** messages of random `kind` / `slot` / `index` and **bit-pattern**
   non-finite `a`/`b`. After the run: every slot still satisfies `isValidSpectralState`;
   `spectralSlotsHandoff_` is `-1` or in `[0,3)`; a subsequent render is finite everywhere (bit-pattern
   check, never `std::isnan`).
2. `TEST_CASE("Seraphis_BlendGesture_IsAbsoluteNotCompounding", …)` — SC-025, Q2. `BlendBegin` (kind 7)
   then `t = 0 → 0.5 → 1 → 0.5 → 0`: the selected slot is **byte-identical** to the pristine A snapshot.
   A second gesture (`BlendBegin` at the now-current state, then `t = 1`) lands on **B**, not on a
   doubly-blended state. A kind 4 with **no preceding kind 7** in the same gesture is **dropped** and leaves
   the slot unchanged.
3. `TEST_CASE("Seraphis_EditMode_RatioEditReachesSoundingVoice", …)` — **SC-028**, the criterion T003a
   exists for. Note-on; render past the attack so the focus voice has `hasSounded_` set and is not finished;
   read `getRejectedConfigureTimeCallCount()` (`seraphis_voice.h:784`) as the **pre** value. Park the
   journey on the contributing slot (`slotContributes`, `spectral_morph_engine.h:558`), then send a kind-1
   `setPartial` ratio edit to that slot **mid-note**. Assert: **(i)** within `SpectralMorphEngine`'s existing
   FR-047 absorption window — **Phase 3's time constant; no new one is introduced** — the rendered peak for
   that partial moves to within **5 cents** of the new authored ratio, measured exactly as T005's arm (a)
   measures a peak move (4096-point FFT of steady state) under the drift precondition, expressed here in the
   plugin form: `kCloudDriftDepthId` (205) = 0 with every macro at its FR-060 neutral.
   **(ii)** `getRejectedConfigureTimeCallCount()` is **unchanged** across the push. Arm (ii) is the one that
   fails on an un-relaxed build even if the retry machinery eventually lands the edit.
4. `TEST_CASE("Seraphis_EditMode_LiveRatioEditIsClickFree", …)` — **SC-029**. Reuse **Phase 3's own**
   continuity machinery: the bounds are `SpectralMorphEngine::kMaxAmpDeltaPerChunk = 0.025f`
   (`spectral_morph_engine.h:133`) and `kMaxRatioDeltaCentsPerChunk = 125.0f` (`:134`), **referenced by name
   from the header, never restated as literals** — the FR-044 contributor `static_assert`s at `:156-186` sum
   the enumerated per-chunk contributors against them, so the case cannot be made to pass by loosening a
   constant. The measuring device already exists: `class ChunkDeltaTracker`
   (`dsp/tests/unit/systems/spectral_morph_engine_test.cpp:990-1026`, `worstAmpDelta()` at `:1016`), used by
   `SpectralMorph_TravelIsContinuous` (`:1145`, asserted `:1182`) — **port it or lift it into a shared test
   header**, do not write a second one. Drive it from the plugin-side render while the mid-note ratio edit
   lands and assert no chunk-to-chunk step exceeds either bound across the full absorption window.
   **Sanity arm:** an in-DSP `SpectralMorphEngine::setState` on the same slot at the same instant satisfies
   the **same** bounds — proving the relaxation created no second, worse path. A measured discontinuity
   bound, **never** a bit-exact comparison.

**Implement** — plan §6.2.

*`Processor::notify` (new override):* null message → `AudioEffect::notify`; wrong message ID → same;
`getBinary(kSeraphisEditAttributeId, data, size)` failing, `data == nullptr`, or
`size != sizeof(UI::EditMessage)` → **dropped silently**, `return kResultOk`; otherwise `memcpy` into a
local `EditMessage` and call `applyEditMessage(m)`.

*`applyEditMessage(const EditMessage&)` — message thread.* Validation first (C-5 clause 5), each a silent
return:
```
if (m.kind >= kEditKindCount)                    return;
if (kind in {1,4,5,6,7} && m.slot > 3)           return;
if (kind in {1,2,3}     && m.index >= 64)        return;
if (!isFiniteBits(m.a) || !isFiniteBits(m.b))    return;   // bit pattern, never std::isnan
```
Then the dispatch:

| kind | Action |
|---|---|
| 0 `EditorGate` | `cloudFrameEnabled_.store(m.a != 0.0f, std::memory_order_relaxed);` |
| 1 `PartialRatioAmp` | `stageSlotEdit(m.slot, [&](SpectralState& s){ setPartial(s, m.index, m.a, m.b); });` |
| 2 `PartialPan` | `partialPanStaging_[m.index].store(clamp(m.a,-1,1), relaxed); partialPanOverrideBits_ \|= bit; partialOverridesPending_.store(true, release);` — **NO engine call** |
| 3 `PartialMask` | set/clear bit in `partialMaskBits_` from `m.a != 0.0f`; `partialOverridesPending_.store(true, release);` — **NO engine call** |
| 4 `BlendStates` | if `!blendSnapshotValid_` → **drop**; else `stageSlotEdit(m.slot, [&](SpectralState& s){ s = blendStates(blendSnapshotA_, spectralSlotsAuthoring_[bIdx], m.a); });` |
| 5 `TiltState` | `stageSlotEdit(m.slot, [&](SpectralState& s){ tiltState(s, m.a); });` |
| 6 `SlotSelect` | `selectedEditSlot_ = m.slot; blendSnapshotValid_ = false;` (a slot change ends any gesture) |
| 7 `BlendBegin` | `blendSnapshotA_ = currentSlotForEdit(m.slot); blendSnapshotValid_ = true;` — **writes the ring not at all** |

**Kinds 2 and 3 make no engine call, and that OVERRULES a normative spec sentence** (C-5 clause 1, spec
`:656-658`, scheduled for correction in T026 as D-9 row 9b). The fan-outs write `HarmonicCloud` state
`process()` concurrently reads and writes; calling them here is a data race. *"Allocates nothing, blocks
nothing"* answers allocation, not concurrency. The deferral is the same mechanism `stageSlotEdit` /
`spectralSlotsHandoff_` already implements for kinds 1/4/5, and the same one Membrum's `notify` uses
(`plugins/membrum/src/processor/processor.cpp:1191`). T011 owns the consuming side.

*`stageSlotEdit(slot, mutate)`* — the **one** function that touches the staging ring, reusing `setState`'s
published sequence verbatim (`processor.cpp:1373-1420`):
1. `const std::size_t w = pickStagingBuffer();` — the existing three-buffer chooser that skips both
   `spectralSlotsHandoff_` and `spectralSlotsConsuming_`.
2. Seed the whole buffer from `spectralSlotsAuthoring_` so the three unedited slots are not lost.
3. `mutate(spectralSlotsStaging_[w][slot]);`
4. **Only if the result is valid**, publish: `spectralSlotsHandoff_.store(w, release);
   stagingWriteCursor_ = (w + 1) % 3;` — `SpectralMorphEngine::setState` rejects an invalid state wholesale
   (`spectral_morph_engine.h:296-298`), so publishing one is a *silently inert* edit; checking here turns it
   into a dropped one.
5. `++editStageWrites_;` (test seam).
6. Write the mutated slot back into `spectralSlotsAuthoring_`.

*`spectralSlotsAuthoring_` — the message-thread mirror.* `spectralSlots_` is **audio-thread-owned**
(`processor.h:858`) and `getState`'s own banner says *"IT NEVER READS `spectralSlots_`"*, so `stageSlotEdit`
must not read it either. Add `std::array<SpectralState, 4> spectralSlotsAuthoring_` (message-thread-only),
seeded from `factoryStates_[clampFactoryIndex(id)]` whenever a 409–412 dropdown change is observed
(mirroring `processor.cpp:1383-1385`), overwritten wholesale by `setState()` at the point it seeds
`spectralSlotsStaging_[w]` (`:1382-1386`), and mutated in place by every accepted `stageSlotEdit`.
**Read `getState()`'s existing source of slot payloads before wiring and extend it** rather than adding a
third source.

**RT safety:** `notify()` allocates nothing, blocks nothing, is never reached from `process()`, and touches
no engine-facing state. Its heaviest operation is one `4 × sizeof(SpectralState) = 2160`-byte POD copy plus
one mutator.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_EditM*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_BlendGesture*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; four new cases green; suite count up by four.

---

## GROUP 11 — The override table and its re-push

### T011 — `partialOverrides_`, `repushPartialOverrides()` at all six sites

**Files**
- Edit: `plugins/seraphis/src/processor/processor.h`
- Edit: `plugins/seraphis/src/processor/processor.cpp`
- Edit: `plugins/seraphis/tests/integration/partial_edit_test.cpp` (created in T010)

**Depends on:** T003 (the fan-outs), T010 (the staging + handshake).

**Test FIRST** — extend `partial_edit_test.cpp`:

1. `TEST_CASE("Seraphis_PartialOverrides_SurviveClearingEvents", "[partial_edit][phase11]")` — SC-014,
   FR-030, FR-043. After a kind-2 pan edit on partial `k` (message → `notify` → deferred fan-out), each
   event below leaves `lastPublishedFrameForTest().position[k]` at **`0.8 ± 0.01`** on the next published
   frame:
   (1) a `kCloudStereoSpreadId` (207) change; (2) a `kSeedId` (3) change; (3) an engine `reset()`;
   (4) `kPolyphonyId` 1 → 8, asserting a **newly allocated** voice reports 0.8 and not the default pan;
   (5) `setupProcessing` at a different sample rate — the only criterion for FR-043.
   The **mask** edit is asserted across events **3, 4, 5 only** (mask survives spread and seed by
   construction: `setStereoSpread` and `setSeed` clear only `positionOverridden_`,
   `harmonic_cloud.h:535-547`, `:703`; `reset()` clears both, `:331-332`).
2. `TEST_CASE("Seraphis_PartialOverrides_SurviveAMacroRingSweep", …)` — **SC-014 arm 6**, the defect this
   task exists to catch. Sweep `kMacroBloomId` over `{0, .25, .5, .75, 1}` with the deep 207 knob **held
   still**, and assert `position[k]` is still `0.8 ± 0.01` at every point. Bloom writes `CloudStereoSpread`
   with `.base = 0.35f, .amount = 0.60f` (`seraphis_macro_matrix.h:252-257`) through `macros_.apply()`
   every slice (`processor.cpp:1858` → `:635`), and `setStereoSpread` wipes `positionOverridden_` on any
   **value** change — a tracker keyed on `ParamID` 207 is blind to it and passes arms 1–5 while failing here.
3. `TEST_CASE("Seraphis_PartialMask_ToggleOffRestoresTheVoice", …)` — **SC-033**, the unmask half, end to
   end. **Write it red first against a walk-set-bits re-push body** so the gap is observed, not assumed.
   Hold a note; send kind 3 with `a = 1` for partial `k`; render past the amplitude smoother and assert —
   **through `engineForTest()` on every voice in `[0, kMaxVoices)`, all sixteen** — that partial `k`'s
   current amplitude has decayed to ≈ 0 while partial `k+1`'s has not. Then send kind 3 with `a = 0` for the
   same `k`, render the same span, and assert partial `k`'s amplitude **recovers to within 1 % of its
   pre-mask value on every voice**. Finally assert `lastPublishedFrameForTest().maskBits` bit `k` is clear,
   so the frame and the engine agree.

**Implement** — plan §6.3.

*The four members. The message thread writes them, the audio thread reads them, so every one is atomic:*
```cpp
std::array<std::atomic<float>, 64> partialPanStaging_{};    // last authored position per partial
std::atomic<std::uint64_t> partialPanOverrideBits_{0};      // bit i: partial i has an authored pan
std::atomic<std::uint64_t> partialMaskBits_       {0};      // bit i: partial i is masked
std::atomic<bool>          partialOverridesPending_{false}; // release/acquire handshake
```
The two bitmasks and the pan array use **`relaxed`**; the ordering that matters is
`partialOverridesPending_`'s **release** store on the writer paired with the **acquire** `exchange` on the
reader, which is what makes the pan/mask writes visible before the fan-out reads them. Lock-freedom is
**asserted at runtime** in T022, not assumed.

*The consume point*, once per `process()` call, before the slice loop, beside
`pushSpectralStatesIfPending()` (`processor.cpp:2790-2810`):
```cpp
if (partialOverridesPending_.exchange(false, std::memory_order_acquire)) {
    repushPartialOverrides();          // the fan-out, on the AUDIO thread
}
```
`exchange(false, acquire)` is consume-and-clear: a message arriving *during* the fan-out sets the flag again
and is picked up next call, so no edit is lost and none is applied twice in one block.

*`repushPartialOverrides()` — audio thread, or the host thread with the audio thread stopped.* The body
**walks all 64 indices and pushes BOTH mask polarities**:
```cpp
const std::uint64_t panBits  = partialPanOverrideBits_.load(std::memory_order_relaxed);
const std::uint64_t maskBits = partialMaskBits_.load(std::memory_order_relaxed);
for (std::size_t i = 0; i < 64; ++i) {
    const bool masked = ((maskBits >> i) & 1u) != 0u;
    engine_->setPartialMaskAllVoices(i, /*active=*/!masked);   // harmonic_cloud.h:1082-1089
    if (((panBits >> i) & 1u) != 0u) {
        engine_->setPartialPositionAllVoices(i, partialPanStaging_[i].load(std::memory_order_relaxed));
    }
}
```
**Walking only the set mask bits is a defect, not an optimisation.** Kinds 2/3 make no engine call, so this
is the *only* audio-thread path to `setPartialMask`; re-issuing `active = false` for each set bit means
**clearing** a bit produces no engine call at all and `masked_[i]` stays `true` forever. `clearPartialMaskAllVoices()`
is **not** on this path — it exists for the FR-033 fan-out surface and is exercised by T003's case.

*The six call sites:*

| Event | Thread | Why it clears | Call site |
|---|---|---|---|
| `partialOverridesPending_` (an edit arrived) | audio | the fan-out is deferred off the message thread | before the slice loop, beside `pushSpectralStatesIfPending()` |
| **composed** `CloudStereoSpread` change | audio | `setStereoSpread` → `positionOverridden_.fill(false)` on any value change (`harmonic_cloud.h:535-547`) | in `renderSlice`, immediately after `macros_.apply(*engine_)` (`processor.cpp:1858`) |
| `kSeedId` (3) change | audio | `setSeed` → `positionOverridden_.fill(false)` (`:703`) | after the seed burst (`processor.cpp:~1690`) |
| engine `reset()` / `silence()` | **host ‡** | `reset()` → `positionOverridden_.fill(false); masked_.fill(false);` (`:331-332`) | after every engine reset/silence in `setActive` / `setupProcessing` |
| polyphony increase | audio | a newly-usable slot never received the write | after `setPolyphony` in `pushGlobalParams()` |
| `setupProcessing` re-entry (rate change) | **host ‡** | prepare reaches `cloud_.reset()` | end of `setupProcessing` |

‡ = host thread **with the audio thread stopped**, legal for the reason `processor.cpp:799-801` already
states. The exception is **per-site, never general**: `setState` and any other message-thread path must
publish `partialOverridesPending_` and let `process()` do the fan-out.

*The stereo-spread trigger keys on the COMPOSED value, not on `ParamID` 207.* Cache
`at(v, SeraphisMacroTarget::CloudStereoSpread)` — the value actually pushed — in a
`float lastPushedComposedSpread_` in `renderSlice`, and set the pending flag whenever it differs from the
previous slice's. This catches the deep-parameter path too, because 207 is that target's `setTargetBase`
origin.

*Cost, disclosed for T023.* Two terms per firing slice: an unconditional **mask term** of 64 × 16 = 1024
byte stores (no transcendental — `setPartialMask` is one bounds test and one store), and a **pan term** of
`popcount(panBits) × 16 × 2` trig calls, up to **2048** with all 64 partials overridden (`updatePanGains` →
`equalPowerGains` is `std::cos`/`std::sin`, `dsp/include/krate/dsp/core/crossfade_utils.h:50-53` — **two
trig calls, not two `sqrt`**). With no overrides authored only the mask term remains. The 64-override worst
case is **measured** in T023 (SC-014 arm 7); if it fails, the fan-out gets cheaper (a
`maskDirtyBits_`/`panDirtyBits_` pair under the same handshake, or coalescing the re-push to once per
`process()` call) — never a raised ceiling and never a body that cannot unmask.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_PartialOverrides*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_PartialMask*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; three new cases green; SC-014 arm 7 is deferred to T023.

---

## GROUP 12 — State persistence

### T012 — the appended `[partials]` block, format version unchanged at 3

**Files**
- Edit: `plugins/seraphis/src/processor/processor.cpp`
- Edit: `plugins/seraphis/src/controller/controller.cpp` (the mirror read in `setComponentState`)
- Edit: `plugins/seraphis/tests/unit/state_v3_test.cpp` (already registered; already carries
  `-fno-fast-math`)

**Depends on:** T011 (the table it serializes).

**Test FIRST** — extend `plugins/seraphis/tests/unit/state_v3_test.cpp`:

`TEST_CASE("Seraphis_EditedState_RoundTripsAtV3", "[state][phase11]")` — SC-015, FR-034/034a.
Edit slot 1's ratios, pan-override partial 5 to 0.8, mask partial 9. Then `getState` → `setState` into a
fresh processor → `getState`:
- the two streams are **byte-identical** (Phase 9's FR-094 property);
- the first four bytes decode to **3** (`kCurrentStateVersion`, `plugin_ids.h:27` — **the version does not
  move**);
- slot 1's 541-byte payload deserializes to the edited state;
- the appended **272-byte** `[partials]` block deserializes to the same pan/mask/override values.
Then a stream **truncated immediately before the block** still loads, with every override absent — proving
the EOF-safe strict-prefix chain rather than a version branch. **No FR-094 carve-out is taken.**

**Implement** — plan §6.4. The block is appended **last**, after `[effects]` (`processor.cpp:1408`); the
loader chain stays EOF-safe with **no version-aware branch**, exactly the mechanism `processor.cpp:1395-1400`
documents.

Layout, **272 bytes**:

| Offset | Size | Field |
|---|---|---|
| 0 | 256 | 64 × `float` pan, index order (`writeFloat` / `readFloat`) |
| 256 | 8 | `uint64` panOverrideBits (`writeInt64u` / `readInt64u`) |
| 264 | 8 | `uint64` maskBits (`writeInt64u` / `readInt64u`) |

**Use the 64-bit accessors — they exist.** `IBStreamer` publicly inherits `FStreamer`
(`extern/vst3sdk/base/source/fstreamer.h:202`), which declares public `writeInt64u(uint64)` /
`readInt64u(uint64&)` (and the signed pair) at `fstreamer.h:97-106`, directly callable on an `IBStreamer&`
and already used in this codebase at `plugins/disrumpo/src/processor/processor_state.cpp:356` and `:908`.
**Do not split the masks into four `int32`s** — that is unmotivated work and changes no arithmetic; two
64-bit masks are 16 bytes either way. Spec FR-034a's "≈268 B" is an arithmetic slip corrected in T026.

- `getState()` appends `savePartialOverrides(streamer)` after `saveEffectsParams`.
- `setState()` appends `loadPartialOverrides(streamer)` after `loadEffectsParams`, **EOF-safe**: each
  failing `readFloat`/`readInt64u` leaves everything already read in place and returns. Because the pan
  array is read **before** the masks, a partially truncated block leaves both masks 0 and every pan value
  therefore unreferenced — absent, not garbage.
- After a successful load, `setState` calls the existing `requestPushAllSurfaces()` (`:1418`) **and**
  publishes `partialOverridesPending_.store(true, std::memory_order_release)`. `setState` runs on the
  message thread, so this handshake is the **only** legal way for a loaded override to reach the voices: it
  must not call the fan-outs itself.
- `Controller::setComponentState` gains the mirror read so the controller's view of the table is correct
  after a project reload (the block is read and discarded there today).

**FR-094 survives by construction**: the block stores values, never arithmetic results, so save → load →
save is byte-identical — the same argument Phase 9's `[morph]` payload uses.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_EditedState*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; the round-trip and truncation arms green; every pre-existing state case still green.

---

## GROUP 13 — Macro reach into the effects surface (Phase 10 RQ-4)

### T013 — the composed effects seam

**Files**
- Edit: `plugins/seraphis/src/processor/processor.h`
- Edit: `plugins/seraphis/src/processor/processor.cpp`
- Edit: `plugins/seraphis/tests/integration/effects_chain_test.cpp` (already registered; already carries
  `-fno-fast-math`)
- Edit: `plugins/seraphis/tests/integration/param_cadence_test.cpp` (the 27 → 29 literal)

**Depends on:** T004 (`computeEffectsTargets`).

**Test FIRST** — extend `effects_chain_test.cpp`:

`TEST_CASE("Seraphis_MacroDissolve_ReachesEffects", "[effects][phase11]")` — SC-021(a)(b)(c), FR-037–039.

- **(a)** `kFxDelayMixId` at its shipped `0`; sweep `kMacroDissolveId` over `{0, .25, .5, .75, 1}`
  **allowing exactly one block of settle per point** (the composition is read before
  `pushMacroSurfaces()` refreshes the bases, so a move arrives on the next `process()` call — 10.67 ms at
  512/48 kHz, inside the 20 ms class-(b) smoothing both consumers already impose). The isolated send return —
  Phase 10 SC-003's definition, read as the **mean of the per-channel RMS over `preOutputTapLForTest()`
  (`processor.h:431`) and `preOutputTapRForTest()` (`:434`)** with `preOutputTapTruncatedForTest() == false`
  (`:444`) — grows **strictly monotonically** in RMS from **exactly `0.0` at neutral**. Same shape for
  `kMacroEntropyId` against the wander stage, measured as M/S side-channel RMS.
- **(b)** at every macro neutral, `composedFxDelaySendForTest()` and `composedFxWanderDepthForTest()` are
  **bit-equal (`==`)** to the raw deep atomic, for each of `{0, .25, .5, 1.0}` on the deep knob.
- **(c)** with the deep knob **held still**, moving `kMacroDissolveId` changes `composedFxDelaySendForTest()`
  and moving `kMacroEntropyId` changes `composedFxWanderDepthForTest()` on the **next** `process()` call, and
  `composedEffectsRecomputeCountForTest()` equals the `process()`-call count. A build that read the raw deep
  atomic at `:2351` / `:3052` leaves both unchanged — exactly the RQ-4 defect.
  **`effectsPushes_` is NOT asserted about**: it is incremented only inside `pushEffectsParams()`
  (`processor.cpp:1665`, `:1709 … :1834`), whose ID set excludes 1410 and 1441, so a *correct*
  implementation never moves it here and an assertion on it would fail on correct code.

**Implement** — plan §4.

*(a) The base half.* `pushMacroSurfaces()` (`processor.cpp:2705-2748`) already iterates `t < kNumTargets` and
calls `macros_.setTargetBase(target, baseValueForTarget(target))` under a per-target on-change +
class-(b)-settling guard (`:2731-2746`). Add **two `case`s** to `baseValueForTarget` (whose Aether arm is at
`:2648-2649`):
```cpp
case Target::FxDelaySend:   return effectsParams_.delayMix.load(kRelaxed);     // ID 1410
case Target::FxWanderDepth: return effectsParams_.wanderDepth.load(kRelaxed);  // ID 1441
```
There remains **exactly one** base writer per target. `targetClassBUnsettled(target)` must return the right
answer for both new targets — 1410 and 1441 *are* class-(b) IDs (they ride `fxReturnGainSm_` /
`fxWanderDepthSm_`), so their entries in `kContinuityMechanism[]` and the `targetClassBUnsettled` switch must
name those two smoothers. **Read the existing table before editing; do not invent a row shape.**
`setTargetBasePushes_` grows **27 → 29** at prepare — `param_cadence_test.cpp` carries that literal and moves
in this same task (grep before editing).

*(b) The composed half — substituted reads, not re-pointed guards.* One member and one pre-slice line:
```cpp
// processor.h
Krate::DSP::SeraphisEffectsTargets composedEffects_{};

// processor.cpp, immediately BEFORE updateEffectsBypassState(total) at :1082
composedEffects_ = macros_.computeEffectsTargets();
updateEffectsBypassState(total);
```
Then **three substitutions and nothing else**:

| # | Site | Today | Phase 11 |
|---|---|---|---|
| 1 | `updateEffectsBypassState`, `:2351` | `const float mix = effectsParams_.delayMix.load(kRelaxed);` | `const float mix = std::clamp(composedEffects_.delaySend, 0.0f, 1.0f);` |
| 2 | `setParamSmootherTargets`, `:3052` | `fxWanderDepthSm_.setTarget(effectsParams_.wanderDepth.load(kRelaxed));` | `fxWanderDepthSm_.setTarget(std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f));` |
| 3 | the `fxWanderRuns_` block, `:1126` | `\|\| effectsParams_.wanderDepth.load(kRelaxed) != 0.0f` | `\|\| std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f) != 0.0f` |

**Substitution 3 is the one that decides whether the wander stage runs at all.** With the shipped default
`kFxWanderDepthDefault = 0.0f`, an Entropy-macro-only move would never set the documented ENGAGE predicate at
`:1119-1134`; the stage would engage only through the FR-010a disengage latch at `:1133`, and because
`setParamSmootherTargets()` runs at `:1207` — **after** `:1133` — engagement would land one further block
late, on a path SC-021(a) would then be measuring. `composedEffects_` is assigned before `:1082`, which is
before `:1119`, so it is available there.

`:3051` (`fxReturnGainSm_.setTarget(fxEffectiveReturnGain_)`) needs **no edit** — `fxEffectiveReturnGain_` is
derived from `mix` inside `updateEffectsBypassState` (`:2354`), so substitution 1 carries through to the
send's engage/bypass ramp and to FR-023a's freeze-forced gain unchanged.

**The clamps are required.** `computeEffectsTargets()` returns the RAW sum (range clamping belongs to the
consuming setter, `seraphis_macro_matrix.h:662-666`); both consumers here are unit-range. This is the same
rule `applyAetherTargets` follows.

**Do not move the composition into the slice loop** — FR-012 fixes `updateEffectsBypassState` at once per
`process()` call and the send's chunk machine (`kFxSendChunkSamples = 512`) is not slice-partitionable. The
one-block lag is ruled and accepted.

*(c) Test seams*, beside the Phase 9/10 `*ForTest()` block (`processor.h:162-240`), never called from
`process()`:
```cpp
[[nodiscard]] float composedFxDelaySendForTest()   const noexcept;
[[nodiscard]] float composedFxWanderDepthForTest() const noexcept;
[[nodiscard]] std::size_t composedEffectsRecomputeCountForTest() const noexcept;  // ++ once per process()
```

**FR-039 falls out arithmetically:** at every macro neutral `computeEffectsTargets()` returns the two bases
bit-for-bit, and `std::clamp(x, 0, 1)` on a value the parameter surface already produced in `[0,1]` is the
identity — so the substituted reads are `==` to the raw atomics they replaced and SC-001's exact-equality
form survives.

**Cost:** one `evaluateAll()` (32 rows × one `applyModCurve`) per `process()` call — ~94 Hz × 32 rows, three
orders below SC-009's snapshot budget.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_MacroDissolve*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; SC-021(a)(b)(c) green; `param_cadence_test`'s cadence case green with 29.

---

## GROUP 14 — `MacroRingKnob` and creator registration

### T014 — the ring knob, its `ViewCreatorAdapter`, and `entry.cpp`

**Files**
- Create: `plugins/seraphis/src/ui/macro_ring_knob.h`
- Edit: `plugins/seraphis/src/entry.cpp`

**Depends on:** T001.

**Test FIRST** — the criterion that proves this task (SC-004 arm 1, by `dynamic_cast`) needs the uidesc from
T019, so the *immediate* red is a build-level one: add to `custom_view_test.cpp` (created in T009) a
compile-time arm that will be extended in T024:

```cpp
static_assert(std::is_base_of_v<VSTGUI::CView, Seraphis::UI::MacroRingKnob>);
static_assert(std::is_base_of_v<Krate::Plugins::ArcKnob, Seraphis::UI::MacroRingKnob>);
```
This fails to compile until the header exists. **The instantiation criterion (exactly five `MacroRingKnob`s
in the built frame, identified by `dynamic_cast`) lands in T019**, and that is the one that catches the
silent-fallback hazard the Phase 8 uidesc banner names (`editor.uidesc:3-5`).

**Implement** — plan §8.2:

```cpp
class MacroRingKnob : public Krate::Plugins::ArcKnob {           // plugins/shared/src/ui/arc_knob.h:49
public:
    MacroRingKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* l, int32_t tag)
        : ArcKnob(size, l, tag) {}
    MacroRingKnob(const MacroRingKnob& other) : ArcKnob(other) {}
    void draw(VSTGUI::CDrawContext* context) override;   // ring styling over ArcKnob's arc
    CLASS_METHODS(MacroRingKnob, Krate::Plugins::ArcKnob)
};

struct MacroRingKnobCreator : VSTGUI::ViewCreatorAdapter {       // arc_knob.h:555-564 shape
    MacroRingKnobCreator() { VSTGUI::UIViewFactory::registerViewCreator(*this); }
    VSTGUI::IdStringPtr   getViewName()     const override { return "MacroRingKnob"; }
    VSTGUI::IdStringPtr   getBaseViewName() const override { return VSTGUI::UIViewCreator::kCControl; }
    VSTGUI::UTF8StringPtr getDisplayName()  const override { return "Macro Ring Knob"; }
    VSTGUI::CView* create(const VSTGUI::UIAttributes&, const VSTGUI::IUIDescription*) const override {
        return new MacroRingKnob(VSTGUI::CRect(0, 0, 96, 96), nullptr, -1);
    }
};
inline MacroRingKnobCreator gMacroRingKnobCreator;   // arc_knob.h:714-716's rule
```

- **A `ViewCreatorAdapter`, not `createCustomView` (C-7a):** the knob must accept `control-tag` and every
  `CControl` attribute from the uidesc, which is exactly what `getBaseViewName() → kCControl` buys
  (`arc_knob.h:562-564`). `createCustomView` views are `CView`s the factory does not decorate with
  `CControl` attributes.
- **FR-021 — the perturbation is the real DSP.** The knob does the standard `beginEdit` / `performEdit` /
  `endEdit` on its `ParamID` and **does nothing to the cloud view**. The visible motion in the constellation
  is whatever the next `CloudFrame` reports. **No view-local animation, no synthetic displacement, no
  interpolation toward a target the DSP is not producing.** T024's SC-022(c) asserts this on the view side.
- `entry.cpp` (FR-052) gains `#include <ui/arc_knob.h>` (which registers `gArcKnobCreator`) **and**
  `#include "ui/macro_ring_knob.h"` — an inline creator object only registers in a TU that is actually
  linked. **`toggle_button.h` is NOT included**: the freeze cluster and every drawer toggle are `CCheckBox`
  (FR-025's four permitted classes are `ArcKnob` / `CSlider` / `COptionMenu` / `CCheckBox`), so registering
  `ToggleButton`'s creator would be dead weight and would leave the intent ambiguous.
  **Rewrite the standing prohibition at `plugins/seraphis/src/entry.cpp:12-14`** (*"this file MUST NOT
  include any ui/*.h header … no custom views until Phase 11"*) in the same change.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; the plugin links; the `static_assert` pair compiles.

---

## GROUP 15 — `CloudView`

### T015 — the constellation view: draw, timer, axis map, gestures

**Files**
- Create: `plugins/seraphis/src/ui/cloud_view.h`, `plugins/seraphis/src/ui/cloud_view.cpp`
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — add `../src/ui/cloud_view.cpp` to the test exe's
  **second-compilation** block beside `../src/processor/processor.cpp` (`:36-38`). The plugin source list is
  T027's job.

**Depends on:** T006 (`CloudFrame`), T007 (`EditMessage`), T009 (the controller's frame cache), T018's
send path is **not** required — this task routes through `Controller::sendEditMessage`, which T018 hardens;
if that method does not exist yet, add its minimal recording form here and let T018 extend it.

**Test FIRST** — extend `custom_view_test.cpp` with three cases (all headless, no processor):

1. `TEST_CASE("Seraphis_CloudView_AxisMapIsMonotoneAndClamped", "[cloud_view][phase11]")` — SC-020 arm (f),
   FR-017. Call `yFromHzForTest` over `{1, 19.99, 20, 100, 1000, 20000, 20000.01, 44100}` and
   `xFromPositionForTest` over `{-2, -1, -0.5, 0, 0.5, 1, 2}`, and assert:
   (i) strict monotonicity across the in-span interior;
   (ii) every sub-20 Hz value maps to the **same** `y` as 20 Hz and every super-20 kHz value to the same `y`
   as 20 kHz — clamped, and demonstrably **not wrapped** (a wrap would put 44 100 near the 20 Hz end);
   (iii) `y` is **inverted** — higher Hz ⇒ smaller `y`;
   (iv) the same clamping for `x` at `±1`.
2. `TEST_CASE("Seraphis_CloudView_MaskedPartialStaysAClickTarget", …)` — SC-020 arm (g), Q5. Feed a
   synthetic frame with `maskBits` bit *i* set **and `amplitude[i] == 0`**, call `onTimerForTest()` then
   **`renderForTest()`** (never a bare `draw(nullptr)` — see below), then assert:
   (i) `pointsDrawnForTest()` counts partial *i*, i.e. it is **not culled**;
   (ii) its `DrawnPoint` has `hollow == true` and `radius == kMaskedRingRadius` (> 0), not `kMinRadius`;
   (iii) `hitTestForTest()` at that point returns *i*.
   Complementary case: an **unmasked** partial with `amplitude == 0` draws at `kMinRadius`.
3. `TEST_CASE("Seraphis_CloudView_GesturesEmitTheRightEditMessage", …)` — **SC-032**, FR-028. Drive
   `onMouseDown` / `onMouseMoved` / `onMouseUp` with synthetic `CPoint` / `CButtonState` sequences against a
   fixed synthetic frame, reading `controller.lastSentEditMessageForTest()` after each gesture. Assert all
   four rows:
   (1) plain vertical drag ⇒ `kind == 1`, `a == newRatio` from the inverse map, **`b ==
   slotMirror_[slot].amplitudes[i]` unchanged**;
   (2) **alt** + vertical drag (a `CButtonState` carrying `kAlt` — a plain VSTGUI modifier, **never** a
   platform key API) ⇒ `kind == 1`, **`a` unchanged** and `b == newAmp`;
   (3) horizontal drag (`|dx| >= |dy|`) ⇒ `kind == 2`, `a ∈ [-1,+1]` and equal to the clamped x-map;
   (4) click within `kClickSlopPx` ⇒ `kind == 3` with `a` the **toggled** value computed from `maskBits` —
   assert **both directions** by running it twice against frames whose bit *i* is clear then set, giving
   `a == 1.0f` then `a == 0.0f`.
   `index` equals the hit-tested partial in all four. A view that emitted kind 2 for an alt-drag, never set
   `b`, or sent an unconditional mask passes every other criterion in this phase.

**Implement** — plan §8.1. `namespace Seraphis::UI`, `class CloudView : public VSTGUI::CView`.

*Public surface (header):* constructor `(const CRect&, Controller*)`; `draw`, `attached`, `removed`,
`onMouseDown`, `onMouseMoved`, `onMouseUp` overrides; `enum class Mode { Observe, Edit }` with
`setMode` / `mode()`; `setSelectedSlot(int)`; `CLASS_METHODS(CloudView, VSTGUI::CView)`.

*Test seams:* `invalidCountForTest()`, `drawCountForTest()`, `pointsDrawnForTest()`, `onTimerForTest()`,
`yFromHzForTest(float)`, `xFromPositionForTest(float)`, `struct DrawnPoint { CCoord x, y, radius; bool
hollow; }` + `drawnPointsForTest()`, `hitTestForTest(const CPoint&) → int` (−1 for a miss), and
**`renderForTest()`**.

**`renderForTest()` exists because NOTHING in the harness ever paints.**
`plugins/seraphis/tests/test_helpers/editor_lifecycle_harness.h:98-133` calls only
`IPlugView::attached(nullptr, …)` and `removed()`, and its own banner records that the platform attach is a
no-op (`CFrame::open(nullptr)` returns false harmlessly, `:12-13`). No platform window ⇒ no paint cycle ⇒ no
`CDrawContext` ever exists, and `draw(nullptr)` is not a defined call. `renderForTest()` builds a
`VSTGUI::COffscreenContext` sized to `getViewSize().getSize()`, calls the **same** `draw()` through it, and
returns — so `drawCountForTest()`, `pointsDrawnForTest()` and `drawnPointsForTest()` all reflect the real
body. **Fallback if a leg cannot create an offscreen context headlessly:** factor `draw()` into
`buildPoints(); emit(context);` and have the seam call `buildPoints()` only. What is **not** acceptable is
leaving the criteria pointed at a path the harness cannot enter.

`drawnPointsForTest`' backing vector is **reserved to 64 once in the constructor** and only `clear()`ed +
`push_back`ed during `draw()`, which runs on the UI thread. No audio-thread allocation is implied and
SC-011's corpus does not include `cloud_view.cpp`.

*Timer (C-8, FR-018).* A `VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer>` at **33 ms** (Membrum's rate,
`plugins/membrum/src/ui/pad_grid_view.h:32-34`), created in `attached()` and **cancelled in `removed()`**
(`:37`). Its body reads `controller_->cachedCloudFrame()`, compares `sequence` against the last seen value,
and calls `invalid()` **only when it changed** — an idle transport costs one timer callback and no redraw.
`onTimerForTest()` exposes exactly that body.

*Axis mapping (FR-017).*
- `x = left + (position[i] + 1) * 0.5 * width` — `position[i]` is already `[-1,+1]`
  (`harmonic_cloud.h:986`). **Clamped, never wrapped.**
- `u = (log2(clamp(frequencyHz[i], kViewMinHz, kViewMaxHz)) - log2(kViewMinHz)) /
  (log2(kViewMaxHz) - log2(kViewMinHz))`, drawn **inverted** (`y = bottom - u * height`).
  `kViewMinHz = 20.0f`, `kViewMaxHz = 20000.0f` — a **fixed span, never an autoscale**, so
  `kCloudStereoSpreadId = 0` (64 coincident points) cannot divide by zero. The zero-filled tail is never
  reached: the loop runs `i < partialCount`.
- `radius = kMinRadius + amplitude[i] * (kMaxRadius - kMinRadius)`, with **`kMinRadius == 0`** for unmasked
  partials so "zero radius, not culled" holds literally — a fading partial dissolves rather than vanishing
  with a discontinuity.
- **Masked exception (Q5):** if `maskBits & (1ull << i)`, draw a **hollow ring** at a fixed
  `kMaskedRingRadius > 0` **regardless of `amplitude[i]`**, so a masked partial stays a click target for the
  reverse gesture. This is the one case where radius is not a monotone function of amplitude.
- `overriddenBits` tints the point (display only).

*Null-frame safety (FR-019).* `cachedCloudFrame_` on the controller is a **value member, never a pointer**,
value-initialised — a `draw()` with no frame ever received sees `partialCount == 0` and renders an empty
field. There is no null to dereference; `pointsDrawnForTest()` returns 0 in that state.

*Edit mode (FR-028) — the four gestures:*

| Gesture | Detection | Message |
|---|---|---|
| Vertical drag | `\|dy\| > \|dx\|`, no alt | kind 1: `a = newRatio`, `b = mirror.amplitudes[i]` (unchanged) |
| Alt + vertical drag | `buttons.getModifierState() & kAlt` — plain VSTGUI `CButtonState`, **never a platform key API** | kind 1: `a = mirror.ratios[i]` (unchanged), `b = newAmp` |
| Horizontal drag | `\|dx\| >= \|dy\|` | kind 2: `a = clamp(x → [-1,+1])` |
| Click (no drag) | mouse-up within `kClickSlopPx` of mouse-down | kind 3: `a = (maskBits bit i) ? 0.0f : 1.0f` — a **toggle** |

Hit test: nearest point within `kHitRadiusPx`; masked partials use `kMaskedRingRadius` so they remain
hittable.

*The vertical drag's inverse map (Q6, SC-024) — the load-bearing arithmetic:*
```
referenceHz = (frame.activeVoices > 0 && frame.fundamentalHz > 0) ? frame.fundamentalHz   // UNDETUNED
                                                                  : 261.63f;              // C4
yOf(i)      = yFromHz(slotMirror_[selectedSlot_].ratios[i] * referenceHz)   // from the MIRROR, not the frame
newRatio    = hzFromY(pointerY) / referenceHz
```
**`frequencyHz[i]` is NEVER used in this map.** It is drift-inclusive by definition, so using it would bake
momentary Brownian detune into the stored ratio. The C4 fallback makes authoring work identically with no
note held.

*In Edit mode the constellation still animates* (C-8): only the **dragged** partial is drawn at the pointer;
every other point keeps following the frames.

*Draw order (FR-006).* The cloud view is the **first** child added to the template's content area so the
rings, the Obs/Edit toggle and the drawer draw over it (VSTGUI z-order is child order). T019 places it and
T019's SC-004 arm 3 asserts child index 0.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_CloudView*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; three new cases green.

---

## GROUP 16 — `DrawerContainer`

### T016 — the pull-up drawer, a direct child of the template root

**Files**
- Create: `plugins/seraphis/src/ui/drawer_container.h`, `plugins/seraphis/src/ui/drawer_container.cpp`
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — add `../src/ui/drawer_container.cpp` to the
  second-compilation block.

**Depends on:** T015 (the cloud view whose rect the drawer must not disturb).

**Test FIRST** — extend `custom_view_test.cpp`:

`TEST_CASE("Seraphis_Drawer_DoesNotStopCloudView", "[drawer][phase11]")` — SC-020, FR-018/FR-024.
Headless. With the drawer **open**, feed `N = 60` synthetic `CloudFrame`s with **strictly increasing
`sequence`** through the controller's frame cache, calling `cloudView_->onTimerForTest()` once per frame:
- (a) `invalidCountForTest() == N` **exactly**;
- (b) 30 further timer calls with **no** `sequence` change add **zero**;
- (c) `cloudView_->getViewSize()` **byte-equal** to `(0, 32, 1000, 670)`;
- (d) the same three with the drawer collapsed;
- (e) `drawer_->getViewSize()` **byte-equal** to `DrawerContainer::kCollapsedRect` `(0, 670, 1000, 700)`
  collapsed and `kOpenRect` `(0, 420, 1000, 700)` open.

This measures the **frame → redraw** path, not the bare timer: a headless controller receives no frames, so
any observed *rate* would be the rate the test chose.

**Implement** — plan §8.3:

```cpp
class DrawerContainer : public VSTGUI::CViewContainer {
public:
    static constexpr int kTabCount = 7;   // Cloud, Morph, Body, Atmos, Aether, FX, Life/Env
    static constexpr VSTGUI::CRect kCollapsedRect{0, 670, 1000, 700};
    static constexpr VSTGUI::CRect kOpenRect     {0, 420, 1000, 700};
    explicit DrawerContainer(const VSTGUI::CRect& collapsedRect);
    void setOpen(bool open) noexcept;     // toggles between the two EXACT rects
    [[nodiscard]] bool isOpen() const noexcept;
    void setActiveTab(int index) noexcept;
    [[nodiscard]] int  activeTab() const noexcept;
    bool removed(VSTGUI::CView* parent) override;   // cancels the slide timer
    CLASS_METHODS(DrawerContainer, VSTGUI::CViewContainer)
};
```
(If `CRect` is not usable as a `constexpr` member on every leg, declare the two rects as `constexpr` coordinate
quadruples and build the `CRect` in a `static` accessor — the byte-comparison in SC-020(e) must still be
against the **same** constants, never against re-typed literals in the test.)

- **Two rects and no others** (FR-023, C-1). Both live in the header so the test compares against the same
  constants the implementation uses.
- **`getViewSize()` is in PARENT coordinates, so the drawer MUST be a direct child of the 1000 × 700
  template root** (D-4). This is a hard constraint, not a layout preference: nested two levels deep under a
  `(0, 32, 1000, 700)` container, the declared collapsed rect is absolute `(0, 702, 1000, 732)` — below the
  window, clipped invisible — and SC-020(c)/(e) could never pass. T019 places it accordingly.
- **The seven tab pages are child containers of this container**, exactly one visible at a time via
  `setVisible` — **never** separate `.uidesc` files and **never** a `UIViewSwitchContainer`, which realises
  only the active template and would make `unreachableParams` report six tabs' worth of IDs as unreachable
  while C-3 requires an **empty** allowlist.
- **Opening it never removes, hides, unmounts or resizes the cloud view** (FR-024): the drawer grows upward
  **over** it. The cloud view's `getViewSize()` stays `(0, 32, 1000, 670)` in both states.
- The slide animation, if any, is a short `CVSTGUITimer` **owned by this view and cancelled in `removed()`**.
  A non-animated instant toggle is acceptable and is the fallback if the animation complicates SC-020.
- Drawer contents are plain uidesc controls — `ArcKnob` / `CSlider` / `COptionMenu` / `CCheckBox`, FR-025's
  exact four. **No additional custom class and no `ToggleButton`.**
- **The seven tab titles are `Cloud, Morph, Body, Atmos, Aether, FX, Life/Env`, in that order** (FR-022),
  set as the tab buttons' `title` attributes in the uidesc in document order. T019 asserts the ordered list.
- Created through `createCustomView` (C-7a): `<view class="CViewContainer"
  custom-view-name="DrawerContainer" …/>`. `Controller::createCustomView` (added in T017 or here, whichever
  first needs it) returns it and caches the raw pointer in `drawer_`, zeroed in `willClose()`.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Drawer*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; all five arms green.

---

## GROUP 17 — The sub-controller

### T017 — `SeraphisEditSubController` and every tag-less control's listener

**Files**
- Create: `plugins/seraphis/src/ui/edit_sub_controller.h`, `plugins/seraphis/src/ui/edit_sub_controller.cpp`
- Edit: `plugins/seraphis/src/controller/controller.h`, `plugins/seraphis/src/controller/controller.cpp`
  (`createCustomView`, `createSubController`, `didOpen`, `willClose`, the cached raw pointers)
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — add `../src/ui/edit_sub_controller.cpp` to the
  second-compilation block.

**Depends on:** T015, T016.

**Test FIRST** — extend `custom_view_test.cpp`:

1. `TEST_CASE("Seraphis_Phase11_SubController_OwnsEveryTaglessControl", "[controller][phase11]")` —
   SC-022(b), FR-045. After `exerciseEditorLifecycle`: `createSubController` was called ≥ once and returned
   a non-null `UI::SeraphisEditSubController`; **every control in the table below — including the header
   preset button** — reports that object from `getListener()` and carries its assigned session tag
   (**`≥ 9000`**, so it can never be a `ParamID`); after `willClose()` `subControllerInstances_` is back
   to 0.
2. `TEST_CASE("Seraphis_PresetButton_OpensTheBrowser", …)` — **SC-022(d)**, FR-007. Nothing anywhere
   exercised the preset button. After `exerciseEditorLifecycle`, drive the button's `valueChanged` through
   the sub-controller and assert a `Krate::Plugins::PresetBrowserView` is present in the frame and bound to
   `presetManager_` (`controller.h:43`, `controller.cpp:57`); drive it again and assert the browser is gone.
   Run inside the ASan lifecycle lane so an open browser at `willClose()` is a report, not luck.

**Implement** — plan §8.4:

```cpp
class SeraphisEditSubController : public VSTGUI::DelegationController {
public:
    SeraphisEditSubController(Controller* owner, VSTGUI::IController* parent);
    void           valueChanged(VSTGUI::CControl* control) override;
    VSTGUI::CView* verifyView(VSTGUI::CView*, const VSTGUI::UIAttributes&,
                              const VSTGUI::IUIDescription*) override;
};
```

**It is NOT a `CView`** — `static_assert(!std::is_base_of_v<VSTGUI::CView, SeraphisEditSubController>)` is
part of T024's arm 1.

*Binding.* `sub-controller="SeraphisEdit"` sits on the **template root** (D-4), so every control in the
document — including the header preset button — is inside its sub-tree. VSTGUI honours the attribute on the
template node: `UIDescription::createViewFromNode` reads it for any node
(`extern/vst3sdk/vstgui4/vstgui/uidescription/uidescription.cpp:672-677`), reached from
`UIDescription::createView` at `:778`. `Controller::createSubController` returns one and
`++subControllerInstances_`; the counter is **reset in `willClose()`** (the documented trap).
Tagged controls are unaffected — `DelegationController` forwards `getControlListener` / `valueChanged` to
the parent controller for anything it does not claim
(`vstgui/uidescription/delegationcontroller.h:26`).

*It owns `valueChanged` for every tag-less control* (C-7b, FR-045) — no tag-less control's `valueChanged`
may live on `Controller`, on a `CView` subclass, or nowhere:

| Control | Tag | Drives |
|---|---|---|
| Preset button (header, FR-007) | `kPresetButtonTag` | opens/closes a `Krate::Plugins::PresetBrowserView` over `controller_->presetManager_` |
| 7 drawer tab buttons | `kTabBaseTag + i` (view tags, **not** `ParamID`s) | `drawer_->setActiveTab(i)` |
| Drawer handle | `kDrawerHandleTag` | `drawer_->setOpen(!isOpen())` |
| Obs/Edit toggle | `kModeToggleTag` | `cloudView_->setMode(...)` |
| Blend A→B slider | `kBlendTag` | mouse-down → kind 7 (`BlendBegin`); moves → kind 4; mouse-up → flush |
| Tilt dB control | `kTiltTag` | kind 5, **absolute** dB/oct |
| 4 morph slot selector buttons | `kSlotBaseTag + i` | kind 6, and `controller_->setSelectedSlot(i)` |

*How a tag-less control acquires a tag and a listener — `verifyView`, not the uidesc.* VSTGUI's control
creator sets a listener **only** when a `control-tag` attribute is present
(`vstgui/uidescription/viewcreator/controlcreator.cpp:75-100`); a control with no `control-tag` keeps tag
`-1` and listener `nullptr`. So:
- each tag-less control carries a **custom attribute** `session-tag="<name>"` in the uidesc (a non-standard
  attribute is preserved in `UIAttributes` and ignored by the view factory — Disrumpo's `menu-items` uses
  the same trick, `plugins/disrumpo/src/controller/sub_controllers.h:194-203`);
- `verifyView` reads it and, for a recognised name, does `control->setTag(kSessionTagFor(name));
  control->setListener(this);` before delegating to `DelegationController::verifyView`;
- an **unrecognised** `session-tag` value is a hard failure, not a silent pass: assert in Debug, and
  SC-022(b) asserts every control in the table reports the sub-controller from `getListener()`, so a typo'd
  attribute is a red test.

**`getTagForName` is NOT overridden.** Seraphis has no repeated template needing per-instance `ParamID`s, so
the `DelegationController` forwarding default is correct. Only `valueChanged` and `verifyView` are
overridden. **Session tags live outside the registered ID space (`9000+`) and are never written as
`control-tag`**, so they can never collide with a `ParamID` and can never be picked up by
`extractControlTagMap` — which is what keeps T019's binding count at exactly 110.

*Controller side (plan §7.1):* add the `VST3EditorDelegate` / `IController` overrides `createCustomView`,
`createSubController`, `didOpen`, `willClose`; cache `UI::CloudView* cloudView_`,
`UI::DrawerContainer* drawer_`, `std::array<UI::MacroRingKnob*, 5> macroRings_`, and
`int subControllerInstances_`. **Teardown (C-7c, FR-041):** `willClose()` zeroes every cached raw pointer
and resets `subControllerInstances_ = 0`; each view cancels its own timer in `removed()`. `VST3EditorDelegate`
is **already a base** (`controller.h:23-24`) — do not re-add it.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_SubController*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_PresetButton*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; both cases green.

---

## GROUP 18 — Controller session state

### T018 — editor refcount, the `SpectralState` mirror, throttle + terminal flush

**Files**
- Edit: `plugins/seraphis/src/controller/controller.h`, `plugins/seraphis/src/controller/controller.cpp`
- Edit: `plugins/seraphis/src/parameters/morph_params.h` (**the phase's only change to this file**)
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`
- Edit: `plugins/seraphis/tests/unit/state_v3_test.cpp`
- Edit: `plugins/seraphis/tests/integration/partial_edit_test.cpp`

**Depends on:** T009, T017.

**Test FIRST** — four cases across three files:

1. `custom_view_test.cpp` → `TEST_CASE("Seraphis_MultiEditor_RefcountGatesCorrectly", …)` — SC-026, Q7.
   Two `didOpen` then one `willClose` leaves the refcount at 1 and sends **no** close message; the gate stays
   open and `cloudFramePublishAttemptCountForTest()` keeps incrementing. Closing the second brings it to 0
   and sends **exactly one** close. `terminate()` resets it regardless of prior value.
2. `custom_view_test.cpp` → `TEST_CASE("Seraphis_EditThrottle_FlushesFinalValue", …)` — SC-027, Q8.
   A synthetic drag emitting **200** pointer-moves inside one 33 ms window, then mouse-up: **at most one**
   throttled message for the window, **plus exactly one** terminal message whose payload equals the last
   pointer-move's value, sent unconditionally.
3. `state_v3_test.cpp` → `TEST_CASE("Seraphis_SlotDropdown_DiscardsOnlyThatSlot", …)` — SC-016, FR-035.
   After editing slots 0 and 1, moving ID 409 restores slot 0 to `makeFactoryState()`'s result
   **byte-compared** and leaves slot 1's payload byte-identical. The controller's `slotMirror_[0]` is
   byte-identical to `makeFactoryState()` after the same move.
4. `state_v3_test.cpp` → `TEST_CASE("Seraphis_SlotMirror_ReSeedsFromTheStateStream", …)` — **SC-016 arm 2**,
   FR-046's *second* re-seed source, which is the sole justification for the `morph_params.h` signature
   change. Build a stream carrying four **edited** slot payloads, call `Controller::setComponentState`, and
   assert each `slotMirror_[i]` is **byte-identical** (`std::memcmp` over `sizeof(SpectralState)`) to the
   corresponding `deserializeSpectralState` result. **Arm 2b:** corrupt one payload's version byte — that
   mirror entry must be **byte-unchanged** from its pre-load value while the other three still load **and
   the following 55 parameters still read from the right offset**.
5. `partial_edit_test.cpp` → `TEST_CASE("Seraphis_EditMode_AuthoringWorksWithoutANote", …)` — SC-024, Q6.
   Two identical ratio-drag pointer-delta sequences on the same slot's partial: (A) a voice sounding at
   exactly C4 (`fundamentalHz == 261.63`) with `BrownianDrift` **active**; (B) no voice
   (`activeVoices == 0`, `fundamentalHz == 0`). Both produce the same stored `ratios[index]` within float
   epsilon, and (A) contains **no drift-baked error** — proving the inverse map excludes drift, not merely
   that (B) avoids a divide by zero.

**Implement** — plan §7.2–7.4.

*(a) The editor-open refcount (FR-047):*
- `didOpen(editor)` → `if (editorOpenCount_++ == 0) sendEditMessage({.kind = 0, .a = 1.0f});`
- `willClose(editor)` → zero every cached raw view pointer, `subControllerInstances_ = 0`, then
  `if (--editorOpenCount_ == 0) sendEditMessage({.kind = 0, .a = 0.0f});` with a `< 0` floor guard.
- `terminate()` → `editorOpenCount_ = 0;` — **not `willClose()`**: `willClose` fires once per closing view,
  `terminate` once per plugin instance, and the refcount must survive the former to do its job.
The processor's gate stays the single `std::atomic<bool>`; it never learns the view count.

*(b) The `SpectralState` mirror (C-11, FR-046).* `std::array<Krate::DSP::SpectralState, 4> slotMirror_{}` is
**display-only** — never serialized, never in `process()`, never read back from the processor. Two re-seed
sources and one mutation site:
1. **Dropdown 409–412** — map the normalized value to a `SpectralStateId` via `dropdown_mappings.h`'s
   `kSpectralStateLabels` and assign `slotMirror_[slot] = Krate::DSP::makeFactoryState(id);`
   (`spectral_state.h:373`) — the *same* source the processor's factory-derivation path uses
   (`processor.cpp:1383-1385`), so the two cannot diverge on that path.
2. **State stream** — `loadMorphParamsToController` (`morph_params.h:521-532`) stops discarding the four
   541-byte payloads. Its signature gains a fourth parameter, matching the processor-side loader's shape:
   ```cpp
   inline void loadMorphParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam,
                                           std::array<Krate::DSP::SpectralState, 4>& mirror);
   ```
   The discard loop becomes `deserializeSpectralState(scratch.data(), scratch.size(), mirror[i])`
   (`spectral_state.h:274`) whose **return value is ignored deliberately** — a rejected payload leaves
   `mirror[i]` bitwise untouched (`:264-265`, `:300-305`), which is the correct display fallback, and the
   cursor still advances by the full 541 bytes so the following 55 parameters read from the right offset.
   **Add a default argument or an overload so no other caller changes shape.** This is the **only** change
   this phase makes to `morph_params.h`.
3. **Mutation** — every authoring gesture applies the *same Layer 2 function* to `slotMirror_[slot]` locally
   **in addition to** sending the `EditMessage` (FR-029: the local write must never substitute for the
   send). The two are never reconciled; divergence is cosmetic by construction.

*(c) Gesture throttling and the terminal flush (FR-048).* One small helper owned by the controller, per
gesture:
```cpp
struct EditThrottle {
    std::chrono::steady_clock::time_point lastSend{};
    UI::EditMessage                       pending{};
    bool                                  hasPending = false;
    bool                                  active     = false;
};
```
- `beginGesture()` — `active = true; hasPending = false; lastSend = {}` (so the first move sends).
- `onGestureValue(m)` — `pending = m; hasPending = true;` then
  `if (now - lastSend >= 33ms) { send(pending); lastSend = now; hasPending = false; }`
- `endGesture()` — **unconditionally** `send(pending)` if `hasPending || active`, then `active = false`.
  At most one redundant identical message, **never** a dropped final value.
33 ms is C-8's 30 Hz redraw rate, so the message rate can never exceed the rate the view can show. **The
throttle rate is not a lever** — T023's SC-031 measures the cost at exactly this rate.

*(d) `sendEditMessage(const UI::EditMessage&)`* — `allocateMessage()` →
`setMessageID(kSeraphisEditMessageId)` → `getAttributes()->setBinary(kSeraphisEditAttributeId, &m, sizeof(m))`
→ `sendMessage(msg)` → `msg->release()`. UI thread; a no-op when the component connection is absent
(headless tests). **It records what it was asked to send BEFORE the connection test:**
```cpp
UI::EditMessage lastSentEditMessage_{};   // UI thread only
std::size_t     editMessagesSent_ = 0;    // counts CALLS, not deliveries
[[nodiscard]] const UI::EditMessage& lastSentEditMessageForTest() const noexcept;
[[nodiscard]] std::size_t            editMessageSendCountForTest() const noexcept;
```
Recording **before** the `allocateMessage()` / connection path is what makes SC-032 (T015) and SC-027
observable in a headless controller with no processor attached — otherwise both would be asserting about a
call that provably does nothing.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_MultiEditor*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_EditThrottle*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Slot*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_EditMode_Authoring*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; five new cases green; every pre-existing morph/state case still green (the
`loadMorphParamsToController` signature change must not move any other caller).

---

## GROUP 19 — `editor.uidesc`, replaced wholesale

### T019 — the organism-first layout and the binding budget

**Files**
- Edit: `plugins/seraphis/resources/editor.uidesc` (**replaced wholesale**)
- Edit: `plugins/seraphis/tests/unit/parameter_surface_test.cpp`
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`

**Depends on:** T014, T015, T016, T017 (every class the XML names must exist and be registered first).

**Test FIRST**

*In `parameter_surface_test.cpp`* — extend the existing `Seraphis_ParameterSurface_IsComplete` case
(SC-002, FR-003):
- `getParameterCount() == 107` (`:508`, unchanged — **no parameter is added, removed, retyped, re-ranged or
  re-defaulted by this phase**);
- tag/ID equality both ways (`:700-709`);
- `unreachableParams(xml, ids, {})` — **empty result with an EMPTY allowlist**;
- `extractBoundViews(xml).size() == 110u` (`:714`, was `== 8u`);
- the multiset of bound IDs contains `{1008, 1204, 1430}` **exactly twice each** and every other registered
  ID **exactly once**, asserted against an enumerated allowlist.

And SC-003/FR-004: the per-view class check at `:731` widens `expectedViewClass` (`:488-495`) to a **set**
per kind:
```
R -> { "CSlider", "ArcKnob" }   + the exception { "MacroRingKnob" } ENUMERATED BY ID for 100..104
L -> { "COptionMenu" }
T -> { "CCheckBox" }            # NOT widened
```
**`T` stays a singleton set.** The freeze cluster and every drawer toggle are `CCheckBox`; `ToggleButton`
appears nowhere in the shipped uidesc, and widening `T` would make SC-003 unable to detect either choice.
The `MacroRingKnob` exception is **enumerated by ID in the test**, never a loosened rule.

*In `custom_view_test.cpp`* — `TEST_CASE("Seraphis_Phase11_CustomViews_AreInstantiated", …)`, SC-004:
- **Arm 1.** After `exerciseEditorLifecycle`, walk the frame and count by **`dynamic_cast`**, not by view
  count: exactly **one** `CloudView`, exactly **one** `DrawerContainer`, exactly **five** `MacroRingKnob`.
  This is the arm that catches the Phase 8 banner's hazard (`editor.uidesc:3-5`) — a creator TU that failed
  to link silently yields stock views.
- **Arm 2 (FR-022).** Walk the `DrawerContainer`'s tab buttons in **child order** and assert the title list
  is exactly `{"Cloud", "Morph", "Body", "Atmos", "Aether", "FX", "Life/Env"}` — string-equal, same order,
  size 7. `kTabCount == 7` is a `static_assert` and is **not** a substitute for this.
- **Arm 3, three one-line assertions for three FRs that otherwise have no criterion:**
  (i) **FR-006** — `dynamic_cast<UI::CloudView*>(root->getView(0)) != nullptr`, i.e. child **index 0** of the
  template root. A build that put the cloud view last (hiding every ring behind it) passes arm 1 and SC-020.
  (ii) **FR-025** — walk the drawer's seven page containers and assert `std::count_if(…, isVisible) == 1`;
  then `setActiveTab(i)` for each `i` and re-assert it is still exactly one **and** that it is page `i`.
  (iii) **FR-027** — immediately after `didOpen`, `cloudView_->mode() == CloudView::Mode::Observe`, asserted
  on **every** cycle of the lifecycle loop, not only the first (the failure mode is a mode that survives a
  close).

**Implement** — plan §9. **Fixed 1000 × 700** (RQ-3): no `setAllowedZoomFactors`, no `onSize` relayout.

- The `<control-tags>` block (`editor.uidesc:21-139`) is carried over **verbatim, all 107 entries** — no tag
  added, removed, renamed or re-numbered (FR-002). The Phase 8 placeholder template (`:140-184`) and its
  banner (`:3-5`) are deleted.
- **One sub-controller, on the template root** (D-4): `<template name="editor" class="CViewContainer"
  size="1000, 700" sub-controller="SeraphisEdit">`. Every view below is a **direct child of the root**, so
  `getViewSize()` on the cloud view and on the drawer is in the window's own coordinate space and FR-023 /
  FR-024 / C-1's absolute rects are literally what the XML declares — and the header preset button lands
  inside the sub-controller's sub-tree, which FR-045 requires (D-5).
- Child order (z-order is child order):
  1. `<view class="CView" custom-view-name="CloudView" origin="0, 32" size="1000, 638"/>` — **FIRST**
     (FR-006), so everything below draws over it; `getViewSize() == (0, 32, 1000, 670)` (FR-024).
  2. **Header** `(0,0,1000,32)` — 7 bound views: the `SERAPHIS` label (no tag, not counted); the preset
     button `<view class="COnOffButton" session-tag="preset" …/>` (**no `control-tag`**, not counted in the
     110; tag + listener assigned by `verifyView`); the freeze cluster as three **second** bindings
     `CCheckBox control-tag="AtmosFreeze"` (1008), `"AetherFreeze"` (1204), `"FxSpectralFreeze"` (1430); and
     the four header-exclusive globals `CSlider "MasterGain"` (0, R), `COptionMenu "Polyphony"` (1, L),
     `CCheckBox "SoftLimit"` (2, T), `COptionMenu "Seed"` (3, L).
  3. **Five macro rings** over the cloud view at C-1's anchors, `size="96, 96"`:
     `MacroDream (24, 56)`, `MacroBloom (880, 56)`, `MacroGravity (24, 518)`, `MacroDissolve (880, 518)`,
     `MacroEntropy (452, 556)`.
  4. **Edit mini-toolbar**, tag-less + session-tagged: `COnOffButton session-tag="mode"` (Obs|Edit),
     `CSlider session-tag="blend"`, `CSlider session-tag="tilt"`.
  5. **Drawer**, a **direct child of root**, declared at its **collapsed** rect:
     `<view class="CViewContainer" custom-view-name="DrawerContainer" origin="0, 670" size="1000, 30">`
     containing `COnOffButton session-tag="drawerHandle"`, seven tab buttons
     `session-tag="tab0".."tab6"` with FR-022's exact titles in order, and **seven page containers, all
     present in the XML**, one visible. **Never a `UIViewSwitchContainer`** (R-10).

**The binding budget, and it is exact (C-3, FR-003):**

| Surface | IDs | Bindings |
|---|---|---|
| Header globals | 0, 1, 2, 3 | 4 |
| Header freeze cluster | 1008, 1204, 1430 (second bindings) | 3 |
| Macro rings | 100–104 | 5 |
| Cloud tab | 200–210 | 11 |
| Morph tab | 400–412 (incl. the four slot selectors) | 13 |
| Body tab | 800–812 | 13 |
| Atmos tab | 1000–1016 | 17 |
| Aether tab | 1200–1217 | 18 |
| FX tab | 1400–1443 | 16 |
| Life/Env tab | 600–604, 700–704 | 10 |
| **Total** | | **110** |

107 primary + 3 duplicates. `{1008, 1204, 1430}` is the **complete, enumerated** duplicate allowlist, and
`unreachableParams(xml, ids, {})` must return empty with an **empty** allowlist.

**No registered parameter's type, range, default or `stepCount` changes** (FR-004); `plugin_ids.h:184-240`'s
frozen legend is untouched. Never swap a registered `ParamID`'s view kind in a way that changes its
registered parameter type.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_ParameterSurface*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_CustomViews*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -3
```
Zero warnings; SC-002, SC-003 and SC-004 arms 1–3 green. If `extractBoundViews` returns anything other than
110, **fix the XML, not the number**.

---

## GROUP 20 — The negative control and determinism

### T020 — the producer changes no sample; the frame sequence is reproducible

**Files**
- Create: `plugins/seraphis/tests/integration/ui_negative_control_test.cpp`
- Edit: `plugins/seraphis/tests/integration/cloud_frame_test.cpp`
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — append the new TU to `add_executable(seraphis_tests …)`
  **and** to the `set_source_files_properties` `-fno-fast-math` block (SC-001's exact per-sample
  comparison must not be reshaped by fast-math contraction).

**Depends on:** T008, T011, T013, T019 (everything that runs inside `process()` must be in place before the
negative control means anything).

**Test FIRST**

1. `ui_negative_control_test.cpp` →
   `TEST_CASE("Seraphis_Phase11_OpenGate_ChangesNoSample", "[ui][phase11]")` — SC-001, FR-042.
   **One build, one process, one `Processor` instance.** Arm A: `setCloudFrameGateForTest(true)`; arm B:
   `false`. Identical 10 s MIDI script at the 8-voice operating point, all defaults. Assert
   `cloudFramePublishAttemptCountForTest() > 0` in A and `== 0` in B **before** comparing, then
   `max|a[i] - b[i]| == 0.0f` over both channels.
   **The exact equality is legitimate here and only here**: both arms are the *same compiled code path on the
   same instance*, differing by one bool. A cross-build or cross-toolchain bit-exact comparison is
   **forbidden** (roadmap line 598); `node tools/lint-float-bit-goldens.js` is the gate and this case must
   pass it — state the reasoning in a comment at the assertion.
2. `cloud_frame_test.cpp` → `TEST_CASE("Seraphis_CloudFrame_IsDeterministic", …)` — SC-008. Two runs of the
   same seeded script **in the same process on the same build**. The sequence is accumulated by reading
   `lastPublishedFrameForTest()` **once after every `process()` call** — the producer's own frame, not the
   queue's, so a headless run with zero landed blocks still has a full sequence. Four aggregates over that
   sequence, each compared **relatively at 1e-5**: amplitude-weighted mean pitch `Σaᵢlog2(fᵢ)/Σaᵢ`; its
   total variation; mean amplitude `Σaᵢ/partialCount`; mean position. Plus `sequence` strictly increasing
   (`cloudFrameSequenceForTest()`) and `partialCount` equal frame-for-frame.
   **`render_fingerprint.h`'s constants are deliberately NOT used** — `kSampleTolerance = 1e-4f` is an
   *absolute* per-sample bound on **audio** calibrated against a peak of 2.17, which on absolute Hz would be
   2.5e-8 relative on a 4 kHz partial, below float epsilon. If a pilot run measures a spread above 1e-5 on
   any metric, **the measured number is recorded in the spec (T026) and the criterion re-stated with it** —
   never relaxed to fit a failing run. Same-build determinism only; no cross-toolchain claim.

**Implement:** no production code is expected. If SC-001 fails, something in the publish path is mutating
engine state — find it and remove the mutation; **do not** narrow the comparison.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_OpenGate*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_CloudFrame_IsDeterministic" 2>&1 | tail -5
node tools/lint-float-bit-goldens.js
```
Zero warnings; both cases green; the float-golden lint clean.

---

## GROUP 21 — Lifecycle under the shared harness

### T021 — 10 cycles with the full layout, and an editor that never receives a frame

**Files**
- Edit: `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp`

**Depends on:** T019 (the real uidesc must be in place — a lifecycle pass over the placeholder template
proves nothing).

**Test FIRST**

1. Extend the existing case at `editor_lifecycle_test.cpp:235-262` — SC-005, FR-041:
   `exerciseEditorLifecycle(controller, "editor", …, /*cycles=*/10)`; `getParameterCount() == 107` before
   and after; **zero reports** under `-DENABLE_ASAN=ON` Debug and in the valgrind-nightly `[lifecycle]` lane.
2. `TEST_CASE("Seraphis_Editor_WorksWithNoFrameEverReceived", "[lifecycle][phase11]")` — SC-023, FR-019.
   Gate never opened, `cycles = 10`: the lifecycle completes; `getParameterCount() == 107` before and after;
   and **per cycle, between `attached()` and `removed()`, call `cloudView_->renderForTest()` once**, then
   assert `drawCountForTest() >= 1` and `pointsDrawnForTest() == 0`.
   **`exerciseEditorLifecycle` alone cannot satisfy SC-023 as the spec words it** — it calls only
   `IPlugView::attached(nullptr, …)` and `removed()` (`editor_lifecycle_harness.h:98-133`) and its banner
   records that the platform attach is a no-op (`:12-13`), so `draw()` is never entered and no
   `CDrawContext` exists. Because the helper owns the cycle loop, this arm drives the open/close pair
   directly (the same three calls) rather than through the helper. The spec sentence is re-pointed at the
   `renderForTest()` seam in T026 (D-9 row 9g).
   Run under `-DENABLE_ASAN=ON` Debug so a null-frame dereference is a report, not luck.

**Implement:** no production code expected. Any report under ASan is a real teardown defect — fix it here
(zeroed pointers in `willClose()`, timers cancelled in `removed()`), never by shortening the cycle count.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe "[lifecycle]" 2>&1 | tail -5

# ASan lane (separate build dir, Debug):
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target seraphis_tests
build-asan/bin/Debug/seraphis_tests.exe "[lifecycle]" 2>&1 | tail -20
```
Zero warnings; both cases green in Release **and** under ASan with zero reports.

---

## GROUP 22 — RT safety of the snapshot

### T022 — allocations, exceptions, locks, lock-free atomics, and the platform-API scan

**Files**
- Create: `plugins/seraphis/tests/integration/ui_perf_test.cpp`
- Edit: `plugins/seraphis/src/processor/processor.cpp` (only if `phase11AtomicsAreLockFreeForTest()` still
  needs its body)
- Edit: `plugins/seraphis/tests/CMakeLists.txt` — append the new TU to `add_executable(seraphis_tests …)`.
  **It must NOT be added to the `-fno-fast-math` block** — it carries the `[.perf]` arms T023 adds, and
  `-fno-fast-math` would move the figures those baselines are pinned to (the same rule
  `param_perf_test.cpp` and `effects_perf_test.cpp` already follow). **Create the TU now, before any
  baseline is pinned** — retro-fitting the split later invalidates the baseline.

**Depends on:** T008, T011, T013.

**Test FIRST** — create `plugins/seraphis/tests/integration/ui_perf_test.cpp` with two cases:

1. `TEST_CASE("Seraphis_CloudFrame_AllocatesNothing", "[ui][phase11]")` — SC-011, FR-040. Re-point Phase
   10's instrument (`effects_perf_test.cpp:683-759`, `:872-879`) with **both anti-vacuity guards carried
   over**. Corpus **exactly** `{ src/processor/processor.cpp, src/processor/processor.h,
   src/processor/cloud_frame.h }` (the source roots arrive via the existing `SERAPHIS_SRC_DIR` compile
   definition). Assert `scan.filesMissing == 0`, `scan.codeBytes > 0`, and a **witness count > 0** for the
   token `publishCloudFrame` (the role `runSendStage` plays for Phase 10 at `:692-695`) — a scan that found
   no files must be red, not green. Then 60 s / **5 625 blocks** with the gate open inside
   `TestHelpers::AllocationScope`: `allocations == 0`, `exceptions == 0` through a real `try/catch(...)`,
   zero lock primitives, zero throw sites.
   `spectral_state.h` is **not** scanned — the three mutators are message-thread-only.
   **Plus the lock-free arm:** `CHECK(processor.phase11AtomicsAreLockFreeForTest())`, which ANDs
   `is_lock_free()` over `cloudFrameEnabled_`, `partialOverridesPending_`, both bitmasks and
   `partialPanStaging_[0]`. The constitution's rule is that only `std::atomic_flag` is *guaranteed*
   lock-free, so T011's "lock-free on x86-64/arm64" claim is asserted at runtime rather than assumed. A
   locking atomic on the audio thread is an RT violation and this is the arm that finds it.
2. `TEST_CASE("Seraphis_Phase11_UsesNoPlatformApi", …)` — **SC-011 arm 2, FR-005**. FR-005 is otherwise
   mapped only to SC-019 (builds + portability + clang-tidy), and **a platform-guarded native popup compiles
   clean on all three legs**, so nothing detects it. Reuse the same source-scan instrument with corpus
   `src/ui/*.{h,cpp}` **plus** `src/processor/processor.{h,cpp}` and `src/controller/controller.{h,cpp}`,
   and a **forbidden-token** list: `windows.h`, `HWND`, `CreateWindow`, `MessageBox`, `NSView`, `NSWindow`,
   `NSAlert`, `#import`, `gtk_`, `XCreateWindow`. Carry **both** anti-vacuity guards
   (`filesMissing == 0`, `codeBytes > 0`) and a **witness count > 0** for a token that must be present
   (`VSTGUI::`). Zero hits required.
   **Observe this case fail before trusting it**: temporarily insert `#include <windows.h>` behind a
   platform guard in one `src/ui/` file, confirm red, revert.

**Implement:** `phase11AtomicsAreLockFreeForTest()`'s body if not already written in T008 — a single
expression ANDing the five `is_lock_free()` results. Nothing else.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_CloudFrame_AllocatesNothing" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_UsesNoPlatformApi" 2>&1 | tail -5
```
Zero warnings; both cases green; both observed red first for the right reason.

---

## GROUP 23 — CPU budgets

### T023 — SC-009, SC-010, the worst-case re-push, and SC-031

**Files**
- Edit: `plugins/seraphis/tests/integration/ui_perf_test.cpp` (created in T022)
- Edit: `plugins/seraphis/tests/integration/param_perf_test.cpp` (re-run of the full-poly case with the
  gate open; **do not touch its baseline constants**)

**Depends on:** T022 (the TU), T003a (the relaxation SC-031 gates), T011 (the re-push arm 7 measures).

**Measurement protocol for every arm below** (`param_perf_test.cpp:133-156`): fresh boot, idle machine,
**seven** consecutive runs, **best-of-16 per estimate**, **worst reported**. All four cases are `[.perf]`
and therefore outside the default run.

**Anchors** (recorded in T001): `kFullPolyCeilingNs = 2 666 666.7` (`param_perf_test.cpp:392`);
`kRegressionFactor = 1.15` (`:395`); `kBaselineFullPolyNs = 2318840.0` (`:472`); Phase 10's pinned
worst-of-seven **2 380 980 ns = 22.32 %** ⇒ **2.68 points of headroom**.

**Test FIRST** — four cases:

1. `TEST_CASE("Seraphis_CloudFrame_CpuBudget", "[.perf][phase11]")` — SC-009.
   **(a)** gate open at 8 voices, full surface: worst-of-seven whole-`process()` ns/block
   `<= kFullPolyCeilingNs`. Also re-run `Seraphis_FullPoly_CpuBudget_WithFullSurface` in
   `param_perf_test.cpp` with the gate open.
   **(b)** the snapshot stage alone: `cloudFrameStageNsForTest() / cloudFrameStageProcessCallsForTest()`
   with `setCloudFrameInstrumentedForTest(true)`, **`<= 10 666 ns` per 512-sample block** (0.10 % of one
   core). **The timer scope must open OUTSIDE the `cloudFrameEnabled_` predicate and the divisor must count
   every `process()` call**, not every publish (T008) — that is what makes SC-010(b)'s reasoning hold.
2. `TEST_CASE("Seraphis_CloudFrame_CostsNothingWhenClosed", "[.perf][phase11]")` — SC-010. Gate false, 60 s
   render: (a) `cloudFramePublishAttemptCountForTest() == 0`; (b) **whole-`process()`** best-of-16 ns/block
   at 8 voices `<= 1.15 × 2 318 840` ns. Arm (b) is deliberately the whole-`process()` number: a
   snapshot-stage timer *inside* the gate reads zero by construction and would measure the instrumentation.
3. `TEST_CASE("Seraphis_PartialOverrides_RepushWorstCase", "[.perf][phase11]")` — **SC-014 arm 7**. Author
   **64** pan overrides (all bits set), then sweep `kMacroBloomId` across its full range so the **composed**
   spread changes on consecutive slices and `repushPartialOverrides()` fires repeatedly. Worst-of-seven
   whole-`process()` ns/block at 8 voices must still satisfy `kFullPolyCeilingNs`. This measures the
   64 × 16 × 2 = **2048 transcendental** worst case (`updatePanGains` → `equalPowerGains` is `cos`/`sin`,
   `crossfade_utils.h:50-53` — **two trig calls, not two `sqrt`**) instead of assuming it is bounded.
   **If it fails**, the fan-out gets cheaper — a `maskDirtyBits_`/`panDirtyBits_` pair published under the
   same release/acquire handshake, or coalescing the re-push to once per `process()` call — **never** a
   raised ceiling, and never a body that cannot unmask.
4. `TEST_CASE("Seraphis_EditGestureInFlight_FitsTheBudget", "[.perf][phase11]")` — **SC-031**, the arm
   T003a's relaxation needs and no existing criterion supplies (SC-009/SC-010/arm 7 all run with a **static**
   slot set; SC-029 measures continuity, not time). Hold a note at the 8-voice operating point and drive a
   **30 Hz** kind-1 partial-ratio drag (T018's throttle rate — one accepted `EditMessage`, hence one
   `stageSlotEdit` handoff, every 33 ms), so `consumeSpectralSlotHandoff()` re-arms
   `spectralRetryMask_ = 0xFFFFu` (`processor.cpp:2834`) and `applySpectralStates` runs 16 voices × 4 slots
   of `buildSanitized` ≈ **4096 `std::log2`** roughly every third block at 512 / 48 kHz. Assert
   worst-of-seven whole-`process()` ns/block `<= kFullPolyCeilingNs`.
   **If it fails, the push gets cheaper, in this order:** (1) narrow `spectralRetryMask_` at `:2834` to the
   voices that can still reject — with the gate relaxed a blanket `0xFFFFu` re-arm is pure waste; (2) add a
   pre-`applySpectralStates` identity check against the processor's last-pushed `spectralSlots_` copy so an
   unchanged slot never reaches `buildSanitized`. **Raising the ceiling and dropping below the 30 Hz throttle
   are both forbidden.** Both remedies are inside the plugin; neither is a `dsp/` change.

**Implement:** whichever remedy a red arm forces, plus nothing else. Record every measured figure (all seven
runs, the worst, and the percentage of one core) in the phase notes — T026 needs them and the compliance
table quotes them.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -30
```
All four cases inside their ceilings on the worst of seven runs; figures recorded.

---

## GROUP 24 — The view-surface bound and the view half of FR-021

### T024 — exactly three `CView` classes, and no view-local animation

**Files**
- Edit: `plugins/seraphis/tests/unit/controller/custom_view_test.cpp`

**Depends on:** T014, T015, T016, T017, T019.

**Test FIRST** — two cases:

1. `TEST_CASE("Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController", "[ui][phase11]")` —
   SC-022(a), FR-026. **Two arms, and the first is a compile-time check, not a text scan** — a pure source
   scan cannot resolve transitive bases (`MacroRingKnob` derives from `Krate::Plugins::ArcKnob`
   (`arc_knob.h:49`) → `CKnobBase` → `CControl` → `CView`, a chain living entirely outside `src/ui/`), and a
   fourth class written `: public VSTGUI::CTextLabel` is transitively a `CView` and invisible to a token
   scan. That is exactly the failure mode FR-026 exists to catch, and SC-004 counts *instances* so it cannot
   see a class the uidesc never references.
   **Arm 1 (compile-time):** the TU includes **every** header under `src/ui/` and asserts
   `static_assert(std::is_base_of_v<VSTGUI::CView, T>)` for exactly the three named types
   `{CloudView, MacroRingKnob, DrawerContainer}` plus
   `static_assert(!std::is_base_of_v<VSTGUI::CView, SeraphisEditSubController>)`.
   **Arm 2 (scan as a tripwire, not the proof):** scan `src/ui/*.h` for every `class X : public B` and fail
   on any `B` **not on an enumerated allowlist** `{VSTGUI::CView, VSTGUI::CViewContainer,
   Krate::Plugins::ArcKnob, VSTGUI::DelegationController, VSTGUI::ViewCreatorAdapter}` — an **unknown base
   name is a red test**, never a silent pass. Carries the SC-011 guards (`filesMissing == 0`,
   `codeBytes > 0`, witness count > 0 for the token `CloudView`).
   **Observe arm 1 fail before trusting it**: temporarily add a fourth `src/ui/` class deriving from
   `VSTGUI::CTextLabel`, confirm the assertion set goes red, revert. Adding a fourth view class must force
   either a visible spec amendment (new allowlist entry *and* new `static_assert`) or a failing build.
2. `TEST_CASE("Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally", …)` — **SC-022(c)**, the view half of
   FR-021. FR-021's only other criterion, SC-017, is measured **entirely on the producer**, so a `CloudView`
   that faked the constellation's reaction to a ring would leave `P` untouched and SC-017 would still pass.
   **Arm:** with a **fixed** cached `CloudFrame` (constant `sequence`, never updated), drive
   `MacroRingKnob::valueChanged` / `performEdit` across its full range and assert
   `cloudView_->invalidCountForTest()` and `cloudView_->drawnPointsForTest()` are **unchanged** — no redraw,
   no moved point. The view has no path from a macro value to a point position; its only input is the frame.

**Implement:** no production code expected. A red arm means a view class or an animation path must be
**removed**, not allowlisted.

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_ViewSurface*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_MacroRing_DoesNot*" 2>&1 | tail -5
```
Zero warnings; both cases green; arm 1 observed red first.

---

## GROUP 25 — Macro reach into the constellation, and the two pilot measurements

### T025 — SC-017 and OQ-4

**Files**
- Edit: `plugins/seraphis/tests/integration/cloud_frame_test.cpp`
- Edit: `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` (the two `.amount` literals only)

**Depends on:** T013 (the composed seam), T019, T023 (a pinned perf picture before tuning amounts).

**Test FIRST** — extend `cloud_frame_test.cpp`:

`TEST_CASE("Seraphis_MacroRing_PerturbsConstellation", "[cloud_frame][phase11]")` — SC-017, FR-021.
Headless, on the producer, reading `lastPublishedFrameForTest()` after each `process()` call — *"published
`CloudFrame`s"* means the producer's frame, not a queue delivery.

Metric **`P = Σᵢ aᵢ · log2(fᵢ / f₀) / Σᵢ aᵢ`** over `i < partialCount`, with `f₀ = frame.fundamentalHz` —
dimensionless, octaves above the fundamental.
- **(a)** sweep `kMacroBloomId` over `{0, .25, .5, .75, 1}`: `P` strictly monotonically increasing, and
  `P(1) − P(0) ≥ 0.35 oct` — **a placeholder; the pilot below replaces it with the measured value rounded
  DOWN to two decimals.**
- **(b)** negative control: the same script with Bloom held at neutral at all five points, every other macro
  at neutral, and `kMasterGainId` swept over the same five points instead. The control arm's
  `|ΔP| ≤ 0.1 × ΔP(swept)` **and** below the drift-only spread over the same script.
- **No suppression seam is invented** — `SeraphisMacroMatrix`'s entire mutator surface is `setMacro`
  (`:554`), `setMacros` (`:599`), `setTargetBase` (`:708`); building one would be a `dsp/` addition outside
  the enumerated set. **"No movement" is not asserted and could not be**: per-partial drift runs
  unconditionally and `frequencyHz` is drift-inclusive.

Run under the drift precondition in its plugin form: `kCloudDriftDepthId` (205) = 0 with every macro at its
FR-060 neutral.

**Do — the two pilot measurements (OQ-4). Methodology and acceptance band are RULED; the numbers are
measured, then fixed, and never relaxed afterwards.**

1. **`.amount` for the two new macro rows.**
   - **Starting values, fixed by the ruling and not free:** `0.35f` Dissolve → `FxDelaySend`, `0.50f`
     Entropy → `FxWanderDepth` (already in `kRows` from T004).
   - **Acceptance band, fixed by the ruling:** the isolated send-return RMS at **Dissolve = 1** must land
     between **−20 dB and −6 dB** relative to the dry sum, **and** the five-point sweep must be strictly
     monotone. A value outside the band means the `.amount` moves; **the band does not.**
   - **Procedure:** build with the starting values; run T013's SC-021(a) sweep (one block of settle per
     point); record the isolated send-return RMS — mean of the per-channel RMS over `preOutputTapLForTest()`
     / `preOutputTapRForTest()` with `preOutputTapTruncatedForTest() == false` — at all five Dissolve
     points, and the M/S side RMS at all five Entropy points. **Record the full five-point table**, not just
     the accepted number, so the monotonicity claim is inspectable.
   - **Write-back:** the accepted `.amount` literals go into `kRows` (this task) **and** into spec C-10
     clause 1 (T026); the measured table is appended to the plan's §10.4.
2. **SC-017(a)'s octave threshold.** Run the Bloom sweep once under the drift precondition, compute
   `P(1.0) − P(0.0)`, round **DOWN** to two decimals, and replace the `0.35` placeholder in the test **and**
   in spec SC-017(a) (T026). **If the measured value is below 0.35 the criterion moves, not the
   implementation** — the roadmap's "Bloom pulls partials upward" is what is being measured, and a smaller
   true value is a smaller true value. Raising the implementation's response to hit a pre-guessed number
   would be tuning the instrument to a test.

**Neither number may still read "pilot" or "placeholder" when the compliance table is filled.**

**Verify**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests seraphis_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -3
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_MacroRing_Perturbs*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_MacroDissolve*" 2>&1 | tail -5
```
Zero warnings; SC-017 both arms green with the measured threshold; SC-021(a) still green with the accepted
`.amount`s; the five-point tables recorded.

---

## GROUP 26 — Integration

### T026 — spec corrections (D-3 … D-6, D-9's ten rows) and the two write-backs

**Files**
- Edit: `specs/seraphis-phase11-ui/spec.md`
- Edit: `specs/seraphis-phase11-ui/plan.md` (§10.4's measured tables only)

**Depends on:** T025 (the two measured numbers), and every task whose criterion arms must be written into
the spec.

**Why this task is not optional:** the compliance table is filled against **spec.md's** Success Criteria
list (root `CLAUDE.md`, *Completion Honesty*). An arm that lives only in the plan is invisible at compliance
time and is the first casualty of schedule pressure.

**Five edit groups:**

1. **D-3 — the `[partials]` block.** FR-034a's "≈268 B" becomes **272 B** (64 floats = 256, plus two 64-bit
   masks = 16). And the *rationale* is corrected: the previous claim that `IBStreamer` has no 64-bit integer
   accessor is **false** — `IBStreamer` publicly inherits `FStreamer`
   (`extern/vst3sdk/base/source/fstreamer.h:202`), whose public `writeInt64u`/`readInt64u` are at
   `fstreamer.h:97-106` and are already used at
   `plugins/disrumpo/src/processor/processor_state.cpp:356` and `:908`. The masks move as one 8-byte field
   each; the split changed no arithmetic either way.
2. **D-4 — the sub-controller's placement.** C-7's prose *"the drawer/cloud sub-tree's root container carries
   `sub-controller`"* becomes *"the template root carries it"*. **No number changes**: C-1's table, FR-023 and
   FR-024 all now speak the same absolute coordinate space.
3. **D-5 — the preset button.** Add a `kPresetButtonTag` row for the header preset button to **C-7b's
   table**. FR-045 is **not** carved out — carving it out would have been the weakening resolution.
4. **D-6 — the gate's type.** C-2 clause 6 (and the two other sites that say *plain bool*) become
   `std::atomic<bool>`, relaxed on both sides. Note the sibling atomics: `partialOverridesPending_`
   (release/acquire, because it *does* publish other state), the two override bitmasks and the pan array.
5. **D-9 — ten rows, each naming the sentence and the section that supersedes it:**

| # | Spec sentence | Correction |
|---|---|---|
| 9a | C-4's mask row (`:609`), `setPartialMaskAllVoices(i, !currentMask)` | Polarity-inverted (`masked_[index] = !active`, `harmonic_cloud.h:1082-1089`). Restate as `setPartialMaskAllVoices(i, /*active=*/!desiredMasked)`. |
| 9b | C-5 clause 1 (`:656-658`), *"kinds 2, 3 … call the C-4 fan-outs directly"* | Data race. Restate: kinds 2/3 stage + release-store `partialOverridesPending_`; the **audio** thread performs the fan-out. |
| 9c | C-9 (`:857-859`) and FR-042 (`:1222-1224`), *"the only additions inside `process()` are …"* | Incomplete after 9b. Add `partialOverridesPending_.exchange()` and the deferred `repushPartialOverrides()`. |
| 9d | C-6's `setPartial` no-op list (`:698-699`) | Must name **whole-state invalidity** as well, or SC-012 clause 2's byte-unchanged assertion and the contract disagree. |
| 9e | C-2 clause 7 (the attempt counter) | Must state the counter increments whenever the **gate** is open, independently of whether a queue exists — `ProcessorFixture` never calls `connect()`. |
| 9f | SC-007 and R-1, *"equals the number of `process()` calls"* | False as an invariant (six pre-slice-loop early returns, `processor.cpp:978 … :1008`). Restate as *"`process()` calls that reached the slice loop"*. |
| 9g | SC-023, *"`draw()` is entered … during `exerciseEditorLifecycle`"* | Structurally unreachable (`editor_lifecycle_harness.h:12-13`, `:98-133`). Re-point at the `renderForTest()` seam. |
| 9h | SC-026 clause 2, *"activation allocates nothing"* | `DataExchangeHandler::onActivate` allocates in the SDK fallback (`dataexchange.cpp:76-105`). **Narrow** to *"no allocation on any audio-thread-reachable path"*; the host-thread queue open is out of scope. |
| 9i | SC-013's drift precondition (`:1439-1440`), stated only in plugin `ParamID`s | Unimplementable in the `dsp/` TU that hosts SC-013. Restate in **both** forms: `setDriftDepthCents(0)` (dsp TU) **or** `kCloudDriftDepthId = 0` with every macro at its FR-060 neutral (plugin TU), so SC-013 and SC-028 can both cite it verbatim. |
| 9j | **The Success Criteria list itself** | Write **every** criterion arm the plan's §10 adds into spec.md's Success Criteria with its FR back-reference, and add the matching Traceability rows: SC-004 arms 2 and 3; SC-006 arms (g), (h), (i); SC-011's lock-free and FR-005 arms; SC-014 arms 6 and 7; SC-016 arm 2; SC-020 arms (f) and (g); SC-022 arms (c) and (d); and the four new criteria **SC-028, SC-029, SC-030, SC-031, SC-032, SC-033**. Several are the **only** criterion for an FR (FR-005, FR-006, FR-011, FR-014, FR-016, FR-017, FR-021's view half, FR-022, FR-025, FR-027, FR-028, FR-046's second re-seed source). |

**Plus T025's two write-backs:** the accepted `.amount` literals into spec C-10 clause 1, and SC-017(a)'s
measured octave figure. **No "placeholder" or "pilot" text may survive into compliance.**

**Verify:** every SC named anywhere in the plan's §10 is findable by name in `spec.md`; no spec sentence
contradicts the shipped build; `node tools/gen-specs-index.js` (if it touches this slug) still clean.

---

### T027 — CMake registration, the single authoritative audit pass

**Files**
- Edit: `plugins/seraphis/CMakeLists.txt`
- Edit: `plugins/seraphis/tests/CMakeLists.txt`
- Edit: `dsp/tests/CMakeLists.txt`

**Depends on:** every task that created a file.

**Do — audit and complete, in one pass. Both lists are ENUMERATED, never globbed.**

1. `plugins/seraphis/CMakeLists.txt` `smtg_add_vst3plugin` source list (`:18-46`) — must contain, exactly
   once each: `src/processor/cloud_frame.h`, `src/ui/edit_message.h`, `src/ui/cloud_view.h`,
   `src/ui/cloud_view.cpp`, `src/ui/macro_ring_knob.h`, `src/ui/drawer_container.h`,
   `src/ui/drawer_container.cpp`, `src/ui/edit_sub_controller.h`, `src/ui/edit_sub_controller.cpp`.
2. `plugins/seraphis/tests/CMakeLists.txt` `add_executable(seraphis_tests …)` (`:5`) — must contain, exactly
   once each: `unit/controller/custom_view_test.cpp`, `integration/cloud_frame_test.cpp`,
   `integration/partial_edit_test.cpp`, `integration/ui_negative_control_test.cpp`,
   `integration/ui_perf_test.cpp`.
3. **The second compilation** — the same file lists the plugin `.cpp`s a second time beside
   `${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp` (`:36-38`). It must now also carry
   `../src/ui/cloud_view.cpp`, `../src/ui/drawer_container.cpp`, `../src/ui/edit_sub_controller.cpp`.
   **A new plugin `.cpp` must appear in BOTH lists** — the plugin target and the test exe compile them
   separately.
4. `set_source_files_properties` `-fno-fast-math` block (`:91-116`, guarded by
   `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`) — add **exactly two** entries:
   `integration/partial_edit_test.cpp` and `integration/ui_negative_control_test.cpp`.
   **`integration/cloud_frame_test.cpp` and `integration/ui_perf_test.cpp` must stay OUT** — the perf TU's
   baselines would move under `-fno-fast-math`, the same rule `param_perf_test.cpp` and
   `effects_perf_test.cpp` already follow and already document in that block.
5. `dsp/tests/CMakeLists.txt` — the `dsp_systems_tests` `add_executable` list must contain
   `unit/systems/spectral_state_authoring_test.cpp` and `unit/systems/seraphis_partial_fanout_test.cpp`,
   exactly once each. `unit/processors/spectral_state_test.cpp` is **already** registered to
   `dsp_processors_tests` and stays there. **No new `dsp/` TU is needed for SC-030** — it is a re-run of the
   existing `spectral_morph_*` cases in the same executable.

**Verify — case counts, not exit codes.** Rebuild all three suites and compare the
`All tests passed (N assertions in M test cases)` lines against T001's recorded baseline: `M` must have
grown by the number of cases this phase added. An unregistered TU makes Catch2 report *"No tests ran"*
rather than a failure, so exit 0 proves nothing here.

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests seraphis_tests Seraphis
for t in dsp_processors_tests dsp_systems_tests seraphis_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```

---

### T028 — leaf docs and CHANGELOG

**Files**
- Edit: `plugins/seraphis/CLAUDE.md`
- Edit: `plugins/seraphis/CHANGELOG.md`

**Depends on:** T027.

**Do (FR-053/FR-054):**

1. `plugins/seraphis/CLAUDE.md` — `src/ui/` is **no longer empty**. Record: the custom-view roster (exactly
   three `CView` classes — `CloudView`, `MacroRingKnob`, `DrawerContainer` — plus the
   `SeraphisEditSubController`, which is a `DelegationController` and **not** a view); the cloud-frame data
   path (audio thread → `publishCloudFrame()` once per `process()` call → DataExchange → controller cache →
   30 Hz view timer); the edit channel (UI thread → `IMessage` "SeraphisEdit" → `Processor::notify` →
   staging ring / atomic override table → audio-thread fan-out); the session-tag convention (`9000+`, never
   a `ParamID`); and the rule that the **only** header under `src/ui/` the processor may include is
   `edit_message.h`.
2. `plugins/seraphis/CHANGELOG.md` — a Phase 11 entry naming the organism-first editor, the three custom
   views, the per-partial editing surface, the three `SpectralState` authoring mutators, the Dissolve/Entropy
   reach into the effects surface, and the `[partials]` state block (**format version stays 3**).
   **Do not touch `version.json`** — a version bump is a separate, explicitly requested operation.

**Verify:** `node tools/check-changelog-coverage.js` clean.

---

### T029 — full-suite, portability, static analysis, pluginval

**Files:** none edited (fix-forward only if something is red).

**Depends on:** T028.

**Do — SC-019, FR-044. Capture each command's output to a log on the FIRST run; never re-run a slow tool
just to look at its output.**

```bash
# 1. Full build + every suite this phase touches
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests seraphis_tests Seraphis
for t in dsp_processors_tests dsp_systems_tests seraphis_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done

# 2. Perf arms (outside the CI gate; seven runs, fresh boot, worst reported)
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tee /tmp/seraphis-perf.log | tail -30

# 3. pluginval, strictness 5
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"

# 4. The cheap CI gates — a green build says NOTHING about these
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-float-bit-goldens.js
node tools/lint-arch-guarded-includes.js
node tools/check-changelog-coverage.js

# 5. Static analysis
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja 2>&1 | tee /tmp/seraphis-tidy.log | tail -20

# 6. ASan lifecycle lane
"$CMAKE" --build build-asan --config Debug --target seraphis_tests
build-asan/bin/Debug/seraphis_tests.exe "[lifecycle]" 2>&1 | tail -20
```

**Also required, and not runnable locally:** the macOS leg's `auval -v aumu Srph KrAt` and the Linux leg's
build. **MSVC-green proves nothing about the other two legs** — `node tools/check-portability.js` is the
local proxy, not a substitute.

**Verify:** every command clean; **zero** compiler warnings; **zero** clang-tidy warnings (including in code
this phase did not write but did touch); pluginval strictness 5 clean; all four `[.perf]` arms inside their
ceilings.

---

## Coverage map — every criterion has an owning task

| Criterion | Owning task | Where it lives |
|---|---|---|
| SC-001 (negative control) | T020 | `integration/ui_negative_control_test.cpp` |
| SC-002, SC-003 (surface, view classes) | T019 | `unit/parameter_surface_test.cpp` |
| SC-004 arms 1–3 (custom views, tab titles, FR-006/025/027) | T019 | `unit/controller/custom_view_test.cpp` |
| SC-005 (10-cycle lifecycle) | T021 | `unit/controller/editor_lifecycle_test.cpp` |
| SC-006 arms a–f (frame mirrors accessors) | T008 | `integration/cloud_frame_test.cpp` |
| SC-006 arm (g) (focus rule, FR-014) | T008 | same |
| SC-006 arm (h) (receiver caches newest, FR-016) | T009 | `unit/controller/custom_view_test.cpp` |
| SC-006 arm (i) (handler lifecycle, FR-011) | T008 | `integration/cloud_frame_test.cpp` |
| SC-007 (once per `process()` call that reached the slice loop) | T008 | same |
| SC-008 (determinism) | T020 | same |
| SC-009 (a) (b) (CPU) | T023 | `integration/ui_perf_test.cpp` + `param_perf_test.cpp` |
| SC-010 (closed gate costs nothing) | T023 | `integration/ui_perf_test.cpp` |
| SC-011 + lock-free arm | T022 | same |
| SC-011 arm 2 (FR-005 platform scan) | T022 | same |
| SC-012 clauses 1–3 | T002 | `dsp/tests/unit/processors/spectral_state_test.cpp` |
| SC-012 acceptance arm | T005 | `dsp/tests/unit/systems/spectral_state_authoring_test.cpp` |
| SC-013 (a)–(d) | T005 | same |
| SC-014 arms 1–5 | T011 | `integration/partial_edit_test.cpp` |
| SC-014 arm 6 (macro clearing path) | T011 | same |
| SC-014 arm 7 (worst-case re-push) | T023 | `integration/ui_perf_test.cpp` |
| SC-015 (state round trip, `[partials]`) | T012 | `unit/state_v3_test.cpp` |
| SC-016 (slot dropdown) | T018 | same |
| SC-016 arm 2 (mirror re-seeds from the stream, FR-046) | T018 | same |
| SC-017 (macro perturbs the constellation) | T025 | `integration/cloud_frame_test.cpp` |
| SC-018 (garbage `EditMessage`s) | T010 | `integration/partial_edit_test.cpp` |
| SC-019 (builds, pluginval, portability, tidy) | T029 | CI + local |
| SC-020 arms a–e (drawer never stops the view) | T016 | `unit/controller/custom_view_test.cpp` |
| SC-020 arm (f) (axis map, FR-017) | T015 | same |
| SC-020 arm (g) (masked ring, Q5) | T015 | same |
| SC-021 (a)(b)(c) | T013 | `integration/effects_chain_test.cpp` |
| SC-021 (d) (macro-matrix additivity) | T004 | `dsp/tests/unit/systems/seraphis_macro_test.cpp` |
| SC-022 (a) (view-surface bound, FR-026) | T024 | `unit/controller/custom_view_test.cpp` |
| SC-022 (b) (every tag-less control's listener) | T017 | same |
| SC-022 (c) (no view-local animation, FR-021) | T024 | same |
| SC-022 (d) (preset browser, FR-007) | T017 | same |
| SC-023 (no frame ever received) | T021 | `unit/controller/editor_lifecycle_test.cpp` |
| SC-024 (authoring with no note, Q6) | T018 | `integration/partial_edit_test.cpp` |
| SC-025 (blend is absolute, Q2) | T010 | same |
| SC-026 (multi-editor refcount, Q7) | T018 | `unit/controller/custom_view_test.cpp` |
| SC-027 (throttle's terminal flush, Q8) | T018 | same |
| **SC-028** (live ratio edit reaches a sounding voice) | T010 | `integration/partial_edit_test.cpp` |
| **SC-029** (live edit is click-free) | T010 | same |
| **SC-030** (Phase 3 suites pass unmodified) | T003a | re-run of `spectral_morph_*` in `dsp_systems_tests` |
| **SC-031** (edit gesture in flight fits the budget) | T023 | `integration/ui_perf_test.cpp` |
| **SC-032** (all four FR-028 gestures) | T015 | `unit/controller/custom_view_test.cpp` |
| **SC-033** (mask → unmask reaches every voice) | T011 | `integration/partial_edit_test.cpp` |
| FR-033 (the fan-outs) | T003 | `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` |
| FR-050/051 (CMake registration) | T027 | both `CMakeLists.txt` |
| FR-053/054 (docs) | T028 | `CLAUDE.md`, `CHANGELOG.md` |
| Spec corrections D-3 … D-6, D-9, OQ-4 write-backs | T026 | `spec.md` |

**Registration outside `plugins/`:** none. Seraphis is already in every CI/tooling roster from Phase 8;
Phase 11 adds no new plugin, no new target and no new workflow entry.
