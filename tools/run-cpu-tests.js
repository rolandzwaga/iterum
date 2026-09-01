#!/usr/bin/env node
// Run the timing-sensitive tests (CPU budgets, benchmarks, [long] renders) that
// every concurrent gate deliberately excludes.
//
// WHY THIS EXISTS: these cases assert wall-clock against audio time, so any
// competing load inflates the number and produces a false red on code nobody
// touched. Observed twice: "RuinaeVoice SC-002 CPU < 3%" read 3.047 % beside a
// 32-job clang-tidy (passed 5/5 alone), and "Rungler CPU usage is within budget"
// read 0.637 % vs 0.5 % inside a workflow running agents (passed 3/3 alone).
// CI (.github/workflows/ci.yml) and .claude/workflows/*-phase.js therefore filter
// them out with '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]'. This
// script is the other half of that bargain: the one place they DO run.
//
// The isolation guarantee is structural, not advisory -- suites run strictly one
// at a time, in-process, and nothing else may be running while you invoke it.
//
// ISOLATION HAS A SECOND CLAUSE: not concurrently, and not BACK-TO-BACK either.
// Measured on this repo 2026-09-01: running the timing lane continuously drove
// NoiseOrganism's SC-004 (c) from 142,794 to 162,840 ns on identical code (+14 %),
// and successive runs of the same suite failed 1 -> 3 -> 7 cases -- atmosphere,
// seraphis, continuous_body, harmonic_cloud and spectral_morph all went red
// without a line of their code changing, then all went green again after the
// machine idled. Sustained benchmarking heats the package and sustained boost
// clocks drop, so a suite run immediately after another measures the cooldown of
// the previous one. Hence SETTLE_MS between suites; raise it if you see the same
// suite flip verdicts across runs.
//
// Usage: node tools/run-cpu-tests.js [target ...]     (default: all suites)
//        SETTLE_MS=30000 node tools/run-cpu-tests.js  (longer cooldown)

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const BIN = 'build/windows-x64-release/bin/Release';
const DEFAULT_TARGETS = [
  'dsp_core_tests', 'dsp_primitives_tests', 'dsp_processors_tests',
  'dsp_systems_tests', 'dsp_effects_tests', 'shared_tests',
  'plugin_tests', 'approval_tests', 'disrumpo_tests', 'ruinae_tests',
  'innexus_tests', 'gradus_tests', 'membrum_tests', 'seraphis_tests',
];

// Catch2 unions comma-separated tags. [!benchmark] and hidden [.perf] cases are
// excluded from a default run, so they must be named explicitly to run at all.
const FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]';

const targets = process.argv.slice(2).length ? process.argv.slice(2) : DEFAULT_TARGETS;
const results = [];

// Synchronous cooldown between suites. Atomics.wait blocks this thread outright,
// which is what we want: nothing of ours should run during a settle, not even an
// event loop turn.
// 20 s was measured to be INSUFFICIENT on this hardware: with a 20 s settle the
// batched lane still produced a different failure set on every pass (aether_reverb,
// spectral_morph, seraphis -- none of them twice), while each suite passed when run
// alone. If you need a verdict you can rely on, run ONE suite per invocation with
// real idle time between them; batching is a convenience, not a gate.
const SETTLE_MS = Number(process.env.SETTLE_MS || 60000);
function settle(ms) {
  const buf = new Int32Array(new SharedArrayBuffer(4));
  Atomics.wait(buf, 0, 0, ms);
}

let first = true;
for (const t of targets) {
  const exe = path.join(BIN, `${t}.exe`);
  if (!fs.existsSync(exe)) {
    console.log(`SKIP ${t} (not built)`);
    continue;
  }
  if (!first && SETTLE_MS > 0) {
    process.stdout.write(`settle ${SETTLE_MS / 1000}s ... `);
    settle(SETTLE_MS);
  }
  first = false;
  process.stdout.write(`RUN  ${t} ... `);
  // Strictly sequential: spawnSync blocks, so no two suites are ever in flight.
  const r = spawnSync(exe, [FILTER], { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const log = path.join('f:/tmp', `cpu_${t}.log`);
  try { fs.writeFileSync(log, out); } catch { /* log dir optional */ }
  const summary = out.trim().split(/\r?\n/).filter(Boolean).pop() || '(no output)';
  const ok = r.status === 0;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${summary}`);
  if (!ok) console.log(`      full output: ${log}`);
  results.push({ t, ok });
}

const failed = results.filter((r) => !r.ok);
console.log(`\n${results.length - failed.length}/${results.length} suites passed.`);
if (failed.length) {
  console.log(`FAILED: ${failed.map((r) => r.t).join(', ')}`);
  console.log('Before treating any of these as a defect: confirm nothing else was');
  console.log('running, then re-run that suite ALONE, after the machine has idled.');
  console.log('A suite that flips verdicts between runs is measuring the machine,');
  console.log('not the code -- raise SETTLE_MS. Never relax a budget to fit.');
  process.exit(1);
}
