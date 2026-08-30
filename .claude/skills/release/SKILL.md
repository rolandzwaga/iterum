---
name: release
description: Guardrailed version bump + release commit for a single Krate Audio plugin. Edits ONLY version.json, adds the matching CHANGELOG entry, then commits — never pushes. Builds and runs that plugin's tests + pluginval only when the tree is not already verified. Use when the user asks to "release", "bump version", "cut a release", or "ship X.Y.Z" for a plugin.
allowed-tools: Read, Edit, Bash
---

# /release — Plugin Release Bump

Perform a clean, guardrailed release for **one** plugin. This exists because the version-bump
sequence has been botched repeatedly (editing generated files, forgetting the changelog, pushing
without permission). Follow every step; do not skip or reorder.

## Inputs

- **Plugin** (required): one of `iterum`, `disrumpo`, `ruinae`, `innexus`, `gradus`, `membrum`,
  `seraphis`.
  If not given in the invocation, ask which plugin — do not guess.
- **New version** (required): `X.Y.Z`. If not given, read the current `plugins/<plugin>/version.json`
  version and ask the user for the target (or confirm the intended semver bump). Do not invent it.

## Plugin → target/bundle map

| Plugin | Test target | pluginval bundle |
|--------|-------------|------------------|
| iterum | `plugin_tests` + `approval_tests` | `Iterum.vst3` |
| disrumpo | `disrumpo_tests` | `Disrumpo.vst3` |
| ruinae | `ruinae_tests` | `Ruinae.vst3` |
| innexus | `innexus_tests` | `Innexus.vst3` |
| gradus | `gradus_tests` | `Gradus.vst3` |
| membrum | `membrum_tests` | `Membrum.vst3` |
| seraphis | `seraphis_tests` | `Seraphis.vst3` |

## Steps (in order)

1. **Bump version.json ONLY.** Edit `plugins/<plugin>/version.json` — change the `"version"` field to
   the new `X.Y.Z`. **NEVER edit `version.h`** or any other generated file — it is produced from
   `version.json` at build time. Touch nothing else in this file.

2. **Add the CHANGELOG entry in the SAME change.** Prepend a new section to
   `plugins/<plugin>/CHANGELOG.md` directly above the most recent `## [` section:
   ```
   ## [X.Y.Z] - <today's date, YYYY-MM-DD>

   ### Added / Changed / Fixed
   - <concise bullets describing what shipped>
   ```
   Use only the Keep-a-Changelog subsections that apply. Use the real current date — do NOT
   hardcode a stale one.

   **Do NOT source the changes from `git log --oneline` alone.** That misses work two ways, and
   has already shipped an entry covering 3 of ~30 changes:

   - The baseline is not "the last few commits" or the last tag. It is the commit that **wrote
     the previous `## [X.Y.Z]` heading** — the last point at which the changelog was known
     complete. Anything merged after it and never released belongs in this entry.
   - **Squash merges erase the detail.** A PR that squashes 12 commits lands on `main` as one
     commit whose message describes none of them. Reading `git log` after the squash finds
     nothing to read, so the work goes unrecorded rather than merely unsummarised.

   Run the coverage tool, which resolves the real baseline and expands squash merges via `gh`:

   ```bash
   node tools/check-changelog-coverage.js <plugin>
   ```

   It exits **1** when `version.json` has no matching entry, or when commits exist in range and
   the entry has no bullets. Otherwise it prints every commit to reconcile — including the
   constituent commits of each squashed PR, and shared `dsp/` commits that can change this
   plugin's output without touching its directory.

   **Exit 0 does not mean the entry is complete** — no tool can judge prose. It means the range
   has been surfaced. Read each commit and confirm it is either covered by a bullet or genuinely
   not user-facing. When a commit's own message explains a defect well (measured numbers, the
   user-visible symptom), prefer that wording over paraphrasing from memory; write what a user
   would notice, not the work-item ID.

3. **Decide whether steps 4–6 are needed at all — usually they are NOT.**

   Steps 1 and 2 edit two text files. A `version.json` string and a Markdown entry **cannot break a
   build, a test, or pluginval.** Verification here is about the state of the tree you are tagging,
   not about the bump.

   **Skip straight to step 7 when the code at HEAD was already built, tested and validated** — which
   is the normal case, because the work being released was just finished and verified in the commits
   immediately preceding this one. Re-running the suite to "confirm the release" confirms nothing that
   the previous commits did not already establish, and a release bump is a routine, frequent action;
   the waste compounds.

   Run steps 4–6 only if one of these is actually true:
   - Code has changed since the last verified build (uncommitted work, a rebase, a merge, a fresh
     clone, or you simply do not know the tree's state).
   - You are releasing work you did not just verify yourself in this session.

   **Do not chase generated files.** `version.h` is produced from `version.json` by `configure_file`
   at CMake *configure* time, so an incremental build will not refresh it and a stale copy in a build
   directory is meaningless — it is gitignored and regenerates on the next real configure. Verifying
   that the new version "propagated" is never part of this task.

4. **Build.** (Full CMake path required on Windows.)
   ```bash
   CMAKE="/c/Program Files/CMake/bin/cmake.exe"
   "$CMAKE" --build build/windows-x64-release --config Release --target <test-target>
   ```
   Fix any compilation errors/warnings before proceeding. No tests without a clean build.

5. **Run that plugin's tests.** (For iterum, run BOTH `plugin_tests` and `approval_tests`.)
   ```bash
   build/windows-x64-release/bin/Release/<test-target>.exe 2>&1 | tail -5
   ```
   The last line must read "All tests passed". If anything fails, STOP and report — do not commit.

6. **Run pluginval.**
   ```bash
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/<Bundle>.vst3"
   ```
   Must pass. If the built bundle isn't present (post-build copy can fail on permissions), that copy
   failure is fine — validate the bundle in the build tree above.

7. **Stage exactly the two files** and commit — do NOT `git add -A`:
   ```bash
   git add plugins/<plugin>/version.json plugins/<plugin>/CHANGELOG.md
   git commit -m "chore(<plugin>): release X.Y.Z"
   ```
   (End the commit body with the standard `Co-Authored-By` trailer.)

8. **STOP. Never push.** Report the new version, the commit hash, and the test/pluginval results if
   steps 4–6 were run (say plainly that they were skipped as already-verified if they were not).
   Pushing requires explicit user permission every time.

## Guardrails (why this skill exists)

- A release bump is two text edits. Match the effort to that: when the code was just verified, edit,
  commit, stop. Building and re-testing to "confirm" a changelog entry is pure waste.
- version.json is the single source of truth; `version.h` is generated — editing it is always wrong,
  and checking whether it refreshed is equally beside the point.
- The changelog entry ships in the same commit as the bump, never split.
- Never push without explicit permission.
- Never bump on `main` if the workflow calls for a branch — verify the branch first.
