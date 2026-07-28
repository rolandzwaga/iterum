#!/usr/bin/env node
//
// lint-nonfinite-symbols.js
// =============================================================================
// Fails when a DSP header or source that is NOT compiled with -fno-fast-math
// names one of the standard-library non-finite predicates or constants:
//
//     std::isnan   std::isinf   std::isfinite
//     std::numeric_limits<T>::infinity()
//     std::numeric_limits<T>::quiet_NaN()   /  signaling_NaN()
//     NAN / INFINITY / HUGE_VALF  (the <cmath> macros)
//
// WHY THIS IS A GATE AND NOT A REVIEW ITEM. The macOS leg of this repo builds
// with -ffast-math (it inherits the VST3 SDK's global flags) and MSVC ships
// /fp:fast in several targets. Under -ffinite-math-only the compiler is
// entitled to assume no operand is ever NaN or Inf, so:
//
//   * `std::isnan(x)` folds to `false` -- the guard silently disappears, and
//     the NaN it was there to catch propagates into the host's audio buffer;
//   * `std::numeric_limits<float>::quiet_NaN()` / `infinity()` fold to finite
//     garbage, so a test that injects them is not testing what it claims.
//
// Both failure modes are INVISIBLE on Windows. The house remedy is the
// bit-pattern check in core/db_utils.h (`detail::isNaN` / `detail::isInf`,
// which inspect the IEEE-754 exponent field) and, for tests that must build
// non-finite INPUTS, construction from bit patterns through a `volatile` sink.
//
// Seraphis Phase 5 (specs/seraphis-phase5-atmosphere/spec.md, SC-013) requires
// this to be "enforced by a scripted grep gate run alongside the lints ... not
// by a review step. A manual check is not a gate." That is what this file is.
//
// NOT flagged (deliberately):
//   * Translation units listed with COMPILE_OPTIONS -fno-fast-math / /fp:precise
//     in dsp/tests/CMakeLists.txt or plugins/*/CMakeLists.txt. Those are the
//     documented exception: the predicates work there because fast-math is off,
//     and every NaN-injecting TU in this repo is in that list.
//   * `std::numeric_limits<T>::max() / min() / lowest() / epsilon() / digits`,
//     which are unaffected by fast-math.
//   * Comments. A line whose match sits after `//` or inside a /* */ block is
//     documentation ABOUT the rule -- this very file, and the banners of the
//     headers that explain why they use the bit-pattern form instead.
//
// SCOPE, STATED HONESTLY. This gate covers an EXPLICIT LIST of guarded files
// (GUARDED below), not the whole tree. Run with --all it reports 63 pre-existing
// hits across dsp/, plugins/ and tests/ -- the accumulated fast-math debt of
// components written before the rule was written down, including a handful in
// core/db_utils.h itself that are deliberate (it RETURNS quiet_NaN from its own
// documented-domain helpers). Turning those red today would gate every commit
// on an unrelated repo-wide cleanup, so the list is the enforcement surface and
// it GROWS as components are converted. Adding a file here is a one-line change;
// the point of the list is that a file on it can never regress silently.
//
// Usage:
//   node tools/lint-nonfinite-symbols.js          # gate the guarded files
//   node tools/lint-nonfinite-symbols.js --all    # survey the whole tree
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

// The enforcement surface. Repo-relative, forward slashes.
//
// Seraphis Phase 5 (SC-013) put the first five entries here: the atmosphere
// engine header and its three fast-math-built test TUs. The fourth test TU,
// atmosphere_engine_nonfinite_test.cpp, is deliberately ABSENT -- it carries
// -fno-fast-math source properties in dsp/tests/CMakeLists.txt precisely so it
// can build non-finite inputs, and collectFastMathExemptTus() would exempt it
// anyway.
const GUARDED = [
  'dsp/include/krate/dsp/systems/atmosphere_engine.h',
  'dsp/tests/unit/systems/atmosphere_engine_test.cpp',
  'dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp',
  'dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp',
];

const SCAN_DIRS = ['dsp', 'plugins', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// The banned spellings. Each is anchored so `detail::isNaN` (the house helper)
// and a member `isNan()` never match.
const BANNED = [
  { re: /\bstd::isnan\s*\(/, what: 'std::isnan' },
  { re: /\bstd::isinf\s*\(/, what: 'std::isinf' },
  { re: /\bstd::isfinite\s*\(/, what: 'std::isfinite' },
  { re: /\bnumeric_limits\s*<[^>]*>\s*::\s*infinity\s*\(/, what: 'numeric_limits::infinity()' },
  { re: /\bnumeric_limits\s*<[^>]*>\s*::\s*quiet_NaN\s*\(/, what: 'numeric_limits::quiet_NaN()' },
  { re: /\bnumeric_limits\s*<[^>]*>\s*::\s*signaling_NaN\s*\(/, what: 'numeric_limits::signaling_NaN()' },
  { re: /(^|[^A-Za-z0-9_])INFINITY([^A-Za-z0-9_]|$)/, what: 'INFINITY macro' },
  { re: /(^|[^A-Za-z0-9_])HUGE_VALF([^A-Za-z0-9_]|$)/, what: 'HUGE_VALF macro' },
];

// -----------------------------------------------------------------------------
// The exemption list: translation units built WITHOUT fast-math.
//
// Read from the CMake files rather than hard-coded, so a TU that is removed
// from the -fno-fast-math list immediately becomes subject to this lint instead
// of silently keeping an exemption it no longer has.
// -----------------------------------------------------------------------------
function collectFastMathExemptTus() {
  const exempt = new Set();
  const cmakeFiles = [];
  walk(REPO_ROOT, (p) => {
    if (path.basename(p) === 'CMakeLists.txt') cmakeFiles.push(p);
  }, new Set(['.txt']));

  for (const file of cmakeFiles) {
    const text = fs.readFileSync(file, 'utf8');
    // Any set_source_files_properties(...) block that mentions a no-fast-math
    // flag exempts every source file named in it.
    //
    // The block is delimited by a line that is JUST `)`, not by the first `)`
    // in the text: these blocks carry long explanatory comments, and those
    // comments contain parentheses ("(SC-013)", "(a)-(c)"), so a non-greedy
    // `\(...\)` match ends inside the first comment and sees none of the files.
    const blocks = [];
    const lines = text.split(/\r?\n/);
    for (let i = 0; i < lines.length; ++i) {
        if (!/set_source_files_properties\s*\(/.test(lines[i])) continue;
        const start = i;
        while (i < lines.length && !/^\s*\)\s*$/.test(lines[i])) ++i;
        blocks.push(lines.slice(start, i + 1).join('\n'));
    }
    for (const block of blocks) {
      if (!/-fno-fast-math|-fno-finite-math-only|\/fp:precise|\/fp:strict/.test(block)) continue;
      const names = block.match(/[\w./-]+\.(?:cpp|cc|mm|h|hpp)/g) || [];
      for (const n of names) exempt.add(path.basename(n));
    }
  }
  return exempt;
}

// -----------------------------------------------------------------------------
// Comment stripping. Block comments are tracked across lines so a banner
// paragraph cannot trip the lint.
// -----------------------------------------------------------------------------
function stripComments(lines) {
  const out = [];
  let inBlock = false;
  for (const raw of lines) {
    let line = raw;
    let cleaned = '';
    let i = 0;
    while (i < line.length) {
      if (inBlock) {
        const end = line.indexOf('*/', i);
        if (end === -1) { i = line.length; } else { i = end + 2; inBlock = false; }
        continue;
      }
      const lineComment = line.indexOf('//', i);
      const blockStart = line.indexOf('/*', i);
      if (blockStart !== -1 && (lineComment === -1 || blockStart < lineComment)) {
        cleaned += line.slice(i, blockStart);
        i = blockStart + 2;
        inBlock = true;
        continue;
      }
      if (lineComment !== -1) {
        cleaned += line.slice(i, lineComment);
        i = line.length;
        continue;
      }
      cleaned += line.slice(i);
      i = line.length;
    }
    out.push(cleaned);
  }
  return out;
}

function walk(dir, visit, exts) {
  let entries;
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of entries) {
    if (e.name.startsWith('.') && e.name !== '.github') continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) {
      if (SKIP_DIR.has(e.name)) continue;
      walk(p, visit, exts);
    } else if (exts.has(path.extname(e.name))) {
      visit(p);
    }
  }
}

function scan(file, exempt, violations) {
  if (exempt.has(path.basename(file))) return;
  let text;
  try { text = fs.readFileSync(file, 'utf8'); } catch { return; }
  const lines = text.split(/\r?\n/);
  const code = stripComments(lines);
  code.forEach((line, idx) => {
    for (const rule of BANNED) {
      if (rule.re.test(line)) {
        violations.push({
          file: path.relative(REPO_ROOT, file).replace(/\\/g, '/'),
          line: idx + 1,
          what: rule.what,
          text: lines[idx].trim(),
        });
      }
    }
  });
}

function main() {
  const surveyAll = process.argv.includes('--all');
  const exempt = collectFastMathExemptTus();
  const violations = [];

  if (surveyAll) {
    for (const dir of SCAN_DIRS) {
      walk(path.join(REPO_ROOT, dir), (f) => scan(f, exempt, violations), SOURCE_EXT);
    }
  } else {
    for (const rel of GUARDED) {
      const abs = path.join(REPO_ROOT, rel);
      if (!fs.existsSync(abs)) {
        console.error(`lint-nonfinite-symbols: guarded file is missing: ${rel}`);
        return 1;
      }
      scan(abs, exempt, violations);
    }
  }

  if (violations.length === 0) {
    console.log(
      surveyAll
        ? 'lint-nonfinite-symbols: all clear (whole-tree survey)'
        : `lint-nonfinite-symbols: all clear (${GUARDED.length} guarded files)`);
    return 0;
  }

  console.error('lint-nonfinite-symbols: FAILED\n');
  for (const v of violations) {
    console.error(`  ${v.file}:${v.line}  ${v.what}`);
    console.error(`      ${v.text}`);
  }
  console.error(`\n${violations.length} violation(s).`);
  console.error('These fold away under -ffast-math (the macOS leg) and /fp:fast.');
  console.error('Use the bit-pattern helpers in dsp/include/krate/dsp/core/db_utils.h');
  console.error('(detail::isNaN / detail::isInf), or add the TU to the -fno-fast-math');
  console.error('list in its CMakeLists.txt if it genuinely needs the std predicates.');
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('lint-nonfinite-symbols: internal error');
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(2);
}
