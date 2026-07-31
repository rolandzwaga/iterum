# Changelog

All notable changes to Seraphis will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
