#pragma once

// ==============================================================================
// ModalResonatorBank - Bank of damped coupled-form resonators
// ==============================================================================
// Layer 2 Processor | Namespace: Krate::DSP
//
// Up to 96 parallel modes using the Gordon-Smith coupled-form topology.
// SoA memory layout with 32-byte alignment for SIMD processing.
// Chaigne-Lambourg frequency-dependent damping model.
// ==============================================================================

#include <krate/dsp/core/dsp_utils.h>
#include <krate/dsp/processors/iresonator.h>
#include <krate/dsp/processors/modal_resonator_bank_simd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Krate {
namespace DSP {

/// Internal bandpass filter for bowed-mode velocity taps (FR-020).
/// Standard biquad BPF: b1 is always 0 for BPF, so only b0, b2 are stored.
struct BowedModeBPF {
    float b0{0.0f};  ///< Feedforward coefficient (= alpha)
    float b2{0.0f};  ///< Feedforward coefficient (= -alpha)
    float a1{0.0f};  ///< Feedback coefficient
    float a2{0.0f};  ///< Feedback coefficient
    float z1{0.0f};  ///< State variable
    float z2{0.0f};  ///< State variable

    /// Compute BPF coefficients for the given center frequency and Q.
    void setCoefficients(float freq, float q, double sampleRate) noexcept
    {
        const float w0 = 2.0f * std::numbers::pi_v<float> * freq
                          / static_cast<float>(sampleRate);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = (-2.0f * std::cos(w0)) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    /// Process one sample through the bandpass filter (Direct Form II Transposed).
    [[nodiscard]] float process(float input) noexcept
    {
        float output = b0 * input + z1;
        z1 = /* b1 * input = 0 */ -a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

    /// Reset filter state to zero.
    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }
};

// C4324: structure padded due to alignas -- intentional for SIMD arrays
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
class ModalResonatorBank : public IResonator {
public:
    static constexpr int kMaxModes = 96;
    static constexpr int kNumBowedModes = 8;

    /// Mode-count granularity handed to the Highway kernel.
    ///
    /// The kernel's SCALAR TAIL (`modal_resonator_bank_simd.cpp`, the
    /// `for (; i < count; ++i)` loop) is pathologically expensive relative to
    /// the vector body: measured on this repo's MSVC/AVX2 Release build,
    /// 512 samples of an 8-mode bank cost 4,328 ns/block while a 9-mode bank
    /// — one single scalar iteration more — cost 50,631 ns/block, and every
    /// count that is a multiple of the lane width came in at 4,300-6,700 ns
    /// while every count that is not came in at 41,000-58,000 ns. Padding the
    /// count up to a whole number of vectors removes the tail entirely and
    /// restores the vector-body figure (11 modes: 51,520 -> 5,582 ns/block,
    /// with an unchanged output sum).
    ///
    /// 16 is the float lane count of AVX-512, so it is a multiple of every
    /// x86 and NEON lane count Highway can dispatch to here; on a wider
    /// scalable vector (SVE/RVV) the tail simply survives, which is the
    /// pre-existing behaviour. `kMaxModes % kSimdModeGranularity == 0`, so the
    /// padded count is always in bounds.
    ///
    /// The padding lanes are NOT free-floating: `computeModeCoefficients`
    /// zeroes epsilon/radius/inputGain AND the oscillator state above
    /// `numModes_`, so each padded lane evaluates
    /// `s_new = 0*(0 + 0*c) + 0*excitation = 0` and contributes exactly 0.0f
    /// to the mode sum. Only the ORDER of the floating-point accumulation
    /// changes (the former scalar-tail additions now land inside the vector
    /// reduction), never the set of non-zero terms.
    static constexpr int kSimdModeGranularity = 16;
    static_assert(kMaxModes % kSimdModeGranularity == 0,
                  "the padded mode count must never index past the state arrays");

    /// Direct per-mode damping-law coefficients.
    /// R_k = exp(-(b1 + b3 * f_k^2) / sampleRate).
    /// Phase 8: exposed as first-class params so presets can dial in
    /// material character (wood vs metal vs glass vs plastic) independent
    /// of the decayTime/brightness convenience pair.
    struct DampingLaw {
        float b1{0.0f};  ///< Flat damping, s^-1 (legacy: 1 / decayTime)
        float b3{0.0f};  ///< Frequency-squared damping coefficient. Multiplies
                         ///< f^2 with f in **Hz** (not angular freq), so its
                         ///< units are s (decayRate_k = b1 + b3*f^2 is s^-1). A
                         ///< value expressed per rad^2 would be (2*pi)^2 ~= 39.5x
                         ///< too large -- see computeModeCoefficients() which
                         ///< never converts Hz->rad/s.
    };

    /// Legacy b3 ceiling. Matches the (1 - brightness) * kMaxB3 formulation
    /// retained for backward-compat callers.
    static constexpr float kLegacyMaxB3 = 4.0e-5f;

    /// Build a DampingLaw from the legacy (decayTime, brightness) pair.
    /// decayTime is the 1/e amplitude time constant for low-frequency modes
    /// (b1 = 1/decayTime), NOT the T60: T60 = ln(1000) * decayTime ~= 6.9x
    /// decayTime. (Historic comment claimed t60 -- every caller is tuned to
    /// the actual 1/e behaviour, so reconciling the semantics is a separate
    /// audited change; see 06-orchestralKit-fix-plan.md D9.) brightness
    /// rebalances the b3 term so brightness=1.0 removes frequency-squared
    /// damping.
    [[nodiscard]] static DampingLaw dampingLawFromLegacy(float decayTime,
                                                         float brightness) noexcept
    {
        decayTime = std::clamp(decayTime, 0.01f, 5.0f);
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        return DampingLaw{1.0f / decayTime, (1.0f - brightness) * kLegacyMaxB3};
    }

    ModalResonatorBank() noexcept = default;
    ~ModalResonatorBank() override = default;

    // -------------------------------------------------------------------------
    // Output-stage configuration (Phase 11 / spec 145)
    //
    // The bank has historically applied a per-sample soft-clip at +/-0.707
    // (`kSoftClipThreshold`) to its summed mode output, plus no
    // mode-count normalisation. This is BIBO-safe but produces "gain-staging
    // masquerading as safety" -- on percussive transients the modal sum sits
    // pinned at the clipper ceiling for the entire ring-out, which silently
    // masks per-mode damping modulation (Material, decay, b1/b3, etc.).
    //
    // The defaults are kept at LEGACY to preserve bit-identity for existing
    // consumers (Innexus' physical_model_mixer expects clipped input). New
    // consumers (Membrum's percussion voices) opt into the transparent
    // path via these setters. Field-standard analogues:
    //   - Faust physmodels.lib `:> /(nModes)` summation normalisation
    //   - STK Modal -- no output clipping
    //   - Bilbao NSS -- stability via energy methods, not output saturation
    // See plugins/membrum spec 145 research notes for full citations.
    // -------------------------------------------------------------------------

    /// Linear gain applied to the summed bank output (before the soft-clip).
    /// Default 1.0 preserves the historical scale. Callers that want the
    /// Faust-style "sum-of-N-modes" headroom apply `1/N` or `1/sqrt(N)`
    /// here; callers with body-specific amplitude profiles (Bessel Membrane,
    /// Bell, etc.) can compute and pass `1 / sum(amplitudes)` for an
    /// amplitude-aware peak ceiling.
    void setOutputGain(float gain) noexcept { outputGain_ = gain; }

    /// Configure the soft-clip threshold applied to the summed bank output.
    /// `threshold > 0` engages the tanh-shaped clipper at that level
    /// (`softClip(x / threshold) * threshold`). `threshold <= 0` disables
    /// the clipper entirely. Default is `kSoftClipThreshold = 0.707f`
    /// (-3 dBFS) -- preserves Innexus' physical_model_mixer bit-identity.
    /// Membrum's non-feedback voices raise this to 1.0 so the clipper only
    /// engages at near-clip levels rather than masking transient damping
    /// modulation throughout the entire ring-out.
    void setOutputSoftClipThreshold(float threshold) noexcept
    {
        outputSoftClipThreshold_ = threshold;
    }

    [[nodiscard]] float getOutputGain() const noexcept { return outputGain_; }
    [[nodiscard]] float getOutputSoftClipThreshold() const noexcept
    {
        return outputSoftClipThreshold_;
    }

    /// Sum of the per-mode input gains stored at the last setModes/updateModes
    /// call. With initially-in-phase impulse excitation the bank's transient
    /// peak amplitude is upper-bounded by this sum, so `1 / inputGainSum()`
    /// is the canonical "unit-peak" output gain that keeps the bank in its
    /// linear region for any body's amplitude profile (Bessel Membrane,
    /// Bell, Plate, etc.). Returns 1.0 if the bank has not yet been
    /// configured (avoids divide-by-zero in callers).
    [[nodiscard]] float getInputGainSum() const noexcept
    {
        return (inputGainSum_ > 1e-6f) ? inputGainSum_ : 1.0f;
    }

    // Non-copyable (large aligned arrays), movable
    ModalResonatorBank(const ModalResonatorBank&) = delete;
    ModalResonatorBank& operator=(const ModalResonatorBank&) = delete;
    ModalResonatorBank(ModalResonatorBank&&) noexcept = default;
    ModalResonatorBank& operator=(ModalResonatorBank&&) noexcept = default;

    /// Prepare for processing at the given sample rate.
    void prepare(double sampleRate) noexcept override
    {
        sampleRate_ = static_cast<float>(sampleRate);
        smoothCoeff_ = std::exp(-1.0f / (kSmoothingTimeMs * 0.001f * sampleRate_));
        envelopeAttackCoeff_ = std::exp(-1.0f / (kEnvelopeAttackMs * 0.001f * sampleRate_));
        // Energy follower EMA coefficients (FR-023)
        controlAlpha_ = std::exp(-1.0f / (kControlEnergyTauMs * 0.001f * sampleRate_));
        perceptualAlpha_ = std::exp(-1.0f / (kPerceptualEnergyTauMs * 0.001f * sampleRate_));
        prepared_ = true;
    }

    /// Reset all filter states and envelope state to zero.
    void reset() noexcept
    {
        std::memset(sinState_, 0, sizeof(sinState_));
        std::memset(cosState_, 0, sizeof(cosState_));
        envelopeState_ = 0.0f;
        previousEnvelope_ = 0.0f;
        // Snap smoothed arrays to targets
        std::memcpy(epsilon_, epsilonTarget_, sizeof(epsilon_));
        std::memcpy(radius_, radiusTarget_, sizeof(radius_));
        std::memcpy(inputGain_, inputGainTarget_, sizeof(inputGain_));
        // Reset bowed-mode filters
        for (auto& f : bowedModeFilters_)
            f.reset();
        bowedModeSumVelocity_ = 0.0f;
    }

    /// Configure all modes from analyzed harmonic data.
    /// Clears filter states (for note-on).
    void setModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept
    {
        setModes(frequencies, amplitudes, numPartials,
                 dampingLawFromLegacy(decayTime, brightness),
                 stretch, scatter);
    }

    /// Configure all modes with an explicit per-mode damping law.
    /// Phase 8 (#3): lets presets dial in b1/b3 directly instead of
    /// routing through decayTime/brightness.
    void setModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        DampingLaw damping,
        float stretch,
        float scatter
    ) noexcept
    {
        // Clear filter states on note-on (FR-018)
        std::memset(sinState_, 0, sizeof(sinState_));
        std::memset(cosState_, 0, sizeof(cosState_));
        envelopeState_ = 0.0f;
        previousEnvelope_ = 0.0f;
        computeModeCoefficients(frequencies, amplitudes, numPartials,
                                damping.b1, damping.b3, stretch, scatter, true);
    }

    /// Update mode coefficients without clearing filter states.
    /// Used during frame transitions (FR-019).
    void updateModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept
    {
        updateModes(frequencies, amplitudes, numPartials,
                    dampingLawFromLegacy(decayTime, brightness),
                    stretch, scatter);
    }

    /// Update mode coefficients with explicit damping law (state-preserving).
    void updateModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        DampingLaw damping,
        float stretch,
        float scatter
    ) noexcept
    {
        computeModeCoefficients(frequencies, amplitudes, numPartials,
                                damping.b1, damping.b3, stretch, scatter, false);
    }

    /// Snap the smoothed coefficient arrays (epsilon/radius/inputGain) onto
    /// their targets WITHOUT touching the resonator states.
    ///
    /// This is the state-preserving half of `reset()` (`:199-201` snaps the same
    /// three arrays but also zeroes `sinState_`/`cosState_`), and the same snap
    /// `setModes` performs internally (`computeModeCoefficients(..., true)`).
    ///
    /// Why it exists: `smoothCoefficients()` runs once per `processBlock` call
    /// (`:357`), NOT once per sample, so the smoothing trajectory is a function
    /// of how the caller partitions a buffer -- 16 calls for one 1024-sample
    /// block, 27 for the same 1024 samples delivered as 100+...+24. A caller
    /// that already retunes on its own fixed control grid (Seraphis'
    /// `ContinuousBody`, 64-sample chunks) can call `updateModes()` and then
    /// this, which leaves `target - current == 0` and makes the per-block
    /// smoothing an exact no-op, so the render no longer depends on the host's
    /// block size. Such a caller supplies its own continuity by retuning on a
    /// fine grid behind a dirty gate.
    ///
    /// Strictly additive: no existing call path invokes it, so every current
    /// consumer is bit-identical.
    /// Read-only views of the freshly-computed coefficient TARGETS.
    ///
    /// `setModes`/`updateModes` leave these holding, per mode, exactly the
    /// `2 sin(pi f_w / fs)`, `exp(-(b1 + b3 f_w^2)/fs)` and amplitude the bank
    /// will run - after the stretch and scatter warps, after the amplitude cull
    /// and after the Nyquist cull (a culled mode reads back as three zeros).
    /// A caller that needs to reason about the bank's steady-state gain can
    /// therefore read them instead of re-deriving `f_w` and its transcendentals
    /// from the frequency table, which is both cheaper and guaranteed to
    /// describe the same mode set. Valid until the next `setModes`,
    /// `updateModes` or `updateDampingLaw`. Length is `getNumModes()`.
    [[nodiscard]] const float* getEpsilonTargets() const noexcept { return epsilonTarget_; }
    [[nodiscard]] const float* getRadiusTargets() const noexcept { return radiusTarget_; }
    [[nodiscard]] const float* getInputGainTargets() const noexcept { return inputGainTarget_; }

    void snapCoefficients() noexcept
    {
        // Only the lanes the kernel actually advances. Copying all kMaxModes
        // (96) would be three times as much memory traffic as the mode set
        // needs, on a path a control-grid caller runs every 64 samples.
        const auto bytes = static_cast<std::size_t>(numSimdModes_) * sizeof(float);
        std::memcpy(epsilon_, epsilonTarget_, bytes);
        std::memcpy(radius_, radiusTarget_, bytes);
        std::memcpy(inputGain_, inputGainTarget_, bytes);
    }

    /// Re-compute per-mode radii with a new damping law, preserving
    /// currently-configured frequencies, amplitudes, and stretch/scatter.
    /// Phase 8 (#3 + preparation for #7): cheap enough to call block-rate.
    void updateDampingLaw(DampingLaw damping) noexcept
    {
        storedDampingLaw_ = damping;
        const float b1 = damping.b1;
        const float b3 = damping.b3;
        for (int k = 0; k < numModes_; ++k) {
            if (!active_[k]) continue;
            float eps = epsilonTarget_[k];
            if (eps <= 0.0f) continue;
            float f_w = std::asin(eps * 0.5f) * sampleRate_
                        / std::numbers::pi_v<float>;
            float decayRate_k = b1 + b3 * f_w * f_w;
            radiusTarget_[k] = std::exp(-decayRate_k / sampleRate_);
        }
    }

    /// Process a single sample through the resonator bank.
    /// Includes per-sample coefficient smoothing (for single-sample callers).
    [[nodiscard]] float processSample(float excitation) noexcept
    {
        return processSample(excitation, 1.0f);
    }

    /// Block-rate coefficient smoothing -- call once at block start when the
    /// caller is about to drive `processSampleNoSmooth` per sample. Pairs
    /// with the slow-path FeedbackExciter route in DrumVoice that needs
    /// strict per-sample body feedback but cannot use the SIMD `processBlock`
    /// path. Hoists the smoothCoefficients() work out of the per-sample inner
    /// loop (Phase 9 perf fix; without this the FeedbackExciter combos
    /// blew the per-voice CPU budget after the Phase 8B mode-count bump).
    void prepareBlockSmoothing() noexcept
    {
        smoothCoefficients();
    }

    /// Per-sample SIMD-accelerated process with coefficient smoothing already
    /// done block-rate by `prepareBlockSmoothing()`. Falls back to the scalar
    /// path when bowed-mode taps are active (the per-mode bow-weight injection
    /// can't be folded into the SIMD kernel's broadcast `excitation`) or when
    /// `decayScale != 1.0` (the per-mode std::pow on the radius isn't in the
    /// kernel either). Both fallbacks are cold paths -- bow mode is rarely
    /// engaged for the kit voice and decayScale only diverges during mallet
    /// choke.
    [[nodiscard]] float processSampleNoSmooth(float excitation,
                                              float decayScale = 1.0f) noexcept
    {
        if (bowModeActive_ || decayScale != 1.0f) {
            return processSampleCore(excitation, decayScale);
        }

        const float ex = applyTransientEmphasis(excitation);
        float modeSum = processModalBankSampleSIMD(
            sinState_, cosState_, epsilon_, radius_, inputGain_,
            ex, numSimdModes_);

        return applyOutputStage(modeSum);
    }

    /// Process a single sample with optional decay scaling (mallet choke).
    /// @param excitation Input excitation signal
    /// @param decayScale Decay acceleration factor:
    ///   - 1.0f = normal operation (no choke)
    ///   - >1.0f = accelerated decay (choke), applied as R_eff = pow(R, decayScale)
    ///   - Preserves relative damping between modes (material character retained)
    /// @note The pow() per mode is only computed when decayScale != 1.0f
    [[nodiscard]] float processSample(float excitation, float decayScale) noexcept
    {
        smoothCoefficients();
        return processSampleCore(excitation, decayScale);
    }

    /// Process a block of samples.
    /// Smooths coefficients once at block start (block-rate smoothing is
    /// sufficient for coefficient updates and saves ~9 FLOPs/mode/sample).
    /// Uses SIMD-accelerated mode loop via Highway dynamic dispatch.
    void processBlock(const float* input, float* output, int numSamples) noexcept
    {
        smoothCoefficients();
        for (int i = 0; i < numSamples; ++i) {
            float ex = applyTransientEmphasis(input[i]);

            // SIMD-accelerated mode loop (processes all modes for one sample).
            // `numSimdModes_`, not `numModes_`: see the kSimdModeGranularity
            // banner - the kernel's scalar tail costs ~9x the vector body.
            float modeSum = processModalBankSampleSIMD(
                sinState_, cosState_, epsilon_, radius_, inputGain_,
                ex, numSimdModes_);

            output[i] = applyOutputStage(modeSum);
        }
        flushSilentModes();
    }

    /// Process a block of samples with decay scaling (mallet choke).
    void processBlock(const float* input, float* output, int numSamples,
                      float decayScale) noexcept
    {
        smoothCoefficients();
        for (int i = 0; i < numSamples; ++i) {
            output[i] = processSampleCore(input[i], decayScale);
        }
        flushSilentModes();
    }

    /// Check mode energy and zero out states below silence threshold (FR-027).
    void flushSilentModes() noexcept
    {
        for (int k = 0; k < numModes_; ++k) {
            if (!active_[k])
                continue;
            float energy = sinState_[k] * sinState_[k] + cosState_[k] * cosState_[k];
            if (energy < kSilenceThreshold) {
                sinState_[k] = 0.0f;
                cosState_[k] = 0.0f;
                active_[k] = false;
                --numActiveModes_;
            }
        }
    }

    /// @return Number of active (non-culled) modes
    [[nodiscard]] int getNumActiveModes() const noexcept
    {
        return numActiveModes_;
    }

    /// Total instantaneous modal (vibrational) energy: the sum over modes of
    /// the coupled-form state magnitude `sin^2 + cos^2`. For a Gordon-Smith
    /// phasor `sin^2 + cos^2` is the mode's amplitude^2 (a smooth envelope, not
    /// an oscillating quantity), so this is the bank's stored vibrational
    /// energy. It is INVARIANT to `outputGain_`/the soft-clip stage -- it
    /// reflects only the internal physical state -- which makes it the correct
    /// driver for tension modulation: the quasistatic tension variation that
    /// produces percussion pitch glide is linearly proportional to the system
    /// energy (Tolonen-Valimaki-Karjalainen 2000; Avanzini-Marogna-Bank 2012,
    /// "quasistatic tension is linear in membrane energy"). Reading the radiated
    /// output instead couples the glide to the (arbitrary) output gain staging.
    [[nodiscard]] float getModalEnergy() const noexcept
    {
        float energy = 0.0f;
        for (int k = 0; k < numModes_; ++k)
            energy += sinState_[k] * sinState_[k] + cosState_[k] * cosState_[k];
        return energy;
    }

    /// Scale all mode radii toward a faster decay. Mirrors STK's Modal::damp()
    /// note-off idiom: multiply every active mode's radius (both smoothed and
    /// target) by `scale`, so the voice finishes through its own damping law
    /// instead of through an externally-applied amp-envelope release.
    /// @param scale in (0, 1]. 1.0 is a no-op; 0.97 shortens the effective
    /// T60 by a factor of ~30x for a nominal 500-sample release.
    void damp(float scale) noexcept
    {
        scale = std::clamp(scale, 0.0f, 1.0f);
        for (int k = 0; k < numModes_; ++k) {
            if (!active_[k])
                continue;
            radiusTarget_[k] *= scale;
            radius_[k] *= scale;
        }
    }

    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept
    {
        return prepared_;
    }

    /// Get the frequency of mode k (Hz). Returns 0 if k is out of range.
    /// Recovers Hz from stored epsilon: f = asin(eps * 0.5) * sr / pi
    [[nodiscard]] float getModeFrequency(int k) const noexcept
    {
        if (k < 0 || k >= numModes_) return 0.0f;
        float eps = epsilonTarget_[k];
        if (eps <= 0.0f) return 0.0f;
        return std::asin(eps * 0.5f) * sampleRate_
               / std::numbers::pi_v<float>;
    }

    /// Get the number of configured modes.
    [[nodiscard]] int getNumModes() const noexcept { return numModes_; }

    /// Get the last applied damping law (for diagnostics / tests).
    [[nodiscard]] DampingLaw getDampingLaw() const noexcept
    {
        return storedDampingLaw_;
    }

    // =========================================================================
    // IResonator interface adapter methods (FR-020, FR-023, FR-024, FR-025)
    // =========================================================================

    /// Store frequency for use by the voice engine via IResonator.
    /// ModalResonatorBank receives frequencies via setModes()/updateModes(),
    /// so this stores the value for future setModes calls from the voice engine.
    void setFrequency(float f0) noexcept override
    {
        storedFrequency_ = f0;
    }

    /// Store decay time for use via IResonator interface.
    void setDecay(float t60) noexcept override
    {
        storedDecayTime_ = t60;
    }

    /// Store brightness for use via IResonator interface.
    void setBrightness(float brightness) noexcept override
    {
        storedBrightness_ = brightness;
    }

    /// Process one sample through the resonator bank (IResonator interface).
    /// Delegates to processSample() and updates energy followers.
    [[nodiscard]] float process(float excitation) noexcept override
    {
        float output = processSample(excitation);
        // Update dual energy followers (FR-023, FR-024)
        float squaredOutput = output * output;
        controlEnergy_ = controlAlpha_ * controlEnergy_
                       + (1.0f - controlAlpha_) * squaredOutput;
        perceptualEnergy_ = perceptualAlpha_ * perceptualEnergy_
                          + (1.0f - perceptualAlpha_) * squaredOutput;
        return output;
    }

    /// Get fast energy follower (tau ~5ms) (FR-023).
    [[nodiscard]] float getControlEnergy() const noexcept override
    {
        return controlEnergy_;
    }

    /// Get slow energy follower (tau ~30ms) (FR-023).
    [[nodiscard]] float getPerceptualEnergy() const noexcept override
    {
        return perceptualEnergy_;
    }

    /// Clear all internal state including energy followers (FR-025).
    void silence() noexcept override
    {
        reset();
        controlEnergy_ = 0.0f;
        perceptualEnergy_ = 0.0f;
        // bowedModeSumVelocity_ already cleared by reset()
    }

    /// Return bowed-mode summed velocity when active, else 0.0f.
    [[nodiscard]] float getFeedbackVelocity() const noexcept override
    {
        if (bowModeActive_)
            return bowedModeSumVelocity_;
        return 0.0f;
    }

    // =========================================================================
    // Bowed-mode coupling (FR-020, FR-024)
    // =========================================================================

    /// Enable/disable bowed-mode velocity taps.
    /// When active, getFeedbackVelocity() returns summed bandpass outputs.
    void setBowModeActive(bool active) noexcept
    {
        bool wasActive = bowModeActive_;
        bowModeActive_ = active;
        if (active && !wasActive) {
            // Initialize BPF coefficients from current mode frequencies
            initBowedModeFilters();
            recomputeBowWeights();
        }
        if (!active)
            bowedModeSumVelocity_ = 0.0f;
    }

    /// Set bow position for harmonic weighting of excitation input.
    /// Weight per mode k: sin((k+1) * pi * bowPosition)
    void setBowPosition(float position) noexcept
    {
        bowPosition_ = std::clamp(position, 0.0f, 1.0f);
        if (bowModeActive_)
            recomputeBowWeights();
    }

private:
    // Voicing constants (may be promoted to parameters in future phases)
    // kTransientEmphasisGain may be promoted to a user-facing parameter in a future phase
    static constexpr float kTransientEmphasisGain = 4.0f;
    static constexpr float kMaxB3 = 4.0e-5f;
    static constexpr float kSilenceThreshold = 1e-12f;
    static constexpr float kNyquistGuard = 0.49f;
    static constexpr float kAmplitudeThresholdLinear = 0.0001f; // -80 dB
    static constexpr float kSmoothingTimeMs = 2.0f;
    static constexpr float kEnvelopeAttackMs = 5.0f;
    static constexpr float kSoftClipThreshold = 0.707f; // -3 dBFS
    static constexpr float kControlEnergyTauMs = 5.0f;     // FR-023: fast energy follower
    static constexpr float kPerceptualEnergyTauMs = 30.0f;  // FR-023: slow energy follower
    static constexpr float kBowInjectionGain = 0.01f;      // FR-020: bow force injection scaling

    // Golden-ratio-derived scatter displacement constant
    static constexpr float kScatterD =
        std::numbers::pi_v<float> * (std::numbers::phi_v<float> - 1.0f);

    // SoA state arrays (32-byte aligned for SIMD)
    alignas(32) float sinState_[kMaxModes]{};
    alignas(32) float cosState_[kMaxModes]{};
    alignas(32) float epsilon_[kMaxModes]{};
    alignas(32) float epsilonTarget_[kMaxModes]{};
    alignas(32) float radius_[kMaxModes]{};
    alignas(32) float radiusTarget_[kMaxModes]{};
    alignas(32) float inputGain_[kMaxModes]{};
    alignas(32) float inputGainTarget_[kMaxModes]{};
    bool active_[kMaxModes]{};

    int numActiveModes_ = 0;
    int numModes_ = 0;
    /// `numModes_` rounded up to `kSimdModeGranularity` (see its banner). Only
    /// the SIMD kernel sees this; every scalar loop still stops at `numModes_`.
    int numSimdModes_ = 0;
    float sampleRate_ = 44100.0f;
    float smoothCoeff_ = 0.0f;
    float envelopeState_ = 0.0f;
    float previousEnvelope_ = 0.0f;
    float envelopeAttackCoeff_ = 0.0f;
    bool prepared_ = false;

    // Output-stage configuration (Phase 11 / spec 145). Defaults preserve the
    // historical "softClip-only at 0.707, no extra gain" behaviour so
    // existing consumers (Innexus physical_model_mixer) are bit-identical.
    // Membrum opts into the transparent path via the setters above.
    float outputGain_               = 1.0f;
    float outputSoftClipThreshold_  = kSoftClipThreshold;  // 0.707 default
    float inputGainSum_             = 0.0f;  // updated by computeModeCoefficients

    // IResonator energy followers (FR-023)
    float controlEnergy_ = 0.0f;
    float perceptualEnergy_ = 0.0f;
    float controlAlpha_ = 0.0f;
    float perceptualAlpha_ = 0.0f;

    // IResonator stored parameters (adapter state)
    float storedFrequency_ = 440.0f;
    float storedDecayTime_ = 0.5f;
    float storedBrightness_ = 0.5f;
    DampingLaw storedDampingLaw_{2.0f, 2.0e-5f};

    // Bowed-mode coupling state (FR-020)
    std::array<BowedModeBPF, kNumBowedModes> bowedModeFilters_{};
    float bowWeights_[kMaxModes]{};  ///< Harmonic weights for bow excitation
    float bowedModeSumVelocity_{0.0f};
    float bowPosition_{0.13f};
    bool bowModeActive_{false};

    /// Recompute harmonic weights for bowed-mode excitation (FR-024).
    /// Weight per mode k: sin((k+1) * pi * bowPosition_)
    void recomputeBowWeights() noexcept
    {
        for (int k = 0; k < numModes_; ++k) {
            float weight = std::sin(static_cast<float>(k + 1)
                                    * std::numbers::pi_v<float> * bowPosition_);
            // Apply bow weight on top of existing gain
            // Note: inputGainTarget_ already has the base amplitude-derived gain.
            // The bow weight is applied multiplicatively in processSampleCore
            // when bowModeActive_ is true, via the bowWeights_ array.
            bowWeights_[k] = weight;
        }
    }

    /// Initialize bowed-mode BPF coefficients from current mode frequencies.
    void initBowedModeFilters() noexcept
    {
        // Configure the first kNumBowedModes filters from stored mode data.
        // Mode frequencies are derived from epsilonTarget_:
        //   epsilon = 2 * sin(pi * f / sampleRate)
        //   f = asin(epsilon / 2) * sampleRate / pi
        int numBowed = std::min(numModes_, kNumBowedModes);
        for (int k = 0; k < numBowed; ++k) {
            float eps = epsilonTarget_[k];
            if (eps <= 0.0f) continue;
            float freq = std::asin(eps * 0.5f) * sampleRate_
                         / std::numbers::pi_v<float>;
            if (freq > 0.0f && freq < sampleRate_ * 0.49f)
                bowedModeFilters_[static_cast<size_t>(k)].setCoefficients(
                    freq, 50.0f, static_cast<double>(sampleRate_));
        }
    }

    /// Compute mode coefficients from partial data.
    void computeModeCoefficients(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float b1In,
        float b3In,
        float stretch,
        float scatter,
        bool snapSmoothing
    ) noexcept
    {
        // Clamp parameters to valid ranges
        numPartials = std::clamp(numPartials, 0, kMaxModes);
        stretch = std::clamp(stretch, 0.0f, 1.0f);
        scatter = std::clamp(scatter, 0.0f, 1.0f);

        numModes_ = numPartials;
        numActiveModes_ = 0;
        inputGainSum_ = 0.0f;

        // Chaigne-Lambourg damping coefficients (FR-006).
        // b1 floor mirrors the legacy decayTime clamp of 5.0 s -> 0.2 s^-1;
        // allow slightly below that since b3 now contributes separately.
        const float b1 = std::max(b1In, 1.0f / 5.0f);
        const float b3 = std::max(b3In, 0.0f);
        storedDampingLaw_ = DampingLaw{b1, b3};

        // Inharmonicity parameters. `scatter` is bank-wide across every
        // consumer; Phase 8C widens the per-mode dither ceiling from 2 %
        // to 10 % so Membrum's mode-scatter knob produces a musically
        // useful sweep. Callers that never populate scatter (Innexus)
        // are unaffected because scatter stays at 0.
        //
        // `B` is the stiff-string inharmonicity coefficient in
        // f_n = n*f0 * sqrt(1 + B*n^2) (Fletcher & Rossing; Young 1952). The
        // ceiling was 0.001 — orders of magnitude below real piano/bar values
        // (~5e-5 bass to >1e-2 treble) — so the Stretch knob barely moved the
        // partials. Widened to 0.01 so full Stretch reaches a clearly metallic,
        // inharmonic spectrum (audit §3-B). Callers with stretch==0 (Innexus
        // default, Membrum default-off) get B==0 and are bit-identical.
        const float B = stretch * stretch * 0.01f;
        const float C = scatter * 0.10f;

        for (int k = 0; k < numPartials; ++k) {
            float f_k = frequencies[k];
            float amp = amplitudes[k];

            // Amplitude culling (FR-016)
            if (amp < kAmplitudeThresholdLinear) {
                active_[k] = false;
                epsilonTarget_[k] = 0.0f;
                radiusTarget_[k] = 0.0f;
                inputGainTarget_[k] = 0.0f;
                continue;
            }

            // Apply Stretch warping (FR-011): stiff-string inharmonicity
            // f_n = n*f0 * sqrt(1 + B*n^2). Partial number n is 1-based, so the
            // array index k maps to n = k+1: the fundamental (k=0) gets
            // sqrt(1+B) and the dispersion grows with the true partial number.
            // (Previously used k directly, which left the fundamental unstretched
            // and gave every partial the dispersion of the partial below it —
            // audit §3-B. The bow-weight code below already uses k+1.)
            const float n_k = static_cast<float>(k + 1);
            float f_w = f_k * std::sqrt(1.0f + B * n_k * n_k);

            // Apply Scatter warping (FR-012): deterministic sinusoidal displacement
            f_w *= (1.0f + C * std::sin(static_cast<float>(k) * kScatterD));

            // Nyquist culling (FR-015)
            if (f_w >= kNyquistGuard * sampleRate_) {
                active_[k] = false;
                epsilonTarget_[k] = 0.0f;
                radiusTarget_[k] = 0.0f;
                inputGainTarget_[k] = 0.0f;
                continue;
            }

            // Frequency-dependent damping (FR-006, FR-014). f_w is in Hz, so b3
            // is in s (Hz convention), not the s/rad^2 of the angular form.
            float decayRate_k = b1 + b3 * f_w * f_w;
            float R_k = std::exp(-decayRate_k / sampleRate_);

            // Coupled-form frequency coefficient
            float eps_k = 2.0f * std::sin(std::numbers::pi_v<float> * f_w / sampleRate_);

            // Input gain: amplitude-weighted excitation (FR-009)
            // Note: the coupled-form oscillator's amplitude is naturally bounded
            // by R_k (decay radius), so no (1-R_k) normalization is needed.
            // That normalization was appropriate for leaky integrators with
            // continuous driving but kills transient response (~-84 dB at
            // typical decay settings), making the physical model inaudible.
            float gain_k = amp;

            epsilonTarget_[k] = eps_k;
            radiusTarget_[k] = R_k;
            inputGainTarget_[k] = gain_k;
            active_[k] = true;
            ++numActiveModes_;
            inputGainSum_ += std::abs(gain_k);
        }

        // Deactivate and zero coefficient TARGETS for modes beyond numPartials.
        for (int k = numPartials; k < kMaxModes; ++k) {
            active_[k] = false;
            epsilonTarget_[k] = 0.0f;
            radiusTarget_[k] = 0.0f;
            inputGainTarget_[k] = 0.0f;
        }

        // Round the count the SIMD kernel is handed up to a whole number of
        // vectors (see the `kSimdModeGranularity` banner). Safe for every ISA
        // whose lane count divides it, and kMaxModes is a multiple of it so the
        // rounded count can never index past the arrays.
        numSimdModes_ = std::min(
            ((numPartials + kSimdModeGranularity - 1) / kSimdModeGranularity)
                * kSimdModeGranularity,
            kMaxModes);

        // The PADDING lanes - the ones the SIMD kernel advances but the mode
        // set does not use - must be genuinely silent, in the CURRENT arrays
        // and in the oscillator state, not merely in the targets:
        // `smoothCoefficients()` only walks `k < numModes_`, so a mode set that
        // shrank would otherwise leave a stale non-zero radius up there and
        // ring a dead mode back into the sum. Everything at or above
        // `numSimdModes_` is never read by anything and is left alone.
        for (int k = numPartials; k < numSimdModes_; ++k) {
            epsilon_[k] = 0.0f;
            radius_[k] = 0.0f;
            inputGain_[k] = 0.0f;
            sinState_[k] = 0.0f;
            cosState_[k] = 0.0f;
        }

        if (snapSmoothing) {
            std::memcpy(epsilon_, epsilonTarget_, sizeof(epsilon_));
            std::memcpy(radius_, radiusTarget_, sizeof(radius_));
            std::memcpy(inputGain_, inputGainTarget_, sizeof(inputGain_));
        }

        // Reinitialize bowed-mode filters when modes change
        if (bowModeActive_) {
            initBowedModeFilters();
            recomputeBowWeights();
        }
    }

    /// Apply the configured output stage (linear gain + optional soft-clip)
    /// to a raw mode sum. Inlined hot-path: with default settings
    /// (gain=1.0, threshold=0.707) this reduces to the historical
    /// `softClip(x / 0.707) * 0.707` expression bit-identically.
    [[nodiscard]] float applyOutputStage(float modeSum) const noexcept
    {
        modeSum *= outputGain_;
        const float t = outputSoftClipThreshold_;
        if (t > 0.0f)
            modeSum = softClip(modeSum / t) * t;
        return modeSum;
    }

    /// Smooth coefficients toward targets (one-pole per coefficient).
    /// Called once per block (in processBlock) or once per sample (in processSample).
    /// Branchless: inactive modes have zero targets and converge to zero.
    void smoothCoefficients() noexcept
    {
        const float oneMinusCoeff = 1.0f - smoothCoeff_;
        for (int k = 0; k < numModes_; ++k) {
            epsilon_[k] += oneMinusCoeff * (epsilonTarget_[k] - epsilon_[k]);
            radius_[k] += oneMinusCoeff * (radiusTarget_[k] - radius_[k]);
            inputGain_[k] += oneMinusCoeff * (inputGainTarget_[k] - inputGain_[k]);
        }
    }

    /// Core per-sample resonator processing (no coefficient smoothing).
    /// Branchless inner loop: inactive modes have zero coefficients and
    /// contribute nothing to output.
    [[nodiscard]] float processSampleCore(float excitation, float decayScale = 1.0f) noexcept
    {
        float ex = applyTransientEmphasis(excitation);
        float output = 0.0f;

        if (decayScale != 1.0f) {
            for (int k = 0; k < numModes_; ++k) {
                float s = sinState_[k];
                float c = cosState_[k];
                float eps = epsilon_[k];
                float R = std::pow(radius_[k], decayScale);
                float gain = inputGain_[k];

                // FR-024: When bowed, inject force directly with harmonic weight
                // (bypass inputGain normalization designed for impulsive excitation)
                float injection = bowModeActive_
                    ? gain * ex + bowWeights_[k] * excitation * kBowInjectionGain
                    : gain * ex;

                float s_new = R * (s + eps * c) + injection;
                float c_new = R * (c - eps * s_new);

                sinState_[k] = s_new;
                cosState_[k] = c_new;
                output += s_new;
            }
        } else {
            for (int k = 0; k < numModes_; ++k) {
                float s = sinState_[k];
                float c = cosState_[k];
                float eps = epsilon_[k];
                float R = radius_[k];
                float gain = inputGain_[k];

                // FR-024: When bowed, inject force directly with harmonic weight
                float injection = bowModeActive_
                    ? gain * ex + bowWeights_[k] * excitation * kBowInjectionGain
                    : gain * ex;

                // Gordon-Smith coupled-form resonator (FR-003)
                float s_new = R * (s + eps * c) + injection;
                float c_new = R * (c - eps * s_new);

                sinState_[k] = s_new;
                cosState_[k] = c_new;
                output += s_new;
            }
        }

        output = applyOutputStage(output);

        // FR-020: Feed output through bowed-mode bandpass velocity taps
        if (bowModeActive_) {
            float sum = 0.0f;
            int numBowed = std::min(numModes_, kNumBowedModes);
            for (int k = 0; k < numBowed; ++k) {
                sum += bowedModeFilters_[static_cast<size_t>(k)].process(output);
            }
            bowedModeSumVelocity_ = sum;
        }

        return output;
    }

    /// Transient emphasis: continuous proportional boost (FR-022).
    float applyTransientEmphasis(float sample) noexcept
    {
        // One-pole envelope follower (~5ms attack)
        envelopeState_ = envelopeAttackCoeff_ * envelopeState_
                       + (1.0f - envelopeAttackCoeff_) * std::abs(sample);

        // Compute derivative
        float derivative = envelopeState_ - previousEnvelope_;

        // Continuous proportional boost (NOT binary on/off)
        // kTransientEmphasisGain may be promoted to a user-facing parameter in a future phase
        float emphasis = 1.0f + kTransientEmphasisGain * std::max(0.0f, derivative);

        previousEnvelope_ = envelopeState_;

        return sample * emphasis;
    }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace DSP
} // namespace Krate
