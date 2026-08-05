# Seraphis — User Manual

## Introduction

Seraphis is a **spectral organism synthesizer**. Each voice is a cloud of 64 harmonic partials
driven by a spectral-evolution layer, resonated through a continuous modal body, with a parallel
granular atmosphere breathing alongside it. A family of slow autonomous *life modulators* —
Brownian drift, breathing, tides, orbits — animates all of it, so nothing you play is ever static.
The integrated **Aether** space engine (an infinite-tail reverb with shimmer, bloom, and spectral
diffusion) sits *inside* the signal path rather than behind it: the space is part of the instrument,
not an effect appended to it.

Seraphis is the ethereal counterpart to Ruinae. Where Ruinae is instability, chaos and aggression,
Seraphis is emergence, resonance and weightlessness. It is built for textures, pads, drones, bells,
choirs and cinematic atmospheres — sounds that move on their own and never quite repeat.

**Formats:** VST3 (Windows, macOS, Linux) and Audio Unit (macOS). One MIDI input, one stereo output.

## Signal Flow

```
MIDI in
  │
  ▼
Harmonic Cloud ──── 64 additive partials per voice
  │                 (richness, inharmonicity, tilt, mutation, drift, per-partial envelopes)
  ▼
Spectral Morph ──── travels between up to 4 spectral states
  │                 (entropy, bloom, external position or autonomous spline travel)
  ▼
Continuous Body ─── modal resonator (Glass / Strings / Metal Plate / Chamber / Ice)
  │                 + a slow post-body decay cloud wash
  ▼
Granular Atmosphere (parallel layer: grains fed from the voice, blur, freeze)
  │
  ▼
Aether ──────────── the space engine, in the path: infinite FDN reverb,
  │                 shimmer taps, bloom stage, spectral diffusion, breath & tide
  ▼
Effects ─────────── tape saturation → spectral delay → spectral freeze → stereo stage
  │                 (width, wander, azimuth)
  ▼
Output ──────────── master gain → true-peak limiter (always on)
```

The five **macros** — Dream, Bloom, Dissolve, Gravity, Entropy — reach across every stage of this
chain at once, and the **life modulators** run underneath it continuously. Both are described in
their own sections below.

## Interface Overview

The editor is a fixed 1000 × 700 window built *organism-first*: the entire window is a live view of
the sound, and the controls orbit it.

- **The constellation** — the whole central area is the **cloud view**: one point per partial of the
  loudest sounding voice, drawn live at 30 Hz. Horizontal position is the partial's stereo
  placement, vertical position is its frequency on a fixed 20 Hz – 20 kHz log axis, and point size
  is its amplitude. You watch drift, mutation, morphing and masking actually happen.
- **Five macro rings** — large ring knobs anchored in the corners and bottom-centre:
  Dream (top-left), Bloom (top-right), Gravity (bottom-left), Dissolve (bottom-right),
  Entropy (bottom-centre).
- **The header bar** — master gain, polyphony, soft-limit toggle, seed selector, three freeze
  switches (Atmosphere, Aether, Spectral), and the preset browser button.
- **The drawer** — a pull-up panel along the bottom edge holding every deep parameter across seven
  tabs: **Cloud, Morph, Body, Atmos, Aether, FX, Life/Env**. Click the **DEEP** handle (chevron) to
  open or close it. Every parameter stays automatable from the host whether or not the drawer is
  open.
- **Observe | Edit** — a mode toggle at the bottom-left of the constellation. In Observe mode the
  cloud view is a display; in Edit mode it becomes an instrument panel (see *Editing Partials*).

Every control in the editor carries a tooltip — hover over anything to get a one-line description.

## The Cloud View

### Observe mode

The default. The constellation shows the loudest currently-sounding voice: partials rise and fall
with their envelopes, drift sideways as the stereo field moves, brighten and dim with mutation, and
travel vertically as morphing retunes them. With no note held, the view shows the selected morph
slot's spectrum at a reference pitch, so the display is never empty.

### Edit mode — editing partials

Flip the toggle to **EDIT** and the constellation becomes editable:

- **Drag a partial** vertically to change its frequency ratio, horizontally to change its stereo
  position; drag its size to reshape amplitude.
- **Click a partial** to mask or unmask it. A masked partial keeps drawing as a hollow ring so it
  remains a click target.
- **Blend A to B** — a slider that blends the edited spectral state from slot A toward slot B.
- **Tilt dB/oct** — a slider that tilts the whole edited spectrum in dB per octave.
- On the Morph tab, the **Edit slot** selector (A/B/C/D) chooses which morph slot your gestures
  write to.

Editing works with or without a note held — with no note sounding, authoring uses a fixed reference
pitch. Edits reach an already-sounding voice immediately, absorbed through the morph engine's own
fade so there is never a click. Your authored states are saved with the project and with presets,
and restored exactly.

## Macros

The five macro rings are the intended playing surface of Seraphis. Each macro sweeps a curated set
of targets across the whole engine — voice, space and effects at once. Two things are worth knowing
about how they work:

1. **The deep parameters set the origin the macros travel from.** Moving a drawer parameter moves
   the *base* a macro's sweep starts at; the macro then adds its travel on top. The two never fight
   over a value.
2. **All macros default to neutral** (0 for all, centre for Gravity). At neutral a macro contributes
   exactly nothing — the drawer parameters alone describe the sound.

### Dream — *"Purifies the harmonics, deepens the reverb and calms entropy"*

| Target | Travel at full Dream |
|---|---|
| Cloud Inharmonicity | → 0 (pure harmonic grid) |
| Cloud Gravity | → 0 |
| Cloud Mutation | → 0 |
| Morph Entropy | → 0 |
| Orbit Depth | → maximum |
| Aether Mix | 0.35 → 0.70 |
| Aether Size Breath | → 0.80 |
| Aether Tide Depth | → 0.80 |

Dream removes disorder from the spectrum while deepening the space and its slow breathing: at full
Dream the cloud is a pure, weightless harmonic stack inside a living reverb.

### Bloom — *"Opens upper partials, shimmer and width; morphs toward brightness"*

| Target | Travel at full Bloom |
|---|---|
| Cloud Tilt | +9 dB/oct |
| Cloud Richness | → 1.0 |
| Cloud Stereo Spread | → 0.95 |
| Voice Width | +30 % |
| Morph Position | → slot B |
| Aether Shimmer Octave | → 0.60 |
| Aether Shimmer Fifth | → 0.40 |
| Aether Bloom Send | → 0.60 |
| Aether Width | → maximum |

### Dissolve — *"Melts the tone into granular fog, blur and slower envelopes"*

| Target | Travel at full Dissolve |
|---|---|
| Atmosphere Level | → maximum |
| Atmosphere Blur | → 0.40 |
| Cloud Attack | 0.05 s → 2 s |
| Envelope Stage 0 | 2 s → 6 s |
| Envelope Stage 1 | 4 s → 9 s |
| Envelope Release | 8 s → 10 s |
| Spectral Delay Mix | → 0.20 |

### Gravity — *"Air-to-stone axis"* (bipolar)

Gravity is the one **bipolar** macro: its centre is neutral, turning it *down* moves toward **air**,
up toward **stone**.

| Target | Air (full down) | Stone (full up) |
|---|---|---|
| Cloud Richness | 0.95 | 0.25 |
| Body Damping | 0.0 | 0.50 |
| Cloud Tilt | +8 dB/oct | −8 dB/oct |
| Aether Size | 0.05 | 0.95 |

### Entropy — *"Decays order into jitter, detune and partial death and rebirth"*

| Target | Travel at full Entropy |
|---|---|
| Morph Entropy | 0.20 → 0.50 |
| Cloud Drift Depth | → 50 cents |
| Atmosphere Drift Depth | → maximum |
| Stereo Wander Depth | → 0.50 |

## Header Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| **Gain** | 0 – 200 % (−80 dB … +6 dB) | unity (centre) | Overall output level of the whole instrument |
| **Voices** | 1 – 16 | 8 | Maximum simultaneous voices |
| **Limit** | on/off | on | Tape-style output saturation. **The true-peak limiter always stays on** — this toggle only adds or removes the tape colouration in front of it |
| **Seed** | Seed 1 – Seed 16 | Seed 1 | Random seed for drift, grains and reverb modulation; changing it re-rolls the texture without changing any parameter |
| **Atmos Frz** | on/off | off | Suspend the atmosphere on its current material |
| **Aether Frz** | on/off | off | Hold the reverb tail forever, conserving its energy |
| **Spec Frz** | on/off | off | Capture and hold the current spectrum indefinitely |
| **Presets** | — | — | Opens the preset browser |

The three freeze switches in the header are duplicates of the same parameters on the Atmos, Aether
and FX tabs, placed within reach while performing.

## The Drawer: Cloud Tab

The harmonic cloud is the additive core — 64 partials per voice.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Richness** | 0 – 100 % | 60 % | Number of active partials and how fast they roll off |
| **Inharm** | 0 – 0.1 | 0.03 | Stretches partials off the harmonic grid, piano toward bell |
| **Tilt** | −12 – +12 dB/oct | 0 | Spectral slope: dark below zero, bright above |
| **Mutation** | 0 – 100 % | 25 % | Rate of slow random re-weighting of partial amplitudes |
| **Gravity** | −1 – +1 | +0.2 | Pulls partial ratios toward (or pushes away from) the pure harmonic grid |
| **Drift** | 0 – 50 ct | 0 | Depth of the per-partial pitch drift, in cents |
| **Drift Smth** | 0 – 100 % | 50 % | How slow and smooth the per-partial drift is |
| **Spread** | 0 – 100 % | 35 % | Spreads the individual partials across the stereo field |
| **Attack** | 0.05 – 30 s (log) | 0.05 s | Attack of each partial's own envelope |
| **Decay** | 0.05 – 60 s (log) | 0.5 s | Decay of each partial's own envelope |
| **Env Spread** | 0 – 100 % | 0 % | Staggers per-partial attack times so the cloud blooms in |

> **Tip:** *Env Spread* is what makes a chord "assemble" — with a slow attack and full spread, the
> partials enter one after another and the note builds itself in front of you (hear *Bell Garden*).

## The Drawer: Morph Tab

The morph engine travels the cloud between up to four **spectral states** — factory spectra loaded
into slots A–D, or your own states authored in Edit mode.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Entropy** | 0 – 100 % | 20 % | Decays spectral order: amplitude jitter, phase smear, then partial death |
| **Bloom** | 0 – 0.6 | 0 | Delays high partials behind low ones so a morph blooms upward |
| **Position** | 0 – 3 | 0 | Where the sound sits between the spectral slots (0 = A, 3 = D) |
| **Travel** | External / Spline | External | External follows the Position knob; Spline travels its own trajectory |
| **Rate** | 0.0017 – 1 j/s (log) | 0.0017 | Speed of autonomous travel, in journeys per second |
| **Sync** | Free / Synced | Free | Lock the morph travel rate to host tempo |
| **Sync Note** | 1/16 … 8 Bars | 1 Bar | Length of one journey when Sync is on |
| **Waypoint** | 0.5 – 30 s (log) | 2 s | Seconds between spline trajectory waypoints |
| **States** | 2 / 3 / 4 | 2 | How many slots the morph travels between |
| **Slot A–D** | Sine Stack / Bell / Choir / Glass / Breath | A: Sine Stack, B: Glass | Factory spectrum loaded into each slot |
| **Edit slot** | A / B / C / D | A | Which slot the cloud-view editing gestures write to |

**Position always spans 0–3** regardless of the States count. The five factory spectra are starting
material — any slot can be overwritten by hand in Edit mode, and authored slots survive saving and
preset load exactly.

## The Drawer: Body Tab

The cloud speaks through a **continuous modal body** — a physical resonator that can morph between
materials.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Material** | Glass / Strings / Metal Plate / Chamber / Ice | Glass | Resonant body the cloud speaks through |
| **Resonance** | 0 – 100 % | 70 % | How long and how strongly the body rings |
| **Damping** | 0 – 100 % | 25 % | Damps the body's high modes, darker and shorter |
| **Key Track** | 0 – 100 % | 100 % | How far the body retunes with the played note |
| **Drive** | 0 – 4 | 1.0 | Excitation level driven into the resonator |
| **Mix** | 0 – 100 % | 100 % | Blend between the dry cloud and the resonated body |
| **Cloud Mix** | 0 – 100 % | 25 % | Amount of the slow decay-cloud wash after the resonator |
| **Cloud Decay** | 0.1 – 30 s (log) | 4 s | Decay time of the post-body wash |
| **Cloud Size** | 0 – 100 % | 100 % | Size of the diffusion network behind the wash |
| **Cloud Damp** | 0 – 100 % | 30 % | Darkens the post-body wash as it decays |
| **Width** | 0 – 100 % | 100 % | Stereo width of the body output |
| **Input AGC** | on/off | on | Normalizes the level feeding the resonator |
| **Res Bypass** | on/off | off | Bypasses the modal resonator, keeping only the wash |

Note that **Mix defaults to 100 %** — by default you hear the instrument *through* the Glass body.
Turn Mix down for the raw additive cloud. *Res Bypass* with some Cloud Mix gives a body-less
diffuse wash (hear *Warm Static*); turning **Input AGC off** lets the Drive knob genuinely overload
the resonator (hear *Iron Lung*).

## The Drawer: Atmos Tab

A granular layer running in parallel with the voice: grains are captured from the sound itself and
re-scattered.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Level** | 0 – 2 | 0.5 | Level of the granular atmosphere layer |
| **Blur** | 0 – 100 % | 0 % | Smears grain phase into fog, from material toward mist |
| **Density** | 0.1 – 20 gr/s (log) | 4 | Grains per second; higher overlaps into a solid wash |
| **Grain Sec** | 0.05 – 30 s (log) | 4 s | Length of each grain |
| **Drift** | 0 – 100 % | 30 % | Depth of the per-grain pitch drift |
| **Pan Spread** | 0 – 100 % | 70 % | Spreads the grains across the stereo field |
| **Decorr** | 0 – 100 % | 50 % | Decorrelates the channels for a wider, more diffuse image |
| **Freeze Mix** | 0 – 100 % | 0 % | Blend of the frozen spectrum into the grain stream |
| **Freeze** | on/off | off | Suspend the atmosphere on its current material |
| **Drift Smth** | 0 – 100 % | 70 % | How slow and smooth the per-grain pitch drift is |
| **Drift Range** | 0 – 12 st | 2 st | Maximum per-grain pitch drift |
| **Jitter** | 0 – 100 % | 50 % | Randomizes grain start timing, looser and less rhythmic |
| **Position** | 0 – 30 s | 1 s | How far back in the capture buffer grains are read |
| **Pos Spread** | 0 – 100 % | 30 % | Scatters the grain read positions around Position |
| **Pitch** | −24 – +24 st | 0 | Transposes the grains |
| **Pitch Spread** | 0 – 100 % | 15 % | Random pitch scatter between individual grains |
| **Grain Env** | Hann / Trapezoid / Sine / Blackman / Linear / Exponential | Hann | Window shape of each grain, from soft to abrupt |

> **Tip:** Position at 0 reads grains right at the write head — the atmosphere shadows the voice
> almost live. Long positions with wide Pos Spread turn the buffer into a memory the instrument
> keeps digging through (hear *Aftermath*).

## The Drawer: Aether Tab

The space engine. Aether sits inside the signal path — freezing it, breathing it and blooming it
are part of playing the instrument.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Mix** | 0 – 100 % | 35 % | Amount of Aether reverb in the output |
| **Size** | 0 – 100 % | 50 % | Size of the space, from small chamber to impossible |
| **Density** | 0 – 100 % | 70 % | Diffusion density; higher smooths the tail |
| **Decay** | 0.5 – 60 s (log) | 4 s | Reverb decay time |
| **Freeze** | on/off | off | Hold the tail forever, conserving its energy |
| **Dimension** | 0 – 100 % | 35 % | Space character, from flat plate to N-dimensional hall |
| **Damping** | 0 – 100 % | 40 % | Darkens the reverb tail as it decays |
| **Pre-Delay** | 0 – 200 ms | 0 | Delay before the reverb starts |
| **Mod Depth** | 0 – 100 % | 25 % | Depth of modulation inside the reverb delay lines |
| **Mod Smth** | 0 – 100 % | 60 % | How slow and smooth the internal modulation is |
| **Shim Oct** | 0 – 100 % | 0 % | Send into the octave-up shimmer tap inside the tail |
| **Shim 5th** | 0 – 100 % | 0 % | Send into the fifth-up shimmer tap |
| **Bloom Send** | 0 – 100 % | 0 % | Send into the bloom stage that reinforces held partials |
| **Bloom Decay** | 0 – 100 % | 50 % | How long the bloom sympathetic resonance sustains |
| **Spec Diff** | 0 – 100 % | 0 % | Smears the tail spectrally for an underwater character |
| **Breath** | 0 – 100 % | 20 % | Depth of the slow breathing of the space size |
| **Tide** | 0 – 100 % | 20 % | Depth of the very slow tidal drift through the space |
| **Width** | 0 – 100 % | 100 % | Stereo width of the reverb tail |

## The Drawer: FX Tab

The final stage: saturation, a spectral delay, spectral freeze, and the stereo stage.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Saturation** | 0 – 100 % | 15 % | Tape-style saturation on the output, a gentle ceiling |
| **Delay Mix** | 0 – 100 % | 0 % | Amount of the spectral delay send in the output |
| **Delay Time** | 0 – 2000 ms | 250 ms | Delay time when Sync is off |
| **Spread** | 0 – 2000 ms | 0 | Spreads the delay time across frequency bins |
| **Spread Dir** | Low → High / High → Low / Center → Out | Low → High | Direction the per-bin spread runs across the spectrum |
| **Feedback** | 0 – 95 % | 35 % | Delay regeneration, internally compensated to stay stable at any Tilt |
| **Delay Tilt** | −1 – +1 | 0 | Biases the delay feedback toward low or high bins |
| **Diffusion** | 0 – 100 % | 30 % | Smears the delay repeats into a wash |
| **Delay Width** | 0 – 100 % | 50 % | Stereo width of the delay repeats |
| **Delay Sync** | on/off | off | Lock the delay time to host tempo |
| **Sync Note** | 1/64T … 1/8T | 1/16 | Delay time as a note division when Sync is on |
| **Spec Freeze** | on/off | off | Capture and hold the current spectrum indefinitely |
| **Stereo Width** | 0 – 200 % | 100 % | Master stereo width, mono to super-wide |
| **Wander** | 0 – 100 % | 0 % | Depth of the slow random wander of the stereo width |
| **Wander Rate** | 0 – 100 % | 50 % | Smoothness of the wander; *higher drifts more slowly* |
| **Azimuth** | 0 – 100 % | 0 % | Depth of the slow random wander of the stereo position |

The spectral delay delays each frequency bin independently — with **Spread** up, lows and highs
repeat at different times, and **Delay Tilt** feeds back the top or bottom of the spectrum harder.
Feedback is internally reduced as Tilt increases, so the loop stays bounded at every setting.

## The Drawer: Life/Env Tab

The per-voice life modulators and the voice envelope.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Orbit Depth** | 0 – 100 % | 35 % | Depth of the per-voice orbital wander across the stereo field |
| **Orbit Rate** | 0.01 – 0.5 Hz (log) | 0.1 Hz | Speed of the orbital wander (100 s to 2 s per cycle) |
| **Coupling** | 0 – 100 % | 0 % | How tightly the two orbit axes lock together |
| **Growth** | −1 – +1 | 0 | Orbit radius shrinks below zero, expands above |
| **Voice Width** | 50 – 150 % | 100 % | Stereo width of each individual voice |
| **Env Mode** | Standard / Growth | Standard | Standard attack, or Growth for a slow logistic rise |
| **Growth Dur** | 1 – 60 s (log) | 10 s | Length of the Growth-mode rise |
| **Stage 0** | 1 – 10000 ms (log) | 2000 ms | First envelope stage time |
| **Stage 1** | 1 – 10000 ms (log) | 4000 ms | Second envelope stage time |
| **Release** | 1 – 10000 ms (log) | 8000 ms | Release time after note off |

**Growth mode** replaces the standard attack with a slow logistic swell — the note *arrives* rather
than starting. It is the engine behind *First Light*, *Approach Vector* and *Rising Dread*.

## Presets

Click **Presets** in the header to open the browser. Categories appear as tabs; double-click a
preset to load it. Loading restores everything — every parameter, and any spectral states you
authored by hand. **Save** writes the complete current state, authored slots included, into your
user preset folder:

- **Windows:** `C:\ProgramData\Krate Audio\Seraphis\{Category}\`
- **macOS:** `/Library/Application Support/Krate Audio/Seraphis/{Category}/`
- **Linux:** `~/.local/share/Krate Audio/Seraphis/{Category}/`

### Factory Library

42 presets ship with the plugin, six in each of seven categories. Every one is rendered and verified
during the build — each is proven to make sound, stay bounded, and be measurably distinct from all
41 others.

#### Textures
| Preset | Character |
|---|---|
| Vellum | Thin, papery two-state morph with a soft upper tilt |
| Sea Glass | Bright glass body, barely damped, over a sparse cloud |
| Slow Snow | Sparse Blackman grains falling through a quiet cloud |
| Paper Sky | Wide, downward-tilted wash with a slow partial drift |
| Rust Bloom | Saturated, inharmonic and slowly mutating |
| Quiet Machine | Dense short trapezoid grains, jittered into a hum |

#### Pads
| Preset | Character |
|---|---|
| First Light | Sine-stack pad that grows in over eight seconds |
| Long Exhale | Standard envelope stretched to the ceiling: 8 s attack, 9 s release |
| Cathedral Moss | Chamber body inside a large, soft Aether space |
| Warm Static | Resonator bypassed — saturation and decorrelated grain only |
| Distant Choir | Choral second state, pre-delayed and set well back |
| Blue Hour | Dim, slowly orbiting pad with a wide grain bed |

#### Drones
| Preset | Character |
|---|---|
| Deep Well | Low, unlit drone with a twenty-second Aether tail |
| Stone Circle | Very slow onset into a thirty-second room |
| Tectonic | Heavy low tilt, inharmonic and driven hard |
| Iron Lung | Body input AGC off — the resonator takes the full drive |
| Continuum | Strings body, almost undamped, held indefinitely |
| Undertow | Tidal Aether motion under a detuning cloud |

#### Bells
| Preset | Character |
|---|---|
| Frost Bell | Bell state, thin and cold, with a six-second cloud decay |
| Temple Rim | Long struck rim, nearly harmonic, in a twelve-second room |
| Glass Carillon | Parked on the glass state, struck and left to ring |
| Struck Ice | Ice body, high resonance, almost no damping |
| Bronze Halo | Metal plate, driven, ringing well past the note |
| Bell Garden | Partials entering at staggered times, spread hard across the field |

#### Choirs
| Preset | Character |
|---|---|
| Vowel Field | The choir state held open, tilted down and blurred |
| Breath Chorus | Breath into choir — the airiest pair in the library |
| Ghost Choir | Atmosphere frozen: the grain bed stops moving and hangs |
| Aeolian Voices | Wind-detuned voices, wide and never quite still |
| Whispered Mass | Almost all grain: decorrelated, pitch-spread, barely pitched |
| Angelic Drift | Shimmer octave and fifth folded back into the tail |

#### Motion
| Preset | Character |
|---|---|
| Orbit Study | External travel driving a fast spatial orbit |
| Tide Pool | Three states, crawling between them while the space breathes |
| Wander Lamp | Spline travel with four-second waypoints and a wandering image |
| Restless | Tempo-synced spectral delay feeding a mutating cloud |
| Spiral Arms | Coupled orbits sweeping the azimuth at the edge of the field |
| Slow Weather | All four states in play, crossed at a geological rate |

#### Cinematic
| Preset | Character |
|---|---|
| Approach Vector | Growth envelope closing in over nine seconds |
| Event Horizon | Aether frozen at maximum size — the note never lands |
| Signal Lost | Spectral freeze over a saturated, inharmonic bed |
| Rising Dread | Ten-second growth under a heavy downward tilt |
| Vast | Frozen Aether at full width — scale without motion |
| Aftermath | Exponential grains scattered late across a wide buffer |

## Performance Notes

- **Latency:** Seraphis reports a constant 1024 samples of latency in every state — it never
  changes with settings, so the host compensates once and stays correct.
- The audio path allocates no memory, takes no locks and performs no I/O while running; everything
  is sized up-front when the host prepares the plugin.
- With the editor closed, the visualization pipeline costs effectively nothing (a single flag check
  per block). The constellation only consumes CPU while you are looking at it.
- Voice count is the main CPU lever: the default is 8 voices; long-release pads hold voices for a
  long time, so reduce **Voices** if you are stacking instances.
- The output stage always ends in a true-peak limiter. The **Limit** toggle adds or removes tape
  saturation in front of it but never removes the safety ceiling.

## Cross-Platform

| Platform | Formats | Notes |
|---|---|---|
| Windows 10+ | VST3 | 64-bit |
| macOS 12+ | VST3, Audio Unit | Universal (Intel + Apple Silicon) |
| Linux | VST3 | x86-64 |

State saved on any platform loads identically on the others.

## Tips & Tricks

- **Start from neutral.** With all five macros at rest, the drawer describes the sound. Design the
  patch there, then use the macros as the performance surface on top — they always travel *from*
  what you set.
- **Seed is a free variation knob.** Same patch, different seed — a genuinely different take on the
  same texture, without moving a single parameter. Automate it between song sections.
- **Freeze is an instrument.** Hold a chord, engage **Aether Frz**, release — the space holds the
  chord forever while you play something new over it. **Spec Frz** does the same for the voice
  itself, and *Freeze Mix* on the Atmos tab lets the frozen spectrum bleed into the moving grain
  stream instead of replacing it.
- **Draw your own spectra.** Set the morph slot count to 2, author slot A by hand in Edit mode,
  pick a factory Bell for slot B, then ride **Position** — you are morphing between your drawing and
  a bell.
- **Masking is subtractive synthesis.** In Edit mode, click away every even partial and you built a
  hollow square-ish tone; mask everything above the 8th and you have a soft sub-tone — all without a
  filter.
- **The body is half the identity.** Before reaching for the Aether, try the same patch through
  each of the five materials — Glass to Ice is a bigger timbral distance than most filter sweeps.
- **Bipolar knobs rest at centre:** the Gravity macro, Cloud Gravity, Growth (orbit) and Delay Tilt
  all sit neutral at 12 o'clock — automation that "does nothing" is usually sitting on one of these
  centres.
- **Wander Rate is inverted from what you may expect:** higher values drift *more slowly*. Set it
  high for tectonic stereo motion, low for restless movement.
- **Long tails stack.** Envelope release (up to 10 s), body cloud decay (30 s), Aether decay (60 s)
  and the spectral delay all overlap; when a patch turns to mud, shorten one stage at a time,
  starting with the body wash.

---

*Seraphis is free and open-source software from Krate Audio, released under the MIT license.*
