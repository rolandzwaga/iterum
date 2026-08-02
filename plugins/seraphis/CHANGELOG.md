# Changelog

All notable changes to Seraphis will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-08-01

The instrument becomes playable. 0.1.0 shipped the engine behind three global controls and five macros
that did nothing; this release exposes the whole engine — 83 new parameters across the harmonic cloud,
spectral morph and entropy, life modulators, continuous body, granular atmosphere and the Aether space
engine — saves and restores all of it, and wires the five macros to the sound. No DSP changed: this is
the surface over the Phase 1-7 engine that was already there.

### Added

- **The full parameter surface — 91 registered parameters, up from 8** — Every engine control that
  Phase 9 scoped is now a host parameter: 11 for the harmonic cloud, 13 for spectral morph and entropy,
  10 for the life modulators and voice envelope, 13 for the continuous body, 17 for the granular
  atmosphere and 18 for the Aether space engine, plus a global seed. Each sits in the reserved band of
  the component it controls, and every registered type is frozen for the life of the plugin.
- **The macros do something now** — Dream, Bloom, Dissolve, Gravity and Entropy move the engine along
  their documented axes, and they **compose with** the deep parameters rather than fighting them: a
  deep parameter sets the origin the macros travel from, so moving a slider and then moving a macro
  does what you would expect. Left at their neutral defaults, with every new parameter at its default,
  a 0.2.0 render matches a 0.1.0 one — the surface is new, the sound is not.
- **Spectral states are selectable and saved** — Four morph slots, each independently set to one of the
  factory spectra (sine stack, bell, choir, glass, breath), with the active count settable from 2 to 4.
  The full spectral payload is written into the project state, so a preset restores the exact spectrum
  it was authored against — not just the name of a factory state.
- **Host-synced spectral travel** — The morph journey can free-run at its own rate or lock to the host
  tempo across eight note divisions. The synced rate is recomputed every block, so a tempo ramp is
  followed rather than frozen at whatever the tempo was when you last touched a control; with sync off,
  or when the host reports no valid tempo, the free-running rate is used unchanged.
- **A seed control** — Sixteen curated seeds. The seed spreads across the voices so no two drift
  identically, and re-selecting one makes a performance reproducible.
- **Body and atmosphere switches that were previously internal** — Input AGC and resonator bypass on
  the continuous body; freeze, grain envelope shape, grain position, per-grain pitch and their spreads
  on the atmosphere. The resonator bypass crossfades over 10 ms and re-tunes the waveguide on the way
  back in, so it is a usable performance control rather than a debug switch.

### Changed

- **Project state is version 2** — 2532 bytes, up from 36. A version-1 stream is a strict byte prefix
  of a version-2 one, so **projects and presets saved with 0.1.0 load unchanged**; the parameters that
  did not exist then come back at their registered defaults.
- **Loading a preset now reaches the engine** — Every restored value is pushed to the DSP after the
  state is read and after any sample-rate change, rather than only when a control is next moved. The
  same push runs on re-prepare, so switching sample rate no longer leaves voices on prepare-time
  defaults.
- **Parameter moves are continuous by construction** — Every automatable parameter is either smoothed
  inside the component it feeds or pushed through a smoother on the way in, on a 64-sample control
  grid. Sweeping any one of them is free of steps in the output.

### Performance

- Measured at 48 kHz / 512-sample blocks with **all 91 parameters at non-default values** and a
  deliberately worst-case space engine (larger than the one the plugin actually prepares, so the shipped
  configuration has margin on top), the composed chain costs **24.21 % of one core at the shipped 8
  voices** (worst of six best-of-16 runs) against the 25 % ceiling, and **47.36 % at 16 voices** —
  recorded, non-gating, and the reason the roadmap's full-poly budget is now stated at 8 voices. The
  parameter-push machinery itself is **0.31 % of one core** in its worst case (all 91 changing every
  block) and effectively free when nothing is moving.
- Reported latency is unchanged at 1024 samples.

### Known limitations

- The editor is still the 0.1.0 placeholder. Every parameter is host-automatable and has a control tag,
  but only the original eight have on-screen controls — the interface is Phase 11.
- The effects roster (spectral freeze, spectral delay, tape-like saturation, stereo wandering) is
  Phase 10; IDs 1400+ are reserved for it and unregistered.
- Spectral states are chosen from the factory set. Editing individual partials, and the authoring
  functions behind it, ship with the per-partial editor in Phase 11.
- No MPE or per-note expression. It ships in Phase 13; the event input is still the whole note surface.
- No factory presets ship.

## [0.1.0] - 2026-07-31

First cut of the plugin. Seraphis has existed since Phase 1 as a set of KrateDSP
components with unit tests and no way to play them; this release wraps the
finished engine in a VST3 you can load, hold a note on, and hear. The sound
design surface is deliberately not here yet — this is the scaffold the parameter
work (Phase 9), the effects (Phase 10) and the interface (Phase 11) are built on.

### Added

- **Seraphis is a plugin** — A registered VST3 instrument (`Seraphis.vst3`, AU `aumu`/`Srph`/`KrAt`, bundle `com.krateaudio.seraphis`) with one event input and one stereo output. Holding a note runs the whole Phase 1-7 chain — harmonic cloud, spectral evolution, life modulators, continuous resonant body and granular atmosphere per voice, then the Aether space engine and the output stage — and produces audio. Eight voices by default, sixteen available.
- **The space engine is in the signal path, not after it** — Each block goes engine, then Aether reverb, then the engine's output stage, so the saturator and the true-peak limiter are the last things the audio meets and the reverb tail is inside the instrument rather than bolted on behind it. Voice bloom events drive the reverb's shimmer bloom directly, so a new note seeds the space it sounds in.
- **Three global controls** — Master gain (smoothed per sample, so moving it never zippers), voice count (1-16), and an output soft limit that can be switched off. All three are saved with the project.
- **Five macros: Dream, Bloom, Dissolve, Gravity, Entropy** — Present, saved and restored, and shown in the placeholder editor, but **inert in this release**: moving them changes nothing you can hear. They ship now so that the parameter IDs, the defaults and the saved state are fixed before anything depends on them; Phase 9 wires them to the engine. Their defaults are the documented neutral position, so a Phase 9 build will sound the same as this one until you move them.
- **Presets scan the `Textures` category** — The preset browser is live and reads `Krate Audio/Seraphis/Textures`. The category ships empty; the factory library is Phase 12. The name is permanent — later releases add categories beside it and never rename it, because a rename orphans every preset saved against it.
- **1024 samples of reported latency** — Constant at every sample rate. Spectral diffusion ships enabled, and the plugin reports its delay so the host can compensate.

### Known limitations

- The editor is a placeholder: one panel of stock controls, enough to prove the parameter wiring. The real interface is Phase 11.
- Only the three global parameters and the five macros are exposed. Every engine parameter — cloud, morph and entropy, life modulators, body, atmosphere, aether — is Phase 9, and the effects (spectral freeze, spectral delay, tape saturation, stereo wandering) are Phase 10.
- No MPE or per-note expression. The event input is the whole note surface.
- No factory presets ship.
