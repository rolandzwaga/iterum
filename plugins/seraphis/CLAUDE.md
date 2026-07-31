# plugins/seraphis/ — Seraphis Spectral-Organism Synthesizer

Auto-loads when working under `plugins/seraphis/`. Root `CLAUDE.md` still applies.

- **Type:** spectral-organism synthesizer instrument (AU `aumu`, subtype `Srph`, manufacturer `KrAt`,
  bundle base `com.krateaudio.seraphis`). **Version:** see `version.json`.
- **Roadmap:** `specs/Seraphis-roadmap.md` — the plugin wrapper starts at Phase 8; the DSP it drives is
  Phases 1–7 in `dsp/include/krate/dsp/` (`SeraphisEngine`, `SeraphisVoice`, `SeraphisMacroMatrix`,
  `AetherReverb`). **No DSP lives in this plugin** — `src/engine/` holds prepare-time *config* only.
- **src skeleton:** `controller/ engine/ parameters/ preset/ processor/ ui/ update/`
  — `engine/` = `seraphis_engine_config.h` (thin prepare-time config factories, no DSP);
  `ui/` is **empty until Phase 11** (`.gitkeep` only); `preset/` and `update/` are the shared-config
  adapters (`makeSeraphisPresetConfig()` / `makeSeraphisUpdateConfig()`).
  Generated, never hand-edited, never committed (see `.gitignore`): `src/version.h`,
  `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` — only `audiounitconfig.h.in` is authored.
- **Buses:** 1 event input, 1 stereo audio output, **no `addAudioInput()`**. `kSupportedNumChannels` is
  `02` and `au-info.plist` declares exactly `Inputs 0 / Outputs 2`; a mismatch between the bus layout and
  those two files is the documented AU `-10875` init failure.
- **Param IDs:** flat base 0 with 100-ID section gaps. The **reserved map is load-bearing** — a later phase
  must claim its own band, never squat in another:

  | Range | Section | Phase |
  |---|---|---|
  | 0–99 | Global (master gain, polyphony, soft limit) | 8 — shipped |
  | 100–199 | Macros (Dream, Bloom, Dissolve, Gravity, Entropy) | 8 — shipped, inert |
  | 200–399 | Harmonic Cloud | 9 |
  | 400–599 | Spectral Morph / Entropy | 9 |
  | 600–799 | Life Modulators | 9 |
  | 800–999 | Continuous Body | 9 |
  | 1000–1199 | Atmosphere | 9 |
  | 1200–1399 | Aether | 9 |
  | 1400+ | Effects | 10 |

  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
  State version lives in `plugin_ids.h` (`kCurrentStateVersion`), shared by processor and controller with
  no cross-include.
- **Tests:** `seraphis_tests`.
  ```bash
  build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`

## IMMUTABLE identity — the FUIDs

Both GUIDs were generated once for Phase 8 and are **IMMUTABLE**. Changing either one after release makes
every host treat the plugin as a different class: saved projects lose their instance, and preset/state
bindings referencing the processor UID are orphaned. Never regenerate, never "tidy", never reuse elsewhere.

| Class | Constant | Value (verbatim, as in `src/plugin_ids.h`) |
|---|---|---|
| Processor | `Seraphis::kProcessorUID` | `0xD13457BF, 0x55DC4576, 0xA26AF99B, 0x8873244D` |
| Controller | `Seraphis::kControllerUID` | `0x18FAB644, 0xBA15411A, 0x8F635433, 0x1FB8B7C5` |

`src/plugin_ids.h` is the code-of-record; this table is the **durable prose record** so the values survive
even if the header is ever regenerated from a template. If the two ever disagree, `plugin_ids.h` as shipped
in the released binary wins — and the disagreement is a bug to be investigated, not silently "fixed" by
editing the header.

## Decisions that outlive Phase 8

### 1. MPE / note expression is a known **Phase 9** decision

Phase 8 ships **no `INoteExpressionController`** and declares no note-expression types. The event-input
bus is the whole note surface, because the engine's note API is `noteOn(note, velocity)` /
`noteOff(note)` with no per-note expression input at all — an implemented controller would have nothing
to drive.

**The caveat that makes this a decision rather than an oversight:** adding an interface to an
**already-released** controller FUID can invalidate host-cached class metadata. Hosts cache the interface
set they discovered for a class UID; a class that suddenly answers `queryInterface` for
`INoteExpressionController` may be seen inconsistently until the host's cache is cleared, and users do not
clear plugin caches. So Phase 9 must make this call **knowingly**: either implement note expression before
Seraphis ships to users, or accept that adding it later means a host-cache hazard (and possibly a new
controller FUID). Do not discover this in Phase 9 — it is written here for that reason.

### 2. Preset categories are **additive-only**

Phase 8 seeds exactly one category: **`Textures`**. It is a **seed, not a placeholder** — it is not to be
renamed, replaced or "cleaned up" when the real category set lands.

Phase 12 **EXTENDS** the list and **MUST NOT rename a shipped category.** A rename orphans every preset
ever saved against the old name: the user's presets still carry the old string and no longer resolve into
any category. This is the Membrum lesson (roadmap line 388) — Membrum's kit categories (`Acoustic`,
`Electronic`, `Percussive`, `Unnatural`) are fixed for exactly this reason.

The category name is carried in **two** places and they must **always agree**:

1. the filesystem subdirectory — `resources/presets/{Category}/`, and at runtime
   `C:\ProgramData\Krate Audio\Seraphis\{Category}\`;
2. the **XML metadata** inside each preset (and the `subcategoryNames` list in
   `preset/seraphis_preset_config.h`).

Changing one without the other produces presets that exist on disk but never appear in the browser.
Adding a category means adding it to **both**, in the same change.
