// ==============================================================================
// Layer 2: Processor Tests - Life Modulator Suite, combined control-rate cost
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (SC-007)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 7.2, SC-007)
// Covers: SC-007 (combined control-rate CPU of all six modulators) plus a
//         shared-contract sweep (FR-001 / FR-006 source-range fixity, FR-004
//         well-defined output after prepare + one block).
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. SC-007 therefore pins
//   the measurement basis to NANOSECONDS PER 512-SAMPLE BLOCK and gates only
//   against a CHECKED-IN BASELINE as a relative regression bound
//   (fail if > baseline x 1.5). The absolute "<= 0.05 % of the 512-samples-at-
//   48 kHz budget" figure (512/48000 ~= 10.67 ms -> ~5.3 us/block) is REPORTED,
//   not asserted, because it is only binding on the designated perf runner.
//
// NO ALLOCATION-TRACKING INCLUDES HERE:
//   brownian_drift_test.cpp is the single owner of the global operator
//   new/delete replacements for the dsp_processors_tests binary. This TU must
//   not include <allocation_operator_overrides.h> (duplicate-symbol link
//   error), and does not need it - SC-006 is covered per modulator elsewhere.
//
// No std::isnan anywhere: macOS CI builds with -ffast-math, which folds it to
// false. Finiteness is checked on the IEEE-754 exponent field instead.
// ==============================================================================

#include <krate/dsp/processors/breathing_modulator.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/orbit_modulator.h>
#include <krate/dsp/processors/spline_trajectory.h>
#include <krate/dsp/processors/tidal_modulator.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-007's absolute reference: 0.05 % of that budget (~5333 ns). REPORTED
/// only - see the header comment.
constexpr double kReferenceNsPerBlock = kBlockBudgetNs * 0.0005;

/// Checked-in ns/block baseline - the load-bearing regression gate.
///
/// PROVISIONAL VALUE. It was set from the analytic work per block (Brownian and
/// Orbit take 512/32 = 16 control steps each; the other four are O(1) per
/// block), deliberately generous so the gate cannot go flaky, and it still sits
/// below kReferenceNsPerBlock after the x1.5 factor (3000 x 1.5 = 4500 ns <
/// 5333 ns), so the test is no weaker than the SC-007 reference figure.
/// The measured value is WARN-reported on every run; tighten this constant to
/// the number observed on the dev machine (Windows 11, MSVC Release,
/// build/windows-x64-release) and record that machine here.
constexpr double kBaselineNsPerBlock = 3000.0;

/// Relative regression bound applied to the checked-in baseline (SC-007).
constexpr double kRegressionFactor = 1.5;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// One instance of each of the six life modulators, advanced together.
struct LifeModulatorBundle {
    BrownianDrift brownian;
    BreathingModulator breathing;
    TidalModulator tidal;
    OrbitModulator orbit;
    SplineTrajectory spline;
    GrowthEnvelope growth;

    void prepareAll(double sampleRate) noexcept {
        brownian.prepare(sampleRate);
        breathing.prepare(sampleRate);
        tidal.prepare(sampleRate);
        orbit.prepare(sampleRate);
        spline.prepare(sampleRate);

        // Longest rise + an immediate trigger keeps GrowthEnvelope in its
        // Rising phase (the expensive path: one std::exp per advance) for the
        // whole measured run, instead of short-circuiting in Complete.
        growth.setDuration(GrowthEnvelope::kMaxDuration);
        growth.prepare(sampleRate);
        growth.trigger();

        // Non-default settings so no modulator sits on a trivial code path:
        // coupling gives the Kuramoto term work, irregularity makes the breath
        // draw the RNG at every wrap.
        brownian.setSmoothness(0.5f);
        breathing.setRate(0.5f);
        breathing.setIrregularity(0.7f);
        tidal.setRate(1.0f);
        orbit.setRate(0.5f);
        orbit.setCoupling(1.0f);
        spline.setWaypointInterval(static_cast<double>(SplineTrajectory::kMinInterval));
    }

    void advanceBlock(std::size_t numSamples) noexcept {
        brownian.processBlock(numSamples);
        breathing.processBlock(numSamples);
        tidal.processBlock(numSamples);
        orbit.processBlock(numSamples);
        spline.processBlock(numSamples);
        growth.processBlock(numSamples);
    }

    /// Read every output (7 values - OrbitModulator contributes both axes) so
    /// the optimizer cannot dead-code the advance away. A real consumer reads
    /// these once per block too, so this is not artificial overhead.
    [[nodiscard]] double sumOutputs() const noexcept {
        return static_cast<double>(brownian.getCurrentValue()) +
               static_cast<double>(breathing.getCurrentValue()) +
               static_cast<double>(tidal.getCurrentValue()) +
               static_cast<double>(orbit.getCurrentValue()) +
               static_cast<double>(orbit.getY()) +
               static_cast<double>(spline.getCurrentValue()) +
               static_cast<double>(growth.getCurrentValue());
    }
};

}  // namespace

// =============================================================================
// SC-007: combined control-rate cost of all six modulators
// =============================================================================

TEST_CASE("LifeModulators_ControlRateCost", "[.perf]") {
    // 5000 blocks = 5000 * 512 / 48000 ~= 53 s of wall clock, which is inside
    // GrowthEnvelope's 60 s rise, so every trial measures the same work.
    constexpr int kTrials = 5;
    constexpr int kWarmupBlocks = 2000;
    constexpr int kBlocksPerTrial = 5000;

    double sink = 0.0;

    {
        LifeModulatorBundle warmup;
        warmup.prepareAll(kSr48);
        for (int i = 0; i < kWarmupBlocks; ++i) {
            warmup.advanceBlock(kBlockSize);
            sink += warmup.sumOutputs();
        }
    }

    // Best-of-N: the minimum is the least OS-noise-contaminated estimate of the
    // real cost, which is what a regression bound wants.
    double bestNsPerBlock = std::numeric_limits<double>::max();
    for (int trial = 0; trial < kTrials; ++trial) {
        LifeModulatorBundle bundle;
        bundle.prepareAll(kSr48);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            bundle.advanceBlock(kBlockSize);
            sink += bundle.sumOutputs();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs =
            std::chrono::duration<double, std::nano>(end - start).count();
        bestNsPerBlock =
            std::min(bestNsPerBlock, elapsedNs / static_cast<double>(kBlocksPerTrial));
    }

    // Guards against the whole loop being optimized out (a zero-cost "pass").
    REQUIRE(isFiniteValue(static_cast<float>(sink)));

    const double percentOfBudget = (bestNsPerBlock / kBlockBudgetNs) * 100.0;

    WARN("SC-007 combined control-rate cost of all six life modulators:\n"
         << "  measured        : " << bestNsPerBlock << " ns/block (512 samples @ 48 kHz)\n"
         << "  block budget    : " << kBlockBudgetNs << " ns\n"
         << "  % of budget     : " << percentOfBudget << " %  (reference: <= 0.05 %)\n"
         << "  reference figure: " << kReferenceNsPerBlock << " ns/block\n"
         << "  checked-in base : " << kBaselineNsPerBlock << " ns/block  (gate: x"
         << kRegressionFactor << " = " << (kBaselineNsPerBlock * kRegressionFactor)
         << " ns/block)");

    // The binding assertion: a relative regression bound against the
    // checked-in baseline (SC-007). The 0.05 %-of-budget figure above is
    // reported, not asserted, on non-reference machines.
    REQUIRE(bestNsPerBlock <= kBaselineNsPerBlock * kRegressionFactor);
}

// =============================================================================
// Shared contract sweep (FR-001 / FR-004 / FR-006)
// =============================================================================

TEST_CASE("LifeModulators_SharedContractSweep", "[processors][life_modulators]") {
    constexpr int kBlocks = 64;

    // Fixed source range (FR-006): bipolar full scale for five, unipolar for
    // GrowthEnvelope. Never shrinks with any setting.
    const auto checkSource = [](ModulationSource& source, float expectedLo, float expectedHi) {
        const auto range = source.getSourceRange();
        REQUIRE(range.first == expectedLo);
        REQUIRE(range.second == expectedHi);

        const float v = source.getCurrentValue();
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    };

    SECTION("BrownianDrift") {
        BrownianDrift mod;
        mod.prepare(kSr48);
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, -1.0f, 1.0f);
    }

    SECTION("BreathingModulator") {
        BreathingModulator mod;
        mod.prepare(kSr48);
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, -1.0f, 1.0f);
    }

    SECTION("TidalModulator") {
        TidalModulator mod;
        mod.prepare(kSr48);
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, -1.0f, 1.0f);
    }

    SECTION("OrbitModulator") {
        OrbitModulator mod;
        mod.prepare(kSr48);
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, -1.0f, 1.0f);

        // Second axis obeys the same fixed range (FR-042).
        const float y = mod.getY();
        REQUIRE(isFiniteValue(y));
        REQUIRE(y >= -1.0f);
        REQUIRE(y <= 1.0f);
    }

    SECTION("SplineTrajectory") {
        SplineTrajectory mod;
        mod.prepare(kSr48);
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, -1.0f, 1.0f);
    }

    SECTION("GrowthEnvelope") {
        GrowthEnvelope mod;
        mod.prepare(kSr48);
        mod.trigger();
        for (int i = 0; i < kBlocks; ++i) mod.processBlock(kBlockSize);
        checkSource(mod, 0.0f, 1.0f);
    }
}
