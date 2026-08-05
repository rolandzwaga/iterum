# Implementation Plan: Seraphis Phase 12 — Factory Presets & Release Readiness

**Spec:** `specs/seraphis-phase12-presets-release/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 12 (lines 602–610); hard precondition Phase 11.5
(lines 588–590, 653–655)
**Status:** PLAN — no implementation yet
**Date:** 2026-08-04

Every API signature quoted below was read from the header this session at the cited `file:line`.
Nothing here is recalled or inferred from another plugin's shape.

---

## 0. What this phase actually builds

Nine artifacts, no new DSP, no new parameter, no new registered type:

| # | Artifact | Path | Requirements |
|---|---|---|---|
| 1 | Category-set extension | `plugins/seraphis/src/preset/seraphis_preset_config.h` | FR-001 |
| 2 | Shared preset-definition header | `tools/seraphis_preset_defs.h` | FR-016, FR-016a |
| 3 | Generator | `tools/seraphis_preset_generator.cpp` | FR-010, FR-011, FR-013, FR-015 |
| 4 | CMake targets | root `CMakeLists.txt` | FR-010, FR-012 |
| 5 | Determinism script | `tools/check-preset-generator-determinism.js` | FR-014, FR-035a |
| 6 | Test-side support header | `plugins/seraphis/tests/preset_test_support.h` | FR-019…FR-029 machinery |
| 7 | Static harness TU | `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp` | FR-001 (all three clauses), FR-002, FR-004, **FR-005**, FR-006, FR-006a, FR-007, FR-008, FR-008a, FR-009, FR-019…FR-023, FR-029, FR-029a |
| 8 | Render-sweep TU | `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp` | FR-024…FR-028a (also consumes `tools/seraphis_preset_defs.h` for FR-024a's stimulus override — §1.2) |
| 9 | Roster / version / doc | `.claude/workflows/release-readiness.js`, `.claude/skills/release/SKILL.md`, `version.json`, `CHANGELOG.md`, `plugins/seraphis/CLAUDE.md` | FR-031…FR-034 |

Plus one **additive** change to shared test infrastructure — the thread filter in
`tests/test_helpers/allocation_detector.h` (§3.7) — which FR-028a explicitly leaves to this plan.

**Precondition handling (FR-030).** Items 1–8 are unblocked and are built now. Item 9's *release
verdict* is recorded as `DEFERRED` in the compliance document until Phase 11.5's three exit criteria
(roadmap 592–600) are green, with Phase 11.5's **measured** whole-`process()` figure and its
`file:line` provenance cited. `version.json`/`CHANGELOG.md` still land here (FR-033, Q7).

---

## 1. Component design

### 1.1 `makeSeraphisPresetConfig()` — the category set (FR-001)

**File:** `plugins/seraphis/src/preset/seraphis_preset_config.h` (currently 34 lines; `:24-31` is the
whole function).

Current, verbatim (`:24-31`):

```cpp
inline Krate::Plugins::PresetManagerConfig makeSeraphisPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Seraphis",
        /*.pluginCategoryDesc =*/ "Synth",
        /*.subcategoryNames =*/ {"Textures"}
    };
}
```

The **only** edit is the fourth initializer:

```cpp
        /*.subcategoryNames =*/ {"Textures", "Pads", "Drones", "Bells",
                                 "Choirs", "Motion", "Cinematic"}
```

The comment-style designators are load-bearing and must stay in `PresetManagerConfig`'s declaration
order — `FUID processorUID; std::string pluginName; std::string pluginCategoryDesc;
std::vector<std::string> subcategoryNames;` (`plugins/shared/src/preset/preset_manager_config.h:19-24`,
with the "field order matters for C++20 designated initializers" banner at `:16-18`).

`Textures` keeps its byte-exact spelling because `PresetManager::parsePresetFile` matches the parent
directory name against `config_.subcategoryNames` by exact `==` and leaves `subcategory` empty on a
miss (`plugins/shared/src/preset/preset_manager.cpp:95-103` — read this session at the loop
`for (const auto& subcatName : config_.subcategoryNames) { if (parentName == subcatName) …`).

The banner at `:8-13` of that header ("`Textures` is a SEED … Phase 12 EXTENDS this list") is updated
to record that the extension has happened and the list is now additive-only.

### 1.2 `tools/seraphis_preset_defs.h` — the shared definition table (FR-016a)

**Kind:** data header, build tooling, no DSP layer. **Namespace:** `Seraphis::PresetDefs`.
**Included by exactly three consumers, in two binaries:**

| Consumer | Binary | Why it needs the table |
|---|---|---|
| `tools/seraphis_preset_generator.cpp` | `seraphis_preset_generator` | writes the tree |
| `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp` | `seraphis_tests` | FR-029's in-process regeneration |
| `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp` | `seraphis_tests` | FR-024a's per-preset `AuditionStimulus` override (§4.2, SC-009) |

The third consumer costs no build change: `${CMAKE_SOURCE_DIR}/tools` is already on `seraphis_tests`'
include path (`plugins/seraphis/tests/CMakeLists.txt:97`, verified this session). It *does* mean the two
TUs in `seraphis_tests` see the same definitions in one binary — which is why every entity in this header
is `inline` (below), not merely a convenience.

ODR sweep (spec's, re-verified this session):
`grep -rn "SeraphisPresetDef|seraphis_preset_defs|PresetDefs" dsp/ plugins/ tools/ tests/` → **0 hits**.
`grep -rn "struct PresetDef|class PresetDef" …` → 5 hits, all anonymous-namespace, all in *separate*
executables (`tools/{disrumpo,gradus,innexus,ruinae}_preset_generator.cpp`, `tools/preset_generator.cpp`).
Because Seraphis's table is shared across three TUs (two of them in one binary) the anonymous-namespace
trick is unavailable,
so the distinct name `SeraphisPresetDef` is mandatory.

```cpp
#pragma once
// Data only: names, categories, descriptions, {ParamID, normalizedValue} pairs and the optional
// audition-stimulus override. NO state layout, NO component-stream serialization (spec C-3),
// NO EditMessage list (FR-016 / OQ-4 ratified NO).

#include "pluginterfaces/vst/vsttypes.h"   // Steinberg::Vst::ParamID

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Seraphis::PresetDefs {

struct ParamSetting {
    Steinberg::Vst::ParamID id;
    double normalized;              // 0..1, exactly what a host delivers (spec C-4)
};

/// FR-024a. Present only for outlier presets; absent means pitch 60 / velocity 0.8f.
struct AuditionStimulus {
    Steinberg::int16 pitch;
    float velocity;
};

struct SeraphisPresetDef {
    std::string_view name;          // file stem, XML `Name`, browser row (FR-005)
    std::string_view category;      // one of the seven; also the directory (FR-002/FR-003)
    std::string_view description;   // reserved for the XML `Comment` slot; unused this phase
    std::vector<ParamSetting> params;
    std::optional<AuditionStimulus> stimulus;
};

/// The 42 presets, in category order. Defined in a .h-inline function so all three TUs see one copy.
[[nodiscard]] const std::vector<SeraphisPresetDef>& allPresets();

/// FR-024a lookup for the render sweep, which discovers files on disk and must map a
/// (directory, stem) pair back to its definition to read the stimulus override.
/// MISS POLICY: returns nullptr. Callers MUST treat null as a FAILURE, never as
/// "use the default stimulus" - a silently-unmatched preset would render at pitch 60 /
/// velocity 0.8 and pass every arm while its authored outlier stimulus went untested.
[[nodiscard]] const SeraphisPresetDef* findDef(std::string_view category, std::string_view stem);

/// The seven category names, in C-1 order. MUST equal makeSeraphisPresetConfig().subcategoryNames.
inline constexpr std::array<std::string_view, 7> kCategories{
    "Textures", "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"};

/// The Info-chunk writer (see §1.3.3 for why it lives here rather than in the generator TU).
[[nodiscard]] std::string buildSeraphisInfoXml(std::string_view presetName,
                                               std::string_view subcategory);

}  // namespace Seraphis::PresetDefs
```

`allPresets()`, `findDef()` and `buildSeraphisInfoXml()` are defined `inline` in the header
(function-local `static const std::vector` for the former) so the three TUs share one definition and no
`.cpp` is added. **Two of the three TUs link into the same binary**, so a non-`inline` definition here is
a duplicate-symbol link error, not a style preference.

**Why `std::vector<ParamSetting>` and not a fixed array:** presets touch different numbers of IDs
(3–40). A vector built once inside a function-local static is allocated at first use in a *tool* and a
*test*, never on an audio thread — the RT rule does not reach here.

**`buildSeraphisInfoXml` placement — deliberate, and a reading of FR-016a.** FR-029 clause 2 requires
the test to prove the committed `Info` XML is **byte-identical** to what the generator writes. If the
writer is file-local to the generator TU (the spec's *New Components* table entry), the test must carry
a second copy of the 9-line ASCII template, which is exactly the drift FR-016a's shared header exists
to prevent. FR-016a forbids "state layout and serialization code"; the `Info` chunk is neither — it is
not part of the 2868-byte component stream and touches no `IBStreamer`. The name is new
(`buildSeraphisInfoXml`, not `buildInfoXml`), so the five same-name tool-local functions
(`tools/membrum_preset_generator.cpp:349`, `tools/ruinae_preset_generator.cpp:43`) are untouched.
**Fallback if the phase owner reads FR-016a strictly:** keep `buildInfoXml` file-local in the generator
and give the test its own builder, with the drift caught (loudly, not silently) by the byte comparison
itself. Recorded as **OI-1** in §9.

### 1.3 `tools/seraphis_preset_generator.cpp` (FR-010 … FR-016)

#### 1.3.1 Drive sequence (C-4, FR-013)

`Processor::processParameterChanges` is private (`plugins/seraphis/src/processor/processor.h`, declared
inside the private section — the public surface ends at `getState` on `:357` and the DataExchange /
`notify` overrides at `:364-368`). The only supported way to move a normalized value into the atomics
is a `process()` call carrying an `IParameterChanges`, which is exactly what the shipped fixture does:
`initialize(nullptr)` → `setupProcessing` → `setActive(true)`
(`plugins/seraphis/tests/seraphis_test_fixture.h:179-213`), then `setParam(id, normalized)` (`:219-221`)
and `processBlock(n)` (`:343-352`).

**`prepare()` in full — it does four things, not three.** Read this session,
`seraphis_test_fixture.h:179-213`: `initialize(nullptr)` → `setupProcessing(setup)` →
**`proc->setEffectsStageInstrumentedForTest(true)`** (`:210`) → `setActive(true)`. The third call is the
one the plan's earlier "initialize/setupProcessing/setActive" shorthand elided. It flips
`effectsStageInstrumented_` (`processor.h:1470`), which `processor.cpp:2421-2436` documents as
**FALSE on every shipping path** and which gates a `std::chrono::steady_clock::now()` read plus a full
pre-output bus copy **once per slice** — and the slice loop subdivides on the absolute 64-sample control
grid whenever a class-(b) smoother is unsettled (`processor.cpp:2421-2431`).

**Decision: the generator reuses `SeraphisTest::ProcessorFixture` for the plumbing, then turns the
instrumentation gate back OFF before driving audio.** `seraphis_preset_generator` is a *release-pipeline*
tool (`.github/workflows/release.yml:168-174`), so leaving a test-only audio-thread instrumentation path
enabled in shipping tooling is wrong on two counts: it is not the C-4 drive sequence FR-013 requires, and
it couples the committed preset tree's provenance to a header whose stated scope is test-only. The setter
is public (`processor.h:552`, no access specifier between `public:` at `:339` and it), so the disable is
one line. Precedent for reusing the fixture in a tool: `gen_v2_fixtures` already compiles a plugin's `tests/` file into a tool
(`plugins/gradus/tests/vstgui_test_stubs.cpp`, root `CMakeLists.txt:641`). The fixture is header-only,
pulls only `processor/processor.h` + `<vst_event_list.h>` + four `pluginterfaces` headers
(`seraphis_test_fixture.h:22-29`) and **no Catch2**, so including it costs nothing but two include
directories. The payoff is that the generator and FR-029's in-process regeneration drive the processor
through **one** code path; a divergence there would make FR-029 compare two different things and pass.

Per preset:

```cpp
SeraphisTest::ProcessorFixture fx;                       // owns a unique_ptr<Seraphis::Processor>
REQUIRE_OK(fx.prepare(44100.0, 64));                     // initialize/setupProcessing/INSTRUMENT/setActive
fx.proc->setEffectsStageInstrumentedForTest(false);      // Constitution II: FALSE on every shipping
                                                         // path (processor.h:1466-1470). A release tool
                                                         // drives the SHIPPED configuration, not the
                                                         // test one. State serialization is unaffected
                                                         // either way - this is fidelity to C-4, not a
                                                         // correctness fix.
for (const ParamSetting& s : def.params) fx.setParam(s.id, s.normalized);
fx.processBlock(64);                                     // ONE block: the whole fan-out latches here
Steinberg::MemoryStream stream;
REQUIRE_OK(fx.proc->getState(&stream));                  // processor.cpp:1856
```

**FR-029's in-process regeneration runs the identical five lines**, disable included — otherwise the two
sides of the comparison would differ in configuration, which is exactly the drift R-2 exists to prevent.
Recorded as **OI-5** in §9 so the phase owner ratifies the disable on both sides at once.

Untouched IDs keep their registered defaults (C-4), which `getState()` writes unchanged.
`processParameterChanges` takes the **last** point of each queue (`processor.cpp:1923-1929` banner), and
`setParam` writes one point at offset 0, so one block latches everything.

The four `SpectralState` payloads need **no** extra drive: `getState()` calls
`syncAuthoringMirrorFromDropdowns()` first (`processor.cpp:1866`) and then writes
`spectralSlotsAuthoring_[s]` for every slot with no outstanding handoff (`:1893-1897`). So setting IDs
409–412 through the parameter fan-out is sufficient to place the right factory payloads in the stream —
which is why FR-016's "no `notify()`, no `IMessage`" is not a limitation.

`[partials]` is written from `partialPanStaging_` / `partialPanOverrideBits_` / `partialMaskBits_`
(`processor.cpp:1911-1914`), all of which are zero on a fresh `Processor` — FR-006a's all-zero
requirement is satisfied *by never touching the edit channel*, and the harness asserts it rather than
assuming it.

#### 1.3.2 Container writer (FR-003, C-3)

File-local, anonymous namespace, modelled byte-for-byte on
`tools/membrum_preset_generator.cpp:363-405` (read this session):

```
offset 0   "VST3"                     4 B
offset 4   uint32 version = 1         4 B
offset 8   class id, 32 ASCII chars  32 B     <- NOT hardcoded, see below
offset 40  int64 listOffset           8 B
offset 48  Comp payload            2868 B
           Info payload            |info| B
listOffset "List", uint32 2, then {"Comp", int64 off, int64 size}, {"Info", int64 off, int64 size}
```

The class id comes from `Seraphis::kProcessorUID` at run time (C-3):

```cpp
Steinberg::char8 classIdAscii[33] = {};
Seraphis::kProcessorUID.toString(classIdAscii);   // extern/vst3sdk/pluginterfaces/base/funknown.h:295
f.write(classIdAscii, 32);
```

`kProcessorUID` is `FUID(0xD13457BF, 0x55DC4576, 0xA26AF99B, 0x8873244D)`
(`plugins/seraphis/src/plugin_ids.h:32`). No literal is ever written into the tool.

`Info` XML (via `buildSeraphisInfoXml`, §1.2), six attributes, Membrum's exact form
(`tools/membrum_preset_generator.cpp:349-361`) with Seraphis values:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<MetaInfo>
  <Attr id="MediaType" value="VstPreset" type="string"/>
  <Attr id="PlugInName" value="Seraphis" type="string"/>
  <Attr id="PlugInCategory" value="Synth" type="string"/>
  <Attr id="Name" value="{stem}" type="string"/>
  <Attr id="MusicalCategory" value="{category}" type="string"/>
  <Attr id="MusicalInstrument" value="{category}" type="string"/>
</MetaInfo>
```

`PlugInName`/`PlugInCategory` equal `PresetManagerConfig::pluginName` / `pluginCategoryDesc`
("Seraphis" / "Synth", `seraphis_preset_config.h:27-28`) — FR-003. `\n` line endings are written
explicitly (`std::ofstream` in `std::ios::binary`), so the bytes are identical on all three legs.

#### 1.3.3 `main` (FR-011)

**`moduleHandle` must be defined in this TU or the target does not link.** `moduleinit.cpp` (in the
source list, §1.4) declares `extern void* moduleHandle;` at
`extern/vst3sdk/public.sdk/source/main/moduleinit.cpp:21` and dereferences it at `:85`. The in-tree file
the plan previously credited with supplying it — `plugins/seraphis/tests/vstgui_test_stubs.cpp` — is 13
lines long and defines **only** `GetPluginFactory` (`:12`, read this session); Seraphis's `moduleHandle`
lives in `plugins/seraphis/tests/unit/test_main.cpp:24`, which is a Catch2 main and is **not** in the
generator's source list. The precedent this plan follows elsewhere, `gen_v2_fixtures`, supplies it from
its own main (`tools/gen_v2_fixtures/main.cpp:19`). So:

```cpp
// Satisfies moduleinit.cpp's `extern void* moduleHandle;` (vst3sdk
// public.sdk/source/main/moduleinit.cpp:21). MUST be a MUTABLE global with EXTERNAL linkage -
// `static`, `const`, or an anonymous namespace all turn it into an unresolved external.
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;
```

Without this, MSVC and the GCC/Linux leg `release.yml:168-174` depends on both fail with an unresolved
external — the exact failure FR-015 exists to prevent, and one a Windows-only build would *also* hit, so
it is caught at step 2 of §8, not at release time.

```cpp
int main(int argc, char* argv[]) {
    std::filesystem::path out = (argc > 1) ? std::filesystem::path(argv[1])
                                           : std::filesystem::path("plugins/seraphis/resources/presets");
    for (std::string_view c : PresetDefs::kCategories) std::filesystem::create_directories(out / c);
    for (const SeraphisPresetDef& def : PresetDefs::allPresets()) { … write out/def.category/(def.name + ".vstpreset") … }
    return failures == 0 ? 0 : 1;
}
```

Iteration order is the **definition order of `allPresets()`** — never a directory iteration and never an
unordered container — which is half of FR-014's determinism. The other half is that no timestamp, no
path string and no RNG enters any byte: the `Comp` chunk is `getState()` output over stored values, and
the `Info` chunk is derived from the definition table. Argv contract matches
`./build/bin/seraphis_preset_generator generated-presets` (`.github/workflows/release.yml:170-174`,
read this session).

#### 1.3.4 Why VSTGUI is not linked (FR-015)

`processor.cpp`'s only `ui/` include is `ui/edit_message.h` (a POD + three `constexpr` strings), and the
parameter packs reach the SDK only through `plugins/shared/src/ui/parameter_helpers.h`, which includes
`public.sdk/source/vst/vstparameters.h`. `gen_v2_fixtures` is the in-repo proof that a plugin
`processor.cpp` links with `KrateDSP + KratePluginsShared + sdk` and **no `vstgui_support`**
(root `CMakeLists.txt:644-649`). `tools/krate-render` is *not* the precedent: it needs `vstgui_support`
because `membrum_core` bundles the controller and UI TUs (`tools/krate-render/CMakeLists.txt:17-29`),
and Seraphis has no static core library at all — `plugins/seraphis/CMakeLists.txt:18-67` puts every
`.cpp` inside one `smtg_add_vst3plugin` call, which the SDK builds as a CMake `MODULE`.

### 1.4 CMake wiring (FR-010, FR-012)

Appended to root `CMakeLists.txt` immediately after the `membrum_preset_generator` block
(`:575-590`), before `gen_v2_fixtures` (`:592`):

```cmake
add_executable(seraphis_preset_generator
    tools/seraphis_preset_generator.cpp

    # The shipped processor, compiled as a source (Seraphis has no static core library —
    # plugins/seraphis/CMakeLists.txt:18-67 puts everything in one smtg_add_vst3plugin MODULE).
    plugins/seraphis/src/processor/processor.cpp

    # VST3 SDK boilerplate, same set gen_v2_fixtures needs (root CMakeLists.txt:632-638)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp

    # GetPluginFactory stub ONLY - this file is 13 lines and defines nothing else
    # (plugins/seraphis/tests/vstgui_test_stubs.cpp:12, read this session).
    # `moduleHandle` is NOT here; it is defined in seraphis_preset_generator.cpp (§1.3.3),
    # exactly as tools/gen_v2_fixtures/main.cpp:19 does for its own target.
    plugins/seraphis/tests/vstgui_test_stubs.cpp
)
target_link_libraries(seraphis_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)
target_include_directories(seraphis_preset_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/src      # "plugin_ids.h", "parameters/…"
    ${CMAKE_SOURCE_DIR}/plugins/seraphis/tests    # seraphis_test_fixture.h
    ${CMAKE_SOURCE_DIR}/tests/test_helpers        # <vst_event_list.h>  (header only, NOT the
                                                  # test_helpers target — that links Catch2
                                                  # INTERFACE, tests/test_helpers/CMakeLists.txt:14-17)
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

Warning posture copies `gen_v2_fixtures` (`root CMakeLists.txt:684-700`): MSVC `/W4 /permissive-
/Zc:__cplusplus /wd4100 /wd4458`, otherwise `-Wall -Wextra -Wpedantic -Wno-unused-parameter`. Seraphis
additionally needs MSVC `/wd4459` for the `kPi` shadow in `timevar_comb_bank.h:915`
(`plugins/seraphis/CMakeLists.txt:120-135` documents it) — the same suppression, on this target only.

**Zero-warning check (SC-018):** `cmake --build … --target seraphis_preset_generator` must **compile and
link** clean on MSVC *and* on WSL/GCC before the phase is called done. The link half is not ceremony:
`moduleHandle` (§1.3.3) is a link-time-only failure and MSVC and GCC fail it identically, so §8 step 2's
gate is "the binary exists and emits files", never "it compiled".

`release.yml` needs **no** edit: its `*` case already maps `seraphis` → `seraphis_preset_generator`
(`release.yml:158-166`), builds it (`:168-169`), runs `./build/bin/seraphis_preset_generator
generated-presets` (`:170-174`) and uploads with `if-no-files-found: error` (`:180`). The FetchContent
cache key already hashes `plugins/seraphis/CMakeLists.txt` (`:138`, verified this session).

### 1.5 `tools/check-preset-generator-determinism.js` (FR-035a, SC-016)

Node (project rule), no dependencies, CLI:

```
node tools/check-preset-generator-determinism.js [--bin <path>]
```

Default binary resolution, in order: `build/windows-x64-release/bin/Release/seraphis_preset_generator.exe`,
`build/bin/seraphis_preset_generator`, `build/bin/seraphis_preset_generator.exe`.

1. `fs.mkdtempSync` two fresh dirs `A`, `B`; run the binary into each; walk both trees; assert identical
   relative-path sets and `Buffer.compare` equal for every file. (The byte comparison is legitimate here
   and **only** here: one binary, one machine, one run set — C-7.)
2. Snapshot `A`'s file mtimes+bytes; run the binary a **third** time with `A` as the output directory;
   assert **0** files changed byte-wise. (Idempotence — the half that previously had no measurement at
   all.)
3. Exit 0 / print a one-line summary; exit 1 with the first differing path on any failure. Temp dirs are
   removed on success and **kept** on failure (path printed) so the diff is inspectable.

### 1.6 `plugins/seraphis/tests/preset_test_support.h` — the harness machinery

Header-only, `namespace SeraphisTest`, included by both new TUs — **which link into the same binary**
(`seraphis_tests`, `plugins/seraphis/tests/CMakeLists.txt:5-79`). Therefore **every free function
declared below is defined `inline`, and any table is a function-local `static const`.** The declarations
in the snippet omit the keyword only for readability; a literal transcription without `inline` is a
duplicate-symbol link error, not a warning. (Same rule, same reason, as §1.2.) The alternative — adding
`preset_test_support.cpp` to the enumerated source list — is **not** taken: the header carries templates
and `constexpr` tables and a second TU buys nothing.

ODR sweep run this session:
`grep -rn "DecodedPresetState|decodePresetState|preset_test_support|PresetTestSupport|fingerprintDistance"
dsp/ plugins/ tools/ tests/` → **0 hits**.

Contents:

```cpp
// --- discovery ---------------------------------------------------------------
/// resources/presets root. SERAPHIS_RESOURCES_DIR is already defined for seraphis_tests
/// (plugins/seraphis/tests/CMakeLists.txt:112), so no walk-up loop is needed — Membrum's
/// walk-up (test_factory_kit_presets.cpp:55-69) exists only because it has no such define.
[[nodiscard]] std::filesystem::path factoryPresetRoot();
[[nodiscard]] std::vector<std::filesystem::path> allPresetFiles();   // sorted, deterministic

// --- container -----------------------------------------------------------------
struct PresetFile {
    std::filesystem::path path;
    std::string stem;                       // == Name attribute (FR-003)
    std::string category;                   // parent directory name
    std::string classIdAscii;               // 32 chars
    std::vector<std::uint8_t> comp;         // the component chunk
    std::string info;                       // the Info chunk, raw bytes
    std::string parseError;                 // empty on success
};
[[nodiscard]] PresetFile parseVstPreset(const std::filesystem::path&);

// --- Info XML ------------------------------------------------------------------
/// Six attributes; a missing attribute yields an absent entry, never a default.
[[nodiscard]] std::map<std::string, std::string> parseInfoAttributes(std::string_view xml);

// --- typed decode (FR-025a) -----------------------------------------------------
struct DecodedPresetState { … };            // see §2.1
[[nodiscard]] bool decodePresetState(const std::vector<std::uint8_t>& comp, DecodedPresetState& out,
                                     std::string& why);

// --- timeline (C-6.1) ------------------------------------------------------------
struct SweepTimeline { double A, Rel, RT60, susBegin, susEnd, H, settle, W, total; bool aetherFreeze,
                       atmosOrFxFreeze; };
[[nodiscard]] SweepTimeline makeTimeline(const DecodedPresetState&);

// --- measurement -----------------------------------------------------------------
[[nodiscard]] bool bufferIsFinite(std::span<const float>);            // BIT PATTERN, never std::isnan
[[nodiscard]] double rmsOver(std::span<const float> l, std::span<const float> r,
                             std::size_t first, std::size_t lastExclusive);
[[nodiscard]] std::vector<double> perSecondRms(std::span<const float> l, std::span<const float> r,
                                               std::size_t first, std::size_t lastExclusive, double sr);
[[nodiscard]] double fingerprintDistance(const Krate::DSP::TestUtils::RenderFingerprint&,
                                         const Krate::DSP::TestUtils::RenderFingerprint&);
```

`bufferIsFinite` uses `Krate::DSP::detail::isNaN` / `isInf` — the same pair `processor.cpp:482` uses on
untrusted stream floats, which is the project's `-ffast-math` rule (never `std::isnan`).

**Definition-table lookup, and its miss policy.** The render sweep discovers files on disk
(`allPresetFiles()`) but needs the *definition* to read FR-024a's `AuditionStimulus` override. It calls
`Seraphis::PresetDefs::findDef(category, stem)` (§1.2) and **`REQUIRE`s a non-null result for 42/42
before any render begins**, printing the unmatched `(category, stem)` on failure. A null result is never
silently downgraded to "use pitch 60 / velocity 0.8": that would let an authored outlier stimulus go
untested while every arm passed. This is FR-029 clause 1's both-directions file-set check applied a
second time, at the point of use, because the two checks live in different TUs and a sweep run in
isolation must not depend on the static TU having run first.

### 1.7 Additive thread filter in `allocation_detector.h` (FR-028a's plan choice)

**Decision: add an opt-in, default-off thread filter — do NOT route FR-028a to the valgrind lane.**

Reasons: (a) the valgrind-linux lane runs *nightly*, so an FR-028a regression would be invisible to the
PR that caused it, while SC-015a is a phase criterion; (b) valgrind reports leaks/races, not "this
thread allocated", so proving clause (c) there needs a bespoke suppression story anyway; (c) the filter
is 6 lines, default-off, and provably cannot change the six existing consumers' behaviour.

`tests/test_helpers/allocation_detector.h` (currently 139 lines; the class is `:26-68`, `AllocationScope`
`:75-95`) gains:

```cpp
/// Per-thread opt-in. Allocation-free on first touch because this header is only ever linked into
/// test EXECUTABLES (local-exec / initial-exec TLS -> static TLS area, no __tls_get_addr calloc).
/// DO NOT use from a dynamically loaded module: general-dynamic TLS allocates on first touch, and
/// allocation_operator_overrides.h:66-94 calls recordAllocation() from operator new itself.
inline thread_local bool tAllocationTrackThisThread = false;

class AllocationDetector {
    // ... unchanged ...
    void setThreadFilterEnabled(bool on) noexcept { threadFilter_.store(on, std::memory_order_release); }
    [[nodiscard]] bool threadFilterEnabled() const noexcept {
        return threadFilter_.load(std::memory_order_acquire);
    }
    void recordAllocation() {
        if (!tracking_.load(std::memory_order_acquire)) return;
        if (threadFilter_.load(std::memory_order_acquire) && !tAllocationTrackThisThread) return;
        allocationCount_.fetch_add(1, std::memory_order_relaxed);
    }
private:
    std::atomic<bool> threadFilter_{false};   // DEFAULT OFF: every existing usage is unchanged
};

/// RAII: enable the filter, start tracking, restore both on scope exit.
class ThreadScopedAllocationScope { … };
```

The audio thread in FR-028a opens with, in this order and before anything else:
`TestHelpers::tAllocationTrackThisThread = true;` then `enableFTZDAZ();` (R-9: MXCSR is **fresh** per
thread on Windows). Both run **before** the `ThreadScopedAllocationScope` is opened. Existing
`AllocationScope` users see identical behaviour because `threadFilter_` is false unless a
`ThreadScopedAllocationScope` is alive.

**Portability note — the real mechanism.** Constant initialisation removes the *dynamic-initialisation
guard* (`__tls_init`), but that is **not** what makes first touch allocation-free. On glibc a
`thread_local` in the **general-dynamic** TLS model is reached through `__tls_get_addr`, which `calloc`s
the module's TLS block on first touch **per thread**, constant initialiser or not. Since
`recordAllocation()` is called from the replacement `operator new`
(`tests/test_helpers/allocation_operator_overrides.h:66-94`), that would be re-entrancy.

The conclusion holds for a different reason: **`allocation_detector.h` is only ever linked into test
EXECUTABLES**, so the TLS model is local-exec / initial-exec — the block is part of the thread's static
TLS area, allocated by the runtime at thread creation, and first touch performs no allocation. A one-line
guard goes in the header: *this helper must not be used from a dynamically loaded module (`.so`/`.dll`
loaded at run time); the thread filter's allocation-freedom depends on static TLS.*

Two consequences the plan carries forward:
- FR-028a's audio thread **touches `tAllocationTrackThisThread` (writes `true`) before the
  `ThreadScopedAllocationScope` is opened**, so even the local-exec first-touch cost — whatever it is —
  falls outside the measured window rather than inside it.
- The filter stays a `thread_local bool` rather than an `std::atomic<std::thread::id>` for the unrelated
  and still-valid reason that the latter's lock-freedom is not guaranteed and its comparison would run
  inside every allocation.

---

## 2. Algorithms — stated exactly, so no implementer guesses

### 2.1 FR-025a decode: order, tripwires, and what is hand-skipped

`DecodedPresetState` holds one instance of each shipped pack plus the payloads:

```cpp
struct DecodedPresetState {
    Steinberg::int32 version = 0;
    Seraphis::GlobalParams     global;      Seraphis::MacroParams      macro;
    Seraphis::CloudParams      cloud;       Seraphis::MorphParams      morph;
    Seraphis::LifeModParams    life;        Seraphis::BodyParams       body;
    Seraphis::AtmosphereParams atmos;       Seraphis::AetherParams     aether;
    Seraphis::EffectsParams    effects;
    std::array<Krate::DSP::SpectralState, 4> payloads{};
    std::array<float, 64> partialPan{};
    std::uint64_t panOverrideBits = 0, maskBits = 0;
};
```

The read order is `getState()`'s, verbatim (`processor.cpp:1868-1914`), with the documented per-block
byte sizes used as **cumulative offset tripwires**:

| Step | Call (real signature) | Bytes | Cumulative |
|---|---|---|---|
| 0 | `streamer.readInt32(version)` | 4 | 4 |
| 1 | `loadGlobalParams(GlobalParams&, IBStreamer&)` — `global_params.h:218` | 12 | 16 |
| 2 | `loadMacroParams(MacroParams&, IBStreamer&)` — `macro_params.h:132` | 20 | 36 |
| 3 | `loadGlobalSeed(GlobalParams&, IBStreamer&)` — `global_params.h:280` | 4 | 40 |
| 4 | `loadCloudParams(CloudParams&, IBStreamer&)` — `cloud_params.h:302` | 44 | 84 |
| 5 | `loadMorphParams(MorphParams&, IBStreamer&, std::array<SpectralState,4>&)` — `morph_params.h:400-402` | 52 + 2164 | 2300 |
| 6 | `loadLifeModParams(LifeModParams&, IBStreamer&)` — `life_mod_params.h:298` | 40 | 2340 |
| 7 | `loadBodyParams(BodyParams&, IBStreamer&)` — `body_params.h:369` | 52 | 2392 |
| 8 | `loadAtmosphereParams(AtmosphereParams&, IBStreamer&)` — `atmosphere_params.h:377` | 68 | 2460 |
| 9 | `loadAetherParams(AetherParams&, IBStreamer&)` — `aether_params.h:297` | 72 | 2532 |
| 10 | `loadEffectsParams(EffectsParams&, IBStreamer&)` — `effects_params.h:442` | 64 | 2596 |
| 11 | `[partials]`: 64 × `readFloat` + 2 × `readInt64u` | 272 | **2868** |

**Correction to the spec's FR-025a wording, in the plan's favour:** the four 541-byte payloads do **not**
need hand-skipping. `loadMorphParams`'s third parameter *is* the payload destination
(`morph_params.h:400-402`, signature read this session), so step 5 consumes all 2216 bytes and delivers
the decoded `SpectralState`s that FR-029 clause 5 needs — using the shipped decoder path rather than a
second one. Only the `[partials]` block (step 11) is read by hand, and it is read with the same
`readFloat`/`readInt64u` calls `loadPartialOverrides` uses (`processor.cpp:472-499`) but into plain
(non-atomic) fields, with **no clamping** — the harness must see the raw stored bytes to assert FR-009's
`[-1, 1]` range and FR-006a's all-zero rule, not the clamped value the processor would install.

**Tripwires (mandatory):**
- after every step, `REQUIRE(streamer.tell() == cumulative[step])` — catches a shipped block growing
  without this decoder growing with it, and names the block that drifted;
- at the end, `REQUIRE(streamer.tell() == 2868)` — FR-025a's mandatory total tripwire;
- `REQUIRE(comp.size() == 2868)` before decoding at all (FR-006 / SC-004).

`IBStreamer` is constructed over a `Steinberg::MemoryStream` built from the `comp` bytes; the position is
read with `int64 tell()` — declared pure-virtual on `FStreamer`
(`extern/vst3sdk/base/source/fstreamer.h:49`) and overridden public on
`IBStreamer` (`:215`), the same class every shipped `load*Params` already streams through.

### 2.2 Timeline arithmetic (C-6.1) — exact

All inputs come from `DecodedPresetState` (never from the definition table, never hardcoded):

```
A      = (life.envMode == 1 /*Growth, kEnvelopeModeLabels[1]*/ ? life.growthDurationSec
                                                               : life.stage0Ms * 1e-3)
         + life.stage1Ms * 1e-3                                                   [s]
Rel    = life.releaseMs * 1e-3                                                     [s]
RT60   = aether.decaySeconds                                                       [s], ∈ [0.5, 60]
susBegin = A + 1.0     susEnd = A + 4.0
H      = A + 5.0
frozen = aether.freeze || atmos.freeze || effects.spectralFreeze
G      = frozen ? 2.0 : 0.5
Settle = H + Rel + G
W      = aether.freeze ? 60.0 : (frozen ? 20.0 : 10.0)
Total  = Settle + W
```

Sources: `envMode` is the list index with Standard = 0, Growth = 1
(`dropdown_mappings.h:190-191`, mirroring `SeraphisVoice::EnvelopeMode`); `growthDurationSec`,
`stage0Ms`, `stage1Ms`, `releaseMs` are `LifeModParams` fields (`life_mod_params.h:81-84`) stored in
seconds/milliseconds by `handleLifeModParamChange` (`:140-163`, all four via `logMapFromNormalized`);
`decaySeconds` is `AetherParams::decaySeconds` (`aether_params.h:77`), a **true RT60** — the same value
drives `const float t60dc = decaySeconds;` in the Jot gain computation
(`dsp/include/krate/dsp/effects/aether_reverb.h:3139`).

Sample conversion: `n(t) = static_cast<std::size_t>(std::llround(t * sampleRate))`, windows half-open
`[n(t0), n(t1))`. Block size is **fixed at 512** for every sweep render (the block-size-invariance claim
belongs to `processor_audio_test.cpp:91-96` and is not re-litigated here — spec Edge Cases). The render
length is `ceil(Total * sr / 512)` blocks; the extra tail samples past `Total` are rendered but not
measured.

### 2.3 Bounded arm (C-6.2, FR-025 / SC-010 / SC-010a)

```cpp
constexpr float kLimiterCeilingLin  = 0.8912509f;   // effects_chain_test.cpp:332
constexpr float kCeilingAllowanceDb = 0.1f;         // effects_chain_test.cpp:854
const float kPeakBound = kLimiterCeilingLin * std::pow(10.0f, kCeilingAllowanceDb / 20.0f);
                                                    // = 0.901567…
```

For every sample of both channels over `[0, Total]`: not NaN and not Inf **by bit pattern**, and
`|s| <= kPeakBound`. Both are accumulated into counters and asserted **once** after the loop (a
`REQUIRE` per sample would make the sweep unusably slow and floods Catch2's section reporting).

### 2.4 Sustain arm (FR-024 / SC-009)

`rms = rmsOver(L, R, n(A+1.0), n(A+4.0))` where
`rmsOver = sqrt((Σ L² + Σ R²) / (2·N))`. Assert `rms > 1e-3` (= −60 dBFS). Failure message names the
preset, the decoded `A`, and the measured dBFS.

### 2.5 Tail arms (C-6.3, FR-026 / SC-011 / SC-012)

Per-second series: partition `[Settle, Settle + W)` into `floor(W)` one-second windows, one RMS each
(same `rmsOver` definition).

**Digital-silence guard — arms (1) and (2) ONLY.** If any window's RMS is `< 1e-9`, the *freeze* arms
fail with an explicit "frozen preset produced digital silence" message rather than dividing by zero: a
silent window under an engaged freeze means the freeze is broken, which is precisely what those arms
exist to catch.

**Arm (3) carries NO such guard, deliberately.** SC-011 / FR-026 case 3 is a **one-sided** bound
(`dropDb >= min(0.5·60·W/RT60, 20.0)`), and the spec states outright that "decaying faster than the
reverb predicts (a dry-dominant preset) always passes" (C-6.3 case 3). A dry-dominant preset with a short
RT60 — the legal range starts at 0.5 s (`aether_params.h:45-46`) — is fully decayed well before
`Settle = H + Rel + 0.5 s` and produces exactly the sub-1e-9 windows a guard would reject. **A
fully-decayed no-freeze tail is a PASS, not a failure.** The degenerate case is already handled by
§2.5(3)'s own `max(rms(final second), 1e-12)` floor, which yields a large `dropDb` and passes. Applying
the guard there would fail correct presets and contradict the formula in the same section.

**(1) Aether-freeze ON** — `maxRms/minRms` over the 60 windows:
```
bandDb = 20 * log10(maxRms / minRms)      require bandDb <= 2.0            // ±1.0 dB
```
The 60 s duration is roadmap line 282's, unreduced. The band widening 0.5 → 1.0 dB is the phase's one
disclosed relaxation (C-6.3) and is repeated in the failure message so it can never be mistaken for the
`AetherReverb`-alone criterion.

**(2) Atmos/FX-freeze only** — the spec's literal wording ("final-second RMS ≤ loudest-second RMS +
1.0 dB") is **tautological**: `loudest ≥ final` by construction, so the assertion cannot fail. The plan
repairs it without touching the threshold number:

```
r0      = rms(first second of the window)
rFinal  = rms(final second)
rMax    = max over all windows
(a) NORMATIVE:  20*log10(rFinal / r0) <= 1.0        // non-growing, end to end
(b) REPORTED, promoted to gating after measurement across all 42:
                20*log10(rMax  / r0) <= 1.0        // no intermediate growth either
```

Clause (a) is what ships gating in the first build; clause (b) is computed and printed for all 42
presets in the same run, and is promoted to a `REQUIRE` once the measured margin is recorded in the
compliance document. Nothing is relaxed — an arm that could not fail is replaced by one that can.
**Recorded as OI-2 (§9)** because it changes what SC-012 arm 2 measures.

**(3) No freeze** —
```
dropDb = 20 * log10( rms(first second) / max(rms(final second), 1e-12) )
require dropDb >= min(0.5 * 60.0 * W / RT60, 20.0)      with W = 10
```
Worked, matching the spec: RT60 ≤ 6 s ⇒ ≥ 20 dB; RT60 = 30 s ⇒ ≥ 10 dB; RT60 = 60 s ⇒ ≥ 5 dB. The bound
is one-sided; decaying faster than the reverb predicts always passes — **including all the way to
digital silence**, where the `1e-12` denominator floor is the whole mechanism and no guard fires.
`rms(first second)` is *not* floored: a first window that is itself at 1e-12 means the preset was
already silent at `Settle`, which arm (3) still passes (it decayed) and which SC-009's sustain arm has
already rejected if the preset is silent during `[A+1, A+4]`.

### 2.6 Reproducibility (FR-027a / SC-026) and distinctness (FR-027b / SC-028)

Reproducibility: two `ProcessorFixture`s, each freshly `prepare(44100, 512)`d, each loaded with the same
`comp` chunk via `setState`, each driven through the identical event schedule, both rendered over
`[0, H]`. Fingerprint the interleaved-by-channel concatenation `L ++ R` with
`Krate::DSP::TestUtils::fingerprintRender(std::span<const float>)`
(`tests/test_helpers/render_fingerprint.h:63`, re-read this session) and require
`compareFingerprints(a, b).withinTolerance()` (`:100`, `:94-96`) — i.e. worst metric relative
error ≤ `kMetricTolerance = 1e-5` (`:52`), worst checkpoint error ≤ `kSampleTolerance = 1e-4f` (`:49`).
**No float bit digest and no integer digest derived from float bits enters this phase.**

**Distinctness distance — LEVEL-NORMALISED, because the obvious metric measures the wrong thing.**
The spec leaves the metric to the plan. The plan's first draft took a relative L∞ over
`{rms, peak, meanAbs, totalVariation}` of the raw fingerprint. That is **rejected**: `rms`, `peak` and
`meanAbs` are all pure amplitude aggregates (`tests/test_helpers/render_fingerprint.h:63-88`, read this
session — `sumSquares`, `max(|s|)`, `sumAbs/n`), so two *timbrally identical* presets differing only in
master gain score a large `d` and pass. C-10 exists to catch "42 near-identical presets", and a
loudness-dominated metric does not catch it.

**What ships instead.** For each preset, take the SC-009 sustain-window buffer (the same
`L ++ R` concatenation over `[A+1, A+4]` at 44 100 Hz), compute its RMS, and **scale the whole buffer by
`1 / max(rms, 1e-12)` before fingerprinting.** Fingerprint the normalised buffer. Then:

```
after unit-RMS normalisation:  rms == 1 by construction  ->  EXCLUDED (carries no information)

d(P,Q) = max over m ∈ {peak, meanAbs, totalVariation} of
             |m_P − m_Q| / max(|m_P|, |m_Q|, 1e-12)
```

Each surviving term is now a **shape** statistic of a unit-level signal:
- `peak` of a unit-RMS buffer **is the crest factor** — envelope/transient character;
- `meanAbs` of a unit-RMS buffer is the inverse form factor — waveform fullness (a sine, a square and a
  sparse grain cloud give three different numbers at the same RMS);
- `totalVariation` per unit RMS is a brightness / spectral-centroid proxy, and is the metric
  `dsp/CLAUDE.md` names as the sharp, shape-tracking one.

**Checkpoints stay excluded, for a corrected reason.** Not "because they are amplitude-weighted"
(normalisation fixes that) but because at a 44 100 Hz sustain window the 32 evenly spaced raw samples
(`render_fingerprint.h:83-86`) are dominated by *instantaneous phase*: two renders of genuinely
near-identical presets that differ only by a few cents of drift produce near-uncorrelated checkpoint
vectors and a distance near its maximum. Folding that into a `max` would swamp the three shape terms and
make the arm pass unconditionally — the same failure mode, arrived at from the other side. They remain
available in the **reported** output for diagnosis, never in the gate.

**Floor, and how it is validated.**

```
kDistinctnessFloorAbsolute = 0.02        // 2 % relative on the strictest shape aggregate
kDistinctnessFloor         = max(kDistinctnessFloorAbsolute, 0.5 × observedMinimum)
```

`0.02` replaces the earlier `1e-3`, which was indefensible: a 0.1 % relative difference (≈ 0.009 dB on a
level metric) is not distinctness by any listening standard, and the only argument for it was "100× the
reproducibility tolerance" — a statement about *toolchain noise*, not about *audible difference*.
`observedMinimum` over all 861 pairs is still measured on the first green run and recorded with its
provenance (C-10's measure-then-pin rule), and if it lands below `0.02` the two presets involved are
**re-authored**, never accommodated.

**The floor is validated against an injected failure, not only against the observed minimum.** A
`[.measure]`-tagged negative control ships in the same TU:

1. Clone a shipped preset's definition; change **only** `kMasterGainId` (ID 0, normalized → linear
   `2·u`, `global_params.h:91-96`) from its value to the one `3 dB` below it (`u' = u / √2`).
2. Render the twin through the identical sweep path and **pre-check that the render is a pure scaling**:
   the twin's raw (pre-normalisation) `peak` must equal `0.7079 ×` the original's within 1 %. Master gain
   is applied **pre-limiter** (`processor.cpp:2392-2404`, "a post-limiter multiply is FORBIDDEN" at
   `:2397`), so if
   the limiter was engaged at the original level the twin is *not* a pure scaling and the control is
   invalid — in that case pick a preset whose peak has headroom, do not weaken the check.
3. **Require `d(original, twin) < kDistinctnessFloor`.** This is the injected near-duplicate: a
   level-only twin MUST be reported as non-distinct. The raw-fingerprint metric scored it large and
   passed; the normalised metric scores it ≈ 0. Record the measured `d` in the compliance document
   alongside `observedMinimum`, so the floor sits between a demonstrated non-distinct pair and the
   closest real pair — a two-sided justification rather than a one-sided one.

Recorded as **OI-4 (§9)** for owner ratification, since the spec left the metric open and this changes
both the metric and the floor.

Cost: the 861 pairs are computed from the **42 sustain-window buffers the FR-024 render already
produced** — the only added work is one `1/rms` scale plus one `fingerprintRender` pass per preset (the
normalised fingerprint is a *second* fingerprint, kept beside the raw one SC-009 reports), then `O(n²)`
over 42 small structs. The negative control adds exactly one extra sustain-window render.

### 2.7 FR-029 semantic comparison, and FR-029a's measurement procedure

The test regenerates each preset's `Comp` chunk **in process** (same drive sequence, §1.3.1, same shared
definitions header), then compares against the committed file:

1. **File set** — the set of `(category, stem)` pairs on disk equals the set the definition table
   produces, in both directions.
2. **`Info` XML** — `std::string` equality against `buildSeraphisInfoXml(stem, category)` (pure ASCII,
   toolchain-invariant).
3. **Version + length** — `version == 3` (`kCurrentStateVersion`, `plugin_ids.h:27`) and
   `comp.size() == 2868`.
4. **Integer / enum / bool fields** — exact equality, via `DecodedPresetState` on both sides.
5. **Float fields** — two tolerance classes, never one number:
   - *scalar* class: every `float` field of the nine packs (Aether decay is the canonical
     single-`std::pow` case, `aether_params.h:111-116` through
     `logMapFromNormalized = clamp(mn * pow(mx/mn, u), mn, mx)`,
     `plugins/shared/src/ui/parameter_helpers.h:80-83`);
   - *payload* class: for each of the four `SpectralState`s — `numPartials` **exact**,
     `isValidSpectralState` (`dsp/include/krate/dsp/processors/spectral_state.h:82`) true on both sides,
     and `ratios[i]`, `amplitudes[i]` (`i < numPartials`), `tiltDbPerOct`, `inharmonicity` within the
     payload tolerance. These are `makeFactoryState` outputs, whose banner records ~200
     `std::pow`/`std::exp` calls and an `1.0f / std::sqrt(sumSquares)` normalisation
     (`spectral_state.h:164-170`), memcpy'd as raw bit patterns (`:238-260`).

**Field coverage tripwire.** Each pack's comparator increments a counter and the test asserts the
counter equals the pack's documented field count (`[global]` 3, `[macro]` 5, `[seed]` 1, `[cloud]` 11,
`[morph]` 9 scalars, `[life]` 10, `[body]` 13, `[atmos]` 17, `[aether]` 18, `[effects]` 16 — each equal
to that block's byte size / 4). Combined with §2.1's cumulative-offset tripwires, a field added to a
pack in a later phase fails **both** the offset check and the count check rather than silently escaping
comparison.

**FR-029a measurement (must run before the tolerances are written down):**

1. Build `seraphis_preset_generator` and `seraphis_tests` under WSL/GCC 13 (the project's standing probe
   route). Run the FR-029 case in **report mode** — a `[.measure]`-tagged Catch2 case in the same TU
   that computes the same per-field relative errors and `WARN`s the worst per class instead of
   asserting.
2. Repeat once on the real macOS and Linux CI legs (one dry run) so Apple Clang `-ffast-math` is in the
   sample.
3. Pin `kScalarFieldTolerance = 10 × worst-observed-scalar` and
   `kPayloadFieldTolerance = 10 × worst-observed-payload`, as two separate constants with the measured
   numbers and their provenance in the compliance document. Neither may be written before step 1–2 have
   produced a number, and they may not be equal by accident — if the measurement says they are, the
   compliance document says so explicitly.

Expected magnitudes (a sanity band, **not** a substitute for measurement): a single `std::pow` differs
by ≲ 2 ULP across UCRT/glibc/libm ⇒ relative error ~1e-7, so the scalar tolerance should land near 1e-6;
the payloads compound ~200 transcendentals plus an L2 normalisation ⇒ relative error plausibly 1e-6…1e-5,
so the payload tolerance should land near 1e-4. If a measurement comes back orders of magnitude outside
that band, the right response is to investigate, not to widen the tolerance.

### 2.8 Leg tagging (C-9 / FR-027)

Two arms run on Windows only: SC-012's 60 s Aether-freeze band and SC-026's second reproducibility
render. Mechanism:

```cpp
TEST_CASE("Seraphis_PresetSweep_FrozenPresetsHold", "[seraphis][preset][sweep]") {
    …
#if defined(_WIN32)
    // C-9 / Q8 Option C: the 60 s Aether-freeze observation is one of the two heaviest arms.
    …assert the ±1.0 dB band…
#else
    SUCCEED("SC-012 Aether-freeze arm is Windows-leg only (spec C-9); Atmos/FX arm ran above");
#endif
}
```

The guard is **inside** the `TEST_CASE`, never around it, so the Catch2 `test cases:` count is identical
on all three legs and SC-021's count comparison stays meaningful.

---

## 3. The 42-preset library

### 3.1 Categories and names (FR-002, FR-004, FR-005)

Seven directories under `plugins/seraphis/resources/presets/`, six presets each. All names ASCII, unique
library-wide, free of `/\:*?"<>|` — the exact set `PresetManager::isValidPresetName` rejects
(`preset_manager.cpp:491-501`, read this session). **This is an authoring rule *and* a gate:** it is
asserted by `Seraphis_FactoryPresets_NamesAreValidAndUnique` (§4.1), which calls the shipped
`PresetManager::isValidPresetName` (`plugins/shared/src/preset/preset_manager.h:120`) rather than
re-deriving the rejected set, and checks uniqueness over the **whole library** rather than per category.

| Textures | Pads | Drones | Bells | Choirs | Motion | Cinematic |
|---|---|---|---|---|---|---|
| Vellum | First Light | Deep Well | Frost Bell | Vowel Field | Orbit Study | Approach Vector |
| Sea Glass | Long Exhale | Stone Circle | Temple Rim | Breath Chorus | Tide Pool | Event Horizon |
| Slow Snow | Cathedral Moss | Tectonic | Glass Carillon | Ghost Choir | Wander Lamp | Signal Lost |
| Paper Sky | Warm Static | Iron Lung | Struck Ice | Aeolian Voices | Restless | Rising Dread |
| Rust Bloom | Distant Choir | Continuum | Bronze Halo | Whispered Mass | Spiral Arms | Vast |
| Quiet Machine | Blue Hour | Undertow | Bell Garden | Angelic Drift | Slow Weather | Aftermath |

`Textures/.gitkeep` is removed in the same change that adds the six Textures presets (spec Edge Cases:
optional, and never to be confused with removing the directory).

### 3.2 Coverage assignment (FR-007 / SC-013)

Every row of C-2's matrix is pinned to a **named** preset so authoring is not a search:

| Enumeration value | Source table | Assigned preset(s) |
|---|---|---|
| Body material Glass (800→0) | `dropdown_mappings.h:198-200` | Sea Glass |
| Strings (1) | | Continuum |
| Metal Plate (2) | | Bronze Halo |
| Chamber (3) | | Cathedral Moss |
| Ice (4) | | Struck Ice |
| Spectral state Sine Stack (409–412 → 0) | `:178-180` | First Light |
| Bell (1) | | Frost Bell |
| Choir (2) | | Vowel Field |
| Glass (3) | | Glass Carillon |
| Breath (4) | | Breath Chorus |
| Morph state count 2 / 3 / 4 (408) | `:165-166` | Vellum / Tide Pool / Slow Weather |
| Travel mode External / Spline (403) | `:118` | Orbit Study / Wander Lamp |
| Envelope mode Standard / Growth (700) | `:190-191` | most presets / Approach Vector, Rising Dread, First Light |
| Grain envelope ≥ 3 of 6 (1016) | `:211-213` | Hann (default, many) · Trapezoid → Quiet Machine · Blackman → Slow Snow · Exponential → Aftermath |
| `kAetherFreezeId` (1204) ON / OFF | `plugin_ids.h:169` | Event Horizon, Vast / all others |
| `kAtmosFreezeId` (1008) ON / OFF | `:154` | Ghost Choir / all others |
| `kFxSpectralFreezeId` (1430) ON / OFF | `:196` | Signal Lost / all others |
| `kFxDelaySyncId` (1418) ON / OFF | `:194` | Restless / all others |
| `kBodyResonatorBypassId` (812) ON / OFF | `:143` | Warm Static / all others |
| `kBodyInputAgcId` (811) ON / OFF | `:142` | Iron Lung (OFF) / all others (ON) |

The harness computes this ledger from the **decoded** states, not from the definition table, and prints
the missing value on failure (SC-013).

### 3.3 Authoring constraints that are easy to violate

1. **FR-008 / C-5:** `kPolyphonyId` normalized must decode to ≤ 8. The denormalizer is
   `clamp((int)(value*15 + 1 + 0.5), 1, 16)` (`global_params.h:97-102`), so polyphony *p* ⇒
   `normalized = (p − 1) / 15`. Eight voices ⇒ `7/15 = 0.4666666666666667` (also the registered default,
   `:141-148`, index 7). `kSoftLimitId` stays ON — its registered default is 1.0 (`:152-154`), so simply
   **not touching ID 2** satisfies FR-008's second half.
2. **FR-008a is a live trap for Growth presets.** `A ≤ 12.0 s` with
   `A = growthDurationSec + stage1Ms/1000` in Growth mode, and the *defaults* are
   `kEnvGrowthDurationDefault = 10.0` s + `kEnvStage1MsDefault = 4000.0` ms = **14 s** — over the
   ceiling (`life_mod_params.h:65-67`). Every Growth-mode preset therefore **must** set 701 and/or 703
   explicitly (e.g. growth 8 s + stage1 3 s = 11 s). Standard-mode defaults are 2 s + 4 s = 6 s and are
   safe. `Rel` default is 8 s (`:68`), inside the 10 s ceiling.
3. **Normalized values for log-mapped IDs** are computed, never guessed:
   `normalized = ln(v / min) / ln(max / min)` — the exact inverse of
   `logMapFromNormalized = clamp(mn * pow(mx/mn, u), mn, mx)` (`parameter_helpers.h:80-83`). Ranges:
   growth duration `[1, 60]` s (`GrowthEnvelope::kMinDuration/kMaxDuration`, `growth_envelope.h:96, :98`);
   stage/release times `[1, 10000]` ms (`kEnvStageTimeMinMs`, `MultiStageEnvelope::kMaxStageTimeMs`,
   `life_mod_params.h:56-58`); Aether decay `[0.5, 60]` s (`aether_params.h:45-46`).
   Authoring is nevertheless **verified against the decoder**, not against this formula — SC-014a is the
   gate, and a preset that misses it is re-authored, never measured with a capped value (FR-008a).
4. **Seed spread.** `kSeedId` is an *index* into `kSeedValues` with index 0 pinned to `1u`
   (`dropdown_mappings.h:93-99`, `global_params.h:53-59`). Presets that intend audibly different motion
   must not all sit on index 0 — this is also the cheapest lever if FR-027b's distinctness floor is
   tight for a near-identical pair.
5. **Tail-arm classification is chosen, not discovered.** A preset's tail arm follows only from its three
   freeze toggles (C-6.3), so the author decides which arm a preset will be measured against by setting
   1008 / 1204 / 1430 — and the coverage matrix already requires each ON in ≥ 1 preset.

### 3.4 Authoring workflow

1. Write the definition entry (name, category, `{id, normalized}` list).
2. `cmake --build … --target generate_seraphis_presets` — writes into the committed tree.
3. `build/…/bin/Release/seraphis_tests.exe "Seraphis_FactoryPresets_*"` — fast static gates
   (container, length, coverage, budget, timing ceiling) in ~seconds.
4. `… "Seraphis_PresetSweep_*"` for the render gates once the static ones are green.
5. Audition (FR-034a / SC-029) and record the outcome per preset.

---

## 4. Test plan

Both TUs are added to the **enumerated** list in `plugins/seraphis/tests/CMakeLists.txt` (the file's own
rule, `:16-18`, `:29-31`, `:37-44`), and `integration/preset_render_sweep_test.cpp` is additionally added
to the `-fno-fast-math -fno-finite-math-only` block (`:117-170`) — it does bit-pattern non-finite checks
and per-second RMS statistics, which is exactly the stated criterion for that block.
`unit/preset/factory_preset_test.cpp` also goes in that block: FR-009 checks stored floats for
non-finiteness by bit pattern.

### 4.1 `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`

| Criterion | `TEST_CASE` | Assertion strategy |
|---|---|---|
| SC-001 (FR-002) + **FR-001 all three clauses** | `Seraphis_FactoryPresets_CategoriesMatchConfig` | (i) Directory names under `resources/presets` collected into a `std::set`; symmetric difference against `makeSeraphisPresetConfig().subcategoryNames` must be size 0; no `.vstpreset` at any other depth (`recursive_directory_iterator`, matching how `PresetManager::scanDirectory` walks, `preset_manager.cpp:58-69`). (ii) **FR-001 clause a+b — the literal, ordered list**: `subcategoryNames` compared **element-wise, in order** against the hard-coded seven-element vector `{"Textures","Pads","Drones","Bells","Choirs","Motion","Cinematic"}`, with `subcategoryNames[0] == "Textures"` asserted separately so the byte-exact-spelling clause names itself on failure. A `std::set` comparison is order-blind by construction and cannot carry this. (iii) **FR-001 clause c — the three unchanged fields**: `pluginName == "Seraphis"`, `pluginCategoryDesc == "Synth"`, `processorUID == Seraphis::kProcessorUID`. (iv) **The §1.2 header's own MUST**: `std::equal` between `Seraphis::PresetDefs::kCategories` and `subcategoryNames`, pinning the generator's directory source to the runtime's browser source — today that MUST is a comment with no check. |
| SC-002 (FR-004) | `Seraphis_FactoryPresets_CountAndDistribution` | total == 42; each of the seven counts ≥ 5; also == 6 (the C-2 target) reported, gated at ≥ 5. |
| SC-003 (FR-019) | `Seraphis_FactoryPresets_ContainerIsValid` | `parseVstPreset`: magic `"VST3"`, class id == `FUID::toString(kProcessorUID)` (32 upper-hex, `funknown.h:295`), `List` with **both** `Comp` and `Info`, every `offset+size` inside the file. 42/42. |
| SC-004 (FR-006) | `Seraphis_FactoryPresets_StreamIsCurrentVersion` | first `int32` == 3; `comp.size() == 2868` exactly. |
| SC-005 (FR-020) | `Seraphis_FactoryPresets_RoundTripByteIdentical` | fresh `ProcessorFixture`, `prepare(44100, 512)`, `setState(MemoryStream(comp))` == `kResultOk`, `getState()` into a second stream, `std::memcmp` == 0 over 2868 bytes. (Legitimate byte comparison — a *serialized state stream* of stored values, per `dsp/CLAUDE.md`'s explicit carve-out, not a float render digest.) |
| SC-006 (FR-021) | `Seraphis_FactoryPresets_InfoMetadataMatchesDirectory` | `parseInfoAttributes` must yield exactly the six ids; `MediaType=="VstPreset"`, `PlugInName=="Seraphis"`, `PlugInCategory=="Synth"`, `Name==stem`, `MusicalCategory==MusicalInstrument==` parent dir; parent dir ∈ `subcategoryNames`. |
| SC-007 (FR-022) | `Seraphis_FactoryPresets_BrowserScanFilesEveryPreset` | `PresetManager(makeSeraphisPresetConfig(), nullptr, nullptr, tempUserDir, resources/presets)` — the 4th and 5th ctor params (`preset_manager.h:55-61`). `tempUserDir` is a fresh `mkdtemp`-style directory the case creates and removes; it is load-bearing because `scanPresets()` scans the **user** dir first (`preset_manager.cpp:41-49`). Assert 42 entries, 0 empty `subcategory`, 0 with `isFactory == false`, and per-category `getPresetsForSubcategory(c).size()` == on-disk count for all seven. |
| SC-008 (FR-023) | `Seraphis_PresetBrowser_TabsMatchConfig` | Rebuild the tab vector exactly as `Controller::togglePresetBrowser` does (`controller.cpp:463-470`: `"All"` then `config.subcategoryNames` inserted in order) and compare element-wise against the **literal** eight-element vector `{"All","Textures","Pads","Drones","Bells","Choirs","Motion","Cinematic"}`. **Not** against `{"All"} ∪ subcategoryNames` — both sides of that comparison read the same config, so it is a tautology that passes for any wrong or reordered list. The literal is the second, independent copy that makes the assertion able to fail. |
| SC-013 (FR-007) | `Seraphis_FactoryPresets_CoversShippedSurface` | Decode all 42 (§2.1); build the coverage ledger; every C-2 row must be hit; failure message names the missing value and its ID. |
| SC-014 (FR-008) | `Seraphis_FactoryPresets_RespectVoiceBudget` | `global.polyphony <= 8` and `global.softLimit == true` for 42/42. |
| SC-014a (FR-008a) | `Seraphis_FactoryPresets_RespectTimingCeiling` | `makeTimeline(...).A <= 12.0` and `.Rel <= 10.0` for 42/42; message prints the decoded `envMode`, growth/stage/release values. |
| **FR-005** (no SC in the spec — see OI-6) | `Seraphis_FactoryPresets_NamesAreValidAndUnique` | Over `allPresetFiles()`: (a) `PresetManager::isValidPresetName(stem)` true for 42/42 (`plugins/shared/src/preset/preset_manager.h:120`, a `static bool` that exists precisely to be called; the rejected set is `/\:*?"<>\|` plus empty/oversize, `preset_manager.cpp:491-501`); (b) the **stem set has size 42** — uniqueness across the WHOLE library, not per-category, because the browser's flat list and its search collide on duplicates regardless of directory (`preset_manager.cpp:123-147`); (c) every code unit of every stem `< 0x80` (ASCII, checked on `unsigned char`, never on a possibly-signed `char`); (d) `stem == ` the `Name` Info attribute — the same equality SC-006 asserts from the metadata side, cross-linked here so a rename that touches only one side fails in both places. |
| FR-006a, FR-009 | `Seraphis_FactoryPresets_PartialsBlockIsInert` | Both bitmasks == 0 and all 64 pans exactly `0.0f` and within `[-1, 1]`. FR-009's finiteness clause is a **typed enumeration, never a 4-byte walk of the stream** — see the note directly below this table for why a byte walk is wrong. |
| SC-017 (FR-029, FR-029a) | `Seraphis_FactoryPresets_TreeMatchesGenerator` | The five-clause semantic comparison of §2.7, all three legs. Companion `[.measure]`-tagged case `Seraphis_FactoryPresets_TreeToleranceProbe` reports the per-class worst error for FR-029a's pinning run and is excluded from the default run by its `.` tag. |

#### 4.1.1 FR-009's finiteness clause — why it is a typed enumeration

FR-009 says "no preset may store a non-finite float **anywhere** in its stream". A literal 4-byte walk of
the 2868-byte stream is **not implementable and is wrong for 2164 of those bytes**:

- §2.1's decode produces **typed structs** (`GlobalParams`, `CloudParams`, …) plus
  `std::array<SpectralState,4>`; its tripwires are cumulative **block** offsets, not per-field float
  offsets. There is no list of float offsets for a walker to consume.
- The four 541-byte payloads are **not a float grid**. Verified this session at
  `dsp/include/krate/dsp/processors/spectral_state.h:192-205`: offset 0 is a `uint8` version, offset 1 an
  `int32 numPartials`, offsets 5 and 9 are the two scalars, 13 and 269 the two 64-float arrays, and
  **525-540 is a 16-byte `name` character array**. A 4-byte walk would reinterpret the version byte, the
  partial count and the ASCII name as floats and could report a "non-finite" that is a letter.

**What the case actually does** — the same shape as §2.7's field-coverage tripwire, so the two cannot
drift apart:

1. For each of the **nine decoded packs**, run a `bufferIsFinite`-style bit-pattern check
   (`Krate::DSP::detail::isNaN` / `isInf`, never `std::isnan`) over **every named `float` member**, and
   increment the same per-pack field counter §2.7 defines. The counters must equal the documented counts
   (`[global]` 3, `[macro]` 5, `[seed]` 1, `[cloud]` 11, `[morph]` 9 scalars, `[life]` 10, `[body]` 13,
   `[atmos]` 17, `[aether]` 18, `[effects]` 16), so a `float` added to a pack in a later phase and not
   added here fails the count, not silently escapes the check.
2. For each of the **four decoded `SpectralState`s**: `ratios[i]` and `amplitudes[i]` for
   `i < numPartials` — entries at `i >= numPartials` are explicitly scratch space the morph engine owns
   (`spectral_state.h:78-79`) and are **not** examined — plus `tiltDbPerOct` and `inharmonicity`. These
   are the floats actually at risk: `makeFactoryState` outputs of ~200 `std::pow`/`std::exp` calls and a
   `1.0f/std::sqrt` normalisation (`spectral_state.h:164-170`).
3. For the `[partials]` block: all 64 pans, finite by bit pattern and within `[-1, 1]`.

**Explicitly excluded, because they are not floats:** `numPartials` (`int`), the payload version byte
(`uint8`), the 16-byte `name` array (`char`), the leading `int32` state version, and the two `uint64`
bitmasks.

**Relationship to `isValidSpectralState`.** `isValidSpectralState`
(`dsp/include/krate/dsp/processors/spectral_state.h:82`) already rejects non-finite `ratios[i]` and
`amplitudes[i]` over `i < numPartials`, and §2.7 clause 5 already requires it true on both sides. This
case **duplicates** that coverage deliberately rather than relying on it: `isValidSpectralState` is a
composite predicate that also enforces ranges and monotonicity, so a failure there does not tell the
reader *which* clause fired, and FR-009 is specifically a finiteness requirement that must name its own
failure. The duplication is ~8 lines and the two must not be collapsed.

### 4.2 `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp`

One shared, file-local render helper produces each preset's buffers once per (preset, rate) and the arms
read them — the sweep must not render the same timeline twice.

| Criterion | `TEST_CASE` | Assertion strategy |
|---|---|---|
| SC-009 (FR-024) | `Seraphis_PresetSweep_NoSilence` | §2.4, at **both** 44 100 and 48 000 Hz (48 kHz renders to `H + 5 s`, C-9). Stimulus: `Seraphis::PresetDefs::findDef(category, stem)`'s `AuditionStimulus` override when present, else pitch 60 / velocity 0.8f — with a **`REQUIRE(def != nullptr)` for 42/42 before any render** (§1.6), so an unmatched preset fails loudly instead of silently defaulting; one NoteOn at t=0 through `Krate::Test::EventList` (`fx.pushEvent(kNoteOnEvent, pitch, vel, 0)`, `seraphis_test_fixture.h:275-298`), NoteOff at `t = H` (`pushEvent(kNoteOffEvent, …)` inside `renderBlocks`'s per-block script, `:388-410`). |
| SC-010 (FR-025) | `Seraphis_PresetSweep_BoundedAndFinite` | §2.3 over the **whole** `[0, Total]`, hold included, both rates. Also asserts `fx.checkCanaries()` (`:371-374`) so an out-of-bounds write is caught in the same pass. |
| SC-010a (FR-024a) | `Seraphis_PresetSweep_ChordBoundedAndFinite` | 44 100 Hz only. Four simultaneous NoteOns at t=0: root = stimulus pitch, plus `+4`, `+7`, `+12`. Render to `H`; **only** the bounded arm. |
| SC-011 (FR-026 case 3) | `Seraphis_PresetSweep_DecayMatchesRt60` | §2.5(3), 44 100 Hz only, presets with all three freeze toggles OFF. |
| SC-012 (FR-026 cases 1–2) | `Seraphis_PresetSweep_FrozenPresetsHold` | §2.5(1) inside `#if defined(_WIN32)` (§2.8); §2.5(2) on all legs. |
| SC-015 (FR-028) | `Seraphis_PresetSweep_NoAudioThreadAllocation` | See §4.2.1 — the recipe has **three** preconditions, and the capture-vector clear is as load-bearing as the warm-up. |
| SC-015a (FR-028a) | `Seraphis_PresetSweep_ConcurrentLoadIsRtSafe` | See §4.2.2 — the audio thread runs a **capture-free** loop; `renderBlocks` must not appear in it. Asserts (a) finite + peak ≤ `kPeakBound` throughout, (b) 42/42 `setState` == `kResultOk` and the post-join `getState()` `memcmp`-equals one of the 42 chunks, (c) `ThreadScopedAllocationScope` count == 0. |
| SC-026 (FR-027a) | `Seraphis_PresetSweep_RendersAreReproducible` | §2.6, Windows leg only via the in-case guard. |
| SC-028 (FR-027b) | `Seraphis_PresetSweep_PresetsAreDistinct` | All 861 pairs of the SC-009 sustain-window buffers, each **normalised to unit RMS before fingerprinting** (§2.6); `d(P,Q) > kDistinctnessFloor` over `{peak, meanAbs, totalVariation}`; failure prints the closest pair and its distance. Companion `[.measure]`-tagged `Seraphis_PresetSweep_DistinctnessNegativeControl` renders the level-only twin and requires `d < kDistinctnessFloor`, so the floor is justified from **both** sides. |
| SC-027 | (no case) | Catch2's own reported duration for the TU, recorded in the compliance document; budget ≤ 6 min on `windows-x64-release`. |

#### 4.2.1 SC-015's render path — the capture vectors grow unless cleared

`ProcessorFixture::renderBlocks` **appends** (`capturedL.push_back(...)`,
`plugins/seraphis/tests/seraphis_test_fixture.h:403-406`, read this session) and re-requests capacity as
`reserveCapture(capturedL.size() + numBlocks * blockSize)` (`:391`). It **never clears**. So the plan's
earlier recipe — one warm-up render, then per-preset scopes — allocates *inside* every measured scope:
after the warm-up `capturedL.size() == N·blockSize`, so the first measured render asks for
`2·N·blockSize` and reallocates both vectors, and across 42 presets the requirement grows monotonically
so every subsequent preset reallocates too. The fixture's own banner warns about exactly this
(`:15-19`: "every container here grows on demand and is then REUSED — `clear()`/`reset()` keep
capacity"). The arm would then report a false audio-thread allocation — the mirror image of the
misattribution FR-028 exists to avoid.

**The recipe, complete and in order:**

```
fx.prepare(44100.0, 512);                       // once
fx.reserveCapture(N * 512);                     // ONE render's worth - the clear keeps this capacity
fx.renderBlocks(N, 512);                        // warm-up: grows both vectors to N*512 and warms
                                                // every processor-internal container too
for (each of the 42 comp chunks) {
    fx.proc->setState(...);                     // OUTSIDE the scope
    fx.capturedL.clear();  fx.capturedR.clear();// size -> 0, CAPACITY RETAINED. Without this the
                                                // next reserveCapture() request grows past capacity
                                                // and reallocates inside the scope.
    {
        TestHelpers::AllocationScope scope;     // opens here
        fx.renderBlocks(N, 512);                // requests reserveCapture(0 + N*512) -> no-op
    }                                           // closes here
    REQUIRE(scope.getAllocationCount() == 0);   // AFTER the close - Catch2 allocates
}
```

The alternative — `reserveCapture((1 + 42) * N * 512)` up front and never clearing — is correct but
holds ~43× the audio in RAM for no benefit. The clear is the specified form.

#### 4.2.2 SC-015a's audio thread — capture-free, and bounded

The concurrent arm must **not** call `renderBlocks`. A continuous loop through it appends `blockSize`
samples per block forever (`:403-406`) and re-requests capacity every call (`:391`), so the capture
vectors reallocate repeatedly **on the audio thread, inside the very scope that must report zero**, and
the arm's memory footprint is unbounded. Specified instead:

```
// --- audio thread, in this order, BEFORE the scope opens ---
TestHelpers::tAllocationTrackThisThread = true;   // first touch of the TLS flag (§1.7)
enableFTZDAZ();                                   // R-9: MXCSR is FRESH per thread on Windows
fx.processBlock(512);                             // one warm-up block, capture-free
                                                  // (seraphis_test_fixture.h:343-352 - it touches
                                                  //  neither capturedL nor capturedR)
{
    TestHelpers::ThreadScopedAllocationScope scope;
    for (std::size_t b = 0; b < kConcurrentBlocks && !stop.load(); ++b) {
        fx.processBlock(512);
        // finiteness + peak scanned PER BLOCK over fx.audioL()/fx.audioR() (:356-357),
        // accumulated into plain counters. Nothing grows.
    }
}
REQUIRE(scope.getAllocationCount() == 0);         // after the close
```

`kConcurrentBlocks` is **bounded and stated**: `8600` blocks ≈ 100 s of audio at 44 100 Hz, which gives
the message thread's 42-`setState` loop ample overlap while keeping the arm's wall clock and its (zero,
by construction) memory growth defined. The loop also exits early on `stop` once the message thread has
finished all 42 loads, so the arm normally costs far less than its bound. No Catch2 macro executes inside
the scope, on either thread.

#### 4.2.3 Cost: MEASURED FIRST, not assumed (SC-027, C-9, R-6)

**Rendered-audio volume** (arithmetic, and the part that is sound): under FR-008a's ceiling the worst
`Total` is 89 s (Aether-freeze), 49 s (Atmos/FX-freeze) and 37.5 s (no freeze); adding the 48 kHz render
(≤ 22 s), the chord render (≤ 17 s) and the Windows-only 2 × `[0, H]` reproducibility renders (≤ 34 s)
puts a *typical* preset near 110 s of rendered audio, **≈ 4 600 s across 42**.

**The real-time factor is NOT assumed.** The plan's earlier "RTF ≈ 0.05, therefore ≈ 4 minutes" is
withdrawn. Its justification — "the sweep holds one voice for every arm but the chord one, so per-block
cost is far under the gated 24.21 %-at-8-voices figure" — does not follow: most of Seraphis's per-block
cost is **per-block, not per-voice** (Aether's 1024-sample diffusion FFT, Atmosphere, spectral freeze,
saturation, the true-peak limiter). The in-repo measurement says so directly: a settled whole-block
Seraphis render is **1 267 675 – 1 530 620 ns/block** (`plugins/seraphis/tests/integration/param_perf_test.cpp:92-93`,
arm 3 undivided/subdivided, read this session) against the **10 666 666.7 ns** block budget the same file
derives (`:384-385`) — the file itself calls this "a ~13 %-of-core chain" (`:102`). That is **RTF ≈ 0.13,
~2.6× the withdrawn assumption**, and 4 600 s at 0.13 is **≈ 10 minutes against a 360 s budget** (which
would require RTF < 0.078). The estimate is *not* "inside the budget with little margin"; on the only
numbers this repo actually has, it **overruns by ~1.7×**.

On top of that, `ProcessorFixture::prepare()` enables `setEffectsStageInstrumentedForTest(true)`
(`seraphis_test_fixture.h:210`) — a per-slice `steady_clock::now()` plus a per-slice full-bus copy,
which the fixture's own banner says makes "the test build pay MORE than the shipping build, never less"
(`:198-209`). The 4 600 s figure charges nothing for it. §1.3.1 turns it off in the *generator*; the
sweep keeps it on (it is a test), so this is a term the volume arithmetic omits, not a lever.

**Procedure — this replaces the sanity check as a gate on §8's ordering.** At execution-order step 7,
**before authoring at volume is treated as safe and before the arm/leg matrix is fixed**:

1. Render **one representative preset** (a no-freeze one and an Aether-freeze one) end to end through
   the real sweep path and record **seconds of audio rendered per second of wall clock** on
   `windows-x64-release`. Put the number and its provenance in the compliance document.
2. Multiply out against the 4 600 s volume. If the projection exceeds 360 s, apply levers from the
   pre-declared list below **in order**, re-measure, and record which were used.

**Pre-declared levers (C-9 / R-6), in the order they may be applied.** The phase may use these without
re-opening the spec; anything beyond them requires the phase owner:

| # | Lever | Cost saved | What it does NOT do |
|---|---|---|---|
| L-1 | Shorten `W` for the Atmos/FX-freeze case from 20 s (the original single lever) | ≈ 10 s × the Atmos/FX-only presets — a few percent | Does not touch the 60 s Aether band |
| L-2 | Compute the per-second RMS **streaming** (accumulate per block, never retain the full render) | Removes the 4 600 s × 2 ch × 4 B ≈ 37 MB peak and its cache traffic; also removes the capture-vector growth from every non-fingerprint arm | Changes no threshold and no window |
| L-3 | Run the **tail arms at 44 100 Hz only** and drop the 48 kHz *tail* render, keeping the 48 kHz sustain + bounded arms | ≈ 22 s → ≈ 5 s per preset | FR-024/FR-025's dual-rate coverage is retained; only the tail statistics become single-rate |
| L-4 | Restrict the **48 kHz render to a subset** — every preset that is the sole holder of a C-2 coverage row, plus ≥ 1 per category | Roughly halves the 48 kHz volume | Every coverage row is still rendered at both rates |
| L-5 | Move the **60 s Aether-freeze band** and the reproducibility renders to a `[.slow]`-tagged case run in the nightly lane, with SC-012/SC-026 evidence taken from that lane | Removes the two heaviest arms from the PR-time 6 min | Requires recording in the compliance document that these two criteria are nightly-gated, not PR-gated |

**Never sanctioned:** dropping presets, dropping the chord arm, dropping either rate entirely, or
relaxing any threshold. SC-027's **measured** figure governs; nothing in this section is evidence.

### 4.3 Non-test gates

- **SC-016:** `node tools/check-preset-generator-determinism.js` exit 0, run in the FR-035 gate beside
  `check-portability.js`.
- **SC-018:** generator builds with 0 warnings on MSVC and on WSL/GCC; emits 42 files.
- **SC-019:** after a Release build, list `%PROGRAMDATA%\Krate Audio\Seraphis` — seven directories,
  42 files — and record the listing. The copy is the **existing** POST_BUILD
  `krate_plugin_install_presets(${PLUGIN_NAME})` at `plugins/seraphis/CMakeLists.txt:113`, which resolves
  to `copy_directory resources/presets → $ENV{PROGRAMDATA}/Krate Audio/Seraphis`
  (`cmake/KratePlugin.cmake:288-311`, Windows-only guard at `:291-293`). No new install rule, no
  `SRC_SUBDIR`/`DEST_SUBDIR` — that form exists only for Membrum's nested `Kits` layout. Destination must
  match `Platform::getFactoryPresetDirectory("Seraphis")`
  (`plugins/shared/src/platform/preset_paths.h:23-27`: Windows `%PROGRAMDATA%\Krate Audio\{pluginName}`).
- **FR-018 — BOTH clauses, and clause 2 previously had no step.** FR-018 is a verification obligation, so
  it needs a verification *action*, not just an entry saying nothing changed.
  - *Clause 1:* confirm `plugins/seraphis/installers/windows/setup.iss:66-68` remains the sole preset
    install path (no second copy rule added anywhere), and record the line verbatim.
  - *Clause 2:* read `plugins/seraphis/installers/linux/README.txt` and the **Linux branch of
    `plugins/shared/src/platform/preset_paths.cpp`**, and record **both destination strings verbatim with
    their `file:line`** in the compliance document. The header's doc comment
    (`preset_paths.h:23-27`) is *not* sufficient evidence — it says `/usr/share/krate-audio/{pluginName}`
    while the implementation is `fs::path("/usr/share/krate-audio/" + lowerName)`
    (`preset_paths.cpp:49`), i.e. **lower-cased**. Today the README's system-wide destination is
    `/usr/share/krate-audio/seraphis` and the user destination is `~/Documents/Krate Audio/Seraphis`
    (`preset_paths.cpp:20`, `:27`), so both match — but that is a fact to be *recorded from the files*,
    not assumed, and a future `pluginName`-cased edit to either side breaks it silently.
- **SC-020:** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Seraphis.vst3"`.
- **SC-021:** `seraphis_tests` `test cases:` count recorded **before** the phase and after; the delta must
  equal the number of new cases (a TU missing from the enumerated list exits 0 silently).
- **SC-022:** `node tools/check-portability.js` clean; `./tools/run-clang-tidy.ps1 -Target seraphis`
  0 warnings; 0 compiler warnings.
- **SC-023:** `node tools/check-changelog-coverage.js seraphis` exit 0 — `'seraphis'` is already in that
  script's `PLUGINS` array (`tools/check-changelog-coverage.js:50`, verified), so this is a green run,
  not a code change.
- **SC-024:** both roster files read back after the edit and the flow run once.
- **SC-025 / SC-029:** compliance-document entries (Phase 11.5's measured figure with provenance; the
  42-row listening table). A missing entry ⇒ verdict `DEFERRED`.

---

## 5. Release-roster and metadata changes

### 5.1 `.claude/workflows/release-readiness.js` (FR-031)

`PLUGIN_MAP` currently ends at `membrum` (`:14-21`, verified this session) and unknown names are filtered
out at `:26-29`, so a `{plugins:["seraphis"]}` run silently checks nothing. One line:

```js
  seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' },
```

### 5.2 `.claude/skills/release/SKILL.md` (FR-032)

Two edits: the **Plugin** input list at `:15` (today ends `…, membrum`) gains `seraphis`, and the
**Plugin → target/bundle map** table at `:22-29` gains the row
`| seraphis | \`seraphis_tests\` | \`Seraphis.vst3\` |`.

### 5.3 Version and changelog (FR-033)

`plugins/seraphis/version.json` `"version": "0.4.0"` → `"0.5.0"` (verified current value this session).
**Only** the `version` field; `src/version.h` is generated by
`krate_plugin_configure_generated_files()` (`plugins/seraphis/CMakeLists.txt:11`) and is never hand-edited.
A matching `## [0.5.0] - 2026-xx-xx` section lands in `plugins/seraphis/CHANGELOG.md` in the **same**
change. `1.0.0` stays reserved for the Phase 13 release.

### 5.4 `plugins/seraphis/CLAUDE.md` (FR-034)

The existing *"Decisions that outlive Phase 8 → 2. Preset categories are additive-only"* section is
updated from "Phase 8 seeds exactly one category" to the shipped seven-name table, keeping the
rename-orphans-user-presets warning and the "carried in two places" rule verbatim. Same treatment as the
parameter-band table at `plugins/seraphis/CLAUDE.md:22-32`.

---

## 6. Build integration summary

| File | Change |
|---|---|
| root `CMakeLists.txt` | `seraphis_preset_generator` executable + `generate_seraphis_presets` custom target (§1.4) |
| `plugins/seraphis/tests/CMakeLists.txt` | two TUs into the **enumerated** source list; both into the `-fno-fast-math -fno-finite-math-only` block (`:117-170`) |
| `tests/test_helpers/allocation_detector.h` | additive default-off thread filter + `ThreadScopedAllocationScope` (§1.7) |
| `.github/workflows/*.yml` | **no change** — `release.yml`'s `*` case already constructs the target name (`:158-166`); `ci.yml` already runs `seraphis_tests` on all three legs (`:271`, `:322`, `:525`, `:582`, `:947`, `:995`, verified) |
| `plugins/seraphis/CMakeLists.txt` | **no change** — `krate_plugin_install_presets` already called at `:113` |
| `plugins/seraphis/installers/windows/setup.iss` | **no change** — `presets\*` line already ships to `{commonappdata}\Krate Audio\Seraphis` (FR-018 clause 1; the verification step is §4.3's FR-018 bullet) |
| `plugins/seraphis/installers/linux/README.txt` | **no change expected** — but FR-018 clause 2 requires its two documented destinations to be read and recorded against `preset_paths.cpp`'s Linux branch (`:20`, `:27`, `:49`); if they diverge, the README is what changes, not the header (§4.3, §8 step 10) |

Targets to build and run:

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_preset_generator
"$CMAKE" --build build/windows-x64-release --config Release --target generate_seraphis_presets
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
node tools/check-preset-generator-determinism.js
node tools/check-portability.js
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja
```

`tools/lint-layers.js` and the SIMD-alignment lint have nothing to police — no new class enters `dsp/`.
`tools/lint-float-bit-goldens.js` and `tools/lint-midi-timing-goldens.js` must stay green: the only byte
comparisons this phase introduces are over **serialized state streams** and generator **file bytes**,
never over rendered float samples.

---

## 7. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| R-1 | **Byte-equality creep** — a later contributor "simplifies" FR-029 into a `memcmp` of the `Comp` chunks | The committed tree is MSVC-generated, the release artifact GCC-generated (`release.yml:100-102`, `:158-174`), and ~14 fields plus four payloads are `std::pow`/`std::exp`-derived bit patterns | C-8's reasoning is restated in the test's banner at the comparison site; `ci.yml:162-166`'s bit-exact-float lint is a standing gate |
| R-2 | **Generator/test drift in the drive sequence** | Two copies of "prepare → fan out → getState" would let FR-029 compare two different pipelines and pass | Both use `SeraphisTest::ProcessorFixture` (§1.3.1) — one implementation |
| R-3 | **Decode-order drift** vs `getState()` | FR-025a's decoder is a third copy of the block order (after `getState` and `Controller::setComponentState`) | Cumulative per-block offset asserts + the mandatory 2868 total tripwire (§2.1) + per-pack field-count counters (§2.7) |
| R-4 | **The tautological SC-012 arm 2** ships and proves nothing | `final ≤ loudest + 1 dB` cannot fail | §2.5(2)'s repair: compare against the **first** window; reported clause (b) promoted after measurement. Flagged as OI-2 |
| R-5 | **Growth presets breach FR-008a silently** | The *defaults* (10 s growth + 4 s stage1 = 14 s) already exceed the 12 s ceiling | SC-014a gates every preset; §3.3(2) calls it out; a breach is re-authored, never capped |
| R-6 | **Sweep exceeds SC-027's 6-minute budget** | **Likely, not hypothetical**: the only in-repo RTF measurement is ≈ 0.13 (`param_perf_test.cpp:92-93` vs `:384-385`), which projects ≈ 10 min for the 4 600 s volume — a ~1.7× overrun. The withdrawn RTF-0.05 assumption made this look like a margin question | §4.2.3: measure the real RTF on one representative preset at §8 step 7 **before** the matrix is fixed, then apply the pre-declared lever list L-1…L-5 in order and record which were used. The old "only sanctioned lever" (L-1) is a few percent and cannot close a 1.7× gap on its own |
| R-7 | **`AllocationScope` misuse** — three ways, all of which report a *false* audio-thread allocation | (a) Catch2 allocates on assertion, so `REQUIRE` inside the scope self-inflicts a failure; (b) `renderBlocks` **appends and never clears** (`seraphis_test_fixture.h:391`, `:403-406`), so an uncleared capture vector reallocates inside every measured scope; (c) a continuous capture-ful loop on the FR-028a audio thread reallocates repeatedly *and* is unbounded in memory | Three preconditions, equally load-bearing, all specified: scope closes before any assertion; `capturedL/R.clear()` immediately before every scope opens (§4.2.1); the concurrent arm uses the capture-free `processBlock` loop with a stated block bound (§4.2.2) |
| R-8 | **Thread filter breaks the other six plugins** | `allocation_detector.h` is shared | `threadFilter_` defaults **false**; the added `thread_local bool` is allocation-free on first touch because this header is only ever linked into test **executables** (static TLS — §1.7 corrects the earlier "constant initialiser" reasoning, which was the wrong mechanism), with a header guard note forbidding use from a dynamically loaded module; a full run of every plugin's suite is part of the gate |
| R-14 | **The generator ships a release artifact produced with test-only instrumentation on** | `ProcessorFixture::prepare()` sets `setEffectsStageInstrumentedForTest(true)` (`seraphis_test_fixture.h:210`), and `seraphis_preset_generator` is a release-pipeline tool (`release.yml:168-174`); a "verbatim" fixture reuse would silently enable it | §1.3.1 disables it as the line after `prepare()`, on **both** the generator and FR-029's in-process regeneration, so the two sides of the comparison run one configuration (OI-5) |
| R-9 | **Denormals in a 60 s frozen tail** dragging the sweep's wall clock | Long frozen tails at low level | `enableFTZDAZ()` already runs in `unit/test_main.cpp:27`; the sweep inherits it on the main thread and must call it on the FR-028a audio thread too (MXCSR is **fresh** per thread on Windows and **inherited** on glibc — the recorded divergence) |
| R-10 | **`-ffast-math` on the macOS leg** defeating the non-finite checks | The bounded arm is the phase's main safety net | Both new TUs are in the `-fno-fast-math -fno-finite-math-only` block, and every finiteness test goes through `Krate::DSP::detail::isNaN`/`isInf` bit-pattern helpers, never `std::isnan` |
| R-11 | **A category directory added to disk but not to the config** (the Membrum failure) | `parsePresetFile` leaves `subcategory` empty on a miss (`preset_manager.cpp:95-103`) | SC-001 and SC-007 catch it from opposite directions; SC-008 catches the tab-list half |
| R-12 | **MSVC-green ≠ portable** for a brand-new tool | The generator is a *new* target that only CI's Linux leg builds today via `release.yml` | `node tools/check-portability.js` before commit **and** a WSL/GCC build of `seraphis_preset_generator` (SC-018), which is also FR-029a's measurement vehicle |
| R-13 | **`recursive_directory_iterator` ordering** making the generator or a test non-deterministic | Filesystem order is unspecified | The generator iterates the **definition table**; the tests sort `allPresetFiles()` before use |

---

## 8. Execution order (for `/speckit.tasks`)

1. **Config + directories** — FR-001, seven directories, `.gitkeep` removal. Gates: SC-001 shell-level.
2. **Definitions header + generator + CMake** — FR-010…FR-016a. Gate: **links** (not merely compiles —
   `moduleHandle`, §1.3.3) with 0 warnings on MSVC *and* WSL/GCC, and emits files for a first small
   subset (2–3 presets) so the pipeline is proven before authoring at volume.
3. **Test-support header + static TU** — FR-019…FR-023, FR-006a, FR-009, and the coverage/budget/timing
   ledgers (which must exist before authoring, so authoring is verified as it goes).
4. **Author the 42 presets** in category order, regenerating and running the static gates after each
   category (§3.4).
5. **Determinism script** — FR-035a / SC-016.
6. **Allocation-detector thread filter** — §1.7, plus a full run of all seven plugin suites to prove the
   additive change is inert.
7. **Render-sweep TU** — FR-024…FR-028a. First without the pinned floors, printing measurements.
   **Step 7a, before the arm/leg matrix is fixed:** render one no-freeze and one Aether-freeze preset end
   to end and record the **measured** seconds-of-audio-per-second-of-wall-clock (§4.2.3). Project against
   the 4 600 s volume; if the projection exceeds 360 s, apply levers L-1…L-5 in order, re-measure, and
   record which were used. Do not discover this at step 11.
8. **Measure-then-pin** — FR-029a's two tolerances (WSL/GCC probe + one CI dry run), and FR-027b's
   distinctness floor **together with its injected near-duplicate control** (§2.6: the level-only twin
   must score below the floor). Record every number — `observedMinimum`, the twin's `d`, the two
   FR-029a tolerances — and its provenance in the compliance document.
9. **FR-029 committed-tree comparison** switched from report mode to gating.
10. **Roster, version, changelog, CLAUDE.md, and FR-018's two documentation checks** — FR-031…FR-034,
    plus §4.3's FR-018 bullet: record `setup.iss:66-68` verbatim (clause 1) and record the Linux
    README's two destination strings against `preset_paths.cpp`'s Linux branch (clause 2), each with its
    `file:line`.
11. **Release gate** — FR-035: build → `seraphis_tests` → pluginval 5 → version/changelog sync →
    portability → clang-tidy → determinism script. Verdict recorded `DEFERRED` until Phase 11.5's three
    exit criteria are cited as met (FR-030 / SC-025).

Steps 1–9 are unblocked by Phase 11.5; only step 11's **verdict** is gated.

---

## 9. Open items for the phase owner

- **OI-1 — `buildSeraphisInfoXml` placement.** The plan puts it in the shared definitions header so
  FR-029 clause 2's byte comparison has a single source. FR-016a says that header is "data only"; the
  `Info` chunk is not state layout and not component-stream serialization, so the plan reads this as
  compliant. If the phase owner reads FR-016a strictly, the fallback (file-local writer + a second
  template in the test, drift caught loudly by the comparison itself) is in §1.2.
- **OI-2 — SC-012 arm 2 is tautological as written.** `final ≤ loudest + 1.0 dB` cannot fail. §2.5(2)
  repairs it by comparing the final window against the **first** window at the same 1.0 dB allowance,
  with a stricter "no intermediate growth" clause reported first and promoted to gating after
  measurement. No threshold is relaxed, but what the criterion measures changes, so the owner should
  ratify it.
- **OI-3 — FR-025a's payload hand-skip.** The spec instructs the decoder to hand-skip the four 541-byte
  payloads. `loadMorphParams`'s third parameter is the payload destination (`morph_params.h:400-402`), so
  skipping them would mean *not* calling the shipped loader as written and would leave FR-029 clause 5
  needing a separate `deserializeSpectralState` pass. The plan calls the shipped loader and lets it fill
  the payloads (§2.1). Only `[partials]` is read by hand. Functionally identical, strictly less code, one
  fewer copy of a layout — confirm.
- **OI-4 — FR-027b's distance metric and floor (CHANGED).** The spec says "pairwise fingerprint
  distance" without defining it. The plan's first draft used a relative L∞ over
  `{rms, peak, meanAbs, totalVariation}` of the raw fingerprint with a `1e-3` floor; that is
  **withdrawn** — three of those four terms are pure amplitude aggregates
  (`render_fingerprint.h:63-88`), so two timbrally identical presets differing only in master gain
  scored large and passed, which is exactly the failure C-10 exists to catch, and `1e-3` (≈ 0.009 dB on
  a level metric) certified near-duplicates as distinct. §2.6 now **normalises each sustain-window
  buffer to unit RMS before fingerprinting**, drops `rms` (identically 1 afterwards), keeps
  `{peak, meanAbs, totalVariation}` as shape statistics, raises the absolute floor to **0.02**, and
  validates it from below with an **injected near-duplicate** (a level-only twin that MUST score below
  the floor). Confirm the metric, the 0.02 floor and the negative control before the floor is pinned.
- **OI-5 — the test-only instrumentation gate in a release tool.** `ProcessorFixture::prepare()` calls
  `setEffectsStageInstrumentedForTest(true)` (`seraphis_test_fixture.h:210`), which
  `processor.h:1466-1470` documents as FALSE on every shipping path. §1.3.1 disables it immediately
  after `prepare()` in the generator (a release-pipeline tool) **and** in FR-029's in-process
  regeneration, so both sides of the comparison run one configuration. Confirm that the generator should
  drive the shipping configuration rather than the test one; the alternative (record that the committed
  tree was produced instrumented, and accept the coupling) is stated in §1.3.1.
- **OI-6 — FR-005 has no SC in the spec.** FR-005 (names unique library-wide, `isValidPresetName`-valid,
  ASCII, no path separators) had no test row and no success criterion, so nothing downstream would have
  caught a duplicate — which collides in the browser's flat list and its search
  (`preset_manager.cpp:123-147`). §4.1 now carries
  `Seraphis_FactoryPresets_NamesAreValidAndUnique` as an FR-keyed row, following the precedent of the
  existing FR-006a/FR-009 row. **The spec should gain a matching SC** so the criterion survives into the
  compliance table rather than living only in the plan; that edit is outside this document and is flagged
  for the phase owner.

---

## 10. Review notes

Every issue from the plan review was **accepted**. Two resolutions deviate in detail from the reviewer's
suggested wording; both are recorded here so the deviation is visible rather than silent.

1. **§2.6 — normalised checkpoints are still excluded from the gate.** The review's option (a) suggested
   keeping "normalized checkpoints in the max" alongside `totalVariation`. The plan normalises the buffer
   (accepting the substance of the finding) but keeps the checkpoints **out of the gating max**, for a
   different and corrected reason: `render_fingerprint.h:83-86` samples 32 evenly spaced *raw samples*,
   and over a 3-second sustain window at 44 100 Hz those are dominated by instantaneous phase. Two
   presets that differ only by a few cents of drift would produce near-uncorrelated checkpoint vectors
   and a distance near maximum, which inside a `max()` swamps the three shape terms and makes the arm
   pass unconditionally — the same "cannot fail" failure the review identified, reached from the other
   side. The checkpoints are still computed and **reported** for diagnosis. If the phase owner wants them
   gated, the correct form is a separate, separately-thresholded term (not a `max` member), and that
   threshold would itself need the same injected-near-duplicate validation — flagged under OI-4.
2. **§4.1 — FR-005's success criterion lives in the plan until the spec is amended.** The review asked
   for the test row *and* a matching SC in the spec. The test row is added and keyed to FR-005 directly
   (the same shape as the pre-existing FR-006a/FR-009 row). Adding an SC to `spec.md` is outside this
   document's edit scope, so it is raised as **OI-6** for the phase owner rather than being applied here.
   Until it lands, FR-005's compliance-table row cites `Seraphis_FactoryPresets_NamesAreValidAndUnique`.

No threshold was relaxed to resolve any issue. Two were **tightened** as a direct consequence of the
review: FR-027b's distinctness floor moved from `1e-3` to `0.02` (§2.6), and SC-011's spurious
digital-silence guard was removed from the no-freeze arm so a correctly-decayed preset passes (§2.5).
