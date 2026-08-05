// ==============================================================================
// Layer 1: DSP Primitive - Wavetable Oscillator
// ==============================================================================
// Real-time wavetable playback with automatic mipmap selection, cubic Hermite
// interpolation, and mipmap crossfading. Follows same interface pattern as
// PolyBlepOscillator for interchangeability in downstream components
// (FM Operator, PD Oscillator, Vector Mixer).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/processBlock: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IX: Layer 1 (depends on Layer 0 only: wavetable_data.h,
//   interpolation.h, phase_utils.h, math_constants.h, db_utils.h)
// - Principle XII: Test-First Development
//
// Reference: specs/016-wavetable-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// WavetableOscillator Class (FR-029 through FR-052)
// =============================================================================

/// @brief Wavetable playback oscillator with automatic mipmap selection.
///
/// Reads from a mipmapped WavetableData structure using cubic Hermite
/// interpolation. Automatically selects and crossfades between mipmap
/// levels based on playback frequency to prevent aliasing.
///
/// @par Memory Model
/// Holds a non-owning pointer to WavetableData. The caller is responsible
/// for ensuring the WavetableData outlives the oscillator.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe.
class WavetableOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-030, FR-031)
    // =========================================================================

    WavetableOscillator() noexcept = default;
    ~WavetableOscillator() = default;

    WavetableOscillator(const WavetableOscillator&) noexcept = default;
    WavetableOscillator& operator=(const WavetableOscillator&) noexcept = default;
    WavetableOscillator(WavetableOscillator&&) noexcept = default;
    WavetableOscillator& operator=(WavetableOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate.
    /// Resets all internal state. NOT real-time safe.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        frequency_ = 440.0f;
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;
        table_ = nullptr;
        phaseWrapped_ = false;
        phaseAcc_.reset();
        phaseAcc_.increment = 0.0;
        updatePhaseIncrement();
    }

    /// @brief Reset phase and modulation state without changing configuration.
    /// Preserves: frequency, sample rate, wavetable pointer.
    void reset() noexcept {
        phaseAcc_.reset();
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;
        phaseWrapped_ = false;
    }

    // =========================================================================
    // Parameter Setters (FR-032, FR-033)
    // =========================================================================

    /// @brief Set the wavetable data for playback (non-owning pointer).
    void setWavetable(const WavetableData* table) noexcept {
        table_ = table;
    }

    /// @brief Set the oscillator frequency in Hz, clamped to [0, sampleRate/2).
    void setFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = 0.0f;
        }
        const float nyquist = sampleRate_ * 0.5f;
        if (hz < 0.0f) {
            frequency_ = 0.0f;
        } else if (hz >= nyquist) {
            frequency_ = nyquist - 0.001f;
        } else {
            frequency_ = hz;
        }
        updatePhaseIncrement();
    }

    // =========================================================================
    // Processing (FR-034, FR-035, FR-035a, FR-036, FR-037, FR-038)
    // =========================================================================

    /// @brief Generate and return one sample of wavetable output.
    [[nodiscard]] float process() noexcept {
        // Early exit for null table
        if (table_ == nullptr || table_->numLevels() == 0) {
            phaseWrapped_ = phaseAcc_.advance();
            fmOffset_ = 0.0f;
            pmOffset_ = 0.0f;
            return 0.0f;
        }

        // Compute effective frequency with FM
        float effectiveFreq = frequency_ + fmOffset_;
        if (detail::isNaN(effectiveFreq) || detail::isInf(effectiveFreq)) {
            effectiveFreq = 0.0f;
        }
        const float nyquist = sampleRate_ * 0.5f;
        if (effectiveFreq < 0.0f) {
            effectiveFreq = 0.0f;
        } else if (effectiveFreq >= nyquist) {
            effectiveFreq = nyquist - 0.001f;
        }

        // Compute effective phase with PM
        float safePmOffset = (detail::isNaN(pmOffset_) || detail::isInf(pmOffset_))
                           ? 0.0f : pmOffset_;
        float pmNormalized = safePmOffset / kTwoPi;
        double effectivePhase = wrapPhase(phaseAcc_.phase + static_cast<double>(pmNormalized));

        // Select fractional mipmap level.
        // selectMipmapLevelFractional returns log2(ratio). Adding 1.0 ensures
        // floor(fracLevel) = ceil(log2(ratio)), so BOTH crossfade levels
        // (intLevel and intLevel+1) have all harmonics below Nyquist.
        float fracLevel = selectMipmapLevelFractional(effectiveFreq, sampleRate_, table_->tableSize());
        fracLevel += 1.0f;
        const size_t numLevels = table_->numLevels();

        // Clamp fracLevel to valid range
        const float maxLevel = static_cast<float>(numLevels - 1);
        if (fracLevel > maxLevel) fracLevel = maxLevel;
        if (fracLevel < 0.0f) fracLevel = 0.0f;

        // Determine crossfade
        auto intLevel = static_cast<size_t>(fracLevel);
        float frac = fracLevel - static_cast<float>(intLevel);

        float sample = 0.0f;

        if (frac < 0.05f || frac > 0.95f || intLevel >= numLevels - 1) {
            // Single lookup
            size_t level = (frac > 0.5f && intLevel < numLevels - 1) ? intLevel + 1 : intLevel;
            sample = readLevel(level, effectivePhase);
        } else {
            // Dual lookup with crossfade
            float s1 = readLevel(intLevel, effectivePhase);
            float s2 = readLevel(intLevel + 1, effectivePhase);
            sample = Interpolation::linearInterpolate(s1, s2, frac);
        }

        // Update phase increment for effective frequency (handles FM)
        phaseAcc_.increment = calculatePhaseIncrement(effectiveFreq, sampleRate_);
        phaseWrapped_ = phaseAcc_.advance();

        // Reset modulation offsets
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;

        return sanitize(sample);
    }

    /// @brief Generate numSamples at constant frequency.
    void processBlock(float* output, size_t numSamples) noexcept {
        if (numSamples == 0 || output == nullptr) return;

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Generate numSamples with per-sample FM input.
    void processBlock(float* output, const float* fmBuffer, size_t numSamples) noexcept {
        if (numSamples == 0 || output == nullptr) return;

        for (size_t i = 0; i < numSamples; ++i) {
            if (fmBuffer != nullptr) {
                fmOffset_ = fmBuffer[i];
            }
            output[i] = process();
        }
    }

    // =========================================================================
    // Phase Access (FR-039, FR-040, FR-041)
    // =========================================================================

    /// @brief Get the current phase position in [0, 1).
    [[nodiscard]] double phase() const noexcept {
        return phaseAcc_.phase;
    }

    /// @brief Check if the most recent process() call produced a phase wrap.
    [[nodiscard]] bool phaseWrapped() const noexcept {
        return phaseWrapped_;
    }

    /// @brief Force the phase to a specific position, wrapped to [0, 1).
    void resetPhase(double newPhase = 0.0) noexcept {
        phaseAcc_.phase = wrapPhase(newPhase);
    }

    // =========================================================================
    // Modulation Inputs (FR-042, FR-043)
    // =========================================================================

    /// @brief Add a phase modulation offset (radians, per-sample, non-accumulating).
    void setPhaseModulation(float radians) noexcept {
        pmOffset_ = radians;
    }

    /// @brief Add a frequency modulation offset (Hz, per-sample, non-accumulating).
    void setFrequencyModulation(float hz) noexcept {
        fmOffset_ = hz;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Read a sample from a single mipmap level using cubic Hermite.
    [[nodiscard]] float readLevel(size_t level, double normalizedPhase) const noexcept {
        const float* levelData = table_->getLevel(level);
        if (levelData == nullptr) return 0.0f;

        const size_t tableSize = table_->tableSize();
        double tablePhase = normalizedPhase * static_cast<double>(tableSize);
        auto intPhase = static_cast<size_t>(tablePhase);
        auto fracPhase = static_cast<float>(tablePhase - static_cast<double>(intPhase));

        // Clamp intPhase to valid range (should be [0, tableSize-1])
        if (intPhase >= tableSize) intPhase = tableSize - 1;

        // Branchless cubic Hermite using guard samples
        const float* p = levelData + intPhase;
        return Interpolation::cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], fracPhase);
    }

    /// @brief Update phase increment from current frequency and sample rate.
    void updatePhaseIncrement() noexcept {
        phaseAcc_.increment = calculatePhaseIncrement(frequency_, sampleRate_);
    }

    /// @brief Branchless output sanitization (FR-051).
    [[nodiscard]] static float sanitize(float x) noexcept {
        const auto bits = detail::opaqueFloatBits(x);  // fast-math-immune read
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    PhaseAccumulator phaseAcc_;
    float sampleRate_ = 0.0f;
    float frequency_ = 440.0f;
    float fmOffset_ = 0.0f;
    float pmOffset_ = 0.0f;
    const WavetableData* table_ = nullptr;
    bool phaseWrapped_ = false;
};

} // namespace DSP
} // namespace Krate
