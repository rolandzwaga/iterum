// ==============================================================================
// Seraphis - Version-2 state tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.10, §7.13)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-010  a v2 stream round-trips every registered parameter
//   SC-011  a v1 stream still loads (FR-093's migration), with the Phase 9
//           fields taking their registered defaults
//   SC-012  a truncated / over-long / non-finite stream is rejected or
//           EOF-recovered without corrupting live state
//   SC-023  a preset loaded into an ALREADY-PREPARED processor reaches the DSP,
//           against §7.13's own all-non-default value table
//
// COMPILE FLAGS: this TU IS listed under "-fno-fast-math -fno-finite-math-only"
//   in plugins/seraphis/tests/CMakeLists.txt, because SC-012 injects non-finite
//   payloads and needs IEEE semantics to assert on the rejection.
//
// ------------------------------------------------------------------------------
// TWO DOCUMENTED CONSTRUCTION CHOICES, both stated here rather than discovered
// in review:
//
// 1. THE NON-DEFAULT TABLE IS A RULE, NOT A TRANSCRIBED VALUE COLUMN.
//    Plan §7.13 asks for a constexpr array of {id, plainValue} plus a per-row
//    check that every value differs from that ID's registered default. The table
//    below is a constexpr array of {id, kind, entries} - the 91 rows of spec C-6
//    - and the value for each row is DERIVED from the ID's own registered
//    default at run time:
//        R rows  ->  normalized (defaultNormalized <= 0.5) ? 0.87 : 0.13
//        L/T     ->  index      (defaultIndex == 0) ? (entries - 1) : 0
//    That is strictly stronger than a transcribed column: a row can never
//    silently BE its own default (the failure §7.13's construction check exists
//    to catch), and the check is still asserted per row over all 91 rows against
//    Vst::ParameterInfo::defaultNormalizedValue - the live registered default,
//    not a hand-copied one. TWO rows are PINNED rather than derived, because
//    §7.13 names their rendered observables: kAetherPreDelayId (1207) at
//    normalized 1.0 == 200 ms (aether_params.h:48) and kAetherDecayId (1203) at
//    normalized 1.0 == 60 s (:46).
//
// 2. THE `AE` RENDERED OBSERVABLES ARE DIRECTIONAL.
//    AetherReverb ships no getter for any of the ten AE controls, so
//    "applyAetherParamsCallCountForTest() incremented" alone passes for ANY
//    loaded value (§7.13). The two mandated observables are therefore rendered
//    here as a MATCHED PAIR - two freshly prepared processors that receive
//    streams differing ONLY in 1207 (or only in 1203) - and asserted
//    DIRECTIONALLY (onset strictly later; tail strictly louder) rather than
//    against an absolute 200 ms / 60 s figure. A directional assertion over a
//    matched pair closes exactly the hole §7.13 names (a self-inverse
//    save/load field swap) without pinning a number to a 4 s stochastic
//    granular + reverb render, which this repo has broken three times.
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"

#include "plugin_ids.h"
#include "seraphis_test_fixture.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using Catch::Approx;

namespace {

// =============================================================================
// Stream plumbing
// =============================================================================

/// Spec C-8 / plan §5.1. 73 floats + 18 int32 + the version int32 + 4 x 541.
constexpr int32 kV2StateBytes = 2532;
/// Phase 8's layout, which is a STRICT PREFIX of v2 (plan §5.1).
constexpr int32 kV1StateBytes = 36;

static_assert(73 * 4 + 18 * 4 + 4 + 4 * 541 == kV2StateBytes,
              "SC-010: the v2 arithmetic of plan 5.1 must reproduce 2532");

struct StreamReleaser {
    void operator()(MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<MemoryStream, StreamReleaser>;

[[nodiscard]] StreamPtr makeStream() { return StreamPtr(new MemoryStream()); }

void rewindStream(MemoryStream& s) { s.seek(0, IBStream::kIBSeekSet, nullptr); }

[[nodiscard]] StreamPtr captureState(Seraphis::Processor& proc) {
    StreamPtr s = makeStream();
    REQUIRE(proc.getState(s.get()) == kResultOk);
    rewindStream(*s);
    return s;
}

/// The first `n` bytes of `full`, as an independent stream positioned at 0.
[[nodiscard]] StreamPtr makeTruncatedStream(MemoryStream& full, int32 n) {
    StreamPtr s = makeStream();
    if (n > 0) {
        int32 written = 0;
        s->write(full.getData(), n, &written);
        REQUIRE(written == n);
    }
    rewindStream(*s);
    return s;
}

/// A hand-built 36-byte VERSION-1 stream: version | float gain | int32 poly |
/// int32 soft | five macro floats. Nothing else - that is the whole point.
[[nodiscard]] StreamPtr makeV1Stream(int32 version, float gain, int32 poly, int32 soft,
                                     float dream, float bloom, float dissolve,
                                     float gravity, float entropy) {
    StreamPtr s = makeStream();
    {
        IBStreamer w(s.get(), kLittleEndian);
        w.writeInt32(version);
        w.writeFloat(gain);
        w.writeInt32(poly);
        w.writeInt32(soft);
        w.writeFloat(dream);
        w.writeFloat(bloom);
        w.writeFloat(dissolve);
        w.writeFloat(gravity);
        w.writeFloat(entropy);
    }
    rewindStream(*s);
    return s;
}

// =============================================================================
// Spec C-6, all 91 rows (the same grouping as unit/parameter_surface_test.cpp)
// =============================================================================

/// C-6's *Type* column. `R` = plain Vst::Parameter, `L` = StringListParameter,
/// `T` = stepped toggle (stepCount == 1, i.e. two states).
enum class Kind { R, L, T };

struct SurfaceRow {
    Vst::ParamID id;
    Kind kind;
    /// Entry count for `L` rows; 0 otherwise.
    int entries;
};

constexpr SurfaceRow kSurface[] = {
    // --- Global (0-99) -------------------------------------------------------
    {Seraphis::kMasterGainId, Kind::R, 0},
    {Seraphis::kPolyphonyId, Kind::L, 16},
    {Seraphis::kSoftLimitId, Kind::T, 0},
    {Seraphis::kSeedId, Kind::L, 16},

    // --- Macros (100-199) ----------------------------------------------------
    {Seraphis::kMacroDreamId, Kind::R, 0},
    {Seraphis::kMacroBloomId, Kind::R, 0},
    {Seraphis::kMacroDissolveId, Kind::R, 0},
    {Seraphis::kMacroGravityId, Kind::R, 0},
    {Seraphis::kMacroEntropyId, Kind::R, 0},

    // --- Harmonic Cloud (200-399) -------------------------------------------
    {Seraphis::kCloudRichnessId, Kind::R, 0},
    {Seraphis::kCloudInharmonicityId, Kind::R, 0},
    {Seraphis::kCloudTiltId, Kind::R, 0},
    {Seraphis::kCloudMutationId, Kind::R, 0},
    {Seraphis::kCloudGravityId, Kind::R, 0},
    {Seraphis::kCloudDriftDepthId, Kind::R, 0},
    {Seraphis::kCloudDriftSmoothnessId, Kind::R, 0},
    {Seraphis::kCloudStereoSpreadId, Kind::R, 0},
    {Seraphis::kCloudAttackId, Kind::R, 0},
    {Seraphis::kCloudDecayId, Kind::R, 0},
    {Seraphis::kCloudEnvOffsetSpreadId, Kind::R, 0},

    // --- Spectral Morph / Entropy (400-599) ---------------------------------
    {Seraphis::kMorphEntropyId, Kind::R, 0},
    {Seraphis::kMorphBloomId, Kind::R, 0},
    {Seraphis::kMorphPositionId, Kind::R, 0},
    {Seraphis::kMorphTravelModeId, Kind::L, 2},
    {Seraphis::kMorphTravelRateId, Kind::R, 0},
    {Seraphis::kMorphSyncId, Kind::T, 0},
    {Seraphis::kMorphSyncNoteId, Kind::L, 8},
    {Seraphis::kMorphWaypointIntervalId, Kind::R, 0},
    {Seraphis::kMorphStateCountId, Kind::L, 3},
    {Seraphis::kMorphState0Id, Kind::L, 5},
    {Seraphis::kMorphState1Id, Kind::L, 5},
    {Seraphis::kMorphState2Id, Kind::L, 5},
    {Seraphis::kMorphState3Id, Kind::L, 5},

    // --- Life Modulators (600-699) + Voice Envelope (700-799) ---------------
    {Seraphis::kLifeSpatialDepthId, Kind::R, 0},
    {Seraphis::kLifeSpatialRateId, Kind::R, 0},
    {Seraphis::kLifeSpatialCouplingId, Kind::R, 0},
    {Seraphis::kLifeSpatialGrowthId, Kind::R, 0},
    {Seraphis::kLifeVoiceWidthId, Kind::R, 0},
    {Seraphis::kEnvModeId, Kind::L, 2},
    {Seraphis::kEnvGrowthDurationId, Kind::R, 0},
    {Seraphis::kEnvStage0MsId, Kind::R, 0},
    {Seraphis::kEnvStage1MsId, Kind::R, 0},
    {Seraphis::kEnvReleaseMsId, Kind::R, 0},

    // --- Continuous Body (800-999) ------------------------------------------
    {Seraphis::kBodyMaterialId, Kind::L, 5},
    {Seraphis::kBodyResonanceId, Kind::R, 0},
    {Seraphis::kBodyDampingId, Kind::R, 0},
    {Seraphis::kBodyKeyTrackingId, Kind::R, 0},
    {Seraphis::kBodyDriveId, Kind::R, 0},
    {Seraphis::kBodyMixId, Kind::R, 0},
    {Seraphis::kBodyCloudMixId, Kind::R, 0},
    {Seraphis::kBodyCloudDecayId, Kind::R, 0},
    {Seraphis::kBodyCloudSizeId, Kind::R, 0},
    {Seraphis::kBodyCloudDampingId, Kind::R, 0},
    {Seraphis::kBodyWidthId, Kind::R, 0},
    {Seraphis::kBodyInputAgcId, Kind::T, 0},
    {Seraphis::kBodyResonatorBypassId, Kind::T, 0},

    // --- Granular Atmosphere (1000-1199) ------------------------------------
    {Seraphis::kAtmosLevelId, Kind::R, 0},
    {Seraphis::kAtmosBlurId, Kind::R, 0},
    {Seraphis::kAtmosDensityId, Kind::R, 0},
    {Seraphis::kAtmosGrainSecondsId, Kind::R, 0},
    {Seraphis::kAtmosDriftDepthId, Kind::R, 0},
    {Seraphis::kAtmosPanSpreadId, Kind::R, 0},
    {Seraphis::kAtmosDecorrelationId, Kind::R, 0},
    {Seraphis::kAtmosFreezeMixId, Kind::R, 0},
    {Seraphis::kAtmosFreezeId, Kind::T, 0},
    {Seraphis::kAtmosDriftSmoothnessId, Kind::R, 0},
    {Seraphis::kAtmosDriftRangeId, Kind::R, 0},
    {Seraphis::kAtmosJitterId, Kind::R, 0},
    {Seraphis::kAtmosPositionId, Kind::R, 0},
    {Seraphis::kAtmosPositionSpreadId, Kind::R, 0},
    {Seraphis::kAtmosPitchId, Kind::R, 0},
    {Seraphis::kAtmosPitchSpreadId, Kind::R, 0},
    {Seraphis::kAtmosGrainEnvelopeId, Kind::L, 6},

    // --- Aether Space (1200-1399) -------------------------------------------
    {Seraphis::kAetherMixId, Kind::R, 0},
    {Seraphis::kAetherSizeId, Kind::R, 0},
    {Seraphis::kAetherDensityId, Kind::R, 0},
    {Seraphis::kAetherDecayId, Kind::R, 0},
    {Seraphis::kAetherFreezeId, Kind::T, 0},
    {Seraphis::kAetherDimensionalityId, Kind::R, 0},
    {Seraphis::kAetherDampingId, Kind::R, 0},
    {Seraphis::kAetherPreDelayId, Kind::R, 0},
    {Seraphis::kAetherModDepthId, Kind::R, 0},
    {Seraphis::kAetherModSmoothnessId, Kind::R, 0},
    {Seraphis::kAetherShimmerOctaveId, Kind::R, 0},
    {Seraphis::kAetherShimmerFifthId, Kind::R, 0},
    {Seraphis::kAetherBloomSendId, Kind::R, 0},
    {Seraphis::kAetherBloomDecayId, Kind::R, 0},
    {Seraphis::kAetherSpectralDiffusionId, Kind::R, 0},
    {Seraphis::kAetherSizeBreathDepthId, Kind::R, 0},
    {Seraphis::kAetherTideDepthId, Kind::R, 0},
    {Seraphis::kAetherWidthId, Kind::R, 0},
};

constexpr std::size_t kSurfaceRowCount = sizeof(kSurface) / sizeof(kSurface[0]);
static_assert(kSurfaceRowCount == 91, "spec C-6 is a 91-row table");

/// Number of discrete states for an `L` or `T` row; 0 for a continuous `R` row.
[[nodiscard]] constexpr int stateCountOf(const SurfaceRow& row) noexcept {
    switch (row.kind) {
        case Kind::R: return 0;
        case Kind::T: return 2;
        case Kind::L: return row.entries;
    }
    return 0;
}

// =============================================================================
// Controller helpers - the registered defaults are read LIVE, never transcribed
// =============================================================================

[[nodiscard]] bool infoForId(Vst::EditController& controller, Vst::ParamID id,
                             Vst::ParameterInfo& out) {
    const int32 count = controller.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::ParameterInfo info{};
        if (controller.getParameterInfo(i, info) != kResultOk) {
            continue;
        }
        if (info.id == id) {
            out = info;
            return true;
        }
    }
    return false;
}

[[nodiscard]] double defaultNormalizedFor(Vst::EditController& controller, Vst::ParamID id) {
    Vst::ParameterInfo info{};
    INFO("parameter ID " << id << " must be registered");
    REQUIRE(infoForId(controller, id, info));
    return info.defaultNormalizedValue;
}

/// The discrete index a normalized value denormalizes to (the `L`/`T`
/// denormalization form of plan §2.3.1, shared by every pack).
[[nodiscard]] int indexOfNormalized(double normalized, int states) {
    if (states < 2) {
        return 0;
    }
    const double raw = normalized * static_cast<double>(states - 1) + 0.5;
    const int i = static_cast<int>(raw);
    return (i < 0) ? 0 : ((i > states - 1) ? states - 1 : i);
}

/// SC-023's derived non-default value for one row. See construction choice 1 in
/// the banner: guaranteed different from the registered default BY CONSTRUCTION,
/// and asserted per row anyway.
[[nodiscard]] double nonDefaultNormalizedFor(Vst::EditController& controller,
                                             const SurfaceRow& row) {
    // The two PINNED rows §7.13 names for their rendered observables.
    if (row.id == Seraphis::kAetherPreDelayId || row.id == Seraphis::kAetherDecayId) {
        return 1.0;  // 200 ms and 60 s respectively
    }

    const double def = defaultNormalizedFor(controller, row.id);
    const int states = stateCountOf(row);
    if (states == 0) {
        return (def <= 0.5) ? 0.87 : 0.13;
    }
    const int defIndex = indexOfNormalized(def, states);
    const int target = (defIndex == 0) ? (states - 1) : 0;
    return static_cast<double>(target) / static_cast<double>(states - 1);
}

/// A deterministic pseudo-random normalized value per row (SC-010's
/// "randomized-but-valid setting, fixed seed"). The generator is a plain LCG so
/// the sequence is identical on every toolchain - no <random> distribution,
/// whose implementation is not specified across standard libraries.
class Lcg {
public:
    explicit Lcg(std::uint32_t seed) : state_(seed) {}

    [[nodiscard]] double nextUnit() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>((state_ >> 8) & 0xFFFFFFu) / static_cast<double>(0x1000000u);
    }

private:
    std::uint32_t state_;
};

// =============================================================================
// Driving a processor to a full 91-parameter setting
// =============================================================================

/// One rendered, prepared processor plus the 91 normalized values it was driven
/// with. `values[i]` corresponds to `kSurface[i]`.
struct DrivenProcessor {
    std::unique_ptr<SeraphisTest::ProcessorFixture> fx =
        std::make_unique<SeraphisTest::ProcessorFixture>();
    std::array<double, kSurfaceRowCount> values{};
};

/// Prepare, push all 91 parameters through IParameterChanges, render one block
/// so every push path has run.
void driveAllParameters(DrivenProcessor& d, double sampleRate, int32 blockSize) {
    REQUIRE(d.fx->prepare(sampleRate, blockSize) == kResultOk);
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        d.fx->setParam(kSurface[i].id, d.values[i]);
    }
    REQUIRE(d.fx->processBlock(blockSize) == kResultOk);
}

// =============================================================================
// DSP read-back snapshots (SC-023 clause 4)
// =============================================================================
// The 37 VP expressions are the SAME ones
// dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp:308-345 uses, so the
// two files cannot disagree about which getter serves which field.

constexpr std::size_t kVpFieldCount = 37;

[[nodiscard]] std::array<double, kVpFieldCount> voiceSnapshot(
    const Krate::DSP::SeraphisVoice& v) {
    std::array<double, kVpFieldCount> s{};
    std::size_t k = 0;
    s[k++] = static_cast<double>(v.cloud().getDriftSmoothness());
    s[k++] = static_cast<double>(v.cloud().getDecayTimeSec());
    s[k++] = static_cast<double>(v.cloud().getEnvelopeOffsetSpread());
    s[k++] = static_cast<double>(v.morph().getBloom());
    s[k++] = static_cast<double>(static_cast<int>(v.getTravelMode()));
    s[k++] = static_cast<double>(v.morph().getTravelRate());
    s[k++] = v.morph().getWaypointInterval();
    s[k++] = static_cast<double>(v.orbit().getRate());
    s[k++] = static_cast<double>(v.orbit().getCoupling());
    s[k++] = static_cast<double>(v.orbit().getGrowth());
    s[k++] = static_cast<double>(static_cast<int>(v.getEnvelopeMode()));
    s[k++] = static_cast<double>(v.growth().getDuration());
    s[k++] = static_cast<double>(static_cast<int>(v.body().getMaterial()));
    s[k++] = static_cast<double>(v.body().getResonance());
    s[k++] = static_cast<double>(v.body().getKeyTracking());
    s[k++] = static_cast<double>(v.body().getDrive());
    s[k++] = static_cast<double>(v.body().getMix());
    s[k++] = static_cast<double>(v.body().getCloudMix());
    s[k++] = static_cast<double>(v.body().getCloudDecaySec());
    s[k++] = static_cast<double>(v.body().getCloudSize());
    s[k++] = static_cast<double>(v.body().getCloudDamping());
    s[k++] = static_cast<double>(v.body().getWidth());
    s[k++] = v.body().isInputAgcEnabled() ? 1.0 : 0.0;
    s[k++] = v.body().isResonatorBypass() ? 1.0 : 0.0;
    s[k++] = static_cast<double>(v.atmos().getDensity());
    s[k++] = static_cast<double>(v.atmos().getGrainSeconds());
    s[k++] = static_cast<double>(v.atmos().getPanSpread());
    s[k++] = static_cast<double>(v.atmos().getDecorrelation());
    s[k++] = static_cast<double>(v.atmos().getFreezeMix());
    s[k++] = static_cast<double>(v.atmos().getDriftSmoothness());
    s[k++] = static_cast<double>(v.atmos().getDriftRangeSemitones());
    s[k++] = static_cast<double>(v.atmos().getJitter());
    s[k++] = static_cast<double>(v.atmos().getPositionSeconds());
    s[k++] = static_cast<double>(v.atmos().getPositionSpread());
    s[k++] = static_cast<double>(v.atmos().getPitchSemitones());
    s[k++] = static_cast<double>(v.atmos().getPitchSpread());
    s[k++] = static_cast<double>(static_cast<int>(v.atmos().getGrainEnvelope()));
    REQUIRE(k == kVpFieldCount);
    return s;
}

[[nodiscard]] bool spectralStatesEqual(const Krate::DSP::SpectralState& a,
                                       const Krate::DSP::SpectralState& b) {
    if (a.numPartials != b.numPartials || a.tiltDbPerOct != b.tiltDbPerOct
        || a.inharmonicity != b.inharmonicity) {
        return false;
    }
    if (a.ratios != b.ratios || a.amplitudes != b.amplitudes || a.name != b.name) {
        return false;
    }
    return true;
}

/// SC-023 clause 4, in whole: every route's read-back on `target` equals the one
/// on `reference`. `reference` reached its values through IParameterChanges;
/// `target` reached them through setState(). A loader that dropped, swapped or
/// mis-scaled a field makes the two disagree on that field.
void requireEveryRouteMatches(Seraphis::Processor& target, Seraphis::Processor& reference) {
    Krate::DSP::SeraphisEngine* te = target.engineForTest();
    Krate::DSP::SeraphisEngine* re = reference.engineForTest();
    REQUIRE(te != nullptr);
    REQUIRE(re != nullptr);

    // --- 37 VP rows x every one of the sixteen slots -------------------------
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        INFO("voice " << v);
        const auto got = voiceSnapshot(te->getVoice(v));
        const auto want = voiceSnapshot(re->getVoice(v));
        for (std::size_t f = 0; f < kVpFieldCount; ++f) {
            INFO("VP field index " << f);
            CHECK(got[f] == want[f]);
        }
    }

    // --- 27 MB rows, through the matrix AND through the post-slice voice ------
    for (std::size_t t = 0; t < Krate::DSP::SeraphisMacroMatrix::kNumTargets; ++t) {
        INFO("macro target index " << t);
        const auto target_t = static_cast<Krate::DSP::SeraphisMacroTarget>(t);
        CHECK(target.macroMatrixForTest().getTargetBase(target_t)
              == reference.macroMatrixForTest().getTargetBase(target_t));
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        INFO("voice " << v << " MB read-back");
        CHECK(te->getVoice(v).getVoiceWidthBasePercent()
              == re->getVoice(v).getVoiceWidthBasePercent());
        CHECK(te->getVoice(v).getEnvelopeReleaseMs() == re->getVoice(v).getEnvelopeReleaseMs());
        CHECK(te->getVoice(v).cloud().getRichness() == re->getVoice(v).cloud().getRichness());
        CHECK(te->getVoice(v).body().getDamping() == re->getVoice(v).body().getDamping());
        CHECK(te->getVoice(v).atmos().getLevel() == re->getVoice(v).atmos().getLevel());
    }

    // --- 4 ENG rows ----------------------------------------------------------
    CHECK(te->getPolyphony() == re->getPolyphony());
    CHECK(te->getAtmosphereFreeze() == re->getAtmosphereFreeze());
    CHECK(te->getSeed() == re->getSeed());
    CHECK(te->getOutputSaturation() == re->getOutputSaturation());

    // --- 5 CFG rows ----------------------------------------------------------
    CHECK_FALSE(target.spectralStatesPendingForTest());
    for (int slot = 0; slot < 4; ++slot) {
        INFO("spectral slot " << slot);
        CHECK(spectralStatesEqual(target.spectralSlotForTest(slot),
                                  reference.spectralSlotForTest(slot)));
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        INFO("voice " << v << " state count");
        CHECK(te->getVoice(v).morph().getStateCount()
              == re->getVoice(v).morph().getStateCount());
    }
}

/// True when at least one route's read-back DIFFERS - the shape clause 6's
/// negative control needs (`requireEveryRouteMatches` would abort the run).
[[nodiscard]] bool anyRouteDiffers(Seraphis::Processor& target, Seraphis::Processor& reference) {
    Krate::DSP::SeraphisEngine* te = target.engineForTest();
    Krate::DSP::SeraphisEngine* re = reference.engineForTest();
    if (te == nullptr || re == nullptr) {
        return true;
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        if (voiceSnapshot(te->getVoice(v)) != voiceSnapshot(re->getVoice(v))) {
            return true;
        }
    }
    for (std::size_t t = 0; t < Krate::DSP::SeraphisMacroMatrix::kNumTargets; ++t) {
        const auto target_t = static_cast<Krate::DSP::SeraphisMacroTarget>(t);
        if (target.macroMatrixForTest().getTargetBase(target_t)
            != reference.macroMatrixForTest().getTargetBase(target_t)) {
            return true;
        }
    }
    if (te->getPolyphony() != re->getPolyphony() || te->getSeed() != re->getSeed()
        || te->getAtmosphereFreeze() != re->getAtmosphereFreeze()
        || te->getOutputSaturation() != re->getOutputSaturation()) {
        return true;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (!spectralStatesEqual(target.spectralSlotForTest(slot),
                                 reference.spectralSlotForTest(slot))) {
            return true;
        }
    }
    return false;
}

/// RAII around the SC-023 clause 6 / clause 7(d) negative-control seam, so a
/// failing REQUIRE inside a section can never leave the switch on for the rest
/// of the suite.
struct SurfacePushDisableGuard {
    SurfacePushDisableGuard(bool onPresetLoad, bool onReprepare) {
        Seraphis::Processor::setSurfacePushDisabledForTest(onPresetLoad, onReprepare);
    }
    ~SurfacePushDisableGuard() {
        Seraphis::Processor::setSurfacePushDisabledForTest(false, false);
    }
    SurfacePushDisableGuard(const SurfacePushDisableGuard&) = delete;
    SurfacePushDisableGuard& operator=(const SurfacePushDisableGuard&) = delete;
};

// =============================================================================
// Render helpers (SC-011's migration render, SC-023's two AE observables)
// =============================================================================

/// Render `seconds` of note 60 (velocity 100) held throughout, at `sampleRate`,
/// in `blockSize` blocks, and return the interleaved-by-channel capture.
void renderNote(SeraphisTest::ProcessorFixture& fx, double sampleRate, std::size_t blockSize,
                double seconds) {
    const auto totalBlocks =
        static_cast<std::size_t>(sampleRate * seconds / static_cast<double>(blockSize));
    fx.renderBlocks(totalBlocks, blockSize,
                    [](std::size_t block, Krate::Test::EventList& events,
                       SeraphisTest::ParameterChanges&) {
                        if (block == 0) {
                            Vst::Event e{};
                            e.type = Vst::Event::kNoteOnEvent;
                            e.sampleOffset = 0;
                            e.busIndex = 0;
                            e.noteOn.channel = 0;
                            e.noteOn.pitch = 60;
                            e.noteOn.velocity = 100.0f / 127.0f;
                            e.noteOn.noteId = -1;
                            events.addEvent(e);
                        }
                    });
}

[[nodiscard]] double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (d > worst) {
            worst = d;
        }
    }
    return worst;
}

/// Index of the first sample whose magnitude exceeds `threshold` on either
/// channel, or `size()` when the render never gets there.
[[nodiscard]] std::size_t onsetIndex(const std::vector<float>& l, const std::vector<float>& r,
                                     double threshold) {
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (std::fabs(static_cast<double>(l[i])) > threshold
            || std::fabs(static_cast<double>(r[i])) > threshold) {
            return i;
        }
    }
    return l.size();
}

[[nodiscard]] double rmsOfRange(const std::vector<float>& l, const std::vector<float>& r,
                                std::size_t fromIndex, std::size_t toIndex) {
    const std::size_t hi = (toIndex > l.size()) ? l.size() : toIndex;
    if (fromIndex >= hi) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = fromIndex; i < hi; ++i) {
        sum += static_cast<double>(l[i]) * static_cast<double>(l[i]);
        sum += static_cast<double>(r[i]) * static_cast<double>(r[i]);
    }
    return std::sqrt(sum / static_cast<double>(2 * (hi - fromIndex)));
}

}  // namespace

// =============================================================================
// SC-010
// =============================================================================

TEST_CASE("Seraphis_StateRoundTrip_IsExact", "[seraphis][state][v2]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int32 kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // A randomized-but-valid setting of all 91 parameters, fixed seed. `L`/`T`
    // rows are snapped onto an exact index grid point so the round trip through
    // the index quantizer is exact; `R` rows stay inside [0.05, 0.95] so no
    // range clamp truncates the value and makes the inverse non-unique.
    Lcg rng(0xC0FFEEu);
    DrivenProcessor driven;
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        const int states = stateCountOf(kSurface[i]);
        if (states == 0) {
            driven.values[i] = 0.05 + 0.90 * rng.nextUnit();
        } else {
            const int index = static_cast<int>(rng.nextUnit() * static_cast<double>(states))
                              % states;
            driven.values[i] = static_cast<double>(index) / static_cast<double>(states - 1);
        }
    }
    driveAllParameters(driven, kSampleRate, kBlock);

    SECTION("getState -> setState -> getState is byte-identical at exactly 2532 bytes") {
        StreamPtr a = captureState(*driven.fx->proc);
        REQUIRE(a->getSize() == kV2StateBytes);

        // NON-VACUITY: the stream must NOT be the default stream, or the
        // byte-identity check below would pass for a getState() writing
        // constants.
        Seraphis::Processor pristine;
        StreamPtr defaults = captureState(pristine);
        REQUIRE(defaults->getSize() == kV2StateBytes);
        REQUIRE(std::memcmp(a->getData(), defaults->getData(),
                            static_cast<std::size_t>(kV2StateBytes)) != 0);

        Seraphis::Processor second;
        rewindStream(*a);
        REQUIRE(second.setState(a.get()) == kResultOk);
        StreamPtr b = captureState(second);

        REQUIRE(b->getSize() == kV2StateBytes);
        CHECK(std::memcmp(a->getData(), b->getData(),
                          static_cast<std::size_t>(kV2StateBytes)) == 0);

        // ...and a third pass, because FR-094's byte identity must be a fixed
        // point and not merely a single self-consistent hop.
        Seraphis::Processor third;
        rewindStream(*b);
        REQUIRE(third.setState(b.get()) == kResultOk);
        StreamPtr c = captureState(third);
        REQUIRE(c->getSize() == kV2StateBytes);
        CHECK(std::memcmp(b->getData(), c->getData(),
                          static_cast<std::size_t>(kV2StateBytes)) == 0);
    }

    SECTION("Controller::setComponentState reproduces every one of the 91 values") {
        StreamPtr a = captureState(*driven.fx->proc);
        rewindStream(*a);
        REQUIRE(controller.setComponentState(a.get()) == kResultOk);

        for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
            INFO("parameter ID " << kSurface[i].id);
            CHECK(controller.getParamNormalized(kSurface[i].id)
                  == Approx(driven.values[i]).margin(1.0e-6));
        }
    }

    // -------------------------------------------------------------------------
    // The LOCALIZED check on §2.3.0's discard loop. Without it the failure mode
    // is 55 parameters reading garbage from a 2164-byte-misaligned cursor, and
    // the parity assertion above cannot say which side drifted.
    // -------------------------------------------------------------------------
    SECTION("loadMorphParamsToController consumes the same 2216 bytes the processor does") {
        constexpr int64 kMorphBlockBytes = 52 + 4 * 541;  // 13 scalars + 4 payloads
        static_assert(kMorphBlockBytes == 2216, "plan 5.1's [morph] block is 2216 bytes");

        // A [morph] block on its own, written by exactly the path getState uses.
        StreamPtr s = makeStream();
        {
            IBStreamer w(s.get(), kLittleEndian);
            Seraphis::saveMorphParams(driven.fx->proc->morphParamsForTest(), w);
            std::array<Krate::DSP::SpectralState, 4> payload{};
            for (int slot = 0; slot < 4; ++slot) {
                payload[static_cast<std::size_t>(slot)] =
                    driven.fx->proc->spectralSlotForTest(slot);
            }
            Seraphis::saveSpectralPayloads(payload, w);
        }
        REQUIRE(s->getSize() == kMorphBlockBytes);

        rewindStream(*s);
        int64 processorEnd = 0;
        {
            Seraphis::MorphParams sink{};
            std::array<Krate::DSP::SpectralState, 4> destination{};
            IBStreamer r(s.get(), kLittleEndian);
            REQUIRE(Seraphis::loadMorphParams(sink, r, destination));
            processorEnd = r.tell();
        }

        rewindStream(*s);
        int64 controllerEnd = 0;
        {
            IBStreamer r(s.get(), kLittleEndian);
            Seraphis::loadMorphParamsToController(r, [](Vst::ParamID, double) {});
            controllerEnd = r.tell();
        }

        CHECK(processorEnd == kMorphBlockBytes);
        CHECK(controllerEnd == processorEnd);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// SC-011
// =============================================================================

TEST_CASE("Seraphis_StateVersion_MigratesAndRefuses", "[seraphis][state][v2]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    SECTION("A 36-byte version-1 stream loads, and the 83 new IDs take their defaults") {
        // The eight Phase 8 parameters carry NON-default values, so "the stream
        // was read" is distinguishable from "nothing happened".
        StreamPtr v1 = makeV1Stream(Seraphis::kStateVersion1, 1.5f, 12, 0, 0.125f, 0.25f,
                                    0.375f, 0.8f, 0.625f);
        REQUIRE(v1->getSize() == kV1StateBytes);

        Seraphis::Processor proc;
        REQUIRE(proc.setState(v1.get()) == kResultOk);

        // The stream it writes back is a FULL v2 stream: migration widens.
        StreamPtr out = captureState(proc);
        REQUIRE(out->getSize() == kV2StateBytes);

        rewindStream(*out);
        REQUIRE(controller.setComponentState(out.get()) == kResultOk);

        // The eight Phase 8 parameters took their stream values...
        CHECK(controller.getParamNormalized(Seraphis::kMasterGainId) == Approx(0.75));
        CHECK(controller.getParamNormalized(Seraphis::kPolyphonyId) == Approx(11.0 / 15.0));
        CHECK(controller.getParamNormalized(Seraphis::kSoftLimitId) == Approx(0.0));
        CHECK(controller.getParamNormalized(Seraphis::kMacroDreamId) == Approx(0.125));
        CHECK(controller.getParamNormalized(Seraphis::kMacroBloomId) == Approx(0.25));
        CHECK(controller.getParamNormalized(Seraphis::kMacroDissolveId) == Approx(0.375));
        CHECK(controller.getParamNormalized(Seraphis::kMacroGravityId) == Approx(0.8));
        CHECK(controller.getParamNormalized(Seraphis::kMacroEntropyId) == Approx(0.625));

        // ...and ALL 83 Phase 9 parameters read back at their registered
        // defaults. The four Phase 8 macro IDs and the four Phase 8 globals are
        // the only rows excluded.
        for (const SurfaceRow& row : kSurface) {
            if (row.id < Seraphis::kMacroParamRangeEnd) {
                continue;  // 0-199: the Phase 8 eight (and kSeedId, below)
            }
            INFO("Phase 9 parameter ID " << row.id << " must be at its registered default");
            CHECK(controller.getParamNormalized(row.id)
                  == Approx(defaultNormalizedFor(controller, row.id)).margin(1.0e-6));
        }
        // kSeedId is in the 0-99 band but is a PHASE 9 parameter, and its four
        // bytes sit immediately after the v1 prefix - the single most likely
        // place for a mis-positioned reader to eat the wrong field (FR-091a).
        CHECK(controller.getParamNormalized(Seraphis::kSeedId)
              == Approx(defaultNormalizedFor(controller, Seraphis::kSeedId)).margin(1.0e-6));
    }

    SECTION("A version-1 stream at the Phase 8 defaults renders the Phase 8 sound") {
        // SC-002's pass condition, using SC-002's construction: a same-binary,
        // same-TU control from the Phase 8 shipped defaults, per-sample
        // maxAbsDiff <= 1.0e-5 over both channels, and NO checked-in
        // fingerprint reference.
        StreamPtr v1 = makeV1Stream(Seraphis::kStateVersion1, 1.0f, 8, 1, 0.0f, 0.0f, 0.0f,
                                    0.5f, 0.0f);

        SeraphisTest::ProcessorFixture migrated;
        REQUIRE(migrated.prepare(kSampleRate, static_cast<int32>(kBlock)) == kResultOk);
        REQUIRE(migrated.proc->setState(v1.get()) == kResultOk);
        renderNote(migrated, kSampleRate, kBlock, 4.0);

        SeraphisTest::ProcessorFixture control;  // never given any state at all
        REQUIRE(control.prepare(kSampleRate, static_cast<int32>(kBlock)) == kResultOk);
        renderNote(control, kSampleRate, kBlock, 4.0);

        CHECK(maxAbsDiff(migrated.capturedL, control.capturedL) <= 1.0e-5);
        CHECK(maxAbsDiff(migrated.capturedR, control.capturedR) <= 1.0e-5);
    }

    SECTION("A version-3 stream is refused with no state mutated") {
        Seraphis::Processor proc;
        StreamPtr before = captureState(proc);
        std::vector<char> beforeBytes(static_cast<std::size_t>(before->getSize()));
        std::memcpy(beforeBytes.data(), before->getData(), beforeBytes.size());

        StreamPtr v3 = makeV1Stream(Seraphis::kCurrentStateVersion + 1, 1.5f, 12, 0, 0.1f,
                                    0.2f, 0.3f, 0.8f, 0.4f);
        REQUIRE(Seraphis::kCurrentStateVersion + 1 == 3);
        CHECK(proc.setState(v3.get()) == kResultFalse);

        StreamPtr after = captureState(proc);
        REQUIRE(after->getSize() == static_cast<int64>(beforeBytes.size()));
        CHECK(std::memcmp(after->getData(), beforeBytes.data(), beforeBytes.size()) == 0);

        rewindStream(*v3);
        CHECK(controller.setComponentState(v3.get()) == kResultFalse);
    }

    SECTION("Twelve truncated version-2 streams load without crash") {
        DrivenProcessor driven;
        Lcg rng(0x5EEDu);
        for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
            const int states = stateCountOf(kSurface[i]);
            driven.values[i] =
                (states == 0) ? (0.05 + 0.90 * rng.nextUnit())
                              : static_cast<double>(
                                    static_cast<int>(rng.nextUnit()
                                                     * static_cast<double>(states))
                                    % states)
                                    / static_cast<double>(states - 1);
        }
        driveAllParameters(driven, kSampleRate, static_cast<int32>(kBlock));
        StreamPtr full = captureState(*driven.fx->proc);
        REQUIRE(full->getSize() == kV2StateBytes);

        // Deliberately chosen: below the version field; at the v1 boundary; at
        // the [seed] and [cloud] boundaries; INSIDE the first 541-byte payload;
        // at a payload boundary; inside the last payload; at the [life],
        // [body] and [atmos] block boundaries; and one byte short of the end.
        constexpr int32 kMorphScalarsEnd = 84 + 52;                  // 136
        constexpr int32 kFirstPayloadEnd = kMorphScalarsEnd + 541;   // 677
        const int32 cuts[] = {
            2,                        // below the mandatory version int32
            kV1StateBytes,            // exactly the v1 prefix
            40,                       // after [seed]
            84,                       // after [cloud]
            kMorphScalarsEnd,         // after [morph]'s 13 scalars
            kMorphScalarsEnd + 200,   // INSIDE payload 0
            kFirstPayloadEnd,         // payload boundary
            2300 - 100,               // inside the LAST payload
            2300,                     // after [morph]
            2340,                     // after [life]
            2460,                     // after [atmos]
            kV2StateBytes - 1,        // one byte short of the end
        };
        static_assert(sizeof(cuts) / sizeof(cuts[0]) == 12,
                      "SC-011 asks for twelve chosen offsets");

        for (const int32 n : cuts) {
            INFO("truncated to " << n << " bytes");
            StreamPtr cut = makeTruncatedStream(*full, n);

            Seraphis::Processor proc;
            const tresult r = proc.setState(cut.get());
            if (n < 4) {
                CHECK(r == kResultFalse);
            } else {
                CHECK(r == kResultOk);
            }

            // Whatever survived, the processor is still serializable at the
            // full v2 size and reloadable - "without crash, remainder at
            // defaults" is only meaningful if the object is still usable.
            StreamPtr out = captureState(proc);
            CHECK(out->getSize() == kV2StateBytes);

            Seraphis::Processor again;
            rewindStream(*out);
            CHECK(again.setState(out.get()) == kResultOk);
        }
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// SC-012
// =============================================================================

TEST_CASE("Seraphis_SpectralStateSlots_RoundTripExactly", "[seraphis][state][v2]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int32 kBlock = 512;

    SECTION("Each of the five factory states survives save/reload field-by-field") {
        // Every slot combination that fits: slot i gets factory state
        // (base + i) mod 5, for each of the five bases - so all five states are
        // exercised in every slot position.
        for (int base = 0; base < 5; ++base) {
            INFO("factory base " << base);

            SeraphisTest::ProcessorFixture source;
            REQUIRE(source.prepare(kSampleRate, kBlock) == kResultOk);
            for (int slot = 0; slot < 4; ++slot) {
                const int id = (base + slot) % 5;
                source.setParam(
                    static_cast<Vst::ParamID>(Seraphis::kMorphState0Id + slot),
                    static_cast<double>(id) / 4.0);
            }
            REQUIRE(source.processBlock(kBlock) == kResultOk);

            StreamPtr saved = captureState(*source.proc);
            REQUIRE(saved->getSize() == kV2StateBytes);

            SeraphisTest::ProcessorFixture reloaded;
            REQUIRE(reloaded.prepare(kSampleRate, kBlock) == kResultOk);
            REQUIRE(reloaded.proc->setState(saved.get()) == kResultOk);
            REQUIRE(reloaded.processBlock(kBlock) == kResultOk);

            for (int slot = 0; slot < 4; ++slot) {
                INFO("slot " << slot);
                const Krate::DSP::SpectralState& want = source.proc->spectralSlotForTest(slot);
                const Krate::DSP::SpectralState& got =
                    reloaded.proc->spectralSlotForTest(slot);

                // Field by field, as SC-012 names them - never through the
                // ENGINE slot, which stores SANITIZED log2 ratios and discards
                // name / tilt / inharmonicity (spectral_morph_engine.h:285-313).
                CHECK(got.numPartials == want.numPartials);
                CHECK(got.tiltDbPerOct == want.tiltDbPerOct);
                CHECK(got.inharmonicity == want.inharmonicity);
                CHECK(got.name == want.name);
                CHECK(got.ratios == want.ratios);
                CHECK(got.amplitudes == want.amplitudes);

                // Non-vacuity: the payload really did carry a NAMED factory
                // state, not an all-zero default.
                CHECK(want.numPartials > 0);
                CHECK(want.name[0] != '\0');
            }
        }
    }

    SECTION("541 bytes of garbage deserialize to false and leave the state untouched") {
        const Krate::DSP::SpectralState original =
            Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::Bell);
        Krate::DSP::SpectralState victim = original;

        std::array<std::byte, Krate::DSP::kSpectralStateBytes> garbage{};
        for (std::size_t i = 0; i < garbage.size(); ++i) {
            garbage[i] = static_cast<std::byte>((i * 37u + 11u) & 0xFFu);
        }
        // Byte 0 is the format byte (spectral_state.h:222). Forcing it to a
        // value kSpectralStateFormatVersion can never take makes the rejection
        // deterministic rather than dependent on random floats failing
        // isValidSpectralState.
        garbage[0] = static_cast<std::byte>(0xFFu);

        CHECK_FALSE(
            Krate::DSP::deserializeSpectralState(garbage.data(), garbage.size(), victim));
        CHECK(std::memcmp(&victim, &original, sizeof(Krate::DSP::SpectralState)) == 0);
    }

    SECTION("A stream whose payloads are garbage leaves spectralSlots_ bitwise unchanged") {
        SeraphisTest::ProcessorFixture source;
        REQUIRE(source.prepare(kSampleRate, kBlock) == kResultOk);
        // Slot 1 -> Choir (index 2), so the slot content is NOT the default.
        source.setParam(Seraphis::kMorphState1Id, 2.0 / 4.0);
        REQUIRE(source.processBlock(kBlock) == kResultOk);

        StreamPtr saved = captureState(*source.proc);
        REQUIRE(saved->getSize() == kV2StateBytes);

        // Corrupt every byte of the four payloads in place. The 13 [morph]
        // scalars in front of them - including the four slot IDs - stay intact,
        // so the receiving processor knows which factory states the slots want.
        constexpr int32 kPayloadStart = 84 + 52;
        {
            char* bytes = saved->getData();
            for (int32 i = kPayloadStart; i < kPayloadStart + 4 * 541; ++i) {
                bytes[i] = static_cast<char>((i * 7) & 0x7F);
            }
            // ...and force each payload's format byte to a value
            // kSpectralStateFormatVersion can never take, so all four
            // rejections are deterministic.
            for (int slot = 0; slot < 4; ++slot) {
                bytes[kPayloadStart + slot * 541] = static_cast<char>(0xFF);
            }
        }

        SeraphisTest::ProcessorFixture target;
        REQUIRE(target.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        std::array<Krate::DSP::SpectralState, 4> before{};
        for (int slot = 0; slot < 4; ++slot) {
            before[static_cast<std::size_t>(slot)] = target.proc->spectralSlotForTest(slot);
        }

        rewindStream(*saved);
        REQUIRE(target.proc->setState(saved.get()) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        // A rejected payload leaves the staging slot at the factory state the
        // slot ID selects; nothing garbled ever reaches spectralSlots_.
        for (int slot = 0; slot < 4; ++slot) {
            INFO("slot " << slot);
            const Krate::DSP::SpectralState& got = target.proc->spectralSlotForTest(slot);
            CHECK(got.numPartials > 0);
            CHECK(got.name[0] != '\0');
            CHECK(spectralStatesEqual(got, before[static_cast<std::size_t>(slot)]));
        }
    }
}

// =============================================================================
// SC-023 clauses 1-6
// =============================================================================

TEST_CASE("Seraphis_PresetLoadAfterPrepare_ReachesDsp", "[seraphis][state][v2]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int32 kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // --- SC-023's own all-non-default table, and its CONSTRUCTION CHECK -------
    std::array<double, kSurfaceRowCount> table{};
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        table[i] = nonDefaultNormalizedFor(controller, kSurface[i]);
    }

    SECTION("Every one of the 91 rows differs from that ID's registered default") {
        for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
            const SurfaceRow& row = kSurface[i];
            INFO("parameter ID " << row.id);
            const double def = defaultNormalizedFor(controller, row.id);
            const int states = stateCountOf(row);
            if (states == 0) {
                CHECK(table[i] != def);  // exact !=, per §7.13
            } else {
                CHECK(indexOfNormalized(table[i], states) != indexOfNormalized(def, states));
            }
        }
    }

    // Clause 2's stream: written by the same getState path from a processor
    // driven to the table above.
    DrivenProcessor reference;
    reference.values = table;
    driveAllParameters(reference, kSampleRate, kBlock);
    StreamPtr preset = captureState(*reference.fx->proc);
    REQUIRE(preset->getSize() == kV2StateBytes);

    SECTION("Clauses 1-5: the preset reaches every route, consumed once on the audio thread") {
        // 1. Prepare and render ONE block, so every prepare-time push has run.
        SeraphisTest::ProcessorFixture target;
        REQUIRE(target.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        // Non-vacuity for clause 6's sake: the target does NOT already hold the
        // preset's values.
        REQUIRE(anyRouteDiffers(*target.proc, *reference.fx->proc));

        const std::size_t aetherPushesBefore =
            target.proc->applyAetherParamsCallCountForTest();

        // 2. setState().
        rewindStream(*preset);
        REQUIRE(target.proc->setState(preset.get()) == kResultOk);

        // 5, first half: the staging copy did NOT happen on the message thread.
        CHECK(target.proc->spectralHandoffConsumeCountForTest() == 0u);

        // 3. Render ONE block.
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        // 5, second half: it happened exactly once, on the audio thread.
        CHECK(target.proc->spectralHandoffConsumeCountForTest() == 1u);

        // 4. Every route.
        requireEveryRouteMatches(*target.proc, *reference.fx->proc);

        // The ten AE rows: the push ran...
        CHECK(target.proc->applyAetherParamsCallCountForTest() > aetherPushesBefore);
        const std::size_t afterLoad = target.proc->applyAetherParamsCallCountForTest();
        // ...and the on-change tracker latched, so an idle block does NOT
        // re-push (which is what makes the count above evidence of THIS load).
        REQUIRE(target.processBlock(kBlock) == kResultOk);
        CHECK(target.proc->applyAetherParamsCallCountForTest() == afterLoad);
    }

    SECTION("Clause 4's two rendered AE observables: pre-delay (1207) and decay (1203)") {
        // Matched pairs: two streams differing in ONE aether ID only, loaded
        // into two freshly prepared processors, so the renders are comparable.
        constexpr std::size_t kBlockSz = 512;

        auto makeAetherStream = [&](double preDelayNorm, double decayNorm) {
            DrivenProcessor d;
            for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
                d.values[i] = defaultNormalizedFor(controller, kSurface[i].id);
                if (kSurface[i].id == Seraphis::kAetherMixId) {
                    d.values[i] = 1.0;  // fully wet, so the wet path IS the output
                } else if (kSurface[i].id == Seraphis::kAetherPreDelayId) {
                    d.values[i] = preDelayNorm;
                } else if (kSurface[i].id == Seraphis::kAetherDecayId) {
                    d.values[i] = decayNorm;
                }
            }
            driveAllParameters(d, kSampleRate, static_cast<int32>(kBlockSz));
            StreamPtr s = captureState(*d.fx->proc);
            REQUIRE(s->getSize() == kV2StateBytes);
            return s;
        };

        const double defaultPreDelay =
            defaultNormalizedFor(controller, Seraphis::kAetherPreDelayId);
        const double defaultDecay = defaultNormalizedFor(controller, Seraphis::kAetherDecayId);

        // --- ID 1207: onset shift --------------------------------------------
        {
            StreamPtr shortDelay = makeAetherStream(defaultPreDelay, defaultDecay);
            StreamPtr longDelay = makeAetherStream(1.0, defaultDecay);  // 200 ms

            SeraphisTest::ProcessorFixture a;
            REQUIRE(a.prepare(kSampleRate, static_cast<int32>(kBlockSz)) == kResultOk);
            rewindStream(*shortDelay);
            REQUIRE(a.proc->setState(shortDelay.get()) == kResultOk);
            renderNote(a, kSampleRate, kBlockSz, 1.0);

            SeraphisTest::ProcessorFixture b;
            REQUIRE(b.prepare(kSampleRate, static_cast<int32>(kBlockSz)) == kResultOk);
            rewindStream(*longDelay);
            REQUIRE(b.proc->setState(longDelay.get()) == kResultOk);
            renderNote(b, kSampleRate, kBlockSz, 1.0);

            constexpr double kOnsetThreshold = 1.0e-4;
            const std::size_t onsetA = onsetIndex(a.capturedL, a.capturedR, kOnsetThreshold);
            const std::size_t onsetB = onsetIndex(b.capturedL, b.capturedR, kOnsetThreshold);
            INFO("onset at default pre-delay: " << onsetA << ", at 200 ms: " << onsetB);
            REQUIRE(onsetA < a.capturedL.size());

            // THE ASSERTION IS ON THE FIRST 150 ms OF ENERGY, not on the raw
            // onset index, and that is deliberate: a 200 ms pre-delay removes
            // the WET contribution from the first 200 ms, and any dry component
            // the mix control leaves in place is COMMON to both renders - so the
            // early-window energy is strictly lower for the delayed one whether
            // or not `AetherMix` at 1.0 removes the dry path entirely. A raw
            // onset-index comparison would be a statement about the dry path,
            // which ID 1207 does not control.
            const std::size_t earlyEnd = static_cast<std::size_t>(0.15 * kSampleRate);
            const double earlyA = rmsOfRange(a.capturedL, a.capturedR, 0, earlyEnd);
            const double earlyB = rmsOfRange(b.capturedL, b.capturedR, 0, earlyEnd);
            INFO("first 150 ms RMS at 0 ms pre-delay: " << earlyA << ", at 200 ms: " << earlyB);
            REQUIRE(earlyA > 0.0);
            CHECK(earlyB < earlyA);
        }

        // --- ID 1203: T60 ----------------------------------------------------
        {
            StreamPtr shortDecay = makeAetherStream(defaultPreDelay, defaultDecay);  // 4 s
            StreamPtr longDecay = makeAetherStream(defaultPreDelay, 1.0);            // 60 s

            SeraphisTest::ProcessorFixture a;
            REQUIRE(a.prepare(kSampleRate, static_cast<int32>(kBlockSz)) == kResultOk);
            rewindStream(*shortDecay);
            REQUIRE(a.proc->setState(shortDecay.get()) == kResultOk);
            renderNote(a, kSampleRate, kBlockSz, 3.0);

            SeraphisTest::ProcessorFixture b;
            REQUIRE(b.prepare(kSampleRate, static_cast<int32>(kBlockSz)) == kResultOk);
            rewindStream(*longDecay);
            REQUIRE(b.proc->setState(longDecay.get()) == kResultOk);
            renderNote(b, kSampleRate, kBlockSz, 3.0);

            const std::size_t tailFrom = a.capturedL.size() * 3 / 4;
            const double tailA =
                rmsOfRange(a.capturedL, a.capturedR, tailFrom, a.capturedL.size());
            const double tailB =
                rmsOfRange(b.capturedL, b.capturedR, tailFrom, b.capturedL.size());
            INFO("tail RMS at 4 s decay: " << tailA << ", at 60 s decay: " << tailB);
            CHECK(tailB > tailA);
        }
    }

    SECTION("Clause 6 negative control: with the push request stubbed out, clause 4 fails") {
        SurfacePushDisableGuard guard(/*onPresetLoad=*/true, /*onReprepare=*/false);

        SeraphisTest::ProcessorFixture target;
        REQUIRE(target.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        rewindStream(*preset);
        REQUIRE(target.proc->setState(preset.get()) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        CHECK(anyRouteDiffers(*target.proc, *reference.fx->proc));
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// SC-023 clause 6a
// =============================================================================

TEST_CASE("Seraphis_PresetLoadBeforePrepare_ReachesDsp", "[seraphis][state][v2]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int32 kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // A preset whose four spectral slots are all DIFFERENT from the registered
    // defaults ({SineStack, Glass, SineStack, SineStack}) and whose state count
    // is 4 rather than the default 2.
    DrivenProcessor reference;
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        reference.values[i] = defaultNormalizedFor(controller, kSurface[i].id);
    }
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        if (kSurface[i].id == Seraphis::kMorphState0Id) { reference.values[i] = 1.0 / 4.0; }
        if (kSurface[i].id == Seraphis::kMorphState1Id) { reference.values[i] = 2.0 / 4.0; }
        if (kSurface[i].id == Seraphis::kMorphState2Id) { reference.values[i] = 4.0 / 4.0; }
        if (kSurface[i].id == Seraphis::kMorphState3Id) { reference.values[i] = 1.0 / 4.0; }
        if (kSurface[i].id == Seraphis::kMorphStateCountId) { reference.values[i] = 1.0; }
    }
    driveAllParameters(reference, kSampleRate, kBlock);
    StreamPtr preset = captureState(*reference.fx->proc);
    REQUIRE(preset->getSize() == kV2StateBytes);

    // setState() BEFORE setupProcessing(), which FR-047 and the Edge cases ->
    // State bullet both make legal.
    SeraphisTest::ProcessorFixture target;
    REQUIRE(target.proc->initialize(nullptr) == kResultOk);
    rewindStream(*preset);
    REQUIRE(target.proc->setState(preset.get()) == kResultOk);

    Vst::ProcessSetup setup{};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;
    REQUIRE(target.proc->setupProcessing(setup) == kResultOk);
    REQUIRE(target.proc->setActive(true) == kResultOk);
    REQUIRE(target.processBlock(kBlock) == kResultOk);

    // The four payloads and the state count hold the PRESET's states, not the
    // registered defaults - the "seed from the CURRENT atomics" rule of §3.7.
    Seraphis::Processor pristine;
    for (int slot = 0; slot < 4; ++slot) {
        INFO("slot " << slot);
        CHECK(spectralStatesEqual(target.proc->spectralSlotForTest(slot),
                                  reference.fx->proc->spectralSlotForTest(slot)));
    }
    // ...and at least one of them really does differ from the default table, so
    // the assertion above is not satisfied by the defaults.
    bool anySlotIsNonDefault = false;
    for (int slot = 0; slot < 4; ++slot) {
        if (!spectralStatesEqual(target.proc->spectralSlotForTest(slot),
                                 pristine.spectralSlotForTest(slot))) {
            anySlotIsNonDefault = true;
        }
    }
    CHECK(anySlotIsNonDefault);

    Krate::DSP::SeraphisEngine* engine = target.proc->engineForTest();
    REQUIRE(engine != nullptr);
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        INFO("voice " << v);
        CHECK(engine->getVoice(v).morph().getStateCount() == 4);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// SC-023 clause 7
// =============================================================================

TEST_CASE("Seraphis_SampleRateChange_RePushesEverySurface", "[seraphis][state][v2]") {
    constexpr double kFirstRate = 44100.0;
    constexpr double kSecondRate = 96000.0;
    constexpr int32 kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    std::array<double, kSurfaceRowCount> table{};
    for (std::size_t i = 0; i < kSurfaceRowCount; ++i) {
        table[i] = nonDefaultNormalizedFor(controller, kSurface[i]);
    }

    auto buildTarget = [&](SeraphisTest::ProcessorFixture& fx, MemoryStream& preset) {
        REQUIRE(fx.prepare(kFirstRate, kBlock) == kResultOk);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        rewindStream(preset);
        REQUIRE(fx.proc->setState(&preset) == kResultOk);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
    };

    auto reprepareAt96k = [&](SeraphisTest::ProcessorFixture& fx) {
        Vst::ProcessSetup setup{};
        setup.processMode = Vst::kRealtime;
        setup.symbolicSampleSize = Vst::kSample32;
        setup.maxSamplesPerBlock = kBlock;
        setup.sampleRate = kSecondRate;
        REQUIRE(fx.proc->setupProcessing(setup) == kResultOk);
    };

    // -------------------------------------------------------------------------
    // Clause 7(b)'s observable MUST be read with the reverb momentarily THAWED.
    //
    // Clause 2 requires all 91 rows to be non-default, and kAetherFreezeId's
    // registered default is OFF - so this criterion's own value table loads the
    // reverb with FREEZE ON. AetherReverb::refreshControlState() then skips
    // updateGeometry() entirely while freezeTarget_ is set (aether_reverb.h:
    // 3610-3614), which is Phase 6's FR-034/C-4 by design: "effectiveDelay_,
    // feedbackGain_ and dampCoeff_ keep their LATCHED values until the freeze is
    // released" (:3599-3605). getEffectiveDelayLengthSamples() therefore stops
    // tracking Size, the breath depth AND - the point of this clause - the
    // rate-scaled reference lengths the re-prepare installed.
    //
    // Read raw, the two sides of the comparison are two unrelated latched
    // snapshots, and no implementation change can align them: 510.302 at 44.1 kHz
    // (the geometry from the block that ran BEFORE setState, i.e. Size 0.5 and
    // breath depth 0.2) against 483.5 at 96 kHz (latched right after the
    // re-prepare, with the preset's breath depth 0.87 already pushed). Their
    // ratio is 0.95, not the 2.177 rate ratio - measured, not assumed.
    //
    // Each reading is therefore taken with the freeze momentarily released: one
    // block at kAetherFreezeId = 0 puts the geometry live again, the preset's own
    // value is restored immediately afterwards, and nothing else about the DUT
    // moves - kAetherFreezeId is an AE row and clause (c)'s route comparison
    // covers VP/MB/ENG/CFG only. Live, the readings are 222.108 and 483.617
    // samples on channel 0 (279.530 / 608.647 on channel 1): a ratio of 2.1774 on
    // BOTH channels, which is 96 000 / 44 100 to four figures.
    auto readLiveTankLengths = [&](SeraphisTest::ProcessorFixture& fx) {
        fx.setParam(Seraphis::kAetherFreezeId, 0.0);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        Krate::DSP::AetherReverb* rv = fx.proc->reverbForTest();
        REQUIRE(rv != nullptr);
        std::array<double, 2> lengths{};
        for (std::size_t ch = 0; ch < lengths.size(); ++ch) {
            lengths[ch] = static_cast<double>(rv->getEffectiveDelayLengthSamples(ch));
            REQUIRE(lengths[ch] > 0.0);
        }
        // Put the preset's own freeze back before anything else observes the DUT.
        fx.setParam(Seraphis::kAetherFreezeId, 1.0);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        return lengths;
    };

    // The reference is prepared at 96 kHz from the start and driven by
    // automation, so clause 7(c) compares against a processor that never went
    // through a re-prepare at all.
    DrivenProcessor reference;
    reference.values = table;
    driveAllParameters(reference, kSecondRate, kBlock);

    DrivenProcessor presetSource;
    presetSource.values = table;
    driveAllParameters(presetSource, kFirstRate, kBlock);
    StreamPtr preset = captureState(*presetSource.fx->proc);
    REQUIRE(preset->getSize() == kV2StateBytes);

    SECTION("(a)-(c): the re-prepare clears the DSP and every surface is re-pushed") {
        SeraphisTest::ProcessorFixture target;
        buildTarget(target, *preset);

        Krate::DSP::SeraphisEngine* engine = target.proc->engineForTest();
        Krate::DSP::AetherReverb* reverb = target.proc->reverbForTest();
        REQUIRE(engine != nullptr);
        REQUIRE(reverb != nullptr);

        const std::array<double, 2> delayBefore = readLiveTankLengths(target);

        reprepareAt96k(target);

        // (a) every voice's every body engine counter is cleared, read BEFORE
        //     the new block - the counter is documented "Cleared by
        //     reset()/prepare()" (continuous_body.h:1532-1537).
        using Engine = Krate::DSP::ContinuousBody::Engine;
        constexpr Engine kEngines[] = {Engine::Modal, Engine::Waveguide, Engine::Comb};
        for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
            for (const Engine e : kEngines) {
                INFO("voice " << v);
                CHECK(engine->getVoice(v).body().getEngineSampleCount(e) == 0u);
            }
        }

        // (c) clause 4, verbatim.
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        // (b) the reverb re-prepared and its tank lengths scaled with the rate.
        //     Measured AFTER the block and through readLiveTankLengths (see its
        //     banner: the preset freezes the reverb, and a frozen reverb latches
        //     its geometry), so both readings are taken with the geometry live
        //     and with the same pushed Size and breath depth.
        //     The window is a band rather than an exact ratio because the length
        //     is a smoothed, Size-scaled quantity; the 96/44.1 = 2.18x rate
        //     change is far outside anything smoothing or rounding can produce.
        CHECK(reverb->isPrepared());
        const std::array<double, 2> delayAfter = readLiveTankLengths(target);
        const double ratio = kSecondRate / kFirstRate;
        for (std::size_t ch = 0; ch < delayBefore.size(); ++ch) {
            INFO("channel " << ch << ", rate ratio " << ratio);
            CHECK(delayAfter[ch] > delayBefore[ch] * 1.5);
            CHECK(delayAfter[ch] < delayBefore[ch] * 3.0);
        }

        requireEveryRouteMatches(*target.proc, *reference.fx->proc);
    }

    SECTION("(d): with pushAllSurfaces stubbed out of setupProcessing, (c) fails") {
        SeraphisTest::ProcessorFixture target;
        buildTarget(target, *preset);

        Krate::DSP::AetherReverb* reverb = target.proc->reverbForTest();
        Krate::DSP::SeraphisEngine* engine = target.proc->engineForTest();
        REQUIRE(reverb != nullptr);
        REQUIRE(engine != nullptr);
        const std::array<double, 2> delayBefore = readLiveTankLengths(target);

        {
            SurfacePushDisableGuard guard(/*onPresetLoad=*/false, /*onReprepare=*/true);
            reprepareAt96k(target);
            REQUIRE(target.processBlock(kBlock) == kResultOk);
        }

        // Sampled HERE, on the block the stub governed, so the extra blocks the
        // (b) measurement renders below can never soften the negative control.
        const bool routesDiffer = anyRouteDiffers(*target.proc, *reference.fx->proc);

        // (a) and (b) still pass - the stub removes only the re-push.
        using Engine = Krate::DSP::ContinuousBody::Engine;
        CHECK(engine->getVoice(0).body().getEngineSampleCount(Engine::Modal)
              <= static_cast<std::uint64_t>(kBlock));
        CHECK(reverb->isPrepared());
        const std::array<double, 2> delayAfter = readLiveTankLengths(target);
        CHECK(delayAfter[0] > delayBefore[0]);

        // (c) must FAIL.
        CHECK(routesDiffer);
    }

    REQUIRE(controller.terminate() == kResultOk);
}
