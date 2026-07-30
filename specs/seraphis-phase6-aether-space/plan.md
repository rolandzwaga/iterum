# Implementation Plan: Seraphis Phase 6 — Aether Space Engine

**Spec:** `specs/seraphis-phase6-aether-space/spec.md` (reviewed revision, 2026-07-29)
**Roadmap:** `specs/Seraphis-roadmap.md` → Part A → Phase 6 (lines 258–282)
**Ships:** one new Layer 4 header, `dsp/include/krate/dsp/effects/aether_reverb.h`, plus five test TUs.
**Amends:** nothing (RA-1). No existing header is edited.

**Citation discipline.** Every `file:line` below was opened and read in the session that produced this
document. Where the spec's own arithmetic or its closed lists turned out to be off, the discrepancy is
recorded in **§11 Spec deltas** rather than silently followed or silently ignored.

---

## 0. Reading key

| Symbol | Meaning |
|---|---|
| `N` | FDN channel count, `config.numChannels ∈ {8, 16}` |
| `t` | **global** morph position [0,1] (FR-020 coordinate convention) |
| `u` | **per-segment** blend parameter, `u = 2t` (segment 1) or `u = 2t − 1` (segment 2) |
| `S` | Size scale on the delay lengths, `S(v) = 0.25·16^v ∈ [0.25, 4.0]` |
| `m_i` | current, Size-scaled delay length of channel `i`, in samples |
| control grid | absolute 64-sample grid anchored to `sampleCounter_` (FR-005) |

---

## 1. File manifest and build integration

### 1.1 New files

| Path | Contents |
|---|---|
| `dsp/include/krate/dsp/effects/aether_reverb.h` | `class AetherReverb` (Layer 4), header-only |
| `dsp/tests/unit/effects/aether_reverb_test.cpp` | SC-001, 002, 005, 006, 009, 010, 011, 012, 015, 017, 018 |
| `dsp/tests/unit/effects/aether_reverb_matrix_test.cpp` | SC-004 (all six clauses) |
| `dsp/tests/unit/effects/aether_reverb_spectral_test.cpp` | SC-003, 007, 016 |
| `dsp/tests/unit/effects/aether_reverb_perf_test.cpp` | SC-008, `[.perf]` |
| `dsp/tests/unit/effects/aether_reverb_nonfinite_test.cpp` | SC-014 (incl. the FR-009/FR-008 setter-guard clause and `AetherReverb_BloomNoteApi`), `-fno-fast-math` TU |

No `.cpp` is added to `KrateDSP` — the header is fully inline. The one out-of-line symbol it calls,
`processSympatheticBankSIMD`, is already compiled into `KrateDSP` from
`dsp/include/krate/dsp/systems/sympathetic_resonance_simd.cpp` (registered at `dsp/CMakeLists.txt:17`),
and `dsp_effects_tests` already links `KrateDSP` (`dsp/tests/CMakeLists.txt:390-395`). **No CMake change
is needed to reach that kernel.**

### 1.2 CMake edits — exactly four sites

> **`COMPILE_FLAGS` is a single string property.** A source file may therefore appear in **exactly one**
> `set_source_files_properties(... PROPERTIES COMPILE_FLAGS ...)` call — a second call **replaces**, it
> does not append. That is why item 3 below is a *separate* block and not an addition to the existing
> `-O2` cap list, whose property string is not `-O2` alone (see `dsp/tests/CMakeLists.txt:710`).

1. **`dsp/tests/CMakeLists.txt:364-388`** — the `dsp_effects_tests` source list (explicit, not globbed;
   an unlisted file silently drops). Append after `unit/effects/fdn_reverb_test.cpp` (`:387`):

   ```cmake
       unit/effects/aether_reverb_test.cpp
       unit/effects/aether_reverb_matrix_test.cpp
       unit/effects/aether_reverb_spectral_test.cpp
       unit/effects/aether_reverb_perf_test.cpp
       unit/effects/aether_reverb_nonfinite_test.cpp
   ```

   `catch_discover_tests(dsp_effects_tests REPORTER console)` already exists (`:721`); nothing else is
   needed for CTest registration.

2. **`dsp/tests/CMakeLists.txt:425-702`** — the `set_source_files_properties(... COMPILE_FLAGS
   "-fno-fast-math -fno-finite-math-only")` block, guarded by
   `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` (`:424`). Add **only**
   `unit/effects/aether_reverb_nonfinite_test.cpp`, immediately before the closing
   `PROPERTIES` line (`:701`), with the same comment discipline Phases 4 and 5 used (`:690-700`):

   ```cmake
       # Seraphis Phase 6: SC-014 injects NaN/Inf via bit patterns in this TU and needs
       # IEEE semantics to assert on them. ONLY this one of the five Phase 6 TUs is listed;
       # the other four must NOT be — the perf TU especially, since -fno-fast-math would
       # change the figures its baselines are pinned to.
       unit/effects/aether_reverb_nonfinite_test.cpp
   ```

3. **`dsp/tests/CMakeLists.txt`, a NEW block immediately after `:711`, still inside the
   `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` guard** — the GCC `-O2` cap for the two Aether
   audio-rendering TUs. `reverb_test.cpp` and `fdn_reverb_test.cpp` are already capped at `:707-711`
   because *"GCC 13+ `-O3` causes pathological performance regression in the reverb processing loop
   (100x+ slower than `-O2`). This is a known class of GCC auto-vectorization bug."* `AetherReverb` is
   the same shape of loop (an `N`-channel recirculating delay network) and B-1's 60 s always-on
   wall-clock budget is a **requirement**, so a 100× Linux regression is a spec failure, not a nuisance.

   **Do NOT extend the existing block at `:707-711`.** Verified this session, its property string is
   `"-fno-fast-math -fno-finite-math-only -O2"` (`dsp/tests/CMakeLists.txt:710`) — not `-O2` alone.
   Joining that list would compile `aether_reverb_test.cpp` and `aether_reverb_spectral_test.cpp` with
   IEEE semantics on the Clang/GNU legs, which is exactly what item 2 forbids and what R-5 depends on:
   the macOS leg is the **only** leg that actually ships `-ffast-math`, so SC-012's `0 non-finite` /
   `getNonFiniteRecoveryCount() == 0` clause, FR-082's input guard and SC-015 would then never exercise
   the `ITERUM_NOINLINE` + `detail::isNaN`/`isInf` guards in the FP mode the header ships in. That is
   the trap the Phase 5 comment at `:690-700` documents as *"DELIBERATE AND LOAD-BEARING, not an
   omission"*. Write a third, independent call instead:

   ```cmake
       # Seraphis Phase 6: same GCC 13+ -O3 reverb pathology as reverb_test.cpp /
       # fdn_reverb_test.cpp above (an N-channel recirculating delay network), and
       # B-1's 60 s always-on wall budget is a requirement. -fno-fast-math is
       # DELIBERATELY ABSENT here: these two TUs must build in the header's shipping
       # FP mode so the ITERUM_NOINLINE finiteness guards are proved under -ffast-math
       # on the macOS leg (R-5). Only aether_reverb_nonfinite_test.cpp gets IEEE
       # semantics, and it is listed in the block above -- not here, because
       # COMPILE_FLAGS is a single string property and a file may appear in only one
       # set_source_files_properties() call.
       set_source_files_properties(
           unit/effects/aether_reverb_test.cpp
           unit/effects/aether_reverb_spectral_test.cpp
           PROPERTIES COMPILE_FLAGS "-O2"
       )
   ```

   Do **not** add `aether_reverb_perf_test.cpp` (an `-O2` cap would change the figures its baselines are
   pinned to) and do not add `aether_reverb_matrix_test.cpp` (no audio is rendered there).

4. **`dsp/tests/CMakeLists.txt:390-395`** — immediately after the `target_link_libraries(dsp_effects_tests
   …)` call, add

   ```cmake
   # Seraphis Phase 6: compiles AetherReverb's FR-083 fault-injection hook
   # (injectNonFiniteStateForTest, section 7.14). Target-wide, NOT per-source: the
   # hook changes the class definition, so every TU linked into this executable must
   # see the same one or it is an ODR violation. KrateDSP itself never compiles
   # aether_reverb.h (header-only, no .cpp), and dsp_lint_stub is a separate
   # EXCLUDE_FROM_ALL object library (dsp/CMakeLists.txt:200-203), so no other image
   # sees a differently-defined AetherReverb. The shipping build never defines this.
   target_compile_definitions(dsp_effects_tests PRIVATE KRATE_DSP_AETHER_TEST_HOOKS)
   ```

   This site is **not optional** — without it SC-014 clause 3 and FR-083's detection branch have no
   reachable fault (see §7.14 and §8.6, and §11 delta **D-8**).

### 1.3 Optional-but-recommended registration

- **`dsp/lint_all_headers.cpp`** — add `#include <krate/dsp/effects/aether_reverb.h>` to the Layer 4
  block. This is the only TU that gives `./tools/run-clang-tidy.ps1 -Target dsp` visibility of the
  header under the *root* `.clang-tidy` (strict) config (`dsp/CMakeLists.txt:197-200`).
- **`dsp/CMakeLists.txt`** header list (`:170-182` for effects) — add
  `include/krate/dsp/effects/aether_reverb.h`.

**Precedent note, stated so it is a decision rather than an oversight:** Phases 4 and 5 added *neither*
`continuous_body.h` nor `atmosphere_engine.h` to those two lists (verified by grep this session — zero
hits in both files). Following that precedent means Phase 6's header is linted only through the test
TUs, under the *tests* `.clang-tidy`. Recommendation: **add it** — it costs two lines and closes a gap
the two previous phases left open.

### 1.4 Gates that must be run (SC-013)

```
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-float-bit-goldens.js
node tools/lint-nonfinite-symbols.js
node tools/lint-arch-guarded-includes.js
node tools/lint-allocation-operator-overrides.js
node tools/lint-simd-aligned-loadstore.js     # only if §7.13's SIMD lever is taken
```

Plus: `dsp_effects_tests`, `dsp_processors_tests`, `dsp_systems_tests` green **unedited** — the whole of
RA-1's containment claim.

---

## 2. Header layout

### 2.1 Banner (FR-001)

The banner must state, in this order:

1. Layer 4; spec slug `seraphis-phase6-aether-space`; roadmap lines 258–282.
2. The `fdn_reverb.h` line ranges whose topology is **re-derived, not included** (C-1):
   Jot per-line absorption `:576-600`; prime reference lengths `:91`; contiguous power-of-two delay
   sections with per-section mask/offset `:638-689`; Householder matrix `:749-758`; Hadamard FWHT
   `:696-729`; freeze-bypasses-damping-and-DC `:296-322`; DC-blocker `R` derivation `:207`;
   even/odd output tap split and M/S width `:354-371`; equal-power mix `:374-377`.
3. **The matrix sign convention** (C-8): row 0 of `H_N/√N` is negated and the random-orthogonal
   endpoint is forced to `det = −1`, so all three endpoints live in the `det = −1` component of `O(N)`.
   State that the shipped Hadamard therefore differs from `fdn_reverb.h:696-729`'s by that sign, and
   why (`det` is continuous on `O(N)` and takes only `±1`, so no continuous orthogonal path joins the
   two components).
4. The reachable modal density table (FR-013, §7.3).
5. The shimmer loop-time table per `PitchMode` (FR-054, §7.9).
6. The `[8000, 192000] Hz` range and the sub-44.1 kHz shimmer force-disable (N-8, C-6, RA-6).
7. `silence()`'s **non-latching** divergence from `AtmosphereEngine::silence()`
   (`systems/atmosphere_engine.h:636-644`, which latches) — FR-007.
8. FR-072's departure from `FDNReverb`'s `modDepth · 5 %` of the **longest** line (`:631`): per-line,
   `0.5 %`, and the reason (at `S = 4` the longest line is 424 ms, so 5 % of it is 21 ms of excursion
   applied to a 5 ms line).
9. The **prepare-time memory footprint by stage** (§4's table), the note that the largest single
   allocation is configuration-dependent, and that **each shimmer tap pays for all four internal
   shifters** — including the phase vocoder's fixed 4096-point STFT — irrespective of
   `PrepareConfig::shimmerMode` (`processors/pitch_shift_processor.h:1213-1216`,
   `processors/pitch_shift_processor.cpp:295-340`). This is why RA-6's sub-44.1 kHz force-disable saves
   memory and not merely CPU.
10. The **cadence contract**: `OnePoleSmoother`s are advanced once per 64-sample control chunk with
    `advanceSamples`, `spectralSm_` per STFT frame with `advanceSamples(hopSize_)`, and the two
    `LinearRamp`s (`freezeRamp_`, `outputGate_`) **per sample** — `LinearRamp` has no `advanceSamples`,
    and a per-chunk crossfade coefficient is a staircase (§3, §11 delta D-12).
11. That `silence()` and `emergencyClear()` **amortize** the state clear across their fade window
    (§5.3), with the reason (a single-chunk clear of up to ≈5 MiB against a 0.33 ms deadline at
    192 kHz / `maxBlockSamples = 64`).

### 2.2 Includes (FR-002) — downward only

```cpp
#include <krate/dsp/core/audio_constants.h>    // L0  kDenormalGuard  (see §11 delta D-1)
#include <krate/dsp/core/db_utils.h>           // L0  detail::isNaN / isInf / flushDenormal
#include <krate/dsp/core/interpolation.h>      // L0  Interpolation::cubicHermiteInterpolate
#include <krate/dsp/core/math_constants.h>     // L0  kPi, kTwoPi, kHalfPi
#include <krate/dsp/core/random.h>             // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/delay_line.h>   // L1  DelayLine, nextPowerOf2
#include <krate/dsp/primitives/smoother.h>     // L1  OnePoleSmoother, LinearRamp, ITERUM_NOINLINE
#include <krate/dsp/primitives/spectral_buffer.h>            // L1
#include <krate/dsp/primitives/stft.h>                       // L1  STFT, OverlapAdd, WindowType
#include <krate/dsp/processors/breathing_modulator.h>        // L2
#include <krate/dsp/processors/brownian_drift.h>             // L2
#include <krate/dsp/processors/diffusion_network.h>          // L2
#include <krate/dsp/processors/pitch_shift_processor.h>      // L2  PitchMode, PitchShiftProcessor
#include <krate/dsp/processors/tidal_modulator.h>            // L2
#include <krate/dsp/systems/sympathetic_resonance_simd.h>    // L3  free-function kernel ONLY
```

**No other Layer 4 include.** Not `fdn_reverb.h` (C-1), not `reverb.h` (C-2), not `shimmer_delay.h`
(C-5). `node tools/lint-layers.js` gates this; do not rely on inspection.

`window_functions.h` and `pitch_utils.h` appear in FR-002's list but are **not needed**: `WindowType` is
re-exported through `stft.h` (`primitives/stft.h:21`), and no pitch conversion is performed (semitone
values go to `PitchShiftProcessor::setSemitones` as literals). Omit them; an unused include is a
clang-tidy finding.

### 2.3 Public constants

All `static constexpr` members of `AetherReverb`, so tests can name them without magic numbers.
The **salt constants must be public** — SC-017 clause 1a constructs its own `BreathingModulator` and
seeds it with `deriveStreamSeed(config.seed, kBreathSalt)`.

```cpp
// --- cadence / lifecycle ---
static constexpr std::size_t kControlChunkSamples = 64;   // value copied from continuous_body.h:97,
                                                          // harmonic_cloud.h:144, atmosphere_engine.h:269
static constexpr float kSilenceRampMs   = 20.0f;          // FR-007
static constexpr float kFreezeLatchMs   = 50.0f;          // FR-033

// --- geometry ---
static constexpr double kReferenceSampleRate = 48000.0;
static constexpr float  kMinSampleRate = 8000.0f;         // fdn_reverb.h:13, :130
static constexpr float  kMaxSampleRate = 192000.0f;
static constexpr double kShimmerMinSampleRate = 44100.0;  // pitch_shift_processor.h:141
static constexpr float  kMinFullSizeDelaySeconds = 0.45f; // FR-012
static constexpr float  kSizeScaleMin = 0.25f;
static constexpr float  kSizeScaleMax = 4.0f;
static constexpr float  kModExcursionFraction = 0.005f;   // FR-072, per-line 0.5 %
static constexpr std::size_t kInterpMarginSamples = 4;    // cubic Hermite needs y[-1] .. y[+2]

// --- injection / taps ---
static constexpr std::size_t kTapReadCount = 4;           // FR-050
static constexpr float kTapReadNormalisation = 0.25f;     // 1 / kTapReadCount
static constexpr int         kMaxBloomResonators = 32;    // FR-055, Q7 — int, NOT size_t (see below)
static constexpr std::size_t kMaxBloomVoices = 8;
static constexpr float kOrthogonalityTolerance = 1e-5f;   // FR-022
static constexpr float kMorphEpsilon = 1e-6f;             // matrix recompute gate

// --- seed salts (public: SC-017 reconstructs the breath trajectory from these) ---
static constexpr std::size_t kMatrixSalt    = 0;
static constexpr std::size_t kBreathSalt    = 1;
static constexpr std::size_t kTideSalt      = 2;
static constexpr std::size_t kSmearSaltL    = 3;
static constexpr std::size_t kSmearSaltR    = 4;
static constexpr std::size_t kDriftSaltBase = 16;         // channel j uses kDriftSaltBase + j
```

`kMaxBloomVoices = 8` is not spec-pinned; it is the pool bound FR-056's "a bank that is already full
retires its oldest voice" needs a number for. 8 voices × 4 partials saturates `kMaxBloomResonators`
exactly, which is the sizing FR-055's "32 is ≥ 4× the partial count SC-016 clause 3 needs" implies.

**`kMaxBloomResonators` is `int`, deliberately.** It is passed straight through to
`processSympatheticBankSIMD`, whose sixth parameter is `int count`
(`systems/sympathetic_resonance_simd.h:45`). Declaring it `std::size_t` and passing it would be an
implicit narrowing in a call expression — MSVC C4267 / GCC-Clang `-Wconversion` — against the project's
zero-warning gate. `int` also matches the kernel's own pool constants (`kMaxSympatheticResonators`,
`systems/sympathetic_resonance.h:43`; `kSympatheticPartialCount`, `:40`). Where a `std::size_t` is
required (the `count` clamp in `bloomNoteOn`, `getActiveBloomResonatorCount()`'s return) the conversion
is written explicitly with `static_cast`.

### 2.4 `PrepareConfig` (nested, FR-009)

```cpp
struct PrepareConfig {
    std::size_t numChannels = 8;              // admissible: 8 or 16 only (Q3)
    std::size_t maxBlockSamples = 2048;       // [64, 8192]
    float       maxDelaySeconds = 0.50f;      // [0.05, 1.0]
    bool        shimmerEnabled = true;
    PitchMode   shimmerMode = PitchMode::Granular;   // processors/pitch_shift_processor.h:58-63
    bool        bloomEnabled = true;
    bool        spectralDiffusionEnabled = true;     // Q2 — default ON
    std::size_t diffusionFftSize = 1024;      // [256, 4096], std::bit_floor then re-clamp
    std::uint32_t seed = 1;
};
```

Validation follows `AtmosphereEngine::prepare` (`systems/atmosphere_engine.h:404-420`): clamp in place,
snap FFT sizes **down** to a power of two, never reject.

---

## 3. Public API — full signatures

```cpp
namespace Krate { namespace DSP {

class AetherReverb {
public:
    struct PrepareConfig { /* §2.4 */ };

    AetherReverb() noexcept = default;
    ~AetherReverb() noexcept = default;
    AetherReverb(const AetherReverb&) = delete;              // STFT/OverlapAdd delete copy
    AetherReverb& operator=(const AetherReverb&) = delete;   //   (primitives/stft.h:41-44, :210-213)
    AetherReverb(AetherReverb&&) noexcept = default;         // PitchShiftProcessor is movable
    AetherReverb& operator=(AetherReverb&&) noexcept = default;  //   (:125-126)

    // ---- lifecycle -------------------------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;  // FR-003, ONLY non-RT method
    void reset() noexcept;                                                  // FR-006
    void silence() noexcept;                                                // FR-007

    // ---- processing ------------------------------------------------------
    void processStereoBlock(const float* inLeft, const float* inRight,
                            float* outLeft, float* outRight,
                            std::size_t numSamples) noexcept;               // FR-004

    // ---- control table (FR-009) -----------------------------------------
    void setSize(float v) noexcept;                    // 0..1     default 0.50
    void setDensity(float v) noexcept;                 // 0..1     default 0.70
    void setDecaySeconds(float seconds) noexcept;      // 0.5..60  default 4.0
    void setFreeze(bool on) noexcept;                  //          default false
    void setDimensionality(float v) noexcept;          // 0..1     default 0.35
    void setDamping(float v) noexcept;                 // 0..1     default 0.40
    void setPreDelayMs(float ms) noexcept;             // 0..200   default 0.0
    void setModDepth(float v) noexcept;                // 0..1     default 0.25
    void setModSmoothness(float v) noexcept;           // 0..1     default 0.60
    void setShimmerOctaveSend(float v) noexcept;       // 0..1     default 0.0
    void setShimmerFifthSend(float v) noexcept;        // 0..1     default 0.0
    void setBloomSend(float v) noexcept;               // 0..1     default 0.0
    void setBloomDecay(float v) noexcept;              // 0..1     default 0.50  -> Q 20..400
    void setSpectralDiffusion(float v) noexcept;       // 0..1     default 0.0
    void setSizeBreathDepth(float v) noexcept;         // 0..1     default 0.20
    void setDimensionalityTideDepth(float v) noexcept; // 0..1     default 0.20
    void setWidth(float v) noexcept;                   // 0..1     default 1.0
    void setMix(float v) noexcept;                     // 0..1     default 0.35
    void setSeed(std::uint32_t seed) noexcept;         //          default 1

    // ---- harmonic-bloom note API (FR-056, RA-7) -------------------------
    void bloomNoteOn(std::int32_t voiceId, const float* partialHz,
                     std::size_t count) noexcept;
    void bloomNoteOff(std::int32_t voiceId) noexcept;

    // ---- introspection (FR-086) — all const, allocation-free,
    //      never called from process() -----------------------------------
    [[nodiscard]] bool        isPrepared() const noexcept;                 // FR-085
    [[nodiscard]] bool        isFrozen() const noexcept;                   // FR-037
    [[nodiscard]] bool        isShimmerActive() const noexcept;            // RA-6
    [[nodiscard]] float       getMatrixOrthogonalityError() const noexcept;// FR-027
    [[nodiscard]] float       getEffectiveDelayLengthSamples(std::size_t channel) const noexcept;
    [[nodiscard]] float       getModalDensityPerHz() const noexcept;       // FR-013
    [[nodiscard]] float       getMaxSizeScale() const noexcept;            // FR-012
    [[nodiscard]] float       getCurrentMorphPosition() const noexcept;    // FR-023
    [[nodiscard]] float       getStateEnergy() const noexcept;             // FR-086 Q8
    [[nodiscard]] std::size_t getActiveBloomResonatorCount() const noexcept;
    [[nodiscard]] std::size_t getNonFiniteRecoveryCount() const noexcept;  // FR-083
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;          // FR-084
    void copyCurrentMatrix(float* dstRowMajor, std::size_t n) const noexcept;
    void applyCurrentMatrix(const float* in, float* out) const noexcept;

    // ---- prepare-time linear algebra, PUBLIC so SC-004 clause 6 can test it
    //      without friend-declaring the test (see §11 delta D-2) ----------
    /// Real-Schur reduction of R in SO(n): R == V * B(theta) * V^T, with V
    /// orthogonal and B block-diagonal in 2x2 rotations. n must be even and
    /// <= 16. Writes V row-major (n*n), thetas (n/2 entries).
    /// Returns false if R is not numerically in SO(n).
    [[nodiscard]] static bool schurReduceSO(const float* rRowMajor, std::size_t n,
                                            float* vRowMajor, float* thetas) noexcept;

#if defined(KRATE_DSP_AETHER_TEST_HOOKS)
    // ---- FR-083 fault injection — TEST BUILDS ONLY (§1.2 item 4, §7.14, §11 D-8)
    /// Writes a bit-pattern NaN into filterState_[0] — the exact array FR-083's
    /// control-grid sweep tests. This is the ONLY way to make internal state
    /// non-finite: every input path is sealed (FR-082 replaces non-finite input
    /// with 0.0f, every setter falls back to its default, bloomNoteOn clamps every
    /// partial before coefficient computation) and FR-025 + FR-032 make the
    /// unfrozen loop structurally non-expansive, so no legal call sequence can
    /// drive the state to Inf. Without this hook SC-014 clause 3 is unimplementable
    /// and FR-083's detect->emergencyClear->count branch is dead code.
    ///
    /// @pre MUST be called at a control-chunk boundary, i.e. between
    ///      processStereoBlock() calls whose cumulative sample count is a multiple
    ///      of kControlChunkSamples. The FR-083 sweep runs at the TOP of the next
    ///      control step, before any sample of that chunk is rendered, so the fault
    ///      is caught before it can reach the output. Called mid-chunk, up to 63
    ///      non-finite samples reach the wet path — outside FR-083's contract and
    ///      not what SC-014 clause 1 asserts against.
    /// @note Absent from the shipping build. Never called from process().
    void injectNonFiniteStateForTest() noexcept;
#endif
};

}} // namespace Krate::DSP
```

**Setter contract, uniform for every row above.** Each setter runs `clamp(isFinite(x) ? x : <default>,
lo, hi)` and writes a smoother target (or a raw member where the table says "Smoothing: none"). The
private `isFinite` helper is `ITERUM_NOINLINE` and composes `detail::isNaN` / `detail::isInf`
(`core/db_utils.h:54`, `:175`) — the exact pattern `AtmosphereEngine` uses
(`systems/atmosphere_engine.h:766`, and `OnePoleSmoother::setTarget`, `primitives/smoother.h:168`).
`ITERUM_NOINLINE` comes in with `smoother.h` and is **defined at `primitives/smoother.h:39-45`** — not
in `envelope_utils.h`. (Verified this session: `smoother.h`'s only krate include is `core/db_utils.h`
at `:28`, so it never sees `envelope_utils.h`, whose `:36-44` copy is an `#ifndef`-guarded duplicate.
`systems/atmosphere_engine.h:1198` already cites the correct site.) **It is load-bearing, not style** —
without it the guard is inlined and folded away under `-ffast-math` on the macOS leg. No fourth
reimplementation of a bit test (FR-008); `node tools/lint-nonfinite-symbols.js` gates it.

**This contract is tested, not assumed.** §8.6 clause 4 drives every float setter in FR-009's table with
bit-pattern NaN, ±Inf and ±1e9 in the one TU built with IEEE semantics, and asserts both the
default-fallback (via a render fingerprint) and the range clamp. Without that clause the exact code path
R-5 says `-ffast-math` destroys is never exercised anywhere.

**Smoother-initialisation rule (FR-009, binding).** A setter called while `isPrepared()` and before any
sample has been processed since the last `prepare()`/`reset()` calls `snapTo(target)`
(`primitives/smoother.h:263`) instead of `setTarget`. Track with one `bool anySamplesProcessed_`, set
`true` on the first non-zero-length `processStereoBlock`, cleared by `prepare()` and `reset()`.
SC-010 clause 3 and Edge case 24 are unsatisfiable without it.

**Smoother-cadence rule (FR-009, binding).** `OnePoleSmoother::configure(ms, sr)` computes a
**per-`process()`-call** coefficient (`primitives/smoother.h:159-163`). Every **`OnePoleSmoother`** in
this design is read once per control chunk and advanced with `advanceSamples(kControlChunkSamples)`
(`primitives/smoother.h:243`) — **except** `spectralSm_`, which is advanced with
`advanceSamples(hopSize_)` immediately before each frame's read (FR-064). Advancing it once per frame
with `process()` would stretch its 100 ms constant to ~25 s at the default hop.

**Ramp-cadence rule (binding, and the one deliberate exception to the control-chunk cadence).**
The two `LinearRamp`s — `freezeRamp_` and `outputGate_` — are advanced and read **per sample**, inside
`renderSlice`'s sample loop, with `LinearRamp::process()` (`primitives/smoother.h:370-389`). Two
independent reasons, both binding:

1. **`LinearRamp` has no `advanceSamples`.** Verified this session: its complete public API is
   `configure` (`:329`), `setTarget` (`:342`), `getTarget` (`:358`), `getCurrentValue` (`:364`),
   `process` (`:370`), `processBlock` (`:394`), `isComplete` (`:409`), `snapToTarget` (`:414`),
   `snapTo` (`:421`), `reset` (`:434`), `setSampleRate` (`:442`). `advanceSamples` exists only on
   `OnePoleSmoother` (`:243`) and `SlewLimiter`. The spec's Existing-components row (spec.md:372) gives
   paired line refs for every `LinearRamp` method *except* `advanceSamples`, which it cites once at
   `:243` — an `OnePoleSmoother`-only method. RA-1 forbids adding one to `smoother.h`.
2. **A per-chunk advance would ship a staircase, not a crossfade.** Both ramps are used as crossfade
   coefficients. `kSilenceRampMs = 20 ms` at 48 kHz is 960 samples = 15 chunks, i.e. ~6.7 % (~0.6 dB)
   gain steps applied by §6.3 step F to the **summed dry+wet** output; `kFreezeLatchMs = 50 ms` is
   2400 samples ≈ 37 chunks, ~2.7 % per step, and §7.7 step 2 lerps the *delay read position* on the
   same grid, stepping the read pointer every 64 samples. That contradicts §5.3's own premise ("a hard
   clear under a non-zero mix would click") and is asserted against directly by SC-015's **0**
   `ClickDetector` detections across `silence()` + resumption and five freeze cycles.

`LinearRamp::process()` is a pure per-sample recurrence (`current_ += increment_`, overshoot clamp,
`flushDenormal`) with **no block-boundary state**, so SC-011's 1e-6 partition-invariance argument in
§6.1 / R-4 is unaffected: the ramp value at absolute sample `n` is a function of `n` alone. The ramp
*targets* are still set on the control grid.

---

## 4. Internal state layout

All arrays are sized for the maximum `N = 16` and `alignas(32)`, the SoA discipline
`FDNReverb` uses (`effects/fdn_reverb.h:774-780`).

```cpp
private:
    static constexpr std::size_t kMaxChannels = 16;

    // ---- configuration snapshot ----
    double sampleRate_ = 0.0;  bool prepared_ = false;
    std::size_t numChannels_ = 8, maxBlockSamples_ = 2048;
    std::size_t diffusionFftSize_ = 1024, diffusionHopSize_ = 256;
    bool spectralEnabled_ = true, bloomEnabled_ = true, shimmerAllocated_ = true;
    float maxSizeScale_ = 4.0f;  std::uint32_t seed_ = 1;
    std::uint64_t sampleCounter_ = 0;  bool anySamplesProcessed_ = false;

    // ---- FDN core: ONE contiguous buffer, N power-of-two sections ----
    std::vector<float> delayBuffer_;                       // fdn_reverb.h:638-689 layout
    std::size_t sectionOffset_[kMaxChannels]{}, sectionMask_[kMaxChannels]{},
                writePos_[kMaxChannels]{};
    float refDelaySamples_[kMaxChannels]{};                // reference length x (sr / 48000)
    alignas(32) float effectiveDelay_[kMaxChannels]{};     // current, Size+drift+breath scaled
    alignas(32) float feedbackGain_[kMaxChannels]{};       // Jot, FR-030
    alignas(32) float dampCoeff_[kMaxChannels]{};          // one-pole, FR-031
    alignas(32) float filterState_[kMaxChannels]{};
    alignas(32) float dcBlockX_[kMaxChannels]{}, dcBlockY_[kMaxChannels]{};
    float dcBlockR_ = 0.0f;                                // 1 - 250/sr, fdn_reverb.h:207
    alignas(32) float chanIn_[kMaxChannels]{}, chanOut_[kMaxChannels]{};  // per-sample scratch

    // ---- matrix morph (nested MatrixMorph) ----
    alignas(32) float matrix_[kMaxChannels * kMaxChannels]{};   // the applied M(t)
    MatrixMorph morph_;                                          // §7.5
    float morphPosition_ = 0.35f, lastMorphPosition_ = -1.0f;
    float orthogonalityError_ = 0.0f;

    // ---- input path ----
    DelayLine preDelayL_, preDelayR_;                      // FR-015, 200 ms each
    DiffusionNetwork diffuser_;                            // FR-040
    std::vector<float> preScratchL_, preScratchR_,         // kControlChunkSamples each
                       diffScratchL_, diffScratchR_;

    // ---- shimmer (nested ShimmerTap x2) ----
    PitchShiftProcessor shifterOctave_, shifterFifth_;     // only when shimmerAllocated_
    std::vector<float> tapSumScratch_;                     // 64, accumulated this chunk
    std::vector<float> shimmerOutOctave_, shimmerOutFifth_;// 64, injected NEXT chunk
    float shimmerShelfStateOct_ = 0.0f, shimmerShelfStateFifth_ = 0.0f;

    // ---- harmonic bloom (nested BloomBank) ----
    alignas(32) float bloomY1_[kMaxBloomResonators]{}, bloomY2_[kMaxBloomResonators]{};
    alignas(32) float bloomCoeff_[kMaxBloomResonators]{}, bloomRSq_[kMaxBloomResonators]{};
    alignas(32) float bloomGain_[kMaxBloomResonators]{}, bloomEnv_[kMaxBloomResonators]{};
    float bloomFreq_[kMaxBloomResonators]{};
    std::int32_t bloomOwner_[kMaxBloomResonators]{};       // voiceId, -1 = free
    bool bloomDriven_[kMaxBloomResonators]{};              // false => released, ringing down
    std::int32_t bloomVoiceId_[kMaxBloomVoices]{};
    std::uint64_t bloomVoiceAge_[kMaxBloomVoices]{};
    std::size_t bloomActiveCount_ = 0;
    float bloomReleaseCoeff_ = 0.0f, bloomGuardScale_ = 1.0f, bloomShelfState_ = 0.0f;

    // ---- spectral diffusion ----
    STFT stftL_, stftR_;  OverlapAdd olaL_, olaR_;  SpectralBuffer specL_, specR_;
    Xorshift32 smearRngL_{1}, smearRngR_{1};
    std::vector<float> wetFifoL_, wetFifoR_;               // ring, >= fftSize + maxBlockSamples
    std::size_t wetFifoRead_ = 0, wetFifoWrite_ = 0, wetFifoCount_ = 0;
    DelayLine dryAlignL_, dryAlignR_;                      // FR-062

    // ---- life modulators (6 at N=8, 10 at N=16 — FR-006) ----
    BreathingModulator breath_;  TidalModulator tide_;
    BrownianDrift drift_[kMaxChannels / 2];

    // ---- smoothers (FR-009) ----
    OnePoleSmoother sizeSm_, densitySm_, decaySm_, dimSm_, dampSm_, preDelaySm_,
                    modDepthSm_, shimmerOctSm_, shimmerFifthSm_, bloomSendSm_,
                    bloomDecaySm_, spectralSm_, widthSm_, mixSm_;
    LinearRamp freezeRamp_;                                // 0 = running, 1 = frozen
    LinearRamp outputGate_;                                // silence() / recovery fade
    bool freezeTarget_ = false;
    enum class GateState : std::uint8_t { Open, FadingOut, FadingIn } gate_ = GateState::Open;

    // ---- amortized state clear (silence() / emergencyClear(), §5.3 + §7.14) ----
    bool        clearPending_ = false;       // while true: delay writes are literal 0, wet is literal 0
    std::size_t clearCursor_ = 0;            // float index into delayBuffer_
    std::size_t clearStage_ = 0;             // index into the deferred sub-object reset() list
    std::size_t clearQuotaFloats_ = 0;       // per-control-chunk slab, sized at prepare

    std::size_t nonFiniteRecoveries_ = 0;
```

**Memory (48 kHz, `maxDelaySeconds = 0.5`, `maxSizeScale_ = 4.0`).** Per channel the section is
`nextPowerOf2(ceil(ref_i · S_max · (1 + 0.005)) + 4)`:

| `N` | `delayBuffer_` floats | bytes |
|---|---|---|
| 8 | 110 592 | 432 KiB |
| 16 | 249 856 | 976 KiB |

(Section sizes at `N = 8`: `4096, 8192, 8192, 8192, 16384, 16384, 16384, 32768`. At `N = 16`:
one 4096, six 8192, six 16384, three 32768.)

At 192 kHz both figures ×4 (Edge case 19).

**`delayBuffer_` is NOT the single largest allocation at the shipped default configuration.** The
prepare-time footprint has three parts, and at `N = 8`, 48 kHz, `diffusionFftSize = 1024`, shimmer on,
the shimmer stage is the biggest of them:

| stage | what is allocated | floats (default cfg) | ≈ bytes |
|---|---|---|---|
| **Delay network** | `delayBuffer_` (§4 table) | 110 592 | 432 KiB |
| | `preDelayL_/R_` — `nextPowerOf2(0.2·sr + 1)` each (`primitives/delay_line.h:267-277`) | 2 × 16 384 | 128 KiB |
| | `dryAlignL_/R_` — `nextPowerOf2(fftSize + 1)` each | 2 × 2 048 | 16 KiB |
| **Spectral stage** | `stftL_/R_` — `inputBuffer_.resize(fftSize·8)` (`primitives/stft.h:77`) + window + frame scratch | 2 × ≈ 10 k | ≈ 80 KiB |
| | `olaL_/R_` — `outputBuffer_.resize(fftSize·2)` (`:266`) + `ifftBuffer_.resize(fftSize)` (`:269`) | 2 × 3 072 | 24 KiB |
| | `specL_/R_` + `wetFifoL_/R_` | 2 × ≈ 5 k | ≈ 40 KiB |
| **Shimmer taps** | `shifterOctave_` + `shifterFifth_`, **each a full `PitchShiftProcessor`** | see below | **≈ 0.8–1.0 MiB** |

**Each shimmer tap pays for all four internal shifters, irrespective of `PrepareConfig::shimmerMode`.**
`PitchShiftProcessor::prepare` unconditionally prepares `simpleShifter`, `granularShifter`,
`phaseVocoderShifter` **and** `pitchSyncShifter` (`processors/pitch_shift_processor.h:1213-1216`), and
`PhaseVocoderPitchShifter::prepare` (`processors/pitch_shift_processor.cpp:295-340`) alone allocates,
at its fixed `kFFTSize = 4096` / `kHopSize = 1024` (`pitch_shift_processor.h:826-827`): an `STFT`
(`inputBuffer_` = 32 768 floats = 128 KiB), an `OverlapAdd` (48 KiB), two `SpectralBuffer`s, five
`numBins = 2049` arrays (`prevPhase_`, `synthPhase_`, `magnitude_`, `frequency_`, `expectedPhaseInc_`),
an `outputBuffer_` and an `inputBuffer_` of `kFFTSize·4` floats each (64 KiB each), plus the formant
preserver's two envelope arrays — **≳ 0.36 MiB per tap from the phase vocoder alone**, before the
granular / pitch-sync / simple buffers. Selecting `PitchMode::Granular` (the default) does not avoid it.

Two consequences the header must state next to `maxDelaySeconds`:

- The *largest single* allocation is configuration-dependent: `delayBuffer_` only overtakes the shimmer
  stage at the large geometries (it reaches ≈ 3.81 MiB at `N = 16`, 192 kHz, `maxDelaySeconds = 0.5`).
- **RA-6's sub-44.1 kHz force-disable saves real memory, not just CPU** — `shimmerAllocated_ == false`
  drops the whole ≈ 0.8–1.0 MiB shimmer column, which is why §5.1 step 3 prepares nothing rather than
  preparing and muting.

The exact per-stage byte figures are measured once during step 1 of §9 and transcribed into the header
and `compliance.md`; the table above is the derivation, not a substitute for the measurement.

---

## 5. Lifecycle contracts

### 5.1 `prepare(double sampleRate, const PrepareConfig& config)` — FR-003

Order matters; each step depends on the previous.

1. `sampleRate_ = clamp(sampleRate, 8000.0, 192000.0)` — **no 44.1 kHz floor** (C-6, RA-6).
2. Validate config: `numChannels_ = (config.numChannels == 16) ? 16 : 8`;
   `maxBlockSamples_ = clamp(.., 64, 8192)`; `maxDelaySeconds = clamp(.., 0.05f, 1.0f)`;
   `diffusionFftSize_ = clamp(std::bit_floor(clamp(cfg, 256, 4096)), 256, 4096)`;
   `diffusionHopSize_ = diffusionFftSize_ / 4` (75 % overlap, FR-060);
   `seed_ = config.seed`.
3. `shimmerAllocated_ = config.shimmerEnabled && sampleRate_ >= 44100.0`.
   **If false, no `PitchShiftProcessor` is prepared and no tap scratch is allocated** — RA-6.
4. Reference lengths: `refDelaySamples_[i] = kRefDelays<N>[i] * sampleRate_ / 48000.0`.
5. `maxSizeScale_` (FR-012, §7.2), then the per-channel section sizes and one
   `delayBuffer_.assign(total, 0.0f)`.
6. `dcBlockR_ = 1.0f - 250.0f / sampleRate_` (`fdn_reverb.h:207`).
7. `preDelayL_/R_.prepare(sampleRate_, 0.200f)` (`primitives/delay_line.h` `prepare`, buffer rounded to
   `nextPowerOf2(maxDelaySamples_+1)`).
8. `diffuser_.prepare(static_cast<float>(sampleRate_), maxBlockSamples_)`
   (`processors/diffusion_network.h:242`; `maxBlockSize` is explicitly unused, `:243`).
   Leave `setModDepth`/`setModRate` at their defaults `0 / 1 Hz` (`:226`, `:230`) — FR-042.
9. Shimmer (only if `shimmerAllocated_`): `shifterOctave_.prepare(sampleRate_, kControlChunkSamples)`,
   `setMode(config.shimmerMode)`, `setSemitones(12.0f)`, `setCents(0.0f)`; ditto fifth at `7.0f`.
   `maxBlockSize = 64` satisfies the documented `[1, 8192]` precondition
   (`processors/pitch_shift_processor.h:139-142`) and is all FR-050's fixed cadence needs.
   Allocate `tapSumScratch_`, `shimmerOutOctave_`, `shimmerOutFifth_` at 64 each.
10. Bloom (only if `config.bloomEnabled`): zero every array;
    `bloomReleaseCoeff_ = exp(-1.0f / (0.010f * sr))` — the 10 ms release
    `SympatheticResonance::prepare` uses (`systems/sympathetic_resonance.h:117`).
11. Spectral (only if `config.spectralDiffusionEnabled`):
    `stftL_/R_.prepare(fftSize, hop, WindowType::Hann)` (`primitives/stft.h:58-63`);
    `olaL_/R_.prepare(fftSize, hop, WindowType::Hann, 9.0f, /*applySynthesisWindow=*/true)` —
    **`true` is mandatory at 75 % overlap and forbidden at 50 %** (`primitives/stft.h:225-228`);
    `specL_/R_.prepare(fftSize)`; `wetFifo*_.assign(nextPowerOf2(fftSize + maxBlockSamples_ + 64), 0)`;
    `dryAlignL_/R_.prepare(sampleRate_, static_cast<float>(static_cast<double>(diffusionFftSize_) /
    sampleRate_))` — the cast is **explicit**: `DelayLine::prepare` is `(double, float)`
    (`primitives/delay_line.h:86`), so writing `float(fftSize)/sr` would pass a `double` as the second
    argument and produce C4244 against the zero-warning gate.
12. Life modulators: `breath_.prepare(sr)`, **`breath_.setRate(0.05f)`** — explicit, *not* the class
    default `kDefaultRate = 0.1f` (`processors/breathing_modulator.h:111`), giving exactly a 20 s
    period inside `[kMinRate 0.01, kMaxRate 0.5]` (`:108-110`). Depth and irregularity stay at the class
    defaults `1.0f` / `0.0f` (`:112-113`), so `setSizeBreathDepth` is the *only* depth (FR-070).
    `tide_.prepare(sr)`, **`tide_.setRate(1.0f)`** — again explicit, not the class default `0.5f`
    (`processors/tidal_modulator.h:143`), giving `getBasePeriodSeconds() == kMinPeriod == 30 s`
    (`:125`, `:217-219`) and layer periods `30 / 42.43 / 51.96 s` (`kLayerRatios = {1, √2, √3}`,
    `:149-150`, `:226-229`). Each `drift_[j].prepare(sr)`.
13. Matrix: build `M₀`, `M₁`, `M₂` (§7.4); run the two `schurReduceSO` reductions (§7.5); precompute
    `AV_seg = A_seg · V_seg`.
14. Configure every smoother (`configure(ms, float(sr))`) and `snapTo` its FR-009 default. Configure
    `freezeRamp_.configure(kFreezeLatchMs, float(sr))` and
    `outputGate_.configure(kSilenceRampMs, float(sr))`.
15. **Clear-amortization quota** (§5.3, §7.14): `fadeChunks = max<std::size_t>(1,
    (kSilenceRampMs·sr/1000) / kControlChunkSamples)`;
    `clearQuotaFloats_ = ceilDiv(delayBuffer_.size(), fadeChunks)`. This is what bounds the worst-case
    control chunk during `silence()` / `emergencyClear()`; without it the whole clear (up to ≈ 5 MiB of
    `memset` at `N = 16` / 192 kHz, §4) lands in one 64-sample chunk whose deadline can be 0.33 ms.
16. `reset()`.

`prepare` may be called any number of times and fully reconfigures (Edge case 6). It is the **only**
non-RT-safe method.

### 5.2 `reset()` — FR-006

- **Cleared:** `delayBuffer_`, all `writePos_`, `filterState_`, `dcBlock*`, `preDelayL_/R_.reset()`,
  `diffuser_.reset()`, shifters' `reset()`, all bloom state and voice slots, `stft*.reset()`,
  `ola*.reset()`, `spec*.reset()`, wet FIFO, `dryAlign*.reset()`, `sampleCounter_ = 0`,
  `anySamplesProcessed_ = false`, `freezeRamp_.snapTo(0)`, `freezeTarget_ = false` (so `isFrozen()`
  is `false`), `gate_ = Open`, `outputGate_.snapTo(1)`, `clearPending_ = false`, `clearCursor_ = 0`,
  `clearStage_ = 0`.
- **Re-seeded** with `deriveStreamSeed(seed_, salt)` and then each object's own `reset()`:
  `breath_` (`kBreathSalt`), `tide_` (`kTideSalt`), every `drift_[j]` (`kDriftSaltBase + j`),
  `smearRngL_/R_` (`kSmearSaltL/R`). Each class re-seeds inside its own `reset()`
  (`processors/brownian_drift.h:133`, `:242-247`), but the **derived** seed must be re-applied
  explicitly — that is what makes a post-`reset` render match the original (SC-010 clause 3).
- **NOT re-generated:** the random-orthogonal endpoint `M₂` (FR-021 — `prepare`-time only).
- **Preserved:** every FR-009 control target; each smoother is `snapToTarget()`-ed to it. The morph
  position is restored to *the current Dimensionality target*, not to the 0.35 default.
- Allocates nothing.

### 5.3 `silence()` — FR-007, non-latching

Three-phase, because a hard clear under a non-zero mix would click:

1. `gate_ = FadingOut`; `outputGate_.setTarget(0)`, reached over `kSilenceRampMs = 20 ms` **per sample**
   (§3's ramp-cadence rule). `clearPending_ = true`, `clearCursor_ = 0`, `clearStage_ = 0`.
2. The clear of every audio state (the `reset()` "Cleared" list, **without** touching the sample
   counter, the modulators, the seeds or the control targets) runs **amortized across the fade-out**,
   not in the instant the gate reaches 0 — see below.
3. When `outputGate_.getCurrentValue() == 0` **and** `clearPending_ == false` — both tested at the next
   control step — `gate_ = FadingIn`; `outputGate_.setTarget(1)` over the same 20 ms; then
   `gate_ = Open`. The transitions are evaluated on the control grid, so the gate can sit at 0 for up to
   63 extra samples; that window is silence, so it is inaudible, and it keeps the transition anchored to
   `sampleCounter_` (SC-011).

**Why the clear is amortized (a wall-clock requirement, not a nicety).** The `reset()` "Cleared" list is
large: `delayBuffer_` alone is 432 KiB at `N = 8` / 976 KiB at `N = 16` (§4 table) and ×4 at 192 kHz
(Edge case 19) ⇒ up to ≈ 3.9 MiB, plus `stftL_/R_.reset()` (`inputBuffer_` of `fftSize·8` floats each,
`primitives/stft.h:77`), `olaL_/R_.reset()`, `shifterOctave_/Fifth_.reset()` — each of which resets all
four internal shifters including the phase vocoder's own STFT
(`processors/pitch_shift_processor.h:1227-1238`, `:826-827`) — the wet FIFOs, the two pre-delays, the
two dry-alignment delays and `DiffusionNetwork`'s 16 allpass stages. That is 1–5 MiB of `memset`.
`PrepareConfig::maxBlockSamples` admits 64, so the host deadline can be **1.33 ms at 48 kHz and 0.33 ms
at 192 kHz** — a single-chunk clear can exceed a whole callback budget. CPU budgets are functional
requirements in this project (SC-008), so this burst is bounded rather than hoped about, and SC-008
configuration **(f)** measures the worst clear chunk.

**The amortization, normatively.** While `clearPending_`, each `runControlStep()` consumes exactly one
work unit:

- `std::fill` of the next `clearQuotaFloats_` slab of `delayBuffer_` (quota sized at §5.1 step 15 so the
  whole buffer is covered within the fade window), **and**
- at most one deferred sub-object reset, taken in a fixed order from
  `{preDelayL_, preDelayR_, diffuser_, stftL_, stftR_, olaL_, olaR_, specL_, specR_, wetFifoL_/R_,
  dryAlignL_, dryAlignR_, shifterOctave_, shifterFifth_}` (`clearStage_`). Each is at least an order of
  magnitude smaller than the delay buffer and cannot be split, so one per chunk is the granularity.

The O(N)- and O(`kMaxBloomResonators`)-sized scalar state (`filterState_`, `dcBlockX_/Y_`, `chanIn_`,
`chanOut_`, `writePos_`, the bloom arrays and voice slots, the three shelf states, `tapSumScratch_`,
`shimmerOut*`) is cleared **immediately** in step 1 — it is a few hundred floats.

While `clearPending_`, the render loop writes literal `0.0f` into the delay lines instead of `chanIn_`
and forces the wet contribution to literal `0.0f`. Both are **assignments, not gain multiplies**, so a
value that has not yet been reached by the cursor cannot leak through a `× 0` product. When
`clearCursor_` and `clearStage_` are both exhausted, `clearPending_ = false`. If the fade window is too
short for the quota (only reachable at very low sample rates, where the buffers are correspondingly
small), the fade-out simply holds at 0 for the extra chunks — the sequence is defined by
`clearPending_`, not by a fixed 40 ms.

Total ≈ 40 ms. `silence()` during freeze abandons the latch (`freezeTarget_ = false`,
`freezeRamp_.snapTo(0)`) — Edge case 8. A second call while already fading is idempotent.
**This deliberately diverges from `AtmosphereEngine::silence()`** (`systems/atmosphere_engine.h:636-644`,
which latches and has no resume); the header says so. **The non-latching half is tested** — SC-015's
silence clause (§8.2) requires the wet level after resumption to match a never-silenced reference within
±3 dB, which a latching implementation fails. Counting allocations and clicks alone cannot see it: a
latch that outputs digital zero for the rest of the render scores 0 on both.

---

## 6. Process contract and the control grid

### 6.1 `processStereoBlock` skeleton (FR-004, FR-005)

```cpp
void processStereoBlock(const float* inL, const float* inR,
                        float* outL, float* outR, std::size_t n) noexcept {
    if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) return;  // FR-004
    if (n == 0) return;                                     // no state change, grid does not advance
    if (!prepared_) { std::fill(outL, outL+n, 0.0f); std::fill(outR, outR+n, 0.0f); return; }

    std::size_t done = 0;
    while (done < n) {
        const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
        if (phase == 0) runControlStep();                   // ALWAYS advances by a FULL 64
        const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);
        renderSlice(inL + done, inR + done, outL + done, outR + done, slice, phase);
        sampleCounter_ += slice;
        done += slice;
    }
    anySamplesProcessed_ = true;
}
```

**`runControlStep()` runs only at `phase == 0`, and advances every modulator and smoother by the full
`kControlChunkSamples`, never by the slice length.** This is the single most important structural
decision in the design and SC-011 depends on it: `BreathingModulator::processBlock(n)` is
`advancePhase(n); setTarget(shape); advanceSamples(n)`
(`processors/breathing_modulator.h:209-216`), so splitting 64 into 36 + 28 inserts an extra
`setTarget` and does **not** produce the same state as one call of 64. Anchoring the control step to
`sampleCounter_ % 64 == 0` makes the split invisible. The identical trap applies to `TidalModulator`
(`:250-257`) and `BrownianDrift` (`:194-206`).

`slice ≤ 64` always, so every per-chunk scratch buffer is a fixed 64 samples. A call longer than
`maxBlockSamples_` is therefore *already* handled by the loop and is never a precondition violation
(FR-004, Edge case 3). In-place is safe iff `inL == outL && inR == outR`: the slice's input is copied
into `preScratch*` before anything is written to `out*`.

### 6.2 `runControlStep()` — order is normative

1. `breath_.processBlock(64)`, `tide_.processBlock(64)`, `drift_[j].processBlock(64)` for all `j`.
   (FR-074: unconditional — there is no input-activity gate.)
2. Advance every FR-009 `OnePoleSmoother` with `advanceSamples(64)` and read `getCurrentValue()`.
   `spectralSm_` is **excluded** here — FR-064 advances it per frame with `advanceSamples(hopSize_)`.
3. **Gate / freeze state machine.** Read `freezeRamp_.getCurrentValue()` and
   `outputGate_.getCurrentValue()` (chunk-boundary snapshots, used only for the *decisions* below) and
   run the §5.3 state machine: set the ramp **targets**, advance the amortized clear by one work unit
   when `clearPending_`, and evaluate the `FadingOut → FadingIn → Open` transitions.
   **The two ramps are NOT advanced here** — `LinearRamp` has no `advanceSamples`, and a per-chunk
   advance would turn both crossfades into staircases (§3, ramp-cadence rule). They are advanced per
   sample in §6.3.
4. **Geometry** (§7.2): unless frozen, `sizeCombined = clamp(sizeSm + breathDepth·breath_.get(), 0, 1)`;
   `S = 0.25f · exp2f(4·sizeCombined)`; per channel
   `effectiveDelay_[i] = refDelaySamples_[i]·S` and, for `i ≥ N/2`,
   `+= modDepth · kModExcursionFraction · (refDelaySamples_[i]·S) · drift_[i−N/2].getCurrentValue()`.
   While frozen the whole step is skipped (FR-034) and `effectiveDelay_` keeps its latched values.
5. **Decay/damping** (§7.6): recompute `feedbackGain_[i]` and `dampCoeff_[i]` iff `S` or
   `decaySeconds` or `damping` moved by more than 1e-7 since the last recompute.
6. **Morph** (§7.5): `t = clamp(dimSm + tideDepth·tide_.get(), 0, 1)`; if
   `|t − lastMorphPosition_| > kMorphEpsilon`, materialise `matrix_` and recompute
   `orthogonalityError_`.
7. **Diffuser**: `diffuser_.setSize(sizeCombined·100)`, `diffuser_.setDensity(densitySm·100)`, then
   **`diffuser_.snapSmoothers()`** (`processors/diffusion_network.h:361-369`). The header's own doc
   block (`:347-360`) is explicit that a caller which already smooths on its own control grid must do
   this; without it the network's static fast path (`:534`, `:550`) is permanently defeated and a
   second 10 ms lag sits in series with ours (FR-041).
8. **Shimmer chunk boundary** (§7.9): if `shimmerAllocated_`, call
   `shifterOctave_.process(tapSumScratch_.data(), shimmerOutOctave_.data(), 64)` and the same for the
   fifth; then zero `tapSumScratch_`. The outputs are injected across the **coming** 64 samples —
   one chunk late (Q5).
9. **Bloom** (§7.10): reclaim released slots whose envelope is below
   `kReclaimThresholdLinear = 1.585e-5f` (`systems/sympathetic_resonance.h:52`), recompute the
   `1/√count` factor and the FR-058 guard scale if the bank retuned or `bloomDecay`/`decaySeconds`/
   `size` moved.
10. **Non-finite sweep** (FR-083, §7.14): test `filterState_[0..N)` and `matrix_[i·N+i]`; on detection,
    `emergencyClear()` and `++nonFiniteRecoveries_`.
11. Snapshot the block-rate scalars the render loop holds constant: `width`, `dryGain = cos(mix·π/2)`,
    `wetGain = sin(mix·π/2)` (`fdn_reverb.h:374-377`), `preDelaySamples`, the three send gains, the
    two output-tap scales.

### 6.3 `renderSlice(...)` — per-slice then per-sample

Per slice (`k = 0 .. slice-1`), in this order:

```
A. finiteness guard on inL[k]/inR[k]  -> preScratchL_/R_[k]        (FR-082; fdn_reverb.h:264-265)
B. preDelayL_.write(preScratchL_[k]); preScratchL_[k] = preDelayL_.readLinear(preDelaySamples)
   (same for R, SAME length so the pair stays phase-coherent — FR-015)
C. diffuser_.process(preScratchL_.data(), preScratchR_.data(),
                     diffScratchL_.data(), diffScratchR_.data(), slice)   (FR-040, FR-043)
```

Then per sample `k`:

```
 0. ADVANCE THE TWO RAMPS, PER SAMPLE (§3's ramp-cadence rule — this is what makes the freeze latch
    and the silence gate crossfades rather than 64-sample staircases):
       const float freezeRamp = freezeRamp_.process();     // primitives/smoother.h:370-389
       const float gate       = outputGate_.process();
    Both are pure per-sample recurrences with no block-boundary state, so the value at absolute sample
    n depends on n alone — SC-011's 1e-6 partition-invariance bound is unaffected. Every `freezeRamp`
    below is THIS sample's value, not a chunk snapshot; `gate` is consumed at step F.
    While `clearPending_` (§5.3/§7.14) steps 1-10 still run, but step 10 writes literal 0.0f into the
    delay lines instead of `chanIn_[i]`, and steps 2/D/E's wet result is replaced by literal 0.0f.
 1. read the N delay lines at effectiveDelay_[i]:
       frac == 0 and settled  -> integer  delayRead(i, (size_t)d)
       otherwise              -> Interpolation::cubicHermiteInterpolate(ym1,y0,y1,y2,frac)
                                 (core/interpolation.h:84; the call fdn_reverb.h:669 makes)
 2. OUTPUT TAPS, taken BEFORE damping (fdn_reverb.h:280-292, :356-364):
       wetL = (2/N) * sum(delRead[i] for even i);  wetR = (2/N) * sum(delRead[i] for odd i)
 3. damping one-pole, then Jot gain:
       lp[i]  = dampCoeff_[i]*delRead[i] + (1-dampCoeff_[i])*filterState_[i]; filterState_[i] = lp[i]
       damped = lerp(lp[i], delRead[i], freezeRamp)                    (FR-033 step 3, crossfaded)
 4. DC blocker, crossfaded out by freezeRamp the same way          (FR-016, fdn_reverb.h:311-322)
 5. tapSum = kTapReadNormalisation * sum(chanOut_[i] for i in [N-4, N))          (FR-050)
    tapSumScratch_[chunkIdx] += tapSum                       -> next chunk's shimmer input
 6. bloom (if enabled): processSympatheticBankSIMD(bloomY1_, bloomY2_, bloomCoeff_, bloomRSq_,
       bloomGain_, kMaxBloomResonators, tapSum, &bloomOut, bloomReleaseCoeff_, bloomEnv_)
    bloomReturn = hfShelf(bloomOut) * bloomSendGain * injGain(bloom) * invSqrtCount
                  * bloomGuardScale_ * (1 - freezeRamp)                       (FR-055, FR-058)
 7. shimmer returns: octRet   = hfShelf(shimmerOutOctave_[chunkIdx]) * octSend  * (1 - freezeRamp)
                     fifthRet = hfShelf(shimmerOutFifth_ [chunkIdx]) * fifthSend* (1 - freezeRamp)
                     (injection gain sqrt(2/2) == 1.0, so send IS the injected gain — FR-051)
 8. matrix:  chanIn_ = matrix_ * chanOut_        (dense N x N, 64 MACs at N=8 — FR-024)
 9. gains + injections (the fdn_reverb.h:336-338 ordering):
       chanIn_[i] = chanIn_[i] * lerp(feedbackGain_[i], 1.0f, freezeRamp)
                  + (1 - freezeRamp) * ( injGainIn * (even ? diffL[k] : diffR[k])   // FR-015a
                                       + (i in octaveInject  ? octRet   : 0)
                                       + (i in fifthInject   ? fifthRet : 0)
                                       + (i in bloomInject   ? bloomReturn : 0) )
       + (freezeRamp < 1 ? denormalTickle(i) : 0)                                    (FR-036)
10. write chanIn_[i] to the delay lines
```

Then per slice again:

```
D. width:  mid = 0.5*(wetL+wetR); side = 0.5*(wetL-wetR);
           wetL = mid + width*side; wetR = mid - width*side            (fdn_reverb.h:368-371)
E. spectral diffusion (if enabled): push wetL/R into stftL_/R_, drain frames, pull into wetFifo_,
   read `slice` samples back out; dry goes through dryAlign*_ at exactly fftSize    (§7.11)
F. out[k] = gate[k] * (dryGain*dryDelayed + wetGain*wetProcessed)      (fdn_reverb.h:374-377)
   `gate[k]` is step 0's PER-SAMPLE outputGate_ value, recorded into the slice's scratch during the
   sample loop. Applying a per-chunk snapshot here would put ~0.6 dB steps on the summed dry+wet
   signal every 64 samples during silence()/recovery, which SC-015 asserts against (0 detections).
```

**Cost of the per-sample ramps.** Two `LinearRamp::process()` calls per sample — an add, an overshoot
compare and a denormal flush each — against the `N×N` matrix multiply's 64 MACs at `N = 8` and 256 at
`N = 16`. Negligible, and both short-circuit to a single compare once `current_ == target_`
(`primitives/smoother.h:371-373`), which is the steady state for all but 20–50 ms per transition.

---

## 7. Algorithms

### 7.1 Reference delay tables (FR-011)

Both tables ship (Q3). They are **distinct primes** (hence pairwise coprime), **ascending** (so channel
index order *is* length order, which FR-050's "four longest" subset and FR-018's even/odd split both
rely on), exponentially spaced, and span 20.15 ms … 105.98 ms at 48 kHz — a 5.26 : 1 ratio, matching
`FDNReverb`'s `{149 … 797}` spread (`effects/fdn_reverb.h:91`) at ~5× the absolute length.

```cpp
static constexpr std::size_t kRefDelays8[8] = {
    967, 1217, 1543, 1973, 2477, 3163, 4001, 5087 };
static constexpr std::size_t kRefDelays16[16] = {
    967, 1087, 1201, 1361, 1511, 1693, 1879, 2099,
   2347, 2621, 2927, 3271, 3659, 4079, 4561, 5087 };
```

*Derivation, so the tables are re-derivable rather than magic:* geometric from 960 samples (20 ms at
48 kHz) with ratio `r = 5.3^(1/(N−1))`, each term replaced by the nearest prime not already used.

*The arithmetic these tables have to satisfy, and do:*

| quantity | value | required by |
|---|---|---|
| shortest at `S = 0.25` | `967·0.25 = 242` samples = **5.0 ms** | FR-012 "5 ms … 424 ms" |
| longest at `S = 4.0` | `5087·4 = 20 348` samples = **423.9 ms** | FR-012 |
| + 0.5 % excursion + 4 | **20 454** samples = **426.1 ms** | FR-009's `maxDelaySeconds` derivation (≈426.2 ms) |
| `Σm_i` at `S = 1`, `N = 8` | 20 428 → `D = 0.4256` modes/Hz | FR-013 |
| `Σm_i` at `S = 1`, `N = 16` | 40 350 → `D = 0.8406` modes/Hz | FR-013 |
| SC-003 window at `size = 1` | `W = 3·423.9 ms = 1.27 s` | SC-003's own stated figure ✓ |

Ship a compile-time pairwise-coprimality check (`static_assert` over a `constexpr` gcd fold) **and**
SC-003's companion runtime assertion, so a future table edit cannot silently break FR-011.

### 7.2 Size (FR-012, FR-014)

```
S(v) = 0.25f * exp2f(4.0f * v)          // == 0.25 * 16^v ; S(0)=0.25, S(0.5)=1, S(1)=4
```

`exp2f` rather than `powf` — one intrinsic, evaluated once per control chunk.

`maxSizeScale_` at `prepare`:

```
maxDelaySamples = maxDelaySeconds * sr
longestRef      = refDelaySamples_[N-1]
maxSizeScale_   = min(4.0f, (maxDelaySamples - kInterpMarginSamples)
                            / (longestRef * (1.0f + kModExcursionFraction)))
```

At `maxDelaySeconds = 0.45` and 48 kHz that gives `(21600−4)/(5087·1.005) = 4.22 ≥ 4`, so
`kMinFullSizeDelaySeconds = 0.45f` carries ~5.6 % margin over the true threshold (≈0.4262 s). At the
range minimum 0.05 it gives **≈0.47**, which is Edge case 10's expected `getMaxSizeScale()` and the only
place the clamp path is exercised — every success criterion asserts `getMaxSizeScale() == 4.0f` first
(P-2).

Section sizing uses the **clamped** `maxSizeScale_`, so a clamped configuration allocates only what it
can reach.

### 7.3 Modal density (FR-013)

`getModalDensityPerHz()` returns `Σᵢ effectiveDelay_[i] / sampleRate_`, computed from the **current,
Size-scaled** lengths — never from the `prepare`-time geometry. SC-003 clause 3(a) exists precisely to
catch a stale-accessor implementation, and it recomputes the same sum independently from
`getEffectiveDelayLengthSamples(i)`.

Header table (48 kHz):

| `N` | `S = 0.25` | `S = 1.0` | `S = 4.0` |
|---|---|---|---|
| 8 | 0.106 | 0.426 | 1.702 |
| 16 | 0.210 | 0.841 | 3.363 |

### 7.4 The three matrix endpoints (FR-020, FR-021, C-8)

All three are pinned into the `det = −1` component of `O(N)`. **This is normative, not cosmetic:**
`det` is continuous on `O(N)` and takes only `±1`, so endpoints in different components cannot be
joined by *any* continuous orthogonal path.

- **`M₀` (`t = 0`, Householder / "2D plate")** — `M₀ = I − (2/N)·J`, i.e. `x[i] -= (2/N)·Σx`.
  This is `FDNReverb::applyHouseholder` (`effects/fdn_reverb.h:749-758`, `x[i] -= sum*0.25f` at `N=8`).
  Writing `u₀ = 𝟙/√N`, `M₀ = I − 2u₀u₀ᵀ` is a **single reflection**, so `det(M₀) = −1`.
  It is **dense** (diagonal `0.75`, every off-diagonal `−0.25` at `N = 8`) — the banner must not call it
  sparse.
- **`M₁` (`t = 0.5`, sign-corrected Hadamard / "3D hall")** — `M₁ = D · H_N/√N` with
  `D = diag(−1, 1, …, 1)`. Build `H_N/√N` with the same 3-stage (4-stage at `N=16`) FWHT butterfly
  `FDNReverb::applyHadamard` uses (`:696-729`), then negate row 0. Row negation is left-multiplication
  by an orthogonal `±1` diagonal, so `M₁` stays exactly orthogonal while `det` flips `+1 → −1`
  (verified in the spec at both `N = 8` and `N = 16`).
- **`M₂` (`t = 1`, seeded random-orthogonal / "N-D impossible")** — modified Gram–Schmidt over an `N×N`
  matrix of `Xorshift32::nextFloat()` draws (`core/random.h:59`) seeded
  `deriveStreamSeed(config.seed, kMatrixSalt)` (`:102`). Then compute `det(Q)` by LU with partial
  pivoting and **negate one column whenever `det(Q) > 0`**. If any pivot norm falls below `1e-4`
  (numerically rank-deficient), redraw — bounded at 8 attempts, `prepare`-time only.
  `M₂` is regenerated by `prepare` **and by nothing else**; `setSeed` does not touch it (FR-021,
  FR-073, Edge case 23).

### 7.5 The real-Schur geodesic (FR-022, Q4) — the one shipped morph mechanism

**Contract.** For each of the two segments (`A₁ = M₀, B₁ = M₁`; `A₂ = M₁, B₂ = M₂`), at `prepare`:

```
R      = A_segᵀ · B_seg                     // det(R) = (-1)(-1) = +1, so R ∈ SO(N)
R      = V · B(θ) · Vᵀ                      // real Schur form: V orthogonal, B block-diagonal 2x2 rotations
AV_seg = A_seg · V                          // precomputed once
```

and at each control chunk:

```
M(u) = AV_seg · B(u·θ) · Vᵀ
```

`M(u)` is **exactly orthogonal at every `u`** (a product of orthogonal factors; there is no
re-orthonormalisation step that could fail), hits both endpoints exactly (`B(0) = I`, `B(θ) = R` so
`M(1) = A·R = A·AᵀB = B`), is continuous, and traverses every invariant 2-plane at a **constant angular
rate** — which is why the path is canonical and the shipped Dimensionality axis is re-derivable.

**Cost.** `B(u·θ)` is block-diagonal, so `AV·B` touches two columns per block: `4N²` MACs (256 at
`N = 8`). The second product `(AV·B)·Vᵀ` is `N³` (512). ~768 MACs once per 64 samples ≈ 12 MAC/sample —
negligible against SC-008's 533 333 ns/block. Gate the recompute on
`|t − lastMorphPosition_| > kMorphEpsilon`.

**`schurReduceSO(R, n, V, thetas)` — the hand-written reduction (there is no LAPACK in this repo).**
It exploits the fact that an orthogonal `R` is *normal*, so its symmetric part shares its invariant
subspaces:

1. `S = (R + Rᵀ)/2`. `S` is symmetric with eigenvalues `cos θ_k`, each appearing with the
   multiplicity of its 2-plane.
2. **Cyclic Jacobi eigendecomposition** of `S = Q Λ Qᵀ`. Classic two-sided rotation sweep; `n ≤ 16`
   converges in ≤ 12 sweeps to `‖offdiag‖ < 1e-9`. Bounded iteration count (hard cap 30 sweeps),
   allocation-free (`prepare` already sized everything). Sort `Λ` descending, permuting `Q`'s columns.
3. **Cluster** eigenvalues with `|λ_a − λ_b| ≤ 1e-6`. Every cluster has even size at even `N`:
   `det(R) = +1` forces the multiplicity of `−1` to be even, and `N` even then forces the multiplicity
   of `+1` to be even too.
4. Per cluster with orthonormal basis columns `Q_c` (`n × 2m`) and eigenvalue `λ = clamp(λ, −1, 1)`:
   - `λ ≈ +1` → `θ = 0`; emit `m` identity blocks, basis columns taken in order.
   - `λ ≈ −1` → `θ = π`; emit `m` blocks `[[-1,0],[0,-1]]`, basis columns taken in order.
   - otherwise → `s = √(1 − λ²)`, `θ = atan2(s, λ) ∈ (0, π)`.
     Form the restriction `C = Q_cᵀ R Q_c` (`2m × 2m`) and `J = (C − λI)/s`. `J` is skew-symmetric and
     orthogonal (`J² = −I`). Deflate: take the first unused unit vector `u₁` in the cluster, set
     `u₂ = J u₁` (automatically unit and orthogonal to `u₁`), emit the block, project `u₁, u₂` out of the
     remaining cluster basis, re-orthonormalise the remainder, repeat `m` times.
     With `u₂ = J u₁`: `R u₁ = cos θ·u₁ + sin θ·u₂` and `R u₂ = −sin θ·u₁ + cos θ·u₂`, so the block is
     `[[cos θ, −sin θ], [sin θ, cos θ]]` — the canonical orientation.
5. `V`'s columns are the emitted `u` vectors in block order; `thetas[b]` is block `b`'s angle.

**This helper is `public static`** so SC-004 clause 6 can measure `V`, `B(θ)`, the reconstruction and
the degenerate cases directly, without friend-declaring the test (see §11 delta D-2).

**Explicitly not shipped, recorded so it is not re-litigated:**
- *Lerp + Newton–Schulz.* `(1−u)A + uB′` is exactly **singular** at `u = 0.5` for the corrected pair
  (`σ_min = 0`, `‖MᵀM−I‖_F = 2.0000` at `N=8`, `2.8284` at `N=16`), because `(1−u)A + uB` is singular
  somewhere in `(0,1)` exactly when `AᵀB` has eigenvalue `−1`, which this pair does — and Newton–Schulz
  acts on singular values as `σ ← 1.5σ − 0.5σ³`, which has `σ = 0` as a fixed point. Unrecoverable.
- *Householder product.* A `K`-reflection factorisation of an endpoint is not unique, so two
  implementations could satisfy every SC-004 clause while traversing different paths — a shipped sound
  chosen at implementation time.

### 7.6 Decay and damping (FR-030, FR-031, FR-032)

Recomputed on the control grid when `S`, `decaySeconds` or `damping` moves. The formulas are
`FDNReverb`'s, at `effects/fdn_reverb.h:576-600`:

```
T60_dc  = decaySeconds                       // 0.5 .. 60 s  (RA-4)
T60_nyq = T60_dc * powf(0.05f, damping)      // damping=0 -> flat; damping=1 -> 20x shorter at Nyquist
for each channel i, with m = effectiveDelay_[i]:
    gDC  = powf(10.0f, -3.0f * m / (T60_dc  * sr))
    gNyq = powf(10.0f, -3.0f * m / (T60_nyq * sr))
    feedbackGain_[i] = min(gDC, 1.0f)                             // FR-030 + FR-032's clamp
    ratio            = clamp(gDC > 1e-10f ? gNyq / gDC : 1.0f, 0.0f, 1.0f)
    dampCoeff_[i]    = clamp(2.0f * ratio / (1.0f + ratio), 0.001f, 1.0f)
```

The one-pole `y = c·x + (1−c)·y` has DC gain exactly 1 and Nyquist gain `c/(2−c)`, so the Jot gain is a
separate multiply. **Unlike `FDNReverb`, there is no separate base `fbGain`** — that class divides
`gDC` by its `roomSize`-derived base (`:589-591`); Aether's per-line gain *is* `gDC`, which is what makes
FR-032's "≤ 1.0 at all times outside freeze" a structural statement rather than a correction factor.

Combined with FR-025 (`M(t)` orthogonal ⇒ exactly unit loop gain), the unfrozen loop is unconditionally
non-expansive regardless of Size, Decay, Dimensionality or their modulation.

`16 · N` `powf` calls per recompute is the heaviest control-grid item. Gate it hard: skip entirely when
none of the three inputs moved by more than `1e-7`. With Size breathing at 0.05 Hz it will move every
chunk, so budget for it in SC-008 configuration (e).

### 7.7 Freeze (FR-033–FR-037, C-4)

`freezeRamp_` is a `LinearRamp` over `kFreezeLatchMs = 50 ms`, target 1 when frozen, **advanced and read
per sample** (§3's ramp-cadence rule and §6.3 step 0). Every one of the six FR-033 steps is expressed as
a **crossfade on that ramp**, so both transitions are click-free. `freezeRamp` below is always the
current sample's value — a per-chunk snapshot would make step 2 step the delay read pointer, and step 6
step the per-line gain, once every 64 samples, which is exactly the staircase SC-015 asserts against:

| step | expression |
|---|---|
| 1 — mod excursion → 0 | `excursion *= (1 - freezeRamp)` |
| 2 — reads latch to integer | `d = lerp(dynamicDelay, roundf(latchedDelay), freezeRamp)`; at ramp 1 the fractional part is exactly 0, so reads become integer `read(size_t)` — **no interpolation, hence no interpolation loss** (C-4) |
| 3 — damping + DC bypass | `y = lerp(filtered, raw, freezeRamp)` for both stages |
| 4 — input injection → 0 | `inject *= (1 - freezeRamp)` |
| 5 — **all three sends → 0** | `octRet, fifthRet, bloomReturn *= (1 - freezeRamp)`. The two `PitchShiftProcessor`s keep running (no state discontinuity); only their returns are muted |
| 6 — per-line gains → 1 | `g = lerp(feedbackGain_[i], 1.0f, freezeRamp)` |

Additionally: **FR-036's denormal tickle is applied only when `freezeRamp < 1`** — under freeze it would
be an energy source and would break SC-002.

`isFrozen()` returns `freezeTarget_ && freezeRamp_.getCurrentValue() >= 1.0f` — true only once the
sequence has completed (FR-037).

While frozen, `setSize`, `setDecaySeconds` and `setDamping` are **accepted and stored but not applied**
(FR-034): the §6.2 step 4/5 geometry block is skipped. `getEffectiveDelayLengthSamples(i)` must
therefore be unchanged by a `setSize` issued under freeze — SC-017 clause 3 measures exactly that.

**Motion during freeze is provided by the matrix morph alone**, which is exactly orthogonal and
therefore exactly lossless. RA-5 records that this makes freeze inert in every other dimension; the
header must say so, because Phase 7's Dream macro and Phase 12's presets will otherwise assume shimmer
survives freeze.

### 7.8 Input path (FR-015, FR-015a, FR-017, FR-018, FR-040–FR-044)

```
inL,inR → finiteness guard → preDelayL/R (stereo, same smoothed length)
        → DiffusionNetwork::process(preL, preR, diffL, diffR, slice)
        → diffL → EVEN channels, diffR → ODD channels, each at kInputInjectionGain = sqrt(2/N)
        → added after the feedback gain
```

`sqrt(2/N)` makes injected energy independent of `N`: each of the `N/2` channels in a subset receives
the same signal at gain `g`, so the subset's injected energy is `(N/2)·g²·E = E`. At `N = 8` that is
exactly 0.5.

The even/odd split mirrors FR-018's even→L / odd→R **output** tap split, so the diffuser's stereo image
survives the network. The pre-delay is stereo, not a mono sum, because `DiffusionNetwork`'s entire value
over a mono allpass chain is the L/R decorrelation it derives from `kStereoOffset = 1.127f`
(`processors/diffusion_network.h:56`) applied to `kDelayRatiosL` (`:51-53`).

Output taps are read **before** damping (`fdn_reverb.h:280-292`) and scaled by `2/N` (0.25 at `N = 8`,
matching `:364-365`).

`DiffusionNetwork` is also **partition-invariant** in this configuration, which SC-011 needs: with
`snapSmoothers()` called every control chunk and `modDepth` left at its `0.0f` default (`:226`), the
network always takes its static fast path (`canUseStaticPath()`, `:534-548`), which the header documents
as bit-identical to the per-sample path (`:507-529`). Slice length does not enter.

At `density = 0` the stage enables crossfade out (`:615-638`) and the input reaches the FDN essentially
undiffused — FR-044's intended plate-like extreme, and SC-003 clause 2's negative control.

### 7.9 Shimmer taps (FR-050–FR-054, C-5)

**Two `PitchShiftProcessor` instances total, not four** — they run on a *mono* sum, so stereo costs
nothing extra. `ShimmerDelay` pays for one per channel (`effects/shimmer_delay.h:88-89`); this halves it.

| constant | `N = 8` | `N = 16` |
|---|---|---|
| read subset (four longest) | `{4,5,6,7}` | `{12,13,14,15}` |
| `kShimmerOctaveInjectChannels` (+12) | `{1, 4}` | `{1, 8}` |
| `kShimmerFifthInjectChannels` (+7) | `{3, 6}` | `{3, 12}` |
| `kBloomInjectChannels` | `{0, 2, 5, 7}` | the remaining 12 |
| injection gain | `sqrt(2/2) = 1.0` (shimmer) | `sqrt(2/4) = 0.7071` / `sqrt(2/12) = 0.4082` (bloom) |

Each shimmer pair **spans both parities**, so neither interval is hard-panned by the even→L / odd→R
output split — the defect an "octave into even, fifth into odd" rule would ship.

**Cadence (Q5), and why it is structural.** The mono tap sum is accumulated over one control chunk into
`tapSumScratch_[0..64)`. At the *next* chunk boundary `process(in, out, 64)` is called once per tap and
the result is injected across that chunk. Each leg therefore carries **64 samples of deferral on top of
its mode latency**. 64 matches the shifter's own `kSmoothingSubBlockSize`
(`processors/pitch_shift_processor.h:165`), and because the cadence is anchored to `sampleCounter_`
rather than to caller blocks, the shifter's internal grain/phase state does not depend on how the host
partitions its blocks. That is what makes SC-011's 1e-6 invariance **structural** rather than hoped-for.

**Loop-time table for the header (FR-054), at 48 kHz** — mode latency (`:280-287`) **plus 64**:

| `PitchMode` | latency | + cadence | ms @ 48 kHz |
|---|---|---|---|
| `Simple` | 0 | 64 | 1.33 (but audible artifacts; a delay-modulation shifter is poor in a recirculating loop) |
| `Granular` (**default**) | ≈2048 | 2112 | ≈44 |
| `PhaseVocoder` | 4096 + 1024 | 5184 | ≈108 |

Mode is a `prepare`-time choice only (FR-053): `getLatencySamples()` "changes immediately" on `setMode`
(`:189-193`), and a loop whose latency changes mid-render is a click.

**Why comb filtering does not arise (FR-052), stated accurately.** *Not* because the read and inject
subsets are disjoint — FR-020's endpoints are **dense**, so after a single sample step every channel
already carries a contribution from every other and the disjointness survives zero round trips. The
correct statement is that **a +12 or +7 copy is not a coherent copy of the signal it came from**, so
the fixed-delay-plus-copy geometry that produces classical comb notches does not exist. The pinned
subsets buy stereo re-diffusion of a mono tap and a measurable injected level. SC-007 clause 4(b)
measures this rather than assuming it.

### 7.10 Harmonic bloom (FR-055–FR-059, C-7, Q1, Q7)

**Kernel.** The per-sample loop is the reused free function
`processSympatheticBankSIMD(y1s, y2s, coeffs, rSquareds, gains, count, scaledInput, sums, releaseCoeff,
envelopes)` (`systems/sympathetic_resonance_simd.h:39-50`) — plain arrays, no ownership, legally callable
from Layer 4. It is called with `count = kMaxBloomResonators` unconditionally; inactive slots hold
`coeff = gain = y1 = y2 = 0` and contribute nothing, exactly as `SympatheticResonance::process` does
(`systems/sympathetic_resonance.h:326-333`). The parameter is `int count`
(`systems/sympathetic_resonance_simd.h:45`), which is why §2.3 declares `kMaxBloomResonators` as `int`
— passing a `std::size_t` would be an implicit narrowing in the call expression (C4267 / `-Wconversion`)
against the zero-warning gate.

**Coefficients (FR-057) — re-derived, because the originals are `private static`.** Verified this
session: `computeResonatorCoeffs`, `computeResonatorPeakGainInverse` and `computeFreqDependentQ` all sit
below the `private:` at `systems/sympathetic_resonance.h:383`. Copy the maths, cite the lines:

```
Q       = 20.0f * powf(20.0f, bloomDecay)                 // 0..1 -> [20, 400]     (FR-057)
Q_eff   = Q * clamp(500.0f / f, 0.5f, 1.0f)               // :440-446, :58, :61
r       = expf(-kPi * (f / Q_eff) / sr)                   // :430
coeff   = 2r·cos(2π f / sr) ;  rSquared = r²              // :431-435
peakInv = (1 - r) * sqrt(1 - 2r·cos(2ω) + r²)             // :401-420, with cos(2ω) from the
                                                          //   double-angle identity on coeff/(2r)
                                                          //   and kDenormalGuard from
                                                          //   core/audio_constants.h:40
```

**Note API (FR-056, RA-7).** `bloomNoteOn(voiceId, partialHz, count)` / `bloomNoteOff(voiceId)` — the
`SympatheticResonance` shape (`:179`, `:264`) **without** its 4-partial `SympatheticPartialInfo` cap
(`:40`, `:71-74`). Both are audio-thread-callable, allocation-free, `noexcept`:

- `partialHz == nullptr` or `count == 0` → no-op; `!isPrepared()` → no-op (Edge cases 27, 31).
- `count` clamped to `kMaxBloomResonators`.
- Every frequency tested with `detail::isNaN` / `detail::isInf` and clamped to `[20 Hz, 0.45·sr]`
  **before** any coefficient computation, so no non-finite coefficient can reach the kernel (Edge case
  28).
- A `voiceId` already live **replaces** its own partial set (Edge case 29); a full bank retires its
  oldest voice by `bloomVoiceAge_`.
- `bloomNoteOff(voiceId)` sets those slots' `gains[k] = 0` and `bloomDriven_[k] = false`. The resonator
  rings down naturally (`r < 1`); the control-grid reclaim pass frees the slot when
  `bloomEnv_[k] < kReclaimThresholdLinear` (`systems/sympathetic_resonance.h:52`, the reclaim loop at
  `:337-352`, moved to control rate here because per-sample branching would defeat the SIMD loop).
  Click-free by construction: nothing is hard-cut.
- `getActiveBloomResonatorCount()` counts **driven** slots, so it drops on note-off — which is what
  SC-016 clause 3's release assertion needs.
- Accepted while frozen, but the return is muted for the freeze's duration (Edge case 30, FR-033 step 5).

**Stability guard (FR-058), stated as a computable criterion.** Two multiplicative factors, both
recomputed on the control grid:

1. per-resonator `gains[k] = peakInv(coeffs_k, f_k)` — so a high-Q resonator contributes **unit** gain
   at its own centre frequency instead of `Q`-fold gain;
2. a global `1/√count` on the summed return, `count = getActiveBloomResonatorCount()`.

Then, per control chunk, define the combined loop gain at resonator `k`'s own centre frequency:

```
g_bloom(f_k) = kTapReadNormalisation          // 0.25 — the tap sum the bank is driven from
             * (1 / sqrt(count))
             * sendGain                       // setBloomSend mapping, below
             * kTapInjectionGain(bloomSubset) // sqrt(2/|subset|)
             * hfShelfMagnitude(f_k)          // FR-059
g_line(k)    = feedbackGain_[c] for the bloom-injected channel c
worst        = max over active k of  g_line(k) * g_bloom(f_k)
bloomGuardScale_ = (worst > kBloomLoopGainCeiling) ? kBloomLoopGainCeiling / worst : 1.0f
```

One scalar on the summed return, so the bank's relative tuning is untouched and no resonator is silently
detuned or dropped.

`kBloomLoopGainCeiling = 0.95f` — strictly inside FR-058's "≤ 1.0", with margin for smoother lag and
single-precision rounding.

**`setBloomSend` mapping — the one constant this plan expects to tune against a measurement.**
`sendGain = v · kBloomSendMax`. With the pinned normalisations, `v = 1` and four held partials gives
`g_bloom ≈ 0.25 · 0.5 · 0.7071 · kBloomSendMax = 0.0884 · kBloomSendMax`; the steady-state emphasis at
`f_k` is ≈ `1/(1 − g_line·g_bloom)`. To reach SC-016 clause 3's **≥ 6 dB** (a factor of 2, i.e.
`g_line·g_bloom ≈ 0.5`) at `g_line ≈ 1`, `kBloomSendMax` must be ≈ 5.7; **start at
`kBloomSendMax = 8.0f`**, which targets ≈ 0.71 loop gain ⇒ ≈ 10.7 dB, comfortably above the criterion
and below the guard ceiling. **If SC-016 clause 3 does not reach 6 dB, the admissible fixes are this
constant and the normalisations — never the criterion (B-4).** Record the finally-shipped value and its
measured emphasis in the header and in `compliance.md`.

### 7.11 Spectral diffusion (FR-060–FR-065, RA-2, Q2)

**Topology.** `prepare`-time flag, default `true`, no runtime toggle. One stereo
`STFT → phase-smear → OverlapAdd` stage on the **wet** path at `fftSize = diffusionFftSize_`,
`hop = fftSize/4` (75 % overlap) with `applySynthesisWindow = true` — the configuration `OverlapAdd`'s
own header documents as required for spectral-modification processors at ≥75 % overlap and **forbids**
at 50 % (`primitives/stft.h:225-228`).

**Pump, per slice:**

```
stftL_.pushSamples(wetL, slice);  stftR_.pushSamples(wetR, slice);
while (stftL_.canAnalyze()) {                             // primitives/stft.h:134-138
    spectralSm_.advanceSamples(diffusionHopSize_);         // FR-064's cadence
    const float a = spectralSm_.getCurrentValue();
    stftL_.analyze(specL_);  stftR_.analyze(specR_);       // :144
    smear(specL_, smearRngL_, a);  smear(specR_, smearRngR_, a);
    olaL_.synthesize(specL_);  olaR_.synthesize(specR_);   // :289
    pull hop samples from ola*, scale by g(a), push into wetFifo_
}
read `slice` samples out of wetFifo_ as the wet output   // see the underflow rule below
```

`STFT::analyze` consumes exactly `hopSize` (`:171`) and `OverlapAdd::synthesize` marks `hopSize` ready
(`:311`), so the pump is self-balancing. Analysis boundaries depend only on the *total* pushed count →
partition-invariant (SC-011).

**FIFO underflow rule (normative — this is what creates the `fftSize` offset the dry path aligns to).**
`STFT::canAnalyze()` requires `samplesAvailable_ >= fftSize_` (`primitives/stft.h:134-138`), so **no**
frame is produced until `fftSize` samples have been pushed, while the consumer demands one output
sample per input sample from the very first slice. The FIFO is therefore short by exactly `fftSize`
samples for the whole warm-up. When `wetFifoCount_ < slice`:

- emit `0.0f` for the missing samples, and
- do **not** advance `wetFifoRead_` past `wetFifoWrite_`.

Left unstated the implementation would either read uninitialised ring memory or silently change the
dry/wet alignment. This zero-fill is precisely what establishes the `fftSize` offset that
`dryAlignL_/R_` is set to (FR-062), and SC-018 clause 5 asserts it directly: with `setMix(1)` the first
`getLatencySamples()` wet samples must be **exactly** `0.0f`.

**Smear (FR-061).** Per bin, per channel, **redrawn every hop** — not drawn once and held. A held draw
is a static dispersive allpass (fixed colouration, no smearing over time); a redrawn draw decorrelates
successive frames and produces the time-smeared "underwater chamber" the roadmap asks for. Both readings
are audibly and measurably different, so this is stated normatively:

```
for (bin = 0; bin < spec.numBins(); ++bin)
    spec.setPhase(bin, spec.getPhase(bin) + a * rng.nextFloat() * kPi);   // :91, :106, :59
```

Magnitudes are **never** modified (FR-061).

**Coherence make-up gain `g(a)`.** Independently randomised per-frame phases sum *incoherently*, so
`OverlapAdd`'s fixed COLA factor `colaNormalization_ = 1/colaSum` (computed at prepare, `:243-262`, and
applied unconditionally in `synthesize()`, `:299-307`) is wrong by a level that grows with `a`. The
spec's measured table, and its reciprocal:

| `a` | 0 | 0.25 | 0.5 | 0.75 | 1.0 |
|---|---|---|---|---|---|
| `outRMS/inRMS` | 1.0000 | 0.9260 | 0.7443 | 0.5635 | 0.5001 |
| **`g(a)`** | **1.0000** | **1.0799** | **1.3435** | **1.7746** | **1.9996** |

Ship those five knots and interpolate with the existing Layer 0
`Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`), end tangents clamped. Checked:
Catmull-Rom tangents on this data are `0.1718 / 0.3474 / 0.3281`, each inside the Fritsch–Carlson
monotonicity bound (`≤ 3×` the adjacent secant), so the interpolant is monotone — no new maths, no
`powf`.

`g(a)` is applied as a **scalar on the pulled time-domain samples** (not on the bins), so FR-061's
"magnitudes are never modified" holds literally. The per-frame variation this introduces is bounded by
the 100 ms smoother against a 5.3 ms hop (<1 % between overlapping frames) and is far inside SC-007
clause 5's ±1.0 dB.

**Transfer to other FFT sizes (Edge case 13).** The coherence loss is a function of the **window and
the overlap count**, not of `fftSize` — the incoherent/coherent amplitude ratio is
`sqrt(Σ_k w⁴(n−kh)) / Σ_k w²(n−kh)`, which is scale-invariant in `fftSize` at fixed overlap. The table
is therefore expected to transfer to 256 and 4096 unchanged; Edge case 13 and SC-007 clause 5 measure it.

**Latency and dry alignment (FR-062, FR-084, RA-2).** `getLatencySamples()` returns
`spectralEnabled_ ? diffusionFftSize_ : 0` and is constant for a prepared configuration. The dry path
runs through `dryAlignL_/R_` at exactly `fftSize` samples so the engine reports **one** latency, not
two. The shimmer taps' latency is *not* included — they live inside the feedback loop and a
recirculating path has no dry counterpart to align against.

**Warm-up.** The first `fftSize − hop` output samples are under-summed (only frames `0..k` exist). The
engine starts silent, so this is inaudible; note it in the header rather than special-casing it.

### 7.12 Life modulation (FR-070–FR-074)

| | source | rate | depth control | applied |
|---|---|---|---|---|
| Size breathes | `BreathingModulator` | **pinned `setRate(0.05f)`** → 20 s period | `setSizeBreathDepth` | added to smoothed Size *before* the `S(v)` mapping, combined value clamped to [0,1] |
| Matrix tides | `TidalModulator` | **pinned `setRate(1.0f)`** → base 30 s, layers 30 / 42.43 / 51.96 s | `setDimensionalityTideDepth` | added to smoothed Dimensionality *before* the [0,1] clamp |
| Delay jitter | `BrownianDrift` × `N/2` | no rate control exists — `setModSmoothness` → `tau = lerp(0.2, 30) s` | `setModDepth` | `± modDepth · 0.5 % of that channel's own current length` |

Both rates are **pinned in `prepare`, not inherited** — the class defaults are `0.1f`
(`processors/breathing_modulator.h:111`) and `0.5f` (`processors/tidal_modulator.h:143`). Pinning is
what makes SC-017's thresholds *derivable from the classes' own constants* instead of guessed, and what
makes the 20 s breath period affordable as an always-on 24 s render. There is no rate setter on
`AetherReverb` for either.

Both modulators' **own** depths stay at their class defaults `1.0f` (`:112`, `:144`), so the two Aether
depth controls are the only ones and nothing is multiplied twice.

`BrownianDrift` has **no rate setter** — its complete public API is `prepare/reset/setSeed/setSmoothness/
setDepth/setMean/process/processBlock/getCurrentValue/getSourceRange` (`processors/brownian_drift.h:121,
133, 145, 152, 159, 165, 178, 194, 212, 217`, verified). Its only time-scale control is `setSmoothness`
→ `tau = lerp(kTauMin 0.2 s, kTauMax 30 s, smoothness)` — the constants are at
`processors/brownian_drift.h:97` and `:99` (`:35-36` is the banner prose that describes them; the
spec's own row, spec.md:370, cites `:97-99` and the plan previously dropped it), the lerp itself at
`:231-234`. `setModSmoothness` is
forwarded **verbatim**; no Hz domain is invented and none is advertised. Header states the reachable
`tau ∈ [0.2 s, 30 s]` (≈0.005 … 0.8 Hz of equivalent wander rate); the 0.6 default gives `tau ≈ 18 s`.

`getCurrentValue()` is hard-clamped to `[−1, +1]` (`:212-214`), which bounds the excursion structurally.

FR-072 deliberately differs from `FDNReverb`, which applies `modDepth · 5 %` of the **longest** line to
every modulated channel (`effects/fdn_reverb.h:631`): at this phase's Size range the longest line is up
to 424 ms, so 5 % of it is 21 ms of excursion applied to a 5 ms line. Per-line and ten times smaller is
the correction; the header records the reason.

Modulators are advanced **even when the input is silent** — there is no input-activity gate (FR-074).
SC-017 clauses 1a/2a render silence on both inputs and require the introspection values to move, so a
stubbed or input-gated modulator fails on **every build**, not only in the nightly lane.

### 7.13 Output stage (FR-080, FR-081)

`mid/side` width on the wet signal only (`fdn_reverb.h:368-371`), then equal-power mix
`dryGain = cos(mix·π/2)`, `wetGain = sin(mix·π/2)` (`:374-377`). Both gains and `width` are **computed
once per control chunk** from their smoothers and held constant across the slice — FR-019's discipline.
(FR-009's table says "per sample" for these three; see §11 delta D-3.)

Finally `out = outputGate_ * (dryGain·dry + wetGain·wet)`.

**SIMD lever (SC-008's list, taken only if a configuration misses).** The two hot loops are the
per-sample `N×N` matrix multiply and the per-channel damping/DC/gain loop. Both are `alignas(32)` SoA
arrays. If Highway is added, **`hn::LoadU`/`StoreU` unless alignment is proven** —
`node tools/lint-simd-aligned-loadstore.js` enforces it, and an aligned load on an AVX-512 runner is the
known cause of intermittent Linux-CI-only SIGSEGV. **The matrix *mechanism* is not a lever**: FR-022
pins the geodesic, and swapping it changes the shipped Dimensionality axis.

### 7.14 Non-finite hygiene (FR-082, FR-083)

- **Input (FR-082):** every input sample tested with `detail::isNaN`/`detail::isInf` and replaced with
  `0.0f` before it can enter the loop — the guard `FDNReverb` applies at `:264-265`. This path does
  **not** increment `getNonFiniteRecoveryCount()` (SC-014 clause 2).
- **Internal (FR-083):** once per control chunk, test `filterState_[0..N)` and the current matrix's
  diagonal. On detection: `emergencyClear()` and `++nonFiniteRecoveries_`.

`emergencyClear()` is a **plan-level refinement of FR-083's "invokes `silence()`"** and is documented as
such in the header: ramping a non-finite value down over 20 ms is not possible, so there is no
fade-out. The sequence is:

1. **Immediately**, in the detecting control chunk: zero the O(N)/O(`kMaxBloomResonators`) scalar state
   (`filterState_`, `dcBlockX_/Y_`, `chanIn_`, `chanOut_`, the bloom arrays, the three shelf states,
   `tapSumScratch_`, `shimmerOut*`) — a few hundred floats, which is where the detected NaN lives.
2. `clearPending_ = true`, `clearCursor_ = 0`, `clearStage_ = 0`; `outputGate_.snapTo(0)`,
   `gate_ = FadingIn`, `outputGate_.setTarget(1)` over `kSilenceRampMs`.
3. The **bulk** clear — `delayBuffer_` plus the deferred sub-object resets (STFT/OLA/spectral buffers,
   wet FIFOs, pre-delays, dry-alignment delays, `diffuser_`, both `PitchShiftProcessor`s) — is
   **amortized exactly as §5.3 specifies**, one work unit per control chunk, with
   `clearQuotaFloats_` sized at §5.1 step 15 so the set completes inside the fade-in window. The same
   wall-clock argument applies: at `N = 16` / 192 kHz an unamortized clear is ≈ 5 MiB of `memset`
   against a deadline that can be 0.33 ms.
4. While `clearPending_`, §6.3 step 0's rule holds: the delay lines are written with literal `0.0f`
   (no recirculation, no injection) and the wet contribution is replaced by literal `0.0f`. Both are
   assignments, not `× 0` products, so a not-yet-cleared non-finite value **cannot** reach the output
   during the deferred window — which is what keeps SC-014 clause 1 true.

SC-014 clause 3's "recovery point" is the first sample at which **both** `clearPending_ == false` and
the fade-in has completed. By construction (step 3's quota) that is the end of the fade-in.

This is a last-resort net, not a substitute for FR-025/FR-032's structural bounds.

**How the net is reached at all (FR-083's testability — see §11 delta D-8).** Every input path into the
engine is sealed by design: FR-082 replaces non-finite input samples with `0.0f` *before* the loop
(above, and explicitly *without* incrementing the counter); every setter falls back to its FR-009
default on a non-finite argument (§3); `bloomNoteOn` clamps every partial to `[20 Hz, 0.45·sr]` before
any coefficient computation (§7.10); and FR-025 + FR-032 make the unfrozen loop structurally
non-expansive (§7.6), so no legal input can drive it to Inf. There is therefore **no** legal call
sequence that makes internal state non-finite, and the spec forbids friend-declaring tests
(spec.md:1562) — so without a deliberate hook, FR-083's detect → `emergencyClear()` → counter branch is
unreachable dead code and SC-014 clause 3 is unimplementable (it would silently degenerate into a
second measurement of clause 2).

The hook is `injectNonFiniteStateForTest()` (§3), compiled only when `KRATE_DSP_AETHER_TEST_HOOKS` is
defined — which §1.2 item 4 does **target-wide** on `dsp_effects_tests`, never per-source, so every TU
in that image sees the same class definition and there is no ODR hazard. It writes a bit-pattern NaN
(built through a `volatile` sink, never `std::numeric_limits`) into `filterState_[0]` — the exact array
this section's sweep tests. The shipping build has no such member; the header states that next to the
FR-083 contract.

### 7.15 `getStateEnergy()` — the summation window is normative (FR-086, SC-002 clause 1)

FR-086's prose calls it "the sum of squares of the **entire** FDN delay-line contents" (spec.md:1244).
Taken literally that is **not** the quantity orthogonality conserves, and §4's own buffer layout makes
the difference structural rather than academic. Sections are
`nextPowerOf2(ceil(ref_i·S_max·1.005) + 4)`, e.g. **32768** for a longest line of **20 348** samples at
`S = 4` (§4, §7.1) — so a whole-buffer sweep would include up to ~60 % stale history that is *not* part
of the state, and far more at `size < 1` (at `S = 0.25` the longest line is 1 272 samples inside a
32 768-float section, i.e. ~96 % stale). The first ~0.7 s after a freeze latch still contains pre-freeze
content, which lands directly inside SC-002 clause 1's ±0.5 dB window.

The exactly-conserved quantity implied by FR-025 (spec.md:769-771) is the **L2 norm of the `N`-channel
state vector**: `Σ_i Σ_{k=1..m_i} x_i[n−k]²` over each line's *current effective length* `m_i`. Per
freeze step the network drops the sample at offset `m_i` and adds `‖M·read‖² = ‖read‖²`, so the sum is
invariant. Summing to `sectionSize` instead drops the sample at offset `sectionSize`, and conservation
does not follow.

**Definition (binding):**

```
double e = 0.0;
for (i = 0; i < numChannels_; ++i) {
    const std::size_t m = static_cast<std::size_t>(std::ceil(effectiveDelay_[i]));  // the length
                                                    // the reads actually use this control chunk
    for (std::size_t k = 0; k < m; ++k) {
        const std::size_t idx = sectionOffset_[i]
                              + ((writePos_[i] - 1u - k) & sectionMask_[i]);
        const double s = static_cast<double>(delayBuffer_[idx]);
        e += s * s;
    }
}
return static_cast<float>(e);
```

`writePos_[i] - 1u - k` wraps modulo 2⁶⁴, and `sectionMask_[i] + 1` is a power of two, so the mask
recovers the correct index without a signed subtraction. Accumulation is in `double` (Q8's own
requirement), and the sweep stays an on-demand accessor never called from `process()`.

Under freeze `effectiveDelay_` is latched and the reads are integer at `roundf(latchedDelay)`
(FR-033 step 2, §7.7); `m_i` is computed from the same latched values, so the window the accessor sums
is exactly the window the loop recirculates. That identity is what makes SC-002 clause 1 a derivation
from FR-025 rather than an empirical hope, and §8.2 states the dependency explicitly.

Recorded as §11 delta **D-9**.

---

## 8. Test plan

Five TUs. Tags: untagged = always-on (B-1's ≤ 60 s wall budget),
`[.slow]` = nightly, `[.perf]` = perf lane. Every case also carries `[effects][aether]`.

### 8.1 Shared fixtures (one anonymous-namespace block per TU, or a small shared header in the TU)

Implement SC-0's pinned generators and metrics once:

| id | implementation |
|---|---|
| **G-1** harmonic stack | 220 Hz + 2×…9× at `1/n`, all sine, zero phase, scaled to peak 0.5 |
| **G-2** band-limited noise | `Xorshift32` at a pinned seed → 4th-order Butterworth pair, 80 Hz…11 kHz, peak 0.5 |
| **G-3** unit impulse | 1.0 at sample 0, both channels, rendered with `setMix(1)` |
| **G-4** single partial | 220 Hz sine, peak 0.5, 2 s |
| **G-5** noise reference | G-2 used *directly*, never through the engine — the M-1 ceiling |
| **M-1** banded frame-averaged flatness | non-overlapping 4096-sample frames; Hann; real FFT; keep bins in [80 Hz, 11 kHz]; per frame `exp(mean(ln|X|))/mean(|X|)`; report the mean, the frame count and `SE = stddev/√frames` |
| **P-1** | `setSizeBreathDepth(0)`, `setDimensionalityTideDepth(0)`, `setModDepth(0)` |
| **P-2** | `maxDelaySeconds = 0.5f`, `REQUIRE(engine.getMaxSizeScale() == 4.0f)` **before** any Size sweep |
| **P-3** | `setMix(1.0f)` |
| **P-4** | `numChannels = 8` |

**M-1 is defined locally, not delegated to `calculateSpectralFlatness`**
(`tests/test_helpers/signal_metrics.h:326`), for two verified reasons: it picks one FFT size capped at
4096 and windows only the **first `fftSize` samples** of the buffer (`:337`, `:351`), so "flatness over
the last 2 s" would in fact measure ~85 ms; and it computes `geomMean/arithMean` over **all** non-DC
bins (`:397`), so the 11–24 kHz bins G-2 never excites crush the geometric mean. Its own ceiling on
ideal white noise is ≈0.845 — below the 0.85 an absolute threshold would need.

### 8.2 `aether_reverb_test.cpp`

| Case | Tag | Covers | Assertion strategy |
|---|---|---|---|
| `AetherReverb_NoAllocationAfterPrepare` | — | SC-001, FR-003, FR-008 | `TestHelpers::AllocationScope` (`tests/test_helpers/allocation_detector.h:75`) around 30 s at the worst case (`N=16`, shimmer `Granular`, bloom, spectral @4096) with every setter, `setFreeze` both ways, `setSeed`, `silence()` exercised mid-render. **Read the count with the bracketing idiom, NOT `scope.getAllocationCount()`** — see the note below |
| `AetherReverb_FreezeEnergyConservation` | — / `[.slow]` | SC-002, FR-025, FR-033, C-4 | **Clause 1 (primary):** `getStateEnergy()` sampled once per second for 60 s after the latch, in dB relative to the first post-latch sample; `±0.5 dB`; always-on config `size=1, dimensionality=1, tideDepth=1, N=8`. **The derivation from FR-025 holds only under §7.15's definition of the summation window** (`m_i` samples per line, not the whole section) — the test states that in a comment and the header states it on the accessor. **Clause 2:** wet 1 s-window RMS, `±1.0 dB` at tide 1 (hard bound — no re-derivation), `±0.5 dB` positive control at tide 0. **Clause 3:** per-octave (125…8k) at tide 0, `±0.5 dB`, with a `−80 dBFS` reference-window gate and `REQUIRE(qualified >= 6)`. **Clauses 2's positive control and 3 need their OWN always-on 62 s render at `tideDepth = 0`** — they cannot ride on clause 1's render, which pins `tideDepth = 1` (§8.7 ledger, §11 delta **D-10**). **Clause 4:** all three sends at 1 + `bloomDecay=1` + `spectralDiffusion=0.5` set **before** freeze; clause 1's bound unchanged. **Clause 5 `[.slow]`:** ten enter/leave cycles, `±0.5 dB` and 0 clicks |
| `AetherReverb_Rt60Accuracy` | — / `[.slow]` | SC-005, FR-030, **FR-031** | Schroeder backward integration of the G-3 impulse response, wet-only, `damping = 0`, record ≥ `1.2 × decaySeconds`. `±15 %` and monotone non-decreasing in `setDecaySeconds`. Always-on: `{0.5, 4}` s × `size ∈ {0.25, 1}` × `dim = 0.5`, **plus one full 60 s config at `size = 0.5`** (B-2's full-scope requirement). **Clause D (always-on, FR-031's only teeth):** at `decaySeconds = 4`, `size = 0.5`, P-1, G-2 input, 5 s recorded per point, measure banded Schroeder T60 in the **8 kHz octave** and the **250 Hz octave** at `damping ∈ {0, 1}` and require `ratio(1) ≤ 0.25 · ratio(0)` where `ratio(d) = T60_8k(d) / T60_250(d)`. FR-031's law is `T60_nyq = T60_dc·0.05^damping`, i.e. **20×** shorter at Nyquist at `damping = 1`, so 4× is a generous margin; **both ratios and both raw T60s are recorded**. Without this clause **no criterion in §8 fails if the per-line damping one-pole is a no-op**: SC-005/006/012 pin `damping = 0`, SC-007 clause 4(a) runs at 0.4 but measures each 1/3-octave band against its *own* neighbour median, which a uniform brightening leaves unchanged, and SC-014 pins 0.4 only to stabilise its convergence quantity. Cost ≈ 10 s always-on |
| `AetherReverb_ShimmerRegenerationStability` | — / `[.slow]` | SC-006, FR-058, FR-059 | 5 s G-1 then 175 s silence, all sends at 1, `decay = 60`, `damping = 0`, `density = 1`. (1) peak ≤ 4.0; (2) peak and RMS of `E_final` ≤ `0.95 ×` those of `E2`, **and** the 20 s-window RMS sequence non-increasing from `E2` on; (3) `HF(E_final) ≤ 1.25 × HF(E1)` as a **fraction**, plus centroid ≤ `1.25 ×`; (4) 0 non-finite and `getNonFiniteRecoveryCount() == 0`. Record every measured figure |
| `AetherReverb_SampleRateIndependence` | — / `[.slow]` | SC-009, FR-003, RA-6 | T60 `±10 %`, NED ≥ 0.8, modal density `±2 %` across 44.1/48/96/192 kHz at one config. **Sub-44.1 clause:** `prepare(8000.0, …)` **succeeds** (no clamp); `getModalDensityPerHz()` matches the table computed at 8 kHz to `±2 %`; T60 at `setDecaySeconds(4)` within `±15 %` **at 8 kHz**; `REQUIRE(engine.isShimmerActive() == false)`; and the render with both sends at 1 compares equal to the render with both at 0 via `compareFingerprints` — proving the taps are *inert*, not merely unallocated. At ≥ 44.1 kHz `isShimmerActive()` must be `true` |
| `AetherReverb_SeededDeterminism` | — | SC-010, FR-073, FR-006 | `fingerprintRender` / `compareFingerprints` (`tests/test_helpers/render_fingerprint.h:64`, `:101`) at the helper's own `kSampleTolerance = 1e-4` / `kMetricTolerance = 1e-5` (`:49`, `:52`). (1) same seed ⇒ equal; (2a) `setSeed` **before** `prepare` at `dimensionality = 1` ⇒ unequal (exercises the matrix path); (2b) `setSeed` **after** `prepare` at the 0.35 default with `sizeBreathDepth = tideDepth = modDepth = 1`, `spectralDiffusion = 0.5` ⇒ unequal (exercises the modulator/smear streams); (3) `prepare → H → render A → reset() → render B` (H **not** re-applied) ⇒ equal — *depends on FR-009's smoother-initialisation rule*; plus `prepare → H → A → prepare(same) → H → C` ⇒ `C == A` |
| `AetherReverb_BlockPartitionInvariance` | — | SC-011, FR-005, FR-050 | 48 000 samples rendered whole vs in the repeating partition `{1,7,64,65,511,512,513,2048}`; sample-wise max abs diff ≤ **1e-6**; config (b) with all life modulation active **and both shimmer sends at 1** |
| `AetherReverb_BoundedUnderAdversarialInput` | — | SC-012 | 60 s of noise → DC → 1 Hz square → silence at `decay = 60`, `damping = 0`, `size` swept 0→1→0, `dimensionality` swept, shimmer and bloom at max. Peak ≤ 4.0; 0 non-finite; `getNonFiniteRecoveryCount() == 0`; `|DC|` in the final second ≤ 1e-3 |
| `AetherReverb_NoTransitionClicks` | — | SC-015 | `ClickDetector` (`tests/test_helpers/artifact_detection.h:99`, `:130`) with **exactly** `ClickDetectorConfig{.sampleRate=48000.0f, .frameSize=512, .hopSize=256, .detectionThreshold=5.0f, .energyThresholdDb=-60.0f, .mergeGap=5}`, designated-initialiser form as at `dsp/tests/unit/effects/shimmer_delay_test.cpp:1224-1231`. Input **G-1** (pinned: the detector flags `|Δy| > mean + kσ` per frame, `artifact_detection.h:38-45`, and a near-Gaussian reverb tail gives false positives at ~1e-4/sample). 120 s render with a size sweep, a dimensionality sweep, 5 freeze cycles, a `setDensity` 0→1 step, a `setDecaySeconds` 0.5→60 step, a `setShimmerOctaveSend` 0→1 step, **a `setSpectralDiffusion` 0→1 step**, `silence()` + resumption ⇒ **0 detections**. **Calibration is capped and must be proven:** if a 30 s no-transition reference render shows a non-zero false-positive floor, raise `detectionThreshold` to the smallest value giving 0 there — **cap 8.0**, and the *same* calibrated config must then report **≥ 1 detection** on a 10 s control render carrying a single-sample step of amplitude 0.1. Record the threshold, the floor and the control-render count. **Clause S (FR-007, `silence()` does NOT latch):** two 3 s G-1 renders at the FR-009 default mix, identical except that the subject calls `silence()` at `t = 1.0 s`. Require (a) the wet RMS of the 40 ms window starting at the call is below **−80 dBFS** (the fade-out half is measured, not assumed), and (b) the wet RMS of the final 200 ms is within **±3 dB** of the same window of the never-silenced reference. A latching implementation — `AtmosphereEngine::silence()`'s behaviour (`systems/atmosphere_engine.h:636-644`), which the header explicitly diverges from — outputs digital zero for the rest of the render and so scores **0 clicks and 0 allocations**, i.e. passes every other clause in §8. Cost ≈ 6 s. **Clause F (FR-064's smoother cadence):** after the `setSpectralDiffusion` 0→1 step above, require the wet RMS to reach within **1 dB** of its settled value no later than **500 ms** after the step. FR-064 warns that advancing `spectralSm_` once per `process()` instead of by `advanceSamples(hopSize_)` stretches its 100 ms constant to ~25 s — two orders of magnitude past this bound. This is the only place the cadence is observable: SC-007 sets the amount *before* rendering, where FR-009's smoother-initialisation rule snaps it |
| `AetherReverb_LifeModulation` | — / `[.slow]` | SC-017, FR-070–FR-074, FR-034 | **1a (always-on):** one **24 s silent** render at `sizeBreathDepth = 1`, `size = 0.5`; sample `getEffectiveDelayLengthSamples(0)` every 100 ms; p-p `> 0` and `≥ 80 %` of the depth-implied excursion. **The expectation is in SAMPLES, not in scale factors:** `getEffectiveDelayLengthSamples(0) == refDelaySamples_[0]·S` (§6.2 step 4), so the correct expression is `refDelaySamples_[0] · (S(clamp(size + depth·b_max,0,1)) − S(clamp(size + depth·b_min,0,1)))`. The spec's bare `S(…) − S(…)` form (spec.md:2130) is dimensionless and evaluates to ≈3.75 at depth 1, against a measured p-p of ≈`967·(4.0 − 0.25) ≈ 3626` samples at 48 kHz — a comparison wrong by ~3 orders of magnitude (§11 delta **D-11**). The test obtains `refDelaySamples_[0]` as `getEffectiveDelayLengthSamples(0) / S(size)` measured at `sizeBreathDepth = 0`, and **records the numeric expectation next to the measurement**. `b_max/b_min` come from a **test-owned `BreathingModulator`** prepared at the same rate, seeded `deriveStreamSeed(config.seed, AetherReverb::kBreathSalt)`, `setRate(0.05f)`, advanced by `processBlock(64)` on the same grid. **Second 24 s render at `sizeBreathDepth = 0`, `size = 0.5`, and now also `setModDepth(1)`, `setModSmoothness(0.0)` (⇒ `tau = 0.2 s`, `brownian_drift.h:97`, so the drift fully resolves inside the window), `tideDepth = 0`:** (i) channel **0** flat to ≤ 1 sample p-p — the FR-070 depth-0 control, and channel 0 is by construction *not* drift-modulated (§6.2 step 4 applies drift only to `i ≥ N/2`); (ii) `getEffectiveDelayLengthSamples(N−1)` p-p in `(0, 1.2 · kModExcursionFraction · refDelaySamples_[N−1] · S(0.5)]` — at `N = 8`, 48 kHz that is `(0, 30.5]` samples. **Third render, 2 s, same config but `setModDepth(0)`:** channel `N−1` flat to ≤ 1e-6 samples. Clauses (ii)+(iii) are the only place `setModDepth` and `setModSmoothness` are exercised functionally: P-1 zeroes `modDepth` for SC-003/005/006/007/009/014, SC-011 measures partition invariance (which a stubbed drift satisfies *maximally*), and the criterion whose title claims FR-072 previously sampled only channel **0**, which is never drift-modulated. **2a (always-on):** same renders, `getCurrentMorphPosition()` every 100 ms over the first 10 s, p-p ≥ **0.05** at `tideDepth = 1` (derivation: layer 0's 30 s period traverses 120° in 10 s; a 120° arc of a sine spans ≥ half its amplitude, and layer 0's amplitude is `kLayerWeight · kSinePairScale · 2 = 1/3`, `tidal_modulator.h:136-138` ⇒ ≥ 0.167, better than 3× margin) and ≤ 1e-6 at depth 0. **3 (always-on):** frozen, `setSize(1.0f)` leaves every `getEffectiveDelayLengthSamples(i)` unchanged (≤1e-6) and they move after `setFreeze(false)` settles. **1b/2b/4 `[.slow]`:** the 120 s grids, and clause 4 repeats them with **G-2 input** requiring the spreads to match the silent renders within 5 % |
| `AetherReverb_LatencyAndDryAlignment` | — | SC-018, FR-084, FR-062 | (1) `getLatencySamples() == diffusionFftSize` at `{256, 1024, 4096}` and **exactly 0** when disabled, constant across the whole control table; (2) `setMix(0)` ⇒ input↔output cross-correlation peak at lag `getLatencySamples() ± 1` with peak correlation ≥ 0.999, repeated with the stage disabled (expected lag 0); (3) `setMix(0.5)` ⇒ a **single** peak — no secondary above 0.2 of the primary within `±2·fftSize`; **(4) FR-015 / `setPreDelayMs` (the only place it is measurable):** `setMix(1.0f)`, `spectralDiffusionEnabled = false`, `density = 0`, G-3 impulse — the first wet sample above `peak·0.01` occurs at **100 ms ± 1 ms** with `setPreDelayMs(100.0f)`, and at ≈0 ms with `setPreDelayMs(0.0f)`. Clause 2 structurally cannot see the pre-delay: it renders `setMix(0.0f)` (dry only) while the pre-delay sits on the **wet** path between the finiteness guard and `DiffusionNetwork` (§6.3 step B, FR-015a), and the default is 0.0 ms — so an entirely unwired `setPreDelayMs` ships green without this clause. Cost < 1 s of audio. **(5) FR-062's warm-up offset:** with `setMix(1.0f)` and the spectral stage enabled, the first `getLatencySamples()` wet samples are **exactly `0.0f`** — the zero-fill §7.11's FIFO-underflow rule mandates, and the thing that establishes the `fftSize` offset the dry path is aligned to |

**`AllocationScope` idiom (binding for SC-001 — the naive form is vacuous).** Verified this session:
`AllocationScope` assigns `count_` only in its **destructor**
(`tests/test_helpers/allocation_detector.h:81-83`), so `scope.getAllocationCount()` (`:85-87`) returns
`0` for the object's entire lifetime — and once the scope ends the object is gone. `REQUIRE(scope.
getAllocationCount() == 0)` therefore **can never fail**. Two sibling phases already documented this
exact trap (`dsp/tests/unit/systems/harmonic_cloud_test.cpp:4864-4870`, "Reading it in scope would make
BOTH clauses here vacuous"; `dsp/tests/unit/systems/atmosphere_engine_test.cpp:2279-2288`). Use the
in-repo bracketing pair:

```cpp
std::size_t allocs = 0;
bool sawFrozen = false, sawUnfrozen = false;
{
    [[maybe_unused]] const TestHelpers::AllocationScope scope;
    // ... render only; record observations into plain bools/PODs ...
    allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
}
REQUIRE(allocs == 0);
REQUIRE(sawFrozen);
REQUIRE(sawUnfrozen);
```

**Nothing but the component runs inside the tracked window.** No Catch2 macro (`INFO` builds a
`ScopedMessage` and `REQUIRE` decomposes into strings — both allocate,
`harmonic_cloud_test.cpp:4873-4875`), no `std::vector` growth, no stream formatting. The `isFrozen()`
`true`/`false` observations are recorded into plain `bool`s inside the window and asserted after it.
Buffers are `std::array` or pre-`reserve`d, and one warm-up block is rendered **before** tracking starts
so first-call runtime dispatch is not charged to the loop.

### 8.3 `aether_reverb_matrix_test.cpp`

| Case | Tag | Covers | Strategy |
|---|---|---|---|
| `AetherReverb_MatrixOrthogonality` | — / `[.slow]` | SC-004 clauses 1–5, FR-020–FR-027, C-3, C-8 | Materialise `M(t)` at 101 positions `t = 0.00 … 1.00` via `reset(); setDimensionality(t); processStereoBlock(64 zeros); copyCurrentMatrix(dst, N)` — with P-1 so `t` is exactly the target and the smoother-initialisation rule makes it settled. **(1)** `‖MᵀM − I‖_F ≤ 1e-5`, recomputed **in the test**, at `N=8` always-on and `N=16` `[.slow]`; also assert agreement with `getMatrixOrthogonalityError()`. **(2)** `‖applyCurrentMatrix(x)‖₂` within 1e-4 of `‖x‖₂` for 64 random unit vectors at 21 positions, and `‖copyCurrentMatrix·x − applyCurrentMatrix(x)‖ ≤ 1e-6`. **(3)** negative control: a locally-built naive lerp of the *same* endpoints, measured with the same code, asserted against the spec's exact table **in global `t`** (`t=0.0625→0.8750`, `0.125→1.5000`, `0.1875→1.8750`, **`0.250→2.0000` with `σ_min ≤ 1e-6` and `|det| ≤ 1e-6`**, `0.375→1.5000`, `0.5→0.0000` at `N=8`; the `N=16` column likewise), plus segment 2 at `t = 0.75` exceeding the clause-1 threshold by ≥ 4 orders of magnitude. **(4)** endpoint identity: `t=0` entrywise `I − (2/N)J` to 1e-6; `t=0.5` entrywise `D·H_N/√N` to 1e-6 **including row 0's flipped sign**; `t=1` orthogonal, seed-reproducible to 1e-6 and seed-**sensitive** (max-abs entrywise difference ≥ 0.1 between two seeds). **(5)** `|det(M(t)) + 1| ≤ 1e-5` at all 101 positions |
| `AetherReverb_SchurReduction` | — | SC-004 clause 6, FR-022 | Direct on `AetherReverb::schurReduceSO`, `N ∈ {8, 16}`, over (i) the two shipped endpoint pairs and (ii) ≥32 seeded random `SO(N)` inputs. (a) `‖VᵀV − I‖_F ≤ 1e-6`; (b) `B(θ)` block-diagonal to 1e-6 with `b₀₀=b₁₁`, `b₀₁=−b₁₀`, `b₀₀²+b₀₁²=1` to 1e-6; (c) `‖V·B(θ)·Vᵀ − R‖_F ≤ 1e-6`; (d) `‖A·V·B(0)·Vᵀ − A‖ ≤ 1e-6` and `‖A·V·B(θ)·Vᵀ − B‖ ≤ 1e-6`; (e) degenerate inputs exercised **explicitly** — repeated eigenvalues, `θᵢ = 0`, `θᵢ = π` — because those are what a hand-written reduction gets wrong and what a random `SO(N)` draw effectively never produces. No audio rendered |

Random `SO(N)` generation for (ii): draw Gaussian-ish entries from `Xorshift32`, Gram–Schmidt, negate a
column if `det < 0`.

### 8.4 `aether_reverb_spectral_test.cpp`

| Case | Tag | Covers | Strategy |
|---|---|---|---|
| `AetherReverb_EchoDensity` | — / `[.slow]` | SC-003, FR-011, FR-013, FR-044 | NED exactly as implemented for `FDNReverb` at `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` (1 ms windows, RMS per window, fraction above `peak·0.01`), on the mono sum of the G-3 impulse response. **Window is derived, not fixed:** `t_start` = first 1 ms window above `peak·0.01` (earlier windows excluded from the denominator and the excluded count recorded), `W = max(250 ms, 3·m_long)` with `m_long` read via `getEffectiveDelayLengthSamples`. **(1)** NED ≥ 0.8, always-on `size ∈ {0,0.5,1} × dim ∈ {0,1}`, `N=8`; `[.slow]` the 5×3×2 grid. **(2)** NED non-decreasing over `density ∈ {0,0.25,0.5,0.75,1}`, strictly lower at 0 than at 1. **(3a)** the test recomputes `D = Σ getEffectiveDelayLengthSamples(i) / sr` and requires agreement with `getModalDensityPerHz()` to **0.5 %** at five Sizes — the clause that catches a stale accessor. **(3b)** `D(1)/D(0) == 16` to 1 %, with P-2 asserted first. **(3c)** at `density = 0`, mean inter-arrival time of above-threshold windows scales with `S` to within **15 %** across `size ∈ {0.25, 1.0}`. Plus: the reference tables are pairwise coprime |
| `AetherReverb_TailSmoothness` | — / `[.slow]` | SC-007, FR-060–FR-063, FR-052 | **(1)** across `spectralDiffusion ∈ {0,0.25,0.5,0.75,1}`: (a) M-1 non-decreasing; (b) per-bin peak-to-median in dB **falls by ≥ 3 dB** from 0 to 1; (c) `M-1(1) − M-1(0) ≥ 3·√(SE₀² + SE₁²)` — a significance requirement, so a noisier implementation raises the bar and a stub (difference exactly 0) fails unconditionally. Record `M-1(G-5)` as the empirical ceiling. **(2)** L/R correlation non-increasing over the sweep. **(3)** at amount 0, the wet output matches a `fftSize`-delayed reference rendered with `spectralDiffusionEnabled = false` to **per-sample ≤ 1e-4 and error RMS ≤ −70 dBFS**, with a **negative control** at 50 % overlap (which `stft.h:225-228` forbids for synthesis-windowed modification) that must exceed both. **(4a)** 1/3-octave bands with centres in [100 Hz, 10 kHz]: `level(b) ≤ median{b−2,b−1,b+1,b+2} + 9 dB`, at `damping=0.4`, `density=0.7`, **all sends 0**. **(4b)** the FR-052 comb check: same analysis at both shimmer sends = 1, `bloomSend = 0`, `size = 0.5`, with a **notch** bound — no band more than 9 dB *below* its neighbour median. **(5)** wet RMS over the last 2 s varies ≤ **1.0 dB** across the five amounts (this is what verifies FR-061's `g(a)`; without it the 6 dB coherence loss ships silently). **(6) FR-080 / `setWidth` (no new render — swept on clause 3's amount-0 G-2 render):** L/R correlation of the wet tail at `setWidth ∈ {0, 1}`; require **≥ 0.999 at width 0** (M/S collapse: `wetL = wetR = mid` exactly, `fdn_reverb.h:368-371`) and **strictly lower at width 1**, with both values recorded. `setWidth` is one of the two explicitly non-roadmap-derived controls the spec keeps and justifies (spec.md:111-116, Traceability row spec.md:2398, with a stated Phase 10 obligation to modulate *this* setter rather than add a second width stage); without this clause it appears only in SC-001's allocation sweep and SC-018's latency re-read, both of which an unwired `setWidth` passes, and the only other L/R-correlation measurement (clause 2) sweeps at the default width 1.0 |
| `AetherReverb_ShimmerBloomEffect` | — | SC-016, FR-050–FR-058 | Input G-4, 2 s + 6 s tail, analysis on the last 4 s; all levels expressed relative to an otherwise identical render with the send under test at 0. **(1)** octave send: (a) `L(2f₀) ≥ L_ref(2f₀) + 12 dB`; (b) `L(2f₀) ≥ L(f₀) − 20 dB` (the scale-free "this is a real signal, not an amplified floor" anchor); (c) `L(1.5f₀) ≤ max(L_ref(1.5f₀) + 3 dB, L(2f₀) − 12 dB)`. **(2)** the exact mirror for the fifth — together these measure FR-051's *independent* sends; a single shared gain fails both. **(3)** bloom: `bloomNoteOn(0, {f₀,2f₀,3f₀,4f₀}, 4)` before the render, `bloomSend = bloomDecay = 1`, `REQUIRE(getActiveBloomResonatorCount() > 0)` throughout; the four 1/3-octave target bands rise **≥ 6 dB** while the mean of all non-target bands in [100 Hz, 10 kHz] rises **≤ 2 dB**; then `bloomNoteOff(0)` and, after settling, the four bands fall back within **2 dB** of the reference. **Clause 3 runs in TWO configurations** — the default (spectral stage enabled) and `spectralDiffusionEnabled = false` — with the **same** ≥ 6 dB target-band emphasis required in both, and `REQUIRE(getLatencySamples() == 0)` in the second. That is FR-065's only teeth: the spec requires the bloom to run identically with the spectral stage off (spec.md:1114-1117), and no other case in §8 combines an active bloom with the stage disabled (SC-018 clause 1 disables it with no bloom). Cost ≈ 8 s. **(4)** freeze mutes all three: after freezing, the ±50-cent band at `2f₀` stops growing — level over the last 5 s of a 15 s frozen tail within ±0.5 dB of its level at latch completion. Record every band level and `L(f₀)` |

### 8.5 `aether_reverb_perf_test.cpp` — `[.perf]`

`AetherReverb_CpuBudget`, modelled directly on
`dsp/tests/unit/systems/continuous_body_perf_test.cpp:100-260`:

```cpp
constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;
constexpr double kBlockBudgetNs = (kBlockSize / kSr48) * 1.0e9;   // 10,666,666.67
constexpr double kRegressionFactor = 1.5;
constexpr double kReferenceNs = kBlockBudgetNs * 0.05;            // 533,333.33  (roadmap line 282)
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;
```

**Six** configurations, each with its own checked-in baseline and **two** `static_assert`s
(`baseline * kRegressionFactor <= kReferenceNs` and `baseline <= kMaxAdmissibleNs`):

| # | configuration |
|---|---|
| (a) | `N=8`, defaults, shimmer/bloom/spectral **off** |
| (b) | `N=8`, shimmer `Granular` + bloom + spectral @1024 — **the shipped default** |
| (c) | `N=16`, everything on, spectral @4096, `size=1`, `density=1`, `maxDelaySeconds=0.5`, 32 active bloom resonators — the worst case, and the **only** gated `N=16` configuration (Q3) |
| (d) | (b) frozen and settled — freeze must not be *more* expensive |
| (e) | (b) with `dimensionality` swept continuously — the matrix recomputed every control chunk |
| **(f)** | **the state-clear burst.** Configuration (c) — the largest `delayBuffer_` the perf lane measures — with `silence()` called at a control-chunk boundary; the metric is the **single worst control chunk** during the clear, not the block mean, measured at `maxBlockSamples = 64` so the reported figure is against the tightest deadline the API admits (1.33 ms at 48 kHz). §5.3's amortization is what makes this bounded; without it the whole 1–5 MiB `memset` lands in one chunk. Baseline pinned like the others |

Trial shape: best-of-25 × 500 blocks, eight consecutive runs, worst run rounded up, padded ≤ +5 %.
A **BASELINE PROVENANCE** comment block in the TU records machine, build, trial shape, date and the
eight per-configuration figures — the shape `continuous_body_perf_test.cpp:140-220` uses. The five
ns/block figures are transcribed **verbatim** into `compliance.md` (RA-3), because Phase 7 tallies
measurements, not ceilings.

Lever list in the TU header, in order, for a configuration that misses: SIMD the per-sample `N×N`
multiply and the channel loop → shimmer mode → spectral FFT size → active bloom count → `N`.
**Never** raise a baseline, relax the reference or renegotiate `kRegressionFactor`. **The matrix
mechanism is not a lever.**

### 8.6 `aether_reverb_nonfinite_test.cpp` — the `-fno-fast-math` TU

`AetherReverb_NonFiniteHygiene`. Pinned configuration (clause 3's quantity is meaningless without it):
P-2, P-3, `N = 8`, `decay = 4`, `damping = 0.4`, `size = 0.5`, `dimensionality = 0.35`, all sends 0,
`spectralDiffusion = 0`, life modulation at P-1. Fault at **`t_f = 3.0 s`** of a **10 s** render.

NaN and ±Inf are built **from bit patterns through a `volatile` sink** — never `std::numeric_limits`,
which folds under the macOS leg's `-ffast-math`.

1. No non-finite value ever reaches the output, at any point.
2. Injecting input NaN/Inf only ⇒ `getNonFiniteRecoveryCount()` stays **0** (FR-082 is not FR-083).
3. **Recovery, operationally — and the fault is an INTERNAL one, injected through the §3 hook.**
   Clause 2 already pins the input path as *not* being FR-083's trigger, and §7.14 records why no legal
   call sequence can make internal state non-finite. Clause 3 therefore calls
   `injectNonFiniteStateForTest()` (compiled by §1.2 item 4's `KRATE_DSP_AETHER_TEST_HOOKS`) at
   `t_f = 3.0 s`, which at 48 kHz is sample **144 000 = 64 × 2250**, i.e. exactly a control-chunk
   boundary — the hook's documented precondition, and what guarantees the FR-083 sweep fires at the top
   of the next control step before any sample of that chunk is rendered.
   Render a clean **reference** (no injection); render the **subject** with the fault at `t_f` and G-1
   continuing uninterrupted; assert `getNonFiniteRecoveryCount() == 1` (it was 0 before the injection);
   time-align the reference to the subject's recovery point (§7.14: the first sample at which both
   `clearPending_` is finished and the fade-in has completed). (a) wet RMS over the 100 ms window ending
   at `recovery + 1.0 s` is non-zero and within **±3 dB** of the aligned reference window; (b) that
   difference shrinks monotonically over the four preceding 100 ms windows. The measured convergence
   time is **recorded**, not thresholded.
4. **Setter guards under IEEE semantics (FR-009's clamp, FR-008's non-finite rule).** This is the only
   TU built with `-fno-fast-math`, and R-5 makes the `ITERUM_NOINLINE isFinite` guard load-bearing
   *precisely because* `-ffast-math` would fold it away — yet nothing in §8 previously passed a
   non-finite or out-of-range value to **any** setter. For every float setter in FR-009's table
   (`setSize`, `setDensity`, `setDecaySeconds`, `setDimensionality`, `setDamping`, `setPreDelayMs`,
   `setModDepth`, `setModSmoothness`, `setShimmerOctaveSend`, `setShimmerFifthSend`, `setBloomSend`,
   `setBloomDecay`, `setSpectralDiffusion`, `setSizeBreathDepth`, `setDimensionalityTideDepth`,
   `setWidth`, `setMix`), using the same `volatile`-sink bit-pattern construction as above:
   (a) **NaN sub-case** — call every setter with bit-pattern NaN, then render 1 s of G-1: require 0
   non-finite output samples, `getNonFiniteRecoveryCount() == 0`, **and** `compareFingerprints` equal to
   a render of the same engine with no setter ever called. That equality is what proves the argument
   fell back to the FR-009 **default** rather than propagating or landing on a clamp endpoint.
   (b) **±Inf and ±1e9 sub-cases** — three further 1 s renders (`+Inf`, `−Inf`, and `±1e9` alternating):
   0 non-finite output, `getNonFiniteRecoveryCount() == 0`, peak ≤ 4.0. These clamp to the range
   endpoints, not to the default, so no fingerprint equality is asserted. Cost ≈ 6 s including the
   defaults reference.
5. **`AetherReverb_BloomNoteApi`** (a second `TEST_CASE` in this TU — it needs IEEE semantics for the
   NaN partials). FR-056 states five normative guards and Edge cases 27–31 restate them
   (spec.md:1004-1012, spec.md:2302-2311); none was tested. §8.4's SC-016 clause 3 makes exactly one
   well-formed `bloomNoteOn(0, {f₀,2f₀,3f₀,4f₀}, 4)` call. A NaN or above-Nyquist partial reaching
   `computeResonatorCoeffs` poisons `coeff`/`rSquared` for the whole bank, which then feeds
   `processSympatheticBankSIMD` **inside** the feedback loop — exactly the failure class FR-083's net
   exists for, and Edge case 28 explicitly asserts the counter stays 0. Assertions:
   (a) `bloomNoteOn(0, nullptr, 4)`, `bloomNoteOn(0, partials, 0)` and a call before `prepare()` are all
   no-ops — `getActiveBloomResonatorCount() == 0`, `isPrepared() == false` in the third
   (Edge cases 27, 31);
   (b) `bloomNoteOn(0, partials, 64)` clamps to **32** active slots with no out-of-bounds write — run
   inside the §8.2 bracketing `AllocationScope`, and in the ASan lane under B-3's always-on core;
   (c) partials of bit-pattern NaN, ±Inf, `0.0f`, `−440.0f` and `0.9·sr` followed by a 1 s G-1 render ⇒
   0 non-finite output samples and `getNonFiniteRecoveryCount() == 0` (Edge case 28);
   (d) a repeat `bloomNoteOn(0, …)` for the same `voiceId` leaves `getActiveBloomResonatorCount()`
   **unchanged**, not doubled (Edge case 29); `bloomNoteOff(7)` for a `voiceId` never noted on is a
   no-op.
   Cost ≈ 4 s.

### 8.7 Runtime ledger (B-5) — must be measured and recorded

**The spec's 1 020 s figure is an under-count and this plan adds to it. The corrected arithmetic is done
here, not at build time (B-5's own rule), and it says B-1 is exceeded before any of §8.7's conservatism
is spent — so the demotion order is *expected to be taken*, not held in reserve.**

The two corrections to the spec's ledger:

- **SC-002 is 186 s always-on, not 124 s.** spec.md:1367 asserts "Clauses 2–3 ride on those same
  renders". Arithmetically they cannot: clause 1's always-on configuration pins `tideDepth = 1`
  (spec.md:1421), clause 4 runs at the FR-009 default 0.2 (spec.md:1454-1457), while clause 2's
  **positive control** and clause 3's per-octave measurement both require `tideDepth = 0`
  (spec.md:1440-1444, spec.md:1448-1451). A third ~62 s always-on render at `tideDepth = 0` is
  unavoidable. Recorded as §11 delta **D-10**.
- **The new always-on clauses this plan adds** to close six untestable-requirement gaps.

| Criterion | spec ledger | corrected | delta and why |
|---|---|---|---|
| SC-001 | 30 s | 30 s | — |
| SC-002 | 124 s | **186 s** | +62 s: the `tideDepth = 0` render clauses 2–3 need (above) |
| SC-003 | ≈ 22 s | ≈ 22 s | — |
| SC-004 | ≈ 0 s | ≈ 0 s | matrix-only |
| SC-005 | 83 s | **93 s** | +10 s: FR-031's damping clause D (§8.2) |
| SC-006 | 180 s | 180 s | — |
| SC-007 | ≈ 110 s | ≈ 110 s | +0: FR-080's `setWidth` clause reuses clause 3's amount-0 render |
| SC-009 | ≈ 28 s | ≈ 28 s | — |
| SC-010 | ≈ 40 s | ≈ 40 s | — |
| SC-011 | 2 s | 2 s | — |
| SC-012 | 60 s | 60 s | — |
| SC-014 | ≈ 20 s | **≈ 30 s** | +10 s: §8.6 clause 4's setter-guard renders (≈6 s) and clause 5's bloom-API renders (≈4 s) |
| SC-015 | 160 s | **166 s** | +6 s: clause S, `silence()` non-latching. Clause F costs 0 (it rides the 120 s transition render) |
| SC-016 | ≈ 90 s | **≈ 98 s** | +8 s: FR-065's `spectralDiffusionEnabled = false` configuration of clause 3 |
| SC-017 | ≈ 53 s | **≈ 55 s** | +2 s: the `setModDepth(0)` control render |
| SC-018 | ≈ 15 s | **≈ 17 s** | +2 s: clauses 4 (`setPreDelayMs`) and 5 (FIFO zero-fill) |
| **Total** | ≈ 1 020 s | **≈ 1 120 s** | |

**Wall clock.** 1 120 s of audio at SC-008's 5 % worst case is ≈ **56 s** of DSP; +20 % for offline
analysis ⇒ ≈ **67 s**. **That is over B-1's 60 s.** The demotion order is fixed, is not a judgement
call, and steps (1) and (2) are **expected to be taken**:

| step | action | audio removed | new total | new wall clock |
|---|---|---|---|---|
| (1) | SC-006's tail → 90 s always-on, the 180 s form to `[.slow]` | −90 s | ≈ 1 030 s | ≈ **62 s** |
| (2) | SC-002 clause 4 → `[.slow]` (clause 1 keeps its full-60 s always-on config) | −62 s | ≈ 968 s | ≈ **58 s** ✓ |
| (3) | SC-005's 60 s configuration → `[.slow]` — held in reserve | −72 s | ≈ 896 s | ≈ 54 s |

Each is a demotion to the nightly lane, **never a deletion**, and B-4's prohibition on pruning and
threshold relaxation is unaffected. The estimate is deliberately conservative: most always-on
configurations run with shimmer, bloom and/or spectral diffusion **off**, where the measured real-time
factor is well below 5 %, so steps (1) and (2) may turn out unnecessary. **Measure the always-on wall
clock first, apply only the steps the measurement requires, and record both the measurement and the
steps taken in `compliance.md`.**

---

## 9. Implementation order

Each step ends green — build the TU, run `dsp_effects_tests.exe "AetherReverb_*"`, fix warnings before
tests (build-before-test discipline).

| # | Step | Verify |
|---|---|---|
| 1 | ODR sweep (`grep -rn "class AetherReverb\|struct MatrixMorph\|struct BloomBank\|struct ShimmerTap" dsp/ plugins/ tools/`), header skeleton, banner, includes, `PrepareConfig`, constants, empty method bodies, CMake registration (§1.2) | `dsp_effects_tests` compiles and links; `node tools/lint-layers.js` clean |
| 2 | Delay tables + coprimality `static_assert` + `S(v)` + `maxSizeScale_` + buffer sectioning + `getEffectiveDelayLengthSamples` / `getModalDensityPerHz` / `getMaxSizeScale` | SC-003 clause 3(a)(b), FR-011 companion |
| 3 | `schurReduceSO` + the three endpoints + `MatrixMorph` + `copyCurrentMatrix` / `applyCurrentMatrix` / `getMatrixOrthogonalityError` | **SC-004 in full** — no audio needed, so this lands before the loop exists |
| 4 | FDN core: control grid, delay reads, damping, Jot, DC, matrix apply, taps, width, mix, pre-delay, diffuser, injection | SC-005, SC-003 clause 1–2, SC-012 |
| 5 | Freeze latch (all six FR-033 steps as **per-sample** ramp crossfades, §3's ramp-cadence rule) + `isFrozen` + FR-034 latch + `getStateEnergy` **to §7.15's summation window** | SC-002 clauses 1–3, SC-017 clause 3 |
| 6 | Life modulators (pinned rates), `setSeed`, `reset` re-seed, smoother-initialisation rule | SC-010, SC-017 clauses 1a/2a |
| 7 | Shimmer taps (64-sample cadence, one chunk late) + HF shelf | SC-006, SC-011, SC-016 clauses 1–2 |
| 8 | Bloom bank (kernel, note API, guard, reclaim) — **tune `kBloomSendMax` here** | SC-016 clause 3, SC-002 clause 4, SC-006 |
| 9 | Spectral diffusion + `g(a)` + dry alignment + `getLatencySamples` | SC-007, SC-018 |
| 10 | `silence()` + `emergencyClear()` with §5.3's **amortized** clear, FR-082/FR-083 guards, the `KRATE_DSP_AETHER_TEST_HOOKS` injection hook (§1.2 item 4, §7.14), setter-guard and bloom-API clauses | SC-014 (all five clauses), SC-001, SC-015 clause S |
| 11 | Perf TU + baselines (eight runs), **including configuration (f)**, the worst clear chunk | SC-008 |
| 12 | All gates (§1.4), clang-tidy, `compliance.md` with every measured figure transcribed | SC-013 |

Steps 3 and 4 are independent and can be parallelised. Step 8 is the only one with an expected
measurement-driven constant.

---

## 10. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| **R-1** | **Shimmer runaway at `send = 1`, `decay = 60`.** Per pass each leg contributes `kTapReadNormalisation · send = 0.25` into two channels at unity injection gain; two legs together approach a unit-gain regeneration path, and FR-051 pins the send to *be* the injected gain, so no broadband trim is admissible | SC-006 is the gate, and it is the roadmap's own criterion (line 281) | The only admissible lever is **FR-059's HF shelf** — a header constant, not a spec threshold. Start at a first-order high shelf, corner 6 kHz (clamped ≤ `0.45·sr`), HF gain 0.5 (−6 dB), one state per return path. If SC-006 clause 3's HF-fraction bound fails, lower the corner and/or the shelf gain and record the shipped values |
| **R-2** | **`kBloomSendMax` under- or over-shoots.** The pinned normalisations put the bare loop contribution at 0.088, i.e. ≈0.8 dB of emphasis — far under SC-016's 6 dB | Arithmetic done in §7.10; the spec anticipates it (B-4's escape names the mapping and the normalisations) | Start at 8.0 targeting ≈10.7 dB; the FR-058 guard caps combined loop gain at `kBloomLoopGainCeiling = 0.95`. Tune the constant, **never the criterion**; record the shipped value and the measured emphasis |
| **R-3** | **GCC 13 `-O3` reverb pathology** (100×, documented at `dsp/tests/CMakeLists.txt:704-706` for `reverb_test.cpp` and `fdn_reverb_test.cpp`) would blow B-1's 60 s budget on the Linux lane | Same loop shape; B-1 is a requirement | Cap `aether_reverb_test.cpp` and `aether_reverb_spectral_test.cpp` at `-O2` **from the start** — but in a **separate** `set_source_files_properties` call with the property string `"-O2"` alone (§1.2 item 3). **Do NOT join the existing block at `:707-711`:** its property string is `"-fno-fast-math -fno-finite-math-only -O2"` (`:710`), so joining it would build both TUs under IEEE semantics on Clang/GNU and destroy R-5. Not the perf TU |
| **R-4** | **Block-partition variance from advancing modulators by the slice length.** `BreathingModulator::processBlock` inserts a `setTarget` per call (`:209-216`), so 36+28 ≠ 64 | SC-011's 1e-6 bound is unforgiving | §6.1's structure: control work runs **only** at `sampleCounter_ % 64 == 0` and always advances by a full 64. Same for the shimmer `process(…, 64)` call. Verified reasoning also given for `DiffusionNetwork` (static fast path, `:534-548`) and the STFT pump |
| **R-5** | **`-ffast-math` on the macOS leg folds the finiteness guards away** | FR-008, FR-082, FR-083 | Private `ITERUM_NOINLINE isFinite` composing `detail::isNaN`/`detail::isInf` (`core/db_utils.h:54`, `:175`), the pattern at `primitives/smoother.h:168` and `systems/atmosphere_engine.h:1191` (`ITERUM_NOINLINE` itself is defined at `primitives/smoother.h:39-45`). Only `aether_reverb_nonfinite_test.cpp` gets `-fno-fast-math`; the other four TUs must **not**, or the guard is never proved in its shipping FP mode — which is precisely why §1.2 item 3's `-O2` cap is a **separate** block and not an addition to the `-fno-fast-math -fno-finite-math-only -O2` list at `:707-711`. **And the guard is now actually exercised:** §8.6 clause 4 drives every float setter with bit-pattern NaN/±Inf/±1e9 in this TU. `node tools/lint-nonfinite-symbols.js` gates the helper reuse |
| **R-6** | **Denormals in a 60 s frozen tail.** FR-036 forbids the tickle under freeze, and freeze is exactly the state where levels decay toward the denormal floor | Edge case 25; SC-008 (d) | Document that the caller is expected to have FTZ/DAZ on (`core/scoped_denormal_mode.h`; `dsp/tests/dsp_test_main.cpp:12-13` enables it for every DSP test exe). The frozen loop is exactly lossless by construction so levels do **not** decay; the risk is confined to a tail entered near silence |
| **R-7** | **Hand-written Jacobi/Schur reduction is subtly wrong in a way the geodesic absorbs** — mis-ordered angles, a `θ` of the wrong sign, an identity block where a rotation belongs — still yielding orthogonal matrices and exact endpoints while traversing an unspecified path | SC-004 clauses 1–5 cannot see it | SC-004 clause 6 tests the helper directly, including the degenerate cases a random `SO(N)` draw never produces. Making `schurReduceSO` public is what makes that testable without a friend declaration |
| **R-8** | **Float bit-goldens creeping in** via a "pin this render" reflex | `node tools/lint-float-bit-goldens.js` is a CI gate; both CI legs have been broken by this before | SC-010 uses `render_fingerprint.h` only. No FNV over sample bits anywhere |
| **R-9** | **AVX-512 runner lottery** if the SC-008 SIMD lever is taken | Known intermittent Linux-CI-only SIGSEGV from aligned loads | `hn::LoadU`/`StoreU` unless alignment is proven; `node tools/lint-simd-aligned-loadstore.js` |
| **R-10** | **`prepare` at 8 kHz with `diffusionFftSize = 4096`** gives a 512 ms hop and a 4096-sample (512 ms) reported latency | Legal per FR-003; not a defect, but surprising | Header states it next to the latency contract; Edge case 19's inverse (192 kHz, unchanged sample latency, quartered time resolution) is stated the same way |
| **R-11** | **The state-clear burst blows a callback deadline.** `silence()` / `emergencyClear()` touch 1–5 MiB of state (§4's table plus the four STFTs and both `PitchShiftProcessor`s); `maxBlockSamples` admits 64, so the deadline can be 1.33 ms at 48 kHz and **0.33 ms at 192 kHz** | CPU budgets are functional requirements here (SC-008), and this is the one burst the design leaves unbounded in wall-clock terms if it is done in a single control chunk | §5.3's amortization: `clearQuotaFloats_` sized at `prepare` from the fade window, one delay-buffer slab **and** at most one sub-object `reset()` per control chunk; literal-zero writes into the delay lines and a literal-zero wet path while `clearPending_`, so nothing leaks through a `× 0` product. SC-008 configuration **(f)** measures and pins the worst clear chunk |
| **R-12** | **The FR-083 test hook rots or leaks into the shipping build.** A `#if`-guarded member changes the class definition, which is an ODR hazard if only some TUs in an image define the macro | The hook is the only way SC-014 clause 3 and FR-083's detection branch are reachable at all (§7.14) | `KRATE_DSP_AETHER_TEST_HOOKS` is defined **target-wide** on `dsp_effects_tests` (§1.2 item 4), never per-source, so every TU in that image agrees. `KrateDSP` never compiles `aether_reverb.h` (header-only, no `.cpp`), and `dsp_lint_stub` is a separate `EXCLUDE_FROM_ALL` object library (`dsp/CMakeLists.txt:200-203`). The header states that the shipping build has no such member |

---

## 11. Spec deltas — where this plan departs from, or completes, the spec

Each is a place the spec's letter is incomplete or self-inconsistent. None changes a threshold.

- **D-1 — `core/audio_constants.h` is added to FR-002's "closed" include list.** FR-057 re-derives
  `computeResonatorPeakGainInverse`, which uses `kDenormalGuard` (`core/audio_constants.h:40`;
  `systems/sympathetic_resonance.h:407`, `:416`). It is Layer 0, so the include is legal. This is the
  same class of omission FR-002 itself already fixed for `interpolation.h`. The alternative — a private
  local constant — duplicates a Layer 0 value and is rejected on the same grounds FR-008 rejects a
  fourth finiteness helper. Conversely, `window_functions.h` and `pitch_utils.h` are **dropped** from
  the list: `WindowType` arrives via `stft.h:21` and no pitch conversion is performed.
- **D-2 — `schurReduceSO` is `public static`, not private.** FR-022 calls it "a private static helper"
  and simultaneously requires it to carry its own unit tests (SC-004 clause 6) while the spec forbids
  friend-declaring tests (FR-086's preamble, SC-004's justification for `copyCurrentMatrix`). The two
  cannot both hold. Public-static keeps it class-nested (no namespace-scope ODR surface,
  `lint-odr.js` clean) and makes clause 6 measurable.
- **D-3 — `setWidth` and `setMix` gains are computed per control chunk, applied per sample.** FR-009's
  table says "per sample" for both; FR-081 says mix is "computed once per sub-block, not per sample";
  FR-019 requires block-rate values to be snapshotted per sub-block. The plan follows FR-019/FR-081.
  Same resolution for `setPreDelayMs`: the smoothed length is read per chunk, the two `DelayLine`s are
  written and read per sample.
- **D-4 — FR-083's "invokes `silence()`" becomes `emergencyClear()`.** `silence()` ramps the output down
  over 20 ms *before* clearing; ramping a non-finite value is not possible. The plan zeroes the
  O(N)-sized scalar state (where the detected NaN lives) **immediately**, fades **in** over
  `kSilenceRampMs`, and amortizes the bulk clear across that fade-in exactly as §5.3 does for
  `silence()` — with literal-zero delay writes and a literal-zero wet path meanwhile, so nothing leaks
  through a `× 0` product. SC-014 clause 3's recovery point is the end of the fade-in (by construction
  the clear finishes first), which is what the criterion already describes ("the first sample after
  `silence()`'s ramp completes").
- **D-5 — SC-003's stated window at `size = 0.5` is off.** The spec says "at `size = 0.5` the 250 ms
  floor governs". With FR-011's tables `m_long = 5087` samples = 106.0 ms at `S = 1`, so
  `3·m_long = 318 ms > 250 ms` and the derived term governs. The 250 ms floor governs only below
  `size ≈ 0.46`. **No threshold moves** — the formula `W = max(250 ms, 3·m_long)` is used exactly as
  written; only the prose example is wrong. The spec's other stated figure (`1.27 s` at `size = 1`) is
  reproduced exactly by these tables, which is corroboration that the tables are the intended ones.
- **D-6 — the "mirrored list at `dsp/tests/CMakeLists.txt:574-593`" does not exist.** SC-013's preamble
  and the Success-Criteria header describe a second `dsp_effects_tests` source list there. Verified:
  `add_executable` appears only at `:22, :87, :156, :299, :364`, and lines `426-701` are the single
  `set_source_files_properties(... -fno-fast-math -fno-finite-math-only)` block. §1.2 registers the five
  new TUs in the one real place and adds only the non-finite TU to the fast-math block.
- **D-7 — `kShimmerFifthInjectChannels` at `N = 16`.** FR-050's table gives the rule `{3, 3N/4}` and the
  cell `{3, 3N/4}`; `3N/4 = 12` at `N = 16`. §7.9 writes `{3, 12}` explicitly. Note that 12 is **even**,
  as is 8 for the octave pair's `{1, 8}` — both pairs still span both parities (`1` odd + `8` even,
  `3` odd + `12` even), which is the property FR-050 requires.
- **D-8 — a `KRATE_DSP_AETHER_TEST_HOOKS`-gated `injectNonFiniteStateForTest()` is added, because
  FR-083 is otherwise untestable.** The spec gives no mechanism by which internal state can become
  non-finite: FR-082 replaces every non-finite *input* sample with `0.0f` before it enters the loop
  (spec.md:1212) and SC-014 clause 2 explicitly requires that path to leave the counter at **0**
  (spec.md:1972); FR-009 makes every setter fall back to its default (spec.md:496-497); FR-056 clamps
  every bloom partial before coefficient computation (spec.md:1004-1012); and FR-025 + FR-032
  (spec.md:769-771, `:776-780`) make the unfrozen loop structurally non-expansive, so no legal input can
  drive it to Inf. Tests may not friend-declare the class (spec.md:1562). SC-014 clause 3's "recovery
  point (first sample after the fade-in completes)" therefore never occurs, and FR-083's
  detect → `emergencyClear()` → `getNonFiniteRecoveryCount()` branch is unreachable dead code — clause 3
  silently degenerates into a second measurement of clause 2. The hook writes a bit-pattern NaN into
  `filterState_[0]`, the exact array FR-083 sweeps (§7.14), is compiled only for `dsp_effects_tests`
  (§1.2 item 4, target-wide so there is no ODR hazard), and is **absent from the shipping build**. No
  threshold moves: SC-014's three clauses are asserted as written, with clause 3 now measuring what it
  says it measures.
- **D-9 — FR-086's "the sum of squares of the **entire** FDN delay-line contents" (spec.md:1244) is
  corrected to "the active `m_i` samples of each line".** The two differ by construction: §4's sections
  are `nextPowerOf2(ceil(ref_i·S_max·1.005) + 4)`, e.g. 32768 for a longest line of 20 348 samples at
  `S = 4`, so a whole-buffer sweep includes up to ~60 % stale history at `S = 4` and ~96 % at
  `S = 0.25`, plus ~0.7 s of pre-freeze content right inside SC-002 clause 1's ±0.5 dB window. The
  quantity FR-025 (spec.md:769-771) actually conserves is the L2 norm of the `N`-channel state vector,
  `Σ_i Σ_{k=1..m_i} x_i[n−k]²`. §7.15 gives the binding definition. **No threshold moves** — the
  ±0.5 dB bound is asserted as written, against the quantity that makes it a derivation rather than a
  hope.
- **D-10 — spec.md:1367's "Clauses 2–3 ride on those same renders" is arithmetically false.** SC-002
  clause 1's always-on configuration pins `tideDepth = 1` (spec.md:1421) and clause 4 runs at the FR-009
  default 0.2 (spec.md:1454-1457), while clause 2's positive control and clause 3's per-octave
  measurement both require `tideDepth = 0` (spec.md:1440-1451). A third ~62 s always-on render is
  unavoidable, taking SC-002's always-on cost from 124 s to **186 s** and the ledger past B-1 before any
  of this plan's additions. §8.7 carries the corrected arithmetic and states that B-5's demotion steps
  (1) and (2) are **expected to be taken**. **No clause is pruned and no threshold is relaxed** — B-4 is
  satisfied by demotion to the nightly lane, which is exactly the escape B-5 pre-decided.
- **D-11 — SC-017 clause 1a's stated expected peak-to-peak has the wrong units** (spec.md:2130). It
  gives `S(clamp(size + depth·b_max,0,1)) − S(clamp(size + depth·b_min,0,1))`, a dimensionless scale
  difference evaluating to ≈3.75 at `depth = 1`, and compares it against
  `getEffectiveDelayLengthSamples(0)`, which is `refDelaySamples_[0]·S` — ≈3626 **samples** at 48 kHz.
  A literal implementation fails by ~3 orders of magnitude. The plan multiplies by the channel's
  reference length (§8.2). **The 80 % threshold is unchanged**; only the quantity it is 80 % of is
  corrected.
- **D-12 — `LinearRamp` has no `advanceSamples`, so the freeze and gate ramps run per sample.** The
  spec's Existing-components row (spec.md:372) cites `advanceSamples(size_t)` once, at
  `primitives/smoother.h:243` — an `OnePoleSmoother` method — while giving paired refs for every
  `LinearRamp` method that does exist. `LinearRamp`'s complete public API is `smoother.h:329, 342, 358,
  364, 370, 394, 409, 414, 421, 434, 442`; there is no closed-form N-sample advance, and RA-1 forbids
  adding one. Independently, a per-chunk advance of a *crossfade coefficient* is a staircase, not a
  ramp (~0.6 dB steps on the summed output during `silence()`, and a read-pointer step every 64 samples
  during the freeze latch), which SC-015's **0** `ClickDetector` detections asserts against. §3's
  ramp-cadence rule and §6.3 step 0 advance both ramps with `LinearRamp::process()` per sample; the
  *targets* stay on the control grid, and SC-011's partition-invariance argument is unaffected because
  `process()` has no block-boundary state.
- **D-13 — FR-083's amortized clear and the FIFO underflow rule are plan-level completions.** FR-083
  says "invokes `silence()`" and FR-060–FR-065 say nothing about what the wet pump emits before the
  first STFT frame exists. Neither is a threshold change: §5.3/§7.14 bound the clear's wall-clock cost
  (which SC-008 measures as configuration (f)) and §7.11 states the zero-fill that establishes the
  `fftSize` offset FR-062 aligns the dry path to (which SC-018 clause 5 asserts).

---

## 12. Open questions for the user

1. **`kBloomSendMax` and `kBloomLoopGainCeiling` (R-2).** The plan starts at `8.0` / `0.95` from the
   arithmetic in §7.10, but the shipped values are a measurement against SC-016 clause 3 and SC-006.
   Confirm that tuning these two constants (and only these, plus FR-059's shelf) is the intended
   response if the 6 dB emphasis is not reached — i.e. that B-4's escape is read this way.
2. **`dsp/lint_all_headers.cpp` and the `dsp/CMakeLists.txt` header list (§1.3).** Phases 4 and 5 did
   not register `continuous_body.h` or `atmosphere_engine.h` there, so those headers get no strict
   clang-tidy pass. Register `aether_reverb.h` (breaking with the precedent, +2 lines), or match the
   precedent and leave it out?
3. **GCC `-O2` cap (R-3, §1.2 item 3).** The plan caps `aether_reverb_test.cpp` and
   `aether_reverb_spectral_test.cpp` at `-O2` pre-emptively, on the strength of the documented 100×
   GCC 13 reverb pathology, in a **separate** `set_source_files_properties` call carrying `"-O2"` alone
   — *not* by joining the existing block at `dsp/tests/CMakeLists.txt:707-711`, whose property string
   also carries `-fno-fast-math -fno-finite-math-only` and would put both TUs under IEEE semantics on
   the very legs where `-ffast-math` is the thing being guarded against (R-5). The alternative is to
   ship without the cap and add it after a red Linux lane. Pre-emptive is recommended; confirm.
4. **The FR-083 test hook (§1.2 item 4, §7.14, §11 delta D-8).** SC-014 clause 3 and FR-083's detection
   branch are unreachable without a deliberate fault-injection surface — every input path into the
   engine is sealed and tests may not friend-declare the class. The plan ships a
   `KRATE_DSP_AETHER_TEST_HOOKS`-gated `injectNonFiniteStateForTest()`, absent from the shipping build.
   The only alternative that does not add a hook is to restate SC-014 clause 3 as measuring
   `emergencyClear()`'s recovery via `silence()` and to mark FR-083's detection branch as covered by
   **code inspection only** — which is a real reduction in coverage and would have to be written into
   the spec, not left implicit. Confirm the hook.
5. **Expected `[.slow]` demotions (§8.7).** The corrected ledger is ≈1 120 s ⇒ ≈67 s, past B-1's 60 s,
   so B-5's demotion steps (1) SC-006's tail → 90 s always-on and (2) SC-002 clause 4 → `[.slow]` are
   expected to be taken (⇒ ≈58 s). Both are pre-decided by the spec and neither prunes a clause or
   relaxes a threshold. Confirm that taking them on the measurement — rather than treating ≈67 s as
   acceptable — is the intended reading of B-1.

### Resolutions (recorded before build stage, 2026-07-29)

All five resolved with the plan's recommendation; none changes a grill decision, prunes a clause,
or relaxes a threshold:

1. **CONFIRMED.** Tune `kBloomSendMax` / `kBloomLoopGainCeiling` (+ FR-059's shelf) only, never the
   criterion — this is the same rule the Q7 clarification already pinned.
2. **Register `aether_reverb.h`** in `dsp/lint_all_headers.cpp` and the `dsp/CMakeLists.txt` header
   list (+2 lines, breaking the Phase 4/5 precedent). New surface only; the older headers are out of
   this phase's scope.
3. **CONFIRMED pre-emptive `-O2` cap**, as a separate `set_source_files_properties` call carrying
   `"-O2"` alone (not joined to the `-fno-fast-math` block).
4. **CONFIRMED the `KRATE_DSP_AETHER_TEST_HOOKS`-gated `injectNonFiniteStateForTest()`** — the
   inspection-only alternative is a real coverage reduction and is rejected.
5. **CONFIRMED** — take the two pre-decided demotions on the measured ledger.
