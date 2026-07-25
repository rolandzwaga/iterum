// ==============================================================================
// Layer 2: DSP Processor - SplineTrajectory (Seraphis Life Modulator)
// ==============================================================================
// A Catmull-Rom trajectory through a ring of random-walk waypoints. Waypoints
// are regenerated ahead of the playhead from a seeded Xorshift32, so the path
// advances indefinitely with no allocation and no repeat.
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-051..FR-054)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 5)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-051) - uniform Catmull-Rom over a 4-waypoint ring
// ------------------------------------------------------------------------------
//   The ring wp_[0..3] holds p0, p1, p2, p3. The playhead parameter u advances
//   inside the segment p1 -> p2, with p0/p3 supplying the end tangents:
//
//     q(u) = 0.5 * [ 2*p1
//                  + (-p0 + p2) * u
//                  + (2*p0 - 5*p1 + 4*p2 - p3) * u^2
//                  + (-p0 + 3*p1 - 3*p2 + p3) * u^3 ]
//
//   u advances by du = 1 / (interval * sampleRate) per sample. On u >= 1 the
//   ring rotates (drop p0, shift, draw a fresh p3) and u wraps by 1. A block
//   longer than one interval loops that rotation, drawing one fresh waypoint per
//   consumed waypoint, so the RNG stream stays in step with wall-clock time and
//   the ring can never be indexed past wp_[3].
//
// ------------------------------------------------------------------------------
// C1 CONTINUITY (FR-053) - by construction, and NOT decimated away
// ------------------------------------------------------------------------------
//   Evaluating the polynomial above at the segment ends gives
//
//     q(1)  = p2                    q'(1) = 0.5 * (p3 - p1)
//     q(0)  = p1                    q'(0) = 0.5 * (p2 - p0)
//
//   After the rotation the new (p0, p1, p2) are the old (p1, p2, p3), so the
//   next segment opens at q(0) = old p2 with slope 0.5 * (old p3 - old p1):
//   both the value and the first derivative match across the join. du is
//   constant, so continuity in u is continuity in time.
//
//   Because C1 is the deliverable, the output is evaluated PER SAMPLE from q(u)
//   and is NOT routed through an output smoother -- a smoother would only add a
//   lag, and decimating the output to the control grid would replace the C1
//   curve with a staircase. processBlock() therefore advances u by the whole
//   block and re-evaluates q once; process() advances one sample at a time.
//
// ------------------------------------------------------------------------------
// BOUNDEDNESS (FR-054) - structural, no clamp required
// ------------------------------------------------------------------------------
//   Waypoints are drawn in [-kWaypointMax, +kWaypointMax] = [-0.8, +0.8]. The
//   uniform Catmull-Rom Lebesgue constant is
//
//     max_u sum_i |b_i(u)| = 1.25       (attained at u = 0.5, where the basis
//                                        weights are -0.0625, 0.5625, 0.5625,
//                                        -0.0625)
//
//   so |q(u)| <= 1.25 * 0.8 = 1.0 for EVERY waypoint sequence, and the output
//   depth * q(u) with depth in [0,1] never leaves [-1, +1]. The clamp in
//   updateOutput() is an inert safety net; it never engages, which is what keeps
//   the delivered signal C1 (a clamp that bit would introduce a slope kink).
//
// ------------------------------------------------------------------------------
// SC-002 ANALYTIC JUSTIFICATION - bounded per-sample slew
// ------------------------------------------------------------------------------
//   The steepest configuration is the shortest spacing at full depth. Over one
//   segment the value travels at most the segment amplitude (<= 2, the source
//   range span) in one interval, and the Catmull-Rom tangent overshoot factor is
//   ~1.5, so
//
//     |dq/dt| <= (segment amplitude / interval) * ~1.5
//             <= (2 / 0.5) * 1.5 = 6 per second
//
//   Per sample at 48 kHz that is 6 / 48000 = 1.25e-4, comfortably inside
//   SC-002's threshold of 1e-3 of the source-range span = 2.0e-3 absolute.
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - waypoints are finite by
//   construction, the cubic is a finite combination of them, prepare() floors
//   the sample rate so du can never be infinite (which would spin the rotation
//   loop forever), and the output is clamped to the source range.
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Slow, bounded, C1-continuous path through random waypoints.
///
/// Implements the ModulationSource interface. The output is evaluated per
/// sample from the Catmull-Rom polynomial; a block consumer can advance it once
/// per block via processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with
///      the depth setting; depth scales the signal inside that range).
class SplineTrajectory : public ModulationSource {
public:
    /// Shortest waypoint spacing (seconds).
    static constexpr float kMinInterval = 0.5f;
    /// Longest waypoint spacing (seconds).
    static constexpr float kMaxInterval = 30.0f;
    /// Waypoint magnitude limit. See the FR-054 boundedness note above.
    static constexpr float kWaypointMax = 0.8f;

    static constexpr float kDefaultInterval = 2.0f;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr std::uint32_t kDefaultSplineSeed = 0x5F11u;

    SplineTrajectory() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Derive the per-sample playhead increment and initialise state.
    /// After this call getCurrentValue() is well defined without any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        updateIncrement();
        initState();
    }

    /// @brief Rewind to the exact post-prepare state (RNG included).
    /// Keeps the configured sample rate; the same seed re-renders identically.
    void reset() noexcept {
        initState();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Set the RNG seed (FR-005). Plain non-virtual member:
    /// ModulationSource declares only getCurrentValue()/getSourceRange() as
    /// virtuals (modulation_source.h:37,41).
    /// @param seedValue Seed (0 is substituted by Xorshift32's default)
    void setSeed(std::uint32_t seedValue) noexcept {
        configuredSeed_ = seedValue;
        rng_.seed(seedValue);
    }

    /// @brief Waypoint spacing in seconds (FR-052), clamped to
    /// [kMinInterval, kMaxInterval]. Changing it mid-flight only changes the
    /// playhead speed, so the output stays continuous.
    /// @param seconds Desired spacing
    void setWaypointInterval(double seconds) noexcept {
        interval_ = std::clamp(seconds,
                               static_cast<double>(kMinInterval),
                               static_cast<double>(kMaxInterval));
        updateIncrement();
    }

    /// @brief Output depth. Scales the signal INSIDE the fixed range (FR-006).
    /// @param normalized 0..1
    void setDepth(float normalized) noexcept {
        depth_ = std::clamp(normalized, 0.0f, 1.0f);
        updateOutput();
    }

    [[nodiscard]] double getWaypointInterval() const noexcept { return interval_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample and re-evaluate the spline.
    void process() noexcept {
        advance(1);
    }

    /// @brief Advance a whole block (FR-003). processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        if (numSamples == 0u) {
            return;
        }
        advance(numSamples);
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-001)
    // -------------------------------------------------------------------------

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return value_;
    }

    /// @brief Fixed at polarity full scale, independent of depth (FR-006).
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

private:
    void updateIncrement() noexcept {
        du_ = 1.0 / (interval_ * sampleRate_);
    }

    [[nodiscard]] float drawWaypoint() noexcept {
        return rng_.nextFloat() * kWaypointMax;
    }

    void initState() noexcept {
        rng_.seed(configuredSeed_);
        for (float& w : wp_) {
            w = drawWaypoint();
        }
        u_ = 0.0;
        updateOutput();
    }

    void rotateWaypoints() noexcept {
        wp_[0] = wp_[1];
        wp_[1] = wp_[2];
        wp_[2] = wp_[3];
        wp_[3] = drawWaypoint();
    }

    /// Uniform Catmull-Rom evaluation at the current playhead. Accumulated in
    /// double (u_ is double) and narrowed once at the output.
    [[nodiscard]] double evaluateSpline() const noexcept {
        const double p0 = static_cast<double>(wp_[0]);
        const double p1 = static_cast<double>(wp_[1]);
        const double p2 = static_cast<double>(wp_[2]);
        const double p3 = static_cast<double>(wp_[3]);

        const double c0 = 2.0 * p1;
        const double c1 = -p0 + p2;
        const double c2 = (2.0 * p0) - (5.0 * p1) + (4.0 * p2) - p3;
        const double c3 = -p0 + (3.0 * p1) - (3.0 * p2) + p3;

        return 0.5 * (c0 + (u_ * (c1 + (u_ * (c2 + (u_ * c3))))));
    }

    void updateOutput() noexcept {
        const double q = evaluateSpline();
        // Inert safety net: |q| <= 1 by the Lebesgue bound (see the header note),
        // so this clamp never engages and the signal stays C1.
        value_ = std::clamp(static_cast<float>(static_cast<double>(depth_) * q),
                            -1.0f, 1.0f);
    }

    void advance(size_t numSamples) noexcept {
        u_ += du_ * static_cast<double>(numSamples);
        while (u_ >= 1.0) {
            u_ -= 1.0;
            rotateWaypoints();
        }
        updateOutput();
    }

    /// Waypoint ring: p0, p1, p2, p3. Fixed size - no allocation ever.
    std::array<float, 4> wp_{};

    double sampleRate_ = 44100.0;
    double interval_ = static_cast<double>(kDefaultInterval);
    double du_ = 1.0 / (static_cast<double>(kDefaultInterval) * 44100.0);
    double u_ = 0.0;  ///< playhead inside the current segment, [0, 1)

    float depth_ = kDefaultDepth;
    float value_ = 0.0f;  ///< last evaluated output

    std::uint32_t configuredSeed_ = kDefaultSplineSeed;
    Xorshift32 rng_{kDefaultSplineSeed};
};

}  // namespace DSP
}  // namespace Krate
