// ==============================================================================
// Layer 2: DSP Processor - OrbitModulator (Seraphis Life Modulator)
// ==============================================================================
// Two weakly coupled phase oscillators (Kuramoto phase-difference coupling)
// producing a 2D modulation output: x on the base ModulationSource contract,
// y via getY(). An orbital decay/growth control drives a clamped radius
// envelope so the orbit can spiral in, sustain, or bloom out - always bounded.
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-041..FR-043)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 4)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-041/FR-042) - Kuramoto two-oscillator phase model
// ------------------------------------------------------------------------------
//   dphi1/dt = w1 + k*sin(phi2 - phi1)      w1 = 2*pi*rate
//   dphi2/dt = w2 + k*sin(phi1 - phi2)      w2 = 2*pi*rate*(1 + kOscDetune)
//
//       x = depth * r * sin(phi1)   -> getCurrentValue()   (FR-042: base axis)
//       y = depth * r * sin(phi2)   -> getY()
//
//   kOscDetune = 0.1 gives the two oscillators a phase difference for the
//   coupling term to act on; without it sin(phi2-phi1) would be stuck at its
//   initial value and the coupling would be inert.
//
//   Integrated with FORWARD EULER at control rate (dt = controlDtSeconds_ =
//   kControlRateInterval / sampleRate). Unconditionally stable here: the phase
//   derivative is bounded by w_max + k_max = 2*pi*0.5 + 1 = 4.14 rad/s, so
//   |dphi| per step <= 4.14 * 32/44100 = 3.0e-3 rad << 1, and the coupling term
//   is a bounded sinusoid (it cannot feed back into an amplitude).
//
// ------------------------------------------------------------------------------
// RADIUS ENVELOPE (FR-043) - decay / sustain / growth, clamped
// ------------------------------------------------------------------------------
//   Per control step:  r += growth * kRadiusRate * dt,  then
//                      r  = clamp(r, kRadiusMin, 1.0)
//
//   growth = 0  -> r unchanged: the orbit SUSTAINS (the neutral setting).
//   growth < 0  -> spirals inward, floored at kRadiusMin = 0.05, so the orbit
//                  never collapses to a stuck point.
//   growth > 0  -> blooms outward, clamped at 1.0, so it never diverges.
//
//   The clamp is also the boundedness proof (SC-001, both axes):
//       |x|, |y| <= depth * r * 1 <= 1 * 1 * 1 = 1
//   getCurrentValue()/getY() additionally clamp to [-1,+1] as belt and braces.
//
// ------------------------------------------------------------------------------
// SC-002 PROOF - bounded per-sample slew (both axes)
// ------------------------------------------------------------------------------
//   The raw axis signal is r*sin(phi) with r <= 1, so
//
//       |d(sin phi)/dt| <= |dphi/dt| <= w_max = 2*pi*0.5 = 3.14 /s
//
//   at zero coupling, and <= w_max + k_max = 4.14 /s at maximum coupling. Per
//   sample at 48 kHz that is
//
//       3.14 / 48000 = 6.5e-5      (and 8.6e-5 with maximum coupling)
//                                   << 2.0e-3  (= SC-002's 1e-3 of the span 2)
//
//   The radius contributes at most kRadiusRate = 0.1 /s, i.e. 2.1e-6 per
//   sample. The 20 ms output smoothers only bridge control-step boundaries:
//   with a per-control-step target jump J <= 4.14 * 32/48000 = 2.8e-3 and
//   coeff = exp(-5000/(20*48000)), the steady-state tracking error is
//   J / (1 - coeff^32) ~= 1.8e-2, giving a per-sample delta of
//   1.8e-2 * (1 - coeff) ~= 9.3e-5 - still two decades inside the bound.
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - the phases are double accumulators
//   wrapped into [0, 2*pi), sin is bounded, the radius is clamped every step,
//   the smoother targets are clamped to [-1,+1], and OnePoleSmoother::setTarget
//   already sanitizes NaN/Inf (smoother.h:170).
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

/// @brief Two coupled phase oscillators with a clamped radius envelope.
///
/// Implements the ModulationSource interface. Evaluated at control rate
/// (every kControlRateInterval samples) and smoothed per sample, so a block
/// consumer can advance it once per block via processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED on BOTH axes - it does not
///      shrink with the depth setting; depth scales the signal inside it).
class OrbitModulator : public ModulationSource {
public:
    /// Slowest orbital rate (Hz) - a 100 s period.
    static constexpr float kMinRate = 0.01f;
    /// Fastest orbital rate (Hz) - a 2 s period.
    static constexpr float kMaxRate = 0.5f;
    /// Relative detune of the second oscillator, so coupling has work to do.
    static constexpr float kOscDetune = 0.1f;
    /// Lower clamp of the radius envelope: a decaying orbit never sticks.
    static constexpr float kRadiusMin = 0.05f;
    /// Radius change per second at |growth| = 1.
    static constexpr float kRadiusRate = 0.1f;
    /// Output smoothing time (ms) - block-boundary safety, see the SC-002 proof.
    static constexpr float kOutputSmoothMs = 20.0f;
    /// Control-rate decimation, matching ChaosModSource (chaos_mod_source.h:43).
    static constexpr size_t kControlRateInterval = 32;

    static constexpr float kDefaultRate = 0.1f;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr std::uint32_t kDefaultOrbitSeed = 0x0B1Fu;

    OrbitModulator() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Derive per-control-step quantities and initialise state.
    /// After this call getCurrentValue() and getY() are well defined without
    /// any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make controlDtSeconds_ non-finite)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        controlDtSeconds_ =
            static_cast<double>(kControlRateInterval) / sampleRate_;
        xSmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
        ySmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
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

    /// @brief Orbital rate (FR-041), clamped to [kMinRate, kMaxRate].
    /// @param hz Rate in Hz
    void setRate(float hz) noexcept {
        rate_ = std::clamp(hz, kMinRate, kMaxRate);
    }

    /// @brief Kuramoto coupling strength (FR-041), clamped to [0,1] (rad/s).
    /// @param normalized 0 = independent oscillators, 1 = strongly coupled
    void setCoupling(float normalized) noexcept {
        coupling_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    /// @brief Orbital decay/growth (FR-043), clamped to [-1,+1].
    /// @param growth -1 = fastest decay, 0 = sustain, +1 = fastest growth
    void setGrowth(float growth) noexcept {
        growth_ = std::clamp(growth, -1.0f, 1.0f);
    }

    /// @brief Output depth (FR-006). Scales both axes INSIDE the fixed range.
    /// @param normalized 0..1
    void setDepth(float normalized) noexcept {
        depth_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] float getCoupling() const noexcept { return coupling_; }
    [[nodiscard]] float getGrowth() const noexcept { return growth_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample. Updates the orbit on control boundaries.
    void process() noexcept {
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = static_cast<int>(kControlRateInterval);
            advanceControlStep();
        }
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as RandomSource does (random_source.h:110) so the zero-warning
        // gate stays clean.
        static_cast<void>(xSmoother_.process());
        static_cast<void>(ySmoother_.process());
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
            xSmoother_.advanceSamples(static_cast<size_t>(advance));
            ySmoother_.advanceSamples(static_cast<size_t>(advance));
        }
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-001) + second axis (FR-042)
    // -------------------------------------------------------------------------

    /// @brief The x axis: depth * radius * sin(phi1).
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(xSmoother_.getCurrentValue(), -1.0f, 1.0f);
    }

    /// @brief The y axis: depth * radius * sin(phi2) (FR-042).
    /// Plain non-virtual member - ModulationSource has no second-axis virtual.
    [[nodiscard]] float getY() const noexcept {
        return std::clamp(ySmoother_.getCurrentValue(), -1.0f, 1.0f);
    }

    /// @brief Fixed at polarity full scale, independent of depth (FR-006).
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

private:
    static constexpr double kTwoPi = 6.283185307179586476925286766559;

    /// Wrap a phase into [0, 2*pi) in O(1) - branch plus one floor, never a
    /// loop. RT safety demands a bound that does not depend on how large the
    /// increment is: at the 1 Hz prepare() floor a control step spans 32 s and
    /// the phase advances ~100 rad, which a subtract-in-a-loop wrap would spin
    /// on. Same arithmetic as the ordinary case (floor == 1 -> exactly one
    /// kTwoPi subtracted), so seeded determinism is unchanged.
    [[nodiscard]] static double wrapPhase(double phase) noexcept {
        if (phase >= kTwoPi || phase < 0.0) {
            phase -= kTwoPi * std::floor(phase / kTwoPi);
        }
        return phase;
    }

    void initState() noexcept {
        rng_.seed(configuredSeed_);
        // Sequenced explicitly: the operands of an expression are unsequenced
        // in C++, so drawing both phases inline would leave the draw order
        // unspecified and break seeded determinism (SC-004).
        const float u1 = rng_.nextUnipolar();
        const float u2 = rng_.nextUnipolar();
        phi1_ = wrapPhase(static_cast<double>(u1) * kTwoPi);
        phi2_ = wrapPhase(static_cast<double>(u2) * kTwoPi);
        radius_ = 1.0f;
        xSmoother_.snapTo(axisTarget(phi1_));
        ySmoother_.snapTo(axisTarget(phi2_));
        samplesUntilControl_ = 0;
    }

    [[nodiscard]] float axisTarget(double phase) const noexcept {
        const float raw = depth_ * radius_ * static_cast<float>(std::sin(phase));
        return std::clamp(raw, -1.0f, 1.0f);
    }

    void advanceControlStep() noexcept {
        const double dt = controlDtSeconds_;
        const double rate = static_cast<double>(rate_);
        const double omega1 = kTwoPi * rate;
        const double omega2 = kTwoPi * rate * (1.0 + static_cast<double>(kOscDetune));
        const double coupling = static_cast<double>(coupling_);

        // sin(phi1 - phi2) = -sin(phi2 - phi1): one sin call, both directions.
        const double couplingTerm = coupling * std::sin(phi2_ - phi1_);
        const double dPhi1 = omega1 + couplingTerm;
        const double dPhi2 = omega2 - couplingTerm;

        phi1_ = wrapPhase(phi1_ + (dPhi1 * dt));
        phi2_ = wrapPhase(phi2_ + (dPhi2 * dt));

        radius_ = std::clamp(
            radius_ + (growth_ * kRadiusRate * static_cast<float>(dt)),
            kRadiusMin,
            1.0f);

        xSmoother_.setTarget(axisTarget(phi1_));
        ySmoother_.setTarget(axisTarget(phi2_));
    }

    double sampleRate_ = 44100.0;
    double controlDtSeconds_ =
        static_cast<double>(kControlRateInterval) / 44100.0;

    double phi1_ = 0.0;  ///< phase of oscillator 1 (x axis), in [0, 2*pi)
    double phi2_ = 0.0;  ///< phase of oscillator 2 (y axis), in [0, 2*pi)

    float radius_ = 1.0f;  ///< orbital radius envelope, in [kRadiusMin, 1]
    float rate_ = kDefaultRate;
    float coupling_ = 0.0f;
    float growth_ = 0.0f;
    float depth_ = kDefaultDepth;

    int samplesUntilControl_ = 0;

    std::uint32_t configuredSeed_ = kDefaultOrbitSeed;
    Xorshift32 rng_{kDefaultOrbitSeed};
    OnePoleSmoother xSmoother_;
    OnePoleSmoother ySmoother_;
};

}  // namespace DSP
}  // namespace Krate
