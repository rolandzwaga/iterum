// ==============================================================================
// Layer 2: Processor Tests - OrbitModulator (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 4)
// Covers: FR-001..FR-006, FR-041..FR-043, SC-001, SC-002, SC-004, SC-005, SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   The global operator new/delete replacements for the `dsp_processors_tests`
//   binary are owned by ONE translation unit -- brownian_drift_test.cpp, which
//   includes <allocation_operator_overrides.h>. This file therefore includes
//   only <allocation_detector.h>; pulling in the overrides header from a second
//   TU is a duplicate-symbol link error.
//
// Statistical thresholds are MEASURED tolerances, never bit-exact float
// goldens. The only exact-equality assertions are same-build determinism
// comparisons (SC-004), which compare one compiler's output against itself.
// ==============================================================================

#include <krate/dsp/processors/orbit_modulator.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr size_t kBlock = 512;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// One captured pair of axes.
struct XY {
    std::vector<float> x;
    std::vector<float> y;
};

/// Advance exactly `numBlocks` blocks, capturing both axes once per block.
[[nodiscard]] XY renderNBlocks(OrbitModulator& orbit, size_t numBlocks, size_t blockSize) {
    XY out;
    out.x.reserve(numBlocks);
    out.y.reserve(numBlocks);
    for (size_t b = 0; b < numBlocks; ++b) {
        orbit.processBlock(blockSize);
        out.x.push_back(orbit.getCurrentValue());
        out.y.push_back(orbit.getY());
    }
    return out;
}

/// Advance `seconds` of wall clock in `blockSize` blocks, capturing both axes
/// once per block.
[[nodiscard]] XY renderBlocks(OrbitModulator& orbit,
                              double seconds,
                              double sampleRate,
                              size_t blockSize) {
    const auto totalBlocks =
        static_cast<size_t>(seconds * sampleRate / static_cast<double>(blockSize));
    return renderNBlocks(orbit, totalBlocks, blockSize);
}

[[nodiscard]] double meanOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (float x : v) acc += static_cast<double>(x);
    return acc / static_cast<double>(v.size());
}

[[nodiscard]] double rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (float x : v) {
        const double d = static_cast<double>(x);
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(v.size()));
}

[[nodiscard]] double maxAbsOf(const std::vector<float>& v) {
    double m = 0.0;
    for (float x : v) m = std::max(m, std::abs(static_cast<double>(x)));
    return m;
}

}  // namespace

// =============================================================================
// SC-001 / FR-042 / FR-043 - Boundedness on BOTH axes at every extreme
// =============================================================================

TEST_CASE("OrbitModulator_NeverExceedsRange", "[processors][orbit_modulator][seraphis]") {
    const std::array<float, 2> rateGrid{OrbitModulator::kMinRate, OrbitModulator::kMaxRate};
    const std::array<float, 2> couplingGrid{0.0f, 1.0f};
    const std::array<float, 3> growthGrid{-1.0f, 0.0f, 1.0f};
    const std::array<float, 2> depthGrid{0.0f, 1.0f};

    size_t violations = 0;
    size_t nonFinite = 0;

    for (float rate : rateGrid) {
        for (float coupling : couplingGrid) {
            for (float growth : growthGrid) {
                for (float depth : depthGrid) {
                    OrbitModulator orbit;
                    orbit.setSeed(1618u);
                    orbit.setRate(rate);
                    orbit.setCoupling(coupling);
                    orbit.setGrowth(growth);
                    orbit.setDepth(depth);
                    orbit.prepare(kSr48);

                    const auto range = orbit.getSourceRange();
                    REQUIRE(range.first == -1.0f);
                    REQUIRE(range.second == 1.0f);

                    // Render horizon = 3x the longest configured period
                    // (SC-001): at kMinRate = 0.01 Hz the period is 100 s, so
                    // 300 s; the fast setting (0.5 Hz, 2 s period) uses the
                    // 60 s floor, which is 30 periods.
                    const double seconds = (rate <= 0.02f) ? 300.0 : 60.0;
                    const auto blocks =
                        static_cast<size_t>(seconds * kSr48 / static_cast<double>(kBlock));
                    for (size_t b = 0; b < blocks; ++b) {
                        orbit.processBlock(kBlock);
                        const float vx = orbit.getCurrentValue();
                        const float vy = orbit.getY();
                        if (!isFiniteValue(vx) || !isFiniteValue(vy)) ++nonFinite;
                        if (vx < range.first || vx > range.second) ++violations;
                        if (vy < range.first || vy > range.second) ++violations;
                    }
                }
            }
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);

    // The zero-violation counts above would also be reported by a broken
    // implementation whose values are only in range BECAUSE getCurrentValue() /
    // getY() clamp. Prove the clamp is inert rather than trusting it: depth
    // enters as a pure multiplier (raw = depth * radius * sin(phi), radius in
    // [kRadiusMin, 1]) and neither the phases nor the radius depend on it, so
    // halving the depth must halve the excursion on both axes. If the depth-1
    // render were being clamped, its peak would sit pinned at 1.0 while twice
    // the depth-0.5 peak reported the larger true value.
    //
    // Peaks (not per-sample proportionality) are the right comparison: the
    // output smoothers snap to target below an ABSOLUTE completion threshold
    // (smoother.h:199), which is not a linear operation, but its lag vanishes at
    // the extrema where the target's slope is zero.
    {
        const auto peakOf = [](float depth) {
            OrbitModulator orbit;
            orbit.setSeed(90210u);
            orbit.setRate(OrbitModulator::kMaxRate);
            orbit.setCoupling(1.0f);
            orbit.setGrowth(0.0f);  // radius pinned at 1 -> full-scale swing
            orbit.setDepth(depth);
            orbit.prepare(kSr48);

            double peak = 0.0;
            const auto numSamples = static_cast<size_t>(20.0 * kSr48);
            for (size_t i = 0; i < numSamples; ++i) {
                orbit.process();
                peak = std::max(peak, std::abs(static_cast<double>(orbit.getCurrentValue())));
                peak = std::max(peak, std::abs(static_cast<double>(orbit.getY())));
            }
            return peak;
        };

        const double peakFull = peakOf(1.0f);
        const double peakHalf = peakOf(0.5f);

        // The full-depth render really does run up against the bound, so the
        // comparison below is made exactly where a clamp would bite.
        REQUIRE(peakFull > 0.99);
        REQUIRE(peakFull <= 1.0);
        // 1 % of full scale: the smoothers' peak attenuation at these rates is
        // ~1e-4 and applies equally to both renders, so this only rejects real
        // clamping.
        REQUIRE(std::abs(peakFull - (2.0 * peakHalf)) <= 0.01);
    }
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("OrbitModulator_MaxSlewBounded", "[processors][orbit_modulator][seraphis]") {
    // Worst case per spec.md:260 -- MAXIMUM orbital rate and MAXIMUM coupling at
    // MAXIMUM depth, measured on both axes. growth = 0 keeps the radius at its
    // initial 1.0, i.e. the full-scale swing.
    OrbitModulator orbit;
    orbit.setSeed(2024u);
    orbit.setRate(OrbitModulator::kMaxRate);
    orbit.setCoupling(1.0f);
    orbit.setGrowth(0.0f);
    orbit.setDepth(1.0f);
    orbit.prepare(kSr48);

    float prevX = orbit.getCurrentValue();
    float prevY = orbit.getY();
    double maxDeltaX = 0.0;
    double maxDeltaY = 0.0;

    // 4 s = 2 full periods at kMaxRate (0.5 Hz), rendered per sample.
    const auto numSamples = static_cast<size_t>(4.0 * kSr48);
    for (size_t i = 0; i < numSamples; ++i) {
        orbit.process();
        const float curX = orbit.getCurrentValue();
        const float curY = orbit.getY();
        maxDeltaX = std::max(
            maxDeltaX, std::abs(static_cast<double>(curX) - static_cast<double>(prevX)));
        maxDeltaY = std::max(
            maxDeltaY, std::abs(static_cast<double>(curY) - static_cast<double>(prevY)));
        prevX = curX;
        prevY = curY;
    }

    // Threshold: 1e-3 of the source-range span (span = 2) = 2.0e-3 absolute.
    // Analytic bound (see the header's SC-002 proof): the raw axis slope is
    // |d(sin phi)/dt| <= omega_max + k_max = 2*pi*0.5 + 1 = 4.14 /s, i.e.
    // <= 8.6e-5 per sample at 48 kHz, and the 20 ms output smoother cannot
    // exceed that by more than its own bounded lag response (~9.3e-5).
    REQUIRE(maxDeltaX <= 2.0e-3);
    REQUIRE(maxDeltaY <= 2.0e-3);
}

// =============================================================================
// SC-004 - Seeded determinism and reset() rewind (both axes)
// =============================================================================

TEST_CASE("OrbitModulator_SeededDeterminism", "[processors][orbit_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    auto render = [&](std::uint32_t seed) {
        OrbitModulator orbit;
        orbit.setSeed(seed);
        orbit.setRate(0.2f);
        orbit.setCoupling(0.4f);
        orbit.setGrowth(0.0f);
        orbit.setDepth(1.0f);
        orbit.prepare(kSr48);
        return renderNBlocks(orbit, kNumBlocks, kBlock);
    };

    const auto a = render(1234u);
    const auto b = render(1234u);
    REQUIRE(a.x.size() == kNumBlocks);
    REQUIRE(a.y.size() == kNumBlocks);
    // Same build, same seed, same call sequence -> exact equality on both axes.
    REQUIRE(a.x == b.x);
    REQUIRE(a.y == b.y);

    const auto c = render(9999u);
    REQUIRE(c.x.size() == kNumBlocks);
    REQUIRE(a.x != c.x);
    REQUIRE(a.y != c.y);
}

TEST_CASE("OrbitModulator_ResetRewindsToSeed", "[processors][orbit_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    OrbitModulator orbit;
    orbit.setSeed(4711u);
    orbit.setRate(0.3f);
    orbit.setCoupling(0.6f);
    orbit.setGrowth(-0.5f);
    orbit.setDepth(0.8f);
    orbit.prepare(kSr48);

    const auto first = renderNBlocks(orbit, kNumBlocks, kBlock);

    orbit.reset();

    const auto second = renderNBlocks(orbit, kNumBlocks, kBlock);

    REQUIRE(first.x == second.x);
    REQUIRE(first.y == second.y);
}

// =============================================================================
// SC-005 (option a) - Sample-rate invariance, like-for-like
// =============================================================================

TEST_CASE("OrbitModulator_SampleRateInvariant", "[processors][orbit_modulator][seraphis]") {
    // OrbitModulator draws its RNG exactly twice (the two initial phases) and
    // its dynamics are defined in seconds, so 44.1 kHz and 96 kHz produce the
    // SAME wall-clock trajectory up to control-rate forward-Euler error.
    // Per spec SC-005 option (a) this is a like-for-like comparison.
    constexpr double kSeconds = 60.0;  // 6 full periods at 0.1 Hz

    auto measure = [&](double sampleRate) {
        OrbitModulator orbit;
        orbit.setSeed(777u);
        orbit.setRate(0.1f);
        orbit.setCoupling(0.3f);
        orbit.setGrowth(0.0f);
        orbit.setDepth(1.0f);
        orbit.prepare(sampleRate);
        return renderBlocks(orbit, kSeconds, sampleRate, kBlock);
    };

    const auto a = measure(44100.0);
    const auto b = measure(96000.0);
    REQUIRE(a.x.size() > 1000u);
    REQUIRE(b.x.size() > 1000u);

    const double rmsXa = rmsOf(a.x);
    const double rmsXb = rmsOf(b.x);
    const double rmsYa = rmsOf(a.y);
    const double rmsYb = rmsOf(b.y);

    // A full-depth, unit-radius sine has RMS ~0.707; this guards against a
    // silent (all-zero) render passing the comparison trivially.
    REQUIRE(rmsXa > 0.3);
    REQUIRE(rmsYa > 0.3);

    // Tolerance: 5% relative on RMS. The only difference between the two rates
    // is the forward-Euler step size (32/44100 s vs 32/96000 s) applied to a
    // bounded coupling term, so the realised difference is orders of magnitude
    // below this bound.
    REQUIRE(std::abs(rmsXa - rmsXb) / rmsXa <= 0.05);
    REQUIRE(std::abs(rmsYa - rmsYb) / rmsYa <= 0.05);

    // The source is ~zero-mean, so a RELATIVE mean comparison divides by ~0 and
    // is undefined. Compare absolutely, in source-range-span units (span = 2):
    // 0.05 absolute = 2.5% of span.
    REQUIRE(std::abs(meanOf(a.x) - meanOf(b.x)) <= 0.05);
    REQUIRE(std::abs(meanOf(a.y) - meanOf(b.y)) <= 0.05);
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("OrbitModulator_NoAllocInProcess", "[processors][orbit_modulator][seraphis]") {
    OrbitModulator orbit;
    orbit.setSeed(64u);
    orbit.setRate(0.25f);
    orbit.setCoupling(0.5f);
    orbit.setGrowth(0.2f);
    orbit.setDepth(1.0f);
    orbit.prepare(kSr48);
    orbit.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        orbit.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        orbit.process();
    }
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale
// =============================================================================

TEST_CASE("OrbitModulator_SourceRangeIndependentOfDepth",
          "[processors][orbit_modulator][seraphis]") {
    OrbitModulator orbit;
    orbit.prepare(kSr48);

    for (float depth : {0.0f, 0.5f, 1.0f}) {
        orbit.setDepth(depth);
        const auto range = orbit.getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Both axes are well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("OrbitModulator_OutputDefinedAfterPrepare",
          "[processors][orbit_modulator][seraphis]") {
    OrbitModulator orbit;
    orbit.setSeed(11u);
    orbit.setRate(0.4f);
    orbit.setCoupling(0.9f);
    orbit.setGrowth(0.7f);
    orbit.setDepth(0.6f);
    orbit.prepare(kSr48);

    const auto range = orbit.getSourceRange();

    {
        // No intervening process()/processBlock().
        const float vx = orbit.getCurrentValue();
        const float vy = orbit.getY();
        REQUIRE(isFiniteValue(vx));
        REQUIRE(isFiniteValue(vy));
        REQUIRE(vx >= range.first);
        REQUIRE(vx <= range.second);
        REQUIRE(vy >= range.first);
        REQUIRE(vy <= range.second);
    }

    for (int i = 0; i < 100; ++i) orbit.processBlock(kBlock);
    orbit.reset();

    {
        const float vx = orbit.getCurrentValue();
        const float vy = orbit.getY();
        REQUIRE(isFiniteValue(vx));
        REQUIRE(isFiniteValue(vy));
        REQUIRE(vx >= range.first);
        REQUIRE(vx <= range.second);
        REQUIRE(vy >= range.first);
        REQUIRE(vy <= range.second);
    }
}

// =============================================================================
// Edge cases (FR-043 radius extremes, processBlock(0))
// =============================================================================

TEST_CASE("OrbitModulator_EdgeCases", "[processors][orbit_modulator][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety (the worst failure class there is): prepare(0) made
        // controlDtSeconds_ non-finite, so advanceControlStep() fed an infinite
        // phase into wrapPhase(), whose subtract-in-a-loop wrap never
        // terminated - an audio-thread hang, not merely a wrong value.
        // prepare() now floors the sample rate at 1 Hz and wrapPhase() is O(1).
        // Reaching the assertions at all is half of what this section proves.
        for (double sr : {0.0, -48000.0, 0.25}) {
            OrbitModulator orbit;
            orbit.setSeed(1234u);
            orbit.setRate(OrbitModulator::kMaxRate);
            orbit.setCoupling(1.0f);
            orbit.setGrowth(1.0f);
            orbit.setDepth(1.0f);
            orbit.prepare(sr);

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                orbit.processBlock(kBlock);
                observe(orbit.getCurrentValue());
                observe(orbit.getY());
            }
            for (int i = 0; i < 2000; ++i) {
                orbit.process();
                observe(orbit.getCurrentValue());
                observe(orbit.getY());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        OrbitModulator withZeros;
        OrbitModulator reference;
        for (OrbitModulator* o : {&withZeros, &reference}) {
            o->setSeed(555u);
            o->setRate(0.3f);
            o->setCoupling(0.5f);
            o->setGrowth(0.0f);
            o->setDepth(1.0f);
            o->prepare(kSr48);
        }

        for (int b = 0; b < 50; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float beforeX = withZeros.getCurrentValue();
        const float beforeY = withZeros.getY();
        for (int i = 0; i < 10; ++i) withZeros.processBlock(0);
        REQUIRE(withZeros.getCurrentValue() == beforeX);
        REQUIRE(withZeros.getY() == beforeY);

        // State (phases, radius, smoothers) is untouched as well.
        std::vector<float> a, b2;
        for (int i = 0; i < 50; ++i) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
            a.push_back(withZeros.getCurrentValue());
            a.push_back(withZeros.getY());
            b2.push_back(reference.getCurrentValue());
            b2.push_back(reference.getY());
        }
        REQUIRE(a == b2);
    }

    SECTION("growth = -1 floors the radius, it never sticks at a point") {
        OrbitModulator orbit;
        orbit.setSeed(808u);
        orbit.setRate(OrbitModulator::kMaxRate);  // 2 s period
        orbit.setCoupling(0.2f);
        orbit.setGrowth(-1.0f);
        orbit.setDepth(1.0f);
        orbit.prepare(kSr48);

        // Settle well past the full contraction: 1.0 -> kRadiusMin takes
        // (1 - kRadiusMin) / kRadiusRate seconds.
        const double settleSeconds =
            3.0 * static_cast<double>(1.0f - OrbitModulator::kRadiusMin) /
            static_cast<double>(OrbitModulator::kRadiusRate);
        static_cast<void>(renderBlocks(orbit, settleSeconds, kSr48, kBlock));

        // 10 s = 5 further periods at the contracted radius.
        const auto trace = renderBlocks(orbit, 10.0, kSr48, kBlock);
        REQUIRE(trace.x.size() > 100u);

        // With the radius floored at kRadiusMin = 0.05 the axes still swing
        // +/-0.05 (RMS ~0.035). A radius that decayed to 0 would give ~0.
        REQUIRE(rmsOf(trace.x) > 0.01);
        REQUIRE(rmsOf(trace.y) > 0.01);
        REQUIRE(maxAbsOf(trace.x) > 0.02);
        REQUIRE(maxAbsOf(trace.y) > 0.02);

        // ...and still bounded.
        REQUIRE(maxAbsOf(trace.x) <= 1.0);
        REQUIRE(maxAbsOf(trace.y) <= 1.0);
    }

    SECTION("growth = +1 clamps the radius, it never diverges") {
        OrbitModulator orbit;
        orbit.setSeed(909u);
        orbit.setRate(OrbitModulator::kMaxRate);
        orbit.setCoupling(1.0f);
        orbit.setGrowth(1.0f);
        orbit.setDepth(1.0f);
        orbit.prepare(kSr48);

        const auto trace = renderBlocks(orbit, 60.0, kSr48, kBlock);
        REQUIRE(trace.x.size() > 1000u);

        bool allFinite = true;
        for (size_t i = 0; i < trace.x.size(); ++i) {
            if (!isFiniteValue(trace.x[i]) || !isFiniteValue(trace.y[i])) allFinite = false;
        }
        REQUIRE(allFinite);
        REQUIRE(maxAbsOf(trace.x) <= 1.0);
        REQUIRE(maxAbsOf(trace.y) <= 1.0);
        // The orbit is alive at full radius, not pinned to a rail.
        REQUIRE(rmsOf(trace.x) > 0.3);
        REQUIRE(rmsOf(trace.y) > 0.3);
    }
}
