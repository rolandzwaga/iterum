# Changelog

All notable changes to Ruinae will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.12.2] - 2026-07-24

Fixes in the shared KrateDSP arpeggiator engine, found by an adversarially-verified
audit of the MIDI signal flow in Gradus. Ruinae drives the same engine, so they are
listed here too and marked *(shared DSP)*. No Ruinae-specific code changed.

### Fixed

- **Notes cut short mid-step in Chord and Ratchet modes at gates above 100 %** *(shared DSP)* — Those modes replace the previous step's notes outright, but the previous step's scheduled release was not cancelled, so it fired part-way through the step that had just re-struck the same pitch. Measured on a sustained triad at 180 % gate: the first step sounded its full length and every step after it was clipped by 20 %, with an audible gap before the next strike.
- **Phantom note-offs when a single block emitted more events than it could carry** *(shared DSP)* — Each block has a fixed event budget. Once it filled, the engine stopped writing note-ons but carried on recording them as sounding and scheduling their releases, so it emitted note-offs for notes that never started. A 20-note chord at 1/64 produced 2708 of them. Reachable with a large chord at a fast rate, especially with ratchets.
- **Strummed chord notes could be emitted outside the block they belong to** *(shared DSP)* — The Strum control staggers a chord's notes in time, but that stagger was added after the safety clamp rather than before it. At 48 kHz a 50 ms strum in a 64-sample block placed note-ons roughly 2400 samples past the end of the block, while their releases still fired on time.
- **Negative Humanize could kill a note the instant it was struck** *(shared DSP)* — Humanize shifts a step earlier or later, but in Chord and Ratchet modes the *release* of the previous step kept the un-shifted position. Shifted early, the new note-on came before the old release, so for a pitch held across two steps the note was silenced immediately. Measured 48 such inversions in a single 400-block run at 75 % Humanize.
- **Positive Humanize cut notes short, and past a point orphaned them entirely** *(shared DSP)* — The gate length was counted from the step's un-shifted position, so a note shifted later lost exactly its shift from its own gate. Once the shift exceeded the gate, its release was scheduled *before* its note-on: an orphan note-off, and a note with nothing to release it. Measured 39 occurrences at 1/32 with a 1 % gate and full Humanize.
- **With Retrigger = Beat, the first step of each bar played the previous bar's continuation** *(shared DSP)* — A gate near 100 % puts the previous step's release exactly on the bar line, and the release was handled ahead of the bar reset even though the documented priority puts the bar first. The pattern reset then slipped a full step, so the bar opened on the wrong note — pitch 55 instead of 48 in the new regression test.
- **Ratchet decay could emit notes at velocity 0** *(shared DSP)* — A high Ratchet Decay drove later subdivisions to zero velocity, which many hosts read as a note-off. Ratchet subdivisions now floor at velocity 1, matching every other emission path.

### Changed

- **Eleven arpeggiated factory presets sound slightly different** — *Arp Chaos Matrix*, *Chaos Garden*, *Chaos Step*, *Dorian Cascade*, *Lydian Aurora*, *Noise Tick*, *Ratchet Fury*, *Reich Drift*, *Spectral Ratchet*, *Stab Machine* and *Vox Oracle* all use gates above 100 % or Humanize, so the two fixes above change what they emit. Note timing and pitch are unchanged; what changes is where the note-offs land — the notes now sustain for the length the gate asks for instead of being clipped early. Their reference captures were regenerated accordingly.

## [0.12.1] - 2026-07-21

Fixes in the shared KrateDSP arpeggiator engine, found while auditing Gradus.
Ruinae drives the same engine, so they are listed here too and marked
*(shared DSP)*. No Ruinae-specific code changed.

### Fixed

- **Arpeggiated notes cut short, and arp events emitted out of time order, during a big sustained chord** *(shared DSP)* — The engine's scheduled-note-off ring held 32 entries, but a full 32-note chord sustained at the maximum 200 % gate overlaps itself across two steps and needs 64. Measured on such a chord the ring overflowed 307 times in a single run; each overflow fired the oldest note's note-off early — truncating that note — and emitted it at the start of the block, after notes that had already been placed later in it. That produced 28 events stepping backwards in time. Ruinae passes the arpeggiator's output straight to the host without re-sorting, so those were reaching the host as-is.
- **Out-of-bounds write while tracking sounding notes** *(shared DSP)* — The single-note path stored into the sounding-note array *before* checking it against the array's bound. A chord can leave that array full, and a gate above 100 % keeps those notes sounding into the next step, so the following single note wrote one element past the end.

## [0.12.0] - 2026-07-20

Two strands of work: a rebuild of the factory preset bank, and the remediation of
a full-codebase audit of Ruinae. Several entries below are fixes in shared KrateDSP
components that Ruinae uses; they are listed here because they change how Ruinae
sounds or behaves, and are marked *(shared DSP)*.

### Added

- **Factory bank rebuilt — 168 presets across 15 categories** — Every one of the
  original 148 presets was rewritten. The bank had drifted toward a house sound:
  most presets leaned on the same saw-into-ladder voice with reverb, so they were
  hard to tell apart and large parts of the synth were never demonstrated by
  anything. Each preset now has a stated identity and between them they exercise
  every oscillator engine, every filter type, every distortion and delay type, the
  harmonizer, the mod matrix, the chaos and rungler sources, and the arpeggiator's
  lane system
- **Twenty arpeggiated presets in the Rhythmic category** — Rhythmic previously
  contained exactly one preset that used the arpeggiator. It now has twenty-one,
  taking the category to 40. These are deliberately not more of what the dedicated
  Arp categories already do: those run the arp on its own to demonstrate a lane or
  a mode, whereas each of these pairs it with a second rhythmic element — a trance
  gate, a transient duck, a Euclidean gate, a ratchet lane or a tempo-synced
  effect — whose cycle length does not divide the arp's, so the two grids drift
  against one another. *Polymeter Pulse* is the clearest case: lane lengths of 5,
  7, 3 and 4 produce a 420-step cycle from entirely static data

### Fixed

- **Spectral-freeze presets playing one fixed pitch regardless of the note
  played** — The oscillator adapter had no frequency handling for the spectral
  freeze engine, and the frozen spectrum itself was a hard-coded 440 Hz sine, so
  every note in every spectral-freeze preset sounded concert A. Affected nine
  presets including *Frozen Time* and *Nebula Rise*. The played note is now
  expressed as a pitch shift away from the frozen reference, and the shift range
  was widened so notes at the top of the keyboard are reachable *(shared DSP)*
- **The self-oscillating filter never actually self-oscillating** — Ruinae mapped
  its Q range of 0.1–30 linearly onto the filter's normalized resonance, but the
  filter only begins to oscillate above 0.9 normalized, which required a Q above
  27 out of a maximum of 30. Every preset built around this filter was therefore
  silent — all three measured a peak of exactly zero. Oscillation now begins
  around Q 10, and the three affected presets produce sound without any change to
  their stored data
- **Filter resonance stored as a normalized fraction instead of a Q value** — 116
  of 150 factory resonance values had been authored as if the field ran 0–1, but
  it is a raw Q in the range 0.1–30. Values intended as moderate resonance were
  landing at or near the bottom of the range, which is why so much of the bank
  sounded flat and characterless. All affected presets were remapped onto a
  musical Q range; two bandpass presets that grew quieter under the remap (higher
  Q narrows their band rather than adding a boost) were re-balanced individually
- **Filter resonance defaulting to the bottom of its range** — A new instance, and
  a host's "reset to default" on the resonance control, both landed on Q 0.1 — no
  resonance at all. The default is now Q 0.707 (Butterworth) in the processor, the
  preset format and the registered parameter alike. Saved projects are unaffected
  because they carry explicit values
- **Crash after closing the editor, switching tabs, or switching mod source** — The controller cached raw pointers to nine frame-owned views (the LFO 1/2 waveform, chaos, rungler, sample & hold and random visualisers, plus the three sidechain indicator labels). Three separate teardown paths destroyed those views without clearing the pointers: editor close, a MainTab switch away from MOD, and any change of the Mod Source dropdown. Because a dangling pointer is non-null, it passed every `if (ptr)` guard at the places they were read, so the next preset load or automation sync walked freed memory. All three paths now clear them
- **Unison notes sounding thinner than the voice count** — In the shared voice allocator, a unison group that had to steal voices could be handed the same voice slot more than once: several note-ons targeted one physical voice, so a four-voice unison could sound as few as one. Reachable by raising the unison count while notes are held. Each voice is now claimed as it is committed *(shared DSP)*
- **Reverb type switch double-reverberating and jumping ~3 dB** — Two faults in the same crossfade. The incoming reverb already processed the whole block, but when the crossfade finished mid-block the remainder was run through it a second time, producing a doubly-reverberated tail. Separately, both reverbs were fed the user's dry/wet mix, so each emitted its own copy of the dry signal; the equal-power crossfade gains sum to √2 at the midpoint, which is correct for two decorrelated tails but swelled the shared dry part by about 3 dB every time the type changed. Both reverbs now run 100 % wet with the mix applied outside them, matching how the delay slot already worked
- **Flanger and chorus reverting to defaults after loading a preset** — Both were driven only by live control changes. Loading a preset resets the DSP, and nothing pushed the stored values back down, so a recalled preset ran the flanger and chorus at their reset defaults until a control was touched. They are now re-applied from saved state every block, like every other effect
- **A reused voice playing the previous note's pitch** — Portamento state was cleared by neither voice reset nor note-on. If a glide was interrupted partway and the voice was later reused for an unrelated note, the first block resumed the old glide and overwrote the new note's frequency. Legato glides are unaffected
- **OSC A and OSC B Level doing nothing in SpectralMorph mode** — The level scaling only ran in the crossfade mixer, so in SpectralMorph mode the raw oscillators were fed straight to the spectral morph. Both the Level knobs and the OscALevel/OscBLevel modulation destinations were inert. Levels now apply to the morph inputs; note that in this mode OscLevel modulation is block-rate, since the morph is block-based
- **Modulation not returning to its base value when a route is removed** — Effect Mix and all six AllVoice destinations (voice cutoff, morph, trance gate rate, spectral tilt, resonance, filter envelope amount) only wrote to the engine when the modulation offset was non-zero — which is exactly when the base value needed restoring. Removing a route, bypassing it, or turning its depth to zero left the last modulated value stuck until the underlying parameter was touched. The global filter and master gain never had this problem, and all seven now behave the same way
- **Sidechain status parameter colliding with an arpeggiator parameter** — The hidden sidechain-active parameter used ID 3400, inside the arp routing range and sharing its ID with an arp parameter. VST3 requires unique parameter IDs, and changes to 3400 were being dispatched to the arp handler. It has moved to 3500, outside that range. The parameter is hidden and not persisted, so no preset migration is needed
- **Ring modulator chirping after a reset** — Reset zeroed the carrier frequency smoothers instead of snapping them, so the carrier swept up from near 0 Hz over the following 5 ms *(shared DSP)*
- **LFO fade-in running for the wrong duration after a sample rate change** — The fade-in increment was not recomputed when the LFO was re-prepared, so at double the sample rate the fade completed in half the intended time *(shared DSP)*
- **Harmonizer voice jumping to full level when unmuted** — Of the three pitch-shift paths, only PhaseVocoder kept a muted voice's smoothers running. In the other two the level smoother froze, so unmuting arrived at full gain in a single block instead of over its 5 ms ramp *(shared DSP)*
- **Click when enabling or disabling the delay or reverb** — Both were instant on/off switches, so an audible wet tail was cut in a single sample, and re-enabling replayed whatever was left in the buffers. Both now fade their wet signal over about 20 ms and clear their buffers only once the fade completes, so the tail is heard out and the next enable starts clean
- **Active voice count reading zero in mono mode** — Mono bypasses the voice allocator, so anything reading the engine's active-voice count saw zero for the whole of a mono performance *(shared DSP)*
- **Parameter count sentinel smaller than the real parameter IDs** — `kNumParameters` was 3401 while parameters ran up to 3445, breaking its documented "one past the highest ID" contract

### Changed

- **No more memory allocation on the audio thread** — Restoring a preset's modulation routes allocated on the audio path on every preset load, and the arpeggiator's skip messages allocated on their first send per lane. Route restore now happens on the message thread alongside the rest of the preset, the skip-message fields are created up front, and three one-time handshake messages to the editor moved out of the audio callback into activation
- **Lower CPU cost for the phaser, formant filter, ring modulator and SVF filter** — The phaser derived the same allpass coefficient once per stage (up to 24 `tan` calls per sample in stereo) and recomputed its sweep bounds every sample though they only depend on settled parameters. The formant filter — the most expensive per-voice filter — recomputed all five biquads on every sample regardless of whether anything had changed. The ring modulator recomputed the sine carrier's phasor constant per sample, defeating the point of the phasor. The SVF ran a `tan` for its second stage even in single-stage mode, where that stage is never used. All four are output-identical
- **Phaser and flanger feedback saturation** — Both now use the same fast hyperbolic-tangent approximation the chorus already used. This is a genuine, very small change to the sound of the feedback path (worst-case error about 0.0018 across the usable range), not a bit-identical optimisation
- **Mono voice rendering** — The mono path rendered one sample at a time, re-running all of the voice's per-block setup for every single sample. It now renders in 32-sample chunks. Portamento is still advanced per sample, but the pitch is applied at chunk boundaries — about 0.7 ms at 44.1 kHz — rather than per sample
- **Arpeggiator note timing documented as block-quantised** — Note events are dispatched at block boundaries rather than at their exact sample offset. This is unchanged behaviour, now stated in the code rather than left implicit
- **Internal: arpeggiator preset regression coverage widened** — The golden-MIDI
  regression test selected presets by an "Arp" category-name prefix, which meant an
  arpeggiated preset filed under any other category was not covered at all — one
  had in fact been shipping that way. It now selects on the preset's actual
  arpeggiator state, so an arp preset is covered whichever category it ships in.
  Coverage went from 25 presets to 46
- **Internal: parameter modules consolidated** — The OSC A and OSC B parameter modules (about 1670 lines, identical apart from the bank letter) and the chorus and flanger modules were each collapsed to a single implementation with thin per-bank bindings, and the OSC dropdown labels now come from one canonical table instead of three hand-synced copies. `applyParamsToEngine`, an ~810-line function, is split into fourteen per-section helpers. No behaviour or preset-format change: the saved byte streams are pinned by tests

### Removed

- **Unused arpeggiator parameter IDs 3399–3401** (velocity curve type, velocity curve amount, transpose) — Declared but never registered, processed, saved or loaded by Ruinae; the backing values were never read. Gradus keeps its own definitions at the same numbers, and the arp save format the two plugins share does not include them
- **Dead debug scaffolding** — Compiled-out phaser, trance gate and effects-chain trace helpers, including a mutable global shared between two source files

## [0.11.2] - 2026-05-25

### Fixed

- **Delay effect dry signal arriving late** — When the delay slot was enabled, the latency-compensation delay was applied to the per-type delay processor's already-mixed dry+wet output, dragging the dry portion back by ~6144 samples (~140 ms at 44.1 kHz). At low mix values (e.g. 20 %) the synth attack audibly lagged the MIDI note. Fixed by forcing each delay processor to 100 % wet internally, capturing the dry input before delay processing, and performing `(1-mix)·dry + mix·compDelayedWet` outside the delay block. Dry is now immediate; wet stays comp-aligned across delay types so cross-fades and type switches remain smooth. Includes a smoothed external mix to avoid zipper noise.

## [0.11.1] - 2026-04-08

### Fixed

- **Oscillator mix leaking NaN/Inf on ARM (Apple Silicon)** — At mix position 0.0 or 1.0, the crossfade formula `oscA * 0.0 + oscB * 1.0` could leak NaN/Inf values from the Chaos oscillator through FMA contraction on ARM. Added explicit branches at mix extremes to bypass the multiply entirely

## [0.11.0] - 2026-03-31

### Added

- **Per-lane speed multipliers** — Each arpeggiator lane (velocity, gate, pitch, modifier, ratchet, condition, chord, inversion) can run at its own clock rate from 0.25x to 4x, adding a second dimension of polyrhythmic complexity on top of independent lane lengths. Uses fractional accumulators for sample-accurate timing. Click the speed label in any lane header to select from 10 discrete values.
- **8 new arp parameters** (IDs 3380-3387) — One speed multiplier per lane, registered as discrete RangeParameters with 10 steps. Fully automatable and preset-compatible with Gradus.

## [0.10.0] - 2026-03-20

### Added

- **MPE (MIDI Polyphonic Expression) support** — Per-note expression routing via VST3 NoteExpression events. Each voice tracks its own noteId, enabling independent per-note tuning (±120 semitones), volume (0–4x gain), and brightness (±48 semitone filter cutoff offset). The shared MIDI dispatcher's C++20 concept detection automatically routes MPE events when the noteId-aware callbacks are present
- **Per-note pitch via NoteExpression tuning** — Expression tuning offsets applied multiplicatively to oscillator frequencies, stacking with per-oscillator tuning and modulation
- **Per-note volume via NoteExpression volume** — Expression volume multiplied into the VCA stage alongside the amplitude envelope
- **Per-note brightness via NoteExpression brightness** — Expression brightness mapped to ±48 semitone filter cutoff offset, adding spectral control per voice
- **Global pitch bend callback** — `onPitchBend` routes through the existing NoteProcessor for global pitch bend, combinable with per-note expression tuning
- **noteId-aware voice allocation** — Engine stamps noteId on allocated voices and uses noteId-first lookup for note-off, ensuring accurate MPE voice targeting

### Fixed

- **Arpeggiator note value dropdown count mismatch** — Tests hardcoded 21 entries (stepCount=20) but the DSP layer's `kNoteValueDropdownCount` was updated to 30. Fixed tests and added missing note value labels (2/1, 3/1, 4/1 variants) to `formatArpParam`

## [0.9.13] - 2026-03-17

### Fixed

- **Envelope timing completely wrong** — Amp and mod envelope decay/attack/release times were drastically shorter than specified (e.g., 10s decayed in ~1s). Root cause: curve amounts were never applied to amp and mod envelopes, leaving them on the one-pole exponential path instead of the phase-based table path. Added float curve setter overloads in engine and wired them in the processor
- **Bezier curves ignored by envelopes** — Bezier control points were stored in parameters but never forwarded to the DSP engine. Added Bezier curve setters for all three envelopes (amp, filter, mod) and conditional application in the processor when Bezier mode is enabled
- **ADSR playback dot doesn't follow curve** — The playback indicator dot moved in a straight line instead of tracing the drawn curve path. Added inverse curve table lookup to compute the correct phase from the output level, so the dot now follows Bezier and power curves accurately

### Changed

- **ADSR mode toggle labels** — In the expanded overlay (>500px width), the Bezier/Simple toggle button now shows full "Bezier"/"Simple" labels instead of abbreviated "B"/"S"

## [0.9.12] - 2026-03-13

### Added

- **Mod source visualisers** — UI-thread visualisers for Rungler (XY Lissajous plot, CV staircase, shift register bits), Sample & Hold (scrolling time-series with trigger markers and slew smoothing), and Random (scrolling output with smoothness morphing) mod sources. Also added shared LFO waveform and Chaos mod display components
- **Sidechain input bus** — Auxiliary stereo sidechain input for audio-dependent mod sources (EnvFollower, PitchFollower, Transient). Falls back to analyzing the synth's own output when no sidechain is connected
- **Sidechain status indicators** — CTextLabel indicators on EnvFollower, PitchFollower, and Transient mod source views showing connection state (green "SIDECHAIN ACTIVE" / amber "NO SIDECHAIN — analyzing synth output")
- **ADSR expanded overlay** — New overlay component for expanded ADSR envelope editing

## [0.9.11] - 2026-03-12

### Added

- **Chorus effect** — New stereo chorus DSP processor (Layer 2) with LFO-modulated delay lines, configurable depth and rate, and true dry/wet crossfade. Parameters: Rate (0.1–5 Hz), Depth, Mix, Voices (1–4), Stereo Spread
- **Chorus UI controls** — New CHORUS fieldset on the FX tab with enable toggle, knobs, and voice selector
- **Chorus preset support** — Chorus parameters included in state save/load with backward-compatible defaults for older presets

### Fixed

- **Harmonizer + reverb clipping artifacts** — Combined signal from harmonizer voices exceeded ±1.0, causing hard compression through the output limiter. Fixed with three changes: harmonizer wet bus now scales by 1/√N (N = active voices), new threshold-based soft limiter (`softLimit`) preserves signals below 0.85 and only saturates peaks, and inter-stage soft limiting between harmonizer and reverb prevents overdriving the reverb feedback network. Click count reduced ~60%, max sample step reduced ~25%

## [0.9.10] - 2026-03-12

### Added

- **Flanger effect** — New stereo flanger DSP processor (Layer 2) with modulated short delay line, LFO-driven comb-filter sweep, and tanh-clipped feedback for resonance control. Parameters: Rate (0.05–5 Hz), Depth, Feedback (±1.0), Mix (true dry/wet crossfade), Stereo Spread (0–360°), Waveform (Sine/Triangle), Tempo Sync with note value selection
- **Modulation type selector** — The phaser enable toggle is replaced by a three-way selector (None / Phaser / Flanger) in the modulation slot. Switching between effects uses a 30ms linear-ramp crossfade for click-free transitions. Only one modulation effect is active at a time
- **Flanger UI controls** — Modulation fieldset shows context-appropriate controls: flanger knobs and dropdowns when Flanger is selected, phaser controls when Phaser is selected, or a "No modulation" label when None is selected
- **Backward-compatible preset migration** — Old presets with `phaserEnabled` are automatically migrated: enabled → Phaser, disabled → None. All flanger parameters default gracefully when absent from older state streams

### Changed

- **Modulation fieldset renamed** — "PHASER" fieldset is now "MODULATION" to reflect the expanded modulation slot

## [0.9.9] - 2026-03-11

### Fixed

- **Dattorro plate reverb bandwidth filter** — Filter coefficient derivation was inverted, producing a 3.8 Hz lowpass instead of the near-transparent filter specified in the Dattorro paper. The reverb now has proper frequency response and much richer sound
- **Plate reverb decay range** — Recalibrated roomSize-to-decay mapping from [0.75, 0.9995] to [0.50, 0.90] to give musically useful RT60 range (~2-12 seconds) with the corrected bandwidth filter. Previously roomSize=100% produced a 42-minute tail
- **Plate reverb output gain** — Reduced from 3.0 to 0.6 to compensate for correct bandwidth filter energy levels
- **Tank modulation interpolation** — Upgraded DD1 modulated allpass reads from linear to cubic Hermite interpolation, reducing HF loss in the recirculating tank
- **LFO amplitude drift on rate changes** — Gordon-Smith phasor now renormalized to the unit circle when modulation rate changes, preventing amplitude drift
- **Input diffusion stale buffers** — Diffusion allpasses are now always processed (even at diffusion=0) to prevent stale audio bursts when re-enabling diffusion
- **DD2 integer delay read** — Fixed unnecessary linear interpolation on integer-length delays
- **Output tap bounds checking** — Tap positions are now clamped against section lengths in prepare() to prevent buffer overruns at unusual sample rates
- **Per-sample damping overhead** — Damping filter coefficient now cached to avoid redundant exp() calls when cutoff hasn't changed

## [0.9.8] - 2026-03-08

### Added

- **Arpeggiator Chord Lane** — New per-step chord type lane (None/Dyad/Triad/7th/9th) that generates chords by stacking diatonic scale degrees using the arp's scale harmonizer. Transforms the arpeggiator from a single-note sequencer into a harmonic sequencer
- **Arpeggiator Inversion Lane** — Per-step chord inversion control (Root/1st/2nd/3rd) for voice-leading variety across the pattern
- **Global Voicing Mode** — Close, Drop-2, Spread, and Random voicing options that control how chord notes are distributed across registers
- **5 new "Arp Chords" factory presets** — Diatonic Triads, Minor 7th Pulse, Chord Cascade, Spread Ninths, Stab Machine

### Fixed

- **Condition lane popup menu positioning** — COptionMenu popups on condition, chord, and inversion lanes now appear at the clicked step instead of at an incorrect offset

## [0.9.7] - 2026-03-08

### Added

- **Arpeggiator Pitch as global modulation source** — Arp pitch (last played arp note) is now available as a modulation source in the global mod matrix, enabling pitch-following effects like filter tracking or delay time modulation

### Changed

- **Enriched factory presets** — Deeper modulation routing across factory presets for more expressive out-of-the-box sound design

### Fixed

- **Voice mod routes disappearing on click** — Clicking any control in a voice modulation row caused all voice routes to vanish. Root cause: VST3's synchronous `sendMessage→notify` chain caused re-entrant `VoiceModRouteState` responses to overwrite the grid during callback execution
- **Voice mod route delete button not working** — The remove button appeared to do nothing because the processor's synchronous response overwrote the grid mid-loop, undoing the shift-up operation
- **Voice route interactions corrupting global mod parameters** — Voice route detail controls (curve, smooth, scale, bypass, amount) were incorrectly calling `beginEdit`/`performEdit`/`endEdit` on global mod matrix parameter IDs. Voice routes now communicate exclusively via IMessage

## [0.9.6] - 2026-03-07

### Fixed

- **ADSR envelope edits not persisting when switching tabs** — Dragging envelope control points only called `performEdit` (sending values to the host/processor) but did not call `setParamNormalized` to update the controller's internal parameter state. Switching between envelope tabs and back would show stale pre-edit values. Now both calls are made, matching the pattern used by all other custom editors.

## [0.9.5] - 2026-03-05

### Added

- **In-plugin update checker** — Automatically checks for new versions on editor open (24h cooldown) and shows a non-intrusive banner with download link when an update is available
- **"Check for Updates" button** — Manual version check in the settings panel
- **Version label** — Dynamic version display in the top bar
- Version dismiss functionality for update notifications
- **Category dropdown in quick-save preset dialog** — Preset save dialog now includes a category selector

### Changed

- **Removed dynamic voice-count gain compensation** — Eliminated phantom volume boost caused by gain scaling adjusting mid-note when voices overlapped

### Fixed

- **Harmonizer silent after preset load** — Use-after-free crash on preset switch fixed by nulling dangling view pointers
- **Global LFO retrigger not firing on note-on** — LFO phase was not resetting when retrigger was enabled
- **Near-silent chaos presets** — Chaos oscillator presets producing barely audible output due to incorrect gain staging
- **VSTGUI crash during rapid preset switching** — Multiple use-after-free and null-pointer issues in view lifecycle during bulk parameter loads
- **Preset loading race conditions** — RTTransferT lock-free transfer prevents data races on voice routes during preset switches
- **Spectral distortion drive scaling and output buffer clobbering** — Drive parameter had incorrect range mapping; output buffer was being overwritten
- **Inactive spectral distortion controls not dimmed** — Curve dropdown and bits controls now gray out based on spectral mode selection
- **Bass presets missing from preset browser** — Category filtering excluded bass subcategory
- **Euclidean Bells preset** — Opened filter and initialized all partials to full amplitude

## [0.9.4] - 2026-03-02

### Added

- **ARP Save Preset button** — "Save" button on the arpeggiator tab opens the ARP preset browser with the save dialog shown, defaulting to "Arp Classic" subcategory
- **Shared OutlineBrowserButton** — Unified outline button component for preset browser and save dialog, replacing duplicated CTextButton/DialogButton classes

### Changed

- **Distinct FX tab section colors** — Each effect section (Phaser, Delay, Harmonizer, Reverb) now has its own accent color
- **MOD tab layout** — Redistributed controls and fixed mod source dropdown display
- **Preset browser filtering** — ARP presets hidden from synth preset browser via tab label filtering

### Fixed

- **CI AUv3 verify step** — No longer fails when only a subset of plugins changed

## [0.9.3] - 2026-03-02

### Added

- **30 new synth presets** across 6 categories: Basses (5), Leads (5), Pads (5), Rhythmic (5), Textures (5), Experimental (5)
- **Crash-proof preset loading** — RTTransferT lock-free triple-buffer safely transfers preset state to the audio thread, preventing data races on voiceRoutes during preset switches

### Fixed

- **Chaos oscillator producing inaudible DC output** — Lorenz, Rossler, and Chua attractors were evolving at sub-audio rates (0.3-10 Hz instead of 440 Hz) due to incorrect baseDt calibration. Recalibrated all three attractors and adjusted gain compensation. The Chaos Wind preset and all chaos-type oscillator presets now produce proper audible audio.

## [0.9.2] - 2026-03-01

### Added

- **Ring Modulator distortion type** — New per-voice distortion with internal sine oscillator, waveform selection (Sine, Triangle, Square, Saw), fixed/ratio frequency modes, depth control, and stereo spread
- **Ring Modulator DSP processor** (KrateDSP Layer 2) with DC offset removal

### Changed

- **UI layout: taller Row 1 and Row 2** — Added 25px height to each of the first two rows on the Sound tab for better visual spacing
- **Knob label positioning** — Labels beneath ArcKnobs in oscillator, filter, and distortion sub-panels now render below the knob rather than overlapping it
- **Sub-panel heights increased** — Oscillator type-specific panels (32→48px), filter/distortion sub-panels (88→113px) to use the added room

## [0.9.1] - 2026-02-28

### Added

- **Arpeggiator Scale Mode** — Optional musical scale constraint for the arpeggiator
  - 16 scale types: Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone, Diminished (W-H), Diminished (H-W)
  - 12 root notes (C through B)
  - Pitch lane interprets step values as scale degree offsets when a non-Chromatic scale is active (e.g., +2 in C Major = E, not D)
  - Correct octave wrapping for all scale sizes (5-12 notes)
  - Scale Quantize Input toggle: snaps incoming MIDI notes to the nearest scale note before entering the arp pool
  - Root Note and Scale Quantize Input controls dim when Chromatic is selected
  - Pitch lane popup suffix changes from "st" to "deg" in scale mode
  - Chromatic mode (default) is identical to pre-feature behavior

- **Harmonizer scale extension** — Harmonizer dropdown now exposes all 16 scale types (was 9)

### Changed

- VST3 post-build copy destination changed from `C:/Program Files/Common Files/VST3` to `%LOCALAPPDATA%/Programs/Common/VST3` (no admin required)

## [0.9.0] - 2026-02-27

### Added

- **Preset Browser**
  - Overlay-based preset browser with category and subcategory filtering
  - Case-insensitive name search
  - Persistent preset browser button in the top bar across all tabs

- **Factory Presets** (14 presets across 6 categories)
  - Arp Acid: Acid_Line_303, Acid_Stab
  - Arp Classic: Basic_Up_1-16, Down_1-8, UpDown_1-8T
  - Arp Euclidean: Bossa_E(516), Samba_E(716), Tresillo_E(38)
  - Arp Generative: Chaos_Garden, Spice_Evolver
  - Arp Performance: Fill_Cascade, Probability_Waves
  - Arp Polymetric: 3x5x7_Evolving, 4x5_Shifting

### Fixed

- **Stuck notes with arp slide modifier**: Poly-legato glide changed voice pitch without updating the voice allocator's note tracking, so the subsequent noteOff for the new pitch couldn't find the voice. The allocator note is now updated after glide.

## [0.8.0] - 2026-02-25

### Added

- **Arpeggiator UI** — Full arpeggiator interface in the SEQ tab
  - Specialized lane views for Velocity, Gate, Pitch, Modifier, Ratchet, and Condition
  - `ArpConditionLane` with right-click reset and tooltip descriptions for all 18 condition types
  - `ArpModifierLane` with color-coded step display (Active, Rest, Tie, Slide, Accent)
  - `ArpLaneContainer` managing multiple independent-length lanes
  - Dropdown callbacks for arp mode, note value, latch, and retrigger

### Fixed

- Arp lane rendering bugs: consistent clamping, collapse guard, lane color consistency

## [0.7.0] - 2026-02-23

### Added

- **Arpeggiator Engine** — Full-featured polyphonic arpeggiator
  - 10 modes: Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord
  - 6 independent lanes (each 1-32 steps): Velocity, Gate, Pitch, Modifier, Ratchet, Condition
  - Per-step modifiers: Active, Rest, Tie, Slide, Accent
  - 18 conditional trigger types: probability (10%-90%), cycle-based (every Nth), First, Fill
  - Euclidean timing with configurable hits, steps, and rotation
  - Ratcheting (1-4 subdivisions per step) with swing
  - Spice/Dice randomization and Humanize for timing/velocity variation
  - Swing (0-75%), latch (Off/Hold/Add), 1-4 octave range
  - Tempo sync with 21 note values (1/32T to 4/1) or free rate (0.5-50 Hz)
  - Modulation destinations: Arp Rate, Gate Length, Octave Range, Swing, Spice

### Fixed

- Reverb volume drop when switching parameters (switched to equal-power crossfade)

## [0.6.0] - 2026-02-20

### Added

- **4-Tab UI Layout** — Restructured interface into SOUND, MOD, FX, and SEQ tabs
- **UI resize** from 925x880 to 1200x720 for better screen utilization
- **Oscillator type-specific parameter panels** — Dynamic sub-panels swap when oscillator type changes (10 panel variants per oscillator via UIViewSwitchContainer)
- **Value popup on ArcKnob** — Shows current value on hover
- **Hand cursor on ADSR control points** — Visual feedback for draggable envelope handles

### Changed

- Global Filter merged into the MASTER section (persistent across all tabs)
- Gain compensation now scales with active voice count rather than the polyphony setting

### Fixed

- Settings drawer toggle broken by harmonizer tag range overlap

## [0.5.0] - 2026-02-19

### Added

- **Harmonizer** effect in the FX chain
  - Chromatic and Scalic harmony modes
  - 12 keys and 9 scales (Major, Minor, Harmonic Minor, Dorian, Mixolydian, etc.)
  - Up to 4 simultaneous harmony voices with per-voice interval, level, pan, delay, and detune
  - 4 pitch shift algorithms: Simple, Granular, Phase Vocoder, Pitch Sync
  - Formant preservation toggle
  - Independent dry/wet level controls

- **Trance Gate UI improvements**
  - Right-click to clear individual steps
  - Dropdown-based preset selector (replaced button row)

## [0.4.0] - 2026-02-16

### Added

- **Extended Modulation Sources**
  - Pitch Follower — follows MIDI note pitch as a mod source
  - Transient — transient detection mod source
  - Sample & Hold — sampled random CV source
  - Env Follower — audio-reactive envelope follower

- **Macros** — 4 user-assignable macro knobs (Macro 1-4), host-automatable

- **Rungler** — Buchla-style shift register random CV generator
  - Dual oscillator cross-modulation
  - Configurable shift register length (4-16 bits)
  - Chaos/Loop mode toggle
  - CV smoothing filter

- **Settings Drawer** — Slide-in panel for voice and tuning configuration
  - Pitch bend range (0-24 semitones)
  - Velocity curve (Linear, Soft, Hard, Fixed)
  - Tuning reference (400-480 Hz)
  - Voice allocation and steal mode
  - Gain compensation toggle

- **Mono Mode** with note priority (Last, High, Low) and legato toggle

- **Global Filter** (post-voice-mix, pre-effects) — LP, HP, BP, Notch with cutoff and resonance

- **Trance Gate tempo sync** with note value selector

## [0.3.0] - 2026-02-14

### Added

- **Phaser** effect — 2-12 allpass stages, tempo sync, stereo spread, 4 LFO waveforms
- **Chaos Mod Source** — Lorenz/Rossler attractor as modulation source with rate and depth
- **Master Section** — Master gain, stereo width, voice spread, soft limiter toggle
- **LFO enhancements** — Shape selector (6 shapes), tempo sync, phase offset, retrigger, unipolar/bipolar, fade-in, symmetry, quantize steps

### Fixed

- Chaos mod source evolving ~500x too slowly (was advancing 1 tick per block instead of per sample)
- Filter cutoff modulation scaling producing inaudible results
- Phaser feedback parameter labeled as "Resonance" (renamed to "Feedback")

## [0.2.0] - 2026-02-13

### Added

- **Modulation Matrix** — 8-slot global matrix with source, destination, bipolar amount, curve (Linear/Exp/Log/S-Curve), smoothing, scaling, and bypass per slot
- **Mod Matrix Grid UI** — Visual 8x4 grid with heatmap display and per-slot detail panel
- **Per-voice distortion** — 6 types: Clean, Chaos Waveshaper, Spectral Distortion, Granular Distortion, Wavefolder (Triangle/Sine/Lockhart), Tape Saturator (Simple/Hysteresis)
- **Per-voice filter** — 13 filter types with type-specific controls: SVF variants (LP/HP/BP/Notch/Allpass/Peak/Low Shelf/High Shelf), Ladder (4 slopes), Formant, Comb, Envelope Filter, Self-Oscillating
- **Reverb** — Dattorro plate algorithm with size, damping, width, pre-delay, diffusion, freeze, and modulation
- **ADSR envelope editor** — Draggable control points with per-segment curve amounts and Bezier mode
- **XY Morph Pad** — 2D control for oscillator crossfade/spectral morph position
- **Oscillator type selector** — Per-oscillator type switching with dynamic sub-panel visibility
- **Named color palette** — Extracted from hard-coded values for consistent theming
- **Spectral tilt** as both per-voice and global modulation destination
- **AllVoice Resonance and Envelope Amount** modulation destinations

### Fixed

- SVF filter crackle on note retrigger
- Formant filter being almost inaudible (added Q-based gain compensation)
- Mod amount slider UX and heatmap sync bugs
- Spectral tilt gain clamping and morph pad tag assignment
- State restoration bug causing parameter values to reset

## [0.1.0] - 2026-02-09

### Added

- **Initial plugin skeleton** — VST3 entry point, processor, controller, and VSTGUI editor
- **Synth engine** — Polyphonic engine with up to 16 pre-allocated voices and configurable voice allocation (Round Robin, Oldest, Lowest Velocity, Highest Note)
- **10 oscillator types** (per voice, dual oscillators A + B):
  - PolyBLEP (Sine, Saw, Square, Pulse, Triangle)
  - Wavetable (mipmapped single-cycle)
  - Phase Distortion (8 waveforms)
  - Oscillator Sync (Hard, Reverse, Phase Advance)
  - Additive (1-128 partials, spectral tilt, inharmonicity)
  - Chaos Attractor (Lorenz, Rossler, Chua, Duffing, Van der Pol)
  - Particle (granular cloud, 1-64 density, scatter, drift)
  - Formant (A-E-I-O-U vowel morphing)
  - Spectral Freeze (FFT-based, pitch/tilt/formant shift)
  - Noise (White, Pink, Brown, Blue, Violet, Grey)
- **Dual oscillator mixer** — Crossfade and Spectral Morph modes
- **3 ADSR envelopes** per voice — Amplitude, Filter, Modulation (each with per-segment curve control)
- **Trance Gate** — Step sequencer (2-32 steps), euclidean mode, rate/depth/attack/release
- **Portamento** — 0-5000 ms with Always and Legato Only modes
- **Step Pattern Editor** — Custom VSTGUI control for trance gate step editing
- **ArcKnob** — Custom VSTGUI knob control
- **FieldsetContainer** — Custom VSTGUI grouping control
- **Gain compensation** — Square-root-of-N voice scaling with soft sigmoid limiter
- **NaN/Inf safety flush** on output
