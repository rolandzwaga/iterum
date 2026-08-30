# Vorago — Dark Ambient Drone Instrument Roadmap

*(Working title: **Vorago**, Latin "abyss/chasm" — fits the Krate naming line of Iterum / Disrumpo /
Ruinae / Innexus / Gradus / Membrum / Seraphis. Rename freely; nothing below depends on it.)*

A phased, DSP-library-first plan for a dedicated drone instrument in the Lustmord tradition: not "a
synth with long attacks" but a procedural ecosystem where holding one note for five minutes is
rewarded. Each phase is sized to become one speckit spec with its own tests and evaluation criteria.

## Positioning in the lineup

| Plugin | Identity |
|---|---|
| Ruinae | instability, chaos, aggression |
| Innexus | analysis/resynthesis of existing sounds |
| Seraphis | ethereal, weightless, angelic evolution |
| **Vorago** | **dark, massive, subterranean, alive — geological time scales** |

Vorago is Seraphis's dark sibling, not its clone. Both are "living slow synthesis," but they diverge
on every axis that matters: Seraphis floats (upper partials, shimmer, air), Vorago sinks
(subharmonics, cavern resonance, pressure). Seraphis evolves via spectral morphing between authored
states; Vorago evolves via **emergent agent interaction** and **discrete slow events** — nothing is
authored, everything is grown.

## Core Philosophy

- The engine thinks in **sound masses**, not oscillators. Every note instantiates a small
  environment of semi-independent generators that drift apart over time.
- **Nothing repeats exactly.** All movement comes from bounded stochastic processes and emergent
  agent interaction, never from LFO loops.
- **Time scale is minutes, not milliseconds.** Events fire every 20–90 s; blooms unfold over 45 s;
  seasonal modulation cycles span 5–20 minutes.
- **Weight is a feature.** Subharmonic content, acoustic body mass, and cavern-scale space are core
  signal path, not post-effects.
- **Concepts, not parameters.** The performance surface is ~12 concept macros (Darkness, Age,
  Density, Gravity, Entropy, Pressure, …) mapping internally to hundreds of micro-parameters.

## Architecture Overview

```
                      ┌───────────────────┐
                      │  Note / Gesture   │
                      └─────────┬─────────┘
                                ▼
┌─ Per Voice (4–8, deep & expensive) ─────────────────────────────┐
│                                                                 │
│  ┌──────────────────┐  ┌──────────────────┐                     │
│  │ HARMONIC CLOUD   │  │ NOISE ORGANISM   │   ┌──────────────┐  │
│  │ 64 partials,     │  │ living noise →   │◄──┤ ECOSYSTEM    │  │
│  │ drift + gravity  │  │ wandering filters│   │ ENGINE       │  │
│  └────────┬─────────┘  └────────┬─────────┘   │ agents:      │  │
│           ▼                     ▼             │ attract /    │  │
│  ┌──────────────────────────────────────┐     │ repel /      │  │
│  │ RESONANCE DRIFT NETWORK              │◄────┤ exchange     │  │
│  │ 12 wandering resonant peaks          │     │ energy       │  │
│  └────────┬─────────────────────────────┘     └──────▲───────┘  │
│           ▼                                          │          │
│  ┌──────────────────┐  ┌──────────────────┐          │          │
│  │ FEEDBACK ECOLOGY │  │ GRANULAR GHOSTS  │   ┌──────┴───────┐  │
│  │ 5–6 cross-coupled│  │ memories of the  │   │ SLOW EVENT   │  │
│  │ micro-loops      │  │ drone (capture/  │◄──┤ ENGINE       │  │
│  └────────┬─────────┘  │ reverse/stretch) │   │ 20–90 s      │  │
│           ▼            └────────┬─────────┘   │ scheduler    │  │
│  ┌──────────────────────────────┴───────┐     └──────────────┘  │
│  │ HARMONIC BLOOM  (minutes-scale       │                       │
│  │ child-partial generation)            │     LIFE MODULATORS   │
│  └────────┬─────────────────────────────┘     (Brownian, tidal, │
│           ▼                                    chaos, Perlin,   │
│  ┌──────────────────────────────────────┐      seasonal)        │
│  │ ACOUSTIC BODY (stone/steel/hull/…)   │                       │
│  └────────┬─────────────────────────────┘                       │
└───────────┼─────────────────────────────────────────────────────┘
            ▼
┌─ Global ────────────────────────────────────────────────────────┐
│  SUBHARMONIC ENGINE  (÷2, fifth-below, ÷4 → LP → saturation)    │
│  SPECTRAL SMEAR      (FFT bin blur — fog, distance, age)        │
│  CAVERN SPACE ENGINE (ER → diffusion → FDN → moving dampers →   │
│                       spectral damping — underground bunker)    │
│  CONCEPT MACROS      (Darkness · Age · Density · Movement ·     │
│                       Gravity · Entropy · Pressure · Weight ·   │
│                       Fog · Life · Depth · Mass)                │
│  Output: soft saturation + true-peak safety                     │
└─────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Emergence over scripting.** The Ecosystem Engine (agents exchanging energy) and the Slow Event
   Engine (discrete scheduled happenings) are the identity layer — the analogue of Seraphis's
   spectral-morph layer. They ship as first-class DSP components, unit-tested for boundedness.
2. **Few, enormous voices.** 4–8 voices. A drone instrument is played with one or two held notes;
   per-voice CPU budget is correspondingly generous (~4–5% per voice vs Seraphis's ~3%).
3. **All randomness bounded and slow.** Every stochastic process has hard bounds, mean-reversion,
   and slew limits. Feedback ecology has an energy governor. A drone left running overnight must
   neither die nor explode (Membrum's infinite-ring test pattern applies everywhere).
4. **Low end is engineered, not hoped for.** Subharmonic engine + body mass + spectral tilt are
   coordinated by the Weight/Mass macros; true-peak limiting and DC safety are mandatory.
5. **Share the Seraphis substrate, diverge at the identity layer.** Life modulators, harmonic
   cloud, atmosphere/granular infrastructure, continuous body, and (likely) the space-engine core
   are shared KrateDSP components. Vorago-specific components are the noise organism, resonance
   drift network, feedback ecology, subharmonic engine, slow-event scheduler, and ecosystem agents.

## Reuse Inventory (existing KrateDSP → Vorago)

Legend: ✅ = exists and is largely sufficient · 🔶 = exists, needs extension · 🆕 = new component.

| Plan layer | Existing components | Verdict / new work |
|---|---|---|
| L1 Harmonic Cloud Oscillator | `systems/harmonic_cloud.h` (64-partial SIMD bank, per-partial Brownian drift, pan, mutation, inharmonicity, **spectral gravity**), `harmonic_oscillator_bank_simd`, `additive_oscillator` | ✅ **Already built** (Seraphis Phase 2). Vorago uses darker default states + deeper sub-partial weighting. |
| L2 Noise Organism | `processors/noise_generator.h` (12 models: White/Pink/Brown/Blue/Violet/Grey/**Velvet**/TapeHiss/**VinylCrackle**/Asperity/VinylRumble/ModulationNoise), `stochastic_filter.h` (filter with stochastic parameter wander), `resonator_bank`, `timevar_comb_bank`, `multimode_filter` | 🔶 Models + wandering filters exist. 🆕 `NoiseOrganism` (L3): N noise sources → per-source wandering resonator/comb chains, life-modulated. Missing models (filtered wind, granular dust, metallic hiss) are configurations of existing pieces (Brown+`StochasticFilter`, `Velvet`+grain envelope, Blue+`TimevarCombBank`/`FrequencyShifter`). |
| L3 Resonance Network | `resonator_bank`, `modal_resonator_bank_simd`, `iresonator`, `timevar_comb_bank`, `sympathetic_resonance_simd` | 🆕 `ResonanceDriftNetwork` (L3): 12 peaks whose freq/Q/gain each wander via life modulators — thin composition of `ResonatorBank` + `BrownianDrift`; the banks themselves need no new DSP. |
| L4 Spectral Smear | `STFT`, `spectral_buffer`, `spectral_simd`, spectral-blur stage inside `atmosphere_engine` (Seraphis Phase 5) | 🔶 Blur math exists but is embedded per-grain in AtmosphereEngine. 🆕 Extract standalone `SpectralSmear` (L2): bin-magnitude smearing + phase decoherence on a continuous stream. |
| L5 Feedback Ecology | `feedback_network`, `flexible_feedback_network`, `filter_feedback_matrix`, `i_feedback_processor`, `crossfading_delay_line`, `dc_blocker` | 🔶 Single-loop infrastructure is mature. 🆕 `FeedbackEcology` (L3): 5–6 micro-loops (osc→filter→delay→resonator→back), cross-coupling matrix, per-loop tiny gain/mod, **global energy governor**. |
| L6 Granular Ghosts | `systems/atmosphere_engine.h` (Seraphis Phase 5: self-granulating capture, 50 ms–30 s grains, spectral blur, per-grain pitch drift), `rolling_capture_buffer`, `reverse_buffer`, `grain_pool/scheduler/processor`, `slice_pool` | ✅ **~90% built.** 🔶 Add ghost-flavoured config: reverse playback per grain, event-triggered (not continuous-density) scheduling, darker blur defaults. |
| L7 Harmonic Bloom | `harmonic_snapshot`, `spectral_coring_estimator`, `fft_autocorrelation`, `sympathetic_resonance_simd`; Seraphis Phase 6 plans in-loop shimmer/bloom | 🆕 `BloomEngine` (L3): analyze strongest current peaks → spawn child partials into the cloud → 45 s fade-in / 3 min fade-out lifecycle. Peak analysis and partial banks exist; the lifecycle manager is new. |
| L8 Dark Modulation | **Seraphis Phase 1 suite ✅**: `brownian_drift` (Ornstein–Uhlenbeck), `tidal_modulator` (30 s–10 min never-repeating = "seasonal cycles"), `spline_trajectory`, `orbit_modulator`, `breathing_modulator`, `growth_envelope`; `chaos_mod_source` (Lorenz/Rossler/Chua/Henon), `random_source`, `sample_hold_source`, `modulation_engine`, `voice_mod_router` | ✅ **Almost entirely built.** 🆕 Only gaps: `PerlinNoiseSource` (L2, small) and optionally the Aizawa attractor added to `ChaosModSource`. |
| L9 Space Engine | `fdn_reverb`, `reverb`, `diffusion_network`, `pitch_shift_processor`; **Seraphis Phase 6 `AetherReverb`** (ER→diffusion→FDN→spectral damping, freeze, life-modulated internals) is the same topology | 🔶 **Strategic reuse point:** build `AetherReverb` (Seraphis Phase 6) as the shared L4 space core; Vorago's "cavern" is a dark configuration + `MovingDampers` extension (per-line damping filters that wander). Avoid building two big FDNs. |
| L10 Subharmonic Engine | `sub_oscillator`, `pitch_tracker`/`pitch_detector` (not needed — pitch is known from the note), `one_pole`, `saturation_processor`, `tape_saturator`, `dc_blocker` | 🆕 `SubharmonicEngine` (L3): synchronous dividers (÷2, ÷4) + fifth-below tracked oscillator + LP + saturation. Small: composes existing pieces, driven by known voice pitch (no detection needed). |
| L11 Slow Event Engine | `pattern_scheduler` (rhythmic, wrong time scale), `multi_stage_envelope` | 🆕 `SlowEventScheduler` (L2): seeded stochastic scheduler, one event per 20–90 s, event = {target, envelope (rise/hold/fall over seconds–minutes), depth}. RT-safe, no allocation, deterministic under seed. **Identity component.** |
| L12 Entropy | `processors/entropy_processor.h` ✅ (Seraphis Phase 3: amp jitter → phase decoherence → ratio scatter → partial death/rebirth), `modulation_engine` macro routing | ✅ Reuse directly; Vorago's global Entropy knob = EntropyProcessor + scaled drift/event depths via macro system. |
| L13 Harmonic Gravity | `HarmonicCloud` **spectral gravity** parameter ✅ (pull toward/away from harmonic grid) | ✅ Built. Vorago exposes it as a slow, life-modulated macro ("breathing" = gravity oscillating via `BreathingModulator`). |
| L14 Acoustic Body | `systems/continuous_body.h` ✅ (Seraphis Phase 4: ModalResonatorBankSimd + WaveguideString + TimevarCombBank behind material selector, continuous-excitation adapter, energy normalization, Glass/Strings/Metal Plate/Chamber/Ice materials, 30 s decay cloud) | ✅ **Built.** 🔶 Add dark material table: Stone Chamber, Steel Tank, Wooden Hull, Cathedral Column, Cavern Wall, Glass Sphere (mode-ratio tables + damping laws — data, not code) and **multi-body blend** (2 bodies crossfaded/parallel). |
| Ecosystem agents | `modulation_engine` (routing), `envelope_follower` (energy sensing) | 🆕 `EcosystemEngine` (L3): agent graph with energy-exchange rules. **The flagship differentiator — and the highest-risk component.** Prototype offline first (see Phase 8). |
| Voice / Poly | `voice_allocator`, `poly_synth_engine`, `synth_voice` pattern, `multi_stage_envelope`, `adsr_envelope` | 🔶 Reuse patterns; Vorago voice/engine composition is new but mechanical (Seraphis Phase 7 is the template). |
| Output safety | `true_peak_limiter`, `tape_saturator`, `dc_blocker`, `midside_processor`, `stereo_field` | ✅ Direct reuse. |

ODR note: before creating any class below, run `grep -r "class Name" dsp/ plugins/` — the near-name
hazard list here is long (`ResonatorBank`, `FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`,
`PatternScheduler`).

## Relationship to Seraphis (sequencing constraint)

Vorago's substrate is ~60% Seraphis components, four of which are **already complete** (life
modulators, harmonic cloud, entropy, continuous body, atmosphere engine). Two Seraphis phases are
still pending and matter here:

- **Seraphis Phase 6 (AetherReverb)** — build it once as the shared space-engine core; Vorago's
  Cavern engine extends it rather than duplicating an FDN. Vorago Phase 9 depends on it.
- **Seraphis Phase 7 (voice/engine)** — establishes the voice-composition, macro-system, and
  determinism-harness patterns Vorago Phase 10 copies.

**Recommendation:** start Vorago Part A phases 1–8 any time (they are independent of Seraphis's
remaining work), but schedule Vorago phases 9–10 after Seraphis 6–7 land so the shared patterns are
proven once, in one place.

---

## Part A — DSP Foundations (KrateDSP, unit-tested, no plugin yet)

### Phase 1: Modulation Gap-Fill + Slow Event Engine

**Spec:** `vorago-phase1-events-modulation`
**Goal:** Complete the modulation vocabulary and build the first identity component.

New components:

- `PerlinNoiseSource` (L2) — 1D gradient-noise modulation source (smooth, natural, band-limited by
  construction), conforming to `ModulationSource` so `ModulationEngine`/`VoiceModRouter` route it
  unchanged. Octave-summed (fBm) variant for roughness control.
- Aizawa attractor added to `ChaosModSource` (existing Lorenz/Rossler/Chua/Henon roster).
- `SlowEventScheduler` (L2) — seeded stochastic scheduler: draws next-event time from a bounded
  distribution (20–90 s default, scalable), fires one event with {target id, attack/hold/release
  envelope over seconds–minutes, depth, polarity}. Multiple independent schedulers per voice.
  Deterministic under seed; RT-safe; no allocation after prepare.

**Success criteria:** Perlin smoothness (bounded derivative) and spectral rolloff tests; scheduler
inter-event-time distribution tests (seeded, statistical bounds); event envelope continuity (C1, no
clicks); 60 s CSV renders inspected for organic character (Phase 1 Seraphis evaluation pattern).

---

### Phase 2: Noise Organism

**Spec:** `vorago-phase2-noise-organism`
**Goal:** Living noise — the second sound source beside the harmonic cloud.

New component (L3, `dsp/include/krate/dsp/systems/noise_organism.h`):

- N (2–4) simultaneous noise sources selected from the existing `NoiseGenerator` roster (velvet,
  pink, brown, crackle, hiss…) plus three composed models: **filtered wind** (brown +
  `StochasticFilter` band-pass wander), **granular dust** (velvet impulses through grain envelopes),
  **metallic hiss** (blue/violet through `TimevarCombBank` + optional `FrequencyShifter` detune).
- Each source feeds a per-source chain: resonator (from `ResonatorBank`) → comb (`TimevarCombBank`)
  → `StochasticFilter`. **All filter parameters wander** via `BrownianDrift`/`PerlinNoiseSource` —
  the plan's core requirement.
- Per-source level breathing via `BreathingModulator`; event hooks so the `SlowEventScheduler` can
  "awaken" a dormant source.

**Success criteria:** long-render stationarity (no level creep over 10 min), spectral-motion metric
(band-energy autocorrelation shows drift at the configured rate), zero allocation after prepare,
CPU ≤ 1% per voice.

---

### Phase 3: Resonance Drift Network

**Spec:** `vorago-phase3-resonance-drift`
**Goal:** Standing waves inside a cave — 12 independently wandering resonant peaks.

New component (L3, `systems/resonance_drift_network.h`):

- 12 resonant peaks (compose `ResonatorBank`/`ModalResonatorBankSimd` — SIMD path preferred, proven
  in Membrum) with per-peak `BrownianDrift` on frequency (bounded around anchor ratios), Q, and gain.
- Anchor modes: free (peaks wander anywhere in range), keyed (anchors track note pitch ×
  harmonic-ish ratios), hybrid (gravity-style pull, reusing the HarmonicCloud gravity concept).
- Peak "life cycle": individual peaks can sleep/wake (gain → 0 and back over tens of seconds),
  event-hookable.

**Success criteria:** stability at max Q under sustained input (infinite-ring harness pattern), no
zipper under drift (per-block coefficient smoothing), wander-rate spectral tests, CPU ≤ 0.75% per
voice.

---

### Phase 4: Spectral Smear

**Spec:** `vorago-phase4-spectral-smear`
**Goal:** Fog, distance, age — spectral blur that is not reverb.

New component (L2, `processors/spectral_smear.h`):

- STFT (existing `STFT`/`SpectralBuffer`) → per-bin magnitude smearing (leaky integrator per bin,
  frequency-dependent time constants — lows smear longer) + phase decoherence amount → reconstruct.
- Extracted/generalized from the per-grain blur inside `AtmosphereEngine` (refactor that engine to
  consume the shared component only if it is a genuine drop-in; otherwise leave Atmosphere untouched
  — no speculative unification).
- Smear amount and tilt are modulation targets (fog rolls in via `TidalModulator`).

**Success criteria:** spectral-flatness increase monotonic with smear amount, latency reported
correctly, transparent at 0% (null test within tolerance), no time-domain smearing artifacts
(pre-echo metric), CPU ≤ 0.5% global.

---

### Phase 5: Feedback Ecology

**Spec:** `vorago-phase5-feedback-ecology`
**Goal:** Five or six tiny interacting feedback loops that behave like coupled vibrating objects.

New component (L3, `systems/feedback_ecology.h`):

- Micro-loop = filter (`MultimodeFilter`) → delay (`CrossfadingDelayLine`, 10–500 ms) → resonator
  (single `IResonator` mode) → gain (< 1) → back, with `DCBlocker` in-loop. 5–6 instances.
- Cross-coupling matrix (each loop bleeds a few % into its neighbours) — reuse
  `FilterFeedbackMatrix`/`FlexibleFeedbackNetwork` topology knowledge.
- **Energy governor:** global RMS tracker with soft compression of total loop energy — interaction
  without runaway. Per-loop tiny life-modulation of delay time and filter cutoff.
- Input taps from cloud + noise organism; output mixed back at low level.

**Success criteria:** bounded output for ANY parameter combination over 30 min renders (this is the
critical test — worst-case gain/coupling sweep), audible cross-loop interaction (coherence metric
between loop outputs rises with coupling), no zipper on delay-time drift, CPU ≤ 1% per voice.

---

### Phase 6: Subharmonic Engine

**Spec:** `vorago-phase6-subharmonic`
**Goal:** Impossible low frequencies — cinematic weight.

New component (L3, `systems/subharmonic_engine.h`):

- Driven by the known voice pitch (no pitch detection needed — this is a synth): phase-locked
  sub-oscillators at f/2, f/4 (extend/reuse `SubOscillator`) and 2f/3 (fifth below), each with its
  own level and slow level-breathing.
- Amplitude-follows the voice body (via `EnvelopeFollower`) so subs swell with the drone rather
  than droning independently.
- Chain: sub sum → gentle low-pass (`OnePole`/`TwoPoleLP`) → saturation (`SaturationProcessor` at
  low drive) → `DCBlocker`.
- Global (post-voice-sum) with per-voice pitch tracking of the lowest sounding voice, or per-voice
  — decide in spec after CPU measurement.

**Success criteria:** sub level tracks input envelope (no free-running boom), harmonic purity of
dividers (THD measured), true-peak safety with subs at max (headroom test), mono-compatibility
check (`midside` correlation), CPU ≤ 0.5%.

---

### Phase 7: Harmonic Bloom

**Spec:** `vorago-phase7-harmonic-bloom`
**Goal:** Every few minutes the drone grows new harmonics; later they die back.

New component (L3, `systems/bloom_engine.h`):

- Input: the voice's current partial state (direct from `HarmonicCloud` — no FFT analysis needed
  in-voice; the cloud already knows its spectrum. `HarmonicSnapshot`/`SpectralCoringEstimator`
  remain available if an audio-domain variant is wanted later).
- Picks strongest K partials → spawns children (octave/fifth/detuned-neighbour relationships) into
  reserved cloud partial slots → 45 s fade-in, minutes-scale hold, 3 min fade-out lifecycle per
  child, managed by a small lifecycle table (no allocation).
- Trigger source: `SlowEventScheduler` (Phase 1) or continuous slow probability; depth scaled by
  the Life/Age macros.

**Success criteria:** child partials appear/disappear with C1 amplitude envelopes (no clicks),
reserved-slot accounting never exceeds cloud capacity (asserted), long-render evolution test (30 min
render shows spectral centroid/partial-count trajectory, never static, never divergent), CPU ≈ free
(bookkeeping only — partials render in the existing SIMD bank).

---

### Phase 8: Ecosystem Engine (flagship, highest risk)

**Spec:** `vorago-phase8-ecosystem`
**Goal:** The differentiator — agents, not modulation. Dozens of tiny autonomous entities (partial
clusters, resonator peaks, noise emitters, feedback loops, ghost triggers) that sense one another's
energy and attract / repel / synchronize / exchange.

**De-risk first:** before the spec, an offline Node.js prototype (project scripting rule) simulating
the agent graph + rule set, rendering behaviour traces to CSV/plots. The rule set is tuned there —
where iteration is seconds, not audio-thread builds. The spec then encodes the proven rules.

New component (L3, `systems/ecosystem_engine.h`):

- Fixed-capacity agent table (~24–48 agents/voice). Agent = {kind, energy, position (abstract 1–2D
  "habitat" coordinate), rule params}. Control-rate only (per-block): sense neighbours' energy →
  apply rules → write outputs.
- Outputs are ordinary modulation values consumed via the existing `ModulationEngine`/
  `VoiceModRouter` contract — the ecosystem *is* a bank of `ModulationSource`s, so every engine
  from phases 2–7 hooks in without new plumbing (a resonator agent's energy drives a
  `ResonanceDriftNetwork` peak gain; a noise agent wakes a `NoiseOrganism` source; a feedback agent
  opens an ecology loop's coupling).
- Conservation constraint: total system energy budgeted and leaky — guarantees global boundedness
  regardless of rule configuration (the same philosophy as the feedback governor).
- Deterministic under seed for golden testing.

**Success criteria:** boundedness under random rule fuzzing (1000 seeded configs × accelerated
sim), non-triviality metric (energy distribution entropy keeps changing over 30 min — no frozen
fixed points, no limit cycles shorter than N minutes), determinism harness, CPU ≤ 0.5% per voice
(control-rate math only).

---

### Phase 9: Cavern Space Engine

**Spec:** `vorago-phase9-cavern-space`
**Depends on:** Seraphis Phase 6 (`AetherReverb`) — build that first as the shared core.
**Goal:** An enormous underground bunker: ER → diffusion → FDN feedback matrix → moving dampers →
late field → spectral damping.

Work (L4, `effects/cavern_verb.h` — or a configuration layer over `AetherReverb`, decided in spec):

- Reuse the AetherReverb FDN/diffusion/spectral-damping core. Vorago-specific extensions:
  - **Moving dampers** — per-delay-line damping filters whose cutoffs wander slowly
    (`BrownianDrift`), so the space itself breathes darkly.
  - **Cavern ER pattern** — sparse, long-predelay early reflections (stone-space flavoured) vs
    Seraphis's air-flavoured onset.
  - Dark tuning: HF decay strongly shortened, size range biased huge, no shimmer-up taps (bloom
    lives in the voice, not the reverb; +12 shimmer is Seraphis's identity, not Vorago's).
- Infinite-hold (freeze) retained — a drone instrument needs it.

**Success criteria:** inherits AetherReverb's gates (energy conservation in freeze ±0.5 dB/60 s, no
metallic ringing via echo-density metric) + damper-motion smoothness test, CPU ≤ 5% global.

---

### Phase 10: Vorago Voice & Engine

**Spec:** `vorago-phase10-voice-engine`
**Depends on:** all above; pattern-template from Seraphis Phase 7.
**Goal:** Compose everything into the playable instrument core.

- `VoragoVoice` (L3) — cloud + noise organism → resonance drift network → feedback ecology tap →
  bloom lifecycle → body (`ContinuousBody` with the new dark material table + 2-body blend) →
  ghost/atmosphere tap (`AtmosphereEngine`, ghost config) → voice envelope (`MultiStageEnvelope`,
  very slow defaults / `GrowthEnvelope` mode).
- Dark material data: Stone Chamber, Steel Tank, Wooden Hull, Cathedral Column, Cavern Wall, Glass
  Sphere — mode-ratio tables + frequency-dependent damping laws (Aramaki-style, the Membrum/
  Seraphis material pattern; this is data authoring + listening, not new DSP).
- `VoragoEngine` (L3) — 4–8 voices (`VoiceAllocator`, quietest-steal with long-release amnesty),
  per-voice unique seeds, voice-sum → subharmonic engine → spectral smear → cavern space → output
  (`TapeSaturator` low drive + `TruePeakLimiter`).
- **Concept macro system** (via `ModulationEngine` presets): Darkness, Age, Density, Movement,
  Gravity, Decay, Instability, Distance, Entropy, Pressure, Weight, Fog, Life, Depth, Mass. Each is
  a documented multi-target mapping (e.g. **Weight** = sub levels ↑ + body mass materials + tilt
  darkening; **Life** = ecosystem activity + event rate + bloom probability; **Fog** = smear ↑ +
  ghost mix ↑ + distance filtering). Trim the list in-spec if some macros prove redundant in
  listening — 15 concepts is a ceiling, not a target.

**Success criteria:** full-poly CPU: 8 voices everything-on ≤ 30% of one core @ 48 kHz (sets
per-voice budgets with headroom); overnight soak render (8 h) bounded and non-static; macro sweeps
render-verified along documented axes; determinism harness (`render_fingerprint.h` tolerances — no
bit-exact goldens, project rule).

---

## Part B — Plugin (plugins/vorago/)

Follows the Seraphis Part B template nearly verbatim — those phases were specified against the same
repo infrastructure and their checklists apply directly.

### Phase 11: Plugin Scaffold

**Spec:** `vorago-phase11-plugin-scaffold`

- **Template: Ruinae shape** (poly instrument, large parameter surface, `parameters/` packs,
  engine-config layer, preset browser) with **Membrum/Gradus bus config** (event-in + stereo-out,
  no `addAudioInput()`, `kSupportedNumChannels 02`, matching single plist config).
- AU identity: type `aumu`; subtype `Vrgo` (taken: `Itrm Dsrm Ruin Innx Grad Mbrm` + Seraphis's
  reserved `Srph`); manufacturer `KrAt`; two fresh FUIDs.
- Parameter ID base 0 with 100-ID section gaps (Seraphis Phase 8 rationale applies unchanged).
- Full out-of-tree registration checklist from Seraphis Phase 8.5 (root CMake, ci.yml ~17 sites,
  release.yml, valgrind-nightly, both clang-tidy scripts, check-changelog-coverage, gen-specs-index,
  CLAUDE.md rosters + new leaf) — every item, day one.
- Day-one tests: bus setup, denorm round-trip, state round-trip, non-silent render,
  editor-lifecycle harness enrollment.

**Success criteria:** builds on all three OS legs, `vorago_tests` green, pluginval strictness 5
clean, `auval -v aumu Vrgo KrAt` passes, check-portability clean, clang-tidy `all` picks it up in
both scripts.

### Phase 12: Full Parameter Surface & State

**Spec:** `vorago-phase12-parameters`

All engine parameters registered/denormalized/persisted with `kCurrentStateVersion`; concept-macro
system wired; per-section parameter packs (`cloud`, `noise`, `resonance`, `ecology`, `sub`, `smear`,
`events`, `ecosystem`, `body`, `space`, `macros`); pluginval + full round-trip tests.

### Phase 13: UI

**Spec:** `vorago-phase13-ui`

VSTGUI only. **Concept-first layout:** the macro concepts dominate; engine panels beneath. One
signature visualization: the **ecosystem view** — live agent habitat (agents as glowing points,
energy as brightness, interactions as fading links) via DataExchange piggyback (Membrum MetersBlock
pattern — no new queues). No param-type swaps on registered IDs, ever.

### Phase 14: Factory Presets & Release Readiness

**Spec:** `vorago-phase14-presets-release`

Fixed preset category set (filesystem dirs + XML metadata must match — Membrum lesson), installed to
`C:\ProgramData\Krate Audio\Vorago\`. Validation harness: round-trip tests + all-presets NoteOn-only
**long-render** sweep (drone presets need minutes-scale non-silence/non-runaway assertions, not the
usual seconds). Release gate via `release-readiness` flow.

---

## Dependency Graph

```
                  ┌─→ Phase 2 (noise organism) ──┐
                  ├─→ Phase 3 (resonance drift) ─┤
Phase 1           ├─→ Phase 4 (spectral smear) ──┤
(events + ────────┼─→ Phase 5 (feedback ecology)─┼─→ Phase 10 (voice/engine) ─→ Phase 11 (scaffold)
 Perlin/Aizawa)   ├─→ Phase 6 (subharmonic) ─────┤            ▲                        │
                  ├─→ Phase 7 (bloom) ───────────┤            │                        ▼
                  └─→ Phase 8 (ecosystem) ───────┘            │           Phase 12 → 13 → 14
                                                              │
Seraphis Phase 6 (AetherReverb) ──→ Phase 9 (cavern space) ───┘
Seraphis Phase 7 (voice pattern) ─────────────────────────────┘  (pattern template, not code dep)
```

Phases 2–8 are mutually independent once Phase 1 lands — build in any order, interleaved with
listening checkpoints. Phase 8's offline prototype can start immediately (no DSP dependency).
Already-complete Seraphis components (life modulators, harmonic cloud, entropy, continuous body,
atmosphere engine) are consumed as-is from day one.

## Cross-Cutting Constraints (apply to every spec)

- **RT safety:** no allocations/locks/exceptions/IO on the audio thread; all pools/tables sized at
  prepare.
- **Boundedness is the theme-level FR:** every stochastic/feedback/agent component ships with a
  worst-case soak test (accelerated where possible, real-time where not). A drone instrument that
  can run away or die overnight is broken by definition.
- **Layer discipline** + **ODR sweep** before every new class name.
- **CPU budgets are FRs**, measured in tests (per-voice budgets phases 2–8, global 9–10).
- **No bit-exact float goldens** — `render_fingerprint.h` / measured tolerances only.
- **Portability:** `node tools/check-portability.js` before commits; WSL probe for Linux doubts;
  aligned-load lint on any new SIMD.
- **Naming:** `k{Section}{Parameter}Id`; standard parameter names from the project table.
- **Shared-component changes** (AtmosphereEngine ghost config, ContinuousBody materials,
  SubOscillator extension, AetherReverb extensions) must keep Seraphis's tests green — they are
  consumers of the same components.

## Open Questions (resolve in the relevant spec, not before)

1. Final name (Vorago is a placeholder) — before Phase 11 (FUIDs/subtype/bundle-id depend on it).
2. Ecosystem rule set: which agent kinds and interaction rules survive the offline prototype —
   Phase 8, after prototyping.
3. Cavern space: configuration layer over shared `AetherReverb` vs separate L4 effect — Phase 9,
   after Seraphis Phase 6 exists.
4. Subharmonic engine placement: global (track lowest voice) vs per-voice — Phase 6, after CPU
   measurement.
5. Voice count (4, 6, or 8) and whether ghost/atmosphere is per-voice or global — Phase 10, after
   budgets are real.
6. Macro roster trim (which of the 15 concepts survive listening) — Phase 10/12.
7. MPE / channel-pressure mapping (pressure → Weight/Pressure macros is a natural fit) — Phase
   11/12 scope call.
