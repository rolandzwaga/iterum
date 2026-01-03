<!-- SYNC: v1.11.0→1.12.0 | VII: Monorepo layout | VIII: DSP path | XV: ODR search path -->

# VST Plugin Development Constitution

**Version**: 1.12.0 | **Ratified**: 2025-12-21 | **Last Amended**: 2026-01-03

---

## Core Principles

### I. VST3 Architecture Separation

**Non-Negotiable Rules:**
- Processor and Controller MUST be separate classes; set `kDistributable` flag when they can run in different processes
- Use `IMessage`, `IConnectionPoint`, `IAttributeList` for all processor-controller communication
- Processor MUST function correctly without controller instantiation
- State synchronization via `setComponentState()` - controller syncs to processor, never reverse
- NEVER send raw pointers via IMessage

### II. Real-Time Audio Thread Safety

**Non-Negotiable Rules:**
- NEVER allocate memory in audio callbacks (`malloc`, `new`, or allocating STL operations)
- NEVER use locks, mutexes, or blocking primitives in audio thread
- NEVER perform file I/O, network ops, or system calls in audio thread
- NEVER throw/catch exceptions in audio thread
- Pre-allocate ALL buffers in `initialize()` or `setupProcessing()` before `setActive(true)`
- Audio callbacks MUST complete within buffer duration (1-10ms typical)
- Use lock-free SPSC queues for inter-thread communication

### III. Modern C++ Standards

**Non-Negotiable Rules:**
- Target C++17 minimum; prefer C++20 where SDK-compatible
- Use RAII for ALL resource management
- Prefer `std::unique_ptr`; use `std::shared_ptr` only when genuinely needed
- NEVER use raw `new`/`delete` - wrap in smart pointers immediately
- Use `constexpr` and `const` aggressively
- Prefer `std::array` over C-style arrays; `std::span` for views (C++20)

### IV. SIMD & DSP Optimization

**Non-Negotiable Rules:**
- Align audio buffers to 16-byte (SSE) or 32-byte (AVX) boundaries
- Process audio in contiguous, sequential memory patterns
- Minimize branching in inner loops; prefer branchless algorithms
- Avoid virtual function calls in tight processing loops
- Profile before optimizing - measure actual bottlenecks

### V. VSTGUI Development

**Non-Negotiable Rules:**
- Use `UIDescription` XML and WYSIWYG editor for layout
- Implement `VST3EditorDelegate` for custom view creation
- UI thread MUST NEVER directly access audio processing data
- Use `IParameterChanges` for UI→Processor updates
- All parameter values normalized (0.0-1.0) at VST boundary

### VI. Cross-Platform Compatibility (CRITICAL)

**Non-Negotiable Rules:**
- NEVER use Windows-native APIs (Win32, COM) or macOS-native APIs (Cocoa, AppKit) for UI
- ALWAYS use VSTGUI cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- Platform-specific code ONLY for: debug logging (guarded), optimizations with fallbacks, documented bug workarounds
- ANY platform-specific solution requires explicit user approval
- CI/CD MUST build and test on all three platforms

### VII. Project Structure & Build System

**Non-Negotiable Rules:**
- Use Modern CMake (3.20+) with target-based configuration
- Directory structure MUST follow monorepo layout:
  ```
  dsp/                          # Shared KrateDSP library (Krate::DSP namespace)
  ├── include/krate/dsp/        # Public headers (use <krate/dsp/...>)
  │   ├── core/                 # Layer 0
  │   ├── primitives/           # Layer 1
  │   ├── processors/           # Layer 2
  │   ├── systems/              # Layer 3
  │   └── effects/              # Layer 4
  └── tests/                    # DSP unit tests

  plugins/<plugin>/             # Individual plugins (e.g., plugins/iterum/)
  ├── src/                      # Plugin source
  ├── tests/                    # Plugin tests
  └── resources/                # UI resources, installers
  ```
- DSP: angle bracket includes `#include <krate/dsp/...>`
- Plugin: relative includes `#include "processor/processor.h"`
- External deps via git submodules in `extern/` with pinned versions
- Must build on Windows (MSVC), macOS (Clang), Linux (GCC)

### VIII. Testing Discipline

**Non-Negotiable Rules:**
- DSP algorithms in `dsp/include/krate/dsp/` MUST be pure functions testable without VST infrastructure
- Unit tests MUST cover all DSP algorithms with known input/output pairs
- Integration tests MUST verify plugin loads correctly in test host
- Tests MUST run in CI/CD on every commit
- NEVER commit code that breaks existing tests

---

## Technical Constraints

### SDK & Platform Requirements

| Component | Version |
|-----------|---------|
| VST3 SDK | 3.7.x+ (git submodule) |
| VSTGUI | 4.12+ |
| C++ | C++17 min, C++20 preferred |
| CMake | 3.20+ |

| Platform | Compiler | Status |
|----------|----------|--------|
| Windows 10/11 | MSVC 2019+ | Required |
| macOS 11+ | Clang/Xcode 13+ | Required |
| Linux | GCC 10+ / Clang 12+ | Optional |

### Performance Targets

| Metric | Target |
|--------|--------|
| Audio callback | < 50% buffer time |
| Memory alloc in audio | 0 |
| Plugin scan time | < 500ms |

### Forbidden Patterns

`new`/`delete` without smart pointer • `malloc`/`free` in audio • `std::mutex` in audio thread • `vector::push_back` in audio • `dynamic_cast` in audio path • `throw`/`catch` in audio • Global mutable state without sync • Busy-wait spinlocks

### Cross-Platform Compatibility

#### Floating-Point

| Issue | Solution |
|-------|----------|
| `-ffast-math` breaks `std::isnan()` | Use bit manipulation + compile with `-fno-fast-math -fno-finite-math-only` |
| MSVC/Clang precision differs at 7th decimal | Use ≤6 decimal places in tests; use `Approx().margin()` |
| Denormals cause 100x CPU slowdown | Enable FTZ/DAZ in audio thread |
| `std::pow/log10/exp` not constexpr in MSVC | Use custom Taylor series |

#### SIMD & Alignment

| Issue | Solution |
|-------|----------|
| SSE/AVX alignment | Use `alignas(32)` for buffers; `_mm_malloc`/`_mm_free` for dynamic |
| x86 vs ARM | Use SIMD abstraction or separate code paths |
| Apple Silicon | Build universal binaries (x86_64 + arm64) |

#### Threading

| Issue | Solution |
|-------|----------|
| `std::atomic<T>::is_lock_free()` varies | Only `std::atomic_flag` guaranteed lock-free; verify others |
| ARM weak memory model | Use `acquire`/`release` not `seq_cst` |
| Thread priority | Let host manage; don't set manually |

#### Build System

| Issue | Solution |
|-------|----------|
| macOS requires Xcode generator | Use `-G Xcode` for Objective-C++ support |
| VST3 ABI is compiler-dependent | Use recommended compiler per platform |
| Windows UTF-16 paths | Use UTF-8 internally; convert for Windows APIs |

---

## Development Workflow

### Code Review Requirements

1. Constitution compliance verified
2. Thread safety audited for audio code
3. No new allocations in audio paths
4. New DSP code includes unit tests
5. CI passes on all required platforms

### Profiling Protocol

1. Profile in Release with representative audio
2. Identify bottlenecks with CPU profiler
3. Document baseline before changes
4. Verify improvement with same methodology

---

## Advanced Principles

### IX. Layered DSP Architecture

**Non-Negotiable Rules:**
- Layer 0 (Core): Memory pools, fast math, SIMD - NO deps on higher layers
- Layer 1 (Primitives): Delay lines, LFOs, biquads - depend ONLY on Layer 0
- Layer 2 (Processors): Filters, saturators, pitch shifters - compose from 0-1
- Layer 3 (Systems): Delay engine, feedback network - compose from 0-2
- Layer 4 (Features): Complete modes (Tape, BBD) - compose from 0-3
- NEVER circular dependencies; NEVER skip layers
- Each layer independently testable

### X. DSP Processing Constraints

**Non-Negotiable Rules:**
- All timing-critical ops at sample granularity
- Oversampling (min 2x) for saturation/distortion/waveshaping
- DC blocking (~5-20Hz highpass) after asymmetric saturation
- Interpolation: Allpass for fixed feedback delays; Linear for modulated; Cubic for pitch
- TDF2 for floating-point biquads; validate stability at extremes
- Feedback >100% MUST include soft limiting
- FFT: power-of-2 sizes, proper windowing, maintain COLA

### XI. Performance Budgets

| Component | CPU Target |
|-----------|------------|
| Total plugin | < 5% single core @ 44.1kHz stereo |
| Layer 1 primitive | < 0.1% per instance |
| Layer 2 processor | < 0.5% per instance |
| Layer 3 system | < 1% per instance |
| Max delay buffer | 10s @ 192kHz (1.92M samples) |

### XII. Debugging Discipline (Anti-Pivot)

**Non-Negotiable Rules:**
- **Debug Before Pivot**: When code doesn't work:
  1. Read ALL debug logs
  2. Add logging to trace values
  3. Read framework source code
  4. Identify EXACT divergence point
  5. ONLY then consider alternatives

- **Framework Commitment**: The framework works in thousands of plugins. If it's not working, YOU are doing something wrong. Your job is to find WHAT, not abandon the framework.

- **Forbidden Patterns**: "Let me try something else" without investigating WHY • "Maybe framework bug" • Trying 3+ approaches without reading debug output

### XIII. Test-First Development

**Non-Negotiable Rules:**
- **Context Verification**: Before ANY implementation, verify `specs/TESTING-GUIDE.md` and `specs/VST-GUIDE.md` are in context
- **Test Before Implementation**: Write failing test FIRST, then implement
- **Bug-First Testing**: Reproduce bug in test → verify fails → fix → verify passes
- **Explicit Todo Items**:
  1. Verify guides in context
  2. Write failing test
  3. Implement to make test pass
  4. Verify all tests pass
  5. Commit

### XIV. Living Architecture Documentation

**Non-Negotiable Rules:**
- `ARCHITECTURE.md` MUST exist at repository root
- Every spec implementation MUST update it as final task
- Organized by DSP layer (0-4), not chronologically
- Each component: purpose, API, location, "when to use"

### XV. Pre-Implementation Research (ODR Prevention)

**Non-Negotiable Rules:**
- **Search Before Creating**: `grep -r "class ClassName" dsp/ plugins/`
- **Check ARCHITECTURE.md** for existing components
- Same namespace + same name = undefined behavior (ODR violation)
- Symptoms: garbage values, uninitialized-looking data, mysterious test failures

**First action for strange failures**: Search for duplicate class definitions before debugging logic.

### XVI. Honest Completion (Anti-Cheating)

**Non-Negotiable Rules:**
- **Definition of "Done"**: ALL acceptance criteria met, tests at spec thresholds, no placeholders/TODOs, performance targets measured
- **Forbidden Patterns**:
  - Relaxing test thresholds to pass
  - Placeholder values marked "needs proper design"
  - Removing scope without declaration
  - Saying "tests pass" when tests were weakened

- **Mandatory Verification**: Before claiming complete, review EVERY FR-xxx and SC-xxx. If ANY not met, spec is NOT complete.

- **Compliance Table Format**:
  | Requirement | Status | Evidence |
  |-------------|--------|----------|
  | FR-001 | ✅ MET | Test X |
  | FR-002 | ❌ NOT MET | Reason |

### XVII. Framework Knowledge Documentation

**Non-Negotiable Rules:**
- **Read Before Working**: Check `specs/VST-GUIDE.md` before VSTGUI/VST3 work
- **Document Discoveries**: Log non-obvious framework behavior immediately after fix
- **Incident Log**: Date, symptom, root cause, solution, time wasted, lesson

### XVIII. Spec Numbering

**Non-Negotiable Rules:**
- **NEVER check branches** for spec numbering (branches deleted after merge)
- **ALWAYS check specs/ directory**: `ls specs/ | grep -E '^[0-9]+' | sed 's/-.*//' | sort -n | tail -1`
- New spec = highest + 1

---

## Governance

### Constitution Authority

This constitution is supreme for development decisions. In conflicts with other docs, constitution prevails.

### Amendment Process

1. Propose with rationale
2. Review codebase impact
3. Update version (MAJOR: removal/incompatible change, MINOR: new principle, PATCH: clarification)
4. Update dependent templates if affected

### Exception Process

If principle cannot be followed:
1. Document constraint
2. Propose minimal deviation
3. Include timeline if temporary
4. Record in Complexity Tracking
