// ==============================================================================
// Layer 4: Effect Tests - AetherReverb, CPU budgets (SC-008), [.perf]
//                                        (specs/seraphis-phase6-aether-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase6-aether-space/spec.md   (SC-008, :1792-1821)
//            specs/seraphis-phase6-aether-space/plan.md   (S8.5, :1612-1647)
//            specs/seraphis-phase6-aether-space/tasks.md  (T001 creates this TU,
//                                                          T014 fills it)
//
// SCOPE OF THIS TU (plan S1.1's TU-ownership table): SC-008 only. Every case
//   here carries the [.perf] tag so it is excluded from the default run and
//   selected with:  dsp_effects_tests.exe "[.perf]"
//
// COMPILE FLAGS: none, deliberately. This TU is absent from BOTH the
//   -fno-fast-math list and the -O2 cap list in dsp/tests/CMakeLists.txt: either
//   flag would change the figures the CPU baselines are pinned to. In
//   particular the GCC -O2 cap that aether_reverb_test.cpp and
//   aether_reverb_spectral_test.cpp carry must NOT be extended to this file.
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. SC-008 therefore pins
//   the measurement basis to NANOSECONDS PER 512-SAMPLE BLOCK (the basis
//   dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp established and
//   dsp/tests/unit/systems/continuous_body_perf_test.cpp:104-137 reuses) and
//   gates against a CHECKED-IN BASELINE as a relative regression bound
//   (fail if > baseline x 1.5). The percent-of-budget figure is REPORTED via
//   WARN, never asserted.
//
// HOW THE ABSOLUTE ROADMAP FIGURE IS STILL BOUND (spec.md:1797-1813):
//   roadmap line 282 makes "CPU <= 5 % of one core, global" a FUNCTIONAL
//   requirement, and roadmap lines 496-497 make CPU budgets FRs generally, so a
//   purely relative gate would not discharge SC-008. Each checked-in baseline
//   therefore carries BOTH compile-time clauses
//
//       static_assert(baseline * kRegressionFactor <= kReferenceNs, ...);
//       static_assert(baseline <= kMaxAdmissibleNs, ...);
//
//   alongside the run-time REQUIRE(measured <= baseline * kRegressionFactor).
//   The two COMPOSE: a baseline that would let `measured` exceed the reference
//   does not COMPILE, so the run-time REQUIRE transitively binds the absolute
//   figure on every machine and every run. The "[.perf]" tag keeps the TIMING
//   out of CI (.github/workflows/ci.yml excludes perf-tagged cases), but the
//   static_asserts are evaluated by every CI leg regardless of tags - which is
//   exactly why the gate is placed there.
//
// AetherReverb IS GLOBAL - ONE INSTANCE (spec.md:1797-1798, roadmap line 262).
//   Unlike the Phase 3/4/5 per-voice budgets, this figure does NOT multiply by
//   polyphony. RA-3 (spec.md:255-272) requires all six ns/block figures to be
//   transcribed verbatim into compliance.md so Phase 7 tallies MEASUREMENTS,
//   not ceilings.
//
// IF A MEASUREMENT IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE.
//   The lever list, in order (spec.md:1814-1820, plan S8.5, plan S12 "SIMD
//   lever" at plan.md:1415):
//     1. SIMD the per-sample N x N matrix multiply (FR-024) AND the per-channel
//        loop. 64 MACs at N = 8, 256 at N = 16, every sample - by far the
//        largest single term at N = 16.
//        IF THIS LEVER IS TAKEN: hn::LoadU / hn::StoreU UNLESS ALIGNMENT IS
//        PROVEN. `node tools/lint-simd-aligned-loadstore.js` enforces it, and an
//        aligned load on an AVX-512 runner is the known cause of intermittent
//        Linux-CI-only SIGSEGV (risk R-9, plan.md:1801).
//     2. Shimmer mode (FR-053). PitchMode::Granular is the shipped default;
//        PitchMode::Simple is far cheaper and PhaseVocoder far dearer. Changing
//        it changes FR-054's loop-time figure, so it is a documented trade.
//     3. Spectral-diffusion FFT size. Configuration (c) runs @4096; the shipped
//        default is @1024. Halving it halves the analysis/synthesis term and
//        halves the reported latency (FR-084, RA-2).
//     4. Active bloom resonator count. kMaxBloomResonators = 32 is the ceiling
//        configuration (c) measures; the kernel is
//        processSympatheticBankSIMD (systems/sympathetic_resonance_simd.h:39-50)
//        and its cost is linear in the count.
//     5. N. Both orders ship (Q3) and N = 8 is the default; dropping the N = 16
//        option is the last lever, not the first.
//   THE MATRIX MECHANISM IS NOT A LEVER. FR-022 pins the real-Schur geodesic
//   (Q4) and its cost is one O(N^3) product per 64 samples, not per sample.
//   Swapping it changes the shipped Dimensionality axis: that is a spec change,
//   not a performance tweak.
//   NEVER raise a baseline, never relax the reference, never renegotiate
//   kRegressionFactor at implementation time.
//
// This TU does NOT inject non-finite values, so it is deliberately NOT listed in
// the -fno-fast-math -fno-finite-math-only source-property block. It also has no
// std::isnan / std::isinf / numeric_limits infinity anywhere: the macOS leg
// builds with -ffast-math, which folds them. Finiteness is checked on the
// IEEE-754 exponent field instead (isFiniteValue below).
//
// NO ALLOCATION-TRACKING INCLUDES HERE: this TU must not pull in
// <allocation_operator_overrides.h> (duplicate-symbol link error against the
// single owner in this binary), and does not need it - SC-001's allocation
// sweep lives in aether_reverb_test.cpp.
//
// FTZ/DAZ: dsp/tests/dsp_test_main.cpp:12-13 calls enableFTZDAZ() before any
// case runs, so every figure below is measured with denormals flushed BY THE
// PROCESS - the same environment the audio thread runs in. That matters most
// for configuration (d): FR-036 forbids the denormal tickle under freeze
// (risk R-6, plan.md:1798).
//
// Run it explicitly (it is excluded by tag everywhere else):
//   build/windows-x64-release/bin/Release/dsp_effects_tests.exe "[.perf]"
//   build/windows-x64-release/bin/Release/dsp_effects_tests.exe "AetherReverb_CpuBudget*"
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/effects/aether_reverb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::AetherReverb;
using Krate::DSP::PitchMode;

namespace {

// =============================================================================
// Measurement basis (SC-008, spec.md:1793-1798; plan.md:1617-1624)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// Relative regression bound applied to every checked-in baseline (SC-008).
constexpr double kRegressionFactor = 1.5;

/// The roadmap's 5 % GLOBAL ceiling (roadmap line 282), 533,333.33 ns/block.
/// One instance, post-voice-sum, so this does NOT multiply by polyphony (RA-3).
constexpr double kReferenceNs = kBlockBudgetNs * 0.05;

/// The largest baseline the static_asserts can accept. A measurement above this
/// means the phase is OVER BUDGET: the response is the numbered lever list in
/// this file's header, NEVER a raised baseline.
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;

/// Configuration (f) is measured one CONTROL CHUNK at a time, not one block.
constexpr std::size_t kChunkSamples = AetherReverb::kControlChunkSamples;
constexpr std::size_t kChunksPerBlock = kBlockSize / kChunkSamples;

/// The tightest deadline PrepareConfig admits: maxBlockSamples = 64 at 48 kHz,
/// i.e. 1,333,333.33 ns. Configuration (f)'s figure is REPORTED against this
/// (and GATED, like the other five, against kReferenceNs - see (f)'s note in
/// BASELINE PROVENANCE).
constexpr double kChunkDeadlineNs = (static_cast<double>(kChunkSamples) / kSr48) * 1.0e9;

// --- Structural clauses: what these numbers describe -------------------------
// If any of these moves, the measurement no longer describes what SC-008
// specified and the TU stops compiling rather than silently reporting a figure
// for a different configuration.

static_assert(kBlockSize % kChunkSamples == 0,
              "the measured block must be a whole number of control chunks");
static_assert(kChunksPerBlock == 8, "SC-008 measures 8 control chunks per 512-sample block");
static_assert(AetherReverb::kControlChunkSamples == 64,
              "configuration (f)'s 1.33 ms deadline is the 64-sample control chunk");
static_assert(AetherReverb::kMaxBloomResonators == 32,
              "SC-008 configuration (c) measures the kMaxBloomResonators = 32 ceiling (Q7)");

// =============================================================================
// BASELINE PROVENANCE (SC-008)
// =============================================================================
//   Machine    : 13th Gen Intel(R) Core(TM) i9-13900HX, idle, on AC
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-25 x 500 blocks per configuration (see the trial-shape
//                comment below); configuration (f) uses its own burst shape
//   Measured   : 2026-07-30. The eight-run procedure below WAS EXECUTED and its
//                result is recorded here IN FULL. THE SIX CONSTANTS ARE NOW
//                DERIVED FROM DATASET 1, per the PHASE-OWNER RULING of
//                2026-07-30 (see "PHASE-OWNER RULING" below). Read all of it
//                before touching them.
//
//   DATASET 1 - eight consecutive runs, quiet machine, ns/block:
//   run           |    1       2       3       4       5       6       7       8 |    max
//   (a) core      |  62719   62763   61446   66279   61277   63926   64693   64048 |  66279
//   (b) default   | 104811  106000  109138  104677  105632  108571  106526  108522 | 109138
//   (c) worst     | 172350  171667  190584  187612  182329  187255  185930  184161 | 190584
//   (d) frozen    |  93755   91931   89371   88053   92675   86926   92335   89342 |  93755
//   (e) dim sweep | 107125  110137  112822  107601  108292  107929  109509  111934 | 112822
//   (f) clear     |  45900   46800   50000   48200   45100   51200   50000   49000 |  51200
//
//   Steps 2-3 applied to DATASET 1 give, as ceil(worst x 1.05):
//     (a) 69593   (b) 114595   (c) 200114
//     (d) 98443   (e) 118464   (f) 53760
//   THESE ARE THE SHIPPED CONSTANTS below (:322/:326/:330/:334/:338/:343). All
//   six are under kMaxAdmissibleNs = 355555.6, so step 3's cap does not bind and
//   the phase is not over budget.
//
//   A near-identical set (69000 / 114000 / 200000 / 98000 / 118000 / 53000) was
//   pinned once before, built and re-run - AND THE SAME BINARY FAILED THE GATE
//   WITHIN THE HOUR, on the same machine:
//
//   DATASET 2 - four runs, same binary, ~40 min later, same machine idle on AC
//   but thermally soaked after sustained load, ns/block:
//   run           |    9      10      11      12 |    max  | vs dataset-1 max
//   (a) core      | 142826  130805  120769  133368 | 142826 |  2.15x
//   (b) default   | 248282  238134  237041  234107 | 248282 |  2.27x
//   (c) worst     | 392689  386255  399509  364521 | 399509 |  2.10x
//   (d) frozen    | 209383  206464  218853  205171 | 218853 |  2.33x
//   (e) dim sweep | 246604  239375  247732  239482 | 247732 |  2.20x
//   (f) clear     | 129400  121700  122700  142300 | 142300 |  2.65x
//
//   THIS IS THE MACHINE, NOT THE CODE, and there is independent evidence:
//   over the same window the ALWAYS-ON Aether lane's summed case durations rose
//   12.565 s -> 23.047 s (1.83x) on a completely different workload, and no
//   Aether source changed between the two datasets. Dataset 2's spread is also
//   tight (< 10 % within itself), so it is a stable second state, not a
//   transient - which is exactly what makes it fatal for a x1.5 gate cut from
//   dataset 1 alone.
//
//   PHASE-OWNER RULING, 2026-07-30 - THE CLAUSE IS KEPT AND THESE BASELINES SHIP.
//   The previous revision of this block retained T014's estimates (150000 /
//   240000 / 340000 / 240000 / 250000 / 350000) on the argument that only they
//   pass in BOTH machine states. The phase owner ruled otherwise:
//     - SC-008's provenance clause (spec.md:1836-1837 as of this edit, "the
//       worst of at least eight consecutive best-of-N runs, +5 % max") STAYS
//       UNCHANGED. It was not amended, and the estimates violated it.
//     - The DATASET 1 quiet-machine derivation is ACCEPTED as the clause's
//       "eight consecutive best-of-N runs", and the six ceil(worst x 1.05)
//       figures above are now THE SHIPPED BASELINES. The gate is a measurement,
//       not an allowance.
//     - The DATASET 2 load-state failure below is a KNOWN, ACCEPTED RISK: this
//       lane is [.perf], excluded from CI, and run by hand. If it fails on a
//       thermally soaked or loaded machine, WAIT AND RE-RUN ON A QUIET ONE. Do
//       NOT loosen these constants; that is what the ruling forbids.
//   For the record, what the estimates bought and what was given up: dataset 2's
//   worst cleared every estimate at x1.5 (142826 <= 225000, 248282 <= 360000,
//   399509 <= 510000, 218853 <= 360000, 247732 <= 375000, 142300 <= 525000) and
//   clears NONE of the shipped baselines. Merging both datasets under steps 2-3
//   would demand a (c) baseline of 419484 ns, ABOVE kMaxAdmissibleNs (355556) -
//   it would not compile - even though 399509 ns/block is only 3.75 % of one
//   core and inside the roadmap's 5 %. That is the defect in the procedure, not
//   in the engine, and it is why the accepted risk is stated rather than hidden.
//
//   FOR WHOEVER RUNS THE PROCEDURE NEXT: step 2's "+5 %" is a ROUNDING
//   allowance, not a noise margin, and eight runs taken inside one quiet window
//   do not sample this machine's variability. Either spread the eight runs
//   across machine states (cold and thermally soaked, at minimum) and take the
//   worst, or derive the pad from the measured spread (~2.2x here) rather than
//   assuming 5 % - but that is a SPEC change to SC-008's clause, and the clause
//   was deliberately kept as written by the 2026-07-30 ruling.
//
//   FOR RA-3 / Phase 7's TALLY, the number to carry is the quiet-machine
//   dataset-1 figure - (b) = 109138 ns/block = 1.023 % of one core, GLOBAL - and
//   the worst case (c) = 190584 ns = 1.787 %. Those are measurements of the
//   engine. Dataset 2 measures a throttled laptop.
//
// REPLACEMENT PROCEDURE, and it is the only legal way these constants change:
//   1. Run  dsp_effects_tests.exe "AetherReverb_CpuBudget*"  at least EIGHT
//      consecutive times on an IDLE machine, on AC, Release - SC-008's clause as
//      written, and as the 2026-07-30 ruling kept it. Know what that does not
//      cover: eight runs inside one quiet window measured a 2.1-2.7x-optimistic
//      figure on this machine (DATASET 1 vs DATASET 2 above), so a gate cut this
//      way WILL fail on a thermally soaked or loaded machine. That is the
//      accepted risk, not a licence to loosen the constants.
//   2. Per configuration take the WORST across those runs, round it up, pad by
//      at most +5 % - i.e. ceil(worst x 1.05), which is exactly how the six
//      shipped figures were derived from DATASET 1.
//   3. baseline = min(that figure, kMaxAdmissibleNs).
//   4. Fill the table above with the eight figures, the machine and the date,
//      and transcribe all six into compliance.md verbatim (RA-3, tasks T018).
//   5. If step 3's cap BINDS for any configuration - i.e. the measurement
//      exceeds kMaxAdmissibleNs - the phase is OVER BUDGET. Work the lever list
//      in this file's header. Do not raise the baseline; it will not compile.
//   A measured figure may only ever move a baseline DOWN relative to the
//   figure it replaces, or fail the build.
//
// WHAT EACH CONFIGURATION IS, and why it is in the set (spec.md:1801-1807):
//   (a) N = 8, FR-009 defaults, shimmer + bloom + spectral diffusion all OFF at
//       prepare. The FDN core, the input diffuser and the life modulators
//       alone - the line item every other configuration is read against.
//   (b) N = 8, the SHIPPED DEFAULT: shimmer on in PitchMode::Granular, bloom on
//       and driven, spectral diffusion on @1024. This is the figure Phase 7's
//       tally actually consumes.
//   (c) N = 16, everything on, spectral @4096, size = 1, density = 1,
//       maxDelaySeconds = 0.5 (P-2), kMaxBloomResonators = 32 driven. The worst
//       case AND the ONLY configuration in which the shipped N = 16 order is
//       gated at all (Q3). Whether N = 16 is ever promoted to the default is
//       decided from THIS measured figure in compliance.md, not from an
//       estimate.
//   (d) (b) frozen and settled. FREEZE MUST NOT BE MORE EXPENSIVE: FR-034 skips
//       updateGeometry() and updateDecayAndDamping() entirely while frozen
//       (aether_reverb.h:3515-3519) and FR-033 step 5 mutes all three sends, so
//       the frozen figure should be at or below (b). A frozen figure ABOVE (b)
//       is a defect, not a budget item - and it is exactly what a freeze that
//       kept recomputing latched geometry would produce.
//   (e) (b) with dimensionality swept, one setDimensionality per 64-sample
//       control chunk, so updateMorph() re-materialises the matrix on EVERY
//       chunk instead of skipping on the kMorphEpsilon gate
//       (aether_reverb.h:1430). The geodesic is O(N^3) per chunk (plan.md:
//       1024-1026), and this is the only configuration that pays it every time.
//   (f) THE STATE-CLEAR BURST. Configuration (c) - the largest delayBuffer_ the
//       perf lane builds - with silence() called at a control-chunk boundary,
//       measured at maxBlockSamples = 64. The metric is THE SINGLE WORST
//       CONTROL CHUNK during the clear, not the block mean: plan S5.3's amortization
//       (aether_reverb.h:1840-1856, :3636-3658) is what turns a 1-5 MiB memset
//       into ~15 bounded slabs, and a block MEAN would average that burst away
//       to nothing - which is precisely the regression this configuration
//       exists to catch.
//       ON THE GATE FOR (f): it is held to the SAME kReferenceNs /
//       kMaxAdmissibleNs constants as the other five, per spec.md:1808-1810.
//       That is a real bound and not a formality: a 64-sample chunk's own
//       real-time deadline is kChunkDeadlineNs = 1,333,333 ns, so a baseline at
//       kMaxAdmissibleNs caps the worst clear chunk at 40 % of the deadline it
//       has to meet, i.e. 2.5x margin on the tightest configuration the API
//       admits. The percentage of kChunkDeadlineNs is REPORTED alongside.
// =============================================================================

/// (a) N = 8, defaults, shimmer / bloom / spectral OFF.  ceil(66279 x 1.05)
constexpr double kBaselineCoreNsPerBlock = 69593.0;

/// (b) N = 8, the shipped default: Granular shimmer + bloom + spectral @1024.
/// ceil(109138 x 1.05)
constexpr double kBaselineDefaultNsPerBlock = 114595.0;

/// (c) N = 16, everything on, spectral @4096, size = density = 1, 32 resonators.
/// ceil(190584 x 1.05)
constexpr double kBaselineWorstNsPerBlock = 200114.0;

/// (d) (b) frozen and settled. Freeze must not be MORE expensive than (b).
/// ceil(93755 x 1.05)
constexpr double kBaselineFrozenNsPerBlock = 98443.0;

/// (e) (b) with dimensionality swept - the geodesic re-materialised every chunk.
/// ceil(112822 x 1.05)
constexpr double kBaselineMorphNsPerBlock = 118464.0;

/// (f) the worst SINGLE 64-sample control chunk during a silence() clear, in the
/// (c) configuration at maxBlockSamples = 64. Not a block mean.
/// ceil(51200 x 1.05)
constexpr double kBaselineClearChunkNs = 53760.0;

// --- The twelve SC-008 compile-time clauses (spec.md:1808-1810) --------------
// Two per baseline. They are equivalent by construction; both are written
// because each fails with the message that names the actual rule, and because
// the second is the form spec.md states.

static_assert(kBaselineCoreNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-008 (a) core: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineCoreNsPerBlock <= kMaxAdmissibleNs,
              "SC-008 (a) core: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work the lever list, never raise the baseline");

static_assert(kBaselineDefaultNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-008 (b) shipped default: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineDefaultNsPerBlock <= kMaxAdmissibleNs,
              "SC-008 (b) shipped default: a baseline above kMaxAdmissibleNs means the phase is "
              "over budget - work the lever list, never raise the baseline");

static_assert(kBaselineWorstNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-008 (c) worst case: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineWorstNsPerBlock <= kMaxAdmissibleNs,
              "SC-008 (c) worst case: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work the lever list, never raise the baseline");

static_assert(kBaselineFrozenNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-008 (d) frozen: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineFrozenNsPerBlock <= kMaxAdmissibleNs,
              "SC-008 (d) frozen: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work the lever list, never raise the baseline");

static_assert(kBaselineMorphNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-008 (e) dimensionality sweep: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineMorphNsPerBlock <= kMaxAdmissibleNs,
              "SC-008 (e) dimensionality sweep: a baseline above kMaxAdmissibleNs means the phase "
              "is over budget - work the lever list, never raise the baseline");

static_assert(kBaselineClearChunkNs * kRegressionFactor <= kReferenceNs,
              "SC-008 (f) state-clear burst: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineClearChunkNs <= kMaxAdmissibleNs,
              "SC-008 (f) state-clear burst: a baseline above kMaxAdmissibleNs means the "
              "amortization is not bounding the burst - fix plan S5.3's slab sizing, never raise "
              "the "
              "baseline");

/// (d)'s own structural clause, stated as a constant relation so a future edit
/// that quietly budgets freeze ABOVE the unfrozen default does not compile.
static_assert(kBaselineFrozenNsPerBlock <= kBaselineDefaultNsPerBlock,
              "SC-008 (d): freeze must not be MORE expensive than configuration (b) - FR-034 "
              "skips geometry and decay/damping while frozen and FR-033 step 5 mutes all three "
              "sends, so a larger frozen budget would be budgeting for a defect");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// The shape is MANY SHORT trials (25 x 500 blocks), copied deliberately from
// continuous_body_perf_test.cpp:288-313, and the reason is the dev machine's
// CPU: a 13th Gen Intel Core i9 is a HYBRID part. The dominant noise source is
// not scheduling jitter smeared across a trial, it is the whole trial being
// migrated onto an E-core, which is a ~20 % step in ns/block that best-of-N
// cannot reject when N is small and each trial is long enough to be migrated.
// Shortening each trial to 500 blocks (~15 ms of wall clock) and taking 25 of
// them makes it very likely that at least one trial runs start-to-finish on a
// boosted P-core, which is the figure the baseline wants to describe. Pinning
// affinity was tried and REJECTED in harmonic_cloud_perf_test.cpp: it is not
// portable, and on a hybrid part it selects a core by index without knowing its
// type.
//
// 500 blocks ~= 5.3 s of audio. Every one of configurations (a)-(e) is a
// SUSTAINED one - continuous excitation, no note-off, no transient - so every
// trial measures identical steady-state work.

constexpr int kTrials = 25;
constexpr int kWarmupBlocks = 400;  ///< ~4.3 s: past the 300 ms Size smoother,
                                    ///< the 50 ms freeze latch, the spectral
                                    ///< warm-up and the whole reverb build-up.
constexpr int kBlocksPerTrial = 500;

/// Configuration (d) only: blocks rendered UNFROZEN before setFreeze(true).
///
/// This is not padding. FR-033 step 4 scales the input injection by
/// (1 - freezeRamp) (aether_reverb.h:4256-4257), so once the latch completes
/// NOTHING enters the network - a freeze entered at t = 0 would hold, and
/// measure, an empty loop. It would still pay the same arithmetic (there is no
/// silence early-out anywhere in renderSlice), so the ns/block figure would look
/// plausible while describing a state no player can reach, and the one risk the
/// frozen configuration exists to probe - denormals in an exactly-lossless loop
/// that FR-036 forbids the tickle in (risk R-6, plan.md:1798) - would be
/// measured on zeros instead of on a real tail.
constexpr int kPreFreezeBlocks = 400;

// Configuration (f)'s own shape. A clear is ~15 amortized slabs plus the 20 ms
// fade-out and 20 ms fade-in (aether_reverb.h:1846-1853), i.e. a few dozen
// chunks - far too short for the 500-block trial above, and a MAXIMUM rather
// than a mean, so the driver repeats the whole clear and takes the best (i.e.
// least-contaminated) per-clear maximum.
constexpr int kClearWarmupChunks = 3200;  ///< 3200 x 64 = ~4.3 s, matching kWarmupBlocks
constexpr int kClearRefillChunks = 800;   ///< ~1.07 s of re-excitation between clears
constexpr int kClearTrials = 25;
constexpr std::size_t kClearChunkCap = 4096;  ///< safety stop; a clear is ~40 chunks

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Best-of-N driver. `runBlock` performs exactly one 512-sample block of work.
/// Taken by const reference, not by forwarding reference: it is INVOKED, many
/// times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestNsPerBlock(int trials, int blocksPerTrial, const BlockFn& runBlock)
{
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < trials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        best = std::min(best, elapsedNs / static_cast<double>(blocksPerTrial));
    }
    return best;
}

// =============================================================================
// Excitation and buffers
// =============================================================================

/// One block of input plus its output scratch. Separate arrays: the perf lane
/// never relies on in-place support.
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
};

/// Deterministic decorrelated stereo noise at ~-12 dBFS, built once and replayed
/// every block.
///
/// Sustained broadband excitation, not an impulse: SC-008 measures the cost of a
/// space that is BEING DRIVEN. It also keeps the recirculating state well above
/// the denormal floor in every configuration except (d), where FR-036 forbids
/// the tickle and FTZ/DAZ (dsp_test_main.cpp:12-13) is the mitigation.
/// Xorshift32 rather than <random> so the sequence is identical on every
/// toolchain (std::uniform_real_distribution is not portable).
void fillExcitation(Buffers& buf) noexcept
{
    std::uint32_t state = 0x9E3779B9u;
    const auto next = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // [-0.25, 0.25): 24 mantissa bits scaled, no division by a magic float.
        return (static_cast<float>(state >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.5f;
    };
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        buf.inLeft[i] = next();
        buf.inRight[i] = next();
    }
}

// =============================================================================
// The six configurations
// =============================================================================

/// Everything that distinguishes one SC-008 configuration from another.
struct ConfigSpec {
    AetherReverb::PrepareConfig prepare{};
    bool stagesLive = false;        ///< sends at 1, spectral amount 0.5, bloom driven
    std::size_t bloomPartials = 0;  ///< how many resonators bloomNoteOn drives
    bool maxGeometry = false;       ///< size = 1, density = 1 (configuration (c))
    bool freeze = false;            ///< configuration (d)
};

/// FR-056's note-informed tuning source (Q1, RA-7). A harmonic series on 110 Hz:
/// deterministic, and at 32 partials the top is 3520 Hz, comfortably inside the
/// kBloomMaxFreqFraction * sampleRate = 21.6 kHz clamp so no partial is folded
/// onto another and the bank really does hold `count` distinct resonators.
void driveBloom(AetherReverb& r, std::size_t count) noexcept
{
    std::array<float, static_cast<std::size_t>(AetherReverb::kMaxBloomResonators)> partials{};
    const std::size_t n =
        std::min(count, static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));
    for (std::size_t i = 0; i < n; ++i) {
        partials[i] = 110.0f * static_cast<float>(i + 1u);
    }
    r.bloomNoteOn(0, partials.data(), n);
}

/// prepare() + the control history that defines the configuration. Everything
/// not set here is left at its FR-009 default on purpose: (a) is specified as
/// "defaults", and (b)/(d)/(e) are specified as the SHIPPED default, so
/// overriding size / decay / damping / mod depth would measure a configuration
/// nobody ships.
void buildEngine(AetherReverb& r, const ConfigSpec& s) noexcept
{
    r.prepare(kSr48, s.prepare);

    if (s.maxGeometry) {
        r.setSize(1.0f);     // FR-012: the longest lines the geometry can reach
        r.setDensity(1.0f);  // FR-040: every DiffusionNetwork stage enabled
    }
    if (s.stagesLive) {
        // The sends do not gate the COST of the shimmer taps - updateShimmerTaps
        // drives both PitchShiftProcessors on every control chunk whenever they
        // were allocated (aether_reverb.h:3219-3227) - but they do gate the
        // return path's shelf and injection work, and a configuration labelled
        // "everything on" that left them at the 0 default would be mislabelled.
        r.setShimmerOctaveSend(1.0f);
        r.setShimmerFifthSend(1.0f);
        r.setBloomSend(1.0f);
        r.setBloomDecay(1.0f);
        // The spectral stage runs whenever it was enabled at prepare
        // (aether_reverb.h:3931-3959); the amount only changes how much work
        // smearSpectrum does per bin. 0.5 is a mid-sweep operating point rather
        // than either extreme.
        r.setSpectralDiffusion(0.5f);
        driveBloom(r, s.bloomPartials);
    }
    // ConfigSpec::freeze is deliberately NOT applied here: measureSteady() has
    // to excite the network first (see kPreFreezeBlocks).
}

/// (a) N = 8, FR-009 defaults, shimmer / bloom / spectral diffusion all OFF.
[[nodiscard]] ConfigSpec specCore() noexcept
{
    ConfigSpec s{};
    s.prepare = AetherReverb::PrepareConfig{
        .numChannels = std::size_t{8},
        .maxBlockSamples = kBlockSize,
        .maxDelaySeconds = 0.50f,
        .shimmerEnabled = false,
        .shimmerMode = PitchMode::Granular,
        .bloomEnabled = false,
        .spectralDiffusionEnabled = false,
        .diffusionFftSize = std::size_t{1024},
        .seed = std::uint32_t{1},
    };
    return s;
}

/// (b) N = 8, the shipped default: Granular shimmer, bloom, spectral @1024.
///
/// Eight bloom partials, not 32: (b) is "the shipped default", and the default
/// caller is Phase 7 forwarding ONE voice's low-order partials (RA-7). The
/// 32-resonator ceiling is configuration (c)'s job.
[[nodiscard]] ConfigSpec specDefault() noexcept
{
    ConfigSpec s{};
    s.prepare = AetherReverb::PrepareConfig{
        .numChannels = std::size_t{8},
        .maxBlockSamples = kBlockSize,
        .maxDelaySeconds = 0.50f,
        .shimmerEnabled = true,
        .shimmerMode = PitchMode::Granular,
        .bloomEnabled = true,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{1024},
        .seed = std::uint32_t{1},
    };
    s.stagesLive = true;
    s.bloomPartials = 8;
    return s;
}

/// (c) N = 16, everything on, spectral @4096, size = density = 1,
/// maxDelaySeconds = 0.5 (P-2), kMaxBloomResonators = 32 driven.
///
/// @param maxBlockSamples 512 for the block measurement, 64 for configuration
///        (f)'s burst measurement - the ONE field that differs between them.
[[nodiscard]] ConfigSpec specWorst(std::size_t maxBlockSamples) noexcept
{
    ConfigSpec s{};
    s.prepare = AetherReverb::PrepareConfig{
        .numChannels = std::size_t{16},
        .maxBlockSamples = maxBlockSamples,
        .maxDelaySeconds = 0.50f,
        .shimmerEnabled = true,
        .shimmerMode = PitchMode::Granular,
        .bloomEnabled = true,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{4096},
        .seed = std::uint32_t{1},
    };
    s.stagesLive = true;
    s.bloomPartials = static_cast<std::size_t>(AetherReverb::kMaxBloomResonators);
    s.maxGeometry = true;
    return s;
}

/// (d) (b), frozen and settled.
[[nodiscard]] ConfigSpec specFrozen() noexcept
{
    ConfigSpec s = specDefault();
    s.freeze = true;
    return s;
}

// =============================================================================
// The dimensionality trajectory (configuration (e))
// =============================================================================

constexpr std::size_t kDimTableSize = 64;  ///< power of two: the index masks

/// A triangle 0 -> 1 -> 0 across 64 control chunks (~85 ms per traversal at
/// 48 kHz), stepping the SETTER by 1/32 = 0.03125 per chunk.
///
/// WHAT THE GATE ACTUALLY SEES, because the setter's step is not it:
/// updateMorph() tests the SMOOTHED position (aether_reverb.h:3128-3135) -
/// `t = clamp(dimSm_.getCurrentValue() + tide, 0, 1)` against
/// `lastMorphPosition_`, with kMorphEpsilon = 1e-6 (:1489). Behind a 200 ms
/// smoother a 1.33 ms chunk advances t by roughly
/// alpha * |target - t| with alpha ~ 6.7e-3, i.e. ~1e-4 .. 3e-3 for this
/// trajectory - still two to three orders of magnitude above the gate, but NOT
/// the 0.03125 the setter moves. A triangle is used rather than a sine because
/// its target is in motion at a constant rate everywhere except two turning
/// points per traversal; the two chunks where the smoothed position crosses its
/// target and its own step momentarily collapses are exactly why the (e)
/// precondition is a PROPORTION of chunks (>= 90 %) and not "every chunk".
/// That proportion is MEASURED, not assumed - see morphChunksRecomputed.
[[nodiscard]] std::array<float, kDimTableSize> buildDimTable() noexcept
{
    std::array<float, kDimTableSize> table{};
    constexpr std::size_t half = kDimTableSize / 2u;
    for (std::size_t k = 0; k < kDimTableSize; ++k) {
        const std::size_t up = (k < half) ? k : (kDimTableSize - k);
        table[k] = static_cast<float>(up) / static_cast<float>(half);
    }
    return table;
}

// =============================================================================
// Measurement
// =============================================================================

struct Measurement {
    double nsPerBlock = 0.0;
    double sink = 0.0;
    bool finite = false;
    bool shimmerActive = false;
    bool frozen = false;
    std::size_t bloomCount = 0;
    std::size_t latencySamples = 0;
    std::size_t recoveries = 0;
    /// (e) ONLY, and left at 0 by every other driver: control chunks probed for
    /// morph motion, and how many of them moved the applied morph position by
    /// more than kMorphEpsilon (i.e. re-materialised the matrix).
    std::size_t morphChunksProbed = 0;
    std::size_t morphChunksRecomputed = 0;
};

/// Configurations (a) - (d): one 512-sample call per block, static controls.
[[nodiscard]] Measurement measureSteady(const ConfigSpec& s)
{
    AetherReverb r;
    buildEngine(r, s);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;

    // Reading two samples per block is what stops the optimizer dead-coding the
    // render away; a real consumer reads the whole buffer, so this is not
    // artificial overhead.
    const auto renderBlock = [&]() noexcept {
        r.processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                             buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    if (s.freeze) {
        // Fill the network with a real tail FIRST, then latch. The 50 ms
        // kFreezeLatchMs window closes inside the first few blocks of the
        // warm-up below, so the measurement starts long after isFrozen() is
        // true - which the REQUIRE on Measurement::frozen then confirms.
        for (int i = 0; i < kPreFreezeBlocks; ++i) {
            renderBlock();
        }
        r.setFreeze(true);
    }

    for (int i = 0; i < kWarmupBlocks; ++i) {
        renderBlock();
    }

    Measurement out{};
    out.nsPerBlock = bestNsPerBlock(kTrials, kBlocksPerTrial, renderBlock);
    out.sink = sink;
    out.finite = isFiniteValue(static_cast<float>(sink));
    out.shimmerActive = r.isShimmerActive();
    out.frozen = r.isFrozen();
    out.bloomCount = r.getActiveBloomResonatorCount();
    out.latencySamples = r.getLatencySamples();
    out.recoveries = r.getNonFiniteRecoveryCount();
    return out;
}

/// Configuration (e): (b) with one setDimensionality per 64-sample control chunk.
///
/// The block is rendered as 8 x 64 rather than 1 x 512 because that IS the
/// Phase 7 cadence: the setter has to land between control chunks to be paid for
/// at the control rate. FR-005's absolute control grid makes the two
/// decompositions identical in DSP terms, so the only thing this adds over (b)
/// is the control-rate work - which is what it is here to measure.
[[nodiscard]] Measurement measureDimensionalitySweep(const ConfigSpec& s)
{
    const auto dims = buildDimTable();

    AetherReverb r;
    buildEngine(r, s);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;
    std::size_t stepIndex = 0;

    const auto sweptBlock = [&]() noexcept {
        for (std::size_t chunk = 0; chunk < kChunksPerBlock; ++chunk) {
            r.setDimensionality(dims[stepIndex]);
            stepIndex = (stepIndex + 1u) & (kDimTableSize - 1u);

            const std::size_t offset = chunk * kChunkSamples;
            r.processStereoBlock(buf.inLeft.data() + offset, buf.inRight.data() + offset,
                                 buf.outLeft.data() + offset, buf.outRight.data() + offset,
                                 kChunkSamples);
        }
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        sweptBlock();
    }

    Measurement out{};

    // (e)'s precondition data, gathered UNTIMED between the warm-up and the
    // trials: one full traversal of the table, one chunk at a time, counting
    // the chunks on which the APPLIED morph position moved by more than
    // kMorphEpsilon. updateMorph() re-materialises the matrix on exactly that
    // condition (aether_reverb.h:3133-3138) and writes morphPosition_
    // unconditionally just above it (:3129), so this count IS the number of
    // geodesic evaluations - without needing an accessor the class does not
    // expose. A configuration (e) whose matrix was in fact skipping the
    // recompute would measure (b) under (e)'s label; this is what stops that.
    {
        float previous = r.getCurrentMorphPosition();
        for (std::size_t k = 0; k < kDimTableSize; ++k) {
            r.setDimensionality(dims[stepIndex]);
            stepIndex = (stepIndex + 1u) & (kDimTableSize - 1u);

            const std::size_t offset = (k % kChunksPerBlock) * kChunkSamples;
            r.processStereoBlock(buf.inLeft.data() + offset, buf.inRight.data() + offset,
                                 buf.outLeft.data() + offset, buf.outRight.data() + offset,
                                 kChunkSamples);

            const float now = r.getCurrentMorphPosition();
            if (std::fabs(now - previous) > AetherReverb::kMorphEpsilon) {
                ++out.morphChunksRecomputed;
            }
            previous = now;
            ++out.morphChunksProbed;
        }
    }

    out.nsPerBlock = bestNsPerBlock(kTrials, kBlocksPerTrial, sweptBlock);
    out.sink = sink;
    out.finite = isFiniteValue(static_cast<float>(sink));
    out.shimmerActive = r.isShimmerActive();
    out.frozen = r.isFrozen();
    out.bloomCount = r.getActiveBloomResonatorCount();
    out.latencySamples = r.getLatencySamples();
    out.recoveries = r.getNonFiniteRecoveryCount();
    return out;
}

struct ClearBurstMeasurement {
    double worstChunkNs = 0.0;   ///< best (least contaminated) per-clear maximum
    double meanWorstNs = 0.0;    ///< mean of the per-clear maxima, reported only
    double sink = 0.0;
    bool finite = false;
    std::size_t chunksPerClear = 0;
    std::size_t clearsMeasured = 0;
    /// Fewest DRIVEN bloom resonators any measured clear STARTED from. The
    /// configuration label says 32; this is the number that proves it (see the
    /// re-drive note on measureClearBurst).
    std::size_t minBloomAtClear = 0;
    bool everCappedOut = false;  ///< a clear that never finished - a defect
};

/// Configuration (f): the state-clear burst.
///
/// maxBlockSamples = 64 and every call renders exactly kChunkSamples, so a call
/// is exactly one control chunk (the engine starts at sampleCounter_ == 0 and
/// only ever advances by 64, so no call ever straddles a boundary and
/// silence() is always issued AT a boundary - the configuration SC-008 (f)
/// specifies).
///
/// The metric is a MAXIMUM, so best-of-N is applied to it the only way that
/// makes sense: repeat the whole clear kClearTrials times and take the SMALLEST
/// per-clear maximum. The smallest maximum is the least OS-noise-contaminated
/// estimate of the true worst chunk, exactly as the minimum is for a mean. The
/// mean of the maxima is reported alongside so a wide spread is visible.
///
/// Two clock reads per 64-sample chunk is ~40-60 ns of steady_clock overhead
/// against a chunk that costs orders of magnitude more; it is inside the
/// measurement and is NOT subtracted, which biases the figure UPWARD - the safe
/// direction for a gate.
///
/// THE BLOOM BANK IS RE-DRIVEN AFTER EVERY CLEAR, and it has to be. silence()'s
/// phase 2 calls clearBloomBankState() (aether_reverb.h:3636-3655), which frees
/// every slot AND every voice, so without a re-drive only the FIRST of the
/// kClearTrials clears would run in configuration (c) - the remaining 24 would
/// measure a bank with zero driven resonators while still being reported under
/// the "(c), 32 resonators" label. The re-drive is issued AFTER the clear loop
/// rather than before the next silence() because that is the one point where
/// the bank is known to be entirely FREE: bloomNoteOn on a live voiceId
/// RELEASES its old slots first (aether_reverb.h:2390-2395) and a released slot
/// stays occupied until its envelope falls under kBloomReclaimThresholdLinear,
/// so a re-drive issued on a live bank would find acquireBloomSlot returning -1
/// for every partial and silently leave the bank empty. ClearBurstMeasurement::
/// minBloomAtClear is what makes that failure mode a REQUIRE rather than a hope.
[[nodiscard]] ClearBurstMeasurement measureClearBurst(const ConfigSpec& s)
{
    AetherReverb r;
    buildEngine(r, s);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;
    std::size_t cursor = 0;  // rotating 64-sample window into the 512-sample buffer

    const auto renderChunk = [&]() noexcept {
        const std::size_t offset = cursor;
        cursor = (cursor + kChunkSamples) % kBlockSize;
        r.processStereoBlock(buf.inLeft.data() + offset, buf.inRight.data() + offset,
                             buf.outLeft.data() + offset, buf.outRight.data() + offset,
                             kChunkSamples);
        sink += static_cast<double>(buf.outLeft[offset]);
    };

    for (int i = 0; i < kClearWarmupChunks; ++i) {
        renderChunk();
    }

    ClearBurstMeasurement out{};
    out.worstChunkNs = std::numeric_limits<double>::max();
    out.minBloomAtClear = std::numeric_limits<std::size_t>::max();
    double sumOfMaxima = 0.0;

    for (int trial = 0; trial < kClearTrials; ++trial) {
        // Refill the network between clears: a clear that walks an already-zero
        // delayBuffer_ costs the same memset, but the fade-in and the loop are
        // then running on silence and the figure would not describe the state a
        // player actually silences.
        for (int i = 0; i < kClearRefillChunks; ++i) {
            renderChunk();
        }

        out.minBloomAtClear = std::min(out.minBloomAtClear, r.getActiveBloomResonatorCount());
        r.silence();

        double worst = 0.0;
        std::size_t chunks = 0;
        while (r.isRecovering() && (chunks < kClearChunkCap)) {
            const auto start = std::chrono::steady_clock::now();
            renderChunk();
            const auto end = std::chrono::steady_clock::now();
            worst = std::max(worst,
                             std::chrono::duration<double, std::nano>(end - start).count());
            ++chunks;
        }
        if (chunks >= kClearChunkCap) {
            out.everCappedOut = true;
        }

        out.chunksPerClear = chunks;
        out.worstChunkNs = std::min(out.worstChunkNs, worst);
        sumOfMaxima += worst;
        ++out.clearsMeasured;

        // The bank is entirely free at this point (the clear wiped it), so this
        // re-acquires all s.bloomPartials slots cleanly. See the note above.
        if (s.bloomPartials > 0u) {
            driveBloom(r, s.bloomPartials);
        }
    }

    out.meanWorstNs = (out.clearsMeasured > 0u)
                          ? (sumOfMaxima / static_cast<double>(out.clearsMeasured))
                          : 0.0;
    out.sink = sink;
    out.finite = isFiniteValue(static_cast<float>(sink)) && (r.getNonFiniteRecoveryCount() == 0u);
    return out;
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string reportBlock(const char* configName, double measuredNs,
                                      double baselineNs)
{
    std::ostringstream os;
    os << "SC-008 " << configName << " - AetherReverb, ns per 512-sample block @ 48 kHz\n"
       << "  block budget    : " << kBlockBudgetNs << " ns\n"
       << "  reference (5 %) : " << kReferenceNs << " ns/block  (roadmap line 282, GLOBAL - "
                                                    "does not multiply by polyphony)\n"
       << "  measured        : " << measuredNs << " ns/block  ("
       << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  checked-in base : " << baselineNs << " ns/block  (gate: x" << kRegressionFactor
       << " = " << (baselineNs * kRegressionFactor) << " ns/block)\n"
       << "  headroom vs ref : " << ((measuredNs / kReferenceNs) * 100.0) << " % of the reference";
    return os.str();
}

}  // namespace

// ------------------------------------------------------------------------------
// T001 smoke case: prepare + one block through the render entry point, so the
// perf TU's harness shape is compiled and linked before any baseline exists.
// ------------------------------------------------------------------------------
TEST_CASE("AetherReverb_PerfSmoke", "[.perf][effects][aether]") {
    constexpr std::size_t kBlock = 512;

    AetherReverb r;
    r.prepare(48000.0, AetherReverb::PrepareConfig{});
    REQUIRE(r.isPrepared());

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    r.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

    REQUIRE(true);
}

// =============================================================================
// SC-008: CPU <= 5 % of one core, global
// =============================================================================
// Six configurations, gated against six checked-in constants. See BASELINE
// PROVENANCE above for how the constants are pinned and what to do when one is
// exceeded (work the lever list; never raise the baseline).
//
// EVERY MEASUREMENT AND EVERY REPORT COMES BEFORE ANY GATE. A REQUIRE aborts the
// case, so gating each configuration where it is measured would let the first
// over-budget configuration hide the other five - and the whole point of RA-3 is
// that Phase 7 gets all six numbers, including the ones that failed.

TEST_CASE("AetherReverb_CpuBudget", "[.perf][effects][aether]")
{
    // -------------------------------------------------------------------------
    // (a) N = 8, defaults, shimmer / bloom / spectral OFF
    // -------------------------------------------------------------------------
    const Measurement core = measureSteady(specCore());
    // Guards against the whole loop being optimized out (a zero-cost "pass") and
    // against a figure measured on an engine that had already blown up.
    REQUIRE(core.finite);
    REQUIRE(core.recoveries == 0u);
    // Preconditions, not perf assertions: a figure for a configuration that is
    // not the one named would be the wrong number wearing the right label.
    REQUIRE_FALSE(core.shimmerActive);           // shimmerEnabled = false (FR-003)
    REQUIRE(core.bloomCount == 0u);              // bloomEnabled = false
    REQUIRE(core.latencySamples == 0u);          // spectral stage off => exactly 0 (FR-084)
    WARN(reportBlock("(a) core - N=8, defaults, shimmer/bloom/spectral OFF", core.nsPerBlock,
                     kBaselineCoreNsPerBlock));

    // -------------------------------------------------------------------------
    // (b) N = 8, the shipped default
    // -------------------------------------------------------------------------
    const Measurement shipped = measureSteady(specDefault());
    REQUIRE(shipped.finite);
    REQUIRE(shipped.recoveries == 0u);
    REQUIRE(shipped.shimmerActive);              // 48 kHz >= kShimmerMinSampleRate (RA-6)
    REQUIRE(shipped.bloomCount == 8u);           // driveBloom(8) really took 8 slots
    REQUIRE(shipped.latencySamples == 1024u);    // diffusionFftSize @1024 (RA-2)
    WARN(reportBlock("(b) shipped default - N=8, Granular shimmer + bloom + spectral @1024",
                     shipped.nsPerBlock, kBaselineDefaultNsPerBlock));

    // -------------------------------------------------------------------------
    // (c) N = 16, everything on, spectral @4096, size = density = 1, 32 resonators
    // -------------------------------------------------------------------------
    // The ONLY configuration in which the shipped N = 16 order is gated (Q3).
    // Its measured figure is what a later decision to promote N = 16 to the
    // default is made from, in compliance.md - never from an estimate.
    const Measurement worst = measureSteady(specWorst(kBlockSize));
    REQUIRE(worst.finite);
    REQUIRE(worst.recoveries == 0u);
    REQUIRE(worst.shimmerActive);
    REQUIRE(worst.bloomCount == static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));
    REQUIRE(worst.latencySamples == 4096u);
    WARN(reportBlock("(c) worst case - N=16, everything on, spectral @4096, size=density=1, "
                     "32 bloom resonators",
                     worst.nsPerBlock, kBaselineWorstNsPerBlock));

    // -------------------------------------------------------------------------
    // (d) (b) frozen and settled
    // -------------------------------------------------------------------------
    const Measurement frozen = measureSteady(specFrozen());
    REQUIRE(frozen.finite);
    REQUIRE(frozen.recoveries == 0u);
    // FR-037: isFrozen() is true only once the 50 ms latch has COMPLETED. If it
    // is false the measurement caught a mid-latch engine, which is a different
    // (and partly lossy) configuration.
    REQUIRE(frozen.frozen);
    REQUIRE(frozen.shimmerActive);
    REQUIRE(frozen.latencySamples == 1024u);
    WARN(reportBlock("(d) frozen - (b) with setFreeze(true) settled", frozen.nsPerBlock,
                     kBaselineFrozenNsPerBlock));

    // -------------------------------------------------------------------------
    // (e) (b) with dimensionality swept every control chunk
    // -------------------------------------------------------------------------
    const Measurement morph = measureDimensionalitySweep(specDefault());
    REQUIRE(morph.finite);
    REQUIRE(morph.recoveries == 0u);
    REQUIRE(morph.shimmerActive);
    REQUIRE(morph.bloomCount == 8u);
    REQUIRE(morph.latencySamples == 1024u);
    // Precondition: the trajectory really does defeat the kMorphEpsilon gate.
    // >= 90 % rather than 100 % because a smoothed triangle crosses its own
    // target twice per traversal, and on those chunks the applied position's
    // step momentarily collapses (see buildDimTable's note). A configuration
    // that had settled would score ~0 here, which is the regression this
    // catches - it is not a tolerance on the budget.
    INFO("SC-008 (e): " << morph.morphChunksRecomputed << " of " << morph.morphChunksProbed
                        << " control chunks re-materialised the matrix");
    REQUIRE(morph.morphChunksProbed == kDimTableSize);
    REQUIRE((morph.morphChunksRecomputed * 10u) >= (morph.morphChunksProbed * 9u));
    WARN(reportBlock("(e) dimensionality sweep - (b) with setDimensionality per 64-sample chunk, "
                     "geodesic re-materialised every chunk",
                     morph.nsPerBlock, kBaselineMorphNsPerBlock));

    // -------------------------------------------------------------------------
    // (f) the state-clear burst
    // -------------------------------------------------------------------------
    const ClearBurstMeasurement burst = measureClearBurst(specWorst(kChunkSamples));
    REQUIRE(burst.finite);
    // Preconditions: a "clear burst" figure taken from a clear that never
    // happened, or that never finished, is not a measurement of anything.
    REQUIRE(burst.clearsMeasured == static_cast<std::size_t>(kClearTrials));
    REQUIRE(burst.chunksPerClear > 0u);
    REQUIRE_FALSE(burst.everCappedOut);
    // ...and every one of them started from the FULL configuration (c) bank.
    // silence() wipes the bloom bank at gate 0, so this is the assertion that a
    // silently-empty bank cannot pass itself off as the 32-resonator worst case.
    REQUIRE(burst.minBloomAtClear == static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));
    {
        std::ostringstream os;
        os << "SC-008 (f) state-clear burst - AetherReverb, WORST SINGLE 64-sample control chunk "
              "during silence(), configuration (c) at maxBlockSamples = 64\n"
           << "  chunk deadline  : " << kChunkDeadlineNs
           << " ns  (64 samples @ 48 kHz - the tightest deadline PrepareConfig admits)\n"
           << "  worst chunk     : " << burst.worstChunkNs << " ns  ("
           << ((burst.worstChunkNs / kChunkDeadlineNs) * 100.0) << " % of that deadline)\n"
           << "  mean of maxima  : " << burst.meanWorstNs << " ns over " << burst.clearsMeasured
           << " clears  (a wide spread here means the slab sizing is not the bound)\n"
           << "  chunks / clear  : " << burst.chunksPerClear
           << "  (amortization: aether_reverb.h:1846-1856)\n"
           << "  bloom at clear  : " << burst.minBloomAtClear
           << " driven resonators, fewest of the " << burst.clearsMeasured
           << " clears  (configuration (c) is 32; the bank is re-driven after "
              "every clear because silence() wipes it)\n"
           << "  reference (5 %) : " << kReferenceNs << " ns  (the same absolute constant the "
                                                        "other five are gated against)\n"
           << "  checked-in base : " << kBaselineClearChunkNs << " ns  (gate: x"
           << kRegressionFactor << " = " << (kBaselineClearChunkNs * kRegressionFactor) << " ns)";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // Cross-configuration reporting (no gate - the gates are below)
    // -------------------------------------------------------------------------
    {
        std::ostringstream os;
        os << "SC-008 summary, ns/block @ 48 kHz (transcribe ALL SIX into compliance.md verbatim "
              "- RA-3: Phase 7 tallies measurements, not ceilings):\n"
           << "  (a) core            : " << core.nsPerBlock << "\n"
           << "  (b) shipped default : " << shipped.nsPerBlock << "\n"
           << "  (c) worst case      : " << worst.nsPerBlock << "\n"
           << "  (d) frozen          : " << frozen.nsPerBlock << "\n"
           << "  (e) dim sweep       : " << morph.nsPerBlock << "\n"
           << "  (f) worst clear chunk (64 samples, NOT a block figure): " << burst.worstChunkNs
           << "\n"
           << "  stage cost (b) - (a): " << (shipped.nsPerBlock - core.nsPerBlock)
           << "  [shimmer + bloom + spectral @1024]\n"
           << "  N=16 cost (c) - (b) : " << (worst.nsPerBlock - shipped.nsPerBlock)
           << "  [N 8->16, spectral 1024->4096, 8->32 resonators, size/density 1]\n"
           << "  morph cost (e) - (b): " << (morph.nsPerBlock - shipped.nsPerBlock)
           << "  [one O(N^3) geodesic product per 64 samples]\n"
           << "  freeze delta (d)-(b): " << (frozen.nsPerBlock - shipped.nsPerBlock)
           << "  [must be <= 0 in the mean; see the gate below]";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // THE GATES
    // -------------------------------------------------------------------------
    INFO("SC-008 (a) core");
    REQUIRE(core.nsPerBlock <= kBaselineCoreNsPerBlock * kRegressionFactor);

    INFO("SC-008 (b) shipped default");
    REQUIRE(shipped.nsPerBlock <= kBaselineDefaultNsPerBlock * kRegressionFactor);

    INFO("SC-008 (c) worst case, N = 16");
    REQUIRE(worst.nsPerBlock <= kBaselineWorstNsPerBlock * kRegressionFactor);

    INFO("SC-008 (d) frozen");
    REQUIRE(frozen.nsPerBlock <= kBaselineFrozenNsPerBlock * kRegressionFactor);

    INFO("SC-008 (e) dimensionality sweep");
    REQUIRE(morph.nsPerBlock <= kBaselineMorphNsPerBlock * kRegressionFactor);

    INFO("SC-008 (f) state-clear burst, worst 64-sample control chunk");
    REQUIRE(burst.worstChunkNs <= kBaselineClearChunkNs * kRegressionFactor);

    // -------------------------------------------------------------------------
    // (d)'s RELATIVE clause: freeze must not be MORE expensive than (b)
    // -------------------------------------------------------------------------
    // spec.md:1806 - "freeze must not be *more* expensive". FR-034 skips
    // updateGeometry() and updateDecayAndDamping() entirely while frozen
    // (aether_reverb.h:3515-3519) and FR-033 step 5 mutes all three sends, so
    // the frozen figure is structurally a subset of (b)'s work. This is a
    // measured clause and not only the static_assert on the two baselines,
    // because the two baselines could both be over-generous and hide a freeze
    // that quietly recomputed the latched geometry every chunk.
    //
    // The tolerance is 10 % of (b), not 0: the two figures come from two
    // best-of-25 runs on a hybrid CPU, and run-to-run spread on this machine is
    // a few percent (continuous_body_perf_test.cpp:151-155). A freeze that
    // recomputes what FR-034 says it skips costs far more than 10 %.
    INFO("SC-008 (d) relative clause: frozen " << frozen.nsPerBlock << " ns/block vs (b) "
                                               << shipped.nsPerBlock << " ns/block");
    REQUIRE(frozen.nsPerBlock <= shipped.nsPerBlock * 1.10);
}
