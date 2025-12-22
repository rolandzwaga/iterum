#pragma once
// ==============================================================================
// Test Signal Generators
// ==============================================================================
// Standard test signals for DSP algorithm verification.
// See specs/TESTING-GUIDE.md for usage guidance.
// ==============================================================================

#include <array>
#include <cmath>
#include <random>
#include <algorithm>
#include <numbers>

namespace TestHelpers {

// ==============================================================================
// Constants
// ==============================================================================

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

// ==============================================================================
// Impulse Signal
// ==============================================================================
// Single sample at 1.0, rest zeros. Used for measuring impulse response.

template <size_t N>
inline void generateImpulse(std::array<float, N>& buffer, size_t offset = 0) {
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    if (offset < N) {
        buffer[offset] = 1.0f;
    }
}

inline void generateImpulse(float* buffer, size_t size, size_t offset = 0) {
    std::fill(buffer, buffer + size, 0.0f);
    if (offset < size) {
        buffer[offset] = 1.0f;
    }
}

// ==============================================================================
// Step Signal
// ==============================================================================
// Zeros before offset, ones after. Used for DC response and settling time.

template <size_t N>
inline void generateStep(std::array<float, N>& buffer, size_t offset = 0) {
    for (size_t i = 0; i < N; ++i) {
        buffer[i] = (i >= offset) ? 1.0f : 0.0f;
    }
}

inline void generateStep(float* buffer, size_t size, size_t offset = 0) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = (i >= offset) ? 1.0f : 0.0f;
    }
}

// ==============================================================================
// Sine Wave
// ==============================================================================
// Pure sinusoid. Used for frequency response and THD measurement.

template <size_t N>
inline void generateSine(std::array<float, N>& buffer,
                         float frequency,
                         float sampleRate,
                         float amplitude = 1.0f,
                         float phase = 0.0f) {
    const float phaseIncrement = kTwoPi * frequency / sampleRate;
    for (size_t i = 0; i < N; ++i) {
        buffer[i] = amplitude * std::sin(phase + phaseIncrement * static_cast<float>(i));
    }
}

inline void generateSine(float* buffer,
                         size_t size,
                         float frequency,
                         float sampleRate,
                         float amplitude = 1.0f,
                         float phase = 0.0f) {
    const float phaseIncrement = kTwoPi * frequency / sampleRate;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(phase + phaseIncrement * static_cast<float>(i));
    }
}

// ==============================================================================
// White Noise
// ==============================================================================
// Random values in [-1, 1]. Used for full-spectrum response.

template <size_t N>
inline void generateWhiteNoise(std::array<float, N>& buffer, uint32_t seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < N; ++i) {
        buffer[i] = dist(gen);
    }
}

inline void generateWhiteNoise(float* buffer, size_t size, uint32_t seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

// ==============================================================================
// DC Signal
// ==============================================================================
// Constant value. Used for DC offset testing.

template <size_t N>
inline void generateDC(std::array<float, N>& buffer, float level = 1.0f) {
    std::fill(buffer.begin(), buffer.end(), level);
}

inline void generateDC(float* buffer, size_t size, float level = 1.0f) {
    std::fill(buffer, buffer + size, level);
}

// ==============================================================================
// Silence
// ==============================================================================
// All zeros. Used for noise floor and silence detection tests.

template <size_t N>
inline void generateSilence(std::array<float, N>& buffer) {
    std::fill(buffer.begin(), buffer.end(), 0.0f);
}

inline void generateSilence(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
}

// ==============================================================================
// Linear Sweep (Chirp)
// ==============================================================================
// Frequency sweep from startFreq to endFreq. Used for time-varying response.

template <size_t N>
inline void generateSweep(std::array<float, N>& buffer,
                          float startFreq,
                          float endFreq,
                          float sampleRate,
                          float amplitude = 1.0f) {
    const float duration = static_cast<float>(N) / sampleRate;
    const float freqRate = (endFreq - startFreq) / duration;

    float phase = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float instantFreq = startFreq + freqRate * t;
        buffer[i] = amplitude * std::sin(phase);
        phase += kTwoPi * instantFreq / sampleRate;

        // Keep phase bounded
        if (phase > kTwoPi) phase -= kTwoPi;
    }
}

inline void generateSweep(float* buffer,
                          size_t size,
                          float startFreq,
                          float endFreq,
                          float sampleRate,
                          float amplitude = 1.0f) {
    const float duration = static_cast<float>(size) / sampleRate;
    const float freqRate = (endFreq - startFreq) / duration;

    float phase = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float instantFreq = startFreq + freqRate * t;
        buffer[i] = amplitude * std::sin(phase);
        phase += kTwoPi * instantFreq / sampleRate;

        if (phase > kTwoPi) phase -= kTwoPi;
    }
}

// ==============================================================================
// Square Wave
// ==============================================================================
// Alternating +1/-1. Rich in odd harmonics.

template <size_t N>
inline void generateSquare(std::array<float, N>& buffer,
                           float frequency,
                           float sampleRate,
                           float amplitude = 1.0f) {
    const float period = sampleRate / frequency;
    for (size_t i = 0; i < N; ++i) {
        float phase = std::fmod(static_cast<float>(i), period) / period;
        buffer[i] = (phase < 0.5f) ? amplitude : -amplitude;
    }
}

// ==============================================================================
// Sawtooth Wave
// ==============================================================================
// Linear ramp from -1 to +1. Rich in all harmonics.

template <size_t N>
inline void generateSawtooth(std::array<float, N>& buffer,
                             float frequency,
                             float sampleRate,
                             float amplitude = 1.0f) {
    const float period = sampleRate / frequency;
    for (size_t i = 0; i < N; ++i) {
        float phase = std::fmod(static_cast<float>(i), period) / period;
        buffer[i] = amplitude * (2.0f * phase - 1.0f);
    }
}

} // namespace TestHelpers
