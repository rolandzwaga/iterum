// ==============================================================================
// Layer 2: Processor Tests - Vorago Phase 1 control-rate cost (SC-014)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md   (SC-014)
// Plan:  specs/vorago-phase1-events-modulation/plan.md
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T011)
//
// Workload: four PerlinNoiseSource (4 octaves each) + four SlowEventScheduler
//           + one Aizawa ChaosModSource, advanced together and READ at each
//           component's published poll-rate contract.
//
// WHY THE TAG IS "[.perf]" AND NOTHING ELSE:
//   The leading dot hides this case from the default run (copying
//   life_modulators_perf_test.cpp:147). The per-push CI filter is
//   ~[performance]~[perf]~[benchmark]~[!benchmark]~[long]
//   (.github/workflows/ci.yml:366,638,1063) and Catch2 tag exclusion is
//   EXACT-MATCH, so an invented tag would enrol a hard wall-clock REQUIRE in
//   the per-push lane on all three shared-runner OS legs. SC-014 is a
//   DEVELOPER-RUN gate; the part CI enforces continuously is the compiled
//   static_assert below.
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. The measurement
//   basis is therefore NANOSECONDS PER 512-SAMPLE BLOCK, gated against a
//   checked-in baseline as a relative regression bound (fail if > baseline x
//   1.5). The static_assert pins that gate underneath SC-014's absolute
//   reference figure so the bound can never become self-referential
//   (life_modulators_perf_test.cpp:60-66 states the same reason).
//
// READ RATES ARE PART OF THE MEASUREMENT, NOT DECORATION:
//   PerlinNoiseSource and ChaosModSource write their value inside the
//   control-rate update and expose it through a cached read
//   (perlin_noise_source.h:321-323, chaos_mod_source.h:104-106), so a consumer
//   reads them ONCE PER BLOCK. SlowEventScheduler caches nothing: its
//   getCurrentValue() evaluates the envelope polynomial on every call
//   (slow_event_scheduler.h:331-341 -> envelopeAt() at :449-458) and its banner
//   (:51) obliges consumers to poll PER SAMPLE. Reading the schedulers once per
//   block would measure almost nothing for four of the nine objects, because
//   SlowEventScheduler::processBlock() (:304-321) is integer counter arithmetic
//   plus a rare drawCycle().
//
// NO ALLOCATION-TRACKING INCLUDES HERE:
//   brownian_drift_test.cpp:27-28 is the single owner of the global operator
//   new/delete replacements for the dsp_processors_tests binary; a second
//   include is a duplicate-symbol link error (documented at
//   life_modulators_perf_test.cpp:19-23). Allocation freedom is covered by
//   SC-013 in the per-component TUs.
//
// No std::isnan/isinf/isfinite anywhere: tools/lint-nonfinite-symbols.js bans
// them and -ffast-math folds them on the macOS leg. Finiteness goes through
// Krate::DSP::detail::isFinite (core/db_utils.h:118 float, :126 double).
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>  // ChaosModel::Aizawa
#include <krate/dsp/processors/chaos_mod_source.h>
#include <krate/dsp/processors/perlin_noise_source.h>
#include <krate/dsp/processors/slow_event_scheduler.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-014's absolute reference: 0.1 % of that budget (~10 667 ns/block).
constexpr double kReferenceNsPerBlock = kBlockBudgetNs * 0.001;

/// Checked-in ns/block baseline - the load-bearing regression gate.
///
/// MEASURED, NOT ASSUMED (T011). Dev machine: 13th Gen Intel Core i9-13900HX,
/// Windows 11, MSVC Release, build/windows-x64-release. Five consecutive
/// otherwise-idle runs of the CYCLE-INCLUSIVE figure the WARN below reports:
/// 6112.0, 6121.3, 5932.8, 5953.4, 6139.5 ns/block (spread 3.4 %). 6150 is the
/// top of that spread, so a clean run passes with ~50 % headroom and a genuine
/// regression of more than half is caught.
///
/// It also stays under the ceiling the static_assert below imposes
/// (kReferenceNsPerBlock / kRegressionFactor = 7111.1 ns/block), so the REQUIRE
/// at the bottom of this case remains no weaker than SC-014's own 0.1 %-of-
/// budget reference: 6150 x 1.5 = 9225 ns < 10 667 ns. The measured cost is
/// 0.058 % of the block budget against the 0.1 % SC-014 commits to.
///
/// A slower machine will read higher - this is a DEVELOPER-RUN gate on the
/// recorded machine, and a run competing with a parallel build measures 2-3x
/// this figure. Measure on an idle machine before believing a red result.
constexpr double kBaselineNsPerBlock = 6150.0;

/// Relative regression bound applied to the checked-in baseline (SC-014).
constexpr double kRegressionFactor = 1.5;

// Without this the gate is self-referential: any measured cost, however large,
// could be enshrined as "the baseline" and the row would pass forever
// (life_modulators_perf_test.cpp:60-66).
static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock,
              "baseline x factor must stay below the reference figure");

/// Which scheduler configuration the measured pass runs at.
enum class SchedulerConfig : std::uint8_t {
    /// FR-052 / FR-055 defaults: 20-90 s period, 5/3/8 s envelope. Over a
    /// ~21 s measured pass most samples sit in the idle stretch, so this is
    /// the IDLE-PATH figure.
    Defaults,
    /// Pinned to the fastest legal cadence: 1 s period, 0.05 s per segment.
    /// One full onset->release->idle cycle per 93.75 blocks, i.e. every
    /// ~100 measured blocks, so the drawn cycle, the state transitions and the
    /// envelope-evaluation path are all inside the timed region. This is the
    /// CYCLE-INCLUSIVE figure the regression gate applies to.
    PinnedCycle
};

/// The nine Vorago Phase 1 control-rate objects, advanced and read together.
struct VoragoPhase1Bundle {
    static constexpr std::size_t kPerlinCount = 4;
    static constexpr std::size_t kSchedulerCount = 4;

    std::array<PerlinNoiseSource, kPerlinCount> perlin{};
    std::array<SlowEventScheduler, kSchedulerCount> schedulers{};
    ChaosModSource aizawa;

    void prepareAll(double sampleRate, SchedulerConfig config) noexcept {
        for (std::size_t i = 0; i < kPerlinCount; ++i) {
            // Distinct seeds: four instances on one seed would share a branch
            // history and understate the real cost.
            perlin[i].setSeed(static_cast<std::uint32_t>(PerlinNoiseSource::kDefaultPerlinSeed) +
                              static_cast<std::uint32_t>(0x2545u * (i + 1u)));
            // 4 octaves = the expensive fBm path (SC-014's stated workload).
            perlin[i].setOctaves(PerlinNoiseSource::kMaxOctaves);
            perlin[i].setDepth(PerlinNoiseSource::kDefaultDepth);
            perlin[i].setRate(PerlinNoiseSource::kDefaultRate);
            perlin[i].prepare(sampleRate);
        }

        for (std::size_t i = 0; i < kSchedulerCount; ++i) {
            SlowEventScheduler& s = schedulers[i];
            s.setSeed(static_cast<std::uint32_t>(SlowEventScheduler::kDefaultEventSeed) +
                      static_cast<std::uint32_t>(0x1234u * (i + 1u)));
            // CONFIGURATION-ORDERING RULE (tasks.md, Group 4 preamble):
            // initState() draws the FR-067 pre-roll from the interval range AS
            // IT STANDS AT THAT MOMENT, and no later setter shortens it
            // (FR-066 latches). Every interval/segment setter therefore runs
            // BEFORE prepare(). Configuring after prepare() would leave the
            // scheduler idling for a pre-roll drawn from the DEFAULT 20-90 s
            // range - the whole measured pass would sit idle and the
            // cycle-inclusive figure would silently degenerate into a second
            // copy of the idle-path figure.
            if (config == SchedulerConfig::PinnedCycle) {
                s.setIntervalRange(SlowEventScheduler::kMinIntervalSeconds,
                                   SlowEventScheduler::kMinIntervalSeconds);
                s.setEnvelopeTimes(SlowEventScheduler::kMinSegmentSeconds,
                                   SlowEventScheduler::kMinSegmentSeconds,
                                   SlowEventScheduler::kMinSegmentSeconds);
            }
            s.prepare(sampleRate);
        }

        aizawa.prepare(sampleRate);
        aizawa.setModel(ChaosModel::Aizawa);
        aizawa.setSpeed(ChaosModSource::kDefaultSpeed);
    }

    /// Advance one block on all nine instances, then read every output at its
    /// contract rate. The reads exist for the reason stated at
    /// life_modulators_perf_test.cpp:124-130: without them the optimizer can
    /// dead-code the advance away and the row measures nothing. A real consumer
    /// reads at exactly these rates, so this is not artificial overhead.
    [[nodiscard]] double advanceAndReadBlock(std::size_t numSamples) noexcept {
        for (PerlinNoiseSource& p : perlin) {
            p.processBlock(numSamples);
        }
        for (SlowEventScheduler& s : schedulers) {
            s.processBlock(numSamples);
        }
        aizawa.processBlock(numSamples);

        double sum = 0.0;

        // Once per block: cached reads (perlin_noise_source.h:321-323,
        // chaos_mod_source.h:104-106).
        for (const PerlinNoiseSource& p : perlin) {
            sum += static_cast<double>(p.getCurrentValue());
        }
        sum += static_cast<double>(aizawa.getCurrentValue());

        // Per sample: SlowEventScheduler evaluates the envelope on every call
        // and caches nothing (slow_event_scheduler.h:51, :321-326).
        for (std::size_t n = 0; n < numSamples; ++n) {
            for (const SlowEventScheduler& s : schedulers) {
                sum += static_cast<double>(s.getCurrentValue());
            }
        }

        return sum;
    }
};

constexpr int kTrials = 5;
constexpr int kWarmupBlocks = 500;   // > the 1 s pinned pre-roll (93.75 blocks)
constexpr int kBlocksPerTrial = 2000;  // 21.3 s of wall clock = ~21 pinned cycles

/// Best-of-N ns/block for one configuration. The minimum is the least
/// OS-noise-contaminated estimate of the real cost, which is what a regression
/// bound wants. `sink` escapes the timing loop so nothing can be elided;
/// `periodOut` reports the last trial's latched period so the caller can prove
/// the configuration actually took effect (see the anti-vacuity guards).
[[nodiscard]] double measureNsPerBlock(SchedulerConfig config, double& sink,
                                       double& periodOut) {
    {
        VoragoPhase1Bundle warmup;
        warmup.prepareAll(kSr48, config);
        for (int i = 0; i < kWarmupBlocks; ++i) {
            sink += warmup.advanceAndReadBlock(kBlockSize);
        }
    }

    double bestNsPerBlock = std::numeric_limits<double>::max();
    double lastPeriod = 0.0;

    for (int trial = 0; trial < kTrials; ++trial) {
        VoragoPhase1Bundle bundle;
        bundle.prepareAll(kSr48, config);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            sink += bundle.advanceAndReadBlock(kBlockSize);
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        bestNsPerBlock =
            std::min(bestNsPerBlock, elapsedNs / static_cast<double>(kBlocksPerTrial));

        lastPeriod = bundle.schedulers[0].getPeriodSeconds();
    }

    periodOut = lastPeriod;
    return bestNsPerBlock;
}

}  // namespace

// =============================================================================
// SC-014: combined control-rate cost of the Vorago Phase 1 modulation set
// =============================================================================

TEST_CASE("VoragoPhase1_ControlRateCost", "[.perf]") {
    double sink = 0.0;

    double defaultPeriodSeconds = 0.0;
    const double idleNsPerBlock =
        measureNsPerBlock(SchedulerConfig::Defaults, sink, defaultPeriodSeconds);

    double pinnedPeriodSeconds = 0.0;
    const double cycleNsPerBlock =
        measureNsPerBlock(SchedulerConfig::PinnedCycle, sink, pinnedPeriodSeconds);

    // Guards against the whole loop being optimized out (a zero-cost "pass").
    REQUIRE(detail::isFinite(sink));

    // Anti-vacuity: the two passes really did run at different cadences, so the
    // cycle-inclusive figure is not a second copy of the idle-path figure. A
    // configuration applied on the wrong side of prepare() fails here.
    REQUIRE(defaultPeriodSeconds >= static_cast<double>(SlowEventScheduler::kDefaultMinInterval));
    REQUIRE(defaultPeriodSeconds <= static_cast<double>(SlowEventScheduler::kDefaultMaxInterval));
    REQUIRE(pinnedPeriodSeconds ==
            static_cast<double>(SlowEventScheduler::kMinIntervalSeconds));

    const double idlePercent = (idleNsPerBlock / kBlockBudgetNs) * 100.0;
    const double cyclePercent = (cycleNsPerBlock / kBlockBudgetNs) * 100.0;

    WARN("SC-014 Vorago Phase 1 control-rate cost"
         " (4x PerlinNoiseSource[4 oct] + 4x SlowEventScheduler + 1x Aizawa ChaosModSource):\n"
         << "  idle path (FR-052/FR-055 defaults) : " << idleNsPerBlock << " ns/block ("
         << idlePercent << " % of budget)\n"
         << "  cycle-inclusive (1 s / 0.05 s pin) : " << cycleNsPerBlock << " ns/block ("
         << cyclePercent << " % of budget)   <-- GATED\n"
         << "  block budget (512 @ 48 kHz)        : " << kBlockBudgetNs << " ns\n"
         << "  SC-014 reference (0.1 % of budget) : " << kReferenceNsPerBlock << " ns/block\n"
         << "  checked-in baseline                : " << kBaselineNsPerBlock << " ns/block (gate: x"
         << kRegressionFactor << " = " << (kBaselineNsPerBlock * kRegressionFactor)
         << " ns/block)");

    // The binding assertion applies to the CYCLE-INCLUSIVE figure: the idle
    // path skips drawCycle(), every state transition and the envelope
    // polynomial, so gating on it would let the expensive path regress freely.
    //
    // If this figure exceeds ~2000 ns/block for the four schedulers alone,
    // check that SlowEventScheduler::riseShape() (slow_event_scheduler.h:409)
    // really is the closed-form polynomial - a transcendental there is the one
    // change that makes SC-014 unattainable.
    REQUIRE(cycleNsPerBlock <= kBaselineNsPerBlock * kRegressionFactor);
}
