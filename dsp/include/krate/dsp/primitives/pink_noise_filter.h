// ==============================================================================
// Layer 1: DSP Primitive - Pink Noise Filter
// ==============================================================================
// Paul Kellet's pink noise filter for converting white noise to pink noise.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle XII: Test-First Development
//
// Reference: https://www.firstpr.com.au/dsp/pink-noise/
// Spec: specs/023-noise-oscillator/spec.md (RF-001, RF-002)
// ==============================================================================

#pragma once

#include <cmath>

namespace Krate {
namespace DSP {

/// @brief Paul Kellet's pink noise filter.
///
/// Converts white noise to pink noise (-3dB/octave spectral rolloff).
/// Uses a 7-state recursive filter for excellent accuracy with minimal CPU.
///
/// @par Algorithm
/// Filter coefficients from Paul Kellet's "pink noise generation" article.
/// Accuracy: +/- 0.05dB from 9.2Hz to Nyquist at 44.1kHz.
///
/// @par Sample rate
/// Kellet's published coefficients are for 44.1 kHz. They are NOT rate-portable
/// as written: each stage is a one-pole whose coefficient fixes its corner in
/// NORMALISED frequency, so at 96 kHz every corner sits 2.18x higher in Hz and
/// the 1/f region moves up with it. This header previously claimed the opposite
/// ("coefficients work across all sample rates"); measured through three
/// fixed-Hz resonators at 70/140/260 Hz, the error was +2.14 dB at 96 kHz and
/// +4.00 dB at 192 kHz.
///
/// prepare() therefore maps each stage to the running rate, preserving its time
/// constant in SECONDS and its DC gain: for a pole a at reference rate 44.1 kHz,
///     a' = sign(a) * |a| ^ (44100 / fs),      g' = g * (1 - a') / (1 - a)
/// At 44.1 kHz the exponent is exactly 1, so a' == a and g' == g and Kellet's
/// published numbers are reproduced EXACTLY (RF-002). A default-constructed
/// filter that never sees prepare() also keeps them, so existing consumers are
/// unaffected.
///
/// @par Reference
/// https://www.firstpr.com.au/dsp/pink-noise/
///
/// @par Layer
/// Layer 1 (primitives/) - depends only on Layer 0
///
/// @par Real-Time Safety
/// process() is fully real-time safe (noexcept, no allocation)
///
/// @par Usage
/// @code
/// PinkNoiseFilter filter;
/// Xorshift32 rng(12345);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     float white = rng.nextFloat();  // White noise in [-1, 1]
///     float pink = filter.process(white);  // Pink noise in [-1, 1]
///     output[i] = pink;
/// }
/// @endcode
class PinkNoiseFilter {
public:
    /// @brief Process one white noise sample through the filter.
    ///
    /// Applies Paul Kellet's 7-stage recursive filter to convert white noise
    /// to pink noise (-3dB/octave slope).
    ///
    /// @param white Input white noise sample (typically [-1, 1])
    /// @return Pink noise sample (bounded to [-1, 1])
    ///
    /// @note RF-002: Exact Paul Kellet coefficients preserved
    [[nodiscard]] float process(float white) noexcept {
        // Paul Kellet's filter, its poles mapped to the running sample rate by
        // prepare(). At 44.1 kHz these ARE the published coefficients (RF-002);
        // see the class note for the mapping.
        b0_ = pole_[0] * b0_ + white * gain_[0];
        b1_ = pole_[1] * b1_ + white * gain_[1];
        b2_ = pole_[2] * b2_ + white * gain_[2];
        b3_ = pole_[3] * b3_ + white * gain_[3];
        b4_ = pole_[4] * b4_ + white * gain_[4];
        b5_ = pole_[5] * b5_ + white * gain_[5];

        float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        b6_ = white * 0.115926f;

        // Normalize output to stay within [-1, 1] range
        // The filter has peak gain of approximately 5.0, so we use a conservative factor
        // and clamp to ensure we never exceed the range
        // (RF-002: exact normalization factor 0.2f preserved)
        float normalized = pink * 0.2f;
        return (normalized > 1.0f) ? 1.0f : ((normalized < -1.0f) ? -1.0f : normalized);
    }

    /// @brief Re-derive the stage coefficients for a sample rate.
    ///
    /// Idempotent and allocation-free, so it is safe to call from a consumer's
    /// prepare(). Passing 44100 -- or never calling it at all -- leaves Kellet's
    /// published coefficients exactly in place.
    ///
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz)
    void prepare(float sampleRate) noexcept {
        const double fs    = sampleRate > 1.0f ? static_cast<double>(sampleRate) : 1.0;
        const double ratio = static_cast<double>(kReferenceRate) / fs;
        for (int i = 0; i < kNumStages; ++i) {
            const double aRef = static_cast<double>(kReferencePole[i]);
            // Preserve the stage's time constant in seconds. The sign is carried
            // separately rather than fed to pow(): stage 5's pole is NEGATIVE (a
            // Nyquist-region pole), so |a|^ratio with the sign restored keeps it
            // at angle pi while its radius maps like every other stage's.
            const double mag = std::pow(std::fabs(aRef), ratio);
            const double a   = (aRef < 0.0) ? -mag : mag;
            pole_[i] = static_cast<float>(a);
            // Preserve the stage's DC gain g/(1-a): that is what holds the
            // cascade's low-frequency shape fixed in Hz.
            gain_[i] = static_cast<float>(static_cast<double>(kReferenceGain[i]) *
                                          (1.0 - a) / (1.0 - aRef));
        }
    }

    /// @brief Reset filter state to zero.
    ///
    /// Clears all internal state variables, causing the filter to restart
    /// from a clean state. Useful when starting a new noise sequence or
    /// when switching noise colors.
    void reset() noexcept {
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }

private:
    static constexpr int   kNumStages     = 6;
    static constexpr float kReferenceRate = 44100.0f;
    /// Kellet's published poles. Stage 5's is negative by design.
    static constexpr float kReferencePole[kNumStages] = {
        0.99886f, 0.99332f, 0.96900f, 0.86650f, 0.55000f, -0.7616f};
    /// Kellet's published input gains. Stage 5's is negative by design: the
    /// original line reads `b5_ = -0.7616f * b5_ - white * 0.0168980f`.
    static constexpr float kReferenceGain[kNumStages] = {
        0.0555179f, 0.0750759f, 0.1538520f, 0.3104856f, 0.5329522f, -0.0168980f};

    /// Defaulted to the published values, so a filter that never sees prepare()
    /// behaves exactly as it did before this class became rate-aware.
    float pole_[kNumStages] = {0.99886f, 0.99332f, 0.96900f, 0.86650f, 0.55000f, -0.7616f};
    float gain_[kNumStages] = {
        0.0555179f, 0.0750759f, 0.1538520f, 0.3104856f, 0.5329522f, -0.0168980f};

    float b0_ = 0.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float b3_ = 0.0f;
    float b4_ = 0.0f;
    float b5_ = 0.0f;
    float b6_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
