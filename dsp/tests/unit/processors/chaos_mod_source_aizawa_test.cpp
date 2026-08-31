// ==============================================================================
// Layer 2: Processor Tests - ChaosModSource / Aizawa attractor (Vorago Phase 1)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md
// Plan:  specs/vorago-phase1-events-modulation/plan.md   (sections 2.1-2.3)
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T005)
//
// Covers: FR-031..FR-036 and SC-006, SC-007 (in-TU half), SC-013 (Aizawa third).
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
//   macOS leg.
//
// NOTE ON THRESHOLDS (plan section 2.3, forward Euler, 2e6 steps per point):
//
//     speed              | dt      | max||state|| | max|out| (2nd half) | sigma
//     0.05 (kMinSpeed)   | 2.5e-5  | 1.885        | 0.750               | 0.327
//     0.1                | 5.0e-5  | 1.885        | 0.755               | 0.331
//     1.0                | 5.0e-4  | 1.885        | 0.762               | 0.326
//     5.0                | 2.5e-3  | 1.887        | 0.768               | 0.294
//     20 (kMaxSpeed)     | 1.0e-2  | 1.938        | 0.780               | 0.263
//
//   Collapse boundary: dt = 0.015 is still chaotic (sigma = 0.222); dt >= 0.02
//   COLLAPSES onto the x = y = 0 fixed point (z ~= -1.105) where max|out| and
//   sigma are both 0.0000 -- silently, with no divergence and no guard reset.
//   Clause (b) below is the ONLY clause that can catch that: the output goes
//   through std::clamp(std::tanh(...), -1, 1) (chaos_mod_source.h:236) and tanh
//   bounds ANY finite input, so "output in [-1,+1]" alone proves nothing.
//
//   Coupling worst case (coupling = 1, inputLevel = 1.0, 900 000 control steps
//   = 600 s at 48 kHz): max||state|| = 111.86 at kMinSpeed, 19.22 at speed 1,
//   2.47 at kMaxSpeed -- all far below the safeBound_ * 10 = 250 guard
//   (chaos_mod_source.h:306-309 with FR-033's safeBound_ = 25.0f).
//
//   Every numeric bound below is a MEASURED tolerance. There is no checked-in
//   float digest anywhere in this TU.
//
// NOTE ON ASSERTION DENSITY:
//   The two 1 h accelerated renders sweep ~5.4e6 control steps per speed. A
//   REQUIRE per sample would make Catch2 assertion bookkeeping, not the DSP, the
//   run time. The per-sample clauses are therefore accumulated into worst-case
//   reductions (maxAbs / anyNonFinite) inside the render loop and asserted once
//   per corner. The semantics are identical: the reduction fails iff at least
//   one sample violates the clause.
// ==============================================================================

#include <krate/dsp/processors/chaos_mod_source.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;

/// The five speeds SC-006 sweeps: both ends of [kMinSpeed, kMaxSpeed]
/// (chaos_mod_source.h:37,38) plus three interior points.
constexpr std::array<float, 5> kSpeeds = {ChaosModSource::kMinSpeed, 0.1f, 1.0f, 5.0f,
                                          ChaosModSource::kMaxSpeed};

/// 1 h of wall clock at 48 kHz rendered in 4096-sample blocks
/// (3600 * 48000 / 4096 = 42187.5).
constexpr std::size_t kAccelBlock = 4096;
constexpr std::size_t kOneHourBlocks = 42188;

// -----------------------------------------------------------------------------
// Configuration helper
// -----------------------------------------------------------------------------

/// setModel() BEFORE prepare(): prepare() then runs updateModelParams() /
/// resetModelState() for the Aizawa tables and zeroes the FR-036 divergence
/// counter, so every render starts from a known count of 0.
void configureAizawa(ChaosModSource& src, float speed, float coupling, float inputLevel,
                     double sampleRate) noexcept {
    src.setModel(ChaosModel::Aizawa);
    src.prepare(sampleRate);
    src.setSpeed(speed);
    src.setCoupling(coupling);
    src.setInputLevel(inputLevel);
}

// -----------------------------------------------------------------------------
// Render helpers
// -----------------------------------------------------------------------------

struct RenderStats {
    double maxAbs = 0.0;
    bool anyNonFinite = false;
    double secondHalfMaxAbs = 0.0;
    double secondHalfStdDev = 0.0;
    std::uint32_t divergenceResets = 0;
};

/// Accelerated render: one capture of getCurrentValue() per block.
/// Bound/finiteness clauses are accumulated as reductions (see the assertion
/// density note above); the non-tautology statistics are taken over the second
/// half only, so the initial transient off the fixed initial state cannot
/// inflate them.
[[nodiscard]] RenderStats renderAccelerated(ChaosModSource& src, std::size_t numBlocks,
                                            std::size_t blockSize) {
    RenderStats stats;
    const std::size_t halfway = numBlocks / 2;
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t count = 0;

    for (std::size_t i = 0; i < numBlocks; ++i) {
        src.processBlock(blockSize);
        const float raw = src.getCurrentValue();
        if (!detail::isFinite(raw)) {
            stats.anyNonFinite = true;
        }
        const double value = static_cast<double>(raw);
        const double magnitude = std::abs(value);
        if (magnitude > stats.maxAbs) {
            stats.maxAbs = magnitude;
        }
        if (i >= halfway) {
            if (magnitude > stats.secondHalfMaxAbs) {
                stats.secondHalfMaxAbs = magnitude;
            }
            sum += value;
            sumSq += value * value;
            ++count;
        }
    }

    if (count > 1) {
        const double mean = sum / static_cast<double>(count);
        const double variance = sumSq / static_cast<double>(count) - mean * mean;
        stats.secondHalfStdDev = variance > 0.0 ? std::sqrt(variance) : 0.0;
    }
    stats.divergenceResets = src.getDivergenceResetCount();
    return stats;
}

/// Capture the control-rate trajectory: exactly one attractor update per
/// processBlock(kControlRateInterval) call (chaos_mod_source.h:80-91).
[[nodiscard]] std::vector<double> renderControlTrajectory(ChaosModSource& src,
                                                          std::size_t numControlSteps) {
    std::vector<double> out;
    out.reserve(numControlSteps);
    for (std::size_t i = 0; i < numControlSteps; ++i) {
        src.processBlock(ChaosModSource::kControlRateInterval);
        out.push_back(static_cast<double>(src.getCurrentValue()));
    }
    return out;
}

// -----------------------------------------------------------------------------
// Autocorrelation (SC-006(c))
// -----------------------------------------------------------------------------

/// Geometric lag grid (ratio 1.15). The decorrelation lag spans three orders of
/// magnitude across the speed sweep -- 8.9 s at kMinSpeed, 0.023 s at kMaxSpeed
/// (= ~34 control steps) -- so a linear grid would either miss the fast corner
/// or cost O(maxLag * N) at the slow one.
[[nodiscard]] std::vector<std::size_t> geometricLags(std::size_t maxLag) {
    std::vector<std::size_t> lags;
    double value = 1.0;
    std::size_t last = 0;
    while (true) {
        const auto lag = static_cast<std::size_t>(value + 0.5);
        if (lag > maxLag) {
            break;
        }
        if (lag != last) {
            lags.push_back(lag);
            last = lag;
        }
        value *= 1.15;
    }
    return lags;
}

/// Minimum normalized autocorrelation over the lag grid up to `maxLag`.
/// A frozen output (zero variance -- the dt >= 0.02 collapse) has no defined
/// autocorrelation; 1.0 is returned so the criterion FAILS rather than dividing
/// by zero.
[[nodiscard]] double minAutocorrelationWithinLag(const std::vector<double>& x,
                                                 std::size_t maxLag) {
    const std::size_t n = x.size();
    if (n < 2 || maxLag == 0 || maxLag >= n) {
        return 1.0;
    }
    double sum = 0.0;
    for (const double v : x) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(n);

    double variance = 0.0;
    for (const double v : x) {
        const double d = v - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(n);
    if (!(variance > 0.0)) {
        return 1.0;
    }

    double minAc = 1.0;
    for (const std::size_t lag : geometricLags(maxLag)) {
        double acc = 0.0;
        const std::size_t count = n - lag;
        for (std::size_t i = 0; i < count; ++i) {
            acc += (x[i] - mean) * (x[i + lag] - mean);
        }
        const double ac = acc / (static_cast<double>(count) * variance);
        if (ac < minAc) {
            minAc = ac;
        }
    }
    return minAc;
}

}  // namespace

// =============================================================================
// 1. SC-006 - Aizawa bounded, non-divergent and chaotic
// =============================================================================

TEST_CASE("ChaosModSource_AizawaBoundedAndChaotic", "[processors][chaos][aizawa][vorago]") {
    SECTION("(a)+(b) coupling 0: bounded, finite, no guard reset, non-trivial excursion") {
        for (const float speed : kSpeeds) {
            ChaosModSource src;
            configureAizawa(src, speed, 0.0f, 0.0f, kSr48);
            const RenderStats stats = renderAccelerated(src, kOneHourBlocks, kAccelBlock);

            INFO("speed = " << speed << "  maxAbs = " << stats.maxAbs
                            << "  secondHalfMaxAbs = " << stats.secondHalfMaxAbs
                            << "  stddev = " << stats.secondHalfStdDev);

            // (a) FR-031 / FR-032 / FR-036.
            REQUIRE_FALSE(stats.anyNonFinite);
            REQUIRE(stats.maxAbs <= 1.0);
            REQUIRE(stats.divergenceResets == 0u);

            // (b) Non-tautology. Measured 0.750-0.780 and sigma 0.263-0.331
            // (plan section 2.3). The only clause that catches the dt >= 0.02
            // fixed-point collapse.
            REQUIRE(stats.secondHalfMaxAbs > 0.5);
            REQUIRE(stats.secondHalfMaxAbs < 0.99);
            REQUIRE(stats.secondHalfStdDev > 0.1);
        }
    }

    SECTION("(c) chaotic character at coupling 0") {
        // 120 s of control-rate trajectory, autocorrelation scanned to a 60 s lag.
        constexpr std::size_t kTrajectorySteps = 180000;  // 120 s at 48 kHz / 32
        constexpr std::size_t kMaxLagSteps = 90000;       // 60 s
        constexpr double kInverseE = 0.36787944117144233;

        for (const float speed : kSpeeds) {
            ChaosModSource src;
            configureAizawa(src, speed, 0.0f, 0.0f, kSr48);
            const std::vector<double> trajectory = renderControlTrajectory(src, kTrajectorySteps);
            const double minAc = minAutocorrelationWithinLag(trajectory, kMaxLagSteps);

            // Measured crossing: 8.9 s at kMinSpeed (the worst case), 0.023 s at
            // kMaxSpeed. Neither a slow LFO nor a frozen output can cross 1/e here.
            INFO("speed = " << speed << "  min autocorrelation within 60 s = " << minAc);
            REQUIRE(minAc < kInverseE);
        }

        // Sensitive dependence at kMaxSpeed.
        //
        // PERTURBATION RECIPE - two traps, both checked against the header:
        //   * the coupling path adds coupling_ * inputLevel_ * 0.1f to state_.x
        //     (chaos_mod_source.h:213-216), so setCoupling(0.1f) with
        //     setInputLevel(1.0e-2f) displaces x by EXACTLY 1e-4 -- the magnitude
        //     SC-006(c) names and at which its "RMS 0.21 by 10 s" was measured.
        //     setCoupling(1e-3f)/setInputLevel(0.1f) would displace by 1e-5, an
        //     order below the criterion.
        //   * the gate is std::abs(inputLevel_) > 0.001f (:214) -- STRICT -- so
        //     setInputLevel(1.0e-3f) fires no perturbation at all and the two
        //     instances stay bit-identical forever, silently asserting nothing.
        // The perturbation lasts exactly one control step: after prepare(),
        // samplesUntilUpdate_ == 0, so the first process() call performs one
        // attractor update (chaos_mod_source.h:69-75) and leaves both instances
        // in phase at samplesUntilUpdate_ == 31.
        constexpr std::size_t kSteps60s = 90000;         // 60 s at 48 kHz / 32
        constexpr std::size_t kCheckpointSteps = 15000;  // 10 s

        ChaosModSource reference;
        ChaosModSource perturbed;
        configureAizawa(reference, ChaosModSource::kMaxSpeed, 0.0f, 0.0f, kSr48);
        configureAizawa(perturbed, ChaosModSource::kMaxSpeed, 0.1f, 1.0e-2f, kSr48);

        reference.process();
        perturbed.process();
        perturbed.setCoupling(0.0f);
        perturbed.setInputLevel(0.0f);

        double sumSquaredDiff = 0.0;
        double maxPrefixRms = 0.0;
        for (std::size_t i = 0; i < kSteps60s; ++i) {
            reference.processBlock(ChaosModSource::kControlRateInterval);
            perturbed.processBlock(ChaosModSource::kControlRateInterval);
            const double diff = static_cast<double>(perturbed.getCurrentValue()) -
                                static_cast<double>(reference.getCurrentValue());
            sumSquaredDiff += diff * diff;
            if ((i + 1) % kCheckpointSteps == 0) {
                const double rms = std::sqrt(sumSquaredDiff / static_cast<double>(i + 1));
                if (rms > maxPrefixRms) {
                    maxPrefixRms = rms;
                }
            }
        }

        INFO("max prefix RMS difference within 60 s = " << maxPrefixRms);
        REQUIRE(maxPrefixRms > 0.1);  // measured 0.21 by 10 s
    }

    SECTION("(d) coupling 1.0 with a full-scale input: bounded, finite, no guard reset") {
        // Clauses (b) and (c) are deliberately NOT asserted here: a DC-biased
        // input legitimately saturates the output, and the state legitimately
        // reaches |state| ~= 112 at kMinSpeed against the safeBound_ * 10 = 250
        // guard threshold (plan section 2.3).
        for (const float speed : kSpeeds) {
            ChaosModSource src;
            configureAizawa(src, speed, 1.0f, 1.0f, kSr48);
            const RenderStats stats = renderAccelerated(src, kOneHourBlocks, kAccelBlock);

            INFO("speed = " << speed << "  maxAbs = " << stats.maxAbs);
            REQUIRE_FALSE(stats.anyNonFinite);
            REQUIRE(stats.maxAbs <= 1.0);
            REQUIRE(stats.divergenceResets == 0u);
        }
    }
}

// =============================================================================
// 2. FR-036 - the divergence counter is observable and zeroed correctly
// =============================================================================
//
// The anti-vacuity partner of SC-006's three `== 0u` clauses: without a case
// that drives the counter UP, a stubbed `return 0u;` passes all three in full.
// The case is run for Aizawa AND for the pre-existing Lorenz model, because the
// counter is model-agnostic -- asserting it on Lorenz is what proves the
// increment sits in the shared guard (chaos_mod_source.h:306-312) rather than in
// an Aizawa-only branch.

TEST_CASE("ChaosModSource_DivergenceCounterObservable", "[processors][chaos][aizawa][vorago]") {
    constexpr std::array<ChaosModel, 2> kModels = {ChaosModel::Aizawa, ChaosModel::Lorenz};
    constexpr int kDriveSteps = 512;

    for (const ChaosModel model : kModels) {
        INFO("model = " << static_cast<int>(model));

        ChaosModSource src;
        src.prepare(kSr48);
        src.setModel(model);
        REQUIRE(src.getDivergenceResetCount() == 0u);

        // setInputLevel is UNCLAMPED (chaos_mod_source.h:119-121), so this
        // displaces state_.x by 1e5 * 1.0 * 0.1 = 1e4 per control step -- 40x the
        // Aizawa guard threshold of safeBound_ * 10 = 250 and 20x the Lorenz
        // threshold of 500. The guard fires on the first step.
        src.setCoupling(1.0f);
        src.setInputLevel(1.0e5f);
        for (int i = 0; i < kDriveSteps; ++i) {
            src.processBlock(ChaosModSource::kControlRateInterval);
        }
        REQUIRE(src.getDivergenceResetCount() > 0u);
        REQUIRE(detail::isFinite(src.getCurrentValue()));

        // Zeroing rule, direction 1: reset().
        src.reset();
        REQUIRE(src.getDivergenceResetCount() == 0u);

        // reset() also zeroes inputLevel_ (chaos_mod_source.h:63), so the drive
        // must be re-applied before the second half of the zeroing rule.
        src.setCoupling(1.0f);
        src.setInputLevel(1.0e5f);
        for (int i = 0; i < kDriveSteps; ++i) {
            src.processBlock(ChaosModSource::kControlRateInterval);
        }
        REQUIRE(src.getDivergenceResetCount() > 0u);

        // Zeroing rule, direction 2: prepare().
        src.prepare(kSr48);
        REQUIRE(src.getDivergenceResetCount() == 0u);
    }
}

// =============================================================================
// 3. SC-007 - Aizawa causes no regression (in-TU half)
// =============================================================================

TEST_CASE("ChaosModSource_AizawaNoRegression", "[processors][chaos][aizawa][vorago]") {
    // The four pre-existing indices are pinned by chaos_waveshaper_test.cpp:33-36
    // and are persisted by index in plugin state, so Aizawa must APPEND at 4.
    REQUIRE(static_cast<int>(static_cast<std::uint8_t>(ChaosModel::Lorenz)) == 0);
    REQUIRE(static_cast<int>(static_cast<std::uint8_t>(ChaosModel::Rossler)) == 1);
    REQUIRE(static_cast<int>(static_cast<std::uint8_t>(ChaosModel::Chua)) == 2);
    REQUIRE(static_cast<int>(static_cast<std::uint8_t>(ChaosModel::Henon)) == 3);
    REQUIRE(static_cast<int>(static_cast<std::uint8_t>(ChaosModel::Aizawa)) == 4);

    // Aizawa is a ChaosModSource-only model (FR-034). The ChaosWaveshaper
    // validator (chaos_waveshaper.h:451-455) rejects anything above Henon and
    // substitutes Lorenz -- its semantics are UNCHANGED by this phase.
    ChaosWaveshaper fromDefault;
    fromDefault.prepare(kSr48, 512);
    fromDefault.setModel(ChaosModel::Aizawa);
    REQUIRE(fromDefault.getModel() == ChaosModel::Lorenz);

    // The same clause from a non-Lorenz starting model, so a validator that
    // silently stopped substituting could not hide behind the Lorenz default.
    ChaosWaveshaper fromRossler;
    fromRossler.prepare(kSr48, 512);
    fromRossler.setModel(ChaosModel::Rossler);
    REQUIRE(fromRossler.getModel() == ChaosModel::Rossler);
    fromRossler.setModel(ChaosModel::Aizawa);
    REQUIRE(fromRossler.getModel() == ChaosModel::Lorenz);
}

// =============================================================================
// 4. SC-013 - zero allocations in the Aizawa process path
// =============================================================================

TEST_CASE("ChaosModSource_AizawaNoAllocInProcess", "[processors][chaos][aizawa][vorago]") {
    constexpr std::size_t kBlock512 = 512;
    constexpr std::size_t kOneSecondAt48k = 48000;

    ChaosModSource src;
    configureAizawa(src, 1.0f, 0.0f, 0.0f, kSr48);

    // Warm-up OUTSIDE the tracked scope: prepare() and any first-touch work must
    // not be attributed to the steady state under test.
    src.processBlock(kBlock512);
    src.process();

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        src.processBlock(kBlock512);
    }
    for (int i = 0; i < 4096; ++i) {
        src.process();
    }
    for (int i = 0; i < 40; ++i) {
        src.processBlock(kOneSecondAt48k);
    }
    const std::size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
    REQUIRE(detail::isFinite(src.getCurrentValue()));
    REQUIRE(src.getDivergenceResetCount() == 0u);
}
