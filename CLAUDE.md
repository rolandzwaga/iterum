# CLAUDE.md - VST Plugin Development Guidelines

This file provides guidance for AI assistants working on this VST3 plugin project. All code contributions must comply with the project constitution at `.specify/memory/constitution.md`.

## Project Overview

This is a **VST3 plugin** built with:
- **Steinberg VST3 SDK** (not JUCE or other frameworks)
- **VSTGUI** for user interface
- **Modern C++20**
- **CMake 3.20+** build system

## Critical Rules (Non-Negotiable)

#### ⚠️ CRITICAL Bug Workaround

CRITICAL: Follow these guidelines when using tools operating on file paths EXCEPT `Bash` in any skill or agent. Otherwise, they will not work correctly.

- When referencing file paths in tool calls, ALWAYS use the Windows-style paths with backslashes (`\`), as we're running on Windows.
- When referencing paths to the home directory, ALWAYS EXPAND the tilde symbol (`~`) representing the home directory to the full path, e.g., `C:\Users\some.user`.

#### Applies To

- `Edit`
- `Glob`
- `Grep`
- `Read`

#### Examples (CRITICAL Bug Workaround)

##### ✅ Correct (File Paths)

```json
{
    "type": "tool_use",
    "id": "...",
    "name": "Read",
    "input": {
        "file_path": "C:\\path\\to\\some\\file.txt"
    }
}
```

```json
{
    "type": "tool_use",
    "id": "...",
    "name": "Edit",
    "input": {
        "file_path": "C:\\path\\to\\some\\file.txt",
        "old_string": "...",
        "new_string": "..."
    }
}
```

##### ❌ Incorrect (File Paths)

```json
{
    "type": "tool_use",
    "id": "...",
    "name": "Read",
    "input": {
        "file_path": "/c/path/to/some/file.txt"
    }
}
```

#### ⚠️ CROSS-PLATFORM REQUIREMENT (CRITICAL)

**This plugin MUST be fully cross-platform (Windows, macOS, Linux).** Platform-specific solutions are FORBIDDEN.

**NEVER do these:**
- Switch from VSTGUI generic menus to native Windows popups
- Use Win32 APIs (GetTickCount, WM_* messages, HWND, etc.) for UI functionality
- Use macOS-native APIs (Cocoa, AppKit) for UI functionality
- Suggest "try the native popup" as a fix for VSTGUI issues
- Use ANY platform-specific UI solution without explicit user approval

**ALWAYS do these:**
- Use VSTGUI's cross-platform abstractions (COptionMenu, CFileSelector, etc.)
- When VSTGUI offers native vs generic options, PREFER generic (cross-platform)
- When debugging UI issues, find cross-platform solutions, not Windows workarounds

**Platform-specific code is ONLY acceptable for:**
- Debug logging (guarded by `#if defined(_DEBUG) && defined(_WIN32)`)
- Performance timing that has cross-platform fallbacks
- Workarounds for platform-specific bugs WITH documented justification AND user approval

**Why this matters:** Every time you suggest "try the Windows native popup", you're proposing a solution that:
1. Won't work on macOS or Linux
2. Defeats the entire purpose of using VSTGUI
3. Creates technical debt and platform fragmentation

**If a VSTGUI feature doesn't work correctly, the fix must also use VSTGUI.**


### 1. Real-Time Audio Thread Safety

The audio thread (`Processor::process()`) has **hard real-time constraints**. The following are **FORBIDDEN** in any code path called from `process()`:

```cpp
// NEVER DO THESE IN AUDIO THREAD:
new / delete / malloc / free          // Memory allocation
std::vector::push_back()              // May reallocate
std::mutex / std::lock_guard          // Blocking synchronization
throw / catch                         // Exception handling
std::cout / printf / logging          // I/O operations
File operations                       // Disk I/O
Virtual function calls in tight loops // Indirect calls (where avoidable)
```

**ALWAYS:**
- Pre-allocate buffers in `setupProcessing()` before `setActive(true)`
- Use `std::atomic` with relaxed memory ordering for parameter access
- Use lock-free SPSC ring buffers for thread communication

### 2. VST3 Architecture Separation

Processor and Controller are **separate components**:

```cpp
// Processor (audio thread) - src/processor/
class Processor : public Steinberg::Vst::AudioEffect {
    // Audio processing ONLY
    // NO UI code, NO direct parameter storage for UI
};

// Controller (UI thread) - src/controller/
class Controller : public Steinberg::Vst::EditControllerEx1 {
    // UI and parameter management ONLY
    // NO audio processing code
};
```

**Rules:**
- Never include controller headers in processor files (and vice versa)
- Use `IMessage` for processor↔controller communication
- Processor must work without controller instantiated
- State flows: Host → Processor → Controller (via `setComponentState`)

### 3. Parameter Handling

All parameters at the VST interface boundary are **normalized (0.0 to 1.0)**:

```cpp
// In Controller::initialize()
parameters.addParameter(
    STR16("Gain"),
    STR16("dB"),
    0,                    // stepCount (0 = continuous)
    0.5,                  // defaultValue NORMALIZED
    Steinberg::Vst::ParameterInfo::kCanAutomate,
    kGainId
);

// In Processor::process() - read from atomic
float gain = gain_.load(std::memory_order_relaxed);

// Conversion happens in processParameterChanges()
case kGainId:
    gain_.store(static_cast<float>(normalizedValue * 2.0), // denormalize
               std::memory_order_relaxed);
```

## Cross-Platform Compatibility (CRITICAL)

This project runs CI on Windows (MSVC), macOS (Clang), and Linux (GCC). The following platform differences MUST be considered during specification and implementation. See constitution section "Cross-Platform Compatibility" for the complete reference.

### Floating-Point Behavior

**NaN Detection:**

The VST3 SDK enables `-ffast-math` globally (see `SMTG_PlatformToolset.cmake`), which causes `std::isnan()`, `__builtin_isnan()`, and even bit manipulation to be optimized away.

```cpp
// WRONG - optimized away by -ffast-math
if (x != x) { /* NaN */ }
if (std::isnan(x)) { /* NaN */ }  // Also optimized away!
if (__builtin_isnan(x)) { /* NaN */ }  // Also optimized away!

// CORRECT - bit manipulation WITH -fno-fast-math on the source file
constexpr bool isNaN(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}
```

**Critical:** Source files using NaN detection MUST disable fast-math:
```cmake
# In CMakeLists.txt - follows VSTGUI's pattern (see uijsonpersistence.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(my_file.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")
endif()
```

**Floating-Point Precision:**
- MSVC and Clang differ at 7th-8th decimal places
- Approval tests: Use `std::setprecision(6)` or less
- Unit tests: Use `Approx().margin()` for comparisons

**Denormalized Numbers (Performance Killer):**
- IIR filters decay into denormals when fed silence → 100x CPU slowdown
- ARM NEON auto-flushes to zero; x86 requires explicit FTZ/DAZ
- Solution: Enable FTZ/DAZ in audio thread or add tiny DC offset (~1e-15)
```cpp
// Enable FTZ/DAZ on x86 (in setupProcessing or setActive)
_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
```

**Constexpr Math:**
- `std::pow`, `std::log10`, `std::exp` are NOT constexpr in MSVC
- Use custom Taylor series (see `src/dsp/core/db_utils.h`)

### Brace Initialization and Narrowing Conversions

**CRITICAL:** Clang treats narrowing conversions in brace initialization as errors (`-Wc++11-narrowing`), while MSVC only warns.

```cpp
// WRONG - Clang error, MSVC warning
struct BlockContext {
    double sampleRate;
    size_t blockSize;      // <-- size_t, not double!
    double tempoBPM;
    bool isPlaying;
};
BlockContext ctx{44100.0, 120.0, false};  // 120.0 narrows to size_t - ERROR on Clang!

// CORRECT - Use C++20 designated initializers for clarity and safety
BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 120.0, .isPlaying = false};
```

**Rules:**
- ALWAYS use designated initializers for structs with mixed types (especially `size_t` + `double`)
- NEVER rely on positional brace initialization for complex structs
- If you see MSVC warnings about narrowing (`C4244`, `C4838`), fix them - they WILL be errors on Clang

**Common offender:** `BlockContext` has `size_t blockSize` between `double sampleRate` and `double tempoBPM`. Easy to forget when initializing with `{sampleRate, tempo, isPlaying}`.

### SIMD and Memory Alignment

**Alignment Requirements:**
- SSE: 16-byte; AVX: 32-byte; AVX-512: 64-byte
- Unaligned SSE access crashes; unaligned AVX is slow
```cpp
// Stack/static allocation
alignas(32) std::array<float, 256> buffer;

// Dynamic allocation (C++17)
auto* buffer = static_cast<float*>(std::aligned_alloc(32, size * sizeof(float)));
// Or use _mm_malloc/_mm_free for portability
```

**Cross-Platform SIMD (SSE/AVX vs NEON):**
- x86: SSE/AVX intrinsics; ARM: NEON with different API
- Use abstraction library (sse2neon, SIMDe) for portability
- Apple Silicon (M1/M2/M3/M4): NEON native; x86 via Rosetta 2 (with overhead)
- Build universal binaries for macOS: `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"`

### Threading and Atomics

**Real-Time Thread Priority:**
- Don't manually set thread priority in plugins; let host manage it
- Windows: MMCSS; macOS: THREAD_TIME_CONSTRAINT_POLICY; Linux: SCHED_FIFO

**Atomic Operations:**
- Only `std::atomic_flag` is guaranteed lock-free everywhere
- Verify with `is_lock_free()` for other types (especially `std::atomic<double>`)
- MSVC may silently use mutex for 128-bit atomics

**Memory Ordering:**
- x86: Strong model (acquire/release nearly free)
- ARM: Weak model (seq_cst is expensive)
- Use `memory_order_relaxed` for counters, `acquire`/`release` for sync

**Spinlocks:**
- NEVER block-wait on audio thread
- Use `try_lock()` with fallback, not spin loops

### Build System

**CMake:**
- macOS: Use `-G Xcode` for Objective-C++ (VSTGUI)
- FetchContent: Use URL + SHA256, not git clone (CI rate limits)

**ABI Compatibility:**
- Use MSVC on Windows, Clang on macOS, GCC on Linux
- Mixing compilers can cause vtable/exception handling crashes

**Runtime Libraries (Windows):**
- Link CRT statically (`/MT`) or ship VCRUNTIME redistributable

### File System

**Path Encoding:**
- Windows: UTF-16 (`wchar_t` = 2 bytes); macOS/Linux: UTF-8 (`wchar_t` = 4 bytes)
- Use UTF-8 internally; convert to UTF-16 for Windows file APIs
- Prefer `std::filesystem` (C++17) for cross-platform paths

**State Persistence:**
- Use explicit byte order (little-endian preferred)
- State saved on Windows must load on macOS and vice versa

### Plugin Validation

- Use [pluginval](https://www.tracktion.com/develop/pluginval) at strictness level 5+
- Test in multiple DAWs (scanning behavior varies)
- CI should run validation on all platforms before release

## Code Style

### Modern C++ Requirements

```cpp
// USE: Smart pointers
auto buffer = std::make_unique<float[]>(size);

// USE: RAII
class DelayLine {
    std::vector<float> buffer_;  // Automatically cleaned up
public:
    DelayLine(size_t size) : buffer_(size, 0.0f) {}
};

// USE: constexpr for compile-time computation
constexpr float kPi = 3.14159265358979323846f;
constexpr float dBToLinear(float dB) { return std::pow(10.0f, dB / 20.0f); }

// USE: Move semantics for large buffers
void setBuffer(std::vector<float>&& buffer) {
    buffer_ = std::move(buffer);
}

// AVOID: Raw new/delete
float* buffer = new float[size];  // BAD
delete[] buffer;                   // BAD
```

### DSP Code Guidelines

DSP algorithms go in `src/dsp/` and must be:
1. **Pure functions** - no side effects, testable without VST infrastructure
2. **Header-inline** where performance matters
3. **SIMD-friendly** - contiguous memory access, minimal branching

```cpp
// Good: Pure, testable function
inline void applyGain(float* buffer, size_t numSamples, float gain) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= gain;
    }
}

// Good: Aligned for SIMD
alignas(16) std::array<float, 4> simdBuffer;
```

### Naming Conventions

```cpp
// Classes: PascalCase
class AudioProcessor {};

// Functions/Methods: camelCase
void processAudio();

// Member variables: camelCase with trailing underscore
float sampleRate_;
std::atomic<float> gain_;

// Constants: kPascalCase
constexpr float kDefaultGain = 1.0f;

// Namespaces: PascalCase
namespace Iterum { namespace DSP { ... } }

// Parameter IDs: kDescriptiveNameId
enum ParameterIDs : Steinberg::Vst::ParamID {
    kBypassId = 0,
    kGainId = 1,
};
```

## Layered DSP Architecture

This project uses a **5-layer compositional architecture**. Higher layers compose from lower layers, never the reverse. See [ROADMAP.md](specs/ROADMAP.md) for full details.

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
│  (Tape Mode, BBD Mode, Multi-Tap, Ping-Pong, Shimmer, etc.) │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
│    (Delay Engine, Modulation Matrix, Feedback Network)      │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
│  (Filters, Saturation, Pitch Shifter, Diffuser, Envelope)   │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│    (Delay Line, LFO, Biquad, Smoother, Oversampler)         │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
│      (Memory Pool, Lock-free Queue, Fast Math, SIMD)        │
└─────────────────────────────────────────────────────────────┘
```

**Rules:**
- Each layer can ONLY depend on layers below it
- No circular dependencies between layers
- Each layer must be independently testable
- Add layer identifier in file header comments: `// Layer 1: DSP Primitive`

### Layer 0 Refactoring Analysis (During Planning)

When creating specs for Layer 2+ features, **proactively identify** utility functions or patterns that should live in Layer 0 for reuse. This analysis should happen during `/speckit.plan`, not after implementation.

**When to Extract to Layer 0:**

| Criteria | Extract? | Example |
|----------|----------|---------|
| Used by 2+ Layer 1 primitives | **Yes** | `kDenormalThreshold`, `flushDenormal()` |
| Pure, stateless utility function | **Yes** | `stereoCrossBlend()`, `dbToGain()` |
| Algorithm with specific audio semantics | **Yes** | Cross-feedback blending formula |
| One-liner arithmetic | **No** | `ms * sampleRate / 1000.0` |
| Class-specific member function | **No** | `DelayEngine::msToSamples()` |
| Fewer than 3 usages expected | **No** | Wait until third usage appears |

**Decision Framework:**

1. **Is it a one-liner?** → Keep as member function (duplication is acceptable)
2. **Does it have audio-specific semantics?** → Extract to Layer 0 with documentation
3. **Will 3+ components need it?** → Extract proactively
4. **Is it already duplicated?** → Refactor to Layer 0 now

**Case Study: FeedbackNetwork vs DelayEngine**

Both systems needed `msToSamples()`:
```cpp
float msToSamples(float ms) const noexcept {
    return static_cast<float>(ms * sampleRate_ / 1000.0);
}
```

**Decision: Keep as member function** because:
- It's a one-liner (trivial duplication)
- Each class stores `sampleRate_` anyway
- Only 2 usages exist (if a third appears, reconsider)

Contrast with `stereoCrossBlend()`:
```cpp
void stereoCrossBlend(float inL, float inR, float crossAmount, float& outL, float& outR);
```

**Decision: Extract to Layer 0** because:
- Implements specific audio algorithm (cross-channel blending)
- Has edge-case semantics worth documenting once
- Will be needed by future ping-pong delays, stereo wideners, etc.

## File Organization

```
src/
├── entry.cpp              # Plugin factory - don't modify unless adding new components
├── plugin_ids.h           # All GUIDs and parameter IDs - single source of truth
├── version.h              # Version info - keep in sync with CMakeLists.txt
├── processor/
│   ├── processor.h        # Audio processor declaration
│   └── processor.cpp      # Audio processing implementation
├── controller/
│   ├── controller.h       # Edit controller declaration
│   └── controller.cpp     # UI/parameter management
└── dsp/
    ├── core/              # Layer 0: Core utilities
    │   ├── memory_pool.h  #   Pre-allocated memory for real-time safety
    │   ├── lock_free_queue.h  # SPSC queue for thread communication
    │   ├── fast_math.h    #   Optimized transcendentals (fastSin, fastTanh, etc.)
    │   └── simd_ops.h     #   SIMD abstractions (SSE/AVX/NEON)
    ├── primitives/        # Layer 1: DSP primitives
    │   ├── delay_line.h   #   Circular buffer with interpolation
    │   ├── lfo.h          #   Low-frequency oscillator (wavetable-based)
    │   ├── biquad.h       #   Biquad filter (Transposed Direct Form II)
    │   ├── smoother.h     #   Parameter smoothing (one-pole, linear ramp)
    │   └── oversampler.h  #   Up/downsampling for nonlinear processing
    ├── processors/        # Layer 2: DSP processors
    │   ├── filter.h       #   Multi-mode filter (LP/HP/BP/Notch/Shelf)
    │   ├── saturator.h    #   Saturation with oversampling
    │   ├── pitch_shifter.h #  Pitch shifting (granular/phase vocoder)
    │   ├── envelope_follower.h # Amplitude tracking
    │   └── diffuser.h     #   Allpass diffusion network
    ├── systems/           # Layer 3: System components
    │   ├── delay_engine.h #   Complete delay with tempo sync
    │   ├── feedback_network.h # Feedback path with filtering/saturation
    │   └── modulation_matrix.h # LFO/envelope routing
    └── features/          # Layer 4: User features
        ├── tape_mode.h    #   Tape delay emulation
        ├── bbd_mode.h     #   Bucket-brigade emulation
        └── shimmer_mode.h #   Pitch-shifted feedback delay
```

## Common Patterns

### Adding a New Parameter

1. **Add ID** in `src/plugin_ids.h`:
```cpp
enum ParameterIDs : Steinberg::Vst::ParamID {
    kBypassId = 0,
    kGainId = 1,
    kNewParamId = 2,  // ADD HERE
    kNumParameters
};
```

2. **Add atomic** in `src/processor/processor.h`:
```cpp
std::atomic<float> newParam_{0.5f};  // Default value
```

3. **Handle in processParameterChanges()** in `processor.cpp`:
```cpp
case kNewParamId:
    newParam_.store(static_cast<float>(value), std::memory_order_relaxed);
    break;
```

4. **Register in Controller::initialize()** in `controller.cpp`:
```cpp
parameters.addParameter(STR16("New Param"), ...);
```

5. **Add to state save/load** in both processor and controller

6. **Add UI control** in `resources/editor.uidesc`

### Pre-Implementation Research (ODR Prevention)

**CRITICAL**: Before creating ANY new class, struct, or component, you MUST search the codebase for existing implementations:

```bash
# Search for existing class with the same name
grep -r "class ClassName" src/
grep -r "struct StructName" src/

# Check common utility files
cat src/dsp/dsp_utils.h | grep -A 20 "class"
```

**Why This Matters**: C++ has the One Definition Rule (ODR). Two classes with the same name in the same namespace cause **undefined behavior**, even if in different files. The compiler silently picks one definition, leading to:
- Garbage memory values (e.g., `-107374176.0f` = `0xCCCCCCCC` debug fill)
- Members that appear uninitialized despite correct constructors
- Tests that mysteriously fail with no apparent logic error

**Mandatory Checklist Before Creating New Components**:
1. [ ] Search codebase: `grep -r "class NewClassName" src/`
2. [ ] Check `src/dsp/dsp_utils.h` for simple utilities
3. [ ] Check layer-appropriate directory (primitives/, processors/, etc.)
4. [ ] Read ARCHITECTURE.md for existing component inventory

**Diagnostic Checklist for Strange Test Failures**:
If you see garbage values or "uninitialized" data in tests:
1. Are values like `-107374176.0f` (MSVC debug pattern) appearing?
2. Does one member work while an adjacent member has garbage?
3. Did the class recently get added to a new file?
→ **First action**: Search for duplicate class definitions before debugging logic

**Lesson Learned**: In spec 005-parameter-smoother, a new `OnePoleSmoother` was created in `smoother.h` while an older version existed in `dsp_utils.h`. Both were in namespace `Iterum::DSP`. The old class had 2 members, the new had 5. Tests accessed the 5-member layout but got the 2-member class, causing the 3rd-5th members to read uninitialized memory.

**Layer 0 Audit for Utility Functions**:

ODR also applies to **constants and inline functions**, not just classes. Before defining ANY of these in Layer 1+ headers, check if they exist in Layer 0:

```bash
# Check db_utils.h for math utilities
grep -E "constexpr|inline" src/dsp/core/db_utils.h | head -30

# Common utilities that MUST live in Layer 0 (db_utils.h):
# - kDenormalThreshold, flushDenormal() - denormal prevention
# - isNaN(), isInf() - IEEE 754 checks
# - constexprExp(), constexprPow10(), constexprLog10() - constexpr math
# - dbToGain(), gainToDb() - dB conversions
```

**Rule**: If a utility function/constant could be used by multiple Layer 1 primitives, it belongs in Layer 0. Never duplicate between Layer 1 headers - centralize in `src/dsp/core/`.

**Lesson Learned**: In spec 008-multimode-filter, including both `biquad.h` and `smoother.h` exposed that both defined `kDenormalThreshold` and `flushDenormal()`. These should have been in `db_utils.h` from the start.

### Adding DSP Processing

1. Create pure function in `src/dsp/`:
```cpp
// src/dsp/my_effect.h
#pragma once
namespace Iterum::DSP {
    inline void myEffect(float* buffer, size_t numSamples, float param) noexcept {
        // Implementation
    }
}
```

2. Write tests first in `tests/unit/`:
```cpp
TEST_CASE("myEffect processes correctly", "[dsp]") {
    std::array<float, 4> buffer = {1.0f, 0.5f, -0.5f, -1.0f};
    Iterum::DSP::myEffect(buffer.data(), buffer.size(), 0.5f);
    REQUIRE(buffer[0] == Approx(expected));
}
```

3. Call from `Processor::process()`:
```cpp
#include "dsp/my_effect.h"
// In process():
DSP::myEffect(outputBuffer, numSamples, param_.load(std::memory_order_relaxed));
```

### Pre-allocating Buffers

```cpp
// In Processor::setupProcessing()
Steinberg::tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // ALLOCATE HERE - this is the ONLY safe place
    delayBuffer_.resize(static_cast<size_t>(sampleRate_ * kMaxDelaySeconds));
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);

    return AudioEffect::setupProcessing(setup);
}
```

### Adding a Layer 1 DSP Primitive

1. Create header in `src/dsp/primitives/`:
```cpp
// src/dsp/primitives/my_primitive.h
// Layer 1: DSP Primitive
#pragma once
#include "dsp/core/fast_math.h"  // OK: Layer 0 dependency

namespace Iterum::DSP {
    class MyPrimitive {
    public:
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void process(float* buffer, size_t numSamples) noexcept;
        void reset() noexcept;
    private:
        // Pre-allocated state only
    };
}
```

2. Write tests in `tests/unit/primitives/`

3. **Never** include headers from Layer 2+ in primitive code

### Adding a Layer 2 DSP Processor

Processors compose primitives. Always check the layer hierarchy before adding includes.

```cpp
// src/dsp/processors/my_processor.h
// Layer 2: DSP Processor
#pragma once
#include "dsp/primitives/biquad.h"      // OK: Layer 1
#include "dsp/primitives/smoother.h"    // OK: Layer 1
#include "dsp/primitives/oversampler.h" // OK: Layer 1
// #include "dsp/systems/delay_engine.h" // FORBIDDEN: Layer 3
```

## DSP Implementation Rules

### Interpolation Selection

Choose interpolation based on use case:

| Use Case | Interpolation | Why |
|----------|---------------|-----|
| Fixed delay in feedback loop | Allpass | No amplitude distortion |
| LFO-modulated delay (chorusing) | Linear or Cubic | Allpass causes artifacts when modulated |
| Pitch shifting | Lagrange or Sinc | Higher quality for transposition |
| Smooth parameter changes | One-pole smoother | Simple and effective |

**Critical:** Never use allpass interpolation for modulated delays!

### Oversampling Requirements

Nonlinear processing (saturation, waveshaping) MUST be oversampled:

```cpp
// In saturator processor
void process(float* buffer, size_t numSamples) noexcept {
    // 1. Upsample (2x minimum)
    oversampler_.upsample(buffer, numSamples, oversampledBuffer_);

    // 2. Apply nonlinearity at higher rate
    for (size_t i = 0; i < numSamples * 2; ++i) {
        oversampledBuffer_[i] = std::tanh(oversampledBuffer_[i] * drive_);
    }

    // 3. Downsample back
    oversampler_.downsample(oversampledBuffer_, numSamples * 2, buffer);

    // 4. DC block after asymmetric saturation
    dcBlocker_.process(buffer, numSamples);
}
```

**2x oversampling** is the practical limit for real-time - higher factors add transient smear that can be more audible than the aliasing they prevent.

### DC Blocking

Always apply DC blocking (~5-20Hz highpass) after:
- Asymmetric saturation (tube, diode curves)
- Rectification
- Any processing that can introduce DC offset

```cpp
// Simple one-pole DC blocker
class DCBlocker {
    float x1_ = 0.0f, y1_ = 0.0f;
    float coeff_ = 0.995f;  // ~15Hz at 44.1kHz
public:
    float process(float x) noexcept {
        float y = x - x1_ + coeff_ * y1_;
        x1_ = x; y1_ = y;
        return y;
    }
};
```

### Feedback Path Safety

Feedback exceeding 100% MUST be limited to prevent runaway oscillation:

```cpp
// In feedback network
float feedbackSample = delayOutput * feedbackAmount_;
if (feedbackAmount_ > 1.0f) {
    // Soft limit feedback signal
    feedbackSample = std::tanh(feedbackSample);
}
```

### Parameter Smoothing Formula

Use one-pole smoothing to prevent zipper noise:

```cpp
// Calculate coefficient once when sample rate changes
// smoothTimeMs = 5-20ms is typical
float smoothCoeff = std::exp(-2.0f * kPi / (smoothTimeMs * 0.001f * sampleRate));

// In process loop
smoothedValue_ = target + smoothCoeff * (smoothedValue_ - target);
```

## Performance Budgets

| Component Type | CPU Target | Memory |
|---------------|------------|--------|
| Layer 1 primitive | < 0.1% per instance | Minimal |
| Layer 2 processor | < 0.5% per instance | Pre-allocated |
| Layer 3 system | < 1% per instance | Fixed buffers |
| Full plugin | < 5% total | 10s delay @ 192kHz max |

- Profile in **Release builds** only
- Test at 44.1kHz stereo for CPU targets
- Maximum delay buffer: 10 seconds at 192kHz = 1,920,000 samples

## Testing Requirements

- All DSP in `src/dsp/` must have corresponding tests in `tests/unit/`
- Tests must pass before committing
- Run tests: `ctest --test-dir build/tests -C Debug --output-on-failure`

```cpp
// Test pattern for DSP functions
TEST_CASE("Function does X", "[dsp][category]") {
    // Arrange
    std::array<float, N> input = {...};
    std::array<float, N> expected = {...};

    // Act
    Iterum::DSP::function(input.data(), input.size(), params);

    // Assert
    for (size_t i = 0; i < N; ++i) {
        REQUIRE(input[i] == Approx(expected[i]));
    }
}
```

## Test-First Development Enforcement (MANDATORY)

**CRITICAL**: This section describes non-negotiable workflow requirements for ALL implementation tasks.

### Pre-Task Context Check

Before starting ANY implementation task, you MUST:

1. **Check if `specs/TESTING-GUIDE.md` is in your current context window**
2. **If NOT in context**: Read the file IMMEDIATELY before proceeding
3. This check MUST appear as an explicit todo item: "Verify TESTING-GUIDE.md is in context (ingest if needed)"

This is REQUIRED because context compaction may have removed the testing guide from your working memory.

### Test-First Workflow

For every implementation task, the todo list MUST include these explicit items IN ORDER:

```
1. [ ] Verify TESTING-GUIDE.md is in context (ingest if needed)
2. [ ] Write failing tests for [feature name]
3. [ ] Implement [feature name] to make tests pass
4. [ ] Verify all tests pass
5. [ ] Commit completed work
```

**NEVER skip steps 1, 2, or 5.** These are checkpoints, not optional guidelines.

### Why This Matters

- **Step 1** ensures testing patterns are fresh in context
- **Step 2** (tests first) catches design issues early and documents expected behavior
- **Step 5** (commit) creates save points and ensures work isn't lost

### Example Todo List for DSP Feature

```
1. [x] Verify TESTING-GUIDE.md is in context (ingest if needed)
2. [ ] Write failing tests for dbToGain function
3. [ ] Implement dbToGain to make tests pass
4. [ ] Write failing tests for gainToDb function
5. [ ] Implement gainToDb to make tests pass
6. [ ] Verify all tests pass
7. [ ] Commit completed work
```

### Enforcement

If you find yourself writing implementation code without corresponding test files already created, STOP and write the tests first. This is a constitution-level requirement (Principle XII).

## Completion Honesty Enforcement (MANDATORY)

**CRITICAL**: This section describes non-negotiable requirements for claiming ANY spec is complete. Violation of these rules destroys user trust.

### Before Claiming Completion

Before you EVER claim a spec is "done" or "complete", you MUST:

1. **Review EVERY FR-xxx requirement** in the spec and verify your implementation meets it
2. **Review EVERY SC-xxx success criterion** and verify measurable targets are achieved
3. **Search your implementation for cheating patterns**:
   - `// placeholder` or `// TODO` comments
   - Test thresholds that differ from spec requirements
   - Features quietly removed from scope

### Forbidden Cheating Patterns

**You MUST NEVER do these and claim completion:**

| Cheating Pattern | Example | Why It's Wrong |
|-----------------|---------|----------------|
| Relaxing test thresholds | Spec says "-3dB flatness", test accepts "-10dB" | Tests pass but requirement fails |
| Placeholder values | "// FIR coefficients need proper design" | Implementation is incomplete |
| Scope reduction without declaration | Removing 2 of 5 quality modes | User expects all modes |
| Vague success claims | "Tests pass" when tests were weakened | Hides non-compliance |

### Mandatory Compliance Table

Every spec completion report MUST include this table:

```markdown
## Implementation Compliance

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001: [name] | ✅ MET | [Test name] verifies [what] |
| FR-002: [name] | ❌ NOT MET | [Reason for failure] |
| SC-001: [name] | ⚠️ PARTIAL | Achieves X of Y target |
```

**Status Definitions:**
- ✅ MET: Fully satisfied with evidence
- ❌ NOT MET: Not satisfied (honest failure)
- ⚠️ PARTIAL: Partially met with documented gap
- 🔄 DEFERRED: Moved to future work with explicit user approval

### Self-Check Before Completion

Ask yourself these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

### What To Do If Requirements Aren't Met

If you cannot meet a requirement, be HONEST:

```markdown
## Honest Status Report

**Completed:**
- FR-001: Upsampling/downsampling pipeline ✅
- FR-002: IIR mode (Economy quality) ✅

**NOT Completed:**
- FR-015: Passband flatness (-3dB spec) ❌
  - Current: -10dB attenuation
  - Reason: FIR coefficients are placeholder values
  - Fix required: Proper Kaiser window coefficient design

**Recommendation:** This spec is NOT complete. FIR filter design
requires additional work to meet SC-003 (0.1dB flatness).
```

This is a constitution-level requirement (Principle XVI).

## Debugging Discipline (MANDATORY)

**CRITICAL**: This section describes non-negotiable requirements for troubleshooting framework-based solutions. Premature pivoting to alternative approaches is FORBIDDEN.

### The Anti-Pivot Rule

When a framework-based solution (VSTGUI, VST3 SDK, etc.) doesn't work:

1. **DO NOT** immediately try a different approach
2. **DO NOT** assume the framework has a bug
3. **DO NOT** switch to platform-native code
4. **DO** investigate deeply until you understand WHY it's not working

### Required Investigation Before Pivoting

Before proposing ANY alternative approach, you MUST complete this checklist:

1. [ ] **Read debug logs**: Check `%TEMP%\iterum_debug.log` and console output
2. [ ] **Add targeted logging**: Trace actual values through the problematic code path
3. [ ] **Read framework source**: VSTGUI is in `extern/vst3sdk/vstgui4/vstgui/`
4. [ ] **Identify divergence point**: Find the EXACT line where behavior differs from expectation
5. [ ] **Document findings**: Explain what you learned and why the approach cannot work
6. [ ] **Propose alternative**: ONLY with user approval, after steps 1-5 are complete

### Forbidden Pivot Patterns

| Pattern | Why It's Wrong |
|---------|----------------|
| "It didn't work, let me try something else" | You didn't investigate WHY |
| "Maybe VSTGUI has a bug" | VSTGUI works in thousands of plugins - you have a usage error |
| "Let me try a manual workaround" | Workarounds create technical debt and often introduce new bugs |
| "I'll use native Windows code" | Violates cross-platform requirements |
| Trying 3+ approaches without reading logs | Guessing instead of debugging |

### Framework Commitment

When using VSTGUI or VST3 SDK features:

1. These frameworks are designed to solve your problem
2. They are battle-tested across thousands of commercial plugins
3. If something isn't working, **YOU are doing something wrong**
4. Your job is to find WHAT you're doing wrong, not to abandon the framework
5. Custom workarounds are the LAST resort, not the first

### Self-Check Questions

Before switching approaches, ask yourself:

1. Have I read the debug log output from the last test?
2. Can I trace the exact value flow from input to incorrect output?
3. Have I read the framework source for this feature?
4. Do I understand what SHOULD happen vs what IS happening?
5. Have I spent at least 30 minutes investigating?

**If any answer is "no", you cannot pivot to an alternative approach.**

### Lesson Learned

The mode switching incident wasted hours pivoting between:
- `template-switch-control` automatic binding
- Manual `setParamNormalized` with SDK's `toPlain()`
- Native Windows popup menus

None of these approaches were properly debugged. The actual values flowing through the system were never traced. The framework source was never read. This principle exists to prevent repeating this waste.

This is a constitution-level requirement (Principle XII).

## VST-GUIDE.md (MANDATORY)

**CRITICAL**: Before any VSTGUI or VST3 SDK implementation work, you MUST read `specs/VST-GUIDE.md`.

### Pre-Task Requirement

Before starting ANY task involving:
- Parameter registration or handling
- COptionMenu or other VSTGUI controls
- UIViewSwitchContainer or view switching
- Custom UI controls or bindings
- Value normalization or conversion

You MUST:
1. **Read VST-GUIDE.md** to check for documented pitfalls
2. **Ingest it into context** if not already present
3. This check MUST appear as an explicit todo item: "Read VST-GUIDE.md (ingest if needed)"

### Post-Discovery Requirement

When a debugging session reveals non-obvious framework behavior:

1. **Immediately** add findings to VST-GUIDE.md after confirming the fix works
2. Add to the Incident Log section with:
   - Date, symptom, root cause, solution
   - Time wasted (to emphasize importance)
   - Lesson learned
3. Update the version and date at the top of the file

### Quick Reference: Common Pitfalls

| Pitfall | Solution | See Section |
|---------|----------|-------------|
| `toPlain()` returns normalized value | Use `StringListParameter` or `RangeParameter` | §1 |
| Custom `value * 10 + 0.5` formulas | Use `param->toPlain(normalized)` | §1 |
| UIViewSwitchContainer doesn't switch | Check parameter type and template-switch-control | §3 |
| Feedback loops resetting values | VSTGUI handles this - check parameter type | §4 |

### Why This Matters

The mode switching bug wasted hours because we didn't know that basic `Parameter::toPlain()` just returns the input unchanged. This knowledge is now in VST-GUIDE.md. Reading it before starting would have prevented the entire debugging session.

This is a constitution-level requirement (Principle XVII).

## Build Commands

```bash
# Configure (first time)
cmake --preset windows-x64-debug

# Build
cmake --build --preset windows-x64-debug

# Test
ctest --preset windows-x64-debug

# Release build
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

## VSTGUI Notes

- UI definition: `resources/editor.uidesc` (XML)
- Edit visually: Enable inline editor in debug build, right-click plugin UI
- Custom views: Implement in `Controller::createCustomView()`
- Control tags in uidesc must match `ParameterIDs` values

## Debugging Tips

1. **Audio Issues**: Check for allocations in process() - use memory profiler
2. **Crashes on Load**: Verify GUIDs in plugin_ids.h are unique
3. **Parameters Not Working**: Check ID matches in Controller and uidesc
4. **State Not Saving**: Ensure getState/setState read/write same format in Processor AND Controller handles setComponentState

## Quick Reference: What Goes Where

| Task | File(s) |
|------|---------|
| Add parameter | plugin_ids.h → processor.h → processor.cpp → controller.cpp → editor.uidesc |
| Add Layer 0 utility | src/dsp/core/ → tests/unit/core/ |
| Add Layer 1 primitive | src/dsp/primitives/ → tests/unit/primitives/ |
| Add Layer 2 processor | src/dsp/processors/ → tests/unit/processors/ |
| Add Layer 3 system | src/dsp/systems/ → tests/unit/systems/ |
| Add Layer 4 feature | src/dsp/features/ → tests/unit/features/ → processor.cpp |
| Change UI layout | resources/editor.uidesc |
| Add custom UI control | controller.cpp (createCustomView) |
| Change plugin metadata | plugin_ids.h, version.h, CMakeLists.txt |
| Add new source file | CMakeLists.txt (smtg_add_vst3plugin section) |

### Layer Dependency Quick Check

Before adding an `#include`, verify it respects the layer hierarchy:

| Your File Layer | Can Include |
|-----------------|-------------|
| Layer 0 (core/) | Standard library only |
| Layer 1 (primitives/) | Layer 0 |
| Layer 2 (processors/) | Layer 0, 1 |
| Layer 3 (systems/) | Layer 0, 1, 2 |
| Layer 4 (features/) | Layer 0, 1, 2, 3 |

## References

- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VSTGUI Documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- Project Constitution: `.specify/memory/constitution.md`
- Feature Roadmap: `specs/ROADMAP.md` - layered architecture and implementation phases
