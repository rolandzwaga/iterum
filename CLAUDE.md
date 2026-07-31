# CLAUDE.md - VST Plugin Development Guidelines

This file provides guidance for AI assistants working on this VST3 plugin project. All code contributions must comply with the project constitution at `.specify/memory/constitution.md`.

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Project Overview

This is a **monorepo** for Krate Audio plugins, featuring:
- **KrateDSP**: Shared DSP library at `dsp/` (namespace: `Krate::DSP`)
- **Iterum**: Delay plugin at `plugins/iterum/`
- **Disrumpo**: Multi-band distortion plugin at `plugins/disrumpo/`
- **Ruinae**: Synthesizer plugin at `plugins/ruinae/`
- **Innexus**: Harmonic analysis/resynthesis instrument at `plugins/innexus/` (AU type: `aumu`)
- **Gradus**: Standalone step arpeggiator at `plugins/gradus/` (AU type: `aumu`) — extracted from Ruinae's arp section, shares parameter IDs 3000-3372
- **Membrum**: Physically-modelled drum synthesizer at `plugins/membrum/` (AU type: `aumu`)
- **Seraphis**: Spectral-organism synthesizer at `plugins/seraphis/` (AU type: `aumu`, subtype `Srph`) — the current active-development plugin
- **Shared plugin infrastructure** at `plugins/shared/` (presets, UI components, MIDI, platform)
- **Steinberg VST3 SDK** (not JUCE or other frameworks)
- **VSTGUI** for user interface
- **Modern C++20**
- **CMake 3.20+** build system

### Monorepo Structure

The current roster is whatever lives under `plugins/` — **trust the filesystem, not a hand-maintained
tree here** (that is exactly where staleness accrues). Plugins today: `iterum`, `disrumpo`, `ruinae`,
`innexus`, `gradus`, `membrum`, `seraphis`, plus `shared`. The shared DSP library is `dsp/` (`Krate::DSP`, 5 layers
under `dsp/include/krate/dsp/{core,primitives,processors,systems,effects}/`).

- **Per-area detail** (skeleton, param-ID base, test target, pluginval path): the area `CLAUDE.md` leaf
  files (see below) and the generated maps under `specs/_architecture_/` (`repo-map.json`, layer/plugin
  reference docs).
- **Dev tooling** is in `tools/`; feature specs (numbered) in `specs/`; the VST3 SDK is vendored at
  `extern/vst3sdk/`. Other deps (pffft, Highway, dr_libs) are fetched/vendored — see External Dependencies.

## Per-Directory Context Files

This file holds cross-cutting rules. **Area-specific `CLAUDE.md` leaf files auto-load when you work in
their subtree** — read them for the concrete facts (skeleton, param-ID scheme, test target, pluginval path):

- [`dsp/CLAUDE.md`](dsp/CLAUDE.md) — layer architecture, ODR procedure, header-only/SIMD conventions
- [`plugins/iterum/CLAUDE.md`](plugins/iterum/CLAUDE.md) · [`disrumpo`](plugins/disrumpo/CLAUDE.md) · [`ruinae`](plugins/ruinae/CLAUDE.md) · [`innexus`](plugins/innexus/CLAUDE.md) · [`gradus`](plugins/gradus/CLAUDE.md) · [`membrum`](plugins/membrum/CLAUDE.md) · [`seraphis`](plugins/seraphis/CLAUDE.md)

## Critical Rules (Non-Negotiable)

### No Amending Commits

**NEVER use `git commit --amend`.** Always create a new commit. Amending is ONLY allowed when the user explicitly asks for it.

### Subagent Policy

**NEVER run Task agents in the background.** Always use `run_in_background: false` (or omit the parameter). The user needs to monitor agent progress in real-time. Background execution obscures what's happening and is forbidden.

**NEVER use subagents that require user permissions** (e.g., write/edit/bash actions that trigger permission prompts). Subagents that only perform read-only operations (file reads, searches, web fetches) are allowed and encouraged for parallelizing independent research tasks.

### Windows Path Workaround

For Edit/Glob/Grep/Read tools: Use Windows backslash paths (`C:\path\file.txt`). Expand `~` to full path (e.g., `C:\Users\name`). Does NOT apply to Bash.

### Cross-Platform Requirement

**This plugin MUST be fully cross-platform (Windows, macOS, Linux).** Platform-specific UI solutions are FORBIDDEN.

- NEVER use Win32 APIs, Cocoa/AppKit, or native popups for UI
- ALWAYS use VSTGUI's cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- If a VSTGUI feature doesn't work, the fix must also use VSTGUI
- Platform-specific code is ONLY acceptable for debug logging (guarded by `#ifdef`) or documented bug workarounds with user approval

### Use Node.js for Helper Scripts

**When generating helper scripts, CLI tools, or automation utilities, use Node.js — NOT Python.** This applies to any scripting needs such as code generators, data processing, build helpers, or dev tooling.

### Build-Before-Test Discipline

**NO TESTS WITHOUT A CLEAN BUILD. PERIOD.**

After ANY code changes:
1. Build: `cmake --build build --config Release --target <target>`
2. Check for compilation errors and warnings
3. Fix errors and warnings BEFORE running tests
4. Only then run tests

If tests don't appear/run, the FIRST action is to check build output for errors, not blame CMake cache.

## Real-Time Audio Thread Safety

The audio thread has **hard real-time constraints**. No allocations, locks, exceptions, or I/O on audio thread. See `dsp-architecture` skill for details.

## VST3 Architecture Separation

Processor and Controller are **separate components**:

```cpp
// Processor (audio thread) - plugins/{plugin}/src/processor/
class Processor : public Steinberg::Vst::AudioEffect { /* Audio ONLY */ };

// Controller (UI thread) - plugins/{plugin}/src/controller/
class Controller : public Steinberg::Vst::EditControllerEx1 { /* UI ONLY */ };
```

**Rules:** Never cross-include headers. Use `IMessage` for communication. Processor must work without controller. State flows: Host → Processor → Controller.

## Parameter Handling

All parameters at VST boundary are **normalized (0.0 to 1.0)**. Denormalize in `processParameterChanges()`.

## Cross-Platform Compatibility

See constitution section "Cross-Platform Compatibility" for complete reference. Key points:

**NaN Detection:** `-ffast-math` breaks `std::isnan()`. Use bit manipulation with `-fno-fast-math` on source file.

**Floating-Point:** MSVC/Clang differ at 7th-8th decimal. Use `Approx().margin()` in tests, `std::setprecision(6)` in approval tests.

**Denormals:** Enable FTZ/DAZ on x86: `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);`

**Narrowing:** Clang errors on narrowing in brace init. Use designated initializers: `BlockContext{.sampleRate = 44100.0, .tempoBPM = 120.0}`.

**SIMD:** SSE needs 16-byte alignment, AVX needs 32-byte. Use `alignas()` or `_mm_malloc`.

**Atomics:** Only `std::atomic_flag` is guaranteed lock-free. Verify with `is_lock_free()`.

## Code Style

### Naming Conventions

```cpp
class AudioProcessor {};           // Classes: PascalCase
void processAudio();               // Functions: camelCase
float sampleRate_;                 // Members: trailing underscore
constexpr float kDefaultGain;      // Constants: kPascalCase
namespace Krate::DSP {}            // Namespaces: PascalCase
enum { kBypassId = 0, kGainId };   // Parameter IDs: kNameId
```

### Parameter ID Naming Convention

Parameter IDs in `plugin_ids.h` follow `k{Prefix}{Parameter}Id` with canonical parameter
names (`Mix` not `DryWet`, `Mod` not `Modulation`). The full Iterum delay-mode table lives in
[`plugins/iterum/CLAUDE.md`](plugins/iterum/CLAUDE.md) and loads when working there.

### Modern C++ Requirements

Use smart pointers, RAII, constexpr, move semantics. Avoid raw `new`/`delete`.

## Layered DSP Architecture

5-layer architecture: Layer 0 (core) → Layer 1 (primitives) → Layer 2 (processors) → Layer 3 (systems) → Layer 4 (effects). Each layer can only depend on layers below. See `dsp-architecture` skill for details.

## File Organization

Each plugin follows the same skeleton — `src/{processor,controller,...}`, `tests/`, `resources/` — with
per-plugin deviations (e.g. Membrum has no `parameters/` dir; Ruinae has `engine/`; Innexus/Gradus/Disrumpo
have a plugin-local `dsp/`). **Read the area `CLAUDE.md` leaf for the exact skeleton** rather than a tree
here; `specs/_architecture_/plugin-architecture.md` has the full breakdown. DSP is layered under
`dsp/include/krate/dsp/{core,primitives,processors,systems,effects}/` with unit tests mirroring the layers.

**Include patterns:**
- DSP headers: `#include <krate/dsp/primitives/delay_line.h>`
- Plugin headers: `#include "processor/processor.h"`

## Common Patterns

### Adding a New Parameter

1. Add ID in `plugin_ids.h`
2. Add atomic in `processor.h`
3. Handle in `processParameterChanges()` in `processor.cpp`
4. Register in `Controller::initialize()` in `controller.cpp`
5. Add to state save/load
6. Add UI control in `editor.uidesc`

### ODR Prevention (CRITICAL)

Before creating ANY new class/struct, search codebase:
```bash
grep -r "class ClassName" dsp/ plugins/
```

Two classes with same name in same namespace = undefined behavior (garbage values, mysterious test failures). Check `dsp_utils.h` and `specs/_architecture_/` before adding components.

## External Dependencies

### pffft (SIMD-Optimized FFT)

The `FFT` class (`dsp/include/krate/dsp/primitives/fft.h`) uses **pffft** ([marton78 fork](https://github.com/marton78/pffft), BSD license) as its backend. pffft is **fetched via FetchContent** (root `CMakeLists.txt`, ~L342-360) and lands in `build/_deps/` — it is **not** vendored in `extern/`.

- **SIMD auto-detection**: SSE on x86/x64 (including MSVC via `_mm_*` intrinsics), NEON on ARM, scalar fallback
- **Build integration**: pffft is a static C library linked PUBLIC to KrateDSP (see `dsp/CMakeLists.txt`)
- **Public API unchanged**: The `Complex` struct and `FFT::forward()`/`inverse()` signatures are the same for all consumers (STFT, OverlapAdd, spectral processors, additive oscillator, etc.)
- **pffft output format**: For real transforms, ordered output is `[DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]` — conversion to/from our `Complex[N/2+1]` format happens inside `fft.h`
- **Inverse normalization**: pffft does NOT normalize the inverse transform; `FFT::inverse()` applies `1/N` scaling

### Google Highway (SIMD-Accelerated Math)

KrateDSP uses [Google Highway](https://github.com/google/highway) (v1.2.0, Apache-2.0) for SIMD-accelerated spectral math. Fetched via FetchContent in root `CMakeLists.txt`.

- **Runtime dispatch**: Automatically uses best available ISA (SSE2/AVX2/AVX-512 on x86, NEON on ARM)
- **Build integration**: Linked PRIVATE to KrateDSP — no Highway headers in public API
- **Used for**: `spectral_simd.cpp` and related spectral processing internals

### dr_libs (WAV loading)

`dr_wav` (from [dr_libs](https://github.com/mackron/dr_libs), public domain) is a single-header WAV
loader **vendored** at `extern/dr_libs/`. Used by Innexus's `sample_analyzer` (`plugins/innexus/CMakeLists.txt`
adds it as an include dir) and the `tools/membrum-fit` offline drum-sample fitter. It is a build-time
dependency of those two consumers only — not part of the shared KrateDSP public API.

## DSP Implementation Rules

See `dsp-architecture` skill for interpolation selection, oversampling, DC blocking, feedback safety, and performance budgets.

## Workflow Requirements (MANDATORY)

### Canonical Todo List for Implementation Tasks

```
1. [ ] Write failing test for feature/change (skills auto-load: testing-guide, vst-guide)
2. [ ] Implement to make test pass
3. [ ] Fix all compiler warnings
4. [ ] Verify all tests pass
5. [ ] Run pluginval (if plugin code changed)
6. [ ] Run clang-tidy (./tools/run-clang-tidy.ps1 or .sh)
7. [ ] Commit
```

For **bug fixes**, step 1 is "Write test reproducing the bug" and verify it fails before fixing.

### Pluginval

Run after any plugin source changes:
```bash
# Iterum
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"

# Disrumpo
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"

# Ruinae
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"

# Innexus
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"

# Gradus
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Gradus.vst3"

# Membrum
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"

# Seraphis
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

Skip for docs-only, CI config, or test-only changes.

### Compiler Warnings

All code must compile with ZERO warnings. Common fixes:

| Warning | Fix |
|---------|-----|
| C4244 (double→float) | Add `f` suffix |
| C4267 (size_t→int) | Explicit cast |
| C4100 (unused param) | `[[maybe_unused]]` |

## Completion Honesty (MANDATORY)

**DO NOT fill the compliance table from memory or assumptions.** Every single row must be verified against actual code and test output.

Before claiming spec complete:
1. **For each FR-xxx**: Open the implementation file(s), read the relevant code, and confirm the requirement is met. Cite the file and line number.
2. **For each SC-xxx**: Run the specific test or measurement. Copy the actual output. Compare it against the spec threshold. Do not paraphrase — use real numbers.
3. **Fill the compliance table with this concrete evidence** (file paths, line numbers, test names, actual measured values). Generic claims like "implemented" or "test passes" without specifics are NOT acceptable.
4. **Self-check**: No relaxed thresholds? No placeholders? No quietly removed features? No ✅ without having just now verified the code/test?

If requirements aren't met, be honest and document gaps. A table full of ✅ that wasn't individually verified is worse than an honest ❌.

## Framework Debugging (MANDATORY)

When VSTGUI/VST3 SDK doesn't work as expected:

1. **DO NOT** immediately try different approaches
2. **DO NOT** assume framework bug or switch to native code
3. **DO** investigate: read logs, trace values, read framework source
4. Before pivoting, complete: debug logs checked, values traced, source read, divergence point identified

The `vst-guide` skill auto-loads with documented pitfalls and patterns.

## Build Commands

**CRITICAL: On Windows, the Python cmake wrapper in PATH does not work. You MUST use the full path to CMake:**

```bash
# Set alias for convenience (or use full path every time)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (Release)
"$CMAKE" --preset windows-x64-release

# Build (Release)
"$CMAKE" --build build/windows-x64-release --config Release

# Run DSP tests (split into 5 per-layer executables: core/primitives/processors/systems/effects)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
# (Editing one layer only relinks that layer's exe — e.g. --target dsp_core_tests)

# Run plugin-specific tests
"$CMAKE" --build build/windows-x64-release --config Release --target plugin_tests    # Iterum
"$CMAKE" --build build/windows-x64-release --config Release --target approval_tests  # Iterum golden-output approvals (see note)
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests  # Disrumpo
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests    # Ruinae
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests   # Innexus
"$CMAKE" --build build/windows-x64-release --config Release --target gradus_tests    # Gradus
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests   # Membrum
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests  # Seraphis
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests    # Shared infra

# Iterum has a SECOND, generically-named test target: `approval_tests` holds the
# golden-reference approval tests. CI builds + runs it for Iterum. Run it for ANY
# Iterum DSP/output change (running only `plugin_tests` silently skips golden-output
# regression coverage):
build/windows-x64-release/bin/Release/approval_tests.exe 2>&1 | tail -5

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure
# NOTE: to run ONE suite, invoke its exe directly (bin/Release/<name>.exe) — do NOT use
# `ctest -R <exe>`. catch_discover_tests registers individual Catch2 CASE names, not
# executable names, so `ctest -R dsp_core_tests` matches nothing and reports success.

# Debug build (same pattern)
"$CMAKE" --preset windows-x64-debug
"$CMAKE" --build build/windows-x64-debug --config Debug
```

**Note:** The plugin build may fail on the post-build copy step (permission error copying to `C:/Program Files/Common Files/VST3/`). This is fine - the actual compilation succeeded. Built plugins are at:
- `build/windows-x64-release/VST3/Release/Iterum.vst3/`
- `build/windows-x64-release/VST3/Release/Disrumpo.vst3/`
- `build/windows-x64-release/VST3/Release/Ruinae.vst3/`
- `build/windows-x64-release/VST3/Release/Innexus.vst3/`
- `build/windows-x64-release/VST3/Release/Gradus.vst3/`
- `build/windows-x64-release/VST3/Release/Membrum.vst3/`
- `build/windows-x64-release/VST3/Release/Seraphis.vst3/`

### AddressSanitizer (ASan)

Use ASan to detect memory errors (use-after-free, buffer overflows, etc.) that cause crashes but don't throw exceptions:

```bash
# Configure with ASan enabled
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON

# Build (use Debug for better stack traces)
cmake --build build-asan --config Debug

# Run tests - ASan will abort and report if memory errors occur
ctest --test-dir build-asan -C Debug --output-on-failure
```

**When to use ASan:**
- Investigating crashes that don't have clear stack traces
- After fixing editor lifecycle bugs (to verify no use-after-free)
- Before releases to catch latent memory issues

**Note:** ASan increases memory usage and slows execution. Use a separate build directory.

### Clang-Tidy (Static Analysis)

Run before every commit (canonical todo list step 6) and after significant refactoring:

```powershell
./tools/run-clang-tidy.ps1 -Target <all|dsp|shared|iterum|disrumpo|ruinae|innexus|gradus|membrum|seraphis> -BuildDir build/windows-ninja
# Linux/macOS: ./tools/run-clang-tidy.sh --target <same roster>
```

**`all` MUST cover dsp + every plugin** — a new plugin needs its case added to BOTH scripts, or the
Linux/macOS pre-commit lint silently skips it. One-time environment setup (LLVM/Ninja install,
compile_commands.json generation) and the full flag reference: `clang-tidy-setup` skill.

## Quick Reference

| Task | File(s) |
|------|---------|
| Add Iterum parameter | plugins/iterum/src/plugin_ids.h → parameters/ → processor → controller → uidesc |
| Add Disrumpo parameter | plugins/disrumpo/src/plugin_ids.h → processor → controller → uidesc |
| Add Ruinae parameter | plugins/ruinae/src/plugin_ids.h → parameters/ → processor → controller → uidesc |
| Add Innexus parameter | plugins/innexus/src/plugin_ids.h → parameters/ → processor → controller → uidesc |
| Add Gradus parameter | plugins/gradus/src/plugin_ids.h → parameters/ → processor → controller → uidesc |
| Add Membrum parameter | plugins/membrum/src/plugin_ids.h → processor → controller → uidesc (no `parameters/` dir) |
| Add Seraphis parameter | plugins/seraphis/src/plugin_ids.h → parameters/ → processor → controller → uidesc |
| Add DSP component | dsp/include/krate/dsp/{layer}/ → dsp/tests/unit/{layer}/ |
| Add Iterum test | plugins/iterum/tests/unit/{section}/ |
| Add Disrumpo test | plugins/disrumpo/tests/ |
| Add Ruinae test | plugins/ruinae/tests/unit/ |
| Add Innexus unit test | plugins/innexus/tests/unit/{processor,vst}/ |
| Add Innexus integration test | plugins/innexus/tests/integration/ |
| Add Gradus test | plugins/gradus/tests/unit/ |
| Add Membrum test | plugins/membrum/tests/ |
| Add Seraphis test | plugins/seraphis/tests/unit/ (or tests/integration/) |
| Add shared component | plugins/shared/src/{section}/ → plugins/shared/tests/ |
| Change Iterum UI | plugins/iterum/resources/editor.uidesc |
| Change Disrumpo UI | plugins/disrumpo/resources/editor.uidesc |
| Change Gradus UI | plugins/gradus/resources/editor.uidesc |
| Change Membrum UI | plugins/membrum/resources/editor.uidesc |
| Change Seraphis UI | plugins/seraphis/resources/editor.uidesc |

| Your Layer | Location | Can Include |
|------------|----------|-------------|
| 0 (core/) | dsp/include/krate/dsp/core/ | stdlib only |
| 1 (primitives/) | dsp/include/krate/dsp/primitives/ | Layer 0 |
| 2 (processors/) | dsp/include/krate/dsp/processors/ | 0, 1 |
| 3 (systems/) | dsp/include/krate/dsp/systems/ | 0, 1, 2 |
| 4 (effects/) | dsp/include/krate/dsp/effects/ | 0, 1, 2, 3 |

## References

- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VSTGUI Documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)
- Project Constitution: `.specify/memory/constitution.md`

**Skills (auto-load when relevant):**
- `.claude/skills/claude-file/` - Display project CLAUDE.md with all development guidelines
- `.claude/skills/code-review/` - DSP & VST3 specialized code review (real-time safety, thread safety, numerical stability)
- `.claude/skills/testing-guide/` - Broad testing: build-before-test, Catch2 patterns, integration, VST3 validation, anti-patterns
- `.claude/skills/testing-dsp-analysis/` - Deep DSP verification: FFT aliasing, THD/SNR, artifact detection, spectral goldens
- `.claude/skills/vst-guide/` - VST3/VSTGUI patterns, thread safety, UI components
- `.claude/skills/dsp-architecture/` - Real-time safety, layers, interpolation, performance
