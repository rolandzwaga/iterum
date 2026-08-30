// ==============================================================================
// Seraphis - Effects performance tests (Phase 10)   [.perf]
// ==============================================================================
// Reference: specs/seraphis-phase10-effects/spec.md   (SC-011, SC-012, SC-013)
//            specs/seraphis-phase10-effects/plan.md   (D-6, D-8, R-3, R-8)
//            specs/seraphis-phase10-effects/tasks.md  (T010 created this TU as a
//                                                      stub, T024 writes its body)
//
// CRITERIA OWNED BY THIS TU:
//   SC-011  "Effects stage is RT-safe"                    - allocation/lock/
//           exception freedom plus the two BURST budgets (FR-008's deferred
//           SpectralDelay::reset() and FR-027's seedRng()+reset() pair)
//   SC-012  "Effects cost nothing at defaults"            - 0.10 % of one core
//   SC-013  "Effects stage stays inside its 2.5 % budget" - 2.5 % of one core
//
// EVERY CASE HERE IS TAGGED "[.perf]" - hidden by default, run explicitly with
//   seraphis_tests.exe "[.perf]"
// and, exactly like every other [.perf] arm, NONE of them is part of the CI
// gate. A breach on an ordinary dev or CI machine is not a failure of these
// criteria; re-measuring under the protocol below is the first response to one.
//
// COMPILE FLAGS: this TU MUST NOT be listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt - those flags
//   move the figures its baselines are pinned to. Same rule as
//   integration/param_perf_test.cpp:18-24 and dsp/tests/CMakeLists.txt:735-740,
//   and the CMake list already records it. It therefore injects NO non-finite
//   value and names no std::isnan / std::isinf / std::numeric_limits infinity:
//   the macOS leg builds with -ffast-math, which folds them. Finiteness is
//   checked on the IEEE-754 exponent field instead (isFiniteValue below).
//
// NO CHECKED-IN FLOAT GOLDEN: the figures here are wall-clock nanoseconds
// measured in-process, never a render digest.
//
// ==============================================================================
// ONE PROTOCOL, AND ONLY ONE - "THE SC-013 PROTOCOL"
// ==============================================================================
//   spec.md's Success Criteria preamble (and SC-011, SC-012, SC-013, SC-014
//   individually) name a SINGLE admissible protocol for every CPU figure in
//   Phase 10:
//
//     a FRESH-BOOT, IDLE machine; SEVEN consecutive whole-suite runs of
//     `seraphis_tests.exe "[.perf]"`; each figure itself a BEST-OF-16; the
//     WORST OF THE SEVEN is the reported figure.
//
//   That is verbatim the protocol Phase 9's shipped SC-009 baseline was pinned
//   under on 2026-08-02 (param_perf_test.cpp:133-156).
//
//   THE "WORST OF SIX" RULE IS STRUCK. It was anchored to the BASELINE
//   PROVENANCE banner at param_perf_test.cpp:65-84 - the WITHDRAWN T028 HOT
//   dataset, which spec.md disowns in C-3 and RQ-1 and on which no argument in
//   Phase 10 rests. `param_perf_test.cpp:65-84` survives here ONLY as the
//   FORMATTING SHAPE of a BASELINE PROVENANCE banner, never as a measurement
//   protocol. Nothing in this file cites the six-run shape.
//
//   WHY IT IS STATED RATHER THAN ASSUMED: the SAME 91-row configuration measured
//   24.21 % on the earlier HOT machine (param_perf_test.cpp:83, :103-131) and
//   20.91 % cold (:148-156) - a 3.3-point swing, LARGER than SC-013's whole
//   2.5 % budget.
//
// ==============================================================================
// BASELINE PROVENANCE - SC-013's per-run table
//   ** FILLED 2026-08-03, SEVEN-RUN IDLE DATASET, ALL SEVEN RUNS EXIT=0 **
// ==============================================================================
//   SHAPE: param_perf_test.cpp:65-84 / :133-156 (the cold dataset), i.e. one
//   column per run, each cell itself a best-of-16, and a `worst` column.
//
//   WHAT WAS RUN: seven consecutive whole-suite runs of
//   `seraphis_tests.exe "[.perf]"` on 2026-08-03, windows-x64-release / MSVC,
//   ~89 s per run, each figure a best-of-16, worst of the seven reported. All
//   seven exited 0 with "All tests passed (502 assertions in 7 test cases)" -
//   which is the point: every ceiling below was gated on all seven runs, not
//   just on the one that produced the transcribed number.
//
//   MACHINE STATE - STATED EXACTLY, NOT ROUNDED UP TO "FRESH-BOOT". The host was
//   IDLE (no concurrent build, no other test process - the earlier attempt at
//   this dataset was taken with two other seraphis_tests.exe instances resident
//   and is superseded, see below). It was NOT rebooted immediately before the
//   run, so the words "fresh-boot" are not claimed here.
//
//   WHY THAT IS NEVERTHELESS THE PROTOCOL'S MACHINE, AND HOW IT IS SHOWN. The
//   protocol's purpose is a host whose timings match the 2026-08-02 fresh-boot
//   reference, and the reference itself supplies the instrument to check that:
//   SC-008 arm 1, a settled steady-state push, is the most stable estimator in
//   the whole [.perf] set and was pinned at 73.85-82.40 ns across the seven cold
//   runs, worst 82.40 (param_perf_test.cpp:157). Across THESE seven runs it read
//
//     67.95  66.40  71.65  73.50  71.60  61.25  78.80   ns   (worst 78.80)
//
//   i.e. the whole spread sits AT OR BELOW the cold reference's own spread, and
//   the worst of the seven (78.80) is BELOW the cold worst (82.40). This host is
//   not inflated relative to the protocol's machine; it is marginally faster. No
//   scaling factor is applied to anything below, and none is needed.
//
//   THE SUPERSEDED HOT DATASET. An earlier attempt at this table was taken on a
//   loaded workstation whose calibrator read 113.35-116.35 ns - a 1.38-1.41x
//   inflation - and it recorded a BREACH on SC-011's "every OTHER block" row
//   (two of seven runs at 307 100 / 314 000 ns against the 266 667 ns ceiling,
//   five at 99 300-120 700, nothing in between). That dataset is REPLACED, not
//   merged. Its breach is resolved rather than dismissed: on the idle host the
//   row reads 64 100-71 800 across all seven runs - a 1.09x spread with no
//   second mode at all - so the bimodality was host preemption surviving the
//   per-block-position best-of-16, exactly as its own note flagged as the thing
//   to re-test cold. The ceiling was never touched; the machine was.
//
//   WHAT GATES ON EVERY RUN, TABLE OR NO TABLE - these are SPEC constants, not
//   measurements, so they bind from the first run:
//     - SC-011's 5.0 %-of-one-core burst ceiling (533 333 ns) on the worst block
//       containing an FR-008 reset AND on the worst block containing an FR-027
//       seedRng()+reset() pair;
//     - SC-011's 2.5 % ceiling (266 667 ns) on every OTHER block;
//     - SC-011's allocation/lock/exception clause, which is
//       PROTOCOL-INDEPENDENT and is a hard failure on ANY machine;
//     - SC-012's 0.10 % ceiling (10 667 ns/block);
//     - SC-013's 2.5 % ceiling (266 667 ns/block);
//     - the divisor identity (FR-041 clause 1): the seam's per-CALL counter must
//       equal the number of process() calls the harness itself made;
//     - the strictly-non-zero elapsed time of every arm.
//   NO ARM IS COMPILED OUT and no case is skipped.
//
//   IDLE DATASET, 2026-08-03, seven consecutive runs, each cell a best-of-16:
//
//   ns/block                   run1    run2    run3    run4    run5    run6    run7    worst   ceiling
//   SC-008 arm 1 (calibrator)  67.95   66.40   71.65   73.50   71.60   61.25   78.80    78.80     5333  ok
//   SC-012 defaults               94      69      79      73      76      82     112      112    10667  ok
//   SC-013 stage at maxima     48994   49790   48267   46624   47635   48723   47883    49790   266667  ok
//   SC-011 worst FR-008 blk   113900  104300  102600  106000  111800  118100  108600   118100   533333  ok
//   SC-011 median FR-008 blk   99100   87400   84000   88900   88000   82600   86700    99100       --
//   SC-011 worst seed  blk      9600    9500    8900    9100    9200    9200    9000     9600   533333  ok
//   SC-011 median seed blk      8200    7800    8100    8100    8400    8200    8100     8400       --
//   SC-011 worst OTHER blk     71800   66400   64100   68300   67400   65600   70400    71800   266667  ok
//
//   And the composed figure SC-013's budget exists to pay for, from the same
//   seven runs (`Seraphis_FullPoly_CpuBudget_WithFullSurface`, the 107-row
//   table, param_perf_test.cpp:2534) - transcribed here because it is the same
//   dataset, with param_perf_test.cpp:2405-2470 as its code-of-record:
//
//   SC-014 107-row poly 8    2307790 2205730 2254400 2366810 2316940 2302560 2334470  2366810  2666667  ok
//   SC-014 as % of one core    21.64   20.68   21.14   22.19   21.72   21.59   21.89    22.19    25.00  ok
//
//   EVERY GATED ROW CLEARS, and the four that matter clear with room that no
//   plausible host difference closes: SC-012 uses 1.1 % of its budget, SC-013
//   18.7 %, the FR-008 burst 22.1 %, the seed burst 1.8 %, SC-011's OTHER row
//   26.9 %, and SC-014 sits 2.81 POINTS under the 25 % ceiling.
//
//   THE ONE ROW WITH REAL, NOT NOMINAL, MARGIN IS SC-014, and its margin is the
//   arithmetic SC-013's budget was sized by: Phase 9's pinned cold worst was
//   20.91 % with 91 rows; this is 22.19 % with 107, i.e. Phase 10 composes in at
//   +1.28 points against the 2.5 points SC-013 reserves. If a later change
//   pushes it over, the levers are the stage's own cost and the shipped defaults
//   - NEVER kFullPolyCeilingNs and never kBaselineFullPolyNs (roadmap 313-326).
//
//   Every case below PRINTS its row in exactly this shape (WARN), so the run
//   that refills the table transcribes numbers rather than re-deriving them.
//
// ==============================================================================
// SC-013's BUDGET DERIVATION - where 2.5 % comes from, and what the levers are
// ==============================================================================
//   pinned cold worst of PHASE 9's SC-009 arm  = 2 230 830 ns = 20.91 % of one
//                                                core (param_perf_test.cpp:443-456,
//                                                table at :148-156)
//   absolute ceiling  kFullPolyCeilingNs       = 2 666 666.7 ns = 25 %
//                                                (param_perf_test.cpp:376)
//   20.91 + 2.5 = 23.41 %, leaving 1.59 POINTS OF MARGIN inside the ceiling -
//   which is what makes SC-014 arithmetically reachable at all.
//
//   IF THE STAGE MEASURES ABOVE 2.5 %, THE LEVERS ARE THE STAGE'S OWN COST AND
//   THE SHIPPED DEFAULTS. THE 25 % CEILING IS NEVER THE LEVER (roadmap lines
//   313-326), and neither is kBaselineFullPolyNs, which param_perf_test.cpp:454-455
//   records is already the maximum both of its static_asserts admit.
//
//   NOTE the SC-009 cross-reference: "SC-009" here always means PHASE 9's SC-009
//   arm in param_perf_test.cpp (the full-poly CPU criterion). Phase 10's OWN
//   SC-009 is the state-v3 round-trip and lives in unit/state_v3_test.cpp.
//
// ==============================================================================
// TWO IMPLEMENTATION NOTES THE PLAN PRE-DECLARED
// ==============================================================================
//   plan R-8 - WHAT THE SEAM ITSELF COSTS. FR-041 clause 1's timer is two
//   std::chrono::steady_clock::now() reads and one add PER SLICE, plus the tap's
//   one 2n-float copy pair (processor.cpp:1889-1922). Under plan D-6 a
//   2048-sample block can carry up to 32 sub-slices, i.e. up to 64 clock reads
//   and 32 copies per block. SC-012's 10 667 ns threshold is ~3 orders of
//   magnitude above a clock read even then. If a future measurement shows
//   otherwise, the DOCUMENTED remedy is a single now() per process() call
//   bracketing the whole slice loop - not a relaxed threshold.
//
//   plan R-3 - DENORMALS. Long silent drains are a classic denormal generator:
//   the send holds 4 x 513 per-bin DelayLines (2052 of them) decaying toward
//   zero, and SC-011's script drives 17 full bypass excursions through exactly
//   that state. Two shipped mechanisms cover it, and this file relies on both
//   rather than re-arming anything: ScopedDenormalMode is armed at the top of
//   process() (processor.cpp:960) and is PER-THREAD, so it covers the render
//   this TU drives; and kFxSendDrainFloor = 1e-6 (processor.h:160) ends the
//   drain long before any value approaches 1e-38.
//
// STACK RULE (inherited from param_perf_test.cpp:294-297): sizeof(SeraphisEngine)
//   is ~772 KB against MSVC's 1 MiB default main-thread stack. Every fixture here
//   is std::make_unique'd; Processor holds its engine through a unique_ptr, so no
//   engine is ever a plain local.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "parameters/effects_params.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/seraphis_engine.h>

#include <allocation_detector.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Fixture = SeraphisTest::ProcessorFixture;

using Seraphis::kFxAzimuthDepthId;
using Seraphis::kFxDelayDiffusionId;
using Seraphis::kFxDelayFeedbackId;
using Seraphis::kFxDelayMixId;
using Seraphis::kFxDelaySpreadDirectionId;
using Seraphis::kFxDelaySpreadId;
using Seraphis::kFxDelaySyncId;
using Seraphis::kFxDelaySyncNoteId;
using Seraphis::kFxDelayTiltId;
using Seraphis::kFxDelayTimeId;
using Seraphis::kFxDelayWidthId;
using Seraphis::kFxSaturationId;
using Seraphis::kFxSpectralFreezeId;
using Seraphis::kFxWanderDepthId;
using Seraphis::kFxWanderRateId;
using Seraphis::kFxWidthId;
using Seraphis::kSeedId;

// =============================================================================
// Measurement basis - the SAME constants param_perf_test.cpp derives
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds:
/// 10 666 666.7 ns - the constant every Seraphis perf TU derives rather than
/// re-types (param_perf_test.cpp:368).
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-012: 0.10 % of one core = 10 666.67 ns/block.
constexpr double kDefaultsBudgetNs = kBlockBudgetNs * 0.001;
/// SC-013: 2.5 % of one core = 266 666.67 ns/block. SC-011 restates the SAME
/// number as its per-block ceiling for every NON-burst block - deliberately in
/// ns rather than by cross-reference, because a single per-block wall time and a
/// worst-of-seven best-of-16 aggregate are not the same quantity.
constexpr double kStageBudgetNs = kBlockBudgetNs * 0.025;
/// SC-011: 5.0 % of one core = 533 333.3 ns, the ceiling on the worst block
/// carrying a burst.
constexpr double kBurstBudgetNs = kBlockBudgetNs * 0.05;

// The spec quotes these three as 10 667 / 266 667 / 533 333 ns. Pinned so a
// change to the block basis cannot silently move a criterion's threshold.
static_assert(kBlockBudgetNs > 10666666.0 && kBlockBudgetNs < 10666667.0,
              "the 512/48 kHz block period is 10 666 666.7 ns");
static_assert(kDefaultsBudgetNs > 10666.0 && kDefaultsBudgetNs < 10667.0,
              "SC-012: 0.10 % of one core is 10 667 ns/block");
static_assert(kStageBudgetNs > 266666.0 && kStageBudgetNs < 266667.0,
              "SC-013: 2.5 % of one core is 266 667 ns/block");
static_assert(kBurstBudgetNs > 533333.0 && kBurstBudgetNs < 533334.0,
              "SC-011: 5.0 % of one core is 533 333 ns/block");

// --- Structural clauses: what these numbers describe -------------------------
static_assert(Seraphis::kFxSendChunkSamples == 512u,
              "C-2 clause 5: SpectralDelay::process is called with EXACTLY the hop size");
static_assert(kBlockSize % Krate::DSP::SeraphisEngine::kControlChunkSamples == 0u,
              "plan D-6 subdivides the measured block on the absolute 64-sample grid");
static_assert(Krate::DSP::SeraphisEngine::kMaxBlockSamples == 2048u,
              "FR-041 clause 6 pins the tap - and this file's blocks stay well under it");

// SC-012's subject, pinned at COMPILE TIME rather than argued in prose: "the C-6
// defaults" is only a zero-cost configuration because those defaults put BOTH
// bypass predicates in their skipping state. If a later phase changes a default,
// the build breaks here instead of the criterion quietly measuring a running
// stage and still passing (the stage would simply be more expensive than
// 10 667 ns and fail with no explanation of why).
static_assert(Seraphis::kFxDelayMixDefault == 0.0f && !Seraphis::kFxSpectralFreezeDefault,
              "SC-012: at the C-6 defaults the send is neither active nor draining (FR-007), "
              "which is the only reason the default configuration costs nothing");
static_assert(Seraphis::kFxWanderDepthDefault == 0.0f
                  && Seraphis::kFxAzimuthDepthDefault == 0.0f
                  && Seraphis::kFxWidthDefault == Krate::DSP::MidSideProcessor::kDefaultWidth,
              "SC-012: at the C-6 defaults FR-010's wander predicate is FALSE, so C-1 step 5 is "
              "skipped");

// =============================================================================
// Trial shape - best-of-16, the protocol's own number
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants. MANY SHORT TRIALS, for the reason
// dsp/tests/unit/systems/seraphis_perf_test.cpp:428-439 records: on a hybrid part
// the dominant noise source is a whole trial migrating onto an E-core, a ~20 %
// step that best-of-N cannot reject when each trial is long.

constexpr int kStageTrials = 16;
constexpr int kStageBlocksPerTrial = 100;  ///< ~1.07 s of audio per trial
/// ~6.4 s: past the atmosphere's capture ring, the body's crossfade, the cloud's
/// attack, the reverb build-up, every smoother in the chain AND - Phase 10's own
/// addition - the send's one-chunk accumulator pipeline and its 20 ms engage ramp.
constexpr int kStageWarmupBlocks = 600;

/// SC-012/SC-013's pinned operating point: 8 voices held, PHASE 9's SC-009 MIDI
/// script (param_perf_test.cpp:1617-1622) - distinct notes from MIDI 57 (220 Hz)
/// so the allocator hands out one slot each rather than retriggering one, and NO
/// note-off is ever issued.
constexpr std::size_t kPolyphony = 8;
constexpr Steinberg::int16 kFirstNote = 57;
/// param_perf_test.cpp:1621 plays velocity 100 (a uint8 straight into
/// SeraphisEngine::noteOn). The plugin boundary takes a FLOAT and maps it with
/// mapNoteOnVelocity = clamp(v * 127 + 0.5, 1, 127) (processor.cpp:60-62), so
/// 100/127 is the float that reproduces that exact uint8.
constexpr float kVelocity = 100.0f / 127.0f;

static_assert(kPolyphony <= Krate::DSP::SeraphisEngine::kMaxVoices,
              "the scenario must fit the pool");

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds it. Inspect the IEEE-754 exponent field instead
/// (param_perf_test.cpp:575-579).
[[nodiscard]] bool isFiniteValue(double v) noexcept {
    const auto f = static_cast<float>(v);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// THE OPTIMIZATION BARRIER. Every arm drains its accumulator into this before it
/// returns, so no timed render can be dead-coded away. `volatile` and never read
/// back by an assertion: the assertions are on the ELAPSED TIME, which every arm
/// requires to be strictly non-zero for exactly this reason.
// The barrier only works if it is a MUTABLE object at namespace scope - const or
// function-local storage lets the optimizer prove the stores dead and delete the
// timed render outright, which is why the check below is suppressed rather than obeyed.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile double gSink = 0.0;

[[nodiscard]] double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return ((n % 2u) == 1u) ? v[n / 2u] : (0.5 * (v[(n / 2u) - 1u] + v[n / 2u]));
}

/// Phase 9's SC-009 chord, delivered through the PLUGIN boundary (this TU drives
/// Processor, not a hand-built engine/reverb pair, because FR-041's seams are the
/// measurement point and they live on Processor).
void pressChord(Fixture& fx, std::size_t voices) {
    for (std::size_t v = 0; v < voices; ++v) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                     static_cast<Steinberg::int16>(kFirstNote + static_cast<int>(v)), kVelocity,
                     0);
    }
}

/// The seed dropdown's `L` denormalization is index = clamp(v * 15 + 0.5, 0, 15)
/// (global_params.h:111-113), so index i is driven by i/15.
[[nodiscard]] double seedNormalizedFor(std::size_t index) noexcept {
    return static_cast<double>(index) / 15.0;
}

// =============================================================================
// One report shape for all three cases, so the run that fills the BASELINE
// PROVENANCE table transcribes numbers instead of re-deriving them.
// =============================================================================
[[nodiscard]] std::string reportRow(const char* name, double measuredNs, double ceilingNs) {
    std::ostringstream os;
    os << name << ":\n"
       << "  block budget : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n"
       << "  ceiling      : " << ceilingNs << " ns  (" << ((ceilingNs / kBlockBudgetNs) * 100.0)
       << " % of one core)\n"
       << "  measured     : " << measuredNs << " ns  ("
       << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  headroom     : " << (ceilingNs - measuredNs) << " ns\n"
       << "  TRANSCRIBE the `measured` figure into this file's BASELINE "
          "PROVENANCE table. Seven runs, fresh-boot idle machine, worst of the "
          "seven is the reported figure. Never re-pin from one run.";
    return os.str();
}

// =============================================================================
// SC-011's script - 60 s, two phases, one purpose each
// =============================================================================
// spec SC-011 asks for ONE render that does four things at once: 60 s long,
// every bypass predicate toggled 100x, kSeedId automated across >= 16 index
// changes, and >= 16 events of EACH burst kind. Those requirements pull against
// each other on the clock, and the split below is what satisfies all of them
// inside 60 s rather than approximately satisfying them:
//
//   PHASE A (toggles). Three INTERLEAVED, MUTUALLY PRIME toggle periods - 9, 11
//   and 13 blocks - so the send predicate, the wander predicate and the freeze
//   never move in lockstep and a defect that only shows when two of them
//   disagree is reachable. kSeedId steps on a fourth period (100 blocks). No
//   FR-008 reset can fire here: fxResetDue_ needs the send BYPASSED for longer
//   than the whole kFxSendDrainMs = 2000 ms window (processor.cpp:2348-2349),
//   i.e. > 187.5 blocks, and the longest bypass in this phase is 13.
//
//   PHASE B (FR-008 resets). 17 cycles of engage -> bypass -> full drain ->
//   > 2 s bypassed -> engage. The SECOND engage of each pair is what satisfies
//   FR-008's precondition, so 17 cycles yield >= 16 resets (cycle 0's engage
//   follows the phase boundary, not a 2 s bypass, and is not counted on).
//
// WHY THE CYCLE FITS AT ALL: the drain is fed SILENCE (FR-009a) and ends EARLY
// on kFxSendDrainFloor = 1e-6 (processor.cpp:2392), so with delay time 0,
// spread 0 and feedback 0 the tail is gone within a few chunks instead of
// occupying the full 2 s window. That energy exit is exactly what C-3 says
// bounds a bypass excursion's cost "by energy, not by wall clock" - this case
// is also its first end-to-end consumer.
// =============================================================================

/// 5625 x 512 = 2 880 000 samples = EXACTLY 60 s at 48 kHz.
constexpr std::size_t kRtBlocks = 5625;
static_assert(kRtBlocks * kBlockSize == 2880000u, "SC-011 renders exactly 60 s at 48 kHz");

constexpr std::size_t kRtToggleBlocks = 2140;  ///< phase A
constexpr std::size_t kRtCycleBlocks = 205;    ///< phase B, one engage/bypass cycle
constexpr std::size_t kRtCycles = 17;
constexpr std::size_t kRtActiveBlocks = 6;  ///< blocks the send is engaged per cycle
static_assert(kRtToggleBlocks + (kRtCycles * kRtCycleBlocks) == kRtBlocks,
              "the two phases must tile the 60 s render exactly");

constexpr std::size_t kRtSendPeriod = 9;
constexpr std::size_t kRtWanderPeriod = 11;
constexpr std::size_t kRtFreezePeriod = 13;
constexpr std::size_t kRtSeedPeriod = 100;

/// Transitions of a period-p square wave over `blocks` blocks: one at every
/// multiple of p in [p, blocks).
[[nodiscard]] constexpr std::size_t transitionsIn(std::size_t blocks, std::size_t period) noexcept {
    return (blocks == 0u) ? 0u : ((blocks - 1u) / period);
}
static_assert(transitionsIn(kRtToggleBlocks, kRtSendPeriod) >= 100u,
              "SC-011: FR-007's send predicate input (kFxDelayMixId, an EXACT != 0.0f test, "
              "processor.cpp:2325-2331) must cross its boundary at least 100 times");
static_assert(transitionsIn(kRtToggleBlocks, kRtWanderPeriod) >= 100u,
              "SC-011: FR-010's wander predicate inputs (1440 / 1441 / 1443, "
              "processor.cpp:1119-1122) must cross their boundary at least 100 times");
static_assert(transitionsIn(kRtToggleBlocks, kRtFreezePeriod) >= 100u,
              "SC-011: FR-023a's freeze input (kFxSpectralFreezeId), which is the SECOND term of "
              "the send predicate, must cross its boundary at least 100 times");
static_assert(transitionsIn(kRtToggleBlocks, kRtSeedPeriod) >= 16u,
              "SC-011 / FR-027: kSeedId must step across at least 16 index changes");

/// The drain must complete AND the >2 s bypass window must elapse inside one
/// cycle. kFxSendDrainMs = 2000 ms = 96 000 samples = 187.5 blocks at 48 kHz, so
/// the bypassed span of a cycle (cycle length minus the engaged span) has to
/// exceed that with room for the few blocks the drain itself takes.
static_assert((kRtCycleBlocks - kRtActiveBlocks) * kBlockSize > 96000u + (8u * kBlockSize),
              "FR-008: a cycle must contain a full drain plus > kFxSendDrainMs of bypass");

/// SC-011 holds TWO voices, not eight, and that is a MEASURED-SUBJECT decision
/// rather than a shortcut. FR-041 clause 1's timer brackets C-1 steps 4-5 and the
/// tap copy (processor.cpp:1889-1922) - none of which reads the voice pool - so
/// the voice count changes the HARNESS's wall cost, never this criterion's
/// subject. Two voices keep the bus non-silent, so the send's decaying tail and
/// plan R-3's denormal hazard are genuinely exercised, while making the
/// protocol's SIXTEEN 60 s repetitions affordable. SC-012 and SC-013, whose
/// operating point IS pinned by the spec, hold the full eight.
constexpr std::size_t kRtPolyphony = 2;

[[nodiscard]] constexpr bool rtSendOn(std::size_t b) noexcept {
    return ((b / kRtSendPeriod) % 2u) == 1u;
}
[[nodiscard]] constexpr bool rtWanderOn(std::size_t b) noexcept {
    return ((b / kRtWanderPeriod) % 2u) == 1u;
}
[[nodiscard]] constexpr bool rtFreezeOn(std::size_t b) noexcept {
    return ((b / kRtFreezePeriod) % 2u) == 1u;
}
[[nodiscard]] constexpr std::size_t rtSeedIndex(std::size_t b) noexcept {
    return (b / kRtSeedPeriod) % 16u;
}

struct RtCounts {
    std::size_t sendToggles = 0;
    std::size_t wanderToggles = 0;
    std::size_t freezeToggles = 0;
    std::size_t seedWrites = 0;
};

/// The static half of the send configuration, written once per trial at block 0.
/// Feedback, delay time and spread are pinned to their MINIMA on purpose: they
/// are what decide how long the FR-009a drain takes, and the cycle arithmetic
/// above depends on that drain ending on its energy exit. Everything else is
/// driven to its most expensive end so the stage the "every other block" clause
/// measures is a LOADED stage, not an idling one.
void rtWriteStaticParams(Fixture& fx) {
    fx.setParam(kFxDelayTimeId, 0.0);            // 0 ms  -> the shortest tail
    fx.setParam(kFxDelaySpreadId, 0.0);          // 0 ms  -> no per-bin spread
    fx.setParam(kFxDelayFeedbackId, 0.0);        // no regeneration
    fx.setParam(kFxDelaySpreadDirectionId, 1.0); // CenterOut (last index)
    fx.setParam(kFxDelayTiltId, 1.0);
    fx.setParam(kFxDelayDiffusionId, 1.0);
    fx.setParam(kFxDelayWidthId, 1.0);
    fx.setParam(kFxDelaySyncId, 0.0);
    fx.setParam(kFxDelaySyncNoteId, 1.0);
    fx.setParam(kFxWanderRateId, 1.0);
    fx.setParam(kFxSaturationId, 1.0);
}

/// Queues this block's automation on the fixture. Returns TRUE when kSeedId was
/// written, which is how the burst classifier below tells an FR-027
/// seedRng()+reset() pair from an FR-008 deferred reset: both increment
/// spectralDelayResetCountForTest() (processor.cpp:1692 and :2031) and the
/// counter alone cannot distinguish them, but the HARNESS knows which blocks it
/// stepped the seed on.
[[nodiscard]] bool rtScriptBlock(Fixture& fx, std::size_t b, RtCounts& counts) {
    bool seedWritten = false;

    if (b == 0u) {
        rtWriteStaticParams(fx);
        pressChord(fx, kRtPolyphony);
    }

    if (b < kRtToggleBlocks) {
        if ((b % kRtSendPeriod) == 0u) {
            fx.setParam(kFxDelayMixId, rtSendOn(b) ? 1.0 : 0.0);
            if (b > 0u) {
                ++counts.sendToggles;
            }
        }
        if ((b % kRtWanderPeriod) == 0u) {
            const bool on = rtWanderOn(b);
            fx.setParam(kFxWanderDepthId, on ? 1.0 : 0.0);
            fx.setParam(kFxAzimuthDepthId, on ? 1.0 : 0.0);
            // 0.5 normalized is EXACTLY kDefaultWidth = 100 % (the min/max span is
            // 0..200, effects_params.h), i.e. the value FR-010's predicate tests
            // for. 1.0 is 200 %.
            fx.setParam(kFxWidthId, on ? 1.0 : 0.5);
            if (b > 0u) {
                ++counts.wanderToggles;
            }
        }
        if ((b % kRtFreezePeriod) == 0u) {
            fx.setParam(kFxSpectralFreezeId, rtFreezeOn(b) ? 1.0 : 0.0);
            if (b > 0u) {
                ++counts.freezeToggles;
            }
        }
        if ((b % kRtSeedPeriod) == 0u && b > 0u) {
            fx.setParam(kSeedId, seedNormalizedFor(rtSeedIndex(b)));
            ++counts.seedWrites;
            seedWritten = true;
        }
    } else {
        const std::size_t offset = (b - kRtToggleBlocks) % kRtCycleBlocks;
        if (offset == 0u) {
            // Engage, and force the phase-A toggles OFF so nothing but the send
            // decides the state machine for the rest of the render. Writing them
            // every cycle rather than once is free: every push is on-change only
            // (processor.cpp:1665, :1714, ...).
            fx.setParam(kFxDelayMixId, 1.0);
            fx.setParam(kFxSpectralFreezeId, 0.0);
            fx.setParam(kFxWanderDepthId, 0.0);
            fx.setParam(kFxAzimuthDepthId, 0.0);
            fx.setParam(kFxWidthId, 0.5);
        } else if (offset == kRtActiveBlocks) {
            fx.setParam(kFxDelayMixId, 0.0);  // -> Draining -> Bypassed
        }
    }

    return seedWritten;
}

// =============================================================================
// SC-011's "LOCKS AND EXCEPTIONS" CLAUSE - MEASURED, NOT ASSERTED
// =============================================================================
// SC-011 reads "zero allocations, LOCKS AND EXCEPTIONS". Allocations have a
// runtime counter (AllocationScope, tests/test_helpers/allocation_detector.h).
// The other two had none, and a prose comment reading "lock-freedom is
// structural" is precisely the failure this instrument closes: a comment does
// not fail when somebody adds a std::mutex. Two instruments, one per clause.
//
// EXCEPTIONS - RUNTIME, and NOT vacuous. `Processor::process()` carries the VST3
// SDK's signature and is NOT noexcept, so anything thrown from it, or from any
// non-noexcept frame beneath it, unwinds into this harness; `rtExceptions`
// counts those. The Phase 10 stage's OWN entry points are noexcept
// (processor.h:491 renderSlice, :503 runSendStage, :517 updateEffectsBypassState,
// :537 runWanderStage), where a throw calls std::terminate instead - a strictly
// harder failure than a count, and one no trial survives to report. So the
// counter covers the survivable part of the chain and the noexcept specifiers
// cover the rest; between them the clause is observed rather than assumed.
//
// LOCKS - STATIC, over a CLOSED SET. No portable runtime hook observes a mutex
// acquisition, so the honest instrument is the source. The audio-thread-reachable
// set this phase adds is exactly the six files below, and scanning them for lock
// primitives is a gate that fails the moment one appears. The scan strips
// comments and string literals first, so the gate cannot be tripped by prose
// (this very comment names `std::mutex`, and this TU is not in the set anyway).
// `filesScanned` is asserted to be 6 so a rename cannot silently empty the scan.
//
// The set is the phase's audio-thread surface, cited: the processor TU that owns
// C-1 steps 4 and 5, its header, the parameter pack those steps read, and the
// three DSP components they drive (SpectralDelay, its STFT, and the two
// BrownianDrift instances behind FR-026/FR-027).
// =============================================================================

/// Source with `//` comments, `/* */` comments and string-literal CONTENTS
/// removed, newlines preserved. Character literals are deliberately NOT tracked:
/// none of the scanned files contains one holding a comment opener, and treating
/// `'` as a state change would misparse a digit separator such as `1'000`.
[[nodiscard]] std::string strippedSource(const std::string& src) {
    enum class State : std::uint8_t { Code, LineComment, BlockComment, StringLiteral };
    State state = State::Code;
    std::string out;
    out.reserve(src.size());

    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1u < src.size()) ? src[i + 1u] : '\0';

        switch (state) {
        case State::Code:
            if ((c == '/') && (next == '/')) {
                state = State::LineComment;
                ++i;
            } else if ((c == '/') && (next == '*')) {
                state = State::BlockComment;
                ++i;
            } else if (c == '"') {
                state = State::StringLiteral;
            } else {
                out.push_back(c);
            }
            break;
        case State::LineComment:
            if (c == '\n') {
                state = State::Code;
                out.push_back(c);
            }
            break;
        case State::BlockComment:
            if ((c == '*') && (next == '/')) {
                state = State::Code;
                ++i;
            } else if (c == '\n') {
                out.push_back(c);
            }
            break;
        case State::StringLiteral:
            if (c == '\\') {
                ++i;  // skip the escaped character, whatever it is
            } else if (c == '"') {
                state = State::Code;
            }
            break;
        }
    }
    return out;
}

[[nodiscard]] std::size_t countOccurrences(const std::string& haystack, const char* needle) {
    std::size_t n = 0;
    const std::string pattern(needle);
    for (std::size_t at = haystack.find(pattern); at != std::string::npos;
         at = haystack.find(pattern, at + pattern.size())) {
        ++n;
    }
    return n;
}

struct RtSourceScan {
    std::size_t filesScanned = 0;
    std::size_t filesMissing = 0;
    std::size_t lockPrimitives = 0;
    std::size_t throwSites = 0;
    /// Non-comment bytes actually examined. Asserted non-trivial so a build that
    /// resolved the paths but read nothing - or a stripper that ate every file -
    /// cannot report "no locks found" about an empty corpus.
    std::size_t codeBytes = 0;
    /// Occurrences of `runSendStage`, the phase's own C-1 step 4 entry point. It
    /// is the WITNESS that the corpus is the intended one: a scan pointed at six
    /// readable but wrong files would clear both token counts and this would be 0.
    std::size_t witnesses = 0;
    std::string firstHit;  ///< "<file>: <token>" for the failure message
};

/// The Phase 10 audio-thread-reachable source set. SERAPHIS_SRC_DIR and
/// KRATE_DSP_INCLUDE_DIR are handed to this TU by
/// plugins/seraphis/tests/CMakeLists.txt.
[[nodiscard]] RtSourceScan scanRtPathSources() {
    const std::array<std::string, 6> files{
        std::string(SERAPHIS_SRC_DIR) + "/processor/processor.cpp",
        std::string(SERAPHIS_SRC_DIR) + "/processor/processor.h",
        std::string(SERAPHIS_SRC_DIR) + "/parameters/effects_params.h",
        std::string(KRATE_DSP_INCLUDE_DIR) + "/krate/dsp/effects/spectral_delay.h",
        std::string(KRATE_DSP_INCLUDE_DIR) + "/krate/dsp/primitives/stft.h",
        std::string(KRATE_DSP_INCLUDE_DIR) + "/krate/dsp/processors/brownian_drift.h",
    };

    // Blocking primitives. `.lock()` and `->lock()` catch a hand-rolled or
    // third-party lockable that none of the named spellings would.
    const std::array<const char*, 11> lockTokens{
        "std::mutex",   "recursive_mutex",  "shared_mutex", "lock_guard",
        "unique_lock",  "scoped_lock",      "shared_lock",  "condition_variable",
        "pthread_mutex", ".lock()",         "->lock()",
    };
    // Exception machinery. Zero today; a hit means somebody put unwinding on -
    // or one call away from - the audio thread, which is worth stopping for even
    // when the site itself turns out to be a non-RT method of the same TU.
    const std::array<const char*, 5> throwTokens{
        "throw ", "throw;", "throw(", "catch (", "catch(",
    };

    RtSourceScan scan{};
    for (const std::string& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            ++scan.filesMissing;
            if (scan.firstHit.empty()) {
                scan.firstHit = path + ": UNREADABLE";
            }
            continue;
        }
        const std::string raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        const std::string code = strippedSource(raw);
        ++scan.filesScanned;
        scan.codeBytes += code.size();
        scan.witnesses += countOccurrences(code, "runSendStage");

        for (const char* token : lockTokens) {
            const std::size_t hits = countOccurrences(code, token);
            scan.lockPrimitives += hits;
            if ((hits != 0u) && scan.firstHit.empty()) {
                scan.firstHit = path + ": " + token;
            }
        }
        for (const char* token : throwTokens) {
            const std::size_t hits = countOccurrences(code, token);
            scan.throwSites += hits;
            if ((hits != 0u) && scan.firstHit.empty()) {
                scan.firstHit = path + ": " + token;
            }
        }
    }
    return scan;
}

struct RtTrial {
    std::size_t fr008Count = 0;
    std::size_t seedCount = 0;
    std::size_t allocations = 0;
    std::size_t exceptions = 0;
    std::size_t blocksOk = 0;
    std::size_t predicateEvals = 0;
    std::size_t processCalls = 0;
    RtCounts counts{};
    double audioSink = 0.0;
};

/// WHERE THE "best-of-16" IS TAKEN, AND WHY IT IS PER BLOCK POSITION.
///
/// SC-011's two burst clauses and its per-block clause are all WORST-block
/// statistics, and a worst-block statistic taken over one 60 s trial is a MAXIMUM
/// over 5625 observations - i.e. it reports the tail of whatever the operating
/// system did to this thread, not the tail of the stage's own cost. Taking the
/// best of sixteen such per-trial maxima does not remove that: each trial's
/// maximum is drawn from its own 5625-sample tail, so the minimum of sixteen
/// maxima is still a maximum.
///
/// MEASURED, on this machine, with the per-trial form (two consecutive trials of
/// the same script, same build): the worst OTHER block was 2 281 800 ns at block
/// 173 in one trial and 2 377 400 ns at block 266 in the next, with 80 and then
/// 186 blocks over the ceiling - random positions, no script feature in common,
/// and a count that doubles between trials. The stage's own per-block cost at the
/// far more expensive SC-013 operating point (all sixteen rows at maxima, EIGHT
/// voices, automation live) is 100 477 ns. A 20-25x excursion that never lands
/// twice in the same place is preemption, not work.
///
/// So each BLOCK POSITION is its own estimate and gets its own best-of-16: for
/// block b, the minimum across the sixteen trials of that block's stage time. The
/// reported worst is then the maximum over block positions of those minima. This
/// keeps every one of the 5625 positions gated (a systematic regression in any
/// block's stage cost appears in all sixteen trials and therefore survives the
/// minimum untouched) while an independent preemption event, which by definition
/// does not recur at the same position, is filtered out. The thresholds, the
/// classification and the number of gated observations are all unchanged - only
/// the axis the best-of-16 is taken along.
///
/// It also makes the sample bookkeeping structural: there is exactly one slot per
/// block position per class, so no burst sample can be dropped for want of
/// capacity.
constexpr double kNoSample = std::numeric_limits<double>::max();

struct PerBlockBest {
    std::vector<double> other;
    std::vector<double> fr008;
    std::vector<double> seed;

    PerBlockBest()
        : other(kRtBlocks, kNoSample), fr008(kRtBlocks, kNoSample), seed(kRtBlocks, kNoSample) {}
};

/// Worst (largest) of the recorded per-block minima; 0.0 when nothing was recorded.
[[nodiscard]] double worstOf(const std::vector<double>& v) {
    double worst = 0.0;
    for (const double x : v) {
        if (x != kNoSample) {
            worst = std::max(worst, x);
        }
    }
    return worst;
}

[[nodiscard]] std::size_t samplesIn(const std::vector<double>& v) {
    std::size_t n = 0;
    for (const double x : v) {
        if (x != kNoSample) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] double medianOfRecorded(const std::vector<double>& v) {
    std::vector<double> present;
    present.reserve(v.size());
    for (const double x : v) {
        if (x != kNoSample) {
            present.push_back(x);
        }
    }
    return medianOf(std::move(present));
}

/// ONE 60 s trial. The AllocationScope covers the WHOLE render - not a sampled
/// window - because SC-011's allocation clause is protocol-independent and is a
/// hard failure on any machine.
///
/// READING FORM (normative, unit/lifecycle_test.cpp:505-520 /
/// param_cadence_test.cpp:374-381): AllocationScope::getAllocationCount()
/// returns a member assigned only in the DESTRUCTOR
/// (allocation_detector.h:81-87), so reading THAT inside the scope yields a
/// value-initialized 0 and passes unconditionally. The count is taken from the
/// LIVE atomic on the detector singleton while tracking is still on, stored in a
/// local, and asserted after the scope has closed - Catch2's REQUIRE is itself an
/// allocating expression and must never run inside.
[[nodiscard]] RtTrial runRtTrial(Fixture& fx, PerBlockBest& best) {
    RtTrial t{};

    const std::size_t callsBefore = fx.proc->effectsStageProcessCallsForTest();
    const std::size_t evalsBefore = fx.proc->bypassPredicateEvalCountForTest();
    double prevNs = fx.proc->effectsStageNsForTest();
    std::size_t prevResets = fx.proc->spectralDelayResetCountForTest();

    {
        TestHelpers::AllocationScope scope;
        for (std::size_t b = 0; b < kRtBlocks; ++b) {
            const bool seedWritten = rtScriptBlock(fx, b, t.counts);
            // SC-011's EXCEPTION clause, counted rather than reasoned about.
            // Processor::process() is not noexcept, so an exception raised
            // anywhere in the non-noexcept part of the chain unwinds to here.
            // Catching costs nothing on the non-throwing path and allocates
            // nothing, so it is legal inside the AllocationScope.
            try {
                if (fx.processBlock(kBlock) == Steinberg::kResultOk) {
                    ++t.blocksOk;
                }
            } catch (...) {
                ++t.exceptions;
            }

            const double ns = fx.proc->effectsStageNsForTest();
            const std::size_t resets = fx.proc->spectralDelayResetCountForTest();
            const double blockNs = ns - prevNs;
            const std::size_t resetDelta = resets - prevResets;
            prevNs = ns;
            prevResets = resets;

            // ONE slot per block position per class; the slot keeps the best
            // (smallest) of the sixteen trials, per the note above. Writing into a
            // pre-sized vector allocates nothing, so this is legal inside the
            // AllocationScope.
            if (resetDelta == 0u) {
                best.other[b] = std::min(best.other[b], blockNs);
            } else if (seedWritten) {
                // Charged to the SEED bucket when both could have fired in one
                // block - conservative for the bucket that is measured, and it can
                // only cost the FR-008 bucket a sample, never add a spurious one.
                ++t.seedCount;
                best.seed[b] = std::min(best.seed[b], blockNs);
            } else {
                ++t.fr008Count;
                best.fr008[b] = std::min(best.fr008[b], blockNs);
            }

            // A real consumer reads the buffer; this is what stops the optimizer
            // dead-coding the render away.
            t.audioSink += static_cast<double>(fx.audioL()[0])
                           + static_cast<double>(fx.audioR()[kBlockSize - 1u]);
        }
        t.allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    t.processCalls = fx.proc->effectsStageProcessCallsForTest() - callsBefore;
    t.predicateEvals = fx.proc->bypassPredicateEvalCountForTest() - evalsBefore;
    return t;
}

// =============================================================================
// SC-012 / SC-013 - the shared measurement, differing only in configuration
// =============================================================================
// BOTH read FR-041 clause 1's seam, NOT a whole-render delta, and the reason is
// arithmetic rather than taste: the live cold dataset's own run-to-run spread on
// the whole-render figure is 107 420 ns (param_perf_test.cpp:148-156, the
// Phase 9 SC-009 poly-8 row ranging 2 123 410 .. 2 230 830) - 10x SC-012's whole
// threshold, and 35x in the withdrawn T028 dataset (:78-84). A delta of two such
// renders cannot resolve 10 667 ns.
//
// THE DIVISOR IS THE PER-CALL COUNTER (FR-041 clause 1 as amended by plan D-8).
// effectsStageNs_ accumulates PER SLICE while both budgets are PER BLOCK, and
// renderSlice() runs once per SLICE - the loop subdivides on every MIDI event
// (processor.cpp:759-786), on the 2048 cap (:792) and, while any class-(b)
// smoother is unsettled, on the absolute 64-sample grid (:811-815). A per-slice
// divisor would report up to 8x below the true per-block cost on a 512-sample
// block and make SC-013's budget structurally unable to fail. Each case below
// therefore ALSO asserts the seam's counter against the number of process()
// calls the harness itself made, so the two can never drift.

struct StageArm {
    double bestNsPerBlock = std::numeric_limits<double>::max();
    std::size_t harnessCalls = 0;   ///< every processBlock() this arm issued
    std::size_t sendChunks = 0;     ///< FR-041 clause 7 - did the send actually run?
    std::size_t activeVoices = 0;
    double audioSink = 0.0;
};

}  // namespace

// =============================================================================
// SC-011 - RT safety and burst cost
// =============================================================================

TEST_CASE("Effects stage is RT-safe", "[.perf]") {
    // ---- SC-011's LOCK clause, and the scanner's own negative control --------
    // The control runs FIRST and on purpose: a scanner that silently found
    // nothing - a bad path, a stripper that ate the whole file, a find() loop
    // that never advanced - would let the lock clause pass vacuously forever.
    // These four assertions prove the instrument detects what it is looking for
    // before it is believed about what it did not find.
    {
        const std::string positive =
            "void f() { std::mutex m; std::lock_guard<std::mutex> g(m); throw 1; }";
        REQUIRE(countOccurrences(strippedSource(positive), "std::mutex") == 2u);
        REQUIRE(countOccurrences(strippedSource(positive), "lock_guard") == 1u);
        REQUIRE(countOccurrences(strippedSource(positive), "throw ") == 1u);
        // ... and that comments and string literals really are removed, which is
        // what lets the scanned files carry prose about locks without tripping it.
        const std::string commented =
            "int a; // std::mutex\n/* lock_guard */ const char* s = \"throw \";\n";
        REQUIRE(countOccurrences(strippedSource(commented), "std::mutex") == 0u);
        REQUIRE(countOccurrences(strippedSource(commented), "lock_guard") == 0u);
        REQUIRE(countOccurrences(strippedSource(commented), "throw ") == 0u);
    }

    const RtSourceScan scan = scanRtPathSources();
    INFO("first hit: " << scan.firstHit);
    // The corpus is the right one, is non-empty, and was really read - asserted
    // BEFORE the two zero-counts, because a zero count over nothing is not a
    // finding. 100 000 non-comment bytes is a floor, not a fit: processor.cpp
    // alone clears it several times over.
    REQUIRE(scan.filesMissing == 0u);
    REQUIRE(scan.filesScanned == 6u);
    REQUIRE(scan.codeBytes > 100000u);
    REQUIRE(scan.witnesses >= 2u);  // declaration in the header + definition in the TU
    // ... and only now, the clause itself.
    REQUIRE(scan.lockPrimitives == 0u);
    REQUIRE(scan.throwSites == 0u);

    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    // WARM EVERYTHING THE HARNESS OWNS, or the case measures the harness growing.
    // The parameter container grows its queue vector and each queue its point
    // storage on first use only (seraphis_test_fixture.h:116-124, :59-61), so the
    // warm-up must present at least as many queues in one block as the busiest
    // scripted block ever will: phase A block 0 writes the eleven static rows plus
    // the five toggles, and a seed block adds kSeedId.
    bool warmupOk = true;
    for (int b = 0; b < 8; ++b) {
        rtWriteStaticParams(*fx);
        fx->setParam(kFxDelayMixId, 1.0);
        fx->setParam(kFxWanderDepthId, 1.0);
        fx->setParam(kFxAzimuthDepthId, 1.0);
        fx->setParam(kFxWidthId, 1.0);
        fx->setParam(kFxSpectralFreezeId, 0.0);
        fx->setParam(kSeedId, seedNormalizedFor(static_cast<std::size_t>(b) % 16u));
        if (b == 0) {
            pressChord(*fx, kRtPolyphony);
        }
        // NOT `warmupOk = warmupOk && processBlock(...)`: && short-circuits, so a
        // single non-ok result would silently stop rendering the rest of the
        // warm-up. The call is always made and the flag is folded afterwards.
        const bool ok = (fx->processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
    }
    // Settle the DSP itself (reverb build-up, the send's accumulator pipeline,
    // every smoother) before the first measured trial. ONE assertion after the
    // loop rather than one per block: a per-block REQUIRE inside a warm-up adds
    // hundreds of Catch2 assertions to every [.perf] run for no extra coverage.
    for (int b = 0; b < kStageWarmupBlocks; ++b) {
        const bool ok = (fx->processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
    }
    REQUIRE(warmupOk);

    // Best-of-16 per estimate, per the SC-013 protocol - taken along the BLOCK
    // POSITION axis, for the measured reason recorded at PerBlockBest above.
    PerBlockBest best;
    double sink = 0.0;

    for (int trial = 0; trial < kStageTrials; ++trial) {
        const RtTrial t = runRtTrial(*fx, best);

        INFO("SC-011 trial " << trial);
        // ---- the protocol-INDEPENDENT clause, a hard failure on any machine --
        // "zero allocations, LOCKS AND EXCEPTIONS": all three now have an
        // instrument. Allocations and exceptions are counted here per trial; the
        // lock clause is the source scan asserted once, above the trial loop.
        REQUIRE(t.allocations == 0u);
        REQUIRE(t.exceptions == 0u);
        REQUIRE(t.blocksOk == kRtBlocks);
        REQUIRE(fx->checkCanaries());

        // ---- the script really did what SC-011 asks of it -------------------
        REQUIRE(t.counts.sendToggles >= 100u);
        REQUIRE(t.counts.wanderToggles >= 100u);
        REQUIRE(t.counts.freezeToggles >= 100u);
        REQUIRE(t.counts.seedWrites >= 16u);
        REQUIRE(t.fr008Count >= 16u);
        REQUIRE(t.seedCount >= 16u);
        // FR-012 / FR-041 clause 5: the predicate is evaluated EXACTLY once per
        // process() call, so a per-slice regression is visible here rather than
        // only in the wall time.
        REQUIRE(t.processCalls == kRtBlocks);
        REQUIRE(t.predicateEvals == kRtBlocks);

        sink += t.audioSink;
    }

    gSink = gSink + sink;  // optimization barrier

    const double bestWorstFr008 = worstOf(best.fr008);
    const double bestMedianFr008 = medianOfRecorded(best.fr008);
    const double bestWorstSeed = worstOf(best.seed);
    const double bestMedianSeed = medianOfRecorded(best.seed);
    const double bestWorstOther = worstOf(best.other);

    // Every gated statistic must actually rest on the number of events SC-011
    // asks for, now that the buckets are indexed by block position rather than by
    // arrival order.
    REQUIRE(samplesIn(best.fr008) >= 16u);
    REQUIRE(samplesIn(best.seed) >= 16u);
    REQUIRE(samplesIn(best.other) > 0u);

    {
        std::ostringstream os;
        os << "SC-011 RT safety + burst cost (60 s, " << kRtPolyphony
           << " voices, best-of-" << kStageTrials << " per estimate):\n"
           << "  block budget            : " << kBlockBudgetNs << " ns\n"
           << "  burst ceiling  (5.0 %)  : " << kBurstBudgetNs << " ns\n"
           << "  other  ceiling (2.5 %)  : " << kStageBudgetNs << " ns\n"
           << "  SC-011 worst FR-008 blk : " << bestWorstFr008 << " ns  ("
           << ((bestWorstFr008 / kBlockBudgetNs) * 100.0) << " %)\n"
           << "  SC-011 median FR-008 blk: " << bestMedianFr008 << " ns\n"
           << "  SC-011 worst seed  blk  : " << bestWorstSeed << " ns  ("
           << ((bestWorstSeed / kBlockBudgetNs) * 100.0) << " %)\n"
           << "  SC-011 median seed blk  : " << bestMedianSeed << " ns\n"
           << "  SC-011 worst OTHER blk  : " << bestWorstOther << " ns  ("
           << ((bestWorstOther / kBlockBudgetNs) * 100.0) << " %)\n"
           << "  TRANSCRIBE these five figures into this file's BASELINE "
              "PROVENANCE table (one column per run, worst of seven reported).";
        WARN(os.str());
    }

    // A zero-length measurement is not a pass: it means the seam never ran.
    REQUIRE(bestWorstFr008 > 0.0);
    REQUIRE(bestWorstSeed > 0.0);
    REQUIRE(bestWorstOther > 0.0);

    // SC-011's two burst ceilings and its per-block ceiling. If any of these
    // fails, the levers are the stage's own cost and the shipped defaults -
    // never the ceilings (roadmap lines 313-326).
    REQUIRE(bestWorstFr008 <= kBurstBudgetNs);
    REQUIRE(bestWorstSeed <= kBurstBudgetNs);
    REQUIRE(bestWorstOther <= kStageBudgetNs);

    REQUIRE(isFiniteValue(sink));

    // LIVENESS PROBE - a SEPARATE, never nested scope. Nesting is silently wrong
    // in both directions: the inner ctor's startTracking() RESETS the outer count
    // (allocation_detector.h:31-34) and the inner dtor's stopTracking() switches
    // tracking off for the outer scope too (:37-40). Without it the zero above
    // would be vacuous on a detector that counts nothing.
    std::size_t probe = 0;
    {
        TestHelpers::AllocationScope scope;
        // `volatile` is load-bearing: [expr.new]/10 lets a compiler elide an
        // otherwise-unobserved new/delete pair even when the global allocation
        // functions are replaced.
        int* volatile deliberate = new int(7);
        probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
        delete deliberate;
    }
    REQUIRE(probe >= 1u);
}

// =============================================================================
// SC-012 - zero cost at the C-6 defaults
// =============================================================================

TEST_CASE("Effects cost nothing at defaults", "[.perf]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    StageArm arm{};

    // NOT ONE effects parameter is written: SC-012's whole subject is the shipped
    // C-6 default configuration, so the atomics keep their registered defaults
    // (effects_params.h's kFx*Default block) and the send is bypassed, the wander
    // is at identity and the saturation is the engine's own kOutputSaturation.
    pressChord(*fx, kPolyphony);
    bool warmupOk = true;
    for (int b = 0; b < kStageWarmupBlocks; ++b) {
        // The call is unconditional; the flag is folded afterwards, because &&
        // would short-circuit and stop the warm-up on the first non-ok block.
        const bool ok = (fx->processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
        ++arm.harnessCalls;
    }
    REQUIRE(warmupOk);

    // The scenario's preconditions, CHECKED rather than assumed: a figure for a
    // configuration that is not the one named would be the wrong number wearing
    // the right label (param_perf_test.cpp:2023-2027).
    REQUIRE(fx->proc->engineForTest() != nullptr);
    arm.activeVoices = fx->proc->engineForTest()->getActiveVoiceCount();
    REQUIRE(arm.activeVoices == kPolyphony);
    // FR-007's prohibition, at the defaults: the send is neither active nor
    // draining, so SpectralDelay::process is NEVER called. This is the same
    // counter SC-018 clause (e) gates in the CI-gated suite; here it is the
    // structural half of "costs nothing".
    REQUIRE(fx->proc->sendChunkCountForTest() == 0u);

    for (int trial = 0; trial < kStageTrials; ++trial) {
        const double nsBefore = fx->proc->effectsStageNsForTest();
        const std::size_t callsBefore = fx->proc->effectsStageProcessCallsForTest();

        for (int b = 0; b < kStageBlocksPerTrial; ++b) {
            fx->processBlock(kBlock);
            ++arm.harnessCalls;
            arm.audioSink += static_cast<double>(fx->audioL()[0])
                             + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
        }

        const double nsAfter = fx->proc->effectsStageNsForTest();
        const std::size_t callsAfter = fx->proc->effectsStageProcessCallsForTest();
        const std::size_t calls = callsAfter - callsBefore;

        INFO("SC-012 trial " << trial);
        REQUIRE(calls == static_cast<std::size_t>(kStageBlocksPerTrial));
        arm.bestNsPerBlock =
            std::min(arm.bestNsPerBlock, (nsAfter - nsBefore) / static_cast<double>(calls));
    }

    gSink = gSink + arm.audioSink;  // optimization barrier
    arm.sendChunks = fx->proc->sendChunkCountForTest();

    WARN(reportRow("SC-012 effects stage at the C-6 DEFAULTS (8 voices held, Phase 9's SC-009 "
                   "MIDI script, 48 kHz / 512)",
                   arm.bestNsPerBlock, kDefaultsBudgetNs));

    // FR-041 clause 1's divisor identity, over the WHOLE case: the seam's
    // per-CALL counter must equal the number of process() calls this harness
    // made. Nothing else in the suite can catch a divisor that silently became
    // per-slice, and a per-slice divisor makes both budgets unfailable.
    REQUIRE(fx->proc->effectsStageProcessCallsForTest() == arm.harnessCalls);
    // Still true after the whole render: the default send never ran a chunk.
    REQUIRE(arm.sendChunks == 0u);
    REQUIRE(fx->checkCanaries());

    // A 0 ns arm FAILS: it means the timer never ran, not that the stage is free.
    // The stage always pays FR-041 clause 6's tap copy plus two clock reads, so a
    // genuinely zero figure is a broken seam.
    REQUIRE(arm.bestNsPerBlock > 0.0);
    REQUIRE(arm.bestNsPerBlock <= kDefaultsBudgetNs);
    REQUIRE(isFiniteValue(arm.audioSink));
}

// =============================================================================
// SC-013 - the effects stage inside its 2.5 % budget, fully active
// =============================================================================

namespace {

/// SC-013's automation. The criterion REQUIRES an automation point on 1410 /
/// 1441 / 1443 so that plan D-6's 64-sample subdivision cost is INSIDE the
/// measured figure rather than discovered later (its ratio was measured at
/// <= 11.7 % of whole-block wall time, param_perf_test.cpp:86-91).
///
/// The three IDs alternate between the maximum and 0.9 x the maximum on a
/// four-block period. Re-writing the IDENTICAL value would be useless: the
/// class-(b) smoothers change-detect on their target (processor.h's
/// setParamSmootherTargets), so an unchanged target moves no smoother, leaves
/// anyClassBSmootherUnsettled() false and produces NO subdivision at all. 0.9 is
/// chosen rather than 0 so the configuration stays at (or immediately beside) the
/// maxima SC-013 names, and so the send never leaves the Active state.
constexpr std::size_t kSc013AutoPeriod = 4;

void sc013WriteMaxima(Fixture& fx) {
    // "All 16 effects parameters at maxima" - every registered row driven to
    // normalized 1.0, which is the top of its C-6 range for the twelve `R` rows,
    // the last index for the two `L` dropdowns and `on` for the two `T` toggles.
    fx.setParam(kFxSaturationId, 1.0);
    fx.setParam(kFxDelayMixId, 1.0);
    fx.setParam(kFxDelayTimeId, 1.0);
    fx.setParam(kFxDelaySpreadId, 1.0);
    fx.setParam(kFxDelaySpreadDirectionId, 1.0);
    fx.setParam(kFxDelayFeedbackId, 1.0);
    fx.setParam(kFxDelayTiltId, 1.0);
    fx.setParam(kFxDelayDiffusionId, 1.0);
    fx.setParam(kFxDelayWidthId, 1.0);
    fx.setParam(kFxDelaySyncId, 1.0);
    fx.setParam(kFxDelaySyncNoteId, 1.0);
    fx.setParam(kFxSpectralFreezeId, 1.0);
    fx.setParam(kFxWidthId, 1.0);
    fx.setParam(kFxWanderDepthId, 1.0);
    fx.setParam(kFxWanderRateId, 1.0);
    fx.setParam(kFxAzimuthDepthId, 1.0);
}

void sc013WriteAutomation(Fixture& fx, std::size_t block) {
    if ((block % kSc013AutoPeriod) != 0u) {
        return;
    }
    const double v = (((block / kSc013AutoPeriod) % 2u) == 0u) ? 1.0 : 0.9;
    fx.setParam(kFxDelayMixId, v);      // 1410, class (b)
    fx.setParam(kFxWanderDepthId, v);   // 1441, class (b)
    fx.setParam(kFxAzimuthDepthId, v);  // 1443, class (b)
}

}  // namespace

TEST_CASE("Effects stage stays inside its 2.5 % budget", "[.perf]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    StageArm arm{};
    std::size_t block = 0;

    // The full-poly operating point WITH VOICES SOUNDING - never zero voices,
    // which is a configuration that never occurs in use and which SC-014's
    // arithmetic could not compose with.
    sc013WriteMaxima(*fx);
    pressChord(*fx, kPolyphony);
    bool warmupOk = true;
    for (int b = 0; b < kStageWarmupBlocks; ++b) {
        sc013WriteAutomation(*fx, block);
        // Unconditional call, flag folded afterwards - && would short-circuit.
        const bool ok = (fx->processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
        ++arm.harnessCalls;
        ++block;
    }
    REQUIRE(warmupOk);

    REQUIRE(fx->proc->engineForTest() != nullptr);
    arm.activeVoices = fx->proc->engineForTest()->getActiveVoiceCount();
    REQUIRE(arm.activeVoices == kPolyphony);
    // The send is genuinely running - otherwise this criterion would be budgeting
    // an idle stage. One chunk per 512 samples once engaged (C-2 clause 5).
    REQUIRE(fx->proc->sendChunkCountForTest() > 0u);

    for (int trial = 0; trial < kStageTrials; ++trial) {
        const double nsBefore = fx->proc->effectsStageNsForTest();
        const std::size_t callsBefore = fx->proc->effectsStageProcessCallsForTest();
        const std::size_t chunksBefore = fx->proc->sendChunkCountForTest();

        for (int b = 0; b < kStageBlocksPerTrial; ++b) {
            sc013WriteAutomation(*fx, block);
            fx->processBlock(kBlock);
            ++arm.harnessCalls;
            ++block;
            arm.audioSink += static_cast<double>(fx->audioL()[0])
                             + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
        }

        const double nsAfter = fx->proc->effectsStageNsForTest();
        const std::size_t callsAfter = fx->proc->effectsStageProcessCallsForTest();
        const std::size_t calls = callsAfter - callsBefore;

        INFO("SC-013 trial " << trial);
        // The same harness-call-count assertion SC-012 makes, and it is
        // load-bearing HERE rather than merely tidy: this render is REQUIRED to
        // carry an automation point on 1410/1441/1443 and therefore renders as
        // 64-sample sub-slices, which is precisely the case a per-slice divisor
        // would under-report by up to 8x.
        REQUIRE(calls == static_cast<std::size_t>(kStageBlocksPerTrial));
        // The subdivision really happened - the send ran at least one chunk per
        // block, so the trial measured the loaded stage.
        REQUIRE((fx->proc->sendChunkCountForTest() - chunksBefore)
                >= static_cast<std::size_t>(kStageBlocksPerTrial));

        arm.bestNsPerBlock =
            std::min(arm.bestNsPerBlock, (nsAfter - nsBefore) / static_cast<double>(calls));
    }

    gSink = gSink + arm.audioSink;  // optimization barrier

    WARN(reportRow("SC-013 effects stage at MAXIMA (8 voices held, Phase 9's SC-009 MIDI script, "
                   "automation on 1410/1441/1443, 48 kHz / 512)",
                   arm.bestNsPerBlock, kStageBudgetNs));

    REQUIRE(fx->proc->effectsStageProcessCallsForTest() == arm.harnessCalls);
    REQUIRE(fx->checkCanaries());
    REQUIRE(arm.bestNsPerBlock > 0.0);
    // THE BUDGET. If this fails, the levers are the stage's own cost and the
    // shipped defaults. The 25 % ceiling is NEVER the lever (roadmap lines
    // 313-326), and neither is kBaselineFullPolyNs (param_perf_test.cpp:454-455).
    REQUIRE(arm.bestNsPerBlock <= kStageBudgetNs);
    REQUIRE(isFiniteValue(arm.audioSink));
}
