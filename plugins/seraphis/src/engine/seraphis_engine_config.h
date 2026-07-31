#pragma once

// ==============================================================================
// Seraphis - Prepare-Time Engine / Reverb Configuration (FR-053, FR-034a)
// ==============================================================================
// NO DSP LIVES HERE. This header introduces no new type: it hands back the
// DSP-owned config structs with Seraphis's shipped values filled in, plus the
// one control-application helper that process() needs and tests need to call
// directly.
// ==============================================================================

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <cstddef>
#include <cstdint>

namespace Seraphis {

/// FR-053. The one seed the whole plugin uses. NOT a parameter in Phase 8; two
/// instances in one host therefore share a trajectory (spec, "Seed
/// determinism"). Phase 9 owns any per-instance seed.
///
/// Stated EXPLICITLY rather than inherited from the struct defaults
/// (seraphis_engine.h:96 / aether_reverb.h:1586), so a future `dsp/` default
/// change cannot silently move Seraphis's sound.
inline constexpr std::uint32_t kEngineSeed = 1u;
inline constexpr std::uint32_t kReverbSeed = 1u;

/// FR-024a clause 3. Master-gain smoothing time. Same 20 ms family as its
/// sibling `SeraphisEngine::kSumGainSmoothMs = 20.0f`
/// (dsp/include/krate/dsp/systems/seraphis_engine.h:138). NEVER an unnamed
/// literal at the use site.
inline constexpr float kMasterGainSmoothMs = 20.0f;

/// FR-023 clause 1 / FR-026 / FR-028: ONE constant governs the engine config,
/// the reverb config, the slice bound and the scratch size, so they cannot
/// drift apart. == 2048 (seraphis_engine.h:134).
inline constexpr std::size_t kMaxBlockSamples = Krate::DSP::SeraphisEngine::kMaxBlockSamples;

/// @brief Build the shipped SeraphisEngineConfig. Prepare-time only.
[[nodiscard]] inline Krate::DSP::SeraphisEngineConfig makeSeraphisEngineConfig(
    std::size_t polyphony,
    std::uint32_t seed,
    std::size_t maxBlockSamples) noexcept {

    Krate::DSP::SeraphisEngineConfig cfg{};   // seraphis_engine.h:92-97
    cfg.voice.captureSeconds  = 4.0f;         // seraphis_voice.h:108  (shipped default)
    cfg.voice.blurEnabled     = true;         // :110
    cfg.voice.freezeEnabled   = true;         // :112
    cfg.voice.blurFftSize     = 1024;         // :114
    cfg.voice.freezeFftSize   = 2048;         // :116
    cfg.voice.maxBlockSamples = maxBlockSamples;  // :119
    cfg.polyphony             = polyphony;    // :95  (SEEDED FROM THE PARAMETER, FR-023 cl.2)
    cfg.seed                  = seed;         // :96  (explicit, never the struct default)
    return cfg;
}

/// @brief Build the shipped AetherReverb::PrepareConfig. Prepare-time only.
///
/// `spectralDiffusionEnabled` and `diffusionFftSize` MUST NOT be changed: the
/// resulting 1024-sample latency is owned by FR-033, not dodged here.
[[nodiscard]] inline Krate::DSP::AetherReverb::PrepareConfig makeSeraphisReverbConfig(
    std::size_t maxBlockSamples) noexcept {

    Krate::DSP::AetherReverb::PrepareConfig cfg{};  // aether_reverb.h:1577-1587
    cfg.numChannels              = 8;               // :1578
    cfg.maxBlockSamples          = maxBlockSamples;  // :1579  (own clamp [64,8192] at :1619)
    cfg.maxDelaySeconds          = 0.50f;           // :1580
    cfg.shimmerEnabled           = true;            // :1581
    // PitchMode is a NAMESPACE-scope enum in Krate::DSP
    // (pitch_shift_processor.h:57-63), NOT a member of AetherReverb - qualified
    // lookup through the class would not find it. Same spelling the DSP tests
    // use (dsp/tests/unit/effects/aether_reverb_test.cpp:3073).
    cfg.shimmerMode              = Krate::DSP::PitchMode::Granular;  // :1582
    cfg.bloomEnabled             = true;            // :1583
    cfg.spectralDiffusionEnabled = true;            // :1584  MUST stay true (FR-033/FR-053)
    cfg.diffusionFftSize         = 1024;            // :1585  MUST stay 1024 -> 1024-sample latency
    cfg.seed                     = kReverbSeed;     // :1586  explicit, never the struct default
    return cfg;
}

/// @brief FR-034a. Push the Aether-owned macro targets into the reverb.
///
/// A FREE FUNCTION, not a lambda inside process(): the eight targets have no
/// getter on AetherReverb, so this is the only surface a test can call directly
/// with non-neutral values.
///
/// @par Real-Time Safety: noexcept and allocation-free. Every setter funnels
///      through AetherReverb::applyControl (aether_reverb.h:2950-2958), a clamp
///      plus a smoother store.
inline void applyAetherTargets(Krate::DSP::AetherReverb& reverb,
                               const Krate::DSP::SeraphisAetherTargets& t) noexcept {
    reverb.setMix(t.mix);                                          // aether_reverb.h:2336
    reverb.setSize(t.size);                                        // :2208
    reverb.setWidth(t.width);                                      // :2333
    reverb.setShimmerOctaveSend(t.shimmerOctaveSend);              // :2280
    reverb.setShimmerFifthSend(t.shimmerFifthSend);                // :2285
    reverb.setBloomSend(t.bloomSend);                              // :2295
    reverb.setSizeBreathDepth(t.sizeBreathDepth);                  // :2320
    reverb.setDimensionalityTideDepth(t.dimensionalityTideDepth);  // :2328
}

}  // namespace Seraphis
