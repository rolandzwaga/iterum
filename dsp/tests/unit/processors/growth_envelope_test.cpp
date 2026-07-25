// ==============================================================================
// Layer 2: Processor Tests - GrowthEnvelope (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 6)
// Covers: FR-001..FR-006, FR-061..FR-063, SC-001, SC-002, SC-004, SC-005, SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   brownian_drift_test.cpp is the ONE owner of the global operator new/delete
//   replacements for the `dsp_processors_tests` binary. This file therefore
//   includes only <allocation_detector.h>; including
//   <allocation_operator_overrides.h> from a second TU would be a
//   duplicate-symbol link error.
//
// GrowthEnvelope owns no RNG (it is a deterministic state machine), so SC-004
// is plain like-for-like equality and SC-005 uses the option (a) like-for-like
// comparison rather than a distributional one. Tolerances below are analytic
// (per-sample quantization / smoother completion threshold), never bit-exact
// float goldens; the only exact-equality assertions compare one build against
// itself.
// ==============================================================================

#include <krate/dsp/processors/growth_envelope.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
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

/// Advance `numBlocks` blocks of `blockSize`, capturing getCurrentValue()
/// after every block.
[[nodiscard]] std::vector<float> renderBlocks(GrowthEnvelope& env,
                                              size_t numBlocks,
                                              size_t blockSize) {
    std::vector<float> out;
    out.reserve(numBlocks);
    for (size_t b = 0; b < numBlocks; ++b) {
        env.processBlock(blockSize);
        out.push_back(env.getCurrentValue());
    }
    return out;
}

}  // namespace

// =============================================================================
// SC-001 - Boundedness at both duration extremes, over >= 3x the duration
// =============================================================================

TEST_CASE("GrowthEnvelope_NeverExceedsRange", "[processors][growth_envelope][seraphis]") {
    size_t violations = 0;
    size_t nonFinite = 0;

    for (float duration : {GrowthEnvelope::kMinDuration, GrowthEnvelope::kMaxDuration}) {
        // Render horizon = 3x the configured duration, floored at 60 s
        // (spec.md SC-001: "GrowthEnvelope at 60 s duration renders >= 180 s").
        const double renderSeconds =
            std::max(60.0, 3.0 * static_cast<double>(duration));

        GrowthEnvelope env;
        env.setDuration(duration);
        env.prepare(kSr48);

        const auto range = env.getSourceRange();
        REQUIRE(range.first == 0.0f);
        REQUIRE(range.second == 1.0f);

        // FR-063: before the first trigger the output sits at the bottom.
        REQUIRE(env.getCurrentValue() == 0.0f);

        env.trigger();

        const auto blocks = static_cast<size_t>(renderSeconds * kSr48 /
                                                static_cast<double>(kBlock));
        for (size_t b = 0; b < blocks; ++b) {
            env.processBlock(kBlock);
            const float v = env.getCurrentValue();
            if (!isFiniteValue(v)) ++nonFinite;
            if (v < range.first || v > range.second) ++violations;
        }
    }

    // Per-sample pass at the fastest setting, where the rise is steepest.
    {
        GrowthEnvelope env;
        env.setDuration(GrowthEnvelope::kMinDuration);
        env.prepare(kSr48);
        env.trigger();

        const auto samples = static_cast<size_t>(2.0 * kSr48);
        for (size_t i = 0; i < samples; ++i) {
            env.process();
            const float v = env.getCurrentValue();
            if (!isFiniteValue(v)) ++nonFinite;
            if (v < 0.0f || v > 1.0f) ++violations;
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("GrowthEnvelope_MaxSlewBounded", "[processors][growth_envelope][seraphis]") {
    // Worst case per spec.md:262 -- MINIMUM duration (1 s), full-range rise.
    GrowthEnvelope env;
    env.setDuration(GrowthEnvelope::kMinDuration);
    env.prepare(kSr48);
    env.trigger();

    float prev = env.getCurrentValue();
    double maxDelta = 0.0;

    // Render the whole rise plus a margin so the hold segment is included.
    const auto numSamples = static_cast<size_t>(
        (static_cast<double>(GrowthEnvelope::kMinDuration) + 0.5) * kSr48);
    for (size_t i = 0; i < numSamples; ++i) {
        env.process();
        const float cur = env.getCurrentValue();
        maxDelta = std::max(maxDelta, std::abs(static_cast<double>(cur) -
                                               static_cast<double>(prev)));
        prev = cur;
    }

    // Threshold: 1e-3 of the source-range span. GrowthEnvelope is UNIPOLAR,
    // so the span is 1 and the bound is 1.0e-3 absolute (not the 2.0e-3 that
    // the five bipolar modulators use).
    //
    // Analytic bound (see the header's SC-002 justification): the logistic's
    // maximum slope is k/4 = 2.5 in tau units; normalized by
    // 1/(L(1)-L(0)) ~= 1.0136 and divided by D = 1 s that is ~2.53 per second,
    // i.e. <= 5.3e-5 per sample at 48 kHz. The one-pole output smoother only
    // reduces this; its completion snap is bounded by kCompletionThreshold
    // = 1e-4 (smoother.h:55), also well inside the bound.
    REQUIRE(maxDelta <= 1.0e-3);
}

// =============================================================================
// FR-062 / FR-063 - rise, retrigger continuation, hold, no fall
// =============================================================================

TEST_CASE("GrowthEnvelope_RiseAndHoldBehavior", "[processors][growth_envelope][seraphis]") {
    constexpr float kDuration = 4.0f;
    // Float rounding slack for the monotonicity assertions: the one-pole update
    // current = target + coeff*(current-target) is monotone increasing in exact
    // arithmetic when target is non-decreasing and current <= target, so any
    // decrease is pure float rounding (relative ~1e-7 on values <= 1).
    constexpr float kMonotoneSlack = 1.0e-6f;

    SECTION("idle output is the bottom of the range (FR-063)") {
        GrowthEnvelope env;
        env.setDuration(kDuration);
        env.prepare(kSr48);
        REQUIRE(env.getCurrentValue() == 0.0f);

        // Advancing without a trigger must not start the rise.
        for (int b = 0; b < 200; ++b) {
            env.processBlock(kBlock);
        }
        REQUIRE(env.getCurrentValue() == 0.0f);
    }

    SECTION("rise is monotonic, then holds at the top with no fall (FR-063)") {
        GrowthEnvelope env;
        env.setDuration(kDuration);
        env.prepare(kSr48);
        env.trigger();

        // 3x the duration: covers the full rise and a long hold segment.
        const auto numBlocks = static_cast<size_t>(3.0 * static_cast<double>(kDuration) *
                                                   kSr48 / static_cast<double>(kBlock));
        const auto trace = renderBlocks(env, numBlocks, kBlock);
        REQUIRE(trace.size() > 100u);

        float prev = 0.0f;
        size_t decreases = 0;
        for (float v : trace) {
            if (v < prev - kMonotoneSlack) ++decreases;
            prev = v;
        }
        REQUIRE(decreases == 0u);

        // Reached and holds the top. The smoother snaps exactly to its target
        // once within kCompletionThreshold (1e-4), so the tail is exactly 1.
        REQUIRE(trace.back() >= 1.0f - 1.0e-4f);
        REQUIRE(trace.back() <= 1.0f);

        // No fall segment: the last tenth of the render never dips.
        const size_t tailStart = trace.size() - (trace.size() / 10);
        for (size_t i = tailStart; i < trace.size(); ++i) {
            REQUIRE(trace[i] >= 1.0f - 1.0e-4f);
        }
    }

    SECTION("mid-rise trigger continues, never snaps back (FR-062)") {
        GrowthEnvelope env;
        env.setDuration(kDuration);
        env.prepare(kSr48);
        env.trigger();

        // Advance to roughly half of the rise.
        const auto halfBlocks = static_cast<size_t>(0.5 * static_cast<double>(kDuration) *
                                                    kSr48 / static_cast<double>(kBlock));
        for (size_t b = 0; b < halfBlocks; ++b) {
            env.processBlock(kBlock);
        }

        const float before = env.getCurrentValue();
        REQUIRE(before > 0.05f);   // genuinely mid-rise, not still at the bottom
        REQUIRE(before < 0.95f);

        env.trigger();  // retrigger mid-rise: continuation, NOT a restart
        env.processBlock(kBlock);
        const float after = env.getCurrentValue();

        // A restart would drop the output back toward 0; continuation cannot.
        REQUIRE(after >= before - kMonotoneSlack);

        // The remainder of the rise stays non-decreasing and still completes on
        // the ORIGINAL schedule (a restart would need another full duration).
        const auto remainingBlocks =
            static_cast<size_t>(0.6 * static_cast<double>(kDuration) * kSr48 /
                                static_cast<double>(kBlock));
        const auto tail = renderBlocks(env, remainingBlocks, kBlock);
        float prev = after;
        size_t decreases = 0;
        for (float v : tail) {
            if (v < prev - kMonotoneSlack) ++decreases;
            prev = v;
        }
        REQUIRE(decreases == 0u);
        REQUIRE(tail.back() >= 1.0f - 1.0e-4f);
    }

    SECTION("trigger() after completion is a no-op (FR-062)") {
        GrowthEnvelope env;
        env.setDuration(GrowthEnvelope::kMinDuration);
        env.prepare(kSr48);
        env.trigger();

        const auto blocks = static_cast<size_t>(3.0 * kSr48 / static_cast<double>(kBlock));
        for (size_t b = 0; b < blocks; ++b) {
            env.processBlock(kBlock);
        }
        const float completed = env.getCurrentValue();
        REQUIRE(completed >= 1.0f - 1.0e-4f);

        env.trigger();
        for (int b = 0; b < 200; ++b) {
            env.processBlock(kBlock);
            REQUIRE(env.getCurrentValue() >= 1.0f - 1.0e-4f);
        }
    }
}

// =============================================================================
// SC-004 - Determinism (no RNG: plain like-for-like equality) + reset()
// =============================================================================

TEST_CASE("GrowthEnvelope_Determinism", "[processors][growth_envelope][seraphis]") {
    constexpr size_t kNumBlocks = 600;
    constexpr float kDuration = 3.0f;

    auto render = [&](GrowthEnvelope& env) {
        env.trigger();
        return renderBlocks(env, kNumBlocks, kBlock);
    };

    GrowthEnvelope a;
    a.setDuration(kDuration);
    a.prepare(kSr48);

    GrowthEnvelope b;
    b.setDuration(kDuration);
    b.prepare(kSr48);

    const auto traceA = render(a);
    const auto traceB = render(b);
    REQUIRE(traceA.size() == kNumBlocks);
    // Same build, same call sequence -> exact equality.
    REQUIRE(traceA == traceB);

    // reset() returns to Idle at the bottom of the range...
    a.reset();
    REQUIRE(a.getCurrentValue() == 0.0f);

    // ...and a re-run reproduces the first trajectory exactly.
    const auto traceAfterReset = render(a);
    REQUIRE(traceAfterReset == traceA);
}

// =============================================================================
// SC-005 (option a) - Sample-rate invariance, like-for-like
// =============================================================================

TEST_CASE("GrowthEnvelope_SampleRateInvariant", "[processors][growth_envelope][seraphis]") {
    // GrowthEnvelope is defined purely in seconds and draws no RNG, so the same
    // wall-clock trajectory must appear at both rates (spec SC-005 option (a)).
    // Capture on a 10 ms wall-clock grid, which is an exact integer number of
    // samples at BOTH rates (441 @ 44.1 kHz, 960 @ 96 kHz).
    constexpr float kDuration = GrowthEnvelope::kMinDuration;  // steepest rise
    constexpr double kCaptureSeconds = 0.01;
    constexpr int kNumCaptures = 200;  // 2 s = 2x the duration

    auto trajectory = [&](double sampleRate) {
        GrowthEnvelope env;
        env.setDuration(kDuration);
        env.prepare(sampleRate);
        env.trigger();

        const auto samplesPerCapture =
            static_cast<size_t>(kCaptureSeconds * sampleRate);
        std::vector<float> out;
        out.reserve(static_cast<size_t>(kNumCaptures));
        for (int c = 0; c < kNumCaptures; ++c) {
            for (size_t i = 0; i < samplesPerCapture; ++i) {
                env.process();
            }
            out.push_back(env.getCurrentValue());
        }
        return out;
    };

    const auto at44 = trajectory(44100.0);
    const auto at96 = trajectory(96000.0);
    REQUIRE(at44.size() == static_cast<size_t>(kNumCaptures));
    REQUIRE(at96.size() == at44.size());

    // Tolerance is analytic, not a guess: the one-pole smoother's wall-clock
    // time constant is rate-independent (coeff^(sr*t) = exp(-5000*t/T_ms),
    // smoother.h:91), so the only residual differences are (a) the per-sample
    // quantization of the rise, bounded by the max slope 2.53/s divided by the
    // lower rate -> 5.7e-5, and (b) the smoother's completion snap, bounded by
    // kCompletionThreshold = 1e-4. 1.0e-3 leaves ~6x headroom over their sum.
    double maxDiff = 0.0;
    for (size_t i = 0; i < at44.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(at44[i]) -
                                             static_cast<double>(at96[i])));
    }
    REQUIRE(maxDiff <= 1.0e-3);

    // Sanity: the trajectory actually rises over the captured window.
    REQUIRE(at44.front() < 0.2f);
    REQUIRE(at44.back() >= 1.0f - 1.0e-4f);
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("GrowthEnvelope_NoAllocInProcess", "[processors][growth_envelope][seraphis]") {
    GrowthEnvelope env;
    env.setDuration(GrowthEnvelope::kMaxDuration);
    env.prepare(kSr48);
    env.trigger();
    env.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        env.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        env.process();
    }
    env.trigger();
    env.processBlock(kBlock);
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale (unipolar [0,1])
// =============================================================================

TEST_CASE("GrowthEnvelope_SourceRangeIndependentOfDepth",
          "[processors][growth_envelope][seraphis]") {
    GrowthEnvelope env;
    env.prepare(kSr48);

    // The reported range never shrinks or shifts: not with the duration
    // setting, and not with the phase (Idle / Rising / Complete).
    for (float duration : {GrowthEnvelope::kMinDuration, 12.5f, GrowthEnvelope::kMaxDuration}) {
        env.setDuration(duration);
        const auto range = env.getSourceRange();
        REQUIRE(range.first == 0.0f);
        REQUIRE(range.second == 1.0f);
    }

    env.setDuration(GrowthEnvelope::kMinDuration);
    env.reset();
    env.trigger();
    env.processBlock(kBlock);  // Rising
    {
        const auto range = env.getSourceRange();
        REQUIRE(range.first == 0.0f);
        REQUIRE(range.second == 1.0f);
    }

    const auto blocks = static_cast<size_t>(3.0 * kSr48 / static_cast<double>(kBlock));
    for (size_t b = 0; b < blocks; ++b) {
        env.processBlock(kBlock);  // -> Complete
    }
    {
        const auto range = env.getSourceRange();
        REQUIRE(range.first == 0.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Output is well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("GrowthEnvelope_OutputDefinedAfterPrepare",
          "[processors][growth_envelope][seraphis]") {
    GrowthEnvelope env;
    env.setDuration(7.5f);
    env.prepare(kSr48);

    const auto range = env.getSourceRange();

    {
        const float v = env.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v == 0.0f);  // FR-063: bottom of range while Idle
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }

    env.trigger();
    for (int b = 0; b < 100; ++b) {
        env.processBlock(kBlock);
    }
    env.reset();

    {
        const float v = env.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v == 0.0f);
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("GrowthEnvelope_EdgeCases", "[processors][growth_envelope][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety: prepare(0) made sampleDtSeconds_ non-finite, poisoning the
        // elapsed-time accumulator (and hence tau = elapsed/duration).
        // prepare() floors the sample rate at 1 Hz. Reaching the assertions at
        // all is half of what this section proves.
        for (double sr : {0.0, -48000.0, 0.25}) {
            GrowthEnvelope env;
            env.setDuration(GrowthEnvelope::kMinDuration);
            env.prepare(sr);
            env.trigger();

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < 0.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                env.processBlock(kBlock);
                observe(env.getCurrentValue());
            }
            for (int i = 0; i < 2000; ++i) {
                env.process();
                observe(env.getCurrentValue());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        GrowthEnvelope withZeros;
        GrowthEnvelope reference;
        for (GrowthEnvelope* e : {&withZeros, &reference}) {
            e->setDuration(5.0f);
            e->prepare(kSr48);
            e->trigger();
        }

        for (int b = 0; b < 50; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float before = withZeros.getCurrentValue();
        for (int i = 0; i < 10; ++i) {
            withZeros.processBlock(0);
        }
        REQUIRE(withZeros.getCurrentValue() == before);

        // Internal state (elapsed time, smoother) is untouched as well.
        const auto a = renderBlocks(withZeros, 50u, kBlock);
        const auto b2 = renderBlocks(reference, 50u, kBlock);
        REQUIRE(a == b2);
    }

    SECTION("duration is clamped to [kMinDuration, kMaxDuration]") {
        GrowthEnvelope env;
        env.prepare(kSr48);

        env.setDuration(0.0f);
        REQUIRE(env.getDuration() == GrowthEnvelope::kMinDuration);

        env.setDuration(1000.0f);
        REQUIRE(env.getDuration() == GrowthEnvelope::kMaxDuration);
    }

    SECTION("trigger() after completion holds the top") {
        GrowthEnvelope env;
        env.setDuration(GrowthEnvelope::kMinDuration);
        env.prepare(kSr48);
        env.trigger();

        const auto blocks = static_cast<size_t>(3.0 * kSr48 / static_cast<double>(kBlock));
        for (size_t b = 0; b < blocks; ++b) {
            env.processBlock(kBlock);
        }
        REQUIRE(env.getCurrentValue() >= 1.0f - 1.0e-4f);

        env.trigger();
        env.trigger();
        env.processBlock(kBlock);
        REQUIRE(env.getCurrentValue() >= 1.0f - 1.0e-4f);
        REQUIRE(env.getCurrentValue() <= 1.0f);
    }
}
