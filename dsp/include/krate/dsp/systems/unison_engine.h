// ==============================================================================
// Layer 3: System Component - Unison Engine
// ==============================================================================
// Multi-voice detuned oscillator with stereo spread, inspired by the Roland
// JP-8000 supersaw (Adam Szabo analysis). Composes up to 16 PolyBlepOscillator
// instances into a rich, harmonically dense unison sound.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, no allocations in process())
// - Principle III: Modern C++ (C++20, std::array, constexpr, [[nodiscard]])
// - Principle IX:  Layer 3 (depends on Layer 0 + Layer 1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/020-supersaw-unison-engine/spec.md
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/stereo_output.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/polyblep_oscillator.h>

// Standard library
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// UnisonEngine (FR-002 through FR-031)
// =============================================================================

/// @brief Multi-voice detuned oscillator with stereo spread (Layer 3 system).
///
/// Composes up to 16 PolyBlepOscillator instances into a supersaw/unison
/// engine with non-linear detune curve (JP-8000 inspired), constant-power
/// stereo panning, equal-power center/outer blend, and gain compensation.
///
/// @par Thread Safety
/// Single-threaded ownership model. All methods must be called from the
/// same thread (typically the audio thread). No internal synchronization.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe: no allocation,
/// no exceptions, no blocking, no I/O.
///
/// @par Memory
/// All 16 oscillators are pre-allocated as a fixed-size array. No heap
/// allocation occurs at any point. Total instance size < 2048 bytes.
class UnisonEngine {
public:
    // =========================================================================
    // Constants (FR-003)
    // =========================================================================

    static constexpr size_t kMaxVoices = 16;

    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    UnisonEngine() noexcept = default;

    /// @brief Initialize all oscillators and assign random phases.
    /// NOT real-time safe.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset oscillator phases to initial random values.
    /// Preserves all configured parameters.
    /// Produces bit-identical output after each reset() call.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-006 through FR-011)
    // =========================================================================

    /// @brief Set number of active unison voices. Clamped to [1, 16].
    void setNumVoices(size_t count) noexcept;

    /// @brief Set detune spread amount. Clamped to [0, 1]. NaN/Inf ignored.
    void setDetune(float amount) noexcept;

    /// @brief Set stereo panning width. Clamped to [0, 1]. NaN/Inf ignored.
    void setStereoSpread(float spread) noexcept;

    /// @brief Set waveform for all voices simultaneously.
    void setWaveform(OscWaveform waveform) noexcept;

    /// @brief Set base frequency in Hz. NaN/Inf ignored.
    void setFrequency(float hz) noexcept;

    /// @brief Set center/outer blend. Clamped to [0, 1]. NaN/Inf ignored.
    /// 0.0 = center only, 0.5 = equal, 1.0 = outer only.
    void setBlend(float blend) noexcept;

    // =========================================================================
    // Processing (FR-021, FR-022)
    // =========================================================================

    /// @brief Generate one stereo sample. Real-time safe.
    /// @return Stereo output with gain compensation and sanitization.
    [[nodiscard]] StereoOutput process() noexcept;

    /// @brief Generate numSamples into left/right buffers. Real-time safe.
    /// Result is bit-identical to calling process() in a loop.
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal constants
    // =========================================================================

    static constexpr float kMaxDetuneCents = 50.0f;
    static constexpr float kDetuneExponent = 1.7f;
    static constexpr uint32_t kPhaseSeed = 0x5EEDBA5E;

    // =========================================================================
    // Private methods
    // =========================================================================

    /// @brief Recompute voice layout: detune offsets, pan positions, blend
    /// weights, gain compensation, and oscillator frequencies.
    void computeVoiceLayout() noexcept;

    /// @brief Branchless output sanitization.
    /// NaN detection via bit manipulation, clamp to [-2.0, 2.0].
    [[nodiscard]] static float sanitize(float x) noexcept;

    // =========================================================================
    // Member variables
    // =========================================================================

    std::array<PolyBlepOscillator, kMaxVoices> oscillators_{};
    std::array<double, kMaxVoices> initialPhases_{};
    std::array<float, kMaxVoices> detuneOffsets_{};
    std::array<float, kMaxVoices> panPositions_{};
    std::array<float, kMaxVoices> leftGains_{};
    std::array<float, kMaxVoices> rightGains_{};
    std::array<float, kMaxVoices> blendWeights_{};

    size_t numVoices_ = 1;
    float detune_ = 0.0f;
    float stereoSpread_ = 0.0f;
    float blend_ = 0.5f;
    float frequency_ = 440.0f;
    float gainCompensation_ = 1.0f;
    float centerGain_ = 0.707f;
    float outerGain_ = 0.707f;
    double sampleRate_ = 0.0;
    Xorshift32 rng_{kPhaseSeed};
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void UnisonEngine::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Initialize all 16 oscillators
    for (auto& osc : oscillators_) {
        osc.prepare(sampleRate);
        osc.setWaveform(OscWaveform::Sawtooth);
    }

    // Reset parameters to defaults
    numVoices_ = 1;
    detune_ = 0.0f;
    stereoSpread_ = 0.0f;
    blend_ = 0.5f;
    frequency_ = 440.0f;

    // Compute blend gains for default blend=0.5
    auto [cGain, oGain] = equalPowerGains(blend_);
    centerGain_ = cGain;
    outerGain_ = oGain;

    // Seed RNG and generate initial phases
    rng_.seed(kPhaseSeed);
    for (size_t i = 0; i < kMaxVoices; ++i) {
        initialPhases_[i] = static_cast<double>(rng_.nextUnipolar());
    }

    // Apply initial phases to oscillators
    for (size_t i = 0; i < kMaxVoices; ++i) {
        oscillators_[i].resetPhase(initialPhases_[i]);
    }

    // Compute initial voice layout
    computeVoiceLayout();
}

inline void UnisonEngine::reset() noexcept {
    // Re-seed RNG and regenerate same phases (FR-005, FR-019)
    rng_.seed(kPhaseSeed);
    for (size_t i = 0; i < kMaxVoices; ++i) {
        initialPhases_[i] = static_cast<double>(rng_.nextUnipolar());
    }

    // Apply phases to oscillators
    for (size_t i = 0; i < kMaxVoices; ++i) {
        oscillators_[i].resetPhase(initialPhases_[i]);
    }
}

inline void UnisonEngine::setNumVoices(size_t count) noexcept {
    // Clamp to [1, kMaxVoices]
    if (count < 1) count = 1;
    if (count > kMaxVoices) count = kMaxVoices;
    numVoices_ = count;
    computeVoiceLayout();
}

inline void UnisonEngine::setDetune(float amount) noexcept {
    if (detail::isNaN(amount) || detail::isInf(amount)) return;
    detune_ = std::clamp(amount, 0.0f, 1.0f);
    computeVoiceLayout();
}

inline void UnisonEngine::setStereoSpread(float spread) noexcept {
    if (detail::isNaN(spread) || detail::isInf(spread)) return;
    stereoSpread_ = std::clamp(spread, 0.0f, 1.0f);
    computeVoiceLayout();
}

inline void UnisonEngine::setWaveform(OscWaveform waveform) noexcept {
    for (auto& osc : oscillators_) {
        osc.setWaveform(waveform);
    }
}

inline void UnisonEngine::setFrequency(float hz) noexcept {
    if (detail::isNaN(hz) || detail::isInf(hz)) return;
    frequency_ = hz;
    // Update all oscillator frequencies with their detune offsets
    for (size_t v = 0; v < numVoices_; ++v) {
        oscillators_[v].setFrequency(frequency_ * semitonesToRatio(detuneOffsets_[v]));
    }
}

inline void UnisonEngine::setBlend(float blend) noexcept {
    if (detail::isNaN(blend) || detail::isInf(blend)) return;
    blend_ = std::clamp(blend, 0.0f, 1.0f);
    auto [cGain, oGain] = equalPowerGains(blend_);
    centerGain_ = cGain;
    outerGain_ = oGain;
    computeVoiceLayout();
}

inline void UnisonEngine::computeVoiceLayout() noexcept {
    const size_t n = numVoices_;
    const size_t numPairs = n / 2;
    const bool hasCenter = (n % 2) != 0;

    // Gain compensation: 1/sqrt(N) (FR-020)
    gainCompensation_ = 1.0f / std::sqrt(static_cast<float>(n));

    // Initialize all arrays to zero
    detuneOffsets_.fill(0.0f);
    panPositions_.fill(0.0f);
    leftGains_.fill(0.0f);
    rightGains_.fill(0.0f);
    blendWeights_.fill(0.0f);

    // Voice layout: voices are arranged symmetrically
    // For odd N (e.g., 7): [P3- P2- P1- C P1+ P2+ P3+]
    // For even N (e.g., 8): [P4- P3- P2- P1- P1+ P2+ P3+ P4+]
    // Center index for odd: n/2

    // Compute group sizes for blend normalization (SC-005: constant power)
    // Center group: 1 voice (odd) or 2 voices (even, innermost pair)
    // Outer group: remaining voices
    size_t numCenter = 0;
    size_t numOuter = 0;
    if (n == 1) {
        numCenter = 1;
        numOuter = 0;
    } else if (hasCenter) {
        numCenter = 1;
        numOuter = n - 1;
    } else {
        numCenter = 2;  // Innermost pair is center group
        numOuter = n - 2;
    }

    // Normalize blend weights by group size so total power remains constant.
    // Without normalization: power = Nc*cGain^2 + No*oGain^2 (varies with blend)
    // With normalization: centerWeight = cGain * sqrt(N/Nc),
    //                     outerWeight  = oGain * sqrt(N/No)
    // Total power = Nc*(cGain*sqrt(N/Nc))^2 + No*(oGain*sqrt(N/No))^2
    //             = N*(cGain^2 + oGain^2) = N  (constant)
    const float centerWeight = (numCenter > 0)
        ? centerGain_ * std::sqrt(static_cast<float>(n) / static_cast<float>(numCenter))
        : 0.0f;
    const float outerWeight = (numOuter > 0)
        ? outerGain_ * std::sqrt(static_cast<float>(n) / static_cast<float>(numOuter))
        : 0.0f;

    if (hasCenter) {
        const size_t centerIdx = n / 2;
        detuneOffsets_[centerIdx] = 0.0f;
        panPositions_[centerIdx] = 0.0f;
        blendWeights_[centerIdx] = centerWeight;
    }

    for (size_t i = 1; i <= numPairs; ++i) {
        // Detune offset in cents using power curve (FR-012, FR-013)
        const float normalizedPairPos = static_cast<float>(i) / static_cast<float>(numPairs);
        const float offsetCents = kMaxDetuneCents * detune_ * std::pow(normalizedPairPos, kDetuneExponent);
        const float offsetSemitones = offsetCents / 100.0f;

        // Pan amount using linear spread (FR-016)
        const float panAmount = stereoSpread_ * normalizedPairPos;

        // Voice indices
        size_t idxDown, idxUp;
        if (hasCenter) {
            const size_t centerIdx = n / 2;
            idxDown = centerIdx - i;
            idxUp = centerIdx + i;
        } else {
            // Even: P1- is at n/2 - 1, P1+ is at n/2
            idxDown = numPairs - i;
            idxUp = numPairs + i - 1;
        }

        // Detune: up voice gets positive offset, down voice gets negative
        detuneOffsets_[idxUp] = offsetSemitones;
        detuneOffsets_[idxDown] = -offsetSemitones;

        // Pan: up voice pans right (+), down voice pans left (-)
        panPositions_[idxUp] = panAmount;
        panPositions_[idxDown] = -panAmount;

        // Blend weights: innermost pair of even count gets center weight
        if (!hasCenter && i == 1) {
            blendWeights_[idxUp] = centerWeight;
            blendWeights_[idxDown] = centerWeight;
        } else {
            blendWeights_[idxUp] = outerWeight;
            blendWeights_[idxDown] = outerWeight;
        }
    }

    // Compute constant-power pan gains for each voice (FR-015)
    for (size_t v = 0; v < n; ++v) {
        const float pan = panPositions_[v];
        const float angle = (pan + 1.0f) * kPi * 0.25f;  // (pan+1) * pi/4
        leftGains_[v] = std::cos(angle);
        rightGains_[v] = std::sin(angle);
    }

    // Update all oscillator frequencies (FR-010)
    for (size_t v = 0; v < n; ++v) {
        oscillators_[v].setFrequency(frequency_ * semitonesToRatio(detuneOffsets_[v]));
    }
}

[[nodiscard]] inline StereoOutput UnisonEngine::process() noexcept {
    // If not prepared, output silence
    if (sampleRate_ == 0.0) {
        return StereoOutput{0.0f, 0.0f};
    }

    float sumL = 0.0f;
    float sumR = 0.0f;

    for (size_t v = 0; v < numVoices_; ++v) {
        const float sample = oscillators_[v].process();
        const float weighted = sample * blendWeights_[v] * gainCompensation_;
        sumL += weighted * leftGains_[v];
        sumR += weighted * rightGains_[v];
    }

    // Sanitize output (FR-030)
    sumL = sanitize(sumL);
    sumR = sanitize(sumR);

    return StereoOutput{sumL, sumR};
}

inline void UnisonEngine::processBlock(float* left, float* right, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const auto out = process();
        left[i] = out.left;
        right[i] = out.right;
    }
}

[[nodiscard]] inline float UnisonEngine::sanitize(float x) noexcept {
    // NaN check via bit manipulation, read through the fast-math barrier
    // (a plain bit_cast is foldable under newer fast-math compilers)
    const auto bits = detail::opaqueFloatBits(x);
    const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
    x = isNan ? 0.0f : x;

    // Clamp to [-2.0, 2.0]
    x = (x < -2.0f) ? -2.0f : x;
    x = (x > 2.0f) ? 2.0f : x;
    return x;
}

} // namespace Krate::DSP
