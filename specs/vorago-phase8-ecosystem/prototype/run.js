// =============================================================================
// Vorago Phase 8 prototype — CLI driver
// =============================================================================
//   node run.js trace    [seconds]   Baseline run, CSV trace + summary
//   node run.js fuzz     [configs]   Random rule configs; boundedness + liveness
//   node run.js ablate   [seconds]   Turn each rule off in turn; what earns its place
//   node run.js determinism          Same seed twice -> identical; adjacent seeds differ
//   node run.js sweep    [seconds]   Grid over predation x capacity x leakExponent
//   node run.js seeds    [n]         Seed sensitivity: do different seeds diverge?
//
// All output CSV goes to ./out/ next to this script.
// =============================================================================

'use strict';

const fs = require('fs');
const path = require('path');
const sim = require('./ecosystem-sim');

const OUT = path.join(__dirname, 'out');
fs.mkdirSync(OUT, { recursive: true });

const cmd = process.argv[2] || 'trace';
const arg = process.argv[3] ? Number(process.argv[3]) : null;

function fmt(x, d = 4) {
  if (!Number.isFinite(x)) return String(x);
  return x.toFixed(d);
}

// -----------------------------------------------------------------------------
// trace — one baseline run at default config
// -----------------------------------------------------------------------------
function cmdTrace() {
  const seconds = arg || 1800; // 30 min, the roadmap's non-triviality window
  console.log(`# baseline trace: ${seconds}s at default config, seed 0xC0FFEE`);
  const r = sim.run({}, 0xc0ffee, seconds, { trace: true });

  const csv = ['t,total_energy,pool,entropy,max_agent_energy'];
  for (const row of r.trace) {
    csv.push([fmt(row.t, 2), fmt(row.total, 9), fmt(row.pool, 6),
              fmt(row.entropy, 6), fmt(row.maxAgent, 6)].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'baseline_trace.csv'), csv.join('\n'));

  // Per-agent energy at 1 Hz, for eyeballing who is alive when.
  const ec = ['t,' + r.trace[0].energies.map((_, i) => `agent${i}`).join(',')];
  for (const row of r.trace) {
    ec.push([fmt(row.t, 2), ...row.energies.map(v => fmt(v, 6))].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'baseline_agents.csv'), ec.join('\n'));

  report(r);
  console.log(`\nwrote out/baseline_trace.csv, out/baseline_agents.csv`);
}

function report(r) {
  const maxH = Math.log2(r.sim.cfg.agentCount);
  console.log(`  non-finite            : ${r.nonFinite}`);
  console.log(`  total energy  min/max : ${fmt(r.minTotal, 9)} / ${fmt(r.maxTotal, 9)}  (budget 1.0)`);
  console.log(`  energy drift          : ${r.energyDrift.toExponential(3)}  ${r.bounded ? '[BOUNDED]' : '[LEAK/GROWTH]'}`);
  console.log(`  max agent energy      : ${fmt(r.maxAgent, 6)}`);
  console.log(`  entropy mean/std      : ${fmt(r.entropyMean)} / ${fmt(r.entropyStd)}   (max possible ${fmt(maxH, 3)})`);
  console.log(`  entropy min..max      : ${fmt(r.entropyMin)} .. ${fmt(r.entropyMax)}`);
  console.log(`  entropy std last 1/4  : ${fmt(r.entropyLateStd)}  ${r.entropyLateStd > 0.02 ? '[STILL MOVING]' : '[FROZEN?]'}`);
  console.log(`  worst autocorr        : ${fmt(r.worstAutocorr, 3)} at lag ${fmt(r.worstAutocorrLagSeconds, 1)}s ${r.worstAutocorr > 0.8 ? '[LIMIT CYCLE?]' : '[no short cycle]'}`);
  console.log(`  pair ops              : ${r.pairOps.toLocaleString()}`);
}

// -----------------------------------------------------------------------------
// fuzz — random rule configurations, the roadmap's boundedness criterion
// -----------------------------------------------------------------------------
function randomConfig(rng) {
  const A = [];
  for (let i = 0; i < sim.KIND_COUNT; i++) {
    A.push([]);
    for (let j = 0; j < sim.KIND_COUNT; j++) A[i].push(rng.range(-2.0, 2.0));
  }
  // Symmetrise: an asymmetric affinity matrix means i pulls j while j pushes i,
  // which is a momentum source. Positions are bounded by the torus anyway, but
  // the C++ component should make this structural too.
  for (let i = 0; i < sim.KIND_COUNT; i++)
    for (let j = i + 1; j < sim.KIND_COUNT; j++) A[j][i] = A[i][j];

  return {
    agentCount: Math.round(rng.range(24, 48)),
    dimensions: rng.nextUnipolar() < 0.5 ? 1 : 2,
    kernelSigma: rng.range(0.03, 0.35),
    exchangeRate: rng.range(0.0, 3.0),
    moveRate: rng.range(0.0, 0.2),
    maxSpeed: rng.range(0.001, 0.05),
    syncRate: rng.range(0.0, 0.5),
    feedRate: rng.range(0.0, 1.0),
    leakRate: rng.range(0.0, 1.0),
    freqLo: rng.range(0.0005, 0.005),
    freqHi: rng.range(0.006, 0.05),
    freqDrift: rng.range(0, 0.0002),
    appetiteDepth: rng.range(0, 1),
    affinity: A,
  };
}

function cmdFuzz() {
  const count = arg || 1000;
  const seconds = 600; // accelerated: 10 min per config
  console.log(`# fuzz: ${count} random rule configs x ${seconds}s`);
  const meta = new sim.Xorshift32(0xf0f0f0);

  let unbounded = 0, nonFinite = 0, frozen = 0, cyclic = 0, alive = 0;
  const worst = { drift: 0, seed: 0 };
  const rows = ['config,seed,bounded,drift,entropy_mean,entropy_std,late_std,worst_ac,ac_lag_s'];

  for (let k = 0; k < count; k++) {
    const cfgSeed = meta.next();
    const rng = new sim.Xorshift32(cfgSeed);
    const cfg = randomConfig(rng);
    const r = sim.run(cfg, cfgSeed, seconds, { sampleEvery: 188 }); // ~0.5 Hz

    if (r.nonFinite) nonFinite++;
    if (!r.bounded) {
      unbounded++;
      if (r.energyDrift > worst.drift) { worst.drift = r.energyDrift; worst.seed = cfgSeed; }
    }
    if (r.entropyLateStd <= 0.02) frozen++;
    else if (r.worstAutocorr > 0.8) cyclic++;
    else alive++;

    rows.push([k, cfgSeed, r.bounded ? 1 : 0, r.energyDrift.toExponential(3),
               fmt(r.entropyMean), fmt(r.entropyStd), fmt(r.entropyLateStd),
               fmt(r.worstAutocorr, 3), fmt(r.worstAutocorrLagSeconds, 1)].join(','));

    if ((k + 1) % 100 === 0) process.stdout.write(`  ${k + 1}/${count}\r`);
  }
  fs.writeFileSync(path.join(OUT, 'fuzz_results.csv'), rows.join('\n'));

  console.log(`\n  configs               : ${count}`);
  console.log(`  non-finite            : ${nonFinite}   ${nonFinite === 0 ? '[OK]' : '[FAIL]'}`);
  console.log(`  unbounded             : ${unbounded}   ${unbounded === 0 ? '[OK]' : '[FAIL]'}`);
  if (unbounded) console.log(`    worst drift ${worst.drift.toExponential(3)} at seed ${worst.seed}`);
  console.log(`  --- liveness split (informational, not a gate) ---`);
  console.log(`  alive                 : ${alive}  (${(100 * alive / count).toFixed(1)}%)`);
  console.log(`  frozen (late std<=.02): ${frozen}  (${(100 * frozen / count).toFixed(1)}%)`);
  console.log(`  short limit cycle     : ${cyclic}  (${(100 * cyclic / count).toFixed(1)}%)`);
  console.log(`\nwrote out/fuzz_results.csv`);
}

// -----------------------------------------------------------------------------
// ablate — which rules actually earn their place?
// -----------------------------------------------------------------------------
function cmdAblate() {
  const seconds = arg || 1800;
  const variants = [
    ['baseline',          {}],
    ['no exchange',       { exchangeRate: 0 }],
    ['no movement',       { moveRate: 0 }],
    ['no sync',           { syncRate: 0 }],
    ['no freq drift',     { freqDrift: 0 }],
    ['no appetite gate',  { appetiteDepth: 0 }],
    ['no sync+no gate',   { syncRate: 0, appetiteDepth: 0 }],
    ['2D habitat',        { dimensions: 2 }],
  ];
  console.log(`# ablation: ${seconds}s each, seed 0xC0FFEE\n`);
  const head = 'variant'.padEnd(18) + 'H mean'.padStart(9) + 'H std'.padStart(9) +
               'late std'.padStart(10) + 'worst ac'.padStart(10) + 'lag s'.padStart(8) +
               '  verdict';
  console.log(head);
  console.log('-'.repeat(head.length + 8));

  const rows = ['variant,entropy_mean,entropy_std,late_std,worst_ac,ac_lag_s,bounded'];
  for (const [name, over] of variants) {
    const r = sim.run(over, 0xc0ffee, seconds, {});
    let verdict;
    if (!r.bounded) verdict = 'UNBOUNDED';
    else if (r.entropyLateStd <= 0.02) verdict = 'frozen';
    else if (r.worstAutocorr > 0.8) verdict = `cycle ~${fmt(r.worstAutocorrLagSeconds, 0)}s`;
    else verdict = 'alive';
    console.log(
      name.padEnd(18) + fmt(r.entropyMean).padStart(9) + fmt(r.entropyStd).padStart(9) +
      fmt(r.entropyLateStd).padStart(10) + fmt(r.worstAutocorr, 3).padStart(10) +
      fmt(r.worstAutocorrLagSeconds, 1).padStart(8) + '  ' + verdict);
    rows.push([name, fmt(r.entropyMean), fmt(r.entropyStd), fmt(r.entropyLateStd),
               fmt(r.worstAutocorr, 3), fmt(r.worstAutocorrLagSeconds, 1), r.bounded ? 1 : 0].join(','));
  }
  fs.writeFileSync(path.join(OUT, 'ablation.csv'), rows.join('\n'));
  console.log(`\nwrote out/ablation.csv`);
}

// -----------------------------------------------------------------------------
// determinism
// -----------------------------------------------------------------------------
function cmdDeterminism() {
  console.log('# determinism');
  const a = sim.run({}, 12345, 300, {});
  const b = sim.run({}, 12345, 300, {});
  let identical = a.hSeries.length === b.hSeries.length;
  if (identical) {
    for (let i = 0; i < a.hSeries.length; i++) {
      if (a.hSeries[i] !== b.hSeries[i]) { identical = false; break; }
    }
  }
  console.log(`  same seed, bit-identical entropy series : ${identical} ${identical ? '[OK]' : '[FAIL]'}`);

  const c = sim.run({}, 12346, 300, {});
  const corr = correlation(a.hSeries, c.hSeries);
  console.log(`  adjacent seed correlation               : ${fmt(corr, 4)} ${Math.abs(corr) < 0.5 ? '[decorrelated]' : '[SUSPICIOUS]'}`);
}

function correlation(a, b) {
  const n = Math.min(a.length, b.length);
  const ma = sim.mean(a.slice(0, n)), mb = sim.mean(b.slice(0, n));
  let num = 0, da = 0, db = 0;
  for (let i = 0; i < n; i++) {
    num += (a[i] - ma) * (b[i] - mb);
    da += (a[i] - ma) ** 2;
    db += (b[i] - mb) ** 2;
  }
  return da > 0 && db > 0 ? num / Math.sqrt(da * db) : 0;
}

// -----------------------------------------------------------------------------
// sweep — locate the regime that is alive rather than flat or frozen
// -----------------------------------------------------------------------------
function cmdSweep() {
  const seconds = arg || 900;
  const predations = [0.5, 0.6, 0.7, 0.8, 0.9, 1.0];
  const capacities = [0.02, 0.04, 0.08, 0.15, 1.0];
  const leakExps = [1.0, 1.5, 2.0];
  console.log(`# sweep: predation x capacity x leakExponent, ${seconds}s each, 3 seeds each\n`);

  const rows = ['predation,capacity,leak_exp,entropy_mean,entropy_std,late_std,worst_ac,ac_lag_s,seed_corr,bounded'];
  const results = [];
  for (const p of predations) {
    for (const cap of capacities) {
      for (const le of leakExps) {
        const over = { predation: p, capacity: cap, leakExponent: le };
        const runs = [0xc0ffee, 0xbeef01, 0x51e7a2].map(s => sim.run(over, s, seconds, { sampleEvery: 94 }));
        const ok = runs.every(r => r.bounded && !r.nonFinite);
        const eMean = sim.mean(runs.map(r => r.entropyMean));
        const eStd = sim.mean(runs.map(r => r.entropyStd));
        const lateStd = sim.mean(runs.map(r => r.entropyLateStd));
        const ac = sim.mean(runs.map(r => r.worstAutocorr));
        const acLag = sim.mean(runs.map(r => r.worstAutocorrLagSeconds));
        // Seed sensitivity: mean pairwise correlation between seeds. LOW is good.
        const sc = (Math.abs(correlation(runs[0].hSeries, runs[1].hSeries)) +
                    Math.abs(correlation(runs[0].hSeries, runs[2].hSeries)) +
                    Math.abs(correlation(runs[1].hSeries, runs[2].hSeries))) / 3;
        results.push({ p, cap, le, eMean, eStd, lateStd, ac, acLag, sc, ok });
        rows.push([p, cap, le, fmt(eMean), fmt(eStd), fmt(lateStd), fmt(ac, 3),
                   fmt(acLag, 1), fmt(sc, 3), ok ? 1 : 0].join(','));
      }
    }
  }
  fs.writeFileSync(path.join(OUT, 'sweep.csv'), rows.join('\n'));

  // A configuration is a CANDIDATE when it is bounded, still moving at the end,
  // has no short limit cycle, and its seeds actually diverge.
  const candidates = results.filter(r => r.ok && r.lateStd > 0.05 && r.ac < 0.8 && r.sc < 0.5);
  candidates.sort((a, b) => (b.lateStd - b.sc) - (a.lateStd - a.sc));

  console.log('pred  cap    leak   H mean   H std  late std  worst ac  seed corr  verdict');
  console.log('-'.repeat(78));
  for (const r of results) {
    let v = !r.ok ? 'UNBOUNDED'
      : r.lateStd <= 0.05 ? 'flat/frozen'
      : r.ac > 0.8 ? 'cyclic'
      : r.sc >= 0.5 ? 'seed-blind'
      : 'ALIVE';
    console.log(
      String(r.p).padEnd(6) + String(r.cap).padEnd(7) + String(r.le).padEnd(7) +
      fmt(r.eMean, 3).padStart(7) + fmt(r.eStd, 3).padStart(8) +
      fmt(r.lateStd, 3).padStart(10) + fmt(r.ac, 3).padStart(10) +
      fmt(r.sc, 3).padStart(11) + '  ' + v);
  }
  console.log(`\n  candidates (alive + seed-sensitive): ${candidates.length}/${results.length}`);
  if (candidates.length) {
    const b = candidates[0];
    console.log(`  best: predation=${b.p} capacity=${b.cap} leakExponent=${b.le}`);
    console.log(`        late std ${fmt(b.lateStd)}, worst ac ${fmt(b.ac, 3)}, seed corr ${fmt(b.sc, 3)}`);
  }
  console.log(`\nwrote out/sweep.csv`);
}

// -----------------------------------------------------------------------------
// seeds — do per-voice seeds actually produce different voices?
// -----------------------------------------------------------------------------
function cmdSeeds() {
  const n = arg || 8;
  const seconds = 900;
  console.log(`# seed sensitivity: ${n} seeds x ${seconds}s at default config`);
  const runs = [];
  const rng = new sim.Xorshift32(0x5EED);
  for (let i = 0; i < n; i++) runs.push(sim.run({}, rng.next(), seconds, { sampleEvery: 94 }));

  // Correlate PER-AGENT trajectories, not entropy. Entropy is permutation-
  // invariant, so two seeds can share an entropy curve while every agent's
  // actual modulation output differs — and agent i drives target i in both
  // voices, so agent-wise comparison is the one that answers "would these two
  // voices sound alike?". Measuring this on entropy reported 0.85 (seed-blind)
  // for a configuration whose per-agent signals are far less alike.
  const agentCount = runs[0].agentSeries[0].length;
  function agentCol(run, i) { return run.agentSeries.map(row => row[i]); }

  let sum = 0, count = 0, worst = 0, hSum = 0, hCount = 0;
  for (let i = 0; i < n; i++) {
    for (let j = i + 1; j < n; j++) {
      hSum += Math.abs(correlation(runs[i].hSeries, runs[j].hSeries)); hCount++;
      let perAgent = 0;
      for (let a = 0; a < agentCount; a++) {
        perAgent += Math.abs(correlation(agentCol(runs[i], a), agentCol(runs[j], a)));
      }
      perAgent /= agentCount;
      sum += perAgent; count++;
      if (perAgent > worst) worst = perAgent;
    }
  }
  // NOISE FLOOR. These signals are slow (decorrelation ~150 s), so a 900 s
  // window holds only a handful of INDEPENDENT samples and |correlation|
  // between genuinely unrelated slow signals runs high by chance — measured
  // 0.61 at 900 s, 0.50 at 3600 s. An absolute threshold is therefore
  // meaningless: judging against 0.5 declared this configuration "seed-blind"
  // when its cross-seed correlation was BELOW the floor. The reference is
  // different agents within the SAME run, which are as unrelated as two
  // signals in this system ever get.
  let floor = 0, floorCount = 0;
  const c0 = [];
  for (let a = 0; a < agentCount; a++) c0.push(agentCol(runs[0], a));
  for (let a = 0; a < agentCount; a++) {
    for (let b = a + 1; b < agentCount; b++) {
      floor += Math.abs(correlation(c0[a], c0[b])); floorCount++;
    }
  }
  const noiseFloor = floor / floorCount;

  const meanCorr = sum / count;
  console.log(`  mean |corr| PER-AGENT between seeds : ${fmt(meanCorr, 4)}`);
  console.log(`  worst pair                          : ${fmt(worst, 4)}`);
  console.log(`  noise floor (diff agents, same run) : ${fmt(noiseFloor, 4)}`);
  console.log(`  verdict                             : ${meanCorr <= noiseFloor ? '[voices differ — at or below the floor]' : '[SEED-BLIND — above the floor]'}`);
  console.log(`  (entropy-based figure, for contrast) : ${fmt(hSum / hCount, 4)}  <- permutation-invariant, misleading`);
  console.log(`  entropy mean spread across seeds    : ${fmt(Math.min(...runs.map(r => r.entropyMean)), 3)} .. ${fmt(Math.max(...runs.map(r => r.entropyMean)), 3)}`);
}

// -----------------------------------------------------------------------------
switch (cmd) {
  case 'trace': cmdTrace(); break;
  case 'fuzz': cmdFuzz(); break;
  case 'ablate': cmdAblate(); break;
  case 'determinism': cmdDeterminism(); break;
  case 'sweep': cmdSweep(); break;
  case 'seeds': cmdSeeds(); break;
  default:
    console.error(`unknown command: ${cmd}`);
    process.exit(1);
}
