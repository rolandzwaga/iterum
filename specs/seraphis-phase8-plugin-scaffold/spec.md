# Feature Specification: Seraphis Phase 8 — Plugin Scaffold

**Spec slug:** `seraphis-phase8-plugin-scaffold`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 8 (roadmap lines 323–435)
**Depends on:** Phase 7 (`SeraphisEngine` / `SeraphisVoice` / `SeraphisMacroMatrix`) and Phase 6 (`AetherReverb`), both ✅ COMPLETE
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-07-31

---

## Overview

Phase 8 turns the seven finished KrateDSP phases into a real VST3 plugin. It creates
`plugins/seraphis/` from Ruinae's shape (roadmap line 331: *"Template: Ruinae, not Membrum"*) with
Membrum's instrument bus/AU configuration (roadmap lines 376–381), wires the Phase 7 composed chain —
`SeraphisEngine::processStereoBlock` → `AetherReverb::processStereoBlock` →
`SeraphisEngine::processOutputStage`, exactly as `tests/test_helpers/seraphis_chain.h` already models it —
into `Processor::process()`, registers two parameter packs (global + macro), ships a placeholder
`editor.uidesc`, stands up a `seraphis_tests` Catch2 target, and enrols the plugin in **every** CI, tooling
and documentation roster outside `plugins/` in the same change. The audible deliverable is small: a held
MIDI note produces non-silent, non-clipping stereo audio through the full Phase 1–7 chain. The
*infrastructural* deliverable is the whole point — roadmap line 417: *"A green local build proves nothing
here; each of these is an independent CI failure surface."*

No new DSP is written in this phase. No `dsp/` file is modified.

---

## Amendments

### Session 2026-07-31 — implementation, GROUP 14

One amendment, made against **measured** evidence during implementation. It is written into the SC body
itself, with its measurement, so this list is an index and nothing depends on reading it.

| Item | What changed | Why, in one line |
|---|---|---|
| SC-005 | the non-silence peak floor drops from `1.0e-3` (−60 dBFS) to the **measured** `5.0e-4`, and the render is pinned to the **registered defaults** (polyphony 8, not 16) | `1.0e-3` was derived from Phase 7's per-voice `3.8e-3` estimate ÷ √8; note 60 at the FR-019 neutral actually reaches `9.59e-4` through the chain — 0.36 dB short — so a *correct* implementation could not pass it |

**No FR changed, and nothing in `dsp/` was touched to accommodate this.** The level itself is Phase 7's
shipped neutral (`SeraphisVoice` FR-019 defaults) combined with Phase 7 FR-052's `1/sqrt(polyphony)`
voice-sum gain, which is applied whether or not the other voices sound; raising it is a *Phase 9*
question (a voice-level or output-trim parameter), not a scaffold question.

### Session 2026-07-31 — implementation, GROUP 19 (plan §8.2, A1–A10)

Ten amendments, each a **factual correction** or a **strengthening**, applied by task T030. Every one is
written into the requirement/criterion body itself, so this table is an index and nothing depends on
reading it. **No threshold moved**; where a criterion changed shape it now asserts *more* observable
behaviour than the text it replaces, and every amended requirement names the implementing task.

| # | Site | What changed | Why, in one line |
|---|---|---|---|
| A1 | FR-008 file list | added `tests/seraphis_test_fixture.h` and the three `.gitkeep` entries (`docs/`, `src/ui/`, `resources/presets/Textures/`) | FR-008 says the skeleton is "**exactly** the file list below", and the shipped tree has four files the list omitted |
| A2 | FR-023 clause 4, FR-033, SC-013 clause 4 | the `restartComponent(kLatencyChanged)` requirement is **deleted**; the latency is the invariant 1024 and hosts read `getLatencySamples()` after `setupProcessing()`; SC-013 clause 4 is restated as T017's invariance matrix | `AetherReverb` reports 1024 *before* any prepare (`aether_reverb.h:2607–2613`), so there is no transition to announce, and an `AudioEffect` has no route to an `IComponentHandler` |
| A3 | FR-031, SC-022 | velocity maps as `uint8(clamp(velocity*127 + 0.5, 1, 127))`; SC-022 gains sub-clause (6) | a truncating `uint8(v*127)` sends `0` for a legal small velocity, and `SeraphisEngine::noteOn` maps velocity 0 to `noteOff` (`seraphis_engine.h:374–377`) |
| A4 | FR-050, SC-003, traceability | the "so the `Textures` category is genuinely scanned" clause is deleted; FR-050/FR-051 re-map to **SC-012 clause 2c** | `PresetManager`'s constructor scans nothing — enumeration is `scanPresets()`, which nothing in Phase 8's *plugin* code calls |
| A5 | SC-010, FR-047 traceability | SC-010 gains an explicit **controller clause** (T019 step 5) | as written SC-010 never called `Controller::setComponentState()`, leaving FR-047 and both `load*ParamsToController` helpers with no detector |
| A6 | SC-009, FR-042 traceability | SC-009 gains the **multi-point-queue clause** (T020 step 2) | with one-point queues `getPoint(0)` and `getPoint(count-1)` are indistinguishable |
| A7 | SC-012 clause 2 | split into 2a/2b/2c; 2b asserts the **eight bound controls** and their types | the harness asserts only `attached()`, `getFrame() != nullptr`, `getNbViews() > 0` — all satisfied by a one-label template |
| A8 | SC-006, SC-024 clause 3 | the master gain is stated **per clause**: normalized 1.0 (linear 2.0) for the peak bound, normalized 0.5 (linear 1.0) for clauses 2–3 | `renderSeraphisChain` applies no gain, so a gain-2.0 comparison fails a *correct* implementation and makes clause 3 pass for the wrong reason |
| A9 | SC-007, SC-026 | the reading form is pinned to `AllocationDetector::instance().getAllocationCount()` | `AllocationScope::getAllocationCount()` returns a member assigned **only** by the destructor, so reading it inside the scope passes unconditionally |
| A10 | FR-030, SC-021 | adds the mono-output (`numChannels < 2`) early-out and clause, the non-zero canary pre-seed, `silenceFlags = 3` on the not-ready path, and the out-of-order `setupProcessing()` clause | a host may ignore `setBusArrangements`' `kResultFalse` and still present a mono bus; and "produces silence" is only detectable from a pre-seeded non-zero buffer |

---

## Scope

**In scope**

1. `plugins/seraphis/` directory skeleton, `CMakeLists.txt`, `version.json`, `CHANGELOG.md`, `README.md`,
   `CLAUDE.md` leaf, `docs/`, `installers/`.
2. Per-plugin identity conventions: two fresh FUIDs, AU `aumu`/`Srph`/`KrAt`, bundle
   `com.krateaudio.seraphis`, bus layout, parameter-ID map, `kCurrentStateVersion = 1`, the seed preset
   category `Textures`.
3. `Processor` + `Controller` (2–3 TUs each), owning and driving the Phase 6/7 chain.
4. Two parameter packs only: `parameters/global_params.h` (master gain, polyphony, output soft-limit) and
   `parameters/macro_params.h` (five macros as **inert** 0–1 values). Roadmap line 397–398.
5. `engine/seraphis_engine_config.h` (thin prepare-time config), `preset/seraphis_preset_config.h`,
   `update/seraphis_update_config.h`.
6. `resources/`: placeholder `editor.uidesc`, `au-info.plist`, `auv3/audiounitconfig.h.in`,
   `auv3/macOS/Seraphis.entitlements`, `presets/Textures/` (the seed category, which Phase 12 extends but
   never renames).
7. `seraphis_tests` target with day-one coverage (roadmap lines 410–413).
8. All eight external registration sites (roadmap lines 419–430), plus the two things that make them
   verifiable rather than hopeful: the `.gitignore` generated-file trio (FR-007a) and a new
   `tools/lint-plugin-roster.js` registered in `guard-ci-gates.js` (FR-081) — without the latter, a missing
   roster entry produces a **green** CI run in which Seraphis is never tested or validated.

**Non-goals — explicitly owned by later phases**

| Deferred to | What |
|---|---|
| Phase 9 (`seraphis-phase9-parameters`) | Every remaining engine parameter (cloud, morph/entropy, life modulators, body, atmosphere, aether, effects); macro→matrix wiring; state versioning/migration; spectral-state serialization |
| Phase 10 (`seraphis-phase10-effects`) | Spectral freeze, spectral delay, tape saturation controls, stereo wandering; send/ordering topology |
| Phase 11 (`seraphis-phase11-ui`) | The real `editor.uidesc`, macro-first layout, the cloud-view visualization, sub-controllers, DataExchange piggyback. `src/ui/` stays empty in Phase 8 (roadmap line 349) |
| Phase 12 (`seraphis-phase12-presets-release`) | The **final** preset category set, factory preset library, preset validation harness, release gate |

Non-goals within Phase 8 itself:

- No **full** Ruinae-style processor/controller decomposition. Roadmap line 368 states the day-one shape
  *is* **2–3 processor TUs and 2–3 controller TUs**, and that Ruinae's 5-processor-TU / 10-controller-TU
  split is adopted only *"when a file passes ~1500 lines"*. Phase 8 therefore ships the small end of that
  range (see FR-008's file list); the additional `processor_*.cpp` / `controller_*.cpp` TUs named in the
  roadmap skeleton are created only if a file actually crosses the threshold in this phase.
- No preset browser UI, no preset generator tool, no factory presets beyond an empty category directory.
- No `.vstpreset` files shipped.
- No modification of any file under `dsp/`.

---

## Existing components (verified this session)

Every signature below was read from the header this session; line numbers are from the files as they
stand on `feat/seraphis-phase1-life-modulators` at 2026-07-31.

| Component | Header / file | What Phase 8 reuses (verified signature) |
|---|---|---|
| `SeraphisEngine` | `dsp/include/krate/dsp/systems/seraphis_engine.h:123` | `void prepare(double sampleRate, const SeraphisEngineConfig& cfg) noexcept` (:201); `void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept` (:370); `void noteOff(std::uint8_t note) noexcept` (:415); `void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept` (:441); `void processOutputStage(float* l, float* r, std::size_t n) noexcept` (:512); `void setPolyphony(std::size_t n) noexcept` (:321); `void setSeed(std::uint32_t) noexcept` (:353); `void setAtmosphereFreeze(bool) noexcept` (:551); `void setOutputSaturation(float amount) noexcept` (:566); `void collectHeldPartials(std::size_t voiceIndex, float* dest, std::size_t capacity, std::size_t& outCount) const noexcept` (:596); `[[nodiscard]] BloomEvents consumeBloomEvents() noexcept` (:654); `void reset() noexcept` (:286); `void silence() noexcept` (:308) |
| `SeraphisEngine` constants | same header | `kMaxVoices = 16` (:130), `kControlChunkSamples = 64` (:132), `kMaxBlockSamples = 2048` (:134), `kBloomPartialCap = 32` (:154), `kOutputSaturation = 0.15f` (:142), `kEngineSizeBound` (:180). `struct BloomEvents { std::uint32_t noteOnMask; std::uint32_t noteOffMask; }` (:186–189) |
| `SeraphisEngineConfig` | same header, `:92–97` | `SeraphisVoiceConfig voice{}; std::size_t polyphony = 8; std::uint32_t seed = 1u;` |
| `SeraphisVoiceConfig` | `dsp/include/krate/dsp/systems/seraphis_voice.h:105–120` | `float captureSeconds = 4.0f; bool blurEnabled = true; bool freezeEnabled = true; std::size_t blurFftSize = 1024; std::size_t freezeFftSize = 2048; std::size_t maxBlockSamples = 2048;` |
| `SeraphisMacroMatrix` | `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h:154` | `void setMacro(SeraphisMacro, float) noexcept` (:554); `void setMacros(const SeraphisMacroValues&) noexcept` (:599); `void apply(SeraphisEngine& engine) const noexcept` (:623); `[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept` (:667) |
| `SeraphisMacroValues` | same header, `:122–128` | `float dream = 0.0f; float bloom = 0.0f; float dissolve = 0.0f; float gravity = 0.5f; float entropy = 0.0f;` — the FR-060 documented neutral. These are the Phase 8 parameter defaults. |
| `SeraphisAetherTargets` | same header, `:110–119` | `mix 0.35f, size 0.50f, width 1.0f, shimmerOctaveSend 0.0f, shimmerFifthSend 0.0f, bloomSend 0.0f, sizeBreathDepth 0.20f, dimensionalityTideDepth 0.20f` |
| `AetherReverb` | `dsp/include/krate/dsp/effects/aether_reverb.h:1377` | `void prepare(double sampleRate, const PrepareConfig& config) noexcept` (:1614); `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, …) noexcept` (:2164); `setMix` (:2336), `setSize` (:2208), `setWidth` (:2333), `setShimmerOctaveSend` (:2280), `setShimmerFifthSend` (:2285), `setBloomSend` (:2295), `setSizeBreathDepth` (:2320), `setDimensionalityTideDepth` (:2328); `void bloomNoteOn(std::int32_t voiceId, const float* partialHz, std::size_t count) noexcept` (:2392); `void bloomNoteOff(std::int32_t voiceId) noexcept` (:2473); `[[nodiscard]] std::size_t getLatencySamples() const noexcept` (:2612); `void reset() noexcept` (:1971) |
| `AetherReverb::PrepareConfig` | same header, `:1578–1588` | `std::size_t numChannels = 8; std::size_t maxBlockSamples = 2048; float maxDelaySeconds = 0.50f; bool shimmerEnabled = true; PitchMode shimmerMode = PitchMode::Granular; bool bloomEnabled = true; bool spectralDiffusionEnabled = true; std::size_t diffusionFftSize = 1024; std::uint32_t seed = 1;` |
| Composed-chain reference | `tests/test_helpers/seraphis_chain.h` | `inline void renderSeraphisChain(SeraphisEngine&, AetherReverb&, const SeraphisMacroMatrix&, const SeraphisChainScript&, double sampleRate, std::size_t blockSize, std::size_t totalSamples, std::vector<float>& outL, std::vector<float>& outR)`. Its six-step slice body (macros→reverb setters → `processStereoBlock` → reverb → `processOutputStage` → bloom lifecycle) is the literal model the processor reproduces; the file banner says so ("*so Phase 8's processor has a literal model to reproduce*"). |
| Ruinae CMake shape | `plugins/ruinae/CMakeLists.txt` | `krate_plugin_read_version(RUINAE)` + `krate_plugin_configure_generated_files()` (:10–11); `smtg_add_vst3plugin(${PLUGIN_NAME} …)` (:18); `krate_plugin_platform_setup(… TAG RUINAE BUNDLE_BASE com.krateaudio.ruinae ENTITLEMENTS Ruinae.entitlements KIND instrument)` (:103–108); `smtg_target_add_plugin_resources(… RESOURCES resources/editor.uidesc)` (:113–116); `krate_plugin_install_to_system` (:121); `krate_plugin_install_presets` (:129); `krate_plugin_set_warnings` (:134); `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()` (:139–141) |
| Shared CMake helpers | `cmake/KratePlugin.cmake` | `macro(krate_plugin_read_version PREFIX)` (:35) reads `version.json` keys `version/name/description/publisher/url/copyright`; `macro(krate_plugin_configure_generated_files)` (:80) generates `src/version.h`, `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` (:83–105); `function(krate_plugin_platform_setup target …)` (:126) with `KIND` validated to `instrument|effect` (:130–132); `krate_plugin_install_to_system` (:255); `krate_plugin_install_presets` (:287); `krate_plugin_set_warnings` (:319) = MSVC `/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458`, GCC/Clang `-Wall -Wextra -Wpedantic -Wno-unused-parameter` |
| Factory/entry pattern | `plugins/ruinae/src/entry.cpp:42–78` | `BEGIN_FACTORY_DEF(stringCompanyName, stringVendorURL, stringVendorEmail)` + two `DEF_CLASS2(INLINE_UID_FROM_FUID(…), PClassInfo::kManyInstances, kVstAudioEffectClass / kVstComponentControllerClass, …, FULL_VERSION_STR, kVstVersionString, …::createInstance)` + `END_FACTORY` |
| Global parameter pack contract | `plugins/ruinae/src/parameters/global_params.h` | `struct GlobalParams` of `std::atomic<>` (:26–33); `handleGlobalParamChange(GlobalParams&, ParamID, ParamValue)` (:39); `registerGlobalParams(ParameterContainer&)` (:87); `formatGlobalParam(ParamID, ParamValue, String128)` (:130); `saveGlobalParams(const GlobalParams&, IBStreamer&)` (:178); `loadGlobalParams(GlobalParams&, IBStreamer&)` (:187); `template<typename SetParamFunc> loadGlobalParamsToController(IBStreamer&, SetParamFunc)` (:220). Master-gain denorm `value * 2.0` clamped `[0,2]` (:47–49); polyphony denorm `clamp(int(value*15+1+0.5), 1, 16)` (:59–61) |
| Macro parameter pack contract | `plugins/ruinae/src/parameters/macro_params.h` | `struct MacroParams { std::atomic<float> values[4]; }` (:13–15) and the same six-function shape (:17, :41, :53, :66, :73, :82) |
| Dropdown helpers | `plugins/shared/src/ui/parameter_helpers.h` | `createDropdownParameter(const TChar* title, ParamID, std::initializer_list<const TChar*>)` (:23); `createDropdownParameterWithDefault(const TChar* title, ParamID, int32_t defaultIndex, std::initializer_list<const TChar*>)` (:47); `logMapFromNormalized/logMapToNormalized` (:80, :85) |
| Preset config | `plugins/shared/src/preset/preset_manager_config.h:20–25` | `struct PresetManagerConfig { Steinberg::FUID processorUID; std::string pluginName; std::string pluginCategoryDesc; std::vector<std::string> subcategoryNames; }` — field order is load-bearing for designated initializers (:17–18). Adapter pattern: `plugins/ruinae/src/preset/ruinae_preset_config.h:17` `inline Krate::Plugins::PresetManagerConfig makeRuinaePresetConfig()` |
| Update config | `plugins/shared/src/update/update_checker_config.h:14–18` | `struct UpdateCheckerConfig { std::string pluginName; std::string currentVersion; std::string endpointUrl; }`. Adapter: `plugins/ruinae/src/update/ruinae_update_config.h:8` `makeRuinaeUpdateConfig()` returning `{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }` |
| Instrument bus shape | `plugins/membrum/src/processor/processor.cpp` | `addEventInput(STR16("Event In"))` (:117); `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)` (:120); `Processor::setBusArrangements` rejecting `numIns != 0` and any non-`kStereo` output (:1050–1069). **Not** copied: Ruinae's `addAudioInput(...)` at `plugins/ruinae/src/processor/processor.cpp:56` |
| AU config template | `plugins/membrum/resources/auv3/audiounitconfig.h.in` | `kAUcomponentType 'aumu'` (:8), `kAUcomponentSubType 'Mbrm'` (:12), `kAUcomponentManufacturer 'KrAt'` (:16), `kAUcomponentVersion @AU_COMPONENT_VERSION@` (:24), `kSupportedNumChannels 02` (:35) with the digit-pair explanation at :28–34 |
| AU plist | `plugins/membrum/resources/au-info.plist` | `AudioComponents` dict with `factoryFunction AUWrapperFactory`, `manufacturer KrAt`, `subtype Mbrm`, `type aumu` (:23–41); `AudioUnit SupportedNumChannels` = single `Inputs 0 / Outputs 2` dict (:42–50) |
| Test-target shape | `plugins/ruinae/tests/CMakeLists.txt` | `add_executable(ruinae_tests …)` (:10); plugin `.cpp`s recompiled via `${CMAKE_CURRENT_SOURCE_DIR}/../src/...` (:139–152); SDK sources `common/memorystream.cpp` (:153), `vst/hosting/hostclasses.cpp` + `pluginterfacesupport.cpp` (:156–157), `vstgui_test_stubs.cpp` + `main/moduleinit.cpp` + `main/pluginfactory.cpp` (:160–162); links `KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk` (:165–173); include dirs incl. `${CMAKE_SOURCE_DIR}/tests` (:175–183); `target_compile_definitions(… RUINAE_RESOURCES_DIR="…/../resources")` (:193–197); `-fno-fast-math -fno-finite-math-only` source props under `Clang\|GNU` (:200–233); `catch_discover_tests(ruinae_tests REPORTER console)` (:236) |
| Test main | `plugins/ruinae/tests/unit/test_main.cpp` | `void* moduleHandle = nullptr;` + `enableFTZDAZ(); return Catch::Session().run(argc, argv);` |
| Linker stub | `plugins/ruinae/tests/vstgui_test_stubs.cpp` | `Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }` |
| Editor-lifecycle harness | `tests/test_helpers/editor_lifecycle_harness.h:102–105` | `inline void exerciseEditorLifecycle(Steinberg::Vst::EditController& controller, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)` |
| Render fingerprint | `tests/test_helpers/render_fingerprint.h` | `fingerprintRender(std::span<const float>)` (:64), `compareFingerprints(const RenderFingerprint&, …)` (:101), `kSampleTolerance = 1.0e-4f` (:49), `kMetricTolerance = 1.0e-5` (:52) |
| Allocation detector | `tests/test_helpers/allocation_detector.h:75` | `class AllocationScope` |
| State stream shape | `plugins/ruinae/src/processor/processor_state.cpp:24–31` | `Steinberg::IBStreamer streamer(state, kLittleEndian); streamer.writeInt32(kCurrentStateVersion);` then packs in deterministic order |
| Bundle resource guard | `tools/check-bundle.js:15–16` | every bundle must contain `Contents/Resources/editor.uidesc` and `Contents/Resources/moduleinfo.json`, both non-empty |

---

## New components

Phase 8 adds no DSP classes. The new C++ types are plugin-local and all live in a new top-level
`Seraphis` namespace.

**ODR sweep run this session** (`grep -rn … dsp/ plugins/`, per root `CLAUDE.md` and `dsp/CLAUDE.md`):

| New type | Layer | Header / file | ODR sweep result |
|---|---|---|---|
| `namespace Seraphis` | plugin (not a DSP layer) | `plugins/seraphis/src/plugin_ids.h` | `grep -rn "namespace Seraphis" dsp/ plugins/` → **2 hits, neither a conflict**: `dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h:80` and `:524` open/close `namespace SeraphisPhase3TestData` (different identifier, test-only TU). **No `namespace Seraphis` exists.** CLEAR. |
| `Seraphis::Processor` | plugin | `src/processor/processor.h` | `grep -rn "class Processor" dsp/ plugins/ --include=*.h` → 6 definitions, each in its own plugin namespace: `disrumpo:43`, `gradus:34`, `innexus:58`, `iterum:51`, `membrum:38`, `ruinae:124`. No `Seraphis::Processor`. CLEAR (and `tools/lint-odr.js` qualifies by namespace — `tools/lint-odr.js:11–13`). |
| `Seraphis::Controller` | plugin | `src/controller/controller.h` | Same grep for `class Controller` → 6 definitions (`disrumpo:60`, `gradus:44`, `innexus:56`, `iterum:40`, `membrum:42`, `ruinae:69`) + 2 forward declarations (`disrumpo/src/controller/custom_buttons.h:13`, `iterum/src/controller/custom_views.h:21`). No `Seraphis::Controller`. CLEAR. |
| `Seraphis::GlobalParams` | plugin | `src/parameters/global_params.h` | `grep -rn "struct GlobalParams" dsp/ plugins/` → **1 hit**: `plugins/ruinae/src/parameters/global_params.h:26`, inside `namespace Ruinae` (:20). Different namespace. CLEAR. |
| `Seraphis::MacroParams` | plugin | `src/parameters/macro_params.h` | `grep -rn "struct MacroParams" dsp/ plugins/` → **1 hit**: `plugins/ruinae/src/parameters/macro_params.h:13`, inside `namespace Ruinae` (:11). Different namespace. CLEAR. |
| `Seraphis::EngineConfig` (free functions `makeSeraphisEngineConfig()`, `makeSeraphisReverbConfig()`) | plugin | `src/engine/seraphis_engine_config.h` | `grep -rn "struct EngineConfig\|class EngineConfig" dsp/ plugins/` → 0 hits. The header is specified below as **free factory functions returning the DSP-owned `SeraphisEngineConfig` / `AetherReverb::PrepareConfig`**, so no new type is introduced at all. CLEAR. |

**AU subtype sweep:** `grep -rn "kAUcomponentSubType " plugins/*/resources/auv3/audiounitconfig.h.in` → `Dsrm`,
`Grad`, `Innx`, `Itrm`, `Mbrm`, `Ruin`. `grep -rn "Srph" plugins/ tools/ .github/` → **0 hits**. `Srph` is free.

**FUID sweep:** the twelve currently-registered FUIDs are
`disrumpo/src/plugin_ids.h:26,:31`, `gradus:20,:23`, `innexus:20,:24`, `iterum:21,:25`,
`membrum:18,:21`, `ruinae:24,:28`. Two freshly generated GUIDs must not collide with any of them
(FR-011).

---

## Conventions decided in this spec

Roadmap line 371 requires these to be decided here and recorded in `plugin_ids.h`.

| Convention | Decision | Roadmap trace |
|---|---|---|
| FUIDs | Two freshly generated GUIDs, never reused, never changed post-release; recorded as `Seraphis::kProcessorUID` / `Seraphis::kControllerUID` | line 373 |
| AU type / subtype / manufacturer | `aumu` / `Srph` / `KrAt` | lines 374–375 |
| Bundle base | `com.krateaudio.seraphis` | line 375 |
| Buses | 1 event input, 1 stereo audio output, **no** `addAudioInput()` | lines 376–380 |
| `kSupportedNumChannels` | `02` (single 0-in/2-out config), matching `au-info.plist` | lines 377–380 |
| Parameter ID base | 0, with 100-ID section gaps (the reserved map below) | lines 381–386 |
| State version | `constexpr Steinberg::int32 kCurrentStateVersion = 1;` in `plugin_ids.h`, shared by processor and controller with no cross-include | lines 386–387 |
| Preset category (Phase 8 seed) | **`Textures`** — one category only; the full set is a Phase 12 decision. It is a *seed*, not a throwaway: see the next row. | lines 388–390 |
| Preset-category evolution | Phase 12 **extends** the list additively. A shipped category is **never renamed** — renaming orphans every preset saved against it. Recorded in `plugins/seraphis/CLAUDE.md` (FR-009). | line 388 + the Membrum lesson |
| Prepare-time block bound | `SeraphisEngine::kMaxBlockSamples` = **2048**, passed unconditionally to both configs; scratch is sized to it once (FR-023, FR-026, FR-028, FR-053) | — |
| Reported latency | **1024 samples**, constant at every sample rate — spectral diffusion ships **enabled** (the Phase 6 default) (FR-033) | — |
| Master-gain smoothing | `OnePoleSmoother` advanced **per sample**, `kMasterGainSmoothMs = 20.0f`, snapped to the parameter atomic on the first block after prepare (FR-024a clause 3) | — |
| MPE / note expression | **Out of scope in Phase 8** — event-input bus only, **no** `INoteExpressionController` (FR-018) | line 514 |
| Controller services | `PresetManager` **is** instantiated in `Controller::initialize()`; `UpdateChecker` is **not** (FR-050, FR-052) | — |

### Reserved parameter-ID map (roadmap lines 383–386)

```
0–99      Global            (Phase 8 — SHIPPED)
100–199   Macros            (Phase 8 — SHIPPED, inert)
200–399   Harmonic Cloud    (Phase 9)
400–599   Spectral Morph / Entropy (Phase 9)
600–799   Life Modulators   (Phase 9)
800–999   Continuous Body   (Phase 9)
1000–1199 Atmosphere        (Phase 9)
1200–1399 Aether            (Phase 9)
1400+     Effects           (Phase 10)
```

### Parameters shipped in Phase 8

| ID | Enum name | Type | Range / mapping | Default (normalized) |
|---|---|---|---|---|
| 0 | `kMasterGainId` | Range | `value * 2.0` clamped `[0, 2]` linear gain, displayed in dB | `0.5` (unity) |
| 1 | `kPolyphonyId` | `StringListParameter` "1".."16" | index+1 voices → `SeraphisEngine::setPolyphony` | index `7` (= 8 voices, the `SeraphisEngineConfig::polyphony` default at `seraphis_engine.h:95`) |
| 2 | `kSoftLimitId` | Toggle (stepCount 1) | on → `setOutputSaturation(SeraphisEngine::kOutputSaturation)` = `0.15f`; off → `setOutputSaturation(0.0f)` | `1.0` (on) |
| 100 | `kMacroDreamId` | Range | inert 0–1 | `0.0` |
| 101 | `kMacroBloomId` | Range | inert 0–1 | `0.0` |
| 102 | `kMacroDissolveId` | Range | inert 0–1 | `0.0` |
| 103 | `kMacroGravityId` | Range | inert 0–1 (bipolar around centre) | `0.5` |
| 104 | `kMacroEntropyId` | Range | inert 0–1 | `0.0` |

Macro defaults are exactly `SeraphisMacroValues` (`seraphis_macro_matrix.h:122–128`) so that a Phase 9
wiring of these IDs into `SeraphisMacroMatrix::setMacros` is neutral at plugin defaults.

---

## Clarifications

### Session 2026-07-31

Every question raised by the clarification scan is answered below and **encoded into the requirement text
of this spec**; the requirements are authoritative and this log is a record of provenance, not a place a
reader must consult to know the behaviour. No threshold was relaxed by any answer.

- **Q: What exactly is the master-gain smoother, and what is its value at the first sample after
  `setupProcessing()`?** → Mirror `AetherReverb::applyControl` (`aether_reverb.h:2950–2958`): an
  `OnePoleSmoother` (`primitives/smoother.h:134`) advanced **per sample**, with a named
  `static constexpr` time constant of ~20 ms (`kMasterGainSmoothMs = 20.0f`, the same family as
  `SeraphisEngine::kSumGainSmoothMs`), **snapped** to the current parameter atomic on the first
  `process()` after `setupProcessing()`/`setActive(true)` and ramped thereafter. SC-019 clause 1
  (silence at gain 0) therefore holds **literally**, over the whole render; SC-008's block-size
  invariance is unaffected. *(→ FR-024a clause 3, FR-053, SC-019 clause 1, conventions table, edge cases.)*
- **Q: Does Seraphis ship 1024 samples of reported latency, and does the processor announce the change?**
  → Ship `spectralDiffusionEnabled = true` (the Phase 6 default) and report **1024 samples**. The spec's
  earlier claim that `getLatencySamples()` scales with sample rate was **false and is corrected**: it is a
  constant 1024 for the shipped config, and SC-013 asserts exactly 1024.
  **The "announce the change" half of this answer is SUPERSEDED by amendment A2** (see FR-023 clause 4 /
  FR-033 / SC-013, which are authoritative): there is no change to announce — an unprepared
  `AetherReverb` already reports 1024 (`aether_reverb.h:2607–2613`) — and an `AudioEffect` has no
  `IComponentHandler` to announce on. *(→ FR-023 clause 4, FR-033, FR-053, SC-013, edge cases.)*
- **Q: Does Phase 8's `Controller` instantiate `PresetManager` and `UpdateChecker`, or only ship the
  config headers?** → `PresetManager` **yes** (instantiated in `initialize()`; *A4 corrects the reason
  given here — instantiation scans nothing, so the scan is asserted by SC-012 clause 2c, not SC-003*);
  `UpdateChecker` **no** — no `std::thread` and no
  network fetch in the editor-lifecycle harness, the ASan lane or the valgrind lane. Only the
  `make*Config()` header ships for the update checker. *(→ FR-050, FR-052, SC-003, traceability table.)*
- **Q: What `maxBlockSamples` is passed at prepare, and what bound governs FR-026's sub-division?** →
  Always the constant **2048** (`SeraphisEngine::kMaxBlockSamples`), passed to **both** configs; scratch
  is sized to 2048 once at `setupProcessing()`; host blocks larger than 2048 are sub-divided by FR-026.
  One documented constant governs the engine config, the reverb config, the slice bound and the scratch
  size, so they cannot drift apart; the ~32 KB scratch cost is accepted. *(→ FR-023 clause 1, FR-026,
  FR-028, FR-053, conventions table, edge cases.)*
- **Q: Where does polyphony enter, and what is the initial value of the "last pushed" tracker?** →
  `setupProcessing()` reads the current parameter atomic into `cfg.polyphony` **and** resets the
  last-pushed tracker to that same value, so engine and parameter agree from the first sample. The
  ordering rule (`setState()` may legally precede `setupProcessing()`) is written into FR-023 and the
  tracker reset into FR-024a clause 1, giving SC-019 clause 3 a defined starting state.
  *(→ FR-023 clauses 2–3, FR-024a clause 1, SC-019 clause 3.)*
- **Q: Does `process()` write `silenceFlags`, and what does `getTailSamples()` report?** → `process()`
  clears `data.outputs[0].silenceFlags` to `0` on every non-degenerate render; `getTailSamples()` stays
  at the SDK default, with tail/idle reporting **explicitly deferred to Phase 10**, the phase that owns
  the effects roster and can define a "reverb has decayed" predicate. *(→ FR-024 silence-flag clause,
  SC-021 silence-flag clause.)*
- **Q: MPE / poly-aftertouch — does `Controller` implement `INoteExpressionController` in Phase 8?** →
  **No.** MPE is out of scope for Phase 8: event-input bus only, no `INoteExpressionController`. The
  decision **and** the FUID host-cache caveat (adding an interface to a released controller FUID can
  invalidate host-cached class metadata) are recorded in `plugins/seraphis/CLAUDE.md` as a known Phase 9
  decision. *(→ FR-019, FR-009 clause 1, "Deferred-scope calls resolved in this spec" item 1.)*
- **Q: How much is in the placeholder `editor.uidesc`?** → One container plus **eight** stock controls
  bound via `<control-tags>` to the eight shipped parameter IDs (knob/slider for master gain and the five
  macros, `COptionMenu` for polyphony, checkbox for soft limit). The lifecycle harness then exercises
  `IDependent` wiring and value display, and a wrong parameter type surfaces now rather than in Phase 11.
  Stock views only; template named `"editor"`. *(→ FR-054, SC-012 clause 2.)*
- **Q (confirmation): is `Textures` the right seed category, and will Phase 12 treat it additively?** →
  Confirmed. `Textures` is seeded now in **both** the filesystem subdirectory and the XML metadata (the
  Membrum lesson). Phase 12 **extends** the category list additively and **never renames** a shipped
  category, because renaming orphans presets saved against it. Recorded in the spec and in
  `plugins/seraphis/CLAUDE.md`. *(→ FR-009 clause 2, FR-050, FR-051, conventions table.)*
- **Q (confirmation): does Phase 8 ship `docs/index.html`?** → No. Phase 8 creates the
  `plugins/seraphis/docs/` directory only; Phase 12 authors `index.html`. `.github/workflows/docs.yml`
  needs no edit — it globs `plugins/*/`, and a missing page is reported as `has_page: false` harmlessly.
  *(→ FR-008, FR-080.)*

---

## Functional Requirements

### A. Plugin target and build integration

- **FR-001** `plugins/seraphis/CMakeLists.txt` MUST call `krate_plugin_read_version(SERAPHIS)` then
  `krate_plugin_configure_generated_files()` before defining the target, mirroring
  `plugins/ruinae/CMakeLists.txt:10–11`. *(Roadmap line 338, 363–366.)*
- **FR-002** `plugins/seraphis/version.json` MUST exist with the six keys
  `krate_plugin_read_version` parses (`cmake/KratePlugin.cmake:37–42`): `version`, `name`
  (`"Seraphis"`), `description`, `publisher` (`"Krate Audio"`), `url`, `copyright`. Initial version
  `0.1.0`.
  The optional `preset_subdir` key MUST be **absent**. Its only reader in the repo is the release
  workflow: `.github/workflows/release.yml:297–303` does
  `PRESET_SUBDIR=$(jq -r '.preset_subdir // empty' plugins/${PLUGIN}/version.json)` and appends it to
  `staging/Library/Application Support/Krate Audio/${PLUGIN_NAME}`. Absent ⇒ presets stage directly under
  `Krate Audio/Seraphis`, which is what FR-050/FR-051 require. `krate_plugin_read_version`
  (`cmake/KratePlugin.cmake:35–42`) parses only the six keys above and never looks at `preset_subdir`, so
  it would neither require nor reject an extra key — the key is a **release-staging** switch, not a CMake
  one. The build-time counterpart is FR-005's no-argument `krate_plugin_install_presets()` call
  (`cmake/KratePlugin.cmake:281–285` documents `SRC_SUBDIR`/`DEST_SUBDIR`, which Seraphis does **not**
  pass).
- **FR-003** The target MUST be created with `smtg_add_vst3plugin(${PLUGIN_NAME} …)` and link
  `sdk vstgui_support KrateDSP KratePluginsShared` PRIVATE, with `${CMAKE_CURRENT_SOURCE_DIR}/src` as a
  PRIVATE include dir. *(Model: `plugins/ruinae/CMakeLists.txt:18, 83–95`.)*
- **FR-004** `krate_plugin_platform_setup(${PLUGIN_NAME} TAG SERAPHIS BUNDLE_BASE
  com.krateaudio.seraphis ENTITLEMENTS Seraphis.entitlements KIND instrument)` MUST be called. `KIND`
  MUST be `instrument` — `cmake/KratePlugin.cmake:130–132` hard-errors on anything else, and
  `:197–206` selects the instrument AUv3 storyboard from it. *(Roadmap line 338, 375.)*
- **FR-005** `smtg_target_configure_version_file`, `smtg_target_add_plugin_resources(… RESOURCES
  resources/editor.uidesc)`, `krate_plugin_install_to_system`, `krate_plugin_install_presets` and
  `krate_plugin_set_warnings` MUST all be called, matching `plugins/ruinae/CMakeLists.txt:98–134`.
- **FR-006** `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()` MUST be present
  (`plugins/ruinae/CMakeLists.txt:139–141`).
- **FR-007** `src/version.h`, `resources/win32resource.rc` and `resources/auv3/audiounitconfig.h` MUST
  be treated as **generated** (`cmake/KratePlugin.cmake:83–105`). Only `auv3/audiounitconfig.h.in` is
  authored. No file in this phase may hand-edit a generated file. *(Roadmap lines 363–366.)*
- **FR-007a** Roadmap line 365's other half — *"commit **only** `audiounitconfig.h.in`"* — is enforced by
  `.gitignore`, which carries exactly three lines per plugin at `.gitignore:58–75`
  (`/plugins/<name>/resources/win32resource.rc`, `/plugins/<name>/src/version.h`,
  `/plugins/<name>/resources/auv3/audiounitconfig.h`, for all six existing plugins). `.gitignore` MUST
  gain the matching Seraphis trio:
  ```
  /plugins/seraphis/resources/win32resource.rc
  /plugins/seraphis/src/version.h
  /plugins/seraphis/resources/auv3/audiounitconfig.h
  ```
  Without them the first `cmake --preset` leaves three generated files untracked-and-committable, and a
  later version bump dirties them — breaking the project rule that a version bump touches `version.json`
  + `CHANGELOG.md` only. Verified by SC-020.
- **FR-008** The directory skeleton MUST be **exactly the file list below**. It is the roadmap
  skeleton (lines 336–360) with the Phase-8 deferrals made explicit, because the roadmap skeleton lists
  files that roadmap 8.3 (lines 396–398) and this spec's Scope §4 do not ship — the skeleton block and
  8.3 contradict each other, and 8.3 (which names the two parameter packs) is binding. Files marked
  *(deferred)* MUST NOT be created in this phase.

  ```
  plugins/seraphis/
    CMakeLists.txt  CLAUDE.md  CHANGELOG.md  README.md  version.json
    src/
      entry.cpp  plugin_ids.h  version.h(GENERATED)
      processor/    processor.h  processor.cpp
      controller/   controller.h  controller.cpp
      parameters/   global_params.h  macro_params.h
      engine/       seraphis_engine_config.h
      preset/       seraphis_preset_config.h
      update/       seraphis_update_config.h
      ui/.gitkeep                                # EMPTY (Phase 11) — the .gitkeep IS the content
    resources/
      editor.uidesc  au-info.plist  win32resource.rc(GENERATED)
      auv3/audiounitconfig.h.in  auv3/audiounitconfig.h(GENERATED)
      auv3/macOS/Seraphis.entitlements
      presets/Textures/.gitkeep
    tests/
      CMakeLists.txt  vstgui_test_stubs.cpp  seraphis_test_fixture.h
      unit/test_main.cpp
      unit/param_denorm_test.cpp  unit/state_roundtrip_test.cpp  unit/processor_bus_test.cpp
      unit/midi_event_test.cpp    unit/lifecycle_test.cpp
      unit/controller/editor_lifecycle_test.cpp
      integration/processor_audio_test.cpp  integration/param_flow_test.cpp
    docs/.gitkeep                                # DIRECTORY ONLY — Phase 12 authors index.html (FR-080)
    installers/windows/setup.iss  installers/linux/README.txt
  ```

  **AMENDED 2026-07-31 (A1).** Four entries were added to this list during implementation, because
  FR-008 binds it as *exactly* the shipped skeleton and the tree ships them:
  - `tests/seraphis_test_fixture.h` — the header-only `ProcessorFixture` (`:157`) plus the
    `MultiPointParamValueQueue` (`:48`) / `ParameterChanges` (`:111`) host doubles that every test case
    in FR-066's table is written against: `prepare()` (`:178`), `setParam` / `setParamPoints`
    (`:204`, `:210`), `withOutputChannels(n)` (`:255`), `processBlock` (`:291`), `seedOutputBuffers(v)`
    (`:311`), `checkCanaries()` (`:319`) and the two `renderBlocks` overloads (`:337`, `:361`). It is
    test infrastructure, not a plugin TU: it is **not** added to FR-060's second-compilation list and is
    included only from `plugins/seraphis/tests/**`.
  - `src/ui/.gitkeep`, `docs/.gitkeep`, `resources/presets/Textures/.gitkeep` — git tracks files, not
    directories, so FR-056 ("`src/ui/` MUST exist and be empty"), FR-080's `docs/` directory and
    FR-051's `presets/Textures/` are each unrepresentable in a clone without the placeholder. Their
    absence would also make SC-020's clean-tree check pass while the directories silently did not exist.

  No file was **removed** from the list, and no *(deferred)* entry was created.

  **Deferred roadmap-skeleton entries, with their owner:**

  | Roadmap skeleton entry | Roadmap line | Deferred to | Why |
  |---|---|---|---|
  | `parameters/cloud_params.h`, `body_params.h`, `atmosphere_params.h`, `aether_params.h`, `life_mod_params.h`, `dropdown_mappings.h` | 344–345 | **Phase 9** | Roadmap 8.3 (line 397–398): *"Phase 8 ships `global_params.h` … and `macro_params.h` … only — the rest are Phase 9."* Creating six empty headers no FR describes is dead weight. |
  | `processor/processor_params.cpp`, `processor/processor_state.cpp` | 342 | **created only on demand** | Roadmap line 368: the split is adopted *"when a file passes ~1500 lines"*. With two parameter packs, `processor.cpp` will not reach it. If it does, the split is in scope and the extra TUs are added to FR-060's second-compilation list. |
  | `controller/controller_view_sync.cpp` | 343 | **Phase 11** | It exists in Ruinae to sync custom views; Seraphis registers no custom views until Phase 11 (FR-018, FR-056). |
  | `controller/parameter_helpers.h` (plugin-local) | 343 | **created only on demand** | `plugins/ruinae/src/controller/parameter_helpers.h` is a real file and is **not** the same file as `plugins/shared/src/ui/parameter_helpers.h` (which FR-048 uses). Phase 8 needs only the shared one; a plugin-local copy is added only when a Seraphis-specific helper appears. |

  Additional files that are **not** in the roadmap skeleton but ARE required: `installers/windows/setup.iss`
  and `installers/linux/README.txt` (consumed by `.github/workflows/release.yml:235, 372`), `CHANGELOG.md`
  (consumed by `release.yml:225, 378` and `tools/check-changelog-coverage.js`), `README.md`.
- **FR-009** A `plugins/seraphis/CLAUDE.md` leaf MUST be added following the shape of
  `plugins/ruinae/CLAUDE.md` / `plugins/membrum/CLAUDE.md`: type, src skeleton, param-ID scheme, test
  target invocation, pluginval path. *(Roadmap line 430.)*
  It MUST additionally record two decisions that outlive this phase and have no other home in the tree:
  1. **MPE / note expression is a known Phase 9 decision.** Phase 8 ships **no** `INoteExpressionController`
     (FR-018). The leaf MUST spell out the caveat that motivates recording it: adding an interface to a
     **released** controller FUID can invalidate host-cached class metadata, so Phase 9 must make the call
     knowingly rather than discover it.
  2. **Preset categories are additive-only.** `Textures` (FR-050, FR-051) is a *seed*, not a placeholder to
     be replaced: Phase 12 EXTENDS the category list and MUST NOT rename a shipped category, because a
     rename orphans every preset saved against it (the Membrum lesson, roadmap line 388). Both the
     filesystem subdirectory and the XML metadata carry the name, and they must always agree.
- **FR-010** `CHANGELOG.md` MUST contain a `## [0.1.0]` section describing the scaffold, so
  `tools/check-changelog-coverage.js` (which parses `^##\s*\[([^\]]+)\]`, `:84–88`) finds a matching entry.

### B. Identity and per-plugin conventions

- **FR-011** `src/plugin_ids.h` MUST declare `static const Steinberg::FUID kProcessorUID(…)` and
  `kControllerUID(…)` with two freshly generated GUIDs that differ from all twelve existing FUIDs listed
  in the New-components section. *(Roadmap line 373.)*
- **FR-012** `src/plugin_ids.h` MUST declare `constexpr Steinberg::int32 kCurrentStateVersion = 1;` at
  namespace scope, referenced by both processor and controller with no cross-include between them.
  *(Roadmap lines 386–387; model `plugins/ruinae/src/plugin_ids.h:20`.)*
- **FR-013** `src/plugin_ids.h` MUST declare `enum ParameterIDs : Steinberg::Vst::ParamID` containing
  exactly the eight IDs in the parameter table above, and MUST document the full reserved range map as a
  comment. *(Roadmap lines 383–386.)*
- **FR-014** `src/plugin_ids.h` MUST declare `kSubCategories` for the factory (instrument category
  string), consumed by `DEF_CLASS2` as `plugins/ruinae/src/entry.cpp:57` does.
- **FR-015** `resources/auv3/audiounitconfig.h.in` MUST be a **complete copy of
  `plugins/membrum/resources/auv3/audiounitconfig.h.in:1–39`** with `Membrum`→`Seraphis` and
  `Mbrm`→`Srph` substituted. The whole file is required, not just the five value macros: the AUv3 target
  built by `krate_plugin_platform_setup` (`cmake/KratePlugin.cmake:208–219`) also consumes the **unquoted
  token forms** and the trailing flags/delegate defines, and a header containing only the five values
  does not compile the macOS AUv3 target. The required defines, verbatim from the model file:

  | Define | Model line | Seraphis value |
  |---|---|---|
  | `kAUcomponentType` | :8 | `'aumu'` |
  | `kAUcomponentType1` | :9 | `aumu` (unquoted token) |
  | `kAUcomponentSubType` | :12 | `'Srph'` |
  | `kAUcomponentSubType1` | :13 | `Srph` (unquoted token) |
  | `kAUcomponentManufacturer` | :16 | `'KrAt'` |
  | `kAUcomponentManufacturer1` | :17 | `KrAt` (unquoted token) |
  | `kAUcomponentDescription` | :19 | `AUv3WrapperExtension` |
  | `kAUcomponentName` | :20 | `Krate Audio: Seraphis` |
  | `kAUcomponentTag` | :21 | `Synthesizer` |
  | `kAUcomponentVersion` | :24 | `@AU_COMPONENT_VERSION@` |
  | `kSupportedNumChannels` | :35 | `02` |
  | `kAUcomponentFlags` | :37 | `0` |
  | `kAUcomponentFlagsMask` | :38 | `0` |
  | `kAUapplicationDelegateClassName` | :39 | `AppDelegate` |

  The `02` value MUST carry the digit-pair rationale comment (model `:26–34`). *(Roadmap lines 374–380.)*
- **FR-016** `resources/au-info.plist` MUST declare exactly one `AudioComponents` entry
  (`factoryFunction AUWrapperFactory`, `type aumu`, `subtype Srph`, `manufacturer KrAt`) and exactly one
  `AudioUnit SupportedNumChannels` dict with `Inputs 0` / `Outputs 2`, matching
  `plugins/membrum/resources/au-info.plist:23–50`. A mismatch between this and the bus configuration is
  the documented `-10875` AU-init failure. *(Roadmap lines 377–380.)*
- **FR-017** `resources/auv3/macOS/Seraphis.entitlements` MUST exist (referenced by
  `krate_plugin_platform_setup`'s `ENTITLEMENTS` argument at `cmake/KratePlugin.cmake:213`).
- **FR-018** `src/entry.cpp` MUST register exactly two classes via `BEGIN_FACTORY_DEF` /
  two `DEF_CLASS2` / `END_FACTORY`, with `#define stringPluginName "Seraphis"`, following
  `plugins/ruinae/src/entry.cpp:40–78`. It MUST **not** include any `ui/*.h` header (Ruinae's
  `entry.cpp:19–32` includes them only to trigger custom `ViewCreator` static registration; Seraphis has
  no custom views until Phase 11). *(Roadmap line 349.)*
- **FR-019** *(MPE / note expression — decided out of scope)*. `Controller` MUST NOT implement
  `INoteExpressionController` in Phase 8, and the plugin MUST NOT declare any note-expression types. The
  event-input bus (FR-020) is the whole note surface. This is not a deferral for convenience: the engine's
  note API is `noteOn(std::uint8_t note, std::uint8_t velocity)` / `noteOff(std::uint8_t note)`
  (`seraphis_engine.h:370, 415`) with **no** per-note expression input at all, so an implemented
  `INoteExpressionController` would have nothing to drive and a declared-but-empty one would assert
  nothing while adding a `queryInterface` branch. The reconsideration belongs to Phase 9, once the
  parameter surface exists — with the host-cache caveat recorded in `plugins/seraphis/CLAUDE.md` (FR-009).
  *(Roadmap line 514, the roadmap's own "Phase 8/9 scope call", resolved here as Phase 9.)*

### C. Buses and the audio path

- **FR-020** `Processor::initialize()` MUST call `addEventInput(STR16("Event In"))` and
  `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)` and MUST NOT call `addAudioInput()`.
  *(Roadmap lines 376–380; model `plugins/membrum/src/processor/processor.cpp:117, 120`; anti-model
  `plugins/ruinae/src/processor/processor.cpp:56`.)*
- **FR-021** `Processor` MUST override `setBusArrangements` to return `kResultFalse` for **all three** of:
  (a) `numIns != 0`; (b) `numOuts != 1`; (c) any output arrangement other than `SpeakerArr::kStereo`.
  Clause (b) is the bound the model carries — `plugins/membrum/src/processor/processor.cpp:1058–1059`
  rejects `numOuts < 0 || numOuts > kMaxOutputBuses`; Seraphis has exactly **one** output bus (FR-020), so
  its bound is `numOuts != 1`. Without it a host proposing two stereo output buses gets `kResultTrue` for
  a bus that does not exist. Rationale for (c) is the model's own
  (`plugins/membrum/src/processor/processor.cpp:1045–1069`): the render path reads `channelBuffers32[0]`
  and `[1]`, so accepting a mono arrangement invites an out-of-bounds read on the audio thread.
  **Rejecting here is not the guard** — a host may ignore `kResultFalse` and present a mono bus anyway —
  so `process()` carries an independent `numChannels < 2` early-out (FR-030 rule 1, amendment A10).
- **FR-022** `Processor` MUST own one `SeraphisEngine`, one `AetherReverb` and one
  `SeraphisMacroMatrix`. The engine and reverb MUST be held behind `std::unique_ptr` members constructed
  once in `initialize()` (non-RT), **never** as by-value members and never constructed on a stack frame.
  `seraphis_engine.h:119–122` states the object is *"several hundred KB of storage … It must NEVER be a
  test local — MSVC's default main-thread stack is 1 MiB"*, and `:159–164` records the measured
  `sizeof(SeraphisEngine)` as **771 968 B**.
- **FR-023** `Processor::setupProcessing()` MUST call `engine->prepare(setup.sampleRate, cfg)` and
  `reverb->prepare(setup.sampleRate, reverbCfg)` with the configs from
  `engine/seraphis_engine_config.h`. `prepare()` is the only allocating path
  (`seraphis_engine.h:195–201`, `aether_reverb.h:1610–1614`) and MUST NOT be reachable from
  `process()`. Four prepare-time rules are binding, in this order:
  1. **Block bound is the constant 2048.** Both configs MUST be built with
     `maxBlockSamples = SeraphisEngine::kMaxBlockSamples` (2048, `seraphis_engine.h:134`) — **not**
     `setup.maxSamplesPerBlock`, clamped or otherwise. One documented constant then governs the voice
     config (`SeraphisVoiceConfig::maxBlockSamples`, `seraphis_voice.h:105–120`), the reverb config
     (`AetherReverb::PrepareConfig::maxBlockSamples`, `aether_reverb.h:1581`, whose own clamp is
     `[64, 8192]` at `:1619`), FR-026's slice bound and FR-028's scratch size, so those four can never
     disagree and the render is block-size invariant *independently of what the host declared at prepare*.
     The cost is ~32 KB of scratch even for a 64-sample host, which is accepted noise beside the 33.6 MB
     of capture rings the edge-case list already accepts.
  2. **Polyphony is seeded from the parameter, not from the struct default.** `cfg.polyphony` MUST be the
     **current denormalized value of the `polyphony` atomic** (FR-043), not `SeraphisEngineConfig`'s
     default 8 (`seraphis_engine.h:95`). `setState()` may legally arrive **before** `setupProcessing()`, so
     preparing at 8 would run the first block at the wrong voice count whenever a preset says otherwise.
  3. **The "last pushed polyphony" tracker is reset here** to the same value written into `cfg.polyphony`
     (FR-024a clause 1), so engine and parameter agree from the first sample and SC-019 clause 3's
     "re-pushing the same value must not re-call `setPolyphony`" has a defined starting state.
  4. **No latency announcement — the reported latency is invariant.** *(AMENDED 2026-07-31, A2: the
     previous clause required `restartComponent(kLatencyChanged)` on a change. It is **deleted**, not
     relaxed — Phase 8 ships no code path that can change the reported value, so the requirement was
     unimplementable and unobservable.)* `setupProcessing()` MUST NOT call `restartComponent`, MUST NOT
     keep a "last reported latency" tracker, and MUST NOT query the host context for an
     `IComponentHandler`. Two facts, both read from the SDK/DSP headers, force this:
     - `AetherReverb::getLatencySamples()` returns `spectralEnabled_ ? diffusionFftSize_ : 0`
       (`aether_reverb.h:2607–2613`) with `diffusionFftSize_ = 1024` and `spectralEnabled_ = true` as
       **member defaults**, so a default-constructed, *unprepared* reverb already reports 1024. FR-053
       pins both for the shipped config, so the value is 1024 from the moment `initialize()` constructs
       the reverb — before the first prepare, after every prepare, at every sample rate, and after every
       one of the eight shipped parameters. There is no transition to announce.
     - A `Steinberg::Vst::AudioEffect` has **no route to an `IComponentHandler`**: the handler is
       delivered only to the edit controller, via `IEditController::setComponentHandler`
       (`vsteditcontroller.h:59`), stored at `:108`, read through `getComponentHandler()` at `:97`. The
       tempting substitute `FUnknownPtr<IComponentHandler>(ComponentBase::getHostContext())` is
       **forbidden**: the `FUnknown*` a host passes to `IComponent::initialize` is an `IHostApplication`
       (the SDK's own `HostApplication`, which `seraphis_tests` links, implements *only* that), so the
       query returns null in every real host and in the test target alike.

     Hosts obtain the value the way VST3 specifies for a constant latency: by calling
     `getLatencySamples()` after `setupProcessing()`. **If a later phase makes the latency variable**
     (a spectral-diffusion on/off parameter, or a `diffusionFftSize` parameter), the announcement MUST be
     added *then*, on the SDK-sanctioned route — processor → `IConnectionPoint`/`IMessage` → controller →
     `getComponentHandler()->restartComponent(kLatencyChanged)` (`vsteditcontroller.h:97`) — and **never**
     via a `getHostContext()` query. Verified by SC-013 (clause 4 is now an invariance matrix). See FR-033.
- **FR-024** `Processor::process()` MUST reproduce the slice body of
  `tests/test_helpers/seraphis_chain.h`, extended with the **global-parameter push** the helper does not
  model (the helper takes no `GlobalParams`), in this order, per slice:
  0. **push `GlobalParams` onto the chain** — see FR-024a; must run before the engine renders;
  1. dispatch host events due at the slice start;
  2. `macroMatrix.apply(*engine)` and push all eight `computeAetherTargets()` fields onto the reverb via
     `setMix / setSize / setWidth / setShimmerOctaveSend / setShimmerFifthSend / setBloomSend /
     setSizeBreathDepth / setDimensionalityTideDepth`;
  3. `engine->processStereoBlock(dryL, dryR, n)`;
  4. `reverb->processStereoBlock(dryL, dryR, wetL, wetR, n)`;
  4b. **apply master gain** to `wetL`/`wetR` — see FR-024a clause 3;
  5. `engine->processOutputStage(wetL, wetR, n)` **in place on the reverb return**;
  6. consume `engine->consumeBloomEvents()` and drive `reverb->bloomNoteOff(v)` for every set
     `noteOffMask` bit, then `engine->collectHeldPartials(v, buf, cap, count)` +
     `reverb->bloomNoteOn(v, buf, count)` for every set `noteOnMask` bit — note-offs **before**
     note-ons, as `seraphis_chain.h` does.

  **Silence flags (once per `process()` call, not per slice).** On every **non-degenerate** render — i.e.
  every path that is not one of FR-030's early-outs — `process()` MUST set
  `data.outputs[0].silenceFlags = 0`. Leaving the SDK default means the flags are whatever the host left
  in the struct, and Seraphis carries an `AetherReverb` whose decay reaches "infinite" by design: a host
  reading a stale "silent" flag may cut the reverb tail the moment the last voice releases. The bus model
  writes the flags for the same reason (`plugins/membrum/src/processor/processor.cpp:707, 762`). Seraphis
  writes only the clearing half on a **rendered** block — it never *asserts* silence about a block it
  actually rendered — because deciding when the instance is genuinely quiet needs a "reverb has decayed"
  predicate that Phase 8 has no criterion for. *(A10 records the one place `silenceFlags` is set to `3`:
  FR-030 rule 3's not-ready path, where the processor has just zero-filled the buffers itself and so
  knows both channels are silent. That is a statement of fact, not a decay prediction.)*
  Correspondingly, `getTailSamples()` MUST be left at the SDK default in Phase 8; **tail and idle
  reporting are deferred to the phase that owns the effects roster (Phase 10)**, together with the
  predicate they require. Asserted by SC-021.

- **FR-024a** *(the three shipped global parameters MUST reach the chain — none of them is decorative)*.
  `SeraphisEngine` has **no gain surface at all**: its complete public API
  (`dsp/include/krate/dsp/systems/seraphis_engine.h:201–730`) is `prepare / reset / silence /
  setPolyphony / setSeed / noteOn / noteOff / processStereoBlock / processOutputStage /
  setAtmosphereFreeze / setOutputSaturation / collectHeldPartials / consumeBloomEvents` plus getters —
  `grep -niE "setgain|masterGain|outputTrim|setLevel|setOutputGain" seraphis_engine.h` → **0 hits**
  (run this session). Master gain is therefore necessarily a **wrapper-side multiply**, and the spec must
  say where it goes.
  1. **Polyphony.** The denormalized value (FR-043) MUST be pushed via `engine->setPolyphony(n)`
     (`seraphis_engine.h:321`) **only when it differs from the last pushed value** — the edge case under
     "Parameter extremes" records why re-calling it every block is wrong. Observable via
     `SeraphisEngine::getPolyphony()` (`:665`). The "last pushed" tracker is **reset in
     `setupProcessing()` to the same value written into `cfg.polyphony`** (FR-023 clauses 2–3), never to a
     hard-coded 8 and never to a force-push sentinel: prepare has already delivered that voice count to
     the engine, so the first `process()` after prepare must **not** re-call `setPolyphony` — which is
     exactly the redundant call this clause exists to avoid, and would re-arm the voice-sum smoother
     (`sumGain_.setTarget(...)`, `seraphis_engine.h:349`) on every host prepare. Verified by SC-019.
  2. **Soft limit.** The denormalized bool MUST be pushed via `engine->setOutputSaturation(...)`
     (FR-044), again **on change only**.
  3. **Master gain.** MUST be applied as a **per-sample multiply with a smoother** (a zipper-free ramp
     toward the target; the raw atomic MUST NOT be applied as a per-block step) to the **wet buffers,
     after step 4 and before step 5**. The smoother is fully specified — it mirrors
     `AetherReverb::applyControl` (`aether_reverb.h:2950–2958`), the pattern already proven inside the
     reverb this processor drives:
     - **Type:** `Krate::DSP::OnePoleSmoother` (`dsp/include/krate/dsp/primitives/smoother.h:134`),
       configured at `setupProcessing()` via `configure(kMasterGainSmoothMs, sampleRate)` (`:160`).
     - **Time constant:** a named `static constexpr float kMasterGainSmoothMs = 20.0f` declared in
       `src/engine/seraphis_engine_config.h` (FR-053) with a provenance comment naming its sibling —
       `SeraphisEngine::kSumGainSmoothMs = 20.0f` (`seraphis_engine.h:138`), the same 20 ms family. It MUST
       NOT be an unnamed literal at the use site.
     - **Cadence:** advanced **once per sample** via `process()` (`smoother.h:197`), never once per slice
       or once per block. This is what keeps SC-008 clean: a ramp advanced per slice is partition-dependent
       by construction, so it would fail block-size invariance across `{1, 7, 64, 65, 512, 2048, 4096}`
       for a correct implementation.
     - **Initial value:** **snapped**, not ramped, on the first `process()` after
       `setupProcessing()`/`setActive(true)` — `snapTo(currentTarget)` (`smoother.h:263`) guarded by a
       "no samples processed since prepare" seam, exactly as `applyControl` guards on
       `prepared_ && !anySamplesProcessed_`. The target snapped to is the **current value of the
       `masterGain` atomic**, not the constructed or default gain. Without the snap, a render at
       `kMasterGainId = 0.0` would ramp down from the previous value and its first milliseconds would be
       non-zero, failing SC-019 clause 1 for a correct implementation; **with** the snap, SC-019 clause 1's
       "peak `< 1e-6` over the whole 4 s render" holds literally, with no "after the first N ms" escape
       hatch. SC-008's block-size invariance is unaffected either way.

     Consequences of the *placement*, all of which are load-bearing:
     - the gain is **pre-saturator and pre-limiter**, so `processOutputStage`'s
       `limiter_.processBlock(l, r, n)` (`seraphis_engine.h:521`, `TruePeakLimiter::kDefaultCeilingDb =
       -1.0f` ⇒ `ceilingLin_ = 0.8912509f`, `true_peak_limiter.h:46, 168`) remains the **last** stage and
       SC-006's `≤ 0.891` bound is achievable at master gain 2.0;
     - a post-limiter multiply is **forbidden**: at master gain 2.0 it would produce peaks up to ~1.78 and
       make SC-006 unsatisfiable by construction.
- **FR-025** The host block MUST be sub-divided at every event's `sampleOffset` so events take effect
  sample-accurately: `SeraphisEngine::noteOn` / `noteOff` (`seraphis_engine.h:370, 415`) have no
  sample-offset parameter, and sub-division is the only way to deliver one. This is timing rule 1 in
  `tests/test_helpers/seraphis_chain.h`'s banner, which states it is *"exactly what Phase 8's event loop
  does with the host's sampleOffset"*.
- **FR-026** No slice handed to `engine->processStereoBlock` or `reverb->processStereoBlock` may exceed
  **`SeraphisEngine::kMaxBlockSamples` (2048, `seraphis_engine.h:134`)**. This is a **single** bound, not
  two: FR-023 clause 1 passes that same constant as the `maxBlockSamples` of both configs, so the engine's
  ceiling and the `AetherReverb::PrepareConfig` value (`aether_reverb.h:1581`) are the same number by
  construction and cannot drift apart. The processor MUST sub-divide any host block larger than 2048 —
  the branch SC-008's 4096 partition exercises, at every prepare and independently of what the host
  declared as `maxSamplesPerBlock`.
- **FR-027** The processor MUST NOT copy `processOutputStage`'s internal 64-sample loop as a block-size
  requirement. `seraphis_engine.h:506–511` states explicitly: *"The 64-sample loop around the saturator
  is a CADENCE CHOICE, NOT A SIZE CONSTRAINT … Phase 8 must not copy the loop as if it were a
  requirement."*
- **FR-028** All per-block scratch buffers (dry L/R, wet L/R, the `kBloomPartialCap`-sized partial
  buffer, the resolved-event index array) MUST be sized once at `setupProcessing()` and never resized in
  `process()`. The four audio scratch buffers MUST be sized to **`SeraphisEngine::kMaxBlockSamples`
  (2048)** — the same constant FR-023 clause 1 passes to both configs and FR-026 bounds slices by — and
  **not** to `setup.maxSamplesPerBlock`, so a host that exceeds its own declared block size cannot
  overrun them. `SeraphisEngine::kBloomPartialCap` is 32 (`seraphis_engine.h:154`), so a
  `std::array<float, 32>` member suffices for the partial buffer.
- **FR-029** `process()` MUST perform no allocation, no locking, no exception handling and no I/O, and
  MUST enter a `ScopedDenormalMode` (or equivalent FTZ/DAZ scope) at its top rather than setting MXCSR in
  `setupProcessing()` — `plugins/membrum/src/processor/processor.cpp:1073–1075` records that MXCSR is
  per-thread and that setting it on the setup thread never reaches the audio thread.
- **FR-030** `process()` MUST handle `data.numInputs == 0`, `data.numOutputs == 0`,
  `data.numSamples == 0`, a null `data.outputs[0].channelBuffers32`, a null `channelBuffers32[0]` or
  `[1]`, **a mono output bus (`data.outputs[0].numChannels < 2`)**, and a call arriving **before**
  `setupProcessing()` by producing silence and returning `kResultOk`, without touching the chain.
  *(AMENDED 2026-07-31, A10: the mono clause, the guard ordering and the two silence rules below were
  added — all are strengthenings; nothing was removed.)*
  Three things are binding and were previously left to interpretation:
  1. **The mono early-out is a separate guard, and `setBusArrangements` is NOT it.** A host is free to
     ignore FR-021's `kResultFalse` and still present a 1-channel bus, in which case `channelBuffers32`
     is a **one-element** array and `channelBuffers32[1]` is an out-of-bounds heap read — followed by
     *writes* through the resulting garbage pointer in FR-024 step 4b and the output copy. `process()`
     MUST therefore carry its own `if (data.outputs[0].numChannels < 2) return kResultOk;`, on the model
     of `plugins/ruinae/src/processor/processor.cpp:430`. The rest of this processor is modelled on
     Membrum, which has **no** `numChannels` check anywhere in its process path — copying Membrum here
     would copy the gap.
  2. **Guard order is load-bearing.** Buffer *validation* (`numOutputs > 0` and `outputs != nullptr`,
     then `channelBuffers32 != nullptr`, then `numChannels >= 2`, then `numSamples > 0`, then the two
     channel pointers) precedes the *readiness* check (`prepared_ && engine_ && reverb_`). Nothing may
     read `data.outputs[0]` until `numOutputs > 0 && outputs != nullptr` is established, and only after
     validation is a writable buffer known to exist for rule 3 to fill.
  3. **"Producing silence" means WRITING zeros, not returning early.** On the one degenerate case that
     has a valid writable buffer — `process()` before `setupProcessing()`, or with a released
     engine/reverb — the processor MUST `std::fill_n` both channels with `0.0f` **and** set
     `data.outputs[0].silenceFlags = 3` (both channels genuinely are silent). VST3 does **not** guarantee
     zeroed output buffers, so returning without writing hands the host back the previous plug-in's or
     previous block's content. Both wrapped components zero-fill on their own not-prepared paths
     (`seraphis_engine.h:448–451`, `aether_reverb.h:2172–2176`); the wrapper matches them.
- **FR-031** `Processor` MUST translate host MIDI events to the engine: `kNoteOnEvent` with
  `velocity > 0` → `noteOn(pitch, mapNoteOnVelocity(velocity))`; `kNoteOnEvent` with `velocity <= 0` and
  `kNoteOffEvent` → `noteOff(pitch)`; anything else ignored. Note numbers and velocities are
  `std::uint8_t` per `seraphis_engine.h:370, 415`, and the pitch MUST be range-checked into `[0, 127]`
  before the cast.
  **The velocity mapping is exactly** *(AMENDED 2026-07-31, A3 — the withdrawn form was the bare
  `velocity*127`)*:
  ```cpp
  static_cast<std::uint8_t>(std::clamp(velocity * 127.0f + 0.5f, 1.0f, 127.0f))
  ```
  i.e. **round-to-nearest with a floor of 1** for any strictly positive velocity. A truncating
  `uint8(velocity * 127)` is wrong, not merely imprecise: a legal VST3 velocity of e.g. `0.003` truncates
  to `0`, and `SeraphisEngine::noteOn` routes velocity `0` to `noteOff`
  (`seraphis_engine.h:374–377`) — so the note-on would *release* a voice instead of allocating one,
  contradicting SC-022 clause 1. The upper clamp is `127`, not `128`: `velocity = 1.0f` gives
  `127.5 → 127`. This is a **strengthening** — it removes a reachable wrong behaviour and adds a
  detector for it (SC-022 sub-clause 6); no threshold moved.
- **FR-032** `Processor::setActive(false)` MUST call `engine->silence()` (`seraphis_engine.h:308`) and
  `reverb->reset()` (`aether_reverb.h:1971`) so a deactivated instance leaves no ringing tail, and
  `setActive(true)` MUST NOT allocate.
- **FR-033** `Processor::getLatencySamples()` MUST return `reverb->getLatencySamples()`
  (`aether_reverb.h:2612`) and MUST return a stable value between `setupProcessing()` calls — the
  reverb's own banner (`:2607–2612`) notes that no setter changes it. *(Traces to roadmap line 433:
  pluginval strictness 5 clean; a mis-reported latency is a strictness-5 finding.)*
  For the shipped configuration this value is **exactly 1024 samples** and is **constant at every sample
  rate** (≈ 21.3 ms @ 48 kHz, ≈ 23.2 ms @ 44.1 kHz — the *time* varies, the *sample count* does not).
  `getLatencySamples()` returns `spectralEnabled_ ? diffusionFftSize_ : 0` (`aether_reverb.h:2610–2613`)
  and FR-053 ships the Phase 6 defaults `spectralDiffusionEnabled = true`, `diffusionFftSize = 1024`
  (`aether_reverb.h:1584–1585`). Seraphis reports honest latency on an instrument and lets the host
  compensate; shipping `spectralDiffusionEnabled = false` to dodge the report is **rejected**, because it
  deviates from the Phase 6 default FR-053 says to inherit — silently removing the "underwater chamber"
  character — and Phase 9/10 would then have to re-enable it, i.e. change the latency of an
  already-released plugin.
  **No announcement — the value never changes.** *(AMENDED 2026-07-31, A2.)* The withdrawn text claimed
  *"the reported value is 0 before the first prepare and 1024 after it"*, and required
  `restartComponent(kLatencyChanged)` for that transition. **The premise is false.**
  `spectralEnabled_` and `diffusionFftSize_` are **member initializers** (`aether_reverb.h:4465, :4467`
  per the plan's read; the getter at `:2607–2613`), so an unprepared `AetherReverb` already reports
  **1024**, and FR-053 pins the shipped config to exactly those values. For Seraphis the reported latency
  is therefore the **invariant 1024** from `initialize()` onwards — the only other value reachable at all
  is the `0` a `Processor` reports between construction and `initialize()`, or after `terminate()`, when
  no `reverb_` exists (no host can observe a render in that window).
  Consequently `Processor::getLatencySamples()` is the **whole** of FR-033 in Phase 8:
  `return reverb_ ? static_cast<uint32>(reverb_->getLatencySamples()) : 0u;`. FR-023 clause 4 records the
  deletion, the SDK reason an `AudioEffect` cannot announce anything, and the mandated route
  (processor → `IMessage` → controller → `getComponentHandler()->restartComponent`) for the phase that
  first makes the latency variable. Asserted by SC-013, whose clause 4 is now an invariance matrix
  measuring strictly more real behaviour than a fabricated-host flag count could.
- **FR-034** The `SeraphisMacroMatrix` instance MUST be applied every slice even in Phase 8, at its
  default (neutral) values. `SeraphisMacroValues`' defaults are the documented FR-060 neutral
  (`seraphis_macro_matrix.h:121–128`), and `computeAetherTargets()` is what pushes the reverb's eight
  controls to their documented defaults (`:110–119`); skipping step 2 would leave the reverb at
  whatever its own constructor set. The Phase 8 macro *parameters* remain inert (FR-041).
- **FR-034a** *(making FR-034 verifiable — and recording why the obvious test is vacuous)*. At Phase 8's
  neutral macro defaults the eight `computeAetherTargets()` values are **numerically identical to the
  reverb's own constructor defaults**: `mix 0.35` = `kDefaultMix` (`aether_reverb.h:2336`),
  `size 0.50` = `kDefaultSize` (`:2207`), `width 1.0` = `kDefaultWidth` (`:2333`), the three sends `0.0`
  = `kDefaultSend` (`:2280`, `:2290`, `:2295`), `sizeBreathDepth 0.20` = `kDefaultSizeBreathDepth`
  (`:2749`), `dimensionalityTideDepth 0.20` = `kDefaultTideDepth` (`:2750`). A "render with step 2
  omitted" control is therefore **provably bit-identical** and cannot verify anything. `AetherReverb`
  also exposes **no getter** for any of the eight (`grep '\[\[nodiscard\]\].*get[A-Z]' aether_reverb.h` →
  `getMatrixOrthogonalityError, getEffectiveDelayLengthSamples, getModalDensityPerHz, getMaxSizeScale,
  getCurrentMorphPosition, getStateEnergy, getActiveBloomResonatorCount, getNonFiniteRecoveryCount,
  getLatencySamples` — none of them a control read-back).
  Therefore FR-024 step 2 MUST be factored into a named free function in
  `src/engine/seraphis_engine_config.h`:
  ```cpp
  inline void applyAetherTargets(Krate::DSP::AetherReverb& reverb,
                                 const Krate::DSP::SeraphisAetherTargets& t) noexcept;
  ```
  called by `process()` and directly unit-testable with **non-neutral** targets. This is the only shape
  in which FR-034 has teeth in Phase 8. Verified by SC-024.

### D. Parameters and state

- **FR-040** `src/parameters/global_params.h` MUST follow the Ruinae contract exactly:
  `struct GlobalParams` of `std::atomic<>` fields, plus `handleGlobalParamChange`,
  `registerGlobalParams`, `formatGlobalParam`, `saveGlobalParams`, `loadGlobalParams` and
  `loadGlobalParamsToController` — the six functions at `plugins/ruinae/src/parameters/global_params.h:39,
  87, 130, 178, 187, 220`. It MUST carry exactly `masterGain`, `polyphony` and `softLimit`.
  *(Roadmap line 397.)*
- **FR-041** `src/parameters/macro_params.h` MUST define, with **explicit initializers**:
  ```cpp
  struct MacroParams {
      std::atomic<float> dream{0.0f}, bloom{0.0f}, dissolve{0.0f}, gravity{0.5f}, entropy{0.0f};
  };
  ```
  (a 5-element array with the same five initializers is equally acceptable — the in-repo model
  `plugins/ruinae/src/parameters/macro_params.h:14` writes
  `std::atomic<float> values[4] = {0.0f, 0.0f, 0.0f, 0.0f};`). The initializers are **load-bearing**:
  default value-initialization would leave `gravity` at `0.0f`, contradicting both the parameter table
  (`kMacroGravityId` default normalized `0.5`) and `SeraphisMacroValues::gravity = 0.5f`
  (`seraphis_macro_matrix.h:126`) — the FR-060 documented neutral — so a `getState()` taken before any
  parameter change would stream `gravity = 0.0` while the controller reports `0.5`. Verified by SC-010's
  default-state clause.
  The struct MUST carry the same six functions as the global pack, and MUST be **inert**: no code in
  Phase 8 reads `MacroParams` and writes it into `SeraphisMacroMatrix`. Roadmap line 398:
  *"macro_params.h (the five macros as inert 0–1 values) only — the rest are Phase 9."* Verified as a
  negative control by SC-023.
- **FR-042** `Processor::processParameterChanges()` MUST dispatch by ID range — IDs `< 100` to
  `handleGlobalParamChange`, IDs `100–199` to `handleMacroParamChange` — using the last value of each
  parameter queue. *(Roadmap lines 394–396.)*
- **FR-043** Denormalization MUST match the parameter table above:
  master gain `clamp(value * 2.0, 0, 2)` (as `plugins/ruinae/src/parameters/global_params.h:47–49`);
  polyphony `clamp(int(value*15 + 1 + 0.5), 1, 16)` (as `:59–61`), which is exactly the range
  `SeraphisEngine::setPolyphony` clamps to (`seraphis_engine.h:203`, `:321`);
  soft limit `value >= 0.5`.
- **FR-044** The soft-limit parameter MUST map to `SeraphisEngine::setOutputSaturation`
  (`seraphis_engine.h:566`): on → `SeraphisEngine::kOutputSaturation` (`0.15f`, `:142`), off → `0.0f`.
  It MUST NOT be specified as bypassing the true-peak limiter: `processOutputStage` ends with
  `limiter_.processBlock(l, r, n)` with **no bypass path** (`seraphis_engine.h:521`), and the parameter's
  documentation string MUST say so.
  **Verification note.** `SeraphisEngine` exposes `setOutputSaturation` (`:566–569`) but **no matching
  getter** — the complete getter list at `:562–730` (`getAtmosphereFreeze, getPolyphony,
  getActiveVoiceCount, getRenderingVoiceCount, getVoiceLevel, getVoiceState, getVoice,
  getLastStolenVoiceIndex, getNonFiniteRecoveryCount, getVoiceAllocationSerial, getSeed,
  getLastBloomPartials, getLastBloomCount`) contains nothing for saturation. FR-044 is therefore verified
  **operationally, at a level where the saturator is engaged**, by SC-027 — which carries a mandatory
  non-vacuity assertion so it cannot pass by measuring nothing.
- **FR-045** `Processor::getState()` MUST write `kCurrentStateVersion` as `int32` first, then
  `saveGlobalParams` then `saveMacroParams`, via `Steinberg::IBStreamer(state, kLittleEndian)` —
  the shape at `plugins/ruinae/src/processor/processor_state.cpp:24–31`.
- **FR-046** `Processor::setState()` MUST read the version int32 first, return `kResultFalse` on a
  version greater than `kCurrentStateVersion`, and load the packs in the same order. Short/truncated
  streams MUST leave the affected parameters at their defaults rather than corrupting them (the
  EOF-safe read pattern at `plugins/ruinae/src/parameters/global_params.h:203–211`).
- **FR-047** `Controller::setComponentState()` MUST consume the same stream via the
  `load…ParamsToController` template helpers so every registered parameter is refreshed on preset load
  (`plugins/ruinae/src/parameters/global_params.h:220–243`, `macro_params.h:82–90`).
- **FR-048** `Controller::initialize()` MUST register exactly the eight parameters, using
  `createDropdownParameterWithDefault` from `plugins/shared/src/ui/parameter_helpers.h:47` for
  polyphony. Parameter *types* registered here are frozen for the life of the plugin — a later phase
  MUST NOT swap a `RangeParameter` for a `StringListParameter` at the same ID.

### E. Controller, preset and update adapters, resources

- **FR-050** `src/preset/seraphis_preset_config.h` MUST provide
  `inline Krate::Plugins::PresetManagerConfig makeSeraphisPresetConfig()` returning
  `{ kProcessorUID, "Seraphis", "Synth", { "Textures" } }`, matching the field order declared at
  `plugins/shared/src/preset/preset_manager_config.h:20–25` (field order is load-bearing, `:17–18`).
  *(Roadmap lines 388–390.)*
  **The config MUST be live, not merely compiled.** `Controller::initialize()` MUST instantiate a
  `Krate::Plugins::PresetManager` from it —
  `std::make_unique<PresetManager>(makeSeraphisPresetConfig(), nullptr, this)`, the shape at
  `plugins/ruinae/src/controller/controller.cpp:225–226`. Preset *browser UI* remains a Phase 11/12
  non-goal; this requirement is about the manager existing, owning the config, and spawning no thread.
  *(AMENDED 2026-07-31, A4: the withdrawn text continued "**so the `Textures` category is genuinely
  scanned and FR-050/FR-051 are verified by SC-003 rather than by inspection**". That premise is
  **false** — `PresetManager`'s constructor stores `config_`, `processor_`, `controller_` and the two
  path overrides and does **nothing else** (`plugins/shared/src/preset/preset_manager.cpp:16–29`); all
  enumeration lives in `scanPresets()` (`:37–55`), which **no Phase 8 plugin code calls**. Instantiation
  therefore scans nothing, and pluginval (SC-003) cannot observe the category at all.)*
  **The instantiation requirement itself stands unchanged**, and FR-050/FR-051 are re-mapped to a
  criterion that can actually fail: **SC-012 clause 2c**, which calls `scanPresets()` explicitly against
  in-repo path overrides and asserts the config's fields, the `presets/Textures/` directory's existence,
  and the default factory directory leaf. That is a *stronger* detector than the withdrawn one, which
  could not fail at all.
- **FR-051** `resources/presets/Textures/` MUST exist (may contain only a `.gitkeep`) so that the
  filesystem category and the XML metadata category agree from day one — the Membrum lesson recorded at
  roadmap line 388. `Textures` is seeded **now in both places** and is a **permanent** category name, not
  a throwaway: Phase 12 extends the category list additively and MUST NOT rename it, because renaming a
  shipped category orphans every preset saved against it. FR-009 records the constraint in
  `plugins/seraphis/CLAUDE.md`.
- **FR-052** `src/update/seraphis_update_config.h` MUST provide
  `inline Krate::Plugins::UpdateCheckerConfig makeSeraphisUpdateConfig()` returning
  `{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }`,
  mirroring `plugins/ruinae/src/update/ruinae_update_config.h:8–14`.
  **`Controller` MUST NOT instantiate `UpdateChecker` in Phase 8** — only the `make*Config()` header
  ships, compiled and unused (its correctness is a build-time/inspection matter, not an SC). Unlike the
  preset manager, `UpdateChecker` spawns a `std::thread` and performs a network fetch
  (`plugins/shared/src/update/update_checker.h:6–8, 44, 71`), which would land a detached thread and a
  socket inside the editor-lifecycle harness, SC-012 clause 3's ASan lane and the valgrind nightly — the
  very lanes this phase exists to make trustworthy. Wiring it up belongs to the phase that ships a UI
  affordance to trigger it (Phase 11/12).
- **FR-053** `src/engine/seraphis_engine_config.h` MUST be **thin** (roadmap line 346): free functions
  returning the DSP-owned config structs, introducing no new type —
  `inline Krate::DSP::SeraphisEngineConfig makeSeraphisEngineConfig(std::size_t polyphony, std::uint32_t seed, std::size_t maxBlockSamples)`
  and `inline Krate::DSP::AetherReverb::PrepareConfig makeSeraphisReverbConfig(std::size_t maxBlockSamples)`.
  Values MUST be the shipped Phase 6/7 defaults (`seraphis_engine.h:92–97`,
  `seraphis_voice.h:105–120`, `aether_reverb.h:1578–1588`) unless a deviation is justified in the header.
  In particular `spectralDiffusionEnabled` MUST stay `true` and `diffusionFftSize` MUST stay `1024` — the
  latency consequence is owned by FR-033, not dodged here.
  **Both callers MUST pass `maxBlockSamples = SeraphisEngine::kMaxBlockSamples` (2048)** and nothing else
  (FR-023 clause 1); the parameter exists so the value is explicit at the call site and testable, not so
  it can vary at runtime.
  The header MUST also declare the master-gain smoothing constant referenced by FR-024a clause 3:
  ```cpp
  /// Master-gain smoothing time. Same 20 ms family as SeraphisEngine::kSumGainSmoothMs
  /// (dsp/include/krate/dsp/systems/seraphis_engine.h:138); see FR-024a clause 3.
  inline constexpr float kMasterGainSmoothMs = 20.0f;
  ```
- **FR-054** `resources/editor.uidesc` MUST be a **placeholder** built only from stock VSTGUI views
  (no custom `ViewCreator`, since Phase 8 registers none — FR-018), containing a template named
  `"editor"` so `exerciseEditorLifecycle(controller, "editor", …)` can open it
  (`tests/test_helpers/editor_lifecycle_harness.h:102–105`). It MUST be non-empty and shipped into the
  bundle, because `tools/check-bundle.js:15–16` fails any bundle missing a non-empty
  `Contents/Resources/editor.uidesc`.
  **Contents are specified, not left to taste:** one container plus **eight** stock controls, each bound
  through a `<control-tags>` entry to one of the eight shipped parameter IDs —
  a knob or slider for `kMasterGainId` and for each of the five macros (`kMacroDreamId`, `kMacroBloomId`,
  `kMacroDissolveId`, `kMacroGravityId`, `kMacroEntropyId`), a `COptionMenu` for `kPolyphonyId`, and a
  checkbox/on-off button for `kSoftLimitId`. The cost is roughly one screen of XML that Phase 11 replaces
  wholesale.
  **This requirement's detector is SC-012 clause 2b, not the lifecycle harness** *(AMENDED 2026-07-31,
  A7 — the withdrawn text claimed the harness itself was made non-vacuous by the eight bindings)*: the
  harness asserts only `attached()`, `getFrame() != nullptr` and `getNbViews() > 0`
  (`tests/test_helpers/editor_lifecycle_harness.h:120–128`), **all three of which a one-label template
  satisfies**, and `VST3Editor` binds a mismatched control to a parameter with no error path. Clause 2b
  walks the built frame and asserts the control **count**, the exact **tag set**, and the concrete view
  classes for the two non-continuous parameters — which is what actually surfaces a wrong parameter type
  (FR-048) now rather than in Phase 11.
  The control types MUST match the registered parameter types: `COptionMenu` for the
  `StringListParameter` polyphony control, a toggle for the stepped soft-limit control, continuous
  controls for the six `RangeParameter`s. No custom view class name may appear anywhere in the file (see
  the cross-platform edge case).
- **FR-055** `Controller::createView("editor")` MUST return a `VSTGUI::VST3Editor` bound to that
  template, and `Controller` MUST be robust to `willClose()` after a view tree that was never attached to
  a platform window (the harness's exact scenario, `editor_lifecycle_harness.h:8–20`).
- **FR-056** `src/ui/` MUST exist and be empty in Phase 8 (roadmap line 349).

### F. Test target `seraphis_tests`

- **FR-060** `plugins/seraphis/tests/CMakeLists.txt` MUST define `add_executable(seraphis_tests …)`
  mirroring `plugins/ruinae/tests/CMakeLists.txt`, including: the test sources; **a second compilation of
  every plugin `.cpp`** via `${CMAKE_CURRENT_SOURCE_DIR}/../src/...` paths (`:139–152`); the SDK sources
  `public.sdk/source/common/memorystream.cpp`, `public.sdk/source/vst/hosting/hostclasses.cpp`,
  `public.sdk/source/vst/hosting/pluginterfacesupport.cpp`, `public.sdk/source/main/moduleinit.cpp`,
  `public.sdk/source/main/pluginfactory.cpp` (`:153–162`); and a local `vstgui_test_stubs.cpp`.
  *(Roadmap lines 402–408.)*
- **FR-061** `tests/vstgui_test_stubs.cpp` MUST define
  `Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }`, as
  `plugins/ruinae/tests/vstgui_test_stubs.cpp` does; `tests/unit/test_main.cpp` MUST define
  `void* moduleHandle = nullptr;` and call `enableFTZDAZ()` before `Catch::Session().run()`
  (`plugins/ruinae/tests/unit/test_main.cpp`).
- **FR-062** The target MUST link `KrateDSP KratePluginsShared Catch2::Catch2 test_helpers
  vstgui_support sdk` in that order (`sdk` after `vstgui_support`, per the stub file's own comment) and
  include `${CMAKE_SOURCE_DIR}/tests` so `test_helpers/…` headers resolve
  (`plugins/ruinae/tests/CMakeLists.txt:165–183`).
- **FR-063** The target MUST define `SERAPHIS_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"`
  so the editor-lifecycle test can pass an absolute `editor.uidesc` path
  (`plugins/ruinae/tests/CMakeLists.txt:193–197`; requirement stated at
  `tests/test_helpers/editor_lifecycle_harness.h:33–36`).
- **FR-064** Any test TU that injects NaN/Inf MUST carry
  `COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` under `CMAKE_CXX_COMPILER_ID MATCHES
  "Clang|GNU"` (`plugins/ruinae/tests/CMakeLists.txt:200–233`). Tests MUST NOT call `std::isnan` /
  `std::isinf` on a `-ffast-math` TU; use bit-pattern checks, and build non-finite inputs from bit
  patterns via a volatile sink.
- **FR-065** `catch_discover_tests(seraphis_tests REPORTER console)` MUST be called
  (`plugins/ruinae/tests/CMakeLists.txt:236`). *(Roadmap line 408.)*
- **FR-066** Day-one coverage MUST include, at minimum (roadmap lines 410–413 plus the four audio-behaviour
  FRs the roadmap list does not reach), with the **TEST_CASE names fixed here** so SC-002 can assert them:

  | File | TEST_CASE name | Covers |
  |---|---|---|
  | `unit/processor_bus_test.cpp` | `Seraphis_ProcessorBusSetup` | FR-020, FR-021 |
  | `unit/param_denorm_test.cpp` | `Seraphis_ParamDenormRoundTrip` | FR-042, FR-043, FR-048 |
  | `unit/state_roundtrip_test.cpp` | `Seraphis_StateRoundTrip` | FR-041, FR-045, FR-046, FR-047 |
  | `unit/midi_event_test.cpp` | `Seraphis_MidiEventTranslation` | FR-025, FR-031, and the event edge cases |
  | `unit/lifecycle_test.cpp` | `Seraphis_ProcessorLifecycle` | FR-029, FR-030, FR-032 |
  | `unit/controller/editor_lifecycle_test.cpp` | `Seraphis_EditorLifecycle` | FR-055 |
  | `integration/processor_audio_test.cpp` | `Seraphis_ProcessorRendersHeldNote` | FR-024, FR-024a, FR-026, FR-034a |
  | `integration/param_flow_test.cpp` | `Seraphis_ParamFlowReachesEngine` | FR-024a, FR-044 |

  `unit/controller/editor_lifecycle_test.cpp`'s `TEST_CASE` MUST declare a tag set **including
  `[lifecycle]`** — e.g. `"[seraphis][controller][ui][lifecycle]"`, matching
  `plugins/disrumpo/tests/unit/controller/editor_lifecycle_test.cpp:14`. This is not cosmetic:
  `.github/workflows/valgrind-nightly.yml:283–290` invokes each binary as `"$BINDIR/$bin" '[lifecycle]'`,
  a Catch2 tag filter, so without the tag the nightly lane selects **zero** Seraphis tests and SC-012's
  valgrind clause is satisfied vacuously (or the job fails on no-tests-matched). Asserted by SC-012.
- **FR-066a** Exactly **one** translation unit in `seraphis_tests` — `tests/unit/test_main.cpp` — MUST
  `#include <allocation_operator_overrides.h>`, and no other Seraphis TU may define global
  `operator new` / `operator delete`. Without it `TestHelpers::AllocationScope` counts **nothing**:
  `AllocationDetector::recordAllocation()` (`tests/test_helpers/allocation_detector.h:53–57`) is called
  only from the global operator replacements in `tests/test_helpers/allocation_operator_overrides.h`,
  whose banner (`:4–7`) states it must be included *"from EXACTLY ONE translation unit per test binary"*
  and is *"the only place in the repo allowed to define them"*. Two plugin test binaries already do this
  (`plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp:30`,
  `plugins/membrum/tests/unit/test_allocation_matrix.cpp:34`); a second include in the same binary is a
  duplicate-symbol link error, and a hand-rolled copy is caught by `tools/lint-allocation-operator-overrides.js`
  (registered at `tools/hooks/guard-ci-gates.js:45`). SC-007's liveness probe cannot pass without this.
- **FR-067** Any test that instantiates `Seraphis::Processor` MUST do so on the heap
  (`std::unique_ptr` / `createInstance`), never as a stack local, for the reason recorded at
  `seraphis_engine.h:119–122`. If FR-022's `unique_ptr` ownership makes `sizeof(Processor)` small, the
  test MAY use a stack local — but the test file MUST assert
  `static_assert(sizeof(Seraphis::Processor) < 64 * 1024)` so the constraint is checked, not assumed.
- **FR-068** No test in this phase may pin a render with a bit-exact float digest. Render comparisons
  MUST use `tests/test_helpers/render_fingerprint.h` (`fingerprintRender` at `:64`, `compareFingerprints`
  at `:101`). `node tools/lint-float-bit-goldens.js` and `node tools/lint-midi-timing-goldens.js` MUST
  stay clean.

### G. Registration outside `plugins/` — all of it in this phase

Roadmap line 417: *"A green local build proves nothing here; each of these is an independent CI failure
surface."*

- **FR-070** Root `CMakeLists.txt` MUST gain `add_subdirectory(plugins/seraphis)` alongside the six
  existing entries at `CMakeLists.txt:488–493`.
- **FR-071** `.github/workflows/ci.yml` MUST be updated at every site that names a plugin. **Measured
  this session:** `grep -ci "membrum" .github/workflows/ci.yml` → **51 matching lines**. The distinct
  site classes, each of which MUST gain a Seraphis entry:
  1. `detect-changes` job output (`:59`);
  2. `paths-filter` entry (`:85–86`);
  3. the `for p in iterum disrumpo ruinae innexus gradus membrum` loop (`:104`);
  4. the `$GITHUB_OUTPUT` echo (`:120`);
  5. the **three** FetchContent cache-key `hashFiles(...)` lists — Windows `:234`, macOS `:466`,
     Linux `:858` — each of which must list `plugins/seraphis/CMakeLists.txt`;
  6. the build matrices on all three legs (`:266`, `:506`, `:892`) and their `case` dispatch
     (`:274`, `:514`, `:900`);
  7. the test matrices on all three legs (`:315`, `:561`, `:938`) and their `case` dispatch
     (`:323`, `:569`, `:946`);
  8. the "Validate + resource-check built bundles" plugin lists on all three legs (`:364`, `:610`,
     `:987`) and their `case` dispatch (`:372`, `:618`, `:995`);
  9. artifact-upload steps on all three legs (`:430–435` Windows, `:811–819` macOS, `:1058–1063` Linux);
  10. the macOS `auval` step (modelled on `:691–697`) — `auval -v aumu Srph KrAt`;
  11. the macOS AUv3 bundle verification step (modelled on `:743–749`).
- **FR-072** `.github/workflows/release.yml` MUST be updated at **two** sites, exactly as roadmap
  line 424 says (*"plugin choice list **+ cache key**"*):
  1. `seraphis` in the `workflow_dispatch` plugin choice list (`:40`);
  2. `'plugins/seraphis/CMakeLists.txt'` in the FetchContent cache-key `hashFiles(...)` list (`:137`),
     which enumerates plugin CMakeLists individually —
     `hashFiles('CMakeLists.txt', 'dsp/CMakeLists.txt', 'plugins/iterum/CMakeLists.txt',
     'plugins/disrumpo/CMakeLists.txt', 'plugins/ruinae/CMakeLists.txt',
     'plugins/innexus/CMakeLists.txt', 'plugins/gradus/CMakeLists.txt', 'tests/CMakeLists.txt')`.
     Without it a Seraphis CMakeLists change does not invalidate the release cache.
     **Pre-existing drift, recorded not fixed:** `plugins/membrum/CMakeLists.txt` is already absent from
     this list. Adding Seraphis does not fix Membrum; FR-081's roster lint is what makes both visible.

  The version/installer/preset staging logic downstream is plugin-generic (`:71–72`, `:221–230`,
  `:297–314`, `:372–378`) provided FR-002/FR-008/FR-010 hold.
- **FR-073** `.github/workflows/valgrind-nightly.yml` MUST gain `seraphis_tests` in the build target
  list (`:276`) and the run list (`:283`). The Membrum-specific sharded job (`:123–192`) is **not**
  duplicated for Seraphis in this phase.
- **FR-074** `tools/run-clang-tidy.ps1` MUST gain `"seraphis"` in the `ValidateSet` (`:60`), a
  `"seraphis" { … }` case adding `plugins/seraphis/src` (modelled on `:190–193`), and the same paths in
  the `all` case (`:203–212`).
- **FR-075** `tools/run-clang-tidy.sh` MUST gain a `seraphis)` case (modelled on `:148–149`), the same
  paths in its `all` case (`:161–163`), and `seraphis` in the usage text (`:63`). Both scripts are
  required — the Linux/macOS pre-commit lint silently skips a plugin missing from the `.sh` one.
- **FR-076** `tools/check-changelog-coverage.js` MUST gain `'seraphis'` in the `PLUGINS` array
  (`:50`).
- **FR-077** `tools/gen-specs-index.js` MUST gain `['seraphis', 'Seraphis']` in `SUBSYSTEMS`
  (`:19–33`). Placement is load-bearing: the comment at `:18` states *"first keyword found in the spec's
  slug wins; order matters"*, and without a `seraphis` entry the existing phase specs classify wrongly
  (e.g. `seraphis-phase3-spectral-morph` currently matches `spectral` → "DSP / Spectral", and
  `seraphis-phase1-life-modulators` matches nothing → "Other"). The entry MUST therefore be placed
  **before** `spectral`, `grain`, `filter`, `oscillat` and `dsp`.
- **FR-078** The generated artifacts CI diff-gates MUST be regenerated and committed:
  `specs/_architecture_/repo-map.json` (`tools/gen-repo-map.js` auto-discovers `plugins/` at `:22–37`,
  so it changes as soon as the directory exists) and `specs/INDEX.md` (`tools/gen-specs-index.js`).
  `specs/_architecture_/symbols.json` scans only `dsp/include` (`tools/gen-symbols.js:19–21`) and is
  therefore unaffected. All three generators plus the lints are enforced by
  `tools/hooks/guard-ci-gates.js:32–47` — **nine** lints after FR-081 adds `lint-plugin-roster.js` to the
  eight at `:38–47`.
- **FR-079** Root `CLAUDE.md` MUST be updated: the plugin roster prose, the pluginval command table, the
  build/test target table, the clang-tidy target list, and the "Quick Reference" rows (add Seraphis
  parameter / test / UI rows). *(Roadmap line 430.)*
- **FR-080** `.github/workflows/docs.yml` needs **no** edit, but only one of its two loops picks Seraphis
  up, and the spec records both:
  - the **per-plugin manual loop** (`:41`, `for plugin_dir in plugins/*/`) enumerates the directory and
    will process Seraphis as soon as `plugins/seraphis/` has docs content;
  - the **root landing page** (`:133–158`) will **skip** Seraphis until a release tag exists. Its
    generator iterates `plugins/*/version.json` (`:137`) but then runs
    `git tag -l "{plugin_dir}/v*"` and `if not tag: continue` (`:141–148`) — a plugin with no release tag
    gets no card at all, so `has_page` (`:152`) is never evaluated. Seraphis first appears there when a
    `seraphis/v*` tag is cut, which is **Phase 12**.

  Both behaviours are expected and neither requires an edit in this phase. This requirement exists to
  record the verification, not to demand a change.
  **Phase 8 creates `plugins/seraphis/docs/` as an empty directory and authors no `index.html`.** Phase 12
  writes the page, alongside the release tag that first makes the plugin visible on the landing page. The
  per-plugin loop's probe of `plugins/<name>/docs/index.html` (`:152`) reports `has_page: false` for a
  missing page — harmlessly, and without failing — so this choice is invisible to CI in either direction
  and is therefore stated here rather than discovered later.
- **FR-081** A new `tools/lint-plugin-roster.js` MUST be added and registered in
  `tools/hooks/guard-ci-gates.js`'s `LINTS` array (currently eight entries, `:38–47`). It MUST enumerate
  the directories under `plugins/` (excluding `shared`) and **fail** if any of them is absent from any of
  the following rosters:
  1. root `CMakeLists.txt`'s `add_subdirectory(plugins/<name>)` list (`:488–493`);
  2. every plugin-naming site in `.github/workflows/ci.yml` — the `detect-changes` outputs, the
     `paths-filter` entries, the `for p in …` loop, each of the nine `for plugin_info in \` loops
     (`:260`, `:309`, `:362`, `:500`, `:555`, `:608`, `:886`, `:932`, `:985`) **and their `case` dispatch
     blocks**, and the three FetchContent `hashFiles(...)` lists;
  3. `.github/workflows/release.yml`'s choice list (`:40`) and `hashFiles(...)` list (`:137`);
  4. `.github/workflows/valgrind-nightly.yml`'s build target list (`:276`) and run list (`:283`);
  5. `tools/run-clang-tidy.ps1` (`ValidateSet`, its per-plugin case, and the `all` case) **and**
     `tools/run-clang-tidy.sh` (case, `all`, usage text);
  6. `tools/check-changelog-coverage.js`'s `PLUGINS` array (`:50`).

  **Why this is a requirement and not a nicety.** Every roster in that list is a static literal, and a
  missing entry produces a **green** run, not a red one: `ci.yml:309–315` builds its test list from
  `for plugin_info in "iterum:plugin_tests.exe approval_tests.exe" … "membrum:membrum_tests.exe"` with a
  matching `case` (`:317–324`), and the bundle-validation loop (`:362–373`) has the same shape plus
  `[ -d "$b" ] && BUNDLES+=("$b")` (`:375–376`), which silently drops even a listed-but-unbuilt bundle. A
  Seraphis entry missing from any of them therefore yields a passing CI in which Seraphis is never tested
  or validated — which no other criterion in this spec can detect. The lint is the only artefact that
  turns FR-070…FR-077 into something that can fail. Verified by SC-025; SC-017's threshold includes it.

---

## Success Criteria

Each criterion names its metric, threshold and the test/command that measures it.

- **SC-001 — Builds on all three OS legs.** `Seraphis.vst3` is produced by the CI build job on
  windows-2022, macOS and Linux. **Threshold:** 3/3 legs green **and zero compiler warnings**.
  **Measured by:** the CI `build` step on each leg for the green half; locally by
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Seraphis`.
  The warning half needs its own measurement, because `krate_plugin_set_warnings`
  (`cmake/KratePlugin.cmake:319–341`) sets MSVC `/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458` and
  GCC/Clang `-Wall -Wextra -Wpedantic -Wno-unused-parameter` but adds **no** `/WX` or `-Werror` — a
  warning does not fail the build step. **Measurement:** capture the build to a log on the first run
  (never re-run to grep it) and require **zero** lines matching `warning C` (MSVC) or `warning:`
  (GCC/Clang) whose file path is under `plugins/seraphis`, i.e.
  `grep -E "plugins[/\\]seraphis" <log> | grep -cE "warning C|warning:"` → `0`, on each leg.
  *(Roadmap line 432.)*

- **SC-002 — `seraphis_tests` green, with the required cases present.** **Threshold, two clauses:**
  1. `build/windows-x64-release/bin/Release/seraphis_tests.exe --list-tests` contains **all eight**
     FR-066 case names: `Seraphis_ProcessorBusSetup`, `Seraphis_ParamDenormRoundTrip`,
     `Seraphis_StateRoundTrip`, `Seraphis_MidiEventTranslation`, `Seraphis_ProcessorLifecycle`,
     `Seraphis_EditorLifecycle`, `Seraphis_ProcessorRendersHeldNote`, `Seraphis_ParamFlowReachesEngine`.
  2. the last line of `build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5` reads
     `All tests passed (N assertions in M test cases)`.

  A test-case **count** is explicitly not the threshold: `M >= 6` is satisfied by six cases in one file
  while five of the required files are missing, so it cannot detect the coverage gap it exists to
  prevent. Clause 2's default run excludes SC-014's `[.perf]`-tagged case by design (Catch2 hides
  hidden-tag cases); SC-014 names its own lane. *(Roadmap line 433.)*

- **SC-003 — pluginval strictness 5 clean.** **Threshold:** exit code 0, zero failures.
  **Measured by:**
  `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`.
  This run constructs the live `PresetManager` (FR-050) when pluginval opens the editor, so a throwing or
  crashing construction surfaces here. *(AMENDED 2026-07-31, A4: it does **not** scan the `Textures`
  category — the constructor stores its arguments and nothing more, `preset_manager.cpp:16–29`; scanning
  is **SC-012 clause 2c**'s job.)* It does **not** exercise any update check: FR-052 ships the config
  header without instantiating `UpdateChecker`, so no thread and no socket exist in this run.
  *(Roadmap line 434.)*

- **SC-004 — `auval` passes on macOS.** **Threshold:** `auval -v aumu Srph KrAt` reports
  `AU VALIDATION SUCCEEDED`, exit 0. **Measured by:** the CI macOS `auval` step added under FR-071.10,
  modelled on `ci.yml:691–697`. A `-10875` here means the FR-015/FR-016/FR-020 triple disagrees.
  *(Roadmap line 434.)*

- **SC-005 — A held note renders non-silent audio.** Test sketch
  `Seraphis_ProcessorRendersHeldNote` (`tests/integration/processor_audio_test.cpp`): prepare at
  48 kHz / 512-sample blocks, `setActive(true)`, send NoteOn(60, 100) at sample 0, render 4 s. The
  render uses the **registered defaults** — master gain unity, **polyphony 8** (the registered default
  index 7), soft limit on — and MUST NOT automate `kPolyphonyId`: this criterion names no polyphony, and
  16 voices would cost 3 dB through FR-052's `1/sqrt(polyphony)` sum gain, which the engine applies
  whether or not the other fifteen slots sound.
  **Threshold (AMENDED — see the Amendments section):** peak of the rendered stereo output
  `>= 5.0e-4` **and** every sample finite (bit-pattern check, not `std::isnan`).

  > **The original `1.0e-3` (−60 dBFS) form of this threshold is WITHDRAWN: it is not reachable by a
  > correct implementation.** It was derived from Phase 7's per-voice figure — *"a voice at the FR-019
  > neutral peaks near 3.8e-3"* (`specs/seraphis-phase7-voice-engine/compliance.md:216`) — divided by
  > FR-052's `sqrt(8)`, giving `1.34e-3`. Measured on **note 60** at the registered defaults, this
  > session: engine-only voice sum at polyphony 1 peaks `0.00300287`; through the full composed chain at
  > polyphony 8 the render peaks **L `0.000858181`, R `0.000959373` (−60.36 dBFS)**, i.e. 0.36 dB under
  > the old bound — and `0.000678379` if polyphony is forced to 16. The per-voice `3.8e-3` is a maximum
  > across notes, not note 60's value; measured pitch spread through the chain at polyphony 8: note 36
  > `0.00312372`, note 48 `0.00136813`, note 60 `0.000959373`, note 72 `0.00120537`.
  >
  > The replacement `5.0e-4` is **half the measured peak** (6 dB of margin for cross-toolchain FP
  > spread) and retains the criterion's teeth: the failure modes SC-005 exists to catch — FR-030's
  > zero-fill path taken by mistake, a note that never reaches the engine, a chain step dropped — all
  > produce an exactly-zero or denormal-floor render, tens of dB below this floor. The measured figures
  > MUST be printed by the test (`WARN`) and recorded in `compliance.md`.

  **Measured by:** that test, in `seraphis_tests`. *(Roadmap lines 434–435.)*

- **SC-006 — Output never exceeds the limiter ceiling, *and the output stage demonstrably ran*.** Test
  sketch `Seraphis_ProcessorRespectsCeiling` (a SECTION of `Seraphis_ProcessorRendersHeldNote`): 16
  voices with all notes held, 4 s.
  **The master gain differs PER CLAUSE, and that is load-bearing** *(AMENDED 2026-07-31, A8 — the
  withdrawn text set "master gain 2.0 (normalized 1.0)" once, for the whole criterion)*:

  | Clause | Render(s) | `kMasterGainId` normalized | Linear gain |
  |---|---|---|---|
  | 1 | processor only | **1.0** | 2.0 |
  | 2 | processor **vs** `renderSeraphisChain` | **0.5** | 1.0 |
  | 3 | processor, chain-with-step-5, hand-rolled-without-step-5 | **0.5** | 1.0 |

  **Threshold, three clauses — all three required:**
  1. **Bound, on the gain-2.0 render.** Absolute peak `<= 0.8912509 * 10^(0.1/20)` — the
     `TruePeakLimiter` ceiling (`kDefaultCeilingDb = -1.0f`, `ceilingLin_ = 0.8912509f`,
     `true_peak_limiter.h:46, 168`) plus the **0.1 dB** allowance Phase 7's SC-015 uses
     (`specs/seraphis-phase7-voice-engine/spec.md`, SC-015). Gain 2.0 is the worst case the Phase 8
     parameter surface can produce, so this is where the bound belongs.
  2. **Positive control, at normalized 0.5.** The processor's render matches a `renderSeraphisChain`
     render of the same script/seed/blocksize **including** `processOutputStage`, via
     `compareFingerprints(...).withinTolerance()`. **It MUST run at normalized 0.5, not 1.0:**
     `renderSeraphisChain` applies **no** master gain (it has no `GlobalParams` and no gain step), so at
     gain 2.0 the processor drives the output-stage nonlinearity and the limiter twice as hard as the
     reference and the comparison would fail a **correct** implementation. At normalized 0.5 the
     denormalization is exactly `1.0f` and FR-024a clause 3's snap makes step 4b an IEEE-754 identity
     multiply, so the two renders are comparable on their own terms.
  3. **Negative control, at normalized 0.5, with mandatory non-vacuity.** The test additionally builds a
     control render that **omits step 5** (an explicit engine+reverb loop in the test, since
     `renderSeraphisChain` has no skip flag) and asserts, in this order:
     - `REQUIRE(maxAbsDiff(withStep5, withoutStep5) > kSampleTolerance)` — the **non-vacuity** assertion;
     - `REQUIRE(maxAbsDiff(processorRender, withoutStep5) > kSampleTolerance)`.

     The same 0.5 reasoning applies: at gain 2.0 the second assertion would be dominated by the gain
     difference rather than by the presence of step 5, and would pass for the wrong reason.

  **No threshold moved.** The ceiling bound in clause 1 is unchanged and still measured at the maximum
  reachable gain; clauses 2–3 are *comparisons between renders*, and stating the gain at which the two
  arms are comparable is what makes them able to fail for the right reason.

  Clause 1 alone has **no discriminating power** and that is why clauses 2–3 exist: Phase 7 measured the
  composed chain's worst case at 16 voices / 60 s adversarial as peak `0.128337 = -17.83 dBFS`
  (`specs/seraphis-phase7-voice-engine/compliance.md:181`) — 16.8 dB of headroom — and recorded that
  *"the limiter never engages — the bound is satisfied with 16.8 dB to spare rather than demonstrated"*
  (`compliance.md:269`). Even at master gain 2.0 (peak ≈ 0.26) clause 1 passes with a processor that
  omitted FR-024 step 5 entirely. If the non-vacuity assertion in clause 3 fails, the criterion is **not**
  dropped: the scenario is escalated (more voices / higher pre-output-stage level) until the output stage
  is measurable, and the measured level is recorded in `compliance.md`.

- **SC-007 — Zero allocations in `process()`.** Test sketch `Seraphis_ProcessorNoAllocInProcess`
  using `tests/test_helpers/allocation_detector.h:75` `AllocationScope`: after `setupProcessing` +
  `setActive(true)`, render 200 blocks of 512 samples with NoteOn/NoteOff traffic and a parameter sweep.
  **Threshold:** allocation count **exactly 0**, with a liveness probe (a deliberate allocation inside
  the scope) asserted to be counted so the detector is proven live. The probe can only pass because of
  **FR-066a** — `AllocationScope` counts nothing unless exactly one TU in the binary includes
  `<allocation_operator_overrides.h>`. *(Cross-cutting constraint, roadmap line 497.)*

  **The reading form is NORMATIVE (ADDED 2026-07-31 by A9), because the obvious one measures nothing.**
  The count MUST be read from the **live singleton** while the scope is open —
  `TestHelpers::AllocationDetector::instance().getAllocationCount()`
  (`tests/test_helpers/allocation_detector.h:48–50`, an atomic load of the running counter) — captured
  into a local, and asserted **after** the scope closes:
  ```cpp
  std::size_t allocations = 0;
  {
      TestHelpers::AllocationScope scope;
      /* ... exercise ... */
      allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
  }
  REQUIRE(allocations == 0u);
  ```
  `REQUIRE(scope.getAllocationCount() == 0)` written **inside** the scope is forbidden: that accessor
  returns the member `count_` (`:85–87`), which is assigned **only** by `~AllocationScope()`
  (`:81–83`), so inside the scope it is still its `0` initializer and the assertion passes
  unconditionally — including for a `process()` that allocated on every block. The in-repo model
  `plugins/membrum/tests/unit/test_allocation_matrix.cpp:129–135` uses exactly that inside-the-scope
  form; it MUST NOT be copied. This pins *how* the existing threshold is read; the threshold itself
  (exactly 0) is unchanged.

- **SC-008 — Block-size invariance.** Test sketch `Seraphis_ProcessorBlockSizeInvariance` (a SECTION of
  `Seraphis_MidiEventTranslation`): render the same 4 s seeded script through `Processor::process()` at
  host block sizes **{1, 7, 64, 65, 512, 2048, 4096}** with events placed at non-multiples of every
  partition, and compare each of {1, 7, 64, 65, 2048, 4096} against the **512-sample reference**.
  **Threshold, in Phase 7's form** (`specs/seraphis-phase7-voice-engine/spec.md`, SC-014):
  1. **Primary gate:** **maximum absolute per-sample difference over all samples ≤ 1e-5**, per channel,
     against the 512-sample reference — `REQUIRE(dl.worst <= 1.0e-5f)` / `REQUIRE(dr.worst <= 1.0e-5f)`,
     the shape shipped at `specs/seraphis-phase7-voice-engine/compliance.md:180`.
  2. **Secondary, WARN-only:** `compareFingerprints(...)` as an aggregate sanity check. It MUST NOT gate.
  3. **Required coverage, asserted rather than assumed:** the parameter set must guarantee that at least
     one partition boundary falls **inside** a 64-sample control chunk (`kControlChunkSamples = 64`,
     `seraphis_engine.h:132`) — block 65 provides this — and the test must assert that it did.

  **Why not the fingerprint as the gate.** Phase 7 evaluated exactly this instrument for exactly this
  comparison and rejected it in both directions: it samples 32 checkpoints (`kRenderCheckpoints = 32`,
  `render_fingerprint.h:46`) out of the whole render, so *"it can miss a localised divergence entirely"*,
  and its `kMetricTolerance = 1e-5` relative bound on `totalVariation` (`render_fingerprint.h:52`) is
  *tighter* than what the sub-components guarantee, because *"those tolerances were measured for
  cross-toolchain spread of the same computation … not for a re-partitioned one"*
  (`specs/seraphis-phase7-voice-engine/spec.md`, SC-014's rationale block). As a sole gate it can both
  fail a correct implementation and pass a broken one.

  The **4096** partition is required, not optional: it is the only entry that exceeds
  `SeraphisEngine::kMaxBlockSamples` (2048, `seraphis_engine.h:134`) and therefore the only one that
  enters FR-026's sub-division branch — the hazard the edge-case list names as *"out of spec, but
  observed"*. Phase 7's driver banner names the same partition set `{1, 7, 64, 65, 512, 4096}`
  (`tests/test_helpers/seraphis_chain.h:34–36`) and Phase 7 shipped it
  (`specs/seraphis-phase7-voice-engine/compliance.md:156`). The 512-sample reference is **not** compared
  against itself — that comparison is trivially true and carries no information.

  This is the direct test of FR-025 (event sub-division) and FR-026 (slice bounding).

- **SC-009 — Parameter denormalization round-trip.** Test sketch `Seraphis_ParamDenormRoundTrip`:
  for each of the eight IDs, sweep normalized values `{0, 0.25, 0.5, 0.75, 1}` through
  `processParameterChanges` and read back the atomic. **Threshold:** master gain within `1e-6` of
  `value*2`; polyphony exactly `clamp(int(v*15+1.5),1,16)`; soft limit exactly `v >= 0.5`; each macro
  within `1e-6` of `v`.
  **Last-point clause (FR-042) — mandatory (ADDED 2026-07-31 by A6).** FR-042 requires *the last value of
  each parameter queue*, and with the single-point queues the sweep above uses, an implementation reading
  `getPoint(0, …)` is **byte-for-byte indistinguishable** from one reading `getPointCount() - 1`. No
  other criterion covers it — SC-008's block-size render deliberately contains no automation. The clause:
  push a **3-point** queue for `kPolyphonyId` of `{0.0, 1.0, 0.4}` (→ 1, 16, **7** voices) and a 3-point
  queue for `kMacroDreamId` of `{0.0, 1.0, 0.25}`, run **one** `processParameterChanges` call, and
  require each atomic to hold the denormalization of the **last** point (`7` and `0.25f`). The last value
  is deliberately neither the first nor the maximum of its queue, so neither a `getPoint(0)` reader nor a
  max-scan can pass.
  **Controller clause — stated to the behaviour the SDK actually provides.**
  `Controller::getParamNormalized(id)` after `setParamNormalized(id, v)` returns `v` **exactly, for every
  registered parameter including the polyphony list**. The SDK does **not** quantize:
  `Parameter::setNormalized` (`extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp:62–80`) only clamps
  to `[0,1]` and stores the raw value, and `StringListParameter`
  (`extern/vst3sdk/public.sdk/source/vst/vstparameters.h:133–158`) overrides `toString` / `fromString` /
  `toPlain` / `toNormalized` but **not** `setNormalized` — so `setParamNormalized(kPolyphonyId, 0.3)` is
  followed by `getParamNormalized(kPolyphonyId) == 0.3`, not the nearest step `0.2`. A criterion demanding
  "nearest step" would fail a correct implementation.
  The step mapping is verified **separately**, on the surfaces that do quantize: for
  `kPolyphonyId`, `StringListParameter::toPlain(v) == std::round(v * 15)` and the displayed string
  (`toString`) equals the expected voice count.

- **SC-010 — State round-trip is byte-stable.** Test sketch `Seraphis_StateRoundTrip`: set all eight
  parameters to distinct non-default values, `getState` into a `MemoryStream`, construct a fresh
  `Processor`, `setState`, `getState` again. **Threshold:** the two byte streams are **byte-identical**
  (a digest over a serialized byte stream is the sanctioned form — `dsp/CLAUDE.md`), the version prefix
  equals `1`, and every parameter reads back equal to what was set. A truncated stream (first N bytes,
  for several N) must not crash and must leave later parameters at defaults.
  **Default-state clause (FR-041).** A `getState()` taken on a freshly constructed `Processor` **before
  any parameter change** must stream values equal to the registered parameter defaults — in particular
  `gravity == 0.5f`, not `0.0f`. This is the assertion that catches a `MacroParams` whose members are
  value-initialized instead of carrying FR-041's explicit initializers.
  **Controller clause (FR-047) — mandatory (ADDED 2026-07-31 by A5).** Without it FR-047 and both
  `load*ParamsToController` helpers have **no detector at all**: as previously written this criterion
  never called `Controller::setComponentState()`, so an implementation that returned `kResultOk` without
  reading the stream — or that loaded the two packs in the wrong order — satisfied every criterion in
  this spec. The clause:
  - set all eight parameters on the processor to distinct non-default values, **including
    `kMacroGravityId` set away from its `0.5` default** (e.g. normalized `0.8`), so a no-op loader that
    leaves the registered defaults in place is caught rather than accidentally matching;
  - `Processor::getState(&stream)`, then `stream.seek(0, IBStream::kIBSeekSet, nullptr)`;
  - on a **fresh** `Seraphis::Controller` after `initialize(nullptr)`,
    `REQUIRE(controller.setComponentState(&stream) == kResultOk)`;
  - for each of the eight IDs, `controller.getParamNormalized(id)` equals the **normalized** value pushed
    in step 1, under the inverse mappings `masterGain / 2.0`, `(polyphony - 1) / 15.0`,
    `softLimit ? 1.0 : 0.0`, macros as-is.

  The test file may include **both** `processor/processor.h` and `controller/controller.h` — they do not
  include each other, so the VST3 separation rule (no cross-include between the two components) is not
  weakened by a test TU that links both.

- **SC-011 — Bus configuration is exactly instrument-shaped.** Test sketch
  `Seraphis_ProcessorBusSetup`: after `initialize(nullptr)`, assert
  `getBusCount(kAudio, kInput) == 0`, `getBusCount(kAudio, kOutput) == 1`,
  `getBusCount(kEvent, kInput) == 1`; `setBusArrangements` returns `kResultFalse` for `numIns != 0`, for
  **`numOuts != 1` (both `0` and `2` asserted)**, and for a mono output arrangement, and `kResultTrue` for
  a single `kStereo` output. The `numOuts == 2` case is FR-021 clause (b): without it a host can
  successfully negotiate an output bus that does not exist.

- **SC-012 — Editor lifecycle is UAF-free.** Test sketch `Seraphis_EditorLifecycle` calling
  `Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", std::string(SERAPHIS_RESOURCES_DIR)
  + "/editor.uidesc")`. **Threshold, all runnable at phase completion:**
  1. **Tag selection is non-vacuous:** `seraphis_tests.exe "[lifecycle]"` selects **≥ 1** test case
     (FR-066's tag requirement). Without this the nightly lane's `"$BINDIR/$bin" '[lifecycle]'` filter
     (`.github/workflows/valgrind-nightly.yml:283–290`) matches nothing.
  2. **Release lane, three sub-clauses (2a/2b/2c — SPLIT 2026-07-31 by A4 and A7).**
     - **2a — harness:** 3 open/close cycles complete with no crash. The harness itself `CHECK`s
       `attached() == kResultTrue` and `REQUIRE`s `getFrame()->getNbViews() > 0`
       (`tests/test_helpers/editor_lifecycle_harness.h:120–128`).
     - **2b — bound controls (FR-054), mandatory (ADDED by A7).** *The withdrawn text asserted that
       clause 2 gained teeth from "FR-054's eight bound controls" and that a wrong parameter type
       "fails here". Neither follows from the harness:* its three assertions are **all satisfied by a
       template holding a single `CTextLabel`**, and `VST3Editor` binds a mismatched control to a
       parameter with **no error path**, so a type mismatch would not fail anywhere. This sub-clause is
       what makes both statements true. In a dedicated SECTION, build a `VSTGUI::VST3Editor` on the
       `"editor"` template directly, `attached(nullptr, Krate::TestSupport::nativePlatformType())`
       (**never** a platform-type literal — `editor_lifecycle_harness.h:87`;
       `tools/lint-platform-type-literals.js` enforces it), walk the frame recursively collecting
       `CControl*`, and require:
       - exactly **8** controls;
       - their tag set is exactly `{0, 1, 2, 100, 101, 102, 103, 104}` — the eight shipped IDs;
       - the control at tag `1` (`kPolyphonyId`, a `StringListParameter`) is a `COptionMenu`, and the
         control at tag `2` (`kSoftLimitId`, the stepped toggle) is a `CCheckBox` —
         `dynamic_cast<>` non-null in both cases.

       This is where a control whose type disagrees with the registered parameter type (FR-048, whose
       types are frozen for the life of the plugin) fails, and it fails **now** rather than in Phase 11.
     - **2c — the preset config is live and its two halves agree (FR-050, FR-051), mandatory (ADDED by
       A4).** Instantiating a `PresetManager` **scans nothing** (`preset_manager.cpp:16–29`), so this
       sub-clause performs the enumeration the withdrawn FR-050 text wrongly attributed to SC-003.
       In a SECTION: `makeSeraphisPresetConfig()`'s `pluginName == "Seraphis"`, `subcategoryNames ==
       {"Textures"}`, `processorUID == kProcessorUID`; `presets/Textures/` **exists on disk** under
       `SERAPHIS_RESOURCES_DIR` (FR-051's filesystem half, which must agree with the metadata half
       above); a `PresetManager` constructed with in-repo user/factory path overrides returns an
       **empty** `scanPresets()` (Phase 8 ships no `.vstpreset`) and reports the config back; and a
       default-constructed one has a factory-preset directory whose **leaf compares equal to `seraphis`
       case-insensitively** (the Linux path builder lowercases the leaf).
  3. **Sanitizer gate, run at completion** (Release passes by luck; the harness only has teeth under a
     sanitizer): configure `cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`,
     build Debug, run `seraphis_tests` and require a **clean exit with no ASan report** (root
     `CLAUDE.md`'s ASan recipe).

  The valgrind nightly lane (`.github/workflows/valgrind-nightly.yml` after FR-073) is the **ongoing
  regression surface**, not the phase gate: it runs nightly rather than on the change, so it cannot be
  evaluated when the phase completes. *(Roadmap lines 411–413.)*

- **SC-013 — Reported latency is exactly 1024 and is invariant.** Test sketch
  `Seraphis_LatencyIsReported` (a SECTION of `Seraphis_ProcessorLifecycle`, T017).
  **Threshold — one invariance matrix, every point asserting the literal `1024u`.** The literal number
  is required, not "equals `AetherReverb::getLatencySamples()`", which a config that disabled spectral
  diffusion would also satisfy at 0. On one processor, in this order:
  1. after `initialize(nullptr)` and **before any `setupProcessing()`**;
  2. after each of **four consecutive `setupProcessing()` calls at 44100, 48000, 96000, 192000 Hz, in
     that order** — a sample count does not scale with the rate, and the ordering catches a stateful
     implementation that latched the first rate;
  3. after `setActive(true)` and after `setActive(false)`;
  4. after a `setState()` carrying non-default values;
  5. after pushing **each** of the eight shipped parameters to `0.0` and to `1.0`;
  6. after 100 `process()` calls.

  Plus: `initialize(nullptr)` immediately followed by `setupProcessing()` must not crash
  (`REQUIRE_NOTHROW`), and — the one place the value is legally not 1024 — a `Processor` whose
  `initialize()` never ran reports `0` (there is no `reverb_`) and must not crash (asserted by SC-021's
  out-of-order clause).

  > **AMENDED 2026-07-31 (A2): clause 4 of the previous form — "`setupProcessing()` calls
  > `restartComponent(kLatencyChanged)` when and only when the reported value changed" — is WITHDRAWN as
  > unimplementable and unobservable, and replaced by the matrix above.** Two header facts kill it: the
  > reported value **never changes** (`aether_reverb.h:2607–2613` returns
  > `spectralEnabled_ ? diffusionFftSize_ : 0` with both pinned by FR-053, so an unprepared reverb
  > already reports 1024 — there is no `0 → 1024` transition to count), and a
  > `Steinberg::Vst::AudioEffect` has **no `IComponentHandler`** (it is delivered only to the edit
  > controller — `vsteditcontroller.h:59, :97, :108`), so the only way to satisfy the old clause was a
  > bespoke test stub feeding a mechanism no host can reach. **This is a strengthening, not a
  > relaxation:** the old clause could observe one flag on a fabricated host; the matrix observes the
  > actual reported value at 14+ distinct lifecycle points on the real object. FR-023 clause 4 records
  > the route a future *variable* latency must use.

- **SC-014 — Wrapper overhead (NON-GATING measurement).** **This criterion does not fail the phase.** It
  is a `[.perf]`-tagged measurement whose result is **recorded in `compliance.md`** — the same treatment
  Phase 5 gave its out-of-region 64-grain configuration (roadmap lines 250–254). Two independent reasons:
  - Roadmap Phase 8 defines **no** CPU criterion. Its success criteria (lines 432–435) are
    build / tests / pluginval / auval / non-silent-render / portability / clang-tidy, and the
    cross-cutting rule at line 500 assigns measured CPU budgets to *"phases 2/4/5"* per-voice and
    *"6/7"* global — not Phase 8. A gating ratio here would be an invented threshold.
  - The margin is inside the measured noise of this lane: Phase 7 recorded that *"the `[.perf]` lane
    spread between an idle and a loaded machine is ~33 %, larger than `kRegressionFactor` (1.15)"*
    (`specs/seraphis-phase7-voice-engine/compliance.md:268`).

  **Protocol (pinned, so the recorded number means something).** Test sketch
  `Seraphis_ProcessorCpuOverhead`, tag `[.perf]`:
  - identical 4 s scenario on both arms — polyphony 8, one held note, 512-sample blocks, 48 kHz, same
    seed;
  - **best-of-16 × 100 blocks** per arm, the trial shape `dsp/tests/unit/systems/seraphis_perf_test.cpp:163`
    uses, with a discarded warm-up trial;
  - the two arms **interleaved in the same process**, not run back to back;
  - **buffer allocation excluded from the chain arm.** `renderSeraphisChain` allocates and zero-fills on
    every call — `outL.assign` / `outR.assign` over `totalSamples`, the `eventAt` vector, and four
    `blockSize` vectors (`tests/test_helpers/seraphis_chain.h:154–186`) — ~1.5 MB of allocation + fill per
    4 s render charged to the denominator only, which biases the ratio in the wrapper's favour. The test
    MUST hoist those vectors out of the timed region (or time an equivalent hand-rolled loop) so both arms
    do the same work;
  - **lane:** the `[.perf]` tag is hidden from the default run, so it is executed explicitly by
    `seraphis_tests.exe "[.perf]"` on the machine that records the figure. It is **not** part of SC-002's
    default run and **no CI lane gates it**.

  **Recorded figure:** `processor_ns / chain_ns`, reported with the machine-idle caveat Phase 7's SC-001
  uses (`seraphis_perf_test.cpp:140–146`). A ratio above ~1.15 is a **flag to investigate**, not a
  failure. Context for the reader: Phase 7 SC-001 measured full-poly at ~20 % of one core against a 25 %
  ceiling (`specs/seraphis-phase7-voice-engine/compliance.md`, SC-001 row), so the wrapper has real but
  finite headroom to spend.

- **SC-015 — Portability gate clean.** **Threshold:** `node tools/check-portability.js` exits 0 over
  every C++ file this phase adds. *(Roadmap line 435.)*

- **SC-016 — clang-tidy picks the plugin up on both scripts.** **Threshold:**
  `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` analyses at least one
  file under `plugins/seraphis/src` and reports zero warnings; `-Target all` also covers it; the
  equivalent `./tools/run-clang-tidy.sh --target seraphis` and `--target all` do the same on
  Linux/macOS. *(Roadmap lines 426–427, 435.)*

- **SC-017 — Generated artifacts and lints are in sync.** **Threshold:**
  `node tools/gen-repo-map.js --check`, `node tools/gen-specs-index.js --check`,
  `node tools/gen-symbols.js --check` all exit 0, and **all nine** lints listed at
  `tools/hooks/guard-ci-gates.js:38–47` exit 0 — the existing eight plus FR-081's
  `lint-plugin-roster.js` — with the Seraphis files present.

- **SC-018 — Bundle resource guard passes.** **Threshold:** `node tools/check-bundle.js
  build/windows-x64-release/VST3/Release/Seraphis.vst3` exits **0** with **no `FAIL` line**
  (`tools/check-bundle.js:53–56`). If the output string is asserted literally it must be quoted exactly
  as emitted — `tools/check-bundle.js:47` prints `` `OK   ${name}: editor.uidesc + moduleinfo.json present` ``
  with `name = path.basename(bundle)` (`:26`), i.e. **`OK   Seraphis.vst3: editor.uidesc + moduleinfo.json present`**
  (three spaces, `.vst3` suffix). A check written against `OK Seraphis: …` fails on a correct build.

- **SC-019 — The three global parameters reach the chain.** Test sketch
  `Seraphis_ParamFlowReachesEngine` (`tests/integration/param_flow_test.cpp`). Without this criterion an
  implementation whose `masterGain` atomic is written and never multiplied in, and whose `polyphony`
  atomic never reaches `SeraphisEngine::setPolyphony`, satisfies every other FR and SC in this spec.
  **Threshold, three clauses (FR-024a):**
  1. **Master gain silences.** At `kMasterGainId` normalized `0.0`, the 4 s render's absolute peak is
     `< 1e-6` — **over the whole render, from sample 0**, with no "after the first N ms" allowance. This
     is literally satisfiable because FR-024a clause 3 **snaps** the master-gain smoother to the current
     parameter atomic on the first `process()` after prepare, rather than ramping down from a previous
     value. A ramped-from-default implementation fails this clause by design, which is the point.
  2. **Master gain scales.** Peak at normalized `1.0` (gain 2.0) divided by peak at normalized `0.5`
     (gain 1.0) is `2.0 ± 5 %`, measured on a render whose peak stays below the limiter ceiling so the
     ratio is not compressed. If the limiter engages, the render level is reduced until it does not, and
     the level used is recorded.
  3. **Polyphony reaches the engine, from the first sample.** Three sub-clauses, all required:
     - **Seeded at prepare.** After `setState()` carrying a non-default polyphony (say 4) **followed by**
       `setupProcessing()` — the legal ordering FR-023 clause 2 exists for — `getPolyphony()`
       (`seraphis_engine.h:665`) equals `4` **before any `process()` call**, not the struct default 8.
     - **Tracked at prepare.** The very first `process()` after that prepare must **not** re-call
       `setPolyphony` (FR-023 clause 3 resets the tracker to the prepared value).
     - **Pushed on change.** After pushing `kPolyphonyId` at normalized `v` and running one `process()`
       call, `getPolyphony()` equals `clamp(int(v*15 + 1.5), 1, 16)`, checked for
       `v ∈ {0, 0.25, 0.5, 0.75, 1}`; and re-pushing the **same** value must not re-call `setPolyphony`
       (FR-024a clause 1, "on change only").

     The two "must not re-call" assertions are made by observing that the voice-sum gain smoother is not
     re-armed, or by a call-count seam if the smoother is not observable.

- **SC-020 — The tree is clean after a full configure + build.** **Threshold:**
  `git status --porcelain plugins/seraphis` produces **no output** after `cmake --preset
  windows-x64-release` + a full build. This is what makes FR-007a's `.gitignore` trio real: without it
  the three generated files (`src/version.h`, `resources/win32resource.rc`,
  `resources/auv3/audiounitconfig.h`) show as untracked, and a later version bump dirties them.

- **SC-021 — Degenerate `process()` shapes are safe (FR-030).** Test sketch, a SECTION of
  `Seraphis_ProcessorLifecycle`. *(AMENDED 2026-07-31, A10: clauses 2, 3 and 5 were added and clause 4
  was given its pre-seeded canary. All are strengthenings — each replaces an assertion that a broken
  implementation could satisfy.)* **Threshold, five clauses:**
  1. **Degenerate shapes.** Each of `data.numInputs == 0`, `data.numOutputs == 0`,
     `data.numSamples == 0`, and `data.outputs[0].channelBuffers32 == nullptr` returns `kResultOk`,
     writes no sample outside the provided buffers (asserted with **canary guard words** around each
     buffer), and leaves `SeraphisEngine::getActiveVoiceCount()` (`seraphis_engine.h:668`) unchanged.
  2. **Mono output.** A `ProcessData` carrying a **one-element** `channelBuffers32` array with
     `numChannels == 1` returns `kResultOk`, never touches `channelBuffers32[1]`, and leaves
     `getActiveVoiceCount()` unchanged. Under SC-012 clause 3's ASan lane an out-of-bounds read here is a
     hard failure rather than a silent garbage read, which is what gives this clause teeth on both lanes.
  3. **Pre-`setupProcessing()` silence, with teeth.** `process()` called before `setupProcessing()` with
     the output buffers **pre-seeded to a non-zero value (`0.5f`)** returns `kResultOk` **and leaves every
     output sample exactly `0.0f`**. The pre-seed is mandatory: without it the assertion is satisfied by
     the test fixture having zeroed its own vectors, and an implementation that returns without writing —
     handing the host back buffer content VST3 never promised to zero — goes undetected.
  4. **Silence flags, both directions (FR-024, FR-030 rule 3).** A normal render (after prepare, with a
     held note) leaves `data.outputs[0].silenceFlags == 0`, asserted from a **pre-seeded `3`**; the
     not-ready path leaves `silenceFlags == 3`, asserted from a **pre-seeded `0`**. Neither direction can
     pass by the host happening to leave the field alone. On the shapes in clause 1, `silenceFlags` must
     be whatever FR-030's early-out leaves — no read of `data.outputs[0]` may occur when
     `numOutputs == 0`, and no write when `channelBuffers32 == nullptr` is the reason for the early-out.
  5. **Out-of-order lifecycle.** `setupProcessing()` on a processor whose `initialize()` never ran
     returns without crashing and leaves `getLatencySamples() == 0` (there is no `reverb_` to report
     1024 — the single documented exception to SC-013's invariance matrix); a subsequent `process()` with
     valid, pre-seeded buffers returns `kResultOk` and produces silence per clause 3. This is the shape
     pluginval strictness 5 probes.

- **SC-022 — MIDI translation is correct (FR-031, FR-025).** Test sketch
  `Seraphis_MidiEventTranslation`. **Threshold, on the engine's own observable surface —
  `getActiveVoiceCount()` (`seraphis_engine.h:668`) and `getVoiceState(i)` (`:693`):**
  1. `kNoteOnEvent` with `velocity > 0` allocates exactly one voice;
  2. `kNoteOnEvent` with `velocity == 0` releases the matching note, identically to `kNoteOffEvent`
     (asserted by comparing the two paths' resulting `VoiceState`);
  3. `kNoteOffEvent` for a note never played is a no-op (no state change, no crash);
  4. `sampleOffset < 0` and `sampleOffset >= data.numSamples` are clamped into `[0, numSamples]` and
     never produce a negative slice length;
  5. two events at the **same** `sampleOffset` are **both** dispatched before the next render, i.e. no
     zero-length slice is handed to `processStereoBlock` (the `while` condition in
     `tests/test_helpers/seraphis_chain.h`);
  6. **velocity floor and ceiling (FR-031, ADDED 2026-07-31 by A3).** A `kNoteOnEvent` whose velocity lies
     strictly inside `(0, 1/127)` — the test uses `0.003f` — **allocates** a voice
     (`getActiveVoiceCount()` increments) rather than releasing one. This is the assertion that fails a
     truncating `uint8(velocity * 127)`, which yields `0` and is routed to `noteOff` by
     `SeraphisEngine::noteOn` (`seraphis_engine.h:374–377`). Additionally `velocity = 1.0f/127.0f` maps to
     `1` and `velocity = 1.0f` maps to **`127`** (not 128 — the upper clamp).

- **SC-023 — The macro parameters are inert (FR-041) — the Phase 9 negative control.** Test sketch, a
  SECTION of `Seraphis_ProcessorRendersHeldNote`. **Threshold:** a 4 s render with all five macro
  parameters at `0.0` and a 4 s render with all five at `1.0`, same seed and script, are
  **fingerprint-identical** — `compareFingerprints(...).withinTolerance()` on both channels — with a
  non-vacuity `REQUIRE(rms > 1e-4)` on the first render so the comparison is not between two silences.
  This is the honest Phase 8 assertion and is the control Phase 9 must **invert**.

- **SC-024 — The eight Aether targets are actually pushed (FR-034, FR-034a).** Test sketch, a SECTION of
  `Seraphis_ProcessorRendersHeldNote`, exercising `applyAetherTargets()` directly with **non-neutral**
  values (a render-diff at Phase 8's neutral defaults is provably vacuous — see FR-034a).
  **Threshold:**
  1. `size`: after `applyAetherTargets` with `size = 0.9` versus `size = 0.1` and enough blocks for the
     300 ms size smoother to settle, `AetherReverb::getEffectiveDelayLengthSamples(0)` differs;
  2. `mix` / `width` / the three sends: a render with `mix = 1.0` differs from one with `mix = 0.0` by
     more than `kSampleTolerance` (max absolute per-sample), with the same non-vacuity guard;
  3. the processor's `process()` calls `applyAetherTargets` every slice — asserted by the positive
     control in SC-006 clause 2, whose reference render pushes the same eight values per slice. *(A8
     applies here too: that inherited assertion is sound **only because** clause 2 runs at normalized
     master gain 0.5, where step 4b is an identity multiply. At gain 2.0 the fingerprint comparison would
     diverge for a reason that has nothing to do with the Aether targets.)*

- **SC-025 — The plugin roster is complete (FR-081).** **Threshold:** `node tools/lint-plugin-roster.js`
  exits **0** with Seraphis present in every roster it enumerates, **and** the lint is proven live by a
  deliberate-omission probe: temporarily removing the Seraphis entry from one roster (e.g.
  `tools/check-changelog-coverage.js`'s `PLUGINS` array) makes it exit non-zero. A lint that cannot be
  shown to fail is not evidence.

- **SC-026 — `setActive` lifecycle leaves no tail and does not allocate (FR-032).** Test sketch, a
  SECTION of `Seraphis_ProcessorLifecycle`. **Threshold:** with a note held and the reverb ringing,
  `setActive(false)` followed by `setActive(true)` and one 512-sample `process()` call yields absolute
  peak `< 1e-6`; and `setActive(true)` performs **exactly 0** allocations under
  `TestHelpers::AllocationScope` (live only because of FR-066a), with the same liveness probe SC-007 uses.
  **The same normative reading form applies (A9):** the count is taken from
  `TestHelpers::AllocationDetector::instance().getAllocationCount()`
  (`tests/test_helpers/allocation_detector.h:48–50`) inside the scope and asserted after it closes;
  `scope.getAllocationCount()` read inside the scope returns a member the destructor has not yet written
  (`:81–83`, `:85–87`) and would pass unconditionally.

- **SC-027 — The soft-limit parameter has a measurable effect (FR-044).** Test sketch, a SECTION of
  `Seraphis_ParamFlowReachesEngine`. **Threshold:** render the same seeded script twice, once with
  `kSoftLimitId` on (`setOutputSaturation(0.15f)`) and once off (`0.0f`), at a level where the saturator
  is engaged, and require:
  1. **non-vacuity first:** `REQUIRE(maxAbsDiff(on, off) > kSampleTolerance)` (`1e-4`,
     `tests/test_helpers/render_fingerprint.h:49`);
  2. the relative RMS difference exceeds a figure **measured during implementation and written into the
     test as a named constant with its provenance comment** (the project rule: a threshold is measured,
     never guessed).

  The pre-output-stage level used MUST be stated in the test and recorded in `compliance.md`. Phase 7
  measured the composed chain at ~−30 dBFS RMS (`specs/seraphis-phase7-voice-engine/compliance.md:181`),
  where a 0.15 tape-saturation amount is **not** obviously above `kSampleTolerance` — so the test drives
  the level up (more voices / higher master gain) until clause 1 passes. If no level reachable within the
  Phase 8 parameter surface produces a measurable difference, FR-044 is recorded in `compliance.md` as
  **verified by code inspection only**, with the measured deltas at each level tried — it is **not**
  silently marked verified.

---

## FR → SC traceability

Every FR maps to at least one criterion or is explicitly marked *build-time* (its failure is a
configure/compile/link error, which is itself the measurement) or *inspection* (recorded as such in
`compliance.md`, never silently). Shape follows Phase 7's table
(`specs/seraphis-phase7-voice-engine/spec.md`, "FR → SC traceability").

| FR | Verified by |
|---|---|
| FR-001, FR-003, FR-004, FR-005, FR-006 | build-time (SC-001) |
| FR-002 | build-time (SC-001) + SC-017 (`version.json` feeds `gen-repo-map`) |
| FR-007 | inspection + SC-020 |
| FR-007a | **SC-020** |
| FR-008 | SC-001 (build), SC-002 (test files present), SC-018 (`editor.uidesc` in bundle) |
| FR-009, FR-079 | inspection |
| FR-010 | SC-017 (`check-changelog-coverage.js` via FR-076) |
| FR-011, FR-012, FR-013, FR-014, FR-018 | SC-003 (pluginval loads the factory), SC-010 (state version) |
| FR-019 | inspection (no `INoteExpressionController` on `Controller`) + SC-003 (pluginval exercises the `queryInterface` surface) |
| FR-015, FR-016, FR-017 | **SC-004** (`auval -v aumu Srph KrAt`) |
| FR-020, FR-021 | **SC-011** |
| FR-022, FR-028 | **SC-007** (zero allocation in `process()`), SC-005 |
| FR-023 | **SC-007**, SC-005; clause 1 (2048 bound) via **SC-008**'s 4096 partition; clauses 2–3 (polyphony seed + tracker reset) via **SC-019 clause 3**; clause 4 (no announcement; invariant latency) via **SC-013**'s invariance matrix (A2) |
| FR-024 | **SC-005, SC-006 (clauses 2–3), SC-008**; the silence-flag clause via **SC-021** |
| FR-024a | **SC-019, SC-006, SC-027** |
| FR-025 | **SC-008, SC-022** |
| FR-026 | **SC-008** (the 4096 partition) |
| FR-027 | SC-008 (partitions 1 / 7 / 65 pass only if the 64-sample cadence is not treated as a size rule) |
| FR-029 | inspection + **SC-007** (a denormal storm inside `process()` is the observable) |
| FR-030 | **SC-021** |
| FR-031 | **SC-022** |
| FR-032 | **SC-026** |
| FR-033 | **SC-013** |
| FR-034, FR-034a | **SC-024** |
| FR-040, FR-043 | **SC-009** (the five-point sweep per ID) |
| FR-042 | **SC-009**, whose **last-point clause** (A6) is the only assertion that distinguishes "last value of the queue" from "first value"; the ID-range dispatch half is covered by the sweep |
| FR-041 | **SC-023** (inert) + **SC-010** default-state clause (initializers) |
| FR-044 | **SC-027** (with the inspection fallback stated there) |
| FR-045, FR-046 | **SC-010** (byte stability, version rejection, truncation) |
| FR-047 | **SC-010's controller clause** (A5) — the only criterion that calls `Controller::setComponentState()` and reads back all eight registered parameters |
| FR-048 | **SC-009** controller clause |
| FR-050, FR-051 | **SC-012 clause 2c** (A4) — it calls `scanPresets()` and asserts the config fields plus the `presets/Textures/` directory, so both halves of "filesystem and metadata agree" can fail; SC-003 additionally covers the live construction not crashing. The additive-only constraint is inspection (FR-009's `CLAUDE.md` entry) |
| FR-052 | build-time (SC-001) for the header; **inspection** for the negative half (no `UpdateChecker` instantiation) — corroborated by SC-012 clause 3's ASan lane staying free of a detached thread |
| FR-053 | build-time (SC-001); the 2048 argument via **SC-008**, `kMasterGainSmoothMs` via **SC-019 clause 1** |
| FR-054 | **SC-012 clause 2b** (A7 — control count, exact tag set, `COptionMenu`/`CCheckBox` types); SC-018 for the file reaching the bundle |
| FR-055, FR-056 | **SC-012** clauses 2a/3, SC-018 |
| FR-060 … FR-065 | build-time + **SC-002** |
| FR-066 | **SC-002** clause 1 (named cases) + **SC-012** clause 1 (the `[lifecycle]` tag) |
| FR-066a | **SC-007** liveness probe, **SC-026** |
| FR-067 | build-time (`static_assert`) |
| FR-068 | **SC-017** (`lint-float-bit-goldens.js`, `lint-midi-timing-goldens.js`) |
| FR-070 … FR-077 | **SC-025** (the roster lint is the only artefact that can fail on a missing entry) + SC-001/SC-016 |
| FR-078 | **SC-017** |
| FR-080 | inspection (recorded verification, no change) |
| FR-081 | **SC-025** |

---

## Edge cases

**Real-time-safety boundaries**

- `process()` is entered before `setupProcessing()` in some hosts' probe paths → the chain may be
  unprepared. The processor must produce silence and return `kResultOk`; `SeraphisEngine`'s own guard
  (`prepared_` check at `seraphis_engine.h:513`) already no-ops, but the wrapper must not dereference a
  null `unique_ptr`.
- A host delivers `data.numSamples > maxSamplesPerBlock` (out of spec, but observed). FR-026's
  sub-division makes this safe; without it, `AetherReverb::processStereoBlock` is handed more than its
  prepared `maxBlockSamples`.
- Events arriving with `sampleOffset >= data.numSamples` or negative: must be clamped into
  `[0, numSamples]` rather than producing a negative slice length.
- Two events at the same `sampleOffset`: the slice loop must dispatch *all* of them before rendering,
  otherwise a zero-length slice is produced (`seraphis_chain.h` handles this with a `while` on
  `eventAt[nextEvent] <= sliceStart`).
- `setActive(false)` during a render is not possible on the audio thread, but `setState()` from the UI
  thread while `process()` runs is: parameter storage must stay `std::atomic<>` throughout, and no
  `prepare()` may be reachable from `setState()`.
- Memory footprint at prepare: `SeraphisEngine::prepare` prepares **all 16 slots regardless of
  `cfg.polyphony`** (`seraphis_engine.h:195–200`), which the Phase 7 spec's RA-8 records as **33.6 MB @
  48 kHz** of capture rings at `captureSeconds = 4`, on top of `sizeof(SeraphisEngine) = 771 968 B`
  (`:159–164`), plus the reverb's own allocation. A host instantiating 16 Seraphis instances is
  ~0.5 GB. This is an accepted Phase 7 consequence, recorded here so Phase 8 does not "fix" it by
  reducing `captureSeconds` without a decision.

**Parameter extremes**

- Master gain at normalized 1.0 → linear 2.0 (+6 dB), applied to the **reverb return before**
  `processOutputStage` (FR-024 step 4b / FR-024a clause 3) — **not** into an already-limited output. The
  ordering is load-bearing: `processOutputStage` ends with `limiter_.processBlock(l, r, n)` and has no
  bypass path (`seraphis_engine.h:521`, ceiling `0.8912509f` at `true_peak_limiter.h:168`), so a
  post-limiter ×2.0 multiply would produce peaks up to ~1.78 and make SC-006's `≤ 0.891` bound
  unsatisfiable by construction. SC-006 covers the resulting bound; SC-019 covers the scaling itself.
- Master gain automated to 0 and back: the `OnePoleSmoother` at `kMasterGainSmoothMs = 20.0f` ramps, so
  no zipper — but it is advanced **per sample** (FR-024a clause 3), never per slice, or the render stops
  being block-size invariant and SC-008 fails a correct implementation.
- Master gain already at 0 when the host prepares: the smoother is **snapped** to the atomic on the first
  block after `setupProcessing()`/`setActive(true)`, not ramped from its previous value, so the render is
  silent from sample 0 (SC-019 clause 1) rather than for all but the first ~20 ms.
- A host that hands `process()` a block larger than the `maxSamplesPerBlock` it declared at
  `setupProcessing()`: harmless, because FR-023 clause 1 / FR-028 size everything to the constant 2048
  rather than to the host's declaration, and FR-026 sub-divides anything above 2048.
- Polyphony driven from 16 → 1 while 16 voices sound: `setPolyphony` is allocation-free
  (`seraphis_engine.h:321–350`) and only changes how many slots are summed; the voice-sum gain smoother
  (`kSumGainSmoothMs = 20.0f`, `:138`) must not be short-circuited by the wrapper re-calling
  `setPolyphony` every block with an unchanged value — the processor should only call it on change.
- Soft limit toggled every block: `setOutputSaturation` writes two `TapeSaturator` amounts
  (`:566–569`); toggling at block rate is a step change in a nonlinearity. The processor must apply it
  on change only, and Phase 9 may add smoothing.
- All five macro parameters at 1.0: inert in Phase 8 by FR-041, so audibly a no-op. A test asserting
  "macros do nothing yet" is the honest Phase 8 assertion and becomes a *negative* control that Phase 9
  must invert.

**Sample-rate and configuration changes**

- `setupProcessing` called repeatedly with different sample rates: both `prepare()` methods are
  documented as safely re-callable (`aether_reverb.h:1612` — *"May be called repeatedly"*). Scratch
  buffers do **not** need re-sizing, because FR-028 sizes them to the constant 2048 rather than to
  `setup.maxSamplesPerBlock`; the wrapper must nonetheless re-seed `cfg.polyphony` and the tracker
  (FR-023 clauses 2–3) on every call.
  **Latency does not change with the sample rate — or with anything else.** `getLatencySamples()` is
  `diffusionFftSize` whenever the spectral stage is enabled and `0` otherwise
  (`aether_reverb.h:2610–2613`) — a **sample count**, independent of the rate — so for the shipped config
  it is `1024` at 44.1, 48, 96 and 192 kHz alike. *(AMENDED, A2: the withdrawn text claimed "the only
  transition that ever needs announcing is the first prepare's `0 → 1024`". There is no such
  transition — both members are defaulted, so the value is 1024 from construction — and there is
  therefore no announcement; FR-023 clause 4 and SC-013 carry the corrected form.)*
- 44.1 / 48 / 96 / 192 kHz must all prepare without clamping surprises: `AetherReverb::prepare` clamps
  the rate into `[kMinSampleRate, kMaxSampleRate]` (`:1615–1616`) and `SeraphisEngine::prepare` floors it
  at 1.0 (`:202`). A rate outside the reverb's range would silently desynchronise the two — the
  processor should assert (debug-only) that both accepted the same rate.
- Sample-rate change while notes are held: `setActive(false)` → `setupProcessing` → `setActive(true)`
  is the host contract; FR-032 makes that path silent rather than leaving a tail at the old rate.

**Seed determinism**

- `SeraphisEngineConfig::seed` defaults to `1u` (`seraphis_engine.h:96`) and Phase 8 exposes **no** seed
  parameter. The scaffold must therefore pass a fixed, documented seed so SC-008's block-size
  invariance and SC-005's non-silence are reproducible; a seed derived from time or address would make
  every test flaky.
- Two plugin instances in one host get the same seed and therefore identical life-modulator
  trajectories. That is a *known* Phase 8 property, not a bug; Phase 9 owns any per-instance seed
  parameter.
- `AetherReverb::PrepareConfig::seed` defaults to `1` (`aether_reverb.h:1586`) independently. Both must
  be set explicitly in `seraphis_engine_config.h` (FR-053) rather than relying on the struct defaults, so
  a future default change in `dsp/` cannot silently move Seraphis's sound.

**Cross-platform**

- The macOS leg builds with `-ffast-math`; any finiteness check in a Phase 8 test must use bit patterns,
  and non-finite test inputs must be constructed from bit patterns through a volatile sink (FR-064).
- A green Windows build proves nothing about GCC/Clang: `node tools/check-portability.js` (SC-015) runs
  before commit. In particular, anything initialized from an SDK constant must be `const`, not
  `constexpr`.
- `resources/editor.uidesc` must not reference any custom view class name — on a leg where the
  registration TU was not linked, `VST3Editor::open()` silently drops the view and the lifecycle test
  passes vacuously.

---

## Deferred-scope calls resolved in this spec

The roadmap explicitly left these three to Phase 8. All three are **decided**; no question in this spec
remains open, and the plan may be written against the requirement text above.

1. **MPE / poly-aftertouch — out of scope for Phase 8.** Roadmap "Open Questions" item 5 (line 514)
   marked this a *"Phase 8/9 scope call"*; it is answered as **Phase 9**. Phase 8 ships the event-input
   bus and **no** `INoteExpressionController` (FR-019), because `SeraphisEngine`'s note surface —
   `noteOn(std::uint8_t, std::uint8_t)` / `noteOff(std::uint8_t)` (`seraphis_engine.h:370, 415`) — has no
   per-note expression input to drive, and a declared-but-empty interface asserts nothing. The host-cache
   caveat (adding an interface to a **released** controller FUID can invalidate cached class metadata) is
   recorded in `plugins/seraphis/CLAUDE.md` per FR-009 so Phase 9 decides knowingly.

2. **The preset category `Textures` is permanent and additive-only.** It is seeded now in **both** the
   filesystem subdirectory and the XML metadata (FR-050, FR-051 — the Membrum lesson, roadmap line 388).
   Phase 12 **extends** the category list; it MUST NOT rename `Textures`, because renaming a shipped
   category orphans every preset saved against it. Recorded in `plugins/seraphis/CLAUDE.md` (FR-009).

3. **Phase 8 ships `docs/` as a directory only; Phase 12 authors `index.html`.** `.github/workflows/docs.yml`
   needs no edit: its per-plugin loop globs `plugins/*/`, and a missing page is reported as
   `has_page: false` (`:152`) without failing (FR-008, FR-080).

---

## Review notes

Responses to the 2026-07-31 review pass. Every issue was applied except where noted; nothing was resolved
by relaxing a threshold.

**Applied with a corrected premise**

- **FR-066a (allocation operator overrides).** The issue's premise — *"`grep -rn allocation_operator_overrides
  plugins/` → 0 hits: no existing plugin test binary links it"* — is **wrong**. Run this session, that grep
  returns **two** hits: `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp:30` and
  `plugins/membrum/tests/unit/test_allocation_matrix.cpp:34`, both `#include
  <allocation_operator_overrides.h>`. The *conclusion* is nonetheless correct and the requirement was added
  in full: this spec's section F never required the include for `seraphis_tests`, so SC-007's liveness probe
  as previously written could not pass. FR-066a now mandates it in `tests/unit/test_main.cpp` and cites the
  two existing precedents rather than claiming none exist.

- **FR-034 (Aether targets) — applied, but the *suggested* test would have been vacuous.** The review
  proposed verifying FR-034 by "a render diff against a run with step 2 omitted". At Phase 8's neutral macro
  defaults all eight `computeAetherTargets()` values are **numerically identical** to the reverb's own
  constructor defaults (`aether_reverb.h:2207, 2280, 2290, 2295, 2333, 2336, 2749, 2750`), so that diff is
  provably bit-identical; and `AetherReverb` exposes no getter for any of the eight. FR-034a therefore
  requires the push to be factored into a testable free function `applyAetherTargets()`, and SC-024 exercises
  it with **non-neutral** values through `getEffectiveDelayLengthSamples` and a render diff. This is stricter
  than the suggestion, not weaker.

- **SC-006 (limiter ceiling).** Applied as suggested, plus the discriminating clause was made explicit as a
  positive/negative control **pair with a mandatory non-vacuity assertion**, because
  `renderSeraphisChain` has no "skip step 5" flag (`tests/test_helpers/seraphis_chain.h:132–190` — no options
  struct) and the negative control must therefore be a hand-rolled loop whose own discriminating power has to
  be asserted rather than assumed.

- **SC-014 (wrapper overhead).** Both the *fidelity* issue (drop or demote) and the *testability* issue (pin
  the protocol) were applied: the criterion is now explicitly **non-gating** and recorded in `compliance.md`,
  **and** its protocol is pinned (best-of-16 × 100 blocks, interleaved arms, allocation hoisted out of the
  chain arm, named lane, machine-idle caveat) so the recorded figure is meaningful.

**Rejected**

- None. All twenty-eight issues were accepted. The two premise corrections above are recorded here rather
  than as rejections, because in both cases the requested change was made — only the stated reason differs
  from the reviewer's.

**Pre-existing drift recorded but not fixed**

- `plugins/membrum/CMakeLists.txt` is absent from `.github/workflows/release.yml:137`'s FetchContent
  cache-key `hashFiles(...)` list. FR-072 records this; fixing Membrum is out of this phase's scope, and
  FR-081's roster lint is what will surface it.

