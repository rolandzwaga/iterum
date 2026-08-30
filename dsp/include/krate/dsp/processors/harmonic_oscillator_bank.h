// ==============================================================================
// Layer 2: DSP Processor - Harmonic Oscillator Bank
// ==============================================================================
// Synthesizes audio from HarmonicFrames using 96 Gordon-Smith Modified Coupled
// Form (MCF) oscillators in Structure-of-Arrays (SoA) layout.
//
// Features:
// - 96 MCF oscillators with per-partial frequency/amplitude control
// - SoA layout with 32-byte alignment for cache efficiency (FR-036)
// - Anti-aliasing via soft rolloff near Nyquist (FR-038)
// - Phase continuity on frequency changes (FR-039)
// - Crossfade on large pitch jumps > 1 semitone (FR-040)
// - Per-partial one-pole amplitude smoothing (FR-041)
// - Inharmonicity control from 0% (perfect harmonics) to 100% (FR-042)
//
// MCF Variant: sinNew = sin + eps*cos; cosNew = cos - eps*sinNew
// (uses updated sinNew, determinant = 1, amplitude-stable)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process: noexcept, no alloc, fixed arrays)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr)
// - Principle IV: SIMD & DSP Optimization (alignas(32), contiguous arrays)
// - Principle IX: Layer 2 (depends on Layer 0 core + harmonic_types.h)
// - Principle XII: Test-First Development
//
// Spec: specs/115-innexus-m1-core-instrument/spec.md (FR-035 to FR-042)
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>  // detail::isFinite (fast-math-immune)
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/harmonic_oscillator_bank_simd.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/core/filter_design.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/pitch_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Suppress MSVC C4324: structure was padded due to alignment specifier
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Krate::DSP {

/// @brief Harmonic oscillator bank for additive resynthesis from HarmonicFrames.
///
/// Uses 96 Gordon-Smith Modified Coupled Form (MCF) oscillators to resynthesize
/// audio from the analysis pipeline's HarmonicFrame output. Each oscillator
/// computes a sine wave via the MCF recurrence:
///   sinNew = sin + epsilon * cos
///   cosNew = cos - epsilon * sinNew
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (math_constants.h, pitch_utils.h), harmonic_types.h
///
/// @par Memory Model
/// SoA (Structure-of-Arrays) layout with 32-byte alignment for SIMD-friendly
/// access. All arrays are fixed-size (kMaxPartials = 96).
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (computes coefficients)
/// - All other methods: Real-time safe (noexcept, no allocations)
class HarmonicOscillatorBank {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Crossfade duration in seconds for large pitch jumps (FR-040, default 3ms)
    static constexpr float kDefaultCrossfadeTimeSec = 0.003f;

    /// Amplitude smoothing time in seconds (FR-041, ~2ms)
    static constexpr float kAmpSmoothTimeSec = 0.002f;

    /// Anti-aliasing fade start as fraction of Nyquist (FR-038)
    static constexpr float kAntiAliasFadeStart = 0.8f;

    /// Output safety clamp
    static constexpr float kOutputClamp = 2.0f;

    /// Target oscillator RMS for adaptive normalization (before harmonic level / master gain).
    /// This is the "unity" output level when globalAmplitude = 1.0.
    static constexpr float kTargetOscRms = 0.5f;

    /// Maximum normalization gain to prevent extreme amplification of near-silent frames.
    static constexpr float kMaxNormGain = 20.0f;

    /// Maximum detune per partial in cents at spread=1.0 (FR-030)
    static constexpr float kDetuneMaxCents = 15.0f;

    /// Fundamental partial spread reduction factor (FR-009)
    static constexpr float kFundamentalSpreadScale = 0.25f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    HarmonicOscillatorBank() noexcept = default;

    /// @brief Destructor.
    ~HarmonicOscillatorBank() noexcept = default;

    // Non-copyable, movable
    HarmonicOscillatorBank(const HarmonicOscillatorBank&) = delete;
    HarmonicOscillatorBank& operator=(const HarmonicOscillatorBank&) = delete;
    HarmonicOscillatorBank(HarmonicOscillatorBank&&) noexcept = default;
    HarmonicOscillatorBank& operator=(HarmonicOscillatorBank&&) noexcept = default;

    /// @brief Initialize for processing.
    ///
    /// Pre-computes smoothing coefficients and crossfade duration.
    /// Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @note NOT real-time safe
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        nyquist_ = static_cast<float>(sampleRate / 2.0);
        inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate);

        // Amplitude smoothing coefficient: one-pole for ~2ms (FR-041)
        // coeff = 1 - exp(-1 / (tau * sampleRate))
        // tau = kAmpSmoothTimeSec
        ampSmoothCoeff_ = 1.0f - std::exp(-1.0f /
            (kAmpSmoothTimeSec * static_cast<float>(sampleRate)));
        normGainSmoothed_ = 1.0f;

        // Per-partial noise LP filter for bandwidth modulation.
        // 4th-order Chebyshev Type I at 500 Hz with 1 dB ripple (~24 dB/oct).
        // Loris uses 3rd-order Chebyshev; 4th-order is even steeper and maps
        // cleanly to 2 biquad stages without needing a 1st-order section.
        constexpr float kNoiseLpCutoffHz = 500.0f;
        constexpr float kNoiseLpRippleDb = 1.0f;
        constexpr size_t kNoiseLpStages = 2;
        for (size_t s = 0; s < kNoiseLpStages; ++s) {
            float q = FilterDesign::chebyshevQ(s, kNoiseLpStages, kNoiseLpRippleDb);
            noiseLpCoeffs_[s] = BiquadCoefficients::calculate(
                FilterType::Lowpass, kNoiseLpCutoffHz, q, 0.0f,
                static_cast<float>(sampleRate));
        }

        // WI-8: measure the LCG->cascade output power for unit input and derive a
        // gain so the filtered noise has E[noise^2] = 0.5, which makes the Loris
        // bandwidth term ampMod = sqrt(1-bw) + noise*sqrt(2*bw) energy-preserving.
        {
            uint32_t seed = 0x9E3779B9u;
            std::array<float, kNoiseLpNumStages> z1{};
            std::array<float, kNoiseLpNumStages> z2{};
            constexpr int kWarmup = 1024;
            constexpr int kMeasure = 32768;
            double sumSq = 0.0;
            for (int n = 0; n < kWarmup + kMeasure; ++n) {
                seed = seed * 1664525u + 1013904223u;
                float x = static_cast<float>(static_cast<int32_t>(seed))
                    * (1.0f / 2147483648.0f);
                for (size_t s = 0; s < kNoiseLpNumStages; ++s) {
                    const auto& c = noiseLpCoeffs_[s];
                    float y = c.b0 * x + z1[s];
                    z1[s] = c.b1 * x - c.a1 * y + z2[s];
                    z2[s] = c.b2 * x - c.a2 * y;
                    x = y;
                }
                if (n >= kWarmup)
                    sumSq += static_cast<double>(x) * x;
            }
            const float rms =
                std::sqrt(static_cast<float>(sumSq / static_cast<double>(kMeasure)));
            constexpr float kTargetRms = 0.70710678f; // sqrt(0.5)
            noiseNormGain_ = kTargetRms / std::max(rms, 1e-9f);
        }

        // Crossfade length in samples (FR-040, default 3ms)
        crossfadeLengthSamples_ = static_cast<int>(
            kDefaultCrossfadeTimeSec * static_cast<float>(sampleRate));
        if (crossfadeLengthSamples_ < 1) crossfadeLengthSamples_ = 1;

        // Semitone ratio threshold for crossfade detection (> 1 semitone)
        crossfadeThresholdRatio_ = semitonesToRatio(1.0f);

        reset();
        prepared_ = true;
    }

    /// @brief Reset all oscillator states to silence.
    ///
    /// Clears all SoA arrays and sets active partials to 0.
    /// Does not change configuration or sample rate.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        sinState_.fill(0.0f);
        cosState_.fill(1.0f); // cos(0) = 1
        epsilon_.fill(0.0f);
        currentAmplitude_.fill(0.0f);
        targetAmplitude_.fill(0.0f);
        antiAliasGain_.fill(0.0f);
        relativeFrequency_.fill(0.0f);
        inharmonicDeviation_.fill(0.0f);
        harmonicIndex_.fill(0);
        activePartials_ = 0;
        targetPitch_ = 0.0f;
        frameLoaded_ = false;

        // Reset crossfade state
        crossfadeRemaining_ = 0;
        crossfadeOldLevel_ = 0.0f;
        // (renormCounter_ removed — MCF determinant=1 needs no renormalization)

        // Reset stereo/detune arrays to center (mono)
        panPosition_.fill(0.0f);
        // At center pan (angle = pi/4): cos(pi/4) = sin(pi/4) = sqrt(2)/2
        constexpr float kCenterGain = 0.7071067811865476f; // sqrt(2)/2
        panLeft_.fill(kCenterGain);
        panRight_.fill(kCenterGain);
        detuneMultiplier_.fill(1.0f);
        tailScanEnd_ = 0;
        // Cached pan/detune bases no longer describe this state (WI-20).
        panBaseValid_ = false;
        detuneBaseValid_ = false;
        bandwidth_.fill(0.0f);
        // Reset per-partial noise filter state
        for (auto& arr : noiseLpZ1_) arr.fill(0.0f);
        for (auto& arr : noiseLpZ2_) arr.fill(0.0f);
        sourcePitch_.fill(0.0f);
        sourceId_.fill(0);
        polyMode_ = false;
    }

    // =========================================================================
    // Frame and Pitch Control
    // =========================================================================

    /// @brief Load a harmonic frame (updates target amplitudes and frequencies).
    ///
    /// Updates target amplitudes and frequency parameters from the analysis frame.
    /// Phase accumulators (sinState/cosState) are NOT reset for phase continuity
    /// (FR-039). Epsilon and target amplitude are updated per partial.
    ///
    /// @param frame The harmonic analysis frame
    /// @param targetPitch The target playback pitch in Hz (from MIDI note)
    /// @note Real-time safe
    void loadFrame(const HarmonicFrame& frame, float targetPitch,
                   bool skipNormalization = false) noexcept {
        if (!prepared_) return;

        // Detect large pitch jumps for crossfade (FR-040)
        if (frameLoaded_ && targetPitch_ > 0.0f && targetPitch > 0.0f) {
            float ratio = targetPitch / targetPitch_;
            if (ratio < 1.0f) ratio = 1.0f / ratio;
            if (ratio > crossfadeThresholdRatio_) {
                // Snapshot current output level for crossfade
                crossfadeOldLevel_ = lastOutputSample_;
                crossfadeRemaining_ = crossfadeLengthSamples_;
            }
        }

        targetPitch_ = targetPitch;
        int numPartials = std::min(frame.numPartials, static_cast<int>(kMaxPartials));
        activePartials_ = numPartials;

        for (int i = 0; i < numPartials; ++i) {
            const auto& partial = frame.partials[i];
            harmonicIndex_[i] = partial.harmonicIndex;
            relativeFrequency_[i] = partial.relativeFrequency;
            inharmonicDeviation_[i] = partial.inharmonicDeviation;
            targetAmplitude_[i] = partial.amplitude;
            // QS-8: guard sqrt(1-bw). NaN-safe by construction -- the `> 0.0f`
            // test is false for NaN, so a non-finite bandwidth falls to 0
            // rather than through (std::clamp would pass NaN straight out,
            // since both of its comparisons are false for NaN).
            bandwidth_[i] = sanitizeBandwidth(partial.bandwidth);

            // Initialize oscillator state if this is the first frame
            if (!frameLoaded_) {
                float freq = computePartialFrequency(i);
                float phase = partial.phase; // use analysis phase
                sinState_[i] = std::sin(phase);
                cosState_[i] = std::cos(phase);
                epsilon_[i] = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
                currentAmplitude_[i] = 0.0f; // will ramp up via smoothing
            }
        }

        // Zero out unused partials
        for (size_t i = static_cast<size_t>(numPartials); i < kMaxPartials; ++i) {
            targetAmplitude_[i] = 0.0f;
            harmonicIndex_[i] = 0;
        }

        // Raise the tail-scan high-water mark so a shrinking partial count still
        // fades its old partials out (WI-22).
        tailScanEnd_ = std::max(tailScanEnd_, static_cast<size_t>(numPartials));

        // The cached pan/detune tables are functions of the harmonic indices as
        // well as the spread values, so a new frame invalidates them even when
        // the spreads have not moved (WI-20).
        panBaseValid_ = false;
        detuneBaseValid_ = false;

        // Adaptive output normalization: scale partial amplitudes so the
        // expected oscillator RMS hits a consistent target regardless of how
        // many partials are active or how the DFT distributed the energy.
        //
        // For N sinusoids with peak amplitudes a_i:
        //   expected RMS = sqrt(sum(a_i^2) / 2)
        //
        // We scale so that expected RMS ≈ kTargetOscRms.  This ensures the
        // oscillator bank produces synth-level output (~-6 dBFS) that the
        // user then shapes with Harmonic Level, velocity, and Master Gain.
        // Relative dynamics between frames are preserved because frames with
        // more partial energy naturally produce a higher expectedRms.
        //
        // The normalization gain is smoothed with the same one-pole coefficient
        // as partial amplitudes.  Without smoothing, every frame applies a
        // potentially different scale to ALL partials simultaneously, creating
        // a correlated step that manifests as broadband inter-harmonic noise.
        // (DDSP, Engel et al. 2020 — amplitude smoothing prevents artifacts.)
        // Skip normalization during spectral decay — the decay envelope is
        // intentionally reducing amplitudes toward zero, and renormalization
        // would fight the decay (boosting gain to compensate for the drop).
        // On ARM/NEON with FMA, the normalization gain diverges from x86 due
        // to different intermediate rounding in the sumSq accumulation,
        // causing catastrophic amplitude collapse instead of gradual fade.
        if (!skipNormalization)
        {
            float sumSq = 0.0f;
            for (int i = 0; i < numPartials; ++i)
                sumSq += targetAmplitude_[i] * targetAmplitude_[i];

            float expectedRms = std::sqrt(sumSq * 0.5f);
            float targetNormGain = 1.0f;
            if (expectedRms > 1e-10f)
            {
                targetNormGain = std::min(kTargetOscRms / expectedRms, kMaxNormGain);
            }

            // Smooth the normalization gain to avoid correlated amplitude steps.
            // Use a fast but non-instant coefficient (~0.3) so the gain tracks
            // within 3-4 frames while eliminating abrupt jumps.  The per-partial
            // one-pole smoother handles the remaining sub-frame smoothing.
            constexpr float kNormSmoothCoeff = 0.3f;
            normGainSmoothed_ += kNormSmoothCoeff * (targetNormGain - normGainSmoothed_);

            for (int i = 0; i < numPartials; ++i)
                targetAmplitude_[i] *= normGainSmoothed_;
        }

        // Recalculate frequencies and anti-aliasing for all active partials
        recalculateFrequencies();
        recalculateAntiAliasing();

        // Check if any partial has bandwidth (for SIMD fast path selection)
        hasBandwidth_ = false;
        for (int i = 0; i < numPartials; ++i) {
            if (bandwidth_[i] > 1e-4f) {
                hasBandwidth_ = true;
                break;
            }
        }

        frameLoaded_ = true;
        polyMode_ = false;
    }

    /// @brief Set the target pitch (MIDI-driven).
    ///
    /// Recalculates epsilon for all active partials immediately.
    /// Anti-aliasing gain is recalculated on the new frequencies.
    /// Triggers crossfade if jump > 1 semitone (FR-040).
    ///
    /// @param frequencyHz Target fundamental frequency in Hz
    /// @note Real-time safe
    void setTargetPitch(float frequencyHz) noexcept {
        if (!prepared_ || frequencyHz <= 0.0f) return;

        // Detect large pitch jumps for crossfade (FR-040)
        if (frameLoaded_ && targetPitch_ > 0.0f) {
            float ratio = frequencyHz / targetPitch_;
            if (ratio < 1.0f) ratio = 1.0f / ratio;
            if (ratio > crossfadeThresholdRatio_) {
                crossfadeOldLevel_ = lastOutputSample_;
                crossfadeRemaining_ = crossfadeLengthSamples_;
            }
        }

        targetPitch_ = frequencyHz;
        recalculateFrequencies();
        recalculateAntiAliasing();
    }

    /// @brief Set inharmonicity amount (FR-042).
    ///
    /// @param amount 0.0 = perfect harmonic ratios, 1.0 = source's captured deviations
    /// @note Real-time safe
    void setInharmonicityAmount(float amount) noexcept {
        inharmonicityAmount_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_ && frameLoaded_) {
            recalculateFrequencies();
            recalculateAntiAliasing();
        }
    }

    // =========================================================================
    // Polyphonic / Source-Aware Resynthesis (Tier 3b)
    // =========================================================================

    /// @brief Load a polyphonic frame with per-source pitch control.
    ///
    /// Merges partials from all sources in a PolyphonicFrame into the oscillator
    /// bank. Each source's partials are resynthesized at their respective
    /// target pitches, enabling independent pitch shifting per source.
    ///
    /// @param polyFrame The polyphonic analysis frame
    /// @param sourcePitches Per-source target pitches in Hz (array of kMaxPolyphonicVoices).
    ///                      Set to 0 for a source to use its analyzed F0.
    /// @param defaultPitch Fallback pitch for sources with sourcePitches[s]==0
    /// @note Real-time safe
    void loadPolyphonicFrame(const PolyphonicFrame& polyFrame,
                              const float* sourcePitches,
                              float defaultPitch) noexcept {
        if (!prepared_) return;

        // Detect large pitch jumps for crossfade (FR-040) using default pitch
        if (frameLoaded_ && targetPitch_ > 0.0f && defaultPitch > 0.0f) {
            float ratio = defaultPitch / targetPitch_;
            if (ratio < 1.0f) ratio = 1.0f / ratio;
            if (ratio > crossfadeThresholdRatio_) {
                crossfadeOldLevel_ = lastOutputSample_;
                crossfadeRemaining_ = crossfadeLengthSamples_;
            }
        }

        targetPitch_ = defaultPitch;
        int totalPartials = 0;

        // Load partials from each source
        for (int s = 0; s < polyFrame.numSources && totalPartials < static_cast<int>(kMaxPartials); ++s) {
            const auto& src = polyFrame.sources[s];
            float srcPitch = (sourcePitches != nullptr && sourcePitches[s] > 0.0f)
                                 ? sourcePitches[s]
                                 : (src.f0 > 0.0f ? defaultPitch : 0.0f);

            for (int i = 0; i < src.numPartials && totalPartials < static_cast<int>(kMaxPartials); ++i) {
                const auto& partial = src.partials[i];
                harmonicIndex_[totalPartials] = partial.harmonicIndex;
                relativeFrequency_[totalPartials] = partial.relativeFrequency;
                inharmonicDeviation_[totalPartials] = partial.inharmonicDeviation;
                targetAmplitude_[totalPartials] = partial.amplitude;
                bandwidth_[totalPartials] = sanitizeBandwidth(partial.bandwidth); // QS-8
                sourceId_[totalPartials] = partial.sourceId;
                sourcePitch_[totalPartials] = srcPitch;

                if (!frameLoaded_) {
                    float freq = computeSourcePartialFrequency(totalPartials);
                    float phase = partial.phase;
                    sinState_[totalPartials] = std::sin(phase);
                    cosState_[totalPartials] = std::cos(phase);
                    epsilon_[totalPartials] = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
                    currentAmplitude_[totalPartials] = 0.0f;
                }

                ++totalPartials;
            }
        }

        activePartials_ = totalPartials;

        // Zero out unused partials
        for (size_t i = static_cast<size_t>(totalPartials); i < kMaxPartials; ++i) {
            targetAmplitude_[i] = 0.0f;
            harmonicIndex_[i] = 0;
            sourceId_[i] = 0;
            sourcePitch_[i] = 0.0f;
        }

        // Recalculate frequencies using per-source pitches
        recalculateSourceFrequencies();
        recalculateAntiAliasing();

        // Check bandwidth for SIMD fast path selection
        hasBandwidth_ = false;
        for (int i = 0; i < totalPartials; ++i) {
            if (bandwidth_[i] > 1e-4f) {
                hasBandwidth_ = true;
                break;
            }
        }

        frameLoaded_ = true;
        polyMode_ = true;
    }

    /// @brief Set the target pitch for a specific source in polyphonic mode.
    ///
    /// @param sourceId 1-based source ID
    /// @param frequencyHz Target frequency for this source
    /// @note Real-time safe
    void setSourcePitch(int sourceId, float frequencyHz) noexcept {
        if (!prepared_ || !polyMode_ || sourceId <= 0 || frequencyHz <= 0.0f) return;

        for (int i = 0; i < activePartials_; ++i) {
            if (sourceId_[i] == sourceId) {
                sourcePitch_[i] = frequencyHz;
            }
        }
        recalculateSourceFrequencies();
        recalculateAntiAliasing();
    }

    // =========================================================================
    // Stereo Spread and Detune (M6: FR-006 to FR-013, FR-030 to FR-032)
    // =========================================================================

    /// @brief Set stereo spread amount (FR-006, FR-008, FR-009).
    ///
    /// Recalculates per-partial pan positions and pan coefficients.
    /// Odd partials pan left, even partials pan right.
    /// Fundamental (partial 1) uses reduced spread (25%) for bass mono compat (FR-009).
    ///
    /// Pan law: constant-power
    ///   angle = pi/4 + panPosition * pi/4
    ///   panLeft[n] = cos(angle)
    ///   panRight[n] = sin(angle)
    ///
    /// @param spread Spread amount [0.0, 1.0]. 0.0 = mono center, 1.0 = max spread.
    /// @note Real-time safe (called once per frame, not per sample)
    void setStereoSpread(float spread) noexcept {
        const float clamped = std::clamp(spread, 0.0f, 1.0f);

        // WI-20: callers drive this from a per-sample smoother, so it runs at
        // audio rate even though the doxygen above says "once per frame". The
        // trig only depends on the spread value and the harmonic indices, so
        // once the smoother settles every recompute reproduces the same table.
        // Cache that table and restore it instead.
        //
        // The restore cannot be skipped as well: harmonic modulators overwrite
        // panLeft_/panRight_ via applyPanOffsets(), so leaving them alone would
        // strand the last modulated values once modulation stops.
        if (panBaseValid_ && clamped == stereoSpread_) {
            panPosition_ = basePanPosition_;
            panLeft_ = basePanLeft_;
            panRight_ = basePanRight_;
            return;
        }

        stereoSpread_ = clamped;
        recalculatePanPositions();
        basePanPosition_ = panPosition_;
        basePanLeft_ = panLeft_;
        basePanRight_ = panRight_;
        panBaseValid_ = true;
        ++panRecomputeCount_;
    }

    /// @brief Set detune spread amount (FR-030, FR-031, FR-032).
    ///
    /// Computes per-partial frequency multipliers for chorus-like detuning.
    /// Offset scales with harmonic number, alternating +/- by odd/even.
    /// Fundamental is excluded from detune (SC-005: < 1 cent deviation).
    ///
    /// @param spread Detune amount [0.0, 1.0]. 0.0 = no detune, 1.0 = max.
    /// @note Real-time safe (called once per frame, not per sample)
    void setDetuneSpread(float spread) noexcept {
        const float clamped = std::clamp(spread, 0.0f, 1.0f);

        // WI-20: as with setStereoSpread, this is driven per sample, and the
        // std::pow per partial plus the anti-aliasing recompute are the most
        // expensive things in that loop.
        //
        // The restore is mandatory, not an optimization detail:
        // applyExternalFrequencyMultipliers() multiplies into detuneMultiplier_
        // in place, so this recompute doubles as the per-sample base reset.
        // Skipping it outright would let the modulator's multiplier compound
        // every sample into a runaway detune.
        //
        // Anti-aliasing is computed from the BASE detune (it already ran before
        // any modulator multiply), so an unchanged spread leaves it unchanged.
        if (detuneBaseValid_ && clamped == detuneSpread_) {
            detuneMultiplier_ = baseDetuneMultiplier_;
            return;
        }

        detuneSpread_ = clamped;
        recalculateDetuneMultipliers();
        baseDetuneMultiplier_ = detuneMultiplier_;
        detuneBaseValid_ = true;
        // Refresh anti-alias gains for the new detuned frequencies (WI-5): a
        // partial detuned toward Nyquist must fade rather than sustain.
        recalculateAntiAliasing();
        ++detuneRecomputeCount_;
    }

    /// @brief Number of times the pan table was actually recomputed (test hook).
    [[nodiscard]] int panRecomputeCount() const noexcept { return panRecomputeCount_; }

    /// @brief Number of times the detune table was actually recomputed (test hook).
    [[nodiscard]] int detuneRecomputeCount() const noexcept {
        return detuneRecomputeCount_;
    }

    /// @brief Get the current stereo spread value.
    [[nodiscard]] float getStereoSpread() const noexcept { return stereoSpread_; }

    /// @brief Get the current detune spread value.
    [[nodiscard]] float getDetuneSpread() const noexcept { return detuneSpread_; }

    /// @brief True if every active partial's oscillator state is finite.
    /// Diagnoses MCF divergence (which the output clamp would otherwise hide by
    /// railing at +/-kOutputClamp). Uses a bit test so it holds under -ffast-math.
    [[nodiscard]] bool stateFinite() const noexcept {
        // detail::isFinite is the barrier-hardened core check: a plain local
        // memcpy/bit-mask is foldable under newer fast-math compilers.
        auto finite = [](float x) noexcept { return detail::isFinite(x); };
        for (int i = 0; i < activePartials_; ++i) {
            const auto idx = static_cast<size_t>(i);
            if (!finite(sinState_[idx]) || !finite(cosState_[idx])) return false;
        }
        return true;
    }

    /// @brief Apply external pan offsets to current pan positions (M6 FR-027).
    ///
    /// Adds the given offset to each partial's pan position and recomputes
    /// the constant-power pan coefficients. Used by harmonic modulators for
    /// per-partial pan animation.
    ///
    /// @param offsets Array of kMaxPartials bipolar offsets to add to pan positions
    /// @note Real-time safe (called once per frame)
    void applyPanOffsets(
        const std::array<float, kMaxPartials>& offsets) noexcept
    {
        constexpr float kQuarterPi = kPi / 4.0f;

        for (size_t i = 0; i < kMaxPartials; ++i)
        {
            float newPos = std::clamp(panPosition_[i] + offsets[i], -1.0f, 1.0f);
            float angle = kQuarterPi + newPos * kQuarterPi;
            panLeft_[i] = std::cos(angle);
            panRight_[i] = std::sin(angle);
        }
    }

    /// @brief Apply external frequency multipliers on top of detune (M6 FR-026).
    ///
    /// Multiplies the given multipliers with the existing detuneMultiplier_
    /// for each partial. Used by harmonic modulators for frequency animation.
    ///
    /// @param multipliers Array of kMaxPartials frequency multipliers
    /// @note Real-time safe (called once per frame)
    void applyExternalFrequencyMultipliers(
        const std::array<float, kMaxPartials>& multipliers) noexcept
    {
        for (size_t i = 0; i < kMaxPartials; ++i)
        {
            detuneMultiplier_[i] *= multipliers[i];
        }
    }

    /// @brief Generate a single stereo output sample (FR-007, FR-050).
    ///
    /// Each partial contributes to left and right channels based on its pan
    /// position. Pan positions are set by setStereoSpread() and updated per frame.
    ///
    /// When stereoSpread == 0.0, left == right (mono center, SC-010).
    ///
    /// @param[out] left Left channel output sample
    /// @param[out] right Right channel output sample
    /// @note Real-time safe
    void processStereo(float& left, float& right) noexcept {
        if (!prepared_ || !frameLoaded_) {
            left = right = 0.0f;
            return;
        }

        float sumL = 0.0f;
        float sumR = 0.0f;
        const int n = activePartials_;

        // Use SIMD path when no bandwidth modulation is needed.
        // The SIMD path handles amplitude smoothing + MCF advance + stereo pan
        // for all active partials in batches of 4/8.
        // Bandwidth modulation remains scalar (rare: only when partials have bw > 0).
        //
        // Note: No periodic renormalization. The MCF (Gordon-Smith modified coupled
        // form) has determinant = 1, so amplitude is bounded in finite precision.
        // Hard renormalization every N samples introduces step discontinuities
        // that degrade SNR by ~10 dB. See Dattorro (AES 2002), Smith (CCRMA).
        if (!hasBandwidth_) {
            processMcfBatchSIMD(
                sinState_.data(), cosState_.data(),
                epsilon_.data(), detuneMultiplier_.data(),
                currentAmplitude_.data(), targetAmplitude_.data(),
                antiAliasGain_.data(), panLeft_.data(), panRight_.data(),
                ampSmoothCoeff_, n, sumL, sumR);
        } else {
            // Scalar path: handles bandwidth modulation
            for (int i = 0; i < n; ++i) {
                // Amplitude smoothing (FR-041)
                float target = targetAmplitude_[i] * antiAliasGain_[i];
                currentAmplitude_[i] += ampSmoothCoeff_ * (target - currentAmplitude_[i]);

                // MCF oscillator
                float s = sinState_[i];
                float c = cosState_[i];
                // Clamp the EFFECTIVE coefficient: detune/modulator multipliers
                // can push epsilon*detune past the |eps|<2 MCF stability bound
                // even though the base epsilon is clamped, causing divergence. (WI-5)
                float eps = std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f);

                // Bandwidth-enhanced synthesis (Loris model)
                float bw = bandwidth_[i];
                float ampMod = 1.0f;
                if (bw > 1e-4f) {
                    float noise = nextFilteredNoise(i);
                    ampMod = std::sqrt(1.0f - bw) + noise * std::sqrt(2.0f * bw);
                }

                // Output: amplitude * sine * bandwidth modulation * pan coefficients
                float ampSample = s * currentAmplitude_[i] * ampMod;
                sumL += ampSample * panLeft_[i];
                sumR += ampSample * panRight_[i];

                // Advance phasor: Gordon-Smith MCF (determinant = 1)
                float sNew = s + eps * c;
                float cNew = c - eps * sNew;

                sinState_[i] = sNew;
                cosState_[i] = cNew;
            }
        }

        // Fade out residual partials beyond activePartials_.
        //
        // Only up to the high-water mark of previously-active partials (WI-22).
        // Everything above it has been silent since the last reset, so scanning
        // all the way to kMaxPartials just tests 96 zeroes every sample. The
        // mark is lowered only once the entire tail has decayed, so a partial
        // that is still fading can never be dropped.
        const size_t tailEnd = std::min(tailScanEnd_, kMaxPartials);
        bool tailActive = false;
        for (size_t i = static_cast<size_t>(n); i < tailEnd; ++i) {
            if (currentAmplitude_[i] > 1e-8f) {
                tailActive = true;
                currentAmplitude_[i] += ampSmoothCoeff_ * (0.0f - currentAmplitude_[i]);

                float s = sinState_[i];
                float c = cosState_[i];
                // Clamp the EFFECTIVE coefficient: detune/modulator multipliers
                // can push epsilon*detune past the |eps|<2 MCF stability bound
                // even though the base epsilon is clamped, causing divergence. (WI-5)
                float eps = std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f);
                float ampSample = s * currentAmplitude_[i];
                sumL += ampSample * panLeft_[i];
                sumR += ampSample * panRight_[i];

                float sNew = s + eps * c;
                float cNew = c - eps * sNew;

                sinState_[i] = sNew;
                cosState_[i] = cNew;
            }
        }
        // Nothing above n is fading any more: shrink the scan window. loadFrame
        // raises it again whenever a larger partial count becomes active.
        if (!tailActive) tailScanEnd_ = static_cast<size_t>(n);

        // Apply crossfade if active (FR-040)
        if (crossfadeRemaining_ > 0) {
            float fadeProgress = static_cast<float>(crossfadeRemaining_) /
                                 static_cast<float>(crossfadeLengthSamples_);
            sumL = crossfadeOldLevel_ * fadeProgress + sumL * (1.0f - fadeProgress);
            sumR = crossfadeOldLevel_ * fadeProgress + sumR * (1.0f - fadeProgress);
            --crossfadeRemaining_;
        }

        // Safety clamp
        left = std::clamp(sumL, -kOutputClamp, kOutputClamp);
        right = std::clamp(sumR, -kOutputClamp, kOutputClamp);

        lastOutputSample_ = (left + right) * 0.5f;
    }

    /// @brief Generate a block of stereo output samples (FR-007).
    ///
    /// @param[out] leftOutput Left channel buffer (must hold numSamples)
    /// @param[out] rightOutput Right channel buffer (must hold numSamples)
    /// @param numSamples Number of samples to generate
    /// @note Real-time safe
    void processStereoBlock(float* leftOutput, float* rightOutput,
                            size_t numSamples) noexcept {
        if (leftOutput == nullptr || rightOutput == nullptr || numSamples == 0) {
            return;
        }

        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                leftOutput[i] = 0.0f;
                rightOutput[i] = 0.0f;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            processStereo(leftOutput[i], rightOutput[i]);
        }
    }

    // =========================================================================
    // Processing (FR-035)
    // =========================================================================

    /// @brief Generate a single output sample.
    ///
    /// @return Mono output sample
    /// @note Real-time safe
    [[nodiscard]] float process() noexcept {
        if (!prepared_ || !frameLoaded_) {
            return 0.0f;
        }

        float sum = 0.0f;
        const int n = activePartials_;

        for (int i = 0; i < n; ++i) {
            // Amplitude smoothing (FR-041)
            float target = targetAmplitude_[i] * antiAliasGain_[i];
            currentAmplitude_[i] += ampSmoothCoeff_ * (target - currentAmplitude_[i]);

            // MCF oscillator (FR-035)
            float s = sinState_[i];
            float c = cosState_[i];
            float eps = epsilon_[i];

            // Bandwidth-enhanced synthesis (Loris model)
            float bw = bandwidth_[i];
            float ampMod = 1.0f;
            if (bw > 1e-4f) {
                float noise = nextFilteredNoise(i);
                ampMod = std::sqrt(1.0f - bw) + noise * std::sqrt(2.0f * bw);
            }

            // Output: amplitude * sine * bandwidth modulation
            sum += s * currentAmplitude_[i] * ampMod;

            // Advance phasor: Gordon-Smith MCF (determinant = 1, no renorm needed)
            float sNew = s + eps * c;
            float cNew = c - eps * sNew;

            sinState_[i] = sNew;
            cosState_[i] = cNew;
        }

        // Also smoothly fade out any partials beyond activePartials_ that
        // still have residual amplitude (bounded per WI-22, see processStereo).
        const size_t tailEnd = std::min(tailScanEnd_, kMaxPartials);
        bool tailActive = false;
        for (size_t i = static_cast<size_t>(n); i < tailEnd; ++i) {
            if (currentAmplitude_[i] > 1e-8f) {
                tailActive = true;
                currentAmplitude_[i] += ampSmoothCoeff_ * (0.0f - currentAmplitude_[i]);

                float s = sinState_[i];
                float c = cosState_[i];
                float eps = epsilon_[i];
                sum += s * currentAmplitude_[i];
                float sNew = s + eps * c;
                float cNew = c - eps * sNew;

                sinState_[i] = sNew;
                cosState_[i] = cNew;
            }
        }
        // Nothing above n is fading any more: shrink the scan window. loadFrame
        // raises it again whenever a larger partial count becomes active.
        if (!tailActive) tailScanEnd_ = static_cast<size_t>(n);

        // Apply crossfade if active (FR-040)
        if (crossfadeRemaining_ > 0) {
            float fadeProgress = static_cast<float>(crossfadeRemaining_) /
                                 static_cast<float>(crossfadeLengthSamples_);
            // Linear crossfade: old * fadeProgress + new * (1 - fadeProgress)
            sum = crossfadeOldLevel_ * fadeProgress + sum * (1.0f - fadeProgress);
            --crossfadeRemaining_;
        }

        // Safety clamp
        sum = std::clamp(sum, -kOutputClamp, kOutputClamp);

        lastOutputSample_ = sum;
        return sum;
    }

    /// @brief Generate a block of output samples.
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @note Real-time safe
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the bank has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get the current target pitch.
    [[nodiscard]] float getTargetPitch() const noexcept { return targetPitch_; }

    /// @brief Get the number of active partials.
    [[nodiscard]] int getActivePartials() const noexcept { return activePartials_; }

    /// @brief Get the current inharmonicity amount.
    [[nodiscard]] float getInharmonicityAmount() const noexcept { return inharmonicityAmount_; }

    /// @brief Check if a frame has been loaded.
    [[nodiscard]] bool isFrameLoaded() const noexcept { return frameLoaded_; }

    /// @brief Verify that SoA arrays are 32-byte aligned (for testing).
    [[nodiscard]] bool areArraysAligned() const noexcept {
        return (reinterpret_cast<std::uintptr_t>(sinState_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(cosState_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(epsilon_.data()) % 32 == 0) &&
               (reinterpret_cast<std::uintptr_t>(currentAmplitude_.data()) % 32 == 0);
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Clamp an incoming bandwidth into [0, 1], mapping non-finite to 0.
    ///
    /// The bank hardens its own input rather than trusting the analysis stage:
    /// bandwidth feeds sqrt(1 - bw), so a value outside [0, 1] yields NaN and a
    /// NaN would propagate into the output sum (never into the MCF state, which
    /// is why stateFinite() does not catch it).
    ///
    /// Deliberately not std::clamp: clamp is `v < lo ? lo : (hi < v ? hi : v)`,
    /// and BOTH comparisons are false for NaN, so clamp returns NaN unchanged.
    /// The `> 0.0f` test below is likewise false for NaN, but here that steers
    /// NaN to the safe 0 branch. Infinity and negatives are handled by the same
    /// expression (+inf takes min -> 1, negatives take the else -> 0).
    ///
    /// @note At present a NaN would also be neutralised downstream by the
    ///       `bw > 1e-4f` bandwidth gate in the synthesis loops. That is a
    ///       coincidence of that gate's comparison direction, not a guarantee;
    ///       normalising at ingestion keeps the invariant local so a later edit
    ///       to the gate cannot silently re-open the hole.
    /// @param bandwidth Raw bandwidth from the analysis frame
    /// @return A finite value in [0, 1]
    [[nodiscard]] static float sanitizeBandwidth(float bandwidth) noexcept {
        return (bandwidth > 0.0f) ? std::min(bandwidth, 1.0f) : 0.0f;
    }

    /// @brief Generate a LP-filtered noise sample for bandwidth modulation.
    /// Uses a per-partial 4th-order Chebyshev Type I LP at 500 Hz (1 dB ripple)
    /// for steep rolloff, matching Loris's filtered noise approach.
    /// Each partial gets independent noise and filter state.
    /// @param partialIdx Index of the partial [0, kMaxPartials)
    /// @return Filtered noise sample
    [[nodiscard]] float nextFilteredNoise(int partialIdx) noexcept {
        // Linear congruential generator (fast, sufficient for noise modulation)
        noiseState_ = noiseState_ * 1664525u + 1013904223u;
        float x = static_cast<float>(static_cast<int32_t>(noiseState_))
            * (1.0f / 2147483648.0f);

        // Apply 2-stage biquad cascade (4th-order Chebyshev Type I LP)
        // using per-partial state and shared coefficients.
        const auto pi = static_cast<size_t>(partialIdx);
        for (size_t s = 0; s < kNoiseLpNumStages; ++s) {
            const auto& c = noiseLpCoeffs_[s];
            float& z1 = noiseLpZ1_[s][pi];
            float& z2 = noiseLpZ2_[s][pi];
            float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            x = y;
        }
        return x * noiseNormGain_; // WI-8: normalize to E[noise^2] = 0.5
    }

    /// @brief Compute per-partial frequency (FR-037).
    ///
    /// freq_n = (harmonicIndex + inharmonicDeviation * inharmonicityAmount) * targetPitch
    [[nodiscard]] float computePartialFrequency(int partialIndex) const noexcept {
        int n = harmonicIndex_[partialIndex];
        float deviation = inharmonicDeviation_[partialIndex];
        return (static_cast<float>(n) + deviation * inharmonicityAmount_) * targetPitch_;
    }

    /// @brief Compute per-partial frequency using per-source pitch (Tier 3b).
    ///
    /// Uses sourcePitch_[i] instead of targetPitch_ when in polyphonic mode.
    [[nodiscard]] float computeSourcePartialFrequency(int partialIndex) const noexcept {
        int n = harmonicIndex_[partialIndex];
        float deviation = inharmonicDeviation_[partialIndex];
        float pitch = sourcePitch_[partialIndex] > 0.0f
                          ? sourcePitch_[partialIndex]
                          : targetPitch_;
        return (static_cast<float>(n) + deviation * inharmonicityAmount_) * pitch;
    }

    /// @brief Recalculate epsilon for all active partials using per-source pitches.
    void recalculateSourceFrequencies() noexcept {
        constexpr float kMaxEpsilon = 1.99f;
        for (int i = 0; i < activePartials_; ++i) {
            float freq = computeSourcePartialFrequency(i);
            float eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
            epsilon_[i] = std::clamp(eps, -kMaxEpsilon, kMaxEpsilon);
        }
    }

    /// @brief Recalculate epsilon for all active partials.
    ///
    /// Epsilon is clamped to a safe range to prevent MCF instability
    /// at frequencies very close to Nyquist.
    void recalculateFrequencies() noexcept {
        // Maximum safe epsilon to prevent MCF divergence near Nyquist
        // |epsilon| must be < 2.0; use 1.99 as safety margin
        constexpr float kMaxEpsilon = 1.99f;

        for (int i = 0; i < activePartials_; ++i) {
            float freq = computePartialFrequency(i);
            float eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
            epsilon_[i] = std::clamp(eps, -kMaxEpsilon, kMaxEpsilon);
        }
    }

    /// @brief Recalculate anti-aliasing gains (FR-038).
    ///
    /// Full gain below 80% Nyquist, linear fade to zero at Nyquist.
    void recalculateAntiAliasing() noexcept {
        const float fadeStart = kAntiAliasFadeStart * nyquist_;
        const float fadeRange = nyquist_ - fadeStart; // 0.2 * nyquist

        for (int i = 0; i < activePartials_; ++i) {
            // WI-5: use the DETUNED frequency actually synthesized (base * detune).
            // This fades a partial that detune pushes toward/past Nyquist and makes
            // the MCF elliptical correction match the detuned orbit's eccentricity,
            // so an over-detuned high partial is amplitude-suppressed instead of
            // sustaining at a large clamped-epsilon level. When detune is 1.0 this
            // is identical to the base frequency (no behavior change).
            float freq = computePartialFrequency(i) * detuneMultiplier_[static_cast<size_t>(i)];

            // MCF elliptical orbit correction (Smith, CCRMA):
            // The MCF sin output has amplitude 1/cos(pi*f/fs), so we
            // pre-compensate by multiplying by cos(pi*f/fs).
            float mcfCorrection = std::cos(kPi * freq * inverseSampleRate_);
            if (mcfCorrection < 0.0f) mcfCorrection = 0.0f; // past Nyquist -> silence

            float aaGain;
            if (freq <= fadeStart) {
                aaGain = 1.0f;
            } else if (freq >= nyquist_) {
                aaGain = 0.0f;
            } else {
                aaGain = (nyquist_ - freq) / fadeRange;
            }
            antiAliasGain_[i] = aaGain * mcfCorrection;
        }

        // Zero gain for unused partials
        for (size_t i = static_cast<size_t>(activePartials_); i < kMaxPartials; ++i) {
            antiAliasGain_[i] = 0.0f;
        }
    }

    /// @brief Recalculate per-partial pan positions and coefficients.
    ///
    /// Odd partials pan left (negative), even partials pan right (positive).
    /// Fundamental (partial 1) uses reduced spread (kFundamentalSpreadScale).
    void recalculatePanPositions() noexcept {
        constexpr float kQuarterPi = kPi / 4.0f;

        for (size_t i = 0; i < kMaxPartials; ++i) {
            int harmIdx = harmonicIndex_[i];
            if (harmIdx <= 0) {
                // Unused partial: center
                panPosition_[i] = 0.0f;
            } else {
                // Odd harmonics (1,3,5,...) pan left (-), even (2,4,6,...) pan right (+)
                float direction = (harmIdx % 2 == 1) ? -1.0f : 1.0f;
                float effectiveSpread = stereoSpread_;

                // FR-009: Fundamental (partial 1) uses reduced spread
                if (harmIdx == 1) {
                    effectiveSpread *= kFundamentalSpreadScale;
                }

                panPosition_[i] = direction * effectiveSpread;
            }

            // Constant-power pan law: angle = pi/4 + panPosition * pi/4
            float angle = kQuarterPi + panPosition_[i] * kQuarterPi;
            panLeft_[i] = std::cos(angle);
            panRight_[i] = std::sin(angle);
        }
    }

    /// @brief Recalculate per-partial detune frequency multipliers.
    ///
    /// Formula: detuneOffset_n = detuneSpread * n * kDetuneMaxCents * direction
    ///          multiplier_n = pow(2.0, detuneOffset_n / 1200.0)
    /// Fundamental (harmonic 1) is excluded (set to 1.0) to satisfy SC-005.
    void recalculateDetuneMultipliers() noexcept {
        for (size_t i = 0; i < kMaxPartials; ++i) {
            int harmIdx = harmonicIndex_[i];
            if (harmIdx <= 1 || detuneSpread_ <= 0.0f) {
                // Fundamental or unused: no detune
                detuneMultiplier_[i] = 1.0f;
            } else {
                // direction: +1 for odd, -1 for even
                float direction = (harmIdx % 2 == 1) ? 1.0f : -1.0f;
                float offsetCents = detuneSpread_ * static_cast<float>(harmIdx)
                                    * kDetuneMaxCents * direction;
                detuneMultiplier_[i] = std::pow(2.0f, offsetCents / 1200.0f);
            }
        }
    }

    // =========================================================================
    // Members -- SoA layout, 32-byte aligned (FR-036)
    // =========================================================================

    alignas(32) std::array<float, kMaxPartials> sinState_{};
    alignas(32) std::array<float, kMaxPartials> cosState_{};
    alignas(32) std::array<float, kMaxPartials> epsilon_{};
    alignas(32) std::array<float, kMaxPartials> currentAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> targetAmplitude_{};
    alignas(32) std::array<float, kMaxPartials> antiAliasGain_{};
    alignas(32) std::array<float, kMaxPartials> relativeFrequency_{};
    alignas(32) std::array<float, kMaxPartials> inharmonicDeviation_{};
    alignas(32) std::array<float, kMaxPartials> bandwidth_{}; ///< Per-partial noisiness [0,1]

    // --- Stereo pan and detune arrays (M6 FR-006 to FR-013, FR-030 to FR-032) ---
    alignas(32) std::array<float, kMaxPartials> panPosition_{};      ///< [-1, +1]
    alignas(32) std::array<float, kMaxPartials> panLeft_{};           ///< cos(angle)
    alignas(32) std::array<float, kMaxPartials> panRight_{};          ///< sin(angle)
    alignas(32) std::array<float, kMaxPartials> detuneMultiplier_{};  ///< freq multiplier

    // WI-20: cached pan/detune tables, recomputed only when the spread value or
    // the harmonic indices change. detuneMultiplier_ and panLeft_/panRight_ are
    // mutated in place by the harmonic modulators, so these hold the unmutated
    // base that each sample is restored from.
    alignas(32) std::array<float, kMaxPartials> basePanPosition_{};
    alignas(32) std::array<float, kMaxPartials> basePanLeft_{};
    alignas(32) std::array<float, kMaxPartials> basePanRight_{};
    alignas(32) std::array<float, kMaxPartials> baseDetuneMultiplier_{};
    /// Highest partial index that may still hold a fading tail (WI-22).
    size_t tailScanEnd_ = kMaxPartials;

    bool panBaseValid_ = false;
    bool detuneBaseValid_ = false;
    int panRecomputeCount_ = 0;
    int detuneRecomputeCount_ = 0;

    // --- Source-aware resynthesis arrays (Tier 3b) ---
    alignas(32) std::array<float, kMaxPartials> sourcePitch_{};     ///< Per-partial source pitch (Hz)
    std::array<int, kMaxPartials> sourceId_{};                       ///< Per-partial source ID (0=mono)

    /// Harmonic index per partial (not float -- integer)
    std::array<int, kMaxPartials> harmonicIndex_{};

    // --- Configuration ---
    float targetPitch_ = 0.0f;            ///< Target fundamental frequency (Hz)
    float inharmonicityAmount_ = 1.0f;    ///< 0 = pure harmonics, 1 = source deviation
    double sampleRate_ = 44100.0;          ///< Current sample rate
    float nyquist_ = 22050.0f;            ///< Nyquist frequency
    float inverseSampleRate_ = 1.0f / 44100.0f; ///< Precomputed 1/sampleRate

    // --- Stereo/Detune state ---
    float stereoSpread_ = 0.0f;           ///< Current stereo spread [0, 1]
    float detuneSpread_ = 0.0f;           ///< Current detune spread [0, 1]

    // --- Smoothing ---
    float ampSmoothCoeff_ = 0.0f;         ///< One-pole amplitude smoothing coefficient
    float normGainSmoothed_ = 1.0f;       ///< Smoothed normalization gain (prevents correlated steps)

    // --- Crossfade (FR-040) ---
    int crossfadeLengthSamples_ = 0;      ///< Crossfade duration in samples
    int crossfadeRemaining_ = 0;          ///< Remaining crossfade samples
    float crossfadeOldLevel_ = 0.0f;      ///< Captured output level before jump
    float crossfadeThresholdRatio_ = 1.0f; ///< Ratio threshold (> 1 semitone)

    // --- State ---
    int activePartials_ = 0;              ///< Number of active partials
    float lastOutputSample_ = 0.0f;       ///< For crossfade capture
    // renormCounter_ removed — MCF determinant=1 guarantees bounded amplitude
    bool prepared_ = false;               ///< Whether prepare() has been called
    bool frameLoaded_ = false;            ///< Whether loadFrame() has been called
    bool polyMode_ = false;               ///< Whether using polyphonic source-aware mode
    bool hasBandwidth_ = false;           ///< Whether any partial has non-zero bandwidth (SIMD path selection)

    // --- Noise generator for bandwidth enhancement ---
    uint32_t noiseState_ = 12345u;        ///< LCG state for noise generation

    // Per-partial 4th-order Chebyshev Type I LP filter at 500 Hz (1 dB ripple).
    // 2 biquad stages with shared coefficients and per-partial state.
    static constexpr size_t kNoiseLpNumStages = 2;
    std::array<BiquadCoefficients, kNoiseLpNumStages> noiseLpCoeffs_{};
    std::array<std::array<float, kMaxPartials>, kNoiseLpNumStages> noiseLpZ1_{};
    std::array<std::array<float, kMaxPartials>, kNoiseLpNumStages> noiseLpZ2_{};
    /// Power-normalization gain applied to the filtered noise so E[noise^2]=0.5
    /// (energy-preserving Loris bandwidth term). Measured in prepare(). (WI-8)
    float noiseNormGain_ = 1.0f;
};

} // namespace Krate::DSP

#ifdef _MSC_VER
#pragma warning(pop)
#endif
