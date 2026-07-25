// ==============================================================================
// Layer 2: DSP Processor - BrownianDrift (Seraphis Life Modulator)
// ==============================================================================
// Bounded random walk with mean reversion: a discrete Ornstein-Uhlenbeck (OU)
// process evaluated at control rate and smoothed per sample. The workhorse of
// the Seraphis life-modulator family (per-partial detune drift, brightness
// wander, stereo wandering).
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-011..FR-014)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 1)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-011) - exact OU discretisation, not forward Euler
// ------------------------------------------------------------------------------
//   Continuous:  dX = (1/tau)*(mu - X) dt + sigma dW
//   Exact AR(1) discretisation over a fixed step dt = kControlRateInterval / sr:
//
//       a = exp(-dt / tau)
//       g = kInternalStd * sqrt(1 - a^2)
//       X <- mu + a*(X - mu) + g*Z          with Z zero-mean, unit-variance
//
//   Z is the Irwin-Hall sum of three Xorshift32::nextFloat() draws (each in
//   [-1,1] with variance 1/3), giving a zero-mean, unit-variance, roughly
//   Gaussian increment. The increment DISTRIBUTION is not load bearing: the
//   AR(1) coefficient a alone fixes the autocorrelation, corr(lag k) = a^k, so
//   the 1/e decorrelation time is exactly tau seconds for any zero-mean
//   increment. That is what makes the smoothness control (FR-012) measurable.
//
//   tau = lerp(kTauMin, kTauMax, smoothness). Higher smoothness -> larger tau
//   -> slower, more correlated motion. kTauMin (0.2 s) sits well above the
//   output smoother's ~30 ms time constant so the smoother can never dominate
//   the decorrelation-time ordering.
//
// ------------------------------------------------------------------------------
// SC-002 PROOF - bounded per-sample slew, independent of OU step size
// ------------------------------------------------------------------------------
//   The control-rate walk never reaches the output directly; it only moves the
//   target of a OnePoleSmoother configured at kDriftOutputSmoothMs = 150 ms.
//   For a one-pole smoother the per-sample change is
//
//       |current - target| * (1 - coeff),   coeff = exp(-5000/(T_ms * sr))
//       (smoother.h:90-92, smoother.h:205)
//
//   Both current and target are confined to [-1, +1] (the target is clamped in
//   advanceControlStep(), and a one-pole never overshoots a target inside the
//   range it starts in), so |current - target| <= 2 = the source-range span.
//   At T_ms = 150 and sr = 48 kHz: 1 - coeff = 1 - exp(-6.944e-4) ~= 6.94e-4,
//   giving a worst-case per-sample delta of
//
//       2 * 6.94e-4 ~= 1.39e-3  <  2.0e-3  (= SC-002's 1e-3 of the span)
//
//   This bound holds REGARDLESS of how large a step the OU takes, because the
//   smoother never jumps. At 44.1 kHz the bound is 1.51e-3, still inside SC-002.
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - a is in (0,1] and g is finite, so
//   the recurrence is a bounded contraction; the walk is hard-clamped to
//   +/-kWalkLimit; the smoother target is clamped to [-1,+1]; and
//   OnePoleSmoother::setTarget already sanitizes NaN/Inf (smoother.h:170).
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Slow, bounded, mean-reverting random walk (Ornstein-Uhlenbeck).
///
/// Implements the ModulationSource interface. Evaluated at control rate
/// (every kControlRateInterval samples) and smoothed per sample, so a block
/// consumer can advance it once per block via processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with
///      the depth setting; depth scales the signal inside that range).
class BrownianDrift : public ModulationSource {
public:
    /// Decorrelation time at smoothness = 0 (seconds).
    static constexpr float kTauMin = 0.2f;
    /// Decorrelation time at smoothness = 1 (seconds).
    static constexpr float kTauMax = 30.0f;
    /// Stationary standard deviation of the internal walk.
    static constexpr float kInternalStd = 0.5f;
    /// Output smoothing time (ms). See the SC-002 proof above.
    static constexpr float kDriftOutputSmoothMs = 150.0f;
    /// Control-rate decimation, matching ChaosModSource (chaos_mod_source.h:43).
    static constexpr size_t kControlRateInterval = 32;

    static constexpr float kDefaultSmoothness = 0.5f;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr std::uint32_t kDefaultDriftSeed = 0xB17Eu;

    BrownianDrift() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Derive per-control-step coefficients and initialise state.
    /// After this call getCurrentValue() is well defined without any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make controlDtSeconds_ non-finite)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        controlDtSeconds_ =
            static_cast<double>(kControlRateInterval) / sampleRate_;
        updateCoefficients();
        outputSmoother_.configure(kDriftOutputSmoothMs,
                                  static_cast<float>(sampleRate_));
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

    /// @brief Smoothness / correlation of the walk (FR-012).
    /// @param normalized 0 = fast, least correlated; 1 = slowest, most correlated
    void setSmoothness(float normalized) noexcept {
        smoothness_ = std::clamp(normalized, 0.0f, 1.0f);
        updateCoefficients();
    }

    /// @brief Output depth (FR-013). Scales the signal INSIDE the fixed range.
    /// @param normalized 0..1
    void setDepth(float normalized) noexcept {
        depth_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    /// @brief Mean the walk reverts toward (FR-011).
    /// @param mean Target mean in [-1, +1]
    void setMean(float mean) noexcept {
        mean_ = std::clamp(mean, -1.0f, 1.0f);
    }

    [[nodiscard]] float getSmoothness() const noexcept { return smoothness_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }
    [[nodiscard]] float getMean() const noexcept { return mean_; }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample. Updates the walk on control boundaries.
    void process() noexcept {
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = static_cast<int>(kControlRateInterval);
            advanceControlStep();
        }
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as RandomSource does (random_source.h:110) so the zero-warning
        // gate stays clean.
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Advance a whole block at control rate (FR-003).
    /// Equivalent to numSamples process() calls but O(control steps).
    /// processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (samplesUntilControl_ <= 0) {
                samplesUntilControl_ = static_cast<int>(kControlRateInterval);
                advanceControlStep();
            }
            const int advance = std::min(remaining, samplesUntilControl_);
            samplesUntilControl_ -= advance;
            remaining -= advance;
            outputSmoother_.advanceSamples(static_cast<size_t>(advance));
        }
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-001)
    // -------------------------------------------------------------------------

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), -1.0f, 1.0f);
    }

    /// @brief Fixed at polarity full scale, independent of depth (FR-006).
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

private:
    /// Hard divergence guard on the internal walk. The stationary distribution
    /// has std kInternalStd = 0.5 around a mean in [-1,+1], so +/-4 is >= 6
    /// sigma away and is never reached in practice - it exists purely so the
    /// recurrence is provably bounded.
    static constexpr float kWalkLimit = 4.0f;
    /// Flush the walk to zero below this magnitude (denormal guard).
    static constexpr float kDenormalFloor = 1e-20f;

    void updateCoefficients() noexcept {
        const double tau = static_cast<double>(kTauMin) +
                           static_cast<double>(smoothness_) *
                               (static_cast<double>(kTauMax) -
                                static_cast<double>(kTauMin));
        const double a = std::exp(-controlDtSeconds_ / tau);
        a_ = static_cast<float>(a);
        const double variance = 1.0 - (a * a);
        g_ = static_cast<float>(static_cast<double>(kInternalStd) *
                                std::sqrt(variance > 0.0 ? variance : 0.0));
    }

    void initState() noexcept {
        rng_.seed(configuredSeed_);
        x_ = mean_;
        outputSmoother_.snapTo(outputTarget());
        samplesUntilControl_ = 0;
    }

    [[nodiscard]] float outputTarget() const noexcept {
        return std::clamp(depth_ * x_, -1.0f, 1.0f);
    }

    void advanceControlStep() noexcept {
        // Sequenced explicitly: the operands of `+` are unsequenced in C++, so
        // summing three nextFloat() calls inline would leave the draw order
        // unspecified.
        const float z0 = rng_.nextFloat();
        const float z1 = rng_.nextFloat();
        const float z2 = rng_.nextFloat();
        const float z = z0 + z1 + z2;  // zero-mean, unit-variance (Irwin-Hall)

        float x = mean_ + (a_ * (x_ - mean_)) + (g_ * z);
        x = std::clamp(x, -kWalkLimit, kWalkLimit);
        if (x < kDenormalFloor && x > -kDenormalFloor) {
            x = 0.0f;
        }
        x_ = x;

        outputSmoother_.setTarget(outputTarget());
    }

    double sampleRate_ = 44100.0;
    double controlDtSeconds_ =
        static_cast<double>(kControlRateInterval) / 44100.0;

    float a_ = 0.0f;  ///< AR(1) retention coefficient, recomputed in prepare
    float g_ = 0.0f;  ///< AR(1) increment gain, recomputed in prepare

    float smoothness_ = kDefaultSmoothness;
    float depth_ = kDefaultDepth;
    float mean_ = 0.0f;
    float x_ = 0.0f;  ///< internal walk state

    int samplesUntilControl_ = 0;

    std::uint32_t configuredSeed_ = kDefaultDriftSeed;
    Xorshift32 rng_{kDefaultDriftSeed};
    OnePoleSmoother outputSmoother_;
};

}  // namespace DSP
}  // namespace Krate
