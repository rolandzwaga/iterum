# Tasks: Seraphis Phase 10 — Integrated Effects

**Spec:** `specs/seraphis-phase10-effects/spec.md`
**Plan:** `specs/seraphis-phase10-effects/plan.md`
**Branch:** `feat/seraphis-phase1-life-modulators` (the one branch every Seraphis phase lands on)
**Status:** TASKS — no implementation
**Date:** 2026-08-02

---

## How to read this file

- Tasks are **T001…T038**, grouped into **ordered GROUPS A…N**. Groups run in order; a group does not
  start until the previous group is green.
- Within a group, tasks marked **[P]** touch **fully disjoint NEW files** and may run in parallel.
  Every task that edits a file another task also edits is **sequential** and sits alone or in a stated
  order inside its group.
- **No task in this phase carries `[P]`, and that is a finding, not an omission.** Phase 10 is a serial
  chain: 12 of the 30 tasks edit `plugins/seraphis/src/processor/{processor.h,processor.cpp}` and 9 edit
  `plugins/seraphis/tests/integration/effects_chain_test.cpp`. The only genuinely new files are
  `effects_params.h`, `state_v3_test.cpp`, `effects_chain_test.cpp` and `effects_perf_test.cpp`, and each
  is created by a task whose *content* depends on the task before it. Parallelising any pair here would
  mean two executors editing one file.
- Every task is **self-contained**: the executor is assumed to have read nothing else. Each task states
  the exact files, the **failing test to write FIRST** (file, `TEST_CASE` name, the literal assertions
  and their numbers), then the implementation intent, then the target that verifies it.
- Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
- **No commit tasks.** Commits happen outside this workflow.

### Build / run commands (Windows — always the full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "Effects*" 2>&1 | tail -20
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -30
```

Catch2 filtering is a **positional** argument (`<exe> "TestName*"`); tags go in `[brackets]`.

### Two stated deviations from the plan's §7 task order

1. **The test seams (FR-040 probe, FR-041 counters, the pre-output tap) move EARLIER — to Group G,
   before the send and the wander.** The plan puts them at step 10, but its own step-7 verify list
   ("SC-004, SC-005, SC-017, SC-019 green") is unsatisfiable there: SC-005 and SC-019 are both defined
   to measure at `preOutputTapForTest()` (spec SC-003's isolated-return definition, FR-041 clause 6),
   and SC-003(a)'s mandatory positive control needs probe capability 2. Nothing measurable exists until
   the seams do.
2. **CMake registration of the three new TUs is a single task placed EARLY (T010), not last.**
   `plugins/seraphis/tests/CMakeLists.txt` enumerates its sources and does **not** glob (`:5-44`), so an
   unregistered TU compiles nowhere and its cases silently never run — registering it after the tests are
   written would mean every intervening task's "verify" step was vacuous. Group N still carries the
   integration tasks (full-suite run, `[.perf]` run, portability, lint, pluginval, docs).

### One obligation the spec's FR-038a misses — found while writing this file

FR-038a enumerates **nine** obligations created by growing the surface 91 → 107 and names
`parameter_surface_test.cpp:480` (`CHECK(controller.getParameterCount() == 91)`). It **misses a tenth**:
the same file carries a **91-row `kSurface[]` table** and
`static_assert(kSurfaceRowCount == 91, "SC-001: spec C-6 is a 91-row table (8 shipped + 83 new)")`
(`plugins/seraphis/tests/unit/parameter_surface_test.cpp:207-209`), read back at `:495` and `:501`.
That `static_assert` **fails the build** the moment the 16 IDs register. **T006 discharges it.**
It is called out here so an implementer working from FR-038a's list alone does not stop at an
unexplained compile error.

### Non-obligation, recorded so nobody chases it

`tests/test_helpers/seraphis_chain.h` models the **Phase 7 DSP-level** chain and is included **only** by
`dsp/tests/unit/systems/seraphis_engine_test.cpp`, `seraphis_macro_test.cpp` and
`seraphis_nonfinite_test.cpp` — never by `plugins/seraphis/`. Phase 10's C-1 steps 4 and 5 are
**plugin-level** insertions between `processor.cpp`'s master-gain loop and `processOutputStage`.
**`seraphis_chain.h` is not modified by this phase**, and no `dsp/` file is (`dsp/tests/CMakeLists.txt`
included — spec *Non-goals*: no new DSP class, no `dsp/` behaviour change).

---

## GROUP A — Blocking rulings (no code)

### T001 — Obtain phase-owner rulings on OQ-1…OQ-4 and land the two editorial spec amendments

**Blocking. No task after this may start without it.** Plan §1 escalates four decisions that contradict
the spec as written, plus two editorial corrections the spec's own criteria already force.

**Files edited:** `specs/seraphis-phase10-effects/spec.md` (record every ruling in *Clarifications* and
amend the named clauses); `specs/seraphis-phase10-effects/plan.md` (mark each D-item RESOLVED).

**The four rulings (each states the plan's recommendation; the whole task list below is written against
those recommendations — if a ruling differs, the named tasks change and only those):**

1. **OQ-1 / D-1 — `kFxDelaySyncNoteLabels` (blocks T003, T005, T015).** The spec's ten labels
   `{"1/32","1/16T","1/16","1/8T","1/8","1/4T","1/4","1/2T","1/2","1/1"}` cite
   `spectral_delay.h:529-531`, but `setNoteValue` feeds `dropdownToDelayMs(index, tempo)`
   (`spectral_delay.h:330`) → `getNoteValueFromDropdown` → `kNoteValueDropdownMapping`
   (`dsp/include/krate/dsp/core/note_value.h:136-164`, 30 entries), whose first ten are
   `1/64T, 1/64, 1/64D, 1/32T, 1/32, 1/32D, 1/16T, 1/16, 1/16D, 1/8T`. Index 4 is **1/32**, not 1/8.
   *Plan recommendation:* ship the behaviour-describing labels and move the default index **4 → 7**
   (`1/16`, 0.25 beats, exactly 125.0 ms at 120 BPM), and rewrite SC-001's literal-label clause and
   SC-019 against them.
2. **OQ-2 / D-2 — three writers on `SeraphisEngine::setOutputSaturation` (blocks T008).**
   `processor.cpp:538-539` (prepare-time) and `processor.cpp:1090-1097` (`pushGlobalParams`, on change
   of `kSoftLimitId`) already write it; FR-021 adds a third. *Plan recommendation:* one writer —
   `pushEffectsParams()` — over the composed value `soft ? effectsParams_.saturation : 0.0f`, the
   `pushGlobalParams` block **removed**, the prepare-time push composed and seeded the same way, and
   FR-021's "and nothing else" amended to "and nothing else writes that setter".
3. **OQ-3 / D-3 — FR-008's "free-running absolute chunk grid" is not deliverable (blocks T012, T013).**
   While bypassed the processor may not write the input FIFO (FR-007), so chunk boundaries are at fixed
   offsets from *engage*, not from the render start. *Plan recommendation:* delete `fxPhase_`; the sole
   grid is the input FIFO's own occupancy `fxChunkFill_`; FR-008's reset fires on the fill-chunk
   boundary; FR-008 and FR-003a are amended to: *"the reset lands on a fill-chunk boundary; within a
   continuously-engaged span the chunk phase is a pure function of the samples consumed since engage and
   is therefore independent of how the host partitions them (the property SC-017 tests); across a bypass
   excursion the phase is a function of engage history."* Residual = plan R-12.
4. **OQ-4 / D-4 — the azimuth pan pair must be centre-normalised (blocks T017).** `equalPowerGains` is a
   *crossfade* law; applied to the two channels of one bus it drops a correlated bus **−3.01 dB** at
   centre (`crossfade_utils.h:50-53`, `gL = gR = cos(π/4) = 0.7071`), producing a permanent steady-state
   level step across FR-010's skip boundary the instant `kFxAzimuthDepthId` leaves 0.
   *Plan recommendation:* multiply both gains by `kFxAzimuthCentreComp = 1.41421356f` (√2), so
   `gL² + gR² = 2` at every position and centre is exactly unity per channel; amend C-5's and the Edge
   cases' energy sentences to say "energy-preserving **up to a fixed centre normalisation**, peak
   per-channel gain at full deflection +3.01 dB, bounded by the limiter (SC-006)".

**The two editorial amendments (not escalated — the spec's own criteria already override its FR text):**

5. **D-7 → FR-040.** "sole capability" becomes **three** capabilities, all test-TU-only:
   (1) skip C-1 steps 4 and 5 at runtime; (2) run step 5 **after** step 6 (mandated by SC-003(a)'s
   positive control, `spec.md:1093-1094`); (3) snap the FR-008/FR-009 return-gain ramp to instant
   (mandated by SC-008's positive control (b), `spec.md:1190-1191`).
6. **D-8 → FR-041.** Seam set becomes **seven** counters plus a truncation flag:
   clause 1's counter renamed `effectsStageProcessCallsForTest()` and incremented **once per
   `process()` call** (a per-slice divisor under-reports by up to 8× — plan D-6); a seventh counter
   `sendChunkCountForTest()` (one per `spectralDelay_.process()` call — the only CI-gated observation of
   FR-007); and `preOutputTapTruncatedForTest()` (the tap buffers are pinned to 2048 while the processor
   supports larger host blocks, `processor.cpp:787-792`).

**Verify:** the four rulings are recorded in `spec.md`'s *Clarifications* with the amended clause text in
place, and `plan.md`'s D-1…D-4 are marked RESOLVED. No build step.

---

## GROUP B — Parameter IDs and state version

### T002 — `plugin_ids.h`: the 16 effects IDs, the band end, and state version 3

**Files edited:** `plugins/seraphis/src/plugin_ids.h` (only).

**Failing test FIRST.** This task's gates are **compile-time**, written into the header itself — that is
the shape `plugin_ids.h` already uses (`:256-286`). Add, and see them fail before the enumerators exist:

```cpp
static_assert(kAetherParamRangeEnd < kEffectsParamRangeEnd,
              "the id-dispatch ladder must be strictly increasing");
static_assert(kFxSaturationId >= kAetherParamRangeEnd
                  && kFxAzimuthDepthId < kEffectsParamRangeEnd,
              "spec C-6: effects IDs must lie inside the 1400+ band");
static_assert(kStateVersion1 < kStateVersion2 && kStateVersion2 < kCurrentStateVersion,
              "FR-031: the version chain must be strictly increasing");
```

**Implement.** Append to `enum ParameterIDs` after `kAetherWidthId = 1217`, with exactly these values
(spec C-6):

| | | | |
|---|---|---|---|
| `kFxSaturationId = 1400` | `kFxDelayMixId = 1410` | `kFxDelayTimeId = 1411` | `kFxDelaySpreadId = 1412` |
| `kFxDelaySpreadDirectionId = 1413` | `kFxDelayFeedbackId = 1414` | `kFxDelayTiltId = 1415` | `kFxDelayDiffusionId = 1416` |
| `kFxDelayWidthId = 1417` | `kFxDelaySyncId = 1418` | `kFxDelaySyncNoteId = 1419` | `kFxSpectralFreezeId = 1430` |
| `kFxWidthId = 1440` | `kFxWanderDepthId = 1441` | `kFxWanderRateId = 1442` | `kFxAzimuthDepthId = 1443` |

Add `constexpr Steinberg::Vst::ParamID kEffectsParamRangeEnd = 1500;` to the dispatch ladder
(`:245-252`, which today ends at `kAetherParamRangeEnd = 1400`). Add
`constexpr Steinberg::int32 kStateVersion2 = 2;` beside `kStateVersion1` (`:25`) and move
`kCurrentStateVersion` (`:26`) from 2 to **3**. Extend the frozen-type legend (`:184-240`) with 16 rows:
**R** = 1400, 1410, 1411, 1412, 1414, 1415, 1416, 1417, 1440, 1441, 1442, 1443 (12);
**L** = 1413, 1419 (2); **T** = 1418, 1430 (2). New totals **85 R + 14 L + 8 T = 107**.

**Verify:** `--target seraphis_tests` compiles the header. The build then goes **red on the surface-count
assertions in four other TUs** (91 vs 107) — that is the expected red, discharged by T005–T007 and T023.

---

## GROUP C — Dropdown tables

### T003 — `dropdown_mappings.h`: two label tables, the enum-count sentinel, the converter

**Files edited:** `plugins/seraphis/src/parameters/dropdown_mappings.h` (only).
**Depends on:** T001 ruling 1 (which label set ships), T002.

**Failing test FIRST — compile-time, in the header, in the shape of `:198`'s `kNumMaterials` gate:**

```cpp
static_assert(kSpreadDirectionCount
                  == static_cast<std::size_t>(Krate::DSP::SpreadDirection::CenterOut) + 1u,
              "FR-017: an enum extension must not silently desynchronise this count");
static_assert(kFxSpreadDirectionLabels.size() == kSpreadDirectionCount,
              "FR-017: one label per SpreadDirection enumerator");
static_assert(kFxDelaySyncNoteDefaultIndex >= 0
                  && kFxDelaySyncNoteDefaultIndex < static_cast<int>(kFxDelaySyncNoteLabels.size()),
              "FR-017: the default index must address the shipped label table");
static_assert(Krate::DSP::dropdownToDelayMs(kFxDelaySyncNoteDefaultIndex, 120.0) == 125.0f,
              "FR-017 / plan D-1: the default label must name the period the component produces");
```

The last one is the D-1 self-check. `dropdownToDelayMs` is `constexpr`
(`dsp/include/krate/dsp/core/note_value.h:259`), and `dropdownToDelayMs(7, 120.0)` is
`0.25 beats × 60000/120` in `double` with a single narrowing cast (`note_value.h:147`, `:226-241`) =
**exactly `125.0f`**. If T001 ruled for the spec's original set, the literal changes to whatever
`dropdownToDelayMs(4, 120.0)` actually returns — **do not change the assertion to `!=` or delete it.**

**Implement.**

- `inline constexpr std::size_t kSpreadDirectionCount = 3;` with `spectral_delay.h:53-57` cited beside
  it (the DSP header declares **no** enumerator-count sentinel, and *Non-goals* forbids adding one).
- `inline constexpr std::array<const Steinberg::Vst::TChar*, 3> kFxSpreadDirectionLabels =
  {STR16("Low \xE2\x86\x92 High"), STR16("High \xE2\x86\x92 Low"), STR16("Center \xE2\x86\x92 Out")};`
  in `SpreadDirection`'s **declaration order**.
- `inline constexpr std::array<const Steinberg::Vst::TChar*, 10> kFxDelaySyncNoteLabels` = the set T001
  ruled for. Under the plan's recommendation:
  `{"1/64T","1/64","1/64D","1/32T","1/32","1/32D","1/16T","1/16","1/16D","1/8T"}` and
  `inline constexpr int kFxDelaySyncNoteDefaultIndex = 7;`.
- `toSpreadDirection(int)` / `fromSpreadDirection(SpreadDirection)` in the shape of `toBodyMaterial`
  (`:221-224`), with `std::clamp(index, 0, kSpreadDirectionCount - 1)`.
- `#include <krate/dsp/effects/spectral_delay.h>` (Layer 4 into a plugin header is legal — `processor.h`
  already includes `aether_reverb.h`).
- **Do not touch and do not reuse `kSyncNoteLabels`** (`:135`, 8 entries, a different ordering) — FR-017
  forbids it for ID 1419.

**Verify:** `--target seraphis_tests` compiles; all four `static_assert`s pass.

---

## GROUP D — The parameter pack

### T004 — `effects_params.h` (NEW): the six-function contract and the FR-016a helper

**Files created:** `plugins/seraphis/src/parameters/effects_params.h`.
**Depends on:** T002, T003.

**Failing test FIRST — compile-time, in the new header:**

```cpp
static_assert(tiltCompensatedFeedback(kFxDelayFeedbackMax, 1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, -1.0f) == 0.475f
                  && tiltCompensatedFeedback(kFxDelayFeedbackMax, 0.0f) == kFxDelayFeedbackMax,
              "FR-016a: the worst tilted bin must land back at the registered cap");
```

**Implement**, copying `plugins/seraphis/src/parameters/aether_params.h`'s shape verbatim
(`:15-23` constants, `:73` struct, `:100`/`:161`/`:221`/`:274`/`:297`/`:347` functions).

*Named range/default constants (FR-015 — each transcribed once, with the DSP source line cited beside
it; never re-typed at a use site):*
`kFxDelayTimeMinMs/MaxMs` ← `SpectralDelay::kMinDelayMs/kMaxDelayMs` (`:91-92`, 0 / 2000);
`kFxDelaySpreadMinMs/MaxMs` ← `:95-96` (0 / 2000); `kFxDelayTiltMin/Max` ← `:101-102` (−1 / +1);
`kFxWidthMinPercent/MaxPercent` ← `MidSideProcessor::kMinWidth/kMaxWidth` (`:65-66`, 0 / 200);
`kFxDelayFeedbackMax = 0.95f` (**deliberately below** the component's `kMaxFeedback = 1.2f`, `:99`).
Defaults: `kFxSaturationDefault = SeraphisEngine::kOutputSaturation` (`:248`, **0.15**),
`kFxDelayMixDefault = 0.0f`, `kFxDelayTimeDefault = SpectralDelay::kDefaultDelayMs` (`:93`, **250**),
`kFxDelaySpreadDefault = 0.0f`, `kFxDelaySpreadDirectionDefault = 0`,
`kFxDelayFeedbackDefault = 0.35f`, `kFxDelayTiltDefault = 0.0f`, `kFxDelayDiffusionDefault = 0.30f`,
`kFxDelayWidthDefault = 0.50f`, `kFxDelaySyncDefault = false`,
`kFxDelaySyncNoteDefault = kFxDelaySyncNoteDefaultIndex` (T003),
`kFxSpectralFreezeDefault = false`, `kFxWidthDefault = MidSideProcessor::kDefaultWidth` (`:67`, **100**),
`kFxWanderDepthDefault = 0.0f`, `kFxWanderRateDefault = BrownianDrift::kDefaultSmoothness` (`:107`,
**0.50**), `kFxAzimuthDepthDefault = 0.0f`.

*The FR-016a helper lives HERE, not in `processor.cpp`* — two test obligations evaluate it from another
TU (SC-005 clause 1's 513-bin sweep and FR-016a's own `SECTION`, both in `effects_chain_test.cpp`), and a
`constexpr` function defined in a `.cpp` with no header declaration is neither callable nor
constant-evaluable from there. **`std::abs` is forbidden**: `<cmath>`'s `float` overloads are not
`constexpr` before C++23 on every leg (plan R-6), so use the branchless form:

```cpp
[[nodiscard]] inline constexpr float tiltCompensatedFeedback(float fb, float tilt) noexcept {
    const float mag = (tilt < 0.0f) ? -tilt : tilt;
    return fb / (1.0f + mag);
}
```

*`struct EffectsParams`*: 12 × `std::atomic<float>`, 2 × `std::atomic<int>` (1413, 1419), 2 ×
`std::atomic<bool>` (1418, 1430), each initialised from its default constant above.

*The six functions, all `inline`:*

- `handleEffectsParamChange(EffectsParams&, ParamID, ParamValue)` — one `switch`; each row denormalizes
  **once** through its named min/max and **clamps**. ID 1414 clamps to `[0, kFxDelayFeedbackMax]`
  *before any use* (spec Edge cases: a state blob carrying an out-of-range value must be clamped **before
  the compensation divide**). 1413/1419 `std::clamp(index, 0, N-1)`. 1418/1430 `value >= 0.5`.
- `registerEffectsParams(ParameterContainer&)` — 12 × `addParameter`; 2 × `addDropdownParam(parameters,
  title, id, defaultIndex, labels, count)` (`dropdown_mappings.h:305-315` — the **one** path that pins
  `info.defaultNormalizedValue`); 2 × `addParameter(..., /*stepCount*/1, default ? 1.0 : 0.0, ...)`.
  Titles: `"FX Saturation"`, `"Delay Mix"`, `"Delay Time"`, `"Delay Spread"`, `"Delay Spread Dir"`,
  `"Delay Feedback"`, `"Delay Tilt"`, `"Delay Diffusion"`, `"Delay Width"`, `"Delay Sync"`,
  `"Delay Sync Note"`, `"Spectral Freeze"`, `"Stereo Width"`, `"Wander Depth"`, `"Wander Rate"`,
  `"Azimuth Depth"`. Units: `"ms"` on 1411/1412, `"%"` on 1440, empty elsewhere.
- `formatEffectsParam(ParamID, ParamValue, String128)` — `%.0f%%` for the `[0,1]` rows, `%.1f ms` for
  1411/1412, `%+.2f` for 1415, `%.0f %%` for 1440, `"On"`/`"Off"` for 1418/1430, the label string for
  1413/1419; `kResultFalse` for any other ID.
- `saveEffectsParams(const EffectsParams&, IBStreamer&)` — **exactly 64 bytes**: 12 `writeFloat` in C-6
  table order (1400, 1410, 1411, 1412, 1414, 1415, 1416, 1417, 1440, 1441, 1442, 1443), then
  `writeInt32(spreadDirection)`, `writeInt32(delaySyncNote)`, `writeInt32(delaySync ? 1 : 0)`,
  `writeInt32(spectralFreeze ? 1 : 0)`. **This order is fixed here and mirrored exactly by both loaders.**
- `loadEffectsParams(EffectsParams&, IBStreamer&) -> bool` — EOF-safe: every read guarded, `return false`
  on a short stream, every unread field left at its C-6 default; re-clamps 1414 and the two indices.
- `loadEffectsParamsToController(IBStreamer&, SetParamFunc)` — inverts every mapping, guarded read per
  field.

**Verify:** `--target seraphis_tests` compiles; the `static_assert` passes.

---

## GROUP E — Registration: the surface becomes 107

*T005 → T006 → T007, in that order. All three edit different existing files; none is `[P]` because none
creates a new file.*

### T005 — Register the 16 IDs, add the control tags, and gate the surface with SC-001

**Files edited:** `plugins/seraphis/src/controller/controller.cpp`,
`plugins/seraphis/resources/editor.uidesc`, `plugins/seraphis/tests/unit/param_denorm_test.cpp`.
**Depends on:** T004.

**Failing test FIRST** — in `plugins/seraphis/tests/unit/param_denorm_test.cpp`, a new
`TEST_CASE("Seraphis effects parameters denormalize")` (SC-001):

1. `REQUIRE(controller.getParameterCount() == 107);`
2. A **checked-in 16-row table** of `{id, kind, min, max, defaultNormalized, stepCount}`; for every row,
   `getParameterInfo` must match: `stepCount == 0` for the 12 R rows, `== 1` for 1418 and 1430,
   `== 2` for 1413 (3 entries) and `== 9` for 1419 (10 entries).
   Defaults, as **normalized** values: 1400 → `0.15`; 1410 → `0.0`; 1411 → `250/2000 = 0.125`;
   1412 → `0.0`; 1413 → `0.0`; 1414 → `0.35/0.95 = 0.368421`; 1415 → `0.5` (bipolar −1…+1);
   1416 → `0.30`; 1417 → `0.50`; 1418 → `0.0`; 1419 → `defaultIndex / 9`
   (T001 ruling 1: `7/9 = 0.777778` under the plan's recommendation); 1430 → `0.0`;
   1440 → `100/200 = 0.5`; 1441 → `0.0`; 1442 → `0.50`; 1443 → `0.0`. Compare with
   `Catch::Approx(...).margin(1e-6)`.
3. Round-trip: for every new ID and every `n ∈ {0.0, 0.25, 0.5, 0.75, 1.0}`,
   `REQUIRE(toNormalized(toPlain(n)) == Approx(n).margin(1e-6));`
4. Every dropdown index of 1413 and 1419 yields a **distinct, non-empty** string via
   `getParamStringByValue`.
5. **Literal label arrays asserted element-by-element**, in order:
   `kFxSpreadDirectionLabels == {"Low → High", "High → Low", "Center → Out"}` and
   `kFxDelaySyncNoteLabels ==` the T001 set (a permuted table is the most likely error, since
   `kSyncNoteLabels` is a *different* 8-entry ordering).

**Implement.** Three one-line additions to `controller.cpp` mirroring the aether rows:
`registerEffectsParams(parameters);` after `:51`; `loadEffectsParamsToController(streamer, setParam);`
after `:100` in `setComponentState` (**in the same order `getState` writes** — last);
`if (formatEffectsParam(id, valueNormalized, string) == kResultOk) { return kResultOk; }` after `:133`.
`setComponentState`'s `version > kCurrentStateVersion` refusal (`:76`) needs no edit — the constant moved
in T002. In `editor.uidesc`, append **16 `<control-tag>` entries** inside the existing `<control-tags>`
block (`:20-121`) under `<!-- Effects (1400+) -->`, named `FxSaturation`, `FxDelayMix`, `FxDelayTime`,
`FxDelaySpread`, `FxDelaySpreadDir`, `FxDelayFeedback`, `FxDelayTilt`, `FxDelayDiffusion`,
`FxDelayWidth`, `FxDelaySync`, `FxDelaySyncNote`, `FxSpectralFreeze`, `FxStereoWidth`, `FxWanderDepth`,
`FxWanderRate`, `FxAzimuthDepth`. **No layout change** — Phase 11 owns layout (FR-036).

**Verify:** `seraphis_tests.exe "Seraphis effects parameters denormalize"` green.

### T006 — `parameter_surface_test.cpp`: the `kSurface` table grows to 107 (the missed tenth obligation)

**Files edited:** `plugins/seraphis/tests/unit/parameter_surface_test.cpp` (only).
**Depends on:** T005.

**Failing test FIRST — the file already fails to compile.** `static_assert(kSurfaceRowCount == 91, …)`
(`:207-209`) is a hard build error the moment T005 registers 16 more parameters.

**Implement.** Append 16 rows to `kSurface[]` (the table ends at `{kAetherWidthId, Kind::R, 0}`), in ID
order, with the kinds and step counts of T005's table: `Kind::R, 0` for the twelve R IDs;
`Kind::T, 1` for 1418 and 1430; `Kind::L, 2` for 1413 and `Kind::L, 9` for 1419 — matching whatever
`expectedStepCount()` (`:212`) derives for each kind, extending that switch if the L rows need an
explicit count. Update `static_assert(kSurfaceRowCount == 107, "SC-001: spec C-6 is a 107-row table
(8 shipped + 83 Phase 9 + 16 Phase 10)")` at `:209`, and
`CHECK(controller.getParameterCount() == 107)` at `:480`. The `CHECK(registered.size() ==
kSurfaceRowCount)` (`:495`) and `CHECK(expected.size() == kSurfaceRowCount)` (`:501`) then hold
automatically — **do not weaken them.**

**Verify:** `seraphis_tests.exe "*surface*"` green.

### T007 — `editor_lifecycle_test.cpp`: two count assertions 91 → 107

**Files edited:** `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp` (only).
**Depends on:** T005.

**Failing test FIRST — already failing:** `REQUIRE(controller.getParameterCount() == 91)` at **`:242`
and `:254`** (FR-038a clause 1).

**Implement.** Both become `== 107`. Nothing else in the file changes; the harness's 10 open/close cycles
are untouched (SC-016).

**Verify:** `seraphis_tests.exe "*editor*lifecycle*"` green.

---

## GROUP F — Processor wiring, routing, and state version 3

### T008 — Pack member, dispatch rung, `Route::FX`, the single-writer saturation, state v3

**Files edited:** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`.
**Files created:** `plugins/seraphis/tests/unit/state_v3_test.cpp`.
**Depends on:** T001 ruling 2, T005–T007.
*(The new TU is registered in CMake by T010; until then it does not build. Write it now — its assertions
define this task — and confirm it green at T010. If the executor prefers, T010 may be pulled forward
ahead of T008; nothing else depends on the ordering.)*

**Failing test FIRST** — `plugins/seraphis/tests/unit/state_v3_test.cpp`,
`TEST_CASE("Seraphis_StateVersion3_RoundTripsAndMigrates")` (SC-009), modelled on
`plugins/seraphis/tests/unit/state_v2_test.cpp`'s blob helpers:

- **(a)** Drive all 16 effects IDs to **non-default** values, `getState` → `setState` into a second
  processor, and assert every one of the 16 reads back **exactly** (floats compared with `==`; this is a
  serialization round-trip inside one process, not a render).
- **(b)** A checked-in **version-2** blob loads with `kResultOk`; all 16 effects fields sit at their C-6
  defaults; every Phase 9 field equals what the blob encodes.
- **(c)** A checked-in **version-1** blob still loads with `kResultOk`.
- **(d)** The v3 blob's bytes **from offset 4 onward** have the v2 blob's bytes from offset 4 onward as a
  **strict prefix**, and the two blobs differ **only** in the leading `int32`. *(Not from offset 0: the
  version is the first field written, `processor.cpp:950`, and the first read, `:877-878`.)*
- Stream size assertion: **2532 → 2596 bytes** (the effects block is exactly **64**).

**Implement.**

- `processor.h`: `#include "parameters/effects_params.h"`; private member `EffectsParams effectsParams_{};`.
- `processor.cpp` `processParameterChanges` — one new rung on the existing `if (id < X)` ladder
  (`:1021-1047`), after the aether rung (FR-018; **never** a 107-case switch):
  `} else if (id < kEffectsParamRangeEnd) { handleEffectsParamChange(effectsParams_, id, value); markDirty(id); }`
- `enum class Route` (`:131`) gains `FX`. `routeOf` (`:133-249`) gains one arm listing **1410–1443** →
  `Route::FX`, and an **explicit** `case kFxSaturationId: return Route::ENG;`. That explicit case is not
  bookkeeping: `routeOf`'s `default:` returns `Route::Local` (`:246-248`), so without it ID 1400
  classifies as `Local`, contradicting C-6's Route column — invisible today because `markDirty`'s
  `Route::ENG` and `Route::Local` arms share one `break;` (`:1222-1225`), which is exactly why it must be
  written now.
- `markDirty` (`:1213-1230`) gains `case Route::FX: break;` with the comment that it bumps **no**
  generation counter (FR-019) — neither `voiceParamGeneration_` nor `aetherParamGeneration_`.
- **D-2, the single writer.** Delete the `engine_->setOutputSaturation(...)` block from
  `pushGlobalParams()` (`:1090-1097`). Add trackers `float lastPushedSaturation_ = 0.0f;
  bool lastPushedSaturationValid_ = false;`. Add `void pushEffectsParams() noexcept;` (body filled in by
  T014; for now it carries **only** the composed saturation push), called once per `process()` call from
  the pre-slice block beside `pushAetherParamsIfDirty()` (`:702`):
  ```cpp
  const float amount = soft ? effectsParams_.saturation.load(std::memory_order_relaxed) : 0.0f;
  if (!lastPushedSaturationValid_ || amount != lastPushedSaturation_) {
      engine_->setOutputSaturation(amount);
      lastPushedSaturation_ = amount; lastPushedSaturationValid_ = true;
      ++engSoftLimitPushes_;   // the EXISTING counter - no Phase 9 assertion moves
  }
  ```
  Rewrite the prepare-time push (`:538-539`) as the same composed expression and seed
  `lastPushedSaturation_ = amount; lastPushedSaturationValid_ = false;` (value seeded, cadence counter not
  — so the first `process()` still pushes once and counts once, which is what
  `engSoftLimitPushCountForTest()` (`processor.h:235`) is asserted against). At the C-6 defaults this is
  **bit-identical to today**: `soft == true`, `saturation == 0.15f == SeraphisEngine::kOutputSaturation`.
- **State.** `getState` (`:943-981`): one line **last**, after `saveAetherParams` —
  `saveEffectsParams(effectsParams_, streamer);`. `setState` (`:866-931`): one line **last**, after
  `loadAetherParams` — `loadEffectsParams(effectsParams_, streamer);`. Both loaders are EOF-safe, so a v1
  or v2 stream stops before the block and every effects field keeps its C-6 default (FR-033) — **no
  version-aware branch** (C-8).
- `pushAllSurfaces()` (`:1622-1675`) sets `lastPushedSaturationValid_ = false;` and invalidates every
  effects push tracker, so FR-034's `setState()`-after-prepare path re-pushes the whole surface.

**Verify:** `seraphis_tests.exe "Seraphis_StateVersion3*"` green (after T010 registers the TU); the
Phase 9 `state_v2_test`, `param_flow_test` and `param_cadence_test` cases stay green.

### T009 — `effects_chain_test.cpp` (NEW): the D-2 single-writer regression

**Files created:** `plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T008. *(Registered in CMake by T010.)*

**Failing test FIRST** — create the TU with `TEST_CASE("Effects chain order matches C-1")`'s file
scaffolding (fixture include `plugins/seraphis/tests/seraphis_test_fixture.h`, `ProcessorFixture`) and
the one `SECTION` this task owns, **"ID 1400 is the only writer on output saturation" (plan R-2)**:

1. Set `kFxSaturationId` to `0.8` (normalized `0.8`), run one `process()` call, assert
   `engineForTest()->getOutputSaturation() == Approx(0.8f).margin(1e-6)`
   (`seraphis_engine.h:695`).
2. Toggle `kSoftLimitId` (ID 2) **off** → run one block → assert `getOutputSaturation() == 0.0f`.
3. Toggle it **on** again → run one block → assert `getOutputSaturation() == Approx(0.8f)` —
   **not** `0.15f`. Under the shipped two-writer code this reverts to `0.15f`, which is the defect.
4. At the C-6 defaults with `kSoftLimitId` on, `getOutputSaturation() == Approx(0.15f)` and
   `engSoftLimitPushCountForTest()` (`processor.h:235`) advances **exactly once** over a 10-block render
   in which nothing moves.

**Implement.** Nothing new — T008's composed writer is what makes this pass. If it fails, the fix is in
`pushEffectsParams()` / `pushGlobalParams()`, never in the assertion.

**Verify:** `seraphis_tests.exe "Effects chain order matches C-1"` green for this section (after T010).

---

## GROUP G — Test seams (moved earlier — see the stated deviation)

### T010 — CMake registration of the three new TUs (single task) + the `-fno-fast-math` list

**Files edited:** `plugins/seraphis/tests/CMakeLists.txt` (only).
**Files created (as minimal compiling stubs if a task above has not created them yet):**
`plugins/seraphis/tests/integration/effects_perf_test.cpp`.

**Failing test FIRST.** Before this task, `seraphis_tests.exe "Seraphis_StateVersion3*"` and
`seraphis_tests.exe "Effects*"` report **zero matching test cases** — Catch2's "No tests ran" is the red.

**Implement.** Add to the enumerated `add_executable(seraphis_tests …)` source list (`:5-44`; **the list
is not globbed — an unregistered TU silently drops**), under a `# Seraphis Phase 10` comment:

```
    unit/state_v3_test.cpp
    integration/effects_chain_test.cpp
    integration/effects_perf_test.cpp
```

Add to the `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (`:76-92`):
**`integration/effects_chain_test.cpp`** (it checks non-finite payloads by bit pattern and measures
per-sample step statistics that fast-math contraction would reshape) and **`unit/state_v3_test.cpp`**
(raw float round-trip). **`integration/effects_perf_test.cpp` MUST stay OUT**, for the reason the block's
own comment gives for `param_perf_test.cpp` — `-fno-fast-math` would move the figures its baselines pin.

No `dsp/tests/CMakeLists.txt` change: Phase 10 adds no DSP component.

**Verify:** `--target seraphis_tests` builds; `seraphis_tests.exe "Seraphis_StateVersion3*"` and
`seraphis_tests.exe "Effects*"` both run and report their cases (T008/T009 green).

### T011 — FR-040's three-capability probe, FR-041's seven counters, the pre-output tap

**Files edited:** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T001 amendments 5–6, T010.

**Failing test FIRST** — two additions to `effects_chain_test.cpp`:

**(i) `TEST_CASE("Effects defaults are a no-op on the same build")` — SC-002.**
**One build, one process, one `Processor` instance.** Two renders of an identical 10 s MIDI sequence
(8 voices, every Phase 9 parameter at its shipped default, all 16 effects parameters at their C-6
defaults): one with the probe's **capability 1 engaged** (C-1 steps 4 and 5 skipped), one **disengaged**.
Assert `max |a[i] − b[i]| == 0.0f` over **every sample of both channels** — exact equality, legitimate
here only because both sides are the same compiled path on the same instance. *(This case must stay green
through every later task; it is the negative control the whole phase is built against.)*

**(ii) A `SECTION` "the pre-output tap is honest about truncation" (FR-041 clause 6, D-8 clause 3).**
- On a **512**-sample block: `preOutputTapLForTest().size() == 512` and
  `preOutputTapTruncatedForTest() == false`.
- On a **4096**-sample block: `preOutputTapLForTest().size() == 2048` and
  `preOutputTapTruncatedForTest() == true`.
- With the limiter provably in gain reduction (drive the pre-limiter peak above
  `kLimiterCeilingLin = 0.8912509f`), the tap contents **differ** from the plugin output —
  `max |tap[i] − out[i]| > 1e-3f`.
- All three probe capabilities are declared and reachable from the test TU.

**Implement.**

- `processor.h`, beside `detail::SeraphisParamSmootherBypassProbe` (`:135`, friended at `:246`):
  `namespace detail { struct SeraphisEffectsStageBypassProbe; }` and
  `friend struct detail::SeraphisEffectsStageBypassProbe;`. **Declared here, DEFINED ONLY in
  `effects_chain_test.cpp`**, in the shape of `param_continuity_test.cpp:111-119`, with an RAII guard so
  a failed assertion cannot leave a mode set for the rest of the suite. ODR sweep before writing the
  name: `grep -rn "SeraphisEffectsStageBypassProbe" dsp/ plugins/` → must be 0 hits.
- Three runtime flags, set **only** by that probe, `false` on every ship path:
  `bool effectsStageBypassed_`, `bool effectsStageAfterOutput_`, `bool effectsReturnRampSnap_`.
- Seven public accessors + the flag (all `[[nodiscard]] … const noexcept`, beside Phase 9's `*ForTest()`
  block at `:162-240`): `effectsStageNsForTest()` (`double`),
  **`effectsStageProcessCallsForTest()`** (incremented **once per `process()` call** in the pre-slice
  block — *never* per `renderSlice`; a block carries up to 8 sub-slices under plan D-6, so a per-slice
  divisor under-reports by that factor), `spectralDelayResetCountForTest()`,
  `effectsPushCountForTest()`, `widthDriftBlockCountForTest()`, `bypassPredicateEvalCountForTest()`,
  `sendChunkCountForTest()`, plus `preOutputTapLForTest()` / `preOutputTapRForTest()`
  (`std::span<const float>`, length carried by the span) and `preOutputTapTruncatedForTest()` (`bool`).
- Buffers `preOutTapL_`, `preOutTapR_` sized `SeraphisEngine::kMaxBlockSamples = 2048` in
  `setupProcessing()` (FR-028); `preOutTapCursor_`, `preOutTapSize_`, `preOutTapTruncated_`.
- `processor.cpp` pre-slice block: `preOutTapCursor_ = 0; preOutTapTruncated_ =
  static_cast<std::size_t>(data.numSamples) > kMaxBlockSamples; ++effectsStageProcessCalls_;`
- `renderSlice`, **between** the master-gain loop (`:1162-1166`) and `processOutputStage` (`:1170`):
  the scoped `std::chrono::steady_clock` timer accumulating into `effectsStageNs_`, wrapping
  ```cpp
  if (!effectsStageBypassed_) {                      // capability 1
      runSendStage(wetL_.data(), wetR_.data(), n);   // C-1 step 4 - EMPTY STUB in this task
      if (!effectsStageAfterOutput_) {               // capability 2
          runWanderStage(wetL_.data(), wetR_.data(), n);  // C-1 step 5 - EMPTY STUB
      }
  }
  ```
  then the **tap copy inside the timed region** (guarded `if (preOutTapCursor_ + n <=
  preOutTapL_.size())`, `std::copy_n` ×2, advance cursor and `preOutTapSize_`), then close the timer,
  then `engine_->processOutputStage(...)` **unchanged and ALWAYS LAST** (FR-002), then
  `if (effectsStageAfterOutput_) { runWanderStage(...); }` — **test paths only**.
  *The tap copy is deliberately inside the timed region*: it is paid by SC-014's whole-render full-poly
  gate, which has the least headroom of any budget in this phase (4.09 points), so hiding it from
  SC-012/SC-013 would charge it to the one gate that was not sized for it.
- Declare `void runSendStage(float*, float*, std::size_t) noexcept;` and
  `void runWanderStage(float*, float*, std::size_t) noexcept;` as **empty-bodied private members** in
  this task; T012/T013/T017 fill them.
- RT safety (FR-029): two clock reads, one add and one buffer copy per slice; no allocation, no lock, no
  throw, no I/O.

**Verify:** `seraphis_tests.exe "Effects defaults are a no-op on the same build"` green;
the tap `SECTION` green; zero compiler warnings.

---

## GROUP H — The spectral-delay send (C-1 step 4)

### T012 — Send members, `setupProcessing()`, `BlockContext`, seeding, `clearFifos()`, `setActive(false)`

**Files edited:** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/unit/processor_bus_test.cpp`.
**Depends on:** T011.

**Failing test FIRST** — `plugins/seraphis/tests/unit/processor_bus_test.cpp`, new
`TEST_CASE("Phase 10 does not change reported latency")` (SC-004):

- `getLatencySamples()` is captured before anything moves, then all 16 effects IDs are driven to
  non-default values (mix 1.0, feedback 0.95, sync on, freeze on, width 200 %, both depths 1.0, …), and
  `getLatencySamples()` must return the **identical** value.
- In every case `getLatencySamples() == reverbForTest()->getLatencySamples()`
  (`processor.cpp:850-854` is unchanged — FR-005: neither the send's `fftSize` nor the accumulator's
  512-sample pipeline delay may be added).
- Sweeping `kFxDelayMixId` 0 → 1 → 0 produces **zero** `restartComponent(kLatencyChanged)` calls. The
  processor holds no `IComponentHandler` (`processor.h:329-339`), so this is asserted as "no such call
  site exists" — a source-level check plus the constant-latency assertion above.

**Implement.**

*Named constants in `processor.h` (FR-015/FR-024a — each with the header line that justifies it, never a
literal at a use site):*

```cpp
inline constexpr std::size_t kFxSendChunkSamples =
    Krate::DSP::SpectralDelay::kDefaultFFTSize / 2u;              // 512; hop at fftSize 1024
inline constexpr float kFxReturnRampMs = kParamSmoothMs;          // 20 ms (processor.h:119)
static_assert(kFxReturnRampMs == kParamSmoothMs, "FR-038b cl.2: ONE smoother for ID 1410");
inline constexpr float kFxSendDrainMs = Krate::DSP::SpectralDelay::kMaxDelayMs;   // 2000
inline constexpr float kFxSendDrainFloor = 1.0e-6f;
inline constexpr float kFxFreezeMinReturnGain = 0.5f;
inline constexpr std::uint64_t kFxFreezePrimeSamples =
    2u * Krate::DSP::SpectralDelay::kDefaultFFTSize;              // 2048 = 42.7 ms @ 48 kHz
inline constexpr float kWanderWidthSpanPercent = 50.0f;
inline constexpr float kFxAzimuthCentreComp = 1.41421356f;        // plan D-4
inline constexpr std::uint32_t kFxWidthDriftSalt   = 0x5E11A001u;
inline constexpr std::uint32_t kFxAzimuthDriftSalt = 0x5E11A002u;
static_assert(kFxWidthDriftSalt != kFxAzimuthDriftSalt, "C-5: the two drift salts must differ");
inline constexpr std::size_t kFxFifoCapacity = 4096;
static_assert((kFxFifoCapacity & (kFxFifoCapacity - 1u)) == 0u, "the ring index is a mask");
static_assert(kFxFifoCapacity >= kFxSendChunkSamples + kMaxBlockSamples, "plan sec. 3.1 bound");
```

*Members* (plan §2.4): `Krate::DSP::SpectralDelay spectralDelay_`; `std::vector<float>` FIFOs
`fxInL_/fxInR_/fxOutL_/fxOutR_` (capacity `kFxFifoCapacity`) and the one chunk scratch pair
`fxChunkL_/fxChunkR_` (`kFxSendChunkSamples`); indices `fxInWrite_/fxInRead_/fxChunkFill_`,
`fxOutWrite_/fxOutRead_/fxOutFill_`; `fxBypassedSamples_`, `fxLiveSamplesSinceEngage_` (saturating
`std::uint64_t`), `fxDrainRemaining_`, `fxSendDrainSamples_` (`std::int64_t`), `fxDrainPeak_`,
`fxEffectiveReturnGain_`, `enum class FxSendState { Bypassed, Active, Draining } fxSendState_`,
`fxResetDue_`, `fxFifoClearDue_`, `Krate::DSP::BlockContext fxBlockCtx_`, and the counters
`fxResetCount_`, `effectsPushes_`, `sendChunks_`, `bypassEvals_`.
**There is NO `fxPhase_`** — plan D-3 deleted it.

*`setupProcessing()`, inserted after step 5 (`:541-545`), in this exact order (load-bearing at two
points):*

```cpp
spectralDelay_.setFFTSize(Krate::DSP::SpectralDelay::kDefaultFFTSize);  // :408  BEFORE prepare
spectralDelay_.setDryWetMix(1.0f);                                      // :500  BEFORE prepare
spectralDelay_.setSpreadCurve(Krate::DSP::SpreadCurve::Logarithmic);    // :448
spectralDelay_.prepare(sampleRate_, kMaxBlockSamples);                  // :131  ends in snapParameters()
const std::size_t si = clampSeedIndex(globalParams_.seedIndex.load(std::memory_order_relaxed));
spectralDelay_.seedRng(kSeedValues[si]);                                // :297   FR-027: seed THEN reset
spectralDelay_.reset();                                                 // :242
lastPushedFxSeedIndex_ = static_cast<int>(si);
```

**FR-004 is why `setDryWetMix` runs BEFORE `prepare`**: post-prepare it only sets a smoother *target*
(`spectral_delay.h:500-503`) advanced **once per `process()` call** (`:373`, `:389`) despite a per-sample
50 ms coefficient (`:184-194`) — tens of seconds of un-aligned current-block dry leaking into the bus
from `kDefaultDryWet = 0.5f`. Pushed before, `prepare()`'s own `setTarget` (`:202`) + `snapParameters()`
(`:206`) snap it to 1.0.

Then the FIFO/scratch sizing with `assign()` (**never `resize()`** — they must start zeroed),
`fxSendDrainSamples_ = std::llround(kFxSendDrainMs * 0.001 * sampleRate_)`, the state re-initialisation
(`fxSendState_ = Bypassed`, `fxBypassedSamples_ = 0`, `fxLiveSamplesSinceEngage_ = 0`,
`fxDrainRemaining_ = 0`, `fxResetDue_ = false`, `fxFifoClearDue_ = false`,
**`fxDrainPeak_ = 1.0f`** — deliberately **above** `kFxSendDrainFloor` so a drain that has not yet run a
chunk can never take the energy exit — `fxEffectiveReturnGain_ = 0.0f`), and one call to `clearFifos()`.

*`clearFifos()` — the ONE definition, plan §3.1, three call sites (prepare, `setActive(false)`, the
single deferred mid-render site at the top of `runSendStage`):*

```cpp
void Processor::clearFifos() noexcept {
    std::fill(fxInL_.begin(), fxInL_.end(), 0.0f);   // and fxInR_, fxOutL_, fxOutR_
    fxInWrite_ = fxInRead_ = fxChunkFill_ = 0;
    fxOutRead_  = 0;
    fxOutWrite_ = fxOutFill_ = kFxSendChunkSamples;  // the one-chunk PRE-FILL
}
```
**Zeroing the counters instead of restoring the pre-fill breaks the §3.1 invariant and wraps a
`std::size_t`** (plan R-13) — see T013.

*FR-030's `BlockContext`, built **once per `process()` call*** in the pre-slice block, from the tempo
sample point Phase 9 already uses (`:1563`), with Phase 9's **three-part** guard verbatim in shape
(`:1585-1586`): `pc != nullptr && (pc->state & Vst::ProcessContext::kTempoValid) != 0 && pc->tempo > 0.0`,
writing `120.0` into `fxBlockCtx_.tempoBPM` otherwise. Relying on the component's own fallback is **not**
sufficient — it fires only on `tempo <= 0.0` (`spectral_delay.h:325-327`) and a host may leave a stale
positive tempo while `kTempoValid` is clear.

*`setActive(false)`* (`:600-616`) gains, beside the engine/reverb clears (FR-035): `spectralDelay_.reset();`
plus `clearFifos()` and the same state re-initialisation as prepare, and `fxReturnGainSm_.snapTo(0.0f)`
(the smoother lands in T013).

**Verify:** `seraphis_tests.exe "Phase 10 does not change reported latency"` green; SC-002 (T011) still
green; zero warnings.

### T013 — `runSendStage`: the fixed-size accumulator and the send state machine

**Files edited:** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T001 ruling 3, T012.

**Failing tests FIRST** — three additions to `effects_chain_test.cpp`:

**(i) `TEST_CASE("The effects send is block-size invariant")` — SC-017.**
`kFxDelayMixId = 1.0`, feedback 0.6, delay time 250 ms, wander engaged, **steady parameters throughout —
no bypass, freeze or seed transition**. A 4 s render delivered as block sizes **1, 2, 3, 7, 512, 2048 and
one oversized 4096 call** must each agree with a single contiguous render within
`tests/test_helpers/render_fingerprint.h` tolerance (`kSampleTolerance = 1.0e-4f`,
`kMetricTolerance = 1.0e-5`, `:48-52`). **Negative control (mandatory):** the same case with the
accumulator bypassed (send fed the raw slice length) must **fail**, so the criterion cannot pass
vacuously. *(This case compares plugin output through `render_fingerprint.h`, not the tap, so the 4096
block is legal here — unlike every tap-measuring criterion.)*

**(ii) A `SECTION` "the send carries no current-block dry" — FR-004.**
Render an impulse at `mix = 1` and assert the isolated return's first
`fftSize + kFxSendChunkSamples = 1024 + 512 = 1536` output samples are **< 1e-7** in absolute value. A
post-`prepare` `setDryWetMix` push leaks ~50 % dry on frame 0 and fails this.

**(iii) A `SECTION` "the drain's energy floor ends the window early" — FR-009a's `kFxSendDrainFloor`.**
At a **low-feedback / short-delay** operating point where the floor provably fires first —
**feedback 0.1, delay 50 ms** ⇒ the peak falls under `1e-6` after `ln(1e-6)/ln(0.1) ≈ 6` traversals
≈ **0.3 s**, an order of magnitude inside `kFxSendDrainMs = 2000` — assert via
`sendChunkCountForTest()` that the send **stops producing chunks before 2 s of drain has elapsed** (the
count is frozen by ~0.5 s after bypass). *This is the only place the floor is the discriminator*: at
every other configuration the phase uses (6.8 s at SC-011a's feedback 0.6, 3.3 s at the C-6 default
0.35) the 2 s cap fires first, so an implementation that omitted the floor entirely would pass SC-011a,
SC-012, SC-013 and SC-018 unchanged.

**Implement.**

*`runSendStage(l, r, n)` — plan §3.1, executed only when `fxSendRuns_`:*

```
clear:  if (fxFifoClearDue_ || (fxResetDue_ && fxChunkFill_ + n >= kFxSendChunkSamples)):
            if (fxResetDue_) { spectralDelay_.reset(); ++fxResetCount_; fxResetDue_ = false; }
            clearFifos();  fxFifoClearDue_ = false
push:   for i in [0,n): fxIn[(fxInWrite_ + i) & MASK] = (fxSendState_ == Active) ? bus[i] : 0.0f
        fxInWrite_ += n;  fxChunkFill_ += n            // FR-009a: SILENCE while draining
run:    while (fxChunkFill_ >= kFxSendChunkSamples):
            copy 512 from fxIn -> fxChunk*;  fxInRead_ += 512;  fxChunkFill_ -= 512
            spectralDelay_.process(fxChunkL_.data(), fxChunkR_.data(), kFxSendChunkSamples, fxBlockCtx_)
            ++sendChunks_
            fxDrainPeak_ = max |fxChunk*|
            copy fxChunk* -> fxOut;  fxOutWrite_ += 512;  fxOutFill_ += 512
mix:    assert(fxOutFill_ >= n)                        // DEBUG-ONLY
        g = fxReturnGainSm_.getCurrentValue()          // READ, never .process() - see below
        for i in [0,n): bus[i] += fxOut[(fxOutRead_ + i) & MASK] * g
        fxOutRead_ += n;  fxOutFill_ -= n
end:    assert(fxChunkFill_ + fxOutFill_ == kFxSendChunkSamples)   // DEBUG-ONLY
```

Four things are load-bearing and each has a failure mode:

- **`SpectralDelay::process` is called with a constant `numSamples == 512`, never a slice length**
  (FR-003a). With a variable `n` the component's output stream position depends on how many analysis
  frames happened to be ready, so the same audio as one 2048-call and as four 512-calls comes back a whole
  hop apart, **permanently** (`stft.h:134-137`, `:171`, `:311`; `spectral_delay.h:366`, `:383-386`).
- **The clear runs at the TOP, never inside the `while`.** Inside the loop the guard
  `fxChunkFill_ >= 512` has already passed, so a clear that zeroes `fxChunkFill_` is immediately followed
  by `-= 512` on a `std::size_t`, which **wraps to ~2⁶⁴, keeps the loop condition true forever and calls
  `SpectralDelay::process()` without bound on the audio thread** — a hard hang (plan R-13).
- **The two debug-only assertions are mandatory.** The §3.1 invariant is
  `fxChunkFill_ + fxOutFill_ == 512` at every slice boundary (proof: `outLen'' = 512 − inLen − n + 512k =
  512 − inLen''`), giving a pipeline delay of exactly **512 samples in every partition** and an output
  FIFO that can never underflow. They cost nothing in Release and turn any future edit that breaks the
  proof into an immediate Debug failure. The path is reachable in the shipping configuration — plan D-6
  forces 64-sample sub-slices for the whole of every engage/bypass ramp.
- **`fxReturnGainSm_` is READ with `getCurrentValue()`, never `.process()`-ed.** It is in
  `classBSmoothers()`, so `advanceParamSmoothers()` already advanced it by `n` at `processor.cpp:821`
  before `renderSlice()` was entered (`:824`). A second per-sample advance makes it move `2n` per `n`
  rendered while the send runs and `n` while it does not — halving `kFxReturnRampMs` and making the ramp
  rate state-dependent. **Invariant, written beside the declaration: no smoother may be in
  `classBSmoothers()` AND be `.process()`-ed.** (The shipped precedent is explicit: `masterGain_`, the one
  smoother advanced per output sample at `:1163`, is deliberately **not** in `classBSmoothers()`.)

*`updateEffectsBypassState()` — plan §3.3, called **once per `process()` call** (FR-012),
`++bypassEvals_` exactly once:*

```
mix        = effectsParams_.delayMix          // FR-007's predicate is EXACT == 0.0f
freezeOn   = effectsParams_.spectralFreeze
wantActive = (mix != 0.0f) || freezeOn        // FR-023a forces the send active
fxEffectiveReturnGain_ = freezeOn ? max(mix, kFxFreezeMinReturnGain) : mix

Bypassed: if wantActive: fxResetDue_ = (fxBypassedSamples_ > fxSendDrainSamples_) && !freezeOn;
                         fxSendState_ = Active; fxLiveSamplesSinceEngage_ = 0
          else:          fxBypassedSamples_ += blockSamples (saturating)
Active:   if !wantActive: fxSendState_ = Draining; fxDrainRemaining_ = fxSendDrainSamples_;
                          fxDrainPeak_ = 1.0f                  // re-arm ABOVE the floor
          else:           fxLiveSamplesSinceEngage_ += blockSamples (saturating)
Draining: if wantActive:  fxSendState_ = Active; fxLiveSamplesSinceEngage_ = 0
          elif (fxDrainRemaining_ <= 0 || fxDrainPeak_ < kFxSendDrainFloor):
                          fxSendState_ = Bypassed; fxBypassedSamples_ = 0
          else:           fxDrainRemaining_ -= blockSamples

fxSendRuns_ = (fxSendState_ != Bypassed)      // FR-007's exact prohibition predicate
```

The `Active → Draining` **re-arm of `fxDrainPeak_` to 1.0f** is not cosmetic: a send engaged and
re-bypassed inside a single chunk period runs no chunk, so without it the predicate would read a stale
sub-floor value from a previous drain and terminate the new drain on its first block — the exact tail
annihilation FR-008/FR-009a exist to prevent.

**FR-007's prohibition:** while `!fxSendRuns_`, `runSendStage` is simply not called — no
`SpectralDelay::process`, no copy of the bus into `fxIn*`, no read or write of any send buffer.

*Three class-(b) smoothers* (`fxReturnGainSm_` at `kFxReturnRampMs`, `fxWanderDepthSm_` and
`fxAzimuthDepthSm_` at `kParamSmoothMs`), configured in `setupProcessing()`;
**`classBSmoothers()`'s return type widens from `std::array<OnePoleSmoother*, 9>` to `…, 12>`**
(`processor.h:312`), and `setParamSmootherTargets()` (`:285`), `advanceParamSmoothers()` (`:301`) and
`anyClassBSmootherUnsettled()` (`:327`) cover the three new entries.
`setParamSmootherTargets()` targets **`fxEffectiveReturnGain_`** for ID 1410 — the state machine's
composed value, not the raw atomic — and the raw atomics for 1441/1443.
*Stated cost (plan D-6):* widening the array puts the send/wander ramps into
`anyClassBSmootherUnsettled()`, which drives the 64-sample slice subdivision (`processor.cpp:811-815`),
so blocks render as 64-sample sub-slices for ~20 ms after any 1410/1441/1443 automation point and for the
whole of every engage/bypass ramp. That is **wanted** — it is what delivers the ramp on the absolute grid
— and SC-013's render is required to exercise it.

*FR-011:* `widthDrift_.processBlock(n)` / `azimuthDrift_.processBlock(n)` and `++widthDriftBlocks_` run in
the pre-slice block **every block regardless of bypass state** (members prepared in T017; if T017 has not
run, declare and prepare them here and leave the wander stage a stub).

**Verify:** SC-017 green (including its negative control failing as required); the FR-004 and
`kFxSendDrainFloor` sections green; SC-002 (T011) still green; zero warnings.
Also run a **Debug** build of `seraphis_tests` once here so the two `assert`s are live.

### T014 — `pushEffectsParams()`: every `SpectralDelay` setter, tilt compensation, seeds

**Files edited:** `plugins/seraphis/src/processor/processor.cpp` (and `.h` for trackers),
`plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T013.

**Failing tests FIRST** — five `SECTION`s in `effects_chain_test.cpp`:

1. **FR-022, one sub-case per setter.** Drive the ID off-default, run one `process()` call, assert the
   component getter reports the pushed value: `getBaseDelayMs()` (`spectral_delay.h:429`),
   `getSpreadMs()` (`:436`), `getSpreadDirection()` (`:442`), `getFeedback()` (`:464` — against the
   **tilt-compensated** value), `getDiffusion()` (`:493`), `getStereoWidth()` (`:516`), `getTimeMode()`
   (`:527`), `getNoteValue()` (`:535`). *Without this a build that never calls
   `setSpreadMs`/`setDiffusion`/`setStereoWidth`/`setSpreadDirection` passes every success criterion.*
2. **FR-022 "on change only".** Writing the **same** value again leaves `effectsPushCountForTest()`
   unmoved across 10 further blocks.
3. **FR-016a runtime.** `Seraphis::tiltCompensatedFeedback(0.95f, 1.0f) == 0.475f` and
   `(0.95f, -1.0f) == 0.475f`; with `kFxDelayFeedbackId = 0.95` and `kFxDelayTiltId = +1`,
   `spectralDelay_.getFeedback() == Approx(0.475f).margin(1e-6)`.
4. **FR-025.** Moving **1442** makes **both** `widthDrift_.getSmoothness()` and
   `azimuthDrift_.getSmoothness()` (`brownian_drift.h:169`) report the pushed value. *A build that
   pushes only one drift is otherwise invisible.*
5. **FR-023 (independence of the two freezes).** Driving **1204** (`kAetherFreezeId`) must not produce a
   held send tail while 1430 is off; driving **1430** must leave the Aether tail still decaying. Neither
   push path may read the other's atomic.

**Implement** `pushEffectsParams()` — called once per `process()` call from the pre-slice block beside
`pushAetherParamsIfDirty()` (`:702`), every push **on change only** against its own tracker, each push
`++effectsPushes_`, allocation-free / lock-free / exception-free. Body order:

1. **ID 1400** → the composed `SeraphisEngine::setOutputSaturation` of T008 (D-2).
2. **The seed**, if `globalParams_.seedIndex` moved since `lastPushedFxSeedIndex_`:
   `spectralDelay_.seedRng(kSeedValues[i]); spectralDelay_.reset(); ++fxResetCount_;` then
   `widthDrift_.setSeed(kSeedValues[i] ^ kFxWidthDriftSalt); widthDrift_.reset();` and the azimuth pair
   with `kFxAzimuthDriftSalt` (FR-026, FR-027). **Set `fxFifoClearDue_ = true` — do NOT clear the FIFOs
   here**; `runSendStage` performs the clear at its top, before any partial chunk-loop state is live
   (T013, plan R-13). This is FR-027's second burst site (SC-011 gates the block containing it).
3. **IDs 1411, 1412, 1416, 1417** → `setBaseDelayMs` / `setSpreadMs` / `setDiffusion` /
   `setStereoWidth` (`:425`, `:432`, `:489`, `:512`), one `lastPushed*` float each.
4. **IDs 1414 + 1415 together** (FR-016a): `spectralDelay_.setFeedback(
   Seraphis::tiltCompensatedFeedback(fb, tilt));` recomputed and re-pushed whenever **either** moves;
   `setFeedbackTilt(tilt)` pushed unchanged. The helper lives in `effects_params.h` (T004) — **not** in
   `processor.cpp`.
5. **IDs 1418 + 1419** → `setTimeMode(sync ? 1 : 0)` (`:524`), `setNoteValue(index)` (`:532`).
6. **ID 1413** → `setSpreadDirection(toSpreadDirection(index))` (`:439`).
7. **ID 1430** → deferred to T016 (the primed freeze push).
8. **ID 1442** → `widthDrift_.setSmoothness(rate); azimuthDrift_.setSmoothness(rate);` (`:152`) —
   **both** drifts, one value.

IDs **1410, 1440, 1441, 1443** are **not** pushed here: 1410/1441/1443 are the three class-(b) smoothers
(targets set in `setParamSmootherTargets()`), and 1440 is pushed inside the wander stage on the 64-sample
control grid (T017).

**Verify:** the five sections green; SC-017 and SC-002 still green; zero warnings.

### T015 — SC-005 (decay at both tilt extremes) and SC-019 (tempo sync)

**Files edited:** `plugins/seraphis/tests/integration/effects_chain_test.cpp` (only).
**Depends on:** T001 ruling 1 (SC-019's expected periods), T014.

Both criteria measure the **isolated send return** — `render(kFxDelayMixId = 1) − render(kFxDelayMixId
= 0)`, same instance, same MIDI script, same seed, read from **`preOutputTapForTest()`** — and both must
render with host blocks **≤ 2048** and assert `!preOutputTapTruncatedForTest()`.

**`TEST_CASE("Spectral delay decays at registered max feedback")` — SC-005, three parts:**

1. *Arithmetic, no render.* Over `tilt ∈ {−1, −0.5, 0, +0.5, +1}` and `fb = 0.95`, assert
   `max over 513 bins of clamp(tiltCompensatedFeedback(fb, tilt) · (1 + tilt·(b/512 − 0.5)·2), 0, 1.2f)`
   is **< 1.0**. Without FR-016a this bound is **1.2** and the decay clauses below are unpassable —
   243 of 513 bins sit above unity loop gain at tilt +1, and the per-bin recursion
   `tanh(delayedMag · binFeedback)` (`spectral_delay.h:751-767`) has a stable non-zero fixed point there.
2. *Decay, as **two separate runs** — tilt = −1 and tilt = +1.* Feedback at its registered maximum
   **0.95**, diffusion **1.0**, spread **0**, and **`kFxDelayTimeId` pinned at 250 ms**
   (`kDefaultDelayMs`, `:93`). *Pinning is required*: feedback applies once per traversal, so 120 s at
   0.95 gives **−213.8 dB** at 250 ms but only −53 dB at 1000 ms and −26.7 dB at 2000 ms. Each run: a 1 s
   burst followed by 120 s of silence; the isolated-return RMS must fall **≥ 60 dB** below its peak
   within those 120 s (480 traversals).
3. *Shape.* Over 5 s windows after the first 5 s, no window's RMS rises more than **0.5 dB** above its
   predecessor. (A tolerance, not strict monotonicity — dispersive per-bin decay with diffusion is not
   guaranteed monotone.)

No sample is non-finite — **checked by bit pattern, never `std::isnan`** (the `-ffast-math` rule; the TU
is on the `-fno-fast-math` list from T010, and the bit-pattern form is still mandatory).

**`TEST_CASE("Synced delay tracks host tempo")` — SC-019:**

- `kFxDelaySyncId` on, `kFxDelaySyncNoteId` at **two different indices**, at **90** and **140** BPM via
  `ProcessorFixture::setTempo(bpm, 4, 4, true, true)` (`plugins/seraphis/tests/seraphis_test_fixture.h:233`).
  The measured echo period of the isolated return must match
  `Krate::DSP::dropdownToDelayMs(index, tempo)` (`spectral_delay.h:330`) within **± one hop = ± 512
  samples**.
- Then `clearProcessContext()` (`:254`), **and separately** a context present with `kTempoValid`
  **clear** but a **stale non-zero `tempo`** — both must fall back to the **120 BPM** period within the
  same tolerance. *That last clause is what discriminates FR-030's three-part guard from the component's
  weaker `tempo <= 0.0` check (`spectral_delay.h:325-327`); without it, a build passing a
  default-constructed `BlockContext` (`tempoBPM = 120.0`) satisfies every other criterion while silently
  ignoring host tempo.*

**Implement.** Nothing new — T012/T013/T014 are what make these pass. A failure is a defect in the
compensation, the accumulator or the `BlockContext`, never in a threshold.

**Verify:** both cases green. SC-005's decay arms are long renders — expect them to dominate the suite's
wall time; do **not** tag them `[.perf]` (they are correctness gates).

### T016 — Spectral freeze: FR-023a's forced engage and D-5's priming

**Files edited:** `plugins/seraphis/src/processor/processor.cpp` (and `.h` for the counter),
`plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T015.

**Failing tests FIRST** — two cases in `effects_chain_test.cpp`, both reading
`preOutputTapForTest()` with blocks ≤ 2048 and asserting `!preOutputTapTruncatedForTest()`:

**`TEST_CASE("Spectral freeze holds the Aether tail")` — SC-007. The two arms use DIFFERENT difference
definitions and the split is normative.**

- **(a) From the C-6 DEFAULTS.** `kFxDelayMixId` held at **0 throughout**; the isolated quantity is
  `render(kFxSpectralFreezeId = on) − render(kFxSpectralFreezeId = off)`. **SC-003's mix-differenced
  definition does NOT apply to this arm** — it would mutate the very parameter the arm pins, rendering
  one side at mix 1 (return gain 1.0) and the other at mix 0 (return gain forced to
  `kFxFreezeMinReturnGain = 0.5`), so a build in which the forced engage worked *only when mix > 0* —
  the exact defect FR-023a exists to prevent — would still show a non-zero difference and pass.
  *Assertions:* engaging 1430 alone produces audible held output — the isolated return's RMS **5 s after
  note-off is > −60 dBFS** and within **±1.0 dB** of the RMS measured **200 ms after engagement**.
- **(b) With the send already at mix 1.0** (SC-003's definition) and a held chord: engage 1430, release
  all notes; the isolated return's RMS 5 s after note-off is within **±1.0 dB** of the RMS at 200 ms
  after engage (a point deliberately **> `kFreezeCrossfadeTimeMs = 75 ms`**, `spectral_delay.h:906`, so
  the crossfade is complete with 125 ms of margin), and the spectral centroid over the same interval
  moves by **< 5 %**. With freeze **off**, the same measurement decays by **≥ 30 dB**.
- Both arms additionally assert the capture happens at **engage** time, not at toggle time.

**`TEST_CASE("A short mix excursion preserves the send tail")` — SC-011a.**
Send active at mix 1.0, feedback **0.6**, delay **250 ms**, with a captured freeze. Drive
`kFxDelayMixId` to **exactly 0** and back to 1.0 over an interval of **200 ms** (comfortably shorter than
`kFxSendDrainMs`): the isolated return's RMS **500 ms after re-engagement** must be within **±2.0 dB** of
the RMS **500 ms before** the excursion, and the frozen spectrum's centroid within **5 %** of its
pre-excursion value. **Control (mandatory):** an excursion **longer** than `kFxSendDrainMs` must *reset* —
RMS falls **≥ 30 dB** and rebuilds — proving the window is the discriminator and not a vacuous pass.
*(The excursion length is pinned because the criterion is length-dependent by design: the drain is fed
silence, so the tail decays through it at the component's own per-bin feedback.)*

**Implement.**

- Add `std::uint64_t fxLiveSamplesSinceEngage_` accounting (already declared in T012) and the plan D-5
  priming gate:
  `freezeReady = freezeOn && sendActive && fxLiveSamplesSinceEngage_ >= kFxFreezePrimeSamples`,
  pushed to `spectralDelay_.setFreezeEnabled(freezeReady)` **on change only** in `pushEffectsParams()`
  item 7. **Freeze-off is never deferred** — `freezeReady` falls the instant `freezeOn` does.
  *Why priming exists:* `processSpectralFrame` captures on the first frame where `freezing &&
  !wasFrozen_` (`spectral_delay.h:677-688`), reading the STFT's current analysis frame. From the C-6
  defaults the send has been bypassed since prepare, so at the moment of a forced engage the STFT holds
  zeros or a stale drained tail — capturing that gives a **silent** frozen spectrum and SC-007 arm (a)
  fails for an implementation that follows FR-023a literally.
  `kFxFreezePrimeSamples = 2 × kDefaultFFTSize = 2048` = four hops = two analyses on wholly-live frames
  = **42.7 ms at 48 kHz**, far inside SC-007's 200 ms measurement point.
- FR-023a's forced engage and its **suppression of FR-008's `reset()`** are already in T013's state
  machine (`wantActive = (mix != 0.0f) || freezeOn`; `fxResetDue_ = … && !freezeOn`;
  `fxEffectiveReturnGain_ = freezeOn ? max(mix, kFxFreezeMinReturnGain) : mix`). Confirm both, because
  without the suppression the reset clears `wasFrozen_`/`freezeCrossfade_` and the frozen spectrum
  buffers (`spectral_delay.h:256-257`, `:276-277`) **in the same block the capture must happen in**.

**Verify:** both cases green; SC-005, SC-017, SC-002 still green; zero warnings.

---

## GROUP I — Stereo wandering (C-1 step 5)

### T017 — `runWanderStage`: the interleaved 64-sample control loop, D-4 compensation, FR-010/FR-010a

**Files edited:** `plugins/seraphis/src/processor/processor.h`,
`plugins/seraphis/src/processor/processor.cpp`,
`plugins/seraphis/tests/integration/effects_chain_test.cpp`.
**Depends on:** T001 ruling 4, T016.

**Failing tests FIRST** — one `TEST_CASE` plus four `SECTION`s in `effects_chain_test.cpp`:

**`TEST_CASE("Effects chain order matches C-1")` — SC-003** (the file already exists from T009; this adds
its three clauses beside the D-2 section). Isolated return = `render(mix=1) − render(mix=0)` at the tap,
send at mix 1.0, delay time 0, feedback 0, wander at width 200 %:

- **(a) step 5 precedes step 6.** *This clause alone measures the TRUE PLUGIN OUTPUT*, because the
  limiter is its subject. *Precondition:* drive the probe signal so the **pre-limiter** peak — read
  directly at `preOutputTapForTest()` — **exceeds `kLimiterCeilingLin = 0.8912509f`**
  (`plugins/seraphis/tests/integration/param_flow_test.cpp:63`), i.e. the limiter is provably in gain
  reduction; otherwise the clause is vacuous. *Assertion:* the **raw output sample peak** still respects
  the ceiling, allowing `kCeilingAllowanceDb = 0.1f`. **Positive control (mandatory):** with the probe's
  capability 2 running step 5 **after** step 6, the same render must **fail**.
- **(b) step 4 follows step 3.** Measured **at the tap**. *Precondition (redundant guard):* `kSoftLimitId`
  off and the probe level chosen so the output peak stays ≥ 3 dB under the ceiling. *Assertion:* doubling
  master gain scales the isolated return's RMS by **6.02 dB ± 0.1 dB**.
- **(c) step 4 precedes step 5.** *Assertion:* the **isolated return's own** M/S side energy scales with
  `kFxWidthId` across `{0 %, 100 %, 200 %}` **monotonically** and within **0.5 dB** of the ideal factor.
  Measured on the isolated return, not the bus, so a dry-only width change cannot satisfy it.

**`SECTION` "azimuth is unity at centre" — FR-010 / plan D-4.** At the tap, blocks ≤ 2048, with a steady
held chord: stepping `kFxAzimuthDepthId` from **0 to a small ε** (which crosses FR-010's skip boundary)
must change broadband RMS by **< 0.1 dB**. *An uncompensated `equalPowerGains` pair fails this by
3.01 dB* — that is what makes D-4's constant measured rather than asserted.

**`SECTION` "the azimuth pan pair is evaluated on the 64-sample grid" — FR-024.** Render **one
2048-sample block** with the wander live and assert the azimuth gain pair took **at most
`ceil(2048/64) = 32` distinct target values**. *The non-interleaved shape (control loop run to completion
before one audio call) reports **one**, because `setWidth`/`setTarget` only move targets.*

**`SECTION` "depth never reaches BrownianDrift::setDepth" — FR-024a.** Both drifts report
`getMean() == 0.0f` (`brownian_drift.h:171`) after `setupProcessing()`; `getDepth()` (`:170`) stays at
its prepared value across a full 0 → 1 → 0 sweep of **both** 1441 and 1443; and driving either to 0 makes
FR-010's skip take effect on **the same block**, measured as bit-exact identity against a
probe-bypassed render. *A 150 ms `kDriftOutputSmoothMs` path (`:103`) cannot do that, which is why depth
is a plugin-side multiply.*

**`SECTION` "the wander disengage does not step the image" — FR-010a.** Writing `kFxWanderDepthId = 0`
from a live wander must not produce a one-sample step: the max per-sample delta across the disengage
block must stay within **1.5 ×** the same statistic over a quiescent window of the same length from the
same render.

**Implement.**

*Members:* `Krate::DSP::MidSideProcessor globalMs_`, `Krate::DSP::BrownianDrift widthDrift_`,
`azimuthDrift_`, `Krate::DSP::OnePoleSmoother azimuthGainLSm_{1.0f}`, `azimuthGainRSm_{1.0f}`,
`bool fxWanderWasActive_`, `bool fxWanderRuns_`, `bool fxWanderRunsEffective_`,
`std::int64_t fxWanderSettleRemaining_`, `std::int64_t fxWanderSettleSamples_`, `float fxWidthBase_`.

*`setupProcessing()` (beside T012's block):* `globalMs_.prepare(float(sampleRate_), kMaxBlockSamples)`
(`:96`), `globalMs_.setWidth(effectsParams_.width)`, `globalMs_.reset()` (`:114`, snaps);
`widthDrift_.prepare(sampleRate_)` / `azimuthDrift_.prepare(sampleRate_)` (`:121`);
**`setMean(0.0f)` pushed explicitly to both** (`:165`, FR-024a — the mapping's symmetry about `base` and
about centre is load-bearing for the Edge cases); `setSeed(kSeedValues[si] ^ salt)` with the **two
distinct** salts (`:145`); `reset()` on both (`:133`);
`azimuthGainLSm_/RSm_.configure(kParamSmoothMs, float(sampleRate_))` and `.snapTo(1.0f)`;
`fxWanderSettleSamples_ = llround(max(MidSideProcessor::kDefaultSmoothingMs (`:73`, 10 ms),
kParamSmoothMs) × 3 × 0.001 × sampleRate_)` — three time constants of the slower of the two.
`setActive(false)` gains `globalMs_.reset(); widthDrift_.reset(); azimuthDrift_.reset();` and the
azimuth smoother snaps (FR-035).

*The predicate, in two parts (FR-010 + FR-010a's disengage arm):*

```
fxWanderRuns_ = !(width == 100.0f && wanderDepth == 0.0f && azimuthDepth == 0.0f)   // RAW atomics
fxWanderRunsEffective_ = fxWanderRuns_ || !wanderAtIdentity()
```
`wanderAtIdentity()` returns true only when `fxWanderSettleRemaining_ <= 0` **and**
`globalMs_.getWidth() == MidSideProcessor::kDefaultWidth` (`:236`, `:67`) **and** both azimuth smoothers
are `isComplete()` (`primitives/smoother.h:232`) at exactly `1.0f` **and** both depth smoothers are
`isComplete()` at exactly `0.0f`. `fxWanderSettleRemaining_` is re-armed to `fxWanderSettleSamples_` on
**every** block the raw predicate is true and decremented by `blockSamples` otherwise, so the disengage
tail is a **stated sample count**, not a guess. *(`MidSideProcessor` exposes only `getWidth()` — the
target, not its smoother's progress — and Non-goals forbids adding a `dsp/` accessor.)*
`runWanderStage`'s early return reads `fxWanderRunsEffective_`, **not** `fxWanderRuns_`.

*FR-010a's ENGAGE arm*, on `!fxWanderWasActive_ && fxWanderRunsEffective_`: `globalMs_`'s width smoother
does **not** advance while the stage is skipped (it advances only inside `process`,
`midside_processor.h:186-192`), so push the current width and **snap**: `globalMs_.setWidth(w);
globalMs_.reset(); azimuthGainLSm_.snapTo(1.0f); azimuthGainRSm_.snapTo(1.0f);` then ramp in over
`kFxReturnRampMs = 20` ms. `fxWanderWasActive_` is assigned `fxWanderRunsEffective_` at the end of every
stage call.

*The body — INTERLEAVED, and it must be* (plan R-14). Running the whole control loop first and calling
`globalMs_.process(l, r, l, r, n)` once afterwards delivers **nothing**: `setWidth` only stores
`width_` + `widthSmoother_.setTarget()` (`midside_processor.h:133-136`) and `OnePoleSmoother::setTarget`
only stores a target (`primitives/smoother.h:170`), so every iteration but the last is overwritten before
a single sample is touched — and the net grid becomes one update per **slice** (up to 2048 samples ≈
43 ms), not 64 samples.

```
off = 0
while off < n:
    chunkLen = min(64 - ((controlPhase_ + off) % 64), n - off)     // ABSOLUTE grid phase
    d_w    = widthDrift_.getCurrentValue()                         // brownian_drift.h:212, [-1,+1]
    depthW = fxWanderDepthSm_.getCurrentValue()                    // class (b) - READ ONLY
    width  = clamp(base + depthW * d_w * kWanderWidthSpanPercent,
                   MidSideProcessor::kMinWidth, kMaxWidth)         // :65-66
    globalMs_.setWidth(width)
    d_a    = azimuthDrift_.getCurrentValue()
    depthA = fxAzimuthDepthSm_.getCurrentValue()                   // class (b) - READ ONLY
    pos    = clamp(0.5f + 0.5f * depthA * d_a, 0.0f, 1.0f)
    equalPowerGains(pos, gl, gr)                                   // crossfade_utils.h:50 - ONCE per chunk
    azimuthGainLSm_.setTarget(gl * kFxAzimuthCentreComp)           // plan D-4
    azimuthGainRSm_.setTarget(gr * kFxAzimuthCentreComp)
    globalMs_.process(l+off, r+off, l+off, r+off, chunkLen)        // IN PLACE (:181)
    for i in [off, off+chunkLen): l[i] *= azimuthGainLSm_.process();  r[i] *= azimuthGainRSm_.process();
    off += chunkLen
```

`controlPhase_` is incremented **after** `renderSlice()` returns (`processor.cpp:825`), so
`controlPhase_ + off` is the correct absolute position of sample `off`. `azimuthGainLSm_/RSm_` are
**not** in `classBSmoothers()` — plugin-local ramps with no ParamID, advanced only here — so `.process()`
per sample is their sole advance and the T013 invariant is not violated. **Per-sample transcendentals are
forbidden** (C-5, FR-024).

**The skip is MANDATORY, not an optimisation.** Running `MidSideProcessor` at width 100 % is an
*algebraic* identity (`mid = (L+R)·0.5`, `side = (L−R)·0.5` at `midside_processor.h:200-201`,
reconstructed as `mid ± side` at `:225-226`) but **not** an IEEE-754 bit identity — each operation rounds,
so e.g. `L = 1.0f, R = 2⁻³⁰` reconstructs `R_out = 0.0f`. SC-002 asserts `max|a−b| == 0.0f`, and an
implementer who trusts a claimed identity and leaves the stage running fails it.

**Verify:** SC-003 (all three clauses + the positive control) and the four sections green; SC-002 still
green (at the C-6 defaults the raw predicate is false and every smoother is already at identity from
`setupProcessing()`, so `fxWanderRunsEffective_` is false from the first block and the latch never arms);
zero warnings.

---

## GROUP J — Whole-chain criteria

### T018 — SC-006 (true-peak ceiling) and SC-010 (determinism, seeds and salts)

**Files edited:** `plugins/seraphis/tests/integration/effects_chain_test.cpp` (only).
**Depends on:** T017.

**Failing tests FIRST — two cases.**

**`TEST_CASE("Effects at maxima respect the true-peak ceiling")` — SC-006.**
A **30 s** render with all 16 effects parameters at maxima (delay mix 1.0, feedback 0.95, width 200 %,
wander and azimuth depth 1.0, saturation 1.0), master gain at maximum, **8 voices held**: no **raw output
sample** exceeds `kLimiterCeilingLin = 0.8912509f` — the linear form of
`TruePeakLimiter::kDefaultCeilingDb = −1.0 dBFS` (`true_peak_limiter.h:46`) — allowing
`kCeilingAllowanceDb = 0.1f`, exactly as every shipped Seraphis ceiling assertion does
(`param_flow_test.cpp:59-63`, `processor_audio_test.cpp:148-153`, applied at `:810`).
**Deliberately NOT an independently-written 4× reconstruction**: the limiter bounds the signal at its own
4× oversampled resolution through its internal polyphase upsampler (`true_peak_limiter.h:38-42`,
`:110-125`), and a differently-written test-side interpolator reports inter-sample peaks a fraction of a
dB apart, turning a correct implementation into a failure.

**`TEST_CASE("Effects renders are seed-deterministic")` — SC-010.**
Two **independently heap-allocated** `Processor` instances (two `ProcessorFixture`s — `proc` is a
`std::unique_ptr`, `plugins/seraphis/tests/seraphis_test_fixture.h:164`), so their `SpectralDelay`
members sit at **different addresses**; the same seeded 20 s sequence, rendered in the same process, must
agree within `render_fingerprint.h`'s measured tolerances (`kSampleTolerance = 1.0e-4f`,
`kMetricTolerance = 1.0e-5`, `:48-52`). **No bit-exact float golden.**

**The render configuration is PINNED and MUST include a live wander:** send active **and** freeze
exercised **and** `kFxWidthId` off 100 % **and** `kFxWanderDepthId > 0` **and** `kFxAzimuthDepthId > 0`.
*Without the wander clause this criterion — the phase's only coverage of FR-026 — proves nothing about
the drifts:* at the C-6 defaults both depths are 0, FR-010's mandatory skip removes the whole stage from
the signal path, and a build that never calls `widthDrift_.setSeed`/`azimuthDrift_.setSeed` produces
identical fingerprints and passes.

Two further clauses:
- **(i)** Two different `kSeedId` indices produce **different** fingerprints, discriminated as a relative
  aggregate-metric difference **> 100 × `kMetricTolerance`** (i.e. **> 1e-3**).
- **(ii) Salt-swap control:** with the seed index held fixed, exchanging `kFxWidthDriftSalt` and
  `kFxAzimuthDriftSalt` must change the fingerprint by **> 1e-3**. This is what makes identical-salt
  lockstep (forbidden by C-5 / FR-024a) observable at all.

This case is **also the negative control for FR-027**: on one instance `this` is constant, so a build
that never calls `seedRng()` would still reproduce itself — two instances is the whole point
(`spectral_delay.h:223-225`).

**Implement.** Nothing new. A failure is a defect in T012's seeding order (`seedRng` **then** `reset`),
T014's seed burst, or T017's salts.

**Verify:** both cases green; every earlier case still green.

### T019 — SC-008: every Phase 10 transition is click-free

**Files edited:** `plugins/seraphis/tests/integration/effects_chain_test.cpp` (only).
**Depends on:** T018.

**Failing test FIRST** — `TEST_CASE("Effects transitions are click-free")`, written in the shape of
Phase 9's SC-005 (`specs/seraphis-phase9-parameters/spec.md:2094-2123`).

*Transitions in scope — **six**:* freeze-on, freeze-off, send-engage, send-bypass, wander-bypass engage,
wander-bypass disengage (FR-010a).

1. *Test statistic.* For each transition, `maxPerSampleDelta` over the **±10 ms window centred on
   `event sample + 1024 (AetherReverb latency) + 512 (kFxSendChunkSamples) + 1024 (fftSize) = event +
   2560 samples`** — i.e. **positioned in the OUTPUT domain**. Phase 10 stacks the send's accumulator
   delay and its FFT latency on top of the reverb's; an unshifted window is ~53 ms off the event at
   48 kHz and measures the wrong audio entirely.
2. *Reference.* **One window per measured transition**, of the **same 20 ms length**, drawn from the
   **same render** at offsets at least **50 ms clear of any transition** in the same output domain,
   uniformly spaced. "Quiescent" means exactly that — not silence.
3. *Bound.* `max(test statistics) ≤ 1.5 × max(reference statistics)`, with the **same number of draws on
   both sides**.
4. *Non-finite clause.* No sample of the render is non-finite, **tested by bit pattern**, never
   `std::isnan`.

*Positive controls — **both mandatory**:*
- **(a) Detector wiring.** The same statistic over a non-transition window with a deliberately injected
  one-sample step of **2× that window's own `maxPerSampleDelta`** must **exceed** the bound.
- **(b) Criterion wiring.** With the probe's **capability 3** snapping the 20 ms return-gain ramp
  (FR-008/FR-009) to instant, the same render must **fail** clause 3.

**Implement.** Only if a transition fails: the fix is in the ramp, the engage snap (FR-010a) or the
priming (D-5) — never in the bound or the window.

**Verify:** the case and both positive controls green.

---

## GROUP K — Cadence, lifecycle, state push-through

*T020 → T021 → T022, sequential (three different existing/new files, none `[P]` since none is a new file
this task creates from scratch except where noted).*

### T020 — `param_cadence_test.cpp`: SC-018 clauses (a)–(e)

**Files edited:** `plugins/seraphis/tests/integration/param_cadence_test.cpp` (only).
**Depends on:** T019.

**Failing test FIRST** — `TEST_CASE("Effects push cadence")`, in the shape of the file's existing
Phase 9 cases:

- **(a)** `spectralDelayResetCountForTest()` increases by **exactly 1** per bypassed→active transition
  that satisfies **both** FR-008 conditions (bypassed longer than `kFxSendDrainMs`, **and** not
  freeze-forced) and by **0** otherwise — explicitly including a **freeze-forced engage** (FR-023a) and a
  **sub-`kFxSendDrainMs` excursion** (FR-009a).
- **(b)** `widthDriftBlockCountForTest()` equals the `process()` **block count** under **both** bypass
  states (FR-011 — the drifts advance while bypassed, so re-engaging does not restart a wander that was
  conceptually running).
- **(c)** `bypassPredicateEvalCountForTest()` equals the `process()` **call** count over a render whose
  blocks are subdivided into **multiple MIDI slices** — **not** the slice count (FR-012).
- **(d)** Driving any of IDs **1410–1443** leaves `applyVoiceParamsCallCountForTest()` (`processor.h:172`)
  and `applyAetherParamsCallCountForTest()` (`:186`) **unmoved**, proving `Route::FX` bumps no generation
  counter (FR-019); driving **1400** moves neither either (it is `Route::ENG`).
- **(e) FR-007 (plan D-8 clause 2).** Over a render held entirely at the C-6 defaults,
  `sendChunkCountForTest()` stays **0**. Over a render that engages and later bypasses the send, it
  advances **only** while the send is active or draining — the last increment lands no later than the
  block on which the state returns to `Bypassed`. *This is what moves FR-007 into the CI-gated suite:
  SC-012's `[.perf]` threshold is outside the gate, and SC-002 cannot see the violation at all, because
  at mix 0 the mix loop adds `fxOut[i] * 0.0f` and a fully-running send still leaves the bus
  bit-identical.*

**Implement.** Nothing new unless a clause fails.

**Verify:** `seraphis_tests.exe "Effects push cadence"` green; the file's Phase 9 cases still green.

### T021 — `lifecycle_test.cpp`: FR-035's four named subjects

**Files edited:** `plugins/seraphis/tests/unit/lifecycle_test.cpp` (only).
**Depends on:** T020.

**Failing test FIRST** — a new `TEST_CASE` (or `SECTION` in the file's existing lifecycle case): after
`setActive(false)` → `setActive(true)`, a render from the same script equals a
fresh-`setupProcessing()` render within `render_fingerprint.h` tolerance, with an **explicit clause for
each of FR-035's four named subjects**:

1. the **send** — `sendChunkCountForTest()` restarts from the cleared FIFO state (no residual chunks
   from before the deactivate);
2. the **M/S stage** — `globalMs_.getWidth() == MidSideProcessor::kDefaultWidth` (100.0f);
3. **both drifts** — reset (observed as an identical fingerprint to the fresh-prepare render with the
   same seed);
4. the **return-gain ramp** — `fxReturnGainSm_` back at **0**.

**Implement.** Only if a clause fails: the fix is T012's `setActive(false)` block, which must call
`spectralDelay_.reset()`, `globalMs_.reset()`, both drift `reset()`s, `clearFifos()` + the state
re-initialisation, the azimuth smoother snaps and `fxReturnGainSm_.snapTo(0.0f)`.

**Verify:** `seraphis_tests.exe "*lifecycle*"` green.

### T022 — `state_v3_test.cpp`: FR-034's push-through clause

**Files edited:** `plugins/seraphis/tests/unit/state_v3_test.cpp` (only).
**Depends on:** T021.

**Failing test FIRST** — a new `SECTION` in the T008 case: call `setupProcessing()`, then `setState()`
with an **all-non-default** effects blob, then render **one block**, then assert the **component getters**
equal the blob's values — `getBaseDelayMs()`, `getFeedback()` (against the tilt-compensated value),
`getDiffusion()`, `getStereoWidth()`, `getSpreadDirection()`, `getTimeMode()`, `getNoteValue()`, and
`globalMs_.getWidth()`.

*SC-009 alone cannot cover FR-034:* it compares values read back out of the `EffectsParams` **atomics**,
so a build that loads state into the atomics and never re-pushes them to the DSP passes SC-009 while the
loaded patch renders with prepare-time defaults. This clause asserts the **push** happened.

**Implement.** Only if it fails: `pushAllSurfaces()` (`processor.cpp:1622-1675`) must invalidate every
effects push tracker (T008).

**Verify:** `seraphis_tests.exe "Seraphis_StateVersion3*"` green.

---

## GROUP L — Phase 9's continuity contract

### T023 — `param_continuity_test.cpp`: 85 → 101 rows, 9 → 12 class-(b) IDs (FR-038a 5–9, FR-038b, SC-001a)

**Files edited:** `plugins/seraphis/tests/integration/param_continuity_test.cpp` (only).
**Depends on:** T022.

**Failing test FIRST — the file already fails to compile** the moment T005 registered 16 more IDs:
`static_assert(std::size(kContinuityMechanism) == 85, …)` (`:478-479`) and
`static_assert(std::size(kClassBIds) == 9, …)` (`:501`).

**Implement — six edits, none of which may be worked around by exempting the band.**

1. `static_assert(std::size(kContinuityMechanism) == 101, "SC-005: 107 registered, less kSeedId and the
   five CFG IDs");` (`:478-479`) — **message text updated**.
2. `static_assert(std::size(kClassBIds) == 12, …)` (`:501`) — message updated to say twelve and to name
   the three Phase 10 additions.
3. `REQUIRE(count == 107)` at **both** `:712` and `:811`.
4. **Add 16 `ContinuityRow`s** to `kContinuityMechanism` (struct at `:141-167`: `ParamID id`,
   `Class {ComponentInternal, ProcessorSmoothed}`, `Evidence {Smoother, Ramp, SnapshotAtBirth,
   CoefficientOnly, PhaseContinuous, Structural}`, **mandatory `const char* citation` (file:line)**,
   `float smoothMs`). The both-ways gate `CHECK(table == expected)` (`:749`) fails in **both** directions,
   so neither a missing row nor a stray one can ship. Classification is **FR-038b's table, verbatim**:

   | ID | Class | Evidence | `smoothMs` | Citation |
   |---|---|---|---|---|
   | 1400 | (a) | `Smoother` | 0.0f | `setOutputSaturation` (`seraphis_engine.h:670-675`) → `TapeSaturator::setSaturation` → `saturationSmoother_` (`tape_saturator.h:248-252`), configured at `kDefaultSmoothingMs` (`:160`) |
   | 1410 | **(b)** | `Smoother` | `kBodySmoothMs` | processor-owned `fxReturnGainSm_`; **IS** the FR-008/FR-009 ramp — one smoother, not two |
   | 1411 | (a) | `Smoother` | 0.0f | `baseDelaySmoother_.setTarget` (`spectral_delay.h:427`) |
   | 1412 | (a) | `Smoother` | 0.0f | `spreadSmoother_.setTarget` (`spectral_delay.h:434`) |
   | 1413 | (a) | **`Structural`** | 0.0f | `setSpreadDirection` is a plain assignment (`spectral_delay.h:440`); the enum is read only inside `calculateBinDelayMs`'s switch (`:587-596`). **No smoother exists — do not cite one.** |
   | 1414 | (a) | `Smoother` | 0.0f | `feedbackSmoother_.setTarget` (`spectral_delay.h:462`) |
   | 1415 | (a) | `Smoother` | 0.0f | `tiltSmoother_.setTarget` (`spectral_delay.h:470`) |
   | 1416 | (a) | `Smoother` | 0.0f | `diffusionSmoother_.setTarget` (`spectral_delay.h:491`) |
   | 1417 | (a) | `Smoother` | 0.0f | `stereoWidthSmoother_.setTarget` (`spectral_delay.h:514`) |
   | 1418 | (a) | `Smoother` | 0.0f | `setTimeMode` is a plain assignment (`:525`), but the mode is consumed in `process()`, which pushes the synced time through the **same** `baseDelaySmoother_.setTarget(syncedMs)` (`:322-336`), read at `:646` |
   | 1419 | (a) | `Smoother` | 0.0f | `setNoteValue` is a plain assignment (`:533`); same path as 1418 (`:330`, `:336`, `:646`) |
   | 1430 | (a) | **`Ramp`** | 0.0f | `setFreezeEnabled` is a plain assignment (`:480`), but engagement crossfades over `kFreezeCrossfadeTimeMs = 75.0f` ms (`:906`), increment derived at `:210-212`, applied at `:698-706`. **`Smoother` is the wrong evidence here.** |
   | 1440 | (a) | `Smoother` | 0.0f | `MidSideProcessor::setWidth` → `widthSmoother_` (`midside_processor.h:133-136`), advanced per sample inside `process` (`:186-188`) |
   | 1441 | **(b)** | `Smoother` | `kBodySmoothMs` | processor-owned `fxWanderDepthSm_` (plugin-side depth multiplier, FR-024a) |
   | 1442 | (a) | **`CoefficientOnly`** | 0.0f | `BrownianDrift::setSmoothness` clamps and calls `updateCoefficients()` (`brownian_drift.h:152-155`) — it retunes the walk's correlation time and never steps its output |
   | 1443 | **(b)** | `Smoother` | `kBodySmoothMs` | processor-owned `fxAzimuthDepthSm_` |

   **Tally: 13 class (a) + 3 class (b) = 16.** Every citation is **mandatory** and gated at `:728-733`
   (non-null, contains `':'`, longer than 16 chars) — the table's remedy rule is one-directional: an ID
   may not be moved *into* class (a) without a file:line citation of the smoother that covers it.
5. `kClassBIds` (`:495-501`) gains `kFxDelayMixId`, `kFxWanderDepthId`, `kFxAzimuthDepthId`.
   **The shipped time-constant gate is two-valued — read it before editing:** `:770-780` admits
   `row.smoothMs == kBodySmoothMs || row.smoothMs == kDepthSmoothMs` on class-(b) rows (where
   `kBodySmoothMs = Seraphis::kParamSmoothMs = 20 ms` and `kDepthSmoothMs = 300 ms`) and `== 0.0f` on
   class-(a) rows. **Do not "simplify" it to "class (b) == kParamSmoothMs"** — that breaks the seven
   existing 300 ms rows. The three new class-(b) rows carry **`kBodySmoothMs`**, and the per-ID pin at
   `:781-793` (today naming 100-104/1215/1216 → `kDepthSmoothMs` and 801/802 → `kBodySmoothMs`) gains the
   three new IDs **alongside 801/802**.
6. **No effects ID is added to `kExemptIds`** (`:488`, exactly six IDs today: `kSeedId` and the five CFG
   IDs). The per-ID automation sweep (`:822-826`) then measures all 16 under Phase 9's **unchanged 1.5×
   click bound**, in addition to SC-008's six transitions. Exempting the band would leave the effects
   surface covered only by six named transitions.

**Verify (SC-001a):** the file compiles; `seraphis_tests.exe "*Continuity*"` green, including the
both-ways exhaustiveness gate and the per-ID sweep. Zero warnings.

---

## GROUP M — Performance

### T024 — `effects_perf_test.cpp` (NEW body): SC-011, SC-012, SC-013

**Files edited:** `plugins/seraphis/tests/integration/effects_perf_test.cpp` (created as a stub by T010).
**Depends on:** T023.

**All three cases are `[.perf]`-tagged and stay OUT of the CI gate.** They are measured under **one
protocol, and only that one** — *the SC-013 protocol*: a **fresh-boot, idle machine**, **seven**
consecutive whole-suite runs of `seraphis_tests.exe "[.perf]"`, each figure itself a **best-of-16**, and
the **worst of the seven** is the reported figure. That is verbatim the protocol Phase 9's shipped SC-009
baseline was pinned under (`plugins/seraphis/tests/integration/param_perf_test.cpp:133-156`). The
"worst of six" rule anchored to the **withdrawn T028 hot banner** (`:65-84`) is struck — `:65-84` survives
only as the *formatting shape* of a BASELINE PROVENANCE banner.

**Failing tests FIRST — three cases.**

**`TEST_CASE("Effects stage is RT-safe", "[.perf]")` — SC-011.**
Under `tests/test_helpers/allocation_detector.h`: **zero** allocations, locks and exceptions across a
**60 s** render that toggles every bypass predicate **100×** *and* automates `kSeedId` across **≥ 16
index changes** (FR-027's second burst site). Over **≥ 16 events of each kind**: the **worst** block
containing a `spectralDelay_.reset()` (FR-008) and the **worst** block containing a `seedRng()` +
`reset()` pair (FR-027) each cost **≤ 5.0 % of one core at 48 kHz / 512-sample blocks = ≤ 533 333 ns**
against the 10 666 667 ns block period; **every other block ≤ 266 667 ns**; the **median** of each burst
kind is recorded in the banner. *The allocation/lock/exception clause is protocol-independent and is a
hard failure on any machine.*

**`TEST_CASE("Effects cost nothing at defaults", "[.perf]")` — SC-012.**
`effectsStageNsForTest() / effectsStageProcessCallsForTest()` **≤ 10 667 ns/block** (0.10 % of one core)
at 8 voices held, **Phase 9's SC-009 MIDI script in `param_perf_test.cpp`**, 48 kHz, 512-sample blocks,
all 16 effects parameters at their C-6 defaults. The case **additionally asserts the divisor equals the
number of `process()` calls the harness itself made**, so the two can never drift.
*Explicitly NOT a whole-render delta:* the live cold dataset's own run-to-run spread is **107 420 ns**
(`param_perf_test.cpp:148-156`, the poly-8 row ranging 2 123 410 … 2 230 830) — **10×** this threshold.

**`TEST_CASE("Effects stage stays inside its 2.5 % budget", "[.perf]")` — SC-013.**
Same seam and the same divisor (with the same harness-call-count assertion), at the **full-poly operating
point with voices sounding** (8 voices held, Phase 9's SC-009 MIDI script — **not** this spec's SC-009,
which is the state-v3 round-trip), all 16 effects parameters at maxima:
**≤ 266 667 ns/block = 2.5 % of one core**. The render **must include an automation point on 1410 / 1441 /
1443**, so plan D-6's 64-sample subdivision cost (measured at ≤ 11.7 % of whole-block wall time,
`param_perf_test.cpp:86-91`) is inside the measured figure rather than discovered later.
The per-run table is transcribed into the file under a **BASELINE PROVENANCE banner** in the shape of
`param_perf_test.cpp:65-84`.
*Budget derivation, recorded in the banner:* the pinned cold worst of Phase 9's SC-009 arm is
2 230 830 ns = **20.91 %** (`:443-456`); the absolute ceiling is `kFullPolyCeilingNs = 2 666 666.7` ns =
25 % (`:376`); **20.91 + 2.5 = 23.41 %**, leaving 1.59 points of margin. **If the stage measures above
2.5 %, the levers are the stage's own cost and the shipped defaults — the 25 % ceiling is never the
lever** (roadmap lines 313–326).

**Implement.** Nothing new unless a budget is breached.
*Note (plan R-8):* the timer costs two `steady_clock::now()` reads and one add **per slice**, plus one
`2n`-float copy pair for the tap; under D-6 a 2048-sample block can carry up to 32 sub-slices, i.e. up to
64 clock reads and 32 copies per block. SC-012's threshold is ~3 orders above a clock read even then. If
a future measurement shows otherwise, the documented remedy is a single `now()` per `process()` call
bracketing the whole slice loop.
*Note (plan R-3):* long silent drains are a classic denormal generator (2052 per-bin delay lines decaying
toward zero). `ScopedDenormalMode` is already armed at the top of `process()` (`processor.cpp:624`) and is
per-thread; `kFxSendDrainFloor = 1e-6` additionally stops the send long before values reach 1e-38.

**Verify:** `seraphis_tests.exe "[.perf]"` — all three green under the seven-run cold protocol; the
banner in the file carries the actual per-run table, not a placeholder.

### T025 — `param_perf_test.cpp`: `kNonDefaultTable` 91 → 107 and SC-014's unrelaxed 25 % gate

**Files edited:** `plugins/seraphis/tests/integration/param_perf_test.cpp` (only).
**Depends on:** T024.

**Failing test FIRST.** `static_assert(kNonDefaultTable.size() == 91, "SC-009: the table is EXHAUSTIVE
over the 91-parameter surface")` (`:902`) **does not fail to compile on its own** — the table is
hand-written — so an unrevised run would silently ship a table whose own self-description had become
false. Change it to **107** *and update the message text to say 107* as the first edit, which makes the
build red until the rows are added.

**Implement.**

1. **Add the 16 effects rows** at their **most-expensive end**, under the table's existing rule
   (`RowClass::NonDefault`, `:682-687`). `idsStrictlyIncreasing()` (`:916-924`) must still hold —
   1400 > 1217 ✓, so the block appends in ID order.
2. **"Most-expensive end" is OPERATIONAL, not argued.** For the four discrete rows (**1413** spread
   direction, **1418** sync, **1419** sync note, **1430** freeze) and the three continuous rows with no
   obvious CPU gradient (**1411** delay time, **1412** spread, **1415** tilt), **measure each candidate
   once** and transcribe the **costlier** value into the table, recording those measurements in a
   **BASELINE PROVENANCE banner beside the table**, in the shape of `:65-84`. A structurally-argued
   choice is **not** sufficient: SC-014's whole claim is that the table *is* the worst case, and only a
   measurement makes that provable.
3. **Any row whose most-expensive end coincides with its registered default** must be declared
   `RowClass::CoincidesWithDefault` and the `countRows(...)` assertions at **`:904-911`** updated
   accordingly. Concretely, `static_assert(countRows(RowClass::NonDefault) == 91 - 8 - 5 - 10, …)`
   (`:911`) becomes `== 107 - 8 - 5 - (10 + X)` where **X** is the number of effects rows classified
   `CoincidesWithDefault`, and `countRows(RowClass::CoincidesWithDefault) == 10` (`:908-910`) becomes
   `== 10 + X` with its message text extended to name the added IDs. The `ProcessorLocal == 8` and
   `ScenarioPinned == 5` assertions are unchanged (no effects ID is in either class).
4. **SC-014's gate is unchanged and unrelaxed.** The re-run must satisfy the absolute
   `kFullPolyCeilingNs = 2 666 666.7` ns (`:376`, 25 % of one core at 8 voices) **and** the run-time
   baseline gate `kBaselineFullPolyNs (2 318 840.0) × kRegressionFactor (1.15) = 2 666 666` ns
   (`:456`, `:379`, `:472-479`). **Neither is a lever** — and raising `kBaselineFullPolyNs` is not one
   either: `:454-455` records it is already the maximum both `static_assert`s admit. Measured under the
   **SC-013 seven-run cold protocol**; a breach on an ordinary dev or CI machine is not a failure of this
   criterion, and re-measuring under the stated protocol is the first response.
5. A companion figure at **16 voices** stays explicitly **non-gating**, following the roadmap's own
   poly-16 precedent (roadmap lines 318–321) — that precedent applies to polyphony *outside* the budgeted
   scenario and does **not** license demoting effects-on at 8 voices, which is inside it.

**Verify:** `seraphis_tests.exe "[.perf]"` green including this arm; the file's own `static_assert`s pass.

---

## GROUP N — Integration, gates and docs

### T026 — Full-suite run

**Depends on:** T025.

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -30
```

**Verify:** the last line reads `All tests passed (N assertions in M test cases)`. **Do not grep the
output** — Catch2 prints the summary. Also build **Debug** once and run the suite, so T013's two
`assert`s are live (they are compiled out in Release).
Additionally re-run the layers this phase did **not** touch only if something in `dsp/` changed — it did
not, so no `dsp_*_tests` run is required.

### T027 — Portability check

**Depends on:** T026.

```bash
node tools/check-portability.js
```

**Verify:** clean. A green MSVC build proves nothing about the GCC/AppleClang legs. Specific hazards this
phase creates: `std::abs` on `float` is **not** `constexpr` before C++23 on every leg (T004 uses the
branchless form); no `std::isnan`/`std::isinf`/`numeric_limits::infinity()` anywhere in Phase 10 source
**or** tests (bit-pattern checks only); no narrowing in brace init (use designated initializers for
`BlockContext`); the two TUs needing IEEE semantics are on the `-fno-fast-math` list and
`effects_perf_test.cpp` is deliberately **not**.

Also run the cheap repo gates: `node tools/lint-float-bit-goldens.js` (Phase 10 ships **no** bit-exact
float golden — SC-002's exact equality is a same-instance, same-code-path A/B, not a checked-in
reference) and the SIMD aligned-load lint (Phase 10 adds no SIMD, so it is a no-op).

### T028 — Static analysis

**Depends on:** T027.

```powershell
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja
```
and the `.sh` form (`./tools/run-clang-tidy.sh --target seraphis`) — **both scripts**, or the
Linux/macOS pre-commit lint silently skips it. **Verify:** clean; fix **all** warnings, not only ones in
new code (SC-015).

### T029 — Host validation

**Depends on:** T028.

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

**Verify:** clean at strictness 5, and the editor-lifecycle harness
(`tests/test_helpers/editor_lifecycle_harness.h`) still passes 10 open/close cycles (SC-016 — already
exercised by T007's TU). The post-build copy step to `C:/Program Files/Common Files/VST3/` may fail with
a permission error; that is expected and does not mean the build failed.

### T030 — Docs (FR-038)

**Depends on:** T029.

**Files edited:** `plugins/seraphis/CLAUDE.md`, `plugins/seraphis/CHANGELOG.md`.

- The parameter-band table's `1400+` row reads `| 1400+ | Effects | 10 |` today, where **"10" is the
  phase number, not a count**. It becomes `| 1400+ | Effects | 10 — shipped |`, matching how every other
  row records its phase. *(The spec's FR-038 text says "from 10 to 10 — shipped"; this note exists so an
  implementer does not read "10" as a parameter count and write "16".)*
- `CHANGELOG.md` gains the matching entry **in the same change as any `version.json` bump** — and
  `version.json` is the **only** generated-adjacent file a version bump touches (never `version.h`,
  `win32resource.rc` or `audiounitconfig.h`, which are generated).

**Verify:** `node tools/check-changelog-coverage.js` clean.

---

## Traceability — task ↔ requirement

| Task | Discharges |
|---|---|
| T001 | plan D-1…D-4 (OQ-1…OQ-4), D-7 (FR-040), D-8 (FR-041) |
| T002 | FR-013, FR-020, FR-031 |
| T003 | FR-017 |
| T004 | FR-014, FR-015, FR-016, FR-016a (helper), FR-032 (wire format) |
| T005 | FR-036, FR-037, **SC-001**, FR-038a cl. 1–2 (partial) |
| T006 | FR-038a cl. 2 + **the missed tenth obligation** (`parameter_surface_test.cpp:207-209`) |
| T007 | FR-038a cl. 1 |
| T008 | FR-018, FR-019, FR-021 (D-2), FR-031–FR-034, **SC-009** |
| T009 | FR-021 / plan R-2 |
| T010 | FR-039 (CMake registration, single task) |
| T011 | FR-040, FR-041, **SC-002** |
| T012 | FR-004, FR-005, FR-027, FR-028, FR-030, FR-035 (setActive), **SC-004** |
| T013 | FR-003, FR-003a, FR-007, FR-008, FR-009, FR-009a, FR-029, **SC-017** |
| T014 | FR-016a, FR-022, FR-023, FR-025, FR-026 |
| T015 | **SC-005**, **SC-019** |
| T016 | FR-023a, plan D-5, **SC-007**, **SC-011a** |
| T017 | FR-006, FR-010, FR-010a, FR-011, FR-024, FR-024a, plan D-4, **SC-003** |
| T018 | **SC-006**, **SC-010** |
| T019 | **SC-008** |
| T020 | FR-007 (CI-gated), FR-011, FR-012, FR-019, **SC-018 (a)–(e)** |
| T021 | FR-035 |
| T022 | FR-034 |
| T023 | FR-038a cl. 5–9, FR-038b, **SC-001a** |
| T024 | **SC-011**, **SC-012**, **SC-013** |
| T025 | FR-038a cl. 3–4, **SC-014** |
| T026–T029 | **SC-015**, **SC-016** |
| T030 | FR-038 |

**FR-001 and FR-002** are structural (C-1's seven-step order; the limiter always last) and are
discharged jointly by T011's `renderSlice` insertion point and proven by **SC-002** (T011) + **SC-003**
(T017) — never by inspection.
