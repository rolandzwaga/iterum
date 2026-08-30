// ==============================================================================
// Layer 3: System Tests - Seraphis Phase 9 parameter broadcast
//                                    (specs/seraphis-phase9-parameters)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.1, §7.2)
//            specs/seraphis-phase9-parameters/tasks.md
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   FR-001  SeraphisVoiceParams, the broadcast POD
//   FR-002  SeraphisEngine::applyVoiceParams
//   FR-003  SeraphisMacroMatrix::setTargetBase / resetTargetBases / getTargetBase
//   FR-004  evaluateAll() seeds from the override
//   FR-005  SeraphisEngine::applySpectralStates
//   FR-070  the thirteen new SeraphisVoice forwarders
//   FR-072  the fourteen new read-back accessors
//   SC-002 clause 4 - a default-constructed SeraphisMacroMatrix still evaluates
//                     all 27 targets to the kRows literals (plan §7.1)
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in dsp/tests/CMakeLists.txt and must not be. Its
//   non-finite section builds NaN/Inf from bit patterns, so it needs no IEEE
//   relaxation (plan §7.2's closing note).
//
// STACK RULE: heap-allocate every SeraphisEngine and every AetherReverb; never
//   a local (the Phase 7 plan §6.3 rule this TU inherits).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

using Krate::DSP::ContinuousBody;
using Krate::DSP::GrainEnvelopeType;
using Krate::DSP::makeFactoryState;
using Krate::DSP::SeraphisAetherTargets;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisMacroRow;
using Krate::DSP::SeraphisMacroTarget;
using Krate::DSP::SeraphisVoice;
using Krate::DSP::SeraphisVoiceConfig;
using Krate::DSP::SeraphisVoiceParams;
using Krate::DSP::SpectralMorphEngine;
using Krate::DSP::SpectralState;
using Krate::DSP::SpectralStateId;
using Krate::DSP::VoiceState;

using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;

// =============================================================================
// Non-finite construction (plan §0.3)
// =============================================================================

/// IEEE-754 binary32 bit patterns. NEVER std::numeric_limits<float>::
/// quiet_NaN() / infinity(): the macOS CI leg builds -ffast-math, which folds
/// both to finite garbage before the setter under test ever sees them.
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief Materialize a non-finite float from its bit pattern.
///
/// The volatile READ is the sink: it is what stops the constant being folded
/// back into the memcpy at compile time.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept
{
    volatile std::uint32_t b = bits;
    const std::uint32_t materialized = b;
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

// =============================================================================
// FR-070 - the thirteen forwarders, and their read-backs
// =============================================================================

// One slot per forwarder, in plan §1.5 order. Everything is widened to double
// so the two booleans and the grain-envelope enum can share one sibling sweep.
constexpr std::size_t kIdxCloudDriftSmoothness = 0;
constexpr std::size_t kIdxEnvelopeOffsetSpread = 1;
constexpr std::size_t kIdxAtmosDriftSmoothness = 2;
constexpr std::size_t kIdxAtmosDriftRangeSemis = 3;
constexpr std::size_t kIdxAtmosJitter = 4;
constexpr std::size_t kIdxAtmosPositionSeconds = 5;
constexpr std::size_t kIdxAtmosPositionSpread = 6;
constexpr std::size_t kIdxAtmosPitchSemitones = 7;
constexpr std::size_t kIdxAtmosPitchSpread = 8;
constexpr std::size_t kIdxAtmosGrainEnvelope = 9;
constexpr std::size_t kIdxMorphWaypointInterval = 10;
constexpr std::size_t kIdxBodyInputAgc = 11;
constexpr std::size_t kIdxBodyResonatorBypass = 12;
constexpr std::size_t kNumReadbacks = 13;

using Readbacks = std::array<double, kNumReadbacks>;

/// Every read-back FR-070 has to move, taken through the sub-component getters
/// that already ship (AtmosphereEngine's six at atmosphere_engine.h:803/:811/
/// :819/:826/:833/:962, HarmonicCloud's two at harmonic_cloud.h:528/:597,
/// SpectralMorphEngine::getWaypointInterval at spectral_morph_engine.h:392) and
/// the two ContinuousBody booleans FR-072 adds.
[[nodiscard]] Readbacks snapshot(const SeraphisVoice& v)
{
    Readbacks s{};
    s[kIdxCloudDriftSmoothness] = static_cast<double>(v.cloud().getDriftSmoothness());
    s[kIdxEnvelopeOffsetSpread] = static_cast<double>(v.cloud().getEnvelopeOffsetSpread());
    s[kIdxAtmosDriftSmoothness] = static_cast<double>(v.atmos().getDriftSmoothness());
    s[kIdxAtmosDriftRangeSemis] = static_cast<double>(v.atmos().getDriftRangeSemitones());
    s[kIdxAtmosJitter] = static_cast<double>(v.atmos().getJitter());
    s[kIdxAtmosPositionSeconds] = static_cast<double>(v.atmos().getPositionSeconds());
    s[kIdxAtmosPositionSpread] = static_cast<double>(v.atmos().getPositionSpread());
    s[kIdxAtmosPitchSemitones] = static_cast<double>(v.atmos().getPitchSemitones());
    s[kIdxAtmosPitchSpread] = static_cast<double>(v.atmos().getPitchSpread());
    s[kIdxAtmosGrainEnvelope] =
        static_cast<double>(static_cast<int>(v.atmos().getGrainEnvelope()));
    s[kIdxMorphWaypointInterval] = v.morph().getWaypointInterval();
    s[kIdxBodyInputAgc] = v.body().isInputAgcEnabled() ? 1.0 : 0.0;
    s[kIdxBodyResonatorBypass] = v.body().isResonatorBypass() ? 1.0 : 0.0;
    return s;
}

/// FR-070's one-to-one contract, stated as an assertion: the named read-back
/// carries the pushed value and EVERY sibling is bit-for-bit where it was.
/// This is what pins the two name collisions plan §1.2 warns about.
void requireOnlyMoved(const Readbacks& before, const Readbacks& after, std::size_t moved,
                      double expected)
{
    REQUIRE(after[moved] == Approx(expected));
    for (std::size_t i = 0; i < kNumReadbacks; ++i) {
        if (i == moved) {
            continue;
        }
        INFO("sibling read-back index " << i << " moved");
        REQUIRE(after[i] == before[i]);
    }
}

// =============================================================================
// FR-072 - the twelve ContinuousBody read-backs
// =============================================================================

/// One row per FR-072 float accessor. Captureless lambdas so the table needs no
/// member-pointer syntax and no std::function.
struct FloatAccessorCase {
    const char* name;
    void (*push)(ContinuousBody&, float);
    float (*read)(const ContinuousBody&);
    float minValue;
    float maxValue;
    float defaultValue;
    float inRange;
};

[[nodiscard]] std::array<FloatAccessorCase, 10> makeFloatAccessorCases()
{
    return {{
        {.name = "resonance",
         .push = [](ContinuousBody& b, float v) { b.setResonance(v); },
         .read = [](const ContinuousBody& b) { return b.getResonance(); },
         .minValue = ContinuousBody::kMinResonance,
         .maxValue = ContinuousBody::kMaxResonance,
         .defaultValue = ContinuousBody::kDefaultResonance,
         .inRange = 0.42f},
        {.name = "damping",
         .push = [](ContinuousBody& b, float v) { b.setDamping(v); },
         .read = [](const ContinuousBody& b) { return b.getDamping(); },
         .minValue = ContinuousBody::kMinDamping,
         .maxValue = ContinuousBody::kMaxDamping,
         .defaultValue = ContinuousBody::kDefaultDamping,
         .inRange = 0.63f},
        {.name = "keyTracking",
         .push = [](ContinuousBody& b, float v) { b.setKeyTracking(v); },
         .read = [](const ContinuousBody& b) { return b.getKeyTracking(); },
         .minValue = ContinuousBody::kMinKeyTracking,
         .maxValue = ContinuousBody::kMaxKeyTracking,
         .defaultValue = ContinuousBody::kDefaultKeyTracking,
         .inRange = 0.31f},
        {.name = "drive",
         .push = [](ContinuousBody& b, float v) { b.setDrive(v); },
         .read = [](const ContinuousBody& b) { return b.getDrive(); },
         .minValue = ContinuousBody::kMinUserDrive,
         .maxValue = ContinuousBody::kMaxUserDrive,
         .defaultValue = ContinuousBody::kDefaultUserDrive,
         .inRange = 2.75f},
        {.name = "mix",
         .push = [](ContinuousBody& b, float v) { b.setMix(v); },
         .read = [](const ContinuousBody& b) { return b.getMix(); },
         .minValue = ContinuousBody::kMinMix,
         .maxValue = ContinuousBody::kMaxMix,
         .defaultValue = ContinuousBody::kDefaultMix,
         .inRange = 0.44f},
        {.name = "cloudMix",
         .push = [](ContinuousBody& b, float v) { b.setCloudMix(v); },
         .read = [](const ContinuousBody& b) { return b.getCloudMix(); },
         .minValue = ContinuousBody::kMinCloudMix,
         .maxValue = ContinuousBody::kMaxCloudMix,
         .defaultValue = ContinuousBody::kDefaultCloudMix,
         .inRange = 0.66f},
        {.name = "cloudDecaySec",
         .push = [](ContinuousBody& b, float v) { b.setCloudDecaySec(v); },
         .read = [](const ContinuousBody& b) { return b.getCloudDecaySec(); },
         .minValue = ContinuousBody::kMinCloudDecaySec,
         .maxValue = ContinuousBody::kMaxCloudDecaySec,
         .defaultValue = ContinuousBody::kDefaultCloudDecaySec,
         .inRange = 12.5f},
        {.name = "cloudSize",
         .push = [](ContinuousBody& b, float v) { b.setCloudSize(v); },
         .read = [](const ContinuousBody& b) { return b.getCloudSize(); },
         .minValue = ContinuousBody::kMinCloudSize,
         .maxValue = ContinuousBody::kMaxCloudSize,
         .defaultValue = ContinuousBody::kDefaultCloudSize,
         .inRange = 0.22f},
        {.name = "cloudDamping",
         .push = [](ContinuousBody& b, float v) { b.setCloudDamping(v); },
         .read = [](const ContinuousBody& b) { return b.getCloudDamping(); },
         .minValue = ContinuousBody::kMinCloudDamping,
         .maxValue = ContinuousBody::kMaxCloudDamping,
         .defaultValue = ContinuousBody::kDefaultCloudDamping,
         .inRange = 0.77f},
        {.name = "width",
         .push = [](ContinuousBody& b, float v) { b.setWidth(v); },
         .read = [](const ContinuousBody& b) { return b.getWidth(); },
         .minValue = ContinuousBody::kMinWidth,
         .maxValue = ContinuousBody::kMaxWidth,
         .defaultValue = ContinuousBody::kDefaultWidth,
         .inRange = 0.55f},
    }};
}

// =============================================================================
// FR-001 / FR-002 - the broadcast POD and its 37 read-backs
// =============================================================================

/// One slot per SeraphisVoiceParams field, in DECLARATION ORDER (plan §1.1).
/// Everything is widened to double so the three enums and the two booleans can
/// share one sibling sweep with the thirty-two floats and the one double.
constexpr std::size_t kVpFields = SeraphisVoiceParams::kFieldCount;

constexpr std::size_t kVpCloudDriftSmoothness = 0;
constexpr std::size_t kVpCloudDecaySec = 1;
constexpr std::size_t kVpCloudEnvOffsetSpread = 2;
constexpr std::size_t kVpMorphBloom = 3;
constexpr std::size_t kVpMorphTravelMode = 4;
constexpr std::size_t kVpMorphTravelRate = 5;
constexpr std::size_t kVpMorphWaypointSeconds = 6;
constexpr std::size_t kVpSpatialRateHz = 7;
constexpr std::size_t kVpSpatialCoupling = 8;
constexpr std::size_t kVpSpatialGrowth = 9;
constexpr std::size_t kVpEnvMode = 10;
constexpr std::size_t kVpEnvGrowthDurationSec = 11;
constexpr std::size_t kVpBodyMaterial = 12;
constexpr std::size_t kVpBodyResonance = 13;
constexpr std::size_t kVpBodyKeyTracking = 14;
constexpr std::size_t kVpBodyDrive = 15;
constexpr std::size_t kVpBodyMix = 16;
constexpr std::size_t kVpBodyCloudMix = 17;
constexpr std::size_t kVpBodyCloudDecaySec = 18;
constexpr std::size_t kVpBodyCloudSize = 19;
constexpr std::size_t kVpBodyCloudDamping = 20;
constexpr std::size_t kVpBodyWidth = 21;
constexpr std::size_t kVpBodyInputAgc = 22;
constexpr std::size_t kVpBodyResonatorBypass = 23;
constexpr std::size_t kVpAtmosDensity = 24;
constexpr std::size_t kVpAtmosGrainSeconds = 25;
constexpr std::size_t kVpAtmosPanSpread = 26;
constexpr std::size_t kVpAtmosDecorrelation = 27;
constexpr std::size_t kVpAtmosFreezeMix = 28;
constexpr std::size_t kVpAtmosDriftSmoothness = 29;
constexpr std::size_t kVpAtmosDriftRangeSemis = 30;
constexpr std::size_t kVpAtmosJitter = 31;
constexpr std::size_t kVpAtmosPositionSeconds = 32;
constexpr std::size_t kVpAtmosPositionSpread = 33;
constexpr std::size_t kVpAtmosPitchSemitones = 34;
constexpr std::size_t kVpAtmosPitchSpread = 35;
constexpr std::size_t kVpAtmosGrainEnvelope = 36;

using VpReadbacks = std::array<double, kVpFields>;

/// The state a voice actually holds, read through the sub-component getters
/// (T003's accessor family plus the six AtmosphereEngine getters that already
/// shipped). NOTHING here reads a smoother's ramp position - every slot is the
/// value its setter stored.
[[nodiscard]] VpReadbacks vpSnapshot(const SeraphisVoice& v)
{
    VpReadbacks s{};
    s[kVpCloudDriftSmoothness] = static_cast<double>(v.cloud().getDriftSmoothness());
    s[kVpCloudDecaySec] = static_cast<double>(v.cloud().getDecayTimeSec());
    s[kVpCloudEnvOffsetSpread] = static_cast<double>(v.cloud().getEnvelopeOffsetSpread());
    s[kVpMorphBloom] = static_cast<double>(v.morph().getBloom());
    s[kVpMorphTravelMode] = static_cast<double>(static_cast<int>(v.getTravelMode()));
    s[kVpMorphTravelRate] = static_cast<double>(v.morph().getTravelRate());
    s[kVpMorphWaypointSeconds] = v.morph().getWaypointInterval();
    s[kVpSpatialRateHz] = static_cast<double>(v.orbit().getRate());
    s[kVpSpatialCoupling] = static_cast<double>(v.orbit().getCoupling());
    s[kVpSpatialGrowth] = static_cast<double>(v.orbit().getGrowth());
    s[kVpEnvMode] = static_cast<double>(static_cast<int>(v.getEnvelopeMode()));
    s[kVpEnvGrowthDurationSec] = static_cast<double>(v.growth().getDuration());
    s[kVpBodyMaterial] = static_cast<double>(static_cast<int>(v.body().getMaterial()));
    s[kVpBodyResonance] = static_cast<double>(v.body().getResonance());
    s[kVpBodyKeyTracking] = static_cast<double>(v.body().getKeyTracking());
    s[kVpBodyDrive] = static_cast<double>(v.body().getDrive());
    s[kVpBodyMix] = static_cast<double>(v.body().getMix());
    s[kVpBodyCloudMix] = static_cast<double>(v.body().getCloudMix());
    s[kVpBodyCloudDecaySec] = static_cast<double>(v.body().getCloudDecaySec());
    s[kVpBodyCloudSize] = static_cast<double>(v.body().getCloudSize());
    s[kVpBodyCloudDamping] = static_cast<double>(v.body().getCloudDamping());
    s[kVpBodyWidth] = static_cast<double>(v.body().getWidth());
    s[kVpBodyInputAgc] = v.body().isInputAgcEnabled() ? 1.0 : 0.0;
    s[kVpBodyResonatorBypass] = v.body().isResonatorBypass() ? 1.0 : 0.0;
    s[kVpAtmosDensity] = static_cast<double>(v.atmos().getDensity());
    s[kVpAtmosGrainSeconds] = static_cast<double>(v.atmos().getGrainSeconds());
    s[kVpAtmosPanSpread] = static_cast<double>(v.atmos().getPanSpread());
    s[kVpAtmosDecorrelation] = static_cast<double>(v.atmos().getDecorrelation());
    s[kVpAtmosFreezeMix] = static_cast<double>(v.atmos().getFreezeMix());
    s[kVpAtmosDriftSmoothness] = static_cast<double>(v.atmos().getDriftSmoothness());
    s[kVpAtmosDriftRangeSemis] = static_cast<double>(v.atmos().getDriftRangeSemitones());
    s[kVpAtmosJitter] = static_cast<double>(v.atmos().getJitter());
    s[kVpAtmosPositionSeconds] = static_cast<double>(v.atmos().getPositionSeconds());
    s[kVpAtmosPositionSpread] = static_cast<double>(v.atmos().getPositionSpread());
    s[kVpAtmosPitchSemitones] = static_cast<double>(v.atmos().getPitchSemitones());
    s[kVpAtmosPitchSpread] = static_cast<double>(v.atmos().getPitchSpread());
    s[kVpAtmosGrainEnvelope] =
        static_cast<double>(static_cast<int>(v.atmos().getGrainEnvelope()));
    return s;
}

/// The SAME thirty-seven quantities taken off the POD, in the SAME order. The
/// two functions together are the FR-001 field->read-back map; a field added to
/// the POD without a row here fails to compile against kFieldCount.
[[nodiscard]] VpReadbacks vpExpected(const SeraphisVoiceParams& p)
{
    VpReadbacks s{};
    s[kVpCloudDriftSmoothness] = static_cast<double>(p.cloudDriftSmoothness);
    s[kVpCloudDecaySec] = static_cast<double>(p.cloudDecaySec);
    s[kVpCloudEnvOffsetSpread] = static_cast<double>(p.cloudEnvOffsetSpread);
    s[kVpMorphBloom] = static_cast<double>(p.morphBloom);
    s[kVpMorphTravelMode] = static_cast<double>(static_cast<int>(p.morphTravelMode));
    s[kVpMorphTravelRate] = static_cast<double>(p.morphTravelRate);
    s[kVpMorphWaypointSeconds] = p.morphWaypointSeconds;
    s[kVpSpatialRateHz] = static_cast<double>(p.spatialRateHz);
    s[kVpSpatialCoupling] = static_cast<double>(p.spatialCoupling);
    s[kVpSpatialGrowth] = static_cast<double>(p.spatialGrowth);
    s[kVpEnvMode] = static_cast<double>(static_cast<int>(p.envMode));
    s[kVpEnvGrowthDurationSec] = static_cast<double>(p.envGrowthDurationSec);
    s[kVpBodyMaterial] = static_cast<double>(static_cast<int>(p.bodyMaterial));
    s[kVpBodyResonance] = static_cast<double>(p.bodyResonance);
    s[kVpBodyKeyTracking] = static_cast<double>(p.bodyKeyTracking);
    s[kVpBodyDrive] = static_cast<double>(p.bodyDrive);
    s[kVpBodyMix] = static_cast<double>(p.bodyMix);
    s[kVpBodyCloudMix] = static_cast<double>(p.bodyCloudMix);
    s[kVpBodyCloudDecaySec] = static_cast<double>(p.bodyCloudDecaySec);
    s[kVpBodyCloudSize] = static_cast<double>(p.bodyCloudSize);
    s[kVpBodyCloudDamping] = static_cast<double>(p.bodyCloudDamping);
    s[kVpBodyWidth] = static_cast<double>(p.bodyWidth);
    s[kVpBodyInputAgc] = p.bodyInputAgc ? 1.0 : 0.0;
    s[kVpBodyResonatorBypass] = p.bodyResonatorBypass ? 1.0 : 0.0;
    s[kVpAtmosDensity] = static_cast<double>(p.atmosDensity);
    s[kVpAtmosGrainSeconds] = static_cast<double>(p.atmosGrainSeconds);
    s[kVpAtmosPanSpread] = static_cast<double>(p.atmosPanSpread);
    s[kVpAtmosDecorrelation] = static_cast<double>(p.atmosDecorrelation);
    s[kVpAtmosFreezeMix] = static_cast<double>(p.atmosFreezeMix);
    s[kVpAtmosDriftSmoothness] = static_cast<double>(p.atmosDriftSmoothness);
    s[kVpAtmosDriftRangeSemis] = static_cast<double>(p.atmosDriftRangeSemis);
    s[kVpAtmosJitter] = static_cast<double>(p.atmosJitter);
    s[kVpAtmosPositionSeconds] = static_cast<double>(p.atmosPositionSeconds);
    s[kVpAtmosPositionSpread] = static_cast<double>(p.atmosPositionSpread);
    s[kVpAtmosPitchSemitones] = static_cast<double>(p.atmosPitchSemitones);
    s[kVpAtmosPitchSpread] = static_cast<double>(p.atmosPitchSpread);
    s[kVpAtmosGrainEnvelope] = static_cast<double>(static_cast<int>(p.atmosGrainEnvelope));
    return s;
}

/// FR-002's one-to-one contract: the named read-back carries the pushed value
/// and every one of the other thirty-six is bit-for-bit where it was. This is
/// what pins plan §1.2's two invertible pairs.
void requireOnlyVpFieldMoved(const VpReadbacks& before, const VpReadbacks& after,
                             std::size_t moved, double expected)
{
    REQUIRE(after[moved] == Approx(expected));
    for (std::size_t i = 0; i < kVpFields; ++i) {
        if (i == moved) {
            continue;
        }
        INFO("sibling read-back index " << i << " moved");
        REQUIRE(after[i] == before[i]);
    }
}

/// Every one of the thirty-seven read-backs equals the POD field that drives it.
void requireVoiceMatches(const SeraphisVoice& v, const SeraphisVoiceParams& p, std::size_t slot)
{
    const VpReadbacks actual = vpSnapshot(v);
    const VpReadbacks expected = vpExpected(p);
    for (std::size_t i = 0; i < kVpFields; ++i) {
        INFO("voice slot " << slot << ", field index " << i);
        REQUIRE(actual[i] == Approx(expected[i]));
    }
}

/// One row per POD field. `mutate` pushes THAT field - and only that field -
/// off its default to a value inside the owning component's clamp range, so the
/// read-back is the pushed value verbatim and no clamp is measured by accident.
struct VpFieldCase {
    const char* name;
    std::size_t index;
    void (*mutate)(SeraphisVoiceParams&);
};

[[nodiscard]] std::array<VpFieldCase, kVpFields> makeVpFieldCases()
{
    return {{
        {.name = "cloudDriftSmoothness",
         .index = kVpCloudDriftSmoothness,
         .mutate = [](SeraphisVoiceParams& p) { p.cloudDriftSmoothness = 0.85f; }},
        // ID 209 -> cloud setDecayTimeSec. The FIRST half of plan §1.2's
        // invertible pair; bodyCloudDecaySec below is the second.
        {.name = "cloudDecaySec",
         .index = kVpCloudDecaySec,
         .mutate = [](SeraphisVoiceParams& p) { p.cloudDecaySec = 3.25f; }},
        {.name = "cloudEnvOffsetSpread",
         .index = kVpCloudEnvOffsetSpread,
         .mutate = [](SeraphisVoiceParams& p) { p.cloudEnvOffsetSpread = 0.4f; }},
        {.name = "morphBloom",
         .index = kVpMorphBloom,
         .mutate = [](SeraphisVoiceParams& p) { p.morphBloom = 0.6f; }},
        {.name = "morphTravelMode",
         .index = kVpMorphTravelMode,
         .mutate =
             [](SeraphisVoiceParams& p) {
                 p.morphTravelMode = SpectralMorphEngine::TravelMode::Spline;
             }},
        {.name = "morphTravelRate",
         .index = kVpMorphTravelRate,
         .mutate = [](SeraphisVoiceParams& p) { p.morphTravelRate = 0.25f; }},
        {.name = "morphWaypointSeconds",
         .index = kVpMorphWaypointSeconds,
         .mutate = [](SeraphisVoiceParams& p) { p.morphWaypointSeconds = 7.5; }},
        {.name = "spatialRateHz",
         .index = kVpSpatialRateHz,
         .mutate = [](SeraphisVoiceParams& p) { p.spatialRateHz = 0.33f; }},
        {.name = "spatialCoupling",
         .index = kVpSpatialCoupling,
         .mutate = [](SeraphisVoiceParams& p) { p.spatialCoupling = 0.45f; }},
        {.name = "spatialGrowth",
         .index = kVpSpatialGrowth,
         .mutate = [](SeraphisVoiceParams& p) { p.spatialGrowth = -0.6f; }},
        {.name = "envMode",
         .index = kVpEnvMode,
         .mutate =
             [](SeraphisVoiceParams& p) { p.envMode = SeraphisVoice::EnvelopeMode::Growth; }},
        {.name = "envGrowthDurationSec",
         .index = kVpEnvGrowthDurationSec,
         .mutate = [](SeraphisVoiceParams& p) { p.envGrowthDurationSec = 25.0f; }},
        {.name = "bodyMaterial",
         .index = kVpBodyMaterial,
         .mutate =
             [](SeraphisVoiceParams& p) {
                 p.bodyMaterial = ContinuousBody::BodyMaterial::Chamber;
             }},
        {.name = "bodyResonance",
         .index = kVpBodyResonance,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyResonance = 0.42f; }},
        {.name = "bodyKeyTracking",
         .index = kVpBodyKeyTracking,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyKeyTracking = 0.31f; }},
        {.name = "bodyDrive",
         .index = kVpBodyDrive,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyDrive = 2.75f; }},
        {.name = "bodyMix",
         .index = kVpBodyMix,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyMix = 0.44f; }},
        {.name = "bodyCloudMix",
         .index = kVpBodyCloudMix,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyCloudMix = 0.66f; }},
        // ID 807 -> body setCloudDecaySec. The SECOND half of the pair: this
        // row must move index 18 and leave index 1 (the cloud's own decay)
        // exactly where prepare() put it.
        {.name = "bodyCloudDecaySec",
         .index = kVpBodyCloudDecaySec,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyCloudDecaySec = 12.5f; }},
        {.name = "bodyCloudSize",
         .index = kVpBodyCloudSize,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyCloudSize = 0.22f; }},
        {.name = "bodyCloudDamping",
         .index = kVpBodyCloudDamping,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyCloudDamping = 0.77f; }},
        // ID 810 -> body setWidth. The voice width BASE (setVoiceWidthBasePercent,
        // ID 604) is MB-routed and deliberately has no POD field; the dedicated
        // section below asserts this row leaves it untouched.
        {.name = "bodyWidth",
         .index = kVpBodyWidth,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyWidth = 0.55f; }},
        {.name = "bodyInputAgc",
         .index = kVpBodyInputAgc,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyInputAgc = false; }},
        {.name = "bodyResonatorBypass",
         .index = kVpBodyResonatorBypass,
         .mutate = [](SeraphisVoiceParams& p) { p.bodyResonatorBypass = true; }},
        {.name = "atmosDensity",
         .index = kVpAtmosDensity,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosDensity = 9.0f; }},
        {.name = "atmosGrainSeconds",
         .index = kVpAtmosGrainSeconds,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosGrainSeconds = 1.5f; }},
        {.name = "atmosPanSpread",
         .index = kVpAtmosPanSpread,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosPanSpread = 0.2f; }},
        {.name = "atmosDecorrelation",
         .index = kVpAtmosDecorrelation,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosDecorrelation = 0.8f; }},
        {.name = "atmosFreezeMix",
         .index = kVpAtmosFreezeMix,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosFreezeMix = 0.35f; }},
        {.name = "atmosDriftSmoothness",
         .index = kVpAtmosDriftSmoothness,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosDriftSmoothness = 0.2f; }},
        {.name = "atmosDriftRangeSemis",
         .index = kVpAtmosDriftRangeSemis,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosDriftRangeSemis = 7.0f; }},
        {.name = "atmosJitter",
         .index = kVpAtmosJitter,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosJitter = 0.9f; }},
        {.name = "atmosPositionSeconds",
         .index = kVpAtmosPositionSeconds,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosPositionSeconds = 12.0f; }},
        {.name = "atmosPositionSpread",
         .index = kVpAtmosPositionSpread,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosPositionSpread = 0.8f; }},
        {.name = "atmosPitchSemitones",
         .index = kVpAtmosPitchSemitones,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosPitchSemitones = -7.0f; }},
        {.name = "atmosPitchSpread",
         .index = kVpAtmosPitchSpread,
         .mutate = [](SeraphisVoiceParams& p) { p.atmosPitchSpread = 0.6f; }},
        {.name = "atmosGrainEnvelope",
         .index = kVpAtmosGrainEnvelope,
         .mutate =
             [](SeraphisVoiceParams& p) {
                 p.atmosGrainEnvelope = GrainEnvelopeType::Blackman;
             }},
    }};
}

/// Every field off its default at once - the payload the fan-out tests push.
[[nodiscard]] SeraphisVoiceParams makeFullyNonDefaultParams()
{
    SeraphisVoiceParams p{};
    for (const VpFieldCase& c : makeVpFieldCases()) {
        c.mutate(p);
    }
    return p;
}

/// The four spectral slots, all distinct, for the FR-005 fan-out.
[[nodiscard]] std::array<SpectralState, 4> makeFourDistinctStates()
{
    return {{makeFactoryState(SpectralStateId::SineStack),
             makeFactoryState(SpectralStateId::Bell), makeFactoryState(SpectralStateId::Glass),
             makeFactoryState(SpectralStateId::Breath)}};
}

/// Per-slot rejection counters, so a delta can be taken across a whole pool.
[[nodiscard]] std::array<std::uint32_t, SeraphisEngine::kMaxVoices> rejectionCounts(
    const SeraphisEngine& e)
{
    std::array<std::uint32_t, SeraphisEngine::kMaxVoices> c{};
    for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
        c[v] = e.getVoice(v).getRejectedConfigureTimeCallCount();
    }
    return c;
}

// =============================================================================
// FR-003 / FR-004 - the per-target base overrides (plan §1.4, §7.1, §7.2)
// =============================================================================

/// The `kRows` base for `target`, re-derived HERE from the public table
/// (seraphis_macro_matrix.h:180) rather than by calling the class's own private
/// literalBaseFor() - which is the very thing under test. The
/// everyRowSharesOneBasePerTarget() static_assert below the class makes the
/// FIRST match unambiguous: rows sharing a target must agree on `base`.
struct BaseLookup {
    bool found = false;
    float base = 0.0f;
};

[[nodiscard]] BaseLookup kRowsBaseFor(SeraphisMacroTarget target) noexcept
{
    for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
        if (row.target == target) {
            return BaseLookup{.found = true, .base = row.base};
        }
    }
    return BaseLookup{};
}

/// The nineteen Voice-owned rows of `apply()` (seraphis_macro_matrix.h:630-658),
/// in that function's own order.
///
/// `value` sits inside the OWNING setter's clamp in every row, so at the FR-060
/// neutral `apply()` must store it VERBATIM and no clamp is measured by
/// accident - which is what licenses the exact `==` in the assertions.
///
/// `read` is nullptr for exactly one target. SpectralMorphEngine writes
/// `targetPosition_` at :352 and its getter block (:392-448) exposes only
/// getTravelPosition() - the SLEW-LIMITED position, not the target - so
/// MorphTargetPosition gets its own section, which renders past convergence.
struct MacroTargetCase {
    const char* name;
    SeraphisMacroTarget target;
    float value;
    float (*read)(const SeraphisVoice&);
};

constexpr std::size_t kNumVoiceMacroTargets = 19;

[[nodiscard]] std::array<MacroTargetCase, kNumVoiceMacroTargets> makeVoiceTargetCases()
{
    return {{
        // -- HarmonicCloud ----------------------------------------------------
        {.name = "CloudInharmonicity",
         .target = SeraphisMacroTarget::CloudInharmonicity,
         .value = 0.012f,  // [0, kMaxInharmonicity = 0.1] (harmonic_cloud.h:430)
         .read = [](const SeraphisVoice& v) { return v.cloud().getInharmonicity(); }},
        {.name = "CloudMutation",
         .target = SeraphisMacroTarget::CloudMutation,
         .value = 0.625f,  // [0, 1] (:456)
         .read = [](const SeraphisVoice& v) { return v.cloud().getMutation(); }},
        {.name = "CloudSpectralGravity",
         .target = SeraphisMacroTarget::CloudSpectralGravity,
         .value = -0.375f,  // [-1, 1] (:482)
         .read = [](const SeraphisVoice& v) { return v.cloud().getSpectralGravity(); }},
        {.name = "CloudRichness",
         .target = SeraphisMacroTarget::CloudRichness,
         .value = 0.875f,  // [0, 1] (:416)
         .read = [](const SeraphisVoice& v) { return v.cloud().getRichness(); }},
        {.name = "CloudSpectralTiltDb",
         .target = SeraphisMacroTarget::CloudSpectralTiltDb,
         .value = -4.5f,  // [-12, +12] dB/oct (:443, :194-195)
         .read = [](const SeraphisVoice& v) { return v.cloud().getSpectralTiltDb(); }},
        {.name = "CloudStereoSpread",
         .target = SeraphisMacroTarget::CloudStereoSpread,
         .value = 0.75f,  // [0, 1] (:539)
         .read = [](const SeraphisVoice& v) { return v.cloud().getStereoSpread(); }},
        {.name = "CloudAttackTimeSec",
         .target = SeraphisMacroTarget::CloudAttackTimeSec,
         .value = 0.75f,  // [kMinAttackSec 0.05, kMaxAttackSec 30] (:560, :218-219)
         .read = [](const SeraphisVoice& v) { return v.cloud().getAttackTimeSec(); }},
        {.name = "CloudDriftDepthCents",
         .target = SeraphisMacroTarget::CloudDriftDepthCents,
         .value = 9.5f,  // [0, kMaxDriftCents = 50] (:505, :214)
         .read = [](const SeraphisVoice& v) { return v.cloud().getDriftDepthCents(); }},

        // -- SpectralMorphEngine ----------------------------------------------
        {.name = "MorphEntropy",
         .target = SeraphisMacroTarget::MorphEntropy,
         .value = 0.4375f,  // [0, 1] (entropy_processor.h:234)
         .read = [](const SeraphisVoice& v) { return v.morph().entropy().getEntropy(); }},
        {.name = "MorphTargetPosition",
         .target = SeraphisMacroTarget::MorphTargetPosition,
         .value = 0.125f,  // [0, numStates-1] = [0, 1] at the shipped count of 2
         .read = nullptr},

        // -- ContinuousBody ----------------------------------------------------
        {.name = "BodyDamping",
         .target = SeraphisMacroTarget::BodyDamping,
         .value = 0.625f,  // [kMinDamping 0, kMaxDamping 1] (continuous_body.h:1175)
         .read = [](const SeraphisVoice& v) { return v.body().getDamping(); }},

        // -- AtmosphereEngine --------------------------------------------------
        {.name = "AtmosLevel",
         .target = SeraphisMacroTarget::AtmosLevel,
         .value = 1.375f,  // [0, kMaxLevel = 2] (atmosphere_engine.h:947, :314)
         .read = [](const SeraphisVoice& v) { return v.atmos().getLevel(); }},
        {.name = "AtmosBlur",
         .target = SeraphisMacroTarget::AtmosBlur,
         .value = 0.28125f,  // [0, 1] (:875)
         .read = [](const SeraphisVoice& v) { return v.atmos().getBlur(); }},
        {.name = "AtmosDriftDepth",
         .target = SeraphisMacroTarget::AtmosDriftDepth,
         .value = 0.71875f,  // [0, 1] (:837)
         .read = [](const SeraphisVoice& v) { return v.atmos().getDriftDepth(); }},

        // -- Spatial -----------------------------------------------------------
        {.name = "SpatialDepth",
         .target = SeraphisMacroTarget::SpatialDepth,
         .value = 0.5625f,  // [0, 1] (orbit_modulator.h:186)
         .read = [](const SeraphisVoice& v) { return v.orbit().getDepth(); }},
        {.name = "VoiceWidth",
         .target = SeraphisMacroTarget::VoiceWidth,
         .value = 130.0f,  // [kMinVoiceWidthPct 50, kMaxVoiceWidthPct 150] (seraphis_voice.h:157-158)
         .read = [](const SeraphisVoice& v) { return v.getVoiceWidthBasePercent(); }},

        // -- Voice envelope ----------------------------------------------------
        // applyStage stores the caller's ms VERBATIM in the shadow
        // (seraphis_voice.h:899), and getEnvelopeStageTimeMs reads that shadow.
        {.name = "EnvStage0Ms",
         .target = SeraphisMacroTarget::EnvStage0Ms,
         .value = 37.5f,
         .read = [](const SeraphisVoice& v) { return v.getEnvelopeStageTimeMs(0); }},
        {.name = "EnvStage1Ms",
         .target = SeraphisMacroTarget::EnvStage1Ms,
         .value = 1250.0f,
         .read = [](const SeraphisVoice& v) { return v.getEnvelopeStageTimeMs(1); }},
        {.name = "EnvReleaseMs",
         .target = SeraphisMacroTarget::EnvReleaseMs,
         .value = 2500.0f,  // releaseMs_ = ms, unclamped (:596)
         .read = [](const SeraphisVoice& v) { return v.getEnvelopeReleaseMs(); }},
    }};
}

/// The eight Aether-owned rows. computeAetherTargets() carries the RAW sum with
/// no clamping of its own (seraphis_macro_matrix.h:662-666), so at the FR-060
/// neutral the override comes back bit-for-bit.
struct AetherTargetCase {
    const char* name;
    SeraphisMacroTarget target;
    float value;
    float SeraphisAetherTargets::*field;
};

[[nodiscard]] std::array<AetherTargetCase, 8> makeAetherTargetCases()
{
    return {{
        {.name = "AetherMix",
         .target = SeraphisMacroTarget::AetherMix,
         .value = 0.71875f,
         .field = &SeraphisAetherTargets::mix},
        {.name = "AetherSize",
         .target = SeraphisMacroTarget::AetherSize,
         .value = 0.1875f,
         .field = &SeraphisAetherTargets::size},
        {.name = "AetherWidth",
         .target = SeraphisMacroTarget::AetherWidth,
         .value = 0.40625f,
         .field = &SeraphisAetherTargets::width},
        {.name = "AetherShimmerOctaveSend",
         .target = SeraphisMacroTarget::AetherShimmerOctaveSend,
         .value = 0.65625f,
         .field = &SeraphisAetherTargets::shimmerOctaveSend},
        {.name = "AetherShimmerFifthSend",
         .target = SeraphisMacroTarget::AetherShimmerFifthSend,
         .value = 0.34375f,
         .field = &SeraphisAetherTargets::shimmerFifthSend},
        {.name = "AetherBloomSend",
         .target = SeraphisMacroTarget::AetherBloomSend,
         .value = 0.875f,
         .field = &SeraphisAetherTargets::bloomSend},
        {.name = "AetherSizeBreathDepth",
         .target = SeraphisMacroTarget::AetherSizeBreathDepth,
         .value = 0.5625f,
         .field = &SeraphisAetherTargets::sizeBreathDepth},
        {.name = "AetherDimensionalityTideDepth",
         .target = SeraphisMacroTarget::AetherDimensionalityTideDepth,
         .value = 0.90625f,
         .field = &SeraphisAetherTargets::dimensionalityTideDepth},
    }};
}

/// A prepared engine with EVERY slot inside the applied polyphony, so
/// `apply()`'s own `i < getPolyphony()` bound (seraphis_macro_matrix.h:625-626)
/// coincides with "every voice" (the plan §7.4 Q6 pinning).
[[nodiscard]] std::unique_ptr<SeraphisEngine> makeFullPolyphonyEngine()
{
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{});
    engine->setPolyphony(SeraphisEngine::kMaxVoices);
    return engine;
}

}  // namespace

// =============================================================================
// FR-070 - thirteen forwarders, each one-to-one
// =============================================================================

/// Plan §1.5. Every forwarder is a SINGLE delegation with no guard of its own,
/// so pushing one must move exactly one component read-back. The sibling sweep
/// is what would catch a copy-paste that wired, say, `setCloudDriftSmoothness`
/// into `atmos_` - which is precisely the collision the `setCloud` / `setAtmos`
/// prefixes exist to prevent (harmonic_cloud.h:513 vs atmosphere_engine.h:844).
///
/// A voice is ~47.6 KB, so it is heap-allocated; Catch2 re-runs this preamble
/// for each SECTION, so every section gets a freshly prepared voice.
TEST_CASE("SeraphisVoice_Phase9Forwarders_AreOneToOne")
{
    auto voice = std::make_unique<SeraphisVoice>();
    voice->prepare(kSr, SeraphisVoiceConfig{});
    const Readbacks before = snapshot(*voice);

    SECTION("setCloudDriftSmoothness reaches HarmonicCloud only")
    {
        voice->setCloudDriftSmoothness(0.9f);
        requireOnlyMoved(before, snapshot(*voice), kIdxCloudDriftSmoothness,
                         static_cast<double>(0.9f));
    }

    SECTION("setEnvelopeOffsetSpread reaches HarmonicCloud only")
    {
        voice->setEnvelopeOffsetSpread(0.75f);
        requireOnlyMoved(before, snapshot(*voice), kIdxEnvelopeOffsetSpread,
                         static_cast<double>(0.75f));
    }

    SECTION("setAtmosDriftSmoothness reaches AtmosphereEngine only")
    {
        voice->setAtmosDriftSmoothness(0.25f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosDriftSmoothness,
                         static_cast<double>(0.25f));
    }

    SECTION("setAtmosDriftRangeSemitones reaches AtmosphereEngine only")
    {
        voice->setAtmosDriftRangeSemitones(7.0f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosDriftRangeSemis, 7.0);
    }

    SECTION("setAtmosJitter reaches AtmosphereEngine only")
    {
        voice->setAtmosJitter(0.9f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosJitter, static_cast<double>(0.9f));
    }

    SECTION("setAtmosPositionSeconds reaches AtmosphereEngine only")
    {
        voice->setAtmosPositionSeconds(12.0f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosPositionSeconds, 12.0);
    }

    SECTION("setAtmosPositionSpread reaches AtmosphereEngine only")
    {
        voice->setAtmosPositionSpread(0.8f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosPositionSpread,
                         static_cast<double>(0.8f));
    }

    SECTION("setAtmosPitchSemitones reaches AtmosphereEngine only")
    {
        voice->setAtmosPitchSemitones(-7.0f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosPitchSemitones, -7.0);
    }

    SECTION("setAtmosPitchSpread reaches AtmosphereEngine only")
    {
        voice->setAtmosPitchSpread(0.6f);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosPitchSpread,
                         static_cast<double>(0.6f));
    }

    SECTION("setAtmosGrainEnvelope reaches AtmosphereEngine only")
    {
        voice->setAtmosGrainEnvelope(GrainEnvelopeType::Blackman);
        requireOnlyMoved(before, snapshot(*voice), kIdxAtmosGrainEnvelope,
                         static_cast<double>(static_cast<int>(GrainEnvelopeType::Blackman)));
    }

    SECTION("setWaypointInterval reaches SpectralMorphEngine only")
    {
        // double, matching the owner's signature (spectral_morph_engine.h:385).
        // 9.0 s sits inside SplineTrajectory's [0.5, 30] clamp, so the store is
        // exact and no clamp is being measured by accident.
        voice->setWaypointInterval(9.0);
        REQUIRE(voice->morph().getWaypointInterval() == 9.0);
        requireOnlyMoved(before, snapshot(*voice), kIdxMorphWaypointInterval, 9.0);
    }

    SECTION("setBodyInputAgcEnabled reaches ContinuousBody only")
    {
        // Shipped default is ON (continuous_body.h:163), so `false` is the move.
        voice->setBodyInputAgcEnabled(false);
        REQUIRE(voice->body().isInputAgcEnabled() == false);
        requireOnlyMoved(before, snapshot(*voice), kIdxBodyInputAgc, 0.0);
    }

    SECTION("setBodyResonatorBypass reaches ContinuousBody only")
    {
        // isResonatorBypass() is the REQUESTED state, stored immediately
        // (continuous_body.h:1305). The 10 ms `bypassPos_` ramp is a separate
        // quantity; an accessor returning it would read `false` here and fail.
        voice->setBodyResonatorBypass(true);
        REQUIRE(voice->body().isResonatorBypass() == true);
        requireOnlyMoved(before, snapshot(*voice), kIdxBodyResonatorBypass, 1.0);
    }
}

// =============================================================================
// FR-072 - the twelve ContinuousBody read-backs
// =============================================================================

/// Plan §1.6. Each accessor is a pure const member read of a value its setter
/// already substituted (non-finite) and clamped, so the accessor's contract is
/// exactly "what the setter stored" - never a smoother's ramp position.
TEST_CASE("ContinuousBody_Phase9Accessors_ReturnClampedStoredValues")
{
    auto body = std::make_unique<ContinuousBody>();
    body->prepare(kSr);
    const auto cases = makeFloatAccessorCases();

    SECTION("an in-range push is stored verbatim")
    {
        for (const FloatAccessorCase& c : cases) {
            INFO(c.name);
            c.push(*body, c.inRange);
            REQUIRE(c.read(*body) == Approx(c.inRange));
        }
    }

    SECTION("an out-of-range push is clamped into the component range")
    {
        for (const FloatAccessorCase& c : cases) {
            INFO(c.name);
            // setResonance(-1.0f) -> 0.0f; setDrive(9.0f) -> 4.0f.
            c.push(*body, c.minValue - 1.0f);
            REQUIRE(c.read(*body) == Approx(c.minValue));
            c.push(*body, c.maxValue + 5.0f);
            REQUIRE(c.read(*body) == Approx(c.maxValue));
        }
    }

    SECTION("a non-finite push substitutes the component default")
    {
        const std::array<std::uint32_t, 3> patterns{
            {kQuietNaNBits, kPosInfBits, kNegInfBits}};
        for (const FloatAccessorCase& c : cases) {
            for (const std::uint32_t bits : patterns) {
                INFO(c.name);
                INFO("non-finite bit pattern " << bits);
                // Move it off the default first, so a setter that simply
                // ignored the argument could not pass by accident.
                c.push(*body, c.inRange);
                REQUIRE(c.read(*body) == Approx(c.inRange));
                c.push(*body, makeNonFinite(bits));
                REQUIRE(c.read(*body) == Approx(c.defaultValue));
            }
        }
    }

    SECTION("the two boolean read-backs carry the requested state")
    {
        REQUIRE(body->isInputAgcEnabled() == ContinuousBody::kDefaultAgcEnabled);
        REQUIRE(body->isResonatorBypass() == ContinuousBody::kDefaultResonatorBypass);

        body->setInputAgcEnabled(false);
        REQUIRE(body->isInputAgcEnabled() == false);
        body->setInputAgcEnabled(true);
        REQUIRE(body->isInputAgcEnabled() == true);

        // setResonatorBypass self-guards on an unchanged value
        // (continuous_body.h:1302-1304); the stored request still tracks.
        body->setResonatorBypass(true);
        REQUIRE(body->isResonatorBypass() == true);
        body->setResonatorBypass(true);
        REQUIRE(body->isResonatorBypass() == true);
        body->setResonatorBypass(false);
        REQUIRE(body->isResonatorBypass() == false);
    }

    SECTION("getDrive is the pushed user drive, getDriveGain the smoothed engine gain")
    {
        // getDriveGain() is exp10Fast(slot.driveLog10.getCurrentValue())
        // (continuous_body.h:1480-1484). setDrive stores userDrive_ raw (:1205)
        // and does NOT retarget that smoother - only a control step does
        // (updateDrive, :3247-3252). So with no block rendered the two are
        // observably different quantities.
        const float gainBefore = body->getDriveGain();

        // Chosen at run time so it provably differs from the un-settled gain,
        // while staying inside [kMinUserDrive, kMaxUserDrive].
        const float pushed = (gainBefore < 2.0f) ? 3.5f : 0.5f;
        body->setDrive(pushed);

        REQUIRE(body->getDrive() == Approx(pushed));
        REQUIRE(body->getDriveGain() == Approx(gainBefore));
        REQUIRE(body->getDriveGain() != Approx(body->getDrive()));
    }
}

// =============================================================================
// FR-001 - the broadcast POD
// =============================================================================

/// Plan §1.1. The POD's default member initializers are not free-standing
/// literals: they are the SHIPPED VOICE DEFAULTS, so pushing a
/// default-constructed SeraphisVoiceParams into a freshly prepared voice must
/// be observably a no-op (that is the property SC-002 leans on). Twenty-nine of
/// them come from SeraphisVoice::prepare()'s steps 6/7 (seraphis_voice.h:
/// 284-364); the eight 2026-08-01 additions are never touched by prepare() and
/// come from the owning component's own member initializer -
/// atmosphere_engine.h:2352-2356 (jitter 0.5, position 1.0, positionSpread 0.3,
/// pitch 0.0, pitchSpread 0.15) and :2292 (envelope Hann), continuous_body.h:
/// 163-164 (AGC on, resonator bypass off).
TEST_CASE("SeraphisVoiceParams_DefaultsMatchPreparedVoice")
{
    STATIC_REQUIRE(SeraphisVoiceParams::kFieldCount == 37);
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeraphisVoiceParams>);

    auto voice = std::make_unique<SeraphisVoice>();
    voice->prepare(kSr, SeraphisVoiceConfig{});

    const VpReadbacks prepared = vpSnapshot(*voice);
    const VpReadbacks defaults = vpExpected(SeraphisVoiceParams{});

    for (std::size_t i = 0; i < kVpFields; ++i) {
        INFO("field index " << i);
        REQUIRE(prepared[i] == Approx(defaults[i]));
    }

    // The eight 2026-08-01 additions, named individually so a wrong literal
    // reports as itself rather than as "field index 31".
    const SeraphisVoiceParams p{};
    REQUIRE(p.atmosJitter == Approx(0.5f));
    REQUIRE(p.atmosPositionSeconds == Approx(1.0f));
    REQUIRE(p.atmosPositionSpread == Approx(0.3f));
    REQUIRE(p.atmosPitchSemitones == Approx(0.0f));
    REQUIRE(p.atmosPitchSpread == Approx(0.15f));
    REQUIRE(p.atmosGrainEnvelope == GrainEnvelopeType::Hann);
    REQUIRE(p.bodyInputAgc == ContinuousBody::kDefaultAgcEnabled);
    REQUIRE(p.bodyResonatorBypass == ContinuousBody::kDefaultResonatorBypass);
}

// =============================================================================
// FR-002 - one field, one setter
// =============================================================================

/// Plan §1.2. Thirty-seven sections, one per POD field: push THAT field off its
/// default, broadcast, and assert its own read-back moved and the other
/// thirty-six did not. The sibling sweep is what catches a transposed setter -
/// which is the whole reason plan §1.2 calls out two invertible pairs.
///
/// The engine is ~772 KB, so it is heap-allocated (the STACK RULE above);
/// Catch2 re-runs this preamble for each section, so every section broadcasts
/// into a freshly prepared pool.
TEST_CASE("SeraphisVoiceParams_MapsEveryFieldToItsOwnSetter")
{
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{});

    const VpReadbacks before = vpSnapshot(engine->getVoice(0));
    // Plan §1.2's second collision, pinned from the other side: ID 810 is the
    // BODY width (ContinuousBody::setWidth, read back at index 21). The VOICE
    // width base (setVoiceWidthBasePercent, ID 604, seraphis_voice.h:334) is
    // MB-routed and is deliberately NOT a POD field, so NO row may disturb it.
    const float widthBaseBefore = engine->getVoice(0).getVoiceWidthBasePercent();
    REQUIRE(widthBaseBefore == Approx(100.0f));

    for (const VpFieldCase& c : makeVpFieldCases()) {
        DYNAMIC_SECTION(c.name << " reaches its own setter only")
        {
            SeraphisVoiceParams p{};
            c.mutate(p);
            // Sanity: the row actually moves something. A row whose value
            // equalled the default would pass the sweep vacuously.
            REQUIRE(vpExpected(p)[c.index] != before[c.index]);

            engine->applyVoiceParams(p);
            requireOnlyVpFieldMoved(before, vpSnapshot(engine->getVoice(0)), c.index,
                                    vpExpected(p)[c.index]);
            REQUIRE(engine->getVoice(0).getVoiceWidthBasePercent()
                    == Approx(widthBaseBefore));
        }
    }
}

// =============================================================================
// FR-002 - the bound is kMaxVoices, not getPolyphony()
// =============================================================================

/// Plan §1.2's banner, stated as a test. A getPolyphony() bound would leave the
/// slots above the current polyphony on prepare-time defaults - both the
/// post-shrink orphan tail (audibly summed for its whole release) and any slot
/// the allocator hands out after a later polyphony INCREASE.
TEST_CASE("SeraphisEngine_ApplyVoiceParams_ReachesAllSixteenSlots")
{
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{});  // shipped default polyphony is 8
    REQUIRE(engine->getPolyphony() == std::size_t{8});

    const SeraphisVoiceParams p = makeFullyNonDefaultParams();

    SECTION("every slot takes the broadcast, including the eight above polyphony")
    {
        engine->applyVoiceParams(p);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            requireVoiceMatches(engine->getVoice(v), p, v);
        }
    }

    SECTION("a post-shrink orphan tail is configured too")
    {
        // Six notes fill six slots of the eight, so the shrink to 4 is
        // guaranteed to strand at least one still-ringing slot at index >= 4.
        for (int i = 0; i < 6; ++i) {
            engine->noteOn(static_cast<std::uint8_t>(60 + i), std::uint8_t{100});
        }
        engine->setPolyphony(4);
        REQUIRE(engine->getPolyphony() == std::size_t{4});

        engine->applyVoiceParams(p);
        for (std::size_t v = 4; v < SeraphisEngine::kMaxVoices; ++v) {
            requireVoiceMatches(engine->getVoice(v), p, v);
        }
        // ...and the four inside the new polyphony are not skipped either.
        for (std::size_t v = 0; v < 4; ++v) {
            requireVoiceMatches(engine->getVoice(v), p, v);
        }
    }
}

// =============================================================================
// FR-005 - the configure-time spectral fan-out
// =============================================================================

/// Plan §1.3. All four slots are written on every accepting voice, not `count`
/// of them, and the bound is kMaxVoices for the same reason applyVoiceParams'
/// is. applySpectralStates adds no guard of its own and swallows no rejection.
/// Phase 11 D-1 / FR-033a removed the per-voice configure-time gate from
/// setSpectralState/setSpectralStateCount (seraphis_voice.h:787-793), so NO
/// voice rejects a state push any more - sounding or not.
TEST_CASE("SeraphisEngine_ApplySpectralStates_WritesAllFourSlotsToAllSixteenVoices")
{
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{});
    const std::array<SpectralState, 4> states = makeFourDistinctStates();

    SECTION("a quiescent pool takes count and all four slots on all sixteen voices")
    {
        engine->applySpectralStates(states.data(), 4);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 4);
            REQUIRE(engine->getVoice(v).getRejectedConfigureTimeCallCount() == 0u);
        }
    }

    // Phase 11 D-1 / FR-033a REPLACES Phase 9's "a sounding voice rejects
    // exactly five calls and the rest still accept". A sounding voice now takes
    // the push like every other slot; nothing rejects, so the pool converges on
    // the FIRST application instead of on the first quiescent block.
    SECTION("FR-033a: a sounding voice accepts too - nothing rejects, all sixteen take it")
    {
        engine->noteOn(std::uint8_t{60}, std::uint8_t{100});
        // Non-vacuity: one slot really is non-idle, i.e. really is in the state
        // the removed gate used to reject (hasSounded_ closes in noteOn,
        // seraphis_voice.h:516).
        REQUIRE(engine->getActiveVoiceCount() == std::size_t{1});
        const auto before = rejectionCounts(*engine);

        engine->applySpectralStates(states.data(), 4);
        const auto after = rejectionCounts(*engine);

        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            REQUIRE(after[v] == before[v]);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 4);
        }
    }

    SECTION("voiceMask selects which slots are written")
    {
        // The DEFAULT argument is the whole pool.
        engine->applySpectralStates(states.data(), 3);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 3);
        }
        const auto before = rejectionCounts(*engine);

        // A one-bit mask touches voice 0 and nothing else - not even the
        // rejection counters, which a masked-out voice cannot move because it
        // is never called.
        engine->applySpectralStates(states.data(), 4, std::uint16_t{0x0001u});
        REQUIRE(engine->getVoice(0).morph().getStateCount() == 4);
        for (std::size_t v = 1; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 3);
            REQUIRE(engine->getVoice(v).getRejectedConfigureTimeCallCount() == before[v]);
        }
    }

    SECTION("a null pointer is a no-op")
    {
        engine->applySpectralStates(nullptr, 4);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 2);
            REQUIRE(engine->getVoice(v).getRejectedConfigureTimeCallCount() == 0u);
        }
    }
}

// =============================================================================
// FR-003 / FR-004 - the per-target base overrides COMPOSE with the macros
// =============================================================================

/// Plan §1.4. `setTargetBase(t, v)` replaces the `kRows` literal evaluateAll()
/// seeds target `t` from, so at the FR-060 macro neutral - where every
/// contribution is exactly 0 by construction (seraphis_macro_matrix.h:777-781) -
/// the value that reaches the DSP IS `v`, unrescaled. Spec C-1 accepts the
/// resulting saturation against a deep extreme; no headroom rescaling exists to
/// test for.
///
/// The engines are ~826 KB, so they are heap-allocated (the STACK RULE above).
/// A SeraphisMacroMatrix is ~155 B and stays on the stack.
TEST_CASE("SeraphisMacroMatrix_TargetBaseOverride_Composes")
{
    SECTION("setTargetBase round-trips through getTargetBase for all 27 targets")
    {
        SeraphisMacroMatrix macros;

        // A DISTINCT value per target - all exactly representable in binary32 -
        // so a getter that read the wrong slot could not pass by coincidence.
        const auto probeFor = [](std::size_t i) {
            return 0.125f + 0.0625f * static_cast<float>(i);
        };

        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            const auto t = static_cast<SeraphisMacroTarget>(i);
            INFO("target index " << i);
            macros.setTargetBase(t, probeFor(i));
            REQUIRE(macros.getTargetBase(t) == probeFor(i));
        }

        // ...and every slot still reads its OWN value after all 27 writes.
        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            INFO("target index " << i);
            REQUIRE(macros.getTargetBase(static_cast<SeraphisMacroTarget>(i)) == probeFor(i));
        }
    }

    SECTION("apply() at the FR-060 neutral writes exactly the override into every voice")
    {
        auto engine = makeFullPolyphonyEngine();
        REQUIRE(engine->getPolyphony() == SeraphisEngine::kMaxVoices);

        SeraphisMacroMatrix macros;  // dream/bloom/dissolve/entropy 0, gravity 0.5
        const auto cases = makeVoiceTargetCases();

        for (const MacroTargetCase& c : cases) {
            const BaseLookup literal = kRowsBaseFor(c.target);
            INFO(c.name);
            REQUIRE(literal.found);
            // Non-vacuity: an override equal to the literal would pass this
            // section even if setTargetBase did nothing at all.
            REQUIRE(c.value != literal.base);
            macros.setTargetBase(c.target, c.value);
            REQUIRE(macros.getTargetBase(c.target) == c.value);
        }

        macros.apply(*engine);

        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (const MacroTargetCase& c : cases) {
                if (c.read == nullptr) {
                    continue;  // MorphTargetPosition - its own section below
                }
                INFO("voice " << v << ", target " << c.name);
                REQUIRE(c.read(engine->getVoice(v)) == c.value);
            }
        }
    }

    SECTION("MorphTargetPosition composes too, seen through the converged travel position")
    {
        // The one Voice-owned target with no direct read-back:
        // SpectralMorphEngine stores targetPosition_ at
        // spectral_morph_engine.h:352 and its getter block (:392-448) exposes
        // only getTravelPosition(), the slew-limited position_. advanceTravel
        // snaps position_ EXACTLY onto the target once |delta| <= cap
        // (:718-724), so rendering past convergence makes the slewed read-back
        // an exact one.
        auto engine = std::make_unique<SeraphisEngine>();
        engine->prepare(kSr, SeraphisEngineConfig{});

        // The push must come AFTER the note-on: SeraphisVoice::reset() calls
        // morph_.reset(), which rewinds targetPosition_ to 0 (:249-251).
        engine->noteOn(std::uint8_t{60}, std::uint8_t{100});

        // kMaxTravelRate (1 journey/s) with the shipped two-state count gives a
        // slew cap of 1.0 units/s, so 0.125 converges in 0.125 s. Every other
        // POD field is at its shipped-voice default, so this push moves the
        // travel rate and nothing else.
        SeraphisVoiceParams p{};
        p.morphTravelRate = SpectralMorphEngine::kMaxTravelRate;
        engine->applyVoiceParams(p);

        SeraphisMacroMatrix macros;
        constexpr float kPosition = 0.125f;
        const BaseLookup literal = kRowsBaseFor(SeraphisMacroTarget::MorphTargetPosition);
        REQUIRE(literal.found);
        REQUIRE(kPosition != literal.base);
        macros.setTargetBase(SeraphisMacroTarget::MorphTargetPosition, kPosition);
        REQUIRE(macros.getTargetBase(SeraphisMacroTarget::MorphTargetPosition) == kPosition);
        macros.apply(*engine);

        // 0.5 s is 4x the convergence time. Nothing in the render path re-pushes
        // a travel target (SeraphisEngine names setTargetPosition nowhere), so
        // the override stands for the whole window.
        constexpr std::size_t kBlock = 512;
        std::array<float, kBlock> outL{};
        std::array<float, kBlock> outR{};
        const auto totalSamples = static_cast<std::size_t>(0.5 * kSr);
        for (std::size_t done = 0; done < totalSamples; done += kBlock) {
            engine->processStereoBlock(outL.data(), outR.data(), kBlock);
        }

        std::size_t sounding = 0;
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            if (engine->getVoiceState(v) == VoiceState::Idle) {
                continue;
            }
            ++sounding;
            INFO("voice " << v);
            REQUIRE(engine->getVoice(v).morph().getStateCount() == 2);
            REQUIRE(engine->getVoice(v).morph().getTravelPosition() == kPosition);
        }
        REQUIRE(sounding >= std::size_t{1});
    }

    SECTION("computeAetherTargets() returns exactly the override for the eight Aether rows")
    {
        SeraphisMacroMatrix macros;
        const auto cases = makeAetherTargetCases();

        for (const AetherTargetCase& c : cases) {
            const BaseLookup literal = kRowsBaseFor(c.target);
            INFO(c.name);
            REQUIRE(literal.found);
            REQUIRE(c.value != literal.base);
            macros.setTargetBase(c.target, c.value);
        }

        const SeraphisAetherTargets out = macros.computeAetherTargets();
        for (const AetherTargetCase& c : cases) {
            INFO(c.name);
            REQUIRE(out.*(c.field) == c.value);
        }
    }

    SECTION("resetTargetBases() restores the kRows literal for all 27")
    {
        SeraphisMacroMatrix macros;
        constexpr float kProbe = 0.375f;  // exact in binary32; not any kRows base

        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            macros.setTargetBase(static_cast<SeraphisMacroTarget>(i), kProbe);
        }
        macros.resetTargetBases();

        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            const auto t = static_cast<SeraphisMacroTarget>(i);
            const BaseLookup literal = kRowsBaseFor(t);
            INFO("target index " << i);
            REQUIRE(literal.found);
            REQUIRE(kProbe != literal.base);  // the restore is not vacuous
            REQUIRE(macros.getTargetBase(t) == literal.base);
        }

        // The DSP side agrees: after the reset, apply() lands exactly what a
        // matrix that was NEVER overridden lands.
        auto restored = makeFullPolyphonyEngine();
        macros.apply(*restored);

        auto pristine = makeFullPolyphonyEngine();
        const SeraphisMacroMatrix neverOverridden;
        neverOverridden.apply(*pristine);

        const auto cases = makeVoiceTargetCases();
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (const MacroTargetCase& c : cases) {
                if (c.read == nullptr) {
                    continue;
                }
                INFO("voice " << v << ", target " << c.name);
                REQUIRE(c.read(restored->getVoice(v)) == c.read(pristine->getVoice(v)));
            }
        }

        const SeraphisAetherTargets restoredAether = macros.computeAetherTargets();
        const SeraphisAetherTargets pristineAether = neverOverridden.computeAetherTargets();
        for (const AetherTargetCase& c : makeAetherTargetCases()) {
            INFO(c.name);
            REQUIRE(restoredAether.*(c.field) == pristineAether.*(c.field));
        }
    }

    SECTION("a non-finite argument leaves the stored base unchanged")
    {
        // NEVER std::numeric_limits<float>::quiet_NaN() / infinity(): the macOS
        // CI leg builds -ffast-math and folds both to finite garbage before
        // setTargetBase's own bit-pattern guard (seraphis_macro_matrix.h:748-752)
        // could ever see them.
        const std::array<std::uint32_t, 3> patterns{{kQuietNaNBits, kPosInfBits, kNegInfBits}};
        constexpr float kKept = 0.4375f;

        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            const auto t = static_cast<SeraphisMacroTarget>(i);
            const BaseLookup literal = kRowsBaseFor(t);
            INFO("target index " << i);
            REQUIRE(literal.found);

            // (a) with NO override in place the kRows literal survives, and no
            //     override is silently installed behind it.
            SeraphisMacroMatrix fresh;
            for (const std::uint32_t bits : patterns) {
                INFO("non-finite bit pattern " << bits);
                fresh.setTargetBase(t, makeNonFinite(bits));
                REQUIRE(fresh.getTargetBase(t) == literal.base);
            }

            // (b) with an override in place, THAT value survives - so the
            //     rejection is a rejection, not a reset.
            SeraphisMacroMatrix overridden;
            overridden.setTargetBase(t, kKept);
            REQUIRE(kKept != literal.base);
            for (const std::uint32_t bits : patterns) {
                INFO("non-finite bit pattern " << bits);
                overridden.setTargetBase(t, makeNonFinite(bits));
                REQUIRE(overridden.getTargetBase(t) == kKept);
            }
        }
    }

    SECTION("an out-of-range target is ignored by both accessors")
    {
        SeraphisMacroMatrix macros;
        const auto outOfRange =
            static_cast<SeraphisMacroTarget>(SeraphisMacroMatrix::kNumTargets);

        macros.setTargetBase(outOfRange, 0.75f);
        REQUIRE(macros.getTargetBase(outOfRange) == 0.0f);

        // ...and no in-range slot was written through by the rejected call.
        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            const auto t = static_cast<SeraphisMacroTarget>(i);
            const BaseLookup literal = kRowsBaseFor(t);
            INFO("target index " << i);
            REQUIRE(literal.found);
            REQUIRE(macros.getTargetBase(t) == literal.base);
        }
    }
}

// =============================================================================
// SC-002 clause 4 - the negative control
// =============================================================================

/// Plan §7.1 clause 4. FR-003's storage is ADDITIVE: a default-constructed
/// matrix has `hasOverride_` all false, so evaluateAll() evaluates the identical
/// expression it did in Phase 7 and apply() / computeAetherTargets() are
/// unchanged at the shipped defaults. This is the case that would go red if the
/// override members were ever seeded with anything other than "absent".
TEST_CASE("SeraphisMacroMatrix_DefaultBases_Unchanged")
{
    SECTION("every one of the 27 targets reports its kRows literal")
    {
        const SeraphisMacroMatrix macros;
        for (std::size_t i = 0; i < SeraphisMacroMatrix::kNumTargets; ++i) {
            const auto t = static_cast<SeraphisMacroTarget>(i);
            const BaseLookup literal = kRowsBaseFor(t);
            INFO("target index " << i);
            REQUIRE(literal.found);
            REQUIRE(macros.getTargetBase(t) == literal.base);
        }
    }

    SECTION("apply() at the FR-060 neutral is unmoved by resetTargetBases()")
    {
        auto engine = makeFullPolyphonyEngine();
        REQUIRE(engine->getPolyphony() == SeraphisEngine::kMaxVoices);

        SeraphisMacroMatrix macros;  // never overridden
        macros.apply(*engine);

        const auto cases = makeVoiceTargetCases();
        std::array<std::array<float, kNumVoiceMacroTargets>, SeraphisEngine::kMaxVoices> before{};
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (std::size_t i = 0; i < cases.size(); ++i) {
                before[v][i] =
                    (cases[i].read == nullptr) ? 0.0f : cases[i].read(engine->getVoice(v));
            }
        }
        const SeraphisAetherTargets aetherBefore = macros.computeAetherTargets();

        macros.resetTargetBases();
        macros.apply(*engine);

        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (std::size_t i = 0; i < cases.size(); ++i) {
                if (cases[i].read == nullptr) {
                    continue;
                }
                INFO("voice " << v << ", target " << cases[i].name);
                REQUIRE(cases[i].read(engine->getVoice(v)) == before[v][i]);
            }
        }

        // ...and every read-back is still the table's own literal, which is what
        // makes this a negative control rather than a self-comparison.
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (const MacroTargetCase& c : cases) {
                if (c.read == nullptr) {
                    continue;
                }
                const BaseLookup literal = kRowsBaseFor(c.target);
                INFO("voice " << v << ", target " << c.name);
                REQUIRE(literal.found);
                REQUIRE(c.read(engine->getVoice(v)) == literal.base);
            }
        }

        const SeraphisAetherTargets aetherAfter = macros.computeAetherTargets();
        for (const AetherTargetCase& c : makeAetherTargetCases()) {
            const BaseLookup literal = kRowsBaseFor(c.target);
            INFO(c.name);
            REQUIRE(literal.found);
            REQUIRE(aetherAfter.*(c.field) == aetherBefore.*(c.field));
            REQUIRE(aetherAfter.*(c.field) == literal.base);
        }
    }
}
