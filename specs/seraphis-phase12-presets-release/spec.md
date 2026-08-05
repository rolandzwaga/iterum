# Feature Specification: Seraphis Phase 12 — Factory Presets & Release Readiness

**Spec slug:** `seraphis-phase12-presets-release`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, **Phase 12** (roadmap lines 602–610), plus roadmap
line 405–406 (the Phase 8 clause that *defers the preset-category set to this phase*), roadmap
lines 588–590 and 653–655 (the **Phase 11.5 hard gate** ahead of this phase), and roadmap line 313–326
(the 8-voice / 25 % budget a shipped preset must not silently exceed).
**Depends on:** Phase 8 (`plugins/seraphis/`, ✅), Phase 9 (107-parameter surface, ✅), Phase 10
(effects + state v3, ✅), Phase 11 (editor + `[partials]` state block, ✅), **Phase 11.5 (whole-`process()`
≤ 25 %, IN PROGRESS — a blocking precondition, see *Hard Precondition* below).**
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-08-04

---

## Overview

Every Seraphis release so far has shipped an **empty preset library**: the only tracked file under
`plugins/seraphis/resources/presets/` is `Textures/.gitkeep`
(`git ls-files plugins/seraphis/resources/presets` → one entry), and
`makeSeraphisPresetConfig()` declares exactly one subcategory,
`{"Textures"}` (`plugins/seraphis/src/preset/seraphis_preset_config.h:29`) — which that file's own banner
labels *"a SEED, not a placeholder"* and hands to this phase to extend
(`seraphis_preset_config.h:8-13`). The plumbing around that emptiness is already complete and was verified
this session: the browser is wired (`controller.cpp:156-157`, `:464-474`), the Windows install
POST_BUILD copy is wired (`plugins/seraphis/CMakeLists.txt:113` → `cmake/KratePlugin.cmake:287-311`), the
Inno installer already ships `presets\*` to `{commonappdata}\Krate Audio\Seraphis`
(`plugins/seraphis/installers/windows/setup.iss:66-68`), and the Linux README already documents both
preset destinations. **What is missing is the content, the tool that produces it, the harness that proves
it, and two release-roster entries.**

Three findings from this session are what make this more than "write some presets":

1. **`.github/workflows/release.yml` already requires a target that does not exist.** Its
   `generate-presets` job maps every plugin except Iterum to `${PLUGIN}_preset_generator` via a `*`
   case (`release.yml:158-166`), builds it with
   `cmake --build build --target seraphis_preset_generator` (`:168-169`), runs
   `./build/bin/seraphis_preset_generator generated-presets` (`:170-174`), and uploads that directory as
   the `factory-presets` artifact with `if-no-files-found: error` (`:175-180`). The Windows packaging job
   then copies that artifact — **not** `resources/presets` — into the installer
   (`:201-224`). A Seraphis release run today fails at the build step. The generator's **target name,
   binary location, argv contract and Linux/GCC buildability are therefore fixed constraints**, not
   design choices.

2. **The state is 2868 bytes and four of its blocks are not plain scalars.** `Processor::getState()`
   writes the version int32 + `[global] 12` + `[macro] 20` + `[seed] 4` + `[cloud] 44` + `[morph] 52`
   scalars + **four 541-byte `SpectralState` payloads (2164 B)** + `[life] 40` + `[body] 52` +
   `[atmos] 68` + `[aether] 72` + `[effects] 64` + `[partials] 272`
   (`plugins/seraphis/src/processor/processor.cpp:1836-1917`, layout banner at `:1836-1843`). The payload
   block goes through `Krate::DSP::serializeSpectralState`
   (`dsp/include/krate/dsp/processors/spectral_state.h:238`, `kSpectralStateBytes == 541` at `:185-186`),
   and `[partials]` is 64 floats + two `writeInt64u` masks (`processor.cpp:420-457`). The Ruinae-style
   answer — a hand-maintained duplicate layout in `tools/<plugin>_preset_format.h` plus a
   `preset_format_compat_test` to catch the drift (`tools/ruinae_preset_format.h:1-13`,
   `plugins/ruinae/tests/unit/preset_format_compat_test.cpp:1-10`) — would mean re-implementing
   `serializeSpectralState` in a tool. **This spec does not duplicate the layout at all** (C-3): the
   generator links the shipped `Seraphis::Processor` and calls the shipped `getState()`, so drift is
   impossible by construction and no compat test is needed.

3. **"No runaway" cannot mean "decays to silence" for this instrument.** Membrum's infinite-ring pattern
   asserts the final block falls below −80 dBFS
   (`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:54-57`). Seraphis ships
   `kAetherFreezeId`, `kAtmosFreezeId` and `kFxSpectralFreezeId` as first-class playing techniques
   (`plugin_ids.h:1204`, `:1008`, `:1430`) and an Aether decay reaching 60 s
   (`aether_params.h:45-46`), and roadmap line 273 makes energy-conserving freeze a *feature*. A
   decay-to-silence sweep would either fail on a correct preset or force the library to omit the
   instrument's signature behaviour. The sweep therefore runs **one render on one explicit timeline**
   (NoteOn, an un-released hold, then a NoteOff and a tail) and asserts a **bounded** arm over the whole
   render plus a **tail** arm chosen from the preset's own freeze toggles — conserve / hold / decay-at-its-
   own-RT60 (C-6). A finite Aether decay is a *rate*, not a sustain, and is checked against the rate it
   declares.

Everything else in this phase is assembly against components read this session, plus the release gate the
roadmap names.

---

## Clarifications

### Session 2026-08-04

- **Q1 — Harness decode mechanism for typed preset field values.** Decided: instantiate the shipped
  per-block params structs in the test and call the shipped processor-side
  `load*Params(Params&, IBStreamer&)` free functions in `getState()` order (real denormalized values, no
  arithmetic re-derivation); hand-skip the four `SpectralState` payloads and the `[partials]` block;
  mandatory tripwire asserts exactly 2868 bytes consumed. A shared decode header touching
  `Controller::setComponentState` is deferred, not adopted this phase. [FR-025a, FR-026, FR-029]
- **Q2 — Audition stimulus for the render sweep.** Decided: fixed default pitch 60 / velocity 0.8 for every
  preset, with an optional per-preset override in the FR-016a definitions table for outlier presets, plus
  one additional 4-note-chord bounded-arm-only render per preset at 44 100 Hz, so `polyphony ≤ 8` and the
  multi-voice limiter sum are actually rendered without paying chord cost on the 60 s tail arms.
  [FR-016a, FR-024, FR-024a, SC-010a]
- **Q3 — Float tolerance for the committed-tree comparison (FR-029).** Decided: measure first (WSL/GCC
  probe plus one CI dry run), then pin each tolerance at ~10× its own worst observed error, recorded in
  the compliance document; split by class — single-`pow` scalars get their own tight tolerance,
  `SpectralState` payloads are compared structurally (`numPartials` exact, `isValidSpectralState` true,
  ratios/amplitudes within their own separately measured, looser tolerance) — never one asserted-without-
  measurement number covering both classes. [FR-029, FR-029a, SC-017]
- **Q4 — Does any factory preset exercise the `[partials]` override block (OQ-4)?** Decided: **no**. Every
  preset's two bitmasks are all-zero and all 64 pans are 0.0f, asserted by the harness; the generator has
  no `IMessage`/`HostApplication`/`notify()` drive surface. OQ-4 closed. [FR-006a, FR-016, FR-016a]
- **Q5 — Category names and library size (OQ-1 + OQ-2).** Decided: adopt the proposal as-is — Textures /
  Pads / Drones / Bells / Choirs / Motion / Cinematic, 42 presets = 7 × 6, ≥ 5 per category floor, C-2's
  coverage matrix. If SC-027's budget is exceeded, Q8's levers are the response — never cutting presets.
  OQ-1 and OQ-2 closed. [C-1, C-2, FR-001, FR-004, FR-007]
- **Q6 — Is there a criterion that the 42 presets are distinct and fit their category?** Decided: add a
  mechanical distinctness criterion — pairwise `RenderFingerprint` distance over the sustain window
  exceeds a measured floor for all 861 preset pairs (never bit-exact) — plus a human listening checkpoint
  for category fit, recorded in the compliance document, with no automated spectral-heuristic arm.
  [C-10, FR-027b, FR-034a, SC-028, SC-029]
- **Q7 — Release version (OQ-3), and does this phase execute the release gate?** Decided: bump to **0.5.0**
  in this phase (version.json + CHANGELOG land here); the release verdict is recorded explicitly as
  `DEFERRED` until Phase 11.5's exit criteria are met (FR-030/SC-025 unchanged in substance). `1.0.0` is
  reserved for the release that includes Phase 13 (per-note expression). OQ-3 closed.
  [FR-030, FR-033, SC-025]
- **Q8 — What bounds the sweep's wall-clock budget, given the timeline is derived from preset state?**
  Decided: (1) an asserted authoring constraint — every factory preset's attack `A ≤ 12 s` and release
  `Rel ≤ 10 s`, bounding the sweep by bounding what ships, with every arm still measuring the real preset;
  (2) the two heaviest arms — the 60 s Aether-freeze conservation arm and FR-027a/SC-026's second render —
  run on the Windows CI leg only; macOS/Linux run the sustain and bounded arms (plus the non-Aether tail
  arms). SC-027's budget is restated as a measured number in the compliance document.
  [FR-008a, SC-014a, C-9, SC-012, SC-026]

---

## Hard Precondition — Phase 11.5

Roadmap line 588–590: *"**Phase 12 MUST NOT ship before this phase is green.** Release readiness that
ships a 31.7 % instrument against a documented 25 % promise is not release readiness."* Roadmap
line 653–655 repeats it in the dependency graph.

`specs/seraphis-phase11-5-process-optimization/` does not exist yet on this branch (`ls specs` shows no
`11-5` directory) although Phase 11.5 work is landing —
`88fca556 perf(seraphis): SIMD gather kernel for the atmosphere grain-span sweep`,
`7666aa83`, `08a1b108` (`git log --oneline`). This spec therefore **states the precondition as a
requirement of its own release gate** (FR-030) rather than assuming it: no Phase 12 release-readiness
verdict may be reported green while Phase 11.5's three exit criteria (roadmap lines 592–600) are unmet,
and Phase 12's compliance document must cite Phase 11.5's measured numbers rather than restate its
promise. **Preset authoring and the validation harness (FR-001 … FR-029) are NOT blocked by 11.5** — only
the release gate is.

---

## Scope

In scope for this phase:

- The **factory preset library**: a fixed, named category set; the `.vstpreset` files; their embedded
  `Info` metadata; their coverage of the shipped surface.
- The **generator**: `tools/seraphis_preset_generator.cpp` and the `seraphis_preset_generator` /
  `generate_seraphis_presets` CMake targets, on the contract `release.yml` already assumes.
- The **validation harness**: container/UID validity, state round-trip, browser-scan agreement,
  metadata↔filesystem agreement, render reproducibility, and the all-presets render sweep (whose hold
  phase is the roadmap's NoteOn-only condition).
- **Release-roster registration**: `.claude/workflows/release-readiness.js` and
  `.claude/skills/release/SKILL.md`, both of which omit Seraphis today.
- The **release gate**: build → `seraphis_tests` → pluginval strictness 5 → `version.json` / `CHANGELOG.md`
  sync, gated behind Phase 11.5.

## Non-goals (owned elsewhere)

- **Per-note expression / MPE** — Phase 13 (roadmap lines 612–633). No preset may depend on it.
- **New parameters, new parameter types, new DSP, new UI.** The registered surface stays at **107 IDs**
  and every registered type stays frozen (`plugin_ids.h:203-208`). `kCurrentStateVersion` stays **3**
  (`plugin_ids.h:27`) — a factory preset is an ordinary current-version stream, not a new format.
- **Whole-`process()` optimization** — Phase 11.5. This phase measures nothing new about CPU; it only
  *refuses to declare release readiness* until 11.5 is green.
- **User preset authoring UX** (save/overwrite/delete/import) — already shipped in the shared
  `PresetManager` (`preset_manager.h:85-103`) and browser; this phase adds no UI.
- **Reading the `Info` chunk back at runtime.** `PresetManager::readMetadata` is a documented stub
  (`preset_manager.cpp`, *"TODO: Implement XML metadata reading"*, returns `true`), so the runtime derives
  subcategory from the directory (`:95-103`). Phase 12 **writes** correct metadata for external hosts and
  **tests** it, but does not implement the reader — that would be a shared-library change affecting six
  other plugins.
- **macOS/Linux factory-preset installers.** `krate_plugin_install_presets` is Windows-only by
  construction (`cmake/KratePlugin.cmake:290-292`) and the Linux path is documented manual copy
  (`installers/linux/README.txt`). Unchanged here.

---

## Decisions

### C-1 — The category set is fixed, additive-only, and includes `Textures` verbatim

Seraphis ships **seven** subcategories, in this order:

```
Textures · Pads · Drones · Bells · Choirs · Motion · Cinematic
```

`Textures` keeps its exact existing spelling and its existing directory
(`resources/presets/Textures/`), because renaming a shipped category orphans every user preset saved
against it — `PresetManager::parsePresetFile` matches `parentName` against `config_.subcategoryNames`
by exact string compare and leaves `subcategory` **empty** on a miss (`preset_manager.cpp:95-103`), and
`savePreset` writes into the subcategory directory by name (`:251-256`). That is the Membrum lesson, and
`seraphis_preset_config.h:8-13` already records it as binding on this phase. **Ratified verbatim as Q5 /
CQ-5 Option A (2026-08-04 Clarifications) — OQ-1 is closed.**

Names are single-word, ASCII, no `&`, no punctuation — they are simultaneously filesystem directory
names, `PresetManagerConfig::subcategoryNames` entries, browser tab labels
(`controller.cpp:462-469`) and `MusicalCategory` / `MusicalInstrument` XML attribute values
(`preset_manager.cpp:271-272`).

Eight tabs (`"All"` + seven) is inside proven range: the shared `CategoryTabBar` lays tabs out
**vertically** at `viewSize.getHeight() / numTabs` (`category_tab_bar.cpp:25-36`) down the browser's
left edge, and Ruinae already ships **fifteen** category directories through the same view
(`ls plugins/ruinae/resources/presets`).

### C-2 — 42 presets, ≥ 5 per category, with a coverage matrix

**42 presets** total, **6 per category**, minimum **5** in any category. Sized against the in-repo
precedents — the counts in the `generate_*_presets` `COMMENT` strings: **Disrumpo 120 / 11**
(root `CMakeLists.txt:520`), **Innexus 35 / 7** (`:554`), **Gradus 25 / 8** (`:571`),
**Membrum 20 kits / 4** (`:588`). Disrumpo is the outlier (a multi-band distortion with a far smaller
per-preset authoring cost) and sets the ceiling of the comparison set, not the target. **Ratified verbatim
as Q5 / CQ-5 Option A (2026-08-04 Clarifications) — OQ-2 is closed.**

The library is not free-form: it must **exercise the shipped surface**, so that a defect in any shipped
enumeration is reachable from a factory preset. Across the 42 presets, each of the following must appear
in **at least one** preset (FR-007):

| Surface | Values that must each appear | Source of the enumeration |
|---|---|---|
| Body material (ID 800) | Glass, Strings, Metal Plate, Chamber, Ice (all 5) | `dropdown_mappings.h:198-200` |
| Factory spectral state (IDs 409–412) | Sine Stack, Bell, Choir, Glass, Breath (all 5) | `dropdown_mappings.h:178-180` |
| Morph state count (ID 408) | 2, 3 and 4 | `dropdown_mappings.h:165-166` |
| Travel mode (ID 403) | both values | `dropdown_mappings.h:118` |
| Envelope mode (ID 700) | Standard **and** Growth | `dropdown_mappings.h:190-191` |
| Grain envelope (ID 1016) | ≥ 3 of the 6 | `dropdown_mappings.h:211-213` |
| Freeze toggles (1008 / 1204 / 1430) | each ON in ≥ 1 preset, each OFF in ≥ 1 preset | `plugin_ids.h:1008`, `:1204`, `:1430` |
| Delay sync (1418) | ON and OFF | `plugin_ids.h:1418` |
| Body resonator bypass (812) / input AGC (811) | each ON and OFF | `plugin_ids.h:811-812` |

### C-3 — The generator reuses the shipped serializers; there is **no** `seraphis_preset_format.h`

The generator links `plugins/seraphis/src/processor/processor.cpp` and produces every preset's
component chunk by calling the shipped `Processor::getState(IBStream*)`
(`processor.cpp:1856`). Consequences, all deliberate:

- The four 541-byte payloads go through the shipped `serializeSpectralState`
  (`spectral_state.h:238`) via `saveSpectralPayloads` (`morph_params.h:380-391`) — not a reimplementation.
- The `[partials]` block goes through the shipped `savePartialOverrides` (`processor.cpp:450-457`).
- **No `tools/seraphis_preset_format.h` is created and no `preset_format_compat_test` is written.** Both
  exist for Ruinae *only because* its generator duplicates the layout
  (`tools/ruinae_preset_format.h:6-11`). With one serializer there is nothing to drift.
- The `.vstpreset` **class id** is derived at runtime from `Seraphis::kProcessorUID` via
  `FUID::toString(char8*)` (`extern/vst3sdk/pluginterfaces/base/funknown.h:295`, documented as the
  32-char uppercase hex form), never a hardcoded literal like Ruinae's
  (`tools/ruinae_preset_generator.cpp:28`).

### C-4 — Parameter values reach the state the same way a host delivers them

`Processor::processParameterChanges` is **private** (`processor.h:745`, `:761`) and is called from
`process()`. The generator therefore drives each preset exactly as the shipped test fixture does —
`initialize(nullptr)` → `setupProcessing` → `setActive(true)` → one `process()` call carrying every
parameter point → `getState()` (`plugins/seraphis/tests/seraphis_test_fixture.h:177-212`,
`:225`). This means the values a preset stores are, by construction, the values the shipped
denormalizers produce (e.g. `handleGlobalParamChange`'s polyphony
`clamp(value*15+1+0.5, 1, 16)`, `global_params.h:97-102`), and a preset can never encode a value the
plugin cannot itself reach.

**Preset definitions are authored in normalized 0–1 units**, one entry per touched `ParamID`. Untouched
IDs keep their registered defaults, which `getState()` writes unchanged.

### C-5 — Factory presets pin polyphony ≤ 8 and leave the soft limit ON

`kPolyphonyId`'s registered range is 1…16 and stays so (roadmap lines 322–326 forbid narrowing it), but
the **budgeted** operating point is 8 voices — roadmap lines 313–321: 24.21 % of one core at 8 voices vs
**47.36 % at 16**, the latter explicitly non-gating. Shipping a factory preset at polyphony 16 would ship
a patch that doubles the documented budget with no warning. Factory presets therefore store
`polyphony ≤ 8`, and `kSoftLimitId` ON (its default, `global_params.h:52`) — a factory preset is not the
place to hand the user a hotter output stage.

### C-6 — One render per preset per rate, on one explicit timeline, with three tail arms

#### C-6.1 The timeline (there is exactly one, and it carries a NoteOff)

An earlier draft of this decision said "NoteOn, **no NoteOff**" in one requirement and "after a NoteOff
appended at the end of the sustain window" in the next. Those cannot both describe one render, and the
contradiction silently decided what the decay arm measures. It is resolved here, once, as a **single
timeline per preset per sample rate**, with every offset derived from the preset's **own decoded state**:

| Symbol | Definition | Provenance |
|---|---|---|
| `A` | **attack span**, s = (envelope mode == Growth ? growth duration : stage0 ms/1000) + stage1 ms/1000, decoded from the loaded preset | `kEnvGrowthDurationDefault = 10.0`, `kEnvStage0MsDefault = 2000.0`, `kEnvStage1MsDefault = 4000.0` (`life_mod_params.h:65-67`); stage ceiling `MultiStageEnvelope::kMaxStageTimeMs = 10000.0f` (`multi_stage_envelope.h:65`) |
| `Rel` | **release**, s = `kEnvReleaseMsId` (704) value / 1000 | `kEnvReleaseMsDefault = 8000.0` (`life_mod_params.h:68`) |
| `RT60` | stored Aether decay, s ∈ [0.5, 60.0] | `kAetherDecayMinSeconds` / `kAetherDecayMaxSeconds` (`aether_params.h:45-46`); it is a **true RT60** — `const float t60dc = decaySeconds;` drives the Jot gains (`dsp/include/krate/dsp/effects/aether_reverb.h:3139`) |
| `Sus` | **sustain-measurement window** = `[A + 1.0 s, A + 4.0 s]` (3 s of RMS) | chosen to clear the preset's own attack span, not a fixed guess |
| `H` | **NoteOff instant** = `A + 5.0 s` | the interval `[0, H]` is the roadmap's **NoteOn-only** hold |
| `Settle` | start of the tail-measurement window = `H + Rel + G`, with `G = 2.0 s` for frozen presets and `G = 0.5 s` for non-frozen ones | the explicit settling allowance the earlier draft lacked |
| `W` | tail-measurement span — **60.0 s** (Aether-freeze), **20.0 s** (Atmos/FX-freeze only), **10.0 s** (non-frozen) | see C-6.3 |
| `Total` | render length = `Settle + W` | |

`A`, `Rel`, `RT60`, `W` and therefore `Total` are **computed from the loaded state**, never hardcoded per
preset. The bounded arm (C-6.2) is asserted over the **whole** `[0, Total]` render, including the
NoteOn-only hold `[0, H]` — that hold *is* the stuck-note / runaway condition the roadmap's "NoteOn-only
render sweep" (line 608) and Membrum's infinite-ring pattern
(`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:54-57`) exist to catch. One
render, not two, keeps the sweep inside the wall-clock budget of C-9.

#### C-6.2 The bounded arm — every preset, no exception

Finite output (bit-pattern NaN/Inf check, never `std::isnan` — the `-ffast-math` rule) and peak ≤ the
limiter ceiling plus allowance, reusing the shipped constants `kLimiterCeilingLin = 0.8912509f` and
`kCeilingAllowanceDb = 0.1f` (`plugins/seraphis/tests/integration/effects_chain_test.cpp:332`, `:854`;
derived from `TruePeakLimiter::kDefaultCeilingDb = -1.0f`,
`dsp/include/krate/dsp/processors/true_peak_limiter.h:46`).

#### C-6.3 The tail arm — three cases, classified by the **freeze toggles only**

An earlier draft also classified a preset as "sustaining" when `RT60 ≥ 30 s` with no freeze, and then
demanded energy conservation of it. That is impossible by construction: a finite RT60 is a **decay rate**,
so a 30 s RT60 falls 2.0 dB/s and a 60 s RT60 falls 1.0 dB/s, i.e. 20–40 dB across a 20 s window against a
±1.0 dB band — and it also created a discontinuity at the threshold (29.9 s must fall ≥ 20 dB, 30.1 s must
stay flat, for physically identical behaviour). The decay clause is **dropped**. Classification is by the
three freeze toggles decoded from `[atmos]`, `[aether]` and `[effects]`, and the long-decay case gets its
own, defensible arm:

1. **Aether-freeze** — `kAetherFreezeId` (1204) ON. Only this mechanism is promised energy-conserving:
   roadmap line 272 (*"freeze at unity feedback, energy-conserving"*) and roadmap line 282's Phase 6
   criterion. **Arm:** the per-second RMS series over `[Settle, Settle + 60 s]` stays inside a ±1.0 dB
   band (max − min ≤ 2.0 dB). **Exactly one disclosed relaxation of roadmap line 282:** the band widens
   0.5 → 1.0 dB because Phase 12 measures the whole plugin (voices + effects + limiter), not
   `AetherReverb` alone. **The 60 s observation duration is NOT relaxed** — an earlier draft silently cut
   it to 20 s, which cannot distinguish a 60 s-RT60 tail (20 dB of fall over 60 s) from a conserving one.
2. **Atmos-/FX-freeze only** — `kAtmosFreezeId` (1008) and/or `kFxSpectralFreezeId` (1430) ON while
   `kAetherFreezeId` is OFF. **Neither mechanism is promised energy conservation anywhere in the
   roadmap**: Phase 5's criteria are allocation, click-freedom, a blur metric and CPU (roadmap
   lines 248-254) and Phase 10 states no criteria at all (lines 464-473). Demanding a ±1 dB band of them
   would fail a *correct* preset — with `kAetherDecayDefault = 4.0 s` (`aether_params.h:53`) the
   non-frozen reverb and voice components are still falling while the frozen layer holds. **Arm** — the
   weaker property this phase can actually defend: over `[Settle, Settle + 20 s]` the output is
   **bounded and non-growing**, i.e. the final-second RMS ≤ the loudest-second RMS **+ 1.0 dB**.
3. **No freeze** — the tail must **decay at a rate consistent with its own stored RT60**. Over
   `[Settle, Settle + 10 s]`, with `dropDb = 20·log10(RMS(first second) / RMS(final second))`:

   ```
   dropDb  ≥  min( 0.5 · 60 · W / RT60 , 20.0 )      with W = 10 s
   ```

   The `min(…, 20 dB)` cap preserves the ordinary case unchanged (any preset with RT60 ≤ 6 s must still
   fall ≥ 20 dB, the original threshold — **nothing is relaxed for short tails**), and the RT60 term gives
   the long-tail case a bound it can meet and that still catches a runaway: RT60 = 60 s must fall ≥ 5 dB,
   RT60 = 30 s ≥ 10 dB. The 0.5 factor is the tolerance on "consistent with", and the bound is one-sided —
   decaying *faster* than the reverb predicts (a dry-dominant preset) always passes.

Because the classification is now continuous in RT60, a preset anywhere in the legal 0.5…60 s decay range
with freeze off is testable and shippable. No arm asserts absolute silence: with an 8-voice organism, a
60 s Aether tail and a −80 dBFS floor, "silent" is not a property the instrument has.

### C-7 — Determinism without bit-exact goldens

`kSeedId` is part of the saved state (`global_params.h:53-59`, `saveGlobalSeed` at `:273`), so two runs
of the same preset are driven from the same seed table entry (`dropdown_mappings.h:93-99`). The sweep
asserts **aggregate metrics and their tolerances** through
`tests/test_helpers/render_fingerprint.h` (`RenderFingerprint{rms, peak, meanAbs, totalVariation,
checkpoints}` at `:54-60`, `kMetricTolerance = 1e-5` at `:52`, `kSampleTolerance = 1e-4f` at `:49`,
`FingerprintComparison::withinTolerance()` at `:95-97`) — never a float bit digest.

**This decision is enforced, not merely stated.** Its positive half is **FR-027a / SC-026** (two renders
of the same preset in one process agree under `compareFingerprints`); its negative half — no bit-exact
float golden, including any integer digest derived from float bits — is a clause of **FR-027a** and is
restated in *Edge Cases → Seed determinism*. An earlier draft left both halves in this decision alone,
with no FR and no SC; downstream stages consume the FR/SC lists, so that is exactly how a decision ships
unbuilt.

Generator determinism (**FR-014**, not FR-011 — FR-011 is the argv/output-directory contract) is a
different, stronger claim and is asserted on **file bytes**. That is legitimate **only** under SC-016's
premise: one binary, one machine, one process pair, never compared across toolchains. C-8 states why that
premise does *not* extend to the committed-tree comparison.

### C-8 — The committed tree is the artifact of record, and the invariant is **semantic**, not byte-level

`resources/presets/**` is committed and is what `krate_plugin_install_presets` copies to
`%PROGRAMDATA%\Krate Audio\Seraphis` on every local Windows build
(`cmake/KratePlugin.cmake:294-310`), while the release installer ships the **freshly generated**
`generated-presets/` artifact (`release.yml:170-224`).

An earlier draft said those two "must be the same bytes". **That premise is false, and asserting it would
have been a bit-exact float golden across toolchains — the thing roadmap line 664 and `ci.yml:162-166`
("Bit-exact float golden lint") forbid.** Three verified reasons:

1. The committed tree is generated on the developer's **Windows/MSVC** build; the release artifact is
   generated on **`ubuntu-latest` with GCC** (`release.yml:100-102`, `:158-174`) and *that* is what the
   Windows packaging job copies into the installer (`release.yml:201-224`). The two are never produced by
   the same toolchain.
2. ~14 stored fields are denormalized through `logMapFromNormalized`, whose body is
   `std::clamp(mn * std::pow(mx / mn, clamped), mn, mx)`
   (`plugins/shared/src/ui/parameter_helpers.h:80-83`) — used for Aether decay
   (`aether_params.h:111-116`), and for cloud/atmos/body/life-mod/morph rates. `std::pow` is not
   correctly-rounded and differs between UCRT, glibc and Apple libm.
3. Each preset embeds four 541-byte `SpectralState` payloads whose floats are `std::memcpy`'d **bit
   patterns** (`spectral_state.h:238-260`, layout at `:218-235`, `kSpectralStateBytes == 541` at
   `:184-186`) of `makeFactoryState()` results — a function whose own banner says it *"evaluates ~200
   `std::pow`/`std::exp` calls"* and which normalizes with `1.0f / std::sqrt(sumSquares)`
   (`spectral_state.h:164-170`). macOS additionally builds with `-ffast-math`.

**The real invariant, and what FR-029 asserts:** the committed tree and the release artifact are produced
by the **same generator source from the same preset definitions**, and are **semantically identical** —
same file set and relative paths, byte-identical `Info` XML (pure ASCII, toolchain-invariant), identical
state version and 2868-byte `Comp` length, integer/enum/bool fields equal exactly, and float fields equal
within a stated relative tolerance. **Per-toolchain ULP differences in the denormalized float fields and
the `SpectralState` payloads are explicitly ACCEPTED** as a decision of this phase; they are inaudible and
functionally inert, and no cheaper honest alternative exists short of rewriting `release.yml` to package
`resources/presets` (considered and rejected here: it would make the shipped library the *unbuilt* one and
silently drop the "the generator still runs on Linux" signal that `release.yml:168-174` provides).

Because the comparison is semantic, **FR-029's test runs on all three CI legs** and each leg proves *its
own* toolchain's generator output is the committed library — which is a strictly stronger statement about
the Linux-generated installer than a Windows-only byte check ever was. **FR-014** (determinism, byte-level,
same-machine) plus **FR-029** (semantic tree equality, all legs) is what closes the gap; the earlier draft
cited FR-011 and FR-012, neither of which says anything about it.

### C-9 — The sweep has a stated wall-clock budget, and the two heaviest arms run on one leg

42 presets × 2 rates × a `Total` that could otherwise reach ~147 s for an unconstrained Growth +
Aether-freeze preset (growth duration max 60 s, `growth_envelope.h:98`, stage1 up to 10 s,
`multi_stage_envelope.h:65`) is a real cost on three CI legs (`seraphis_tests` runs on Windows
`ci.yml:271`, `:322`, macOS `:525`, `:582` and Linux `:947`, `:995`). **Q8 / CQ-8 (2026-08-04
Clarifications, Option B + C)** resolves this with two decisions, not a hypothetical fallback:

1. **Cap the library, not the timeline (FR-008a).** Every factory preset MUST store `A ≤ 12.0 s` and
   `Rel ≤ 10.0 s`. Under that ceiling the worst-case `Total` (C-6.1) is: `H ≤ 17.0 s`; for a frozen preset
   `Settle ≤ 12 + 5 + 10 + 2 = 29.0 s` (`G = 2.0 s`), so an Aether-freeze `Total ≤ 89.0 s` and an
   Atmos/FX-freeze-only `Total ≤ 49.0 s`; for a non-frozen preset `Settle ≤ 12 + 5 + 10 + 0.5 = 27.5 s`
   (`G = 0.5 s`), so `Total ≤ 37.5 s`. The sweep still measures the **real, unmodified** preset — nothing
   about the render itself is truncated; the ceiling only bounds what ships (FR-008a).
2. **Tag the two heaviest arms to the Windows leg only.** The Aether-freeze 60 s conservation arm
   (FR-026/SC-012's first bullet) and FR-027a/SC-026's second (`[0, H]`) reproducibility render both run on
   **Windows only**; macOS and Linux run the sustain (SC-009), bounded (SC-010), chord-bounded
   (FR-024a/SC-010a), Atmos/FX-freeze (SC-012's second bullet) and no-freeze-decay (SC-011) arms, none of
   which reaches the 60 s window.

- **At 48 000 Hz the render stops at `H + 5 s`** (≤ 22.0 s under the FR-008a ceiling) — the sustain
  (SC-009) and bounded (SC-010) arms run at both rates, which is what FR-027's "stable only at the
  development rate" concern actually needs. The **tail arms (SC-011 / SC-012) run at 44 100 Hz only**, and
  the spec says so rather than letting the plan discover it.
- **Budget: the preset-render sweep TU completes in ≤ 6 minutes wall clock** on `windows-x64-release`,
  **measured and recorded, not estimated** (SC-027) — the figure the compliance document states is what
  governs. The estimate below is only the design-time sanity check that motivated the ceiling and the leg
  tagging above: the sweep holds **one** voice for every arm but FR-024a's chord render, so the gated
  24.21 %-at-8-voices figure (roadmap lines 313-321) is an upper bound on per-block cost by a wide margin;
  with the FR-008a ceiling and the leg-tagging above, total rendered audio per leg is well under half of
  the unconstrained ~1 900 s estimate an earlier draft used. **If the measured figure still exceeds the
  budget, the remaining lever is the window table** (shorten `W` for the Atmos/FX-freeze case from 20 s) —
  **never dropping presets, sample rates, or the chord arm from the sweep.**

### C-10 — Distinctness is a mechanical pairwise floor; category fit is a human checkpoint

**Q6 / CQ-6 (2026-08-04 Clarifications, Option B + D).** Two different failure modes are addressed by two
different mechanisms, deliberately not one:

1. **Mechanical distinctness.** Every SC in this spec is structural — 42 near-identical presets would
   satisfy all of them. The harness therefore computes each preset's `RenderFingerprint`
   (`render_fingerprint.h:54-60`) over the same `Sus` sustain window FR-024 already renders, and asserts
   that the pairwise fingerprint distance exceeds a stated floor for **all 861** unordered preset pairs
   (`C(42,2)`). The floor is **measured, then pinned**, per C-7's existing rule for this machinery — never
   a bit-exact comparison (that would reintroduce the FR-027a/SC-026-forbidden float-bit-digest).
2. **Category fit is not mechanized.** Whether a "Bells" preset actually sounds bell-like has no cheap,
   defensible spectral heuristic (a per-category classifier was considered and rejected — expensive to
   define, brittle to author against). It is instead a **human listening checkpoint**: the phase owner
   auditions all 42 presets against their assigned category and records the outcome in the compliance
   document. No automated arm substitutes for it.

---

## Functional Requirements

### Category set and library

- **FR-001** `makeSeraphisPresetConfig()` (`plugins/seraphis/src/preset/seraphis_preset_config.h:24-31`)
  MUST declare exactly the seven C-1 subcategories in the C-1 order, with `Textures` byte-identical to
  its current value. The other three fields (`processorUID`, `pluginName == "Seraphis"`,
  `pluginCategoryDesc == "Synth"`) MUST be unchanged, and the designated-initializer field order
  MUST continue to match `PresetManagerConfig`'s declaration order
  (`plugins/shared/src/preset/preset_manager_config.h:16-24`).
- **FR-002** `plugins/seraphis/resources/presets/` MUST contain exactly one directory per C-1 category
  and no other directory, and every `.vstpreset` file MUST sit directly inside one of them — a 1:1
  correspondence in **both** directions with FR-001's list.
- **FR-003** Each preset file MUST embed an `Info` chunk (Membrum's two-entry `List` form,
  `tools/membrum_preset_generator.cpp:349-400`) whose `MusicalCategory` **and** `MusicalInstrument`
  attributes equal the preset's own directory name, whose `PlugInName` equals
  `PresetManagerConfig::pluginName` (`"Seraphis"`), whose `PlugInCategory` equals
  `pluginCategoryDesc` (`"Synth"`), whose `MediaType` is `VstPreset`, and whose `Name` equals the
  file stem.
- **FR-004** The library MUST contain **42** presets, **≥ 5 in every category** (C-2).
- **FR-005** Every preset name MUST be unique across the whole library (not merely within a category),
  MUST satisfy `PresetManager::isValidPresetName` (`preset_manager.h:120`), and MUST be ASCII with no
  path separators, so the same name is safe as a filename, an XML attribute value and a browser row.
- **FR-006** Every preset's component chunk MUST be a full current-version stream: first int32 equals
  `kCurrentStateVersion` (== 3, `plugin_ids.h:27`) and total length equals the shipped **2868 bytes**
  (`processor.cpp:1836-1843`). No preset may be a truncated (v1/v2-prefix) stream, even though the loader
  would accept one (`processor.cpp:1762-1790`).
- **FR-006a** **OQ-4 ratified NO (Q4 / CQ-4 Option A, 2026-08-04 Clarifications).** No factory preset
  exercises the Phase 11 `[partials]` override block. Every preset's two `[partials]` bitmasks
  (`partialPanOverrideBits_`, `partialMaskBits_`, written at `processor.cpp:1911-1914`) MUST be
  **all-zero**, and all 64 pan floats MUST be 0.0f — asserted by the harness rather than assumed. The
  generator has **no** `IMessage`/`HostApplication`/`notify()` drive surface; it stays a single parameter
  fan-out (C-4).
- **FR-007** The library MUST satisfy the C-2 coverage matrix in full.
- **FR-008** Every preset MUST store `polyphony ≤ 8` and `softLimit` ON (C-5).
- **FR-008a** **Envelope-timing authoring ceiling (Q8 / CQ-8 Option B, 2026-08-04 Clarifications).** Every
  factory preset MUST store envelope timing such that the C-6.1-computed attack span `A ≤ 12.0 s` and
  release `Rel ≤ 10.0 s`, decoded via FR-025a. This is an **authoring constraint that bounds the sweep by
  bounding what ships** — every arm still measures the real, unmodified preset (no timeline truncation); a
  preset whose stored Growth duration or stage times would push `A` or `Rel` past the ceiling MUST be
  re-authored, not measured with a capped `A_used`.
- **FR-009** No preset may store a non-finite float anywhere in its stream (checked by bit pattern, per
  the `-ffast-math` rule), and every stored `[partials]` pan value MUST lie in [−1, 1] — the range
  `loadPartialOverrides` clamps to (`processor.cpp:485`).

### Generator

- **FR-010** A new tool `tools/seraphis_preset_generator.cpp` MUST build as the CMake executable target
  **`seraphis_preset_generator`** with `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`, matching the
  six existing generators (root `CMakeLists.txt:499-590`). The target name and output location are fixed
  by `release.yml:158-174` and are not free choices.
- **FR-011** The generator MUST accept the output directory as `argv[1]`, create the seven category
  subdirectories under it, and write all 42 files — the contract
  `./build/bin/seraphis_preset_generator generated-presets` relies on (`release.yml:170-174`), whose
  upload step fails the release on an empty directory (`:180`).
- **FR-012** A custom target **`generate_seraphis_presets`** MUST regenerate the committed tree in place
  (`COMMAND seraphis_preset_generator "${CMAKE_SOURCE_DIR}/plugins/seraphis/resources/presets"`), matching
  the pattern at root `CMakeLists.txt:517-522`.
- **FR-013** The generator MUST derive every preset's component chunk from the shipped
  `Seraphis::Processor::getState()` through the C-4 drive sequence. It MUST NOT re-implement any part of
  the state layout, MUST NOT introduce a `tools/seraphis_preset_format.h`, and MUST NOT hardcode the
  class-id string (C-3).
- **FR-014** The generator MUST be deterministic and idempotent: two consecutive runs into two empty
  directories produce byte-identical files, and re-running over an existing tree leaves it unchanged
  (checked by FR-035a's script, measured by SC-016). **The byte-level claim is scoped to one binary on one
  machine in one run set** — it is *not* a claim across toolchains, and FR-029 must not be read as its
  cross-platform extension (C-7, C-8).
- **FR-015** The generator MUST build and run on the release runner's toolchain (Linux / GCC / Ninja,
  `release.yml:141-169`), linking only what a headless processor needs. It MUST NOT require VSTGUI:
  `processor.cpp` includes no VSTGUI header (verified — its only `ui/` include is
  `ui/edit_message.h`, whose banner states it carries *"no VSTGUI type"*, `processor.cpp:18`, `:21`), and
  the parameter packs reach only `public.sdk/source/vst/vstparameters.h` through
  `plugins/shared/src/ui/parameter_helpers.h:11-15`.
- **FR-016** Preset definitions MUST be expressed as `{ParamID, normalizedValue}` pairs plus name /
  category / description, so that no denormalization arithmetic is duplicated outside the shipped
  `handle*ParamChange` functions. **OQ-4 is ratified NO (Q4 / CQ-4 Option A, 2026-08-04 Clarifications):**
  the `[partials]` block — not reachable through any ParamID, written only from `partialPanStaging_` /
  `partialPanOverrideBits_` / `partialMaskBits_` (`processor.cpp:1911-1914`) via `applyEditMessage` off
  `Processor::notify()` — is **never** driven by the generator. No preset definition carries an
  `EditMessage` list, the generator never calls `Processor::notify()`, and FR-006a's all-zero rule applies
  to every preset without exception.
- **FR-016a** The preset definition table MUST live in a **header shared by the generator TU and the
  FR-029 test TU** (`tools/seraphis_preset_defs.h`, data only — names, categories, descriptions,
  `{ParamID, normalizedValue}` pairs, and an **optional per-preset audition-stimulus override** (MIDI note
  number, normalized velocity — Q2 / CQ-2 Option B, FR-024a)). It MUST contain no state layout and no
  serialization code (C-3 is unchanged); it carries **no** `EditMessage` list (FR-016, OQ-4 ratified no).
  Sharing the *definitions* is what lets FR-029's comparison be performed in-process on every CI leg
  without shelling out to the generator binary.

### Installation

- **FR-017** Factory presets MUST install to `%PROGRAMDATA%\Krate Audio\Seraphis\<Category>\*.vstpreset`
  on Windows through the **existing** `krate_plugin_install_presets(${PLUGIN_NAME})` call
  (`plugins/seraphis/CMakeLists.txt:113`). No new install rule, no `SRC_SUBDIR`/`DEST_SUBDIR` (that form
  exists only for Membrum's nested `Kits` layout, `cmake/KratePlugin.cmake:282-285`). This requirement is
  a **verification** obligation, not a code change: the destination must match
  `Platform::getFactoryPresetDirectory("Seraphis")`
  (`plugins/shared/src/platform/preset_paths.h:23-27`).
- **FR-018** The Windows installer's existing preset line
  (`installers/windows/setup.iss:66-68`) MUST remain the sole install path for presets, and the Linux
  README's documented destinations MUST match `preset_paths.h`'s Linux branch. Both are verified, not
  rewritten.

### Validation harness

All harness TUs are **enumerated** in `plugins/seraphis/tests/CMakeLists.txt` (never globbed — the file's
own rule, `:16-18`, `:29-31`, `:36-39`) and run inside `seraphis_tests`.

- **FR-019** A factory-preset **container** test MUST assert, for every committed `.vstpreset`: the
  `VST3` magic, a class id equal to `Seraphis::kProcessorUID` in `FUID::toString` form, a `List` chunk
  containing both `Comp` and `Info` entries, and offsets/sizes that lie inside the file. Pattern:
  `plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp:1-13` and
  `plugins/disrumpo/tests/integration/preset/factory_preset_validation_test.cpp:37-64`.
- **FR-020** A **round-trip** test MUST assert, for every preset: `setState(Comp chunk)` succeeds, and the
  subsequent `getState()` is **byte-identical** to the chunk that was loaded. (This is the shipped FR-094
  property — `processor.cpp:434-436` — applied to real files.)
- **FR-021** A **metadata↔filesystem** test MUST parse each file's `Info` XML and assert **all six**
  FR-003 attribute equalities (`MediaType`, `PlugInName`, `PlugInCategory`, `Name`, `MusicalCategory`,
  `MusicalInstrument` — the exact six the reference writer emits,
  `tools/membrum_preset_generator.cpp:353-358`), and MUST assert that each file's directory name is a
  member of `makeSeraphisPresetConfig().subcategoryNames`.
- **FR-022** A **browser-scan** test MUST construct a `PresetManager` with **both** directory overrides
  (`preset_manager.h:55-61`): `userDirOverride` (4th parameter) pointing at an **empty temporary
  directory** created by the test, and `factoryDirOverride` (5th parameter) pointing at
  `resources/presets`. The user override is load-bearing, not hygiene: `scanPresets()` scans the **user**
  directory *first* and the factory directory second (`preset_manager.cpp:41-49`), so leaving the user
  path at its real machine location makes the count and the `isFactory` tally depend on whatever the
  developer or a prior test saved. Against that isolated fixture the test MUST assert: the returned count
  equals FR-004's 42, **every** entry has a non-empty `subcategory` (an empty one is exactly the Membrum
  failure mode, `preset_manager.cpp:95-103`), every entry has `isFactory == true`, and
  `getPresetsForSubcategory(c)` (`:75`) returns the expected count for each of the seven categories.
- **FR-023** A **tab-agreement** test MUST assert that the browser tab list the controller builds is
  `{"All"} ∪ config.subcategoryNames` in that order (`controller.cpp:462-469`), so a category added to the
  filesystem but not to the config, or vice versa, fails a test rather than silently hiding presets.
- **FR-024** An **all-presets render sweep** MUST, for every preset, run **one** render on the C-6.1
  timeline: load the preset into a prepared processor, send a **single NoteOn at t = 0** through the
  `IEventList` path (`tests/test_helpers/vst_event_list.h`, the fixture's event stand-in,
  `seraphis_test_fixture.h:1-20`), **hold it with no NoteOff for `H = A + 5.0 s`** (the roadmap's
  NoteOn-only condition, line 608 — the bounded arm of FR-025 covers this hold), then send **one NoteOff
  at t = H**, then render the tail to `Total = Settle + W`. **The stimulus (Q2 / CQ-2 Option B, 2026-08-04
  Clarifications) is MIDI pitch 60, velocity 0.8 for every preset by default**; a preset's FR-016a
  definition MAY supply an override `{pitch, velocity}` pair for an outlier preset, and the sweep MUST use
  it in place of the default when present. `A`, `Rel`, `Settle` and `W` are computed from
  the preset's own decoded state per C-6.1's table; nothing in the timeline is a hardcoded per-preset
  constant. The sweep MUST assert **non-silence**: RMS over the sustain window `Sus = [A + 1.0 s,
  A + 4.0 s]` strictly above the SC-009 floor. The `+1.0 s` offset exists because a Growth-mode preset is
  legitimately near-silent for several seconds (growth duration default 10.0 s,
  `life_mod_params.h:65`), so a window that opens at t = 0 would fail correct presets.
- **FR-024a** **Chord bounded-arm render (Q2 / CQ-2 Option D).** In addition to the FR-024 single-note
  render, the sweep MUST render, **once per preset at 44 100 Hz only**, a second render using a held
  **4-note chord** (four simultaneous NoteOns at t = 0, using the preset's default-or-override pitch as the
  chord root plus three fixed additional MIDI note numbers, all at the FR-024 stimulus's velocity), sent
  through the same `IEventList` path, and rendered to at least `H` (no NoteOff required — only the bounded
  arm is asserted). This render MUST satisfy **only** the C-6.2 bounded arm (FR-025's finite/peak checks);
  it does **not** assert FR-024's sustain floor or FR-026's tail arm. Its purpose is to exercise
  `polyphony ≤ 8` (FR-008) and the multi-voice limiter sum in actual audio without paying chord-render cost
  on the 60 s tail arms — no other arm of this phase renders more than one voice (SC-010a).
- **FR-025** The same render MUST satisfy the C-6.2 **bounded** arm over its **whole** length `[0, Total]`,
  the NoteOn-only hold included: no NaN/Inf by bit pattern, and peak ≤ `kLimiterCeilingLin` × 10^(0.1/20).
- **FR-025a** **Typed-field decode mechanism (Q1 / CQ-1 Option C, 2026-08-04 Clarifications).** Everywhere
  this phase needs a preset's **typed, denormalized** field values — FR-026's freeze toggles / RT60 /
  envelope timing, FR-008a's `A`/`Rel` ceiling check, SC-013's coverage-matrix values, SC-014's
  polyphony/softLimit, and FR-029 clause 4's integer/enum/bool comparison — the harness MUST instantiate
  the shipped per-block parameter structs in the test binary and call their shipped processor-side
  `load*Params(Params&, IBStreamer&)` free functions (`global_params.h:218`, `aether_params.h:297`,
  `life_mod_params.h:298`, and the remaining packs) **in the exact `getState()` block order**
  (`processor.cpp:1836-1917`), reading real denormalized values (Aether decay in seconds, freeze toggles as
  `bool`) directly — **never** re-deriving them by arithmetic. The four 541-byte `SpectralState` payload
  blocks and the 272-byte `[partials]` block MUST be **hand-skipped** (the streamer advanced by their known
  byte counts) rather than decoded by this mechanism — FR-029 clause 5 decodes `SpectralState` payloads
  separately, via `deserializeSpectralState`. As a **mandatory tripwire**, the reader MUST assert the
  `IBStreamer` has consumed **exactly 2868 bytes** by the end of the read, so any drift between this decode
  order and the shipped `getState()` order fails loudly. This is **not** a shared decode header edited into
  `Controller::setComponentState` (that option is deferred — sanctioned only later, if a third copy of the
  block order proves unacceptable, not in this phase).
- **FR-026** The same render MUST satisfy the C-6.3 **tail** arm, classified **from the three freeze
  toggles decoded — via FR-025a's mechanism — out of the preset's own `[atmos]` / `[aether]` / `[effects]`
  blocks** and from nothing else — Aether-freeze ⇒ conservation over 60 s (Windows CI leg only, C-9/FR-027);
  Atmos/FX-freeze-only ⇒ bounded-and-non-growing over 20 s; no freeze ⇒ RT60-consistent decay over 10 s.
  Measurement begins at `Settle`, never at the NoteOff: the settling allowance is a required part of the
  criterion, not an implementation detail.
- **FR-027** The sweep MUST run at **two sample rates** (44 100 and 48 000 Hz), so a preset that is stable
  only at the development rate fails. Per C-9: at 48 000 Hz the render stops at `H + 5 s` and asserts
  FR-024's non-silence and FR-025's bounded arm; FR-026's tail arm is asserted at 44 100 Hz only. **Per C-9
  / Q8 / CQ-8 (2026-08-04 Clarifications, Option C):** the Aether-freeze arm of FR-026/SC-012 (the 60 s
  conservation band) runs on the **Windows** CI leg only; the macOS and Linux legs of `seraphis_tests` run
  the sustain (FR-024/SC-009), bounded (FR-025/SC-010) and chord-bounded (FR-024a/SC-010a) arms plus the
  non-Aether tail arms (SC-011, SC-012's Atmos/FX-freeze-only case), but skip the 60 s Aether-freeze
  observation.
- **FR-027a** **Render reproducibility (the enforcing half of C-7).** For every preset, **two renders at
  the same sample rate, seed and block size within one process** — each from a freshly prepared processor
  instance driven through the identical event and parameter sequence — MUST agree under
  `Krate::DSP::TestUtils::compareFingerprints`, i.e. `FingerprintComparison::withinTolerance()` is true:
  worst aggregate-metric relative error ≤ `kMetricTolerance = 1e-5` and worst checkpoint-sample error
  ≤ `kSampleTolerance = 1e-4f` (`tests/test_helpers/render_fingerprint.h:49`, `:52`, `:95-97`,
  `:101-…`). To bound cost this arm renders the sustain window only (`[0, H]`) at 44 100 Hz.
  **No float bit digest, and no integer digest derived from float bits, may be introduced by this phase**
  — for the sweep, for the fixtures, or anywhere else (roadmap line 664; `ci.yml:162-166`). SC-016's
  file-byte comparison is the sole, narrowly-premised exception and is not float-render data. **Per C-9 /
  Q8 / CQ-8 (2026-08-04 Clarifications, Option C), this render (SC-026) runs on the Windows CI leg only**
  — it is the second of the two heaviest arms tagged out of macOS/Linux; those legs run the single-render
  arms (FR-024–FR-026) unaffected.
- **FR-027b** **Distinctness arm (C-10, Q6 / CQ-6 Option B, 2026-08-04 Clarifications).** For every one of
  the **861** unordered preset pairs, the harness MUST compute each preset's `RenderFingerprint` over the
  `Sus` sustain window (the same window FR-024 uses) and assert the pairwise fingerprint distance exceeds a
  floor. The floor MUST be **measured** (not asserted without measurement) using the same measure-then-pin
  discipline as C-7's reproducibility tolerances, and recorded in the compliance document. This arm asserts
  **distance**, never bit-exact equality or inequality — it remains subject to the FR-027a float-bit-digest
  prohibition.
- **FR-028** **Sequential arm.** Loading every preset in sequence into a **running, warmed** processor —
  `setState` on the message thread **between** `process()` calls, never concurrently with one — MUST NOT
  allocate, asserted with `TestHelpers::AllocationScope`
  (`tests/test_helpers/allocation_detector.h:75-95`) around the render calls after the buffers are warm
  (the fixture's documented precondition, `seraphis_test_fixture.h:13-19`). This is a **quiescent-load**
  requirement and is stated as one: `AllocationScope` wraps a process-global singleton counter with no
  thread filter (`allocation_detector.h:60-63`, `:75-95`), so it cannot attribute an allocation to a
  thread and cannot, on its own, say anything about a concurrent load.
- **FR-028a** **Concurrency arm.** A distinct test MUST reach the precondition FR-028 cannot: a message
  thread calling `setState` in a loop over all 42 presets **while** the audio thread renders continuously.
  It MUST assert (a) output stays finite and bounded by C-6.2 throughout, (b) every `setState` returns
  `kResultOk` and the final state is one of the 42 loaded, and (c) **no audio-thread allocation**, using a
  **thread-scoped** instrument — admissible mechanisms, plan's choice: an opt-in thread filter added to
  `AllocationDetector` (additive, default-off, so the six other plugins' existing usage is unchanged), or
  running this TU under the existing valgrind-linux / TSAN lane. Asserting (c) with the unfiltered global
  `AllocationScope` is **forbidden**: it would count the message thread's own `setState` allocations as
  audio-thread violations and fail regardless of RT safety.
- **FR-029** A **committed-tree** test MUST assert that the checked-in `resources/presets` tree is what the
  current generator source produces from the current preset definitions, **compared semantically, never
  byte-for-byte** (C-8 — the artifacts are produced by different toolchains and the stream carries
  `std::pow`/`std::exp`-derived float bit patterns). Using the FR-016a shared definitions header, the test
  regenerates every preset's `Comp` chunk in-process and asserts, for each committed file:
  1. the **file set and relative paths** are identical in both directions (no extra, no missing);
  2. the `Info` XML is **byte-identical** (pure ASCII, toolchain-invariant);
  3. the state version int32 == 3 and the `Comp` length == **2868** bytes;
  4. every integer, enum and bool field, read via the FR-025a decode mechanism, decodes **exactly equal**;
  5. every single-`std::pow`-denormalized **scalar** float field (e.g. Aether decay) is equal within the
     **scalar** tolerance FR-029a measures and pins (never asserted without having been measured); and
     every `SpectralState` payload is compared **structurally, with its own separately measured
     tolerance** — `numPartials` matches exactly, `isValidSpectralState` (via the shipped
     `deserializeSpectralState`) is true on both sides, and `ratios`/`amplitudes`/`tiltDbPerOct`/
     `inharmonicity` match within the FR-029a-measured **payload** tolerance, which is never the same
     number as the scalar tolerance (Q3 / CQ-3, 2026-08-04 Clarifications, Option B + C).

  The test runs inside `seraphis_tests` on **all three CI legs**, so each toolchain proves its own
  generator output is the committed library.
- **FR-029a** **Float tolerances are measured, then pinned (Q3 / CQ-3 Option B + C, 2026-08-04
  Clarifications).** Before FR-029/SC-017 ship with fixed numbers, the build stage MUST measure the actual
  per-field relative error between the Windows/MSVC-committed tree and an in-process regeneration on
  another toolchain — a local WSL/GCC probe plus one CI dry run on the real macOS and Linux legs —
  recording the worst observed error **separately** for (a) single-`std::pow`-denormalized scalar fields
  and (b) `SpectralState` payload ratio/amplitude/tilt/inharmonicity fields, then pinning **each class's
  own tolerance at ~10× its own worst observed error**. Neither tolerance may be a number asserted without
  first having been measured, and the two classes MUST NOT share one tolerance. Both measurements and
  their provenance MUST be recorded in the compliance document.

### Release gate

- **FR-030** No Phase 12 release-readiness verdict may be reported green while Phase 11.5's exit criteria
  (roadmap lines 592–600) are unmet. **The verdict MUST be recorded explicitly as `DEFERRED` (Q7 / CQ-7,
  2026-08-04 Clarifications, Option B) — never silently withheld or left blank** — until Phase 11.5 is
  green. Phase 12's compliance document MUST cite Phase 11.5's **measured** whole-`process()` figure with
  its provenance, not its target.
- **FR-031** `.claude/workflows/release-readiness.js` `PLUGIN_MAP` MUST gain
  `seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' }` — it currently stops at `membrum`
  (`:14-21`), so a `release-readiness` run silently skips Seraphis today.
- **FR-032** `.claude/skills/release/SKILL.md` MUST gain `seraphis` to both its **Plugin** input list
  (`:15`, which today ends at `membrum`) and its **Plugin → target/bundle map** table (`:22-29`), for the
  same reason.
- **FR-033** `plugins/seraphis/version.json` MUST be bumped from **0.4.0** to **0.5.0** (Q7 / CQ-7,
  2026-08-04 Clarifications, Option B — version field only, never the generated `src/version.h`) with a
  matching `## [0.5.0]` section in `plugins/seraphis/CHANGELOG.md` in the same change, and
  `node tools/check-changelog-coverage.js seraphis` MUST exit 0
  (`tools/check-changelog-coverage.js:50` already lists `seraphis`). **`1.0.0` is reserved for the release
  that includes Phase 13 (per-note expression/MPE); this phase does not claim it.**
- **FR-034** `plugins/seraphis/CLAUDE.md` MUST record the FR-001 category list as a load-bearing table
  (the same treatment its parameter-band table gets, `plugins/seraphis/CLAUDE.md:22-32`), stating that
  categories are additive-only and that a rename orphans user presets.
- **FR-034a** **Category-fit listening checkpoint (C-10, Q6 / CQ-6 Option D, 2026-08-04 Clarifications).**
  The phase owner MUST audition all 42 presets against their assigned category name and record the outcome
  (pass/fail or notes, per preset) in the compliance document. No automated spectral-heuristic arm is
  added in this phase; a missing record is treated the same as a missing FR-030/SC-025 figure — the
  release verdict cannot be reported green without it.
- **FR-035** The release gate MUST run, in order: build → `seraphis_tests` green → pluginval strictness 5
  clean on `build/windows-x64-release/VST3/Release/Seraphis.vst3` → `version.json`/CHANGELOG sync, plus
  `node tools/check-portability.js` clean, clang-tidy `-Target seraphis` clean, and the FR-035a
  determinism script exit-0. Zero compiler warnings.
- **FR-035a** FR-014's determinism and idempotence MUST be checked by a **script, not a one-shot manual
  observation**: a Node helper (`tools/check-preset-generator-determinism.js`, per the project's
  Node-for-tooling rule) that (1) runs `seraphis_preset_generator` into two fresh temp directories and
  diffs them byte-for-byte, (2) runs it a **third** time over one of those now-populated directories and
  asserts no file changed (the idempotence half, which had no measurement at all), and (3) exits non-zero
  on any difference. It runs in the FR-035 gate alongside `check-portability.js`. Byte comparison is
  legitimate here and only here — one binary, one machine, one run set (C-7).

---

## Success Criteria

Each criterion names the metric, the threshold and the test that measures it. Test names are sketches;
the plan may rename them, but the arms must survive.

- **SC-001 — Category/filesystem bijection.** The set of directory names under `resources/presets` equals
  `makeSeraphisPresetConfig().subcategoryNames` exactly (no extra dir, no missing dir), and no
  `.vstpreset` exists outside them. *Measured by:* `Seraphis_FactoryPresets_CategoriesMatchConfig`
  (`tests/unit/preset/factory_preset_test.cpp`). Threshold: symmetric-difference size **0**.
- **SC-002 — Library size and distribution.** Total files == **42**; per-category count ≥ **5**.
  *Measured by:* `Seraphis_FactoryPresets_CountAndDistribution`.
- **SC-003 — Container validity.** **100 %** of files parse: `VST3` magic, class id ==
  `FUID::toString(kProcessorUID)`, `List` with both `Comp` and `Info`, all offsets in range.
  *Measured by:* `Seraphis_FactoryPresets_ContainerIsValid`.
- **SC-004 — State version and length.** **100 %** of component chunks begin with int32 `3` and are
  exactly **2868** bytes. *Measured by:* `Seraphis_FactoryPresets_StreamIsCurrentVersion`.
- **SC-005 — Round-trip is byte-identical.** For **100 %** of presets,
  `getState(setState(chunk))` == `chunk`, compared with `std::memcmp`. Threshold: **0** differing bytes,
  **0** failed `setState` calls. *Measured by:* `Seraphis_FactoryPresets_RoundTripByteIdentical`.
- **SC-006 — Metadata agrees with the filesystem.** For **100 %** of presets, all **six** FR-003 attribute
  equalities hold — `MediaType`, `PlugInName`, `PlugInCategory`, `Name`, `MusicalCategory`,
  `MusicalInstrument` (enumerated here so the count is not load-bearing). Threshold: **0** mismatches.
  *Measured by:* `Seraphis_FactoryPresets_InfoMetadataMatchesDirectory`.
- **SC-007 — The browser sees every preset, correctly filed.** Against FR-022's isolated fixture — a
  `PresetManager` built with `userDirOverride` = an **empty temp directory** and `factoryDirOverride` =
  `resources/presets` — `scanPresets()` returns **42** entries, **0** with an empty `subcategory`,
  **0** with `isFactory == false`, and per-category `getPresetsForSubcategory` counts equal the on-disk
  counts for all seven categories. Without the user override the thresholds are machine-state dependent
  and the criterion is not determinate. *Measured by:*
  `Seraphis_FactoryPresets_BrowserScanFilesEveryPreset`.
- **SC-008 — Tab list cannot drift.** The controller-built tab vector equals
  `{"All"} ∪ subcategoryNames` element-wise. *Measured by:*
  `Seraphis_PresetBrowser_TabsMatchConfig`.
- **SC-009 — Every preset makes sound.** RMS over the sustain window `Sus = [A + 1.0 s, A + 4.0 s]`
  after NoteOn — with `A` = the preset's own decoded attack span per C-6.1, **not** an undefined
  "sustain window" — is **> −60 dBFS** for **100 %** of presets, at **both** 44 100 and 48 000 Hz.
  *Measured by:* `Seraphis_PresetSweep_NoSilence` (`tests/integration/preset_render_sweep_test.cpp`).
- **SC-010 — Every preset stays bounded.** Over the **whole** render `[0, Total]` — NoteOn-only hold
  `[0, H]` included — **0** non-finite samples (bit-pattern test) and peak ≤
  **0.8912509 × 10^(0.1/20) ≈ 0.9016** for **100 %** of presets at both rates (48 kHz renders to `H + 5 s`
  per C-9). *Measured by:* `Seraphis_PresetSweep_BoundedAndFinite`.
- **SC-010a — The 4-note chord stays bounded too.** For **100 %** of presets, the FR-024a chord render
  (44 100 Hz only) has **0** non-finite samples and peak ≤ the same SC-010 ceiling
  (**≈0.9016**), so `polyphony ≤ 8` (FR-008) and the multi-voice limiter sum are exercised by an actual
  render rather than assumed from the single-note arms. *Measured by:*
  `Seraphis_PresetSweep_ChordBoundedAndFinite`.
- **SC-011 — Decay is consistent with the preset's own RT60 (no-freeze presets).** For every preset with
  all three freeze toggles OFF, over the 10 s window starting at `Settle`:
  `dropDb = 20·log10(RMS(first second) / RMS(final second))` satisfies
  **`dropDb ≥ min(0.5 · 60 · 10 / RT60, 20.0)` dB**. Worked thresholds: RT60 ≤ 6 s ⇒ **≥ 20 dB** (the
  original bound, unchanged); RT60 = 30 s ⇒ ≥ 10 dB; RT60 = 60 s ⇒ ≥ 5 dB. Threshold: **0** presets below
  their own bound. *Measured by:* `Seraphis_PresetSweep_DecayMatchesRt60`.
- **SC-012 — Frozen presets hold, each against the property its mechanism actually promises.** Both arms
  measured at 44 100 Hz starting at `Settle = H + Rel + 2.0 s`:
  - **`kAetherFreezeId` ON** — the per-second RMS series over **60 s** stays within a **±1.0 dB** band
    (max − min ≤ **2.0 dB**). The 60 s duration is roadmap line 282's, unreduced; the only relaxation is
    the band (0.5 → 1.0 dB, disclosed in C-6.3 with its reason). **This arm runs on the Windows CI leg only
    (Q8 / CQ-8, 2026-08-04 Clarifications, Option C) — one of the two heaviest arms tagged out of
    macOS/Linux.**
  - **`kAtmosFreezeId` and/or `kFxSpectralFreezeId` ON with `kAetherFreezeId` OFF** — over **20 s**, the
    output is bounded and **non-growing**: final-second RMS ≤ loudest-second RMS **+ 1.0 dB**. No
    conservation band is asserted, because neither mechanism is promised energy conservation anywhere in
    the roadmap (Phase 5, lines 248-254; Phase 10, lines 464-473). This arm runs on **all three** CI legs.

  Threshold for both arms: **0** presets outside their band, on whichever leg(s) run them. *Measured by:*
  `Seraphis_PresetSweep_FrozenPresetsHold`.
- **SC-013 — Coverage matrix satisfied.** Every row of C-2's table is satisfied by the loaded states of
  the 42 presets; the test reports which value is missing on failure. Threshold: **0** unmet rows.
  *Measured by:* `Seraphis_FactoryPresets_CoversShippedSurface`.
- **SC-014 — Budget-safe presets.** **100 %** of presets decode `polyphony ≤ 8` and `softLimit == true`.
  *Measured by:* `Seraphis_FactoryPresets_RespectVoiceBudget`.
- **SC-014a — Envelope timing stays inside the authored ceiling.** **100 %** of presets decode
  (via FR-025a) `A ≤ 12.0 s` and `Rel ≤ 10.0 s` (FR-008a). Threshold: **0** presets over either ceiling.
  *Measured by:* `Seraphis_FactoryPresets_RespectTimingCeiling`.
- **SC-015 — Sequential preset switching allocates nothing.** Loading all 42 presets in sequence into a
  running, warmed processor, with `setState` strictly **between** `process()` calls, yields **0**
  allocations inside `TestHelpers::AllocationScope` around the render calls. This is the **quiescent-load**
  claim and nothing more — the instrument is a process-global counter with no thread filter
  (`allocation_detector.h:60-63`, `:75-95`). *Measured by:*
  `Seraphis_PresetSweep_NoAudioThreadAllocation`.
- **SC-015a — Concurrent preset load is RT-safe and correct.** With a message thread calling `setState`
  over all 42 presets **while** the audio thread renders continuously (FR-028a): **0** non-finite samples,
  peak within SC-010's ceiling, **0** failed `setState` calls, and **0** audio-thread allocations reported
  by the thread-scoped instrument (or a clean valgrind/TSAN lane run for that TU). *Measured by:*
  `Seraphis_PresetSweep_ConcurrentLoadIsRtSafe`. A green SC-015 does **not** imply SC-015a: they reach
  different preconditions with different instruments.
- **SC-016 — Generator determinism and idempotence, automated.** `node
  tools/check-preset-generator-determinism.js` (FR-035a) exits **0**: two runs into two empty temp
  directories produce **byte-identical** trees (**0** differing files), and a third run over an already
  populated tree changes **0** files. It is a gate in the FR-035 release run, not a one-shot log entry, so
  a later change that makes the generator non-deterministic (unordered iteration, an embedded timestamp)
  fails automatically.
- **SC-017 — Committed tree is semantically the generator's output, on every leg.** FR-029's semantic
  comparison reports **0** file-set differences, **0** `Info`-XML byte differences, **0** version/length
  differences, **0** integer/enum/bool mismatches, **0** scalar float fields outside the FR-029a-measured
  **scalar** tolerance, and **0** `SpectralState` payload fields outside the FR-029a-measured **payload**
  tolerance (a separate, typically looser number than the scalar tolerance — never one number covering
  both classes) — asserted on Windows, macOS **and** Linux legs of `seraphis_tests`. **No byte-level
  comparison of `Comp` chunks is performed** (C-8: the toolchains differ and the payloads are
  `std::pow`/`std::exp`-derived bit patterns). *Measured by:*
  `Seraphis_FactoryPresets_TreeMatchesGenerator`.
- **SC-018 — The generator builds and runs on the release toolchain.** `cmake --build … --target
  seraphis_preset_generator` succeeds with **0 warnings** and the binary emits 42 files, verified on
  Linux/GCC (WSL probe locally per the project rule, and the CI/release leg once run). *Measured by:*
  the build log + a file count.
- **SC-019 — Windows install destination.** After a Release build, `%PROGRAMDATA%\Krate Audio\Seraphis`
  contains the seven category directories and 42 files, matching
  `Platform::getFactoryPresetDirectory("Seraphis")`. *Measured by:* post-build directory listing recorded
  in the compliance document.
- **SC-020 — pluginval strictness 5.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Seraphis.vst3"` exits **0** with no reported failure.
- **SC-021 — Suite green and grown.** `seraphis_tests` reports **0** failures, and its `test cases:` count
  is **strictly greater** than the pre-phase baseline by the number of new cases (guards against a TU
  missing from the enumerated list, which exits 0 silently). *Measured by:* the `tail -5` summary before
  and after.
- **SC-022 — Portability and lint.** `node tools/check-portability.js` clean;
  `node tools/check-preset-generator-determinism.js` exit **0** (SC-016);
  `./tools/run-clang-tidy.ps1 -Target seraphis` reports **0** warnings; the build reports **0** compiler
  warnings.
- **SC-023 — Changelog/version sync.** `node tools/check-changelog-coverage.js seraphis` exits **0**, and
  `version.json`'s version has a matching `## [X.Y.Z]` heading in `CHANGELOG.md`.
- **SC-024 — Release-roster registration is real.** `.claude/workflows/release-readiness.js` invoked with
  `{plugins:["seraphis"]}` resolves a test target and bundle (rather than filtering the name out at
  `:26-29`), and `.claude/skills/release/SKILL.md`'s table contains the Seraphis row. *Measured by:*
  reading both files back after the change and running the flow.
- **SC-025 — Phase 11.5 gate honoured.** The compliance document records Phase 11.5's measured
  whole-`process()` figure at the 8-voice operating point (worst-of-seven, fresh boot) as **≤ 25 % of one
  core**, with its file:line provenance, before any release verdict is stated. A missing or unmet figure
  produces a verdict recorded explicitly as **`DEFERRED`** (not merely "red", and never blank or omitted —
  Q7 / CQ-7 Option B) regardless of SC-001…SC-024, SC-028 and SC-029.
- **SC-026 — Renders are reproducible (C-7's enforcing criterion).** For **100 %** of presets, two renders
  of `[0, H]` at 44 100 Hz in one process, from freshly prepared processor instances driven identically,
  satisfy `compareFingerprints(...).withinTolerance()` — worst aggregate-metric relative error
  ≤ **1e-5** (`kMetricTolerance`, `render_fingerprint.h:52`) and worst checkpoint-sample error
  ≤ **1e-4** (`kSampleTolerance`, `:49`). Threshold: **0** presets failing, **0** float bit digests
  anywhere in the phase's new code (also enforced by the `ci.yml:162-166` lint). **Runs on the Windows CI
  leg only (Q8 / CQ-8, 2026-08-04 Clarifications, Option C — the second of the two heaviest arms tagged
  out of macOS/Linux).** *Measured by:* `Seraphis_PresetSweep_RendersAreReproducible`.
- **SC-027 — The sweep fits its budget.** The preset-render sweep TU completes in **≤ 6 minutes** wall
  clock on `windows-x64-release`, measured with the Catch2 run's own duration and recorded in the
  compliance document. Over budget ⇒ apply C-9's stated levers (shorten `W` for the Atmos/FX arm) —
  **never** drop presets or sample rates from the sweep. *Measured by:* the timed `seraphis_tests` run.
- **SC-028 — Presets are pairwise distinct.** All **861** unordered preset pairs' `RenderFingerprint`
  distance over `Sus` exceeds the C-10/FR-027b-measured floor. Threshold: **0** pairs below the floor.
  *Measured by:* `Seraphis_PresetSweep_PresetsAreDistinct`.
- **SC-029 — Category fit is human-verified.** The FR-034a listening checkpoint is recorded in the
  compliance document for **100 %** of the 42 presets (pass/fail or notes). Threshold: **0** presets
  without a recorded outcome. *Measured by:* the compliance document's checkpoint table.

---

## Edge Cases

**RT-safety boundaries**

- A preset load is a message-thread `setState()` that may run **concurrently with `process()`**
  (`processor.cpp:1813-1818`). The release path — the four-payload staging ring plus the single release
  store (`processor.cpp:1746-1811`) — **only exists for the concurrent case**, so the harness must
  exercise that interleaving. **FR-028/SC-015 do NOT exercise it**: they specify `setState` strictly
  *between* `process()` calls, and their instrument (`AllocationScope`) is a process-global counter with
  no thread filter (`allocation_detector.h:60-63`, `:75-95`) that would misattribute the message thread's
  own allocations if the two were interleaved. **FR-028a/SC-015a** is the arm that reaches the
  precondition, with a thread-scoped instrument or a valgrind/TSAN lane. An earlier draft of this
  paragraph claimed FR-028 covered the interleaving; it did not, and the mismatch would have shipped a
  criterion that proves only what existing coverage already establishes.
- `setState()` publishes via `requestPushAllSurfaces()` unless `gDisablePresetLoadPush` is set
  (`processor.cpp:1816-1818`); the sweep must run with the shipped (push-enabled) behaviour.
- The `[partials]` block is published only when the **whole** block was read
  (`processor.cpp:1830-1832`); a factory preset always carries a whole one (FR-006), so the partial-block
  absence path is *not* exercised by factory presets and must not be assumed tested by this sweep.

**Parameter extremes**

- Polyphony: registered 1…16, factory-pinned ≤ 8 (C-5). A preset at 16 would be legal but out-of-budget —
  SC-014 is what stops one slipping in. FR-024a's 4-note chord render (Q2/CQ-2) is the only arm in this
  phase that exercises multiple simultaneous voices in actual audio; every other arm renders a single
  note.
- Aether decay reaches 60 s (`aether_params.h:45-46`) and freeze is a first-class technique. The sweep
  handles the two cases with **different** windows rather than one truncated compromise: the frozen
  (energy-conserving) case is observed for the roadmap's full **60 s** (SC-012 arm 1), while a finite
  60 s **RT60** is a *decay rate*, not a sustain, and is checked against its own predicted rate over 10 s
  (SC-011). A single 20 s window could distinguish neither, which is why the earlier draft's classifier
  (`freeze OR decay ≥ 30 s` ⇒ ±1 dB band) made every long-decay unfrozen preset unshippable.
- Master gain reaches 2.0 (`global_params.h:91-96`) before the limiter, and *"The limiter is ALWAYS LAST
  and takes the whole block"* (`dsp/include/krate/dsp/systems/seraphis_engine.h:617`). A preset that
  leans on gain 2.0 is bounded by
  the limiter but will sit permanently in gain reduction — legal, but SC-010's ceiling arm is what proves
  the bound rather than assuming it.
- Feedback-bearing effects (`kFxDelayFeedbackId`) interact with the per-bin tilt over-unity behaviour the
  Phase 10 spec documents; a factory preset combining high feedback with extreme tilt is exactly the case
  SC-011/SC-012 must not silently pass by rendering too short a window.

**Sample-rate and block-size changes**

- The sweep runs at 44 100 and 48 000 Hz (FR-027). Presets store **no** sample-rate-dependent value, so a
  criterion that passes at one rate and fails at the other is a defect, not a tolerance issue.
- Block size: the chain is documented block-size invariant to ~1e-5 but **not** bit-identical across
  partitions (`plugins/seraphis/tests/integration/processor_audio_test.cpp:91-96`); the sweep fixes one
  block size and does **not** re-litigate that —
  the invariance claim belongs to its existing owner.

**Seed determinism**

- `kSeedId` is an **index** into `kSeedValues`, pinned so index 0 == seed 1u
  (`global_params.h:53-59`, `dropdown_mappings.h:93-99`). Two presets sharing a seed index render from the
  same stream; presets that intend audibly different motion should not all sit on index 0.
- Reproducibility is asserted through `render_fingerprint.h` tolerances (C-7), and the requirement that
  makes that real is **FR-027a**, measured by **SC-026** — not this bullet. **No bit-exact float golden
  may be introduced by this phase** — including any integer digest derived from float bits; that
  prohibition is a clause of FR-027a and is also why FR-029/SC-017 compare the committed tree
  semantically rather than byte-for-byte (C-8).

**Filesystem and metadata**

- Duplicate preset names across two categories would collide in the browser's flat list and in search
  (`preset_manager.cpp:123-147`); FR-005 forbids them library-wide, not per-directory.
- A category directory present on disk but absent from `subcategoryNames` yields presets with an **empty**
  subcategory that vanish from every tab except "All" (`preset_manager.cpp:95-103`,
  `getPresetsForSubcategory` at `:108-121`) — SC-001 and SC-007 both catch it, from opposite directions.
- An empty category directory is legal to the scanner but violates FR-004's ≥ 5 minimum.
- `Textures/.gitkeep` becomes redundant once real presets land; removing it is optional and must not be
  confused with removing the directory.

**Failure modes of the generator**

- A truncated or failed `getState()` would silently produce a short chunk; FR-006/SC-004's exact 2868-byte
  assertion is the tripwire.
- The generator's own `process()` call must be long enough for the parameter fan-out to complete; if a
  future refactor spreads parameter application across blocks, presets would encode stale values —
  SC-013's coverage matrix and SC-014's decode checks fail loudly if so.

---

## Existing Components

Every signature below was read from the header this session at the cited line.

| Component | Header / file | What is reused (verified) |
|---|---|---|
| `PresetManagerConfig` | `plugins/shared/src/preset/preset_manager_config.h:19-24` | `struct { FUID processorUID; std::string pluginName; std::string pluginCategoryDesc; std::vector<std::string> subcategoryNames; }` — field order is load-bearing for designated initializers (`:16-18`). |
| `makeSeraphisPresetConfig()` | `plugins/seraphis/src/preset/seraphis_preset_config.h:24-31` | Returns the config with `subcategoryNames = {"Textures"}`; **extended** by FR-001, never renamed (`:8-13`). |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h` | `PresetManager(PresetManagerConfig, IComponent*, IEditController*, path userDirOverride = {}, path factoryDirOverride = {})` (`:55-61`); `PresetList scanPresets()` (`:70`); `PresetList getPresetsForSubcategory(const std::string&) const` (`:75`); `static bool isValidPresetName(const std::string&)` (`:120`); `std::filesystem::path getFactoryPresetDirectory() const` (`:113`). |
| `PresetInfo` | `plugins/shared/src/preset/preset_info.h:15-33` | `{name, category, subcategory, path, isFactory, description, author}` + `isValid()`; the harness asserts on `subcategory` and `isFactory`. |
| Subcategory derivation | `plugins/shared/src/preset/preset_manager.cpp:72-106` | Parent-directory name matched **exactly** against `config_.subcategoryNames`; no match ⇒ empty subcategory. The Membrum lesson, mechanized by SC-007. |
| Metadata writer (reference form) | `plugins/shared/src/preset/preset_manager.cpp:263-276` | The XML attribute set the generator's `Info` chunk must match: `MediaType`, `PlugInName`, `PlugInCategory`, `Name`, `MusicalCategory`, `MusicalInstrument`, optional `Comment`. |
| Metadata reader | `plugins/shared/src/preset/preset_manager.cpp` (`readMetadata`) | **Stub** — `// TODO: Implement XML metadata reading; return true`. Out of scope (Non-goals); the harness parses the XML itself. |
| `Platform::getFactoryPresetDirectory` | `plugins/shared/src/platform/preset_paths.h:21-27` | `std::filesystem::path getFactoryPresetDirectory(const std::string& pluginName)` — Windows `%PROGRAMDATA%\Krate Audio\{pluginName}`; the destination SC-019 checks. |
| `krate_plugin_install_presets` | `cmake/KratePlugin.cmake:287-311` | POST_BUILD `copy_directory resources/presets → $ENV{PROGRAMDATA}/Krate Audio/<target>`; Windows-only (`:290-292`). Already called at `plugins/seraphis/CMakeLists.txt:113`. |
| `Processor::getState` / `setState` | `plugins/seraphis/src/processor/processor.cpp:1856-1917` / `:1727-1834` | The **only** state serializer/deserializer; 2868-byte layout documented at `:1836-1843`; version refusal for `version > kCurrentStateVersion` at `:1742-1744`. |
| Parameter-pack save/load | `plugins/seraphis/src/parameters/*.h` | `saveGlobalParams` (`global_params.h:211`), `saveGlobalSeed` (`:273`), `saveMacroParams` (`macro_params.h:123`), `saveCloudParams` (`cloud_params.h:286`), `saveMorphParams` (`morph_params.h:346`), `saveSpectralPayloads(const std::array<Krate::DSP::SpectralState,4>&, IBStreamer&)` (`morph_params.h:380`), `saveLifeModParams` (`life_mod_params.h:282`), `saveBodyParams` (`body_params.h:350`), `saveAtmosphereParams` (`atmosphere_params.h:353`), `saveAetherParams` (`aether_params.h:274`), `saveEffectsParams` (`effects_params.h:417`). Reached through `getState()`, never called directly by the generator. **The mirror `load*Params(Params&, IBStreamer&)` free functions** (`global_params.h:218`, `aether_params.h:297`, `life_mod_params.h:298`, and the remaining packs) **are** called directly — by FR-025a's decode mechanism, in the test binary, in `getState()` order — never through `Processor::setState()`. |
| `savePartialOverrides` | `plugins/seraphis/src/processor/processor.cpp:450-457` | 272-byte `[partials]` block: 64 `writeFloat` + two `writeInt64u`; layout table at `:420-425`. |
| `serializeSpectralState` | `dsp/include/krate/dsp/processors/spectral_state.h:238` | `std::size_t serializeSpectralState(const SpectralState&, std::byte* dest, std::size_t capacity)`; `kSpectralStateBytes == 541` (`:185-186`); `isValidSpectralState` (`:82`); `SpectralStateId{SineStack, Bell, Choir, Glass, Breath}` (`:313`), `kSpectralStateCount == 5` (`:315`). |
| Dropdown label tables | `plugins/seraphis/src/parameters/dropdown_mappings.h` | `kSeedValues` / `kSeedLabels` (16 each, `:93`, `:104`), `kTravelModeLabels` (`:118`), `kStateCountLabels` (`:165`), `kSpectralStateLabels` (`:178`), `kEnvelopeModeLabels` (`:190`), `kBodyMaterialLabels` (`:198`), `kGrainEnvelopeLabels` (`:211`). The C-2 coverage matrix is written against these. |
| `Controller` preset wiring | `plugins/seraphis/src/controller/controller.cpp:156-157`, `:444-475` | `PresetManager` constructed from `makeSeraphisPresetConfig()`; tabs built as `{"All"} ∪ subcategoryNames` (`:462-469`) then handed to `PresetBrowserView` (`:471-472`). |
| `CategoryTabBar` | `plugins/shared/src/ui/category_tab_bar.cpp:25-36` | Vertical tab layout at `height / numTabs` — why seven categories are safe (Ruinae ships fifteen). |
| Seraphis test fixture | `plugins/seraphis/tests/seraphis_test_fixture.h:177-212`, `:225` | `initialize(nullptr)` → `setupProcessing` → `setActive(true)`; `setParamPoints(ParamID, std::initializer_list<double>)`; documented warm-buffer precondition for `AllocationScope` (`:13-19`). Model for both the sweep and the generator's drive sequence (C-4). |
| `render_fingerprint.h` | `tests/test_helpers/render_fingerprint.h:46-101` | `kRenderCheckpoints = 32`, `kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`, `RenderFingerprint{rms, peak, meanAbs, totalVariation, checkpoints}`, `fingerprintRender(std::span<const float>)`, `compareFingerprints(...)`. The **only** sanctioned reproducibility mechanism (C-7). |
| `AllocationScope` | `tests/test_helpers/allocation_detector.h:75-95` (`namespace TestHelpers`, `:19`) | RAII wrapper over `AllocationDetector::instance()` — a **process-global singleton** (`:60-63`) holding one `std::atomic<bool> tracking_` and one `std::atomic<size_t> allocationCount_` (`:66-67`), with **no thread filter** anywhere. Sufficient for FR-028/SC-015's quiescent arm; **structurally unable** to attribute an allocation to a thread, which is why FR-028a/SC-015a names a different instrument. |
| Aether decay is an **RT60** | `dsp/include/krate/dsp/effects/aether_reverb.h:3139-3145` | `const float t60dc = decaySeconds;` then `gDC = pow(10, -3·m/(t60dc·sr))` — the stored seconds value *is* the RT60 that sets the Jot feedback gains. This is the fact that makes C-6.3's "decay ≥ 30 s ⇒ energy-conserving" classification impossible and its RT60-consistency arm correct. |
| Envelope timing constants | `plugins/seraphis/src/parameters/life_mod_params.h:56-58`, `:65-68`; `dsp/include/krate/dsp/processors/multi_stage_envelope.h:65` | `kEnvStageTimeMinMs = 1.0`, `kEnvStageTimeMaxMs = MultiStageEnvelope::kMaxStageTimeMs = 10000.0f`; defaults `kEnvGrowthDurationDefault = 10.0` s, `kEnvStage0MsDefault = 2000.0`, `kEnvStage1MsDefault = 4000.0`, `kEnvReleaseMsDefault = 8000.0`. The source of C-6.1's `A` and `Rel`, and the reason SC-009's window cannot open at t = 0. |
| `logMapFromNormalized` | `plugins/shared/src/ui/parameter_helpers.h:80-83` | `std::clamp(mn * std::pow(mx / mn, clamped), mn, mx)` — the denormalizer behind Aether decay (`aether_params.h:111-116`) and the other log-scaled IDs. `std::pow` is not correctly-rounded ⇒ the stored floats are **not toolchain-invariant** ⇒ C-8 / FR-029 compare semantically. |
| `makeFactoryState` | `dsp/include/krate/dsp/processors/spectral_state.h:164-170`, `:365-375` | Normalizes with `1.0f / std::sqrt(sumSquares)` (`:170`); its banner states it *"evaluates ~200 `std::pow`/`std::exp` calls"*. Its outputs are `memcpy`'d as raw bit patterns into the four 541-byte payloads (`:238-260`, layout `:218-235`) — the second reason C-8's byte-equality premise is false. |
| Preset scan order | `plugins/shared/src/preset/preset_manager.cpp:37-56` | `scanPresets()` scans `getUserPresetDirectory()` **first** (`:41-44`), then the factory directory (`:46-50`). Why FR-022 must pass `userDirOverride`, not only `factoryDirOverride`. |
| CI legs that run `seraphis_tests` | `.github/workflows/ci.yml:271`, `:322` (Windows), `:525`, `:582` (macOS), `:947`, `:995` (Linux); bit-exact lint at `:162-166` | Three toolchains run the same suite, which is why FR-029 must be toolchain-stable and why SC-017 is stated per-leg. |
| Release artifact toolchain | `.github/workflows/release.yml:100-102` (`runs-on: ubuntu-latest`), `:158-174` (build + run the generator), `:201-224` (Windows job packages the **downloaded** artifact) | The shipped preset bytes are **Linux/GCC**-generated while the committed tree is Windows/MSVC-generated — C-8's first reason. |
| Limiter ceiling constants | `plugins/seraphis/tests/integration/effects_chain_test.cpp:332`, `:854`; `dsp/include/krate/dsp/processors/true_peak_limiter.h:46` | `kLimiterCeilingLin = 0.8912509f`, `kCeilingAllowanceDb = 0.1f`, from `kDefaultCeilingDb = -1.0f`. Reused verbatim by SC-010. |
| Membrum preset harness patterns | `plugins/membrum/tests/unit/preset/test_factory_kit_presets.cpp:1-13`, `:54-70`; `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:1-19`, `:54-57` | Walk-up-to-find-`resources` helper, VST3 container parse, and the infinite-ring render pattern C-6 adapts. |
| Membrum generator's `Info` chunk | `tools/membrum_preset_generator.cpp:349-400` | `buildInfoXml(name, subcat)` + two-entry `List` (`Comp` + `Info`) writer — the exact container form FR-003 requires. Ruinae's writer omits `Info` (`tools/ruinae_preset_generator.cpp:42-73`) and is **not** the model. |
| Generator CMake pattern | root `CMakeLists.txt:499-590` | `add_executable(<p>_preset_generator tools/<p>_preset_generator.cpp)` + `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"` + `add_custom_target(generate_<p>_presets COMMAND … "${CMAKE_SOURCE_DIR}/plugins/<p>/resources/presets")`. |
| **`gen_v2_fixtures`** — the FR-013/FR-015 precedent | root `CMakeLists.txt:611-649` | **The model for this phase's generator.** It compiles Gradus's and Ruinae's real `processor.cpp` **directly as executable sources** (`:623-630`) and links only `KrateDSP` + `KratePluginsShared` + `sdk` (`:644-649`) — **zero `vstgui_support`**, which is exactly what FR-015 requires. Its real cost, which the planner must budget for: it must also compile the VST3-SDK boilerplate TUs `memorystream.cpp`, `vstpresetfile.cpp`, `hosting/hostclasses.cpp`, `hosting/pluginterfacesupport.cpp`, `main/moduleinit.cpp`, `main/pluginfactory.cpp` (`:632-638`) plus a `GetPluginFactory` stub (`:641`). |
| `GetPluginFactory` stub (Seraphis's own) | `plugins/seraphis/tests/vstgui_test_stubs.cpp:12` | `Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }` — already in-tree and already used by `seraphis_tests` (`tests/CMakeLists.txt:75-78`, which lists the same four SDK boilerplate TUs). The generator can reuse this file rather than adding a second stub. |
| Seraphis has **no** static core library | `plugins/seraphis/CMakeLists.txt:18-67` | All of `processor.cpp`, `controller.cpp` and `src/ui/*.cpp` are listed inside **one** `smtg_add_vst3plugin(${PLUGIN_NAME} …)` call, which the SDK builds as a CMake `MODULE` (`add_library(${target} MODULE "${SOURCES}")`, `extern/vst3sdk/cmake/modules/SMTG_AddVST3Library.cmake:170`, reached via `:249`). There is nothing for a separate executable to link against, so the generator **must** compile `processor.cpp` as its own source — the `gen_v2_fixtures` shape, not the `krate-render` shape. |
| `krate-render` (**not** the FR-015 precedent) | `tools/krate-render/CMakeLists.txt:13-29`; `plugins/membrum/CMakeLists.txt:154-160` | Recorded so it is not mistaken for one. It links `membrum_core` + `membrum_dsp` + `membrum_preset_io` + `KrateDSP` + `KratePluginsShared` + **`vstgui_support`** + `sdk` (`:17-29`) — it needs `vstgui_support` (the dependency **FR-015 forbids**) precisely because `membrum_core` is Membrum's STATIC library bundling processor + controller + UI + voice-pool TUs together (`plugins/membrum/CMakeLists.txt:153-160`, banner: *"STATIC library of the processor/controller/UI/voice-pool TUs"*). Seraphis has no such library (row above). Its `vstgui_support`-before-`sdk` link-order comment (`:24-26`) is a **GNU-ld rule for targets that link `vstgui_support` at all**, and is therefore *not* applicable to FR-015's VSTGUI-free generator. |
| `FUID::toString` | `extern/vst3sdk/pluginterfaces/base/funknown.h:295` (doc at `:285-294`) | `void toString(char8* string) const` → 32-char uppercase hex into a `char8[33]`. Source of the `.vstpreset` class id (C-3). |
| `release.yml` generate-presets job | `.github/workflows/release.yml:98-180`, `:201-224` | The fixed generator contract: target `${PLUGIN}_preset_generator` (`:158-166`), build (`:168-169`), `./build/bin/<binary> generated-presets` (`:170-174`), artifact with `if-no-files-found: error` (`:180`), Windows packaging copies the artifact (`:223-224`). |
| Release roster files | `.claude/workflows/release-readiness.js:14-21` (`PLUGIN_MAP`, ends at `membrum`; unknown names are filtered out at `:26-29`); `.claude/skills/release/SKILL.md:15` (input list) and `:22-29` (target/bundle table) | Both **omit Seraphis** today — FR-031 / FR-032. |
| Changelog coverage tool | `tools/check-changelog-coverage.js:50` | `PLUGINS` already includes `'seraphis'`; no change needed, only a green run (SC-023). |
| Test registration | `plugins/seraphis/tests/CMakeLists.txt:5-80`, `:120-175` | Enumerated (never globbed) TU list; `-fno-fast-math -fno-finite-math-only` block for TUs that inject non-finite values or measure per-sample step statistics; `catch_discover_tests(seraphis_tests REPORTER console)`. |

---

## New Components

No new DSP class is created at any layer. The phase adds one tool, two test TUs and a handful of
file-local helpers.

| Name | Kind / layer | Path | ODR sweep result |
|---|---|---|---|
| `seraphis_preset_generator` | CMake executable target (build tooling — no DSP layer) | `tools/seraphis_preset_generator.cpp` | `grep -rn "seraphis_preset_generator" .` → **0 hits**: the name exists today only as a *string `release.yml` constructs at run time* (`${PLUGIN}_preset_generator`, `:158-166`), never as a literal. No existing target of that name; no collision — and the constructed form is why the name is not negotiable (FR-010). |
| `generate_seraphis_presets` | CMake custom target | root `CMakeLists.txt` | No such target exists (`grep -n "generate_.*_presets" CMakeLists.txt` → disrumpo/ruinae/innexus/gradus/membrum only). |
| `SeraphisPresetDef` + `kSeraphisPresetDefs` | data-only header, `namespace Seraphis::PresetDefs` (build tooling — no DSP layer) | `tools/seraphis_preset_defs.h` | **New in this revision (FR-016a).** Included by **both** `tools/seraphis_preset_generator.cpp` and `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp`, so FR-029 can regenerate chunks in-process on every CI leg. ODR sweep run this session: `grep -rn "SeraphisPresetDef\|seraphis_preset_defs\|PresetDefs" dsp/ plugins/ tools/` → **0 hits**. Deliberately **not** named `PresetDef` (5 existing tool-local hits, below) — a header shared across two link units must not reuse a name that five anonymous-namespace structs already carry. Contains names/categories/descriptions, `{ParamID, normalizedValue}` pairs and the optional FR-024a audition-stimulus override (`{pitch, velocity}`) — **no state layout, no serialization, no `EditMessage` list** (C-3 stands; OQ-4 ratified no). |
| `PresetDef` | **NOT CREATED** — superseded by `SeraphisPresetDef` above | — | `grep -rn "struct PresetDef\|class PresetDef" dsp/ plugins/ tools/` → **5 hits**, all in `tools/*_preset_generator.cpp`, each in a **separate executable**: `disrumpo:481`, `gradus:84`, `innexus:76`, `preset_generator:251`, `ruinae:80`. Those are safe only because no two share a TU or a link unit. Seraphis's definitions are shared between the generator and `seraphis_tests`, so the anonymous-namespace trick is unavailable and the distinct name is mandatory. |
| `writeVstPreset` / `buildInfoXml` | file-local free functions in the generator TU | `tools/seraphis_preset_generator.cpp` | Same-name functions exist in `tools/membrum_preset_generator.cpp:349`, `:363` and `tools/ruinae_preset_generator.cpp:43` — again separate executables, no linkage. Must be `static`/anonymous-namespace. |
| `SeraphisFormat` (namespace) | **NOT CREATED** (C-3) | — | `grep -rn "namespace SeraphisFormat\|SeraphisPresetFormat" dsp/ plugins/ tools/` → **0 hits**. Recorded so a later contributor does not "restore consistency" with Ruinae by adding one; the absence is deliberate. |
| `BinaryWriter` | **NOT CREATED** (C-3) | — | `grep -rn "class BinaryWriter\|struct BinaryWriter" dsp/ plugins/ tools/` → **5 hits**, all in the duplicate-layout generators (`disrumpo_preset_generator.cpp:26`, `gradus_preset_format.h:24`, `innexus_preset_format.h:24`, `preset_generator.cpp:20`, `ruinae_preset_format.h:25`). Seraphis writes through `IBStreamer` inside the shipped `getState()`, so none is needed. |
| `factory_preset_test.cpp` | new test TU (unit) | `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp` | New path; `plugins/seraphis/tests/unit/preset/` does not exist yet (`ls plugins/seraphis/tests/unit`). Must be added to the **enumerated** list in `tests/CMakeLists.txt`. |
| `check-preset-generator-determinism.js` | Node CLI (FR-035a) | `tools/check-preset-generator-determinism.js` | New file; `ls tools/ \| grep -i determin` → **0 hits**. Node, not Python, per the project rule. It is the automated replacement for SC-016's former "scripted double-run in the phase's verification log". |
| `preset_render_sweep_test.cpp` | new test TU (integration) | `plugins/seraphis/tests/integration/preset_render_sweep_test.cpp` | New name; no file of that name exists under `plugins/`. Must be added to the enumerated list, **and** to the `-fno-fast-math -fno-finite-math-only` block (it performs bit-pattern non-finite checks and per-second RMS statistics), following the rule at `tests/CMakeLists.txt:120-175`. |

**ODR sweeps run this session and their results, verbatim:**

```
grep -rn "class BinaryWriter|struct BinaryWriter" dsp/ plugins/ tools/   -> 5 hits, all tool-local
grep -rn "struct PresetDef|class PresetDef"       dsp/ plugins/ tools/   -> 5 hits, all tool-local
grep -rn "namespace SeraphisFormat|SeraphisPresetFormat" dsp/ plugins/ tools/ -> 0 hits
grep -rn "class SeraphisPresetGenerator|struct SeraphisPresetDef|class PresetSweep|
          struct PresetSweep|class FactoryPreset|struct FactoryPreset" dsp/ plugins/ tools/ -> 0 hits
grep -rn "SeraphisPresetDef|seraphis_preset_defs|PresetDefs" dsp/ plugins/ tools/   -> 0 hits
```

No new class enters `dsp/`, so `tools/lint-layers.js` and the SIMD-alignment lint have nothing new to
police in this phase.

---

## Open Questions

Only where the roadmap explicitly defers the decision to this spec. Each carries this spec's proposed
answer; the phase owner ratifies or overrides before planning.

- **OQ-1 — The category set (roadmap line 405–406 and line 609 both defer it here).**
  **RESOLVED (2026-08-04 Clarifications, Q5 / CQ-5 Option A) — adopted as proposed.** Proposed:
  **Textures · Pads · Drones · Bells · Choirs · Motion · Cinematic** (C-1). The only hard constraints are
  that `Textures` survives verbatim and that the list is additive-only forever after.
- **OQ-2 — Library size (roadmap says "factory preset library", no count).**
  **RESOLVED (2026-08-04 Clarifications, Q5 / CQ-5 Option A) — adopted as proposed.** **42 = 7 × 6**,
  minimum 5 per category (C-2), with the coverage matrix as the real floor.
- **OQ-3 — The release version (roadmap: "version.json/CHANGELOG", no number).**
  **RESOLVED (2026-08-04 Clarifications, Q7 / CQ-7 Option B) — 0.5.0 now.** Current is **0.4.0**
  (`plugins/seraphis/version.json`); this phase bumps it to **0.5.0** (FR-033), with the release verdict
  recorded explicitly as `DEFERRED` until Phase 11.5 is green (FR-030). **1.0.0** is reserved for the
  release that includes Phase 13 (per-note expression).
- **OQ-4 — Do factory presets exercise the Phase 11 `[partials]` override block?**
  **RESOLVED (2026-08-04 Clarifications, Q4 / CQ-4 Option A) — no.** Every preset carries the 272-byte
  block (FR-006), but ships it with both masks all-zero and all 64 pans 0.0f, asserted by the harness
  (FR-006a). The generator has no `IMessage`/`HostApplication`/`notify()` drive surface (FR-016).

---

## Review notes

Every issue raised in the 2026-08-04 review is applied above, with two partial rejections recorded here.
Nothing was resolved by relaxing a threshold; the two thresholds that moved got **stricter** (SC-012's
observation window 20 s → 60 s, restoring roadmap line 282's duration; SC-011's `min(…, 20 dB)` cap keeps
the original 20 dB bound for every preset with RT60 ≤ 6 s).

**Partially rejected — the sweep is ONE render per preset per rate, not two.** The review's two blockers on
the FR-024/FR-026 note-lifecycle contradiction proposed different repairs: an explicit single timeline
(NoteOn → hold → NoteOff → tail) and a split into two renders (NoteOn-only, plus NoteOn→NoteOff). Both
remove the contradiction; the single timeline is adopted (C-6.1) and the two-render form is rejected on
cost. The reason the split was proposed — that a NoteOn-only render is where the roadmap's stuck-note
runaway condition lives — is preserved in full: the hold interval `[0, H]` carries **no NoteOff**, and
FR-025's bounded arm is asserted over the whole render including that hold, which is exactly what the
two-render form's first render would have asserted. What the single timeline gives up is the ability to
observe a *tail* while the note is still held; the review's own note that "a non-sustaining preset held
indefinitely is expected to plateau, not decay" is why that observation would have had no threshold to
assert anyway. The saved render is what buys C-9's stated wall-clock budget on three CI legs.

**Partially rejected — FR-028a's concurrency instrument is left as a plan choice.** The review suggested an
audio-thread-scoped allocation counter *or* a TSAN/valgrind lane. Both are admitted rather than one being
fixed here, because the first requires an additive change to `tests/test_helpers/allocation_detector.h`
that six other plugins consume, and whether that is cheaper than tagging one TU into the existing
valgrind-linux lane is a plan-level judgement. What is **not** left open, and is now normative: asserting
the concurrent arm with the unfiltered global `AllocationScope` is forbidden (FR-028a), and the Edge Cases
paragraph no longer claims FR-028 exercises the interleaving.

**Accepted with an explicit decision, not a workaround — C-8's toolchain problem.** The review offered two
exits: make `release.yml` package `resources/presets`, or accept per-toolchain ULP differences and say so.
The second is taken and recorded as a decision in C-8, with the first considered and rejected in-line
(packaging the committed tree would make the shipped library the *unbuilt* one and would discard the
"the generator still builds and runs on Linux" signal `release.yml:168-174` provides). FR-029's semantic
comparison then runs on all three CI legs, which is a stronger statement about the Linux-generated
installer than the original Windows-only byte check could have made.

**Requirement/criterion numbering.** New items are appended, never renumbered, so downstream stages'
existing references stay valid: FR-006a, FR-008a, FR-016a, FR-024a, FR-025a, FR-027a, FR-027b, FR-028a,
FR-029a, FR-034a, FR-035a, SC-010a, SC-014a, SC-015a, SC-026, SC-027, SC-028, SC-029. FR-027a carries the
render-reproducibility requirement the review filed as "FR-029a"; it sits next to the sweep requirements
it constrains rather than next to the committed-tree requirement. The 2026-08-04 Clarifications session
(see `## Clarifications` above) appended the rest: FR-008a, FR-024a, FR-025a, FR-027b, FR-029a, FR-034a,
SC-010a, SC-014a, SC-028, SC-029.
