// ==============================================================================
// Layer 2: DSP Processor - TidalModulator (Seraphis Life Modulator)
// ==============================================================================
// Very slow (30 s - 10 min) tidal drift built from three layers, each a "sine
// pair" of two slightly detuned sines that beat against one another. The three
// layer periods stand in fixed, mutually incommensurate irrational ratios
// (1 : sqrt(2) : sqrt(3)), so the combined output never repeats exactly.
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-031..FR-033)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 3)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-031 / FR-032)
// ------------------------------------------------------------------------------
//   layer k (k = 0,1,2):  L_k = kSinePairScale * (sin(theta_k0) + sin(theta_k1))
//   output             :  depth * sum_k kLayerWeight * L_k
//
//   A single rate scalar sets the base period:
//       P_base = lerp(kMaxBasePeriod -> kMinPeriod, rate01)  (rate 0 = slowest)
//       P_k    = P_base * kLayerRatios[k]
//       f_k0   = 1 / P_k,   f_k1 = f_k0 * (1 + kDetune)
//
//   kMaxBasePeriod = kMaxPeriod / max(kLayerRatios) = 600 / sqrt(3) ~= 346.4 s,
//   so the SLOWEST layer - not the fastest - is what touches the 10 min limit.
//   That is what keeps FR-032 true across the WHOLE rate range: the layer
//   periods are never individually clamped, so their mutual ratios stay exactly
//   1 : sqrt(2) : sqrt(3) at every rate. (Deriving P_base from kMaxPeriod
//   directly, as an earlier revision did, pushed the sqrt(2)/sqrt(3) layers past
//   kMaxPeriod for every rate < ~0.44 and the range clamp then collapsed them
//   onto the base layer - at rate 0 all three periods became 600 s, a 1:1:1
//   degenerate stack with no incommensurability left at all.)
//   Layer periods therefore span [kMinPeriod, kMaxPeriod] across the rate range
//   (30 s at rate 1, 600 s at rate 0) and always lie inside it (FR-031).
//
//   kLayerRatios = {1, sqrt(2), sqrt(3)} are HARDCODED constants, deliberately
//   NOT seed-drawn: their mutual irrationality is the design justification for
//   indefinite non-repetition, and it keeps the dominant period a function of
//   the rate scalar alone (so it is predictable and testable). The seed varies
//   only the six initial sine phases.
//
// ------------------------------------------------------------------------------
// FR-033 - BOUNDEDNESS IS ANALYTIC, NOT EMPIRICAL
// ------------------------------------------------------------------------------
//   The six sines carry absolute coefficients that sum to exactly the half-span
//   of the declared [-1,+1] source range:
//
//       kNumLayers * kLayerWeight * kSinesPerLayer * kSinePairScale
//         = 3 * (1/3) * 2 * 0.5 = 1
//
//   so even fully-constructive alignment of all six sines yields |out| <= depth
//   <= 1. This is the load-bearing guarantee: worst-case alignment of 30 s -
//   10 min incommensurate periods is unreachable by any practical render, so a
//   boundedness render can only ever be a sanity check. getCurrentValue() still
//   clamps as an (inert) belt-and-braces guard.
//
// ------------------------------------------------------------------------------
// SC-002 ANALYTIC JUSTIFICATION - bounded per-sample slew
// ------------------------------------------------------------------------------
//   Worst case (spec.md:259) is the shortest period (kMinPeriod = 30 s), max
//   depth, all three layers at full amplitude. Differentiating the output,
//
//       |d(out)/dt| <= depth * sum_i |coef_i| * omega_i <= depth * omega_max
//       omega_max    = 2*pi / kMinPeriod = 2*pi / 30 s ~= 0.209 rad/s
//
//   (the +kDetune partner is only 2 % faster and the sqrt(2)/sqrt(3) layers are
//   slower, so omega_max is the binding term). Per sample at 48 kHz:
//
//       0.209 / 48000 ~= 4.4e-6  <<  2.0e-3   (= SC-002's 1e-3 of the span 2)
//
//   ~450x inside the threshold. The light kOutputSmoothMs = 20 ms output
//   smoother exists only so a block-decimated advance cannot introduce a step at
//   a block boundary; a one-pole never moves further than the target step it is
//   chasing, so it cannot exceed the bound above either.
//
// ------------------------------------------------------------------------------
// NUMERICAL STABILITY (long renders) + NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   SC-001 renders this modulator for >= 30 min (~86 M samples at 48 kHz), so
//   the phase accumulators are DOUBLE and are wrapped mod 2*pi on every advance;
//   a float phase would lose its low bits and drift. sin() is evaluated on the
//   wrapped double and cast to float only at the output.
//
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - the periods are clamped to
//   [kMinPeriod, kMaxPeriod] so the increments are finite and positive, sin() is
//   bounded, the output is clamped to [-1,+1], and OnePoleSmoother::setTarget
//   already sanitizes NaN/Inf (smoother.h:170).
//
//   sin() differs in its last bits across toolchains, so nothing here may be
//   pinned by a bit-exact float golden; the guarantees above are all analytic.
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Very slow, bounded, never-exactly-repeating tidal drift.
///
/// Implements the ModulationSource interface. Advance it per sample with
/// process() or once per block with processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with
///      the depth setting; depth scales the signal inside that range).
class TidalModulator : public ModulationSource {
public:
    /// Shortest allowed layer period (seconds).
    static constexpr float kMinPeriod = 30.0f;
    /// Longest allowed layer period (seconds) - the roadmap's "10 min".
    static constexpr float kMaxPeriod = 600.0f;
    /// Relative detune of the second sine in each pair (the beat).
    static constexpr float kDetune = 0.02f;

    /// Number of layers (FR-031: exactly 3).
    static constexpr std::size_t kNumLayers = 3;
    /// Sines per layer (FR-031: a "sine pair").
    static constexpr std::size_t kSinesPerLayer = 2;
    /// Per-layer mix weight. kNumLayers * kLayerWeight == 1 (FR-033).
    static constexpr float kLayerWeight = 1.0f / 3.0f;
    /// Scale applied to a layer's summed sine pair, so |L_k| <= 1.
    static constexpr float kSinePairScale = 0.5f;

    /// Output smoothing time (ms). Block-boundary safety only - see SC-002.
    static constexpr float kOutputSmoothMs = 20.0f;

    static constexpr float kDefaultRate = 0.5f;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr std::uint32_t kDefaultTidalSeed = 0x71DAu;

    /// Fixed, mutually incommensurate irrational ratios (FR-032). Hardcoded,
    /// NOT seed-drawn: {1, sqrt(2), sqrt(3)}.
    static constexpr std::array<float, kNumLayers> kLayerRatios{
        1.0f, 1.41421356f, 1.73205081f};

    /// Slowest base period (seconds), i.e. the period of layer 0 at rate 0.
    /// Chosen so the SLOWEST layer lands exactly on kMaxPeriod there and no
    /// layer period ever needs clamping - the ratios above survive intact at
    /// every rate (FR-032).
    static constexpr float kMaxBasePeriod =
        kMaxPeriod / kLayerRatios[kNumLayers - 1];

    TidalModulator() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Derive per-sample phase increments and initialise state.
    /// After this call getCurrentValue() is well defined without any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make every phase increment non-finite)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        updateIncrements();
        outputSmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
        initState();
    }

    /// @brief Rewind to the exact post-prepare state (RNG and phases included).
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
    ///
    /// The seed varies ONLY the six initial sine phases - the layer periods and
    /// therefore all period statistics are seed independent (Clarifications Q3).
    /// @param seedValue Seed (0 is substituted by Xorshift32's default)
    void setSeed(std::uint32_t seedValue) noexcept {
        configuredSeed_ = seedValue;
        rng_.seed(seedValue);
    }

    /// @brief Single rate scalar for all three layers (FR-031).
    /// @param normalized 0 = slowest (P_base = kMaxBasePeriod, so the sqrt(3)
    ///        layer sits at the 10 min kMaxPeriod), 1 = fastest
    ///        (P_base = kMinPeriod)
    void setRate(float normalized) noexcept {
        rate_ = std::clamp(normalized, 0.0f, 1.0f);
        updateIncrements();
    }

    /// @brief Output depth. Scales the signal INSIDE the fixed source range.
    /// @param normalized 0..1
    void setDepth(float normalized) noexcept {
        depth_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }

    /// @brief Base period (seconds) for the current rate setting.
    [[nodiscard]] float getBasePeriodSeconds() const noexcept {
        return kMaxBasePeriod + (rate_ * (kMinPeriod - kMaxBasePeriod));
    }

    /// @brief Period (seconds) of layer @p layer.
    /// The [kMinPeriod, kMaxPeriod] clamp is inert by construction of
    /// kMaxBasePeriod (it can only bite on the last float ulp of the sqrt(3)
    /// layer at rate 0), so the 1 : sqrt(2) : sqrt(3) ratios hold at every rate.
    /// @param layer Layer index; out-of-range indices saturate to the last layer
    [[nodiscard]] float getLayerPeriodSeconds(std::size_t layer) const noexcept {
        const std::size_t k = std::min(layer, kNumLayers - 1);
        const float raw = getBasePeriodSeconds() * kLayerRatios[k];
        return std::clamp(raw, kMinPeriod, kMaxPeriod);
    }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample.
    void process() noexcept {
        advancePhases(1.0);
        outputSmoother_.setTarget(rawOutput());
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as RandomSource does (random_source.h:110) so the zero-warning
        // gate stays clean.
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Advance a whole block at control rate (FR-003).
    /// Equivalent to numSamples process() calls but O(1). processBlock(0) is a
    /// no-op that leaves state and output unchanged.
    /// @param numSamples Number of audio samples in this block
    void processBlock(std::size_t numSamples) noexcept {
        if (numSamples == 0) {
            return;
        }
        advancePhases(static_cast<double>(numSamples));
        outputSmoother_.setTarget(rawOutput());
        outputSmoother_.advanceSamples(numSamples);
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
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kInvTwoPi = 1.0 / kTwoPi;

    void updateIncrements() noexcept {
        for (std::size_t k = 0; k < kNumLayers; ++k) {
            const double period = static_cast<double>(getLayerPeriodSeconds(k));
            const double f0 = 1.0 / period;
            const double f1 = f0 * (1.0 + static_cast<double>(kDetune));
            inc_[k][0] = kTwoPi * f0 / sampleRate_;
            inc_[k][1] = kTwoPi * f1 / sampleRate_;
        }
    }

    void initState() noexcept {
        rng_.seed(configuredSeed_);
        for (std::size_t k = 0; k < kNumLayers; ++k) {
            for (std::size_t s = 0; s < kSinesPerLayer; ++s) {
                theta_[k][s] = static_cast<double>(rng_.nextUnipolar()) * kTwoPi;
            }
        }
        outputSmoother_.snapTo(rawOutput());
    }

    /// Advance every phase by @p numSamples samples, wrapping mod 2*pi so a
    /// multi-hour render cannot accumulate magnitude and lose precision.
    void advancePhases(double numSamples) noexcept {
        for (std::size_t k = 0; k < kNumLayers; ++k) {
            for (std::size_t s = 0; s < kSinesPerLayer; ++s) {
                double t = theta_[k][s] + (inc_[k][s] * numSamples);
                if (t >= kTwoPi || t < 0.0) {
                    t -= kTwoPi * std::floor(t * kInvTwoPi);
                }
                theta_[k][s] = t;
            }
        }
    }

    [[nodiscard]] float rawOutput() const noexcept {
        double sum = 0.0;
        for (std::size_t k = 0; k < kNumLayers; ++k) {
            const double layer = static_cast<double>(kSinePairScale) *
                                 (std::sin(theta_[k][0]) + std::sin(theta_[k][1]));
            sum += static_cast<double>(kLayerWeight) * layer;
        }
        const auto out = static_cast<float>(static_cast<double>(depth_) * sum);
        // Inert given the FR-033 coefficient-sum bound; kept as a hard guard.
        return std::clamp(out, -1.0f, 1.0f);
    }

    double sampleRate_ = 44100.0;

    /// Sine phases in radians, [layer][sine]. Double so long renders stay exact.
    std::array<std::array<double, kSinesPerLayer>, kNumLayers> theta_{};
    /// Per-sample phase increments in radians, [layer][sine].
    std::array<std::array<double, kSinesPerLayer>, kNumLayers> inc_{};

    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;

    std::uint32_t configuredSeed_ = kDefaultTidalSeed;
    Xorshift32 rng_{kDefaultTidalSeed};
    OnePoleSmoother outputSmoother_;
};

}  // namespace DSP
}  // namespace Krate
