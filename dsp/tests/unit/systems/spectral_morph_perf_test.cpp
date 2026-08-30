// ==============================================================================
// Layer 3: System Tests - SpectralMorphEngine + HarmonicCloud, CPU budget (SC-010)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per specs/seraphis-phase3-spectral-morph/
//
// Reference: specs/seraphis-phase3-spectral-morph/spec.md   (SC-010, :1761-1891)
//            specs/seraphis-phase3-spectral-morph/plan.md   (S8, the cost model
//                                                            and the lever ladder)
//
// This TU also discharges plan S1's T0.2 prerequisite, whose TIMING is amended
// (RA-4): the SC-010 spike is measured HERE rather than before the plan was
// written, and its machine / build config / trial shape / date are recorded in
// the BASELINE PROVENANCE block below, in the shape of
// harmonic_cloud_perf_test.cpp:104-122.
//
// WHY ns/block AND NOT "% of one core" (identical basis to Phase 2 SC-007,
// harmonic_cloud_perf_test.cpp:10-18):
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. SC-010 therefore pins
//   the measurement basis to NANOSECONDS PER 512-SAMPLE BLOCK (8 x 64-sample
//   control chunks) and gates each absolute figure against its own CHECKED-IN
//   BASELINE as a relative regression bound (fail if > baseline x 1.5). The
//   absolute percentages - 0.15 % of one core for clause 1 (~16,000 ns), 0.5 %
//   for clause 2 (~53,333 ns) - are REPORTED, and are binding only on the
//   reference machine named under BASELINE PROVENANCE.
//
// THE THREE CLAUSES (spec.md:1773-1826), and what each one is FOR:
//   1. SpectralMorphEngine ALONE - its owned EntropyProcessor and
//      SplineTrajectory included - at 4 states, bloom 1, entropy 1, Spline
//      travel. Phase 3's own new budget. Gated: measured <= kMorphBaselineNs x 1.5,
//      and the checked-in baseline itself must satisfy both shipped relations
//      against kMorphReferenceNsPerBlock.
//   2. HarmonicCloud with a CHANGING spectral target re-supplied every control
//      chunk - the FR-086 shipped cadence, not a stress case - must fit INSIDE
//      Phase 2's existing 0.5 %/voice envelope, gated against the new checked-in
//      kCloudChangingTargetBaselineNs under BOTH shipped relations plus the
//      injection-cost ceiling against Phase 2's shipped no-target baseline.
//   3. An UNCHANGED target is ~free: measuredUnchangedTargetNs <=
//      measuredCloudBaselineNs x 1.10. This is the ONLY clause that exercises
//      FR-085 lever 1 (the whole-array skip) - clause 2 supplies a changing
//      target every chunk, where that skip provably never fires.
//
// TWO KINDS OF IDENTIFIER, KEPT TYPOGRAPHICALLY DISTINCT (spec.md:1858-1863):
//   `k`-prefixed  -> CHECKED-IN constants, edited only with a provenance block.
//   no prefix     -> PER-RUN measurements. Clause 3 gates a measurement against
//                    another measurement FROM THE SAME RUN - never two literals,
//                    which would be satisfiable by editing the literals and would
//                    prove nothing about whether the skip exists.
//
// THE INJECTION COST IS REPORTED, NEVER GATED (spec.md:1864-1868):
//   measuredChangingTargetNs - measuredCloudBaselineNs is the difference of two
//   sampled minima - a biased, possibly negative statistic that a multiplicative
//   regression factor cannot bound. It is WARN-reported for information only.
//
// THIS CASE IS "[.perf]":
//   Every CI leg excludes perf-tagged cases (.github/workflows/ci.yml filters
//   '~[performance]~[perf]~[benchmark]~[!benchmark]'; :328, :574, :951, and
//   valgrind-nightly.yml:202), so no job ever evaluates an absolute figure.
//   Perf cases are HIDDEN by default and must be named explicitly:
//     build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SpectralMorph_CpuBudget"
//
// NO ALLOCATION-TRACKING INCLUDES HERE: this TU must not pull in
// <allocation_operator_overrides.h> (duplicate-symbol link error against the
// single owner in this binary - plan S0.1 item 5). SC-011 covers allocation in
// spectral_morph_engine_test.cpp.
//
// No std::isnan / std::isinf / numeric_limits infinity anywhere: the macOS leg
// builds with -ffast-math, which folds them. Finiteness is checked on the
// IEEE-754 exponent field instead.
//
// FTZ/DAZ: dsp_test_main.cpp:13 calls enableFTZDAZ() before any case runs, so
// every figure below is measured with denormals flushed BY THE PROCESS - the
// same environment the audio thread runs in.
// ==============================================================================

#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/processors/spline_trajectory.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <catch2/catch_test_macros.hpp>

using namespace Krate::DSP;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

// =============================================================================
// Measurement basis (SC-010, spec.md:1846-1851)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// The FR-086 slice length: a target supplied once per HOST block would be
/// frozen for all 8 of the cloud's internal control chunks, so the shipped
/// cadence slices at <= HarmonicCloud::kControlChunkSamples
/// (harmonic_cloud.h:730-748). The 8 extra call boundaries that costs are
/// INSIDE every cloud figure below, not outside it.
constexpr std::size_t kChunkSamples = HarmonicCloud::kControlChunkSamples;  // 64

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// Relative regression bound applied to every checked-in baseline (SC-010,
/// identical to Phase 2's kRegressionFactor at harmonic_cloud_perf_test.cpp:76).
constexpr double kRegressionFactor = 1.5;

// --- Clause 1 reference figures: Phase 3's own 0.15 %-of-one-core budget ------

/// SC-010 clause 1's absolute reference: 0.15 % of the 512-at-48-kHz budget.
/// REPORTED, not asserted, on non-reference machines.
constexpr double kMorphReferenceNsPerBlock = kBlockBudgetNs * 0.0015;  // 16,000 ns

/// The largest clause-1 baseline the static_asserts below can accept
/// (~10,666.7 ns). A measurement above this figure means Phase 3 is OVER
/// BUDGET: the response is to spend the pre-decided levers of plan S8 in order,
/// and - if they are all spent and the figure still misses - to STOP AND REPORT
/// it as an honest finding against SC-010. kMorphReferenceNsPerBlock IS NEVER
/// RAISED (plan S8 lever 6: "there is no lever 6"; RA-3's escape is scoped to
/// clause 2 only).
constexpr double kMorphMaxAdmissibleBaselineNs = kMorphReferenceNsPerBlock / kRegressionFactor;

// --- Clause 2 reference figures: Phase 2's SHIPPED envelope, reused verbatim --
//
// These three are transcribed from the shipped Phase 2 perf TU rather than
// re-derived, so clause 2 is stated against the gate construction that actually
// ships rather than against a figure that only looks like it:
//   harmonic_cloud_perf_test.cpp:80  kReferenceNsPerBlock            (~53,333)
//   harmonic_cloud_perf_test.cpp:101 kMaxAdmissibleBaselineNsPerBlock (~35,556)
//   harmonic_cloud_perf_test.cpp:140 kAutomatedBaselineNsPerBlock     (26,000)

/// Phase 2 SC-007's absolute reference: 0.5 % of the same block budget.
constexpr double kReferenceNsPerBlock = kBlockBudgetNs * 0.005;  // ~53,333 ns

/// ~35,555.6 ns. The ceiling on any cloud-side baseline.
constexpr double kMaxAdmissibleBaselineNsPerBlock = kReferenceNsPerBlock / kRegressionFactor;

/// Phase 2's SHIPPED gating baseline for the no-target cloud
/// (harmonic_cloud_perf_test.cpp:140). Clause 2 reports
/// kCloudChangingTargetBaselineNs / this figure as the injection's
/// MULTIPLICATIVE cost and requires it <= kMaxInjectionCostFactor.
constexpr double kShippedNoTargetBaselineNs = 26000.0;

/// 35,555.6 / 26,000 = 1.3675, so 1.36 is the ceiling SC-010 clause 2 states
/// (spec.md:1790-1793) - i.e. the injection may cost at most 36 % on top of the
/// shipped no-target baseline before Phase 2's own envelope is breached.
constexpr double kMaxInjectionCostFactor = 1.36;

/// Clause 3's ratio bound: an unchanged target must cost at most 10 % over the
/// no-target cloud. Structurally satisfied by FR-085 lever 1 - an unchanged
/// target sets no mask bits, raises no dirty flag, and costs 128 compares per
/// chunk (harmonic_cloud.h:779-810).
constexpr double kUnchangedTargetRatioBound = 1.10;

// =============================================================================
// BASELINE PROVENANCE (SC-010; shape of harmonic_cloud_perf_test.cpp:104-122)
// =============================================================================
//   Git SHA    : 8d90d9ba + the Phase 3 working tree (levers 4 and 5 SPENT,
//                 FR-085 lever 1's whole-array skip implemented)
//   Machine    : 13th Gen Intel(R) Core(TM) i9-13900HX  (Phase 2's reference
//                 machine, which these figures must be comparable against)
//   OS         : Microsoft Windows 11 Pro, build 26200
//   Compiler   : MSVC 19.44.35220.0, x64 (Visual Studio 17 2022)
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-100 x 500 blocks (see the trial-shape comment below)
//   Measured   : 2026-07-27
//
//   Eight consecutive runs of the SAME BINARY, no rebuild between them, ns/block:
//
//   run                |   1      2      3      4      5      6      7      8   |
//   morph (clause 1)   |  9038   8569   8910   8941   9148   9227   9292   9053 |
//   cloud, no target   | 21382  21491  22225  21758  21618  21688  21648  21778 |
//   cloud, unchanged   | 21092  21852  22429  21608  21772  21791  21024  21984 |
//   cloud, changing    | 28043  28520  27552  29119  28180  28362  28230  28306 |
//   clause-3 ratio     | 0.986  1.017  1.009  0.993  1.007  1.005  0.971  1.009 |
//
//   min / max: morph 8,568.6 / 9,291.8 ; no target 21,382 / 22,224.8 ;
//              unchanged 21,024.4 / 21,983.6 ; changing 27,552 / 29,119.
//
// BOTH BASELINES BELOW ARE THE WORST (LARGEST) OF THOSE EIGHT RUNS, ROUNDED UP -
// not the best - so a normal run has margin against the gate rather than sitting
// on it, which is how Phase 2 set its own (harmonic_cloud_perf_test.cpp:119-122).
// This TU now gates REGRESSIONS as well as the spec budget.
//
// HOW CLAUSE 1 GOT HERE (plan S8's ladder, spent IN ORDER, not improvised):
//   1-3. Log-domain morph pipeline (D1), combined stage-2/3 conversion (D3), and
//        the exact-zero fast paths - ADOPTED FROM THE START (T012/T015).
//   4.   SPENT. centsToPitchRatioFast: the bounded-domain degree-4 Horner of
//        HarmonicCloud::detail::centsToDriftRatio promoted to core/pitch_utils.h,
//        and detail::centsToDriftRatio rewritten as a one-line forward.
//        EntropyProcessor::applyStages now calls it instead of an exp2. Entropy's
//        cent domain is +-11.0, 4.5x inside the polynomial's documented window.
//   5.   SPENT. Entropy OU control interval 32 -> 64 samples: 8 control steps per
//        512-sample block instead of 16, i.e. 3,072 nextFloat() draws per block
//        instead of 6,144. An EXACT re-derivation of a/g from the doubled dt, not
//        an approximation. Consequences landed with it: EntropyProcessor owns
//        kEntropyControlInterval; EntropyProcessor_OuBankMatchesBrownianDrift's
//        Arm 1 was REPLACED (never deleted) with a half-sample-rate reference
//        construction that reproduces the same real dt, and Arm 2 is now the
//        explicit-coefficient check at dt = 64/fs; SC-013's grid was re-run.
//   6.   THERE IS NO LEVER 6, and it was not needed.
//
// Measured effect of the ladder on clause 1, same machine, same binary shape:
//   before levers 4-5 : 14,190 / 14,663 ns/block  (0.133 % / 0.137 % of budget)
//   after  levers 4-5 :  8,569 - 9,292 ns/block   (0.080 % - 0.087 %)
// The dominant term was the OU control steps, exactly as plan S8's cost model
// predicted; lever 4 alone was inside the run-to-run noise at N = 25.
// =============================================================================

/// Checked-in ns/block baseline for CLAUSE 1 (the engine alone) - THE GATE.
/// MEASURED: the worst of the eight runs in BASELINE PROVENANCE above (9,291.8),
/// rounded up. The effective gate is 13,950 ns/block, which is BOTH inside
/// SC-010's 16,000 ns budget and a real regression bound.
constexpr double kMorphBaselineNs = 9300.0;

static_assert(kMorphBaselineNs * kRegressionFactor <= kMorphReferenceNsPerBlock,
              "SC-010 clause 1: baseline must be no weaker than the 0.15 % reference figure");

/// The binding arithmetic of SC-010 spelled out a second way: the checked-in
/// baseline must itself be an admissible baseline. Equivalent to the assert
/// above by construction, but it is the form spec.md:1869-1872 states, and it
/// fails with the message that names the actual rule if someone edits
/// kRegressionFactor instead.
static_assert(kMorphBaselineNs <= kMorphMaxAdmissibleBaselineNs,
              "SC-010 clause 1: a baseline above kMorphMaxAdmissibleBaselineNs means Phase 3 is "
              "over budget - spend plan S8's levers in order, never raise the baseline");

/// Checked-in ns/block baseline for CLAUSE 2 (the cloud with a target changing
/// every control chunk) - THE GATE. MEASURED: the worst of the eight runs in
/// BASELINE PROVENANCE above (29,119), rounded up. Comfortably under all three
/// ceilings - 35,555.6 by admissibility, 35,360 by the 1.36 injection-cost factor
/// - so the FR-080 injection sits INSIDE Phase 2's shipped envelope with margin,
/// and this figure now gates regressions rather than merely restating the spec.
constexpr double kCloudChangingTargetBaselineNs = 29200.0;

static_assert(kCloudChangingTargetBaselineNs * kRegressionFactor <= kReferenceNsPerBlock,
              "SC-010 clause 2: baseline must be no weaker than Phase 2's 0.5 % reference figure");

static_assert(kCloudChangingTargetBaselineNs <= kMaxAdmissibleBaselineNsPerBlock,
              "SC-010 clause 2: a baseline above kMaxAdmissibleBaselineNsPerBlock means the FR-080 "
              "injection has pushed the cloud outside Phase 2's shipped envelope - make the "
              "injection cheaper via FR-085's three levers, never raise the baseline. Only after "
              "all three are spent does RA-3 permit raising roadmap line 164, and only for this "
              "clause");

static_assert(kCloudChangingTargetBaselineNs / kShippedNoTargetBaselineNs
                  <= kMaxInjectionCostFactor,
              "SC-010 clause 2: the injection's multiplicative cost over Phase 2's shipped "
              "no-target baseline must not exceed 1.36");

/// The measured configuration in compile-time statements: a 512-sample block is
/// exactly 8 control chunks, so both of the cloud's drift lane banks and both of
/// the engine's entropy OU banks are read 8 times per block. If either component's
/// chunk size moves, these measurements no longer describe what SC-010 specified
/// and the TU stops compiling.
static_assert(kBlockSize % kChunkSamples == 0, "block must be a whole number of control chunks");
static_assert(kBlockSize / kChunkSamples == 8, "SC-010 measures 8 chunks per 512-sample block");
static_assert(HarmonicCloud::kMaxPartials == 64, "SC-010 measures the 64-partial cloud");
static_assert(SpectralMorphEngine::kStatePartials == HarmonicCloud::kMaxPartials,
              "FR-086 composition: the engine's output arrays feed the cloud slot for slot");
static_assert(SpectralMorphEngine::kMaxStates == 4, "SC-010 clause 1 measures the 4-state engine");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// The shape is Phase 2's, deliberately unchanged, so the figures below are
// directly comparable with harmonic_cloud_perf_test.cpp's - clause 2's whole
// construction is a comparison against that TU's shipped baseline.
// Phase 2's own justification, verified there at :171-188: the reference machine
// is a hybrid part (P-cores + E-cores) whose dominant noise source is a whole
// trial being migrated onto an E-core (~20 % step in ns/block). Many short trials
// make it very likely at least one runs start-to-finish on a boosted P-core.
// Pinning affinity was tried and REJECTED there - not portable, and it selects a
// core by index without knowing its type.
//
// 500 blocks ~= 5.3 s of audio, all of it in envelope Hold (the gate stays on for
// the cloud cases and the engine has no envelope), so every trial measures
// identical steady-state work.
//
// FOUR figures are measured back to back in one run. Best-of-N is what makes that
// sound: a thermal or frequency drift across the run inflates the MEAN of the
// later measurements but not their MINIMUM, and every gate here is on a minimum.
//
// N IS 100 HERE, NOT PHASE 2'S 25, AND THE REASON IS CLAUSE 3.
// Every other gate in this TU compares a measurement against a CONSTANT, where a
// few per cent of residual noise in the minimum is absorbed by the 1.5x
// regression factor. Clause 3 does not: it compares two SAMPLED MINIMA against a
// 1.10 ratio bound, so its noise is the QUOTIENT of two noisy estimates. Measured
// on the reference machine at N = 25, eight consecutive runs of the SAME BINARY
// produced clause-3 ratios from 0.863 to 1.106 - i.e. the clause failed on run 4
// and passed on the other seven, on a path whose real cost difference is two
// 256-byte std::memcmp calls per 64-sample chunk. That is a measurement defect,
// not a finding about the code, and the honest fix is a better estimator rather
// than a wider bound: the minimum of N samples converges from above as N grows,
// and quadrupling N costs ~4 s of test time.
// THE BOUND ITSELF IS UNTOUCHED (1.10), and so are all four checked-in baselines.
constexpr int kTrials = 100;
constexpr int kWarmupBlocks = 400;
constexpr int kBlocksPerTrial = 500;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which folds
/// std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Best-of-N driver. `runBlock` performs exactly one 512-sample block of work.
/// Taken by const reference, not by forwarding reference: it is INVOKED, many
/// times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestNsPerBlock(int trials, int blocksPerTrial, const BlockFn& runBlock) {
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
// Clause 1 configuration: the engine alone
// =============================================================================

/// SC-010 clause 1's configuration (spec.md:1884-1886): 4 states, bloom = 1,
/// entropy = 1 (all four entropy stages live, both OU banks advancing,
/// death/rebirth active), Spline travel at SplineTrajectory::kDefaultInterval.
///
/// Four DISTINCT factory states, not four copies of one: SC-001's 4-state arm
/// uses SineStack/Bell/Glass/Breath, and identical slots would leave the
/// interpolation lerps multiplying equal operands - the same instruction count,
/// but not the same numbers, and not the configuration the criterion names.
///
/// setTravelRate(kMaxTravelRate) keeps the shared slew limiter engaged, so the
/// journey never parks at an endpoint where interpolate()'s a == b fast path
/// (spectral_morph_engine.h:617-631) would take over and measure the degenerate
/// segment instead of the general one.
void configureEngine(SpectralMorphEngine& engine) {
    engine.prepare(kSr48);
    engine.setSeed(1u);
    engine.setState(0, makeFactoryState(SpectralStateId::SineStack));
    engine.setState(1, makeFactoryState(SpectralStateId::Bell));
    engine.setState(2, makeFactoryState(SpectralStateId::Glass));
    engine.setState(3, makeFactoryState(SpectralStateId::Breath));
    engine.setStateCount(SpectralMorphEngine::kMaxStates);
    engine.setBloom(1.0f);
    engine.setEntropy(1.0f);
    engine.setTravelMode(SpectralMorphEngine::TravelMode::Spline);
    engine.setWaypointInterval(static_cast<double>(SplineTrajectory::kDefaultInterval));
    engine.setTravelRate(SpectralMorphEngine::kMaxTravelRate);
}

// =============================================================================
// Clauses 2 and 3 configuration: the cloud, and the targets fed to it
// =============================================================================

/// Phase 2 SC-007's cloud configuration, transcribed from
/// harmonic_cloud_perf_test.cpp:257-264 so clauses 2 and 3 differ from the
/// shipped no-target measurement ONLY in whether and how a target is supplied
/// (spec.md:1886-1890): 64 partials, Mutation 1.0, drift depth at its maximum,
/// both lane banks live, gate on. The fundamental is deliberately left at its
/// default, exactly as Phase 2 leaves it.
///
/// WHY THE PHASE-7 MACRO AUTOMATION IS *NOT* APPLIED HERE, even though Phase 2's
/// GATING baseline is its automated case: every macro setter calls
/// markFreqDirty()/markAmpDirty(), which set the per-slot masks to ALL ONES
/// (harmonic_cloud.h:1206-1208). Under automation an unchanged target and a
/// changing target cost the same, because the automation already dirtied every
/// slot - clause 3 would then be structurally unmeasurable and would pass
/// vacuously on an implementation with no skip at all. Clauses 2 and 3 must be
/// the same configuration except for the target, so both use the STATIC cloud.
/// Consequence to keep in view when reading the figures: measuredCloudBaselineNs
/// is comparable with Phase 2's STATIC baseline (24,000 ns,
/// harmonic_cloud_perf_test.cpp:126) plus the 8 FR-086 call boundaries, while
/// kCloudChangingTargetBaselineNs's 1.36 injection-cost ceiling is stated by
/// SC-010 against the AUTOMATED baseline (26,000 ns, :140) - the ratio is the
/// spec's own normalizer for "36 % headroom inside the 0.5 % envelope", not a
/// like-for-like comparison of two measurements.
void configureCloud(HarmonicCloud& cloud) {
    cloud.prepare(kSr48);
    cloud.setRichness(1.0f);  // N(1) = 64 (also the FR-003 default; explicit here)
    cloud.setMutation(1.0f);
    cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    cloud.setStereoSpread(1.0f);
    cloud.noteOn();
}

/// One control chunk's worth of spectral target: the exact pair of arrays
/// SpectralMorphEngine hands HarmonicCloud in the FR-086 shape.
struct TargetFrame {
    std::array<float, HarmonicCloud::kMaxPartials> ratios{};
    std::array<float, HarmonicCloud::kMaxPartials> amplitudes{};
};

/// Power of two so the timed loop's wrap is a mask, not a modulo. 64 frames is
/// 8 blocks of cadence before the table repeats.
constexpr std::size_t kFrameTableSize = 64;

struct TargetTable {
    std::array<TargetFrame, kFrameTableSize> frames{};
    std::size_t count = 0;
};

/// Pre-built, for the same reason Phase 2 pre-builds its macro trajectory
/// (harmonic_cloud_perf_test.cpp:214-218): the timed loop must pay for
/// setSpectralTarget and the recompute it schedules, NOT for the engine that
/// generates the targets. Running a live SpectralMorphEngine inside clause 2's
/// loop would add clause 1's ~16,000 ns to a figure that has to fit inside
/// 35,556 ns, and would be measuring the wrong thing besides - clause 2 is about
/// the CLOUD's injection cost.
///
/// THE FRAMES MUST EXERCISE BOTH PER-SLOT MASKS (SC-010 clause 2, plan S9.2):
/// they are captured MID-JOURNEY from a bloom = 0.5 traversal at kMaxTravelRate
/// with entropy = 1, which moves the supplied targets in RATIO *and* in
/// AMPLITUDE every chunk. That is what makes a missing `ampSlotDirty_ = 0`
/// (harmonic_cloud.h:1462) show up as a COST rather than as silent dead code.
/// The property is not assumed - the test asserts it on the table below, using
/// the cloud's own public epsilons.
[[nodiscard]] TargetTable buildTargetFrames() {
    SpectralMorphEngine engine;
    engine.prepare(kSr48);
    engine.setSeed(1u);
    engine.setState(0, makeFactoryState(SpectralStateId::SineStack));
    engine.setState(1, makeFactoryState(SpectralStateId::Bell));
    engine.setStateCount(2);
    engine.setBloom(0.5f);
    engine.setEntropy(1.0f);
    engine.setTravelMode(SpectralMorphEngine::TravelMode::External);
    engine.setTravelRate(SpectralMorphEngine::kMaxTravelRate);
    engine.setTargetPosition(1.0f);

    // Advance to mid-journey. The shared slew cap is
    // travelRate * (numStates - 1) * dt = 1.0 * 1 * 64/48000 = 1.3333e-3 units
    // per chunk (spectral_morph_engine.h:690), so 0.5 units is 375 chunks, and
    // the 64 captured chunks below cover 0.5000 -> 0.5853 - entirely inside the
    // journey, with the limiter engaged on every one of them.
    constexpr int kChunksToMidJourney = 375;
    for (int i = 0; i < kChunksToMidJourney; ++i) {
        engine.updateChunk(kChunkSamples);
    }

    TargetTable table{};
    table.count = engine.getOutputCount();
    for (auto& frame : table.frames) {
        engine.updateChunk(kChunkSamples);
        const float* ratios = engine.getOutputRatios();
        const float* amplitudes = engine.getOutputAmplitudes();
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            frame.ratios[i] = ratios[i];
            frame.amplitudes[i] = amplitudes[i];
        }
    }
    return table;
}

/// How many slots setSpectralTarget's per-slot masks would light up for one
/// frame-to-frame step, counted with the cloud's OWN epsilons
/// (harmonic_cloud.h:251-253).
///
/// Comparing consecutive FRAMES is a faithful proxy for the shipped test, which
/// compares against committedRatio_/committedAmp_ - the values the last
/// recompute actually consumed (harmonic_cloud.h:782-798). They coincide
/// whenever the previous chunk recomputed the slot, which at this travel rate is
/// almost every slot on almost every chunk; where they diverge, the committed
/// value is STALER, so the real mask lights up at least as many slots as counted
/// here. The count below is therefore a lower bound on the work the timed loop
/// actually pays for.
struct DirtyCounts {
    std::size_t ratioSlots = 0;
    std::size_t ampSlots = 0;
};

[[nodiscard]] DirtyCounts countDirtySlots(const TargetFrame& previous,
                                          const TargetFrame& next) noexcept {
    DirtyCounts counts;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const float previousRatio = previous.ratios[i];
        if (std::abs(next.ratios[i] - previousRatio)
            > previousRatio * HarmonicCloud::kTargetRatioRelEpsilon) {
            ++counts.ratioSlots;
        }
        if (std::abs(next.amplitudes[i] - previous.amplitudes[i])
            > HarmonicCloud::kTargetAmpEpsilon) {
            ++counts.ampSlots;
        }
    }
    return counts;
}

}  // namespace

// =============================================================================
// SC-010 - all three clauses, ONE run of ONE TU
// =============================================================================
// Deliberately not three TEST_CASEs and not three SECTIONs: clause 3 gates a
// measurement against another measurement, and Catch2 re-runs the whole case body
// per SECTION, which would put the two comparands in different runs on different
// machine states. spec.md:1812-1819 requires them "measured in the same run of
// the same TU".
// =============================================================================

TEST_CASE("SpectralMorph_CpuBudget", "[.perf]") {
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    double sink = 0.0;

    // -------------------------------------------------------------------------
    // CLAUSE 1 - SpectralMorphEngine alone (its EntropyProcessor and
    // SplineTrajectory included), 8 x updateChunk(64) per 512-sample block.
    // -------------------------------------------------------------------------

    SpectralMorphEngine engine;
    configureEngine(engine);

    // Reading two outputs per block is what stops the optimizer dead-coding the
    // whole pipeline away; a real consumer reads both arrays in full, so this is
    // not artificial overhead.
    const auto morphBlock = [&]() noexcept {
        for (std::size_t chunk = 0; chunk < kBlockSize / kChunkSamples; ++chunk) {
            engine.updateChunk(kChunkSamples);
        }
        sink += static_cast<double>(engine.getOutputRatios()[0])
                + static_cast<double>(
                    engine.getOutputAmplitudes()[SpectralMorphEngine::kStatePartials - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        morphBlock();
    }

    // Preconditions, not perf assertions: reporting a "4 states, bloom 1,
    // entropy 1" figure measured on a collapsed configuration would be a lie.
    REQUIRE(engine.getStateCount() == SpectralMorphEngine::kMaxStates);
    REQUIRE(engine.getOutputCount() == SpectralMorphEngine::kStatePartials);
    REQUIRE(engine.getTravelMode() == SpectralMorphEngine::TravelMode::Spline);
    REQUIRE(engine.stateFinite());

    const double measuredMorphNs = bestNsPerBlock(kTrials, kBlocksPerTrial, morphBlock);

    // Guards against the whole loop being optimized out (a zero-cost "pass").
    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    const double morphPercentOfBudget = (measuredMorphNs / kBlockBudgetNs) * 100.0;

    WARN("SC-010 clause 1 (GATING) SpectralMorphEngine alone: 4 states, bloom 1.0,\n"
         << "entropy 1.0, Spline travel at kDefaultInterval, 8 chunks/block.\n"
         << "  measured        : " << measuredMorphNs << " ns/block (512 samples @ 48 kHz)\n"
         << "  block budget    : " << kBlockBudgetNs << " ns\n"
         << "  % of budget     : " << morphPercentOfBudget << " %  (SC-010 clause 1: <= 0.15 %)\n"
         << "  reference figure: " << kMorphReferenceNsPerBlock << " ns/block\n"
         << "  checked-in base : " << kMorphBaselineNs << " ns/block  (gate: x" << kRegressionFactor
         << " = " << (kMorphBaselineNs * kRegressionFactor) << " ns/block)");

    // Over-budget report. A measurement above kMorphMaxAdmissibleBaselineNs means
    // no honest baseline can be checked in, because baseline x 1.5 would exceed
    // the SC-010 clause 1 reference figure.
    if (measuredMorphNs > kMorphMaxAdmissibleBaselineNs) {
        WARN("SC-010 clause 1 OVER BUDGET: "
             << measuredMorphNs << " ns/block exceeds the largest admissible baseline ("
             << kMorphMaxAdmissibleBaselineNs
             << " ns/block). Spend plan S8's levers IN ORDER - 4 (centsToPitchRatioFast), then 5 "
                "(entropy OU control interval 32 -> 64) - and re-measure. If both are spent and it "
                "still misses, STOP AND REPORT it as a finding against SC-010. Do NOT raise "
                "kMorphBaselineNs and do NOT raise kMorphReferenceNsPerBlock.");
    }

    REQUIRE(measuredMorphNs <= kMorphBaselineNs * kRegressionFactor);

    // -------------------------------------------------------------------------
    // The targets clauses 2 and 3 feed the cloud, and the proof that they move
    // BOTH per-slot masks. Built outside every timed loop.
    // -------------------------------------------------------------------------

    const TargetTable table = buildTargetFrames();
    REQUIRE(table.count == HarmonicCloud::kMaxPartials);

    std::size_t minRatioDirty = HarmonicCloud::kMaxPartials;
    std::size_t minAmpDirty = HarmonicCloud::kMaxPartials;
    std::size_t totalRatioDirty = 0;
    std::size_t totalAmpDirty = 0;
    for (std::size_t k = 0; k < kFrameTableSize; ++k) {
        // Cyclic, exactly the sequence the timed loop supplies - including the
        // table wrap, which is a step backwards in the journey and dirties at
        // least as much as a forward step.
        const std::size_t previous = (k + kFrameTableSize - 1u) & (kFrameTableSize - 1u);
        const DirtyCounts counts = countDirtySlots(table.frames[previous], table.frames[k]);
        minRatioDirty = std::min(minRatioDirty, counts.ratioSlots);
        minAmpDirty = std::min(minAmpDirty, counts.ampSlots);
        totalRatioDirty += counts.ratioSlots;
        totalAmpDirty += counts.ampSlots;
    }

    WARN("SC-010 clause 2 target-table dirty coverage (both per-slot masks must move):\n"
         << "  ratio-dirty slots/chunk : min " << minRatioDirty << ", mean "
         << (static_cast<double>(totalRatioDirty) / static_cast<double>(kFrameTableSize)) << " of "
         << HarmonicCloud::kMaxPartials << "\n"
         << "  amp-dirty slots/chunk   : min " << minAmpDirty << ", mean "
         << (static_cast<double>(totalAmpDirty) / static_cast<double>(kFrameTableSize)) << " of "
         << HarmonicCloud::kMaxPartials);

    // Non-vacuity floors, deliberately well below the expected coverage: the
    // point is that BOTH masks light up on EVERY chunk, so a missing
    // `ampSlotDirty_ = 0` is paid for rather than hidden. A failure here is a
    // finding about the target table, not a threshold to lower.
    REQUIRE(minRatioDirty >= std::size_t{32});
    REQUIRE(minAmpDirty >= std::size_t{8});

    // -------------------------------------------------------------------------
    // CLAUSE 3 (part 1) - the cloud with NO target, in the FR-086 slice cadence.
    // -------------------------------------------------------------------------
    // Three separate HarmonicCloud instances, one per configuration: hasTarget_
    // is sticky (only clearSpectralTarget lowers it) and committedRatio_/
    // committedAmp_ carry over, so reusing one instance would make each
    // measurement depend on the one before it.

    HarmonicCloud cloudBaseline;
    configureCloud(cloudBaseline);

    const auto baselineBlock = [&]() noexcept {
        for (std::size_t offset = 0; offset < kBlockSize; offset += kChunkSamples) {
            cloudBaseline.processStereoBlock(left.data() + offset, right.data() + offset,
                                             kChunkSamples);
        }
        sink += static_cast<double>(left[0]) + static_cast<double>(right[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        baselineBlock();
    }

    // The envelopes must be sounding for the whole measurement, otherwise this
    // times the quiescent early-out (harmonic_cloud.h:851) and reports it as the
    // render cost.
    REQUIRE_FALSE(cloudBaseline.isQuiescent());
    REQUIRE(cloudBaseline.getActivePartialCount() == HarmonicCloud::kMaxPartials);
    REQUIRE_FALSE(cloudBaseline.hasSpectralTarget());

    const double measuredCloudBaselineNs = bestNsPerBlock(kTrials, kBlocksPerTrial, baselineBlock);

    // -------------------------------------------------------------------------
    // CLAUSE 3 (part 2) - the SAME target re-supplied every chunk. FR-085 lever
    // 1's whole-array skip is the only thing that can make this ~free.
    // -------------------------------------------------------------------------

    HarmonicCloud cloudUnchanged;
    configureCloud(cloudUnchanged);

    const TargetFrame& frozenFrame = table.frames[0];

    const auto unchangedBlock = [&]() noexcept {
        for (std::size_t offset = 0; offset < kBlockSize; offset += kChunkSamples) {
            cloudUnchanged.setSpectralTarget(frozenFrame.ratios.data(),
                                             frozenFrame.amplitudes.data(), table.count);
            cloudUnchanged.processStereoBlock(left.data() + offset, right.data() + offset,
                                              kChunkSamples);
        }
        sink += static_cast<double>(left[0]) + static_cast<double>(right[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        unchangedBlock();
    }

    REQUIRE_FALSE(cloudUnchanged.isQuiescent());
    REQUIRE(cloudUnchanged.getActivePartialCount() == HarmonicCloud::kMaxPartials);
    REQUIRE(cloudUnchanged.hasSpectralTarget());

    const double measuredUnchangedTargetNs =
        bestNsPerBlock(kTrials, kBlocksPerTrial, unchangedBlock);

    // -------------------------------------------------------------------------
    // CLAUSE 2 - a DIFFERENT target every chunk, moving in ratio AND amplitude.
    // -------------------------------------------------------------------------

    HarmonicCloud cloudChanging;
    configureCloud(cloudChanging);

    std::size_t frameIndex = 0;

    const auto changingBlock = [&]() noexcept {
        for (std::size_t offset = 0; offset < kBlockSize; offset += kChunkSamples) {
            const TargetFrame& frame = table.frames[frameIndex];
            frameIndex = (frameIndex + 1u) & (kFrameTableSize - 1u);
            cloudChanging.setSpectralTarget(frame.ratios.data(), frame.amplitudes.data(),
                                            table.count);
            cloudChanging.processStereoBlock(left.data() + offset, right.data() + offset,
                                             kChunkSamples);
        }
        sink += static_cast<double>(left[0]) + static_cast<double>(right[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        changingBlock();
    }

    REQUIRE_FALSE(cloudChanging.isQuiescent());
    REQUIRE(cloudChanging.getActivePartialCount() == HarmonicCloud::kMaxPartials);
    REQUIRE(cloudChanging.hasSpectralTarget());

    const double measuredChangingTargetNs = bestNsPerBlock(kTrials, kBlocksPerTrial, changingBlock);

    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    // -------------------------------------------------------------------------
    // Report, then gate.
    // -------------------------------------------------------------------------

    const double changingPercentOfBudget = (measuredChangingTargetNs / kBlockBudgetNs) * 100.0;
    const double unchangedRatio = measuredUnchangedTargetNs / measuredCloudBaselineNs;

    WARN("SC-010 clauses 2 and 3 - HarmonicCloud in the FR-086 slice cadence (8 x 64\n"
         << "samples per 512-sample block), Phase 2 SC-007's static cloud configuration,\n"
         << "differing only in whether and how a spectral target is supplied.\n"
         << "  no target       : " << measuredCloudBaselineNs << " ns/block\n"
         << "  unchanged target: " << measuredUnchangedTargetNs << " ns/block  (ratio "
         << unchangedRatio << ", bound " << kUnchangedTargetRatioBound << ")\n"
         << "  changing target : " << measuredChangingTargetNs << " ns/block  ("
         << changingPercentOfBudget << " % of budget; Phase 2 envelope: <= 0.5 %)\n"
         << "  reference figure: " << kReferenceNsPerBlock << " ns/block\n"
         << "  checked-in base : " << kCloudChangingTargetBaselineNs << " ns/block  (gate: x"
         << kRegressionFactor << " = " << (kCloudChangingTargetBaselineNs * kRegressionFactor)
         << " ns/block)\n"
         << "  injection cost  : " << (measuredChangingTargetNs - measuredCloudBaselineNs)
         << " ns/block  -- REPORTED ONLY, NEVER A GATE: the difference of two sampled\n"
         << "                    minima is a biased, possibly negative statistic that a\n"
         << "                    multiplicative regression factor cannot bound "
            "(spec.md:1864-1868).");

    if (measuredChangingTargetNs > kMaxAdmissibleBaselineNsPerBlock) {
        WARN("SC-010 clause 2 OVER BUDGET: "
             << measuredChangingTargetNs << " ns/block exceeds the largest admissible baseline ("
             << kMaxAdmissibleBaselineNsPerBlock
             << " ns/block), i.e. the FR-080 injection has pushed the cloud outside Phase 2's "
                "shipped 0.5 %/voice envelope. Make the injection cheaper via FR-085's three "
                "levers (whole-array skip, identity branches, per-slot dirty test); only after all "
                "three are spent does RA-3 permit raising roadmap line 164 to the measured figure, "
                "recording the measurement and the spent levers. Never raise the baseline "
                "silently.");
    }

    // Clause 2: a measurement against a checked-in constant.
    REQUIRE(measuredChangingTargetNs <= kCloudChangingTargetBaselineNs * kRegressionFactor);

    // Clause 3: a measurement against ANOTHER MEASUREMENT FROM THE SAME RUN. This
    // is the only clause that exercises FR-085 lever 1 at all, and it is stated
    // this way on purpose - as two checked-in literals the relation would be a
    // compile-time comparison satisfiable by editing the literals, proving
    // nothing about whether the skip exists (spec.md:1820-1826).
    REQUIRE(measuredUnchangedTargetNs <= measuredCloudBaselineNs * kUnchangedTargetRatioBound);
}
