// ==============================================================================
// Layer 3: System Tests - HarmonicCloud, CPU budget (SC-007)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per specs/seraphis-phase2-harmonic-cloud/
//
// Reference: specs/seraphis-phase2-harmonic-cloud/spec.md   (SC-007)
//            specs/seraphis-phase2-harmonic-cloud/plan.md   (S6.1-S6.3, S7.4)
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. SC-007 therefore pins
//   the measurement basis to NANOSECONDS PER 512-SAMPLE BLOCK and gates only
//   against a CHECKED-IN BASELINE as a relative regression bound
//   (fail if > baseline x 1.5). The absolute "<= 0.5 % of the 512-samples-at-
//   48 kHz budget" figure (512/48000 ~= 10.667 ms -> ~53,333 ns/block) is
//   REPORTED, not asserted, because it is only binding on the reference machine.
//
// WHY TWO BASELINES:
//   plan S6.3 - the render path is not the operating point. Phase 7 wires all
//   five macros to life modulators and moves the fundamental under glide, so
//   every block also pays config-rate recompute. Measuring the budget in a
//   configuration the component will never be used in understates it, and
//   roadmap line 484 makes the budget a functional requirement. The
//   static_assert therefore lives on the AUTOMATED baseline; the static one is
//   recorded beside it for regression tracking only.
//
// BOTH CASES ARE "[.perf]":
//   Every CI leg excludes perf-tagged cases (.github/workflows/ci.yml:328, :574,
//   :951; valgrind-nightly.yml:202), so no job ever evaluates an absolute figure.
//   Run them explicitly:
//     build/windows-x64-release/bin/Release/dsp_systems_tests.exe "HarmonicCloud_CpuBudget*"
//
// NO ALLOCATION-TRACKING INCLUDES HERE: this TU must not pull in
// <allocation_operator_overrides.h> (duplicate-symbol link error against the
// single owner in this binary), and does not need it - SC-008 is covered in
// harmonic_cloud_test.cpp.
//
// No std::isnan / std::isinf / numeric_limits infinity anywhere: the macOS leg
// builds with -ffast-math, which folds them. Finiteness is checked on the
// IEEE-754 exponent field instead.
//
// FTZ/DAZ: dsp_test_main.cpp:13 calls enableFTZDAZ() before any case runs, so
// every figure below is measured with denormals flushed BY THE PROCESS - the
// same environment the audio thread runs in.
// ==============================================================================

#include <krate/dsp/systems/harmonic_cloud.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
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
// Measurement basis (SC-007)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// Relative regression bound applied to the checked-in baselines (SC-007).
constexpr double kRegressionFactor = 1.5;

/// SC-007's absolute reference: 0.5 % of that budget (~53,333 ns). REPORTED
/// only - see the header comment.
constexpr double kReferenceNsPerBlock = (512.0 / 48000.0) * 1e9 * 0.005;  // ~ 53,333 ns

/// The largest baseline the static_asserts below can accept (~35,556 ns; the
/// plan quotes it as 35,533). A measurement above this figure means the phase
/// is OVER BUDGET: the response is to reduce cost, NEVER to raise the baseline,
/// and never to renegotiate kRegressionFactor at implementation time.
///
/// plan S6.2's std::exp2(cents/1200) lever HAS NOW BEEN SPENT - it is what took
/// this component from over budget to inside it, together with two others. All
/// three are documented where they live, with their measurements:
///   - `detail::centsToDriftRatio`      (harmonic_cloud.h) - the drift ratio, 512
///     calls/block, now a bounded-domain degree-4 polynomial rather than
///     semitonesToRatio's std::pow;
///   - `detail::kHarmonicCloudLog2N`    (harmonic_cloud.h) - the three config-rate
///     power laws (FR-041, FR-061, FR-081) now cost one std::exp2 each instead of
///     a std::pow (and, for FR-061, a std::log2);
///   - `DriftLanes::cachedPowN`         (harmonic_cloud.h) - the drift smoother's
///     coeff^N, previously 32 redundant powf calls per block for one value.
/// Combined, MEASURED on the machine named under BASELINE PROVENANCE below and
/// under the same trial shape: 31,281-32,027 -> 20,641-23,154 ns/block static,
/// 33,257-34,184 -> 21,917-25,262 automated.
constexpr double kMaxAdmissibleBaselineNsPerBlock = kReferenceNsPerBlock / kRegressionFactor;

// =============================================================================
// BASELINE PROVENANCE (SC-007, spec.md "Baseline provenance")
// =============================================================================
//   Machine    : Windows 11 Pro 26200, 13th Gen Intel(R) Core(TM) i9-13900HX
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-25 x 500 blocks (see the trial-shape comment below)
//   Measured   : 2026-07-26
//
// Eight consecutive runs of this TU on that machine, ns per 512-sample block:
//
//   run       |  1     2     3     4     5     6     7     8    | min    max
//   static    | 22774 23154 22188 20641 21347 20729 20844 21383 | 20641  23154
//   automated | 25206 22527 24722 25262 23746 22998 22773 21917 | 21917  25262
//   quiescent | 10278 10256 10998 10113  9216 10277  9166 10334 |  9166  10998
//
// Each baseline below is the WORST (largest) of those best-of-25 figures rounded
// up - not the best - so a normal run has margin against the gate rather than
// sitting on it. Under sustained build load earlier in the same session the same
// code measured up to 26,810 static / 28,643 automated; both clear the gates.
// =============================================================================

/// Checked-in ns/block baseline for the STATIC configuration - regression
/// tracking only, no static_assert (plan S7.4). See BASELINE PROVENANCE above.
constexpr double kStaticBaselineNsPerBlock = 24000.0;

/// Checked-in ns/block baseline for the AUTOMATED configuration - THE GATE.
/// See BASELINE PROVENANCE above.
///
/// 26,000 x 1.5 = 39,000 ns/block, i.e. 0.366 % of the 512-at-48-kHz budget,
/// comfortably inside SC-007's 53,333 ns reference and therefore inside the
/// roadmap's 0.5 % headline even at the gate. The prescribed response to a
/// measurement above kMaxAdmissibleBaselineNsPerBlock is to reduce cost, NEVER to
/// raise this number - which is what was done to get here: the first honest
/// measurement of this component was 35,052 static / 37,002 automated, over the
/// 35,556 ceiling. See harmonic_cloud.h's `detail::centsToDriftRatio` and
/// `detail::kHarmonicCloudLog2N` comments and DriftLanes::cachedPowN for the three
/// levers and their measured effect.
constexpr double kAutomatedBaselineNsPerBlock = 26000.0;

static_assert(kAutomatedBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock,
              "baseline must be no weaker than the SC-007 reference figure");

/// The binding arithmetic of SC-007 spelled out a second way: the checked-in
/// baseline must itself be an admissible baseline. Equivalent to the assert above
/// by construction, but it is the form spec.md:778 states, and it fails with the
/// message that names the actual rule if someone edits kRegressionFactor instead.
static_assert(kAutomatedBaselineNsPerBlock <= kMaxAdmissibleBaselineNsPerBlock,
              "SC-007: a baseline above kMaxAdmissibleBaselineNsPerBlock means the phase is "
              "over budget - reduce cost, never raise the baseline");

/// The measured configuration in one compile-time statement: a 512-sample block
/// is exactly 8 control chunks, so both lane banks are read 8 x 128 = 1024 times
/// per block. If kControlChunkSamples ever moves, this measurement no longer
/// describes what the task specified and the TU stops compiling.
static_assert(kBlockSize % HarmonicCloud::kControlChunkSamples == 0,
              "block must be a whole number of control chunks");
static_assert(kBlockSize / HarmonicCloud::kControlChunkSamples == 8,
              "SC-007 measures 8 chunks per 512-sample block");
static_assert(HarmonicCloud::kMaxPartials == 64, "SC-007 measures the 64-partial cloud");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the
// real cost, which is what a regression bound wants. plan S6.1 measured its
// probes best-of-7 x 2000 blocks; the shape here is MANY SHORT trials instead,
// and the reason is the dev machine's CPU.
//
// WHY 25 x 500 AND NOT 7 x 2000 (same total work, ~12.5 k vs 14 k blocks):
//   the reference machine is a 13th Gen Intel Core i9-13900HX - a HYBRID part
//   with performance and efficiency cores. The dominant noise source is not OS
//   scheduling jitter smeared across a trial, it is the whole trial being
//   migrated onto an E-core, which is a ~20 % step in ns/block that best-of-N
//   cannot reject when N is small and each trial is long enough (2000 blocks
//   ~= 60 ms) to be migrated. MEASURED with the 7 x 2000 shape, five
//   consecutive runs of the automated case: 31049, 29888, 29365, 35455, 29603
//   ns/block - a 20 % spread on identical code, which is more than the whole
//   optimisation this baseline is being recorded for.
//   Shortening each trial to 500 blocks (~15 ms) and taking 25 of them makes it
//   very likely that at least one trial runs start-to-finish on a boosted
//   P-core, which is the figure the baseline wants to describe.
// Pinning affinity was tried and REJECTED: it is not portable, and on this part
// it selects a core by index without knowing its type - affinity mask 4 (CPU 2)
// measured the automated case at 39189-40132 ns/block, worse than no pinning.
//
// 500 blocks ~= 5.3 s of audio, all of it in envelope Hold (the gate stays on),
// so every trial still measures identical steady-state work.

constexpr int kTrials = 25;
constexpr int kWarmupBlocks = 400;
constexpr int kBlocksPerTrial = 500;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// One automation frame: the Phase-7 call pattern, one step per 512-block.
struct MacroStep {
    float fundamentalHz;
    float richness;
    float inharmonicity;
    float tiltDb;
    float mutation;
    float gravity;
};

constexpr std::size_t kStepTableSize = 64;

/// Pre-built so the timed loop pays for the component's setters and NOT for the
/// transcendentals that generate the trajectory.
///
/// Every consecutive pair differs in every field, so no setter's `v == shadow_`
/// no-op guard ever short-circuits - the point of this case is to pay the guard
/// AND the deferred recompute it schedules, not to skip them.
///
/// Ranges are deliberate:
///  - fundamental: 220 Hz +- 1 %, a glide. Well inside FR-013's one-semitone
///    pitch-jump threshold, so the 3 ms crossfade is not armed on every block
///    (which would measure a transient, not the operating point).
///  - richness: [0.999, 1.0]. N(r) = round(64^r) stays pinned at 64 across the
///    whole range (64^0.999 = 63.73 -> 64; the round-to-63 boundary sits at
///    r = ln(63.5)/ln(64) = 0.99811), so the kernel really does run 64 partials
///    for the whole measurement while the setter still dirties both flags.
///  - inharmonicity [0, 0.1], tilt +-6 dB/oct, mutation [0.99, 1.0],
///    gravity +-0.5: all inside their clamps, so no setter early-returns.
[[nodiscard]] std::array<MacroStep, kStepTableSize> buildStepTable() {
    std::array<MacroStep, kStepTableSize> table{};
    for (std::size_t k = 0; k < kStepTableSize; ++k) {
        const double phase =
            6.283185307179586 * static_cast<double>(k) / static_cast<double>(kStepTableSize);
        const double s = std::sin(phase);
        table[k] = MacroStep{
            .fundamentalHz = static_cast<float>(220.0 * (1.0 + 0.01 * s)),
            .richness = static_cast<float>(0.9995 + 0.0005 * s),
            .inharmonicity = static_cast<float>(0.05 + 0.05 * s),
            .tiltDb = static_cast<float>(6.0 * s),
            .mutation = static_cast<float>(0.995 + 0.005 * s),
            .gravity = static_cast<float>(0.5 * s),
        };
    }
    return table;
}

/// The SC-007 configuration: 64 partials, Mutation 1.0, drift depth at its
/// maximum, both lane banks live, gate on.
///
/// The mutation bank free-runs unconditionally (plan S4.5), and the detune bank
/// is advanced on every render regardless of depth - what kMaxDriftCents buys is
/// that the detune is actually APPLIED, i.e. the per-partial ratio path is on
/// the measured hot path rather than multiplying by a constant 1.
void configureCloud(HarmonicCloud& cloud) {
    cloud.prepare(kSr48);
    cloud.setRichness(1.0f);  // N(1) = 64 (also the FR-003 default; explicit here)
    cloud.setMutation(1.0f);
    cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    cloud.setStereoSpread(1.0f);
    cloud.noteOn();
}

/// The Phase-7 call pattern: all five macros AND the fundamental, once per
/// 512-block - the cadence SC-006 already uses.
void applyStep(HarmonicCloud& cloud, const MacroStep& step) noexcept {
    cloud.setFundamentalHz(step.fundamentalHz);
    cloud.setRichness(step.richness);
    cloud.setInharmonicity(step.inharmonicity);
    cloud.setSpectralTiltDb(step.tiltDb);
    cloud.setMutation(step.mutation);
    cloud.setSpectralGravity(step.gravity);
}

/// Best-of-N driver. `runBlock` performs exactly one 512-sample block of work.
/// Taken by const reference, not by forwarding reference: it is INVOKED, many times,
/// never consumed, so there is nothing to forward.
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

}  // namespace

// =============================================================================
// SC-007 (static): 64 partials + both drift banks, no setter calls
// =============================================================================

TEST_CASE("HarmonicCloud_CpuBudget", "[.perf]") {
    HarmonicCloud cloud;
    configureCloud(cloud);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    double sink = 0.0;

    // Reading two samples per block is what stops the optimizer dead-coding the
    // render away; a real consumer reads the whole buffer, so this is not
    // artificial overhead.
    const auto renderBlock = [&]() noexcept {
        cloud.processStereoBlock(left.data(), right.data(), kBlockSize);
        sink += static_cast<double>(left[0]) + static_cast<double>(right[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        renderBlock();
    }

    // The envelopes must be sounding for the whole measurement, otherwise this
    // times the S4.1 quiescent early-out and reports it as the render cost.
    REQUIRE_FALSE(cloud.isQuiescent());
    REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

    const double measuredNsPerBlock = bestNsPerBlock(kTrials, kBlocksPerTrial, renderBlock);

    // Guards against the whole loop being optimized out (a zero-cost "pass").
    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    const double percentOfBudget = (measuredNsPerBlock / kBlockBudgetNs) * 100.0;

    WARN("SC-007 (static) HarmonicCloud: 64 partials, Mutation 1.0, drift depth max,\n"
         << "both lane banks live, 8 chunks/block, no setter calls.\n"
         << "  measured        : " << measuredNsPerBlock << " ns/block (512 samples @ 48 kHz)\n"
         << "  block budget    : " << kBlockBudgetNs << " ns\n"
         << "  % of budget     : " << percentOfBudget << " %  (roadmap headline: <= 0.5 %)\n"
         << "  reference figure: " << kReferenceNsPerBlock << " ns/block\n"
         << "  checked-in base : " << kStaticBaselineNsPerBlock << " ns/block  (gate: x"
         << kRegressionFactor << " = " << (kStaticBaselineNsPerBlock * kRegressionFactor)
         << " ns/block)");

    // Regression tracking only. SC-007's admissibility static_assert lives on
    // the automated case below, which is the configuration the component
    // actually runs in.
    REQUIRE(measuredNsPerBlock <= kStaticBaselineNsPerBlock * kRegressionFactor);
}

// =============================================================================
// SC-007 (automated): THE GATING CASE - same configuration plus the Phase-7
// call pattern, all five macros and the fundamental stepped once per block
// =============================================================================

TEST_CASE("HarmonicCloud_CpuBudgetUnderAutomation", "[.perf]") {
    const auto steps = buildStepTable();

    HarmonicCloud cloud;
    configureCloud(cloud);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    double sink = 0.0;
    std::size_t stepIndex = 0;

    const auto automatedBlock = [&]() noexcept {
        applyStep(cloud, steps[stepIndex]);
        stepIndex = (stepIndex + 1u) & (kStepTableSize - 1u);
        cloud.processStereoBlock(left.data(), right.data(), kBlockSize);
        sink += static_cast<double>(left[0]) + static_cast<double>(right[kBlockSize - 1]);
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        automatedBlock();
    }

    REQUIRE_FALSE(cloud.isQuiescent());
    REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

    const double measuredNsPerBlock = bestNsPerBlock(kTrials, kBlocksPerTrial, automatedBlock);

    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    const double percentOfBudget = (measuredNsPerBlock / kBlockBudgetNs) * 100.0;

    WARN("SC-007 (automated, GATING) HarmonicCloud: the static configuration plus the\n"
         << "Phase-7 call pattern - five macros + setFundamentalHz stepped once per block.\n"
         << "  measured        : " << measuredNsPerBlock << " ns/block (512 samples @ 48 kHz)\n"
         << "  block budget    : " << kBlockBudgetNs << " ns\n"
         << "  % of budget     : " << percentOfBudget << " %  (roadmap headline: <= 0.5 %)\n"
         << "  reference figure: " << kReferenceNsPerBlock << " ns/block\n"
         << "  checked-in base : " << kAutomatedBaselineNsPerBlock << " ns/block  (gate: x"
         << kRegressionFactor << " = " << (kAutomatedBaselineNsPerBlock * kRegressionFactor)
         << " ns/block)");

    // Over-budget report. NOT a gate (the enforced gate is the relative one
    // below): a measurement above kMaxAdmissibleBaselineNsPerBlock means no
    // honest baseline can be checked in, because baseline x 1.5 would exceed
    // the SC-007 reference figure. See that constant for the prescribed
    // response - reduce cost, never raise the baseline.
    if (measuredNsPerBlock > kMaxAdmissibleBaselineNsPerBlock) {
        WARN("SC-007 OVER BUDGET: "
             << measuredNsPerBlock << " ns/block exceeds the largest admissible baseline ("
             << kMaxAdmissibleBaselineNsPerBlock
             << " ns/block). Reduce cost (plan S6.2's std::exp2(cents/1200) lever) and "
                "re-measure - do NOT raise kAutomatedBaselineNsPerBlock.");
    }

    // The binding assertion: a relative regression bound against the checked-in
    // baseline. The 0.5 %-of-budget figure above is reported, not asserted, on
    // non-reference machines.
    REQUIRE(measuredNsPerBlock <= kAutomatedBaselineNsPerBlock * kRegressionFactor);

    // -------------------------------------------------------------------------
    // Third measurement, WARN only, no perf assertion: the QUIESCENT cost.
    // -------------------------------------------------------------------------
    // Proves plan S4.1's early-out is wired - a released voice that Phase 7 has
    // not yet retired must not keep paying for 64 partials. The automation keeps
    // running (a released voice is still modulated), so this figure is the same
    // driver as above minus the sounding work.

    cloud.setEnvelopeOffsetSpread(0.0f);  // no per-partial offsets to wait out
    cloud.setDecayTimeSec(HarmonicCloud::kMinDecaySec);
    cloud.noteOff();

    // 400 blocks ~= 4.3 s, far beyond the 50 ms decay plus the 2 ms amplitude
    // smoother, so the bound is a safety net and not the expected exit.
    constexpr int kMaxDecayBlocks = 400;
    for (int i = 0; i < kMaxDecayBlocks && !cloud.isQuiescent(); ++i) {
        cloud.processStereoBlock(left.data(), right.data(), kBlockSize);
    }

    // Precondition, not a perf assertion: reporting a "quiescent cost" measured
    // on a sounding cloud would be a lie.
    REQUIRE(cloud.isQuiescent());

    const double quiescentNsPerBlock = bestNsPerBlock(kTrials, kBlocksPerTrial, automatedBlock);

    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    WARN("SC-007 (quiescent, plan S4.1 early-out) HarmonicCloud:\n"
         << "  sounding        : " << measuredNsPerBlock << " ns/block\n"
         << "  quiescent       : " << quiescentNsPerBlock << " ns/block\n"
         << "  ratio           : " << (quiescentNsPerBlock / measuredNsPerBlock)
         << "  (expected: materially below 1 - the early-out advances both lane "
            "banks and fills silence, nothing else)");
}
