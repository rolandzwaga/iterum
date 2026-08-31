// ==============================================================================
// Layer 2: DSP Processor - Chaos Modulation Source
// ==============================================================================
// Chaotic attractor modulation source using Lorenz, Rossler, Chua, Henon and
// Aizawa models. Aizawa is a ChaosModSource-only model: ChaosWaveshaper's
// validator rejects it and substitutes Lorenz (FR-034).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-030 to FR-035)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>  // ChaosModel enum

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Chaotic attractor modulation source.
///
/// Implements ModulationSource interface. Evolves a chaotic attractor system
/// and outputs the normalized X-axis value as modulation signal.
///
/// @par Output Range: [-1.0, +1.0]
class ChaosModSource : public ModulationSource {
public:
    static constexpr float kMinSpeed = 0.05f;
    static constexpr float kMaxSpeed = 20.0f;
    static constexpr float kDefaultSpeed = 1.0f;
    static constexpr float kMinCoupling = 0.0f;
    static constexpr float kMaxCoupling = 1.0f;
    static constexpr float kDefaultCoupling = 0.0f;
    static constexpr size_t kControlRateInterval = 32;

    // Per-model normalization scale constants (FR-034)
    static constexpr float kLorenzScale = 20.0f;
    static constexpr float kRosslerScale = 10.0f;
    static constexpr float kChuaScale = 2.0f;
    static constexpr float kHenonScale = 1.5f;
    static constexpr float kAizawaScale = 1.5f;  ///< attractor x-extent ~ +/-1.5

    ChaosModSource() noexcept = default;

    void prepare(double sampleRate) noexcept {
        // FR-002: floor the sample rate at 1 Hz so a host handing us 0 or a
        // negative rate cannot poison derived state. Behaviourally inert today --
        // sampleRate_ is stored but never read by the attractor math, whose step
        // is baseDt_ * effectiveSpeed in updateAttractor() -- but the floor is a
        // shared precondition across every Vorago Phase 1 source.
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        updateModelParams();
        resetModelState();
        samplesUntilUpdate_ = 0;
        divergenceResetCount_ = 0;
    }

    void reset() noexcept {
        resetModelState();
        normalizedOutput_ = 0.0f;
        inputLevel_ = 0.0f;
        samplesUntilUpdate_ = 0;
        divergenceResetCount_ = 0;
    }

    /// @brief Process one sample (call at audio rate).
    /// Updates attractor at control rate (every 32 samples).
    void process() noexcept {
        --samplesUntilUpdate_;
        if (samplesUntilUpdate_ <= 0) {
            samplesUntilUpdate_ = static_cast<int>(kControlRateInterval);
            updateAttractor();
        }
    }

    /// @brief Process a block of samples (call once per block).
    /// Advances the attractor by the correct number of control-rate steps.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (samplesUntilUpdate_ <= 0) {
                samplesUntilUpdate_ = static_cast<int>(kControlRateInterval);
                updateAttractor();
            }
            int advance = std::min(remaining, samplesUntilUpdate_);
            samplesUntilUpdate_ -= advance;
            remaining -= advance;
        }
    }

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return normalizedOutput_;
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // Parameter setters
    void setModel(ChaosModel model) noexcept {
        if (model_ != model) {
            model_ = model;
            updateModelParams();
            resetModelState();
        }
    }

    void setSpeed(float speed) noexcept {
        speed_ = std::clamp(speed, kMinSpeed, kMaxSpeed);
    }

    void setCoupling(float coupling) noexcept {
        coupling_ = std::clamp(coupling, kMinCoupling, kMaxCoupling);
    }

    void setInputLevel(float level) noexcept {
        inputLevel_ = level;
    }

    // Tempo sync
    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
    }

    void setTempo(float bpm) noexcept {
        bpm_ = std::clamp(bpm, 1.0f, 999.0f);
        if (tempoSync_) {
            updateTempoSyncSpeed();
        }
    }

    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = value;
        noteModifier_ = modifier;
        if (tempoSync_) {
            updateTempoSyncSpeed();
        }
    }

    // Parameter getters
    [[nodiscard]] ChaosModel getModel() const noexcept { return model_; }
    [[nodiscard]] float getSpeed() const noexcept { return speed_; }
    [[nodiscard]] float getCoupling() const noexcept { return coupling_; }
    [[nodiscard]] bool isTempoSynced() const noexcept { return tempoSync_; }
    [[nodiscard]] NoteValue getNoteValue() const noexcept { return noteValue_; }
    [[nodiscard]] NoteModifier getNoteModifier() const noexcept { return noteModifier_; }

    /// @brief FR-036: number of times the divergence guard has re-seeded the
    /// attractor state since the last prepare()/reset().
    ///
    /// Model-agnostic: the increment lives in the shared guard, not in any
    /// per-model branch. Zeroed by prepare() and reset() ONLY -- deliberately NOT
    /// by resetModelState(), which the guard itself calls (and which setModel()
    /// also calls), so a guard firing cannot erase its own evidence.
    [[nodiscard]] std::uint32_t getDivergenceResetCount() const noexcept {
        return divergenceResetCount_;
    }

private:
    struct AttractorState {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    void updateModelParams() noexcept {
        switch (model_) {
            case ChaosModel::Lorenz:
                baseDt_ = 0.005f;
                normalizationScale_ = kLorenzScale;
                safeBound_ = 50.0f;
                break;
            case ChaosModel::Rossler:
                baseDt_ = 0.01f;
                normalizationScale_ = kRosslerScale;
                safeBound_ = 30.0f;
                break;
            case ChaosModel::Chua:
                baseDt_ = 0.01f;
                normalizationScale_ = kChuaScale;
                safeBound_ = 5.0f;
                break;
            case ChaosModel::Henon:
                baseDt_ = 1.0f;  // Discrete map
                normalizationScale_ = kHenonScale;
                safeBound_ = 3.0f;
                break;
            case ChaosModel::Aizawa:
                // dt = baseDt_ * effectiveSpeed in updateAttractor() and
                // kMaxSpeed = 20, so baseDt_ * 20 <= 0.01 is required.
                // Forward-Euler Aizawa was simulated over dt in [5e-4, 0.2] from
                // four initial states: chaotic (x-extent +/-1.5..1.6) for
                // dt <= 0.015, and for dt >= 0.02 it COLLAPSES onto the
                // x = y = 0 fixed point (z ~= -1.105) where the output is
                // identically 0 -- silently, with no divergence and no guard
                // reset. 5.0e-4 puts dt in [2.5e-5, 1.0e-2], entirely inside the
                // verified-chaotic region.
                baseDt_ = 5.0e-4f;
                normalizationScale_ = kAizawaScale;
                // Guard threshold is safeBound_ * 10 in checkAndResetIfDiverged().
                // The coupling path in updateAttractor() (setInputLevel is
                // unclamped) legitimately drives |state| to ~112 at kMinSpeed
                // with a full-scale DC input, so 25 -> 250 never fires. The Chua
                // value (5 -> 50) would fire ~2000x per 600 s render.
                safeBound_ = 25.0f;
                break;
        }
    }

    void resetModelState() noexcept {
        switch (model_) {
            case ChaosModel::Lorenz:
                state_ = {1.0f, 1.0f, 1.0f};
                break;
            case ChaosModel::Rossler:
                state_ = {0.1f, 0.0f, 0.0f};
                break;
            case ChaosModel::Chua:
                state_ = {0.7f, 0.0f, 0.0f};
                break;
            case ChaosModel::Henon:
                state_ = {0.1f, 0.0f, 0.0f};
                break;
            case ChaosModel::Aizawa:
                state_ = {0.1f, 0.0f, 0.0f};
                break;
        }
        prevHenonX_ = state_.x;
        henonPhase_ = 0.0f;
    }

    void updateTempoSyncSpeed() noexcept {
        float beatsPerNote = getBeatsForNote(noteValue_, noteModifier_);
        float beatsPerSecond = bpm_ / 60.0f;
        tempoSyncSpeed_ = beatsPerSecond / beatsPerNote;
        tempoSyncSpeed_ = std::clamp(tempoSyncSpeed_, kMinSpeed, kMaxSpeed);
    }

    void updateAttractor() noexcept {
        float effectiveSpeed = tempoSync_ ? tempoSyncSpeed_ : speed_;
        float dt = baseDt_ * effectiveSpeed;

        // Apply coupling perturbation
        if (coupling_ > 0.0f && std::abs(inputLevel_) > 0.001f) {
            state_.x += coupling_ * inputLevel_ * 0.1f;
        }

        switch (model_) {
            case ChaosModel::Lorenz:
                updateLorenz(dt);
                break;
            case ChaosModel::Rossler:
                updateRossler(dt);
                break;
            case ChaosModel::Chua:
                updateChua(dt);
                break;
            case ChaosModel::Henon:
                updateHenon();
                break;
            case ChaosModel::Aizawa:
                updateAizawa(dt);
                break;
        }

        checkAndResetIfDiverged();

        // FR-034: soft-limit normalization
        normalizedOutput_ = std::clamp(std::tanh(state_.x / normalizationScale_), -1.0f, 1.0f);
    }

    void updateLorenz(float dt) noexcept {
        constexpr float sigma = 10.0f;
        constexpr float rho = 28.0f;
        constexpr float beta = 8.0f / 3.0f;

        float dx = sigma * (state_.y - state_.x);
        float dy = state_.x * (rho - state_.z) - state_.y;
        float dz = state_.x * state_.y - beta * state_.z;

        state_.x += dx * dt;
        state_.y += dy * dt;
        state_.z += dz * dt;
    }

    /// @brief Aizawa system, forward Euler (FR-031).
    /// dx/dt = (z - b) x - d y
    /// dy/dt = d x + (z - b) y
    /// dz/dt = c + a z - z^3/3 - (x^2 + y^2)(1 + e z) + f z x^3
    void updateAizawa(float dt) noexcept {
        constexpr float a = 0.95f;
        constexpr float b = 0.7f;
        constexpr float c = 0.6f;
        constexpr float d = 3.5f;
        constexpr float e = 0.25f;
        constexpr float f = 0.1f;

        // All three derivatives are evaluated from the SAME state: updating
        // state_ in place mid-expression would silently make this a
        // Gauss-Seidel step and change the attractor.
        const float x = state_.x;
        const float y = state_.y;
        const float z = state_.z;

        const float dx = (z - b) * x - d * y;
        const float dy = d * x + (z - b) * y;
        const float dz = c + a * z - (z * z * z) / 3.0f - (x * x + y * y) * (1.0f + e * z) +
                         f * z * (x * x * x);

        state_.x += dx * dt;
        state_.y += dy * dt;
        state_.z += dz * dt;
    }

    void updateRossler(float dt) noexcept {
        constexpr float a = 0.2f;
        constexpr float b = 0.2f;
        constexpr float c = 5.7f;

        float dx = -state_.y - state_.z;
        float dy = state_.x + a * state_.y;
        float dz = b + state_.z * (state_.x - c);

        state_.x += dx * dt;
        state_.y += dy * dt;
        state_.z += dz * dt;
    }

    void updateChua(float dt) noexcept {
        constexpr float alpha = 15.6f;
        constexpr float beta = 28.0f;
        constexpr float m0 = -1.143f;
        constexpr float m1 = -0.714f;

        float hx = chuaDiode(state_.x, m0, m1);
        float dx = alpha * (state_.y - state_.x - hx);
        float dy = state_.x - state_.y + state_.z;
        float dz = -beta * state_.y;

        state_.x += dx * dt;
        state_.y += dy * dt;
        state_.z += dz * dt;
    }

    void updateHenon() noexcept {
        constexpr float a = 1.4f;
        constexpr float b = 0.3f;

        float xNew = 1.0f - a * state_.x * state_.x + state_.y;
        float yNew = b * state_.x;

        prevHenonX_ = state_.x;
        state_.x = xNew;
        state_.y = yNew;
    }

    [[nodiscard]] static float chuaDiode(float x, float m0, float m1) noexcept {
        constexpr float bp = 1.0f;  // Breakpoint
        if (x > bp) {
            return m1 * x + (m0 - m1) * bp;
        }
        if (x < -bp) {
            return m1 * x - (m0 - m1) * bp;
        }
        return m0 * x;
    }

    void checkAndResetIfDiverged() noexcept {
        if (std::abs(state_.x) > safeBound_ * 10.0f ||
            std::abs(state_.y) > safeBound_ * 10.0f ||
            std::abs(state_.z) > safeBound_ * 10.0f) {
            // FR-036: count the reset BEFORE re-seeding, so the observation
            // survives resetModelState() (which never touches the counter).
            ++divergenceResetCount_;
            resetModelState();
        }
    }

    // State
    AttractorState state_{1.0f, 1.0f, 1.0f};
    float normalizedOutput_ = 0.0f;
    float inputLevel_ = 0.0f;
    int samplesUntilUpdate_ = 0;
    std::uint32_t divergenceResetCount_ = 0;  ///< FR-036, zeroed by prepare()/reset() only

    // Henon interpolation
    float prevHenonX_ = 0.0f;
    float henonPhase_ = 0.0f;

    // Configuration
    ChaosModel model_ = ChaosModel::Lorenz;
    float speed_ = kDefaultSpeed;
    float coupling_ = kDefaultCoupling;
    double sampleRate_ = 44100.0;

    // Tempo sync
    bool tempoSync_ = false;
    float bpm_ = 120.0f;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    float tempoSyncSpeed_ = 2.0f;

    // Per-model parameters
    float baseDt_ = 0.005f;
    float safeBound_ = 50.0f;
    float normalizationScale_ = 20.0f;
};

}  // namespace DSP
}  // namespace Krate
