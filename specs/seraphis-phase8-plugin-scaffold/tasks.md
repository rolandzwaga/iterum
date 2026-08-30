# Tasks: Seraphis Phase 8 — Plugin Scaffold

**Spec:** `specs/seraphis-phase8-plugin-scaffold/spec.md`
**Plan:** `specs/seraphis-phase8-plugin-scaffold/plan.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 8 (lines 323–435)
**Branch:** `feat/seraphis-phase1-life-modulators` (one branch for all Seraphis phases — do not rename)
**Status:** TASKS — no implementation yet

---

## How to execute this list

- Tasks are grouped. **Groups run in order.** Inside a group, tasks marked **[P]** touch fully disjoint
  **new** files and may run in parallel. Any task that edits a **shared/pre-existing** file (root
  `CMakeLists.txt`, `.gitignore`, `processor.cpp` after it exists, `tests/CMakeLists.txt`, CI workflows,
  `tools/*`) is **alone in its group**.
- Every task is self-contained: it names the exact files, the failing test to write **first**, the exact
  assertions with numbers, the implementation intent, and the command that verifies it.
- Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
  Tasks whose deliverable is not code (resource files, rosters, generated artifacts) say so explicitly
  and name the tool/command that is their failure surface instead.
- **No commit tasks.** Commits happen outside this workflow.
- Build commands always use the full CMake path:
  `CMAKE="C:/Program Files/CMake/bin/cmake.exe"`.
- Capture every slow command (build, tidy, pluginval, full suite) to a log **on its first run**; never
  re-run a slow command just to look at its output.
- **Phase 8 writes no DSP.** No file under `dsp/` may be created or modified by any task below.

**Constants used repeatedly below** (all read from the headers, cited where used):
`SeraphisEngine::kMaxBlockSamples = 2048` (`dsp/include/krate/dsp/systems/seraphis_engine.h:134`),
`kMaxVoices = 16` (`:130`), `kControlChunkSamples = 64` (`:132`), `kBloomPartialCap = 32` (`:154`),
`kOutputSaturation = 0.15f` (`:142`), `kSumGainSmoothMs = 20.0f` (`:138`);
`TruePeakLimiter` ceiling `0.8912509f` (`true_peak_limiter.h:46, 168`);
`kSampleTolerance = 1.0e-4f` (`tests/test_helpers/render_fingerprint.h:49`),
`kMetricTolerance = 1.0e-5` (`:52`).

---

## GROUP 1 — Plugin skeleton, identity and resources (all NEW files, parallel)

No test target exists yet. The failure surface for this group is the **configure + build** in T013 and
the AU gate SC-004; every task here states what would break if its file is wrong.

### T001 [P] — `version.json`, `CHANGELOG.md`, `README.md`

**Create:**
- `plugins/seraphis/version.json`
- `plugins/seraphis/CHANGELOG.md`
- `plugins/seraphis/README.md`

**Failure surface (no unit test):** `krate_plugin_read_version(SERAPHIS)` (`cmake/KratePlugin.cmake:35`)
hard-fails at configure time on a missing key; `tools/check-changelog-coverage.js` (parses
`^##\s*\[([^\]]+)\]`, `:84–88`) fails on a missing version section.

**Implement (FR-002, FR-010):**
- `version.json` MUST contain exactly the six keys `krate_plugin_read_version` parses
  (`cmake/KratePlugin.cmake:37–42`): `version` = `"0.1.0"`, `name` = `"Seraphis"`, `description`,
  `publisher` = `"Krate Audio"`, `url` = `"https://rolandzwaga.github.io/krate-audio/seraphis/"`,
  `copyright`.
- The optional `preset_subdir` key MUST be **absent**. `.github/workflows/release.yml:297–303` reads it
  with `jq -r '.preset_subdir // empty'`; absent ⇒ presets stage directly under
  `Krate Audio/Seraphis`, which is what FR-050/FR-051 require.
- `CHANGELOG.md` MUST contain a `## [0.1.0]` section describing the scaffold.
- `README.md`: short description plus the build / test / pluginval commands for this plugin.

**Verify:** file contents only at this stage; the real gate is T013's configure and T029's
`node tools/check-changelog-coverage.js` run (after T028 adds `seraphis` to its `PLUGINS` array).

---

### T002 [P] — `plugins/seraphis/CLAUDE.md` leaf

**Create:** `plugins/seraphis/CLAUDE.md`

**Failure surface (no unit test):** inspection — FR-009 is a traceability-table "inspection" row, and the
two decisions it records have **no other home in the tree**.

**Implement (FR-009):** follow the shape of `plugins/membrum/CLAUDE.md` / `plugins/ruinae/CLAUDE.md`:
plugin type (`aumu` instrument), `src/` skeleton, parameter-ID scheme (the reserved range map from
FR-013), test-target invocation
(`build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5`), pluginval path
(`tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`).

It MUST additionally record, in prose that survives this phase:
1. **MPE / note expression is a known Phase 9 decision.** Phase 8 ships **no**
   `INoteExpressionController` (FR-019). Spell out the caveat: adding an interface to a **released**
   controller FUID can invalidate host-cached class metadata, so Phase 9 must decide knowingly.
2. **Preset categories are additive-only.** `Textures` is a *seed*, not a placeholder. Phase 12
   **extends** the list and MUST NOT rename a shipped category — a rename orphans every preset saved
   against it (the Membrum lesson, roadmap line 388). Both the filesystem subdirectory and the XML
   metadata carry the name and must always agree.
3. The two FUID values generated in T003, marked **immutable**.

**Verify:** inspection; the FUID values are pasted here by T003's executor after generation.

---

### T003 [P] — `src/plugin_ids.h` (FUIDs, parameter IDs, state version)

**Create:** `plugins/seraphis/src/plugin_ids.h`

**Failure surface:** compile-time (every other TU includes it) plus SC-003 (pluginval loads the factory)
and SC-010 (state version prefix `== 1`).

**Implement (FR-011, FR-012, FR-013, FR-014), exactly the shape in plan §2.1:**
- `namespace Seraphis` (ODR sweep in spec's New-components table: **no** `namespace Seraphis` exists
  anywhere in `dsp/` or `plugins/` — CLEAR).
- `constexpr Steinberg::int32 kCurrentStateVersion = 1;` at namespace scope (FR-012).
- `static const Steinberg::FUID kProcessorUID(…)` and `kControllerUID(…)` with **two freshly generated
  v4 GUIDs**. Generation procedure: `node -e "console.log(require('crypto').randomUUID())"` twice,
  format each as four `0x`-prefixed 32-bit groups, then verify non-collision against the twelve
  registered FUIDs with
  `grep -rn "FUID k\(Processor\|Controller\)UID" plugins/*/src/plugin_ids.h`
  (`disrumpo:26,:31`, `gradus:20,:23`, `innexus:20,:24`, `iterum:21,:25`, `membrum:18,:21`,
  `ruinae:24,:28`). Paste both values into `plugins/seraphis/CLAUDE.md` (T002) as immutable.
- `static const char* kSubCategories = "Instrument|Synth";` — **`static const`, not `constexpr`**
  (cross-platform rule: anything initialized from an SDK constant is `const`).
- `enum ParameterIDs : Steinberg::Vst::ParamID` with **exactly** these eight:
  `kMasterGainId = 0`, `kPolyphonyId = 1`, `kSoftLimitId = 2`, `kMacroDreamId = 100`,
  `kMacroBloomId = 101`, `kMacroDissolveId = 102`, `kMacroGravityId = 103`, `kMacroEntropyId = 104`.
- A comment documenting the full reserved map: `0–99` Global, `100–199` Macros, `200–399` Harmonic
  Cloud, `400–599` Spectral Morph/Entropy, `600–799` Life Modulators, `800–999` Continuous Body,
  `1000–1199` Atmosphere, `1200–1399` Aether, `1400+` Effects.
- A comment recording the **frozen parameter types** (FR-048): seven plain `Steinberg::Vst::Parameter`
  (master gain, soft limit, five macros) and one `Steinberg::Vst::StringListParameter`
  (`kPolyphonyId`, because `createDropdownParameterWithDefault` returns one —
  `plugins/shared/src/ui/parameter_helpers.h:47`). Never swap a type at an ID.
- `constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;` and `kMacroParamRangeEnd = 200;`
  for FR-042's range dispatch.

**Verify:** compiles once T013 links; FUID non-collision grep returns no duplicate of either value.

---

### T004 [P] — AU identity resources

**Create:**
- `plugins/seraphis/resources/auv3/audiounitconfig.h.in`
- `plugins/seraphis/resources/au-info.plist`
- `plugins/seraphis/resources/auv3/macOS/Seraphis.entitlements`

**Failure surface:** **SC-004** — `auval -v aumu Srph KrAt` on the macOS CI leg. A mismatch between
these three and the bus configuration (T016) is the documented `-10875` AU-init failure.

**Implement (FR-015, FR-016, FR-017):**
- `audiounitconfig.h.in` MUST be a **complete copy** of
  `plugins/membrum/resources/auv3/audiounitconfig.h.in:1–39` with `Membrum`→`Seraphis`,
  `Mbrm`→`Srph`. **All fourteen defines** are required, not just the five value macros — the AUv3
  target built by `krate_plugin_platform_setup` (`cmake/KratePlugin.cmake:208–219`) also consumes the
  unquoted token forms and the trailing flags/delegate defines:
  `kAUcomponentType 'aumu'`, `kAUcomponentType1 aumu`, `kAUcomponentSubType 'Srph'`,
  `kAUcomponentSubType1 Srph`, `kAUcomponentManufacturer 'KrAt'`, `kAUcomponentManufacturer1 KrAt`,
  `kAUcomponentDescription AUv3WrapperExtension`, `kAUcomponentName Krate Audio: Seraphis`,
  `kAUcomponentTag Synthesizer`, `kAUcomponentVersion @AU_COMPONENT_VERSION@`,
  `kSupportedNumChannels 02`, `kAUcomponentFlags 0`, `kAUcomponentFlagsMask 0`,
  `kAUapplicationDelegateClassName AppDelegate`.
  The `02` value MUST carry the digit-pair rationale comment (model `:26–34`), rewritten for a
  single-output instrument: `02` = one config, 0 inputs / 2 outputs. **`0022` would parse as
  `(0,0)+(2,2)` and break the instrument.**
- `au-info.plist`: `plugins/membrum/resources/au-info.plist:23–50` with the same substitutions —
  exactly **one** `AudioComponents` dict (`factoryFunction AUWrapperFactory`, `manufacturer KrAt`,
  `name "Krate Audio: Seraphis"`, `subtype Srph`, `type aumu`) and exactly **one**
  `AudioUnit SupportedNumChannels` dict with `Inputs 0` / `Outputs 2`.
- `Seraphis.entitlements`: copy of Membrum's (single key `com.apple.security.app-sandbox = true`);
  referenced by `krate_plugin_platform_setup`'s `ENTITLEMENTS` argument (`cmake/KratePlugin.cmake:213`).
- **`Srph` is free:** `grep -rn "Srph" plugins/ tools/ .github/` → 0 hits (existing subtypes:
  `Itrm Dsrm Ruin Innx Grad Mbrm`).

**Verify:** local `--target Seraphis` build (T013) proves nothing here; the gate is the macOS CI
`auval` step added in T028 (roster site #10) and recorded in T033.

---

### T005 [P] — placeholder `resources/editor.uidesc`

**Create:** `plugins/seraphis/resources/editor.uidesc`

**Failure surface:** SC-012 clause 2b (T024's view-tree walk) and SC-018
(`node tools/check-bundle.js` requires a non-empty `Contents/Resources/editor.uidesc`,
`tools/check-bundle.js:15–16`).

**Implement (FR-054), exactly the file in plan §2.10:**
- Template named **`"editor"`** (the harness opens it by that name:
  `tests/test_helpers/editor_lifecycle_harness.h:102–105`), class `CViewContainer`, size `420, 300`.
- A `<control-tags>` block with **exactly eight** tags: `MasterGain=0`, `Polyphony=1`, `SoftLimit=2`,
  `MacroDream=100`, `MacroBloom=101`, `MacroDissolve=102`, `MacroGravity=103`, `MacroEntropy=104`.
- **Exactly eight `CControl` views**, one per tag, with the class matching the registered parameter
  type (FR-048 freezes those types):
  - `CSlider` for `MasterGain` and the five macros (six plain `Vst::Parameter`s);
  - `COptionMenu` for `Polyphony` (the `StringListParameter`);
  - `CCheckBox` for `SoftLimit` (stepped toggle).
  `CTextLabel`s are permitted as decoration but are **not** `CControl`s and must carry no
  `control-tag` — T024 asserts `controls.size() == 8`.
- **Stock views only.** No custom view class name may appear anywhere in the file: on a leg where the
  `ViewCreator` TU was not linked, `VST3Editor::open()` drops the view silently and SC-012 passes
  vacuously. Permitted classes (all already instantiated from `.uidesc` elsewhere in this repo, all
  bitmap-free): `CViewContainer`, `CSlider`, `COptionMenu`, `CCheckBox`, `CTextLabel`.
- Header comment: *"Phase 8 PLACEHOLDER — Phase 11 replaces this file wholesale."*

**Verify:** T024 (`Seraphis_EditorLifecycle`, clause 2b) and T030's `check-bundle.js`.

---

### T006 [P] — installers, empty directories, preset seed

**Create:**
- `plugins/seraphis/installers/windows/setup.iss`
- `plugins/seraphis/installers/linux/README.txt`
- `plugins/seraphis/docs/.gitkeep`
- `plugins/seraphis/src/ui/.gitkeep`
- `plugins/seraphis/resources/presets/Textures/.gitkeep`

**Failure surface:** `.github/workflows/release.yml:235` (setup.iss) and `:372` (linux README) — a
release run fails on a missing file; FR-051's filesystem/metadata agreement.

**Implement (FR-008, FR-051, FR-056, FR-080):**
- `setup.iss`: `plugins/gradus/installers/windows/setup.iss` with `Gradus`→`Seraphis` and a **fresh
  `AppId` GUID** (generate a new one; do not reuse Gradus's).
- `installers/linux/README.txt`: Gradus's, retitled.
- `docs/` is a **directory only** — Phase 12 authors `index.html` (FR-080;
  `.github/workflows/docs.yml` needs no edit, its per-plugin loop globs `plugins/*/` and reports
  `has_page: false` harmlessly).
- `src/ui/` **empty** in Phase 8 (FR-056; Phase 11 fills it).
- `resources/presets/Textures/` seeds the filesystem half of the category so it agrees with the XML
  metadata in T010's preset config from day one. **`Textures` is permanent — never renamed.**

**Verify:** files exist; T033 records FR-051's both-halves-agree check (also asserted by T024's
`Seraphis_PresetConfigIsLive` section).

---

## GROUP 2 — Plugin-local headers (all NEW files, parallel; depend on T003)

### T007 [P] — `src/engine/seraphis_engine_config.h`

**Create:** `plugins/seraphis/src/engine/seraphis_engine_config.h`

**Test first:** none yet — the header's behaviour is asserted by **T023**'s SC-024 section, which calls
`applyAetherTargets()` directly with non-neutral values. Write the header so that test is possible:
`applyAetherTargets` MUST be a free function, not a lambda inside `process()`.

**Implement (FR-053, FR-034a), exactly plan §2.2 — the header introduces NO new type:**
- `inline constexpr std::uint32_t kEngineSeed = 1u;` and `kReverbSeed = 1u` (explicit, never inherited
  from the struct defaults at `seraphis_engine.h:96` / `aether_reverb.h:1586`, so a future `dsp/`
  default change cannot silently move Seraphis's sound).
- `inline constexpr float kMasterGainSmoothMs = 20.0f;` with a provenance comment naming its sibling
  `SeraphisEngine::kSumGainSmoothMs = 20.0f` (`seraphis_engine.h:138`). **Never an unnamed literal at
  the use site.**
- `inline constexpr std::size_t kMaxBlockSamples = Krate::DSP::SeraphisEngine::kMaxBlockSamples;`
  (= 2048). This ONE constant governs the engine config, the reverb config, FR-026's slice bound and
  FR-028's scratch size — they cannot drift apart.
- `makeSeraphisEngineConfig(std::size_t polyphony, std::uint32_t seed, std::size_t maxBlockSamples)`
  returning `Krate::DSP::SeraphisEngineConfig` with the shipped Phase 7 voice defaults
  (`captureSeconds 4.0f`, `blurEnabled true`, `freezeEnabled true`, `blurFftSize 1024`,
  `freezeFftSize 2048`, `maxBlockSamples` from the argument) and `polyphony` / `seed` from the
  arguments.
- `makeSeraphisReverbConfig(std::size_t maxBlockSamples)` returning
  `Krate::DSP::AetherReverb::PrepareConfig` with `numChannels 8`, `maxBlockSamples` from the argument,
  `maxDelaySeconds 0.50f`, `shimmerEnabled true`, `shimmerMode PitchMode::Granular`,
  `bloomEnabled true`, **`spectralDiffusionEnabled true`**, **`diffusionFftSize 1024`**,
  `seed kReverbSeed`. The last two MUST NOT be changed — the resulting 1024-sample latency is owned by
  FR-033, not dodged here.
- `inline void applyAetherTargets(Krate::DSP::AetherReverb& reverb, const Krate::DSP::SeraphisAetherTargets& t) noexcept`
  calling, in this order: `setMix`, `setSize`, `setWidth`, `setShimmerOctaveSend`,
  `setShimmerFifthSend`, `setBloomSend`, `setSizeBreathDepth`, `setDimensionalityTideDepth` — a copy of
  `tests/test_helpers/seraphis_chain.h:215–222` with the reverb passed by reference.

**RT safety:** `applyAetherTargets` is `noexcept` and allocation-free (each setter is a clamp + smoother
store via `AetherReverb::applyControl`). `make*Config` are prepare-time only.

**Verify:** compiles in T013; behaviour asserted by T023 (SC-024).

---

### T008 [P] — `src/parameters/global_params.h`

**Create:** `plugins/seraphis/src/parameters/global_params.h`

**Test first:** none in this task — **T020** writes `Seraphis_ParamDenormRoundTrip` against these
functions and **T019** writes the state round-trip. Write the six functions so both are possible.

**Implement (FR-040, FR-043, FR-044, FR-048), the Ruinae contract
(`plugins/ruinae/src/parameters/global_params.h:39, 87, 130, 178, 187, 220`):**
- `struct GlobalParams { std::atomic<float> masterGain{1.0f}; std::atomic<int> polyphony{8}; std::atomic<bool> softLimit{true}; };`
- `handleGlobalParamChange(GlobalParams&, ParamID, ParamValue)` with the denormalizations:
  - `kMasterGainId` → `std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f)`
  - `kPolyphonyId` → `std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16)`
  - `kSoftLimitId` → `value >= 0.5`
- `registerGlobalParams(ParameterContainer&)`:
  - `addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.5, kCanAutomate, kMasterGainId)`
  - `addParameter(Krate::Plugins::createDropdownParameterWithDefault(STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/7, {STR16("1"), … STR16("16")}))`
    (`plugins/shared/src/ui/parameter_helpers.h:47`; index 7 ⇒ 8 voices, matching
    `SeraphisEngineConfig::polyphony`)
  - `addParameter(STR16("Soft Limit"), STR16(""), 1, 1.0, kCanAutomate, kSoftLimitId)` — `stepCount = 1`
    toggle, default on.
- `formatGlobalParam(ParamID, ParamValue, String128)`; `saveGlobalParams` writing
  `writeFloat(masterGain)`, `writeInt32(polyphony)`, `writeInt32(softLimit ? 1 : 0)` — **12 bytes**;
  `loadGlobalParams` using the **EOF-safe** read pattern (a failed read leaves the atomic at its
  default and returns `false`); `template <typename SetParamFunc> loadGlobalParamsToController` inverting
  each mapping (`masterGain / 2.0`, `(polyphony - 1) / 15.0`, `softLimit ? 1.0 : 0.0`).
- **`clampPolyphony(int raw)` helper — mandatory, a deliberate divergence from Ruinae.** Ruinae's loader
  stores the raw stream value; Seraphis cannot, because `pushGlobalParams()` (T022) compares the stored
  value against the engine's **clamped** `getPolyphony()`. Declare
  `[[nodiscard]] inline std::size_t clampPolyphony(int raw) noexcept` returning
  `std::clamp(static_cast<std::size_t>(std::max(raw, 1)), std::size_t{1}, Krate::DSP::SeraphisEngine::kMaxVoices)`
  and use it in `loadGlobalParams` and at every other conversion into the engine's domain. Without it a
  corrupt stream (polyphony `0`, `20`, negative) makes the change detector fire on **every block,
  forever**.
- **Soft-limit documentation string (FR-044, binding):** the parameter text and header comment MUST
  state it controls the **tape-saturation amount only** and does **not** bypass the true-peak limiter —
  `processOutputStage` ends with `limiter_.processBlock(l, r, n)` and has no bypass path
  (`seraphis_engine.h:521`).

**Verify:** compiles in T013; asserted by T019 (SC-010) and T020 (SC-009).

---

### T009 [P] — `src/parameters/macro_params.h`

**Create:** `plugins/seraphis/src/parameters/macro_params.h`

**Test first:** none in this task — **T019**'s SC-010 default-state clause and **T023**'s SC-023 inert
control assert this header.

**Implement (FR-041):**
```cpp
struct MacroParams {
    std::atomic<float> dream{0.0f};
    std::atomic<float> bloom{0.0f};
    std::atomic<float> dissolve{0.0f};
    std::atomic<float> gravity{0.5f};
    std::atomic<float> entropy{0.0f};
};
```
The initializers are **load-bearing**: value-initialization would leave `gravity` at `0.0f`,
contradicting both the registered default and `SeraphisMacroValues::gravity = 0.5f`
(`seraphis_macro_matrix.h:126`). Caught by T019's default-state clause.

Same six functions as the global pack (`handleMacroParamChange` clamping to `[0,1]`,
`registerMacroParams` adding five **plain `Vst::Parameter`s** via the
`addParameter(title, units, stepCount = 0, default, kCanAutomate, id)` overload with unit `"%"` and
defaults `0.0 / 0.0 / 0.0 / 0.5 / 0.0`, `formatMacroParam`, `saveMacroParams` = five `writeFloat`
(**20 bytes**), `loadMacroParams` EOF-safe, `loadMacroParamsToController`).

**INERT (FR-041):** no file in Phase 8 may read `MacroParams` and write it into
`SeraphisMacroMatrix::setMacro/setMacros`. This is verified as a negative control by T023 (SC-023) and
is what Phase 9 inverts.

**Verify:** compiles in T013; asserted by T019 and T023.

---

### T010 [P] — preset and update config adapters

**Create:**
- `plugins/seraphis/src/preset/seraphis_preset_config.h`
- `plugins/seraphis/src/update/seraphis_update_config.h`

**Test first:** none in this task — **T024**'s `Seraphis_PresetConfigIsLive` section asserts the preset
config; the update config's only detector is the compile touch point in `controller.cpp` (T012).

**Implement (FR-050, FR-052):**
- `inline Krate::Plugins::PresetManagerConfig makeSeraphisPresetConfig()` returning
  `{ kProcessorUID, "Seraphis", "Synth", { "Textures" } }`. **Field order is load-bearing**
  (`plugins/shared/src/preset/preset_manager_config.h:17–18, 20–25`); use comment-style initializers as
  `plugins/ruinae/src/preset/ruinae_preset_config.h:18–27` does.
- `inline Krate::Plugins::UpdateCheckerConfig makeSeraphisUpdateConfig()` returning
  `{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }`
  (`plugins/shared/src/update/update_checker_config.h:14–18`; model
  `plugins/ruinae/src/update/ruinae_update_config.h:8–14`). Include `"../version.h"` for the two macros.
- **No `UpdateChecker` instance anywhere in Phase 8** — it spawns a `std::thread` and a network fetch
  that would land inside the editor-lifecycle harness, the ASan lane (T032) and the valgrind nightly.

**Verify:** compiles in T013 (via T012's `static_assert` touch point); preset config asserted by T024.

---

## GROUP 3 — Processor and controller skeletons (NEW files, parallel)

These two tasks produce **compiling, linkable** classes. Behaviour is added test-first in Groups 7–12;
the bodies left minimal here are named explicitly so no later task is surprised.

### T011 [P] — `src/processor/processor.{h,cpp}` skeleton

**Create:** `plugins/seraphis/src/processor/processor.h`, `plugins/seraphis/src/processor/processor.cpp`

**Test first:** none — no test target exists yet (T014 creates it). This task's failure surface is the
link step in T013 and the `static_assert` below.

**Implement (FR-022, FR-028, FR-067) — the header exactly as plan §2.5.1:**
- `class Processor : public Steinberg::Vst::AudioEffect` in `namespace Seraphis`, with
  `static Steinberg::FUnknown* createInstance(void*)`.
- Declare **all** overrides now (bodies filled by later tasks): `initialize`, `terminate`,
  `setBusArrangements`, `setupProcessing`, `setActive`, `process`, `getLatencySamples`, `setState`,
  `getState`.
- Private helpers declared: `processParameterChanges(IParameterChanges*) noexcept`,
  `pushGlobalParams() noexcept`, `renderSlice(float*, float*, std::size_t) noexcept`.
  **No `announceLatencyIfChanged()` and no `lastReportedLatency_`** — plan §1.3 C-1/C-2: the reported
  latency is the constant 1024 in every reachable state, and an `AudioEffect` has no route to an
  `IComponentHandler`. Do not add one.
- Members: `std::unique_ptr<Krate::DSP::SeraphisEngine> engine_`,
  `std::unique_ptr<Krate::DSP::AetherReverb> reverb_` (**FR-022: never by value** —
  `sizeof(SeraphisEngine)` is 771 968 B against MSVC's 1 MiB default stack,
  `seraphis_engine.h:119–122, 159–164`), `Krate::DSP::SeraphisMacroMatrix macros_{}` (by value, ~20 B),
  `GlobalParams globalParams_{}`, `MacroParams macroParams_{}`,
  `Krate::DSP::OnePoleSmoother masterGain_{1.0f}`, `bool anySamplesSincePrepare_ = false`,
  `double sampleRate_ = 44100.0`, `bool prepared_ = false`, `std::size_t lastPushedPolyphony_ = 0`,
  `bool lastPushedSoftLimit_ = true`, `std::size_t setPolyphonyCalls_ = 0`,
  `std::vector<float> dryL_, dryR_, wetL_, wetR_`,
  `std::array<float, Krate::DSP::SeraphisEngine::kBloomPartialCap> bloomPartials_{}` (32 floats).
- Test-only read surfaces: `engineForTest()`, `reverbForTest()`, `setPolyphonyCallCountForTest()`.
- `static_assert(sizeof(Processor) < 64u * 1024u, "FR-067: …");`
- `.cpp` this task: `initialize()` = `AudioEffect::initialize(context)` then construct the two
  `unique_ptr`s (non-RT, once). `terminate()` releases both and calls `AudioEffect::terminate()`.
  Every other override is a minimal `return kResultOk;` / delegation, **marked with a
  `// TODO(T0xx)` naming the task that fills it** (T016 buses, T017 setupProcessing/latency,
  T018 process guards + setActive, T019 state, T020 param dispatch, T021 slice loop, T022 push).

**Verify:** links in T013; zero warnings.

---

### T012 [P] — `src/controller/controller.{h,cpp}` (complete)

**Create:** `plugins/seraphis/src/controller/controller.h`, `plugins/seraphis/src/controller/controller.cpp`

**Test first:** none in this task — **T024** (`Seraphis_EditorLifecycle`) and **T019**'s controller
clause assert this file. Write it so both are possible (in particular, `setComponentState` must really
read the stream).

**Implement (FR-047, FR-048, FR-050, FR-052, FR-055, FR-019) — plan §2.6:**
- `class Controller : public Steinberg::Vst::EditControllerEx1, public VSTGUI::VST3EditorDelegate` in
  `namespace Seraphis`, with `createInstance`. **No `INoteExpressionController`** (FR-019). **No**
  `createCustomView` / `verifyView` overrides (FR-018, FR-056 — no custom views until Phase 11).
- `initialize()`: `EditControllerEx1::initialize(context)`; `registerGlobalParams(parameters)`;
  `registerMacroParams(parameters)` — **exactly eight parameters**; then
  `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeSeraphisPresetConfig(), nullptr, this);`
  (shape at `plugins/ruinae/src/controller/controller.cpp:225–226`; ctor signature at
  `plugins/shared/src/preset/preset_manager.h:55–61`). **No `UpdateChecker`** (FR-052).
- `setComponentState(IBStream*)`: read the version `int32` first, return `kResultFalse` on
  `version > kCurrentStateVersion`, then `loadGlobalParamsToController(streamer, setParam)` and
  `loadMacroParamsToController(streamer, setParam)` with
  `setParam = [this](ParamID id, double v){ setParamNormalized(id, v); }`. Order **must** match
  `Processor::getState`.
- `getParamStringByValue()`: try `formatGlobalParam`, then `formatMacroParam`, else fall through to
  `EditControllerEx1::getParamStringByValue` (so the `StringListParameter` formats itself).
- `createView(FIDString name)`: if `FIDStringsEqual(name, ViewType::kEditor)` return
  `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")`, else `nullptr`.
- **Compile touch point for the update config (mandatory).** `seraphis_update_config.h` is included by
  nothing else, and listing a `.h` in a CMake source list does **not** compile it (CMake sets
  `HEADER_FILE_ONLY`). Immediately after the includes in `controller.cpp`:
  ```cpp
  #include "update/seraphis_update_config.h"
  static_assert(std::is_same_v<decltype(Seraphis::makeSeraphisUpdateConfig()),
                               Krate::Plugins::UpdateCheckerConfig>);
  ```
  No `UpdateChecker` object is constructed, so FR-052's prohibition holds exactly.

**Verify:** links in T013; asserted by T019 (controller clause) and T024.

---

## GROUP 4 — Build wiring (edits SHARED root `CMakeLists.txt` and `.gitignore` — sequential, alone)

### T013 — `entry.cpp`, plugin `CMakeLists.txt`, root registration, `.gitignore`

**Create:** `plugins/seraphis/src/entry.cpp`, `plugins/seraphis/CMakeLists.txt`
**Edit (shared):** root `CMakeLists.txt` (after line 493), `.gitignore` (after line 75)

**Test first:** none — this task's failure surface **is** the configure + link, plus SC-020's
`git status --porcelain plugins/seraphis` emptiness.

**Implement:**
- `src/entry.cpp` (FR-018): `plugins/ruinae/src/entry.cpp:40–78` with `Ruinae`→`Seraphis` —
  `#define stringPluginName "Seraphis"` (an *identical* redefinition of the macro `cmake/version.h.in:31`
  generates from `version.json`'s `"name"`, so it is warning-free; keep the spellings identical),
  `BEGIN_FACTORY_DEF(stringCompanyName, stringVendorURL, stringVendorEmail)`, two `DEF_CLASS2`
  (processor → `kVstAudioEffectClass`, controller → `kVstComponentControllerClass`, using
  `INLINE_UID_FROM_FUID(kProcessorUID/kControllerUID)`, `kSubCategories`, `FULL_VERSION_STR`,
  `kVstVersionString`, `createInstance`), `END_FACTORY`. **It MUST NOT include any `ui/*.h` header** —
  Ruinae includes them only to trigger custom `ViewCreator` registration; Seraphis registers none.
- `plugins/seraphis/CMakeLists.txt` (FR-001…FR-006), exactly plan §5.1:
  `krate_plugin_read_version(SERAPHIS)` → `krate_plugin_configure_generated_files()` →
  `set(PLUGIN_NAME "${SERAPHIS_NAME}")` → `smtg_add_vst3plugin(${PLUGIN_NAME} …)` with the Phase 8
  source list (entry, plugin_ids.h, version.h, processor.{h,cpp}, controller.{h,cpp},
  parameters/*.h, engine/preset/update headers, plus
  `${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp` for `PresetManager` saving) →
  `target_link_libraries(… PRIVATE sdk vstgui_support KrateDSP KratePluginsShared)` →
  `target_include_directories(… PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)` →
  `smtg_target_configure_version_file` →
  `krate_plugin_platform_setup(${PLUGIN_NAME} TAG SERAPHIS BUNDLE_BASE com.krateaudio.seraphis ENTITLEMENTS Seraphis.entitlements KIND instrument)`
  (**`KIND instrument` is mandatory** — `cmake/KratePlugin.cmake:130–132` hard-errors otherwise and
  `:197–206` selects the instrument AUv3 storyboard from it) →
  `smtg_target_add_plugin_resources(… RESOURCES resources/editor.uidesc)` →
  `krate_plugin_install_to_system` → `krate_plugin_install_presets()` (**no** `SRC_SUBDIR`/`DEST_SUBDIR`) →
  `krate_plugin_set_warnings` → `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()`.
  *(The `add_subdirectory(tests)` line is added here; the `tests/` directory is created in T014 — run
  T014 before re-configuring, or guard with `if(EXISTS …)` temporarily and remove the guard in T014.)*
- Root `CMakeLists.txt` (FR-070): append `add_subdirectory(plugins/seraphis)` to the existing block at
  `CMakeLists.txt:488–493` (`iterum`, `disrumpo`, `ruinae`, `innexus`, `gradus`, `membrum` — verified
  this session).
- `.gitignore` (FR-007a): append the Seraphis trio to the per-plugin block that currently ends at
  `.gitignore:75` (`membrum`'s three lines at `:73–75`, verified this session):
  ```
  /plugins/seraphis/resources/win32resource.rc
  /plugins/seraphis/src/version.h
  /plugins/seraphis/resources/auv3/audiounitconfig.h
  ```
  Without them the first `cmake --preset` leaves three generated files untracked-and-committable and a
  later version bump dirties them.

**Verify:**
```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis 2>&1 | tee /tmp/seraphis-build.log
grep -E "plugins[/\\]seraphis" /tmp/seraphis-build.log | grep -cE "warning C|warning:"   # MUST be 0
git status --porcelain plugins/seraphis                                                  # MUST be empty
```

---

## GROUP 5 — Test target (creates `tests/CMakeLists.txt`; sequential, alone)

### T014 — `seraphis_tests` target, test main, stubs, empty TUs

**Create:**
- `plugins/seraphis/tests/CMakeLists.txt`
- `plugins/seraphis/tests/vstgui_test_stubs.cpp`
- `plugins/seraphis/tests/unit/test_main.cpp`
- Empty TUs (comment + includes only, **no `TEST_CASE` yet** — later tasks add them):
  `tests/unit/processor_bus_test.cpp`, `tests/unit/param_denorm_test.cpp`,
  `tests/unit/state_roundtrip_test.cpp`, `tests/unit/midi_event_test.cpp`,
  `tests/unit/lifecycle_test.cpp`, `tests/unit/controller/editor_lifecycle_test.cpp`,
  `tests/integration/processor_audio_test.cpp`, `tests/integration/param_flow_test.cpp`

**Test first:** the target itself is the artefact; it is verified by building and running it.

**Implement (FR-060…FR-065, FR-066a), exactly plan §5.2:**
- `add_executable(seraphis_tests …)` listing the nine test TUs **plus a second compilation of every
  plugin `.cpp`** via `${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp` and
  `../src/controller/controller.cpp`, **plus** the SDK sources
  `public.sdk/source/common/memorystream.cpp`, `public.sdk/source/vst/hosting/hostclasses.cpp`,
  `public.sdk/source/vst/hosting/pluginterfacesupport.cpp`, `vstgui_test_stubs.cpp`,
  `public.sdk/source/main/moduleinit.cpp`, `public.sdk/source/main/pluginfactory.cpp`.
- `target_link_libraries(seraphis_tests PRIVATE KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk)`
  — **`sdk` after `vstgui_support`**, per the stub file's own comment.
- Include dirs: `../src`, `${CMAKE_CURRENT_SOURCE_DIR}`, `${CMAKE_SOURCE_DIR}/tests`,
  `${CMAKE_SOURCE_DIR}/tools`, `${vst3sdk_SOURCE_DIR}`, `${vst3sdk_SOURCE_DIR}/vstgui4`.
- `target_compile_features(seraphis_tests PRIVATE cxx_std_20)`.
- `target_compile_definitions(seraphis_tests PRIVATE SERAPHIS_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources")`
  (FR-063).
- FR-064 source properties under `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`:
  `COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` on `unit/lifecycle_test.cpp`,
  `integration/processor_audio_test.cpp`, `integration/param_flow_test.cpp`.
- `catch_discover_tests(seraphis_tests REPORTER console)` (FR-065).
- `vstgui_test_stubs.cpp`: `Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }`.
- `unit/test_main.cpp` (FR-061, FR-066a): `void* moduleHandle = nullptr;`,
  **`#include <allocation_operator_overrides.h>` — in exactly this ONE translation unit** (a second
  include in the same binary is a duplicate-symbol link error; a hand-rolled copy is caught by
  `tools/lint-allocation-operator-overrides.js`), `enableFTZDAZ();`,
  `return Catch::Session().run(argc, argv);`. **Without this include `TestHelpers::AllocationScope`
  counts nothing** and T025's liveness probe cannot pass.
- Remove any temporary `if(EXISTS …)` guard T013 put around `add_subdirectory(tests)`.

**Verify:**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests 2>&1 | tail -20
build/windows-x64-release/bin/Release/seraphis_tests.exe --list-tests
```
Builds warning-free and the exe runs (zero test cases at this point is expected and correct).

---

## GROUP 6 — Shared test fixture (NEW file; sequential, alone — every later test depends on it)

### T015 — `tests/seraphis_test_fixture.h`

**Create:** `plugins/seraphis/tests/seraphis_test_fixture.h`
**Edit:** `plugins/seraphis/tests/unit/lifecycle_test.cpp` (smoke case only)

**Test first:** add, in `tests/unit/lifecycle_test.cpp`, the case shell
`TEST_CASE("Seraphis_ProcessorLifecycle", "[seraphis][processor][lifecycle-proc]")` containing one
smoke `SECTION("fixture constructs and prepares")` that asserts
`REQUIRE(fixture.proc != nullptr)` and `REQUIRE(fixture.prepare(48000.0, 512) == Steinberg::kResultOk)`.
It fails to compile until the fixture exists — that is the failing state.

> **Tag rule (mandatory):** this case's tag is **`[lifecycle-proc]`, NOT `[lifecycle]`**.
> `.github/workflows/valgrind-nightly.yml:283–290` invokes each binary as `"$BINDIR/$bin" '[lifecycle]'`;
> giving a 4 s render + a 771 968 B engine that tag would drag them into a 60-minute-budget memcheck job.
> Only `Seraphis_EditorLifecycle` (T024) carries `[lifecycle]`.

**Implement (plan §4.2):** a `ProcessorFixture` struct providing:
- `std::unique_ptr<Seraphis::Processor> proc = std::make_unique<Seraphis::Processor>();` (FR-067 —
  heap; the `static_assert` in `processor.h` licenses a stack local if one is ever wanted);
- `prepare(double sampleRate, int32 blockSize)` → `initialize(nullptr)`, `setupProcessing`,
  `setActive(true)`;
- `renderBlocks(n, blockSize, script)` → fills separate L/R `std::vector<float>`s;
- `setParam(ParamID, double normalized)` → a **one-point** `IParameterChanges` stand-in;
- `setParamPoints(ParamID, std::initializer_list<double>)` → a **multi-point** queue. **Not optional:**
  FR-042's "*using the last value of each parameter queue*" is untestable with one-point queues —
  `getPoint(0)` and `getPoint(count-1)` are indistinguishable — and T020 asserts it;
- `pushEvent(kind, pitch, velocity, sampleOffset)` → an `IEventList` stand-in;
- `withOutputChannels(int n)` → a `ProcessData` whose `channelBuffers32` array has **exactly `n`
  elements** (so T018's mono clause produces a real out-of-bounds read under ASan);
- `seedOutputBuffers(float value)` → writes a non-zero canary into the output buffers before a call.
  **Mandatory:** without it "produces silence" assertions pass because the fixture zeroed its own
  vectors;
- guard-word canaries either side of each output buffer, checked by a `checkCanaries()` helper.

Back it with the SDK hosting helpers already linked (`hostclasses.cpp`, `pluginterfacesupport.cpp`) and
`MemoryStream` (`public.sdk/source/common/memorystream.cpp`) for state.

**Verify:** `--target seraphis_tests` builds; `seraphis_tests.exe "Seraphis_ProcessorLifecycle"` passes.

---

## GROUP 7 — Buses (edits `processor.cpp`; sequential, alone)

### T016 — bus setup and `setBusArrangements` (FR-020, FR-021 → SC-011)

**Edit:** `plugins/seraphis/tests/unit/processor_bus_test.cpp`, `plugins/seraphis/src/processor/processor.cpp`

**Test first —** `TEST_CASE("Seraphis_ProcessorBusSetup", "[seraphis][processor][bus]")`, after
`proc->initialize(nullptr)`:
- `REQUIRE(proc->getBusCount(kAudio, kInput) == 0)`
- `REQUIRE(proc->getBusCount(kAudio, kOutput) == 1)`
- `REQUIRE(proc->getBusCount(kEvent, kInput) == 1)`
- `setBusArrangements` returns **`kResultFalse`** for: `numIns == 1` (any arrangement);
  **`numOuts == 0`**; **`numOuts == 2`** (FR-021 clause b — without it a host successfully negotiates an
  output bus that does not exist); and one output bus with `SpeakerArr::kMono`.
- `setBusArrangements` returns **`kResultTrue`** for `numIns == 0`, `numOuts == 1`,
  `outputs[0] == SpeakerArr::kStereo`.

Run it, confirm it **fails** (the skeleton has no buses).

**Implement:**
- `initialize()`: after `AudioEffect::initialize(context)` and before constructing the engine/reverb,
  `addEventInput(STR16("Event In"))` and `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)`.
  **MUST NOT call `addAudioInput()`** (model `plugins/membrum/src/processor/processor.cpp:117, 120`;
  anti-model `plugins/ruinae/src/processor/processor.cpp:56`).
- `setBusArrangements`: `if (numIns != 0) return kResultFalse;` `if (numOuts != 1) return kResultFalse;`
  `if (outputs == nullptr || outputs[0] != SpeakerArr::kStereo) return kResultFalse;` `return kResultTrue;`

**Verify:** `"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests` then
`build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_ProcessorBusSetup" 2>&1 | tail -5`
→ `All tests passed`.

---

## GROUP 8 — `setupProcessing`, latency (edits `processor.cpp`; sequential, alone)

### T017 — prepare-time wiring and the invariant 1024-sample latency (FR-023, FR-028, FR-033, FR-053 → SC-013)

**Edit:** `plugins/seraphis/tests/unit/lifecycle_test.cpp`, `plugins/seraphis/src/processor/processor.cpp`

**Test first —** add `SECTION("Seraphis_LatencyIsReported")` to `Seraphis_ProcessorLifecycle`, asserting
the **literal** number `1024` (not merely "equals `AetherReverb::getLatencySamples()`", which a config
that disabled spectral diffusion also satisfies at 0) at every point of one sequence:
1. `REQUIRE(proc->getLatencySamples() == 1024u)` on a freshly `initialize(nullptr)`d processor
   **before any `setupProcessing()`**;
2. `== 1024u` after each of four consecutive `setupProcessing()` calls at **44100, 48000, 96000,
   192000 Hz in that order** (a sample count does not scale with the rate; the ordering catches a
   stateful implementation);
3. `== 1024u` after `setActive(true)` and after `setActive(false)`;
4. `== 1024u` after a `setState()` carrying non-default values;
5. `== 1024u` after pushing **each** of the eight parameters to `0.0` and to `1.0`;
6. `== 1024u` after 100 `process()` calls.
Also `REQUIRE_NOTHROW` that `initialize(nullptr)` followed immediately by `setupProcessing()` does not
crash.

> **Do not implement `restartComponent(kLatencyChanged)`.** Plan §1.3 C-1/C-2: `AetherReverb` already
> reports 1024 *before* any prepare (`getLatencySamples()` returns
> `spectralEnabled_ ? diffusionFftSize_ : 0`, `aether_reverb.h:2607–2613`, with `diffusionFftSize_ = 1024`
> and `spectralEnabled_ = true` by default), so there is no transition to announce; and a
> `Steinberg::Vst::AudioEffect` has **no** route to an `IComponentHandler` — the handler is delivered
> only to the edit controller (`vsteditcontroller.h:59, 97, 108`). The `getHostContext()` substitute is
> **forbidden**: the `FUnknown*` a host passes to `IComponent::initialize` is an `IHostApplication`, the
> query is null in every real host **and** in the SDK hosting layer this test target links. If a later
> phase makes latency variable, add it via processor → `IMessage` → controller →
> `getComponentHandler()->restartComponent(kLatencyChanged)`. This is spec amendment A2 (T031b).

**Implement — `setupProcessing()` exactly in FR-023's order (plan §2.5.4):**
0. Null guard: `if (engine_ == nullptr || reverb_ == nullptr) return AudioEffect::setupProcessing(setup);`
   (leaves `prepared_` false; pluginval strictness 5 probes out-of-order lifecycle calls).
   **No MXCSR here** — it is per-thread and never reaches the audio thread
   (`plugins/membrum/src/processor/processor.cpp:1073–1075`).
1. `const std::size_t bound = kMaxBlockSamples;` — the **constant 2048**, never
   `setup.maxSamplesPerBlock`.
2. `const std::size_t poly = clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));`
   — seeded **from the parameter**, because `setState()` may legally precede `setupProcessing()`.
3. `engine_->prepare(sampleRate_, makeSeraphisEngineConfig(poly, kEngineSeed, bound));`
   `reverb_->prepare(sampleRate_, makeSeraphisReverbConfig(bound));`
4. `lastPushedPolyphony_ = engine_->getPolyphony();` and
   `lastPushedSoftLimit_ = globalParams_.softLimit.load(...)`, then
   `engine_->setOutputSaturation(lastPushedSoftLimit_ ? SeraphisEngine::kOutputSaturation : 0.0f)`.
   **Never reset the tracker to 8 and never to a force-push sentinel** — prepare already delivered that
   voice count, so the first `process()` must NOT re-call `setPolyphony`.
5. Scratch sized **once**, to the constant: `dryL_/dryR_/wetL_/wetR_.assign(bound, 0.0f)` (FR-028).
6. `masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sampleRate_));`
   `anySamplesSincePrepare_ = false;` `prepared_ = true;`
7. `return AudioEffect::setupProcessing(setup);`
- `getLatencySamples()` = `reverb_ ? static_cast<uint32>(reverb_->getLatencySamples()) : 0u;` — that is
  the whole of FR-033 in Phase 8.
- Add a **debug-only** assert that both components accepted the same sample rate
  (`AetherReverb::prepare` clamps into `[kMinSampleRate, kMaxSampleRate]`, `SeraphisEngine::prepare`
  only floors at 1.0 — R15).
- **Record the residual (plan §8.3) as a comment at the `setOutputSaturation` call site:** when
  `softLimit == false` at prepare, the push ramps rather than snaps, because `prepare()` re-applies
  `kOutputSaturation` and snaps the saturator's smoother (`seraphis_engine.h:225–231`) while the
  post-prepare setter takes the ramping branch (`tape_saturator.h:248–252`). Effect: the first
  `kDefaultSmoothingMs = 5.0f` of the render carries a decaying ≤ 0.15 blend. Removing it needs a
  `dsp/` change, which Phase 8 forbids. **Deferred to Phase 9 — do not silently accept it.**

**Verify:** `seraphis_tests.exe "Seraphis_ProcessorLifecycle" 2>&1 | tail -5` → all pass; zero warnings.

---

## GROUP 9 — `process()` guards, `setActive` (edits `processor.cpp`; sequential, alone)

### T018 — degenerate shapes, silence flags, `setActive` (FR-029, FR-030, FR-032 → SC-021, SC-026 part 1)

**Edit:** `plugins/seraphis/tests/unit/lifecycle_test.cpp`, `plugins/seraphis/src/processor/processor.cpp`

**Test first —** add these SECTIONs to `Seraphis_ProcessorLifecycle`:
- **Degenerate shapes.** Each of `data.numInputs == 0`, `data.numOutputs == 0`, `data.numSamples == 0`,
  `data.outputs[0].channelBuffers32 == nullptr` → `REQUIRE(process(data) == kResultOk)`, canary guard
  words intact (no sample written outside the provided buffers), and
  `engine_->getActiveVoiceCount()` (`seraphis_engine.h:668`) **unchanged**.
- **Mono-output clause.** A `ProcessData` built with `fixture.withOutputChannels(1)` — a **one-element**
  `channelBuffers32` array — returns `kResultOk`, never touches `channelBuffers32[1]`, and leaves
  `getActiveVoiceCount()` unchanged. (Under the ASan lane in T032 an out-of-bounds read here becomes a
  hard failure.)
- **Pre-`setupProcessing()` silence, with teeth.** Call `process()` before `setupProcessing()` with the
  output buffers **pre-seeded to `0.5f`** via `fixture.seedOutputBuffers(0.5f)`. `REQUIRE` the return is
  `kResultOk` **and every output sample is exactly `0.0f`**. Without the pre-seed this passes because the
  fixture zeroed its own vectors, and an implementation that returned without writing — handing the host
  back the previous plug-in's buffer content, which VST3 does not zero — goes undetected.
- **Silence flags, both directions.** A normal render (after prepare, with a held note) leaves
  `data.outputs[0].silenceFlags == 0`, asserted from a **pre-seeded `3`**. The not-ready path leaves
  `silenceFlags == 3`, asserted from a **pre-seeded `0`**. Neither can pass by the host happening to
  leave the field alone.
- **`setActive` lifecycle (SC-026 clause 1).** With a note held and the reverb ringing,
  `setActive(false)` → `setActive(true)` → one 512-sample `process()` → `REQUIRE(peak < 1.0e-6f)`.
- **Out-of-order lifecycle.** `setupProcessing()` on a processor whose `initialize()` never ran returns
  without crashing and leaves `getLatencySamples() == 0` (no `reverb_`); a subsequent `process()` with
  valid buffers returns `kResultOk` and produces silence.

**Implement — `process()`'s prologue, in exactly this order (the order is load-bearing twice over):**
```cpp
const Krate::DSP::ScopedDenormalMode denormalGuard;      // FR-029, at the TOP of process()
processParameterChanges(data.inputParameterChanges);     // body added by T020
if (data.numOutputs <= 0 || data.outputs == nullptr) return kResultOk;
if (data.outputs[0].channelBuffers32 == nullptr)     return kResultOk;
if (data.outputs[0].numChannels < 2)                 return kResultOk;   // mono guard
if (data.numSamples <= 0)                            return kResultOk;
float* outL = data.outputs[0].channelBuffers32[0];
float* outR = data.outputs[0].channelBuffers32[1];
if (outL == nullptr || outR == nullptr)              return kResultOk;
if (!prepared_ || !engine_ || !reverb_) {
    std::fill_n(outL, total, 0.0f); std::fill_n(outR, total, 0.0f);
    data.outputs[0].silenceFlags = 3;                // both channels ARE silent
    return kResultOk;
}
```
and at the end of a normal render `data.outputs[0].silenceFlags = 0;` (FR-024's silence-flag clause —
Seraphis writes only the **clearing** half; it never asserts silence, because deciding when the instance
is genuinely quiet needs a "reverb has decayed" predicate Phase 8 has no criterion for.
`getTailSamples()` stays at the SDK default; tail/idle reporting is **Phase 10**).
Buffer **validation precedes the readiness check** so the one degenerate case with a valid writable
buffer can be zero-filled; nothing reads `data.outputs[0]` before `numOutputs > 0` and
`outputs != nullptr` are established.

`setActive(TBool state)` (FR-032): on `true`, set `anySamplesSincePrepare_ = false` (re-arms the
master-gain snap) and **nothing else** — no allocation. On `false`, `engine_->silence()`
(`seraphis_engine.h:308`) and `reverb_->reset()` (`aether_reverb.h:1971`). `silence()` is documented as
**not** an audio-thread operation (~32 MiB of ring clearing) — correct here, `setActive` runs on the
host thread with the audio thread stopped.

**RT-safety audit (assert by inspection, measured in T025):** no `new`/`delete`, no `resize`/`assign`,
no lock, no `try`/`throw`, no I/O, no `std::function` in `process()`; scratch is indexed with `.data()`
and `[]`, never `.at()`.

**Verify:** `seraphis_tests.exe "Seraphis_ProcessorLifecycle" 2>&1 | tail -5` → all pass.

---

## GROUP 10 — State (edits `processor.cpp` + `controller.cpp`; sequential, alone)

### T019 — `getState` / `setState` / `setComponentState` (FR-041, FR-045, FR-046, FR-047 → SC-010)

**Edit:** `plugins/seraphis/tests/unit/state_roundtrip_test.cpp`,
`plugins/seraphis/src/processor/processor.cpp`, `plugins/seraphis/src/controller/controller.cpp`

**Test first —** `TEST_CASE("Seraphis_StateRoundTrip", "[seraphis][state]")`. The file includes **both**
`processor/processor.h` and `controller/controller.h` (they do not include each other — the VST3
separation holds); both TUs are compiled into `seraphis_tests` by T014.
1. **Byte stability.** Set all eight parameters to distinct non-default values; `getState` into
   `MemoryStream` A; construct a **fresh** `Processor`; `setState(A)`; `getState` into B.
   `REQUIRE(A == B)` **byte-for-byte**, `REQUIRE(A.size() == 36)`, version prefix at offset 0 `== 1`,
   and every parameter reads back equal to what was set.
2. **Default-state clause (FR-041).** `getState()` on a **freshly constructed** `Processor`, before any
   parameter change, streams `gravity == 0.5f` at **offset 28**. This is the assertion that catches a
   value-initialized `MacroParams`.
3. **Truncation.** For N ∈ {0, 4, 8, 12, 16, 20, 24, 28, 32}, feed the first N bytes: no crash, and
   every field beyond the cut equals its registered default.
4. **Version rejection.** A stream whose first `int32` is `2` ⇒ `setState` returns `kResultFalse`.
5. **Controller clause (FR-047) — mandatory.** Without it FR-047 has **no** detector: a
   `setComponentState()` that returns `kResultOk` without reading the stream, or that loads the two
   packs in the wrong order, passes every other criterion.
   - set all eight parameters on the processor to distinct non-default values, **including
     `kMacroGravityId` set away from 0.5** (e.g. normalized `0.8`), so a no-op loader that leaves
     registered defaults is caught;
   - `Processor::getState(&stream)`; `stream.seek(0, IBStream::kIBSeekSet, nullptr)`;
   - fresh `Seraphis::Controller`, `initialize(nullptr)`,
     `REQUIRE(controller.setComponentState(&stream) == kResultOk)`;
   - for each of the eight IDs `REQUIRE(controller.getParamNormalized(id) == Approx(expected))` with
     `expected` the **normalized** value pushed in step 1 (inverse mappings: `masterGain / 2.0`,
     `(polyphony - 1) / 15.0`, `softLimit ? 1.0 : 0.0`, macros as-is).

Run it, confirm it **fails**.

**Implement (stream layout is fixed — plan §3.4, little-endian `IBStreamer`, total 36 bytes):**

| Offset | Bytes | Field |
|---|---|---|
| 0 | 4 | `int32 kCurrentStateVersion` (== 1) |
| 4 | 4 | `float masterGain` ([0,2]) |
| 8 | 4 | `int32 polyphony` ([1,16]) |
| 12 | 4 | `int32 softLimit` (0/1) |
| 16–32 | 4 each | `float dream, bloom, dissolve, gravity, entropy` |

- `getState`: `IBStreamer streamer(state, kLittleEndian);` `writeInt32(kCurrentStateVersion);`
  `saveGlobalParams`; `saveMacroParams`.
- `setState`: null-check; read the version `int32`, `return kResultFalse` if the read fails or
  `version > kCurrentStateVersion`; then `loadGlobalParams` then `loadMacroParams` (EOF-safe — a short
  stream leaves defaults). **No `prepare()` is reachable from `setState()`**, and it writes only
  `std::atomic<>` members, so it is safe concurrently with `process()`.
- `Controller::setComponentState`: same order, via the `load*ParamsToController` template helpers.
- **No bit-exact float digest of any render.** A digest over this *serialized byte stream* is the
  sanctioned form; a digest over rendered audio is forbidden (FR-068,
  `tools/lint-float-bit-goldens.js`).

**Verify:** `seraphis_tests.exe "Seraphis_StateRoundTrip" 2>&1 | tail -5` → all pass.

---

## GROUP 11 — Parameter dispatch (edits `processor.cpp`; sequential, alone)

### T020 — `processParameterChanges` (FR-042, FR-043, FR-048 → SC-009)

**Edit:** `plugins/seraphis/tests/unit/param_denorm_test.cpp`, `plugins/seraphis/src/processor/processor.cpp`

**Test first —** `TEST_CASE("Seraphis_ParamDenormRoundTrip", "[seraphis][params]")`:
1. **Sweep.** For each of the eight IDs, push normalized `{0, 0.25, 0.5, 0.75, 1}` through
   `processParameterChanges` and read the atomic:
   - `masterGain` within `1e-6f` of `v * 2.0`;
   - `polyphony` **exactly** `clamp(int(v*15 + 1.5), 1, 16)` → `{1, 5, 9, 12, 16}`;
   - `softLimit` exactly `v >= 0.5`;
   - each macro within `1e-6f` of `v`.
2. **Last-point clause (FR-042) — mandatory.** With one-point queues, an implementation reading
   `getPoint(0, …)` is byte-for-byte indistinguishable from one reading `getPointCount()-1`, and no
   other criterion covers it (T021's block-size test deliberately contains no automation). Using
   `fixture.setParamPoints`, push a **3-point** queue for `kPolyphonyId` = `{0.0, 1.0, 0.4}` (→ 1, 16,
   **7** voices) and for `kMacroDreamId` = `{0.0, 1.0, 0.25}`, run **one** `processParameterChanges`,
   and `REQUIRE` each atomic holds the denormalization of the **last** point (`7` and `0.25f`). The
   last value is neither the first nor the maximum, so neither `getPoint(0)` nor a max-scan can pass.
3. **Controller clause.** `Controller::getParamNormalized(id)` after `setParamNormalized(id, v)` returns
   `v` **exactly, for every registered parameter including `kPolyphonyId`**: `Parameter::setNormalized`
   only clamps to `[0,1]`, and `StringListParameter` overrides `toString`/`fromString`/`toPlain`/
   `toNormalized` but **not** `setNormalized`. A criterion demanding "nearest step" would fail a correct
   implementation. Verify the quantization separately on the surfaces that *do* quantize:
   `StringListParameter::toPlain(v) == std::round(v * 15)` and the displayed string equals the expected
   voice count.

Run it, confirm it **fails**.

**Implement:** `processParameterChanges(IParameterChanges* changes) noexcept` — null-guard; for each
queue take **the last point** (`getPointCount() - 1`); dispatch by ID range: `id < kGlobalParamRangeEnd`
→ `handleGlobalParamChange(globalParams_, id, value)`;
`id < kMacroParamRangeEnd` → `handleMacroParamChange(macroParams_, id, value)`; else ignore. Call it at
the top of `process()` (already wired by T018).

**Verify:** `seraphis_tests.exe "Seraphis_ParamDenormRoundTrip" 2>&1 | tail -5` → all pass.

---

## GROUP 12 — Slice loop, events, master gain, bloom (edits `processor.cpp`; sequential, alone)

### T021 — `renderSlice` and the event-driven slice loop (FR-024, FR-025, FR-026, FR-027, FR-031, FR-034 → SC-008, SC-022)

**Edit:** `plugins/seraphis/tests/unit/midi_event_test.cpp`, `plugins/seraphis/src/processor/processor.cpp`

**Test first —** `TEST_CASE("Seraphis_MidiEventTranslation", "[seraphis][midi]")`, asserting on the
engine's own observable surface (`getActiveVoiceCount()` `seraphis_engine.h:668`, `getVoiceState(i)`
`:693`):
1. `kNoteOnEvent` with `velocity > 0` allocates **exactly one** voice.
2. `kNoteOnEvent` with `velocity == 0` releases the matching note **identically** to `kNoteOffEvent`
   (compare the resulting `VoiceState` of the two paths).
3. `kNoteOffEvent` for a note never played is a no-op — no state change, no crash.
4. `sampleOffset == -5` and `sampleOffset == numSamples + 10` are clamped into `[0, numSamples]` and
   never produce a negative slice length.
5. **Two events at the same `sampleOffset`** are **both** dispatched before the next render (no
   zero-length slice reaches `processStereoBlock`).
6. **Velocity floor (plan §1.3 C-3).** A `kNoteOnEvent` with velocity strictly inside `(0, 1/127)` —
   use `0.003f` — **allocates** a voice (`getActiveVoiceCount()` increments) rather than releasing one.
   A truncating `uint8(velocity * 127)` yields `0`, which `SeraphisEngine::noteOn` maps to `noteOff`
   (`seraphis_engine.h:374–377`). Also assert `velocity = 1.0f/127.0f` → 1 and `velocity = 1.0f` → **127**
   (not 128 — the upper clamp).
7. **`SECTION("Seraphis_ProcessorBlockSizeInvariance")`** — render one 4 s seeded script through
   `Processor::process()` at host block sizes **{1, 7, 64, 65, 512, 2048, 4096}**, with events placed at
   non-multiples of every partition, and compare each of {1, 7, 64, 65, 2048, 4096} against the
   **512-sample reference** (the reference is not compared with itself — that carries no information):
   - **Primary gate:** `REQUIRE(maxAbsDiff_L <= 1.0e-5f)` and `REQUIRE(maxAbsDiff_R <= 1.0e-5f)` —
     max absolute per-sample difference over **all** samples, per channel.
   - **Secondary, WARN-only:** `compareFingerprints(...)`. It **MUST NOT gate**: it samples only 32
     checkpoints (`render_fingerprint.h:46`) so it can miss a localized divergence, and its
     `kMetricTolerance = 1e-5` relative bound was measured for cross-toolchain spread of the *same*
     computation, not a re-partitioned one — as a sole gate it can both fail a correct implementation
     and pass a broken one.
   - **Required coverage, asserted not assumed:** `REQUIRE(65 % 64 != 0)` and that 65 is in the
     partition set, so a boundary provably falls **inside** a 64-sample control chunk
     (`kControlChunkSamples = 64`, `seraphis_engine.h:132`).
   - **4096 is mandatory** — the only partition above `kMaxBlockSamples = 2048` and therefore the only
     one entering FR-026's sub-division branch.
   - The script contains **no parameter automation**: VST3 delivers parameter queues per host block, so
     a re-partitioned automation lane is a different lane by construction.

Run it, confirm it **fails**.

**Implement — the slice loop in `process()` (plan §2.5.6) and `renderSlice` (plan §2.5.6 body):**
- Helper `clampOffset(Steinberg::int32 offset, std::size_t total)`: `offset <= 0 → 0`; else
  `min(size_t(offset), total)`.
- Before the loop: read the master-gain target once —
  `const float gainTarget = globalParams_.masterGain.load(std::memory_order_relaxed);` then
  `if (!anySamplesSincePrepare_) masterGain_.snapTo(gainTarget); else masterGain_.setTarget(gainTarget);`
  Hoisting is valid because `processParameterChanges` already took the **last** value of each queue, so
  the atomic cannot change within a `process()` call.
- Loop: `while (cursor < total)` →
  1. dispatch **every** event with `clampOffset(...) <= cursor` — a **`while`, not an `if`** (else the
     second event at the same offset resolves `sliceEnd` to `cursor` and a zero-length slice reaches
     `processStereoBlock`);
  2. `sliceEnd` = next event offset (if `> cursor`), else `total`; then
     `sliceEnd = std::min(sliceEnd, cursor + kMaxBlockSamples)` — the **only** slice bound, and the
     **same constant** both configs were prepared with, so the engine ceiling and the reverb ceiling
     cannot drift apart;
  3. `renderSlice(outL + cursor, outR + cursor, sliceEnd - cursor);` `cursor = sliceEnd;`
- Event translation (FR-031, with plan §1.3 C-3's correction):
  `kNoteOnEvent && velocity > 0.0f` → `engine_->noteOn(uint8(pitch), uint8(clamp(velocity*127.0f + 0.5f, 1.0f, 127.0f)))`;
  `kNoteOnEvent && velocity <= 0.0f` and `kNoteOffEvent` → `engine_->noteOff(uint8(pitch))`; anything
  else ignored. Pitch is range-guarded to `[0,127]` before the `uint8_t` cast (`noteOn.pitch` is
  `int16`). The velocity-0 path is written explicitly (redundant with the engine's own guard) so the
  test exercises the **wrapper**.
- `renderSlice(outL, outR, n) noexcept` — the six-step body, in order:
  1. `macros_.apply(*engine_);` and `applyAetherTargets(*reverb_, macros_.computeAetherTargets());`
     (FR-034: applied **every slice**, even at neutral defaults, because `computeAetherTargets()` is
     what pushes the reverb's eight controls);
  2. `engine_->processStereoBlock(dryL_.data(), dryR_.data(), n);`
  3. `reverb_->processStereoBlock(dryL_.data(), dryR_.data(), wetL_.data(), wetR_.data(), n);`
  4. **master gain, per sample**: `for (s < n) { const float g = masterGain_.process(); wetL_[s] *= g; wetR_[s] *= g; }`
     — **once per output sample**, never `advanceSamples(n)` and never once per slice. A per-slice ramp
     is partition-dependent by construction and fails the block-size gate for a *correct*
     implementation;
  5. `engine_->processOutputStage(wetL_.data(), wetR_.data(), n);` **in place on the reverb return** —
     the output stage (saturator → `TruePeakLimiter`) is **always last**. A post-limiter multiply is
     **forbidden**: at master gain 2.0 it produces peaks up to ~1.78 and makes the ceiling bound
     unsatisfiable by construction;
  6. `std::copy_n` into `outL`/`outR`; then `const auto bloom = engine_->consumeBloomEvents();` and,
     **note-offs before note-ons**, `reverb_->bloomNoteOff(v)` for every set `noteOffMask` bit, then for
     every set `noteOnMask` bit `engine_->collectHeldPartials(v, bloomPartials_.data(), bloomPartials_.size(), count)`
     followed by `reverb_->bloomNoteOn(v, bloomPartials_.data(), count)` when `count > 0`.
- Set `anySamplesSincePrepare_ = true` after the loop.
- **FR-027:** do **not** mirror `processOutputStage`'s internal 64-sample loop — the engine's banner
  (`seraphis_engine.h:506–511`) states it is a cadence choice, not a size constraint.

**Verify:** `seraphis_tests.exe "Seraphis_MidiEventTranslation" 2>&1 | tail -5` → all pass; zero warnings.

---

## GROUP 13 — Global-parameter push (edits `processor.cpp`; sequential, alone)

### T022 — `pushGlobalParams` + the param-flow integration test (FR-024a → SC-019, SC-027)

**Edit:** `plugins/seraphis/tests/integration/param_flow_test.cpp`,
`plugins/seraphis/src/processor/processor.cpp`

**Test first —** `TEST_CASE("Seraphis_ParamFlowReachesEngine", "[seraphis][integration]")`. Without this
case, an implementation whose `masterGain` atomic is written and never multiplied in, and whose
`polyphony` atomic never reaches `setPolyphony`, satisfies **every other** criterion in this phase.
1. **Master gain silences.** At `kMasterGainId` normalized `0.0`, a 4 s render's absolute peak is
   `< 1.0e-6f` — **over the whole render, from sample 0**, with no "after the first N ms" allowance.
   Satisfiable only because of the snap; a ramped-from-default implementation fails by design.
2. **Master gain scales.** `peak(norm 1.0) / peak(norm 0.5) == 2.0 ± 5 %`, measured at a level where the
   limiter does **not** engage (otherwise the ratio is compressed). If it engages, reduce the render
   level and record the level used in `compliance.md`.
3. **Polyphony reaches the engine.** All four sub-clauses required:
   - *Seeded at prepare:* `setState()` carrying polyphony **4**, **then** `setupProcessing()` ⇒
     `engine_->getPolyphony() == 4` **before any `process()` call** (not the struct default 8).
   - *Tracked at prepare:* the **first** `process()` after that prepare leaves
     `setPolyphonyCallCountForTest()` unchanged.
   - *Pushed on change:* after pushing `kPolyphonyId = v` and one `process()`,
     `getPolyphony() == clamp(int(v*15 + 1.5), 1, 16)` for `v ∈ {0, 0.25, 0.5, 0.75, 1}`; **re-pushing
     the same value leaves the call count unchanged**.
   - *Corrupt stream converges:* build a state stream by hand with polyphony `0`, and another with
     `20`; `setState` each, then run **two consecutive** `process()` calls and
     `REQUIRE` `setPolyphonyCallCountForTest()` increased by **at most 1** in total, and
     `getPolyphony() ∈ [1, 16]` after each. Without clamping at the single conversion point the
     comparison against the engine's clamped `getPolyphony()` never converges and the count increments
     on **every block forever** — re-arming `sumGain_` (`seraphis_engine.h:349`) and walking the
     excess-slot loop (`:339–348`) per block.
4. **`SECTION("Seraphis_SoftLimitIsMeasurable")` (SC-027).** Same seeded script rendered twice,
   `kSoftLimitId` **on** (`setOutputSaturation(0.15f)`) vs **off** (`0.0f`), at a level where the
   saturator is engaged:
   - **non-vacuity first:** `REQUIRE(maxAbsDiff(on, off) > kSampleTolerance)` (`1.0e-4f`,
     `tests/test_helpers/render_fingerprint.h:49`);
   - then the relative RMS difference exceeds a figure **measured during implementation** and written
     into the test as a **named constant with a provenance comment** (project rule: measured, never
     guessed).
   The pre-output-stage level used MUST be stated in the test and recorded in `compliance.md`. Phase 7
   measured the composed chain at ~−30 dBFS RMS, where a 0.15 saturation amount is **not** obviously
   above `kSampleTolerance` — drive the level up (more voices / higher master gain) until clause 1
   passes. **If no reachable level produces a measurable difference, record FR-044 in `compliance.md`
   as verified by code inspection only, listing the measured deltas at each level tried. Never silently
   mark it verified.**

Run it, confirm it **fails**.

**Implement — `pushGlobalParams() noexcept`, called as FR-024 **step 0**, once per `process()` call,
immediately after the readiness guards and before the slice loop:**
```cpp
const std::size_t poly = clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
if (poly != lastPushedPolyphony_) {                  // ON CHANGE ONLY
    engine_->setPolyphony(poly);
    lastPushedPolyphony_ = engine_->getPolyphony();   // re-read post-clamp
    ++setPolyphonyCalls_;
}
const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
if (soft != lastPushedSoftLimit_) {                  // ON CHANGE ONLY
    engine_->setOutputSaturation(soft ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);
    lastPushedSoftLimit_ = soft;
}
```
`clampPolyphony` (T008) is **mandatory here, not decorative** — the comparison is against the engine's
clamped `getPolyphony()`, so both sides must live in the same domain or the detector never converges.
`setPolyphonyCalls_` is a plain `std::size_t` written only from the audio thread and read only from the
test thread after the render completes; no atomic needed.

**Verify:** `seraphis_tests.exe "Seraphis_ParamFlowReachesEngine" 2>&1 | tail -5` → all pass.

---

## GROUP 14 — Integration and controller tests (NEW test-file content only, parallel)

Both tasks edit **different** test files and touch no plugin source.

### T023 [P] — audio integration: non-silence, ceiling, inert macros, Aether targets (SC-005, SC-006, SC-023, SC-024)

**Edit:** `plugins/seraphis/tests/integration/processor_audio_test.cpp`

**Test first —** `TEST_CASE("Seraphis_ProcessorRendersHeldNote", "[seraphis][integration]")`:
1. **SC-005 — non-silence.** Prepare 48 kHz / 512-sample blocks, `setActive(true)`, NoteOn(60, 100) at
   sample 0, render 4 s (375 blocks). `REQUIRE(peak >= 1.0e-3f)` (> −60 dBFS) **and** every sample
   finite via a **bit-pattern** check `((bits & 0x7F800000u) != 0x7F800000u)` — never `std::isnan`
   (the macOS leg is `-ffast-math`).
2. **SC-006 — ceiling, three clauses. The master gain differs per clause and that is load-bearing:**

   | Clause | Render(s) | `kMasterGainId` normalized | Linear gain |
   |---|---|---|---|
   | 1 | processor only | **1.0** | 2.0 |
   | 2 | processor **vs** `renderSeraphisChain` | **0.5** | 1.0 |
   | 3 | processor, chain-with-step-5, hand-rolled-without-step-5 | **0.5** | 1.0 |

   - **Clause 1 (bound), on the gain-2.0 render**, 16 voices all held, 4 s:
     `REQUIRE(peak <= 0.8912509f * std::pow(10.0f, 0.1f/20.0f))` — the `TruePeakLimiter` ceiling
     (`true_peak_limiter.h:46, 168`) plus Phase 7 SC-015's **0.1 dB** allowance.
   - **Clause 2 (positive control), at normalized 0.5:** render the same script/seed/blocksize through
     `Krate::DSP::TestUtils::renderSeraphisChain` (`tests/test_helpers/seraphis_chain.h`) and
     `REQUIRE(compareFingerprints(fingerprintRender(a), fingerprintRender(b)).withinTolerance())`.
     **It must run at normalized 0.5**: `renderSeraphisChain` applies **no** gain, so at gain 2.0 the
     processor drives the output-stage nonlinearity and limiter twice as hard and the comparison would
     fail a *correct* implementation. At normalized 0.5 the denormalization is exactly `1.0f` and the
     snap makes step 4b an IEEE-754 identity multiply — the comparison is bit-identical.
   - **Clause 3 (negative control) with mandatory non-vacuity, at normalized 0.5:** a hand-rolled
     engine+reverb loop that **omits step 5** (`renderSeraphisChain` has no skip flag), then in this
     order: `REQUIRE(maxAbsDiff(withStep5, withoutStep5) > kSampleTolerance)` **first**, then
     `REQUIRE(maxAbsDiff(processorRender, withoutStep5) > kSampleTolerance)`.
     Clause 1 alone has **no** discriminating power — Phase 7 measured the composed chain's worst case
     at peak `0.128337` (16.8 dB below the ceiling), so even at gain 2.0 a processor that dropped step 5
     passes it. **If the non-vacuity assertion fails, escalate the scenario (more voices, higher
     pre-output level) until the output stage is measurable and record the level in `compliance.md` —
     do not drop the clause.**
3. **SC-023 — macros inert (the Phase 9 negative control).** `SECTION`: a 4 s render with all five
   macro parameters at `0.0` and one with all five at `1.0`, same seed and script, are
   **fingerprint-identical** on both channels (`compareFingerprints(...).withinTolerance()`), with a
   non-vacuity `REQUIRE(rms > 1.0e-4f)` on the first render so the comparison is not between two
   silences. **Phase 9 must invert this test** — say so in a comment.
4. **SC-024 — the eight Aether targets are pushed.** `SECTION` exercising `applyAetherTargets()`
   **directly with non-neutral values** (a render diff at Phase 8's neutral macro defaults is provably
   vacuous: all eight `computeAetherTargets()` values equal the reverb's own constructor defaults
   exactly, and `AetherReverb` exposes **no getter** for any of them):
   - `size`: `applyAetherTargets` with `size = 0.9` vs `0.1`, then enough blocks for the 300 ms size
     smoother to settle ⇒ `AetherReverb::getEffectiveDelayLengthSamples(0)` **differs**;
   - `mix`: a render with `mix = 1.0` differs from one with `mix = 0.0` by more than `kSampleTolerance`
     (max absolute per-sample), with the same non-vacuity guard;
   - that `process()` calls it **every slice** is carried by clause 2's positive control (whose
     reference render pushes the same eight values per slice) — sound only because clause 2 runs at
     normalized master gain 0.5.

**Implement:** nothing in plugin source — this task is pure verification of T021/T022's implementation
plus T007's `applyAetherTargets`. If a clause fails, the defect is in the implementation, not the test.

**Verify:** `seraphis_tests.exe "Seraphis_ProcessorRendersHeldNote" 2>&1 | tail -5` → all pass.

---

### T024 [P] — editor lifecycle, bound controls, live preset config (SC-012 clauses 1/2/2b/2c)

**Edit:** `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp`

**Test first —** `TEST_CASE("Seraphis_EditorLifecycle", "[seraphis][controller][ui][lifecycle]")`.
**The `[lifecycle]` tag is mandatory**: `.github/workflows/valgrind-nightly.yml:283–290` invokes each
binary as `"$BINDIR/$bin" '[lifecycle]'`, so without it the nightly lane selects **zero** Seraphis tests
and the valgrind clause is satisfied vacuously (or the job fails on no-tests-matched).
1. **Harness.** `Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc")`
   (`tests/test_helpers/editor_lifecycle_harness.h:102–105`) — 3 open/close cycles, no crash. The
   harness itself `CHECK`s `attached() == kResultTrue` and `REQUIRE`s `getFrame()->getNbViews() > 0`
   (`:120–128`).
2. **`SECTION` — bound-control clause (FR-054), mandatory.** The harness alone verifies nothing about
   the `.uidesc` contents: a template holding a single `CTextLabel` satisfies all three of its
   assertions, and `VST3Editor` binds a mismatched control to a parameter with **no error path**. In
   this section build an editor directly (the harness owns and destroys its own instances):
   ```cpp
   Krate::TestSupport::ensureVstguiInitialized();
   auto* editor = new VSTGUI::VST3Editor(&controller, "editor",
                        (std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc").c_str());
   Steinberg::IPlugView* view = editor;
   REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
   std::vector<VSTGUI::CControl*> controls;
   collectControls(editor->getFrame(), controls);      // local recursive helper, not shared infra
   std::set<int32_t> tags; for (auto* c : controls) tags.insert(c->getTag());
   REQUIRE(controls.size() == 8u);
   REQUIRE(tags == std::set<int32_t>{0, 1, 2, 100, 101, 102, 103, 104});
   REQUIRE(dynamic_cast<VSTGUI::COptionMenu*>(controlWithTag(controls, 1)) != nullptr);
   REQUIRE(dynamic_cast<VSTGUI::CCheckBox*>(controlWithTag(controls, 2))  != nullptr);
   view->removed(); view->release();
   ```
   **Never a platform-type literal** — always `Krate::TestSupport::nativePlatformType()`
   (`editor_lifecycle_harness.h:87`); `tools/lint-platform-type-literals.js` enforces this.
3. **`SECTION("Seraphis_PresetConfigIsLive")` (FR-050, FR-051), mandatory.** Instantiating a
   `PresetManager` **scans nothing** — its constructor only stores `config_`/`processor_`/`controller_`
   and the two path overrides; all enumeration is in `scanPresets()`, which nothing in Phase 8 calls. So
   without this section FR-050/FR-051 have no detector at all:
   ```cpp
   const auto cfg = Seraphis::makeSeraphisPresetConfig();
   REQUIRE(cfg.pluginName == "Seraphis");
   REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Textures"});
   REQUIRE(cfg.processorUID == Seraphis::kProcessorUID);
   const auto presetsDir = std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
   REQUIRE(std::filesystem::is_directory(presetsDir / "Textures"));   // FR-051: both halves agree
   Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, presetsDir, presetsDir);  // overrides stay in-repo
   REQUIRE(pm.scanPresets().empty());                                  // Phase 8 ships no .vstpreset
   REQUIRE(pm.getConfig().pluginName == "Seraphis");
   Krate::Plugins::PresetManager def(cfg, nullptr, nullptr);
   REQUIRE(toLower(def.getFactoryPresetDirectory().filename().string()) == "seraphis");  // Linux lowercases the leaf
   ```
4. **Tag non-vacuity check (SC-012 clause 1), run as a shell check and recorded in `compliance.md`:**
   `seraphis_tests.exe "[lifecycle]" 2>&1 | tail -5` selects **≥ 1** test case.

**Implement:** nothing in plugin source (T012 and T005 already provide it). A failure here is a defect
in `controller.cpp` or `editor.uidesc`.

**Verify:**
```bash
build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_EditorLifecycle" 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "[lifecycle]" 2>&1 | tail -5   # >= 1 case
```

---

## GROUP 15 — Allocation and performance measurements (each edits an already-owned test file)

### T025 — zero-allocation in `process()` and on `setActive(true)` (SC-007, SC-026 clause 2)

**Edit:** `plugins/seraphis/tests/unit/lifecycle_test.cpp`

**Test first —** two SECTIONs of `Seraphis_ProcessorLifecycle`.

**The reading form is normative, because the obvious one measures nothing.**
`AllocationScope::getAllocationCount()` returns the member `count_`
(`tests/test_helpers/allocation_detector.h:85–87`), and `count_` is assigned **only** in
`~AllocationScope()` (`:81–83`) — so `REQUIRE(scope.getAllocationCount() == 0)` written *inside* the
scope passes unconditionally. (That is exactly the form the in-repo model
`plugins/membrum/tests/unit/test_allocation_matrix.cpp:129–135` uses; do **not** copy it.)

- **`SECTION("Seraphis_ProcessorNoAllocInProcess")`** — after `setupProcessing` + `setActive(true)`,
  render **200 blocks of 512 samples** with NoteOn/NoteOff traffic and a parameter sweep:
  ```cpp
  std::size_t allocations = 0;
  {
      TestHelpers::AllocationScope scope;
      fixture.renderBlocks(200, 512, script);
      allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();  // :45-47, live atomic
  }
  REQUIRE(allocations == 0u);
  ```
- **Liveness probe, identical form, in a SEPARATE (never nested) scope** — a nested `AllocationScope`
  ctor calls `startTracking()` which *resets* the outer count (`:31–34`), and its dtor calls
  `stopTracking()` which switches tracking off for the outer scope too (`:37–40`):
  ```cpp
  std::size_t probe = 0;
  { TestHelpers::AllocationScope scope;
    auto* deliberate = new int(7);
    probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
    delete deliberate; }
  REQUIRE(probe >= 1u);
  ```
  The probe can only pass because `unit/test_main.cpp` includes `<allocation_operator_overrides.h>`
  (T014 / FR-066a) — `recordAllocation()` (`:53–57`) fires only from those global replacements.
- **`SECTION` for SC-026 clause 2** — `setActive(true)` performs **exactly 0** allocations, measured in
  the identical normative form, with its own liveness probe.

**Implement:** nothing new — if a count is non-zero, fix the allocation in `process()`/`setActive`.

**Verify:** `seraphis_tests.exe "Seraphis_ProcessorLifecycle" 2>&1 | tail -5` → all pass.

---

### T026 — wrapper-overhead measurement, NON-GATING (SC-014)

**Edit:** `plugins/seraphis/tests/integration/processor_audio_test.cpp`

**Test first —** `TEST_CASE("Seraphis_ProcessorCpuOverhead", "[.perf][seraphis]")`. The `[.perf]` tag
hides it from the default run, so it does **not** affect the suite-green criterion.

**This criterion does not fail the phase.** Roadmap Phase 8 defines no CPU criterion, and the `[.perf]`
lane's idle-vs-loaded spread is ~33 % — larger than any ratio threshold would be. The result is
**recorded in `compliance.md`**, not gated.

**Protocol, pinned so the recorded number means something:**
- identical 4 s scenario on both arms — polyphony 8, one held note, 512-sample blocks, 48 kHz, same seed;
- **best-of-16 × 100 blocks** per arm with a **discarded warm-up trial**;
- the two arms **interleaved in the same process**, not run back to back;
- **buffer allocation hoisted out of the chain arm's timed region.** `renderSeraphisChain` allocates and
  zero-fills ~1.5 MB per call (`outL.assign` / `outR.assign`, the `eventAt` vector, four `blockSize`
  vectors), all charged to the denominator, which biases the ratio in the wrapper's favour. Hoist those
  vectors (or time an equivalent hand-rolled loop) so both arms do the same work;
- lane: executed explicitly by `seraphis_tests.exe "[.perf]"` on the machine that records the figure.

**Report:** `processor_ns / chain_ns` with the machine-idle caveat. A ratio above ~1.15 is a **flag to
investigate**, not a failure.

**Verify:** `build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20`;
record the ratio in `compliance.md` (T033).

---

## GROUP 16 — Roster lint (NEW file + registration in `guard-ci-gates.js`; sequential, alone)

### T027 — `tools/lint-plugin-roster.js` (FR-081 → SC-025)

**Create:** `tools/lint-plugin-roster.js`
**Edit (shared):** `tools/hooks/guard-ci-gates.js` — the `LINTS` array (currently **eight** entries at
`:38–47`, verified this session: `lint-layers`, `lint-odr`, `lint-arch-guarded-includes`,
`lint-float-bit-goldens`, `lint-midi-timing-goldens`, `lint-platform-type-literals`,
`lint-allocation-operator-overrides`, `lint-simd-aligned-loadstore`).

**This task is the "failing test" for T028.** Write the lint first; it MUST exit non-zero on the current
tree (Seraphis is in no roster, and four pre-existing drift sites exist). T028 makes it green.

**Implement (Node only — project rule: helper scripts are Node, never Python), plan §5.7:**
- Enumerate `fs.readdirSync('plugins', {withFileTypes:true})`, keep directories, drop `shared` — the
  same discovery `tools/gen-repo-map.js` uses, so the roster is derived from the filesystem and cannot
  itself go stale.
- For each plugin `p`, assert presence of a required token in a required **region** of each roster file:

  | Roster | Required token(s) | Region |
  |---|---|---|
  | `CMakeLists.txt` | `add_subdirectory(plugins/${p})` | whole file |
  | `.github/workflows/ci.yml` | `${p}:` in the `detect-changes.outputs` block; `${p}:` in `filters:`; `\b${p}\b` in the `for p in …` line; `${p}=` in the `$GITHUB_OUTPUT` block; `plugins/${p}/CMakeLists.txt` in **each** of the three `hashFiles(` lists; `"${p}:` in **each** of the nine `for plugin_info in \` blocks **and** `${p})` in the `case` block that follows each | per-block, located by anchor regex |
  | `.github/workflows/release.yml` | `- ${p}` in `options:`; `plugins/${p}/CMakeLists.txt` in the `hashFiles(` list | whole file |
  | `.github/workflows/valgrind-nightly.yml` | `${p}_tests` in the build-target line **and** in the `for bin in …` line | whole file |
  | `tools/run-clang-tidy.ps1` | `"${p}"` in `ValidateSet`; a `"${p}" {` case; `plugins/${p}/src` in the `"all"` case | per-block |
  | `tools/run-clang-tidy.sh` | `${p})` case; `plugins/${p}/src` in the `all)` case; `${p}` in the usage text | per-block |
  | `tools/check-changelog-coverage.js` | `'${p}'` in the `PLUGINS` array (currently `['iterum','disrumpo','ruinae','innexus','gradus','membrum']`, `:50`) | array literal |

- Exit **0** on full coverage; exit **1** with **one line per missing `(plugin, file, site)`** otherwise.
- Register it as a **ninth** entry in `guard-ci-gates.js`'s `LINTS`:
  `{ script: 'lint-plugin-roster.js', what: 'plugin missing from a CI/tooling roster' }`.

**Why this is a requirement and not a nicety:** every roster is a static literal, and a missing entry
produces a **green** run — the CI test list is built from a literal `for plugin_info in …` with a
matching `case`, and the bundle-validation loop additionally does `[ -d "$b" ] && BUNDLES+=("$b")`,
silently dropping even a listed-but-unbuilt bundle. A Seraphis entry missing from any roster yields a
passing CI in which Seraphis is never tested or validated. **This lint is the only artefact that can
fail on FR-070…FR-077.**

**Verify:** `node tools/lint-plugin-roster.js` exits **non-zero** and names, at minimum:
`seraphis` (every roster), plus the four **pre-existing drift** sites verified this session —
`disrumpo` missing from `.github/workflows/ci.yml:234`, `:466`, `:858` (the three FetchContent cache
keys) and `membrum` missing from `.github/workflows/release.yml:137`.

---

## GROUP 17 — External roster registration (edits many SHARED files; sequential, alone)

### T028 — all eighteen roster sites + the four drift fixes (FR-070…FR-077, FR-079, FR-080 → SC-025)

**Edit (shared):** `.github/workflows/ci.yml`, `.github/workflows/release.yml`,
`.github/workflows/valgrind-nightly.yml`, `tools/run-clang-tidy.ps1`, `tools/run-clang-tidy.sh`,
`tools/check-changelog-coverage.js`, `tools/gen-specs-index.js`, root `CLAUDE.md`

**Failing test:** T027's lint, currently red. This task makes it green **without weakening it**.

**Implement — the site table (plan §5.5; line numbers as verified in the plan):**

| # | File | Site | Edit |
|---|---|---|---|
| 1 | `ci.yml` | `detect-changes` outputs (`:59`) | `seraphis: ${{ steps.set-outputs.outputs.seraphis }}` |
| 2 | `ci.yml` | `paths-filter` block (`:85–86`) | `seraphis:` → `'plugins/seraphis/**'` |
| 3 | `ci.yml` | `for p in iterum disrumpo ruinae innexus gradus membrum` (`:104`) | append `seraphis` |
| 4 | `ci.yml` | `$GITHUB_OUTPUT` echo block (`:120`) | add the `seraphis=` echo |
| 5 | `ci.yml` | three FetchContent `hashFiles(...)` lists — Windows `:234`, macOS `:466`, Linux `:858` | add `'plugins/seraphis/CMakeLists.txt'` to each |
| 6 | `ci.yml` | build matrices `:266 / :506 / :892` + `case` dispatch `:274 / :514 / :900` | add `"seraphis:Seraphis:seraphis_tests"` + the `seraphis)` case |
| 7 | `ci.yml` | test matrices `:315 / :561 / :938` + `case` `:323 / :569 / :946` | add `"seraphis:seraphis_tests.exe"` (`.exe` **only** on the Windows leg) + case |
| 8 | `ci.yml` | bundle-validate lists `:364 / :610 / :987` + `case` `:372 / :618 / :995` | add `"seraphis:Seraphis"` + case |
| 9 | `ci.yml` | artifact upload — Windows `:430–435`, macOS `:811–819`, Linux `:1058–1063` | add a `Seraphis-<OS>-x64` step with the same `if:` condition shape |
| 10 | `ci.yml` | macOS `auval` step (model `:691–697`) | `auval -v aumu Srph KrAt` — **this is SC-004's only measurement surface** |
| 11 | `ci.yml` | macOS AUv3 verify step (model `:743–749`) | `"$APP" = build/bin/$BUILD_TYPE/Seraphis AUv3.app` |
| 12 | `release.yml` | `workflow_dispatch` choice list (`:40`) | add `- seraphis` |
| 13 | `release.yml` | FetchContent `hashFiles(...)` (`:137`) | add `'plugins/seraphis/CMakeLists.txt'` **and** `'plugins/membrum/CMakeLists.txt'` (drift fix) |
| 14 | `valgrind-nightly.yml` | build target list (`:276`) and run list (`:283`) | add `seraphis_tests` to both. **Do not** duplicate the Membrum-specific sharded job (`:123–192`) |
| 15 | `run-clang-tidy.ps1` | `ValidateSet` (`:60`), per-plugin `case` (model `:190–193`), `all` case (`:203–212`) | add `"seraphis"` + `plugins/seraphis/src` to source **and** include dirs |
| 16 | `run-clang-tidy.sh` | `seraphis)` case (model `:148–149`), `all)` (`:161–163`), usage text (`:63`) | `SOURCE_DIRS=("plugins/seraphis/src" "plugins/seraphis/tests")`. **Both scripts are required** — the Linux/macOS pre-commit lint silently skips a plugin missing from the `.sh` one |
| 17 | `check-changelog-coverage.js` | `PLUGINS` array (`:50`) | add `'seraphis'` |
| 18 | `gen-specs-index.js` | `SUBSYSTEMS` (`:19–33`) | add `['seraphis', 'Seraphis']` **before** `spectral`, `filter`, `oscillat`, `grain`, `dsp`. Placement is load-bearing: `:18` says *"first keyword found in the spec's slug wins; order matters"* — without it `seraphis-phase3-spectral-morph` classifies as "DSP / Spectral" and `seraphis-phase1-life-modulators` as "Other" |
| 19 | root `CLAUDE.md` | roster prose, pluginval command table, build/test target table, clang-tidy target list, Quick Reference rows (add Seraphis parameter / test / UI rows) | add Seraphis everywhere the six existing plugins appear |
| 20 | `docs.yml` | — | **NO EDIT** (FR-080). Its per-plugin loop (`:41`) globs `plugins/*/` and reports `has_page: false` harmlessly; the root landing page (`:133–158`) skips a plugin with no `seraphis/v*` tag (`:141–148`), which first exists in Phase 12. Record the verification; change nothing |

**Also fix the four pre-existing drift sites in this same change** (each is a one-line addition to a
cache key — cache granularity only, zero build risk; leaving them keeps T027's lint red for reasons
unrelated to Seraphis):
- `disrumpo` → `.github/workflows/ci.yml:234`, `:466`, `:858`
- `membrum` → `.github/workflows/release.yml:137`

**Do not weaken the lint to make it pass, and do not add an allow-list entry.**

**Verify:**
```bash
node tools/lint-plugin-roster.js                     # MUST exit 0
# SC-025 liveness probe (mandatory): a lint that cannot be shown to fail is not evidence.
# Temporarily delete 'seraphis' from tools/check-changelog-coverage.js's PLUGINS array:
node tools/lint-plugin-roster.js                     # MUST exit non-zero
# then restore the entry and re-run:
node tools/lint-plugin-roster.js                     # MUST exit 0
```

---

## GROUP 18 — Generated artifacts (edits SHARED generated files; sequential, alone)

### T029 — regenerate `repo-map.json` and `INDEX.md`, run all nine lints (FR-078 → SC-017)

**Edit (generated, committed):** `specs/_architecture_/repo-map.json`, `specs/INDEX.md`

**Failing test:** the `--check` modes below, currently red now that `plugins/seraphis/` exists and
T028 changed `gen-specs-index.js`'s `SUBSYSTEMS`.

**Implement:** run the generators (not by hand-editing the artifacts):
- `node tools/gen-repo-map.js` — auto-discovers `plugins/` (every directory except `shared`) and picks
  up `seraphis_tests` from `plugins/seraphis/tests/CMakeLists.txt`'s `add_executable`;
- `node tools/gen-specs-index.js` — also **re-classifies the eight existing Seraphis phase specs**
  under the new `seraphis` keyword;
- `specs/_architecture_/symbols.json` is **unaffected** — `tools/gen-symbols.js` scans only
  `dsp/include`, and Phase 8 touches no `dsp/` file. Run its `--check` anyway to prove it.

**Verify:**
```bash
node tools/gen-repo-map.js --check      # exit 0
node tools/gen-specs-index.js --check   # exit 0
node tools/gen-symbols.js --check       # exit 0
# all NINE lints (the eight at tools/hooks/guard-ci-gates.js:38-47 plus lint-plugin-roster.js):
for l in lint-layers lint-odr lint-arch-guarded-includes lint-float-bit-goldens \
         lint-midi-timing-goldens lint-platform-type-literals \
         lint-allocation-operator-overrides lint-simd-aligned-loadstore lint-plugin-roster; do
  node tools/$l.js || echo "FAILED: $l"
done
```

---

## GROUP 19 — Spec amendments (edits SHARED `spec.md`; sequential, alone)

### T030 — apply the ten spec amendments (plan §8.2, A1–A10)

**Edit:** `specs/seraphis-phase8-plugin-scaffold/spec.md`

**Why this is a task and not bookkeeping:** the plan's §8.2 lists ten places where the spec's text is
**factually wrong or unverifiable**. Without these edits a compliance table filled honestly (T033) would
have to mark the listed FR/SC as **not met** even though the shipped behaviour is strictly stronger.
**None of these relaxes a threshold.**

| # | Spec site | Amendment |
|---|---|---|
| A1 | FR-008 file list (`spec.md:307–339`) | Add `tests/seraphis_test_fixture.h` and the `.gitkeep` entries under `docs/`, `src/ui/`, `resources/presets/Textures/`. FR-008 says the skeleton is "**exactly** the file list below" |
| A2 | FR-023 clause 4, FR-033, SC-013 clause 4 | Delete the `restartComponent(kLatencyChanged)` requirement; state that Phase 8's latency is the **invariant 1024** and that hosts read `getLatencySamples()` after `setupProcessing()`. Restate SC-013 clause 4 as T017's invariance matrix. Record that a future variable latency must use processor → `IMessage` → controller → `getComponentHandler()->restartComponent`, **never** a `getHostContext()` query |
| A3 | FR-031 (`spec.md:583–586`) | Amend `noteOn(pitch, velocity*127)` to `noteOn(pitch, uint8(clamp(velocity*127 + 0.5, 1, 127)))`, citing `seraphis_engine.h:374–377` (velocity 0 → `noteOff`). Add SC-022 sub-clause (6) |
| A4 | FR-050 + its traceability row | Delete "*so the `Textures` category is genuinely scanned and FR-050/FR-051 are verified by SC-003*" — `PresetManager`'s ctor scans nothing. Re-map FR-050/FR-051 to **SC-012 clause 2c** (T024). Keep the instantiation requirement |
| A5 | FR-047 traceability | Add SC-010's controller clause (T019 step 5); as written SC-010 never calls `Controller::setComponentState()` |
| A6 | FR-042 / SC-009 | Add the multi-point-queue clause (T020 step 2) |
| A7 | FR-054 / SC-012 clause 2 | Add the bound-control assertion (clause 2b, T024) — the harness's three assertions are satisfied by a one-label template |
| A8 | SC-006 | State the master gain **per clause**: 2.0 for the peak bound, normalized 0.5 (linear 1.0) for clauses 2–3. SC-024 clause 3 inherits the fix |
| A9 | SC-007 / SC-026 | Pin the reading form: the live singleton `AllocationDetector::instance().getAllocationCount()`, never `scope.getAllocationCount()` inside the scope |
| A10 | FR-030 / SC-021 | Add the mono-output (`numChannels < 2`) early-out and clause, the non-zero canary pre-seed, the `silenceFlags = 3` value on the not-ready path, and the out-of-order `setupProcessing()` clause |

**Verify:** each amended requirement now describes behaviour that a T0xx task actually implements and a
test actually asserts; no threshold moved.

---

## GROUP 20 — Final integration gates

### T031 — full-suite run, pluginval, bundle guard, clean tree (SC-001, SC-002, SC-003, SC-018, SC-020)

**Edit:** none (verification only).

**Run, capturing to a log on the FIRST run:**
```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis        2>&1 | tee /tmp/seraphis-build.log
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests  2>&1 | tee -a /tmp/seraphis-build.log
build/windows-x64-release/bin/Release/seraphis_tests.exe --list-tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
node tools/check-bundle.js build/windows-x64-release/VST3/Release/Seraphis.vst3
git status --porcelain plugins/seraphis
grep -E "plugins[/\\]seraphis" /tmp/seraphis-build.log | grep -cE "warning C|warning:"
```

**Thresholds:**
- **SC-002 clause 1:** `--list-tests` contains **all eight** names — `Seraphis_ProcessorBusSetup`,
  `Seraphis_ParamDenormRoundTrip`, `Seraphis_StateRoundTrip`, `Seraphis_MidiEventTranslation`,
  `Seraphis_ProcessorLifecycle`, `Seraphis_EditorLifecycle`, `Seraphis_ProcessorRendersHeldNote`,
  `Seraphis_ParamFlowReachesEngine`. **A case count is explicitly not the threshold** — six cases in one
  file would satisfy a count while five required files are missing.
- **SC-002 clause 2:** the last line reads `All tests passed (N assertions in M test cases)`.
  (`[.perf]` is hidden from this run by design.)
- **SC-003:** pluginval exit code 0, zero failures.
- **SC-018:** `check-bundle.js` exits **0** with no `FAIL` line. If asserting the output literally, it is
  `OK   Seraphis.vst3: editor.uidesc + moduleinfo.json present` — **three spaces, `.vst3` suffix**.
- **SC-020:** `git status --porcelain plugins/seraphis` produces **no output** (proves FR-007a's
  `.gitignore` trio is real).
- **SC-001 (warning half):** the grep count is **0**. `krate_plugin_set_warnings` sets `/W4` (MSVC) and
  `-Wall -Wextra -Wpedantic` (GCC/Clang) but adds **no** `/WX` or `-Werror`, so a warning does not fail
  the build step — it must be counted explicitly.
- **SC-001 (green half) and SC-004** are CI-only: the three OS legs' build jobs and the macOS
  `auval -v aumu Srph KrAt` step (roster site #10). **`auval` cannot be run locally** — pluginval
  validates the VST3, never the AU. This task does not close until that macOS step is green on a pushed
  branch; record the run URL and the copied `AU VALIDATION SUCCEEDED` line in `compliance.md`.

**No other test target is rebuilt.** Phase 8 touches no `dsp/` file and no other plugin, so
`dsp_*_tests`, `shared_tests` and the five sibling plugin suites are out of scope.

---

### T032 [P] — portability and clang-tidy (SC-015, SC-016)

**Edit:** none (verification only; fix any finding in the offending file).

```bash
node tools/check-portability.js                                             # SC-015: exit 0
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja 2>&1 | tee /tmp/seraphis-tidy.log
./tools/run-clang-tidy.ps1 -Target all      -BuildDir build/windows-ninja 2>&1 | tee -a /tmp/seraphis-tidy.log
```
**Thresholds:** `check-portability.js` exits 0 over every C++ file this phase adds; `-Target seraphis`
analyses **at least one** file under `plugins/seraphis/src` and reports **zero** warnings; `-Target all`
also covers it. The `.sh` equivalents (`--target seraphis`, `--target all`) are exercised by the
Linux/macOS CI legs after T028's edits.

**Reminders:** a green Windows build proves nothing about GCC/Clang; MSVC accepts what they reject. Fix
**all** tidy warnings, not just those on new lines. Inspect the captured log — never re-run a slow tool
just to look at its output.

---

### T033 [P] — ASan lane (SC-012 clause 3)

**Edit:** none (verification only).

```bash
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
cmake --build build-asan --config Debug --target seraphis_tests 2>&1 | tee /tmp/seraphis-asan-build.log
build-asan/bin/Debug/seraphis_tests.exe 2>&1 | tee /tmp/seraphis-asan-run.log
```
**Threshold:** clean exit, **no ASan report**. Release passes by luck — the editor-lifecycle harness and
T018's mono-`channelBuffers32` clause only have teeth under a sanitizer. The valgrind nightly lane
(enabled by T028's site #14) is the *ongoing regression surface*, not the phase gate: it runs nightly
and therefore cannot be evaluated at phase completion.

---

### T034 — `compliance.md`

**Create:** `specs/seraphis-phase8-plugin-scaffold/compliance.md`

**Implement (project rule — Completion Honesty):** one row per **FR-001…FR-081** and per
**SC-001…SC-027**, each filled from **actual** evidence gathered in this phase:
- For each FR: the implementing file **and line number**, read at fill time — not from memory.
- For each SC: the test name and the **actual measured number** copied from the run output, compared
  against the spec threshold. No paraphrase.
- Rows explicitly marked *build-time* or *inspection* in the spec's traceability table carry that label
  plus the artefact that proves it.
- Record specifically:
  - SC-004's macOS CI run URL and the copied `AU VALIDATION SUCCEEDED` line;
  - SC-014's measured `processor_ns / chain_ns` ratio with the machine-idle caveat (**non-gating**);
  - SC-027's pre-output-stage level and the measured relative-RMS delta — or, if no reachable level
    produces a measurable difference, FR-044 marked **verified by code inspection only** with the
    measured deltas at each level tried;
  - SC-006's escalated scenario and level, if clause 3's non-vacuity assertion required escalation;
  - the **residual** from plan §8.3: the soft-limit push ramps (≤ 0.15 blend decaying over
    `kDefaultSmoothingMs = 5.0f`) instead of snapping when `softLimit == false` at prepare, because
    removing it requires a `dsp/` change Phase 8's scope forbids — **deferred to Phase 9, not silently
    accepted**;
  - the pre-existing roster drift fixed in T028 (`disrumpo` × 3 in `ci.yml`, `membrum` × 1 in
    `release.yml`).

**Self-check before submitting:** no relaxed thresholds, no placeholders, no quietly removed features,
no ✅ without a citation or a copied measurement just verified.

---

## Dependency summary

```
G1  (T001–T006)  skeleton, identity, resources          [P within group]
 └─ G2  (T007–T010) plugin-local headers                [P within group]
     └─ G3  (T011–T012) processor + controller skeleton [P within group]
         └─ G4  (T013) entry.cpp + CMake + root + .gitignore   → configures & links
             └─ G5  (T014) seraphis_tests target               → suite builds & runs
                 └─ G6  (T015) shared test fixture
                     └─ G7  (T016) buses
                         └─ G8  (T017) setupProcessing + latency
                             └─ G9  (T018) process() guards + setActive
                                 └─ G10 (T019) state round-trip
                                     └─ G11 (T020) parameter dispatch
                                         └─ G12 (T021) slice loop + events + master gain
                                             └─ G13 (T022) pushGlobalParams + param flow
                                                 └─ G14 (T023, T024)  [P] integration + editor
                                                     └─ G15 (T025, T026) alloc + perf
                                                         └─ G16 (T027) roster lint  (RED)
                                                             └─ G17 (T028) roster edits (GREEN)
                                                                 └─ G18 (T029) generated artifacts
                                                                     └─ G19 (T030) spec amendments
                                                                         └─ G20 (T031; T032, T033 [P]; T034)
```

**Critical-path note:** T028 site #10 (the macOS `auval` step) must land before SC-004 can be measured,
so G17 and T031's SC-004 clause interleave with a pushed branch — that is the only gate in this phase
with no local measurement surface.
