# plugins/seraphis/ — Seraphis Spectral-Organism Synthesizer

Auto-loads when working under `plugins/seraphis/`. Root `CLAUDE.md` still applies.

- **Type:** spectral-organism synthesizer instrument (AU `aumu`, subtype `Srph`, manufacturer `KrAt`,
  bundle base `com.krateaudio.seraphis`). **Version:** see `version.json`.
- **Roadmap:** `specs/Seraphis-roadmap.md` — the plugin wrapper starts at Phase 8; the DSP it drives is
  Phases 1–7 in `dsp/include/krate/dsp/` (`SeraphisEngine`, `SeraphisVoice`, `SeraphisMacroMatrix`,
  `AetherReverb`). **No DSP lives in this plugin** — `src/engine/` holds prepare-time *config* only.
- **src skeleton:** `controller/ engine/ parameters/ preset/ processor/ ui/ update/`
  — `engine/` = `seraphis_engine_config.h` (thin prepare-time config factories, no DSP);
  `ui/` is **populated as of Phase 11** — see *The editor* below; `preset/` and `update/` are the
  shared-config adapters (`makeSeraphisPresetConfig()` / `makeSeraphisUpdateConfig()`).
  Generated, never hand-edited, never committed (see `.gitignore`): `src/version.h`,
  `resources/win32resource.rc`, `resources/auv3/audiounitconfig.h` — only `audiounitconfig.h.in` is authored.
- **Buses:** 1 event input, 1 stereo audio output, **no `addAudioInput()`**. `kSupportedNumChannels` is
  `02` and `au-info.plist` declares exactly `Inputs 0 / Outputs 2`; a mismatch between the bus layout and
  those two files is the documented AU `-10875` init failure.
- **Param IDs:** flat base 0 with 100-ID section gaps. The **reserved map is load-bearing** — a later phase
  must claim its own band, never squat in another:

  | Range | Section | Phase |
  |---|---|---|
  | 0–99 | Global (master gain, polyphony, soft limit; `kSeedId` added in Phase 9) | 8 — shipped |
  | 100–199 | Macros (Dream, Bloom, Dissolve, Gravity, Entropy) | 8 — shipped; wired, no longer inert, in 9 |
  | 200–399 | Harmonic Cloud | 9 — shipped |
  | 400–599 | Spectral Morph / Entropy | 9 — shipped |
  | 600–799 | Life Modulators (600–699 orbit/width, 700–799 voice envelope — one pack, one band) | 9 — shipped |
  | 800–999 | Continuous Body | 9 — shipped |
  | 1000–1199 | Atmosphere | 9 — shipped |
  | 1200–1399 | Aether | 9 — shipped |
  | 1400+ | Effects | 10 — shipped |

  Pipeline to add one: `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.
  State version lives in `plugin_ids.h` (`kCurrentStateVersion`), shared by processor and controller with
  no cross-include. **Editor session state is NOT a parameter** — it uses the `9000+` session-tag
  namespace instead; see *The editor* below.
- **Tests:** `seraphis_tests`.
  ```bash
  build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`

## The editor (`src/ui/`, Phase 11) — organism-first, three custom views

`ui/` stopped being empty in Phase 11. Spec: `specs/seraphis-phase11-ui/`. The editor is a single
**1000 × 700** template whose whole area is the live partial constellation; the macros orbit it and the
deep parameters live in a pull-up drawer along the bottom edge.

### The custom-view roster is CLOSED — exactly three `CView` classes

| File | Class | Base | Registered by |
|---|---|---|---|
| `ui/cloud_view.{h,cpp}` | `CloudView` | `VSTGUI::CView` | `createCustomView` (controller) |
| `ui/macro_ring_knob.h` | `MacroRingKnob` | `Krate::Plugins::ArcKnob` | `ViewCreatorAdapter`, five instances (IDs 100–104) |
| `ui/drawer_container.{h,cpp}` | `DrawerContainer` | `VSTGUI::CViewContainer` | `createCustomView` (controller) |

`ui/edit_sub_controller.{h,cpp}` holds **`SeraphisEditSubController`, which is NOT a view** — it is a
`VSTGUI::DelegationController` bound from the template **root** by `sub-controller="SeraphisEdit"`, so
every view in the document (including the header preset button) is inside its sub-tree. Tagged controls
are untouched; it only claims the tag-less ones.

Everything else in the drawer is a **plain uidesc control** — `ArcKnob`, `CSlider`, `COptionMenu`,
`ToggleButton`, plus the two `IconSegmentButton` bars, and nothing else (2026-08-04 consistency-pass
amendment: `CCheckBox`/`CTextButton` appear nowhere; toggles are the shared `ToggleButton`, the tab and
slot rows are shared `IconSegmentButton`s, the preset button is a shared `OutlineBrowserButton` via
`createCustomView("PresetButton")`). Adding a fourth NEW view class is a spec amendment, not a refactor.

`MacroRingKnob` uses a `ViewCreatorAdapter` (not `createCustomView`) because it must accept `control-tag`
and the rest of the `CControl` attributes from the uidesc. Its creator is an inline global, and an inline
global only runs its constructor in a **linked** TU — which is why `src/entry.cpp` includes
`ui/macro_ring_knob.h`, `<ui/arc_knob.h>`, `<ui/toggle_button.h>` and `<ui/icon_segment_button.h>`. The
Phase 8 "entry.cpp must include no `ui/*.h`" banner is gone; do not restore it.

**The drawer is never a `UIViewSwitchContainer`.** All seven pages (Cloud, Morph, Body, Atmos, Aether, FX,
Life/Env) exist in the XML at once with exactly one visible; a view switch realises only the active
template and would make six tabs' worth of ParamIDs look unreachable. `DrawerContainer` also **must be a
direct child of the template root** — its two rects (`kCollapsedRect`, `kOpenRect`) are absolute window
coordinates, and nesting it one level deeper puts the collapsed strip below the window.

### Cloud-frame data path — one-way, audio thread → UI

```
audio thread            Processor::publishCloudFrame()      ONCE per process() call
                        (last thing after the slice loop, gated by cloudFrameEnabled_)
   │  CloudFrame, 808 B POD  (src/processor/cloud_frame.h, sizeof pinned by static_assert)
   ▼
DataExchange queue      blockSize = sizeof(CloudFrame), numBlocks = 4, alignment = 32,
                        userContextID = kCloudFrameUserContextId ('SCLD')
   │  opened in Processor::connect(), closed in disconnect(); onActivate/onDeactivate in setActive()
   ▼
UI thread               Controller::onDataExchangeBlocksReceived() → cachedCloudFrame_
                        MOST RECENT WINS — every block in a delivery overwrites the cache, no backlog.
                        queueOpened() answers dispatchOnBackgroundThread = false, so the cache is
                        written and read on ONE thread and needs no lock.
   ▼
CloudView               33 ms (~30 Hz) CVSTGUITimer; invalidates ONLY when frame.sequence moved.
```

- This is the **Membrum `MetersBlock` piggyback pattern** — one queue, no new transports. Do not add a
  second queue for anything the frame can carry.
- `cloud_frame.h` lives under `src/processor/` but is included by the **controller**. That is the
  sanctioned shared-POD exception (Membrum's `meters_block.h` is the precedent): it includes only
  `<cstdint>` and names no processor type. **Field order is normative** — it is what produces the 808.
- `CloudFrame::frequencyHz[]` is **drift-inclusive**; `fundamentalHz` is the **undetuned** f0 (from
  `HarmonicCloud::getFundamentalHz()`). Never derive a stored ratio from `frequencyHz[0]`.
- `CloudFrame::maskBits` uses the **plugin** sense: bit *i* set ⇔ partial *i* is **masked**. That is the
  inverse of `HarmonicCloud::setPartialMask(index, active)`, whose body is `masked_[index] = !active`.
  The inversion happens **once**, on the processor side. Getting this backwards silences the wrong
  partials and still passes a naive test.
- Hosts without the native DataExchange API deliver blocks as `IMessage`; `Controller::notify()` runs them
  through `dataExchangeReceiver_.onMessage()` first, then falls through to `EditControllerEx1`.

### Edit channel — the other direction, and it is NOT the same transport

```
UI thread     CloudView / SeraphisEditSubController gesture
   │          throttled to one message per 33 ms, with an UNCONDITIONAL terminal flush on gesture end
   ▼
IMessage      id "SeraphisEdit", one binary attribute "payload" = 12-byte Seraphis::UI::EditMessage
   ▼
message thread  Processor::notify() → applyEditMessage()
   │            Untrusted input: unknown kind, out-of-range slot/index, wrong payload size and
   │            non-finite floats are DROPPED SILENTLY (kResultOk). The Layer 2 mutators' own
   │            rejection is the second line of defence, never the first.
   ├─ kinds 1/4/5 (ratio+amp, blend, tilt) → stageSlotEdit() → the Phase 9 three-buffer staging ring
   │                                          → spectralSlotsHandoff_ (release) → audio thread
   └─ kinds 2/3 (pan, mask)                → atomic override table + partialOverridesPending_ (release)
                                              → audio thread does the fan-out
   ▼
audio thread  process(): partialOverridesPending_.exchange(acquire) → repushPartialOverrides()
              → SeraphisEngine::setPartialPositionAllVoices / setPartialMaskAllVoices / clearPartialMaskAllVoices
```

- **The per-partial fan-outs are audio-thread-only.** They write `HarmonicCloud`'s pan/mask state, which
  `process()` also reads and writes. Calling them from the message thread is a data race — that is the
  whole reason kinds 2/3 stage rather than call.
- `EditMessage` kind 0 is the **editor gate** (`a = 1` open / `a = 0` close), sent by the controller's
  open-editor refcount **only on a 0→1 / 1→0 transition** — never once per view. It is what opens
  `cloudFrameEnabled_`; with no editor open the producer costs one atomic load per block.
- **`src/ui/edit_message.h` is the ONLY header under `src/ui/` the processor may include.** It is the
  wire format: a POD plus three `constexpr` strings, `<cstdint>` and nothing else, no VSTGUI type. Any
  other `ui/*.h` in a processor TU is a Processor/Controller separation violation.
- The controller keeps a **display-only `slotMirror_`** of the four `SpectralState`s, mutated with the
  *same* Layer 2 functions the processor will apply. The local write **never substitutes for the send**;
  the mirror re-seeds from the 409–412 dropdowns and from the state stream.

### Session tags — `9000+`, and never a `ParamID`

A tag-less uidesc control carries a **custom `session-tag="<name>"` attribute** (the view factory ignores
it, `UIAttributes` preserves it); `SeraphisEditSubController::verifyView()` turns it into
`setTag()` + `setListener(this)`. VSTGUI installs a listener only when `control-tag` is present, so this
is how the mode toggle, drawer handle, tabs, slot buttons, Blend, Tilt and the preset button get one
without burning a registered parameter.

| Tag | Control |
|---|---|
| 9000 | header preset button (shared `OutlineBrowserButton` via `createCustomView("PresetButton")`) |
| 9001 | Obs \| Edit mode toggle (`ToggleButton`) |
| 9002 | drawer pull-up handle (`ToggleButton`, chevron) |
| 9003 | Blend A→B slider |
| 9004 | Tilt dB/oct |
| 9100 | the seven-tab drawer bar — ONE `IconSegmentButton`, session-tag `tabs`; index = `round(v·6)` |
| 9200 | the four-slot morph bar — ONE `IconSegmentButton`, session-tag `slots`; index = `round(v·3)` |

The registered surface tops out at 1443, so a session tag can never be mistaken for a `ParamID` and is
never counted as a parameter binding. **Never write one as `control-tag`.**

### Per-partial editing state is serialized, and the format version did NOT move

Drawer/mode/selection and the per-partial override table are session state, not parameters. The override
table is written as a **272-byte `[partials]` block** (64 pan floats + two 64-bit masks), **appended last**
after `[effects]`. `kCurrentStateVersion` stays **3**: every older stream remains a strict byte prefix and
the EOF-safe loader chain migrates with no version-aware branch. The controller's loader must consume the
same blocks in the same order even when it has nowhere to put them, or the next appended block reads from
the wrong offset.

### Teardown discipline

Every view pointer the controller caches (`activeEditor_`, `cloudView_`, `drawer_`, `macroRings_`,
`presetBrowserView_`) is **raw and non-owning**, and every one is zeroed in `willClose()`. The
sub-controller count resets there too — it is per open editor, not per plugin instance. `editorOpenCount_`
is the exception: it is reset in `terminate()`, never in `willClose()`, because `willClose()` fires once
per closing view. A cached pointer that outlives its frame is exactly the use-after-free the ASan /
valgrind lanes exist to catch.

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

### 1. MPE / note expression — **DECIDED in Phase 9 (RQ-2, 2026-08-01): it ships, in Phase 13**

Phase 8 posed this as Phase 9's call. **Phase 9 ruled**, and the ruling is neither "yes, here" nor "no":
per-note expression **is wanted and will ship**, but in a **new named phase — Phase 13, Per-Note
Expression** (`specs/Seraphis-roadmap.md` → Phase 13; `specs/seraphis-phase9-parameters/spec.md` →
*Resolved Questions* RQ-2, FR-064, FR-058 clause 5). "Deferred, owner unknown" was explicitly not an
available outcome.

**Why not Phase 9.** Phase 13 owns **both halves**, because neither works without the other: the DSP
half — per-voice expression inputs on `SeraphisVoice` — is exactly the kind of `dsp/` change Phase 9's
FR-071 froze, and the engine's note API is still `noteOn(note, velocity)` / `noteOff(note)` with no
per-note expression input at all, so an `INoteExpressionController` implemented today would have
nothing to drive.

**Status through Phase 9:** still **no `INoteExpressionController`**, still no declared
note-expression types (FR-064 is unconditional). The event-input bus is the whole note surface.

**The controller-FUID host-cache hazard is ACCEPTED, not open.** Adding an interface to an
already-released controller FUID can invalidate host-cached class metadata: hosts cache the interface
set they discovered for a class UID, so a class that suddenly answers `queryInterface` for
`INoteExpressionController` may be seen inconsistently until the host's cache is cleared — and users do
not clear plugin caches. Phase 9 weighed that and accepted it rather than pre-emptively burning a
second controller FUID. **Do not re-litigate this in Phase 13 as a fresh discovery, and do not
regenerate `kControllerUID` over it.**

### 2. Preset categories are **additive-only**

Phase 8 seeds exactly one category: **`Textures`**. It is a **seed, not a placeholder** — it is not to be
renamed, replaced or "cleaned up" when the real category set lands.

Phase 12 **EXTENDS** the list and **MUST NOT rename a shipped category.** A rename orphans every preset
ever saved against the old name: the user's presets still carry the old string and no longer resolve into
any category. This is the Membrum lesson (roadmap line 405) — Membrum's kit categories (`Acoustic`,
`Electronic`, `Percussive`, `Unnatural`) are fixed for exactly this reason.

The category name is carried in **two** places and they must **always agree**:

1. the filesystem subdirectory — `resources/presets/{Category}/`, and at runtime
   `C:\ProgramData\Krate Audio\Seraphis\{Category}\`;
2. the **XML metadata** inside each preset (and the `subcategoryNames` list in
   `preset/seraphis_preset_config.h`).

Changing one without the other produces presets that exist on disk but never appear in the browser.
Adding a category means adding it to **both**, in the same change.
