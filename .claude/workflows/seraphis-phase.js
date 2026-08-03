export const meta = {
  name: 'seraphis-phase',
  description: 'Three-stage speckit-style pipeline for one Seraphis roadmap phase: specify (spec→challenge→clarify-scan), plan (encode grill answers→plan→challenge→tasks), build (implement→build/test→comply) — adversarial agents check every artifact, the user is grilled between specify and plan',
  whenToUse: 'When building a phase of the Seraphis synth from specs/Seraphis-roadmap.md. Flow: (1) run args {phase: N, stage: "specify"} — returns clarification questions; (2) the MAIN LOOP grills the user (grill-me skill) until every question is resolved; (3) run {phase: N, stage: "plan", clarifications: {...answers...}}; (4) user reviews spec/plan/tasks; (5) run {phase: N, stage: "build"}. Artifacts land in specs/<slug>/. Nothing commits.',
  phases: [
    { title: 'Specify', detail: 'spec.md with FR/SC from roadmap phase', model: 'opus' },
    { title: 'Challenge Spec', detail: 'parallel skeptics + bounded revision loop', model: 'opus' },
    { title: 'Clarify Scan', detail: 'speckit.clarify-style ambiguity questionnaire for the user grill', model: 'opus' },
    { title: 'Encode Clarifications', detail: 'integrate grill answers into spec, gate on unresolved', model: 'opus' },
    { title: 'Plan', detail: 'plan.md, reuse APIs verified against real headers', model: 'opus' },
    { title: 'Challenge Plan', detail: 'layer/RT/reuse skeptics + revision', model: 'opus' },
    { title: 'Tasks', detail: 'dependency-ordered tasks.md with parallel groups', model: 'opus' },
    { title: 'Dispatch', detail: 'parse tasks.md into executable groups', model: 'sonnet' },
    { title: 'Implement', detail: 'one agent per task, TDD, disjoint files', model: 'opus' },
    { title: 'Build+Test', detail: 'build + run suites (sonnet reporter), bounded fix loop (opus fixer)', model: 'opus' },
    { title: 'Comply', detail: 'adversarial FR/SC verification, real output only', model: 'opus' },
    { title: 'Report', detail: 'compliance.md + honest gap list (sonnet); roadmap marked done if COMPLETE (haiku)', model: 'sonnet' },
  ],
}

// ---------------------------------------------------------------------------
// Args / phase registry
// ---------------------------------------------------------------------------
const SLUGS = {
  1: 'seraphis-phase1-life-modulators',
  2: 'seraphis-phase2-harmonic-cloud',
  3: 'seraphis-phase3-spectral-morph',
  4: 'seraphis-phase4-continuous-body',
  5: 'seraphis-phase5-atmosphere',
  6: 'seraphis-phase6-aether-space',
  7: 'seraphis-phase7-voice-engine',
  8: 'seraphis-phase8-plugin-scaffold',
  9: 'seraphis-phase9-parameters',
  10: 'seraphis-phase10-effects',
  11: 'seraphis-phase11-ui',
  12: 'seraphis-phase12-presets-release',
}

// Tolerate args arriving as a JSON string (some launch paths stringify).
let A = args
if (typeof A === 'string') {
  try { A = JSON.parse(A) } catch (e) { throw new Error(`args arrived as unparseable string: ${A}`) }
}
const phaseNum = A && A.phase
const stage = (A && A.stage) || 'specify'
if (!SLUGS[phaseNum]) throw new Error(`args.phase must be 1-12, got: ${JSON.stringify(phaseNum)}. Usage: {phase: 1, stage: "specify"|"plan"|"build", clarifications?: {...}}`)
if (stage !== 'specify' && stage !== 'plan' && stage !== 'build') throw new Error(`args.stage must be "specify", "plan" or "build", got: ${JSON.stringify(stage)}`)

const SLUG = SLUGS[phaseNum]
const DIR = `specs/${SLUG}`
const SPEC = `${DIR}/spec.md`
const PLAN = `${DIR}/plan.md`
const TASKS = `${DIR}/tasks.md`
const COMPLIANCE = `${DIR}/compliance.md`

// ---------------------------------------------------------------------------
// Shared context handed to every agent.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are working on SERAPHIS, a new spectral-organism synthesizer in the Krate
Audio monorepo at f:/projects/iterum. The authoritative roadmap is
specs/Seraphis-roadmap.md — READ IT FIRST (the whole file for context, then
focus on Phase ${phaseNum}, spec slug "${SLUG}").

The phase artifacts live in ${DIR}/ (spec.md, plan.md, tasks.md).

MONOREPO FACTS:
- Shared DSP library: dsp/include/krate/dsp/{core,primitives,processors,systems,effects}/
  namespace Krate::DSP, header-only, 5 layers. Layer N may only include layers < N.
  Unit tests mirror layers in dsp/tests/unit/<layer>/, registered in the layer's
  test CMake list, run via per-layer exes (dsp_processors_tests.exe etc.).
- Plugins live under plugins/ (iterum, disrumpo, ruinae, innexus, gradus, membrum, shared).
  Seraphis plugin work only starts at Phase 8.
- Build (Windows, ALWAYS full cmake path):
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>
  Run one suite directly: build/windows-x64-release/bin/Release/<target>.exe 2>&1 | tail -5
  Filter Catch2 by test name: <exe> "TestName*"  (positional arg, not -c; tags in [brackets]).

CROSS-CUTTING CONSTRAINTS (violations are defects at every stage):
- RT safety: no allocation/locks/exceptions/IO on the audio thread; pools sized at prepare.
- Layer discipline: every new component declares its layer; includes point downward only.
- ODR: before ANY new class name, sweep: grep -r "class <Name>" dsp/ plugins/
- CPU budgets are functional requirements, measured in tests, not aspirations.
- NO bit-exact float goldens — use render_fingerprint.h / measured tolerances.
- Portability: MSVC-green proves nothing; code must satisfy node tools/check-portability.js;
  no std::isnan under -ffast-math (bit-pattern check instead); no narrowing in brace init;
  SIMD loads/stores unaligned (hn::LoadU/StoreU) unless alignment is proven.
- Naming: classes PascalCase, functions camelCase, members trailing underscore,
  constants kPascalCase, params k{Section}{Parameter}Id with the standard names table.
- Modulation sources conform to the existing ModulationSource concept so
  ModulationEngine / VoiceModRouter route them unchanged.

DISCIPLINE: every claim about existing code must cite a file:line you actually
read this session. Never invent an API — open the header and quote the real
signature.
`

// Slim context for MECHANICAL roles (reporters, transcribers, gates): no
// roadmap read — the full roadmap re-read across dozens of agents was the
// single fattest redundant input cost. Everything these roles need is in
// their prompt and the files it names.
const CONTEXT_LITE = `
You are working in the Krate Audio monorepo at f:/projects/iterum, on the
SERAPHIS synthesizer, roadmap Phase ${phaseNum} (spec slug "${SLUG}").
Phase artifacts: ${DIR}/ (spec.md, plan.md, tasks.md). Do NOT read
specs/Seraphis-roadmap.md — everything you need is in this prompt and the
files it names.

Build (Windows, ALWAYS full cmake path):
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>
Run one suite directly: build/windows-x64-release/bin/Release/<target>.exe 2>&1 | tail -5
Filter Catch2 by test name: <exe> "TestName*"  (positional arg, not -c; tags in [brackets]).

DISCIPLINE: report only what you actually observed; quote real output
verbatim, never paraphrase or invent.
`

// ---------------------------------------------------------------------------
// Model policy (tiered 2026-08-03; was: blanket Opus):
// - AUTHOR (opus): authoring + judgment + verification roles. Errors here are
//   SILENT (a lazy comply verifier rubber-stamps a broken FR; a weak spec
//   ships wrong requirements) or rebound as expensive Opus fix loops (impl).
//   Covers: specify, plan, tasks, clarify-scan, revise, impl, fixer,
//   comply, remediate, reverify, and the judgment-lens skeptics.
// - MECH (sonnet): transcription/reporting/gate roles. Errors here are LOUD
//   (schema mismatch, red build gate, reviewable diff), so cheaper is safe.
//   Covers: dispatch, build reporter (effort low), recheck, clarify-gate
//   (effort low), encode-clarifications, report, and the reality-lens
//   skeptics (open-header signature cross-checks). These also get
//   CONTEXT_LITE where the roadmap is irrelevant.
// - TRIVIAL (haiku): mark-roadmap (one status line, self-contained prompt).
// Deliberately NOT tiered yet: impl agents (DSP quality; revisit for a
// mostly-mechanical phase like presets) and all comply lenses (honesty gate).
// ---------------------------------------------------------------------------
const AUTHOR = 'opus'
const MECH = 'sonnet'
const TRIVIAL = 'haiku'
const run = (prompt, opts) => agent(prompt, { model: AUTHOR, ...opts })

// ---------------------------------------------------------------------------
// Schemas
// ---------------------------------------------------------------------------
const DOC_STATUS = {
  type: 'object',
  additionalProperties: false,
  required: ['path', 'summary', 'open_questions'],
  properties: {
    path: { type: 'string', description: 'repo-relative path of the file written' },
    summary: { type: 'string', description: 'dense summary of the document content' },
    open_questions: { type: 'array', items: { type: 'string' }, description: 'decisions deliberately left to the user (empty if none)' },
  },
}

const QUESTIONS_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['questions'],
  properties: {
    questions: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['id', 'area', 'question', 'why_it_matters', 'options', 'recommendation'],
        properties: {
          id: { type: 'string', description: 'Q1, Q2, ... — stable ids the grill answers key on' },
          area: { type: 'string', description: 'taxonomy bucket: functional-scope | behaviour-contract | data-and-state | non-functional | integration | edge-cases | terminology' },
          question: { type: 'string', description: 'one precise question; different answers must lead to materially different specs' },
          why_it_matters: { type: 'string', description: 'which FR/SC is ambiguous without the answer, and what could be built wrong' },
          options: { type: 'array', items: { type: 'string' }, description: '2-4 concrete candidate answers with their consequences, one line each' },
          recommendation: { type: 'string', description: 'the option you would pick and why (one sentence)' },
        },
      },
    },
  },
}

const ISSUES_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['issues'],
  properties: {
    issues: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['severity', 'section', 'problem', 'suggested_resolution'],
        properties: {
          severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
          section: { type: 'string', description: 'heading / FR id / SC id the issue is in' },
          problem: { type: 'string', description: 'what is wrong, with evidence (file:line for code claims)' },
          suggested_resolution: { type: 'string', description: 'concrete edit that would resolve it' },
        },
      },
    },
  },
}

const RECHECK_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['resolved', 'unresolved'],
  properties: {
    resolved: { type: 'boolean', description: 'true if every blocker and major issue is now addressed in the document' },
    unresolved: { type: 'array', items: { type: 'string' }, description: 'issues still not addressed, quoting the document' },
  },
}

const TASKGROUPS_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['test_targets', 'groups'],
  properties: {
    test_targets: { type: 'array', items: { type: 'string' }, description: 'cmake test targets this phase touches, e.g. dsp_processors_tests' },
    groups: {
      type: 'array',
      description: 'executed strictly in order; tasks within a group run in parallel only if parallel=true',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['name', 'parallel', 'tasks'],
        properties: {
          name: { type: 'string' },
          parallel: { type: 'boolean', description: 'true ONLY if all tasks touch fully disjoint NEW files (no shared CMakeLists/headers edits)' },
          tasks: {
            type: 'array',
            items: {
              type: 'object',
              additionalProperties: false,
              required: ['id', 'title', 'instructions', 'files'],
              properties: {
                id: { type: 'string', description: 'task id from tasks.md, e.g. T003' },
                title: { type: 'string' },
                instructions: { type: 'string', description: 'full self-contained instructions copied/expanded from tasks.md: test to write first, exact assertions, then implementation' },
                files: { type: 'array', items: { type: 'string' }, description: 'every file this task creates or edits' },
              },
            },
          },
        },
      },
    },
  },
}

const IMPL_RESULT = {
  type: 'object',
  additionalProperties: false,
  required: ['task_id', 'status', 'files_written', 'notes'],
  properties: {
    task_id: { type: 'string' },
    status: { type: 'string', enum: ['done', 'blocked'] },
    files_written: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string', description: 'deviations from the task instructions, or why blocked' },
  },
}

const BUILD_RESULT = {
  type: 'object',
  additionalProperties: false,
  required: ['build_ok', 'tests_ok', 'summary', 'failures'],
  properties: {
    build_ok: { type: 'boolean' },
    tests_ok: { type: 'boolean' },
    summary: { type: 'string', description: 'the actual Catch2 summary line(s) verbatim, one per target' },
    failures: { type: 'string', description: 'compiler errors / failing test names + assertion output, verbatim excerpts; empty if green' },
  },
}

const COMPLY_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['items'],
  properties: {
    items: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['id', 'verdict', 'evidence'],
        properties: {
          id: { type: 'string', description: 'FR-xxx or SC-xxx' },
          verdict: { type: 'string', enum: ['pass', 'fail', 'partial'] },
          evidence: { type: 'string', description: 'file:line for FRs; verbatim test/measurement output with real numbers for SCs. "Implemented" without specifics is INVALID.' },
        },
      },
    },
  },
}

const DOC_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['markdown'],
  properties: { markdown: { type: 'string' } },
}

// ---------------------------------------------------------------------------
// Helper: challenge → revise → recheck loop for an authored document.
// Bounded at 2 revision rounds; unresolved issues are surfaced, never hidden.
// ---------------------------------------------------------------------------
async function challengeAndRevise(docPath, docKind, skeptics, phaseTitle) {
  let round = 0
  let unresolvedTail = []
  while (round < 2) {
    round++
    const issueSets = await parallel(
      skeptics.map(s => () =>
        run(
          `${CONTEXT}\n\nYou are an adversarial reviewer of ${docPath} (${docKind}). Read it fully, plus every file it cites. YOUR LENS: ${s.lens}\n\nReport only defects you can substantiate. An empty issues list is a valid result — do not pad.`,
          { label: `challenge:${s.key}:r${round}`, phase: phaseTitle, schema: ISSUES_SCHEMA, model: s.model || AUTHOR },
        ).then(r => r.issues.map(i => ({ ...i, lens: s.key }))),
      ),
    )
    const issues = issueSets.filter(Boolean).flat()
    const serious = issues.filter(i => i.severity !== 'minor')
    log(`${docKind} challenge round ${round}: ${issues.length} issues (${serious.length} blocker/major)`)
    if (issues.length === 0) return { rounds: round, unresolved: [] }

    await run(
      `${CONTEXT}\n\nRevise ${docPath} to resolve the review issues below. Apply blockers and majors fully; apply minors where they improve the document; if you REJECT an issue, add a short "Review notes" entry at the bottom of the document stating why. Do not weaken requirements to dodge an issue — resolving by relaxing a threshold is forbidden unless the issue proves the threshold wrong against the roadmap. Edit the file in place.\n\nISSUES:\n${JSON.stringify(issues, null, 2)}`,
      { label: `revise:${docKind}:r${round}`, phase: phaseTitle },
    )

    const recheck = await run(
      `${CONTEXT}\n\nVerify that ${docPath} now addresses every blocker/major issue below (resolved in-text or explicitly rejected in "Review notes" with sound reasoning). Quote the document for each.\n\nISSUES:\n${JSON.stringify(serious, null, 2)}`,
      { label: `recheck:${docKind}:r${round}`, phase: phaseTitle, schema: RECHECK_SCHEMA, model: MECH },
    )
    if (!recheck || recheck.resolved) return { rounds: round, unresolved: [] }
    unresolvedTail = recheck.unresolved
    log(`${docKind} recheck: ${unresolvedTail.length} unresolved — ${round < 2 ? 'one more round' : 'giving up, surfacing to user'}`)
  }
  return { rounds: 2, unresolved: unresolvedTail }
}

// ===========================================================================
// STAGE: SPECIFY — specify → challenge spec → clarify scan.
// Ends by handing a questionnaire back to the main loop, which grills the
// user (grill-me skill) and re-enters at stage "plan" with the answers.
// ===========================================================================
if (stage === 'specify') {
  phase('Specify')
  log(`Authoring ${SPEC} from roadmap phase ${phaseNum}...`)
  const specStatus = await run(
    `${CONTEXT}\n\nWrite the feature specification for roadmap Phase ${phaseNum} to ${SPEC} (create ${DIR}/ if needed). Before writing: read specs/Seraphis-roadmap.md fully; read the ACTUAL headers of every existing component the roadmap's reuse-inventory row for this phase names (quote real class names/signatures); run the ODR sweep for every new class name the roadmap proposes and record the result in the spec.\n\nSPEC FORMAT (mirror the repo's speckit spec style):\n- Title, one-paragraph overview, explicit scope + non-goals (what later phases own).\n- Numbered functional requirements FR-001... — each one testable, each traceable to a roadmap statement. Cover the roadmap's component list completely; do not invent extra features.\n- Numbered success criteria SC-001... — each MEASURABLE with the metric, threshold, and how it will be measured (test name sketch). CPU budgets from the roadmap are SCs.\n- Edge cases section (RT-safety boundaries, parameter extremes, sample-rate changes, seed determinism).\n- 'Existing components' table: component → header path → what is reused (verified, with the real signature).\n- 'New components' table: class → layer → header path → ODR sweep result.\n- Open questions ONLY where the roadmap explicitly defers a decision to this spec.\n\nReturn the doc status.`,
    { label: 'specify', phase: 'Specify', schema: DOC_STATUS },
  )
  if (!specStatus) throw new Error('Specify agent failed — nothing to review')

  phase('Challenge Spec')
  const specReview = await challengeAndRevise(SPEC, 'spec', [
    { key: 'fidelity', lens: `ROADMAP FIDELITY: compare every FR/SC against specs/Seraphis-roadmap.md Phase ${phaseNum}. Flag roadmap requirements missing from the spec, spec content the roadmap never asked for (scope creep), and any silently weakened threshold (roadmap says X, spec says easier-than-X).` },
    { key: 'testability', lens: 'TESTABILITY: every FR must be verifiable by a concrete test; every SC must state metric + threshold + measurement method. Flag vague criteria ("sounds organic", "no artifacts") lacking an operational metric, untestable FRs, and SCs that would need bit-exact float goldens (forbidden — must use measured tolerances).' },
    { key: 'reality', model: MECH, lens: 'CODE REALITY: open every existing-component header the spec cites and verify the claimed API/behaviour is real. Re-run the ODR sweep for each new class name. Flag invented APIs, wrong paths, wrong layer assignments, and name collisions the spec missed.' },
  ], 'Challenge Spec')

  // Clarify scan — speckit.clarify equivalent. The workflow cannot interview
  // the user (agents are headless), so it produces the interview MATERIAL:
  // high-impact questions where different answers yield materially different
  // implementations. The main loop runs the actual grill.
  phase('Clarify Scan')
  const clar = await run(
    `${CONTEXT}\n\nYou are the CLARIFICATION scanner for ${SPEC} (speckit.clarify equivalent). Read the spec fully, the roadmap phase, and any code the spec cites. Find the places where the spec is UNDERSPECIFIED or AMBIGUOUS — where two reasonable engineers would build different things. Taxonomy to sweep: functional scope & behaviour contracts (exact semantics of each control/parameter), data & state (ranges, defaults, units, seeding, reset semantics), non-functional (budgets, tolerances, what "smooth"/"organic" means operationally), integration (how downstream phases will consume this — API shape commitments), edge cases (extremes, sample-rate change, retrigger/continuation semantics), terminology (words used but never defined).\n\nRULES:\n- Max 8 questions, ranked by impact. Only questions whose answer CHANGES the implementation — no bikeshedding, no questions the roadmap or repo conventions already answer.\n- Each question gets 2-4 concrete options with one-line consequences, plus your recommendation.\n- Also APPEND a "## Open Clarifications" section to ${SPEC} listing the same questions verbatim (id, question, options). Later stages gate on this section until answers are encoded.\n- If the spec is genuinely unambiguous, return zero questions and do not touch the file.`,
    { label: 'clarify-scan', phase: 'Clarify Scan', schema: QUESTIONS_SCHEMA },
  )
  const questions = clar ? clar.questions : []
  log(`Clarify scan: ${questions.length} question(s) for the user grill`)

  return {
    stage: 'specify',
    phase: phaseNum,
    slug: SLUG,
    artifacts: { spec: SPEC },
    spec_summary: specStatus.summary,
    open_questions: specStatus.open_questions || [],
    unresolved_review_issues: specReview.unresolved,
    clarification_questions: questions,
    next: questions.length
      ? `MAIN LOOP: grill the user (grill-me skill) through these ${questions.length} questions until every branch is resolved, then run stage "plan" with args {phase: ${phaseNum}, stage: "plan", clarifications: {Q1: "<answer>", ...}}.`
      : `No ambiguities found. Run stage "plan": args {phase: ${phaseNum}, stage: "plan"}.`,
  }
}

// ===========================================================================
// STAGE: PLAN — encode clarifications → plan → challenge plan → tasks
// ===========================================================================
if (stage === 'plan') {
  const clarifications = A.clarifications || null

  phase('Encode Clarifications')
  if (clarifications) {
    await run(
      `${CONTEXT_LITE}\n\nThe user was interviewed about the open clarifications in ${SPEC}. Encode their answers into the spec (speckit.clarify style):\n1. Add/extend a "## Clarifications" section with a dated session log: one line per question — the question and the user's decision.\n2. UPDATE every FR/SC/section the answer affects so the spec body itself reflects the decision (the reader must never need the log to know the behaviour).\n3. DELETE the "## Open Clarifications" section entirely — every question below is now decided.\n4. Do not weaken any threshold unless the answer explicitly decides that.\n\nANSWERS (id → decision):\n${JSON.stringify(clarifications, null, 2)}`,
      { label: 'encode-clarifications', phase: 'Encode Clarifications', model: MECH },
    )
  }
  // Gate: refuse to plan while the spec still carries unresolved questions.
  const gate = await run(
    `${CONTEXT_LITE}\n\nRead ${SPEC}. Check whether a "## Open Clarifications" section with unresolved questions still exists, and whether any FR/SC still contradicts the "## Clarifications" decisions log (if present). resolved=true only if the spec is clean and self-consistent.`,
    { label: 'clarify-gate', phase: 'Encode Clarifications', schema: RECHECK_SCHEMA, model: MECH, effort: 'low' },
  )
  if (gate && !gate.resolved) {
    return {
      stage: 'plan',
      phase: phaseNum,
      slug: SLUG,
      status: 'BLOCKED_UNRESOLVED_CLARIFICATIONS',
      unresolved: gate.unresolved,
      next: `Spec still has open questions. Grill the user on these, then re-run stage "plan" with args.clarifications covering them.`,
    }
  }

  phase('Plan')
  log(`Authoring ${PLAN}...`)
  const planStatus = await run(
    `${CONTEXT}\n\nWrite the technical implementation plan for ${SPEC} to ${PLAN}. Read the (now reviewed) spec first, then the real headers of everything it reuses.\n\nPLAN FORMAT:\n- Component-by-component design: header path, layer, public API sketch (real C++ signatures), how it composes the reused components (cite the actual reused signatures), state layout, prepare/reset/process contract, RT-safety notes.\n- Algorithm choices with brief justification (e.g. exact Ornstein-Uhlenbeck discretization, coupled-phase model equations) — enough that an implementer never has to guess math.\n- Test plan: for each FR/SC, the test file, TEST_CASE name, and assertion strategy (tolerances, seeds, measured thresholds; render_fingerprint.h where golden-ish checks are needed).\n- Build integration: which CMake test list files change, which test targets run.\n- Risks + mitigations (numerical stability, denormals, portability traps from the constraints list).\n\nReturn the doc status.`,
    { label: 'plan', phase: 'Plan', schema: DOC_STATUS },
  )
  if (!planStatus) throw new Error('Plan agent failed')

  phase('Challenge Plan')
  const planReview = await challengeAndRevise(PLAN, 'plan', [
    { key: 'spec-coverage', lens: `SPEC COVERAGE: map every FR/SC in ${SPEC} to a design element + test in the plan. Flag anything unaddressed, any test that could not actually detect its FR failing, and any plan element with no FR behind it.` },
    { key: 'rt-layers', lens: 'RT SAFETY + LAYER DISCIPLINE: audit the proposed designs for audio-thread allocations/locks/exceptions, unbounded work, denormal hazards, and layer violations (a Layer 2 component including Layer 3, wrong directory for the declared layer). Check the prepare/process split actually keeps allocation out of process.' },
    { key: 'reuse-reality', model: MECH, lens: 'REUSE REALITY: open every reused header the plan cites and verify each signature the plan quotes compiles as used (argument types, const-ness, concept conformance — especially the ModulationSource concept). Flag any API drift between plan and reality.' },
  ], 'Challenge Plan')

  phase('Tasks')
  log(`Authoring ${TASKS}...`)
  const tasksStatus = await run(
    `${CONTEXT}\n\nWrite the dependency-ordered task list for ${PLAN} to ${TASKS}. Read spec and plan first.\n\nTASK FORMAT:\n- Tasks T001... grouped into ordered GROUPS. Within a group, tasks marked [P] are parallel-safe: they touch fully disjoint NEW files only. Any task editing a SHARED file (CMake test lists, existing headers, a header another task also touches) goes in its own sequential group.\n- Each task is fully self-contained (executor has no other context): exact files to create/edit, the failing test to write FIRST (test file, TEST_CASE name, exact assertions with numbers), then the implementation intent, then which test target verifies it.\n- Follow the repo's canonical order: failing test → implement → zero warnings → tests pass.\n- Last group: integration tasks — CMake registration (single task), full-suite run, portability check (node tools/check-portability.js).\n- Do NOT include commit tasks — commits happen outside this workflow.\n\nReturn the doc status.`,
    { label: 'tasks', phase: 'Tasks', schema: DOC_STATUS },
  )

  return {
    stage: 'plan',
    phase: phaseNum,
    slug: SLUG,
    artifacts: { spec: SPEC, plan: PLAN, tasks: TASKS },
    clarifications_encoded: clarifications ? Object.keys(clarifications).length : 0,
    plan_summary: planStatus.summary,
    tasks_summary: tasksStatus ? tasksStatus.summary : 'TASKS AGENT FAILED — rerun needed',
    open_questions: [...(planStatus.open_questions || []), ...((tasksStatus && tasksStatus.open_questions) || [])],
    unresolved_review_issues: { plan: planReview.unresolved },
    next: `Review ${DIR}/ (spec now includes the clarification decisions), then run stage "build": args {phase: ${phaseNum}, stage: "build"}`,
  }
}

// ===========================================================================
// STAGE: BUILD — dispatch → implement → build/test → comply → report
// ===========================================================================
phase('Dispatch')
const dispatch = await run(
  `${CONTEXT_LITE}\n\nRead ${SPEC}, ${PLAN}, and ${TASKS}. Convert tasks.md into executable task groups, preserving its group order and [P] parallel markers. Copy each task's instructions VERBATIM-or-expanded (executors see nothing else). Mark a group parallel=true ONLY if every task in it touches disjoint new files. Also list the cmake test targets this phase runs.`,
  { label: 'dispatch', phase: 'Dispatch', schema: TASKGROUPS_SCHEMA, model: MECH },
)
if (!dispatch || !dispatch.groups || dispatch.groups.length === 0) throw new Error(`Dispatcher produced no task groups — check ${TASKS} exists (run stages "specify" then "plan" first)`)
log(`${dispatch.groups.length} groups, ${dispatch.groups.reduce((n, g) => n + g.tasks.length, 0)} tasks, targets: ${dispatch.test_targets.join(', ')}`)

const BUILD_CMDS = (targets) => `
Build: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ${targets.join(' ')}
Then run each: build/windows-x64-release/bin/Release/<target>.exe 2>&1 | tail -5
Capture output to a file on the FIRST run if long; never re-run a suite just to re-read output.`

const implResults = []
let buildLog = null

for (const group of dispatch.groups) {
  phase('Implement')
  log(`Group "${group.name}": ${group.tasks.length} task(s), ${group.parallel ? 'parallel' : 'sequential'}`)

  const runTask = (t) => () =>
    run(
      `${CONTEXT}\n\nExecute this implementation task EXACTLY. Artifacts for reference: ${SPEC}, ${PLAN}. Write the failing test FIRST, then implement. Do NOT build or run tests (a dedicated build agent does that after your group) — but re-read every file you wrote before finishing and fix anything that obviously would not compile. Do not touch any file outside your task's file list. Never commit.\n\nTASK ${t.id}: ${t.title}\nFILES: ${t.files.join(', ')}\n\n${t.instructions}`,
      { label: `impl:${t.id}`, phase: 'Implement', schema: IMPL_RESULT },
    )

  if (group.parallel) {
    const rs = await parallel(group.tasks.map(runTask))
    implResults.push(...rs.filter(Boolean))
  } else {
    for (const t of group.tasks) {
      const r = await runTask(t)()
      if (r) implResults.push(r)
    }
  }

  // Build + fix loop after every group so errors localize to the group that caused them.
  phase('Build+Test')
  let attempt = 0
  while (attempt < 4) {
    attempt++
    buildLog = await run(
      `${CONTEXT_LITE}\n\nBuild and test the current tree.${BUILD_CMDS(dispatch.test_targets)}\nReport verbatim results. Fix NOTHING — you are a reporter.\n(retry-epoch 3: the tree may have been repaired outside the workflow since the last attempt — measure fresh, do not assume prior failures still hold.)`,
      { label: `build:${group.name}:a${attempt}`, phase: 'Build+Test', schema: BUILD_RESULT, model: MECH, effort: 'low' },
    )
    if (!buildLog) throw new Error('Build agent died — aborting to avoid blind fixes')
    if (buildLog.build_ok && buildLog.tests_ok) { log(`Group "${group.name}" green: ${buildLog.summary}`); break }
    if (attempt >= 4) break
    log(`Group "${group.name}" red (attempt ${attempt}) — dispatching fixer`)
    await run(
      `${CONTEXT}\n\nThe build/tests are failing after implementing group "${group.name}" of ${TASKS}. Diagnose and fix the ROOT CAUSE. Rules: never weaken or delete a failing test to make it pass unless it demonstrably contradicts ${SPEC} (then say so loudly in your notes); no warnings allowed; fix all failures, not just the first. You MAY build/run tests yourself while iterating.${BUILD_CMDS(dispatch.test_targets)}\n\nFAILURES:\n${buildLog.failures}\n\nTASK NOTES SO FAR:\n${JSON.stringify(implResults, null, 2)}`,
      { label: `fix:${group.name}:a${attempt}`, phase: 'Build+Test' },
    )
  }
  if (!buildLog.build_ok || !buildLog.tests_ok) {
    // Hard stop: comply on a red tree is meaningless.
    return {
      stage: 'build', phase: phaseNum, slug: SLUG, status: 'FAILED_RED_TREE',
      failed_group: group.name,
      last_build: buildLog,
      impl_results: implResults,
      next: 'Tree is red after 4 fix attempts. Inspect failures, fix in main loop, then re-run stage "build" (it will re-verify from the dispatcher).',
    }
  }
}

// ---------------------------------------------------------------------------
// Comply — adversarial verification of every FR/SC against the real tree.
// ---------------------------------------------------------------------------
phase('Comply')
const complyLenses = [
  { key: 'fr', what: `every FR-xxx in ${SPEC}: open the implementation, read the code, confirm the requirement is met. Evidence = file:line + a one-sentence proof from the code you quote.` },
  { key: 'sc', what: `every SC-xxx in ${SPEC}: RUN the specific test or measurement yourself (${dispatch.test_targets.map(t => `build/windows-x64-release/bin/Release/${t}.exe "<TestName>*"`).join(' or ')}) and paste the ACTUAL numbers vs the threshold. Never paraphrase output.` },
  { key: 'constraints', what: `the cross-cutting constraints: RT safety of every new process path (read for allocations/locks/exceptions), layer discipline of every new header (check its #includes), naming conventions, zero-warning build, and run node tools/check-portability.js reporting its verbatim result. Report each as its own item (id: CC-rt, CC-layers, CC-naming, CC-warnings, CC-portability).` },
]
const complyResults = await parallel(
  complyLenses.map(l => () =>
    run(
      `${CONTEXT}\n\nYou are the COMPLIANCE verifier. Default verdict is FAIL until you personally verify otherwise — assume the implementers cut corners. Verify ${l.what}\n\nAn item without concrete evidence (real file:line, real measured numbers) must be verdict "fail" with evidence explaining what is missing.`,
      { label: `comply:${l.key}`, phase: 'Comply', schema: COMPLY_SCHEMA },
    ).then(r => r.items.map(i => ({ ...i, lens: l.key }))),
  ),
)
const complyItems = complyResults.filter(Boolean).flat()
const failures = complyItems.filter(i => i.verdict !== 'pass')
log(`Compliance: ${complyItems.length - failures.length}/${complyItems.length} pass, ${failures.length} fail/partial`)

// One bounded remediation round for compliance failures, then re-verify just those.
let finalItems = complyItems
if (failures.length > 0) {
  log('Dispatching remediation for failing compliance items...')
  await run(
    `${CONTEXT}\n\nFix these compliance failures against ${SPEC}. Same rules as always: failing test first where a behaviour is wrong, no threshold relaxing, no test deletion, zero warnings, tree must stay green.${BUILD_CMDS(dispatch.test_targets)}\n\nFAILING ITEMS:\n${JSON.stringify(failures, null, 2)}`,
    { label: 'remediate', phase: 'Comply' },
  )
  const reverify = await run(
    `${CONTEXT}\n\nRe-verify ONLY these previously-failing compliance items with the same evidence standard (real file:line, real test output run by you). Also confirm the full suites are still green (run them, quote the summary lines).\n\nITEMS:\n${JSON.stringify(failures, null, 2)}`,
    { label: 'reverify', phase: 'Comply', schema: COMPLY_SCHEMA },
  )
  if (reverify) {
    const reMap = new Map(reverify.items.map(i => [i.id, i]))
    finalItems = complyItems.map(i => reMap.has(i.id) ? { ...reMap.get(i.id), lens: i.lens } : i)
  }
}
const finalFailures = finalItems.filter(i => i.verdict !== 'pass')

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
phase('Report')
const report = await run(
  `${CONTEXT_LITE}\n\nWrite the compliance report to ${COMPLIANCE} and return the same markdown. Content:\n- Header: phase ${phaseNum} (${SLUG}), overall status (COMPLETE only if zero fail/partial items — otherwise INCOMPLETE with the honest gap list first).\n- Compliance table: one row per item below, with its verbatim evidence. Do not soften any fail.\n- Implementation notes: deviations reported by task agents.\n- Remaining gates for the human loop: clang-tidy, commit; pluginval only if plugin code changed (phases 8+).\n\nITEMS:\n${JSON.stringify(finalItems, null, 2)}\n\nTASK NOTES:\n${JSON.stringify(implResults, null, 2)}`,
  { label: 'report', phase: 'Report', schema: DOC_SCHEMA, model: MECH },
)

// Mark the roadmap phase as finished — ONLY on a fully green compliance table.
// An INCOMPLETE phase never touches the roadmap.
if (finalFailures.length === 0) {
  await run(
    `You are in the Krate Audio monorepo at f:/projects/iterum. The implementation of Seraphis roadmap Phase ${phaseNum} is verified complete. Update specs/Seraphis-roadmap.md to mark it finished:\n1. In the "### Phase ${phaseNum}: ..." heading's section, insert a status line directly under the heading: "**Status: ✅ COMPLETE (<today's date, YYYY-MM-DD — get it by running the date command>)** — see ${COMPLIANCE}".\n2. In the Dependency Graph section, if practical, annotate this phase as done (a ✅ next to its node label) WITHOUT breaking the ASCII diagram alignment — if you cannot keep the diagram intact, skip the diagram and only do step 1.\n3. Touch NOTHING else in the roadmap: no rewording, no reformatting, no updates to other phases.`,
    { label: 'mark-roadmap', phase: 'Report', model: TRIVIAL },
  )
  log(`Roadmap phase ${phaseNum} marked COMPLETE`)
} else {
  log(`Phase INCOMPLETE — roadmap NOT updated (${finalFailures.length} failing item(s))`)
}

return {
  stage: 'build',
  phase: phaseNum,
  slug: SLUG,
  status: finalFailures.length === 0 ? 'COMPLETE' : 'INCOMPLETE',
  roadmap_updated: finalFailures.length === 0,
  compliance: { pass: finalItems.length - finalFailures.length, fail_or_partial: finalFailures.length, failures: finalFailures },
  last_build: buildLog,
  report_path: COMPLIANCE,
  report_markdown: report ? report.markdown : null,
  next: finalFailures.length === 0
    ? 'Review diff + compliance.md, then main loop runs clang-tidy and commits.'
    : 'INCOMPLETE — read failures above; fix in main loop or re-run stage "build" after addressing.',
}
