# Tasks: Seraphis Phase 12 — Factory Presets & Release Readiness

**Spec:** `specs/seraphis-phase12-presets-release/spec.md`
**Plan:** `specs/seraphis-phase12-presets-release/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 12 (lines 602–610); hard precondition Phase 11.5
(lines 588–590, 653–655)
**Branch:** `feat/seraphis-phase1-life-modulators` (the single Seraphis branch — do not rename, do not
create a new one)
**Status:** TASKS — no implementation yet
**Date:** 2026-08-04

---

## How to read this file

- Tasks are **T001 …**, collected into **ordered GROUPS**. A group starts only when the previous group is
  green.
- `[P]` marks a task that is **parallel-safe within its group**: every file it creates or edits is touched
  by **no other task in this list**. Any task that edits a file another task also edits — the static test
  TU, the sweep TU, `tools/seraphis_preset_defs.h`, either `CMakeLists.txt` — **sits alone in its own
  group**. Because this phase's dependency chain runs through three shared files, most groups hold exactly
  one task; pretending otherwise produces merge conflicts, not speed.
  - **Exemption:** `specs/seraphis-phase12-presets-release/compliance.md` is **append-only and sectioned by
    criterion**. A task that appends only its own named section does not lose `[P]` over it.
- Every task is **self-contained**: exact files, the **failing test written FIRST** (file, `TEST_CASE`
  name, the assertions with their numbers), then the implementation intent, then the target that verifies
  it. The executor is assumed to have no other context — but MUST still open every cited `file:line`
  before editing and MUST NEVER guess a signature.
- Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
- **No commit tasks.** Commits happen outside this workflow.
- **No threshold in this file is a lever.** The only sanctioned cost levers are plan §4.2.3's pre-declared
  L-1…L-5, applied in order and recorded. Dropping presets, dropping a sample rate, dropping the chord arm,
  or loosening any dB / tolerance / count threshold is a failure of the criterion, not a fix.
- **Two prohibitions apply to every task below**, without restatement:
  1. **No bit-exact float golden**, including any integer digest derived from float bits (spec FR-027a;
     `ci.yml:162-166` lints it). The three byte-level comparisons this phase *does* make are legitimate and
     narrowly premised: a **serialized state stream** (SC-005 round-trip), the **`Info` XML** (pure ASCII),
     and **generator file bytes on one machine in one run set** (SC-016).
  2. **Never `std::isnan` / `std::isinf`.** macOS builds with `-ffast-math`; finiteness is checked by bit
     pattern through `Krate::DSP::detail::isNaN` / `isInf`, the same pair `processor.cpp:482` uses on
     untrusted stream floats.

### Build and run commands (Windows — always the full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target <target>
build/windows-x64-release/bin/Release/<target>.exe 2>&1 | tail -5            # summary only
build/windows-x64-release/bin/Release/<target>.exe "TestCaseName*" 2>&1 | tail -5
```

Targets used here: `seraphis_tests`, `seraphis_preset_generator`, `generate_seraphis_presets`, `Seraphis`,
and (T013 / T027 only) every other plugin suite. No `dsp_*` target is touched by this phase — **no new
class enters `dsp/`**, so `tools/lint-layers.js` and the SIMD-alignment lint have nothing new to police.

**This is the phase-wide roster, NOT a per-group build gate.** Two of these targets do not exist in the
CMake project graph until **T006** creates them (`seraphis_preset_generator`, `generate_seraphis_presets`);
before that, asking MSBuild for either one fails with `MSB1009: Project file does not exist` — a missing
target, not a defect. Target availability by group:

| Target | Exists from |
|---|---|
| `seraphis_tests`, `Seraphis`, all other plugin/`dsp_*` suites | already, pre-phase (T001 baselines them) |
| `seraphis_preset_generator`, `generate_seraphis_presets` | **T006 (GROUP 6)** — the `add_executable` / `add_custom_target` block lands in root `CMakeLists.txt` there, and only there |

A build gate run at the end of GROUP *n* must request only the targets available at GROUP *n*. Wiring the
generator targets earlier to satisfy a gate is forbidden: it would front-run T004/T005 (the target has no
source file before T005) and violate the single-CMake-audit rule stated directly below.

### The CMake convention in this list

`plugins/seraphis/tests/CMakeLists.txt` is **ENUMERATED, never globbed** — its own banner says so at
`:16-18`, `:29-32`, `:37-44`. An unregistered TU drops out of the build silently and Catch2 reports
*"No tests ran"* instead of a failure. Two complementary rules, both mandatory:

1. **The task that creates a new test TU appends that one file to the enumerated `add_executable` list
   (`:5-79`) and, where required, to the `-fno-fast-math -fno-finite-math-only` block (`:117-170`) inside
   its own task**, so the new case runs red-then-green from within the task that writes it.
2. **T026 is the single authoritative audit pass** over `plugins/seraphis/tests/CMakeLists.txt` **and** root
   `CMakeLists.txt`: every new file present exactly once, the fast-math block correct, the generator target
   and its custom target present, and the SC-021 case-count delta reconciled against T001's baseline.

Do not scatter any other CMake edit across tasks.

### Facts verified in-repo before this list was written (do not re-derive; do re-read before editing)

| Fact | Evidence |
|---|---|
| `makeSeraphisPresetConfig()` declares `{"Textures"}` only | `plugins/seraphis/src/preset/seraphis_preset_config.h:24-31`, banner "`Textures` is a SEED" at `:8-13` |
| `resources/presets/` holds exactly one entry, `Textures/` | `ls plugins/seraphis/resources/presets` → `Textures` |
| `plugins/seraphis/tests/unit/preset/` does **not** exist | `ls plugins/seraphis/tests/unit` → no `preset` dir |
| `${CMAKE_SOURCE_DIR}/tools` is already on `seraphis_tests`' include path | `plugins/seraphis/tests/CMakeLists.txt:97` |
| `SERAPHIS_RESOURCES_DIR` is already defined for `seraphis_tests` | `plugins/seraphis/tests/CMakeLists.txt:112` |
| `tools/seraphis_preset_generator.cpp` and `tools/check-preset-generator-determinism.js` do not exist | `ls tools/` |
| `plugins/seraphis/version.json` is `"version": "0.4.0"` | file read this session |
| `AllocationDetector` has **no** thread filter | `tests/test_helpers/allocation_detector.h:26-68` (`tracking_` `:66`, `allocationCount_` `:67`, `recordAllocation` `:53-57`), `AllocationScope` `:75-95`, file is 139 lines |
| Fixture API | `plugins/seraphis/tests/seraphis_test_fixture.h`: `prepare` `:179`, `setParam` `:219`, `setParamPoints` `:225`, `pushEvent` `:275`, `processBlock` `:343`, `audioL()`/`audioR()` `:357-358`, `checkCanaries()` `:371`, `reserveCapture` `:377`, `renderBlocks(n, bs, script)` `:389` (re-requests capacity at `:391`, **appends** at `:404-405`), `renderBlocks(n, bs)` `:413`, `capturedL/capturedR` `:172` |
| Generator CMake precedent | root `CMakeLists.txt:575-590` (`membrum_preset_generator` + `generate_membrum_presets`), `:611-700` (`gen_v2_fixtures`: plugin `processor.cpp` compiled as an executable source, links `KrateDSP KratePluginsShared sdk` **without** `vstgui_support`, SDK boilerplate at `:632-638`, warning posture `:684-700`) |

### Rulings already applied — there is no blocking input for this list

The 2026-08-04 clarifications (spec §Clarifications Q1–Q8) and the plan's open items are encoded below.
Recorded here so no executor re-opens them:

- **Q1 → FR-025a decode uses the shipped `load*Params` free functions in `getState()` order**, with
  cumulative offset tripwires and a mandatory 2868-byte total. **OI-3 applied:** `loadMorphParams`'s third
  parameter *is* the payload destination, so the four 541-byte `SpectralState`s are **not** hand-skipped —
  only `[partials]` is read by hand (T007).
- **Q2 → stimulus is pitch 60 / velocity 0.8f**, with an optional per-preset override, plus one 4-note
  chord bounded-arm render per preset at 44 100 Hz.
- **Q3 → two float tolerance classes, each measured then pinned at ~10× its own worst observed error**
  (T020 measures, T021 gates). They may not share one number.
- **Q4 → OQ-4 is NO.** No preset drives `[partials]`; both bitmasks all-zero, all 64 pans `0.0f`, asserted.
- **Q5 → seven categories, 42 presets (7 × 6), ≥ 5 per category floor.**
- **Q6 → mechanical pairwise distinctness floor + a human listening checkpoint.** **OI-4 applied:** the
  distance is computed on **unit-RMS-normalised** sustain buffers over `{peak, meanAbs, totalVariation}`
  (never `rms`, never checkpoints), absolute floor **0.02**, validated from below by an injected level-only
  twin (T019).
- **Q7 → version 0.5.0 lands in this phase; the release verdict is recorded explicitly as `DEFERRED`**
  until Phase 11.5's three exit criteria are cited as met. `1.0.0` is reserved for the Phase 13 release.
- **Q8 → FR-008a authoring ceiling (`A ≤ 12.0 s`, `Rel ≤ 10.0 s`) + the two heaviest arms tagged to the
  Windows leg.**
- **OI-1 applied:** `buildSeraphisInfoXml` lives in the shared definitions header, so FR-029 clause 2's byte
  comparison has one source (T004).
- **OI-2 applied:** SC-012 arm 2's tautology is repaired — the final window is compared against the
  **first** window at the same 1.0 dB allowance (T017).
- **OI-5 applied:** `setEffectsStageInstrumentedForTest(false)` immediately after `prepare()` in **both**
  the generator (T005) and FR-029's in-process regeneration (T020/T021).
- **OI-6 applied:** FR-005 gets a real test case even though the spec has no matching SC (T008).

---

## GROUP 1 — Baselines (nothing can be measured against nothing)

### T001 — Record the pre-phase baselines and create the compliance document

**Files:** create `specs/seraphis-phase12-presets-release/compliance.md`.

**No test.** This is a measurement task and produces the document every later task appends to.

**Do:**
1. Build and run the suite as it stands:
   `"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests`
   then `build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5`.
   Record the **exact** `test cases: N | N passed` line. This is SC-021's baseline; a TU that never gets
   registered exits 0 silently, and only the delta catches it.
2. Record `plugins/seraphis/version.json`'s current `"version"` value verbatim (expected `0.4.0`).
3. Record the **only in-repo whole-block Seraphis render cost datum**, with its provenance, so T016 has a
   prior to check its own measurement against:
   `plugins/seraphis/tests/integration/param_perf_test.cpp:92-93` (settled whole-block render,
   1 267 675 – 1 530 620 ns/block) against the block budget the same file derives at `:384-385`
   (10 666 666.7 ns) — the file calls this "a ~13 %-of-core chain" at `:102`. Read those lines; do not copy
   the numbers from this task without confirming them.
4. Create `compliance.md` with, at minimum, these empty sections to be appended to later: *Baselines*,
   *SC-016 determinism*, *SC-018 generator build*, *SC-019 install destination*, *FR-018 documentation
   verification*, *SC-027 sweep budget & levers applied*, *FR-029a measured tolerances*, *FR-027b
   distinctness floor & negative control*, *SC-020 pluginval*, *SC-021 case-count delta*, *SC-022 lints*,
   *SC-023 changelog*, *SC-024 roster*, *SC-025 Phase 11.5 gate*, *SC-029 listening checkpoint (42 rows)*,
   *Release verdict*.

**Verify:** `compliance.md` exists and its *Baselines* section carries the case count, the version string
and the RTF datum, each with a `file:line` or a command.

---

## GROUP 2 — FR-001's failing test (and the TU that will carry every static gate)

### T002 — Create the static harness TU, register it, and fail on the one-category config

**Files:**
- create `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp` (new directory `unit/preset/`);
- edit `plugins/seraphis/tests/CMakeLists.txt` — append `unit/preset/factory_preset_test.cpp` to the
  enumerated `add_executable(seraphis_tests …)` list (`:5-79`, after the Phase 11 block that ends at `:52`,
  **before** the "SECOND compilation" block at `:54`), under a new `# Seraphis Phase 12 …` comment that
  restates the enumerated-not-globbed rule; **and** append the same path to the
  `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (`:117-170`, before the
  `PROPERTIES` line at `:168`) with a one-line reason: *FR-009 checks stored floats for non-finiteness by
  bit pattern.*

**Failing test FIRST** — `TEST_CASE("Seraphis_FactoryPresets_CategoriesMatchConfig", "[seraphis][preset]")`,
including `"preset/seraphis_preset_config.h"` and `<filesystem>`:

1. **Ordered literal list (FR-001 clauses a+b).** `const std::vector<std::string> kExpected{"Textures",
   "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"};`
   `REQUIRE(cfg.subcategoryNames.size() == 7);`
   `REQUIRE(cfg.subcategoryNames == kExpected);` — **element-wise and ordered**; a `std::set` comparison is
   order-blind and cannot carry this clause.
   `REQUIRE(cfg.subcategoryNames[0] == "Textures");` asserted **separately**, so the byte-exact-spelling
   clause names itself on failure.
2. **FR-001 clause c — the three unchanged fields.** `REQUIRE(cfg.pluginName == "Seraphis");`
   `REQUIRE(cfg.pluginCategoryDesc == "Synth");` `REQUIRE(cfg.processorUID == Seraphis::kProcessorUID);`
3. **SC-001 — filesystem bijection.** Root is
   `std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets"` (the define already exists,
   `tests/CMakeLists.txt:112` — do **not** write a walk-up loop). Collect directory names directly under it
   into a `std::set<std::string>`; require it equals the set of `kExpected`; require **0** entries of
   symmetric difference, and print the offending names on failure. Then walk with
   `std::filesystem::recursive_directory_iterator` and require **0** `.vstpreset` files at any depth other
   than exactly one level below the root.

**Expected failure right now:** clause 1 fails (`size() == 1`) and clause 3 fails (six directories missing).
Run it and paste the failure into the task's own notes before touching any implementation file.

**Verify:** `"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests` builds
with **0 warnings**, and
`build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_FactoryPresets_CategoriesMatchConfig*"`
**FAILS**.

---

## GROUP 3 — FR-001 implementation

### T003 — Extend the category set to seven and create the directories

**Files:**
- edit `plugins/seraphis/src/preset/seraphis_preset_config.h` (`:24-31`);
- create `plugins/seraphis/resources/presets/{Pads,Drones,Bells,Choirs,Motion,Cinematic}/` each with a
  `.gitkeep` (git does not track empty directories; the six `.gitkeep` files are removed in T014 when real
  presets land, exactly as `Textures/.gitkeep` is).

**Implement:** the **only** code edit is the fourth initializer:

```cpp
        /*.subcategoryNames =*/ {"Textures", "Pads", "Drones", "Bells",
                                 "Choirs", "Motion", "Cinematic"}
```

The comment-style designators are load-bearing and MUST stay in `PresetManagerConfig`'s declaration order
(`plugins/shared/src/preset/preset_manager_config.h:19-24`, banner at `:16-18`). `Textures` keeps its
byte-exact spelling and its existing directory: `PresetManager::parsePresetFile` matches the parent
directory name against `config_.subcategoryNames` by exact `==` and leaves `subcategory` **empty** on a miss
(`plugins/shared/src/preset/preset_manager.cpp:95-103`), so a rename orphans every user preset. Update the
header banner at `:8-13` to record that the Phase 12 extension has happened and the list is now
**additive-only** — do not delete the warning.

**Verify:** `seraphis_tests.exe "Seraphis_FactoryPresets_CategoriesMatchConfig*"` **passes**; 0 warnings.

---

## GROUP 4 — The shared definition table (pilot subset)

### T004 — `tools/seraphis_preset_defs.h` with a 3-preset pilot

**Files:** create `tools/seraphis_preset_defs.h`; edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`
(one added clause).

**ODR (run it, do not trust this line):**
`grep -rn "SeraphisPresetDef\|seraphis_preset_defs\|PresetDefs" dsp/ plugins/ tools/ tests/` must be
**0 hits** before you create the file. The name `PresetDef` is **forbidden** — five anonymous-namespace
structs already carry it (`tools/{disrumpo,gradus,innexus,ruinae}_preset_generator.cpp`,
`tools/preset_generator.cpp`), safe only because each lives in a separate executable. This header is
included by **three** TUs, **two of which link into one binary** (`seraphis_tests`), so the
anonymous-namespace trick is unavailable and the distinct name `SeraphisPresetDef` is mandatory.

**Implement** — `namespace Seraphis::PresetDefs`, data only. **No state layout, no component-stream
serialization (C-3), no `EditMessage` list (FR-016 / OQ-4 = NO).** Every function is defined `inline` in the
header (function-local `static const std::vector` for the table) — a non-`inline` definition is a
duplicate-symbol **link error** in `seraphis_tests`, not a style preference.

```cpp
struct ParamSetting { Steinberg::Vst::ParamID id; double normalized; };   // 0..1, as a host delivers
struct AuditionStimulus { Steinberg::int16 pitch; float velocity; };      // FR-024a override
struct SeraphisPresetDef {
    std::string_view name; std::string_view category; std::string_view description;
    std::vector<ParamSetting> params; std::optional<AuditionStimulus> stimulus;
};
inline constexpr std::array<std::string_view, 7> kCategories{
    "Textures", "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"};
[[nodiscard]] inline const std::vector<SeraphisPresetDef>& allPresets();
[[nodiscard]] inline const SeraphisPresetDef* findDef(std::string_view category, std::string_view stem);
[[nodiscard]] inline std::string buildSeraphisInfoXml(std::string_view presetName,
                                                      std::string_view subcategory);
```

- `findDef` **MISS POLICY, in a comment at the declaration:** returns `nullptr`; callers MUST treat null as
  a **failure**, never as "use the default stimulus" — a silently-unmatched preset would render at pitch 60
  / velocity 0.8 and pass every arm while its authored outlier stimulus went untested.
- `buildSeraphisInfoXml` emits Membrum's exact six-attribute form
  (`tools/membrum_preset_generator.cpp:349-361`, read it) with Seraphis values:
  `MediaType="VstPreset"`, `PlugInName="Seraphis"`, `PlugInCategory="Synth"`, `Name={stem}`,
  `MusicalCategory={category}`, `MusicalInstrument={category}`. `\n` line endings written explicitly, so the
  bytes are identical on all three legs. It lives here — not file-local in the generator — so FR-029
  clause 2's byte comparison has **one** source (OI-1). It is neither state layout nor component-stream
  serialization, so FR-016a is satisfied.
- **Pilot content only:** exactly **three** entries, one each in `Textures`, `Pads`, `Drones`, drawn from
  T014's name table (`Vellum`, `First Light`, `Deep Well`), each touching 3–6 IDs. The pipeline is proven
  before authoring at volume (plan §8 step 2).

**Test edit (still test-first for the new clause):** add to
`Seraphis_FactoryPresets_CategoriesMatchConfig` a fourth clause — `std::equal` between
`Seraphis::PresetDefs::kCategories` and `makeSeraphisPresetConfig().subcategoryNames` element-wise. This
pins the generator's directory source to the runtime's browser source; without it the header's "MUST equal"
is a comment with no check.

**Verify:** `seraphis_tests.exe "Seraphis_FactoryPresets_CategoriesMatchConfig*"` passes with the new
clause; 0 warnings.

---

## GROUP 5 — The generator

### T005 — `tools/seraphis_preset_generator.cpp`

**Files:** create `tools/seraphis_preset_generator.cpp`.

**No new Catch2 case** — the generator's gate is T006's build+run, and its output is gated by every static
case from T008 onward. Write it against these fixed constraints, all read before editing:

1. **`moduleHandle` must be defined in THIS TU or the target does not link.**
   `extern/vst3sdk/public.sdk/source/main/moduleinit.cpp:21` declares `extern void* moduleHandle;` and
   dereferences it at `:85`. `plugins/seraphis/tests/vstgui_test_stubs.cpp` is 13 lines and defines **only**
   `GetPluginFactory` (`:12`); Seraphis's `moduleHandle` lives in `plugins/seraphis/tests/unit/test_main.cpp:24`,
   which is a Catch2 main and is **not** in the generator's source list. So:

```cpp
// Satisfies moduleinit.cpp's `extern void* moduleHandle;` (public.sdk/source/main/moduleinit.cpp:21).
// MUST be a MUTABLE global with EXTERNAL linkage - `static`, `const`, or an anonymous namespace all
// turn it into an unresolved external. Precedent: tools/gen_v2_fixtures/main.cpp:19.
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;
```

2. **Drive sequence (C-4 / FR-013)** — `Processor::processParameterChanges` is **private**, so the only
   supported route is a `process()` call carrying an `IParameterChanges`. Reuse the shipped fixture
   (`plugins/seraphis/tests/seraphis_test_fixture.h`) so the generator and T020/T021's in-process
   regeneration run **one** code path (plan R-2):

```cpp
SeraphisTest::ProcessorFixture fx;
/* REQUIRE-equivalent */ fx.prepare(44100.0, 64);                 // :179
fx.proc->setEffectsStageInstrumentedForTest(false);               // OI-5 / R-14: prepare() sets it TRUE
                                                                  // (:210); processor.h:1466-1470 documents
                                                                  // FALSE on every shipping path, and this
                                                                  // is a RELEASE-pipeline tool.
for (const auto& s : def.params) fx.setParam(s.id, s.normalized); // :219
fx.processBlock(64);                                              // :343 - ONE block latches the fan-out
Steinberg::MemoryStream stream;
fx.proc->getState(&stream);                                       // processor.cpp:1856
```

   Untouched IDs keep their registered defaults; `processParameterChanges` takes the **last** point of each
   queue (`processor.cpp:1923-1929`) and `setParam` writes one point at offset 0, so one block suffices.
   The four `SpectralState` payloads need **no** extra drive: `getState()` calls
   `syncAuthoringMirrorFromDropdowns()` first (`processor.cpp:1866`) then writes `spectralSlotsAuthoring_[s]`
   (`:1893-1897`), so setting IDs 409–412 through the fan-out places the right payloads. `[partials]` is
   written from members that are zero on a fresh `Processor` (`processor.cpp:1911-1914`) — FR-006a is
   satisfied by never touching the edit channel; **the generator has no `IMessage`/`notify()` surface.**

3. **Container writer** — file-local, anonymous namespace, modelled on
   `tools/membrum_preset_generator.cpp:363-405`:
   `"VST3"` (4 B) · `uint32 version = 1` (4 B) · class id 32 ASCII (32 B) · `int64 listOffset` (8 B) ·
   `Comp` payload (2868 B) · `Info` payload · at `listOffset`: `"List"`, `uint32 2`, then
   `{"Comp", int64 off, int64 size}`, `{"Info", int64 off, int64 size}`.
   The class id is derived **at run time**, never hardcoded:
   `Steinberg::char8 classIdAscii[33] = {}; Seraphis::kProcessorUID.toString(classIdAscii);`
   (`extern/vst3sdk/pluginterfaces/base/funknown.h:295` — 32-char uppercase hex).
   `std::ofstream` in `std::ios::binary`. The `Info` payload comes from
   `PresetDefs::buildSeraphisInfoXml` (T004) — **do not write a second template here.**

4. **`main` (FR-011)** — `argv[1]` is the output directory (default
   `plugins/seraphis/resources/presets`); create the seven category subdirectories under it; write
   `out/<category>/<name>.vstpreset` for every entry of `allPresets()`; return non-zero on any failure.
   Iteration order is the **definition order of `allPresets()`** — never a directory iteration, never an
   unordered container (half of FR-014's determinism; the other half is that no timestamp, path string or
   RNG enters any byte).

5. **FR-015 — no VSTGUI.** `processor.cpp`'s only `ui/` include is `ui/edit_message.h` (a POD plus three
   `constexpr` strings, banner: carries *"no VSTGUI type"*, `processor.cpp:18`, `:21`), and the parameter
   packs reach the SDK only through `plugins/shared/src/ui/parameter_helpers.h:11-15`. Do not add a VSTGUI
   include, and do not copy `tools/krate-render`'s link line — that tool needs `vstgui_support` only because
   `membrum_core` bundles controller and UI TUs.

**Verify:** deferred to T006 (this file cannot build until its CMake target exists). Do not claim it
compiles.

---

## GROUP 6 — Generator build + pilot proof

### T006 — Root CMake targets, MSVC + WSL/GCC build, pilot generation

**Files:** edit root `CMakeLists.txt` — insert immediately **after** the `membrum_preset_generator` /
`generate_membrum_presets` block (`:575-590`) and **before** the `gen_v2_fixtures` banner (`:592`).

**Implement** (target name and output directory are **fixed by `release.yml:158-174`**, not free choices):

```cmake
add_executable(seraphis_preset_generator
    tools/seraphis_preset_generator.cpp
    # Seraphis has no static core library - plugins/seraphis/CMakeLists.txt:18-67 puts every .cpp inside
    # one smtg_add_vst3plugin MODULE - so the processor is compiled as an executable source, exactly as
    # gen_v2_fixtures does for Gradus/Ruinae (root CMakeLists.txt:611-642).
    plugins/seraphis/src/processor/processor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
    # GetPluginFactory stub ONLY - this file is 13 lines and defines nothing else
    # (plugins/seraphis/tests/vstgui_test_stubs.cpp:12). moduleHandle is NOT here; it is defined in
    # tools/seraphis_preset_generator.cpp (T005).
    plugins/seraphis/tests/vstgui_test_stubs.cpp
)
target_link_libraries(seraphis_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)  # NO vstgui_support
target_include_directories(seraphis_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/src      # "plugin_ids.h", "parameters/..."
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/tests    # seraphis_test_fixture.h
    ${CMAKE_SOURCE_DIR}/tests/test_helpers        # <vst_event_list.h> - HEADER ONLY, not the
                                                  # test_helpers target (it links Catch2 INTERFACE)
    ${CMAKE_SOURCE_DIR}/tools
    ${vst3sdk_SOURCE_DIR}
)
target_compile_features(seraphis_preset_generator PRIVATE cxx_std_20)
set_target_properties(seraphis_preset_generator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_custom_target(generate_seraphis_presets
    COMMAND seraphis_preset_generator "${CMAKE_SOURCE_DIR}/plugins/seraphis/resources/presets"
    DEPENDS seraphis_preset_generator
    COMMENT "Generating Seraphis factory presets (42 presets across 7 categories)"
    VERBATIM
)
```

Warning posture copies `gen_v2_fixtures` (`:684-700`): MSVC `/W4 /permissive- /Zc:__cplusplus /wd4100
/wd4458`, otherwise `-Wall -Wextra -Wpedantic -Wno-unused-parameter`. Seraphis additionally needs MSVC
`/wd4459` for the `kPi` shadow in `timevar_comb_bank.h:915` — the same suppression
`plugins/seraphis/CMakeLists.txt:120-135` documents, **on this target only**.

**`release.yml` needs NO edit** — its `*` case already maps `seraphis` → `seraphis_preset_generator`
(`:158-166`), builds it (`:168-169`), runs `./build/bin/seraphis_preset_generator generated-presets`
(`:170-174`) and uploads with `if-no-files-found: error` (`:180`). Confirm by reading those lines; do not
edit the workflow.

**Verify — the gate is LINK + RUN, never "it compiled":**
1. `"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_preset_generator` —
   **0 warnings**, binary produced. (`moduleHandle` is a link-time-only failure and MSVC and GCC fail it
   identically.)
2. **WSL/GCC 13 build of the same target** (the project's standing probe route; a green Windows build
   proves nothing about the Linux leg `release.yml` depends on) — **0 warnings**, binary produced.
3. Run it into a scratch directory: `seraphis_preset_generator <tmp>` — seven directories created, exactly
   **3** `.vstpreset` files written (the pilot), each ≥ 2868 bytes.
4. `"$CMAKE" --build … --target generate_seraphis_presets` writes the same three files into
   `plugins/seraphis/resources/presets/`.
5. Append the SC-018 evidence (both build logs' warning counts, the file count) to `compliance.md`
   § *SC-018 generator build*.

---

## GROUP 7 — Harness machinery

### T007 — `plugins/seraphis/tests/preset_test_support.h` + the decode tripwire

**Files:** create `plugins/seraphis/tests/preset_test_support.h`; edit
`plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

**ODR (run it):** `grep -rn "DecodedPresetState\|decodePresetState\|preset_test_support\|PresetTestSupport\|fingerprintDistance" dsp/ plugins/ tools/ tests/`
must be **0 hits**.

**Failing test FIRST** —
`TEST_CASE("Seraphis_PresetSupport_DecodeConsumesWholeStream", "[seraphis][preset]")`:
build a fresh `SeraphisTest::ProcessorFixture`, `prepare(44100.0, 512)`, `getState()` into a
`Steinberg::MemoryStream`, copy the bytes out, then:
- `REQUIRE(comp.size() == 2868);`
- `DecodedPresetState st; std::string why; REQUIRE(decodePresetState(comp, st, why));` (message `why` on
  failure)
- `REQUIRE(st.version == 3);` (`kCurrentStateVersion`, `plugin_ids.h:27`)
- `REQUIRE(st.panOverrideBits == 0u); REQUIRE(st.maskBits == 0u);`
It fails to **compile** until the header exists — that is the red state; record it, then implement.

**Implement** — header-only, `namespace SeraphisTest`. **Every free function is `inline` and every table is
a function-local `static const`**: both new TUs link into `seraphis_tests`, so a non-`inline` definition is
a duplicate-symbol link error. Contents:

```cpp
// --- discovery (SERAPHIS_RESOURCES_DIR already exists, tests/CMakeLists.txt:112 - NO walk-up loop) ---
inline std::filesystem::path factoryPresetRoot();
inline std::vector<std::filesystem::path> allPresetFiles();           // SORTED - filesystem order is
                                                                      // unspecified (plan R-13)
// --- container ---
struct PresetFile { std::filesystem::path path; std::string stem, category, classIdAscii;
                    std::vector<std::uint8_t> comp; std::string info, parseError; };
inline PresetFile parseVstPreset(const std::filesystem::path&);
inline std::map<std::string, std::string> parseInfoAttributes(std::string_view xml);  // missing attr ->
                                                                                      // ABSENT, never a
                                                                                      // default
// --- typed decode (FR-025a) ---
struct DecodedPresetState { … };  // see below
inline bool decodePresetState(const std::vector<std::uint8_t>& comp, DecodedPresetState& out,
                              std::string& why);
// --- timeline (C-6.1) ---
struct SweepTimeline { double A, Rel, RT60, susBegin, susEnd, H, settle, W, total;
                       bool aetherFreeze, atmosOrFxFreeze; };
inline SweepTimeline makeTimeline(const DecodedPresetState&);
// --- measurement ---
inline bool bufferIsFinite(std::span<const float>);                   // BIT PATTERN, never std::isnan
inline double rmsOver(std::span<const float> l, std::span<const float> r,
                      std::size_t first, std::size_t lastExclusive);  // sqrt((SL²+SR²)/(2N))
inline std::vector<double> perSecondRms(std::span<const float> l, std::span<const float> r,
                                        std::size_t first, std::size_t lastExclusive, double sr);
inline double fingerprintDistance(const Krate::DSP::TestUtils::RenderFingerprint&,
                                  const Krate::DSP::TestUtils::RenderFingerprint&);
```

`DecodedPresetState` holds one instance of each shipped pack (`GlobalParams`, `MacroParams`, `CloudParams`,
`MorphParams`, `LifeModParams`, `BodyParams`, `AtmosphereParams`, `AetherParams`, `EffectsParams`), plus
`std::array<Krate::DSP::SpectralState,4> payloads`, `std::array<float,64> partialPan`, and
`std::uint64_t panOverrideBits, maskBits`, plus `Steinberg::int32 version`.

**`decodePresetState` — the read order is `getState()`'s, verbatim (`processor.cpp:1868-1914`), over an
`IBStreamer` on a `MemoryStream` built from `comp`.** Open each header and copy the **real** signature; do
not type these from the table:

| Step | Call | Bytes | Cumulative |
|---|---|---|---|
| 0 | `streamer.readInt32(version)` | 4 | 4 |
| 1 | `loadGlobalParams` — `global_params.h:218` | 12 | 16 |
| 2 | `loadMacroParams` — `macro_params.h:132` | 20 | 36 |
| 3 | `loadGlobalSeed` — `global_params.h:280` | 4 | 40 |
| 4 | `loadCloudParams` — `cloud_params.h:302` | 44 | 84 |
| 5 | `loadMorphParams(MorphParams&, IBStreamer&, std::array<SpectralState,4>&)` — `morph_params.h:400-402` | 52 + 2164 | 2300 |
| 6 | `loadLifeModParams` — `life_mod_params.h:298` | 40 | 2340 |
| 7 | `loadBodyParams` — `body_params.h:369` | 52 | 2392 |
| 8 | `loadAtmosphereParams` — `atmosphere_params.h:377` | 68 | 2460 |
| 9 | `loadAetherParams` — `aether_params.h:297` | 72 | 2532 |
| 10 | `loadEffectsParams` — `effects_params.h:442` | 64 | 2596 |
| 11 | `[partials]`: 64 × `readFloat` + 2 × `readInt64u` | 272 | **2868** |

- **OI-3:** step 5's third parameter **is** the payload destination, so the four 541-byte `SpectralState`s
  are decoded by the shipped loader — they are **not** hand-skipped. Only step 11 is read by hand, with the
  same `readFloat`/`readInt64u` calls `loadPartialOverrides` uses (`processor.cpp:472-499`) but into
  **plain, non-atomic fields with NO clamping**: the harness must see the raw stored bytes to assert
  FR-009's `[-1, 1]` range and FR-006a's all-zero rule, not the value the processor would install.
- **Tripwires, mandatory:** after **every** step assert `streamer.tell() == cumulative[step]` and name the
  block that drifted; at the end assert `tell() == 2868`; before decoding at all assert
  `comp.size() == 2868`. `tell()` is declared pure-virtual on `FStreamer`
  (`extern/vst3sdk/base/source/fstreamer.h:49`) and overridden public on `IBStreamer` (`:215`).

**`makeTimeline` — exact arithmetic (C-6.1), all inputs from the decoded state, nothing hardcoded per
preset:**

```
A      = (life.envMode == 1 /*Growth*/ ? life.growthDurationSec : life.stage0Ms * 1e-3)
         + life.stage1Ms * 1e-3
Rel    = life.releaseMs * 1e-3
RT60   = aether.decaySeconds                        // a TRUE RT60: aether_reverb.h:3139 uses it as t60dc
susBegin = A + 1.0        susEnd = A + 4.0
H      = A + 5.0
frozen = aether.freeze || atmos.freeze || effects.spectralFreeze
G      = frozen ? 2.0 : 0.5
Settle = H + Rel + G
W      = aether.freeze ? 60.0 : (frozen ? 20.0 : 10.0)
Total  = Settle + W
```

`envMode` is the list index with Standard = 0, Growth = 1 (`dropdown_mappings.h:190-191`). Sample
conversion is `n(t) = static_cast<std::size_t>(std::llround(t * sampleRate))`, windows half-open
`[n(t0), n(t1))`.

`bufferIsFinite` uses `Krate::DSP::detail::isNaN` / `isInf` — the pair `processor.cpp:482` uses.

**Verify:** `seraphis_tests.exe "Seraphis_PresetSupport_DecodeConsumesWholeStream*"` passes; 0 warnings.

---

## GROUP 8 — Static gates: inventory and names

### T008 — SC-002 (count/distribution) and FR-005 (names valid + unique)

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

**Failing tests FIRST** (both fail today: only 3 pilot presets exist):

1. `TEST_CASE("Seraphis_FactoryPresets_CountAndDistribution", "[seraphis][preset]")`
   - `const auto files = SeraphisTest::allPresetFiles(); REQUIRE(files.size() == 42);`
   - per-category counts from the parent directory name: `REQUIRE(count[c] >= 5)` for all seven, and
     **report** (`INFO`/`WARN`, not gate) whether each equals the C-2 target of 6.
   - failure message prints the per-category tally.
2. `TEST_CASE("Seraphis_FactoryPresets_NamesAreValidAndUnique", "[seraphis][preset]")` — FR-005, OI-6:
   - `REQUIRE(!files.empty());` first, so a vacuous pass is impossible.
   - (a) `REQUIRE(Krate::Plugins::PresetManager::isValidPresetName(stem));` for **42/42** — call the shipped
     `static bool` (`plugins/shared/src/preset/preset_manager.h:120`); do **not** re-derive the rejected set
     (`/\:*?"<>|` plus empty/oversize, `preset_manager.cpp:491-501`).
   - (b) the set of stems has size **42** — uniqueness across the **whole library**, not per category: the
     browser's flat list and its search collide on duplicates regardless of directory
     (`preset_manager.cpp:123-147`).
   - (c) every code unit of every stem `< 0x80`, checked on `unsigned char` (never on a possibly-signed
     `char`).
   - (d) `stem ==` the `Info` XML `Name` attribute (via `parseInfoAttributes`), cross-linking the metadata
     side so a rename that touches only one side fails in both places.

**Implement:** nothing but the tests — they gate T014's authoring. Both stay **red** until T014.

**Verify:** builds with 0 warnings; both cases **FAIL** with a message that names the current count (3) and
the missing categories. Record that this red state is expected and is cleared by T014.

---

## GROUP 9 — Static gates: container, stream, round-trip

### T009 — SC-003, SC-004, SC-005

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

**Failing tests FIRST** — each begins `REQUIRE(!files.empty());` so a vacuous pass is impossible; each runs
over **every** file and reports the failing path:

1. `TEST_CASE("Seraphis_FactoryPresets_ContainerIsValid", "[seraphis][preset]")` — via `parseVstPreset`:
   `REQUIRE(pf.parseError.empty());` magic `"VST3"`; `REQUIRE(pf.classIdAscii == expectedClassId)` where
   `expectedClassId` is produced **at run time** by `Seraphis::kProcessorUID.toString(buf)` (32-char
   uppercase hex, `funknown.h:295`) — never a literal; a `List` chunk carrying **both** `Comp` and `Info`
   entries; every `offset + size` inside the file. **42/42.**
2. `TEST_CASE("Seraphis_FactoryPresets_StreamIsCurrentVersion", "[seraphis][preset]")` — first `int32` == **3**
   and `pf.comp.size() == 2868` **exactly**. No preset may be a truncated v1/v2-prefix stream even though
   the loader would accept one (`processor.cpp:1762-1790`).
3. `TEST_CASE("Seraphis_FactoryPresets_RoundTripByteIdentical", "[seraphis][preset]")` — fresh
   `ProcessorFixture`, `prepare(44100.0, 512)`, `setState(MemoryStream(comp)) == kResultOk`, then
   `getState()` into a second stream and `std::memcmp` == **0** over 2868 bytes, for 42/42, with **0** failed
   `setState` calls. This is a *serialized state stream* comparison — the explicitly sanctioned carve-out —
   not a float render digest.

**Implement:** nothing but the tests.

**Verify:** builds with 0 warnings; all three pass over the 3 pilot presets and their count assertions stay
red only in T008's cases.

---

## GROUP 10 — Static gates: metadata, browser scan, tabs

### T010 — SC-006, SC-007, SC-008

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_FactoryPresets_InfoMetadataMatchesDirectory", "[seraphis][preset]")` — FR-021 /
   SC-006. `parseInfoAttributes` must yield **exactly the six** ids and, for 42/42:
   `MediaType == "VstPreset"`, `PlugInName == "Seraphis"`, `PlugInCategory == "Synth"`, `Name == stem`,
   `MusicalCategory == MusicalInstrument ==` the parent directory name, and that directory name is a member
   of `makeSeraphisPresetConfig().subcategoryNames`. Threshold: **0** mismatches.
2. `TEST_CASE("Seraphis_FactoryPresets_BrowserScanFilesEveryPreset", "[seraphis][preset]")` — FR-022 /
   SC-007. Construct
   `Krate::Plugins::PresetManager(makeSeraphisPresetConfig(), nullptr, nullptr, tempUserDir, factoryRoot)`
   — the 4th and 5th ctor parameters (`plugins/shared/src/preset/preset_manager.h:55-61`). **`tempUserDir`
   is load-bearing, not hygiene:** `scanPresets()` scans the **user** directory *first*
   (`preset_manager.cpp:41-49`), so leaving it at the real machine location makes the count and the
   `isFactory` tally depend on whatever the developer previously saved. Create it fresh in the case and
   remove it at the end. Assert: `scanPresets()` returns **42** entries; **0** with an empty `subcategory`
   (the Membrum failure mode, `preset_manager.cpp:95-103`); **0** with `isFactory == false`; and
   `getPresetsForSubcategory(c).size()` (`:75`) equals the on-disk count for **all seven** categories.
3. `TEST_CASE("Seraphis_PresetBrowser_TabsMatchConfig", "[seraphis][preset]")` — FR-023 / SC-008. Rebuild
   the tab vector **exactly as `Controller::togglePresetBrowser` does** (`controller.cpp:463-470`: `"All"`
   then `config.subcategoryNames` inserted in order) and compare **element-wise against the literal
   eight-element vector** `{"All","Textures","Pads","Drones","Bells","Choirs","Motion","Cinematic"}`.
   **Not** against `{"All"} ∪ subcategoryNames` — both sides of that comparison read the same config, so it
   is a tautology that passes for any wrong or reordered list. The literal is the second, independent copy
   that makes the assertion able to fail.

**Implement:** nothing but the tests. Cases 1 and 3 go green immediately; case 2 stays red on the count
until T014.

**Verify:** builds with 0 warnings; cases 1 and 3 pass; case 2's failure names the count.

---

## GROUP 11 — Static gates: the authoring ledgers

### T011 — SC-013 coverage, SC-014 budget, SC-014a timing ceiling, FR-006a + FR-009

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

These four cases are what make T014's authoring *verified as it goes* rather than a search. All decode via
`decodePresetState` (T007) — **never** from the definition table, and never by re-deriving a denormalized
value with arithmetic.

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_FactoryPresets_CoversShippedSurface", "[seraphis][preset]")` — FR-007 / SC-013.
   Decode all files, build a coverage ledger, require **every** row of the C-2 matrix hit by ≥ 1 preset, and
   print the **missing value and its ParamID** on failure:
   - Body material (ID 800): Glass, Strings, Metal Plate, Chamber, Ice — **all 5** (`dropdown_mappings.h:198-200`)
   - Factory spectral state (IDs 409–412): Sine Stack, Bell, Choir, Glass, Breath — **all 5** (`:178-180`)
   - Morph state count (408): **2, 3 and 4** (`:165-166`)
   - Travel mode (403): **both** values (`:118`)
   - Envelope mode (700): Standard **and** Growth (`:190-191`)
   - Grain envelope (1016): **≥ 3 of the 6** (`:211-213`)
   - Freeze toggles 1008 / 1204 / 1430: each **ON in ≥ 1** preset and **OFF in ≥ 1** preset
   - Delay sync (1418): ON and OFF
   - Body resonator bypass (812) and input AGC (811): each ON and OFF
2. `TEST_CASE("Seraphis_FactoryPresets_RespectVoiceBudget", "[seraphis][preset]")` — FR-008 / SC-014:
   `global.polyphony <= 8` and `global.softLimit == true` for **42/42**. (Roadmap lines 313–321: 24.21 % of
   one core at 8 voices vs 47.36 % at 16.)
3. `TEST_CASE("Seraphis_FactoryPresets_RespectTimingCeiling", "[seraphis][preset]")` — FR-008a / SC-014a:
   `makeTimeline(st).A <= 12.0` and `.Rel <= 10.0` for **42/42**; the message prints the decoded `envMode`
   and the growth / stage0 / stage1 / release values. A breach is **re-authored, never measured with a
   capped `A`**.
4. `TEST_CASE("Seraphis_FactoryPresets_PartialsBlockIsInert", "[seraphis][preset]")` — FR-006a + FR-009.
   - `panOverrideBits == 0` and `maskBits == 0`; all 64 pans exactly `0.0f` **and** within `[-1, 1]` (the
     range `loadPartialOverrides` clamps to, `processor.cpp:485`).
   - FR-009's finiteness clause is a **typed enumeration, never a 4-byte walk of the stream**. A walk is
     wrong for 2164 of the 2868 bytes: a payload is `uint8` version at offset 0, `int32 numPartials` at 1,
     two scalars at 5 and 9, two 64-float arrays at 13 and 269, and a **16-byte `name` char array at
     525-540** (`dsp/include/krate/dsp/processors/spectral_state.h:192-205`) — a walker would reinterpret a
     letter as a float. Instead:
     (i) for each of the **nine decoded packs**, bit-pattern-check **every named `float` member** and
     increment that pack's field counter; assert the counters equal the documented counts — `[global]` 3,
     `[macro]` 5, `[seed]` 1, `[cloud]` 11, `[morph]` 9 scalars, `[life]` 10, `[body]` 13, `[atmos]` 17,
     `[aether]` 18, `[effects]` 16 (each = that block's byte size / 4), so a `float` added in a later phase
     fails the count rather than silently escaping the check;
     (ii) for each of the **four decoded `SpectralState`s**: `ratios[i]` and `amplitudes[i]` for
     `i < numPartials` only (entries at `i >= numPartials` are scratch space the morph engine owns,
     `spectral_state.h:78-79`), plus `tiltDbPerOct` and `inharmonicity`;
     (iii) the 64 `[partials]` pans.
     **Explicitly excluded because they are not floats:** `numPartials`, the payload version byte, the
     16-byte `name`, the leading state-version `int32`, the two `uint64` bitmasks.
     This duplicates part of `isValidSpectralState`'s coverage **deliberately** — that predicate is
     composite (ranges + monotonicity), so a failure there does not name the finiteness clause. Do not
     collapse the two.

**Implement:** nothing but the tests.

**Verify:** builds with 0 warnings; cases 2–4 pass over the pilot; case 1 fails naming the missing coverage
values (expected until T014).

---

## GROUP 12 — Independent infrastructure (parallel-safe)

### T012 [P] — `tools/check-preset-generator-determinism.js` (FR-035a / SC-016)

**Files:** create `tools/check-preset-generator-determinism.js`. **No other task touches this file.**

**Implement** — Node, no dependencies (project rule: Node, never Python), CLI
`node tools/check-preset-generator-determinism.js [--bin <path>]`. Default binary resolution, in order:
`build/windows-x64-release/bin/Release/seraphis_preset_generator.exe`,
`build/bin/seraphis_preset_generator`, `build/bin/seraphis_preset_generator.exe`.

1. `fs.mkdtempSync` two fresh dirs `A`, `B`; run the binary into each; walk both trees; assert **identical
   relative-path sets** and `Buffer.compare` **equal for every file**. (Byte comparison is legitimate here
   and **only** here: one binary, one machine, one run set — spec C-7. It is not float-render data.)
2. Snapshot `A`'s bytes; run the binary a **third** time with `A` as the output directory; assert **0**
   files changed byte-wise. (Idempotence — the half that previously had no measurement at all.)
3. Exit **0** with a one-line summary; exit **1** printing the first differing path. Temp dirs removed on
   success, **kept** on failure with the path printed so the diff is inspectable.

**Verify:** run it against the pilot tree — exit 0. Append the command and its output to `compliance.md`
§ *SC-016 determinism*. Re-run in T028 after the full 42 land.

### T013 [P] — Additive thread filter in `tests/test_helpers/allocation_detector.h` (FR-028a)

**Files:** edit `tests/test_helpers/allocation_detector.h` (139 lines; class `:26-68`, `recordAllocation`
`:53-57`, `AllocationScope` `:75-95`, `namespace TestHelpers` `:19`). **No other task touches this file.**

**Decision, already made — do NOT route FR-028a to the valgrind lane instead:** (a) the valgrind-linux lane
runs *nightly*, so a regression would be invisible to the PR that caused it while SC-015a is a phase
criterion; (b) valgrind reports leaks/races, not "this thread allocated"; (c) the filter is ~6 lines,
default-off, and provably cannot change the six existing consumers' behaviour.

**Failing test FIRST** — add
`TEST_CASE("TestHelpers_AllocationDetector_ThreadFilterIsOptIn", "[test_helpers]")` to the shared-test suite
that already exercises `allocation_detector.h` (find it with
`grep -rn "AllocationScope" plugins/shared/tests tests/`; if no such case exists, put it in
`plugins/shared/tests/` and register it in that suite's enumerated list). Assert:
- with the filter **off** (default), an allocation on any thread is counted — a plain `AllocationScope`
  around a `new`/`delete` still reports `> 0`, i.e. **existing behaviour is unchanged**;
- with a `ThreadScopedAllocationScope` alive, an allocation on a *different* thread reports **0**, and an
  allocation on the opted-in thread reports `> 0`.

**Implement:**

```cpp
/// Per-thread opt-in. Allocation-free on first touch because this header is only ever linked into test
/// EXECUTABLES (local-exec / initial-exec TLS -> static TLS area, allocated at thread creation).
/// DO NOT use from a dynamically loaded module (.so/.dll loaded at run time): general-dynamic TLS
/// allocates on first touch via __tls_get_addr, and allocation_operator_overrides.h:66-94 calls
/// recordAllocation() from operator new itself - that would be re-entrancy.
inline thread_local bool tAllocationTrackThisThread = false;
```

plus, on `AllocationDetector`: `void setThreadFilterEnabled(bool) noexcept`,
`[[nodiscard]] bool threadFilterEnabled() const noexcept`, a private
`std::atomic<bool> threadFilter_{false};` (**DEFAULT OFF — every existing usage is unchanged**), and one
added line in `recordAllocation()`:

```cpp
if (threadFilter_.load(std::memory_order_acquire) && !tAllocationTrackThisThread) return;
```

plus `class ThreadScopedAllocationScope` — RAII: enable the filter, start tracking, restore **both** on
scope exit, exposing `getAllocationCount()` the same way `AllocationScope` does (`:85-91`).

**Verify — the additive change must be proven inert:** build and run **every** plugin suite plus
`shared_tests` and confirm each summary is unchanged from its pre-edit run:
`iterum plugin_tests`, `approval_tests`, `disrumpo_tests`, `ruinae_tests`, `innexus_tests`, `gradus_tests`,
`membrum_tests`, `seraphis_tests`, `shared_tests`. 0 warnings. Capture each run's `tail -5` to a log file on
the **first** run — never re-run a suite just to look at its output.

---

## GROUP 13 — Author the library

### T014 — The 42 presets

**Files:** edit `tools/seraphis_preset_defs.h` (replace the 3-entry pilot with all 42); delete
`plugins/seraphis/resources/presets/*/.gitkeep` (all seven, including `Textures/.gitkeep`) as the real
presets land; the generated `.vstpreset` files are written by `generate_seraphis_presets`, **never by
hand**.

**The tests are already written and red** — T008 (count 42, names), T010 case 2 (browser scan 42), T011
case 1 (coverage matrix). This task turns them green. Do not weaken any of them.

**Names — 7 × 6, ASCII, unique library-wide:**

| Textures | Pads | Drones | Bells | Choirs | Motion | Cinematic |
|---|---|---|---|---|---|---|
| Vellum | First Light | Deep Well | Frost Bell | Vowel Field | Orbit Study | Approach Vector |
| Sea Glass | Long Exhale | Stone Circle | Temple Rim | Breath Chorus | Tide Pool | Event Horizon |
| Slow Snow | Cathedral Moss | Tectonic | Glass Carillon | Ghost Choir | Wander Lamp | Signal Lost |
| Paper Sky | Warm Static | Iron Lung | Struck Ice | Aeolian Voices | Restless | Rising Dread |
| Rust Bloom | Distant Choir | Continuum | Bronze Halo | Whispered Mass | Spiral Arms | Vast |
| Quiet Machine | Blue Hour | Undertow | Bell Garden | Angelic Drift | Slow Weather | Aftermath |

**Coverage assignment — pinned by name so authoring is not a search (the harness still computes the ledger
from the DECODED states, never from this table):**

| Enumeration value | Assigned preset(s) |
|---|---|
| Body material Glass / Strings / Metal Plate / Chamber / Ice (800) | Sea Glass / Continuum / Bronze Halo / Cathedral Moss / Struck Ice |
| Spectral state Sine Stack / Bell / Choir / Glass / Breath (409–412) | First Light / Frost Bell / Vowel Field / Glass Carillon / Breath Chorus |
| Morph state count 2 / 3 / 4 (408) | Vellum / Tide Pool / Slow Weather |
| Travel mode External / Spline (403) | Orbit Study / Wander Lamp |
| Envelope mode Standard / Growth (700) | most presets / Approach Vector, Rising Dread, First Light |
| Grain envelope ≥ 3 of 6 (1016) | Hann (default, many) · Trapezoid → Quiet Machine · Blackman → Slow Snow · Exponential → Aftermath |
| `kAetherFreezeId` (1204) ON | Event Horizon, Vast (OFF everywhere else) |
| `kAtmosFreezeId` (1008) ON | Ghost Choir (OFF everywhere else) |
| `kFxSpectralFreezeId` (1430) ON | Signal Lost (OFF everywhere else) |
| `kFxDelaySyncId` (1418) ON | Restless (OFF everywhere else) |
| `kBodyResonatorBypassId` (812) ON | Warm Static (OFF everywhere else) |
| `kBodyInputAgcId` (811) OFF | Iron Lung (ON everywhere else) |

**Authoring constraints that are easy to violate — each has a gate, and a breach is re-authored:**

1. **FR-008 / C-5.** The denormalizer is `clamp((int)(value*15 + 1 + 0.5), 1, 16)`
   (`global_params.h:97-102`), so polyphony *p* ⇒ `normalized = (p − 1) / 15`; **8 voices ⇒
   `7/15 = 0.4666666666666667`**, which is also the registered default (`:141-148`, index 7).
   `kSoftLimitId`'s registered default is 1.0 (`:152-154`), so **not touching ID 2** satisfies FR-008's
   second half. Gate: `Seraphis_FactoryPresets_RespectVoiceBudget`.
2. **FR-008a is a live trap for Growth presets.** In Growth mode `A = growthDurationSec + stage1Ms/1000`,
   and the *defaults* are 10.0 s + 4000 ms = **14 s — over the 12 s ceiling**
   (`life_mod_params.h:65-67`). Every Growth-mode preset **must** set 701 and/or 703 explicitly (e.g.
   growth 8 s + stage1 3 s = 11 s). Standard-mode defaults are 2 + 4 = 6 s and are safe; `Rel` default is
   8 s, inside the 10 s ceiling. Gate: `Seraphis_FactoryPresets_RespectTimingCeiling`.
3. **Normalized values for log-mapped IDs are computed, never guessed:**
   `normalized = ln(v / min) / ln(max / min)` — the exact inverse of
   `logMapFromNormalized = clamp(mn * pow(mx/mn, u), mn, mx)`
   (`plugins/shared/src/ui/parameter_helpers.h:80-83`). Ranges: growth duration `[1, 60]` s
   (`growth_envelope.h:96`, `:98`); stage/release times `[1, 10000]` ms (`life_mod_params.h:56-58`); Aether
   decay `[0.5, 60]` s (`aether_params.h:45-46`). Authoring is nevertheless **verified against the
   decoder**, not against this formula.
4. **Seed spread.** `kSeedId` is an *index* into `kSeedValues` with index 0 pinned to `1u`
   (`dropdown_mappings.h:93-99`, `global_params.h:53-59`). Presets that intend audibly different motion must
   not all sit on index 0 — this is also the cheapest lever if T019's distinctness floor is tight for a
   near-identical pair.
5. **Tail-arm classification is chosen, not discovered.** A preset's tail arm follows only from its three
   freeze toggles, so the author decides which arm it will be measured against by setting 1008 / 1204 / 1430.
6. **No preset carries an `EditMessage` list or drives `[partials]`** (FR-016 / OQ-4 = NO).
7. `stimulus` is left **absent** unless a specific preset genuinely needs an outlier pitch/velocity; when
   present it is `{pitch, velocity}` and T015's sweep must use it.

**Workflow — per category, not per library:**
1. Write the six definition entries.
2. `"$CMAKE" --build build/windows-x64-release --config Release --target generate_seraphis_presets`.
3. `seraphis_tests.exe "Seraphis_FactoryPresets_*" 2>&1 | tail -20` — the static gates run in seconds.
4. Fix authoring, not tests. Move to the next category.

**Verify:** all of `Seraphis_FactoryPresets_*` green — count **42**, ≥ 5 per category, 42/42 container /
version / 2868 bytes / round-trip / metadata / names, browser scan 42 with 0 empty subcategories and 0
non-factory, coverage matrix **0 unmet rows**, polyphony ≤ 8 and softLimit ON 42/42, `A ≤ 12` and
`Rel ≤ 10` 42/42, partials block inert 42/42. 0 warnings. Re-run T012's determinism script — still exit 0
over 42 files.

---

## GROUP 14 — The render sweep: core arms

### T015 — Create the sweep TU, register it, and ship SC-009 / SC-010 / SC-010a

**Files:** create `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp`; edit
`plugins/seraphis/tests/CMakeLists.txt` — append it to the enumerated list (next to
`unit/preset/factory_preset_test.cpp` under the Phase 12 comment) **and** to the
`-fno-fast-math -fno-finite-math-only` block (`:117-170`) with the reason: *bit-pattern non-finite checks and
per-second RMS statistics.*

**Structure — one shared, file-local render helper produces each preset's buffers once per (preset, rate)
and the arms read them. The sweep must not render the same timeline twice.**

**Stimulus resolution, before any render:** for each discovered file, call
`Seraphis::PresetDefs::findDef(category, stem)` and **`REQUIRE(def != nullptr)` for 42/42**, printing the
unmatched `(category, stem)` on failure. A null result is **never** downgraded to "use pitch 60 /
velocity 0.8" — that would let an authored outlier stimulus go untested while every arm passed. Default
stimulus is **MIDI pitch 60, velocity 0.8f**; the definition's `stimulus` override replaces it when present.

**Timeline:** `SeraphisTest::makeTimeline(decodePresetState(comp))` (T007). Block size is **fixed at 512**
for every sweep render; the block-size-invariance claim belongs to
`plugins/seraphis/tests/integration/processor_audio_test.cpp:91-96` and is **not** re-litigated here. Render
length is `ceil(Total * sr / 512)` blocks; samples past `Total` are rendered but not measured.

**Event path:** `fx.pushEvent(kNoteOnEvent, pitch, velocity, 0)` at t = 0
(`seraphis_test_fixture.h:275`), then **no NoteOff until `t = H`** — the hold `[0, H]` *is* the roadmap's
NoteOn-only stuck-note condition (line 608). The NoteOff is pushed from `renderBlocks`'s per-block script
(`:389`) at the block containing `n(H)`.

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_PresetSweep_NoSilence", "[seraphis][preset][sweep]")` — FR-024 / SC-009.
   `rms = rmsOver(L, R, n(A + 1.0), n(A + 4.0))`; `REQUIRE(rms > 1e-3)` (= −60 dBFS) for **100 %** of
   presets, at **both** 44 100 and 48 000 Hz (at 48 kHz the render stops at `H + 5 s`, C-9). The `+1.0 s`
   offset exists because a Growth-mode preset is legitimately near-silent for seconds
   (`life_mod_params.h:65`); a window opening at t = 0 would fail correct presets. Failure message names the
   preset, the decoded `A`, and the measured dBFS.
2. `TEST_CASE("Seraphis_PresetSweep_BoundedAndFinite", "[seraphis][preset][sweep]")` — FR-025 / SC-010.
   ```cpp
   constexpr float kLimiterCeilingLin  = 0.8912509f;   // effects_chain_test.cpp:332
   constexpr float kCeilingAllowanceDb = 0.1f;         // effects_chain_test.cpp:854
   const float kPeakBound = kLimiterCeilingLin * std::pow(10.0f, kCeilingAllowanceDb / 20.0f); // ~0.9016
   ```
   Over the **whole** `[0, Total]` render — the NoteOn-only hold included — both channels: **0** non-finite
   samples (bit pattern, never `std::isnan`) and `|s| <= kPeakBound`. Both accumulate into plain counters and
   are asserted **once after the loop** — a `REQUIRE` per sample makes the sweep unusably slow and floods
   Catch2's reporting. Also `REQUIRE(fx.checkCanaries());` (`seraphis_test_fixture.h:371`) so an
   out-of-bounds write is caught in the same pass. Both rates.
3. `TEST_CASE("Seraphis_PresetSweep_ChordBoundedAndFinite", "[seraphis][preset][sweep]")` — FR-024a /
   SC-010a. **44 100 Hz only.** Four simultaneous NoteOns at t = 0: root = the resolved stimulus pitch, plus
   `+4`, `+7`, `+12`, all at the stimulus velocity. Render to at least `H`; **no NoteOff required**. Assert
   **only** the bounded arm (same `kPeakBound`, same bit-pattern finiteness) — **not** SC-009's sustain floor
   and **not** any tail arm. This is the only arm in the phase that renders more than one voice, and it is
   what exercises `polyphony ≤ 8` and the multi-voice limiter sum in actual audio.

**Verify:** builds with 0 warnings; all three pass over 42 presets. Record the TU's Catch2-reported duration
(it feeds SC-027 in T016).

---

## GROUP 15 — Cost measurement and the lever decision

### T016 — Measure the real RTF, project, apply levers in order (SC-027, plan §4.2.3 / R-6)

**Files:** `specs/seraphis-phase12-presets-release/compliance.md` (§ *SC-027*); edit
`plugins/seraphis/tests/integration/preset_render_sweep_test.cpp` **only if a lever applies**.

**This task runs BEFORE the remaining arms are written, not after** — discovering a 1.7× overrun at the
release gate is the failure mode it exists to prevent. The earlier "RTF ≈ 0.05 ⇒ ≈ 4 minutes" assumption is
**withdrawn**: most of Seraphis's per-block cost is per-**block**, not per-voice (Aether's 1024-sample
diffusion FFT, Atmosphere, spectral freeze, saturation, the true-peak limiter), and the only in-repo
measurement (T001 step 3) says RTF ≈ **0.13**, which projects ≈ 10 minutes against a 360 s budget.

**Do:**
1. Render **one no-freeze preset and one Aether-freeze preset** end to end **through the real sweep path**
   and record **seconds of audio rendered per second of wall clock** on `windows-x64-release`, with the
   command and the raw timings.
2. Multiply against the rendered-audio volume: under FR-008a's ceiling the worst `Total` is **89 s**
   (Aether-freeze), **49 s** (Atmos/FX-freeze), **37.5 s** (no freeze); plus the 48 kHz render (≤ 22 s), the
   chord render (≤ 17 s) and the Windows-only 2 × `[0, H]` reproducibility renders (≤ 34 s) — a *typical*
   preset near 110 s, **≈ 4 600 s across 42**. Note that `ProcessorFixture::prepare()` leaves
   `setEffectsStageInstrumentedForTest(true)` on in the *sweep* (it is a test — this is correct there, and
   the fixture's banner at `:198-209` says the test build pays **more** than the shipping build), a term the
   volume arithmetic does not charge for and which is **not** a lever.
3. If the projection exceeds **360 s**, apply the pre-declared levers **in this order**, re-measuring after
   each, and record which were used:

   | # | Lever | What it does NOT do |
   |---|---|---|
   | L-1 | Shorten `W` for the Atmos/FX-freeze case from 20 s | Does not touch the 60 s Aether band |
   | L-2 | Compute per-second RMS **streaming** (accumulate per block, never retain the full render) | Changes no threshold and no window |
   | L-3 | Tail arms at 44 100 Hz only; drop the 48 kHz *tail* render, keep its sustain + bounded arms | FR-024/FR-025's dual-rate coverage is retained |
   | L-4 | Restrict the 48 kHz render to a subset — every sole holder of a C-2 coverage row, plus ≥ 1 per category | Every coverage row is still rendered at both rates |
   | L-5 | Move the 60 s Aether band and the reproducibility renders to a `[.slow]`-tagged nightly case | Requires recording that SC-012/SC-026 are nightly-gated, not PR-gated |

   **Never sanctioned:** dropping presets, dropping the chord arm, dropping either sample rate entirely, or
   relaxing any threshold.

**Verify:** `compliance.md` § *SC-027* carries the **measured** RTF with its provenance, the projection, the
levers applied (or "none"), and the re-measured figure. SC-027's measured number governs; nothing in this
task's arithmetic is evidence on its own.

---

## GROUP 16 — The tail arms

### T017 — SC-011 and SC-012, classified from the freeze toggles only

**Files:** edit `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp`.

Per-second series: partition `[Settle, Settle + W)` into `floor(W)` one-second windows, one `rmsOver` each.
Measurement begins at `Settle`, **never at the NoteOff** — the settling allowance is part of the criterion.
Classification is from the **three decoded freeze toggles and nothing else** (a finite RT60 is a decay
*rate*, not a sustain).

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_PresetSweep_DecayMatchesRt60", "[seraphis][preset][sweep]")` — FR-026 case 3 /
   SC-011. **44 100 Hz only**, presets with all three freeze toggles OFF, `W = 10`:
   ```
   dropDb = 20 * log10( rms(first second) / max(rms(final second), 1e-12) )
   REQUIRE(dropDb >= std::min(0.5 * 60.0 * W / RT60, 20.0));
   ```
   Worked thresholds: RT60 ≤ 6 s ⇒ **≥ 20 dB** (the original bound, unchanged); RT60 = 30 s ⇒ ≥ 10 dB;
   RT60 = 60 s ⇒ ≥ 5 dB. Threshold: **0** presets below their own bound.
   **This arm carries NO digital-silence guard, deliberately.** The bound is one-sided — decaying *faster*
   than the reverb predicts always passes, **including all the way to digital silence**, where the `1e-12`
   denominator floor is the whole mechanism. `rms(first second)` is **not** floored: a first window at
   1e-12 means the preset was already silent at `Settle`, which this arm passes and which SC-009 has already
   rejected if the preset is silent during `[A+1, A+4]`. A guard here would fail correct dry-dominant
   presets (the legal RT60 range starts at 0.5 s) and contradict the formula in the same case.
2. `TEST_CASE("Seraphis_PresetSweep_FrozenPresetsHold", "[seraphis][preset][sweep]")` — FR-026 cases 1–2 /
   SC-012. Both arms at 44 100 Hz starting at `Settle = H + Rel + 2.0`.
   **Digital-silence guard applies to THESE two arms only:** if any window's RMS is `< 1e-9`, fail with an
   explicit *"frozen preset produced digital silence"* message rather than dividing by zero — a silent
   window under an engaged freeze means the freeze is broken, which is precisely what these arms catch.
   - **Arm 1 — `kAetherFreezeId` ON**, `W = 60`:
     `bandDb = 20*log10(maxRms / minRms); REQUIRE(bandDb <= 2.0);` (a ±1.0 dB band).
     **The 60 s observation duration is roadmap line 282's and is NOT reduced** — a 20 s window cannot
     distinguish a 60 s-RT60 tail from a conserving one. The band widening 0.5 → 1.0 dB is the phase's
     **one disclosed relaxation** (Phase 12 measures the whole plugin — voices + effects + limiter — not
     `AetherReverb` alone) and MUST be repeated in the failure message so it can never be mistaken for the
     `AetherReverb`-alone criterion.
     Wrapped in `#if defined(_WIN32)` / `#else SUCCEED("SC-012 Aether-freeze arm is Windows-leg only (spec
     C-9); Atmos/FX arm ran above"); #endif` — the guard is **inside** the `TEST_CASE`, never around it, so
     the Catch2 `test cases:` count is identical on all three legs and SC-021's delta stays meaningful.
   - **Arm 2 — `kAtmosFreezeId` and/or `kFxSpectralFreezeId` ON with `kAetherFreezeId` OFF**, `W = 20`,
     **all three legs**. The spec's literal wording (*final ≤ loudest + 1.0 dB*) is **tautological** —
     `loudest ≥ final` by construction — so it is repaired **without touching the threshold number**
     (OI-2):
     ```
     (a) NORMATIVE:  20*log10(rFinal / r0) <= 1.0      // r0 = first window: non-growing, end to end
     (b) REPORTED:   20*log10(rMax  / r0) <= 1.0      // no intermediate growth either
     ```
     Clause (a) ships gating now; clause (b) is computed and printed for every qualifying preset in the same
     run and is **promoted to a `REQUIRE`** once its measured margin is recorded in `compliance.md`. Nothing
     is relaxed — an arm that could not fail is replaced by one that can.
     No conservation band is asserted here: neither mechanism is promised energy conservation anywhere in
     the roadmap (Phase 5, lines 248-254; Phase 10, lines 464-473), and with `kAetherDecayDefault = 4.0 s`
     the non-frozen components are still falling while the frozen layer holds.

**Verify:** both cases green over 42 presets on Windows; **0** presets outside their band. Record clause
(b)'s measured margins in `compliance.md`, then promote it to gating in the same task if the margin holds.

---

## GROUP 17 — The allocation arms

### T018 — SC-015 (sequential, quiescent) and SC-015a (concurrent, thread-scoped)

**Files:** edit `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp`. Depends on T013's thread
filter.

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_PresetSweep_NoAudioThreadAllocation", "[seraphis][preset][sweep]")` — FR-028 /
   SC-015. **Three preconditions, all equally load-bearing.** `ProcessorFixture::renderBlocks` **appends**
   (`seraphis_test_fixture.h:403-406`) and re-requests capacity as
   `reserveCapture(capturedL.size() + numBlocks * blockSize)` (`:391`) — it **never clears**. Without the
   clear, every measured scope reallocates and the arm reports a **false** audio-thread allocation:
   ```
   fx.prepare(44100.0, 512);
   fx.reserveCapture(N * 512);        // ONE render's worth - the clear keeps this capacity
   fx.renderBlocks(N, 512);           // warm-up: grows both vectors AND warms every internal container
   for (each of the 42 comp chunks) {
       fx.proc->setState(...);        // OUTSIDE the scope, strictly BETWEEN process() calls
       fx.capturedL.clear(); fx.capturedR.clear();   // size -> 0, CAPACITY RETAINED
       { TestHelpers::AllocationScope scope; fx.renderBlocks(N, 512); }
       REQUIRE(scope.getAllocationCount() == 0);     // AFTER the close - Catch2 itself allocates
   }
   ```
   Threshold: **0** allocations, 42/42. State plainly in the case banner that this is the **quiescent-load**
   claim and nothing more — `AllocationScope` wraps a process-global counter with no thread filter
   (`allocation_detector.h:60-67`, `:75-95`) and cannot attribute an allocation to a thread.
2. `TEST_CASE("Seraphis_PresetSweep_ConcurrentLoadIsRtSafe", "[seraphis][preset][sweep]")` — FR-028a /
   SC-015a. A message thread calls `setState` in a loop over all 42 presets **while** the audio thread
   renders continuously. **The audio thread must NOT call `renderBlocks`** — a continuous capture-ful loop
   appends forever and reallocates *inside the very scope that must report zero*, with unbounded memory:
   ```
   // --- audio thread, in this order, BEFORE the scope opens ---
   TestHelpers::tAllocationTrackThisThread = true;   // first touch of the TLS flag (T013)
   enableFTZDAZ();                                   // MXCSR is FRESH per thread on Windows
   fx.processBlock(512);                             // one capture-free warm-up block (:343-352 touches
                                                     // neither capturedL nor capturedR)
   {
       TestHelpers::ThreadScopedAllocationScope scope;
       for (std::size_t b = 0; b < kConcurrentBlocks && !stop.load(); ++b) {
           fx.processBlock(512);
           // finiteness + peak scanned PER BLOCK over fx.audioL()/fx.audioR() (:357-358),
           // accumulated into plain counters. Nothing grows.
       }
   }
   REQUIRE(scope.getAllocationCount() == 0);         // after the close
   ```
   `kConcurrentBlocks = 8600` (≈ 100 s at 44 100 Hz) — bounded and stated; the loop also exits early on
   `stop` once the message thread has finished all 42 loads. **No Catch2 macro executes inside the scope, on
   either thread.**
   Assert: (a) **0** non-finite samples and peak ≤ `kPeakBound` throughout; (b) **42/42** `setState` return
   `kResultOk` and the post-join `getState()` `memcmp`-equals **one of** the 42 chunks; (c) the
   thread-scoped count is **0**.
   Run with the shipped push-enabled behaviour — `setState()` publishes via `requestPushAllSurfaces()`
   unless `gDisablePresetLoadPush` is set (`processor.cpp:1816-1818`); do not disable it.
   **Asserting (c) with the unfiltered global `AllocationScope` is FORBIDDEN** — it would count the message
   thread's own `setState` allocations as audio-thread violations and fail regardless of RT safety.

**Verify:** both cases green; 0 warnings. A green SC-015 does **not** imply SC-015a — they reach different
preconditions with different instruments; both must be run.

---

## GROUP 18 — Reproducibility and distinctness

### T019 — SC-026, SC-028, and the injected near-duplicate control

**Files:** edit `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp`.

**Failing tests FIRST:**

1. `TEST_CASE("Seraphis_PresetSweep_RendersAreReproducible", "[seraphis][preset][sweep]")` — FR-027a /
   SC-026. Two `ProcessorFixture`s, each freshly `prepare(44100.0, 512)`d, each loaded with the **same**
   `comp` chunk via `setState`, each driven through the **identical** event schedule, both rendered over
   `[0, H]`. Fingerprint the concatenation `L ++ R` with
   `Krate::DSP::TestUtils::fingerprintRender(std::span<const float>)`
   (`tests/test_helpers/render_fingerprint.h:63`) and
   `REQUIRE(compareFingerprints(a, b).withinTolerance());` — worst aggregate-metric relative error
   ≤ `kMetricTolerance = 1e-5` (`:52`), worst checkpoint error ≤ `kSampleTolerance = 1e-4f` (`:49`),
   `withinTolerance()` at `:95-97`. **100 %** of presets. **Windows leg only**, via the same in-case
   `#if defined(_WIN32)` / `#else SUCCEED(...)` guard shape T017 uses (never a guard around the
   `TEST_CASE`). **No float bit digest and no integer digest derived from float bits.**
2. `TEST_CASE("Seraphis_PresetSweep_PresetsAreDistinct", "[seraphis][preset][sweep]")` — FR-027b / SC-028,
   over all **861** unordered pairs (`C(42,2)`), computed from the **42 sustain-window buffers the SC-009
   render already produced** — do not re-render.
   **The metric is LEVEL-NORMALISED (OI-4), because the obvious one measures the wrong thing.** `rms`,
   `peak` and `meanAbs` of a raw fingerprint are all pure amplitude aggregates
   (`render_fingerprint.h:63-88`), so two *timbrally identical* presets differing only in master gain would
   score large and pass — exactly the failure C-10 exists to catch. So: take each preset's sustain-window
   `L ++ R` buffer over `[A+1, A+4]` at 44 100 Hz, compute its RMS, **scale the whole buffer by
   `1 / max(rms, 1e-12)` before fingerprinting**, then
   ```
   rms is identically 1 after normalisation  ->  EXCLUDED (carries no information)
   d(P,Q) = max over m in {peak, meanAbs, totalVariation} of
                |m_P - m_Q| / max(|m_P|, |m_Q|, 1e-12)
   ```
   Each surviving term is a **shape** statistic of a unit-level signal: `peak` **is the crest factor**,
   `meanAbs` is the inverse form factor, `totalVariation` per unit RMS is a brightness proxy.
   **Checkpoints stay excluded from the gate** — at a 3 s sustain window the 32 evenly spaced *raw* samples
   (`:83-86`) are dominated by instantaneous phase, so two near-identical presets differing by a few cents
   of drift would score near-maximum distance and, inside a `max`, make the arm pass unconditionally. They
   remain in the **reported** output for diagnosis.
   Floor: `kDistinctnessFloorAbsolute = 0.02` (2 % relative on the strictest shape aggregate);
   `kDistinctnessFloor = max(0.02, 0.5 * observedMinimum)`. `REQUIRE(d(P,Q) > kDistinctnessFloor)` for all
   861 pairs; **0** pairs below. Failure prints the closest pair and its distance. If `observedMinimum`
   lands below 0.02 the two presets involved are **re-authored** (T014 §4's seed spread is the cheapest
   lever), **never accommodated**.
3. `TEST_CASE("Seraphis_PresetSweep_DistinctnessNegativeControl", "[.measure][seraphis][preset]")` — the
   floor is validated **from both sides**, not only against the observed minimum:
   - clone a shipped preset's definition and change **only** `kMasterGainId` (ID 0, normalized → linear
     `2·u`, `global_params.h:91-96`) to the value 3 dB below it (`u' = u / sqrt(2)`);
   - render the twin through the identical sweep path and **pre-check that the render is a pure scaling**:
     the twin's raw (pre-normalisation) `peak` must equal `0.7079 ×` the original's within **1 %**. Master
     gain is applied **pre-limiter** (`processor.cpp:2392-2404`, *"a post-limiter multiply is FORBIDDEN"* at
     `:2397`), so if the limiter was engaged at the original level the twin is **not** a pure scaling and the
     control is invalid — in that case **pick a preset whose peak has headroom; do not weaken the check**;
   - `REQUIRE(d(original, twin) < kDistinctnessFloor);` — a level-only twin MUST be reported as
     non-distinct.

**Measure-then-pin:** record `observedMinimum` over all 861 pairs, the twin's measured `d`, and the pinned
floor in `compliance.md` § *FR-027b distinctness floor & negative control*, so the floor demonstrably sits
between a known non-distinct pair and the closest real pair.

**Verify:** cases 1 and 2 green; case 3 (tag-excluded from the default run) green when invoked explicitly.
0 warnings.

---

## GROUP 19 — FR-029a measurement

### T020 — The tolerance probe (report mode), measured on GCC and on the real CI legs

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`;
`compliance.md` § *FR-029a measured tolerances*.

**No tolerance may be written down before this task produces a number.**

**Implement** the in-process regeneration path and the probe:
`TEST_CASE("Seraphis_FactoryPresets_TreeToleranceProbe", "[.measure][seraphis][preset]")` — the leading `.`
excludes it from the default run.

Regeneration uses the **identical five-line drive sequence T005 specified, `setEffectsStageInstrumentedForTest(false)`
included** (OI-5 / R-2) — otherwise the two sides of the comparison differ in configuration and FR-029
compares two different things and passes. It reads the **same** `tools/seraphis_preset_defs.h` table, so no
shelling out to the generator binary is needed on any leg.

For every committed preset, compute and `WARN` the **worst relative error per class**, separately:
- **scalar class** — every `float` field of the nine decoded packs (Aether decay is the canonical
  single-`std::pow` case: `aether_params.h:111-116` through
  `logMapFromNormalized = clamp(mn * pow(mx/mn, u), mn, mx)`, `parameter_helpers.h:80-83`);
- **payload class** — for each of the four `SpectralState`s: `ratios[i]`, `amplitudes[i]` (`i < numPartials`),
  `tiltDbPerOct`, `inharmonicity`. These are `makeFactoryState` outputs (~200 `std::pow`/`std::exp` calls
  plus a `1.0f/std::sqrt(sumSquares)` normalisation, `spectral_state.h:164-170`) memcpy'd as raw bit
  patterns (`:238-260`).

**Run it three ways and record all three:** (1) locally on Windows/MSVC; (2) under **WSL/GCC 13** (the
standing probe route); (3) **one CI dry run on the real macOS and Linux legs**, so Apple Clang `-ffast-math`
is in the sample.

**Then pin**, in `compliance.md` and as two separate constants for T021:
`kScalarFieldTolerance = 10 × worst-observed-scalar`, `kPayloadFieldTolerance = 10 × worst-observed-payload`.
They **MUST NOT** be the same number by accident — if the measurement says they coincide, say so explicitly.
Sanity band (**not** a substitute for measurement): a single `std::pow` differs by ≲ 2 ULP across
UCRT/glibc/libm ⇒ scalar tolerance near **1e-6**; the payloads compound ⇒ payload tolerance near **1e-4**.
A measurement orders of magnitude outside that band means **investigate**, never widen.

**Verify:** the probe runs clean on all three toolchains; `compliance.md` carries both worst-observed errors,
both pinned tolerances, and each measurement's provenance.

---

## GROUP 20 — FR-029 gating

### T021 — `Seraphis_FactoryPresets_TreeMatchesGenerator`, all three legs

**Files:** edit `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`.

**Failing test FIRST** —
`TEST_CASE("Seraphis_FactoryPresets_TreeMatchesGenerator", "[seraphis][preset]")`, the five-clause
**semantic** comparison. **No byte-level comparison of `Comp` chunks is performed**: the committed tree is
Windows/MSVC-generated while the release artifact is `ubuntu-latest`/GCC-generated
(`release.yml:100-102`, `:158-174`, `:201-224`), ~14 fields go through `std::pow`, and four payloads are
`memcpy`'d transcendental bit patterns. A `memcmp` here would be a bit-exact float golden across toolchains
— the thing `ci.yml:162-166` lints for. **State that reasoning in the test's banner at the comparison
site** (plan R-1).

1. **File set** — the set of `(category, stem)` pairs on disk equals the set `allPresets()` produces, **in
   both directions** (no extra, no missing).
2. **`Info` XML** — `std::string` equality against `PresetDefs::buildSeraphisInfoXml(stem, category)`
   (pure ASCII, toolchain-invariant). **0** byte differences.
3. **Version + length** — `version == 3` and `comp.size() == 2868`.
4. **Integer / enum / bool fields** — **exactly equal**, via `DecodedPresetState` on both sides.
5. **Float fields, two tolerance classes, never one number** — the constants T020 pinned:
   scalar fields within `kScalarFieldTolerance`; for each of the four `SpectralState`s, `numPartials`
   **exact**, `isValidSpectralState` (`dsp/include/krate/dsp/processors/spectral_state.h:82`) **true on both
   sides**, and `ratios[i]` / `amplitudes[i]` (`i < numPartials`) / `tiltDbPerOct` / `inharmonicity` within
   `kPayloadFieldTolerance`.

**Field-coverage tripwire (mandatory):** each pack's comparator increments a counter and the test asserts
the counter equals that pack's documented field count — `[global]` 3, `[macro]` 5, `[seed]` 1, `[cloud]` 11,
`[morph]` 9 scalars, `[life]` 10, `[body]` 13, `[atmos]` 17, `[aether]` 18, `[effects]` 16 (each = block
bytes / 4). Combined with T007's cumulative offset tripwires, a field added to a pack in a later phase fails
**both** checks rather than silently escaping comparison. Reuse the **same** counters T011 case 4 defines —
the two must not drift apart.

**Verify:** green on Windows; green under WSL/GCC; green on the macOS and Linux CI legs (SC-017 is stated
per-leg — each toolchain proves *its own* generator output is the committed library). 0 warnings.

---

## GROUP 21 — Roster, version, documentation, install verification (parallel-safe)

Each task below touches files **no other task in this list touches**; `compliance.md` sections are
append-only and disjoint.

### T022 [P] — Release-roster registration (FR-031, FR-032, SC-024)

**Files:** `.claude/workflows/release-readiness.js`; `.claude/skills/release/SKILL.md`.

`PLUGIN_MAP` currently ends at `membrum` (`release-readiness.js:14-21`) and unknown names are filtered out
at `:26-29`, so a `{plugins:["seraphis"]}` run today silently checks **nothing**. Add:

```js
  seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' },
```

`SKILL.md` gets two edits: `seraphis` appended to the **Plugin** input list (`:15`, today ends at `membrum`)
and the row `| seraphis | \`seraphis_tests\` | \`Seraphis.vst3\` |` added to the **Plugin → target/bundle
map** table (`:22-29`).

**Verify (SC-024):** read both files back after the edit **and run the flow once** with
`{plugins:["seraphis"]}` — it must resolve a test target and a bundle rather than filter the name out.
Record in `compliance.md` § *SC-024*.

### T023 [P] — Version bump and changelog (FR-033, SC-023)

**Files:** `plugins/seraphis/version.json`; `plugins/seraphis/CHANGELOG.md`.

`"version": "0.4.0"` → `"0.5.0"` — **only** the `version` field. `src/version.h` is generated by
`krate_plugin_configure_generated_files()` (`plugins/seraphis/CMakeLists.txt:11`) and is **never**
hand-edited. Add the matching `## [0.5.0] - <date>` section to `CHANGELOG.md` **in the same change** (the
version bump and its changelog entry are one change, never two). **`1.0.0` is reserved for the release that
includes Phase 13 (per-note expression) — this phase does not claim it.**

**Verify:** `node tools/check-changelog-coverage.js seraphis` exits **0** (`'seraphis'` is already in that
script's `PLUGINS` array at `:50` — this is a green run, not a code change). Record in `compliance.md`
§ *SC-023*.

### T024 [P] — `plugins/seraphis/CLAUDE.md` category table (FR-034)

**Files:** `plugins/seraphis/CLAUDE.md`.

Update the existing *"Decisions that outlive Phase 8 → 2. Preset categories are additive-only"* section from
"Phase 8 seeds exactly one category" to the shipped **seven-name table**, given the same load-bearing-table
treatment as the parameter-band table at `:22-32`. **Keep verbatim**: the rename-orphans-user-presets
warning and the "carried in two places that must always agree" rule.

**Verify:** read the file back; the seven names match `makeSeraphisPresetConfig()` exactly and in order.

### T025 [P] — Install-path and documentation verification (FR-017, FR-018, SC-019)

**Files:** `compliance.md` §§ *SC-019 install destination*, *FR-018 documentation verification*. **No source
change is expected** — these are verification obligations, and a verification obligation needs a
verification *action*, not an entry saying nothing changed.

1. **SC-019 / FR-017.** After a Release build, list `%PROGRAMDATA%\Krate Audio\Seraphis` — it must contain
   the **seven** category directories and **42** files. The copy is the **existing** POST_BUILD
   `krate_plugin_install_presets(${PLUGIN_NAME})` at `plugins/seraphis/CMakeLists.txt:113`, which resolves to
   `copy_directory resources/presets → $ENV{PROGRAMDATA}/Krate Audio/Seraphis`
   (`cmake/KratePlugin.cmake:288-311`, Windows-only guard at `:290-293`). **No new install rule, no
   `SRC_SUBDIR`/`DEST_SUBDIR`** — that form exists only for Membrum's nested `Kits` layout. The destination
   must match `Platform::getFactoryPresetDirectory("Seraphis")`
   (`plugins/shared/src/platform/preset_paths.h:23-27`). Record the listing.
2. **FR-018 clause 1.** Confirm `plugins/seraphis/installers/windows/setup.iss:66-68` remains the **sole**
   preset install path (no second copy rule added anywhere) and record the lines **verbatim**.
3. **FR-018 clause 2.** Read `plugins/seraphis/installers/linux/README.txt` **and the Linux branch of
   `plugins/shared/src/platform/preset_paths.cpp`**, and record **both destination strings verbatim with
   their `file:line`**. The header doc comment (`preset_paths.h:23-27`) is **not** sufficient evidence — it
   says `/usr/share/krate-audio/{pluginName}` while the implementation is
   `fs::path("/usr/share/krate-audio/" + lowerName)` (`preset_paths.cpp:49`), i.e. **lower-cased**. Today the
   README's system-wide destination is `/usr/share/krate-audio/seraphis` and the user destination is
   `~/Documents/Krate Audio/Seraphis` (`preset_paths.cpp:20`, `:27`) — but that is a fact to be **recorded
   from the files**, not assumed. If they diverge, **the README is what changes**, not the header.

**Verify:** all three sections of `compliance.md` carry actual listings / verbatim lines with `file:line`.

---

## GROUP 22 — Integration

### T026 — Single CMake registration audit + SC-021 case-count reconciliation

**Files:** read-only audit of root `CMakeLists.txt` and `plugins/seraphis/tests/CMakeLists.txt`; fix any
defect found; append to `compliance.md` § *SC-021*.

Assert, by reading:
1. Root `CMakeLists.txt` carries `add_executable(seraphis_preset_generator …)` **exactly once**, with
   `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`, the `KrateDSP KratePluginsShared sdk` link line
   with **no `vstgui_support`**, and `add_custom_target(generate_seraphis_presets …)` exactly once.
2. `plugins/seraphis/tests/CMakeLists.txt`'s enumerated list (`:5-79`) carries
   `unit/preset/factory_preset_test.cpp` and `integration/preset_render_sweep_test.cpp` **exactly once
   each**, and the `-fno-fast-math -fno-finite-math-only` block (`:117-170`) carries **both** — and carries
   nothing it should not (the perf TUs `param_perf_test.cpp`, `effects_perf_test.cpp`, `ui_perf_test.cpp`
   must stay **out**: `-fno-fast-math` would move the figures their baselines are pinned to).
3. **SC-021:** run `seraphis_tests.exe 2>&1 | tail -5` and reconcile the `test cases:` count against T001's
   baseline — the delta must equal the number of new cases added by T002–T021 (count them explicitly and
   list them). An unregistered TU exits 0 silently; only this delta catches it.

**Verify:** the reconciliation is written out case-by-case in `compliance.md`, not asserted in prose.

### T027 — Full-suite run + pluginval (SC-020, SC-021)

1. `"$CMAKE" --build build/windows-x64-release --config Release` — full build, **0 warnings**.
2. `build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5` — **0 failures**. Capture the
   run's own duration for SC-027 and confirm it is **≤ 6 minutes** for the sweep TU; if not, T016's lever
   list applies (in order) and the outcome is re-recorded.
3. Every other suite, to prove T013's additive change is inert (capture each to a log on the **first** run;
   never re-run a suite just to look at its output): `plugin_tests`, `approval_tests`, `disrumpo_tests`,
   `ruinae_tests`, `innexus_tests`, `gradus_tests`, `membrum_tests`, `shared_tests`, plus the five per-layer
   `dsp_*_tests`.
4. **SC-020:** `tools/pluginval.exe --strictness-level 5 --validate
   "build/windows-x64-release/VST3/Release/Seraphis.vst3"` — exit 0, no reported failure.

**Verify:** every summary line recorded in `compliance.md`. A failure anywhere is fixed here — **"pre-existing"
and "flaky" are not available conclusions.**

### T028 — Portability, lints, static analysis, determinism (SC-016, SC-022)

1. `node tools/check-portability.js` — clean. (A green Windows build proves nothing about the GCC/Clang
   legs; the generator in particular is a **new** target that only CI's Linux leg builds today.)
2. `node tools/check-preset-generator-determinism.js` — exit **0** over the full 42-preset tree (two fresh
   runs byte-identical, a third run over a populated tree changes **0** files).
3. `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` — **0** warnings.
4. `node tools/lint-float-bit-goldens.js` and `node tools/lint-midi-timing-goldens.js` — still green. The
   only byte comparisons this phase introduced are over a **serialized state stream**, the **`Info` XML**,
   and **generator file bytes on one machine**, never over rendered float samples.
5. Re-run the WSL/GCC build of `seraphis_preset_generator` — still 0 warnings, still emits 42 files
   (SC-018's final state).

**Verify:** each command's output recorded in `compliance.md` § *SC-022* / § *SC-016* / § *SC-018*. Capture
slow tool output to a log on the **first** run.

### T029 — Listening checkpoint, Phase 11.5 gate, and the release verdict (FR-030, FR-034a, SC-025, SC-029)

**Files:** `compliance.md` §§ *SC-029*, *SC-025*, *Release verdict*, plus the full FR/SC compliance table.

1. **SC-029 / FR-034a — the human checkpoint.** The phase owner auditions **all 42** presets against their
   assigned category name and records a pass/fail-or-notes row **per preset** (42 rows, **0** presets without
   a recorded outcome). No automated spectral heuristic substitutes for it, and a missing record is treated
   exactly like a missing SC-025 figure.
2. **SC-025 / FR-030 — the hard precondition.** Record Phase 11.5's **measured** whole-`process()` figure at
   the 8-voice operating point (worst-of-seven, fresh boot) with its `file:line` provenance, and check it
   against **≤ 25 % of one core** (roadmap 592–600). Cite the measurement, **never** the promise. Note that
   `specs/seraphis-phase11-5-process-optimization/` may not exist on this branch — if Phase 11.5's exit
   criteria are not demonstrably met, that is not a blocker for anything above, only for the verdict.
3. **The verdict.** Record it **explicitly as `DEFERRED`** — never blank, never omitted, never silently
   withheld — until Phase 11.5's three exit criteria are cited as met, regardless of how green
   SC-001…SC-024, SC-028 and SC-029 are.
4. **Fill the compliance table with concrete evidence**, per the project's completion-honesty rule: every
   FR row cites the implementation `file:line` read **now**; every SC row cites the test name **and the
   actual measured output**, compared against the spec threshold. No ✅ that was not individually verified
   in this task. Where a requirement is unmet, say so.

**Verify:** the compliance document is complete — 42 listening rows, Phase 11.5's cited figure, an explicit
verdict, and no table row filled from memory.

---

## Task index

| Group | Task | Delivers | Verified by |
|---|---|---|---|
| 1 | T001 | Baselines + `compliance.md` | recorded numbers |
| 2 | T002 | Static TU + CMake registration + FR-001 red test | `seraphis_tests` (FAILS) |
| 3 | T003 | FR-001 seven categories + directories | `Seraphis_FactoryPresets_CategoriesMatchConfig` |
| 4 | T004 | `tools/seraphis_preset_defs.h` (3-preset pilot) | same case, +`kCategories` clause |
| 5 | T005 | `tools/seraphis_preset_generator.cpp` | (T006) |
| 6 | T006 | Root CMake targets, MSVC + WSL/GCC link, pilot run | binary emits 3 files; SC-018 |
| 7 | T007 | `preset_test_support.h` + decode tripwires | `Seraphis_PresetSupport_DecodeConsumesWholeStream` |
| 8 | T008 | SC-002, FR-005 | red until T014 |
| 9 | T009 | SC-003, SC-004, SC-005 | `seraphis_tests` |
| 10 | T010 | SC-006, SC-007, SC-008 | `seraphis_tests` |
| 11 | T011 | SC-013, SC-014, SC-014a, FR-006a/FR-009 | `seraphis_tests` |
| 12 | T012 [P] · T013 [P] | determinism script (SC-016) · allocation thread filter | script exit 0 · all suites unchanged |
| 13 | T014 | the 42 presets | every `Seraphis_FactoryPresets_*` green |
| 14 | T015 | sweep TU + SC-009, SC-010, SC-010a | `seraphis_tests` |
| 15 | T016 | measured RTF, projection, levers (SC-027) | `compliance.md` |
| 16 | T017 | SC-011, SC-012 (Windows-guarded Aether arm) | `seraphis_tests` |
| 17 | T018 | SC-015, SC-015a | `seraphis_tests` |
| 18 | T019 | SC-026, SC-028 + negative control | `seraphis_tests` |
| 19 | T020 | FR-029a probe, 3 toolchains, pinned tolerances | `compliance.md` |
| 20 | T021 | SC-017 gating comparison, all legs | `seraphis_tests` |
| 21 | T022–T025 [P] | roster · version/changelog · CLAUDE.md · install/doc verification | SC-024, SC-023, FR-034, SC-019/FR-018 |
| 22 | T026–T029 | CMake audit + SC-021 · full suites + pluginval · lints · verdict | SC-020, SC-022, SC-025, SC-029 |
