// ==============================================================================
// Layer 2: Processors
// spectral_state.cpp - out-of-line factory-state builder
// ==============================================================================
// makeFactoryState was an inline header function until 2026-08-05. Its body
// evaluates ~200 std::pow/std::exp calls, and an inline definition compiles
// once per consuming TU under THAT TU's floating-point flags. On the macOS CI
// leg (Apple Clang, Xcode 26.6) a -ffast-math TU and a -fno-fast-math TU
// lower those calls differently, so the "same" factory state came out with
// different last-ULP bits in the processor, the controller and the tests --
// breaking the byte-identity contracts the state format depends on
// (state_v3_test's slot-discard checks were the first visible casualty).
//
// Compiling the builder exactly ONCE, in this TU, restores the FR-021/FR-022
// guarantee that two calls with the same id are bitwise identical everywhere
// in the process. This TU is listed in dsp/CMakeLists.txt's -fno-fast-math
// block (same treatment as spectral_simd.cpp) so the bits are the IEEE libm
// results on every toolchain.
//
// The body below is moved VERBATIM from spectral_state.h; behavioural comments
// live with the declaration in the header.
// ==============================================================================

#include <krate/dsp/processors/spectral_state.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

[[nodiscard]] SpectralState makeFactoryState(SpectralStateId id) noexcept {
    SpectralState s{};

    int count = 64;
    const char* label = "SineStack";
    switch (id) {
    case SpectralStateId::SineStack:
        break;
    case SpectralStateId::Bell:
        count = 24;
        label = "Bell";
        break;
    case SpectralStateId::Choir:
        label = "Choir";
        break;
    case SpectralStateId::Glass:
        label = "Glass";
        break;
    case SpectralStateId::Breath:
        label = "Breath";
        break;
    case SpectralStateId::Hollow:
        count = 32;
        label = "Hollow";
        break;
    case SpectralStateId::Metal:
        label = "Metal";
        break;
    case SpectralStateId::Organ:
        count = 9;
        label = "Organ";
        break;
    case SpectralStateId::Vowel:
        label = "Vowel";
        break;
    case SpectralStateId::Shimmer:
        count = 16;
        label = "Shimmer";
        break;
    }
    s.numPartials = count;

    // (1) Authored ratios.
    for (int i = 0; i < count; ++i) {
        const float n = static_cast<float>(i + 1);
        float ratio = n;
        switch (id) {
        case SpectralStateId::Bell:
            ratio = n * std::sqrt(1.0f + detail::factory::kBellB * n * n);
            break;
        case SpectralStateId::Glass:
            ratio = n * (1.0f + detail::factory::kGlassStretch * n);
            break;
        case SpectralStateId::Hollow:
            // 1-based authored index k = i + 1: odd harmonics 1, 3, 5, ... 63.
            ratio = 2.0f * n - 1.0f;
            break;
        case SpectralStateId::Metal:
            ratio = n * std::sqrt(1.0f + detail::factory::kMetalB * n * n);
            break;
        case SpectralStateId::Organ:
            ratio = detail::factory::kOrganRatios[static_cast<std::size_t>(i)];
            break;
        case SpectralStateId::Shimmer:
            if (i == 0) {
                ratio = 1.0f; // Faint fundamental anchor.
            } else {
                const float m = detail::factory::kShimmerStep * n;
                ratio = m * (1.0f + detail::factory::kShimmerStretch * m);
            }
            break;
        case SpectralStateId::SineStack:
        case SpectralStateId::Choir:
        case SpectralStateId::Breath:
        case SpectralStateId::Vowel:
            break;
        }
        s.ratios[static_cast<std::size_t>(i)] = ratio;
    }

    // (2) FR-041 geometric continuation over the unauthored slots.
    // The `count < 2` arm is deviation D9's `j + 1` rule and applies for EVERY
    // j, not only j < 2; no factory state reaches it (the sparsest is Organ at
    // 9), but the recurrence is kept identical to the engine's on purpose.
    for (auto j = static_cast<std::size_t>(count); j < SpectralState::kStatePartials; ++j) {
        float grown = 0.0f;
        if (count >= 2) {
            // j >= 2 always holds here: the loop starts at j = count >= 2.
            const float lastSpacing = s.ratios[j - 1] / s.ratios[j - 2];
            const float g = std::clamp(lastSpacing, 1.0f, detail::factory::kFillMaxGrowth);
            grown = std::min(s.ratios[j - 1] * g, detail::factory::kFillMaxRatio);
        } else {
            grown = static_cast<float>(j + 1);
        }
        const float floorValue = (j >= 1) ? s.ratios[j - 1] * detail::factory::kFillSpacingFactor
                                          : static_cast<float>(j + 1);
        s.ratios[j] = std::max(grown, floorValue);
    }

    // (3) Authored amplitudes; slots at or above `count` stay at the
    // value-initialised 0.0f.
    for (int i = 0; i < count; ++i) {
        const float n = static_cast<float>(i + 1);
        float amplitude = 1.0f;
        switch (id) {
        case SpectralStateId::SineStack:
            amplitude = 1.0f / n;
            break;
        case SpectralStateId::Bell:
            amplitude = std::pow(n, -1.4f);
            break;
        case SpectralStateId::Choir: {
            float shape = detail::factory::kChoirFloor;
            for (std::size_t k = 0; k < detail::factory::kChoirCentres.size(); ++k) {
                const float offset = n - detail::factory::kChoirCentres[k];
                const float sigma = detail::factory::kChoirSigmas[k];
                shape += detail::factory::kChoirGains[k]
                         * std::exp(-(offset * offset) / (2.0f * sigma * sigma));
            }
            amplitude = std::pow(n, -0.8f) * shape;
            break;
        }
        case SpectralStateId::Glass:
            amplitude = std::pow(n, -0.5f)
                        * (((i + 1) % 2 == 0) ? detail::factory::kGlassEvenAtten : 1.0f);
            break;
        case SpectralStateId::Breath:
            amplitude =
                std::pow(n, -0.25f)
                * (1.0f
                   - detail::factory::kBreathLowDepth
                         * std::exp(-(n - 1.0f) / detail::factory::kBreathLowScale));
            break;
        case SpectralStateId::Hollow:
            // amp_k = (2k - 1)^-kHollowExp -- flat-ish odd spectrum.
            amplitude = std::pow(2.0f * n - 1.0f, -detail::factory::kHollowExp);
            break;
        case SpectralStateId::Metal:
            // Inverted comb: every THIRD partial dominant, the rest attenuated.
            amplitude = std::pow(n, -0.5f)
                        * (((i + 1) % 3 == 0) ? 1.0f : detail::factory::kMetalCombAtten);
            break;
        case SpectralStateId::Organ:
            amplitude = detail::factory::kOrganAmps[static_cast<std::size_t>(i)];
            break;
        case SpectralStateId::Vowel: {
            float shape = detail::factory::kVowelFloor;
            for (std::size_t k = 0; k < detail::factory::kVowelCentres.size(); ++k) {
                const float offset = n - detail::factory::kVowelCentres[k];
                const float sigma = detail::factory::kVowelSigmas[k];
                shape += detail::factory::kVowelGains[k]
                         * std::exp(-(offset * offset) / (2.0f * sigma * sigma));
            }
            amplitude = std::pow(n, -0.9f) * shape;
            break;
        }
        case SpectralStateId::Shimmer:
            if (i == 0) {
                amplitude = detail::factory::kShimmerAnchorAmp;
            } else {
                const float m = detail::factory::kShimmerStep * n;
                amplitude = std::pow(m, -detail::factory::kShimmerExp);
            }
            break;
        }
        s.amplitudes[static_cast<std::size_t>(i)] = amplitude;
    }

    // (4) FR-014.
    normalizeSpectralState(s);

    // (5) NUL-padded ASCII label; `name` is already all-zero, so the copy stops
    // one byte short of the field and the terminator is guaranteed.
    detail::copyStateName(s, label);

    return s;
}

} // namespace Krate::DSP
