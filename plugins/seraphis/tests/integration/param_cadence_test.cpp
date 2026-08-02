// ==============================================================================
// Seraphis - Push cadence and allocation tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.7)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-006  no allocation on the audio thread across the whole parameter push
//   SC-007  the push cadence - a block-rate surface is pushed once per block,
//           and the configure-time surface only when a value actually moved
//   SC-013  the FR-046 retry - a rejected configure-time push is retried, per
//           voice, and stops retrying once the voice accepts
//
// THE COUNTERS ARE FR-041a's, AND EACH MEANS SOMETHING DIFFERENT (processor.h:
// 168-240): applyVoiceParamsCallCountForTest() and
// applySpectralStatesCallCountForTest() count SUCCESSFUL applications;
// applySpectralStatesAttemptCountForTest(), applyAetherParamsCallCountForTest()
// and setTargetBasePushCountForTest() count INVOCATIONS, so a per-slice re-push
// is visible.
//
// ONE ROW OF SC-007's TABLE IS ASSERTED AGAINST THE PLAN'S OWN NORMATIVE
// DESIGN RATHER THAN ITS PROSE, and the divergence is stated here rather than
// discovered. §7.7's table says "each of the four ENG counters: exactly 1 after
// 200 unchanged blocks". That holds for seed, soft limit and freeze. It CANNOT
// hold for polyphony, because plan §3.4 mandates the opposite in the same
// document: setupProcessing() seeds `lastPushedPolyphony_ = engine_->getPolyphony()`
// AFTER pushAllSurfaces(Reprepared) precisely "so the first process() does NOT
// re-call setPolyphony(), which would re-arm sumGain_ ... and would move
// setPolyphonyCallCountForTest(), which Phase 8 tests assert"
// (processor.cpp:507-513). Those Phase 8 assertions are live -
// integration/param_flow_test.cpp:608 requires the count to be 0 after prepare -
// and processor.h:230-234 records that engPolyphonyPushCountForTest() is "a NAMED
// ALIAS of setPolyphonyCallCountForTest(), not a second counter - so no Phase 8
// assertion moves". The polyphony row therefore asserts 0, and the CADENCE
// property the row exists for (it does not rise across 200 unchanged blocks, and
// a change of ID 3 or ID 1008 leaves it alone) is asserted in full.
//
// THE 200-BLOCK QUIESCENT ARM IS RENDERED ONCE, in its own SECTION. Every delta
// clause instead settles on kSettleBlocks and PROVES quiescence before applying
// its change (makeSettledRig() asserts that the counters taken half-way through
// equal the counters taken at the end), which is the property the deltas need
// and is strictly stronger than assuming it from a block count.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math, under which the compiler may assume finite
// values and fold such a test away.
//
// NO CHECKED-IN FLOAT GOLDEN. The two render comparisons below are both
// SAME-BINARY, same-session A/B arms; nothing is transcribed into the file.
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "parameters/body_params.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <allocation_detector.h>

#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Fixture = SeraphisTest::ProcessorFixture;
using Target = Krate::DSP::SeraphisMacroTarget;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;
constexpr std::size_t kMaxVoices = Krate::DSP::SeraphisEngine::kMaxVoices;
constexpr std::size_t kNumTargets = Krate::DSP::SeraphisMacroMatrix::kNumTargets;

/// The one MIDI note every arm plays. Typed, so the pitch argument is never an
/// implicit int -> int16 narrowing at the call site.
constexpr Steinberg::int16 kNote = 60;

/// SC-007's quiescent arm renders EXACTLY this many unchanged blocks.
constexpr std::size_t kQuiescentBlocks = 200;

/// The settle window every delta clause uses. It is not a substitute for the
/// 200-block arm above (which has its own SECTION): makeSettledRig() asserts the
/// counters have gone QUIET across it, which is the precondition the deltas need.
constexpr std::size_t kSettleBlocks = 8;

/// plan §3.5.2, BODY family (IDs 801, 802): kParamSmoothMs = 20 ms, so
/// tau = 20/5000 s = 4 ms = 192 samples at 48 kHz; the delivery grid is the
/// absolute 64-sample control chunk; kCompletionThreshold = 1.0e-4f
/// (primitives/smoother.h:55). For the worst case D = 1.0,
///   N_chunk = ceil(4 ms * ln(1 / 1e-4) / (64/48000 s)) = ceil(36.84 / 1.3333) = 28.
/// The two class-(b) clauses below move their parameter by D = 0.15, which is a
/// SMALLER step and therefore settles inside this bound with margin - the bound
/// is asserted as the spec states it, not as the step happens to need.
constexpr std::size_t kNChunkBody = 28;

/// plan §3.5.2's per-family constants, pinned so a silent collapse back to one
/// number fails HERE rather than in an unrelated criterion.
static_assert(Seraphis::kParamSmoothMs == 20.0f,
              "SC-007: kNChunkBody = 28 is derived from the 20 ms BODY constant");
static_assert(Seraphis::kAetherDepthSmoothMs == 300.0f,
              "plan 3.5.2 amendment A10: the AETHER-DEPTH family is 300 ms");

/// Enough blocks for a BODY-family class-(b) smoother to settle. N_block = 4
/// (plan §3.5.2); 8 is double that, so "the counter stopped rising" is measured
/// well past convergence and never at it.
constexpr std::size_t kClassBBlocks = 8;

// Every dropdown entry count this TU hard-codes into dropdownNorm(), pinned
// against the ONE table registration and formatting both read (plan §2.2), so a
// re-sized list fails here rather than silently selecting the wrong index.
static_assert(Seraphis::kSeedLabels.size() == 16, "dropdownNorm(i, 16) for kSeedId");
static_assert(Seraphis::kSyncNoteLabels.size() == 8, "dropdownNorm(i, 8) for kMorphSyncNoteId");
static_assert(Seraphis::kStateCountLabels.size() == 3,
              "dropdownNorm(i, 3) for kMorphStateCountId");
static_assert(Seraphis::kSpectralStateLabels.size() == 5,
              "dropdownNorm(i, 5) for the four morph slot IDs");
static_assert(Seraphis::kMorphStateCountMin + 2 == 4,
              "index 2 of kStateCountLabels selects state count 4");

// =============================================================================
// Spec C-6's 91 registered IDs (SC-006 automates every one of them, every block)
// =============================================================================

constexpr ParamID kAllParamIds[] = {
    // --- Global (0-99) -------------------------------------------------------
    Seraphis::kMasterGainId, Seraphis::kPolyphonyId, Seraphis::kSoftLimitId,
    Seraphis::kSeedId,
    // --- Macros (100-199) ----------------------------------------------------
    Seraphis::kMacroDreamId, Seraphis::kMacroBloomId, Seraphis::kMacroDissolveId,
    Seraphis::kMacroGravityId, Seraphis::kMacroEntropyId,
    // --- Harmonic Cloud (200-399) -------------------------------------------
    Seraphis::kCloudRichnessId, Seraphis::kCloudInharmonicityId, Seraphis::kCloudTiltId,
    Seraphis::kCloudMutationId, Seraphis::kCloudGravityId, Seraphis::kCloudDriftDepthId,
    Seraphis::kCloudDriftSmoothnessId, Seraphis::kCloudStereoSpreadId,
    Seraphis::kCloudAttackId, Seraphis::kCloudDecayId, Seraphis::kCloudEnvOffsetSpreadId,
    // --- Spectral Morph / Entropy (400-599) ---------------------------------
    Seraphis::kMorphEntropyId, Seraphis::kMorphBloomId, Seraphis::kMorphPositionId,
    Seraphis::kMorphTravelModeId, Seraphis::kMorphTravelRateId, Seraphis::kMorphSyncId,
    Seraphis::kMorphSyncNoteId, Seraphis::kMorphWaypointIntervalId,
    Seraphis::kMorphStateCountId, Seraphis::kMorphState0Id, Seraphis::kMorphState1Id,
    Seraphis::kMorphState2Id, Seraphis::kMorphState3Id,
    // --- Life Modulators (600-699) + Voice Envelope (700-799) ---------------
    Seraphis::kLifeSpatialDepthId, Seraphis::kLifeSpatialRateId,
    Seraphis::kLifeSpatialCouplingId, Seraphis::kLifeSpatialGrowthId,
    Seraphis::kLifeVoiceWidthId, Seraphis::kEnvModeId, Seraphis::kEnvGrowthDurationId,
    Seraphis::kEnvStage0MsId, Seraphis::kEnvStage1MsId, Seraphis::kEnvReleaseMsId,
    // --- Continuous Body (800-999) ------------------------------------------
    Seraphis::kBodyMaterialId, Seraphis::kBodyResonanceId, Seraphis::kBodyDampingId,
    Seraphis::kBodyKeyTrackingId, Seraphis::kBodyDriveId, Seraphis::kBodyMixId,
    Seraphis::kBodyCloudMixId, Seraphis::kBodyCloudDecayId, Seraphis::kBodyCloudSizeId,
    Seraphis::kBodyCloudDampingId, Seraphis::kBodyWidthId, Seraphis::kBodyInputAgcId,
    Seraphis::kBodyResonatorBypassId,
    // --- Granular Atmosphere (1000-1199) ------------------------------------
    Seraphis::kAtmosLevelId, Seraphis::kAtmosBlurId, Seraphis::kAtmosDensityId,
    Seraphis::kAtmosGrainSecondsId, Seraphis::kAtmosDriftDepthId,
    Seraphis::kAtmosPanSpreadId, Seraphis::kAtmosDecorrelationId,
    Seraphis::kAtmosFreezeMixId, Seraphis::kAtmosFreezeId,
    Seraphis::kAtmosDriftSmoothnessId, Seraphis::kAtmosDriftRangeId,
    Seraphis::kAtmosJitterId, Seraphis::kAtmosPositionId,
    Seraphis::kAtmosPositionSpreadId, Seraphis::kAtmosPitchId,
    Seraphis::kAtmosPitchSpreadId, Seraphis::kAtmosGrainEnvelopeId,
    // --- Aether Space (1200-1399) -------------------------------------------
    Seraphis::kAetherMixId, Seraphis::kAetherSizeId, Seraphis::kAetherDensityId,
    Seraphis::kAetherDecayId, Seraphis::kAetherFreezeId,
    Seraphis::kAetherDimensionalityId, Seraphis::kAetherDampingId,
    Seraphis::kAetherPreDelayId, Seraphis::kAetherModDepthId,
    Seraphis::kAetherModSmoothnessId, Seraphis::kAetherShimmerOctaveId,
    Seraphis::kAetherShimmerFifthId, Seraphis::kAetherBloomSendId,
    Seraphis::kAetherBloomDecayId, Seraphis::kAetherSpectralDiffusionId,
    Seraphis::kAetherSizeBreathDepthId, Seraphis::kAetherTideDepthId,
    Seraphis::kAetherWidthId,
};

constexpr std::size_t kParamIdCount = sizeof(kAllParamIds) / sizeof(kAllParamIds[0]);
static_assert(kParamIdCount == 91, "SC-006 automates ALL 91 registered parameters");

// =============================================================================
// Stream plumbing (the shape unit/state_v2_test.cpp uses)
// =============================================================================

struct StreamReleaser {
    void operator()(Steinberg::MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<Steinberg::MemoryStream, StreamReleaser>;

void rewindStream(Steinberg::MemoryStream& s) {
    s.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
}

[[nodiscard]] StreamPtr captureState(Seraphis::Processor& proc) {
    StreamPtr s(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(s.get()) == Steinberg::kResultOk);
    rewindStream(*s);
    return s;
}

// =============================================================================
// Rig helpers
// =============================================================================

struct ParamPoint {
    ParamID id;
    double normalized;
};

/// The `L` form: a StringListParameter's normalized value for entry `index` of
/// `count`. The exact inverse of Seraphis::detail::morphDropdownIndex
/// (morph_params.h:130-134), which every pack's dropdown handler shares.
[[nodiscard]] constexpr double dropdownNorm(int index, int count) {
    return (count <= 1) ? 0.0 : static_cast<double>(index) / static_cast<double>(count - 1);
}

[[nodiscard]] std::unique_ptr<Fixture> makeRig() {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    return fx;
}

/// `blocks` process() calls with no capture. processBlock() delivers whatever is
/// already queued in fx.params on the FIRST call and clears it, so a caller that
/// wants a change delivered queues it and then calls this with blocks >= 1.
void renderQuiet(Fixture& fx, std::size_t blocks) {
    bool ok = true;
    for (std::size_t b = 0; b < blocks; ++b) {
        ok = ok && (fx.processBlock(kBlock) == Steinberg::kResultOk);
    }
    REQUIRE(ok);
}

void pushParams(Fixture& fx, std::initializer_list<ParamPoint> points) {
    for (const ParamPoint& p : points) {
        fx.setParam(p.id, p.normalized);
    }
}

// =============================================================================
// The FR-041a counter surface, as one value
// =============================================================================

struct Counters {
    std::size_t voice = 0;            ///< applyVoiceParams SUCCESSES
    std::size_t spectralSuccess = 0;  ///< applySpectralStates applications that CLEARED
    std::size_t spectralAttempt = 0;  ///< applySpectralStates ATTEMPTS, incl. retries
    std::size_t aether = 0;           ///< applyAetherParams INVOCATIONS
    std::size_t targetBase = 0;       ///< setTargetBase INVOCATIONS
    std::size_t engSeed = 0;
    std::size_t engPolyphony = 0;
    std::size_t engSoftLimit = 0;
    std::size_t engFreeze = 0;

    bool operator==(const Counters&) const = default;
};

[[nodiscard]] Counters snapshot(const Seraphis::Processor& p) {
    Counters c;
    c.voice = p.applyVoiceParamsCallCountForTest();
    c.spectralSuccess = p.applySpectralStatesCallCountForTest();
    c.spectralAttempt = p.applySpectralStatesAttemptCountForTest();
    c.aether = p.applyAetherParamsCallCountForTest();
    c.targetBase = p.setTargetBasePushCountForTest();
    c.engSeed = p.engSeedPushCountForTest();
    c.engPolyphony = p.engPolyphonyPushCountForTest();
    c.engSoftLimit = p.engSoftLimitPushCountForTest();
    c.engFreeze = p.engFreezePushCountForTest();
    return c;
}

/// All 27 macro-matrix bases, through the one route FR-041a exposes.
[[nodiscard]] std::array<float, kNumTargets> baseSnapshot(const Seraphis::Processor& p) {
    std::array<float, kNumTargets> v{};
    for (std::size_t t = 0; t < kNumTargets; ++t) {
        v[t] = p.macroMatrixForTest().getTargetBase(static_cast<Target>(t));
    }
    return v;
}

/// A prepared rig rendered to the point where EVERY counter has gone quiet -
/// asserted, not assumed: the snapshot taken half-way through the settle window
/// must equal the one taken at its end.
[[nodiscard]] std::unique_ptr<Fixture> makeSettledRig() {
    auto fx = makeRig();
    renderQuiet(*fx, kSettleBlocks / 2u);
    const Counters mid = snapshot(*fx->proc);
    renderQuiet(*fx, kSettleBlocks - kSettleBlocks / 2u);
    const Counters end = snapshot(*fx->proc);
    REQUIRE(end == mid);
    return fx;
}

[[nodiscard]] Krate::DSP::SeraphisEngine& engineOf(Fixture& fx) {
    Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    return *engine;
}

[[nodiscard]] std::array<std::uint32_t, kMaxVoices> rejectionSnapshot(
    const Krate::DSP::SeraphisEngine& engine) {
    std::array<std::uint32_t, kMaxVoices> r{};
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        r[v] = engine.getVoice(v).getRejectedConfigureTimeCallCount();
    }
    return r;
}

// =============================================================================
// Measurement helpers (local, so no criterion depends on a shared estimator)
// =============================================================================

/// RMS(a - b) / RMS(a) over the common prefix - the same "relative RMS
/// differential" integration/param_reach_test.cpp's CFG rows gate on.
[[nodiscard]] double relativeRmsDifference(const std::vector<float>& a,
                                           const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    double diff = 0.0;
    double ref = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        diff += d * d;
        ref += static_cast<double>(a[i]) * static_cast<double>(a[i]);
    }
    if (!(ref > 0.0)) {
        return 0.0;
    }
    return std::sqrt(diff / ref);
}

/// SC-013 clause 1's "unchanged". The two arms differ only in a write the gate
/// REJECTED, so the two renders come out of the identical code path in the
/// identical binary; the floor is two orders of magnitude below the 1 %
/// "changed" floor clause 3 uses, so the two clauses cannot both pass on noise.
constexpr double kUnchangedFloor = 1.0e-4;

/// SC-013 clause 3's "renders the new spectrum", the same 1 % relative-RMS floor
/// SC-003's CFG rows use.
constexpr double kChangedFloor = 1.0e-2;

}  // namespace

// =============================================================================
// SC-006 - nothing allocates on the audio thread
// =============================================================================
//
// THE READING FORM IS NORMATIVE and is unit/lifecycle_test.cpp:505-520's:
// AllocationScope::getAllocationCount() returns a member assigned only in the
// DESTRUCTOR (allocation_detector.h:81-87), so reading it inside the scope
// yields a value-initialized 0 and passes unconditionally. The count is taken
// from the LIVE atomic on the detector singleton while tracking is still on,
// stored in a local, and asserted after the scope has closed - Catch2's REQUIRE
// is itself an allocating expression and must never run inside.
// =============================================================================

TEST_CASE("Seraphis_ParameterPush_IsAllocationFree", "[seraphis][params][alloc]") {
    auto fx = makeRig();

    // 4 seconds at 48 kHz / 512.
    constexpr std::size_t kRenderBlocks =
        static_cast<std::size_t>(4.0 * kSampleRate) / kBlockSamples;
    static_assert(kRenderBlocks == 375, "SC-006 renders 4 s at 48 kHz / 512");

    // A deterministic triangle per (block, row): every one of the 91 lanes moves
    // every block, and no two lanes move together. Automating IDENTICAL values
    // would let the on-change guards short-circuit the very paths this criterion
    // measures (refreshSpectralSlotFromFactory early-returns on an unchanged slot
    // id, processor.cpp:1250-1252).
    const auto valueFor = [](std::size_t block, std::size_t row) {
        const double phase = std::fmod(static_cast<double>(block) * 0.017
                                           + static_cast<double>(row) * 0.0137,
                                       1.0);
        return (phase < 0.5) ? (phase * 2.0) : (2.0 - phase * 2.0);
    };

    const auto script = [&](std::size_t b, Krate::Test::EventList&,
                            SeraphisTest::ParameterChanges& pc) {
        for (std::size_t r = 0; r < kParamIdCount; ++r) {
            pc.addQueue(kAllParamIds[r]).addTestPoint(0, valueFor(b, r));
        }
        if (b == 0) {
            // A sounding voice puts FR-046's rejection/retry path inside the
            // measured window as well - `before[16]` is a stack std::array, and
            // this is where that would show up if it were not.
            fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        }
    };

    // The preset the measured window loads. Captured BEFORE the scope opens, and
    // read-only inside it (MemoryStream::seek moves a cursor and nothing else).
    StreamPtr preset = captureState(*fx->proc);

    // WARM EVERYTHING THE HARNESS OWNS, or the case measures the harness growing:
    // the capture vectors to their FINAL size, the 91 parameter queues and their
    // point storage by running the very script that is about to be measured, and
    // the setState path once.
    fx->reserveCapture(kRenderBlocks * kBlockSamples);
    fx->renderBlocks(4, kBlockSamples, script);
    fx->capturedL.clear();  // clear() keeps capacity; resize() would not
    fx->capturedR.clear();
    rewindStream(*preset);
    REQUIRE(fx->proc->setState(preset.get()) == Steinberg::kResultOk);
    REQUIRE(fx->processBlock(kBlock) == Steinberg::kResultOk);

    constexpr std::size_t kFirstHalf = kRenderBlocks / 2u;

    std::size_t allocations = 0;
    Steinberg::tresult setStateResult = Steinberg::kResultFalse;
    {
        TestHelpers::AllocationScope scope;
        fx->renderBlocks(kFirstHalf, kBlockSamples, script);
        // FR-041b's 2.1 KiB staging handoff and FR-047's pushAllSurfaces() are
        // both raised HERE, so both land inside the measured window: setState()
        // publishes a staging index, and the next process() copies it and
        // consumes the force-push request.
        preset->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        setStateResult = fx->proc->setState(preset.get());
        fx->renderBlocks(kRenderBlocks - kFirstHalf, kBlockSamples, script);
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    REQUIRE(setStateResult == Steinberg::kResultOk);
    REQUIRE(allocations == 0u);
    REQUIRE(fx->checkCanaries());
    // The handoff must have been consumed on the AUDIO thread, which is what
    // puts the 2.1 KiB copy inside the window at all.
    REQUIRE(fx->proc->spectralHandoffConsumeCountForTest() >= 2u);

    // LIVENESS PROBE - a SEPARATE, never nested scope. Nesting is silently wrong
    // in both directions: the inner ctor's startTracking() RESETS the outer count
    // (allocation_detector.h:31-34) and the inner dtor's stopTracking() switches
    // tracking off for the outer scope too (:37-40). Without it the assertion
    // above would be vacuous on a detector that counts nothing.
    std::size_t probe = 0;
    {
        TestHelpers::AllocationScope scope;
        // `volatile` is load-bearing: [expr.new]/10 lets a compiler elide an
        // otherwise-unobserved new/delete pair even when the global allocation
        // functions are replaced.
        int* volatile deliberate = new int(7);
        probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
        delete deliberate;
    }
    REQUIRE(probe >= 1u);
}

// =============================================================================
// SC-007 - on-change-only push is real, on all four surfaces
// =============================================================================

TEST_CASE("Seraphis_ParameterPush_IsOnChangeOnly", "[seraphis][params][cadence]") {

    // -------------------------------------------------------------------------
    // The quiescent arm: 200 blocks, no parameter change, CONSTANT tempo, every
    // class-(b) smoother settled (pushAllSurfaces() raised snapParamSmoothers_ at
    // prepare, so the first advanceParamSmoothers() SNAPPED all nine onto their
    // registered defaults - processor.cpp:1757-1762).
    // -------------------------------------------------------------------------
    SECTION("quiescent 200 blocks - the whole counter table") {
        auto fx = makeRig();
        fx->setTempo(120.0, 4, 4, /*tempoValid*/ true, /*sigValid*/ true);
        renderQuiet(*fx, kQuiescentBlocks);

        const Counters c = snapshot(*fx->proc);
        CHECK(c.voice == 1u);            // FR-047's prepare-time push, once
        CHECK(c.spectralSuccess == 1u);  // the first in-process() push, once
        CHECK(c.spectralAttempt == 1u);  // ... and it did not retry
        CHECK(c.aether == 1u);
        CHECK(c.targetBase == kNumTargets);  // one per MB target, exactly once
        CHECK(c.engSeed == 1u);
        CHECK(c.engSoftLimit == 1u);
        CHECK(c.engFreeze == 1u);
        // See the banner: polyphony is the ONE ENG value plan 3.4 mandates is NOT
        // re-pushed at prepare, because prepare() already installed it through
        // makeSeraphisEngineConfig and re-pushing would re-arm sumGain_ and move a
        // counter Phase 8 tests assert (processor.cpp:507-513,
        // integration/param_flow_test.cpp:608).
        CHECK(c.engPolyphony == 0u);

        // The flag cleared on the very first push and never re-raised.
        REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
    }

    // -------------------------------------------------------------------------
    // class-(a) VP: exactly +1.
    // ID 803 (kBodyKeyTrackingId) is VP-routed (processor.cpp:191) and class (a)
    // (plan 3.5.3: keyTrackSmoother_, continuous_body.h:4222).
    // -------------------------------------------------------------------------
    SECTION("class-(a) VP change (803) pushes applyVoiceParams exactly once") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kBodyKeyTrackingId, 0.40}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.voice == before.voice + 1u);
        REQUIRE(after.targetBase == before.targetBase);
        REQUIRE(after.aether == before.aether);

        // ... and it does not keep pushing.
        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).voice == after.voice);

        const Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        const auto expected = static_cast<float>(
            Seraphis::kBodyKeyTrackingMin
            + 0.40 * (Seraphis::kBodyKeyTrackingMax - Seraphis::kBodyKeyTrackingMin));
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            REQUIRE(engine.getVoice(v).body().getKeyTracking() == expected);
        }
    }

    // -------------------------------------------------------------------------
    // class-(b) VP: +1 ... +N_chunk, it must STOP rising, and the settled
    // read-back must equal the target EXACTLY (the plan 3.3 wasVoiceClassBSettling_
    // latch is what makes the last chunk's exact value reach the voice).
    // ID 801 is the ONE class-(b) VP row (plan 3.5.3).
    // -------------------------------------------------------------------------
    SECTION("class-(b) VP change (801) is bounded, stops rising, and lands exactly") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        constexpr double kResonanceNorm = 0.55;  // from the 0.70 default: D = 0.15
        pushParams(*fx, {{Seraphis::kBodyResonanceId, kResonanceNorm}});
        renderQuiet(*fx, kClassBBlocks);

        const Counters after = snapshot(*fx->proc);
        const std::size_t delta = after.voice - before.voice;
        INFO("applyVoiceParams delta = " << delta << ", N_chunk = " << kNChunkBody);
        REQUIRE(delta >= 1u);
        REQUIRE(delta <= kNChunkBody);

        // "must stop rising" - a push that never stops fails this row even though
        // every individual increment is within range.
        renderQuiet(*fx, kSettleBlocks * 2u);
        REQUIRE(snapshot(*fx->proc).voice == after.voice);

        const Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        const auto expected = static_cast<float>(
            Seraphis::kBodyResonanceMin
            + kResonanceNorm * (Seraphis::kBodyResonanceMax - Seraphis::kBodyResonanceMin));
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            REQUIRE(engine.getVoice(v).body().getResonance() == expected);
        }
    }

    // -------------------------------------------------------------------------
    // class-(a) MB: setTargetBase exactly +1, applyVoiceParams +0.
    // ID 200 (kCloudRichnessId) is MB-routed (processor.cpp:142-150).
    //
    // The +0 half is SC-007's THIRD separation clause, and it is the one that
    // catches a markDirty() that bumps voiceParamGeneration_ on an MB edit: the
    // other two clauses (AE -> voice, VP -> aether) cannot see it.
    // -------------------------------------------------------------------------
    SECTION("class-(a) MB change (200) pushes one base and NOT applyVoiceParams") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);
        const auto basesBefore = baseSnapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kCloudRichnessId, 0.85}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.targetBase == before.targetBase + 1u);
        REQUIRE(after.voice == before.voice);  // the third separation clause
        REQUIRE(after.aether == before.aether);

        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).targetBase == after.targetBase);

        // Exactly one of the 27 moved.
        const auto basesAfter = baseSnapshot(*fx->proc);
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            INFO("macro target " << t);
            if (t == static_cast<std::size_t>(Target::CloudRichness)) {
                REQUIRE(basesAfter[t] != basesBefore[t]);
            } else {
                REQUIRE(basesAfter[t] == basesBefore[t]);
            }
        }
    }

    // -------------------------------------------------------------------------
    // class-(b) MB: +1 ... +N_chunk on setTargetBase, it must STOP rising, and
    // the other 26 targets must be untouched. ID 802 is MB-routed
    // (processor.cpp:187-188) and class (b) (plan 3.5.3).
    // -------------------------------------------------------------------------
    SECTION("class-(b) MB change (802) is bounded and touches ONE target") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);
        const auto basesBefore = baseSnapshot(*fx->proc);

        constexpr double kDampingNorm = 0.40;  // from the 0.25 default: D = 0.15
        pushParams(*fx, {{Seraphis::kBodyDampingId, kDampingNorm}});
        renderQuiet(*fx, kClassBBlocks);

        const Counters after = snapshot(*fx->proc);
        const std::size_t delta = after.targetBase - before.targetBase;
        INFO("setTargetBase delta = " << delta << ", N_chunk = " << kNChunkBody);
        REQUIRE(delta >= 1u);
        REQUIRE(delta <= kNChunkBody);
        REQUIRE(after.voice == before.voice);  // MB never runs the VP fan-out

        renderQuiet(*fx, kSettleBlocks * 2u);
        REQUIRE(snapshot(*fx->proc).targetBase == after.targetBase);

        const auto expected = static_cast<float>(
            Seraphis::kBodyDampingMin
            + kDampingNorm * (Seraphis::kBodyDampingMax - Seraphis::kBodyDampingMin));
        const auto basesAfter = baseSnapshot(*fx->proc);
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            INFO("macro target " << t);
            if (t == static_cast<std::size_t>(Target::BodyDamping)) {
                REQUIRE(basesAfter[t] == expected);
            } else {
                REQUIRE(basesAfter[t] == basesBefore[t]);
            }
        }
    }

    // -------------------------------------------------------------------------
    // A macro knob is NOT an MB base: the macro push owns those five smoothers
    // (plan 3.5.5), so setTargetBase must not move at all - even though the macro
    // smoother is un-settled for the whole window and the slice loop is therefore
    // subdividing on the 64-sample grid.
    // -------------------------------------------------------------------------
    SECTION("a macro knob (101) does not push any base") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);
        const auto basesBefore = baseSnapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kMacroBloomId, 0.50}});
        renderQuiet(*fx, kSettleBlocks);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.targetBase == before.targetBase);
        REQUIRE(after.aether == before.aether);
        REQUIRE(after.spectralAttempt == before.spectralAttempt);

        const auto basesAfter = baseSnapshot(*fx->proc);
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            INFO("macro target " << t);
            REQUIRE(basesAfter[t] == basesBefore[t]);
        }
    }

    // -------------------------------------------------------------------------
    // CFG: one attempt, one success, then silence. The engine is quiescent, so
    // FR-046's gate accepts on the first attempt.
    // -------------------------------------------------------------------------
    SECTION("a CFG change (409) is applied exactly once") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kMorphState0Id, dropdownNorm(4, 5)}});  // Breath
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.spectralSuccess == before.spectralSuccess + 1u);
        REQUIRE(after.spectralAttempt == before.spectralAttempt + 1u);
        REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());

        renderQuiet(*fx, kSettleBlocks);
        const Counters idle = snapshot(*fx->proc);
        REQUIRE(idle.spectralSuccess == after.spectralSuccess);
        REQUIRE(idle.spectralAttempt == after.spectralAttempt);
    }

    // -------------------------------------------------------------------------
    // AE: exactly +1, and it must NOT move applyVoiceParams (separation clause 1).
    // ID 1206 (kAetherDampingId) is AE-routed (processor.cpp:239).
    // -------------------------------------------------------------------------
    SECTION("an AE change (1206) pushes applyAetherParams once and never the voices") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kAetherDampingId, 0.80}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.aether == before.aether + 1u);
        REQUIRE(after.voice == before.voice);        // separation clause 1
        REQUIRE(after.targetBase == before.targetBase);

        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).aether == after.aether);
    }

    // -------------------------------------------------------------------------
    // Separation clause 2: a VP change must not move applyAetherParams. A single
    // shared generation counter passes every row above and fails this.
    // -------------------------------------------------------------------------
    SECTION("a VP change (803) never pushes applyAetherParams") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kBodyKeyTrackingId, 0.25}});
        renderQuiet(*fx, kSettleBlocks);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.voice == before.voice + 1u);
        REQUIRE(after.aether == before.aether);
        REQUIRE(after.spectralAttempt == before.spectralAttempt);
    }

    // -------------------------------------------------------------------------
    // FR-045's ENG cadence: a change of ID 3 moves the SEED counter and nothing
    // else; a change of ID 1008 moves the FREEZE counter and nothing else.
    // -------------------------------------------------------------------------
    SECTION("an ENG seed change (3) moves only the seed counter") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kSeedId, dropdownNorm(5, 16)}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.engSeed == before.engSeed + 1u);
        REQUIRE(after.engPolyphony == before.engPolyphony);
        REQUIRE(after.engSoftLimit == before.engSoftLimit);
        REQUIRE(after.engFreeze == before.engFreeze);

        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).engSeed == after.engSeed);
    }

    SECTION("an ENG freeze change (1008) moves only the freeze counter") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);

        pushParams(*fx, {{Seraphis::kAtmosFreezeId, 1.0}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.engFreeze == before.engFreeze + 1u);
        REQUIRE(after.engSeed == before.engSeed);
        REQUIRE(after.engPolyphony == before.engPolyphony);
        REQUIRE(after.engSoftLimit == before.engSoftLimit);

        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).engFreeze == after.engFreeze);
    }

    // -------------------------------------------------------------------------
    // The setState() sub-clause (plan 3.4's SurfaceInvalidation::PresetLoad arm).
    // A forced re-setSeed() for an UNCHANGED seed is the drift/tide discontinuity
    // aether_reverb.h:2351-2358 documents, and is exactly why kSeedId is exempt
    // from SC-005 clauses 1-3.
    // -------------------------------------------------------------------------
    SECTION("setState with an unchanged seed leaves the ENG counters unmoved") {
        auto fx = makeSettledRig();
        StreamPtr same = captureState(*fx->proc);

        const Counters before = snapshot(*fx->proc);
        REQUIRE(fx->proc->setState(same.get()) == Steinberg::kResultOk);
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.engSeed == before.engSeed);
        REQUIRE(after.engPolyphony == before.engPolyphony);

        // The same stream shape carrying a DIFFERENT seed index moves it by one.
        // The donor differs from `fx` in kSeedId ALONE, so nothing else in the
        // stream can be what moved the counter.
        auto donor = makeRig();
        pushParams(*donor, {{Seraphis::kSeedId, dropdownNorm(7, 16)}});
        renderQuiet(*donor, 1);
        StreamPtr other = captureState(*donor->proc);

        REQUIRE(fx->proc->setState(other.get()) == Steinberg::kResultOk);
        renderQuiet(*fx, 1);

        const Counters third = snapshot(*fx->proc);
        REQUIRE(third.engSeed == after.engSeed + 1u);
        REQUIRE(third.engPolyphony == after.engPolyphony);
    }

    // -------------------------------------------------------------------------
    // The moving-tempo clause (Q3 / FR-042 amendment 2). Tempo is not a
    // parameter, so a moving one legitimately dirties voiceParamGeneration_ -
    // but ONLY when the DERIVED rate actually moved.
    // -------------------------------------------------------------------------
    SECTION("a moving tempo pushes on exactly the blocks the derived rate moved") {
        auto fx = makeRig();
        fx->setTempo(120.0, 4, 4, /*tempoValid*/ true, /*sigValid*/ true);

        // Sync ON at "1 Bar" (C-7 index 4): the derived rate appears once.
        pushParams(*fx, {{Seraphis::kMorphSyncId, 1.0},
                         {Seraphis::kMorphSyncNoteId, dropdownNorm(4, 8)}});
        renderQuiet(*fx, 1);
        const Counters syncOn = snapshot(*fx->proc);

        // CONSTANT tempo with sync on: not one further push.
        renderQuiet(*fx, 20);
        const Counters constantTempo = snapshot(*fx->proc);
        REQUIRE(constantTempo.voice == syncOn.voice);
        REQUIRE(constantTempo.aether == syncOn.aether);
        REQUIRE(constantTempo.spectralAttempt == syncOn.spectralAttempt);

        // Ramp 1 BPM per block. d(rate) = 1 / (60 * 4) = 4.17e-3 journeys/s,
        // ~2500x above kSyncedRateEpsilon (kMinTravelRate * 1e-3 = 1.67e-6), so
        // EVERY block of the ramp is a block "in which the derived rate moved".
        constexpr std::size_t kRampBlocks = 20;
        double bpm = 120.0;
        bool ok = true;
        for (std::size_t b = 0; b < kRampBlocks; ++b) {
            bpm += 1.0;
            fx->setTempo(bpm, 4, 4, true, true);
            ok = ok && (fx->processBlock(kBlock) == Steinberg::kResultOk);
        }
        REQUIRE(ok);

        const Counters ramped = snapshot(*fx->proc);
        REQUIRE(ramped.voice == constantTempo.voice + kRampBlocks);
        REQUIRE(ramped.aether == constantTempo.aether);
        REQUIRE(ramped.spectralAttempt == constantTempo.spectralAttempt);
    }

    // -------------------------------------------------------------------------
    // The retry-bound clause - what applySpectralStatesAttemptCountForTest()
    // exists for. With ONE voice sounding and fifteen idle, the attempt counter
    // rises once per block (the retry is per block by design) but the PER-VOICE
    // work does not: the fifteen accepted on the FIRST attempt and their bits
    // left spectralRetryMask_ there, so applySpectralStates never writes them
    // again. The mask itself is private; "their bits left the mask" is observed
    // in the form it has - the fifteen already hold the pushed state count after
    // attempt 1 and their rejection counters never move again.
    // -------------------------------------------------------------------------
    SECTION("the CFG retry is bounded to the voices that are still rejecting") {
        auto fx = makeRig();
        // 1 ms envelope stages so the note reaches its sustain immediately; it is
        // held (no note-off) for the whole clause, which is what keeps one voice
        // un-configurable.
        pushParams(*fx, {{Seraphis::kEnvStage0MsId, 0.0},
                         {Seraphis::kEnvStage1MsId, 0.0},
                         {Seraphis::kEnvReleaseMsId, 0.0}});
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        renderQuiet(*fx, 4);

        Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        const auto rejBefore = rejectionSnapshot(engine);
        const Counters before = snapshot(*fx->proc);

        // count 4 (index 2 of three entries) - a CFG push with an observable
        // per-voice read-back.
        pushParams(*fx, {{Seraphis::kMorphStateCountId, dropdownNorm(2, 3)}});
        renderQuiet(*fx, 1);

        const auto rejFirst = rejectionSnapshot(engine);
        std::size_t sounding = kMaxVoices;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (rejFirst[v] != rejBefore[v]) {
                INFO("a second voice rejected: " << v);
                REQUIRE(sounding == kMaxVoices);
                sounding = v;
            }
        }
        REQUIRE(sounding < kMaxVoices);

        const Counters afterFirst = snapshot(*fx->proc);
        REQUIRE(afterFirst.spectralAttempt == before.spectralAttempt + 1u);
        REQUIRE(afterFirst.spectralSuccess == before.spectralSuccess);
        REQUIRE(fx->proc->spectralStatesPendingForTest());

        // The fifteen accepted on attempt 1.
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (v == sounding) {
                continue;
            }
            INFO("idle voice " << v);
            REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
        }

        constexpr std::size_t kRetryBlocks = 20;
        renderQuiet(*fx, kRetryBlocks);

        const Counters afterRetries = snapshot(*fx->proc);
        REQUIRE(afterRetries.spectralAttempt == afterFirst.spectralAttempt + kRetryBlocks);
        REQUIRE(afterRetries.spectralSuccess == afterFirst.spectralSuccess);
        REQUIRE(fx->proc->spectralStatesPendingForTest());

        const auto rejAfter = rejectionSnapshot(engine);
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            if (v == sounding) {
                REQUIRE(rejAfter[v] > rejFirst[v]);  // still rejecting, still retried
            } else {
                REQUIRE(rejAfter[v] == rejFirst[v]);  // never written again
            }
        }
    }
}

// =============================================================================
// SC-013 - configure-time gating is respected, and the write converges
// =============================================================================

namespace {

/// The settings every SC-013 arm shares. Each one exists so the voice actually
/// RETIRES within the wait window (clause 3's "every voice has become quiescent"
/// is otherwise unreachable in bounded time) and so the captured spectrum is the
/// morph engine's and not a tail:
///   - the three envelope times at their 1 ms C-6 floor (MB-routed; at the FR-060
///     neutral every macro contribution is exactly 0, so the base IS the applied
///     value - seraphis_macro_matrix.h:775-782), so the note-off is effectively
///     immediate;
///   - the ATMOSPHERE LEVEL at 0. Not cosmetic: the default grain length is 4 s
///     (seraphis_voice.h:323), so grains in flight at the note-off would hold
///     SeraphisVoice::level_ above kTailSilenceThreshold for seconds and
///     isFinished() would never turn true inside the wait window;
///   - the BODY MIX at 0, which continuous_body.h:1208 documents as "dry", so the
///     resonator's own T60 cannot outlive the envelope either and the output is
///     the harmonic cloud - exactly the thing the spectral slot controls;
///   - the body's cloud-feedback blend at 0 and the Aether mix at 0, so nothing
///     downstream of the voice colours the differential.
[[nodiscard]] std::vector<ParamPoint> gateArmSettings() {
    return {{Seraphis::kEnvStage0MsId, 0.0},  {Seraphis::kEnvStage1MsId, 0.0},
            {Seraphis::kEnvReleaseMsId, 0.0}, {Seraphis::kAtmosLevelId, 0.0},
            {Seraphis::kBodyMixId, 0.0},      {Seraphis::kBodyCloudMixId, 0.0},
            {Seraphis::kAetherMixId, 0.0}};
}

void pushVector(Fixture& fx, const std::vector<ParamPoint>& points) {
    for (const ParamPoint& p : points) {
        fx.setParam(p.id, p.normalized);
    }
}

/// One second of note 60 at 48 kHz / 512.
constexpr std::size_t kSpectrumBlocks = 94;

/// Blocks rendered after the note-off while waiting for the voice to retire.
/// BOTH arms render all of them, whether or not the retry has already cleared,
/// because the two fixtures' render histories must stay aligned up to the second
/// note-on for clause 3's differential to mean anything.
///
/// 300 blocks is 3.2 s. The retirement path is a 1 ms release, then level_'s
/// 100 ms one-pole (seraphis_voice.h:370-372) falling under kTailSilenceThreshold
/// = 1e-5, then kQuiescentChunksToRetire = 4 chunks (:147, :155) - Phase 7 derives
/// ~1.15 s for the whole of it - so this is ~2.8x the expected figure and the
/// clause fails loudly (REQUIRE(cleared)) rather than silently if it is not.
constexpr std::size_t kQuiescenceBlocks = 300;

}  // namespace

TEST_CASE("Seraphis_SpectralStateAssignment_HonoursGate", "[seraphis][params][spectral]") {

    // -------------------------------------------------------------------------
    // Clause 1 - assigning a new state while a voice is SOUNDING leaves that
    // voice's audible spectrum unchanged for the note, and increments
    // getRejectedConfigureTimeCallCount().
    //
    // The two arms differ ONLY in whether the CFG change is pushed at block 10.
    // Both render the same note with the same settings from the same fixed seed,
    // so a rejected write is the only thing that could move the output - and it
    // must not.
    // -------------------------------------------------------------------------
    SECTION("clause 1 - a sounding voice rejects and its spectrum does not move") {
        std::vector<float> arms[2];
        std::uint32_t rejectionRise = 0;

        for (int arm = 0; arm < 2; ++arm) {
            INFO("arm " << arm);
            auto fx = makeRig();
            const std::vector<ParamPoint> settings = gateArmSettings();

            fx->renderBlocks(
                kSpectrumBlocks, kBlockSamples,
                [&](std::size_t b, Krate::Test::EventList&,
                    SeraphisTest::ParameterChanges& pc) {
                    if (b == 0) {
                        for (const ParamPoint& p : settings) {
                            pc.addQueue(p.id).addTestPoint(0, p.normalized);
                        }
                        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
                    }
                    if (b == 10 && arm == 1) {
                        pc.addQueue(Seraphis::kMorphState0Id)
                            .addTestPoint(0, dropdownNorm(4, 5));  // Breath
                        pc.addQueue(Seraphis::kMorphStateCountId)
                            .addTestPoint(0, dropdownNorm(2, 3));  // count 4
                    }
                });

            if (arm == 1) {
                const Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    rejectionRise = std::max(
                        rejectionRise, engine.getVoice(v).getRejectedConfigureTimeCallCount());
                }
                // FR-046 leaves the flag set while a targeted voice keeps rejecting.
                REQUIRE(fx->proc->spectralStatesPendingForTest());
            }

            arms[arm] = fx->capturedL;
            REQUIRE(fx->checkCanaries());
        }

        REQUIRE(rejectionRise > 0u);  // the sounding voice DID reject
        const double delta = relativeRmsDifference(arms[0], arms[1]);
        INFO("relative RMS difference = " << delta);
        REQUIRE(delta < kUnchangedFloor);
    }

    // -------------------------------------------------------------------------
    // Clause 2 - the pending flag STAYS SET while a targeted voice keeps
    // rejecting, and the parameter atomics are never cleared or reset in
    // response to a rejection (FR-046 clause 4: a rejected write is retried, not
    // rolled back).
    // -------------------------------------------------------------------------
    SECTION("clause 2 - the flag stays set and the atomics are never rolled back") {
        auto fx = makeRig();
        pushVector(*fx, gateArmSettings());
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        renderQuiet(*fx, 4);

        constexpr int kSlotIndex = 4;   // Breath
        constexpr int kStateCount = 4;  // dropdown index 2 of three entries
        pushParams(*fx, {{Seraphis::kMorphState0Id, dropdownNorm(kSlotIndex, 5)},
                         {Seraphis::kMorphStateCountId, dropdownNorm(2, 3)}});
        renderQuiet(*fx, 1);

        Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        REQUIRE(fx->proc->spectralStatesPendingForTest());

        auto previous = rejectionSnapshot(engine);
        for (std::size_t round = 0; round < 8; ++round) {
            INFO("retry round " << round);
            renderQuiet(*fx, 4);

            // The flag is still set ...
            REQUIRE(fx->proc->spectralStatesPendingForTest());
            // ... the rejection counter is still rising on the sounding voice ...
            const auto current = rejectionSnapshot(engine);
            bool anyRose = false;
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                anyRose = anyRose || (current[v] > previous[v]);
            }
            REQUIRE(anyRose);
            previous = current;

            // ... and the atomics still carry exactly what was pushed.
            const Seraphis::MorphParams& mp = fx->proc->morphParamsForTest();
            REQUIRE(mp.slot[0].load(std::memory_order_relaxed) == kSlotIndex);
            REQUIRE(mp.stateCount.load(std::memory_order_relaxed) == kStateCount);
        }
    }

    // -------------------------------------------------------------------------
    // Clause 3 - on the FIRST block after every voice has become quiescent the
    // retry succeeds: the flag clears, the rejection counter stops rising on
    // every voice, morph().getStateCount() equals the pushed count on ALL SIXTEEN
    // slots, and the next note-on renders the new spectrum.
    //
    // The control arm runs the IDENTICAL sequence with the slot and count
    // dropdowns at their registered defaults, so the two fixtures share their
    // whole render history up to the second note-on and the differential can only
    // come from the state that was installed.
    // -------------------------------------------------------------------------
    SECTION("clause 3 - the retry converges on the first quiescent block") {
        constexpr int kDefaultSlotIndex = 0;  // C-6's default for slot 0
        constexpr int kNewSlotIndex = 4;      // Breath
        constexpr int kDefaultCountIndex = 0;
        constexpr int kNewCountIndex = 2;  // count 4

        std::vector<float> arms[2];

        for (int arm = 0; arm < 2; ++arm) {
            INFO("arm " << arm);
            const bool changed = (arm == 1);
            const int slotIndex = changed ? kNewSlotIndex : kDefaultSlotIndex;
            const int countIndex = changed ? kNewCountIndex : kDefaultCountIndex;

            auto fx = makeRig();
            pushVector(*fx, gateArmSettings());
            fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
            renderQuiet(*fx, 8);

            // The CFG push lands while the voice is sounding, so the gate rejects.
            pushParams(*fx, {{Seraphis::kMorphState0Id, dropdownNorm(slotIndex, 5)},
                             {Seraphis::kMorphStateCountId, dropdownNorm(countIndex, 3)}});
            renderQuiet(*fx, 1);

            Krate::DSP::SeraphisEngine& engine = engineOf(*fx);

            // Identify the sounding voice through the clause-1 observable.
            std::size_t sounding = kMaxVoices;
            const auto rejAtPush = rejectionSnapshot(engine);
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (rejAtPush[v] > 0u) {
                    sounding = v;
                }
            }
            if (changed) {
                REQUIRE(sounding < kMaxVoices);
                REQUIRE(fx->proc->spectralStatesPendingForTest());
            }

            // Note-off, then wait. Both arms render the SAME fixed number of
            // blocks, so their histories stay aligned.
            fx->pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNote, 0.0f, 0);
            bool cleared = false;
            for (std::size_t b = 0; b < kQuiescenceBlocks; ++b) {
                const bool pendingBefore = fx->proc->spectralStatesPendingForTest();
                const bool quiescentBefore =
                    (sounding < kMaxVoices) && engine.getVoice(sounding).isFinished();

                renderQuiet(*fx, 1);

                if (changed && pendingBefore && quiescentBefore && !cleared) {
                    // THE FIRST BLOCK after the voice became quiescent.
                    INFO("block " << b);
                    REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
                    cleared = true;
                }
            }

            if (changed) {
                REQUIRE(cleared);

                // The rejection counter stops rising on EVERY voice.
                const auto settledRejections = rejectionSnapshot(engine);
                renderQuiet(*fx, 8);
                const auto laterRejections = rejectionSnapshot(engine);
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(laterRejections[v] == settledRejections[v]);
                }

                // ALL SIXTEEN voices hold the same state.
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
                }
            } else {
                renderQuiet(*fx, 8);
            }

            // The NEXT note-on renders whatever spectrum is now installed.
            fx->capturedL.clear();
            fx->capturedR.clear();
            fx->renderBlocks(kSpectrumBlocks, kBlockSamples,
                             [&](std::size_t b, Krate::Test::EventList&,
                                 SeraphisTest::ParameterChanges&) {
                                 if (b == 0) {
                                     fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                                                   kNote, 0.8f, 0);
                                 }
                             });
            arms[arm] = fx->capturedL;
            REQUIRE(fx->checkCanaries());
        }

        const double delta = relativeRmsDifference(arms[0], arms[1]);
        INFO("relative RMS difference = " << delta);
        REQUIRE(delta >= kChangedFloor);
    }
}
