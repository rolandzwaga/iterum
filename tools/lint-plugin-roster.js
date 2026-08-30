#!/usr/bin/env node
// =============================================================================
// lint-plugin-roster.js — every plugin under plugins/ must appear in EVERY
// CI/tooling roster that names plugins one by one.
// =============================================================================
//
//   node tools/lint-plugin-roster.js     # exit 0 = fully covered, 1 = gaps
//
// WHY THIS EXISTS (it is a requirement, not a nicety):
//
// Every roster in this repo is a STATIC LITERAL — a `for plugin_info in \` list
// with a matching `case`, a `hashFiles(...)` argument list, a `ValidateSet`, a
// `PLUGINS = [...]` array. A missing entry never produces an error: the loop
// simply iterates over one fewer plugin and the job goes GREEN. The bundle
// validation loop in ci.yml is worse still — it does `[ -d "$b" ] && BUNDLES+=`,
// so even a listed-but-never-built bundle is dropped silently.
//
// The consequence is a passing CI run in which a whole plugin is never built,
// never tested and never validated. No compiler, no test suite, no pluginval
// pass and no clang-tidy sweep can detect that. This lint is the only artefact
// that can.
//
// The plugin roster itself is DERIVED FROM THE FILESYSTEM (readdir of plugins/,
// minus `shared`) — the same discovery tools/gen-repo-map.js:22-27 uses — so the
// lint cannot itself go stale when a plugin is added.
//
// Node only (project rule: helper scripts are Node, never Python).
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const PLUGINS_DIR = path.join(REPO_ROOT, 'plugins');

/** Collected `(plugin, file, site)` gaps — one output line each. */
const failures = [];
/** Structural problems (file/anchor missing) — the lint cannot judge, so it fails. */
const structural = [];

function addFailure(plugin, file, site) {
  failures.push(`${plugin}: ${file} — ${site}`);
}

function readFileOrNull(relPath) {
  const abs = path.join(REPO_ROOT, relPath);
  try {
    // Normalise CRLF -> LF: the workflow files are checked out with CRLF on
    // Windows, and every multi-line anchor below would silently miss otherwise
    // (a lint that cannot find its anchor is a lint that cannot fail). Line
    // counts are unchanged, so reported :NN offsets stay accurate.
    return fs.readFileSync(abs, 'utf8').replace(/\r\n/g, '\n');
  } catch {
    structural.push(`cannot read ${relPath} — roster file missing or unreadable`);
    return null;
  }
}

/** 1-based line number of a character offset, for actionable messages. */
function lineOf(text, index) {
  return text.slice(0, index).split('\n').length;
}

// --- Plugin roster: filesystem is the source of truth -------------------------
let pluginDirs;
try {
  pluginDirs = fs
    .readdirSync(PLUGINS_DIR, { withFileTypes: true })
    .filter((e) => e.isDirectory() && e.name !== 'shared')
    .map((e) => e.name)
    .sort();
} catch (err) {
  process.stderr.write(`lint-plugin-roster: cannot enumerate plugins/: ${err.message}\n`);
  process.exit(1);
}

// Directory names feed regexes below; refuse anything that is not a plain slug
// rather than building a regex out of it.
const plugins = pluginDirs.filter((p) => {
  if (/^[a-z0-9_-]+$/.test(p)) return true;
  structural.push(`plugins/${p}: directory name is not a plain lowercase slug — cannot lint`);
  return false;
});

/**
 * Test-executable targets a plugin declares, from its tests/CMakeLists.txt
 * (same extraction as gen-repo-map.js:33). Iterum's is `plugin_tests`, NOT
 * `iterum_tests`, so the valgrind roster check must not assume `${p}_tests`.
 */
function testTargets(plugin) {
  const rel = `plugins/${plugin}/tests/CMakeLists.txt`;
  let text;
  try {
    text = fs.readFileSync(path.join(REPO_ROOT, rel), 'utf8');
  } catch {
    return [`${plugin}_tests`];
  }
  const found = [...text.matchAll(/add_executable\(\s*([a-z0-9_]+_tests)\b/g)].map((m) => m[1]);
  return found.length > 0 ? [...new Set(found)] : [`${plugin}_tests`];
}

// =============================================================================
// 1. Root CMakeLists.txt — add_subdirectory(plugins/<p>)
// =============================================================================
{
  const file = 'CMakeLists.txt';
  const text = readFileOrNull(file);
  if (text !== null) {
    for (const p of plugins) {
      if (!text.includes(`add_subdirectory(plugins/${p})`)) {
        addFailure(p, file, `missing add_subdirectory(plugins/${p}) — plugin is never configured`);
      }
    }
  }
}

// =============================================================================
// 2. .github/workflows/ci.yml — nine per-plugin loops, three cache keys, and
//    the four change-detection sites.
// =============================================================================
{
  const file = '.github/workflows/ci.yml';
  const text = readFileOrNull(file);
  if (text !== null) {
    // -- detect-changes job outputs -------------------------------------------
    const outputNames = new Set(
      [...text.matchAll(/^ {6}([a-z0-9_]+): \$\{\{ steps\.set-outputs\.outputs\.[a-z0-9_]+ \}\}$/gm)].map(
        (m) => m[1]
      )
    );
    if (outputNames.size === 0) {
      structural.push(`${file}: no detect-changes outputs found — anchor changed`);
    } else {
      for (const p of plugins) {
        if (!outputNames.has(p)) {
          addFailure(p, file, `missing "${p}:" in the detect-changes job outputs block`);
        }
      }
    }

    // -- paths-filter `filters:` block ----------------------------------------
    const filtersMatch = text.match(/filters: \|\n([\s\S]*?)\n {6}- name:/);
    if (!filtersMatch) {
      structural.push(`${file}: paths-filter "filters: |" block not found — anchor changed`);
    } else {
      const filterNames = new Set([...filtersMatch[1].matchAll(/^ {12}([a-z0-9_]+):$/gm)].map((m) => m[1]));
      for (const p of plugins) {
        if (!filterNames.has(p)) {
          addFailure(p, file, `missing "${p}:" filter in the paths-filter filters: block`);
        }
      }
    }

    // -- `for p in iterum disrumpo …` release-dispatch loop --------------------
    const forPMatch = text.match(/for p in ([^;\n]+); do/);
    if (!forPMatch) {
      structural.push(`${file}: "for p in …; do" line not found — anchor changed`);
    } else {
      const words = new Set(forPMatch[1].trim().split(/\s+/));
      const forPLine = lineOf(text, forPMatch.index);
      for (const p of plugins) {
        if (!words.has(p)) {
          addFailure(p, file, `missing from the "for p in …" list at :${forPLine}`);
        }
      }
    }

    // -- $GITHUB_OUTPUT echo block --------------------------------------------
    const echoNames = new Set(
      [...text.matchAll(/echo "([a-z0-9_]+)=\$\{\{ steps\.filter\.outputs\.[a-z0-9_]+ \}\}"/g)].map((m) => m[1])
    );
    if (echoNames.size === 0) {
      structural.push(`${file}: no "$GITHUB_OUTPUT" filter echoes found — anchor changed`);
    } else {
      for (const p of plugins) {
        if (!echoNames.has(p)) {
          addFailure(p, file, `missing "${p}=\${{ steps.filter.outputs.${p} }}" echo in the $GITHUB_OUTPUT block`);
        }
      }
    }

    // -- three FetchContent hashFiles(...) cache keys --------------------------
    const hashLists = [...text.matchAll(/hashFiles\(([^)]*)\)/g)];
    if (hashLists.length !== 3) {
      structural.push(`${file}: expected 3 hashFiles(...) cache keys, found ${hashLists.length} — anchor changed`);
    }
    for (const m of hashLists) {
      const line = lineOf(text, m.index);
      for (const p of plugins) {
        if (!m[1].includes(`'plugins/${p}/CMakeLists.txt'`)) {
          addFailure(p, file, `missing 'plugins/${p}/CMakeLists.txt' from the FetchContent hashFiles() cache key at :${line}`);
        }
      }
    }

    // -- nine `for plugin_info in \` blocks (build / test / bundle × 3 OSes) ----
    // Each block is a literal list plus a `case` that maps the key to the
    // change-detection output. BOTH must name the plugin or it is skipped.
    const blockAnchors = [...text.matchAll(/for plugin_info in \\/g)];
    if (blockAnchors.length !== 9) {
      structural.push(
        `${file}: expected 9 "for plugin_info in \\" blocks, found ${blockAnchors.length} — anchor changed`
      );
    }
    for (const anchor of blockAnchors) {
      const start = anchor.index;
      const esac = text.indexOf('esac', start);
      if (esac < 0) {
        structural.push(`${file}: "for plugin_info in" block at :${lineOf(text, start)} has no closing esac`);
        continue;
      }
      const block = text.slice(start, esac);
      const line = lineOf(text, start);
      for (const p of plugins) {
        if (!block.includes(`"${p}:`)) {
          addFailure(p, file, `missing "${p}:…" entry from the per-plugin loop list at :${line}`);
        }
        if (!new RegExp(`^[ \\t]*${p}\\)`, 'm').test(block)) {
          addFailure(p, file, `missing "${p})" case arm in the per-plugin loop at :${line}`);
        }
      }
    }
  }
}

// =============================================================================
// 3. .github/workflows/release.yml — dispatch choice list + cache key
// =============================================================================
{
  const file = '.github/workflows/release.yml';
  const text = readFileOrNull(file);
  if (text !== null) {
    const optionsMatch = text.match(/type: choice[\s\S]*?options:\n([\s\S]*?)\n\s+default:/);
    if (!optionsMatch) {
      structural.push(`${file}: workflow_dispatch "options:" choice list not found — anchor changed`);
    } else {
      const options = new Set([...optionsMatch[1].matchAll(/^[ \t]*- ([a-z0-9_-]+)[ \t]*$/gm)].map((m) => m[1]));
      for (const p of plugins) {
        if (!options.has(p)) {
          addFailure(p, file, `missing "- ${p}" from the workflow_dispatch plugin choice list`);
        }
      }
    }

    const hashLists = [...text.matchAll(/hashFiles\(([^)]*)\)/g)];
    if (hashLists.length === 0) {
      structural.push(`${file}: no hashFiles(...) cache key found — anchor changed`);
    }
    for (const m of hashLists) {
      const line = lineOf(text, m.index);
      for (const p of plugins) {
        if (!m[1].includes(`'plugins/${p}/CMakeLists.txt'`)) {
          addFailure(p, file, `missing 'plugins/${p}/CMakeLists.txt' from the FetchContent hashFiles() cache key at :${line}`);
        }
      }
    }
  }
}

// =============================================================================
// 4. .github/workflows/valgrind-nightly.yml — editor-lifecycle build + run lists
// =============================================================================
{
  const file = '.github/workflows/valgrind-nightly.yml';
  const text = readFileOrNull(file);
  if (text !== null) {
    const buildMatch = text.match(/--parallel --target \\\n([^\n]*)\n/);
    const runMatch = text.match(/for bin in ([^;\n]+); do/);
    if (!buildMatch) {
      structural.push(`${file}: multi-target "--parallel --target \\" build line not found — anchor changed`);
    }
    if (!runMatch) {
      structural.push(`${file}: "for bin in …; do" line not found — anchor changed`);
    }
    const sites = [];
    if (buildMatch) sites.push({ list: buildMatch[1], what: 'editor-lifecycle build-target list', line: lineOf(text, buildMatch.index) });
    if (runMatch) sites.push({ list: runMatch[1], what: 'editor-lifecycle "for bin in …" run list', line: lineOf(text, runMatch.index) });

    for (const p of plugins) {
      const targets = testTargets(p);
      for (const site of sites) {
        const words = new Set(site.list.trim().split(/\s+/));
        if (!targets.some((t) => words.has(t))) {
          addFailure(p, file, `no test target (${targets.join(' | ')}) in the ${site.what} at :${site.line}`);
        }
      }
    }
  }
}

// =============================================================================
// 5. tools/run-clang-tidy.ps1 — ValidateSet, per-plugin case, "all" case
// =============================================================================
{
  const file = 'tools/run-clang-tidy.ps1';
  const text = readFileOrNull(file);
  if (text !== null) {
    const validateMatch = text.match(/\[ValidateSet\(([^)]*)\)\]/);
    if (!validateMatch) {
      structural.push(`${file}: [ValidateSet(...)] not found — anchor changed`);
    } else {
      for (const p of plugins) {
        if (!validateMatch[1].includes(`"${p}"`)) {
          addFailure(p, file, `missing "${p}" from the -Target ValidateSet`);
        }
      }
    }

    for (const p of plugins) {
      if (!new RegExp(`"${p}"\\s*\\{`).test(text)) {
        addFailure(p, file, `missing a \`"${p}" {\` switch case`);
      }
    }

    const allStart = text.indexOf('"all" {');
    if (allStart < 0) {
      structural.push(`${file}: \`"all" {\` switch case not found — anchor changed`);
    } else {
      const allEnd = text.indexOf('\n}', allStart);
      const allBlock = text.slice(allStart, allEnd < 0 ? text.length : allEnd);
      for (const p of plugins) {
        if (!allBlock.includes(`plugins/${p}/src`)) {
          addFailure(p, file, `missing plugins/${p}/src from the "all" switch case`);
        }
      }
    }
  }
}

// =============================================================================
// 6. tools/run-clang-tidy.sh — per-plugin case, all) case, usage text
// =============================================================================
{
  const file = 'tools/run-clang-tidy.sh';
  const text = readFileOrNull(file);
  if (text !== null) {
    for (const p of plugins) {
      if (!new RegExp(`^[ \\t]*${p}\\)`, 'm').test(text)) {
        addFailure(p, file, `missing a \`${p})\` case arm`);
      }
    }

    const allMatch = text.match(/^[ \t]*all\)/m);
    if (!allMatch) {
      structural.push(`${file}: \`all)\` case arm not found — anchor changed`);
    } else {
      const start = allMatch.index;
      const rest = text.slice(start);
      const endRel = rest.search(/\n\s*;;/);
      const allBlock = endRel < 0 ? rest : rest.slice(0, endRel);
      for (const p of plugins) {
        if (!allBlock.includes(`plugins/${p}/src`)) {
          addFailure(p, file, `missing plugins/${p}/src from the \`all)\` case arm`);
        }
      }
    }

    // Usage text: the --target help lines. A plugin the user cannot discover is
    // a plugin nobody lints locally.
    const usageMatch = text.match(/--target TARGET[\s\S]*?Default: all/);
    if (!usageMatch) {
      structural.push(`${file}: "--target TARGET … Default: all" usage text not found — anchor changed`);
    } else {
      for (const p of plugins) {
        if (!new RegExp(`\\b${p}\\b`).test(usageMatch[0])) {
          addFailure(p, file, `missing "${p}" from the --target usage text`);
        }
      }
    }
  }
}

// =============================================================================
// 7. tools/check-changelog-coverage.js — PLUGINS array literal
// =============================================================================
{
  const file = 'tools/check-changelog-coverage.js';
  const text = readFileOrNull(file);
  if (text !== null) {
    const arrayMatch = text.match(/const PLUGINS = \[([^\]]*)\]/);
    if (!arrayMatch) {
      structural.push(`${file}: "const PLUGINS = [...]" array literal not found — anchor changed`);
    } else {
      for (const p of plugins) {
        if (!arrayMatch[1].includes(`'${p}'`)) {
          addFailure(p, file, `missing '${p}' from the PLUGINS array`);
        }
      }
    }
  }
}

// =============================================================================
// Report
// =============================================================================
if (failures.length === 0 && structural.length === 0) {
  process.stdout.write(`lint-plugin-roster: OK — ${plugins.length} plugins present in every roster (${plugins.join(', ')})\n`);
  process.exit(0);
}

let msg = 'Plugin roster gaps — CI will go GREEN while skipping these plugins entirely:\n\n';
for (const line of failures) msg += `  ${line}\n`;
if (structural.length > 0) {
  msg += '\nStructural problems (a roster file changed shape — update this lint deliberately):\n\n';
  for (const line of structural) msg += `  ${line}\n`;
}
msg += `\n${failures.length} missing roster entr${failures.length === 1 ? 'y' : 'ies'}`;
msg += structural.length > 0 ? `, ${structural.length} structural problem(s).\n` : '.\n';
process.stderr.write(msg);
process.exit(1);
