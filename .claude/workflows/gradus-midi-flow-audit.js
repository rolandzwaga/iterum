export const meta = {
  name: 'gradus-midi-flow-audit',
  description: 'Deep MIDI signal-flow audit of Gradus: trace every event path (host-in -> ArpeggiatorCore -> MidiNoteDelay -> event-out + AuditionVoice), find bugs/inconsistencies, adversarially verify, and synthesize a bug->regression-test->fix implementation plan executable by an Opus-class model',
  whenToUse: 'When you want an exhaustive correctness audit of the Gradus MIDI/event path specifically (note lifecycle, event ordering, sample offsets, buffer caps, echo interaction, stuck notes) ending in a self-contained fix plan.',
  phases: [
    { title: 'Trace', detail: 'parallel readers map each stage of the MIDI path', model: 'opus' },
    { title: 'Find', detail: 'one finder per MIDI-flow bug dimension', model: 'opus' },
    { title: 'Verify', detail: 'two independent skeptics per finding', model: 'opus' },
    { title: 'Synthesize', detail: 'bug -> regression test -> fix plan', model: 'opus' },
  ],
}

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the MIDI SIGNAL FLOW of the Gradus VST3/AU plugin — a
standalone step-arpeggiator instrument (AU 'aumu', MIDI in/out + stereo
audition-only out) in the monorepo at f:/projects/iterum. This is a READ-ONLY
analysis task: DO NOT edit any files. Read source and tests, reason about
correctness.

THE MIDI PATH UNDER AUDIT (trust this shape; verify details in code):
Host MIDI-in -> Processor::process drains data.inputEvents into
arpCore_.noteOn/noteOff (velocity denormalized x127) -> per-block
arpCore_.processBlock generates ArpEvents into arpEvents_[128] ->
midiDelay_.process merges arp events with scheduled geometric echoes into
combinedEvents_[512] -> routed to data.outputEvents as VST NoteOn/NoteOff
(channel 0, noteId -1) AND fed to the built-in monophonic AuditionVoice.

KEY FILES:
- plugins/gradus/src/processor/processor.cpp / processor.h (event drain,
  transport, param changes, output routing, audition)
- dsp/include/krate/dsp/processors/arpeggiator_core.h (+ .cpp for
  processBlock/fireStep bodies) — jump-ahead loop, coincident-event priority
  BarBoundary > NoteOff > Step > SubStep; fireStep runs gate/condition/
  modifier/velocity/pitch/range/humanize/ratchet/strum; every NoteOn schedules
  a guaranteed NoteOff
- dsp/include/krate/dsp/processors/midi_note_delay.h — geometric echo trains
  per NoteOn (feedback <=16, velocity decay, pitch shift, gate scaling),
  256-slot buffer, oldest-stealing overflow guard
- dsp/include/krate/dsp/primitives/held_note_buffer.h — HeldNoteBuffer (cap
  32, dedup, insertion + pitch-sorted arrays) + NoteSelector (12 ArpModes,
  octave expansion)
- dsp/include/krate/dsp/primitives/arp_lane.h — 10 polymetric lanes, each with
  own speed multiplier, swing, length-jitter, optional baked 256-entry
  speed-curve tables (double-buffered off-thread, consumed on audio thread)
- plugins/gradus/src/dsp/audition_voice.h — monophonic monitor voice

INTENTIONAL DESIGN FACTS (do NOT report these as bugs; DO report drift
within/around them):
- Arp param IDs 3000-3372 are byte-shared with Ruinae. Save prefix is unified
  (Krate::Shared::saveArpParamsShared); the LOAD path is deliberately
  Gradus-local because clamp ranges diverge (mode clamps 0-11 here vs 0-9 in
  Ruinae). The split itself is intentional.
- operatingMode is always forced to kArpMIDI after load and re-asserted every
  block — the arp is never bypassed. Audition params (4000-4003) are
  session-only, never serialized.
- Change-gated setters use prev-value caches (prevArpMode_ etc.) so engine
  step state is not reset every block; sourceMode toggle additionally calls
  requestPanicNoteOff(). Note: the caches are NOT reset in setState — decide
  from code whether that is a bug.
- Transport edges belong to Sequencer source-mode only. Live mode free-runs;
  a rising play edge must NOT reset the engine there (reset would clear
  heldNotes_ and orphan sounding notes without NoteOffs).
- requestPanicNoteOff() discharges unconditionally at the top of processBlock;
  the older needsDisableNoteOff_ paths fire only when the arp is disabled or
  the held buffer just emptied.
- Lanes are addressed in TWO orders that disagree at indices 3/4/5: lane-param
  order (getArpLane) is Vel Gate Pitch Ratchet Modifier Condition Chord Inv
  Delay; ring/UI order (subZoneToLaneIndex, ringDataBridge_) is Vel Gate Pitch
  Modifier Condition Ratchet Chord Inv. Ring-supplied indices must resolve via
  getRingLaneStepBaseParamId / ringDataBridge_.laneAt. Mixing the tables is a
  BUG; the existence of two tables is not.

REAL-TIME AUDIO RULES (violations are bugs): no allocation/locks/exceptions/
IO on the audio thread (anything reachable from process()); parameters at the
VST boundary are normalized [0,1] and denormalized in processParameterChanges;
NaN/Inf must not propagate; the engine is zero-heap / all-noexcept — fixed
arrays only.

MIDI-INSTRUMENT HAZARD CHECKLIST (the core of this audit): stuck notes (every
NoteOn must have a guaranteed NoteOff across latch modes, source-mode toggles,
arp mode changes mid-hold, transport stop/start, setActive(false), reset,
state reload while notes held, buffer overflow); double NoteOffs; event
sample-offset ordering within a block (host contract: offsets ascending, in
[0, blockSize)); NoteOff arriving before its NoteOn at the same offset;
velocity 0 NoteOn semantics; events dropped silently at the 128/512/256 caps
(and WHICH events get dropped — dropping a NoteOff is far worse than dropping
a NoteOn); echoes outliving or re-triggering after their source note is gone;
channel/noteId consistency; tempo or sample-rate change mid-pattern
invalidating scheduled sample offsets.

Report format discipline: every finding must cite file and line(s) you
actually read. If you cannot point at concrete code, do not report it.
`

const MAP_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['summary', 'key_files', 'invariants', 'oddities'],
  properties: {
    summary: { type: 'string', description: 'dense prose map of this stage of the MIDI path (data flow, timing model, threading)' },
    key_files: { type: 'array', items: { type: 'string' }, description: 'repo-relative paths with 3-8 word role each, "path — role"' },
    invariants: { type: 'array', items: { type: 'string' }, description: 'rules this stage must uphold (with file:line evidence)' },
    oddities: { type: 'array', items: { type: 'string' }, description: 'suspicious/confusing things not fully investigated — leads for the finder phase, each with file:line' },
  },
}

const FINDING_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['findings'],
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'severity', 'category', 'file', 'line', 'problem', 'why_wrong', 'consequence', 'repro_sketch', 'suggested_fix'],
        properties: {
          title: { type: 'string', description: 'one-line summary' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'] },
          category: { type: 'string', description: 'kebab-case, e.g. stuck-notes, event-ordering, buffer-overflow, echo-lifecycle, timing-math, rt-safety, state-consistency, test-gap' },
          file: { type: 'string', description: 'repo-relative path' },
          line: { type: 'string', description: 'line number or range, e.g. "446" or "446-449"' },
          problem: { type: 'string', description: 'what the code does that is wrong' },
          why_wrong: { type: 'string', description: 'the correct behaviour and why this deviates' },
          consequence: { type: 'string', description: 'concrete user-visible consequence (stuck note, dropped event, wrong timing...)' },
          repro_sketch: { type: 'string', description: 'concrete event sequence / host scenario that triggers it (e.g. "hold 3 notes, toggle latch, stop transport at offset X")' },
          suggested_fix: { type: 'string', description: 'concrete fix direction incl. which test would catch it' },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['refuted', 'reasoning', 'severity_adjustment'],
  properties: {
    refuted: { type: 'boolean', description: 'true if the finding is wrong, already handled elsewhere, or intentional per the stated design facts' },
    reasoning: { type: 'string', description: 'cite the exact code you read to reach this verdict' },
    severity_adjustment: { type: 'string', enum: ['none', 'upgrade', 'downgrade'], description: 'if not refuted, should severity change?' },
  },
}

const DOC_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['markdown'],
  properties: { markdown: { type: 'string' } },
}

// ---------------------------------------------------------------------------
// Phase 1: Trace — parallel stage readers.
// Barrier justified: every finder receives the combined map.
// ---------------------------------------------------------------------------
const STAGES = [
  {
    key: 'input-drain',
    prompt: `Map the INPUT stage: how Processor::process (plugins/gradus/src/
processor/processor.cpp, read fully with processor.h) drains data.inputEvents
into the engine. Cover: event-type handling (which VST event kinds are
consumed/ignored), velocity denormalization x127 (rounding? zero-velocity
NoteOn?), sample-offset handling of input events (are offsets used or
flattened to block start?), ordering guarantees, transport
processing (playing edge, tempo/PPQ extraction), processParameterChanges and
the change-gated setter caches, setActive/setupProcessing/reset paths, and
what happens when data.inputEvents is null or process is called with 0
samples.`,
  },
  {
    key: 'engine-core',
    prompt: `Map the ENGINE stage: dsp/include/krate/dsp/processors/
arpeggiator_core.h AND its .cpp (read processBlock and fireStep fully).
Cover: the jump-ahead loop structure and its coincident-event priority
(BarBoundary > NoteOff > Step > SubStep), how NoteOffs are scheduled and
guaranteed, latch modes (Off/Hold/Add) x source modes (Live/Sequencer) noteOff
branching, panic/disable note-off paths, step advance + polymetric lane
interaction, ratchet/strum sub-events, swing and gate-length-to-samples math,
tempo/sample-rate change handling mid-pattern, and the emission contract into
the caller's event array (capacity respected? what drops on overflow?).`,
  },
  {
    key: 'note-selection',
    prompt: `Map the NOTE-SELECTION stage: dsp/include/krate/dsp/primitives/
held_note_buffer.h (read fully) and the NoteSelector's 12 ArpMode traversals
with octave expansion, plus dsp/include/krate/dsp/primitives/arp_lane.h.
Cover: dedup on repeated NoteOn, cap-32 overflow behaviour, removal while the
selector index points at/past the removed slot, pitch-sorted vs insertion
order use, octave range boundaries, Markov mode sampling, and lane stepping
(speed multipliers, length jitter, baked speed-curve table double-buffering —
who writes, who reads, on which thread).`,
  },
  {
    key: 'echo-delay',
    prompt: `Map the ECHO stage: dsp/include/krate/dsp/processors/
midi_note_delay.h (read fully) and how the Gradus processor calls
midiDelay_.process to merge arpEvents_[128] into combinedEvents_[512].
Cover: how echo trains are scheduled per NoteOn (feedback count, velocity
decay, pitch shift, gate scaling), the 256-slot schedule buffer and its
oldest-stealing overflow guard (can it steal a pending NoteOff?), how echo
NoteOffs are paired with echo NoteOns, behaviour when delay params change
while echoes are pending, reset/panic interaction (does panic clear pending
echoes?), and merge ordering of pass-through vs echo events in the output
array.`,
  },
  {
    key: 'output-audition',
    prompt: `Map the OUTPUT stage: how plugins/gradus/src/processor/
processor.cpp routes combinedEvents_ to data.outputEvents (VST NoteOn/NoteOff
construction: channel, pitch clamping, velocity re-normalization, sample
offset, noteId) and simultaneously feeds plugins/gradus/src/dsp/
audition_voice.h (read fully). Cover: offset ordering of emitted events,
pitch/velocity range clamping, the audition voice's mono note-stealing and
release behaviour, audition atomics threading, whether audition and event-out
can disagree about which notes are sounding, and output-bus silence/flags
handling.`,
  },
  {
    key: 'tests-map',
    prompt: `Map EXISTING MIDI-flow test coverage: plugins/gradus/tests/ (list
all files, read the ones touching processor events, arp engine, midi delay,
stuck notes, transport, state reload) plus dsp/tests/unit/processors/ tests
for arpeggiator_core and midi_note_delay. Cover: what event-flow behaviours
are pinned by tests today, what harnesses exist (how do tests drive
process()/processBlock with synthetic events — name the helpers/fixtures a
new regression test should reuse), and which hazard-checklist items have NO
coverage. Your oddities list = specific untested hazards.`,
  },
]

phase('Trace')
log('Tracing 5 MIDI-path stages in parallel (Opus readers)...')
const maps = await parallel(
  STAGES.map(s => () =>
    agent(
      `${CONTEXT}\n\nYOUR STAGE: ${s.prompt}\n\nReturn a dense, factual map. The "oddities" list is the most valuable output — every suspicious thing you notice becomes a lead for the bug-finder phase.`,
      { label: `trace:${s.key}`, phase: 'Trace', schema: MAP_SCHEMA, model: 'opus' },
    ).then(m => ({ key: s.key, ...m })),
  ),
)
const goodMaps = maps.filter(Boolean)
if (goodMaps.length < STAGES.length) {
  log(`WARNING: only ${goodMaps.length}/${STAGES.length} stage maps completed — finders proceed with partial context`)
}

const MAP_DIGEST = goodMaps
  .map(m => `## ${m.key}\n${m.summary}\nKEY FILES:\n${m.key_files.join('\n')}\nINVARIANTS:\n${m.invariants.join('\n')}\nODDITIES (investigate these):\n${m.oddities.join('\n')}`)
  .join('\n\n')

// ---------------------------------------------------------------------------
// Phase 2: Find — one finder per MIDI-flow bug dimension, all fed the map.
// Barrier justified: dedup below needs ALL findings at once.
// ---------------------------------------------------------------------------
const DIMENSIONS = [
  {
    key: 'stuck-notes',
    prompt: `Hunt STUCK-NOTE bugs: trace EVERY path a NoteOn takes to its
NoteOff and find the one that doesn't arrive. Cover: latch Off/Hold/Add x
source Live/Sequencer, arp mode / note-value / octave changes mid-hold,
transport stop and start edges in both source modes, setActive(false) and
reset() while notes sound, state reload (setState) during playback, panic
paths racing normal emission, disable-arp mid-note, held-buffer overflow at
cap 32 evicting a sounding note, and echoes whose NoteOff schedule is
cleared/stolen while their NoteOn already went out.`,
  },
  {
    key: 'event-ordering',
    prompt: `Hunt EVENT ORDERING and TIMING bugs: sample offsets of emitted
events not monotonically ascending in data.outputEvents; NoteOff and NoteOn
of the same pitch at the same offset emitted in the wrong order (retrigger
click / dropped note in hosts); coincident-event priority (BarBoundary >
NoteOff > Step > SubStep) violated in the jump-ahead loop; offsets computed
outside [0, blockSize); swing/ratchet/strum/humanize pushing an event past
the block edge and how the carry-over works; gate-length-to-samples rounding;
tempo or sample-rate change mid-block invalidating already-scheduled offsets;
input events' own offsets being ignored (all treated as block-start) and
whether that loses inter-block phase.`,
  },
  {
    key: 'buffer-caps',
    prompt: `Hunt BUFFER-CAP and OVERFLOW bugs at every fixed capacity on the
path: arpEvents_[128], combinedEvents_[512], MidiNoteDelay's 256-slot
schedule (oldest-stealing), HeldNoteBuffer cap 32, and any output-event-list
limits. For each: what exactly happens at the boundary, is the check < or <=
correct, can a NoteOff be the thing dropped/stolen (stuck note), is the drop
silent, and can a realistic setting (max ratchet x strum x 16-feedback
echoes x fast rate) actually hit it? Compute worst-case event counts from the
code, don't guess.`,
  },
  {
    key: 'echo-consistency',
    prompt: `Hunt ECHO/DELAY LIFECYCLE bugs in midi_note_delay.h and its
processor integration: echo NoteOn emitted whose NoteOff was never scheduled
or later stolen; velocity decay reaching 0 or negative (and what a
velocity-0 NoteOn means downstream); pitch shift pushing echoes outside
0-127; param changes (delay time, feedback, decay) while echoes are pending
— are in-flight schedules re-scaled, orphaned, or corrupted; panic /
requestPanicNoteOff / reset interaction with pending echoes; echoes of
latched notes after latch release; merge logic double-emitting or dropping
pass-through events; tempo-synced delay time vs tempo change mid-tail.`,
  },
  {
    key: 'boundary-consistency',
    prompt: `Hunt BOUNDARY/REPRESENTATION inconsistencies along the path:
velocity denormalized x127 on input vs how it is re-normalized on output
(round-trip loss? 1.0 -> 127 -> 1.0?); velocity-0 NoteOn treated as NoteOff
anywhere (VST3 hosts may send it); pitch stored as int vs float vs int16
along the chain (truncation, sign); channel forced to 0 on output but input
channel ignored or used inconsistently; noteId -1 vs hosts that supply real
noteIds (are input noteIds echoed or discarded, and does NoteOff matching
depend on them?); tuning/detune fields left uninitialized in emitted VST
events; the audition voice hearing different pitches/velocities than the
event output (clamping applied to one but not the other).`,
  },
  {
    key: 'rt-safety-state',
    prompt: `Hunt RT-SAFETY and STATE bugs on the MIDI path specifically: any
allocation/lock/IO reachable from process() through arpCore_/midiDelay_/
audition; the speed-curve double-buffer handoff (torn reads, wrong memory
ordering, use of a table mid-swap); prev-value setter caches not reset in
setState (does a reload with identical values skip a needed engine
reconfigure, or a changed value fail to trigger panic?); setState during
active processing (what thread, what guards); processParameterChanges applied
before vs after event drain in the same block (one-block lag or reordering
hazards); operatingMode re-assertion interactions.`,
  },
  {
    key: 'gestalt-flow',
    prompt: `You are the GESTALT reviewer for the MIDI path: read
plugins/gradus/src/processor/processor.cpp end to end, then
dsp/include/krate/dsp/processors/arpeggiator_core.cpp's processBlock and
fireStep end to end, and flag anything that just doesn't make sense —
comments contradicting code, branches that can't execute, work done twice,
values computed then ignored, ordering that looks accidental, asymmetries
between paired paths (noteOn vs noteOff, start vs stop, save vs load).
Your value is noticing what the category-driven finders miss.`,
  },
]

phase('Find')
log(`Fanning out ${DIMENSIONS.length} finders (Opus)...`)
const found = await parallel(
  DIMENSIONS.map(d => () =>
    agent(
      `${CONTEXT}\n\nSTAGE MAPS from the trace phase (leads in ODDITIES are gold):\n${MAP_DIGEST}\n\nYOUR DIMENSION: ${d.prompt}\n\nRead the actual code before reporting anything. Quality over quantity: a finding without a concrete file:line you personally read is worthless. Include a concrete repro_sketch (event sequence / host scenario) for every finding — the synthesis phase turns it into a regression test.`,
      { label: `find:${d.key}`, phase: 'Find', schema: FINDING_SCHEMA, model: 'opus' },
    ).then(r => r.findings.map(f => ({ ...f, dimension: d.key }))),
  ),
)
const failedFinders = DIMENSIONS.filter((_, i) => !found[i]).map(d => d.key)
if (failedFinders.length) log(`WARNING: finders failed/skipped: ${failedFinders.join(', ')} — their dimension is UNCOVERED`)

// Dedup by file + overlapping start line (plain code, needs all findings).
const all = found.filter(Boolean).flat()
const deduped = []
for (const f of all) {
  const dup = deduped.find(
    g => g.file === f.file && g.line.split('-')[0] === f.line.split('-')[0],
  )
  if (dup) {
    dup.also_flagged_by = (dup.also_flagged_by || []).concat(f.dimension)
  } else {
    deduped.push(f)
  }
}
log(`${all.length} raw findings -> ${deduped.length} after dedup`)

// ---------------------------------------------------------------------------
// Phase 3: Verify — two independent skeptics per finding.
// Kill if both refute; CONFIRMED if both uphold; PLAUSIBLE if split.
// ---------------------------------------------------------------------------
phase('Verify')
const LENSES = [
  'correctness (re-derive the claimed behaviour from source — does the code actually do what the finding says?)',
  'context (is it intentional, guarded elsewhere, unreachable in practice, or already covered by a test/invariant? Walk the repro_sketch step by step against the code)',
]
const verified = await parallel(
  deduped.map(f => () =>
    parallel(
      LENSES.map(lens => () =>
        agent(
          `${CONTEXT}\n\nA reviewer claims this MIDI-flow bug in Gradus. Your job is to REFUTE it if you can. Lens: ${lens}.\n\nFINDING:\n${JSON.stringify(f, null, 2)}\n\nRead ${f.file} around line ${f.line} AND every code path the claim depends on. Default to refuted=true if you cannot positively confirm the defect from source.`,
          { label: `verify:${f.file.split('/').pop()}:${f.line}`, phase: 'Verify', schema: VERDICT_SCHEMA, model: 'opus' },
        ),
      ),
    ).then(votes => {
      const v = votes.filter(Boolean)
      const refutes = v.filter(x => x.refuted).length
      const verdict = refutes === 0 && v.length >= 2 ? 'CONFIRMED' : refutes >= v.length ? 'REFUTED' : 'PLAUSIBLE'
      return { ...f, verdict, verifier_notes: v.map(x => x.reasoning), severity_votes: v.map(x => x.severity_adjustment) }
    }),
  ),
)
const surviving = verified.filter(Boolean).filter(f => f.verdict !== 'REFUTED')
const confirmed = surviving.filter(f => f.verdict === 'CONFIRMED')
log(`${surviving.length} findings survive (${confirmed.length} CONFIRMED, ${surviving.length - confirmed.length} PLAUSIBLE); ${verified.filter(Boolean).length - surviving.length} refuted`)

// ---------------------------------------------------------------------------
// Phase 4: Synthesize — implementation plan for an Opus-class executor.
// Every bug gets: description -> regression test that PROVES it -> fix.
// ---------------------------------------------------------------------------
phase('Synthesize')
const plan = await agent(
  `${CONTEXT}\n\nEXISTING TEST HARNESS knowledge (from the trace phase):\n${goodMaps.filter(m => m.key === 'tests-map').map(m => m.summary + '\n' + m.invariants.join('\n')).join('\n') || '(tests map unavailable — the plan must tell the executor to inspect plugins/gradus/tests/ for harness fixtures first)'}\n\nYou are writing a DETAILED IMPLEMENTATION PLAN to fix the verified MIDI-flow findings below. The plan will be executed by a LESS CAPABLE model working task-by-task with no memory of this audit, so it must be fully self-contained: never say "fix the issue" — spell out the exact edit intent, the file:line anchors, and the acceptance check.\n\nVERIFIED FINDINGS (CONFIRMED = both skeptics upheld; PLAUSIBLE = one did — for these the regression test doubles as the verification: if it passes on current code, mark the finding refuted, keep the test, skip the fix):\n${JSON.stringify(surviving, null, 2)}\n\nPLAN REQUIREMENTS:\n- Markdown document. Title, one-paragraph scope summary, then a findings table (ID GMF-001..., title, severity, verdict, category, phase).\n- Order phases: (1) critical/high stuck-note & event-loss bugs, (2) ordering/timing bugs, (3) medium consistency bugs, (4) low/cleanup, (5) test-gap fills with no code change. Within a phase, no task may depend on a later one; call out any task that MUST land before another.\n- EACH TASK has exactly three parts, in this order:\n  A. THE BUG — restate it self-contained: file:line, wrong behaviour, correct behaviour, the triggering event sequence (from repro_sketch).\n  B. THE REGRESSION TEST — exact test file to create or extend under plugins/gradus/tests/ (or dsp/tests/unit/processors/ for pure-engine bugs), exact TEST_CASE name string, the harness/fixture to reuse, the synthetic event sequence to drive, and the precise assertions (expected event types/pitches/velocities/offsets). State explicitly: "this test MUST FAIL on current code before the fix; run it and confirm the failure first."\n  C. THE FIX — exact files/functions, before -> after behaviour, edit intent precise enough to implement without re-deriving the analysis, plus any invariant that must NOT break (byte-golden shared save, Ruinae param sharing, buffer caps, RT-safety).\n- After each task: build target gradus_tests with "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests, then run build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5. If the task touched dsp/include/krate/dsp/, ALSO build+run dsp_processors_tests (and dsp_primitives_tests if primitives changed) AND ruinae_tests — flag such tasks CROSS-PLUGIN IMPACT. Pluginval strictness 5 on build/windows-x64-release/VST3/Release/Gradus.vst3 + clang-tidy (-Target gradus) before each commit.\n- End with a completion checklist mirroring the repo's Completion Honesty rules: each finding re-verified against code with file:line evidence, each regression test observed to fail-then-pass, no relaxed thresholds, no silently skipped tasks.\nReturn the full markdown.`,
  { label: 'synthesize:plan', phase: 'Synthesize', schema: DOC_SCHEMA, model: 'opus' },
)

return {
  stats: {
    maps: goodMaps.length,
    finders_failed: failedFinders,
    raw_findings: all.length,
    deduped: deduped.length,
    confirmed: confirmed.length,
    plausible: surviving.length - confirmed.length,
    refuted: verified.filter(Boolean).length - surviving.length,
  },
  findings: surviving,
  plan_markdown: plan ? plan.markdown : null,
}
