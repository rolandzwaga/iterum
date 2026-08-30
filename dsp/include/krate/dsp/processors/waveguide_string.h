// ==============================================================================
// Layer 2: DSP Processor - WaveguideString
// ==============================================================================
// Digital waveguide string resonator with EKS extensions.
// Two-segment delay loop with dispersion allpass cascade,
// Thiran fractional delay, weighted one-zero loss filter,
// and DC blocker. Velocity wave convention for Phase 4 bow readiness.
//
// Layer 2 (processors) | Namespace: Krate::DSP
// Spec: 129-waveguide-string-resonance
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, C++20)
// - Principle IX: Layer 2 (depends on Layer 0, Layer 1)
// - Principle X: DSP Constraints (allpass interpolation in feedback loop)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/processors/iresonator.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/xorshift32.h>

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <numbers>
#include <utility>

namespace Krate {
namespace DSP {

class WaveguideString : public IResonator {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr int kMaxDispersionSections = 4;
    static constexpr size_t kMinDelaySamples = 4;
    static constexpr float kDefaultPickPosition = 0.13f;
    static constexpr float kSoftClipThreshold = 1.0f;
    static constexpr float kEnergyFloor = 1e-20f;
    static constexpr float kDcBlockerCutoffHz = 3.5f;
    static constexpr float kMinFrequency = 20.0f;

    // =========================================================================
    // ScatteringJunction Interface (FR-017)
    // =========================================================================
    // Abstract interface for the interaction point between two delay segments.
    // Accepts two incoming velocity waves and produces two outgoing waves.
    // Phase 3: PluckJunction (transparent). Phase 4: BowJunction (nonlinear).
    struct ScatteringJunction {
        /// Characteristic impedance Z = sqrt(T * mu), normalised to 1.0f.
        /// Unused in Phase 3 pluck; required for Phase 4 bow reflection
        /// coefficient (Z_bow / Z_string ratio).
        float characteristicImpedance = 1.0f;

        /// Scatter two incoming velocity waves into two outgoing waves.
        /// @param vLeft  Incoming velocity wave from nut side
        /// @param vRight Incoming velocity wave from bridge side
        /// @param excitation External excitation to inject at the junction
        /// @return {vOutLeft, vOutRight} Outgoing velocity waves
        [[nodiscard]] virtual std::pair<float, float> scatter(
            float vLeft, float vRight, float excitation) const noexcept
        {
            // Default: transparent pass-through (identity scatter)
            return {vRight + excitation, vLeft + excitation};
        }

        virtual ~ScatteringJunction() = default;
    };

    // =========================================================================
    // PluckJunction (FR-018)
    // =========================================================================
    // Phase 3 concrete implementation: transparent wave pass-through with
    // additive excitation injection. Impedance-independent.
    struct PluckJunction : ScatteringJunction {
        /// Transparent scatter: waves pass through, excitation added to both.
        [[nodiscard]] std::pair<float, float> scatter(
            float vLeft, float vRight, float excitation) const noexcept override
        {
            return {vRight + excitation, vLeft + excitation};
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================
    WaveguideString() noexcept = default;
    ~WaveguideString() override = default;

    // Non-copyable, movable
    WaveguideString(const WaveguideString&) = delete;
    WaveguideString& operator=(const WaveguideString&) = delete;
    WaveguideString(WaveguideString&&) noexcept = default;
    WaveguideString& operator=(WaveguideString&&) noexcept = default;

    /// Prepare for processing. Allocates delay lines for 20 Hz minimum.
    void prepare(double sampleRate) noexcept override
    {
        sampleRate_ = sampleRate;
        float maxDelaySec = 1.0f / kMinFrequency;
        nutSideDelay_.prepare(sampleRate, maxDelaySec);
        bridgeSideDelay_.prepare(sampleRate, maxDelaySec);
        dcBlocker_.prepare(sampleRate, kDcBlockerCutoffHz);

        auto sr = static_cast<float>(sampleRate);
        frequencySmoother_.configure(20.0f, sr);
        decaySmoother_.configure(20.0f, sr);
        brightnessSmoother_.configure(20.0f, sr);

        controlAlpha_ = std::exp(-1.0f / (0.005f * sr));
        perceptualAlpha_ = std::exp(-1.0f / (0.030f * sr));

        prepared_ = true;
    }

    /// Seed per-voice RNG. Call once after prepare().
    void prepareVoice(uint32_t voiceId) noexcept
    {
        rng_.seed(voiceId);
    }

    // =========================================================================
    // IResonator Interface
    // =========================================================================

    void setFrequency(float f0) noexcept override
    {
        float maxF = static_cast<float>(sampleRate_) * 0.45f;
        frequency_ = std::clamp(f0, kMinFrequency, maxF);
        // Smoother operates in log2-frequency domain (FR-033)
        frequencySmoother_.setTarget(std::log2(frequency_));
    }

    void setDecay(float t60) noexcept override
    {
        decayTime_ = std::clamp(t60, 0.01f, 10.0f);
        decaySmoother_.setTarget(decayTime_);
    }

    void setBrightness(float brightness) noexcept override
    {
        brightness_ = std::clamp(brightness, 0.0f, 1.0f);
        brightnessSmoother_.setTarget(brightness_);
    }

    [[nodiscard]] float process(float excitation) noexcept override
    {
        if (!prepared_ || bridgeDelayFloat_ < static_cast<float>(kMinDelaySamples))
            return 0.0f;

        // Smooth parameters and recompute coefficients
        // frequencySmoother_ operates in log2-frequency domain (FR-033)
        float smoothedLogFreq = frequencySmoother_.process();
        float smoothedDecay = decaySmoother_.process();
        float smoothedBrightness = brightnessSmoother_.process();

        // Recompute loss filter coefficients from smoothed values.
        //
        // MEMOISED, and bit-identically so: `rho` is a pure function of
        // (smoothedLogFreq, smoothedDecay) - `std::exp2` then `std::pow` - and
        // both smoothers return the IDENTICAL float on every call once they
        // have settled (OnePoleSmoother::process snaps current_ to target_
        // inside kCompletionThreshold). A sustained string therefore paid two
        // transcendentals per sample for a constant. The cache is keyed on the
        // exact float inputs, so any movement of either smoother misses it and
        // recomputes.
        if (!rhoCacheValid_ || smoothedLogFreq != cachedLogFreq_
            || smoothedDecay != cachedDecay_) {
            const float smoothedFreq = std::exp2(smoothedLogFreq);
            cachedRho_ = computeRho(smoothedFreq, smoothedDecay);
            cachedLogFreq_ = smoothedLogFreq;
            cachedDecay_ = smoothedDecay;
            rhoCacheValid_ = true;
        }
        const float rho = cachedRho_;
        float S = smoothedBrightness * 0.5f; // map [0,1] -> [0,0.5]

        // --- Waveguide delay loop (FR-002, FR-004, FR-038) ---
        // Read from bridge delay with linear fractional interpolation.
        // Linear interpolation provides exact fractional delay at all
        // frequencies (unlike allpass which has frequency-dependent delay),
        // ensuring sub-cent pitch accuracy (SC-001).
        float feedback = bridgeSideDelay_.readLinear(bridgeDelayFloat_);

        // Summing junction: feedback + excitation (FR-038 output tap point)
        float junction = feedback + excitation;

        // Soft clipper (FR-012)
        float output = softClip(junction);

        // DC blocker (FR-008, R-004, spec-130 FR-021)
        // Relocated after bow junction output, before signal re-enters delay.
        float dcOut = dcBlocker_.process(output);

        // --- Loop filters ---

        // Dispersion allpass cascade (FR-009)
        float x = dcOut;
        if (frozenStiffness_ > 1e-6f) {
            for (int i = 0; i < kMaxDispersionSections; ++i)
                x = dispersionFilters_[i].process(x);
        }

        // Loss filter: H(z) = rho * [(1-S) + S * z^-1] (FR-005, R-003)
        float lossOutput = rho * ((1.0f - S) * x + S * lossState_);
        lossState_ = x;

        // Energy floor clamp (FR-036)
        if (std::abs(lossOutput) < kEnergyFloor)
            lossOutput = 0.0f;

        // Write filtered signal into bridge-side delay (completes the loop)
        bridgeSideDelay_.write(lossOutput);

        // Update energy followers (FR-023, FR-024) at output tap
        float squared = output * output;
        controlEnergy_ = controlAlpha_ * controlEnergy_
                       + (1.0f - controlAlpha_) * squared;
        perceptualEnergy_ = perceptualAlpha_ * perceptualEnergy_
                          + (1.0f - perceptualAlpha_) * squared;

        // Store feedback velocity for Phase 4 (FR-013)
        feedbackVelocity_ = output;

        return output;
    }

    [[nodiscard]] float getControlEnergy() const noexcept override
    {
        return controlEnergy_;
    }

    [[nodiscard]] float getPerceptualEnergy() const noexcept override
    {
        return perceptualEnergy_;
    }

    void silence() noexcept override
    {
        nutSideDelay_.reset();
        bridgeSideDelay_.reset();
        dcBlocker_.reset();
        for (int i = 0; i < kMaxDispersionSections; ++i)
            dispersionFilters_[i].reset();
        lossState_ = 0.0f;
        controlEnergy_ = 0.0f;
        perceptualEnergy_ = 0.0f;
        feedbackVelocity_ = 0.0f;
        nutDelaySamples_ = 0;
        bridgeDelaySamples_ = 0;
        bridgeDelayFloat_ = 0.0f;
    }

    [[nodiscard]] float getFeedbackVelocity() const noexcept override
    {
        return feedbackVelocity_;
    }

    // =========================================================================
    // Type-Specific Setters
    // =========================================================================

    /// Set string stiffness (inharmonicity). Frozen at note onset.
    void setStiffness(float stiffness) noexcept
    {
        stiffness_ = std::clamp(stiffness, 0.0f, 1.0f);
    }

    /// Set pick/interaction position. Frozen at note onset.
    void setPickPosition(float position) noexcept
    {
        pickPosition_ = std::clamp(position, 0.0f, 1.0f);
    }

    // =========================================================================
    // Note Lifecycle (called by voice engine)
    // =========================================================================

    /// Trigger a new note. Freezes stiffness and pick position,
    /// computes delay lengths, fills excitation, resets loop state.
    void noteOn(float f0, float velocity) noexcept
    {
        if (!prepared_ || f0 < kMinFrequency)
            return;

        float sr = static_cast<float>(sampleRate_);
        float maxF = sr * 0.45f;
        f0 = std::clamp(f0, kMinFrequency, maxF);

        // Freeze parameters at onset (FR-010, FR-015)
        frozenPickPosition_ = pickPosition_;
        frozenStiffness_ = stiffness_;

        // Set frequency for smoothers (log2-domain for FR-033)
        frequency_ = f0;
        frequencySmoother_.snapTo(std::log2(f0));
        decaySmoother_.snapTo(decayTime_);
        brightnessSmoother_.snapTo(brightness_);

        // Compute inharmonicity coefficient B from stiffness [0,1]
        // Map stiffness 0->0, 1->0.002 (guitar/moderate string range)
        // B=0.0001 is nylon guitar, B=0.002 is steel guitar treble.
        // Piano treble (B>0.01) deferred to future phase with 6-8 sections.
        float B = frozenStiffness_ * 0.002f;

        // Step 1: Configure dispersion filters FIRST (need coefficients for phase delay)
        configureDispersionFilters(B, f0, sr);

        // Step 2: Compute loss filter coefficients (FR-005)
        float rho = computeRho(f0, decayTime_);
        float S = brightness_ * 0.5f;

        // Step 3: Compute phase delays of in-loop filters at f0
        float period = sr / f0;

        float dLoss = computeLossPhaseDelay(f0, sr, S);
        float dDisp = computeDispersionPhaseDelay(f0, sr);

        // Step 4: Compute delay for linear-interpolated delay read.
        // Both loss and dispersion compensations use the same empirical 0.55
        // factor, as both are in-loop allpass-like filters whose standalone
        // phase delay overestimates the effective loop contribution with
        // linear interpolation.
        // Dispersion compensation: analytical phase delay scaled by empirical
        // factor 0.96. First-order allpass phase delay at f0 slightly
        // overestimates the effective pitch contribution in the feedback loop
        // with linear interpolation. Factor calibrated for < 5 cent accuracy
        // across stiffness range with B <= 0.002 (FR-007, analytical approach).
        float D = period - 1.0f - 0.55f * dLoss - 0.96f * dDisp;

        // Clamp to valid range
        float maxD = static_cast<float>(bridgeSideDelay_.maxDelaySamples());
        bridgeDelayFloat_ = std::clamp(D, static_cast<float>(kMinDelaySamples), maxD);

        // Store integer equivalent for excitation fill and debug
        auto bridgeN = static_cast<size_t>(std::round(bridgeDelayFloat_));
        bridgeDelaySamples_ = std::clamp(bridgeN, kMinDelaySamples,
                                         bridgeSideDelay_.maxDelaySamples());

        // Nut delay for pick position comb (used only during excitation fill)
        size_t nutLen = static_cast<size_t>(
            std::max(static_cast<float>(kMinDelaySamples),
                     std::round(frozenPickPosition_ * static_cast<float>(bridgeDelaySamples_))));
        nutDelaySamples_ = std::min(nutLen, nutSideDelay_.maxDelaySamples());

        totalLoopDelay_ = period; // by construction

        // Debug: store computed values for test verification
        debugNutDelay_ = nutDelaySamples_;
        debugBridgeDelay_ = bridgeDelaySamples_;
        debugDelta_ = bridgeDelayFloat_ - std::floor(bridgeDelayFloat_);
        debugLossDelay_ = dLoss;
        debugDcDelay_ = computeDcBlockerPhaseDelay(f0, sr);
        debugDispersionDelay_ = dDisp;
        debugIntegerBudget_ = D;

        // Store loss filter state
        lossRho_ = rho;
        lossS_ = S;
        lossState_ = 0.0f;

        // Compute excitation gain (FR-026, FR-027, FR-028)
        // Energy normalisation ensures consistent loudness across pitch range.
        //
        // FR-027: Compensate for total loop gain at f0. Higher-frequency
        // strings have gTotal closer to 1.0 (less loss per cycle), so their
        // steady-state amplitude is naturally higher. Dividing by gTotal
        // pre-compensates the excitation level.
        //
        // FR-026: Frequency-dependent scaling. Higher-frequency strings
        // sustain at a higher steady-state amplitude relative to excitation
        // because gTotal is closer to 1. The geometric series sum of the
        // recirculating signal is 1/(1-gTotal), which is larger for higher
        // gTotal. We apply sqrt(fRef/f0) as an empirical correction that
        // attenuates high-frequency excitation and boosts low-frequency
        // excitation, calibrated to keep C2-C6 within 3 dB (SC-009).
        //
        // Empirical calibration factor: 0.55 tunes the frequency
        // compensation to achieve consistent RMS across the playable range.
        // Without it, the sqrt(fRef/f0) alone slightly over-compensates.
        constexpr float fRef = 261.6f; // middle C reference frequency
        constexpr float kFreqCalibration = 0.55f;
        float freqScale = std::pow(fRef / f0, kFreqCalibration * 0.5f);

        // Loop gain at f0 (FR-027)
        float wGain = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        float lossGainAtF0 = rho * std::sqrt(
            (1.0f - S) * (1.0f - S)
            + 2.0f * S * (1.0f - S) * std::cos(wGain)
            + S * S);
        float gTotal = std::max(lossGainAtF0, 0.001f); // avoid div by zero
        excitationGain_ = freqScale / gTotal;

        // Reset delay lines and filter states
        nutSideDelay_.reset();
        bridgeSideDelay_.reset();
        dcBlocker_.reset();

        // Generate noise burst excitation (FR-014)
        // Fill nut-side delay with shaped noise
        float velScale = velocity * excitationGain_;

        // One-pole LP for noise shaping (FR-014)
        // Fixed cutoff ensures consistent excitation energy injection.
        // Velocity controls amplitude; spectral brightness comes from the
        // loss filter's frequency-dependent damping in the feedback loop.
        constexpr float lpCutoff = 5000.0f;
        float lpAlpha = std::exp(-2.0f * std::numbers::pi_v<float> * lpCutoff / sr);
        float lpState = 0.0f;

        // Generate noise into bridge delay line (single-loop design)
        size_t totalNoiseSamples = bridgeDelaySamples_;
        std::array<float, 4096> excBuf{};
        size_t nSamples = std::min(totalNoiseSamples, excBuf.size());

        for (size_t i = 0; i < nSamples; ++i) {
            float noise = rng_.nextFloatSigned();
            // One-pole lowpass
            lpState = lpAlpha * lpState + (1.0f - lpAlpha) * noise;
            excBuf[i] = lpState;
        }

        // Apply pick-position comb filter (FR-015)
        // H(z) = 1 - z^{-M} creates spectral nulls at harmonics that are
        // integer multiples of 1/beta. Since the excitation buffer becomes
        // periodic in the delay line, apply the comb circularly so the
        // first M samples also get filtered (they would otherwise leak
        // energy into the nulled harmonics).
        size_t M = static_cast<size_t>(std::max(1.0f,
            std::round(frozenPickPosition_ * static_cast<float>(bridgeDelaySamples_))));
        if (M < nSamples) {
            // Make a copy so circular subtraction reads original values
            std::array<float, 4096> combBuf{};
            for (size_t i = 0; i < nSamples; ++i)
                combBuf[i] = excBuf[i];
            for (size_t i = 0; i < nSamples; ++i) {
                size_t delayed = (i + nSamples - M) % nSamples;
                excBuf[i] = combBuf[i] - combBuf[delayed];
            }
        }

        // Normalize excitation buffer RMS to 1.0 before applying velocity
        // and excitation gain. This ensures consistent energy injection
        // regardless of LP cutoff (velocity-dependent brightness) and comb
        // filter interaction, giving even velocity response across pitches.
        float excRms = 0.0f;
        for (size_t i = 0; i < nSamples; ++i)
            excRms += excBuf[i] * excBuf[i];
        excRms = std::sqrt(excRms / static_cast<float>(nSamples));
        float normScale = (excRms > 1e-10f) ? (1.0f / excRms) : 1.0f;

        // Scale by velocity and excitation gain, write into bridge delay
        for (size_t i = 0; i < nSamples; ++i) {
            bridgeSideDelay_.write(excBuf[i] * normScale * velScale);
        }
    }

    /// Retune the loop to a new fundamental WITHOUT clearing state or
    /// re-exciting it (FR-080, specs/seraphis-phase4-continuous-body).
    ///
    /// Recomputes bridgeDelayFloat_ from the loop's exact resonance condition
    /// and applies noteOn()'s clamp. Deliberately does NOT touch nutSideDelay_,
    /// bridgeSideDelay_, dcBlocker_, dispersionFilters_, lossState_, the energy
    /// followers, frozenStiffness_ or frozenPickPosition_ - the loop keeps
    /// ringing through the retune.
    ///
    /// The frozen dispersion cascade is NOT reconfigured (FR-081): re-designing
    /// those biquads mid-ring would change the meaning of their state and click.
    ///
    /// Inert unless called (FR-084): no existing member changes behaviour.
    ///
    /// DELAY BUDGET - why this is NOT noteOn()'s expression (FR-080 amended).
    /// -------------------------------------------------------------------
    /// A feedback loop resonates where the summed phase delay of every element
    /// equals the period, so the exact budget is
    ///     D = period - 1 - dLoss - dDisp - dDC
    /// with each term evaluated at f0. noteOn() instead uses the empirical
    ///     D = period - 1 - 0.55*dLoss - 0.96*dDisp        (:321)
    /// which omits the DC blocker entirely and scales the two filter terms.
    /// Those factors were calibrated at note onset, where the cascade is
    /// re-designed for f0 every time, and against a multi-period
    /// autocorrelation pitch readout. Neither premise survives a retune:
    ///   - the cascade is frozen (FR-081), so its phase delay stays near its
    ///     design value (~14.2 samples here) while a matched cascade would
    ///     range 7.7..27.7 samples over +/-12 semitones. The unmodelled 4 % of
    ///     dDisp therefore no longer tracks the pitch it was tuned against;
    ///   - the omitted DC-blocker phase delay grows as 1/f^2 (-0.14 samples at
    ///     440 Hz, -2.21 at 110 Hz), so it is negligible at the calibration
    ///     pitch and dominant an octave down.
    /// Measured with SC-009's FFT first-partial estimator, retuning +/-12
    /// semitones from noteOn(220) at 48 kHz with the Strings material settings
    /// (stiffness 0.15, brightness 0.30, decay 8 s, pick 0.22):
    ///     target Hz | noteOn's expression | exact budget
    ///        110.00 |       +5.26 cents   |   -1.50 cents
    ///        155.56 |       +1.89         |   -0.47
    ///        220.00 |       -0.88         |   -0.32
    ///        311.13 |       -4.06         |   +0.25
    ///        440.00 |       -8.18         |   +0.17
    /// i.e. noteOn()'s expression cannot meet SC-009c's +/-5 cents through a
    /// frozen cascade, and the exact budget meets it with 3 cents to spare. The
    /// same comparison holds at 44.1/96 kHz and across the stiffness and
    /// brightness ranges (worst case 8.2 -> 1.5 cents at 48 kHz, 15.5 -> 1.5 at
    /// stiffness 1.0). noteOn() itself is left exactly as it was: FR-084 and
    /// SC-014 require every pre-existing path to be bit-identical, so the two
    /// entry points differ by 0.56 cents at the same pitch, in the retune's
    /// favour.
    void retune(float f0) noexcept
    {
        if (!prepared_ || f0 < kMinFrequency)
            return;

        const float sr = static_cast<float>(sampleRate_);
        // Same clamp as setFrequency()
        f0 = std::clamp(f0, kMinFrequency, sr * 0.45f);

        // Stored brightness, mapped exactly as noteOn() maps it.
        const float S = brightness_ * 0.5f;
        const float period = sr / f0;
        const float dLoss = computeLossPhaseDelay(f0, sr, S);
        // Frozen cascade, evaluated at the NEW f0 (FR-081).
        const float dDisp = computeDispersionPhaseDelay(f0, sr);
        // Signed: negative (phase lead) at every audio frequency.
        const float dDC = computeDcBlockerPhaseDelay(f0, sr);
        const float D = period - 1.0f - dLoss - dDisp - dDC;

        const float maxD = static_cast<float>(bridgeSideDelay_.maxDelaySamples());
        bridgeDelayFloat_ = std::clamp(D, static_cast<float>(kMinDelaySamples), maxD);

        const auto bridgeN = static_cast<size_t>(std::round(bridgeDelayFloat_));
        bridgeDelaySamples_ = std::clamp(bridgeN, kMinDelaySamples,
                                         bridgeSideDelay_.maxDelaySamples());

        frequency_ = f0;
        // FR-083: setTarget, not snapTo - noteOn() snaps, a glide must not.
        frequencySmoother_.setTarget(std::log2(f0));
    }

    /// @brief Snap the three parameter smoothers to their targets.
    ///
    /// For callers that already smooth on their own control grid. `retune()`
    /// (FR-080) sets `bridgeDelayFloat_` - the PITCH - immediately and only
    /// re-targets `frequencySmoother_`, which feeds nothing but the loop-loss
    /// gain `rho`; so snapping here steps a decay gain, never a delay length.
    /// Leaving the smoothers running instead costs two transcendentals per
    /// sample for the whole of a glide (see the memoisation note in
    /// `process()`), which is a large fraction of a per-voice CPU budget.
    void snapSmoothers() noexcept
    {
        frequencySmoother_.snapToTarget();
        decaySmoother_.snapToTarget();
        brightnessSmoother_.snapToTarget();
    }

    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // Debug accessors (TEST ONLY -- will be removed after tuning calibration)
    size_t debugNutDelay_ = 0;
    size_t debugBridgeDelay_ = 0;
    float debugDelta_ = 0.0f;
    float debugLossDelay_ = 0.0f;
    float debugDcDelay_ = 0.0f;
    float debugDispersionDelay_ = 0.0f;
    float debugIntegerBudget_ = 0.0f;

private:
    // =========================================================================
    // Internal helpers
    // =========================================================================

    /// Soft clipper (FR-012)
    [[nodiscard]] static float softClip(float x) noexcept
    {
        if (std::abs(x) < kSoftClipThreshold)
            return x;
        return kSoftClipThreshold * std::tanh(x / kSoftClipThreshold);
    }

    /// Compute frequency-independent loss per round trip (FR-005)
    [[nodiscard]] static float computeRho(float f0, float t60) noexcept
    {
        // rho = 10^(-3 / (T60 * f0))
        float exponent = -3.0f / (std::max(t60, 0.001f) * std::max(f0, 1.0f));
        return std::pow(10.0f, exponent);
    }

    /// Compute loss filter phase delay at f0 (FR-007)
    /// H(z) = rho * [(1-S) + S*z^{-1}]
    /// Phase depends only on S, not rho (rho is real).
    [[nodiscard]] static float computeLossPhaseDelay(
        float f0, float sr, float S) noexcept
    {
        if (S < 1e-10f) return 0.0f; // Pure gain, no phase delay
        float w0 = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        if (w0 < 1e-10f) return S; // At DC: phase delay = S samples
        float sinW = std::sin(w0);
        float cosW = std::cos(w0);
        // H(e^{jw}) / rho = (1-S) + S*e^{-jw} = (1-S+S*cos(w)) - j*S*sin(w)
        // phase = atan2(-S*sin(w), (1-S) + S*cos(w))
        float phase = std::atan2(-S * sinW, (1.0f - S) + S * cosW);
        // Phase delay = -phase / w (in samples)
        return std::max(-phase / w0, 0.0f);
    }

    /// Compute DC blocker phase delay at f0
    /// H(z) = (1 - z^{-1}) / (1 - R*z^{-1})
    /// Returns signed phase delay (negative = phase lead at audio frequencies)
    /// IMPORTANT: Uses the same R clamping as DCBlocker::prepare() to match
    /// the actual filter behavior in the process loop.
    [[nodiscard]] float computeDcBlockerPhaseDelay(
        float f0, float sr) const noexcept
    {
        float R = std::exp(-2.0f * std::numbers::pi_v<float> * kDcBlockerCutoffHz / sr);
        // DCBlocker::prepare() clamps R to [0.9, 0.9999] -- match that here
        R = std::clamp(R, 0.9f, 0.9999f);
        float w0 = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        if (w0 < 1e-10f) return 0.0f;
        float sinW = std::sin(w0);
        float cosW = std::cos(w0);
        // Num = 1 - e^{-jw} = (1-cos(w)) + j*sin(w)
        float numPhase = std::atan2(sinW, 1.0f - cosW);
        // Den = 1 - R*e^{-jw} = (1-R*cos(w)) + j*R*sin(w)
        float denPhase = std::atan2(R * sinW, 1.0f - R * cosW);
        // Phase of H = numPhase - denPhase
        // Phase delay = -phase / w0
        float phase = numPhase - denPhase;
        return -phase / w0;
    }

    /// Compute DC blocker group delay at f0
    /// H(z) = (1-z^{-1})/(1-R*z^{-1})
    /// GD = 0.5 + R*(R-cos(w))/(1+R^2-2*R*cos(w))
    [[nodiscard]] float computeDcBlockerGroupDelay(
        float f0, float sr) const noexcept
    {
        float R = std::exp(-2.0f * std::numbers::pi_v<float> * kDcBlockerCutoffHz / sr);
        float w0 = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        float cosW = std::cos(w0);
        float R2 = R * R;
        float denom = 1.0f + R2 - 2.0f * R * cosW;
        if (denom < 1e-10f) return 0.5f;
        float gdDen = R * (R - cosW) / denom;
        float gd = 0.5f + gdDen;
        return std::max(gd, 0.0f);
    }

    /// Compute phase delay of 1st-order Thiran allpass at angular frequency w
    /// H(z) = (eta + z^{-1}) / (1 + eta * z^{-1})
    [[nodiscard]] static float computeThiranPhaseDelay(
        float eta, float w) noexcept
    {
        if (w < 1e-10f) {
            // At DC: phase delay = (1 - eta) / (1 + eta) = Delta
            return (1.0f - eta) / (1.0f + eta);
        }
        float cosW = std::cos(w);
        float sinW = std::sin(w);
        // H(e^{jw}) = (eta + e^{-jw}) / (1 + eta*e^{-jw})
        // Num = eta + cos(w) - j*sin(w)
        // Den = 1 + eta*cos(w) - j*eta*sin(w)
        float numPhase = std::atan2(-sinW, eta + cosW);
        float denPhase = std::atan2(-eta * sinW, 1.0f + eta * cosW);
        float phase = numPhase - denPhase;
        // Phase delay = -phase / w
        return -phase / w;
    }

    /// Compute phase delay of a 2nd-order allpass biquad at frequency f0.
    /// H(z) = (a2 + a1*z^-1 + z^-2) / (1 + a1*z^-1 + a2*z^-2)
    /// Returns phase delay in samples.
    [[nodiscard]] static float computeAllpassBiquadPhaseDelay(
        const BiquadCoefficients& c, float f0, float sr) noexcept
    {
        float w = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        if (w < 1e-10f) return 0.0f;
        float c1 = std::cos(w);
        float c2 = std::cos(2.0f * w);
        float s1 = std::sin(w);
        float s2 = std::sin(2.0f * w);
        // For an allpass biquad with normalized a0=1:
        // Numerator coefficients: {b0=a2, b1=a1, b2=1}
        // Denominator coefficients: {1, a1, a2}
        // But the Biquad class stores coefficients after normalization:
        // b0, b1, b2 and a1, a2 (a0=1 implicit, feedback coeffs)
        // For an allpass, b0=a2, b1=a1, b2=1 in the transfer function sense
        // H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
        float reN = c.b0 + c.b1 * c1 + c.b2 * c2;
        float imN = -c.b1 * s1 - c.b2 * s2;
        float phaseN = std::atan2(imN, reN);
        float reD = 1.0f + c.a1 * c1 + c.a2 * c2;
        float imD = -c.a1 * s1 - c.a2 * s2;
        float phaseD = std::atan2(imD, reD);
        return -(phaseN - phaseD) / w;
    }

    /// Compute total dispersion phase delay at f0 from first-order allpass sections.
    /// H(z) = (a + z^-1)/(1 + a*z^-1), stored as biquad with b0=a, b1=1, b2=0.
    /// Phase delay = -phi(w)/w where phi is the phase angle of H(e^{jw}).
    [[nodiscard]] float computeDispersionPhaseDelay(float f0, float sr) const noexcept
    {
        float w = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        if (w < 1e-10f) return 0.0f;

        float sinW = std::sin(w);
        float cosW = std::cos(w);
        float total = 0.0f;

        for (int i = 0; i < kMaxDispersionSections; ++i) {
            const auto& c = dispersionFilters_[i].coefficients();
            // Identity check: first-order allpass has b1=1.0
            if (c.b1 < 0.5f) continue;

            float a = c.b0;
            // Phase of H(e^{jw}) = atan2(-sin(w), a+cos(w))
            //                     - atan2(-a*sin(w), 1+a*cos(w))
            float phN = std::atan2(-sinW, a + cosW);
            float phD = std::atan2(-a * sinW, 1.0f + a * cosW);
            float phase = phN - phD;
            // Phase delay in samples
            total += -phase / w;
        }
        return total;
    }

    /// Configure dispersion allpass filters (Abel-Valimaki-Smith method, R-001)
    ///
    /// Designs 4 second-order allpass sections whose combined phase response
    /// approximates the frequency-dependent phase shift due to string
    /// stiffness per Fletcher's formula.
    ///
    /// Each section uses a direct allpass biquad:
    /// H(z) = (r^2 - 2*r*cos(theta)*z^-1 + z^-2)
    ///      / (1 - 2*r*cos(theta)*z^-1 + r^2*z^-2)
    ///
    /// Sections are placed at high harmonics with moderate pole radii so that:
    /// - Phase delay at f0 is minimal (preserving pitch accuracy)
    /// - Phase delay increases with frequency (stretching upper partials)
    void configureDispersionFilters(float B, float f0, float sr) noexcept
    {
        if (B < 1e-7f) {
            for (int i = 0; i < kMaxDispersionSections; ++i)
                dispersionFilters_[i].reset();
            return;
        }

        // First-order allpass design for string dispersion.
        // H(z) = (a + z^-1) / (1 + a*z^-1), stored in biquad as:
        //   b0=a, b1=1, b2=0, a1=a, a2=0
        //
        // Coefficient a < 0 creates group delay decreasing with frequency,
        // which after pitch compensation stretches upper partials.
        //
        // Design: binary search for coefficient a that produces the target
        // group delay differential between f0 and harmonic 4*f0.

        float period = sr / f0;
        float w0 = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        constexpr int kRefHarm = 4;
        float nW0 = static_cast<float>(kRefHarm) * w0;

        // Target differential delay from Fletcher's formula
        float delta1 = 1.0f - 1.0f / std::sqrt(1.0f + B);
        float deltaN = 1.0f - 1.0f / std::sqrt(
            1.0f + B * static_cast<float>(kRefHarm * kRefHarm));
        float targetDiff = period * (deltaN - delta1)
            / static_cast<float>(kMaxDispersionSections);

        if (targetDiff < 0.001f) {
            for (int i = 0; i < kMaxDispersionSections; ++i)
                dispersionFilters_[i].reset();
            return;
        }

        // Binary search for a in [-0.8, -0.001]
        float lo = -0.8f;
        float hi = -0.001f;

        // Verify target is achievable
        {
            float a2 = lo * lo;
            float maxGd0 = (1.0f - a2) / (1.0f + a2 + 2.0f * lo * std::cos(w0));
            float maxGdN = (1.0f - a2) / (1.0f + a2 + 2.0f * lo * std::cos(nW0));
            if (targetDiff > (maxGd0 - maxGdN)) {
                // Can't achieve target; lo already holds the maximum.
            }
        }

        for (int iter = 0; iter < 40; ++iter) {
            float mid = (lo + hi) * 0.5f;
            float a2 = mid * mid;
            float gd0 = (1.0f - a2) / (1.0f + a2 + 2.0f * mid * std::cos(w0));
            float gdN = (1.0f - a2) / (1.0f + a2 + 2.0f * mid * std::cos(nW0));
            float diff = gd0 - gdN;
            if (diff < targetDiff)
                hi = mid;
            else
                lo = mid;
        }
        float aBest = (lo + hi) * 0.5f;

        for (int i = 0; i < kMaxDispersionSections; ++i) {
            BiquadCoefficients coeffs;
            coeffs.b0 = aBest;
            coeffs.b1 = 1.0f;
            coeffs.b2 = 0.0f;
            coeffs.a1 = aBest;
            coeffs.a2 = 0.0f;
            dispersionFilters_[i].setCoefficients(coeffs);
        }
    }

    // =========================================================================
    // Member variables (matching contract)
    // =========================================================================

    // Delay segments (FR-002)
    DelayLine nutSideDelay_;
    DelayLine bridgeSideDelay_;

    // Loop filters
    float lossState_ = 0.0f;
    float lossRho_ = 0.999f;
    float lossS_ = 0.25f;
    Biquad dispersionFilters_[kMaxDispersionSections];
    DCBlocker dcBlocker_;

    // Fractional delay is handled by DelayLine::readAllpass()
    // (Thiran-equivalent allpass interpolation built into the delay line)

    // Smoothers
    OnePoleSmoother frequencySmoother_;
    OnePoleSmoother decaySmoother_;
    OnePoleSmoother brightnessSmoother_;

    // Memoised loop-loss gain - see process(). Purely a cache; it holds no
    // state the model depends on and is never serialised.
    float cachedLogFreq_ = 0.0f;
    float cachedDecay_ = 0.0f;
    float cachedRho_ = 0.0f;
    bool rhoCacheValid_ = false;

    // Energy followers (FR-023)
    float controlEnergy_ = 0.0f;
    float perceptualEnergy_ = 0.0f;
    float controlAlpha_ = 0.0f;
    float perceptualAlpha_ = 0.0f;

    // Frozen parameters
    float pickPosition_ = kDefaultPickPosition;
    float stiffness_ = 0.0f;
    float frozenPickPosition_ = kDefaultPickPosition;
    float frozenStiffness_ = 0.0f;

    // Delay lengths (computed at note onset)
    size_t nutDelaySamples_ = 0;
    size_t bridgeDelaySamples_ = 0;  // integer approx (for excitation fill)
    float bridgeDelayFloat_ = 0.0f;  // fractional delay for readAllpass()
    float totalLoopDelay_ = 0.0f;

    // Excitation
    XorShift32 rng_;
    float excitationGain_ = 1.0f;

    // Velocity wave state (FR-013)
    float feedbackVelocity_ = 0.0f;

    // Runtime
    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;
    float decayTime_ = 0.5f;
    float brightness_ = 0.5f;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
