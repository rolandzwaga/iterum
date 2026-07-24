# Changelog

All notable changes to Gradus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.10.0] - 2026-07-24

An adversarially-verified audit of the whole MIDI signal flow — host input, the
arpeggiator engine, the MIDI Delay echo tail and the MIDI output — plus a
polyphonic rewrite of the audition monitor. Many entries are fixes in the shared
KrateDSP arpeggiator engine; they are listed here because they change how Gradus
behaves, and are marked *(shared DSP)*.

### Added

- **The audition monitor is polyphonic** — The built-in monitor was a single voice, so it could only ever sound one note. Anything with more than one note down at a time — a chord lane, overlapping gates above 100 %, a ratchet, or the MIDI Delay's echo tail sounding under the note that spawned it — was misrepresented: you heard whichever note struck last, and the rest were silent. It is now an eight-voice pool with proper voice stealing, so the monitor sounds what the MIDI output actually says. The clearest case is staggered releases: with Up mode at a 150 % gate, note N's release arrives while note N+1 is still down, and the old monitor went silent for 113 of 320 blocks that had notes sounding throughout. Voices are summed with headroom scaled to the number sounding, so a chord cannot clip; a single note is exactly as loud as before. (The MIDI Gradus sends to the host was never affected by this — only what you heard from Gradus itself.)

### Fixed

- **The ring's step highlight stopped animating** — The moving highlight on the concentric rings only advanced when some *unrelated* parameter happened to repaint the ring, such as switching lane tabs. The processor was reporting the playhead position every block and the ring was storing it correctly; the view was simply never told to redraw. Introduced when per-region redraw batching replaced an earlier blanket repaint.
- **Held notes were silenced when the host changed the buffer size or sample rate** — Any note sounding at that moment had its pending note-off discarded, so it hung in the host until something else released it. Hosts do this on device changes, on switching between realtime and offline rendering, and when starting a bounce.
- **Sequencer mode: Stop then Play left echo notes hanging** — Pressing Play restarts the sequencer pattern, which correctly discards the MIDI Delay's pending echoes — but any echo whose note-on had already reached the host lost its note-off with it. Those note-offs are now emitted before the reset.
- **Echo note-offs were dropped when the delay ran out of slots** — The MIDI Delay tracks up to 256 in-flight echoes and steals the oldest when it needs room. If that oldest echo had sounded but not yet been released, its note-off went with it and the note hung forever. The delay now refuses the incoming echo rather than an outstanding release obligation.
- **A note-on with velocity 0 was treated as a note-on** — MIDI has always allowed velocity 0 as a note-off, and many controllers and hosts send it that way. Gradus added it to the held-note buffer instead, so the note was never released and the arp kept playing it — a stuck note that only a panic cleared.
- **Notes were cut short mid-step in Chord and Ratchet modes at gates above 100 %** *(shared DSP)* — Those modes replace the previous step's notes outright, but the previous step's scheduled release was not cancelled, so it fired part-way through the step that had just re-struck the same pitch. Measured on a sustained triad at 180 % gate: the first step sounded its full length and every step after it was clipped by 20 %, with an audible gap before the next strike.
- **Phantom note-offs when a single block emitted more events than it could carry** *(shared DSP)* — Each block has a fixed event budget. Once it filled, the engine stopped writing note-ons but carried on recording them as sounding and scheduling their releases, so it emitted note-offs for notes the host never heard start. A 20-note chord at 1/64 produced 2708 of them. Reachable with a large chord at a fast rate, especially with ratchets or a big echo tail.
- **Strummed chord notes could be emitted outside the block they belong to** *(shared DSP)* — The Strum control staggers a chord's notes in time, but that stagger was added after the safety clamp rather than before it. At 48 kHz a 50 ms strum in a 64-sample block placed note-ons roughly 2400 samples past the end of the block, which is not a position a host is required to accept — while their releases still fired on time.
- **Negative Humanize could kill a note the instant it was struck** *(shared DSP)* — Humanize shifts a step earlier or later, but in Chord and Ratchet modes the *release* of the previous step kept the un-shifted position. Shifted early, the new note-on sorted before the old release, so for a pitch held across two steps the host saw note-on then note-off and silenced the note immediately. Measured 48 such inversions in a single 400-block run at 75 % Humanize.
- **Positive Humanize cut notes short, and past a point orphaned them entirely** *(shared DSP)* — The gate length was counted from the step's un-shifted position, so a note shifted later lost exactly its shift from its own gate. Once the shift exceeded the gate, its release was scheduled *before* its note-on: an orphan note-off, and a note with nothing to release it. Measured 39 occurrences at 1/32 with a 1 % gate and full Humanize.
- **With Retrigger = Beat, the first step of each bar played the previous bar's continuation** *(shared DSP)* — A gate near 100 % puts the previous step's release exactly on the bar line, and the release was handled ahead of the bar reset even though the documented priority puts the bar first. The pattern reset then slipped a full step, so the bar opened on the wrong note — pitch 55 instead of 48 in the new regression test.
- **Ratchet decay could emit notes at velocity 0** *(shared DSP)* — A high Ratchet Decay drove later subdivisions to zero velocity, which many hosts read as a note-off, while the audition monitor treated them as sounding-but-silent voices. Ratchet subdivisions now floor at velocity 1, matching every other emission path.
- **Editing a lane's speed curve could corrupt the curve the audio thread was reading** *(shared DSP)* — A new curve was published into a single shared buffer, and the copy is not atomic, so a publish landing mid-read handed the audio thread a mixture of the old and new curves. Confirmed directly with ThreadSanitizer. The handoff now rotates through three buffers so the reader and writer never touch the same one.
- **Eight input velocities came out one step low** — Converting the host's note velocity into MIDI's 1-127 range truncated instead of rounding, so 72 arrived as 71, 104 as 103, and six others likewise. The conversion is also now defined if a host sends a velocity outside the normalized range the VST3 specification requires.

## [1.9.1] - 2026-07-21

Remediation of a full-codebase audit of Gradus. Several entries are fixes in the
shared KrateDSP arpeggiator engine; they are listed here because they change how
Gradus behaves, and are marked *(shared DSP)*.

### Fixed

- **The chord you were holding when you pressed Play did nothing** — Source = Live free-runs off held notes and keeps clocking while the transport is stopped, so notes are already sounding and a chord is already held when Play is pressed. The rising transport edge reset the engine unconditionally, which clears the held notes and the sounding-note list *without emitting any note-off*: every note sounding at that instant was orphaned in the host, and the arp then fell silent until the keys were physically re-pressed. The regression fixtures had recorded five full seconds of that silence as expected output. The reset is now gated to Source = Sequencer, which is the mode that wants it (Stop/Play restarts the pattern there as before).
- **Editing the Modifier, Condition or Ratchet ring changed a different lane** — The ring display numbers its lanes in a different order from the parameter tables, and the two disagree at exactly those three positions. Ring edits were resolved through the parameter-table order, so dragging Modifier wrote Ratchet's step parameters, Condition wrote Modifier's, and Ratchet — which is a continuous drag lane — pushed smoothly-varying values into the discrete Condition lane. The ring's initial step counts were seeded in the wrong order too, so hit-testing used one lane's length for another until the first length edit corrected it.
- **MIDI Delay lane Speed, Swing, Jitter and Curve Depth did nothing** — All four were stored, saved, restored and mirrored back into the UI, so the whole surrounding chain implied they worked. They were simply never applied to the engine, and the delay lane always clocked at the base rate.
- **Notes left sounding when the host deactivated the plugin** — Gradus emits its arp and echo notes to a MIDI *output* bus, which the host cannot recall on its behalf, and deactivation released nothing. Outstanding note-offs are now recorded on deactivate and emitted by the first block after the plugin is reactivated.
- **Echo note-offs lost when a block's event output filled up** *(shared DSP)* — The MIDI delay writes into a fixed output slot budget shared with the arp's own events. Once that filled, a due echo's note-off was skipped and then discarded outright on the following block, leaving its note-on unbalanced in the host forever.
- **MIDI output events could step backwards in time within a block** *(shared DSP)* — Pass-through arp events, emergency note-offs and due echoes were appended in emission order rather than time order, so an echo due early in a block could follow an arp note occurring late in it. Hosts are entitled to a monotonic event stream.
- **Notes cut short during a big sustained chord** *(shared DSP)* — The engine's scheduled-note-off ring held 32 entries, but a full 32-note chord sustained at the maximum 200 % gate overlaps itself across two steps and needs 64. Measured on such a chord the ring overflowed 307 times in a single run, and each overflow fired the oldest note's note-off early — truncating that note and emitting the event out of time order.
- **Out-of-bounds write while tracking sounding notes** *(shared DSP)* — The single-note path stored into the sounding-note array *before* checking it against the array's bound. A chord can leave that array full, and a gate above 100 % keeps those notes sounding into the next step, so the following single note wrote one element past the end.
- **Unsynchronised read of the per-lane speed-curve depth** *(shared DSP)* — Depth is set from the editor while the audio thread reads it every block, but it was a plain float while its two sibling curve fields were already synchronised. The audible worst case was a single block using the previous depth; it is now atomic like the others, and the audio thread takes one snapshot per block instead of two reads that could disagree.
- **Sequencer pattern steps 17-32 defaulted to a pitch of 0 instead of C4** — The engine filled all 32 steps while the lane was still 16 long, and the lane clamps writes to its current length, so the upper half never received the default. Masked in normal use because the processor rewrites every step each block.
- **Plugin reported audio on a silent buffer** — Switching audition off part-way through a note left the audition voice permanently flagged active, so Gradus kept telling the host the output was non-silent while writing nothing but zeros.
- **A host reporting tempo 0 while playing produced degenerate note timing** — The fallback to 120 BPM was applied only while the transport was stopped, which is the case that needs it least.
- **The editor accepted preset and session data the processor rejects** — The processor refuses state versions outside v2/v3, but the controller read the version, discarded it and parsed the payload regardless. An out-of-range stream left the processor on its defaults while the editor showed the loaded values.
- **Speed curves with more than 64 points corrupted the saved state** — The writer emitted every point while the reader consumed at most 64, so the surplus was left in the stream and every field after it — the remaining lanes' curves, the whole MIDI Delay lane block and the sequencer pattern — was read from the wrong offset. The curve editor adds points without a limit, so this was reachable by drawing a detailed curve. Both sides now honour the same 64-point bound.

### Removed

- **Dead arp-skip message handler in the editor** — The controller listened for a message the processor never sends.

## [1.9.0] - 2026-06-29

### Added

- **Note name shown in the piano roll on hover.** Hovering a cell in the Sequencer piano roll renders the note name (e.g. `D#4`) centered in that cell — on placed notes (dark text on the gold block) and on the empty "ghost" cell under the cursor (light text), so you can see the note you're about to place. Drawn directly in the view, scientific pitch notation (C4 = middle C = MIDI 60).

### Fixed

- **Sequencer mode now follows the host transport.** In Source = Sequencer the processor hard-coded the engine's "playing" flag to true on every block, so the sequencer kept clocking and emitting notes after the host pressed Stop (and could leave a stuck note). Sequencer playback is now gated on the host transport (Stop halts emission and releases sounding notes); Live mode still free-runs off held notes as before.

## [1.8.1] - 2026-06-11

### Fixed

- **Crash when closing the plugin editor window in a host (macOS/Ableton Live).** `Controller::willClose` removed the speed-curve container view — which owns the toggle, preset menu, and depth knobs as child views — *before* `dynamic_cast`ing those child pointers to unregister their control listeners. Removing the container destroyed the children, so the subsequent `dynamic_cast` dereferenced freed memory, crashing in `__dynamic_cast` (`EXC_BAD_ACCESS` / pointer-authentication failure) during window teardown. Listeners are now unregistered while the controls are still alive, before the container is removed.

### Changed

- **Added a headless editor open/close lifecycle test** (`[lifecycle]` tag) that drives the full `attached()`/`willClose()` teardown path, plus a nightly Valgrind/memcheck CI job so editor-teardown use-after-free regressions are caught automatically.

## [1.8.0] - 2026-05-24

### Added

- **Piano-Roll Sequencer mode** — A new Source dropdown (Live / Sequencer) selects whether Gradus plays held MIDI notes through its existing pattern lanes (Live, unchanged) or plays a programmable 32-step pitch pattern through the same downstream pipeline (Sequencer). When Source = Sequencer, held MIDI input transposes the pattern by `(heldNote - 60)` semitones — last-played note wins; releasing falls back to next-most-recent; no held notes = no transposition. All lane processors (Velocity, Gate, Pitch, Modifier, Ratchet, Condition, Chord, Inversion, MIDI Delay) apply identically in both modes.
- **PianoRollView UI** — VSTGUI custom view renders the Sequencer Note lane as a 48-row × N-column grid (C2 to B5, length 1–32). Left-click places a note (locks pitch to the clicked row); right-click sets a rest; click-and-drag paints the start-pitch across columns. The playhead cursor is driven by a hidden output parameter from the audio thread. The piano roll is shown only when Source = Sequencer (via `UIViewSwitchContainer`).
- **Sequencer Note lane (lane 10)** — A 10th lane inside `ArpeggiatorCore` carries the programmable pattern: 32 step pitches (0–127), 32 rest flags, length (1–32), and per-lane Speed/Swing/Jitter/SpeedCurveDepth modulators. The lane is **conditionally inert** in Live mode (no advance, no emission), so Live-mode MIDI output is byte-identical to v1.7.x.
- **71 new automatable parameters** (IDs 3741–3811) — Source mode, length, 32 pitches, 32 rest flags, 4 modulators, playhead. Sequencer-mode controls that are inapplicable in the other mode (ArpMode, Octave, Markov, Euclidean, Pin Note, Range Mapping, ScaleQuantizeInput, LatchMode) are visually disabled (greyed) and audio-thread inert when Source = Sequencer.
- **State version 3** — Preset format extended with a Sequencer Note lane appendix. v2 presets load cleanly into v3 (all rest flags default to 1, all pitches default to 60, Source defaults to Live), and their Live-mode MIDI output is verified byte-identical to v1.7.x via regression fixtures.

### Fixed

- **Lane-bounds audit (`ArpeggiatorCore`)** — Replaced hardcoded lane-count literals with the `kNumLanes` symbol everywhere. Fixes a pre-existing bug at `consumePendingCurveTables` (was iterating `< 8`, silently skipping lane 8's MIDI Delay curve table consume).

### Changed

- **`kNumLanes` bumped 9 → 10** — All per-lane storage arrays sized accordingly; retrigger reset loops iterate `< kNumLanes`. Pluginval level 5 passes with zero failures and zero warnings.

## [1.7.1] - 2026-05-22

### Fixed

- **AU and AUv3 builds failed to load on macOS hosts (e.g. Ableton Live 12).** `kSupportedNumChannels` in `audiounitconfig.h` was `0022`, which the AUv3 wrapper parsed as two configs `(0in,0out) + (2in,2out)` rather than the intended single `(0in,2out)` instrument config. The `(2,2)` entry declared stereo audio input that Gradus's processor rejects in `setBusArrangements()`, so AU init returned `kAudioUnitErr_FormatNotSupported (-10875)` and hosts surfaced "plugin couldn't be used". Corrected to `02` (one config: 0 in, 2 out), matching the SDK's `note_expression_synth_auv3` pattern. Note that even after this fix Ableton Live cannot route MIDI output from an `aumu` AU plugin — that is a Live limitation; use the VST3 build to route Gradus's arpeggiated MIDI to another instrument in Live.

### Changed

- **`audiounitconfig.h` is now generated from `audiounitconfig.h.in`** by CMake `configure_file`, with `kAUcomponentVersion` derived from `version.json`. The generated `.h` is gitignored; edit the `.in` template only.

## [1.7.0] - 2026-04-07

### Added

- **MIDI Delay lane** — New 9th lane that generates delayed echo copies of arp output notes as MIDI events. Each of the 32 steps has 7 independent parameters: Active (on/off toggle), Sync (free/synced), Delay Time (10-2000ms or note value 1/64T-4/1D), Feedback (0-16 repeats), Velocity Decay (0-100%), Pitch Shift (-24 to +24 semitones per repeat), and Gate Scaling (10-200% per repeat). Delay processing sits at the end of the signal path after all other lane transforms.
- **Multi-knob grid editor** — The DELAY tab shows a scrollable grid of ArcKnob and ToggleButton controls (7 rows x N steps). Horizontal scrolling activates when step count exceeds 10, with mouse wheel support and a draggable scrollbar. Step number bar at the bottom with playhead-highlighted current step.
- **Per-step active toggle** — Each step column has an ACTIVE power button at the top. When off, the 6 parameter controls below are hidden for a cleaner UI. When on, all controls appear and echoes are generated for that step.
- **Tempo-synced delay** — Time knob snaps to 30 discrete note values (1/64T through 4/1D) when Sync is on, with note value labels in the value popup. Free mode shows milliseconds.
- **Pitch cascade echoes** — Pitch Shift parameter transposes each successive echo by N semitones, enabling stacked intervals (fifths, octaves, etc.) that audio delay cannot produce.
- **MidiNoteDelay DSP processor** — Real-time safe echo scheduler with 256-entry fixed-size pending buffer, emergency NoteOff on overflow, iterative velocity/gate decay (no std::pow on audio thread), and sample-accurate timing across process block boundaries.
- **Delay lane integrated into ArpeggiatorCore** — The delay lane is the 9th lane inside the arpeggiator engine, advancing with the same polymetric speed/swing/jitter infrastructure as the other 8 lanes. Resets correctly on retrigger.

### Changed

- **Lane tab bar expanded to 9 tabs** — New "DELAY" tab with warm gold accent color (#D4A856).
- **Audition sound defaults to OFF** — Previously defaulted to on, causing the built-in synth to play even when routed to an external instrument.
- **Chord/inversion playhead parameter defaults** — Fixed from 1.0 to 0.0 to prevent the playhead indicator from being stuck out of range on initial load.
- **Detail strip invalidates lane view on tab switch** — Ensures playhead state renders immediately when switching to a lane that received updates while hidden.

## [1.6.0] - 2026-04-05

### Added

- **Pin grid editor UI** — Completes the v1.5 Step Pinning feature. A new inline 32-cell pin toggle row appears above the pitch bars on the Pitch lane tab, column-aligned 1:1 with the pitch step bars (the row shrinks to match the lane's active length from 1 to 32 steps). Click a cell to toggle pin state for that step without dropping into host automation. Cell alignment matches the pitch lane's bar-area left margin (`kStepContentLeftMargin` = 40px) so each pin cell sits directly above its corresponding pitch bar regardless of the chosen lane length.
- **Pin row host sync** — Preset loads, DAW automation, and undo/redo all mirror back into the pin row widget without echo loops. Host-side updates use `setDirty(true)` rather than `invalid()` for thread-safe display refresh when automation arrives on a non-UI thread.
- **Markov Chain arp mode** — New 12th arp pattern mode that picks the next held note by sampling a 7×7 transition probability matrix keyed by scale degree (I/ii/iii/IV/V/vi/vii°). Held notes are mapped to degrees using the active scale type and root note; in Chromatic mode it falls back to held-note-index indexing. When the sampled degree isn't held, the nearest held degree wins. Row-stochasticity is enforced on the fly so user-edited cells don't need manual balancing.
- **5 hardcoded Markov preset matrices** — **Uniform** (flat baseline, equivalent to Random), **Jazz** (ii→V→I voice leading with circle-of-fifths turnarounds), **Minimal** (strong self-loops + ±1 step motion for meditative patterns), **Ambient** (favors wide jumps 3-5 degrees away for spacious non-stepwise motion), and **Classical** (I–IV–V–I circle-of-fifths bias). Plus a **Custom** sentinel that activates automatically whenever any matrix cell is hand-edited.
- **Editable 7×7 matrix editor** — Custom overlay view anchored in the top-left corner of the ring display, directly below the Pattern dropdown that activates Markov mode. Click-drag cells vertically like mini sliders (top = 1.0, bottom = 0.0); brightness encodes each cell's probability. Row and column labels show Roman numerals for scale degrees. Visible only when Markov mode is the active arp pattern.
- **Collapsible Markov editor** — The matrix editor can be minimized to a compact 32×32 trigger button (with a mini 3×3 dot-grid icon) via the "–" button in its top-right corner. Click the trigger button to re-expand. The expand/collapse state is session-only and lets you hide the editor when you want full visibility of the ring display while Markov mode keeps playing.

- **Per-lane speed curve** — Each lane's speed multiplier can now vary over one loop cycle via a free-form Bezier curve. A toggle button, depth knob (0-100%), and shape preset dropdown (Flat, Sine, Triangle, Saw Up/Down, Square, Exponential) appear in the pin-row area when enabled. The curve editor overlays the lane's step bars: double-click to add points, click to select, Delete/Backspace to remove, drag handles for curvature. The center+offset model means the existing speed multiplier acts as center, and the curve offsets around it scaled by depth — creating accelerando/ritardando effects within each loop cycle.
- **Dynamic version label** — The UI now renders the plugin version from `version.h` instead of a hardcoded "v1.0.0" string.

### Changed

- **Arp mode display names use single source of truth** — Deleted 5 redundant display formatters in `formatArpParam` that duplicated strings already registered with their `StringListParameter`. Adding or renaming any arp-mode entry now requires updating exactly one location (the `createDropdownParameter` call). This also fixes a latent class of bug where display scaling formulas fell out of sync with dropdown entry counts — a regression test now guards against it.
- **Playhead indicator visibility** — All 8 lane editors now use a white-lighten overlay (instead of accent-color alpha) for the current-step playhead, ensuring visibility at all bar heights and colors.

### Fixed

- **Arp mode dropdown duplicate entry** — Extending the arp mode list to 12 entries exposed a duplicate "Diverge" label caused by a stale `value * 10.0 + 0.5` scaling formula in a hand-written display formatter. Root-caused via the refactor above.
- **Missing playhead indicator on VEL/GATE lanes** — The velocity and gate lane editors were missing the playback step overlay because they relied on a base-class `isPlaying_` flag that was never set. Now all lanes use a consistent overlay that only requires a valid step index.

## [1.5.0] - 2026-04-05

### Added

- **Concentric ring display** — Replaced horizontal lane stack with a circular step sequencer where steps are arranged radially across 4 concentric rings combining all 8 lanes; per-step colored playhead highlights race around at independent lane speeds, visually communicating polymetric relationships
- **Detail strip with lane tabs** — 8 color-coded tabs (VEL, GATE, PITCH, MOD, COND, RATCH, CHORD, INV) alongside the ring display, each opening a familiar linear step editor for precision editing; bidirectional selection syncs ring and tab
- **Direct ring editing** — Drag radially on velocity, gate, pitch, and ratchet rings to adjust values; click to cycle chord, inversion, modifier, and condition values
- **Euclidean center visualizer** — Bjorklund-algorithm dot ring in the center of the circular display showing the current Euclidean rhythm pattern (hits, steps, rotation)
- **Hover indicators** — Per-lane colored highlight on ring segments when hovering; vertical resize cursor on bar-type lanes in both ring and linear editors
- **Ring playhead trails** — Fading trail behind each lane's playhead highlight, using existing trail alpha levels for visual continuity
- **Ratchet velocity decay** — New "Rtch Dcy" parameter (0-100%) applies exponential velocity falloff across ratchet subdivisions, giving ratcheted notes a natural bouncing-ball feel instead of flat-velocity bursts
- **Strum mode for chords** — New "Strum" (0-100ms) and "Direction" (Up/Down/Random/Alternate) parameters spread chord note-on events in time, producing a guitar-strum effect for generated chords
- **Per-lane swing** — 8 new swing parameters (0-75%), one per lane, shown contextually in the detail strip for the selected lane; each lane's swing independently skews its advance timing, creating polymetric groove interactions on top of the existing per-lane speed multipliers
- **Velocity curve** — New curve shaping (Linear / Exponential / Logarithmic / S-Curve) with an amount knob (0-100%), applied after the velocity lane; shapes the dynamic feel of the whole pattern without redrawing every step. Shown contextually on the Velocity lane.
- **Transpose** — New global transpose knob (-24 to +24 semitones) in control row 1; when a non-chromatic scale is active the transpose snaps through the scale so the result always stays in key.
- **Per-lane length jitter** — 8 new jitter parameters (0-4 steps), one per lane, shown contextually in the detail strip alongside per-lane swing. Each lane independently re-rolls a random offset when it wraps, extending or shortening its effective cycle length — creating evolving non-repeating patterns that differ per lane.
- **Gravity arp mode** — New 11th arp pattern mode. Instead of traversing held notes in pitch order, Gravity picks the held note closest in pitch to the last played note, creating smooth stepwise motion through chords. Especially musical for voice-leading and legato-style arpeggios.
- **Note Range Mapping** — New global Range Low / Range High knobs (MIDI note floor/ceiling) and a Range Mode dropdown (Wrap / Clamp / Skip). When the arp + octave + pitch processing would push a note outside the range, it's folded back (Wrap), clamped to the limits (Clamp), or dropped entirely (Skip). Added to control row 1 next to the Scale controls — this is a true global effect on all output.
- **Step Pinning** — Per-step pin flags (32 automation parameters) plus a global Pin Note value. When a step is pinned, it outputs the fixed Pin Note instead of the arp-pattern note, bypassing pitch offset, transpose, scale quantization, and range mapping. Creates pedal tones, drone notes, and anchor points within moving patterns. The Pin Note knob lives contextually on the Pitch lane tab; per-step pin flags are editable via host automation (a dedicated pin-grid UI is planned for a later release).

### Changed

- **Window size** — Increased from 900x860 to 1100x816 to accommodate the ring display and detail strip side-by-side
- **UI architecture** — Lane views are now constructed in `didOpen()` and routed to the DetailStrip container; RingDataBridge reads lane data via the existing IArpLane interface without modifying shared code

## [1.0.0] - 2026-03-31

### Added

- **Standalone step arpeggiator** — Extracted from Ruinae's arpeggiator section as an independent VST3 instrument plugin with MIDI output, compatible with any DAW that supports VST3 instruments
- **8 independent polymetric lanes** — Velocity, Gate, Pitch, Chord, Inversion, Ratchet, Modifier, and Condition lanes, each with independent step counts (1-32) for non-repeating pattern evolution
- **Per-lane speed multipliers** — Each lane runs at its own clock rate (0.25x, 0.5x, 0.75x, 1x, 1.25x, 1.5x, 1.75x, 2x, 3x, 4x) using fractional accumulators, adding a second dimension of polyrhythmic complexity on top of independent lane lengths
- **10 arpeggiator modes** — Up, Down, Up/Down, Down/Up, Converge, Diverge, Random, Walk, As Played, and Chord
- **Euclidean rhythm generation** — Mathematically distributed rhythmic patterns with configurable hits (0-32), steps (2-32), and rotation offset (0-31)
- **Conditional triggers** — 18 per-step conditions including probability (10%-90%), ratio patterns (1:2, 2:3, 3:4, etc.), first note, and fill/not-fill triggers for live performance
- **Per-step ratcheting** — 1-4 sub-divisions per step with independent ratchet swing (50-75%) for rapid-fire note bursts
- **Per-step modifiers** — Active, Tie, Slide, and Accent flags per step with configurable accent velocity boost (0-127) and slide portamento time (0-500 ms)
- **Chord generation** — Per-step chord types (None, Dyad, Triad, 7th, 9th) with inversions (Root, 1st, 2nd, 3rd) and voicing modes (Close, Drop 2, Spread, Random)
- **Scale quantization** — 16 scale types (Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Mixolydian, Phrygian, Lydian, Chromatic, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone, Diminished W-H, Diminished H-W) with selectable root note (C through B); optional input note quantization
- **Spice & Dice** — Blend random pattern variations (0-100%) on top of the base pattern; re-roll with the Dice button for fresh generative results
- **Humanize** — Subtle timing variations (0-100%) for a more natural, less mechanical feel
- **Built-in audition synth** — Monophonic PolyBLEP oscillator (Sine, Sawtooth, Square) with linear ADSR envelope (adjustable decay 10-2000 ms) and volume control; session-only settings not saved in presets
- **Tempo sync and free rate** — Lock to host tempo with 30 note values (1/64T through 4/1D), or run at a free rate from 0.5-50 Hz; arp clocks independently of DAW transport
- **Latch modes** — Off, Hold, and Add modes for hands-free pattern playback
- **Retrigger modes** — Off, Note, and Beat retrigger for controlling pattern restart behavior
- **Fill mode** — Latching toggle for live performance; activates fill-conditional steps for build-ups and transitions
- **Copy/paste between lanes** — Copy step data from any lane and paste into another for quick pattern duplication and variation
- **Lane transforms** — Invert, shift left, shift right, and randomize operations per lane
- **MIDI output** — Arpeggiated notes output as standard VST3 MIDI events for driving any instrument in the DAW
- **Preset browser** — Browse and save presets with category filtering
- **40 factory presets** across 8 categories — Classic, Acid, Euclidean, Polymetric, Generative, Performance, Chords, and Advanced; all presets use the full parameter space with creative velocity contours, gate variation, pitch intervals, modifiers, ratchets, conditions, chords, scale quantization, and lane speeds
- **Preset sharing with Ruinae** — Arp parameter IDs (3000-3387) are identical between Gradus and Ruinae, enabling cross-plugin preset exchange
- **Full parameter automation** — All parameters are automatable by the host
- **Cross-platform** — VST3 (Windows, macOS, Linux) + Audio Unit v2/v3 (macOS)
- **Pluginval validated** — Passes strictness level 5 on all platforms
