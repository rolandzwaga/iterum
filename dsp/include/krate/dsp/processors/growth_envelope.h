// ==============================================================================
// Layer 2: DSP Processor - GrowthEnvelope (Seraphis Life Modulator)
// ==============================================================================
// One-shot logistic (S-curve) rise-and-hold over 1-60 seconds: the "the sound
// slowly becomes" behaviour of the Seraphis life-modulator family. Retriggerable
// with continuation - it never snaps back - and it has NO fall/release segment.
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-061..FR-063)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 6)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-061) - NORMALIZED logistic rise
// ------------------------------------------------------------------------------
//   tau  = elapsed / D                  in [0,1],  D = duration, clamp[1,60] s
//   L(t) = 1 / (1 + exp(-k*(t - 0.5))), k = kSteepness = 10
//   y(t) = (L(t) - L(0)) / (L(1) - L(0))
//
//   A bare logistic reaches neither 0 nor 1 in finite time; the normalization
//   makes y(0) EXACTLY 0 and y(1) EXACTLY 1 while keeping the monotone S shape.
//   L(0) and 1/(L(1)-L(0)) depend only on k, so they are precomputed once.
//
//   State machine (FR-062/FR-063):
//     Idle     - before the first trigger; getCurrentValue() reads 0 (bottom).
//     Rising   - elapsed advances; output is monotonic non-decreasing.
//     Complete - holds 1 forever. NO fall/release segment.
//   trigger(): Idle -> Rising (elapsed = 0); Rising -> no-op (CONTINUATION, it
//   never restarts); Complete -> no-op (holds at the top).
//
//   No RNG: this modulator is a deterministic state machine. setSeed() exists
//   only so the six life modulators share one API shape; it is a plain
//   non-virtual no-op (ModulationSource declares only getCurrentValue() and
//   getSourceRange() as virtuals - modulation_source.h:37,41 - so writing
//   `override` on it would not compile).
//
// ------------------------------------------------------------------------------
// SC-002 ANALYTIC JUSTIFICATION - bounded per-sample slew
// ------------------------------------------------------------------------------
//   The logistic's maximum slope is k/4 = 2.5 in tau units, attained at
//   tau = 0.5. After the normalization that becomes
//
//       (k/4) / (L(1) - L(0)) = 2.5 * 1.01357 ~= 2.534   per unit tau
//
//   and in wall-clock terms that is divided by the duration D, so the worst
//   case is the shortest duration, D = kMinDuration = 1 s:
//
//       |dy/dt| <= 2.534 / 1 s  ->  per sample @48 kHz <= 5.28e-5
//
//   SC-002's threshold is 1e-3 of the source-range span. GrowthEnvelope is
//   UNIPOLAR (span = 1), so the bound is 1.0e-3 absolute - roughly 19x the
//   worst-case delta. The one-pole output smoother (kOutputSmoothMs = 20 ms)
//   only ever reduces the delta; its completion snap is bounded by
//   kCompletionThreshold = 1e-4 (smoother.h:55), also inside the bound. The
//   smoother is present so a block-decimated advance (processBlock) cannot
//   introduce a step at the block boundary.
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - tau is clamped to [0,1] before the
//   exponential, so L(tau) is in (0,1) and y is in [0,1]; the smoother target is
//   clamped; and OnePoleSmoother::setTarget already sanitizes NaN/Inf
//   (smoother.h:170).
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief One-shot logistic rise-and-hold envelope (Seraphis life modulator).
///
/// Implements the ModulationSource interface. Advance it per sample with
/// process() or once per block with processBlock().
///
/// @par Output Range: [0.0, 1.0] (UNIPOLAR, FIXED - it does not change with any
///      setting; this is the one life modulator that is not bipolar).
class GrowthEnvelope : public ModulationSource {
public:
    /// Shortest total rise, in seconds (FR-061).
    static constexpr float kMinDuration = 1.0f;
    /// Longest total rise, in seconds (FR-061).
    static constexpr float kMaxDuration = 60.0f;
    /// Default rise duration, in seconds.
    static constexpr float kDefaultDuration = 10.0f;
    /// Logistic steepness. Larger = flatter tails and a steeper middle.
    static constexpr float kSteepness = 10.0f;
    /// Output smoothing time (ms). Block-boundary safety only - see SC-002.
    static constexpr float kOutputSmoothMs = 20.0f;

    GrowthEnvelope() noexcept {
        updateShapeConstants();
    }

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Derive per-sample increments and initialise state.
    /// After this call getCurrentValue() is well defined (0) without any advance.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        // Floored at 1 Hz: a zero/negative rate would make sampleDtSeconds_
        // non-finite and poison the elapsed-time accumulator.
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        sampleDtSeconds_ = 1.0 / sampleRate_;
        updateShapeConstants();
        outputSmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
        initState();
    }

    /// @brief Rewind to the exact post-prepare state: Idle, elapsed 0, output 0.
    /// Keeps the configured sample rate and duration.
    void reset() noexcept {
        initState();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief No-op seed setter (this modulator owns no RNG).
    /// Present only so all six life modulators share one API shape. Plain
    /// non-virtual member - never `override` (modulation_source.h:37,41).
    void setSeed(std::uint32_t /*seedValue*/) noexcept {}

    /// @brief Total rise duration in seconds (FR-061), clamped to [1, 60].
    /// @param seconds Requested duration
    void setDuration(float seconds) noexcept {
        duration_ = static_cast<double>(std::clamp(seconds, kMinDuration, kMaxDuration));
    }

    /// @brief Effective (clamped) rise duration in seconds.
    [[nodiscard]] float getDuration() const noexcept {
        return static_cast<float>(duration_);
    }

    // -------------------------------------------------------------------------
    // Trigger (FR-062)
    // -------------------------------------------------------------------------

    /// @brief Start the rise.
    /// Idle -> Rising. While Rising this is a no-op so the envelope CONTINUES
    /// from its current value instead of snapping back. After completion it is
    /// also a no-op - the envelope holds at the top.
    void trigger() noexcept {
        if (phase_ == Phase::Idle) {
            phase_ = Phase::Rising;
            elapsed_ = 0.0;
        }
    }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample.
    void process() noexcept {
        advanceSeconds(sampleDtSeconds_);
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as RandomSource does (random_source.h:110) so the zero-warning
        // gate stays clean.
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Advance a whole block at control rate (FR-003).
    /// Equivalent to numSamples process() calls but O(1).
    /// processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        if (numSamples == 0) {
            return;
        }
        advanceSeconds(static_cast<double>(numSamples) * sampleDtSeconds_);
        outputSmoother_.advanceSamples(numSamples);
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-001)
    // -------------------------------------------------------------------------

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), 0.0f, 1.0f);
    }

    /// @brief Fixed at polarity full scale - unipolar, never shrinks (FR-006).
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {0.0f, 1.0f};
    }

private:
    enum class Phase { Idle, Rising, Complete };

    /// Raw (un-normalized) logistic at tau.
    [[nodiscard]] static double rawLogistic(double tau) noexcept {
        return 1.0 / (1.0 + std::exp(-static_cast<double>(kSteepness) * (tau - 0.5)));
    }

    /// Precompute L(0) and 1/(L(1)-L(0)); both depend only on kSteepness.
    void updateShapeConstants() noexcept {
        const double l0 = rawLogistic(0.0);
        const double l1 = rawLogistic(1.0);
        const double span = l1 - l0;
        l0_ = l0;
        invSpan_ = (span > 0.0) ? (1.0 / span) : 1.0;
    }

    /// Normalized S-curve: exactly 0 at tau = 0, exactly 1 at tau = 1.
    [[nodiscard]] float shapeAt(double tau) const noexcept {
        const double t = std::clamp(tau, 0.0, 1.0);
        const double y = (rawLogistic(t) - l0_) * invSpan_;
        return static_cast<float>(std::clamp(y, 0.0, 1.0));
    }

    void initState() noexcept {
        phase_ = Phase::Idle;
        elapsed_ = 0.0;
        outputSmoother_.snapTo(0.0f);
    }

    /// Advance wall-clock time and refresh the smoother target.
    /// A no-op unless the envelope is Rising: while Idle the target stays 0 and
    /// while Complete it stays 1 (no fall segment, FR-063).
    void advanceSeconds(double deltaSeconds) noexcept {
        if (phase_ != Phase::Rising) {
            return;
        }
        elapsed_ += deltaSeconds;
        if (elapsed_ >= duration_) {
            elapsed_ = duration_;
            phase_ = Phase::Complete;
        }
        outputSmoother_.setTarget(shapeAt(elapsed_ / duration_));
    }

    Phase phase_ = Phase::Idle;

    double sampleRate_ = 44100.0;
    double sampleDtSeconds_ = 1.0 / 44100.0;
    double elapsed_ = 0.0;
    double duration_ = static_cast<double>(kDefaultDuration);

    double l0_ = 0.0;       ///< L(0), precomputed
    double invSpan_ = 1.0;  ///< 1/(L(1) - L(0)), precomputed

    OnePoleSmoother outputSmoother_;
};

}  // namespace DSP
}  // namespace Krate
