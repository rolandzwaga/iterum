// ==============================================================================
// Layer 2: Processor Tests - Vorago Phase 1 long-run resolution (SC-017)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md   (SC-017)
// Plan:  specs/vorago-phase1-events-modulation/plan.md   (section 5.2 SC-017 row,
//                                                         section 8 deviation 9)
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T012)
//
// WHAT THIS CASE EXISTS TO CATCH
// ------------------------------
// A `float` accumulator whose ULP grows past its per-step increment: the
// modulator silently freezes (RMS -> 0, zero crossings -> 0) or the scheduler
// stops firing, while every OTHER criterion in this phase still passes. No
// other criterion runs long enough to reach that regime.
//
// TAG: [long]. The render costs far more than 15 s and every assertion here is
// toolchain-INDEPENDENT (window-ratio tolerances and event counts, never a
// bit-exact float golden), which is exactly the CLAUDE.md rule for the tag:
// excluded from the per-push CI lane, run nightly on all three OS legs.
//
// MEASUREMENT WINDOWS ARE 1 h, NOT THE SPEC'S 60 s, AND THE RMS/ZCR CLAUSES
// APPLY TO THE PERLIN RENDERS ONLY (plan section 8, deviation 9). SC-017 as
// written in spec.md fails on a CORRECT implementation and is arithmetically
// undefined for the scheduler. The evidence, simulated against the real
// deriveStreamSeed/Xorshift32, the lattice math and the scheduler timeline,
// 8 h at 48 kHz over 8 seeds:
//
//   * Perlin at kDefaultRate with 60 s windows (as specified) is 6 lattice
//     cells and 11-21 zero crossings - both statistics are sampling noise. The
//     RMS deviation reaches 30.7 % (seed 0x9E37: 0.3311 -> 0.2294) and the ZCR
//     deviation 100 % (seed 0x51E7: 11 -> 22) against a 20 % bound: 5 of 8
//     seeds fail on one clause or the other.
//   * Perlin with 1 h windows (360 cells at kDefaultRate): worst over the same
//     8 seeds is RMS 5.3 %, ZCR 7.7 % - a 2.6x margin. At kMaxRate the same
//     windows give <= 0.6 % / 0.7 %. The 20 % thresholds are UNCHANGED; only
//     the window grew, and a frozen accumulator still drives both statistics
//     to 0.
//   * Scheduler with 60 s windows: the output is 0 except during an event, so
//     a 60 s window holds 0-3 zero crossings, and the FR-067 pre-roll (drawn
//     from 20-90 s) exceeds 60 s for 3 of 8 seeds (0x3039: 74.39 s, 0xBEEF:
//     84.66 s, 0xABCD: 70.96 s) - `rmsFirst` is then exactly 0 and the RMS
//     clause DIVIDES BY ZERO. Even at 900 s windows the ZCR swings 22 -> 6 and
//     12 -> 26. Both statistics are dropped for this source and replaced by
//     clauses that are not weaker: the hour-1 vs hour-8 EVENT COUNT (measured
//     deviation 1.5-6.9 % over 8 seeds against the unchanged 20 % bound, with
//     eventsHour1 >= 10 as the anti-vacuity guard) and a final-900 s LIVENESS
//     clause - exactly the "scheduler stops firing" failure SC-017 names.
//
// MEMORY: the four measurement windows are 5 400 000 control-step values each.
// They are accumulated in STREAMING form (sum of squares + sign-change count)
// rather than buffered - the RMS and zero-crossing-rate statistics are
// identical, and buffering all four would cost ~86 MB for no information. The
// 8 h trajectory itself is NEVER buffered; finiteness is checked inline, one
// value per advanced block, inside the render loop (clause (c)).
//
// Finiteness uses Krate::DSP::detail::isFinite (core/db_utils.h:118), never
// std::isnan/isinf/isfinite: tools/lint-nonfinite-symbols.js bans them and
// -ffast-math folds them on the macOS leg.
// ==============================================================================

#include <krate/dsp/processors/perlin_noise_source.h>
#include <krate/dsp/processors/slow_event_scheduler.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_all.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace Krate::DSP;

namespace {

constexpr double kSampleRate = 48000.0;

/// 48 000 Hz x 3600 s.
constexpr std::uint64_t kSamplesPerHour = 172'800'000ULL;
/// The full SC-017 soak: 8 h of wall clock.
constexpr std::uint64_t kTotalSamples = 8ULL * kSamplesPerHour;
/// Accelerated bulk block (the render is far too long for per-sample advance).
constexpr std::uint64_t kBulkBlock = 4096ULL;

/// The two measurement windows are captured at CONTROL-step granularity so the
/// statistics see every value the source can produce; 172 800 000 / 32.
constexpr std::uint64_t kControlStepsPerHour =
    kSamplesPerHour / static_cast<std::uint64_t>(PerlinNoiseSource::kControlRateInterval);

/// Hours 2-7 are advanced in bulk blocks - nothing is measured there beyond
/// finiteness, so there is no reason to pay for control-step granularity.
constexpr std::uint64_t kMiddleBlocks = (kTotalSamples - 2ULL * kSamplesPerHour) / kBulkBlock;

/// Whole-number block counts: an off-by-a-block window would silently shift the
/// comparison the ratio clauses depend on.
static_assert(kSamplesPerHour % static_cast<std::uint64_t>(PerlinNoiseSource::kControlRateInterval) == 0ULL,
              "one hour must be a whole number of Perlin control steps");
static_assert((kTotalSamples - 2ULL * kSamplesPerHour) % kBulkBlock == 0ULL,
              "the six middle hours must be a whole number of bulk blocks");
static_assert(kControlStepsPerHour == 5'400'000ULL, "1 h window == 5 400 000 control steps");
static_assert(kMiddleBlocks == 253'125ULL, "6 h middle span == 253 125 bulk blocks");

/// Streaming RMS + zero-crossing-rate accumulator for one 1 h window.
struct WindowStats {
    double sumSquares = 0.0;
    std::uint64_t count = 0;
    std::uint64_t signChanges = 0;
    float previous = 0.0f;
    bool havePrevious = false;

    void add(float value) noexcept {
        const auto asDouble = static_cast<double>(value);
        sumSquares += asDouble * asDouble;
        if (havePrevious) {
            const bool wasNegative = previous < 0.0f;
            const bool isNegative = value < 0.0f;
            if (wasNegative != isNegative) {
                ++signChanges;
            }
        }
        previous = value;
        havePrevious = true;
        ++count;
    }

    [[nodiscard]] double rms() const noexcept {
        return count == 0ULL ? 0.0
                             : std::sqrt(sumSquares / static_cast<double>(count));
    }

    /// Sign changes per adjacent pair - a RATE, so the two windows are
    /// comparable even if their point counts ever differ.
    [[nodiscard]] double zeroCrossingRate() const noexcept {
        return count < 2ULL ? 0.0
                            : static_cast<double>(signChanges) /
                                  static_cast<double>(count - 1ULL);
    }
};

struct PerlinLongRun {
    WindowStats firstHour{};
    WindowStats lastHour{};
    std::uint64_t nonFinite = 0;
};

/// Render 8 h of PerlinNoiseSource at `rate`, 4 octaves, depth 1, capturing the
/// first and last hour at control-step granularity. Nothing but the two
/// accumulators survives the call.
[[nodiscard]] PerlinLongRun renderPerlinEightHours(float rate) {
    PerlinNoiseSource source;
    source.setSeed(PerlinNoiseSource::kDefaultPerlinSeed);
    source.setRate(rate);
    source.setOctaves(PerlinNoiseSource::kMaxOctaves);
    source.setDepth(1.0f);
    source.prepare(kSampleRate);

    PerlinLongRun result{};

    // --- Hour 1: one capture per control step -------------------------------
    for (std::uint64_t step = 0ULL; step < kControlStepsPerHour; ++step) {
        source.processBlock(PerlinNoiseSource::kControlRateInterval);
        const float value = source.getCurrentValue();
        if (!detail::isFinite(value)) {
            ++result.nonFinite;
        }
        result.firstHour.add(value);
    }

    // --- Hours 2-7: bulk advance, finiteness only (clause (c)) --------------
    for (std::uint64_t block = 0ULL; block < kMiddleBlocks; ++block) {
        source.processBlock(static_cast<std::size_t>(kBulkBlock));
        if (!detail::isFinite(source.getCurrentValue())) {
            ++result.nonFinite;
        }
    }

    // --- Hour 8: one capture per control step -------------------------------
    for (std::uint64_t step = 0ULL; step < kControlStepsPerHour; ++step) {
        source.processBlock(PerlinNoiseSource::kControlRateInterval);
        const float value = source.getCurrentValue();
        if (!detail::isFinite(value)) {
            ++result.nonFinite;
        }
        result.lastHour.add(value);
    }

    return result;
}

/// (a) RMS and (b) zero-crossing RATE must agree between hour 1 and hour 8 to
/// within 20 %. The two guards below are not decoration: without them a frozen
/// source would divide by zero instead of failing the ratio.
void checkPerlinWindows(const PerlinLongRun& run, const char* label) {
    INFO("PerlinNoiseSource long run: " << label);

    REQUIRE(run.nonFinite == 0ULL);
    REQUIRE(run.firstHour.count == kControlStepsPerHour);
    REQUIRE(run.lastHour.count == kControlStepsPerHour);

    const double rmsFirst = run.firstHour.rms();
    const double rmsLast = run.lastHour.rms();
    const double zcrFirst = run.firstHour.zeroCrossingRate();
    const double zcrLast = run.lastHour.zeroCrossingRate();

    WARN(label << ": rms h1=" << rmsFirst << " h8=" << rmsLast
               << "  zcr h1=" << zcrFirst << " h8=" << zcrLast);

    // Anti-vacuity: a frozen accumulator drives both statistics to exactly 0,
    // and a 0/0 ratio would otherwise be NaN rather than a failure.
    REQUIRE(rmsFirst > 0.0);
    REQUIRE(zcrFirst > 0.0);

    // (a) SC-017 RMS clause. Measured worst case over 8 seeds: 5.3 % at
    //     kDefaultRate, <= 0.6 % at kMaxRate.
    REQUIRE(std::abs(rmsLast / rmsFirst - 1.0) <= 0.20);

    // (b) SC-017 zero-crossing clause, same 20 % bound. Measured worst case
    //     over 8 seeds: 7.7 % at kDefaultRate, <= 0.7 % at kMaxRate.
    REQUIRE(std::abs(zcrLast / zcrFirst - 1.0) <= 0.20);
}

}  // namespace

TEST_CASE("VoragoPhase1_LongRunResolution", "[processors][vorago][long]") {
    // -------------------------------------------------------------------------
    // PerlinNoiseSource - the slowest configuration first (kDefaultRate is where
    // a float position accumulator would starve first), then the fastest.
    // -------------------------------------------------------------------------
    checkPerlinWindows(renderPerlinEightHours(PerlinNoiseSource::kDefaultRate),
                       "kDefaultRate (0.1 cells/s), 4 octaves");
    checkPerlinWindows(renderPerlinEightHours(PerlinNoiseSource::kMaxRate),
                       "kMaxRate (5 cells/s), 4 octaves");

    // -------------------------------------------------------------------------
    // SlowEventScheduler at the FR-052/FR-055 defaults (20-90 s interval,
    // 5/3/8 s envelope, kDefaultEventSeed). No configuration call is made at
    // all, so the Group 4 configuration-ordering rule holds trivially.
    //
    // NO RMS OR ZCR CLAUSE HERE - see the banner. What replaces them is the
    // event count per hour (the direct FR-007 observable), a final-period range
    // check, and a liveness check over the final 900 s.
    // -------------------------------------------------------------------------
    {
        SlowEventScheduler scheduler;
        scheduler.prepare(kSampleRate);

        constexpr std::uint64_t kSchedulerBlocks = kTotalSamples / kBulkBlock;
        static_assert(kSchedulerBlocks == 337'500ULL, "8 h == 337 500 bulk blocks");

        // Hour 8 starts once seven hours have elapsed.
        constexpr std::uint64_t kHour7End = 7ULL * kSamplesPerHour;
        // Final 900 s: ~16 events at the 55 s mean of the uniform 20-90 s draw.
        constexpr std::uint64_t kLast900Start = kTotalSamples - 900ULL * 48'000ULL;

        std::uint64_t elapsedSamples = 0ULL;
        std::uint64_t eventsHour1 = 0ULL;
        std::uint64_t eventsHour8 = 0ULL;
        std::uint64_t nonFinite = 0ULL;
        std::uint64_t last900Count = 0ULL;
        double last900SumSquares = 0.0;
        bool wasActive = false;

        for (std::uint64_t block = 0ULL; block < kSchedulerBlocks; ++block) {
            scheduler.processBlock(static_cast<std::size_t>(kBulkBlock));
            elapsedSamples += kBulkBlock;

            const float value = scheduler.getCurrentValue();
            if (!detail::isFinite(value)) {
                ++nonFinite;  // clause (c), inline - nothing is buffered
            }

            // Onset = rising edge of isEventActive(). One event runs 16 s at the
            // defaults (5 + 3 + 8, fit scale 1 against the 20 s minimum period),
            // ~188 blocks, so an 85.3 ms sampling grid cannot miss one.
            const bool isActive = scheduler.isEventActive();
            if (isActive && !wasActive) {
                if (elapsedSamples <= kSamplesPerHour) {
                    ++eventsHour1;
                } else if (elapsedSamples > kHour7End) {
                    ++eventsHour8;
                }
            }
            wasActive = isActive;

            if (elapsedSamples > kLast900Start) {
                const auto asDouble = static_cast<double>(value);
                last900SumSquares += asDouble * asDouble;
                ++last900Count;
            }
        }

        const double rmsLast900s =
            last900Count == 0ULL
                ? 0.0
                : std::sqrt(last900SumSquares / static_cast<double>(last900Count));
        const double finalPeriodSeconds = scheduler.getPeriodSeconds();

        WARN("SlowEventScheduler long run: events h1=" << eventsHour1
             << " h8=" << eventsHour8 << "  rmsLast900s=" << rmsLast900s
             << "  finalPeriod=" << finalPeriodSeconds << " s");

        // (c) finiteness over the whole 8 h.
        REQUIRE(nonFinite == 0ULL);

        // (d) anti-vacuity guard, then the event-count clause. At the 55 s mean
        //     an hour holds ~65 events; measured hour-1 vs hour-8 deviation is
        //     1.5-6.9 % over 8 seeds against this 20 % bound.
        REQUIRE(eventsHour1 >= 10ULL);
        REQUIRE(std::abs(static_cast<double>(eventsHour8) /
                             static_cast<double>(eventsHour1) -
                         1.0) <= 0.20);

        // The period still being counted down after 8 h must remain inside the
        // FR-052 default draw range - a drifting or saturating period accumulator
        // walks out of it.
        REQUIRE(finalPeriodSeconds >= 20.0);
        REQUIRE(finalPeriodSeconds <= 90.0);

        // Liveness: exactly the "scheduler stops firing" failure SC-017 names.
        REQUIRE(last900Count > 0ULL);
        REQUIRE(rmsLast900s > 0.0);
    }
}
