// ==============================================================================
// Layer 1: DSP Primitive - Chaos Attractor Waveshaper
// ==============================================================================
// Time-varying waveshaping using chaos attractor dynamics.
//
// Feature: 104-chaos-waveshaper
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 1: primitives/oversampler.h (Oversampler<2, 1> for anti-aliased waveshaping)
//   - Layer 0: core/db_utils.h (flushDenormal, isNaN, isInf)
//   - Layer 0: core/sigmoid.h (Sigmoid::tanhVariable)
//   - stdlib: <cstdint>, <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library / Layer 1 primitives)
// - Principle X: DSP Constraints (internal 2x oversampling for anti-aliasing)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/104-chaos-waveshaper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/oversampler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// ChaosModel Enumeration (FR-005 to FR-008)
// =============================================================================

/// @brief Available chaos attractor models.
///
/// Each model has distinct mathematical character:
/// - Lorenz: Classic 3D continuous attractor with swirling, unpredictable behavior
/// - Rossler: Smoother 3D continuous attractor with spiraling patterns
/// - Chua: Double-scroll circuit attractor with bi-modal jumps
/// - Henon: 2D discrete map with sharp, rhythmic transitions
///
/// @note All models use standard "chaotic regime" parameters from literature.
enum class ChaosModel : uint8_t {
    Lorenz = 0,   ///< Lorenz system (sigma=10, rho=28, beta=8/3)
    Rossler = 1,  ///< Rossler system (a=0.2, b=0.2, c=5.7)
    Chua = 2,     ///< Chua circuit (alpha=15.6, beta=28, m0=-1.143, m1=-0.714)
    Henon = 3,    ///< Henon map (a=1.4, b=0.3)
    /// Aizawa system (a=0.95, b=0.7, c=0.6, d=3.5, e=0.25, f=0.1).
    /// Implemented by ChaosModSource (Layer 2) ONLY. ChaosWaveshaper's validator
    /// (setModel() below) still rejects it and substitutes Lorenz.
    Aizawa = 4
};

// =============================================================================
// ChaosWaveshaper Class
// =============================================================================

/// @brief Time-varying waveshaping using chaos attractor dynamics.
///
/// The attractor's normalized X component modulates the drive of a tanh-based
/// soft-clipper, producing distortion that evolves over time without external
/// modulation. Four chaos models provide different characters.
///
/// @par Features
/// - 4 chaos models: Lorenz, Rossler, Chua, Henon (FR-005 to FR-008)
/// - ChaosAmount parameter for dry/wet mixing (FR-010)
/// - AttractorSpeed for evolution rate control (FR-011)
/// - InputCoupling for signal-reactive behavior (FR-012, FR-025 to FR-027)
/// - Internal 2x oversampling for anti-aliasing (FR-034, FR-035)
/// - Automatic state reset on divergence (FR-018, FR-033)
/// - Sample-rate compensated integration (FR-019)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0 / Layer 1 primitives)
/// - Principle X: DSP Constraints (internal 2x oversampling)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Design Rationale
/// - Control-rate attractor updates (every 32 samples) for efficiency
/// - Internal 2x oversampling via Oversampler<2, 1> for anti-aliased waveshaping
///
/// @par Usage Example
/// @code
/// ChaosWaveshaper shaper;
/// shaper.prepare(44100.0, 512);
/// shaper.setModel(ChaosModel::Lorenz);
/// shaper.setChaosAmount(0.5f);
/// shaper.setAttractorSpeed(1.0f);
/// shaper.setInputCoupling(0.3f);
///
/// // Block processing (preferred - uses oversampling)
/// shaper.processBlock(buffer, numSamples);
///
/// // Single sample (for manual use, no oversampling)
/// float output = shaper.process(input);
/// @endcode
///
/// @see specs/104-chaos-waveshaper/spec.md
/// @see Oversampler for anti-aliasing implementation
class ChaosWaveshaper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinChaosAmount = 0.0f;
    static constexpr float kMaxChaosAmount = 1.0f;
    static constexpr float kDefaultChaosAmount = 0.5f;

    static constexpr float kMinAttractorSpeed = 0.01f;
    static constexpr float kMaxAttractorSpeed = 100.0f;
    static constexpr float kDefaultAttractorSpeed = 1.0f;

    static constexpr float kMinInputCoupling = 0.0f;
    static constexpr float kMaxInputCoupling = 1.0f;
    static constexpr float kDefaultInputCoupling = 0.0f;

    static constexpr float kMinXDrive = 0.0f;
    static constexpr float kMaxXDrive = 1.0f;
    static constexpr float kDefaultXDrive = 1.0f;   ///< Full X modulation by default

    static constexpr float kMinYDrive = 0.0f;
    static constexpr float kMaxYDrive = 1.0f;
    static constexpr float kDefaultYDrive = 0.0f;   ///< No Y modulation by default

    static constexpr float kMinSmoothness = 0.0f;
    static constexpr float kMaxSmoothness = 1.0f;
    static constexpr float kDefaultSmoothness = 0.0f; ///< No smoothing by default

    static constexpr float kMinDrive = 0.3f;    ///< Minimum waveshaping drive (near-linear)
    static constexpr float kMaxDrive = 10.0f;   ///< Maximum waveshaping drive (hard saturation)

    static constexpr size_t kControlRateInterval = 32;  ///< Samples between attractor updates

    // =========================================================================
    // Construction (FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes ChaosWaveshaper with:
    /// - Model: Lorenz
    /// - ChaosAmount: 0.5 (50% wet)
    /// - AttractorSpeed: 1.0 (nominal rate)
    /// - InputCoupling: 0.0 (no coupling)
    ///
    /// @note prepare() must be called before processing.
    ChaosWaveshaper() noexcept = default;

    // Non-copyable (contains Oversampler which is non-copyable)
    ChaosWaveshaper(const ChaosWaveshaper&) = delete;
    ChaosWaveshaper& operator=(const ChaosWaveshaper&) = delete;

    // Movable
    ChaosWaveshaper(ChaosWaveshaper&&) noexcept = default;
    ChaosWaveshaper& operator=(ChaosWaveshaper&&) noexcept = default;

    ~ChaosWaveshaper() = default;

    // =========================================================================
    // Initialization (FR-001, FR-002)
    // =========================================================================

    /// @brief Prepare for processing at given sample rate.
    ///
    /// Initializes attractor state, configures sample-rate-dependent
    /// integration timestep, and prepares oversampler buffers.
    ///
    /// @param sampleRate Sample rate in Hz (typically 44100-192000)
    /// @param maxBlockSize Maximum samples per processBlock() call
    /// @pre sampleRate >= 1000.0 (clamped internally if lower)
    /// @post Attractor initialized to stable starting conditions
    /// @post Oversampler prepared with maxBlockSize
    /// @note NOT real-time safe (allocates oversampler buffers)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset attractor to stable initial conditions.
    ///
    /// Reinitializes attractor state variables per current model.
    /// Also resets oversampler filter states.
    /// Configuration (model, parameters) is preserved.
    ///
    /// @post State variables reset to model-specific initial conditions
    /// @post Oversampler filter states cleared
    /// @post Configuration preserved
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set the chaos attractor model.
    ///
    /// @param model Chaos algorithm to use
    ///
    /// @note Invalid enum values default to Lorenz (FR-036)
    /// @note Model change resets attractor state to model-specific initial conditions.
    void setModel(ChaosModel model) noexcept;

    /// @brief Set the chaos amount (dry/wet mix).
    ///
    /// @param amount Amount in range [0.0, 1.0]
    ///               - 0.0 = bypass (output equals input)
    ///               - 1.0 = full chaos processing
    ///
    /// @note Values outside [0, 1] are clamped.
    void setChaosAmount(float amount) noexcept;

    /// @brief Set the attractor evolution speed.
    ///
    /// @param speed Speed multiplier in range [0.01, 100.0]
    ///              - 0.01 = very slow evolution
    ///              - 1.0 = nominal rate
    ///              - 100.0 = rapid fluctuation
    ///
    /// @note Values outside [0.01, 100] are clamped.
    void setAttractorSpeed(float speed) noexcept;

    /// @brief Set the input coupling amount.
    ///
    /// Determines how much input signal amplitude perturbs the attractor state.
    ///
    /// @param coupling Coupling in range [0.0, 1.0]
    ///                 - 0.0 = no coupling (chaos evolves independently)
    ///                 - 1.0 = full coupling (maximum input influence)
    ///
    /// @note Values outside [0, 1] are clamped.
    void setInputCoupling(float coupling) noexcept;

    /// @brief Set X-axis drive amount.
    ///
    /// Controls how much the attractor's X state variable modulates waveshaping drive.
    ///
    /// @param drive Drive in range [0.0, 1.0]
    ///              - 0.0 = X has no effect on drive (constant mid-range drive)
    ///              - 1.0 = full X modulation depth
    void setXDrive(float drive) noexcept;

    /// @brief Set Y-axis drive amount.
    ///
    /// Adds the attractor's Y state variable as a second dimension of drive modulation,
    /// producing more complex, multi-dimensional chaos character.
    ///
    /// @param drive Drive in range [0.0, 1.0]
    ///              - 0.0 = Y has no effect on drive
    ///              - 1.0 = full Y modulation depth
    void setYDrive(float drive) noexcept;

    /// @brief Set output smoothness.
    ///
    /// Applies one-pole lowpass filtering to the attractor's normalized output
    /// before it modulates drive, reducing jitter for smoother modulation.
    ///
    /// @param smoothness Smoothness in range [0.0, 1.0]
    ///                   - 0.0 = no smoothing (raw chaos)
    ///                   - 1.0 = heavy smoothing (very slow, gentle modulation)
    void setSmoothness(float smoothness) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current chaos model.
    [[nodiscard]] ChaosModel getModel() const noexcept;

    /// @brief Get the current chaos amount.
    [[nodiscard]] float getChaosAmount() const noexcept;

    /// @brief Get the current attractor speed.
    [[nodiscard]] float getAttractorSpeed() const noexcept;

    /// @brief Get the current input coupling.
    [[nodiscard]] float getInputCoupling() const noexcept;

    /// @brief Get the current X drive amount.
    [[nodiscard]] float getXDrive() const noexcept;

    /// @brief Get the current Y drive amount.
    [[nodiscard]] float getYDrive() const noexcept;

    /// @brief Get the current smoothness.
    [[nodiscard]] float getSmoothness() const noexcept;

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get configured sample rate.
    [[nodiscard]] double getSampleRate() const noexcept;

    /// @brief Get latency introduced by oversampling (in samples).
    /// @return 0 for default Economy/ZeroLatency mode
    [[nodiscard]] size_t latency() const noexcept;

    // =========================================================================
    // Processing (FR-003, FR-004, FR-020 to FR-024, FR-028 to FR-035)
    // =========================================================================

    /// @brief Process a single sample (no oversampling).
    ///
    /// Applies chaos-modulated waveshaping:
    /// output = lerp(input, waveshape(input, chaosModulatedDrive), chaosAmount)
    ///
    /// @param input Input sample (assumed normalized to [-1, 1])
    /// @return Chaos-modulated waveshaped output
    ///
    /// @note Real-time safe: no allocations, O(1) complexity
    /// @note NaN inputs are treated as 0.0 (FR-031)
    /// @note Infinity inputs are clamped to [-1, 1] (FR-032)
    /// @note ChaosAmount=0.0 returns input unchanged (FR-023)
    /// @note Does NOT use oversampling - prefer processBlock() for quality
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place with 2x oversampling.
    ///
    /// Applies chaos-modulated waveshaping with internal 2x oversampling
    /// for anti-aliasing (FR-034, FR-035).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note Real-time safe: no allocations (buffers pre-allocated in prepare)
    /// @note Preferred over process() for quality due to oversampling
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    /// @brief Internal struct holding attractor state variables.
    struct AttractorState {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;  // Not used by Henon (2D map)
    };

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Sanitize input for NaN/Inf (FR-031, FR-032)
    [[nodiscard]] float sanitizeInput(float input) const noexcept;

    /// @brief Apply waveshaping with current chaos-modulated drive (FR-020, FR-022)
    [[nodiscard]] float applyWaveshaping(float input) const noexcept;

    /// @brief Process a single sample internally (called at oversampled rate)
    [[nodiscard]] float processInternal(float input) noexcept;

    /// @brief Update attractor state - dispatches to model-specific update
    void updateAttractor() noexcept;

    /// @brief Check for divergence and reset if needed (FR-018, FR-033)
    void checkAndResetIfDiverged() noexcept;

    /// @brief Reset attractor to model-specific initial conditions
    void resetModelState() noexcept;

    // Model-specific update functions (FR-013 to FR-017)
    void updateLorenz() noexcept;
    void updateRossler() noexcept;
    void updateChua() noexcept;
    void updateHenon() noexcept;

    /// @brief Chua diode nonlinearity h(x) (FR-016)
    [[nodiscard]] static float chuaDiode(float x) noexcept;

    /// @brief Recompute smoothing coefficient from smoothness_ and sampleRate_
    void updateSmoothCoeff() noexcept;

    // =========================================================================
    // State
    // =========================================================================

    // Oversampler (FR-034, FR-035)
    Oversampler<2, 1> oversampler_;

    // Attractor state
    AttractorState state_;
    float normalizedX_ = 0.0f;   ///< Normalized attractor X [-1, 1]
    float normalizedY_ = 0.0f;   ///< Normalized attractor Y [-1, 1]
    float smoothedX_ = 0.0f;     ///< Smoothed X for drive modulation
    float smoothedY_ = 0.0f;     ///< Smoothed Y for asymmetry modulation
    float smoothCoeff_ = 0.0f;   ///< One-pole smoothing coefficient (precomputed)

    // Henon iteration phase. The map advances when this wraps; output is held
    // in between (see updateHenon).
    float henonPhase_ = 0.0f;

    // Input coupling envelope accumulator
    float inputEnvelopeAccum_ = 0.0f;
    size_t envelopeSampleCount_ = 0;

    // Control rate tracking
    int samplesUntilUpdate_ = 0;

    // Configuration
    ChaosModel model_ = ChaosModel::Lorenz;
    float chaosAmount_ = kDefaultChaosAmount;
    float attractorSpeed_ = kDefaultAttractorSpeed;
    float inputCoupling_ = kDefaultInputCoupling;
    float xDrive_ = kDefaultXDrive;
    float yDrive_ = kDefaultYDrive;
    float smoothness_ = kDefaultSmoothness;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Per-model parameters (set by resetModelState)
    float baseDt_ = 0.005f;            ///< Base integration timestep
    float safeBound_ = 50.0f;          ///< Safe bounds for state variables
    float normalizationFactor_ = 20.0f; ///< Factor to normalize X to [-1, 1]
    float perturbationScale_ = 0.1f;   ///< Input coupling perturbation scale
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void ChaosWaveshaper::prepare(double sampleRate, size_t maxBlockSize) noexcept {
    sampleRate_ = std::max(1000.0, sampleRate);

    // Prepare oversampler (FR-034, FR-035)
    oversampler_.prepare(sampleRate_, maxBlockSize);

    // Precompute smoothing coefficient for this sample rate
    updateSmoothCoeff();

    // Initialize attractor state
    resetModelState();
    samplesUntilUpdate_ = static_cast<int>(kControlRateInterval);

    prepared_ = true;
}

inline void ChaosWaveshaper::reset() noexcept {
    resetModelState();
    oversampler_.reset();
    samplesUntilUpdate_ = static_cast<int>(kControlRateInterval);
    inputEnvelopeAccum_ = 0.0f;
    envelopeSampleCount_ = 0;
}

inline void ChaosWaveshaper::setModel(ChaosModel model) noexcept {
    // FR-036: Invalid enum values default to Lorenz
    if (static_cast<uint8_t>(model) > static_cast<uint8_t>(ChaosModel::Henon)) {
        model = ChaosModel::Lorenz;
    }
    // Only reset attractor state when model actually changes —
    // setModel() may be called every sample via MorphEngine::processSameFamily()
    if (model != model_) {
        model_ = model;
        resetModelState();
    }
}

inline void ChaosWaveshaper::setChaosAmount(float amount) noexcept {
    chaosAmount_ = std::clamp(amount, kMinChaosAmount, kMaxChaosAmount);
}

inline void ChaosWaveshaper::setAttractorSpeed(float speed) noexcept {
    attractorSpeed_ = std::clamp(speed, kMinAttractorSpeed, kMaxAttractorSpeed);
}

inline void ChaosWaveshaper::setInputCoupling(float coupling) noexcept {
    inputCoupling_ = std::clamp(coupling, kMinInputCoupling, kMaxInputCoupling);
}

inline void ChaosWaveshaper::setXDrive(float drive) noexcept {
    xDrive_ = std::clamp(drive, kMinXDrive, kMaxXDrive);
}

inline void ChaosWaveshaper::setYDrive(float drive) noexcept {
    yDrive_ = std::clamp(drive, kMinYDrive, kMaxYDrive);
}

inline void ChaosWaveshaper::setSmoothness(float smoothness) noexcept {
    smoothness_ = std::clamp(smoothness, kMinSmoothness, kMaxSmoothness);
    updateSmoothCoeff();
}

inline ChaosModel ChaosWaveshaper::getModel() const noexcept {
    return model_;
}

inline float ChaosWaveshaper::getChaosAmount() const noexcept {
    return chaosAmount_;
}

inline float ChaosWaveshaper::getAttractorSpeed() const noexcept {
    return attractorSpeed_;
}

inline float ChaosWaveshaper::getInputCoupling() const noexcept {
    return inputCoupling_;
}

inline float ChaosWaveshaper::getXDrive() const noexcept {
    return xDrive_;
}

inline float ChaosWaveshaper::getYDrive() const noexcept {
    return yDrive_;
}

inline float ChaosWaveshaper::getSmoothness() const noexcept {
    return smoothness_;
}

inline bool ChaosWaveshaper::isPrepared() const noexcept {
    return prepared_;
}

inline double ChaosWaveshaper::getSampleRate() const noexcept {
    return sampleRate_;
}

inline size_t ChaosWaveshaper::latency() const noexcept {
    return oversampler_.getLatency();
}

inline float ChaosWaveshaper::sanitizeInput(float input) const noexcept {
    // FR-031: NaN treated as 0.0
    if (detail::isNaN(input)) {
        return 0.0f;
    }
    // FR-032: Infinity clamped to [-1, 1]
    if (detail::isInf(input)) {
        return input > 0.0f ? 1.0f : -1.0f;
    }
    return input;
}

inline float ChaosWaveshaper::applyWaveshaping(float input) const noexcept {
    // X controls drive amount: map smoothedX_ from [-1,1] to [0,1]
    const float driveT = smoothedX_ * 0.5f + 0.5f;
    // Quadratic mapping: concentrates perceptual variation in the useful range
    // At t=0: drive=0.3 (near-linear), t=0.5: drive≈2.7, t=1.0: drive=10 (hard clip)
    const float driveT2 = driveT * driveT;
    const float drive = kMinDrive + driveT2 * (kMaxDrive - kMinDrive);

    // Y controls waveshaping asymmetry: different pos/neg gains create even harmonics
    const float asymmetry = smoothedY_;

    if (std::abs(asymmetry) < 0.01f) {
        // Near-symmetric: standard tanh waveshaping
        return Sigmoid::tanhVariable(input, drive);
    }

    // Asymmetric: different drive for positive/negative half-cycles
    // Creates even harmonics (2nd, 4th) → dramatically different timbre
    const float posGain = drive * (1.0f + asymmetry * 0.8f);
    const float negGain = drive * (1.0f - asymmetry * 0.8f);

    if (input >= 0.0f) {
        return FastMath::fastTanh(std::max(0.0f, posGain) * input);
    } else {
        return FastMath::fastTanh(std::max(0.0f, negGain) * input);
    }
}

inline float ChaosWaveshaper::processInternal(float input) noexcept {
    input = sanitizeInput(input);

    // Accumulate input envelope for coupling (FR-025, FR-026)
    if (inputCoupling_ > 0.0f) {
        inputEnvelopeAccum_ += std::abs(input);
        envelopeSampleCount_++;
    }

    // Control-rate attractor update
    if (--samplesUntilUpdate_ <= 0) {
        // Apply input coupling perturbation before update
        if (inputCoupling_ > 0.0f && envelopeSampleCount_ > 0) {
            const float avgEnvelope = inputEnvelopeAccum_ / static_cast<float>(envelopeSampleCount_);
            const float perturbation = inputCoupling_ * avgEnvelope * perturbationScale_;
            state_.x += perturbation;
            state_.y += perturbation * 0.5f;
            // Reset accumulator
            inputEnvelopeAccum_ = 0.0f;
            envelopeSampleCount_ = 0;
        }

        updateAttractor();
        samplesUntilUpdate_ = static_cast<int>(kControlRateInterval);
    }

    // Per-sample smoothing of X and Y targets
    // This runs at audio rate for musically useful time constants
    const float targetX = normalizedX_ * xDrive_;
    const float targetY = normalizedY_ * yDrive_;
    if (smoothCoeff_ > 0.0f) {
        smoothedX_ += (1.0f - smoothCoeff_) * (targetX - smoothedX_);
        smoothedY_ += (1.0f - smoothCoeff_) * (targetY - smoothedY_);
    } else {
        smoothedX_ = targetX;
        smoothedY_ = targetY;
    }

    // FR-023: Bypass when chaosAmount=0
    if (chaosAmount_ <= 0.0f) {
        return input;
    }

    // Apply waveshaping and mix (FR-021, FR-024)
    const float shaped = applyWaveshaping(input);
    return std::lerp(input, shaped, chaosAmount_);
}

inline float ChaosWaveshaper::process(float input) noexcept {
    return processInternal(input);
}

inline void ChaosWaveshaper::processBlock(float* buffer, size_t numSamples) noexcept {
    if (!prepared_ || buffer == nullptr || numSamples == 0) {
        return;
    }

    // FR-023: Skip oversampling entirely for bypass
    if (chaosAmount_ <= 0.0f) {
        return;  // Input unchanged
    }

    // Process with 2x oversampling (FR-034, FR-035)
    oversampler_.process(buffer, numSamples, [this](float* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            data[i] = processInternal(data[i]);
        }
    });
}

inline void ChaosWaveshaper::updateAttractor() noexcept {
    switch (model_) {
        // Unreachable by construction: setModel() substitutes Lorenz for anything
        // above Henon, so model_ is never Aizawa here. Present solely for -Wswitch
        // exhaustiveness on the shared ChaosModel enum. Aizawa is a
        // ChaosModSource-only model (FR-034). Grouped label, not fallthrough.
        case ChaosModel::Aizawa:
        case ChaosModel::Lorenz:
            updateLorenz();
            break;
        case ChaosModel::Rossler:
            updateRossler();
            break;
        case ChaosModel::Chua:
            updateChua();
            break;
        case ChaosModel::Henon:
            updateHenon();
            break;
    }

    // Flush denormals (FR-030)
    state_.x = detail::flushDenormal(state_.x);
    state_.y = detail::flushDenormal(state_.y);
    state_.z = detail::flushDenormal(state_.z);

    // Check bounds and reset if diverged (FR-018, FR-033)
    checkAndResetIfDiverged();

    // Update normalized outputs (smoothing happens per-sample in processInternal)
    normalizedX_ = std::clamp(state_.x / normalizationFactor_, -1.0f, 1.0f);
    normalizedY_ = std::clamp(state_.y / normalizationFactor_, -1.0f, 1.0f);
}

inline void ChaosWaveshaper::checkAndResetIfDiverged() noexcept {
    // FR-033: Check for NaN/Inf
    const bool hasNaN = detail::isNaN(state_.x) || detail::isNaN(state_.y) || detail::isNaN(state_.z);
    const bool hasInf = detail::isInf(state_.x) || detail::isInf(state_.y) || detail::isInf(state_.z);

    // FR-018: Check bounds
    const bool outOfBounds =
        std::abs(state_.x) > safeBound_ ||
        std::abs(state_.y) > safeBound_ ||
        std::abs(state_.z) > safeBound_;

    if (hasNaN || hasInf || outOfBounds) {
        resetModelState();
    }
}

inline void ChaosWaveshaper::resetModelState() noexcept {
    switch (model_) {
        // Unreachable by construction: setModel() substitutes Lorenz for anything
        // above Henon, so model_ is never Aizawa here. Present solely for -Wswitch
        // exhaustiveness on the shared ChaosModel enum. Aizawa is a
        // ChaosModSource-only model (FR-034). Grouped label, not fallthrough.
        case ChaosModel::Aizawa:
        case ChaosModel::Lorenz:
            // Lorenz: sigma=10, rho=28, beta=8/3
            state_ = {1.0f, 1.0f, 1.0f};  // Near attractor
            baseDt_ = 0.005f;
            safeBound_ = 50.0f;
            normalizationFactor_ = 20.0f;
            perturbationScale_ = 0.1f;
            break;

        case ChaosModel::Rossler:
            // Rossler: a=0.2, b=0.2, c=5.7
            state_ = {0.1f, 0.0f, 0.0f};  // Near attractor
            baseDt_ = 0.02f;
            safeBound_ = 20.0f;
            normalizationFactor_ = 10.0f;
            perturbationScale_ = 0.1f;
            break;

        case ChaosModel::Chua:
            // Chua: alpha=15.6, beta=28, m0=-1.143, m1=-0.714
            state_ = {0.1f, 0.0f, 0.0f};  // Near one scroll
            baseDt_ = 0.01f;
            safeBound_ = 10.0f;
            normalizationFactor_ = 5.0f;
            perturbationScale_ = 0.08f;
            break;

        case ChaosModel::Henon:
            // Henon: a=1.4, b=0.3
            state_ = {0.0f, 0.0f, 0.0f};  // y stored in y, z unused
            henonPhase_ = 0.0f;
            baseDt_ = 1.0f;  // One iteration per update
            safeBound_ = 5.0f;
            normalizationFactor_ = 1.5f;
            perturbationScale_ = 0.05f;
            break;
    }

    normalizedX_ = std::clamp(state_.x / normalizationFactor_, -1.0f, 1.0f);
    normalizedY_ = std::clamp(state_.y / normalizationFactor_, -1.0f, 1.0f);
    smoothedX_ = 0.0f;
    smoothedY_ = 0.0f;
}

inline void ChaosWaveshaper::updateLorenz() noexcept {
    // FR-013, FR-014: Lorenz with sigma=10, rho=28, beta=8/3
    constexpr float sigma = 10.0f;
    constexpr float rho = 28.0f;
    constexpr float beta = 8.0f / 3.0f;

    // FR-019: Sample-rate compensated timestep
    const float dt = baseDt_ * static_cast<float>(44100.0 / sampleRate_) * attractorSpeed_;

    // Euler integration
    const float dx = sigma * (state_.y - state_.x);
    const float dy = state_.x * (rho - state_.z) - state_.y;
    const float dz = state_.x * state_.y - beta * state_.z;

    state_.x += dx * dt;
    state_.y += dy * dt;
    state_.z += dz * dt;
}

inline void ChaosWaveshaper::updateRossler() noexcept {
    // FR-015: Rossler with a=0.2, b=0.2, c=5.7
    constexpr float a = 0.2f;
    constexpr float b = 0.2f;
    constexpr float c = 5.7f;

    // FR-019: Sample-rate compensated timestep
    const float dt = baseDt_ * static_cast<float>(44100.0 / sampleRate_) * attractorSpeed_;

    // Euler integration
    const float dx = -state_.y - state_.z;
    const float dy = state_.x + a * state_.y;
    const float dz = b + state_.z * (state_.x - c);

    state_.x += dx * dt;
    state_.y += dy * dt;
    state_.z += dz * dt;
}

inline float ChaosWaveshaper::chuaDiode(float x) noexcept {
    // FR-016: h(x) = m1*x + 0.5*(m0-m1)*(|x+1| - |x-1|)
    constexpr float m0 = -1.143f;
    constexpr float m1 = -0.714f;
    return m1 * x + 0.5f * (m0 - m1) * (std::abs(x + 1.0f) - std::abs(x - 1.0f));
}

inline void ChaosWaveshaper::updateChua() noexcept {
    // FR-016: Chua with alpha=15.6, beta=28
    constexpr float alpha = 15.6f;
    constexpr float beta = 28.0f;

    // FR-019: Sample-rate compensated timestep
    const float dt = baseDt_ * static_cast<float>(44100.0 / sampleRate_) * attractorSpeed_;

    // Euler integration
    const float hx = chuaDiode(state_.x);
    const float dx = alpha * (state_.y - state_.x - hx);
    const float dy = state_.x - state_.y + state_.z;
    const float dz = -beta * state_.y;

    state_.x += dx * dt;
    state_.y += dy * dt;
    state_.z += dz * dt;
}

inline void ChaosWaveshaper::updateHenon() noexcept {
    // FR-017: Henon map with a=1.4, b=0.3
    constexpr float a = 1.4f;
    constexpr float b = 0.3f;

    // Phase increment for interpolation
    const float phaseInc = attractorSpeed_ * static_cast<float>(44100.0 / sampleRate_) * 0.1f;
    henonPhase_ += phaseInc;

    // Iterate map when phase wraps
    if (henonPhase_ >= 1.0f) {
        henonPhase_ -= 1.0f;

        // Henon map iteration
        const float newX = 1.0f - a * state_.x * state_.x + state_.y;
        const float newY = b * state_.x;
        state_.x = newX;
        state_.y = newY;
    }

    // Henon output is HELD between map iterations, not interpolated: the output
    // comes from normalizedX_, which updateAttractor() derives from state_.x, and
    // state_.x only changes when the phase wraps above. henonPhase_ therefore
    // controls the iteration rate, not a ramp between iterations.
}

inline void ChaosWaveshaper::updateSmoothCoeff() noexcept {
    if (smoothness_ <= 0.001f) {
        smoothCoeff_ = 0.0f;
        return;
    }
    // Map smoothness [0,1] to time constant [0, 200ms]
    // Quadratic curve for musical feel (most control at low end)
    const float timeConstantSec = smoothness_ * smoothness_ * 0.2f;
    const float timeConstantSamples = timeConstantSec * static_cast<float>(sampleRate_);
    if (timeConstantSamples < 1.0f) {
        smoothCoeff_ = 0.0f;
    } else {
        smoothCoeff_ = std::exp(-1.0f / timeConstantSamples);
    }
}

} // namespace DSP
} // namespace Krate
