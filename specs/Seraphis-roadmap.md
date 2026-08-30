# Seraphis — Spectral Organism Synthesizer Roadmap

A phased, DSP-library-first plan for building Seraphis, the ethereal counterpart to Ruinae. Each phase is
sized to become one speckit spec (`/speckit.specify`), with its own tests and evaluation criteria. Phases
1–7 build and unit-test KrateDSP components; phases 8–13 assemble the plugin.

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

**Status: ✅ COMPLETE (2026-07-31)** — see specs/seraphis-phase7-voice-engine/compliance.md

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

**Success criteria:** full-poly CPU budget: **8 voices**, everything on, ≤ 25% of one core @ 48 kHz
(amended 2026-08-01 from "16 voices" by the phase owner's 2026-07-30 ruling — Phase 7's RQ-1, recorded
in `specs/seraphis-phase7-voice-engine/spec.md:1390-1394`. The 25% ceiling itself is **unchanged**;
only the voice count the gate is measured at moved, and relaxing the ceiling is never the lever.
Measured at Phase 9's full-surface operating point, worst of six best-of-16 runs on
windows-x64-release: **24.21% of one core at 8 voices** (2 582 570 ns/block) — the gated figure — and
**47.36% at 16 voices** (5 052 111 ns/block), recorded by Phase 9's SC-009 as an explicitly
**non-gating** number; both datasets are transcribed in
`plugins/seraphis/tests/integration/param_perf_test.cpp` under BASELINE PROVENANCE. Polyphony
**9…16 stays user-reachable** — the registered `kPolyphonyId` range remains 1…16, as the "8–16 voices"
design decision above intends — and is deliberately **outside the budgeted scenario**: shrinking the
registered maximum would be a parameter-range change at a shipped ID and is out of scope, and raising
the gate to 16 voices by relaxing the 25% ceiling is forbidden. See
`specs/seraphis-phase9-parameters/spec.md` → FR-057 clause 2, FR-058 and SC-009);
sets per-voice budgets from phases 2/4/5 with headroom; voice-steal clicklessness; macro sweeps
render-verified (each macro audibly moves the sound along its documented axis, no discontinuities);
determinism harness (seeded engine renders are reproducible for golden tests — use
`render_fingerprint.h` tolerances, never bit-exact goldens).

---

## Part B — Plugin (plugins/seraphis/)

### Phase 8: Plugin Scaffold

**Status: ✅ COMPLETE (2026-07-31)** — see specs/seraphis-phase8-plugin-scaffold/compliance.md
(local gates all green; SC-001 Linux/macOS legs + SC-004 auval pending first macOS/Linux CI run after push)

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

**Status: ✅ COMPLETE (2026-08-02)** — see specs/seraphis-phase9-parameters/compliance.md

### Phase 10: Integrated Effects

**Spec:** `seraphis-phase10-effects`

The remaining effects roster, all reuse-heavy: spectral freeze (global capture-and-hold of the
Aether tail), spectral delay (`spectral_delay`), tape-like saturation (`tape_saturator`, gentle
ceiling — no aggressive distortion, that's Disrumpo), stereo wandering (`BrownianDrift` → M/S width
+ azimuth via `midside_processor`). Ordering and sends defined here, not ad hoc.

**Status: ✅ COMPLETE (2026-08-03)** — see specs/seraphis-phase10-effects/compliance.md

### Phase 11: UI

**Status: ✅ COMPLETE (2026-08-04)** — see specs/seraphis-phase11-ui/compliance.md

**Spec:** `seraphis-phase11-ui`

`editor.uidesc` (VSTGUI only, cross-platform). **Organism-first layout**: the **cloud view** — live
partial constellation (per-partial freq/amp/pan as drifting points, x=pan / y=freq / size=amp) — fills
the whole window; it is the interface, not a panel among panels. Fed via DataExchange piggyback
(Membrum MetersBlock pattern — no new queues). The five macros are large custom ring knobs anchored
at the corners/edges, orbiting the view; deep parameter sections live in a pull-up drawer along the
bottom edge. Custom views get the standard sub-controller treatment (vst-guide skill). No param-type
swaps on registered IDs, ever.

```
+----------------------------------------------------------------------+
| SERAPHIS   [preset]                              [seed][poly][limit] |  <- slim header
|                                                                      |
|   (DREAM)                                              (BLOOM)       |
|                    .  o      .        o                              |
|              o          CLOUD VIEW          .                        |
|                  .   fills entire window        o                    |
|   (GRAVITY)        o      O     .    .               (DISSOLVE)      |
|                       .        o                                     |
|                  x=pan  y=freq  size=amp                             |
|                          (ENTROPY)                                   |
|                                                          [obs|edit]  |
+----------------------------------------------------------------------+
|  ^ drawer handle: [Cloud][Morph][Body][Atmos][Aether][FX][Life/Env]  |
+----------------------------------------------------------------------+
```

Layout commitments:

- **Macro rings react**: turning a macro visibly perturbs nearby partials in the cloud view (e.g.
  Bloom pulls partials upward) — the payoff that justifies the custom views.
- **Drawer**: collapsed = tab strip only (~30 px); open = slides up to ~40% height showing one
  section's knobs (7 tabs: Cloud, Morph, Body, Atmos, Aether, FX, Life/Env). The cloud view never
  stops rendering — it compresses or is overlapped, but stays alive.
- **Obs/Edit toggle** (bottom-right of cloud view): *Observe* = live constellation; *Edit* = drag
  partials → `setPartial`, with a Blend A→B slider → `blendStates` and Tilt dB control → `tiltState`
  in a mini-toolbar. This makes the cloud view the sole consumer of the inherited mutators (below)
  rather than a separate editing table.
- **Drawer knobs stay plain uidesc controls.** Custom-view surface is exactly three: cloud view,
  macro ring knob (one class, five instances), drawer container.

Left for the implementation spec to resolve: morph state-slot A–D placement (suggest inside the
Morph tab: 4 slot buttons + travel controls), whether the three freeze controls (Atmos, Aether,
FX spectral) also get a floating always-visible cluster or stay drawer-only, and fixed vs
resizable window sizing (cloud view scales naturally; drawer knob rows are the constraint).

**Inherited from Phase 3 via Phase 9 (RQ-1, decided 2026-08-01).** Phase 11 owns the three
`SpectralState` authoring mutators — `setPartial(index, ratio, amplitude)`, `blendStates(A, B, t)` and
`tiltState(state, dB)` — which Phase 3's C-9 had assigned to Phase 9 by name. They land here because
their **only** consumer is the per-partial editing surface, which is a UI deliverable; shipping them
earlier would have shipped dead API. **Phase 3's attached criterion lands with them**: Phase 11 MUST
carry a success criterion asserting that, over a table of adversarial inputs (out-of-range ratios,
non-monotone ratios, amplitudes outside `[0,1]`, `numPartials` outside `[0,64]`, non-finite arguments
built from bit patterns per the `-ffast-math` rule), each mutator leaves its `SpectralState` satisfying
`isValidSpectralState` (`dsp/include/krate/dsp/processors/spectral_state.h`). Phase 11 likewise owns
the per-partial engine surface those mutators exist to drive — `HarmonicCloud::setSpectralTarget`,
`setPartialPosition` and `setPartialMask` — which Phase 9 deliberately left unregistered for the same
reason: no per-partial control surface exists until this phase. See
`specs/seraphis-phase9-parameters/spec.md` → *Resolved Questions* RQ-1 and FR-058 clause 4.

### Phase 11.5: Processor whole-`process()` optimization

**Spec:** `seraphis-phase11-5-process-optimization`

**Added 2026-08-04 by the phase-owner ruling "Hybrid" on Phase 11's OE-1.** This phase exists because
Phase 11's compliance pass measured, for the first time, the thing line 313 of this document actually
promises — **whole-`process()`, not the chain alone** — and found the shipped plugin over it.

**Goal:** whole-`process()`, 8 voices, everything on, **≤ 25 % of one core on the reference machine**
(fresh-boot, seven runs, worst reported — the protocol `param_perf_test.cpp:144-207` defines).

**Evidence base:** `specs/seraphis-phase11-ui/spec.md` → *Open Escalations* → **OE-1**, which decomposes
the measured 31.7 % as: chain only, 107-row surface **22.04 %** + effects stage at maxima **0.4484 %** =
22.5 %, leaving a **~9.2-point remainder** that is neither. Phase 11's own marginal cost is **≤ 1.88
points at the worst**, measured as same-run deltas, so Phase 11 is not the mechanism and could not have
been the fix.

**Scope:** the ~9.2-point remainder only — the `Processor`'s **8 × 64 control-chunk slice loop** and the
**per-slice parameter fan-out** over a maxed 107-row surface, i.e. **Phase 8–10 plumbing**. Not the
engine, not the effects stage, not Phase 11's producer (all three are already inside their own budgets and
have their own criteria).

**SCOPE RULING 2026-08-04 (phase-owner): WIDENED TO THE ENGINE RENDER PATH.** Step 0 of this phase
instrumented `process()` itself (eight test-gated stage timers, `processor.h` `DecompStage`; diagnostic
case `Seraphis_WholeProcess_Decomposition`, `ui_perf_test.cpp`) and **measured the paragraph above to be
wrong**: at the failing SC-010(b) operating point the engine voice sum is **~91 % of the whole wall time**
(42.1 % of one core on the measuring machine) while ALL Processor plumbing combined is **0.06 %** static
and **0.53 %** under a Bloom sweep — the slice loop runs ONE slice per block at the static operating
point, and the "~9.2-point remainder" was arithmetic between two non-identical test configurations (the
chain-only subject renders a Metal Plate body at diffusion FFT 4096; the plugin ships FFT 1024), not a
measured cost. No in-scope work could recover the ~6.7 points the target needs. The phase therefore
attacks the **engine render path in `dsp/`** (per-component attribution first, then the measured
hotspots — the per-sample SIMD dispatch in `HarmonicCloud`, the morph pipeline's unconditional `exp2`
pass, the anti-alias `sqrt` pass, and whatever the attribution ranks above them). Every `dsp/` change is
pinned by render-behavior tests (aggregate metrics, never bit digests) and the full DSP + plugin suites.
The exit criteria below are UNCHANGED.

**CORRECTION 2026-08-04 (record of an overclaim in commit 7881a6ff):** that commit's message credits
the blur identity skip (skip the per-bin phase loop when the smoother has settled at `blurAmount == 0`)
with "~2.4 %" — that figure was the WHOLE Atmos/Blur decomposition row, which is dominated by the STFT
FFT round-trips the skip does not touch. The skipped loop itself is polar-native (`SpectralBuffer`
stores magnitude/phase directly; `getPhase`/`setPhase` are plain loads/stores, no trig), and the
measured effect of the skip is ≈ zero. The change is kept — it is harmless, exact-identity-gated, and
burns the same RNG draws so the SC-010 stream is unchanged — but it recovered nothing. The measured
recoveries in this phase are the grain-sweep restructure (7666aa83, ~10-15 % off the sweep) and the
Highway gather kernel for the grain span (grain-sample cost 8.90-8.96 → 8.29 ns at matched machine
state, ~12 % further).

**Phase 12 MUST NOT ship before this phase is green.** Release readiness that ships a 31.7 % instrument
against a documented 25 % promise is not release readiness; the gate belongs here, ahead of the release
phase, not inside it.

**Exit criteria:**
1. Whole-`process()` at the 8-voice operating point is **≤ 25 % of one core**, worst-of-seven on a
   fresh-boot idle machine.
2. Phase 11's four restated perf arms stay green **in their differential form** — SC-009(a), SC-014 arm 7
   and SC-031 (`tests/integration/ui_perf_test.cpp`). An optimization that made the marginal costs worse
   while lowering the absolute is not accepted.
3. Phase 11's **SC-010(b) absolute arm passes with `kSc010BaselinePinned = true`** — i.e.
   `kBaselineWholeProcessNs` is re-pinned from a real seven-run fresh-boot cold set after the optimization,
   and the arm gates rather than reports.

### Phase 12: Factory Presets & Release Readiness

**Spec:** `seraphis-phase12-presets-release`

Factory preset library (categories decided in spec — fixed set, filesystem + XML metadata must
match, per Membrum lesson), installed to `C:\ProgramData\Krate Audio\Seraphis\`. Preset validation
harness: round-trip tests + an all-presets NoteOn-only render sweep asserting no silence, no
runaway (infinite-ring test pattern from Membrum). Release gate via `release-readiness` flow:
build → tests → pluginval strictness 5 → version.json/CHANGELOG.

### Phase 13: Per-Note Expression (MPE / poly-aftertouch)

**Spec:** `seraphis-phase13-note-expression`

**This phase owns the former Open Question 5**, moved here — not struck — by Phase 9's RQ-2 ruling
(2026-08-01): per-note expression **ships**, it is simply not Phase 9's. Both halves belong to this
phase, because neither is useful without the other:

1. the **DSP half** — per-voice expression inputs on `SeraphisVoice` (pressure, timbre, per-note
   pitch). The engine's note API is `noteOn(note, velocity)` / `noteOff(note)` and carries no
   per-note expression at all, so an implemented controller would have nothing to drive until these
   exist.
2. the **plugin half** — `INoteExpressionController` on the Seraphis controller plus the declared
   note-expression type list, and the macro wiring that makes Bloom-per-note the first target.

**The controller-FUID host-cache hazard is ACCEPTED**, and recorded here so it is not rediscovered
later as a surprise: adding an interface to an already-released controller FUID can invalidate
host-cached class metadata — a class that suddenly answers `queryInterface` for
`INoteExpressionController` may be seen inconsistently until the host's cache is cleared, and users do
not clear plugin caches. The ruling accepts that cost rather than pre-emptively burning a second
controller FUID. See `specs/seraphis-phase9-parameters/spec.md` → *Resolved Questions* RQ-2, FR-064
and FR-058 clause 5.

---

## Dependency Graph

```
Phase 1 (life mods) ✅┬─→ Phase 2 (cloud) ✅┬─→ Phase 3 (morph/entropy) ✅─┐
                      │                     │                              │
                      ├─→ Phase 4 (body) ✅─┤                              ├─→ Phase 7 (voice/engine) ✅
                      │                     │                              │        │
                      ├─→ Phase 5 (atmos) ──┘                              │        ▼
                      │                                                    │   Phase 8 (scaffold)
                      └─→ Phase 6 (aether) ────────────────────────────────┘        │
                                                                                    ▼
                                                           Phase 9 ✅ → 10 ✅ → 11 ✅ → 11.5 → 12 → 13
```

Phases 2, 4, 5, 6 are independent of each other once Phase 1 lands — they can be specced/built in
any order (or interleaved with listening checkpoints). Phase 7 needs all of them. Part B is strictly
sequential. **Phase 11.5 is a hard gate ahead of Phase 12** (added 2026-08-04 by the phase-owner ruling
on Phase 11's OE-1): the whole-`process()` 25 % promise on line 313 is owned there, and release readiness
does not run before it is met.

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
2. ~~Spectral state authoring: factory-only or user-morphable/savable states — Phase 3/9.~~
   **STRUCK 2026-08-01 — RESOLVED BY PHASE 3.** `specs/seraphis-phase3-spectral-morph/spec.md:207-208`
   and its C-9 settled it: `SpectralState` is assignable and serializable, nothing in the library
   derives one, and a capture path was rejected outright. Phase 9 implements that answer — four
   factory-state slots (IDs 408–412) with full per-slot serialization — and the authoring mutators
   went to Phase 11 (RQ-1, see the Phase 11 entry).
3. Aether FDN order (8×8 vs 16×16) and whether spectral diffusion is always-on — Phase 6.
4. Voice count cap (8, 12, or 16) — Phase 7, after budgets are real.
5. MPE / poly-aftertouch support (natural fit for Bloom-per-note) — **MOVED 2026-08-01 to Phase 13
   (Per-Note Expression)**, which owns both the `SeraphisVoice` per-voice expression inputs and
   `INoteExpressionController`. Deliberately moved, not struck: the answer is "it ships, in a named
   later phase" (Phase 9's RQ-2), and the controller-FUID host-cache hazard is accepted and recorded
   in that entry.
