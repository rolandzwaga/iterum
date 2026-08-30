// ==============================================================================
// Layer 2: DSP Processor - Sync Oscillator
// ==============================================================================
// Band-limited synchronized oscillator with hard sync, reverse sync, and
// phase advance sync modes. Composes a master PhaseAccumulator with a slave
// PhaseAccumulator and MinBlepTable::Residual for anti-aliased sync output.
//
// Architecture note: The slave uses a PhaseAccumulator (not PolyBlepOscillator).
// The naive waveform is evaluated directly at each sample, and ALL
// discontinuity corrections (both sync-induced and slave's natural wraps)
// go through the MinBLEP residual. This avoids the PolyBLEP/minBLEP
// double-correction problem that occurs when a sync reset places the slave
// near its phase wrap boundary.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/processBlock: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr, RAII)
// - Principle IX: Layer 2 (depends on Layer 0 + Layer 1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/018-oscillator-sync/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/polyblep_oscillator.h> // OscWaveform enum
#include <krate/dsp/primitives/minblep_table.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SyncMode Enumeration (FR-001)
// =============================================================================

/// @brief Synchronization mode for the SyncOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SyncMode : uint8_t {
    Hard = 0,        ///< Reset slave phase on master wrap (classic hard sync)
    Reverse = 1,     ///< Reverse slave direction on master wrap (soft sync)
    PhaseAdvance = 2 ///< Advance slave phase by fractional amount on master wrap
};

// =============================================================================
// SyncOscillator Class (FR-002)
// =============================================================================

/// @brief Band-limited synchronized oscillator (Layer 2 processor).
///
/// Composes a lightweight master PhaseAccumulator with a slave phase tracker
/// and a MinBlepTable::Residual for anti-aliased oscillator synchronization.
/// Uses MinBLEP for all discontinuity correction (sync resets and natural wraps).
///
/// Supports three sync modes:
/// - **Hard**: Classic hard sync. Slave phase is reset to master's fractional
///   position at each master wrap. MinBLEP correction at the discontinuity.
/// - **Reverse**: Slave direction is reversed at each master wrap. The
///   effective increment is lerped between forward and reversed based on
///   syncAmount (FR-021). MinBLAMP correction at the derivative discontinuity.
/// - **PhaseAdvance**: Slave phase is nudged toward alignment at each master
///   wrap, controlled by syncAmount. MinBLEP correction proportional to the
///   phase advance.
///
/// @par Ownership Model
/// Constructor takes a `const MinBlepTable*` (caller owns lifetime).
/// Multiple SyncOscillator instances can share one MinBlepTable (read-only
/// after prepare). Each instance maintains its own Residual buffer.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe.
///
/// @par Usage
/// @code
/// MinBlepTable table;
/// table.prepare();
///
/// SyncOscillator osc(&table);
/// osc.prepare(44100.0);
/// osc.setMasterFrequency(220.0f);
/// osc.setSlaveFrequency(660.0f);
/// osc.setSlaveWaveform(OscWaveform::Sawtooth);
/// osc.setSyncMode(SyncMode::Hard);
///
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class SyncOscillator {
public:
    // =========================================================================
    // Constructor (FR-002)
    // =========================================================================

    /// @brief Construct with a pointer to a shared MinBlepTable.
    /// @param table Pointer to prepared MinBlepTable (caller owns lifetime).
    ///        May be nullptr; prepare() will validate before use.
    explicit SyncOscillator(const MinBlepTable* table = nullptr) noexcept
        : table_(table) {
    }

    ~SyncOscillator() = default;

    SyncOscillator(const SyncOscillator&) = default;
    SyncOscillator& operator=(const SyncOscillator&) = default;
    SyncOscillator(SyncOscillator&&) noexcept = default;
    SyncOscillator& operator=(SyncOscillator&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-003, FR-004)
    // =========================================================================

    /// @brief Initialize for the given sample rate. NOT real-time safe.
    inline void prepare(double sampleRate) noexcept {
        // FR-025: Validate table pointer
        if (table_ == nullptr || !table_->isPrepared()) {
            prepared_ = false;
            return;
        }

        sampleRate_ = static_cast<float>(sampleRate);

        // Initialize master phase accumulator
        masterPhase_.reset();
        masterPhase_.increment = 0.0;

        // Initialize slave phase accumulator
        slavePhase_.reset();
        slavePhase_.increment = 0.0;

        // Initialize residual buffer
        residual_ = MinBlepTable::Residual(*table_);

        // Reset state
        masterFrequency_ = 0.0f;
        masterIncrement_ = 0.0f;
        slaveFrequency_ = 440.0f;
        slaveWaveform_ = OscWaveform::Sine;
        slavePulseWidth_ = 0.5f;
        syncMode_ = SyncMode::Hard;
        syncAmount_ = 1.0f;
        reversed_ = false;
        prepared_ = true;
    }

    /// @brief Reset phase and state without changing configuration.
    inline void reset() noexcept {
        masterPhase_.reset();
        slavePhase_.reset();
        residual_.reset();
        reversed_ = false;
    }

    // =========================================================================
    // Parameter Setters (FR-005 through FR-010)
    // =========================================================================

    /// @brief Set the master oscillator frequency in Hz.
    /// Clamped to [0, sampleRate/2). NaN/Inf treated as 0.0.
    inline void setMasterFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = 0.0f;
        }
        if (hz < 0.0f) {
            hz = 0.0f;
        }
        const float nyquist = sampleRate_ * 0.5f;
        if (hz >= nyquist) {
            hz = nyquist - 0.001f;
        }
        masterFrequency_ = hz;
        masterIncrement_ = (sampleRate_ > 0.0f) ? (hz / sampleRate_) : 0.0f;
        masterPhase_.increment = static_cast<double>(masterIncrement_);
    }

    /// @brief Set the slave oscillator frequency in Hz.
    /// Clamped to [0, sampleRate/2). NaN/Inf treated as 0.0.
    inline void setSlaveFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = 0.0f;
        }
        if (hz < 0.0f) {
            hz = 0.0f;
        }
        const float nyquist = sampleRate_ * 0.5f;
        if (hz >= nyquist) {
            hz = nyquist - 0.001f;
        }
        slaveFrequency_ = hz;
        slavePhase_.increment = calculatePhaseIncrement(slaveFrequency_, sampleRate_);
    }

    /// @brief Set the slave oscillator waveform.
    inline void setSlaveWaveform(OscWaveform waveform) noexcept {
        slaveWaveform_ = waveform;
    }

    /// @brief Set the active sync mode.
    inline void setSyncMode(SyncMode mode) noexcept {
        syncMode_ = mode;
    }

    /// @brief Set sync intensity [0.0, 1.0].
    /// 0.0 = no sync (slave runs freely). 1.0 = full sync.
    inline void setSyncAmount(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) {
            return;
        }
        syncAmount_ = (amount < 0.0f) ? 0.0f : ((amount > 1.0f) ? 1.0f : amount);
    }

    /// @brief Set pulse width for the Pulse slave waveform.
    /// Clamped to [0.01, 0.99].
    inline void setSlavePulseWidth(float width) noexcept {
        slavePulseWidth_ = (width < 0.01f) ? 0.01f : ((width > 0.99f) ? 0.99f : width);
    }

    // =========================================================================
    // Processing (FR-011, FR-012)
    // =========================================================================

    /// @brief Generate and return one sample of sync oscillator output.
    /// Real-time safe: no allocation, no exceptions, no blocking, no I/O.
    ///
    /// Pipeline:
    /// 1. Advance master phase, detect wrap
    /// 2. Advance slave phase, detect natural wrap
    /// 3. If slave naturally wrapped: stamp minBLEP for wrap discontinuity
    /// 4. If master wrapped: sync processing (compare post-advance phases,
    ///    reset slave if needed, stamp minBLEP for sync discontinuity)
    /// 5. Evaluate naive waveform at current slave phase
    /// 6. Output = naive + residual correction
    /// 7. Sanitize
    ///
    /// The slave advances BEFORE sync processing so that at integer ratios
    /// (e.g., 1:1), the slave naturally reaches the correct phase and the
    /// sync is a no-op.
    [[nodiscard]] inline float process() noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Step 1: Advance master phase and detect wrap (FR-013, FR-014)
        bool masterWrapped = masterPhase_.advance();

        // Step 2: Advance slave phase and detect natural wrap
        // FR-021: In Reverse mode with reversed_ flag set, the effective
        // increment is lerped between the forward and reversed increments
        // based on syncAmount: effectiveInc = lerp(+inc, -inc, syncAmount)
        //   = inc * (1 - 2 * syncAmount)
        // At syncAmount=0: fully forward. At 0.5: stopped. At 1.0: fully reversed.
        bool slaveWrapped = false;
        if (syncMode_ == SyncMode::Reverse && reversed_) {
            double effInc = slavePhase_.increment
                * (1.0 - 2.0 * static_cast<double>(syncAmount_));
            slavePhase_.phase += effInc;
            if (slavePhase_.phase >= 1.0) {
                slavePhase_.phase -= 1.0;
                slaveWrapped = true;
            } else if (slavePhase_.phase < 0.0) {
                slavePhase_.phase += 1.0;
                slaveWrapped = true;
            }
        } else {
            slaveWrapped = slavePhase_.advance();
        }

        // Step 3: If slave naturally wrapped, stamp minBLEP for wrap
        if (slaveWrapped) {
            double ssOff = 0.0;
            if (syncMode_ == SyncMode::Reverse && reversed_) {
                double effInc = slavePhase_.increment
                    * (1.0 - 2.0 * static_cast<double>(syncAmount_));
                if (effInc > 0.0) {
                    ssOff = subsamplePhaseWrapOffset(
                        slavePhase_.phase, effInc);
                } else if (effInc < 0.0) {
                    double overshoot = 1.0 - slavePhase_.phase;
                    ssOff = overshoot / std::abs(effInc);
                }
            } else {
                ssOff = subsamplePhaseWrapOffset(
                    slavePhase_.phase, slavePhase_.increment);
            }
            float wrapOffset = static_cast<float>(ssOff);
            if (wrapOffset < 0.0f) wrapOffset = 0.0f;
            if (wrapOffset >= 1.0f) wrapOffset = 1.0f - 1e-7f;

            float wrapDiscontinuity = computeWrapDiscontinuity(
                slaveWaveform_, slavePulseWidth_);

            if (std::abs(wrapDiscontinuity) > 1e-7f) {
                residual_.addBlep(wrapOffset, wrapDiscontinuity);
            }
        }

        // Step 4: Sync event processing if master wrapped
        // Done AFTER slave advance so that at integer ratios the slave
        // naturally reaches the correct phase without needing a sync reset.
        // Reverse mode always processes sync events (direction toggle is
        // unconditional per FR-019; syncAmount only controls increment
        // blending per FR-021). Hard and PhaseAdvance gate on syncAmount.
        if (masterWrapped) {
            double subsampleOffset = subsamplePhaseWrapOffset(
                masterPhase_.phase, masterPhase_.increment);

            float ssOffset = static_cast<float>(subsampleOffset);
            if (ssOffset < 0.0f) ssOffset = 0.0f;
            if (ssOffset >= 1.0f) ssOffset = 1.0f - 1e-7f;

            switch (syncMode_) {
                case SyncMode::Hard:
                    if (syncAmount_ > 0.0f) processHardSync(ssOffset);
                    break;
                case SyncMode::Reverse:
                    processReverseSync(ssOffset);
                    break;
                case SyncMode::PhaseAdvance:
                    if (syncAmount_ > 0.0f) processPhaseAdvanceSync(ssOffset);
                    break;
            }
        }

        // Step 5: Evaluate naive waveform at current slave phase
        float phase = static_cast<float>(slavePhase_.phase);
        float naiveSample = evaluateWaveform(slaveWaveform_, phase, slavePulseWidth_);

        // Step 6: Apply residual correction
        float output = naiveSample + residual_.consume();

        // Step 7: Sanitize output (FR-036)
        return sanitize(output);
    }

    /// @brief Generate numSamples into the provided buffer.
    /// Result is identical to calling process() that many times.
    inline void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Evaluate the naive (uncorrected) slave waveform at arbitrary phase.
    [[nodiscard]] static inline float evaluateWaveform(
        OscWaveform wf, float phase, float pulseWidth) noexcept {
        switch (wf) {
            case OscWaveform::Sine:
                return std::sin(kTwoPi * phase);
            case OscWaveform::Sawtooth:
                return 2.0f * phase - 1.0f;
            case OscWaveform::Square:
                return (phase < 0.5f) ? 1.0f : -1.0f;
            case OscWaveform::Pulse:
                return (phase < pulseWidth) ? 1.0f : -1.0f;
            case OscWaveform::Triangle:
                return (phase < 0.5f)
                    ? (4.0f * phase - 1.0f)
                    : (3.0f - 4.0f * phase);
            default:
                return 0.0f;
        }
    }

    /// @brief Evaluate the waveform derivative at arbitrary phase.
    [[nodiscard]] static inline float evaluateWaveformDerivative(
        OscWaveform wf, float phase, float pulseWidth) noexcept {
        switch (wf) {
            case OscWaveform::Sine:
                return kTwoPi * std::cos(kTwoPi * phase);
            case OscWaveform::Sawtooth:
                return 2.0f;
            case OscWaveform::Square:
                return 0.0f;
            case OscWaveform::Pulse:
                return 0.0f;
            case OscWaveform::Triangle:
                return (phase < 0.5f) ? 4.0f : -4.0f;
            default:
                return 0.0f;
        }
    }

    /// @brief Compute waveform step discontinuity at the phase wrap point.
    [[nodiscard]] static inline float computeWrapDiscontinuity(
        OscWaveform wf, float pulseWidth) noexcept {
        float valueBefore = evaluateWaveform(wf, 1.0f - 1e-6f, pulseWidth);
        float valueAfter = evaluateWaveform(wf, 0.0f, pulseWidth);
        return valueAfter - valueBefore;
    }

    /// @brief Branchless output sanitization (FR-036).
    [[nodiscard]] static inline float sanitize(float x) noexcept {
        const auto bits = detail::opaqueFloatBits(x);  // fast-math-immune read
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                           ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    /// @brief Process hard sync event (FR-015 through FR-018).
    ///
    /// Uses the slave's POST-ADVANCE phase as the reference point.
    /// At integer frequency ratios, the slave naturally reaches the correct
    /// phase, making the sync a no-op. At non-integer ratios, the phase
    /// difference drives the sync correction.
    ///
    /// @param subsampleOffset Fraction of sample elapsed since master wrap [0, 1)
    inline void processHardSync(float subsampleOffset) noexcept {
        double currentSlavePhase = slavePhase_.phase;
        double slaveInc = slavePhase_.increment;
        double masterInc = static_cast<double>(masterIncrement_);

        // FR-015: Compute synced phase from the Eli Brandt formula
        double masterFractionalPhase = masterPhase_.phase;
        double syncedPhase = 0.0;
        if (masterInc > 0.0) {
            syncedPhase = masterFractionalPhase * (slaveInc / masterInc);
        }
        syncedPhase = wrapPhase(syncedPhase);

        // FR-016: Compute shortest-path phase difference (wrap-aware)
        double phaseDiff = syncedPhase - currentSlavePhase;
        if (phaseDiff > 0.5) phaseDiff -= 1.0;
        if (phaseDiff < -0.5) phaseDiff += 1.0;

        // Apply syncAmount interpolation
        double effectivePhase = wrapPhase(
            currentSlavePhase + static_cast<double>(syncAmount_) * phaseDiff);

        // FR-017, FR-018: Compute discontinuity and apply minBLEP
        float valueBefore = evaluateWaveform(
            slaveWaveform_, static_cast<float>(currentSlavePhase), slavePulseWidth_);
        float valueAfter = evaluateWaveform(
            slaveWaveform_, static_cast<float>(effectivePhase), slavePulseWidth_);

        float discontinuity = valueAfter - valueBefore;

        if (std::abs(discontinuity) > 1e-7f) {
            residual_.addBlep(subsampleOffset, discontinuity);
        }

        // Reset slave phase
        slavePhase_.phase = effectivePhase;
    }

    /// @brief Process reverse sync event (FR-019 through FR-021a).
    inline void processReverseSync(float subsampleOffset) noexcept {
        reversed_ = !reversed_;

        double currentSlavePhase = slavePhase_.phase;

        float derivative = evaluateWaveformDerivative(
            slaveWaveform_, static_cast<float>(currentSlavePhase), slavePulseWidth_);

        double slaveInc = slavePhase_.increment;
        float blampAmplitude = syncAmount_ * 2.0f * derivative * static_cast<float>(slaveInc);

        if (std::abs(blampAmplitude) > 1e-7f) {
            residual_.addBlamp(subsampleOffset, blampAmplitude);
        }
    }

    /// @brief Process phase advance sync event (FR-022 through FR-024).
    inline void processPhaseAdvanceSync(float subsampleOffset) noexcept {
        double currentSlavePhase = slavePhase_.phase;
        float valueBefore = evaluateWaveform(
            slaveWaveform_, static_cast<float>(currentSlavePhase), slavePulseWidth_);

        double masterFractionalPhase = masterPhase_.phase;
        double slaveInc = slavePhase_.increment;
        double masterInc = static_cast<double>(masterIncrement_);

        double syncedPhase = 0.0;
        if (masterInc > 0.0) {
            syncedPhase = masterFractionalPhase * (slaveInc / masterInc);
        }
        syncedPhase = wrapPhase(syncedPhase);

        double phaseAdvance = static_cast<double>(syncAmount_) *
            (syncedPhase - currentSlavePhase);
        double newPhase = wrapPhase(currentSlavePhase + phaseAdvance);

        float valueAfter = evaluateWaveform(
            slaveWaveform_, static_cast<float>(newPhase), slavePulseWidth_);

        float discontinuity = valueAfter - valueBefore;

        if (std::abs(discontinuity) > 1e-7f) {
            residual_.addBlep(subsampleOffset, discontinuity);
        }

        slavePhase_.phase = newPhase;
    }

    // =========================================================================
    // Internal State
    // =========================================================================

    const MinBlepTable* table_ = nullptr;
    MinBlepTable::Residual residual_;
    PhaseAccumulator masterPhase_;
    PhaseAccumulator slavePhase_;

    float sampleRate_ = 0.0f;
    float masterFrequency_ = 0.0f;
    float masterIncrement_ = 0.0f;
    float slaveFrequency_ = 440.0f;

    OscWaveform slaveWaveform_ = OscWaveform::Sine;
    float slavePulseWidth_ = 0.5f;
    SyncMode syncMode_ = SyncMode::Hard;
    float syncAmount_ = 1.0f;

    bool reversed_ = false;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
