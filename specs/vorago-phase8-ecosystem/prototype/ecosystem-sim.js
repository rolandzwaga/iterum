// =============================================================================
// Vorago Phase 8 — Ecosystem Engine offline prototype (simulation core)
// =============================================================================
// The roadmap mandates this prototype BEFORE the spec: "an offline Node.js
// prototype simulating the agent graph + rule set, rendering behaviour traces
// to CSV/plots. The rule set is tuned there — where iteration is seconds, not
// audio-thread builds. The spec then encodes the proven rules."
//
// What this must answer (roadmap Open Question 2 + Phase 8 success criteria):
//   1. Which agent kinds and interaction rules survive?
//   2. Is the system BOUNDED under random rule fuzzing?
//   3. Is it NON-TRIVIAL — energy-distribution entropy keeps changing over
//      30 min, with no frozen fixed point and no short limit cycle?
//
// Design commitment under test: boundedness is STRUCTURAL, not tuned. Energy
// lives in a closed pool; agents draw from it and leak back. Total system
// energy is therefore invariant by construction, so no rule configuration —
// however hostile — can make the system run away. Fuzzing tests that claim.
//
// The RNG mirrors dsp/include/krate/dsp/core/random.h exactly (Xorshift32 +
// deriveStreamSeed lowbias32 finaliser) so that seeds and stream-splitting
// behaviour transfer to the C++ component unchanged.
// =============================================================================

'use strict';

// -----------------------------------------------------------------------------
// RNG — bit-exact port of core/random.h
// -----------------------------------------------------------------------------
const kDefaultSeed = 2463534242;
const kToFloat = 2.3283064370807974e-10;

class Xorshift32 {
  constructor(seedValue = 1) {
    this.state = (seedValue >>> 0) !== 0 ? seedValue >>> 0 : kDefaultSeed;
  }
  next() {
    let s = this.state;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.state = s;
    return s;
  }
  nextUnipolar() { return this.next() * kToFloat; }
  nextBipolar() { return this.next() * kToFloat * 2.0 - 1.0; }
  range(lo, hi) { return lo + (hi - lo) * this.nextUnipolar(); }
}

function deriveStreamSeed(base, salt) {
  let h = ((base >>> 0) ^ (Math.imul((salt + 1) >>> 0, 0x9e3779b9) >>> 0)) >>> 0;
  h = (h ^ (h >>> 16)) >>> 0;
  h = Math.imul(h, 0x7feb352d) >>> 0;
  h = (h ^ (h >>> 15)) >>> 0;
  h = Math.imul(h, 0x846ca68b) >>> 0;
  h = (h ^ (h >>> 16)) >>> 0;
  return h !== 0 ? h : 0x2545f491;
}

// -----------------------------------------------------------------------------
// Agent kinds — the roadmap's roster (line 305)
// -----------------------------------------------------------------------------
const KINDS = ['partial', 'resonator', 'noise', 'feedback', 'ghost'];
const KIND_COUNT = KINDS.length;

// -----------------------------------------------------------------------------
// Default configuration. Every field is a knob the fuzzer may randomise.
// Rates are per SECOND; the integrator multiplies by dt.
// -----------------------------------------------------------------------------
function defaultConfig() {
  return {
    agentCount: 32,              // roadmap: ~24-48 per voice
    dimensions: 1,               // 1D toroidal habitat (2 = 2D torus)
    energyBudget: 1.0,           // total conserved energy (pool + agents)
    initialPoolFraction: 0.5,

    // Neighbourhood width. This is the single most consequential knob found:
    // at 0.12 every agent's patch overlapped everyone else's, giving mean
    // pairwise correlation 0.84 and per-agent activity 0.06 (one shared
    // signal). Tightening it to 0.03 gives activity 0.32 / correlation 0.44.
    kernelSigma: 0.03,
    exchangeRate: 0.35,          // energy transfer rate between neighbours
    // Exchange character. 0 = pure diffusion (strong feeds weak: homogenises
    // into a dead uniform soup — measured entropy 4.998/5.000 and adjacent
    // seeds correlating at 0.999). 1 = pure predation (weak feeds strong).
    // >0.5 is the regime where structure survives; the carrying capacity below
    // is what stops predation collapsing onto a single permanent winner.
    predation: 0.5,
    capacity: 1.0,               // per-agent energy ceiling; excess spills to pool
    leakExponent: 1.0,           // leak = leakRate * e^exp; >1 punishes hoarding
    moveRate: 0.200,             // habitat movement speed
    maxSpeed: 0.030,             // slew limit on movement (habitat units/s)
    syncRate: 0.030,             // Kuramoto phase coupling
    // Resource field. A single global pool was the first draft and it failed:
    // every agent feeding from one scalar shares a common-mode signal, so all
    // 32 agents rose and fell together (measured mean pairwise correlation
    // 0.84-0.99 — one modulation shape copied N times, useless as a bank of
    // independent sources). Spreading the resource across the habitat makes
    // feeding LOCAL: agents deplete their neighbourhood, must move to find
    // more, and neighbouring agents compete. That is what decorrelates them.
    resourceCells: 64,
    cellCapacity: 0.05,          // max energy one cell can hold
    regenRate: 0.05,             // pool -> cells
    grazeRate: 1.5,              // cells -> agents
    feedRate: 0.0,              // (legacy global feed; 0 disables)
    leakRate: 0.06,              // agents -> pool
    freqLo: 0.0015,              // intrinsic phase freq (Hz): 1/0.0015 = 667 s
    freqHi: 0.0180,              // 1/0.018 = 56 s
    freqDrift: 0.00004,          // bounded OU drift on intrinsic freq
    appetiteDepth: 0.8,          // how strongly phase gates feeding [0..1]

    // Kind-pair interaction matrix, A[i][j] > 0 attract, < 0 repel.
    // Null = build the default structured matrix (same kind repels, others attract).
    affinity: null,
  };
}

function defaultAffinity() {
  const A = [];
  for (let i = 0; i < KIND_COUNT; i++) {
    A.push([]);
    for (let j = 0; j < KIND_COUNT; j++) {
      // Same kind repels (spreads a species across the habitat); different
      // kinds attract weakly (mixed neighbourhoods exchange energy).
      A[i].push(i === j ? -1.0 : 0.45);
    }
  }
  return A;
}

// -----------------------------------------------------------------------------
// Simulation
// -----------------------------------------------------------------------------
class Ecosystem {
  constructor(config, seed) {
    this.cfg = Object.assign(defaultConfig(), config || {});
    if (!this.cfg.affinity) this.cfg.affinity = defaultAffinity();
    this.seed = seed >>> 0;
    this.reset();
  }

  reset() {
    const c = this.cfg;
    const n = c.agentCount;
    // Separate streams per attribute so changing one does not reshuffle others.
    const rPos = new Xorshift32(deriveStreamSeed(this.seed, 0));
    const rEnergy = new Xorshift32(deriveStreamSeed(this.seed, 1));
    const rFreq = new Xorshift32(deriveStreamSeed(this.seed, 2));
    const rPhase = new Xorshift32(deriveStreamSeed(this.seed, 3));
    const rKind = new Xorshift32(deriveStreamSeed(this.seed, 4));
    this.rDrift = new Xorshift32(deriveStreamSeed(this.seed, 5));

    this.kind = new Int32Array(n);
    this.x = new Float64Array(n);
    this.y = new Float64Array(n);
    this.e = new Float64Array(n);
    this.phase = new Float64Array(n);
    this.freq = new Float64Array(n);
    this.freq0 = new Float64Array(n);

    let eSum = 0;
    for (let i = 0; i < n; i++) {
      this.kind[i] = Math.min(KIND_COUNT - 1, Math.floor(rKind.nextUnipolar() * KIND_COUNT));
      this.x[i] = rPos.nextUnipolar();
      this.y[i] = c.dimensions === 2 ? rPos.nextUnipolar() : 0;
      this.e[i] = rEnergy.nextUnipolar();
      eSum += this.e[i];
      this.phase[i] = rPhase.nextUnipolar();
      this.freq[i] = rFreq.range(c.freqLo, c.freqHi);
      this.freq0[i] = this.freq[i];
    }
    // Normalise agent energy to (1 - initialPoolFraction) of the budget.
    const target = c.energyBudget * (1 - c.initialPoolFraction);
    const scale = eSum > 0 ? target / eSum : 0;
    for (let i = 0; i < n; i++) this.e[i] *= scale;

    // Resource cells start with a SEEDED random fill, not empty. Starting them
    // all at zero gave every seed an identical opening transient (empty field
    // refilling at the same rate from the same pool split), and that shared
    // transient dominated the trajectory: per-agent correlation between seeds
    // measured 0.60 — voices would have breathed in unison. Randomising the
    // initial field is what makes each voice's history its own from t=0.
    const rRes = new Xorshift32(deriveStreamSeed(this.seed, 6));
    this.res = new Float64Array(c.resourceCells);
    this.cellPos = new Float64Array(c.resourceCells);
    let resSum = 0;
    for (let k = 0; k < c.resourceCells; k++) {
      this.cellPos[k] = (k + 0.5) / c.resourceCells;
      this.res[k] = rRes.nextUnipolar() * c.cellCapacity;
      resSum += this.res[k];
    }
    // Fit the field inside whatever budget is left, keeping the total exact.
    const remaining = c.energyBudget - target;
    const resTarget = Math.min(resSum, remaining * 0.5);
    const rScale = resSum > 0 ? resTarget / resSum : 0;
    for (let k = 0; k < c.resourceCells; k++) this.res[k] *= rScale;
    this.pool = remaining - resTarget;

    this.time = 0;
    this.pairOps = 0;
    this.poolWentNegative = false;
  }

  // Toroidal separation on one axis, result in [-0.5, +0.5].
  static wrapDelta(d) {
    if (d > 0.5) return d - 1.0;
    if (d < -0.5) return d + 1.0;
    return d;
  }

  step(dt) {
    const c = this.cfg;
    const n = c.agentCount;
    const twoSigmaSq = 2 * c.kernelSigma * c.kernelSigma;
    const A = c.affinity;
    const twoPi = Math.PI * 2;

    const dE = new Float64Array(n);
    const fx = new Float64Array(n);
    const fy = new Float64Array(n);
    const dPhase = new Float64Array(n);

    // --- sense neighbours: one pass over unordered pairs -------------------
    for (let i = 0; i < n; i++) {
      for (let j = i + 1; j < n; j++) {
        let dx = Ecosystem.wrapDelta(this.x[j] - this.x[i]);
        let dy = c.dimensions === 2 ? Ecosystem.wrapDelta(this.y[j] - this.y[i]) : 0;
        const d2 = dx * dx + dy * dy;
        const w = Math.exp(-d2 / twoSigmaSq);
        if (w < 1e-6) continue;
        this.pairOps++;

        // RULE 1 — EXCHANGE. Antisymmetric by construction, so the pairwise
        // flows sum to exactly zero: agent-to-agent transfer cannot create or
        // destroy energy regardless of exchangeRate or predation.
        // (1 - 2*predation) flips the sign: +1 diffusive, -1 predatory.
        const flow = c.exchangeRate * w * (this.e[j] - this.e[i]) * (1 - 2 * c.predation);
        dE[i] += flow;
        dE[j] -= flow;

        // RULE 2 — ATTRACT / REPEL. Force is scaled by the NEIGHBOUR's energy,
        // so a spent agent stops pulling and the topology tracks where the
        // energy actually is.
        const a = A[this.kind[i]][this.kind[j]];
        const dist = Math.sqrt(d2) + 1e-9;
        const ux = dx / dist;
        const uy = dy / dist;
        fx[i] += a * w * this.e[j] * ux;
        fy[i] += a * w * this.e[j] * uy;
        fx[j] -= a * w * this.e[i] * ux;
        fy[j] -= a * w * this.e[i] * uy;

        // RULE 3 — SYNCHRONIZE. Kuramoto coupling, weighted by proximity.
        const pd = twoPi * (this.phase[j] - this.phase[i]);
        const s = Math.sin(pd);
        dPhase[i] += c.syncRate * w * s;
        dPhase[j] -= c.syncRate * w * s;
      }
    }

    // --- RULE 4 — METABOLISM. Closed pool: this is what makes boundedness
    // structural rather than tuned. Appetite is gated by phase, so the sync
    // rule feeds back into the energy economy instead of being decorative.
    let appetiteSum = 0;
    const appetite = new Float64Array(n);
    for (let i = 0; i < n; i++) {
      const gate = 1 + c.appetiteDepth * Math.sin(twoPi * this.phase[i]);
      appetite[i] = gate > 0 ? gate : 0;
      appetiteSum += appetite[i];
    }

    // --- resource regeneration: pool -> cells, capped by cellCapacity.
    let poolDelta = 0;
    // Per-cell LOGISTIC regrowth, each cell independent of the others. The
    // first draft shared one global regen budget deficit-proportionally, which
    // pulled every cell toward the same level: the resource field came out
    // uniform, so grazing pressure was identical everywhere and resourceCells /
    // grazeRate / maxSpeed measurably did nothing (identical sweep rows).
    // Independent regrowth is what lets a grazed patch STAY depleted while an
    // ungrazed one fills — the spatial structure agents can then exploit.
    if (this.pool > 0) {
      for (let k = 0; k < c.resourceCells; k++) {
        const room = c.cellCapacity - this.res[k];
        if (room <= 0) continue;
        const give = Math.min(c.regenRate * room * dt, this.pool + poolDelta);
        if (give <= 0) continue;
        this.res[k] += give;
        poolDelta -= give;
      }
    }

    // --- grazing: cells -> agents, LOCAL. Demand is normalised per cell so a
    // cell can never hand out more than it holds (conservation stays exact),
    // which also makes crowded neighbourhoods genuinely competitive.
    const graze = new Float64Array(n);
    for (let k = 0; k < c.resourceCells; k++) {
      if (this.res[k] <= 0) continue;
      let demandSum = 0;
      const demand = new Float64Array(n);
      for (let i = 0; i < n; i++) {
        const d = Ecosystem.wrapDelta(this.cellPos[k] - this.x[i]);
        const w = Math.exp(-(d * d) / twoSigmaSq);
        if (w < 1e-6) continue;
        demand[i] = w * appetite[i];
        demandSum += demand[i];
      }
      if (demandSum <= 0) continue;
      const taken = Math.min(c.grazeRate * this.res[k] * dt, this.res[k]);
      for (let i = 0; i < n; i++) {
        if (demand[i] <= 0) continue;
        graze[i] += taken * (demand[i] / demandSum);
      }
      this.res[k] -= taken;
    }

    for (let i = 0; i < n; i++) {
      const influx = graze[i] + (appetiteSum > 0
        ? c.feedRate * this.pool * (appetite[i] / appetiteSum) * dt
        : 0);
      // Nonlinear leak: with leakExponent > 1 a hoarding agent bleeds
      // disproportionately, which is what turns predation's rich-get-richer
      // into a boom/bust cycle instead of one permanent winner.
      const leak = c.leakRate * Math.pow(this.e[i], c.leakExponent) * dt;
      dE[i] = dE[i] * dt + influx - leak;
      // Only the GLOBAL feed is debited from the pool here: the grazed part was
      // already taken out of the resource cells above. Debiting it twice would
      // destroy energy and quietly break the conservation invariant.
      poolDelta += leak - (influx - graze[i]);
    }

    // --- integrate --------------------------------------------------------
    const maxStep = c.maxSpeed * dt;
    for (let i = 0; i < n; i++) {
      this.e[i] += dE[i];
      if (this.e[i] < 0) {
        // Clamp at zero and return the overdraw to the pool so the invariant
        // survives: an agent cannot lend energy it does not have.
        poolDelta += this.e[i];
        this.e[i] = 0;
      } else if (this.e[i] > c.capacity) {
        // Carrying capacity: the excess spills back to the pool rather than
        // vanishing, so the conservation invariant still holds exactly.
        poolDelta += this.e[i] - c.capacity;
        this.e[i] = c.capacity;
      }

      let vx = c.moveRate * fx[i] * dt;
      let vy = c.moveRate * fy[i] * dt;
      const speed = Math.sqrt(vx * vx + vy * vy);
      if (speed > maxStep) {
        const k = maxStep / speed;
        vx *= k; vy *= k;
      }
      this.x[i] = (this.x[i] + vx + 1) % 1;
      if (c.dimensions === 2) this.y[i] = (this.y[i] + vy + 1) % 1;

      // Bounded OU drift on intrinsic frequency: keeps the phase population
      // from locking into an exactly periodic limit cycle ("nothing repeats").
      if (c.freqDrift > 0) {
        const mean = this.freq0[i];
        const noise = this.rDrift.nextBipolar() * c.freqDrift * Math.sqrt(dt);
        this.freq[i] += noise - 0.5 * (this.freq[i] - mean) * dt * 0.01;
        if (this.freq[i] < c.freqLo) this.freq[i] = c.freqLo;
        if (this.freq[i] > c.freqHi) this.freq[i] = c.freqHi;
      }

      this.phase[i] = (this.phase[i] + (this.freq[i] + dPhase[i]) * dt + 1) % 1;
    }
    this.pool += poolDelta;
    // NO clamp on the pool. Clamping a negative pool to zero would CREATE
    // energy and break the conservation invariant silently — the whole
    // boundedness argument rests on that invariant, so a violation must be
    // observable, not papered over. (This bug was in the first draft: it made
    // a predation run report "unbounded" for a reason that was the harness's
    // fault, not the model's.) A negative pool is a real design error and the
    // metrics must see it.
    if (this.pool < 0) this.poolWentNegative = true;
    this.time += dt;
  }

  totalEnergy() {
    let s = this.pool;
    for (let i = 0; i < this.cfg.agentCount; i++) s += this.e[i];
    for (let k = 0; k < this.res.length; k++) s += this.res[k];
    return s;
  }

  maxAgentEnergy() {
    let m = 0;
    for (let i = 0; i < this.cfg.agentCount; i++) if (this.e[i] > m) m = this.e[i];
    return m;
  }

  /// Shannon entropy of the normalised agent-energy distribution, in bits.
  /// Flat distribution -> log2(n); all energy on one agent -> 0.
  entropy() {
    const n = this.cfg.agentCount;
    let sum = 0;
    for (let i = 0; i < n; i++) sum += this.e[i];
    if (sum <= 0) return 0;
    let h = 0;
    for (let i = 0; i < n; i++) {
      const p = this.e[i] / sum;
      if (p > 0) h -= p * Math.log2(p);
    }
    return h;
  }

  anyNonFinite() {
    for (let i = 0; i < this.cfg.agentCount; i++) {
      if (!Number.isFinite(this.e[i]) || !Number.isFinite(this.x[i]) ||
          !Number.isFinite(this.phase[i])) return true;
    }
    return !Number.isFinite(this.pool);
  }
}

// -----------------------------------------------------------------------------
// Metrics over a run
// -----------------------------------------------------------------------------
function mean(a) { return a.reduce((s, v) => s + v, 0) / a.length; }
function stddev(a) {
  const m = mean(a);
  return Math.sqrt(a.reduce((s, v) => s + (v - m) * (v - m), 0) / a.length);
}

/// Normalised autocorrelation of a series at a given lag.
/// Pearson correlation between two equal-length series.
function pearson(a, b) {
  const n = Math.min(a.length, b.length);
  if (n < 2) return 0;
  const ma = mean(a), mb = mean(b);
  let num = 0, da = 0, db = 0;
  for (let i = 0; i < n; i++) {
    num += (a[i] - ma) * (b[i] - mb);
    da += (a[i] - ma) ** 2;
    db += (b[i] - mb) ** 2;
  }
  return da > 0 && db > 0 ? num / Math.sqrt(da * db) : 0;
}

function autocorr(series, lag) {
  const n = series.length - lag;
  if (n <= 1) return 0;
  const m = mean(series);
  let num = 0, den = 0;
  for (let i = 0; i < series.length; i++) den += (series[i] - m) ** 2;
  for (let i = 0; i < n; i++) num += (series[i] - m) * (series[i + lag] - m);
  return den > 0 ? num / den : 0;
}

/// Run a simulation and return metrics. `sampleEvery` is in control steps.
function run(config, seed, durationSeconds, opts = {}) {
  const sim = new Ecosystem(config, seed);
  const blockRate = opts.blockRate || (48000 / 512); // 93.75 Hz control rate
  const dt = 1 / blockRate;
  const steps = Math.round(durationSeconds * blockRate);
  const sampleEvery = opts.sampleEvery || Math.max(1, Math.round(blockRate)); // ~1 Hz

  const budget = sim.cfg.energyBudget;
  const hSeries = [];
  const tSeries = [];
  const trace = opts.trace ? [] : null;
  const agentSeries = []; // [sample][agent] energies — drives the activity metrics

  let maxTotal = -Infinity, minTotal = Infinity, maxAgent = 0;
  let nonFinite = false;

  for (let k = 0; k < steps; k++) {
    sim.step(dt);
    if (sim.anyNonFinite()) { nonFinite = true; break; }
    if (k % sampleEvery === 0) {
      const tot = sim.totalEnergy();
      if (tot > maxTotal) maxTotal = tot;
      if (tot < minTotal) minTotal = tot;
      const ma = sim.maxAgentEnergy();
      if (ma > maxAgent) maxAgent = ma;
      const h = sim.entropy();
      hSeries.push(h);
      tSeries.push(sim.time);
      agentSeries.push(Array.from(sim.e));
      if (trace) {
        trace.push({
          t: sim.time, total: tot, pool: sim.pool, entropy: h,
          maxAgent: ma,
          energies: Array.from(sim.e),
          positions: Array.from(sim.x),
        });
      }
    }
  }

  // --- non-triviality ----------------------------------------------------
  // (a) entropy must keep MOVING, not settle;
  // (b) the last quarter must be as alive as the whole run (no late freeze);
  // (c) no strong short-period limit cycle.
  // --- per-agent activity ------------------------------------------------
  // NOTE (prototype finding): the roadmap proposes entropy of the energy
  // distribution as the non-triviality metric, but Shannon entropy is
  // PERMUTATION-INVARIANT. Energy sloshing between agents in a fixed pattern
  // holds entropy exactly constant while every agent's output swings — the
  // metric calls that "frozen" when it is the liveliest case. And a system
  // where all agents drift together in lockstep keeps entropy moving while
  // producing one modulation signal copied N times. Since each agent's energy
  // IS a modulation output driving a DSP target, what matters is (a) does each
  // agent's own value keep moving, and (b) are they decorrelated from each
  // other. Entropy is kept as a secondary statistic; these are the real gates.
  let agentActivity = 0, frozenAgents = 0, pairCorrSum = 0, pairCorrCount = 0;
  if (agentSeries && agentSeries.length > 1) {
    const n = agentSeries[0].length;
    const cols = [];
    for (let i = 0; i < n; i++) cols.push(agentSeries.map(row => row[i]));
    const grandMean = mean(cols.map(c => mean(c))) || 1e-12;
    for (const c of cols) {
      const s = stddev(c);
      agentActivity += s / grandMean;
      if (s / grandMean < 0.02) frozenAgents++;
    }
    agentActivity /= n;
    for (let i = 0; i < n; i++) {
      for (let j = i + 1; j < n; j++) {
        pairCorrSum += Math.abs(pearson(cols[i], cols[j]));
        pairCorrCount++;
      }
    }
  }
  const meanAgentPairCorr = pairCorrCount ? pairCorrSum / pairCorrCount : 1;

  const q = Math.floor(hSeries.length / 4);
  const lastQuarter = hSeries.slice(hSeries.length - q);
  const hStd = hSeries.length ? stddev(hSeries) : 0;
  const lateStd = lastQuarter.length ? stddev(lastQuarter) : 0;

  // Limit-cycle scan. NOT the maximum autocorrelation over all lags: for any
  // smooth signal that maximum always sits at the SHORTEST lag scanned, so the
  // first version of this metric reported "limit cycle at 9.0 s" for a 30-min
  // run whose scan window simply started at 9 s — it was measuring smoothness,
  // not periodicity. A genuine cycle is a RECURRENCE: the autocorrelation must
  // first decay (signal forgets itself), and only a peak AFTER that decay is
  // evidence of a repeat.
  const sampleHz = blockRate / sampleEvery;
  const maxLag = Math.floor(hSeries.length / 2);
  let decorrLag = -1;
  for (let lag = 1; lag < maxLag; lag++) {
    if (autocorr(hSeries, lag) < 0.2) { decorrLag = lag; break; }
  }
  let worstAc = 0, worstLagSeconds = 0;
  if (decorrLag > 0) {
    for (let lag = decorrLag; lag < maxLag; lag++) {
      const ac = autocorr(hSeries, lag);
      if (ac > worstAc) { worstAc = ac; worstLagSeconds = lag / sampleHz; }
    }
  }
  // decorrLag < 0 means the series never decorrelated within half the run —
  // it is one slow trend, which is the opposite of a short limit cycle.

  return {
    seed,
    steps,
    nonFinite,
    // boundedness
    maxTotal, minTotal, maxAgent,
    energyDrift: Number.isFinite(maxTotal) ? Math.abs(maxTotal - budget) / budget : Infinity,
    poolWentNegative: sim.poolWentNegative,
    bounded: !nonFinite && !sim.poolWentNegative && Number.isFinite(maxTotal) &&
             Math.abs(maxTotal - budget) / budget < 1e-9 &&
             Math.abs(minTotal - budget) / budget < 1e-9,
    // The real non-triviality gates (see note above).
    agentActivity, frozenAgents, meanAgentPairCorr,
    // non-triviality
    entropyMean: hSeries.length ? mean(hSeries) : 0,
    entropyStd: hStd,
    entropyLateStd: lateStd,
    entropyMin: hSeries.length ? Math.min(...hSeries) : 0,
    entropyMax: hSeries.length ? Math.max(...hSeries) : 0,
    worstAutocorr: worstAc,
    worstAutocorrLagSeconds: worstLagSeconds,
    pairOps: sim.pairOps,
    decorrelationLagSeconds: decorrLag > 0 ? decorrLag / sampleHz : Infinity,
    hSeries, tSeries, agentSeries, trace,
    sim,
  };
}

module.exports = {
  Xorshift32, deriveStreamSeed, Ecosystem, KINDS, KIND_COUNT,
  defaultConfig, defaultAffinity, run, autocorr, mean, stddev,
};
