#pragma once

// ==============================================================================
// AuditionVoice — Minimal single voice for pattern auditioning
// ==============================================================================
// Simple: PolyBlepOscillator + linear ADSR + gain.
// Not a production synth — just so users can hear arp patterns.
//
// One voice of a polyphonic pool. Note-to-voice routing lives in
// Krate::DSP::VoiceAllocator (Layer 3); this class owns no note identity, only
// a frequency handed to it at noteOn. processBlock ACCUMULATES into the output
// buffers, so a pool sums simply by rendering each voice in turn.
// ==============================================================================

#include <krate/dsp/primitives/polyblep_oscillator.h>

#include <algorithm>
#include <cmath>

namespace Gradus {

class AuditionVoice {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        osc_.prepare(sampleRate);
        osc_.setWaveform(Krate::DSP::OscWaveform::Sine);
    }

    void setWaveform(int type) noexcept {
        switch (type) {
            case 0: osc_.setWaveform(Krate::DSP::OscWaveform::Sine); break;
            case 1: osc_.setWaveform(Krate::DSP::OscWaveform::Sawtooth); break;
            case 2: osc_.setWaveform(Krate::DSP::OscWaveform::Square); break;
            default: osc_.setWaveform(Krate::DSP::OscWaveform::Sine); break;
        }
    }

    void setDecay(float ms) noexcept {
        decayMs_ = std::max(1.0f, ms);
    }

    void setVolume(float vol) noexcept {
        volume_ = std::clamp(vol, 0.0f, 1.0f);
    }

    /// @param frequencyHz Pre-computed pitch. VoiceAllocator already derives it
    ///        from the MIDI note, so the voice does not repeat a std::pow on the
    ///        audio thread the way the old monophonic path did.
    void noteOn(float frequencyHz, uint8_t velocity) noexcept {
        osc_.setFrequency(frequencyHz);
        velocity_ = static_cast<float>(velocity) / 127.0f;
        envPhase_ = EnvPhase::Attack;
        envLevel_ = 0.0f;
        active_ = true;
    }

    void noteOff() noexcept {
        if (active_) {
            envPhase_ = EnvPhase::Release;
            // Pre-calculate fixed release rate so it's sample-rate-independent
            releaseRate_ = envLevel_ /
                std::max(1.0f, kReleaseMs * 0.001f * static_cast<float>(sampleRate_));
        }
    }

    void processBlock(float* outL, float* outR, size_t numSamples) noexcept {
        if (!active_) return;

        const float attackInc = 1.0f / (kAttackMs * 0.001f * static_cast<float>(sampleRate_));
        const float decayInc = (1.0f - kSustainLevel) /
            (decayMs_ * 0.001f * static_cast<float>(sampleRate_));

        for (size_t i = 0; i < numSamples; ++i) {
            // Advance envelope
            switch (envPhase_) {
                case EnvPhase::Attack:
                    envLevel_ += attackInc;
                    if (envLevel_ >= 1.0f) {
                        envLevel_ = 1.0f;
                        envPhase_ = EnvPhase::Decay;
                    }
                    break;
                case EnvPhase::Decay:
                    envLevel_ -= decayInc;
                    if (envLevel_ <= kSustainLevel) {
                        envLevel_ = kSustainLevel;
                        envPhase_ = EnvPhase::Sustain;
                    }
                    break;
                case EnvPhase::Sustain:
                    // Hold at sustain level
                    break;
                case EnvPhase::Release:
                    envLevel_ -= releaseRate_;
                    if (envLevel_ <= 0.0f) {
                        envLevel_ = 0.0f;
                        active_ = false;
                        return; // Stop processing
                    }
                    break;
            }

            float sample = osc_.process() * envLevel_ * velocity_ * volume_;
            // Sanitize NaN/Inf from oscillator or arithmetic edge cases
            if (!std::isfinite(sample)) sample = 0.0f;
            outL[i] += sample;
            outR[i] += sample;
        }
    }

    /// @brief Silence the voice immediately and drop its envelope state.
    /// `active_` otherwise only clears from inside processBlock, so a voice
    /// that stops being rendered (deactivation, audition switched off
    /// mid-envelope) would stay flagged active forever.
    void reset() noexcept {
        envPhase_ = EnvPhase::Release;
        envLevel_ = 0.0f;
        releaseRate_ = 0.0f;
        velocity_ = 0.0f;
        active_ = false;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    enum class EnvPhase { Attack, Decay, Sustain, Release };

    // Envelope timing constants
    static constexpr float kAttackMs = 2.0f;
    static constexpr float kSustainLevel = 0.6f;
    static constexpr float kReleaseMs = 30.0f;

    Krate::DSP::PolyBlepOscillator osc_;
    double sampleRate_ = 44100.0;
    float velocity_ = 0.0f;
    float volume_ = 0.7f;
    float decayMs_ = 200.0f;
    float envLevel_ = 0.0f;
    float releaseRate_ = 0.0f;  // Pre-calculated at noteOff for SR-independence
    EnvPhase envPhase_ = EnvPhase::Release;
    bool active_ = false;
};

} // namespace Gradus
