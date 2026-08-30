#!/usr/bin/env node
//
// check-changelog-coverage.js
// =============================================================================
// Answers one question before a release: does the CHANGELOG entry for the
// version in version.json actually cover everything that landed since the
// PREVIOUS entry?
//
// Why this exists
// ---------------
// The obvious check -- "git log since the last release" -- is wrong twice over:
//
//   1. "Since the last release" is usually taken to mean the last tag or the
//      last few commits. The honest baseline is the commit that INTRODUCED the
//      previous `## [X.Y.Z]` heading, because that is the last point at which
//      the changelog was known to be complete. Work merged after it and never
//      released sits in the gap.
//
//   2. Squash merges erase the detail. A PR that squashes 12 commits appears on
//      main as one commit whose message says nothing about what changed. A
//      changelog written by reading `git log` after the squash has nothing left
//      to read, so the work becomes invisible rather than merely unrecorded.
//
// This is not a hypothetical. Innexus 1.0.2 was first written covering three
// changes; twelve commits of audit work (WI-1..WI-24, QS-1..QS-16) had been
// squashed into PR #262 and were silently unrecorded, including a crash fix and
// a defect that rewrote a 41 Hz bass note to a constant 708 Hz.
//
// So: resolve the baseline from the changelog itself, and expand squash merges
// back into their constituent commits via `gh` where it is available.
//
// Usage:
//   node tools/check-changelog-coverage.js innexus
//   node tools/check-changelog-coverage.js --all
//
// Exit codes:
//   0  entry exists and no commits are unaccounted for (or none in range)
//   1  no entry for the current version, or commits in range with an empty entry
//   2  usage / internal error
//
// Exit 0 is NOT a guarantee the prose is complete -- no tool can verify that.
// It means the range has been surfaced so a human can check it deliberately.
// =============================================================================

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO_ROOT = path.resolve(__dirname, '..');
const PLUGINS = ['iterum', 'disrumpo', 'ruinae', 'innexus', 'gradus', 'membrum', 'seraphis'];

function git(args, { allowFail = false } = {}) {
  try {
    return execFileSync('git', args, {
      cwd: REPO_ROOT,
      encoding: 'utf8',
      maxBuffer: 64 * 1024 * 1024,
    }).trim();
  } catch (err) {
    if (allowFail) return '';
    throw err;
  }
}

/// Expand a squash-merge commit back into the PR's own commits, when `gh` is
/// available and authenticated. Returns [] when it is not -- the caller then
/// reports the commit as opaque rather than pretending it saw inside.
function expandPullRequest(prNumber) {
  try {
    const out = execFileSync(
      'gh',
      ['pr', 'view', String(prNumber), '--json', 'commits', '--jq',
       '.commits[] | "\\(.oid[0:8]) \\(.messageHeadline)"'],
      { cwd: REPO_ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'],
        maxBuffer: 16 * 1024 * 1024 }
    );
    return out.trim().split('\n').filter(Boolean);
  } catch {
    return [];
  }
}

function parseChangelog(changelogPath) {
  const text = fs.readFileSync(changelogPath, 'utf8');
  const lines = text.split(/\r?\n/);

  // Ordered list of { version, headingLine, index }
  const entries = [];
  lines.forEach((line, i) => {
    const m = /^##\s*\[([^\]]+)\]/.exec(line);
    if (m) entries.push({ version: m[1], heading: line, index: i });
  });

  return { lines, entries };
}

/// Count the bullets belonging to a changelog entry (up to the next `## [`).
function countBullets(lines, entries, entryIdx) {
  const start = entries[entryIdx].index;
  const end = entryIdx + 1 < entries.length ? entries[entryIdx + 1].index : lines.length;
  let n = 0;
  for (let i = start + 1; i < end; i++) {
    if (/^\s*[-*]\s+\S/.test(lines[i])) n++;
  }
  return n;
}

/// The commit that first introduced `## [version]` into the changelog.
/// `git log -S` lists newest-first, so the introducing commit is the last one.
function commitIntroducingEntry(changelogRelPath, version) {
  const out = git(
    ['log', '--format=%H', '-S', `## [${version}]`, '--', changelogRelPath],
    { allowFail: true }
  );
  if (!out) return null;
  const all = out.split('\n').filter(Boolean);
  return all[all.length - 1] || null;
}

function checkPlugin(plugin) {
  const pluginDir = path.join(REPO_ROOT, 'plugins', plugin);
  const versionPath = path.join(pluginDir, 'version.json');
  const changelogPath = path.join(pluginDir, 'CHANGELOG.md');
  const changelogRel = `plugins/${plugin}/CHANGELOG.md`;

  if (!fs.existsSync(versionPath) || !fs.existsSync(changelogPath)) {
    console.error(`${plugin}: missing version.json or CHANGELOG.md`);
    return 2;
  }

  const version = JSON.parse(fs.readFileSync(versionPath, 'utf8')).version;
  const { lines, entries } = parseChangelog(changelogPath);

  if (entries.length === 0) {
    console.error(`${plugin}: CHANGELOG.md has no "## [x.y.z]" entries.`);
    return 1;
  }

  const currentIdx = entries.findIndex((e) => e.version === version);

  console.log(`\n=== ${plugin} (version.json: ${version}) ===`);

  if (currentIdx === -1) {
    console.error(
      `FAIL: no "## [${version}]" section in ${changelogRel}.\n` +
      `      version.json was bumped without a matching changelog entry.`
    );
    return 1;
  }

  // Baseline = the entry BELOW the current one. Everything merged since that
  // heading was written belongs in the current entry.
  const prev = entries[currentIdx + 1];
  if (!prev) {
    console.log('First changelog entry — nothing earlier to diff against.');
    return 0;
  }

  const base = commitIntroducingEntry(changelogRel, prev.version);
  if (!base) {
    console.log(
      `Could not locate the commit that introduced "## [${prev.version}]" ` +
      `(entry may predate the file's history). Skipping range check.`
    );
    return 0;
  }

  console.log(`Baseline: ${base.slice(0, 8)} — the commit that wrote "## [${prev.version}]"`);

  const range = `${base}..HEAD`;
  const raw = git(
    ['log', '--no-merges', '--format=%h\t%s', range, '--', `plugins/${plugin}/`],
    { allowFail: true }
  );
  const commits = raw ? raw.split('\n').filter(Boolean) : [];

  // Shared DSP moves independently of any one plugin but frequently carries
  // plugin-visible fixes (the Innexus audit lived largely in dsp/processors/).
  // Reported separately rather than merged in, because most dsp/ commits are
  // NOT relevant to a given plugin and folding them in would train the reader
  // to skim the list.
  const dspRaw = git(
    ['log', '--no-merges', '--format=%h\t%s', range, '--', 'dsp/'],
    { allowFail: true }
  );
  // Drop the ones already listed above -- a commit touching both the plugin and
  // dsp/ is one thing to reconcile, not two.
  const pluginShas = new Set(commits.map((l) => l.split('\t')[0]));
  const dspCommits = (dspRaw ? dspRaw.split('\n').filter(Boolean) : [])
    .filter((l) => !pluginShas.has(l.split('\t')[0]));

  const bullets = countBullets(lines, entries, currentIdx);
  console.log(`Entry "## [${version}]" has ${bullets} bullet(s).`);

  if (commits.length === 0 && dspCommits.length === 0) {
    console.log('No commits in range. Nothing to reconcile.');
    return 0;
  }

  const squashed = [];
  console.log(`\n${commits.length} commit(s) touching plugins/${plugin}/ since the baseline:`);
  for (const line of commits) {
    const [sha, subject] = line.split('\t');
    console.log(`  ${sha}  ${subject}`);
    const pr = /\(#(\d+)\)\s*$/.exec(subject || '');
    if (pr) squashed.push({ sha, subject, number: pr[1] });
  }

  if (dspCommits.length > 0) {
    console.log(
      `\n${dspCommits.length} further commit(s) touching shared dsp/ in the same ` +
      `range. Most will belong to other plugins — scan for any that are\n` +
      `${plugin}-visible (a shared-DSP fix can change this plugin's output ` +
      `without touching its directory):`
    );
    for (const line of dspCommits) {
      const [sha, subject] = line.split('\t');
      console.log(`  ${sha}  ${subject}`);
    }
  }

  // Squash merges are the trap: one opaque commit standing in for many.
  for (const s of squashed) {
    const inner = expandPullRequest(s.number);
    if (inner.length > 0) {
      console.log(
        `\n  ${s.sha} is a squash merge of PR #${s.number} — it stands in for ` +
        `${inner.length} commit(s):`
      );
      for (const c of inner) console.log(`      ${c}`);
    } else {
      console.log(
        `\n  ${s.sha} looks like a squash merge (PR #${s.number}), and its ` +
        `contents are NOT visible in git history.\n` +
        `      Could not expand it via gh (not installed, unauthenticated, or ` +
        `offline).\n` +
        `      Read the PR before trusting this list: ` +
        `gh pr view ${s.number} --json commits`
      );
    }
  }

  if (bullets === 0) {
    console.error(
      `\nFAIL: ${commits.length} commit(s) in range but "## [${version}]" has no bullets.`
    );
    return 1;
  }

  console.log(
    `\nReconcile each commit above against the ${bullets} bullet(s) in the entry.\n` +
    `A passing exit code means the range was surfaced, NOT that the prose is complete.`
  );
  return 0;
}

function main() {
  const args = process.argv.slice(2);
  if (args.length === 0) {
    console.error('usage: node tools/check-changelog-coverage.js <plugin>|--all');
    console.error(`       plugins: ${PLUGINS.join(', ')}`);
    process.exit(2);
  }

  const targets = args[0] === '--all' ? PLUGINS : [args[0]];
  for (const t of targets) {
    if (!PLUGINS.includes(t)) {
      console.error(`unknown plugin "${t}" (expected one of: ${PLUGINS.join(', ')})`);
      process.exit(2);
    }
  }

  let worst = 0;
  for (const t of targets) {
    const rc = checkPlugin(t);
    worst = Math.max(worst, rc);
  }
  process.exit(worst);
}

try {
  main();
} catch (err) {
  console.error('check-changelog-coverage: internal error:', err.message);
  process.exit(2);
}
