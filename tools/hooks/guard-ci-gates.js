#!/usr/bin/env node
// ==============================================================================
// guard-ci-gates.js  —  PreToolUse hook (Bash)
// ==============================================================================
// Runs the cheap, deterministic CI gates before `git commit` and blocks when any
// of them fails. Every check here costs milliseconds locally and a full CI
// round-trip (~7-40 min) to discover remotely, and NONE of them is exercised by
// a local build, test run, pluginval pass or clang-tidy sweep. That gap is the
// whole reason this hook exists -- a completely green local workflow has twice
// pushed a branch that CI rejected on the first job:
//
//   1. symbols.json (the ODR index) left stale after adding/renaming types.
//   2. A phaser/flanger test pinning a DSP render with a bit-exact digest over
//      raw float bits, which cannot pass on GCC or Apple Clang no matter what.
//
// Keep GENERATORS and LINTS in step with the "Generated Artifacts In Sync" and
// lint steps in .github/workflows/ci.yml. A gate added to CI but not here is a
// gate that only fires after push.
//
// Only runs --check / read-only forms, never writes. Reads the PreToolUse JSON
// envelope on stdin; exit 2 blocks. Fails open on any parse/IO/spawn problem so
// the hook can never wedge the session.
// ==============================================================================
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// Generators whose output CI compares against the committed copy.
const GENERATORS = [
    { script: 'gen-repo-map.js', artifact: 'specs/_architecture_/repo-map.json' },
    { script: 'gen-symbols.js', artifact: 'specs/_architecture_/symbols.json' },
    { script: 'gen-specs-index.js', artifact: 'specs/INDEX.md' },
];

// Deterministic architecture-invariant lints (prose rules turned into checks).
const LINTS = [
    { script: 'lint-layers.js', what: 'DSP layer-dependency violation' },
    { script: 'lint-odr.js', what: 'duplicate symbol (ODR hazard)' },
    { script: 'lint-arch-guarded-includes.js', what: 'arch-guarded krate include' },
    { script: 'lint-float-bit-goldens.js', what: 'bit-exact float golden digest' },
    { script: 'lint-midi-timing-goldens.js', what: 'byte-exact MIDI timing golden' },
    { script: 'lint-platform-type-literals.js', what: 'hardcoded platform-type constant in a test' },
    { script: 'lint-allocation-operator-overrides.js', what: 'hand-rolled global operator new/delete' },
    { script: 'lint-simd-aligned-loadstore.js', what: 'aligned hn::Load/Store (faults on AVX-512 runners)' },
    { script: 'lint-plugin-roster.js', what: 'plugin missing from a CI/tooling roster' },
];

function readStdin() {
    return new Promise((resolve) => {
        let data = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (c) => (data += c));
        process.stdin.on('end', () => resolve(data));
        if (process.stdin.isTTY) resolve('');
    });
}

function repoRoot() {
    // hook lives at <root>/tools/hooks/guard-ci-gates.js
    return path.resolve(__dirname, '..', '..');
}

/// True when the command is a real `git commit`. Excludes the read-only
/// inspection forms, which have no reason to be gated.
function isGitCommit(cmd) {
    if (!/\bgit\b[^\n]*\bcommit\b/.test(cmd)) return false;
    // `git log`, `git show <commit>`, `git rev-parse` etc. can mention "commit"
    // in a path or message without being a commit themselves.
    if (/\bgit\s+(log|show|diff|rev-parse|rev-list|status|cherry)\b/.test(cmd)) return false;
    return true;
}

/// Run a tools/ script and return its failure output, or null when it passed or
/// could not be run at all (which must never block).
function runGate(root, script, args) {
    const scriptPath = path.join(root, 'tools', script);
    if (!fs.existsSync(scriptPath)) return null; // script removed -> nothing to check

    try {
        execFileSync(process.execPath, [scriptPath, ...args], {
            cwd: root,
            stdio: 'pipe',
            timeout: 120000,
        });
        return null;
    } catch (err) {
        // A non-zero exit means the gate failed. Anything else (spawn failure,
        // timeout) is a hook problem, not a repo problem -- do not block on it.
        if (typeof err.status !== 'number' || err.status === 0) return null;
        const out = `${err.stdout || ''}${err.stderr || ''}`.toString().trim();
        return out || `${script} exited ${err.status}`;
    }
}

(async () => {
    let payload;
    try {
        payload = JSON.parse(await readStdin());
    } catch {
        process.exit(0);
    }

    const cmd = (payload && payload.tool_input && payload.tool_input.command) || '';
    if (!cmd || !isGitCommit(cmd)) process.exit(0);

    const root = repoRoot();
    const stale = [];
    const lintFailures = [];

    for (const { script, artifact } of GENERATORS) {
        if (runGate(root, script, ['--check']) !== null) stale.push({ script, artifact });
    }
    for (const { script, what } of LINTS) {
        const out = runGate(root, script, []);
        if (out !== null) lintFailures.push({ script, what, out });
    }

    if (stale.length === 0 && lintFailures.length === 0) process.exit(0);

    let msg = 'BLOCKED: cheap CI gates fail on this tree, so the push will go red.\n\n';

    if (stale.length > 0) {
        msg +=
            'Generated artifacts are stale ("Generated Artifacts In Sync" job).\n' +
            'Regenerate and stage them with this commit:\n\n' +
            stale.map((s) => `  ${s.artifact}  ->  node tools/${s.script}`).join('\n') +
            '\n\nsymbols.json goes stale whenever a class or struct is added, renamed\n' +
            'or removed under dsp/ or plugins/.\n\n';
    }

    for (const f of lintFailures) {
        msg += `Lint failed: ${f.what}  (node tools/${f.script})\n\n${f.out}\n\n`;
    }

    process.stderr.write(msg);
    process.exit(2);
})();
