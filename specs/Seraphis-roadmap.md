# Seraphis — Spectral Organism Synthesizer Roadmap

A phased, DSP-library-first plan for building Seraphis, the ethereal counterpart to Ruinae. Each phase is
sized to become one speckit spec (`/speckit.specify`), with its own tests and evaluation criteria. Phases
1–7 build and unit-test KrateDSP components; phases 8–12 assemble the plugin.

## Core Identity (decided)

> A spectral organism synthesizer that creates evolving harmonic clouds, shaped by resonance and slow
> autonomous movement.

**Category decision: spectral-evolution hybrid.** The additive Harmonic Cloud is the sound *source*, the
continuous resonant body is the *body*, but the identity of the instrument is the **spectral evolution +
life-modulator layer** that animates both. All three generators (cloud / body / granular atmosphere) ship.

Philosophical inversion of Ruinae:

| Ruinae | Seraphis |
|---|---|
| instability, chaos, aggression | emergence, resonance, weightlessness |
| complexity from nonlinearity | complexity from depth |
| fast envelopes, hard transients | slow autonomous evolution, no static state |
| distortion rack | integrated space engine |

## Architecture Overview

```
                    ┌───────────────────┐
                    │  Note / Gesture   │
                    └─────────┬─────────┘
                              ▼
┌─ Per Voice (8–16) ──────────────────────────────────────────┐
│                                                             │
│  ┌───────────────────────┐   ┌──────────────────────────┐   │
│  │ HARMONIC CLOUD        │   │ SPECTRAL EVOLUTION       │   │
│  │ 64-partial additive   │◄──┤ state morphing, entropy, │   │
│  │ bank w/ per-partial   │   │ harmonic drift/mutation  │   │
│  │ drift/pan/envelope    │   └──────────▲───────────────┘   │
│  └──────────┬────────────┘              │                   │
│             ▼                           │                   │
│  ┌───────────────────────┐   ┌──────────┴───────────────┐   │
│  │ RESONANT BODY         │   │ LIFE MODULATORS          │   │
│  │ continuous modal /    │◄──┤ breathing, tides, orbit, │   │
│  │ waveguide resonance   │   │ Brownian walks, splines  │   │
│  └──────────┬────────────┘   └──────────────────────────┘   │
│             ▼                                               │
│  ┌───────────────────────┐                                  │
│  │ GRANULAR ATMOSPHERE   │  (parallel layer, mixed in)      │
│  │ 50 ms – 30 s grains,  │                                  │
│  │ spectral blur, drift  │                                  │
│  └──────────┬────────────┘                                  │
│             ▼                                               │
│      voice envelope + spatial position                      │
└─────────────┬───────────────────────────────────────────────┘
              ▼
┌─ Global ────────────────────────────────────────────────────┐
│  AETHER SPACE ENGINE (integrated, not an insert)            │
│  infinite FDN reverb → shimmer bloom → spectral diffusion   │
│                                                             │
│  EFFECTS: spectral freeze · tape saturation · spectral      │
│  delay · stereo wandering                                   │
│                                                             │
│  MACROS: Dream · Bloom · Dissolve · Gravity · Entropy       │
│                                                             │
│  Output: soft saturation + true-peak safety                 │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Nothing is ever static.** Every audible parameter is a modulation target; life modulators run
   free even with no notes held (the space engine breathes at idle).
2. **Space is part of the instrument.** The Aether engine is in the core signal path with its own
   spectral state, not a post-effect. Freeze/infinite-decay is a first-class playing technique.
3. **Voices are expensive, few, and deep.** 8–16 voices, each a full cloud + body + atmosphere.
   SIMD partial banks (already proven in Innexus) make this affordable.
4. **Entropy, not chaos.** All randomness is bounded, slow, and smooth — controlled decay of order,
   never Ruinae-style instability. No aggressive distortion (that belongs to Disrumpo).
5. **Macro-first performance surface.** Dream / Bloom / Dissolve / Gravity / Entropy are the primary
   interface; deep per-engine parameters exist underneath.

## Reuse Inventory (existing KrateDSP → Seraphis)

| Seraphis subsystem | Existing components (reuse/extend) | New components |
|---|---|---|
| Harmonic Cloud | `harmonic_oscillator_bank[_simd]`, `additive_oscillator`, `harmonic_snapshot`, `harmonic_frame_utils`, `spectral_tilt`, `pitch_utils` | `HarmonicCloud` (L3) |
| Spectral Evolution | `harmonic_types`, `spectral_morph_filter` (concept), `crossfade_utils` | `SpectralState`, `SpectralMorphEngine`, entropy processor (L2/L3) |
| Life Modulators | `modulation_source` concept, `random_source`, `sample_hold_source`, `chaos_mod_source` (contrast reference), `smoother`, `random` | `LifeModulator` family (L2) |
| Resonant Body | `modal_resonator_bank[_simd]`, `iresonator`, `waveguide_string`, `body_resonance`, `sympathetic_resonance[_simd]`, `timevar_comb_bank`, `feedback_network` | `ContinuousBody` (L3) |
| Granular Atmosphere | `grain_pool`, `grain_scheduler`, `grain_processor`, `grain_envelope`, `granular_engine`, `spectral_freeze_oscillator`, `rolling_capture_buffer` | `AtmosphereEngine` (L3) |
| Aether Space | `fdn_reverb`, `reverb`, `shimmer_delay` (concepts), `diffusion_network`, `pitch_shift_processor`, `stft`, `spectral_buffer` | `AetherReverb` (L4) |
| Voice / Poly | `voice_allocator`, `poly_synth_engine`, `synth_voice` (pattern), `voice_mod_router`, `adsr_envelope`, `multi_stage_envelope` | `SeraphisVoice`, `SeraphisEngine` (L3) |
| Effects | `tape_saturator`, `spectral_delay`, `stereo_field`, `midside_processor`, `true_peak_limiter`, `frequency_shifter` | thin wiring only |
| Modulation routing | `modulation_engine`, `modulation_matrix`, `modulation_curves` | macro system (plugin layer) |

ODR note: before creating any class below, run the standard `grep -r "class Name" dsp/ plugins/` sweep —
several near-name components already exist (`ResonatorBank`, `ModalResonator`, `GranularEngine`).

---

## Part A — DSP Foundations (KrateDSP, unit-tested, no plugin yet)

### Phase 1: Life Modulator Suite

**Status: ✅ COMPLETE (2026-07-25)** — see specs/seraphis-phase1-life-modulators/compliance.md

**Spec:** `seraphis-phase1-life-modulators`
**Goal:** The identity layer. A family of slow, autonomous, bounded modulation sources that replace LFOs.
Everything downstream consumes these, so they ship first.

New components (Layer 2, `dsp/include/krate/dsp/processors/`):

- `BrownianDrift` — bounded random walk with mean-reversion (Ornstein–Uhlenbeck), smoothness control.
  The workhorse: per-partial detune drift, brightness wander, stereo wandering.
- `BreathingModulator` — asymmetric inhale/exhale cycle (not sinusoidal), rate 0.01–0.5 Hz, depth,
  irregularity (cycle-to-cycle period jitter).
- `TidalModulator` — very slow (30 s – 10 min periods) layered sine pairs with incommensurate ratios;
  never repeats exactly.
- `OrbitModulator` — two coupled oscillators (weakly coupled phase model) producing 2D output
  (x = one target, y = another); orbital decay/growth parameter.
- `SplineTrajectory` — Catmull-Rom trajectory through N random-walk waypoints; regenerates waypoints
  ahead of playback; guarantees C1-continuous output.
- `GrowthEnvelope` — one-shot logistic/S-curve rise over 1–60 s for "the sound slowly becomes"
  behaviour; retriggerable with continuation (never snaps back).

Shared contract: all conform to the existing `ModulationSource` concept so `ModulationEngine` /
`VoiceModRouter` can route them without changes. All must be RT-safe, allocation-free, and cheap at
control rate (evaluated per-block, smoothed per-sample via `Smoother`).

**Success criteria:** unit tests for boundedness (never exceeds depth), smoothness (max slew bounded),
statistical character (autocorrelation time matches rate parameter), determinism with seeded `random.h`.
**Evaluation:** render 60 s trajectories to CSV, inspect plots — motion must read as "organic drift",
not noise and not LFO.

---

### Phase 2: Harmonic Cloud Oscillator

**Status: ✅ COMPLETE (2026-07-26)** — see specs/seraphis-phase2-harmonic-cloud/compliance.md

**Spec:** `seraphis-phase2-harmonic-cloud`
**Goal:** The primary sound source. A 64-partial additive bank where every partial is an individual
living entity with drift, pan, and its own envelope.

New component (Layer 3, `dsp/include/krate/dsp/systems/harmonic_cloud.h`):

- Composes `HarmonicOscillatorBankSimd` (per-partial freq/amp/phase already proven in Innexus) with
  new per-partial state: stereo position, drift amount, individual attack/decay offsets.
- Per-partial `BrownianDrift` (shared-state, decimated: one drift evaluation per partial per block).
- Macro parameter mapping:
  - **Richness** — number of active partials + amplitude rolloff shape
  - **Inharmonicity** — stretched-partial ratio `f_n = f0 · n · sqrt(1 + B·n²)` (piano/bell law)
  - **Spectral tilt** — dB/octave slope (reuse `spectral_tilt` math)
  - **Mutation** — slow random re-weighting of partial amplitudes (life-modulated)
  - **Spectral gravity** — pulls partial ratios toward/away from pure harmonic grid
    (0 = pure harmonic, ± = stretched/compressed toward inharmonic clusters)
- Stereo output via per-partial equal-power pan (SIMD-friendly: two gain vectors).

**Distinction from Innexus:** Innexus analyses existing sounds into partials; Seraphis *generates*
partial worlds from parameters. No analysis pipeline, no `HarmonicFrame` dependency — but the bank
internals and SIMD layout are shared.

**Success criteria:** frequency accuracy per partial (< 0.1 cent static), no zipper noise under
mutation/drift, CPU budget: 64 partials + drift ≤ 0.5% of one core @ 48 kHz per voice (Innexus
particle-oscillator numbers say this is achievable). Spectral tests via `testing-dsp-analysis` (FFT
verification of tilt, inharmonicity law, gravity mapping).

---

### Phase 3: Spectral States & Morphing Engine

**Status: ✅ COMPLETE (2026-07-27)** — see specs/seraphis-phase3-spectral-morph/compliance.md

**Spec:** `seraphis-phase3-spectral-morph`
**Goal:** The heart of the instrument — the sound travels between spectral identities instead of
sitting still.

New components (Layer 2/3):

- `SpectralState` (Layer 2, plain data) — a named target spectrum: 64 partial ratio/amp pairs +
  tilt/inharmonicity metadata. Factory states: pure sine stack, bell, choir/formant, glass, breath.
  (Related to `harmonic_snapshot`, but source-agnostic — verify no ODR overlap.)
- `SpectralMorphEngine` (Layer 3) — holds 2–4 states, travels between them along a
  life-modulated trajectory (not a linear crossfade):
  - per-partial morph time offsets (low partials arrive before high partials — "bloom" motion)
  - travel driven by `SplineTrajectory` or host-synced slow ramp
- `EntropyProcessor` (Layer 2) — the signature macro. One 0–1 control that applies, in order of
  increasing entropy: partial amp jitter → phase decoherence → ratio scatter → partial death/rebirth
  (partials fade out and re-emerge slightly detuned). Low = angelic purity, high = slowly dissolving
  dream texture. Bounded and smooth at every setting — controlled decay, never Ruinae chaos.

**Success criteria:** morph continuity (no clicks, max per-block amp delta bounded), entropy
monotonicity tests (spectral flatness / partial-deviation metrics increase monotonically with the
control), state round-trip serialization. Audible A/B renders for each factory state pair.

---

### Phase 4: Continuous Resonant Body

**Status: ✅ COMPLETE (2026-07-28)** — see specs/seraphis-phase4-continuous-body/compliance.md

**Spec:** `seraphis-phase4-continuous-body`
**Goal:** A physical "body" the cloud speaks through — glass, string, metal plate, chamber, ice —
as continuous resonance, not percussion.

New component (Layer 3, `dsp/include/krate/dsp/systems/continuous_body.h`):

- Composes existing `ModalResonatorBankSimd` (mode banks, material damping laws — proven in Membrum)
  + `WaveguideString` + `TimevarCombBank` behind a material selector with crossfade
  (selector + mix pattern from the Innexus roadmap — only active modules burn CPU).
- **Continuous-excitation adapter** (the new DSP work): Membrum excites bodies with impulses;
  Seraphis feeds the sustained harmonic cloud in. Requires energy normalization (input RMS tracking →
  resonator drive compensation) and feedback-safe damping floors so sustained input never runs away.
- **Slow decay cloud:** post-resonator diffusion stage (reuse `DiffusionNetwork`) with decay times
  up to 30 s, blurring modal ringing into wash.
- Body tuning follows the voice: modes retune to note pitch with a key-tracking amount (0 = fixed
  body like a real instrument, 1 = fully tracked).
- Materials: Glass, Strings, Metal Plate, Chamber, Ice — each a preset of mode-ratio table +
  frequency-dependent damping law (Aramaki et al.: damping law is what sells the material).

**Success criteria:** stability under sustained full-scale input at max resonance (no unbounded
growth — reuse Membrum's infinite-ring test harness pattern), material A/B renders, retune smoothness
under glide, CPU ≤ 1% per voice with body active.

---

### Phase 5: Granular Atmosphere Engine

**Status: ✅ COMPLETE (2026-07-28)** — see specs/seraphis-phase5-atmosphere/compliance.md

**Spec:** `seraphis-phase5-atmosphere`
**Goal:** The third generator: frozen moments and cloud particles, not slicing. A parallel texture
layer that captures the voice's own output and suspends it.

New component (Layer 3, `dsp/include/krate/dsp/systems/atmosphere_engine.h`):

- Source: `RollingCaptureBuffer` tapping the voice's cloud+body output (self-granulating — the
  organism feeds on itself), plus optional pure-freeze mode via `SpectralFreezeOscillator`.
- Extends grain infrastructure (`GrainPool`, `GrainScheduler`, `GrainEnvelope`) for **ultra-long
  grains: 50 ms – 30 s**. This is the main new engineering: current pools assume short grains;
  needs buffer-lifetime management so a 30 s grain survives capture-buffer wraparound
  (per-grain reference into a slice-pool snapshot — `SlicePool` pattern applies).
- **Spectral blur:** per-grain STFT magnitude smearing (phase randomization amount) using existing
  `STFT`/`SpectralBuffer` — turns material into fog at high settings.
- Pitch drift per grain (`BrownianDrift` again), density (grains/s, overlapping), spatial diffusion
  (per-grain pan spread + decorrelation via `stereo_utils`).

**Success criteria:** zero allocation after prepare (30 s × density worst case pre-allocated and
asserted), no clicks at grain boundaries at any lifetime, blur metric tests (spectral flatness rises
with blur), **CPU budget ≤ 1.5% per voice at default density** (amended 2026-07-28 from ≤ 1% by user
budget decision, derived from the five measured configurations; the saturated 64-grain configuration
is out-of-region — measured and regression-tracked, not gated. Phase 7 tallies the *measured*
1.048%/voice unfrozen and 1.440%/voice frozen, not the gate. See
specs/seraphis-phase5-atmosphere/spec.md → SC-004's "AMENDED 2026-07-28" box and RA-4).

---

### Phase 6: Aether Space Engine

**Status: ✅ COMPLETE (2026-07-30)** — see specs/seraphis-phase6-aether-space/compliance.md

**Spec:** `seraphis-phase6-aether-space`
**Goal:** The integrated environment. Algorithmic (no IRs) infinite reverb with shimmer bloom and
spectral diffusion. Global (post-voice-sum), one instance.

New component (Layer 4, `dsp/include/krate/dsp/effects/aether_reverb.h`):

- Core: FDN (reuse `fdn_reverb` topology knowledge; likely a new 8×8/16×16 network rather than
  extending the delay-plugin-flavoured one) with:
  - **Size** — delay-line lengths + modal density scaling (cathedral → impossible spaces)
  - **Density** — diffusion stages engaged (reuse `DiffusionNetwork`)
  - **Decay** — RT60 from 0.5 s to **infinite** (freeze at unity feedback, energy-conserving)
  - **Dimensionality** — feedback-matrix character morph (2D plate → 3D hall → N-D impossible;
    Householder ↔ Hadamard ↔ random-orthogonal matrix interpolation)
- **Shimmer bloom** (inside the feedback loop, not a post layer): pitch-shifted feedback taps at
  +12 and +7 (reuse `PitchShiftProcessor`), plus **harmonic bloom** — a resonant emphasis stage that
  gradually reinforces partials of the held chord (sympathetic-resonance-style, reuse
  `sympathetic_resonance_simd` concepts). Each layer has independent send level.
- **Spectral diffusion:** STFT-domain tail smearing for the "underwater chamber" character.
- Life-modulated internals: size and matrix slowly breathe via Phase 1 modulators.

**Success criteria:** energy conservation in freeze mode (level stays within ±0.5 dB over 60 s),
no metallic ringing at any size (echo-density metric), shimmer regeneration stability at max bloom,
tail-smoothness spectral tests, CPU ≤ 5% global.

---

### Phase 7: Seraphis Voice & Engine

**Spec:** `seraphis-phase7-voice-engine`
**Goal:** Compose phases 1–6 into the playable instrument core.

New components (Layer 3, `dsp/include/krate/dsp/systems/`):

- `SeraphisVoice` — harmonic cloud → continuous body → atmosphere tap → voice envelope →
  spatial position (per-voice azimuth wandering via `OrbitModulator` + `StereoField`).
  Voice envelope is a `MultiStageEnvelope` with very slow defaults and a **Growth** mode
  (`GrowthEnvelope` replaces attack).
- `SeraphisEngine` — 8–16 voices via `VoiceAllocator` (steal policy: quietest, with long-release
  amnesty since releases are 10 s+), unified spread of per-voice life-modulator seeds (no two voices
  drift identically), voice-sum → `AetherReverb` → output stage (`TapeSaturator` at low drive +
  `TruePeakLimiter` safety).
- **Macro system** — the five performance controls, implemented as modulation-matrix presets over
  engine internals (reuse `ModulationEngine`):
  - **Dream** — harmonic purity ↑, reverb send ↑, life-mod depth ↑, entropy ↓
  - **Bloom** — upper partials ↑, shimmer send ↑, stereo width ↑, morph toward brighter state
  - **Dissolve** — atmosphere mix ↑, spectral blur ↑, transient definition ↓, envelope slew ↑
  - **Gravity** — air↔stone density axis: partial count, body damping, reverb size, tilt darkening
  - **Entropy** — direct wire to Phase 3 `EntropyProcessor` + drift depths

**Success criteria:** full-poly CPU budget: 16 voices, everything on, ≤ 25% of one core @ 48 kHz
(sets per-voice budgets from phases 2/4/5 with headroom); voice-steal clicklessness; macro sweeps
render-verified (each macro audibly moves the sound along its documented axis, no discontinuities);
determinism harness (seeded engine renders are reproducible for golden tests — use
`render_fingerprint.h` tolerances, never bit-exact goldens).

---

## Part B — Plugin (plugins/seraphis/)

### Phase 8: Plugin Scaffold

**Spec:** `seraphis-phase8-plugin-scaffold`
**Goal:** A registered, buildable, sound-making `plugins/seraphis/` that passes pluginval strictness 5
and is wired into every CI/tooling roster on day one.

**Template: Ruinae, not Membrum.** Ruinae is the in-repo model for a polyphonic instrument with a large
parameter surface, an engine composition layer, macros, and a preset browser — all of which Seraphis
needs. Membrum is thinner (no `parameters/` dir, kit-oriented state codec) and does not scale to the
Phase 9 parameter surface. Copy Ruinae's shape; take Membrum's *bus/AU* config (below).

#### 8.1 Directory skeleton

```
plugins/seraphis/
  CMakeLists.txt              # Ruinae's, s/RUINAE/SERAPHIS/, KIND instrument
  CLAUDE.md  CHANGELOG.md  README.md  version.json
  src/
    entry.cpp  plugin_ids.h  version.h(GENERATED)
    processor/    processor.{h,cpp} processor_params.cpp processor_state.cpp
    controller/   controller.{h,cpp} controller_view_sync.cpp parameter_helpers.h
    parameters/   global_params.h macro_params.h cloud_params.h body_params.h
                  atmosphere_params.h aether_params.h life_mod_params.h dropdown_mappings.h
    engine/       seraphis_engine_config.h        # THIN — the engine itself is dsp/systems/ (Phase 7)
    preset/       seraphis_preset_config.h
    update/       seraphis_update_config.h
    ui/                                            # empty until Phase 11
  resources/
    editor.uidesc  au-info.plist  win32resource.rc(GENERATED)
    auv3/audiounitconfig.h.in  auv3/audiounitconfig.h(GENERATED)
    auv3/macOS/Seraphis.entitlements
    presets/<categories>/
  tests/
    CMakeLists.txt  vstgui_test_stubs.cpp
    unit/test_main.cpp  unit/{param_denorm,state_roundtrip,processor_bus}_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/{processor_audio,param_flow}_test.cpp
  docs/  installers/{windows/setup.iss, linux/README.txt}
```

`src/version.h`, `resources/win32resource.rc` and `resources/auv3/audiounitconfig.h` are **generated** by
`krate_plugin_configure_generated_files()` from the shared `cmake/*.in` templates + `version.json` — commit
only `audiounitconfig.h.in`; never hand-edit the generated files (project rule: version bumps touch
`version.json` + `CHANGELOG.md` only).

Processor/controller start as 2–3 TUs each and adopt Ruinae's `processor_*` / `controller_*` split when a
file passes ~1500 lines (Ruinae: 5 processor TUs, 10 controller TUs).

#### 8.2 Per-plugin conventions (decide in the spec, record in `plugin_ids.h`)

- **FUIDs:** two freshly generated GUIDs (processor + controller). Never reused, never changed post-release.
- **AU identity:** type `aumu`; subtype **`Srph`** (taken: `Itrm Dsrm Ruin Innx Grad Mbrm`); manufacturer
  `KrAt`; bundle base `com.krateaudio.seraphis`.
- **Buses:** event-in + stereo audio-out only, **no `addAudioInput()`** — Membrum/Gradus shape. Therefore
  `kSupportedNumChannels 02`, and `au-info.plist` declares the matching single config. (Ruinae declares
  `0222` because it *does* add a sidechain input — do not copy that. Mismatched plist/bus config = AU init
  failure `-10875`.)
- **Parameter ID base: start at 0** with Ruinae-style 100-ID section gaps. Parameter IDs are per-plugin
  namespaced; the Gradus↔Ruinae 3000–3372 overlap exists only because those two share a *preset byte
  format*. Seraphis shares state with nothing, so a "fresh non-overlapping range" buys nothing and costs
  readability. Reserve: 0–99 global, 100–199 macros, 200–399 harmonic cloud, 400–599 spectral
  morph/entropy, 600–799 life modulators, 800–999 continuous body, 1000–1199 atmosphere,
  1200–1399 aether, 1400+ effects.
- **State:** `kCurrentStateVersion = 1` in `plugin_ids.h`, shared by processor and controller (no
  cross-includes).
- **Preset categories:** the category list is load-bearing (filesystem dirs *and* XML metadata must match —
  Membrum lesson) but the full set is a Phase 12 decision. Ship Phase 8 with a single placeholder category
  so `seraphis_preset_config.h` compiles and the browser has something to scan.

#### 8.3 Parameter-pack pattern

Each `parameters/<section>_params.h` follows the Ruinae contract: a `struct XParams` of `std::atomic<>`
fields, `handleXParamChange(params, id, normalizedValue)` doing the denormalization, plus register /
save / load helpers. The processor owns the structs; `processParameterChanges()` dispatches by ID range.
Phase 8 ships `global_params.h` (master gain, polyphony, output soft-limit) and `macro_params.h` (the five
macros as inert 0–1 values) only — the rest are Phase 9.

#### 8.4 Test target `seraphis_tests`

Mirrors `plugins/ruinae/tests/CMakeLists.txt`:

- plugin `.cpp`s are compiled a **second** time into the test exe via `../src/...` paths;
- plus SDK sources `memorystream.cpp`, `hosting/hostclasses.cpp`, `hosting/pluginterfacesupport.cpp`,
  `main/moduleinit.cpp`, `main/pluginfactory.cpp` and the local `vstgui_test_stubs.cpp` (Linux linker);
- `-fno-fast-math -fno-finite-math-only` source properties on any test that injects NaN/Inf;
- `catch_discover_tests(seraphis_tests REPORTER console)`.

Day-one coverage: bus/arrangement setup, parameter denormalization round-trip, state save/load round-trip,
a processor render asserting non-silence, and **editor-lifecycle enrollment** via
`tests/test_helpers/editor_lifecycle_harness.h` (every plugin does this; it only has teeth under
ASan/valgrind, which the nightly lane provides).

#### 8.5 Registration outside `plugins/` — do all of it in this spec

A green local build proves nothing here; each of these is an independent CI failure surface.

1. `CMakeLists.txt` — `add_subdirectory(plugins/seraphis)`.
2. `.github/workflows/ci.yml` — ~17 sites: `detect-changes` job output + `paths-filter` entry + the
   `for p in iterum … membrum` loop + the build / test / pluginval matrices on **all three** OS legs +
   artifact upload + macOS `auval` and AUv3 bundle verification + the three FetchContent cache-key
   `hashFiles(...)` lists.
3. `.github/workflows/release.yml` — plugin choice list + cache key.
4. `.github/workflows/valgrind-nightly.yml` — build target list and run list.
5. `tools/run-clang-tidy.ps1` (`ValidateSet` + case + `all`) **and** `tools/run-clang-tidy.sh` (case +
   `all`) — both scripts, or the Linux/macOS pre-commit lint silently skips Seraphis.
6. `tools/check-changelog-coverage.js` — `PLUGINS` array.
7. `tools/gen-specs-index.js` — slug→name map.
8. Root `CLAUDE.md` roster + build/test/pluginval/clang-tidy tables; new `plugins/seraphis/CLAUDE.md` leaf.

**Success criteria:** `Seraphis.vst3` builds on all three OS legs; `seraphis_tests` green; pluginval
strictness 5 clean; `auval -v aumu Srph KrAt` passes on macOS; a held note through the Phase 7
`SeraphisEngine` renders non-silent audio; `node tools/check-portability.js` clean; clang-tidy `all` picks
the plugin up on both scripts.

### Phase 9: Full Parameter Surface & State

**Spec:** `seraphis-phase9-parameters`

Every engine parameter registered, denormalized in `processParameterChanges()`, atomics in processor,
state save/load with versioning, spectral-state serialization (Phase 3 round-trip), macro system
wired. Editor-lifecycle harness enrollment (shared headless open/close harness — all plugins do this).
Pluginval + full state round-trip tests.

### Phase 10: Integrated Effects

**Spec:** `seraphis-phase10-effects`

The remaining effects roster, all reuse-heavy: spectral freeze (global capture-and-hold of the
Aether tail), spectral delay (`spectral_delay`), tape-like saturation (`tape_saturator`, gentle
ceiling — no aggressive distortion, that's Disrumpo), stereo wandering (`BrownianDrift` → M/S width
+ azimuth via `midside_processor`). Ordering and sends defined here, not ad hoc.

### Phase 11: UI

**Spec:** `seraphis-phase11-ui`

`editor.uidesc` (VSTGUI only, cross-platform). Macro-first layout: the five macros dominate; engine
panels underneath. One signature visualization: the **cloud view** — live partial constellation
(per-partial freq/amp/pan as drifting points) fed via DataExchange piggyback (Membrum
MetersBlock pattern — no new queues). Custom views get the standard sub-controller treatment
(vst-guide skill). No param-type swaps on registered IDs, ever.

### Phase 12: Factory Presets & Release Readiness

**Spec:** `seraphis-phase12-presets-release`

Factory preset library (categories decided in spec — fixed set, filesystem + XML metadata must
match, per Membrum lesson), installed to `C:\ProgramData\Krate Audio\Seraphis\`. Preset validation
harness: round-trip tests + an all-presets NoteOn-only render sweep asserting no silence, no
runaway (infinite-ring test pattern from Membrum). Release gate via `release-readiness` flow:
build → tests → pluginval strictness 5 → version.json/CHANGELOG.

---

## Dependency Graph

```
Phase 1 (life mods) ✅┬─→ Phase 2 (cloud) ✅┬─→ Phase 3 (morph/entropy) ✅─┐
                      │                     │                              │
                      ├─→ Phase 4 (body) ✅─┤                              ├─→ Phase 7 (voice/engine)
                      │                     │                              │        │
                      ├─→ Phase 5 (atmos) ──┘                              │        ▼
                      │                                                    │   Phase 8 (scaffold)
                      └─→ Phase 6 (aether) ────────────────────────────────┘        │
                                                                                    ▼
                                                                Phase 9 → 10 → 11 → 12
```

Phases 2, 4, 5, 6 are independent of each other once Phase 1 lands — they can be specced/built in
any order (or interleaved with listening checkpoints). Phase 7 needs all of them. Part B is strictly
sequential.

## Cross-Cutting Constraints (apply to every spec)

- **RT safety:** no allocations/locks/exceptions/IO on the audio thread; all pools sized at prepare.
- **Layer discipline:** new components declare their layer; includes only point downward.
- **ODR sweep** before every new class name.
- **CPU budgets are FRs**, measured in tests, not aspirations (per-voice budgets in phases 2/4/5,
  global in 6/7).
- **No bit-exact float goldens** — `render_fingerprint.h` / measured tolerances only.
- **Portability:** `node tools/check-portability.js` before commits; WSL probe for
  Linux-behavioural doubts; aligned-load lint applies to any new SIMD.
- **Naming:** parameters follow `k{Section}{Parameter}Id`; standard names (`Mix`, `ModDepth`, …)
  from the project convention table.

## Open Questions (resolve in the relevant spec, not before)

1. Exact partial count (64 fixed vs 32/64/128 quality tiers) — Phase 2, driven by measured CPU.
2. Spectral state authoring: factory-only or user-morphable/savable states — Phase 3/9.
3. Aether FDN order (8×8 vs 16×16) and whether spectral diffusion is always-on — Phase 6.
4. Voice count cap (8, 12, or 16) — Phase 7, after budgets are real.
5. MPE / poly-aftertouch support (natural fit for Bloom-per-note) — Phase 8/9 scope call.
