<!--
================================================================================
SYNC IMPACT REPORT
================================================================================
Version Change: 1.4.0 → 1.5.0
Modified Principles: None
Added Sections:
  - Cross-Platform Compatibility significantly expanded with:
    - Denormalized numbers (FTZ/DAZ)
    - Signed integer overflow
    - SIMD alignment requirements
    - Cross-platform SIMD (SSE/AVX/NEON)
    - Apple Silicon considerations
    - Thread priority mechanisms
    - Atomic operations and lock-freedom
    - Memory ordering (x86 vs ARM)
    - Spinlock warnings
    - ABI compatibility
    - Runtime libraries
    - File path encoding (UTF-8/UTF-16)
    - State persistence (endianness)
    - Plugin validation (pluginval)
Removed Sections: None
Templates Requiring Updates: None
Follow-up TODOs: None
================================================================================
-->

# VST Plugin Development Constitution

## Core Principles

### I. VST3 Architecture Separation

The VST3 SDK mandates strict separation between audio processing and user interface components. This separation MUST be maintained at all times.

**Non-Negotiable Rules:**
- Processor (IAudioProcessor + IComponent) and Controller (IEditController) MUST be implemented as separate classes
- Set `kDistributable` flag in PClassInfo2::classFlags when processor and controller can run in different processes
- Use `IMessage`, `IConnectionPoint`, and `IAttributeList` for all processor-controller communication
- Processor MUST function correctly without controller instantiation
- State synchronization MUST use `setComponentState()` - controller synchronizes to processor state, never the reverse
- NEVER send raw pointers via IMessage between components

**Rationale:** This architecture enables hosts to run processors on separate machines/threads, provides clean testability, and follows Steinberg's design philosophy for maximum flexibility.

### II. Real-Time Audio Thread Safety

The audio processing thread operates under hard real-time constraints. Any operation that could cause unbounded delays is FORBIDDEN.

**Non-Negotiable Rules:**
- NEVER allocate memory in audio callbacks (no `malloc`, `free`, `new`, `delete`, or STL containers that allocate)
- NEVER use locks, mutexes, or any blocking synchronization primitives in the audio thread
- NEVER perform file I/O, network operations, or system calls in the audio thread
- NEVER throw or catch exceptions in the audio thread
- Pre-allocate ALL buffers during `initialize()` or `setupProcessing()` before `setActive(true)`
- Audio callbacks MUST complete within the buffer duration (typically 1-10ms at common sample rates)
- Use lock-free queues (SPSC ring buffers) for all inter-thread communication

**Rationale:** Priority inversion, memory allocator locks, and system calls cause audio dropouts and glitches. Real-time audio requires deterministic execution times.

### III. Modern C++ Standards

All code MUST follow modern C++ best practices as defined by the C++ Core Guidelines.

**Non-Negotiable Rules:**
- Target C++17 minimum; prefer C++20 features where available and SDK-compatible
- Use RAII for ALL resource management - no manual resource cleanup in destructors
- Prefer `std::unique_ptr` as the default smart pointer for ownership
- Use `std::shared_ptr` ONLY when shared ownership is genuinely required (rare in plugin code)
- NEVER use raw `new`/`delete` - wrap allocations in smart pointers immediately
- Use move semantics to transfer ownership of large buffers without copying
- Prefer value semantics and stack allocation where object lifetime is scope-bound
- Use `constexpr` and `const` aggressively to enable compile-time computation
- Prefer `std::array` over C-style arrays; `std::span` for non-owning views (C++20)

**Rationale:** RAII eliminates resource leaks, smart pointers enforce ownership semantics, and modern C++ features improve safety and performance.

### IV. SIMD & DSP Optimization

Digital signal processing code MUST be optimized for modern CPU architectures.

**Non-Negotiable Rules:**
- Align audio buffer data to 16-byte boundaries (SSE) or 32-byte boundaries (AVX)
- Use `alignas(16)` or platform-specific alignment attributes for SIMD data structures
- Process audio in contiguous, sequential memory access patterns
- Avoid strided or scattered memory access in inner loops
- Minimize branching in inner processing loops; prefer branchless algorithms
- Avoid virtual function calls in tight audio processing loops
- Use SIMD intrinsics (SSE2/AVX/NEON) for parallel sample processing where beneficial
- Profile before optimizing - measure actual bottlenecks, don't assume

**Rationale:** SIMD operations can process 4-8 samples simultaneously. Proper memory alignment and access patterns maximize cache efficiency and enable vectorization.

### V. VSTGUI Development

User interface development MUST use VSTGUI properly and maintain complete separation from audio processing.

**Non-Negotiable Rules:**
- Use `UIDescription` XML and the WYSIWYG editor for UI layout
- Implement `VST3EditorDelegate` interface for custom view creation
- Register custom views with `UIViewFactory` for editor visibility
- Use `getParameterObject()` for UI-only state (tabs, scroll positions) not affecting audio
- UI thread MUST NEVER directly access audio processing data structures
- Use `IParameterChanges` for parameter updates from UI to processor
- All parameter values MUST be normalized (0.0 to 1.0) at the VST interface boundary
- Repaint operations MUST NOT block or stall the audio thread

**Rationale:** VSTGUI is the official Steinberg toolkit designed for VST3. Proper usage ensures compatibility, maintainability, and correct thread separation.

### VI. Memory Architecture

Memory management MUST be designed for real-time safety and predictable performance.

**Non-Negotiable Rules:**
- Pre-allocate all audio buffers, delay lines, and processing state during initialization
- Use memory pools for any runtime allocation needs outside the audio thread
- Use SPSC (single-producer, single-consumer) lock-free ring buffers for thread communication
- Size ring buffers to handle worst-case latency scenarios without blocking
- NEVER resize containers or reallocate buffers while audio processing is active
- Use `std::atomic` with appropriate memory ordering for simple shared state
- Prefer thread-local storage over shared state where possible

**Rationale:** Dynamic allocation during audio processing causes unpredictable latency. Pre-allocation and lock-free structures ensure deterministic behavior.

### VII. Project Structure & Build System

Project organization MUST follow modern CMake practices and maintain clear separation of concerns.

**Non-Negotiable Rules:**
- Use Modern CMake (3.20+) with target-based configuration; NEVER modify global variables like `CMAKE_CXX_FLAGS`
- Directory structure MUST follow:
  ```
  include/<project>/    # Public headers
  src/                  # Implementation files
  src/processor/        # Audio processor implementation
  src/controller/       # Edit controller and UI
  src/dsp/              # DSP algorithms (pure, testable)
  tests/                # All test code
  extern/               # External dependencies (git submodules)
  cmake/                # CMake helper modules
  resources/            # UI resources, uidesc files
  ```
- Headers in `include/<project>/` MUST use `#include <project/header.h>` style
- External dependencies MUST use git submodules in `extern/` with pinned versions
- VST3 SDK and VSTGUI MUST be included as submodules, not system installations
- Build configurations MUST support Debug, Release, and RelWithDebInfo
- Plugin MUST build and validate on Windows (MSVC), macOS (Clang), and Linux (GCC)

**Rationale:** Consistent structure enables maintainability, reproducible builds, and clear dependency management.

### VIII. Testing Discipline

All code MUST be testable, and critical paths MUST have automated tests.

**Non-Negotiable Rules:**
- DSP algorithms in `src/dsp/` MUST be pure functions testable without VST infrastructure
- Unit tests MUST cover all DSP algorithms with known input/output pairs
- Integration tests MUST verify plugin loads correctly in a test host
- Regression tests MUST compare audio output between versions for critical processing
- Tests MUST run in CI/CD pipeline on every commit
- Use a testing framework compatible with CMake (Catch2, GoogleTest, or doctest)
- Performance benchmarks MUST exist for critical audio processing paths
- NEVER commit code that breaks existing tests

**Rationale:** Testing catches bugs before release, enables refactoring with confidence, and documents expected behavior.

## Technical Constraints

This section defines hard technical requirements that MUST be satisfied by all implementations.

### SDK & Toolkit Requirements

| Component | Version | Source |
|-----------|---------|--------|
| VST3 SDK | 3.7.x or later | git submodule from steinbergmedia/vst3sdk |
| VSTGUI | 4.12 or later | Included with VST3 SDK or separate submodule |
| C++ Standard | C++17 minimum, C++20 preferred | Compiler flag |
| CMake | 3.20 or later | Build requirement |

### Platform Support Matrix

| Platform | Compiler | Architecture | Status |
|----------|----------|--------------|--------|
| Windows 10/11 | MSVC 2019+ | x64, ARM64 | Required |
| macOS 11+ | Clang/Xcode 13+ | x64, ARM64 (Apple Silicon) | Required |
| Linux | GCC 10+ / Clang 12+ | x64 | Optional |

### Performance Targets

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Audio callback duration | < 50% of buffer time | Profiling in release build |
| Parameter update latency | < 1 buffer | Measure UI→Processor delay |
| Memory allocation in audio thread | 0 | Static analysis + runtime checks |
| Plugin scan time | < 500ms | Host plugin scanning |

### Forbidden Patterns

The following patterns are BANNED from the codebase:

- `new`/`delete` without immediate smart pointer wrapping
- `malloc`/`free`/`realloc` anywhere in audio code
- `std::mutex`, `std::lock_guard`, `std::unique_lock` in audio thread
- `std::vector::push_back` or any allocating operation in audio callbacks
- `dynamic_cast` in audio processing paths
- `throw`/`catch` in audio thread code paths
- Global mutable state accessed from multiple threads without synchronization
- Raw C-style arrays for audio buffers (use `std::array` or aligned custom types)
- Busy-wait spinlocks as mutex replacements

### Cross-Platform Compatibility

Code MUST work correctly across MSVC (Windows), Clang (macOS), and GCC (Linux). This section documents known platform-specific behaviors that MUST be accounted for in all implementations.

#### Floating-Point Behavior

**NaN Detection:**
- The VST3 SDK enables `-ffast-math` globally via `SMTG_PlatformToolset.cmake`
- This causes `std::isnan()`, `__builtin_isnan()`, and `x != x` to be optimized away (compiler assumes NaN doesn't exist)
- Even bit manipulation with `std::bit_cast` can be optimized if the compiler recognizes the NaN check pattern
- **Required approach**:
  1. Use `std::bit_cast<uint32_t>` to check IEEE 754 bit patterns
  2. **AND** compile source files with `-fno-fast-math -fno-finite-math-only`
  3. Use CMake `set_source_files_properties()` (follows VSTGUI's pattern for `uijsonpersistence.cpp`)
- Example: `((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0)`

**Floating-Point Precision:**
- MSVC and Clang produce slightly different results at the 7th-8th decimal place
- This is expected behavior due to different floating-point implementations
- **Required approach**: Approval tests and golden master comparisons MUST use ≤6 decimal places
- Unit tests should use `Approx().margin()` for floating-point comparisons

**Denormalized Numbers (Subnormals):**
- Very small floating-point numbers cause massive CPU slowdowns (up to 100x) on some platforms
- IIR filters (lowpass, etc.) decay into denormals when fed silence
- ARM NEON forces flush-to-zero; x86 SSE/AVX requires explicit FTZ/DAZ flags
- **Required approach**: Enable FTZ (flush-to-zero) and DAZ (denormals-are-zero) in audio thread
- Alternative: Add tiny DC offset (~1e-15) to prevent signals from decaying to denormal range
- MSVC enables FTZ/DAZ automatically with SSE2; GCC/Clang require `-msse2 -mfpmath=sse`

**Constexpr Math:**
- `std::pow`, `std::log10`, `std::exp` are NOT constexpr in MSVC (even in C++20)
- **Required approach**: Implement custom Taylor series for constexpr math functions
- Use `std::bit_cast` (C++20) for constexpr bit manipulation

**Signed Integer Overflow:**
- Signed integer overflow is undefined behavior in C++
- GCC aggressively optimizes assuming no overflow; MSVC/Clang may wrap
- **Required approach**: Use unsigned integers where wraparound is needed, or use `-fwrapv` with GCC/Clang

#### SIMD and Memory Alignment

**Alignment Requirements:**
- SSE requires 16-byte alignment; AVX requires 32-byte alignment; AVX-512 requires 64-byte alignment
- Unaligned access may cause crashes (SSE) or performance degradation (AVX)
- **Required approach**: Use `alignas(32)` for audio buffers; use `_mm_malloc`/`_mm_free` for dynamic allocation
- C++17 aligned new works for stack/static; dynamic allocation needs platform-specific functions

**Cross-Platform SIMD:**
- x86 uses SSE/AVX; ARM uses NEON with different instruction sets
- **Required approach**: Use SIMD abstraction (sse2neon, SIMDe) or write separate code paths
- Apple Silicon (M1/M2/M3/M4) uses NEON; code compiled for x86 runs via Rosetta 2 (with overhead)
- For new code targeting x86 (2022+), target AVX2 with FMA as baseline

**Apple Silicon Considerations:**
- x86 plugins require Rosetta 2 translation (CPU overhead, potential latency)
- Native ARM64 builds required for optimal performance on M1/M2/M3/M4
- Rosetta 2 will eventually be deprecated (Rosetta 1 lasted ~2.5 OS versions)
- **Required approach**: Build universal binaries (x86_64 + arm64) for macOS

#### Threading and Synchronization

**Real-Time Thread Priority:**
- Windows: MMCSS (Multimedia Class Scheduler Service) and time-critical threads
- macOS: THREAD_TIME_CONSTRAINT_POLICY; Audio Workgroups on Apple Silicon
- Linux: SCHED_FIFO with rtprio; RT kernel optional but beneficial
- **Required approach**: Let the host/driver manage audio thread priority; don't set manually in plugin

**Atomic Operations:**
- `std::atomic<T>::is_lock_free()` returns different results per platform
- MSVC may use mutex for `std::atomic<double>` on some configurations
- Double-width CAS (128-bit) behavior varies: GCC/Clang may be lock-free where MSVC is not
- **Required approach**: Only `std::atomic_flag` is guaranteed lock-free; verify with `is_lock_free()` for other types
- Use `std::memory_order_acquire`/`release` instead of `seq_cst` for better ARM performance

**Memory Ordering:**
- x86 has strong memory model (most loads are acquire, stores are release)
- ARM has weak memory model (requires explicit barriers, more expensive seq_cst)
- **Required approach**: Use minimal memory ordering (`relaxed` for counters, `acquire`/`release` for synchronization)

**Spinlocks:**
- NEVER use pure spinlocks; they can starve threads holding locks
- **Required approach**: Use try_lock() on audio thread with fallback; never block waiting for lock

#### Build System and Toolchain

**CMake and Generators:**
- macOS requires Xcode generator (`-G Xcode`) for Objective-C++ support (VSTGUI)
- FetchContent with git clone can fail in CI due to rate limiting
- **Required approach**: Use URL-based FetchContent with SHA256 hashes for dependencies

**ABI Compatibility:**
- VST3 SDK uses COM-like C++ interfaces; ABI is compiler-dependent
- GCC/Clang use Itanium ABI; MSVC uses different vtable layout and exception handling
- **Required approach**: Use the recommended compiler per platform (MSVC on Windows, Clang on macOS, GCC on Linux)
- Clang can target MSVC ABI on Windows, but compatibility is not 100%

**Runtime Libraries:**
- Windows plugins should link CRT statically or ensure matching runtime on target systems
- **Required approach**: Use `/MT` (static) or ensure VCRUNTIME redistributable is available

#### File System and Encoding

**File Path Encoding:**
- Windows uses UTF-16 (`wchar_t` is 2 bytes); macOS/Linux use UTF-8 (`wchar_t` is 4 bytes)
- Windows `fopen()` doesn't support UTF-8 paths; requires `_wfopen()` with wide strings
- **Required approach**: Use UTF-8 internally; convert to UTF-16 for Windows file APIs
- Prefer cross-platform file APIs from the SDK or use `std::filesystem` (C++17)

**State Persistence (Endianness):**
- x86 and ARM are little-endian; some legacy systems are big-endian
- Plugin state saved on one platform must load on another
- **Required approach**: Use explicit byte order in serialization; prefer little-endian or use network byte order functions

#### Plugin Validation

**Platform-Specific Validation:**
- Use pluginval (Tracktion) for automated validation on all platforms
- Run validation at strictness level 5+ for each format (VST3, AU) and platform
- Test in multiple DAWs (different DAWs have different plugin scanning behaviors)
- **Required approach**: CI must run pluginval on Windows, macOS, and Linux before release

## Development Workflow

### Code Review Requirements

All code changes MUST satisfy:

1. **Constitution Compliance**: Reviewer MUST verify no principle violations
2. **Thread Safety Audit**: Changes touching audio code MUST have explicit thread safety review
3. **Memory Safety Check**: No new allocations in audio paths
4. **Test Coverage**: New DSP code MUST include unit tests
5. **Build Verification**: CI MUST pass on all required platforms

### Profiling Protocol

Before optimization work:

1. Profile in Release build with representative audio (not synthetic test signals)
2. Identify actual bottlenecks with CPU profiler (VTune, Instruments, perf)
3. Document baseline measurements before changes
4. Verify improvement with same measurement methodology
5. Ensure no regression in other areas

### Documentation Standards

- Public APIs MUST have Doxygen-compatible documentation
- Complex DSP algorithms MUST reference academic papers or explain derivation
- Thread safety requirements MUST be documented in header comments
- Parameter ranges and semantics MUST be documented

### XIII. Living Architecture Documentation

The project MUST maintain an `ARCHITECTURE.md` file that serves as a living inventory of all functional domains, components, and APIs.

**Non-Negotiable Rules:**
- `ARCHITECTURE.md` MUST exist at the repository root
- Every spec implementation MUST update `ARCHITECTURE.md` as a final task before completion
- Updates MUST include: new components added, APIs introduced, layer assignments, and usage examples
- The document is organized by DSP layer (0-4), not chronologically
- Each component entry MUST include: purpose, public API, location, and "when to use this"
- This document serves as the canonical reference when writing new specs to avoid duplication and ensure proper reuse

**Rationale:** A living architecture document prevents reinventing existing functionality, enables proper composition of components, and provides a single source of truth for what exists in the codebase.

## Governance

### Constitution Authority

This constitution is the supreme authority for development decisions. In case of conflict between this constitution and other documentation, the constitution prevails.

### Amendment Process

1. Propose amendment with rationale in writing
2. Review impact on existing codebase
3. Update version number per semantic versioning:
   - MAJOR: Principle removal or incompatible redefinition
   - MINOR: New principle or significant expansion
   - PATCH: Clarification or typo fix
4. Update dependent templates if affected
5. Document migration path for existing code if needed

### Compliance Verification

- All pull requests MUST include constitution compliance statement
- Automated static analysis SHOULD enforce checkable rules
- Periodic codebase audits MUST verify ongoing compliance
- Violations MUST be fixed or explicitly justified with technical rationale

### Exception Process

If a principle cannot be followed due to technical necessity:

1. Document the specific constraint preventing compliance
2. Propose minimal deviation with scope limitation
3. Include timeline for removing the exception if temporary
4. Record in Complexity Tracking section of relevant plan.md

### IX. Layered DSP Architecture

DSP code MUST follow a strict layered architecture where higher layers compose from lower layers, never the reverse.

**Non-Negotiable Rules:**
- Layer 0 (Core Utilities): Memory pools, lock-free queues, fast math, SIMD abstractions - NO dependencies on higher layers
- Layer 1 (DSP Primitives): Delay lines, LFOs, biquads, smoothers, oversamplers - depend ONLY on Layer 0
- Layer 2 (DSP Processors): Filters, saturators, pitch shifters, envelope followers - compose from Layer 0-1
- Layer 3 (System Components): Delay engine, feedback network, modulation matrix - compose from Layer 0-2
- Layer 4 (User Features): Complete modes (Tape, BBD, Shimmer) - compose from Layer 0-3
- NEVER allow circular dependencies between layers
- NEVER skip layers (e.g., Layer 4 directly using Layer 0 internals instead of Layer 1-3 abstractions)
- Each layer MUST be independently testable without instantiating higher layers
- When adding new DSP code, explicitly identify which layer it belongs to in the file header

**Rationale:** Layered architecture maximizes code reuse, ensures improvements propagate to all dependent features, and creates clear boundaries for testing and maintenance.

### X. DSP Processing Constraints

Digital signal processing operations have specific requirements for quality and real-time safety.

**Non-Negotiable Rules:**
- **Sample Accuracy**: All timing-critical operations (modulation, sync, tap tempo) MUST work at sample granularity
- **Oversampling for Nonlinearities**: Saturation, distortion, and waveshaping MUST be oversampled (minimum 2x) to prevent aliasing
- **DC Blocking**: Always apply a DC blocker (high-pass at ~5-20Hz) after asymmetric saturation or any processing that can introduce DC offset
- **Interpolation Selection**:
  - Allpass interpolation: ONLY for fixed delays in feedback loops (not for modulated delays)
  - Linear interpolation: Acceptable for LFO-modulated delays when oversampled
  - Cubic/Lagrange: Required for pitch shifting and high-quality time-varying delays
- **Fast Math Approximations**: May use optimized `fastSin`, `fastTanh`, `fastExp` only when:
  - Error bounds are documented and acceptable for the use case
  - Full precision math is available as fallback for validation testing
- **Filter Stability**: Use Transposed Direct Form II for floating-point biquads; validate stability at extreme parameter values
- **Feedback Limiting**: Feedback paths exceeding 100% MUST include soft limiting or compression to prevent runaway oscillation
- **FFT Processing**: Use power-of-2 sizes, apply appropriate windowing (Hann for general use), and maintain COLA (Constant Overlap-Add) constraint

**Rationale:** These constraints prevent common DSP artifacts (aliasing, DC drift, clicks, instability) while maintaining real-time performance.

### XI. Performance Budgets

All components MUST meet defined performance targets to ensure the complete plugin remains usable.

**Non-Negotiable Rules:**
- Total plugin CPU: < 5% single core for full feature set at 44.1kHz, stereo
- Individual Layer 1 primitive: < 0.1% CPU per instance
- Individual Layer 2 processor: < 0.5% CPU per instance
- Layer 3 system component: < 1% CPU per instance
- Maximum delay time support: 10 seconds at 192kHz (1,920,000 samples buffer)
- Parameter update latency: < 1 audio buffer
- Plugin scan/load time: < 500ms
- All performance claims MUST be validated with profiling in Release builds
- Performance regression tests MUST exist for critical DSP paths

**Rationale:** Performance budgets prevent feature creep from making the plugin unusable and ensure compositions of components remain efficient.

### XII. Test-First Development

All implementation work MUST follow test-first methodology. Testing guidance MUST be actively referenced during development.

**Non-Negotiable Rules:**
- **Context Verification**: Before starting ANY implementation task, VERIFY that `specs/TESTING-GUIDE.md` is in the current context. If not, INGEST IT before proceeding.
- **Test Before Implementation**: Write failing tests BEFORE writing implementation code
- **Task Completion**: Every implementation task MUST end with a commit of the work completed
- **Explicit Todo Items**: The following steps MUST appear as explicit items in the task todo list (not implicit rules):
  1. "Verify TESTING-GUIDE.md is in context (ingest if needed)"
  2. "Write failing tests for [feature]"
  3. "Implement [feature] to make tests pass"
  4. "Commit completed work"
- **Test Categories**: Follow the testing guide for appropriate test categories:
  - Unit tests for pure DSP functions
  - Integration tests for component interactions
  - Regression tests for audio output stability
- **No Skipping Tests**: Implementation without corresponding tests is FORBIDDEN except for trivial refactors with existing test coverage

**Rationale:** Test-first development catches bugs early, documents expected behavior, enables safe refactoring, and ensures the TESTING-GUIDE.md patterns are consistently applied.

**Version**: 1.5.0 | **Ratified**: 2025-12-21 | **Last Amended**: 2025-12-22
