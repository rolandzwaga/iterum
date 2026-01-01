# CLAUDE.md - VST Plugin Development Guidelines

This file provides guidance for AI assistants working on this VST3 plugin project. All code contributions must comply with the project constitution at `.specify/memory/constitution.md`.

## Project Overview

This is a **VST3 plugin** built with:
- **Steinberg VST3 SDK** (not JUCE or other frameworks)
- **VSTGUI** for user interface
- **Modern C++20**
- **CMake 3.20+** build system

## Critical Rules (Non-Negotiable)

### Windows Path Workaround

For Edit/Glob/Grep/Read tools: Use Windows backslash paths (`C:\path\file.txt`). Expand `~` to full path (e.g., `C:\Users\name`). Does NOT apply to Bash.

### Cross-Platform Requirement

**This plugin MUST be fully cross-platform (Windows, macOS, Linux).** Platform-specific UI solutions are FORBIDDEN.

- NEVER use Win32 APIs, Cocoa/AppKit, or native popups for UI
- ALWAYS use VSTGUI's cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- If a VSTGUI feature doesn't work, the fix must also use VSTGUI
- Platform-specific code is ONLY acceptable for debug logging (guarded by `#ifdef`) or documented bug workarounds with user approval

### Build-Before-Test Discipline

**NO TESTS WITHOUT A CLEAN BUILD. PERIOD.**

After ANY code changes:
1. Build: `cmake --build build --config Release --target <target>`
2. Check for compilation errors
3. Fix errors BEFORE running tests
4. Only then run tests

If tests don't appear/run, the FIRST action is to check build output for errors, not blame CMake cache.

## Real-Time Audio Thread Safety

The audio thread (`Processor::process()`) has **hard real-time constraints**. FORBIDDEN:

```cpp
new / delete / malloc / free          // Memory allocation
std::vector::push_back()              // May reallocate
std::mutex / std::lock_guard          // Blocking synchronization
throw / catch                         // Exception handling
std::cout / printf / logging          // I/O operations
```

**ALWAYS:** Pre-allocate in `setupProcessing()`, use `std::atomic` with relaxed ordering, use lock-free SPSC queues.

## VST3 Architecture Separation

Processor and Controller are **separate components**:

```cpp
// Processor (audio thread) - src/processor/
class Processor : public Steinberg::Vst::AudioEffect { /* Audio ONLY */ };

// Controller (UI thread) - src/controller/
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
namespace Iterum::DSP {}           // Namespaces: PascalCase
enum { kBypassId = 0, kGainId };   // Parameter IDs: kNameId
```

### Parameter ID Naming Convention

All parameter IDs in `plugin_ids.h` MUST follow this pattern:

**Pattern:** `k{Mode}{Parameter}Id`

- `Mode`: The delay mode prefix (Granular, Spectral, Shimmer, Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze, Ducking)
- `Parameter`: The parameter name in PascalCase

**Standard Parameter Names (use these exact names):**

| Parameter | ID Suffix | Description |
|-----------|-----------|-------------|
| Delay Time | `DelayTimeId` | Main delay time in ms |
| Feedback | `FeedbackId` | Feedback amount (0-120%) |
| Mix | `MixId` | Dry/Wet mix (NOT "DryWet") |
| Time Mode | `TimeModeId` | Free/Synced selector |
| Note Value | `NoteValueId` | Tempo sync note value |
| Mod Depth | `ModDepthId` | Modulation depth (NOT "ModulationDepth") |
| Mod Rate | `ModRateId` | Modulation rate (NOT "ModulationRate") |
| Stereo Width | `StereoWidthId` | Stereo decorrelation amount |
| Width | `WidthId` | Pan width (use when NOT stereo decorrelation) |
| Age | `AgeId` | Component aging amount |
| Era | `EraId` | Era/model selector |
| Freeze | `FreezeId` | Freeze toggle |
| Filter Enabled | `FilterEnabledId` | Filter on/off toggle |
| Filter Cutoff | `FilterCutoffId` | Filter cutoff frequency |
| Filter Type | `FilterTypeId` | Filter type selector |
| Diffusion | `DiffusionId` | Diffusion amount |

**Compound Parameters:** Use descriptive sub-component names:
- `kTapeHead1EnabledId`, `kTapeHead1LevelId`, `kTapeHead1PanId`
- `kShimmerPitchSemitonesId`, `kShimmerPitchCentsId`
- `kSpectralFeedbackTiltId`

**AVOID:**
- Redundant prefixes: `kShimmerShimmerMixId` → `kShimmerMixId`
- Inconsistent abbreviations: Use `Mod` not `Modulation`
- Inconsistent terms: Use `Mix` not `DryWet`

### Modern C++ Requirements

Use smart pointers, RAII, constexpr, move semantics. Avoid raw `new`/`delete`.

## Layered DSP Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
└─────────────────────────────────────────────────────────────┘
```

**Rules:** Each layer can ONLY depend on layers below. No circular dependencies. Extract utilities to Layer 0 if used by 2+ Layer 1 primitives.

## File Organization

```
src/
├── entry.cpp, plugin_ids.h, version.h
├── processor/processor.{h,cpp}
├── controller/controller.{h,cpp}
└── dsp/
    ├── core/           # Layer 0
    ├── primitives/     # Layer 1
    ├── processors/     # Layer 2
    ├── systems/        # Layer 3
    └── features/       # Layer 4
```

## Common Patterns

### Adding a New Parameter

1. Add ID in `plugin_ids.h`
2. Add atomic in `processor.h`
3. Handle in `processParameterChanges()` in `processor.cpp`
4. Register in `Controller::initialize()` in `controller.cpp`
5. Add to state save/load
6. Add UI control in `editor.uidesc`

### ODR Prevention (CRITICAL)

Before creating ANY new class/struct, search codebase: `grep -r "class ClassName" src/`

Two classes with same name in same namespace = undefined behavior (garbage values, mysterious test failures). Check `dsp_utils.h` and ARCHITECTURE.md before adding components.

## DSP Implementation Rules

| Use Case | Interpolation |
|----------|---------------|
| Fixed delay in feedback | Allpass |
| LFO-modulated delay | Linear/Cubic (NOT allpass) |
| Pitch shifting | Lagrange/Sinc |

**Oversampling:** Required for nonlinear processing. 2x is practical limit.

**DC Blocking:** Apply ~5-20Hz highpass after asymmetric saturation.

**Feedback Safety:** Soft-limit with `std::tanh()` when feedback > 100%.

## Performance Budgets

| Component | CPU Target | Memory |
|-----------|------------|--------|
| Layer 1 primitive | < 0.1% | Minimal |
| Layer 2 processor | < 0.5% | Pre-allocated |
| Layer 3 system | < 1% | Fixed buffers |
| Full plugin | < 5% | 10s @ 192kHz max |

## Workflow Requirements (MANDATORY)

### Canonical Todo List for Implementation Tasks

```
1. [ ] Verify TESTING-GUIDE.md and VST-GUIDE.md are in context
2. [ ] Write failing test for feature/change
3. [ ] Implement to make test pass
4. [ ] Fix all compiler warnings
5. [ ] Verify all tests pass
6. [ ] Run pluginval (if plugin code changed)
7. [ ] Commit
```

For **bug fixes**, step 2 is "Write test reproducing the bug" and verify it fails before fixing.

### Pluginval

Run after any plugin source changes:
```bash
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
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

Before claiming spec complete:
1. Review EVERY FR-xxx and SC-xxx requirement
2. Fill compliance table with evidence
3. Self-check: No relaxed thresholds? No placeholders? No quietly removed features?

If requirements aren't met, be honest and document gaps.

## Framework Debugging (MANDATORY)

When VSTGUI/VST3 SDK doesn't work as expected:

1. **DO NOT** immediately try different approaches
2. **DO NOT** assume framework bug or switch to native code
3. **DO** investigate: read logs, trace values, read framework source
4. Before pivoting, complete: debug logs checked, values traced, source read, divergence point identified

**Before any VSTGUI/VST3 work:** Read `specs/VST-GUIDE.md` for documented pitfalls.

## Build Commands

```bash
cmake --preset windows-x64-debug        # Configure
cmake --build --preset windows-x64-debug # Build
ctest --preset windows-x64-debug         # Test
```

## Quick Reference

| Task | File(s) |
|------|---------|
| Add parameter | plugin_ids.h → processor → controller → uidesc |
| Add Layer N component | src/dsp/{layer}/ → tests/unit/{layer}/ |
| Change UI | resources/editor.uidesc |

| Your Layer | Can Include |
|------------|-------------|
| 0 (core/) | stdlib only |
| 1 (primitives/) | Layer 0 |
| 2 (processors/) | 0, 1 |
| 3 (systems/) | 0, 1, 2 |
| 4 (features/) | 0, 1, 2, 3 |

## References

- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VSTGUI Documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)
- Project Constitution: `.specify/memory/constitution.md`
- VST Pitfalls: `specs/VST-GUIDE.md`
- Testing Patterns: `specs/TESTING-GUIDE.md`
