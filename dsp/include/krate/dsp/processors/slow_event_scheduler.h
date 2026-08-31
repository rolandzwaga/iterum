// ==============================================================================
// Layer 2: DSP Processor - SlowEventScheduler (Vorago Phase 1)
// ==============================================================================
// A very slow, seeded "something happens" clock. It draws an inter-event period
// from a uniform range (tens of seconds), waits, then plays a single smooth
// attack / hold / release swell at a drawn depth, polarity and target index.
// Vorago routes it into whatever the target index names (a body resonance, a
// spectral tilt, a reverb size, ...) so the drone visibly *moves* on a timescale
// far longer than any LFO.
//
// Spec:  specs/vorago-phase1-events-modulation/spec.md  (FR-051..FR-067)
// Plan:  specs/vorago-phase1-events-modulation/plan.md
// Tasks: specs/vorago-phase1-events-modulation/tasks.md (T010)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; every member is a fixed-size scalar - there is
//   no pool, no ring buffer and no container anywhere in this class)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + stdlib only - no Layer 1 header is
//   needed because the envelope is evaluated in closed form, so there is nothing
//   to smooth)
//
// ------------------------------------------------------------------------------
// SINGLE-CLOCK DESIGN - the whole component turns on this
// ------------------------------------------------------------------------------
//   The output, the envelope, the phase and the active flag are PURE FUNCTIONS
//   of `elapsedSamples_` and the four latched boundary offsets
//   (attackEndSamples_, holdEndSamples_, releaseEndSamples_, periodSamples_).
//   Nothing about the output is computed incrementally, so nothing can drift,
//   step or accumulate:
//
//     * FR-064 (non-accumulating timeline) is satisfied BY CONSTRUCTION - an
//       onset carries the remainder (`elapsedSamples_ -= periodSamples_`) and is
//       never zeroed, so the k-th onset lands at exactly
//       sum_{j<k} period_j * sampleRate, quantised up to the control grid and
//       nothing more. That is what makes SC-012's CUMULATIVE 32/44100 s onset
//       bound attainable across 50 events.
//     * FR-066 (mid-event latch) needs no extra machinery: the setters write
//       stored configuration and NOTHING else, and nothing in the output path
//       reads stored configuration. A setter storm therefore cannot move a
//       running envelope by one ULP (SC-018 asserts worstLatchDrift == 0).
//
// ------------------------------------------------------------------------------
// POLL-RATE CONTRACT (FR-065) - unlike BrownianDrift, the getters COMPUTE
// ------------------------------------------------------------------------------
//   BrownianDrift::getCurrentValue() (brownian_drift.h:212-214) is a plain
//   member load: its value is written inside the control-rate update, so a
//   consumer reads it once per block. This class is the opposite. The envelope
//   is a per-sample quantity, so getCurrentValue() / getEnvelopeValue() evaluate
//   riseShape() on every call and NO ENVELOPE VALUE is cached. Consumers MUST
//   poll them per sample; polling once per block yields a 32-sample staircase
//   whose per-sample slew at short attacks (2.5e-3 at a 0.5 s attack) breaches
//   the 2.0e-3 click bound this component exists to respect.
//
//   Because the poll is per sample, the read path is written for THREE LOADS,
//   not for brevity: envelopeAt() tests the idle case first (one load settles
//   the overwhelmingly common branch) and getCurrentValue() multiplies by the
//   draw-time latch signedDepth_ instead of re-reading and re-converting
//   active_.polarity and active_.depth. Both are value-preserving rearrangements
//   - signedDepth_ is polarity * depth in the original order, and the branch
//   tests are the exact complements of the ones they replaced - and together
//   they are worth about a third of SC-014's whole measured control-rate cost
//   (vorago_p1_perf_test.cpp:83-101).
//
//   That per-call cost is also why the rise shape is the smootherstep
//   POLYNOMIAL and not a raised cosine (FR-056 as amended, plan section 8.7).
//   Measured (WSL g++ 13 -O2, 2e7 iterations, two runs): `0.5 - 0.5*cos(pi*u)`
//   costs 4.84-4.94 ns, i.e. ~10 000 ns per 512-sample block in SC-014's
//   four-scheduler workload - 0.94x the 10 667 ns reference and 1.41x the
//   7111 ns baseline ceiling, BEFORE the Perlin and Aizawa work. SC-014 is
//   unattainable with a cosine. The polynomial costs 0.86-0.87 ns (~1780
//   ns/block), is C2 rather than C1, and is the same polynomial the Perlin
//   lattice uses. Consequences, all inside their bounds: peak slope 1.875/T so
//   the worst per-sample slew is 7.81e-4 (kMinSegmentSeconds attack at 48 kHz)
//   against 2.0e-3; peak second derivative 10/sqrt(3) so the decimated interior
//   second difference is ~5.77e-4 against a `> 1.0e-5` guard; and because
//   riseShape'(0) = riseShape'(1) = riseShape''(0) = riseShape''(1) = 0 every
//   one of the four cycle joins is C2, not merely C1.
//
// ------------------------------------------------------------------------------
// CONFIGURATION-ORDERING RULE - binding on every consumer
// ------------------------------------------------------------------------------
//   initState() draws the FR-067 pre-roll period from minIntervalSeconds_ /
//   maxIntervalSeconds_ AS THEY STAND AT THAT MOMENT. Apply ALL interval-range
//   configuration BEFORE prepare(), or call reset() after it. Configuring after
//   prepare() leaves the scheduler idling for a pre-roll drawn from the PREVIOUS
//   range, and no later setter shortens it (FR-066 latches). Measured pre-rolls
//   at the 20-90 s defaults: kDefaultEventSeed (0x51E7) -> 41.236 s
//   (u = 0.303378), 0x1 -> 20.004 s, 12345 -> 74.386 s, 0xBEEF -> 84.659 s,
//   0xABCD -> 70.955 s.
//
// ------------------------------------------------------------------------------
// FIT RULE (FR-055) - scaled against the MINIMUM interval, not the drawn period
// ------------------------------------------------------------------------------
//   scale = min(1, minIntervalSeconds_ / (attack + hold + release)).
//   Scaling against minIntervalSeconds_ rather than the period just drawn means
//   events can never overlap for ANY draw, not merely for the typical one.
//   Uniform scaling preserves the shape (hence the C2 joins survive it) and is
//   order-independent: setEnvelopeTimes() then setIntervalRange() gives exactly
//   the same effective times as the reverse order, because the fit is evaluated
//   at DRAW time from the stored configuration and never under a running
//   envelope.
//
// ------------------------------------------------------------------------------
// DRAW ORDER (FR-060) - period, target, depth, polarity. Exactly four draws.
// ------------------------------------------------------------------------------
//   The fixed order is what keeps two runs that differ only in one range setting
//   RNG-ALIGNED, which is what makes SC-011's exact depth-halving clause
//   meaningful. Xorshift32::nextUnipolar() returns (0, 1] - next() never returns
//   0 (random.h:50, 67, 89) - with two deliberate consequences:
//     * the polarity test is `u <= bipolarProbability_` (<=, NOT <), so
//       probability 0 is all-positive and probability 1 is all-negative;
//     * the target draw needs an explicit upper guard because u can be exactly
//       1.0, which would index one past the last target.
//   The FR-067 pre-roll is NOT an event, so it consumes ONE draw (the period),
//   not four.
//
//   Seed 0 is a documented ALIAS for Xorshift32's kDefaultSeed = 2463534242u
//   (random.h:73-74, :85), not an error.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Seeded slow-event generator: draw a period, wait, swell, repeat.
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with the
///      depth range; depth scales the swell inside that range).
/// @par Poll rate: PER SAMPLE. See the poll-rate contract in the banner above.
class SlowEventScheduler final : public ModulationSource {
public:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    /// Sentinel returned by getActiveTarget() while no event is running.
    static constexpr std::uint8_t kNoTarget = 0xFFu;
    /// Largest routable target index count (targets are 0..kMaxTargets-1).
    static constexpr std::uint8_t kMaxTargets = 16u;

    static constexpr float kMinIntervalSeconds = 1.0f;
    static constexpr float kMaxIntervalSeconds = 600.0f;
    static constexpr float kMinSegmentSeconds = 0.05f;
    static constexpr float kMaxSegmentSeconds = 300.0f;

    /// Control-rate decimation, matching BrownianDrift (brownian_drift.h:105).
    /// Only ONSET DETECTION is decimated to this grid; the envelope itself is
    /// evaluated per sample (FR-065).
    static constexpr int kControlRateInterval = 32;

    static constexpr float kDefaultMinInterval = 20.0f;
    static constexpr float kDefaultMaxInterval = 90.0f;
    static constexpr float kDefaultAttack = 5.0f;
    static constexpr float kDefaultHold = 3.0f;
    static constexpr float kDefaultRelease = 8.0f;
    static constexpr float kDefaultMinDepth = 0.3f;
    static constexpr float kDefaultMaxDepth = 1.0f;
    static constexpr float kDefaultBipolarProbability = 0.5f;
    static constexpr std::uint8_t kDefaultTargetCount = 4u;
    static constexpr std::uint32_t kDefaultEventSeed = 0x51E7u;

    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    /// Where inside the cycle the single clock currently sits.
    enum class Phase : std::uint8_t { Idle = 0, Attack = 1, Hold = 2, Release = 3 };

    /// The three quantities latched at an onset.
    /// @note NOT named ScheduledEvent: three unrelated `ScheduledEvent`
    ///       definitions already exist outside Krate::DSP, and reusing the name
    ///       invites exactly the ODR confusion this repo treats as its
    ///       highest-severity failure.
    struct Event {
        std::uint8_t target = kNoTarget;
        float depth = 0.0f;
        std::int8_t polarity = 1;
    };

    // -------------------------------------------------------------------------
    // Lifecycle (FR-002)
    // -------------------------------------------------------------------------

    /// @brief Construct in the same well-defined state prepare(48 kHz) produces.
    /// Advancing before prepare() is therefore legal and silent: the pre-roll is
    /// already drawn, so periodSamples_ > 0 and takeOnsetIfDue() terminates.
    SlowEventScheduler() noexcept { initState(); }

    /// @brief Adopt a sample rate and re-initialise completely.
    /// Re-preparing mid-event discards that event; there is no half-completed
    /// segment left behind.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make every boundary offset non-finite)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        initState();
    }

    /// @brief Rewind to the exact post-prepare state (RNG included).
    /// Keeps the configured sample rate; the same seed re-renders identically.
    void reset() noexcept { initState(); }

    // -------------------------------------------------------------------------
    // Configuration (FR-052, FR-055, FR-059, FR-066)
    // -------------------------------------------------------------------------
    // Every setter writes stored configuration and NOTHING else - no redraw, no
    // refit, no state-machine touch. A change is visible through its own getter
    // immediately and through the output only at the NEXT onset.

    /// @brief Set the RNG seed and reseed immediately (FR-059).
    /// Plain non-virtual member: ModulationSource declares only
    /// getCurrentValue()/getSourceRange() as virtuals
    /// (modulation_source.h:37,41). Reseeding mid-event does NOT truncate that
    /// event (FR-062) - the in-flight quantities are already latched.
    /// @param seedValue Seed (0 is substituted by Xorshift32's default)
    void setSeed(std::uint32_t seedValue) noexcept {
        configuredSeed_ = seedValue;
        rng_.seed(seedValue);
    }

    /// @brief Uniform inter-event period range in seconds (FR-052).
    /// Both bounds are clamped to [kMinIntervalSeconds, kMaxIntervalSeconds];
    /// an inverted range collapses onto the MIN ARGUMENT (hi is raised to lo,
    /// never the reverse), so setIntervalRange(90, 20) is a fixed 90 s period.
    void setIntervalRange(float minSeconds, float maxSeconds) noexcept {
        minIntervalSeconds_ = sanitizeClamp(minSeconds, kMinIntervalSeconds, kMaxIntervalSeconds);
        maxIntervalSeconds_ = sanitizeClamp(maxSeconds, kMinIntervalSeconds, kMaxIntervalSeconds);
        if (maxIntervalSeconds_ < minIntervalSeconds_) maxIntervalSeconds_ = minIntervalSeconds_;
    }

    /// @brief Configured (pre-fit) attack / hold / release in seconds (FR-055).
    /// Each is clamped to [kMinSegmentSeconds, kMaxSegmentSeconds]. The fit rule
    /// scales the trio at the next onset; see the banner.
    void setEnvelopeTimes(float attackSeconds, float holdSeconds, float releaseSeconds) noexcept {
        attackSeconds_ = sanitizeClamp(attackSeconds, kMinSegmentSeconds, kMaxSegmentSeconds);
        holdSeconds_ = sanitizeClamp(holdSeconds, kMinSegmentSeconds, kMaxSegmentSeconds);
        releaseSeconds_ = sanitizeClamp(releaseSeconds, kMinSegmentSeconds, kMaxSegmentSeconds);
    }

    /// @brief Uniform per-event depth range (FR-053). Both bounds clamped to
    /// [0, 1]; an inverted range collapses onto the min argument.
    void setDepthRange(float minDepth, float maxDepth) noexcept {
        minDepth_ = sanitizeClamp(minDepth, 0.0f, 1.0f);
        maxDepth_ = sanitizeClamp(maxDepth, 0.0f, 1.0f);
        if (maxDepth_ < minDepth_) maxDepth_ = minDepth_;
    }

    /// @brief Probability that an event swings NEGATIVE (FR-057).
    /// 0 => every event positive, 1 => every event negative (the draw test is
    /// `u <= p` and nextUnipolar() returns (0, 1]).
    void setBipolarProbability(float probability) noexcept {
        bipolarProbability_ = sanitizeClamp(probability, 0.0f, 1.0f);
    }

    /// @brief Number of routable targets (FR-059). Clamped to [1, kMaxTargets];
    /// integer setters use plain std::clamp - only the float setters need the
    /// NaN-safe path.
    void setTargetCount(std::uint8_t count) noexcept {
        targetCount_ = std::clamp(count, std::uint8_t{1}, kMaxTargets);
    }

    [[nodiscard]] float getMinIntervalSeconds() const noexcept { return minIntervalSeconds_; }
    [[nodiscard]] float getMaxIntervalSeconds() const noexcept { return maxIntervalSeconds_; }
    /// @brief Configured attack, PRE-fit. getEffectiveAttackSeconds() is the
    /// post-fit figure the running event actually uses.
    [[nodiscard]] float getAttackSeconds() const noexcept { return attackSeconds_; }
    [[nodiscard]] float getHoldSeconds() const noexcept { return holdSeconds_; }
    [[nodiscard]] float getReleaseSeconds() const noexcept { return releaseSeconds_; }
    [[nodiscard]] float getMinDepth() const noexcept { return minDepth_; }
    [[nodiscard]] float getMaxDepth() const noexcept { return maxDepth_; }
    [[nodiscard]] float getBipolarProbability() const noexcept { return bipolarProbability_; }
    [[nodiscard]] std::uint8_t getTargetCount() const noexcept { return targetCount_; }

    // -------------------------------------------------------------------------
    // Advance (FR-063, FR-064)
    // -------------------------------------------------------------------------

    /// @brief Advance one sample. Onset detection runs on the control grid.
    void process() noexcept {
        elapsedSamples_ += 1.0;
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = kControlRateInterval;
            takeOnsetIfDue();
        }
    }

    /// @brief Advance a whole block, exactly equivalent to numSamples process()
    /// calls but O(control steps). processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(std::size_t numSamples) noexcept {
        std::size_t remaining = numSamples;
        while (remaining > 0u) {
            // Invariant: samplesUntilControl_ is in [1, kControlRateInterval]
            // at the top of every iteration (initState() sets it to the full
            // interval and the branch below never leaves it at 0), so `advance`
            // is at least 1 and the loop always terminates.
            const auto untilControl = static_cast<std::size_t>(samplesUntilControl_);
            const std::size_t advance = std::min(remaining, untilControl);
            elapsedSamples_ += static_cast<double>(advance);
            samplesUntilControl_ -= static_cast<int>(advance);
            remaining -= advance;
            if (samplesUntilControl_ <= 0) {
                samplesUntilControl_ = kControlRateInterval;
                takeOnsetIfDue();
            }
        }
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-051)
    // -------------------------------------------------------------------------

    /// @brief polarity * depth * envelope, evaluated NOW (see the poll-rate
    /// contract). The terminal clamp is an inert net - |polarity| == 1,
    /// depth is in [0,1] and the envelope is in [0,1], so the product is already
    /// inside the range - never the source of the bound.
    [[nodiscard]] float getCurrentValue() const noexcept override {
        // signedDepth_ is polarity * depth PRE-MULTIPLIED at draw time, in that
        // exact order, so the product below is bit-identical to
        // (double)polarity * (double)depth * envelope. It is latched rather than
        // recomputed because this is the per-sample path (FR-065): at 2048 calls
        // per 512-sample block in SC-014's four-scheduler workload the two extra
        // loads and the two int8/float -> double conversions dominate the
        // arithmetic they feed.
        const double value = signedDepth_ * envelopeAt(elapsedSamples_);
        return std::clamp(static_cast<float>(value), -1.0f, 1.0f);
    }

    /// @brief Fixed at bipolar full scale, independent of the depth range.
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // -------------------------------------------------------------------------
    // Read surface (FR-058)
    // -------------------------------------------------------------------------
    // These exist because getCurrentValue()'s product destroys information: a
    // consumer routing to a target needs the target index and the unsigned
    // envelope separately.

    /// @brief Active target index, or kNoTarget while idle (pre-roll included).
    [[nodiscard]] std::uint8_t getActiveTarget() const noexcept {
        return isEventActive() ? active_.target : kNoTarget;
    }

    /// @brief True while the swell is running (attack, hold or release).
    [[nodiscard]] bool isEventActive() const noexcept {
        return !firstOnsetPending_ && elapsedSamples_ < releaseEndSamples_;
    }

    [[nodiscard]] Phase getEventPhase() const noexcept {
        if (!isEventActive()) return Phase::Idle;
        if (elapsedSamples_ < attackEndSamples_) return Phase::Attack;
        if (elapsedSamples_ < holdEndSamples_) return Phase::Hold;
        return Phase::Release;
    }

    /// @brief The period currently being counted down, in seconds - i.e. the
    /// onset-to-onset cadence latched at the last onset. Immediately after
    /// prepare()/reset() this is the FR-067 PRE-ROLL length, which is drawn from
    /// the same uniform range but is not an event.
    [[nodiscard]] double getPeriodSeconds() const noexcept { return periodSeconds_; }

    /// @brief Effective attack + hold + release of the running event, seconds.
    [[nodiscard]] double getEventDurationSeconds() const noexcept {
        return effAttackSeconds_ + effHoldSeconds_ + effReleaseSeconds_;
    }

    /// @brief Post-fit segment times of the running event (FR-055).
    [[nodiscard]] double getEffectiveAttackSeconds() const noexcept { return effAttackSeconds_; }
    [[nodiscard]] double getEffectiveHoldSeconds() const noexcept { return effHoldSeconds_; }
    [[nodiscard]] double getEffectiveReleaseSeconds() const noexcept { return effReleaseSeconds_; }

    /// @brief The UNSIGNED, UNSCALED envelope in [0, 1], evaluated NOW.
    /// Computed directly from envelopeAt() - never by dividing polarity*depth
    /// back out of getCurrentValue(), which is undefined at depth 0.
    [[nodiscard]] float getEnvelopeValue() const noexcept {
        return static_cast<float>(envelopeAt(elapsedSamples_));
    }

    /// @brief Depth drawn for the running event (retains the last drawn value
    /// while idle; the envelope is 0 there, so the product identity still holds).
    [[nodiscard]] float getActiveDepth() const noexcept { return active_.depth; }

    /// @brief +1 or -1 for the running event (FR-057).
    [[nodiscard]] std::int8_t getActivePolarity() const noexcept { return active_.polarity; }

private:
    // -------------------------------------------------------------------------
    // NaN-safe clamping
    // -------------------------------------------------------------------------

    /// @brief std::clamp PROPAGATES NaN; every float setter routes through this
    /// instead so a NaN lands on the LOW bound. detail::isNaN (db_utils.h:99) is
    /// used rather than std::isnan, which tools/lint-nonfinite-symbols.js bans
    /// and which -ffast-math folds away on the macOS leg.
    [[nodiscard]] static constexpr float sanitizeClamp(float value, float lo, float hi) noexcept {
        if (detail::isNaN(value)) return lo;
        if (value < lo) return lo;
        if (value > hi) return hi;
        return value;
    }

    // -------------------------------------------------------------------------
    // Shape
    // -------------------------------------------------------------------------

    /// @brief Smootherstep 6u^5 - 15u^4 + 10u^3, clamped to [0, 1].
    /// f'(0) = f'(1) = f''(0) = f''(1) = 0, which is what makes all four cycle
    /// joins C2. Peak slope 1.875 at u = 0.5, peak curvature 10/sqrt(3).
    [[nodiscard]] static double riseShape(double u) noexcept {
        const double c = (u <= 0.0) ? 0.0 : (u >= 1.0) ? 1.0 : u;
        return c * c * c * (c * (6.0 * c - 15.0) + 10.0);
    }

    /// @brief Unsigned envelope at `t` samples since the last onset.
    /// Pure function of `t` and the four latched offsets - see the single-clock
    /// note in the banner.
    ///
    /// BRANCH ORDER IS THE IDLE ORDER, DELIBERATELY. The overwhelmingly common
    /// case is the idle stretch (at the FR-052/FR-055 defaults an event occupies
    /// 16 s of a 20-90 s period; even pinned to the fastest legal cadence it is
    /// 0.15 s of 1 s), and this function is called PER SAMPLE. Testing
    /// `t >= releaseEndSamples_` first settles that case with ONE load and one
    /// compare; the previous attack-first order loaded four members and ran four
    /// compares before reaching the same answer. The three tests below are the
    /// exact complements of the old four, so every t returns what it did before.
    ///
    /// THE `firstOnsetPending_` TEST IS SUBSUMED, NOT DROPPED: initState() is the
    /// only writer that sets the flag, and it zeroes attackEndSamples_,
    /// holdEndSamples_ and releaseEndSamples_ in the same breath, so during the
    /// FR-067 pre-roll `t >= releaseEndSamples_ == 0.0` holds for every t >= 0
    /// and the first test already returns 0.0. Anything that sets the flag
    /// without zeroing those three offsets must re-add the test.
    [[nodiscard]] double envelopeAt(double t) const noexcept {
        // Idle stretch between the release end and the next onset - and the
        // whole FR-067 pre-roll (see above).
        if (t >= releaseEndSamples_) return 0.0;
        if (t >= holdEndSamples_) {
            return riseShape((releaseEndSamples_ - t) / (releaseEndSamples_ - holdEndSamples_));
        }
        if (t >= attackEndSamples_) return 1.0;
        return riseShape(t / attackEndSamples_);
    }

    // -------------------------------------------------------------------------
    // Draw / fit
    // -------------------------------------------------------------------------

    /// @brief One nextUnipolar() draw mapped onto the stored interval range.
    [[nodiscard]] double drawPeriodSeconds() noexcept {
        const double u = static_cast<double>(rng_.nextUnipolar());
        const double lo = static_cast<double>(minIntervalSeconds_);
        const double hi = static_cast<double>(maxIntervalSeconds_);
        return lo + u * (hi - lo);
    }

    /// @brief FR-055 fit, evaluated against the CURRENTLY STORED configuration
    /// at draw time only. Scaling against minIntervalSeconds_ (not the drawn
    /// period) is what guarantees non-overlap for every draw.
    void fitSegments() noexcept {
        const double attack = static_cast<double>(attackSeconds_);
        const double hold = static_cast<double>(holdSeconds_);
        const double release = static_cast<double>(releaseSeconds_);
        const double total = attack + hold + release;
        const double scale =
            (total <= 0.0) ? 1.0
                           : std::min(1.0, static_cast<double>(minIntervalSeconds_) / total);
        effAttackSeconds_ = attack * scale;
        effHoldSeconds_ = hold * scale;
        effReleaseSeconds_ = release * scale;
        attackEndSamples_ = effAttackSeconds_ * sampleRate_;
        holdEndSamples_ = (effAttackSeconds_ + effHoldSeconds_) * sampleRate_;
        releaseEndSamples_ = (effAttackSeconds_ + effHoldSeconds_ + effReleaseSeconds_) *
                             sampleRate_;
    }

    /// @brief Exactly four draws in the fixed FR-060 order, then the fit.
    /// Called once per onset and NEVER from a setter.
    void drawCycle() noexcept {
        periodSeconds_ = drawPeriodSeconds();
        periodSamples_ = periodSeconds_ * sampleRate_;

        const double uTarget = static_cast<double>(rng_.nextUnipolar());
        const double uDepth = static_cast<double>(rng_.nextUnipolar());
        const double uPolarity = static_cast<double>(rng_.nextUnipolar());

        // nextUnipolar() can return exactly 1.0, which would index one past the
        // last target - hence the explicit guard rather than a bare cast.
        const auto count = static_cast<std::uint32_t>(targetCount_);
        auto index = static_cast<std::uint32_t>(uTarget * static_cast<double>(count));
        if (index >= count) index = count - 1u;
        active_.target = static_cast<std::uint8_t>(index);

        const double depthLo = static_cast<double>(minDepth_);
        const double depthHi = static_cast<double>(maxDepth_);
        active_.depth = static_cast<float>(depthLo + uDepth * (depthHi - depthLo));

        // `<=`, not `<`: nextUnipolar() is (0, 1], so probability 0 must never
        // fire and probability 1 must always fire.
        active_.polarity = (uPolarity <= static_cast<double>(bipolarProbability_))
                               ? static_cast<std::int8_t>(-1)
                               : static_cast<std::int8_t>(1);

        // Latched here and nowhere else but initState(): active_.depth and
        // active_.polarity have exactly two writers, and both keep this in step.
        signedDepth_ = static_cast<double>(active_.polarity) * static_cast<double>(active_.depth);

        fitSegments();
    }

    // -------------------------------------------------------------------------
    // Clock
    // -------------------------------------------------------------------------

    /// @brief Take every onset that has come due since the last control step.
    /// Bounded: periodSamples_ >= kMinIntervalSeconds * 1 Hz > 0 always, so
    /// elapsedSamples_ strictly decreases each iteration and at most
    /// kControlRateInterval iterations can run.
    void takeOnsetIfDue() noexcept {
        while (elapsedSamples_ >= periodSamples_) {
            elapsedSamples_ -= periodSamples_;  // FR-064: carry, NEVER = 0
            firstOnsetPending_ = false;
            drawCycle();
        }
    }

    /// @brief Full re-initialisation, including the FR-067 pre-roll draw.
    /// The pre-roll is not an event, so it consumes ONE draw, not four: the
    /// scheduler idles a full drawn period before its first onset and emits
    /// exactly 0.0f throughout.
    void initState() noexcept {
        rng_.seed(configuredSeed_);
        active_ = Event{};
        signedDepth_ = 0.0;  // Event{} is depth 0, polarity +1
        effAttackSeconds_ = 0.0;
        effHoldSeconds_ = 0.0;
        effReleaseSeconds_ = 0.0;
        attackEndSamples_ = 0.0;
        holdEndSamples_ = 0.0;
        releaseEndSamples_ = 0.0;
        elapsedSamples_ = 0.0;
        samplesUntilControl_ = kControlRateInterval;
        firstOnsetPending_ = true;
        periodSeconds_ = drawPeriodSeconds();
        periodSamples_ = periodSeconds_ * sampleRate_;
    }

    // -------------------------------------------------------------------------
    // State - every member is a fixed-size scalar
    // -------------------------------------------------------------------------

    double sampleRate_ = 48000.0;

    // Stored configuration (read only at draw time).
    float minIntervalSeconds_ = kDefaultMinInterval;
    float maxIntervalSeconds_ = kDefaultMaxInterval;
    float attackSeconds_ = kDefaultAttack;
    float holdSeconds_ = kDefaultHold;
    float releaseSeconds_ = kDefaultRelease;
    float minDepth_ = kDefaultMinDepth;
    float maxDepth_ = kDefaultMaxDepth;
    float bipolarProbability_ = kDefaultBipolarProbability;
    std::uint8_t targetCount_ = kDefaultTargetCount;

    // Latched per cycle (FR-066).
    Event active_{};
    /// active_.polarity * active_.depth, in that order, as a double. Redundant
    /// with active_ by construction (drawCycle() and initState() are its only
    /// writers) and exists purely to keep the per-sample path at three loads.
    double signedDepth_ = 0.0;
    double periodSeconds_ = 0.0;
    double periodSamples_ = 0.0;
    double effAttackSeconds_ = 0.0;
    double effHoldSeconds_ = 0.0;
    double effReleaseSeconds_ = 0.0;
    double attackEndSamples_ = 0.0;
    double holdEndSamples_ = 0.0;
    double releaseEndSamples_ = 0.0;
    bool firstOnsetPending_ = true;

    // Clocks.
    double elapsedSamples_ = 0.0;
    int samplesUntilControl_ = kControlRateInterval;

    std::uint32_t configuredSeed_ = kDefaultEventSeed;
    Xorshift32 rng_{kDefaultEventSeed};
};

}  // namespace DSP
}  // namespace Krate
