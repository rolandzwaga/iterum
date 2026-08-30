#!/usr/bin/env node
// ==============================================================================
// check-preset-generator-determinism.js — Seraphis generator determinism gate
// ==============================================================================
// Seraphis Phase 12, FR-014 / FR-035a, measured by SC-016.
//
// FR-014 makes two claims about `seraphis_preset_generator`, and until this
// script existed only the first had ever been observed (once, by hand):
//
//   1. DETERMINISM  — two runs into two empty directories produce byte-identical
//                     trees.
//   2. IDEMPOTENCE  — re-running over an already-populated tree changes nothing.
//
// Both are checked here, and the exit code is the gate: this runs in the FR-035
// release gate beside `check-portability.js`, so a later change that makes the
// generator non-deterministic (an unordered container, a directory iteration, an
// embedded timestamp, an RNG draw) fails automatically instead of shipping.
//
//   node tools/check-preset-generator-determinism.js [--bin <path>]
//
// Exit 0 with a one-line summary; exit 1 printing the first differing path.
// Temp directories are removed on success and KEPT on failure, with their paths
// printed, so the difference is inspectable with a normal diff tool.
//
// ------------------------------------------------------------------------------
// Why a BYTE comparison is legitimate here — and only here.
// ------------------------------------------------------------------------------
// The project rule (roadmap "Cross-Cutting Constraints"; ci.yml's float-bit-golden
// lint) forbids bit-exact float goldens, including integer digests derived from
// float bits, because they demand identical FP codegen across MSVC / GCC /
// AppleClang(-ffast-math). That rule is not violated by this script: spec C-7
// scopes FR-014's byte claim to ONE binary, on ONE machine, in ONE run set. The
// bytes compared below are produced by the same executable image within seconds
// of each other; nothing here is compared across toolchains, and nothing here is
// committed as a reference value. The cross-toolchain question — "is the
// committed tree what the generator produces?" — is FR-029's SEMANTIC comparison
// (spec C-8), which is a different check in a different place.
// ==============================================================================
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO_ROOT = path.resolve(__dirname, '..');
const TOOL = 'check-preset-generator-determinism';

// Default binary resolution order (plan §1.5): the Windows multi-config Release
// output first, then the single-config `build/bin` layout `release.yml:170-174`
// runs the generator from on the Linux leg.
const DEFAULT_BINARIES = [
    'build/windows-x64-release/bin/Release/seraphis_preset_generator.exe',
    'build/bin/seraphis_preset_generator',
    'build/bin/seraphis_preset_generator.exe',
];

const USAGE = `usage: node tools/${TOOL}.js [--bin <path>]

  --bin <path>   Path to seraphis_preset_generator (absolute, or relative to the
                 current working directory). Defaults, tried in order:
${DEFAULT_BINARIES.map((b) => `                   ${b}`).join('\n')}
  -h, --help     Print this message.`;

// ------------------------------------------------------------------------------
// Failure handling: every failure path prints the kept temp directories so the
// difference can be inspected, then exits 1.
// ------------------------------------------------------------------------------
const keepOnFailure = [];

function fail(message, details) {
    console.error(`${TOOL}: FAILED — ${message}`);
    if (details) console.error(details);
    if (keepOnFailure.length > 0) {
        console.error('\nTemp directories KEPT for inspection:');
        for (const d of keepOnFailure) console.error(`  ${d}`);
    }
    process.exit(1);
}

function parseArgs(argv) {
    const opts = { bin: null };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '-h' || a === '--help') {
            console.log(USAGE);
            process.exit(0);
        } else if (a === '--bin') {
            if (i + 1 >= argv.length) fail('--bin requires a path argument.\n\n' + USAGE);
            opts.bin = argv[++i];
        } else {
            fail(`unrecognized argument '${a}'.\n\n` + USAGE);
        }
    }
    return opts;
}

function resolveBinary(explicit) {
    if (explicit) {
        const p = path.resolve(explicit);
        if (!fs.existsSync(p)) {
            fail(`--bin '${explicit}' does not exist (resolved to ${p}).`);
        }
        return p;
    }
    for (const rel of DEFAULT_BINARIES) {
        const p = path.join(REPO_ROOT, rel);
        if (fs.existsSync(p)) return p;
    }
    fail(
        'seraphis_preset_generator not found. Build it first:\n' +
        '  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release ' +
        '--config Release --target seraphis_preset_generator\n' +
        'or pass --bin <path>. Searched, relative to the repo root:\n' +
        DEFAULT_BINARIES.map((b) => `  ${b}`).join('\n')
    );
    return null; // unreachable; fail() exits
}

// ------------------------------------------------------------------------------
// Generator invocation. argv[1] is the output directory (FR-011).
// ------------------------------------------------------------------------------
function runGenerator(bin, outDir, label) {
    const r = spawnSync(bin, [outDir], { cwd: REPO_ROOT, encoding: 'utf8' });
    if (r.error) {
        fail(`could not run the generator (${label}): ${r.error.message}`);
    }
    if (r.status !== 0) {
        fail(
            `generator exited ${r.status} (${label}).`,
            `--- stdout ---\n${r.stdout || ''}\n--- stderr ---\n${r.stderr || ''}`
        );
    }
    return r;
}

// ------------------------------------------------------------------------------
// Tree walk. Keys are POSIX-style relative paths so the comparison and the
// reported names are stable on Windows and Linux alike; the list is sorted so the
// "first differing path" is a well-defined thing rather than whatever the
// filesystem handed back.
// ------------------------------------------------------------------------------
function walkTree(root) {
    const files = [];
    const visit = (dir) => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, e.name);
            if (e.isDirectory()) visit(full);
            else if (e.isFile()) files.push(full);
        }
    };
    visit(root);
    const out = new Map();
    for (const full of files.sort()) {
        const rel = path.relative(root, full).split(path.sep).join('/');
        out.set(rel, full);
    }
    return new Map([...out.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)));
}

function readAll(tree) {
    const bytes = new Map();
    for (const [rel, full] of tree) bytes.set(rel, fs.readFileSync(full));
    return bytes;
}

/// Returns a human-readable description of the FIRST difference, or null if the
/// two byte maps are identical. Path-set differences are reported before content
/// differences, because a missing file explains a content mismatch and not the
/// other way round.
function firstDifference(bytesA, bytesB, labelA, labelB) {
    const relsA = [...bytesA.keys()];
    const relsB = [...bytesB.keys()];

    for (const rel of relsA) {
        if (!bytesB.has(rel)) return `present in ${labelA} but missing from ${labelB}: ${rel}`;
    }
    for (const rel of relsB) {
        if (!bytesA.has(rel)) return `present in ${labelB} but missing from ${labelA}: ${rel}`;
    }
    for (const rel of relsA) {
        const a = bytesA.get(rel);
        const b = bytesB.get(rel);
        if (Buffer.compare(a, b) !== 0) {
            let offset = -1;
            const n = Math.min(a.length, b.length);
            for (let i = 0; i < n; i++) {
                if (a[i] !== b[i]) { offset = i; break; }
            }
            if (offset < 0) offset = n; // identical prefix, differing length
            return (
                `byte-level difference: ${rel}\n` +
                `  ${labelA}: ${a.length} bytes\n` +
                `  ${labelB}: ${b.length} bytes\n` +
                `  first differing byte offset: ${offset}`
            );
        }
    }
    return null;
}

// ------------------------------------------------------------------------------
// main
// ------------------------------------------------------------------------------
function main() {
    const opts = parseArgs(process.argv.slice(2));
    const bin = resolveBinary(opts.bin);

    const tmpBase = path.join(os.tmpdir(), 'seraphis-presets-');
    const dirA = fs.mkdtempSync(tmpBase);
    keepOnFailure.push(dirA);
    const dirB = fs.mkdtempSync(tmpBase);
    keepOnFailure.push(dirB);

    console.log(`${TOOL}: binary  ${bin}`);
    console.log(`${TOOL}: run A   ${dirA}`);
    console.log(`${TOOL}: run B   ${dirB}`);

    // --- 1. Determinism: two runs into two fresh, empty directories ------------
    runGenerator(bin, dirA, 'run A');
    runGenerator(bin, dirB, 'run B');

    const bytesA = readAll(walkTree(dirA));
    const bytesB = readAll(walkTree(dirB));

    // A generator that wrote nothing would make every comparison below vacuously
    // true, so the count is asserted before the comparison, not after it.
    if (bytesA.size === 0) {
        fail(`run A wrote 0 files into ${dirA} — nothing to compare (vacuous pass refused).`);
    }

    const diff = firstDifference(bytesA, bytesB, 'run A', 'run B');
    if (diff) {
        fail(`the two fresh runs are NOT byte-identical (FR-014 determinism).`, `  ${diff}`);
    }

    // --- 2. Idempotence: a third run over the already-populated tree A ---------
    runGenerator(bin, dirA, 'run C (over the populated tree A)');
    const bytesA2 = readAll(walkTree(dirA));

    const changed = firstDifference(bytesA, bytesA2, 'tree A before re-run', 'tree A after re-run');
    if (changed) {
        fail(`re-running over an existing tree CHANGED it (FR-014 idempotence).`, `  ${changed}`);
    }

    // --- success ---------------------------------------------------------------
    fs.rmSync(dirA, { recursive: true, force: true });
    fs.rmSync(dirB, { recursive: true, force: true });

    console.log(
        `${TOOL}: OK — ${bytesA.size} file(s); 0 differing between two fresh runs, ` +
        `0 changed by a third run over an existing tree.`
    );
    process.exit(0);
}

main();
