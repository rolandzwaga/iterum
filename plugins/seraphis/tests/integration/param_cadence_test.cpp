// ==============================================================================
// Seraphis - Push cadence and allocation tests (Phase 9 + Phase 10)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.7)
//            specs/seraphis-phase10-effects/spec.md     (SC-018, T020)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-006  no allocation on the audio thread across the whole parameter push
//   SC-007  the push cadence - a block-rate surface is pushed once per block,
//           and the configure-time surface only when a value actually moved
//   SC-013  the FR-046 retry - a rejected configure-time push is retried, per
//           voice, and stops retrying once the voice accepts
//   SC-018  (Phase 10) the effects cadence - clauses (a)-(e), at the bottom of
//           this file: FR-008's conditional reset, FR-011's per-block drift
//           advance, FR-012's per-CALL bypass predicate, FR-019's Route::FX
//           bumping no generation counter, and FR-007's "no chunk while the
//           send is neither active nor draining"
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
#include <span>
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
static_assert(Seraphis::kSpectralStateLabels.size() == 10,
              "dropdownNorm(i, 10) for the four morph slot IDs");
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

/// All 29 macro-matrix bases, through the one route FR-041a exposes. (27 through
/// Phase 10; Phase 11 / C-10 appended FxDelaySend and FxWanderDepth, which is why
/// the loop bound is kNumTargets and never a transcribed literal.)
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

/// "renders the new spectrum", the same 1 % relative-RMS floor SC-003's CFG rows
/// use. Phase 11 FR-033a made this the floor for BOTH spectral-assignment
/// clauses: the old `kUnchangedFloor` (1e-4) existed only for the arm that
/// asserted a sounding voice's spectrum does NOT move, which is exactly the
/// behaviour D-1 removed, so it has no remaining consumer.
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

        pushParams(*fx, {{.id = Seraphis::kBodyKeyTrackingId, .normalized = 0.40}});
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
        pushParams(*fx, {{.id = Seraphis::kBodyResonanceId, .normalized = kResonanceNorm}});
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

        pushParams(*fx, {{.id = Seraphis::kCloudRichnessId, .normalized = 0.85}});
        renderQuiet(*fx, 1);

        const Counters after = snapshot(*fx->proc);
        REQUIRE(after.targetBase == before.targetBase + 1u);
        REQUIRE(after.voice == before.voice);  // the third separation clause
        REQUIRE(after.aether == before.aether);

        renderQuiet(*fx, kSettleBlocks);
        REQUIRE(snapshot(*fx->proc).targetBase == after.targetBase);

        // Exactly one of the 29 moved.
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
    // the other 28 targets must be untouched. ID 802 is MB-routed
    // (processor.cpp:187-188) and class (b) (plan 3.5.3).
    // -------------------------------------------------------------------------
    SECTION("class-(b) MB change (802) is bounded and touches ONE target") {
        auto fx = makeSettledRig();
        const Counters before = snapshot(*fx->proc);
        const auto basesBefore = baseSnapshot(*fx->proc);

        constexpr double kDampingNorm = 0.40;  // from the 0.25 default: D = 0.15
        pushParams(*fx, {{.id = Seraphis::kBodyDampingId, .normalized = kDampingNorm}});
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

        pushParams(*fx, {{.id = Seraphis::kMacroBloomId, .normalized = 0.50}});
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

        pushParams(*fx, {{.id = Seraphis::kMorphState0Id, .normalized = dropdownNorm(4, 10)}});  // Breath
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

        pushParams(*fx, {{.id = Seraphis::kAetherDampingId, .normalized = 0.80}});
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

        pushParams(*fx, {{.id = Seraphis::kBodyKeyTrackingId, .normalized = 0.25}});
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

        pushParams(*fx, {{.id = Seraphis::kSeedId, .normalized = dropdownNorm(5, 16)}});
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

        pushParams(*fx, {{.id = Seraphis::kAtmosFreezeId, .normalized = 1.0}});
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
        pushParams(*donor, {{.id = Seraphis::kSeedId, .normalized = dropdownNorm(7, 16)}});
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
        pushParams(*fx, {{.id = Seraphis::kMorphSyncId, .normalized = 1.0},
                         {.id = Seraphis::kMorphSyncNoteId, .normalized = dropdownNorm(4, 8)}});
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
    // exists for.
    //
    // PHASE 11 D-1 / FR-033a REWROTE THIS CLAUSE. It used to read "the CFG retry
    // is bounded to the voices that are still rejecting", and held a note for the
    // whole clause precisely so ONE voice would keep rejecting and the retry
    // would keep running every block. FR-033a removed that gate from
    // setSpectralState/setSpectralStateCount, so a held note no longer rejects
    // anything and the bound is now the STRONGER one: with a note sounding, the
    // whole pool accepts on attempt 1, spectralRetryMask_ empties in the same
    // block, and applySpectralStates is never called again until something new
    // is pushed. The mask is private; "it emptied" is observed in the form it
    // has - the flag cleared, the success counter moved, and the attempt counter
    // stops.
    // -------------------------------------------------------------------------
    SECTION("the CFG push converges on attempt 1 even with a note sounding") {
        auto fx = makeRig();
        // 1 ms envelope stages so the note reaches its sustain immediately; it is
        // held (no note-off) for the whole clause, so the push lands on a voice
        // that HAS sounded and is NOT finished - the state Phase 9's gate rejected.
        pushParams(*fx, {{.id = Seraphis::kEnvStage0MsId, .normalized = 0.0},
                         {.id = Seraphis::kEnvStage1MsId, .normalized = 0.0},
                         {.id = Seraphis::kEnvReleaseMsId, .normalized = 0.0}});
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        renderQuiet(*fx, 4);

        Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        // Non-vacuity: a voice really is sounding for the whole clause.
        REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        const auto rejBefore = rejectionSnapshot(engine);
        const Counters before = snapshot(*fx->proc);

        // count 4 (index 2 of three entries) - a CFG push with an observable
        // per-voice read-back.
        pushParams(*fx, {{.id = Seraphis::kMorphStateCountId, .normalized = dropdownNorm(2, 3)}});
        renderQuiet(*fx, 1);

        // NOTHING rejected - not the sounding voice, not the fifteen idle ones.
        const auto rejFirst = rejectionSnapshot(engine);
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            REQUIRE(rejFirst[v] == rejBefore[v]);
        }

        const Counters afterFirst = snapshot(*fx->proc);
        REQUIRE(afterFirst.spectralAttempt == before.spectralAttempt + 1u);
        REQUIRE(afterFirst.spectralSuccess == before.spectralSuccess + 1u);
        REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());

        // ALL SIXTEEN took it on attempt 1, sounding slot included.
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
        }
        REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});  // still held

        constexpr std::size_t kRetryBlocks = 20;
        renderQuiet(*fx, kRetryBlocks);

        // ... and it never runs again: no attempt, no success, no rejection.
        const Counters afterRetries = snapshot(*fx->proc);
        REQUIRE(afterRetries.spectralAttempt == afterFirst.spectralAttempt);
        REQUIRE(afterRetries.spectralSuccess == afterFirst.spectralSuccess);
        REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());

        const auto rejAfter = rejectionSnapshot(engine);
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            INFO("voice " << v);
            REQUIRE(rejAfter[v] == rejFirst[v]);
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
    return {{.id = Seraphis::kEnvStage0MsId, .normalized = 0.0},
            {.id = Seraphis::kEnvStage1MsId, .normalized = 0.0},
            {.id = Seraphis::kEnvReleaseMsId, .normalized = 0.0},
            {.id = Seraphis::kAtmosLevelId, .normalized = 0.0},
            {.id = Seraphis::kBodyMixId, .normalized = 0.0},
            {.id = Seraphis::kBodyCloudMixId, .normalized = 0.0},
            {.id = Seraphis::kAetherMixId, .normalized = 0.0}};
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

// PHASE 11 D-1 / FR-033a INVERTED THIS TEST CASE, which shipped in Phase 9 as
// `Seraphis_SpectralStateAssignment_HonoursGate` (SC-013). The gate it pinned -
// SeraphisVoice::setSpectralState / setSpectralStateCount rejecting while the
// voice sounds - was deliberately removed, because SpectralMorphEngine::setState
// absorbs a live swap through the FR-047 fade and Phase 3's FR-042/FR-044 prove
// that path continuity-safe. Every clause below now asserts the OPPOSITE
// outcome through the SAME observables: the push lands on the sounding voice, on
// the first block, audibly, and the rejection counter never moves.
//
// What did NOT change, and is still asserted here: FR-046 clause 4 - the
// parameter atomics are never rolled back - and FR-046 clause 3 - the pending
// flag clears exactly when every targeted voice has taken the push.
// spec.md (seraphis-phase11-ui) FR-033a, SC-028 - SC-030.
TEST_CASE("Seraphis_SpectralStateAssignment_ReachesSoundingVoice",
          "[seraphis][params][spectral]") {

    // -------------------------------------------------------------------------
    // Clause 1 - assigning a new state while a voice is SOUNDING moves that
    // voice's audible spectrum WITHIN the note, and does NOT increment
    // getRejectedConfigureTimeCallCount().
    //
    // The two arms differ ONLY in whether the CFG change is pushed at block 10.
    // Both render the same note with the same settings from the same fixed seed,
    // so the accepted write is the only thing that could move the output - and it
    // must.
    // -------------------------------------------------------------------------
    SECTION("clause 1 - a sounding voice accepts and its spectrum moves") {
        std::vector<float> arms[2];

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
                            .addTestPoint(0, dropdownNorm(4, 10));  // Breath
                        pc.addQueue(Seraphis::kMorphStateCountId)
                            .addTestPoint(0, dropdownNorm(2, 3));  // count 4
                    }
                });

            const Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
            // The note is never released, so the push at block 10 landed - and
            // the render ended - with the voice still sounding.
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                INFO("voice " << v);
                REQUIRE(engine.getVoice(v).getRejectedConfigureTimeCallCount() == 0u);
            }
            if (arm == 1) {
                // FR-046 clause 3: every targeted voice took it, so the flag is
                // down - it is not left set waiting for a quiescent block.
                REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
                }
            }

            arms[arm] = fx->capturedL;
            REQUIRE(fx->checkCanaries());
        }

        const double delta = relativeRmsDifference(arms[0], arms[1]);
        INFO("relative RMS difference = " << delta);
        REQUIRE(delta >= kChangedFloor);
    }

    // -------------------------------------------------------------------------
    // Clause 2 - the pending flag CLEARS on the block the push lands, and the
    // parameter atomics are never cleared or reset by the push (FR-046 clause 4:
    // the atomics are the record of what the user asked for, and nothing in the
    // apply path writes back to them).
    //
    // Phase 9's version of this clause held the note so the flag would STAY set
    // and the rejection counter would keep rising; FR-033a removed the rejection,
    // so the surviving obligations - clause 3's "clears when every targeted voice
    // took it" and clause 4's "never rolled back" - are asserted in their new
    // form, over the same held note.
    // -------------------------------------------------------------------------
    SECTION("clause 2 - the flag clears on the first block and the atomics are never rolled back") {
        auto fx = makeRig();
        pushVector(*fx, gateArmSettings());
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        renderQuiet(*fx, 4);

        constexpr int kSlotIndex = 4;   // Breath
        constexpr int kStateCount = 4;  // dropdown index 2 of three entries
        pushParams(*fx, {{.id = Seraphis::kMorphState0Id, .normalized = dropdownNorm(kSlotIndex, 10)},
                         {.id = Seraphis::kMorphStateCountId, .normalized = dropdownNorm(2, 3)}});
        renderQuiet(*fx, 1);

        Krate::DSP::SeraphisEngine& engine = engineOf(*fx);
        // Non-vacuity: the note is held for the whole clause, so every assertion
        // below is made against a voice that HAS sounded and is NOT finished.
        REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());

        const auto atPush = rejectionSnapshot(engine);
        for (std::size_t round = 0; round < 8; ++round) {
            INFO("retry round " << round);
            renderQuiet(*fx, 4);

            // The flag stays down - nothing re-raises it while the note runs ...
            REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
            // ... no voice ever rejects, sounding one included ...
            const auto current = rejectionSnapshot(engine);
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                INFO("voice " << v);
                REQUIRE(current[v] == atPush[v]);
                REQUIRE(engine.getVoice(v).morph().getStateCount() == kStateCount);
            }
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});

            // ... and the atomics still carry exactly what was pushed.
            const Seraphis::MorphParams& mp = fx->proc->morphParamsForTest();
            REQUIRE(mp.slot[0].load(std::memory_order_relaxed) == kSlotIndex);
            REQUIRE(mp.stateCount.load(std::memory_order_relaxed) == kStateCount);
        }
    }

    // -------------------------------------------------------------------------
    // Clause 3 - the push converges on the FIRST block, with the note still
    // sounding: the flag clears there, no rejection counter ever moves,
    // morph().getStateCount() equals the pushed count on ALL SIXTEEN slots, and
    // the state stays installed across the note-off and the whole quiescence
    // wait, so the next note-on renders the new spectrum.
    //
    // Phase 9's version waited for quiescence because that was the only block on
    // which the retry could succeed; FR-033a made that wait unnecessary. The wait
    // is RETAINED here anyway - both arms still render kQuiescenceBlocks - because
    // the two fixtures' render histories must stay aligned up to the second
    // note-on for the differential to mean anything, and because "the installed
    // state survives retirement" is worth pinning.
    //
    // The control arm runs the IDENTICAL sequence with the slot and count
    // dropdowns at their registered defaults, so the two fixtures share their
    // whole render history up to the second note-on and the differential can only
    // come from the state that was installed.
    // -------------------------------------------------------------------------
    SECTION("clause 3 - the push converges on the first block and the state survives retirement") {
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

            // The CFG push lands while the voice is sounding, and is taken (FR-033a).
            pushParams(*fx, {{.id = Seraphis::kMorphState0Id, .normalized = dropdownNorm(slotIndex, 10)},
                             {.id = Seraphis::kMorphStateCountId, .normalized = dropdownNorm(countIndex, 3)}});
            renderQuiet(*fx, 1);

            Krate::DSP::SeraphisEngine& engine = engineOf(*fx);

            // Non-vacuity: the push above landed on a voice that HAS sounded and
            // is NOT finished - the state Phase 9's gate rejected.
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
            const auto rejAtPush = rejectionSnapshot(engine);
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                INFO("voice " << v);
                REQUIRE(rejAtPush[v] == 0u);
            }
            if (changed) {
                // Converged on THIS block - no waiting for quiescence.
                REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
                }
            }

            // Note-off, then wait. Both arms render the SAME fixed number of
            // blocks, so their histories stay aligned.
            fx->pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNote, 0.0f, 0);
            bool retired = false;
            for (std::size_t b = 0; b < kQuiescenceBlocks; ++b) {
                renderQuiet(*fx, 1);
                retired = retired || (engine.getActiveVoiceCount() == std::size_t{0});
            }
            // The wait really does retire the voice, so "survives retirement"
            // below is a claim about a retired pool, not an unreleased one.
            REQUIRE(retired);

            renderQuiet(*fx, 8);

            if (changed) {
                // Nothing re-raised the flag and nothing rejected across the whole
                // note-off, retirement and settle window.
                REQUIRE_FALSE(fx->proc->spectralStatesPendingForTest());
                const auto settledRejections = rejectionSnapshot(engine);
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(settledRejections[v] == 0u);
                }

                // ALL SIXTEEN voices still hold the state that was pushed.
                for (std::size_t v = 0; v < kMaxVoices; ++v) {
                    INFO("voice " << v);
                    REQUIRE(engine.getVoice(v).morph().getStateCount() == 4);
                }
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

// =============================================================================
// SC-018 (Phase 10) - the effects cadence, clauses (a)-(e)
// =============================================================================
//
// EVERY COUNTER READ HERE IS FR-041's, AND NONE OF THEM IS EVER RESET (they are
// plain std::size_t members, processor.h:1029-1033), so every clause below is a
// DELTA across a known number of process() calls rather than an absolute.
//
// THE SEED IS NEVER TOUCHED IN THIS CASE, and that is load-bearing rather than
// incidental: pushEffectsParams()' seed-change burst calls spectralDelay_.reset()
// and increments the SAME counter FR-008's deferred reset does
// (processor.cpp:1621-1638), so an arm that moved kSeedId could not attribute a
// reset to a transition at all.
//
// NO std::isnan / std::isinf ANYWHERE - see this file's top banner.
// =============================================================================

namespace {

// -----------------------------------------------------------------------------
// FR-009a's window, on this file's 48 kHz / 512 grid
// -----------------------------------------------------------------------------
/// kFxSendDrainMs (processor.h:156 - one SpectralDelay::kMaxDelayMs) in samples.
constexpr std::size_t kSamplesPerMsAt48k = 48;
constexpr std::size_t kDrainWindowSamples =
    static_cast<std::size_t>(Seraphis::kFxSendDrainMs) * kSamplesPerMsAt48k;
static_assert(kDrainWindowSamples == 96000u, "kFxSendDrainMs = 2000 ms at 48 kHz");

/// The drain window rounded UP to this file's block grid: ceil(96000 / 512).
constexpr std::size_t kDrainWindowBlocks =
    (kDrainWindowSamples + kBlockSamples - 1u) / kBlockSamples;
static_assert(kDrainWindowBlocks == 188u, "ceil(96000 / 512)");

/// FR-008 condition (a) is "bypassed for LONGER than kFxSendDrainMs", and
/// fxBypassedSamples_ advances by the WHOLE block on every bypassed process()
/// call (processor.cpp:2290-2294). 190 blocks is 97 280 samples, i.e. strictly
/// past the window with just over two blocks of margin - so the arms below fail
/// on the requirement rather than on an off-by-one at the boundary.
constexpr std::size_t kLongBypassBlocks = 190;
static_assert(kLongBypassBlocks * kBlockSamples > kDrainWindowSamples,
              "SC-018(a): the qualifying engage needs a WHOLE kFxSendDrainMs of prior bypass");

/// FR-009a's "shorter than the drain window" excursion: 19 x 512 = 9728 samples
/// = 202.7 ms, the same 200 ms figure SC-011a pins.
constexpr std::size_t kShortExcursionBlocks = 19;
static_assert(kShortExcursionBlocks * kBlockSamples < kDrainWindowSamples,
              "SC-018(a): the excursion must be well INSIDE kFxSendDrainMs");

/// Blocks rendered either side of a transition when only a counter is being read.
constexpr std::size_t kFxObserveBlocks = 24;

// -----------------------------------------------------------------------------
// The Phase 10 counter surface, as one value
// -----------------------------------------------------------------------------
struct FxCounters {
    std::size_t resets = 0;         ///< FR-041 clause 2 - FR-008's deferred reset()
    std::size_t chunks = 0;         ///< clause 7 - one per SpectralDelay::process()
    std::size_t driftBlocks = 0;    ///< clause 4 - FR-011's per-block advance
    std::size_t predicateEvals = 0; ///< clause 5 - FR-012's per-CALL predicate
    std::size_t effectsPushes = 0;  ///< clause 3 - FR-022/FR-024 pushes issued
    std::size_t stageCalls = 0;     ///< clause 1's per-process()-CALL divisor
    std::size_t voice = 0;          ///< Phase 9, processor.h:172
    std::size_t aether = 0;         ///< Phase 9, processor.h:186
    std::size_t engSoftLimit = 0;   ///< Phase 9, re-used by FR-021's sole writer
};

[[nodiscard]] FxCounters fxSnapshot(const Seraphis::Processor& p) {
    FxCounters c;
    c.resets = p.spectralDelayResetCountForTest();
    c.chunks = p.sendChunkCountForTest();
    c.driftBlocks = p.widthDriftBlockCountForTest();
    c.predicateEvals = p.bypassPredicateEvalCountForTest();
    c.effectsPushes = p.effectsPushCountForTest();
    c.stageCalls = p.effectsStageProcessCallsForTest();
    c.voice = p.applyVoiceParamsCallCountForTest();
    c.aether = p.applyAetherParamsCallCountForTest();
    c.engSoftLimit = p.engSoftLimitPushCountForTest();
    return c;
}

/// A prepared rig holding one note for the whole arm, rendered until the send has
/// been CONTINUOUSLY BYPASSED for longer than kFxSendDrainMs - i.e. FR-008
/// condition (a) is satisfied and only condition (b) is left to decide the reset.
///
/// The note is held (there is no note-off) because a bypass excursion's drain is
/// fed SILENCE and terminates early on kFxSendDrainFloor (FR-009a): with a silent
/// BUS as well, the drain would end on energy in every arm and the excursion
/// clause would stop describing the window it names.
[[nodiscard]] std::unique_ptr<Fixture> makeLongBypassedRig() {
    auto fx = makeRig();
    fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
    renderQuiet(*fx, kLongBypassBlocks);
    // The precondition, asserted rather than assumed: at the C-6 defaults the
    // send has never run a chunk, which is FR-007 and is what makes
    // fxBypassedSamples_ the whole render.
    REQUIRE(fx->proc->sendChunkCountForTest() == std::size_t{0});
    return fx;
}

/// Largest |L - R| over the pre-output-stage tap, i.e. C-1 step 5's OUTPUT.
///
/// The tap is the correct measurement point and the plugin output is not: satL_
/// and satR_ are two independent TapeSaturator instances whose filter state has
/// already diverged on the stereo bus that preceded the collapse, so a mono
/// collapse is exact BEFORE processOutputStage and only approximate after it.
[[nodiscard]] float maxChannelDifference(std::span<const float> l, std::span<const float> r) {
    const std::size_t n = std::min(l.size(), r.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(l[i] - r[i]));
    }
    return worst;
}

// -----------------------------------------------------------------------------
// Clause (d)'s drive table - all SIXTEEN C-6 IDs, every value DIFFERENT from the
// registered default, so each row is a real change rather than a no-op the
// on-change guards would swallow.
// -----------------------------------------------------------------------------
constexpr ParamPoint kFxDrivePoints[] = {
    {.id = Seraphis::kFxSaturationId, .normalized = 0.80},                    // 0.15 -> 0.80   (Route::ENG)
    {.id = Seraphis::kFxDelayMixId, .normalized = 1.00},                      // 0    -> 1      (engages the send)
    {.id = Seraphis::kFxDelayTimeId, .normalized = 0.25},                     // 250  -> 500 ms
    {.id = Seraphis::kFxDelaySpreadId, .normalized = 0.10},                   // 0    -> 200 ms
    {.id = Seraphis::kFxDelaySpreadDirectionId, .normalized = dropdownNorm(2, 3)},   // LowToHigh -> CenterOut
    {.id = Seraphis::kFxDelayFeedbackId, .normalized = 0.50},                 // 0.35 -> 0.475
    {.id = Seraphis::kFxDelayTiltId, .normalized = 0.75},                     // 0    -> +0.5
    {.id = Seraphis::kFxDelayDiffusionId, .normalized = 0.60},                // 0.30 -> 0.60
    {.id = Seraphis::kFxDelayWidthId, .normalized = 0.80},                    // 0.50 -> 0.80
    {.id = Seraphis::kFxDelaySyncId, .normalized = 1.00},                     // off  -> on
    {.id = Seraphis::kFxDelaySyncNoteId, .normalized = dropdownNorm(2, 10)},  // index 7 -> index 2
    {.id = Seraphis::kFxSpectralFreezeId, .normalized = 1.00},                // off  -> on
    {.id = Seraphis::kFxWidthId, .normalized = 0.75},                         // 100  -> 150 %
    {.id = Seraphis::kFxWanderDepthId, .normalized = 0.50},                   // 0    -> 0.50
    {.id = Seraphis::kFxWanderRateId, .normalized = 0.25},                    // 0.50 -> 0.25
    {.id = Seraphis::kFxAzimuthDepthId, .normalized = 0.50},                  // 0    -> 0.50
};

constexpr std::size_t kFxDrivePointCount = sizeof(kFxDrivePoints) / sizeof(kFxDrivePoints[0]);
static_assert(kFxDrivePointCount == 16, "C-6 registers exactly 16 effects IDs");

/// The ten pack rows whose push is counted in effectsPushes_ unconditionally once
/// their value moves: 1411, 1412, 1413, 1414, 1415, 1416, 1417, 1418, 1419, 1442
/// (processor.cpp:1645-1763). 1410, 1440, 1441 and 1443 are plugin-owned and
/// deliberately absent from that helper (:1582-1586), and 1430's push is the
/// COMPOSED freezeReady of plan D-5, which is deferred until the send has
/// consumed kFxFreezePrimeSamples of live bus - so this is a floor, not an
/// equality.
constexpr std::size_t kFxCountedPackRows = 10;

}  // namespace

TEST_CASE("Effects push cadence", "[seraphis][params][cadence][effects]") {

    // =========================================================================
    // (a) FR-008 - the reset is CONDITIONAL, and both conditions are load-bearing
    // =========================================================================
    SECTION("(a) a qualifying engage resets the send exactly once") {
        auto fx = makeLongBypassedRig();
        const FxCounters before = fxSnapshot(*fx->proc);
        REQUIRE(before.resets == std::size_t{0});

        // The engage: mix 0 -> 1 after a WHOLE drain window of bypass, with the
        // freeze OFF. Both FR-008 conditions hold, so exactly one reset() runs -
        // on the first fill-chunk boundary of this engage (processor.cpp:1961).
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 1.0}});
        renderQuiet(*fx, kFxObserveBlocks);

        const FxCounters after = fxSnapshot(*fx->proc);
        INFO("resets " << before.resets << " -> " << after.resets);
        REQUIRE(after.resets == before.resets + 1u);
        // Non-vacuity: the engage really happened, so the +1 describes a
        // transition and not an idle rig.
        REQUIRE(after.chunks > before.chunks);

        // ... and it does not keep resetting while the send stays engaged.
        renderQuiet(*fx, kFxObserveBlocks);
        REQUIRE(fx->proc->spectralDelayResetCountForTest() == after.resets);
    }

    SECTION("(a) a freeze-FORCED engage resets nothing (FR-023a)") {
        auto fx = makeLongBypassedRig();
        const FxCounters before = fxSnapshot(*fx->proc);
        REQUIRE(before.resets == std::size_t{0});

        // Condition (a) is satisfied EXACTLY as in the arm above - the ONLY
        // difference is that the engage is freeze-forced, which is what makes
        // this the control for condition (b). kFxDelayMixId stays at its C-6
        // default of 0 throughout.
        pushParams(*fx, {{.id = Seraphis::kFxSpectralFreezeId, .normalized = 1.0}});
        renderQuiet(*fx, kFxObserveBlocks);

        const FxCounters after = fxSnapshot(*fx->proc);
        INFO("freeze-forced engage: resets " << before.resets << " -> " << after.resets);
        // A reset here would clear the frozen spectrum buffers and wasFrozen_
        // (spectral_delay.h:256-257, :276-277) at the very instant the capture is
        // meant to happen.
        CHECK(after.resets == before.resets);
        // Non-vacuity: FR-023a's forced engage DID run the send at mix 0.
        REQUIRE(after.chunks > before.chunks);
    }

    SECTION("(a) a sub-drain mix excursion resets nothing (FR-009a)") {
        auto fx = makeLongBypassedRig();

        // Engage first (this one qualifies, so it is allowed its single reset),
        // then take the counter as the baseline for the excursion itself.
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 1.0}});
        renderQuiet(*fx, kFxObserveBlocks);
        const FxCounters engaged = fxSnapshot(*fx->proc);
        REQUIRE(engaged.resets == std::size_t{1});

        // EXACTLY 0 - FR-007's predicate is an exact comparison, and an epsilon
        // would leave the send engaged and this arm measuring nothing.
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 0.0}});
        renderQuiet(*fx, kShortExcursionBlocks);
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 1.0}});
        renderQuiet(*fx, kFxObserveBlocks);

        const FxCounters after = fxSnapshot(*fx->proc);
        INFO("excursion: resets " << engaged.resets << " -> " << after.resets);
        // The excursion is far shorter than kFxSendDrainMs, so FR-008 condition
        // (a) cannot hold on the re-engage and the tail survives.
        CHECK(after.resets == engaged.resets);
        REQUIRE(after.chunks > engaged.chunks);
    }

    // =========================================================================
    // (b) FR-011 - the drifts advance once per process() CALL in BOTH bypass
    //     states, so re-engaging continues a walk instead of restarting one
    // =========================================================================
    SECTION("(b) the wander sources advance per block under both bypass states") {
        auto fx = makeRig();

        // --- state 1: the stage is SKIPPED (the C-6 defaults are FR-010's exact
        //     identity: width 100 %, both depths 0), so the advance happens in
        //     the pre-slice block (processor.cpp:1092-1095).
        const FxCounters start = fxSnapshot(*fx->proc);
        renderQuiet(*fx, kFxObserveBlocks);
        const FxCounters bypassed = fxSnapshot(*fx->proc);
        INFO("skipped-stage delta = " << (bypassed.driftBlocks - start.driftBlocks));
        REQUIRE(bypassed.driftBlocks == start.driftBlocks + kFxObserveBlocks);
        // The divisor and the drift counter are both per-CALL, so they move
        // together - a build that moved either per SLICE fails here.
        REQUIRE(bypassed.stageCalls == start.stageCalls + kFxObserveBlocks);

        // --- state 2: the stage RUNS. The advance moves inside it, onto the
        //     absolute 64-sample control grid (processor.cpp:2141-2144) - eight
        //     grid boundaries per 512-sample block - and the counter must STILL
        //     be one per block, because it counts blocks and not advances.
        pushParams(*fx, {{.id = Seraphis::kFxWanderDepthId, .normalized = 0.50},
                         {.id = Seraphis::kFxAzimuthDepthId, .normalized = 0.50}});
        renderQuiet(*fx, kFxObserveBlocks);
        const FxCounters engaged = fxSnapshot(*fx->proc);
        INFO("running-stage delta = " << (engaged.driftBlocks - bypassed.driftBlocks));
        REQUIRE(engaged.driftBlocks == bypassed.driftBlocks + kFxObserveBlocks);

        // --- and with the SEND engaged as well, which is the other bypass state
        //     FR-011 names.
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 1.0}});
        renderQuiet(*fx, kFxObserveBlocks);
        const FxCounters both = fxSnapshot(*fx->proc);
        REQUIRE(both.driftBlocks == engaged.driftBlocks + kFxObserveBlocks);
        REQUIRE(both.chunks > engaged.chunks);  // the send really was running
    }

    // =========================================================================
    // (c) FR-012 - the bypass predicate is evaluated once per process() CALL,
    //     never per SLICE
    // =========================================================================
    SECTION("(c) the bypass predicate is evaluated once per process() call") {
        constexpr std::size_t kSlicedBlocks = 16;
        // Three events per block at three DISTINCT offsets, ascending (VST3
        // requires sorted events). The slice loop cuts at the two non-zero ones
        // (processor.cpp:1168-1190), i.e. THREE slices per block - [0, 170),
        // [170, 340), [340, 512) - so a per-slice evaluation would land at 3x the
        // call count.
        constexpr Steinberg::int32 kOffsetB = 170;
        constexpr Steinberg::int32 kOffsetC = 340;
        static_assert(kOffsetB > 0 && kOffsetC > kOffsetB
                          && static_cast<std::size_t>(kOffsetC) < kBlockSamples,
                      "the three offsets must be strictly inside the block, in order");

        auto fx = makeSettledRig();
        const FxCounters before = fxSnapshot(*fx->proc);

        fx->renderBlocks(kSlicedBlocks, kBlockSamples,
                         [&](std::size_t, Krate::Test::EventList&,
                             SeraphisTest::ParameterChanges&) {
                             fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
                             fx->pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNote, 0.0f,
                                           kOffsetB);
                             fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                                           static_cast<Steinberg::int16>(kNote + 7), 0.8f,
                                           kOffsetC);
                         });

        const FxCounters after = fxSnapshot(*fx->proc);
        INFO("predicate evaluations = " << (after.predicateEvals - before.predicateEvals)
                                        << " over " << kSlicedBlocks << " process() calls");
        REQUIRE(after.predicateEvals == before.predicateEvals + kSlicedBlocks);
        REQUIRE(after.stageCalls == before.stageCalls + kSlicedBlocks);
        REQUIRE(fx->checkCanaries());

        // ---------------------------------------------------------------------
        // NON-VACUITY: the render above really WAS subdivided. Two same-binary,
        // same-session arms differing only in ONE event's sampleOffset must
        // render differently - which they can only do if the loop cut the block
        // at that offset. Without this, a processor that ignored sampleOffset
        // entirely (one slice per block, always) would satisfy the clause above
        // trivially.
        // ---------------------------------------------------------------------
        constexpr std::size_t kWitnessBlocks = 3;
        constexpr Steinberg::int32 kWitnessOffset = 300;
        std::vector<float> witness[2];
        for (int arm = 0; arm < 2; ++arm) {
            INFO("witness arm " << arm);
            auto w = makeRig();
            const Steinberg::int32 offset = (arm == 0) ? 0 : kWitnessOffset;
            w->renderBlocks(kWitnessBlocks, kBlockSamples,
                            [&](std::size_t b, Krate::Test::EventList&,
                                SeraphisTest::ParameterChanges&) {
                                if (b == 0) {
                                    w->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote,
                                                 0.8f, offset);
                                }
                            });
            witness[arm] = w->capturedL;
            REQUIRE(w->checkCanaries());
        }
        const double witnessDelta = relativeRmsDifference(witness[0], witness[1]);
        INFO("witness relative RMS difference = " << witnessDelta);
        REQUIRE(witnessDelta >= kChangedFloor);
    }

    // =========================================================================
    // (d) FR-019 - Route::FX bumps NO generation counter
    // =========================================================================
    SECTION("(d) driving the effects surface never re-pushes the voices or the Aether") {
        auto fx = makeSettledRig();
        const FxCounters before = fxSnapshot(*fx->proc);

        // One ID per block, so a single failing row is named by its own INFO
        // rather than hidden inside a batch.
        for (auto kFxDrivePoint : kFxDrivePoints) {
            INFO("effects ID " << kFxDrivePoint.id);
            pushParams(*fx, {kFxDrivePoint});
            renderQuiet(*fx, 1);

            const FxCounters step = fxSnapshot(*fx->proc);
            // ID 1400 is Route::ENG and 1410-1443 are Route::FX; NEITHER route
            // may bump voiceParamGeneration_ or aetherParamGeneration_.
            REQUIRE(step.voice == before.voice);
            REQUIRE(step.aether == before.aether);
        }

        // The three class-(b) smoothers (1410, 1441, 1443) keep the slice loop on
        // the 64-sample grid for ~20 ms after the last row, which is exactly the
        // window in which a per-slice re-push would show up.
        renderQuiet(*fx, kSettleBlocks);

        const FxCounters after = fxSnapshot(*fx->proc);
        REQUIRE(after.voice == before.voice);
        REQUIRE(after.aether == before.aether);

        // --- NON-VACUITY: the sixteen values really did reach the processor ---
        // ID 1400 (Route::ENG) through FR-021's sole writer and its retained
        // Phase 9 counter ...
        REQUIRE(after.engSoftLimit == before.engSoftLimit + 1u);
        // ... the ten counted pack rows through effectsPushes_ ...
        INFO("effects pushes = " << (after.effectsPushes - before.effectsPushes));
        REQUIRE(after.effectsPushes >= before.effectsPushes + kFxCountedPackRows);
        // ... and ID 1410 through the send actually running.
        REQUIRE(after.chunks > before.chunks);

        // The three plugin-owned wander IDs have no push counter of their own, so
        // ID 1440 is witnessed through the audio it controls: at width 0 % the M/S
        // stage emits mid only, and the azimuth pair is a single per-sample scalar
        // applied identically to both channels at depth 0 - so the PRE-OUTPUT bus
        // is exactly mono once the 10 ms width smoother has snapped
        // (midside_processor.h:186-210, smoother.h:199-200).
        auto w = makeRig();
        w->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        renderQuiet(*w, 12);
        REQUIRE_FALSE(w->proc->preOutputTapTruncatedForTest());
        const float stereoBefore = maxChannelDifference(w->proc->preOutputTapLForTest(),
                                                        w->proc->preOutputTapRForTest());
        INFO("pre-output |L - R| before the width collapse = " << stereoBefore);
        REQUIRE(stereoBefore > 0.0f);  // the control: the bus IS stereo

        pushParams(*w, {{.id = Seraphis::kFxWidthId, .normalized = 0.0}});  // 0 % width
        renderQuiet(*w, 24);                            // >> the 10 ms width smoother
        REQUIRE_FALSE(w->proc->preOutputTapTruncatedForTest());
        const float stereoAfter = maxChannelDifference(w->proc->preOutputTapLForTest(),
                                                       w->proc->preOutputTapRForTest());
        INFO("pre-output |L - R| after the width collapse = " << stereoAfter);
        CHECK(stereoAfter == 0.0f);
        REQUIRE(w->checkCanaries());
    }

    // =========================================================================
    // (e) FR-007 - the send runs ONLY while it is active or draining
    //
    // THIS CLAUSE IS THE ONLY CI-GATED OBSERVATION OF FR-007. SC-012's threshold
    // is [.perf]-tagged and outside the gate, and SC-002 is structurally blind to
    // it: at mix 0 the mix loop adds fxOut[i] * 0.0f, so a fully-running send
    // leaves the bus bit-identical.
    // =========================================================================
    SECTION("(e) at the C-6 defaults the send never runs a chunk") {
        auto fx = makeRig();
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        const FxCounters before = fxSnapshot(*fx->proc);

        constexpr std::size_t kDefaultsBlocks = 40;
        renderQuiet(*fx, kDefaultsBlocks);

        const FxCounters after = fxSnapshot(*fx->proc);
        // Non-vacuity: the render really happened (the drift advance is
        // unconditional, FR-011), so `chunks == 0` is a property of the send and
        // not of an empty render.
        REQUIRE(after.driftBlocks == before.driftBlocks + kDefaultsBlocks);
        CHECK(after.chunks == std::size_t{0});
        CHECK(after.resets == std::size_t{0});
    }

    SECTION("(e) the send stops running once the drain window has ended") {
        // Geometry, in blocks: [0, kBypassBlock) active, then bypassed for the
        // whole drain window plus two blocks of margin, then a tail across which
        // the chunk counter must not move at all.
        constexpr std::size_t kBypassBlock = 40;
        constexpr std::size_t kFrozenTailBlocks = 40;
        constexpr std::size_t kTotalBlocks =
            kBypassBlock + kDrainWindowBlocks + 2u + kFrozenTailBlocks;
        static_assert(kTotalBlocks > kBypassBlock + kDrainWindowBlocks + 1u + kFrozenTailBlocks,
                      "the tail must start AFTER the last block the drain can still run on");

        auto fx = makeRig();
        fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote, 0.8f, 0);
        pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 1.0}});  // engaged from block 0

        std::size_t previous = fx->proc->sendChunkCountForTest();
        std::size_t chunksAtBypass = 0;
        std::size_t lastIncreaseBlock = 0;
        bool anyIncrease = false;

        for (std::size_t b = 0; b < kTotalBlocks; ++b) {
            if (b == kBypassBlock) {
                // EXACTLY 0 - FR-007's predicate is an exact comparison.
                pushParams(*fx, {{.id = Seraphis::kFxDelayMixId, .normalized = 0.0}});
            }
            renderQuiet(*fx, 1);

            const std::size_t now = fx->proc->sendChunkCountForTest();
            if (now > previous) {
                lastIncreaseBlock = b;
                anyIncrease = true;
            }
            if (b == kBypassBlock) {
                chunksAtBypass = now;
            }
            previous = now;
        }

        INFO("last chunk on block " << lastIncreaseBlock << ", bypass at block " << kBypassBlock);
        REQUIRE(anyIncrease);
        // It advanced while ACTIVE ...
        REQUIRE(chunksAtBypass > std::size_t{0});
        // ... it kept advancing through the drain, which is FR-009a's whole point
        // (a send cut in one sample would stop on the bypass block) ...
        CHECK(lastIncreaseBlock >= kBypassBlock);
        // ... and the LAST increment lands no later than the block on which the
        // state returns to Bypassed. fxDrainRemaining_ is armed at 96 000 on the
        // bypass block and decremented by the whole block on every DRAINING call
        // (processor.cpp:2321-2330), so the `<= 0` exit is taken on block
        // kBypassBlock + 189 = kBypassBlock + kDrainWindowBlocks + 1, and the
        // last block that can still produce a chunk is the one before it. The
        // kFxSendDrainFloor energy exit can only make this EARLIER.
        CHECK(lastIncreaseBlock <= kBypassBlock + kDrainWindowBlocks + 1u);
        // ... after which nothing runs at all: no copy into the accumulator, no
        // SpectralDelay::process, for the whole tail.
        CHECK(lastIncreaseBlock < kTotalBlocks - kFrozenTailBlocks);
        REQUIRE(fx->checkCanaries());
    }
}
