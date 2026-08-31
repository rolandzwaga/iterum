// ==============================================================================
// Layer 2: Processor Tests - SlowEventScheduler (Vorago Phase 1)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md
// Plan:  specs/vorago-phase1-events-modulation/plan.md
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T009)
//
// Covers: FR-001, FR-002, FR-006, FR-051..FR-067 and SC-008..SC-013, SC-018.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   This TU includes <allocation_detector.h> ONLY. brownian_drift_test.cpp:27-28
//   is the single owner of <allocation_operator_overrides.h> in
//   dsp_processors_tests; a second include is a duplicate-symbol link error
//   (documented at life_modulators_perf_test.cpp:19-23).
//
// NOTE ON FINITENESS:
//   Krate::DSP::detail::isFinite (db_utils.h:118 float / :126 double) is used
//   everywhere. std::isnan/isinf/isfinite are BANNED by
//   tools/lint-nonfinite-symbols.js and are folded away by -ffast-math on the
//   macOS leg. Non-finite *inputs* for the setter-clamp clauses are built from
//   IEEE-754 bit patterns through a volatile sink, never from
//   std::numeric_limits (which fast-math folds to finite garbage).
//
// ------------------------------------------------------------------------------
// CONFIGURATION-ORDERING RULE - binding on every case in this file
// ------------------------------------------------------------------------------
//   initState() draws the FR-067 pre-roll period from minIntervalSeconds_ /
//   maxIntervalSeconds_ AS THEY STAND AT THAT MOMENT. All interval-range
//   configuration must therefore be applied BEFORE prepare() (or reset() must be
//   called after it). Configuring after prepare() leaves the scheduler idling for
//   a pre-roll drawn from the PREVIOUS range, and no later setter shortens it
//   (FR-066 latches). Getting this wrong silently empties SC-009(i), SC-013 and
//   SC-014: SC-013's tracked window holds 45 rising edges with the correct
//   ordering and 1 with the wrong one, against a ">= 20" guard.
//
//   Measured pre-rolls at the 20-90 s defaults: kDefaultEventSeed (0x51E7) ->
//   41.24 s (u = 0.303378), 0x1 -> 20.00 s, 0x3039 -> 74.39 s, 0xBEEF -> 84.66 s,
//   0xABCD -> 70.96 s.
//
//   Every helper below applies the whole configuration and then calls prepare(),
//   so the rule is satisfied by construction.
// ------------------------------------------------------------------------------
//
// NOTE ON THRESHOLDS:
//   Every numeric bound is a MEASURED tolerance or an analytic closed form
//   evaluated in the test. There is no checked-in float digest: the only
//   exact-equality comparisons are same-binary/same-run determinism comparisons
//   (SC-010), i.e. one build against itself.
//
// NOTE ON ASSERTION DENSITY:
//   The long corner renders sweep ~1e9 control steps. A REQUIRE per observation
//   would make Catch2's assertion bookkeeping, not the DSP, the run time. The
//   per-sample clauses are accumulated into worst-case reductions
//   (maxAbs / anyNonFinite / anyBadTarget) inside the render loop and asserted
//   once per corner. The semantics are identical: the reduction fails iff at
//   least one observation violates the clause.
// ==============================================================================

#include <krate/dsp/processors/slow_event_scheduler.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr double kSr44 = 44100.0;
constexpr double kSr96 = 96000.0;

/// One control step, as a block size. Kept as size_t so processBlock() takes it
/// without a conversion warning regardless of the class constant's own type.
constexpr std::size_t kCtl = static_cast<std::size_t>(SlowEventScheduler::kControlRateInterval);

/// The 32 seeds used by every multi-seed clause in this TU.
/// All 32 values are DISTINCT (0x3039 == 12345 decimal, so the hex spelling is
/// deliberately absent): the "different seeds => different streams" clause of
/// SC-010 compares all 496 pairs and a duplicate would fail it.
constexpr std::array<std::uint32_t, 32> kSeeds32 = {
    1u,      2u,      3u,      7u,      11u,        13u,     17u,     19u,
    23u,     29u,     31u,     37u,     0x51E7u,    0x9E37u, 0xBEEFu, 0xABCDu,
    0x2468u, 12345u,  54321u,  424242u, 987654321u, 0xF00Du, 0xC0DEu, 0xDEADu,
    0xFACEu, 0xB0BAu, 0x1234u, 0x7FFFu, 0x8001u,    0xA5A5u, 0x5A5Au, 0x0F0Fu};

/// The 8 seeds used by the corner-grid clauses (a strict prefix of kSeeds32 so
/// the two grids stay comparable).
constexpr std::array<std::uint32_t, 8> kSeeds8 = {1u, 2u, 3u, 7u, 11u, 13u, 17u, 19u};

// -----------------------------------------------------------------------------
// Non-finite float construction that survives -ffast-math
// -----------------------------------------------------------------------------

/// Build a float from an IEEE-754 bit pattern through a volatile sink. A plain
/// std::numeric_limits<float>::infinity()/quiet_NaN() is constant-folded to
/// finite garbage under the macOS leg's -ffast-math.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t local = sink;
    float value = 0.0f;
    std::memcpy(&value, &local, sizeof(value));
    return value;
}

[[nodiscard]] float qNaN() noexcept { return floatFromBits(0x7FC00000u); }
[[nodiscard]] float posInf() noexcept { return floatFromBits(0x7F800000u); }
[[nodiscard]] float negInf() noexcept { return floatFromBits(0xFF800000u); }

// -----------------------------------------------------------------------------
// Configuration helper - ALWAYS configure, THEN prepare (ordering rule above)
// -----------------------------------------------------------------------------

struct SchedulerConfig {
    std::uint32_t seed = SlowEventScheduler::kDefaultEventSeed;
    float minInterval = SlowEventScheduler::kDefaultMinInterval;
    float maxInterval = SlowEventScheduler::kDefaultMaxInterval;
    float attack = SlowEventScheduler::kDefaultAttack;
    float hold = SlowEventScheduler::kDefaultHold;
    float release = SlowEventScheduler::kDefaultRelease;
    float minDepth = SlowEventScheduler::kDefaultMinDepth;
    float maxDepth = SlowEventScheduler::kDefaultMaxDepth;
    float bipolar = SlowEventScheduler::kDefaultBipolarProbability;
    std::uint8_t targets = static_cast<std::uint8_t>(SlowEventScheduler::kDefaultTargetCount);
};

/// Apply the whole configuration and then prepare(). The order is load-bearing:
/// see the CONFIGURATION-ORDERING RULE banner at the top of this file.
void applyAndPrepare(SlowEventScheduler& s, const SchedulerConfig& cfg, double sampleRate) noexcept {
    s.setSeed(cfg.seed);
    s.setIntervalRange(cfg.minInterval, cfg.maxInterval);
    s.setEnvelopeTimes(cfg.attack, cfg.hold, cfg.release);
    s.setDepthRange(cfg.minDepth, cfg.maxDepth);
    s.setBipolarProbability(cfg.bipolar);
    s.setTargetCount(cfg.targets);
    s.prepare(sampleRate);
}

/// The FR-055 fit rule, replicated here ONLY to choose SC-009's measurement grid
/// (a grid choice is not an assertion). The component's own numbers are asserted
/// against these values inside SlowEventScheduler_EnvelopeC1AtJoins.
[[nodiscard]] double fitScale(double attack, double hold, double release,
                              double minIntervalSeconds) noexcept {
    const double total = attack + hold + release;
    if (total <= 0.0) return 1.0;
    return std::min(1.0, minIntervalSeconds / total);
}

// -----------------------------------------------------------------------------
// Onset collection at control-step granularity
// -----------------------------------------------------------------------------

struct Onset {
    std::size_t sampleIndex = 0;   ///< absolute sample index of the detection point
    double periodSeconds = 0.0;    ///< getPeriodSeconds() read at the onset
};

/// Advance `totalControlSteps` control steps with processBlock(kCtl), recording
/// one Onset per isEventActive() rising edge. Every event lasts at least
/// 3 * kMinSegmentSeconds = 0.15 s = 7200 samples at 48 kHz, i.e. >> one control
/// step, so no rising edge can be stepped over.
[[nodiscard]] std::vector<Onset> collectOnsets(SlowEventScheduler& s,
                                               std::size_t totalControlSteps,
                                               std::size_t maxOnsets) {
    std::vector<Onset> onsets;
    bool prevActive = s.isEventActive();
    for (std::size_t step = 0; step < totalControlSteps; ++step) {
        s.processBlock(kCtl);
        const bool active = s.isEventActive();
        if (active && !prevActive) {
            onsets.push_back(Onset{(step + 1) * kCtl, static_cast<double>(s.getPeriodSeconds())});
            if (onsets.size() >= maxOnsets) return onsets;
        }
        prevActive = active;
    }
    return onsets;
}

/// Number of control steps needed to cover `seconds` of wall clock.
[[nodiscard]] std::size_t controlStepsFor(double seconds, double sampleRate) noexcept {
    return static_cast<std::size_t>(seconds * sampleRate / static_cast<double>(kCtl)) + 1u;
}

/// Advance PER SAMPLE until the next isEventActive() rising edge, mirroring every
/// advance on `mirror` when one is supplied (lockstep comparisons). Returns false
/// if `maxSamples` was exhausted first, so the caller asserts the bound ONCE
/// rather than once per sample (see the assertion-density note).
[[nodiscard]] bool advanceToOnsetPerSample(SlowEventScheduler& s, std::size_t maxSamples,
                                           SlowEventScheduler* mirror = nullptr) noexcept {
    bool prevActive = s.isEventActive();
    for (std::size_t i = 0; i < maxSamples; ++i) {
        s.process();
        if (mirror != nullptr) mirror->process();
        const bool active = s.isEventActive();
        if (active && !prevActive) return true;
        prevActive = active;
    }
    return false;
}

/// Same, driven at control-step granularity.
[[nodiscard]] bool advanceToOnsetControlRate(SlowEventScheduler& s, std::size_t maxSteps) noexcept {
    bool prevActive = s.isEventActive();
    for (std::size_t step = 0; step < maxSteps; ++step) {
        s.processBlock(kCtl);
        const bool active = s.isEventActive();
        if (active && !prevActive) return true;
        prevActive = active;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Block-capture helpers (determinism clauses)
// -----------------------------------------------------------------------------

struct CapturedStream {
    std::vector<float> values;
    std::vector<std::uint8_t> targets;
    std::vector<std::int8_t> polarities;
};

/// Advance `numBlocks` blocks of `blockSize` samples, capturing one observation
/// per block.
[[nodiscard]] CapturedStream captureBlocks(SlowEventScheduler& s, std::size_t numBlocks,
                                           std::size_t blockSize) {
    CapturedStream out;
    out.values.reserve(numBlocks);
    out.targets.reserve(numBlocks);
    out.polarities.reserve(numBlocks);
    for (std::size_t i = 0; i < numBlocks; ++i) {
        s.processBlock(blockSize);
        out.values.push_back(s.getCurrentValue());
        out.targets.push_back(s.getActiveTarget());
        out.polarities.push_back(static_cast<std::int8_t>(s.getActivePolarity()));
    }
    return out;
}

}  // namespace

// =============================================================================
// SC-008 - Inter-event-time distribution
// =============================================================================
//
// 32 seeds x 500 events at the default 20-90 s range, accelerated with
// processBlock(32); the histogram is built from getPeriodSeconds() (FR-058)
// sampled at each isEventActive() rising edge. getPeriodSeconds() is the drawn
// quantity itself; inferring the period from edge spacing alone would quantize
// it to the control grid and measure a different number (clause (d) below then
// asserts the two ARE the same number, which is FR-054's whole point).
// =============================================================================

TEST_CASE("SlowEventScheduler_IntervalDistribution", "[processors][slow_events][vorago]") {
    constexpr std::size_t kEventsPerSeed = 500;
    constexpr std::size_t kTotalEvents = kEventsPerSeed * kSeeds32.size();  // 16000

    std::vector<double> periods;
    periods.reserve(kTotalEvents);

    // The pre-roll (FR-067) plus 500 periods of at most 90 s each, with slack.
    const std::size_t kStepsPerSeed =
        controlStepsFor(90.0 * static_cast<double>(kEventsPerSeed + 2), kSr48);

    for (const auto seed : kSeeds32) {
        SlowEventScheduler s;
        SchedulerConfig cfg;
        cfg.seed = seed;
        applyAndPrepare(s, cfg, kSr48);

        const auto onsets = collectOnsets(s, kStepsPerSeed, kEventsPerSeed);
        REQUIRE(onsets.size() == kEventsPerSeed);
        for (const auto& o : onsets) periods.push_back(o.periodSeconds);
    }

    REQUIRE(periods.size() == kTotalEvents);

    // ---- (a) every drawn period is inside [20, 90] s - zero violations -------
    std::size_t outOfRange = 0;
    double sum = 0.0;
    for (const double p : periods) {
        // Single named predicate: NaN fails isFinite and both comparisons, so a
        // non-finite period is counted here exactly as an out-of-range one.
        const bool periodOk = detail::isFinite(p) && p >= 20.0 && p <= 90.0;
        if (!periodOk) ++outOfRange;
        sum += p;
    }
    REQUIRE(outOfRange == 0u);

    // ---- (b) sample mean within 55 +/- 2 s ----------------------------------
    // Uniform mean 55 s, standard error 20.2/sqrt(16000) ~ 0.16 s, so +/-2 s is
    // a >12 sigma band and cannot go flaky.
    const double mean = sum / static_cast<double>(periods.size());
    WARN("SC-008(b) mean drawn period: " << mean << " s over " << periods.size() << " events");
    REQUIRE(std::abs(mean - 55.0) <= 2.0);

    // ---- (c) 10-bin histogram flat within +/-15 % ---------------------------
    constexpr std::size_t kBins = 10;
    std::array<std::size_t, kBins> hist{};
    for (const double p : periods) {
        auto bin = static_cast<std::size_t>((p - 20.0) / 7.0);
        if (bin >= kBins) bin = kBins - 1;  // p == 90 lands on the top edge
        ++hist[bin];
    }
    const double expected = static_cast<double>(kTotalEvents) / static_cast<double>(kBins);
    double worstDeviation = 0.0;
    for (const auto count : hist) {
        worstDeviation =
            std::max(worstDeviation, std::abs(static_cast<double>(count) / expected - 1.0));
    }
    WARN("SC-008(c) worst histogram deviation: " << (worstDeviation * 100.0) << " %");
    REQUIRE(worstDeviation <= 0.15);

    // ---- (d) onset-to-onset cadence IS the drawn period (FR-054) ------------
    // Under the rejected end-of-release semantics this clause would read
    // 30 + attack + hold + release and fail on a correct implementation. That
    // identity is the point of the clause.
    {
        SlowEventScheduler s;
        SchedulerConfig cfg;
        cfg.minInterval = 30.0f;
        cfg.maxInterval = 30.0f;
        applyAndPrepare(s, cfg, kSr48);  // interval range applied BEFORE prepare()

        const auto onsets = collectOnsets(s, controlStepsFor(30.0 * 14.0, kSr48), 12);
        REQUIRE(onsets.size() == 12u);

        const double oneControlStep = static_cast<double>(kCtl) / kSr48;
        for (std::size_t i = 0; i < onsets.size(); ++i) {
            REQUIRE(onsets[i].periodSeconds == 30.0);
            if (i == 0) continue;
            const double spacing =
                static_cast<double>(onsets[i].sampleIndex - onsets[i - 1].sampleIndex) / kSr48;
            REQUIRE(std::abs(spacing - 30.0) <= oneControlStep);
        }
    }

    // ---- (e) FR-067 pre-roll ------------------------------------------------
    // Without this clause an implementation that fires at t = 0 passes SC-008,
    // SC-010, SC-011 and SC-012 unchanged.
    {
        SlowEventScheduler s;
        SchedulerConfig cfg;  // kDefaultEventSeed, 20-90 s defaults
        applyAndPrepare(s, cfg, kSr48);

        REQUIRE(!s.isEventActive());
        REQUIRE(s.getActiveTarget() == SlowEventScheduler::kNoTarget);
        REQUIRE(s.getCurrentValue() == 0.0f);

        const double p0 = static_cast<double>(s.getPeriodSeconds());
        REQUIRE(p0 >= 20.0);
        REQUIRE(p0 <= 90.0);
        // With kDefaultEventSeed the drawn pre-roll is 41.236 s (u = 0.303378).
        // Quoted, not asserted: a change of draw order shows up in this WARN.
        WARN("SC-008(e) kDefaultEventSeed pre-roll: " << p0 << " s (expected ~41.236 s)");

        // Per SAMPLE up to the first onset: every sample before it is exactly 0.
        const auto preRollSamples = static_cast<std::size_t>(p0 * kSr48);
        std::size_t firstOnsetSample = 0;
        bool anyNonZeroBeforeOnset = false;
        bool prevActive = false;
        for (std::size_t i = 0; i < preRollSamples + static_cast<std::size_t>(kCtl) * 4u; ++i) {
            s.process();
            const bool active = s.isEventActive();
            if (active && !prevActive) {
                firstOnsetSample = i + 1u;
                break;
            }
            if (s.getCurrentValue() != 0.0f) anyNonZeroBeforeOnset = true;
            prevActive = active;
        }
        REQUIRE(!anyNonZeroBeforeOnset);
        REQUIRE(firstOnsetSample > 0u);
        REQUIRE(std::abs(static_cast<double>(firstOnsetSample) / kSr48 - p0) <=
                static_cast<double>(kCtl) / kSr48);
    }

    // ---- (e) repeated over the 32 seeds of clause (a) -----------------------
    {
        const double oneControlStep = static_cast<double>(kCtl) / kSr48;
        for (const auto seed : kSeeds32) {
            SlowEventScheduler s;
            SchedulerConfig cfg;
            cfg.seed = seed;
            applyAndPrepare(s, cfg, kSr48);

            REQUIRE(!s.isEventActive());
            REQUIRE(s.getActiveTarget() == SlowEventScheduler::kNoTarget);
            REQUIRE(s.getCurrentValue() == 0.0f);

            const double p0 = static_cast<double>(s.getPeriodSeconds());
            REQUIRE(p0 >= 20.0);
            REQUIRE(p0 <= 90.0);

            const auto onsets = collectOnsets(s, controlStepsFor(95.0, kSr48), 1);
            REQUIRE(onsets.size() == 1u);
            const double onsetSeconds = static_cast<double>(onsets[0].sampleIndex) / kSr48;
            REQUIRE(std::abs(onsetSeconds - p0) <= oneControlStep);
        }
    }
}

// =============================================================================
// SC-009 - Event envelope C1 continuity (no clicks)
// =============================================================================
//
// Rendered PER SAMPLE at 48 kHz, never accelerated (FR-065 makes the envelope a
// per-sample quantity; a control-rate staircase would make the interior/join
// ratio measure quantization rather than curvature).
//
// The second difference is formed on a DECIMATED grid, copying
// spline_trajectory_test.cpp:252-256, 265-268, 312-315. A per-sample second
// difference is unmeasurable here: for FR-056's smootherstep the peak per-sample
// second difference is (10/sqrt(3)) * (dt/T)^2, i.e. 1.0e-8 at the default 5 s
// attack and 1.0e-6 at kMinSegmentSeconds = 0.05 s - at or below the float32
// noise floor (~1.2e-7 at amplitude 1). The grid is therefore
//     stride = round(min(effA, effH, effR) * sr / 100)      (100 points/segment)
// exactly as the precedent's kStride = 240. With h = stride*dt the interior
// curvature term is ~5.77e-4 in the shortest-segment configuration, ~58x above
// the 1.0e-5 guard.
// =============================================================================

namespace {

struct C1Result {
    double interiorMax = 0.0;
    double joinMax = 0.0;
    double maxPerSampleDelta = 0.0;
    std::size_t stride = 0;
    std::size_t events = 0;
    // FR-058 read-surface reductions, sampled once per control step.
    double worstIdentityError = 0.0;
    bool anyEnvelopeOutOfUnit = false;
    bool anyEnvelopeNonZeroWhileIdle = false;
    double worstDurationError = 0.0;
    double worstEffectiveError = 0.0;
};

/// Per-sample render + decimated second-difference analysis.
/// `expectedEff*` are the analytic FR-055 effective segment times; the component's
/// own getters are asserted against them through `worstEffectiveError`.
[[nodiscard]] C1Result renderC1(const SchedulerConfig& cfg, double seconds, double expectedEffA,
                                double expectedEffH, double expectedEffR) {
    C1Result r;
    r.stride = static_cast<std::size_t>(
        std::lround(std::min({expectedEffA, expectedEffH, expectedEffR}) * kSr48 / 100.0));
    REQUIRE(r.stride > 0u);

    SlowEventScheduler s;
    applyAndPrepare(s, cfg, kSr48);

    const auto totalSamples = static_cast<std::size_t>(seconds * kSr48);
    const std::size_t numPoints = totalSamples / r.stride;

    std::vector<double> decimated;
    decimated.reserve(numPoints + 1u);
    std::vector<char> straddles(numPoints + 1u, 0);

    double prevValue = static_cast<double>(s.getCurrentValue());
    bool prevActive = s.isEventActive();

    for (std::size_t i = 0; i < totalSamples; ++i) {
        s.process();
        const double v = static_cast<double>(s.getCurrentValue());
        r.maxPerSampleDelta = std::max(r.maxPerSampleDelta, std::abs(v - prevValue));
        prevValue = v;

        if (i % r.stride == 0u && decimated.size() <= numPoints) decimated.push_back(v);

        const bool active = s.isEventActive();
        if (active && !prevActive) {
            ++r.events;
            const double effA = static_cast<double>(s.getEffectiveAttackSeconds());
            const double effH = static_cast<double>(s.getEffectiveHoldSeconds());
            const double effR = static_cast<double>(s.getEffectiveReleaseSeconds());
            r.worstEffectiveError = std::max(
                {r.worstEffectiveError, std::abs(effA - expectedEffA), std::abs(effH - expectedEffH),
                 std::abs(effR - expectedEffR)});

            // Mark the four cycle boundaries (onset, attackEnd, holdEnd,
            // releaseEnd) on the decimated grid. A triple straddles a join when
            // the join index lies in [i-2, i].
            const std::array<double, 4> boundaries = {0.0, effA, effA + effH, effA + effH + effR};
            for (const double b : boundaries) {
                const auto sampleIdx = static_cast<double>(i) + b * kSr48;
                const auto j = static_cast<std::size_t>(sampleIdx / static_cast<double>(r.stride));
                for (std::size_t k = j; k < j + 3u && k < straddles.size(); ++k) straddles[k] = 1;
            }
        }
        prevActive = active;

        // ---- FR-058 read-surface identities, once per control step ----------
        if (i % kCtl == 0u) {
            const double env = static_cast<double>(s.getEnvelopeValue());
            // Named predicate, not an inlined negation: a NaN envelope makes both
            // comparisons false, so envInUnit is false and the flag still trips.
            const bool envInUnit = env >= 0.0 && env <= 1.0;
            if (!envInUnit) r.anyEnvelopeOutOfUnit = true;
            if (!active && s.getEnvelopeValue() != 0.0f) r.anyEnvelopeNonZeroWhileIdle = true;

            const double product = static_cast<double>(s.getActivePolarity()) *
                                   static_cast<double>(s.getActiveDepth()) * env;
            r.worstIdentityError =
                std::max(r.worstIdentityError, std::abs(static_cast<double>(v) - product));

            const double duration = static_cast<double>(s.getEventDurationSeconds());
            const double sumEff = static_cast<double>(s.getEffectiveAttackSeconds()) +
                                  static_cast<double>(s.getEffectiveHoldSeconds()) +
                                  static_cast<double>(s.getEffectiveReleaseSeconds());
            r.worstDurationError = std::max(r.worstDurationError, std::abs(duration - sumEff));
        }
    }

    for (std::size_t i = 2; i < decimated.size(); ++i) {
        const double d2 = decimated[i] - 2.0 * decimated[i - 1] + decimated[i - 2];
        const bool straddle = straddles[i] != 0 || straddles[i - 1] != 0 || straddles[i - 2] != 0;
        if (straddle) {
            r.joinMax = std::max(r.joinMax, std::abs(d2));
        } else {
            r.interiorMax = std::max(r.interiorMax, std::abs(d2));
        }
    }
    return r;
}

}  // namespace

TEST_CASE("SlowEventScheduler_EnvelopeC1AtJoins", "[processors][slow_events][vorago]") {
    // ---- (i) shortest legal segments, fixed 1 s cadence ---------------------
    // Both setEnvelopeTimes and setIntervalRange are applied BEFORE prepare()
    // by applyAndPrepare() - see the ordering banner.
    {
        SchedulerConfig cfg;
        cfg.minInterval = SlowEventScheduler::kMinIntervalSeconds;   // 1 s
        cfg.maxInterval = SlowEventScheduler::kMinIntervalSeconds;   // 1 s
        cfg.attack = SlowEventScheduler::kMinSegmentSeconds;         // 0.05 s
        cfg.hold = SlowEventScheduler::kMinSegmentSeconds;
        cfg.release = SlowEventScheduler::kMinSegmentSeconds;

        const double scale =
            fitScale(static_cast<double>(cfg.attack), static_cast<double>(cfg.hold),
                     static_cast<double>(cfg.release), static_cast<double>(cfg.minInterval));
        const double effA = static_cast<double>(cfg.attack) * scale;
        const double effH = static_cast<double>(cfg.hold) * scale;
        const double effR = static_cast<double>(cfg.release) * scale;

        const C1Result r = renderC1(cfg, 14.0, effA, effH, effR);
        WARN("SC-009(i) stride=" << r.stride << " events=" << r.events
                                 << " interiorMax=" << r.interiorMax << " joinMax=" << r.joinMax
                                 << " maxDelta=" << r.maxPerSampleDelta);

        REQUIRE(r.events >= 10u);
        REQUIRE(r.worstEffectiveError <= 1.0e-6);
        REQUIRE(r.interiorMax > 1.0e-5);
        REQUIRE(r.joinMax <= 5.0 * r.interiorMax);
    }

    // ---- (ii) FR-055 defaults ----------------------------------------------
    {
        SchedulerConfig cfg;  // 5 / 3 / 8 s, 20-90 s range
        const double scale =
            fitScale(static_cast<double>(cfg.attack), static_cast<double>(cfg.hold),
                     static_cast<double>(cfg.release), static_cast<double>(cfg.minInterval));
        const double effA = static_cast<double>(cfg.attack) * scale;
        const double effH = static_cast<double>(cfg.hold) * scale;
        const double effR = static_cast<double>(cfg.release) * scale;
        // 16 s total inside a 20 s minimum cadence: the fit is inert here.
        REQUIRE(scale == 1.0);

        const C1Result r = renderC1(cfg, 1000.0, effA, effH, effR);
        WARN("SC-009(ii) stride=" << r.stride << " events=" << r.events
                                  << " interiorMax=" << r.interiorMax << " joinMax=" << r.joinMax
                                  << " maxDelta=" << r.maxPerSampleDelta);

        REQUIRE(r.events >= 10u);
        REQUIRE(r.worstEffectiveError <= 1.0e-6);
        REQUIRE(r.interiorMax > 1.0e-5);
        REQUIRE(r.joinMax <= 5.0 * r.interiorMax);

        // Analytic worst case 7.81e-4 at 48 kHz (peak slope 1.875/T at
        // T = kDefaultAttack = 5 s scaled by depth <= 1), 8.50e-4 at 44.1 kHz.
        REQUIRE(r.maxPerSampleDelta <= 2.0e-3);

        // ---- FR-058 read-surface identities (>= 3 events covered above) -----
        // These three accessors exist precisely because getCurrentValue()'s
        // polarity * depth * envelope product destroys information, and no other
        // case reads them. The identity rejects the plausible wrong
        // implementations of getEnvelopeValue(): returning the polarity-signed
        // value, returning depth * env, or returning 0.
        REQUIRE(!r.anyEnvelopeOutOfUnit);
        REQUIRE(!r.anyEnvelopeNonZeroWhileIdle);
        REQUIRE(r.worstIdentityError <= 1.0e-6);
        REQUIRE(r.worstDurationError <= 1.0e-6);
    }
}

// =============================================================================
// SC-010 - Seeded determinism
// =============================================================================

TEST_CASE("SlowEventScheduler_SeededDeterminism", "[processors][slow_events][vorago]") {
    constexpr std::size_t kBlocks = 400;
    constexpr std::size_t kBlockSize = 2048;  // 400 x 2048 = 17.07 s at 48 kHz

    // A short, fully-fitting configuration so 400 captured blocks actually span
    // several complete events (at the 20-90 s defaults they would span at most
    // one, and every "different seeds differ" clause would be vacuous).
    SchedulerConfig base;
    base.minInterval = 1.0f;
    base.maxInterval = 3.0f;
    base.attack = 0.2f;
    base.hold = 0.2f;
    base.release = 0.2f;

    // ---- same seed => bit-identical value / target / polarity streams -------
    CapturedStream a;
    CapturedStream b;
    {
        SlowEventScheduler s1;
        SlowEventScheduler s2;
        applyAndPrepare(s1, base, kSr48);
        applyAndPrepare(s2, base, kSr48);
        a = captureBlocks(s1, kBlocks, kBlockSize);
        b = captureBlocks(s2, kBlocks, kBlockSize);
    }
    REQUIRE(a.values == b.values);
    REQUIRE(a.targets == b.targets);
    REQUIRE(a.polarities == b.polarities);

    // Anti-vacuity: the captured stream must contain real events, otherwise
    // "identical" would merely compare two runs of zeros.
    REQUIRE(std::any_of(a.values.begin(), a.values.end(), [](float v) { return v != 0.0f; }));
    REQUIRE(std::any_of(a.targets.begin(), a.targets.end(),
                        [](std::uint8_t t) { return t != SlowEventScheduler::kNoTarget; }));

    // ---- different seeds => different streams (32 distinct seeds) -----------
    std::vector<CapturedStream> streams;
    streams.reserve(kSeeds32.size());
    for (const auto seed : kSeeds32) {
        SlowEventScheduler s;
        SchedulerConfig cfg = base;
        cfg.seed = seed;
        applyAndPrepare(s, cfg, kSr48);
        streams.push_back(captureBlocks(s, kBlocks, kBlockSize));
    }
    for (std::size_t i = 0; i < streams.size(); ++i) {
        for (std::size_t j = i + 1; j < streams.size(); ++j) {
            REQUIRE(streams[i].values != streams[j].values);
        }
    }

    // ---- reset() reproduces the post-prepare stream exactly (FR-061) -------
    {
        SlowEventScheduler s;
        applyAndPrepare(s, base, kSr48);
        const CapturedStream run1 = captureBlocks(s, kBlocks, kBlockSize);
        s.reset();
        const CapturedStream run2 = captureBlocks(s, kBlocks, kBlockSize);
        REQUIRE(run1.values == run2.values);
        REQUIRE(run1.targets == run2.targets);
        REQUIRE(run1.polarities == run2.polarities);
    }

    // ---- setSeed() mid-event does not truncate that event (FR-062) ---------
    // Two instances advanced in lockstep; only one is re-seeded, mid-attack. The
    // remaining samples of the in-flight event must stay BIT-identical.
    {
        SlowEventScheduler reseeded;
        SlowEventScheduler reference;
        applyAndPrepare(reseeded, base, kSr48);
        applyAndPrepare(reference, base, kSr48);

        // Advance both to the first onset, in lockstep.
        REQUIRE(advanceToOnsetPerSample(reseeded, static_cast<std::size_t>(10.0 * kSr48),
                                        &reference));
        // Into the attack segment.
        for (std::size_t i = 0; i < 1000; ++i) {
            reseeded.process();
            reference.process();
        }

        reseeded.setSeed(0xFEEDu);

        std::vector<float> tail1;
        std::vector<float> tail2;
        while (reference.isEventActive()) {
            reseeded.process();
            reference.process();
            tail1.push_back(reseeded.getCurrentValue());
            tail2.push_back(reference.getCurrentValue());
        }
        REQUIRE(tail1.size() > 100u);
        REQUIRE(tail1 == tail2);
    }
}

// =============================================================================
// SC-011 - Output boundedness over a long run
// =============================================================================
//
// 2 h of accelerated rendering (processBlock(4096)) across the parameter corners.
// The per-observation clauses are accumulated into reductions (see the assertion
// density note at the top of this file).
// =============================================================================

TEST_CASE("SlowEventScheduler_BoundedOverLongRun", "[processors][slow_events][vorago]") {
    constexpr std::size_t kBlock = 4096;
    constexpr double kHours = 2.0;
    const auto kBlocks =
        static_cast<std::size_t>(kHours * 3600.0 * kSr48 / static_cast<double>(kBlock));

    const std::array<std::pair<float, float>, 2> intervalCorners = {
        std::pair{SlowEventScheduler::kMinIntervalSeconds, SlowEventScheduler::kMinIntervalSeconds},
        std::pair{SlowEventScheduler::kMaxIntervalSeconds, SlowEventScheduler::kMaxIntervalSeconds}};
    const std::array<float, 2> segmentCorners = {SlowEventScheduler::kMinSegmentSeconds,
                                                 SlowEventScheduler::kMaxSegmentSeconds};
    const std::array<std::pair<float, float>, 2> depthCorners = {std::pair{0.0f, 0.0f},
                                                                std::pair{1.0f, 1.0f}};
    const std::array<float, 3> bipolarCorners = {0.0f, 0.5f, 1.0f};
    const std::array<std::uint8_t, 2> targetCorners = {
        static_cast<std::uint8_t>(1u), static_cast<std::uint8_t>(SlowEventScheduler::kMaxTargets)};

    bool anyOutOfRange = false;
    bool anyNonFinite = false;
    bool anyBadTarget = false;
    bool anyBadSourceRange = false;

    for (const auto& interval : intervalCorners) {
        for (const float segment : segmentCorners) {
            for (const auto& depth : depthCorners) {
                for (const float bipolar : bipolarCorners) {
                    for (const std::uint8_t targets : targetCorners) {
                        for (const auto seed : kSeeds8) {
                            SlowEventScheduler s;
                            SchedulerConfig cfg;
                            cfg.seed = seed;
                            cfg.minInterval = interval.first;
                            cfg.maxInterval = interval.second;
                            cfg.attack = segment;
                            cfg.hold = segment;
                            cfg.release = segment;
                            cfg.minDepth = depth.first;
                            cfg.maxDepth = depth.second;
                            cfg.bipolar = bipolar;
                            cfg.targets = targets;
                            applyAndPrepare(s, cfg, kSr48);

                            if (s.getSourceRange() != std::pair{-1.0f, 1.0f}) {
                                anyBadSourceRange = true;
                            }

                            for (std::size_t i = 0; i < kBlocks; ++i) {
                                s.processBlock(kBlock);
                                const float v = s.getCurrentValue();
                                if (!detail::isFinite(v)) anyNonFinite = true;
                                if (std::abs(v) > 1.0f) anyOutOfRange = true;
                                const std::uint8_t t = s.getActiveTarget();
                                if (t != SlowEventScheduler::kNoTarget && t >= targets) {
                                    anyBadTarget = true;
                                }
                            }

                            if (s.getSourceRange() != std::pair{-1.0f, 1.0f}) {
                                anyBadSourceRange = true;
                            }
                        }
                    }
                }
            }
        }
    }

    REQUIRE(!anyNonFinite);
    REQUIRE(!anyOutOfRange);
    REQUIRE(!anyBadTarget);
    REQUIRE(!anyBadSourceRange);

    // ---- Non-tautology: depth range halving halves the peak exactly ---------
    // setDepthRange(0.15, 0.5) versus setDepthRange(0.3, 1.0), same seed. FR-060
    // fixes the draw order (period, target, depth, polarity) and consumes exactly
    // one nextUnipolar() for depth in both runs, so the two RNG streams stay
    // aligned and each event's depth is 0.3 + u*0.7 versus exactly half of it.
    // This is exact in float because 0.3f == 2*0.15f and 0.7f == 2*0.35f
    // bit-exactly. A clamp cannot produce exact half-scaling.
    {
        const auto peakOf = [&](float lo, float hi) {
            SlowEventScheduler s;
            SchedulerConfig cfg;
            cfg.seed = 0x51E7u;
            cfg.minDepth = lo;
            cfg.maxDepth = hi;
            cfg.bipolar = 0.0f;  // all-positive so the peak is the drawn depth
            applyAndPrepare(s, cfg, kSr48);
            double peak = 0.0;
            const auto blocks =
                static_cast<std::size_t>(3600.0 * kSr48 / static_cast<double>(kBlock));
            for (std::size_t i = 0; i < blocks; ++i) {
                s.processBlock(kBlock);
                peak = std::max(peak, std::abs(static_cast<double>(s.getCurrentValue())));
            }
            return peak;
        };
        // 85 ms block granularity against a 3 s hold plateau: the plateau (where
        // the envelope is exactly 1 and |out| is exactly the drawn depth) is
        // always sampled.
        const double peakA = peakOf(0.15f, 0.5f);
        const double peakB = peakOf(0.3f, 1.0f);
        WARN("SC-011 depth peaks: A=" << peakA << " B=" << peakB);
        REQUIRE(peakB > 0.0);
        REQUIRE(std::abs(peakA / peakB - 0.5) < 1e-4);
    }
}

// =============================================================================
// SC-012 - Sample-rate invariance
// =============================================================================
//
// processBlock(32) at 44 100 and 96 000, same seed, never at a reduced sample
// rate. The tolerance is a CUMULATIVE bound on onset position: it is attainable
// only because FR-064 mandates a non-accumulating timeline (without it the ~200
// transitions in 50 events would drift by up to ~79 ms).
// =============================================================================

TEST_CASE("SlowEventScheduler_SampleRateInvariant", "[processors][slow_events][vorago]") {
    constexpr std::size_t kEvents = 50;
    constexpr double kRenderSeconds = 5000.0;  // 50 events at up to 90 s each, with slack

    const auto onsetsAt = [&](double sampleRate) {
        SlowEventScheduler s;
        SchedulerConfig cfg;  // defaults, kDefaultEventSeed
        applyAndPrepare(s, cfg, sampleRate);
        std::vector<double> times;
        bool prevActive = s.isEventActive();
        const std::size_t steps = controlStepsFor(kRenderSeconds, sampleRate);
        for (std::size_t step = 0; step < steps; ++step) {
            s.processBlock(kCtl);
            const bool active = s.isEventActive();
            if (active && !prevActive) {
                times.push_back(static_cast<double>((step + 1) * kCtl) / sampleRate);
            }
            prevActive = active;
        }
        return times;
    };

    const std::vector<double> t44 = onsetsAt(kSr44);
    const std::vector<double> t96 = onsetsAt(kSr96);

    REQUIRE(t44.size() >= kEvents);
    REQUIRE(t96.size() >= kEvents);

    // One control step at the coarser rate: 32 / 44100 = 7.256e-4 s.
    const double tolerance = static_cast<double>(kCtl) / kSr44;
    double worst = 0.0;
    for (std::size_t i = 0; i < kEvents; ++i) {
        worst = std::max(worst, std::abs(t44[i] - t96[i]));
    }
    WARN("SC-012 worst onset-position difference over " << kEvents << " events: " << worst
                                                        << " s (tolerance " << tolerance << " s)");
    for (std::size_t i = 0; i < kEvents; ++i) {
        REQUIRE(std::abs(t44[i] - t96[i]) <= tolerance);
    }

    // ---- event count over a fixed 30 min wall-clock window is identical -----
    const auto countWithin = [](const std::vector<double>& times, double windowSeconds) {
        return static_cast<std::size_t>(
            std::count_if(times.begin(), times.end(),
                          [windowSeconds](double t) { return t < windowSeconds; }));
    };
    const std::size_t n44 = countWithin(t44, 1800.0);
    const std::size_t n96 = countWithin(t96, 1800.0);
    REQUIRE(n44 > 0u);
    REQUIRE(n44 == n96);
}

// =============================================================================
// SC-013 - RT safety, zero allocations (scheduler half)
// =============================================================================
//
// The configuration is PINNED inside the criterion and applied BEFORE prepare().
// At the defaults the tracked window (2 180 096 samples = 45.42 s at 48 kHz) is
// shorter than the 20 s minimum period plus the 41.24 s pre-roll, so the whole
// run would sit idle and an allocation in the draw path (FR-052/059/060), in any
// state transition (FR-054) or in the FR-063 multi-transition loop would pass
// unnoticed.
//
// ANTI-VACUITY MEASUREMENT NOTE. The onset count cannot be observed from inside
// the tracked window: 40 x processBlock(48'000) at a 1 s period advances exactly
// one whole cycle per call, so isEventActive() reads the same value at every
// observation point and no rising edge is visible at that granularity. The count
// is therefore taken from an untracked REPLICA - same seed, same configuration,
// the same 2 180 096 samples - driven at control-step granularity. The two runs
// are the same trajectory by construction: onsets are taken at control-rate and
// FR-064 carries the remainder, so processBlock(48'000) is exactly equivalent to
// 1500 x processBlock(32).
// =============================================================================

TEST_CASE("SlowEventScheduler_NoAllocInProcess", "[processors][slow_events][vorago]") {
    SchedulerConfig cfg;
    cfg.minInterval = SlowEventScheduler::kMinIntervalSeconds;  // 1 s
    cfg.maxInterval = SlowEventScheduler::kMinIntervalSeconds;  // 1 s
    cfg.attack = SlowEventScheduler::kMinSegmentSeconds;        // 0.05 s
    cfg.hold = SlowEventScheduler::kMinSegmentSeconds;
    cfg.release = SlowEventScheduler::kMinSegmentSeconds;

    // ---- tracked window ----------------------------------------------------
    SlowEventScheduler s;
    applyAndPrepare(s, cfg, kSr48);
    s.processBlock(512);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        s.processBlock(512);
    }
    for (int i = 0; i < 4096; ++i) {
        s.process();
    }
    for (int i = 0; i < 40; ++i) {
        s.processBlock(48'000);
    }
    const std::size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
    REQUIRE(detail::isFinite(s.getCurrentValue()));

    // ---- anti-vacuity: the pinned window really is event-dense --------------
    constexpr std::size_t kTrackedSamples = 500u * 512u + 4096u + 40u * 48'000u;  // 2 180 096
    static_assert(kTrackedSamples % kCtl == 0u,
                  "tracked window must be a whole number of control steps");

    SlowEventScheduler replica;
    applyAndPrepare(replica, cfg, kSr48);
    replica.processBlock(512);  // mirror the untracked warm-up

    std::size_t risingEdges = 0;
    bool prevActive = replica.isEventActive();
    for (std::size_t step = 0; step < kTrackedSamples / kCtl; ++step) {
        replica.processBlock(kCtl);
        const bool active = replica.isEventActive();
        if (active && !prevActive) ++risingEdges;
        prevActive = active;
    }
    WARN("SC-013 rising edges inside the tracked window: " << risingEdges << " (expected ~45)");
    // Measured 45 with the correct configuration ordering (~2.25x margin); a
    // return to the 20 s default drops it to ~1 and fails loudly.
    REQUIRE(risingEdges >= 20u);
}

// =============================================================================
// SC-018 - Setter-storm continuity (FR-066 latch rule)
// =============================================================================

TEST_CASE("SlowEventScheduler_SetterStormContinuity", "[processors][slow_events][vorago]") {
    SchedulerConfig cfg;
    cfg.minInterval = 2.0f;
    cfg.maxInterval = 2.0f;
    cfg.attack = 0.5f;
    cfg.hold = 0.3f;
    cfg.release = 0.8f;  // 1.6 s total, fits inside the 2 s cadence

    SlowEventScheduler s;
    applyAndPrepare(s, cfg, kSr48);

    // Advance per sample to the first onset.
    REQUIRE(advanceToOnsetPerSample(s, static_cast<std::size_t>(10.0 * kSr48)));

    // Into mid-attack (attack is 0.5 s; 0.25 s in).
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.25 * kSr48); ++i) {
        s.process();
    }
    REQUIRE(s.isEventActive());

    // The latched, in-flight quantities. FR-066: none of these may move until the
    // next onset, no matter what the setters do.
    // `auto` throughout: the getters' exact return types are the component's
    // business, and pinning them to float here would be a narrowing conversion
    // (and a C4244 warning) if any of them returns double.
    const auto latchedEffA = s.getEffectiveAttackSeconds();
    const auto latchedEffH = s.getEffectiveHoldSeconds();
    const auto latchedEffR = s.getEffectiveReleaseSeconds();
    const auto latchedPeriod = s.getPeriodSeconds();
    const auto latchedDepth = s.getActiveDepth();
    const auto latchedPolarity = s.getActivePolarity();

    // A small deterministic table of deliberately different values, cycled once
    // per control step for the rest of the event.
    struct StormStep {
        float attack;
        float hold;
        float release;
        float minInterval;
        float maxInterval;
        float minDepth;
        float maxDepth;
        float bipolar;
        std::uint8_t targets;
    };
    const std::array<StormStep, 4> storm = {
        StormStep{0.05f, 0.05f, 0.05f, 1.0f, 1.0f, 0.0f, 0.1f, 0.0f, 1u},
        StormStep{60.0f, 30.0f, 90.0f, 300.0f, 600.0f, 0.9f, 1.0f, 1.0f, 16u},
        StormStep{0.5f, 12.0f, 0.5f, 5.0f, 5.0f, 0.25f, 0.75f, 0.25f, 7u},
        StormStep{300.0f, 300.0f, 300.0f, 1.0f, 600.0f, 0.0f, 1.0f, 0.75f, 3u}};

    double maxPerSampleDelta = 0.0;
    double worstLatchDrift = 0.0;
    bool anyPolarityChange = false;
    double prevValue = static_cast<double>(s.getCurrentValue());
    std::size_t stormSteps = 0;
    std::size_t sampleIndex = 0;
    const auto kSampleGuard = static_cast<std::size_t>(20.0 * kSr48);

    while (s.isEventActive() && sampleIndex < kSampleGuard) {
        if (sampleIndex % kCtl == 0u) {
            const StormStep& step = storm[stormSteps % storm.size()];
            s.setEnvelopeTimes(step.attack, step.hold, step.release);
            s.setIntervalRange(step.minInterval, step.maxInterval);
            s.setDepthRange(step.minDepth, step.maxDepth);
            s.setBipolarProbability(step.bipolar);
            s.setTargetCount(step.targets);
            ++stormSteps;

            // The stored configuration IS visible immediately through its own
            // getters - that half of FR-066 is asserted here so the latch clause
            // below cannot be satisfied by a setter that simply does nothing.
            REQUIRE(s.getAttackSeconds() ==
                    std::clamp(step.attack, SlowEventScheduler::kMinSegmentSeconds,
                               SlowEventScheduler::kMaxSegmentSeconds));
            REQUIRE(s.getTargetCount() == step.targets);
        }

        s.process();
        ++sampleIndex;

        const double v = static_cast<double>(s.getCurrentValue());
        maxPerSampleDelta = std::max(maxPerSampleDelta, std::abs(v - prevValue));
        prevValue = v;

        // Direct FR-066 observation: an implementation that re-fits or re-draws
        // mid-event fails at the instant the change is applied.
        worstLatchDrift =
            std::max({worstLatchDrift,
                      std::abs(static_cast<double>(s.getEffectiveAttackSeconds() - latchedEffA)),
                      std::abs(static_cast<double>(s.getEffectiveHoldSeconds() - latchedEffH)),
                      std::abs(static_cast<double>(s.getEffectiveReleaseSeconds() - latchedEffR)),
                      std::abs(static_cast<double>(s.getPeriodSeconds() - latchedPeriod)),
                      std::abs(static_cast<double>(s.getActiveDepth() - latchedDepth))});
        if (s.getActivePolarity() != latchedPolarity) anyPolarityChange = true;
    }
    REQUIRE(sampleIndex < kSampleGuard);  // the event completed naturally
    REQUIRE(!anyPolarityChange);

    WARN("SC-018 storm steps=" << stormSteps << " maxPerSampleDelta=" << maxPerSampleDelta
                               << " worstLatchDrift=" << worstLatchDrift);

    REQUIRE(stormSteps > 10u);
    REQUIRE(worstLatchDrift == 0.0);
    REQUIRE(maxPerSampleDelta <= 2.0e-3);
}

// =============================================================================
// FR-001 / FR-002 / FR-006 / Edge Cases
// =============================================================================

TEST_CASE("SlowEventScheduler_EdgeCases", "[processors][slow_events][vorago]") {
    // ---- FR-001: it IS a ModulationSource, through a base handle ------------
    static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, Krate::DSP::SlowEventScheduler>,
                  "SlowEventScheduler must publicly derive from ModulationSource (FR-051)");
    {
        SlowEventScheduler s;
        SchedulerConfig cfg;
        cfg.minInterval = 1.0f;
        cfg.maxInterval = 1.0f;
        cfg.attack = 0.2f;
        cfg.hold = 0.2f;
        cfg.release = 0.2f;
        applyAndPrepare(s, cfg, kSr48);
        s.processBlock(60'000);  // land inside an event, not on the idle stretch

        ModulationSource& ms = s;
        REQUIRE(ms.getCurrentValue() == s.getCurrentValue());
        REQUIRE(ms.getSourceRange() == std::pair{-1.0f, 1.0f});
        REQUIRE(s.getSourceRange() == std::pair{-1.0f, 1.0f});
    }

    // ---- processBlock(0) is a no-op ----------------------------------------
    {
        SchedulerConfig cfg;
        cfg.minInterval = 1.0f;
        cfg.maxInterval = 3.0f;
        cfg.attack = 0.2f;
        cfg.hold = 0.2f;
        cfg.release = 0.2f;

        SlowEventScheduler withZeros;
        SlowEventScheduler without;
        applyAndPrepare(withZeros, cfg, kSr48);
        applyAndPrepare(without, cfg, kSr48);

        std::vector<float> a;
        std::vector<float> b;
        for (std::size_t i = 0; i < 400; ++i) {
            withZeros.processBlock(0);
            withZeros.processBlock(512);
            withZeros.processBlock(0);
            without.processBlock(512);
            a.push_back(withZeros.getCurrentValue());
            b.push_back(without.getCurrentValue());
        }
        REQUIRE(a == b);
        REQUIRE(std::any_of(a.begin(), a.end(), [](float v) { return v != 0.0f; }));
    }

    // ---- processBlock(10'000'000): FR-063 bounded multi-transition loop -----
    // ~3.5 min in one call. The state after the single huge block must equal the
    // state after the same number of samples taken one control step at a time,
    // and the fine-grained replica must show the correct number of onsets.
    {
        constexpr std::size_t kHuge = 10'000'000;
        SchedulerConfig cfg;
        cfg.minInterval = SlowEventScheduler::kMinIntervalSeconds;  // 1 s = 48 000 samples
        cfg.maxInterval = SlowEventScheduler::kMinIntervalSeconds;
        cfg.attack = 0.1f;
        cfg.hold = 0.1f;
        cfg.release = 0.1f;

        SlowEventScheduler big;
        applyAndPrepare(big, cfg, kSr48);

        auto& detector = TestHelpers::AllocationDetector::instance();
        detector.startTracking();
        big.processBlock(kHuge);
        const std::size_t allocations = detector.stopTracking();
        REQUIRE(allocations == 0u);

        REQUIRE(detail::isFinite(big.getCurrentValue()));
        REQUIRE(std::abs(big.getCurrentValue()) <= 1.0f);

        SlowEventScheduler fine;
        applyAndPrepare(fine, cfg, kSr48);
        std::size_t onsets = 0;
        bool prevActive = fine.isEventActive();
        static_assert(kHuge % kCtl == 0u, "huge block must be a whole number of control steps");
        for (std::size_t step = 0; step < kHuge / kCtl; ++step) {
            fine.processBlock(kCtl);
            const bool active = fine.isEventActive();
            if (active && !prevActive) ++onsets;
            prevActive = active;
        }
        // Pre-roll 1 s, then one onset per second: floor(10'000'000 / 48'000) = 208.
        REQUIRE(onsets == 208u);
        REQUIRE(big.getCurrentValue() == fine.getCurrentValue());
        REQUIRE(big.getActiveTarget() == fine.getActiveTarget());
        REQUIRE(big.getActiveDepth() == fine.getActiveDepth());
        REQUIRE(big.getActivePolarity() == fine.getActivePolarity());
        REQUIRE(big.getEnvelopeValue() == fine.getEnvelopeValue());
    }

    // ---- advance before prepare(): no crash, finite output ------------------
    {
        SlowEventScheduler s;
        s.process();
        s.processBlock(1024);
        REQUIRE(detail::isFinite(s.getCurrentValue()));
        REQUIRE(std::abs(s.getCurrentValue()) <= 1.0f);
    }

    // ---- prepare() twice, and prepare() while an event is active ------------
    {
        SchedulerConfig cfg;
        cfg.minInterval = 1.0f;
        cfg.maxInterval = 1.0f;
        cfg.attack = 0.3f;
        cfg.hold = 0.3f;
        cfg.release = 0.3f;

        SlowEventScheduler fresh;
        applyAndPrepare(fresh, cfg, kSr48);
        const CapturedStream reference = captureBlocks(fresh, 400, 512);
        // 400 x 512 = 4.27 s at a 1 s cadence: the reference really contains
        // events, so the equality below is not a comparison of two runs of zeros.
        REQUIRE(std::any_of(reference.values.begin(), reference.values.end(),
                            [](float v) { return v != 0.0f; }));

        SlowEventScheduler reprepared;
        applyAndPrepare(reprepared, cfg, kSr48);
        // Land inside an event, then re-prepare.
        REQUIRE(advanceToOnsetPerSample(reprepared, static_cast<std::size_t>(10.0 * kSr48)));
        reprepared.processBlock(2000);
        REQUIRE(reprepared.isEventActive());
        reprepared.prepare(kSr48);

        // Full re-initialisation: idle, silent, no half-completed segment.
        REQUIRE(!reprepared.isEventActive());
        REQUIRE(reprepared.getEventPhase() == SlowEventScheduler::Phase::Idle);
        REQUIRE(reprepared.getCurrentValue() == 0.0f);
        REQUIRE(reprepared.getEnvelopeValue() == 0.0f);
        REQUIRE(reprepared.getActiveTarget() == SlowEventScheduler::kNoTarget);

        const CapturedStream after = captureBlocks(reprepared, 400, 512);
        REQUIRE(after.values == reference.values);
        REQUIRE(after.targets == reference.targets);
        REQUIRE(after.polarities == reference.polarities);
    }

    // ---- setIntervalRange(90, 20) collapses to a FIXED 90 s period ----------
    // FR-052's "both collapse to min" means the MIN ARGUMENT, so hi is raised to
    // lo, never the reverse. (spec.md's Edge Cases section says "20 s" and is
    // wrong - recorded as plan section 8.8. The header is authoritative.)
    {
        SlowEventScheduler s;
        s.setIntervalRange(90.0f, 20.0f);
        REQUIRE(s.getMinIntervalSeconds() == 90.0f);
        REQUIRE(s.getMaxIntervalSeconds() == 90.0f);
        s.prepare(kSr48);
        REQUIRE(s.getPeriodSeconds() == 90.0f);
    }

    // ---- setIntervalRange(0, 0) clamps to kMinIntervalSeconds ---------------
    {
        SlowEventScheduler s;
        s.setIntervalRange(0.0f, 0.0f);
        REQUIRE(s.getMinIntervalSeconds() == SlowEventScheduler::kMinIntervalSeconds);
        REQUIRE(s.getMaxIntervalSeconds() == SlowEventScheduler::kMinIntervalSeconds);
        s.setEnvelopeTimes(2.0f, 2.0f, 2.0f);  // 6 s of envelope inside a 1 s cadence
        s.prepare(kSr48);

        // The fit rule scales the envelope inside 1 s; bounded, C1, allocation-free.
        double maxDelta = 0.0;
        bool anyNonFinite = false;
        bool anyOutOfRange = false;
        double prevValue = static_cast<double>(s.getCurrentValue());
        for (std::size_t i = 0; i < static_cast<std::size_t>(20.0 * kSr48); ++i) {
            s.process();
            const float v = s.getCurrentValue();
            if (!detail::isFinite(v)) anyNonFinite = true;
            if (std::abs(v) > 1.0f) anyOutOfRange = true;
            maxDelta = std::max(maxDelta, std::abs(static_cast<double>(v) - prevValue));
            prevValue = static_cast<double>(v);
        }
        REQUIRE(!anyNonFinite);
        REQUIRE(!anyOutOfRange);
        REQUIRE(maxDelta <= 2.0e-3);
        // 2/6 s of a 1 s cadence => 0.3333 s per segment.
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveAttackSeconds()) - 1.0 / 3.0) <= 1.0e-5);
    }

    // ---- setEnvelopeTimes(0, 0, 0) clamps each to kMinSegmentSeconds --------
    {
        SlowEventScheduler s;
        s.setEnvelopeTimes(0.0f, 0.0f, 0.0f);
        REQUIRE(s.getAttackSeconds() == SlowEventScheduler::kMinSegmentSeconds);
        REQUIRE(s.getHoldSeconds() == SlowEventScheduler::kMinSegmentSeconds);
        REQUIRE(s.getReleaseSeconds() == SlowEventScheduler::kMinSegmentSeconds);
    }

    // ---- fit rule: 300/300/300 s, and order-independence --------------------
    {
        // (a) default 20-90 s range: scale = 20/900 => 6.6667 s per segment.
        SlowEventScheduler s;
        s.setEnvelopeTimes(300.0f, 300.0f, 300.0f);
        s.prepare(kSr48);
        REQUIRE(advanceToOnsetControlRate(s, controlStepsFor(120.0, kSr48)));
        const double expected = 300.0 * (20.0 / 900.0);  // 6.66667 s
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveAttackSeconds()) - expected) <= 1.0e-4);
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveHoldSeconds()) - expected) <= 1.0e-4);
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveReleaseSeconds()) - expected) <= 1.0e-4);

        // (b) widen to 600 s: the NEXT onset fits 300/300/300 at scale 600/900
        // => 200 s per segment. The current cycle keeps its 6.6667 s (FR-066).
        s.setIntervalRange(600.0f, 600.0f);
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveAttackSeconds()) - expected) <= 1.0e-4);

        REQUIRE(advanceToOnsetControlRate(s, controlStepsFor(200.0, kSr48)));
        REQUIRE(std::abs(static_cast<double>(s.getEffectiveAttackSeconds()) - 200.0) <= 1.0e-3);
    }
    {
        // Order-independence: the fit is computed at draw time from the STORED
        // configuration, so the two call orders must agree exactly.
        SlowEventScheduler a;
        a.setEnvelopeTimes(300.0f, 300.0f, 300.0f);
        a.setIntervalRange(600.0f, 600.0f);
        a.prepare(kSr48);

        SlowEventScheduler b;
        b.setIntervalRange(600.0f, 600.0f);
        b.setEnvelopeTimes(300.0f, 300.0f, 300.0f);
        b.prepare(kSr48);

        REQUIRE(advanceToOnsetControlRate(a, controlStepsFor(700.0, kSr48)));
        REQUIRE(advanceToOnsetControlRate(b, controlStepsFor(700.0, kSr48)));

        REQUIRE(a.getEffectiveAttackSeconds() == b.getEffectiveAttackSeconds());
        REQUIRE(a.getEffectiveHoldSeconds() == b.getEffectiveHoldSeconds());
        REQUIRE(a.getEffectiveReleaseSeconds() == b.getEffectiveReleaseSeconds());
        REQUIRE(std::abs(static_cast<double>(a.getEffectiveAttackSeconds()) - 200.0) <= 1.0e-3);
    }

    // ---- setTargetCount(0) -> 1, and getActiveTarget() is then always 0 -----
    {
        SlowEventScheduler s;
        SchedulerConfig cfg;
        cfg.minInterval = 1.0f;
        cfg.maxInterval = 1.0f;
        cfg.attack = 0.2f;
        cfg.hold = 0.2f;
        cfg.release = 0.2f;
        cfg.targets = 0u;
        applyAndPrepare(s, cfg, kSr48);
        REQUIRE(s.getTargetCount() == 1u);

        bool anyBadTarget = false;
        std::size_t activeObservations = 0;
        for (std::size_t step = 0; step < controlStepsFor(60.0, kSr48); ++step) {
            s.processBlock(kCtl);
            if (s.isEventActive()) {
                ++activeObservations;
                if (s.getActiveTarget() != 0u) anyBadTarget = true;
            }
        }
        REQUIRE(activeObservations > 100u);
        REQUIRE(!anyBadTarget);
    }

    // ---- bipolar probability 0 -> all positive, 1 -> all negative -----------
    {
        const auto polaritySweep = [](float probability) {
            SlowEventScheduler s;
            SchedulerConfig cfg;
            cfg.minInterval = 1.0f;
            cfg.maxInterval = 1.0f;
            cfg.attack = 0.2f;
            cfg.hold = 0.2f;
            cfg.release = 0.2f;
            cfg.minDepth = 0.5f;
            cfg.maxDepth = 1.0f;
            cfg.bipolar = probability;
            applyAndPrepare(s, cfg, kSr48);

            double minValue = 0.0;
            double maxValue = 0.0;
            std::size_t events = 0;
            bool prevActive = s.isEventActive();
            for (std::size_t step = 0; step < controlStepsFor(120.0, kSr48); ++step) {
                s.processBlock(kCtl);
                const bool active = s.isEventActive();
                if (active && !prevActive) ++events;
                prevActive = active;
                const double v = static_cast<double>(s.getCurrentValue());
                minValue = std::min(minValue, v);
                maxValue = std::max(maxValue, v);
            }
            REQUIRE(events > 50u);
            return std::pair{minValue, maxValue};
        };

        const auto allPositive = polaritySweep(0.0f);
        REQUIRE(allPositive.first == 0.0);   // nothing ever went negative
        REQUIRE(allPositive.second > 0.4);   // and events really happened

        const auto allNegative = polaritySweep(1.0f);
        REQUIRE(allNegative.second == 0.0);  // nothing ever went positive
        REQUIRE(allNegative.first < -0.4);
    }

    // ---- NaN / +-Inf into every float setter --------------------------------
    // NaN maps to the LOW bound (std::clamp propagates NaN; the component routes
    // every float setter through its own sanitizeClamp).
    {
        SlowEventScheduler s;

        s.setIntervalRange(qNaN(), qNaN());
        REQUIRE(s.getMinIntervalSeconds() == SlowEventScheduler::kMinIntervalSeconds);
        REQUIRE(s.getMaxIntervalSeconds() == SlowEventScheduler::kMinIntervalSeconds);
        s.setIntervalRange(negInf(), posInf());
        REQUIRE(s.getMinIntervalSeconds() == SlowEventScheduler::kMinIntervalSeconds);
        REQUIRE(s.getMaxIntervalSeconds() == SlowEventScheduler::kMaxIntervalSeconds);

        s.setEnvelopeTimes(qNaN(), qNaN(), qNaN());
        REQUIRE(s.getAttackSeconds() == SlowEventScheduler::kMinSegmentSeconds);
        REQUIRE(s.getHoldSeconds() == SlowEventScheduler::kMinSegmentSeconds);
        REQUIRE(s.getReleaseSeconds() == SlowEventScheduler::kMinSegmentSeconds);
        s.setEnvelopeTimes(posInf(), posInf(), negInf());
        REQUIRE(s.getAttackSeconds() == SlowEventScheduler::kMaxSegmentSeconds);
        REQUIRE(s.getHoldSeconds() == SlowEventScheduler::kMaxSegmentSeconds);
        REQUIRE(s.getReleaseSeconds() == SlowEventScheduler::kMinSegmentSeconds);

        s.setDepthRange(qNaN(), qNaN());
        REQUIRE(s.getMinDepth() == 0.0f);
        REQUIRE(s.getMaxDepth() == 0.0f);
        s.setDepthRange(negInf(), posInf());
        REQUIRE(s.getMinDepth() == 0.0f);
        REQUIRE(s.getMaxDepth() == 1.0f);

        s.setBipolarProbability(qNaN());
        REQUIRE(s.getBipolarProbability() == 0.0f);
        s.setBipolarProbability(posInf());
        REQUIRE(s.getBipolarProbability() == 1.0f);
        s.setBipolarProbability(negInf());
        REQUIRE(s.getBipolarProbability() == 0.0f);

        s.setTargetCount(0u);
        REQUIRE(s.getTargetCount() == 1u);
        s.setTargetCount(99u);
        REQUIRE(s.getTargetCount() == static_cast<std::uint8_t>(SlowEventScheduler::kMaxTargets));

        s.prepare(kSr48);
        s.processBlock(48'000);
        REQUIRE(detail::isFinite(s.getCurrentValue()));
    }

    // ---- prepare(0.0) / prepare(-1.0): 1 Hz floor, finite output ------------
    {
        for (const double badRate : {0.0, -1.0}) {
            SlowEventScheduler s;
            SchedulerConfig cfg;
            applyAndPrepare(s, cfg, badRate);
            bool anyNonFinite = false;
            for (std::size_t i = 0; i < 4096; ++i) {
                s.process();
                if (!detail::isFinite(s.getCurrentValue())) anyNonFinite = true;
            }
            s.processBlock(48'000);
            if (!detail::isFinite(s.getCurrentValue())) anyNonFinite = true;
            REQUIRE(!anyNonFinite);
            REQUIRE(std::abs(s.getCurrentValue()) <= 1.0f);
        }
    }

    // ---- setSeed(0) is a documented alias for kDefaultSeed = 2463534242u ----
    // random.h:73-74 substitutes the default for a zero seed (:85 defines it).
    // This is an alias, NOT an error.
    {
        SchedulerConfig cfg;
        cfg.minInterval = 1.0f;
        cfg.maxInterval = 3.0f;
        cfg.attack = 0.2f;
        cfg.hold = 0.2f;
        cfg.release = 0.2f;

        SlowEventScheduler zero;
        SchedulerConfig zeroCfg = cfg;
        zeroCfg.seed = 0u;
        applyAndPrepare(zero, zeroCfg, kSr48);

        SlowEventScheduler aliased;
        SchedulerConfig aliasCfg = cfg;
        aliasCfg.seed = 2463534242u;
        applyAndPrepare(aliased, aliasCfg, kSr48);

        const CapturedStream a = captureBlocks(zero, 400, 2048);
        const CapturedStream b = captureBlocks(aliased, 400, 2048);
        REQUIRE(a.values == b.values);
        REQUIRE(a.targets == b.targets);
        REQUIRE(a.polarities == b.polarities);
        REQUIRE(std::any_of(a.values.begin(), a.values.end(), [](float v) { return v != 0.0f; }));
    }
}
