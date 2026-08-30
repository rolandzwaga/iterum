# Changelog

All notable changes to Seraphis will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] - 2026-08-05

Phase 12 — the library. Every release so far shipped an empty preset browser: one seeded category,
`Textures`, and nothing in it. This one fills it with **42 factory presets across seven categories**, and
it does so with a generator that links the shipped processor and asks it for its own state, so a preset can
never encode a layout the plugin does not write. **No new parameters, no state-format change and no DSP
change** — the registered surface stays at 107, the project stream stays version 3 at 2868 bytes, and a
0.4.0 project loads bit-for-bit unchanged. One playing bug found while auditioning the library is fixed.

### Added

- **42 factory presets, six in each of seven categories** — `Textures`, `Pads`, `Drones`, `Bells`,
  `Choirs`, `Motion` and `Cinematic`. `Textures` keeps its exact spelling and its existing directory: the
  category list is **additive-only**, because a rename orphans every preset a user has already saved
  against the old name. Each category is a real directory under the installed preset folder and a real tab
  in the browser.
- **The library covers the instrument, not a corner of it** — Every body material (Glass, Strings, Metal
  Plate, Chamber, Ice), every factory spectrum (Sine Stack, Bell, Choir, Glass, Breath), both travel modes,
  both envelope modes, morph counts of 2, 3 and 4, and each of the three freeze switches both engaged and
  disengaged are exercised by at least one preset. The coverage is asserted by the test suite against the
  decoded preset state — not against the table the presets were authored from.
- **The presets are audibly different from each other, and that is measured** — All 861 preset pairs are
  compared on a normalised render of their sustain, and the closest pair sits at **0.0366** against a floor
  of **0.02**; the median pair is at 0.53. The floor is validated from below by an injected level-only twin
  of a real preset, which lands at 0.00021 — two orders of magnitude under the floor — so the criterion is
  known to be able to fail rather than merely observed to pass.
- **A preset generator that cannot drift from the plugin** — `seraphis_preset_generator` compiles the
  shipped processor, drives the authored parameter values through a real `process()` call, and writes what
  `getState()` produces. Nothing about the 2868-byte layout is duplicated in the tool, so there is no second
  copy of the format to keep in sync and no compatibility test standing in for one. It carries no
  UI-framework dependency, which is what lets the release pipeline build it on its Linux leg.
- **Generated presets are reproducible byte-for-byte** — `tools/check-preset-generator-determinism.js`
  runs the generator into two fresh directories and compares every byte, then runs it a third time over an
  existing tree and requires nothing changed. No timestamp, path string or random value reaches any preset
  byte. Measured over the shipped library: 42 files, 0 differing, 0 changed.
- **Every preset is proved to make sound, and to stay bounded** — Each one is rendered end to end at
  44 100 Hz and 48 000 Hz on a timeline derived from its own envelope and reverb settings: the sustain is
  required to be audible, the whole render finite and inside the output ceiling, and the tail is checked
  against the behaviour the preset's own freeze switches promise — a frozen preset must hold, and an
  unfrozen one must decay at the RT60 it declares. A four-note chord render of every preset checks the
  multi-voice sum as well.
- **Every preset stays inside the CPU promise** — All 42 ship at 8 voices or fewer with the output soft
  limit engaged, asserted from the saved state rather than from the authoring table, so no preset can
  quietly buy its sound with polyphony the 25 %-of-one-core budget does not have.
- **Preset metadata matches where the preset lives** — Each file carries the six-attribute `Info` block the
  browser reads, its `Name` matches the filename, and its category matches the directory it sits in. A
  preset that disagrees with its own directory would exist on disk and never appear in the browser; that is
  now a test failure instead of a silent absence.
- **A tooltip on every control** — All 110 bound views and the six session controls (mode toggle, drawer
  handle, Blend, Tilt, the tab bar and the slot bar) carry a short description of what the control does to
  the sound, rather than a restatement of its name.

### Fixed

> The six editor items below landed **after** the 0.4.0 entry was written and were never recorded against
> it. They are Phase 11 interface work, listed here because this is the first release entry that covers
> them.

- **The drawer tabs did nothing visible** — Clicking a tab switched the visibility of pages clipped inside
  the 30 px collapsed strip without opening the drawer. A tab click now opens it, and the handle keeps its
  own toggle behaviour.
- **Three controls were invisible click targets** — The preset button, the mode toggle and the drawer
  handle were bitmap-less on/off buttons drawn over mouse-disabled labels, with no pressed or active state.
  All three are real buttons now, and a test fails the build if a bitmap-less on/off button reappears in the
  interface description.
- **The constellation was barely visible** — Point radius was linear in amplitude with a zero minimum, so
  typical normalised amplitudes across 64 partials drew sub-pixel dots. The mapping is perceptual now, the
  maximum radius is larger, points are drawn with a glow under a solid core, and Edit mode is marked by a
  border so the mode switch is visible. An amplitude of exactly zero still draws at the minimum radius,
  which is what keeps a masked partial clickable.
- **Edit mode showed nothing with no note held** — With no live frame, Edit mode now draws the selected
  slot's authored partials against a fixed reference pitch, so a spectrum can be seen and dragged in
  silence. Frames that carry a release tail still draw as frames.
- **The selected slot never showed whether it was sounding** — When the selected morph slot does not
  contribute to the current blend, its border is drawn in amber instead of the accent colour.
- **Toggles read the same on and off** — The off state used a colour nearly as bright as the on state.
  Toggles now use a dark neutral for off, so on/off is legible at a glance. Segment bars keep their
  readable label colour for unselected entries, which are text rather than indicators.
- **A note played in the same buffer as a preset load could be completely silent** — Note events were
  dispatched at the start of a slice, *before* the block's parameter values reached the voices. A voice
  started in Growth envelope mode reads its envelope mode at the instant the note begins, so a note that
  arrived in the same buffer as the preset load (or as a change to the envelope-mode control) triggered
  against the *previous* block's mode, the growth envelope never started, and the voice was bit-silent for
  its entire life. Events are now scanned first and dispatched after the parameter push, so a voice always
  starts against current values. No sample was ever rendered on a stale value by the old order, which is why
  the other 36 parameter paths never showed it — this failure needed a note-on to read state, and only the
  envelope mode does.

### Changed

- **Preset categories are seven, and the list is closed to renames** — `Textures` is unchanged in both
  places the name is carried (the subcategory list and the filesystem directory), and both must always
  agree. Later phases may add categories beside these seven; none of them may be renamed.
- **Nothing about the plugin's saved format moved** — State is still version 3 and still 2868 bytes. Every
  factory preset carries the Phase 11 per-partial override block in its un-edited form: both bitmasks zero,
  all 64 pans at 0.0. No factory preset ships a per-partial edit, so loading one never has to reconcile an
  authored override table against the spectrum it came from.
- **The editor is built from the shared component library** — The eleven toggles, the seven drawer tabs,
  the four morph-slot buttons and the preset button now use the same shared controls every other Krate
  plugin uses, instead of stock check boxes and capsule buttons. The tab and slot rows each collapse into a
  single segmented bar. No new view class was added and no parameter binding changed.

### Performance

- **The instrument's dominant cost was located by measurement, not by guess** — Stage timers inside
  `process()` (compiled out of every shipping path) attributed the whole-block wall time: the voice sum is
  ~91 % of it, and all processor plumbing combined is **0.06 %**. Within the voice, the atmosphere grain
  sweep alone measured **25.4 % of one core** at the 8-voice operating point — about 80 % of the whole
  overshoot of the 25 % promise. This refutes the earlier assumption that the remainder lived in the
  plugin-side plumbing, and it is why the optimisation work went where it did.
- **The atmosphere grain sweep was restructured grain-major** — Instead of re-loading every grain's state
  once per sample, each grain now renders its whole span with its state in registers, with the per-sample
  bookkeeping (births, retirements, capture, scheduler) kept sample-exact in its own pass. Read positions,
  interpolation weights and admission decisions are unchanged; the only difference is the order in which
  per-sample contributions are summed, which is last-ULP and inside every render tolerance.
- **The grain span reads through a SIMD gather kernel** — Index arithmetic is computed scalar and
  bit-identically to the scalar reader; the memory phase gathers, interpolates and accumulates per lane
  with no cross-lane reduction, so splitting a block differently cannot move any sample. Measured at
  matched machine state: **8.90–8.96 ns → 8.29 ns per grain-sample**, ~12 % off the sweep.
- **A settled blur of exactly zero no longer pays for a polar round-trip** — At the shipped default the
  per-bin phase perturbation is identically zero, so the spectrum is left untouched rather than
  re-rounded through `atan2`/`sin`/`cos`. The same per-bin random draws are still consumed, so the render
  stream is unchanged. Any non-zero blur, however small, takes the full path — this is an exact-identity
  gate, not a threshold. The measured saving is near zero; the earlier "~2.4 %" figure attributed to it was
  the whole containing row, and that overclaim is corrected here rather than left standing.
- **The 25 %-of-one-core target is not met yet.** The measurements above are progress against it, not a
  claim of arrival, and that is why this version's release verdict is deferred.
- Reported latency is unchanged at 1024 samples.

### Known limitations

- **The release verdict for this version is `DEFERRED`.** The library, the generator and the harness are
  complete; the release itself waits on the performance gate that precedes it, and no green verdict is
  recorded until that gate's own measured criteria are met.
- No MPE or per-note expression. It ships in Phase 13, which is also the release that claims `1.0.0`.
- The drawer opens and closes instantly; there is no slide animation.
- The window is a fixed 1000 × 700.

## [0.4.0] - 2026-08-04

Phase 11 — the interface. Every release so far shipped the placeholder editor from 0.1.0: 107
host-automatable parameters and eight on-screen controls. This one replaces it with the organism-first
editor the roadmap describes — the live partial constellation fills the window and **is** the interface,
the five macros orbit it, and the deep parameters live in a pull-up drawer. It also opens the two paths
that make that possible in both directions: the engine now publishes what it is doing, and the editor can
now author individual partials. **No new parameters** — the registered surface stays at 107 and every
registered type is unchanged.

### Added

- **The cloud view** — A live constellation of the harmonic cloud: one point per partial, x = stereo
  position, y = frequency on a fixed 20 Hz – 20 kHz log axis, size = amplitude. It redraws at 30 Hz and
  only when the engine actually published something new. The frequency axis is deliberately **fixed, never
  autoscaled** — with stereo spread at zero all 64 points are coincident, and an autoscaled axis would
  divide by zero exactly there.
- **Five macro rings, and they show the real DSP** — Dream, Bloom, Dissolve, Gravity and Entropy are large
  ring knobs anchored around the view. Turning one visibly perturbs the constellation, and the motion you
  see is **the engine's actual response read back out of it** — there is no view-local animation, no
  synthetic displacement, and nothing interpolating toward a target the DSP is not producing.
- **A pull-up drawer for the deep parameters** — Seven tabs (Cloud, Morph, Body, Atmos, Aether, FX,
  Life/Env), collapsed to a 30 px tab strip or open to the bottom 40 % of the window. All seven pages are
  present at once with one visible, so no parameter is ever unreachable. Opening the drawer never removes,
  hides or resizes the cloud view — the drawer grows up over it and the constellation keeps rendering.
- **Per-partial editing** — An Observe | Edit toggle on the cloud view. In Edit mode a partial can be
  dragged in frequency and amplitude, panned, and masked or unmasked with a click; a Blend A→B slider and
  an absolute Tilt dB/oct control sit in the same mini-toolbar. A masked partial keeps drawing as a hollow
  ring after its amplitude has smoothed to zero, so it stays a click target for the un-mask gesture rather
  than disappearing and becoming unreachable.
- **Editing works with no note held** — Authoring uses a fixed reference pitch when nothing is sounding,
  so a spectrum can be built silently and then played, not only adjusted while a note rings.
- **The three `SpectralState` authoring mutators** — `setPartial`, `blendStates` and `tiltState`, in the
  shared DSP library. They exist for exactly one consumer, the editing surface, which is why they ship now
  rather than with the states themselves. Each one is total: adversarial input — out-of-range ratios,
  non-monotone neighbours, amplitudes outside 0…1, partial counts outside 0…64, non-finite arguments — is
  rejected **before the first store**, so a rejected edit leaves the state byte-for-byte unchanged rather
  than half-written. A ratio dragged past its neighbour clamps; it never swaps.
- **Edits reach a sounding voice immediately** — Setting a spectral state on a voice is no longer gated to
  configure time. The morph engine absorbs a live swap through its own fade, so a partial dragged while a
  chord is held is audible on that chord instead of waiting for the next note-on.
- **Dissolve and Entropy now reach the effects** — The macro matrix gains a fourth target owner: Dissolve
  drives the spectral-delay send and Entropy drives the stereo-wander depth. This closes the 0.3.0 known
  limitation that the macros moved the engine but not the effects stage. It is purely additive — every
  pre-existing macro target keeps its index and its response bit-for-bit.
- **Per-partial pan and mask fan-outs** — The engine can now push a pan position or a mask to the same
  partial index on **all sixteen** voice slots, including ones the allocator has not handed out yet, so a
  voice stolen or allocated later already carries the override.

### Changed

- **Project state still says version 3** — The per-partial override table is written as a 272-byte
  `[partials]` block appended after `[effects]`, which keeps every 0.1.0, 0.2.0 and 0.3.0 stream a strict
  byte prefix of a Phase 11 one. **Projects and presets saved with any earlier release load unchanged**;
  the override table comes back empty, which is the un-edited configuration. The stream is 2868 bytes.
- **Editor session state is not a parameter** — Mode, drawer state, active tab, selected morph slot and the
  per-partial override table are session state. The controls that carry them use a tag namespace starting
  at 9000, outside the registered ID space, so none of them can collide with a parameter, be automated by
  the host, or be counted as a parameter binding.
- **The two directions use two transports, on purpose** — The engine → editor path is a DataExchange queue
  carrying one 808-byte frame per processed block, piggybacked on the existing mechanism rather than a new
  one. The editor → engine path is a discrete 12-byte message per gesture step. Nothing about editing
  travels on the frame queue, and no frame data travels on the message channel.
- **Edit messages are treated as untrusted input** — An unknown message kind, an out-of-range slot or
  partial index, a wrong payload size or a non-finite value is dropped silently. The mutators' own
  rejection is the second line of defence, not the first.

### Performance

- **A closed editor costs one atomic load per block.** The frame producer is gated on an editor actually
  being open. With none open, the gate is the entire cost: no focus voice is picked, no frame is built and
  nothing is published. The gated-closed cost is enforced by the test suite, not asserted here.
- With the editor open the frame is published **once per processed block**, never once per internal
  64-sample slice, and the whole composed chain still fits under the **25 % of one core at 8 voices**
  budget the roadmap sets. A drag in flight is rate-limited to the same 30 Hz the view redraws at, with a
  mandatory flush at gesture end so the value you released on is never swallowed.
- Reported latency is unchanged at 1024 samples.

### Known limitations

- No factory presets ship. The library, and the preset categories beyond the seeded `Textures`, are
  Phase 12.
- No MPE or per-note expression. It ships in Phase 13.
- The drawer opens and closes instantly; there is no slide animation.
- The window is a fixed 1000 × 700.

## [0.3.0] - 2026-08-03

The last stage of the global bus. 0.2.0 exposed the engine; this release puts the four effects the
roadmap names — spectral freeze, spectral delay, tape-like saturation and stereo wandering — behind the
voices and the space engine, and **fixes the order they run in** so it is a decision on record rather
than an accident of where each call landed. Sixteen new parameters, IDs 1400–1443. No DSP algorithm
changed and no new DSP class was written: every stage is composed from components that already shipped.
At the defaults the whole stage is bypassed, so a 0.3.0 render sounds exactly like a 0.2.0 one.

### Added

- **A spectral delay, as a parallel send** — Delay time to 2 seconds, per-bin spread up to another
  2 seconds in three directions (low→high, high→low, centre→out), feedback, a feedback *tilt* that makes
  highs or lows ring longer than the rest, diffusion, stereo width, and a return mix. It is a **send**,
  not an inline blend: the bus is tapped, delayed fully wet, and summed back. That is deliberate — the
  component's own dry/wet blends a current-block dry against a wet that is a full FFT frame late, which
  would put a comb filter across the whole instrument. Blending outside it avoids that entirely.
- **The send is block-size invariant** — It runs behind a fixed-size accumulator, so its contribution
  does not move when the host changes buffer size or when incoming MIDI splits a block. Fed directly, the
  underlying component lands its output stream a hop earlier or later depending on how many analysis
  frames happened to be ready, which would be a permanent half-frame offset of the whole effect rather
  than a start-up transient.
- **Tempo-synced delay time** — Ten note divisions from 1/64 triplet to 1/8 triplet, defaulting to 1/16.
  The labels name the periods the delay **actually produces**, which is not what the component's own
  documentation claimed; the values were read back out of the note-division table rather than copied from
  the comment.
- **Spectral freeze** — Captures the spectrum passing through the send at the moment you engage it and
  holds it, crossfading in over 75 ms with a slow phase drift underneath so it settles into a texture
  instead of a static resonance. It is a different control from the Aether freeze at ID 1204, and the two
  are independent.
- **Stereo wandering** — A mid/side width control from mono to 200 %, plus two independent Brownian
  drifts that move the width and the stereo azimuth on their own, with a shared rate and separate depths.
  Both depths default to 0, so the field only moves when you ask it to.
- **Tape saturation is now a control** — The output saturator has been in the chain since 0.1.0 at a fixed
  amount; it is now a parameter, defaulting to exactly the value it has always run at. Only the *amount*
  is exposed. Drive stays a compile-time 0 dB, which is what keeps the "gentle ceiling, not distortion"
  property structural rather than a matter of where you leave the knob.
- **The chain order is on record** — voices → Aether → master gain → spectral-delay send → stereo
  wandering → tape saturator → true-peak limiter → voice-bloom lifecycle. Every Phase 10 stage that
  changes level lands **before** the output stage, never after it: a multiply behind the limiter makes the
  ceiling unsatisfiable by construction. The order is asserted by a test, not by this paragraph.
- **The delay is reproducible** — The spectral delay seeds its randomness from the global seed control
  rather than from its own address, so the same seed and the same notes give the same render. As prepared,
  the component seeds itself from a pointer value, which is different on every run.

### Changed

- **Project state is version 3** — A version-1 or version-2 stream is a strict byte prefix of a
  version-3 one, so **projects and presets saved with 0.1.0 or 0.2.0 load unchanged**; the sixteen
  effects parameters come back at their registered defaults, which is the bypassed configuration.
- **Feedback is pre-compensated against the tilt** — The registered feedback maximum is 0.95, but the
  underlying per-bin loop multiplies feedback by a tilt factor that reaches 2.0, and clamps the product at
  1.2 rather than at the registered range. Left alone, feedback 0.95 with full positive tilt gives 243 of
  513 bins a loop gain above 1 and those bins sustain forever. The value pushed to the component is
  compensated so the worst per-bin gain is the registered feedback, at every tilt setting.
- **Reported latency is unchanged at 1024 samples** — The send's own FFT latency is not added to it. The
  send *is* a delay, so its ~21 ms is absorbed into its delay time; reporting it would charge every user
  21 ms of latency for an effect most of them have switched off, and reporting it conditionally would mean
  the plugin changed its latency mid-performance.

### Performance

- **Nothing at the defaults.** When the send is neither active nor draining, the processor does not copy
  into the accumulator, does not call the spectral delay, and does not touch its buffers; the wander stage
  is skipped the same way. The effects stage is gated at **≤ 0.10 % of one core** in that state.
- Fully active — every one of the sixteen at its most expensive setting — the stage is gated at
  **≤ 2.5 % of one core** at 48 kHz / 512-sample blocks, sized so that the composed everything-on chain
  still fits under the **25 % at 8 voices** budget the roadmap sets. Engaging the send re-initialises the
  delay lines; that block is bounded and measured rather than assumed, as is a seed change.
- All four figures are enforced by the test suite under a single cold measurement protocol, not asserted
  here.

### Known limitations

- **The effects ship inert.** Delay mix, wander depth and azimuth depth all default to 0 and saturation
  defaults to the value that was already running, so nothing changes until you move something. This is a
  deliberate deviation from the roadmap's "nothing is ever static" principle for these sixteen
  parameters: it is what lets the release prove it changed no existing sound.
- **The macros do not reach the effects.** Dream, Bloom, Dissolve, Gravity and Entropy move the engine,
  not this stage. Routing a macro axis into the delay send or the wander is new routing rather than
  wiring, and it would put a second writer on parameters the host also automates — it belongs with the
  phase that owns the performance surface (Phase 11), and the shipped patches that use it with Phase 12.
- The frequency shifter named in the roadmap's reuse inventory does **not** ship. It stays available to a
  later phase.
- Tape saturation exposes amount only. Drive is not user-facing and there is no plan for it to be.
- The editor is still the 0.1.0 placeholder. The sixteen new parameters are host-automatable and carry
  control tags, but have no on-screen controls — the interface is Phase 11.
- Spectral states are still chosen from the factory set; per-partial editing is Phase 11.
- No MPE or per-note expression. It ships in Phase 13.
- No factory presets ship.

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
