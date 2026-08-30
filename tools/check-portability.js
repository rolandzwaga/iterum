#!/usr/bin/env node
// ==============================================================================
// check-portability.js
// ==============================================================================
// Syntax-checks changed C++ translation units with GCC (via WSL on Windows) to
// catch the class of error a local MSVC build structurally cannot find.
//
// The failure this exists to prevent: MSVC accepts constructs that GCC and Clang
// correctly reject, so a fully green Windows build can still break both the
// Linux and macOS CI legs. The instance that prompted this script:
//
//     constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeHWND;
//
// The SDK declares those as plain `const FIDString`, so they are not usable in a
// constant expression. MSVC compiled it; GCC and Clang both failed, ~7 minutes
// into CI, after the Windows leg had already gone green locally.
//
// This is a syntax check (-fsyntax-only), not a build: it is about portability
// of the source, not correctness of the binary. It takes seconds per file.
//
// Usage:
//   node tools/check-portability.js                 # files changed vs origin/main
//   node tools/check-portability.js <file> [...]    # explicit files
//   node tools/check-portability.js --staged        # staged files only
//
// Exits non-zero if any file fails to compile.
// ==============================================================================
'use strict';

const { execFileSync, spawn, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');

// Include roots mirroring what the CMake targets pass. Kept deliberately broad:
// this only needs to resolve headers, not link.
const INCLUDES = [
    '.',
    'dsp/include',
    'dsp/tests',
    'dsp/tests/test_helpers',
    'tests',
    'tests/test_helpers',
    'tools',
    'plugins/shared/src',
    'extern/vst3sdk',
    'extern/vst3sdk/vstgui4',
];

// Generated / fetched include roots, discovered rather than hard-coded so this
// keeps working when the build directory or dependency versions change.
function discoverGeneratedIncludes() {
    const found = [];
    const depsRoots = [
        'build/windows-x64-release/_deps',
        'build/linux-release/_deps',
        'build/windows-ninja/_deps',
    ];
    for (const rel of depsRoots) {
        const deps = path.join(ROOT, rel);
        if (!fs.existsSync(deps)) continue;
        const candidates = [
            'catch2-src/src',
            'catch2-build/generated-includes',
            'pffft-src/include',
            'highway-src',
        ];
        for (const c of candidates) {
            const p = path.join(deps, c);
            // Forward slashes: these are handed to g++ inside WSL, where a
            // Windows-style backslash path is not a path at all.
            if (fs.existsSync(p)) found.push(`${rel}/${c}`);
        }
        if (found.length) break; // one build tree is enough
    }
    return found;
}

// Per-plugin include roots and the compile definitions their targets set.
function pluginFlagsFor(file) {
    // The DSP test targets get the absolute fixture root the same way plugin
    // test targets do (dsp/tests/CMakeLists.txt). Same rule as below: the value
    // only has to exist as a token, nothing here opens it.
    if (file.startsWith('dsp/tests/')) {
        const flags = ['-DKRATE_DSP_TESTS_DIR=\\"/tmp\\"'];
        // dsp_effects_tests sets this TARGET-WIDE (dsp/tests/CMakeLists.txt:409)
        // so AetherReverb's FR-083 fault-injection hook is declared. Without it,
        // aether_reverb_nonfinite_test.cpp hits its own #error guard and this
        // script reports a portability failure that does not exist -- which would
        // block every commit through tools/hooks/guard-portability.js.
        if (file.startsWith('dsp/tests/unit/effects/')) {
            flags.push('-DKRATE_DSP_AETHER_TEST_HOOKS');
        }
        return flags;
    }
    const m = file.match(/plugins\/([a-z0-9-]+)\//);
    if (!m) return [];
    const plugin = m[1];
    const upper = plugin.toUpperCase();
    const flags = [`-I plugins/${plugin}/src`, `-I plugins/${plugin}/tests`];
    // Resource/fixture dirs are string macros; the value only has to exist as a
    // token, since nothing here opens them.
    flags.push(`-D${upper}_RESOURCES_DIR=\\"/tmp\\"`);
    flags.push(`-D${upper}_FIXTURES_DIR=\\"/tmp\\"`);
    // Source roots. Seraphis' SC-011 lock clause scans the audio-thread source
    // set at runtime and needs these two spelled the way
    // plugins/seraphis/tests/CMakeLists.txt sets them. Same rule as above: this
    // script only compiles, so the value need only exist as a token.
    flags.push(`-D${upper}_SRC_DIR=\\"/tmp\\"`);
    flags.push('-DKRATE_DSP_INCLUDE_DIR=\\"/tmp\\"');
    return flags;
}

// Prefer the current branch's own upstream as the diff base: on a long-lived
// feature branch, diffing against origin/main re-checks every TU the branch
// ever added (already pushed, already CI-verified) on every single run, and
// the check degrades to O(branch) instead of O(unpushed work).
function baseRef() {
    try {
        const branch = execFileSync('git', ['rev-parse', '--abbrev-ref', 'HEAD'], {
            cwd: ROOT,
            encoding: 'utf8',
        }).trim();
        if (branch && branch !== 'HEAD') {
            const remote = `origin/${branch}`;
            const r = spawnSync('git', ['rev-parse', '--verify', '--quiet', remote], {
                cwd: ROOT,
                encoding: 'utf8',
            });
            if (r.status === 0) return remote;
        }
    } catch {
        // fall through to origin/main
    }
    return 'origin/main';
}

function changedFiles(mode) {
    if (mode === '--staged') {
        try {
            return execFileSync('git', ['diff', '--cached', '--name-only', '--diff-filter=ACMR'], {
                cwd: ROOT,
                encoding: 'utf8',
            })
                .split('\n')
                .filter(Boolean);
        } catch {
            return [];
        }
    }
    // Unpushed commits on this branch, plus uncommitted work (the base...HEAD
    // form only sees committed trees, so the working-tree diff is unioned in),
    // plus UNTRACKED files.
    //
    // The untracked union is not a nicety. `git diff` in every form only reports
    // paths git already knows about, so a phase that ADDS new .cpp files sees
    // them silently skipped until the moment they are committed - i.e. this gate
    // is at its weakest on exactly the code most likely to be unportable, and it
    // still reports "all clear". Phase 11 hit this: ten new Seraphis TUs
    // (src/ui/*.cpp and five new test TUs) were never compiled by a run that
    // printed "all clear -- 18 compiled". `--exclude-standard` keeps .gitignore
    // honoured, so build outputs and generated sources stay out.
    const seen = new Set();
    try {
        execFileSync('git', ['ls-files', '--others', '--exclude-standard'], {
            cwd: ROOT,
            encoding: 'utf8',
        })
            .split('\n')
            .filter(Boolean)
            .forEach((f) => seen.add(f));
    } catch {
        // ignore
    }
    try {
        execFileSync('git', ['diff', '--name-only', '--diff-filter=ACMR', `${baseRef()}...HEAD`], {
            cwd: ROOT,
            encoding: 'utf8',
        })
            .split('\n')
            .filter(Boolean)
            .forEach((f) => seen.add(f));
    } catch {
        // No usable base (fresh clone, detached) -> working tree only.
    }
    try {
        execFileSync('git', ['diff', '--name-only', '--diff-filter=ACMR', 'HEAD'], {
            cwd: ROOT,
            encoding: 'utf8',
        })
            .split('\n')
            .filter(Boolean)
            .forEach((f) => seen.add(f));
    } catch {
        // ignore
    }
    return [...seen];
}

// Only translation units are worth checking: a header alone has no TU to compile,
// and every header here is reached through some .cpp anyway.
function isCheckable(f) {
    if (!/\.(cpp|cc)$/.test(f)) return false;
    if (f.startsWith('extern/')) return false;
    if (f.startsWith('build')) return false;
    return fs.existsSync(path.join(ROOT, f));
}

function haveWsl() {
    const r = spawnSync('wsl', ['-e', 'bash', '-lc', 'command -v g++'], { encoding: 'utf8' });
    return r.status === 0 && (r.stdout || '').trim().length > 0;
}

/// Translate the repo root to its WSL mount point, rather than hard-coding a
/// path that only works on one machine.
function wslRoot() {
    const drive = ROOT[0].toLowerCase();
    const rest = ROOT.slice(2).replace(/\\/g, '/');
    return `/mnt/${drive}${rest}`;
}

function checkFile(file, generated) {
    const includes = [...INCLUDES, ...generated].map((i) => `-I ${i}`).join(' ');
    const pluginFlags = pluginFlagsFor(file).join(' ');
    // bash -c, not -lc: a login shell re-reads the user profile on every spawn,
    // and nothing here needs it.
    const cmd =
        `cd ${wslRoot()} && g++ -std=c++20 -fsyntax-only -DNDEBUG -DRELEASE ` +
        `${includes} ${pluginFlags} "${file}" 2>&1`;

    return new Promise((resolve) => {
        const child = spawn('wsl', ['-e', 'bash', '-c', cmd]);
        let out = '';
        child.stdout.on('data', (d) => { out += d; });
        child.stderr.on('data', (d) => { out += d; });
        child.on('close', (code) => {
            // Missing headers mean the include set is incomplete for this TU, which
            // is a limitation of this script rather than a portability problem.
            if (/fatal error: .*: No such file or directory/.test(out)) {
                resolve({ status: 'skipped', detail: (out.match(/fatal error: ([^\n]*)/) || [])[1] || '' });
                return;
            }
            if (code === 0) {
                resolve({ status: 'ok' });
                return;
            }
            const firstError = (out.match(/^[^\n]*error:[^\n]*$/m) || [])[0] || out.slice(0, 400);
            resolve({ status: 'failed', detail: firstError.trim() });
        });
    });
}

// The per-file work is a full g++ front-end pass reading headers through the
// /mnt 9p bridge — heavily I/O bound, so a modest pool overlaps well.
async function checkAll(files, generated) {
    const concurrency = Math.max(1, Math.min(6, os.cpus().length - 2, files.length));
    const results = new Array(files.length);
    let next = 0;
    async function worker() {
        for (;;) {
            const i = next++;
            if (i >= files.length) return;
            results[i] = await checkFile(files[i], generated);
        }
    }
    await Promise.all(Array.from({ length: concurrency }, worker));
    return results;
}

async function main() {
    const args = process.argv.slice(2);
    const mode = args.find((a) => a.startsWith('--'));
    const explicit = args.filter((a) => !a.startsWith('--'));

    if (process.platform === 'win32' && !haveWsl()) {
        console.log('check-portability: WSL with g++ not available -- skipping.');
        console.log('Install with: wsl --install, then `sudo apt install g++`');
        process.exit(0);
    }

    const files = (explicit.length ? explicit : changedFiles(mode)).filter(isCheckable);
    if (files.length === 0) {
        console.log('check-portability: no changed C++ translation units to check.');
        process.exit(0);
    }

    const generated = discoverGeneratedIncludes();
    console.log(`check-portability: ${files.length} translation unit(s) with g++\n`);

    const results = await checkAll(files, generated);

    const failed = [];
    let skipped = 0;
    for (let i = 0; i < files.length; i++) {
        const f = files[i];
        const res = results[i];
        const label = f.length > 66 ? '...' + f.slice(-63) : f;
        if (res.status === 'ok') {
            console.log(`  OK      ${label}`);
        } else if (res.status === 'skipped') {
            skipped++;
            console.log(`  skip    ${label}  (${res.detail})`);
        } else {
            failed.push({ file: f, detail: res.detail });
            console.log(`  FAILED  ${label}`);
            console.log(`          ${res.detail}`);
        }
    }

    console.log('');
    if (failed.length) {
        console.log(
            `check-portability: ${failed.length} file(s) fail under g++ but may well build ` +
            `under MSVC.\nFix before pushing -- the Linux and macOS CI legs will fail on these.`
        );
        process.exit(1);
    }

    // A run where nothing actually compiled proves nothing. Reporting "all clear"
    // on a wall of skips is worse than reporting failure, because it is believed.
    const checked = files.length - skipped;
    if (checked === 0) {
        console.log(
            'check-portability: NOTHING was actually compiled -- every file was skipped for\n' +
            'missing headers, so this run proves nothing. The include set is wrong or the\n' +
            'build tree has not been configured. Fix the include discovery before trusting\n' +
            'this check.'
        );
        process.exit(1);
    }
    if (skipped > checked) {
        console.log(
            `check-portability: WARNING -- ${skipped} of ${files.length} file(s) were skipped\n` +
            `for missing headers. Coverage is thin; treat a pass here with suspicion.`
        );
    }
    console.log(
        `check-portability: all clear -- ${checked} compiled` +
        `${skipped ? `, ${skipped} skipped` : ''}.`
    );
    process.exit(0);
}

main().catch((err) => {
    console.error(`check-portability: internal error: ${err && err.message ? err.message : err}`);
    process.exit(1);
});
