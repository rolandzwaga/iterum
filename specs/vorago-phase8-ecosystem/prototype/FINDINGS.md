# Vorago Phase 8 — Ecosystem prototype findings

**Status: preliminary. Not yet sufficient to write the spec from.** Boundedness is settled (§1);
the rule set and its success metrics are not (§5). Read the Open Problems
section before treating any number here as settled.

The roadmap mandates this prototype before the Phase 8 spec: *"an offline Node.js prototype
simulating the agent graph + rule set... The rule set is tuned there — where iteration is seconds,
not audio-thread builds. The spec then encodes the proven rules."* This file records what the
simulation actually showed, including the parts that contradict the roadmap's design sketch.

Run it: `node run.js trace|fuzz|ablate|sweep|seeds|determinism` from this directory.
The RNG is a bit-exact port of `dsp/include/krate/dsp/core/random.h` (`Xorshift32` +
`deriveStreamSeed`), so seeds and stream-splitting transfer to the C++ component unchanged.

---

## 1. Boundedness — and the two guards it actually requires

**Final state: 1000 randomised configurations, 0 non-finite, 0 pool-negative, 0 unbounded.**
Energy lives in a closed system (reservoir pool + resource cells + agents) and total energy is
invariant. Measured drift over a 30-minute run: **2.4e-15 to 1.9e-13 relative** — float noise.

**But this was NOT true of the first design, and the correction is the finding.** An earlier
version of this document claimed boundedness was structural "by construction". Fuzzing disproved
it twice. Closure requires two guards that are not obvious, and both belong in the C++ spec as
requirements rather than as implementation detail:

**Guard 1 — one withdrawal budget per step.** Cell regrowth and the global feed each capped their
draw against a pool that did not yet know about the other, so together they could overdraw.
**591/1000 configs** drove the pool negative this way. Both draws must decrement a single running
balance.

**Guard 2 — a transfer is limited by what the source holds, enforced in two passes.**
This is the subtle one. The exchange rule was *antisymmetric* — every transfer added to one agent
exactly what it removed from another, so the pairwise sum was provably zero. That looks like
conservation is structural, and it is why the original claim was made. It is not sufficient: an
agent can be asked to give away more than it has, and clamping it at zero charges the shortfall to
the pool — an uncapped withdrawal. After Guard 1, **468/1000 configs** still failed this way.

The fix is to defer the flows, accumulate each agent's desired outflow, compute
`scale_i = min(1, e_i / desired_i)`, then apply each pair scaled by `min(scale_i, scale_j)`.
Scaling *both sides of a pair by the same factor* is what preserves antisymmetry while making
solvency structural; a solvent agent is never throttled. This also raised the alive fraction from
6.4% to **14.6%**, since agents that stay solvent stay lively.

**The lesson worth carrying:** antisymmetry gives conservation only among participants who can
actually pay. In C++ this class of defect would have surfaced as a slow, configuration-dependent
energy leak, months later, under an audio-thread debugger.

**Determinism under seed** holds — same seed reproduces a bit-identical trajectory.

---

## 2. What broke (the roadmap's sketch does not work as written)

### 2.1 A single global energy pool makes every agent a copy of every other

The first draft had agents feeding from one shared scalar pool, as the roadmap implies. Result:
mean pairwise correlation between agent outputs of **0.84–0.99**. Thirty-two agents producing one
modulation shape thirty-two times — useless as the "bank of `ModulationSource`s" Phase 8 is
supposed to deliver.

Fix that worked: make the resource **spatial**. A field of resource cells around the habitat, each
regrowing independently, grazed locally. Agents deplete their own neighbourhood, compete with
whoever is nearby, and must move. Correlation fell to **0.44–0.46**, which is at this system's
noise floor (see §3.1).

### 2.2 Pure diffusion homogenises into a dead flat soup

The roadmap names "exchange energy" as a core rule. Taken literally — energy flowing from richer
to poorer neighbours — it is an averaging operator, and averaging destroys structure. Measured
entropy **4.998 out of a 5.000 maximum**: all 32 agents holding near-identical energy, nothing
happening. Adjacent seeds correlated at **0.9989**.

### 2.3 Neighbourhood width is the most consequential parameter, and it is not close

At `kernelSigma = 0.12` (each agent's patch spanning an eighth of the habitat) every neighbourhood
overlapped every other: activity 0.06, correlation 0.84. At **0.03**: activity **0.32**,
correlation **0.44**, zero frozen agents. Same rules, same everything else. Locality is what
creates individuality.

### 2.4 Deficit-proportional regrowth silently erases the spatial structure it is meant to create

The first spatial version shared one regeneration budget across cells in proportion to how empty
each was. That pulls every cell toward the same level, so the field comes out uniform and the
spatial model buys nothing. The tell was unmistakable in the sweep: `resourceCells`, `grazeRate`
and `maxSpeed` produced **byte-identical rows** across their whole tested range — three parameters
doing measurably nothing.

Fix: each cell regrows logistically and independently. A grazed patch stays depleted while an
ungrazed one fills. After the change those parameters visibly matter.

### 2.5 Predation above the neutral point is unbounded, and below it does nothing

`predation` blends the exchange rule between diffusive (strong feeds weak) and predatory (weak
feeds strong).

Measured **before** Guard 2 (§1) existed:

| predation | activity | frozen agents | bounded (pre-Guard-2) |
|---|---|---|---|
| 0.0 | 0.222 | 0 | yes |
| 0.25 | 0.222 | 0 | yes |
| 0.5 | 0.215 | 0 | yes |
| 0.75 | 0.081 | 22 | **no** |
| 1.0 | 0.057 | 23 | **no** |

The unboundedness in the last two rows is **fixed** — it was the uncapped-overdraw defect Guard 2
closes, and predation 0.75–1.0 at `exchangeRate` 3.0 now holds conservation to ~5e-14. Do not read
this table as "predation is unsafe"; read it as how the defect was first observed.

What survives the fix is the other half: **the safe settings are indistinguishable from each
other** (activity 0.215–0.222 across predation 0–0.5). The agent-to-agent exchange rule does not
earn its place on the evidence so far — spatial grazing competition already does that job. Whether
predation becomes useful now that it is solvent is untested and belongs with open problem 1.

This was caught by an ablation run in which *"no exchange" came out bit-identical to baseline, to
every printed digit* — because the default `predation: 0.5` makes the `(1 - 2·predation)` factor
exactly zero. The rule had been silently disabled in the very configuration being validated.

---

## 3. The roadmap's proposed success metrics do not measure what they claim

This matters more than any parameter value, because these metrics would become the Phase 8 spec's
success criteria and would then be enforced against a C++ implementation.

### 3.1 An absolute correlation threshold is meaningless here

These signals are slow — autocorrelation decays over ~150 s — so a 900 s window contains only a
handful of *independent* samples, and `|correlation|` between genuinely unrelated slow signals runs
high by chance. Measured noise floor, taken between **different agents in the same run** (as
unrelated as two signals in this system get):

| window | noise floor | cross-seed correlation |
|---|---|---|
| 900 s | 0.606 | 0.570 |
| 3600 s | 0.501 | 0.458 |

Cross-seed correlation is *below* the floor at both durations: two seeds produce outputs as
different as two different agents within one voice. Judged against a fixed 0.5 threshold, this
same configuration was declared "SEED-BLIND — voices would sound alike". **Any decorrelation
criterion must be stated relative to the measured within-run floor, never as an absolute number.**

### 3.2 Entropy of the energy distribution is permutation-invariant

The roadmap proposes "energy distribution entropy keeps changing over 30 min" as the
non-triviality metric. Shannon entropy cannot see *which* agent holds the energy. Energy sloshing
between agents in a fixed pattern holds entropy exactly constant while every agent's output swings
— the metric calls the liveliest case frozen. Conversely, agents drifting in lockstep keep entropy
moving while producing one signal copied N times.

Entropy also barely discriminated in practice: it sat near 4.9–5.0 across every regime tested in
the first two rounds, including regimes that differed by 5× in per-agent activity.

Proposed replacements, both of which track what a DSP consumer actually receives:
- **per-agent activity** — mean over agents of `stddev(eᵢ)/grand mean`; and
- **inter-agent decorrelation** — mean `|corr(eᵢ, eⱼ)|`, judged against the §3.1 noise floor.

Keep entropy as a secondary descriptive statistic; do not gate on it.

### 3.3 "No limit cycle" needs a recurrence test, not a maximum

The first detector reported the maximum autocorrelation over all lags ≥ 10 s. For any smooth
signal that maximum is always at the shortest lag scanned, so it reported *"limit cycle: 0.909 at
lag 9.0 s"* for a 30-minute run whose scan window merely started at 9 s. It was measuring
smoothness.

A cycle is a **recurrence**: the autocorrelation must first decay (the signal forgets itself), and
only a peak *after* that decay is evidence of repetition. Rewritten that way, the same run reports
0.198–0.200 at ~130–170 s — no short cycle.

---

## 4. Current best configuration

Promoted to `defaultConfig()` in `ecosystem-sim.js`. 32 agents, 1D toroidal habitat, 64 resource
cells, `kernelSigma` 0.03, `regenRate` 0.05, `grazeRate` 1.5, `moveRate` 0.2, `maxSpeed` 0.03,
`leakRate` 0.06, `appetiteDepth` 0.8, phase periods 56–667 s with bounded OU drift.

30-minute run: energy drift 1.9e-13 (bounded); per-agent activity 0.32; zero frozen agents;
inter-agent correlation 0.44; no short limit cycle (0.198 at 138 s); cross-seed correlation at or
below the noise floor.

---

## 5. Open problems — why this is not yet spec-ready

1. **Three of the roadmap's four named rules show no measurable contribution.** Ablation at 1200 s
   found only the phase-gated local grazing to be load-bearing: removing the appetite gate freezes
   the system (late-window std → 0.0000), but removing *exchange*, *movement* or *synchronize* left
   it "alive" by the current criteria. If that survives scrutiny, Phase 8 is a much simpler
   component than the roadmap imagines — and the "agents attract/repel/synchronize" framing would
   be largely decorative. This needs settling before the spec, because it decides what the
   component *is*.

2. **The liveness criterion is duration-unstable.** The baseline scored late-window std 0.0141 at
   1200 s ("frozen") and 0.0276 at 1800 s ("still moving") — the verdict flips with run length.
   A threshold that depends on how long you looked is not a success criterion. Needs either a
   duration-normalised statistic or a fixed, justified measurement window.

3. **Per-agent activity of 0.32 has no musical calibration.** Nothing yet says what magnitude of
   modulation swing is *interesting* versus merely non-zero. That judgement needs a consumer —
   which is the argument for building Phase 2 (Noise Organism) before Phase 8 proper, so agent
   outputs can drive something audible.

4. **Movement is suspiciously inert.** Agents should need to move to find food; that "no movement"
   remains alive suggests either the grazing kernel is wide enough to reach food without moving, or
   the movement forces are too weak to matter at these rates. Unresolved.

5. **1D vs 2D habitat undecided.** 2D scored alive with markedly lower entropy (2.93 vs 4.07) and
   higher variability. Not enough evidence to choose.

6. **The alive regime is a narrow island.** With every knob randomised, only **14.6%** of 1000
   configurations stay lively; 85.4% freeze. Boundedness is universal, liveness is rare. That is a
   real risk for Phase 10: concept macros (Darkness, Life, Entropy) will move these parameters, and
   most of the space is dead. Either the macros must be constrained to the live region, or the rule
   set needs a mechanism that resists freezing. Unresolved, and it interacts with open problem 2 —
   the frozen count is measured with the duration-unstable criterion, so treat 85.4% as indicative
   rather than exact.

7. **The fuzzer only proves things about parameters it varies.** Its first version silently omitted
   `predation`, `capacity`, `leakExponent` and the entire resource field, leaving predation at the
   0.5 default — the one value that zeroes the exchange rule. It reported a clean
   "0 unbounded / 1000" while never visiting a regime already known to break conservation. The
   C++ fuzz test must assert that every configuration field is actually randomised, or it will rot
   into the same false green.

## 6. Recommendation

Do **not** write the Phase 8 spec from this yet. Items 1 and 2 change what the component is and how
it would be judged. The cheap next step is Phase 2 (Noise Organism), which consumes the shipped
Phase 1 components, produces audible material, and gives the ecosystem something real to drive —
at which point item 3 becomes answerable and items 1 and 4 can be judged by ear rather than by a
statistic whose thresholds keep proving unstable.
